#!/usr/bin/env python3
"""Optional affine parallel scan for the prepared Gaussian channel likelihood.

This changes execution ordering, not the Gaussian inference model.  It accepts
one complete fresh observation with zero initial mean; it is not a streaming
state replacement.  Keep the serial prepared runtime as numerical reference.
No receiver selects this path automatically and no lower/tensor arithmetic is
enabled.  Physical sensitivity and admission calibration remain external.
"""
from __future__ import annotations

import math
import time

import torch

from triad_gaussian_prepared import PreparedGaussianDesign


def _sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


class PreparedGaussianScan:
    """Cached public transitions and a bounded, batch-parallel observation scan.

    Input shape is [blocks,batch,*design.leading_shape,taps].  ``score`` returns
    FP64 ``scores`` and the selected-precision ``final_mean``.  Optional
    ``means`` and ``increments`` preserve the input's block/batch ordering.
    Memory accounting includes owned public design/device copies and bounded
    scan/likelihood scratch, but is not a bound on a library's allocator cache.
    """

    def __init__(self, design: PreparedGaussianDesign, device="cpu",
                 dtype=torch.complex64, *, memory_limit_bytes=2*1024**3,
                 residual_limit=5e-7, transition_norm_limit=8.,
                 serial_below_blocks=0):
        started = time.perf_counter()
        if not isinstance(design, PreparedGaussianDesign):
            raise TypeError("scan requires a PreparedGaussianDesign")
        if dtype not in (torch.complex64, torch.complex128):
            raise ValueError("scan arithmetic must be complex64 or complex128")
        if not isinstance(memory_limit_bytes, int) or memory_limit_bytes < 1:
            raise ValueError("positive integer memory budget required")
        if (not math.isfinite(residual_limit) or residual_limit <= 0 or
                not math.isfinite(transition_norm_limit) or transition_norm_limit <= 0 or
                not isinstance(serial_below_blocks, int) or serial_below_blocks < 0):
            raise ValueError("positive finite public numerical guards required")
        self.design, self.dtype = design, dtype
        self.device = torch.device(device)
        if self.device.type == "cuda":
            if not torch.cuda.is_available():
                raise RuntimeError("requested CUDA device is unavailable")
            if self.device.index is None:
                self.device = torch.device("cuda", torch.cuda.current_device())
            if dtype == torch.complex64 and (torch.backends.cuda.matmul.allow_tf32 or
                    torch.get_float32_matmul_precision() != "highest"):
                raise ValueError("FP32 minimum requires TF32 disabled and highest matmul precision")
        self.memory_limit_bytes = memory_limit_bytes
        n, k = design.blocks, design.taps
        leading = math.prod(design.leading_shape)
        item = 8 if dtype == torch.complex64 else 16
        self.matrix_elements = n*leading*k*k
        matrix = self.matrix_elements*item
        scalars = n*leading*8+n*item
        self.owned_bytes_estimate = design.estimated_owned_bytes+3*matrix+scalars
        # Transition preparation uses FP64 PG, I-PG, prior covariance and
        # residual scratch in addition to the original prepared design.
        self.preparation_peak_bytes_estimate = self.owned_bytes_estimate+5*self.matrix_elements*16
        if self.preparation_peak_bytes_estimate > memory_limit_bytes:
            raise MemoryError("scan transition preparation exceeds memory budget")

        g, p = design.grams_cpu, design.posterior_cpu
        eye = torch.eye(k, dtype=torch.complex128)
        # Important: do this cancellation-prone subtraction BEFORE downcast.
        contraction = eye-p@g
        prior = torch.diag(torch.tensor(design.prior.stationary_power,
                                        dtype=torch.float64)).to(torch.complex128)
        q = torch.empty_like(p)
        q[0] = prior
        if n > 1:
            decay2 = design.rotations_cpu[1:].abs().square()
            weight = decay2.reshape((n-1,)+(1,)*(p.ndim-1))
            q[1:] = p[:-1]*weight+prior*(1-weight)
        # P=(I-PG)Q is an independent public covariance identity.  Relative
        # cancellation at huge observation powers can make the affine form
        # unattractive even while the maintained serial update remains usable.
        residual = (contraction@q-p).abs().amax(dim=(-2, -1))
        denominator = p.abs().amax(dim=(-2, -1))
        denominator = denominator+torch.finfo(torch.float64).eps*q.abs().amax(dim=(-2, -1))
        relative = torch.where(denominator > 0, residual/denominator.clamp_min(1e-300), residual)
        self.maximum_covariance_identity_residual = float(relative.amax())
        rotation_shape = (n,)+(1,)*(p.ndim-1)
        transitions = contraction*design.rotations_cpu.reshape(rotation_shape)
        self.maximum_transition_norm = float(torch.linalg.matrix_norm(transitions, ord=2).amax())
        reasons = []
        if not bool(torch.isfinite(transitions).all()):
            reasons.append("nonfinite public affine transition")
        if self.maximum_covariance_identity_residual > residual_limit:
            reasons.append("public covariance-identity residual exceeds scan guard")
        if self.maximum_transition_norm > transition_norm_limit:
            reasons.append("public transition norm exceeds scan guard")
        if n < serial_below_blocks:
            reasons.append("below caller-selected scan block crossover")
        self.fallback_reason = "; ".join(reasons) or None
        self.gram = g.to(device=self.device, dtype=dtype)
        self.posterior = p.to(device=self.device, dtype=dtype)
        self.transition = transitions.to(device=self.device, dtype=dtype)
        self.rotation = design.rotations_cpu.to(device=self.device, dtype=dtype)
        self.logdet = design.log_determinants_cpu.to(device=self.device, dtype=torch.float64)
        _sync(self.device)
        self.preparation_seconds = time.perf_counter()-started

    def _preflight(self, correlations):
        if not isinstance(correlations, torch.Tensor) or not correlations.is_complex():
            raise TypeError("correlations must be a complex tensor")
        expected = (self.design.blocks, *self.design.leading_shape, self.design.taps)
        if (correlations.ndim != len(expected)+1 or correlations.shape[1] < 1 or
                (correlations.shape[0], *correlations.shape[2:]) != expected):
            raise ValueError("correlations need [blocks,batch,*design_leading,taps]")
        if correlations.requires_grad:
            raise ValueError("autograd observations are unsupported")
        n, b, k = self.design.blocks, correlations.shape[1], self.design.taps
        leading = math.prod(self.design.leading_shape)
        item = 8 if self.dtype == torch.complex64 else 16
        state = n*b*leading*k*item
        # Two A buffers, two mean buffers, transferred/layout-copied inputs,
        # matrix-product temporaries, shifted prediction and likelihood terms.
        # This conservative peak also covers optional returned means/increments.
        peak = (self.owned_bytes_estimate+3*self.matrix_elements*item+
                14*state+8*n*b*leading*8)
        if peak > self.memory_limit_bytes:
            raise MemoryError("scan observation batch exceeds memory budget")
        return int(peak)

    def score(self, correlations, *, return_means=False, return_increments=False):
        """Score fresh complete captures; includes input conversion/transfers."""
        peak = self._preflight(correlations)
        started = time.perf_counter()
        r = correlations.to(device=self.device, dtype=self.dtype)
        if not bool(torch.isfinite(r).all()):
            raise ValueError("correlations contain nonfinite values")
        if self.fallback_reason:
            result = self._serial(r, return_means, return_increments)
        else:
            result = self._scan(r, return_means, return_increments)
        _sync(self.device)
        result.update(execution="serial-fallback" if self.fallback_reason else "affine-parallel-scan",
                      fallback_reason=self.fallback_reason,
                      score_seconds=time.perf_counter()-started,
                      preparation_seconds=self.preparation_seconds,
                      peak_owned_bytes_estimate=peak)
        if not bool(torch.isfinite(result["scores"]).all()) or not bool(torch.isfinite(result["final_mean"]).all()):
            raise FloatingPointError("nonfinite scan likelihood or posterior mean")
        return result

    def _serial(self, r, return_means, return_increments):
        runtime = self.design.to(self.device, self.dtype, check_inputs=False)
        means, increments = [], []
        previous = torch.zeros(r.shape[1:-1], dtype=torch.float64, device=self.device)
        for block in r:
            score = runtime.update(block)
            if return_means:
                means.append(runtime.mean)
            if return_increments:
                increments.append(score-previous)
                previous = score
        result = dict(scores=runtime.scores(), final_mean=runtime.mean)
        if return_means:
            result["means"] = torch.stack(means)
        if return_increments:
            result["increments"] = torch.stack(increments)
        return result

    def _scan(self, r, return_means, return_increments):
        # One matrix per public design, NOT one matrix per independent trial.
        # Move trials to matrix columns: [N,*leading,K,batch].
        rhs = r.movedim(1, -1).contiguous()
        means = self.posterior@rhs
        transition = self.transition.clone()
        next_mean = torch.empty_like(means)
        next_transition = torch.empty_like(transition)
        distance = 1
        while distance < self.design.blocks:
            next_mean[:distance] = means[:distance]
            next_transition[:distance] = transition[:distance]
            next_mean[distance:] = (means[distance:]+
                transition[distance:]@means[:-distance])
            next_transition[distance:] = transition[distance:]@transition[:-distance]
            means, next_mean = next_mean, means
            transition, next_transition = next_transition, transition
            distance *= 2
        # Inclusive means are posterior means. The likelihood MUST use the
        # previous posterior; reusing the current one counts observations twice.
        predicted = torch.zeros_like(means)
        predicted[1:] = means[:-1]
        rotation_shape = (self.design.blocks,)+(1,)*(predicted.ndim-1)
        predicted *= self.rotation.reshape(rotation_shape)
        g_mean = self.gram@predicted
        difference = rhs-g_mean
        adjustment = self.posterior@difference
        linear = 2*(predicted.conj()*rhs).sum(-2).real
        predicted_energy = (predicted.conj()*g_mean).sum(-2).real
        innovation_energy = (difference.conj()*adjustment).sum(-2).real
        increment = (linear.to(torch.float64)-predicted_energy.to(torch.float64)+
                     innovation_energy.to(torch.float64)-self.logdet[..., None])
        result = dict(scores=increment.sum(0).movedim(-1, 0),
                      final_mean=means[-1].movedim(-1, 0))
        if return_means:
            result["means"] = means.movedim(-1, 1)
        if return_increments:
            result["increments"] = increment.movedim(-1, 1)
        return result

    def diagnostics(self):
        return dict(blocks=self.design.blocks, taps=self.design.taps,
            leading_shape=list(self.design.leading_shape), device=str(self.device),
            state_dtype=str(self.dtype), likelihood_dtype="torch.float64",
            transition_preparation="CPU complex128; (I-PG)F before downcast",
            maximum_covariance_identity_residual=self.maximum_covariance_identity_residual,
            maximum_transition_norm=self.maximum_transition_norm,
            fallback_reason=self.fallback_reason,
            preparation_seconds=self.preparation_seconds,
            owned_bytes_estimate=self.owned_bytes_estimate,
            preparation_peak_bytes_estimate=self.preparation_peak_bytes_estimate,
            scope="Fresh complete zero-initial-mean captures; reordered finite arithmetic; no automatic receiver selection")

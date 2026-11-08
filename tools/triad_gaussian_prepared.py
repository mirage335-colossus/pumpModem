#!/usr/bin/env python3
"""Prepared-design implementation of the triad Gaussian state likelihood.

For a fixed public Gram sequence and elapsed-time calendar, covariance
prediction, eigendecomposition, Cholesky, posterior covariance and log
determinant do not depend on the observed correlations. This module computes
those quantities once in CPU complex128 and reuses them for multiple
observations. The online mean and likelihood updates still consume every
observation. Finite-precision results are an implementation variant, not an
exactly equivalent numerical computation.
"""
from __future__ import annotations

import math
import time
from typing import Sequence

import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter


class PreparedGaussianDesign:
    """FP64 public design tensors, independent of received data."""

    def __init__(self, grams: torch.Tensor, elapsed_seconds: Sequence[float],
                 prior: GaussianPrior, *, memory_limit_bytes: int = 4*1024**3):
        if not isinstance(grams, torch.Tensor) or not grams.is_complex():
            raise TypeError("grams must be a complex tensor")
        if grams.ndim < 3 or grams.shape[-1] != grams.shape[-2]:
            raise ValueError("grams must have shape [blocks,...,taps,taps]")
        if len(elapsed_seconds) != grams.shape[0]:
            raise ValueError("elapsed-time count must equal the Gram block count")
        if grams.shape[-1] != len(prior.stationary_power):
            raise ValueError("Gram tap extent differs from the prior")
        if memory_limit_bytes < 1:
            raise ValueError("memory limit must be positive")

        # Include the temporary posterior list and the stacked destination in
        # peak preparation memory, in addition to the retained Grams. Check
        # this before constructing outputs.
        blocks, taps = int(grams.shape[0]), int(grams.shape[-1])
        if blocks == 0:
            raise ValueError("prepared design requires at least one block")
        leading = math.prod(grams.shape[1:-2])
        complex_bytes = blocks * leading * taps * taps * 16
        scalar_bytes = blocks * leading * 8 + blocks * 16
        estimate = 3*complex_bytes + scalar_bytes
        if estimate > memory_limit_bytes:
            raise MemoryError(f"prepared design needs {estimate} owned bytes, over limit {memory_limit_bytes}")

        g_cpu = grams.detach().to(device="cpu", dtype=torch.complex128).contiguous()
        if not bool(torch.isfinite(g_cpu).all()):
            raise ValueError("Gram design contains nonfinite values")
        asym = (g_cpu-g_cpu.mH).abs().amax(dim=(-2, -1))
        scale = g_cpu.abs().amax(dim=(-2, -1)).clamp_min(1.)
        if bool((asym > 1e-10*scale).any()):
            raise ValueError("Gram design is not Hermitian")
        g_cpu = (g_cpu+g_cpu.mH)*.5
        eig = torch.linalg.eigvalsh(g_cpu)
        if bool((eig[..., 0] < -1e-10*scale).any()):
            raise ValueError("Gram design is not positive semidefinite")

        dts = tuple(float(value) for value in elapsed_seconds)
        if any(not math.isfinite(dt) or dt < 0 for dt in dts):
            raise ValueError("elapsed seconds must be finite and nonnegative")
        self.prior = prior
        self.dts = dts
        self.grams_cpu = g_cpu
        self.posterior_cpu = []
        self.log_determinants_cpu = []
        self.rotations = []
        self.covariance_minima = []
        k = taps
        power = torch.diag(torch.tensor(prior.stationary_power, dtype=torch.float64,
                                        device="cpu")).to(torch.complex128)
        covariance = power.expand(g_cpu.shape[1:]).clone()
        for index, (gram, dt) in enumerate(zip(g_cpu, dts)):
            if index:
                if math.isinf(prior.coherence_seconds):
                    decay, innovation = 1., 0.
                else:
                    decay = math.exp(-dt/prior.coherence_seconds)
                    innovation = -math.expm1(-2*dt/prior.coherence_seconds)
                covariance = covariance*(decay*decay)+power*innovation
            covariance = (covariance+covariance.mH)*.5
            values, vectors = torch.linalg.eigh(covariance)
            scale_cov = covariance.abs().amax(dim=(-2, -1)).clamp_min(1.)
            if bool((values[..., 0] < -1e-10*scale_cov).any()):
                raise ValueError("prepared prior covariance lost positive semidefiniteness")
            self.covariance_minima.append(float((values[..., 0]/scale_cov).amin()))
            factor = vectors*values.clamp_min(0.).sqrt()[..., None, :]
            eye = torch.eye(k, dtype=torch.complex128)
            info = eye+factor.mH@gram@factor
            info = (info+info.mH)*.5
            chol, status = torch.linalg.cholesky_ex(info, check_errors=False)
            if bool((status != 0).any()):
                raise ValueError("prepared innovation information is not positive definite")
            posterior = factor@torch.cholesky_solve(factor.mH, chol)
            posterior = (posterior+posterior.mH)*.5
            logdet = 2*chol.diagonal(dim1=-2, dim2=-1).real.log().sum(-1)
            self.posterior_cpu.append(posterior.contiguous())
            self.log_determinants_cpu.append(logdet.contiguous())
            phase_cycles = prior.frequency_hz*dt
            if not math.isfinite(phase_cycles):
                raise ValueError("frequency/time product overflow")
            self.rotations.append(_rotation(prior, dt))
            covariance = posterior

        self.posterior_cpu = torch.stack(self.posterior_cpu).contiguous()
        self.log_determinants_cpu = torch.stack(self.log_determinants_cpu).contiguous()
        self.rotations_cpu = torch.tensor(self.rotations, dtype=torch.complex128)
        self.memory_limit_bytes = memory_limit_bytes
        self.estimated_owned_bytes = int(self.grams_cpu.numel()*16 +
            self.posterior_cpu.numel()*16 + self.log_determinants_cpu.numel()*8 +
            self.rotations_cpu.numel()*16)
        if self.estimated_owned_bytes > memory_limit_bytes:
            raise MemoryError("prepared tensors exceed the declared memory limit")

    @property
    def blocks(self):
        return len(self.dts)

    @property
    def taps(self):
        return len(self.prior.stationary_power)

    @property
    def leading_shape(self):
        return tuple(self.grams_cpu.shape[1:-2])

    def to(self, device="cpu", dtype=torch.complex128, *, check_inputs=True) -> "PreparedGaussianRuntime":
        if dtype not in (torch.complex64, torch.complex128):
            raise ValueError("prepared Gaussian state must be complex64 or complex128")
        dev = torch.device(device)
        if dtype == torch.complex64 and dev.type == "cuda":
            if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
                raise ValueError("FP32 mode requires TF32 disabled and highest matmul precision")
        if dev.type == "cuda" and dev.index is None:
            dev = torch.device("cuda", torch.cuda.current_device())
        return PreparedGaussianRuntime(self, dev, dtype, check_inputs=check_inputs)


def _rotation(prior: GaussianPrior, dt: float) -> complex:
    import cmath
    decay = math.exp(-dt/prior.coherence_seconds) if not math.isinf(prior.coherence_seconds) else 1.
    cycles = prior.frequency_hz*dt
    if not math.isfinite(cycles):
        raise ValueError("frequency/time product overflow")
    return cmath.rect(decay, 2*math.pi*math.remainder(cycles, 1.))


class PreparedGaussianRuntime:
    """Online observation state for one fixed prepared design."""

    def __init__(self, design: PreparedGaussianDesign, device: torch.device,
                 dtype: torch.dtype, *, check_inputs=True):
        self.design, self.device, self.dtype = design, device, dtype
        self.check_inputs = bool(check_inputs)
        self.grams = design.grams_cpu.to(device=device, dtype=dtype)
        self.posterior = design.posterior_cpu.to(device=device, dtype=dtype)
        self.logdet = design.log_determinants_cpu.to(device=device, dtype=torch.float64)
        self.rotation = design.rotations_cpu.to(device=device, dtype=dtype)
        power = torch.tensor(design.prior.stationary_power,
                             dtype=torch.float64 if dtype == torch.complex128 else torch.float32)
        self.mean = None
        self.score = None
        self.observation_shape = None
        self.blocks = 0
        self._power = power

    def reset(self):
        """Reset observation state while retaining resident prepared tensors."""
        self.mean = None
        self.score = None
        self.observation_shape = None
        self.blocks = 0

    def update(self, correlations: torch.Tensor):
        if not isinstance(correlations, torch.Tensor) or not correlations.is_complex():
            raise TypeError("correlations must be a complex tensor")
        expected_ndim = len(self.design.leading_shape)+2
        if correlations.ndim != expected_ndim or correlations.shape[0] < 1:
            raise ValueError("correlations require [batch, *design_leading, taps] with a positive batch")
        if correlations.shape[-1] != self.design.taps:
            raise ValueError("correlation tap extent differs from the design")
        if tuple(correlations.shape[1:-1]) != self.design.leading_shape:
            raise ValueError("correlation leading extents differ from the design")
        if correlations.device.type != self.device.type or correlations.device != self.device:
            raise ValueError("correlations must already be on the prepared device")
        if correlations.requires_grad:
            raise ValueError("autograd observations are unsupported")
        r = correlations.to(dtype=self.dtype)
        if self.check_inputs and not bool(torch.isfinite(r).all()):
            raise ValueError("correlations contain nonfinite values")
        if self.blocks >= self.design.blocks:
            raise ValueError("more observations than prepared blocks")
        i = self.blocks
        if self.observation_shape is None:
            self.observation_shape = tuple(r.shape)
        elif tuple(r.shape) != self.observation_shape:
            raise ValueError("correlation batch or leading shape changed")
        gram, posterior = self.grams[i], self.posterior[i]
        if self.mean is None:
            self.mean = torch.zeros_like(r)
            self.score = torch.zeros(r.shape[:-1], dtype=torch.float64, device=self.device)
        predicted = self.mean*self.rotation[i]
        g_mean = (gram @ predicted[..., None]).squeeze(-1)
        difference = r-g_mean
        adjustment = (posterior @ difference[..., None]).squeeze(-1)
        linear = 2*(predicted.conj()*r).sum(-1).real
        predicted_energy = (predicted.conj()*g_mean).sum(-1).real
        innovation_energy = (difference.conj()*adjustment).sum(-1).real
        # Match the reference's sensitive-reduction policy: cast each score
        # component to FP64 before subtracting/adding them.
        increment = (linear.to(torch.float64)-predicted_energy.to(torch.float64)+
                     innovation_energy.to(torch.float64)-self.logdet[i])
        self.mean = predicted+adjustment
        self.score = self.score+increment
        self.blocks += 1
        return self.score

    def scores(self):
        if self.score is None:
            raise ValueError("no correlations have been observed")
        return self.score

    def diagnostics(self):
        return {"blocks": self.blocks, "taps": self.design.taps,
                "state_dtype": str(self.dtype), "log_likelihood_dtype": "torch.float64",
                "design_dtype": "complex128 CPU preparation", "device": str(self.device),
                "design_owned_bytes": self.design.estimated_owned_bytes,
                "input_checks":self.check_inputs,
                "execution": "precomputed Gram/covariance factors; per-observation mean and likelihood update",
                "precision_scope": "finite precision; paired numerical validation required"}


def prepare(grams, elapsed_seconds, prior, *, memory_limit_bytes=4*1024**3):
    return PreparedGaussianDesign(grams, elapsed_seconds, prior,
                                  memory_limit_bytes=memory_limit_bytes)


def filter(design, correlations, *, device="cpu", dtype=torch.complex128,
           check_inputs=True):
    """Run [blocks,batch,*design_leading,taps], returning FP64 log evidence."""
    if correlations.shape[0] != design.blocks:
        raise ValueError("correlation block count differs from the design")
    runtime = design.to(device, dtype, check_inputs=check_inputs)
    for index in range(design.blocks):
        runtime.update(correlations[index:index+1])
    return runtime.scores(), runtime.diagnostics()

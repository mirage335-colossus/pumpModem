#!/usr/bin/env python3
"""Prepared independent-window Gaussian channel likelihoods.

The receiver-facing statistics are r=A.H@y/v_ref and G=A.H@A/v_ref.
Within a public window the coefficient prior has covariance
Q[(j,k),(l,m)] = exp(-(pi*B*(t_j-t_l))**2/2) * power[k] * 1[k=m].
B is twice the RMS Doppler of the ideal Gaussian coefficient spectrum;
nominal ray Dopplers already represented in A must not be added again.

Windows are independent in this ALTERNATIVE model. Physical Gaussian-FIR
gains generally correlate across windows and evolve within retained blocks.
Those approximation losses require independent sampling/window refinement.
The module never reads waveform means, signal energy, bit truth or noise truth.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

import numpy as np
import torch


@dataclass(frozen=True)
class _Window:
    first: int
    stop: int
    transform: np.ndarray  # [candidate, whitened block/tap, covariance mode]
    eigenvalues: np.ndarray  # [candidate, covariance mode]


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + " must be a positive integer")
    return value


def _array(value, name):
    if isinstance(value, torch.Tensor):
        if value.requires_grad:
            raise ValueError(name + " must not require gradients")
        return value.detach().cpu().numpy()
    if not isinstance(value, np.ndarray):
        raise TypeError(name + " must be a NumPy array or Torch tensor")
    return value


class PreparedWindowedGaussian:
    """Prepare fixed Grams once; score many conditional noisy trials cheaply.

    Grams have shape [blocks,candidates,taps,taps]. Correlations have shape
    [blocks,trials,candidates,taps], or [blocks,candidates,taps] for one trial.
    Both use one FIXED receiver reference variance. ``variance_scale`` is a
    positive scalar, [trials], [trials,1], or [trials,candidates], constant
    through the complete observation. It is the chosen noise/reference ratio.
    Use ``correlations_scaled=True`` when r was already divided by that ratio.

    A receiver may use an independent whole-bank residual variance upper bound
    as variance_scale. Its Gaussian assumptions and failure-budget allocation
    belong to the caller. Conditioned on an upper bound covering true stationary
    white noise, these normalized Gaussian integrals have null expectation <=1
    in exact arithmetic. Numerical residual guards and regression checks do not
    constitute a floating-point rare-event proof.
    Independence of the alternative's windows does not assert independence of
    the physical signal. No threshold, anytime rule or physical-end rule is
    supplied here. A different chosen scale for each BLOCK is unsupported by
    this stationary-scaling reuse interface.

    Preparation, small block whitening/scaling and evidence reductions use
    FP64. Optional FP32 covers the bounded, whitened-observation/mode GEMM.
    Caller observations, framework and solver workspaces are outside the explicit-array memory bound.
    """

    def __init__(self, grams, centers_seconds, *, doppler_width_hz: float,
                 window_blocks: int = 16, stationary_power=(.5, .5),
                 relative_tolerance: float = 1e-10,
                 memory_limit_bytes: int = 256 * 1024 * 1024):
        self.window_blocks = _positive_integer(window_blocks, "window_blocks")
        self.memory_limit_bytes = _positive_integer(memory_limit_bytes, "memory_limit_bytes")
        if not math.isfinite(doppler_width_hz) or doppler_width_hz < 0:
            raise ValueError("Gaussian Doppler width must be nonnegative finite")
        if not math.isfinite(relative_tolerance) or not 0 < relative_tolerance < .01:
            raise ValueError("invalid covariance tolerance")
        self.doppler_width_hz = float(doppler_width_hz)
        self.relative_tolerance = float(relative_tolerance)
        shape = tuple(grams.shape) if hasattr(grams, "shape") else ()
        if len(shape) != 4 or shape[-1] != shape[-2] or min(shape) < 1:
            raise ValueError("Grams require shape [blocks,candidates,taps,taps]")
        self.blocks, self.candidates, self.taps = shape[:3]
        if self.taps > 16:
            raise ValueError("Gaussian window supports at most 16 taps")
        dimension = min(self.window_blocks, self.blocks) * self.taps
        if dimension > 2048:
            raise ValueError("Gaussian window dimension exceeds 2048")
        powers = tuple(float(p) for p in stationary_power)
        if len(powers) != self.taps or any(not math.isfinite(p) or p < 0 for p in powers):
            raise ValueError("tap powers must match the nonnegative finite tap prior")
        self.stationary_power = powers
        full, remainder = divmod(self.blocks, self.window_blocks)
        squares = full * self.window_blocks**2 + remainder**2
        # Factors/eigenvalues are retained; all dense preparation temporaries
        # are bounded by the largest window rather than observation duration.
        stored = self.candidates * squares * self.taps**2 * 16
        stored += self.candidates * self.blocks * self.taps * 8
        input_and_checks = 14 * math.prod(shape) * 16
        temporary = 24 * dimension**2 * 16
        prior_key_bytes = 2 * min(self.window_blocks, self.blocks) * 8
        stored += math.prod(shape) * 16  # Cached small observation whiteners.
        self.preparation_array_bound_bytes = stored + input_and_checks + temporary + self.blocks * 8 + prior_key_bytes
        if self.preparation_array_bound_bytes > self.memory_limit_bytes:
            raise MemoryError("Gaussian window preparation exceeds explicit-array memory limit")

        G = np.array(_array(grams, "Grams"), dtype=np.complex128, copy=True)
        centers = np.array(_array(centers_seconds, "centers"), dtype=np.float64, copy=True)
        if centers.shape != (self.blocks,) or not np.isfinite(centers).all():
            raise ValueError("one finite physical center is required per retained block")
        if self.blocks > 1 and np.any(np.diff(centers) <= 0):
            raise ValueError("physical block centers must increase strictly")
        if not np.isfinite(G).all():
            raise ValueError("nonfinite observation Gram")
        scale = np.maximum(np.max(abs(G), axis=(-2, -1)), np.finfo(float).tiny)
        if np.any(np.max(abs(G - G.conj().swapaxes(-1, -2)), axis=(-2, -1)) > relative_tolerance * scale):
            raise ValueError("observation Gram is not Hermitian")
        G = (G + G.conj().swapaxes(-1, -2)) * .5
        values, vectors = np.linalg.eigh(G)
        if np.any(values[..., 0] < -relative_tolerance * scale):
            raise ValueError("observation Gram is not positive semidefinite")
        self.maximum_relative_gram_repair = float(np.max(np.maximum(-values[..., 0], 0) / scale))
        # Project only unresolved observation modes. Unlike truncating Q,
        # this is an explicit lower-dimensional observation experiment. The
        # same retained modes define its whitening and signal covariance.
        values = np.maximum(values, 0)
        cutoff = relative_tolerance * np.maximum(values[..., -1:], np.finfo(float).tiny)
        keep_observation = values > cutoff
        retained_values = np.where(keep_observation, values, 0.)
        inverse_root = np.zeros_like(values)
        np.divide(1., np.sqrt(values), out=inverse_root, where=keep_observation)
        self._whitener = inverse_root[..., :, None] * vectors.conj().swapaxes(-1, -2)
        gram_root = np.sqrt(retained_values)[..., :, None] * vectors.conj().swapaxes(-1, -2)
        covariance = self._whitener @ G @ self._whitener.conj().swapaxes(-1, -2)
        expected = np.zeros_like(covariance)
        diagonal = np.arange(self.taps)
        expected[..., diagonal, diagonal] = keep_observation
        self.maximum_whitening_covariance_error = float(np.max(abs(covariance - expected)))
        self.whitening_error_limit = 8 * np.finfo(float).eps / relative_tolerance
        if self.maximum_whitening_covariance_error > self.whitening_error_limit:
            raise ValueError("observation whitening is numerically unresolved")
        # The checked maximum-entry residual bounds the covariance operator
        # error by taps*limit. Inflate conservatively and scale the observation
        # and alternative together; never change only a denominator or a mode.
        self.whitening_variance_inflation = math.nextafter(
            1 + self.taps * self.whitening_error_limit, math.inf)
        if not math.isfinite(self.whitening_variance_inflation):
            raise ValueError("observation rank tolerance cannot resolve a finite noise envelope")
        safety_root = math.sqrt(self.whitening_variance_inflation)
        self._whitener /= safety_root
        gram_root /= safety_root
        self.observable_rank_min = int(keep_observation.sum(-1).min())
        self.observable_rank_max = int(keep_observation.sum(-1).max())
        self.discarded_observation_modes = int(np.sum(~keep_observation))
        removed = np.where(keep_observation, 0., values).sum(-1)
        self.maximum_relative_discarded_gram_trace = float(np.max(
            removed / np.maximum(values.sum(-1), np.finfo(float).tiny)))
        self.maximum_relative_prior_repair = 0.
        self.maximum_relative_information_repair = 0.
        self.maximum_mode_orthogonality_error = 0.
        windows = []
        prior_time_key = None
        root = None
        for first in range(0, self.blocks, self.window_blocks):
            stop = min(first + self.window_blocks, self.blocks)
            time = centers[first:stop] - centers[first]
            time_key = time.tobytes()
            # Width, powers and tolerance are fixed within this constructor.
            # Reuse only the immediately preceding, exactly identical calendar;
            # irregular windows and partial tails never grow an unbounded cache.
            if time_key != prior_time_key:
                root = None  # Release the previous dense factor before replacing it.
                if self.doppler_width_hz == 0:
                    # Exactly constant taps have rank <= taps, even for long
                    # windows. An eigensolve invents tiny positive modes here.
                    root = np.tile(np.diag(np.sqrt(powers)), (len(time), 1))
                    root = root[:, np.asarray(powers) > 0]
                    prior_time_key = time_key
                else:
                    argument = math.pi * self.doppler_width_hz * (time[:, None] - time[None, :])
                    with np.errstate(over="ignore"):
                        correlation = np.exp(-.5 * argument**2)
                    Q = np.kron(correlation, np.diag(powers))
                    q, u = np.linalg.eigh(Q)
                    qscale = max(float(abs(q).max()), np.finfo(float).tiny)
                    if q[0] < -relative_tolerance * qscale:
                        raise ValueError("Gaussian prior covariance lost positive semidefiniteness")
                    self.maximum_relative_prior_repair = max(self.maximum_relative_prior_repair, max(0., -float(q[0])) / qscale)
                    keep = q > 0
                    root = u[:, keep] * np.sqrt(q[keep])[None, :]
                    prior_time_key = time_key
                    del argument, correlation, Q, q, u, keep
            n, rank = root.shape
            transform = np.empty((self.candidates, n, rank), np.complex128)
            spectrum = np.empty((self.candidates, rank), np.float64)
            for candidate in range(self.candidates):
                if rank:
                    weighted = np.empty((n, rank), np.complex128)
                    for local, block_root in enumerate(gram_root[first:stop, candidate]):
                        lo = local * self.taps
                        weighted[lo:lo + self.taps] = block_root @ root[lo:lo + self.taps]
                    # Do not square this design into an information Gram:
                    # eigensolver clipping can leave a nonzero transform with
                    # zero modeled noise energy at extreme noise scales.
                    modes, singular, _ = np.linalg.svd(weighted, full_matrices=False)
                    spectrum[candidate] = singular**2
                    transform[candidate] = modes
                    error = float(np.max(abs(modes.conj().T @ modes - np.eye(rank))))
                    self.maximum_mode_orthogonality_error = max(self.maximum_mode_orthogonality_error, error)
                    if error > 64 * np.finfo(float).eps * max(n, 1):
                        raise ValueError("Gaussian covariance modes are numerically unresolved")
            windows.append(_Window(first, stop, transform, spectrum))
        self._windows = tuple(windows)
        self.owned_factor_bytes = self._whitener.nbytes + sum(w.transform.nbytes + w.eigenvalues.nbytes for w in windows)
        self.maximum_window_dimension = dimension
        self.maximum_window_rank = max(w.transform.shape[-1] for w in windows)

    def score(self, correlations, *, variance_scale=1., correlations_scaled: bool = False,
              device="cpu", precision="fp64"):
        """Return FP64 [trials,candidates], or [candidates] for a single trial."""
        if not isinstance(correlations, (np.ndarray, torch.Tensor)):
            raise TypeError("correlations must be a NumPy array or Torch tensor")
        if isinstance(correlations, torch.Tensor) and correlations.requires_grad:
            raise ValueError("correlations must not require gradients")
        if not isinstance(correlations_scaled, bool):
            raise ValueError("correlations_scaled must be boolean")
        shape = tuple(correlations.shape)
        single = len(shape) == 3
        if single:
            correlations = correlations[:, None]
            shape = tuple(correlations.shape)
        if len(shape) != 4 or shape[0] != self.blocks or shape[2:] != (self.candidates, self.taps) or shape[1] < 1:
            raise ValueError("correlations require [blocks,trials,candidates,taps]")
        if precision not in ("fp32", "fp64"):
            raise ValueError("correlation GEMM precision must be fp32 or fp64")
        target = torch.device(device)
        if target.type not in ("cpu", "cuda"):
            raise ValueError("device must be CPU or CUDA")
        if target.type == "cuda" and precision == "fp32":
            if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
                raise ValueError("FP32 minimum requires TF32 disabled and highest matmul precision")
        trials = shape[1]
        temporary = 160 * trials * self.candidates * self.maximum_window_dimension
        temporary += 64 * self.candidates * self.maximum_window_dimension**2
        temporary += 64 * trials * self.candidates
        self.last_score_array_bound_bytes = self.owned_factor_bytes + temporary
        if self.last_score_array_bound_bytes > self.memory_limit_bytes:
            raise MemoryError("Gaussian window scoring exceeds explicit-array memory limit")
        if isinstance(variance_scale, torch.Tensor) and variance_scale.requires_grad:
            raise ValueError("variance scale must not require gradients")
        alpha = torch.as_tensor(variance_scale, dtype=torch.float64, device=target)
        if alpha.ndim == 1:
            if alpha.shape != (trials,):
                raise ValueError("one stationary variance scale is required per trial")
            alpha = alpha[:, None]
        try:
            alpha = torch.broadcast_to(alpha, (trials, self.candidates))
        except RuntimeError as error:
            raise ValueError("stationary variance scale does not match trials/candidates") from error
        if not bool(torch.isfinite(alpha).all()) or bool((alpha <= 0).any()):
            raise ValueError("stationary variance scales must be positive finite")
        dtype = torch.complex64 if precision == "fp32" else torch.complex128
        total = torch.zeros((trials, self.candidates), dtype=torch.float64, device=target)
        for window in self._windows:
            observation = torch.as_tensor(correlations[window.first:window.stop], dtype=torch.complex128, device=target)
            if not bool(torch.isfinite(observation).all()):
                raise ValueError("nonfinite Gaussian window observations")
            whitener = torch.as_tensor(self._whitener[window.first:window.stop],
                                       dtype=torch.complex128, device=target)
            white = torch.matmul(observation.permute(0, 2, 1, 3),
                                 whitener.transpose(-1, -2)).permute(0, 2, 1, 3)
            scale = torch.sqrt(alpha)[None, :, :, None]
            white = white * scale if correlations_scaled else white / scale
            # Cast only after whitening and noise scaling: the bulk operands
            # then have unit noise scale instead of amplifying tiny null modes.
            flat = white.to(dtype).permute(2, 1, 0, 3).reshape(self.candidates, trials, -1)
            factor = torch.as_tensor(window.transform, dtype=dtype, device=target)
            z = torch.bmm(flat, factor.conj()).permute(1, 0, 2)
            power = z.real.to(torch.float64).square() + z.imag.to(torch.float64).square()
            lam = torch.as_tensor(window.eigenvalues, dtype=torch.float64, device=target)[None]
            denominator = alpha[..., None] + lam
            quadratic = (power * (lam / denominator)).sum(-1)
            determinant = torch.log1p(lam / alpha[..., None]).sum(-1)
            total += quadratic - determinant
        if not bool(torch.isfinite(total).all()):
            raise ValueError("Gaussian window likelihood became nonfinite")
        return total[0] if single else total

    def diagnostics(self):
        return {
            "blocks": self.blocks, "candidates": self.candidates, "taps": self.taps,
            "window_blocks": self.window_blocks, "windows": len(self._windows),
            "doppler_width_hz": self.doppler_width_hz,
            "stationary_power": list(self.stationary_power),
            "maximum_window_dimension": self.maximum_window_dimension,
            "maximum_window_rank": self.maximum_window_rank,
            "owned_factor_bytes": self.owned_factor_bytes,
            "preparation_array_bound_bytes": self.preparation_array_bound_bytes,
            "maximum_relative_gram_repair": self.maximum_relative_gram_repair,
            "maximum_relative_prior_repair": self.maximum_relative_prior_repair,
            "maximum_relative_information_repair": self.maximum_relative_information_repair,
            "observable_rank_min": self.observable_rank_min,
            "observable_rank_max": self.observable_rank_max,
            "discarded_observation_modes": self.discarded_observation_modes,
            "observable_relative_rank_tolerance": self.relative_tolerance,
            "maximum_relative_discarded_gram_trace": self.maximum_relative_discarded_gram_trace,
            "maximum_whitening_covariance_error": self.maximum_whitening_covariance_error,
            "whitening_error_limit": self.whitening_error_limit,
            "whitening_variance_inflation": self.whitening_variance_inflation,
            "maximum_mode_orthogonality_error": self.maximum_mode_orthogonality_error,
            "preparation_arithmetic": "CPU complex128/float64 observation whitening, PSD prior roots and rectangular SVD; exact constant-tap root at B=0",
            "score_arithmetic": "FP64 small block whitening/scaling on selected device; explicit complex64/128 bounded mode GEMM; FP64 weights, squares, determinant and evidence",
            "rank_scope": "fixed projection removes observation Gram eigenmodes <= relative tolerance times block maximum; no extra truncation of positive prior modes; omitted information and numerical residuals are reported",
            "variance_scope": "one stationary chosen variance/reference ratio per trial/candidate, not per block; independent bound and its failure budget are caller responsibilities",
            "model_scope": "normalized independent-window proper-complex Gaussian coefficient alternative with physical-center Gaussian covariance; not a full matched Gaussian-FIR receiver or anytime admission rule",
            "sampling_scope": "block-center coefficient approximation and independent-window boundaries require physical sampling/window refinement",
            "memory_scope": "explicit owned factors and per-window arrays only; caller observations and framework/solver workspace excluded",
        }


def prepare_windowed_gaussian(grams, centers_seconds, **kwargs):
    return PreparedWindowedGaussian(grams, centers_seconds, **kwargs)

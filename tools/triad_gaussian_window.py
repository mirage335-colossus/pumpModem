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
    transform: np.ndarray  # [candidate, flattened block/tap, covariance mode]
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
    white noise, these normalized Gaussian integrals have null expectation <=1.
    Independence of the alternative's windows does not assert independence of
    the physical signal. No threshold, anytime rule or physical-end rule is
    supplied here. A different chosen scale for each BLOCK is unsupported by
    this stationary-scaling reuse interface.

    Preparation/eigenvalues and evidence reductions use FP64. Optional FP32
    covers correlation/transform GEMM only. Caller observations, framework and
    solver workspaces are outside the explicit-array memory bound.
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
        input_and_checks = 6 * math.prod(shape) * 16
        temporary = 24 * dimension**2 * 16
        prior_key_bytes = 2 * min(self.window_blocks, self.blocks) * 8
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
        if np.any(values < 0):
            G = (vectors * np.maximum(values, 0)[..., None, :]) @ vectors.conj().swapaxes(-1, -2)
        self.maximum_relative_prior_repair = 0.
        self.maximum_relative_information_repair = 0.
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
                block_gram = np.zeros((n, n), np.complex128)
                for local, gram in enumerate(G[first:stop, candidate]):
                    lo = local * self.taps
                    block_gram[lo:lo + self.taps, lo:lo + self.taps] = gram
                information = root.conj().T @ block_gram @ root
                information = (information + information.conj().T) * .5
                if rank:
                    lam, rotation = np.linalg.eigh(information)
                    lscale = max(float(abs(lam).max()), np.finfo(float).tiny)
                    if lam[0] < -relative_tolerance * lscale:
                        raise ValueError("Gaussian information lost positive semidefiniteness")
                    self.maximum_relative_information_repair = max(self.maximum_relative_information_repair, max(0., -float(lam[0])) / lscale)
                    spectrum[candidate] = np.maximum(lam, 0)
                    transform[candidate] = root @ rotation
            windows.append(_Window(first, stop, transform, spectrum))
        self._windows = tuple(windows)
        self.owned_factor_bytes = sum(w.transform.nbytes + w.eigenvalues.nbytes for w in windows)
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
            observation = torch.as_tensor(correlations[window.first:window.stop], dtype=dtype, device=target)
            if not bool(torch.isfinite(observation).all()):
                raise ValueError("nonfinite Gaussian window observations")
            flat = observation.permute(2, 1, 0, 3).reshape(self.candidates, trials, -1)
            factor = torch.as_tensor(window.transform, dtype=dtype, device=target)
            z = torch.bmm(flat, factor.conj()).permute(1, 0, 2)
            power = z.real.to(torch.float64).square() + z.imag.to(torch.float64).square()
            lam = torch.as_tensor(window.eigenvalues, dtype=torch.float64, device=target)[None]
            denominator = alpha[..., None] + lam
            if correlations_scaled:
                quadratic = (power * (alpha[..., None] / denominator)).sum(-1)
            else:
                quadratic = ((power / alpha[..., None]) / denominator).sum(-1)
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
            "preparation_arithmetic": "CPU complex128/float64 PSD square roots and information eigensolves; no covariance inverse",
            "score_arithmetic": "explicit complex64/128 correlation GEMM; FP64 weights, squares, determinant and evidence",
            "variance_scope": "one stationary chosen variance/reference ratio per trial/candidate, not per block; independent bound and its failure budget are caller responsibilities",
            "model_scope": "normalized independent-window proper-complex Gaussian coefficient alternative with physical-center Gaussian covariance; not a full matched Gaussian-FIR receiver or anytime admission rule",
            "sampling_scope": "block-center coefficient approximation and independent-window boundaries require physical sampling/window refinement",
            "memory_scope": "explicit owned factors and per-window arrays only; caller observations and framework/solver workspace excluded",
        }


def prepare_windowed_gaussian(grams, centers_seconds, **kwargs):
    return PreparedWindowedGaussian(grams, centers_seconds, **kwargs)

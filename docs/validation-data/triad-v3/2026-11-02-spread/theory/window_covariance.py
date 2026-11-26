"""Standalone covariance spectra for the retained independent-window model.

Mathematical preparation extracted from tools/triad_gaussian_window.py at
f13e3bc9 (SHA256 dd5d8083999a97f66f74673264f80117b06557c45b3f4646a88e9668a29d03ee).
Only the public Gram/covariance eigensolves are retained: no receiver scoring,
Torch, observations, simulator, persistent state, or accelerator interface.
The original conservative preparation-array bound is kept; transform matrices
used only by the retired scorer are neither formed nor retained here.
"""
from dataclasses import dataclass
import math
import numpy as np


@dataclass(frozen=True)
class _Window:
    first: int
    stop: int
    eigenvalues: np.ndarray


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + " must be a positive integer")
    return value


def _array(value, name):
    if not isinstance(value, np.ndarray):
        raise TypeError(name + " must be a NumPy array")
    return value


class PublicWindowCovariance:
    """Eigenvalues of Q**(1/2) G Q**(1/2), with the original PSD checks."""

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
        # Keep the original conservative factor/eigenvalue bound, although only
        # eigenvalues are retained. Dense temporaries use at most one window.
        stored = self.candidates * squares * self.taps**2 * 16
        stored += self.candidates * self.blocks * self.taps * 8
        input_and_checks = 6 * math.prod(shape) * 16
        temporary = 24 * dimension**2 * 16
        self.preparation_array_bound_bytes = stored + input_and_checks + temporary + self.blocks * 8
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
        for first in range(0, self.blocks, self.window_blocks):
            stop = min(first + self.window_blocks, self.blocks)
            time = centers[first:stop] - centers[first]
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
            n, rank = root.shape
            spectrum = np.empty((self.candidates, rank), np.float64)
            for candidate in range(self.candidates):
                block_gram = np.zeros((n, n), np.complex128)
                for local, gram in enumerate(G[first:stop, candidate]):
                    lo = local * self.taps
                    block_gram[lo:lo + self.taps, lo:lo + self.taps] = gram
                information = root.conj().T @ block_gram @ root
                information = (information + information.conj().T) * .5
                if rank:
                    lam, _ = np.linalg.eigh(information)
                    lscale = max(float(abs(lam).max()), np.finfo(float).tiny)
                    if lam[0] < -relative_tolerance * lscale:
                        raise ValueError("Gaussian information lost positive semidefiniteness")
                    self.maximum_relative_information_repair = max(self.maximum_relative_information_repair, max(0., -float(lam[0])) / lscale)
                    spectrum[candidate] = np.maximum(lam, 0)
            windows.append(_Window(first, stop, spectrum))
        self._windows = tuple(windows)
        self.owned_spectrum_bytes = sum(w.eigenvalues.nbytes for w in windows)
        self.maximum_window_dimension = dimension
        self.maximum_window_rank = max(w.eigenvalues.shape[-1] for w in windows)

    def diagnostics(self):
        return {
            "blocks": self.blocks, "candidates": self.candidates, "taps": self.taps,
            "window_blocks": self.window_blocks,
            "doppler_width_hz": self.doppler_width_hz,
            "stationary_power": self.stationary_power,
            "maximum_window_dimension": self.maximum_window_dimension,
            "maximum_window_rank": self.maximum_window_rank,
            "owned_spectrum_bytes": self.owned_spectrum_bytes,
            "preparation_array_bound_bytes": self.preparation_array_bound_bytes,
            "maximum_relative_gram_repair": self.maximum_relative_gram_repair,
            "maximum_relative_prior_repair": self.maximum_relative_prior_repair,
            "maximum_relative_information_repair": self.maximum_relative_information_repair,
            "preparation_arithmetic": "CPU complex128/float64 covariance eigensolves",
            "model_scope": "independent-window block-center Gaussian covariance; not a receiver or physical FIR channel",
            "memory_scope": "conservative explicit arrays only; caller observations and solver/Python overhead excluded",
        }


def prepare_windowed_gaussian(grams, centers_seconds, **kwargs):
    return PublicWindowCovariance(grams, centers_seconds, **kwargs)

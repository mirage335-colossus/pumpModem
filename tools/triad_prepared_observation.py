"""Prepare an exact, reusable Gaussian design for paired triad simulations.

This caches fixed simulator inputs (public Gram matrices and PSD factors,
plus the noiseless physical-path mean). The mean is simulator truth, never a
receiver input. Each call still makes new per-block noise with the same SeedSequence
and operation order as :meth:`BlockWorkspace.iter_simulator`. Nonlinear sampled
frontends cannot use this Gaussian shortcut.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
import time
from typing import Iterator

import numpy as np

from triad_state_workspace import BlockWorkspace, DetectorBlock, WorkspaceError


@dataclass(frozen=True)
class PreparedPair:
    """The null and signal blocks generated from the same noise realization."""

    null: DetectorBlock
    signal: DetectorBlock


class PreparedGaussianObservation:
    """Cached public Gaussian design for reproducible paired H0/H1 draws.

    ``memory_limit_bytes`` bounds the retained design, one public-validation
    tile, eigensolver scratch, and one paired output block. Correlated noise is
    generated on demand and is never cached between calls.
    """

    def __init__(self, workspace: BlockWorkspace, *, seed: int, batch: int,
                 cn0_db_hz: float, memory_limit_bytes: int = 256 * 1024 * 1024,
                 start: int = 0, stop: int | None = None):
        preparation_start = time.perf_counter()
        if not isinstance(workspace, BlockWorkspace):
            raise TypeError("workspace must be a BlockWorkspace")
        if not workspace.gaussian_projection_supported:
            raise WorkspaceError(
                "prepared Gaussian design is unsupported for this sampled frontend; "
                "use its actual sampled observations")
        if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
            raise ValueError("seed must be a nonnegative integer")
        if isinstance(batch, bool) or not isinstance(batch, int) or batch < 1:
            raise ValueError("batch must be positive")
        if not math.isfinite(cn0_db_hz):
            raise ValueError("C/N0 must be finite")
        try:
            variance = workspace.sample_rate / (10.0 ** (cn0_db_hz / 10.0))
        except OverflowError as exc:
            raise ValueError("C/N0 gives an invalid noise variance") from exc
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("C/N0 gives an invalid noise variance")
        if isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or memory_limit_bytes < 4096:
            raise ValueError("memory_limit_bytes must be an integer of at least 4096")
        stop = workspace.block_count if stop is None else stop
        if (isinstance(start, bool) or not isinstance(start, int) or
                isinstance(stop, bool) or not isinstance(stop, int) or
                not 0 <= start < stop <= workspace.block_count):
            raise ValueError("invalid nonempty block range")

        self.workspace = workspace
        self.seed = seed
        self.batch = batch
        self.cn0_db_hz = cn0_db_hz
        self.variance = variance
        self.start = start
        self.stop = stop
        self.count = stop - start
        self.dimension = workspace.dimension
        d = self.dimension
        # Two cached complex matrices per block; truth/calendar metadata;
        # bounded public validation and eigensolver workspace; and temporary
        # batch arrays used to make one exact paired observation.
        # Two matrices, the mean, interval and valid-row metadata, center, and
        # dt: include all seven retained arrays in the declared byte count.
        self.cache_bytes = self.count * (2*d*d*16 + d*16 + 2*8 + 8 + 8 + 8)
        tile_blocks = min(workspace.max_tile_blocks, self.count)
        self.validation_tile_bytes = tile_blocks * (4*d*d*16 + 4*d*16 + 256)
        self.factor_scratch_bytes = 4*d*d*16 + 8*d*16
        self.pair_scratch_bytes = batch*d*16*6 + d*d*16
        self.estimated_peak_bytes = (self.cache_bytes + self.validation_tile_bytes +
                                     self.factor_scratch_bytes + self.pair_scratch_bytes)
        if self.estimated_peak_bytes > memory_limit_bytes:
            raise WorkspaceError(
                f"prepared Gaussian design estimate ({self.estimated_peak_bytes} bytes) "
                "exceeds memory_limit_bytes")
        self.memory_limit_bytes = memory_limit_bytes

        self.grams = np.empty((self.count, d, d), dtype=np.complex128)
        self.factors = np.empty((self.count, d, d), dtype=np.complex128)
        self.means = np.empty((self.count, d), dtype=np.complex128)
        self.intervals = np.empty((self.count, 2), dtype=np.uint64)
        self.valid_rows = np.empty(self.count, dtype=np.uint64)
        self.centers = np.empty(self.count, dtype=np.float64)
        self.dts = np.empty(self.count, dtype=np.float64)
        self._timings = {"public_validation_seconds": 0.0,
                         "factorization_seconds": 0.0,
                         "preparation_seconds": 0.0,
                         "rng_seconds": 0.0,
                         "correlated_noise_projection_seconds": 0.0,
                         "paired_normalization_seconds": 0.0,
                         "blocks_generated": 0,
                         "rng_calls": 0}
        self._prepare()
        self._timings["preparation_seconds"] = time.perf_counter()-preparation_start

    def _prepare(self) -> None:
        ws = self.workspace
        d = self.dimension
        means = ws._map("mean.c128", ws._MEAN_DTYPE, (ws.block_count, d))
        energies = ws._map("signal_energy.f64", ws._F64_DTYPE, (ws.block_count,))
        previous_center = ws._preceding_center(self.start)
        cursor = 0
        tile_iterator = iter(ws._enumerated_public(self.start, self.stop,
                                                   ws.max_tile_blocks))
        while True:
            validate_start = time.perf_counter()
            try:
                first, tile = next(tile_iterator)
            except StopIteration:
                break
            # iter_public performs independent layout, interval, Hermitian,
            # finite-value, and configured PSD validation on each tile.
            self._timings["public_validation_seconds"] += time.perf_counter()-validate_start
            for offset, (Graw, interval, valid) in enumerate(
                    zip(tile.gram, tile.intervals, tile.valid_rows)):
                index = first + offset
                slot = cursor
                mu = means[index]
                energy = float(energies[index])
                if not np.isfinite(mu).all() or not math.isfinite(energy) or energy < 0:
                    raise WorkspaceError(f"nonfinite simulation truth in block {index}")
                factor_start = time.perf_counter()
                factor = ws._factor_psd(Graw, ws.psd_tolerance)
                self._timings["factorization_seconds"] += time.perf_counter()-factor_start
                center = (int(interval[0]) + int(interval[1])) / (2.0*ws.sample_rate)
                dt = 0.0 if previous_center is None else center - previous_center
                if dt <= 0 and previous_center is not None:
                    raise WorkspaceError("block centers are not strictly increasing")
                previous_center = center
                self.grams[slot] = Graw
                self.factors[slot] = factor
                self.means[slot] = mu
                self.intervals[slot] = interval
                self.valid_rows[slot] = valid
                self.centers[slot] = center
                self.dts[slot] = dt
                cursor += 1
        if cursor != self.count:
            raise WorkspaceError("prepared design did not cover the complete requested block range")
        # The cache is public design state and should not be accidentally
        # altered while multiple SNR/algorithm trials reuse it.
        for array in (self.grams, self.factors, self.means, self.intervals,
                      self.valid_rows, self.centers, self.dts):
            array.flags.writeable = False

    def iter_paired(self, *, seed: int | None = None,
                    cn0_db_hz: float | None = None) -> Iterator[PreparedPair]:
        """Yield fresh paired blocks; optional seed/SNR can vary on each pass."""
        root = self.workspace
        seed = self.seed if seed is None else seed
        cn0_db_hz = self.cn0_db_hz if cn0_db_hz is None else cn0_db_hz
        if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
            raise ValueError("seed must be a nonnegative integer")
        if not math.isfinite(cn0_db_hz):
            raise ValueError("C/N0 must be finite")
        try:
            variance = root.sample_rate / (10.0 ** (cn0_db_hz / 10.0))
        except OverflowError as exc:
            raise ValueError("C/N0 gives an invalid noise variance") from exc
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("C/N0 gives an invalid noise variance")
        sqrt_variance = math.sqrt(variance)
        for slot, index in enumerate(range(self.start, self.stop)):
            # Match iter_simulator's RNG stream and draw/order exactly. The
            # one noise realization is shared by the paired H0/H1 observations.
            rng_start = time.perf_counter()
            rng = np.random.default_rng(np.random.SeedSequence([seed, index]))
            e = (rng.standard_normal((self.batch, self.dimension)) +
                 1j*rng.standard_normal((self.batch, self.dimension))) / math.sqrt(2.0)
            self._timings["rng_seconds"] += time.perf_counter()-rng_start
            self._timings["rng_calls"] += 1

            factor = self.factors[slot]
            noise_start = time.perf_counter()
            noise = e @ factor.T * sqrt_variance
            self._timings["correlated_noise_projection_seconds"] += time.perf_counter()-noise_start

            normalization_start = time.perf_counter()
            mu = self.means[slot]
            # Keep the exact operations from iter_simulator for each label.
            raw0 = noise + 0.0
            raw1 = noise + mu[None, :]
            Graw = self.grams[slot]
            Gnorm = Graw / variance
            r0 = raw0 / variance
            r1 = raw1 / variance
            self._timings["paired_normalization_seconds"] += time.perf_counter()-normalization_start
            self._timings["blocks_generated"] += 1
            interval = (int(self.intervals[slot, 0]), int(self.intervals[slot, 1]))
            valid = int(self.valid_rows[slot])
            dt = float(self.dts[slot])
            center = float(self.centers[slot])
            gram0, gram1 = Gnorm, Gnorm.copy()
            root.memory_bytes = max(root.memory_bytes,
                                    int(r0.nbytes + gram0.nbytes + factor.nbytes))
            yield PreparedPair(
                null=DetectorBlock(r0, gram0, dt, interval, valid, index, center),
                signal=DetectorBlock(r1, gram1, dt, interval, valid, index, center))

    def diagnostics(self) -> dict:
        """Return stage timings and bounded cache accounting so far."""
        return {**self._timings,
                "cache_bytes": self.cache_bytes,
                "validation_tile_bytes": self.validation_tile_bytes,
                "factor_scratch_bytes": self.factor_scratch_bytes,
                "paired_block_scratch_estimate_bytes": self.pair_scratch_bytes,
                "estimated_peak_bytes": self.estimated_peak_bytes,
                "memory_limit_bytes": self.memory_limit_bytes,
                "fixed_simulator_inputs_only": True,
                "noiseless_mean_role": "simulation truth only; not passed to receiver",
                "cached_noise_or_observation_bytes": 0,
                "dtype": "complex128/float64"}


def prepare_paired_observation(workspace: BlockWorkspace, **kwargs) -> PreparedGaussianObservation:
    """Convenience constructor for the opt-in exact paired-design cache."""
    return PreparedGaussianObservation(workspace, **kwargs)

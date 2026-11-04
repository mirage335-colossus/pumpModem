"""Bounded reader and Gaussian experiment sampler for triad-block-stats-v1.

The exported means and signal energies are simulator truth. Receiver-facing
iterators return only the normalized sufficient statistics ``r`` and ``G``.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Iterator

import numpy as np


class WorkspaceError(ValueError):
    """The statistics export is malformed or outside supported bounds."""


@dataclass(frozen=True)
class PublicTile:
    gram: np.ndarray
    intervals: np.ndarray
    valid_rows: np.ndarray


@dataclass(frozen=True)
class DetectorBlock:
    r: np.ndarray  # [batch, dimension], normalized by complex noise variance
    gram: np.ndarray  # [dimension, dimension], normalized by variance
    dt_seconds: float
    interval: tuple[int, int]
    valid_rows: int
    index: int
    center_seconds: float


@dataclass(frozen=True)
class ReceivedBlock:
    """Actual sampled-observation statistics normalized by a caller reference."""
    r: np.ndarray  # [1, dimension], A^H W y / reference_variance
    gram: np.ndarray  # [dimension, dimension], A^H W A / reference_variance
    received_energy: float  # sum |y|^2 over valid raw samples / reference_variance
    valid_rows: int
    interval: tuple[int, int]
    center_seconds: float
    dt_seconds: float
    index: int


class BlockWorkspace:
    """Read exported block Gram matrices with bounded memory.

    Circular complex noise is sampled from the *full* Gram matrix so noise
    terms remain coupled across all candidate/tap columns. The PSD check uses
    a relative tolerance in eigenvalue units: negative eigenvalues less than
    ``-psd_tolerance * max(max_eigenvalue, 1)`` are rejected. Small negative
    roundoff eigenvalues are clipped to zero only for noise generation.
    """

    FORMAT = "triad-block-stats-v1"
    _GRAM_DTYPE = np.dtype("<c16")
    _MEAN_DTYPE = np.dtype("<c16")
    _U64_DTYPE = np.dtype("<u8")
    _F64_DTYPE = np.dtype("<f8")

    def __init__(self, directory: str | Path, *, max_tile_blocks: int = 256,
                 psd_tolerance: float = 1e-9, check_psd: bool = True,
                 memory_limit_bytes: int = 256 * 1024 * 1024):
        self.directory = Path(directory)
        self.manifest_path = self.directory / "manifest.json"
        try:
            self.manifest = json.loads(self.manifest_path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise WorkspaceError(f"cannot read manifest: {exc}") from exc
        m = self.manifest
        supported = m.get("gaussian_projection_supported", True)
        if not isinstance(supported, bool):
            raise WorkspaceError("gaussian_projection_supported must be boolean")
        self.gaussian_projection_supported = supported
        if m.get("format") != self.FORMAT:
            raise WorkspaceError("unsupported block statistics format")
        if m.get("byte_order") != "little" or m.get("complex_type") != "complex128" or m.get("real_type") != "float64" or m.get("integer_type") != "uint64":
            raise WorkspaceError("unsupported exported scalar representation")
        if m.get("layout") != "block,row,column":
            raise WorkspaceError("unsupported array layout")
        self.block_count = self._positive_int(m, "block_count")
        self.dimension = self._positive_int(m, "dimension")
        self.sample_rate = self._positive_finite(m, "sample_rate")
        self.noise_variance = self._positive_finite(m, "noise_variance")
        self.sample_count = self._positive_int(m, "sample_count")
        self.block_samples = self._positive_int(m, "block_samples")
        if self.block_count > self.sample_count or self.sample_count > self.block_count * self.block_samples:
            raise WorkspaceError("block count, sample count and block size are inconsistent")
        if not isinstance(max_tile_blocks, int) or max_tile_blocks < 1:
            raise ValueError("max_tile_blocks must be positive")
        if isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or memory_limit_bytes < 4096:
            raise ValueError("memory_limit_bytes must be an integer of at least 4096")
        bytes_per_validation_block = 4 * self.dimension * self.dimension * 16 + 4 * self.dimension * 16 + 256
        bounded_tile = memory_limit_bytes // bytes_per_validation_block
        if bounded_tile < 1:
            raise WorkspaceError("memory limit is too small to validate one Gram block")
        if not math.isfinite(psd_tolerance) or psd_tolerance < 0:
            raise ValueError("psd_tolerance must be finite and nonnegative")
        self.memory_limit_bytes = memory_limit_bytes
        self.max_tile_blocks = min(max_tile_blocks, bounded_tile)
        self.psd_tolerance = psd_tolerance
        self.check_psd = bool(check_psd)
        self.column_mapping = self._mapping(m.get("column_mapping"))
        self.candidates = m.get("candidates")
        if not isinstance(self.candidates, list) or len(self.candidates) != m.get("candidate_count"):
            raise WorkspaceError("candidate metadata count is inconsistent")
        descriptors = m.get("column_descriptors")
        if not isinstance(descriptors, list) or len(descriptors) != self.dimension:
            raise WorkspaceError("column descriptor count is inconsistent")
        self._validate_candidates(descriptors)
        self._validate_shapes()
        self._validate_files()
        self._maps = {}
        self.memory_bytes = 0

    @staticmethod
    def _positive_int(obj, key):
        value = obj.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise WorkspaceError(f"{key} must be a positive integer")
        return value

    @staticmethod
    def _positive_finite(obj, key):
        value = obj.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise WorkspaceError(f"{key} must be positive and finite")
        return float(value)

    def _mapping(self, raw):
        if not isinstance(raw, list) or len(raw) != self.manifest.get("candidate_count"):
            raise WorkspaceError("candidate column mapping is malformed")
        output = []
        for row in raw:
            if not isinstance(row, list) or not row:
                raise WorkspaceError("candidate mapping rows must be nonempty lists")
            values = []
            for col in row:
                if isinstance(col, bool) or not isinstance(col, int) or not 0 <= col < self.dimension:
                    raise WorkspaceError("candidate mapping column is outside the Gram matrix")
                values.append(col)
            output.append(tuple(values))
        return tuple(output)

    @staticmethod
    def _finite_number(value):
        return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)

    def _validate_candidates(self, descriptors):
        used = set()
        for i, (candidate, mapping) in enumerate(zip(self.candidates, self.column_mapping)):
            if not isinstance(candidate, dict) or candidate.get("bit") not in (0, 1):
                raise WorkspaceError(f"candidate {i} must specify bit 0 or 1")
            if candidate.get("tap_count") != len(mapping):
                raise WorkspaceError(f"candidate {i} tap count disagrees with its column mapping")
            for key in ("arrival_seconds", "frequency_hz", "clock_ppm", "frequency_drift_hz_per_second"):
                if not self._finite_number(candidate.get(key)):
                    raise WorkspaceError(f"candidate {i} has invalid {key}")
            taps = candidate.get("taps")
            if not isinstance(taps, list) or len(taps) != candidate["tap_count"]:
                raise WorkspaceError(f"candidate {i} tap metadata is inconsistent")
            for tap in taps:
                if not isinstance(tap, dict) or not self._finite_number(tap.get("delay_seconds")) or not self._finite_number(tap.get("doppler_hz")):
                    raise WorkspaceError(f"candidate {i} has invalid tap metadata")
            used.update(mapping)
        if used != set(range(self.dimension)):
            raise WorkspaceError("candidate mappings do not cover every Gram column")
        for i, descriptor in enumerate(descriptors):
            if not isinstance(descriptor, dict):
                raise WorkspaceError(f"column descriptor {i} is malformed")
            ci = descriptor.get("candidate_index")
            if isinstance(ci, bool) or not isinstance(ci, int) or not 0 <= ci < len(self.candidates):
                raise WorkspaceError(f"column descriptor {i} has an invalid candidate index")
            if descriptor.get("source_bit") != self.candidates[ci]["bit"]:
                raise WorkspaceError(f"column descriptor {i} bit disagrees with its candidate")
            for key in ("arrival_seconds", "frequency_hz", "clock_ppm", "frequency_drift_hz_per_second", "tap_delay_seconds", "tap_doppler_hz"):
                if not self._finite_number(descriptor.get(key)):
                    raise WorkspaceError(f"column descriptor {i} has invalid {key}")

    def _validate_shapes(self):
        m, b, d = self.manifest, self.block_count, self.dimension
        expected = {
            "gram_shape": [b, d, d], "mean_shape": [b, d],
            "intervals_shape": [b, 2], "valid_rows_shape": [b],
            "signal_energy_shape": [b],
        }
        for key, shape in expected.items():
            if m.get(key) != shape:
                raise WorkspaceError(f"{key} does not match manifest dimensions")
        if m.get("gram_file") != "gram.c128" or m.get("mean_file") != "mean.c128" or m.get("intervals_file") != "intervals.u64" or m.get("valid_rows_file") != "valid_rows.u64" or m.get("signal_energy_file") != "signal_energy.f64":
            raise WorkspaceError("unexpected data filename")
        if m.get("stats_noisy"):
            if m.get("observation_file") != "observation.c128" or m.get("observation_shape") != [b, d]:
                raise WorkspaceError("noisy observation metadata is inconsistent")
        self.has_observation = bool(m.get("stats_noisy"))
        energy_name = m.get("received_energy_file")
        energy_shape = m.get("received_energy_shape")
        if energy_name is None and energy_shape is None:
            self.has_received_energy = False
        elif (energy_name == "received_energy.f64" and energy_shape == [b] and
              m.get("received_energy_semantics") ==
              "per-block sum |y|^2 of actual noisy samples over valid rows from the same second channel pass; simulation evaluation only"):
            self.has_received_energy = True
        else:
            raise WorkspaceError("received-energy metadata is incomplete or inconsistent")

    def _validate_files(self):
        b, d, m = self.block_count, self.dimension, self.manifest
        sizes = {
            "gram.c128": b*d*d*16, "mean.c128": b*d*16,
            "intervals.u64": b*2*8, "valid_rows.u64": b*8,
            "signal_energy.f64": b*8,
        }
        if self.has_observation:
            sizes["observation.c128"] = b*d*16
        if self.has_received_energy:
            sizes["received_energy.f64"] = b*8
        recorded = m.get("file_sizes")
        if not isinstance(recorded, dict):
            raise WorkspaceError("file size table is missing")
        for name, expected in sizes.items():
            if recorded.get(name) != expected:
                raise WorkspaceError(f"recorded size for {name} is inconsistent")
            try:
                actual = (self.directory / name).stat().st_size
            except OSError as exc:
                raise WorkspaceError(f"cannot stat {name}: {exc}") from exc
            if actual != expected:
                raise WorkspaceError(f"file size for {name} is inconsistent")

    def _map(self, name, dtype, shape):
        if name not in self._maps:
            self._maps[name] = np.memmap(self.directory / name, mode="r", dtype=dtype, shape=shape, order="C")
        return self._maps[name]

    def iter_public(self, *, start: int = 0, stop: int | None = None,
                    tile_blocks: int | None = None) -> Iterator[PublicTile]:
        """Yield only the public design Gram matrices and observation calendar."""
        stop = self.block_count if stop is None else stop
        tile = self.max_tile_blocks if tile_blocks is None else tile_blocks
        if not 0 <= start <= stop <= self.block_count or not isinstance(tile, int) or tile < 1:
            raise ValueError("invalid block range or tile size")
        tile = min(tile, self.max_tile_blocks)
        d, b = self.dimension, self.block_count
        gram = self._map("gram.c128", self._GRAM_DTYPE, (b, d, d))
        intervals = self._map("intervals.u64", self._U64_DTYPE, (b, 2))
        valid = self._map("valid_rows.u64", self._U64_DTYPE, (b,))
        previous_stop = None
        for first in range(start, stop, tile):
            last = min(stop, first + tile)
            G = np.asarray(gram[first:last])
            iv = np.asarray(intervals[first:last])
            vr = np.asarray(valid[first:last])
            self._check_tile(G, iv, vr, first, previous_stop)
            previous_stop = int(iv[-1, 1])
            yield PublicTile(G, iv, vr)

    def _check_tile(self, G, iv, valid, first_index, previous_stop):
        if not np.isfinite(G).all():
            raise WorkspaceError(f"nonfinite statistics in block tile beginning {first_index}")
        lengths = iv[:, 1] - iv[:, 0]
        if (iv[:, 1] <= iv[:, 0]).any() or (valid > lengths).any():
            raise WorkspaceError("intervals or valid row counts are invalid")
        if previous_stop is not None and int(iv[0, 0]) != previous_stop:
            raise WorkspaceError("intervals are not contiguous across tiles")
        if first_index == 0 and int(iv[0, 0]) != 0:
            raise WorkspaceError("first interval must start at sample zero")
        if len(iv) > 1 and not np.array_equal(iv[:-1, 1], iv[1:, 0]):
            raise WorkspaceError("intervals are not contiguous within tile")
        indices = np.arange(first_index, first_index + len(iv))
        expected_lengths = np.minimum(self.block_samples, self.sample_count - indices * self.block_samples)
        if not np.array_equal(lengths, expected_lengths):
            raise WorkspaceError("interval lengths do not match the declared block calendar")
        if not np.array_equal(iv[:, 0], indices * self.block_samples):
            raise WorkspaceError("interval starts do not match the declared absolute block calendar")
        if first_index + len(iv) == self.block_count and int(iv[-1, 1]) != self.sample_count:
            raise WorkspaceError("final interval does not end at sample_count")
        if not np.allclose(G, G.swapaxes(-1, -2).conj(), rtol=1e-11, atol=1e-9):
            raise WorkspaceError(f"Gram matrix is not Hermitian near block {first_index}")
        if self.check_psd:
            eig = np.linalg.eigvalsh(G)
            scale = np.maximum(np.max(eig, axis=-1), 1.0)
            if (eig[:, 0] < -self.psd_tolerance * scale).any():
                raise WorkspaceError(f"Gram matrix is not positive semidefinite near block {first_index}")

    @staticmethod
    def _factor_psd(G, tolerance):
        G = (G + G.conj().T) * 0.5
        values, vectors = np.linalg.eigh(G)
        scale = max(float(values[-1]), 1.0)
        if values[0] < -tolerance * scale:
            raise WorkspaceError("Gram matrix is not positive semidefinite")
        values = np.maximum(values, 0.0)
        return vectors * np.sqrt(values)[None, :]

    def iter_simulator(self, *, seed: int, batch: int, cn0_db_hz: float,
                        signal: bool = True, start: int = 0, stop: int | None = None,
                        tile_blocks: int = 256) -> Iterator[DetectorBlock]:
        """Yield normalized receiver statistics with coupled circular noise.

        A fresh independent ``batch`` of whole-D noise vectors is generated at
        each disjoint block. The physical complex noise variance is Fs/10^(C/N0/10).
        The yielded likelihood inputs satisfy ``r=AᴴWy/σ²`` and ``G=AᴴWA/σ²``.
        """
        if not self.gaussian_projection_supported:
            raise WorkspaceError("Gaussian projection simulation is unsupported for this sampled frontend; use independently sampled observation exports")
        if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
            raise ValueError("seed must be a nonnegative integer")
        if isinstance(batch, bool) or not isinstance(batch, int) or batch < 1:
            raise ValueError("batch must be positive")
        if not math.isfinite(cn0_db_hz):
            raise ValueError("C/N0 must be finite")
        try:
            variance = self.sample_rate / (10.0 ** (cn0_db_hz / 10.0))
        except OverflowError as exc:
            raise ValueError("C/N0 gives an invalid noise variance") from exc
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("C/N0 gives an invalid noise variance")
        requested_tile = min(tile_blocks, self.max_tile_blocks)
        tile_estimate = requested_tile * (4 * self.dimension * self.dimension * 16 + 4 * self.dimension * 16 + 256)
        working_estimate = tile_estimate + batch * self.dimension * 16 * 5 + self.dimension * self.dimension * 16 * 4 + 4096
        if working_estimate > self.memory_limit_bytes:
            raise WorkspaceError("requested simulation tile and batch exceed the workspace memory limit")
        previous_center = self._preceding_center(start)
        stop = self.block_count if stop is None else stop
        if not 0 <= start <= stop <= self.block_count:
            raise ValueError("invalid block range")
        means = self._map("mean.c128", self._MEAN_DTYPE, (self.block_count, self.dimension))
        energies = self._map("signal_energy.f64", self._F64_DTYPE, (self.block_count,))
        for tile_first, tile in self._enumerated_public(start, stop, tile_blocks):
            for offset, (Graw, interval, valid) in enumerate(zip(tile.gram, tile.intervals, tile.valid_rows)):
                i = tile_first + offset
                mu = means[i]
                if not np.isfinite(mu).all() or not math.isfinite(float(energies[i])) or energies[i] < 0:
                    raise WorkspaceError(f"nonfinite simulation truth in block {i}")
                L = self._factor_psd(Graw, self.psd_tolerance)
                rng = np.random.default_rng(np.random.SeedSequence([seed, i]))
                e = (rng.standard_normal((batch, self.dimension)) + 1j*rng.standard_normal((batch, self.dimension))) / math.sqrt(2.0)
                noise = e @ L.T * math.sqrt(variance)
                raw = noise + (mu[None, :] if signal else 0.0)
                Gn = Graw / variance
                rn = raw / variance
                center = (int(interval[0]) + int(interval[1])) / (2.0 * self.sample_rate)
                dt = 0.0 if previous_center is None else center - previous_center
                if dt <= 0 and previous_center is not None:
                    raise WorkspaceError("block centers are not strictly increasing")
                previous_center = center
                self.memory_bytes = max(self.memory_bytes, int(rn.nbytes + Gn.nbytes + L.nbytes))
                yield DetectorBlock(rn, Gn, dt, (int(interval[0]), int(interval[1])), int(valid), i, center)

    def _enumerated_public(self, start, stop, tile_blocks):
        first = start
        for tile in self.iter_public(start=start, stop=stop, tile_blocks=tile_blocks):
            yield first, tile
            first += len(tile.intervals)

    def _preceding_center(self, start):
        # A nonzero start continues the same public observation calendar.
        # Preserve the transition across iterator/chunk boundaries.
        return None if start == 0 else ((start-1)*self.block_samples +
            min(start*self.block_samples, self.sample_count))/(2*self.sample_rate)

    def iter_observation(self, *, start: int = 0, stop: int | None = None,
                         tile_blocks: int = 256):
        """Yield actual noisy projections in the same normalized block API."""
        if not self.has_observation:
            raise WorkspaceError("this export has no actual noisy projection")
        if isinstance(tile_blocks, bool) or not isinstance(tile_blocks, int) or tile_blocks < 1:
            raise ValueError("tile_blocks must be a positive integer")
        stop = self.block_count if stop is None else stop
        if not 0 <= start <= stop <= self.block_count:
            raise ValueError("invalid block range")
        tile_blocks = min(tile_blocks, self.max_tile_blocks)
        values = self._map("observation.c128", self._GRAM_DTYPE,
                           (self.block_count, self.dimension))
        variance = self.noise_variance
        previous_center = self._preceding_center(start)
        for first, tile in self._enumerated_public(start, stop, tile_blocks):
            for offset, (raw, Graw, interval, valid) in enumerate(zip(values[first:first+len(tile.intervals)], tile.gram, tile.intervals, tile.valid_rows)):
                if not np.isfinite(raw).all():
                    raise WorkspaceError(f"nonfinite actual observation near block {first+offset}")
                G = Graw / variance
                r = (raw / variance)[None, :]
                center = (int(interval[0]) + int(interval[1])) / (2.0 * self.sample_rate)
                dt = 0.0 if previous_center is None else center - previous_center
                if dt <= 0 and previous_center is not None:
                    raise WorkspaceError("block centers are not strictly increasing")
                previous_center = center
                yield DetectorBlock(r, G, dt, (int(interval[0]), int(interval[1])), int(valid), first+offset, center)

    def iter_received_blocks(self, *, reference_variance: float, start: int = 0,
                              stop: int | None = None, tile_blocks: int = 256) -> Iterator[ReceivedBlock]:
        """Yield actual observed projections and energy under an explicit reference.

        The reference is supplied by the caller and may be an intentionally
        uncertain working value. This path never uses or returns the actual
        simulation noise variance from the manifest. The frontend's declared
        ``gaussian_projection_supported`` flag is preserved on the workspace;
        consumers must not infer a Gaussian likelihood guarantee from these
        sufficient statistics when that flag is false.
        """
        if not self.has_observation:
            raise WorkspaceError("actual received projections are missing; regenerate export-stats with --stats-noisy")
        if not self.has_received_energy:
            raise WorkspaceError("received_energy.f64 is missing; regenerate export-stats with the received-energy exporter")
        if not self._finite_number(reference_variance) or reference_variance <= 0:
            raise ValueError("reference_variance must be explicitly supplied, positive, and finite")
        if isinstance(tile_blocks, bool) or not isinstance(tile_blocks, int) or tile_blocks < 1:
            raise ValueError("tile_blocks must be a positive integer")
        stop = self.block_count if stop is None else stop
        if not 0 <= start <= stop <= self.block_count:
            raise ValueError("invalid block range")
        tile_blocks = min(tile_blocks, self.max_tile_blocks)
        tile_estimate = tile_blocks * (4 * self.dimension * self.dimension * 16 +
                                       4 * self.dimension * 16 + 256)
        working_estimate = tile_estimate + self.dimension * self.dimension * 16 * 4 + self.dimension * 16 * 3 + 4096
        if working_estimate > self.memory_limit_bytes:
            raise WorkspaceError("requested actual-observation tile exceeds the workspace memory limit")
        values = self._map("observation.c128", self._GRAM_DTYPE,
                           (self.block_count, self.dimension))
        energies = self._map("received_energy.f64", self._F64_DTYPE, (self.block_count,))
        previous_center = self._preceding_center(start)
        for first, tile in self._enumerated_public(start, stop, tile_blocks):
            actual_tile = values[first:first+len(tile.intervals)]
            energy_tile = energies[first:first+len(tile.intervals)]
            for offset, (raw, Graw, interval, valid, energy) in enumerate(
                    zip(actual_tile, tile.gram, tile.intervals, tile.valid_rows, energy_tile)):
                index = first + offset
                if not np.isfinite(raw).all():
                    raise WorkspaceError(f"nonfinite actual observation near block {index}")
                if not math.isfinite(float(energy)) or energy < 0:
                    raise WorkspaceError(f"invalid received energy near block {index}")
                G = Graw / reference_variance
                r = (raw / reference_variance)[None, :]
                center = (int(interval[0]) + int(interval[1])) / (2.0 * self.sample_rate)
                dt = 0.0 if previous_center is None else center - previous_center
                if dt <= 0 and previous_center is not None:
                    raise WorkspaceError("block centers are not strictly increasing")
                previous_center = center
                yield ReceivedBlock(r, G, float(energy)/reference_variance,
                                    int(valid), (int(interval[0]), int(interval[1])),
                                    center, dt, index)


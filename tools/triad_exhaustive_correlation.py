"""Exhaustive tiled sampled correlations for the isolated triad study.

Every candidate (including bit/epoch/clock/channel alternatives), integer lag,
CFO, block and coupled tap survives this stage.  This is a sufficient-statistic
producer, not an acquisition gate.  In particular native FP4 statistics are
experimental and do not inherit the strict receiver's admission calibration.

The public basis is [candidate, sample, tap].  For a lag l and CFO f its value
at receiver sample n+l is T[n]*exp(2*pi*j*f*(origin+(n+l)/sample_rate)).
Disjoint blocks partition the complete basis, including zero-energy gaps and
the final partial block.  All shifted windows must fit; there is no truncation,
per-window energy normalization, candidate selection or per-carrier tap fit.
Missing-sample masks are unsupported by this first prototype: its caller must
reject captures with erasures rather than treating their zeros as observations.

Native backends implement pack_rows(real_rows), matmul(Apack, Bpack), and
packed_nbytes(pack) (or pack.nbytes). Both packed operands store rows along K;
matmul returns A @ B.T with FP32 accumulation/output. Optional decode_rows(pack)
enables CPU FP64 representation diagnostics. Padding/scales belong to the
backend and must be included in its byte count. No CPU fallback is supplied.
The explicit diagnostic-fp64 mode instead requires an injected diagnostic
backend returning FP64 represented-input products on those same FP32 staged
rows. It never denotes native arithmetic or a native performance measurement.
Native complex real/imag halves are individually zero-padded to the backend's
scale group before concatenation. This keeps quadrature rows representations
of one complex quantized template, including partial final blocks.
"""
from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass
import math
import time
from typing import Any, Iterator

import numpy as np
import torch


def _integer(value: int, name: str, minimum: int = 1) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return int(value)


def _positive(value: float, name: str) -> float:
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be finite and positive")
    return value


def _cpu_complex(value: Any, name: str) -> np.ndarray:
    if isinstance(value, torch.Tensor):
        value = value.detach().cpu().numpy()
    result = np.array(value, dtype=np.complex128, copy=True)
    if not np.isfinite(result).all():
        raise ValueError(f"{name} must be finite")
    return result


def _require_ieee(device: torch.device) -> None:
    # These are process-wide flags; never change another user's arithmetic.
    if device.type == "cuda" and (torch.backends.cuda.matmul.allow_tf32 or
                                   torch.get_float32_matmul_precision() != "highest"):
        raise ValueError("strict FP32 requires TF32 disabled and highest Torch matmul precision")


def _halves(value: torch.Tensor, alignment: int):
    alignment = _integer(alignment, "half_alignment")
    extra = (-value.shape[-1]) % alignment
    real, imag = value.real, value.imag
    if extra:
        real = torch.nn.functional.pad(real, (0, extra))
        imag = torch.nn.functional.pad(imag, (0, extra))
    return real, imag


def complex_template_rows(template: torch.Tensor, *, half_alignment=1) -> torch.Tensor:
    """[...,sample] -> [...,quadrature,2*padded_half], real then imag row."""
    real, imag = _halves(template, half_alignment)
    return torch.stack((torch.cat((real, imag), -1),
                        torch.cat((-imag, real), -1)), -2)


def observation_rows(observation: torch.Tensor, *, half_alignment=1) -> torch.Tensor:
    real, imag = _halves(observation, half_alignment)
    return torch.cat((real, imag), -1).contiguous()


@dataclass(frozen=True)
class CorrelationTile:
    block_index: int
    interval: tuple[int, int]
    dt: float
    frequency_slice: slice
    candidate_slice: slice
    # Columns are flattened trial-major, lag-minor. This handles ragged N tiles
    # without padding the bank or pretending a tile is always rectangular.
    column_indices: torch.Tensor
    trial_indices: torch.Tensor
    lag_indices: torch.Tensor
    centers_seconds: torch.Tensor
    r: torch.Tensor  # [column, CFO, candidate, tap], complex128
    gram: torch.Tensor  # [candidate,tap,tap], complex128, already /variance


@dataclass(frozen=True)
class CorrelationBlock:
    block_index: int
    interval: tuple[int, int]
    dt: float
    centers_seconds: torch.Tensor  # [lag], absolute receiver time coordinates
    r: torch.Tensor  # [trial,CFO,candidate,lag,tap], complex128
    gram: torch.Tensor  # [candidate,tap,tap], complex128, already /variance


class ExhaustiveCorrelationBank:
    """Immutable public template design with a byte-bounded packed-A LRU.

    B windows are gathered/packed once per column tile per block and reused
    across every A tile. Cached A packs survive subsequent observations. A
    block-at-a-time iterator bounds output storage; iter_tiles avoids even the
    dense block output. Input observations and the bank-owned FP64 templates
    are additional, explicitly reported storage, not part of the LRU budget.

    Timings are host/enqueue times without CUDA synchronization. Use external
    CUDA events or synchronized complete-job measurements for GPU timings.
    A bank is single-consumer: do not interleave its scan iterators or mutate a
    backend's quantization policy after construction.
    """

    _CONFIGURATION = frozenset(("_basis", "candidates", "samples", "taps", "sample_rate",
        "frequencies", "lags", "block_samples", "candidate_tile", "frequency_tile",
        "column_tile", "template_cache_bytes", "gram_cache_bytes", "geometry_cache_bytes",
        "fused_reconstruction", "device", "precision", "backend", "_dtype", "_half_alignment"))

    def __setattr__(self, name, value):
        if name in self._CONFIGURATION and hasattr(self, name):
            raise AttributeError(f"bank configuration {name} is immutable; construct a new bank")
        super().__setattr__(name, value)

    def __init__(self, basis, sample_rate: float, frequencies_hz, lags,
                 block_samples: int, *, device="cpu", precision="fp64",
                 backend=None, candidate_tile=16, frequency_tile=4,
                 column_tile=128, template_cache_bytes=256*1024*1024,
                 gram_cache_bytes=16*1024*1024, geometry_cache_bytes=32*1024*1024,
                 fused_reconstruction=False):
        self._basis = _cpu_complex(basis, "basis")
        if self._basis.ndim != 3 or min(self._basis.shape) < 1:
            raise ValueError("basis must have nonempty [candidate,sample,tap] shape")
        self.candidates, self.samples, self.taps = self._basis.shape
        self.sample_rate = _positive(sample_rate, "sample_rate")
        self.frequencies = np.array(frequencies_hz, dtype=np.float64, copy=True)
        if self.frequencies.ndim != 1 or not self.frequencies.size or not np.isfinite(self.frequencies).all():
            raise ValueError("frequencies_hz must be a nonempty finite vector")
        lag_values = list(lags)
        self.lags = np.array([_integer(lag, "lag", 0) for lag in lag_values], dtype=np.int64)
        if not self.lags.size:
            raise ValueError("lags must be nonempty")
        if len(set(self.lags.tolist())) != len(self.lags) or len(set(self.frequencies.tolist())) != len(self.frequencies):
            raise ValueError("duplicate lag or CFO hypotheses")
        self.frequencies.setflags(write=False)
        self.lags.setflags(write=False)
        self._basis.setflags(write=False)
        self.block_samples = _integer(block_samples, "block_samples")
        self.candidate_tile = _integer(candidate_tile, "candidate_tile")
        self.frequency_tile = _integer(frequency_tile, "frequency_tile")
        self.column_tile = _integer(column_tile, "column_tile")
        self.template_cache_bytes = _integer(template_cache_bytes, "template_cache_bytes", 0)
        self.gram_cache_bytes = _integer(gram_cache_bytes, "gram_cache_bytes", 0)
        self.geometry_cache_bytes = _integer(geometry_cache_bytes, "geometry_cache_bytes", 0)
        self.device = torch.device(device)
        if not isinstance(fused_reconstruction, bool):
            raise ValueError("fused_reconstruction must be boolean")
        if fused_reconstruction and self.device.type != "cuda":
            raise ValueError("fused reconstruction requires CUDA; no CPU fallback")
        self.fused_reconstruction = fused_reconstruction
        if precision not in ("fp64", "fp32", "native-fp4", "diagnostic-fp64"):
            raise ValueError("precision must be fp64, fp32, native-fp4 or diagnostic-fp64")
        if precision in ("native-fp4", "diagnostic-fp64") and backend is None:
            raise ValueError(f"{precision} requires an explicit backend; no CPU fallback")
        if precision not in ("native-fp4", "diagnostic-fp64") and backend is not None:
            raise ValueError("backend is only valid for native-fp4 or diagnostic-fp64")
        if precision == "fp32":
            _require_ieee(self.device)
        self.precision, self.backend = precision, backend
        self._dtype = torch.float64 if precision == "fp64" else torch.float32
        alignment = 1
        if backend is not None:
            alignment = getattr(backend, "group_size", None)
            if alignment is None:
                alignment = {"nvfp4": 16, "mxfp4": 32}.get(getattr(backend, "format", None))
            if alignment is None:
                raise TypeError("native backend must expose group_size or a known FP4 format")
        self._half_alignment = _integer(alignment, "backend group_size")
        if self._dtype == torch.float32 and np.max(np.abs(self._basis)) > np.finfo(np.float32).max:
            raise ValueError("template amplitude is outside the finite FP32 product-input range")
        self._cache: OrderedDict[tuple, tuple[Any, int]] = OrderedDict()
        self._cache_bytes = 0
        self._gram_cache: OrderedDict[tuple, tuple[torch.Tensor, int]] = OrderedDict()
        self._gram_cache_bytes = 0
        self._geometry_cache: OrderedDict[tuple, tuple[tuple, int]] = OrderedDict()
        self._geometry_cache_bytes = 0
        # Public grid preparation belongs to bank construction, not every
        # scan/block. All subsequent center arithmetic stays on the device.
        self._lags_device = torch.tensor(self.lags.copy(), device=self.device)
        self._frequencies_device = torch.tensor(self.frequencies.copy(), device=self.device)
        self._lag_seconds = self._lags_device.double()/self.sample_rate
        self.reset_counters()

    @property
    def block_count(self):
        return (self.samples+self.block_samples-1)//self.block_samples

    @property
    def hypotheses(self):
        return len(self.frequencies)*self.candidates*len(self.lags)

    def reset_counters(self):
        self.counters = dict(a_pack_calls=0, b_pack_calls=0, matmul_calls=0,
                             a_cache_hits=0, a_cache_evictions=0,
                             a_pack_input_bytes=0, b_pack_input_bytes=0,
                             a_complex_half_padding_bytes=0, b_complex_half_padding_bytes=0,
                             b_packed_bytes=0, a_packed_bytes=0,
                             gram_cache_hits=0, gram_cache_evictions=0,
                             gram_compute_calls=0, gram_h2d_calls=0, gram_h2d_bytes=0,
                             geometry_prepare_calls=0, geometry_cache_hits=0,
                             geometry_cache_evictions=0, geometry_prepare_seconds=0.,
                             fused_reconstruct_calls=0,
                             gather_seconds=0., a_prepare_seconds=0.,
                             b_pack_seconds=0., matmul_seconds=0.,
                             reconstruct_seconds=0., gram_seconds=0.,
                             gram_compute_seconds=0., gram_transfer_seconds=0.)

    def clear_cache(self):
        self._cache.clear()
        self._cache_bytes = 0
        self._gram_cache.clear()
        self._gram_cache_bytes = 0
        self._geometry_cache.clear()
        self._geometry_cache_bytes = 0

    def accounting(self):
        return dict(self.counters, template_cache_bytes=self._cache_bytes,
                    template_cache_limit_bytes=self.template_cache_bytes,
                    gram_cache_bytes=self._gram_cache_bytes,
                    gram_cache_limit_bytes=self.gram_cache_bytes,
                    geometry_cache_bytes=self._geometry_cache_bytes,
                    geometry_cache_limit_bytes=self.geometry_cache_bytes,
                    public_grid_device_bytes=sum(t.numel()*t.element_size() for t in
                        (self._lags_device, self._frequencies_device, self._lag_seconds)),
                    public_basis_cpu_bytes=self._basis.nbytes,
                    public_grid_cpu_bytes=self.frequencies.nbytes+self.lags.nbytes,
                    complex_half_alignment=self._half_alignment,
                    timing_kind="host_enqueue_unsynchronized",
                    precision=self.precision,
                    fused_reconstruction=self.fused_reconstruction,
                    admission_status=("diagnostic_only_uncalibrated" if self.precision == "diagnostic-fp64" else
                                      "uncalibrated_experimental" if self.backend else "strict_statistics_only"))

    def _pack(self, rows):
        return rows if self.backend is None else self.backend.pack_rows(rows)

    def _nbytes(self, packed):
        if self.backend is None:
            return packed.numel()*packed.element_size()
        if hasattr(self.backend, "packed_nbytes"):
            size = self.backend.packed_nbytes(packed)
        else:
            size = getattr(packed, "nbytes", None)
        if size is None:
            raise TypeError("native packed storage must expose packed_nbytes or nbytes")
        return _integer(size, "packed nbytes", 0)

    def _template_pack(self, block, first, stop, f0, f1, c0, c1):
        key = (block, f0, f1, c0, c1)
        if key in self._cache:
            self.counters["a_cache_hits"] += 1
            self._cache.move_to_end(key)
            return self._cache[key][0]
        started = time.perf_counter()
        # Generate time and CFO phase in FP64 before arithmetic conversion.
        times = torch.arange(first, stop, dtype=torch.float64, device=self.device)/self.sample_rate
        frequency = torch.tensor(self.frequencies[f0:f1].copy(), dtype=torch.float64, device=self.device)
        phase = torch.exp((2j*math.pi)*frequency[:, None]*times[None, :])
        source = torch.tensor(self._basis[c0:c1, first:stop, :].copy(), device=self.device).permute(0, 2, 1)
        templates = source[None, ...]*phase[:, None, None, :]
        half_width = math.ceil((stop-first)/self._half_alignment)*self._half_alignment
        rows = complex_template_rows(templates, half_alignment=self._half_alignment).reshape(-1, 2*half_width).to(self._dtype).contiguous()
        packed = self._pack(rows)
        size = self._nbytes(packed)
        self.counters["a_pack_calls"] += 1
        self.counters["a_pack_input_bytes"] += rows.numel()*rows.element_size()
        self.counters["a_complex_half_padding_bytes"] += rows.shape[0]*2*(half_width-(stop-first))*rows.element_size()
        self.counters["a_packed_bytes"] += size
        self.counters["a_prepare_seconds"] += time.perf_counter()-started
        if size <= self.template_cache_bytes:
            while self._cache and self._cache_bytes+size > self.template_cache_bytes:
                _, (_, old_size) = self._cache.popitem(last=False)
                self._cache_bytes -= old_size
                self.counters["a_cache_evictions"] += 1
            self._cache[key] = (packed, size)
            self._cache_bytes += size
        return packed

    def _observations(self, observations):
        if isinstance(observations, torch.Tensor):
            value = observations.detach().to(self.device)
        else:
            value = torch.as_tensor(np.asarray(observations), device=self.device)
        if value.ndim != 2 or value.shape[0] < 1 or not value.is_complex():
            raise ValueError("observations must be complex [trial,capture_sample]")
        if int(self.lags.max())+self.samples > value.shape[1]:
            raise ValueError("complete shifted template support leaves capture; no truncation is allowed")
        if not bool(torch.isfinite(value).all()):
            raise ValueError("observations must be finite")
        if self._dtype == torch.float32 and bool((value.abs() > torch.finfo(torch.float32).max).any()):
            raise ValueError("observation amplitude is outside the finite FP32 product-input range")
        # Preserve c128 actual-sample exports until the declared product input
        # conversion. No per-capture normalization or simulated-mean access.
        return value

    def _gram(self, first, stop, variance):
        started = time.perf_counter()
        key = (first, stop)
        if key in self._gram_cache:
            self._gram_cache.move_to_end(key)
            self.counters["gram_cache_hits"] += 1
            raw = self._gram_cache[key][0]
        else:
            computing = time.perf_counter()
            source = self._basis[:, first:stop, :]
            gram = np.einsum("csj,csk->cjk", source.conj(), source, optimize=True)
            self.counters["gram_compute_calls"] += 1
            self.counters["gram_compute_seconds"] += time.perf_counter()-computing
            transferring = time.perf_counter()
            raw = torch.tensor(gram, dtype=torch.complex128, device=self.device)
            self.counters["gram_transfer_seconds"] += time.perf_counter()-transferring
            size = raw.numel()*raw.element_size()
            if self.device.type == "cuda":
                self.counters["gram_h2d_calls"] += 1
                self.counters["gram_h2d_bytes"] += size
            if size <= self.gram_cache_bytes:
                while self._gram_cache and self._gram_cache_bytes+size > self.gram_cache_bytes:
                    _, (_, old_size) = self._gram_cache.popitem(last=False)
                    self._gram_cache_bytes -= old_size
                    self.counters["gram_cache_evictions"] += 1
                self._gram_cache[key] = (raw, size)
                self._gram_cache_bytes += size
        result = raw/variance
        self.counters["gram_seconds"] += time.perf_counter()-started
        return result

    def _column_geometry(self, total_columns, n0, n1, origin):
        key = (total_columns, n0, n1, origin)
        if key in self._geometry_cache:
            self._geometry_cache.move_to_end(key)
            self.counters["geometry_cache_hits"] += 1
            return self._geometry_cache[key][0]
        started = time.perf_counter()
        columns = torch.arange(n0, n1, device=self.device)
        trials, lag_indices = columns//len(self.lags), columns % len(self.lags)
        selected_lags = self._lags_device[lag_indices]
        selected_lag_seconds = selected_lags.to(torch.float64)/self.sample_rate
        offset = origin+selected_lag_seconds
        residual = torch.exp((-2j*math.pi)*self._frequencies_device[:, None]*offset[None, :])
        values = (columns, trials, lag_indices, selected_lags, selected_lag_seconds, residual)
        size = sum(value.numel()*value.element_size() for value in values)
        self.counters["geometry_prepare_calls"] += 1
        self.counters["geometry_prepare_seconds"] += time.perf_counter()-started
        if size <= self.geometry_cache_bytes:
            while self._geometry_cache and self._geometry_cache_bytes+size > self.geometry_cache_bytes:
                _, (_, old_size) = self._geometry_cache.popitem(last=False)
                self._geometry_cache_bytes -= old_size
                self.counters["geometry_cache_evictions"] += 1
            self._geometry_cache[key] = (values, size)
            self._geometry_cache_bytes += size
        return values

    def iter_tiles(self, observations, noise_variance=1., *, time_origin_seconds=0.,
                   start_block=0, stop_block=None) -> Iterator[CorrelationTile]:
        variance = _positive(noise_variance, "noise_variance")
        origin = float(time_origin_seconds)
        if not math.isfinite(origin):
            raise ValueError("time_origin_seconds must be finite")
        first_block = _integer(start_block, "start_block", 0)
        last_block = self.block_count if stop_block is None else _integer(stop_block, "stop_block")
        if not 0 <= first_block < last_block <= self.block_count:
            raise ValueError("invalid nonempty block range")
        if self.precision == "fp32":
            _require_ieee(self.device)
        observations = self._observations(observations)
        total_columns = observations.shape[0]*len(self.lags)
        reciprocal_variance = (torch.full((), 1./variance, dtype=torch.float64, device=self.device)
                               if self.fused_reconstruction else None)
        for block in range(first_block, last_block):
            first, stop = block*self.block_samples, min((block+1)*self.block_samples, self.samples)
            # Match BlockWorkspace's half-open physical-interval centers.
            center = (first+stop)/(2*self.sample_rate)
            previous = (max(0, first-self.block_samples)+first)/(2*self.sample_rate)
            dt = 0. if block == 0 else center-previous
            gram = self._gram(first, stop, variance)
            for n0 in range(0, total_columns, self.column_tile):
                started = time.perf_counter()
                columns, trials, lag_indices, selected_lags, selected_lag_seconds, residual = self._column_geometry(
                    total_columns, n0, min(n0+self.column_tile, total_columns), origin)
                samples = torch.arange(first, stop, device=self.device)
                windows = observations[trials[:, None], selected_lags[:, None]+samples[None, :]]
                rows = observation_rows(windows, half_alignment=self._half_alignment).to(self._dtype).contiguous()
                self.counters["gather_seconds"] += time.perf_counter()-started
                started = time.perf_counter()
                bpack = self._pack(rows)
                self.counters["b_pack_calls"] += 1
                self.counters["b_pack_input_bytes"] += rows.numel()*rows.element_size()
                self.counters["b_complex_half_padding_bytes"] += rows.shape[0]*(rows.shape[1]-2*(stop-first))*rows.element_size()
                self.counters["b_packed_bytes"] += self._nbytes(bpack)
                self.counters["b_pack_seconds"] += time.perf_counter()-started
                # CFO quantization occurs on templates; B does not depend on f.
                # Multiplication by this residual retains receiver-time phase.
                for f0 in range(0, len(self.frequencies), self.frequency_tile):
                    f1 = min(f0+self.frequency_tile, len(self.frequencies))
                    for c0 in range(0, self.candidates, self.candidate_tile):
                        c1 = min(c0+self.candidate_tile, self.candidates)
                        apack = self._template_pack(block, first, stop, f0, f1, c0, c1)
                        started = time.perf_counter()
                        product = (apack@bpack.T if self.backend is None else self.backend.matmul(apack, bpack))
                        expected = (2*(f1-f0)*(c1-c0)*self.taps, len(columns))
                        required_dtype = torch.float64 if self.precision in ("fp64", "diagnostic-fp64") else torch.float32
                        wrong_device = (product.device.type != self.device.type or
                                        (self.device.index is not None and product.device.index != self.device.index))
                        if product.shape != expected or product.dtype != required_dtype or wrong_device:
                            raise ValueError("backend returned incorrect product shape, dtype or device")
                        self.counters["matmul_calls"] += 1
                        self.counters["matmul_seconds"] += time.perf_counter()-started
                        started = time.perf_counter()
                        if self.fused_reconstruction:
                            from triad_correlation_fusion import reconstruct_correlations
                            value = reconstruct_correlations(product, residual[f0:f1], reciprocal_variance,
                                                             candidates=c1-c0, taps=self.taps)
                            self.counters["fused_reconstruct_calls"] += 1
                        else:
                            parts = product.reshape(f1-f0, c1-c0, self.taps, 2, len(columns)).double()
                            value = torch.complex(parts[:, :, :, 0], parts[:, :, :, 1])
                            value = value*residual[f0:f1, None, None, :]/variance
                            value = value.permute(3, 0, 1, 2).contiguous()
                        self.counters["reconstruct_seconds"] += time.perf_counter()-started
                        yield CorrelationTile(block, (first, stop), dt, slice(f0, f1), slice(c0, c1),
                                              columns, trials, lag_indices, selected_lag_seconds+(origin+center),
                                              value, gram[c0:c1])

    def iter_blocks(self, observations, noise_variance=1., *, time_origin_seconds=0.,
                    max_output_bytes=1024*1024*1024, start_block=0, stop_block=None) -> Iterator[CorrelationBlock]:
        """Materialize one complete block, rejecting excessive output before allocation."""
        batch = observations.shape[0]
        output_bytes = batch*self.hypotheses*self.taps*16
        if output_bytes > _integer(max_output_bytes, "max_output_bytes"):
            raise ValueError("dense block output exceeds max_output_bytes; use iter_tiles")
        value = None
        current = None
        last_tile = None
        for tile in self.iter_tiles(observations, noise_variance, time_origin_seconds=time_origin_seconds,
                                    start_block=start_block, stop_block=stop_block):
            if current != tile.block_index:
                if value is not None:
                    yield self._block(current, last_tile, value, grams, time_origin_seconds)
                current = tile.block_index
                value = torch.empty((batch, len(self.frequencies), self.candidates, len(self.lags), self.taps),
                                    device=self.device, dtype=torch.complex128)
                grams = torch.empty((self.candidates, self.taps, self.taps), device=self.device, dtype=torch.complex128)
            # Permuting the output first keeps paired advanced indices adjacent.
            destination = value.permute(0, 3, 1, 2, 4)
            destination[tile.trial_indices, tile.lag_indices, tile.frequency_slice, tile.candidate_slice] = tile.r
            grams[tile.candidate_slice] = tile.gram
            last_tile = tile
        if value is not None:
            yield self._block(current, last_tile, value, grams, time_origin_seconds)

    def _block(self, index, tile, value, grams, origin):
        first, stop = tile.interval
        centers = self._lag_seconds+(origin+(first+stop)/(2*self.sample_rate))
        return CorrelationBlock(index, tile.interval, tile.dt, centers, value, grams)


def direct_fp64_reference(basis, observations, sample_rate, frequencies_hz, lags,
                          block_samples, noise_variance=1., *, time_origin_seconds=0.):
    """Independent NumPy complex128 oracle on the exact represented input samples.

    Returns [(r[trial,CFO,candidate,lag,tap], G[candidate,tap,tap]), ...].
    This intentionally does not use the real GEMM decomposition or bank cache.
    """
    source, observed = _cpu_complex(basis, "basis"), _cpu_complex(observations, "observations")
    if source.ndim != 3 or min(source.shape) < 1 or observed.ndim != 2 or min(observed.shape) < 1:
        raise ValueError("invalid source or observation shape")
    rate, variance = _positive(sample_rate, "sample_rate"), _positive(noise_variance, "noise_variance")
    block_samples = _integer(block_samples, "block_samples")
    lag_values = [_integer(lag, "lag", 0) for lag in lags]
    frequencies = np.asarray(frequencies_hz, dtype=np.float64)
    origin = float(time_origin_seconds)
    if not lag_values or frequencies.ndim != 1 or not frequencies.size or not np.isfinite(frequencies).all() or not math.isfinite(origin):
        raise ValueError("invalid reference grid or time origin")
    if max(lag_values)+source.shape[1] > observed.shape[1]:
        raise ValueError("complete shifted template support leaves capture")
    output = []
    for first in range(0, source.shape[1], block_samples):
        stop = min(first+block_samples, source.shape[1])
        local = source[:, first:stop, :]
        gram = np.stack([design.conj().T@design for design in local])/variance
        result = np.empty((len(observed), len(frequencies), len(source), len(lag_values), source.shape[2]), dtype=np.complex128)
        for fi, frequency in enumerate(frequencies):
            for li, lag in enumerate(lag_values):
                # Keep local FP64 phase separate from the potentially long
                # receiver origin to avoid losing fine sample-time increments.
                phase = np.exp((-2j*math.pi*frequency)*(np.arange(first, stop, dtype=np.float64)/rate))
                phase *= np.exp((-2j*math.pi*frequency)*(origin+lag/rate))
                for ci, design in enumerate(local):
                    result[:, fi, ci, li] = (observed[:, first+lag:stop+lag]*phase)@design.conj()/variance
        output.append((result, gram))
    return output


def representation_diagnostics(a_rows, b_rows, represented_a, represented_b):
    """Separate one/both-operand representation loss in CPU FP64 dot products.

    Callers must decode the *actual* packed operands, including scales, rather
    than substituting an unrelated quantizer. These are diagnostic products;
    they do not establish native instruction dispatch or admission calibration.
    """
    values = []
    for source in (a_rows, b_rows, represented_a, represented_b):
        if isinstance(source, torch.Tensor):
            source = source.detach().cpu().numpy()
        value = np.asarray(source, dtype=np.float64)
        if value.ndim != 2 or not np.isfinite(value).all():
            raise ValueError("diagnostic operands must be finite matrices")
        values.append(value)
    a, b, qa, qb = values
    if a.shape != qa.shape or b.shape != qb.shape or a.shape[1] != b.shape[1]:
        raise ValueError("diagnostic operand shapes differ")
    return dict(strict=a@b.T, template_only=qa@b.T, observation_only=a@qb.T,
                both=qa@qb.T)

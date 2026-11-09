#!/usr/bin/env python3
"""Bounded reusable DATA templates for mapped three-carrier timing searches.

Only unwarped keyed DATA is cached. Receiver timing, Doppler/CFO and the final
real-audio center phase are applied to each public candidate afterwards. The
cache is independent of received values and likelihoods. Its cubic interpolation
is an approximation, qualified against the direct native map; the reported
spectral error proxy is a planning criterion, not a finite-pulse bandlimit.
Unsupported pulse/oversampling regimes explicitly use the native reference.
"""
from __future__ import annotations

from collections import OrderedDict
import ctypes
from functools import lru_cache
import math
from pathlib import Path
import shutil
import sys

import numpy as np


_MAX_PINNED_TILES = 32


def mapped_kernel_path(exporter):
    """The CMake-built mapped renderer is colocated with the native exporter."""
    executable = Path(shutil.which(str(exporter)) or exporter).resolve()
    suffix = '.dll' if sys.platform == 'win32' else '.dylib' if sys.platform == 'darwin' else '.so'
    return executable.with_name('triad_mapped_kernel' + suffix)


class _MappedRow(ctypes.Structure):
    _fields_ = [('fraction', ctypes.c_double), ('scale', ctypes.c_double),
        ('phase', ctypes.c_double), ('phase_step', ctypes.c_double),
        ('origin', ctypes.c_int64), ('tile_count', ctypes.c_size_t),
        ('tiles', ctypes.c_void_p), ('output', ctypes.c_void_p)]


@lru_cache(maxsize=4)
def _load_kernel(path):
    try:
        render = ctypes.CDLL(str(path)).triad_render_mapped_v1
    except (OSError, AttributeError) as error:
        raise OSError('mapped basis kernel unavailable; rebuild triad_helper_export: ' + str(path)) from error
    render.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.c_size_t]
    render.restype = None
    return render


class CachedMappedBasis:
    """Same mapped-basis call contract as a NativeStream, with bounded LRU data.

    ``basis_mapped(first, offsets, rows, audio_center_hz)`` returns complex128
    ``[candidate row, observation sample]``. Row fields retain the native
    ``(bit, epoch, arrival, frequency, clock_ppm, path_delay, path_doppler)``
    convention. The full source-time shift is applied before all three carrier
    samples are summed; this is not a carrier-label channel approximation.
    """
    def __init__(self, source, *, cache_bytes=32*1024**2, tile_samples=8192,
                 minimum_oversampling=8., maximum_dense_factor=4,
                 executor=None, workers=1, kernel_path=None, accelerator=None):
        self.source = source
        if (isinstance(tile_samples, bool) or not isinstance(tile_samples, int) or not 4 <= tile_samples <= 16384 or
            isinstance(cache_bytes, bool) or not isinstance(cache_bytes, int) or not 2*tile_samples*16 <= cache_bytes <= 1024**3 or
            isinstance(maximum_dense_factor, bool) or not isinstance(maximum_dense_factor, int) or not 1 <= maximum_dense_factor <= 16 or
            not math.isfinite(minimum_oversampling) or minimum_oversampling < 8):
            raise ValueError('invalid bounded tracking-template cache geometry')
        self.tile_samples = tile_samples
        self.byte_limit = cache_bytes
        self.cache = OrderedDict()
        self.bytes = self.peak_bytes = 0
        self.calls = self.cache_hits = self.cache_misses = self.native_calls = self.evictions = 0
        self.interpolated_rows = self.direct_rows = self.native_samples = 0
        self.large_phase_fallback_rows = 0
        self.staging_fallback_rows = 0
        self.mapped_worker_tasks = self.maximum_mapped_workers = 0
        if (isinstance(workers, bool) or not isinstance(workers, int) or workers < 1 or
                (workers > 1 and executor is None)):
            raise ValueError('mapped basis workers require the shared receiver executor')
        self.executor, self.workers = executor, workers
        self.accelerator = accelerator
        self.kernel = None
        self.staging_workspace_bytes = 0
        d = source.descriptor
        self.sample_rate = float(d['sample_rate_hz'])
        self.source_samples = int(d['source_samples'])
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0 or not 0 < self.source_samples < (1 << 64):
            raise ValueError('invalid native template source extent')
        f = source.plan.get('format', {})
        width = float(d.get('nominal_triplet_bandwidth_hz', math.nan))
        self.fallback_reason = None
        if f.get('pulse_shape') != 'rrc-025-span8-c2-tail2':
            self.fallback_reason = 'unsupported-or-abrupt-pulse'
        if not isinstance(f.get('fhss'), bool):
            self.fallback_reason = 'missing-gate-description'
        if f.get('fhss'):
            ramp = float(f.get('ramp_seconds', math.nan))
            if not math.isfinite(ramp) or ramp <= 0:
                self.fallback_reason = 'invalid-gate-description'
            else:
                width += 2/ramp
        if not math.isfinite(width) or width <= 0:
            self.fallback_reason = 'missing-spectral-description'
            width = self.sample_rate
        wanted = minimum_oversampling*width/self.sample_rate
        self.dense_factor = max(1, math.ceil(wanted))
        if self.dense_factor > maximum_dense_factor:
            self.fallback_reason = 'insufficient-cache-oversampling'
        # Uniform dense samples remain a public geometric preparation choice.
        # A data-dependent received-score choice is never made here.
        self.effective_width_hz = width
        self.oversampling_ratio = self.sample_rate*self.dense_factor/width
        self.interpolation_proxy = (3/128)*(math.pi/self.oversampling_ratio)**4
        self.dense_source_samples = self.source_samples*self.dense_factor
        self.maximum_dense_factor = maximum_dense_factor
        if not self.fallback_reason:
            if accelerator is None:
                if kernel_path is None:
                    kernel_path = mapped_kernel_path(source.command[0])
                self.kernel = _load_kernel(Path(kernel_path))
            # Bounded pinned arrays may outlive eviction from the LRU until a
            # batch joins. Include native response and descriptor/map staging.
            self.staging_workspace_bytes = (
                (_MAX_PINNED_TILES+4)*self.tile_samples*16+512*1024)
        if accelerator is not None:
            self.staging_workspace_bytes += accelerator.workspace_bytes

    def _insert(self, key, data):
        previous = self.cache.pop(key, None)
        if previous is not None:
            self.bytes -= previous.nbytes
        while self.bytes+data.nbytes > self.byte_limit:
            _, old = self.cache.popitem(last=False)
            self.bytes -= old.nbytes
            self.evictions += 1
        self.cache[key] = data
        self.bytes += data.nbytes
        self.peak_bytes = max(self.peak_bytes, self.bytes)

    def _tile(self, bit, epoch, tile):
        key = (bit, epoch, tile)
        if key in self.cache:
            self.cache_hits += 1
            self.cache.move_to_end(key)
            return self.cache[key]
        self.cache_misses += 1
        begin = tile*self.tile_samples
        count = min(self.tile_samples, self.dense_source_samples-begin)
        if tile < 0 or count <= 0:
            raise ValueError('internal template tile outside source support')
        rows = [(b, epoch, 0., 0., 0., 0., 0.) for b in (0, 1)]
        if self.dense_factor == 1:
            data = self.source.basis(begin, count, rows)
        else:
            first, fraction = divmod(begin, self.dense_factor)
            offsets = (fraction+np.arange(count, dtype=np.float64))/self.dense_factor
            data = self.source.basis_mapped(first, offsets, rows, 0.)
        values = np.asarray(data, dtype=np.complex128)
        if values.shape != (2, count) or not np.isfinite(values).all():
            raise ValueError('native cache preparation returned invalid DATA samples')
        self.native_calls += 1
        self.native_samples += 2*count
        for b in (0, 1):
            # Fixed-size tiles keep the memory bound independent of endpoints.
            stored = np.zeros(self.tile_samples, dtype=np.complex128)
            stored[:count] = values[b]
            stored.flags.writeable = False
            self._insert((b, epoch, tile), stored)
        self.cache.move_to_end(key)
        return self.cache[key]

    def _direct(self, first, offsets, rows, center):
        self.direct_rows += len(rows)
        values = self.source.basis_mapped(first, offsets, rows, center)
        return values if self.accelerator is None else self.accelerator.transfer(values)

    def _render(self, rows, offsets, answer, pinned):
        if self.accelerator is not None:
            self.accelerator.render(rows, offsets, answer, pinned)
            self.interpolated_rows += len(rows)
            return
        descriptors = (_MappedRow*len(rows))(*(row for row, _ in rows))
        offset_data = offsets.ctypes.data

        def run(lo, hi, _keep_alive=(rows, answer, pinned)):
            # Each future retains the pointer targets even if its controller
            # is interrupted while waiting.
            self.kernel(ctypes.byref(descriptors, lo*ctypes.sizeof(_MappedRow)), hi-lo,
                        offset_data, len(offsets), self.tile_samples)

        workers = min(self.workers, len(rows))
        if workers == 1:
            run(0, len(rows))
        else:
            futures, error = [], None
            for worker in range(workers):
                try:
                    futures.append(self.executor.submit(run, worker*len(rows)//workers,
                                                         (worker+1)*len(rows)//workers))
                except BaseException as exc:
                    error = exc
                    break
            # Join every worker before the controller can release its input
            # tiles or output, even after a submission or worker error.
            for future in futures:
                try:
                    future.result()
                except BaseException as exc:
                    if error is None:
                        error = exc
            if error is not None:
                raise error
        self.interpolated_rows += len(rows)
        self.mapped_worker_tasks += workers
        self.maximum_mapped_workers = max(self.maximum_mapped_workers, workers)

    def basis_mapped(self, first, offsets, rows, audio_center_hz):
        offsets = np.asarray(offsets, dtype=np.float64)
        if (isinstance(first, bool) or not isinstance(first, int) or not 0 <= first < (1 << 64) or
            offsets.ndim != 1 or not 1 <= len(offsets) <= 16384 or not np.isfinite(offsets).all() or
            offsets[0] < 0 or offsets[-1] > 1048576 or np.any(np.diff(offsets) <= 0) or
            np.longdouble(offsets[-1]) > np.longdouble((1 << 64)-1-first) or
            not 1 <= len(rows) <= 128 or not math.isfinite(audio_center_hz) or not 0 <= audio_center_hz <= 120e6):
            raise ValueError('bounded increasing mapped cache coordinates required')
        parsed = []
        for row in rows:
            if len(row) != 7:
                raise ValueError('mapped template rows need seven fields')
            bit, epoch, arrival, frequency, clock, delay, doppler = row
            if (bit not in (0, 1) or int(bit) != bit or not -6 <= epoch <= 6 or int(epoch) != epoch or
                not all(math.isfinite(x) for x in (arrival, frequency, clock, delay, doppler)) or
                clock <= -1e6 or delay < 0):
                raise ValueError('invalid mapped template candidate')
            parsed.append((int(bit), int(epoch), float(arrival), float(frequency), float(clock), float(delay), float(doppler)))
        self.calls += 1
        if self.fallback_reason:
            return self._direct(first, offsets, parsed, audio_center_hz)
        # Native mapped synthesis uses a long-double integer origin plus small
        # relative coordinates. Preserve that arithmetic instead of converting
        # first+offset to an absolute FP64 timestamp.
        if np.finfo(np.longdouble).nmant < 63 and first > (1 << 48):
            return self._direct(first, offsets, parsed, audio_center_hz)
        offsets = np.ascontiguousarray(offsets)
        fs = np.longdouble(self.sample_rate)
        first_ld = np.longdouble(first)
        initial_time = first_ld/fs
        # Compute only the per-row absolute origins in extended precision.
        # Positive affine maps let their two endpoints bound all tile accesses.
        values = np.asarray(parsed, dtype=np.float64)
        _, _, arrival, frequency, clock, delay, doppler = values.T
        # Extreme finite input can overflow an intermediate. The endpoint
        # and phase guards below then use the bounded native reference.
        with np.errstate(over='ignore', invalid='ignore'):
            scale = 1+clock*1e-6
            shift = (arrival+delay).astype(np.longdouble)*fs
            wide_scale = scale.astype(np.longdouble)
            dense_scale = scale*self.dense_factor
            dense_origin = ((first_ld-shift)*wide_scale)*self.dense_factor
            origin_floor = np.floor(dense_origin)
            fraction = (dense_origin-origin_floor).astype(np.float64)
            relative_first = offsets[0]*dense_scale+fraction
            relative_last = offsets[-1]*dense_scale+fraction
            difference = ((wide_scale-1)*first_ld-wide_scale*shift)/fs
            terms = (frequency.astype(np.longdouble)*initial_time,
                     doppler.astype(np.longdouble)*initial_time,
                     np.longdouble(audio_center_hz)*difference)
            large_phase = np.maximum.reduce([np.abs(term) for term in terms]) > 2**40
            constant = np.remainder(sum(np.remainder(term, 1) for term in terms), 1).astype(np.float64)
            slope = (frequency+doppler+audio_center_hz*(scale-1))/self.sample_rate
            phase_first = constant+slope*offsets[0]
            phase_last = constant+slope*offsets[-1]

        shape = (len(parsed), len(offsets))
        answer = (np.empty(shape, dtype=np.complex128) if self.accelerator is None
                  else self.accelerator.empty(shape))
        answer_data = answer.ctypes.data if self.accelerator is None else None
        pending, pinned = [], {}

        def flush():
            if pending:
                self._render(pending, offsets, answer, pinned)
                pending.clear()
                pinned.clear()

        for column, row in enumerate(parsed):
            bit, epoch = row[:2]
            if not (math.isfinite(relative_first[column]) and math.isfinite(relative_last[column])):
                answer[column] = self._direct(first, offsets, [row], audio_center_hz)[0]
                continue
            origin = int(origin_floor[column])
            lower = origin+math.floor(relative_first[column])-1
            upper = origin+math.floor(relative_last[column])+2
            if upper < 0 or lower >= self.dense_source_samples:
                if self.accelerator is None:
                    answer[column] = 0
                continue
            low_tile = max(0, lower)//self.tile_samples
            high_tile = min(self.dense_source_samples-1, upper)//self.tile_samples
            count = high_tile-low_tile+1
            bounded = (abs(relative_last[column]) <= 2**31 and
                       abs(phase_first[column]) <= 2**30 and abs(phase_last[column]) <= 2**30)
            if large_phase[column] or not bounded or count > _MAX_PINNED_TILES:
                self.large_phase_fallback_rows += int(large_phase[column])
                self.staging_fallback_rows += int(count > _MAX_PINNED_TILES)
                answer[column] = self._direct(first, offsets, [row], audio_center_hz)[0]
                continue
            keys = [(bit, epoch, tile) for tile in range(low_tile, high_tile+1)]
            if len(pinned)+sum(key not in pinned for key in keys) > _MAX_PINNED_TILES:
                flush()
            pointers = (ctypes.c_void_p*count)()
            for i, key in enumerate(keys):
                if key in pinned and key not in self.cache:
                    self.cache_hits += 1
                    tile = pinned[key]
                else:
                    tile = self._tile(*key)
                    # An earlier descriptor may still point to the old array
                    # if a sibling fill reintroduced this key into the LRU.
                    tile = pinned.setdefault(key, tile)
                pointers[i] = tile.ctypes.data
            pending.append((_MappedRow(fraction[column], dense_scale[column], constant[column],
                slope[column], origin-low_tile*self.tile_samples, count, ctypes.addressof(pointers),
                answer_data+16*column*len(offsets) if answer_data is not None else column), pointers))
        flush()
        if self.accelerator is None and not np.isfinite(answer).all():
            raise FloatingPointError('mapped template cache produced nonfinite samples')
        return answer

    def report(self):
        return dict(method='uniform-DATA-cache-native-cubic-Lagrange-v2', calls=self.calls,
            interpolated_rows=self.interpolated_rows, direct_rows=self.direct_rows,
            cache_hits=self.cache_hits, cache_misses=self.cache_misses,
            native_preparation_calls=self.native_calls, native_preparation_samples=self.native_samples,
            evictions=self.evictions, cache_bytes=self.bytes, peak_cache_bytes=self.peak_bytes,
            cache_limit_bytes=self.byte_limit, tile_samples=self.tile_samples,
            dense_factor=self.dense_factor, oversampling_ratio=self.oversampling_ratio,
            large_phase_fallback_rows=self.large_phase_fallback_rows,
            staging_fallback_rows=self.staging_fallback_rows,
            staging_workspace_bytes=self.staging_workspace_bytes,
            mapped_worker_tasks=self.mapped_worker_tasks, maximum_mapped_workers=self.maximum_mapped_workers,
            **({'accelerator': self.accelerator.report()} if self.accelerator is not None else {}),
            interpolation_amplitude_proxy=self.interpolation_proxy, fallback_reason=self.fallback_reason,
            scope='metadata-selected interpolation; finite-pulse proxy is not a strict spectral/error bound; direct native fallback retained')

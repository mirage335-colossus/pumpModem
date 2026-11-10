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


_MAPPED_ROW_DTYPE = np.dtype(_MappedRow)


class _MappedBlock:
    """Prepared, disjoint DATA row writes and their pinned native inputs."""

    def __init__(self, renderer, candidates, taps, samples):
        self.renderer = renderer
        self.raw = np.empty((2*candidates, taps, samples), dtype=np.complex128)
        self.batches = []
        self.pins = {}

    def append(self, first, descriptors, offsets, tables, pins):
        self.batches.append((first, descriptors, offsets, tables))
        self.pins.update({id(tile): tile for tile in pins})

    def dispatch(self):
        if self.batches:
            self.dispatch_group((self,), (None,))

    def pack_pairs(self):
        """Only re-layout an entirely staged block with no preceding writes."""
        expected = 0
        if not self.batches:
            return None
        offsets = self.batches[0][2]
        for base, descriptors, coordinates, _ in self.batches:
            if base != expected or coordinates is not offsets:
                return None
            expected += len(descriptors)
        count, taps, samples = self.raw.shape
        if expected != count*taps:
            return None
        candidates = count//2
        descriptors = np.concatenate([entry[1] for entry in self.batches])
        pairs = descriptors.reshape(2, candidates, taps).transpose(1, 2, 0).copy()
        output_rows = np.arange(count*taps, dtype=np.uintp).reshape(candidates, 2, taps)
        pairs['output'] = self.raw.ctypes.data+output_rows.transpose(0, 2, 1)*samples*16
        return pairs, offsets

    @staticmethod
    def dispatch_group(blocks, statistics):
        renderer = blocks[0].renderer
        count, taps, _ = blocks[0].raw.shape
        packed = ([block.pack_pairs() for block in blocks]
                  if renderer.paired_kernel is not None and all(statistics) else None)
        paired = packed is not None and all(plan is not None for plan in packed)
        work_items = count//2 if paired else count
        workers = min(renderer.workers, work_items)
        # Keep all pointer targets in the submitted closure until its join.
        prepared = tuple((block.raw, tuple(block.batches), tuple(block.pins.values()), statistic)
                         for block, statistic in zip(blocks, statistics, strict=True))

        def run(first, stop):
            result = []
            for index, (raw, batches, _pins, statistic) in enumerate(prepared):
                if paired:
                    descriptors, offsets = packed[index]
                    renderer.paired_kernel(descriptors.ctypes.data+
                        first*taps*2*_MAPPED_ROW_DTYPE.itemsize, (stop-first)*taps,
                        offsets.ctypes.data, raw.shape[-1], renderer.tile_samples)
                    result.append(statistic(raw[2*first:2*stop]))
                else:
                    row_first, row_stop = first*taps, stop*taps
                    for base, desc, offsets, _tables in batches:
                        lo, hi = max(base, row_first), min(base+len(desc), row_stop)
                        if lo < hi:
                            renderer.kernel(desc.ctypes.data+(lo-base)*_MAPPED_ROW_DTYPE.itemsize, hi-lo,
                                            offsets.ctypes.data, raw.shape[-1], renderer.tile_samples)
                    result.append(None if statistic is None else statistic(raw[first:stop]))
            return result

        if workers == 1:
            results = [run(0, work_items)]
        else:
            futures, results, error = [], [], None
            for worker in range(workers):
                try:
                    futures.append(renderer.executor.submit(
                        run, worker*work_items//workers, (worker+1)*work_items//workers))
                except BaseException as exc:
                    error = exc
                    break
            for future in futures:
                try:
                    results.append(future.result())
                except BaseException as exc:
                    if error is None:
                        error = exc
            if error is not None:
                raise error
        if any(block.batches for block in blocks):
            renderer.mapped_worker_tasks += workers
            renderer.maximum_mapped_workers = max(renderer.maximum_mapped_workers, workers)
        for block in blocks:
            block.batches.clear()
            block.pins.clear()
        return list(zip(*results, strict=True)), paired


@lru_cache(maxsize=4)
def _load_kernel(path, symbol='triad_render_mapped_v1'):
    try:
        render = getattr(ctypes.CDLL(str(path)), symbol)
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
    The CPU receiver may supply its own contiguous complex128 output block.
    The renderer joins all writes before returning it.
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
        self.row_tile_references = 0
        self.large_phase_fallback_rows = 0
        self.staging_fallback_rows = 0
        self.mapped_worker_tasks = self.maximum_mapped_workers = 0
        if (isinstance(workers, bool) or not isinstance(workers, int) or workers < 1 or
                (workers > 1 and executor is None)):
            raise ValueError('mapped basis workers require the shared receiver executor')
        self.executor, self.workers = executor, workers
        self.accelerator = accelerator
        self.kernel = None
        self.paired_kernel = None
        self.native_gaussian = None
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
                self.paired_kernel = _load_kernel(Path(kernel_path), 'triad_render_mapped_paired_v1')
                from triad_gaussian_native import NativeGaussianBatch
                self.native_gaussian = NativeGaussianBatch(Path(kernel_path), executor, workers)
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

    def _stage_mapped(self, block, values, offsets, answer, origin_floor, fraction,
                      dense_scale, constant, slope, relative_first, relative_last,
                      large_phase, phase_first, phase_last):
        # A simple signed range makes all vectorized endpoint arithmetic
        # exact. Extreme inputs and out-of-support rows use the normal map.
        if (self.dense_source_samples >= 2**62 or np.any(large_phase) or
                not (np.abs(origin_floor) < 2**60).all() or
                not (np.abs(relative_first) <= 2**31).all() or
                not (np.abs(relative_last) <= 2**31).all() or
                not (np.abs(phase_first) <= 2**30).all() or
                not (np.abs(phase_last) <= 2**30).all()):
            return False
        origin = origin_floor.astype(np.int64)
        lower = origin+np.floor(relative_first).astype(np.int64)-1
        upper = origin+np.floor(relative_last).astype(np.int64)+2
        if np.any(upper < 0) or np.any(lower >= self.dense_source_samples):
            return False
        low = np.maximum(lower, 0)//self.tile_samples
        high = np.minimum(upper, self.dense_source_samples-1)//self.tile_samples
        spans = np.column_stack((values[:, :2].astype(np.int64), low, high))
        boundaries = [0, *(np.flatnonzero(np.any(spans[1:] != spans[:-1], axis=1))+1),
                      len(spans)]
        # Count at most the current pin limit, including both members of
        # every source tile fill, before preparing any native data.
        keys = set()
        for at in boundaries[:-1]:
            bit, epoch, lo, hi = map(int, spans[at])
            if hi-lo >= _MAX_PINNED_TILES:
                return False
            keys.update((bit, epoch, k) for k in range(lo, hi+1))
            if len(keys) > _MAX_PINNED_TILES:
                return False
        if len(keys)*2*self.tile_samples*16 > self.byte_limit:
            return False
        if len(block.pins)+len(keys) > _MAX_PINNED_TILES:
            block.dispatch()
        descriptors = np.empty(len(values), dtype=_MAPPED_ROW_DTYPE)
        descriptors['fraction'] = fraction
        descriptors['scale'] = dense_scale
        descriptors['phase'] = constant
        descriptors['phase_step'] = slope
        descriptors['origin'] = origin-low*self.tile_samples
        descriptors['tile_count'] = high-low+1
        descriptors['output'] = answer.ctypes.data+np.arange(len(values), dtype=np.uintp)*len(offsets)*16
        tables, pins = [], {}
        for at, stop in zip(boundaries, boundaries[1:]):
            bit, epoch, lo, hi = map(int, spans[at])
            keys = [(bit, epoch, k) for k in range(lo, hi+1)]
            for key in keys:
                # Filling the sibling may replace a resident array with the
                # same key. Earlier descriptors still own the first array.
                pins.setdefault(key, self._tile(*key))
            table = (ctypes.c_void_p*len(keys))(*(pins[key].ctypes.data for key in keys))
            tables.append(table)
            descriptors['tiles'][at:stop] = ctypes.addressof(table)
            if stop-at > 1:
                # Repeated identical spans would touch these same live
                # tiles in this order without another native fill.
                for key in keys:
                    self.cache.move_to_end(key)
        first_row = (answer.ctypes.data-block.raw.ctypes.data)//(16*len(offsets))
        block.append(first_row, descriptors, offsets, tables, pins.values())
        self.interpolated_rows += len(values)
        self.row_tile_references += int(np.sum(high-low+1))
        return True

    def block_statistics(self, requests, rows, center, candidates, taps):
        """Render up to two prepared blocks on each candidate tile."""
        if (self.kernel is None or not 1 <= len(requests) <= 2 or
                len(rows) != 2*candidates*taps or len(rows) > 1024 or
                any(not 1 <= len(offsets) <= 16384 for _, offsets, *_ in requests)):
            return None

        def make_statistic(projection, observation, variance):
            def statistic(raw):
                if not np.isfinite(raw).all():
                    raise ValueError('mapped template cache produced nonfinite samples')
                projected = projection.numpy(raw)
                if not np.isfinite(projected).all():
                    raise ValueError('projected basis is nonfinite')
                conjugate = projected.conj()
                gram = np.einsum('ctn,csn->cts', conjugate, projected, optimize=True)/variance
                correlation = (conjugate.reshape(-1, projected.shape[-1]) @ observation).reshape(
                    -1, taps)/variance
                return correlation, gram, float(np.vdot(raw, raw).real)
            return statistic

        blocks, statistics = [], []
        for first, offsets, projection, observation, variance in requests:
            block = _MappedBlock(self, candidates, taps, len(offsets))
            flat = block.raw.reshape(-1, len(offsets))
            for at in range(0, len(rows), 128):
                self.basis_mapped(first, offsets, rows[at:at+128], center,
                                  out=flat[at:at+128], _stage=block)
            blocks.append(block)
            statistics.append(make_statistic(projection, observation, variance))
        result = []
        results, paired = _MappedBlock.dispatch_group(blocks, statistics)
        for parts in results:
            r = np.zeros((3, candidates, taps), dtype=np.complex128)
            gram = np.zeros((3, candidates, taps, taps), dtype=np.complex128)
            data_r = np.concatenate([part[0] for part in parts])
            data_gram = np.concatenate([part[1] for part in parts])
            if paired:
                r[1:] = data_r.reshape(candidates, 2, taps).swapaxes(0, 1)
                gram[1:] = data_gram.reshape(candidates, 2, taps, taps).swapaxes(0, 1)
            else:
                r[1:] = data_r.reshape(2, candidates, taps)
                gram[1:] = data_gram.reshape(2, candidates, taps, taps)
            result.append((r, gram, math.fsum(part[2] for part in parts)))
        return result

    def basis_mapped(self, first, offsets, rows, audio_center_hz, *, out=None, _stage=None):
        offsets = np.asarray(offsets, dtype=np.float64)
        if (isinstance(first, bool) or not isinstance(first, int) or not 0 <= first < (1 << 64) or
            offsets.ndim != 1 or not 1 <= len(offsets) <= 16384 or not np.isfinite(offsets).all() or
            offsets[0] < 0 or offsets[-1] > 1048576 or np.any(np.diff(offsets) <= 0) or
            np.longdouble(offsets[-1]) > np.longdouble((1 << 64)-1-first) or
            not 1 <= len(rows) <= 128 or not math.isfinite(audio_center_hz) or not 0 <= audio_center_hz <= 120e6):
            raise ValueError('bounded increasing mapped cache coordinates required')
        if _stage is None:
            parsed = []
            for row in rows:
                if len(row) != 7:
                    raise ValueError('mapped template rows need seven fields')
                bit, epoch, arrival, frequency, clock, delay, doppler = row
                if (bit not in (0, 1) or int(bit) != bit or not -6 <= epoch <= 6 or int(epoch) != epoch or
                    not all(math.isfinite(x) for x in (arrival, frequency, clock, delay, doppler)) or
                    clock <= -1e6 or delay < 0):
                    raise ValueError('invalid mapped template candidate')
                parsed.append((int(bit), int(epoch), float(arrival), float(frequency),
                               float(clock), float(delay), float(doppler)))
            values = np.asarray(parsed, dtype=np.float64)
        else:
            values = np.asarray(rows, dtype=np.float64)
            if (values.shape != (len(rows), 7) or not np.isfinite(values).all() or
                    np.any((values[:, 0] != 0) & (values[:, 0] != 1)) or
                    np.any(np.abs(values[:, 1]) > 6) or np.any(values[:, 1] != np.floor(values[:, 1])) or
                    np.any(values[:, 4] <= -1e6) or np.any(values[:, 5] < 0)):
                raise ValueError('invalid mapped template candidate')
        shape = (len(rows), len(offsets))
        if out is not None and (self.accelerator is not None or type(out) is not np.ndarray or
                out.shape != shape or out.dtype != np.complex128 or
                not out.flags.c_contiguous or not out.flags.aligned or not out.flags.writeable or
                np.may_share_memory(out, offsets)):
            raise ValueError('mapped destination must be separate writable aligned contiguous complex128 CPU storage')
        self.calls += 1
        # Native mapped synthesis uses a long-double integer origin plus small
        # relative coordinates. Preserve that arithmetic instead of converting
        # first+offset to an absolute FP64 timestamp.
        if self.fallback_reason or (np.finfo(np.longdouble).nmant < 63 and first > (1 << 48)):
            if _stage is not None:
                _stage.dispatch()
                parsed = [(int(bit), int(epoch), arrival, frequency, clock, delay, doppler)
                          for bit, epoch, arrival, frequency, clock, delay, doppler in values]
            values = self._direct(first, offsets, parsed, audio_center_hz)
            if out is None:
                return values
            out[:] = values
            return out
        offsets = np.ascontiguousarray(offsets)
        fs = np.longdouble(self.sample_rate)
        first_ld = np.longdouble(first)
        initial_time = first_ld/fs
        # Compute only the per-row absolute origins in extended precision.
        # Positive affine maps let their two endpoints bound all tile accesses.
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

        if _stage is not None:
            if self._stage_mapped(_stage, values, offsets, out, origin_floor, fraction,
                    dense_scale, constant, slope, relative_first, relative_last,
                    large_phase, phase_first, phase_last):
                return out
            _stage.dispatch()
            parsed = [(int(bit), int(epoch), arrival, frequency, clock, delay, doppler)
                      for bit, epoch, arrival, frequency, clock, delay, doppler in values]
        answer = out
        if answer is None:
            answer = (np.empty(shape, dtype=np.complex128) if self.accelerator is None
                      else self.accelerator.empty(shape))
        answer_data = answer.ctypes.data if self.accelerator is None else None
        pending, pinned, tile_tables = [], {}, {}

        def flush():
            if pending:
                self._render(pending, offsets, answer, pinned)
                pending.clear()
                pinned.clear()
                tile_tables.clear()

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
            self.row_tile_references += count
            if len(pinned)+sum(key not in pinned for key in keys) > _MAX_PINNED_TILES:
                flush()
            # Rows with the same span reference the same pinned arrays until
            # flush joins all readers. Preserve each row's LRU touches.
            span = (bit, epoch, low_tile, count) if self.accelerator is None else None
            pointers = tile_tables.get(span) if span is not None else None
            new_table = pointers is None
            if new_table:
                pointers = (ctypes.c_void_p*count)()
                if span is not None:
                    tile_tables[span] = pointers
            for i, key in enumerate(keys):
                if key in pinned and key not in self.cache:
                    self.cache_hits += 1
                    tile = pinned[key]
                else:
                    tile = self._tile(*key)
                    # An earlier descriptor may still point to the old array
                    # if a sibling fill reintroduced this key into the LRU.
                    tile = pinned.setdefault(key, tile)
                if new_table:
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
            row_tile_references=self.row_tile_references,
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

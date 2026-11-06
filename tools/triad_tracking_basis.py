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
import math

import numpy as np


class CachedMappedBasis:
    """Same mapped-basis call contract as a NativeStream, with bounded LRU data.

    ``basis_mapped(first, offsets, rows, audio_center_hz)`` returns complex128
    ``[candidate row, observation sample]``. Row fields retain the native
    ``(bit, epoch, arrival, frequency, clock_ppm, path_delay, path_doppler)``
    convention. The full source-time shift is applied before all three carrier
    samples are summed; this is not a carrier-label channel approximation.
    """
    def __init__(self, source, *, cache_bytes=32*1024**2, tile_samples=8192,
                 minimum_oversampling=8., maximum_dense_factor=4):
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
            self._insert((b, epoch, tile), stored)
        self.cache.move_to_end(key)
        return self.cache[key]

    def _gather(self, bit, epoch, origin, relative):
        result = np.zeros(len(relative), dtype=np.complex128)
        tile_origin, within_origin = divmod(origin, self.tile_samples)
        small = relative+within_origin
        tile_delta = small//self.tile_samples
        within = small % self.tile_samples
        for delta in np.unique(tile_delta):
            tile = tile_origin+int(delta)
            if tile < 0 or tile*self.tile_samples >= self.dense_source_samples:
                continue
            selected = tile_delta == delta
            result[selected] = self._tile(bit, epoch, tile)[within[selected]]
        return result

    def _direct(self, first, offsets, rows, center):
        self.direct_rows += len(rows)
        return self.source.basis_mapped(first, offsets, rows, center)

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
        fs = np.longdouble(self.sample_rate)
        answer = np.empty((len(parsed), len(offsets)), dtype=np.complex128)
        for column, (bit, epoch, arrival, frequency, clock, delay, doppler) in enumerate(parsed):
            scale = 1+clock*1e-6  # Same binary64 scale as the native candidate.
            shift = np.longdouble(arrival+delay)
            # Only the scalar origin needs long-double arithmetic. Relative
            # offsets stay small, enabling vectorized FP64 gathers and avoiding
            # absolute floating-point sample coordinates at large origins.
            dense_origin = ((np.longdouble(first)-shift*fs)*np.longdouble(scale))*self.dense_factor
            relative = offsets*(scale*self.dense_factor)
            if not np.isfinite(dense_origin) or not np.isfinite(relative).all():
                answer[column] = self._direct(first, offsets, [parsed[column]], audio_center_hz)[0]
                continue
            origin = int(np.floor(dense_origin))
            relative = relative+float(dense_origin-np.longdouble(origin))
            if (origin+math.ceil(float(relative[-1])) < -2 or
                    origin+math.floor(float(relative[0])) >= self.dense_source_samples+2):
                answer[column] = 0
                continue
            if np.max(np.abs(relative)) > 2**31:
                answer[column] = self._direct(first, offsets, [parsed[column]], audio_center_hz)[0]
                continue
            # Outside finite source support the native template is exactly0.
            # Compare using a scalar integer origin and relative coordinates.
            relevant = (relative >= -2-origin) & (relative < self.dense_source_samples+2-origin)
            if not np.any(relevant):
                answer[column] = 0
                continue
            selected = np.flatnonzero(relevant)
            grid = relative[selected]
            if np.max(grid) > np.iinfo(np.int64).max-self.tile_samples-4:
                answer[column] = self._direct(first, offsets, [parsed[column]], audio_center_hz)[0]
                continue
            integer = np.floor(grid).astype(np.int64)
            fraction = grid-integer
            w0 = -fraction*(fraction-1)*(fraction-2)/6
            w1 = (fraction+1)*(fraction-1)*(fraction-2)/2
            w2 = -(fraction+1)*fraction*(fraction-2)/2
            w3 = (fraction+1)*fraction*(fraction-1)/6
            values = (w0*self._gather(bit, epoch, origin, integer-1) +
                      w1*self._gather(bit, epoch, origin, integer) +
                      w2*self._gather(bit, epoch, origin, integer+1) +
                      w3*self._gather(bit, epoch, origin, integer+2))
            # Reduce the large constant origin before evaluating the small
            # varying phase. CFO/Doppler stay in physical time; IF includes
            # the complete source-time warp, not just its common CFO.
            initial_time = np.longdouble(first)/fs
            difference = ((np.longdouble(scale)-1)*np.longdouble(first)-np.longdouble(scale)*shift*fs)/fs
            if max(abs(np.longdouble(frequency)*initial_time),
                   abs(np.longdouble(doppler)*initial_time),
                   abs(np.longdouble(audio_center_hz)*difference)) > 2**40:
                # At enormous canceled origins the reference reduces each
                # full absolute phase separately; match it directly rather
                # than claim a more accurate split origin is bit-equivalent.
                self.large_phase_fallback_rows += 1
                answer[column] = self._direct(first, offsets, [parsed[column]], audio_center_hz)[0]
                continue
            constant = (np.remainder(np.longdouble(frequency)*initial_time, 1) +
                        np.remainder(np.longdouble(doppler)*initial_time, 1) +
                        np.remainder(np.longdouble(audio_center_hz)*difference, 1))
            slope = (frequency+doppler+audio_center_hz*(scale-1))/self.sample_rate
            phase = float(np.remainder(constant, 1))+slope*offsets[selected]
            if not np.isfinite(phase).all() or np.max(np.abs(phase)) > 2**30:
                answer[column] = self._direct(first, offsets, [parsed[column]], audio_center_hz)[0]
                continue
            values *= np.exp(2j*math.pi*np.remainder(phase, 1))
            answer[column] = 0
            answer[column, selected] = values
            self.interpolated_rows += 1
        if not np.isfinite(answer).all():
            raise FloatingPointError('mapped template cache produced nonfinite samples')
        return answer

    def report(self):
        return dict(method='uniform-DATA-cache-cubic-Lagrange-v1', calls=self.calls,
            interpolated_rows=self.interpolated_rows, direct_rows=self.direct_rows,
            cache_hits=self.cache_hits, cache_misses=self.cache_misses,
            native_preparation_calls=self.native_calls, native_preparation_samples=self.native_samples,
            evictions=self.evictions, cache_bytes=self.bytes, peak_cache_bytes=self.peak_bytes,
            cache_limit_bytes=self.byte_limit, tile_samples=self.tile_samples,
            dense_factor=self.dense_factor, oversampling_ratio=self.oversampling_ratio,
            large_phase_fallback_rows=self.large_phase_fallback_rows,
            interpolation_amplitude_proxy=self.interpolation_proxy, fallback_reason=self.fallback_reason,
            scope='metadata-selected interpolation; finite-pulse proxy is not a strict spectral/error bound; direct native fallback retained')

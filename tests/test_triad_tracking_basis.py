#!/usr/bin/env python3
"""Independent analytic and actual-native checks of reusable mapped DATA."""
import math
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest import mock

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_tracking_basis import CachedMappedBasis

NATIVE = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
sys.argv = sys.argv[:1]


class AnalyticSource:
    def __init__(self, *, fs=6000, width=225, samples=120000, pulse='rrc-025-span8-c2-tail2', fhss=False):
        self.descriptor = dict(sample_rate_hz=fs, source_samples=samples,
            nominal_triplet_bandwidth_hz=width)
        self.plan = dict(format=dict(pulse_shape=pulse, fhss=fhss, ramp_seconds=.0065))
        self.preparations = self.direct_calls = 0

    def waveform(self, sample, bit, epoch):
        # Independent smooth finite analytic source, including both codewords,
        # differing amplitude/phase and all three carrier relationships.
        fs = self.descriptor['sample_rate_hz']
        t = np.asarray(sample, dtype=np.longdouble)/fs
        end = self.descriptor['source_samples']/fs
        envelope = np.where((t >= 0) & (t < end), np.sin(np.pi*np.minimum(np.maximum(t/end, 0), 1))**2, 0)
        frequency = self.descriptor['nominal_triplet_bandwidth_hz']*.35
        coefficients = ((.4+.1j, -.3+.4j, .2-.5j), (-.1+.4j, .5+.2j, -.4-.1j))[bit]
        return np.asarray(envelope*sum(c*np.exp(2j*np.pi*np.remainder(f*t+.17*epoch, 1))
            for c, f in zip(coefficients, (-frequency, 0, frequency))), np.complex128)

    def basis(self, first, count, rows):
        self.preparations += 1
        samples = np.longdouble(first)+np.arange(count, dtype=np.longdouble)
        return np.stack([self.waveform(samples, int(row[0]), int(row[1])) for row in rows])

    def basis_mapped(self, first, offsets, rows, center):
        self.direct_calls += 1
        fs = self.descriptor['sample_rate_hz']
        index = np.longdouble(first)+np.asarray(offsets, dtype=np.longdouble)
        time = index/fs
        answer = []
        for bit, epoch, arrival, frequency, clock, delay, doppler in rows:
            rate = np.longdouble(1+clock*1e-6)
            shift = np.longdouble(arrival+delay)
            coordinate = (index-shift*fs)*rate
            difference = ((rate-1)*index-rate*shift*fs)/fs
            phase = np.remainder(np.longdouble(frequency)*time, 1)+np.remainder(np.longdouble(doppler)*time, 1)+np.remainder(np.longdouble(center)*difference, 1)
            answer.append(self.waveform(coordinate, bit, epoch)*np.exp(2j*np.pi*np.remainder(phase, 1)))
        return np.asarray(answer, np.complex128)


class CachedBasisTests(unittest.TestCase):
    def test_candidate_warps_and_entire_audio_phase_match_analytic_reference(self):
        source = AnalyticSource()
        cache = CachedMappedBasis(source)
        n = np.arange(1200)
        offsets = .123+n+1e-5*n*n
        rows = [(bit, epoch, .0173, .19, clock, delay, -.11)
            for bit in (0, 1) for epoch in (-1, 0, 2)
            for clock, delay in ((-400, .0037), (200, .0093))]
        expected = source.basis_mapped(51000, offsets, rows, 1725)
        actual = cache.basis_mapped(51000, offsets, rows, 1725)
        self.assertLess(np.linalg.norm(actual-expected)/np.linalg.norm(expected), 5e-5)
        self.assertLess(np.max(np.abs(actual-expected)), 5e-5)
        # The same cached waveforms are reused for timing, taps and both bits.
        preparations = cache.native_calls
        cache.basis_mapped(51000, offsets, rows, 1775)
        self.assertEqual(cache.native_calls, preparations)
        self.assertGreater(cache.cache_hits, 0)

    def test_dense_grid_and_explicit_reference_fallback(self):
        source = AnalyticSource(fs=24000, width=9000)
        cache = CachedMappedBasis(source)
        self.assertEqual(cache.dense_factor, 3)
        rows = [(0, 0, 0, 0, 137, 0, 0), (1, 1, .0001, .2, -133, .00003, -.1)]
        offsets = np.arange(500)+.371
        actual = cache.basis_mapped(50000, offsets, rows, 8270)
        expected = source.basis_mapped(50000, offsets, rows, 8270)
        self.assertLess(np.linalg.norm(actual-expected)/np.linalg.norm(expected), .0003)
        narrow = CachedMappedBasis(source, maximum_dense_factor=1)
        np.testing.assert_array_equal(narrow.basis_mapped(50000, offsets, rows, 8270), expected)
        self.assertEqual(narrow.report()['fallback_reason'], 'insufficient-cache-oversampling')
        self.assertEqual(narrow.bytes, 0)
        legacy = CachedMappedBasis(AnalyticSource(pulse='rrc-025-span8'))
        legacy.basis_mapped(0, offsets, rows, 1725)
        self.assertEqual(legacy.interpolated_rows, 0)
        self.assertEqual(legacy.native_calls, 0)

    def test_integer_origins_chunking_support_and_memory_bound(self):
        source = AnalyticSource()
        cache = CachedMappedBasis(source, tile_samples=128, cache_bytes=4096)
        first = 2**54+137
        rows = [(1, 0, float((np.longdouble(first)-51000)/6000), .13, -90, .0013, -.07)]
        offsets = np.arange(600)*1.0002+.125
        full = cache.basis_mapped(first, offsets, rows, 1725)
        expected = source.basis_mapped(first, offsets, rows, 1725)
        self.assertLess(np.linalg.norm(full-expected)/np.linalg.norm(expected), 7e-5)
        parts = np.concatenate([cache.basis_mapped(first+start, offsets[start:stop]-start, rows, 1725)
            for start, stop in ((0, 123), (123, 317), (317, 600))], axis=1)
        np.testing.assert_allclose(parts, full, rtol=0, atol=2e-8)
        self.assertLessEqual(cache.peak_bytes, cache.byte_limit)
        self.assertGreater(cache.large_phase_fallback_rows, 0)
        cache.basis_mapped(51000, offsets, [(1, 0, 0, 0, 0, 0, 0)], 1725)
        self.assertGreater(cache.evictions, 0)
        empty = cache.basis_mapped(0, np.arange(12)+.1,
            [(0, 0, 1e10, 0, 0, 0, 0)], 1725)
        np.testing.assert_array_equal(empty, 0)

    def test_split_sibling_eviction_does_not_double_count_replaced_arrays(self):
        cache = CachedMappedBasis(AnalyticSource(), tile_samples=4, cache_bytes=192)
        for bit, tile in ((0, 0), (0, 1), (0, 0), (1, 0)):
            cache._tile(bit, 0, tile)
            self.assertEqual(cache.bytes, sum(value.nbytes for value in cache.cache.values()))
            self.assertLessEqual(cache.bytes, cache.byte_limit)

    def test_single_tile_gather_preserves_order_duplicates_and_skips_unique_sort(self):
        source = AnalyticSource(samples=4096)
        cache = CachedMappedBasis(source, tile_samples=128, cache_bytes=4096)
        relative = np.array([3, 7, 7, 11, 64, 127], dtype=np.int64)
        expected = cache._tile(1, 0, 0)[relative]
        cache.cache.clear()
        cache.bytes = 0
        cache.cache_hits = cache.cache_misses = cache.native_calls = cache.evictions = 0
        with mock.patch('triad_tracking_basis.np.unique',
                                 side_effect=AssertionError('single-tile sort used')):
            actual = cache._gather(1, 0, 0, relative)
        np.testing.assert_array_equal(actual, expected)
        self.assertEqual(cache.cache_misses, 1)
        self.assertEqual(cache.evictions, 0)

    def test_tile_crossing_keeps_grouped_gather_and_boundary_order(self):
        cache = CachedMappedBasis(AnalyticSource(samples=4096), tile_samples=128,
                                  cache_bytes=4096)
        relative = np.array([0, .5, 1, 3], dtype=np.float64)
        first = 126
        origin, within_origin = divmod(first, cache.tile_samples)
        integer = np.floor(relative).astype(np.int64)
        small = integer+within_origin
        expected = np.zeros(len(relative), dtype=np.complex128)
        for delta in np.unique(small//cache.tile_samples):
            selected = (small//cache.tile_samples) == delta
            expected[selected] = cache._tile(0, 0, origin+int(delta))[
                small[selected] % cache.tile_samples]
        cache.cache.clear()
        cache.bytes = 0
        cache.cache_hits = cache.cache_misses = cache.native_calls = cache.evictions = 0
        actual = cache._gather(0, 0, first, integer)
        np.testing.assert_array_equal(actual, expected)
        self.assertEqual(cache.cache_misses, 2)

    def test_nonmonotonic_private_gather_retains_general_fallback(self):
        cache = CachedMappedBasis(AnalyticSource(samples=4096), tile_samples=128,
                                  cache_bytes=4096)
        relative = np.array([128, 1, 127, 0], dtype=np.int64)
        expected = np.array([cache._tile(0, 0, int(index)//128)[int(index)%128]
                             for index in relative])
        cache.cache.clear()
        cache.bytes = 0
        cache.cache_hits = cache.cache_misses = cache.native_calls = cache.evictions = 0
        actual = cache._gather(0, 0, 0, relative)
        np.testing.assert_array_equal(actual, expected)
        self.assertEqual(cache.cache_misses, 2)

    def test_unwarped_integer_samples_are_exact_and_rows_do_not_use_observations(self):
        source = AnalyticSource()
        cache = CachedMappedBasis(source)
        rows = [(0, 0, 0, 0, 0, 0, 0), (1, -2, 0, 0, 0, 0, 0)]
        expected = source.basis(50000, 1024, rows)
        np.testing.assert_array_equal(cache.basis_mapped(50000, np.arange(1024), rows, 0), expected)
        for bad in ([0, 0], [1, .5], [-.1, 1], [0, math.nan]):
            with self.assertRaises(ValueError): cache.basis_mapped(0, bad, rows, 1725)
        with self.assertRaises(ValueError):
            cache.basis_mapped(0, [0, 1], [(0, 0, 0, 0, -1e6, 0, 0)], 1725)

    @unittest.skipUnless(NATIVE, 'supply triad_helper_export for actual waveform qualification')
    def test_actual_native_shaping_fhss_burst_and_high_rate(self):
        from triad_continuous import NativeStream
        cases = [
            ('hf', 6000, 36, 10, False, 0, 1725),
            ('hf', 6000, 36, 10, True, 0, 1725),
            ('eme', 6000, 360, 1, True, 0, 1725),
            ('hf', 6000, 36, 10, False, 3, 1725),
            ('hf', 96000, 24000, 1, False, 0, 20000),
        ]
        for case, fs, rate, added, fhss, burst, center in cases:
            with self.subTest(fs=fs, rate=rate, fhss=fhss, burst=burst):
                args = ['--case', case, '--sample-rate', str(fs), '--seconds', '16',
                    '--rate', str(rate), '--added-symbols', str(added), '--key-seed', '95031',
                    '--epoch', '1793850000', '--helper', 'off', '--spread-preset', 'none',
                    '--selective-perturbation', 'off', '--oscillator-effects', 'off',
                    '--fhss', 'on' if fhss else 'off', '--burst-wait-tiles', str(burst),
                    '--data-if', str(center)]
                source = NativeStream(NATIVE, args, kind='emitted', log=subprocess.DEVNULL)
                try:
                    cache = CachedMappedBasis(source)
                    rows = [(bit, epoch, .00371, .23, clock, delay, -.17)
                        for bit in (0, 1) for epoch in (0, 1)
                        for clock, delay in ((-371, .00713), (287, .00329))]
                    # Sample active interior, gate boundaries and a tile/wait
                    # boundary. No generated channel or received scores select
                    # this fixed public validation set.
                    anchors = [int(fs*x) for x in (1.15, 1.23, 2.375, 11.65)]
                    error = peak = denominator = 0.
                    for first in anchors:
                        offsets = np.arange(1024)*1.00013+.347
                        expected = source.basis_mapped(first, offsets, rows, center)
                        actual = cache.basis_mapped(first, offsets, rows, center)
                        error += float(np.linalg.norm(actual-expected)**2)
                        denominator += float(np.linalg.norm(expected)**2)
                        peak = max(peak, float(np.max(np.abs(actual-expected))))
                    relative = math.sqrt(error/max(denominator, np.finfo(float).tiny))
                    print(f'native-cache rate={rate} added={added} fhss={fhss} burst={burst} '
                          f'dense={cache.dense_factor} relative_rms={relative:.9g} peak={peak:.9g} '
                          f'cache_bytes={cache.peak_bytes}', flush=True)
                    self.assertLess(relative, .001)
                    self.assertLess(peak, .004)
                    self.assertIsNone(cache.fallback_reason)
                finally:
                    source.close()


if __name__ == '__main__': unittest.main()

#!/usr/bin/env python3
"""Independent analytic and actual-native checks of reusable mapped DATA."""
from concurrent.futures import ThreadPoolExecutor
import math
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
import weakref

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_tracking_basis import CachedMappedBasis, mapped_basis_staging_bytes, mapped_kernel_path

NATIVE = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
sys.argv = sys.argv[:1]


def cached(source, **options):
    return CachedMappedBasis(source, kernel_path=mapped_kernel_path(NATIVE), **options)


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


@unittest.skipUnless(NATIVE, 'supply the built triad_helper_export')
class CachedBasisTests(unittest.TestCase):
    def test_pure_staging_bound_matches_live_renderer_reservation(self):
        for tile_samples in (4, 8192, 16384):
            cache = cached(AnalyticSource(), tile_samples=tile_samples)
            one = mapped_basis_staging_bytes(tile_samples, blocks=1)
            self.assertEqual(cache.staging_workspace_bytes, one)
            self.assertEqual(mapped_basis_staging_bytes(tile_samples), 2*one)
        for options in (dict(tile_samples=True), dict(tile_samples=3), dict(tile_samples=16385),
                        dict(blocks=True), dict(blocks=0), dict(blocks=3)):
            with self.assertRaises(ValueError):
                mapped_basis_staging_bytes(**options)

    def test_native_gaussian_batch_matches_seeded_fp64_filters(self):
        import torch
        from triad_gaussian_native import NativeGaussianBatch
        from triad_gaussian_state import GaussianPrior, GaussianStateFilter

        rng = np.random.default_rng(906)
        with ThreadPoolExecutor(max_workers=4) as executor:
            native = NativeGaussianBatch(mapped_kernel_path(NATIVE), executor, 4)
            for taps in (1, 2):
                for powers in ((.5,)*taps, (0.,)*taps):
                    priors = [GaussianPrior(powers, coherence, .037) for coherence in (.2, 5., math.inf)]
                    reference = [GaussianStateFilter(prior) for prior in priors]
                    batched = [GaussianStateFilter(prior) for prior in priors]
                    blocks = []
                    for index in range(3):
                        template = rng.normal(size=(3, 5, taps, 4)) + 1j*rng.normal(size=(3, 5, taps, 4))
                        if index == 2 and taps == 2:
                            template[..., 1, :] = 1j*template[..., 0, :]
                        template[0] = 0.
                        gram = template @ template.swapaxes(-1, -2).conj()
                        correlation = template.sum(axis=-1)
                        blocks.append((torch.from_numpy(correlation), torch.from_numpy(gram), .11*index))
                    native(batched, blocks[:1])
                    self.assertEqual(native(batched, blocks[1:]), 4)
                    for actual, expected in zip(batched, reference, strict=True):
                        for block in blocks:
                            expected.update(*block)
                        self.assertEqual(actual.blocks, 3)
                        self.assertEqual(torch.count_nonzero(actual.scores()[0]), 0)
                        for received, wanted in zip((actual.scores(), *actual.posterior()),
                                (expected.scores(), *expected.posterior()), strict=True):
                            torch.testing.assert_close(received, wanted, atol=1e-11, rtol=1e-11)

    def test_native_gaussian_collinear_guard_and_failed_batch_does_not_publish(self):
        import torch
        from triad_gaussian_native import NativeGaussianBatch
        from triad_gaussian_state import GaussianPrior, GaussianStateFilter

        with ThreadPoolExecutor(max_workers=2) as executor:
            native = NativeGaussianBatch(mapped_kernel_path(NATIVE), executor, 2)
            correlation = torch.zeros((3, 2, 2), dtype=torch.complex128)
            for difference in (0., 1e-10, 1e-8):
                runtime = GaussianStateFilter(GaussianPrior((.5, .5), math.inf))
                gram = torch.zeros((3, 2, 2, 2), dtype=torch.complex128)
                gram[1:] = 1e8*torch.tensor([[1., 1.-difference], [1.-difference, 1.]],
                    dtype=torch.complex128)
                native([runtime], [(correlation, gram, 0.)])
                expected = -math.log1p(.5*1e8*(2.-difference))-math.log1p(.5*1e8*difference)
                self.assertAlmostEqual(float(runtime.scores()[1, 0]), expected, delta=2e-8)
                self.assertGreaterEqual(float(torch.linalg.eigvalsh(runtime.covariance).min()), 0.)
                # Only the second candidate tile fails. Successful writes
                # in the first tile must stay private until all tiles join.
                before = (runtime.scores().clone(), *runtime.posterior())
                gram[2, 1, 0, 0] = -1e8
                with self.assertRaisesRegex(ValueError, 'positive semidefinite'):
                    native([runtime], [(correlation, gram, .1)])
                self.assertEqual(runtime.blocks, 1)
                for current, previous in zip((runtime.scores(), *runtime.posterior()), before, strict=True):
                    torch.testing.assert_close(current, previous, atol=0, rtol=0)

    def test_native_gaussian_interrupted_join_drains_and_preserves_unpublished_state(self):
        import torch
        from triad_gaussian_native import NativeGaussianBatch
        from triad_gaussian_state import GaussianPrior, GaussianStateFilter

        class Delayed:
            def __init__(self, operation, index):
                self.operation, self.index = operation, index
                self.waits = 0
                self.completed = False

            def result(self):
                self.waits += 1
                if self.index == 1 and self.waits == 1:
                    raise KeyboardInterrupt
                self.operation(self.index)
                self.completed = True

            def done(self):
                return self.completed

        class Pool:
            def __init__(self):
                self.jobs = []

            def submit(self, operation, index):
                self.jobs.append(Delayed(operation, index))
                return self.jobs[-1]

        pool = Pool()
        native = NativeGaussianBatch(mapped_kernel_path(NATIVE), pool, 2)
        r = torch.zeros((3, 2, 2), dtype=torch.complex128)
        g = torch.zeros((3, 2, 2, 2), dtype=torch.complex128)
        invalid = GaussianStateFilter(GaussianPrior((1.,)))
        with self.assertRaisesRegex(ValueError, 'extent'):
            native([invalid], [(r, g, 0.)])
        self.assertEqual(pool.jobs, [])
        runtime = GaussianStateFilter(GaussianPrior())
        with self.assertRaises(KeyboardInterrupt):
            native([runtime], [(r, g, 0.)])
        self.assertEqual([job.waits for job in pool.jobs], [1, 2])
        self.assertTrue(all(job.done() for job in pool.jobs))
        self.assertIsNone(runtime.mean)
        self.assertEqual(runtime.blocks, 0)

    def test_native_coarse_plan_preserves_finite_cubic_support(self):
        from triad_case_replay import _load_integer_cubic_plan
        from triad_dynamic_joint import _cubic_stencil, _sample_cubic

        apply = _load_integer_cubic_plan(mapped_kernel_path(NATIVE))
        source = np.array([1+2j, -.5-.75j, 0., -1+1j, .5+.25j])
        position = np.array([-25., -2., -1.5, -1., -.5, 0., .2, 1., 2.1, 4., 4.5, 5., 5.5, 24.])
        rotation = np.exp(2j*np.pi*np.remainder(position*.13, 1.))
        center, weights = _cubic_stencil(position)
        weights = np.asarray(weights)
        for values in (source, center, weights, rotation):
            values.flags.writeable = False
        expected = _sample_cubic(source, position)*rotation
        np.testing.assert_allclose(apply(source, center, weights, rotation), expected, rtol=2e-12, atol=2e-13)
        np.testing.assert_array_equal(apply(source[:0], center, weights, rotation), np.zeros_like(expected))
        limits = np.array([np.iinfo(np.int64).min, np.iinfo(np.int64).max], np.int64)
        np.testing.assert_array_equal(apply(source, limits, weights[:, :2].copy(), rotation[:2]), [0., 0.])
        with self.assertRaisesRegex(RuntimeError, 'stencil rejected'):
            apply(source, center[:0], weights[:, :0].copy(), rotation[:0])

    def test_candidate_warps_and_entire_audio_phase_match_analytic_reference(self):
        source = AnalyticSource()
        cache = cached(source)
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
        cache = cached(source)
        self.assertEqual(cache.dense_factor, 3)
        rows = [(0, 0, 0, 0, 137, 0, 0), (1, 1, .0001, .2, -133, .00003, -.1)]
        offsets = np.arange(500)+.371
        actual = cache.basis_mapped(50000, offsets, rows, 8270)
        expected = source.basis_mapped(50000, offsets, rows, 8270)
        self.assertLess(np.linalg.norm(actual-expected)/np.linalg.norm(expected), .0003)
        narrow = cached(source, maximum_dense_factor=1)
        destination = np.empty_like(expected)
        self.assertIs(narrow.basis_mapped(50000, offsets, rows, 8270, out=destination), destination)
        np.testing.assert_array_equal(destination, expected)
        self.assertEqual(narrow.report()['fallback_reason'], 'insufficient-cache-oversampling')
        self.assertEqual(narrow.bytes, 0)
        legacy = cached(AnalyticSource(pulse='rrc-025-span8'))
        legacy.basis_mapped(0, offsets, rows, 1725)
        self.assertEqual(legacy.interpolated_rows, 0)
        self.assertEqual(legacy.native_calls, 0)

    def test_integer_origins_chunking_support_and_memory_bound(self):
        source = AnalyticSource()
        cache = cached(source, tile_samples=128, cache_bytes=4096)
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
        cache = cached(AnalyticSource(), tile_samples=4, cache_bytes=192)
        for bit, tile in ((0, 0), (0, 1), (0, 0), (1, 0)):
            cache._tile(bit, 0, tile)
            self.assertEqual(cache.bytes, sum(value.nbytes for value in cache.cache.values()))
            self.assertLessEqual(cache.bytes, cache.byte_limit)

    def test_workers_match_across_evictions_tile_edges_and_support(self):
        source = AnalyticSource(samples=4103)
        rows = [(bit, epoch, arrival, .2, -120, .002, -.17)
                for epoch in range(-2, 3) for bit in (0, 1) for arrival in (-.6, .002, .016)]
        rows.insert(2, rows[1])
        offsets = .15+np.arange(1024)*.5
        serial = cached(source, tile_samples=128, cache_bytes=4096)
        first = serial.basis_mapped(120, offsets, rows, 1725)
        expected = source.basis_mapped(120, offsets, rows, 1725)
        np.testing.assert_allclose(first, expected, atol=5e-5, rtol=5e-5)
        sentinel = -22+7j
        guarded = np.full((len(rows)+2, len(offsets)), sentinel)
        destination = guarded[1:-1]
        with ThreadPoolExecutor(max_workers=4) as executor:
            parallel = cached(source, tile_samples=128, cache_bytes=4096, executor=executor, workers=4)
            second = parallel.basis_mapped(120, offsets, rows, 1725, out=destination)
        self.assertIs(second, destination)
        np.testing.assert_array_equal(destination, first)
        np.testing.assert_array_equal(guarded[[0, -1]], sentinel)
        self.assertGreater(parallel.evictions, 0)
        self.assertEqual(parallel.maximum_mapped_workers, 4)
        for key in ('cache_hits', 'cache_misses', 'native_preparation_calls', 'native_preparation_samples',
                    'evictions', 'direct_rows', 'interpolated_rows'):
            self.assertEqual(parallel.report()[key], serial.report()[key])

    def test_mapped_destination_rejects_strided_and_aliased_storage(self):
        cache = cached(AnalyticSource())
        rows = [(0, 0, 0, 0, 0, 0, 0)]
        offsets = np.arange(8, dtype=np.float64)
        with self.assertRaisesRegex(ValueError, 'separate writable aligned contiguous'):
            cache.basis_mapped(10, offsets, rows, 0, out=np.empty((1, 16), np.complex128)[:, ::2])
        destination = np.empty((1, 8), np.complex128)
        overlapping = destination.view(np.float64).ravel()[:8]
        overlapping[:] = offsets
        with self.assertRaisesRegex(ValueError, 'separate writable aligned contiguous'):
            cache.basis_mapped(10, overlapping, rows, 0, out=destination)

    def test_fused_data_statistics_match_full_projection_and_common_null(self):
        from triad_dynamic_joint import BlockProjection

        offsets = .15+np.arange(241, dtype=np.float64)**1.01
        variance = 2.3
        rng = np.random.default_rng(483)
        raw = rng.normal(size=len(offsets))+1j*rng.normal(size=len(offsets))
        for taps, band, first, center in (
                (1, (-1., 1.), 50320, 1725.),
                (2, (-500., 500.), 50320, 1725.),
                (2, (-500., 500.), 2**54+137, 0.)):
            with self.subTest(taps=taps, first=first):
                candidates = 71
                shift = 0. if first < 2**48 else float((np.longdouble(first)-51000)/6000)
                rows = [(bit, candidate//24, shift+.0002*(candidate%4), .24, -32., tap*.002, -.17)
                        for bit in (0, 1) for candidate in range(candidates) for tap in range(taps)]
                projection = BlockProjection.make(len(offsets), 6000, (band,))
                observation = projection.numpy(raw)
                reference = cached(AnalyticSource())
                basis = np.concatenate([reference.basis_mapped(first, offsets, rows[i:i+128], center)
                                        for i in range(0, len(rows), 128)])
                data = projection.numpy(basis).reshape(2, candidates, taps, -1)
                with ThreadPoolExecutor(max_workers=4) as executor:
                    fused = cached(AnalyticSource(), executor=executor, workers=4)
                    (correlations, gram, energy), = fused.block_statistics(
                        [(first, offsets, projection, observation, variance)], rows, center, candidates, taps)
                np.testing.assert_array_equal(correlations[0], 0)
                np.testing.assert_array_equal(gram[0], 0)
                np.testing.assert_allclose(correlations[1:],
                    np.einsum('bctn,n->bct', data.conj(), observation)/variance, rtol=2e-14, atol=2e-14)
                np.testing.assert_allclose(gram[1:],
                    (data.conj() @ data.swapaxes(-1, -2))/variance, rtol=2e-14, atol=2e-14)
                np.testing.assert_allclose(energy, float(np.vdot(basis, basis).real), rtol=2e-14, atol=0.)
                for key in ('interpolated_rows', 'row_tile_references',
                            'native_preparation_calls', 'native_preparation_samples', 'evictions'):
                    self.assertEqual(reference.report()[key], fused.report()[key])
                self.assertEqual(fused.row_geometry_builds, 1)
                self.assertEqual(fused.row_geometry_rows, len(rows))
                if reference.interpolated_rows:
                    self.assertLess(fused.cache_hits, reference.cache_hits)

    def test_adjacent_blocks_share_full_bank_geometry_and_preserve_pin_fallback(self):
        from triad_dynamic_joint import BlockProjection

        candidates, taps = 256, 2
        rows = [(bit, candidate//100, .0002*(candidate%4), .24, -32., tap*.002, -.17)
                for bit in (0, 1) for candidate in range(candidates) for tap in range(taps)]
        offsets = .15+np.arange(47, dtype=np.float64)**1.01
        projection = BlockProjection.make(len(offsets), 6000, ((-500., 500.),))
        observation = projection.numpy(np.exp(.03j*offsets))
        requests = [(first, offsets, projection, observation, 2.3) for first in (50320, 50561)]
        reference = cached(AnalyticSource())
        expected = [np.concatenate([reference.basis_mapped(first, offsets, rows[at:at+128], 1725.)
                    for at in range(0, len(rows), 128)]) for first, *_ in requests]
        for cache_bytes in (32*1024**2, 2*8192*16):
            with self.subTest(cache_bytes=cache_bytes), ThreadPoolExecutor(max_workers=4) as executor:
                fused = cached(AnalyticSource(), cache_bytes=cache_bytes, executor=executor, workers=4)
                with patch.object(fused, '_stage_mapped', wraps=fused._stage_mapped) as stage:
                    results = fused.block_statistics(requests, rows, 1725., candidates, taps)
                self.assertEqual(fused.row_geometry_builds, 1)
                self.assertEqual(fused.row_geometry_rows, 1024)
                attempted_rows = [len(call.args[1]) for call in stage.call_args_list]
                self.assertEqual(attempted_rows.count(1024), 2)
                if cache_bytes > 2*8192*16:
                    self.assertEqual(attempted_rows, [1024, 1024])
                else:
                    self.assertEqual(attempted_rows.count(128), 16)
                self.assertLessEqual(fused.peak_bytes, cache_bytes)
                for (correlation, gram, energy), basis in zip(results, expected, strict=True):
                    data = projection.numpy(basis).reshape(2, candidates, taps, -1)
                    np.testing.assert_array_equal(correlation[0], 0)
                    np.testing.assert_array_equal(gram[0], 0)
                    np.testing.assert_allclose(correlation[1:],
                        np.einsum('bctn,n->bct', data.conj(), observation)/2.3, rtol=2e-14, atol=2e-14)
                    np.testing.assert_allclose(gram[1:],
                        (data.conj() @ data.swapaxes(-1, -2))/2.3, rtol=2e-14, atol=2e-14)
                    self.assertAlmostEqual(energy/float(np.vdot(basis, basis).real), 1., places=13)
        with self.assertRaises(ValueError):
            reference.basis_mapped(50320, offsets, rows[:129], 1725.)

    def test_large_bank_direct_fallback_retains_128_row_native_limit(self):
        from triad_dynamic_joint import BlockProjection

        first, candidates = 2**54+137, 65
        arrival = float((np.longdouble(first)-51000)/6000)
        rows = [(bit, 0, arrival, .24, -32., 0., -.17)
                for bit in (0, 1) for _ in range(candidates)]
        offsets = .15+np.arange(31, dtype=np.float64)
        projection = BlockProjection.make(len(offsets), 6000, ((-500., 500.),))
        observation = projection.numpy(np.exp(.03j*offsets))
        source = AnalyticSource()
        fused = cached(source)
        native = source.basis_mapped
        calls = []
        def bounded_direct(*arguments):
            calls.append(len(arguments[2]))
            self.assertLessEqual(calls[-1], 128)
            return native(*arguments)
        source.basis_mapped = bounded_direct
        real_finfo = np.finfo
        class NarrowLongDouble:
            nmant = 52
        with patch('triad_tracking_basis.np.finfo', side_effect=lambda dtype:
                   NarrowLongDouble() if dtype is np.longdouble else real_finfo(dtype)):
            (correlation, gram, energy), = fused.block_statistics(
                [(first, offsets, projection, observation, 2.3)], rows, 0., candidates, 1)
        self.assertEqual(calls, [128, 2])
        basis = np.concatenate([native(first, offsets, rows[at:at+128], 0.)
                                for at in range(0, len(rows), 128)])
        data = projection.numpy(basis).reshape(2, candidates, 1, -1)
        np.testing.assert_array_equal(correlation[0], 0)
        np.testing.assert_array_equal(gram[0], 0)
        np.testing.assert_allclose(correlation[1:],
            np.einsum('bctn,n->bct', data.conj(), observation)/2.3, rtol=2e-14, atol=2e-14)
        np.testing.assert_allclose(gram[1:], (data.conj() @ data.swapaxes(-1, -2))/2.3,
                                   rtol=2e-14, atol=2e-14)
        self.assertAlmostEqual(energy/float(np.vdot(basis, basis).real), 1., places=13)

    def test_fused_renderer_joins_started_writes_after_submission_failure(self):
        from triad_dynamic_joint import BlockProjection

        jobs = []
        with ThreadPoolExecutor(max_workers=1) as backing:
            class PartialExecutor:
                def submit(self, *args, **kwargs):
                    if jobs:
                        raise RuntimeError('injected submission failure')
                    jobs.append(backing.submit(*args, **kwargs))
                    return jobs[0]
            cache = cached(AnalyticSource(), executor=PartialExecutor(), workers=2)
            offsets = np.arange(240, dtype=np.float64)
            projection = BlockProjection.make(len(offsets), 6000, ((-100., 100.),))
            observation = projection.numpy(np.ones(len(offsets), dtype=np.complex128))
            rows = [(bit, 0, .0003*candidate, .2, 3., 0., 0.)
                    for bit in (0, 1) for candidate in range(8)]
            with self.assertRaisesRegex(RuntimeError, 'injected submission failure'):
                cache.block_statistics([(51000, offsets, projection, observation, 1.)],
                                       rows, 1725., 8, 1)
            self.assertTrue(jobs[0].done())
            self.assertIsNone(jobs[0].exception())

    def test_staged_nonconsecutive_spans_share_tables_without_repeating_tile_calls(self):
        from triad_dynamic_joint import BlockProjection
        from triad_tracking_basis import _MappedBlock

        epochs = (0, 1, 0, 2, 1, 0, 2, 1, 0)
        rows = [(bit, epoch, 0., .2, 3., 0., 0.) for bit in (0, 1) for epoch in epochs]
        offsets = np.arange(48, dtype=np.float64)
        projection = BlockProjection.make(len(offsets), 6000, ((-100., 100.),))
        observation = projection.numpy(np.exp(.03j*offsets))
        reference_cache = cached(AnalyticSource())
        reference = reference_cache.basis_mapped(51000, offsets, rows, 1725.)
        cache = cached(AnalyticSource())
        append = _MappedBlock.append
        captured = []
        def capture(block, first, descriptors, coordinates, tables, pins):
            captured.append((descriptors['tiles'].copy(), len(tables)))
            return append(block, first, descriptors, coordinates, tables, pins)
        with patch.object(_MappedBlock, 'append', capture), patch.object(cache, '_tile', wraps=cache._tile) as tile:
            (correlation, gram, energy), = cache.block_statistics(
                [(51000, offsets, projection, observation, 2.3)], rows, 1725., len(epochs), 1)
        self.assertEqual(tile.call_count, 6)
        self.assertEqual([call.args for call in tile.call_args_list],
                         [(bit, epoch, 51000//8192) for bit in (0, 1) for epoch in (0, 1, 2)])
        for field in ('cache_hits', 'cache_misses', 'native_calls', 'evictions'):
            self.assertEqual(getattr(cache, field), getattr(reference_cache, field))
        self.assertEqual(list(cache.cache), list(reference_cache.cache))
        self.assertEqual(len(captured), 1)
        pointers, table_count = captured[0]
        self.assertEqual(table_count, 6)
        for bit in (0, 1):
            for epoch in (0, 1, 2):
                selected = pointers[bit*len(epochs): (bit+1)*len(epochs)][np.asarray(epochs) == epoch]
                np.testing.assert_array_equal(selected, selected[0])
        self.assertEqual(len(set(pointers)), 6)
        data = projection.numpy(reference).reshape(2, len(epochs), 1, -1)
        np.testing.assert_array_equal(correlation[0], 0)
        np.testing.assert_array_equal(gram[0], 0)
        np.testing.assert_allclose(correlation[1:],
            np.einsum('bctn,n->bct', data.conj(), observation)/2.3, rtol=2e-14, atol=2e-14)
        np.testing.assert_allclose(gram[1:], (data.conj() @ data.swapaxes(-1, -2))/2.3,
                                   rtol=2e-14, atol=2e-14)
        self.assertAlmostEqual(energy/float(np.vdot(reference, reference).real), 1., places=13)

    def test_fused_renderer_pins_first_array_through_sibling_replacement(self):
        from triad_dynamic_joint import BlockProjection

        first, samples, tile_samples = 51000, 240, 8192
        cache = cached(AnalyticSource(), cache_bytes=4*tile_samples*16, tile_samples=tile_samples)
        cache.paired_kernel = None
        tile = first//tile_samples
        for epoch in (0, 1, 0, 2):
            cache._tile(0, epoch, tile)
        self.assertIn((0, 0, tile), cache.cache)
        self.assertNotIn((1, 0, tile), cache.cache)
        earlier = weakref.ref(cache.cache[0, 0, tile])
        kernel = cache.kernel
        def checked_kernel(*arguments):
            self.assertIsNotNone(earlier())
            self.assertIsNot(earlier(), cache.cache[0, 0, tile])
            return kernel(*arguments)
        cache.kernel = checked_kernel
        offsets = np.arange(samples, dtype=np.float64)
        projection = BlockProjection.make(samples, 6000, ((-100., 100.),))
        observation = projection.numpy(np.ones(samples, dtype=np.complex128))
        rows = [(bit, 0, 0., .2, 3., 0., 0.) for bit in (0, 1, 0, 1)]
        self.assertIsNotNone(cache.block_statistics(
            [(first, offsets, projection, observation, 1.)], rows, 1725., 2, 1))

    def test_unwarped_integer_samples_are_exact_and_rows_do_not_use_observations(self):
        source = AnalyticSource()
        cache = cached(source)
        rows = [(0, 0, 0, 0, 0, 0, 0), (1, -2, 0, 0, 0, 0, 0)]
        expected = source.basis(50000, 1024, rows)
        np.testing.assert_array_equal(cache.basis_mapped(50000, np.arange(1024), rows, 0), expected)
        for bad in ([0, 0], [1, .5], [-.1, 1], [0, math.nan]):
            with self.assertRaises(ValueError): cache.basis_mapped(0, bad, rows, 1725)
        with self.assertRaises(ValueError):
            cache.basis_mapped(0, [0, 1], [(0, 0, 0, 0, -1e6, 0, 0)], 1725)

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

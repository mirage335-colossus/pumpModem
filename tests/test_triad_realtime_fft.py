#!/usr/bin/env python3
"""Cheap numerical contract tests for the coarse FFT proposal stage."""
from __future__ import annotations

import copy
from concurrent.futures import ThreadPoolExecutor
import hashlib
import math
from pathlib import Path
import sys
import threading
import unittest
from unittest import mock

import numpy as np
from scipy import fft, signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_realtime_fft import (FFTScreen, _quadratic_scores, _fir_decimate_chunks,
    _collapsed_cubic_fir, _project_coarse_column, _mapped_finite_source,
    compare_template_projectors, _candidate_peak_indices)
from triad_dynamic_joint import _sample_cubic


def fixture():
    fs = 400
    t = np.arange(200)/fs
    envelope = np.sin(np.pi*np.arange(200)/199)**2
    source = np.array([[envelope*np.exp(2j*np.pi*(4*t+8*t*t)),
                        envelope*np.exp(2j*np.pi*(-7*t-9*t*t))],
                       [envelope*np.exp(2j*np.pi*(10*t+3*t*t)),
                        envelope*np.exp(2j*np.pi*(-12*t+8*t*t))]], dtype=np.complex128)
    manifest = dict(sample_rate_hz=fs,
        calendar=dict(capture_samples=2000, capture_lead_seconds=1.,
                      data_sample_offset=400, arrival_prior_seconds=[-.1, .1]),
        channel=dict(spread_delay_ms=17.5, spread_doppler_hz=13.),
        format=dict(occupied_bandwidth_hz=40., fine_chip_rate_hz=30.),
        power=dict(complex_noise_variance=.1))
    return manifest, source, (-1, 1)


def screen(m=None, source=None, epochs=None, **kwargs):
    if m is None:
        m, source, epochs = fixture()
    return FFTScreen(m, source, epochs, clock_ppm=kwargs.pop('clock_ppm', (0.,)),
        output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90.,
        workers=kwargs.pop('workers', 1), **kwargs)


def waveform(m, source, epoch_index, bit, arrival, frequency, clock):
    fs = m['sample_rate_hz']
    t = np.arange(m['calendar']['capture_samples'])/fs-m['calendar']['capture_lead_seconds']
    scale = 1+clock*1e-6
    offset = m['calendar']['data_sample_offset']/fs
    y = np.zeros(len(t), complex)
    for gain, delay, doppler in ((.8+.2j, 0., 0.),
                                (.3-.6j, m['channel']['spread_delay_ms']*.001,
                                 m['channel']['spread_doppler_hz'])):
        y += gain*_sample_cubic(source[epoch_index, bit], ((t-arrival-delay)*scale-offset)*fs)*np.exp(2j*np.pi*doppler*t)
    return y*np.exp(2j*np.pi*frequency*t)


class FFTScreenTests(unittest.TestCase):
    def test_integer_source_lease_keeps_identical_columns_and_native_owner(self):
        m, source, epochs = fixture()
        owner = threading.get_ident()
        lanes = tuple((epoch, bit) for epoch in epochs for bit in (0, 1))
        def read(epoch, bit, first, count):
            self.assertEqual(threading.get_ident(), owner)
            return source[epochs.index(epoch), bit, first:first+count]
        class Provider:
            def __init__(self):
                self.leases = []
                self.direct_calls = 0

            def __call__(self, *args):
                self.direct_calls += 1
                return read(*args)

            def lease(self, selected, count, _chunk):
                self.leases.append(tuple(selected))
                values = np.stack([read(epoch, bit, 0, count) for epoch, bit in selected])
                values.flags.writeable = False
                return values

        options = dict(clock_ppm=(-10000., 0., 10000.), precision='fp64',
            source_samples=source.shape[-1], template_identity='0'*64,
            template_chunk_samples=31)
        baseline = screen(m, None, epochs, template_provider=read, **options)
        provider = Provider()
        actual = screen(m, None, epochs, template_provider=provider, workers=4,
                        mapped_template_batch_lanes=4, **options)
        self.assertEqual(provider.leases, [lanes])
        self.assertEqual(provider.direct_calls, 0)
        self.assertEqual(actual.policy['preparation_projection_workers'], 4)

        def load_plan():
            def apply(source_row, center, weights, rotation):
                for values in (source_row, center, weights, rotation):
                    self.assertFalse(values.flags.writeable)
                    self.assertTrue(values.flags.c_contiguous)
                result = np.zeros(len(center), np.complex128)
                for shift, weight in zip((-1, 0, 1, 2), weights, strict=True):
                    indices = center+shift
                    valid = (indices >= 0) & (indices < len(source_row))
                    result[valid] += weight[valid]*source_row[indices[valid]]
                return result*rotation
            return apply

        planned_provider = Provider()
        planned_provider.cubic_plan = mock.Mock(side_effect=load_plan)
        planned = screen(m, None, epochs, template_provider=planned_provider, workers=4,
                         mapped_template_batch_lanes=4, **options)
        self.assertEqual(planned.policy['integer_template_projection'], 'native_cubic_plan')
        planned_provider.cubic_plan.assert_called_once_with()
        planned_provider.cubic_plan.reset_mock()
        plan_rejected = screen(m, None, epochs, template_provider=planned_provider, workers=4,
                               mapped_template_batch_lanes=4,
                               memory_limit_bytes=actual.preparation_peak_estimate_bytes, **options)
        self.assertEqual(plan_rejected.policy['integer_template_projection'], 'numpy_cubic')
        planned_provider.cubic_plan.assert_not_called()
        fallback = Provider()
        bounded = screen(m, None, epochs, template_provider=fallback, workers=4,
            memory_limit_bytes=baseline.preparation_peak_estimate_bytes, **options)
        self.assertEqual(fallback.leases, [])
        self.assertGreater(fallback.direct_calls, 0)
        for candidate in (actual, bounded, planned, plan_rejected):
            self.assertEqual(candidate.projection_receipt(), baseline.projection_receipt())
            for left, right in zip(baseline.workspaces, candidate.workspaces, strict=True):
                for key in ('columns', 'gram', 'transform', 'inverse'):
                    np.testing.assert_array_equal(right[key], left[key])
                for key in ('rank', 'epoch', 'bit', 'clock_ppm', 'quadratic_explicit'):
                    self.assertEqual(right[key], left[key])
        raw = waveform(m, source, 1, 1, .03, 0., 0.)
        self.assertEqual(actual.score(raw, [0.])['candidates'], baseline.score(raw, [0.])['candidates'])
        rejected = Provider()
        with self.assertRaises(MemoryError):
            screen(m, None, epochs, template_provider=rejected, workers=4,
                   memory_limit_bytes=baseline.preparation_peak_estimate_bytes-1, **options)
        self.assertEqual(rejected.leases, [])
        self.assertEqual(rejected.direct_calls, 0)

    def test_integer_source_lease_validation_precedes_projection(self):
        m, source, epochs = fixture()
        def provider(*_args):
            self.fail('an admitted lease must not use the native slice callback')
        def create():
            return screen(m, None, epochs, template_provider=provider, workers=2,
                          source_samples=source.shape[-1], template_identity='0'*64)
        with mock.patch.object(FFTScreen, '_project_integer_column') as project:
            for mode in ('shape', 'writable', 'nonfinite', 'unaligned'):
                def lease(lanes, count, _chunk):
                    shape = (len(lanes), count-(mode=='shape'))
                    if mode == 'unaligned':
                        value = np.ndarray(shape, np.complex128,
                            buffer=bytearray(16*len(lanes)*count+1), offset=1)
                    else:
                        value = np.zeros(shape, np.complex128)
                    if mode == 'nonfinite':
                        value.flat[0] = np.nan
                    value.flags.writeable = mode == 'writable'
                    return value
                provider.lease = lease
                with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, 'integer source lease'):
                    create()
            project.assert_not_called()

    def test_integer_source_projection_failure_drains_disjoint_jobs(self):
        m, source, epochs = fixture()
        def provider(*_args):
            self.fail('the worker must not call the native provider')
        def lease(lanes, count, _chunk):
            values = np.ones((len(lanes), count), np.complex128)
            values.flags.writeable = False
            return values
        provider.lease = lease
        ready = threading.Barrier(2)
        drained = threading.Event()
        lock = threading.Lock()
        calls = 0
        def projection(instance, chunks):
            nonlocal calls
            with lock:
                index = calls
                calls += 1
            if index < 2:
                ready.wait(timeout=3)
            if index == 0:
                raise ValueError('controlled projection failure')
            if index == 1:
                next(chunks)
                drained.set()
            return np.zeros(instance.kernel_rows, np.complex128)
        with mock.patch.object(FFTScreen, '_project_integer_column', projection), \
                self.assertRaisesRegex(ValueError, 'controlled projection failure'):
            screen(m, None, epochs, template_provider=provider, workers=2,
                   source_samples=source.shape[-1], template_identity='0'*64)
        self.assertTrue(drained.is_set())

    def test_exact_frequency_tiles_match_complete_grid_and_defer_proposals(self):
        m, source, epochs = fixture()
        raw = waveform(m, source, 1, 1, .035, .37, 0.)
        frequencies = np.linspace(-2., 2., 17)
        reference = screen(m, source, epochs, precision='fp64',
            maximum_frequency_points=64, maximum_fft_products=1024)
        projected = reference.prepare_observation(raw)
        expected = reference.score_projected(projected, frequencies, top_per_bit=4,
            projection_receipt=reference.projection_receipt())
        bounded = screen(m, source, epochs, precision='fp64',
            maximum_frequency_points=4, maximum_fft_products=32)
        ordinary = bounded.score_projected(projected, frequencies,
            projection_receipt=bounded.projection_receipt())
        self.assertEqual(ordinary['reason'], 'frequency_work_limit')
        with ThreadPoolExecutor(max_workers=2) as executor:
            session = bounded.start_tiled_frequency(projected, frequencies,
                projection_receipt=bounded.projection_receipt(), executor=executor,
                workspace_workers=2, top_per_bit=4)
            try:
                plan = session.work_plan
                self.assertEqual(plan['units'], 5)
                self.assertEqual(plan['terminal_unit_frequency_points'], 1)
                self.assertEqual(plan['full_fft_products'], 17*4*2)
                self.assertLessEqual(plan['maximum_kernel_fft_products'], 32)
                first = session.step(1)
                self.assertEqual(first['completed_frequency_points'], 4)
                self.assertFalse(first['proposals_available'])
                partial = session.finish()
                self.assertEqual(partial['status'], 'unresolved')
                self.assertEqual(partial['reason'], 'incomplete_frequency_tiles')
                self.assertEqual(partial['candidates'], [])
                probe = session.selection_probe()
                self.assertEqual(probe['status'], 'diagnostic_only')
                self.assertFalse(probe['proposals_available'])
                self.assertFalse(probe['included_in_receiver_service'])
                session.step(2)
                session.step(4)  # two remaining units, including the singleton
                actual = session.finish()
                self.assertTrue(session.complete)
                self.assertEqual(actual['status'], 'proposed')
                self.assertEqual(actual['frequencies_hz'], expected['frequencies_hz'])
                self.assertEqual(actual['work']['fft_products'], expected['work']['fft_products'])
                self.assertIs(session.finish(), actual)
                for left, right in zip(actual['candidates'], expected['candidates']):
                    self.assertEqual({k:v for k,v in left.items() if k != 'score'},
                                     {k:v for k,v in right.items() if k != 'score'})
                    self.assertAlmostEqual(left['score'], right['score'], places=11)
            finally:
                session.close()

    def test_frequency_tiles_keep_suppressed_boundary_rows_until_global_nms(self):
        manifest, source, epochs = fixture()
        s = screen(manifest, source[:1], epochs[:1], precision='fp64',
                   maximum_frequency_points=4, maximum_fft_products=32)
        frequencies = np.array([-1.35, -.9, -.45, 0.])
        values = dict(zip(frequencies, [99., 98., 100., 101.]))
        def rows(workspace, spectra, supplied, **arguments):
            return [dict(bit=workspace['bit'], epoch=workspace['epoch'], arrival=0.,
                frequency=float(frequency), clock_ppm=workspace['clock_ppm'],
                score=values[frequency], rank=workspace['rank']) for frequency in supplied]
        projected = np.zeros(s.crop_rows, complex)
        # In the first tile, -.45 suppresses -1.35. Once the final0Hz peak
        # suppresses -.45, -1.35 must reappear as the second retained peak.
        with mock.patch.object(s, '_frequency_peak_rows', side_effect=rows):
            expected = s.score_projected(projected, frequencies, top_per_bit=2)
            session = s.start_tiled_frequency(projected, frequencies,
                frequency_unit_points=3, top_per_bit=2)
            try:
                session.step()
                self.assertEqual(session.finish()['candidates'], [])
                self.assertEqual(session.selection_probe()['raw_candidate_rows'], 6)
                session.step()
                actual = session.finish()
            finally:
                session.close()
        self.assertEqual(actual['candidates'], expected['candidates'])
        self.assertEqual([row['frequency'] for row in actual['candidates_by_bit'][0]],
                         [0., -1.35])

    def test_frequency_tiles_preserve_ties_interval_union_and_input_ownership(self):
        s = screen(precision='fp64', maximum_frequency_points=16,
                   maximum_fft_products=128)
        projected = np.zeros(s.crop_rows, complex)
        intervals = [[-.4, 0.], [0., .4], [1., 1.]]
        expected = s.score_projected(projected, frequency_intervals=intervals,
            top_per_bit=4)
        session = s.start_tiled_frequency(projected, frequency_intervals=intervals,
            frequency_unit_points=1, top_per_bit=4)
        projected[:] = 100.+100.j  # the ongoing search owns its input copy
        try:
            while not session.complete:
                session.step(2)
            actual = session.finish()
            self.assertEqual(actual['frequencies_hz'], [-.4, 0., .4, 1.])
            self.assertEqual(actual['candidates'], expected['candidates'])
        finally:
            session.close()

    def test_frequency_tiles_preflight_global_memory_and_kernel_limits(self):
        s = screen(precision='fp64', maximum_frequency_points=4,
                   maximum_fft_products=32)
        frequencies = np.linspace(-2., 2., 17)
        plan = s.tiled_frequency_plan(frequencies, top_per_bit=4, workspace_workers=2)
        self.assertEqual(plan['status'], 'ready')
        self.assertEqual(plan['raw_candidate_storage_estimate_bytes'],
                         plan['raw_candidate_row_capacity']*512)
        self.assertGreater(plan['raw_candidate_row_capacity'], 4*4*4)
        self.assertGreater(s.preparation_peak_estimate_bytes, s.resident_estimate)
        s.memory_limit_bytes = plan['estimated_peak_bytes']-1
        with mock.patch.object(s, '_frequency_spectra') as transform:
            rejected = s.tiled_frequency_plan(frequencies, workspace_workers=2)
            self.assertEqual(rejected['reason'], 'complete_tiled_frequency_memory_limit')
            with self.assertRaisesRegex(ValueError, 'complete_tiled_frequency_memory_limit'):
                s.start_tiled_frequency(np.zeros(s.crop_rows, complex), frequencies,
                    executor=mock.Mock(), workspace_workers=2)
        transform.assert_not_called()
        s.memory_limit_bytes = 1024**3
        self.assertEqual(s.tiled_frequency_plan(frequencies, frequency_unit_points=5)['reason'],
                         'frequency_tile_kernel_work_limit')
        with self.assertRaises(ValueError):
            s.tiled_frequency_plan(frequencies, maximum_total_frequency_points=65537)
        with self.assertRaises(ValueError):
            s.start_tiled_frequency(np.zeros(s.crop_rows, complex), frequencies,
                workspace_workers=2)
        session = s.start_tiled_frequency(np.zeros(s.crop_rows, complex), frequencies)
        session.close()
        self.assertEqual(session.finish()['status'], 'unresolved')
        with self.assertRaises(RuntimeError):
            session.step()

    def test_frequency_tiles_join_submitted_work_before_reporting_executor_failure(self):
        s = screen(precision='fp64')
        future = mock.Mock()
        future.result.return_value = []
        executor = mock.Mock()
        executor.submit.side_effect = [future, RuntimeError('executor stopped')]
        session = s.start_tiled_frequency(np.zeros(s.crop_rows, complex), [0.],
            executor=executor, workspace_workers=2)
        try:
            with self.assertRaisesRegex(RuntimeError, 'executor stopped'):
                session.step()
            future.result.assert_called_once_with()
            failed = session.finish()
            self.assertEqual(failed['reason'], 'failed_frequency_tile')
            self.assertEqual(failed['candidates'], [])
            self.assertFalse(failed['frequency_tiles']['complete_public_search'])
        finally:
            session.close()

    def test_ordered_endpoint_insertion_preserves_canonical_peak_indices(self):
        rng = np.random.default_rng(51291)
        scores = [np.array(row, dtype=float) for row in (
            [0.], [0., 0.], [1., 0.], [0., 1.], [0., 1., 0.],
            [1., 1., 0.], [0., 1., 1.], [1., 1., 1.],
            [0., 2., 2., 0., 2., 2., 0.], [2., 0., 2., 0., 2.],
            [0., np.inf, 0.], [np.nan, 1., 0.])]
        scores.extend([np.zeros(71), np.arange(71.), np.arange(71.)[::-1]])
        scores.extend(rng.normal(size=(20, 71)))
        scores.extend(rng.integers(-2, 3, size=(20, 71)).astype(float))
        for score in scores:
            for separation in (1, 2, 15, 100):
                for maximum in (1, 2, 4, 64):
                    original = signal.find_peaks(score, distance=separation)[0]
                    if len(score) == 1:
                        original = np.array([0])
                    else:
                        original = np.unique(np.r_[original,
                            0 if score[0] >= score[1] else [],
                            len(score)-1 if score[-1] >= score[-2] else []]).astype(int)
                    if len(original) > maximum:
                        original = original[np.argpartition(score[original], -maximum)[-maximum:]]
                    np.testing.assert_array_equal(
                        _candidate_peak_indices(score, separation, maximum), original)

    def test_lag_zoomfft_matches_direct_complex_correlations_and_frequency_path(self):
        m, source, epochs = fixture()
        s = screen(m, source, epochs, precision='fp64', correlation_method='lag',
                   frequency_batch_size=2)
        raw = waveform(m, source, 1, 1, -.1, .17, 10000.)
        projected = s.prepare_observation(raw)
        frequencies = np.array([-.4, -.2, 0., .2, .4])
        w = s.workspaces[0]
        lags = np.arange(s.lag_count)
        windows = projected[lags[:, None]+np.arange(s.kernel_rows)[None, :]]
        direct = np.asarray([[[np.sum(projected[lag:lag+s.kernel_rows]*
            w['columns'][ray].conj()*np.exp(-2j*np.pi*f*
            (lag+np.arange(s.kernel_rows))/s.output_fs))
            for lag in range(s.lag_count)] for ray in range(len(s.rays))]
            for f in frequencies])
        zoom_correlations = []
        for column in w['columns']:
            zoom = signal.ZoomFFT(s.kernel_rows, (frequencies[0], frequencies[-1]),
                                  m=len(frequencies), fs=s.output_fs, endpoint=True)
            transformed = zoom(windows*column.conj()[None, :], axis=-1).T
            phase = np.exp(-2j*np.pi*frequencies[:, None]*(lags[None, :]/s.output_fs))
            zoom_correlations.append(transformed*phase)
        np.testing.assert_allclose(np.asarray(zoom_correlations).transpose(1,0,2),
                                   direct, rtol=2e-11, atol=2e-11)
        captured = {}
        reference_quadratic = _quadratic_scores
        def record_correlations(values, inverse, *, explicit=True):
            if 'correlations' not in captured:
                captured['correlations'] = np.array(values, copy=True)
            scores = reference_quadratic(values, inverse, explicit=explicit)
            if 'scores' not in captured:
                captured['scores'] = np.array(scores, copy=True)
            return scores
        with mock.patch('triad_realtime_fft._quadratic_scores', side_effect=record_correlations):
            lag_result = s.score_projected(projected, frequencies, top_per_bit=4,
                projection_receipt=s.projection_receipt())
        np.testing.assert_allclose(captured['correlations'], direct, rtol=2e-11, atol=2e-11)
        direct_scores = reference_quadratic(direct, w['inverse'], explicit=w['quadratic_explicit'])/s.reference_variance
        np.testing.assert_allclose(captured['scores']/s.reference_variance, direct_scores,
                                   rtol=3e-11, atol=3e-11)
        self.assertEqual(lag_result['status'], 'proposed')
        self.assertEqual(lag_result['work']['selected_correlation_method'], 'lag')
        # Score and candidate selection are compared with the existing path
        # on the same projected observation, grid, templates and inverses.
        legacy = screen(m, source, epochs, precision='fp64', correlation_method='frequency')
        legacy_result = legacy.score_projected(legacy.prepare_observation(raw), frequencies, top_per_bit=4,
            projection_receipt=legacy.projection_receipt())
        self.assertEqual(legacy_result['status'], 'proposed')
        for left, right in zip(lag_result['candidates'], legacy_result['candidates']):
            self.assertEqual({k:v for k,v in left.items() if k != 'score'},
                             {k:v for k,v in right.items() if k != 'score'})
            self.assertAlmostEqual(left['score']/right['score'], 1., places=10)

    def test_lag_zoomfft_handles_one_ray_rank_deficiency_and_interval_unions(self):
        m, source, epochs = fixture()
        m['channel'].update(spread_delay_ms=0., spread_doppler_hz=0.)
        one_ray = screen(m, source, epochs, precision='fp64', correlation_method='lag')
        observation = one_ray.prepare_observation(waveform(m, source, 0, 1, .1, 0., 0.))
        intervals = [[-.4, 0.], [0., .4]]
        result = one_ray.score_projected(observation, frequency_intervals=intervals, top_per_bit=3,
            projection_receipt=one_ray.projection_receipt())
        self.assertEqual(result['status'], 'proposed')
        self.assertEqual(result['frequencies_hz'], [-.4, 0., .4])
        self.assertEqual(result['work']['frequency_grid_groups'], 2)
        self.assertTrue(all(np.isfinite(row['score']) for row in result['candidates']))
        singleton = one_ray.score_projected(observation, frequency_intervals=[[-.2, -.2]],
            top_per_bit=3, projection_receipt=one_ray.projection_receipt())
        singleton_old = screen(m, source, epochs, precision='fp64', correlation_method='frequency')
        singleton_reference = singleton_old.score_projected(
            observation,
            frequency_intervals=[[-.2, -.2]], top_per_bit=3,
            projection_receipt=singleton_old.projection_receipt())
        for a, b in zip(singleton['candidates'], singleton_reference['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k != 'score'},
                             {k:v for k,v in b.items() if k != 'score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=10)
        nonzero_singleton = one_ray.score_projected(observation,
            frequency_intervals=[[.2, .2]], top_per_bit=3,
            projection_receipt=one_ray.projection_receipt())
        nonzero_reference = singleton_old.score_projected(
            observation,
            frequency_intervals=[[.2, .2]], top_per_bit=3,
            projection_receipt=singleton_old.projection_receipt())
        for a, b in zip(nonzero_singleton['candidates'], nonzero_reference['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k != 'score'},
                             {k:v for k,v in b.items() if k != 'score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=10)

        disjoint = one_ray.score_projected(observation,
            frequency_intervals=[[-.4, -.2], [.2, .4]], top_per_bit=3,
            projection_receipt=one_ray.projection_receipt())
        disjoint_reference = singleton_old.score_projected(
            observation,
            frequency_intervals=[[-.4, -.2], [.2, .4]], top_per_bit=3,
            projection_receipt=singleton_old.projection_receipt())
        self.assertEqual(disjoint['frequencies_hz'], disjoint_reference['frequencies_hz'])
        for a, b in zip(disjoint['candidates'], disjoint_reference['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k != 'score'},
                             {k:v for k,v in b.items() if k != 'score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=10)

        m, source, epochs = fixture()
        rank_one = screen(m, source, epochs, precision='fp64', correlation_method='lag')
        for w in rank_one.workspaces:
            w['columns'][1] = w['columns'][0]
            w['transform'][1] = w['transform'][0]
            w['inverse'] = np.full((2, 2), .25, dtype=np.complex128)
            w['rank'] = 1
            w['quadratic_explicit'] = False
        observed = rank_one.prepare_observation(waveform(m, source, 1, 1, .1, -.2, 0.))
        lag = rank_one.score_projected(observed, np.linspace(-.4, .4, 5), top_per_bit=3,
            projection_receipt=rank_one.projection_receipt())
        old = screen(m, source, epochs, precision='fp64', correlation_method='frequency')
        for w, copy_w in zip(old.workspaces, rank_one.workspaces):
            w['columns'][1] = w['columns'][0]
            w['transform'][1] = w['transform'][0]
            w['inverse'] = copy_w['inverse']
            w['rank'] = 1
            w['quadratic_explicit'] = False
        legacy = old.score_projected(old.prepare_observation(waveform(m, source, 1, 1, .1, -.2, 0.)),
            np.linspace(-.4, .4, 5), top_per_bit=3, projection_receipt=old.projection_receipt())
        self.assertEqual([x['bit'] for x in lag['candidates']], [x['bit'] for x in legacy['candidates']])
        for a, b in zip(lag['candidates'], legacy['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k != 'score'},
                             {k:v for k,v in b.items() if k != 'score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=9)

    def test_auto_lag_budget_and_memory_preflight(self):
        m, source, epochs = fixture()
        auto = screen(m, source, epochs, correlation_method='auto',
                      maximum_frequency_points=64, maximum_fft_products=100000)
        observation = auto.prepare_observation(waveform(m, source, 1, 0, 0., .1, 0.))
        bank = np.linspace(-.5, .5, 32)
        result = auto.score_projected(observation, bank, projection_receipt=auto.projection_receipt())
        plan = auto._correlation_plan(bank, [bank], raw_input=False)
        self.assertEqual(result['work']['selected_correlation_method'], plan['selected'][0])
        self.assertLessEqual(result['work']['fft_products'], auto.maximum_fft_products)

        bounded = screen(m, source, epochs, correlation_method='lag')
        bounded.memory_limit_bytes = 1
        with mock.patch('triad_realtime_fft.signal.ZoomFFT') as zoom:
            limited = bounded.score(np.zeros(bounded.samples, complex), frequency_intervals=[[-.5, .5]])
        self.assertEqual(limited['reason'], 'correlation_working_memory_limit')
        zoom.assert_not_called()

        frequencies = np.linspace(-.5, .5, 32)
        coarse = auto._correlation_plan(frequencies, [frequencies], raw_input=False,
            top_per_bit=2, separation=1.)
        fine = auto._correlation_plan(frequencies, [frequencies], raw_input=False,
            top_per_bit=64, separation=1e-6)
        self.assertGreater(fine['estimated_peak_bytes_by_method']['lag'],
                           coarse['estimated_peak_bytes_by_method']['lag'])
        bounded.memory_limit_bytes = coarse['estimated_peak_bytes_by_method']['lag']
        with mock.patch('triad_realtime_fft.signal.ZoomFFT') as zoom:
            limited = bounded.score_projected(observation, frequencies,
                top_per_bit=64, minimum_arrival_separation_seconds=1e-6,
                projection_receipt=bounded.projection_receipt())
        self.assertEqual(limited['reason'], 'correlation_working_memory_limit')
        zoom.assert_not_called()

    def test_ground_geometry_zoom_matches_direct_edges_and_center(self):
        # Arithmetic-only control at the long-kernel Ground search geometry.
        # No channel simulation, receiver timing, or sensitivity trial is used.
        fs, count = 20, 13448
        times = np.arange(count)/fs
        envelope = np.sin(np.pi*np.arange(count)/(count-1))**2
        source = np.asarray([[envelope*np.exp(2j*np.pi*(.08*times+.00003*times**2)),
                              envelope*np.exp(-2j*np.pi*(.1*times+.00002*times**2))]])
        manifest = dict(sample_rate_hz=fs,
            calendar=dict(capture_samples=14500, capture_lead_seconds=8.,
                data_sample_offset=20, arrival_prior_seconds=[-7., 7.]),
            channel=dict(spread_delay_ms=400., spread_doppler_hz=.03),
            format=dict(occupied_bandwidth_hz=.4, fine_chip_rate_hz=1/6),
            power=dict(complex_noise_variance=.1))
        options = dict(clock_ppm=(0.,), output_sample_rate=5,
            pass_edge_hz=2., stop_edge_hz=2.45, precision='fp64', workers=1,
            maximum_frequency_points=4096, correlation_method='lag')
        s = FFTScreen(manifest, source, (0,), **options)
        self.assertEqual(s.lag_count, 71)
        self.assertGreaterEqual(s.kernel_rows, 3370)
        rng = np.random.default_rng(84201)
        observation = rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)
        frequencies = np.linspace(-.377, .166, 1462)
        captured = {}
        def record(values, inverse, *, explicit=True):
            scores = _quadratic_scores(values, inverse, explicit=explicit)
            if not captured:
                captured.update(correlations=np.array(values, copy=True),
                                scores=np.array(scores, copy=True))
            return scores
        with (mock.patch('triad_realtime_fft._quadratic_scores', side_effect=record),
              mock.patch('triad_realtime_fft.signal.ZoomFFT', wraps=signal.ZoomFFT) as zoom):
            result = s.score_projected(observation, frequencies,
                projection_receipt=s.projection_receipt())
        self.assertEqual(result['status'], 'proposed')
        self.assertEqual(zoom.call_count, 1)  # cached for every workspace/ray/lag batch
        self.assertEqual(result['work']['actual_fft_transform_count'],
                         1+2*len(s.workspaces)*len(s.rays)*s.lag_count)
        indices = [0, len(frequencies)//2, len(frequencies)-1]
        lags = [0, s.lag_count//2, s.lag_count-1]
        workspace = s.workspaces[0]
        direct = np.asarray([[[np.sum(observation[lag:lag+s.kernel_rows]*column.conj()*
            np.exp(-2j*np.pi*frequency*(lag+np.arange(s.kernel_rows))/s.output_fs))
            for lag in lags] for column in workspace['columns']]
            for frequency in frequencies[indices]])
        np.testing.assert_allclose(captured['correlations'][indices][:, :, lags],
                                   direct, rtol=3e-10, atol=3e-10)
        expected = _quadratic_scores(direct, workspace['inverse'],
                                    explicit=workspace['quadratic_explicit'])
        np.testing.assert_allclose(captured['scores'][indices][:, lags], expected,
                                   rtol=6e-10, atol=3e-10)

    def test_lag_explicit_bank_sorting_and_irregular_auto_fallback(self):
        s = screen(precision='fp64', correlation_method='lag')
        observation = s.prepare_observation(np.zeros(s.samples, complex))
        result = s.score_projected(observation, [.2, -.2, 0.],
            projection_receipt=s.projection_receipt())
        self.assertEqual(result['status'], 'proposed')
        self.assertEqual(result['frequencies_hz'], [-.2, 0., .2])
        s = screen(precision='fp64', correlation_method='auto')
        result = s.score(np.zeros(s.samples, complex), [-.2, -.01, .2])
        self.assertEqual(result['status'], 'proposed')
        self.assertEqual(result['work']['selected_correlation_method'], 'frequency')

    def test_mapped_source_preserves_fractional_zero_extension_at_both_edges(self):
        amplitudes = {(-3, 0): 1., (5, 1): -2j}
        lanes = tuple(amplitudes)
        source = np.ones(12, complex)
        requests = []
        def provider(requested, positions):
            requests.append(positions.copy())
            return np.asarray([np.full(len(positions), amplitudes[lane], complex) for lane in requested])
        positions = np.array([-2., -1.1, -1e-6, 0., 1., 5., 10., 11., 11.9, 12.1, 13., 14.])
        result = _mapped_finite_source(provider, lanes, positions, len(source), [])
        expected = np.asarray([_sample_cubic(source*amplitudes[lane], positions) for lane in lanes])
        np.testing.assert_allclose(result, expected, rtol=2e-14, atol=2e-14)
        self.assertTrue(all(np.min(request) >= 0 and np.max(request) < len(source) for request in requests))

    def test_collapsed_cubic_fir_matches_explicit_reconstruction_and_finite_edges(self):
        rng = np.random.default_rng(971)
        taps = np.array([.03, .12, .2, .3, .2, .12, .03])
        for step in (1, 3, 8):
            for decimation in (2, 5):
                count = 197
                coarse = rng.normal(size=math.ceil((count-1)/step)+1)+1j*rng.normal(size=math.ceil((count-1)/step)+1)
                raw = _sample_cubic(coarse, np.arange(count)/step)
                expected = np.convolve(raw, taps)[::decimation]
                kernel, shift = _collapsed_cubic_fir(taps, step, decimation)
                actual = _project_coarse_column(coarse, count, taps, step,
                    decimation, kernel, shift, np.complex128)
                np.testing.assert_allclose(actual, expected, rtol=3e-13, atol=3e-14)

    def test_collapsed_final_coarse_knot_cannot_leak_beyond_finite_raw_end(self):
        count, step, decimation = 197, 8, 2
        taps = np.ones(64)
        coarse = np.zeros(26, complex)
        coarse[-1] = 1
        expected = np.convolve(_sample_cubic(coarse, np.arange(count)/step), taps)[::decimation]
        kernel, shift = _collapsed_cubic_fir(taps, step, decimation)
        actual = _project_coarse_column(coarse, count, taps, step,
            decimation, kernel, shift, np.complex128)
        np.testing.assert_allclose(actual, expected, rtol=3e-13, atol=3e-14)

    def test_streamed_templates_and_observation_match_raw_warp_and_full_tail(self):
        m, source, epochs = fixture()
        clocks = (-10000., 0., 10000.)
        reference = screen(m, source, epochs, clock_ppm=clocks, precision='fp64')
        raw = waveform(m, source, 1, 1, .1, 0., clocks[-1])
        expected = reference.score(raw, [0.], top_per_bit=2)
        for chunk in (17, 31, 64):
            requests = []
            def provider(epoch, bit, first, count):
                requests.append((first, count))
                return source[epochs.index(epoch), bit, first:first + count]
            actual = screen(m, None, epochs, clock_ppm=clocks, precision='fp64',
                template_provider=provider, source_samples=source.shape[-1],
                template_identity=hashlib.sha256(source.view(np.uint8)).hexdigest(),
                template_chunk_samples=chunk)
            for before, after in zip(reference.workspaces, actual.workspaces):
                np.testing.assert_allclose(after['columns'], before['columns'], rtol=3e-12, atol=3e-13)
                np.testing.assert_allclose(after['gram'], before['gram'], rtol=3e-12, atol=3e-12)
            comparison = compare_template_projectors(reference, actual)
            self.assertTrue(comparison['rank_agreement'])
            self.assertLess(comparison['maximum_projector_distance'], 1e-6)
            projected = actual.prepare_observation_stream(raw[first:first + chunk]
                for first in range(0, len(raw), chunk))
            np.testing.assert_allclose(projected, reference.prepare_observation(raw), rtol=3e-12, atol=3e-13)
            result = actual.score_projected(projected, [0.], top_per_bit=2)
            receipt = actual.projection_receipt()
            self.assertEqual(receipt['first_cropped_convolution_raw_sample'],
                actual.crop_first*actual.decimation)
            self.assertEqual(receipt['cropped_output_count'], len(projected))
            self.assertEqual(receipt['raw_samples'], len(raw))
            self.assertEqual(receipt['fir_taps_sha256'],
                hashlib.sha256(actual.taps.astype('<f8').tobytes()).hexdigest())
            for before, after in zip(expected['candidates'], result['candidates']):
                self.assertEqual({k:v for k,v in before.items() if k != 'score'},
                                 {k:v for k,v in after.items() if k != 'score'})
                self.assertAlmostEqual(before['score'], after['score'], places=10)
            self.assertLessEqual(max(count for _, count in requests), math.ceil(chunk * 1.01) + 4)
            self.assertLess(actual.resident_estimate, reference.resident_estimate)

    def test_large_cpu_capture_uses_bounded_projection_and_counts_live_input(self):
        m, source, epochs = fixture()
        raw = waveform(m, source, 1, 1, .1, .12, 0.)
        raw[-1] = 4+3j
        valid = np.ones(len(raw), np.uint8)
        for precision, tolerance in (('fp64', 3e-12), ('fp32', 2e-6)):
            s = screen(m, source, epochs, precision=precision, template_chunk_samples=31)
            expected = s.prepare_observation(raw, valid)
            whole_peak = s._observation_peak_estimate()
            with self.subTest(precision=precision), mock.patch(
                    'triad_realtime_fft._STREAM_OBSERVATION_SAMPLES', len(raw)):
                self.assertLess(s._observation_peak_estimate(), whole_peak)
                actual = s.prepare_observation(raw, valid)
                self.assertEqual(actual.dtype, expected.dtype)
                np.testing.assert_allclose(actual, expected, rtol=tolerance, atol=tolerance*.1)
                result = s.score(raw, [0.], valid=valid)
                self.assertEqual(result['status'], 'proposed')
                plan = s._correlation_plan(np.array([0.]), None, raw_input=True)
                self.assertEqual(result['work']['estimated_peak_bytes'],
                    plan['estimated_peak_bytes_by_method']['frequency']+raw.nbytes+valid.nbytes)
                s.memory_limit_bytes = result['work']['estimated_peak_bytes']-1
                with mock.patch.object(s, 'prepare_observation') as prepare:
                    self.assertEqual(s.score(raw, [0.], valid=valid)['reason'],
                                     'correlation_working_memory_limit')
                    prepare.assert_not_called()
                s.memory_limit_bytes = s._observation_peak_estimate()
                self.assertEqual(len(s.prepare_observation(raw, valid)), s.crop_rows)
                s.memory_limit_bytes -= 1
                with mock.patch('triad_realtime_fft._fir_decimate_chunks') as fir:
                    with self.assertRaisesRegex(MemoryError, 'streamed observation FIR'):
                        s.prepare_observation(raw, valid)
                    fir.assert_not_called()
                s.memory_limit_bytes = whole_peak
                for invalid, message in ((0, 'erasures'), (2, 'binary capture validity')):
                    mask = valid.copy()
                    mask[-1] = invalid
                    with self.assertRaisesRegex(ValueError, message):
                        s.prepare_observation(raw, mask)
                with self.assertRaisesRegex(ValueError, 'binary capture validity'):
                    s.prepare_observation(raw, valid[:-1])
                with self.assertRaisesRegex(ValueError, 'finite complex'):
                    s.prepare_observation(raw[:-1], valid)
                invalid_raw = raw.copy()
                invalid_raw[-1] = np.inf
                with self.assertRaisesRegex(ValueError, 'finite complex'):
                    s.prepare_observation(invalid_raw, valid)
                with mock.patch.object(s, 'device_parallel', True), \
                        mock.patch.object(s, 'prepare_observation_stream') as stream:
                    self.assertEqual(s._observation_peak_estimate(), whole_peak)
                    np.testing.assert_array_equal(s.prepare_observation(raw, valid), expected)
                    stream.assert_not_called()

    def test_streamed_fir_noise_covariance_keeps_edges_and_color(self):
        # Independent finite convolution-matrix oracle. White unit complex
        # input becomes H H*: neither the edge variance nor adjacent-output
        # covariance can be replaced by one automatic white-noise variance.
        taps = np.array([.2, .3, .5, .3, .2])
        samples, decimation = 19, 3
        expected, actual = [], []
        for index in range(samples):
            unit = np.zeros(samples, complex)
            unit[index] = 1
            expected.append(np.convolve(unit, taps)[::decimation])
            actual.append(_fir_decimate_chunks((unit[first:first + 4]
                for first in range(0, samples, 4)), taps, decimation, samples, np.complex128, 4))
        before, after = np.asarray(expected).T, np.asarray(actual).T
        np.testing.assert_allclose(after, before, rtol=2e-14, atol=2e-15)
        covariance = after @ after.conj().T
        np.testing.assert_allclose(covariance, before @ before.conj().T, rtol=2e-14, atol=2e-15)
        self.assertAlmostEqual(float(covariance[2, 2].real), .51)
        self.assertAlmostEqual(float(covariance[2, 3].real), .12)
        self.assertAlmostEqual(float(covariance[0, 0].real), .04)

    def test_streamed_entry_points_reject_incomplete_or_ambiguous_inputs(self):
        m, source, epochs = fixture()
        with self.assertRaisesRegex(ValueError, 'identity'):
            screen(m, None, epochs, template_provider=lambda *args: source,
                source_samples=200, template_identity='not-a-hash')
        with self.assertRaisesRegex(ValueError, 'raw_templates'):
            screen(m, source, epochs, template_provider=lambda *args: source,
                source_samples=200, template_identity='0' * 64)
        s = screen(template_chunk_samples=17)
        for chunks in (iter([np.zeros(17, complex)]), iter([np.zeros(18, complex)])):
            with self.assertRaises(ValueError):
                s.prepare_observation_stream(chunks)
        with self.assertRaises(ValueError):
            s.score_projected(np.zeros(s.crop_rows + 1, complex), [0.])
        receipt = s.projection_receipt()
        receipt['cropped_output_start'] += 1
        with self.assertRaisesRegex(ValueError, 'receipt'):
            s.score_projected(np.zeros(s.crop_rows, complex), [0.], projection_receipt=receipt)

    def test_explicit_quadratic_noise_rankings_and_precision(self):
        rng = np.random.default_rng(1742)
        for precision in (np.complex64, np.complex128):
            for rays in (1, 2):
                c = (rng.normal(size=(8, rays, 1000))+
                     1j*rng.normal(size=(8, rays, 1000))).astype(precision)
                h = np.array([[1.3, .13+.27j], [.13-.27j, 1.]], np.complex128)[:rays, :rays]
                reference = np.einsum('brl,rs,bsl->bl', c.conj(), h, c, optimize=True).real
                actual = _quadratic_scores(c, h)
                np.testing.assert_allclose(actual, reference, rtol=2e-15, atol=2e-14)
                np.testing.assert_array_equal(np.argsort(actual.ravel())[-20:],
                                              np.argsort(reference.ravel())[-20:])
                self.assertEqual(actual.dtype, np.float64)

    def test_ill_conditioned_quadratic_keeps_reference_path(self):
        rng = np.random.default_rng(1842)
        c = (rng.normal(size=(2, 2, 73))+1j*rng.normal(size=(2, 2, 73))).astype(np.complex64)
        v = np.array([[1, 1j], [1j, 1]], complex)/np.sqrt(2)
        h = (v*np.array([1., 1e8])) @ v.conj().T
        reference = np.einsum('brl,rs,bsl->bl', c.conj(), h, c, optimize=True).real
        np.testing.assert_array_equal(_quadratic_scores(c, h, explicit=False), reference)
        m, source, epochs = fixture()
        m['channel'].update(spread_delay_ms=1e-5, spread_doppler_hz=0.)
        s = screen(m, source, epochs)
        self.assertTrue(all(not w['quadratic_explicit'] for w in s.workspaces))
        result = s.score(np.zeros(s.samples, complex), [0.])
        self.assertEqual(result['work']['explicit_quadratic_workspaces'], 0)
        self.assertTrue(all(np.isfinite(c['score']) for c in result['candidates']))

    def test_fft_scores_match_direct_projected_glrt(self):
        s = screen(precision='fp64')
        rng = np.random.default_rng(421)
        raw = rng.normal(size=s.samples)+1j*rng.normal(size=s.samples)
        y = s.prepare_observation(raw)
        frequency = 1.3
        y *= np.exp(-2j*np.pi*frequency*np.arange(len(y))/s.output_fs)
        for w in s.workspaces:
            corr = fft.ifft(fft.fft(y, n=s.nfft)[None]*w['transform'], axis=-1)[:, :s.lag_count]
            for lag in (0, s.lag_count//2, s.lag_count-1):
                direct = w['columns'].conj() @ y[lag:lag+s.kernel_rows]
                np.testing.assert_allclose(corr[:, lag], direct, rtol=2e-12, atol=2e-12)
                score = np.vdot(corr[:, lag], w['inverse']@corr[:, lag]).real
                reference = np.linalg.norm(np.linalg.lstsq(w['columns'].T,
                    y[lag:lag+s.kernel_rows], rcond=1e-10)[0] @ w['columns'])**2
                self.assertAlmostEqual(score, reference, places=9)

    def test_valid_only_fft_padding_matches_every_direct_lag(self):
        padding_cases = set()
        rng = np.random.default_rng(1852)
        for extra_lags in range(8):
            m, source, epochs = fixture()
            m['calendar']['arrival_prior_seconds'][1] += extra_lags/200
            for precision in ('fp32', 'fp64'):
                s = screen(m, source, epochs, precision=precision)
                self.assertEqual(s.nfft, fft.next_fast_len(s.crop_rows))
                self.assertLess(s.nfft, s.crop_rows+s.kernel_rows-1)
                padding_cases.add(s.nfft > s.crop_rows)
                y = (rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)).astype(s.dtype)
                transformed = fft.fft(y, n=s.nfft)
                for w in s.workspaces:
                    corr = fft.ifft(transformed[None]*w['transform'], axis=-1)[:, :s.lag_count]
                    direct = np.asarray([w['columns'].astype(np.complex128).conj() @
                        y[lag:lag+s.kernel_rows].astype(np.complex128)
                        for lag in range(s.lag_count)]).T
                    tolerance = 3e-6 if precision == 'fp32' else 3e-13
                    np.testing.assert_allclose(corr, direct, rtol=tolerance,
                                               atol=tolerance*max(1., np.abs(direct).max()))
                    scores = np.einsum('rl,rs,sl->l', corr.conj(), w['inverse'], corr).real
                    reference = np.einsum('rl,rs,sl->l', direct.conj(), w['inverse'], direct).real
                    np.testing.assert_allclose(scores, reference, rtol=tolerance*3,
                                               atol=tolerance*3*max(1., reference.max()))
                    self.assertEqual(int(np.argmax(scores)), int(np.argmax(reference)))
        self.assertEqual(padding_cases, {False, True})

    def test_clock_delay_and_endpoint_arrivals(self):
        m, source, epochs = fixture()
        clocks = (-10000., 0., 10000.)
        s = screen(m, source, epochs, clock_ppm=clocks, precision='fp64')
        for arrival, clock, bit in ((-.1, -10000., 0), (.1, 10000., 1)):
            raw = waveform(m, source, 1, bit, arrival, 0., clock)
            result = s.score(raw, [0.], top_per_bit=2)
            winner = max(result['candidates'], key=lambda x:x['score'])
            self.assertEqual(winner['bit'], bit)
            self.assertEqual(winner['epoch'], epochs[1])
            self.assertAlmostEqual(winner['arrival'], arrival, places=12)
            self.assertEqual(winner['clock_ppm'], clock)
            self.assertEqual({c['bit'] for c in result['candidates']}, {0, 1})
            self.assertTrue(result['work']['source_support_complete'])

    def test_fp32_and_fp64_agree_on_rankings(self):
        m, source, epochs = fixture()
        raw = waveform(m, source, 0, 1, .025, .2, 0.)
        results = [screen(m, source, epochs, precision=p).score(raw, [-.2, 0., .2])
                   for p in ('fp32', 'fp64')]
        for a, b in zip(results[0]['candidates'], results[1]['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k!='score'},
                             {k:v for k,v in b.items() if k!='score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=5)

    def test_frequency_intervals_fine_grid_and_limits(self):
        s = screen(maximum_frequency_points=8)
        raw = np.zeros(s.samples, complex)
        result = s.score(raw, frequency_intervals=[[-.5, .5]])
        self.assertEqual(result['status'], 'proposed')
        self.assertLessEqual(np.diff(result['frequencies_hz']).max(), s.maximum_frequency_step)
        result = s.score(raw, frequency_intervals=[[-10., 10.]])
        self.assertEqual(result['reason'], 'frequency_work_limit')
        self.assertEqual(result['candidates'], [])
        result = s.score(raw, frequencies_hz=[50.])
        self.assertEqual(result['reason'], 'frequency_bank_outside_nominal_passband')
        result = s.score(raw, frequencies_hz=[-2., 2.])
        self.assertEqual(result['reason'], 'explicit_frequency_grid_too_coarse')

    def test_erasure_and_truncated_capture_rejected(self):
        s = screen()
        valid = np.ones(s.samples, bool)
        valid[100] = False
        with self.assertRaisesRegex(ValueError, 'erasures'):
            s.score(np.zeros(s.samples, complex), [0.], valid=valid)
        m, source, epochs = fixture()
        m['calendar']['capture_samples'] = 500
        with self.assertRaisesRegex(ValueError, 'complete source support'):
            screen(m, source, epochs)

    def test_policy_and_inputs_are_bound(self):
        m, source, epochs = fixture()
        a = screen(m, source, epochs)
        b = screen(m, source*1.001, epochs)
        self.assertNotEqual(a.procedure_id, b.procedure_id)
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples), [0.])
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples, complex), [0., 0.])
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples, complex), [0.], frequency_intervals=[[0., 0.]])

    def test_native_coarse_capability_fallback_is_explicit_and_idempotent(self):
        s = screen()
        with mock.patch('triad_coarse_kernel.CoarseRank2Kernel', side_effect=OSError('absent')):
            s.enable_native_coarse('local-exporter')
            first = s.procedure_id
            s.enable_native_coarse('local-exporter')
        self.assertEqual(first, s.procedure_id)
        self.assertIsNone(s.coarse_kernel)
        self.assertEqual(s.policy['coarse_score_kernel']['status'], 'numpy_scipy_fallback')


if __name__ == '__main__':
    unittest.main()

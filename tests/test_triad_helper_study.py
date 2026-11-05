#!/usr/bin/env python3
"""Cheap orchestration checks with mocked DSP, not waveform/receiver evidence.

Tiny synthetic public manifests and on-disk samples exercise the actual study
control flow. Native propagation, estimator accuracy and FFT numerics belong to
their separate focused tests and sampled qualification runs.
"""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_helper_study as study


class StudyOrchestrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.manifest = dict(schema='triad-helper-public-v1', sample_rate_hz=200,
            format=dict(physical_rf_retuning=False, requested_bit_seconds=16., bit_seconds=20.,
                        occupied_bandwidth_hz=225.),
            calendar=dict(data_sample_offset=7600, capture_lead_seconds=8., capture_samples=18000,
                          arrival_prior_seconds=[-7.035, 7.035], helper_end_seconds=80.),
            helper_plan=dict(low_hz=550., high_hz=2400., pilot_dwell_gating=False),
            channel=dict(deterministic_cfo_bound_hz=.5, spread_margin=1., spread_delay_ms=21.,
                         spread_doppler_hz=90., nominal_conversion_if_hz=1725.,
                         selective_slow_depth=.65, selective_fast_depth=.65,
                         relative_phase_diffusion_rad_sqrt_s=.01, relative_pair_ppm_bound=.0001),
            key_seed=72001, public_epoch_seconds=1793850000,
            power=dict(template_data_gain=1., complex_noise_variance=10.),
            templates=dict(enabled=True, shape=[1, 2, 4], file='templates.c128', epoch_offsets_seconds=[0]),
            observation=dict(shape=[18000], file='observation.c128'),
            validity=dict(file='validity.u8'))
        self.write_manifest()
        np.ones((1, 2, 4), dtype='<c16').tofile(self.folder / 'templates.c128')
        # Values identify the actual selected slice without invoking signal DSP.
        np.arange(18000, dtype=np.float64).astype('<c16').tofile(self.folder / 'observation.c128')
        np.ones(18000, dtype='u1').tofile(self.folder / 'validity.u8')
        (self.folder / 'evaluator-truth.json').write_text('deliberately invalid evaluator JSON')

        self.prepared = object()
        self.receiver = MagicMock()
        self.receiver.receiver_id = 'mock-full-bank'
        self.receiver.preprocessing_id = 'mock-preprocessing'
        self.receiver.prepare_observation.return_value = self.prepared
        self.receiver.score_prepared.return_value = dict(bit=0, working_presence=5., receiver_id='mock-full-bank')
        self.child = MagicMock()
        self.child.score_prepared.return_value = dict(bit=0, working_presence=6., receiver_id='mock-narrow-bank')
        self.receiver.with_frequency_offsets.return_value = self.child
        self.factory_patch = patch.object(study, 'HelperDataReceiver', return_value=self.receiver)
        self.factory = self.factory_patch.start()
        self.addCleanup(self.factory_patch.stop)
        self.fir_patch = patch.object(study, 'data_fir_taps', return_value=np.array([1.]))
        self.fir_patch.start()
        self.addCleanup(self.fir_patch.stop)
        self.estimator_patch = patch.object(study, 'estimate_frequency', side_effect=self.fallback)
        self.estimator = self.estimator_patch.start()
        self.addCleanup(self.estimator_patch.stop)

    def write_manifest(self):
        (self.folder / 'manifest.json').write_text(json.dumps(self.manifest))

    @staticmethod
    def fallback(samples, **kwargs):
        return dict(status='fallback', selected_frequencies=list(kwargs['original_frequencies']),
                    wall_seconds=.001)

    def test_public_clock_grid_bounds_and_work_cap(self):
        manifest = {'channel': {'sample_clock_pair_ppm': 100.,
                    'endpoint_uniform_half_bound_ppm': 50., 'spread_margin': 3.}}
        clocks, scales = study.public_clock_grids(manifest, 100., 3)
        np.testing.assert_array_equal(clocks, [-300, -200, -100, 0, 100, 200, 300])
        self.assertAlmostEqual(scales[0], 1 - 150 / (1 - 50e-6) * 1e-6)
        self.assertAlmostEqual(scales[-1], 1 + 150 / (1 - 50e-6) * 1e-6)
        self.assertEqual(study.public_clock_grids(manifest), ((0.,), (1.,)))
        self.assertEqual(len(study.public_clock_grids(manifest, 600 / 4095)[0]), 4096)
        with self.assertRaisesRegex(ValueError, '4096-point'):
            study.public_clock_grids(manifest, 600 / 4096)
        for step in (-1, float('nan'), 1e-9):
            with self.assertRaises(ValueError):
                study.public_clock_grids(manifest, step, 3)

    def test_independent_modulation_rate_is_not_silently_omitted(self):
        self.manifest['channel'].update(spread_doppler_hz=0, selective_modulation_rate_hz=.7)
        self.write_manifest()
        study.HelperComparison(self.folder).score(self.folder)
        self.assertEqual(self.estimator.call_args.kwargs['modulation_rate_hz'], .7)
        self.assertEqual(self.factory.call_args.kwargs['lag_step'], 1)
        self.assertEqual(self.factory.call_args.kwargs['source_half_bandwidth_hz'],112.5)
        self.assertEqual(self.factory.call_args.kwargs['channel_line_half_width_hz'],.7)
        self.manifest['channel']['selective_modulation_rate_hz'] = 300.
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'frontend geometry'):
            study.HelperComparison(self.folder)

    def test_cli_accepts_explicit_legacy_wide_frontend(self):
        output = self.folder / 'legacy-wide.json'
        fake = MagicMock()
        fake.score.return_value = dict(timing={}, assistance={'status':'fallback'})
        with patch.object(study, 'HelperComparison', return_value=fake) as factory, \
             patch.object(sys, 'argv', ['study', '--fixture', str(self.folder),
                   '--output', str(output), '--frontend-profile', 'wide']), \
             patch('builtins.print'):
            study.main()
        self.assertEqual(factory.call_args.kwargs['frontend_profile'], 'wide')
        self.assertEqual(factory.call_args.kwargs['fft_backend'], 'legacy')
        self.assertTrue(output.exists())
        # Parsing the new profile is an independent workflow entry point. The
        # rate-aware geometry itself is exercised by the constructor test.
        rate_output = self.folder / 'rate-aware.json'
        with patch.object(study, 'HelperComparison', return_value=fake) as rate_factory, \
             patch.object(sys, 'argv', ['study', '--fixture', str(self.folder),
                   '--output', str(rate_output), '--frontend-profile', 'rate-aware']), \
             patch('builtins.print'):
            study.main()
        self.assertEqual(rate_factory.call_args.kwargs['frontend_profile'], 'rate-aware')
        self.assertTrue(rate_output.exists())

    def test_cli_exposes_assisted_only_without_changing_default(self):
        output = self.folder / 'assisted-only.json'
        fake = MagicMock()
        fake.score.return_value = dict(timing={}, assistance={'status': 'narrowed'},
                                       executed_arms=['assisted'])
        with patch.object(study, 'HelperComparison', return_value=fake), \
             patch.object(sys, 'argv', ['study', '--fixture', str(self.folder),
                   '--output', str(output), '--assisted-only']), \
             patch('builtins.print'):
            study.main()
        self.assertFalse(fake.score.call_args.kwargs['include_baseline'])
        output_default = self.folder / 'paired-default.json'
        with patch.object(study, 'HelperComparison', return_value=fake), \
             patch.object(sys, 'argv', ['study', '--fixture', str(self.folder),
                   '--output', str(output_default)]), \
             patch('builtins.print'):
            study.main()
        self.assertTrue(fake.score.call_args.kwargs['include_baseline'])

    def test_fixed_public_window_dated_availability_and_no_truth_access(self):
        comparison = study.HelperComparison(self.folder)
        result = comparison.score(self.folder)
        args, kwargs = self.estimator.call_args
        self.assertEqual(len(args[0]), 36 * 200)
        self.assertEqual(args[0][0], 3087)  # (8 + 7.035 + .4) * 200
        self.assertEqual(args[0][-1], 3087 + 7200 - 1)
        self.assertAlmostEqual(kwargs['first_sample_seconds'], 7.435)
        self.assertAlmostEqual(kwargs['first_sample_seconds'] + len(args[0]) / 200, 43.435)
        self.assertEqual(kwargs['noise_variance'], 10.)
        self.assertEqual(kwargs['doppler_hz'], 90.)
        self.assertEqual(kwargs['data_if_hz'], 1725.)
        self.assertEqual(kwargs['high_pilot_hz'], 2400.)
        self.assertEqual(kwargs['slow_depth'], .65)
        self.assertEqual(kwargs['fast_depth'], .65)
        self.assertEqual(result['procedure_id'], comparison.procedure_id)
        self.assertNotIn('evaluator_truth', result)
        # Invalid evaluator JSON remains unopened by constructor and score.
        self.assertEqual((self.folder / 'evaluator-truth.json').read_text(), 'deliberately invalid evaluator JSON')

    def test_fallback_reuses_score_without_second_data_work(self):
        result = study.HelperComparison(self.folder).score(self.folder)
        self.receiver.prepare_observation.assert_called_once()
        self.receiver.score_prepared.assert_called_once_with(self.prepared)
        self.receiver.with_frequency_offsets.assert_not_called()
        self.assertTrue(result['assisted']['score_reused_from_baseline'])
        self.assertEqual(result['timing']['additional_assisted_scoring_seconds'], 0.)
        self.assertNotIn('score_reused_from_baseline', result['baseline'])

    def test_helper_disabled_skips_aperture_and_estimation(self):
        self.manifest['helper_enabled'] = False
        self.manifest['calendar'].update(capture_samples=4000, data_sample_offset=0)
        # A twenty-second DATA-only capture cannot contain a 36-second pilot
        # aperture. Its receiver need not even have a Helper endpoint.
        self.manifest['calendar'].pop('helper_end_seconds')
        self.manifest['observation']['shape'] = [4000]
        self.write_manifest()
        np.ones(4000, dtype='<c16').tofile(self.folder / 'observation.c128')
        np.ones(4000, dtype='u1').tofile(self.folder / 'validity.u8')
        comparison = study.HelperComparison(self.folder)
        result = comparison.score(self.folder)
        self.estimator.assert_not_called()
        self.receiver.prepare_observation.assert_called_once()
        self.receiver.score_prepared.assert_called_once_with(self.prepared)
        self.receiver.with_frequency_offsets.assert_not_called()
        assistance = result['assistance']
        self.assertEqual(assistance['reason'], 'helper_disabled')
        self.assertEqual(assistance['observation_seconds'], 0.)
        self.assertEqual(assistance['wall_seconds'], 0.)
        self.assertEqual(assistance['selected_frequencies'], comparison.frequencies.tolist())
        self.assertFalse(assistance['presence_is_formal_pilot_lock'])

    def test_helper_enable_policy_is_bound_to_geometry_and_procedure(self):
        historical = study.HelperComparison(self.folder)
        self.manifest['helper_enabled'] = True
        self.write_manifest()
        explicit = study.HelperComparison(self.folder)
        self.assertEqual(historical.procedure_id, explicit.procedure_id)
        self.manifest['helper_enabled'] = False
        self.write_manifest()
        disabled = study.HelperComparison(self.folder)
        self.assertNotEqual(disabled.procedure_id, explicit.procedure_id)
        self.assertNotEqual(disabled.shared_capture_id, explicit.shared_capture_id)
        with self.assertRaisesRegex(ValueError, 'capture geometry changed'):
            explicit.prepare_capture(self.folder)
        # DATA absence remains a calibration control under the same procedure.
        before = study.public_geometry(self.manifest)
        self.manifest['data_enabled'] = False
        self.assertEqual(before, study.public_geometry(self.manifest))
        self.manifest['helper_enabled'] = 'false'
        with self.assertRaisesRegex(ValueError, 'public boolean'):
            study.public_geometry(self.manifest)

    def test_stochastic_models_are_rejected_before_static_frontend_or_estimation(self):
        original = deepcopy(self.manifest)
        for helper_enabled in (False, True):
            for change in ({'channel_evolution': 'fixed-time-gaussian-v1'},
                           {'selective_coefficient_law': 'gaussian-erf-v1'},
                           {'random_doppler_width_hz': .01},
                           {'delay_excursion_seconds': .001}):
                with self.subTest(helper=helper_enabled, change=change):
                    self.manifest = deepcopy(original)
                    self.manifest['helper_enabled'] = helper_enabled
                    self.manifest['channel'].update(change)
                    self.write_manifest()
                    with self.assertRaisesRegex(ValueError, 'stochastic channel is unsupported'):
                        study.HelperComparison(self.folder)
        self.factory.assert_not_called()
        self.estimator.assert_not_called()
        # A requested stochastic profile cleared by the flat control is a
        # legacy effective channel and remains usable by the old receiver.
        self.manifest = original
        self.manifest['channel'].update(requested_channel_evolution='stochastic',
            channel_evolution='legacy', selective_coefficient_law='sinusoidal',
            random_doppler_width_hz=0., delay_excursion_seconds=0.)
        self.write_manifest()
        study.HelperComparison(self.folder).score(self.folder)
        self.estimator.assert_called_once()

    def test_data_only_frontend_does_not_reserve_absent_helper_bands(self):
        self.manifest['sample_rate_hz'] = 6000
        self.manifest['helper_enabled'] = False
        # An alleged auxiliary at DATA center would reject Helper-on operation.
        self.manifest['helper_plan']['auxiliary_hz'] = 1725.
        support = study._frontend_support(self.manifest, 'rate-aware', (0.,), (1.,))
        self.assertEqual(support['rejected_helper_bands_hz'], [])
        self.assertIsNone(support['nearest_rejected_band_edge_hz'])
        self.manifest['helper_enabled'] = True
        with self.assertRaisesRegex(ValueError, 'overlaps a declared Helper'):
            study._frontend_support(self.manifest, 'rate-aware', (0.,), (1.,))

    def test_assisted_only_narrowing_skips_full_bank_and_matches_paired_assisted_score(self):
        self.estimator.side_effect = lambda samples, **kwargs: dict(
            status='narrowed', selected_frequencies=[0.], wall_seconds=.001)
        comparison = study.HelperComparison(self.folder)
        capture = comparison.prepare_capture(self.folder)
        paired = comparison.score_capture(capture)
        self.receiver.score_prepared.reset_mock()
        self.child.score_prepared.reset_mock()
        assisted_only = comparison.score_capture(capture, include_baseline=False)
        self.assertIsNone(assisted_only['baseline'])
        self.assertEqual(assisted_only['executed_arms'], ['assisted'])
        self.assertNotEqual(paired['execution_policy_id'], assisted_only['execution_policy_id'])
        self.assertIsNone(assisted_only['timing']['additional_assisted_scoring_seconds'])
        self.assertGreaterEqual(assisted_only['timing']['assisted_score_seconds'], 0.)
        self.receiver.score_prepared.assert_not_called()
        self.child.score_prepared.assert_called_once_with(self.prepared)
        self.assertEqual(assisted_only['assisted']['bit'], paired['assisted']['bit'])
        self.assertEqual(assisted_only['assisted']['working_presence'],
                         paired['assisted']['working_presence'])
        self.assertEqual(assisted_only['search_banks'], paired['search_banks'])

    def test_assisted_only_fallback_scores_full_bank_once_as_assisted(self):
        comparison = study.HelperComparison(self.folder)
        capture = comparison.prepare_capture(self.folder)
        self.receiver.score_prepared.reset_mock()
        result = comparison.score_capture(capture, include_baseline=False)
        self.assertIsNone(result['baseline'])
        self.assertEqual(result['executed_arms'], ['assisted'])
        self.assertFalse(result['assisted']['score_reused_from_baseline'])
        self.receiver.score_prepared.assert_called_once_with(self.prepared)
        self.receiver.with_frequency_offsets.assert_not_called()
        self.assertEqual(result['assisted']['bit'], 0)

    def test_narrowing_reuses_same_prepared_capture_and_keeps_procedure_identity(self):
        comparison = study.HelperComparison(self.folder)
        original_identity = comparison.procedure_id
        self.estimator.side_effect = lambda samples, **kwargs: dict(
            status='narrowed', selected_frequencies=[0.], wall_seconds=.001)
        result = comparison.score(self.folder)
        self.receiver.prepare_observation.assert_called_once()
        self.receiver.score_prepared.assert_called_once_with(self.prepared)
        self.receiver.with_frequency_offsets.assert_called_once_with([0.])
        self.child.score_prepared.assert_called_once_with(self.prepared)
        self.child.prepare_observation.assert_not_called()
        self.assertFalse(result['assisted']['score_reused_from_baseline'])
        self.assertEqual(result['procedure_id'], original_identity)
        self.assertNotEqual(result['baseline']['receiver_id'], result['assisted']['receiver_id'])

    def test_changed_noise_and_geometry_rejected_before_dsp(self):
        comparison = study.HelperComparison(self.folder)
        original = deepcopy(self.manifest)
        self.manifest['power']['complex_noise_variance'] *= 2
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'noise reference changed'):
            comparison.score(self.folder)
        self.manifest = deepcopy(original)
        self.manifest['calendar']['arrival_prior_seconds'][1] += 1
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'capture geometry changed'):
            comparison.score(self.folder)
        self.estimator.assert_not_called()
        self.receiver.prepare_observation.assert_not_called()

    def test_missing_or_erased_samples_rejected_before_dsp(self):
        comparison = study.HelperComparison(self.folder)
        valid_path = self.folder / 'validity.u8'
        for values in (np.ones(17999, dtype='u1'), np.r_[np.zeros(1, dtype='u1'), np.ones(17999, dtype='u1')]):
            values.tofile(valid_path)
            with self.assertRaisesRegex(ValueError, 'capture validity differs'):
                comparison.score(self.folder)
        np.ones(18000, dtype='u1').tofile(valid_path)
        np.zeros(17999, dtype='<c16').tofile(self.folder / 'observation.c128')
        with self.assertRaisesRegex(ValueError, 'capture file size mismatch'):
            comparison.score(self.folder)
        self.estimator.assert_not_called()
        self.receiver.prepare_observation.assert_not_called()

    def test_fixed_data_erasure_shared_preparation_and_helper_fallback(self):
        values = np.ones(18000, dtype='u1')
        values[15000:15002] = 0  # outside fixed Helper window
        values.tofile(self.folder / 'validity.u8')
        comparison = study.HelperComparison(self.folder)
        np.testing.assert_array_equal(self.factory.call_args.kwargs['capture_validity'], values)
        capture = comparison.prepare_capture(self.folder)
        comparison.score_capture(capture)
        comparison.score_capture(capture)
        self.estimator.assert_called_once()
        self.receiver.prepare_observation.assert_called_once()
        self.receiver.score_prepared.assert_called_with(self.prepared)
        with self.assertRaisesRegex(ValueError, 'shared capture policy'):
            comparison.score_capture(dict(capture, shared_capture_id='different'))
        values[4000] = 0  # inside fixed Helper window: do not infer through lost samples
        values.tofile(self.folder / 'validity.u8')
        comparison = study.HelperComparison(self.folder)
        result = comparison.score(self.folder)
        self.assertEqual(result['assistance']['reason'], 'known erasure in Helper observation')
        self.assertTrue(result['assisted']['score_reused_from_baseline'])
        self.estimator.assert_called_once()

    def test_shared_preparation_rejects_different_noise_reference(self):
        first = study.HelperComparison(self.folder)
        capture = first.prepare_capture(self.folder)
        self.manifest['power']['complex_noise_variance'] *= 2
        self.write_manifest()
        second = study.HelperComparison(self.folder)
        with self.assertRaisesRegex(ValueError, 'shared capture policy differs'):
            second.score_capture(capture)

    def test_insufficient_guaranteed_helper_support_rejected(self):
        self.manifest['calendar']['helper_end_seconds'] = 50.
        self.write_manifest()
        comparison = study.HelperComparison(self.folder)
        with self.assertRaisesRegex(ValueError, 'insufficient guaranteed Helper support'):
            comparison.score(self.folder)
        self.estimator.assert_not_called()
        self.receiver.prepare_observation.assert_not_called()

    def test_physical_retuning_and_template_mismatch_rejected(self):
        self.manifest['format']['physical_rf_retuning'] = True
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'physical RF retuning'):
            study.HelperComparison(self.folder)
        self.manifest['format']['physical_rf_retuning'] = False
        self.write_manifest()
        other = self.folder / 'different-templates'
        other.mkdir()
        different = deepcopy(self.manifest)
        different['key_seed'] += 1
        (other / 'manifest.json').write_text(json.dumps(different))
        with self.assertRaisesRegex(ValueError, 'template source mismatch: key_seed'):
            study.HelperComparison(self.folder, other)
        self.factory.assert_not_called()

    def test_frequency_grid_endpoints_spacing_and_work_bound(self):
        values = study.frequency_grid(.58, 26.4)
        self.assertEqual(len(values), 125)
        self.assertEqual(values[0], -.58)
        self.assertEqual(values[-1], .58)
        self.assertEqual(values[len(values) // 2], 0.)
        self.assertLessEqual(np.max(np.diff(values)), 1 / (4 * 26.4))
        np.testing.assert_array_equal(study.frequency_grid(.001, 1), [-.001, 0., .001])
        for bound, duration in ((0, 1), (-1, 1), (1, 0), (float('nan'), 1), (1, float('inf'))):
            with self.assertRaisesRegex(ValueError, 'positive finite'):
                study.frequency_grid(bound, duration)
        with self.assertRaisesRegex(ValueError, 'bounded study work'):
            study.frequency_grid(1, 512)

    def test_endpoint_inclusive_clock_and_rate_grid_helpers(self):
        clocks = study._inclusive_grid(300., 7)
        self.assertEqual(clocks[0], -300.)
        self.assertEqual(clocks[-1], 300.)
        self.assertEqual(clocks[3], 0.)
        scales = study._inclusive_grid(150e-6, 3, center=1.)
        np.testing.assert_allclose(scales, [1 - 150e-6, 1., 1 + 150e-6], rtol=0, atol=1e-15)
        bank = study.frequency_grid(90.5220261, 23.2, max_points=1_000_000)
        self.assertEqual(len(bank), 2 * int(np.ceil(90.5220261 * 4 * 23.2)) + 1)
        with self.assertRaisesRegex(ValueError, 'bounded study work'):
            study.frequency_grid(90.5220261, 23.2)

    def test_interval_subset_keeps_adjacent_public_clock_candidates(self):
        public = (-300., -200., -100., 0., 100., 200., 300.)
        selected = study._bank_subset(public, [[70., 130.]])
        np.testing.assert_array_equal(selected, [0., 100., 200.])
        edge = study._bank_subset(public, [[-350., -330.]])
        np.testing.assert_array_equal(edge, [-300.])
        with self.assertRaisesRegex(ValueError, 'increasing'):
            study._bank_subset(public, [[2., 1.]])

    def test_aligned_fft_grid_is_opt_in_and_bound_into_procedure_identity(self):
        self.receiver.aligned_frequency_offsets.return_value = (-.5, -.25, 0., .25, .5)
        self.receiver.with_hypotheses.return_value = self.receiver
        default = study.HelperComparison(self.folder)
        self.assertEqual(len(default.frequencies), 81)
        aligned = study.HelperComparison(self.folder, fft_backend='batched',
                                          aligned_fft_grid=True)
        np.testing.assert_array_equal(aligned.frequencies, [-.5, -.25, 0., .25, .5])
        self.receiver.aligned_frequency_offsets.assert_called_once_with(aligned.original_bound)
        self.receiver.with_hypotheses.assert_called_once_with(frequency_offsets=aligned.frequencies)
        self.assertNotEqual(default.procedure_id, aligned.procedure_id)
        with self.assertRaisesRegex(ValueError, 'requires --fft-backend batched'):
            study.HelperComparison(self.folder, aligned_fft_grid=True)

    def test_workspace_cache_capacity_and_memory_limit_are_explicit(self):
        cached = study.HelperComparison(self.folder, fft_backend='batched',
            max_cached_hypotheses=13, memory_limit_bytes=8 * 1024**3)
        options = self.factory.call_args.kwargs
        self.assertEqual(options['max_cached_hypotheses'], 13)
        self.assertEqual(options['memory_limit_bytes'], 8 * 1024**3)
        self.assertNotEqual(cached.procedure_id,
            study.HelperComparison(self.folder, fft_backend='batched').procedure_id)

    def test_actual_wider_data_band_is_unsupported_before_receiver_preparation(self):
        # Fine200 has nominal bandwidth750 Hz. It fits the wide profile under
        # this selected 6-Hz channel, but not the old fixed330-Hz pass edge.
        self.manifest['format']['occupied_bandwidth_hz'] = 750.
        self.manifest['channel']['spread_doppler_hz'] = 6.
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'unsupported DATA frontend geometry'):
            study.HelperComparison(self.folder, fft_backend='batched')
        self.factory.assert_not_called()
        comparison = study.HelperComparison(self.folder, fft_backend='batched', frontend_profile='wide')
        self.assertAlmostEqual(comparison.frontend_support['required_nominal_edge_hz'],
                               375.0000000375 + 12.0000000006 + .5)
        self.assertFalse(comparison.frontend_support['compact_spectral_support'])
        self.assertFalse(comparison.frontend_support['receiver_sensitivity_qualification'])

    def test_rate_aware_frontend_selects_public_band_clock_and_forwards_it(self):
        # A 225-Hz triplet plus the configured line/CFO envelope needs just
        # under 300 Hz. The cheapest divisor of 6000 with a 0.4 pass fraction
        # is 750 Hz; timing remains on that output clock.
        self.manifest['sample_rate_hz'] = 6000
        self.manifest['calendar']['data_sample_offset'] = 228000
        self.manifest['calendar']['capture_samples'] = 250000
        self.write_manifest()
        np.ones(250000, dtype='u1').tofile(self.folder / 'validity.u8')
        comparison = study.HelperComparison(self.folder, fft_backend='batched',
            frontend_profile='rate-aware')
        self.assertEqual(comparison.frontend_support['output_sample_rate_hz'], 750)
        self.assertEqual(comparison.frontend_support['decimation'], 8)
        self.assertLess(comparison.frontend_support['pass_edge_hz'],
                        comparison.frontend_support['stop_edge_hz'])
        helper_edges = comparison.frontend_support['rejected_helper_bands_hz']
        self.assertEqual(len(helper_edges), 3)
        # Preserve the signed two-ray/Each65 union: D90 lines reach −90 and
        # +180 Hz, not a symmetric ±180-Hz overestimate on the high pilot.
        self.assertAlmostEqual(helper_edges[1][0], 2400. - 1725. - 90. - .5, delta=.001)
        self.assertAlmostEqual(helper_edges[1][1], 2400. - 1725. + 180. + .5, delta=.001)
        self.assertGreater(min(abs(edge) for band in helper_edges for edge in band),
                           comparison.frontend_support['stop_edge_hz'])
        options = self.factory.call_args.kwargs
        self.assertEqual(options['sample_rate'], 6000)
        self.assertEqual(options['output_sample_rate'], 750)
        self.assertEqual(options['frontend_profile'], 'rate-aware')
        self.assertEqual(options['pass_edge_hz'], comparison.frontend_support['pass_edge_hz'])
        self.assertEqual(options['stop_edge_hz'], comparison.frontend_support['stop_edge_hz'])
        self.assertEqual(options['source_offset_seconds'], 38.)
        self.assertFalse(comparison.frontend_support['compact_spectral_support'])

    def test_rate_aware_frontend_rejects_band_beyond_input_passband(self):
        self.manifest['sample_rate_hz'] = 6000
        self.manifest['format']['occupied_bandwidth_hz'] = 5000.
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'exceeds rate-aware input passband'):
            study._frontend_support(self.manifest, 'rate-aware', (0.,), (1.,))

    def test_helper_band_does_not_scale_carrier_center_twice(self):
        bands = study._helper_bands_relative_to_data(self.manifest,
            audio_clock_bound_ppm=0., channel_scale=1.00015, cfo_bound_hz=0.)
        # Matched TX/RX clocks have source audio ratio a=1, even when both
        # clocks differ from ideal. A physical path sideband advances in true
        # seconds and therefore has a separate inverse-RX scale. Construct
        # sampled phase independently of the interval helper's arithmetic.
        raw_rate, inverse_rx = 18000., 1.00015
        tx_scale = rx_scale = 1 / inverse_rx
        physical_times = np.arange(2) / (raw_rate * rx_scale)
        phase = 2 * np.pi * (675. * tx_scale * physical_times - 90. * physical_times)
        tone = np.exp(1j * phase)
        measured_frequency = np.angle(tone[1] * tone[0].conjugate()) * raw_rate / (2 * np.pi)
        self.assertAlmostEqual(measured_frequency, bands[1][0], delta=1e-10)
        self.assertAlmostEqual(bands[1][0], 675. - 90. * inverse_rx,
                               delta=1e-10)
        self.assertAlmostEqual(bands[1][1], 675. + 180. * inverse_rx,
                               delta=1e-10)
        old_incorrect_lower = 675. * inverse_rx - 90. * inverse_rx
        self.assertGreater(old_incorrect_lower, measured_frequency + .1)

    def test_helper_bands_cover_independent_source_and_physical_clock_extrema(self):
        q_bound, inverse_rx_bound, cfo_bound = 300., 1.00015, .7
        bands = study._helper_bands_relative_to_data(self.manifest,
            audio_clock_bound_ppm=q_bound, channel_scale=inverse_rx_bound,
            cfo_bound_hz=cfo_bound)
        physical_offsets = (-90., -9., 0., 9., 81., 90., 99., 180.)
        for index, center in enumerate((-1175., 675., -775.)):
            for q in (-q_bound, 0., q_bound):
                for inverse_rx in (.99985, 1., inverse_rx_bound):
                    tx_scale = (1 + q * 1e-6) / inverse_rx
                    rx_scale = 1 / inverse_rx
                    physical_times = np.arange(2) / (18000. * rx_scale)
                    for offset in physical_offsets:
                        for cfo in (-cfo_bound, cfo_bound):
                            # The auxiliary nominal envelope is source-timed,
                            # unlike Doppler and selective channel modulation.
                            for auxiliary_offset in ((-200., 200.) if index == 2 else (0.,)):
                                phase = 2 * np.pi * ((center + auxiliary_offset) * tx_scale * physical_times +
                                    offset * physical_times + cfo * np.arange(2) / 18000.)
                                waveform = np.exp(1j * phase)
                                observed = np.angle(waveform[1] * waveform[0].conjugate()) * 18000 / (2 * np.pi)
                                self.assertGreaterEqual(observed, bands[index][0] - 1e-10)
                                self.assertLessEqual(observed, bands[index][1] + 1e-10)

    def test_rate_aware_frontend_rejects_data_band_that_reaches_helper_line(self):
        self.manifest['sample_rate_hz'] = 6000
        self.manifest['format']['occupied_bandwidth_hz'] = 1000.
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'overlaps a declared Helper'):
            study._frontend_support(self.manifest, 'rate-aware', (0.,), (1.,))

    def test_templates_at_different_raw_sample_rate_are_rejected(self):
        template_folder = self.folder / 'other-rate-templates'
        template_folder.mkdir()
        (template_folder / 'templates.c128').write_bytes((self.folder / 'templates.c128').read_bytes())
        template_manifest = deepcopy(self.manifest)
        template_manifest['sample_rate_hz'] = 18000
        (template_folder / 'manifest.json').write_text(json.dumps(template_manifest))
        with self.assertRaisesRegex(ValueError, 'template source mismatch: rate'):
            study.HelperComparison(self.folder, template_folder)
        self.factory.assert_not_called()

    def test_selected_modulation_sidebands_are_included_in_support_guard(self):
        self.manifest['channel']['spread_doppler_hz'] = 110.
        self.manifest['channel']['selective_slow_depth'] = 0.
        self.manifest['channel']['selective_fast_depth'] = 0.
        self.write_manifest()
        study.HelperComparison(self.folder)
        self.factory.reset_mock()
        self.manifest['channel']['selective_fast_depth'] = .65
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'unsupported DATA frontend geometry'):
            study.HelperComparison(self.folder)
        self.factory.assert_not_called()

    def test_support_guard_keeps_full_hf_clock_bank_and_rejects_dilation_overflow(self):
        self.manifest['channel'].update(deterministic_cfo_bound_hz=90.5220261,
            sample_clock_pair_ppm=300.015, endpoint_uniform_half_bound_ppm=150.)
        self.write_manifest()
        comparison = study.HelperComparison(self.folder, fft_backend='batched', frontend_profile='wide',
            clock_ppm_values=(-300.015, 0., 300.015),
            doppler_scale_values=(.99985, 1., 1.00015))
        self.assertLess(comparison.frontend_support['required_nominal_edge_hz'], 400.)
        self.assertGreater(comparison.frontend_support['required_nominal_edge_hz'], 383.)
        self.factory.reset_mock()
        self.manifest['format']['occupied_bandwidth_hz'] = 600.
        self.manifest['channel'].update(deterministic_cfo_bound_hz=.5,
            spread_doppler_hz=0., sample_clock_pair_ppm=0., endpoint_uniform_half_bound_ppm=0.)
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'unsupported DATA frontend geometry'):
            study.HelperComparison(self.folder, fft_backend='batched', clock_ppm_values=(100000.,))
        self.factory.assert_not_called()

    def test_support_guard_rejects_invalid_public_geometry(self):
        self.manifest['channel']['spread_margin'] = 0.
        with self.assertRaisesRegex(ValueError, 'finite positive public DATA frontend geometry'):
            study._frontend_support(self.manifest, 'legacy', (0.,), (1.,))
        self.manifest['channel']['spread_margin'] = 1.
        for invalid in (float('nan'), 0., -1.):
            self.manifest['format']['occupied_bandwidth_hz'] = invalid
            with self.assertRaisesRegex(ValueError, 'finite positive public DATA frontend geometry'):
                study._frontend_support(self.manifest, 'legacy', (0.,), (1.,))


if __name__ == '__main__':
    unittest.main()

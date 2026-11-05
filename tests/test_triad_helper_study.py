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
            format=dict(physical_rf_retuning=False, requested_bit_seconds=16., bit_seconds=20.),
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
            with self.assertRaisesRegex(ValueError, 'complete observation required'):
                comparison.score(self.folder)
        np.ones(18000, dtype='u1').tofile(valid_path)
        np.zeros(17999, dtype='<c16').tofile(self.folder / 'observation.c128')
        with self.assertRaisesRegex(ValueError, 'capture file size mismatch'):
            comparison.score(self.folder)
        self.estimator.assert_not_called()
        self.receiver.prepare_observation.assert_not_called()

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


if __name__ == '__main__':
    unittest.main()

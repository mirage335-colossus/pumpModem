#!/usr/bin/env python3
"""Cheap input/coverage checks for the sampled Helper frequency study CLI."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_dynamic_frequency_study as study
from triad_dynamic_frequency_study import frequency_bank, interval_contains, load_schedule


class HelperFrequencyStudyTests(unittest.TestCase):
    def manifest(self, bound=1.01, margin=3.):
        return {'channel': {'deterministic_cfo_bound_hz': bound, 'spread_margin': margin}}

    def test_bank_is_symmetric_and_covers_public_prior(self):
        bank, half = frequency_bank(self.manifest(), .5)
        self.assertEqual(len(bank), 15)
        self.assertEqual(bank[0], -half)
        self.assertEqual(bank[-1], half)
        self.assertEqual(bank[len(bank)//2], 0.)
        self.assertTrue(all(a < b for a, b in zip(bank, bank[1:])))
        self.assertGreaterEqual(max(abs(value) for value in bank), half)

    def test_zero_prior_retains_one_hypothesis(self):
        self.assertEqual(frequency_bank(self.manifest(0.), .5), ((0.,), 0.))

    def test_bank_refuses_excessive_search_before_allocation(self):
        with self.assertRaisesRegex(ValueError, 'safety cap'):
            frequency_bank(self.manifest(100_000.), .1)

    def test_interval_union_contains_endpoints_only_when_included(self):
        regions = [[-2., -1.], [1., 3.]]
        self.assertTrue(interval_contains(regions, -2.))
        self.assertTrue(interval_contains(regions, 3.))
        self.assertFalse(interval_contains(regions, 0.))

    def test_schedule_requires_unique_phase_tagged_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'schedule.json'
            path.write_text(json.dumps([
                {'id': 'cal-0', 'directory': '/tmp/cal-0', 'phase': 'calibration'},
                {'id': 'sig-0', 'directory': '/tmp/sig-0', 'phase': 'signal'}]))
            self.assertEqual([row['phase'] for row in load_schedule(path)], ['calibration', 'signal'])
            path.write_text(json.dumps([
                {'id': 'x', 'directory': '/tmp/a', 'phase': 'null'},
                {'id': 'x', 'directory': '/tmp/b', 'phase': 'signal'}]))
            with self.assertRaisesRegex(ValueError, 'unique'):
                load_schedule(path)

    def test_calibrated_cli_scores_all_arms_before_opening_evaluator_truth(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entries = []
            expected_bits = {'cal': 0, 'signal': 0, 'null': None}
            manifest = dict(schema='fake-v1', sample_rate_hz=8., format={}, calendar={},
                helper_plan={}, channel={'deterministic_cfo_bound_hz': .4,
                                         'spread_margin': 1., 'rf_hz': 3.,
                                         'nominal_conversion_if_hz': 1.},
                power={}, helper_enabled=True, public_epoch_seconds=1., key_seed=7,
                helper_component_control='full', evaluator_truth_file='truth.json')
            common = study.public_geometry(manifest)
            raw = np.ones(16, dtype=np.complex128)
            valid = np.ones(16, dtype=np.uint8)
            calls = []
            def load_capture(directory, expected=None):
                if expected is not None:
                    self.assertEqual(expected, common)
                return manifest, raw, valid, dict(input_sha256='x', validity_sha256='v', manifest_sha256='m')
            def evaluator(directory, current_manifest):
                self.assertEqual(calls, ['adaptive', 'baseline'])
                trial_id = Path(directory).name
                return dict(source_bit=expected_bits[trial_id])
            def fake_truth(directory, cfo):
                return dict(actual_cfo_hz_after_score=0., source_bit=expected_bits[Path(directory).name])
            def make_result(identifier, assisted):
                bit_scores = np.tile(np.array([[float(1 if assisted else 0), 0.]]), (3, 1))
                return dict(procedure_id=identifier, bit_scores=bit_scores,
                    frequency_assistance=[dict(status='narrowed' if assisted else 'fallback',
                        selected_frequencies=[0.], expanded_intervals_hz=[[-.1, .1]]) for _ in range(3)],
                    phase_assistance={'effective_pilot_use': 'none'},
                    work=dict(candidates_per_trial=[1]*3, frequencies_per_trial=[1]*3,
                              selected_frequency_banks=[[0.]]*3, total_candidate_trial_evaluations=3,
                              original_candidates=3, projection_sha256='p'),
                    timing={'total_score_call_seconds': 1.})
            class FakeAdaptive:
                def __init__(self, *args, **kwargs):
                    self.procedure_id = 'adaptive-policy'
                    self.policy = {'test': True}
                    self.planning_seconds = .01
                def score(self, *args, **kwargs):
                    calls.append('adaptive')
                    result = make_result('adaptive-result', True)
                    result['frequency_assistance'][1]['status'] = 'fallback'
                    return result
            class FakeBaseline:
                def __init__(self, *args, **kwargs):
                    self.procedure_id = 'baseline-policy'
                    self.planning_seconds = .01
                def score(self, *args, **kwargs):
                    calls.append('baseline')
                    return make_result('baseline-result', False)
            schedule = root/'schedule.json'
            schedule.write_text(json.dumps([
                {'id': name, 'directory': str(root/name), 'phase': phase}
                for name, phase in [('cal','calibration'), ('signal','signal'), ('null','null')]]))
            for name in expected_bits:
                folder = root/name
                folder.mkdir()
                (folder/'truth.json').write_text(json.dumps(dict(
                    tx={'rf_ppm': 0., 'audio_ppm': 0.}, rx={'rf_ppm': 0., 'audio_ppm': 0.},
                    source_bit=expected_bits[name])))
            out = root/'out'
            with mock.patch.multiple(study,
                    implementation_hashes=mock.DEFAULT,
                    public_geometry=mock.DEFAULT,
                    load_capture=mock.DEFAULT,
                    read_templates=mock.DEFAULT,
                    evaluator_metrics=mock.DEFAULT,
                    HelperFrequencyAcquisition=mock.DEFAULT,
                    TiledAcquisition=mock.DEFAULT,
                    write_json=mock.DEFAULT):
                study.implementation_hashes.return_value = {'source': 'hash'}
                study.public_geometry.side_effect = lambda m: common
                study.load_capture.side_effect = load_capture
                study.read_templates.return_value = (np.ones((1,2,4), np.complex128), (0,),
                    {'template_sha256':'t', 'template_manifest_sha256':'tm'})
                study.evaluator_metrics.side_effect = evaluator
                study.HelperFrequencyAcquisition.side_effect = FakeAdaptive
                study.TiledAcquisition.side_effect = FakeBaseline
                # Preserve real file output so the CLI's artifact flow is checked.
                study.write_json.side_effect = lambda path, value: Path(path).write_text(json.dumps(value, default=str))
                with mock.patch('sys.stdout'):
                    result = study.run(['--schedule', str(schedule), '--templates', str(root/'templates'),
                        '--output', str(out), '--alpha', '.5', '--frequency-step', '.5'])
            self.assertEqual(calls, ['adaptive', 'baseline'])
            self.assertEqual(result['admission_status'], 'empirical calibration gate applied')
            self.assertTrue((out/'protocol.json').exists())
            self.assertTrue((out/'rows.json').exists())
            self.assertEqual(result['no_helper_narrowing'], 1)


if __name__ == '__main__':
    unittest.main()

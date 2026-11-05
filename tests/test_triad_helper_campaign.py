#!/usr/bin/env python3
"""Cheap statistical/orchestration contracts; no sampled campaign is run here."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_helper_campaign import (binomial_interval, decide, evaluator_metrics,
                                   freeze_gate, outcome_counts, schedule, write_json)
from triad_helper_statistics import calibration_condition_id


def score(value, bit=0, procedure='whole-adaptive-rule'):
    return dict(procedure_id=procedure,
                calibration_condition_id=calibration_condition_id(procedure, True),
                baseline=dict(working_presence=value, bit=bit, receiver_id='baseline-bank'),
                assisted=dict(working_presence=value + 1, bit=bit, receiver_id='selected-bank'))


class CampaignTests(unittest.TestCase):
    def test_schedule_is_disjoint_fixed_and_balanced(self):
        rows = schedule(103, 19, 8, 8)
        self.assertEqual(len({r['seed'] for r in rows}), 35)
        self.assertEqual(rows, schedule(103, 19, 8, 8))
        self.assertEqual([r['bit'] for r in rows if r['phase'] == 'signal'], [0, 1] * 4)
        self.assertTrue(all(r['data'] == (r['phase'] == 'signal') for r in rows))
        with self.assertRaises(ValueError):
            schedule(2**64 - 1, 2, 0, 0)

    def test_rank_gate_ties_and_complete_procedure_identity(self):
        gate = freeze_gate([score(i) for i in range(19)], 'whole-adaptive-rule', .05)
        self.assertEqual(gate['minimum_p'], .05)
        self.assertFalse(decide(score(18), gate, 'baseline')['accepted'])
        self.assertTrue(decide(score(19), gate, 'baseline')['accepted'])
        self.assertTrue(decide(score(19), gate, 'assisted')['accepted'])
        changed_bank = score(19)
        changed_bank['assisted']['receiver_id'] = 'another-data-dependent-bank'
        self.assertTrue(decide(changed_bank, gate, 'assisted')['accepted'])
        with self.assertRaisesRegex(ValueError, 'identity'):
            decide(score(19, procedure='changed-algorithm'), gate, 'assisted')
        with self.assertRaisesRegex(ValueError, 'identity'):
            freeze_gate([score(1, procedure='wrong')], 'whole-adaptive-rule', .05)

    def test_gate_cannot_cross_helper_conditions(self):
        gate = freeze_gate([score(i) for i in range(19)], 'whole-adaptive-rule', .05)
        absent = score(19)
        absent['calibration_condition_id'] = calibration_condition_id(absent['procedure_id'], False)
        with self.assertRaisesRegex(ValueError, 'condition identity'):
            decide(absent, gate, 'assisted')
        with self.assertRaisesRegex(ValueError, 'condition identity'):
            freeze_gate([score(0), absent], 'whole-adaptive-rule', .05)

    def test_small_calibration_cannot_reach_smaller_alpha(self):
        gate = freeze_gate([score(0)], 'whole-adaptive-rule', .05)
        self.assertFalse(decide(score(1e6), gate, 'baseline')['accepted'])

    def test_intervals_include_endpoints_and_zero_trial_is_unqualified(self):
        zero = binomial_interval(0, 8)
        self.assertEqual(zero['two_sided'][0], 0)
        self.assertAlmostEqual(zero['one_sided_upper'], 1 - .05**(1 / 8), places=12)
        all_success = binomial_interval(8, 8)
        self.assertEqual(all_success['two_sided'][1], 1)
        self.assertAlmostEqual(all_success['two_sided'][0], .025**(1 / 8), places=12)
        self.assertIsNone(binomial_interval(0, 0)['two_sided'])

    def test_outcomes_keep_errors_misses_and_false_admissions_separate(self):
        rows = [dict(trial=dict(phase=phase), evaluator=dict(source_bit=truth),
                     decisions=dict(baseline=dict(accepted=accepted, bit=bit)))
                for phase, accepted, bit, truth in (
                    ('signal', True, 0, 0), ('signal', True, 0, 1),
                    ('signal', False, 1, 1), ('null', True, 0, 0), ('null', False, 1, 0))]
        rows.insert(0, dict(trial=dict(phase='calibration')))
        result = outcome_counts(rows, 'baseline')
        self.assertEqual(result['signal_trials'], 3)
        self.assertEqual(result['null_trials'], 2)
        self.assertEqual(result['counts'], dict(correct_admitted=1, wrong_admitted=1,
            missed=1, false_admitted=1, rejected_null=1, raw_bit_errors=1))
        self.assertNotIn('correct_admitted', result['intervals'])
        self.assertEqual(result['signal_uncertainty']['per_bit']['0']['trials'], 1)
        self.assertEqual(result['signal_uncertainty']['per_bit']['1']['trials'], 2)

    def test_evaluator_does_not_change_decisions(self):
        result = score(7)
        result['baseline']['best_group_public_parameters'] = dict(frequency_hz=.2)
        result['assistance'] = dict(selected_frequencies=[.1, .2, .3], expanded_intervals_hz=[[.1, .3]])
        before = json.dumps(result, sort_keys=True)
        truth = dict(tx=dict(audio_ppm=0., rf_ppm=.2), rx=dict(audio_ppm=0., rf_ppm=0.),
                     source_bit=0, arrival_seconds=.01, source_energy={}, channel_report={})
        manifest = dict(channel=dict(rf_hz=1e6, nominal_conversion_if_hz=1725.))
        diagnostic = evaluator_metrics(truth, manifest, result)
        self.assertTrue(diagnostic['baseline_best_cfo_retained'])
        self.assertTrue(diagnostic['static_cfo_in_expanded_interval'])
        self.assertAlmostEqual(diagnostic['nominal_static_cfo_hz'], .2)
        self.assertEqual(before, json.dumps(result, sort_keys=True))

    def test_frozen_output_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'gate.json'
            write_json(path, dict(frozen=True))
            with self.assertRaises(FileExistsError):
                write_json(path, dict(frozen=False))
            self.assertEqual(json.loads(path.read_text()), dict(frozen=True))


if __name__ == '__main__':
    unittest.main()

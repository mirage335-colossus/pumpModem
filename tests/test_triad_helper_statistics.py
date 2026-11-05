#!/usr/bin/env python3
"""Reporting-only checks: no receiver or sampled simulation is imported."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_helper_statistics import (balanced_signal_statistics, binomial_interval,
                                     calibration_condition_id, signal_outcome_statistics)


def counts(correct, wrong=0, missed=0, raw_errors=None):
    return dict(correct_admitted=correct, wrong_admitted=wrong, missed=missed,
                raw_bit_errors=wrong if raw_errors is None else raw_errors)


class HelperStatisticsTests(unittest.TestCase):
    def test_extremes_and_bonferroni_component_confidence(self):
        result = balanced_signal_statistics({0: counts(4), 1: counts(4)}, {0: 4, 1: 4})
        correct = result['balanced_population']['correct_admitted']
        self.assertTrue(correct['qualified'])
        self.assertAlmostEqual(correct['component_confidence'], .975)
        self.assertEqual(correct['fraction'], 1.)
        self.assertEqual(correct['two_sided'][1], 1.)
        self.assertAlmostEqual(correct['two_sided'][0], .0125**.25)
        false = result['balanced_population']['wrong_admitted']
        self.assertEqual(false['two_sided'][0], 0.)
        self.assertAlmostEqual(false['two_sided'][1], 1 - .0125**.25)
        self.assertAlmostEqual(false['one_sided_upper'], 1 - .025**.25)
        self.assertEqual(result['per_bit']['0']['intervals']['correct_admitted']['confidence'], .95)

    def test_unequal_counts_keep_balanced_population_weights(self):
        result = balanced_signal_statistics({0: counts(1), 1: counts(0, missed=9)}, {0: 1, 1: 9})
        correct = result['balanced_population']['correct_admitted']
        self.assertEqual(correct['fraction'], .5)
        self.assertEqual(result['pooled_observed_fractions']['correct_admitted'], .1)
        self.assertEqual(correct['weights'], [.5, .5])
        expected = [binomial_interval(k, n, .975) for k, n in ((1, 1), (0, 9))]
        for side in (0, 1):
            self.assertAlmostEqual(correct['two_sided'][side], sum(x['two_sided'][side] for x in expected) / 2)

    def test_missing_bit_is_explicitly_unqualified(self):
        result = balanced_signal_statistics({0: counts(4)}, {0: 4})
        for interval in result['balanced_population'].values():
            self.assertFalse(interval['qualified'])
            self.assertIsNone(interval['fraction'])
            self.assertEqual(interval['two_sided'], [0., 1.])
            self.assertEqual(interval['one_sided_upper'], 1.)
        self.assertEqual(result['per_bit']['1']['intervals']['missed']['two_sided'], [0., 1.])
        self.assertFalse(result['per_bit']['1']['intervals']['missed']['qualified'])
        self.assertEqual(result['signal_trials'], 4)

    def test_adapter_preserves_outcomes_and_ignores_null_calibration(self):
        rows = [dict(trial=dict(phase=phase), evaluator=dict(source_bit=bit),
                     decisions=dict(assisted=dict(accepted=accepted, bit=decoded)))
                for phase, bit, accepted, decoded in (
                    ('signal', 0, True, 0), ('signal', 0, False, 1),
                    ('signal', 1, True, 0), ('signal', 1, False, 1),
                    ('null', 0, True, 1), ('calibration', 1, False, 1))]
        original = deepcopy(rows)
        result = signal_outcome_statistics(rows, 'assisted')
        self.assertEqual(rows, original)
        self.assertEqual(result['counts'], counts(1, wrong=1, missed=2, raw_errors=2))
        self.assertEqual(result['per_bit']['0']['counts'], counts(1, missed=1, raw_errors=1))
        self.assertEqual(result['per_bit']['1']['counts'], counts(0, wrong=1, missed=1, raw_errors=1))

    def test_empty_population(self):
        result = signal_outcome_statistics([], 'baseline')
        self.assertEqual(result['signal_trials'], 0)
        self.assertFalse(result['balanced_population']['correct_admitted']['qualified'])
        self.assertIsNone(result['pooled_observed_fractions']['correct_admitted'])

    def test_bad_counts_nonfinite_inputs_and_partition_rejected(self):
        for value in (float('nan'), float('inf'), -1, 1.5, True):
            with self.assertRaises(ValueError):
                binomial_interval(value, 4)
        for value in (float('nan'), float('inf'), 0, 1, True):
            with self.assertRaises(ValueError):
                binomial_interval(0, 4, value)
        with self.assertRaises(ValueError):
            balanced_signal_statistics({0: counts(2)}, {0: 1})
        with self.assertRaises(ValueError):
            balanced_signal_statistics({0: counts(0)}, {0: 1})
        with self.assertRaises(ValueError):
            balanced_signal_statistics({0: counts(1, raw_errors=1)}, {0: 1})
        with self.assertRaises(ValueError):
            balanced_signal_statistics({2: counts(1)}, {2: 1})

    def test_calibration_condition_is_deterministic_and_helper_specific(self):
        on = calibration_condition_id('frozen-procedure', True)
        self.assertEqual(on, calibration_condition_id('frozen-procedure', True))
        self.assertEqual(len(on), 64)
        self.assertNotEqual(on, calibration_condition_id('frozen-procedure', False))
        self.assertNotEqual(on, calibration_condition_id('changed-procedure', True))
        for procedure, enabled in (('', True), (float('nan'), True), ('valid', 1), ('valid', float('nan'))):
            with self.assertRaises(ValueError):
                calibration_condition_id(procedure, enabled)


if __name__ == '__main__':
    unittest.main()

import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_perf_adaptive import AdaptivePolicy, regression_profile
from triad_perf_decision_controls import NoiseModel
from triad_perf_decision_policy_controls import (FACTORS, POLICY, decide, default_factors,
    deterministic_scope_controls, main, policy_for_profile, simulate_factor)


class DecisionStoppingControlTests(unittest.TestCase):
    def test_profiles_match_maintained_directional_policies(self):
        self.assertIs(policy_for_profile('strict'), POLICY)
        self.assertEqual(default_factors(POLICY), list(FACTORS))
        for name in ('normal', 'ci', 'strict'):
            policy = policy_for_profile(name)
            expected = regression_profile(name)
            self.assertEqual(policy.pass_error_budget, expected['pass_alpha'])
            self.assertEqual(policy.failure_error_budget, expected['failure_alpha'])
            self.assertEqual(policy.slowdown_threshold, expected['slowdown_threshold'])
        self.assertEqual(default_factors(policy_for_profile('ci')), [1., 1.69, 1.70, 1.71, 2.])

    def test_profile_changes_real_helper_gate_and_directional_metadata(self):
        reference = np.ones(33)
        candidate = np.full(33, 1.6)
        normal = decide(reference, candidate, policy_for_profile('normal'))
        ci = decide(reference, candidate, policy_for_profile('ci'))
        self.assertEqual(normal['performance_gate'], 'performance_regression')
        self.assertEqual(ci['performance_gate'], 'pass')
        self.assertEqual(normal['policy']['pass_alpha'], .15)
        self.assertEqual(ci['policy']['pass_alpha'], .30)
        self.assertEqual(ci['policy']['failure_alpha'], .05)

    def test_decision_stops_before_unnecessary_precision(self):
        reference = np.exp(np.linspace(-.8, .8, 33))
        row = decide(reference, reference)
        self.assertEqual(row['sample_count'], 5)
        self.assertEqual(row['performance_gate'], 'pass')
        self.assertEqual(row['stop_reason'], 'regression_decision_reached')
        self.assertFalse(row['timing_precision_reached'])

    def test_no_family_multiplicity_in_decision_gate(self):
        ref = np.exp(np.linspace(-.08, .08, 33))
        candidate = ref * np.exp(.02 * np.cos(np.arange(33))) * 1.2
        one = decide(ref, candidate)
        many = decide(ref, candidate, AdaptivePolicy(stopping_rule='decision', family_count=999))
        self.assertEqual(one['sample_count'], many['sample_count'])
        self.assertEqual(one['performance_gate'], many['performance_gate'])
        self.assertEqual(one['paired_candidate_over_reference']['model_adjusted_gate_interval'],
                         many['paired_candidate_over_reference']['model_adjusted_gate_interval'])

    def test_first_regression_proof_stops(self):
        row = decide(np.ones(33), np.full(33, 1.6))
        self.assertEqual(row['sample_count'], 5)
        self.assertEqual(row['performance_gate'], 'performance_regression')

    def test_exact_allowed_boundary_is_not_a_roundoff_regression(self):
        for ref in (np.ones(33), np.exp(np.linspace(-.8, .8, 33)), np.linspace(1., 2., 33)):
            row = decide(ref, ref * POLICY.slowdown_threshold)
            self.assertEqual(row['performance_gate'], 'pass')
            self.assertLessEqual(row['sample_count'], 33)

    def test_old_precision_policy_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'decision stopping'):
            decide(np.ones(33), np.ones(33), AdaptivePolicy())

    def test_seeded_near_threshold_counts_are_repeatable(self):
        kwargs = dict(trials=12, seed=3107, model=NoiseModel(log_sigma=.04))
        self.assertEqual(simulate_factor(1.31, **kwargs), simulate_factor(1.31, **kwargs))
        low = simulate_factor(1.29, **kwargs)
        high = simulate_factor(1.31, **kwargs)
        self.assertTrue(low['false_regression_applicable'])
        self.assertFalse(low['false_pass_applicable'])
        self.assertTrue(high['false_pass_applicable'])
        self.assertEqual(sum(high['stopping_sample_count'].values()), 12)
        self.assertEqual(sum(high['outcomes'][key]['count'] for key in
                             ('pass', 'performance_regression', 'inconclusive')), 12)

    def test_scope_controls_do_not_claim_universal_prediction(self):
        rows = deterministic_scope_controls()
        self.assertEqual(rows['outside_receiver']['complete_timer_gate'], 'performance_regression')
        self.assertEqual(rows['outside_receiver']['incorrect_receiver_only_gate'], 'pass')
        self.assertEqual(rows['full_size_only']['abbreviated_gate'], 'pass')
        self.assertTrue(rows['full_size_only']['calibration_blocking'])
        self.assertFalse(rows['changed_regime']['applicability_accepted'])
        self.assertEqual(rows['common_environment']['paired_gate'], 'pass')
        self.assertTrue(rows['common_environment']['hardware_screen']['result']['warning'])
        self.assertTrue(rows['not_actual_receiver_or_hardware_runs'])

    def test_ci_scope_injection_exceeds_its_own_threshold(self):
        rows = deterministic_scope_controls(policy_for_profile('ci'))
        self.assertEqual(rows['outside_receiver']['complete_timer_gate'], 'performance_regression')
        self.assertEqual(rows['outside_receiver']['injected_external_fraction'], 1.)
        self.assertEqual(rows['full_size_only']['full_target_ratio'], 2.)
        self.assertTrue(rows['full_size_only']['calibration_blocking'])

    def test_frozen_cli_plan_and_results_are_separate_from_legacy(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / 'new.json'
            self.assertEqual(main(['--output', str(output), '--trials', '3', '--factor', '1']), 0)
            row = json.loads(output.read_text())
            self.assertEqual(row['plan']['policy']['stopping_rule'], 'decision')
            self.assertEqual(row['plan']['regression_profile'], 'strict')
            self.assertTrue(row['plan']['timing_precision_is_not_a_stop_requirement'])
            self.assertEqual(row['status'], 'complete')
            self.assertEqual(row['pending_factors'], [])
            self.assertTrue(output.with_suffix('.plan.json').exists())

    def test_cli_freezes_selected_profile_and_threshold_factors(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / 'ci.json'
            self.assertEqual(main(['--output', str(output), '--trials', '3',
                                   '--regression-profile', 'ci']), 0)
            row = json.loads(output.read_text())
            self.assertEqual(row['plan']['regression_profile'], 'ci')
            self.assertEqual(row['plan']['directional_error_budgets'], {'pass': .30, 'failure': .05})
            self.assertEqual(row['plan']['factors'], [1., 1.69, 1.70, 1.71, 2.])
            self.assertEqual(row['plan']['policy']['slowdown_threshold'], 1.70)
            self.assertEqual(row['status'], 'complete')


if __name__ == '__main__':
    unittest.main()

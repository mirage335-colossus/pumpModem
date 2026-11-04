import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr, redirect_stdout
import io

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_perf_adaptive import AdaptivePolicy
from triad_perf_decision_controls import (DelayLedger, DelayRule, NoiseModel, binomial_interval,
    conditional_decision, environment_screen, policy_decision, read_residuals, run_control,
    sample_logs, main)


class DecisionControlsTests(unittest.TestCase):
    def test_exact_binomial_zero_does_not_mean_zero_failure_probability(self):
        result = binomial_interval(0, 100)
        self.assertEqual(result['interval'][0], 0.)
        self.assertAlmostEqual(result['interval'][1], 1 - .025 ** .01)
        self.assertGreater(result['interval'][1], .03)
        with self.assertRaises(ValueError):
            binomial_interval(2, 1)

    def test_actual_fixed_look_gate_and_near_threshold_inconclusive(self):
        policy = AdaptivePolicy(looks=(5, 9), family_count=14)
        ref = np.ones(9)
        self.assertEqual(policy_decision(ref, ref * 1.2, policy)['performance_gate'], 'pass')
        self.assertEqual(policy_decision(ref, ref * 1.6, policy)['performance_gate'], 'performance_regression')
        candidate = 1.31 * np.exp(np.array([-.07, .07, -.03, .03, 0., -.04, .04, -.02, .02]))
        row = policy_decision(ref, candidate, policy)
        self.assertEqual(row['performance_gate'], 'inconclusive')
        self.assertEqual(row['sample_count'], 9)

    def test_full_only_control_unchanged_training_but_real_guard_blocks(self):
        policy = AdaptivePolicy(family_count=14)
        values = [1.] * 33
        row = conditional_decision(values, values, {'reference': 1., 'candidate': 1.6}, policy)
        self.assertEqual(row['timing']['paired_candidate_over_reference']['point_seconds'], 1.)
        self.assertTrue(row['calibration_blocking'])
        self.assertFalse(row['applicability_accepted'])
        self.assertEqual(row['gate'], 'inconclusive')

    def test_changed_regime_rejected_by_actual_conditional_validator(self):
        row = conditional_decision([1.] * 33, [1.] * 33,
            {'reference': 1., 'candidate': 1.}, AdaptivePolicy(), changed_regime=True)
        self.assertFalse(row['applicability_accepted'])
        self.assertEqual(row['sentinel_status'], 'unvalidated_request_focused_qualification')
        self.assertFalse(row['calibration_blocking'])  # not evidence of a mean slowdown

    def test_conditional_allowance_is_separate_from_direct_gate(self):
        values = [1.] * 33
        row = conditional_decision(values, [1.31] * 33,
                                  {'reference': 1., 'candidate': 1.31}, AdaptivePolicy())
        self.assertTrue(row['applicability_accepted'])
        self.assertEqual(row['gate'], 'inconclusive')

    def test_environment_cancels_ratio_but_actual_health_screen_warns(self):
        row = run_control(NoiseModel(log_sigma=0), factor=1.6, mode='environment',
                          campaigns=3, policy=AdaptivePolicy(family_count=2), seed=87)
        self.assertEqual(row['any_case']['performance_failure']['count'], 0)
        self.assertTrue(environment_screen(1.6)['result']['warning'])
        self.assertFalse(environment_screen(1.2)['result']['warning'])

    def test_outside_score_delay_is_in_full_time_gate(self):
        row = run_control(NoiseModel(log_sigma=0), factor=1.6, mode='outside-score',
                          campaigns=3, policy=AdaptivePolicy(family_count=2), seed=87)
        self.assertEqual(row['any_case']['performance_failure']['count'], 3)

    def test_seeded_campaign_is_repeatable_and_counts_campaigns_not_cases(self):
        args = dict(factor=1.31, mode='direct', campaigns=4,
                    policy=AdaptivePolicy(family_count=3, looks=(5, 9)), seed=877)
        a = run_control(NoiseModel(log_sigma=.04), **args)
        b = run_control(NoiseModel(log_sigma=.04), **args)
        self.assertEqual(a, b)
        self.assertEqual(a['any_case']['performance_failure']['campaigns'], 4)
        self.assertEqual(len(a['per_case']), 3)

    def test_residual_import_binds_source_and_never_counts_inner_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            path.write_text(json.dumps({'status': 'warning', 'exit_code': 0, 'complete_required_coverage': True,
                'elapsed_seconds': 1., 'harness_sha256': {'source': 'hash'}, 'candidate': {'commit': 'source'},
                'cases': [{'id': 'one', 'predicted_full_seconds': {'reference': [1, 2, 3, 4, 5],
                    'candidate': [2, 4, 6, 8, 10]}, 'prediction_valid': False}]}))
            rows, meta = read_residuals(path)
            self.assertEqual(rows[0]['outer_count'], 5)
            np.testing.assert_allclose(rows[0]['logs'].mean(axis=0), 0., atol=1e-14)
            np.testing.assert_allclose(rows[0]['logs'][:, 0], rows[0]['logs'][:, 1], atol=1e-14)
            self.assertEqual(len(meta['sha256']), 64)
            self.assertFalse(rows[0]['measured_model_valid'])
            self.assertEqual(meta['original_status'], 'warning')
            path.write_text(json.dumps({'status': 'running', 'cases': []}))
            with self.assertRaisesRegex(ValueError, 'completed'):
                read_residuals(path)

    def test_adjacent_block_sampling_preserves_order_without_wrap(self):
        rows = np.array([[0, 0], [1, 1], [10, 10], [11, 11], [100, 100]], float)
        a = sample_logs(np.random.default_rng(4), 8,
                        NoiseModel(kind='empirical-block', block_length=2), rows)
        np.testing.assert_allclose(a[1::2] - a[::2], 1.)
        self.assertLess(abs(a).max(), 6.)  # unmatched tail never joins first block

    def test_delay_wrapper_preserves_return_value_and_records_outer_cost(self):
        class Timer:
            now = 0.
            def clock(self): return self.now
            def sleep(self, seconds): self.now += seconds
        timer = Timer()
        ledger = DelayLedger({'prepare': DelayRule(multiplier=1.6)},
                             clock=timer.clock, sleep=timer.sleep, maximum_delay_seconds=2.)
        value = object()
        def operation():
            timer.now += 1.
            return value
        self.assertIs(ledger.call('prepare', operation), value)
        report = ledger.report()
        self.assertAlmostEqual(report['calls'][0]['complete_wrapped_seconds'], 1.6)
        self.assertAlmostEqual(report['actual_total_seconds'], .6)
        self.assertEqual(report['plan_sha256'], ledger.plan_sha256)
        self.assertFalse(report['frozen_plan']['returned_values_modified'])

    def test_wall_budget_marks_unfinished_control_without_fixed_n_claim(self):
        row = run_control(NoiseModel(), factor=1.6, mode='direct', campaigns=4,
                          policy=AdaptivePolicy(family_count=2), seed=8, deadline=0.)
        self.assertEqual(row['status'], 'budget_exhausted')
        self.assertEqual(row['campaigns'], 0)
        self.assertEqual(row['requested_campaigns'], 4)
        self.assertIsNone(row['any_case']['performance_failure']['interval'])
        self.assertFalse(row['any_case']['performance_failure']['nominal_binomial_coverage_claim'])

    def test_checkpoint_keeps_first_finished_control_on_later_error(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'controls.json'
            first = {'status': 'complete', 'campaigns': 3, 'mode': 'direct', 'factor': 1.}
            with patch('triad_perf_decision_controls.run_control', side_effect=[first, RuntimeError('injected failure')]), redirect_stdout(io.StringIO()):
                code = main(['--output', str(output), '--campaigns', '3', '--families', '1',
                             '--mode', 'direct', '--factor', '1', '--factor', '1.6'])
            report = json.loads(output.read_text())
            self.assertEqual(code, 2)
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['controls'], [first])
            self.assertEqual(report['completed_control_count'], 1)
            self.assertEqual(len(report['pending_controls']), 1)
            self.assertEqual(report['error']['message'], 'injected failure')
            self.assertTrue(output.with_suffix('.json.plan.json').exists())

    def test_cli_budget_leaves_all_unexecuted_work_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'controls.json'
            code = main(['--output', str(output), '--campaigns', '3', '--families', '1',
                         '--mode', 'direct', '--factor', '1', '--budget-seconds', '0.0000000001'])
            report = json.loads(output.read_text())
            self.assertEqual(code, 2)
            self.assertEqual(report['status'], 'budget_exhausted')
            self.assertEqual(report['completed_control_count'], 0)
            self.assertEqual(len(report['pending_controls']), 1)
            before = output.read_bytes()
            with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
                main(['--output', str(output)])
            self.assertEqual(output.read_bytes(), before)

    def test_delay_budget_is_not_silently_truncated(self):
        ledger = DelayLedger({'load': DelayRule(extra_seconds=1.)}, maximum_delay_seconds=.1,
                             clock=lambda: 0., sleep=lambda seconds: None)
        with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
            ledger.call('load', lambda: 3)
        self.assertEqual(ledger.rows, [])


if __name__ == '__main__':
    unittest.main()

import math
import json
from dataclasses import FrozenInstanceError
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_perf_adaptive import (AdaptivePolicy, MODEL_CONFIRMATION_ALPHA,
                                 MODEL_CONFIRMATION_CONFIDENCE,
                                 MODEL_CONFIRMATION_FAMILY_ALPHA,
                                 MODEL_CONFIRMATION_LOOKS, MODEL_CONFIRMATION_PER_LOOK_ALPHA,
                                 MODEL_CONFIRMATION_POLICY, model_confirmation_policy,
                                 next_look, qualify_log_residuals, summarize)


class AdaptivePrecisionTests(unittest.TestCase):
    def test_model_confirmation_policy_is_frozen_and_json_reportable(self):
        policy = model_confirmation_policy()
        self.assertIs(policy, MODEL_CONFIRMATION_POLICY)
        self.assertEqual(policy.looks, (3, 5, 9, 17))
        self.assertEqual(MODEL_CONFIRMATION_LOOKS, policy.looks)
        self.assertAlmostEqual(MODEL_CONFIRMATION_FAMILY_ALPHA, .10)
        self.assertAlmostEqual(MODEL_CONFIRMATION_ALPHA, .10)
        self.assertAlmostEqual(MODEL_CONFIRMATION_PER_LOOK_ALPHA, .025)
        self.assertAlmostEqual(MODEL_CONFIRMATION_CONFIDENCE, .975)
        self.assertAlmostEqual(policy.confidence, .975)
        with self.assertRaises(FrozenInstanceError):
            policy.family_alpha = .05
        report = policy.to_dict()
        json.dumps(report)
        self.assertEqual(report["looks"], [3, 5, 9, 17])
        self.assertFalse(report["cross_metric_or_case_simultaneous_claim"])
        self.assertIn("one error family", report["scope"])

    def test_model_residual_equivalence_uses_frozen_prediction_actual_ratio(self):
        within = qualify_log_residuals([math.log(1.04)] * 5,
                                       maximum_relative_error=.05)
        self.assertEqual(within["status"], "validated")
        self.assertAlmostEqual(within["relative_mean_error_interval"][0], .04, places=12)
        biased = qualify_log_residuals([math.log(1.12)] * 5,
                                       maximum_relative_error=.05)
        self.assertEqual(biased["status"], "invalid")

    def test_model_residual_equivalence_can_be_inconclusive(self):
        result = qualify_log_residuals((-.2, 0., .2), maximum_relative_error=.05)
        self.assertEqual(result["status"], "inconclusive")

    def test_model_residual_equivalence_requires_three_finite_observations(self):
        with self.assertRaisesRegex(ValueError, "three finite"):
            qualify_log_residuals((.01, -.01), maximum_relative_error=.05)
        with self.assertRaisesRegex(ValueError, "three finite"):
            qualify_log_residuals((.01, math.nan, -.01), maximum_relative_error=.05)

    def test_fixed_predeclared_look_reaches_five_percent(self):
        policy = AdaptivePolicy()
        reference = [10. + x for x in (-.002, .001, 0., .002, -.001)]
        candidate = [10. + x for x in (-.002, .001, 0., .002, -.001)]
        result = summarize(reference, candidate, policy, model_validation="direct_full")
        self.assertEqual(result["sample_count"], 5)
        self.assertTrue(result["timing_precision_reached"])
        self.assertLessEqual(result["reference"]["relative_timing_half_width"], .05)
        self.assertEqual(result["next_look"], 9)
        self.assertEqual(result["stop_reason"], "precision_reached")
        self.assertFalse(result["inference"]["distribution_free_coverage_claim"])

    def test_noisy_samples_continue_at_declared_look_then_stop_at_limit(self):
        policy = AdaptivePolicy(looks=(5, 9), relative_precision=.01)
        values = [8., 9., 10., 11., 12.]
        at_five = summarize(values, values, policy, model_validation="direct_full")
        self.assertFalse(at_five["timing_precision_reached"])
        self.assertEqual(at_five["next_look"], 9)
        self.assertEqual(at_five["stop_reason"], "continue")
        at_nine = summarize(values + [7., 8., 9., 10.], values + [7., 8., 9., 10.],
                            policy, model_validation="direct_full")
        self.assertFalse(at_nine["timing_precision_reached"])
        self.assertIsNone(at_nine["next_look"])
        self.assertEqual(at_nine["stop_reason"], "maximum_samples")

    def test_one_percent_is_optional_and_increases_sample_requirement(self):
        values = [10. + x for x in (-.12, .12, -.06, .06, 0.)]
        five = summarize(values, values, AdaptivePolicy(relative_precision=.05),
                         model_validation="direct_full")
        one = summarize(values, values, AdaptivePolicy(relative_precision=.01,
                                                       looks=(5, 9)),
                        model_validation="direct_full")
        self.assertTrue(five["timing_precision_reached"])
        self.assertFalse(one["timing_precision_reached"])
        self.assertEqual(one["next_look"], 9)

    def test_family_count_and_multiple_looks_widen_timing_interval(self):
        values = [9.9, 10.1, 9.95, 10.05, 10.]
        one = summarize(values, values, AdaptivePolicy(looks=(5, 9), family_count=1),
                        model_validation="direct_full")
        many = summarize(values, values, AdaptivePolicy(looks=(5, 9), family_count=10),
                         model_validation="direct_full")
        self.assertGreater(many["policy"]["interval_confidence_per_look"],
                           one["policy"]["interval_confidence_per_look"])
        self.assertGreater(many["reference"]["relative_timing_half_width"],
                           one["reference"]["relative_timing_half_width"])
        # Three intervals each spend alpha/(3 * families * looks), so their
        # total declared familywise allocation is exactly policy.alpha.
        allocation = many["policy"]["alpha_allocation"]
        self.assertAlmostEqual(allocation["timing_intervals_per_family_look_metric"] *
                               3 * 10 * 2, many["policy"]["alpha"])
        self.assertTrue(allocation["gate_reuses_paired_timing_interval"])

    def test_large_paired_slowdown_is_rejected_when_model_is_directly_measured(self):
        reference = [10., 10.001, 9.999, 10.002, 9.998]
        candidate = [1.4 * x for x in reference]
        result = summarize(reference, candidate, AdaptivePolicy(),
                           model_validation="direct_full")
        self.assertEqual(result["performance_gate"], "performance_regression")
        self.assertGreater(result["paired_candidate_over_reference"]["model_adjusted_gate_interval"][0], 1.30)
        self.assertEqual(result["stop_reason"], "performance_regression")

    def test_abbreviated_prediction_never_passes_without_independent_validation(self):
        reference = [10., 10.001, 9.999, 10.002, 9.998]
        result = summarize(reference, reference)
        self.assertEqual(result["performance_gate"], "unvalidated")
        self.assertFalse(result["gate_performance_pass_eligible"])

    def test_heldout_model_error_widens_and_does_not_shrink_with_more_timing(self):
        policy = AdaptivePolicy(looks=(5, 9), model_allowance_log=.02)
        first = [10., 10.001, 9.999, 10.002, 9.998]
        second = first + [10.0005, 9.9995, 10.0015, 9.9985]
        errors = (.01, -.015, .02)
        a = summarize(first, first, policy, model_validation="validated",
                      reference_model_log_errors=errors,
                      candidate_model_log_errors=errors)
        b = summarize(second, second, policy, model_validation="validated",
                      reference_model_log_errors=errors,
                      candidate_model_log_errors=errors)
        self.assertEqual(a["performance_gate"], "pass")
        self.assertEqual(b["performance_gate"], "pass")
        self.assertEqual(a["reference"]["independent_model_log_error_bound"], .02)
        self.assertEqual(b["reference"]["independent_model_log_error_bound"], .02)
        self.assertAlmostEqual(
            a["candidate"]["future_execution_prediction_interval_seconds"][1] /
            a["candidate"]["future_execution_prediction_interval_seconds"][0],
            b["candidate"]["future_execution_prediction_interval_seconds"][1] /
            b["candidate"]["future_execution_prediction_interval_seconds"][0], delta=.01)

    def test_future_prediction_interval_is_explicitly_unqualified_scenario_range(self):
        values = [10., 10.001, 9.999, 10.002, 9.998]
        errors = (.01, -.015, .02)
        result = summarize(values, values, AdaptivePolicy(looks=(5,)),
                           model_validation="validated",
                           reference_model_log_errors=errors,
                           candidate_model_log_errors=errors)
        for name in ("reference", "candidate"):
            row = result[name]
            self.assertFalse(row["statistical_future_coverage_qualified"])
            self.assertFalse(row["model_error_bound_is_verified_for_future_runs"])
            self.assertEqual(row["bounded_model_error_scenario_interval_seconds"],
                             row["future_execution_prediction_interval_seconds"])
            self.assertIn("future model-error coverage is unverified",
                          row["future_execution_interval_scope"])
        self.assertFalse(result["inference"]["statistical_future_coverage_qualified"])
        self.assertTrue(result["inference"]["future_execution_intervals_are_engineering_scenario_ranges"])

    def test_separate_source_model_bounds_accumulate_for_paired_gate(self):
        values = [10., 10.001, 9.999, 10.002, 9.998]
        policy = AdaptivePolicy(looks=(5,), model_allowance_log=.01)
        errors = (.005, -.006, .007)
        separate = summarize(values, [1.4 * x for x in values], policy,
                             model_validation="validated",
                             reference_model_log_errors=errors,
                             candidate_model_log_errors=errors)
        self.assertAlmostEqual(
            separate["paired_candidate_over_reference"]["paired_model_log_error_bound"], .02)
        self.assertEqual(
            separate["paired_candidate_over_reference"]["paired_model_log_error_bound_source"],
            "sum of separate reference and candidate model-error bounds")
        paired = summarize(values, [1.4 * x for x in values], policy,
                           model_validation="validated",
                           reference_model_log_errors=errors,
                           candidate_model_log_errors=errors,
                           paired_model_log_errors=(.003, -.004, .005))
        self.assertAlmostEqual(
            paired["paired_candidate_over_reference"]["paired_model_log_error_bound"], .01)
        self.assertEqual(
            paired["paired_candidate_over_reference"]["paired_model_log_error_bound_source"],
            "independent paired full-run residuals")

    def test_model_validation_requires_independent_source_residuals(self):
        values = [10., 10.01, 9.99, 10.005, 9.995]
        with self.assertRaisesRegex(ValueError, "finite independent log residuals"):
            summarize(values, values, model_validation="validated",
                      reference_model_log_errors=(.01,))
        with self.assertRaisesRegex(ValueError, "validated model status"):
            summarize(values, values, model_validation="validated",
                      reference_model_log_errors=(0., .01, -.01),
                      candidate_model_log_errors=())

    def test_only_predeclared_looks_are_valid_and_next_look_is_explicit(self):
        policy = AdaptivePolicy()
        self.assertEqual(next_look(0, policy), 5)
        self.assertEqual(next_look(5, policy), 9)
        self.assertEqual(next_look(33, policy), None)
        with self.assertRaisesRegex(ValueError, "predeclared"):
            next_look(6, policy)
        with self.assertRaisesRegex(ValueError, "predeclared look"):
            summarize([1.] * 6, [1.] * 6, policy, model_validation="direct_full")

    def test_bad_values_and_policies_fail_closed(self):
        for bad in (0., -1., math.nan, math.inf):
            with self.subTest(value=bad), self.assertRaises(ValueError):
                summarize([bad] * 5, [1.] * 5, model_validation="direct_full")
        with self.assertRaisesRegex(ValueError, "identical paired lengths"):
            summarize([1.] * 5, [1.] * 4, model_validation="direct_full")
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            AdaptivePolicy(looks=(5, 5, 9))
        with self.assertRaisesRegex(ValueError, "family_count"):
            AdaptivePolicy(family_count=0)
        with self.assertRaisesRegex(ValueError, "model_allowance"):
            AdaptivePolicy(model_allowance_log=-.1)


if __name__ == "__main__":
    unittest.main()


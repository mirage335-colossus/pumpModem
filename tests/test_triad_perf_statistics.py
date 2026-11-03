import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_perf_statistics import (compare_paired, predict_full,
                                   transferable_stage_estimate)


def linear_case(*, nonlinearity=0., repeat_count=5):
    preparation = [1.0 + .01 * i for i in range(repeat_count)]
    scores = []
    holds = []
    for i in range(repeat_count):
        run_scale = 1. + .002 * (i - (repeat_count - 1) / 2)
        scores += [
            {"repeat": i, "count": 1, "seconds": run_scale * 2.5},
            {"repeat": i, "count": 3, "seconds": run_scale * 3.5},
        ]
        holds.append({"repeat": i, "count": 6,
                      "seconds": run_scale * (2. + .5 * 6 + nonlinearity)})
    return preparation, scores, holds


class FullWorkloadPredictionTests(unittest.TestCase):
    def test_linear_prediction_is_in_full_seconds_and_validates_holdouts(self):
        preparation, scores, holds = linear_case()
        result = predict_full(preparation, scores, target_count=5, holdouts=holds)
        self.assertEqual(result["status"], "validated_prediction")
        self.assertTrue(result["performance_pass_eligible"])
        self.assertEqual(result["target_full_cfo_count"], 5)
        for i, fixed in enumerate(result["fixed_score_seconds_by_repeat"]):
            run_scale = 1. + .002 * (i - 2)
            self.assertAlmostEqual(fixed, 2. * run_scale)
        self.assertAlmostEqual(result["per_cfo_seconds_by_repeat"][0], .5 * .996)
        # Full work: preparation + fitted fixed score + per-CFO score.
        self.assertAlmostEqual(result["predicted_full_time"]["point_seconds"], 1.03 + 4.5, delta=.03)
        low, high = result["predicted_full_time"]["conditional_prediction_interval_seconds"]
        self.assertLess(low, result["predicted_full_time"]["point_seconds"])
        self.assertGreater(high, result["predicted_full_time"]["point_seconds"])
        self.assertTrue(result["inference"]["target_is_full_workload_seconds"])
        self.assertFalse(result["inference"]["distribution_free_coverage_claim"])
        self.assertAlmostEqual(result["holdout_validation"]["maximum_absolute_relative_error"], 0., places=12)

    def test_allowance_bounds_actual_time_and_future_variation(self):
        rows = [{"repeat": i, "count": f, "seconds": float(f)}
                for i in range(3) for f in (1, 2)]
        holds = [{"repeat": i, "count": 8, "seconds": 10.} for i in range(3)]
        result = predict_full([.01]*3, rows, 8, holds)
        self.assertEqual(result["status"], "validated_prediction")
        bounds = result["predicted_full_time"]["conditional_prediction_interval_seconds"]
        self.assertLessEqual(bounds[0], 10.01)
        self.assertGreaterEqual(bounds[1], 10.01)
        preparation, rows, holds = linear_case()
        interval = predict_full(preparation, rows, 5, holds)["predicted_full_time"]
        self.assertLess(interval["conditional_prediction_interval_seconds"][0],
                        interval["conditional_mean_confidence_interval_seconds"][0])
        self.assertGreater(interval["conditional_prediction_interval_seconds"][1],
                           interval["conditional_mean_confidence_interval_seconds"][1])

    def test_missing_holdout_cannot_become_performance_pass(self):
        preparation, scores, _ = linear_case()
        result = predict_full(preparation, scores, target_count=5)
        self.assertEqual(result["status"], "unvalidated_prediction")
        self.assertFalse(result["performance_pass_eligible"])
        self.assertIsNone(result["holdout_validation"]["maximum_absolute_relative_error"])

    def test_nonlinear_full_holdout_rejects_scaling_model(self):
        preparation, scores, holds = linear_case(nonlinearity=2.)
        result = predict_full(preparation, scores, target_count=5, holdouts=holds)
        self.assertEqual(result["status"], "invalid_scaling")
        self.assertFalse(result["performance_pass_eligible"])
        self.assertGreater(result["holdout_validation"]["maximum_absolute_relative_error"], .20)

    def test_insufficient_outer_repeats_has_no_confidence_interval(self):
        preparation, scores, holds = linear_case(repeat_count=2)
        result = predict_full(preparation, scores, target_count=5, holdouts=holds)
        self.assertEqual(result["status"], "insufficient_replicates")
        self.assertIsNone(result["predicted_full_time"])
        self.assertFalse(result["performance_pass_eligible"])

    def test_extrapolation_and_data_contract_fail_closed(self):
        preparation, scores, holds = linear_case()
        with self.assertRaisesRegex(ValueError, "extrapolation bound"):
            predict_full(preparation, scores, target_count=25, holdouts=holds)
        with self.assertRaisesRegex(ValueError, "two distinct"):
            predict_full(preparation, [r for r in scores if r["count"] == 1],
                         target_count=1)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            predict_full(preparation, scores + [scores[0]], target_count=5,
                         holdouts=holds)
        with self.assertRaisesRegex(ValueError, "confidence"):
            predict_full(preparation, scores, target_count=5, confidence=1.0)
        with self.assertRaisesRegex(ValueError, "finite"):
            predict_full([1., 1., float("nan")], scores, target_count=5)

    def test_negative_empirical_slope_is_bounded_at_zero(self):
        scores = []
        for repeat in range(3):
            scores += [{"repeat": repeat, "count": 1, "seconds": 5.},
                       {"repeat": repeat, "count": 3, "seconds": 4.}]
        result = predict_full([1., 1., 1.], scores, target_count=3)
        self.assertEqual(result["status"], "unvalidated_prediction")
        self.assertEqual(result["per_cfo_seconds_by_repeat"], [0., 0., 0.])
        self.assertTrue(all(math.isfinite(x) for x in result["predicted_full_seconds_by_repeat"]))


class PairedGateTests(unittest.TestCase):
    def test_unchanged_full_runtime_passes_with_declared_allowance(self):
        reference = [10., 10.1, 9.9, 10.05, 9.95]
        result = compare_paired(reference, reference)
        self.assertEqual(result["gate"], "pass")
        self.assertTrue(result["performance_pass_eligible"])
        self.assertEqual(result["mean_candidate_full_seconds"], 10.)
        self.assertAlmostEqual(result["absolute_mean_full_seconds_difference_magnitude"], 0.)
        self.assertGreater(result["expanded_full_prediction_ratio_interval"][1], 1.)

    def test_large_material_slowdown_fails_only_on_lower_bound(self):
        reference = [10., 10.1, 9.9, 10.05, 9.95]
        candidate = [x * 1.7 for x in reference]
        result = compare_paired(reference, candidate)
        self.assertEqual(result["gate"], "performance_regression")
        self.assertGreater(result["expanded_full_prediction_ratio_interval"][0], 1.30)
        self.assertAlmostEqual(result["absolute_mean_full_seconds_difference_magnitude"], 7.)

    def test_subthreshold_or_uncertain_change_is_not_regression_failure(self):
        reference = [10., 10.1, 9.9, 10.05, 9.95]
        candidate = [x * 1.15 for x in reference]
        result = compare_paired(reference, candidate)
        self.assertEqual(result["gate"], "inconclusive")
        self.assertFalse(result["performance_pass_eligible"])
        self.assertLess(result["observed_prediction_ratio"], 1.30)

    def test_ci_confidence_level_and_model_error_widen_gate_interval(self):
        reference = [10., 10.01, 9.99, 10.005, 9.995]
        candidate = [x * ratio for x, ratio in zip(reference, (.98, 1.05, 1.12, 1.17, 1.18))]
        lo = compare_paired(reference, candidate, confidence=.80,
                            relative_allowance=.05)
        hi = compare_paired(reference, candidate, confidence=.99,
                            relative_allowance=.05)
        holdout = compare_paired(reference, candidate,
                                 paired_model_log_errors=(.25,))
        self.assertGreater(hi["unexpanded_timing_ratio_interval"][1],
                           lo["unexpanded_timing_ratio_interval"][1])
        self.assertGreater(holdout["log_allowance_used"], math.log(1.20))
        self.assertGreater(holdout["expanded_full_prediction_ratio_interval"][1],
                           compare_paired(reference, candidate)["expanded_full_prediction_ratio_interval"][1])

    def test_wrong_pair_counts_and_nonfinite_values_rejected(self):
        with self.assertRaisesRegex(ValueError, "identical paired lengths"):
            compare_paired([1., 2., 3.], [1., 2.])
        with self.assertRaisesRegex(ValueError, "finite"):
            compare_paired([1., 2., float("inf")], [1., 2., 3.])
        result = compare_paired([1., 2.], [1., 2.])
        self.assertEqual(result["gate"], "inconclusive")
        self.assertFalse(result["performance_pass_eligible"])


class ArchitectureTransferTests(unittest.TestCase):
    def test_unmeasured_target_estimate_has_no_confidence_interval(self):
        result = transferable_stage_estimate(
            {"fft": 4., "score": 6.}, {"fft": 100., "score": 50.},
            {"fft": 200., "score": 25.})
        self.assertAlmostEqual(result["target_seconds_point_estimate"], 14.)
        self.assertEqual(result["status"], "unvalidated_transfer_estimate")
        self.assertIsNone(result["target_seconds_validation_interval"])
        self.assertFalse(result["performance_pass_eligible"])
        self.assertFalse(result["inference"]["hardware_specifications_alone_used"])

    def test_measured_target_holdouts_bound_transfer_or_reject_it(self):
        args = ({"fft": 4., "score": 6.}, {"fft": 100., "score": 50.},
                {"fft": 200., "score": 25.})
        supported = transferable_stage_estimate(*args, validation_log_errors=(.01, -.01, .02))
        invalid = transferable_stage_estimate(*args, validation_log_errors=(0., .01, .35))
        self.assertEqual(supported["status"], "validated_target_profile")
        self.assertIsNotNone(supported["target_seconds_validation_interval"])
        self.assertFalse(supported["performance_pass_eligible"])
        self.assertEqual(invalid["status"], "invalid_scaling")


if __name__ == "__main__":
    unittest.main()

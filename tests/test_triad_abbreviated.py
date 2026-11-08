"""Cheap inference and refusal checks; no performance campaign is executed."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_abbreviated import analyze, log_interval, policy, validate_baseline, KERNELS, SCHEMA


class AbbreviatedTests(unittest.TestCase):
    def test_exact_additive_work_and_large_projection_execute_no_target(self):
        result = analyze([[.01, .02, .04]]*3, 10**9)
        self.assertEqual(result["model_status"], "abbreviated_holdout_supported")
        self.assertEqual(result["measured_kernel_units"], 21)
        self.assertAlmostEqual(result["measured_seconds"], .21)
        self.assertAlmostEqual(result["projected_seconds"], 10**7)
        self.assertEqual(result["comparison"], "uncompared")
        self.assertTrue(result["stop"])

    def test_holdout_detects_nonadditive_work_and_refuses_projection(self):
        result = analyze([[.01, .02, .08]]*3, 1000)
        self.assertEqual(result["model_status"], "unresolved")
        self.assertIsNone(result["projected_seconds"])
        self.assertFalse(result["stop"])
        final = analyze([[.01, .02, .08]]*9, 1000)
        self.assertEqual(final["stop_reason"], "bounded_limit")
        self.assertIsNone(final["projected_seconds"])

    def test_model_error_floor_does_not_shrink_with_more_repetitions(self):
        short = analyze([[.01, .02, .044]]*3, 1000)
        longer = analyze([[.01, .02, .044]]*9, 1000)
        self.assertEqual(short["observed_model_log_error_floor"], longer["observed_model_log_error_floor"])
        self.assertEqual(short["conditional_projection_interval_seconds"], longer["conditional_projection_interval_seconds"])

    def test_material_slowdown_negative_control_and_clear_comparison(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        slow = analyze([[.02, .04, .08]]*3, 1000, baseline=baseline)
        self.assertEqual(slow["comparison"], "material_slowdown")
        same = analyze([[.01, .02, .04]]*3, 1000, baseline=baseline)
        self.assertEqual(same["comparison"], "no_material_slowdown_for_measured_kernels")
        invalid = analyze([[.01, .02, .08]]*3, 1000, baseline=baseline)
        self.assertEqual(invalid["comparison"], "unresolved_model")

    def test_uncertain_timing_cannot_be_reported_as_decision(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        varying = analyze([[.005, .01, .02], [.01, .02, .04], [.025, .05, .1]], 1000, baseline=baseline)
        self.assertEqual(varying["comparison"], "inconclusive")
        self.assertFalse(varying["stop"])

    def test_invalid_baseline_model_fields_cannot_produce_a_verdict(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        for value in (-1, float("nan"), float("inf"), True, "0"):
            with self.subTest(error_floor=value), self.assertRaises(ValueError):
                analyze([[.02, .04, .08]]*3, 1000,
                        baseline={**baseline, "observed_model_log_error_floor": value})
        with self.assertRaises(ValueError):
            analyze([[.02, .04, .08]]*3, 1000,
                    baseline={**baseline, "model_status": "qualified"})

    def test_undeclared_look_bad_target_and_invalid_observations_rejected(self):
        for rows, target in (([[1, 2, 4]]*4, 100), ([[1, 2, 4]]*3, 3),
                             ([[1, 2, 4]]*3, True), ([[0, 2, 4]]*3, 100),
                             ([[1, float("nan"), 4]]*3, 100)):
            with self.assertRaises(ValueError):
                analyze(rows, target)
        with self.assertRaises(ValueError):
            log_interval([1, 1], alpha=.05)

    def test_baseline_must_match_inputs_device_environment_policy_and_coverage(self):
        baseline = {"schema": SCHEMA, "geometry": {"input": "abc"},
                    "environment": {"device": "cpu"}, "policy": policy(),
                    "kernels": {k: {} for k in KERNELS}}
        validate_baseline(baseline, baseline["geometry"], baseline["environment"])
        for geometry, environment in (({"input": "other"}, {"device": "cpu"}),
                                      ({"input": "abc"}, {"device": "cuda"})):
            with self.assertRaises(ValueError):
                validate_baseline(baseline, geometry, environment)
        baseline["kernels"].pop(KERNELS[0])
        with self.assertRaises(ValueError):
            validate_baseline(baseline, baseline["geometry"], baseline["environment"])


if __name__ == "__main__":
    unittest.main()

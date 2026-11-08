"""Focused statistical and resource-boundary checks; no timing campaign."""
from pathlib import Path
from unittest import TestCase, main
from unittest.mock import patch
import math
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_case_metrics as metrics


class ServiceSummaryTests(TestCase):
    def test_constant_ratios_support_fixed_geometry_and_report_only_ratios(self):
        result = metrics.summarize_service([[.5, .5, .5]] * 3)
        self.assertEqual(result["realtime_status"], "pass")
        self.assertEqual(result["model_status"], "abbreviated_holdout_supported")
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], .5)
        self.assertAlmostEqual(result["margin_fraction"], .5)
        self.assertEqual(result["margin_interval"], [1 - x for x in reversed(
            result["ratio_processing_seconds_per_baseband_second_interval"])])
        self.assertEqual(result["ratios"], [[.5, .5, .5]] * 3)
        self.assertFalse(any("cpu_seconds" in key or "projected" in key for key in result))

    def test_training_uses_declared_one_two_unit_weights_and_four_unit_holdout(self):
        result = metrics.summarize_service([[.2, .4, .6]] * 3)
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], 1 / 3)
        self.assertAlmostEqual(result["holdout_ratio_interval"][0], .6 / (1 / 3))
        self.assertEqual(result["model_status"], "unresolved")

    def test_service_point_is_arithmetic_mean_of_outer_training_ratios(self):
        result = metrics.summarize_service([[.1, .1, .1], [.2, .2, .2], [.8, .8, .8]])
        actual_mean = (.1 + .2 + .8) / 3
        geometric_mean = (.1 * .2 * .8) ** (1 / 3)
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], actual_mean)
        self.assertNotAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], geometric_mean)
        self.assertLessEqual(result["training_ratio_interval"][0], actual_mean)
        self.assertGreaterEqual(result["training_ratio_interval"][1], actual_mean)

    def test_arithmetic_interval_clamps_positive_ratio_lower_bound(self):
        center, interval = metrics.arithmetic_interval([.01, .5, 1.5], alpha=.05)
        self.assertAlmostEqual(center, (.01 + .5 + 1.5) / 3)
        self.assertEqual(interval[0], 0.0)
        self.assertGreater(interval[1], center)
        for values, alpha in (([1, 2], .05), ([1, 2, 3], 0),
                              ([1, float("nan"), 3], .05), ([True, 2, 3], .05)):
            with self.subTest(values=values, alpha=alpha), self.assertRaises(ValueError):
                metrics.arithmetic_interval(values, alpha=alpha)

    def test_realtime_equality_fails_and_ninety_percent_margin_is_preserved(self):
        equal = metrics.summarize_service([[1, 1, 1]] * 3)
        self.assertEqual(equal["realtime_status"], "fail")
        self.assertEqual(equal["stop_reason"], "point_at_or_above_realtime_limit")
        fast = metrics.summarize_service([[.1, .1, .1]] * 3)
        self.assertAlmostEqual(fast["margin_fraction"], .9)
        self.assertEqual(fast["realtime_status"], "pass")

    def test_observed_model_error_floor_does_not_shrink_across_looks(self):
        short = metrics.summarize_service([[.5, .5, .65]] * 3)
        long = metrics.summarize_service([[.5, .5, .65]] * 9)
        self.assertAlmostEqual(short["observed_model_log_error_floor"], math.log(1.3))
        self.assertEqual(short["observed_model_log_error_floor"], long["observed_model_log_error_floor"])

    def test_declared_looks_family_size_and_all_ratio_values_are_validated(self):
        invalid = [
            ([[1, 1, 1]] * 4, 1), ([[1, 1]] * 3, 1), ([[1, 1, 1]] * 3, 0),
            ([[True, 1, 1]] * 3, 1), ([[0, 1, 1]] * 3, 1),
            ([[1, float("nan"), 1]] * 3, 1), ([[1, float("inf"), 1]] * 3, 1),
        ]
        for rows, family_size in invalid:
            with self.subTest(rows=rows, family_size=family_size), self.assertRaises(ValueError):
                metrics.summarize_service(rows, family_size=family_size)

    def test_family_alpha_covers_looks_and_three_claim_kinds(self):
        with patch.object(metrics, "arithmetic_interval", wraps=metrics.arithmetic_interval) as arithmetic, \
             patch.object(metrics, "log_interval", wraps=metrics.log_interval) as logarithmic:
            metrics.summarize_service([[.5, .5, .5]] * 3, family_size=2)
        self.assertEqual(arithmetic.call_count, 1)
        self.assertEqual(logarithmic.call_count, 1)
        self.assertEqual(arithmetic.call_args.kwargs["alpha"], .05 / (2 * 3 * 3))
        self.assertEqual(logarithmic.call_args.kwargs["alpha"], .05 / (2 * 3 * 3))


class WorkspaceTests(TestCase):
    def test_observed_peak_can_raise_conservative_peak_and_margin(self):
        result = metrics.workspace_summary(60, 100, observed_peak_bytes=80)
        self.assertEqual(result["estimated_peak_bytes"], 60)
        self.assertEqual(result["observed_peak_bytes"], 80)
        self.assertEqual(result["conservative_peak_bytes"], 80)
        self.assertEqual(result["headroom_bytes"], 20)
        self.assertAlmostEqual(result["margin_fraction"], .2)
        self.assertEqual(result["qualification"], "not_a_certificate")

    def test_budget_equality_fails_and_estimate_only_has_no_margin_claim(self):
        failed = metrics.workspace_summary(100, 100)
        self.assertEqual(failed["status"], "failure")
        self.assertEqual(failed["headroom_bytes"], 0)
        self.assertIsNone(failed["margin_fraction"])
        for values in ((-1, 100, None), (1, 0, None), (1, 100, float("nan")),
                       (True, 100, None), (1, 100, True)):
            with self.subTest(values=values), self.assertRaises(ValueError):
                metrics.workspace_summary(*values)


class ProbabilityAndResourceTests(TestCase):
    def test_clopper_pearson_endpoints_are_exact_and_small_samples_do_not_overclaim(self):
        lower, upper = metrics.success_interval(0, 3)
        self.assertEqual(lower, 0)
        self.assertAlmostEqual(upper, 1 - .05 ** (1 / 3))
        lower, upper = metrics.success_interval(3, 3)
        self.assertAlmostEqual(lower, .05 ** (1 / 3))
        self.assertEqual(upper, 1)
        self.assertLess(lower, .9)

    def test_invalid_success_counts_and_alpha_are_rejected(self):
        for args, kwargs in (((-1, 3), {}), ((4, 3), {}), ((True, 3), {}),
                             ((1, 0), {}), ((1, 3), {"alpha": 0}),
                             ((1, 3), {"alpha": 1.1}), ((1, 3), {"alpha": True})):
            with self.subTest(args=args, kwargs=kwargs), self.assertRaises(ValueError):
                metrics.success_interval(*args, **kwargs)

    def test_machine_snapshot_uses_half_of_constrained_memory_and_retains_warnings(self):
        with patch("triad_cpu.detect_cpu_budget", return_value={"available_cpus": 2}), \
             patch("build_capacity.available_memory", return_value=1000), \
             patch("build_capacity.linux_limits", return_value=([2], [600])), \
             patch.object(Path, "exists", return_value=False), \
             patch.object(metrics, "platform") as platform:
            platform.platform.return_value = "Linux"
            platform.processor.return_value = "model-test"
            with patch("sys.platform", "linux"):
                result = metrics.machine_resources()
        self.assertEqual(result["cpu"], {"available_cpus": 2})
        self.assertEqual(result["cpu_model"], "model-test")
        self.assertEqual(result["available_memory_bytes"], 600)
        self.assertEqual(result["memory_budget_bytes"], 300)
        self.assertEqual(result["cgroup_memory"]["memory_headroom_bytes"], [600])

    def test_machine_snapshot_surfaces_unknown_cgroup_capacity(self):
        with patch("triad_cpu.detect_cpu_budget", return_value={"available_cpus": 1}), \
             patch("build_capacity.available_memory", return_value=100), \
             patch("build_capacity.linux_limits", side_effect=ValueError("cgroup mount not visible")), \
             patch.object(metrics, "platform") as platform, \
             patch("sys.platform", "linux"):
            platform.processor.return_value = "test"
            result = metrics.machine_resources()
        self.assertEqual(result["memory_budget_bytes"], 50)
        self.assertEqual(result["cgroup_memory"]["status"], "unknown")
        self.assertTrue(any("cgroup mount not visible" in warning for warning in result["warnings"]))


if __name__ == "__main__":
    main()

"""Cheap inference and refusal checks; no performance campaign is executed."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_abbreviated import analyze, log_interval, policy, validate_baseline, KERNELS, SCHEMA
from triad_native import ProbeError, runtime_identity, validate_receipt


def device_receipt():
    return {"schema": "datapump.triad.native-probe.v1", "command": "continuation", "status": "complete",
            "configuration": {}, "budget": {}, "observation": {"input_sha256": "1" * 64},
            "allocation": {"hardware_threads": 2, "workers": 4, "slot_owners": 4,
                           "memory_limit_bytes": 2**30, "receiver_numerical_threads_per_worker": 1},
            "identity": {
                "backend": "cuda", "implementation": "native-production-receiver",
                "source_manifest_sha256": "2" * 64, "build_options_sha256": "3" * 64, "compiler": "GNU-14",
                "build_config": "Release", "configured_tensor_backend": True,
                "receiver_arithmetic": "native_fp64_state_and_admission", "requested_tensor_precision": "automatic",
                "backend_receipt": {"device": "cuda:0", "device_name": "Fixture GPU",
                                    "library_version": "2.15", "cuda_runtime_version": 13000,
                                    "cuda_driver_version": 13000, "cuda": True, "numerical_threads": 1,
                                    "precision": "automatic", "executed": True, "mapped_values": 0,
                                    "statistics_blocks": 4, "fft_calls": 0, "dc_reduction_values": 12,
                                    "score_rows": 0}},
            "phases": [{"name": "fine_work", "work": {}, "wall_seconds": .01, "represented_seconds": 1}],
            "result": {"coordinate_scope": "declared_public_work_coordinate", "stratum": "terminal"}}


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

    def test_native_runtime_comparison_binds_build_and_nested_device_fields(self):
        original = device_receipt()
        expected = runtime_identity(original)
        for field, value in (("build_config", "Debug"), ("build_options_sha256", "f" * 64),
                             ("requested_tensor_precision", "fp64")):
            changed = copy.deepcopy(original)
            changed["identity"][field] = value
            self.assertNotEqual(runtime_identity(changed), expected)
        for field, value in (("library_version", "2.14"), ("device_name", "Other GPU"),
                             ("cuda_driver_version", 12800), ("precision", "fp64")):
            changed = copy.deepcopy(original)
            changed["identity"]["backend_receipt"][field] = value
            self.assertNotEqual(runtime_identity(changed), expected)
        changed = copy.deepcopy(original)
        changed["identity"]["source_manifest_sha256"] = "f" * 64
        changed["identity"]["backend_receipt"]["dc_reduction_values"] += 8
        self.assertEqual(runtime_identity(changed), expected)

    def test_terminal_zero_mapping_requires_actual_accelerated_reductions(self):
        receipt = device_receipt()
        validate_receipt(receipt, command="continuation", backend="cuda", returncode=0)
        for change in ("wrong_scope", "no_projection", "wrong_stratum", "fallback"):
            wrong = copy.deepcopy(receipt)
            if change == "wrong_scope":
                wrong["result"]["coordinate_scope"] = "complete_public_selection"
            elif change == "no_projection":
                wrong["identity"]["backend_receipt"]["dc_reduction_values"] = 0
            elif change == "wrong_stratum":
                wrong["result"]["stratum"] = "cold-maximum"
            else:
                wrong["identity"]["backend_receipt"]["executed"] = False
            with self.subTest(change=change), self.assertRaises(ProbeError):
                validate_receipt(wrong, command="continuation", backend="cuda", returncode=0)


if __name__ == "__main__":
    unittest.main()

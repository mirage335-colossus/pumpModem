"""Focused native full-simulation prediction contracts; no sustained campaign."""
from __future__ import annotations

import copy
import contextlib
from dataclasses import replace
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_native as native
from triad_perf_adaptive import AdaptivePolicy, model_confirmation_policy, regression_profile

spec = importlib.util.spec_from_file_location("native_performance_runner", ROOT / "tools/triad-performance.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def record(rows, *, receiver=None, other=None, overhead=.01):
    receiver = .002*rows-.05 if receiver is None else receiver
    other = .001*rows+.02 if other is None else other
    return {"total_rows": rows, "batch_trials": 1, "regime": {"shape": "fixed"},
            "stage_seconds": {"receiver": receiver, "generation_setup_other": other,
                              "process_startup_output": overhead},
            "full_test_wall_seconds": receiver+other+overhead}


def mock_identity(binary, case, threads):
    return {"binary_sha256": native.digest(str(binary)), "case_sha256": native.digest(case),
            "environment": {"threads": threads, "affinity_cpu_count": 2, "affinity_cpus": [0, 6]},
            "arithmetic": "fp64", "timing_boundary": "synthetic complete invocation",
            "protocol": "test reporting revision", "measurement_contract": native.MEASUREMENT_CONTRACT,
            "adapter_sha256": "synthetic-adapter", "adaptive_protocol_sha256": "synthetic-adaptive"}


class NativeModelTests(unittest.TestCase):
    def test_explicit_direct_override_preserves_full_native_work_and_policy(self):
        original = next(case for case in native.scenarios("ci") if case["id"] == "native-vlf-low-cn0")
        snapshot = copy.deepcopy(original)
        selected, overrides = runner.native_direct_overrides([original], [original["id"]], binary="/native", threads=1)
        direct = selected[0]
        self.assertEqual(direct, {key: value for key, value in original.items() if key != "training_seconds"})
        self.assertEqual(original, snapshot)
        route = overrides[original["id"]]
        self.assertNotEqual(route["original_case_sha256"], route["transformed_case_sha256"])
        self.assertTrue(route["full_target_equivalence"]["run_argv_equal"])
        self.assertTrue(route["full_target_equivalence"]["configuration_argv_equal"])
        calls = []

        def fake_run(binary, case, duration, seed, **kwargs):
            calls.append((duration, seed))
            self.assertEqual(duration, original["target_seconds"])
            return {**record(int(duration)), "seed": seed, "requested_seconds": duration}

        policy = AdaptivePolicy(stopping_rule="decision", **regression_profile("ci"))
        with (patch.object(native, "identity", side_effect=mock_identity),
              patch.object(native, "target_geometry", return_value={"performance_geometry": {"capture_samples": int(original["target_seconds"])}}),
              patch.object(native, "run_native", side_effect=fake_run),
              patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
            result = native.measure_native_case("reference", "candidate", direct, policy,
                validation_mode="routine", anchor={"invalid": "unused historical model"})
        self.assertEqual(len(calls), 10)
        self.assertTrue(result["measurement_plan"]["direct"])
        self.assertEqual(result["prediction"]["policy"]["slowdown_threshold"], 1.70)
        self.assertEqual(result["prediction"]["policy"]["pass_alpha"], .30)
        self.assertEqual(result["prediction"]["stop_reason"], "regression_decision_reached")
        self.assertEqual(result["anchors"], {})
        self.assertEqual(result["current_validation"]["holds"], [])

    def test_direct_override_rejects_bad_selection_before_hardware_work(self):
        base = ["--device", "cpu", "--binary", "/native", "--output", "/tmp/native-route-not-run.json"]
        invalid = (["--native-direct-case", "not-a-case"],
                   ["--native-direct-case", "native-vlf-low-cn0", "--native-direct-case", "native-vlf-low-cn0"],
                   ["--native-direct-case", "native-flat"],
                   ["--native-direct-case", "native-vlf-low-cn0", "--case", "native-flat"],
                   ["--native-direct-case", "native-vlf-low-cn0", "--validation-mode", "qualify"])
        with patch("triad_perf_hardware.collect_profile") as hardware:
            for extra in invalid:
                with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    runner.main(base+extra)
            hardware.assert_not_called()
        parsed = runner.parse_args(base+["--native-direct-case", "native-vlf-low-cn0"])
        self.assertEqual(parsed.native_direct_case, ["native-vlf-low-cn0"])
        self.assertEqual(parsed.validation_mode, "auto")

    def test_direct_override_bypasses_existing_anchor_in_normal_backend_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            binary = directory/"native"
            binary.write_bytes(b"mock native binary")
            anchors = directory/"anchors"
            anchors.mkdir()
            anchor = anchors/"native-vlf-low-cn0.native-anchor.json"
            untouched = b"an existing anchor must neither be read nor overwritten"
            anchor.write_bytes(untouched)
            args = runner.parse_args(["--device", "cpu", "--_backend-worker", "cpu", "--binary", str(binary),
                "--reference-binary", str(binary), "--output", str(directory/"result.json"),
                "--anchor-directory", str(anchors), "--case", "native-vlf-low-cn0",
                "--native-direct-case", "native-vlf-low-cn0", "--regression-profile", "ci"])

            def path_witness(command, **kwargs):
                path = Path(command[command.index("--output")+1])
                path.write_text(json.dumps({"complete_applicable_coverage": True}))
                return SimpleNamespace(returncode=0, stderr="")

            def full_run(binary, selected, duration, seed, **kwargs):
                self.assertNotIn("training_seconds", selected)
                return {**record(int(duration)), "seed": seed, "requested_seconds": duration}

            report = runner.initial_report(args, "cpu")
            with (patch("triad_perf_hardware.collect_profile", return_value={"status": "complete", "hardware": {}}),
                  patch.object(runner, "hardware_health", return_value={}),
                  patch.object(runner.subprocess, "run", side_effect=path_witness),
                  patch.object(native, "identity", side_effect=mock_identity),
                  patch.object(native, "target_geometry", return_value={"performance_geometry": {"capture_samples": 800}}),
                  patch.object(native, "run_native", side_effect=full_run),
                  patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
                self.assertEqual(runner.run_backend(args, report), 0, report["failures"])
            self.assertEqual(anchor.read_bytes(), untouched)
            self.assertEqual(len(list(anchors.glob("*.native-anchor.json"))), 1)
            measured = report["cases"][0]
            self.assertEqual(measured["prediction"]["policy"]["slowdown_threshold"], 1.70)
            override = measured["measurement_override"]
            self.assertFalse(override["historical_anchor_read"])
            self.assertFalse(override["historical_anchor_written"])
            for name, identity in measured["identities"].items():
                self.assertEqual(override["original_measurement_identities"][name]["case_sha256"],
                                 override["original_case_sha256"])
                self.assertEqual(override["transformed_measurement_identities"][name], identity)

    def test_complete_row_model_keeps_negative_intercept_and_fixed_overhead(self):
        predicted = native.fit_duration_model([record(100), record(200)], 800)
        self.assertAlmostEqual(predicted["stage_seconds"]["receiver"], 1.55)
        self.assertAlmostEqual(predicted["stage_seconds"]["generation_setup_other"], .82)
        self.assertAlmostEqual(predicted["full_test_wall_seconds"], 2.38)
        check = native.validate_holdout(predicted, record(800))
        self.assertTrue(check["single_run_within_bound"])
        self.assertAlmostEqual(check["absolute_relative_error"], 0.)

    def test_model_rejects_changed_geometry_duplicate_rows_and_unbounded_extrapolation(self):
        a, b = record(100), record(200)
        bad = copy.deepcopy(b); bad["regime"]["shape"] = "changed-bank"
        with self.assertRaises(native.NativePerformanceError):
            native.fit_duration_model([a, bad], 400)
        with self.assertRaises(native.NativePerformanceError):
            native.fit_duration_model([a, a], 400)
        with self.assertRaises(native.NativePerformanceError):
            native.fit_duration_model([a, b], 1601)
        with self.assertRaises(native.NativePerformanceError):
            native.fit_duration_model([a, record(200, receiver=.01)], 400)

    def test_independent_complete_run_can_falsify_the_frozen_prediction(self):
        predicted = native.fit_duration_model([record(100), record(200)], 800)
        actual = record(800, receiver=3.)
        check = native.validate_holdout(predicted, actual, maximum_relative_error=.05)
        self.assertFalse(check["single_run_within_bound"])
        self.assertGreater(check["absolute_relative_error"], .30)
        changed = record(800); changed["regime"]["shape"] = "changed"
        self.assertFalse(native.validate_holdout(predicted, changed)["geometry_matched"])

    def test_complete_native_records_must_prove_basis_work(self):
        # The basis count check runs before more detailed stage witnesses.
        row = {"source_format": {}, "channel_model": {"path_evolution": {}},
               "path_truth_simulation_only": {"selective_perturbation": {}}, "frontend_report": {},
               "capture_samples": 100, "channel_samples": 100, "complete_observation": True,
               "candidates": [{"tap_count": 1}, {"tap_count": 2}], "basis_values": 299}
        with self.assertRaisesRegex(native.NativePerformanceError, "basis work"):
            native.coverage_witnesses(row)

    def test_one_frozen_model_before_independent_full_holds(self):
        case = {"id": "synthetic", "scope": "contract test", "target_seconds": 800.,
                "holdout_seconds": 400., "training_seconds": [100., 200.], "witnesses": []}
        frozen, held = [], []

        def fake_geometry(binary, selected, *, seconds=None, **kwargs):
            return {"performance_geometry": {"capture_samples": int(seconds or selected["target_seconds"])}}

        def fake_run(binary, selected, duration, seed, **kwargs):
            if duration == 800.:
                self.assertEqual(len(frozen), 1)
                held.append(seed)
            return {**record(int(duration)), "seed": seed, "requested_seconds": duration}

        with (patch.object(native, "identity", side_effect=mock_identity),
              patch.object(native, "target_geometry", side_effect=fake_geometry),
              patch.object(native, "run_native", side_effect=fake_run),
              patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
            result = native.measure_native_case("reference", "candidate", case,
                                                AdaptivePolicy(looks=(3, 5), stopping_rule="decision", **regression_profile("ci")),
                                                freeze_callback=frozen.append)
        self.assertEqual(held, [840000, 840000, 841000, 841000, 842000, 842000])
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["qualification"]["status"], "validated")
        self.assertEqual(frozen[0]["model_confirmation_policy"], model_confirmation_policy().to_dict())
        self.assertEqual(result["anchors"]["confirmation_stop_reason"], "validated")
        self.assertEqual(len(result["raw"]["reference"]), 3)
        self.assertAlmostEqual(result["prediction"]["reference"]["point_seconds"],
                               frozen[0]["predictions"]["reference"]["full_test_wall_seconds"])
        self.assertTrue(all(hold["frozen_prediction_sha256"] == frozen[0]["sha256"]
                            for hold in result["anchors"]["full_holdouts"]))
        anchor, identities = result["anchors"], result["identities"]
        self.assertTrue(native.anchor_compatible(anchor, identities, case))
        different_mask = copy.deepcopy(identities)
        different_mask["candidate"]["environment"]["affinity_cpus"] = [0, 1]
        self.assertFalse(native.anchor_compatible(anchor, different_mask, case))
        reporter = copy.deepcopy(identities)
        reporter["candidate"]["adapter_sha256"] = "new reporting bytes"
        reporter["candidate"]["adaptive_protocol_sha256"] = "new adaptive reporting bytes"
        self.assertTrue(native.anchor_compatible(anchor, reporter, case))
        reporter["candidate"]["measurement_contract"] = "different timing semantics"
        self.assertFalse(native.anchor_compatible(anchor, reporter, case))
        self.assertFalse(native.anchor_compatible(anchor, identities, {**case, "target_seconds": 801.}))
        bad = copy.deepcopy(anchor)
        bad["full_holdouts"][0]["actual"]["total_rows"] -= 1
        bad["anchor_sha256"] = native.digest({k: v for k, v in bad.items() if k != "anchor_sha256"})
        self.assertFalse(native.anchor_compatible(bad, identities, case))

        # Synthetic legacy fixture using the allowlisted historical code IDs.
        # Changing the self-hashed provenance still does not make a different
        # measured implementation eligible for the reviewed contract mapping.
        def legacy_fixture(adapter_sha256):
            old = copy.deepcopy(anchor)
            old["schema"] = native.LEGACY_ANCHOR_SCHEMA
            protocol, _, adaptive = next(iter(native.LEGACY_MEASUREMENT_CONTRACTS))
            for identity in old["identities"].values():
                identity.pop("measurement_contract")
                identity.update(protocol=protocol, adapter_sha256=adapter_sha256, adaptive_protocol_sha256=adaptive)
            old["frozen_prediction"]["identities"] = old["identities"]
            frozen = old["frozen_prediction"]
            frozen["sha256"] = native.digest({k: v for k, v in frozen.items() if k != "sha256"})
            for hold in old["full_holdouts"]:
                hold["frozen_prediction_sha256"] = frozen["sha256"]
            old["anchor_sha256"] = native.digest({k: v for k, v in old.items() if k != "anchor_sha256"})
            return old

        known_sha = next(iter(native.LEGACY_MEASUREMENT_CONTRACTS))[1]
        legacy = legacy_fixture(known_sha)
        self.assertTrue(native.historical_anchor_proof(legacy, case))
        self.assertTrue(native.anchor_compatible(legacy, identities, case))
        unknown = legacy_fixture("unreviewed adapter")
        self.assertTrue(native.historical_anchor_proof(unknown, case))
        self.assertFalse(native.anchor_compatible(unknown, identities, case))

    def test_confirmation_advances_only_to_next_declared_look_and_respects_budget(self):
        case = {"id": "synthetic", "scope": "contract test", "target_seconds": 800.,
                "holdout_seconds": 400., "training_seconds": [100., 200.], "witnesses": []}
        full_seeds = []

        def fake_run(binary, selected, duration, seed, **kwargs):
            result = {**record(int(duration)), "seed": seed, "requested_seconds": duration}
            if duration == selected["target_seconds"]:
                full_seeds.append(seed)
                factor = math.exp({840000: .02, 841000: -.02}.get(seed, 0.))
                result["full_test_wall_seconds"] *= factor
                result["stage_seconds"] = {k: v*factor for k, v in result["stage_seconds"].items()}
            return result

        policy = AdaptivePolicy(looks=(3, 5))
        with (patch.object(native, "identity", side_effect=mock_identity),
              patch.object(native, "target_geometry", return_value={"performance_geometry": {"capture_samples": 800}}),
              patch.object(native, "run_native", side_effect=fake_run),
              patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
            bounded = native.measure_native_case("reference", "candidate", case, policy,
                                                 deadline=time.monotonic()+.2)
            self.assertEqual(bounded["current_validation"]["confirmation_stop_reason"], "budget_limit")
            self.assertEqual(full_seeds, [])
            result = native.measure_native_case("reference", "candidate", case, policy)
        self.assertEqual(full_seeds, [seed for seed in range(840000, 845000, 1000) for _ in range(2)])
        self.assertEqual([(look["sample_count_per_source"], look["status"])
                          for look in result["anchors"]["confirmation_looks"]], [(3, "inconclusive"), (5, "validated")])
        self.assertTrue(native.anchor_compatible(result["anchors"], result["identities"], case))

    def test_routine_uses_one_full_pair_and_keeps_historical_proof_untouched(self):
        case = {"id": "synthetic", "scope": "contract test", "target_seconds": 800.,
                "holdout_seconds": 400., "training_seconds": [100., 200.], "witnesses": []}
        full_seeds = []
        commitments = []
        sentinel_scale = 1.
        training_jitter = False

        def fake_run(binary, selected, duration, seed, **kwargs):
            self.assertNotEqual(duration, selected["holdout_seconds"])
            result = {**record(int(duration)), "seed": seed, "requested_seconds": duration}
            if training_jitter and seed >= 1_000_000:
                scale = (1., .99, 1.01)[((seed - 1_000_000)//1000) % 3]
                for stage in result["stage_seconds"]:
                    result["stage_seconds"][stage] *= scale
                result["full_test_wall_seconds"] *= scale
            if duration == selected["target_seconds"]:
                full_seeds.append(seed)
            if seed == 900000:
                self.assertEqual(commitments[-1]["conditional_model"]["status"], "conditional_contract_ready")
                if str(binary) == "candidate":
                    extra_wall = (sentinel_scale - 1.) * result["full_test_wall_seconds"]
                    result["stage_seconds"]["process_startup_output"] += extra_wall
                    result["full_test_wall_seconds"] += extra_wall
            return result

        policy = AdaptivePolicy(looks=(3, 5))
        with (patch.object(native, "identity", side_effect=mock_identity),
              patch.object(native, "target_geometry", return_value={"performance_geometry": {"capture_samples": 800}}),
              patch.object(native, "run_native", side_effect=fake_run),
              patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
            historical = native.measure_native_case("reference", "candidate", case, policy)["anchors"]
            untouched = copy.deepcopy(historical)
            full_seeds.clear()
            training_jitter = True
            routine_policy = replace(policy, stopping_rule="decision")
            reused = native.measure_native_case("reference", "candidate", case, routine_policy,
                validation_mode="routine", anchor=historical, freeze_callback=commitments.append)
            self.assertEqual(full_seeds, [900000, 900000])
            self.assertEqual(reused["prediction"]["model_validation"], "historically_supported_conditional")
            self.assertEqual(reused["comparison"]["gate"], "pass")
            self.assertEqual(reused["prediction"]["sample_count"], 3)
            self.assertEqual(reused["prediction"]["stop_reason"], "conditional_decision_ready_for_endpoint")
            self.assertFalse(reused["prediction"]["timing_precision_reached"])
            self.assertFalse(commitments[-1]["conditional_readiness"]["performance_pass_eligible"])
            self.assertFalse(reused["comparison"]["training_timing_uncertainty_included"])
            self.assertIsNone(reused["current_validation"]["qualification"])
            self.assertEqual(reused["current_validation"]["confirmation_looks"], [])
            self.assertEqual(reused["anchors"], untouched)
            self.assertEqual(historical, untouched)

            full_seeds.clear()
            sentinel_scale = 1.6
            contradicted = native.measure_native_case("reference", "candidate", case, routine_policy,
                validation_mode="routine", anchor=historical, freeze_callback=commitments.append)
        self.assertEqual(full_seeds, [900000, 900000])
        self.assertFalse(contradicted["prediction_valid"])
        self.assertEqual(contradicted["comparison"]["gate"], "unvalidated")
        self.assertEqual(contradicted["current_validation"]["conditional_model"]["status"],
                         "unvalidated_request_focused_qualification")
        self.assertEqual(contradicted["anchors"], untouched)

    def test_unstable_training_fit_retains_timing_without_model_pass(self):
        case = {"id": "synthetic", "scope": "contract test", "target_seconds": 800.,
                "holdout_seconds": 400., "training_seconds": [100., 200.], "witnesses": []}

        def fake_run(binary, selected, duration, seed, **kwargs):
            self.assertLess(duration, selected["target_seconds"])
            receiver = .05 if seed >= 1_000_000 and duration == 200. else None
            return record(int(duration), receiver=receiver)

        with (patch.object(native, "identity", side_effect=mock_identity),
              patch.object(native, "target_geometry", return_value={"performance_geometry": {"capture_samples": 800}}),
              patch.object(native, "run_native", side_effect=fake_run),
              patch.object(native, "compare_native_outputs", return_value={"eligible": True})):
            result = native.measure_native_case("reference", "candidate", case, AdaptivePolicy(looks=(3, 5)))
        self.assertFalse(result["prediction_valid"])
        self.assertEqual(result["prediction"]["stop_reason"], "unstable_duration_fit")
        self.assertEqual(result["comparison"]["gate"], "unvalidated")
        self.assertEqual([len(rows) for rows in result["raw"].values()], [1, 1])
        self.assertEqual(result["frozen_predictions"], [])

    def test_case_manifest_preserves_full_banks_and_duration_only_shortening(self):
        cases = native.scenarios("ci")
        self.assertGreaterEqual(len(cases), 8)
        by_id = {case["id"]: case for case in cases}
        self.assertNotIn("training_seconds", by_id["native-flat"])
        self.assertNotIn("training_seconds", by_id["native-fhss-selective-burst"])
        vlf = by_id["native-vlf-low-cn0"]
        self.assertEqual(vlf["training_seconds"], [vlf["target_seconds"]/32, vlf["target_seconds"]/4])
        self.assertEqual(vlf["holdout_seconds"], vlf["target_seconds"]/2)
        self.assertEqual(len({c["id"] for c in cases}), len(cases))
        for case in cases:
            self.assertIn("complete", case["witnesses"])
            args = native._argv("/tmp/binary", case, case["target_seconds"], 7, 2)
            self.assertEqual(args[1:1+len(case["args"])], case["args"])
            self.assertNotIn("--candidate-limit", args)
            if "training_seconds" in case:
                self.assertLess(case["training_seconds"][0], case["training_seconds"][1])
                self.assertLess(case["training_seconds"][1], case["holdout_seconds"])
                self.assertLess(case["holdout_seconds"], case["target_seconds"])
        with self.assertRaises(ValueError):
            native.scenarios("unknown")

    def test_anchor_identity_separates_same_size_affinity_masks(self):
        import triad_perf_hardware as hardware

        cpu_topology = {0: ("0", "0,6"), 1: ("1", "1,7"), 6: ("0", "0,6")}

        def topology_read(path):
            if "/topology/" not in path:
                return None
            cpu = int(Path(path).parent.parent.name.removeprefix("cpu"))
            core, siblings = cpu_topology[cpu]
            return {"core_id": core, "physical_package_id": "0",
                    "thread_siblings_list": siblings}[Path(path).name]

        case = {"id": "topology-control", "target_seconds": 8.}
        with (patch.object(hardware.os, "sched_getaffinity", side_effect=[{6, 0}, {1, 0}], create=True),
              patch.object(hardware, "_read", side_effect=topology_read)):
            smt = native.identity(__file__, case, 2)
            separate = native.identity(__file__, case, 2)
        self.assertEqual(smt["binary_sha256"], separate["binary_sha256"])
        self.assertEqual(smt["environment"]["affinity_cpu_count"], separate["environment"]["affinity_cpu_count"])
        self.assertEqual(smt["environment"]["affinity_cpus"], [0, 6])
        self.assertEqual(separate["environment"]["affinity_cpus"], [0, 1])
        self.assertEqual([p["thread_siblings_list"] for p in smt["environment"]["affinity_topology"]],
                         ["0,6", "0,6"])
        self.assertEqual([p["core_id"] for p in separate["environment"]["affinity_topology"]], ["0", "1"])
        self.assertNotEqual(smt, separate)

    def test_legacy_geometry_fallback_is_static_clock_only(self):
        config = {"configuration": {"duration": {"source_support_seconds": 100.}}}
        training = {"batch_trials": 2, "regime": {"sample_rate": 64,
                    "channel_model": {"sample_clock_reference": "RX0ppm; TX carries declared relative error"}},
                    "native_results": [{"capture_samples": 1000, "source_samples": 990}]}
        self.assertEqual(native.target_rows_from_configuration(config, training), 12820)
        training["regime"]["channel_model"]["sample_clock_reference"] = "independent endpoints"
        with self.assertRaises(native.NativePerformanceError):
            native.target_rows_from_configuration(config, training)
        config["performance_geometry"] = {"capture_samples": 6410}
        self.assertEqual(native.target_rows_from_configuration(config, training), 12820)


if __name__ == "__main__":
    unittest.main()

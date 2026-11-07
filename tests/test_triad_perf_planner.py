"""Planner direct-target identities, work guards and decision replay."""
import copy
from dataclasses import asdict
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import triad_perf_planner as planner
from triad_perf_adaptive import AdaptivePolicy, regression_profile


def native(case, identity):
    return {"schema": "triad-planner-perf-native-v1", "case": case["id"],
            "driver_sha256": identity["driver_sha256"], "production_sha256": identity["production_sha256"],
            "repetitions": case["repetitions"], "scenario_evaluations": case["repetitions"]*case["scenarios_per_repetition"],
            "checksum": 3., "operation_seconds": .1,
            "arithmetic": "fp64_and_native_long_double_bounds", "internal_iterations_instrumented": False,
            "outputs": [{"name": name, "status": "complete", "value": 1., "coordinate": 7}
                        for name in case["outputs"]], "complete": True}


class PlannerTests(unittest.TestCase):
    def test_native_planner_dependencies_match_cmake_in_order(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        prefix = "  set(_triad_planner_dependencies\n"
        self.assertEqual(cmake.count(prefix), 1)
        declaration = cmake.split(prefix, 1)[1].split(")", 1)[0]
        self.assertEqual(tuple(declaration.split()), planner.DEPENDENCIES)
    def setUp(self):
        self.sources = {side: {"driver_sha256": "driver", "production_sha256": side}
                        for side in ("reference", "candidate")}
        self.identity = {"sources": self.sources, "environment": {"fixture": True}}
        self.case = planner.manifest()["cases"][0]

    def invoke(self, binary, case, identity, deadline, receipt_path=None):
        result = {"argv": [str(binary)], "wall_seconds": 1., "native": native(case, identity)}
        if receipt_path is not None:
            planner.write_json(receipt_path, {"argv": result["argv"], "wall_seconds": 1.,
                "returncode": 0, "stdout": json.dumps(result["native"]), "stderr": "", "timeout": False, "error": None})
        return result

    def test_manifest_is_full_fixed_work_not_a_horizon_model(self):
        manifest = planner.manifest()
        self.assertEqual(len(manifest["cases"]), 6)
        self.assertFalse(manifest["internal_repetitions_are_independent_samples"])
        self.assertIn("complete native subprocess", manifest["timing_boundary"])
        self.assertIn("not instrumented", manifest["work_counter_limits"])

    def test_embedded_sources_work_counts_and_numeric_outputs_are_independent_guards(self):
        row = native(self.case, self.sources["candidate"])
        self.assertTrue(planner.validate_native(row, self.case, self.sources["candidate"]))
        for key, value in (("production_sha256", "wrong"), ("scenario_evaluations", 1), ("complete", False),
                           ("checksum", float("nan")), ("arithmetic", "fp32")):
            broken = copy.deepcopy(row); broken[key] = value
            with self.assertRaises(ValueError):
                planner.validate_native(broken, self.case, self.sources["candidate"])
        changed = copy.deepcopy(row); changed["outputs"][0]["coordinate"] += 1
        with self.assertRaisesRegex(ValueError, "coordinate"):
            planner.numerical_agreement(row, changed)

    def test_direct_target_stops_on_decision_not_timing_width(self):
        def varying(binary, case, identity, deadline, receipt_path=None):
            pair = len(calls)//2
            calls.append(binary)
            result = self.invoke(binary, case, identity, deadline)
            result["wall_seconds"] = (pair+1)**2
            return result
        calls = []
        with tempfile.TemporaryDirectory() as tmp, patch.object(planner, "invoke", varying):
            report = planner.measure_case(self.case, {"reference": "ref", "candidate": "cand"},
                self.identity, AdaptivePolicy(stopping_rule="decision", **regression_profile("normal")),
                time.monotonic()+10, tmp)
        self.assertEqual(report["comparison"]["gate"], "pass")
        self.assertEqual(report["full_target_observations_per_source"], 5)
        self.assertFalse(report["adaptive"]["timing_precision_reached"])
        self.assertIsNone(report["abbreviation_model"])

    def test_external_cost_slowdown_cannot_hide_in_native_timer(self):
        def slow(binary, case, identity, deadline, receipt_path=None):
            result = self.invoke(binary, case, identity, deadline)
            if binary == "cand": result["wall_seconds"] = 2.
            return result
        with tempfile.TemporaryDirectory() as tmp, patch.object(planner, "invoke", slow):
            report = planner.measure_case(self.case, {"reference": "ref", "candidate": "cand"},
                self.identity, AdaptivePolicy(stopping_rule="decision", **regression_profile("ci")),
                time.monotonic()+10, tmp)
        self.assertEqual(report["comparison"]["gate"], "performance_regression")
        self.assertEqual(report["pairs"][0]["reference"]["native"]["operation_seconds"],
                         report["pairs"][0]["candidate"]["native"]["operation_seconds"])

    def test_operation_slowdown_cannot_hide_in_unchanged_process_time(self):
        def slow_operation(binary, case, identity, deadline, receipt_path=None):
            result = self.invoke(binary, case, identity, deadline)
            if binary == "cand": result["native"]["operation_seconds"] = .2
            return result
        with tempfile.TemporaryDirectory() as tmp, patch.object(planner, "invoke", slow_operation):
            report = planner.measure_case(self.case, {"reference": "ref", "candidate": "cand"},
                self.identity, AdaptivePolicy(stopping_rule="decision", **regression_profile("normal")),
                time.monotonic()+10, tmp)
        self.assertEqual(report["adaptive"]["performance_gate"], "pass")
        self.assertEqual(report["compute_adaptive"]["performance_gate"], "performance_regression")
        self.assertEqual(report["comparison"]["gate"], "performance_regression")
        self.assertEqual(report["metric_policy"]["pass_alpha"], .15)
        self.assertEqual(report["metric_policy"]["failure_alpha"], .025)

    def test_invalid_native_stdout_and_nonzero_exit_retain_raw_receipts(self):
        for returncode, stdout in ((0, "not JSON"), (7, "partial target output")):
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)/"raw.json"
                process = subprocess.CompletedProcess(["fixture"], returncode, stdout, "diagnostic stderr")
                with patch.object(planner.subprocess, "run", return_value=process):
                    with self.assertRaises(ValueError):
                        planner.invoke("fixture", self.case, self.sources["candidate"], time.monotonic()+10, path)
                receipt = json.loads(path.read_text())
                self.assertEqual(receipt["returncode"], returncode)
                self.assertEqual(receipt["stdout"], stdout)
                self.assertEqual(receipt["stderr"], "diagnostic stderr")
                self.assertGreater(receipt["wall_seconds"], 0)

    def test_numerical_failure_preserves_both_invocations_and_failed_pair(self):
        def different(binary, case, identity, deadline, receipt_path=None):
            result = self.invoke(binary, case, identity, deadline, receipt_path)
            if binary == "cand":
                result["native"]["outputs"][0]["coordinate"] += 1
            return result
        with tempfile.TemporaryDirectory() as tmp, patch.object(planner, "invoke", different):
            with self.assertRaisesRegex(ValueError, "coordinate"):
                planner.measure_case(self.case, {"reference": "ref", "candidate": "cand"},
                    self.identity, AdaptivePolicy(stopping_rule="decision", **regression_profile("normal")),
                    time.monotonic()+10, tmp)
            pair = json.loads((Path(tmp)/self.case["id"]/"pair-001.json").read_text())
            self.assertEqual(pair["status"], "failed")
            self.assertIn("reference", pair)
            self.assertIn("candidate", pair)
            self.assertEqual(len(pair["raw_receipts"]), 2)
            for receipt in pair["raw_receipts"].values():
                self.assertIsNotNone(receipt["sha256"])

    def test_bounds_thread_environment_is_explicit_and_preserves_other_controls(self):
        case = planner.manifest()["bounds_cases"][0]
        process = subprocess.CompletedProcess(["fixture"], 0, "{}", "")
        with patch.object(planner.subprocess, "run", return_value=process) as run, \
             patch.object(planner, "validate_bound"), patch.dict(os.environ, {"OPENBLAS_NUM_THREADS": "99", "OMP_PROC_BIND": "spread"}):
            with self.assertRaises(KeyError):
                planner.invoke_bound(ROOT, ROOT, case, {}, "candidate", time.monotonic()+10)
            environment = run.call_args.kwargs["env"]
            self.assertEqual(environment["OPENBLAS_NUM_THREADS"], "2")
            self.assertEqual(environment["OMP_NUM_THREADS"], "2")
            self.assertEqual(environment["OMP_DYNAMIC"], "FALSE")
            self.assertEqual(environment["OMP_PROC_BIND"], "spread")

    def test_full_inventory_replay_binds_current_sources_plan_and_raw_pairs(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)/"run"
            with patch.object(planner, "identities", return_value=self.identity), \
                 patch.object(planner, "invoke", self.invoke):
                code = planner.main(["--reference-root", str(ROOT), "--candidate-binary", "cand",
                                     "--reference-binary", "ref", "--output", str(output)])
                self.assertEqual(code, 0)
                report = json.loads((output/"summary.json").read_text())
                context = dict(candidate_root=ROOT, reference_root=ROOT, candidate_binary="cand",
                               reference_binary="ref", regression_profile="normal", report_path=output/"summary.json")
                receipt = planner.validate_routine_report(report, context)
                self.assertTrue(receipt["valid"], receipt)
                self.assertEqual(receipt["status"], "passed")
                self.assertEqual(receipt["gap_ids"], [planner.GAP])
                partial = copy.deepcopy(report); partial["cases"].pop()
                self.assertFalse(planner.validate_routine_report(partial, context)["valid"])
                wrong = copy.deepcopy(report); wrong["cases"][0]["pairs"][0]["candidate"]["wall_seconds"] = 10
                self.assertFalse(planner.validate_routine_report(wrong, context)["valid"])
                changed = copy.deepcopy(self.identity); changed["environment"]["fixture"] = False
                with patch.object(planner, "identities", return_value=changed):
                    self.assertFalse(planner.validate_routine_report(report, context)["valid"])

    def test_reference_overlay_copies_only_the_test_target_and_preserves_production(self):
        with tempfile.TemporaryDirectory() as tmp:
            ref = Path(tmp)
            for name in planner.DEPENDENCIES:
                (ref/name).parent.mkdir(parents=True, exist_ok=True)
                (ref/name).write_text("unchanged reference production " + name)
            (ref/"tools").mkdir()
            cmake = ('  add_executable(triad_oscillator_study EXCLUDE_FROM_ALL tools/old.cpp)\n'
                     'if(_target STREQUAL "triad_long" OR _target STREQUAL "triad_oscillator_study")\nendif()\n')
            (ref/"CMakeLists.txt").write_text(cmake)
            receipt = planner.prepare_reference_driver(ROOT, ref)
            self.assertTrue(receipt["production_unchanged"])
            self.assertEqual((ref/planner.DRIVER).read_bytes(), (ROOT/planner.DRIVER).read_bytes())
            again = planner.prepare_reference_driver(ROOT, ref)
            self.assertEqual(receipt["reference_cmake_after_sha256"], again["reference_cmake_after_sha256"])
            self.assertIn('_target STREQUAL "triad_planner_perf"', (ref/"CMakeLists.txt").read_text())

    def test_gaussian_design_work_and_numeric_checks_are_not_native_planner_witnesses(self):
        case = planner.manifest()["bounds_cases"][0]
        identity = {"workspace": {"files": {"manifest.json": "fixture"}, "blocks": 3,
                                  "candidates": 2, "pairs": 1, "selected_candidates": [0, 1],
                                  "sample_count": 12, "sample_rate": 2},
                    "sources": {"candidate": dict.fromkeys(planner.BOUNDS_DEPENDENCIES, "source")},
                    "libraries": {"torch": "torch-test"}}
        row = {"schema": "triad-gaussian-fixed-bank-bounds-v1", "script_sha256": "source",
               "gaussian_state_sha256": "source", "workspace_module_sha256": "source",
               "source_manifest_sha256": "fixture", "observation_blocks": 3, "candidate_count": 2,
               "pair_designs": 1, "selected_candidates": [0, 1], "chernoff_s_grid": [],
               "null_calibration": "markov", "hidden_channel_means_or_signal_energies_read": False,
               "torch_version": "torch-test", "stationary_coefficient_powers": [.5, .5],
               "full_exported_capture_seconds": 6, "wall_seconds": .2}
        planner.validate_bound(row, case, identity, "candidate")
        changed = {**row, "pair_designs": 0}
        with self.assertRaisesRegex(ValueError, "work"):
            planner.validate_bound(changed, case, identity, "candidate")
        with self.assertRaisesRegex(ValueError, "numerical"):
            planner.bounds_agreement(row, {**row, "full_exported_capture_seconds": 3})

    def test_retained_gaussian_design_cli_completes_both_fixed_banks(self):
        workspace = os.environ.get("TRIAD_PLANNER_BOUNDS_WORKSPACE")
        binary = os.environ.get("TRIAD_PLANNER_BINARY")
        if not workspace or not binary:
            self.skipTest("set TRIAD_PLANNER_BOUNDS_WORKSPACE and TRIAD_PLANNER_BINARY for retained-input checks")
        identity = planner.identities(ROOT, ROOT, binary, binary, workspace)["bounds"]
        for case in planner.manifest()["bounds_cases"]:
            result = planner.invoke_bound(ROOT, workspace, case, identity, "candidate", time.monotonic()+20)
            self.assertEqual(result["bounds"]["observation_blocks"], identity["workspace"]["blocks"])
            self.assertEqual(result["bounds"]["pair_designs"], identity["workspace"]["pairs"])

    def test_native_build_matches_source_and_executes_every_branch_bundle(self):
        binary = os.environ.get("TRIAD_PLANNER_BINARY")
        if not binary:
            self.skipTest("set TRIAD_PLANNER_BINARY for focused native driver checks")
        identity = planner.source_identity(ROOT, binary)
        for original in planner.manifest()["cases"]:
            case = {**original, "repetitions": 1}
            row = planner.invoke(binary, case, identity, time.monotonic()+10)
            self.assertGreater(row["wall_seconds"], 0)
            self.assertEqual(row["native"]["scenario_evaluations"], case["scenarios_per_repetition"])


if __name__ == "__main__":
    unittest.main()

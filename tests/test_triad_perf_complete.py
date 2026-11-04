"""Evidence aggregation and process-boundary checks; no timing campaign here."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_complete as complete


def case(name="legacy", **changes):
    row = {"id": name, "prediction_valid": True, "comparison": {"gate": "pass"},
           "adaptive": {"timing_precision_reached": True}}
    row.update(changes)
    return row


def legacy(**changes):
    row = {"cases": [case()], "complete_required_coverage": True,
           "full_profile_case_ids": ["legacy"],
           "hardware_health": {"status": "raw_hardware_screen", "warning": False,
                               "rows": [{"time_equivalent_ratio": 1.}]}}
    row.update(changes)
    return row


def report():
    return {"functional_commands": [{"argv": ["mapped-test"], "status": "passed", "functional_evidence": {"status": "passed"}}], "functional_scope": "test scope",
            "functional_defaults_requested": True, "required_default_functional_commands": [["mapped-test"]],
            "native_test_inventory": {"status": "passed", "present": sorted(complete.NATIVE_NAMES)},
            "coverage_receipts": [{"status": "source_rows_matched_historical", "missing_expected_sources": [],
                                   "declared_dependency_status": "matched", "counter_quality": {"status": "valid"}} for _ in range(2)],
            "errors": []}


class CompleteSuiteTests(unittest.TestCase):
    def test_extension_closures_require_the_exact_maintained_mapping(self):
        matrix = {"rows": [{"row_id": "resource.memory-thresholds", "required_gaps": ["boundary"],
            "required_extension": {"id": "resource", "gap_ids": ["resource.memory-thresholds"]}},
            {"row_id": "unmapped", "required_gaps": ["other"]}]}
        gaps = complete.inventory_gaps(matrix)
        validation = {"valid": True, "status": "passed", "errors": [],
                      "gap_ids": ["resource.memory-thresholds", "unmapped"]}
        receipt = {"validation": validation, "report_sha256": "verified-report", "command": {"status": "passed", "exit_code": 0}}
        remaining, closed = complete.resolve_extension_gaps(gaps, {"resource": receipt})
        self.assertEqual([row["id"] for row in remaining], ["unmapped"])
        self.assertEqual(closed, [{"id": "resource.memory-thresholds", "extension": "resource", "report_sha256": "verified-report"}])
        for change in ({"valid": False}, {"status": "inconclusive"}, {"status": "not_applicable"},
                       {"status": "failed"}, {"errors": ["changed source"]}, {"gap_ids": ["unmapped"]}):
            bad = {**receipt, "validation": {**validation, **change}}
            self.assertEqual(len(complete.resolve_extension_gaps(gaps, {"resource": bad})[0]), 2)
        bad_exit = {**receipt, "command": {"status": "failed", "exit_code": 2}}
        self.assertEqual(len(complete.resolve_extension_gaps(gaps, {"resource": bad_exit})[0]), 2)
        matrix["rows"][0]["required_extension"]["gap_ids"] = ["unrelated"]
        with self.assertRaisesRegex(ValueError, "invalid extension mapping"):
            complete.inventory_gaps(matrix)

    def test_cold_qualification_requires_the_validated_lifecycle_receipt(self):
        evidence = report()
        evidence["extensions"] = {"lifecycle": {"command": {"status": "passed", "exit_code": 0}, "validation": {"valid": True, "status": "passed",
            "errors": [], "gap_ids": [], "cold_latency_qualified": True}}}
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertTrue(result["complete_scope_qualification"])
        self.assertEqual(result["cold_latency_qualification"], "qualified")
        evidence["extensions"]["lifecycle"]["validation"]["valid"] = False
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertFalse(result["complete_scope_qualification"])
        self.assertNotEqual(result["cold_latency_qualification"], "qualified")

    def test_direct_stability_review_preserves_work_and_cold_but_blocks_release(self):
        evidence = report()
        gap = "scheduler.concurrency-and-batches"
        evidence["inventory_unresolved"] = [{"id": gap, "required_extension": {"id": "lifecycle", "gap_ids": [gap]}}]
        review = {"id": "sustained_stability", "case_ids": ["mixed-cpu"],
                  "basis": "unchanged last/first warm-cycle diagnostic >1.10; direct timings include variation"}
        validation = {"valid": True, "status": "passed", "errors": [], "gap_ids": [gap],
            "cold_latency_qualified": True, "cold_validation": {"valid": True, "status": "passed", "case_ids": ["cold-cpu-phase"], "errors": []},
            "review_required": [review], "outcomes": {"sustained_observation": "complete", "sustained_stability": "review_required"}}
        evidence["extensions"] = {"lifecycle": {"command": {"status": "passed", "exit_code": 0}, "validation": validation}}
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertTrue(result["maintained_workload_scope_complete"])
        self.assertTrue(result["warm_target_decisions_complete"])
        self.assertEqual(result["cold_latency_qualification"], "qualified")
        self.assertEqual(result["remaining_inventory_gaps"], [])
        self.assertEqual(result["release_review_requirements"], [{"extension": "lifecycle", **review}])
        self.assertFalse(result["complete_scope_qualification"])
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["exit_code"], 2)
        self.assertTrue(any("extension review required: lifecycle/sustained_stability" in b for b in result["scope_blockers"]))
        degraded = legacy(hardware_health={"status": "raw_hardware_screen", "warning": True, "rows": [{"time_equivalent_ratio": 1.4}]})
        result = complete.summarize(evidence, degraded, {}, [])
        self.assertTrue(result["maintained_workload_scope_complete"])
        self.assertEqual(result["outcomes"]["hardware_health"]["status"], "degraded_screen")
        self.assertEqual(len(result["release_review_requirements"]), 1)

    def test_independent_cold_proof_survives_only_completed_mixed_local_failure(self):
        evidence = report()
        receipt = {"command": {"status": "failed", "exit_code": 2}, "validation": {"valid": False, "status": "inconclusive",
            "errors": ["mixed-local failure"], "gap_ids": [], "cold_latency_qualified": True,
            "cold_validation": {"valid": True, "status": "passed", "case_ids": ["cold-cpu-phase"], "errors": []}}}
        evidence["extensions"] = {"lifecycle": receipt}
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertEqual(result["cold_latency_qualification"], "qualified")
        self.assertFalse(result["complete_scope_qualification"])
        self.assertFalse(result["maintained_workload_scope_complete"])
        for command in ({"status": "timed_out", "exit_code": 2}, {"status": "failed", "exit_code": 1}, {}):
            self.assertFalse(complete.cold_receipt_qualified({**receipt, "command": command}))
        receipt["validation"]["cold_validation"]["errors"] = ["source changed"]
        self.assertFalse(complete.cold_receipt_qualified(receipt))

    def test_extension_failure_and_missing_execution_cannot_hide_behind_core_passes(self):
        evidence = report()
        evidence["configured_extensions"] = ["resource", "planner"]
        evidence["extensions"] = {"resource": {"validation": {"valid": True, "status": "failed",
            "errors": [], "gap_ids": [], "outcomes": {"performance_regression": "failed"}}}}
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertTrue(result["warm_target_decisions_complete"])
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["complete_scope_qualification"])
        self.assertTrue(any("planner, resource" in reason for reason in result["scope_blockers"]))

    def test_extension_validation_calls_current_checker_instead_of_trusting_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"summary.json"
            path.write_text(json.dumps({"status": "passed", "complete_scope": True}))
            spec = {"id": "resource", "module": "triad_perf_resource", "context": {"report_path": str(path)}}
            expected = {"valid": False, "status": "unqualified", "gap_ids": [], "errors": ["proof mismatch"]}
            module = Mock()
            module.validate_routine_report.return_value = expected
            with patch.object(complete.importlib, "import_module", return_value=module) as load:
                self.assertEqual(complete.validate_extension_report(spec), expected)
                load.assert_called_once_with("triad_perf_resource")
                module.validate_routine_report.assert_called_once_with({"status": "passed", "complete_scope": True}, spec["context"])
                module.validate_routine_report.return_value = {"status": "passed"}
                with self.assertRaisesRegex(ValueError, "invalid receipt"):
                    complete.validate_extension_report(spec)
                for fields in ({"review_required": None}, {"review_required": [{"id": "missing-fields"}]},
                               {"cold_validation": {"valid": True, "status": "passed"}}):
                    module.validate_routine_report.return_value = {**expected, **fields}
                    with self.assertRaises(ValueError):
                        complete.validate_extension_report(spec)

    def test_new_mapped_test_file_requires_new_coverage_dependency_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"tools").mkdir()
            (root/"tests").mkdir()
            source = root/"tools/runtime.py"
            old_test = root/"tests/test_old.py"
            new_test = root/"tests/test_new.py"
            for path in (source, old_test, new_test):
                path.write_text("pass\n")
            matrix = {"rows": [{"source_files": ["tools/runtime.py"],
                                "cheap_functional_tests": ["tests/test_old.py", "tests/test_new.py::test_added"]}]}
            required = complete.coverage_dependencies(root, matrix, ["tools/runtime.py"], "python")
            self.assertIn("tests/test_new.py", required)
            receipt = root/"coverage.json"
            receipt.write_text(json.dumps({"schema": "datapump.triad.python-branch-coverage",
                "sources": {"tools/runtime.py": {"sha256": complete.digest(source)}},
                "dependency_sha256": {"tests/test_old.py": complete.digest(old_test)}}))
            result = complete.check_coverage(receipt, root, ["tools/runtime.py"], required)
            self.assertEqual(result["missing_dependency_identities"], ["tests/test_new.py"])
            self.assertEqual(result["historical_applicability"], "unqualified")

    def test_explicit_required_gaps_are_separate_from_retained_limitations(self):
        matrix = {"rows": [
            {"row_id": "direct-full", "required_gaps": [], "unresolved": ["No scaling forecast"],
             "limitations": ["This direct target does not extrapolate particle counts."]},
            {"row_id": "missing-target", "required_gaps": ["A declared full target remains unmeasured."],
             "unresolved": ["Historical prose remains unchanged."], "limitations": []}],
            "unrepresented_expensive_boundaries": [{"id": "cross-device", "required_gaps": [],
                "scope": "No universal architecture transfer.", "limitations": ["Unmeasured devices remain outside this target."]}]}
        gaps = complete.inventory_gaps(matrix)
        self.assertEqual([row["id"] for row in gaps], ["missing-target"])
        self.assertEqual(gaps[0]["disposition"], "explicit_required_gaps")
        limits = complete.inventory_limitations(matrix)
        self.assertEqual([row["id"] for row in limits], ["direct-full", "cross-device"])
        self.assertEqual(limits[0]["legacy_prose"], ["No scaling forecast"])

    def test_legacy_or_malformed_inventory_cannot_silently_drop_required_work(self):
        matrix = {"rows": [{"row_id": "legacy", "unresolved": ["unknown coverage"]}],
                  "unrepresented_expensive_boundaries": [{"id": "legacy-boundary", "scope": "unknown regime"}]}
        self.assertEqual(len(complete.inventory_gaps(matrix)), 2)
        self.assertTrue(all(row["disposition"] == "legacy_fail_closed" for row in complete.inventory_gaps(matrix)))
        matrix["rows"][0]["required_gaps"] = None
        with self.assertRaisesRegex(ValueError, "gap disposition"):
            complete.inventory_gaps(matrix)

    def test_precision_goal_miss_does_not_replace_supported_regression_gate(self):
        row = case("sampled", adaptive={"timing_precision_reached": False},
                   comparison={"gate": "pass", "full_time_ratio_interval": [.7774, 1.2686]})
        result = complete.summarize(report(), legacy(), {"sampled": row}, ["sampled"])
        self.assertTrue(result["warm_target_decisions_complete"])
        self.assertFalse(result["precision_target_met"])
        self.assertEqual(result["outcomes"]["timing_precision"]["status"], "inconclusive")
        self.assertEqual(result["timing_cases"]["rows"][-1]["comparison"]["full_time_ratio_interval"], [.7774, 1.2686])
        self.assertIn("not a universal", result["warm_decision_statistical_scope"])
        self.assertFalse(result["complete_scope_qualification"])  # Cold proof is still missing.

    def test_decision_policy_reports_per_test_confidence_separately_from_scope(self):
        evidence = report()
        evidence["requested_stopping_rule"] = "decision"
        evidence["regression_profile"] = {"name": "normal", "pass_alpha": .15,
                                           "failure_alpha": .05, "slowdown_threshold": 1.30}
        result = complete.summarize(evidence, legacy(), {}, [])
        self.assertTrue(result["warm_target_decisions_complete"])
        self.assertFalse(result["complete_scope_qualification"])
        note = result["warm_decision_statistical_scope"]
        self.assertIn("per-test one-sided pass alpha=0.15", note)
        self.assertIn("slowdown ratio 1.3", note)
        self.assertIn("failure-declaration alpha=0.05", note)
        self.assertIn("not runtime-width targets or a whole-suite confidence guarantee", note)
        evidence["regression_profile"] = {"name": "ci", "pass_alpha": .30,
                                           "failure_alpha": .05, "slowdown_threshold": 1.70}
        ci = complete.summarize(evidence, legacy(), {}, [])
        self.assertIn("profile ci", ci["warm_decision_statistical_scope"])
        self.assertIn("pass alpha=0.3", ci["warm_decision_statistical_scope"])
        self.assertIn("slowdown ratio 1.7", ci["warm_decision_statistical_scope"])

    def test_precise_measurement_does_not_override_invalid_model_or_unresolved_gate(self):
        for row in (case("sampled", prediction_valid=False), case("sampled", comparison={"gate": "inconclusive"})):
            result = complete.summarize(report(), legacy(), {"sampled": row}, ["sampled"])
            self.assertTrue(result["precision_target_met"])
            self.assertFalse(result["warm_target_decisions_complete"])
            self.assertFalse(result["complete_scope_qualification"])

    def test_child_environment_exactly_matches_frozen_legacy_helper(self):
        tree = ast.parse((ROOT/"tools/triad-performance.py").read_text())
        node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "child_environment")
        namespace = {"os": os}
        exec(compile(ast.Module(body=[node], type_ignores=[]), "legacy-environment-helper", "exec"), namespace)
        for inherited in ({"CUSTOM": "unchanged"}, {"PYTHONUNBUFFERED": "user-explicit", "OMP_NUM_THREADS": "99"}):
            with patch.dict(os.environ, inherited, clear=True):
                self.assertEqual(complete.child_environment(2), namespace["child_environment"](2))
        with patch.dict(os.environ, {}, clear=True):
            self.assertNotIn("PYTHONUNBUFFERED", complete.child_environment(2))

    def test_named_method_must_execute_even_when_other_tests_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"stdout"
            receipt = {"status": "passed", "stdout": str(path)}
            path.write_text("test_other (__main__.Checks.test_other) ... ok\n\nRan 1 test in 0.01s\n\nOK\n")
            result = complete.functional_evidence(["python", "tests.py", "-v"], receipt, "cpu", ["test_required"])
            self.assertEqual(result["status"], "incomplete")
            self.assertEqual(result["missing_named_methods"], ["test_required"])
            result = complete.functional_evidence(["python", "tests.py", "-v"], receipt, "cpu", ["test_other"])
            self.assertEqual(result["status"], "passed")

    def test_negative_native_or_header_counters_block_matched_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"source.cpp").write_text("source\n")
            (root/"header.hpp").write_text("header\n")
            row = {"sha256": complete.digest(root/"source.cpp"), "lines": {"coverage_status": "counted"},
                   "gcov_branches": {"coverage_status": "counted"}}
            data = {"schema": "datapump.triad.native-gcov-coverage", "sources": {"source.cpp": row},
                    "dependency_sha256": {"header.hpp": complete.digest(root/"header.hpp")},
                    "headers": {"header.hpp": {"lines": {"coverage_status": "unqualified_negative_line_counters", "invalid_negative_counts": {"5": [-24]}},
                                                "gcov_branches": {"coverage_status": "counted"}}}}
            path = root/"coverage.json"
            path.write_text(json.dumps(data))
            result = complete.check_coverage(path, root, ["source.cpp"], ["header.hpp"])
            self.assertEqual(result["declared_dependency_status"], "matched")
            self.assertEqual(result["counter_quality"]["status"], "invalid")
            self.assertEqual(result["counter_quality"]["invalid"][0]["category"], "headers")
            self.assertEqual(result["historical_applicability"], "unqualified")
            summary = report()
            summary["coverage_receipts"][0] = result
            self.assertNotEqual(complete.summarize(summary, legacy(), {"sampled": case("sampled")}, ["sampled"])["outcomes"]["code_path_coverage"]["status"], "declared_source_and_dependency_matched_historical")

    def test_retained_cache_connects_legacy_without_recursive_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root/"new-run"
            output.mkdir()
            cache = root/"retained"
            result = complete.connect_fixture_cache(output, cache)
            self.assertEqual(result["status"], "retained_link_verified")
            self.assertEqual((output/"triad-performance-fixtures").resolve(), cache)
            for invalid in (output, output/"child", root):
                with self.assertRaisesRegex(ValueError, "non-nested"):
                    complete.connect_fixture_cache(output, invalid)

    def test_unsupported_cache_link_does_not_silently_regenerate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root/"run"
            output.mkdir()
            with patch.object(Path, "symlink_to", side_effect=OSError("unsupported symlink")):
                with self.assertRaisesRegex(OSError, "unsupported symlink"):
                    complete.connect_fixture_cache(output, root/"cache")

    def test_explicit_fft_routes_partition_the_complete_ci_profile(self):
        plan = complete.make_route_plan({"profiles": {"ci": ["native"]}}, {"profiles": {"ci": ["fft-a", "fft-b"]}}, ["fft-b"])
        self.assertEqual(plan["legacy_case_ids"], ["native", "fft-a", "gaussian-multiblock"])
        self.assertEqual(plan["identical_fft_case_ids"], ["fft-b"])
        for selected in (["not-a-ci-fft"], ["fft-a", "fft-a"]):
            with self.assertRaisesRegex(ValueError, "duplicate or non-CI"):
                complete.make_route_plan({"profiles": {"ci": ["native"]}}, {"profiles": {"ci": ["fft-a"]}}, selected)

    def test_mapped_identical_route_closes_only_its_exact_focused_gap(self):
        plan = complete.make_route_plan({"profiles": {"ci": ["native"]}}, {"profiles": {"ci": ["fft"]}}, ["fft"])
        old = legacy(cases=[case(name) for name in plan["legacy_case_ids"]], complete_required_coverage=False,
                     selected_case_ids=plan["legacy_case_ids"], full_profile_case_ids=plan["full_legacy_profile_ids"])
        fft = case("fft", identity_agreement={"status": "implementation_unchanged"}, numeric_agreement={"eligible": True},
                   implementation_comparison={"measured": False, "ratio": None})
        result = complete.timing_outcomes(old, {}, [], identical={"fft": fft}, route_plan=plan)
        self.assertTrue(result["all_declared_cases_measured"])
        self.assertEqual(result["prediction_accuracy"]["status"], "passed")
        self.assertFalse(complete.timing_outcomes(old, {}, [], identical={"fft": fft})["all_declared_cases_measured"])
        fft["identity_agreement"]["status"] = "deduplication_ineligible"
        result = complete.timing_outcomes(old, {}, [], identical={"fft": fft}, route_plan=plan)
        self.assertEqual(result["prediction_accuracy"]["unresolved"], ["fft"])
        self.assertFalse(complete.timing_outcomes(old, {}, [], route_plan=plan)["all_declared_cases_measured"])

    def test_nonpartitioned_route_rejects_self_declared_smaller_profile(self):
        plan = complete.make_route_plan({"profiles": {"ci": ["native"]}}, {"profiles": {"ci": ["fft"]}}, [])
        old = legacy()
        self.assertFalse(complete.timing_outcomes(old, {}, [], route_plan=plan)["all_declared_cases_measured"])
        old.update(cases=[case(name) for name in plan["legacy_case_ids"]], full_profile_case_ids=plan["full_legacy_profile_ids"])
        self.assertTrue(complete.timing_outcomes(old, {}, [], route_plan=plan)["all_declared_cases_measured"])
        old["full_profile_case_ids"] = ["different", "fft", "gaussian-multiblock"]
        self.assertFalse(complete.timing_outcomes(old, {}, [], route_plan=plan)["all_declared_cases_measured"])

    def test_staged_top_level_precision_and_direct_measurement_are_distinct(self):
        row = case("sampled", adaptive={}, timing_precision_reached=True, status="direct_full")
        result = complete.timing_outcomes(legacy(), {"sampled": row}, ["sampled"])
        self.assertEqual(result["timing_precision"]["status"], "passed")
        self.assertEqual(result["prediction_accuracy"]["sampled_direct_case_ids"], ["sampled"])

    def test_green_children_cannot_qualify_cold_or_declared_inventory_gaps(self):
        receipt = report()
        receipt["inventory_unresolved"] = [{"id": "particle-count-boundary", "unresolved": ["no validated scaling model"]}]
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertTrue(result["warm_target_decisions_complete"])
        self.assertFalse(result["complete_scope_qualification"])
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["exit_code"], 2)
        self.assertEqual(result["outcomes"]["performance_regression"]["status"], "passed")
        self.assertEqual(result["outcomes"]["code_path_coverage"]["fresh"], False)

    def test_finite_inventory_can_close_without_universal_branch_coverage_claim(self):
        receipt = report()
        receipt["inventory_unresolved"] = []
        receipt["sampled_summary"] = {"cold_latency_qualification": "qualified"}
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertTrue(result["complete_scope_qualification"])
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["scope_blockers"], [])

    def test_custom_passing_command_does_not_close_required_functional_scope(self):
        receipt = report()
        receipt["inventory_unresolved"] = []
        receipt["sampled_summary"] = {"cold_latency_qualification": "qualified"}
        receipt["functional_defaults_requested"] = False
        receipt["functional_scope"] = "operator-supplied command scope only"
        receipt["required_default_functional_commands"] = []
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertEqual(result["outcomes"]["functional_correctness"]["status"], "passed")
        self.assertFalse(result["outcomes"]["functional_correctness"]["required_scope_complete"])
        self.assertFalse(result["complete_scope_qualification"])
        self.assertEqual(result["exit_code"], 2)

    def test_missing_or_substituted_default_command_does_not_close_scope(self):
        receipt = report()
        receipt["sampled_summary"] = {"cold_latency_qualification": "qualified"}
        receipt["required_default_functional_commands"].append(["second-required-test"])
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertFalse(result["complete_scope_qualification"])
        receipt["required_default_functional_commands"] = [["different-test"]]
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertFalse(result["complete_scope_qualification"])

    def test_missing_or_duplicate_work_is_not_complete(self):
        result = complete.timing_outcomes(legacy(cases=[case(), case()]), {}, ["sampled"])
        self.assertFalse(result["all_declared_cases_measured"])
        self.assertEqual(result["missing_sampled_cases"], ["sampled"])
        self.assertEqual(result["performance_regression"]["status"], "inconclusive")

    def test_prediction_precision_function_and_regression_remain_separate(self):
        sampled = case("sampled", prediction_valid=False, adaptive={"timing_precision_reached": False})
        result = complete.summarize(report(), legacy(), {"sampled": sampled}, ["sampled"])
        outcomes = result["outcomes"]
        self.assertEqual(outcomes["functional_correctness"]["status"], "passed")
        self.assertEqual(outcomes["performance_regression"]["status"], "passed")
        self.assertEqual(outcomes["prediction_accuracy"]["status"], "unqualified")
        self.assertEqual(outcomes["timing_precision"]["status"], "inconclusive")

    def test_hard_receiver_regression_fails_and_hardware_stays_separate(self):
        base = legacy(hardware_health={"status": "raw_hardware_screen", "warning": True})
        sampled = case("sampled", comparison={"gate": "performance_regression"})
        result = complete.summarize(report(), base, {"sampled": sampled}, ["sampled"])
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["outcomes"]["performance_regression"]["regressions"], ["sampled"])
        self.assertEqual(result["outcomes"]["hardware_health"]["status"], "degraded_screen")

    def test_functional_failure_is_a_failure_even_with_good_timing(self):
        receipt = report()
        receipt["functional_commands"][0]["status"] = "failed"
        result = complete.summarize(receipt, legacy(), {"sampled": case("sampled")}, ["sampled"])
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["outcomes"]["performance_regression"]["status"], "passed")

    def test_historical_coverage_is_bound_to_current_source_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"source.py").write_text("original\n")
            receipt = root/"coverage.json"
            receipt.write_text(json.dumps({"schema": "datapump.triad.python-branch-coverage",
                "sources": {"source.py": {"sha256": complete.digest(root/"source.py"), "branches": {"covered_arcs": 2}}}}))
            good = complete.check_coverage(receipt, root, ["source.py"])
            self.assertEqual(good["status"], "source_rows_matched_historical")
            self.assertEqual(good["declared_dependency_status"], "unverified")
            (root/"source.py").write_text("changed\n")
            stale = complete.check_coverage(receipt, root, ["source.py", "missing.py"])
            self.assertEqual(stale["status"], "source_mismatch")
            self.assertEqual(stale["missing_expected_sources"], ["missing.py"])
            with self.assertRaisesRegex(ValueError, "outside"):
                complete.relative_file(root, "../escape.py")

    def test_default_native_no_tests_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"tests").mkdir()
            (root/"tests/test_example.py").write_text("pass\n")
            commands = complete.functional_defaults(root, root/"build/native", {"rows": [
                {"cheap_functional_tests": ["tests/test_example.py", "tests/test_example.py::one"]}]})
            self.assertIn("--no-tests=error", commands[0])
            self.assertEqual(len(commands), 2)
            self.assertIn("-v", commands[1])

    def test_default_cuda_phase_commands_select_cuda(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"tests").mkdir()
            tests = []
            for name in complete.DEVICE_TESTS:
                (root/"tests"/name).write_text("pass\n")
                tests.append("tests/"+name)
            commands = complete.functional_defaults(root, root/"native", {"rows": [{"cheap_functional_tests": tests}]}, "cuda")
            self.assertTrue(all(command[-2:] == ["--device", "cuda:0"] for command in commands[1:]))

    def test_missing_native_member_and_disabled_member_are_incomplete(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"list.json"
            rows = [{"name": name} for name in sorted(complete.NATIVE_NAMES)]
            receipt = {"status": "passed", "stdout": str(path)}
            path.write_text(json.dumps({"tests": rows[:-1]}))
            self.assertEqual(complete.native_inventory(receipt)["status"], "incomplete")
            rows[0]["properties"] = [{"name": "DISABLED", "value": True}]
            path.write_text(json.dumps({"tests": rows}))
            self.assertEqual(complete.native_inventory(receipt)["status"], "incomplete")
            rows[0]["properties"] = []
            path.write_text(json.dumps({"tests": rows}))
            self.assertEqual(complete.native_inventory(receipt)["status"], "passed")

    def test_skipped_and_unexecuted_cuda_are_not_functional_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"stdout"
            receipt = {"status": "passed", "stdout": str(path)}
            path.write_text("Ran 3 tests in 0.01s\n\nOK (skipped=3)\n")
            result = complete.functional_evidence(["python", "unit.py"], receipt, "cuda")
            self.assertEqual(result["status"], "incomplete")
            self.assertEqual(result["executed_tests"], 0)
            path.write_text(json.dumps({"cpu": {"status": "passed", "checks": 10}, "cuda": {"status": "unexecuted"}}))
            command = ["python", "test_triad_phase_tensor.py", "--device", "cuda:0"]
            self.assertEqual(complete.functional_evidence(command, receipt, "cuda")["status"], "incomplete")
            cpu = complete.functional_evidence(command, receipt, "cpu")
            self.assertEqual(cpu["status"], "passed")
            self.assertEqual(cpu["executed_scope_receipt"]["cuda"], "unexecuted")

    def test_ctest_requires_all_four_executed_passed_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"stdout"
            receipt = {"status": "passed", "stdout": str(path)}
            rows = [f"{i}/4 Test #{i}: {name} ..... Passed 0.10 sec" for i, name in enumerate(sorted(complete.NATIVE_NAMES), 1)]
            path.write_text("\n".join(rows[:-1]))
            self.assertEqual(complete.functional_evidence(["ctest"], receipt, "cpu")["status"], "incomplete")
            path.write_text("\n".join(rows))
            self.assertEqual(complete.functional_evidence(["ctest"], receipt, "cpu")["status"], "passed")

    def test_header_drift_does_not_reuse_cpp_counters_as_complete_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, header = root/"source.cpp", root/"header.hpp"
            source.write_text("include header\n")
            header.write_text("original\n")
            path = root/"coverage.json"
            row = {"schema": "datapump.triad.native-gcov-coverage", "sources": {"source.cpp": {"sha256": complete.digest(source)}}}
            path.write_text(json.dumps(row))
            result = complete.check_coverage(path, root, ["source.cpp"], ["source.cpp", "header.hpp"])
            self.assertEqual(result["status"], "source_rows_matched_historical")
            self.assertEqual(result["historical_applicability"], "unqualified")
            self.assertEqual(result["missing_dependency_identities"], ["header.hpp"])
            row["dependency_sha256"] = {"header.hpp": complete.digest(header)}
            path.write_text(json.dumps(row))
            header.write_text("changed\n")
            result = complete.check_coverage(path, root, ["source.cpp"], ["source.cpp", "header.hpp"])
            self.assertEqual(result["changed_dependencies"], ["header.hpp"])
            self.assertEqual(result["historical_applicability"], "unqualified")

    def test_budget_exhaustion_does_not_start_process(self):
        with patch.object(complete.subprocess, "Popen") as start:
            row = complete.execute(["never"], cwd=ROOT, directory=ROOT/"never-created", deadline=time.monotonic()-1, env={})
            start.assert_not_called()
            self.assertEqual(row["status"], "not_started_budget_exhausted")

    def test_timeout_reaps_owned_process_group_and_keeps_receipt(self):
        process = Mock()
        process.wait.side_effect = subprocess.TimeoutExpired(["child"], 1)
        with tempfile.TemporaryDirectory() as tmp, patch.object(complete.subprocess, "Popen", return_value=process) as start, \
                patch.object(complete, "_stop_group") as stop:
            row = complete.execute(["child"], cwd=ROOT, directory=Path(tmp)/"logs", deadline=time.monotonic()+1, env={})
            self.assertEqual(row["status"], "deadline_exceeded")
            self.assertTrue(start.call_args.kwargs["start_new_session"])
            stop.assert_called_once_with(process)

    def test_main_keeps_models_in_reuse_mode_and_uses_one_deadline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = root/"native"
            binary.write_bytes(b"native")
            output = root/"out"
            calls = []
            def run(argv, **kwargs):
                calls.append((argv, kwargs["deadline"]))
                destination = Path(argv[argv.index("--output")+1])
                if "triad-performance.py" in Path(argv[1]).name:
                    complete.write_json(destination, legacy())
                else:
                    for i, value in enumerate(argv):
                        if value == "--case":
                            case_dir = destination/argv[i+1]
                            case_dir.mkdir(parents=True)
                            complete.write_json(case_dir/"result.json", case(argv[i+1]))
                return {"status": "passed", "exit_code": 0}
            with patch.object(complete, "source_identity", return_value={"source": "hash"}), \
                    patch.object(complete, "execute", side_effect=run):
                code = complete.main(["--output", str(output), "--reference-root", str(ROOT),
                    "--candidate-root", str(ROOT), "--binary", str(binary), "--reference-binary", str(binary),
                    "--anchor-directory", str(root/"old-anchors"), "--family-anchor-directory", str(root/"new-anchors")])
            self.assertEqual(code, 2)  # Missing functional/coverage/cold proof remains visible.
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[0][1], calls[1][1])
            old, new = calls[0][0], calls[1][0]
            self.assertEqual(old[old.index("--validation-mode")+1], "reuse")
            self.assertIn("--_backend-worker", old)
            self.assertEqual(new[new.index("--mode")+1], "reuse")
            for command in (old, new):
                self.assertEqual(command[command.index("--stopping-rule")+1], "decision")
                self.assertEqual(command[command.index("--regression-profile")+1], "normal")
            summary = json.loads((output/"summary.json").read_text())
            self.assertEqual(summary["requested_stopping_rule"], "decision")
            self.assertEqual(summary["regression_profile"], {"name": "normal", "pass_alpha": .15,
                "failure_alpha": .05, "slowdown_threshold": 1.30})
            self.assertEqual(summary["budget_seconds"], 1500.)
            self.assertFalse(summary["routine_requalifies_models"])
            self.assertFalse(summary["complete_scope_qualification"])
            self.assertEqual(summary["computational_savings"]["status"], "not_established_by_this_run")

    def test_main_staged_and_identical_routes_execute_each_target_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = root/"native"
            binary.write_bytes(b"native")
            output = root/"out"
            calls = []
            selected_fft = "narrow-hf-tight"
            native_config = json.loads((ROOT/"tools/triad-performance-native-cases.json").read_text())
            fft_config = json.loads((ROOT/"tools/triad-performance-cases.json").read_text())
            plan = complete.make_route_plan(native_config, fft_config, [selected_fft])
            def run(argv, **kwargs):
                calls.append((argv, kwargs["deadline"]))
                destination = Path(argv[argv.index("--output")+1])
                cases = [argv[i+1] for i, value in enumerate(argv) if value == "--case"]
                driver = Path(argv[1]).name
                if driver == "triad-performance.py":
                    complete.write_json(destination, legacy(cases=[case(name) for name in cases], complete_required_coverage=False,
                        selected_case_ids=cases, full_profile_case_ids=plan["full_legacy_profile_ids"]))
                else:
                    for name in cases:
                        directory = destination/name
                        directory.mkdir(parents=True)
                        row = case(name)
                        if driver == "triad_perf_identical_fft.py":
                            row.update(identity_agreement={"status": "implementation_unchanged"}, numeric_agreement={"eligible": True},
                                       implementation_comparison={"measured": False, "ratio": None})
                        else:
                            row.update(adaptive={}, timing_precision_reached=True)
                        complete.write_json(directory/"result.json", row)
                return {"status": "passed", "exit_code": 0}
            with patch.object(complete, "source_identity", return_value={"source": "hash"}), \
                    patch.object(complete, "execute", side_effect=run):
                complete.main(["--output", str(output), "--reference-root", str(ROOT), "--binary", str(binary),
                    "--reference-binary", str(binary), "--anchor-directory", str(root/"old"), "--family-route", "staged",
                    "--staged-anchor-directory", str(root/"staged"), "--identical-fft-case", selected_fft,
                    "--identical-anchor-directory", str(root/"identical"), "--fixture-cache", str(root/"fixtures"),
                    "--stopping-rule", "precision", "--regression-profile", "ci"])
            self.assertEqual(len(calls), 3)
            self.assertEqual(len({deadline for _, deadline in calls}), 1)
            self.assertEqual([Path(argv[1]).name for argv, _ in calls], ["triad-performance.py", "triad_perf_identical_fft.py", "triad_perf_staged.py"])
            self.assertNotIn(selected_fft, calls[0][0])
            for argv, _ in calls[:2]:
                self.assertEqual(argv[argv.index("--fixture-cache")+1], str(root/"fixtures"))
            for argv, _ in calls:
                self.assertEqual(argv[argv.index("--stopping-rule")+1], "precision")
                self.assertEqual(argv[argv.index("--regression-profile")+1], "ci")
            for argv, _ in calls[1:]:
                self.assertEqual(argv[argv.index("--mode")+1], "reuse")
            summary = json.loads((output/"summary.json").read_text())
            self.assertEqual(summary["requested_stopping_rule"], "precision")
            self.assertFalse(summary["core_decision_profile_active"])
            self.assertEqual(summary["regression_profile"], {"name": "ci", "pass_alpha": .30,
                "failure_alpha": .05, "slowdown_threshold": 1.70})
            self.assertTrue(summary["warm_target_decisions_complete"])
            self.assertFalse(summary["complete_scope_qualification"])

    def test_configured_extensions_use_the_shared_deadline_and_current_validators(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = root/"native"
            binary.write_bytes(b"native")
            workspace = root/"bounds-workspace"
            workspace.mkdir()
            output = root/"out"
            calls = []
            def run(argv, **kwargs):
                calls.append((argv, kwargs["deadline"]))
                destination = Path(argv[argv.index("--output")+1])
                if Path(argv[1]).name == "triad-performance.py":
                    complete.write_json(destination, legacy())
                else:
                    destination.mkdir(parents=True, exist_ok=True)
                    complete.write_json(destination/"summary.json", {"schema": "mocked-extension-or-family"})
                return {"status": "passed", "exit_code": 0}
            def checked(spec):
                self.assertTrue(Path(spec["context"]["report_path"]).is_file())
                return {"valid": True, "status": "passed", "errors": [], "gap_ids": []}
            with patch.object(complete, "source_identity", return_value={}), \
                    patch.object(complete, "execute", side_effect=run), \
                    patch.object(complete, "validate_extension_report", side_effect=checked) as validate:
                complete.main(["--output", str(output), "--reference-root", str(ROOT), "--binary", str(binary),
                    "--reference-binary", str(binary), "--anchor-directory", str(root/"old"), "--family-mode", "direct",
                    "--lifecycle-anchor-directory", str(root/"life"), "--resource-anchor", str(root/"resource.json"),
                    "--planner-binary", str(binary), "--planner-reference-binary", str(binary),
                    "--planner-bounds-workspace", str(workspace), "--regression-profile", "ci",
                    "--native-direct-case", "native-vlf-low-cn0"])
            self.assertEqual(len(calls), 5)
            self.assertEqual(len({deadline for _, deadline in calls}), 1)
            self.assertEqual(validate.call_count, 3)
            self.assertEqual(calls[0][0][calls[0][0].index("--native-direct-case")+1], "native-vlf-low-cn0")
            self.assertTrue(all("--native-direct-case" not in argv for argv, _ in calls[1:]))
            self.assertEqual([Path(argv[1]).name for argv, _ in calls[2:]],
                             ["triad_perf_lifecycle.py", "triad_perf_resource.py", "triad_perf_planner.py"])
            for argv, _ in calls[2:]:
                self.assertNotIn("qualify", argv)
                self.assertEqual(argv[argv.index("--regression-profile")+1], "ci")
            self.assertEqual(calls[2][0][calls[2][0].index("--mode")+1], "reuse")
            self.assertEqual(calls[3][0][calls[3][0].index("--mode")+1], "routine")
            self.assertIn("--source-root", calls[3][0])
            self.assertEqual(calls[4][0][calls[4][0].index("--bounds-workspace")+1], str(workspace))
            summary = complete.read_json(output/"summary.json")
            self.assertEqual(set(summary["extensions"]), {"lifecycle", "resource", "planner"})
            self.assertEqual(summary["native_direct_case_ids"], ["native-vlf-low-cn0"])
            self.assertEqual(summary["extensions"]["resource"]["context"]["mode"], "routine")
            self.assertTrue(all(row.get("report_sha256") for row in summary["extensions"].values()))
            self.assertEqual(summary["extensions"]["planner"]["context"]["bounds_workspace"], str(workspace))
            self.assertFalse(summary["complete_scope_qualification"])

    def test_explicit_direct_family_mode_needs_no_unused_anchor(self):
        args = complete.parse_args(["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct"])
        self.assertEqual(args.family_mode, "direct")
        self.assertIsNone(args.family_anchor_directory)
        self.assertEqual(args.stopping_rule, "decision")
        self.assertEqual(args.regression_profile, "normal")
        self.assertEqual(args.budget_seconds, 1500.)
        self.assertEqual(args.native_direct_case, [])

    def test_native_direct_routes_require_unique_modeled_profile_cases(self):
        base = ["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct"]
        cases = complete.read_json(ROOT/"tools/triad-performance-native-cases.json")
        direct = next(row["id"] for row in cases["cases"] if "training_seconds" not in row)
        for names in (["not-a-case"], [direct], ["native-vlf-low-cn0"]*2):
            with patch.object(sys, "stderr"), self.assertRaises(SystemExit):
                complete.parse_args(base+[item for name in names for item in ("--native-direct-case", name)])

    def test_strict_profile_is_explicit_and_keeps_the_manual_budget(self):
        args = complete.parse_args(["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct",
            "--regression-profile", "strict", "--budget-seconds", "3600"])
        self.assertEqual(args.regression_profile, "strict")
        self.assertEqual(args.budget_seconds, 3600.)

    def test_resource_direct_needs_no_anchor_and_keeps_optional_history_separate(self):
        base = ["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct",
            "--resource-direct"]
        for history in ([], ["--resource-anchor", "/tmp/historical-anchor.json"]):
            args = complete.parse_args(base+history)
            specs = complete.extension_specs(args, ROOT, ROOT, Path("/tmp/native"), Path("/tmp/ref"), Path("/tmp/out"))
            self.assertEqual(len(specs), 1)
            spec = specs[0]
            self.assertEqual(spec["id"], "resource")
            self.assertEqual(spec["context"]["mode"], "direct")
            self.assertEqual(spec["arguments"][spec["arguments"].index("--mode")+1], "direct")
            self.assertEqual("--anchor" in spec["arguments"], bool(history))
            self.assertEqual("anchor" in spec["context"], bool(history))

    def test_cpu_lifecycle_direct_activates_without_a_model_anchor(self):
        base = ["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct"]
        args = complete.parse_args(base+["--lifecycle-mixed-method", "direct"])
        self.assertIsNone(args.lifecycle_anchor_directory)
        specs = complete.extension_specs(args, ROOT, ROOT, Path("/tmp/native"), Path("/tmp/ref"), Path("/tmp/out"))
        self.assertEqual(len(specs), 1)
        self.assertEqual(specs[0]["context"]["mixed_method"], "direct")
        self.assertEqual(specs[0]["arguments"][-4:], ["--mixed-method", "direct", "--direct-cycles", "6"])
        self.assertEqual(specs[0]["context"]["direct_cycles"], 6)
        self.assertEqual(specs[0]["context"]["anchor_directory"], "/tmp/out/lifecycle-unused-anchors")
        with patch.object(sys, "stderr"), self.assertRaises(SystemExit):
            complete.parse_args(base+["--device", "cuda", "--lifecycle-mixed-method", "direct"])
        args = complete.parse_args(base+["--lifecycle-mixed-method", "direct", "--lifecycle-direct-cycles", "auto"])
        specs = complete.extension_specs(args, ROOT, ROOT, Path("/tmp/native"), Path("/tmp/ref"), Path("/tmp/out"))
        self.assertEqual(specs[0]["context"]["direct_cycles"], "auto")
        self.assertEqual(specs[0]["arguments"][-2:], ["--direct-cycles", "auto"])
        for value in ("1", "257", "6.0", "nan"):
            with patch.object(sys, "stderr"), self.assertRaises(SystemExit):
                complete.parse_args(base+["--lifecycle-mixed-method", "direct", "--lifecycle-direct-cycles", value])

    def test_modeled_gpu_lifecycle_keeps_prior_argv_and_context(self):
        args = complete.parse_args(["--output", "/tmp/not-run", "--reference-root", str(ROOT), "--binary", "/tmp/not-run",
            "--reference-binary", "/tmp/not-run", "--anchor-directory", "/tmp/not-run", "--family-mode", "direct",
            "--device", "cuda", "--lifecycle-anchor-directory", "/tmp/qualified-anchors"])
        spec = complete.extension_specs(args, ROOT, ROOT, Path("/tmp/native"), Path("/tmp/ref"), Path("/tmp/out"))[0]
        self.assertEqual(spec["arguments"], ["--candidate-root", str(ROOT), "--reference-root", str(ROOT),
            "--binary", "/tmp/native", "--reference-binary", "/tmp/ref", "--device", "cuda", "--precision", "fp32",
            "--threads", "2", "--native-threads", "1", "--mode", "reuse", "--anchor-directory", "/tmp/qualified-anchors"])
        self.assertNotIn("mixed_method", spec["context"])
        self.assertEqual(args.lifecycle_mixed_method, "modeled")


if __name__ == "__main__":
    unittest.main()

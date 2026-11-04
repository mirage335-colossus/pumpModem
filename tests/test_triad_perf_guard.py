#!/usr/bin/env python3
"""Decision/coverage/cleanup regressions for the reject-only runner."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import triad_perf_guard as guard
from triad_perf_guard_policy import GuardPolicy, build_guard_plan, build_guard_report


class Clock:
    def __init__(self):
        self.value = 0.

    def __call__(self):
        return self.value


class FakeAdapter:
    def __init__(self, cases, *, clock=None, ratios=None, errors=None, close_error=False):
        self.cases = cases
        self.clock = clock or Clock()
        self.ratios = ratios or {}
        self.errors = errors or {}
        self.calls = []
        self.closed = False
        self.close_error = close_error

    def descriptors(self):
        return [{"id": name, "boundary": "one complete synthetic target", **details}
                for name, details in self.cases.items()]

    def measure_pair(self, case_id, run_id, seed, order, phase):
        index = sum(call["case_id"] == case_id and call["phase"] == phase for call in self.calls)
        self.calls.append({"case_id": case_id, "run_id": run_id, "seed": seed,
                           "order": order, "phase": phase})
        self.clock.value += .001
        error = self.errors.get((case_id, phase, index))
        if error:
            raise error
        values = self.ratios.get((case_id, phase), [1.])
        ratio = values[min(index, len(values) - 1)]
        return {"run_id": run_id, "reference_seconds": 1., "candidate_seconds": ratio,
                "details": {"complete_work": True, "phase": phase, "source_checks": "matched"}}

    def close(self):
        self.closed = True
        if self.close_error:
            raise RuntimeError("failed to reap a worker")

    def summary(self):
        return {"closed": self.closed, "calls": self.calls}


class GuardExecutionTests(unittest.TestCase):
    def exercise(self, adapter, *, profile="ci", budget=1000.):
        policy = GuardPolicy(profile)
        descriptors, owners = guard.collect_descriptors([("fake", adapter)])
        ids = [row["id"] for row in descriptors]
        schedule = guard.measurement_schedule(ids, policy, seed=20, session="test")
        plan = build_guard_plan(ids, policy, metadata={"schedule": schedule})
        with tempfile.TemporaryDirectory() as directory:
            result = guard.exercise_workloads([("fake", adapter)], descriptors, owners, plan, schedule,
                deadline=budget, output=Path(directory), clock=adapter.clock)
            retained = [json.loads(p.read_text()) for p in sorted(Path(directory).glob("pair-*.json"))]
        screens, confirmations, futilities, failures, advisory, measurements = result
        report = build_guard_report(plan, screens, confirmations, functional_failures=failures,
                                    advisory=advisory, futility_records=futilities)
        return report, retained, schedule

    def test_noisy_screen_cannot_itself_make_ci_red(self):
        adapter = FakeAdapter({"a": {}}, ratios={
            ("a", "screen"): [4.], ("a", "confirmation"): [1., 1.01, .99, 1.02, .98]})
        report, _, _ = self.exercise(adapter)
        self.assertEqual(report["exit_code"], 0)
        self.assertEqual(len(report["confirmations"]), 0)
        self.assertEqual(report["futile_confirmations"], ["a"])
        self.assertEqual(report["futility_records"][0]["sample_count"], 2)
        self.assertFalse(report["affirmative_non_regression_certification"])

    def test_all_screens_precede_confirmation_and_ci_stops_after_first_reject(self):
        adapter = FakeAdapter({"a": {}, "b": {}, "c": {}}, ratios={
            ("a", "screen"): [3.], ("b", "screen"): [4.],
            ("a", "confirmation"): [3.]})
        report, retained, _ = self.exercise(adapter)
        self.assertEqual([row["phase"] for row in adapter.calls[:3]], ["screen"] * 3)
        self.assertEqual(report["confirmed_regressions"], ["a"])
        self.assertEqual(report["unconfirmed_nominations"], ["b"])
        self.assertEqual(len(retained), 15)
        self.assertEqual(report["exit_code"], 2)
        screen_ids = {row["observation"]["run_id"] for row in report["screens"]}
        self.assertEqual(report["confirmations"][0]["sample_count"], 12)
        self.assertFalse(screen_ids & {row["run_id"] for row in report["confirmations"][0]["observations"]})

    def test_gpu_development_continues_to_other_suspects(self):
        adapter = FakeAdapter({"a": {}, "b": {}}, ratios={
            ("a", "screen"): [3.], ("b", "screen"): [3.],
            ("a", "confirmation"): [3.], ("b", "confirmation"): [3.]})
        report, _, _ = self.exercise(adapter, profile="gpu-dev")
        self.assertEqual(report["confirmed_regressions"], ["a", "b"])
        self.assertEqual(len(adapter.calls), 26)

    def test_gpu_development_second_look_extends_same_fresh_sequence(self):
        adapter = FakeAdapter({"a": {}}, ratios={
            ("a", "screen"): [3.],
            ("a", "confirmation"): [1.4, 1., 1.4, 1.4, 1., 1.4, 1.4, 1., 1.4, 1.4, 1.4, 1.4,
                                      1.4, 1.4, 1.4, 1.4, 1.4, 1.4, 1.4, 1.4]})
        report, _, _ = self.exercise(adapter, profile="gpu-dev")
        self.assertEqual([row["sample_count"] for row in report["confirmations"]], [12, 20])
        self.assertFalse(report["confirmations"][0]["confirmed_regression"])
        self.assertTrue(report["confirmations"][1]["confirmed_regression"])
        self.assertEqual(report["confirmations"][0]["observations"], report["confirmations"][1]["observations"][:12])

    def test_budget_does_not_start_unusable_partial_confirmation(self):
        adapter = FakeAdapter({"a": {}}, ratios={("a", "screen"): [3.]})
        report, retained, _ = self.exercise(adapter, budget=24.)
        self.assertEqual(len(retained), 1)
        self.assertEqual(report["exit_code"], 0)
        self.assertEqual(report["unconfirmed_nominations"], ["a"])
        self.assertTrue(any("next predeclared" in reason for reason in report["advisory"]))

    def test_timeout_is_limited_assurance_not_a_statistical_regression(self):
        adapter = FakeAdapter({"a": {}}, errors={("a", "screen", 0): TimeoutError("deadline")})
        report, retained, _ = self.exercise(adapter)
        self.assertEqual(report["exit_code"], 0)
        self.assertEqual(report["missing_screens"], ["a"])
        self.assertEqual(retained[0]["status"], "budget_limited")

    def test_nonfinite_measurement_is_a_functional_integrity_failure(self):
        adapter = FakeAdapter({"a": {}}, ratios={("a", "screen"): [float("nan")]})
        report, retained, _ = self.exercise(adapter)
        self.assertEqual(report["exit_code"], 2)
        self.assertEqual(report["confirmed_regressions"], [])
        self.assertEqual(retained[0]["status"], "execution_or_integrity_failure")

    def test_unavailable_hypothesis_keeps_its_error_budget_and_scope(self):
        adapter = FakeAdapter({"a": {}, "missing": {"available": False, "reason": "input omitted"}})
        report, retained, _ = self.exercise(adapter)
        self.assertEqual(len(retained), 1)
        self.assertEqual(report["missing_screens"], ["missing"])
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(any("input omitted" in item for item in report["advisory"]))

    def test_seed_schedule_reuses_screens_but_reserves_disjoint_capture_ranges(self):
        policy = GuardPolicy("gpu-dev")
        schedule = guard.measurement_schedule(["resource:paired", "resource:sequential", "family:cfo"],
                                               policy, seed=30, session="fixed")
        self.assertEqual({row["screen"]["seed"] for row in schedule.values()}, {30})
        ranges = []
        for row in schedule.values():
            for invocation in row["confirmation"]:
                value = set(range(invocation["seed"], invocation["seed"] + 3))
                self.assertNotIn(30, value)
                self.assertFalse(any(value & earlier for earlier in ranges))
                ranges.append(value)
        self.assertEqual(len(ranges), 60)

    def test_schedule_uses_actual_family_job_floor_and_reproducible_random_order(self):
        policy = GuardPolicy("ci", workflow_job_count=1024)
        ids = ["a", "b"]
        plan = build_guard_plan(ids, policy)
        self.assertGreater(plan["looks"][0], policy.looks[0])
        one = guard.measurement_schedule(ids, policy, seed=30, session="one", looks=plan["looks"])
        two = guard.measurement_schedule(ids, policy, seed=30, session="two", looks=plan["looks"])
        different = guard.measurement_schedule(ids, policy, seed=31, session="three", looks=plan["looks"])
        orders = lambda schedule: [row["order"] for name in ids for row in (
            [schedule[name]["screen"]] + schedule[name]["confirmation"])]
        self.assertEqual(len(one["a"]["confirmation"]), plan["looks"][-1])
        self.assertEqual(orders(one), orders(two))
        self.assertNotEqual(orders(one), orders(different))
        self.assertEqual({tuple(value) for value in orders(one)}, {("reference", "candidate"), ("candidate", "reference")})

    def test_duplicate_or_unknown_case_selection_fails_before_data(self):
        adapter = FakeAdapter({"a": {}})
        with self.assertRaises(ValueError):
            guard.collect_descriptors([("one", adapter), ("two", adapter)])
        with self.assertRaises(ValueError):
            guard.collect_descriptors([("one", adapter)], ["unknown"])
        self.assertEqual(adapter.calls, [])

    def test_changed_run_id_cannot_supply_confirmation_evidence(self):
        class Corrupt(FakeAdapter):
            def measure_pair(self, *args, **kwargs):
                value = super().measure_pair(*args, **kwargs)
                value["run_id"] = "other"
                return value
        report, _, _ = self.exercise(Corrupt({"a": {}}))
        self.assertEqual(report["exit_code"], 2)
        self.assertTrue(any("identity" in value for value in report["functional_failures"]))


class GuardRunTests(unittest.TestCase):
    def _args(self, directory, extra=()):
        root = Path(directory)
        (root / "binary").write_text("fixture binary")
        return guard.parse_args(["--reference-root", str(root), "--candidate-root", str(root),
            "--binary", str(root / "binary"), "--reference-binary", str(root / "binary"),
            "--skip-hardware-probes", "--output", str(root / "output"), *extra])

    def run_fake(self, directory, *, adapter=None, extra=(), source_result=None):
        adapter = adapter or FakeAdapter({"a": {}})
        args = self._args(directory, extra)
        snapshot = {"paths": {}, "sha256": {}, "missing_declared_historical_files": {}}
        with patch.object(guard, "resolve_device", return_value=("cpu", None)), \
             patch.object(guard, "source_snapshot", return_value=snapshot), \
             patch.object(guard, "verify_snapshot", return_value=source_result or {"status": "passed", "changed": []}), \
             patch.object(guard, "check_coverage_receipts", return_value=([], ["no retained coverage supplied"])), \
             patch.object(guard, "run_functional", return_value=([], [], [])):
            report = guard.run(args, factories=[("fake", lambda context: adapter)])
        return report, adapter

    def test_aggregate_zero_exit_retains_limited_scope_and_never_qualifies(self):
        with tempfile.TemporaryDirectory() as directory:
            report, adapter = self.run_fake(directory)
            retained = json.loads((Path(directory) / "output/summary.json").read_text())
            self.assertTrue((Path(directory) / "output/plan.json").is_file())
            self.assertEqual(report, retained)
        self.assertTrue(adapter.closed)
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(report["limited_assurance"])
        self.assertFalse(report["complete_scope_qualification"])
        self.assertEqual(report["qualification_status"], "not_evaluated_by_fast_guard")
        self.assertEqual(report["hypothesis_count"], 1)
        self.assertEqual(report["suite_alpha"], .01)

    def test_cleanup_failure_is_blocking_without_mislabelling_a_slowdown(self):
        with tempfile.TemporaryDirectory() as directory:
            report, adapter = self.run_fake(directory, adapter=FakeAdapter({"a": {}}, close_error=True))
        self.assertTrue(adapter.closed)
        self.assertEqual(report["exit_code"], 2)
        self.assertEqual(report["confirmed_regressions"], [])
        self.assertEqual(report["adapter_summaries"]["fake"]["status"], "cleanup_failed")

    def test_changed_source_is_blocking_independently_of_statistical_outcome(self):
        with tempfile.TemporaryDirectory() as directory:
            report, _ = self.run_fake(directory, source_result={"status": "failed", "changed": ["candidate:code"]})
        self.assertEqual(report["exit_code"], 2)
        self.assertTrue(any("candidate:code" in message for message in report["functional_failures"]))

    def test_final_snapshot_checks_actual_file_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source"
            path.write_text("original")
            snapshot = {"paths": {"candidate:source": str(path)}, "sha256": {"candidate:source": guard._sha(path)}}
            self.assertEqual(guard.verify_snapshot(snapshot)["status"], "passed")
            path.write_text("changed")
            self.assertEqual(guard.verify_snapshot(snapshot), {"status": "failed", "changed": ["candidate:source"]})

    def test_interrupted_work_closes_adapters_and_preserves_nonzero_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            adapter = FakeAdapter({"a": {}}, errors={("a", "screen", 0): KeyboardInterrupt("cancelled")})
            with self.assertRaises(KeyboardInterrupt):
                self.run_fake(directory, adapter=adapter)
            report = json.loads((Path(directory) / "output/summary.json").read_text())
            pair = json.loads((Path(directory) / "output/measurements/pair-0000.json").read_text())
        self.assertTrue(adapter.closed)
        self.assertEqual(report["exit_code"], 2)
        self.assertTrue(report["execution_error"]["interrupted"])
        self.assertEqual(pair["status"], "interrupted")

    def test_advisory_hardware_warning_does_not_override_valid_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self._args(directory)
            args.skip_hardware_probes = False
            adapter = FakeAdapter({"a": {}})
            snapshot = {"paths": {}, "sha256": {}, "missing_declared_historical_files": {}}
            raw_profile = {"status": "complete", "hardware": {}, "cases": {}}
            with patch.object(guard, "resolve_device", return_value=("cpu", None)), \
                 patch.object(guard, "source_snapshot", return_value=snapshot), \
                 patch.object(guard, "verify_snapshot", return_value={"status": "passed", "changed": []}), \
                 patch.object(guard, "check_coverage_receipts", return_value=([], [])), \
                 patch.object(guard, "run_functional", return_value=([], [], [])), \
                 patch("triad_perf_hardware.collect_profile", return_value=raw_profile), \
                 patch.object(guard, "_legacy_api") as legacy:
                legacy.return_value.hardware_health.return_value = {"status": "raw_hardware_screen", "warning": True}
                report = guard.run(args, factories=[("fake", lambda context: adapter)])
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(report["hardware_health"]["warning"])
        self.assertTrue(report["limited_assurance"])

    def test_parser_validates_job_budget_and_force_floor_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self._args(directory, ["--profile", "gpu-dev", "--precision", "fp64"])
            self.assertEqual(args.device, "auto")
            self.assertEqual(args.budget_seconds, 600.)
            self.assertEqual(args.precision, "fp64")
            for extra in (["--suite-alpha", ".051"], ["--workflow-job-count", "0"],
                          ["--workflow-job-count", "2", "--workflow-job-index", "2"],
                          ["--budget-seconds", "nan"], ["--seed", "-1"]):
                with self.subTest(extra=extra), patch("sys.stderr"):
                    with self.assertRaises(SystemExit):
                        self._args(directory, extra)


if __name__ == "__main__":
    unittest.main()

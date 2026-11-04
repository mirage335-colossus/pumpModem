#!/usr/bin/env python3
"""Bounded identity, scalar inference and full-work FFT predictor checks."""
from __future__ import annotations

import copy
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_identical_fft as study
from triad_perf_adaptive import AdaptivePolicy, regression_profile

spec = importlib.util.spec_from_file_location("identical_test_api", ROOT / "tools/triad-performance.py")
api = importlib.util.module_from_spec(spec); spec.loader.exec_module(api)


def case(target=64):
    return {"id": "fake-fixed-input", "input_directory": "/not-used", "lag_max_seconds": .1,
            "group_tile": 32, "frequency_offsets_hz": [i*.01 for i in range(target)],
            "configuration": {"lag_group_count": 97}}


def audit():
    return {"repository_modules": {"tools/receiver.py": "same"}, "numerical_modules": {"numpy": "same"},
            "executable_mappings": [{"file": "lib.so", "sha256": "same"}], "native_mapping_status": "observed",
            "versions": {"scipy": "1"}, "controls": {"threads": 2}, "hardware": {"model": "same"},
            "measurement_files": {"adapter.py": "same"}, "timing_boundary": "test full resident time"}


class Worker:
    def __init__(self, factor=1., target=64, full_only=1., corrupt=None, on_full=None):
        self.factor, self.target, self.full_only = factor, target, full_only
        self.corrupt, self.on_full = corrupt, on_full
        self.requests = []
        self.audit = audit()

    def request(self, request):
        self.requests.append(request)
        action = request["action"]
        if action == "audit":
            return copy.deepcopy(self.audit)
        if action == "prepare":
            return {"wall_seconds": self.factor*2., "input_hashes": {"array": "same"},
                    "source_hashes": {"receiver.py": "same"}, "stages": {},
                    "geometry": {"sample_count": 10000, "precision": request["precision"],
                                 "lags": 97, "group_tile": 32, "memory_limit_bytes": 1024**3}}
        count = len(request["frequency_offsets_hz"])
        if count == self.target and self.on_full:
            self.on_full()
        groups = 97*count
        if self.corrupt == "count" and count == self.target:
            groups -= 1
        return {"wall_seconds_including_finish": self.factor*(.5+.25*count)*(self.full_only if count == self.target else 1.),
                "geometry": {"group_count": groups}, "measured_group_count": groups,
                "bit_log_likelihoods": [1., 2.], "strict": {"accepted": False, "bit": 1},
                "relaxed": {"accepted": False, "bit": 1}, "rank_histogram": [0, 0, 0, 0, groups]}


def run(directory, *, mode="qualify", history=None, workers=None, target=64, projection=None):
    workers = workers or {name: Worker(target=target) for name in study.SOURCES}
    return study.measure_identical_fft(case(target), workers, "fp32", "cpu",
        policy=AdaptivePolicy(stopping_rule="decision" if mode == "reuse" else "precision"),
        api=api, deadline=None, output=directory, mode=mode, historical=history, maximum_holds=3,
        reporting_projection=projection)


class IdenticalTests(unittest.TestCase):
    def test_profiles_keep_absolute_failure_budget_and_thresholds(self):
        noisy = [9.6, 9.85, 10., 10.15, 10.4]
        upper = {}
        lower = {}
        for name in ("strict", "normal", "ci"):
            policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(name))
            for ratio, expected in ((policy.slowdown_threshold-.01, "pass"),
                                    (policy.slowdown_threshold+.01, "performance_regression")):
                result = study.routine_decision([10.*ratio]*5, [10.]*3, policy)
                self.assertEqual(result["comparison"]["gate"], expected)
            comparison = study.routine_decision(noisy, [10.]*3, policy)["comparison"]
            self.assertEqual(comparison["upper_ledger"]["family_alpha"], policy.pass_error_budget)
            self.assertEqual(comparison["lower_ledger"]["family_alpha"], .05)
            lower[name], upper[name] = comparison["full_time_ratio_interval"]
        self.assertEqual(lower["strict"], lower["normal"])
        self.assertEqual(lower["normal"], lower["ci"])
        self.assertGreater(upper["strict"], upper["normal"])
        self.assertGreater(upper["normal"], upper["ci"])

    def test_routine_decision_is_per_test_one_sided_not_width_target(self):
        values = [9.6, 9.85, 10., 10.15, 10.4]
        policy = AdaptivePolicy(stopping_rule="decision", family_count=14)
        result = study.routine_decision(values, [10., 10., 10., 999., 999.], policy,
                                       model_log_allowance=math.log1p(.10))
        self.assertEqual(result["stop_reason"], "decision_pass")
        self.assertGreater(result["relative_timing_half_width"], .05)
        comparison = result["comparison"]
        self.assertEqual(comparison["per_test_one_sided_confidence"], .95)
        self.assertEqual(comparison["historical_full_seconds_used"], [10., 10., 10.])
        self.assertAlmostEqual(comparison["upper_ledger"]["historical_fixed_alpha"]+
                               len(policy.looks)*comparison["upper_ledger"]["current_per_look_alpha"], .05)
        self.assertFalse(result["paired_observations_constructed"])
        self.assertEqual(comparison, study.routine_decision(values, [10.]*3,
            AdaptivePolicy(stopping_rule="decision", family_count=1), model_log_allowance=math.log1p(.10))["comparison"])

    def test_routine_near_threshold_stays_inconclusive(self):
        policy = AdaptivePolicy(stopping_rule="decision")
        for factor, gate in [(1.29, "pass"), (1.31, "performance_regression")]:
            result = study.routine_decision([10*factor]*5, [10.]*3, policy)
            self.assertEqual(result["comparison"]["gate"], gate)
        near = study.routine_decision([12.9*v for v in [.96, .985, 1., 1.015, 1.04]], [10.]*3, policy)
        self.assertEqual(near["comparison"]["gate"], "inconclusive")
        self.assertEqual(near["stop_reason"], "continue")

    def test_routine_measurement_stops_before_five_percent_width(self):
        class JitterWorker(Worker):
            prepares = 0
            def request(self, request):
                if request["action"] == "prepare":
                    self.factor = [.96, .985, 1., 1.015, 1.04][self.prepares % 5]
                    self.prepares += 1
                return super().request(request)
        with tempfile.TemporaryDirectory() as tmp:
            history = run(Path(tmp)/"qual")
            result = run(Path(tmp)/"reuse", mode="reuse", history=history,
                         workers={name: JitterWorker() for name in study.SOURCES})
        self.assertEqual(len(result["training"]), 5)
        self.assertGreater(result["adaptive"]["relative_timing_half_width"], .05)
        self.assertEqual(result["comparison"]["gate"], "pass")
        self.assertTrue(result["prediction_valid"])

    def test_reviewed_projection_preserves_proof_and_rejects_changed_work(self):
        with tempfile.TemporaryDirectory() as tmp:
            workers = {name: Worker() for name in study.SOURCES}
            for worker in workers.values():
                worker.audit["measurement_files"]["triad_perf_identical_fft.py"] = "0"*64
            record = run(Path(tmp)/"qual", workers=workers)
        original = copy.deepcopy(record)
        identity = copy.deepcopy(record["identity"])
        identity["runtime"]["measurement_files"]["triad_perf_identical_fft.py"] = "1"*64
        projection = {"schema": "triad-identical-fft-reporting-projection-v1", "reviewed_reporting_only": True,
            "unchanged_measurement_core": study.measurement_core_hashes(),
            "measurement_files": {"triad_perf_identical_fft.py": {"historical_sha256": "0"*64,
                                                                "current_sha256": "1"*64}}}
        self.assertFalse(study.historical_applicability(record, identity)["compatible"])
        self.assertTrue(study.historical_applicability(record, identity, projection)["compatible"])
        self.assertEqual(record, original)
        for field in ("repository_modules", "controls", "numerical_modules"):
            changed = copy.deepcopy(identity); changed["runtime"][field]["changed"] = True
            self.assertFalse(study.historical_applicability(record, changed, projection)["compatible"])
        changed = copy.deepcopy(identity); changed["geometry"]["lags"] += 1
        self.assertFalse(study.historical_applicability(record, changed, projection)["compatible"])
        changed = copy.deepcopy(projection); changed["unchanged_measurement_core"][next(iter(changed["unchanged_measurement_core"]))] = "changed"
        self.assertFalse(study.historical_applicability(record, identity, changed)["compatible"])

    def test_actual_module_origins_exempt_only_pinned_harness(self):
        fake_torch = SimpleNamespace(__file__=sys.executable, __version__="test",
            get_num_threads=lambda: 2, get_num_interop_threads=lambda: 2,
            get_float32_matmul_precision=lambda: "highest",
            __config__=SimpleNamespace(parallel_info=lambda: "test"),
            backends=SimpleNamespace(cuda=SimpleNamespace(matmul=SimpleNamespace(allow_tf32=False)),
                                     cudnn=SimpleNamespace(allow_tf32=False)))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/"tools").mkdir()
            helper = root/"tools/triad_perf_extra.py"; helper.write_text("version = 1\n")
            other = root/"ordinary_name.py"; other.write_text("version = 1\n")
            modules = {name: module for name, module in sys.modules.items()
                       if not name.startswith(("triad_", "_triad"))}
            modules.update({"numpy": SimpleNamespace(__file__=sys.executable, __version__="test"),
                       "scipy": SimpleNamespace(__file__=sys.executable, __version__="test"),
                       "torch": fake_torch,
                       "triad_perf_extra": SimpleNamespace(__file__=str(helper)),
                       "ordinary_name": SimpleNamespace(__file__=str(other)),
                       "_triad_pinned": SimpleNamespace(__file__=str(study.WORKER))})
            ops_type = type("_Ops", (ModuleType,), {"__file__": "_ops.py", "__module__": "torch._ops"})
            fake_torch.ops = ops_type("torch.ops")
            modules.update({"torch.ops": fake_torch.ops,
                            "torch._ops": SimpleNamespace(__file__=sys.executable, _Ops=ops_type)})
            observed = study.RuntimeAudit(root, None)
            def fast_hash(path):
                return study.sha(path) if Path(path).is_relative_to(root) else "external-test-hash"
            with mock.patch.dict(sys.modules, modules, clear=True), \
                 mock.patch.object(study, "load_harness", return_value=SimpleNamespace(discover_hardware=lambda *args: {})), \
                 mock.patch.object(observed, "file_hash", side_effect=fast_hash):
                first = observed.snapshot("cpu")
                self.assertIn("tools/triad_perf_extra.py", first["repository_modules"])
                self.assertIn("ordinary_name.py", first["repository_modules"])
                self.assertEqual(first["numerical_modules"]["torch.ops"]["kind"], "known_torch_dynamic_namespace")
                self.assertEqual(first["numerical_modules"]["torch.ops"]["implementation_type"], "_Ops")
                helper.write_text("version = 2\n")
                self.assertNotEqual(first["repository_modules"], observed.snapshot("cpu")["repository_modules"])
                sys.modules["missing_application"] = SimpleNamespace(__file__=str(root/"missing.py"))
                with self.assertRaises(FileNotFoundError):
                    observed.snapshot("cpu")
                del sys.modules["missing_application"]
                sys.modules["triad_perf_unknown"] = SimpleNamespace(__file__=sys.executable)
                with self.assertRaisesRegex(ValueError, "outside the requested checkout"):
                    observed.snapshot("cpu")

    def test_changed_dependency_or_injected_control_disables_dedup(self):
        base = audit()
        prepared = {name: {"geometry": {"n": 1}, "input_hashes": {"a": "same"},
                           "source_hashes": {"r": "same"}} for name in study.SOURCES}
        for field in ("repository_modules", "numerical_modules", "executable_mappings", "versions", "controls", "hardware", "measurement_files"):
            audits = {"reference": copy.deepcopy(base), "candidate": copy.deepcopy(base)}
            audits["candidate"][field] = {"injected_delay": ".05"}
            result = study.identity_agreement(audits, prepared, request_contract={})
            self.assertEqual(result["status"], "deduplication_ineligible", field)
        missing = {"reference": copy.deepcopy(base), "candidate": copy.deepcopy(base)}
        missing["candidate"]["native_mapping_status"] = "unavailable"
        self.assertEqual(study.identity_agreement(missing, prepared, request_contract={})["status"], "deduplication_ineligible")

    def test_scalar_interval_does_not_create_a_paired_observation(self):
        result = study.scalar_summary([10., 10.1, 9.9, 10.01, 9.99], AdaptivePolicy())
        self.assertFalse(result["paired_observations_constructed"])
        self.assertNotIn("paired_candidate_over_reference", result)
        self.assertLess(result["timing_mean_confidence_interval_seconds"][0], 10.)
        with self.assertRaises(ValueError):
            study.scalar_summary([10.]*4, AdaptivePolicy())

    def test_forecast_frozen_before_independent_full_single_stream(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/"qual"
            def frozen():
                p = json.loads((out/"forecast.json").read_text())
                self.assertTrue(p["prediction_frozen_before_validation"])
            workers = {name: Worker(on_full=frozen) for name in study.SOURCES}
            result = run(out, workers=workers)
            self.assertTrue(study.anchor_valid(result))
            self.assertEqual(result["status"], "validated")
            self.assertEqual(result["full_measurement_count"], 3)
            self.assertEqual(result["fresh_timing_streams"], 1)
            self.assertEqual(result["comparison"]["gate"], "inconclusive")
            reference_scores = [r for r in workers["reference"].requests if r["action"] == "score"]
            self.assertEqual(len(reference_scores), 1)
            self.assertIsNone(result["implementation_comparison"]["confidence_interval"])

    def test_absolute_common_slowdown_is_visible_with_identical_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            history = run(Path(tmp)/"qual")
            unchanged = copy.deepcopy(history)
            workers = {name: Worker(factor=1.6) for name in study.SOURCES}
            measured = run(Path(tmp)/"reuse", mode="reuse", history=history, workers=workers)
            self.assertEqual(history, unchanged)
            self.assertEqual(measured["full_measurement_count"], 1)
            self.assertTrue(measured["prediction_valid"])
            self.assertEqual(measured["comparison"]["gate"], "performance_regression")
            self.assertGreater(measured["comparison"]["full_time_ratio_interval"][0], 1.3)
            self.assertEqual(measured["implementation_comparison"]["status"], "implementation_unchanged")

    def test_full_only_slowdown_blocks_abbreviation(self):
        with tempfile.TemporaryDirectory() as tmp:
            history = run(Path(tmp)/"qual")
            workers = {name: Worker(full_only=1.6) for name in study.SOURCES}
            measured = run(Path(tmp)/"reuse", mode="reuse", history=history, workers=workers)
            self.assertFalse(measured["prediction_valid"])
            self.assertEqual(measured["comparison"]["gate"], "inconclusive")
            self.assertFalse(measured["sentinel"]["model_envelope_changed"])

    def test_wrong_full_work_count_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            workers = {name: Worker(corrupt="count") for name in study.SOURCES}
            with self.assertRaisesRegex(ValueError, "different work count"):
                run(Path(tmp)/"qual", workers=workers)
            out = Path(tmp)/"qual"
            failure = json.loads((out/"failure.json").read_text())
            self.assertEqual(failure["status"], "measurement_failed")
            self.assertFalse(failure["prediction_valid"])
            receipts = [json.loads(line) for line in (out/"observations.jsonl").read_text().splitlines()]
            self.assertTrue(any(r["event"] == "observation" for r in receipts))
            self.assertEqual(receipts[-1]["event"], "score")
            self.assertEqual(sum(receipts[-1]["value"]["receipt"]["rank_histogram"]), 64*97-1)

    def test_actual_histogram_catches_short_work_with_full_nominal_metadata(self):
        receipt = Worker().request(api.score_request(case(), 64))
        receipt["rank_histogram"][-1] -= 1
        with self.assertRaisesRegex(ValueError, "different work count"):
            study.work_receipt(receipt, 64, 97)

    def test_rehashed_shortened_work_and_stopping_policy_cannot_qualify(self):
        with tempfile.TemporaryDirectory() as tmp:
            modeled = run(Path(tmp)/"modeled")
            direct = run(Path(tmp)/"direct", target=2)
        def check(record):
            record["record_sha256"] = study.digest({k: v for k,v in record.items() if k != "record_sha256"})
            self.assertFalse(study.anchor_valid(record))
        for original in (modeled, direct):
            record = copy.deepcopy(original)
            record["full_runs"][0]["scores"][0]["work"]["rank_histogram"][-1] -= 1
            check(record)
            record = copy.deepcopy(original)
            record["adaptive"]["policy"]["looks"] = [5, 7, 9]
            check(record)
        record = copy.deepcopy(modeled)
        record["training"][0]["scores"][-1]["count"] -= 1
        check(record)
        record = copy.deepcopy(modeled)
        record["full_runs"][0]["id"] = record["training"][0]["id"]
        check(record)
        record = copy.deepcopy(direct)
        record["training"] = record["training"][:-1]
        record["full_runs"] = record["training"]
        check(record)

    def test_changed_runtime_or_missing_history_never_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            measured = run(Path(tmp)/"no-history", mode="reuse")
            self.assertFalse(measured["prediction_valid"])
            self.assertEqual(measured["full_measurement_count"], 0)
            history = run(Path(tmp)/"qual")
            bad = copy.deepcopy(history); bad["identity"]["runtime"]["versions"]["scipy"] = "other"
            self.assertFalse(study.anchor_valid(bad, history["identity"]))

    def test_tampered_frozen_predictions_cannot_rehash_into_valid_proof(self):
        with tempfile.TemporaryDirectory() as tmp:
            record = run(Path(tmp)/"qual")
        record["frozen"]["predicted_full_seconds"] *= 1.01
        record["frozen"]["commitment_sha256"] = study.digest({k: v for k,v in record["frozen"].items() if k != "commitment_sha256"})
        record["record_sha256"] = study.digest({k: v for k,v in record.items() if k != "record_sha256"})
        self.assertFalse(study.anchor_valid(record))

    def test_small_full_banks_are_direct_but_still_need_absolute_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = run(Path(tmp)/"direct", target=2)
            self.assertTrue(study.anchor_valid(first))
            self.assertEqual(first["status"], "direct_full")
            self.assertEqual(first["comparison"]["gate"], "inconclusive")
            reused = run(Path(tmp)/"reuse", target=2, mode="reuse", history=first)
            self.assertEqual(reused["comparison"]["gate"], "pass")


if __name__ == "__main__":
    unittest.main()

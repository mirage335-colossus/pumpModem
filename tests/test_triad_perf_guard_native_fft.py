#!/usr/bin/env python3
"""Cheap orchestration checks: real adapter, deterministic process stand-ins."""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_guard_native_fft as guard


class FakeWorker:
    instances = []
    corruption = None

    def __init__(self, root, *, threads, **kwargs):
        self.name = kwargs["name"]
        self.requests = []
        self.closed = False
        self.identity = {"source_hashes": {name + ".py": guard.native.file_sha(Path(root) / "tools" / (name + ".py"))
                         for name in guard._RECEIVER_MODULES}, "threads": threads, "tf32_allowed": False}
        FakeWorker.instances.append(self)

    def request(self, request):
        self.requests.append(copy.deepcopy(request))
        if request["action"] == "prepare":
            self.precision, self.device = request["precision"], request["device"]
            return {"wall_seconds": .001 if self.precision == "fp32" else .002,
                    "input_hashes": {name: guard.native.file_sha(Path(request["input_directory"]) / name)
                                     for name in guard.fixtures.PUBLIC_FILES},
                    "source_hashes": self.identity["source_hashes"], "stages": {"load": .0001},
                    "geometry": {"sample_count": 100, "sample_rate": 10., "nfft": 256,
                                 "lags": 3, "lag_first_sample": 0, "lag_last_sample": 2,
                                 "lag_step_samples": 1, "group_tile": 16, "array_bound_bytes": 1024,
                                 "memory_limit_bytes": 2**20, "precision": self.precision, "device": self.device}}
        count = len(request["frequency_offsets_hz"])
        result = {"wall_seconds_including_finish": .001, "bit_log_likelihoods": [1., 2.],
                  "strict": {"accepted": False, "bit": 1}, "relaxed": {"accepted": True, "bit": 1},
                  "rank_histogram": [0, 3 * count], "measured_group_count": 3 * count,
                  "full_group_count_for_reporting": request["full_group_count_for_reporting"],
                  "geometry": {"group_count": 3 * count, "frequency_offsets_hz": request["frequency_offsets_hz"],
                               "sample_count": 100, "sample_rate": 10., "nfft": 256, "base_group_count": 3},
                  "arithmetic": {"requested_correlation_precision": self.precision, "observation_fft_device": self.device,
                                 "likelihood_bulk": self.precision, "tf32_allowed": False, "implicit_fallback": False}}
        if count == 8 and self.name == "candidate":
            if self.corruption == "count":
                result["measured_group_count"] -= 1
            elif self.corruption == "rank":
                result["rank_histogram"] = [0, 23]
            elif self.corruption == "noninteger_rank":
                result["rank_histogram"] = [0., 24.]
            elif self.corruption == "frequencies":
                result["geometry"]["frequency_offsets_hz"] = list(reversed(request["frequency_offsets_hz"]))
            elif self.corruption == "score":
                result["bit_log_likelihoods"] = [100., 2.]
            elif self.corruption == "arithmetic":
                result["arithmetic"]["tf32_allowed"] = True
        return result

    def close(self):
        self.closed = True


class GuardWorkloadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.api = guard._api()
        roots = {}
        for name in ("reference", "candidate"):
            roots[name] = self.directory / name
            for relative in self.api.SOURCE_FILES:
                path = roots[name] / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative)
            (roots[name] / "binary").write_text("executable identity")
        self.context = {"reference_root": roots["reference"], "candidate_root": roots["candidate"],
                        "binary": roots["candidate"] / "binary", "reference_binary": roots["reference"] / "binary",
                        "fixture_cache": self.directory / "cache", "output": self.directory / "output",
                        "threads": 2, "native_threads": 1, "device": "cpu", "deadline": time.monotonic() + 30}
        self.input_path = self.directory / "input"
        self.input_path.mkdir()
        for name in guard.fixtures.PUBLIC_FILES:
            (self.input_path / name).write_text(name)
        self.case = {"id": "test-fft", "input_directory": str(self.input_path), "lag_max_seconds": .2,
                     "lag_step_samples": 1, "frequency_offsets_hz": [i * .01 for i in range(8)],
                     "group_tile": 16, "memory_mib": 1,
                     "configuration": {"sample_count": 100, "lag_group_count": 3},
                     "fixture_hashes": {name: guard.native.file_sha(self.input_path / name)
                                        for name in guard.fixtures.PUBLIC_FILES},
                     "source_bound_metadata": {"cache_hit": True}}
        plan = self.directory / "plan.json"
        plan.write_text("{}")
        recipes = [{"id": "test-fft", "source_plan": "test", "source_case": "fixed"}]
        patches = [mock.patch.object(guard.fixtures, "scenarios", return_value=recipes),
                   mock.patch.object(guard.fixtures, "_config", return_value={"source_plans": {"test": str(plan)}}),
                   mock.patch.object(guard.fixtures, "prepare_cases", side_effect=lambda *a, **k: [copy.deepcopy(self.case)]),
                   mock.patch.object(self.api, "ReceiverWorker", FakeWorker)]
        self.prepare_mock = None
        for patch in patches:
            result = patch.start()
            self.addCleanup(patch.stop)
            if patch.attribute == "prepare_cases":
                self.prepare_mock = result
        FakeWorker.instances, FakeWorker.corruption = [], None

    def adapter(self):
        adapter = guard.NativeFFTWorkloads(self.context)
        self.addCleanup(adapter.close)
        return adapter

    @staticmethod
    def native_result(case, seconds, seed, **kwargs):
        return {"requested_seconds": seconds, "batch_trials": case.get("batch_trials", 1),
                "full_test_wall_seconds": 2., "regime": {"threads": kwargs["threads"]},
                "total_rows": 100, "counter_summary": {"basis_values": 400, "blocks": 2, "channel_samples": 100},
                "seed": seed, "native_results": [], "stage_seconds": {"receiver": 1., "generation_setup_other": .5,
                                                                          "process_startup_output": .5}}

    def test_inventory_is_frozen_without_running_or_preparing(self):
        adapter = self.adapter()
        with mock.patch.object(guard.native, "run_native") as run:
            descriptors = adapter.descriptors()
        self.assertEqual(len(descriptors), 10)  # Nine real native recipes plus the single mocked FFT recipe.
        self.assertIn("native-added-max", {row["id"] for row in descriptors})
        self.assertIn("fft:test-fft", {row["id"] for row in descriptors})
        self.assertEqual(adapter.lifecycle["events"], [])
        run.assert_not_called()
        self.prepare_mock.assert_not_called()
        self.assertEqual(FakeWorker.instances, [])
        descriptors[0]["id"] = "mutated"
        self.assertNotEqual(adapter.descriptors()[0]["id"], "mutated")

    def test_native_full_batch_is_one_outer_observation_and_respects_order(self):
        adapter = self.adapter()
        calls = []
        def invoke(binary, case, seconds, seed, **kwargs):
            calls.append((binary, seconds, seed, case["batch_trials"], kwargs["threads"]))
            return self.native_result(case, seconds, seed, **kwargs)
        with mock.patch.object(guard.native, "run_native", side_effect=invoke), mock.patch.object(
                guard.native, "compare_native_outputs", return_value={"eligible": True}) as compare:
            result = adapter.measure_pair("native-flat", "outer1", 111, ("candidate", "reference"))
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][0], self.context["binary"])
        self.assertEqual([row[1:] for row in calls], [(262.4, 111, 4, 1)] * 2)
        self.assertEqual(result["reference_seconds"], 2.)
        self.assertTrue(result["details"]["full_target"])
        compare.assert_called_once()
        self.prepare_mock.assert_not_called()

    def test_fft_full_bank_prepares_every_pair_but_reuses_workers_fixture_and_pilot(self):
        adapter = self.adapter()
        first = adapter.measure_pair("fft:test-fft", "screen", 1)
        second = adapter.measure_pair("fft:test-fft", "confirm1", 2, ("candidate", "reference"), phase="confirmation")
        self.assertGreater(first["candidate_seconds"], 0.)
        self.assertEqual(second["details"]["phase"], "confirmation")
        self.assertFalse(second["details"]["seed_applied_to_simulation"])
        self.assertEqual(second["details"]["precision_selection"]["selected"], "fp32")
        self.assertEqual(len(FakeWorker.instances), 2)
        for worker in FakeWorker.instances:
            scores = [r for r in worker.requests if r["action"] == "score"]
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in scores], [3, 3, 8, 8])
            self.assertEqual(len([r for r in worker.requests if r["action"] == "prepare"]), 4)
        self.prepare_mock.assert_called_once()
        kinds = [event["kind"] for event in adapter.lifecycle["events"]]
        self.assertEqual(kinds.count("fft_numerical_precision_pilot"), 1)
        self.assertEqual(kinds.count("worker_startup"), 2)
        adapter.close()
        self.assertTrue(all(worker.closed for worker in FakeWorker.instances))
        self.assertEqual(adapter.close(), adapter.lifecycle)

    def test_fft_full_bank_rejects_counts_ranks_frequency_or_score_corruption(self):
        for corruption in ("count", "rank", "noninteger_rank", "frequencies", "score", "arithmetic"):
            with self.subTest(corruption=corruption):
                adapter = self.adapter()
                FakeWorker.corruption = corruption
                with self.assertRaises(ValueError):
                    adapter.measure_pair("fft:test-fft", "screen", 1)
                adapter.close()

    def test_forced_fp64_skips_unused_fp32_pilots_and_stays_forced(self):
        self.context["precision"] = "fp64"
        adapter = self.adapter()
        first = adapter.measure_pair("fft:test-fft", "screen", 1)
        second = adapter.measure_pair("fft:test-fft", "confirm", 2, phase="confirmation")
        selection = second["details"]["precision_selection"]
        self.assertEqual(selection["selected"], "fp64")
        self.assertEqual(selection["requested_precision"], "fp64")
        self.assertFalse(selection["fallback"])
        self.assertEqual(first["details"]["precision_selection"], selection)
        for worker in FakeWorker.instances:
            self.assertEqual([r["precision"] for r in worker.requests if r["action"] == "prepare"], ["fp64"] * 3)
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in worker.requests if r["action"] == "score"], [3, 8, 8])

    def test_fp32_floor_may_explicitly_select_faster_qualified_fp64(self):
        adapter = self.adapter()
        original = FakeWorker.request
        def faster_fp64(worker, request):
            result = original(worker, request)
            if request["action"] == "prepare" and request["precision"] == "fp64":
                result["wall_seconds"] = .0001
            return result
        with mock.patch.object(FakeWorker, "request", faster_fp64):
            result = adapter.measure_pair("fft:test-fft", "screen", 1)
        selection = result["details"]["precision_selection"]
        self.assertEqual(selection["selected"], "fp64")
        self.assertEqual(selection["requested_precision"], "fp32")
        self.assertTrue(selection["higher_precision_selected"])
        self.assertFalse(selection["fallback"])

    def test_missing_nonfinite_or_non_numeric_deadline_is_rejected(self):
        for value in (None, float("inf"), float("nan"), True, "later"):
            with self.subTest(value=value):
                self.context["deadline"] = value
                with self.assertRaisesRegex(ValueError, "explicit finite"):
                    guard.NativeFFTWorkloads(self.context)
        del self.context["deadline"]
        with self.assertRaisesRegex(ValueError, "explicit finite"):
            guard.NativeFFTWorkloads(self.context)
        self.assertEqual(FakeWorker.instances, [])

    def test_direct_fft_allows_verified_private_memory_optimization(self):
        adapter = self.adapter()
        original = FakeWorker.request
        def changed_memory(worker, request):
            result = original(worker, request)
            if request["action"] == "prepare" and worker.name == "candidate":
                result["geometry"]["array_bound_bytes"] = 512
            return result
        with mock.patch.object(FakeWorker, "request", changed_memory):
            pair = adapter.measure_pair("fft:test-fft", "screen", 1)
        self.assertTrue(pair["details"]["agreement"]["eligible"])
        work = adapter.work_identities["test-fft"]
        self.assertNotEqual(work["reference"]["prepare"]["array_bound_bytes"], work["candidate"]["prepare"]["array_bound_bytes"])

    def test_source_and_fixture_changes_are_integrity_failures(self):
        adapter = self.adapter()
        adapter.measure_pair("fft:test-fft", "screen", 1)
        (self.input_path / "observation.c128").write_text("different waveform")
        with self.assertRaisesRegex(ValueError, "fixture bytes"):
            adapter.measure_pair("fft:test-fft", "confirm", 2, phase="confirmation")
        path = self.context["candidate_root"] / self.api.SOURCE_FILES[0]
        path.write_text("changed receiver")
        with self.assertRaisesRegex(ValueError, "changed during guard"):
            adapter.descriptors()

    def test_native_regime_change_does_not_extrapolate_or_become_timing_failure(self):
        adapter = self.adapter()
        with mock.patch.object(guard.native, "run_native", side_effect=lambda b, c, s, seed, **k: self.native_result(c, s, seed, **k)), mock.patch.object(
                guard.native, "compare_native_outputs", return_value={"eligible": True}):
            adapter.measure_pair("native-flat", "screen", 1)
            adapter.work_identities["native-flat"]["candidate"]["total_rows"] -= 1
            with self.assertRaisesRegex(ValueError, "regime changed"):
                adapter.measure_pair("native-flat", "confirm", 2, phase="confirmation")

    def test_native_timeout_and_correctness_error_remain_distinct(self):
        adapter = self.adapter()
        with mock.patch.object(guard.native, "run_native", side_effect=subprocess.TimeoutExpired("native", 1)):
            with self.assertRaises(TimeoutError):
                adapter.measure_pair("native-flat", "timedout", 1)
        with mock.patch.object(guard.native, "run_native", side_effect=lambda b, c, s, seed, **k: self.native_result(c, s, seed, **k)), mock.patch.object(
                guard.native, "compare_native_outputs", side_effect=guard.native.NativePerformanceError("wrong score")):
            with self.assertRaisesRegex(ValueError, "correctness/work"):
                adapter.measure_pair("native-flat", "wrong", 1)

    def test_budget_unknown_case_and_reused_observation_fail_before_measurement(self):
        adapter = self.adapter()
        with self.assertRaises(ValueError):
            adapter.measure_pair("unknown", "bad", 1)
        self.prepare_mock.assert_not_called()
        adapter.measure_pair("fft:test-fft", "screen", 1)
        with self.assertRaisesRegex(ValueError, "globally fresh"):
            adapter.measure_pair("fft:test-fft", "screen", 1, phase="confirmation")
        adapter.deadline = time.monotonic() - 1
        with self.assertRaises(TimeoutError):
            adapter.measure_pair("fft:test-fft", "later", 2)


if __name__ == "__main__":
    unittest.main()

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
import triad_perf_guard as suite_guard


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

    def test_historical_serpent_absence_is_bound_and_candidate_stays_required(self):
        for relative in guard.fixtures.OPTIONAL_HISTORICAL_FILES:
            (self.context["reference_root"] / relative).unlink()
        adapter = self.adapter()
        for relative in guard.fixtures.OPTIONAL_HISTORICAL_FILES:
            self.assertEqual(adapter.sources["reference:" + relative], guard.fixtures.ABSENT_SOURCE)
        adapter._check_sources()
        (self.context["reference_root"] / "tools/triad_fixture_key.hpp").write_text("appeared")
        with self.assertRaisesRegex(ValueError, "changed during guard"):
            adapter._check_sources()
        (self.context["reference_root"] / "tools/triad_fixture_key.hpp").unlink()
        driver = self.context["reference_root"] / "tools/triad-long.cpp"
        driver.write_text('#include "triad_fixture_key.hpp"\n')
        with self.assertRaisesRegex(ValueError, "includes missing fixture"):
            self.adapter()
        driver.write_text("restored")
        (self.context["reference_root"] / "src/serpent.cpp").write_text("appeared")
        with self.assertRaisesRegex(ValueError, "incomplete Serpent implementation"):
            adapter._check_sources()
        (self.context["reference_root"] / "src/serpent.cpp").unlink()
        (self.context["reference_root"] / "src/serpent.cpp").symlink_to("missing")
        with self.assertRaisesRegex(ValueError, "missing or symlinked"):
            self.adapter()
        (self.context["reference_root"] / "src/serpent.cpp").unlink()
        (self.context["candidate_root"] / "src/serpent.cpp").unlink()
        with self.assertRaisesRegex(ValueError, "missing or symlinked"):
            self.adapter()
        (self.context["candidate_root"] / "src/serpent.cpp").write_text("restored")
        (self.context["reference_root"] / "src/crypto.cpp").unlink()
        with self.assertRaisesRegex(ValueError, "missing or symlinked.*crypto.cpp"):
            self.adapter()

    def test_source_ancestor_symlinks_are_rejected(self):
        root = self.context["reference_root"]
        adapter = self.adapter()
        (root / "src").rename(root / "real-src")
        (root / "src").symlink_to(root / "real-src", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlinked.*reference/src"):
            self.adapter()
        with self.assertRaisesRegex(ValueError, "symlinked.*reference/src"):
            adapter._check_sources()

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

    def test_explicit_physical_alignment_preflights_before_one_four_trial_command(self):
        self.context["physical_bit_alignment"]=True
        adapter=self.adapter();events=[]
        alignment={"schema":guard.physical.SCHEMA,"trials":4,"physical_signal_bits":[0,1,0,1],"sources":{
            "reference":{"tx_masks":[0,1,0,1],"rx_masks":[1,0,1,0]},
            "candidate":{"tx_masks":[1,1,0,0],"rx_masks":[0,1,1,0]}}}
        complete={"bound":"all samples"}
        def prepare(*args,**kwargs):events.append("source-only");return alignment
        def preflight(*args,**kwargs):events.append("complete-input");return complete
        def run(binary,case,seconds,seed,**kwargs):
            side="candidate" if binary==self.context["binary"] else "reference"
            events.append(side)
            self.assertEqual(case["batch_trials"],4)
            self.assertEqual(case["args"][-6:],guard.physical.arguments(alignment,side))
            return self.native_result(case,seconds,seed,**kwargs)
        with mock.patch.object(guard.physical,"prepare_pair",side_effect=prepare), mock.patch.object(
                guard.physical,"complete_native_inputs",side_effect=preflight), mock.patch.object(
                guard.physical,"validate_native_receipt") as binding, mock.patch.object(
                guard.physical,"validate_native_input_binding") as full_binding, mock.patch.object(
                guard.native,"run_native",side_effect=run), mock.patch.object(
                guard.native,"compare_native_outputs",return_value={"eligible":True}) as strict:
            result=adapter.measure_pair("native-flat","aligned",111,("candidate","reference"))
        self.assertEqual(events,["source-only","complete-input","candidate","reference"])
        self.assertEqual(binding.call_count,2);self.assertEqual(full_binding.call_count,2)
        strict.assert_called_once()
        self.assertEqual(result["details"]["physical_source_alignment"],alignment)
        self.assertEqual(result["details"]["complete_input_preflight"],complete)

    def test_native_confirmation_has_real_invocation_owned_contexts(self):
        adapter=self.adapter()
        with mock.patch.object(guard.native,"run_native",side_effect=lambda b,c,t,seed,**k:self.native_result(c,t,seed,**k)), mock.patch.object(
                guard.native,"compare_native_outputs",return_value={"eligible":True}):
            first=adapter.measure_pair("native-null","native-confirm0",111,("candidate","reference"),phase="confirmation")
            second=adapter.measure_pair("native-null","native-confirm1",112,("reference","candidate"),phase="confirmation")
        used=set()
        for result in [first,second]:
            context=result["details"]["confirmation_context"]
            values,reason=suite_guard._context_receipt(result,result["details"]["order"],used)
            self.assertIsNone(reason)
            self.assertEqual(len(values),2)
            for evidence in context["isolation_evidence"].values():
                self.assertTrue(Path(evidence["raw_launch_receipt"]).is_file())
        self.assertEqual(len(used),4)

    def test_context_protocol_failure_remains_hard_after_process_cleanup(self):
        adapter=self.adapter();worker=mock.Mock();alive={"value":True}
        worker.process.pid=123
        worker.process.poll.side_effect=lambda:None if alive["value"] else 0
        worker.request.side_effect=adapter.api.SuiteError("close rejected")
        worker.process.stdin.closed=False;worker.process.stdout.closed=False;worker.log.closed=False
        worker.reader.is_alive.return_value=False
        def close():
            alive["value"]=False
            worker.process.stdin.closed=True;worker.process.stdout.closed=True;worker.log.closed=True
        worker.close.side_effect=close
        adapter.workers={"reference":worker};adapter._worker_context={"id":"broken-protocol"}
        with self.assertRaisesRegex(RuntimeError,"close protocol failure"):
            adapter._close_worker_context("test")
        self.assertFalse(alive["value"])
        self.assertFalse(adapter.workers)
        record=adapter.lifecycle["context_cleanup"][-1]["workers"]["reference"]
        self.assertTrue(record["cleanup_verified"])
        self.assertIn("SuiteError",record["close_protocol_error"])

    def test_acknowledged_shutdown_timeout_is_owned_cleanup_not_random_failure(self):
        adapter=self.adapter();worker=mock.Mock();alive={"value":True}
        worker.process.pid=123
        worker.process.poll.side_effect=lambda:None if alive["value"] else -15
        worker.request.return_value={"closed":True}
        worker.process.wait.side_effect=subprocess.TimeoutExpired("worker",2.)
        worker.process.stdin.closed=False;worker.process.stdout.closed=False;worker.log.closed=False
        worker.reader.is_alive.return_value=False
        def close():
            self.assertLess(worker.deadline,time.monotonic())
            alive["value"]=False
            worker.process.stdin.closed=True;worker.process.stdout.closed=True;worker.log.closed=True
        worker.close.side_effect=close
        adapter.workers={"reference":worker};adapter._worker_context={"id":"slow-shutdown"}
        result=adapter._close_worker_context("test")
        self.assertFalse(result["cleanup_errors"])
        self.assertTrue(result["workers"]["reference"]["cleanup_verified"])
        self.assertEqual(result["workers"]["reference"]["cleanup_mode"],"owned_forced_after_timeout")
        self.assertEqual(worker.request.call_count,1)

    def test_already_dead_nonzero_worker_is_not_silently_clean(self):
        adapter=self.adapter();worker=mock.Mock()
        worker.process.pid=124;worker.process.poll.return_value=1
        worker.process.stdin.closed=True;worker.process.stdout.closed=True;worker.log.closed=True
        worker.reader.is_alive.return_value=False
        adapter.workers={"reference":worker};adapter._worker_context={"id":"crashed-worker"}
        with self.assertRaisesRegex(RuntimeError,"abnormally outside owned"):
            adapter._close_worker_context("test")
        worker.request.assert_not_called()
        worker.close.assert_called_once()
        self.assertEqual(adapter.lifecycle["context_cleanup"][-1]["workers"]["reference"]["pre_cleanup_exit"],1)

    def test_fft_confirmation_rebuilds_contexts_and_full_case_warms_outside_target(self):
        adapter = self.adapter()
        first = adapter.measure_pair("fft:test-fft", "screen", 1)
        second = adapter.measure_pair("fft:test-fft", "confirm1", 2, ("candidate", "reference"), phase="confirmation")
        self.assertGreater(first["candidate_seconds"], 0.)
        self.assertEqual(second["details"]["phase"], "confirmation")
        self.assertFalse(second["details"]["seed_applied_to_simulation"])
        self.assertEqual(second["details"]["precision_selection"]["selected"], "fp32")
        self.assertEqual(len(FakeWorker.instances), 4)
        for worker in FakeWorker.instances[:2]:
            scores = [r for r in worker.requests if r["action"] == "score"]
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in scores], [3, 3, 8])
            self.assertEqual(len([r for r in worker.requests if r["action"] == "prepare"]), 3)
        for worker in FakeWorker.instances[2:]:
            scores = [r for r in worker.requests if r["action"] == "score"]
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in scores], [8, 8])
            self.assertEqual(len([r for r in worker.requests if r["action"] == "prepare"]), 2)
        context = second["details"]["confirmation_context"]
        self.assertEqual(context["protocol"], "fresh-source-context-pair-v1")
        self.assertEqual(context["creation_order"], ["candidate", "reference"])
        self.assertTrue(context["target_boundary_preserved"])
        self.assertTrue(context["setup_warmup_costs_charged_to_campaign"])
        self.assertEqual(context["warmup_policy"], "full-target-outside-timer")
        self.assertEqual(context["teardown"]["reason"], "confirmation_period_complete")
        self.assertEqual(len(set(context["context_ids"].values())), 2)
        self.assertTrue(all(event["warmup"]["measured_group_count"] == 24
                            for event in context["workers"].values()))
        accepted, reason = suite_guard._context_receipt(second, ("candidate", "reference"), set())
        self.assertIsNone(reason)
        self.assertEqual(set(accepted), set(context["context_ids"].values()))
        self.prepare_mock.assert_called_once()
        kinds = [event["kind"] for event in adapter.lifecycle["events"]]
        self.assertEqual(kinds.count("fft_numerical_precision_pilot"), 1)
        self.assertEqual(kinds.count("worker_startup"), 4)
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
        for worker in FakeWorker.instances[:2]:
            self.assertEqual([r["precision"] for r in worker.requests if r["action"] == "prepare"], ["fp64"] * 2)
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in worker.requests if r["action"] == "score"], [3, 8])
        for worker in FakeWorker.instances[2:]:
            self.assertEqual([r["precision"] for r in worker.requests if r["action"] == "prepare"], ["fp64"] * 2)
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in worker.requests if r["action"] == "score"], [8, 8])

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

#!/usr/bin/env python3
"""Cheap orchestration/forecast contract checks; no stress calibration campaign."""
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("triad_performance_runner", ROOT / "tools/triad-performance.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class FakeWorker:
    def __init__(self, factor=1.):
        self.factor = factor
        self.requests = []

    def request(self, request):
        self.requests.append(request)
        if request["action"] == "prepare":
            return {"wall_seconds": self.factor * 2., "geometry": {
                "sample_count": 100000, "sample_rate": 1024., "nfft": 200000, "lags": 97,
                "lag_first_sample": 0, "lag_last_sample": 96, "lag_step_samples": 1,
                "group_tile": 32, "precision": request["precision"], "device": request["device"],
                "array_bound_bytes": int(self.factor * 1000000)}}
        return {"wall_seconds_including_finish": self.factor * (.5 + .25 * len(request["frequency_offsets_hz"])),
                "bit_log_likelihoods": [1., 2.], "strict": {"accepted": False, "bit": 1},
                "relaxed": {"accepted": False, "bit": 1}, "rank_histogram": [1, 2],
                "geometry": {"group_count": 97 * len(request["frequency_offsets_hz"])},
                "measured_group_count": 97 * len(request["frequency_offsets_hz"])}


def case():
    return {"id": "example", "input_directory": "/not/read/by/fake", "lag_max_seconds": .1,
            "frequency_offsets_hz": [i * .01 for i in range(-16, 17)],
            "training_counts": [1, 5], "holdout_count": 11, "group_tile": 32,
            "configuration": {"lag_group_count": 97}, "fixture_scope": "fixture",
            "fixture_hashes": {}, "source_bound_metadata": {}}


class OrchestrationTests(unittest.TestCase):
    def test_cfo_selection_is_public_unique_and_preserves_endpoints(self):
        values = list(range(-100, 101))
        self.assertEqual(runner.frequency_subset(values, 1), [0])
        for count in (2, 3, 5, 17, 201):
            selected = runner.frequency_subset(values, count)
            self.assertEqual(len(selected), count)
            self.assertEqual((selected[0], selected[-1]), (-100, 100))
        with self.assertRaises(ValueError):
            runner.frequency_subset(values, 202)

    def test_prediction_absolute_times_describe_full_geometry_full_cfo_work(self):
        workers = {"reference": FakeWorker(), "candidate": FakeWorker(1.05)}
        result = runner.measure_fft(case(), workers, "fp32", "cpu", repeats=3, confidence=.95)
        self.assertTrue(result["prediction_valid"])
        # 2 seconds full preparation + .5 fixed score + 33 * .25 per CFO.
        self.assertAlmostEqual(result["prediction"]["reference"]["predicted_full_seconds_by_repeat"][0], 10.75)
        self.assertAlmostEqual(result["comparison"]["mean_candidate_full_seconds"], 10.75 * 1.05)
        self.assertEqual(result["comparison"]["gate"], "inconclusive")
        self.assertFalse(result["full_target_measured"])
        prepared = [r for r in workers["reference"].requests if r["action"] == "prepare"]
        self.assertEqual(len(prepared), 3)
        self.assertTrue(all(r["lag_max_seconds"] == .1 for r in prepared))

    def test_full_fallback_measures_only_complete_target(self):
        workers = {"reference": FakeWorker(), "candidate": FakeWorker()}
        result = runner.measure_fft(case(), workers, "fp64", "cpu", repeats=3, confidence=.95, force_direct=True)
        self.assertTrue(result["full_target_measured"])
        self.assertEqual(result["comparison"]["gate"], "pass")
        self.assertEqual(result["prediction"]["candidate"]["status"], "direct_full_measurement")
        for worker in workers.values():
            scores = [r for r in worker.requests if r["action"] == "score"]
            self.assertEqual([len(r["frequency_offsets_hz"]) for r in scores], [33, 33, 33])
        bounds = result["prediction"]["candidate"]["predicted_full_time"]
        self.assertIn("conditional_prediction_interval_seconds", bounds)

    def test_numerical_admission_and_rank_changes_disqualify_fast_arithmetic(self):
        expected = {"bit_log_likelihoods": [12., 22.7211], "strict": {"accepted": True, "bit": 1},
                    "relaxed": {"accepted": True, "bit": 1}, "rank_histogram": [0, 0, 0, 0, 1]}
        value = json.loads(json.dumps(expected))
        self.assertTrue(runner.numeric_agreement(expected, value)["eligible"])
        value["strict"]["accepted"] = False
        self.assertFalse(runner.numeric_agreement(expected, value)["eligible"])
        value = json.loads(json.dumps(expected)); value["rank_histogram"] = [0, 0, 0, 1, 0]
        self.assertFalse(runner.numeric_agreement(expected, value)["eligible"])
        value = json.loads(json.dumps(expected)); value["bit_log_likelihoods"][1] = math.nan
        self.assertFalse(runner.numeric_agreement(expected, value)["eligible"])

    def test_mixed_full_and_larger_holdouts_use_each_actual_count(self):
        selected = case(); selected["full_holdout_first_repeat"] = True
        result = runner.measure_fft(selected, {"reference": FakeWorker(), "candidate": FakeWorker()},
                                    "fp32", "cpu", repeats=3, confidence=.95)
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["heldout_cfo_counts"], [33, 11, 11])
        self.assertEqual(result["full_target_measurement_count_per_source"], 1)
        self.assertTrue(all(abs(x) < 1e-12 for x in result["comparison"]["paired_model_log_errors"]))

    def test_estimation_is_skipped_when_calibration_costs_as_much_as_full_work(self):
        selected = case(); selected["training_counts"] = [3, 9]
        selected["holdout_count"] = 17; selected["full_holdout_first_repeat"] = True
        result = runner.measure_fft(selected, {"reference": FakeWorker(), "candidate": FakeWorker()},
                                    "fp32", "cpu", repeats=3, confidence=.95)
        self.assertEqual(result["measurement_plan"]["abbreviated_cfo_work"], 103)
        self.assertEqual(result["measurement_plan"]["full_cfo_work"], 99)
        self.assertTrue(result["measurement_plan"]["direct"])
        self.assertEqual(result["heldout_cfo_counts"], [33,33,33])
        self.assertEqual(result["prediction"]["candidate"]["status"], "direct_full_measurement")

    def test_large_free_running_bank_keeps_useful_abbreviation(self):
        selected = case(); selected["frequency_offsets_hz"] = [i*.01 for i in range(992)]
        selected["training_counts"] = [1, 124]; selected["holdout_count"] = 249
        result = runner.measure_fft(selected, {"reference": FakeWorker(), "candidate": FakeWorker()},
                                    "fp32", "cpu", repeats=3, confidence=.95)
        self.assertFalse(result["measurement_plan"]["direct"])
        self.assertAlmostEqual(result["measurement_plan"]["planned_abbreviated_over_full_work"], 374/992)
        self.assertTrue(result["prediction_valid"])

    def test_wrong_large_bank_result_cannot_pass_as_speedup(self):
        class WrongBank(FakeWorker):
            def request(self, request):
                result = super().request(request)
                if request["action"] == "score" and len(request["frequency_offsets_hz"]) > 3:
                    result["bit_log_likelihoods"] = [100., 2.]
                return result
        with self.assertRaisesRegex(runner.SuiteError, "measured-bank"):
            runner.measure_fft(case(), {"reference": FakeWorker(), "candidate": WrongBank()},
                               "fp32", "cpu", repeats=3, confidence=.95)

    def test_hardware_low_utilization_and_lost_capability_remain_visible(self):
        profile = {"device": "cpu", "hardware": {"requested_threads": 2, "cpu": {"model name": "fixture"}},
                   "cases": {"gemm": {"status": "complete", "operations_per_second": 10.,
                                      "process_cpu_cores_used": 2.}}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"profile.json"; path.write_text(json.dumps(profile))
            current = json.loads(json.dumps(profile)); current["cases"]["gemm"]["process_cpu_cores_used"] = .5
            result = runner.hardware_health(current, path)
            self.assertTrue(result["warning"])
            self.assertTrue(result["rows"][0]["low_cpu_utilization_screen"])
            current["cases"]["gemm"]["status"] = "unsupported"
            self.assertTrue(runner.hardware_health(current, path)["warning"])

    def test_immutable_baseline_reference_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(runner.SuiteError):
                runner.extract_baseline(ROOT, "HEAD", directory)
            with self.assertRaises(runner.SuiteError):
                runner.extract_baseline(ROOT, "242ea15", directory)

    def test_missing_coverage_is_not_reported_as_pass(self):
        args = runner.parse_args(["--output", "/tmp/not-created-triad-test.json", "--binary", "/tmp/no-binary"])
        report = runner.initial_report(args, "cpu")
        self.assertFalse(report["complete_required_coverage"])
        self.assertEqual(report["status"], "running")
        self.assertEqual(report["native_receiver_performance"], "unmeasured")
        unavailable = runner.hardware_health({"device": "cpu"}, None)
        self.assertEqual(unavailable["status"], "hardware_baseline_unavailable")
        self.assertFalse(unavailable["performance_pass_eligible"])

    @unittest.skipUnless(os.name == "posix", "POSIX process-group deadline")
    def test_watchdog_terminates_a_blocked_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            start = time.monotonic()
            with self.assertRaises(subprocess.TimeoutExpired):
                runner.supervise([sys.executable, "-c", "import time; time.sleep(30)"], timeout=.1,
                    stdout=Path(directory)/"stdout", stderr=Path(directory)/"stderr")
            self.assertLess(time.monotonic()-start, 3.)

    @unittest.skipUnless(os.name == "posix" and Path("/proc").exists(), "Linux orphan cleanup")
    def test_watchdog_kills_orphan_after_leader_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            pid_file = Path(directory)/"child.pid"
            script = "import os,signal,time,pathlib; p=os.fork(); " + \
                "signal.signal(signal.SIGTERM,signal.SIG_IGN) if p==0 else None; " + \
                "pathlib.Path(" + repr(str(pid_file)) + ").write_text(str(os.getpid())) if p==0 else time.sleep(.1); " + \
                "time.sleep(30) if p==0 else None"
            self.assertEqual(runner.supervise([sys.executable,"-c",script],timeout=2,
                stdout=Path(directory)/"stdout",stderr=Path(directory)/"stderr"),0)
            child = int(pid_file.read_text())
            for _ in range(50):
                stat = Path(f"/proc/{child}/stat")
                if not stat.exists() or stat.read_text().split()[2] == "Z":
                    break
                time.sleep(.01)
            else:
                self.fail("orphan descendant survived process-group cleanup")

    @unittest.skipUnless(os.name == "posix", "POSIX receiver protocol")
    def test_persistent_worker_failure_is_explicit_and_reaped(self):
        with tempfile.TemporaryDirectory() as directory:
            adapter = Path(directory)/"adapter.py"
            adapter.write_text("import sys,json\nfor line in sys.stdin:\n r=json.loads(line)\n print(json.dumps({'ok':False,'error':'intentional startup failure'}),flush=True)\n")
            with self.assertRaisesRegex(runner.SuiteError, "intentional startup failure"):
                runner.ReceiverWorker(ROOT, threads=1, deadline=time.monotonic()+3,
                                      log_directory=directory, name="bad", adapter=adapter)


if __name__ == "__main__":
    unittest.main()

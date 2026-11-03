"""Cheap orchestration contracts; all receiver and calibration costs are mocked."""
from __future__ import annotations

from contextlib import ExitStack, contextmanager
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "triad_performance_integration_runner", ROOT / "tools/triad-performance.py")
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)

import triad_perf_cases as cases_module
import triad_perf_fft_trace as fft_module
import triad_perf_hardware as hardware_module
import triad_perf_native as native_module
import triad_perf_sustained as sustained_module


def measured(identifier, *, qualified=True, fft=False):
    return {"id": identifier, "prediction_valid": qualified,
            "adaptive": {"timing_precision_reached": True},
            "comparison": {"gate": "pass" if qualified else "unvalidated"},
            "anchors": {"status": "validated" if qualified else "unvalidated"} if fft else {}}


class FakeWorker:
    instances = []

    def __init__(self, source, *, name, **kwargs):
        self.name = name
        self.identity = {"mock_source": str(source)}
        self.external_startup_seconds = 0.
        self.closed = False
        self.requests = []
        self.instances.append(self)

    def request(self, request):
        self.requests.append(request)
        return {}

    def close(self):
        self.closed = True


class SustainedOrchestrationTests(unittest.TestCase):
    @contextmanager
    def harness(self, *, qualified=True, enabled=True, selected=None, severe_endpoint=False):
        """Keep real CLI, selection, report and commitment-writing code."""
        native_cases = [{"id": "native-largest", "target_seconds": 1000.},
                        {"id": "native-smallest", "target_seconds": 10.},
                        {"id": "native-middle", "target_seconds": 100.}]
        fft_cases = [{"id": "fft-middle", "frequency_offsets_hz": list(range(5))},
                     {"id": "fft-largest", "frequency_offsets_hz": list(range(9))},
                     {"id": "fft-smallest", "frequency_offsets_hz": list(range(3))}]
        for case in fft_cases:
            case["configuration"] = {"lag_group_count": 2}
        FakeWorker.instances = []
        with tempfile.TemporaryDirectory(prefix="triad-perf-integration-") as directory, ExitStack() as stack:
            output = Path(directory) / "report.json"
            # This existing file supplies an identity only: no executable is run.
            binary = ROOT / "tools/triad-performance.py"
            args = runner.parse_args(["--output", str(output), "--binary", str(binary),
                                      "--reference-binary", str(binary), "--profile", "ci",
                                      "--sustained-seconds", "60" if enabled else "0",
                                      *[argument for name in (selected or []) for argument in ("--case", name)]])
            args._backend_worker = "cpu"
            report = runner.initial_report(args, "cpu")
            commitments = []

            def path_process(argv, **kwargs):
                Path(argv[argv.index("--output") + 1]).write_text(json.dumps(
                    {"complete_applicable_coverage": True}))
                return subprocess.CompletedProcess(argv, 0, "", "")

            def eligibility(result, case):
                self.assertEqual(result["id"], case["id"])
                if not qualified:
                    raise ValueError("mock forecast has no independent qualification")
                return "joint_case_qualified"

            def campaign(kind, callback):
                commitment = {"kind": kind, "frozen_before_run": True}
                path = output.parent / (output.stem + ".work") / (kind + "-sustained-commitment.json")
                self.assertFalse(path.exists())
                callback(commitment)
                # A simulated operation cannot start before the runner's
                # supplied callback has persisted the frozen commitment.
                self.assertEqual(json.loads(path.read_text()), commitment)
                commitments.append(kind)
                return {"kind": kind, "sustained_threshold_reached": True,
                        "qualification_claim": False}

            def native_campaign(binary, case, result, **kwargs):
                self.assertEqual(result["id"], case["id"])
                self.assertEqual(kwargs["minimum_seconds"], 60.)
                self.assertEqual(kwargs["threads"], args.native_threads)
                self.assertIn("deadline", kwargs)
                return campaign("native", kwargs["freeze_callback"])

            def fft_campaign(case, worker, result, api, **kwargs):
                self.assertEqual(worker.name, "candidate")
                self.assertEqual(result["id"], case["id"])
                self.assertIs(api, runner)
                self.assertEqual(kwargs["minimum_seconds"], 60.)
                self.assertIn("deadline", kwargs)
                return campaign("fft", kwargs["freeze_callback"])

            for owner, name, value in (
                    (hardware_module, "collect_profile", {"status": "complete", "hardware": {}}),
                    (cases_module, "scenarios", fft_cases),
                    (cases_module, "prepare_cases", fft_cases),
                    (native_module, "scenarios", native_cases),
                    (native_module, "identity", {"mock": "identity"}),
                    (native_module, "anchor_compatible", False),
                    (runner, "hardware_health", {}),
                    (runner, "extract_baseline", {}),
                    (runner, "qualify_fft", {"selected": "fp32"}),
                    (runner, "measure_gaussian_adaptive", measured("gaussian-multiblock"))):
                stack.enter_context(patch.object(owner, name, return_value=value))
            stack.enter_context(patch.object(native_module, "measure_native_case", autospec=True,
                side_effect=lambda reference, candidate, case, policy, **kw: measured(case["id"], qualified=qualified)))
            def fft_result(case, *a, **kw):
                result = measured(case["id"], qualified=qualified, fft=True)
                if severe_endpoint and case["id"] == "fft-largest":
                    result["prediction_valid"] = False
                    result["comparison"]["gate"] = "inconclusive"
                    result["anchors"]["conditional_check"] = {
                        "schema": "triad-performance-conditional-v2",
                        "status": "unvalidated_request_focused_qualification",
                        "checks": {"candidate": {"log_actual_over_predicted": .43,
                            "training_timing_log_half_width": .041,
                            "pre_sentinel_historical_observation_log_envelope": .0953}}}
                return result
            stack.enter_context(patch.object(fft_module, "measure_fft", autospec=True,
                                             side_effect=fft_result))
            stack.enter_context(patch.object(runner, "ReceiverWorker", FakeWorker))
            stack.enter_context(patch.object(runner.subprocess, "run", side_effect=path_process))
            # Source identity guards have separate tests. Avoid hashing the
            # checkout here, including files another development task may edit.
            stack.enter_context(patch.object(runner, "sha", return_value="mock-source-hash"))
            eligible = stack.enter_context(patch.object(sustained_module, "_require_native_candidate",
                                                       autospec=True, side_effect=eligibility))
            native = stack.enter_context(patch.object(sustained_module, "native_sustained",
                                                     autospec=True, side_effect=native_campaign))
            fft = stack.enter_context(patch.object(sustained_module, "fft_sustained",
                                                  autospec=True, side_effect=fft_campaign))
            code = runner.run_backend(args, report)
            yield {"code": code, "report": report, "native": native, "fft": fft,
                   "eligibility": eligible, "native_cases": native_cases,
                   "commitments": commitments, "persisted": json.loads(output.read_text()),
                   "hardware": hardware_module.collect_profile, "fixtures": cases_module.prepare_cases}

    def test_selects_largest_qualified_cases_once_and_freezes_before_work(self):
        with self.harness() as result:
            self.assertEqual(result["code"], 0, result["report"]["failures"])
            self.assertEqual(result["report"]["status"], "passed")
            self.assertTrue(result["report"]["complete_required_coverage"])
            self.assertEqual(result["native"].call_count, 1)
            self.assertEqual(result["native"].call_args.args[1]["id"], "native-largest")
            self.assertEqual(result["fft"].call_count, 1)
            self.assertEqual(result["fft"].call_args.args[0]["id"], "fft-largest")
            self.assertEqual(result["commitments"], ["native", "fft"])
            self.assertEqual([call.args[1] for call in result["eligibility"].call_args_list],
                             result["native_cases"])
            self.assertEqual(len(result["persisted"]["sustained_checks"]), 2)
            self.assertTrue(all(worker.closed for worker in FakeWorker.instances))

    def test_unqualified_forecasts_warn_without_running_sustained_work(self):
        with self.harness(qualified=False) as result:
            self.assertEqual(result["code"], 0, result["report"]["failures"])
            self.assertEqual(result["report"]["status"], "warning")
            self.assertTrue(result["report"]["complete_required_coverage"])
            result["native"].assert_not_called()
            result["fft"].assert_not_called()
            self.assertEqual(result["report"]["sustained_checks"], [])
            self.assertIn("native sustained check unavailable: no independently qualified candidate forecast",
                          result["report"]["warnings"])
            self.assertIn("FFT sustained check unavailable: no validated full-bank forecast",
                          result["report"]["warnings"])

    def test_disabled_sustained_work_does_not_qualify_or_run_campaigns(self):
        with self.harness(enabled=False) as result:
            self.assertEqual(result["code"], 0, result["report"]["failures"])
            self.assertEqual(result["report"]["status"], "passed")
            result["eligibility"].assert_not_called()
            result["native"].assert_not_called()
            result["fft"].assert_not_called()
            self.assertEqual(result["report"]["sustained_checks"], [])

    def test_focused_case_keeps_multiplicity_and_cannot_claim_complete_ci(self):
        with self.harness(enabled=False, selected=["native-middle"]) as result:
            self.assertEqual(result["code"], 0, result["report"]["failures"])
            self.assertEqual([row["id"] for row in result["report"]["cases"]], ["native-middle"])
            self.assertEqual(result["report"]["adaptive_policy"]["families"], 7)
            self.assertFalse(result["report"]["complete_required_coverage"])
            self.assertEqual(FakeWorker.instances, [])
            result["fixtures"].assert_not_called()
            self.assertEqual(result["report"]["status"], "warning")

    def test_repeatable_fft_selection_filters_fixture_work_and_native_claim(self):
        with self.harness(enabled=False, selected=["fft-smallest", "fft-middle"]) as result:
            self.assertEqual(result["code"], 0, result["report"]["failures"])
            self.assertEqual(result["fixtures"].call_args.kwargs["case_ids"], ["fft-middle", "fft-smallest"])
            self.assertEqual([row["id"] for row in result["report"]["cases"]], ["fft-middle", "fft-smallest"])
            self.assertIn("unmeasured", result["report"]["native_receiver_performance"])
            self.assertFalse(result["report"]["complete_required_coverage"])

    def test_unknown_focused_case_fails_before_claiming_coverage(self):
        with self.harness(enabled=False, selected=["does-not-exist"]) as result:
            self.assertEqual(result["code"], 2)
            self.assertEqual(result["report"]["cases"], [])
            result["hardware"].assert_not_called()
            self.assertFalse(result["report"]["complete_required_coverage"])
            self.assertTrue(any("unknown cases" in x for x in result["report"]["failures"]))

    def test_severe_endpoint_mismatch_cannot_become_a_green_warning(self):
        with self.harness(enabled=False, severe_endpoint=True) as result:
            self.assertEqual(result["code"], 2)
            self.assertEqual(result["persisted"]["status"], "failed")
            self.assertTrue(any("severe_endpoint_prediction_mismatch" in x
                                for x in result["persisted"]["failures"]))


if __name__ == "__main__":
    unittest.main()

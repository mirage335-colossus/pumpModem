"""Offline proposal checks against actual source-bound mock FFT anchors."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


proposal = load(ROOT/"tools/triad-performance.py", "fft_precision_proposal")
fixture = load(ROOT/"tests/test_triad_perf_fft_trace.py", "fft_precision_fixture")
from triad_perf_adaptive import AdaptivePolicy


class PrecisionWorker(fixture.Worker):
    def __init__(self, *, invalid_fp32=False):
        super().__init__()
        self.precision = None
        self.invalid_fp32 = invalid_fp32

    def request(self, request):
        if request["action"] == "prepare":
            self.precision = request["precision"]
            self.factor = .9974 if self.precision == "fp64" else 1.
        result = super().request(request)
        if request["action"] == "score" and self.precision == "fp32" and self.invalid_fp32:
            result["bit_log_likelihoods"][0] += .1
        return result


def workers(**kwargs):
    return {name: PrecisionWorker(**kwargs) for name in ("reference", "candidate")}


class PrecisionPreferenceTests(unittest.TestCase):
    def test_qualification_and_routine_forward_the_same_explicit_fixture_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            binary = directory/"mock-native"
            binary.write_bytes(b"mock binary")
            cache = directory/"shared-cache"

            def witness(command, **kwargs):
                path = Path(command[command.index("--output")+1])
                path.write_text(json.dumps({"complete_applicable_coverage": True}))
                return SimpleNamespace(returncode=0, stderr="")

            for mode in ("qualify", "reuse"):
                args = proposal.parse_args(["--output", str(directory/(mode+".json")), "--binary", str(binary),
                    "--candidate-root", str(ROOT), "--device", "cpu", "--_backend-worker", "cpu",
                    "--case", "trace-case", "--validation-mode", mode, "--fixture-cache", str(cache)])
                report = proposal.initial_report(args, "cpu")
                with (patch.object(proposal, "ROOT", ROOT),
                      patch("triad_perf_native.scenarios", return_value=[]),
                      patch("triad_perf_cases.scenarios", return_value=[fixture.case()]),
                      patch("triad_perf_hardware.collect_profile", return_value={"status": "complete", "hardware": {}}),
                      patch.object(proposal, "hardware_health", return_value={}),
                      patch.object(proposal.subprocess, "run", side_effect=witness),
                      patch("triad_perf_cases.prepare_cases", side_effect=proposal.SuiteError("intentional stop after cache dispatch")) as prepare):
                    proposal.run_backend(args, report)
                self.assertEqual(prepare.call_args.args[1], cache.resolve())
                self.assertEqual(report["fixture_cache_directory"], str(cache.resolve()))
                self.assertEqual(report["failures"], ["SuiteError: intentional stop after cache dispatch"])

    def test_exact_qualified_precision_wins_over_small_pilot_timing_flip(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture.run(anchor_directory=directory)
            result = proposal.qualify_fft(fixture.case(), workers(), "cpu", historical_anchor_directory=directory)
        self.assertEqual(result["fresh_pilot_fastest"], "fp64")
        self.assertEqual(result["selected"], "fp32")
        self.assertEqual(result["selection_method"], "eligible_exact_historical_precision")
        self.assertTrue(result["historical_precision_lookups"]["fp32"]["compatible"])
        self.assertFalse(result["historical_precision_lookups"]["fp64"]["compatible"])

    def test_absent_preference_keeps_qualification_selection(self):
        result = proposal.qualify_fft(fixture.case(), workers(), "cpu")
        self.assertEqual(result["selected"], "fp64")
        self.assertEqual(result["historical_precision_lookups"], {})
        self.assertEqual(result["selection_method"], "fastest_eligible_pilot")

    def test_no_matching_input_hash_never_reuses_old_arithmetic(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture.run(anchor_directory=directory)
            changed = copy.deepcopy(fixture.case())
            changed["fixture_hashes"]["observation"] = "different observation bytes"
            result = proposal.qualify_fft(changed, workers(), "cpu", historical_anchor_directory=directory)
        self.assertEqual(result["selected"], "fp64")
        self.assertFalse(any(row["compatible"] for row in result["historical_precision_lookups"].values()))

    def test_historical_precision_must_remain_numerically_eligible(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture.run(anchor_directory=directory)
            result = proposal.qualify_fft(fixture.case(), workers(invalid_fp32=True), "cpu",
                                          historical_anchor_directory=directory)
        self.assertFalse(result["alternatives"]["fp32"]["eligible"])
        self.assertNotIn("fp32", result["historical_precision_lookups"])
        self.assertEqual(result["selected"], "fp64")

    def test_multiple_matching_histories_have_a_fixed_eligible_order(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture.run(anchor_directory=directory)
            fixture.measure_fft(fixture.case(), {name: fixture.Worker() for name in ("reference", "candidate")},
                "fp64", "cpu", policy=AdaptivePolicy(), deadline=None, api=fixture.api,
                anchor_directory=directory)
            result = proposal.qualify_fft(fixture.case(), workers(), "cpu", historical_anchor_directory=directory)
        self.assertTrue(all(row["compatible"] for row in result["historical_precision_lookups"].values()))
        self.assertEqual(result["selected"], "fp32")


if __name__ == "__main__":
    unittest.main()

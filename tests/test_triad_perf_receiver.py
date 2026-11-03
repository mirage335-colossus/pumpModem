#!/usr/bin/env python3
"""Cheap protocol and real-helper checks for the persistent perf adapter."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_PATH = ROOT / "tests" / "test_triad_fft_acquire.py"
spec = importlib.util.spec_from_file_location("fft_test_fixture", FIXTURE_PATH)
fixture_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture_module)


class ReceiverAdapterTests(unittest.TestCase):
    def run_worker(self, lines):
        command = [sys.executable, str(ROOT / "tools" / "triad_perf_receiver.py"),
                   "--serve", "--source-root", str(ROOT), "--threads", "1"]
        completed = subprocess.run(command, input="\n".join(map(json.dumps, lines)) + "\n",
                                   text=True, capture_output=True, timeout=20, cwd=ROOT)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        return [json.loads(line) for line in completed.stdout.splitlines()]

    def test_jsonl_actual_fft_prepare_and_cfo_only_score(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary) / "sampled"
            fixture_module.fixture(data, seed=818, erased=True)
            responses = self.run_worker([
                {"action": "hello"},
                {"action": "prepare", "input_directory": str(data), "lag_max_seconds": .1,
                 "lag_step_samples": 1, "precision": "fp32", "device": "cpu", "memory_mib": 64,
                 "group_tile": 4},
                {"action": "score", "frequency_offsets_hz": [-.07, 0., .07],
                 "full_group_count_for_reporting": 42, "group_tile": 4},
                {"action": "close"},
            ])
        self.assertEqual(len(responses), 4)
        self.assertTrue(responses[0]["ok"])
        hashes = responses[0]["result"]["source_hashes"]
        self.assertEqual(set(hashes), {name + ".py" for name in (
            "triad_fft_acquire", "triad_group_acquire", "triad_gaussian_window",
            "triad_noise_likelihood", "triad_gaussian_state", "triad_state_workspace")})
        self.assertTrue(responses[1]["ok"], responses[1])
        geom = responses[1]["result"]["geometry"]
        self.assertEqual(geom["nfft"] >= 255, True)
        self.assertEqual(geom["lags"], 14)
        scored = responses[2]["result"]
        self.assertTrue(responses[2]["ok"], responses[2])
        self.assertTrue(scored["performance_only"])
        self.assertEqual(scored["measured_group_count"], 42)
        self.assertEqual(len(scored["bit_log_likelihoods"]), 2)
        self.assertIn("invalid", scored["admission_interpretation"])
        self.assertTrue(responses[3]["ok"])

    def test_protocol_errors_are_json_and_no_implicit_search_modes(self):
        responses = self.run_worker([
            {"action": "unknown"},
            {"action": "score", "frequency_offsets_hz": [0.], "full_group_count_for_reporting": 1,
             "drift_offsets_hz_per_second": [0., .1]},
            {"action": "close"},
        ])
        self.assertEqual(len(responses), 3)
        self.assertFalse(responses[0]["ok"])
        self.assertFalse(responses[1]["ok"])
        self.assertIn("prepare", responses[1]["error"])

    def test_algebraic_gaussian_fixture_precision(self):
        responses = self.run_worker([
            {"action": "gaussian", "blocks": 8, "candidates": 12, "taps": 2,
             "window_blocks": 4, "precision": "fp32", "seed": 991},
            {"action": "gaussian", "blocks": 8, "candidates": 12, "taps": 2,
             "window_blocks": 4, "precision": "fp64", "seed": 991},
            {"action": "close"},
        ])
        self.assertTrue(responses[0]["ok"], responses[0])
        self.assertTrue(responses[1]["ok"], responses[1])
        a, b = (r["result"] for r in responses[:2])
        self.assertEqual(a["fixture_kind"], "deterministic algebraic PSD/shared-complex observation; not sampled propagation")
        self.assertEqual(len(a["score_summary"]["scores"]), 12)
        np.testing.assert_allclose(a["score_summary"]["scores"], b["score_summary"]["scores"], rtol=2e-4, atol=2e-5)

    def test_gaussian_qualification_extremes_and_combined_memory(self):
        requests = []
        for condition in ("strong", "near-singular"):
            for precision in ("fp32", "fp64"):
                requests.append({"action": "gaussian", "blocks": 8, "candidates": 12,
                    "taps": 2, "window_blocks": 4, "precision": precision,
                    "condition": condition, "seed": 991})
        responses = self.run_worker(requests)
        for response in responses:
            self.assertTrue(response["ok"], response)
            result = response["result"]
            self.assertGreater(result["resources"]["resident_input_bytes"], 0)
            self.assertLess(result["diagnostics"]["preparation_array_bound_bytes"] +
                result["resources"]["resident_input_bytes"], result["resources"]["total_memory_limit_bytes"])
        for offset in (0, 2):
            a, b = (r["result"]["score_summary"]["scores"] for r in responses[offset:offset+2])
            np.testing.assert_allclose(a, b, rtol=2e-4, atol=2e-5)
        failed = self.run_worker([{"action": "gaussian", "condition": "ignored-option"}])[0]
        self.assertFalse(failed["ok"])


if __name__ == "__main__":
    unittest.main()

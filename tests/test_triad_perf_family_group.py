#!/usr/bin/env python3
"""Bounded adapter checks on native actual-noisy sampled group statistics."""
import copy
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from triad_perf_family_group import run_case
from triad_perf_families import load_source, validate_measurement


class GroupPerformanceAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = Path(os.environ.get("TRIAD_LONG", ROOT / "build/dev-cli/triad_long"))
        if not binary.is_file():
            raise unittest.SkipTest("native triad_long is unavailable; no algebraic fixture substitute")
        torch.set_num_threads(1)
        cls.temp = tempfile.TemporaryDirectory(prefix="triad-perf-group-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.fixture = Path(cls.temp.name) / "sampled"
        command = [str(binary), "--mode", "export-group-stats", "--seconds", "8",
                   "--rate", "36", "--added", "1", "--sample-rate", "256",
                   "--channel", "none", "--phase-diffusion", "0", "--basis", "two-ray",
                   "--tap-delay-ms", ".005", "--tap-doppler-hz", ".001",
                   "--block-seconds", ".25", "--start", "0", ".02", ".01",
                   "--frequency", "0", "0", "1", "--arrival-prior-seconds", "0", "0",
                   "--stats-noisy", "--threads", "1", "--memory-mib", "64",
                   "--export-max-samples", "100000", "--export-max-mib", "16",
                   "--seed", "7201", "--output", str(cls.fixture)]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
        # Receiver access must not depend on exported simulator-only means.
        for filename in ("group_mean.c128", "signal_energy.f64"):
            (cls.fixture / filename).unlink()
        cls.case = {"id": "native-group-small", "family": "group", "unit": "cfo",
                    "training": [6, 8], "options": {
                        "doppler_width_hz": .1, "window_blocks": 4, "reblock": 2,
                        "group_tile": 2, "projection_tile_blocks": 7,
                        "memory_limit_bytes": 64 * 1024 * 1024,
                        "frequency_offsets_hz": [sign * (i + 1) * .005
                                                  for i in range(12) for sign in (-1, 1)]}}

    def test_complete_native_family_and_training_prefixes_keep_bulk_regime(self):
        outputs = [run_case(ROOT, self.fixture, self.case, "cpu", "fp64", limit)
                   for limit in (6, 8, None)]
        short, medium, full = outputs
        self.assertEqual(short["regime"], medium["regime"])
        self.assertEqual(short["regime"], full["regime"])
        self.assertEqual([value["work"]["count"] for value in outputs], [6, 8, 24])
        regime = full["regime"]
        self.assertEqual(regime["base_groups"], 3)
        self.assertGreater(regime["source_tail_samples"], 0)
        self.assertEqual(regime["tail_group_tile"], 1)
        self.assertGreater(regime["tail_window_blocks"], 0)
        self.assertGreater(regime["tail_projection_tile_blocks"], 0)
        self.assertEqual(regime["frequency_branch"], "nonzero")
        for output in outputs:
            work = output["work"]
            self.assertTrue(work["delta_exactly_equal"])
            self.assertEqual(work["delta_per_group"], full["work"]["delta_per_group"])
            self.assertEqual(work["sample_count"], full["work"]["sample_count"])
            self.assertEqual(work["source_blocks"], full["work"]["source_blocks"])
            self.assertEqual(work["effective_groups"], work["count"] * 3)
            self.assertEqual(work["resources"]["explicit_array_bound_bytes"] +
                             work["omitted_result_budget_bytes"],
                             full["work"]["resources"]["explicit_array_bound_bytes"])
            self.assertEqual(set(output["seconds"]), {"load", "prepare", "transfer_and_score", "finish"})
            self.assertTrue(all(math.isfinite(t) and t >= 0 for t in output["seconds"].values()))
            self.assertIn("combined native score_workspace", output["regime"]["timing_boundaries"]["transfer_and_score"])
            validate_measurement(self.case, output, work["count"])
            json.dumps(output, allow_nan=False)
        for output in (short, medium):
            self.assertFalse(output["result"]["complete"])
            self.assertIsNone(output["result"]["decisions"])
            self.assertIsNone(output["result"]["bit_log_likelihoods"])
            count = output["work"]["effective_groups"]
            np.testing.assert_allclose(output["result"]["scores"], full["result"]["scores"][:count],
                                       rtol=1e-13, atol=1e-13)
        receiver = load_source(ROOT, "triad_group_acquire.py")
        options = dict(self.case["options"])
        options["frequency_offsets_hz"] = tuple(options["frequency_offsets_hz"])
        expected = receiver.score_workspace(receiver.GroupedWorkspace(self.fixture),
                                             receiver.AcquisitionConfig(**options))
        self.assertTrue(full["result"]["complete"])
        self.assertEqual(full["result"]["decisions"],
                         {key: expected[key] for key in ("strict", "relaxed")})
        np.testing.assert_array_equal(full["result"]["scores"], expected["group_bit_log_likelihoods"])
        np.testing.assert_array_equal(full["result"]["bit_log_likelihoods"], expected["bit_log_likelihoods"])

    def test_zero_cfo_has_separate_complete_regime_and_fp32_is_explicit(self):
        case = copy.deepcopy(self.case)
        case["options"]["frequency_offsets_hz"] = [0.]
        case["training"] = None
        result = run_case(ROOT, (self.fixture,), case, "cpu", "fp32")
        self.assertEqual(result["regime"]["frequency_branch"], "zero")
        self.assertEqual(result["work"]["count"], 1)
        self.assertTrue(result["result"]["complete"])
        arithmetic = result["result"]["precision_diagnostics"]["arithmetic"]
        self.assertEqual(arithmetic["bulk"], "fp32")
        self.assertIn("complex128/float64", arithmetic["preparation"])
        self.assertFalse(arithmetic["tf32_allowed"])
        self.assertFalse(result["result"]["precision_diagnostics"]["fallback"])

    def test_invalid_limits_and_mixed_branches_fail_before_scoring(self):
        for limit in (True, 0, -1, 25, 1.5, 5):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                run_case(ROOT, self.fixture, self.case, "cpu", "fp64", limit)
        case = copy.deepcopy(self.case)
        case["options"]["frequency_offsets_hz"] = [0., .01]
        with self.assertRaisesRegex(ValueError, "separate performance cases"):
            run_case(ROOT, self.fixture, case, "cpu", "fp64")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            run_case(ROOT, (self.fixture, self.fixture), self.case, "cpu", "fp64")
        case["options"]["frequency_offsets_hz"] = [(i + 1) * 1e-6 for i in range(4096)]
        with self.assertRaisesRegex(ValueError, "at least 1 MiB"):
            run_case(ROOT, self.fixture, case, "cpu", "fp64", 1)


if __name__ == "__main__":
    unittest.main()

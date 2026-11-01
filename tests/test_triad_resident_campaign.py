#!/usr/bin/env python3
"""Persistent campaign reset, exact preparation and seed-domain checks."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from test_triad_optimization import write_export

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT/"tools"/"triad-resident-campaign.py"
sys.path.insert(0, str(ROOT/"tools"))
SPEC = importlib.util.spec_from_file_location("resident_campaign", SCRIPT)
campaign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(campaign)


class ResidentTests(unittest.TestCase):
    def test_seed_domains(self):
        seeds = {campaign.noise_seed(17, split, index) for split in campaign.SPLITS
                 for index in range(campaign.MAX_NOISE_BATCHES)}
        self.assertEqual(len(seeds), 96)

    def test_32_noise_batches_and_rejection_above_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_export(root/"export")
            command = [sys.executable, str(SCRIPT), str(root/"export"), "--output", str(root/"maximum.json"),
                "--device", "cpu", "--phase-states", "4", "--relative-phase-states", "1",
                "--batch", "1", "--batches", "32", "--cn0", "18"]
            accepted = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(accepted.returncode, 0, accepted.stdout+accepted.stderr)
            report = json.loads((root/"maximum.json").read_text())
            self.assertEqual(len(report["batches"]), 32)
            self.assertEqual(report["workload"]["paired_observations_scored"], 64)
            self.assertEqual(len({batch["seed"] for batch in report["batches"]}), 32)
            command[-3] = "33"
            command[command.index(str(root/"maximum.json"))] = str(root/"rejected.json")
            rejected = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("one through 32", rejected.stderr)
            self.assertFalse((root/"rejected.json").exists())

    def test_explicit_fp64_reduction_backend_guard_and_cpu_rejection(self):
        parser = campaign.parser()
        defaults = parser.parse_args(["input", "--output", "out.json"])
        self.assertEqual(defaults.backend, "reference")
        args = parser.parse_args(["input", "--output", "out.json", "--backend", "graph-fp64",
            "--phase-states", "4", "--relative-phase-states", "1", "--batch", "2"])
        args.stop, args.nominal_dt = 3, 4.
        workspace = SimpleNamespace(dimension=4, max_tile_blocks=64)
        dts = np.asarray([0., 4., 4.])
        new = campaign.memory_preflight(workspace, args, 2, dts, campaign.torch.device("cpu"))
        args.backend = "graph"
        original = campaign.memory_preflight(workspace, args, 2, dts, campaign.torch.device("cpu"))
        self.assertEqual(new["receiver_working_bound_bytes"], original["receiver_working_bound_bytes"])
        args.backend = "graph-fp64"
        with self.assertRaisesRegex(RuntimeError, "unsupported on CPU"):
            campaign.make_receiver(args, 2, campaign.torch.device("cpu"), dts)

    def test_tensor_is_explicit_and_rejects_fp64(self):
        args = campaign.parser().parse_args(["input", "--output", "out.json",
            "--backend", "tensor", "--tensor-mode", "bf16", "--precision", "fp64"])
        with self.assertRaisesRegex(ValueError, "explicit FP32 bulk"):
            campaign.run(args)
        args.backend, args.precision = "reference", "fp32"
        with self.assertRaisesRegex(ValueError, "tensor options require"):
            campaign.run(args)
        for script in ("triad-state-study.py", "triad-path-study.py"):
            result = subprocess.run([sys.executable, str(ROOT/"tools"/script), "input",
                "--output", "out.json", "--phase-implementation", "tensor"],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("explicit FP32 bulk", result.stderr)

    def test_preallocation_rejects_large_batch(self):
        workspace = SimpleNamespace(dimension=4, max_tile_blocks=64)
        args = SimpleNamespace(stop=929, batch=10**9, batches=8, taps=2,
            precision="fp32", observation_preparation="cached", workspace_memory_mib=512,
            phase_states=512, relative_phase_states=8, nominal_dt=4., frequency_bound=.02,
            backend="graph", memory_gib=12.)
        with self.assertRaisesRegex(MemoryError, "before allocation"):
            campaign.memory_preflight(workspace, args, 2, np.asarray([0., 4.]), campaign.torch.device("cpu"))

    def test_persistent_reference_cached_original_and_optimized_pair(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_export(root/"export")
            reports = []
            for name, backend, preparation, lifetime in (("baseline", "reference", "reference", "campaign"),
                    ("cached", "reference", "cached", "campaign"),
                    ("optimized", "optimized", "cached", "campaign"),
                    ("capture", "reference", "cached", "capture")):
                command = [sys.executable, str(SCRIPT), str(root/"export"), "--output", str(root/f"{name}.json"),
                    "--backend", backend, "--observation-preparation", preparation,
                    "--receiver-lifetime", lifetime, "--device", "cpu", "--precision", "fp32",
                    "--phase-states", "4", "--relative-phase-states", "1", "--batch", "2", "--batches", "3", "--cn0", "18"]
                if name != "baseline":
                    command.extend(["--compare-report", str(root/"baseline.json")])
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                report = json.loads((root/f"{name}.json").read_text())
                reports.append(report)
                self.assertEqual(report["workload"]["paired_observations_scored"], 12)
                self.assertEqual(report["workload"]["conditional_independent_noise_trials_per_kind"], 6)
                self.assertGreater(report["timing"]["complete_processing_pipeline_seconds"], report["timing"]["inference_seconds"])
                self.assertEqual(len(set(b["seed"] for b in report["batches"])), 3)
                if name != "baseline":
                    comparison = report["comparison_to_original_reference"]
                    self.assertEqual(comparison["checked_batch_indices"], [0, 1, 2])
                    self.assertEqual(comparison["boundary_batch_indices"], [0, 2])
                    self.assertEqual(comparison["maximum_absolute_log_lr_error"], 0.)
                    self.assertEqual(comparison["candidate_ranking_differences"], 0)
            with np.load(root/"baseline.scores.npz") as a, np.load(root/"capture.scores.npz") as b:
                np.testing.assert_array_equal(a["scores"], b["scores"])
                synthetic = reports[0].copy()
                synthetic["configuration"] = dict(synthetic["configuration"], backend="graph-fp64")
                (root/"synthetic.json").write_text(json.dumps(synthetic))
                np.savez(root/"synthetic.scores.npz", scores=a["scores"])
                comparison = campaign.compare_report(root/"synthetic.json", synthetic["input_identity"],
                    SimpleNamespace(**synthetic["configuration"]), a["scores"],
                    synthetic["represented_observation_sha256_by_batch"], np.asarray([0, 1]))
                self.assertEqual(comparison["reference_backend"], "graph-fp64")
                self.assertEqual(comparison["maximum_bit_likelihood_margin_error"], 0.)


if __name__ == "__main__":
    unittest.main(verbosity=2)

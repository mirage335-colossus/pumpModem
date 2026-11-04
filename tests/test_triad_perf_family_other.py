"""Bounded functional adapters checks; no throughput or sensitivity claims.

Set TRIAD_PERF_NATIVE_BINARY to a compatible triad_long for the actual sampled
export check. The other fixtures are algebraic regression inputs only.
"""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import triad_perf_family_other as other
from triad_perf_families import load_source

spec = importlib.util.spec_from_file_location("other_stat_fixture", ROOT/"tests/test_triad_noise_study.py")
fixture_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture_module)


def case(family, **extra):
    options = {"reference_variance": 1., "threads": 2}
    if family in ("rbpf", "cfo-batch"):
        options.update(particles=8, frequency_bound=.3, coherence=13.)
    if family == "rbpf":
        options.update(resampling="systematic", ess_fraction=1., phase_diffusion=.2, seed=1701)
    if family in ("static-frequency", "ray-frequency"):
        options.update(cn0=0., frequency_bound=.3, frequency_step=.1)
    if family == "ray-frequency":
        options.update(ray_frequency_bound=.14, frequency_tile=4, ray_frequency_tile=3,
                       projection_tile_blocks=3)
    if family == "cfo-batch":
        options.update(batch_size=2, projection_tile_blocks=3)
    options.update(extra)
    return {"family": family, "options": options}


def workspace(parent, index=0):
    path = parent / f"path{index}"
    path.mkdir()
    fixture_module.write_workspace(path, seed=1701+index, observation=True,
                                   block_count=7, mixed_taps=True)
    manifest = json.loads((path/"manifest.json").read_text())
    manifest.update(channel_seed=1701+index, key_seed=2701+index, epoch=1793404800+index)
    (path/"manifest.json").write_text(json.dumps(manifest))
    return path


class OtherFamilyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_partial_measurements_rejected_before_source_or_fixture_access(self):
        for family in other.SOURCES:
            for limit in (0, 1, True, -1):
                with self.assertRaisesRegex(ValueError, "full direct"):
                    other.run_case(Path("absent"), Path("absent"), case(family), "cpu", "fp64", limit)

    def test_complete_adapters_match_maintained_study_endpoints(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            for family, filename in other.SOURCES.items():
                with self.subTest(family=family):
                    declaration = case(family)
                    actual = other.run_case(ROOT, path, declaration, "cpu", "fp64")
                    module = load_source(ROOT, filename)
                    args = other._arguments(module, family, (path,), declaration["options"], "cpu", "fp64")
                    args.output = Path(temp)/(family+".json")
                    expected = module.run(args)
                    self.assertEqual(expected["failed_physical_capture_records"], 0)
                    scores = [m["bit_log_scores"] for c in expected["captures"] for m in c["methods"].values()]
                    np.testing.assert_allclose(actual["result"]["scores"], scores, rtol=2e-12, atol=2e-12)
                    self.assertTrue(actual["work"]["complete"])
                    self.assertEqual(actual["work"]["count"], actual["work"]["target_count"])
                    self.assertEqual(actual["work"]["blocks"], 7)
                    self.assertEqual(set(actual["seconds"]), {"load", "prepare", "transfer_and_score", "finish", "unattributed"})
                    self.assertTrue(all(v >= 0 for v in actual["seconds"].values()))
                    self.assertAlmostEqual(sum(actual["seconds"].values()),
                        actual["result"]["timing_accounting"]["adapter_wall_seconds"], places=12)
                    json.dumps(actual, allow_nan=False)

    def test_controller_threads_are_observed_and_conflicting_options_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            with self.assertRaisesRegex(ValueError, "contradict.*controller"):
                other.run_case(ROOT, path, case("rbpf", threads=1), "cpu", "fp64")
            self.assertEqual(torch.get_num_threads(), 2)
            declaration = case("rbpf")
            del declaration["options"]["threads"]
            torch.set_num_threads(1)
            try:
                result = other.run_case(ROOT, path, declaration, "cpu", "fp64")
                self.assertEqual(torch.get_num_threads(), 1)
                self.assertEqual(result["regime"]["settings"]["threads"], 1)
                threads = result["regime"]["threads"]
                self.assertEqual(threads["torch_intraop"], 1)
                self.assertEqual(threads["torch_interop"], torch.get_num_interop_threads())
                self.assertEqual(threads["environment"]["OMP_NUM_THREADS"], os.environ.get("OMP_NUM_THREADS"))
                self.assertIn("at::get_num_threads() : 1", threads["torch_parallel_info"])
                self.assertTrue(threads["library_pool_inspection"])
            finally:
                torch.set_num_threads(2)

    def test_incomplete_execution_receipts_cannot_claim_complete_work(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            for family, filename in other.SOURCES.items():
                module = load_source(ROOT, filename)
                name = {"rbpf": "infer", "static-frequency": "frequency_correlations",
                        "ray-frequency": "joint_evidence", "cfo-batch": "score_batch"}[family]
                original = getattr(module, name)
                def truncated(*args, **kwargs):
                    result = original(*args, **kwargs)
                    if family == "rbpf":
                        for row in result[0].values():
                            row["diagnostics"]["blocks"] -= 1
                    elif family == "static-frequency":
                        return result[:-1]
                    elif family == "ray-frequency":
                        result[1]["evaluated_frequency_pairs_per_candidate"] -= 1
                    else:
                        result[1]["blocks"] -= 1
                    return result
                with self.subTest(family=family), patch.object(module, name, side_effect=truncated):
                    with self.assertRaisesRegex(ValueError, "executed work.*full public target"):
                        other.run_case(ROOT, path, case(family), "cpu", "fp64")

    def test_rbpf_reports_actual_resampling_and_both_full_bank_passes(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            result = other.run_case(ROOT, path, case("rbpf"), "cpu", "fp32")
            geometry = result["work"]["geometries"][0]
            diagnostics = result["result"]["precision_diagnostics"][0]
            self.assertGreater(diagnostics["resampling_events_total"], 0)
            self.assertEqual(result["work"]["resampling_events"], diagnostics["resampling_events_total"])
            self.assertEqual(geometry["full_bank_projection_blocks"], 14)
            self.assertEqual(geometry["statistics_read_bytes"], 2*geometry["statistics_bytes"])
            self.assertEqual(result["work"]["count"], 7*2*8)
            self.assertEqual(diagnostics["bulk_dtype"], "torch.complex64")
            self.assertEqual(diagnostics["covariance_dtype"], "torch.complex128")

    def test_static_preserves_full_fft_grid_and_direct_comparison(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            fft = other.run_case(ROOT, path, case("static-frequency"), "cpu", "fp64")
            direct = other.run_case(ROOT, path, case("static-frequency", implementation="direct"), "cpu", "fp64")
            np.testing.assert_allclose(fft["result"]["scores"], direct["result"]["scores"], atol=2e-12)
            f, d = [r["work"]["geometries"][0] for r in (fft, direct)]
            self.assertEqual((f["nfft"], f["frequency_bins"]), (d["nfft"], d["frequency_bins"]))
            self.assertEqual(f["fft_elements"], f["nfft"]*f["candidate_count"]*f["taps"])
            self.assertEqual(d["fft_elements"], 0)

    def test_ray_tails_and_cfo_partial_final_batch(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = tuple(workspace(Path(temp), i) for i in range(3))
            ray = other.run_case(ROOT, paths[0], case("ray-frequency"), "cpu", "fp32")
            g = ray["work"]["geometries"][0]
            self.assertEqual((g["common_bins"], g["relative_bins"]), (11, 7))
            self.assertEqual(g["joint_tiles"], 9)
            self.assertEqual(g["frequency_candidate_scores"], 11*7*2)
            batch = other.run_case(ROOT, paths, case("cfo-batch"), "cpu", "fp64")
            self.assertEqual([b["batch_count"] for b in batch["work"]["batches"]], [2, 1])
            singles = [other.run_case(ROOT, p, case("cfo-batch"), "cpu", "fp64")["result"]["scores"][0] for p in paths]
            np.testing.assert_allclose(batch["result"]["scores"], singles, atol=2e-12)
            self.assertEqual(batch["work"]["count"], 3*7*2*8)
            self.assertEqual(batch["work"]["resampling_events"], 0)

    def test_actual_only_reads_and_bad_inputs_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = workspace(Path(temp))
            for name, dtype in (("mean.c128", "<c16"), ("signal_energy.f64", "<f8")):
                np.full((path/name).stat().st_size//np.dtype(dtype).itemsize, np.nan, dtype=dtype).tofile(path/name)
            original = np.fromfile
            def guarded(file, *args, **kwargs):
                self.assertNotIn(Path(file.name).name, ("mean.c128", "signal_energy.f64"))
                return original(file, *args, **kwargs)
            with patch.object(np, "fromfile", side_effect=guarded):
                for family in other.SOURCES:
                    result = other.run_case(ROOT, path, case(family), "cpu", "fp64")
                    self.assertTrue(np.isfinite(result["result"]["scores"]).all())
            with self.assertRaisesRegex(ValueError, "distinct physical"):
                other.run_case(ROOT, (path, path), case("cfo-batch"), "cpu", "fp64")
            manifest = json.loads((path/"manifest.json").read_text())
            manifest["gaussian_projection_supported"] = False
            (path/"manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "Gaussian-supported"):
                other.run_case(ROOT, path, case("rbpf"), "cpu", "fp64")

    @unittest.skipUnless(os.environ.get("TRIAD_PERF_NATIVE_BINARY"), "set TRIAD_PERF_NATIVE_BINARY for sampled native export")
    def test_actual_sampled_native_fixture_all_families(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"sampled-stats"
            command = [os.environ["TRIAD_PERF_NATIVE_BINARY"], "--mode", "export-stats", "--stats-noisy",
                "--seconds", "8", "--rate", "36", "--added", "1", "--sample-rate", "64", "--channel", "none",
                "--basis", "two-ray", "--phase-diffusion", "0", "--block", "32", "--chunk", "256", "--cn0", "10",
                "--memory-mib", "64", "--threads", "2", "--epoch", "1793404800", "--seed", "1701", "--output", str(path)]
            subprocess.run(command, check=True, timeout=15, capture_output=True, text=True)
            manifest = json.loads((path/"manifest.json").read_text())
            self.assertTrue(manifest["stats_noisy"])
            self.assertGreater(manifest["block_count"], 16)
            for family in other.SOURCES:
                full = other.run_case(ROOT, path, case(family), "cpu", "fp64")
                lower = other.run_case(ROOT, path, case(family), "cpu", "fp32")
                self.assertEqual(full["work"]["blocks"], manifest["block_count"])
                self.assertTrue(full["work"]["complete"])
                np.testing.assert_allclose(lower["result"]["scores"], full["result"]["scores"], atol=1e-4, rtol=1e-4)


if __name__ == "__main__":
    unittest.main()

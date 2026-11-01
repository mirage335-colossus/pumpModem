#!/usr/bin/env python3
"""Focused CPU smoke checks for the isolated triad optimization runner.

Run with: /tmp/triad-weak-env/bin/python tests/test_triad_optimization.py
"""
from __future__ import annotations

import importlib.util
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "triad-optimization-study.py"
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("triad_optimization_study", RUNNER)
study = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(study)


def write_export(directory: Path, *, gaussian_projection_supported=True, observation=None):
    directory.mkdir()
    gram = np.broadcast_to(np.eye(2, dtype="<c16"), (3, 2, 2)).copy()
    mean = np.broadcast_to(np.array([[.1+.2j, .2-.1j]], dtype="<c16"), (3, 2)).copy()
    intervals = np.array([[0, 8], [8, 16], [16, 24]], dtype="<u8")
    valid = np.array([8, 8, 8], dtype="<u8")
    energy = np.ones(3, dtype="<f8")
    arrays = {"gram.c128": gram, "mean.c128": mean, "intervals.u64": intervals,
              "valid_rows.u64": valid, "signal_energy.f64": energy}
    sizes = {}
    for name, value in arrays.items():
        value.tofile(directory / name)
        sizes[name] = (directory / name).stat().st_size
    candidates = []
    for bit in (0, 1):
        candidates.append({"bit": bit, "tap_count": 2, "arrival_seconds": 0.,
                           "frequency_hz": 0., "clock_ppm": 0.,
                           "frequency_drift_hz_per_second": 0.,
                           "taps": [{"delay_seconds": float(tap), "doppler_hz": 0.} for tap in range(2)]})
    descriptors = []
    for bit in (0, 1):
        descriptors.append({"candidate_index": bit, "source_bit": bit,
            "arrival_seconds": 0., "frequency_hz": 0., "clock_ppm": 0.,
            "frequency_drift_hz_per_second": 0., "tap_delay_seconds": 0., "tap_doppler_hz": 0.})
    manifest = {"format": "triad-block-stats-v1", "byte_order": "little",
        "complex_type": "complex128", "real_type": "float64", "integer_type": "uint64",
        "layout": "block,row,column", "block_count": 3, "dimension": 2,
        "sample_rate": 64., "noise_variance": 1., "sample_count": 24, "block_samples": 8,
        "gram_file": "gram.c128", "gram_shape": [3, 2, 2],
        "mean_file": "mean.c128", "mean_shape": [3, 2],
        "intervals_file": "intervals.u64", "intervals_shape": [3, 2],
        "valid_rows_file": "valid_rows.u64", "valid_rows_shape": [3],
        "signal_energy_file": "signal_energy.f64", "signal_energy_shape": [3],
        "column_mapping": [[0, 1], [0, 1]], "candidate_count": 2, "candidates": candidates,
        "column_descriptors": descriptors, "file_sizes": sizes, "signal_bit": 0,
        "gaussian_projection_supported": gaussian_projection_supported}
    if observation is not None:
        sample = np.asarray(observation, dtype="<c16")
        sample.tofile(directory / "observation.c128")
        sizes["observation.c128"] = (directory / "observation.c128").stat().st_size
        manifest.update({"stats_noisy": True, "observation_file": "observation.c128",
                         "observation_shape": [3, 2]})
    (directory / "manifest.json").write_text(json.dumps(manifest))


class OptimizationStudyTests(unittest.TestCase):
    def test_threshold_and_exact_interval_helpers(self):
        labels = np.array([0, 0, 1, 1])
        scores = np.array([[0., 0., 0., 0.]])
        family = {"duration_alternatives": 3, "selection_methods": 2}
        _, results = study._verify(scores, labels, 0, family)
        expected_default = math.log(1440*3*2*2/1e-4)
        expected_relaxed = math.log(720*3*2*2/.01)
        self.assertAlmostEqual(results["operational_default"]["threshold_nats"], expected_default)
        self.assertAlmostEqual(results["relaxed"]["threshold_nats"], expected_relaxed)
        self.assertEqual(study.interval(0, 10)[0], 0.)
        self.assertEqual(study.interval(10, 10)[1], 1.)

    def test_explicit_tensor_contract_and_uncalibrated_labels(self):
        receiver = SimpleNamespace(diagnostics=lambda: dict(tf32_allowed=True,
            implementation="experimental_graph_tensor_frequency_v1"))
        with self.assertRaisesRegex(RuntimeError, "FP32 minimum"):
            study._verified_method(receiver)
        self.assertTrue(study._verified_method(receiver, experimental_tensor=True)["tf32_allowed"])
        wrong = SimpleNamespace(diagnostics=lambda: dict(tf32_allowed=False, implementation="reference"))
        with self.assertRaisesRegex(RuntimeError, "did not execute"):
            study._verified_method(wrong, experimental_tensor=True)
        _, rows = study._verify(np.array([[30., 0.]]), np.array([0, 1]), 0,
            {"duration_alternatives":1, "selection_methods":1}, calibration_supported=False)
        for row in rows.values():
            self.assertFalse(row["admission_calibration_supported"])
            self.assertIsNone(row["conditional_correct_accepted"])
            self.assertEqual(row["hypothetical_score_gate_crossings"], 1)
            self.assertIn("tensor arithmetic", row["scope"])
        with tempfile.TemporaryDirectory() as temporary:
            args = study.parse_args(["input", "--output", str(Path(temporary)/"out.json"),
                                     "--backend", "tensor", "--precision", "fp64"])
            with self.assertRaisesRegex(ValueError, "explicit FP32 bulk"):
                study.run(args)

    def test_paired_full_runner_records_stages_provenance_and_decisions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export = root / "export"
            write_export(export)
            output = root / "report.json"
            command = [sys.executable, str(RUNNER), str(export), "--output", str(output),
                       "--device", "cpu", "--precision", "fp32", "--batch", "2",
                       "--phase-states", "4", "--relative-phase-states", "1",
                       "--max-blocks", "2", "--cn0", "18", "--warmup-blocks", "1"]
            result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(report["schema"], "triad-gpu-optimization-report-v1")
            self.assertEqual(report["status"], "screening_only")
            self.assertTrue(report["workload"]["truncated"])
            self.assertEqual(report["workload"]["scored_blocks"], 2)
            self.assertEqual(report["workload"]["candidate_count_scored"], 2)
            self.assertEqual(report["results"]["signal_log_lr_shape"], [2, 2])
            self.assertLess(report["results"]["paired_sufficient_mean_difference_max_abs"], 1e-12)
            self.assertLess(report["results"]["streamed_vs_resident_max_abs_log_lr"]["signal_max_abs_log_lr"], 1e-5)
            timing = report["timing_seconds"]
            for stage in ("workspace_setup_schema_and_file_validation", "public_gram_interval_psd_validation",
                          "signal_statistics_noise_rng_generation", "signal_host_to_device",
                          "signal_pretransferred_resident_inference", "signal_streamed_host_transfer_plus_inference",
                          "verification_d2h_and_decision_metrics", "complete_job_wall_seconds"):
                self.assertIn(stage, timing)
            self.assertGreaterEqual(timing["complete_job_wall_seconds"],
                                    timing["signal_pretransferred_resident_inference"]["synchronized_wall_seconds"])
            self.assertIn("input_files", report["provenance"])
            self.assertIn("source_sha256", report["provenance"])
            self.assertIn("triad-long.cpp", " ".join(report["provenance"]["source_sha256"].keys()))
            self.assertFalse(report["provenance"]["triad_long_cpp_is_original_export_binding"])
            self.assertEqual(report["configuration"]["batch"], 2)
            self.assertIn("null_admission_one_sided_95", report["results"]["null"]["operational_default"])
            self.assertIn("resident_candidate_block_updates_per_second", timing["candidate_scoring_work"])

            reference_report = root / "reference.json"
            reference_cmd = [sys.executable, str(RUNNER), str(export), "--output", str(reference_report),
                             "--output-scores", "--device", "cpu", "--precision", "fp64", "--batch", "2",
                             "--phase-states", "4", "--relative-phase-states", "1",
                             "--max-blocks", "2", "--cn0", "18", "--warmup-blocks", "1"]
            reference_result = subprocess.run(reference_cmd, capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(reference_result.returncode, 0, reference_result.stdout+reference_result.stderr)
            paired_report = root / "paired.json"
            paired_cmd = [sys.executable, str(RUNNER), str(export), "--output", str(paired_report),
                          "--device", "cpu", "--precision", "fp32", "--batch", "2",
                          "--phase-states", "4", "--relative-phase-states", "1",
                          "--max-blocks", "2", "--cn0", "18", "--warmup-blocks", "1",
                          "--compare-report", str(reference_report)]
            paired_result = subprocess.run(paired_cmd, capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(paired_result.returncode, 0, paired_result.stdout+paired_result.stderr)
            paired = json.loads(paired_report.read_text())["results"]["paired_numerical_reference"]
            self.assertTrue(paired["matched_inputs_and_reception_model"])
            self.assertEqual(paired["results"]["signal"]["candidate_ranking_disagreements"], 0)

    def test_active_sampled_frontend_only_emits_uncalibrated_signal_diagnostics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            export = root / "active-frontend"
            observation = np.ones((3, 2), dtype=np.complex128)*(.1+.2j)
            write_export(export, gaussian_projection_supported=False, observation=observation)
            output = root / "active.json"
            command = [sys.executable, str(RUNNER), str(export), "--output", str(output),
                       "--device", "cpu", "--precision", "fp32", "--batch", "8",
                       "--phase-states", "4", "--relative-phase-states", "1",
                       "--actual-observation", "--max-blocks", "2", "--cn0", "18.06179973983887"]
            result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(report["status"], "uncalibrated_frontend_score_diagnostic")
            self.assertFalse(report["workload"]["gaussian_projection_supported"])
            self.assertIsNone(report["results"]["signal"]["operational_default"]["conditional_correct_accepted"])
            self.assertGreaterEqual(report["results"]["signal"]["operational_default"]["hypothetical_score_gate_crossings"], 0)
            self.assertEqual(report["results"]["null"]["status"], "unavailable")
            self.assertIsNone(report["timing_seconds"]["null_host_to_device"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

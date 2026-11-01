#!/usr/bin/env python3
"""Focused tests for the isolated projected/actual triad noise study."""
from __future__ import annotations

import importlib.util
import json
import math
import resource
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("triad_noise_study", ROOT / "tools" / "triad-noise-study.py")
study = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(study)


def write_workspace(directory: Path, *, seed: int, observation: bool = False,
                    block_count: int = 2, mixed_taps: bool = False,
                    valid_rows_count: int = 8) -> None:
    rng = np.random.default_rng(seed)
    mapping = [[0], [1], [2, 3], [4, 5]] if mixed_taps else [[0], [1]]
    d, block_samples, sample_rate = (6 if mixed_taps else 2), 8, 64.
    gram = np.broadcast_to(np.eye(d, dtype=np.complex128), (block_count, d, d)).copy()
    mean = np.full((block_count, d), .22+.1j, dtype=np.complex128)
    signal_energy = np.sum(np.abs(mean)**2, axis=1)
    intervals = np.asarray([[i*block_samples, (i+1)*block_samples]
                            for i in range(block_count)], dtype="<u8")
    valid_rows = np.full(block_count, valid_rows_count, dtype="<u8")
    arrays = {"gram.c128": gram.astype("<c16"), "mean.c128": mean.astype("<c16"),
              "intervals.u64": intervals, "valid_rows.u64": valid_rows,
              "signal_energy.f64": signal_energy.astype("<f8")}
    sizes = {}
    for name, array in arrays.items():
        array.tofile(directory / name)
        sizes[name] = (directory / name).stat().st_size
    candidates = []
    descriptors = []
    for index, columns in enumerate(mapping):
        bit = index % 2
        candidates.append({"bit": bit, "tap_count": len(columns), "arrival_seconds": 0.,
                           "frequency_hz": 0., "clock_ppm": 0.,
                           "frequency_drift_hz_per_second": 0.,
                           "taps": [{"delay_seconds": 0., "doppler_hz": 0.} for _ in columns]})
        for _ in columns:
            descriptors.append({"candidate_index": index, "source_bit": bit,
                            "arrival_seconds": 0., "frequency_hz": 0.,
                            "clock_ppm": 0., "frequency_drift_hz_per_second": 0.,
                            "tap_delay_seconds": 0., "tap_doppler_hz": 0.})
    manifest = {
        "format": "triad-block-stats-v1", "byte_order": "little",
        "complex_type": "complex128", "real_type": "float64", "integer_type": "uint64",
        "layout": "block,row,column", "block_count": block_count, "dimension": d,
        "sample_rate": sample_rate, "noise_variance": 1.,
        "sample_count": block_count*block_samples, "block_samples": block_samples,
        "gram_file": "gram.c128", "gram_shape": [block_count,d,d],
        "mean_file": "mean.c128", "mean_shape": [block_count,d],
        "intervals_file": "intervals.u64", "intervals_shape": [block_count,2],
        "valid_rows_file": "valid_rows.u64", "valid_rows_shape": [block_count],
        "signal_energy_file": "signal_energy.f64", "signal_energy_shape": [block_count],
        "column_mapping": mapping, "candidate_count": len(mapping), "candidates": candidates,
        "column_descriptors": descriptors, "file_sizes": sizes,
        "stats_noisy": bool(observation), "gaussian_projection_supported": True,
        "control": "signal", "signal_bit_simulation_truth": seed % 2,
        "channel": "fixture",
    }
    if observation:
        raw = rng.standard_normal((block_count,d)) + 1j*rng.standard_normal((block_count,d))
        raw = raw.astype("<c16")
        raw.tofile(directory / "observation.c128")
        orthogonal = rng.standard_normal((block_count, valid_rows_count-d)) + 1j*rng.standard_normal((block_count, valid_rows_count-d))
        energy = (np.sum(np.abs(raw)**2, axis=1) + np.sum(np.abs(orthogonal)**2, axis=1)).astype("<f8")
        energy.tofile(directory / "received_energy.f64")
        sizes["observation.c128"] = (directory / "observation.c128").stat().st_size
        sizes["received_energy.f64"] = (directory / "received_energy.f64").stat().st_size
        manifest.update({"observation_file": "observation.c128",
                         "observation_shape": [block_count,d],
                         "received_energy_file": "received_energy.f64",
                         "received_energy_shape": [block_count],
                         "received_energy_semantics": "per-block sum |y|^2 of actual noisy samples over valid rows from the same second channel pass; simulation evaluation only"})
    (directory / "manifest.json").write_text(json.dumps(manifest))


class NoiseStudyTests(unittest.TestCase):
    def test_static_coherence_actual_report_is_strict_json_without_changing_prior(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"actual"; path.mkdir()
            write_workspace(path, seed=30, observation=True)
            output = Path(temp)/"static.json"
            args = study._parse_args([
                str(path), "--output", str(output), "--mode", "actual", "--cn0", "0",
                "--reference-variance", "2", "--methods", "gaussian-ou", "residual-ucb",
                "angular-gaussian", "--coherences", "10000", "100000", "1000000", "inf",
                "--taps", "1", "--calibration-scope", "stationary",
            ])
            report = study.run(args)
            self.assertEqual(args.coherences, [10000., 100000., 1000000., math.inf])
            self.assertEqual(report["configuration"]["coherences"],
                             [10000., 100000., 1000000., "infinity"])
            self.assertEqual(json.loads(output.read_text()), report)
            self.assertEqual(report["capture_outcomes"]["completed_capture_records"], 1)
            self.assertEqual(report["capture_outcomes"]["failed_capture_records"], 0)
            self.assertTrue(all(row["status"] == "scored"
                                for row in report["captures"][0]["methods"].values()))
            with np.load(output.with_suffix(".scores.npz")) as scores:
                self.assertTrue(all(np.isfinite(scores[name]).all() for name in scores.files))

    def test_configuration_infinity_encoding_is_recursive_and_keeps_invalid_values_rejected(self):
        encoded = study._json_model_parameters({"prior": [1., {"coherence": (math.inf,)}]})
        self.assertEqual(encoded, {"prior": [1., {"coherence": ["infinity"]}]})
        json.dumps(encoded, allow_nan=False)
        for invalid in (math.nan, -math.inf):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                json.dumps(study._json_model_parameters({"prior": [invalid]}), allow_nan=False)

    def test_phase_frequency_model_is_explicit_and_static_gps_prior_is_available(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"path"; path.mkdir()
            write_workspace(path, seed=4001)
            output = Path(temp)/"static.json"
            base = [str(path), "--output", str(output), "--cn0", "0",
                    "--methods", "phase-frequency", "--taps", "1",
                    "--phase-states", "16", "--relative-phase-states", "2"]
            defaults = study._parse_args(base)
            self.assertEqual(defaults.frequency_model, "ou")
            self.assertEqual(defaults.frequency_random_walk, 1e-6)
            configured = study._parse_args(base + ["--frequency-model", "static",
                                                   "--frequency-random-walk", ".025"])
            workspace = study.BlockWorkspace(path)
            states = study._new_states(configured, workspace, np.asarray([[0],[1]]), 1, False)
            diagnostic = states["phase-frequency"][0].diagnostics()
            self.assertEqual(diagnostic["frequency_model"], "static")
            self.assertEqual(diagnostic["random_walk_hz_per_sqrt_second"], .025)

    @unittest.skipUnless(hasattr(resource, "RLIMIT_NOFILE"), "requires POSIX file-descriptor limits")
    def test_many_actual_paths_do_not_accumulate_mmap_descriptors(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = []
            for index in range(64):
                path = Path(temp)/f"path-{index:05d}"
                path.mkdir()
                write_workspace(path, seed=2000+index, observation=True, block_count=1)
                paths.append(path)
            old_soft, old_hard = resource.getrlimit(resource.RLIMIT_NOFILE)
            low_limit = min(old_soft, 24)
            if old_hard != resource.RLIM_INFINITY:
                low_limit = min(low_limit, old_hard)
            self.assertGreaterEqual(low_limit, 16)
            try:
                resource.setrlimit(resource.RLIMIT_NOFILE, (low_limit, old_hard))
                output = Path(temp)/"many.json"
                args = study._parse_args([
                    *(str(path) for path in paths), "--output", str(output),
                    "--mode", "actual", "--cn0", "0", "--reference-variance", "1",
                    "--methods", "gaussian-ou", "--coherences", "16", "--taps", "1",
                    "--threads", "2",
                ])
                report = study.run(args)
            finally:
                resource.setrlimit(resource.RLIMIT_NOFILE, (old_soft, old_hard))
            outcomes = report["capture_outcomes"]
            self.assertEqual(outcomes["attempted_capture_records"], 64)
            self.assertEqual(outcomes["completed_capture_records"], 64)
            self.assertEqual(outcomes["failed_capture_records"], 0)
            self.assertEqual(report["summary_by_control"]["signal"]["gaussian-ou"]["independent_physical_paths"], 64)

    def test_cli_reports_capture_errors_and_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = [Path(temp)/"path-00000", Path(temp)/"path-00001"]
            for index, path in enumerate(paths):
                path.mkdir()
                write_workspace(path, seed=3000+index, observation=True, block_count=1)
            output = Path(temp)/"with-error.json"
            argv = [*(str(path) for path in paths), "--output", str(output),
                    "--mode", "actual", "--cn0", "0", "--reference-variance", "1",
                    "--methods", "gaussian-ou", "--coherences", "16", "--taps", "1"]
            original = study._run_capture

            def fail_one(args, workspace, *rest, **kwargs):
                if workspace.directory.name == "path-00001":
                    raise OSError("synthetic descriptor failure")
                return original(args, workspace, *rest, **kwargs)

            with patch.object(study, "_run_capture", side_effect=fail_one):
                self.assertEqual(study.main(argv), 2)
            report = json.loads(output.read_text())
            self.assertEqual(report["capture_outcomes"], {
                "attempted_capture_records": 2, "completed_capture_records": 1,
                "failed_capture_records": 1,
                "failed_records_are_excluded_from_reception_denominators": True,
            })
            self.assertEqual(report["captures"][1]["error"], "synthetic descriptor failure")
            summary = report["summary_by_control"]["signal"]["gaussian-ou"]
            self.assertEqual(summary["independent_physical_paths"], 1)
            self.assertEqual(summary["failed_physical_capture_records"], 1)

    def test_stationary_pool_dense_gamma_and_deterministic_replay(self):
        import torch
        rng = np.random.default_rng(995)
        blocks, raw_residuals = [], []
        for index in range(3):
            A = study._cn(rng, (13, 4))
            y = study._cn(rng, (7, 13))*1.7
            Q, _ = np.linalg.qr(A, mode="complete")
            raw_residuals.append(np.sum(abs(y@Q[:, 4:].conj())**2, axis=1))
            blocks.append((index, y@A.conj(), A.conj().T@A,
                           np.sum(abs(y)**2, axis=1), 13, 1., 0.))
        delta = 1e-8
        upper, info = study._pool_stationary_residuals(iter(blocks), delta, device=torch.device("cpu"))
        expected = study.variance_upper_confidence(np.sum(raw_residuals, axis=0), 27, delta)
        np.testing.assert_allclose(upper.numpy(), expected.numpy(), atol=1e-12, rtol=1e-12)
        self.assertEqual(info["complex_dof"], 27)
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)/"replay"; p.mkdir()
            write_workspace(p, seed=34, block_count=5)
            args = study._parse_args([str(p), "--output", str(Path(temp)/"out.json"),
                "--cn0", "0", "--reference-variance", "1", "--trials", "7",
                "--methods", "residual-ucb", "--coherences", "16", "--taps", "1",
                "--calibration-scope", "stationary", "--projection-validation-trials", "600"])
            w = study.BlockWorkspace(p)
            first = list(study._capture_blocks(args, w, 0, "null", 1., [1.]*5, actual=False))
            second = list(study._capture_blocks(args, w, 0, "null", 1., [1.]*5, actual=False))
            for left, right in zip(first, second):
                for i in (1, 2, 3):
                    np.testing.assert_array_equal(left[i], right[i])

    def test_stationary_sensitivity_budget_and_variable_power_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)/"path"; p.mkdir()
            write_workspace(p, seed=10, block_count=32)
            mean = np.zeros((32, 2), dtype="<c16"); mean[:, 0] = .6
            mean.tofile(p/"mean.c128")
            np.full(32, .36, dtype="<f8").tofile(p/"signal_energy.f64")
            results = {}
            for scope in ("per-block", "stationary"):
                args = study._parse_args([str(p), "--output", str(Path(temp)/(scope+".json")),
                    "--cn0", "0", "--reference-variance", "1", "--trials", "128",
                    "--controls", "signal", "--methods", "gaussian-ou", "residual-ucb",
                    "--coherences", "1000000000000", "--taps", "1", "--calibration-scope", scope,
                    "--calibration-alternatives", "2", "--selection-methods", "7",
                    "--false-probability", ".05", "--horizon-seconds", "60",
                    "--attempt-spacing", "60", "--projection-validation-trials", "600"])
                report = study.run(args)
                capture = report["captures"][0]
                self.assertEqual(capture["status"], "completed")
                with np.load(Path(temp)/(scope+".scores.npz")) as arrays:
                    results[scope] = {m: arrays["path00000_signal_"+m].copy() for m in ("gaussian-ou", "residual-ucb")}
                gate = capture["methods"]["residual-ucb"]["threshold"]["ucb_budget"]
                self.assertEqual(gate["allocated_segments_per_capture"], (32 if scope == "per-block" else 1)*2)
                self.assertEqual(gate["calibration_alternatives"], 2)
                delta_total = gate["delta_per_segment"]*gate["allocated_segments_per_capture"]
                self.assertLessEqual(delta_total, .025)
                if scope == "stationary":
                    pool = capture["stationary_residual_pool"]
                    self.assertEqual(pool["complex_dof"], 32*6)
                    self.assertTrue(pool["replay_identity_verified"])
            np.testing.assert_array_equal(results["stationary"]["gaussian-ou"], results["per-block"]["gaussian-ou"])
            pooled_gap = np.median(abs(results["stationary"]["residual-ucb"][:, 0]-results["stationary"]["gaussian-ou"][:, 0]))
            block_gap = np.median(abs(results["per-block"]["residual-ucb"][:, 0]-results["per-block"]["gaussian-ou"][:, 0]))
            self.assertLess(pooled_gap, block_gap*.5)
            with self.assertRaises(SystemExit):
                study._parse_args([str(p), "--output", str(Path(temp)/"rejected.json"),
                    "--cn0", "0", "--methods", "residual-ucb", "--calibration-scope", "stationary",
                    "--noise-step-factor", "2", "--noise-step-fraction", ".5"])

    def test_independent_path_masks_and_known_only_reserved_budget(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = [Path(temp)/"p0", Path(temp)/"p1"]
            for i, p in enumerate(paths):
                p.mkdir(); write_workspace(p, seed=40+i, observation=True, valid_rows_count=8-i)
            args = study._parse_args([*(str(p) for p in paths), "--output", str(Path(temp)/"out.json"),
                "--mode", "actual", "--cn0", "0", "--reference-variance", "1",
                "--methods", "gaussian-ou", "--selection-methods", "7", "--coherences", "16", "--taps", "1"])
            report = study.run(args)
            self.assertEqual([c["valid_raw_complex_samples"] for c in report["captures"]], [16, 14])
            for capture in report["captures"]:
                threshold = capture["methods"]["gaussian-ou"]["threshold"]
                self.assertEqual(threshold["score_probability"], .5e-4)
                self.assertEqual(threshold["calibration_failure_probability"], 0.)
                self.assertEqual(threshold["reserved_calibration_probability"], .5e-4)

    def test_mixed_tap_bank_selects_model_but_keeps_full_residual_bank(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)/"mixed"
            directory.mkdir()
            write_workspace(directory, seed=41, mixed_taps=True)
            args = study._parse_args([
                str(directory), "--output", str(Path(temp)/"mixed.json"),
                "--mode", "projected", "--cn0", "-4", "--reference-variance", "1",
                "--trials", "4", "--methods", "gaussian-ou", "angular-gaussian",
                "residual-ucb", "--selection-methods", "5", "--coherences", "16",
                "--taps", "2", "--projection-validation-trials", "600",
            ])
            with patch.object(study, "whole_bank_residual", wraps=study.whole_bank_residual) as residual:
                report = study.run(args)
            self.assertTrue(residual.called)
            self.assertTrue(all(call.args[1].shape == (6, 6) for call in residual.call_args_list))
            for capture in report["captures"]:
                self.assertEqual(capture["status"], "completed")
                self.assertEqual(capture["candidate_count"], 4)
                self.assertEqual(capture["selected_candidate_indices"], [2, 3])
                self.assertEqual(capture["selected_candidate_count"], 2)
                self.assertEqual(capture["calibration_full_bank_dimension"], 6)
                self.assertTrue(all(method["status"] == "scored" for method in capture["methods"].values()))

    def test_joint_sampler_dense_raw_validation(self):
        result = study._projection_self_test(seed=51543, trials=1200)
        self.assertTrue(result["passed"])
        self.assertLess(result["metrics"]["angular_common_unit_scale_error"], 1e-12)
        self.assertLess(result["metrics"]["covariance_relative_error"], .06)

    def test_rank_truncation_and_mixed_family_budget(self):
        # A Gram formed from three rows and eight columns is rank three even
        # when roundoff leaves small positive eigenvalues.
        rng = np.random.default_rng(213)
        A = (rng.standard_normal((3,8)) + 1j*rng.standard_normal((3,8))) / np.sqrt(2.)
        G = A.conj().T @ A
        signal = np.array([.4+.2j, -.1+.3j, .2-.2j])
        mean = A.conj().T @ signal
        energy = float(np.linalg.norm(signal)**2)
        # Verify the rank-truncated sampler against independent raw draws,
        # including the total-energy law when the public bank is overcomplete.
        trials, variance = 3000, 1.2
        raw_y = study._cn(np.random.default_rng(100), (trials,3))*np.sqrt(variance) + signal
        raw_r, raw_energy = raw_y @ A.conj(), np.sum(np.abs(raw_y)**2, axis=1)
        r, observed_energy = study.project_joint_statistics(
            G, mean, energy, 3, variance, np.random.default_rng(4), trials, True)
        self.assertEqual(r.shape, (trials,8))
        self.assertTrue(np.isfinite(observed_energy).all())
        stable = study._rank_stable_gram(G)
        self.assertLess(np.linalg.norm(stable-G), 1e-9)
        np.testing.assert_allclose((r.T @ r.conj())/trials,
                                   variance*G + mean[:,None]*mean.conj()[None,:],
                                   rtol=.12, atol=.12)
        expected_mean = energy + 3*variance
        self.assertLess(abs(observed_energy.mean()-expected_mean),
                        4.5*np.sqrt((3*variance**2+2*variance*energy)/trials))
        self.assertLess(abs(raw_energy.mean()-observed_energy.mean()),
                        4.5*np.sqrt(2*(3*variance**2+2*variance*energy)/trials))

        args = study._parse_args([
            "fake0", "fake1", "fake2", "fake3", "fake4", "--output", "out.json",
            "--cn0", "0", "--methods", "gaussian-ou", "angular-gaussian",
            "residual-ucb", "phase-frequency", "phase-frequency-ucb",
            "--false-probability", ".05",
        ])
        _, gates = study._gates(args, 24)
        self.assertTrue(all(g["score_probability"] == .05*.5 for g in gates.values()))
        self.assertTrue(all(g["calibration_failure_probability"] == .05*.5 for g in gates.values()))
        attempts = np.ceil(args.horizon_seconds/args.attempt_spacing)
        score_union = sum(np.exp(-g["threshold_log_likelihood_ratio"])
                          * attempts * args.duration_alternatives * 2
                          for g in gates.values())
        calibration_union = .05*.5
        self.assertLessEqual(score_union + calibration_union, .05 + 1e-12)

    def test_projected_paired_controls_and_path_independence_report(self):
        with tempfile.TemporaryDirectory() as temp:
            p0, p1 = Path(temp)/"path0", Path(temp)/"path1"
            p0.mkdir(); p1.mkdir()
            write_workspace(p0, seed=10)
            write_workspace(p1, seed=11)
            output = Path(temp)/"projected.json"
            args = study._parse_args([
                str(p0), str(p1), "--output", str(output), "--mode", "projected",
                "--cn0", "10", "--reference-variance", "1", "--noise-scale", "1.25",
                "--trials", "4", "--methods", "gaussian-ou", "angular-gaussian",
                "residual-ucb", "--coherences", "16", "--taps", "1",
                "--false-probability", ".05", "--horizon-seconds", "60",
                "--attempt-spacing", "60", "--projection-validation-trials", "600",
            ])
            result = study.run(args)
            self.assertEqual(len(result["captures"]), 4)
            self.assertEqual({capture["conditional_noise_trials"] for capture in result["captures"]}, {4})
            self.assertEqual(result["summary_by_control"]["signal"]["gaussian-ou"]["independent_physical_paths"], 2)
            self.assertEqual(result["summary_by_control"]["null"]["residual-ucb"]["independent_physical_paths"], 2)
            for capture in result["captures"]:
                self.assertEqual(capture["methods"]["angular-gaussian"]["status"], "scored")
                self.assertEqual(capture["methods"]["residual-ucb"]["calibration_status"], "independent_whole_bank_ucb_budgeted")
            with np.load(output.with_suffix(".scores.npz")) as scores:
                self.assertEqual(scores["path00000_signal_gaussian-ou"].shape, (4,2))
            self.assertEqual(json.loads(output.read_text())["schema"], "triad-noise-study-v1")

    def test_piecewise_scale_skips_angular_instead_of_claiming_validity(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)/"path"
            p.mkdir()
            write_workspace(p, seed=22, block_count=4)
            args = study._parse_args([
                str(p), "--output", str(Path(temp)/"step.json"), "--mode", "projected",
                "--cn0", "10", "--reference-variance", "1", "--noise-scale", "1",
                "--noise-step-factor", "2", "--noise-step-fraction", ".5",
                "--trials", "3", "--methods", "angular-gaussian", "residual-ucb",
                "--coherences", "16", "--taps", "1", "--false-probability", ".05",
                "--horizon-seconds", "60", "--attempt-spacing", "60",
                "--projection-validation-trials", "600",
            ])
            result = study.run(args)
            for capture in result["captures"]:
                self.assertEqual(capture["methods"]["angular-gaussian"]["status"], "skipped_model_mismatch")
                self.assertEqual(capture["methods"]["residual-ucb"]["status"], "scored")

    def test_actual_uses_explicit_reference_and_one_observation(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)/"actual"
            p.mkdir()
            write_workspace(p, seed=30, observation=True, block_count=2)
            output = Path(temp)/"actual.json"
            args = study._parse_args([
                str(p), "--output", str(output), "--mode", "actual", "--cn0", "0",
                "--reference-variance", "2", "--methods", "gaussian-ou", "angular-gaussian",
                "residual-ucb", "--coherences", "16", "--taps", "1",
                "--false-probability", ".05", "--horizon-seconds", "60",
                "--attempt-spacing", "60", "--projection-validation-trials", "600",
            ])
            result = study.run(args)
            self.assertEqual(len(result["captures"]), 1)
            self.assertEqual(result["captures"][0]["methods"]["angular-gaussian"]["status"], "scored")
            self.assertEqual(result["captures"][0]["conditional_noise_trials"], 1)
            self.assertEqual(result["captures"][0]["reference_variance"], 2)


if __name__ == "__main__":
    unittest.main()

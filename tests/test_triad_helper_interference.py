import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "triad_helper_interference", ROOT / "tools/triad_helper_interference.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def controls(root, *, wrong_noise=False):
    fs, count = 1000, 45000
    time = np.arange(count) / fs
    rng = np.random.default_rng(102)
    noise = .01 * (rng.standard_normal(count) + 1j * rng.standard_normal(count))
    data = .2 * np.exp(2j * np.pi * 30 * time) * ((time >= 38) & (time < 43))
    helper = 2 * np.exp(2j * np.pi * 150 * time) + np.exp(-2j * np.pi * 150 * time)
    paths = []
    for name, d, h, values in (
            ("selected", True, True, data + helper + noise),
            ("data", True, False, data + noise),
            ("helper", False, True, helper + noise),
            ("noise", False, False, noise + (.001 if wrong_noise else 0))):
        folder = root / name
        folder.mkdir()
        (folder / "observation.c128").write_bytes(values.astype("<c16").tobytes())
        (folder / "validity.u8").write_bytes(bytes([1]) * count)
        manifest = {
            "schema": "triad-helper-public-v1", "case": "unit", "control": "flat",
            "sample_rate_hz": fs, "public_epoch_seconds": 2, "key_seed": 3,
            "data_enabled": d, "helper_enabled": h,
            "observation": {"file": "observation.c128", "dtype": "<c16", "shape": [count]},
            "validity": {"file": "validity.u8", "shape": [count]},
            "format": {"occupied_bandwidth_hz": 100, "dwell_seconds": .4,
                       "physical_rf_retuning": False},
            "calendar": {"capture_lead_seconds": 0, "helper_start_seconds": 0,
                         "helper_end_seconds": 45, "arrival_prior_seconds": [0, 0]},
            "channel": {"nominal_conversion_if_hz": 700, "deterministic_cfo_bound_hz": .1,
                        "spread_margin": 1, "spread_doppler_hz": 0, "selective_slow_depth": 0,
                        "selective_fast_depth": 0, "relative_phase_diffusion_rad_sqrt_s": 0,
                        "sample_clock_pair_ppm": 0, "endpoint_uniform_half_bound_ppm": 0},
            "helper_plan": {"low_hz": 550, "high_hz": 850, "pilot_dwell_gating": False},
            "power": {"complex_noise_variance": .0002},
        }
        (folder / "manifest.json").write_text(json.dumps(manifest))
        (folder / "evaluator-truth.json").write_text(json.dumps({
            "random_seed_roots": {"channel": 1, "noise": 2}, "source_energy": {},
            "source_bit": 0, "source_epoch_offset_seconds": 0, "arrival_seconds": 0,
            "tx": {"audio_ppm": .1, "rf_ppm": .2},
            "rx": {"audio_ppm": -.1, "rf_ppm": -.2}}))
        paths.append(folder)
    return paths, data, helper, noise


class InterferenceTests(unittest.TestCase):
    def test_four_control_subtraction_and_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, d, h, n = controls(Path(temporary))
            result = MODULE.matched_components(*paths)
            np.testing.assert_allclose(result["data"], d, atol=1e-14)
            np.testing.assert_allclose(result["helper"], h, atol=1e-14)
            np.testing.assert_allclose(result["noise"], n, atol=1e-14)
            self.assertTrue(result["linearity"]["passed"])
            self.assertEqual(len(result["identities"]), 4)
            self.assertEqual(len(result["identities"]["data_only"]["manifest_sha256"]), 64)

    def test_independent_control_detects_nonlinear_or_mismatched_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary), wrong_noise=True)
            with self.assertRaisesRegex(ValueError, "superposition"):
                MODULE.matched_components(*paths)

    def test_rejects_mismatched_public_channel_and_missing_samples(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary))
            metadata = paths[1] / "manifest.json"
            value = json.loads(metadata.read_text())
            value["channel"]["spread_doppler_hz"] = 1
            metadata.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, "channel"):
                MODULE.matched_components(*paths)
            value["channel"]["spread_doppler_hz"] = 0
            metadata.write_text(json.dumps(value))
            for path in paths:
                (path / "validity.u8").write_bytes(bytes([0]) + bytes([1]) * 44999)
            with self.assertRaisesRegex(ValueError, "complete captured"):
                MODULE.matched_components(*paths)

    def test_triplet_rejects_opposite_data_bit_without_fourth_control(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary))
            path = paths[1] / "evaluator-truth.json"
            truth = json.loads(path.read_text())
            truth["source_bit"] = 1
            path.write_text(json.dumps(truth))
            with self.assertRaisesRegex(ValueError, "source_bit"):
                MODULE.matched_components(*paths[:3])

    def test_triplet_rejects_changed_physical_realization(self):
        for field, changed in (("arrival_seconds", .01), ("tx", {"audio_ppm": 2}),
                               ("rx", {"rf_ppm": 3})):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                paths, *_ = controls(Path(temporary))
                path = paths[2] / "evaluator-truth.json"
                truth = json.loads(path.read_text())
                truth[field] = changed
                path.write_text(json.dumps(truth))
                with self.assertRaisesRegex(ValueError, field):
                    MODULE.matched_components(*paths[:3])

    def test_inactive_data_bit_is_irrelevant_but_missing_endpoint_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary))
            path = paths[2] / "evaluator-truth.json"
            truth = json.loads(path.read_text())
            truth["source_bit"] = 1
            path.write_text(json.dumps(truth))
            MODULE.matched_components(*paths[:3])
            del truth["tx"]
            path.write_text(json.dumps(truth))
            with self.assertRaisesRegex(ValueError, "tx"):
                MODULE.matched_components(*paths[:3])

    def test_actual_fir_complete_tail_noise_energy_and_coherent_bound(self):
        rng = np.random.default_rng(4)
        d = rng.standard_normal(100) + 1j * rng.standard_normal(100)
        n = rng.standard_normal(100) + 1j * rng.standard_normal(100)
        h = .01 * n
        taps = np.array([.25, .5, .25])
        result = MODULE.helper_to_data(d, h, n, taps=taps, decimation=1, noise_variance=2)
        self.assertEqual(result["output_samples_including_filter_tail"], 102)
        self.assertAlmostEqual(result["expected_white_noise_output_energy"], 2 * 100 * .375)
        self.assertAlmostEqual(result["helper_to_measured_noise_energy_db"], -40)
        self.assertAlmostEqual(result["actual_helper_plus_noise_to_noise_energy_db"],
                               result["coherent_noise_norm_envelope_db"])
        self.assertLess(result["power_based_noise_equivalent_penalty_db"], .001)

    def test_frontend_profiles_do_not_silently_cover_other_rates(self):
        with self.assertRaisesRegex(ValueError, "18 kHz"):
            MODULE.frontend_filter(48000, 225)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            MODULE.frontend_filter(18000, 1000)
        taps, down, report = MODULE.frontend_filter(1000, 100, "custom", 80, 100)
        self.assertFalse(report["is_existing_receiver_frontend"])
        self.assertFalse(report["receiver_qualification_established"])
        self.assertGreater(len(taps), 1)
        self.assertGreaterEqual(down, 1)
        self.assertLess(report["measured_stopband_maximum_gain"], 1e-4)

    def test_dated_phase_probe_retains_frequency_bias(self):
        fs, n, pilot, cfo = 1000, 1200, 100, .02
        t = 50 + np.arange(n) / fs
        clean = np.exp(2j * np.pi * (pilot + cfo) * t)
        result = MODULE.phase_probe(clean, clean, sample_rate=fs, first_sample_seconds=50,
            pilot_relative_hz=pilot, estimate_hz=cfo + .001, reference_estimate_hz=cfo)
        self.assertFalse(result["frequency_bias_removed"])
        self.assertFalse(result["phase_offset_fitted_away"])
        self.assertEqual(result["apertures"], 3)
        self.assertAlmostEqual(result["rows"][0]["phase_error_radians"],
                               -2 * np.pi * .001 * result["rows"][0]["center_seconds"], places=5)
        self.assertGreater(result["rms_error_degrees"], 18)

    def test_phase_zero_reference_is_unresolved(self):
        result = MODULE.phase_probe(np.ones(500), np.zeros(500), sample_rate=1000,
            first_sample_seconds=0, pilot_relative_hz=0, estimate_hz=0, reference_estimate_hz=0)
        self.assertEqual(result["resolved_apertures"], 0)
        self.assertIsNone(result["rms_error_degrees"])

    def test_fresh_phase_is_calendar_origin_invariant_but_transport_is_not(self):
        fs, n, pilot = 1000, 1200, 100
        clean = np.exp(2j * np.pi * pilot * np.arange(n) / fs)
        arguments = dict(sample_rate=fs, pilot_relative_hz=pilot,
                         estimate_hz=.001, reference_estimate_hz=0)
        early = MODULE.phase_probe(clean, clean, first_sample_seconds=0, **arguments)
        late = MODULE.phase_probe(clean, clean, first_sample_seconds=50, **arguments)
        self.assertEqual(late["phase_reference_origin_seconds"], 0)
        self.assertLess(early["rms_error_degrees"], 1)
        self.assertGreater(late["rms_error_degrees"], 18)
        self.assertAlmostEqual(early["centered_aperture_fresh_phase"]["rms_error_degrees"],
                               late["centered_aperture_fresh_phase"]["rms_error_degrees"], places=12)

    def test_local_quadratic_peak_is_labeled_diagnostic(self):
        grid = np.array([-.1, 0, .1])
        scores = 10 - (grid - .02) ** 2
        value, method = MODULE._peak({"peaks_hz": {"joint": 0},
                                     "profile_grid_hz": grid, "profile_scores": scores})
        self.assertAlmostEqual(value, .02)
        self.assertIn("not_receiver", method)

    def test_template_energy_does_not_mistake_global_phase_for_discrimination(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary))
            manifest = json.loads((paths[0] / "manifest.json").read_text())
            templates = np.ones((1, 2, 64), dtype="<c16")
            templates[0, 1] = -1
            (paths[0] / "templates.c128").write_bytes(templates.tobytes())
            manifest["templates"] = {"enabled": True, "file": "templates.c128", "dtype": "<c16",
                                      "shape": [1, 2, 64], "epoch_offsets_seconds": [0]}
            (paths[0] / "manifest.json").write_text(json.dumps(manifest))
            result = MODULE.template_energy_metrics(paths[0])
            self.assertEqual(result["source_bit_energy_samples"], [64, 64])
            self.assertEqual(result["source_bit_difference_energy_samples"], 256)
            self.assertEqual(result["unknown_global_phase_minimum_difference_energy_samples"], 0)

    def test_lead_only_measurement_rejected_and_overlap_calls_real_estimator_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths, *_ = controls(Path(temporary))
            components = MODULE.matched_components(*paths)
            with self.assertRaisesRegex(ValueError, "lead-only"):
                MODULE.helper_measurement(components, start_seconds=.4)
            values = [{"peaks_hz": {"joint": v}, "profile_grid_hz": [], "profile_scores": [],
                       "status": "narrowed"} for v in [.001, .0005, 0]]
            with mock.patch.object(MODULE, "estimate_frequency", side_effect=values) as estimator:
                report = MODULE.describe_interference(*paths[:3], noise_directory=paths[3],
                    helper_start_seconds=4, frontend_profile="custom", pass_hz=80, stop_hz=100)
            self.assertEqual(estimator.call_count, 3)
            self.assertEqual(estimator.call_args_list[0].kwargs["first_sample_seconds"], 4)
            self.assertEqual(estimator.call_args_list[0].kwargs["observation_seconds"], 36)
            result = report["data_to_helper"]
            self.assertGreater(result["data_energy_in_window"], 0)
            self.assertAlmostEqual(result["composite_minus_helper_only_cfo_hz"], .0005)
            self.assertEqual(result["absolute_frequency_error_ratio"], 2)
            self.assertEqual(result["phase"]["composite"]["high"]["apertures"], 90)
            json.dumps(report, allow_nan=False)


if __name__ == "__main__":
    unittest.main()

"""Cheap bridge/projection checks; no audio hardware or long source export."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_live_waveforms as live


class GeometryTests(unittest.TestCase):
    def test_real_images_and_measured_band_are_distinct(self):
        self.assertTrue(live.audio_geometry(2250, 1725, 48000)["supported"])
        bad = live.audio_geometry(.625, .005, 48000)
        self.assertFalse(bad["real_image_separated"])
        self.assertFalse(live.audio_geometry(15000, 1725, 48000)["supported"])
        self.assertTrue(live.audio_geometry(15000, 9000, 48000)["supported"])
        analog = live.audio_geometry(.00625, .005, 48000, audio_band_hz=(20, 20000))
        self.assertTrue(analog["real_image_separated"])
        self.assertFalse(analog["measured_audio_band_supported"])
        self.assertIsNone(live.audio_geometry(2250, 1725, 48000)["measured_audio_band_supported"])

    def test_census_factors_only_native_geometry(self):
        def configure(argv, **kwargs):
            rate = float(argv[argv.index("--rate") + 1])
            added = int(argv[argv.index("--added") + 1])
            if added == 1000:
                return subprocess.CompletedProcess(argv, 2, "", "unsupported native example")
            duration = max(9, 384 / rate)
            result = dict(configuration=dict(capture=dict(nominal_triplet_bandwidth_hz=.625 * rate * added,
                                                         sample_rate_hz=100)),
                          performance_geometry=dict(capture_samples=int(duration * 100)))
            return subprocess.CompletedProcess(argv, 0, json.dumps(result), "")
        with mock.patch.object(live.subprocess, "run", side_effect=configure) as run:
            rows = live.geometry_census("fake-binary", rates=(.1, 3600), added_values=(1, 1000),
                                        carrier_modes=("half_rate", 1725, "adaptive"),
                                        fhss_values=(False, True))
        self.assertEqual(run.call_count, 8)
        self.assertEqual(len(rows), 24)
        self.assertIn("actual_rate_component_and_model_prediction_only", {r["evidence"] for r in rows})
        self.assertIn("native_geometry_unavailable", {r["evidence"] for r in rows})
        self.assertTrue(all(not r["real_rf_retuning_tested"] for r in rows))


class ExportTests(unittest.TestCase):
    def fixture(self, folder, window=False):
        n = 16
        bank = np.asarray([np.ones(n), np.exp(2j * np.pi * np.arange(n) / n)], np.complex64)
        signal = bank[0].copy()
        basis_name = "source_candidates.c64" if window else "basis.c64"
        bank.tofile(folder / basis_name)
        signal.tofile(folder / "signal.c64")
        manifest = dict(format="triad-source-window-v1" if window else "triad-long-basis-v1",
                        byte_order="native", complex_type="complex64", sample_rate=100,
                        layout="candidate,sample" if window else "candidate,sample,column",
                        signal_file="signal.c64", signal_shape=[n])
        if window:
            manifest.update(source_candidates_file=basis_name, source_candidates_shape=[2, n],
                            candidate_bits=[0, 1], partial_source_window=True, first_sample=12345)
        else:
            manifest.update(basis_file=basis_name, basis_shape=[2, n, 1], bitlabels=[0, 1])
        (folder / "manifest.json").write_text(json.dumps(manifest))
        return manifest

    def test_public_bank_and_native_source_window_layouts(self):
        for window in (False, True):
            with self.subTest(window=window), tempfile.TemporaryDirectory() as folder:
                path = Path(folder)
                manifest = self.fixture(path, window)
                result = live.load_export(path)
                self.assertEqual(result["basis"].shape, (2, 16, 1))
                self.assertEqual(result["manifest"], manifest)
                self.assertEqual(result["candidate_bits"], [0, 1])
                self.assertNotIn("gaussian_admission", result["manifest"])

    def test_truncated_arrays_and_path_escape_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = self.fixture(path)
            (path / "signal.c64").write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "byte count"):
                live.load_export(path)
            self.fixture(path)
            manifest["basis_file"] = "../basis.c64"
            (path / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "basename"):
                live.load_export(path)


class BridgeTests(unittest.TestCase):
    def test_composite_roundtrip_gain_phase_and_time(self):
        for native_fs, audio_fs, carrier, width in ((960, 48000, 1725, 600),
                                                   (48000, 48000, 1725, 2250),
                                                   (960, 48000, 23500, 600)):
            with self.subTest(native_fs=native_fs, carrier=carrier):
                t = np.arange(int(native_fs * .6)) / native_fs
                x = (.1 * np.exp(2j * np.pi * 113 * t + .2j)
                     + .07 * np.exp(-2j * np.pi * 211 * t - .9j)).astype(np.complex64)
                pcm, tx = live.to_audio(x, native_fs, audio_fs, carrier,
                                       occupied_bandwidth_hz=width, fixed_gain=.2,
                                       source_start_seconds=12.345)
                recovered, valid, rx = live.from_audio(pcm, audio_fs, native_fs, carrier,
                    occupied_bandwidth_hz=width, source_start_seconds=12.345,
                    expected_samples=len(x), undo_fixed_gain=.2)
                edge = int(np.ceil((tx["resample_half_support_seconds"] +
                                    rx["resample_half_support_seconds"]) * native_fs))
                valid[:edge] = False
                valid[len(valid)-edge:] = False
                self.assertTrue(valid.any())
                error = np.linalg.norm(recovered[valid] - x[valid]) / np.linalg.norm(x[valid])
                self.assertLess(error, 4e-4)
                self.assertEqual(tx["fixed_gain"], .2)
                self.assertEqual(rx["retained_samples"], len(x))

    def test_gain_is_not_silently_normalized_and_low_image_is_explicit(self):
        source = np.ones(960, np.complex64)
        with self.assertRaisesRegex(ValueError, "explicit lower gain"):
            live.to_audio(source, 960, 48000, 1725, occupied_bandwidth_hz=60)
        with self.assertRaisesRegex(ValueError, "65537"):
            live.from_audio(np.zeros(4800), 48000, 960, .005, occupied_bandwidth_hz=.00625)
        with self.assertRaisesRegex(ValueError, "real-audio conjugate"):
            live.to_audio(source, 960, 48000, .005, occupied_bandwidth_hz=.625, fixed_gain=.1)


class ProjectionTests(unittest.TestCase):
    def make_bank(self, n=192):
        rng = np.random.default_rng(193)
        a = rng.normal(size=(2, n, 2)) + 1j * rng.normal(size=(2, n, 2))
        a[0, :, 1] = 2j * a[0, :, 0]  # Exact rank-deficient nuisance mode.
        return a

    def test_svd_projection_matches_independent_lstsq_and_mask(self):
        bank = self.make_bank()
        y = bank[1] @ np.asarray([1 + .4j, -.7j])
        y[4] = 1e5j  # Must not leak into masked diagnostic.
        mask = np.ones(len(y), bool)
        mask[4] = False
        result = live.project_diagnostic(y, bank, mask=mask)
        self.assertEqual(result["candidate_ranks"], [1, 2])
        self.assertEqual(result["chosen_bit"], 1)
        for i in range(2):
            a = bank[i, mask]
            fit = a @ np.linalg.lstsq(a, y[mask], rcond=1e-10)[0]
            self.assertAlmostEqual(result["candidate_scores"][i], np.vdot(fit, fit).real, places=9)
        self.assertIsNone(result["operational_admission"])
        with self.assertRaisesRegex(NotImplementedError, "colored-noise"):
            live.project_diagnostic(y, bank, noise_psd=np.ones(len(y)))

    def test_partial_pattern_global_phase_alias_has_no_margin_curve(self):
        one = np.exp(.5j * np.arange(32))
        bank = np.asarray([one, 3j * one])
        result = live.project_diagnostic(one, bank)
        self.assertFalse(result["identifiability"]["all_cross_bit_subspaces_distinct"])
        curve = live.noise_margin_comparison(one, one, bank, (-20, 0, 20), 19,
            sample_rate=32, duration_seconds=1, reference_power=1, truth_bit=0)
        self.assertEqual(curve["status"], "unidentifiable_bank")
        self.assertEqual(curve["rows"], [])

    def test_fp32_rounding_does_not_create_spurious_channel_rank(self):
        rng = np.random.default_rng(292)
        source = (rng.normal(size=(2, 257)) + 1j * rng.normal(size=(2, 257))).astype(np.complex64)
        bank = np.stack([source, source * np.complex64(1.2 + .7j)], axis=-1)
        result = live.project_diagnostic(source[0], bank)
        self.assertEqual(result["candidate_ranks"], [1, 1])
        self.assertGreater(result["rank_policy"]["effective_relative_threshold"], 1e-7)
        self.assertEqual(result["rank_policy"]["source_dtype"], "complex64")
        # A real high-precision mode of this size remains available to a
        # genuinely FP64 bank; this is not an upcast of quantized columns.
        orthogonal = np.roll(source.astype(np.complex128), 1, axis=1)
        high_precision = np.stack([source.astype(np.complex128),
            source.astype(np.complex128) * (1.2 + .7j) + 1e-8 * orthogonal], axis=-1)
        reference = live.project_diagnostic(high_precision[0, :, 0], high_precision)
        self.assertEqual(reference["candidate_ranks"], [2, 2])
        self.assertEqual(reference["rank_policy"]["effective_relative_threshold"], 1e-10)
        check = live.validate_noise_projection(source[0], bank, trials=3)
        self.assertEqual(check["rank_policy"], result["rank_policy"])
        self.assertLess(check["maximum_relative_score_error"], 1e-12)
        self.assertTrue(check["bit_rankings_equal"])

    def test_full_sample_noise_check_and_joint_covariance(self):
        bank = self.make_bank()
        y = bank[1, :, 0]
        check = live.validate_noise_projection(y, bank, trials=3, sample_variance=20)
        self.assertTrue(check["bit_rankings_equal"])
        self.assertLess(check["maximum_relative_score_error"], 1e-12)
        prepared = live._prepare_bank(bank)
        factor, covariance = live._noise_factor(prepared)
        np.testing.assert_allclose(factor @ factor.conj().T, covariance, atol=2e-14)
        self.assertGreater(abs(covariance[0, 1]), 1e-3)

    def test_same_noise_draws_and_common_reference_preserve_attenuation(self):
        n = 64
        bank = np.asarray([np.ones(n), np.exp(2j * np.pi * np.arange(n) / n)])
        common = dict(sample_rate=64, duration_seconds=1, reference_power=1,
                      truth_bit=0, trials=4096, sampled_validation_trials=1)
        paired = live.noise_margin_comparison(bank[0], bank[0], bank, (0, 10), 19, **common)
        self.assertTrue(all(r["real_only_correct"] == r["predicted_only_correct"] == 0
                            for r in paired["rows"]))
        attenuated = live.noise_margin_comparison(.2 * bank[0], bank[0], bank, (10,), 19, **common)
        row = attenuated["rows"][0]
        self.assertGreater(row["predicted_correct_fraction"] - row["real_correct_fraction"], .25)
        self.assertAlmostEqual(attenuated["real_observation_energy_seconds"], .04)
        self.assertIsNone(attenuated["false_admission_probability"])
        self.assertEqual(attenuated["independent_hardware_observations"], 1)


class BoundedTimingTests(unittest.TestCase):
    def bank(self, n=256):
        t = np.arange(n) - 128.
        return np.asarray([np.exp(-.5 * (t/13)**2) * np.exp(2j*np.pi*f*t)
                           for f in (.08, .23)])

    def test_fractional_delay_sign_and_exact_public_endpoints(self):
        bank = self.bank()
        fs = 960.
        for delay in (-3.4, 2.5, 3.4):
            t = np.arange(256) - 128. - delay
            y = (1.4-.7j) * np.exp(-.5*(t/13)**2) * np.exp(2j*np.pi*.23*t)
            result = live.bounded_timing_projection(y, bank[..., None], fs,
                                                    (-3.4/fs, 3.4/fs))
            self.assertEqual(result["chosen_bit"], 1)
            self.assertAlmostEqual(result["selected_delay_seconds"], delay/fs, places=12)
            self.assertGreater(result["best_retained_observation_fraction"], .999999)
            self.assertTrue(result["includes_both_prior_endpoints"])
            self.assertFalse(result["search_center_derived_from_payload"])

    def test_fixed_mask_energy_normalization_and_linear_not_circular(self):
        rng = np.random.default_rng(700)
        bank = rng.normal(size=(2, 35)) + 1j*rng.normal(size=(2, 35))
        y = rng.normal(size=35) + 1j*rng.normal(size=35)
        mask = np.ones(35, bool)
        mask[:6], mask[21:24] = False, False
        result = live.bounded_timing_projection(y, bank, 10, (-.4,.4),
                                               fractional_phases=1, mask=mask)
        for ci in range(2):
            expected = []
            for delay in range(-4,5):
                shifted = np.zeros(35, np.complex128)
                for n in range(35):
                    source = n-delay
                    if 0 <= source < 35:
                        shifted[n] = bank[ci,source]
                dot = np.vdot(shifted[mask], y[mask])
                expected.append(abs(dot)**2 / np.vdot(shifted[mask], shifted[mask]).real)
            np.testing.assert_allclose(result["bit_projection_energy_curves"][ci], expected,
                                       rtol=2e-13, atol=2e-13)
        rescaled = bank.copy()
        rescaled[0] *= 71.3
        again = live.bounded_timing_projection(y, rescaled, 10, (-.4,.4),
                                              fractional_phases=1, mask=mask)
        np.testing.assert_allclose(result["bit_projection_energy_curves"],
                                   again["bit_projection_energy_curves"], rtol=2e-13, atol=2e-13)

    def test_fhss_grid_bound_and_unsupported_channel_dimension(self):
        bank = self.bank()
        result = live.bounded_timing_projection(bank[1], bank, 960, (-.09,.09))
        self.assertEqual(result["timing_hypotheses"], 693)
        self.assertEqual(result["fractional_actual_branches"], 6)
        self.assertAlmostEqual(result["regular_grid_step_seconds"], 1/3840)
        self.assertIsNone(result["operational_admission"])
        with self.assertRaisesRegex(NotImplementedError, "one column"):
            live.bounded_timing_projection(bank[1], np.repeat(bank[...,None],2,axis=2),960,(-.09,.09))
        with self.assertRaisesRegex(ValueError, "resource cap"):
            live.bounded_timing_projection(bank[1],bank,960,(-.09,.09),maximum_fft_work_points=1)


if __name__ == "__main__":
    unittest.main()

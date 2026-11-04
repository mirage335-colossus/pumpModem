from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest
from unittest import mock

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_live_model as m


def physical_path(x, fs, *, delay=73, cutoff=18, gain=.7, noise=1e-5):
    b, a = m._hp_coefficients(cutoff, fs)
    y = signal.lfilter([gain, .035, -.017], [1.], signal.lfilter(b, a, x))
    y = np.r_[np.zeros(delay), y, np.zeros(fs)]
    return y + np.random.default_rng(913).normal(0, noise, len(y))


class LiveAudioModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fs = 4000
        cls.tx, cls.meta = m.generate_probe(cls.fs, 77, period_seconds=1, repeats=4)
        cls.rx = physical_path(cls.tx, cls.fs)
        cls.profile = m.fit_audio_profile(cls.tx, cls.rx, cls.fs, cls.meta)

    def test_probe_has_distinct_measurement_markers_and_periods(self):
        self.assertLessEqual(float(np.max(abs(self.tx))), .350001)
        self.assertEqual(len(self.meta["markers"]), 2)
        self.assertTrue(self.meta["measurement_markers_not_modem_symbols"])
        self.assertTrue(all(b % 4 for b in self.meta["excited_bins"]))
        self.assertEqual(self.profile["measured_transfer"]["disjoint_period_count"], 6)
        self.assertFalse(self.profile["alignment"]["alignment_uses_payload"])

    def test_frozen_profile_predicts_independent_phase_seed_and_level(self):
        tx, meta = m.generate_probe(self.fs, 998, levels=(.05, .18), period_seconds=1, repeats=4)
        rx = physical_path(tx, self.fs)
        y, valid, _ = m.align_capture(tx, rx, self.fs, meta["markers"])
        result = m.assess_prediction(tx, y, self.profile, valid)
        self.assertLess(abs(result["energy_prediction_error_db"]), .3, result)
        self.assertLess(result["fixed_model_residual_to_signal_db"], -22, result)
        self.assertFalse(result["gain_fit_applied_to_prediction"])
        self.assertFalse(result["operational_admission_claimed"])
        self.assertLess(abs(self.profile["highpass_cutoff_hz"] - 18), 3)
        json.dumps(self.profile, allow_nan=False)

    def test_relative_clock_fit_uses_marker_positions(self):
        y = physical_path(self.tx, self.fs, noise=0)
        ratio = 1.00012
        positions = np.arange(round(len(y) * ratio)) / ratio
        rx = np.interp(positions, np.arange(len(y)), y)
        _, _, report = m.align_capture(self.tx, rx, self.fs, self.meta["markers"])
        self.assertLess(abs(report["relative_rate_ppm"] - 120), 10, report)
        self.assertIsNone(report["rate_uncertainty_ppm"])
        self.assertFalse(report["absolute_clock_accuracy_measured"])

    def test_frozen_path_predicts_untrained_tones_bursts_and_steps(self):
        t = np.arange(3 * self.fs) / self.fs
        gate = (t % .31 < .14).astype(float)
        payload = gate * (.08 * np.cos(2 * np.pi * 733.375 * t + .7) +
                          .04 * np.cos(2 * np.pi * 1371.6 * t - .4))
        payload += .04 * ((t > .7) & (t < 1.15))
        tx, meta = m.wrap_with_markers(payload, self.fs, seed=172)
        rx = physical_path(tx, self.fs)
        y, valid, _ = m.align_capture(tx, rx, self.fs, meta["markers"])
        result = m.assess_prediction(tx, y, self.profile, valid)
        self.assertLess(abs(result["energy_prediction_error_db"]), .3, result)
        self.assertLess(result["fixed_model_residual_to_signal_db"], -22, result)

    def test_stream_state_and_full_tails_preserve_burst_history(self):
        profile = {"schema": m.SCHEMA, "sample_rate": self.fs,
                   "highpass_cutoff_hz": 15., "fir": [0., .7, .2, -.03]}
        x = np.r_[np.ones(731) * .2, np.zeros(90), np.ones(377) * -.1,
                  np.zeros(100)]
        whole = m.apply_audio_profile(x, profile)
        state = m.AudioModelState(profile)
        chunks = [state.process(x[a:b]) for a, b in [(0, 400), (400, 731),
                                                      (731, 840), (840, len(x))]]
        np.testing.assert_allclose(np.concatenate(chunks), whole, atol=2e-15)
        self.assertGreater(np.max(abs(whole[735:780])), .01)
        tail = m.apply_audio_profile(x, profile, full_tail=True)
        self.assertGreater(len(tail), len(x))
        self.assertLess(abs(tail[-1]), 1e-7)

    def test_level_dependent_path_is_flagged_instead_of_fitted_away(self):
        compressed = np.tanh(self.tx * 5) / 5
        rx = physical_path(compressed, self.fs)
        profile = m.fit_audio_profile(self.tx, rx, self.fs, self.meta)
        self.assertTrue(profile["level_dependence_flag"], profile["level_response"])
        self.assertGreater(profile["level_gain_span_db"], .5)

    def test_low_frequency_prediction_is_explicitly_extrapolation(self):
        self.assertGreater(self.profile["measured_frequency_support_hz"][0], .005)
        response = m.spectral_response(self.profile, np.array([.005, 1000]))
        self.assertLess(abs(response[0]) / abs(response[1]), .001)
        self.assertIn("Response outside measured frequency support is model extrapolation.", self.profile["limits"])
        self.assertFalse(self.profile["operational_admission_calibrated"])

    def test_bad_support_or_weak_marker_cannot_be_reported_valid(self):
        with self.assertRaises(ValueError):
            m.align_capture(self.tx, np.zeros_like(self.rx), self.fs, self.meta["markers"])
        with self.assertRaises(ValueError):
            m.align_capture(self.tx, self.rx, self.fs, self.meta["markers"][:1])
        with self.assertRaises(ValueError):
            m.generate_probe(self.fs, 1, levels=(.8,))

    def test_noise_density_and_model_energy_remain_separate(self):
        self.assertGreater(self.profile["noise"]["quiet_rms"], 0)
        self.assertEqual(self.profile["noise"]["effective_independent_hardware_captures"], 1)
        f = np.asarray(self.profile["noise"]["frequency_hz"])
        p = np.asarray(self.profile["noise"]["psd_fs_squared_per_hz"])
        self.assertTrue(np.all(np.diff(f) > 0))
        self.assertTrue(np.all(p >= 0))
        self.assertGreater(self.profile["training_prediction"]["actual_energy_gain_db"], -5)
        self.assertLess(self.profile["training_prediction"]["actual_energy_gain_db"], -2)

    def test_native_source_quantization_and_fitted_contract(self):
        source = np.array([-2., -1., -.5, -.9 / 32767, 0., .9 / 32767, .5, 1., 2.])
        expected = np.array([-32767, -32767, -16383, 0, 0, 0, 16383, 32767, 32767]) / 32768
        np.testing.assert_array_equal(m.quantize_production_playback(source), expected)
        # Float64 multiplication would truncate this source to zero, whereas
        # native float32 multiplication rounds to exactly one before truncation.
        threshold = np.array([np.float32(1 / 32767)])
        np.testing.assert_array_equal(m.quantize_production_playback(threshold), [1 / 32768])
        with self.assertRaises(ValueError):
            m.quantize_production_playback(np.array([1j]))
        pcm = m.quantize_production_playback(self.tx)
        profile = m.fit_audio_profile(self.tx, physical_path(pcm, self.fs), self.fs,
                                      self.meta, input_pcm="s16-native")
        self.assertEqual(profile["input_pcm_contract"], "s16-native")
        self.assertNotEqual(profile["source_sample_sha256"], profile["training_sample_sha256"])
        np.testing.assert_array_equal(m.simulate_audio_profile(self.tx, profile, include_noise=False),
                                      m.apply_audio_profile(pcm, profile))
        # The linear model stays linear: candidate columns must not be separately
        # quantized before their physical composite is formed.
        a = np.linspace(-.00002, .00002, 100)
        b = a[::-1] + .00001
        np.testing.assert_allclose(m.apply_audio_profile(a + 1j * b, profile),
                                   m.apply_audio_profile(a, profile) + 1j * m.apply_audio_profile(b, profile),
                                   atol=1e-18)

    def test_post_adc_noise_surrogate_density_and_optional_lattice(self):
        variance = 2.5e-7
        profile = {"schema": m.SCHEMA, "sample_rate": self.fs,
                   "highpass_cutoff_hz": 0., "fir": [1.],
                   "input_pcm_contract": "s16-native",
                   "noise": {"frequency_hz": [0., self.fs / 2],
                             "psd_fs_squared_per_hz": [2 * variance / self.fs] * 2}}
        x = np.zeros(65536)
        y = m.simulate_audio_profile(x, profile, seed=917)
        self.assertLess(abs(np.var(y) / variance - 1), .03)
        np.testing.assert_array_equal(y, m.simulate_audio_profile(x, profile, seed=917))
        self.assertFalse(np.all(y * 32768 == np.rint(y * 32768)))
        lattice = m.simulate_audio_profile(x, profile, seed=917, capture_lattice=True)
        np.testing.assert_array_equal(lattice * 32768, np.rint(lattice * 32768))
        self.assertIn("Gaussian", self.profile["noise"]["simulation_surrogate"])
        self.assertIn("post-ADC", self.profile["noise"]["measurement_domain"])
        profile["noise"] = {"frequency_hz": [0., 500., 501., self.fs / 2],
                            "psd_fs_squared_per_hz": [1e-7, 1e-7, 5e-9, 5e-9]}
        colored = m.simulate_audio_profile(x, profile, seed=918)
        frequencies = np.fft.rfftfreq(len(x), 1 / self.fs)
        power = abs(np.fft.rfft(colored)) ** 2
        ratio = np.mean(power[(frequencies > 50) & (frequencies < 450)]) / np.mean(power[(frequencies > 700) & (frequencies < 1800)])
        self.assertLess(abs(10 * np.log10(ratio / 20)), .4)

    def test_high_audio_fractional_resampling_does_not_create_level_loss(self):
        n = np.arange(2000)
        for frequency in (.1, .3, .4, .43):
            x = np.exp(2j * np.pi * frequency * n)
            position = n + .413
            y, valid = m._sinc_sample(x, position)
            expected = np.exp(2j * np.pi * frequency * position)
            self.assertLess(np.max(abs(y[valid] - expected[valid])), .0005)

    def test_unidentified_very_long_tail_cannot_allocate_silently(self):
        profile = {"schema": m.SCHEMA, "sample_rate": self.fs,
                   "highpass_cutoff_hz": .00001, "fir": [1.]}
        with self.assertRaisesRegex(ValueError, "tail .* exceeds explicit"):
            m.apply_audio_profile(np.ones(100), profile, full_tail=True)
        # Streaming still supports the same operator without a giant tail array.
        state = m.AudioModelState(profile)
        self.assertEqual(len(state.process(np.ones(100))), 100)

    def test_metrics_reuse_full_prediction_before_masking_without_refilter(self):
        x = np.r_[np.ones(100) * .1, np.zeros(400)]
        prediction = m.apply_audio_profile(x, self.profile)
        valid = np.arange(len(x)) >= 100
        baseline = m.assess_prediction(x, prediction, self.profile, valid)
        with mock.patch.object(m, "apply_audio_profile", side_effect=AssertionError("unexpected refilter")):
            reused = m.assess_prediction(x, prediction, self.profile, valid, prediction=prediction)
        self.assertEqual(reused, baseline)
        self.assertGreater(reused["predicted_energy"], 0)
        for bad in (prediction[:-1], np.full(len(x), np.nan), prediction[:, None]):
            with self.subTest(shape=bad.shape), self.assertRaises(ValueError):
                m.assess_prediction(x, prediction, self.profile, valid, prediction=bad)

    def test_overlap_add_long_complex_capture_matches_full_fft_and_tail(self):
        profile = {"schema": m.SCHEMA, "sample_rate": self.fs,
                   "highpass_cutoff_hz": 12., "fir": (np.hanning(257) / 128).tolist()}
        rng = np.random.default_rng(819)
        x = (rng.normal(size=32768) + 1j * rng.normal(size=32768)) * .01
        actual = m.apply_audio_profile(x, profile, full_tail=True)
        padded = np.pad(x, (0, len(actual) - len(x)))
        b, a = m._hp_coefficients(profile["highpass_cutoff_hz"], self.fs)
        expected = signal.fftconvolve(signal.lfilter(b, a, padded), profile["fir"], mode="full")[:len(actual)]
        np.testing.assert_allclose(actual, expected, atol=2e-17, rtol=2e-13)


class LowFrequencySectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fs = 4000
        cls.tx, cls.manifest = m.generate_low_frequency_probe(cls.fs, 32)
        # Independent polynomial construction of a known stable analog cascade,
        # then one bilinear transform; the fitter builds separate SOS sections.
        w1, w2 = 2 * np.pi * 7.5, 2 * np.pi * 2.
        cls.b, cls.a = signal.bilinear([1., 0., 0., 0.],
                                      np.polymul([1., w1], [1., w2 / .65, w2*w2]), fs=cls.fs)
        cls.rx = cls.physical(cls.tx)
        cls.section = m.fit_low_frequency_section(cls.tx, cls.rx, cls.fs, cls.manifest)
        broad, meta = m.generate_probe(cls.fs, 41, period_seconds=1)
        cls.profile = m.fit_audio_profile(broad, cls.physical(broad), cls.fs, meta,
                                         input_pcm="s16-native", low_frequency_section=cls.section)

    @classmethod
    def physical(cls, tx):
        x = m.quantize_production_playback(tx)
        y = signal.lfilter([.73, .01], [1.], signal.lfilter(cls.b, cls.a, x))
        y = np.r_[np.zeros(73), y, np.zeros(cls.fs)]
        return y + np.random.default_rng(642).normal(0, 1e-7, len(y))

    def test_concentrated_training_reserves_one_hz_and_identifies_stable_section(self):
        frequencies = np.array(self.manifest["excited_bins"]) / self.manifest["period_seconds"]
        self.assertNotIn(1., frequencies)
        self.assertLessEqual(len(frequencies), 32)
        self.assertEqual(self.manifest["samples"] / self.fs, 140.6)
        self.assertLess(abs(self.section["real_pole_hz"] / 7.5 - 1), .03)
        self.assertLess(abs(self.section["biquad_frequency_hz"] / 2 - 1), .03)
        self.assertLess(abs(self.section["biquad_q"] / .65 - 1), .03)
        self.assertEqual(self.section["training_flags"], [])
        self.assertEqual(self.section["model_order"], 3)
        self.assertEqual(self.profile["highpass_cutoff_hz"], 0.)
        self.assertEqual(self.profile["residual_low_frequency_constraint"]["through_hz"], 80.)
        json.dumps(self.profile, allow_nan=False)

    def test_frozen_full_profile_predicts_distinct_validation_bins_and_bursts(self):
        tx, meta = m.generate_low_frequency_probe(self.fs, 61, training=False, level=.12)
        self.assertEqual(meta["samples"] / self.fs, 85.6)
        y, valid, _ = m.align_capture(tx, self.physical(tx), self.fs, meta["markers"])
        result = m.assess_low_frequency_validation(tx, y, self.fs, meta, self.profile, valid)
        self.assertEqual(result["status"], "validated_resolved_bins", result)
        self.assertFalse(result["frozen_model_refitted"])
        self.assertTrue(all(abs(row["energy_gain_error_db"]) < .1 for row in result["rows"]))
        self.assertTrue(all(abs(row["phase_error_radians"]) < .03 for row in result["rows"]))
        with self.assertRaisesRegex(ValueError, "training manifest"):
            m.fit_low_frequency_section(tx, self.physical(tx), self.fs, meta)

    def test_sos_state_response_and_tail_agree_and_invalid_sections_reject(self):
        rng = np.random.default_rng(62)
        x = np.r_[rng.normal(size=700) * .01, np.zeros(12000)].astype(complex)
        x[:700] += 1j * rng.normal(size=700) * .01
        full = m.apply_audio_profile(x, self.profile)
        state = m.AudioModelState(self.profile)
        chunks = [state.process(x[:301]), state.process(x[301:2000]), state.process(x[2000:])]
        np.testing.assert_allclose(np.r_[tuple(chunks)], full, atol=3e-16)
        frequency = np.array([.5, 1., 3., 30., 200.])
        impulse = m.apply_audio_profile(np.r_[1., np.zeros(20*self.fs)], self.profile)
        measured = np.array([np.dot(impulse, np.exp(-2j*np.pi*f*np.arange(len(impulse))/self.fs)) for f in frequency])
        np.testing.assert_allclose(measured, m.spectral_response(self.profile, frequency), rtol=2e-7, atol=1e-9)
        bad = dict(self.profile, highpass_cutoff_hz=10.)
        with self.assertRaisesRegex(ValueError, "old one-pole"):
            m.apply_audio_profile(x, bad)
        section = dict(self.section, sos=[[1., -1., 0., 1., -1.01, 0.], self.section["sos"][1]])
        with self.assertRaisesRegex(ValueError, "unit circle"):
            m.apply_audio_profile(x, dict(self.profile, low_frequency_section=section))

    def test_validation_rejects_wrong_model_and_does_not_pass_unresolved_lines(self):
        tx, meta = m.generate_low_frequency_probe(self.fs, 64, training=False, level=.12)
        y, valid, _ = m.align_capture(tx, self.physical(tx), self.fs, meta["markers"])
        wrong = dict(self.profile, highpass_cutoff_hz=11.5)
        del wrong["low_frequency_section"]
        report = m.assess_low_frequency_validation(tx, y, self.fs, meta, wrong, valid)
        self.assertEqual(report["status"], "failed")
        empty = m.assess_low_frequency_validation(tx, np.zeros_like(y), self.fs, meta, self.profile, valid)
        self.assertEqual(empty["status"], "unresolved")
        self.assertEqual(empty["resolved_count"], 0)

    def test_four_pole_option_recovers_second_corner_without_extra_sos_states(self):
        b, a = signal.bilinear([1., 0.], [1., 2 * np.pi * .47], fs=self.fs)
        rx = signal.lfilter(b, a, self.rx)
        section = m.fit_low_frequency_section(self.tx, rx, self.fs, self.manifest, model_order=4)
        self.assertEqual(section["model_order"], 4)
        self.assertEqual(section["model_law"], "two_real_hp_poles_plus_hp_biquad")
        self.assertEqual(np.asarray(section["sos"]).shape, (2, 6))
        self.assertEqual(section["real_poles_hz_descending"], sorted(section["real_poles_hz_descending"], reverse=True))
        np.testing.assert_allclose(section["real_poles_hz_descending"], [7.5, .47], rtol=.04)
        self.assertLess(abs(section["biquad_frequency_hz"] / 2 - 1), .04)
        self.assertLess(abs(section["biquad_q"] / .65 - 1), .04)
        self.assertEqual(section["training_flags"], [])
        _, high = signal.sosfreqz(np.array(section["sos"]), worN=[np.pi])
        self.assertAlmostEqual(float(high[0].real), 1., places=12)

    def test_fresh_validation_grid_shares_only_reserved_one_hz(self):
        _, old = m.generate_low_frequency_probe(self.fs, 72, training=False)
        tx, fresh = m.generate_low_frequency_probe(self.fs, 983131, training=False,
                                                   level=.09, validation_variant=1)
        frequencies = set(np.array(fresh["excited_bins"]) / fresh["period_seconds"])
        previous = set(np.array(old["excited_bins"]) / old["period_seconds"])
        training = set(np.array(self.manifest["excited_bins"]) / self.manifest["period_seconds"])
        self.assertEqual(frequencies & previous, {1.})
        self.assertFalse(frequencies & training)
        self.assertEqual(fresh["validation_variant"], 1)
        y, valid, _ = m.align_capture(tx, self.physical(tx), self.fs, fresh["markers"])
        result = m.assess_low_frequency_validation(tx, y, self.fs, fresh, self.profile, valid)
        self.assertEqual(result["status"], "validated_resolved_bins", result)


if __name__ == "__main__":
    unittest.main()

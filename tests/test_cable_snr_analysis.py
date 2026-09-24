"""Synthetic signal checks for the offline cable SNR measurement method."""

import importlib.util
import json
from pathlib import Path
import unittest
import numpy as np


SPEC = importlib.util.spec_from_file_location("cable_snr_analysis", Path(__file__).resolve().parents[1] / "tools/cable_snr_analysis.py")
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


class ToneAnalysisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fs = 48000
        cls.time = np.arange(cls.fs * 2) / cls.fs
        cls.frequency = 997.2314
        cls.amplitude = 0.5
        cls.signal_power = cls.amplitude ** 2 / 2
        # A 60 dB SNR inside 300..18300 Hz, with white full-Nyquist noise.
        cls.noise_variance = cls.signal_power / 1e6 * (cls.fs / 2) / 18000
        cls.noise = np.random.default_rng(417).normal(0, np.sqrt(cls.noise_variance), cls.time.size)
        cls.tone = cls.amplitude * np.sin(2 * np.pi * cls.frequency * cls.time + 0.37)

    def test_non_bin_centered_awgn_snr_and_psd_normalization(self):
        result = ANALYSIS.analyze_tone(self.tone + self.noise, self.fs, 997)
        self.assertTrue(result["qualified"])
        self.assertAlmostEqual(result["frequency_hz"], self.frequency, delta=0.0001)
        self.assertAlmostEqual(result["bands"][0]["snr_db"], 60.0, delta=0.2)
        self.assertAlmostEqual(result["bands"][0]["sinad_db"], 60.0, delta=0.2)
        expected_wide = 60 + 10 * np.log10(18000 / 19980)
        self.assertAlmostEqual(result["bands"][1]["snr_db"], expected_wide, delta=0.2)
        window = np.hanning(self.time.size)
        centered = self.tone + self.noise - np.mean(self.tone + self.noise)
        expected_power = float(np.sum((centered * window) ** 2) / np.sum(window ** 2))
        measured_power = float(np.sum(result["psd"]["input"]) * self.fs / self.time.size)
        self.assertAlmostEqual(measured_power, expected_power, places=12)
        json.dumps({key: value for key, value in result.items() if key != "psd"}, allow_nan=False)

    def test_harmonic_exclusion_changes_sinad_not_noise_snr(self):
        # Third harmonic at -40 dBc, far above the -60 dBc noise.
        distortion = self.amplitude * 0.01 * np.sin(2 * np.pi * 3 * self.frequency * self.time - 0.8)
        result = ANALYSIS.analyze_tone(self.tone + distortion + self.noise, self.fs, 997)
        band = result["bands"][0]
        self.assertAlmostEqual(band["snr_db"], 60, delta=0.2)
        self.assertAlmostEqual(band["sinad_db"], -10 * np.log10(1e-4 + 1e-6), delta=0.05)
        self.assertAlmostEqual(band["thd_db"], -40, delta=0.05)
        self.assertAlmostEqual(band["thd_percent"], 1, delta=0.01)
        third = next(h for h in result["harmonics"] if h["order"] == 3)
        self.assertAlmostEqual(third["dbc"], -40, delta=0.05)

    def test_harmonics_above_configured_limit_remain_in_noise(self):
        distortion = self.amplitude * 0.01 * np.sin(2 * np.pi * 7 * self.frequency * self.time)
        values = self.tone + distortion + self.noise
        default = ANALYSIS.analyze_tone(values, self.fs, 997)
        extended = ANALYSIS.analyze_tone(values, self.fs, 997, max_harmonic=32)
        self.assertAlmostEqual(default["bands"][0]["snr_db"], -10 * np.log10(1e-4 + 1e-6), delta=0.05)
        self.assertAlmostEqual(extended["bands"][0]["snr_db"], 60, delta=0.2)
        self.assertAlmostEqual(default["bands"][0]["sinad_db"], extended["bands"][0]["sinad_db"], delta=0.01)

    def test_clipped_measurement_is_returned_and_unqualified(self):
        clipped = np.clip(self.tone * 2.4, -1, 1)
        result = ANALYSIS.analyze_tone(clipped, self.fs, 997)
        self.assertFalse(result["qualified"])
        self.assertGreater(result["levels"]["s16_full_scale_samples"], 0)
        self.assertIn("near_full_scale_samples_present", result["qualification_reasons"])
        self.assertIsNotNone(result["bands"][0]["sinad_db"])
        self.assertLess(result["bands"][0]["sinad_db"], 30)

    def test_higher_harmonic_is_folded_at_nyquist(self):
        distortion = self.amplitude * 0.01 * np.sin(2 * np.pi * 30 * self.frequency * self.time)
        result = ANALYSIS.analyze_tone(self.tone + distortion + self.noise, self.fs, 997, max_harmonic=32)
        harmonic = next(h for h in result["harmonics"] if h["order"] == 30)
        self.assertTrue(harmonic["aliased"])
        self.assertAlmostEqual(harmonic["frequency_hz"], self.fs - 30 * self.frequency, delta=0.001)
        self.assertAlmostEqual(harmonic["dbc"], -40, delta=0.05)
        self.assertAlmostEqual(result["bands"][0]["snr_db"], 60, delta=0.2)

    def test_separate_silence_band_power(self):
        result = ANALYSIS.analyze_silence(self.noise, self.fs)
        expected = self.noise_variance * 18000 / (self.fs / 2)
        self.assertAlmostEqual(10 * np.log10(result["bands"][0]["noise_power_fs2"] / expected), 0, delta=0.2)
        self.assertTrue(result["qualified"])

    def test_invalid_inputs_are_rejected(self):
        for values in (np.zeros((1000, 2)), np.zeros(127), np.full(1000, np.nan)):
            with self.assertRaises(ValueError):
                ANALYSIS.analyze_tone(values, self.fs)
        with self.assertRaises(ValueError):
            ANALYSIS.analyze_tone(self.tone, self.fs, bands=((300, 25000),))
        with self.assertRaises(ValueError):
            ANALYSIS.analyze_tone(self.tone, self.fs, max_harmonic=0)


class MultitoneAnalysisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fs = 48000
        cls.time = np.arange(cls.fs * 2) / cls.fs
        cls.frequencies = np.array([353, 1009, 1999, 3001, 4003, 5003, 6007, 7001,
                                    8009, 9001, 10007, 11003, 12007, 13001,
                                    14009, 15013, 16001, 17011, 18199], dtype=float)
        cls.amplitudes = np.full(cls.frequencies.size, 0.03)
        cls.phases = np.arange(cls.frequencies.size) ** 2 * 0.23
        cls.signal_power = float(np.sum(cls.amplitudes ** 2) / 2)
        cls.noise_variance = cls.signal_power / 1e6 * (cls.fs / 2) / 18000
        cls.noise = np.random.default_rng(20260920).normal(0, np.sqrt(cls.noise_variance), cls.time.size)

    def waveform(self, scale=1.0):
        return np.sum(self.amplitudes[:, None] * np.cos(
            2 * np.pi * self.frequencies[:, None] * scale * self.time + self.phases[:, None]), axis=0)

    def test_known_awgn_and_per_tone_amplitude_phase(self):
        result = ANALYSIS.analyze_multitone(self.waveform() + self.noise, self.fs, self.frequencies,
                                           reference_amplitudes=self.amplitudes,
                                           reference_phases_rad=self.phases)
        self.assertTrue(result["qualified"])
        self.assertFalse(result["clock_scale_fitted"])
        self.assertTrue(result["clock_scale_is_nominal"])
        self.assertEqual(result["bands"][0]["fitted_tones_in_band"], 19)
        self.assertAlmostEqual(result["bands"][0]["linear_multitone_residual_ratio_db"], 60, delta=0.2)
        self.assertNotIn("snr_db", result["bands"][0])
        for tone in result["tones"]:
            self.assertAlmostEqual(tone["amplitude_gain"], 1, delta=0.0002)
            self.assertAlmostEqual(tone["phase_difference_rad"], 0, delta=0.0002)
        json.dumps({key: value for key, value in result.items() if key != "psd"}, allow_nan=False)

    def test_uncommanded_distortion_remains_in_residual(self):
        distortion_amplitude = np.sqrt(2 * self.signal_power * 1e-4)
        distortion = distortion_amplitude * np.sin(2 * np.pi * 2501 * self.time)
        result = ANALYSIS.analyze_multitone(self.waveform() + distortion + self.noise, self.fs, self.frequencies)
        self.assertAlmostEqual(result["bands"][0]["linear_multitone_residual_ratio_db"],
                               -10 * np.log10(1e-4 + 1e-6), delta=0.05)
        frequencies = result["psd"]["frequency_hz"]
        density = result["psd"]["without_commanded_tones"]
        peak_hz = frequencies[np.argmax(density)]
        self.assertEqual(peak_hz, 2501)

    def test_supplied_common_clock_scale_without_frequency_overfit(self):
        scale = 1.00002
        values = self.waveform(scale) + self.noise
        result = ANALYSIS.analyze_multitone(values, self.fs, self.frequencies, clock_scale=scale)
        nominal = ANALYSIS.analyze_multitone(values, self.fs, self.frequencies)
        self.assertFalse(result["clock_scale_fitted"])
        self.assertFalse(result["clock_scale_is_nominal"])
        self.assertAlmostEqual(result["bands"][0]["linear_multitone_residual_ratio_db"], 60, delta=0.2)
        self.assertLess(nominal["bands"][0]["linear_multitone_residual_ratio_db"], 20)

    def test_invalid_tone_configuration(self):
        for frequencies in ([], [1000, 1000], [0, 1000], [1000, 24000]):
            with self.assertRaises(ValueError):
                ANALYSIS.analyze_multitone(self.noise, self.fs, frequencies)
        with self.assertRaises(ValueError):
            ANALYSIS.analyze_multitone(self.noise, self.fs, self.frequencies, reference_amplitudes=[0.1])


if __name__ == "__main__":
    unittest.main()

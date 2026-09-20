"""Synthetic controls for the offline acoustic channel measurement method."""
import importlib.util
import json
from pathlib import Path
import unittest

import numpy as np


SPEC = importlib.util.spec_from_file_location("acoustic_channel_analysis", Path(__file__).resolve().parents[1] / "tools/acoustic_channel_analysis.py")
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


def channel_capture(source, gain=.4, delay=2500, noise_rms=2e-4, impulse=None, nonlinear=0.):
    if impulse is None:
        output = gain * source + nonlinear * source ** 3
    else:
        size = 1 << (source.size + impulse.size - 2).bit_length()
        output = np.fft.irfft(np.fft.rfft(source, size) * np.fft.rfft(impulse, size), size)[:source.size + impulse.size - 1]
    result = np.concatenate([np.zeros(delay), output, np.zeros(3000)])
    return result + np.random.default_rng(771).normal(0, noise_rms, result.size)


class AcousticAnalysisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source, cls.metadata = ANALYSIS.generate_stimulus(
            amplitude=.3, sample_rate=12000, period_samples=4096,
            blocks=6, repeats=4, low_hz=150, high_hz=4500)

    def test_flat_channel_gain_noise_normalization_and_json(self):
        noise = 2e-4
        report, arrays = ANALYSIS.analyze_capture(self.source, channel_capture(self.source, noise_rms=noise), self.metadata)
        selected = (arrays["frequency_hz"] > 300) & (arrays["frequency_hz"] < 4200)
        gain = np.hypot(arrays["transfer_real"], arrays["transfer_imag"])
        self.assertAlmostEqual(float(np.median(gain[selected])), .4, delta=.003)
        self.assertAlmostEqual(report["delay_samples"], 2500, delta=.05)
        self.assertAlmostEqual(report["clock_error_ppm"], 0, delta=.3)
        measured_noise = arrays["repeat_residual_psd"].sum() * 12000 / 4096
        self.assertAlmostEqual(measured_noise, noise ** 2, delta=noise ** 2 * .1)
        self.assertGreater(float(np.median(arrays["coherence"][selected])), .999)
        self.assertTrue(report["qualified"])
        self.assertGreater(report["bands"][0]["uniform_power_bps"], 35000)
        json.dumps(report, allow_nan=False)

    def test_independent_phases_retain_coherent_nonlinearity(self):
        report, arrays = ANALYSIS.analyze_capture(self.source, channel_capture(self.source, nonlinear=6., noise_rms=1e-5), self.metadata)
        selected = (arrays["frequency_hz"] > 300) & (arrays["frequency_hz"] < 4200)
        heldout = arrays["heldout_residual_psd"][selected].sum()
        stochastic = arrays["repeat_residual_psd"][selected].sum()
        self.assertGreater(heldout / stochastic, 1000)
        self.assertLess(report["bands"][0]["signal_to_heldout_residual_db"], 30)

    def test_two_delayed_echoes_remain_in_impulse(self):
        impulse = np.zeros(701)
        impulse[[0, 150, 700]] = [.4, .25, .15]
        report, arrays = ANALYSIS.analyze_capture(self.source, channel_capture(self.source, impulse=impulse, noise_rms=1e-5), self.metadata)
        spread = report["impulse"]["energy_90_percent_span_seconds"]
        self.assertGreater(spread, .055)
        self.assertLess(spread, .062)
        self.assertGreater(report["impulse"]["energy_outside_20ms_fraction"], .07)
        self.assertLess(report["impulse"]["energy_outside_20ms_fraction"], .13)

    def test_clock_drift_estimated_from_independent_markers(self):
        delay, scale = 2500.2, 1.000080
        padded = np.pad(self.source, (128, 128))
        size = int(delay + self.source.size * scale + 3000)
        time = (np.arange(size) - delay) / scale
        valid = (time >= 0) & (time <= self.source.size - 1)
        capture = np.zeros(size)
        capture[valid] = .4 * ANALYSIS._sample_at(padded, time[valid] + 128)
        capture += np.random.default_rng(831).normal(0, 1e-5, size)
        report, arrays = ANALYSIS.analyze_capture(self.source, capture, self.metadata)
        self.assertAlmostEqual(report["clock_error_ppm"], 80, delta=1.)
        self.assertAlmostEqual(report["delay_samples"], delay, delta=.1)
        self.assertGreater(report["bands"][0]["signal_to_heldout_residual_db"], 40)

    def test_capacity_uniform_awgn_and_water_filling_power(self):
        gains, noise, source = np.ones(100), np.ones(100) * .01, np.ones(100) * .1
        flat = ANALYSIS.capacity_integral(gains, noise, source, 10)
        self.assertAlmostEqual(flat["uniform_power_bps"], 1000 * np.log2(11), places=8)
        self.assertAlmostEqual(flat["water_filling_bps"], flat["uniform_power_bps"], places=8)
        gains[:50] = .001
        selective = ANALYSIS.capacity_integral(gains, noise, source, 10)
        self.assertGreater(selective["water_filling_bps"], selective["uniform_power_bps"])
        self.assertAlmostEqual(selective["water_filling_used_power_fs2"], 100., places=8)
        self.assertEqual(selective["water_filling_active_hz"], 500.)
        self.assertFalse(ANALYSIS.capacity_integral(gains, noise * 0, source, 10)["qualified"])

    def test_gain_motion_model_is_scored_on_other_repeats(self):
        modified = self.source.copy()
        for segment in self.metadata["segments"]:
            if segment["kind"] == "periodic":
                begin, count = segment["start_sample"], segment["samples"]
                modified[begin:begin + count] *= (1.15 if segment["block"] % 2 else .85)
        report, _ = ANALYSIS.analyze_capture(self.source, channel_capture(modified, noise_rms=1e-5), self.metadata)
        band = report["subbands"][1]
        self.assertLess(band["independent_gain_delay_test_adjusted_power_fs2"],
                        band["independent_gain_delay_test_baseline_power_fs2"] / 100)
        self.assertEqual(report["gain_delay_variation_diagnostic"]["fitted_real_parameters_per_block"], 3)

    def test_clipping_and_invalid_values_do_not_qualify(self):
        clipped = np.clip(channel_capture(self.source, gain=20), -1, 1)
        report, _ = ANALYSIS.analyze_capture(self.source, clipped, self.metadata)
        self.assertFalse(report["qualified"])
        self.assertIn("capture_near_full_scale", report["qualification_reasons"])
        invalid = self.source.copy()
        invalid[5] = np.nan
        with self.assertRaises(ValueError):
            ANALYSIS.analyze_capture(self.source, invalid, self.metadata)
        with self.assertRaises(ValueError):
            ANALYSIS.generate_stimulus(amplitude=1.)


if __name__ == "__main__":
    unittest.main()

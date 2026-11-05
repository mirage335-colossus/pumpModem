#!/usr/bin/env python3
"""Small public-input checks for the conditional Helper CFO estimator."""
import json
import pathlib
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import triad_helper_assistance as helper


class HelperFrequencyTests(unittest.TestCase):
    rate = 128
    count = rate * 36

    def setUp(self):
        self.time = np.arange(self.count) / self.rate
        self.arguments = dict(
            sample_rate=self.rate, data_if_hz=550, low_pilot_hz=525.123,
            high_pilot_hz=575.356, original_frequencies=np.linspace(-1, 1, 41),
            prior_half_width_hz=1, doppler_hz=0, slow_depth=0, fast_depth=0,
            noise_variance=1, first_sample_seconds=2.125,
            phase_diffusion_radians_per_sqrt_second=0)

    def signal(self, cfo=.27, low_amplitude=.6, high_amplitude=.5j):
        low = self.arguments["low_pilot_hz"] - self.arguments["data_if_hz"]
        high = self.arguments["high_pilot_hz"] - self.arguments["data_if_hz"]
        return (low_amplitude * np.exp(2j * np.pi * (low + cfo) * self.time)
                + high_amplitude * np.exp(2j * np.pi * (high + cfo) * self.time))

    def estimate(self, values=None, **changes):
        return helper.estimate_frequency(self.signal() if values is None else values,
                                         **(self.arguments | changes))

    def assert_fallback(self, result, reason, bank=None):
        self.assertEqual(result["status"], "fallback")
        self.assertEqual(result["reason"], reason)
        expected = self.arguments["original_frequencies"] if bank is None else bank
        self.assertEqual(result["selected_frequencies"], list(expected))
        self.assertEqual(result["selected_count"], len(expected))

    def test_flat_channel_strong_cfo_and_dated_availability(self):
        rng = np.random.default_rng(701)
        noisy = self.signal() + (rng.normal(size=self.count) + 1j * rng.normal(size=self.count)) / np.sqrt(2)
        with patch.object(helper, "_profile", wraps=helper._profile) as profiles:
            result = self.estimate(noisy, pilot_dwell_gating=True)
        self.assertEqual(result["status"], "narrowed", result["reason"])
        self.assertLessEqual(result["grid_step_hz"], 1 / (8 * 36) + 1e-15)
        self.assertLess(abs(result["peaks_hz"]["joint"] - .27), .005)
        self.assertEqual(result["available_at"], 38.125)
        self.assertEqual(result["sample_count"], self.count)
        self.assertGreaterEqual(result["wall_seconds"], 0)
        self.assertLess(result["selected_count"], result["original_count"])
        self.assertTrue(set(result["selected_frequencies"]) <= set(self.arguments["original_frequencies"]))
        self.assertLessEqual(min(result["selected_frequencies"]), .27)
        self.assertGreaterEqual(max(result["selected_frequencies"]), .27)
        self.assertEqual([len(call.args[0]) for call in profiles.call_args_list],
                         [self.count, self.count // 2, self.count // 2])
        self.assertEqual(set(result["peaks_hz"]), {"joint", "low", "high", "first_half", "second_half"})
        self.assertLess(max(result["fft_dot_relative_errors"]), .003)
        self.assertTrue(result["gate_phase_validation_required"])
        self.assertFalse(result["gating_harmonics_modeled"])
        self.assertFalse(result["stochastic_channel_claim"])
        self.assertFalse(result["presence_is_formal_pilot_lock"])
        self.assertIn("not calibrated confidence", result["interval_kind"])
        json.dumps(result, allow_nan=False)
        # Additional samples cannot move the advertised observation end.
        longer = self.estimate(np.r_[noisy, noisy[:128]])
        self.assertEqual(longer["available_at"], result["available_at"])

    def test_modulation_rate_independent_of_ray_separation(self):
        np.testing.assert_allclose(helper.tone_offsets(0, .65, .65, modulation_rate_hz=.7),
                                   [-.7, -.07, 0, .07, .7])
        np.testing.assert_array_equal(helper.tone_offsets(8, .65, .65, modulation_rate_hz=8),
                                      helper.tone_offsets(8, .65, .65))
        np.testing.assert_array_equal(helper.tone_offsets(8, .65, .65, modulation_rate_hz=0), [0, 8])
        envelope = 1 + .65 * np.cos(2 * np.pi * .07 * self.time) + .65 * np.sin(2 * np.pi * .7 * self.time)
        rng = np.random.default_rng(715)
        noise = (rng.normal(size=self.count) + 1j * rng.normal(size=self.count)) / np.sqrt(2)
        result = self.estimate(self.signal() * envelope + noise, slow_depth=.65,
                               fast_depth=.65, modulation_rate_hz=.7)
        self.assertEqual(result['status'], 'narrowed', result['reason'])
        self.assertLess(abs(result['peaks_hz']['joint'] - .27), .01)
        self.assertEqual(len(result['tone_offsets_hz']), 5)
        for doppler, modulation in ((.005, .05), (.3, 3.)):
            self.assertEqual(np.count_nonzero(helper.tone_offsets(
                doppler, .65, .65, modulation_rate_hz=modulation) == 0), 1)
        for bad in (-1, float('nan')):
            with self.assertRaises(ValueError):
                helper.tone_offsets(0, .65, .65, modulation_rate_hz=bad)

    def test_eight_lines_and_full_exact_gram_match_direct_columns(self):
        offsets = helper.tone_offsets(8., .65, .65)
        np.testing.assert_allclose(offsets, [-8, -.8, 0, .8, 7.2, 8, 8.8, 16])
        self.assertEqual(len(helper.tone_offsets(8, 0, 0)), 2)
        self.assertEqual(len(helper.tone_offsets(8, .65, 0)), 6)
        self.assertEqual(len(helper.tone_offsets(8, 0, .65)), 4)
        np.testing.assert_array_equal(helper.tone_offsets(0, .65, .65), [0])
        frequencies = np.r_[-24.1 + offsets, 24.2 + offsets]
        count = 193
        t = np.arange(count) / self.rate
        design = np.exp(2j * np.pi * t[:, None] * frequencies) / np.sqrt(count)
        actual = helper.tone_gram(frequencies, count, self.rate)
        expected = design.conj().T @ design
        np.testing.assert_allclose(actual, expected, atol=4e-14, rtol=4e-14)
        self.assertGreater(np.max(np.abs(actual[:8, 8:])), 1e-4)
        rng = np.random.default_rng(902)
        values = rng.normal(size=count) + 1j * rng.normal(size=count)
        z = (design.conj().T @ values)[None, :]
        scores, rank = helper._projection_scores(z, frequencies, count, self.rate, 2.)
        fit = design @ np.linalg.lstsq(design, values, rcond=1e-10)[0]
        self.assertEqual(rank, 16)
        self.assertAlmostEqual(scores[0], np.vdot(fit, fit).real / 2., places=10)
        # Fractional bins exercise interpolation, including negative wrapping.
        grid = np.array([-.317, .027, .293])
        _, _, interpolated, spectrum = helper._profile(values, frequencies, grid, self.rate, 2.)
        self.assertEqual(spectrum.dtype, np.dtype("complex64"))
        direct = np.array([np.exp(-2j * np.pi * (frequencies + cfo)[:, None] * t) @ values
                           / np.sqrt(count) for cfo in grid])
        self.assertLess(np.max(np.abs(interpolated - direct)), .003)

    def test_noise_only_and_zero_fall_back(self):
        rng = np.random.default_rng(23)
        noise = (rng.normal(size=self.count) + 1j * rng.normal(size=self.count)) / np.sqrt(2)
        for values in (noise, np.zeros(self.count, dtype=complex)):
            self.assert_fallback(self.estimate(values), "insufficient_presence_diagnostic")

    def test_physical_hops_and_unknown_slot_phase_fall_back(self):
        self.assert_fallback(self.estimate(physical_rf_retuning=True), "physical_rf_retuning_unsupported")
        self.assert_fallback(self.estimate(pilot_phase_model="unknown-slot-phases"),
                             "unknown_or_noncontinuous_pilot_phase_model")

    def test_boundary_multimodal_flat_and_no_shrink(self):
        self.assert_fallback(self.estimate(self.signal(cfo=1)), "competitive_basin_at_prior_boundary")
        equal_peaks = self.signal(cfo=-.35, high_amplitude=.5) + self.signal(cfo=.35, high_amplitude=.5)
        multimodal = self.estimate(equal_peaks)
        self.assert_fallback(multimodal, "multiple_competitive_basins")
        self.assertGreater(len(multimodal["competitive_intervals_hz"]), 1)
        self.assert_fallback(self.estimate(self.signal(cfo=0), original_frequencies=[0.],
                                           prior_half_width_hz=0), "flat_profile", [0.])
        self.assert_fallback(self.estimate(original_frequencies=[-1., 1.]),
                             "no_bank_shrink", [-1., 1.])

    def test_pilot_and_half_crosschecks(self):
        only_low = self.signal(high_amplitude=0)
        self.assert_fallback(self.estimate(only_low), "crosscheck_insufficient_presence")
        differing_pilots = (self.signal(cfo=-.2, high_amplitude=0)
                            + self.signal(cfo=.2, low_amplitude=0))
        self.assert_fallback(self.estimate(differing_pilots), "pilot_or_half_peak_disagreement")
        differing_halves = self.signal(cfo=-.2)
        differing_halves[self.count // 2:] = .7 * self.signal(cfo=.2)[self.count // 2:]
        self.assert_fallback(self.estimate(differing_halves), "pilot_or_half_peak_disagreement")

    def test_direct_dot_guard(self):
        interpolate = helper._interpolate_fft
        with patch.object(helper, "_interpolate_fft", side_effect=lambda *a: 1.02 * interpolate(*a)):
            result = self.estimate()
        self.assert_fallback(result, "fft_direct_dot_disagreement")
        self.assertGreater(max(result["fft_dot_relative_errors"]), .003)

    def test_allowance_and_original_bank_neighbors(self):
        result = self.estimate(audio_error_bound_ppm=.2,
                               phase_diffusion_radians_per_sqrt_second=.0001,
                               drift_bound_hz_per_second=.00001)
        expected = (.01 + max(abs(f) for f in result["tone_frequencies_hz"]) * .2e-6
                    + 6 * .0001 / (2 * np.pi * 6) + .00001 * 36 + result["grid_step_hz"])
        self.assertAlmostEqual(result["allowance_hz"], expected, places=14)
        bank = np.array([.2, -.2, 0., .1, -.1])
        np.testing.assert_array_equal(helper._bank_subset(bank, [[-.01, .01]]), [0., .1, -.1])
        np.testing.assert_array_equal(helper._bank_subset(bank, [[.03, .07]]), [0., .1])
        np.testing.assert_array_equal(helper._bank_subset(bank, [[-.2, -.15], [.15, .2]]), [.2, -.2, .1, -.1])

    def test_grid_guard_without_coarsening(self):
        result = self.estimate(np.zeros(512 * 36, dtype=complex), sample_rate=512,
                               prior_half_width_hz=120)
        self.assert_fallback(result, "profile_grid_limit")
        self.assertEqual(result["profile_grid_hz"], [])

    def test_invalid_samples_and_public_bank(self):
        for values in (self.signal().real, self.signal()[:-1], self.signal().reshape(36, -1),
                       np.full(self.count, complex(float("nan"), 0)), np.array([], dtype=complex)):
            with self.subTest(shape=values.shape), self.assertRaises(ValueError):
                self.estimate(values)
        for bank in ([], [0, 0], [float("nan")], [float("inf")], [2.], [0j], [[0]], [True]):
            with self.subTest(bank=bank), self.assertRaises(ValueError):
                self.estimate(original_frequencies=bank)
        for changes in (dict(sample_rate=0), dict(noise_variance=0), dict(observation_seconds=35),
                        dict(phase_diffusion_radians_per_sqrt_second=-1), dict(doppler_hz=-1),
                        dict(slow_depth=-1), dict(fast_depth=float("nan")), dict(sample_rate=True),
                        dict(pilot_dwell_gating="yes"), dict(physical_rf_retuning=1),
                        dict(pilot_phase_model=None), dict(high_pilot_hz=525),
                        dict(high_pilot_hz=700), dict(first_sample_seconds=float("inf"))):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.estimate(**changes)


if __name__ == "__main__":
    unittest.main()

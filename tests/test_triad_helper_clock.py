#!/usr/bin/env python3
"""Independent sampled-line checks for coupled Helper/audio-clock inference."""
import pathlib
import sys
import unittest
from unittest.mock import patch

import numpy as np
from scipy import fft

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import triad_helper_assistance as helper


class JointClockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rate = 4096
        cls.count = cls.rate * 36
        cls.times = np.arange(cls.count) / cls.rate
        cls.centers = np.array([-1175., 675.])
        cls.offsets = helper.tone_offsets(90, .65, .65)
        cls.cfo, cls.clock, cls.scale = .27, -72., 1.000041
        rng = np.random.default_rng(17)
        cls.phases = rng.uniform(-np.pi, np.pi, 16)
        cls.noise = (rng.normal(size=cls.count) + 1j * rng.normal(size=cls.count)) / np.sqrt(2)
        cls.values = cls.signal(cls.cfo, cls.clock, cls.scale) + cls.noise
        cls.arguments = dict(
            sample_rate=cls.rate, data_if_hz=1725, low_pilot_hz=550,
            high_pilot_hz=2400, original_frequencies=np.linspace(-1, 1, 401),
            prior_half_width_hz=1, doppler_hz=90, slow_depth=.65, fast_depth=.65,
            noise_variance=1, first_sample_seconds=2.125,
            audio_error_bound_ppm=300.015, channel_rate_error_bound_ppm=150.008,
            phase_diffusion_radians_per_sqrt_second=0)
        cls.result = helper.estimate_frequency(cls.values, **cls.arguments)

    @classmethod
    def signal(cls, cfo, clock, scale, *, high=True):
        # Independent direct sampled sum: source frequencies and physical
        # propagation frequencies deliberately have different scale factors.
        frequencies = (cls.centers[:, None] * (1 + clock * 1e-6)
                       + cls.offsets[None, :] * scale).reshape(-1) + cfo
        result = np.zeros(cls.count, dtype=np.complex128)
        for i, frequency in enumerate(frequencies):
            if high or i < len(cls.offsets):
                result += .08 * np.exp(1j * (2 * np.pi * frequency * cls.times + cls.phases[i]))
        return result

    def test_independent_audio_and_propagation_scales_are_recovered(self):
        r = self.result
        self.assertEqual(r['status'], 'narrowed', r['reason'])
        self.assertEqual(r['clock_status'], 'narrowed')
        self.assertLess(abs(r['peaks_hz']['joint'] - self.cfo), .001)
        self.assertLess(abs(r['clock_estimate_ppm'] - self.clock), 1.)
        self.assertLess(abs(r['doppler_scale_estimate'] - self.scale), 2e-6)
        self.assertLessEqual(r['expanded_intervals_hz'][0][0], self.cfo)
        self.assertGreaterEqual(r['expanded_intervals_hz'][0][1], self.cfo)
        self.assertLessEqual(r['expanded_clock_intervals_ppm'][0][0], self.clock)
        self.assertGreaterEqual(r['expanded_clock_intervals_ppm'][0][1], self.clock)
        self.assertLess(r['selected_count'], 12)
        self.assertLess(max(r['fft_dot_relative_errors']), .003)
        self.assertFalse(r['joint_search_is_exhaustive'])
        self.assertFalse(r['doppler_scale_narrowing'])
        self.assertAlmostEqual(r['available_at'], 38.125)
        # The uncalibrated nuisance interval is intentionally not narrowed.
        self.assertEqual(r['expanded_doppler_scale_intervals'], [[1 - 150.008e-6, 1 + 150.008e-6]])
        self.assertGreaterEqual(r['joint_seed_count'], len(r['coarse_doppler_scales']))

    def test_joint_fit_dominates_independent_finite_3d_reference(self):
        spectrum = fft.fft(self.values.astype(np.complex64),
                           n=fft.next_fast_len(8 * self.count), workers=2)
        maximum = -np.inf
        # Full declared finite grid for this diagnostic. It is independent of
        # the proposal basins and includes the true clock/rate neighborhood.
        for cfo in np.linspace(.25, .29, 9):
            for clock in np.linspace(-90, -54, 9):
                for scale in (1.00002, 1.00004, 1.00006):
                    score = helper._joint_clock_score(
                        spectrum, self.centers, self.offsets, cfo, clock,
                        scale, self.count, self.rate, 1)[0]
                    maximum = max(maximum, score)
        self.assertGreaterEqual(self.result['presence_score'], maximum - 1e-5)
        # The wrong convention (propagation offsets scaled by TX/RX) loses
        # measurable likelihood in this intentionally separated-clock case.
        wrong = helper._joint_clock_score(
            spectrum, self.centers, self.offsets, self.cfo, self.clock,
            1 + self.clock * 1e-6, self.count, self.rate, 1)[0]
        self.assertGreater(self.result['presence_score'] - wrong, 1000)

    def test_exact_joint_gram_and_likelihood_match_direct_design(self):
        count = 731
        time = np.arange(count) / self.rate
        frequencies = (self.centers[:, None] * (1 + self.clock * 1e-6)
                       + self.offsets[None, :] * self.scale).ravel() + self.cfo
        design = np.exp(2j * np.pi * time[:, None] * frequencies) / np.sqrt(count)
        expected_gram = design.conj().T @ design
        np.testing.assert_allclose(helper.tone_gram(frequencies, count, self.rate),
                                   expected_gram, rtol=2e-12, atol=2e-12)
        # This direct projection is independent of the FFT interpolation.
        values = self.values[:count]
        z = (design.conj().T @ values)[None, :]
        score, rank = helper._projection_scores(z, frequencies, count, self.rate, 1)
        fitted = design @ np.linalg.lstsq(design, values, rcond=1e-10)[0]
        self.assertEqual(rank, 16)
        self.assertAlmostEqual(score[0], np.vdot(fitted, fitted).real, places=9)

    def test_absent_pilot_and_noise_do_not_narrow(self):
        for values in (self.noise, self.signal(self.cfo, self.clock, self.scale, high=False) + self.noise):
            with self.subTest(high_present=bool(np.max(np.abs(values)) > 1)):
                result = helper.estimate_frequency(values, **self.arguments)
                self.assertEqual(result['status'], 'fallback', result['reason'])
                self.assertEqual(result['clock_status'], 'fallback')
                self.assertEqual(result['selected_frequencies'], list(self.arguments['original_frequencies']))
                self.assertEqual(result['expanded_clock_intervals_ppm'], [[-300.015, 300.015]])

    def test_bounded_seed_failure_retains_full_banks(self):
        with patch.object(helper, 'MAX_JOINT_SEEDS', 0):
            result = helper.estimate_frequency(self.values, **self.arguments)
        self.assertEqual(result['reason'], 'joint_seed_limit')
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['clock_status'], 'fallback')

    def test_unresolved_sparse_comb_aliases_do_not_narrow(self):
        values = self.noise.copy()
        for center in self.centers:
            for offset in (0, 1):
                frequency = center * (1 + self.clock * 1e-6) + offset + self.cfo
                values += .2 * np.exp(2j * np.pi * frequency * self.times)
        result = helper.estimate_frequency(values, **(self.arguments | dict(
            doppler_hz=1, prior_half_width_hz=1.5,
            original_frequencies=np.linspace(-1.5, 1.5, 601))))
        self.assertEqual(result['status'], 'fallback', result['reason'])
        self.assertEqual(result['reason'], 'multiple_competitive_joint_basins')
        self.assertEqual(result['clock_status'], 'fallback')

    def test_unresolved_close_line_basis_is_explicit_fallback(self):
        result = helper.estimate_frequency(self.values, **(self.arguments | dict(doppler_hz=.001)))
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['reason'], 'joint_line_model_rank_deficient')

    def test_independent_half_search_rejects_frequency_step_sidelobe(self):
        values = 2 * self.signal(.27, self.clock, self.scale)
        second = self.signal(.37, self.clock, self.scale)
        values[self.count // 2:] = second[self.count // 2:]
        values += self.noise
        result = helper.estimate_frequency(values, **self.arguments)
        self.assertEqual(result['status'], 'fallback', result['reason'])
        self.assertEqual(result['reason'], 'pilot_or_half_peak_disagreement')
        self.assertTrue(result['crosschecks']['second_half']['independent_full_prior'])
        self.assertLess(abs(result['crosschecks']['second_half']['peak_hz'] - .37), .002)

    def test_unsuccessful_nonwinning_full_refinement_keeps_full_bank(self):
        minimize = helper.minimize
        calls = 0

        def fail_one(*args, **kwargs):
            nonlocal calls
            fitted = minimize(*args, **kwargs)
            calls += 1
            if calls == 1:
                fitted.success = False
            return fitted

        with patch.object(helper, 'minimize', side_effect=fail_one):
            result = helper.estimate_frequency(self.values, **self.arguments)
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['reason'], 'joint_refinement_failed')
        self.assertEqual(result['selected_frequencies'], list(self.arguments['original_frequencies']))

    def test_unsuccessful_half_refinement_cannot_validate_consistency(self):
        minimize = helper.minimize
        calls = 0

        def fail_half(*args, **kwargs):
            nonlocal calls
            fitted = minimize(*args, **kwargs)
            calls += 1
            if calls == self.result['joint_seed_count'] + 1:
                fitted.success = False
            return fitted

        with patch.object(helper, 'minimize', side_effect=fail_half):
            result = helper.estimate_frequency(self.values, **self.arguments)
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['reason'], 'joint_half_acquisition_joint_refinement_failed')

    def test_new_controls_validate_and_grid_fallback_is_explicit(self):
        for changes in (dict(fit_audio_clock='yes'), dict(channel_rate_error_bound_ppm=-1),
                        dict(channel_rate_error_bound_ppm=float('nan'))):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                helper.estimate_frequency(self.values, **(self.arguments | changes))
        result = helper.estimate_frequency(self.values, **(self.arguments | dict(prior_half_width_hz=120)))
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['reason'], 'profile_grid_limit')


if __name__ == '__main__':
    unittest.main()

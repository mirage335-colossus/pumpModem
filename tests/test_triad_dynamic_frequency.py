#!/usr/bin/env python3
"""Small independently synthesized spectral checks; no physical qualification."""
import copy
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_dynamic_frequency import estimate_dynamic_frequency, frequency_policy, _subset, _regions


def fixture(*, cfo=.7, phases=(.3, 2.2), arrival=.11, pilots=('low', 'high'), seed=17):
    fs, duration = 512, 42
    m = dict(sample_rate_hz=fs, helper_enabled=True,
        format=dict(physical_rf_retuning=False, occupied_bandwidth_hz=40.),
        channel=dict(spread_delay_ms=1., spread_doppler_hz=2.,
            nominal_conversion_if_hz=200., random_doppler_width_hz=.2,
            selective_slow_depth=0., selective_fast_depth=0., selective_modulation_rate_hz=.05,
            sample_clock_pair_ppm=0., endpoint_uniform_half_bound_ppm=0.,
            delay_excursion_seconds=0., delay_motion_doppler_bound_hz=0.,
            relative_phase_diffusion_rad_sqrt_s=0., spread_margin=1.),
        calendar=dict(capture_samples=fs*duration, capture_lead_seconds=1.,
            data_sample_offset=38*fs, arrival_prior_seconds=[-.3, .3],
            helper_start_seconds=0., helper_end_seconds=40.),
        helper_plan=dict(low_hz=100., high_hz=300., auxiliary_hz=160., pilot_dwell_gating=False))
    rng = np.random.default_rng(seed)
    t = np.arange(fs*duration)/fs-1-arrival
    y = .03*(rng.standard_normal(len(t))+1j*rng.standard_normal(len(t)))
    # Directly sampled Gaussian-frequency sums, independent from estimator FFT,
    # grid, widths and fitting routine. Separate pilot phases and ray mixtures.
    for pi, name in enumerate(('low', 'high')):
        if name not in pilots:
            continue
        for ray in (0., 2.):
            rates = rng.normal(0, .1, 35)
            gains = (rng.normal(size=35)+1j*rng.normal(size=35))/math.sqrt(70)
            fading = sum(gain*np.exp(2j*np.pi*rate*t) for gain, rate in zip(gains, rates))
            y += .8*fading*np.exp(1j*phases[pi]+2j*np.pi*(m['helper_plan'][name+'_hz']-200+cfo+ray)*t)*(t >= 0)
    return m, y, np.linspace(-6, 6, 121)


class DynamicFrequencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m, cls.y, cls.bank = fixture()
        cls.result = estimate_dynamic_frequency(cls.y, cls.m, cls.bank)

    def test_random_ray_spectrum_and_independent_phases(self):
        self.assertEqual(self.result['status'], 'narrowed', self.result['reason'])
        self.assertIn(.7, [round(x, 8) for x in self.result['selected_frequencies']])
        self.assertLess(self.result['selected_count'], len(self.bank)/2)
        self.assertEqual(self.result['clock_status'], 'unchanged')
        self.assertFalse(self.result['phase_reference_usable'])
        json.dumps(self.result, allow_nan=False)
        for phases, arrival in (((2.7, -.8), -.28), ((-2., .4), .29)):
            m, y, bank = fixture(phases=phases, arrival=arrival)
            result = estimate_dynamic_frequency(y, m, bank)
            self.assertEqual(result['status'], 'narrowed', result['reason'])
            self.assertTrue(any(a <= .7 <= b for a, b in result['expanded_intervals_hz']))

    def test_one_pilot_does_not_require_joint_phase(self):
        for pilot in ('low', 'high'):
            m, y, bank = fixture(pilots=(pilot,))
            result = estimate_dynamic_frequency(y, m, bank)
            self.assertEqual(result['status'], 'narrowed', result['reason'])
            self.assertEqual(result['usable_pilots'], [pilot])

    def test_weak_absent_disabled_and_erasure_fall_back(self):
        m, y, bank = fixture(pilots=())
        result = estimate_dynamic_frequency(y, m, bank)
        self.assertEqual(result['status'], 'fallback')
        self.assertEqual(result['selected_frequencies'], bank.tolist())
        m['helper_enabled'] = False
        self.assertEqual(estimate_dynamic_frequency(y, m, bank)['reason'], 'helper_disabled')
        m['helper_enabled'] = True
        valid = np.ones(len(y), np.uint8)
        valid[2000] = 0
        self.assertEqual(estimate_dynamic_frequency(y, m, bank, valid=valid)['reason'], 'known_erasure_in_helper_aperture')

    def test_support_gating_and_public_metadata(self):
        for key, value, reason in (('data_sample_offset', 20*512, 'insufficient_guaranteed_data_free_helper_support'),):
            m = copy.deepcopy(self.m)
            m['calendar'][key] = value
            self.assertEqual(estimate_dynamic_frequency(self.y, m, self.bank, config=dict(require_data_free=True))['reason'], reason)
        m = copy.deepcopy(self.m)
        m['helper_plan']['pilot_dwell_gating'] = True
        self.assertEqual(estimate_dynamic_frequency(self.y, m, self.bank)['reason'], 'gated_or_retuned_pilots_unsupported')
        m = copy.deepcopy(self.m)
        del m['channel']['sample_clock_pair_ppm']
        self.assertEqual(estimate_dynamic_frequency(self.y, m, self.bank)['reason'], 'public_uncertainty_metadata_missing')

    def test_future_horizon_and_clock_prior_widen_without_clock_narrowing(self):
        short = estimate_dynamic_frequency(self.y, self.m, self.bank,
            config=dict(future_horizon_seconds=0, drift_bound_hz_per_second=.01))
        long = estimate_dynamic_frequency(self.y, self.m, self.bank,
            config=dict(future_horizon_seconds=100, drift_bound_hz_per_second=.01))
        self.assertGreater(long['selected_count'], short['selected_count'])
        self.assertGreater(long['applicable_horizon'], short['applicable_horizon'])
        m = copy.deepcopy(self.m)
        m['channel']['sample_clock_pair_ppm'] = 4000
        m['channel']['endpoint_uniform_half_bound_ppm'] = 2000
        wide = estimate_dynamic_frequency(self.y, m, self.bank)
        self.assertEqual(wide['clock_status'], 'unchanged')
        self.assertGreater(wide['pilot_diagnostics']['low']['audio_clock_allowance_hz'], 0)

    def test_multimodal_regions_and_neighbor_preservation(self):
        grid = np.arange(-4., 5.)
        regions = _regions(grid, np.array([0, 9, 10, 0, 0, 0, 10, 9, 0.]), 1.)
        self.assertEqual(regions, [(-3., -2.), (2., 3.)])
        np.testing.assert_array_equal(_subset(np.array([4., -2., 0., 2., -4.]), [(-.1, .1)]), [-2., 0., 2.])

    def test_prior_boundary_and_broad_allowance_fall_back(self):
        m, y, bank = fixture(cfo=5.99)
        self.assertEqual(estimate_dynamic_frequency(y, m, bank)['status'], 'fallback')
        result = estimate_dynamic_frequency(self.y, self.m, self.bank, config=dict(model_allowance_hz=30))
        self.assertEqual(result['reason'], 'profile_or_allowance_too_broad')
        self.assertEqual(result['selected_frequencies'], self.bank.tolist())

    def test_data_overlap_is_explicit_and_disjoint_fit_remains_usable(self):
        m = copy.deepcopy(self.m)
        m['calendar']['data_sample_offset'] = 30*512
        result = estimate_dynamic_frequency(self.y, m, self.bank)
        self.assertTrue(result['overlaps_possible_data'])
        self.assertEqual(result['status'], 'narrowed', result['reason'])
        self.assertEqual(len(result['original_bank_nonconformity']), len(self.bank))
        self.assertLess(result['original_bank_nonconformity'][67], 18.)
        m['format']['occupied_bandwidth_hz'] = 400
        result = estimate_dynamic_frequency(self.y, m, self.bank)
        self.assertEqual(result['reason'], 'no_usable_pilot')
        self.assertTrue(all(d['reason'] == 'pilot_fit_window_overlaps_data_envelope' for d in result['pilot_diagnostics'].values()))

    def test_nonconformity_does_not_depend_on_cutoff_or_its_fallbacks(self):
        outputs = [estimate_dynamic_frequency(self.y, self.m, self.bank,
            config=dict(competitive_score_drop=drop)) for drop in (2., 18., 1e6)]
        for result in outputs[1:]:
            np.testing.assert_array_equal(result['original_bank_nonconformity'], outputs[0]['original_bank_nonconformity'])
        self.assertEqual(outputs[-1]['status'], 'fallback')

    def test_flat_presence_qualified_pilot_cannot_be_dropped_from_union(self):
        calls = []
        def profiles(power, frequencies, grid, separation, sigma, policy):
            calls.append(1)
            scores = (np.full(len(grid), 20.) if len(calls) == 1 else
                      50.-100*(grid-4.)**2)
            return scores, np.full(len(grid), 10.)
        with patch('triad_dynamic_frequency._whittle_profile', side_effect=profiles):
            result = estimate_dynamic_frequency(self.y, self.m, self.bank)
        self.assertEqual(len(calls), 2)
        self.assertEqual(result['reason'], 'flat_profile')
        self.assertEqual(result['selected_frequencies'], self.bank.tolist())
        self.assertEqual(result['original_bank_nonconformity'], [0.]*len(self.bank))

    def test_configuration_validation(self):
        with self.assertRaises(ValueError): frequency_policy(dict(unrecognized=1))
        with self.assertRaises(ValueError): frequency_policy(dict(observation_seconds=35))
        with self.assertRaises(ValueError): frequency_policy(dict(profile_iterations=True))
        with self.assertRaises(ValueError): estimate_dynamic_frequency(self.y, self.m, [0., 0.])


if __name__ == '__main__':
    unittest.main()

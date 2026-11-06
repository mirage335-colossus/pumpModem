#!/usr/bin/env python3
"""Cheap independent waveform checks for adaptive Helper window proposals."""
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_realtime_helper import estimate_helper, helper_policy


def fixture(arrival=.17, *, seed=51, amplitude=1., enabled=True):
    fs, seconds, lead = 512, 84, 8.
    m = dict(sample_rate_hz=fs, helper_enabled=enabled,
        format=dict(physical_rf_retuning=False, occupied_bandwidth_hz=40.),
        channel=dict(spread_delay_ms=1., spread_doppler_hz=2.,
            nominal_conversion_if_hz=200., random_doppler_width_hz=0.,
            selective_slow_depth=.65, selective_fast_depth=.65,
            selective_modulation_rate_hz=2., source_spectral_skirt_allowance_hz=200.,
            sample_clock_pair_ppm=20., endpoint_uniform_half_bound_ppm=10.,
            delay_excursion_seconds=0., delay_motion_doppler_bound_hz=0.,
            relative_phase_diffusion_rad_sqrt_s=0., spread_margin=1.),
        calendar=dict(capture_samples=fs*seconds, capture_lead_seconds=lead,
            data_sample_offset=37.866*fs, arrival_prior_seconds=[-7.035, 7.035],
            helper_start_seconds=0., helper_end_seconds=61.6,
            fixed_capture_tail_bound_seconds=.035),
        helper_plan=dict(low_hz=100., high_hz=300., auxiliary_hz=160., pilot_dwell_gating=False))
    rng = np.random.default_rng(seed)
    t = np.arange(fs*seconds)/fs-lead
    y = .3*(rng.standard_normal(len(t))+1j*rng.standard_normal(len(t)))
    # Independent public waveform, unequal ray strengths and pilot phases.
    for center, phase in ((-100., .7), (100., -2.4)):
        signal = (np.exp(2j*np.pi*(center+.7)*t+1j*phase)+
                  .7*np.exp(2j*np.pi*(center+2.7)*t-1.4j*phase))
        y += amplitude*signal*((t >= arrival) & (t < arrival+61.6))
    return m, y, np.linspace(-6, 6, 121)


class RealtimeHelperTests(unittest.TestCase):
    def test_broad_prior_uses_observed_onset_without_mutating_manifest(self):
        m, y, bank = fixture()
        original = copy.deepcopy(m)
        result = estimate_helper(y, m, bank)
        self.assertEqual(result['status'], 'narrowed', result['reason'])
        self.assertEqual(m, original)
        self.assertEqual(result['original_arrival_prior_seconds'], [-7.035, 7.035])
        self.assertTrue(any(lo <= .17 <= hi for lo, hi in result['onset_intervals_seconds']))
        frequency = result['frequency']
        self.assertFalse(frequency['overlaps_possible_data'])
        self.assertGreaterEqual(frequency['observation_seconds'], 36.)
        self.assertTrue(any(lo <= .7 <= hi for lo, hi in result['expanded_intervals_hz']))
        self.assertFalse(result['phase_reference_usable'])
        self.assertEqual(result['clock_status'], 'unchanged')
        self.assertEqual(result['policy']['frequency_config']['modulation_width_allowance'], 0.)
        self.assertGreater(result['timing']['total_cpu_seconds'], 0.)
        json.dumps(result, allow_nan=False)

    def test_onsets_near_prior_edges_keep_truth_in_engineering_region(self):
        for arrival in (-7.03, 7.03):
            m, y, bank = fixture(arrival, seed=52)
            result = estimate_helper(y, m, bank)
            self.assertEqual(result['status'], 'narrowed', result['reason'])
            self.assertTrue(any(lo <= arrival <= hi for lo, hi in result['onset_intervals_seconds']))

    def test_absent_disabled_and_missing_metadata_fall_back(self):
        m, y, bank = fixture(amplitude=0.)
        result = estimate_helper(y, m, bank)
        self.assertEqual(result['reason'], 'weak_or_absent_helper_onset')
        self.assertIsNone(result['frequency'])
        self.assertEqual(result['selected_frequencies'], bank.tolist())
        m['helper_enabled'] = False
        self.assertEqual(estimate_helper(y, m, bank)['reason'], 'helper_disabled')
        m['helper_enabled'] = True
        del m['channel']['sample_clock_pair_ppm']
        self.assertEqual(estimate_helper(y, m, bank)['reason'], 'public_uncertainty_metadata_missing')

    def test_known_erasure_and_insufficient_support_fall_back(self):
        m, y, bank = fixture()
        mask = np.ones(len(y), bool)
        mask[10] = False
        self.assertEqual(estimate_helper(y, m, bank, valid=mask)['reason'], 'known_erasure_in_level_intervals')
        mask[:] = True
        mask[int(8*512)] = False
        self.assertEqual(estimate_helper(y, m, bank, valid=mask)['reason'], 'known_erasure_in_onset_search')
        m['calendar']['data_sample_offset'] = 8*512
        self.assertEqual(estimate_helper(y, m, bank)['reason'], 'insufficient_public_level_support')

    def test_ambiguous_onset_is_not_silently_collapsed(self):
        m, y, bank = fixture()
        result = estimate_helper(y, m, bank, config=dict(onset_score_drop=1e9))
        self.assertEqual(result['reason'], 'ambiguous_helper_onset')
        self.assertIsNone(result['frequency'])
        self.assertEqual(result['selected_frequencies'], bank.tolist())

    def test_complete_procedure_identity_ignores_data_truth(self):
        m, y, bank = fixture()
        with patch('triad_realtime_helper.estimate_dynamic_frequency', return_value=dict(status='fallback', reason='synthetic')):
            a = estimate_helper(y, m, bank)
            m.update(data_enabled=False, evaluator_truth_file='do-not-read.json')
            b = estimate_helper(y, m, bank)
            c = estimate_helper(y, m, bank, config=dict(onset_padding_seconds=.2))
        self.assertEqual(a['procedure_id'], b['procedure_id'])
        self.assertNotEqual(a['procedure_id'], c['procedure_id'])
        self.assertEqual(a['reason'], 'frequency_synthetic')

    def test_invalid_inputs_are_explicit(self):
        for config in (dict(bogus=1), dict(onset_step_seconds=0), dict(maximum_samples=True),
                       dict(frequency_config=dict(require_data_free=False))):
            with self.assertRaises(ValueError):
                helper_policy(config)
        m, y, bank = fixture()
        with self.assertRaises(ValueError): estimate_helper(y.real, m, bank)
        with self.assertRaises(ValueError): estimate_helper(y, m, [0., 0.])
        y[0] = np.nan
        with self.assertRaises(ValueError): estimate_helper(y, m, bank)


if __name__ == '__main__':
    unittest.main()

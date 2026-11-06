#!/usr/bin/env python3
"""Cheap numerical/guard tests, separate from sampled-channel qualification."""
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_realtime_helper import estimate_helper
from triad_weak_helper import estimate_weak_helper, weak_helper_policy


def fixture(arrival=.17, *, amplitude=.11, seed=30):
    fs,lead=512,8.
    m=dict(sample_rate_hz=fs, helper_enabled=True,
        format=dict(physical_rf_retuning=False,occupied_bandwidth_hz=40.),
        channel=dict(spread_delay_ms=1.,spread_doppler_hz=20.,
            nominal_conversion_if_hz=200.,random_doppler_width_hz=0.,
            selective_slow_depth=.65,selective_fast_depth=.65,
            selective_modulation_rate_hz=20.,source_spectral_skirt_allowance_hz=200.,
            sample_clock_pair_ppm=20.,endpoint_uniform_half_bound_ppm=10.,
            delay_excursion_seconds=0.,delay_motion_doppler_bound_hz=0.,
            relative_phase_diffusion_rad_sqrt_s=0.,spread_margin=1.),
        calendar=dict(capture_samples=fs*84,capture_lead_seconds=lead,
            data_sample_offset=37.866*fs,arrival_prior_seconds=[-7.035,7.035],
            helper_start_seconds=0.,helper_end_seconds=61.6,
            fixed_capture_tail_bound_seconds=.035),
        helper_plan=dict(low_hz=100.,high_hz=300.,auxiliary_hz=160.,pilot_dwell_gating=False))
    rng=np.random.default_rng(seed);t=np.arange(fs*84)/fs-lead
    y=.4*(rng.normal(size=len(t))+1j*rng.normal(size=len(t)))
    for center,phase in ((-100.,.7),(100.,-2.4)):
        wave=np.exp(2j*np.pi*(center+.7131)*t+1j*phase)+.7*np.exp(2j*np.pi*(center+20.7131)*t-1.4j*phase)
        y+=amplitude*wave*((t>=arrival)&(t<arrival+61.6))
    return m,y,np.linspace(-6,6,121)


class WeakHelperTests(unittest.TestCase):
    def test_narrow_pilots_recover_onset_below_broadband_guard(self):
        m,y,bank=fixture();before=copy.deepcopy(m)
        old=estimate_helper(y,m,bank)
        self.assertEqual(old['reason'],'weak_or_absent_helper_onset')
        r=estimate_weak_helper(y,m,bank)
        self.assertEqual(r['status'],'narrowed',r['reason'])
        self.assertTrue(any(a<=.17<=b for a,b in r['onset_intervals_seconds']))
        self.assertTrue(any(a<=.7131<=b for a,b in r['expanded_intervals_hz']))
        self.assertEqual(r['onset']['method'],'independent persistent pilot/ray complex means')
        self.assertEqual(m,before)
        self.assertFalse(r['frequency']['overlaps_possible_data'])
        self.assertGreaterEqual(r['frequency']['observation_seconds'],36.)
        self.assertFalse(r['phase_reference_usable'])
        self.assertEqual(r['clock_status'],'unchanged')
        self.assertTrue(all(x['success'] for x in r['coarse_frequency']['core_refinements']))
        json.dumps(r,allow_nan=False)

    def test_boundary_and_independent_pilot_phases(self):
        for arrival in (-7.03,7.03):
            m,y,bank=fixture(arrival,amplitude=.25,seed=31)
            r=estimate_weak_helper(y,m,bank)
            self.assertEqual(r['status'],'narrowed',r['reason'])
            self.assertTrue(any(a<=arrival<=b for a,b in r['onset_intervals_seconds']))

    def test_renewing_public_law_does_not_extrapolate_complex_mean(self):
        m,y,bank=fixture(amplitude=.4)
        m['channel']['random_doppler_width_hz']=.5
        r=estimate_weak_helper(y,m,bank)
        self.assertEqual(r['status'],'narrowed',r['reason'])
        self.assertEqual(r['onset']['method'],'renewing-ray filtered energy change point')
        self.assertNotIn('core_refinements',r['coarse_frequency'])

    def test_noise_and_disabled_helper_do_not_claim_acquisition(self):
        m,y,bank=fixture(amplitude=0.)
        r=estimate_weak_helper(y,m,bank)
        self.assertEqual(r['status'],'fallback')
        self.assertIsNone(r['frequency'])
        self.assertEqual(r['selected_frequencies'],bank.tolist())
        m['helper_enabled']=False
        self.assertEqual(estimate_weak_helper(y,m,bank)['reason'],'helper_disabled')

    def test_erasures_gating_and_short_lead_are_explicit(self):
        m,y,bank=fixture();valid=np.ones(len(y),bool);valid[20]=False
        self.assertEqual(estimate_weak_helper(y,m,bank,valid=valid)['reason'],'known_erasure_in_helper_support')
        m['helper_plan']['pilot_dwell_gating']=True
        self.assertEqual(estimate_weak_helper(y,m,bank)['reason'],'gated_or_retuned_pilots_unsupported')
        m['helper_plan']['pilot_dwell_gating']=False;m['calendar']['data_sample_offset']=10*512
        self.assertEqual(estimate_weak_helper(y,m,bank)['reason'],'insufficient_guaranteed_coarse_support')

    def test_identity_does_not_read_truth_or_control_label(self):
        m,y,bank=fixture(amplitude=0.)
        a=estimate_weak_helper(y,m,bank)
        m.update(data_enabled=False,evaluator_truth_file='must-not-open.json')
        b=estimate_weak_helper(y,m,bank)
        self.assertEqual(a['procedure_id'],b['procedure_id'])
        c=estimate_weak_helper(y,m,bank,config=dict(onset_padding_seconds=.2))
        self.assertNotEqual(a['procedure_id'],c['procedure_id'])

    def test_fit_failure_and_policy_validation(self):
        m,y,bank=fixture(amplitude=.3)
        with patch('triad_weak_helper.optimize.minimize_scalar') as fit:
            fit.return_value.x=0.;fit.return_value.fun=-1.;fit.return_value.success=False;fit.return_value.nfev=24
            r=estimate_weak_helper(y,m,bank)
            self.assertEqual(r['reason'],'coarse_frequency_refinement_failed')
        for config in ({'unknown':1},{'maximum_samples':True},{'onset_step_seconds':0},
                       {'frequency_config':{'require_data_free':False}}):
            with self.assertRaises(ValueError):weak_helper_policy(config)
        with self.assertRaises(ValueError):estimate_weak_helper(y.real,m,bank)
        with self.assertRaises(ValueError):estimate_weak_helper(y,m,[0.,0.])

    def test_all_onset_apertures_are_unioned_or_fallback_is_preserved(self):
        m,y,bank=fixture(amplitude=.3)
        seen=[]
        def estimate(raw,conditioned,frequencies,**kwargs):
            bounds=conditioned['calendar']['arrival_prior_seconds'];seen.append(bounds)
            self.assertLessEqual(bounds[1]-bounds[0],1.25+1e-12)
            self.assertTrue(kwargs['config']['require_data_free'])
            return dict(status='narrowed',selected_frequencies=[float(len(seen))],
                selected_count=1,expanded_intervals_hz=[[len(seen)-.1,len(seen)+.1]],
                observation_seconds=36.)
        with patch('triad_weak_helper.estimate_dynamic_frequency',side_effect=estimate):
            r=estimate_weak_helper(y,m,bank,config=dict(onset_padding_seconds=1.3))
        self.assertGreater(len(seen),1)
        self.assertEqual(r['selected_count'],len(seen))
        self.assertEqual(seen[0][0],r['onset']['padded_arrival_hull_seconds'][0])
        self.assertEqual(seen[-1][1],r['onset']['padded_arrival_hull_seconds'][1])
        self.assertIsNone(r['frequency']['overlaps_possible_data'])
        with patch('triad_weak_helper.estimate_dynamic_frequency',return_value=dict(status='fallback',reason='test')):
            r=estimate_weak_helper(y,m,bank,config=dict(onset_padding_seconds=1.3))
        self.assertEqual(r['reason'],'onset_branch_frequency_fallback')
        self.assertEqual(r['selected_frequencies'],bank.tolist())
        r=estimate_weak_helper(y,m,bank,config=dict(onset_padding_seconds=1.3,maximum_onset_apertures=1))
        self.assertEqual(r['reason'],'onset_aperture_work_limit')


if __name__=='__main__':unittest.main()

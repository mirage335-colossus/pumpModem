"""Cheap model/selection contracts; no sampled receiver or hardware campaign."""
import copy
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import numpy as np
from scipy.stats import ncx2
from triad_duration_model import (effective_modes, gaussian_prediction,
                                  deterministic_prediction, plan_from_native)


def fixture(stochastic=True):
    return dict(geometry_feasible=True, format=dict(tile_seconds=11.6,
        burst_wait_tiles=0, inner_coefficients=66, tile_count=1, fhss=False),
        physics=dict(spread_margin=3., deterministic_cfo_bound_hz=30.174,
            sample_clock_pair_ppm=100.005, arrival_prior_seconds=[-7.035, 7.035],
            channel_evolution='fixed-time-gaussian-v1' if stochastic else 'legacy',
            random_doppler_width_hz=.5 if stochastic else 0,
            relative_phase_diffusion_rad_sqrt_s=.00877, spread_delay_ms=21.,
            spread_doppler_hz=90., selective_enabled=True, delay_excursion_seconds=2e-7,
            data_w=4., effective_helper_w=40.),
        export_constraints=dict(fixed_helper_lead_seconds=38.))


class DurationModelTests(unittest.TestCase):
    def test_finite_and_asymptotic_modes(self):
        self.assertAlmostEqual(effective_modes(1e8, 0, 0, 2), 2)
        self.assertAlmostEqual(effective_modes(.001, .5, 0), 1, places=5)
        self.assertAlmostEqual(effective_modes(1e4, .5, 0, 2)/(1e4*math.sqrt(math.pi)), 1, places=4)
        self.assertGreater(effective_modes(100, .5, .5), effective_modes(100, .5, 0))

    def test_tail_bounds_against_exact_distributions(self):
        for energy in (20., 100., 300.):
            for modes in (1., 2.5, 40.):
                p = gaussian_prediction(energy, modes, 20.)
                self.assertLessEqual(p['modeled_detection_lower_bound'], p['modeled_detection_probability']+1e-12)
            for rank in (1, 2):
                p = deterministic_prediction(energy, rank, 20.)
                lam = energy/rank
                x = (20+rank*math.log1p(lam))/(lam/(1+lam))
                exact = ncx2.sf(2*x, 2*rank, 2*energy)
                self.assertLessEqual(p['modeled_detection_lower_bound'], exact+1e-12)

    def test_target_drives_native_tiles_not_fixed_duration(self):
        plans = [plan_from_native(fixture(), cn) for cn in (20., 8., 5., 0.)]
        self.assertEqual([p['active_seconds'] for p in plans], sorted(p['active_seconds'] for p in plans))
        for p in plans:
            self.assertEqual(p['status'], 'provisional_model_plan')
            self.assertFalse(p['correct_probability_qualified'])
            self.assertFalse(p['physical_null_qualified'])
            self.assertTrue(p['selected']['meets_detection_allocation'])
            if p['previous_legal']:
                self.assertFalse(p['previous_legal']['meets_detection_allocation'])
            self.assertLess(p['evaluation_count'], 20)

    def test_error_budget_and_outages_increase_duration(self):
        base = plan_from_native(fixture(), 8.)
        for opts in [dict(message_bits=22), dict(alpha_per_day=1e-8),
                     dict(outage_fraction=.5), dict(retained_fraction=.5),
                     dict(duration_alternatives=100)]:
            p = plan_from_native(fixture(), 8., **opts)
            self.assertGreaterEqual(p['active_seconds'], base['active_seconds'])
        self.assertEqual(base['operational_budget']['attempts_per_24h_bound'], 216001)

    def test_no_helper_pruning_discount_and_manual_shortfall(self):
        native = fixture()
        native['helper_frequency_candidates'] = 1
        p = plan_from_native(native, 8., manual_seconds=12.)
        self.assertEqual(p['active_seconds'], 23.2)
        self.assertFalse(p['selected']['meets_detection_allocation'])
        self.assertEqual(p['full_grid_count'], plan_from_native(fixture(), 8.)['full_grid_count'])
        self.assertEqual(p['status'], 'manual_duration_override')

    def test_static_fading_and_weak_scaling(self):
        n = fixture(); n['physics'].update(random_doppler_width_hz=0., relative_phase_diffusion_rad_sqrt_s=0.)
        p = plan_from_native(n, 8.)
        self.assertGreater(p['active_seconds'], plan_from_native(fixture(), 8.)['active_seconds'])
        # Fixed coherence's low per-mode-SNR regime approaches inverse-square cost.
        a = plan_from_native(fixture(), -6.)['active_seconds']
        b = plan_from_native(fixture(), -9.)['active_seconds']
        self.assertGreater(b/a, 3.)
        self.assertLess(b/a, 4.5)

    def test_phase_uncertainty_does_not_invent_rayleigh_outage(self):
        n = fixture(False)
        n['physics'].update(relative_phase_diffusion_rad_sqrt_s=.01)
        p = plan_from_native(n, -10.)
        self.assertIn('fixed actual energy', p['selected']['model'])

    def test_limits_and_burst_energy_accounting(self):
        p = plan_from_native(fixture(), -60.)
        self.assertEqual(p['status'], 'duration_limit')
        self.assertIsNone(p['active_seconds'])
        n = fixture(); n['format']['burst_wait_tiles'] = 3
        p = plan_from_native(n, 8.)
        self.assertGreater(p['calendar']['elapsed_data_seconds'], p['active_seconds'])
        self.assertAlmostEqual(p['selected']['energy_n0'], 10**.8*p['active_seconds'])
        n['physics']['effective_helper_w'] = 0
        p = plan_from_native(n, 8.)
        self.assertEqual(p['status'], 'receiver_unavailable')
        self.assertFalse(p['export_supported'])

    def test_extreme_finite_arithmetic_and_invalid_inputs(self):
        for cn in (-60., 200.):
            p = plan_from_native(fixture(), cn)
            self.assertTrue(math.isfinite(p['ideal_known_mean_energy_n0']))
        for opts in [dict(correct_probability=1.), dict(message_bits=True),
                     dict(outage_fraction=1.), dict(retained_fraction=0.),
                     dict(max_active_seconds=math.inf)]:
            with self.assertRaises(ValueError): plan_from_native(fixture(), 8., **opts)

    def test_unavailable_helper_and_placement_are_not_exportable(self):
        n=fixture();n['helper_enabled']=False
        p=plan_from_native(n,8.)
        self.assertEqual(p['status'],'receiver_unavailable')
        self.assertFalse(p['export_supported'])
        n=fixture();n['helper_placement']=dict(feasible_at_requested_if=False)
        p=plan_from_native(n,8.)
        self.assertEqual(p['status'],'geometry_unavailable')
        self.assertFalse(p['export_supported'])

    def test_message_clock_widening_grows_with_elapsed_calendar(self):
        a=plan_from_native(fixture(),8.,message_bits=22)
        n=fixture();n['format']['burst_wait_tiles']=3
        b=plan_from_native(n,8.,message_bits=22)
        self.assertGreater(b['full_grid_count'],a['full_grid_count'])
        self.assertGreater(b['origin_shift_allowance_seconds'],a['origin_shift_allowance_seconds'])


if __name__ == '__main__':
    unittest.main()

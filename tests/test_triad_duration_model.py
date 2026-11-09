"""Cheap model/selection contracts; no sampled receiver or hardware campaign."""
import copy
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import numpy as np
from scipy.stats import ncx2, norm
from triad_duration_model import (effective_modes, gaussian_prediction,
                                  deterministic_prediction, plan_from_native)
from triad_operational_admission import DEFAULT_COHERENCES, PublicGrid, operational_threshold, public_grid


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
        export_constraints=dict(fixed_helper_lead_seconds=38., sample_rate_hz=6000))


def complete_receipt(native=None, *, residual_states=3, frequency_step=.01):
    native = fixture() if native is None else native
    grid = public_grid(native['physics'], native['physics']['arrival_prior_seconds'], tuple(range(-6, 7)))
    if frequency_step != grid.frequency_step:
        grid = PublicGrid(grid.epochs, grid.arrival_lower, grid.arrival_upper, grid.arrival_step,
                          grid.frequency_bound, frequency_step, grid.clock_bound, grid.clock_intervals)
    return dict(schema='triad-complete-admission-model-v1',
        operational_evidence='fixed_initial_affine', full_grid=grid.__dict__,
        full_grid_identity=grid.identity(), full_grid_count=grid.candidate_count,
        operational_budget=operational_threshold(), model_count=len(DEFAULT_COHERENCES),
        residual_state_count=residual_states, affine_lane_weight=1.,
        public_slot_indices=list(range(24)))


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
        self.assertFalse(p['stream_geometry_supported'])

    def test_extreme_finite_arithmetic_and_invalid_inputs(self):
        ordinary = plan_from_native(fixture(), 8.)
        old_maximum_tests = (2*ordinary['full_grid_count']*ordinary['model_count']*
                             ordinary['operational_budget']['attempts_per_24h_bound']*
                             ordinary['operational_budget']['duration_alternatives'])
        old_ideal = .5*(norm.isf(ordinary['operational_budget']['alpha_per_24h']/old_maximum_tests)+
                        norm.isf(ordinary['per_bit_detection_miss_budget']))**2
        self.assertAlmostEqual(ordinary['ideal_known_mean_energy_n0'], old_ideal)
        for cn in (-60., 200.):
            p = plan_from_native(fixture(), cn)
            self.assertTrue(math.isfinite(p['ideal_known_mean_energy_n0']))
        for opts in [dict(correct_probability=1.), dict(message_bits=True),
                     dict(outage_fraction=1.), dict(retained_fraction=0.),
                     dict(max_active_seconds=math.inf)]:
            with self.assertRaises(ValueError): plan_from_native(fixture(), 8., **opts)

    def test_unavailable_helper_and_placement_are_not_streamable(self):
        n=fixture();n['helper_enabled']=False
        p=plan_from_native(n,8.)
        self.assertEqual(p['status'],'receiver_unavailable')
        self.assertFalse(p['stream_geometry_supported'])
        n=fixture();n['helper_placement']=dict(feasible_at_requested_if=False)
        p=plan_from_native(n,8.)
        self.assertEqual(p['status'],'geometry_unavailable')
        self.assertFalse(p['stream_geometry_supported'])

    def test_message_clock_widening_grows_with_elapsed_calendar(self):
        a=plan_from_native(fixture(),8.,message_bits=22)
        n=fixture();n['format']['burst_wait_tiles']=3
        b=plan_from_native(n,8.,message_bits=22)
        self.assertGreater(b['full_grid_count'],a['full_grid_count'])
        self.assertGreater(b['origin_shift_allowance_seconds'],a['origin_shift_allowance_seconds'])

    def test_multi_day_and_multi_year_manual_forecasts_stay_bounded(self):
        horizons = (3600., 86400., 38*86400., 365.25*86400., 10*365.25*86400.-11.6)
        baseline = plan_from_native(fixture(), 8., manual_seconds=11.6)
        for seconds in horizons:
            with self.subTest(seconds=seconds):
                p = plan_from_native(fixture(), 8., manual_seconds=seconds)
                d = p['long_span_diagnostics']
                self.assertEqual(p['status'], 'manual_duration_override')
                self.assertGreaterEqual(d['selected_active_seconds'], seconds)
                self.assertEqual(d['current_receiver_long_span_qualified'], False)
                self.assertEqual(d['current_search_grid']['frequency_step_hz'], .01)
                self.assertEqual(d['full_coherent_design_reference']['frequency_requirements'][-1]
                                 ['coherent_window_seconds'],d['selected_elapsed_seconds'])
                self.assertEqual(d['whole_message_days_charged'],
                                 math.ceil(d['admission_accounting_horizon_seconds']/86400))
                self.assertGreaterEqual(d['admission_accounting_horizon_seconds'],d['whole_message_elapsed_seconds'])
                self.assertEqual(d['whole_message_conditional_null_union_bound'],
                                 min(1.,d['whole_message_days_charged']*1e-4))
                self.assertEqual(p['full_grid_count'],baseline['full_grid_count'])
                self.assertLessEqual(p['evaluation_count'],4)
                self.assertTrue(math.isfinite(d['modeled_active_data_energy_n0']))
        longest = plan_from_native(fixture(),8.,manual_seconds=horizons[-1])['long_span_diagnostics']
        day = plan_from_native(fixture(),8.,manual_seconds=horizons[2])['long_span_diagnostics']
        self.assertGreater(longest['full_coherent_design_reference']['frequency_requirements'][-1]
                           ['required_points_across_current_bound'],
                           day['full_coherent_design_reference']['frequency_requirements'][-1]
                           ['required_points_across_current_bound'])

    def test_frequency_and_clock_design_counts_are_descriptive_only(self):
        native=fixture();native['format']['fine_chip_rate_hz']=60.
        p = plan_from_native(native,8.,manual_seconds=86400.)
        d = p['long_span_diagnostics']
        coherent = d['full_coherent_design_reference']['frequency_requirements'][-1]
        expected_step = 1/(4*d['selected_elapsed_seconds'])
        self.assertAlmostEqual(coherent['required_step_hz'],expected_step)
        self.assertGreater(coherent['maximum_rectangular_bin_mismatch_loss_db'],.224)
        self.assertLess(coherent['maximum_rectangular_bin_mismatch_loss_db'],.225)
        self.assertEqual(coherent['required_points_across_current_bound'],
                         math.ceil(2*d['current_search_grid']['frequency_bound_hz']/expected_step)+1)
        clock=d['full_coherent_design_reference']['clock_requirement']
        self.assertEqual(clock['status'],'design_reference')
        self.assertEqual(clock['required_points_across_current_bound'],
                         math.ceil(2*d['current_search_grid']['clock_bound_ppm'] /
                                   clock['required_step_ppm'])+1)
        self.assertIn('not added to that grid',d['receiver_search_scope'])
        self.assertFalse(d['current_search_grid']['full_span_frequency_resolution_meets_design_reference'])
        self.assertFalse(d['current_search_grid']['clock_resolution_meets_design_reference'])
        self.assertFalse(d['full_span_grid_resolution_compatible'])
        self.assertFalse(p['correct_probability_qualified'])
        self.assertFalse(p['physical_null_qualified'])

    def test_null_exposure_includes_per_bit_helper_and_alignment_allowances(self):
        d=plan_from_native(fixture(),8.,manual_seconds=86350.,message_bits=2)['long_span_diagnostics']
        self.assertEqual(math.ceil(d['whole_message_elapsed_seconds']/86400.),2)
        self.assertEqual(d['whole_message_days_charged'],3)
        self.assertAlmostEqual(d['whole_message_conditional_null_union_bound'],3e-4)

    def test_zero_offset_grid_meets_resolution_reference(self):
        native=fixture();native['format']['fine_chip_rate_hz']=60.
        native['physics'].update(deterministic_cfo_bound_hz=0.,sample_clock_pair_ppm=0.)
        d=plan_from_native(native,8.,manual_seconds=86400.)['long_span_diagnostics']
        self.assertTrue(d['current_search_grid']['full_span_frequency_resolution_meets_design_reference'])
        self.assertTrue(d['current_search_grid']['clock_resolution_meets_design_reference'])
        self.assertTrue(d['full_span_grid_resolution_compatible'])

    def test_missing_native_fine_rate_does_not_invent_clock_resolution(self):
        native=fixture();p=plan_from_native(native,8.,manual_seconds=86400.)
        clock=p['long_span_diagnostics']['full_coherent_design_reference']['clock_requirement']
        self.assertEqual(clock['status'],'unavailable')
        self.assertIn('fine_chip_rate_hz',clock['reason'])

    def test_diagnostic_fields_do_not_change_existing_selected_tiles(self):
        for cn,expected in ((20.,11.6),(8.,46.4),(5.,92.8)):
            p=plan_from_native(fixture(),cn)
            self.assertAlmostEqual(p['active_seconds'],expected)
            self.assertFalse(p['long_span_diagnostics']['current_receiver_long_span_qualified'])
            self.assertEqual(p['forecast_horizon']['configured_max_active_seconds'],315576000.)

    def test_complete_receipt_uses_matched_fixed_grid_and_exact_prior_penalty(self):
        native = fixture()
        receipt = complete_receipt(native)
        result = plan_from_native(native, 8., manual_seconds=11.6, complete_admission=receipt)
        n, m, r = receipt['full_grid_count'], receipt['model_count'], receipt['residual_state_count']
        expected = receipt['operational_budget']['log_e_threshold'] + math.log(2*n*m*r)
        self.assertEqual(result['full_grid'], receipt['full_grid'])
        self.assertAlmostEqual(result['true_atom_log_lr_threshold'], expected)
        self.assertAlmostEqual(result['selected']['true_atom_log_lr_threshold'], expected)
        self.assertAlmostEqual(result['residual_state_prior'], 1/r)
        self.assertEqual(result['complete_admission_receipt'], receipt)
        self.assertTrue(any('no adaptive half-prior' in assumption for assumption in result['assumptions']))
        maximum_tests = math.exp(receipt['operational_budget']['log_e_threshold'])*2*n*m*r
        beta = (1-result['requested_correct_probability'])/(2*result['message_bits'])
        ideal = .5*(norm.isf(1/maximum_tests)+norm.isf(beta))**2
        self.assertAlmostEqual(result['ideal_reference_maximum_tests']/maximum_tests, 1.)
        self.assertAlmostEqual(result['ideal_known_mean_energy_n0'], ideal)

    def test_finer_frequency_grid_and_more_residual_states_raise_threshold(self):
        native = fixture()
        base = complete_receipt(native)
        fine = complete_receipt(native, frequency_step=.001)
        more_residuals = complete_receipt(native, residual_states=6)
        base_result = plan_from_native(native, 8., manual_seconds=11.6, complete_admission=base)
        fine_result = plan_from_native(native, 8., manual_seconds=11.6, complete_admission=fine)
        residual_result = plan_from_native(native, 8., manual_seconds=11.6, complete_admission=more_residuals)
        self.assertGreater(fine['full_grid_count'], base['full_grid_count'])
        self.assertGreater(fine_result['true_atom_log_lr_threshold'], base_result['true_atom_log_lr_threshold'])
        self.assertAlmostEqual(residual_result['true_atom_log_lr_threshold']-base_result['true_atom_log_lr_threshold'],
                               math.log(2))

    def test_complete_receipts_are_fixed_duration_only_and_strictly_validated(self):
        native = fixture()
        receipt = complete_receipt(native)
        with self.assertRaisesRegex(ValueError, 'fixed manual durations'):
            plan_from_native(native, 8., complete_admission=receipt)
        invalid = []
        for field, value in (
            ('operational_evidence', 'adaptive'), ('full_grid_identity', 'bad'),
            ('full_grid_count', receipt['full_grid_count']+1), ('model_count', 0),
            ('residual_state_count', True), ('affine_lane_weight', .5),
            ('public_slot_indices', list(range(23))),
        ):
            changed = copy.deepcopy(receipt); changed[field] = value; invalid.append(changed)
        changed = copy.deepcopy(receipt); changed['operational_budget']['log_e_threshold'] = math.inf; invalid.append(changed)
        changed = copy.deepcopy(receipt); changed['operational_budget']['log_e_threshold'] += 1.; invalid.append(changed)
        changed = copy.deepcopy(receipt); changed['operational_budget']['attempts_per_24h_bound'] += 1; invalid.append(changed)
        changed = copy.deepcopy(receipt); changed['operational_budget']['log_null_expectation_penalty'] = .1; invalid.append(changed)
        for changed in invalid:
            with self.subTest(receipt=changed), self.assertRaises(ValueError):
                plan_from_native(native, 8., manual_seconds=11.6, complete_admission=changed)

    def test_complete_receipt_allows_helper_off_but_keeps_acquisition_unqualified(self):
        native = fixture(False); native['helper_enabled'] = False
        result = plan_from_native(native, 8., manual_seconds=11.6,
                                  complete_admission=complete_receipt(native))
        self.assertEqual(result['status'], 'manual_duration_override')
        self.assertFalse(result['helper_present'])
        self.assertTrue(result['stream_geometry_supported'])
        self.assertTrue(any('acquisition' in text.lower() for text in result['unmodeled_selected_effects']))
        self.assertTrue(result['selected'])


if __name__ == '__main__':
    unittest.main()

"""Small numerical and scope checks for the coverage reference models."""
import importlib.util
import json
import math
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[1]/"tools"/"triad_coverage_bounds.py"
SPEC = importlib.util.spec_from_file_location("coverage_bounds", PATH)
B = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(B)


class CoverageBoundsTests(unittest.TestCase):
    def test_known_presence_values(self):
        self.assertAlmostEqual(B.known_waveform_presence_energy(.99,1e-4),18.27321511953556,places=10)
        self.assertAlmostEqual(B.known_waveform_presence_energy(.9,.01),6.5084691830804715,places=11)

    def test_known_mean_q_normalization(self):
        for p in (.9,.99,.999):
            for correlation in (-1.,0.,.8):
                energy=B.equal_energy_bit_requirement(p,correlation)
                self.assertAlmostEqual(B.known_mean_bit_error(2*energy*(1-correlation)),1-p,places=14)
        self.assertEqual(B.known_mean_bit_error(0),.5)

    def test_phase_antipodal_is_reference_only(self):
        self.assertAlmostEqual(B.equal_energy_bit_requirement(.99,0),2*B.equal_energy_bit_requirement(.99,-1))
        with self.assertRaises(ValueError):B.equal_energy_bit_requirement(.99,1)

    def test_twenty_db_duration_factor(self):
        low=B.energy_to_duration(18,-60)
        high=B.energy_to_duration(18,-40)
        self.assertAlmostEqual(low['wall_seconds_lower_bound']/high['wall_seconds_lower_bound'],100)

    def test_duty_and_outage_energy_accounting(self):
        a=B.energy_to_duration(12,-6)
        b=B.energy_to_duration(12,-6,duty_cycle=1/3,retained_energy_fraction=.5)
        self.assertEqual(a['useful_energy_equivalent_seconds'],b['useful_energy_equivalent_seconds'])
        self.assertAlmostEqual(b['active_seconds_lower_bound']/a['active_seconds_lower_bound'],2)
        self.assertAlmostEqual(b['wall_seconds_lower_bound']/a['wall_seconds_lower_bound'],6)

    def test_one_completion_is_favorable_to_allocation(self):
        alpha,family=B.allocated_alpha(1e-4,attempts=1440,candidates=2,durations=8,models=16,score_budget_fraction=.5)
        self.assertEqual(family,368640)
        self.assertAlmostEqual(alpha,1e-4/737280)
        self.assertGreater(B.known_waveform_presence_energy(.99,alpha),B.known_waveform_presence_energy(.99,1e-4))

    def test_tiny_alpha_without_one_minus_cancellation(self):
        self.assertTrue(math.isfinite(B.known_waveform_presence_energy(.99,1e-200)))
        self.assertGreater(B.known_waveform_presence_energy(.99,1e-200),B.known_waveform_presence_energy(.99,1e-30))

    def test_full_requested_grids_sorted_and_finite(self):
        r=B.build_report()
        self.assertEqual(len(r['cn0_rows']),19)
        self.assertEqual(r['cn0_grid_sorted'],sorted(B.CN0_GRID))
        self.assertEqual(len(r['rate_geometry']),17)
        self.assertEqual(r['optional_gaussian_screens'],[])
        json.dumps(r,allow_nan=False)

    def test_report_duration_has_no_rate_or_spreading_gain_assumption(self):
        a=B.build_report(rates=(1.,),spreading=1)
        b=B.build_report(rates=(30e6,),spreading=1000)
        self.assertEqual(a['cn0_rows'],b['cn0_rows'])
        self.assertNotEqual(a['rate_geometry'],b['rate_geometry'])

    def test_reference_extreme_values_no_overflow(self):
        r=B.build_report(cn0_grid=(-300,200,300))
        json.dumps(r,allow_nan=False)
        self.assertGreater(r['cn0_rows'][0]['targets']['strict']['known_waveform_one_completion_presence_floor']['wall_seconds_lower_bound'],1e30)

    def test_representation_flags_are_precision_specific(self):
        a=B.representation_diagnostic(100,1)
        self.assertTrue(a['formats']['fp32']['covariance_or_subtractive_residual_risk'])
        self.assertFalse(a['formats']['fp32']['raw_signal_plus_noise_risk'])
        self.assertFalse(a['formats']['fp64']['covariance_or_subtractive_residual_risk'])
        b=B.representation_diagnostic(200,1)
        self.assertTrue(b['formats']['fp32']['raw_signal_plus_noise_risk'])
        self.assertTrue(b['formats']['fp64']['covariance_or_subtractive_residual_risk'])
        self.assertFalse(b['formats']['fp64']['raw_signal_plus_noise_risk'])

    def test_representation_uses_sample_domain_snr(self):
        a=B.representation_diagnostic(140,1)
        b=B.representation_diagnostic(140,1e6)
        self.assertAlmostEqual(a['sample_snr_db']-b['sample_snr_db'],60)

    def test_gaussian_small_width_and_large_time_limits(self):
        self.assertAlmostEqual(B.gaussian_covariance_square_integral(10,1e-12),100,places=10)
        t=1e8;b=.1
        actual=B.gaussian_covariance_square_integral(t,b)
        asymptotic=t/(math.sqrt(math.pi)*b)-1/(math.pi*b)**2
        self.assertAlmostEqual(actual/asymptotic,1.,places=14)

    def test_gaussian_edge_integral_against_midpoint_quadrature(self):
        t=3.;b=.2;n=10000;dt=t/n
        numeric=2*math.fsum((t-(i+.5)*dt)*math.exp(-(math.pi*b*(i+.5)*dt)**2)*dt for i in range(n))
        exact=B.gaussian_covariance_square_integral(t,b)
        self.assertLess(abs(numeric-exact)/exact,1e-8)

    def test_gaussian_increasing_width_reduces_upper_information(self):
        a=B.gaussian_presence_upper(-60,315576000,.001)
        b=B.gaussian_presence_upper(-60,315576000,.1)
        self.assertGreater(a['kl_upper_nats'],b['kl_upper_nats'])
        self.assertTrue(a['targets']['strict']['excluded_in_this_continuous_gaussian_model'])
        self.assertTrue(a['not_a_spread_preset_or_sampled_channel_prediction'])
        with self.assertRaises(ValueError):B.gaussian_presence_upper(-60,100,0)

    def test_invalid_inputs_do_not_silently_clamp(self):
        for value in (float('nan'),float('inf'),-301,301):
            with self.assertRaises(ValueError):B.linear_cn0(value)
        for value in (0,-1,1.1,float('nan')):
            with self.assertRaises(ValueError):B.energy_to_duration(12,-6,duty_cycle=value)
        with self.assertRaises(ValueError):B.build_report(cn0_grid=[])
        with self.assertRaises(ValueError):B.build_report(spreading=True)


if __name__ == '__main__':unittest.main()

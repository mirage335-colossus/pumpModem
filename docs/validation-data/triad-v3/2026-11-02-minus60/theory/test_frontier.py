"""Focused mathematical checks; no waveform/receiver qualification."""
import math
import unittest

import mpmath as mp

import frontier


class FrontierChecks(unittest.TestCase):
    def test_archived_finite_grid_parity(self):
        checked = frontier.archived_parity()
        self.assertEqual(checked["finite_grid_points"], 35)
        self.assertLess(checked["maximum_relative_logdet_difference"], 5e-12)
        self.assertLess(checked["maximum_relative_variance_difference"], 5e-12)

    def test_independent_hyperbolic_formula(self):
        with mp.workdps(80):
            for days in frontier.DAYS:
                for tau in frontier.COHERENCES[:-1]:
                    t, q, coherence = mp.mpf(str(days*86400)), mp.mpf(".000001"), mp.mpf(str(tau))
                    z = mp.sqrt(1+q*coherence)
                    raw = 2*(-t/coherence + mp.log(mp.cosh(z*t/coherence) +
                        (1+q*coherence/2)/z*mp.sinh(z*t/coherence)))
                    value = frontier.moments(float(t), tau)["two_ray_log_determinant"]
                    self.assertAlmostEqual(value/float(raw), 1., places=14)

    def test_variance_from_covariance_integral(self):
        with mp.workdps(70):
            for duration, coherence in ((1., 1e8), (1e5, 1e6), (1e8, 1e6)):
                t, tau, c = map(mp.mpf, (duration, coherence, 1e-6))
                variance = c*c*mp.quad(lambda u: (t-u)*mp.exp(-2*u/tau), [0, min(t, tau), t])
                calculated = frontier.moments(duration, coherence)["H1_logLR_variance"]
                self.assertAlmostEqual(calculated/float(variance), 1., places=13)

    def test_static_limit_and_low_power_limit(self):
        for duration in (1., 38*86400., 3652.5*86400.):
            finite = frontier.moments(duration, 1e20)
            static = frontier.moments(duration, math.inf)
            for field in ("two_ray_log_determinant", "H1_logLR_mean", "H1_logLR_variance"):
                self.assertAlmostEqual(finite[field]/static[field], 1., places=10)
        small = frontier.moments(1e6, 1e4, cn0=1e-16)
        approximation = 1e-32*1e4/4
        self.assertAlmostEqual(small["asymptotic_KL_rate_nats_per_second"]/approximation, 1., places=10)
        self.assertAlmostEqual(small["H1_logLR_mean"]/(small["H1_logLR_variance"]/2), 1., places=10)

    def test_static_exact_law_and_cantelli(self):
        for days in frontier.DAYS:
            values = frontier.moments(days*86400, math.inf)
            energy = values["nominal_mean_energy_E_N0"]
            for p in frontier.gates_and_known_waveform():
                gate = p["matched_LR_log_gate"]
                exact = frontier.static_presence(energy, gate)
                x = (gate+2*math.log1p(energy/2))/(energy/2)
                self.assertAlmostEqual(exact["exact_H1_presence_probability"], math.exp(-x)*(1+x), places=14)
                bounds = frontier.cantelli(values["H1_logLR_mean"], values["H1_logLR_variance"], gate)
                self.assertLessEqual(bounds["lower"], exact["exact_H1_presence_probability"]+1e-14)
                self.assertGreaterEqual(bounds["upper"]+1e-14, exact["exact_H1_presence_probability"])
                self.assertLessEqual(exact["exact_H0_false_probability_per_component"], math.exp(-gate))

    def test_schedules_and_reference_gate(self):
        profiles = {p["id"]: p for p in frontier.gates_and_known_waveform()}
        self.assertEqual(profiles["minute-strict"]["known_waveform_reference"]["scheduled_attempts"], 1440)
        self.assertEqual(profiles["minute-relaxed"]["known_waveform_reference"]["scheduled_attempts"], 720)
        self.assertEqual(profiles["daily-strict"]["known_waveform_reference"]["scheduled_attempts"], 1)
        self.assertEqual(profiles["daily-relaxed"]["known_waveform_reference"]["scheduled_attempts"], 1)
        self.assertEqual(profiles["minute-strict"]["known_waveform_reference"]["method_alternatives_K"], 16)
        self.assertEqual(profiles["minute-strict"]["known_waveform_reference"]["duration_alternatives_D"], 8)
        self.assertAlmostEqual(profiles["minute-strict"]["matched_LR_log_gate"],
                               math.log(1440*16*8*2/(.5e-4)), places=12)
        self.assertLess(profiles["daily-strict"]["required_known_waveform_useful_days_at_minus60"],
                        profiles["minute-strict"]["required_known_waveform_useful_days_at_minus60"])

    def test_invalid_inputs_and_finite_report(self):
        for duration, coherence, cn0 in ((0, 1, 1), (math.nan, 1, 1), (1, 0, 1),
                (1, -math.inf, 1), (1, math.nan, 1), (1, 1, 0)):
            with self.assertRaises(ValueError):
                frontier.moments(duration, coherence, cn0)
        with self.assertRaises(ValueError):
            frontier.cantelli(0, 0, 1)
        report = frontier.report()
        self.assertEqual(len(report["OU_and_static_Gaussian_rows"]), 42)
        self.assertEqual(report["cn0_linear_hz"], 1e-6)


if __name__ == "__main__":
    unittest.main()

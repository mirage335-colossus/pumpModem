"""Small deterministic mathematical checks, not physical validation."""
import math
import unittest

import mpmath as mp
import numpy as np
from scipy.integrate import quad
from scipy.linalg import eigvalsh
from scipy.stats import norm

import spread_frontier as f


def discrete_kl(T, C, width, spectrum, channels, n):
    t = (np.arange(n)+.5)*T/n
    lag = t[:, None]-t[None, :]
    p = f.spectral_parameters(width)
    R = (np.exp(-2*math.pi**2*p["Gaussian_sigma_hz"]**2*lag**2)
         if spectrum == "gaussian" else
         np.exp(-np.abs(lag)/p["OU_correlation_efold_seconds"]))
    eigenvalues = eigvalsh(C/channels*T/n*R)
    return channels*sum(f.g(max(0., float(x))) for x in eigenvalues)


class SpreadChecks(unittest.TestCase):
    def test_normalizations_and_width_semantics(self):
        p = f.spectral_parameters(.01)
        sigma, gamma = p["Gaussian_sigma_hz"], p["matched_spectral_HWHM_hz"]
        gaussian, _ = quad(lambda x: math.exp(-x*x/2)/math.sqrt(2*math.pi), -10, 10)
        lorentz, _ = quad(lambda x: 1/(math.pi*(1+x*x)), -math.inf, math.inf)
        self.assertAlmostEqual(gaussian, 1., places=12)
        self.assertAlmostEqual(lorentz, 1., places=12)
        self.assertAlmostEqual(math.exp(-gamma*gamma/(2*sigma*sigma)), .5, places=14)
        self.assertAlmostEqual(math.exp(-2*math.pi*gamma*p["OU_correlation_efold_seconds"]), math.exp(-1), places=14)
        self.assertAlmostEqual(math.exp(-2*math.pi**2*sigma*sigma*p["Gaussian_correlation_efold_seconds"]**2), math.exp(-1), places=14)

    def test_finite_correlation_integrals(self):
        with mp.workdps(60):
            for T, width in ((1., 1e-8), (10., .1), (100., .1)):
                for spectrum in ("gaussian", "ou"):
                    J, I, _ = f.correlation_integrals(T, width, spectrum)
                    p = f.spectral_parameters(width)
                    correlation2 = ((lambda u: mp.exp(-4*mp.pi**2*(width/2)**2*u*u))
                                    if spectrum == "gaussian" else
                                    (lambda u: mp.exp(-2*u/p["OU_correlation_efold_seconds"])))
                    exact_J = mp.quad(correlation2, [0, T])
                    exact_I = mp.quad(lambda u: (T-u)*correlation2(u), [0, T])
                    self.assertAlmostEqual(J/float(exact_J), 1., places=13)
                    self.assertAlmostEqual(I/float(exact_I), 1., places=13)

    def test_continuous_bounds_against_refined_covariance(self):
        for spectrum in ("gaussian", "ou"):
            for channels in (1, 2):
                bounds = f.finite_kl_bounds(20., .1, spectrum, .2, channels)
                sequence = [discrete_kl(20., .2, .1, spectrum, channels, n) for n in (64, 128, 256)]
                value = sequence[-1]
                self.assertLess(bounds["finite_KL_lower_nats"], value)
                self.assertGreater(bounds["finite_KL_upper_nats"], value)
                self.assertLess(abs(sequence[2]-sequence[1]), abs(sequence[1]-sequence[0]))
                if spectrum == "ou":
                    self.assertLess(abs(value/bounds["finite_KL_exact_nats"]-1), .001)

    def test_spectral_rate_and_static_limits(self):
        for spectrum in ("gaussian", "ou"):
            p = f.spectral_parameters(.01)
            for channels in (1, 2):
                C = 1e-10
                rate = f.spectral_kl_rate(.01, spectrum, C, channels)
                expected = C*C*p[("Gaussian" if spectrum == "gaussian" else "OU")+"_squared_correlation_integral_seconds"]/(2*channels)
                self.assertAlmostEqual(rate["KL_rate_nats_per_second"]/expected, 1., places=7)
                self.assertLess(rate["omitted_tail_analytic_upper"], expected*1e-20)
                if spectrum == "ou":
                    long = f.ou_exact_kl(1e7, p["OU_correlation_efold_seconds"], 1e-4, channels)/1e7
                    limit = f.spectral_kl_rate(.01, spectrum, 1e-4, channels)["KL_rate_nats_per_second"]
                    self.assertLess(abs(long/limit-1), 1e-5)
        for channels in (1, 2):
            actual = f.ou_exact_kl(10, 1e20, .1, channels)
            self.assertAlmostEqual(actual, 1-channels*math.log1p(1/channels), places=14)

    def test_sum_centers_and_envelope_upper_bound(self):
        n, T, C, width = 128, 10., .1, .1
        t = (np.arange(n)+.5)*T/n
        R = np.exp(-2*math.pi**2*(width/2)**2*(t[:, None]-t[None, :])**2)
        K = C*T/n*R
        eigenvalues = eigvalsh(K)
        separated = 2*sum(f.g(max(0., float(x)/2)) for x in eigenvalues)
        overlapping = sum(f.g(max(0., float(x))) for x in eigenvalues)
        for center in (0., .01, .1, 1.):
            modulation = np.exp(2j*math.pi*center*t)
            total = .5*K+.5*modulation[:, None]*K*modulation.conj()[None, :]
            actual = sum(f.g(max(0., float(x))) for x in eigvalsh(total))
            self.assertLessEqual(separated-1e-12, actual)
            self.assertLessEqual(actual, overlapping+1e-12)
        a = np.sqrt(1+.4*np.sin(2*math.pi*t/T))
        b = np.sqrt(1+.4*np.cos(2*math.pi*t/T))*np.exp(2j*math.pi*.1*t)
        total = .5*a[:, None]*K*a[None, :]+.5*b[:, None]*K*b.conj()[None, :]
        actual = sum(f.g(max(0., float(x))) for x in eigvalsh(total))
        bound = f.shaped_envelope_kl_upper(T, width, "gaussian", C, 1.4)
        self.assertLess(actual, bound)

    def test_binary_necessity_and_known_waveform_reference(self):
        for profile in f.profiles():
            p = profile["target_correct_probability"]
            a = profile["false_probability_used_in_necessity"]
            threshold = profile["required_binary_KL_nats"]
            self.assertAlmostEqual(f.upper_power_given_kl(threshold, a), p, places=10)
            E = profile["known_fixed_waveform_required_E_N0"]
            self.assertAlmostEqual(float(norm.cdf(math.sqrt(2*E)-norm.isf(a))), p, places=13)
        self.assertAlmostEqual(f.upper_power_given_kl(0., 1e-4), 1e-4)
        self.assertEqual(f.upper_power_given_kl(100., 1e-4), 1.)
        strict = f.profiles()[0]["required_binary_KL_nats"]
        for spectrum in ("gaussian", "ou"):
            upper = f.shaped_envelope_kl_upper(f.MAX_SECONDS, .001, spectrum, 1e-6, 10.)
            self.assertLess(upper, strict)
        d = f.necessary_duration(lambda t: .001*t, 1.)
        self.assertFalse(d["ten_year_target_excluded"])
        self.assertLessEqual(d["necessary_seconds_lower_bound"], 1000.)
        self.assertGreater(d["necessary_seconds_lower_bound"], 999.99999)

    def test_bit_identifiability(self):
        records = f.bit_identifiability_examples()["examples"]
        for row in records:
            value = row["KL_bit0_to_bit1_nats"]
            if row["alternative"] == "global-phase" or (row["independent_channel_cells"] and row["alternative"] == "varying-phase"):
                self.assertLess(value, 1e-12)
            else:
                self.assertGreater(value, .01)

    def test_invalid_inputs(self):
        for width in (0, -1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                f.spectral_parameters(width)
        with self.assertRaises(ValueError):
            f.finite_kl_bounds(1, .1, "gaussian", 1, 3)
        with self.assertRaises(ValueError):
            f.shaped_envelope_kl_upper(1, .1, "gaussian", 1, .5)
        with self.assertRaises(ValueError):
            f.binary_kl(1, .1)
        with self.assertRaises(ValueError):
            f.spectral_kl_rate(.1, "jakes", 1)


if __name__ == "__main__":
    unittest.main()

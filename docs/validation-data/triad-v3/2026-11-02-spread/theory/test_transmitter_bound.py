"""Actual-helper and independent quadrature checks for the source envelope."""
import math
import unittest

import numpy as np

import transmitter_bound as b


class TransmitterBounds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table, cls.gain, cls.amplitudes = b.actual_table()
        cls.bounds = b.synthesis_bounds(cls.table, cls.gain, cls.amplitudes)
        cls.knots = np.asarray(cls.table["knots"])

    def pulse(self, coordinates):
        x = np.abs(coordinates)*256
        return np.interp(x, np.arange(len(self.knots)), self.knots, left=0, right=0)

    def test_actual_table_and_phase_refinement(self):
        self.assertEqual(self.table["half_span"], 8)
        self.assertEqual(len(self.knots), 2049)
        self.assertAlmostEqual(self.bounds["chip_triplet_coefficient_norm_squared"], 1, places=14)
        values = [np.abs(self.pulse(u-np.arange(-8, 10))).sum()
                  for u in np.arange(1025)/1024]
        self.assertLessEqual(max(values), self.bounds["sum_absolute_pulse_max_unpadded"]+1e-14)
        self.assertEqual(self.bounds["maximizing_phase_grid_index"], 128)
        self.assertLess(self.bounds["lookup_piecewise_linear_energy"], 1.)
        self.assertGreater(self.bounds["lookup_piecewise_linear_energy"], .9999)

    def test_gram_against_independent_gauss_legendre(self):
        nodes, weights = np.polynomial.legendre.leggauss(8)
        for shift in (0, 1, 7, 15, 16):
            for frequency in (0., 1.25, 2.5):
                low, high = max(-8, shift-8), min(8, shift+8)
                starts = np.arange(low*256, high*256)/256
                u = starts[:, None]+(nodes[None, :]+1)/512
                values = self.pulse(u)*self.pulse(u-shift)*np.exp(2j*math.pi*frequency*u)
                reference = np.sum(values*weights[None, :])/512
                actual = b.gram_entry(self.knots, 256, shift, frequency)
                self.assertLess(abs(actual-reference), 3e-14)

    def test_finite_synthesis_matrix(self):
        count = 8
        frequencies = (-1.25, 0., 1.25)
        labels = [(k, f) for k in range(count) for f in frequencies]
        gram = np.empty((len(labels), len(labels)), complex)
        for i, (k, fk) in enumerate(labels):
            for j, (l, fl) in enumerate(labels):
                gram[i, j] = np.exp(2j*math.pi*(fl-fk)*k)*b.gram_entry(self.knots, 256, l-k, fl-fk)
        self.assertLess(np.max(np.abs(gram-gram.conj().T)), 1e-13)
        self.assertGreater(np.linalg.eigvalsh(gram)[0], 0)
        self.assertLess(np.linalg.eigvalsh(gram)[-1], self.bounds["synthesis_energy_operator_bound"])
        # Deterministic admissible amplitude permutations/eighth-turn phases,
        # independent of source key choice; no receiver or propagation model.
        coefficients = np.array([self.gain*self.amplitudes[(k+c)%3]*np.exp(1j*math.pi/4*((3*k+c)%8))
                                 for k in range(count) for c in range(3)])
        energy = float(np.vdot(coefficients, gram@coefficients).real)
        self.assertLess(energy, count*self.bounds["synthesis_energy_operator_bound"])

    def test_information_connection_retains_energy_upper(self):
        result = b.envelope_information_bound(315576000., .000006,
            self.bounds["absolute_peak_power_bound"], self.bounds["synthesis_energy_operator_bound"],
            self.bounds["chip_triplet_coefficient_norm_squared"], 1e-6, .001, "gaussian")
        self.assertGreater(result["source_average_power_upper"], 1.)
        self.assertLess(result["source_average_power_upper"], 1.021)
        self.assertLess(result["Gaussian_channel_KL_upper_nats"], 1.)
        self.assertGreater(result["full_delayed_observation_seconds"], result["active_elapsed_seconds"])

    def test_discrete_FIR_bound_uses_actual_covariance(self):
        for kernel in self.table["gaussian_fir"]:
            result = b.discrete_fir_peak_bound(315576000., 1e-6,
                self.bounds["absolute_peak_power_bound"], kernel)
            self.assertAlmostEqual(result["FIR_covariance_zero"], 1., places=13)
            self.assertGreater(result["source_discrete_energy_seconds_upper"], 9*315576000.)
            self.assertLess(result["Gaussian_discrete_KL_upper_nats"], 9.062236433951577)
            self.assertGreater(result["Gaussian_discrete_KL_upper_nats"], 3.8205752275831846)
            self.assertLess(result["one_completion_admission_probability_upper"]["strict-one-completion-global-alpha"], .99)


if __name__ == "__main__":
    unittest.main()

import unittest
from decimal import Decimal, localcontext

import numpy as np
from scipy.special import ndtr

from tools.triad_covariance_reference import (
    MAX_SAMPLES,
    block_center_coefficient_covariance,
    channel_covariances,
    covariance_bhattacharyya,
    covariance_kl,
    factor_covariance,
    gaussian_coefficient_covariance,
    gaussian_log_likelihood,
    known_realized_channel_error_probability,
    proper_complex_gaussian_samples,
)


class TriadCovarianceReferenceTests(unittest.TestCase):
    def setUp(self):
        self.times = np.arange(12, dtype=np.float64) * 0.002
        self.basis = np.empty((2, 12, 2), dtype=np.complex128)
        # Two distinct complex sample/tap bases. Values are only algebraic
        # fixtures; they are not presented as a transmitted physical pattern.
        for bit in range(2):
            for tap in range(2):
                n = np.arange(12)
                self.basis[bit, :, tap] = (0.4 + 0.08 * bit) * np.exp(
                    1j * (0.17 * n + 0.29 * tap + 0.21 * bit * n))

    def test_zero_doppler_is_common_complex_gain_covariance(self):
        got = gaussian_coefficient_covariance(self.times, 0.0)
        np.testing.assert_array_equal(got, np.ones((12, 12)))
        expected = np.stack([
            sum(power * np.outer(self.basis[b, :, tap],
                                 self.basis[b, :, tap].conj())
                for tap, power in enumerate((0.7, 0.3)))
            for b in range(2)
        ])
        actual = channel_covariances(
            self.basis, self.times, 0.0, (0.7, 0.3))
        np.testing.assert_allclose(actual, expected, rtol=2e-15, atol=2e-15)

    def test_general_covariance_matches_direct_matrix_definition(self):
        C = gaussian_coefficient_covariance(self.times, 17.0)
        got = channel_covariances(self.basis, self.times, 17.0, (0.7, 0.3))
        expected = np.empty_like(got)
        for bit in range(2):
            expected[bit] = sum(
                power * np.diag(self.basis[bit, :, tap]) @ C @
                np.diag(self.basis[bit, :, tap]).conj().T
                for tap, power in enumerate((0.7, 0.3)))
        np.testing.assert_allclose(got, expected, rtol=3e-14, atol=3e-14)
        for covariance in got:
            np.testing.assert_allclose(covariance, covariance.conj().T,
                                       rtol=0, atol=2e-15)
            self.assertGreaterEqual(np.linalg.eigvalsh(covariance)[0], -1e-12)

    def test_single_sample_per_block_recovers_full_covariance(self):
        exact = gaussian_coefficient_covariance(self.times, 4.0)
        approximation = block_center_coefficient_covariance(
            self.times, 4.0, block_samples=1, window_samples=12)
        np.testing.assert_allclose(approximation, exact, rtol=2e-15, atol=2e-15)
        a = channel_covariances(self.basis, self.times, 4.0, (0.5, 0.5))
        b = channel_covariances(self.basis, self.times, 4.0, (0.5, 0.5),
                                block_samples=1, window_samples=12)
        np.testing.assert_allclose(a, b, rtol=2e-15, atol=2e-15)

    def test_block_center_windows_keep_fixed_physical_span(self):
        times = np.arange(128, dtype=np.float64) / 2048.0
        for block in (32, 16, 8):
            actual = block_center_coefficient_covariance(
                times, 30.0, block_samples=block, window_samples=64)
            self.assertEqual(actual.shape, (128, 128))
            # The physical 64-sample window is common across the comparisons;
            # no covariance is asserted across its independence boundary.
            self.assertTrue(np.all(actual[:64, :64] >= 0))
            self.assertEqual(actual[0, 64], 0.0)
            np.testing.assert_allclose(actual, actual.T, rtol=0, atol=0)
            self.assertGreaterEqual(np.linalg.eigvalsh(actual)[0], -1e-11)

    def test_complex_gaussian_likelihood_matches_direct_density(self):
        K = np.array([[1.2, .2 + .1j], [.2 - .1j, .7]], dtype=np.complex128)
        sigma2 = 0.4
        y = np.array([.3 + .5j, -.8 + .12j])
        factor = factor_covariance(K, sigma2)
        expected = -np.linalg.slogdet(K + sigma2*np.eye(2))[1] - np.real(
            y.conj() @ np.linalg.solve(K + sigma2*np.eye(2), y))
        self.assertAlmostEqual(float(gaussian_log_likelihood(y, factor)),
                               float(expected), places=13)
        # Factoring must not mutate caller-owned covariance storage.
        np.testing.assert_array_equal(K, np.array([[1.2, .2 + .1j],
                                                   [.2 - .1j, .7]]))

    def test_noise_only_likelihood_uses_complex_not_real_dof(self):
        # For CN(0, sigma^2), the magnitude square has mean sigma^2 and the
        # no-signal log likelihood includes -log(sigma^2) per sample.
        y = np.array([1 + 2j, -0.5 + .25j])
        sigma2 = 3.0
        score = gaussian_log_likelihood(y, factor_covariance(np.zeros((2, 2)), sigma2))
        expected = -2 * np.log(sigma2) - np.vdot(y, y).real / sigma2
        self.assertAlmostEqual(float(score), float(expected), places=14)

    def test_covariance_divergences_have_correct_complex_normalization(self):
        zero = np.zeros((2, 2), dtype=np.complex128)
        same = covariance_kl(zero, zero, 1.0)
        self.assertEqual(same, 0.0)
        K0 = np.diag([1.0, 0.0]).astype(np.complex128)
        K1 = np.diag([0.0, 1.0]).astype(np.complex128)
        kl01 = covariance_kl(K0, K1, 1.0)
        kl10 = covariance_kl(K1, K0, 1.0)
        self.assertAlmostEqual(kl01, kl10, places=13)
        self.assertGreater(kl01, 0)
        bhat = covariance_bhattacharyya(K0, K1, 1.0)
        self.assertAlmostEqual(bhat, 2*np.log(1.5) - np.log(2), places=13)
        self.assertGreater(bhat, 0)

    def test_known_realized_waveform_error_uses_complex_noise_variance(self):
        p = known_realized_channel_error_probability(
            np.array([1 + 0j]), np.array([-1 + 0j]), 2.0)
        self.assertAlmostEqual(p, float(ndtr(-1.0)), places=14)

    def test_weak_covariance_divergence_preserves_quadratic_information(self):
        # Decimal is only an independent test oracle; runtime uses FP64.
        zero = np.zeros((1, 1), dtype=np.complex128)
        for value in (1e-4, 1e-7, 1e-8, 1e-12, 1e-16):
            signal = np.array([[value]], dtype=np.complex128)
            with localcontext() as context:
                context.prec = 70
                x = Decimal.from_float(value)
                kl = float(x - (1+x).ln())
                bh = float((1+x/2).ln() - (1+x).ln()/2)
            self.assertAlmostEqual(covariance_kl(signal, zero, 1.) / kl, 1., places=13)
            self.assertAlmostEqual(covariance_bhattacharyya(signal, zero, 1.) / bh, 1., places=13)
            self.assertAlmostEqual(covariance_bhattacharyya(zero, signal, 1.) / bh, 1., places=13)

    def test_fp32_covariance_construction_is_not_offered(self):
        with self.assertRaises(TypeError):
            channel_covariances(self.basis, self.times, 11.0, (.6, .4),
                                precision="fp32-bulk")

    def test_fp32_bulk_whitening_is_explicit_and_compares_to_strict_reference(self):
        fp64 = channel_covariances(self.basis, self.times, 11.0, (.6, .4))
        factor64 = factor_covariance(fp64[0], .2)
        samples = np.stack([np.arange(12) * .1 + 1j*np.arange(12) * .03])
        s64 = gaussian_log_likelihood(samples, factor64)
        s32 = gaussian_log_likelihood(samples, factor64, precision="fp32-bulk")
        self.assertLess(abs(float(s64[0] - s32[0])), 2e-4)

    def test_fp64_covariances_remain_factorable_for_oversampled_high_width(self):
        times = np.arange(128, dtype=np.float64) / 2048.0
        n = np.arange(128)
        basis = np.stack([
            np.stack([np.exp(1j * (.071*n + bit*.013*n)),
                      .6*np.exp(1j * (.089*n + .17 + bit*.019*n))], axis=-1)
            for bit in range(2)
        ])
        for width in (0.0, 10.0, 30.0):
            covariance = channel_covariances(basis, times, width, (.6, .4))
            for bit in range(2):
                factor = factor_covariance(covariance[bit], .2)
                self.assertTrue(np.isfinite(factor.log_determinant))
                hermitian = .5 * (covariance[bit] + covariance[bit].conj().T)
                self.assertGreaterEqual(np.linalg.eigvalsh(hermitian)[0], -1e-10)

    def test_fp32_whitening_rejects_overflow_and_underflow_range(self):
        identity = factor_covariance(np.eye(1, dtype=np.complex128), 1.0)
        with self.assertRaisesRegex(ValueError, "finite FP32 range"):
            gaussian_log_likelihood(np.array([1e40+0j]), identity,
                                    precision="fp32-bulk")
        with self.assertRaisesRegex(ValueError, "subnormal-range"):
            gaussian_log_likelihood(np.array([1e-50+0j]), identity,
                                    precision="fp32-bulk")

    def test_proper_complex_sample_generator_is_seeded_and_represented(self):
        factor = factor_covariance(np.eye(3, dtype=np.complex128), .5)
        a = proper_complex_gaussian_samples(factor, 7, seed=123)
        b = proper_complex_gaussian_samples(factor, 7, seed=123)
        self.assertEqual(a.shape, (7, 3))
        np.testing.assert_array_equal(a, b)

    def test_rejects_bad_shapes_ranges_and_loss_of_dense_bound(self):
        with self.assertRaises(ValueError):
            gaussian_coefficient_covariance(self.times[::-1], 1.0)
        with self.assertRaises(ValueError):
            gaussian_coefficient_covariance(np.array([0.0, 1.0+0.1j]), 1.0)
        with self.assertRaises(ValueError):
            gaussian_coefficient_covariance(np.array([False, True]), 1.0)
        with self.assertRaises(ValueError):
            channel_covariances(self.basis, self.times, -1.0, (.5, .5))
        with self.assertRaises(ValueError):
            channel_covariances(self.basis, self.times, 1.0, (.5,))
        with self.assertRaises(ValueError):
            channel_covariances(self.basis, self.times, 1.0, (.5, .5),
                                block_samples=3, window_samples=8)
        with self.assertRaises(ValueError):
            gaussian_coefficient_covariance(np.arange(MAX_SAMPLES+1), 0.0)
        with self.assertRaises(ValueError):
            factor_covariance(np.array([[1, 1j], [0, 1]], dtype=np.complex128), 1.0)


if __name__ == "__main__":
    unittest.main()

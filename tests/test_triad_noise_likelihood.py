#!/usr/bin/env python3
"""Focused public-statistics oracles; no ordinary receiver campaign."""
from pathlib import Path
import math
import sys
import unittest

import numpy as np
from scipy.integrate import quad
from scipy.special import gammainc
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_noise_likelihood import (
    AngularGaussianLikelihood, FalseAdmissionBudget,
    IndependentResidualGaussianLikelihood, angular_log_likelihood,
    normalize_by_variance_upper, variance_upper_confidence, whole_bank_residual,
)

torch.set_num_threads(2)


def cn(rng, shape):
    return (rng.normal(size=shape) + 1j*rng.normal(size=shape))/math.sqrt(2.)


def tensor(value):
    return torch.as_tensor(value, dtype=torch.complex128)


def raw_covariance(designs, times, prior):
    lengths = [len(A) for A in designs]
    offsets = np.cumsum([0] + lengths)
    C = np.zeros((offsets[-1], offsets[-1]), dtype=complex)
    P = np.diag(prior.stationary_power)
    for i, Ai in enumerate(designs):
        for j, Aj in enumerate(designs):
            lag = times[i]-times[j]
            decay = 1. if math.isinf(prior.coherence_seconds) else math.exp(-abs(lag)/prior.coherence_seconds)
            cov = decay*np.exp(2j*np.pi*prior.frequency_hz*lag)*P
            C[offsets[i]:offsets[i+1], offsets[j]:offsets[j+1]] = Ai@cov@Aj.conj().T
    return C


class NoiseLikelihoodTests(unittest.TestCase):
    def test_dynamic_angular_dense_covariance_scale_and_chunk_invariance(self):
        rng = np.random.default_rng(93205)
        prior = GaussianPrior((.3, .7), coherence_seconds=4.3, frequency_hz=.07)
        designs = [cn(rng, (n, 2)) for n in (3, 4, 2)]
        times = [0., .7, 2.3]
        ys = [cn(rng, (5, len(A))) for A in designs]
        y = np.concatenate(ys, axis=1)
        C = raw_covariance(designs, times, prior)
        R = np.eye(len(C))+C
        f = np.linalg.slogdet(R)[1]
        energy = np.sum(abs(y)**2, axis=-1)
        inv_energy = np.einsum("bi,ij,bj->b", y.conj(), np.linalg.inv(R), y).real
        oracle = -f-len(C)*np.log(inv_energy/energy)
        results = []
        for amplitude in (1e-150, 1e-3, 1., 137., 1e150):
            receiver = AngularGaussianLikelihood(prior)
            for i, (A, values) in enumerate(zip(designs, ys)):
                scaled = amplitude*values
                r = scaled@A.conj()
                receiver.update(tensor(r), tensor(A.conj().T@A),
                                0. if i == 0 else times[i]-times[i-1],
                                np.sum(abs(scaled)**2, axis=-1), len(A))
            results.append(receiver.finish().numpy())
            self.assertEqual(receiver.diagnostics()["complex_samples"], 9)
        for result in results:
            np.testing.assert_allclose(result, oracle, atol=3e-12, rtol=3e-12)
        # Static gain permits changing block/chunk boundaries without changing R.
        static = GaussianPrior((.3, .7), coherence_seconds=math.inf)
        A = np.concatenate(designs)
        once = AngularGaussianLikelihood(static)
        once.update(tensor(y@A.conj()), tensor(A.conj().T@A), 0., energy, len(A))
        chunks = AngularGaussianLikelihood(static)
        for Ai, yi in zip(designs, ys):
            chunks.update(tensor(yi@Ai.conj()), tensor(Ai.conj().T@Ai), 0.,
                          np.sum(abs(yi)**2, axis=-1), len(Ai))
        torch.testing.assert_close(once.finish(), chunks.finish(), atol=2e-12, rtol=2e-12)

    def test_angular_normalization_and_block_varying_noise_counterexample(self):
        # In C^2, |u_1|² is exactly uniform on [0,1] under isotropic H0.
        R = np.array([4., 1.])
        f = np.log(R).sum()
        def likelihood(x, actual_variances):
            z = np.array([x, 1-x])*actual_variances
            e = z.sum()
            q = np.sum(z*(1-1/R))
            return float(angular_log_likelihood(f, q, e, 2).exp())
        for common_scale in (.01, 1., 1000.):
            mean = quad(lambda x: likelihood(x, np.array([common_scale]*2)), 0, 1,
                        epsabs=1e-11)[0]
            self.assertAlmostEqual(mean, 1., places=11)
        # Shape-coloured noise from changing block powers is not a uniform
        # direction; this positively demonstrates why no such guarantee exists.
        coloured_mean = quad(lambda x: likelihood(x, R), 0, 1, epsabs=1e-11)[0]
        self.assertGreater(coloured_mean, 1.5)

    def test_angular_noise_monte_carlo_normalization(self):
        rng = np.random.default_rng(8621)
        y = cn(rng, (32768, 6))
        R = np.diag([1.7, 1.2, 1., 1., 1., 1.])
        f = np.linalg.slogdet(R)[1]
        energy = np.sum(abs(y)**2, axis=-1)
        q = np.sum(abs(y)**2*(1-1/np.diag(R)), axis=-1)
        lr = angular_log_likelihood(f, q, energy, 6).exp().numpy()
        self.assertLess(abs(lr.mean()-1.), 5*lr.std(ddof=1)/math.sqrt(len(lr)))

    def test_python_energy_fp64_and_scalar_covariance_direction_oracle(self):
        # R=cI changes radial scale only, so its angular density is exactly H0.
        # Python inputs must be constructed directly in FP64, not rounded via
        # Torch's FP32 default before conversion. This previously gave -.02638.
        c, n = 1e6, 1
        f, E = n*math.log(c), 1.
        q = E*(1-1/c)
        self.assertLess(abs(float(angular_log_likelihood(f, q, E, n))), 1e-9)
        self.assertLess(abs(float(angular_log_likelihood([f], [q], [E], n))), 1e-9)
        # The actual workspace exposes Python float energies. Compare that
        # path directly against the raw dense angular covariance expression.
        A = np.array([[1.1+.3j], [-.7+.2j], [.1-.8j]])
        y = np.array([.3+.2j, -.2+.7j, .6-.1j])
        prior = GaussianPrior((.4,), coherence_seconds=math.inf)
        receiver = AngularGaussianLikelihood(prior)
        energy = float(np.vdot(y, y).real)
        receiver.update(tensor(A.conj().T@y), tensor(A.conj().T@A), 0., energy, len(y))
        R = np.eye(len(y))+.4*A@A.conj().T
        oracle = -np.linalg.slogdet(R)[1]-len(y)*np.log(float(np.vdot(y, np.linalg.solve(R, y)).real)/energy)
        self.assertAlmostEqual(float(receiver.finish()), oracle, places=12)
        # Large but finite Python energies must not overflow through FP32.
        self.assertEqual(float(angular_log_likelihood(0., 0., 1e300, 3)), 0.)
        for invalid in (True, [True], [True, 1.], 1j, [1.+0j]):
            with self.assertRaisesRegex(ValueError, "real"):
                angular_log_likelihood(0., 0., invalid, 3)

    def test_near_unit_quadratic_fails_closed_in_helper_and_stream(self):
        c, n = 1e14, 1000000
        # Direct FP64 subtraction previously manufactured ~799.597 log units
        # instead of the exact zero angular evidence of the scalar shape R=cI.
        with self.assertRaisesRegex(ValueError, "unresolved"):
            angular_log_likelihood(n*math.log(c), n*(1-1/c), float(n), n)
        receiver = AngularGaussianLikelihood(GaussianPrior((c-1,), coherence_seconds=math.inf))
        with self.assertRaisesRegex(ValueError, "unresolved"):
            receiver.update(tensor([1.]), tensor([[1.]]), 0., 1., 1)

    def test_whole_bank_residual_matches_independent_raw_svd(self):
        rng = np.random.default_rng(9912)
        base = cn(rng, (17, 3))
        A = np.column_stack([base, base[:, 0]+2*base[:, 1], 1e-8*base[:, 2]])
        y = cn(rng, (19, 17))
        r, G = y@A.conj(), A.conj().T@A
        result = whole_bank_residual(tensor(r), tensor(G), np.sum(abs(y)**2, -1), 17)
        U, singular, _ = np.linalg.svd(A, full_matrices=False)
        rank = int(np.sum(singular**2 > singular[0]**2*1e-10))
        projected = y@U[:, :rank].conj()
        residual_oracle = y-projected@U[:, :rank].T
        np.testing.assert_allclose(result.energy.numpy(), np.sum(abs(residual_oracle)**2, -1), rtol=1e-12, atol=1e-12)
        self.assertEqual(int(result.rank), rank)
        self.assertEqual(int(result.complex_dof), 17-rank)
        # Returned statistics, including deliberate numerical projection, form
        # one consistent bank; original removed modes may not be scored.
        np.testing.assert_allclose(result.projected_gram.numpy(), G, atol=2e-12)
        np.testing.assert_allclose(result.projected_correlations.numpy(), r, atol=3e-12)

    def test_residual_rank_projection_zero_and_cancellation_boundaries(self):
        # Deliberately discard a genuinely positive near-null mode. Returned
        # score statistics remove it too, preserving exact residual independence.
        G = tensor(np.diag([1., 1e-12]))
        r = tensor([[.4+.2j, .7e-6]])
        result = whole_bank_residual(r, G, torch.tensor([5.]), 4)
        self.assertEqual(int(result.rank), 1)
        self.assertEqual(int(result.complex_dof), 3)
        self.assertEqual(complex(result.projected_correlations[0, 1]), 0j)
        self.assertEqual(complex(result.projected_gram[1, 1]), 0j)
        self.assertAlmostEqual(float(result.energy), 4.8, places=12)
        zero = whole_bank_residual(tensor([[0., 0.]]), tensor(np.zeros((2, 2))), [3.], 4)
        self.assertEqual(int(zero.rank), 0)
        self.assertEqual(int(zero.complex_dof), 4)
        self.assertEqual(float(zero.energy), 3.)
        no_residual = whole_bank_residual(tensor([1.]), tensor([[1.]]), 1., 1)
        self.assertEqual(int(no_residual.complex_dof), 0)
        with self.assertRaisesRegex(ValueError, "dof"):
            variance_upper_confidence(no_residual.energy, no_residual.complex_dof, .01)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            whole_bank_residual(tensor([2.]), tensor([[1.]]), 1., 3)
        with self.assertRaisesRegex(ValueError, "unresolved"):
            whole_bank_residual(tensor([1.]), tensor([[1.]]), 1.+1e-12, 3)
        with self.assertRaisesRegex(ValueError, "rank exceeds"):
            whole_bank_residual(tensor([0., 0.]), tensor(np.eye(2)), 2., 1)
        erased = whole_bank_residual(tensor([0., 0.]), tensor(np.zeros((2, 2))), 0., 0)
        self.assertEqual(int(erased.complex_dof), 0)
        self.assertEqual(float(erased.energy), 0.)

    def test_variance_upper_coverage_and_budget_arithmetic(self):
        # Exact CDF identity, not a finite-trial claim about a tiny tail.
        for M in (1, 17, 252, 233981):
            for delta in (.1, 1e-4, 3.7e-11):
                upper = float(variance_upper_confidence(1., M, delta))
                self.assertAlmostEqual(float(gammainc(M, 1/upper))/delta, 1., places=9)
        budget = FalseAdmissionBudget(false_probability=1e-4, attempts=1440,
                                     candidates=18, duration_alternatives=3,
                                     model_alternatives=7, calibration_segments=929)
        self.assertLessEqual(budget.score_probability+budget.calibration_probability, budget.false_probability)
        self.assertLessEqual(budget.delta_per_segment*1440*3*929, budget.calibration_probability)
        self.assertGreaterEqual(budget.log_threshold,
                                math.log(1440*3*7*18/budget.score_probability))
        self.assertEqual(budget.calibration_probability, 5e-5)

    def test_ucb_receiver_dense_known_upper_and_varying_noise(self):
        rng = np.random.default_rng(88751)
        B, M = 8192, 32
        prior = GaussianPrior((.2,), coherence_seconds=3.)
        budget = FalseAdmissionBudget(false_probability=.08, attempts=1,
                                     candidates=1, calibration_segments=3)
        receiver = IndependentResidualGaussianLikelihood(prior, budget)
        direct = GaussianStateFilter(prior)
        receiver.update(tensor(np.zeros((B, 1))), tensor([[0.]]), 0., residual_energy=0., complex_dof=0)
        direct.update(tensor(np.zeros((B, 1))), tensor(np.zeros((B, 1, 1))), 0.)
        total_bad = np.zeros(B, dtype=bool)
        for variance in (.25, 1., 16.):
            raw_r = tensor(cn(rng, (B, 1))*math.sqrt(variance))
            raw_G = tensor([[1.]])
            Ecal = variance*rng.gamma(M, 1., size=B)
            upper = variance_upper_confidence(Ecal, M, budget.delta_per_segment)
            total_bad |= upper.numpy() < variance
            receiver.update(raw_r, raw_G, 1., residual_energy=Ecal, complex_dof=M)
            r, G = normalize_by_variance_upper(raw_r, raw_G, upper)
            direct.update(r, G, 1.)
        torch.testing.assert_close(receiver.finish(), direct.finish(), rtol=0, atol=0)
        admitted = receiver.scores().numpy() >= budget.log_threshold
        # Exact proof is alpha+delta; this broad diagnostic also exercises a
        # genuinely changing variance schedule rather than a global average.
        self.assertLess(admitted.mean(), budget.false_probability)
        self.assertLess(total_bad.mean(), budget.calibration_probability+.01)
        self.assertEqual(receiver.diagnostics()["variance_segments"], 3)
        with self.assertRaisesRegex(ValueError, "exhausted"):
            receiver.update(raw_r, raw_G, 1., residual_energy=Ecal, complex_dof=M)

    def test_candidate_axis_shared_calibration_and_no_subset_independence(self):
        prior = GaussianPrior((.2,), coherence_seconds=3.)
        receiver = IndependentResidualGaussianLikelihood(
            prior, FalseAdmissionBudget(attempts=1, candidates=2))
        r = tensor([[[.1], [.2]], [[.3], [.4]], [[.5], [.6]]])
        G = tensor([[[1.]], [[1.]]])
        receiver.update(r, G, 0., residual_energy=[10., 20., 30.], complex_dof=17)
        self.assertEqual(tuple(receiver.finish().shape), (3, 2))
        # Projecting only candidate 0 leaves noise correlated with candidate 1.
        A = np.array([[1., .5], [0., 1.], [0., 0.]], dtype=complex)
        P_subset = np.diag([1., 0., 0.])
        P_full = A@np.linalg.inv(A.conj().T@A)@A.conj().T
        self.assertGreater(np.linalg.norm((np.eye(3)-P_subset)@A[:, 1]), .5)
        self.assertLess(np.linalg.norm((np.eye(3)-P_full)@A), 1e-14)
        # Unbatched K-vector has no candidate axis to append. Previously the
        # scalar UCB became [1] and incorrectly expanded the scalar score.
        scalar = IndependentResidualGaussianLikelihood(
            prior, FalseAdmissionBudget(attempts=1, candidates=1))
        scalar.update(tensor([.1]), tensor([[1.]]), 0., residual_energy=10., complex_dof=17)
        self.assertEqual(tuple(scalar.finish().shape), ())

    def test_conservative_gaussian_null_mgf_noncommuting_covariance(self):
        rng = np.random.default_rng(55)
        A = cn(rng, (4, 2))*.6
        C = A@A.conj().T
        R = np.eye(4)+C
        B = np.eye(4)-np.linalg.inv(R)
        # A nonconstant diagonal actual/assumed ratio D<=I need not commute
        # with C. This dense proper-Gaussian MGF proves conditional E0 LR<=1.
        for ratios in ([1., 1., 1., 1.], [.2, 1., .4, .7]):
            S = np.diag(np.sqrt(ratios))
            expectation = np.exp(-np.linalg.slogdet(R)[1] -
                                 np.linalg.slogdet(np.eye(4)-S@B@S)[1])
            self.assertLessEqual(expectation, 1.+2e-14)

    def test_erasure_gradient_and_malformed_input_guards(self):
        prior = GaussianPrior((.2,), coherence_seconds=3.)
        receiver = AngularGaussianLikelihood(prior)
        self.assertIsNone(receiver.update(tensor([0.]), tensor([[0.]]), 4., 0., 0))
        receiver.update(tensor([.2]), tensor([[1.]]), 4., 2., 2)
        self.assertTrue(torch.isfinite(receiver.finish()).all())
        bad = AngularGaussianLikelihood(prior)
        with self.assertRaisesRegex(ValueError, "gradients"):
            bad.update(tensor([.2]).requires_grad_(), tensor([[1.]]), 0., 2., 2)
        with self.assertRaisesRegex(ValueError, "positive energy"):
            angular_log_likelihood(1., 0., 0., 2)
        with self.assertRaisesRegex(ValueError, "nonpositive"):
            angular_log_likelihood(1., 2., 2., 2)
        with self.assertRaisesRegex(ValueError, "materially negative"):
            angular_log_likelihood(1., -.1, 2., 2)
        with self.assertRaisesRegex(ValueError, "positive"):
            normalize_by_variance_upper(tensor([1.]), tensor([[1.]]), 0.)
        with self.assertRaises(ValueError):
            FalseAdmissionBudget(calibration_fraction=1.)


if __name__ == "__main__":
    unittest.main()

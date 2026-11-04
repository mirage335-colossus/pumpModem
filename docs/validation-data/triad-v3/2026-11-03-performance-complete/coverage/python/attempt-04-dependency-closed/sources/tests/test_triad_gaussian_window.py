#!/usr/bin/env python3
"""Independent raw-observation oracles for Gaussian-window likelihoods."""
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_gaussian_window import PreparedWindowedGaussian, prepare_windowed_gaussian
from triad_gaussian_state import GaussianPrior, GaussianStateFilter


def cn(rng, shape):
    return (rng.normal(size=shape) + 1j * rng.normal(size=shape)) / math.sqrt(2)


def fixture(seed=410, blocks=5, trials=3, candidates=2, rows=4, taps=2):
    rng = np.random.default_rng(seed)
    A = .32 * cn(rng, (blocks, candidates, rows, taps))
    if taps == 2:
        A[1, ..., 1] = (.6 + .3j) * A[1, ..., 0]
    y = cn(rng, (blocks, trials, rows))
    G = np.einsum("bcni,bcnj->bcij", A.conj(), A)
    r = np.einsum("bcnk,btn->btck", A.conj(), y)
    centers = np.arange(blocks, dtype=float) * 48 + 24
    centers[-1] -= 7
    return A, y, G, r, centers


def dense_raw_oracle(A, y, centers, width, window, powers, scales):
    blocks, candidates, rows, taps = A.shape
    trials = y.shape[1]
    scales = np.broadcast_to(np.asarray(scales), (trials, candidates))
    result = np.zeros((trials, candidates))
    for first in range(0, blocks, window):
        stop = min(first + window, blocks)
        count = stop - first
        time = centers[first:stop]
        R = np.exp(-.5 * (math.pi * width * (time[:, None] - time[None, :]))**2)
        Q = np.kron(R, np.diag(powers))
        for candidate in range(candidates):
            design = np.zeros((count * rows, count * taps), dtype=complex)
            for j in range(count):
                design[j*rows:(j+1)*rows, j*taps:(j+1)*taps] = A[first+j, candidate]
            covariance = design @ Q @ design.conj().T
            for trial in range(trials):
                alpha = scales[trial, candidate]
                Y = y[first:stop, trial].reshape(-1) / math.sqrt(alpha)
                L = np.linalg.cholesky(np.eye(count * rows) + covariance / alpha)
                z = np.linalg.solve(L, Y)
                result[trial, candidate] += (np.vdot(Y, Y).real - np.vdot(z, z).real
                                            - 2 * np.log(L.diagonal().real).sum())
    return result


class GaussianWindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_raw_dense_covariance_oracle_with_rank_deficiency_partial_window_and_noise_scales(self):
        A, y, G, r, time = fixture()
        scales = np.asarray([[.4, .7], [1., 1.], [2., 3.]])
        powers = (.7, .3)
        for width in (0., .001, .02):
            plan = prepare_windowed_gaussian(G, time, doppler_width_hz=width,
                                             window_blocks=3, stationary_power=powers)
            expected = dense_raw_oracle(A, y, time, width, 3, powers,
                                        scales * plan.whitening_variance_inflation)
            actual = plan.score(r, variance_scale=scales)
            np.testing.assert_allclose(actual.numpy(), expected, rtol=2e-11, atol=2e-11)
            scaled = plan.score(r / scales[None, :, :, None], variance_scale=scales,
                                correlations_scaled=True)
            np.testing.assert_allclose(scaled.numpy(), expected, rtol=2e-11, atol=2e-11)

    def test_zero_width_matches_existing_static_gaussian_filter(self):
        _, _, G, r, time = fixture(blocks=6, trials=4)
        plan = prepare_windowed_gaussian(G, time, doppler_width_hz=0, window_blocks=6)
        model = GaussianStateFilter(GaussianPrior(coherence_seconds=math.inf))
        for j in range(len(time)):
            model.update(torch.from_numpy(r[j] / plan.whitening_variance_inflation),
                         torch.from_numpy(G[j] / plan.whitening_variance_inflation),
                         0. if j == 0 else time[j] - time[j-1])
        np.testing.assert_allclose(plan.score(r).numpy(), model.finish().numpy(), atol=2e-11, rtol=2e-11)

    def test_independent_window_sum_actual_time_gaps_and_zero_information(self):
        _, _, G, r, time = fixture(blocks=5)
        plan = prepare_windowed_gaussian(G, time, doppler_width_hz=.003, window_blocks=2)
        expected = np.zeros((r.shape[1], r.shape[2]))
        for first in range(0, len(time), 2):
            separate = prepare_windowed_gaussian(G[first:first+2], time[first:first+2],
                                                 doppler_width_hz=.003, window_blocks=2)
            expected += separate.score(r[first:first+2]).numpy()
        np.testing.assert_allclose(plan.score(r).numpy(), expected, atol=2e-13)

        _, _, G, r, _ = fixture(blocks=4)
        gapped = np.asarray([0., 48., 1e6, 1e6 + 48])
        whole = prepare_windowed_gaussian(G, gapped, doppler_width_hz=.003, window_blocks=4)
        split = prepare_windowed_gaussian(G, gapped, doppler_width_hz=.003, window_blocks=2)
        np.testing.assert_allclose(whole.score(r).numpy(), split.score(r).numpy(), atol=2e-12)
        regular = prepare_windowed_gaussian(G, np.arange(4) * 48., doppler_width_hz=.003, window_blocks=4)
        self.assertGreater(float(abs(whole.score(r)-regular.score(r)).max()), .01)

        zero = prepare_windowed_gaussian(np.zeros_like(G), gapped, doppler_width_hz=.003)
        np.testing.assert_array_equal(zero.score(np.zeros_like(r)).numpy(), np.zeros((3, 2)))
        no_prior = prepare_windowed_gaussian(G, gapped, doppler_width_hz=0, stationary_power=(0., 0.))
        np.testing.assert_array_equal(no_prior.score(r).numpy(), np.zeros((3, 2)))

    def test_stationary_scale_reuse_and_single_trial_torch_interface(self):
        _, _, G, r, time = fixture(trials=3)
        alpha = np.asarray([.5, 1., 2.])
        reused = prepare_windowed_gaussian(G, time, doppler_width_hz=.002, window_blocks=3)
        actual = reused.score(torch.from_numpy(r / alpha[None, :, None, None]),
                              variance_scale=torch.from_numpy(alpha), correlations_scaled=True)
        for trial, scale in enumerate(alpha):
            direct = prepare_windowed_gaussian(G / scale, time, doppler_width_hz=.002, window_blocks=3)
            expected = direct.score(r[:, trial] / scale)
            self.assertEqual(tuple(expected.shape), (2,))
            np.testing.assert_allclose(actual[trial].numpy(), expected.numpy(), rtol=2e-12, atol=2e-12)

    def test_prior_reuse_matches_raw_oracle_and_exact_calendar_changes(self):
        A, y, G, r, _ = fixture(blocks=11)
        regular = np.arange(11, dtype=float) / 8
        gapped = regular.copy(); gapped[6:] += 1024
        irregular = regular.copy(); irregular[4] += 1 / 64
        adjacent_float = regular.copy(); adjacent_float[4] = np.nextafter(adjacent_float[4], math.inf)
        for centers, prior_solves in ((regular, 2), (gapped, 2), (irregular, 4), (adjacent_float, 4)):
            with self.subTest(centers=centers):
                # The real prior factor is shared; complex information factors
                # still depend on every candidate's Gram and must be recomputed.
                with patch("triad_gaussian_window.np.linalg.eigh", wraps=np.linalg.eigh) as eig:
                    plan = prepare_windowed_gaussian(G, centers, doppler_width_hz=3,
                                                     window_blocks=3, stationary_power=(.7, .3))
                real_solves = sum(call.args[0].ndim == 2 and np.isrealobj(call.args[0])
                                  for call in eig.call_args_list)
                self.assertEqual(real_solves, prior_solves)
                expected = dense_raw_oracle(A, y, centers, 3, 3, (.7, .3),
                                            1.3 * plan.whitening_variance_inflation)
                np.testing.assert_allclose(plan.score(r, variance_scale=1.3).numpy(),
                                           expected, rtol=2e-11, atol=2e-11)

    def test_prior_reuse_preserves_zero_power_and_rank_deficient_windows(self):
        A, y, G, r, _ = fixture(blocks=8)
        centers = np.arange(8, dtype=float) / 8
        for powers in ((1., 0.), (0., 0.)):
            for width in (0., 3.):
                with self.subTest(powers=powers, width=width):
                    plan = prepare_windowed_gaussian(G, centers, doppler_width_hz=width,
                                                     window_blocks=3, stationary_power=powers)
                    expected = dense_raw_oracle(A, y, centers, width, 3, powers,
                                                plan.whitening_variance_inflation)
                    np.testing.assert_allclose(plan.score(r).numpy(), expected, rtol=2e-11, atol=2e-11)
                    # Separate construction cannot reuse a preceding window's
                    # factor. Whole-array versus per-window Gram repair may
                    # differ by rounding and eigenvector phase, so compare
                    # retained ranks, represented covariance and likelihood.
                    independent_score = np.zeros((r.shape[1], r.shape[2]))
                    for window in plan._windows:
                        first, stop = window.first, window.stop
                        independent = prepare_windowed_gaussian(G[first:stop], centers[first:stop],
                            doppler_width_hz=width, window_blocks=3, stationary_power=powers)
                        other = independent._windows[0]
                        self.assertEqual(window.transform.shape, other.transform.shape)
                        np.testing.assert_allclose(window.eigenvalues, other.eigenvalues, rtol=2e-12, atol=2e-13)
                        np.testing.assert_allclose(window.transform @ window.transform.conj().swapaxes(-1, -2),
                            other.transform @ other.transform.conj().swapaxes(-1, -2), rtol=2e-12, atol=2e-13)
                        independent_score += independent.score(r[first:stop]).numpy()
                    np.testing.assert_allclose(plan.score(r).numpy(), independent_score, rtol=2e-12, atol=2e-12)

    def test_fp32_bulk_has_fp64_finish_and_matches_fp64(self):
        _, _, G, r, time = fixture(trials=7)
        plan = PreparedWindowedGaussian(G, time, doppler_width_hz=.001, window_blocks=4)
        precise = plan.score(r)
        bulk = plan.score(r, precision="fp32")
        self.assertEqual(bulk.dtype, torch.float64)
        np.testing.assert_allclose(bulk.numpy(), precise.numpy(), rtol=3e-6, atol=2e-6)
        self.assertIn("FP64", plan.diagnostics()["score_arithmetic"])

    def test_fixed_null_mean_likelihood_diagnostic_and_conservative_scale(self):
        rng = np.random.default_rng(991)
        A = .2 * cn(rng, (4, 2, 3, 2))
        y = cn(rng, (4, 20000, 3))
        G = np.einsum("bcni,bcnj->bcij", A.conj(), A)
        r = np.einsum("bcnk,btn->btck", A.conj(), y)
        plan = prepare_windowed_gaussian(G, np.arange(4) * 64., doppler_width_hz=.002,
                                         window_blocks=2, stationary_power=(.15, .1))
        likelihood = plan.score(r).exp().mean(0).numpy()
        self.assertTrue(np.all(abs(likelihood - 1) < .01), likelihood)
        conservative = plan.score(r, variance_scale=1.8).exp().mean(0).numpy()
        self.assertTrue(np.all(conservative <= 1.005), conservative)
        # This finite moment check is a diagnostic, not a rare-tail certificate.

    def test_static_extreme_noise_scales_match_raw_thin_svd(self):
        # At B=0 the exact sample-domain alternative has only two tap modes.
        # Numerical positive eigenvalues of a rank-two temporal covariance must
        # not create additional evidence when the noise is extremely small.
        A, y, G, r, time = fixture(blocks=16, trials=5)
        powers = np.asarray([.7, .3])
        model = prepare_windowed_gaussian(G, time, doppler_width_hz=0,
                                         window_blocks=16, stationary_power=powers)
        self.assertEqual(model.maximum_window_rank, 2)
        for alpha in (1e-20, 1., 1e20):
            raw = y * math.sqrt(alpha)
            correlations = r * math.sqrt(alpha)
            expected = np.empty((y.shape[1], A.shape[1]))
            for candidate in range(A.shape[1]):
                design = A[:, candidate].reshape(-1, 2) * np.sqrt(powers)
                u, singular, _ = np.linalg.svd(design, full_matrices=False)
                projected = raw.transpose(1, 0, 2).reshape(y.shape[1], -1) @ u.conj()
                effective = alpha * model.whitening_variance_inflation
                energy = abs(projected / math.sqrt(effective))**2
                lam = singular**2
                expected[:, candidate] = (energy * (lam / (effective + lam))).sum(-1) - np.log1p(lam / effective).sum()
            for precision, tolerance in (("fp64", 2e-10), ("fp32", 2e-5)):
                for scaled in (False, True):
                    observed = correlations / alpha if scaled else correlations
                    actual = model.score(observed, variance_scale=alpha,
                                         correlations_scaled=scaled, precision=precision).numpy()
                    np.testing.assert_allclose(actual, expected, atol=tolerance, rtol=tolerance)

    def test_small_positive_width_extreme_noise_is_a_contraction(self):
        A, y, G, r, time = fixture(blocks=16, trials=8)
        # Both scales of tiny positive temporal eigenvalues occurred in the
        # failing real null capture. This bound is in sample space and does not
        # duplicate the receiver's whitening/eigensolver implementation.
        for width in (1e-12, 1e-8, .001):
            model = prepare_windowed_gaussian(G, time, doppler_width_hz=width,
                                             window_blocks=16)
            for alpha in (1e-20, 1., 1e20):
                determinant = sum(np.log1p(w.eigenvalues / alpha).sum(-1) for w in model._windows)
                raw_energy = np.sum(abs(y)**2, axis=(0, 2))[:, None]
                bound = raw_energy - determinant[None]
                reference = model.score(r * math.sqrt(alpha), variance_scale=alpha).numpy()
                self.assertTrue(np.all(reference <= bound + 2e-8))
                fp32 = model.score(r / math.sqrt(alpha), variance_scale=alpha,
                                   correlations_scaled=True, precision="fp32").numpy()
                np.testing.assert_allclose(fp32, reference, atol=3e-5, rtol=3e-6)

    def test_observable_rank_policy_handles_null_and_boundary_modes(self):
        rng = np.random.default_rng(774)
        # A non-axis-aligned exact null checks roundoff leakage after whitening;
        # the two nonzero ratios bracket the declared observation-rank cutoff.
        unitary = np.asarray([[1, 1j], [1j, 1]], complex) / math.sqrt(2)
        for ratio, expected_rank in ((0., 1), (5e-11, 1), (2e-10, 2)):
            A = np.diag(np.sqrt([1., ratio])) @ unitary.conj().T
            grams = np.tile((A.conj().T @ A)[None, None], (8, 1, 1, 1))
            y = cn(rng, (8, 4, 2))
            correlations = np.einsum("ij,btj->bti", A.conj().T, y)[:, :, None]
            for width in (0., 1e-8):
                model = prepare_windowed_gaussian(grams, np.arange(8.),
                    doppler_width_hz=width, window_blocks=8)
                self.assertEqual(model.observable_rank_min, expected_rank)
                self.assertEqual(model.observable_rank_max, expected_rank)
                self.assertLessEqual(model.maximum_relative_discarded_gram_trace, 1e-10)
                covariance = model._whitener @ grams @ model._whitener.conj().swapaxes(-1, -2)
                self.assertLessEqual(float(np.linalg.eigvalsh(covariance).max()), 1.)
                self.assertGreaterEqual(model.whitening_variance_inflation, 1.)
                alpha = 1e-20
                scores = model.score(correlations * math.sqrt(alpha), variance_scale=alpha,
                                     precision="fp32").numpy()
                self.assertTrue(np.isfinite(scores).all())
                self.assertTrue(np.all(scores < 0), scores)
        zero = prepare_windowed_gaussian(np.zeros_like(grams), np.arange(8.),
                                         doppler_width_hz=1e-8, window_blocks=8)
        np.testing.assert_array_equal(zero.score(np.zeros_like(correlations), variance_scale=1e-20), 0.)
        self.assertEqual(zero.observable_rank_max, 0)

    def test_single_block_batched_svd_matches_raw_covariance_with_rank_and_power_edges(self):
        rng=np.random.default_rng(81173)
        A=.32*cn(rng,(1,31,5,2));A[0,::5,:,1]=(.4+.7j)*A[0,::5,:,0];A[0,0]=0.
        y=cn(rng,(1,4,5))
        G=np.einsum("bcni,bcnj->bcij",A.conj(),A)
        r=np.einsum("bcnk,btn->btck",A.conj(),y)
        centers=np.asarray([32.])
        scales=np.asarray([.01,1.,100.,1e4])[:,None]
        for powers in ((.7,.3),(1.,0.),(0.,0.)):
            with self.subTest(powers=powers):
                with patch("triad_gaussian_window.np.linalg.svd",wraps=np.linalg.svd) as svd:
                    plan=prepare_windowed_gaussian(G,centers,doppler_width_hz=.2,window_blocks=1,stationary_power=powers)
                self.assertEqual(svd.call_count,int(any(powers)))
                if any(powers):self.assertEqual(svd.call_args.args[0].shape,(31,2,sum(p>0 for p in powers)))
                expected=dense_raw_oracle(A,y,centers,.2,1,powers,scales*plan.whitening_variance_inflation)
                np.testing.assert_allclose(plan.score(r,variance_scale=scales).numpy(),expected,rtol=2e-11,atol=2e-11)
                self.assertGreater(plan.single_block_batched_svd_work_bytes,0)
                self.assertEqual(plan.diagnostics()['single_block_batched_svd_work_bytes'],plan.single_block_batched_svd_work_bytes)
                with self.assertRaises(MemoryError):
                    prepare_windowed_gaussian(G,centers,doppler_width_hz=.2,window_blocks=1,stationary_power=powers,
                        memory_limit_bytes=plan.preparation_array_bound_bytes-1)

    def test_input_failures_and_memory_preflight(self):
        _, _, G, r, time = fixture()
        for kwargs in ({"window_blocks": 0}, {"doppler_width_hz": -.1},
                       {"stationary_power": (1.,)}, {"relative_tolerance": 0}):
            options = {"doppler_width_hz": .001, **kwargs}
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                prepare_windowed_gaussian(G, time, **options)
        bad = G.copy(); bad[0, 0, 0, 1] += 1
        with self.assertRaisesRegex(ValueError, "Hermitian"):
            prepare_windowed_gaussian(bad, time, doppler_width_hz=.001)
        bad = G.copy(); bad[0, 0] = -np.eye(2)
        with self.assertRaisesRegex(ValueError, "positive semidefinite"):
            prepare_windowed_gaussian(bad, time, doppler_width_hz=.001)
        with self.assertRaisesRegex(ValueError, "increase"):
            prepare_windowed_gaussian(G, time[::-1], doppler_width_hz=.001)
        with patch("triad_gaussian_window.np.linalg.eigh", side_effect=AssertionError("allocated before cap")):
            with self.assertRaises(MemoryError):
                prepare_windowed_gaussian(G, time, doppler_width_hz=.001, memory_limit_bytes=100)
        plan = prepare_windowed_gaussian(G, time, doppler_width_hz=.001)
        for scale in (0., math.nan, np.ones((len(time), 3, 2))):
            with self.assertRaises(ValueError):
                plan.score(r, variance_scale=scale)
        bad = r.copy(); bad[0, 0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            plan.score(bad)
        bounded = prepare_windowed_gaussian(G, time, doppler_width_hz=.001, memory_limit_bytes=100000)
        many = np.zeros((len(time), 1000, 2, 2), dtype=complex)
        with patch("triad_gaussian_window.torch.as_tensor", side_effect=AssertionError("copied before cap")):
            with self.assertRaises(MemoryError):
                bounded.score(many)


if __name__ == "__main__":
    unittest.main()

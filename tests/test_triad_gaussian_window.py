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
            expected = dense_raw_oracle(A, y, time, width, 3, powers, scales)
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
            model.update(torch.from_numpy(r[j]), torch.from_numpy(G[j]),
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

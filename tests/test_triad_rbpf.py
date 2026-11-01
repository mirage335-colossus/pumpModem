#!/usr/bin/env python3
"""Exact-mixture and normalized-particle checks for isolated triad inference."""
from itertools import product
import math
from pathlib import Path
import sys
import unittest

import numpy as np
from numpy.polynomial.hermite import hermgauss
from numpy.polynomial.legendre import leggauss
from scipy.special import logsumexp
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_rbpf import RBPhasePrior, RBPhaseFilter

torch.set_num_threads(2)


def cn(rng, shape):
    return (rng.normal(size=shape)+1j*rng.normal(size=shape))/math.sqrt(2.)


def tensor(x):
    return torch.as_tensor(x, dtype=torch.complex128)


def dense_score(designs, ys, times, phases, prior):
    lengths = [len(A) for A in designs]
    edges = np.cumsum([0]+lengths)
    C = np.zeros((edges[-1], edges[-1]), dtype=complex)
    for i, Ai in enumerate(designs):
        for j, Aj in enumerate(designs):
            dt = times[i]-times[j]
            decay = 1. if math.isinf(prior.coherence_seconds) else math.exp(-abs(dt)/prior.coherence_seconds)
            covariance = np.diag(prior.stationary_power)*decay*np.exp(2j*np.pi*prior.frequency_hz*dt)
            C[edges[i]:edges[i+1], edges[j]:edges[j+1]] = np.exp(1j*(phases[i]-phases[j]))*Ai@covariance@Aj.conj().T
    R = np.eye(len(C))+C
    y = np.concatenate(ys)
    return float(np.vdot(y, y-np.linalg.solve(R, y)).real-np.linalg.slogdet(R)[1])


class RBPhaseTests(unittest.TestCase):
    def test_stratified_frequency_strata_seed_weights_and_paired_rng(self):
        n, bound, seed = 17, .1234, 9021
        prior, phase = GaussianPrior((1.,)), RBPhasePrior(bound, .05, .001)
        kwargs = dict(particles=n, seed=seed, frequency_initialization="stratified")
        first, repeated = RBPhaseFilter(prior, phase, **kwargs), RBPhaseFilter(prior, phase, **kwargs)
        other = RBPhaseFilter(prior, phase, particles=n, seed=seed+1, frequency_initialization="stratified")
        iid = RBPhaseFilter(prior, phase, particles=n, seed=seed)
        r, G = torch.zeros((2, 3, 1), dtype=torch.complex128), tensor([[0.]])
        for f in (first, repeated, other, iid):
            f.update(r, G, 0.)
        fractional_bin = (first.frequency/bound+1)*(n/2)
        strata = torch.arange(n, dtype=torch.float64).expand_as(fractional_bin)
        self.assertTrue(bool(((fractional_bin >= strata) & (fractional_bin < strata+1)).all()))
        torch.testing.assert_close(first.frequency, repeated.frequency, atol=0., rtol=0.)
        self.assertFalse(torch.equal(first.frequency, other.frequency))
        torch.testing.assert_close(first.log_weights.exp().sum(-1), torch.ones(6, dtype=torch.float64), atol=1e-14, rtol=0.)
        self.assertEqual(first.diagnostics()["frequency_initialization"], "stratified")
        # Match the original IID formula exactly, including its draw shape/order.
        generator = torch.Generator(device="cpu").manual_seed(seed)
        expected = (2*torch.rand((6, n), dtype=torch.float64, generator=generator)-1)*bound
        torch.testing.assert_close(iid.frequency, expected, atol=0., rtol=0.)
        self.assertTrue(torch.equal(first._generator.get_state(), iid._generator.get_state()))
        for f in (first, iid):
            f.update(r, G, .7)
        self.assertTrue(torch.equal(first._generator.get_state(), iid._generator.get_state()))
        with self.assertRaisesRegex(ValueError, "frequency_initialization"):
            RBPhaseFilter(prior, frequency_initialization="midpoints")

    def test_frequency_initialization_zero_bound_single_particle_and_default(self):
        prior = GaussianPrior((.4, .6), coherence_seconds=3.)
        r, G = tensor([.7+.1j, -.4j]), tensor([[1., .2j], [-.2j, .8]])
        for bound, n in ((0., 7), (.17, 1)):
            phase = RBPhasePrior(bound, .03, .001)
            filters = [RBPhaseFilter(prior, phase, particles=n, seed=83, **extra) for extra in
                       ({}, {"frequency_initialization": "iid"}, {"frequency_initialization": "stratified"})]
            for i in range(5):
                for f in filters:
                    f.update(r, G, 0. if i == 0 else .3)
            for f in filters[1:]:
                for name in ("mean", "covariance", "frequency", "phase", "log_weights", "log_likelihood_ratio"):
                    torch.testing.assert_close(getattr(f, name), getattr(filters[0], name), atol=0., rtol=0.)
                self.assertTrue(torch.equal(f._generator.get_state(), filters[0]._generator.get_state()))

    def test_stratified_cfo_likelihood_mean_matches_dense_continuous_prior(self):
        # Integrate the independent dense raw-sample Gaussian likelihood over
        # continuous uniform CFO. No Brownian phase or resampling is involved.
        bound = .31
        prior = GaussianPrior((.45,), coherence_seconds=math.inf)
        times = np.array([0., 1.3, 4.7])
        ys = [np.array([value]) for value in (.8+.4j, 1.7-.5j, 1.1+.9j)]
        designs = [np.ones((1, 1), dtype=complex) for _ in times]
        def oracle(order):
            nodes, weights = leggauss(order)
            log_values = [dense_score(designs, ys, times, 2*np.pi*bound*x*times, prior) for x in nodes]
            return float(np.dot(weights, np.exp(log_values))/2)
        expected = oracle(64)
        self.assertAlmostEqual(expected, oracle(96), places=12)
        likelihoods = []
        for seed in range(711, 719):
            f = RBPhaseFilter(prior, RBPhasePrior(bound), particles=8, seed=seed,
                              frequency_initialization="stratified")
            for i, y in enumerate(ys):
                f.update(torch.full((256, 1), y[0], dtype=torch.complex128), tensor([[1.]]),
                         0. if i == 0 else times[i]-times[i-1])
            likelihoods.extend(np.exp(f.finish().numpy()))
        likelihoods = np.array(likelihoods)
        standard_error = likelihoods.std(ddof=1)/math.sqrt(len(likelihoods))
        self.assertLess(abs(likelihoods.mean()-expected), 6*standard_error+1e-7)

    def test_zero_phase_matches_existing_gaussian_with_broadcast_and_independent_grams(self):
        rng = np.random.default_rng(6392)
        prior = GaussianPrior((.3, .7), coherence_seconds=3.4, frequency_hz=.031)
        for shared in (False, True):
            rb = RBPhaseFilter(prior, particles=7)
            reference = GaussianStateFilter(prior)
            for step in range(7):
                A = cn(rng, (3, 2)) if shared else cn(rng, (2, 3, 4, 2))
                G = np.swapaxes(A.conj(), -1, -2)@A
                r = cn(rng, (2, 3, 2))
                dt = 0. if step == 0 else .4+step*.1
                rb.update(tensor(r), tensor(G), dt)
                reference.update(tensor(r), tensor(G), dt)
            np.testing.assert_allclose(rb.finish(), reference.finish(), rtol=2e-12, atol=2e-12)
            self.assertEqual(tuple(rb.covariance.shape), (6, 2, 2))
            self.assertEqual(tuple(rb.mean.shape), (6, 7, 2))
            np.testing.assert_allclose(rb.covariance.reshape(2, 3, 2, 2),
                                       reference.covariance.expand(2, 3, 2, 2), atol=1e-13)

    def test_all_finite_phase_paths_match_dense_gaussian_mixture(self):
        rng = np.random.default_rng(504)
        prior = GaussianPrior((.4, .6), coherence_seconds=2.7, frequency_hz=.021)
        times = [0., .6, 1.8, 2.6]
        designs = [cn(rng, (n, 2)) for n in (2, 3, 2, 4)]
        ys = [cn(rng, (len(A),)) for A in designs]
        increments = np.array(list(product((-.41, .41), repeat=3)))
        phases = np.column_stack((np.zeros(len(increments)), np.cumsum(increments, axis=1)))
        expected = logsumexp([dense_score(designs, ys, times, phi, prior) for phi in phases])-math.log(len(phases))
        receiver = RBPhaseFilter(prior, particles=len(phases))
        for i, (A, y) in enumerate(zip(designs, ys)):
            delta = np.zeros(len(phases)) if i == 0 else increments[:, i-1]
            receiver.update(tensor(A.conj().T@y), tensor(A.conj().T@A),
                0. if i == 0 else times[i]-times[i-1],
                phase_increment_radians=torch.tensor(delta, dtype=torch.float64))
        self.assertAlmostEqual(float(receiver.finish()), expected, places=12)
        self.assertTrue(receiver.diagnostics()["phase_increment_override_used"])
        self.assertIn("conditional", receiver.diagnostics()["calibration_scope"])

    def test_covariance_is_observation_and_particle_independent(self):
        prior = GaussianPrior((.5, .5), coherence_seconds=8.)
        phase = RBPhasePrior(.03, .2)
        a = RBPhaseFilter(prior, phase, particles=4)
        b = RBPhaseFilter(prior, phase, particles=31, seed=7, resampling="systematic")
        G = tensor([[1., .2j], [-.2j, 1.]])
        for i in range(9):
            a.update(tensor([.1, .3j]), G, float(i>0))
            b.update(tensor([3j, 1.]), G, float(i>0))
            torch.testing.assert_close(a.covariance, b.covariance, rtol=0., atol=0.)
        self.assertNotEqual(float(a.finish()), float(b.finish()))

    def test_erasure_advances_time_without_evidence_or_history_growth(self):
        prior = GaussianPrior((1.,), coherence_seconds=5., frequency_hz=.1)
        a, b = RBPhaseFilter(prior, particles=8), RBPhaseFilter(prior, particles=8)
        G, r = tensor([[.6]]), tensor([.7+.2j])
        for x in (a, b): x.update(r, G, 0.)
        before = float(a.scores())
        increment = a.update(tensor([0.]), tensor([[0.]]), .7)
        self.assertAlmostEqual(float(increment), 0., places=14)
        self.assertAlmostEqual(float(a.scores()), before, places=14)
        a.update(r, G, 1.3); b.update(r, G, 2.)
        torch.testing.assert_close(a.finish(), b.finish(), atol=1e-13, rtol=1e-13)
        size = a.diagnostics()["owned_state_bytes"]
        for _ in range(50): a.update(tensor([0.]), tensor([[0.]]), 1000.)
        self.assertEqual(a.diagnostics()["owned_state_bytes"], size)

    def test_integrated_random_walk_frequency_has_correct_covariance(self):
        # Exact Brownian endpoint/integral covariance: Var(F)=s²t,
        # Var(integral F)=s²t³/3, Cov(F,integral F)=s²t²/2.
        sigma, dt, n = .0007, 4., 100000
        f = RBPhaseFilter(GaussianPrior((1.,)), RBPhasePrior(
            frequency_random_walk_hz_sqrt_second=sigma), particles=n, seed=48)
        f.update(tensor([0.]), tensor([[0.]]), dt)
        frequency = f.frequency[0].numpy()
        integral = f.phase[0].numpy()/(2*math.pi)
        actual = np.cov(np.stack((frequency, integral)))
        expected = sigma*sigma*np.array([[dt, dt*dt/2], [dt*dt/2, dt**3/3]])
        np.testing.assert_allclose(actual, expected, rtol=.015, atol=0.)
        self.assertAlmostEqual(float(f.finish()), 0., places=12)

    def test_likelihood_not_log_estimate_converges_with_resampling(self):
        # Independent exact quadrature for a rank-one, three-sample static tap.
        # L(phi)=exp(q*|sum conj(e^jphi)y|²/(1+3q))/(1+3q).
        q, sigma = .45, .72
        y = np.array([.8+.4j, 1.7-.5j, 1.1+.9j])
        def oracle(order):
            x, w = hermgauss(order)
            increments = math.sqrt(2)*sigma*x
            phases = np.stack(np.broadcast_arrays(np.zeros((order, order)), increments[:, None],
                                                  increments[:, None]+increments[None, :]), -1)
            lr = np.exp(q*abs(np.sum(np.exp(-1j*phases)*y, -1))**2/(1+3*q))/(1+3*q)
            return float(np.sum(w[:, None]*w[None, :]*lr)/math.pi)
        expected = oracle(48)
        self.assertAlmostEqual(expected, oracle(64), places=12)
        for mode in ("none", "multinomial", "systematic"):
            f = RBPhaseFilter(GaussianPrior((q,), coherence_seconds=math.inf),
                RBPhasePrior(phase_diffusion_rad_sqrt_second=sigma), particles=16,
                seed=9102, resampling=mode, ess_fraction=1.)
            for i, yi in enumerate(y):
                f.update(torch.full((4096, 1), yi, dtype=torch.complex128), tensor([[1.]]), float(i>0))
            log_likelihood = f.finish().numpy()
            likelihood = np.exp(log_likelihood)
            standard_error = likelihood.std(ddof=1)/math.sqrt(len(likelihood))
            self.assertLess(abs(likelihood.mean()-expected), 6*standard_error+1e-5)
            self.assertLess(log_likelihood.mean(), math.log(likelihood.mean()))
            if mode != "none":
                self.assertGreater(f.diagnostics()["resampling_events_total"], 0)
                self.assertIn("joint", f.diagnostics()["calibration_scope"])

    def test_resampling_reproducibility_and_normalized_empty_observations(self):
        for mode in ("multinomial", "systematic"):
            kwargs = dict(particles=19, seed=631, resampling=mode, ess_fraction=1.)
            prior = GaussianPrior((.5,), coherence_seconds=10.)
            phase = RBPhasePrior(.1, .8)
            a, b = RBPhaseFilter(prior, phase, **kwargs), RBPhaseFilter(prior, phase, **kwargs)
            for i in range(8):
                for f in (a, b): f.update(tensor([[1+.2j], [.3-.4j]]), tensor([[1.]]), float(i>0))
            torch.testing.assert_close(a.finish(), b.finish(), atol=0., rtol=0.)
            torch.testing.assert_close(a.mean, b.mean, atol=0., rtol=0.)
            empty = RBPhaseFilter(prior, phase, **kwargs)
            for _ in range(6): empty.update(tensor([0.]), tensor([[0.]]), 3.)
            self.assertAlmostEqual(float(empty.finish()), 0., places=13)
            self.assertEqual(empty.diagnostics()["resampling_events_total"], 0)

    def test_singular_gram_invalid_inputs_and_allocation_guard(self):
        prior = GaussianPrior((1., 0.), coherence_seconds=math.inf)
        f = RBPhaseFilter(prior, particles=3)
        f.update(tensor([.5, 0.]), tensor([[1., 0.], [0., 0.]]), 0.)
        self.assertTrue(torch.isfinite(f.finish()).all())
        for r, G in ((tensor([float("nan"), 0.]), torch.eye(2)),
                     (tensor([0., 0.]), tensor([[1., 1.], [0., 1.]])),
                     (tensor([0., 0.]), tensor([[1., 0.], [0., -.1]]))):
            with self.assertRaises(ValueError): RBPhaseFilter(prior).update(r, G, 0.)
        with self.assertRaisesRegex(ValueError, "memory"):
            RBPhaseFilter(prior, particles=1000, memory_limit_bytes=4096).update(tensor([0., 0.]), torch.eye(2), 0.)
        with self.assertRaises(ValueError): RBPhasePrior(phase_diffusion_rad_sqrt_second=-1)
        with self.assertRaises(ValueError): RBPhaseFilter(prior, particles=True)
        with self.assertRaises(ValueError): RBPhaseFilter(prior, resampling="maximum")
        with self.assertRaises(ValueError): f.update(tensor([0., 0.]), torch.eye(2), -1.)
        with self.assertRaises(ValueError): f.update(tensor([[0., 0.]]), torch.eye(2), 0.)
        with self.assertRaisesRegex(ValueError, "zero oscillator"):
            RBPhaseFilter(prior, RBPhasePrior(.1), particles=3).update(tensor([0., 0.]), torch.eye(2), 0.,
                phase_increment_radians=torch.zeros(3))

    def test_fp32_bulk_retains_ranking_near_gate_and_long_recurrence(self):
        gate = math.log(1440*4*11*2/.00005)
        targets = np.array([gate+.0005, gate-.01])
        r = tensor(np.sqrt(2*(targets+math.log(2.)))[:, None])
        results = []
        for dtype in (torch.complex64, torch.complex128):
            f = RBPhaseFilter(GaussianPrior((1.,), coherence_seconds=math.inf), particles=5, dtype=dtype)
            f.update(r, tensor([[1.]]), 0.)
            results.append(f.finish().numpy())
            self.assertEqual(f.covariance.dtype, torch.complex128)
        np.testing.assert_allclose(results[0], results[1], atol=1e-5, rtol=0.)
        np.testing.assert_array_equal(results[0] >= gate, results[1] >= gate)
        self.assertEqual(results[0].argmax(), results[1].argmax())
        rng = np.random.default_rng(1315)
        prior = GaussianPrior((.4, .6), coherence_seconds=600.)
        phase = RBPhasePrior(.0001, .001)
        fs = [RBPhaseFilter(prior, phase, particles=11, seed=90, dtype=d) for d in (torch.complex64, torch.complex128)]
        G = tensor([[.12, .01j], [-.01j, .09]])
        for i in range(400):
            r = tensor(cn(rng, (2, 2))*.1+np.array([[.12, .08], [.04, -.03]]))
            for f in fs: f.update(r, G, 0. if i == 0 else 512.)
        np.testing.assert_allclose(fs[0].finish(), fs[1].finish(), atol=2e-4, rtol=2e-5)
        self.assertEqual(int(fs[0].scores().argmax()), int(fs[1].scores().argmax()))

    def test_memory_guard_covers_live_bulk_products_and_resampling_gather(self):
        # Six retained NxK bulk arrays plus an inner-product/gather result
        # already need seven array extents, before any scalar or matrix state.
        # The previous six-array allowance incorrectly admitted this budget.
        taps, particles = 16, 65536
        prior = GaussianPrior((1./taps,)*taps)
        for dtype, element_bytes in ((torch.complex64, 8), (torch.complex128, 16)):
            known_bulk_peak = 7*taps*particles*element_bytes
            for mode in ("none", "multinomial", "systematic"):
                with self.subTest(dtype=dtype, resampling=mode):
                    f = RBPhaseFilter(prior, particles=particles, dtype=dtype,
                                      resampling=mode, memory_limit_bytes=known_bulk_peak)
                    with self.assertRaisesRegex(ValueError, "memory"):
                        f.update(torch.zeros(taps, dtype=dtype), torch.eye(taps, dtype=dtype), 0.)
                    self.assertGreater(f.estimated_array_peak_bytes, known_bulk_peak)
                    self.assertIsNone(f.mean)
                    self.assertIsNone(f._generator)


if __name__ == "__main__":
    unittest.main()

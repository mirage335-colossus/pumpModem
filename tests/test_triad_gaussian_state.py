#!/usr/bin/env python3
"""Small deterministic checks for the canonical Gaussian-state likelihood."""
import math
from pathlib import Path
import sys
import unittest

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_gaussian_state import GaussianPrior, GaussianStateFilter


class GaussianStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def random_complex(self, shape, seed):
        generator = torch.Generator().manual_seed(seed)
        real = torch.randn(shape, dtype=torch.float64, generator=generator)
        imag = torch.randn(shape, dtype=torch.float64, generator=generator)
        return torch.complex(real, imag) / math.sqrt(2)

    def dense_fixture(self, coherence=5., frequency=.037, rank_deficient=False):
        centers = [.5, 2.5, 7.2, 8.9, 12.8]
        basis = self.random_complex((5, 3, 2), 1401)
        if rank_deficient:
            basis[:, :, 1] = basis[:, :, 0] * (.3 + .2j)
            basis[2] = 0
        observations = self.random_complex((5, 3), 1402)
        power = torch.diag(torch.tensor([.4, .7], dtype=torch.complex128))
        covariance = torch.empty((15, 15), dtype=torch.complex128)
        for i in range(5):
            for j in range(5):
                delta = centers[i] - centers[j]
                decay = 1 if math.isinf(coherence) else math.exp(-abs(delta)/coherence)
                phase = complex(math.cos(2*math.pi*frequency*delta), math.sin(2*math.pi*frequency*delta))
                covariance[i*3:(i+1)*3, j*3:(j+1)*3] = basis[i] @ power @ basis[j].mH * (decay*phase)
        noise = 1.7
        normalized = observations.flatten()/math.sqrt(noise)
        total = torch.eye(15, dtype=torch.complex128) + covariance/noise
        cholesky = torch.linalg.cholesky(total)
        solved = torch.cholesky_solve(normalized[:, None], cholesky).squeeze(-1)
        reference = normalized.abs().square().sum() - (normalized.conj()*solved).sum().real - 2*cholesky.diagonal().real.log().sum()
        grams = basis.mH @ basis/noise
        r = (basis.mH @ observations[..., None]).squeeze(-1)/noise
        elapsed = [0.] + [centers[i]-centers[i-1] for i in range(1, len(centers))]
        return r[:, None, :], grams, elapsed, float(reference)

    def test_raw_dense_gaussian_reference(self):
        for deficient in (False, True):
            r, grams, elapsed, reference = self.dense_fixture(rank_deficient=deficient)
            state = GaussianStateFilter(GaussianPrior((.4, .7), 5., .037))
            state.update_many(r, grams, elapsed)
            self.assertAlmostEqual(float(state.finish()), reference, places=11)

    def test_unknown_static_gain_full_window_reference(self):
        r, grams, elapsed, reference = self.dense_fixture(coherence=math.inf, frequency=0)
        state = GaussianStateFilter(GaussianPrior((.4, .7), math.inf))
        state.update_many(r, grams, elapsed)
        self.assertAlmostEqual(float(state.finish()), reference, places=11)
        total_gram = grams.sum(0)
        total_r = r.sum(0).squeeze(0)
        root = torch.diag(torch.tensor([math.sqrt(.4), math.sqrt(.7)], dtype=torch.complex128))
        information = torch.eye(2, dtype=torch.complex128) + root @ total_gram @ root
        covariance = root @ torch.linalg.solve(information, root)
        reference_information = (total_r.conj() * (covariance @ total_r)).sum().real - torch.linalg.slogdet(information).logabsdet
        self.assertAlmostEqual(float(state.finish()), float(reference_information), places=11)

    def test_chunk_partition_and_storage(self):
        r, grams, elapsed, _ = self.dense_fixture()
        whole = GaussianStateFilter(GaussianPrior((.4, .7), 5., .037))
        chunks = GaussianStateFilter(GaussianPrior((.4, .7), 5., .037))
        whole.update_many(r, grams, elapsed)
        chunks.update_many(r[:2], grams[:2], elapsed[:2])
        storage = chunks.diagnostics()["owned_state_bytes"]
        chunks.update_many(r[2:], grams[2:], elapsed[2:])
        self.assertTrue(torch.equal(whole.finish(), chunks.finish()))
        self.assertEqual(storage, chunks.diagnostics()["owned_state_bytes"])

    def test_erasure_gap_advances_state_without_evidence(self):
        prior = GaussianPrior((.7,), 5., .037)
        split = GaussianStateFilter(prior)
        joined = GaussianStateFilter(prior)
        first_r = torch.tensor([[.4+.2j]], dtype=torch.complex128)
        gram = torch.tensor([[.8]], dtype=torch.complex128)
        for state in (split, joined):
            state.update(first_r, gram, 0)
        previous = split.scores().clone()
        increment = split.update(torch.zeros_like(first_r), torch.zeros_like(gram), 3.)
        self.assertEqual(float(increment), 0.)
        self.assertTrue(torch.equal(previous, split.scores()))
        split.update(first_r, gram, 4.)
        joined.update(first_r, gram, 7.)
        self.assertTrue(torch.allclose(split.finish(), joined.finish(), atol=1e-14, rtol=1e-14))
        self.assertTrue(torch.allclose(split.covariance, joined.covariance, atol=1e-14, rtol=1e-14))

    def test_zero_power_is_exact_noise_model(self):
        state = GaussianStateFilter(GaussianPrior((0., 0.)))
        r, grams, elapsed, _ = self.dense_fixture()
        state.update_many(r, grams, elapsed)
        self.assertEqual(float(state.finish()), 0.)


    def test_fp32_against_fp64(self):
        r, grams, elapsed, _ = self.dense_fixture(rank_deficient=True)
        double = GaussianStateFilter(GaussianPrior((.4, .7), 5., .037))
        single = GaussianStateFilter(GaussianPrior((.4, .7), 5., .037), dtype=torch.complex64)
        double.update_many(r, grams, elapsed)
        single.update_many(r.to(torch.complex64), grams.to(torch.complex64), elapsed)
        self.assertTrue(torch.allclose(double.finish(), single.finish(), atol=2e-5, rtol=2e-5))

    def test_fp64_preserves_identity_at_high_snr_collinear_rays(self):
        # At this information scale, FP32 can round I+F.H G F to a
        # singular matrix even though G is a valid rank-one Gram.  The
        # complete CPU receiver predeclares FP64 rather than retrying only
        # failed factorizations: inaccurate FP32 successes are also possible.
        for difference in (0., 1e-10, 1e-8):
            scale = 1e8
            gram = scale*torch.tensor([[1., 1.-difference],
                                       [1.-difference, 1.]], dtype=torch.complex128)
            state = GaussianStateFilter(GaussianPrior((.5, .5), math.inf))
            increment = state.update(torch.zeros((1, 2), dtype=torch.complex128), gram, 0.)
            expected = -math.log1p(.5*scale*(2.-difference))-math.log1p(.5*scale*difference)
            self.assertAlmostEqual(float(increment), expected, delta=2e-8)
            self.assertTrue(torch.isfinite(state.finish()).all())
            self.assertGreaterEqual(float(torch.linalg.eigvalsh(state.covariance).min()), 0.)
            self.assertEqual(state.diagnostics()['state_dtype'], 'torch.complex128')

    def test_invalid_inputs_are_rejected(self):
        for prior in (((), 1.), ((-1.,), 1.), ((1.,), 0.)):
            with self.assertRaises(ValueError):
                GaussianPrior(*prior)
        state = GaussianStateFilter(GaussianPrior((1.,)))
        r = torch.zeros((1, 1), dtype=torch.complex128)
        with self.assertRaises(ValueError):
            state.update(r, torch.tensor([[-1.]], dtype=torch.complex128), 0.)
        with self.assertRaises(ValueError):
            GaussianStateFilter(GaussianPrior((1.,))).update(r, torch.tensor([[float("nan")]], dtype=torch.complex128), 0.)
        with self.assertRaises(ValueError):
            GaussianStateFilter(GaussianPrior((1.,))).update(r, torch.ones((1, 1), dtype=torch.complex128), -1.)

    def test_autograd_inputs_cannot_retain_observation_history(self):
        r = torch.zeros((1, 1), dtype=torch.complex128)
        gram = torch.ones((1, 1), dtype=torch.complex128)
        for r_grad, gram_grad in ((True, False), (False, True)):
            state = GaussianStateFilter(GaussianPrior((1.,)))
            with self.assertRaisesRegex(ValueError, "must not require gradients"):
                state.update(r.clone().requires_grad_(r_grad),
                             gram.clone().requires_grad_(gram_grad), 0.)
            self.assertIsNone(state.mean)


if __name__ == "__main__":
    unittest.main()

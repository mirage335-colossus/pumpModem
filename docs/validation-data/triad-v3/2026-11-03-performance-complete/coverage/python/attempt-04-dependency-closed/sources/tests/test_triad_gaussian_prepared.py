#!/usr/bin/env python3
"""Numerical regression for the prepared public-design Gaussian likelihood."""
from __future__ import annotations

import sys
from pathlib import Path

import torch
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_gaussian_prepared import prepare


def _hermitian_psd(seed, blocks, hypotheses, taps):
    generator = torch.Generator().manual_seed(seed)
    real = torch.randn((blocks, hypotheses, taps, taps), generator=generator, dtype=torch.float64)
    imag = torch.randn(real.shape, generator=generator, dtype=torch.float64)
    a = torch.complex(real, imag)
    gram = a.mH @ a
    gram /= max(1, taps)
    # Deliberate rank loss on one candidate and an all-zero observation gap.
    if taps == 2:
        v = torch.complex(real[1, 0, :, 0], imag[1, 0, :, 0])
        gram[1, 0] = torch.outer(v, v.conj())
    gram[3] = 0
    return gram


class PreparedGaussianTests(unittest.TestCase):
    def test_prepared_matches_reference_for_paired_observations(self):
        for taps in (1, 2):
            for frequency in (0., .0017):
                with self.subTest(taps=taps, frequency=frequency):
                    blocks, batch, hypotheses = 9, 4, 3
                    grams = _hermitian_psd(901+taps, blocks, hypotheses, taps)
                    dts = [0., 1.25, 4., 2.75, 8., 0., 3.5, 4., 4.]
                    prior = GaussianPrior(tuple([1/taps]*taps), 128., frequency)
                    design = prepare(grams, dts, prior)
                    generator = torch.Generator().manual_seed(20267+taps)
                    real = torch.randn((blocks, batch, hypotheses, taps), generator=generator, dtype=torch.float64)
                    imag = torch.randn(real.shape, generator=generator, dtype=torch.float64)
                    observations = torch.complex(real, imag)*.07
                    observations[3] = 0

                    runtime = design.to("cpu", torch.complex128)
                    for index in range(blocks):
                        runtime.update(observations[index])
                    expected = []
                    for b in range(batch):
                        reference = GaussianStateFilter(prior, dtype=torch.complex128, check_inputs=False)
                        for index in range(blocks):
                            reference.update(observations[index, b], grams[index], dts[index])
                        expected.append(reference.finish())
                    expected = torch.stack(expected)
                    torch.testing.assert_close(runtime.scores(), expected, rtol=2e-11, atol=2e-11)
                    self.assertEqual(runtime.blocks, blocks)
                    self.assertTrue(torch.isfinite(runtime.scores()).all())

    def test_prepared_fp32_is_explicit_approximation_and_keeps_fp64_scores(self):
        grams = _hermitian_psd(88, 12, 2, 2)
        dts = [0.] + [4.]*11
        prior = GaussianPrior((.5, .5), 40., .003)
        design = prepare(grams, dts, prior)
        generator = torch.Generator().manual_seed(101)
        r = torch.complex(torch.randn((12, 3, 2, 2), generator=generator),
                          torch.randn((12, 3, 2, 2), generator=generator))*.03
        fp64 = design.to("cpu", torch.complex128)
        fp32 = design.to("cpu", torch.complex64)
        for index in range(12):
            fp64.update(r[index].to(torch.complex128))
            fp32.update(r[index].to(torch.complex64))
        self.assertEqual(fp32.scores().dtype, torch.float64)
        self.assertTrue(torch.isfinite(fp32.scores()).all())
        self.assertLess(torch.max((fp64.scores()-fp32.scores()).abs()), 2e-5)

    def test_fp32_cancellation_sensitive_reduction_tracks_reference(self):
        blocks = 96
        grams = torch.full((blocks, 1, 1), 12., dtype=torch.complex128)
        dts = [0.] + [4.]*(blocks-1)
        prior = GaussianPrior((1.,), 240., .0007)
        design = prepare(grams, dts, prior)
        phase = torch.arange(blocks, dtype=torch.float64)*.002
        r = (torch.polar(torch.full((blocks, 1, 1), 400., dtype=torch.float64), phase[:, None, None]) +
             torch.complex(torch.full((blocks, 1, 1), .25, dtype=torch.float64),
                           torch.full((blocks, 1, 1), -.125, dtype=torch.float64)))
        reference = GaussianStateFilter(prior, dtype=torch.complex64, check_inputs=False)
        prepared = design.to("cpu", torch.complex64, check_inputs=False)
        for index in range(blocks):
            reference.update(r[index].to(torch.complex64), grams[index].to(torch.complex64), dts[index])
            prepared.update(r[index].to(torch.complex64))
        self.assertTrue(torch.isfinite(prepared.scores()).all())
        self.assertLess(torch.max((reference.finish()-prepared.scores()).abs()), 1.)

    def test_prepare_rejects_invalid_design_and_memory_bound(self):
        grams = torch.eye(2, dtype=torch.complex128).expand(5, 2, 2).clone()
        with self.assertRaises(MemoryError):
            prepare(grams, [0., 1., 1., 1., 1.], GaussianPrior((.5,.5)), memory_limit_bytes=1)
        with self.assertRaisesRegex(ValueError, "elapsed-time"):
            prepare(grams, [0., 1.], GaussianPrior((.5,.5)))
        bad = grams.clone(); bad[2, 0, 0] = -1
        with self.assertRaisesRegex(ValueError, "positive semidefinite"):
            prepare(bad, [0., 1., 1., 1., 1.], GaussianPrior((.5,.5)))

    def test_runtime_rejects_wrong_design_extent_and_extra_updates(self):
        grams = torch.eye(1, dtype=torch.complex128).expand(2,1,1).clone()
        design = prepare(grams, [0., 1.], GaussianPrior((1.,)))
        runtime = design.to()
        with self.assertRaisesRegex(ValueError, r"\[batch"):
            runtime.update(torch.ones((3,2,1), dtype=torch.complex128))
        runtime.update(torch.ones((3,1), dtype=torch.complex128))
        with self.assertRaisesRegex(ValueError, "batch or leading shape changed"):
            runtime.update(torch.ones((4,1), dtype=torch.complex128))
        runtime.update(torch.ones((3,1), dtype=torch.complex128))
        with self.assertRaisesRegex(ValueError, "more observations"):
            runtime.update(torch.ones((3,1), dtype=torch.complex128))


if __name__ == "__main__":
    unittest.main()

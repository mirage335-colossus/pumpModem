#!/usr/bin/env python3
"""Independent raw-covariance checks for the conditional Gaussian bounds."""
import math
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_gaussian_state import GaussianPrior
from triad_gaussian_bounds import GaussianDesignMoments, cantelli_miss_upper, chernoff_miss_upper, evaluate_workspace, gaussian_null_calibration, log_affinity, reception_bounds


class GaussianBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def fixtures(self, static=False, deficient=False):
        generator = torch.Generator().manual_seed(8801)
        real = torch.randn((3, 2, 2), dtype=torch.float64, generator=generator)
        imag = torch.randn((3, 2, 2), dtype=torch.float64, generator=generator)
        left = torch.complex(real, imag)*.4
        right = left.clone()
        right[:, :, 0] = right[:, :, 0].roll(1, dims=0)*(.2+.9j)
        right[:, :, 1] *= -.6j
        if deficient:
            left[:, :, 1] = left[:, :, 0]*.5j
            right[:, :, 1] = right[:, :, 0]*.5j
            left[1] = 0; right[1] = 0
        times = [0., 1.5, 4.]
        prior = GaussianPrior((.4, .6), math.inf if static else 2., .031)
        def covariance(basis):
            result = torch.empty((6, 6), dtype=torch.complex128)
            power = torch.diag(torch.tensor(prior.stationary_power, dtype=torch.complex128))
            for i in range(3):
                for j in range(3):
                    delta = times[i]-times[j]
                    decay = 1. if static else math.exp(-abs(delta)/prior.coherence_seconds)
                    phase = torch.exp(torch.tensor(2j*math.pi*prior.frequency_hz*delta, dtype=torch.complex128))
                    result[2*i:2*i+2, 2*j:2*j+2] = basis[i]@power@basis[j].mH*decay*phase
            return result
        return left, right, times, prior, covariance(left), covariance(right)

    def design(self, basis, times, prior):
        state = GaussianDesignMoments(prior)
        for index, time in enumerate(times):
            state.update(basis[index].mH@basis[index], 0. if index == 0 else time-times[index-1])
        return state.result()

    def test_kl_variance_and_affinity_against_dense_covariance(self):
        for static in (False, True):
            for deficient in (False, True):
                left, right, times, prior, cb, cw = self.fixtures(static, deficient)
                mb = self.design(left, times, prior)
                mw = self.design(right, times, prior)
                average = self.design(torch.cat((left, right), dim=-1), times,
                    GaussianPrior(tuple(value*.5 for value in prior.stationary_power)*2,
                                  prior.coherence_seconds, prior.frequency_hz))
                identity = torch.eye(6, dtype=torch.complex128)
                db = float(torch.linalg.slogdet(identity+cb).logabsdet)
                dw = float(torch.linalg.slogdet(identity+cw).logabsdet)
                da = float(torch.linalg.slogdet(identity+(cb+cw)*.5).logabsdet)
                self.assertAlmostEqual(mb["trace_signal_covariance"], float(cb.trace().real), places=11)
                self.assertAlmostEqual(mb["h1_log_lr_mean"], float(cb.trace().real)-db, places=11)
                self.assertAlmostEqual(mb["h1_log_lr_variance"], float(torch.trace(cb@cb).real), places=11)
                self.assertAlmostEqual(mb["logdet_noise_plus_signal"], db, places=11)
                self.assertAlmostEqual(log_affinity(mb["logdet_noise_plus_signal"], mw["logdet_noise_plus_signal"], average["logdet_noise_plus_signal"]), .5*db+.5*dw-da, places=11)

    def test_identical_design_affinity_and_zero_signal(self):
        basis, _, times, prior, _, _ = self.fixtures()
        one = self.design(basis, times, prior)
        self.assertAlmostEqual(log_affinity(one["logdet_noise_plus_signal"], one["logdet_noise_plus_signal"], one["logdet_noise_plus_signal"]), 0.)
        zero = self.design(torch.zeros_like(basis), times, prior)
        self.assertEqual(zero["h1_log_lr_mean"], 0.)
        self.assertEqual(zero["h1_log_lr_variance"], 0.)
        self.assertEqual(cantelli_miss_upper(0., 0., 1.), 1.)

    def test_scaled_covariance_chernoff_against_dense(self):
        basis, _, times, prior, covariance, _ = self.fixtures()
        scales = (.001, .03, .3, 1., 3., 10.)
        state = GaussianDesignMoments(prior, chernoff_s=scales)
        for index in range(len(times)):
            state.update(basis[index].mH@basis[index], 0. if index == 0 else times[index]-times[index-1])
        result = state.result()
        dense = [float(torch.linalg.slogdet(torch.eye(6, dtype=torch.complex128)+scale*covariance).logabsdet) for scale in scales]
        torch.testing.assert_close(torch.tensor(result["scaled_logdet_noise_plus_signal"], dtype=torch.float64),
                                   torch.tensor(dense, dtype=torch.float64), atol=1e-12, rtol=1e-12)
        upper, selected, log_upper = chernoff_miss_upper(result["logdet_noise_plus_signal"], scales, result["scaled_logdet_noise_plus_signal"], -.2)
        expected = min((min(0., scale*(-.2+result["logdet_noise_plus_signal"])-value), scale) for scale, value in zip(scales, dense))
        self.assertEqual(selected, expected[1])
        self.assertAlmostEqual(log_upper, expected[0], places=11)
        self.assertAlmostEqual(upper, math.exp(expected[0]), places=11)

    def test_chernoff_avoids_static_cantelli_floor_and_zero_energy(self):
        energy, threshold = 1e6, 20.
        scales = (.001, .003, .01, .03, .1, .3, 1.)
        cantelli = cantelli_miss_upper(energy-math.log1p(energy), energy**2, threshold)
        chernoff, _, _ = chernoff_miss_upper(math.log1p(energy), scales,
                                            [math.log1p(scale*energy) for scale in scales], threshold)
        self.assertGreater(cantelli, .49)
        self.assertLess(chernoff, .0001)
        self.assertEqual(chernoff_miss_upper(0., scales, [0.]*len(scales), threshold)[0], 1.)
        self.assertAlmostEqual(cantelli_miss_upper(1e200, 1e308, 0.)/1e-92, 1., places=12)

    def test_null_fractional_moments_against_dense_eigenvalues(self):
        basis, _, times, prior, covariance, _ = self.fixtures()
        scales = (.001, .01, .1, .3)
        state = GaussianDesignMoments(prior, chernoff_s=scales)
        for index in range(len(times)):
            state.update(basis[index].mH@basis[index], 0. if index == 0 else times[index]-times[index-1])
        moment = state.result()
        eigenvalues = torch.linalg.eigvalsh(covariance).clamp_min(0)
        calibrated = gaussian_null_calibration([moment], 20.)
        for row in calibrated["candidates"][0]["evaluated_fractional_moments"]:
            t = row["t"]
            reference = -t*torch.log1p(eigenvalues).sum()-torch.log1p(-t*eigenvalues/(1+eigenvalues)).sum()
            self.assertAlmostEqual(row["log_null_fractional_moment"], float(reference), places=11)

    def test_calibrated_common_gate_bounds_exact_rank_one_null(self):
        scales = (.001, .003, .01, .03, .1, .3)
        baseline = 20.
        for energies in ((0.,), (.01,), (1.,), (10.,), (1e6,), (.01, 1e6)):
            moments = [{"logdet_noise_plus_signal": math.log1p(energy), "chernoff_s": list(scales),
                        "scaled_logdet_noise_plus_signal": [math.log1p(scale*energy) for scale in scales]}
                       for energy in energies]
            calibrated = gaussian_null_calibration(moments, baseline)
            gate = calibrated["common_threshold"]
            self.assertLessEqual(gate, baseline)
            self.assertGreaterEqual(gate, max(row["candidate_gate"] for row in calibrated["candidates"]))
            for energy, row in zip(energies, calibrated["candidates"]):
                actual_tail = 0. if energy == 0 else math.exp(-(gate+math.log1p(energy))/(energy/(1+energy)))
                self.assertLessEqual(actual_tail, math.exp(-baseline)*(1+1e-10))
                self.assertLessEqual(row["log_null_tail_upper_at_common_gate"], -baseline+1e-10)
        self.assertGreater(gaussian_null_calibration(moments[1:], baseline)["reduction_from_markov"], 0.)

    def test_correctness_identity_and_union_accounting(self):
        moments = [{"h1_log_lr_mean": 100., "h1_log_lr_variance": 4.} for _ in range(3)]
        pairs = {(0, 1): math.log(.001), (0, 2): math.log(.1), (1, 2): math.log(.002)}
        bounds = reception_bounds(moments, pairs, [(0, 0), (1, 0), (0, 1)], 20.)
        miss = 4/(4+80**2)
        self.assertAlmostEqual(bounds[0]["correct_bit_admission_lower"], 1-miss-.001)
        self.assertAlmostEqual(bounds[0]["correct_bit_epoch_admission_lower"], 1-miss-.101)
        self.assertGreater(bounds[0]["correct_bit_admission_lower"], bounds[0]["correct_bit_epoch_admission_lower"])

    def test_invalid_and_autograd_inputs(self):
        state = GaussianDesignMoments(GaussianPrior((1.,)))
        for gram in (torch.tensor([[-1.]], dtype=torch.complex128),
                     torch.ones((1,1), dtype=torch.complex128, requires_grad=True)):
            with self.assertRaises(ValueError):
                state.update(gram, 0.)
        with self.assertRaises(ValueError):
            cantelli_miss_upper(1., -1., 0.)

    def test_public_only_workspace_and_operational_family(self):
        # This object provides no signal means or realized channel energies.
        # A successful evaluation therefore cannot depend on those fields.
        grams = np.array([[[1., .2j], [-.2j, .8]], [[.4, 0.], [0., .7]]], dtype=np.complex128)
        tile = SimpleNamespace(gram=grams, intervals=np.array([[0,2], [2,4]], dtype=np.uint64),
                               valid_rows=np.array([2,2], dtype=np.uint64))
        workspace = SimpleNamespace(column_mapping=((0,), (1,)),
            candidates=[{"bit": 0, "epoch_offset_seconds": 0}, {"bit": 1, "epoch_offset_seconds": 0}],
            sample_rate=1., sample_count=4, block_count=2, iter_public=lambda: iter([tile]))
        one = evaluate_workspace(workspace, taps=1, cn0_db_hz=0)
        three = evaluate_workspace(workspace, taps=1, cn0_db_hz=0, duration_alternatives=3)
        self.assertEqual(one["observation_blocks"], 2)
        self.assertEqual(one["total_family_tests"], 2*1440)
        self.assertAlmostEqual(three["threshold"]-one["threshold"], math.log(3))
        self.assertFalse(one["hidden_channel_means_or_signal_energies_read"])


if __name__ == "__main__":
    unittest.main()

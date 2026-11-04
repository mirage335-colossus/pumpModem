#!/usr/bin/env python3
"""Small hardware-free tests for actual-kernel physical receiver adaptation."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_fft_acquire import FFTWorkspace, score_workspace
from triad_live_receiver import (EmpiricalReceiver, binomial_interval, empirical_decision,
                                 evaluate, freeze_calibration)


def fixtures(seed=101):
    rng = np.random.default_rng(seed)
    basis = np.zeros((2, 128, 2), np.complex128)
    basis[:, 12:86] = (rng.normal(size=(2, 74, 2)) + 1j * rng.normal(size=(2, 74, 2))) * .3
    noise = (rng.normal(size=128) + 1j * rng.normal(size=128)) * .05
    return basis, noise


class PhysicalReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def receiver(self, basis, **kwargs):
        return EmpiricalReceiver(basis, 128., np.arange(13), reference_variance=.005,
                                 memory_limit_bytes=32 * 1024 * 1024,
                                 preprocessing_identity="fixed-test-bridge-v1", **kwargs)

    def test_actual_kernel_parity_reuse_and_both_bits_search(self):
        basis, noise = fixtures()
        rx = self.receiver(basis, precision="fp64")
        cached_fft = rx._workspace.template_fft
        cached_gram = rx._workspace.gram
        for bit, lag in ((0, 3), (1, 9), (0, 6)):
            y = noise.copy()
            y[lag:] += basis[bit, :-lag] @ np.asarray([1.2 + .5j, -.7 + .2j])
            actual = rx.score(y, retain_group_scores=True)
            fresh = self.receiver(basis, precision="fp64").score(y, retain_group_scores=True)
            np.testing.assert_allclose(actual["bit_log_likelihoods"], fresh["bit_log_likelihoods"], rtol=0., atol=0.)
            np.testing.assert_allclose(actual["group_bit_log_likelihoods"], fresh["group_bit_log_likelihoods"], rtol=0., atol=0.)
            direct_work = FFTWorkspace(rx._sampled, np.arange(13), precision="fp64", memory_limit_bytes=32 * 1024 * 1024)
            direct_work.observation = y
            direct_work.energy = float(np.sum(abs(y)**2))
            raw = score_workspace(direct_work, rx._config)
            np.testing.assert_allclose(actual["bit_log_likelihoods"], raw["bit_log_likelihoods"], atol=1e-10)
            self.assertEqual(actual["bit"], bit)
            self.assertEqual(actual["best_scoring_group_for_winning_bit"], lag)
            self.assertEqual(actual["geometry"]["group_count"], 13)
        self.assertIs(cached_fft, rx._workspace.template_fft)
        self.assertIs(cached_gram, rx._workspace.gram)

    def test_physical_results_do_not_emit_analytic_admissions(self):
        basis, noise = fixtures()
        result = self.receiver(basis).score(noise)
        self.assertNotIn("strict", result)
        self.assertNotIn("relaxed", result)
        self.assertNotIn("accepted", result)
        self.assertFalse(result["calibration"]["analytic_admission_validated"])
        self.assertNotIn("gaussian_projection_supported", json.dumps(result))
        self.assertNotIn("log_threshold", json.dumps(result))

    def test_invalid_inputs_and_complete_support_guard(self):
        basis, noise = fixtures()
        with self.assertRaises(ValueError):
            self.receiver(basis.real)
        with self.assertRaises(ValueError):
            self.receiver(basis[:, :, :1])
        bad = basis.copy(); bad[0, 20, 0] = np.nan
        with self.assertRaises(ValueError):
            self.receiver(bad)
        bad = basis.copy(); bad[0, -1, 0] = 1
        with self.assertRaisesRegex(ValueError, "complete public template support"):
            self.receiver(bad)
        rx = self.receiver(basis)
        with self.assertRaises(ValueError):
            rx.score(noise[:-1])
        with self.assertRaises(ValueError):
            rx.score(np.full(128, complex(float("nan"))))
        with self.assertRaises(ValueError):
            self.receiver(basis, valid=np.full(128, 2, np.uint8))

    def test_owned_templates_and_identity(self):
        basis, noise = fixtures()
        rx = self.receiver(basis)
        original = rx.score(noise)
        basis[:] = 0
        np.testing.assert_equal(original["bit_log_likelihoods"], rx.score(noise)["bit_log_likelihoods"])
        other, _ = fixtures(102)
        gate = rx.freeze_calibration(np.arange(19))
        with self.assertRaisesRegex(ValueError, "identity differs"):
            empirical_decision(self.receiver(other).score(noise), gate)


class RankGateTests(unittest.TestCase):
    def outcome(self, score):
        return dict(receiver_id="fixed", bit=1, score=score, opposite_bit_margin=1.)

    def test_resolution_and_conservative_ties(self):
        with self.assertRaisesRegex(ValueError, "too few"):
            freeze_calibration(np.arange(18), receiver_id="fixed", alpha=.05)
        gate = freeze_calibration(np.arange(19), receiver_id="fixed", alpha=.05)
        self.assertFalse(empirical_decision(self.outcome(18.), gate)["accepted"])
        accepted = empirical_decision(self.outcome(19.), gate)
        self.assertTrue(accepted["accepted"])
        self.assertEqual(accepted["rank_p_value"], .05)
        ties = freeze_calibration(np.ones(39), receiver_id="fixed", alpha=.05)
        self.assertEqual(empirical_decision(self.outcome(1.), ties)["rank_p_value"], 1.)
        self.assertTrue(empirical_decision(self.outcome(1.01), ties)["accepted"])

    def test_exact_exchangeable_rank_enumeration(self):
        # Each possible held-out rank has equal probability with no ties.
        accepted = []
        for held_out in range(40):
            gate = freeze_calibration([i for i in range(40) if i != held_out], receiver_id="fixed", alpha=.05)
            accepted.append(empirical_decision(self.outcome(held_out), gate)["accepted"])
        self.assertEqual(sum(accepted), 2)

    def test_evaluation_and_uncertainty_remain_separate(self):
        gate = freeze_calibration(np.arange(19), receiver_id="fixed")
        accepted = empirical_decision(self.outcome(20), gate)
        rejected = empirical_decision(self.outcome(1), gate)
        self.assertEqual(evaluate(accepted, 1), "correct_admission")
        self.assertEqual(evaluate(accepted, 0), "wrong_admission")
        self.assertEqual(evaluate(accepted, None), "false_admission")
        self.assertEqual(evaluate(rejected, 1), "miss")
        self.assertEqual(evaluate(rejected, None), "correct_rejection")
        interval = binomial_interval(0, 20)
        self.assertEqual(interval["interval"][0], 0.)
        self.assertAlmostEqual(interval["interval"][1], 1 - .025**(1 / 20))


if __name__ == "__main__":
    unittest.main()

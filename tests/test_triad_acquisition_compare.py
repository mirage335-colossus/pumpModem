#!/usr/bin/env python3
"""Focused exhaustive-family parity check for the optional acquisition screen."""
import argparse
import importlib.util
from pathlib import Path
import unittest

import torch


SOURCE = Path(__file__).resolve().parents[1]/"tools"/"triad-acquisition-compare.py"
SPEC = importlib.util.spec_from_file_location("triad_acquisition_compare_test", SOURCE)
study = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(study)


class CompareTests(unittest.TestCase):
    def test_seed_splits_have_distinct_reproducible_domains(self):
        # The borrowed GPU CLI seed helper only distinguishes calibration and
        # holdout, causing the original development/holdout collision. This
        # comparator has its own explicit domain for every accepted split.
        seeds = {split: [study.observation_seed(710191, split, trial) for trial in range(16)]
                 for split in ("development", "calibration", "holdout")}
        self.assertEqual(len({seed for values in seeds.values() for seed in values}), 48)
        for split, values in seeds.items():
            self.assertEqual(values, [study.observation_seed(710191, split, trial) for trial in range(16)])
        self.assertNotEqual(study.observation_seed(710191, "holdout", 0),
                            study.observation_seed(710192, "holdout", 0))
        with self.assertRaises(ValueError):
            study.observation_seed(710191, "typo", 0)

    def setUp(self):
        torch.set_num_threads(1)
        study.fft.shared._configure_torch()
        generator = torch.Generator().manual_seed(1991)
        basis = torch.complex(torch.randn((2, 48, 1), generator=generator),
                              torch.randn((2, 48, 1), generator=generator))
        basis[:, :3] = 0
        basis[:, 32:] = 0
        params = [{"bit": bit, "arrival_seconds": .1, "frequency_hz": 0.,
                   "epoch_offset_seconds": 0, "clock_ppm": 0.} for bit in (0, 1)]
        data = {"basis_shape": (2, 48, 1), "sample_rate": 20., "candidateparams": params,
                "bitlabels": [0, 1], "basis": basis, "signal": basis[0, :, 0]}
        args = argparse.Namespace(device="cpu", lag_max_seconds=.3,
            lag_step_samples=2, frequency_step=.1, frequency_bound=.2,
            attempts_per_horizon=1440, duration_alternatives=1, model_alternatives=1,
            false_per_horizon=1e-4, frequency_tile=2, memory_gib=.1, batch=3,
            reference_points=2, reference_trials=2, seed=1992)
        self.work = study.fft.Workspace(data, {}, args)
        self.observations = torch.complex(torch.randn((3, 48), generator=generator),
                                           torch.randn((3, 48), generator=generator))
        # A shifted signal at the largest lag and nonzero CFO exercises the
        # absolute receiver-time phasor and complete-support conventions.
        lag = self.work.lags[-1]
        shifted = torch.zeros(48, dtype=torch.complex64)
        shifted[lag:] = basis[1, :48-lag, 0]
        shifted *= self.work.phasors[-1].conj()
        self.observations[0] = shifted*3

    def test_full_family_direct_fft_reference_and_existing_scanner(self):
        bank = study.direct_bank(self.work)
        direct = study.score_direct(self.observations, bank, self.work)
        cached = study.score_fft(self.observations, self.work)
        recomputed = study.score_fft(self.observations, self.work, recompute_templates=True)
        reference = study.reference_scores(self.observations, self.work)
        torch.testing.assert_close(direct, reference, atol=3e-4, rtol=3e-6)
        torch.testing.assert_close(cached, reference, atol=3e-4, rtol=3e-6)
        torch.testing.assert_close(recomputed, cached, atol=0, rtol=0)
        for score in (direct, cached, recomputed):
            self.assertEqual(tuple(score.shape), (3, 5, 2, 4))
            self.assertTrue(torch.equal(score.flatten(1).argmax(1), reference.flatten(1).argmax(1)))
        best, code, _ = self.work.scan(self.observations, [[], [], []])
        torch.testing.assert_close(best, cached.flatten(1).max(1).values, atol=0, rtol=0)
        self.assertTrue(torch.equal(code, cached.flatten(1).argmax(1)))
        self.assertEqual(self.work.decode_index(code[0]), (4, 1, 3))

    def test_complete_support_guard(self):
        # Existing Workspace rejects a lag whose full transmitted support
        # would be discarded rather than silently renormalizing/truncating it.
        self.assertLess(self.work.support[0][-1]+self.work.lags[-1], self.work.samples)
        with self.assertRaises(ValueError):
            args = argparse.Namespace(device="cpu", lag_max_seconds=2.,
                lag_step_samples=2, frequency_step=.1, frequency_bound=.2,
                attempts_per_horizon=1, duration_alternatives=1, model_alternatives=1,
                false_per_horizon=.01, frequency_tile=2, memory_gib=.1, batch=1,
                reference_points=2, reference_trials=1, seed=1992)
            data = {"basis_shape": (2, 48, 1), "sample_rate": 20.,
                    "candidateparams": self.work.params, "bitlabels": self.work.labels,
                    "basis": self.work.templates_cpu[:, :, None], "signal": self.work.signal}
            study.fft.Workspace(data, {}, args)

    def test_admission_metric_does_not_claim_synchronization(self):
        scores = study.score_fft(self.observations[:2], self.work)
        report = study.metrics(scores, self.work, 1, 1)
        self.assertIn("correct_bit_admission_wilson_95pct_ci", report)
        self.assertNotIn("correct_reception_wilson_95pct_ci", report)
        self.assertIsNone(report["synchronization_success"])

    def test_explicit_non_gaussian_or_unsupported_admission_is_rejected(self):
        study.validate_linear_noise_model({})
        study.validate_linear_noise_model({"gaussian_projection_supported": True,
                                          "analytic_admission_supported": True})
        for flag in ("gaussian_projection_supported", "analytic_admission_supported"):
            with self.assertRaises(ValueError):
                study.validate_linear_noise_model({flag: False})
        with self.assertRaises(ValueError):
            study.validate_linear_noise_model({"sampled_frontend": {"active": True}})


if __name__ == "__main__":
    with torch.inference_mode():
        unittest.main()

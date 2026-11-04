#!/usr/bin/env python3
"""Admission and precision regressions for the isolated state study."""
import importlib.util
import math
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.stats import gamma
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
spec = importlib.util.spec_from_file_location('state_study', Path(__file__).resolve().parents[1]/'tools/triad-state-study.py')
study = importlib.util.module_from_spec(spec)
spec.loader.exec_module(study)


class StudyTests(unittest.TestCase):
    def test_gamma_tail_underflow_preserves_bit_ranking(self):
        scores = -study.gamma_log_survival(np.array([[1000., 1500.]]), np.array([1, 1]))
        np.testing.assert_array_equal(scores, [[1000., 1500.]])
        self.assertEqual(study.describe(scores, 1, 20.)['correct_accepted'], 1)
        value = study.gamma_log_survival(np.array([[2000.]]), np.array([2]))[0, 0]
        self.assertAlmostEqual(value, -2000+math.log1p(2000), places=11)
        x = np.array([[20., 1000., 100000.]])
        ranks = np.array([4, 900, 100000])
        np.testing.assert_allclose(study.gamma_log_survival(x, ranks), gamma.logsf(x, ranks), rtol=1e-13)

    def test_model_and_candidate_mixtures_are_normalized(self):
        # A mixture of uninformative proper likelihoods remains LR=1;
        # counting more alternatives must not add fabricated evidence.
        scores = torch.zeros((7, 9, 6), dtype=torch.float64)
        mixed = study.bit_mixture(study.logmean(scores, 0), torch.tensor([0, 1, 0, 1, 0, 1]))
        torch.testing.assert_close(mixed, torch.zeros((9, 2), dtype=torch.float64))


if __name__ == '__main__':
    torch.set_num_threads(2)
    unittest.main()

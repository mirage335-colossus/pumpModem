#!/usr/bin/env python3
"""Checks for the conservative ADC rounding-to-analog admission implication."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import triad_adc_bound as study


class BoundTests(unittest.TestCase):
    def test_seed_domains_are_distinct(self):
        self.assertEqual(len({study.seed_for(195071, split, i)
                              for split in study.SPLITS for i in range(20)}), 60)

    def test_rounding_error_and_overload(self):
        x = np.array([[.1+.3j, -.2-.4j], [20.+0j, 0j]])
        rounded, rails, delta, clips = study.quantize(x, 8, 1.)
        self.assertEqual(clips.tolist(), [0, 1])
        self.assertEqual(rails.tolist(), [0, 1])
        self.assertLessEqual(np.linalg.norm(rounded[0]-x[0]), study.rounding_norm_bound(2, delta))
        self.assertEqual(rounded[1, 0].real, 127*delta)

    def test_sampled_bound_for_complex_correlations(self):
        rng = np.random.default_rng(121)
        q = rng.normal(size=(3,127))+1j*rng.normal(size=(3,127))
        y = rng.normal(size=(16,127))+1j*rng.normal(size=(16,127))
        rounded, rails, delta, clips = study.quantize(y, 12, 8.)
        self.assertFalse(clips.any())
        self.assertFalse(rails.any())
        variance = 2.
        analog = study.normalized_amplitudes(q, y, variance)
        adc = study.normalized_amplitudes(q, rounded, variance)
        margin = study.rounding_norm_bound(127, delta)/np.sqrt(variance)
        self.assertTrue(np.all(analog >= adc-margin-1e-14))

    def test_admitted_candidate_implication_near_gate(self):
        q = np.ones((1,16), dtype=np.complex128)/4
        g = study.gate(16, 1/128, 1., 1, 1e-4, 86400., 60.)
        for amplitude in np.linspace(np.sqrt(g["ideal_analog_energy_threshold"])-.1,
                                     g["quantized_amplitude_threshold"]+.1, 401):
            y = amplitude*q
            rounded, rails, _, clips = study.quantize(y, 12, 16.)
            self.assertFalse(clips.any())
            self.assertFalse(rails.any())
            adc = study.normalized_amplitudes(q, rounded, 1.)[0,0]
            analog = study.normalized_amplitudes(q, y, 1.)[0,0]
            if adc >= g["quantized_amplitude_threshold"]:
                self.assertGreaterEqual(analog, np.sqrt(g["ideal_analog_energy_threshold"]))

    def test_family_and_invalid_parameters(self):
        g = study.gate(100, .001, 2., 2, 1e-4, 86400., 60., 3)
        self.assertEqual(g["full_declared_family_tests"], 8640)
        self.assertGreater(g["gate_energy_inflation_db"], 0)
        with self.assertRaises(ValueError):
            study.quantize(np.zeros((1,3)), 1, 2)

    def test_observable_rail_rule_catches_all_true_overloads(self):
        x = np.array([[-1.2+0j], [-1.+0j], [.9921875+0j], [1.2+0j]])
        _, rails, _, clips = study.quantize(x, 8, 1.)
        self.assertTrue(np.all(rails[clips > 0] > 0))
        self.assertEqual(clips.tolist(), [1, 0, 0, 1])
        self.assertEqual(rails.tolist(), [1, 1, 1, 1])


if __name__ == "__main__":
    unittest.main()

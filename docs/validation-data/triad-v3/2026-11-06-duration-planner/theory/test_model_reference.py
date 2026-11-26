"""Small reproducibility checks; no receiver, waveform or 100k-draw campaign."""
import importlib.util
import json
import math
from pathlib import Path
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('duration_reference',HERE/'model-reference.py')
reference=importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class DurationReferenceTests(unittest.TestCase):
    def assert_nested_close(self,actual,expected):
        if isinstance(expected,dict):
            self.assertEqual(set(actual),set(expected))
            for key in expected:self.assert_nested_close(actual[key],expected[key])
        elif isinstance(expected,list):
            self.assertEqual(len(actual),len(expected))
            for a,b in zip(actual,expected):self.assert_nested_close(a,b)
        elif type(expected) in (int,float):
            self.assertTrue(math.isclose(actual,expected,rel_tol=5e-12,abs_tol=1e-13),
                            (actual,expected))
        else:self.assertEqual(actual,expected)

    def test_exact_historical_inputs(self):
        for name in ('hf-native-plan.json','model-reference.json'):
            self.assertEqual(reference.sha(HERE/name),reference.HISTORICAL_SOURCE_SHA256[name])
        original=json.loads((HERE/'model-reference.json').read_text())
        self.assertEqual(original['source_sha256'],
                         reference.HISTORICAL_SOURCE_SHA256['frozen-duration-model.py'])
        self.assertEqual([p['trials'] for p in original['points']],[100000]*6)

    def test_all_six_deterministic_points_and_original_intervals(self):
        original=json.loads((HERE/'model-reference.json').read_text())
        current=reference.report()
        self.assertIn('no Monte Carlo draws',current['reference_mode'])
        for actual,expected in zip(current['points'],original['points']):
            for name in ('cn0','data_seconds','threshold','prediction',
                         'reference_eigenvalues','energy_trace','likelihood_mean'):
                self.assert_nested_close(actual[name],expected[name])
            self.assertIsNone(actual['misses'])
            self.assertIsNone(actual['miss_probability_ci95'])
            self.assertEqual(actual['trials'],0)
            self.assert_nested_close(reference.miss_interval(expected['misses'],expected['trials']),
                                     expected['miss_probability_ci95'])
        json.dumps(current,allow_nan=False)

    def test_small_seeded_reference_matches_original_program(self):
        # Independently obtained with archived model-reference.py and exact
        # frozen-duration-model.py; only trials=100000 changed to trials=257.
        # These are smoke counts, not the retained 100,000-draw result.
        result=reference.report(trials=257)
        self.assertEqual([p['misses'] for p in result['points']],[24,0,0,157,0,1])
        self.assertEqual([p['trials'] for p in result['points']],[257]*6)

    def test_covariance_limits_and_surrogate_bound_scope(self):
        self.assertAlmostEqual(reference.effective_modes(1e8,0,0,2),2)
        self.assertAlmostEqual(reference.effective_modes(.001,.5,0),1,places=5)
        for energy in (20.,100.,300.):
            for modes in (1.,2.5,40.):
                p=reference.gaussian_prediction(energy,modes,20.)
                self.assertLessEqual(p['modeled_detection_lower_bound'],
                                     p['modeled_detection_probability']+1e-12)
                self.assertIn('surrogate',p['tail_scope'])


if __name__=='__main__':unittest.main()

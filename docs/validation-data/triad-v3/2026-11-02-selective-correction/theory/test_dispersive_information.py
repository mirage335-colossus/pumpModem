#!/usr/bin/env python3
"""Cheap checks for corrected-operator information inequalities."""
import importlib.util
import json
import math
from pathlib import Path
import unittest
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('corrected_information',HERE/'dispersive_information.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)


class CorrectedInformation(unittest.TestCase):
    def test_cubic_gain_contraction_identity(self):
        omega=np.linspace(-math.pi,math.pi,513)
        for fraction in np.linspace(0,1,21):
            actual=sum(math.prod((fraction-j)/(k-j) for j in (-1,0,1,2) if j!=k)*
                np.exp(-1j*omega*k) for k in (-1,0,1,2))
            v=np.sin(omega/2)**2
            formula=1-4/9*fraction*(1-fraction)*(2-fraction)*(1+fraction)*v*v*(3+4*fraction*(1-fraction)*v)
            self.assertLess(float(np.max(np.abs(np.abs(actual)**2-formula))),3e-15)
            self.assertLessEqual(float(np.max(np.abs(actual))),1+2e-15)

    def test_corrected_helper_and_complete_tail(self):
        result=s.actual_filter_check(s.compile_probe())
        self.assertTrue(result['passed'])
        self.assertGreater(result['output_samples'],result['input_samples'])

    def test_uniform_phase_moments(self):
        result=s.phase_moment_check(.65,.65)
        self.assertEqual(result['phase_nodes'],5**6)
        self.assertLessEqual(result['eu_u_star_max_eigenvalue'],result['q']+2e-12)

    def test_named_factors_and_exclusions(self):
        self.assertAlmostEqual(s.depth_parameters(.25,.25)['quadratic_KL_multiplier_upper'],1.421875)
        self.assertAlmostEqual(s.depth_parameters(.65,.65)['quadratic_KL_multiplier_upper'],5.677075)
        for depth in (0,.01,.05,.1,.25,.65):
            p=s.depth_parameters(depth,depth)
            self.assertLessEqual(p['quadratic_KL_multiplier_upper'],p['legacy_quadratic_multiplier_upper'])
        result=json.loads((HERE/'dispersive_information.json').read_text())
        for filename,digest in result['source_sha256'].items():
            self.assertEqual(s.sha(s.ROOT/filename),digest)
        self.assertEqual(len(result['minus60_bounds']),36)
        self.assertTrue(all(all(t['excluded_at_minus60'] for t in row['targets'].values()) for row in result['minus60_bounds']))
        selected=[row for row in result['minus60_bounds'] if row['control']=='each-65' and
            row['B_2sigma_hz']==.1 and row['excess_delay_seconds']==.007 and row['full_preselective_span_seconds']==10*365.25*86400]
        self.assertEqual(len(selected),1)
        self.assertAlmostEqual(selected[0]['oracle_mixture_KL_upper_nats'],1.3403526993953994)
        self.assertTrue(any('B=0' in limit for limit in result['limits']))


if __name__=='__main__':unittest.main()

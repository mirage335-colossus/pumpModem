import itertools
from pathlib import Path
import sys
import unittest
import numpy as np
from scipy.special import logsumexp
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_operational_admission import PublicGrid,operational_decision


class AdmissionTests(unittest.TestCase):
    def grid(self):return PublicGrid((0,1),-1,1,1,1,1,2,2)

    def test_adaptive_subset_retains_original_mass(self):
        grid=self.grid()
        atoms=list(itertools.product((0,1),(-1.,0.,1.),(-1.,0.,1.),(-2.,0.,2.)))
        rng=np.random.default_rng(53)
        for _ in range(20):
            scores=rng.normal(size=(len(atoms),2))*3
            ids=np.argsort(scores.max(axis=1))[-4:]
            mean=logsumexp(scores[ids],axis=0)-np.log(len(ids))
            r=operational_decision(mean,[atoms[i]for i in ids],grid,zero_h0_verified=True)
            expected=logsumexp(scores[ids])-np.log(2*len(atoms))
            self.assertAlmostEqual(r['log_e_lower_bound'],expected,places=12)
            self.assertLessEqual(expected,logsumexp(scores)-np.log(2*len(atoms)))

    def test_repetition_and_durations_are_charged(self):
        grid=self.grid();c=[grid.snap((0,.3,.4,1))]
        a=operational_decision([100,0],c,grid,zero_h0_verified=True)
        b=operational_decision([100,0],c,grid,zero_h0_verified=True,duration_alternatives=4)
        self.assertAlmostEqual(b['log_e_threshold']-a['log_e_threshold'],np.log(4))
        self.assertEqual(a['attempts_per_24h_bound'],216001)
        self.assertFalse(a['physical_null_qualified'])

    def test_selected_atoms_must_be_finite_unique_and_fixed(self):
        grid=self.grid()
        for c in [[(0,.25,0,0)],[(0,0,0,0)]*2,[]]:
            with self.assertRaises(ValueError):operational_decision([0,0],c,grid,zero_h0_verified=True)
        with self.assertRaises(ValueError):operational_decision([0,0],[(0,0,0,0)],grid)

    def test_original_uncertainty_not_observed_subset_defines_grid(self):
        grid=PublicGrid(tuple(range(-6,7)),-7.035,7.035,.001,90.522026,.01,300.015)
        self.assertEqual(grid.counts,(13,14071,18107,9))
        for a in (-7.035,0,7.035):
            row=grid.snap((0,a,90.522026,300.015));grid.validate_selected([row])

    def test_residual_allowance_reduces_evidence(self):
        grid=self.grid();c=[(0,0,0,0)]
        a=operational_decision([30,10],c,grid,zero_h0_verified=True)
        b=operational_decision([30,10],c,grid,zero_h0_verified=True,log_null_expectation_penalty=7)
        self.assertAlmostEqual(a['log_e_lower_bound']-b['log_e_lower_bound'],7)

    def test_scalar_gaussian_expectation_and_mean_penalty(self):
        # Exact complex-Gaussian MGF, no rare-tail Monte Carlo required.
        for lam in (0.,.01,1.,100.):
            b=lam/(1+lam)
            for variance in (0.,.25,1.):
                expectation=1/((1+lam)*(1-variance*b))
                self.assertLessEqual(expectation,1+1e-13)
                mean_cost=.3*b/(1-variance*b)
                self.assertLessEqual(mean_cost,.3*lam+1e-12)
            self.assertAlmostEqual(1/((1+lam)*(1-b)),1.,places=12)
        # Underestimating noise is outside the theorem, and can raise E[e]>1.
        self.assertGreater(1/(2*(1-1.5*.5)),1.)


if __name__=='__main__':unittest.main()

"""Independent numerical controls for the ideal acoustic BICM screen."""
import importlib.util
from pathlib import Path
import unittest

import numpy as np


SPEC = importlib.util.spec_from_file_location("acoustic_bicm_analysis", Path(__file__).resolve().parents[1] / "tools/acoustic_bicm_analysis.py")
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


class BicmAnalysisTests(unittest.TestCase):
    def test_quadrature_convergence_and_gaussian_upper_bound(self):
        for order in ANALYSIS.ORDERS:
            for db in (0.,10.,20.,30.):
                first=ANALYSIS.qam_bit_information(order,db,64).sum()
                refined=ANALYSIS.qam_bit_information(order,db,160).sum()
                self.assertAlmostEqual(first,refined,delta=.004)
                self.assertLessEqual(refined,np.log2(1+10**(db/10))+1e-5)
                self.assertGreaterEqual(refined,0)
                self.assertLessEqual(refined,np.log2(order))

    def test_independent_monte_carlo_observations(self):
        rng=np.random.default_rng(47192)
        for order,db in ((4,0.),(64,18.)):
            side=int(np.sqrt(order));axis_bits=int(np.log2(side));snr=10**(db/10)
            levels=(2*np.arange(side)+1-side)/np.sqrt(2*(order-1)/3)
            labels=np.arange(side)^(np.arange(side)>>1)
            indices=rng.integers(side,size=200000)
            observations=levels[indices]+rng.normal(0,np.sqrt(.5/snr),size=indices.size)
            likelihood=np.exp(-(observations[:,None]-levels[None,:])**2*snr)
            result=0.
            for bit in range(axis_bits):
                one=(labels>>bit)&1
                p1=likelihood[:,one==1].sum(axis=1)
                p0=likelihood[:,one==0].sum(axis=1)
                chosen=np.where(one[indices],p1,p0)
                result+=1+np.mean(np.log2(chosen/(p0+p1)))
            self.assertAlmostEqual(2*result,ANALYSIS.qam_bit_information(order,db).sum(),delta=.012)

    def test_low_noise_and_low_signal_limits(self):
        for order in ANALYSIS.ORDERS:
            low=ANALYSIS.qam_bit_information(order,-20).sum()
            high=ANALYSIS.qam_bit_information(order,45).sum()
            self.assertLess(low,.015)
            self.assertAlmostEqual(high,np.log2(order),delta=.0001)
        with self.assertRaises(ValueError):ANALYSIS.qam_bit_information(8,10)
        with self.assertRaises(ValueError):ANALYSIS.qam_bit_information(16,float('nan'))

    def test_pooled_loading_shares_information_without_exceeding_bound(self):
        information={4:np.array([1.9,1.9]),16:np.array([3.95,2.8]),64:np.array([5.9,3.2]),
                     256:np.array([7.8,3.3]),1024:np.array([9.6,3.35])}
        chosen,margin=ANALYSIS.pooled_bit_loading(information,.75)
        self.assertGreaterEqual(margin,0)
        # Exhaustive two-carrier optimum for these deliberately different bins.
        maximum=0
        choices=[(0,np.zeros(2))]+[(int(np.log2(q)),information[q]) for q in ANALYSIS.ORDERS]
        for bits1,mi1 in choices:
            for bits2,mi2 in choices:
                if mi1[0]+mi2[1]>=.75*(bits1+bits2):maximum=max(maximum,bits1+bits2)
        self.assertEqual(chosen.sum(),maximum)


if __name__ == '__main__':
    unittest.main()

"""Small analytical checks for the source-only AGC diagnostic."""
import math
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_pattern_metrics import window_mean, envelope_metrics, coefficient_metrics, spectrum_metrics


class PatternMetricsTests(unittest.TestCase):
    def test_window_integral_and_insufficient_sampler(self):
        x=np.arange(100,dtype=float)
        np.testing.assert_allclose(window_mean(x,13),np.convolve(x,np.ones(13)/13,'valid'))
        self.assertFalse(envelope_metrics(np.ones(100),np.ones(100),1.,.1,2.)['measured'])
        with self.assertRaisesRegex(ValueError, 'no finite source energy'):
            spectrum_metrics(np.zeros(100),100.,10.)

    def test_balanced_carrier_powers_do_not_imply_balanced_composite(self):
        fs=2000.;t=np.arange(8000)/fs
        wave=(1+2*np.cos(2*np.pi*t))/math.sqrt(3)
        envelope=np.ones(len(wave))
        constant=envelope_metrics(np.ones(len(wave)),envelope,fs,.1,2.)
        actual=envelope_metrics(wave**2,envelope,fs,.1,2.)
        self.assertTrue(constant['strict_all_windows_within_quarter_step'])
        self.assertGreater(actual['maximum_absolute_deviation_db'],5.)
        self.assertFalse(actual['strict_all_windows_within_quarter_step'])

    def test_exact_legacy_permutation_is_detected(self):
        amplitude=np.tile(np.sqrt([.4,1,1.6]),(32,1))
        coefficients=np.array([amplitude,1j*np.roll(amplitude,-1,axis=1)])
        result=coefficient_metrics(coefficients)
        self.assertAlmostEqual(result['best_fixed_amplitude_permutation_mse'],0.)
        self.assertEqual(result['best_fixed_amplitude_permutation'],[1,2,0])


if __name__=='__main__':unittest.main()

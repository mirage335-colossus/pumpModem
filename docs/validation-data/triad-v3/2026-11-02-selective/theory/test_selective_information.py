import itertools
import json
import math
import unittest

import numpy as np
import selective_information as s


class SelectiveInformationTests(unittest.TestCase):
    def test_actual_shared_filter_and_fir(self):
        probe=s.compile_probe()
        self.assertTrue(s.actual_filter_check(probe)['passed'])
        for k in probe['kernels']:
            width=k['width_hz'];dt=1/k['sample_rate']
            half=math.ceil(5/(math.pi*width*dt))
            h=np.exp(-(math.pi*width*dt*np.arange(-half,half+1))**2)
            h/=np.linalg.norm(h)
            np.testing.assert_allclose(h,k['coefficients'],rtol=3e-15,atol=3e-16)

    def test_complementary_contractions_and_exact_bypasses(self):
        for fs,bw in ((32,6),(.25,.0225),(128,22.5)):
            b=s.band_matrices(48,fs,bw)
            np.testing.assert_allclose(b.sum(axis=0),np.eye(48),rtol=0,atol=2e-16)
            for branch in b:self.assertLessEqual(np.linalg.norm(branch,2),1+2e-14)
            for doppler,slow,fast in ((0,.65,.65),(1,0,0)):
                np.testing.assert_array_equal(s.modulation_operator(b,fs,doppler,slow,fast,np.zeros((3,2))),np.eye(48))

    def test_exact_phase_quadrature_checks_moments_and_bounds(self):
        # Five nodes integrate every trigonometric polynomial through degree4
        # in each of six independent phases. This is exact polynomial quadrature,
        # not a Monte Carlo probability claim or an exact quadrature of logdet.
        n=4;fs=16.;doppler=.7;b=s.band_matrices(n,fs,3.)
        rng=np.random.default_rng(4471);z=rng.normal(size=(n,n))+1j*rng.normal(size=(n,n))
        k=z@z.conj().T/100
        phi=np.array(list(itertools.product(2*math.pi*np.arange(5)/5,repeat=6))).reshape(-1,3,2)
        time=np.arange(n)/fs
        slow,fast=.25,.65;parameters=s.depth_parameters(slow,fast)
        gain=1+slow*np.sin(2*math.pi*.1*doppler*time[None,None,:]+phi[:,:,0,None])
        gain+=fast*np.sin(2*math.pi*doppler*time[None,None,:]+phi[:,:,1,None])
        a=np.einsum('pbn,bnk->pnk',gain,b)
        np.testing.assert_allclose(a.mean(axis=0),np.eye(n),atol=2e-14)
        gram=np.einsum('pji,pjk->pik',a.conj(),a).mean(axis=0)
        expected=np.eye(n)+parameters['q']*np.einsum('bji,bjk->ik',b.conj(),b)
        np.testing.assert_allclose(gram,expected,rtol=3e-14,atol=3e-14)
        kp=a@k@a.conj().transpose(0,2,1)
        trace_square=np.einsum('pij,pji->p',kp,kp).real.mean()
        upper=parameters['quadratic_KL_multiplier_upper']*np.trace(k@k).real
        self.assertLessEqual(trace_square,upper)
        self.assertLessEqual(np.linalg.eigvalsh(gram).max(),parameters['ensemble_energy_factor_upper']+1e-13)

    def test_covariance_mixture_direction_is_not_reversed(self):
        result=s.scalar_gaussian_mixture_kl([1.1,3.],[.5,.5])
        self.assertLess(result['same_covariance_Gaussian_KL_lower_nats'],result['mixture_KL_nats'])
        self.assertLess(result['mixture_KL_nats'],result['latent_oracle_KL_upper_nats'])
        exact=s.scalar_gaussian_mixture_kl([1.5,1.5],[.5,.5])
        self.assertAlmostEqual(exact['mixture_KL_nats'],.5-math.log(1.5),places=11)

    def test_binary_necessity_and_low_signal_scaling(self):
        strict=s.binary_kl(.99,1e-4)
        self.assertAlmostEqual(s.detection_upper(strict,1e-4),.99,places=11)
        self.assertEqual(s.detection_upper(0,1e-4),1e-4)
        k=np.array([[.001,.0001j],[-.0001j,.002]])
        self.assertAlmostEqual(s.gaussian_kl(k*.1)/s.gaussian_kl(k),.01,delta=2e-5)

    def test_component_depth_limits_and_nonzero_mean(self):
        self.assertEqual(s.depth_parameters(.5,.5)['gain_minimum'],0.)
        self.assertAlmostEqual(s.depth_parameters(.65,.65)['gain_minimum'],-.3)
        for x in (-.1,1.1,float('nan'),float('inf')):
            with self.assertRaises(ValueError):s.depth_parameters(x,0)
        self.assertEqual(s.depth_parameters(0,0)['quadratic_KL_multiplier_upper'],1)

    def test_full_report_is_strict_json_serializable(self):
        result=json.loads(json.dumps(s.report(),allow_nan=False))
        self.assertEqual(len(result['ten_year_minus60_bounds']),228)
        self.assertIsInstance(result['ten_year_minus60_bounds'][0]['targets']['strict']['excluded_at_minus60'],bool)


if __name__=='__main__':unittest.main()

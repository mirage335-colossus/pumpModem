#!/usr/bin/env python3
"""Focused numerical checks for optional complete-observation affine scanning."""
from pathlib import Path
import math
import sys
import unittest

import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from triad_dynamic_scan import PreparedGaussianScan
from triad_gaussian_prepared import PreparedGaussianDesign
from triad_gaussian_state import GaussianPrior


def fixture(blocks=17, batch=3, leading=(2,), taps=2, seed=810, static=False):
    generator=torch.Generator().manual_seed(seed)
    shape=(blocks,*leading,taps,taps)
    a=torch.complex(torch.randn(shape,generator=generator,dtype=torch.float64),
                    torch.randn(shape,generator=generator,dtype=torch.float64))*.4
    grams=a.mH@a
    if blocks>3:
        grams[3]=0
        grams[1]=a[1,...,:1,:].mH@a[1,...,:1,:]
    dts=[0.]+[.07+(index%5)*.13 for index in range(blocks-1)]
    prior=GaussianPrior(tuple([1/taps]*taps),math.inf if static else 2.1,.137)
    design=PreparedGaussianDesign(grams,dts,prior)
    rshape=(blocks,batch,*leading,taps)
    r=torch.complex(torch.randn(rshape,generator=generator,dtype=torch.float64),
                    torch.randn(rshape,generator=generator,dtype=torch.float64))*.3
    if blocks>3:r[3]=0
    return design,r


def serial(design,r,dtype):
    runtime=design.to("cpu",dtype,check_inputs=False)
    means,increments=[],[]
    previous=torch.zeros(r.shape[1:-1],dtype=torch.float64)
    for block in r.to(dtype):
        score=runtime.update(block)
        means.append(runtime.mean)
        increments.append(score-previous)
        previous=score
    return runtime.scores(),torch.stack(means),torch.stack(increments)


class AffineScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)
        torch.backends.cuda.matmul.allow_tf32=False
        torch.set_float32_matmul_precision("highest")

    def test_serial_all_means_increments_rotations_gaps_rank_static(self):
        for static in (False,True):
            for leading in ((),(2,),(3,2)):
                for dtype in (torch.complex128,torch.complex64):
                    with self.subTest(static=static,leading=leading,dtype=dtype):
                        design,r=fixture(leading=leading,static=static)
                        expected,means,increments=serial(design,r,dtype)
                        scan=PreparedGaussianScan(design,dtype=dtype)
                        actual=scan.score(r,return_means=True,return_increments=True)
                        self.assertEqual(actual["execution"],"affine-parallel-scan")
                        tol=2e-11 if dtype==torch.complex128 else 3e-6
                        torch.testing.assert_close(actual["scores"],expected,rtol=tol,atol=tol)
                        torch.testing.assert_close(actual["means"],means,rtol=tol,atol=tol)
                        torch.testing.assert_close(actual["increments"],increments,rtol=tol,atol=tol)
                        self.assertEqual(actual["scores"].dtype,torch.float64)

    def test_independent_dense_gaussian_observation_reference(self):
        generator=torch.Generator().manual_seed(991)
        n,k,rows=4,2,2
        a=torch.complex(torch.randn((n,rows,k),generator=generator,dtype=torch.float64),
                        torch.randn((n,rows,k),generator=generator,dtype=torch.float64))*.7
        y=torch.complex(torch.randn((n,rows),generator=generator,dtype=torch.float64),
                        torch.randn((n,rows),generator=generator,dtype=torch.float64))
        dts=[0.,.2,.6,.1]; times=torch.tensor(dts,dtype=torch.float64).cumsum(0)
        prior=GaussianPrior((.3,.7),1.2,.19)
        covariance=torch.zeros((n*k,n*k),dtype=torch.complex128)
        power=torch.diag(torch.tensor(prior.stationary_power,dtype=torch.float64)).to(torch.complex128)
        for i in range(n):
            for j in range(n):
                dt=float(times[i]-times[j])
                rotation=complex(math.cos(2*math.pi*prior.frequency_hz*dt),math.sin(2*math.pi*prior.frequency_hz*dt))
                covariance[i*k:(i+1)*k,j*k:(j+1)*k]=power*math.exp(-abs(dt)/prior.coherence_seconds)*rotation
        observation=torch.block_diag(*list(a))
        sigma=torch.eye(n*rows,dtype=torch.complex128)+observation@covariance@observation.mH
        flat=y.flatten(); expected=(flat.conj()@flat-flat.conj()@torch.linalg.solve(sigma,flat)).real-torch.linalg.slogdet(sigma).logabsdet
        design=PreparedGaussianDesign(a.mH@a,dts,prior)
        r=(a.mH@y[...,None]).squeeze(-1)[:,None,:]
        actual=PreparedGaussianScan(design,dtype=torch.complex128).score(r)["scores"][0]
        torch.testing.assert_close(actual,expected,rtol=3e-12,atol=3e-12)

    def test_long_ten_thousand_block_scan_and_ranking(self):
        design,r=fixture(blocks=10000,batch=4,leading=(3,),seed=3001)
        reference,_,_=serial(design,r,torch.complex128)
        expected,means,_=serial(design,r,torch.complex64)
        actual=PreparedGaussianScan(design,dtype=torch.complex64).score(r)
        torch.testing.assert_close(actual["scores"],expected,rtol=2e-6,atol=2e-3)
        torch.testing.assert_close(actual["final_mean"],means[-1],rtol=3e-6,atol=3e-6)
        self.assertTrue(torch.equal(actual["scores"].argmax(-1),reference.argmax(-1)))
        # Frozen thresholds just either side of the reference. This is an
        # arithmetic decision check, not a statistical admission guarantee.
        for gap in (-.02,.02):
            gate=reference+gap
            self.assertTrue(torch.equal(actual["scores"]>=gate,reference>=gate))

    def test_strong_observations_and_explicit_public_fallback(self):
        blocks=64
        grams=torch.eye(2,dtype=torch.complex128).expand(blocks,2,2).clone()*1e6
        grams[:,0,1]=3e5j; grams[:,1,0]=-3e5j
        design=PreparedGaussianDesign(grams,[0.]+[.05]*(blocks-1),GaussianPrior((.5,.5),2.,.1))
        r=torch.full((blocks,2,2),1e5+.5j,dtype=torch.complex128)
        expected,means,_=serial(design,r,torch.complex64)
        actual=PreparedGaussianScan(design,dtype=torch.complex64).score(r)
        torch.testing.assert_close(actual["scores"],expected,rtol=2e-6,atol=.2)
        torch.testing.assert_close(actual["final_mean"],means[-1],rtol=2e-6,atol=2e-6)
        fallback=PreparedGaussianScan(design,dtype=torch.complex64,residual_limit=1e-30)
        result=fallback.score(r,return_means=True,return_increments=True)
        self.assertEqual(result["execution"],"serial-fallback")
        self.assertIn("residual",result["fallback_reason"])
        torch.testing.assert_close(result["scores"],expected,rtol=0,atol=0)
        torch.testing.assert_close(result["means"],means,rtol=0,atol=0)

    def test_zero_power_single_block_and_fresh_repeat(self):
        design=PreparedGaussianDesign(torch.ones((1,2,2),dtype=torch.complex128),[0.],GaussianPrior((0.,0.)))
        scan=PreparedGaussianScan(design,dtype=torch.complex128)
        r=torch.ones((1,3,2),dtype=torch.complex128)
        a=scan.score(r);b=scan.score(r)
        self.assertTrue(torch.equal(a["scores"],torch.zeros(3,dtype=torch.float64)))
        self.assertTrue(torch.equal(a["scores"],b["scores"]))
        self.assertTrue(torch.equal(a["final_mean"],b["final_mean"]))

    def test_guards_precede_conversion_and_reject_invalid_inputs(self):
        design,r=fixture()
        with self.assertRaises(MemoryError):
            PreparedGaussianScan(design,memory_limit_bytes=1)
        scan=PreparedGaussianScan(design,memory_limit_bytes=50000)
        with self.assertRaises(MemoryError):scan.score(r[:,:1].expand(-1,300,-1,-1))
        with self.assertRaisesRegex(ValueError,"correlations need"):
            scan.score(r[:-1])
        with self.assertRaisesRegex(ValueError,"autograd"):
            scan.score(r.clone().requires_grad_())
        bad=r.clone();bad[0,0,0,0]=float("nan")
        with self.assertRaisesRegex(ValueError,"nonfinite"):
            scan.score(bad)
        with self.assertRaisesRegex(ValueError,"arithmetic"):
            PreparedGaussianScan(design,dtype=torch.float32)
        with self.assertRaisesRegex(ValueError,"guards"):
            PreparedGaussianScan(design,residual_limit=0)


if __name__=="__main__":
    unittest.main()

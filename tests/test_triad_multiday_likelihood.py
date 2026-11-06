#!/usr/bin/env python3
"""Independent dense/sample-noise checks of persistent long-bank statistics."""
import math
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.special import logsumexp
from scipy.stats import ks_2samp
import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_multiday_likelihood import (StreamingBankStatistics,mix_log_evidence,
                                      classify_scores,binomial_interval)
from triad_incremental_receiver import IncrementalReceiver


class LongBankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32=False
        torch.set_float32_matmul_precision('highest')

    def fixture(self,n=73,c=3):
        rng=np.random.default_rng(71303+n+c)
        basis=(rng.normal(size=(2,c,n))+1j*rng.normal(size=(2,c,n)))*.18
        signal=np.stack([basis[0,1]*np.exp(.17j),basis[1,0]*.92])
        noise=(rng.normal(size=(2,n))+1j*rng.normal(size=(2,n)))*math.sqrt(.7/2)
        return basis,signal,signal+noise

    def accumulate(self,basis,signals,observations=None,*,precision='fp64',chunk=17,valid=None):
        n=basis.shape[-1]
        bank=StreamingBankStatistics(sample_rate=8,samples=n,candidates=basis.shape[1],
            noise_variance=.7,observed_streams=0 if observations is None else len(observations),
            precision=precision,maximum_chunk_samples=max(chunk,16),reduction_samples=min(11,chunk),
            memory_limit_bytes=16*1024**2)
        for first in range(0,n,chunk):
            stop=min(n,first+chunk)
            bank.push(first,basis[...,first:stop],signals[:,first:stop],
                None if observations is None else observations[:,first:stop],
                None if valid is None else valid[first:stop])
        return bank.finish()

    def test_dense_gram_and_maintained_incremental_likelihood_match(self):
        basis,signals,ys=self.fixture();valid=np.ones(basis.shape[-1],np.uint8);valid[[4,18,72]]=0
        snapshot=self.accumulate(basis,signals,ys,valid=valid)
        rows=np.concatenate((basis.reshape(6,-1),signals))[:,valid.astype(bool)]
        np.testing.assert_allclose(snapshot.gram,rows.conj()@rows.T/.7,rtol=2e-14,atol=2e-14)
        np.testing.assert_allclose(snapshot.observed_correlations,ys[:,valid.astype(bool)]@rows.conj().T/.7,
                                   rtol=2e-14,atol=2e-14)
        shaped=np.concatenate((np.zeros_like(basis[:1]),basis))[:,:,None,:]
        expected=[]
        for y in ys:
            rx=IncrementalReceiver(sample_rate=8,samples=len(y),block_samples=13,
                basis=lambda lo,hi:shaped[...,lo:hi],required_bands=((-3.9,3.9),),
                noise_variance=.7,stationary_power=(1.,),coherence_seconds=(math.inf,),
                candidate_count=3,precision='fp64',projection_mode='valid-rows')
            for first in range(0,len(y),19):rx.push(first,y[first:first+19],valid[first:first+19])
            expected.append(rx.finish()['bit_scores'])
        np.testing.assert_allclose(snapshot.observed_scores(),expected,atol=2e-13,rtol=2e-13)
        r=snapshot.observed_correlations[:,:6].reshape(2,2,3)
        g=snapshot.gram.diagonal().real[:6].reshape(2,3)
        independent=logsumexp(abs(r)**2/(1+g)-np.log1p(g),axis=-1)-math.log(3)
        np.testing.assert_allclose(snapshot.observed_scores(),independent,atol=2e-13,rtol=2e-13)
        self.assertEqual(snapshot.metadata['erased_rows'],3)

    def test_joint_sufficient_statistics_match_actual_sampled_noise_distribution(self):
        basis,signals,_=self.fixture(n=53,c=2)
        snapshot=self.accumulate(basis,signals)
        n=4000;rng=np.random.default_rng(81030)
        noise=(rng.normal(size=(n,53))+1j*rng.normal(size=(n,53)))*math.sqrt(.7/2)
        rows=np.concatenate((basis.reshape(4,-1),signals))
        sampled=(noise+signals[0])@rows.conj().T/.7
        joint=snapshot.draw_correlations(trials=n,seed=81031,signal_index=0)
        statistic=np.concatenate((joint['bank_correlations'].reshape(n,4),joint['oracle_correlations']),axis=-1)
        mean=snapshot.gram[:,4]
        # These are independent finite MC populations; distribution tests have
        # fixed generous bounds, never ranking or repeated-look retry gates.
        for values in (sampled,statistic):
            empirical=(values-mean).T@(values-mean).conj()/n
            np.testing.assert_allclose(empirical,snapshot.gram,atol=.09*abs(snapshot.gram).max(),rtol=0)
        a=snapshot.static_scores(sampled[:,:4].reshape(n,2,2))
        b=snapshot.static_scores(joint['bank_correlations'])
        for bit in range(2):self.assertLess(ks_2samp(a[:,bit],b[:,bit]).statistic,.065)
        # Oracle is explicitly distinct from the practical bank likelihood.
        np.testing.assert_allclose(snapshot.oracle_scores(joint['oracle_correlations']),
            2*joint['oracle_correlations'].real-snapshot.reference_energies)
        before=id(snapshot._noise_factor)
        snapshot.draw_correlations(trials=2,seed=81032)
        self.assertEqual(id(snapshot._noise_factor),before)

    def test_chunking_fp32_floor_and_rank_deficient_candidates(self):
        basis,signals,_=self.fixture(n=101)
        basis[:,2]=basis[:,1]
        wide=self.accumulate(basis,signals,chunk=31)
        tiled=self.accumulate(basis,signals,chunk=7)
        narrow=self.accumulate(basis,signals,chunk=31,precision='fp32')
        np.testing.assert_allclose(wide.gram,tiled.gram,atol=3e-14,rtol=3e-14)
        np.testing.assert_allclose(wide.gram,narrow.gram,atol=2e-6,rtol=2e-6)
        d=narrow.draw_correlations(trials=20,seed=1,signal_index=1)
        np.testing.assert_allclose(narrow._noise_factor@narrow._noise_factor.conj().T,
            narrow.gram,atol=2e-12,rtol=2e-12)
        np.testing.assert_allclose(d['bank_correlations'][:,:,1],d['bank_correlations'][:,:,2],atol=2e-6,rtol=0)
        self.assertEqual(narrow.metadata['outer_sum_dtype'],'torch.complex128')
        self.assertFalse(narrow.metadata['tf32_allowed'])
        self.assertGreaterEqual(d['diagnostics']['relative_negative_eigenvalue_repair'],0)

    def test_zero_energy_and_erasures_are_unit_evidence(self):
        basis,signals,_=self.fixture()
        result=self.accumulate(basis,signals,valid=np.zeros(73,np.uint8))
        self.assertEqual(result.metadata['observed_rows'],0)
        draw=result.draw_correlations(trials=17,seed=2,signal_index=1)
        self.assertTrue(np.all(result.static_scores(draw['bank_correlations'])==0))
        self.assertTrue(np.all(result.oracle_scores(draw['oracle_correlations'])==0))

    def test_fixed_model_mixture_bound_and_endpoint_counts(self):
        rng=np.random.default_rng(1);a=rng.normal(size=(1000,2));b=rng.normal(size=(1000,2))
        mixture=mix_log_evidence([a,b])
        self.assertTrue(np.all(mixture>=np.maximum(a,b)-math.log(2)-1e-14))
        np.testing.assert_allclose(mix_log_evidence([a,b],[1,0]),a)
        scores=np.array([[30.,0.],[0.,30.],[-20.,-20.]])
        report=classify_scores(scores,5.,true_bit=0)
        self.assertEqual(report['counts'],dict(correct_admissions=1,wrong_admissions=1,misses=1,raw_bit_errors=1))
        self.assertEqual(classify_scores(scores,5.)['counts']['false_admissions'],2)
        self.assertGreater(binomial_interval(1000,1000)[0],.99)
        self.assertGreater(binomial_interval(0,1000)[1],1e-4)
        with self.assertRaises(ValueError):mix_log_evidence([a,b],[1,-1])

    def test_bounds_fail_closed_and_duration_does_not_allocate(self):
        huge=StreamingBankStatistics(sample_rate=1/16,samples=1<<54,candidates=1,
            noise_variance=1.,maximum_chunk_samples=16,reduction_samples=8,memory_limit_bytes=1<<20)
        self.assertLess(huge.owned_bytes_bound,1<<20)
        with self.assertRaisesRegex(RuntimeError,'incomplete'):huge.finish()
        with self.assertRaises(ValueError):huge.push(1,np.zeros((2,1,2)),np.zeros((2,2)))
        with self.assertRaises(MemoryError):StreamingBankStatistics(sample_rate=1,samples=100,candidates=4096,
            noise_variance=1.,memory_limit_bytes=1<<20)
        z=np.zeros((2,1,16));s=np.zeros((2,16))
        huge.push(0,z,s)
        with self.assertRaisesRegex(ValueError,'nonfinite'):huge.push(16,z+math.inf,s)
        with self.assertRaisesRegex(RuntimeError,'failure'):huge.push(16,z,s)


if __name__=='__main__':unittest.main()

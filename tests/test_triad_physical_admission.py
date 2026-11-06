"""Small dense references for native-real conditional physical admission."""
from __future__ import annotations
import copy
import math
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.linalg import block_diag
from scipy.special import logsumexp
import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_physical_admission import (PhysicalReceiver, physical_decision,
    validate_profile, source_sha256, PROFILE_CHECKS)


def binding():
    return dict(backend='alsa-raw',device='hw:test,0',hardware_rate_hz=64,
        hardware_channels=2,selected_channel=0,mixer_sha256='a'*64,
        audio_binary_sha256='b'*64,receiver_sha256=source_sha256(),
        receiver_configuration=dict(coherences=['0.25','inf'],prior='fixed'))


def profile():
    return dict(schema='triad-physical-noise-profile-v1',binding=binding(),
        geometry=dict(sample_rate_hz=64,block_samples=8,data_if_hz=12.,
            required_bands_hz=[[8.,20.]],fixed_gain=.7,maximum_capture_seconds=10.),
        null_model=dict(kind='global-real-subgaussian-proxy-v1',variance_proxy=1.3,
            assumptions=['Zero-mean joint sub-Gaussian real ADC noise with the stated global proxy.'],
            dc_component='constant-only',bound_scope='conditional-on-profile'),
        qualification=dict(passed=True,calibration_sha256='c'*64,validation_sha256='d'*64,
            calibration_samples=1000,validation_samples=1000,
            checks=[dict(name=stage+':'+name,passed=True)
                    for stage in ('calibration','validation') for name in PROFILE_CHECKS]))


def validate(p=None,**overrides):
    arguments=dict(sample_rate=64,block_samples=8,data_if_hz=12.,
        required_bands_hz=[[8.,20.]],fixed_gain=.7,capture_seconds=1.)
    arguments.update(overrides)
    return validate_profile(profile() if p is None else p,binding(),**arguments)


def setup(n=37,precision='fp64',**extra):
    rng=np.random.default_rng(1238)
    source=(rng.normal(size=(2,2,1,n))+1j*rng.normal(size=(2,2,1,n)))*.5
    observation=rng.normal(size=n)*.7
    options=dict(sample_rate=64,samples=n,block_samples=8,basis=lambda lo,hi:source[...,lo:hi],
        candidate_count=2,required_bands_hz=[[8.,20.]],variance_proxy=1.3,
        stationary_power=(1.,),coherence_seconds=(.25,math.inf),
        candidate_probabilities=[.3,.7],model_probabilities=[.2,.8],fixed_gain=.7,
        precision=precision,maximum_input_chunk_samples=64)
    options.update(extra)
    return PhysicalReceiver(**options),source,observation


def dense_reference(rx,source,observation):
    blocks=[rx._block_bounds(i) for i in range(rx.block_count)]
    projectors=[]
    for lo,hi in blocks:
        # Construct the actual public real projection independently as cos/sin
        # rows, rather than use its FFT implementation for both sides.
        n=hi-lo; frequencies=np.fft.rfftfreq(n,1/rx.sample_rate)
        selected=(frequencies>=8.-rx.sample_rate/(2*n)) & (frequencies<=20.+rx.sample_rate/(2*n))
        selected[0]=False
        if n%2==0:selected[-1]=False
        indices=np.flatnonzero(selected)
        angle=2*np.pi*indices[:,None]*np.arange(n)[None,:]/n
        projectors.append(np.concatenate((np.cos(angle),-np.sin(angle)))*math.sqrt(2/n))
    P=block_diag(*projectors)
    y=P@observation
    centers=np.array([(lo+hi-1)/2/rx.sample_rate for lo,hi in blocks])
    scores=np.empty((len(rx.coherences),2,rx.candidates))
    operators=[]
    for b in (0,1):
        for c in range(rx.candidates):
            columns=[]
            for (lo,hi),p in zip(blocks,projectors):
                z=source[b,c,0,lo:hi]
                a=math.sqrt(2)*rx.fixed_gain*np.stack((z.real,-z.imag),axis=1)
                columns.append(p@a)
            A=block_diag(*columns)
            for m,tau in enumerate(rx.coherences):
                correlation=np.ones((len(blocks),len(blocks))) if math.isinf(tau) else np.exp(-abs(centers[:,None]-centers[None,:])/tau)
                K=np.kron(correlation,np.eye(2)*.5)
                S=A@K@A.T
                Q=np.eye(len(y))*rx.variance
                B=np.eye(len(y))/rx.variance-np.linalg.inv(Q+S)
                scores[m,b,c]=-.5*np.linalg.slogdet(np.eye(len(y))+S/rx.variance)[1]+.5*y@B@y
                operators.append((S,B,P))
    mixed_candidate=logsumexp(scores+np.log(rx.candidate_probabilities)[None,None,:],axis=-1)
    bits=logsumexp(mixed_candidate+np.log(rx.model_probabilities)[:,None],axis=0)
    return bits,mixed_candidate,operators


class PhysicalAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_projection_real_orthonormal_and_dc_rejection_odd_even(self):
        for n in (16,17):
            indices=np.arange(1,(n+1)//2)
            P=PhysicalReceiver.project(np.eye(n),indices).T
            np.testing.assert_allclose(P@P.T,np.eye(2*len(indices)),atol=5e-15)
            np.testing.assert_allclose(P@np.ones(n),0,atol=5e-15)

    def test_streamed_real_gaussian_likelihood_matches_dense_all_priors(self):
        rx,z,y=setup()
        expected,models,_=dense_reference(rx,z,y)
        for first in range(0,len(y),3):rx.push(first,y[first:first+3])
        result=rx.finish()
        np.testing.assert_allclose(result['model_bit_scores'],models,rtol=2e-12,atol=2e-12)
        np.testing.assert_allclose(result['bit_scores'],expected,rtol=2e-12,atol=2e-12)
        self.assertTrue(result['complete_observation'])
        self.assertEqual(result['processed_rows'],len(y))
        self.assertEqual(result['block_updates'],4)

    def test_colored_improper_joint_gaussian_null_bound_is_global(self):
        rx,z,y=setup()
        _,_,operators=dense_reference(rx,z,y)
        n=len(y)
        # Correlations cross every processing boundary and make complex
        # positive-frequency marginals improper. Only global domination is used.
        C=.8**abs(np.arange(n)[:,None]-np.arange(n)[None,:])
        C*=rx.variance*.9/np.linalg.eigvalsh(C)[-1]
        expectations=[]
        for S,B,P in operators:
            Cprojected=P@C@P.T
            values,vectors=np.linalg.eigh(Cprojected)
            root=(vectors*np.sqrt(values.clip(0)))@vectors.T
            moment=np.eye(len(S))-root@B@root
            self.assertGreater(np.linalg.eigvalsh(moment)[0],0)
            log_expectation=-.5*np.linalg.slogdet(np.eye(len(S))+S/rx.variance)[1]-.5*np.linalg.slogdet(moment)[1]
            expectations.append(math.exp(log_expectation))
        self.assertLessEqual(max(expectations),1.+1e-12)
        self.assertLess(min(expectations),.95)

    def test_chunks_dc_and_precision_do_not_change_admission_statistic(self):
        a,z,y=setup();b,_,_=setup();c,_,_=setup(precision='fp32')
        a.push(0,y);b.push(0,y+100.)
        for i in range(len(y)):c.push(i,y[i:i+1])
        np.testing.assert_allclose(a.finish()['bit_scores'],b.finish()['bit_scores'],atol=2e-12)
        np.testing.assert_allclose(a.finish()['bit_scores'],c.finish()['bit_scores'],atol=2e-5)

    def test_incomplete_bad_basis_and_memory_bound(self):
        rx,z,y=setup();rx.push(0,y[:7])
        self.assertFalse(rx.finish()['complete_observation'])
        with self.assertRaisesRegex(ValueError,'contiguous'):rx.push(8,y[7:8])
        with self.assertRaises(MemoryError):setup(memory_limit_bytes=100)
        small,_,_=setup(n=64)
        enormous,_,_=setup(n=64,samples=1<<50)
        self.assertEqual(small.owned_bytes_estimate,enormous.owned_bytes_estimate)
        bad,_,_=setup(basis=lambda lo,hi:np.full((2,2,1,hi-lo),np.nan+1j))
        with self.assertRaisesRegex(ValueError,'physical basis'):bad.push(0,y[:8])
        self.assertEqual(bad.finish()['status'],'failed')

    def test_profile_and_decision_are_matched_and_explicitly_conditional(self):
        p=validate()
        result=physical_decision([100.,0.],complete=True,profile=p,log_threshold=21.5)
        self.assertTrue(result['admitted'])
        self.assertFalse(result['unconditional_physical_rate_certified'])
        self.assertFalse(physical_decision([100,0],complete=False,profile=p,log_threshold=21.5)['admitted'])
        self.assertFalse(physical_decision([100,0],complete=True,profile=p,log_threshold=21.5,clean_capture=False)['admitted'])
        with self.assertRaises(ValueError):physical_decision([100,0],complete=True,profile=None,log_threshold=21.5)
        for key,value in (('sample_rate',65),('block_samples',9),('data_if_hz',13.),
                ('fixed_gain',.8),('capture_seconds',11.)):
            with self.subTest(key=key),self.assertRaises(ValueError):validate(**{key:value})
        for change in ('failed','same-capture','missing-assumption','stale-source','missing-check'):
            q=copy.deepcopy(profile())
            if change=='failed':q['qualification']['checks'][0]['passed']=False
            if change=='same-capture':q['qualification']['validation_sha256']=q['qualification']['calibration_sha256']
            if change=='missing-assumption':q['null_model']['assumptions']=[]
            if change=='stale-source':q['binding']['receiver_sha256']='0'*64
            if change=='missing-check':q['qualification']['checks'].pop()
            with self.subTest(change=change),self.assertRaises(ValueError):validate(q)


if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Independent raw-design oracles for common/relative two-ray inference."""
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from scipy.special import logsumexp
import torch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('triad_ray_frequency_study',ROOT/'tools/triad-ray-frequency-study.py')
study=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(study)
FIXTURE=importlib.util.spec_from_file_location('triad_ray_fixture',ROOT/'tests/test_triad_noise_study.py')
fixture=importlib.util.module_from_spec(FIXTURE);FIXTURE.loader.exec_module(fixture)


def cn(rng,shape):
    return (rng.normal(size=shape)+1j*rng.normal(size=shape))/math.sqrt(2)


def design_case(seed=713,blocks=5,candidates=3,partial=True):
    rng=np.random.default_rng(seed);rows=7;dt=.4
    centers=(np.arange(blocks)+.5)*dt
    if partial:centers[-1]-=.11
    A=cn(rng,(blocks,candidates,rows,2))*.4
    # Strong complex nonorthogonality exposes the cross-Gram sign and forbids
    # treating the two correlations as independent post-whitening channels.
    A[...,1]=(.8+.45j)*A[...,0]+.18*A[...,1]
    y=cn(rng,(blocks,rows))
    r=np.einsum('bcnk,bn->bck',A.conj(),y)
    gram=np.einsum('bcni,bcnj->bcij',A.conj(),A)
    return A,y,r,gram,centers,dt


def raw_oracle(A,y,centers,dt,nfft,fbins,dbins,zero_mass):
    c=A.shape[1];weights=study.ray_weights(dbins,zero_mass);values=[]
    for f in fbins:
        for d,w in zip(dbins,weights):
            if w==0:continue
            phase=np.exp(2j*np.pi*centers[:,None]*np.asarray([f,f+d])[None,:]/(nfft*dt))
            design=(A*phase[:,None,None,:]).transpose(1,0,2,3).reshape(c,-1,2)
            observation=y.reshape(-1);row=[]
            for matrix in design:
                # Dense raw-observation covariance oracle; independently
                # integrates the Gaussian coefficients without the 2x2 path.
                covariance=np.eye(len(observation))+.5*matrix@matrix.conj().T
                L=np.linalg.cholesky(covariance)
                z=np.linalg.solve(L,observation)
                row.append(np.vdot(observation,observation).real-np.vdot(z,z).real
                           -2*np.log(L.diagonal().real).sum())
            values.append(np.asarray(row)+math.log(w)-math.log(len(fbins)))
    return logsumexp(values,axis=0)


class RayFrequencyTests(unittest.TestCase):
    def test_two_dimensional_fft_matches_raw_design_with_partial_center_and_nonorthogonal_rays(self):
        A,y,r,g,t,dt=design_case()
        nfft=64;fbins=np.arange(-3,4);dbins=np.arange(-2,3)
        for mass in (0.,.5,1.):
            expected=raw_oracle(A,y,t,dt,nfft,fbins,dbins,mass)
            for ft,rt in ((2,2),(64,32)):
                actual,info=study.joint_evidence(r,g,t,dt,nfft,fbins,dbins,
                    zero_mass=mass,frequency_tile=ft,ray_tile=rt)
                np.testing.assert_allclose(actual.numpy(),expected,rtol=2e-12,atol=2e-12)
                self.assertEqual(info['active_relative_bins'],1 if mass==1 else len(dbins))

    def test_single_partial_block_and_zero_bounds(self):
        A,y,r,g,t,dt=design_case(blocks=1,candidates=2)
        nfft,fh,dh=study.grid_shape(1,dt,2*t[-1],0,0)
        self.assertEqual((fh,dh),(0,0))
        expected=raw_oracle(A,y,t,dt,nfft,np.asarray([0]),np.asarray([0]),.5)
        actual,_=study.joint_evidence(r,g,t,dt,nfft,np.asarray([0]),np.asarray([0]))
        np.testing.assert_allclose(actual.numpy(),expected,atol=2e-12)

    def test_zero_relative_matches_same_grid_static_and_atom_lower_bound(self):
        A,y,r,g,t,dt=design_case(candidates=2)
        nfft=64;f=np.arange(-4,5);zero=np.asarray([0]);labels=np.asarray([0,1])
        diagnostic={}
        w,det=study.adapter.whiten_channel(r,g.sum(0),diagnostics=diagnostic)
        self.assertEqual(diagnostic['retained_ranks'],[2,2])
        R=study.adapter.frequency_correlations(w,t,dt,nfft,f)
        static=study.adapter.scores_from_correlations(R,det,labels)[0]
        matched,_=study.joint_evidence(r,g,t,dt,nfft,f,zero)
        expected=raw_oracle(A,y,t,dt,nfft,f,zero,.5)
        np.testing.assert_allclose(matched.numpy(),expected,atol=3e-12,rtol=3e-12)
        # The static adapter inflates numerical noise by scaling both A and y
        # by 1/sqrt(inflation), so both sufficient statistics scale by 1/inflation.
        # Compare the same model without changing the exact ray-model oracle.
        inflation=diagnostic['numerical_noise_inflation']
        aligned,_=study.joint_evidence(r/inflation,g/inflation,t,dt,nfft,f,zero)
        np.testing.assert_allclose(aligned.numpy(),static,atol=3e-12,rtol=3e-12)
        mixture,_=study.joint_evidence(r,g,t,dt,nfft,f,np.arange(-3,4),zero_mass=.5)
        self.assertTrue(bool((mixture>=matched-math.log(2)-1e-12).all()))
        atom,_=study.joint_evidence(r,g,t,dt,nfft,f,np.arange(-3,4),zero_mass=1.)
        np.testing.assert_allclose(atom.numpy(),matched.numpy(),atol=2e-12)

    def test_cross_gram_uses_positive_relative_phase_including_last_partial_center(self):
        A,_,_,g,t,dt=design_case(candidates=1)
        nfft=64;d=np.asarray([-2,-1,0,1,2])
        actual=study.adapter.frequency_correlations(g[...,0,1,None],t,dt,nfft,-d)[...,0].numpy()
        expected=np.einsum('db,bc->dc',np.exp(2j*np.pi*d[:,None]*t/(nfft*dt)),g[...,0,1])
        wrong=np.einsum('db,bc->dc',np.exp(-2j*np.pi*d[:,None]*t/(nfft*dt)),g[...,0,1])
        np.testing.assert_allclose(actual,expected,atol=2e-13)
        self.assertGreater(float(abs(actual-wrong).max()),.1)

    def test_fp32_correlation_fft_uses_fp64_solve_and_candidate_mixtures(self):
        _,_,r,g,t,dt=design_case(candidates=2)
        common=np.arange(-2,3);relative=np.arange(-1,2)
        precise,_=study.joint_evidence(r,g,t,dt,64,common,relative,precision='fp64')
        narrow,_=study.joint_evidence(r,g,t,dt,64,common,relative,precision='fp32')
        self.assertEqual(narrow.dtype,torch.float64)
        np.testing.assert_allclose(narrow.numpy(),precise.numpy(),rtol=2e-6,atol=2e-6)
        duplicated=study.mix_candidates(torch.cat((precise,precise)),np.asarray([0,1,0,1]))
        np.testing.assert_allclose(duplicated[0],precise.numpy(),atol=2e-14)

    def test_actual_ucb_reference_invariance_control_poison_and_family(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir()
            fixture.write_workspace(path,seed=700,observation=True,block_count=5,mixed_taps=True)
            manifest_path=path/'manifest.json';manifest=json.loads(manifest_path.read_text())
            def run(name,reference=1.):
                args=study.parse_args([str(path),'--output',str(Path(temp)/(name+'.json')),
                    '--cn0','0','--reference-variance',str(reference),'--frequency-bound','.2','--ray-frequency-bound','.1'])
                return study.run(args)
            original=run('original');self.assertEqual(original['failed_physical_capture_records'],0)
            scaled=run('scaled',3.)
            key='ray-frequency-ucb'
            np.testing.assert_allclose(original['captures'][0]['methods'][key]['bit_log_scores'],
                scaled['captures'][0]['methods'][key]['bit_log_scores'],atol=2e-12,rtol=2e-12)
            self.assertAlmostEqual(original['family']['fixed_window_log_threshold'],math.log(1440*8*16*2/(.5e-4)),places=12)
            for name,dtype in (('mean.c128','<c16'),('signal_energy.f64','<f8')):
                count=(path/name).stat().st_size//np.dtype(dtype).itemsize
                np.full(count,np.nan,dtype=dtype).tofile(path/name)
            fromfile=np.fromfile
            def guarded(file,*args,**kwargs):
                self.assertNotIn(Path(file.name).name,('mean.c128','signal_energy.f64'))
                return fromfile(file,*args,**kwargs)
            for control in ('null','wrong-key'):
                # The shared workspace schema requires positive finite noise
                # metadata; an unrelated valid value must not enter scoring.
                manifest.update(control=control,signal_bit='poison',signal_bit_simulation_truth=float('nan'),noise_variance=12345.)
                manifest_path.write_text(json.dumps(manifest))
                with patch.object(study.np,'fromfile',side_effect=guarded):result=run(control)
                self.assertEqual(result['failed_physical_capture_records'],0)
                self.assertIsNone(result['captures'][0]['truth_bit_simulation_only'])
                for method in study.METHODS:
                    np.testing.assert_array_equal(original['captures'][0]['methods'][method]['bit_log_scores'],
                        result['captures'][0]['methods'][method]['bit_log_scores'])
                if control=='wrong-key':self.assertIsNone(result['summary_by_control'][control][key]['null_qualification_complete'])

    def test_memory_preflight_rejects_before_grids_or_statistics_and_alias_limits(self):
        with self.assertRaisesRegex(ValueError,'Nyquist'):study.grid_shape(4,.25,1,1.9,.1)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=702,observation=True,mixed_taps=True)
            args=study.parse_args([str(path),'--output',str(Path(temp)/'small.json'),'--cn0','0','--memory-gib','.0001'])
            with (patch.object(study.np,'arange',side_effect=AssertionError('allocated grid before cap')),
                  patch.object(study.np,'fromfile',side_effect=AssertionError('read statistics before cap'))):
                result=study.run(args)
            self.assertEqual(result['captures'][0]['error_type'],'MemoryError')
            self.assertEqual(result['summary_by_control']['signal']['ray-frequency']['aggregate_counts']['missed'],1)
            manifest=json.loads((path/'manifest.json').read_text());manifest['control']='unsupported';(path/'manifest.json').write_text(json.dumps(manifest))
            args=study.parse_args([str(path),'--output',str(Path(temp)/'unknown.json'),'--cn0','0'])
            rejected=study.run(args)
            self.assertIn('unsupported actual control',rejected['captures'][0]['error'])


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()

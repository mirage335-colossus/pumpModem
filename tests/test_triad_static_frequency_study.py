#!/usr/bin/env python3
"""Independent static-channel and exact finite-grid frequency likelihood checks."""
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('triad_static_frequency_study',ROOT/'tools/triad-static-frequency-study.py')
study=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(study)
FIXTURE=importlib.util.spec_from_file_location('triad_static_frequency_fixture',ROOT/'tests/test_triad_noise_study.py')
fixture=importlib.util.module_from_spec(FIXTURE);FIXTURE.loader.exec_module(fixture)


def cn(rng,shape):return (rng.normal(size=shape)+1j*rng.normal(size=shape))/math.sqrt(2)


class StaticFrequencyTests(unittest.TestCase):
    def test_channel_marginal_matches_independent_dense_gaussian_density(self):
        rng=np.random.default_rng(71)
        for n,k in ((11,1),(19,2)):
            A=cn(rng,(n,k))*.17;y=cn(rng,(n,));powers=np.arange(1,k+1)*.3
            r=(A.conj().T@y)[None,None,:];G=(A.conj().T@A)[None,:,:]
            w,det=study.whiten_channel(r,G,powers)
            actual=np.sum(np.abs(w)**2)-det[0]
            covariance=np.eye(n)+A@np.diag(powers)@A.conj().T
            L=np.linalg.cholesky(covariance)
            z=np.linalg.solve(L,y)
            expected=np.vdot(y,y).real-np.vdot(z,z).real-2*np.log(np.diag(L).real).sum()
            self.assertAlmostEqual(actual,expected,places=12)

    def test_density_is_normalized_under_independent_raw_complex_noise(self):
        rng=np.random.default_rng(901);A=cn(rng,(5,2))*.06
        y=cn(rng,(25000,5));r=(y@A.conj())[:,None,:];G=(A.conj().T@A)[None,:,:]
        w,det=study.whiten_channel(r,G)
        values=np.exp(np.abs(w[:,0,:])**2@np.ones(2)-det[0])
        self.assertLess(abs(float(values.mean())-1),.002)

    def test_fft_matches_direct_actual_centers_including_partial_last_block(self):
        rng=np.random.default_rng(14)
        for blocks,last_duration in ((8,.19),(1,.08),(8,.25)):
            dt=.25;centers=(np.arange(blocks)+.5)*dt
            centers[-1]=(blocks-1)*dt+last_duration/2
            r=cn(rng,(blocks,3,2));nfft,bins,f=study.grid_for(blocks,dt,(blocks-1)*dt+last_duration,.65)
            actual=study.frequency_correlations(r,centers,dt,nfft,bins)
            direct=study.frequency_correlations(r,centers,dt,nfft,bins,implementation='direct')
            oracle=np.einsum('fb,bck->fck',np.exp(-2j*np.pi*f[:,None]*centers),r)
            np.testing.assert_allclose(actual.numpy(),oracle,atol=2e-13,rtol=2e-13)
            np.testing.assert_allclose(direct.numpy(),oracle,atol=2e-13,rtol=2e-13)
            zero=np.flatnonzero(bins==0)[0]
            np.testing.assert_allclose(actual[zero].numpy(),r.sum(0),atol=2e-13)

    def test_candidate_and_frequency_mixtures_are_normalized_not_maximized(self):
        correlations=torch.tensor([[[1+0j],[2+0j]],[[3+0j],[4+0j]]],dtype=torch.complex128)
        scores=study.scores_from_correlations(correlations,np.asarray([.3,.6]),np.asarray([0,1]))
        expected=np.logaddexp(np.asarray([1,4])-[.3,.6],np.asarray([9,16])-[.3,.6])-math.log(2)
        np.testing.assert_allclose(scores[0],expected,atol=2e-14)
        duplicated=torch.cat((correlations,correlations),dim=1)
        doubled=study.scores_from_correlations(duplicated,np.asarray([.3,.6,.3,.6]),np.asarray([0,1,0,1]))
        np.testing.assert_allclose(scores,doubled,atol=2e-14)

    def test_actual_fft_direct_and_ucb_reference_scale_invariance(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=510,observation=True,block_count=4,mixed_taps=True)
            results={}
            for impl,reference in [('fft',1.),('direct',1.),('fft-scaled',3.)]:
                output=Path(temp)/(impl+'.json')
                args=study.parse_args([str(path),'--output',str(output),'--cn0','0','--reference-variance',str(reference),'--frequency-bound','.65','--implementation','direct' if impl=='direct' else 'fft'])
                result=study.run(args);self.assertEqual(result['failed_physical_capture_records'],0)
                with np.load(output.with_suffix('.scores.npz')) as z:results[impl]={k:z[k].copy() for k in z.files}
            for key in results['fft']:
                np.testing.assert_allclose(results['fft'][key],results['direct'][key],atol=2e-12,rtol=2e-12)
            key='path00000_signal_static-frequency-ucb'
            np.testing.assert_allclose(results['fft'][key],results['fft-scaled'][key],atol=2e-12,rtol=2e-12)
            r=json.loads((Path(temp)/'fft.json').read_text())
            self.assertAlmostEqual(r['family']['fixed_window_log_threshold'],math.log(1440*3*9*2/(.5e-4)),places=12)
            self.assertEqual(r['configuration']['calibration_alternatives'],2)
            self.assertEqual(set(r['captures'][0]['input_hashes']),{'manifest.json','gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64'})

    def test_memory_failures_and_alias_limits_are_explicit(self):
        with self.assertRaisesRegex(ValueError,'Nyquist'):study.grid_for(4,.25,1,2)
        with self.assertRaisesRegex(ValueError,'regular block centers'):
            study.frequency_correlations(np.ones((3,1,1)),[.125,.42,.625],.25,16,np.asarray([0]))
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=512,observation=True,block_count=4,mixed_taps=True)
            args=study.parse_args([str(path),'--output',str(Path(temp)/'small.json'),'--cn0','0','--memory-gib','.0005'])
            with patch.object(study.np,'arange',side_effect=AssertionError('grid allocation before memory preflight')):
                result=study.run(args)
            self.assertEqual(result['failed_physical_capture_records'],1)
            self.assertEqual(result['captures'][0]['error_type'],'MemoryError')
            huge=study.parse_args([str(path),'--output',str(Path(temp)/'huge-grid.json'),'--cn0','0','--memory-gib','.001','--frequency-step','0.000001'])
            with patch.object(study.np,'arange',side_effect=AssertionError('large grid allocation before memory preflight')):
                rejected=study.run(huge)
            self.assertEqual(rejected['captures'][0]['error_type'],'MemoryError')
            s=result['summary_by_control']['signal']['static-frequency-ucb']
            self.assertEqual(s['aggregate_counts']['captures'],1);self.assertEqual(s['aggregate_counts']['missed'],1)
            self.assertEqual(s['completed_physical_paths'],0)

    def test_no_simulator_means_are_read_and_frontend_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=514,observation=True,block_count=3,mixed_taps=True)
            original=np.fromfile
            def guarded(file,*args,**kwargs):
                self.assertNotIn(Path(file.name).name,('mean.c128','signal_energy.f64'))
                return original(file,*args,**kwargs)
            args=study.parse_args([str(path),'--output',str(Path(temp)/'guarded.json'),'--cn0','0'])
            with patch.object(study.np,'fromfile',side_effect=guarded):r=study.run(args)
            self.assertEqual(r['failed_physical_capture_records'],0)
            manifest=json.loads((path/'manifest.json').read_text());manifest['gaussian_projection_supported']=False;(path/'manifest.json').write_text(json.dumps(manifest))
            args=study.parse_args([str(path),'--output',str(Path(temp)/'bad.json'),'--cn0','0'])
            r=study.run(args);self.assertEqual(r['failed_physical_capture_records'],1)
            self.assertIn('Gaussian null',r['captures'][0]['error'])

    def test_wrong_key_metadata_preserves_scores_and_is_not_noise_null_qualification(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=516,observation=True,block_count=4,mixed_taps=True)
            manifest_path=path/'manifest.json'
            manifest=json.loads(manifest_path.read_text())
            data_hashes={p.name:study.sha256(p) for p in path.iterdir() if p.name!='manifest.json'}
            original=np.fromfile
            def guarded(file,*args,**kwargs):
                self.assertNotIn(Path(file.name).name,('mean.c128','signal_energy.f64'))
                return original(file,*args,**kwargs)
            results={}
            for control in ('signal','null','wrong-key','unsupported-control'):
                manifest['control']=control;manifest_path.write_text(json.dumps(manifest))
                args=study.parse_args([str(path),'--output',str(Path(temp)/(control+'.json')),'--cn0','0'])
                with patch.object(study.np,'fromfile',side_effect=guarded):
                    results[control]=study.run(args)
            for control in ('signal','null','wrong-key'):
                self.assertEqual(results[control]['failed_physical_capture_records'],0)
            for method in study.METHODS:
                signal=results['signal']['captures'][0]['methods'][method]
                for control in ('null','wrong-key'):
                    actual=results[control]['captures'][0]['methods'][method]
                    np.testing.assert_array_equal(actual['bit_log_scores'],signal['bit_log_scores'])
                    self.assertEqual(actual['threshold'],signal['threshold'])
                wrong_key=results['wrong-key']['summary_by_control']['wrong-key'][method]
                null=results['null']['summary_by_control']['null'][method]
                self.assertEqual(wrong_key['aggregate_counts'],null['aggregate_counts'])
                self.assertIsNone(wrong_key['null_qualification_complete'])
                self.assertTrue(null['null_qualification_complete'])
                threshold=signal['threshold']
                admitted=study.classify(np.asarray([[threshold+1,threshold-1]]),0,threshold,'wrong-key')
                self.assertEqual(admitted,{'captures':1,'false_admitted':1,'not_admitted':0})
            self.assertTrue(any('does not cover wrong-key signals' in text for text in results['wrong-key']['limitations']))
            self.assertEqual(results['unsupported-control']['failed_physical_capture_records'],1)
            self.assertIn('only actual signal/null/wrong-key controls',results['unsupported-control']['captures'][0]['error'])
            self.assertEqual(data_hashes,{p.name:study.sha256(p) for p in path.iterdir() if p.name!='manifest.json'})

    def test_controls_ignore_unusable_truth_metadata_and_hidden_oracle_arrays(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'path';path.mkdir();fixture.write_workspace(path,seed=518,observation=True,block_count=4,mixed_taps=True)
            manifest_path=path/'manifest.json';manifest=json.loads(manifest_path.read_text())
            args=study.parse_args([str(path),'--output',str(Path(temp)/'signal.json'),'--cn0','0'])
            baseline=study.run(args)
            self.assertEqual(baseline['failed_physical_capture_records'],0)
            actual_names=('gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64')
            data_hashes={name:study.sha256(path/name) for name in actual_names}
            for name,dtype in (('mean.c128','<c16'),('signal_energy.f64','<f8')):
                cells=(path/name).stat().st_size//np.dtype(dtype).itemsize
                np.full(cells,np.nan,dtype=dtype).tofile(path/name)
            original=np.fromfile
            def guarded(file,*args,**kwargs):
                self.assertNotIn(Path(file.name).name,('mean.c128','signal_energy.f64'))
                return original(file,*args,**kwargs)
            for control in ('null','wrong-key'):
                for i,unusable in enumerate((float('nan'),'not-a-truth-bit')):
                    manifest.update(control=control,signal_bit=unusable,signal_bit_simulation_truth=unusable)
                    manifest_path.write_text(json.dumps(manifest))
                    args=study.parse_args([str(path),'--output',str(Path(temp)/(control+str(i)+'.json')),'--cn0','0'])
                    with patch.object(study.np,'fromfile',side_effect=guarded):
                        result=study.run(args)
                    self.assertEqual(result['failed_physical_capture_records'],0)
                    self.assertIsNone(result['captures'][0]['truth_bit_simulation_only'])
                    for method in study.METHODS:
                        actual=result['captures'][0]['methods'][method]
                        expected=baseline['captures'][0]['methods'][method]
                        np.testing.assert_array_equal(actual['bit_log_scores'],expected['bit_log_scores'])
                        self.assertEqual(actual['threshold'],expected['threshold'])
            self.assertEqual(data_hashes,{name:study.sha256(path/name) for name in actual_names})


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()

#!/usr/bin/env python3
"""Actual-path deterministic CFO batching contracts and independent parity."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('triad_cfo_batch',ROOT/'tools/triad-cfo-batch-study.py')
study=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(study)
FIX=importlib.util.spec_from_file_location('triad_cfo_fixture',ROOT/'tests/test_triad_noise_study.py')
fixture=importlib.util.module_from_spec(FIX);FIX.loader.exec_module(fixture)


class CFOBatchTests(unittest.TestCase):
    def workspaces(self,temp,count=3):
        paths=[]
        for i in range(count):
            path=Path(temp)/f'path{i}';path.mkdir()
            fixture.write_workspace(path,seed=611+i,observation=True,block_count=4,mixed_taps=True)
            manifest=json.loads((path/'manifest.json').read_text())
            manifest.update(epoch=1000+i,key_seed=2000+i,channel_seed=3000+i,control=('signal','null','wrong-key')[i%3])
            (path/'manifest.json').write_text(json.dumps(manifest))
            # Actual noise levels vary between paths, but each is stationary.
            factor=i+1
            r=np.fromfile(path/'observation.c128',dtype='<c16')*factor;r.tofile(path/'observation.c128')
            energy=np.fromfile(path/'received_energy.f64',dtype='<f8')*factor**2;energy.tofile(path/'received_energy.f64')
            paths.append(path)
        return paths

    def args(self,paths,output,*extra):
        return study.parse_args([*(str(p) for p in paths),'--output',str(output),'--reference-variance','1',
            '--particles','8','--frequency-bound','.3','--coherence','13',*extra])

    def test_batch_matches_individual_actual_same_grid_and_mixed_controls(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp)
            expected=[]
            for i,path in enumerate(paths):
                a=study.adapter.parse_args([str(path),'--output',str(Path(temp)/f'one{i}.json'),'--reference-variance','1',
                    '--methods','rbpf-ucb','--frequency-initialization','midpoint-grid','--frequency-bound','.3',
                    '--coherence','13','--particles','8','--selection-methods','16','--duration-alternatives','8'])
                result=study.adapter.run(a);self.assertEqual(result['failed_physical_capture_records'],0)
                expected.append(result['captures'][0]['methods']['rbpf-ucb']['bit_log_scores'])
            for path in paths[1:]:
                manifest=json.loads((path/'manifest.json').read_text());manifest['signal_bit_simulation_truth']='poisoned truth'
                (path/'manifest.json').write_text(json.dumps(manifest))
            actual=study.run(self.args(paths,Path(temp)/'batch.json'))
            self.assertEqual(actual['failed_physical_capture_records'],0)
            self.assertEqual(actual['batches'][0]['batch_count'],3)
            for i,row in enumerate(actual['captures']):
                np.testing.assert_allclose(row['methods'][study.METHOD]['bit_log_scores'],expected[i],atol=2e-12,rtol=2e-12)
                if i:self.assertIsNone(row['truth_bit_simulation_only'])
            upper=[r['calibration']['variance_upper_over_reference'] for r in actual['captures']]
            self.assertEqual(len(set(upper)),3)
            self.assertEqual(set(actual['summary_by_control']),{'signal','null','wrong-key'})
            self.assertIsNone(actual['summary_by_control']['wrong-key'][study.METHOD]['null_qualification_complete'])

    def test_batch_partition_invariance_and_no_hidden_mean_reads(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp)
            for path in paths:
                np.full((4,6),complex(np.nan,np.nan),dtype='<c16').tofile(path/'mean.c128')
                np.full(4,np.nan,dtype='<f8').tofile(path/'signal_energy.f64')
            original=np.fromfile
            def guarded(file,*args,**kwargs):
                self.assertNotIn(Path(file.name).name,('mean.c128','signal_energy.f64'))
                return original(file,*args,**kwargs)
            with patch.object(study.adapter.np,'fromfile',side_effect=guarded):
                one=study.run(self.args(paths,Path(temp)/'one.json','--batch-size','1'))
                many=study.run(self.args(paths,Path(temp)/'many.json','--batch-size','8'))
            self.assertEqual(one['failed_physical_capture_records'],0);self.assertEqual(many['failed_physical_capture_records'],0)
            for a,b in zip(one['captures'],many['captures']):
                np.testing.assert_allclose(a['methods'][study.METHOD]['bit_log_scores'],b['methods'][study.METHOD]['bit_log_scores'],atol=2e-12)
                self.assertEqual(a['methods'][study.METHOD]['counts'],b['methods'][study.METHOD]['counts'])

    def test_preflight_rejects_before_resident_preparation(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp)
            args=self.args(paths,Path(temp)/'small.json','--memory-gib','.0001','--particles','1024')
            with patch.object(study,'prepare_batch',side_effect=AssertionError('late preflight')):
                result=study.run(args)
            self.assertEqual(result['failed_physical_capture_records'],3)
            self.assertEqual(result['batches'][0]['error_type'],'MemoryError')
            self.assertEqual(result['summary_by_control']['signal'][study.METHOD]['aggregate_counts']['missed'],1)
            self.assertFalse(result['summary_by_control']['null'][study.METHOD]['null_qualification_complete'])

    def test_incompatible_geometry_and_actual_calendar_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp,2)
            manifest=json.loads((paths[1]/'manifest.json').read_text());manifest['sample_rate']=32
            (paths[1]/'manifest.json').write_text(json.dumps(manifest))
            result=study.run(self.args(paths,Path(temp)/'mismatch.json'))
            self.assertEqual(result['failed_physical_capture_records'],1)
            self.assertIn('signature mismatch',result['captures'][1]['error'])
            self.assertEqual(result['captures'][0]['status'],'completed')
            manifest['sample_rate']=64;(paths[1]/'manifest.json').write_text(json.dumps(manifest))
            interval=np.fromfile(paths[1]/'intervals.u64',dtype='<u8').reshape(4,2);interval[1,0]+=1;interval.tofile(paths[1]/'intervals.u64')
            invalid=study.run(self.args(paths,Path(temp)/'calendar.json'))
            self.assertEqual(invalid['failed_physical_capture_records'],2)
            self.assertIn('calendar',invalid['batches'][0]['error'])

    def test_tiled_single_pass_matches_original_per_block_projection_and_ucb(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp,2)
            # Exercise row-count grouping and erased physical blocks.
            for path in paths:
                np.asarray([8,0,8,7],dtype='<u8').tofile(path/'valid_rows.u64')
                for name,shape,dtype in [('gram.c128',(4,6,6),'<c16'),('observation.c128',(4,6),'<c16'),('received_energy.f64',(4,),'<f8')]:
                    values=np.fromfile(path/name,dtype=dtype).reshape(shape);values[1]=0;values.tofile(path/name)
            args=self.args(paths,Path(temp)/'unused.json','--projection-tile-blocks','3')
            workspaces=[study.BlockWorkspace(p) for p in paths]
            _,mapping,labels=study.adapter.candidate_mapping(workspaces[0],2)
            expected_r=np.empty((4,2,2,2),np.complex128)
            expected_G=np.empty((4,2,2,2,2),np.complex128)
            expected_upper=[]
            for index,ws in enumerate(workspaces):
                upper,_=study.adapter.calibrate(ws,1.,args.budget.delta_per_segment);expected_upper.append(upper)
                for j,(r,G,E,valid,dt) in enumerate(study.adapter.actual_blocks(ws,1.)):
                    projection=study.adapter.whole_bank_residual(torch.from_numpy(r[None]),torch.from_numpy(G),E,valid)
                    expected_r[j,index]=projection.projected_correlations.numpy()[0][mapping]/upper
                    expected_G[j,index]=projection.projected_gram.numpy()[mapping[:,:,None],mapping[:,None,:]]/upper
            with patch.object(study.adapter,'calibrate',side_effect=AssertionError('second calibration pass')):
                actual_r,actual_G,elapsed,metadata=study.prepare_batch(workspaces,mapping,args)
            np.testing.assert_allclose(actual_r,expected_r,atol=1e-12,rtol=1e-12)
            np.testing.assert_allclose(actual_G,expected_G,atol=1e-12,rtol=1e-12)
            np.testing.assert_allclose([m['calibration']['variance_upper_over_reference'] for m in metadata],expected_upper,atol=1e-12,rtol=1e-12)
            a,_,_=study.score_batch(actual_r,actual_G,elapsed,args,1024**3)
            b,_,_=study.score_batch(expected_r,expected_G,elapsed,args,1024**3)
            np.testing.assert_allclose(a,b,atol=1e-10,rtol=1e-10)
            for row in metadata:self.assertEqual(row['calibration']['pooled_complex_dof'],5)

    def test_projection_calls_are_tiled_and_memory_includes_full_bank_temporaries(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp,3)
            args=self.args(paths,Path(temp)/'tile.json')
            with patch.object(study.adapter,'whole_bank_residual',wraps=study.adapter.whole_bank_residual) as projection:
                result=study.run(args)
            self.assertEqual(result['failed_physical_capture_records'],0)
            self.assertEqual(projection.call_count,3)
            small=study.memory_preflight(256,8,2,2,64,256,'fp32',1)
            large=study.memory_preflight(256,8,2,2,64,256,'fp32',128)
            self.assertGreater(large['residual_preparation_bound_bytes'],small['residual_preparation_bound_bytes'])
            self.assertGreaterEqual(large['residual_preparation_bound_bytes'],24*128*64*64*16)
            self.assertEqual(large['projection_tile_blocks'],128)
            self.assertEqual(small['core_array_bound_bytes'],large['core_array_bound_bytes'])

    def test_predeclared_defaults_and_incompatible_model_options(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp,1)
            args=self.args(paths,Path(temp)/'valid.json')
            self.assertEqual((args.selection_methods,args.duration_alternatives,args.batch_size),(16,8,8))
            for extra in [('--phase-diffusion','.1'),('--resampling','systematic'),('--frequency-initialization','iid'),('--batch-size','0'),('--projection-tile-blocks','0')]:
                with self.subTest(extra=extra),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                    self.args(paths,Path(temp)/'bad.json',*extra)

    def test_fp32_and_fp64_near_agreement_and_core_bound(self):
        with tempfile.TemporaryDirectory() as temp:
            paths=self.workspaces(temp,2)
            results=[study.run(self.args(paths,Path(temp)/(precision+'.json'),'--precision',precision)) for precision in ('fp32','fp64')]
            for result in results:
                self.assertEqual(result['failed_physical_capture_records'],0)
                batch=result['batches'][0]
                self.assertGreaterEqual(batch['memory']['core_array_bound_bytes'],batch['diagnostics']['estimated_array_peak_bytes'])
                self.assertEqual(result['arithmetic']['residual_projection_gram_covariance'],'FP64')
            for a,b in zip(results[0]['captures'],results[1]['captures']):
                np.testing.assert_allclose(a['methods'][study.METHOD]['bit_log_scores'],b['methods'][study.METHOD]['bit_log_scores'],atol=2e-6,rtol=2e-6)


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()

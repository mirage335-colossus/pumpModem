#!/usr/bin/env python3
"""Conditional sampled-channel noise screening checks; no rare-tail claims."""
import contextlib
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
import weakref
from unittest.mock import patch
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('triad_conditional_sweep',ROOT/'tools/triad-conditional-sweep.py')
study=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(study)
FIX=importlib.util.spec_from_file_location('conditional_fixture',ROOT/'tests/test_triad_noise_study.py')
fixture=importlib.util.module_from_spec(FIX);FIX.loader.exec_module(fixture)


class ConditionalTests(unittest.TestCase):
    def workspace(self,temp,observation=False):
        path=Path(temp)/'path';path.mkdir()
        fixture.write_workspace(path,seed=610,block_count=4,mixed_taps=True,observation=observation)
        return path

    def args(self,path,output,*extra):
        return study.parse_args([str(path),'--output',str(output),'--cn0-grid=0,4','--noise-trials','8','--trial-batch','8','--particles','8',*extra])

    def test_sampler_existing_independent_dense_raw_validation(self):
        result=study.noise._projection_self_test(seed=51543,trials=6000)
        self.assertTrue(result['passed'])
        self.assertLess(result['metrics']['dense_raw_qr_residual_max_error'],1e-9)

    def test_zero_cfo_static_reference_and_three_receiver_parity(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp);args=self.args(path,Path(temp)/'unused.json')
            design,_=study.load_design(study.BlockWorkspace(path),args)
            prepared,_=study.draw_batch(design,args,0.,71,8,'signal')
            reference,details=study.score_prepared(prepared,args,'static-fft-ucb')
            # Match the static path's explicit conservative numerical-noise
            # envelope in the independent OU references; keep strict parity.
            inflation=details['whitening']['numerical_noise_inflation']
            adjusted=prepared._replace(correlations=prepared.correlations/inflation,
                grams=prepared.grams/inflation,variance_upper=prepared.variance_upper*inflation)
            for method in study.METHODS[1:3]:
                actual,_=study.score_prepared(adjusted,args,method)
                np.testing.assert_allclose(actual,reference,atol=2e-12,rtol=2e-12)
            self.assertEqual(prepared.correlations.shape,(4,8,2,2));self.assertEqual(prepared.grams.shape,(4,8,2,2,2))
            self.assertFalse(hasattr(prepared,'mean'));self.assertFalse(hasattr(prepared,'truth'))
            self.assertNotIn('injected_noise_variance_simulation_only',prepared.metadata)

    def test_reference_scale_invariance_and_seed_replay(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp);a=self.args(path,Path(temp)/'unused.json')
            design,_=study.load_design(study.BlockWorkspace(path),a)
            first,sim=study.draw_batch(design,a,-2.,77,8,'signal')
            second,_=study.draw_batch(design,a,-2.,77,8,'signal')
            np.testing.assert_array_equal(first.correlations,second.correlations)
            a.reference_variance=7.
            scaled,_=study.draw_batch(design,a,-2.,77,8,'signal')
            np.testing.assert_allclose(first.correlations,scaled.correlations,atol=2e-13,rtol=2e-13)
            np.testing.assert_allclose(first.grams,scaled.grams,atol=2e-13,rtol=2e-13)
            np.testing.assert_allclose(first.variance_upper/7,scaled.variance_upper,atol=2e-12)
            self.assertAlmostEqual(sim['injected_noise_variance_simulation_only'],64/10**(-.2))

    def test_conditional_h0_screen_end_to_end_is_not_rare_tail_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            args=self.args(path,Path(temp)/'h0.json','--cn0-grid=0','--noise-trials','256','--trial-batch','256','--controls','null')
            result=study.run(args);self.assertEqual(result['failed_physical_path_records'],0)
            self.assertEqual(result['sampled_physical_paths'],1)
            row=result['paths'][0]['conditions'][0]
            with np.load(args.output.with_suffix('.scores.npz')) as arrays:
                for method in study.METHODS[:3]:
                    item=row['methods'][method]
                    self.assertEqual(item['counts']['trials'],256)
                    self.assertEqual(item['counts']['false_admitted'],0)
                    self.assertGreater(item['conditional_interval']['one_sided_95_upper'],.01)
                    self.assertLess(np.exp(arrays[item['score_array']]).mean(),1.02)
            self.assertTrue(any('not independent channel' in text for text in result['limitations']))

    def test_signal_and_null_use_same_draw_addresses_and_shared_observations(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp);args=self.args(path,Path(temp)/'paired.json','--controls','signal','null')
            seen=[];original=study.score_prepared
            def capture(prepared,args,method,receiver_cache=None):
                seen.append((id(prepared),method))
                return original(prepared,args,method,receiver_cache)
            with patch.object(study,'score_prepared',side_effect=capture):result=study.run(args)
            self.assertEqual(result['failed_physical_path_records'],0)
            conditions=result['paths'][0]['conditions']
            seeds=[c['draws_simulation_only'][0]['noise_seed'] for c in conditions]
            self.assertEqual(len(set(seeds)),1)
            for first in range(0,len(seen),3):self.assertEqual(len({v[0] for v in seen[first:first+3]}),1)
            self.assertEqual(len(conditions),4)

    def test_erasures_keep_physical_dt_and_non_linear_or_memory_reject(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp);args=self.args(path,Path(temp)/'unused.json')
            valid=np.fromfile(path/'valid_rows.u64',dtype='<u8');valid[1]=0;valid.tofile(path/'valid_rows.u64')
            for name,dtype,shape in [('gram.c128','<c16',(4,6,6)),('mean.c128','<c16',(4,6)),('signal_energy.f64','<f8',(4,))]:
                value=np.fromfile(path/name,dtype=dtype).reshape(shape);value[1]=0;value.tofile(path/name)
            design,_=study.load_design(study.BlockWorkspace(path),args)
            prepared,_=study.draw_batch(design,args,0.,12,8,'signal')
            np.testing.assert_array_equal(prepared.correlations[1],0.)
            np.testing.assert_allclose(prepared.dt_seconds,[0,.125,.125,.125],atol=0)
            self.assertEqual(prepared.metadata['complex_residual_dof'],6)
            tiny=self.args(path,Path(temp)/'tiny.json','--memory-gib','.00005')
            with patch.object(study.np,'fromfile',side_effect=AssertionError('load before memory preflight')):
                with self.assertRaises(MemoryError):study.load_design(study.BlockWorkspace(path),tiny)
            manifest=json.loads((path/'manifest.json').read_text());manifest['gaussian_projection_supported']=False;(path/'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(study.WorkspaceError,'nonlinear'):study.load_design(study.BlockWorkspace(path),args)

    def test_fp32_receiver_parity_and_dynamic_cfo_model_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp);args=self.args(path,Path(temp)/'unused.json','--coherence','10','--frequency-bound','.5')
            design,_=study.load_design(study.BlockWorkspace(path),args);prepared,_=study.draw_batch(design,args,0.,913,8,'signal')
            for method in study.METHODS[:3]:
                ref,_=study.score_prepared(prepared,args,method);args.precision='fp32'
                low,_=study.score_prepared(prepared,args,method);args.precision='fp64'
                np.testing.assert_allclose(low,ref,atol=3e-6,rtol=3e-6)

    def test_duration_sized_source_cache_released_between_paths_and_after_error(self):
        with tempfile.TemporaryDirectory() as temp:
            roots=[Path(temp)/name for name in ('one','two')]
            for root in roots:root.mkdir()
            paths=[self.workspace(root) for root in roots]
            args=study.parse_args([*(str(p) for p in paths),'--output',str(Path(temp)/'lifetime.json'),
                '--cn0-grid=0','--noise-trials','1','--trial-batch','1','--methods','gaussian-ou-ucb'])
            tracked=[];load=study.load_design;draw=study.draw_batch;calls=0
            def loaded(ws,args):
                if tracked:self.assertIsNone(tracked[-1]())
                value,bound=load(ws,args);tracked.append(weakref.ref(value.gram));return value,bound
            def failed_first(*a,**kw):
                nonlocal calls
                calls+=1
                if calls==1:raise ValueError('deliberate first-path preparation failure')
                return draw(*a,**kw)
            # Plain wrappers do not retain duration-sized arguments in mock
            # call history; this measures production ownership, not the mock.
            with patch.object(study,'load_design',new=loaded),patch.object(study,'draw_batch',new=failed_first):
                result=study.run(args)
            self.assertEqual(result['failed_physical_path_records'],1)
            self.assertEqual(result['paths'][1]['status'],'completed')
            self.assertTrue(all(ref() is None for ref in tracked))

    def test_window_static_limit_parity_plan_reuse_and_variance_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            args=self.args(path,Path(temp)/'unused.json','--methods','static-fft-ucb','gaussian-window-ucb',
                '--window-doppler-width','0','--window-blocks','4')
            design,bound=study.load_design(study.BlockWorkspace(path),args)
            prepared,_=study.draw_batch(design,args,0.,128,8,'signal')
            cache={};window,details=study.score_prepared(prepared,args,'gaussian-window-ucb',cache)
            reference,_=study.score_prepared(prepared,args,'static-fft-ucb')
            np.testing.assert_allclose(window,reference,atol=3e-12,rtol=3e-12)
            repeated,details2=study.score_prepared(prepared,args,'gaussian-window-ucb',cache)
            np.testing.assert_array_equal(window,repeated);self.assertTrue(details2['prepared_plan_reused'])
            self.assertGreaterEqual(bound['window_bound_bytes'],details['preparation_array_bound_bytes'])
            changed=prepared._replace(grams=prepared.grams.copy());changed.grams[:,1]*=1.1
            with self.assertRaisesRegex(ValueError,'shared reference Gram'):study.score_prepared(changed,args,'gaussian-window-ucb')

    def test_actual_noisy_reads_observations_only_and_counts_independent_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            roots=[Path(temp)/name for name in ('one','two')]
            for root in roots:root.mkdir()
            paths=[self.workspace(root,observation=True) for root in roots]
            manifest=json.loads((paths[1]/'manifest.json').read_text());manifest['control']='null';manifest['signal_bit_simulation_truth']='unused'
            (paths[1]/'manifest.json').write_text(json.dumps(manifest))
            for path in paths:
                np.full((4,6),complex(np.nan,np.nan),dtype='<c16').tofile(path/'mean.c128')
                np.full(4,np.nan,dtype='<f8').tofile(path/'signal_energy.f64')
            args=study.parse_args([*(str(p) for p in paths),'--output',str(Path(temp)/'actual.json'),'--mode','actual-noisy',
                '--methods','static-fft-ucb','gaussian-window-ucb','--window-doppler-width','0','--window-blocks','4'])
            original=np.fromfile
            def guarded(file,*a,**kw):
                name=Path(getattr(file,'name',file)).name
                self.assertNotIn(name,('mean.c128','signal_energy.f64'))
                return original(file,*a,**kw)
            with patch.object(study.np,'fromfile',side_effect=guarded),patch.object(study.noise,'project_joint_statistics',side_effect=AssertionError('actual observation resimulated')):
                result=study.run(args)
            self.assertEqual(result['failed_physical_path_records'],0)
            self.assertEqual(result['observation_mode'],'actual-noisy');self.assertEqual(result['configuration']['cn0_grid'],[])
            self.assertEqual(result['configuration']['noise_trials'],1)
            for path in result['paths']:
                self.assertEqual(len(path['conditions']),1);self.assertEqual(path['conditions'][0]['actual_observations'],1)
                self.assertEqual(set(path['input_hashes']),set(study.adapter.INPUT_FILES))
                values=path['conditions'][0]['methods']
                with np.load(args.output.with_suffix('.scores.npz')) as arrays:
                    np.testing.assert_allclose(arrays[values['static-fft-ucb']['score_array']],arrays[values['gaussian-window-ucb']['score_array']],atol=3e-12)
            self.assertIsNone(result['paths'][1]['truth_bit_simulation_only'])
            self.assertEqual(result['actual_summary_by_control']['signal']['gaussian-window-ucb']['counts']['trials'],1)
            self.assertTrue(result['actual_summary_by_control']['null']['gaussian-window-ucb']['null_qualification_complete'])
            self.assertNotIn('mean.c128',result['paths'][0]['input_hashes'])

    def test_actual_mode_rejects_sweeps_or_artificial_replication(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp,observation=True)
            for extra in [('--cn0-grid=0',),('--noise-trials','2'),('--trial-batch','2'),('--controls','null')]:
                with self.subTest(extra=extra),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                    study.parse_args([str(path),'--output',str(Path(temp)/'bad.json'),'--mode','actual-noisy',*extra])

    def test_actual_truncated_control_remains_in_its_declared_family(self):
        for control in ('null','wrong-key','signal'):
            with self.subTest(control=control),tempfile.TemporaryDirectory() as temp:
                roots=[Path(temp)/name for name in ('broken','complete')]
                for root in roots:root.mkdir()
                paths=[self.workspace(root,observation=True) for root in roots]
                for path in paths:
                    manifest=json.loads((path/'manifest.json').read_text());manifest['control']=control
                    (path/'manifest.json').write_text(json.dumps(manifest))
                (paths[0]/'observation.c128').write_bytes(b'')
                args=study.parse_args([*(str(p) for p in paths),'--output',str(Path(temp)/'actual.json'),
                    '--mode','actual-noisy','--methods','gaussian-ou-ucb'])
                result=study.run(args)
                self.assertEqual(result['failed_physical_path_records'],1)
                self.assertEqual(result['unclassified_failed_physical_path_records'],0)
                self.assertEqual(result['paths'][0]['control'],control)
                self.assertEqual(result['paths'][0]['status'],'error')
                self.assertIn('file size for observation.c128',result['paths'][0]['error'])
                summary=result['actual_summary_by_control'][control]['gaussian-ou-ucb']
                self.assertEqual(summary['attempted_physical_paths'],2)
                self.assertEqual(summary['completed_physical_paths'],1)
                self.assertEqual(summary['failed_physical_paths'],1)
                self.assertFalse(summary['qualification_complete'])
                if control=='signal':
                    self.assertEqual(summary['counts']['trials'],2)
                    self.assertGreaterEqual(summary['counts']['missed'],1)
                elif control=='null':self.assertFalse(summary['null_qualification_complete'])
                else:self.assertFalse(summary['wrong_key_campaign_complete'])

    def test_actual_unclassified_failure_prevents_control_qualification(self):
        for failure in ('missing-manifest','invalid-json','missing-control','unknown-control'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as temp:
                roots=[Path(temp)/name for name in ('broken','complete')]
                for root in roots:root.mkdir()
                paths=[self.workspace(root,observation=True) for root in roots]
                manifest=json.loads((paths[1]/'manifest.json').read_text());manifest['control']='null'
                (paths[1]/'manifest.json').write_text(json.dumps(manifest))
                broken=paths[0]/'manifest.json'
                if failure=='missing-manifest':broken.unlink()
                elif failure=='invalid-json':broken.write_text('{')
                else:
                    manifest=json.loads(broken.read_text())
                    if failure=='missing-control':manifest.pop('control')
                    else:manifest['control']='unrecognized'
                    broken.write_text(json.dumps(manifest));(paths[0]/'observation.c128').write_bytes(b'')
                args=study.parse_args([*(str(p) for p in paths),'--output',str(Path(temp)/'actual.json'),
                    '--mode','actual-noisy','--methods','gaussian-ou-ucb'])
                result=study.run(args)
                self.assertEqual(result['failed_physical_path_records'],1)
                self.assertEqual(result['unclassified_failed_physical_path_records'],1)
                self.assertIsNone(result['paths'][0]['control'])
                for control in study.ACTUAL_CONTROLS:
                    summary=result['actual_summary_by_control'][control]['gaussian-ou-ucb']
                    self.assertEqual(summary['unclassified_failed_physical_paths'],1)
                    self.assertFalse(summary['qualification_complete'])
                    if control=='null':
                        self.assertEqual(summary['attempted_physical_paths'],1)
                        self.assertEqual(summary['counts']['trials'],1)
                        self.assertFalse(summary['null_qualification_complete'])
                    if control=='wrong-key':self.assertFalse(summary['wrong_key_campaign_complete'])

    def test_invalid_sweep_and_model_arguments(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.workspace(temp)
            for extra in [('--cn0-grid=0,0',),('--cn0-grid=nan',),('--noise-trials','0'),('--trial-batch','9'),('--phase-diffusion','.1'),('--frequency-initialization','iid'),('--frequency-initialization','stratified')]:
                with self.subTest(extra=extra),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                    self.args(path,Path(temp)/'bad.json',*extra)


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()

#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('retention_batch_tests',HERE/'retention_batch.py')
BATCH=importlib.util.module_from_spec(spec);spec.loader.exec_module(BATCH)
SCALAR=BATCH.REFERENCE


def fixture(directory,blocks=19):
    rng=np.random.default_rng(593)
    grams=[];means=[];energies=[];valid=[];intervals=[]
    for block in range(blocks):
        rows=5 if block==blocks-1 else 8
        a=rng.normal(size=(rows,4))+1j*rng.normal(size=(rows,4))
        if block%4==0:a[:,1]=a[:,0]  # Rank-deficient full and candidate Grams.
        signal=a[:,2]+.2*(rng.normal(size=rows)+1j*rng.normal(size=rows))
        if block%6==0:signal[:]=0
        if block%7==0:
            g=np.zeros((4,4),complex);m=np.zeros(4,complex);e=0.;n=0
        else:
            g=a.conj().T@a;m=a.conj().T@signal;e=float(np.vdot(signal,signal).real);n=rows
        grams.append(g);means.append(m);energies.append(e);valid.append(n)
        intervals.append([8*block,8*block+rows])
    arrays={'gram':(np.array(grams),'<c16'),'mean':(np.array(means),'<c16'),
            'signal_energy':(np.array(energies),'<f8'),'valid_rows':(np.array(valid),'<u8'),
            'intervals':(np.array(intervals),'<u8')}
    m={'format':'triad-block-stats-v1','byte_order':'little','complex_type':'complex128',
       'real_type':'float64','integer_type':'uint64','layout':'block,row,column',
       'block_count':blocks,'dimension':4,'sample_rate':.25,'sample_count':8*blocks-3,
       'block_samples':8,'candidate_count':4,'candidates':[{'bit':0},{'bit':1},{'bit':0},{'bit':1}],
       'column_mapping':[[0],[2],[0,1],[2,3]],'signal_bit':1,'file_sizes':{},
       'cn0_db_hz':-25,'source_format':{'encrypted':True},'channel_model':{'test_fixture':True},
       'requested_active_seconds':8*blocks/.25,'actual_elapsed_seconds':(8*blocks-3)/.25}
    for name,(value,dtype) in arrays.items():
        filename=name+('.c128' if dtype=='<c16' else '.u64' if dtype=='<u8' else '.f64')
        value.astype(dtype).tofile(directory/filename)
        m[name+'_file']=filename;m[name+'_shape']=list(value.shape);m['file_sizes'][filename]=value.nbytes
    (directory/'manifest.json').write_text(json.dumps(m))
    return m


class BatchedRetentionTests(unittest.TestCase):
    def test_complete_fixture_every_report_field_and_metadata_matches(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);fixture(directory)
            scalar,sm=SCALAR.analyze_workspace(directory)
            for blocks in (1,2,7,256):
                batch,bm=BATCH.analyze_workspace(directory,batch_blocks=blocks)
                self.assertEqual(sm,bm)
                comparison=BATCH.compare_reports(scalar,batch)
                self.assertTrue(comparison['passed'],comparison)
                self.assertEqual(scalar['input_sha256'],batch['input_sha256'])
                self.assertEqual(scalar['full_bank_rank_histogram'],batch['full_bank_rank_histogram'])
                for a,b in zip(scalar['candidate_projections'],batch['candidate_projections']):
                    self.assertEqual(a['rank_histogram'],b['rank_histogram'])
                # Identical accumulation order for untouched energy inputs.
                self.assertEqual(scalar['signal_energy_samples'],batch['signal_energy_samples'])

    def test_per_block_nominal_projection_and_rank_match_raw_row_reference(self):
        rng=np.random.default_rng(875);G=[];mean=[];energy=[];valid=[]
        for i in range(21):
            raw=rng.normal(size=(11,4))+1j*rng.normal(size=(11,4))
            if i%3==0:raw[:,3]=raw[:,0]
            signal=rng.normal(size=11)+1j*rng.normal(size=11)
            scale=1e-70 if i%5==0 else 1.
            raw*=scale
            G.append(raw.conj().T@raw);mean.append(raw.conj().T@signal)
            energy.append(float(np.vdot(signal,signal).real));valid.append(11)
        actual,ranks=BATCH.project_batch(G,mean,energy,valid)
        for i in range(len(G)):
            expected,rank=SCALAR.projection(G[i],mean[i],energy[i],valid[i])
            self.assertEqual(int(ranks[i]),rank)
            self.assertAlmostEqual(actual[i],expected,delta=5e-12*max(1.,expected))

    def test_mutated_workspace_fail_closed_parity(self):
        mutations=('nonfinite','PSD','nullspace','rank','calendar','energy','erasure','shape')
        for mutation in mutations:
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temporary:
                p=Path(temporary);m=fixture(p)
                if mutation in ('nonfinite','PSD','nullspace','rank','erasure'):
                    g=np.fromfile(p/'gram.c128',dtype='<c16').reshape(m['block_count'],4,4)
                    if mutation=='nonfinite':g[1,0,0]=np.nan
                    if mutation=='PSD':g[1]=-np.eye(4)
                    if mutation=='nullspace':
                        g[1]=0
                        v=np.fromfile(p/'mean.c128',dtype='<c16').reshape(m['block_count'],4);v[1]=1;v.tofile(p/'mean.c128')
                    if mutation in ('rank','erasure'):
                        v=np.fromfile(p/'valid_rows.u64',dtype='<u8');v[1]=1 if mutation=='rank' else 0;v.tofile(p/'valid_rows.u64')
                    g.tofile(p/'gram.c128')
                if mutation=='calendar':
                    v=np.fromfile(p/'intervals.u64',dtype='<u8').reshape(m['block_count'],2);v[1,0]+=1;v.tofile(p/'intervals.u64')
                if mutation=='energy':
                    v=np.fromfile(p/'signal_energy.f64',dtype='<f8');v[1]=0;v.tofile(p/'signal_energy.f64')
                if mutation=='shape':
                    (p/'mean.c128').write_bytes((p/'mean.c128').read_bytes()[:-16])
                for function in (SCALAR.analyze_workspace,BATCH.analyze_workspace):
                    with self.assertRaises((ValueError,OverflowError)):function(p)

    def test_numeric_overflow_nullspace_and_truncated_positive_modes_match(self):
        examples=[(np.eye(2),np.full(2,1e200),1.,2,False),
                  (np.zeros((2,2)),np.ones(2),1.,2,False),
                  (np.diag([1.,1e-12]),np.array([0.,1e-6]),1.,2,True),
                  (np.array([[1e200]]),np.array([1e200]),1e200,1,True),
                  (np.zeros((2,2)),np.zeros(2),0.,0,True)]
        for G,m,E,valid,good in examples:
            if good:
                expected,rank=SCALAR.projection(G,m,E,valid)
                actual,ranks=BATCH.project_batch(G[None],m[None],[E],[valid])
                self.assertEqual(int(ranks[0]),rank)
                self.assertAlmostEqual(actual[0]/max(1.,expected),expected/max(1.,expected),places=12)
            else:
                for function,args in ((SCALAR.projection,(G,m,E,valid)),
                                      (BATCH.project_batch,(G[None],m[None],[E],[valid]))):
                    with self.assertRaises(ValueError):function(*args)

    def test_memory_chunk_controls_and_source_bindings(self):
        with tempfile.TemporaryDirectory() as temporary:
            p=Path(temporary);fixture(p)
            scalar,_=SCALAR.analyze_workspace(p)
            bounded,_=BATCH.analyze_workspace(p,batch_blocks=1024,memory_limit_bytes=8192)
            self.assertTrue(BATCH.compare_reports(scalar,bounded)['passed'])
            for options in ({'batch_blocks':0},{'memory_limit_bytes':2048},{'memory_limit_bytes':4096}):
                with self.assertRaises(ValueError):BATCH.analyze_workspace(p,**options)
            bindings=BATCH.source_bindings()
            self.assertEqual(bindings['scalar_reference_sha256'],SCALAR.sha256(BATCH.REFERENCE_PATH))
            self.assertEqual(len(bindings['batch_module_sha256']),64)

    def test_reference_module_is_not_mutated(self):
        before=SCALAR.projection;workspace_before=SCALAR.analyze_workspace
        with tempfile.TemporaryDirectory() as temporary:
            p=Path(temporary);fixture(p);BATCH.analyze_workspace(p)
        self.assertIs(SCALAR.projection,before)
        self.assertIs(SCALAR.analyze_workspace,workspace_before)


if __name__=='__main__':unittest.main()

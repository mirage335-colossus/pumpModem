#!/usr/bin/env python3
"""Tiny independent raw-row checks of the evidence aggregation audit."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

SPEC=importlib.util.spec_from_file_location('compression_audit',Path(__file__).with_name('audit_compression.py'))
audit=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(audit)


class CompressionAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        rng=np.random.default_rng(81945)
        self.A=rng.normal(size=(16,2))+1j*rng.normal(size=(16,2))
        self.s=rng.normal(size=16)+1j*rng.normal(size=16)
        self.y=self.s+.4*(rng.normal(size=16)+1j*rng.normal(size=16))
        self.valid=np.ones(16,bool);self.valid[3]=False

    def workspace(self,name,block,n=14):
        path=self.root/name;path.mkdir();b=(n+block-1)//block
        intervals=np.asarray([(j*block,min((j+1)*block,n)) for j in range(b)],dtype='<u8')
        arrays={key:[] for key in audit.ARRAYS}
        for lo,hi in intervals:
            mask=self.valid[int(lo):int(hi)];A=self.A[int(lo):int(hi)][mask];s=self.s[int(lo):int(hi)][mask];y=self.y[int(lo):int(hi)][mask]
            arrays['gram'].append(A.conj().T@A);arrays['mean'].append(A.conj().T@s)
            arrays['observation'].append(A.conj().T@y);arrays['signal_energy'].append(float(np.vdot(s,s).real))
            arrays['received_energy'].append(float(np.vdot(y,y).real));arrays['valid_rows'].append(len(s))
        m={'format':'triad-block-stats-v1','byte_order':'little','complex_type':'complex128','real_type':'float64','integer_type':'uint64',
            'layout':'block,row,column','block_count':b,'block_samples':block,'dimension':2,'sample_count':n,'sample_rate':64.,'stats_noisy':True,
            'key_seed':514,'epoch':1800000000,'signal_bit':0,'control':'signal','source_samples':14,
            'random_seed_roots':{k:i+71 for i,k in enumerate(('channel','oscillator','noise','frontend','path'))},
            'source_format':{'encrypted':True},'file_sizes':{},
            'command_argv':['/tool','--block-seconds',str(block/64),'--output',str(path)]}
        for key,(dtype,_) in audit.ARRAYS.items():
            value=np.asarray(arrays[key],dtype=dtype);filename=key+('.c128' if dtype=='<c16' else '.u64' if dtype=='<u8' else '.f64')
            value.tofile(path/filename);m[key+'_file']=filename;m[key+'_shape']=list(value.shape);m['file_sizes'][filename]=value.nbytes
        intervals.tofile(path/'intervals.u64');m['intervals_file']='intervals.u64';m['intervals_shape']=list(intervals.shape);m['file_sizes']['intervals.u64']=intervals.nbytes
        (path/'manifest.json').write_text(json.dumps(m));return path

    def test_raw_sample_sums_with_erasure_and_partial_last_block(self):
        result=audit.audit_pair(self.workspace('fine',2),self.workspace('coarse',8))
        self.assertTrue(result['passed']);self.assertEqual(result['last_compared_coarse_fine_blocks'],3)
        self.assertEqual(result['last_coarse_interval_rows'],6);self.assertEqual(result['compared_prefix_samples'],14)
        self.assertAlmostEqual(result['signal_energy_seconds']['fine'],float(np.sum(np.abs(self.s[:14][self.valid[:14]])**2))/64)
        self.assertTrue(all(x['passed'] for x in result['statistics'].values()))

    def test_changed_actual_noise_projection_is_not_paired(self):
        fine=self.workspace('fine',2);coarse=self.workspace('coarse',8)
        value=np.fromfile(coarse/'observation.c128',dtype='<c16');value[0]+=.001;value.tofile(coarse/'observation.c128')
        result=audit.audit_pair(fine,coarse)
        self.assertFalse(result['passed']);self.assertFalse(result['statistics']['observation']['passed'])
        self.assertTrue(result['statistics']['mean']['passed'])

    def test_different_noise_root_or_tail_never_forced_to_pass(self):
        fine=self.workspace('fine',2);coarse=self.workspace('coarse',8)
        manifest=json.loads((coarse/'manifest.json').read_text());manifest['random_seed_roots']['noise']+=1
        (coarse/'manifest.json').write_text(json.dumps(manifest));result=audit.audit_pair(fine,coarse)
        self.assertFalse(result['passed']);self.assertIn('random_seed_roots.noise',result['unexpected_metadata_differences'])
        tail=self.workspace('tail',8,n=16);result=audit.audit_pair(fine,tail)
        self.assertFalse(result['passed']);self.assertFalse(result['full_capture_support_equal'])
        self.assertEqual(result['compared_prefix_samples'],8);self.assertEqual(result['fine_uncompared_tail_samples'],6)
        self.assertEqual(result['coarse_uncompared_tail_samples'],8)

    def test_actual_score_recount_and_path_error_remain_visible(self):
        self.workspace('fine',2)
        source=str(audit.REMOTE_ROOT/'fine');output=str(audit.REMOTE_ROOT/'score.json')
        job={'name':'fine-score','argv':['/python','/study',source,'--output',output]}
        values=np.asarray([[6.,-2.]])
        np.savez_compressed(self.root/'scores.npz',scores=values)
        counts={'trials':1,'correct_accepted':1,'wrong_accepted':0,'missed':0,'raw_correct':1}
        row={'input':source,'status':'completed','truth_bit_simulation_only':0,'conditions':[
            {'control':'signal','noise_trials':1,'methods':{'gaussian-window-ucb':{'score_array':'scores','threshold':5.,'counts':counts}}}]}
        doc={'schema':'triad-conditional-sweep-v1','observation_mode':'actual-noisy','paths':[row],
            'configuration':{'noise_trials':1,'window_blocks':32,'window_doppler_width':10.},
            'scores_file':'scores.npz','scores_sha256':audit.sha256(self.root/'scores.npz')}
        (self.root/'score.json').write_text(json.dumps(doc));result=audit.score_summary(self.root,job)
        self.assertTrue(result['passed']);self.assertEqual(result['counts']['correct_accepted'],1)
        row['conditions'][0]['methods']['gaussian-window-ucb']['counts']['correct_accepted']=0
        (self.root/'score.json').write_text(json.dumps(doc))
        with self.assertRaisesRegex(ValueError,'counts disagree'):audit.score_summary(self.root,job)
        row.update(status='error',conditions=[],error='original export failed')
        np.savez_compressed(self.root/'scores.npz');doc['scores_sha256']=audit.sha256(self.root/'scores.npz')
        (self.root/'score.json').write_text(json.dumps(doc));result=audit.score_summary(self.root,job)
        self.assertFalse(result['passed']);self.assertEqual(result['counts']['failed_paths'],1)
        self.assertEqual(result['counts']['signal_attempts'],1);self.assertEqual(result['counts']['missed'],1)
        self.assertEqual(result['path_errors'][0]['error'],'original export failed')


if __name__=='__main__':unittest.main()

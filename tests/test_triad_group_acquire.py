#!/usr/bin/env python3
"""Small raw-observation checks for fixed grouped acquisition."""
import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import numpy as np
import torch
from scipy.special import gammaincinv, logsumexp

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from triad_group_acquire import (AcquisitionConfig, GroupedWorkspace, WorkspaceError,
    bit_mixture, decision, main, parse_args, prepare_group_tile, score_workspace)
from triad_gaussian_window import PreparedWindowedGaussian
from triad_noise_likelihood import whole_bank_residual, variance_upper_confidence


def cn(rng,shape):
    return (rng.normal(size=shape)+1j*rng.normal(size=shape))/math.sqrt(2)


def fixture(seed=918,blocks=6,groups=3,rows=8,rank_deficient=False,erased=False):
    rng=np.random.default_rng(seed)
    A=.3*cn(rng,(blocks,groups,rows,4))
    y=cn(rng,(blocks,rows))  # Exactly one observation shared by every group.
    valid=np.full(blocks,rows,dtype=np.uint64)
    if rank_deficient:
        A[1,:,:,3]=(.4+.2j)*A[1,:,:,2]
    if erased:
        A[2]=0;y[2]=0;valid[2]=0
        A[3,:,-2:]=0;y[3,-2:]=0;valid[3]-=2
    G=np.einsum('bhni,bhnj->bhij',A.conj(),A)
    r=np.einsum('bhni,bn->bhi',A.conj(),y)
    E=np.einsum('bn,bn->b',y.conj(),y).real
    mapping=np.tile(np.asarray([[0,1],[2,3]])[None],(groups,1,1))
    return A,y,G,r,E,valid,mapping


def write_fixture(directory, data, sample_rate=128.):
    A,y,G,r,E,valid,mapping=data
    b,h,rows,d=A.shape
    candidates=[];groups=[]
    for gi in range(h):
        key=dict(arrival_seconds=.01*gi,frequency_hz=0.,clock_ppm=0.,epoch_offset_seconds=0,frequency_drift_hz_per_second=0.)
        ids=[];maps=[];descriptors=[]
        for bit in (0,1):
            ci=len(candidates)
            candidate=dict(key,bit=bit,tap_count=2,taps=[dict(delay_seconds=0.,doppler_hz=0.),dict(delay_seconds=.007,doppler_hz=30.)])
            candidates.append(candidate);ids.append(ci);maps.append(mapping[gi,bit].tolist())
            for ti,tap in enumerate(candidate['taps']):
                descriptors.append(dict(key,source_bit=bit,tap_delay_seconds=tap['delay_seconds'],tap_doppler_hz=tap['doppler_hz'],
                    linear_gain=False,gain_column=0,candidate_index=ci))
        groups.append(dict(sync_key=key,candidate_indices=ids,column_mapping=maps,column_descriptors=descriptors))
    arrays={'group_gram.c128':G.astype('<c16'),'group_observation.c128':r.astype('<c16'),
            'received_energy.f64':E.astype('<f8'),'intervals.u64':np.column_stack((np.arange(b)*rows,np.arange(1,b+1)*rows)).astype('<u8'),
            'valid_rows.u64':valid.astype('<u8')}
    m=dict(format='triad-group-block-stats-v1',layout='block,group,row,column',byte_order='little',complex_type='complex128',
           real_type='float64',integer_type='uint64',stats_noisy=True,gaussian_projection_supported=True,
           cross_group_gram_saved=False,conditional_noise_resampling_supported=False,block_count=b,group_count=h,dimension=d,
           sample_count=b*rows,block_samples=rows,sample_rate=sample_rate,candidate_count=len(candidates),candidates=candidates,groups=groups,
           gram_file='group_gram.c128',gram_shape=list(G.shape),observation_file='group_observation.c128',observation_shape=list(r.shape),
           received_energy_file='received_energy.f64',received_energy_shape=list(E.shape),intervals_file='intervals.u64',intervals_shape=[b,2],
           valid_rows_file='valid_rows.u64',valid_rows_shape=[b],file_sizes={name:value.nbytes for name,value in arrays.items()},
           control='signal',signal_bit_simulation_truth=0,noise_variance=99999,selected_truth_arrival_seconds_simulation_only=923.)
    directory.mkdir(parents=True,exist_ok=True)
    for name,value in arrays.items():value.tofile(directory/name)
    (directory/'manifest.json').write_text(json.dumps(m))
    return m


class GroupAcquisitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(1)

    def test_explicit_default_sized_window_is_a_valid_cli_choice(self):
        args=parse_args(['fixture','--doppler-width-hz','0','--window-blocks','16'])
        self.assertEqual(args.window_blocks,16)
        self.assertIsNone(args.window_seconds)
        args=parse_args(['fixture','--doppler-width-hz','0','--window-seconds','.0625'])
        self.assertEqual(args.window_seconds,.0625)
        self.assertEqual(args.window_blocks,16)


    def test_group_residual_matches_raw_qr_with_erasure_and_rank_deficiency(self):
        data=fixture(rank_deficient=True,erased=True)
        A,y,G,r,E,valid,mapping=data
        result=prepare_group_tile(G,r,E,valid,mapping,reference=2.3,delta=1e-6,projection_tile_blocks=2)
        correlations,grams,upper,residual,dof,ranks=result
        for group in range(G.shape[1]):
            expected_residual=0;expected_dof=0
            for block in range(len(G)):
                count=int(valid[block]);design=A[block,group,:count];obs=y[block,:count]
                u,s,_=np.linalg.svd(design,full_matrices=False)
                keep=s*s>1e-10*(s[0]*s[0] if len(s) else 0)
                project=u[:,keep]@(u[:,keep].conj().T@obs)
                expected_residual+=np.vdot(obs-project,obs-project).real/2.3
                expected_dof+=count-int(keep.sum())
            self.assertAlmostEqual(residual[group],expected_residual,places=12)
            self.assertEqual(dof[group],expected_dof)
            self.assertAlmostEqual(upper[group],expected_residual/gammaincinv(expected_dof,1e-6),places=12)
            self.assertEqual(int(ranks[group].sum()),len(G))
        self.assertEqual(correlations.shape,(6,3,2,2))
        self.assertEqual(grams.shape,(6,3,2,2,2))

    def test_single_group_matches_existing_whole_bank_and_window_receiver(self):
        data=fixture(groups=1,rank_deficient=True,erased=True)
        _,_,G,r,E,valid,mapping=data
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);write_fixture(path,data)
            config=AcquisitionConfig(1.5,window_blocks=3)
            report=score_workspace(GroupedWorkspace(path),config)
        projected_r=[];projected_G=[];residual=0;dof=0
        for j in range(len(G)):
            fit=whole_bank_residual(torch.from_numpy(r[j,0]),torch.from_numpy(G[j,0]),E[j],int(valid[j]))
            projected_r.append(fit.projected_correlations.numpy()[mapping[0]])
            projected_G.append(fit.projected_gram.numpy()[mapping[0,:,:,None],mapping[0,:,None,:]])
            residual+=float(fit.energy);dof+=int(fit.complex_dof)
        upper=float(variance_upper_confidence(residual,dof,config.budgets()[0].delta_per_segment))
        plan=PreparedWindowedGaussian(np.asarray(projected_G),np.arange(6)/16+1/32,doppler_width_hz=1.5,window_blocks=3)
        expected=plan.score(np.asarray(projected_r),variance_scale=upper).numpy()
        np.testing.assert_allclose(report['bit_log_likelihoods'],expected,atol=1e-12,rtol=1e-12)

    def test_all_groups_are_mixed_and_calibration_is_preallocated(self):
        data=fixture(groups=3)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);write_fixture(path,data)
            config=AcquisitionConfig(1.2,window_blocks=2,group_tile=1)
            report=score_workspace(GroupedWorkspace(path),config)
            tiled=score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.2,window_blocks=2,group_tile=3))
        scores=np.asarray(report['group_bit_log_likelihoods'])
        np.testing.assert_allclose(report['bit_log_likelihoods'],logsumexp(scores,axis=0)-math.log(3),atol=1e-14)
        np.testing.assert_allclose(tiled['bit_log_likelihoods'],report['bit_log_likelihoods'],atol=1e-12)
        expected=math.nextafter(config.budgets()[0].delta_per_segment/3,0.)
        self.assertEqual(report['calibration']['delta_per_group'],expected)
        self.assertFalse(report['calibration']['inter_group_independence_assumed'])
        self.assertEqual(len(report['calibration']['variance_upper_over_reference']),3)

    def test_reblock_matches_direct_combined_raw_rows(self):
        data=fixture(blocks=6,groups=2)
        A,y,_,_,_,_,_=data
        combined_A=A.reshape(3,2,2,8,4).transpose(0,2,1,3,4).reshape(3,2,16,4)
        combined_y=y.reshape(3,16)
        G=np.einsum('bhni,bhnj->bhij',combined_A.conj(),combined_A)
        r=np.einsum('bhni,bn->bhi',combined_A.conj(),combined_y)
        E=np.sum(abs(combined_y)**2,axis=1)
        direct=(combined_A,combined_y,G,r,E,np.full(3,16,dtype=np.uint64),data[-1])
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);write_fixture(root/'fine',data);write_fixture(root/'coarse',direct)
            fine=score_workspace(GroupedWorkspace(root/'fine'),AcquisitionConfig(.5,window_seconds=.25,reblock=2))
            coarse=score_workspace(GroupedWorkspace(root/'coarse'),AcquisitionConfig(.5,window_seconds=.25))
        np.testing.assert_allclose(fine['bit_log_likelihoods'],coarse['bit_log_likelihoods'],atol=2e-12)
        np.testing.assert_allclose(fine['calibration']['variance_upper_over_reference'],coarse['calibration']['variance_upper_over_reference'],atol=2e-12)
        self.assertEqual(fine['geometry']['nominal_window_seconds'],.25)

    def test_truth_changes_do_not_change_receiver_or_open_hidden_arrays(self):
        data=fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);m=write_fixture(path,data)
            before=score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.))
            m.update(noise_variance=1e-12,signal_bit_simulation_truth=1,control='wrong-key',selected_truth_arrival_seconds_simulation_only=-100.)
            m['mean_file']='DO_NOT_OPEN';m['signal_energy_file']='DO_NOT_OPEN'
            (path/'manifest.json').write_text(json.dumps(m))
            after=score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.))
        self.assertEqual(before['bit_log_likelihoods'],after['bit_log_likelihoods'])
        self.assertEqual(before['strict'],after['strict'])

    def test_bad_shapes_groups_frontend_and_data_fail_closed(self):
        data=fixture()
        edits=(lambda m:m.update(stats_noisy=False),lambda m:m.update(gaussian_projection_supported=False),
               lambda m:m.update(gram_shape=[1,1]),lambda m:m['groups'][0]['candidate_indices'].append(0),
               lambda m:m['groups'][0]['sync_key'].update(arrival_seconds=7),
               lambda m:m.update(conditional_noise_resampling_supported=True))
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            for edit in edits:
                m=write_fixture(path,data);edit(m);(path/'manifest.json').write_text(json.dumps(m))
                with self.assertRaises(WorkspaceError):GroupedWorkspace(path)
            write_fixture(path,data)
            bad=np.array(data[3]);bad[0,0,0]=np.nan;bad.astype('<c16').tofile(path/'group_observation.c128')
            with self.assertRaisesRegex(WorkspaceError,'nonfinite'):
                score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.))
            write_fixture(path,data)
            bad=np.array(data[4]);bad[0]=-1;bad.astype('<f8').tofile(path/'received_energy.f64')
            with self.assertRaisesRegex(WorkspaceError,'energy'):
                score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.))

    def test_epoch_keys_remain_exact_above_float_integer_range(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);m=write_fixture(path,fixture(groups=2))
            for gi,group in enumerate(m['groups']):
                key=dict(group['sync_key'],arrival_seconds=0.,epoch_offset_seconds=2**53+gi)
                group['sync_key']=key
                for ci in group['candidate_indices']:m['candidates'][ci].update(key)
                for descriptor in group['column_descriptors']:descriptor.update(key)
            (path/'manifest.json').write_text(json.dumps(m))
            ws=GroupedWorkspace(path)
            self.assertEqual([x['epoch_offset_seconds'] for x in ws.sync_keys],[2**53,2**53+1])
            for invalid in (True,2**63,-2**63-1):
                bad=copy.deepcopy(m);bad['groups'][0]['sync_key']['epoch_offset_seconds']=invalid
                (path/'manifest.json').write_text(json.dumps(bad))
                with self.assertRaisesRegex(WorkspaceError,'exact signed'):
                    GroupedWorkspace(path)

    def test_zero_dof_and_memory_guards(self):
        data=fixture(rows=4,groups=1)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);write_fixture(path,data)
            with self.assertRaisesRegex(ValueError,'degrees of freedom'):
                score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.))
            write_fixture(path,fixture())
            with patch('triad_group_acquire.PreparedWindowedGaussian',side_effect=AssertionError('factor before preflight')):
                with self.assertRaises(MemoryError):
                    score_workspace(GroupedWorkspace(path),AcquisitionConfig(1.,memory_limit_bytes=4096))
        # A tiny working tile does not remove the whole-bank result/mapping
        # arrays. This must fail before any calendar or scoring allocation.
        large=SimpleNamespace(blocks=1,groups=5000,dimension=4)
        with self.assertRaises(MemoryError):
            score_workspace(large,AcquisitionConfig(1.,memory_limit_bytes=300000))

    def test_fp32_bulk_decisions_and_known_noise_diagnostic(self):
        data=fixture(blocks=8)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);write_fixture(path,data)
            precise=score_workspace(GroupedWorkspace(path),AcquisitionConfig(.5,window_blocks=3))
            bulk=score_workspace(GroupedWorkspace(path),AcquisitionConfig(.5,window_blocks=3,precision='fp32'))
            diagnostic=score_workspace(GroupedWorkspace(path),AcquisitionConfig(.5,window_blocks=3,known_noise_variance=1.))
        np.testing.assert_allclose(bulk['bit_log_likelihoods'],precise['bit_log_likelihoods'],atol=2e-6,rtol=2e-6)
        values=np.asarray(precise['bit_log_likelihoods']);other=np.asarray(bulk['bit_log_likelihoods'])
        for gate in (max(values)+1e-4,max(values)-1e-4):
            self.assertEqual(decision(values,gate)['accepted'],decision(other,gate)['accepted'])
            self.assertEqual(decision(values,gate)['bit'],decision(other,gate)['bit'])
        self.assertIn('known-noise-diagnostic',diagnostic['method'])
        self.assertEqual(diagnostic['calibration']['known_noise_variance_diagnostic'],1.)

    def test_cli_actual_only_and_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);write_fixture(root/'input',fixture(groups=1))
            code=main([str(root/'input'),'--doppler-width-hz','1','--window-blocks','2',
                       '--evaluate-simulation','--output',str(root/'result.json'),'--scores-output',str(root/'scores.npz')])
            self.assertEqual(code,0)
            report=json.loads((root/'result.json').read_text())
            self.assertEqual(len(report['captures']),1)
            self.assertIn('simulation_evaluation_only',report['captures'][0])
            self.assertTrue((root/'scores.npz').is_file())
            with self.assertRaises(ValueError):main([str(root/'input'),'--doppler-width-hz','1','--window-blocks','2','--output',str(root/'result.json')])


if __name__=='__main__':unittest.main()

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('selective_retention', HERE/'retention.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)


def fixture(directory):
    rng=np.random.default_rng(72)
    A=rng.normal(size=(13,4))+1j*rng.normal(size=(13,4))
    signal=A[:,2]+.3*(rng.normal(size=13)+1j*rng.normal(size=13))
    G=[];mean=[];energy=[]
    for lo,hi in ((0,8),(8,13)):
        a=A[lo:hi];s=signal[lo:hi]
        G.append(a.conj().T@a);mean.append(a.conj().T@s);energy.append(np.vdot(s,s).real)
    arrays={'gram':(np.asarray(G),'<c16'),'mean':(np.asarray(mean),'<c16'),
        'signal_energy':(np.asarray(energy),'<f8'),'valid_rows':(np.array([8,5]),'<u8'),
        'intervals':(np.array([[0,8],[8,13]]),'<u8')}
    m={'format':'triad-block-stats-v1','byte_order':'little','complex_type':'complex128',
       'real_type':'float64','integer_type':'uint64','layout':'block,row,column',
       'dimension':4,'block_count':2,'sample_count':13,'block_samples':8,'sample_rate':2,
       'noise_variance':2,'candidate_count':2,'candidates':[{'bit':0},{'bit':1}],
       'column_mapping':[[0,1],[2,3]],'signal_bit':1,'file_sizes':{},'key_seed':4,'epoch':5,
       'source_samples':13,'source_format':{'rate':1},'endpoint_truth':{'clock':0},
       'source_hash_binding':{'src':'123'},'random_seed_roots':dict(zip(('channel','oscillator','noise','frontend','path'),range(5))),
       'channel_model':{'spread_doppler_hz':.001,'perturbation_slow_depth':0,'perturbation_fast_depth':0},
       'oscillator_model':{'relative_white_fm_hz_sqrt_s':0},'sampled_frontend':{'gain_step_factor':1},
       'capture_lead_seconds':0,'requested_active_seconds':6,'actual_elapsed_seconds':6.5,
       'selected_truth_arrival_seconds_simulation_only':0,'selected_truth_frequency_hz_simulation_only':0,
       'selected_truth_clock_ppm_simulation_only':0,'selected_truth_frequency_drift_hz_per_second_simulation_only':0,
       'resolved_configuration':{'selective_perturbation':'off','capture':{'deterministic_nyquist_margin_hz':1}}}
    for name,(value,dtype) in arrays.items():
        filename=name+('.c128' if dtype=='<c16' else '.u64' if dtype=='<u8' else '.f64')
        value.astype(dtype).tofile(directory/filename)
        m[name+'_file']=filename;m[name+'_shape']=list(value.shape);m['file_sizes'][filename]=value.nbytes
    (directory/'manifest.json').write_text(json.dumps(m))
    return A,signal,m


class RetentionTests(unittest.TestCase):
    def test_projection_matches_independent_raw_row_lstsq_and_rank_deficiency(self):
        rng=np.random.default_rng(38)
        for duplicate in (False,True):
            A=rng.normal(size=(17,3))+1j*rng.normal(size=(17,3))
            if duplicate:A[:,2]=A[:,0]
            y=rng.normal(size=17)+1j*rng.normal(size=17)
            energy=float(np.vdot(y,y).real)
            observed,rank=r.projection(A.conj().T@A,A.conj().T@y,energy,17)
            fit,_,expected_rank,_=np.linalg.lstsq(A,y,rcond=1e-5)
            self.assertEqual(rank,expected_rank)
            self.assertAlmostEqual(observed,float(np.vdot(A@fit,A@fit).real),places=11)

    def test_projection_rejects_overflow_and_true_nullspace_but_keeps_truncated_modes(self):
        with self.assertRaisesRegex(ValueError,'finite range'):
            r.projection(np.eye(2),np.full(2,1e200),1.,2)
        with self.assertRaisesRegex(ValueError,'nullspace'):
            r.projection(np.zeros((2,2)),np.ones(2),1.,2)
        with self.assertRaisesRegex(ValueError,'nullspace'):
            r.projection(np.diag([1.,0]),np.array([0.,1.]),1.,2)
        # A deliberately truncated positive mode is still physically possible.
        self.assertEqual(r.projection(np.diag([1.,1e-12]),np.array([0.,1e-6]),1.,2),(0.,1))
        # This physically finite projected energy must avoid the old |m|^2 overflow.
        value,rank=r.projection(np.array([[1e200]]),np.array([1e200]),1e200,1)
        self.assertAlmostEqual(value/1e200,1.)
        self.assertEqual(rank,1)

    def test_completed_workspace_energy_units_candidate_order_and_tail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);A,y,m=fixture(path);result,_=r.analyze_workspace(path)
            expected=[]
            for columns in ([0,1],[2,3]):
                total=0.
                for lo,hi in ((0,8),(8,13)):
                    a=A[lo:hi,columns];v=y[lo:hi];fit=np.linalg.lstsq(a,v,rcond=1e-5)[0]
                    total+=float(np.vdot(a@fit,a@fit).real)
                expected.append(total)
            self.assertEqual(result['sample_count'],13)
            self.assertEqual(result['full_bank_noise_residual_complex_dof'],5)
            self.assertAlmostEqual(result['signal_energy_seconds'],float(np.vdot(y,y).real)/2)
            self.assertEqual(result['best_fixed_correct_label_candidate'],1)
            for item,e in zip(result['candidate_projections'],expected):
                self.assertAlmostEqual(item['projection_energy_samples'],e,places=11)
            self.assertGreater(result['correct_label_retained_fraction'],result['wrong_label_retained_fraction'])

    def test_validation_rejects_corrupted_energy_calendar_and_hermitian(self):
        with self.assertRaisesRegex(ValueError,'signal energy'):
            r.projection(np.eye(2),np.ones(2),.1,4)
        with self.assertRaisesRegex(ValueError,'Hermitian'):
            r.projection(np.array([[1,1j],[0,1]]),np.zeros(2),1,4)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);_,_,m=fixture(p)
            np.array([[0,8],[9,13]],dtype='<u8').tofile(p/'intervals.u64')
            with self.assertRaisesRegex(ValueError,'calendar'):r.analyze_workspace(p)

    def test_zero_energy_and_erasure_are_explicit(self):
        self.assertEqual(r.projection(np.zeros((2,2)),np.zeros(2),0.,0),(0.,0))
        self.assertIsNone(r.ratio(0,0))
        with self.assertRaisesRegex(ValueError,'zero signal energy|erased'):r.projection(np.zeros((2,2)),np.ones(2),0.,0)

    def test_pair_checks_only_allowed_am_changes_and_preserves_tail_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            _,_,left=fixture(Path(tmp));right=copy.deepcopy(left)
            right['channel_model']['perturbation_slow_depth']=.65
            right['sample_count']+=1
            result=r.paired_metadata(left,right)
            self.assertTrue(result['recorded_pair_configuration_matches'])
            self.assertFalse(result['same_sample_support'])
            right['channel_model']['spread_doppler_hz']=.01
            self.assertFalse(r.paired_metadata(left,right)['recorded_pair_configuration_matches'])
            right=copy.deepcopy(left);del right['random_seed_roots']['noise']
            self.assertFalse(r.paired_metadata(left,right)['recorded_pair_configuration_matches'])

    def test_pair_rejects_all_original_unbound_scientific_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            _,_,left=fixture(Path(tmp))
            changes=[('oscillator_model','relative_white_fm_hz_sqrt_s',3),
                     ('sampled_frontend','gain_step_factor',.1),
                     (None,'selected_truth_arrival_seconds_simulation_only',1),
                     (None,'capture_lead_seconds',1),
                     (None,'requested_active_seconds',12),
                     ('resolved_configuration','extra_future_physical_control',2)]
            for container,key,value in changes:
                right=copy.deepcopy(left)
                (right if container is None else right[container])[key]=value
                self.assertFalse(r.paired_metadata(left,right)['recorded_pair_configuration_matches'],key)
            right=copy.deepcopy(left);del right['oscillator_model']
            self.assertFalse(r.paired_metadata(left,right)['recorded_pair_configuration_matches'])
            right=copy.deepcopy(left)
            right['resolved_configuration']['selective_perturbation']='each-65'
            right['channel_model']['perturbation_slow_depth']=.65
            right['resolved_configuration']['capture']['deterministic_nyquist_margin_hz']=.8
            right['sample_count']+=1;right['block_count']+=1
            right['gram_shape'][0]+=1
            right['file_sizes']['gram.c128']+=256
            self.assertTrue(r.paired_metadata(left,right)['recorded_pair_configuration_matches'])
            right['gram_shape'][1]+=1
            self.assertFalse(r.paired_metadata(left,right)['recorded_pair_configuration_matches'])

    def test_optional_selective_telemetry_keeps_physical_units_and_stage(self):
        self.assertFalse(r.selective_energy_telemetry({})['available'])
        m={'path_truth_simulation_only':{'selective_perturbation':{'enabled':True,
           'sample_rate_hz':2.,'pre_selective_energy_samples':100.,'post_selective_energy_samples':200.,
           'processed_rows':50,'component_depths':[.65,.65],'component_rates_hz':[.001,.01]}}}
        actual=r.selective_energy_telemetry(m)
        self.assertEqual(actual['post_over_pre_signal_energy_ratio'],2.)
        self.assertEqual(actual['pre_selective_energy_seconds'],50.)
        self.assertEqual(actual['post_selective_energy_seconds'],100.)
        m['path_truth_simulation_only']['selective_perturbation']['post_selective_energy_samples']=float('inf')
        with self.assertRaisesRegex(ValueError,'signal energy'):r.selective_energy_telemetry(m)

    def test_cli_preserves_failure_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);data=p/'workspace';data.mkdir();fixture(data);out=p/'result.json'
            self.assertEqual(r.main([str(data),str(p/'missing'),'--output',str(out)]),1)
            doc=json.loads(out.read_text())
            self.assertEqual(doc['workspaces'][0]['status'],'completed')
            self.assertEqual(doc['workspaces'][1]['status'],'error')
            self.assertFalse(doc['scientific_qualification'])
            with self.assertRaises(FileExistsError):r.main([str(data),'--output',str(out)])


if __name__=='__main__':unittest.main()

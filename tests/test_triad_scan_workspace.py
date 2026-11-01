#!/usr/bin/env python3
"""Fresh-input validation and optional actual whole-scan graph tests."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys
import unittest
import weakref
import numpy as np
import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_scan_workspace import (WholeScanWorkspace,validate_observation,preparation_estimate,
                                   backend_contract_metadata)

CUDA='--cuda' in sys.argv


class Checks(unittest.TestCase):
    def test_fresh_validation(self):
        a=np.ones((2,19),dtype=np.complex128)
        validate_observation(a,a.shape,torch.complex128,fp32_input=True)
        for value in (a[:1],a.astype(np.complex64),a*np.nan,a*1e40):
            with self.assertRaises(ValueError):
                validate_observation(value,a.shape,torch.complex128,fp32_input=True)
        validate_observation(a*1e40,a.shape,torch.complex128,fp32_input=False)
        validate_observation(torch.from_numpy(a),a.shape,torch.complex128,fp32_input=True)

    def test_estimate_and_cpu_rejection(self):
        bank=SimpleNamespace(device=torch.device('cpu'),lags=[0,2],frequencies=[-.1,0.,.1],
            samples=169,block_samples=2,_half_alignment=1,candidates=2,taps=1,backend=None,
            frequency_tile=3,candidate_tile=2,column_tile=128,
            sample_rate=11.,precision='fp32',_basis=np.ones((2,169,1),dtype=np.complex128),block_count=85)
        grouped=SimpleNamespace(groups=[(None,1,SimpleNamespace(working_bytes_bound=4096,
                                                               frequency_states=1,phase_states=16),None)])
        estimate=preparation_estimate(bank,grouped,(1,171),torch.complex128)
        self.assertEqual(estimate['capture_transient_bound_bytes'],estimate['graph_private_pool_bound_bytes'])
        self.assertEqual(estimate['capture_transient_bound_bytes'],estimate['eager_warm_pool_bound_bytes'])
        longer=SimpleNamespace(**vars(bank))
        longer.samples=337;longer.block_count=169;longer._basis=np.ones((2,337,1),dtype=np.complex128)
        second=preparation_estimate(longer,grouped,(1,339),torch.complex128)
        self.assertEqual(estimate['capture_transient_bound_bytes'],second['capture_transient_bound_bytes'])
        self.assertGreater(second['schedule_packed_a_bytes'],estimate['schedule_packed_a_bytes'])
        with self.assertRaisesRegex(RuntimeError,'requires CUDA'):
            WholeScanWorkspace(bank,grouped,(1,171),noise_variance=1.)

    def test_full_p512_planning_preserves_model_on_16gb(self):
        bank=SimpleNamespace(lags=[0,1,2],frequencies=[-.01,0.,.01],samples=21673,
            sample_rate=64.,block_samples=256,block_count=85,_half_alignment=1,
            candidates=4,taps=1,backend=None,precision='fp32',frequency_tile=4,candidate_tile=16,
            column_tile=128,gram_cache_bytes=16*1024**2,_basis=np.empty((4,21673,1),dtype=np.complex128))
        grouped=SimpleNamespace(groups=[(None,1,SimpleNamespace(working_bytes_bound=154537536,
                                                               frequency_states=43,phase_states=512),None)])
        estimate=preparation_estimate(bank,grouped,(1,21889),torch.complex128)
        self.assertLess(estimate['device_total_bound_bytes'],2*1024**3)
        self.assertEqual(estimate['exact_transition_count'],2)
        self.assertGreater(85*estimate['capture_transient_bound_bytes'],24*1024**3)
        self.assertEqual(estimate['schedule_normalized_gram_bytes'],85*4*16)
        self.assertEqual(estimate['schedule_sample_index_bytes'],3*21673*8)

    def test_backend_pack_bytes_and_stable_contract(self):
        from triad_tensor_native import TensorBackend,pack_rows_cpu,FORMATS
        rows=torch.linspace(-1.,1.,5*37,dtype=torch.float32).reshape(5,37)
        for format in FORMATS:
            backend=TensorBackend(format,implementation='triton')
            packed=pack_rows_cpu(rows,format,block_k=backend.block_k)
            self.assertEqual(backend.estimated_packed_nbytes(5,37),packed.nbytes)
            identity=backend_contract_metadata(backend)
            backend._resolved['cuda:0']='triton'
            self.assertEqual(identity,backend_contract_metadata(backend))
            self.assertEqual(identity['contract']['scaling'],'power-of-two-per-row')
        self.assertNotEqual(backend_contract_metadata(TensorBackend('fp16',implementation='triton')),
                            backend_contract_metadata(TensorBackend('bf16',implementation='triton')))
        self.assertNotEqual(backend_contract_metadata(TensorBackend('fp16',implementation='triton')),
                            backend_contract_metadata(TensorBackend('fp16',implementation='torch')))

    def test_tiled_backend_accounting_and_memory_guard(self):
        class Backend:
            format='test'
            def estimated_packed_nbytes(self,m,k):return m*k*2+m*20
            def estimated_pack_scratch_nbytes(self,m,k):return m*k*8
            def estimated_matmul_scratch_nbytes(self,m,n,k):return m*n*32
        bank=SimpleNamespace(lags=[0,1],frequencies=[-.1,0.,.1],samples=19,sample_rate=11.,
            block_samples=7,block_count=3,_half_alignment=1,candidates=4,taps=1,backend=Backend(),
            precision='tensor',frequency_tile=2,candidate_tile=3,column_tile=3,
            _basis=np.ones((4,19,1),dtype=np.complex128))
        grouped=SimpleNamespace(groups=[(None,1,SimpleNamespace(working_bytes_bound=4096,
                                                               frequency_states=1,phase_states=16),None)])
        estimate=preparation_estimate(bank,grouped,(2,20),torch.complex128)
        # Four F/C tiles per block; each keeps its row scales/diagnostics.
        expected=sum((2*f*c)*(2*length)*2+(2*f*c)*20 for length in (7,7,5)
                     for f in (2,1) for c in (3,1))
        self.assertEqual(estimate['schedule_packed_a_bytes'],expected)
        workspace=object.__new__(WholeScanWorkspace)
        workspace.device=torch.device('cpu');workspace.memory_limit_bytes=100;workspace.memory_observations=[]
        with patch('torch.cuda.memory_allocated',return_value=20),patch('torch.cuda.memory_reserved',return_value=40):
            workspace._memory_guard('fits',additional_reserved=60)
            with self.assertRaisesRegex(MemoryError,'setup memory cap'):
                workspace._memory_guard('does not fit',additional_reserved=61)

    def test_block_helper_releases_dense_before_next_block(self):
        # Private CPU algebra/lifetime experiment, not a public CPU graph path.
        workspace=object.__new__(WholeScanWorkspace)
        workspace.bank=SimpleNamespace(frequencies=[0.],candidates=1,lags=[0],taps=1,
            _half_alignment=1,_dtype=torch.float64,backend=None,fused_reconstruction=False,
            _pack=lambda value:value)
        workspace.shape=(1,2);workspace.device=torch.device('cpu')
        workspace.observation=torch.tensor([[1+2j,3-1j]],dtype=torch.complex128)
        workspace.reciprocal_variance=torch.tensor(1.,dtype=torch.float64)
        receiver=SimpleNamespace(weights=torch.ones(1),_scores=torch.zeros((1,1),dtype=torch.float64),
            _graph_r=torch.empty((1,1,1),dtype=torch.complex128),
            _graph_g=torch.empty((1,1,1,1),dtype=torch.complex128),complex_dtype=torch.complex128)
        receiver._graph_body=lambda dt:receiver._scores.add_(receiver._graph_r.real[...,0])
        indices=torch.tensor([0]);workspace.grouped=SimpleNamespace(batch=1,groups=[(indices,1,receiver,None)])
        workspace._initial=[(receiver,torch.ones(1))]
        apack=torch.tensor([[1.,0.,0.,0.],[0.,0.,1.,0.]],dtype=torch.float64)
        tile=(indices,indices,torch.ones((1,1),dtype=torch.complex128),torch.tensor([[0,1]]),[(0,1,0,1,apack)])
        workspace.schedule=[(0.,torch.ones((1,1,1),dtype=torch.complex128),[tile])]*3
        workspace.output=torch.empty((1,1),dtype=torch.float64)
        previous=[]
        original=workspace._write_column_tile
        def observed(dense,current):
            if previous:self.assertIsNone(previous[-1]())
            previous.append(weakref.ref(dense))
            original(dense,current)
        workspace._write_column_tile=observed
        workspace._body()
        self.assertEqual(len(previous),3)
        self.assertTrue(all(ref() is None for ref in previous))
        self.assertEqual(float(workspace.output),3.)

    def test_actual_schedule_ragged_tiles_and_block_keys(self):
        from triad_exhaustive_correlation import ExhaustiveCorrelationBank
        from triad_phase_frequency import PhaseFrequencyLikelihood,PhaseFrequencyModel
        rng=np.random.default_rng(552)
        basis=rng.normal(size=(4,19,2))+1j*rng.normal(size=(4,19,2))
        bank=ExhaustiveCorrelationBank(basis,11.,[-.1,0.,.2],[0,2,5],7,
            frequency_tile=2,candidate_tile=3,column_tile=4,
            template_cache_bytes=0,geometry_cache_bytes=0)
        model=PhaseFrequencyModel(phase_states=16,frequency_grid_hz=(0.,),
                                  amplitude_nodes=(1.,),amplitude_weights=(1.,),relative_phase_states=2)
        receiver=PhaseFrequencyLikelihood(18,4,2,model,dtype=torch.float64)
        workspace=object.__new__(WholeScanWorkspace)
        workspace.bank=bank;workspace.shape=(2,24);workspace.device=torch.device('cpu')
        workspace.origin=2.25;workspace.variance=2.3;workspace.signature={}
        workspace.grouped=SimpleNamespace(groups=[(torch.arange(4),2,receiver,receiver.weights.clone())])
        workspace._transitions={id(receiver):{}};workspace.schedule=[]
        workspace.estimate={'template_preparation_transient_bound_bytes':0}
        workspace._memory_guard=lambda *args,**kwargs:None
        workspace._prepare_schedule()
        self.assertEqual(len(workspace.schedule),3)
        self.assertEqual(bank.counters['a_pack_calls'],12)
        self.assertEqual(bank.counters['geometry_prepare_calls'],2)
        self.assertEqual(len(workspace._transitions[id(receiver)]),2)
        for block,(dt,gram,tiles) in enumerate(workspace.schedule):
            self.assertEqual(len(tiles),2)
            self.assertIs(tiles[0][-1],tiles[1][-1])
            first=block*7;stop=min(first+7,19)
            for trials,lag_indices,residual,indices,apacks in tiles:
                expected=torch.tensor(bank.lags.copy())[lag_indices,None]+torch.arange(first,stop)[None,:]
                torch.testing.assert_close(indices,expected,rtol=0,atol=0)
                self.assertEqual(len(apacks),4)
                for f0,f1,c0,c1,packed in apacks:
                    expected_pack=bank._template_pack(block,first,stop,f0,f1,c0,c1)
                    torch.testing.assert_close(packed,expected_pack,rtol=0,atol=0)

    def test_actual_cuda_aba(self):
        if not CUDA:self.skipTest('explicit --cuda qualification not requested')
        if not torch.cuda.is_available():self.fail('--cuda requested but CUDA unavailable')
        from triad_exhaustive_correlation import ExhaustiveCorrelationBank
        from triad_phase_reduction import Fp64ReductionGraphLikelihood
        from triad_phase_frequency import PhaseFrequencyModel
        torch.backends.cuda.matmul.allow_tf32=False
        torch.set_float32_matmul_precision('highest')
        rng=np.random.default_rng(154)
        basis=(rng.normal(size=(2,169,1))+1j*rng.normal(size=(2,169,1)))*.03
        a=rng.normal(size=(1,171))+1j*rng.normal(size=(1,171))
        b=rng.normal(size=(1,171))+1j*rng.normal(size=(1,171))
        model=PhaseFrequencyModel(phase_states=16,frequency_grid_hz=(0.,),relative_phase_states=1,
                                  amplitude_nodes=(1.,),amplitude_weights=(1.,),nominal_dt_seconds=2/11.)
        def pair():
            bank=ExhaustiveCorrelationBank(basis,11.,[-.1,0.,.1],[0,2],2,device='cuda',precision='fp32',
                                           fused_reconstruction=True)
            receiver=Fp64ReductionGraphLikelihood(6,2,1,model,device='cuda',dtype=torch.float32,validate_inputs=False)
            indices=torch.arange(2,device='cuda')
            grouped=SimpleNamespace(batch=6,count=2,groups=[(indices,1,receiver,receiver.weights.clone())])
            return bank,grouped
        bank,grouped=pair()
        workspace=WholeScanWorkspace(bank,grouped,a.shape,noise_variance=2.3,memory_limit_bytes=2*1024**3)
        expected=[]
        refbank,refgroup=pair();receiver=refgroup.groups[0][2]
        for observation in (a,b,a):
            receiver.reset()
            for block in refbank.iter_blocks(observation,2.3):
                receiver.update(block.r.permute(0,1,3,2,4).reshape(6,2,1).to(torch.complex64),
                                block.gram.to(torch.complex64),block.dt)
            expected.append(receiver.scores().cpu().numpy())
        actual=[]
        for value,reference in zip((a,b,a),expected):
            score,timing=workspace.replay(value);actual.append(score)
            np.testing.assert_allclose(score,reference,rtol=0,atol=2e-5)
            self.assertGreater(timing['complete_replay_seconds'],0.)
        np.testing.assert_array_equal(actual[0],actual[2])
        self.assertGreater(float(abs(actual[0]-actual[1]).max()),1e-5)
        with self.assertRaises(ValueError):workspace.replay(a*np.nan)
        workspace.close()
        with self.assertRaises(RuntimeError):workspace.replay(a)
        # Owned capture must not contaminate a later normal-mode capture, even
        # if the workspace constructor's caller has inference_mode enabled.
        ambient_bank,ambient_group=pair()
        with torch.inference_mode():
            ambient=WholeScanWorkspace(ambient_bank,ambient_group,a.shape,noise_variance=2.3,
                                       memory_limit_bytes=2*1024**3)
        self.assertFalse(torch.is_inference(ambient.observation))
        normal_bank,normal_group=pair();normal=normal_group.groups[0][2]
        for value,reference in zip((a,b,a),expected):
            normal.reset()
            for block in normal_bank.iter_blocks(value,2.3):
                normal.update(block.r.permute(0,1,3,2,4).reshape(6,2,1).to(torch.complex64),
                              block.gram.to(torch.complex64),block.dt)
            score,_=ambient.replay(value)
            np.testing.assert_allclose(score,normal.scores().cpu().numpy(),rtol=0,atol=2e-5)
            np.testing.assert_allclose(score,reference,rtol=0,atol=2e-5)
        ambient.close()
        with torch.inference_mode():
            bad_bank,bad_group=pair()
        with self.assertRaisesRegex(ValueError,'created in inference_mode'):
            WholeScanWorkspace(bad_bank,bad_group,a.shape,noise_variance=2.3,memory_limit_bytes=2*1024**3)


if __name__=='__main__':
    if CUDA:sys.argv.remove('--cuda')
    unittest.main()

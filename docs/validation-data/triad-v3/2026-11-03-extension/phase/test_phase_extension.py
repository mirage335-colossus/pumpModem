import copy
import math
from types import SimpleNamespace
import unittest

import numpy as np
import torch

from diagnostic import phase_integral
from prepare_actual import build_jobs
from score_actual import validate_actual_workspace
from score_actual_batch import infer


class PhaseExtensionChecks(unittest.TestCase):
    def test_analytic_phase_mixture_against_quadrature(self):
        phase=np.arange(32768)*2*math.pi/32768
        for value in (.1+.2j,1.3-.4j,0j):
            expected=math.log(np.exp(2*np.real(value*np.exp(-1j*phase))-.7).mean())
            self.assertAlmostEqual(float(phase_integral(value,.7)),expected,places=12)

    def workspace(self):
        candidates=[dict(bit=bit,arrival_seconds=.05,frequency_hz=0,clock_ppm=0,
            frequency_drift_hz_per_second=0,epoch_offset_seconds=0,
            taps=[dict(delay_seconds=0,doppler_hz=0)]) for bit in (1,0)]
        return SimpleNamespace(gaussian_projection_supported=True,dimension=2,candidates=candidates,
            column_mapping=((0,),(1,)),manifest=dict(control='signal',capture_lead_seconds=.05,
            public_timing_bounds_capture_seconds=[.05,.05],initial_physical_arrival_bounds_capture_seconds=[.05,.05]))

    def test_public_capture_origin_and_bit_order(self):
        np.testing.assert_array_equal(validate_actual_workspace(self.workspace()),[1,0])

    def test_scope_rejections(self):
        for mutation in ('frontend','wrongkey','epoch','arrival','tap','labels'):
            w=self.workspace()
            if mutation=='frontend':w.gaussian_projection_supported=False
            elif mutation=='wrongkey':w.manifest['control']='wrong-key'
            elif mutation=='epoch':w.candidates[0]['epoch_offset_seconds']=1
            elif mutation=='arrival':w.candidates[0]['arrival_seconds']=0
            elif mutation=='tap':w.candidates[0]['taps'][0]['doppler_hz']=.1
            elif mutation=='labels':w.candidates[0]['bit']=0
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):validate_actual_workspace(w)

    def test_batch_matches_individual_and_precision(self):
        torch.set_num_threads(1)
        rng=np.random.default_rng(912)
        r=(rng.normal(size=(4,2,2,1))+1j*rng.normal(size=(4,2,2,1)))*.1
        g=np.full((4,2,2,1,1),.2);dt=np.array([0.,3.,3.,1.])
        args=dict(sigma=.02,states=32,device='cpu',precision='fp32',implementation='reference',memory_mib=64)
        batch,_=infer(r,g,dt,**args)
        singles=np.concatenate([infer(r[:,j:j+1],g[:,j:j+1],dt,**args)[0] for j in range(2)])
        np.testing.assert_allclose(batch,singles,rtol=0,atol=2e-6)
        args['precision']='fp64';reference,_=infer(r,g,dt,**args)
        np.testing.assert_allclose(batch,reference,rtol=0,atol=2e-6)

    def test_frozen_batch_balance_and_unique_paths(self):
        jobs=build_jobs();exports=[j for j in jobs if j['stage']=='export']
        self.assertEqual(len(exports),20)
        signals=[j for j in exports if j['control']=='signal']
        self.assertEqual(sum(j['signal_bit']==0 for j in signals),8)
        self.assertEqual(sum(j['signal_bit']==1 for j in signals),8)
        self.assertEqual(sum(j['control']=='null' for j in exports),4)
        self.assertEqual(len({j['id'] for j in jobs}),len(jobs))
        self.assertEqual(len({j['argv'][j['argv'].index('--noise-seed')+1] for j in exports}),20)
        self.assertTrue(all(j['timeout_seconds']<=900 for j in jobs))
        gpu_batch=next(j for j in jobs if j['id']=='benchmark-all20-cuda-fp32')
        self.assertEqual(len(gpu_batch['depends_on']),20)

    def test_resident_and_state_share_one_memory_limit(self):
        r=np.zeros((2,2,2,1),dtype=np.complex128)
        g=np.ones((2,2,2,1,1));dt=np.array([0.,1.])
        with self.assertRaisesRegex(ValueError,'state exceeds'):
            infer(r,g,dt,sigma=.02,states=4096,device='cpu',precision='fp32',
                implementation='reference',memory_mib=64,resident_bound_bytes=63*1024**2)

    def test_cn0_variant_preserves_physics_and_offsets_all_seed_domains(self):
        default=build_jobs();explicit=build_jobs(-60.,0)
        self.assertEqual(default,explicit)
        variant=build_jobs(-57.,10000000)
        for old,new in zip(default,variant):
            self.assertEqual(old['id'],new['id'])
            if old['stage'] not in ('configure','export'):
                self.assertEqual(old,new);continue
            restored=new['argv'].copy()
            self.assertEqual(restored[restored.index('--cn0')+1],'-57')
            restored[restored.index('--cn0')+1]='-60'
            for flag in ('--seed','--key-seed','--channel-seed','--oscillator-seed','--noise-seed','--frontend-seed','--path-seed'):
                index=restored.index(flag)+1
                self.assertEqual(int(restored[index]),int(old['argv'][index])+10000000)
                restored[index]=old['argv'][index]
            self.assertEqual(restored,old['argv'])

    def test_cn0_and_seed_bounds_reject_inexact_or_nonfinite_inputs(self):
        for cn0 in (float('nan'),float('inf'),-81,21,True):
            with self.subTest(cn0=cn0),self.assertRaises(ValueError):build_jobs(cn0,0)
        for offset in (-1,1.0,True,1000000001):
            with self.subTest(offset=offset),self.assertRaises(ValueError):build_jobs(-60,offset)


if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Focused actual-observation parity checks for timing FFT reuse."""
import copy
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

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import triad_fft_acquire as fft_receiver
from triad_fft_acquire import (FFTWorkspace, SampledWorkspace, lag_grid, main,
    phase, score_workspace, parse_args)
from triad_group_acquire import AcquisitionConfig, prepare_group_tile
from triad_gaussian_window import PreparedWindowedGaussian


def fixture(directory,*,seed=771,duplicate=False,erased=True,nonlinear=False):
    rng=np.random.default_rng(seed)
    n=128; fs=128.
    times=np.arange(n,dtype=np.longdouble)/fs
    columns=np.zeros((4,n),np.complex128)
    for j in range(4):
        columns[j,9:83]=(rng.normal(size=74)+1j*rng.normal(size=74))*.16
    if duplicate:
        columns[1]=columns[0];columns[3]=columns[2]
    base=dict(arrival_seconds=.153,frequency_hz=.11,clock_ppm=.00003,
        epoch_offset_seconds=2**53+1,frequency_drift_hz_per_second=.002)
    taps=[dict(delay_seconds=0.,doppler_hz=0.),dict(delay_seconds=.0047,doppler_hz=0. if duplicate else 3.25)]
    a=columns.copy()
    for j in range(4):
        a[j]*=phase(base['frequency_hz']+taps[j%2]['doppler_hz'],times,base['frequency_drift_hz_per_second'])
    candidates=[];basis=np.zeros((4,n,2),np.complex128)
    for model in (1,2):
        for bit in (0,1):
            ci=len(candidates)
            candidates.append(dict(base,bit=bit,tap_count=model,taps=copy.deepcopy(taps[:model])))
            basis[ci,:,0]=a[2*bit]
            if model==2:basis[ci,:,1]=a[2*bit+1]
    valid=np.ones(n,np.uint8)
    if erased:valid[[2,12,13,31,32,33,90,111]]=0
    truthlag=7; frequency=.07; drift=-.001
    A=np.zeros((n,2),np.complex128)
    for tap in range(2):
        A[truthlag:,tap]=columns[2+tap,:n-truthlag]*phase(base['frequency_hz']+taps[tap]['doppler_hz']+frequency,
            times[truthlag:],base['frequency_drift_hz_per_second']+drift)
    y=A@np.asarray([2+.8j,-1+.3j])+(rng.normal(size=n)+1j*rng.normal(size=n))*.2
    arrays={'basis.c128':basis.astype('<c16'),'observation.c128':y.astype('<c16'),'validity.u8':valid}
    manifest=dict(format='triad-sampled-waveforms-v1',layout='candidate,sample,column',byte_order='little',
        complex_type='complex128',integer_type='uint8',sample_count=n,sample_rate=fs,
        basis_file='basis.c128',basis_shape=list(basis.shape),observation_file='observation.c128',observation_shape=[n],
        validity_file='validity.u8',validity_shape=[n],candidate_count=len(candidates),candidates=candidates,
        gaussian_projection_supported=not nonlinear,sampled_frontend=dict(active=nonlinear),
        frontend_report=dict(gaussian_projection_supported=not nonlinear),
        control='signal',signal_bit_simulation_truth=1,
        # These values deliberately disagree with useful receiver quantities.
        noise_variance_pre_frontend=999999,channel_signal_energy_samples=-99,
        selected_truth_arrival_seconds_simulation_only=900,
        file_sizes={name:value.nbytes for name,value in arrays.items()})
    directory.mkdir(parents=True,exist_ok=True)
    for name,value in arrays.items():value.tofile(directory/name)
    (directory/'manifest.json').write_text(json.dumps(manifest))
    return manifest,columns,y,valid


def explicit_scores(work,config,drifts=(0.,)):
    strict,_=config.budgets()
    count=len(work.lags)*len(config.frequency_offsets_hz)*len(drifts)
    delta=math.nextafter(strict.delta_per_segment/count,0.)
    energy=np.asarray([work.energy]);valid=np.asarray([work.valid_count],np.uint64)
    result=[]
    ranks=[]
    for drift in drifts:
        for frequency in config.frequency_offsets_hz:
            for lag in work.lags:
                G,r=work.direct_statistics(lag,frequency,drift)
                correlations,grams,upper,_,_,hist=prepare_group_tile(G[None,None],r[None,None],energy,valid,
                    work.mapping[None],reference=config.reference_variance,delta=delta,projection_tile_blocks=1)
                plan=PreparedWindowedGaussian(grams.reshape(1,2,2,2),np.asarray([.5]),doppler_width_hz=0.,
                    window_blocks=1,stationary_power=config.stationary_power,memory_limit_bytes=32*1024*1024)
                chosen=upper if config.known_noise_variance is None else np.full_like(upper,config.known_noise_variance/config.reference_variance)
                score=plan.score(correlations.reshape(1,2,2),variance_scale=np.repeat(chosen,2)[None],
                    device='cpu',precision=config.precision).numpy().reshape(2)
                result.append(score);ranks.append(hist)
    return np.asarray(result),np.sum(ranks,axis=(0,1))


class TimingFFTTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(1)

    def make(self,root,**kwargs):
        fixture(root,**kwargs)
        sampled=SampledWorkspace(root)
        return FFTWorkspace(sampled,np.arange(16),memory_limit_bytes=128*1024*1024)

    def test_fft_matches_actual_raw_masked_dots_with_delay_doppler_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            work=self.make(Path(tmp))
            for offset,drift in ((0.,0.),(.07,-.001),(-.17,.004)):
                r=work.correlations(offset,drift)
                for index,lag in enumerate(work.lags):
                    G,reference=work.direct_statistics(lag,offset,drift)
                    np.testing.assert_allclose(work.gram[index],G,rtol=2e-13,atol=3e-13)
                    np.testing.assert_allclose(r[index],reference,rtol=2e-13,atol=3e-13)
            # The received-row mask stays fixed while templates shift.
            a,b=work.direct_statistics(0),work.direct_statistics(15)
            self.assertGreater(np.linalg.norm(a[0]-b[0]),.01)

    def test_complete_bank_scores_noise_allocation_and_tiling_match_direct(self):
        with tempfile.TemporaryDirectory() as tmp:
            work=self.make(Path(tmp))
            config=AcquisitionConfig(0.,window_blocks=1,frequency_offsets_hz=(-.17,0.,.07),
                group_tile=3,memory_limit_bytes=128*1024*1024)
            drift=(-.001,.004)
            result=score_workspace(work,config,drift_offsets=drift)
            reference,ranks=explicit_scores(work,config,drift)
            np.testing.assert_allclose(result['group_bit_log_likelihoods'],reference,rtol=2e-11,atol=2e-11)
            np.testing.assert_allclose(result['bit_log_likelihoods'],logsumexp(reference,axis=0)-math.log(len(reference)),rtol=1e-12,atol=1e-12)
            np.testing.assert_array_equal(result['calibration']['rank_histogram'],ranks)
            changed=AcquisitionConfig(0.,window_blocks=1,frequency_offsets_hz=config.frequency_offsets_hz,
                group_tile=11,memory_limit_bytes=128*1024*1024)
            tiled=score_workspace(work,changed,drift_offsets=drift,retain_group_scores=False)
            np.testing.assert_allclose(tiled['bit_log_likelihoods'],result['bit_log_likelihoods'],rtol=1e-13,atol=1e-13)
            self.assertEqual(tiled['strict'],result['strict'])
            self.assertEqual(tiled['relaxed'],result['relaxed'])
            self.assertIsNone(tiled['group_bit_log_likelihoods'])
            self.assertEqual(result['calibration']['effective_groups'],96)
            self.assertEqual(result['calibration']['delta_per_group'],math.nextafter(config.budgets()[0].delta_per_segment/96,0.))

    def test_exact_rank_deficiency_is_not_promoted_to_noise_modes(self):
        with tempfile.TemporaryDirectory() as tmp:
            work=self.make(Path(tmp),duplicate=True)
            config=AcquisitionConfig(0.,window_blocks=1,group_tile=7,memory_limit_bytes=128*1024*1024)
            result=score_workspace(work,config)
            reference,ranks=explicit_scores(work,config)
            np.testing.assert_allclose(result['group_bit_log_likelihoods'],reference,rtol=2e-11,atol=2e-11)
            self.assertEqual(result['calibration']['rank_histogram'],[0,0,16,0,0])

    def test_zero_and_tiny_observed_support_match_exact_public_dots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root)
            valid=np.zeros(128,np.uint8);valid[110:]=1
            valid.tofile(root/'validity.u8')
            work=FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
            config=AcquisitionConfig(0.,window_blocks=1,group_tile=4,memory_limit_bytes=128*1024*1024)
            result=score_workspace(work,config)
            np.testing.assert_array_equal(work.gram,0.)
            np.testing.assert_array_equal(work.correlations(),0.)
            np.testing.assert_array_equal(result['bit_log_likelihoods'],[0.,0.])
            self.assertFalse(result['strict']['accepted'])

            # One tiny publicly observed edge must not become an FFT noise mode.
            m=json.loads((root/'manifest.json').read_text())
            basis=np.fromfile(root/'basis.c128',dtype='<c16').reshape(4,128,2)
            t=np.arange(128,dtype=np.longdouble)/128
            for bit,ci in ((0,2),(1,3)):
                for tap in (0,1):
                    row=100+tap
                    frequency=m['candidates'][ci]['frequency_hz']+m['candidates'][ci]['taps'][tap]['doppler_hz']
                    basis[ci,row,tap]=(bit+1+(.3+tap)*1j)*1e-16*phase(frequency,t[row:row+1],.002)[0]
            basis.astype('<c16').tofile(root/'basis.c128')
            work=FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
            result=score_workspace(work,config)
            self.assertGreater(result['numerical_refinement']['direct_refined_groups'],0)
            with patch.object(fft_receiver,'MAX_REFINEMENT_ROWS',1):
                with self.assertRaisesRegex(ValueError,'examined-row'):
                    FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
            reference,ranks=explicit_scores(work,config)
            np.testing.assert_allclose(result['group_bit_log_likelihoods'],reference,atol=1e-12,rtol=1e-12)
            for index,lag in enumerate(work.lags):
                G,r=work.direct_statistics(lag)
                np.testing.assert_allclose(work.gram[index],G,rtol=1e-12,atol=1e-40)
                np.testing.assert_allclose(work.correlations()[index],r,rtol=1e-12,atol=1e-30)

    def test_near_edge_amplitude_sweep_preserves_rank_and_likelihood(self):
        for amplitude in (1e-1,1e-2,1e-3,1e-4,1e-5,3e-6,1e-6,3e-7,1e-7,1e-8,1e-16):
            with self.subTest(amplitude=amplitude),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);fixture(root)
                m=json.loads((root/'manifest.json').read_text())
                basis=np.fromfile(root/'basis.c128',dtype='<c16').reshape(4,128,2)
                t=np.arange(128,dtype=np.longdouble)/128
                valid=np.zeros(128,np.uint8);valid[110:]=1;valid.tofile(root/'validity.u8')
                for bit,ci in ((0,2),(1,3)):
                    for tap in (0,1):
                        row=100+tap
                        frequency=m['candidates'][ci]['frequency_hz']+m['candidates'][ci]['taps'][tap]['doppler_hz']
                        basis[ci,row,tap]=(bit+1+(.3+tap)*1j)*amplitude*phase(frequency,t[row:row+1],.002)[0]
                basis.astype('<c16').tofile(root/'basis.c128')
                work=FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
                config=AcquisitionConfig(0.,window_blocks=1,group_tile=4,memory_limit_bytes=128*1024*1024)
                result=score_workspace(work,config)
                reference,ranks=explicit_scores(work,config)
                np.testing.assert_allclose(result['group_bit_log_likelihoods'],reference,atol=2e-11,rtol=2e-11)
                np.testing.assert_array_equal(result['calibration']['rank_histogram'],ranks)
                self.assertFalse(result['strict']['accepted'])
                if amplitude <= 1e-2:self.assertGreater(result['numerical_refinement']['direct_refined_groups'],0)

    def test_partial_multitap_support_and_support_work_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root)
            basis=np.fromfile(root/'basis.c128',dtype='<c16').reshape(4,128,2)
            for ci in (2,3):
                value=basis[ci,10,1]
                basis[ci,:,1]=0.;basis[ci,10,1]=value
            basis.astype('<c16').tofile(root/'basis.c128')
            valid=np.zeros(128,np.uint8);valid[70:90]=1;valid.tofile(root/'validity.u8')
            work=FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
            np.testing.assert_array_equal(work.gram[:,[1,3]],0.)
            np.testing.assert_array_equal(work.correlations()[:,[1,3]],0.)
            for index,lag in enumerate(work.lags):
                G,r=work.direct_statistics(lag)
                np.testing.assert_allclose(work.gram[index],G,rtol=2e-12,atol=2e-13)
                np.testing.assert_allclose(work.correlations()[index],r,rtol=2e-12,atol=2e-13)
            with patch.object(fft_receiver,'MAX_SUPPORT_OPERATIONS',0):
                with self.assertRaisesRegex(ValueError,'run/lag'):
                    FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)

    def test_fp32_correlations_are_explicit_partial_precision(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root)
            exact=FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024)
            bulk=FFTWorkspace(SampledWorkspace(root),np.arange(16),precision='fp32',memory_limit_bytes=128*1024*1024)
            np.testing.assert_array_equal(bulk.gram,exact.gram)
            np.testing.assert_allclose(bulk.correlations(.07),exact.correlations(.07),rtol=2e-5,atol=2e-6)
            config=AcquisitionConfig(0.,window_blocks=1,group_tile=5,precision='fp32',memory_limit_bytes=128*1024*1024)
            result=score_workspace(bulk,config)
            self.assertIn('substantial bulk work',result['arithmetic']['gram_fft'])
            self.assertEqual(result['arithmetic']['observation_fft_and_products'],'complex64')
            self.assertFalse(result['arithmetic']['tf32_allowed'])

    def test_truth_fields_are_not_receiver_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);work=self.make(root)
            config=AcquisitionConfig(0.,window_blocks=1,memory_limit_bytes=128*1024*1024)
            before=score_workspace(work,config)
            path=root/'manifest.json';m=json.loads(path.read_text())
            m.update(signal_bit_simulation_truth=0,control='null',noise_variance_pre_frontend=.0000001,
                selected_truth_arrival_seconds_simulation_only=-999,channel_signal_energy_samples=999)
            path.write_text(json.dumps(m))
            after=score_workspace(FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=128*1024*1024),config)
            self.assertEqual(before['bit_log_likelihoods'],after['bit_log_likelihoods'])
            self.assertEqual(before['strict'],after['strict'])

    def test_support_nonlinear_geometry_and_memory_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root,nonlinear=True)
            with self.assertRaisesRegex(ValueError,'Gaussian'):SampledWorkspace(root)
            fixture(root)
            nested=json.loads((root/'manifest.json').read_text());nested['frontend_report']['gaussian_projection_supported']=False
            (root/'manifest.json').write_text(json.dumps(nested))
            with self.assertRaisesRegex(ValueError,'Gaussian'):SampledWorkspace(root)
            fixture(root)
            with self.assertRaises(MemoryError):FFTWorkspace(SampledWorkspace(root),np.arange(16),memory_limit_bytes=1024)
            with self.assertRaisesRegex(ValueError,'support'):FFTWorkspace(SampledWorkspace(root),np.asarray([0,50]),memory_limit_bytes=128*1024*1024)
            with self.assertRaisesRegex(ValueError,'unique'):FFTWorkspace(SampledWorkspace(root),np.asarray([0,0]),memory_limit_bytes=128*1024*1024)
            m=json.loads((root/'manifest.json').read_text());m['candidates'][0]['clock_ppm']=1
            (root/'manifest.json').write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'one fixed'):SampledWorkspace(root)
        np.testing.assert_array_equal(lag_grid(10,4),[0,4,8,10])
        with self.assertRaises(ValueError):lag_grid(1_000_001,1)
        with self.assertRaises(ValueError):lag_grid(1_999_999,2)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root)
            with self.assertRaises(ValueError):FFTWorkspace(SampledWorkspace(root),np.asarray([0,2**64-1],dtype=np.uint64),memory_limit_bytes=128*1024*1024)

    def test_scientific_drift_arguments_and_overflowing_lag_fail_closed(self):
        args=parse_args(['input','--lag-max-seconds','1','--drift-offsets-hz-per-second','-1e-8','0','1e-8','--output','out'])
        self.assertEqual(args.drift_offsets_hz_per_second,[-1e-8,0.,1e-8])
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root/'input')
            with self.assertRaisesRegex(ValueError,'extent'):
                main([str(root/'input'),'--lag-max-seconds','1e308','--output',str(root/'out.json')])

    def test_cli_scientific_frequency_and_fresh_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture(root/'input')
            output=root/'score.json'
            argv=[str(root/'input'),'--lag-max-seconds',str(15/128),'--frequency-offset-grid','-7e-2','7e-2','.07',
                '--group-tile','4','--memory-mib','128','--evaluate-simulation','--output',str(output)]
            self.assertEqual(main(argv),0)
            result=json.loads(output.read_text())
            self.assertEqual(result['geometry']['group_count'],48)
            self.assertEqual(set(result['input_sha256']),{'manifest.json','basis.c128','observation.c128','validity.u8'})
            self.assertIn('simulation_evaluation_only',result)
            with self.assertRaisesRegex(ValueError,'exists'):main(argv)


if __name__=='__main__':unittest.main()

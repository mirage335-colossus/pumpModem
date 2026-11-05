#!/usr/bin/env python3
"""Bounded actual raw FFT/mask/precision contracts; no sampled campaign."""
from pathlib import Path
from dataclasses import replace
import sys,unittest
from unittest import mock
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_helper_raw_receiver import RawHelperDataReceiver
from test_triad_helper_receiver import source_templates,capture


def receiver(templates,**kwargs):
    options=dict(source_offset_seconds=.2,capture_lead_seconds=.1,capture_rows=18000,
        arrival_bounds_seconds=(0.,.02),delay_seconds=.0005,doppler_hz=6.,reference_variance=.005,
        frequency_offsets=(-.25,0.,.25),precision='fp64',device='cpu',memory_limit_bytes=128*1024**2,
        frequency_batch_size=2,max_cached_hypotheses=2)
    options.update(kwargs)
    return RawHelperDataReceiver(templates,tuple(range(len(templates))),**options)


class RawReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1);torch.backends.cuda.matmul.allow_tf32=False;torch.set_float32_matmul_precision('highest')

    def test_full_epoch_timing_frequency_and_fp32_fp64_same_banks(self):
        templates=source_templates(2,rows=3600)
        high=receiver(templates);low=receiver(templates,precision='fp32')
        for bit in (0,1):
            raw=capture(templates,high,bit=bit,epoch=1,lag=13,frequency=.25)
            a,b=high.score(raw),low.score(raw)
            self.assertEqual(a['bit'],bit);self.assertEqual(b['bit'],bit)
            np.testing.assert_allclose(a['bit_log_likelihoods'],b['bit_log_likelihoods'],rtol=2e-5,atol=.002)
            self.assertEqual(a['geometry']['effective_group_count'],2*19*3)
            self.assertAlmostEqual(a['best_group_public_parameters']['arrival_seconds'],13/900)
            self.assertEqual(a['geometry']['sample_rate_hz'],18000)
            self.assertEqual(a['preprocessing']['noise_variance_gain'],1.)
            self.assertFalse(a['calibration']['analytic_admission_validated'])
            self.assertNotIn('accepted',a)

    def test_known_mask_has_no_dilation_and_exact_masked_grams(self):
        templates=source_templates(rows=3600);valid=np.ones(18000,np.uint8);valid[7000:7180]=0
        r=receiver(templates,capture_validity=valid);raw=capture(templates,r,bit=1,lag=13,frequency=.25)
        raw[valid==0]=np.nan;prepared=r.prepare_observation(raw,valid=valid)
        expected=raw[r.crop_start_raw:].copy();expected[valid[r.crop_start_raw:]==0]=0
        np.testing.assert_array_equal(prepared.samples,expected)
        self.assertEqual(r.preprocessing['raw_missing_rows'],180)
        self.assertEqual(int((r.output_validity==0).sum()),180)
        result=r.score_prepared(prepared);self.assertEqual(result['bit'],1)
        w=r._cache[0].workspace;G,z=w.direct_statistics(13*20,frequency_offset=.25)
        gi=np.flatnonzero(r.lags==13*20)[0]
        np.testing.assert_allclose(G,w.gram[gi],rtol=2e-12,atol=2e-8)
        self.assertEqual(w.valid_count,int(valid[r.crop_start_raw:].sum()))
        self.assertEqual(r.preprocessing['filter'],'none')
        with self.assertRaises(ValueError):r.prepare_observation(raw,valid=np.ones(18000,np.uint8))
        with self.assertRaises(ValueError):r.score_prepared(replace(prepared,validity_id='other'))

    def test_subset_cache_identity_and_aligned_frequencies(self):
        templates=source_templates(rows=1800);r=receiver(templates,max_cached_hypotheses=1)
        source=source_templates(rows=1800);raw=capture(source,r,bit=0,frequency=0.)
        variant=r.with_frequency_offsets((0.,));self.assertIs(variant._cache,r._cache)
        self.assertNotEqual(variant.receiver_id,r.receiver_id)
        r.score(raw);self.assertEqual(len(r._cache),1)
        self.assertEqual(variant.score(raw)['bit'],0)
        bank=r.aligned_frequency_offsets(.58)
        self.assertEqual(bank[0],-.58);self.assertEqual(bank[-1],.58)
        self.assertLessEqual(np.max(np.diff(bank)),1/(4*(source.shape[2]/18000)))
        altered=receiver(templates,reference_variance=.006)
        self.assertNotEqual(altered.receiver_id,r.receiver_id)

    def test_likelihood_dependencies_are_in_receiver_identity(self):
        templates=source_templates(rows=1800);baseline=receiver(templates)
        original=Path.read_bytes
        for name in ('triad_group_acquire.py','triad_gaussian_state.py','triad_state_workspace.py'):
            def changed(path):
                data=original(path)
                return data+b'\n# identity-change probe\n' if path.name==name else data
            with mock.patch.object(Path,'read_bytes',changed):
                altered=receiver(templates)
            self.assertNotEqual(altered.receiver_id,baseline.receiver_id)

    def test_public_bounds_memory_and_missing_support_rejected(self):
        templates=source_templates(rows=1800)
        for kwargs in [dict(frequency_offsets=(.60001,)),dict(clock_ppm_values=(-1.,0.,1.)),
                       dict(doppler_scale_values=(.999,1.)),dict(sample_rate=900),dict(output_sample_rate=900),
                       dict(channel_line_half_width_hz=5.),dict(capture_rows=6500),dict(reference_variance=0.),
                       dict(capture_validity=np.full(18000,2,np.uint8))]:
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):receiver(templates,**kwargs)
        with self.assertRaises(MemoryError):receiver(templates,memory_limit_bytes=65536)
        with self.assertRaises(ValueError):receiver(templates).with_hypotheses(clock_ppm_values=(1.,))

    @unittest.skipUnless(torch.cuda.is_available(),'CUDA unavailable; actual device path remains untested locally')
    def test_actual_cuda_fp32_and_fp64_match_cpu_reference(self):
        templates=source_templates(rows=1800);mask=np.ones(18000,np.uint8);mask[7000:7180]=0
        cpu=receiver(templates,capture_validity=mask);raw=capture(templates,cpu,bit=1,lag=13,frequency=.25)
        reference=cpu.score(raw)
        for precision in ('fp32','fp64'):
            gpu=receiver(templates,device='cuda:0',precision=precision,capture_validity=mask,memory_limit_bytes=256*1024**2)
            actual=gpu.score(raw)
            self.assertEqual(actual['bit'],reference['bit'])
            np.testing.assert_allclose(actual['bit_log_likelihoods'],reference['bit_log_likelihoods'],rtol=2e-5,atol=.003)
            self.assertIn('cuda',actual['arithmetic']['observation_fft_device'])
            self.assertFalse(actual['arithmetic']['tf32_allowed'])


if __name__=='__main__':unittest.main()

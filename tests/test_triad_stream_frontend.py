import math
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_stream_frontend import StreamingAudioDDC


def reference(pcm, frontend, valid=None, *, tail=True):
    """Independent whole-stream direct FIR with exact per-sample phase origin."""
    valid = np.ones(len(pcm), bool) if valid is None else np.asarray(valid, bool)
    num, den = frontend.carrier_hz.as_integer_ratio()
    den *= frontend.input_rate
    cycles = np.array([((frontend.start_index+i)*num % den)/den for i in range(len(pcm))])
    ph = np.exp(-2j*np.pi*cycles-1j*frontend.phase_at_zero_radians)
    # Preserve PCM input quantization, then perform scaling in the selected
    # arithmetic. Multiplying a float32 array first would silently round an
    # FP64 reference's mixer in FP32.
    mixed = (np.where(valid, pcm, 0).astype(frontend.real_dtype)*frontend.scale*ph).astype(frontend.dtype)
    filtered = signal.convolve(mixed, frontend.taps.astype(frontend.real_dtype), method='direct')
    length = len(filtered) if tail else len(pcm)
    indices = np.arange(length)+frontend.start_index
    selected = indices % frontend.decimation == 0
    complete = np.convolve(np.r_[np.zeros(frontend.support_samples, int), valid.astype(int),
        np.zeros(frontend.support_samples if tail else 0, int)], np.ones(len(frontend.taps), int), mode='valid') == len(frontend.taps)
    return filtered[:length][selected], complete[selected], indices[selected]


class StreamingFrontendTests(unittest.TestCase):
    def test_arbitrary_chunks_equal_whole_direct_filter_including_tail(self):
        rng = np.random.default_rng(1301)
        pcm = rng.normal(size=8193).astype(np.float32)
        for precision in ('fp32', 'fp64'):
            for chunks in ((8193,), (1, 7, 0, 33, 256, 4095, 3801), (113,)*73):
                with self.subTest(precision=precision,chunks=chunks[:3]):
                    f = StreamingAudioDDC(precision=precision, maximum_chunk_samples=1024,
                                          start_sample_index=3, phase_at_zero_radians=.23)
                    expected, mask, indices = reference(pcm, f)
                    parts=[]; masks=[]; actual_indices=[]; start=0
                    for n in chunks:
                        end=min(len(pcm),start+n)
                        y,v,r=f.process(pcm[start:end]);start=end
                        parts.append(y);masks.append(v)
                        if len(y):actual_indices.extend(r['first_output_input_sample']+np.arange(len(y))*8)
                    if start<len(pcm):
                        y,v,r=f.process(pcm[start:]);parts.append(y);masks.append(v)
                        if len(y):actual_indices.extend(r['first_output_input_sample']+np.arange(len(y))*8)
                    y,v,r=f.flush();parts.append(y);masks.append(v)
                    if len(y):actual_indices.extend(r['first_output_input_sample']+np.arange(len(y))*8)
                    np.testing.assert_allclose(np.concatenate(parts),expected,rtol=2e-5 if precision=='fp32' else 2e-11,
                                               atol=2e-6 if precision=='fp32' else 2e-12)
                    np.testing.assert_array_equal(np.concatenate(masks),mask)
                    np.testing.assert_array_equal(actual_indices,indices)
                    self.assertTrue(r['finite_zero_extension_tail'])
                    self.assertFalse(v.any())
                    with self.assertRaisesRegex(RuntimeError,'closed'):f.process([])

    def test_phase_large_integer_index_and_chunk_boundaries(self):
        pcm=np.cos(np.arange(5003)*.31).astype(np.float32)
        for start in (2**60+17, -(2**60)+17):
            f=StreamingAudioDDC(start_sample_index=start,carrier_hz=1725.123456789,
                                phase_at_zero_radians=.53,precision='fp64',maximum_chunk_samples=257)
            expected,valid,indices=reference(pcm,f)
            a,va,ra=f.process(pcm[:3]);b,vb,rb=f.process(pcm[3:]);c,vc,rc=f.flush()
            np.testing.assert_allclose(np.r_[a,b,c],expected,rtol=1e-10,atol=3e-12)
            np.testing.assert_array_equal(np.r_[va,vb,vc],valid)
            first=next(r['first_output_input_sample']for r in (ra,rb,rc)if r['output_samples'])
            self.assertEqual(first,int(indices[0]))

    def test_erasure_zero_fill_and_full_support_dilation(self):
        pcm=np.ones(1601,np.float32);valid=np.ones(len(pcm),bool)
        valid[301:307]=False;valid[1100]=False;pcm[301:307]=np.nan;pcm[1100]=np.inf
        f=StreamingAudioDDC(maximum_chunk_samples=99)
        expected,mask,_=reference(pcm,f,valid)
        a,va,_=f.process(pcm[:304],valid[:304]);b,vb,_=f.process(pcm[304:],valid[304:]);c,vc,_=f.flush()
        np.testing.assert_allclose(np.r_[a,b,c],expected,atol=2e-6)
        np.testing.assert_array_equal(np.r_[va,vb,vc],mask)
        self.assertTrue(np.isfinite(np.r_[a,b,c]).all())
        self.assertTrue(mask.any())

    def test_passband_helper_and_data_gain_phase(self):
        fs=48000;n=np.arange(24000);freqs=np.array([-1175.,-75.,0.,75.,675.])
        amps=np.array([.12,.03,.02,.04,.11]);phases=np.array([.2,-.5,.7,1.1,-.8])
        x=np.sum(amps[:,None]*np.exp(2j*np.pi*(1725+freqs[:,None])*n/fs+1j*phases[:,None]),axis=0)
        pcm=(math.sqrt(2)*.2*x.real).astype(np.float32)
        f=StreamingAudioDDC(undo_fixed_gain=.2)
        y,v,r=f.process(pcm);t=np.arange(len(y))*8/fs
        _,h=signal.freqz(f.taps,worN=freqs,fs=fs)
        expected=np.sum(amps[:,None]*h[:,None]*np.exp(2j*np.pi*freqs[:,None]*t+1j*phases[:,None]),axis=0)
        self.assertLess(np.linalg.norm(y[v]-expected[v])/np.linalg.norm(expected[v]),2e-4)
        self.assertTrue(f.metadata()['continuous_cross_window_filter_state'])
        self.assertEqual(r['group_delay_input_samples'],f.group_delay_samples)

    def test_empty_and_validation_preserve_state(self):
        f=StreamingAudioDDC(start_sample_index=11)
        y,v,r=f.process([])
        self.assertEqual(len(y),0);self.assertIsNone(r['first_output_input_sample'])
        self.assertEqual(f.next_input_index,11)
        for pcm,valid in (([np.nan],None),([1+0j],None),([1],[2]),([[1]],None)):
            with self.assertRaises(ValueError):f.process(pcm,valid)
            self.assertEqual(f.next_input_index,11)
        y,v,r=f.flush();self.assertEqual(len(y),0)
        with self.assertRaises(RuntimeError):f.flush()
        for kwargs in (dict(output_sample_rate=7000),dict(pass_edge_hz=1800),
            dict(stop_edge_hz=2200),dict(precision='bf16'),dict(start_sample_index=.5),
            dict(maximum_chunk_samples=0)):
            with self.assertRaises(ValueError):StreamingAudioDDC(**kwargs)

    def test_noise_variance_calibration_and_finite_history(self):
        f=StreamingAudioDDC(undo_fixed_gain=.25,maximum_chunk_samples=511)
        m=f.metadata()
        self.assertAlmostEqual(m['white_real_pcm_variance_to_complex_output_variance'],
                               2/.25**2*float(np.dot(f.taps.astype(float),f.taps.astype(float))))
        f.process(np.zeros(2000,np.float32))
        self.assertEqual(len(f._history),len(f.taps)-1)
        self.assertEqual(len(f._history_valid),len(f.taps)-1)
        self.assertLessEqual(len(f._phase_values),4096)
        self.assertEqual(f.input_samples,2000)


if __name__=='__main__':unittest.main()

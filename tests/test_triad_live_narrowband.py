#!/usr/bin/env python3
"""Small hardware-free support/arithmetic tests for the optional cable FIR."""
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import signal

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_live_narrowband import NarrowbandFilter


class NarrowbandTests(unittest.TestCase):
    def test_causal_direct_and_fft_references_both_precisions(self):
        rng=np.random.default_rng(1203)
        values=(rng.normal(size=4096)+1j*rng.normal(size=4096)).astype(np.complex64)
        for precision, tolerance in [('fp32',3e-7),('fp64',2e-15)]:
            f=NarrowbandFilter(5040,2250,precision=precision)
            y=f.filter_observation(values)
            direct=np.convolve(values.astype(np.complex128),f.taps.astype(float))[:len(values)]
            fft=signal.fftconvolve(values.astype(np.complex128),f.taps.astype(float))[:len(values)]
            np.testing.assert_allclose(y,direct,atol=tolerance,rtol=2e-7 if precision=='fp32' else 2e-15)
            np.testing.assert_allclose(y,fft,atol=tolerance,rtol=2e-7 if precision=='fp32' else 2e-15)
            self.assertEqual(y.dtype,np.dtype(np.complex64 if precision=='fp32' else np.complex128))

    def test_chunk_state_and_stateless_calls_do_not_interfere(self):
        rng=np.random.default_rng(455)
        x=(rng.normal(size=2048)+1j*rng.normal(size=2048)).astype(np.complex64)
        f=NarrowbandFilter(5040,2250)
        expected=f.filter_observation(x)
        pieces=[f.process(x[:17]),f.process(x[17:600])]
        f.filter_observation(x)  # Must not reset or consume streaming state.
        pieces += [f.process(np.zeros(0,np.complex64)),f.process(x[600:1001]),f.process(x[1001:])]
        np.testing.assert_allclose(np.concatenate(pieces),expected,atol=3e-7,rtol=3e-7)
        f.reset()
        np.testing.assert_array_equal(f.process(x),expected)

    def test_pass_tones_and_audio_dc_offset_rejection(self):
        f=NarrowbandFilter(5040,2250)
        t=np.arange(20000)/5040
        for hz in (0,750,1125):
            x=np.exp(2j*np.pi*hz*t)
            y=f.filter_observation(x)
            gain=float(np.mean(abs(y[2*len(f.taps):])))
            self.assertLess(abs(20*np.log10(gain)),.01)
        y=f.filter_observation(np.exp(-2j*np.pi*1725*t))
        self.assertLess(20*np.log10(float(np.mean(abs(y[2*len(f.taps):])))),-70.)
        self.assertLess(f.metadata()['response']['stopband_max_gain_db'],-70.)

    def test_every_basis_column_support_mask_energy_and_reference(self):
        f=NarrowbandFilter(5040,2250)
        rng=np.random.default_rng(77)
        basis=np.zeros((2,1024,2),np.complex64)
        basis[:,150:600,:]=rng.normal(size=(2,450,2))+1j*rng.normal(size=(2,450,2))
        valid=np.zeros(1024,bool);valid[12:1000]=True
        actual,mask,info=f.filter_basis(basis,valid,max_lag=30)
        self.assertTrue(mask[12+f.support_samples:1000].all())
        self.assertFalse(mask[:12+f.support_samples].any())
        self.assertFalse(mask[1000:].any())
        for bit in (0,1):
            for col in (0,1):
                np.testing.assert_array_equal(actual[bit,:,col],f.filter_observation(basis[bit,:,col]))
                self.assertFalse(actual[bit,:150,col].any())
        self.assertEqual(len(info['public_template_energy']),4)
        self.assertTrue(info['complete_tail_guard_passed'])
        for detail in info['public_template_energy']:
            self.assertAlmostEqual(detail['nominal_valid_fraction_of_filtered'],1.,places=12)
            self.assertAlmostEqual(detail['minimum_valid_fraction_over_integer_lags'],1.,places=12)

    def test_geometry_support_and_invalid_data_guards(self):
        for fs,bw in [(0,2250),(5040,float('nan')),(5040,4500),(1e9,1)]:
            with self.assertRaises(ValueError):NarrowbandFilter(fs,bw)
        f=NarrowbandFilter(5040,2250)
        with self.assertRaises(ValueError):f.filter_observation(np.ones(10))
        with self.assertRaises(ValueError):f.filter_observation(np.array([np.nan+1j]))
        mask=np.ones(1024,bool);mask[200]=False
        with self.assertRaisesRegex(ValueError,'contiguous'):f.valid_mask(mask)
        with self.assertRaises(ValueError):f.valid_mask(np.ones(10,bool))
        basis=np.zeros((2,1024,2),complex);basis[:,100:900,:]=1
        with self.assertRaisesRegex(ValueError,'FIR tail'):f.filter_basis(basis,np.ones(1024,bool),max_lag=100)
        with self.assertRaises(ValueError):f.filter_basis(basis,np.ones(1024,bool),max_lag=-1)
        with self.assertRaises(ValueError):f.filter_basis(basis,np.ones(1024,bool),max_lag=1.5)
        basis[0,:,1]=0
        with self.assertRaisesRegex(ValueError,'nonzero support'):f.filter_basis(basis,np.ones(1024,bool))


if __name__=='__main__':unittest.main()

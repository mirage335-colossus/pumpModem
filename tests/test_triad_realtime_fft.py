#!/usr/bin/env python3
"""Cheap numerical contract tests for the coarse FFT proposal stage."""
from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import fft

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_realtime_fft import FFTScreen, _quadratic_scores
from triad_dynamic_joint import _sample_cubic


def fixture():
    fs = 400
    t = np.arange(200)/fs
    envelope = np.sin(np.pi*np.arange(200)/199)**2
    source = np.array([[envelope*np.exp(2j*np.pi*(4*t+8*t*t)),
                        envelope*np.exp(2j*np.pi*(-7*t-9*t*t))],
                       [envelope*np.exp(2j*np.pi*(10*t+3*t*t)),
                        envelope*np.exp(2j*np.pi*(-12*t+8*t*t))]], dtype=np.complex128)
    manifest = dict(sample_rate_hz=fs,
        calendar=dict(capture_samples=2000, capture_lead_seconds=1.,
                      data_sample_offset=400, arrival_prior_seconds=[-.1, .1]),
        channel=dict(spread_delay_ms=17.5, spread_doppler_hz=13.),
        format=dict(occupied_bandwidth_hz=40., fine_chip_rate_hz=30.),
        power=dict(complex_noise_variance=.1))
    return manifest, source, (-1, 1)


def screen(m=None, source=None, epochs=None, **kwargs):
    if m is None:
        m, source, epochs = fixture()
    return FFTScreen(m, source, epochs, clock_ppm=kwargs.pop('clock_ppm', (0.,)),
        output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90., workers=1, **kwargs)


def waveform(m, source, epoch_index, bit, arrival, frequency, clock):
    fs = m['sample_rate_hz']
    t = np.arange(m['calendar']['capture_samples'])/fs-m['calendar']['capture_lead_seconds']
    scale = 1+clock*1e-6
    offset = m['calendar']['data_sample_offset']/fs
    y = np.zeros(len(t), complex)
    for gain, delay, doppler in ((.8+.2j, 0., 0.),
                                (.3-.6j, m['channel']['spread_delay_ms']*.001,
                                 m['channel']['spread_doppler_hz'])):
        y += gain*_sample_cubic(source[epoch_index, bit], ((t-arrival-delay)*scale-offset)*fs)*np.exp(2j*np.pi*doppler*t)
    return y*np.exp(2j*np.pi*frequency*t)


class FFTScreenTests(unittest.TestCase):
    def test_explicit_quadratic_noise_rankings_and_precision(self):
        rng = np.random.default_rng(1742)
        for precision in (np.complex64, np.complex128):
            for rays in (1, 2):
                c = (rng.normal(size=(8, rays, 1000))+
                     1j*rng.normal(size=(8, rays, 1000))).astype(precision)
                h = np.array([[1.3, .13+.27j], [.13-.27j, 1.]], np.complex128)[:rays, :rays]
                reference = np.einsum('brl,rs,bsl->bl', c.conj(), h, c, optimize=True).real
                actual = _quadratic_scores(c, h)
                np.testing.assert_allclose(actual, reference, rtol=2e-15, atol=2e-14)
                np.testing.assert_array_equal(np.argsort(actual.ravel())[-20:],
                                              np.argsort(reference.ravel())[-20:])
                self.assertEqual(actual.dtype, np.float64)

    def test_ill_conditioned_quadratic_keeps_reference_path(self):
        rng = np.random.default_rng(1842)
        c = (rng.normal(size=(2, 2, 73))+1j*rng.normal(size=(2, 2, 73))).astype(np.complex64)
        v = np.array([[1, 1j], [1j, 1]], complex)/np.sqrt(2)
        h = (v*np.array([1., 1e8])) @ v.conj().T
        reference = np.einsum('brl,rs,bsl->bl', c.conj(), h, c, optimize=True).real
        np.testing.assert_array_equal(_quadratic_scores(c, h, explicit=False), reference)
        m, source, epochs = fixture()
        m['channel'].update(spread_delay_ms=1e-5, spread_doppler_hz=0.)
        s = screen(m, source, epochs)
        self.assertTrue(all(not w['quadratic_explicit'] for w in s.workspaces))
        result = s.score(np.zeros(s.samples, complex), [0.])
        self.assertEqual(result['work']['explicit_quadratic_workspaces'], 0)
        self.assertTrue(all(np.isfinite(c['score']) for c in result['candidates']))

    def test_fft_scores_match_direct_projected_glrt(self):
        s = screen(precision='fp64')
        rng = np.random.default_rng(421)
        raw = rng.normal(size=s.samples)+1j*rng.normal(size=s.samples)
        y = s.prepare_observation(raw)
        frequency = 1.3
        y *= np.exp(-2j*np.pi*frequency*np.arange(len(y))/s.output_fs)
        for w in s.workspaces:
            corr = fft.ifft(fft.fft(y, n=s.nfft)[None]*w['transform'], axis=-1)[:, :s.lag_count]
            for lag in (0, s.lag_count//2, s.lag_count-1):
                direct = w['columns'].conj() @ y[lag:lag+s.kernel_rows]
                np.testing.assert_allclose(corr[:, lag], direct, rtol=2e-12, atol=2e-12)
                score = np.vdot(corr[:, lag], w['inverse']@corr[:, lag]).real
                reference = np.linalg.norm(np.linalg.lstsq(w['columns'].T,
                    y[lag:lag+s.kernel_rows], rcond=1e-10)[0] @ w['columns'])**2
                self.assertAlmostEqual(score, reference, places=9)

    def test_valid_only_fft_padding_matches_every_direct_lag(self):
        padding_cases = set()
        rng = np.random.default_rng(1852)
        for extra_lags in range(8):
            m, source, epochs = fixture()
            m['calendar']['arrival_prior_seconds'][1] += extra_lags/200
            for precision in ('fp32', 'fp64'):
                s = screen(m, source, epochs, precision=precision)
                self.assertEqual(s.nfft, fft.next_fast_len(s.crop_rows))
                self.assertLess(s.nfft, s.crop_rows+s.kernel_rows-1)
                padding_cases.add(s.nfft > s.crop_rows)
                y = (rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)).astype(s.dtype)
                transformed = fft.fft(y, n=s.nfft)
                for w in s.workspaces:
                    corr = fft.ifft(transformed[None]*w['transform'], axis=-1)[:, :s.lag_count]
                    direct = np.asarray([w['columns'].astype(np.complex128).conj() @
                        y[lag:lag+s.kernel_rows].astype(np.complex128)
                        for lag in range(s.lag_count)]).T
                    tolerance = 3e-6 if precision == 'fp32' else 3e-13
                    np.testing.assert_allclose(corr, direct, rtol=tolerance,
                                               atol=tolerance*max(1., np.abs(direct).max()))
                    scores = np.einsum('rl,rs,sl->l', corr.conj(), w['inverse'], corr).real
                    reference = np.einsum('rl,rs,sl->l', direct.conj(), w['inverse'], direct).real
                    np.testing.assert_allclose(scores, reference, rtol=tolerance*3,
                                               atol=tolerance*3*max(1., reference.max()))
                    self.assertEqual(int(np.argmax(scores)), int(np.argmax(reference)))
        self.assertEqual(padding_cases, {False, True})

    def test_clock_delay_and_endpoint_arrivals(self):
        m, source, epochs = fixture()
        clocks = (-10000., 0., 10000.)
        s = screen(m, source, epochs, clock_ppm=clocks, precision='fp64')
        for arrival, clock, bit in ((-.1, -10000., 0), (.1, 10000., 1)):
            raw = waveform(m, source, 1, bit, arrival, 0., clock)
            result = s.score(raw, [0.], top_per_bit=2)
            winner = max(result['candidates'], key=lambda x:x['score'])
            self.assertEqual(winner['bit'], bit)
            self.assertEqual(winner['epoch'], epochs[1])
            self.assertAlmostEqual(winner['arrival'], arrival, places=12)
            self.assertEqual(winner['clock_ppm'], clock)
            self.assertEqual({c['bit'] for c in result['candidates']}, {0, 1})
            self.assertTrue(result['work']['source_support_complete'])

    def test_fp32_and_fp64_agree_on_rankings(self):
        m, source, epochs = fixture()
        raw = waveform(m, source, 0, 1, .025, .2, 0.)
        results = [screen(m, source, epochs, precision=p).score(raw, [-.2, 0., .2])
                   for p in ('fp32', 'fp64')]
        for a, b in zip(results[0]['candidates'], results[1]['candidates']):
            self.assertEqual({k:v for k,v in a.items() if k!='score'},
                             {k:v for k,v in b.items() if k!='score'})
            self.assertAlmostEqual(a['score']/b['score'], 1., places=5)

    def test_frequency_intervals_fine_grid_and_limits(self):
        s = screen(maximum_frequency_points=8)
        raw = np.zeros(s.samples, complex)
        result = s.score(raw, frequency_intervals=[[-.5, .5]])
        self.assertEqual(result['status'], 'proposed')
        self.assertLessEqual(np.diff(result['frequencies_hz']).max(), s.maximum_frequency_step)
        result = s.score(raw, frequency_intervals=[[-10., 10.]])
        self.assertEqual(result['reason'], 'frequency_work_limit')
        self.assertEqual(result['candidates'], [])
        result = s.score(raw, frequencies_hz=[50.])
        self.assertEqual(result['reason'], 'frequency_bank_outside_nominal_passband')
        result = s.score(raw, frequencies_hz=[-2., 2.])
        self.assertEqual(result['reason'], 'explicit_frequency_grid_too_coarse')

    def test_erasure_and_truncated_capture_rejected(self):
        s = screen()
        valid = np.ones(s.samples, bool)
        valid[100] = False
        with self.assertRaisesRegex(ValueError, 'erasures'):
            s.score(np.zeros(s.samples, complex), [0.], valid=valid)
        m, source, epochs = fixture()
        m['calendar']['capture_samples'] = 500
        with self.assertRaisesRegex(ValueError, 'complete source support'):
            screen(m, source, epochs)

    def test_policy_and_inputs_are_bound(self):
        m, source, epochs = fixture()
        a = screen(m, source, epochs)
        b = screen(m, source*1.001, epochs)
        self.assertNotEqual(a.procedure_id, b.procedure_id)
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples), [0.])
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples, complex), [0., 0.])
        with self.assertRaises(ValueError):
            a.score(np.zeros(a.samples, complex), [0.], frequency_intervals=[[0., 0.]])


if __name__ == '__main__':
    unittest.main()

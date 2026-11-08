#!/usr/bin/env python3
"""Cheap direct-arithmetic contracts for segmented complex FFT proposals."""
import copy
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import fft

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_segmented_fft import SegmentedFFTScreen, _correlation_fft_length
from triad_realtime_fft import _quadratic_scores
from test_triad_realtime_fft import fixture, waveform, screen as whole_screen


def screen(m=None, source=None, epochs=None, **kwargs):
    if m is None:
        m, source, epochs = fixture()
    return SegmentedFFTScreen(m, source, epochs, clock_ppm=kwargs.pop('clock_ppm', (0.,)),
        output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90., workers=1,
        block_seconds=kwargs.pop('block_seconds', .15), **kwargs)


class SegmentedFFTTests(unittest.TestCase):
    def test_every_lag_matches_direct_block_sum(self):
        rng = np.random.default_rng(47713)
        for precision in ('fp32', 'fp64'):
            s = screen(precision=precision)
            y = (rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)).astype(s.dtype)
            frequencies = np.array([-s.maximum_frequency_step, 0., s.maximum_frequency_step])
            actual = s._score_grid(y, frequencies)
            direct = np.zeros_like(actual)
            for bi, (start, stop) in enumerate(s.blocks):
                for fi, frequency in enumerate(frequencies):
                    segment = y[start:stop+s.lag_count-1]*np.exp(
                        -2j*np.pi*frequency*np.arange(stop-start+s.lag_count-1)/s.output_fs)
                    for wi in range(len(s.workspaces)):
                        a = s.columns[wi, :, start:stop].astype(complex)
                        basis = np.linalg.svd(a.T, full_matrices=False)[0][:, :s.ranks[bi,wi]]
                        for lag in range(s.lag_count):
                            c = basis.conj().T @ segment[lag:lag+stop-start]
                            direct[wi, fi, lag] += np.vdot(c, c).real/s.reference_variance
            tolerance = 2e-6 if precision == 'fp32' else 3e-12
            np.testing.assert_allclose(actual, direct, atol=tolerance, rtol=tolerance)
            np.testing.assert_array_equal(actual.argmax(axis=-1), direct.argmax(axis=-1))
            self.assertEqual(s.blocks[0][0], 0)
            self.assertEqual(s.blocks[-1][1], s.kernel_rows)
            self.assertEqual(sum(b-a for a,b in s.blocks), s.kernel_rows)

    def test_one_block_matches_whole_bit_statistic(self):
        rng = np.random.default_rng(523)
        s = screen(block_seconds=100, precision='fp64')
        original = whole_screen(precision='fp64')
        y = rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)
        actual = s._score_grid(y, np.array([0.]))
        for wi, w in enumerate(original.workspaces):
            c = fft.ifft(fft.fft(y, n=original.nfft)[None]*w['transform'], axis=-1)[:, :s.lag_count]
            expected = _quadratic_scores(c[None], w['inverse'])/s.reference_variance
            np.testing.assert_allclose(actual[wi], expected, rtol=4e-12, atol=4e-12)

    def test_zero_anchored_grid_covers_interval_and_limits(self):
        s = screen(block_seconds=.5, maximum_frequency_points=9)
        raw = np.zeros(s.samples, complex)
        r = s.score(raw, frequency_intervals=[[-1.13, .82]])
        np.testing.assert_equal(r['frequencies_hz'], [-1.5, -1., -.5, 0., .5, 1.])
        self.assertEqual({c['bit'] for c in r['candidates']}, {0, 1})
        self.assertEqual(s.score(raw, frequency_intervals=[[-9., 9.]])['reason'], 'frequency_work_limit')
        self.assertEqual(s.score(raw, frequencies_hz=[.1])['reason'], 'explicit_frequency_grid_not_zero_anchored')
        self.assertEqual(s.score(raw, frequencies_hz=[0., 1.])['reason'], 'explicit_frequency_grid_too_coarse')
        limited = screen(maximum_fft_products=1)
        self.assertEqual(limited.score(raw, [0.])['reason'], 'fft_product_work_limit')

    def test_timing_clocks_both_bits_and_precision(self):
        m, source, epochs = fixture()
        for arrival, clock, bit in ((-.1, -10000., 0), (.1, 10000., 1)):
            raw = waveform(m, source, 1, bit, arrival, 0., clock)
            results = [screen(m, source, epochs, clock_ppm=(-10000.,0.,10000.),
                              precision=p).score(raw, [0.], top_per_bit=2)
                       for p in ('fp32','fp64')]
            for r in results:
                winner = max(r['candidates'], key=lambda c:c['score'])
                self.assertEqual((winner['bit'], winner['epoch'], winner['clock_ppm']),
                                 (bit, epochs[1], clock))
                self.assertAlmostEqual(winner['arrival'], arrival, places=12)
            for a,b in zip(results[0]['candidates'], results[1]['candidates']):
                self.assertEqual({k:v for k,v in a.items()if k!='score'},
                                 {k:v for k,v in b.items()if k!='score'})
                # This comparison includes independently FP32-filtered source
                # columns, so nearly dependent tiny tail subspaces can change
                # before the FFT. A 2e-5 relative representation budget is
                # <0.00009 dB. Arithmetic on the SAME represented columns is
                # checked against independent SVD at 2e-6 in the all-lag test.
                self.assertAlmostEqual(a['score']/b['score'], 1., delta=2e-5)

    def test_renewing_phase_uses_accumulation_before_energy(self):
        s = screen(precision='fp64')
        wi = 0
        y = np.zeros(s.crop_rows, complex)
        lag = s.lag_count//2
        # Synthetic projected diagnostic: alternate independent constant block
        # gains. Physical raw-channel validation is a separate campaign.
        for bi,(start,stop) in enumerate(s.blocks):
            y[lag+start:lag+stop] = ((-1)**bi)*s.columns[wi,0,start:stop]
        score = s._score_grid(y,np.array([0.]))[wi,0,lag]
        expected = np.vdot(y,y).real/s.reference_variance
        self.assertAlmostEqual(score/expected,1.,places=10)

    def test_rank_empty_tails_memory_and_validity(self):
        m, source, epochs = fixture()
        m['channel'].update(spread_delay_ms=1e-7, spread_doppler_hz=0.)
        s = screen(m, source, epochs, precision='fp64')
        self.assertTrue(np.any(s.ranks < 2))
        raw = np.zeros(s.samples, complex)
        result = s.score(raw,[0.])
        self.assertTrue(all(np.isfinite(c['score'])for c in result['candidates']))
        self.assertTrue(all('transform' not in w for w in s.workspaces))
        s.memory_limit_bytes = s.resident_estimate
        self.assertEqual(s.score(raw,[0.])['reason'], 'segmented_fft_memory_limit')
        s.memory_limit_bytes=2**30
        valid=np.ones(s.samples,bool);valid[4]=False
        with self.assertRaisesRegex(ValueError,'erasures'):
            s.score(raw,[0.],valid=valid)

    def test_public_policy_and_block_work(self):
        a=screen(block_seconds=.25);b=screen(block_seconds=.5)
        self.assertNotEqual(a.procedure_id,b.procedure_id)
        self.assertEqual(a.maximum_frequency_step,1.)
        work=a.work(3)
        self.assertEqual(work['fft_products'],len(a.blocks)*len(a.workspaces)*len(a.rays)*3)
        self.assertEqual(work['observation_ffts'],len(a.blocks)*3)
        self.assertLess(work['estimated_peak_bytes'],a.memory_limit_bytes)
        with self.assertRaises(ValueError):screen(block_seconds=0)

    def test_padding_and_tiling_preserve_all_scores(self):
        rng = np.random.default_rng(83122)
        for precision in ('fp32', 'fp64'):
            reference = screen(precision=precision, transform_batch_bytes=2**30)
            tiled = screen(precision=precision, transform_batch_bytes=1,
                           workspace_batch_size=2)
            y = (rng.normal(size=tiled.crop_rows)+1j*rng.normal(size=tiled.crop_rows)).astype(tiled.dtype)
            frequencies = np.array([-tiled.maximum_frequency_step, 0., tiled.maximum_frequency_step])
            expected = reference._score_grid(y, frequencies)
            actual = tiled._score_grid(y, frequencies)
            tolerance = 2e-6 if precision == 'fp32' else 4e-12
            np.testing.assert_allclose(actual, expected, atol=tolerance, rtol=tolerance)
            np.testing.assert_array_equal(actual.argmax(axis=-1), expected.argmax(axis=-1))
            self.assertEqual(tiled._batch_shape(max(tiled.block_nfft), 3), (1, 1))
            work = tiled.work(3)
            self.assertEqual(work['maximum_transform_product_bytes'],
                max(len(tiled.rays)*n*np.dtype(tiled.dtype).itemsize for n in tiled.block_nfft))
            self.assertLessEqual(work['executed_fft_products'], work['fft_products'])
            self.assertNotEqual(tiled.procedure_id, reference.procedure_id)

    def test_bounded_padding_handles_radix_and_power_boundaries(self):
        # The reference-host regression was a repeated radix-11 length; the
        # choice changes padding only, while the independent SVD test above
        # checks every requested linear-correlation lag in both precisions.
        self.assertEqual(_correlation_fft_length(14582), 16384)
        for rows in (1, 2, 17, 511, 512, 513, 8192, 8193, 14582, 16384, 16385):
            actual = _correlation_fft_length(rows)
            smooth = fft.next_fast_len(rows, real=True)
            self.assertGreaterEqual(actual, rows)
            self.assertLessEqual(actual, smooth+smooth//10)
        with self.assertRaisesRegex(ValueError, 'transform batch bytes'):
            screen(transform_batch_bytes=0)

    def test_zero_rank_blocks_preserve_the_full_zero_projection(self):
        m, source, epochs = fixture()
        source[..., :100] = 0
        s = screen(m, source, epochs, precision='fp64', block_seconds=.1)
        work = s.work(1)
        self.assertGreater(work['skipped_zero_rank_blocks'], 0)
        rng = np.random.default_rng(2856)
        y = rng.normal(size=s.crop_rows)+1j*rng.normal(size=s.crop_rows)
        actual = s._score_grid(y, np.array([0.]))
        # Force execution of the same exactly-zero represented projections;
        # only rank bookkeeping changes, never columns or observations.
        s.ranks[s.ranks == 0] = 1
        full_zero_projection = s._score_grid(y, np.array([0.]))
        np.testing.assert_array_equal(actual, full_zero_projection)
        self.assertEqual(s.work(1)['skipped_zero_rank_blocks'], 0)


if __name__=='__main__':unittest.main()

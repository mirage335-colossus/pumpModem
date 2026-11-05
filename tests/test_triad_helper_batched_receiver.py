#!/usr/bin/env python3
"""Small actual-receiver equivalence and clock-coordinate checks."""
from pathlib import Path
import sys
import unittest
from unittest import mock
import weakref

import numpy as np
from scipy import signal
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_helper_receiver import HelperDataReceiver, clock_warp, data_fir_taps
from triad_helper_fft import BatchedFFTReceiver, _check_precision
from test_triad_helper_receiver import source_templates, receiver, capture


class BatchedReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_actual_legacy_likelihood_matches_cached_batches(self):
        templates = source_templates()
        old = receiver(templates)
        new = receiver(templates, fft_backend="batched", frequency_batch_size=2)
        raw = capture(templates, old, bit=1, lag=12, frequency=8.)
        expected = old.score(raw)
        actual = new.score(raw)
        np.testing.assert_allclose(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"],
                                   rtol=2e-11, atol=2e-8)
        self.assertEqual(actual["bit"], expected["bit"])
        self.assertEqual(actual["best_group_public_parameters"]["clock_ppm"], 0.)
        self.assertEqual(actual["geometry"]["effective_group_count"], expected["geometry"]["effective_group_count"])
        self.assertFalse(actual["resources"]["retained_group_scores"])

    def test_aligned_cfo_shift_and_arbitrary_frequency_match_legacy(self):
        templates = source_templates()
        old = receiver(templates, frequency_offsets=(0.,))
        child = old._receivers[0]
        w = child._workspace
        step = 900 / w.nfft
        bank = (-3 * step, 0., 5 * step, 1.234)
        adapted = BatchedFFTReceiver(child._sampled.array("basis.c128"), 900, old.lags,
            key=child._sampled.key, frequencies=child._sampled.frequencies,
            reference_variance=old.reference_variance, precision="fp64", device="cpu",
            memory_limit_bytes=64 * 1024**2, preprocessing_identity="test", frequency_batch_size=3)
        raw = capture(templates, old, bit=0, frequency=1.234)
        prepared = old.prepare_observation(raw)
        expected = old.with_frequency_offsets(bank).score_prepared(prepared)
        actual = adapted.score(prepared.samples, bank)
        np.testing.assert_allclose(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"],
                                   rtol=2e-11, atol=2e-8)
        self.assertEqual(actual["resources"]["aligned_cfo_count"], 3)

    def test_wide_filter_and_broad_bank_are_explicit(self):
        taps = data_fir_taps("wide")
        frequencies, response = signal.freqz(taps, worN=131072, fs=18000)
        self.assertLess(np.max(abs(abs(response[frequencies <= 400]) - 1)), 1e-4)
        self.assertLess(20 * np.log10(np.max(abs(response[frequencies >= 440]))), -80.)
        templates = source_templates(rows=1800)
        bank = np.linspace(-90.522, 90.522, 5001)
        rx = receiver(templates, fft_backend="batched", frontend_profile="wide", frequency_offsets=bank,
                      clock_ppm_values=(-300., 0., 300.), doppler_scale_values=(.99985, 1., 1.00015),
                      memory_limit_bytes=128 * 1024**2)
        self.assertEqual(rx.geometry_count if hasattr(rx, "geometry_count") else rx.effective_group_count,
                         len(rx.lags) * 5001 * 9)
        self.assertEqual(len(rx._cache), 0)  # No huge bank allocation at construction.
        with self.assertRaises(ValueError):
            receiver(templates, frequency_offsets=bank)
        with self.assertRaises(ValueError):
            rx.with_hypotheses(clock_ppm_values=(1.,))

    def test_cuda_strict_fp32_guard_without_gpu(self):
        with mock.patch("torch.get_float32_matmul_precision", return_value="medium"):
            with self.assertRaisesRegex(ValueError, "FP32 minimum"):
                _check_precision("cuda:0", "fp32")
            _check_precision("cpu", "fp32")
            _check_precision("cuda:0", "fp64")

    def test_fp32_actual_receiver_and_complete_family_allocation(self):
        templates = source_templates(3, rows=1800)
        rx = receiver(templates, fft_backend="batched", precision="fp32", frequency_batch_size=2)
        raw = capture(templates, rx, bit=1, epoch=2, frequency=8.)
        actual = rx.score(raw)
        self.assertEqual(actual["bit"], 1)
        expected = np.asarray([o["bit_log_likelihoods"] for o in actual["hypothesis_bit_log_likelihoods"]])
        from scipy.special import logsumexp
        np.testing.assert_allclose(actual["bit_log_likelihoods"], logsumexp(expected, axis=0) - np.log(3), atol=1e-10)
        self.assertEqual(actual["calibration"]["residual_family_groups"], actual["geometry"]["effective_group_count"])
        alternate = receiver(templates, fft_backend="batched", precision="fp32", reference_variance=.002)
        self.assertNotEqual(rx.receiver_id, alternate.receiver_id)

    def test_clock_warp_matches_independent_bandlimited_tone(self):
        rows = 18000
        frequency = 112.5
        source = np.exp(2j * np.pi * frequency * np.arange(rows) / 18000)
        for ppm in (-300., 0., 300.):
            actual = clock_warp(source, ppm)
            if ppm == 0:
                np.testing.assert_array_equal(actual, source)
            else:
                ids = np.arange(8, len(actual) - 8)
                expected = np.exp(2j * np.pi * frequency * ids / 18000 * (1 + ppm * 1e-6))
                self.assertLess(np.max(abs(actual[ids] - expected)), 1e-7)

    def test_independent_analytic_clock_and_rx_rate_injection(self):
        rows = 36000
        templates = source_templates(rows=rows)
        clock, rate = 6000., 1.0008
        rx = receiver(templates, fft_backend="batched", frequency_offsets=(8.,),
                      clock_ppm_values=(-6000., 0., 6000.), doppler_scale_values=(rate,),
                      capture_rows=54000, memory_limit_bytes=128 * 1024**2)
        audio = 1 + clock * 1e-6
        times = np.arange(rx.capture_rows) / 18000
        arrival = 8 / 900 * rate
        def analytic(position):
            stop = (rows - 1) / 18000
            supported = (position >= 0) & (position <= stop)
            envelope = np.sin(np.pi * position / stop)**2 * supported
            return envelope * np.exp(2j * np.pi * 145 * position) * (1 + .35 * np.cos(2 * np.pi * 13 * position))
        direct = analytic((times - (rx.capture_lead_seconds + arrival) / rate - rx.source_offset_seconds / audio) * audio)
        delayed = analytic((times - (rx.capture_lead_seconds + arrival + rx.delay_seconds) / rate - rx.source_offset_seconds / audio) * audio)
        propagated = ((1.2 + .3j) * direct + (.6 - .4j) * delayed *
                      np.exp(2j * np.pi * rx.doppler_hz * rate * (times - rx.capture_lead_seconds / rate)))
        rng = np.random.default_rng(77)
        raw = propagated * np.exp(2j * np.pi * 8 * times) + .05 * (rng.normal(size=len(times)) + 1j * rng.normal(size=len(times)))
        actual = rx.score(raw)
        self.assertEqual(actual["bit"], 1)
        self.assertEqual(actual["best_group_public_parameters"]["clock_ppm"], clock)
        self.assertEqual(actual["best_group_public_parameters"]["doppler_scale"], rate)
        self.assertAlmostEqual(actual["best_group_public_parameters"]["arrival_physical_seconds"], arrival)

    def test_variants_share_bounded_cache_and_normalize_duplicate_sources(self):
        templates = source_templates(rows=1800)
        rx = receiver(templates, fft_backend="batched", frequency_offsets=(0.,),
                      clock_ppm_values=(-300., 0., 300.), max_cached_hypotheses=1)
        raw = capture(templates, rx, bit=0, frequency=0.)
        prepared = rx.prepare_observation(raw)
        previous = []
        def checked_constructor(*args, **kwargs):
            if previous:
                self.assertIsNone(previous[-1](), "evicted workspace must die before next allocation")
            child = BatchedFFTReceiver(*args, **kwargs)
            previous.append(weakref.ref(child))
            return child
        with mock.patch("triad_helper_fft.BatchedFFTReceiver", side_effect=checked_constructor):
            outcome = rx.score_prepared(prepared)
        self.assertEqual(len(rx._cache), 1)
        self.assertEqual(len(outcome["hypothesis_bit_log_likelihoods"]), 3)
        variant = rx.with_hypotheses(clock_ppm_values=(0.,))
        self.assertIs(variant._cache, rx._cache)
        self.assertIs(variant._cache_lock, rx._cache_lock)
        self.assertNotEqual(variant.receiver_id, rx.receiver_id)
        self.assertEqual(variant.score_prepared(prepared)["geometry"]["clock_ppm_values"], [0.])


if __name__ == "__main__":
    unittest.main()

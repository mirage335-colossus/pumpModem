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
        raw = capture(templates, old, bit=1, lag=13, frequency=8.)
        expected = old.score(raw)
        actual = new.score(raw)
        np.testing.assert_allclose(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"],
                                   rtol=2e-11, atol=2e-8)
        self.assertEqual(actual["bit"], expected["bit"])
        self.assertEqual(actual["best_group_public_parameters"]["clock_ppm"], 0.)
        self.assertEqual(actual["geometry"]["effective_group_count"], expected["geometry"]["effective_group_count"])
        self.assertFalse(actual["resources"]["retained_group_scores"])
        np.testing.assert_array_equal(new.lags, np.arange(19))
        self.assertAlmostEqual(actual["best_group_public_parameters"]["arrival_seconds"], 13 / 900)

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

    def test_known_masked_grams_match_legacy_and_keep_caches_separate(self):
        templates = source_templates()
        valid = np.ones(18000, np.uint8)
        valid[7200:7380] = 0
        old = receiver(templates, capture_validity=valid)
        new = receiver(templates, fft_backend="batched", frequency_batch_size=2,
                       capture_validity=valid)
        raw = capture(templates, old, bit=1, lag=13, frequency=8.)
        expected, actual = old.score(raw, valid=valid), new.score(raw, valid=valid)
        np.testing.assert_allclose(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"],
                                   rtol=2e-11, atol=2e-8)
        self.assertEqual(actual["bit"], 1)
        self.assertEqual(new._cache[(0, 0., 1.)][0].workspace.valid_count,
                         int(new.output_validity.sum()))
        variant = new.with_frequency_offsets((8.,))
        self.assertIs(variant._cache, new._cache)
        self.assertIs(variant.output_validity, new.output_validity)
        unmasked = receiver(templates, fft_backend="batched", frequency_batch_size=2)
        self.assertIsNot(new._cache, unmasked._cache)
        self.assertNotEqual(new.receiver_id, unmasked.receiver_id)
        self.assertGreater(new.explicit_array_bound_bytes, unmasked.explicit_array_bound_bytes)

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

    def test_public_narrow_source_high_doppler_uses_actual_frontend_support(self):
        t = np.arange(3600) / 18000
        envelope = np.sin(np.pi * np.arange(len(t)) / (len(t) - 1))**2
        templates = np.asarray([[envelope * np.exp(2j * np.pi * f * t) for f in (9., -23.)]])
        options = dict(frontend_profile="wide", doppler_hz=165.,
                       source_half_bandwidth_hz=31.25, channel_line_half_width_hz=330.)
        old = receiver(templates, **options)
        new = receiver(templates, fft_backend="batched", **options)
        for bit in (0, 1):
            raw = capture(templates, old, bit=bit, lag=13, frequency=8.)
            expected, actual = old.score(raw), new.score(raw)
            self.assertEqual(actual["bit"], bit)
            self.assertEqual(expected["bit"], bit)
            np.testing.assert_allclose(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"],
                                       rtol=2e-11, atol=2e-8)
        for rx in (old, new):
            self.assertEqual(rx.preprocessing["source_half_bandwidth_hz"], 31.25)
            self.assertEqual(rx.preprocessing["channel_line_half_width_hz"], 330.)
            self.assertEqual(rx.preprocessing["pass_edge_hz"], 400.)
            with self.assertRaises(ValueError):
                rx.with_frequency_offsets((39.,))  # 31.25 + 330 + 39 > 400.

    def test_public_band_support_boundaries_and_independent_modulation_width(self):
        templates = source_templates(rows=1800)
        for backend in ("legacy", "batched"):
            wide = receiver(templates, fft_backend=backend, frontend_profile="wide",
                            source_half_bandwidth_hz=375., channel_line_half_width_hz=12.,
                            frequency_offsets=(-.6, 0., .6))
            self.assertEqual(wide.source_half_bandwidth_hz, 375.)
            with self.assertRaises(ValueError):
                receiver(templates, fft_backend=backend, source_half_bandwidth_hz=375.,
                         channel_line_half_width_hz=12., frequency_offsets=(.6,))
            # A separately selected fast-modulation width can exceed 2*D.
            independent = receiver(templates, fft_backend=backend, frontend_profile="wide",
                channel_line_half_width_hz=250., frequency_offsets=(37.5,))
            with self.assertRaises(ValueError):
                independent.with_frequency_offsets((37.6,))
            for invalid in (dict(doppler_hz=450.), dict(doppler_hz=900.),
                            dict(doppler_hz=165., channel_line_half_width_hz=164.9),
                            dict(source_half_bandwidth_hz=0.), dict(source_half_bandwidth_hz=np.nan),
                            dict(channel_line_half_width_hz=-1.), dict(channel_line_half_width_hz=np.inf)):
                with self.subTest(backend=backend, invalid=invalid), self.assertRaises(ValueError):
                    receiver(templates, fft_backend=backend, frontend_profile="wide", **invalid)
        # Clock/rate subset variants retain and recheck the declared widths.
        original = receiver(templates, fft_backend="batched", frontend_profile="wide",
            source_half_bandwidth_hz=31.25, channel_line_half_width_hz=330., doppler_hz=165.,
            clock_ppm_values=(-300., 0., 300.), doppler_scale_values=(.99985, 1., 1.00015))
        subset = original.with_hypotheses(clock_ppm_values=(0.,), doppler_scale_values=(1.,))
        self.assertEqual(subset.source_half_bandwidth_hz, 31.25)
        self.assertEqual(subset.channel_line_half_width_hz, 330.)
        with self.assertRaises(ValueError): subset.with_frequency_offsets((39.,))
        other = receiver(templates, fft_backend="batched", frontend_profile="wide",
            source_half_bandwidth_hz=31.25, channel_line_half_width_hz=331., doppler_hz=165.,
            clock_ppm_values=(-300., 0., 300.), doppler_scale_values=(.99985, 1., 1.00015))
        self.assertNotEqual(original.receiver_id, other.receiver_id)

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

    def test_excessive_cache_capacity_clamps_to_reachable_keys_without_score_change(self):
        templates = source_templates(epochs=3, rows=1800)
        options = dict(fft_backend="batched", frequency_offsets=(0.,),
                       memory_limit_bytes=64 * 1024**2)
        requested13 = receiver(templates, max_cached_hypotheses=13, **options)
        requested400 = receiver(templates, max_cached_hypotheses=400, **options)
        self.assertEqual(requested13.requested_cache_capacity, 13)
        self.assertEqual(requested400.requested_cache_capacity, 400)
        self.assertEqual(requested13.reachable_cache_key_count, 3)
        self.assertEqual(requested400.reachable_cache_key_count, 3)
        self.assertEqual(requested13._cache_capacity, requested400._cache_capacity)
        self.assertEqual(requested400._cache_capacity, 3)
        self.assertLess(requested400.explicit_array_bound_bytes, requested400.memory_limit_bytes)
        self.assertEqual(requested13.explicit_array_bound_bytes, requested400.explicit_array_bound_bytes)

        raw = capture(templates, requested13, bit=1, epoch=2, lag=13, frequency=0.)
        expected, actual = requested13.score(raw), requested400.score(raw)
        self.assertEqual(actual["bit"], expected["bit"])
        np.testing.assert_array_equal(actual["bit_log_likelihoods"], expected["bit_log_likelihoods"])
        self.assertEqual(actual["resources"]["requested_maximum_cached_hypotheses"], 400)
        self.assertEqual(actual["resources"]["maximum_cached_hypotheses"], 3)
        self.assertEqual(len(requested400._cache), 3)

    def test_cache_capacity_preserves_superset_for_shared_subset_variants(self):
        templates = source_templates(epochs=3, rows=1800)
        original = receiver(templates, fft_backend="batched", frequency_offsets=(0.,),
            clock_ppm_values=(-300., 0., 300.), doppler_scale_values=(.99985, 1., 1.00015),
            max_cached_hypotheses=400, memory_limit_bytes=128 * 1024**2)
        reachable = 3 * 3 * 3
        self.assertEqual(original.reachable_cache_key_count, reachable)
        self.assertEqual(original._cache_capacity, reachable)
        subset = original.with_hypotheses(clock_ppm_values=(0.,), doppler_scale_values=(1.,))
        self.assertIs(subset._cache, original._cache)
        self.assertEqual(subset.reachable_cache_key_count, reachable)
        self.assertEqual(subset._cache_capacity, reachable)
        self.assertEqual(subset.requested_cache_capacity, 400)


if __name__ == "__main__":
    unittest.main()

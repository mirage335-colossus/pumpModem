#!/usr/bin/env python3
"""Bounded hardware-free checks of the isolated Helper DATA adapter."""
from pathlib import Path
from dataclasses import replace
import sys
import unittest

import numpy as np
from scipy import signal
from scipy.special import logsumexp
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_helper_receiver import HelperDataReceiver, data_fir_taps, fractional_delay
from triad_live_receiver import empirical_decision


def source_templates(epochs=1, rows=7200):
    t = np.arange(rows) / 18000
    envelope = np.sin(np.pi * np.arange(rows) / (rows - 1))**2
    return np.asarray([[envelope * np.exp(2j * np.pi * (frequency + 19 * epoch) * t)
                        * (1 + .35 * np.cos(2 * np.pi * (13 + 3 * epoch) * t))
                        for frequency in (55, 145)] for epoch in range(epochs)])


def receiver(templates, **overrides):
    options = dict(source_offset_seconds=.2, capture_lead_seconds=.1,
                   capture_rows=18000, arrival_bounds_seconds=(0., .02),
                   delay_seconds=.0005, doppler_hz=6., reference_variance=.001,
                   frequency_offsets=(-8., 0., 8.), precision="fp64",
                   memory_limit_bytes=64 * 1024 * 1024)
    options.update(overrides)
    return HelperDataReceiver(templates, tuple(range(templates.shape[0])), **options)


def capture(templates, rx, *, bit, epoch=0, lag=8, frequency=8., phase=.7):
    """Independent integer-coordinate two-ray injection before the DATA FIR."""
    rng = np.random.default_rng(29)
    # Keep the full-column residual well above floating-point cancellation.
    # A near-noiseless .003 fixture produced ~3.09e8 working evidence and
    # 1.17e-10 relative rotation error in the existing residual normalizer.
    result = .05 * (rng.normal(size=rx.capture_rows) + 1j * rng.normal(size=rx.capture_rows))
    start = round((rx.capture_lead_seconds + rx.source_offset_seconds +
                   rx.arrival_bounds_seconds[0]) * 18000) + lag * 20
    delay = round(rx.delay_seconds * 18000)
    source = templates[epoch, bit]
    times = np.arange(rx.capture_rows) / 18000 - rx.capture_lead_seconds
    direct = np.zeros(rx.capture_rows, complex)
    delayed = direct.copy()
    direct[start:start + len(source)] = source
    delayed[start + delay:start + delay + len(source)] = source
    propagated = (1.2 + .3j) * direct + (.6 - .4j) * delayed * np.exp(2j * np.pi * rx.doppler_hz * times)
    return result + propagated * np.exp(1j * phase + 2j * np.pi * frequency * times)


class HelperDataReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_both_bits_timing_frequency_and_constant_phase(self):
        templates = source_templates()
        rx = receiver(templates)
        for bit, lag, cfo in ((0, 5, -8.), (1, 13, 8.)):
            raw = capture(templates, rx, bit=bit, lag=lag, frequency=cfo)
            outcome = rx.score(raw)
            self.assertEqual(outcome["bit"], bit)
            self.assertAlmostEqual(outcome["best_group_public_parameters"]["arrival_seconds"], lag / 900)
            self.assertEqual(outcome["best_group_public_parameters"]["frequency_hz"], cfo)
            rotated = rx.score(raw * np.exp(1.73j))
            np.testing.assert_allclose(outcome["bit_log_likelihoods"], rotated["bit_log_likelihoods"], atol=2e-8, rtol=1e-11)
            self.assertFalse(outcome["calibration"]["analytic_admission_validated"])
            self.assertNotIn("accepted", outcome)

    def test_epoch_log_mixture_and_shared_preprocessing(self):
        templates = source_templates(3)
        rx = receiver(templates)
        raw = capture(templates, rx, bit=1, epoch=2)
        prepared = rx.prepare_observation(raw)
        outcome = rx.score_prepared(prepared)
        epoch_scores = np.asarray([child.score(prepared.samples)["bit_log_likelihoods"]
                                   for child in rx._receivers])
        expected = logsumexp(epoch_scores, axis=0) - np.log(3)
        np.testing.assert_allclose(outcome["bit_log_likelihoods"], expected, atol=1e-12)
        self.assertEqual(outcome["bit"], 1)
        self.assertEqual(outcome["best_epoch_offset_seconds"], 2)
        assisted = rx.with_frequency_offsets((8.,))
        for baseline, narrower in zip(rx._receivers, assisted._receivers):
            self.assertIs(baseline._workspace, narrower._workspace)
            self.assertIs(baseline._lock, narrower._lock)
        self.assertIs(rx.taps, assisted.taps)
        self.assertNotEqual(rx.receiver_id, assisted.receiver_id)
        self.assertEqual(assisted.score_prepared(prepared)["bit"], 1)
        self.assertFalse(prepared.samples.flags.writeable)
        # A changed frequency bank cannot reuse another bank's empirical gate.
        gate = rx.freeze_calibration(np.arange(19))
        with self.assertRaisesRegex(ValueError, "identity differs"):
            empirical_decision(assisted.score_prepared(prepared), gate)

    def test_duplicate_epoch_normalization_identity(self):
        one = source_templates()
        rx = receiver(one, frequency_offsets=(0.,))
        repeated = receiver(np.repeat(one, 3, axis=0), frequency_offsets=(0.,))
        raw = capture(one, rx, bit=0, frequency=0.)
        np.testing.assert_allclose(rx.score(raw)["bit_log_likelihoods"],
                                   repeated.score(raw)["bit_log_likelihoods"], atol=1e-10)

    def test_filter_alias_rejection_and_cfo_passband_ripple(self):
        taps = data_fir_taps()
        frequencies, response = signal.freqz(taps, worN=131072, fs=18000)
        rejection_db = -20 * np.log10(np.max(abs(response[frequencies >= 430])))
        self.assertGreaterEqual(rejection_db, 80.)
        self.assertLess(np.max(abs(abs(response[frequencies <= 330]) - 1)), 1e-4)
        # Public widest HF center plus channel spread and maximum campaign CFO.
        probes = np.asarray([202.5, 292.5])
        _, reference = signal.freqz(taps, worN=probes, fs=18000)
        for offset in (-.58, .58):
            _, shifted = signal.freqz(taps, worN=probes + offset, fs=18000)
            # Remove linear-phase constant (absorbed in each fitted ray gain).
            corrected = shifted * np.exp(2j * np.pi * offset * (len(taps) - 1) / 36000)
            self.assertLess(np.max(abs(corrected - reference)), 1e-4)

    def test_continuous_whole_capture_filter_and_complete_tail(self):
        templates = source_templates(rows=1800)
        rx = receiver(templates, delay_seconds=.021, doppler_hz=90., frequency_offsets=(0.,))
        raw = np.zeros(rx.capture_rows, complex)
        raw[rx.crop_start_raw - 10] = 1
        prepared = rx.prepare_observation(raw)
        full = signal.upfirdn(rx.taps, raw, down=20)
        np.testing.assert_array_equal(prepared.samples,
                                      full[rx.crop_start_output:rx.crop_start_output + rx.samples])
        self.assertGreater(abs(prepared.samples[0]), 0.)
        for child in rx._receivers:
            for _, stop in child._workspace.support:
                self.assertLessEqual(stop + rx.lags[-1], rx.samples)
        self.assertEqual(rx.lags[-1], 18)
        np.testing.assert_array_equal(rx.lags, np.arange(19))
        coarse = receiver(templates, delay_seconds=.021, doppler_hz=90.,
                          frequency_offsets=(0.,), lag_step=4)
        # Explicit historical grids still include the non-step-aligned endpoint.
        np.testing.assert_array_equal(coarse.lags, [0, 4, 8, 12, 16, 18])
        self.assertNotEqual(coarse.receiver_id, rx.receiver_id)
        self.assertGreater(rx.template_energy[0][0][0]["ratio"], .99)

    def test_integer_and_fractional_delay_finite_support(self):
        values = np.asarray([1 + 2j, 3 - 4j, 5 + 6j])
        for delay in (9, 378):
            delayed = fractional_delay(values, delay)
            np.testing.assert_array_equal(delayed[:delay], 0)
            np.testing.assert_array_equal(delayed[delay:], values)
        delayed = fractional_delay(values, 2.5)
        self.assertEqual(len(delayed), len(values) + 2 + 3)
        np.testing.assert_allclose(delayed.sum(), values.sum(), atol=1e-14)
        self.assertGreater(abs(delayed[-1]), 0.)

    def test_known_gaps_exclude_complete_fir_support_and_keep_retained_samples_exact(self):
        templates = source_templates(rows=1800)
        rng = np.random.default_rng(89321)
        raw = rng.normal(size=18000) + 1j * rng.normal(size=18000)
        for profile in ("legacy", "wide"):
            options = dict(fft_backend="batched", frontend_profile=profile,
                           frequency_offsets=(0.,))
            complete = receiver(templates, **options)
            reference = complete.prepare_observation(raw)
            for length in (180, 7200):  # 10 ms and a complete 400 ms dwell.
                valid = np.ones(len(raw), np.uint8)
                valid[7200:7200 + length] = 0
                masked = receiver(templates, capture_validity=valid, **options)
                damaged = raw.copy()
                damaged[valid == 0] = np.nan  # Unknown input values are ignored.
                prepared = masked.prepare_observation(damaged, valid=valid)
                contamination = signal.upfirdn(np.ones(len(masked.taps)), 1 - valid, down=20)
                expected = contamination[masked.crop_start_output:masked.crop_start_output + masked.samples] == 0
                np.testing.assert_array_equal(masked.output_validity, expected)
                np.testing.assert_array_equal(prepared.samples[expected], reference.samples[expected])
                np.testing.assert_array_equal(prepared.samples[~expected], 0)
                metadata = masked.preprocessing["erasure_support"]
                self.assertEqual(metadata["raw_missing_rows"], length)
                self.assertEqual(metadata["output_missing_rows"], int((~expected).sum()))
                self.assertGreater(metadata["output_missing_rows"], length // 20)
                self.assertEqual(prepared.validity_id, masked.validity_id)
                self.assertFalse(masked.capture_validity.flags.writeable)
                self.assertFalse(masked.output_validity.flags.writeable)
                self.assertGreater(masked.validity_array_bound_bytes, valid.nbytes + expected.nbytes)
                valid[:] = 1  # The caller cannot mutate the frozen mask.
                self.assertEqual(int((masked.capture_validity == 0).sum()), length)
                with self.assertRaisesRegex(ValueError, "fixed capture mask"):
                    masked.prepare_observation(raw, valid=valid)
                with self.assertRaisesRegex(ValueError, "preprocessing identity"):
                    masked.score_prepared(replace(prepared, validity_id="different-mask"))

    def test_explicit_all_valid_is_identical_and_masks_are_fixed(self):
        templates = source_templates(rows=1800)
        for backend in ("legacy", "batched"):
            omitted = receiver(templates, fft_backend=backend)
            explicit = receiver(templates, fft_backend=backend,
                                capture_validity=np.ones(18000, np.uint8))
            raw = capture(templates, omitted, bit=1)
            a, b = omitted.prepare_observation(raw), explicit.prepare_observation(raw)
            self.assertIsNone(explicit.capture_validity)
            self.assertIsNone(explicit.output_validity)
            self.assertEqual(explicit.validity_array_bound_bytes, 0)
            self.assertEqual(omitted.receiver_id, explicit.receiver_id)
            np.testing.assert_array_equal(a.samples, b.samples)
            np.testing.assert_array_equal(omitted.score_prepared(a)["bit_log_likelihoods"],
                                          explicit.score_prepared(b)["bit_log_likelihoods"])
            changed = np.ones(18000, np.uint8)
            changed[6000] = 0
            with self.assertRaisesRegex(ValueError, "fixed capture mask"):
                explicit.prepare_observation(raw, valid=changed)
        for mask in (np.ones(18000, float), np.ones(17999, np.uint8),
                     np.full(18000, 2, np.uint8), np.zeros(18000, np.uint8)):
            with self.assertRaises(ValueError):
                receiver(templates, capture_validity=mask)

    def test_invalid_geometry_memory_and_erasures(self):
        templates = source_templates(rows=1800)
        for options in (dict(arrival_bounds_seconds=(1., -1.)),
                        dict(arrival_bounds_seconds=(-1., 0.)),
                        dict(capture_rows=6000), dict(reference_variance=0.),
                        dict(sample_rate=900), dict(delay_seconds=-.1),
                        dict(frequency_offsets=(206.,))):
            with self.subTest(options=options), self.assertRaises(ValueError):
                receiver(templates, **options)
        with self.assertRaises(MemoryError):
            receiver(templates, memory_limit_bytes=65536)
        with self.assertRaises(ValueError):
            receiver(templates.real)
        bad = templates.copy(); bad[0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            receiver(bad)
        rx = receiver(templates)
        raw = np.zeros(rx.capture_rows, complex)
        with self.assertRaises(ValueError):
            rx.score(raw[:-1])
        mask = np.ones(rx.capture_rows, np.uint8); mask[123] = 0
        with self.assertRaisesRegex(ValueError, "fixed capture mask"):
            rx.score(raw, valid=mask)
        other = receiver(templates, source_offset_seconds=.21)
        with self.assertRaisesRegex(ValueError, "preprocessing identity"):
            other.score_prepared(rx.prepare_observation(raw))


if __name__ == "__main__":
    unittest.main()

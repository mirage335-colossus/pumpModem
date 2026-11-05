#!/usr/bin/env python3
"""Cheap distinct-clock checks for the isolated rate-aware Helper frontend."""
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import signal
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_helper_receiver import (HelperDataReceiver, data_fir_taps,
                                    frontend_plan, rate_aware_frontend)


class RateAwareFrontendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_cheapest_divisor_and_explicit_rejections(self):
        for raw, edge, output in ((6000, 300., 750), (48000, 5500., 16000),
                                  (96000, 8000., 24000)):
            plan = rate_aware_frontend(raw, edge)
            self.assertEqual(plan["output_sample_rate"], output)
            self.assertEqual(raw % output, 0)
            self.assertGreaterEqual(plan["pass_edge_hz"], edge)
            # Exhaustive small integer-divisor reference independent of planner.
            options = [x for x in range(1, output + 1) if raw % x == 0 and .4 * x >= edge]
            self.assertEqual(output, min(options))
        for args in ((48000, 48000.), (True, 1.), (48000, float("nan"))):
            with self.assertRaises(ValueError):
                rate_aware_frontend(*args)
        with self.assertRaises(ValueError):
            frontend_plan("rate-aware", sample_rate=48000, output_sample_rate=7000)
        with self.assertRaisesRegex(ValueError, "18000"):
            frontend_plan("wide", sample_rate=48000, output_sample_rate=16000)

    def test_fir_response_and_legacy_coefficients(self):
        for profile, raw, output in (("legacy", 18000, 900), ("wide", 18000, 900),
                                     ("rate-aware", 6000, 1200), ("rate-aware", 96000, 24000)):
            plan = frontend_plan(profile, sample_rate=raw, output_sample_rate=output)
            taps = data_fir_taps(profile, sample_rate=raw, output_sample_rate=output)
            f, h = signal.freqz(taps, worN=131072, fs=raw)
            self.assertLess(np.max(abs(abs(h[f <= plan["pass_edge_hz"]]) - 1)), 1e-4)
            self.assertGreater(-20 * np.log10(max(abs(h[f >= plan["stop_edge_hz"]]))), 80.)
            self.assertEqual(len(taps) % 2, 1)
        for profile, edges in (("legacy", (330., 430.)), ("wide", (400., 440.))):
            count, beta = signal.kaiserord(96., (edges[1] - edges[0]) / 9000)
            count += 1 - count % 2
            old = signal.firwin(count, sum(edges) / 2, window=("kaiser", beta), fs=18000)
            np.testing.assert_array_equal(old, data_fir_taps(profile))

    def test_helper_rejection_is_independent_of_integer_output_clock(self):
        # Same public 90-Hz ray separation/fast modulation envelope as the HF
        # stress family. The old 0.4*output passband retained these high pilots.
        for raw, width, center, high in ((48000, 2250, 3600, 5175),
                                         (48000, 7500, 7800, 12000),
                                         (96000, 11250, 10800, 16875),
                                         (96000, 15000, 13800, 21750)):
            data_edge = width / 2 + 180
            rejected = [(high - center - 180, high - center + 180),
                        (550 - center - 180, 550 - center + 180),
                        (950 - center - 380, 950 - center + 380)]
            plan = rate_aware_frontend(raw, data_edge, rejected_bands_hz=rejected)
            self.assertEqual(plan["pass_edge_hz"], data_edge)
            self.assertLessEqual(plan["stop_edge_hz"], high - center - 180)
            taps = data_fir_taps("rate-aware", sample_rate=raw,
                output_sample_rate=plan["output_sample_rate"],
                pass_edge_hz=plan["pass_edge_hz"], stop_edge_hz=plan["stop_edge_hz"])
            frequencies = np.asarray([data_edge, high - center - 180, high - center])
            _, response = signal.freqz(taps, worN=frequencies, fs=raw)
            self.assertLess(abs(abs(response[0]) - 1), 1e-4)
            self.assertTrue(np.all(-20 * np.log10(abs(response[1:])) > 80))
        for bands in (((-10, 10),), ((300, 400),), ((-400, -300),)):
            with self.assertRaisesRegex(ValueError, "overlaps"):
                rate_aware_frontend(6000, 300, rejected_bands_hz=bands)
        with self.assertRaisesRegex(ValueError, "together"):
            frontend_plan("rate-aware", sample_rate=6000, output_sample_rate=1200,
                          pass_edge_hz=300)
        with self.assertRaisesRegex(ValueError, "Nyquist"):
            frontend_plan("rate-aware", sample_rate=6000, output_sample_rate=1200,
                          pass_edge_hz=300, stop_edge_hz=600)

    def test_both_backends_with_high_band_independent_injection(self):
        raw, output, rows = 48000, 16000, 4800
        t = np.arange(rows) / raw
        envelope = np.sin(np.pi * np.arange(rows) / (rows - 1))**2
        templates = np.asarray([[envelope * (1 + .2 * np.cos(2 * np.pi * 137 * t)) *
                                  np.exp(2j * np.pi * f * t) for f in (-4500, 5100)]])
        for backend in ("legacy", "batched"):
            rx = HelperDataReceiver(templates, (0,), source_offset_seconds=.2,
                capture_lead_seconds=.1, capture_rows=24000,
                arrival_bounds_seconds=(0., .002), delay_seconds=.0005, doppler_hz=30.,
                source_half_bandwidth_hz=5500., reference_variance=.002,
                frequency_offsets=(-8., 0., 8.), sample_rate=raw, output_sample_rate=output,
                frontend_profile="rate-aware", fft_backend=backend, precision="fp64",
                pass_edge_hz=5600., stop_edge_hz=7000.,
                memory_limit_bytes=256 * 1024**2)
            for bit, lag, cfo in ((0, 5, -8.), (1, 13, 8.)):
                noise = np.random.default_rng(613 + bit)
                observation = .05 * (noise.normal(size=rx.capture_rows) +
                                      1j * noise.normal(size=rx.capture_rows))
                direct = np.zeros(rx.capture_rows, complex)
                delayed = direct.copy()
                start = round(.3 * raw) + lag * (raw // output)
                direct[start:start + rows] = templates[0, bit]
                delayed[start + 24:start + 24 + rows] = templates[0, bit]
                times = np.arange(rx.capture_rows) / raw - .1
                observation += ((1.2 + .3j) * direct + (.6 - .4j) * delayed *
                                np.exp(2j * np.pi * 30 * times)) * np.exp(2j * np.pi * cfo * times)
                score = rx.score(observation)
                self.assertEqual(score["bit"], bit)
                self.assertEqual(score["preprocessing"]["decimation"], 3)
                self.assertEqual(score["preprocessing"]["output_sample_rate"], output)
                energy = (score["template_energy"][0]["bits"] if backend == "batched"
                          else rx.template_energy[0])
                self.assertGreater(energy[0][0]["ratio"], .999)

    def test_dynamic_known_gap_support_matches_direct_filter(self):
        raw, output = 6000, 1200
        source = np.exp(2j * np.pi * np.arange(300) / 40)
        templates = np.asarray([[source, source.conj()]])
        valid = np.ones(6000, np.uint8)
        valid[2000:2010] = 0
        rx = HelperDataReceiver(templates, (0,), source_offset_seconds=.2,
            capture_lead_seconds=.1, capture_rows=len(valid), arrival_bounds_seconds=(0., .01),
            delay_seconds=0., doppler_hz=0., source_half_bandwidth_hz=200.,
            reference_variance=.01, sample_rate=raw, output_sample_rate=output,
            frontend_profile="rate-aware", capture_validity=valid,
            memory_limit_bytes=64 * 1024**2)
        observation = np.random.default_rng(73).normal(size=6000).astype(complex)
        observation[valid == 0] = 0
        prepared = rx.prepare_observation(observation, valid=valid)
        expected = signal.upfirdn(rx.taps, observation, down=5)[
            rx.crop_start_output:rx.crop_start_output + rx.samples]
        expected[rx.output_validity == 0] = 0
        np.testing.assert_allclose(prepared.samples, expected, atol=1e-12, rtol=1e-12)
        np.testing.assert_array_equal(prepared.samples[rx.output_validity == 0], 0)
        for j, is_valid in enumerate(rx.output_validity):
            index = rx.crop_start_raw + 5 * j
            expected_valid = np.all(valid[max(0, index - len(rx.taps) + 1):index + 1])
            self.assertEqual(bool(is_valid), expected_valid)

    def test_overlap_add_preserves_full_tail_and_decimation_phase(self):
        rx = object.__new__(HelperDataReceiver)
        rx.taps = data_fir_taps("rate-aware", sample_rate=96000, output_sample_rate=19200,
                                pass_edge_hz=7680., stop_edge_hz=7860.)
        rx.decimation = 5
        rx._overlap_add_eligible = True
        for rows in (777, 4 * len(rx.taps), 110003):
            rng = np.random.default_rng(rows)
            values = rng.normal(size=rows) + 1j * rng.normal(size=rows)
            # Explicit endpoint impulses check full causal tails and a
            # non-divisible input length, independently of random energy.
            values[0], values[-1] = 10 + 3j, -7 + 2j
            expected = signal.upfirdn(rx.taps, values, down=rx.decimation)
            actual = rx._filter(values)
            self.assertEqual(len(actual), len(expected))
            np.testing.assert_allclose(actual, expected, atol=1e-12, rtol=1e-12)
            self.assertIsNone(actual.base)
            if rows >= 4 * len(rx.taps):
                self.assertGreater(rx._filter_scratch_bytes(rows), 0)
            else:
                self.assertEqual(rx._filter_scratch_bytes(rows), 0)


if __name__ == "__main__":
    unittest.main()

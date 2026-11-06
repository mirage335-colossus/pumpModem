#!/usr/bin/env python3
"""Small independent covariance/path references for streaming timing DSP."""
import math
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_incremental_receiver import IncrementalReceiver
from triad_timing_forward import SparseTimingForward, TimingState
from triad_tracking_receiver import TrackingReceiver


class TrackingReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")

    def prior(self, *, models=2, states=None, **kw):
        return SparseTimingForward(states or [TimingState(0, 0, 0)],
            model_count=models, model_probabilities=[.3, .7] if models == 2 else None,
            delay_step_seconds=.0001, rate_step_ppm=50, maximum_rate_ppm=50,
            maximum_acceleration_ppm_per_second=50,
            maximum_states=128, maximum_proposals=128, **kw)

    def make(self, provider, *, samples=32, segment=16, block=8, prior=None,
             projection_mode="valid-rows", **kw):
        return TrackingReceiver(sample_rate=16, samples=samples,
            block_samples=block, segment_samples=segment, basis_provider=provider,
            timing_forward=prior or self.prior(), required_bands=[[-7, 7]],
            noise_variance=1.7, stationary_power=(.8,), coherence_seconds=(.6, math.inf),
            precision="fp64", projection_mode=projection_mode,
            maximum_input_chunk_samples=128, memory_limit_bytes=16*1024**2, **kw)

    @staticmethod
    def provider(states, anchor, first, stop, origin, offsets):
        # Three separate complex carriers and independently selected bit
        # codewords. This is an analytic test source, not the native modem.
        u = (origin+offsets)/16
        result = np.zeros((3, len(states), 1, stop-first), np.complex128)
        coefficients = ((.5+.2j, .4-.3j, .2+.1j), (.2-.5j, -.3+.1j, .5+.2j))
        for column, state in enumerate(states):
            source = u + state.delay_index*.0001 + state.rate_index*50e-6*(u-float(anchor))
            center = np.exp(2j*np.pi*3*(source-u))
            for bit in (0, 1):
                result[bit+1, column, 0] = sum(c*np.exp(2j*np.pi*f*source)
                    for c, f in zip(coefficients[bit], (-3, 1, 4))) * center
        return result

    @staticmethod
    def covariance_score(values, basis, offsets, coherence, fs=16, block=8):
        """Dense proper-complex Gaussian covariance, independent of filter."""
        count = len(values)
        bounds = [(i, min(i+block, count)) for i in range(0, count, block)]
        # These fixtures have complete blocks; the separate engine tests cover
        # its declared short-terminal-block merge convention.
        columns = np.zeros((count, len(bounds)), np.complex128)
        centers = []
        for index, (first, stop) in enumerate(bounds):
            columns[first:stop, index] = basis[first:stop]
            centers.append((offsets[first]+offsets[stop-1])/(2*fs))
        time = np.asarray(centers)
        channel_covariance = (.8*np.ones((len(time), len(time))) if math.isinf(coherence) else
            .8*np.exp(-np.abs(time[:, None]-time[None, :])/coherence))
        covariance = np.eye(count) + columns@channel_covariance@columns.conj().T/1.7
        whitened = values/math.sqrt(1.7)
        determinant_sign, logdet = np.linalg.slogdet(covariance)
        assert abs(determinant_sign-1) < 1e-12
        return float((whitened.conj()@whitened -
            whitened.conj()@np.linalg.solve(covariance, whitened)).real-logdet)

    def test_two_segment_dense_covariance_and_explicit_path_sum(self):
        rng = np.random.default_rng(81891)
        n = np.arange(32, dtype=float)
        mapping = n + .0004*n*n  # exact known local map, deliberately nonlinear
        values = (rng.normal(size=32)+1j*rng.normal(size=32))*.4
        receiver = self.make(self.provider, require_physical_map=True)
        for first in range(0, 32, 7):
            stop = min(first+7, 32)
            receiver.push(first, values[first:stop], physical_origin=first,
                physical_offsets=mapping[first:stop]-first)
        result = receiver.finish()
        self.assertTrue(result["complete_observation"])
        self.assertIsNone(result["admission"])

        centers = [(mapping[0]+mapping[15])/32, (mapping[16]+mapping[31])/32]
        dt = centers[1]-centers[0]
        initial = TimingState(0, 0, 0)
        b0 = self.provider((initial,), centers[0], 0, 16, 0, mapping[:16])[1:, 0, 0]
        prior_models = (.3, .7)
        expected = np.zeros(2)
        for mi, coherence in enumerate((.6, math.inf)):
            first_log = [self.covariance_score(values[:16], b0[bit], mapping[:16], coherence)
                         for bit in (0, 1)]
            # Independent explicit path enumeration, not SparseTimingForward.
            radius = min(2, math.floor(math.nextafter(50*dt/50, math.inf)))
            rates = range(max(-1, -radius), min(1, radius)+1)
            for rate in rates:
                displacement = .5*rate*50e-6*dt/.0001
                lower = math.floor(displacement)
                fraction = displacement-lower
                for delay, split in ((lower, 1-fraction), (lower+1, fraction)):
                    if not split:
                        continue
                    state = TimingState(0, delay, rate)
                    b1 = self.provider((state,), centers[1], 16, 32, 0, mapping[16:])[1:, 0, 0]
                    for bit in (0, 1):
                        second_log = self.covariance_score(values[16:], b1[bit], mapping[16:], coherence)
                        expected[bit] += prior_models[mi]*split/len(rates)*math.exp(first_log[bit]+second_log)
        np.testing.assert_allclose(result["bit_scores"], np.log(expected), rtol=0, atol=4e-12)
        self.assertEqual(result["segments"], 2)
        self.assertEqual(result["mapped_samples"], 32)

    def test_chunking_does_not_change_mapped_likelihood_or_global_pattern_indices(self):
        rng = np.random.default_rng(81892)
        values = rng.normal(size=53)+1j*rng.normal(size=53)
        n = np.arange(len(values), dtype=float)
        positions = n+.03*np.sin(n/10)
        answers = []
        for chunk in (5, 23, 53):
            seen = []
            def provider(states, anchor, lo, hi, origin, offsets):
                seen.append((lo, hi))
                np.testing.assert_allclose((origin+offsets), positions[lo:hi], atol=1e-14, rtol=0)
                return self.provider(states, anchor, lo, hi, origin, offsets)
            receiver = self.make(provider, samples=len(values), require_physical_map=True)
            for first in range(0, len(values), chunk):
                stop = min(first+chunk, len(values))
                origin = math.floor(positions[first])
                receiver.push(first, values[first:stop], physical_origin=origin,
                    physical_offsets=positions[first:stop]-origin)
            result = receiver.finish()
            self.assertEqual(seen, [(0, 8), (8, 16), (16, 24), (24, 32), (32, 40), (40, 48), (48, 53)])
            self.assertEqual(result["processed_rows"], len(values))
            self.assertLessEqual(result["peak_pending_rows"], 16)
            answers.append(result["bit_scores"])
        np.testing.assert_allclose(answers, np.repeat([answers[0]], len(answers), axis=0), rtol=0, atol=1e-12)

    def test_large_map_origin_preserves_fractional_coordinates(self):
        base = 2**54+137
        n = np.arange(33, dtype=float)
        offsets = n + .125 + .001*n
        calls = []
        def provider(states, anchor, first, stop, origin, relative):
            calls.append((first, stop))
            # Absolute-double addition would destroy .125 and .001 here.
            np.testing.assert_allclose((origin-base)+relative, offsets[first:stop], rtol=0, atol=1e-14)
            self.assertIsInstance(origin, int)
            return np.zeros((3, len(states), 1, stop-first), np.complex128)
        receiver = self.make(provider, samples=33, require_physical_map=True)
        for first in range(0, 33, 7):
            stop = min(first+7, 33)
            receiver.push(first, np.zeros(stop-first), physical_origin=base+first,
                physical_offsets=offsets[first:stop]-first)
        result = receiver.finish()
        self.assertTrue(result["complete_observation"])
        self.assertEqual(calls[-1], (32, 33))
        self.assertAlmostEqual(result["timing"]["elapsed_seconds"],
            ((offsets[32]+offsets[32])-(offsets[0]+offsets[15]))/32)

    def test_erased_segments_add_unit_emissions_and_advance_timing_uncertainty(self):
        def forbidden(*args):
            raise AssertionError("fully erased observations must not synthesize templates")
        receiver = self.make(forbidden)
        receiver.push(0, np.ones(32), np.zeros(32, dtype=np.uint8))
        result = receiver.finish()
        np.testing.assert_allclose(result["bit_scores"], 0, atol=5e-16, rtol=0)
        self.assertEqual(result["erased_rows"], 32)
        self.assertEqual(result["timing"]["predictions"], 1)
        self.assertGreater(result["timing"]["state_count"], 1)

    def test_dft_partial_erasure_matches_explicit_actual_block_erasure(self):
        rng = np.random.default_rng(81894)
        values = rng.normal(size=44)+1j*rng.normal(size=44)
        mask = np.ones(44, np.uint8)
        mask[[6, 18, 35, 40]] = 0
        # Segment sizes 19,19,6 give blocks [0,8),[8,19),[19,27),
        # [27,38),[38,44): the last two segments exercise both terminal cases.
        explicit = np.zeros(44, np.uint8)
        explicit[19:27] = 1
        reports = []
        for chunk, used_mask in ((5, mask), (44, mask), (44, explicit)):
            called = []
            def basis(states, anchor, lo, hi, origin, offsets):
                called.append((lo, hi))
                return self.provider(states, anchor, lo, hi, origin, offsets)
            receiver = self.make(basis, samples=44, segment=19, projection_mode="dft")
            for first in range(0, 44, chunk):
                receiver.push(first, values[first:first+chunk], used_mask[first:first+chunk])
            reports.append(receiver.finish())
            self.assertEqual(called, [(19, 27)])
        for report in reports[:2]:
            np.testing.assert_array_equal(report["bit_scores"], reports[2]["bit_scores"])
            self.assertEqual(report["requested_invalid_rows"], 4)
            self.assertEqual(report["processed_requested_invalid_rows"], 4)
            self.assertEqual(report["additional_discarded_rows"], 32)
            self.assertEqual(report["expanded_erasure_blocks"], 4)
            self.assertEqual(report["observed_rows"], 8)
            self.assertEqual(report["erased_rows"], 36)
            self.assertEqual(report["blocks"], 5)
        self.assertEqual(reports[2]["additional_discarded_rows"], 0)
        self.assertEqual(reports[2]["expanded_erasure_blocks"], 0)

    def test_dft_expanded_fully_erased_segments_have_unit_likelihood(self):
        def forbidden(*args):
            raise AssertionError("erased DFT blocks must not synthesize templates")
        mask = np.ones(32, np.uint8)
        mask[[0, 9, 17, 31]] = 0
        receiver = self.make(forbidden, projection_mode="dft")
        receiver.push(0, np.full(32, 1e20), mask)
        result = receiver.finish()
        np.testing.assert_allclose(result["bit_scores"], 0, atol=5e-16, rtol=0)
        self.assertEqual(result["requested_invalid_rows"], 4)
        self.assertEqual(result["additional_discarded_rows"], 28)
        self.assertEqual(result["expanded_erasure_blocks"], 4)
        self.assertEqual(result["observed_rows"], 0)
        self.assertEqual(result["timing"]["predictions"], 1)

    def test_valid_rows_do_not_expand_erasure_and_direct_dft_still_rejects(self):
        rng = np.random.default_rng(81895)
        values = rng.normal(size=32)+1j*rng.normal(size=32)
        mask = np.ones(32, np.uint8)
        mask[[6, 18]] = 0
        changed = values.copy()
        changed[mask == 0] = 1e20
        reports = []
        for data in (values, changed):
            receiver = self.make(self.provider)
            receiver.push(0, data, mask)
            reports.append(receiver.finish())
        np.testing.assert_array_equal(reports[0]["bit_scores"], reports[1]["bit_scores"])
        self.assertEqual(reports[0]["observed_rows"], 30)
        self.assertEqual(reports[0]["requested_invalid_rows"], 2)
        self.assertEqual(reports[0]["additional_discarded_rows"], 0)
        self.assertEqual(reports[0]["expanded_erasure_blocks"], 0)
        direct = IncrementalReceiver(sample_rate=16, samples=16, block_samples=8,
            basis=lambda lo, hi: np.zeros((3, 1, 1, hi-lo)),
            required_bands=[[-7, 7]], stationary_power=(.8,), projection_mode="dft")
        with self.assertRaisesRegex(ValueError, "partial erasures"):
            direct.push(0, values[:16], mask[:16])

    def test_prefix_never_admits_and_incomplete_finish_can_continue(self):
        receiver = self.make(self.provider)
        receiver.push(0, np.zeros(20))
        report = receiver.finish()
        self.assertEqual(report["status"], "incomplete")
        self.assertFalse(report["complete_observation"])
        self.assertIsNone(report["admission"])
        self.assertEqual(report["pending_rows"], 4)
        receiver.push(20, np.zeros(12))
        self.assertTrue(receiver.finish()["complete_observation"])
        with self.assertRaisesRegex(RuntimeError, "completed finish"):
            receiver.push(32, np.array([]))

    def test_missing_discontinuous_or_invalid_maps_fail_before_consumption(self):
        receiver = self.make(self.provider, require_physical_map=True)
        for kwargs in ({}, {"physical_origin": 0},
                       {"physical_origin": 0, "physical_offsets": [0, 0]},
                       {"physical_origin": 0, "physical_offsets": [0, math.nan]},
                       {"physical_origin": 0, "physical_offsets": [-1, 0]},
                       {"physical_origin": 0, "physical_offsets": [2**53, 2**53+2]}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                receiver.push(0, np.zeros(2), **kwargs)
            self.assertEqual(receiver.next_input_sample, 0)
        receiver.push(0, np.zeros(2), physical_origin=0, physical_offsets=[0, 1])
        with self.assertRaisesRegex(ValueError, "discontinuous"):
            receiver.push(2, np.zeros(2), physical_origin=5, physical_offsets=[0, 1])
        self.assertEqual(receiver.next_input_sample, 2)
        optional = self.make(self.provider)
        optional.push(0, np.zeros(2), physical_origin=0, physical_offsets=[0, 1])
        with self.assertRaisesRegex(ValueError, "omitted"):
            optional.push(2, np.zeros(2))

    def test_nonzero_common_null_and_failed_segment_cannot_be_reused(self):
        def wrong(states, anchor, first, stop, origin, offsets):
            return np.ones((3, len(states), 1, stop-first), np.complex128)
        receiver = self.make(wrong, samples=16)
        with self.assertRaisesRegex(ValueError, "zero H0"):
            receiver.push(0, np.ones(16))
        self.assertEqual(receiver.finish()["status"], "failed")
        with self.assertRaisesRegex(RuntimeError, "failure"):
            receiver.push(16, np.array([]))

    def test_long_calendar_preflight_is_bounded_and_does_not_build_templates(self):
        def forbidden(*args):
            raise AssertionError("constructor synthesized templates")
        receiver = self.make(forbidden, samples=2**54+1, segment=32)
        self.assertEqual(receiver.segment_capacity, 32)
        self.assertLess(receiver.owned_bytes_estimate, 16*1024**2)
        self.assertEqual(receiver.finish()["status"], "incomplete")
        with self.assertRaisesRegex(MemoryError, "bounded sample storage"):
            self.make(forbidden, samples=2**54, segment=2**40)

    def test_physical_block_time_is_optional_and_default_equivalent(self):
        rng = np.random.default_rng(81893)
        n = 24
        data = rng.normal(size=n)+1j*rng.normal(size=n)
        basis = np.zeros((3, 2, 1, n), np.complex128)
        basis[1:] = rng.normal(size=(2, 2, 1, n))+1j*rng.normal(size=(2, 2, 1, n))
        options = dict(sample_rate=16, samples=n, block_samples=8,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=[[-7, 7]],
            stationary_power=(.8,), coherence_seconds=(.6, math.inf),
            candidate_count=2, precision="fp64", projection_mode="valid-rows")
        a = IncrementalReceiver(**options)
        b = IncrementalReceiver(**options, block_center_seconds=lambda lo, hi: (lo+hi-1)/32)
        a.push(0, data); b.push(0, data)
        np.testing.assert_array_equal(a.unmixed_log_evidence(), b.unmixed_log_evidence())
        self.assertEqual(a.unmixed_log_evidence().shape, (2, 3, 2))
        np.testing.assert_array_equal(a.finish()["bit_scores"], b.finish()["bit_scores"])
        c = IncrementalReceiver(**options, block_center_seconds=lambda lo, hi: 0.)
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            c.push(0, data)


if __name__ == "__main__":
    unittest.main()

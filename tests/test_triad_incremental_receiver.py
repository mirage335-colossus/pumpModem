#!/usr/bin/env python3
"""Focused batch-equivalence checks for bounded incremental likelihood."""
import math
from pathlib import Path
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from unittest import mock

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_dynamic_joint import ProjectedJointReceiver
from triad_incremental_receiver import IncrementalGaussianReceiver
from triad_cpu import configure_numerical_threads, initialize_numerical_worker


class IncrementalReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")

    def fixture(self, n=29, block=8, projection="valid-rows"):
        rng = np.random.default_rng(19031+n+block)
        basis = (rng.normal(size=(3, 2, 2, n)) + 1j*rng.normal(size=(3, 2, 2, n))) * .08
        basis[0] *= .03
        raw = rng.normal(size=n) + 1j*rng.normal(size=n)
        valid = np.ones(n, dtype=np.uint8)
        fs = 32.
        kwargs = dict(sample_rate=fs, samples=n, block_samples=block,
            basis=lambda lo, hi: basis[..., lo:hi], required_bands=((-15., 15.),),
            noise_variance=1.7, stationary_power=(.4, .6), coherence_seconds=(.3, math.inf),
            precision="fp64", candidate_count=2, projection_mode=projection,
            memory_limit_bytes=8*1024*1024)
        if projection == "valid-rows":
            valid[3] = 0
            valid[17] = 0
        return raw, valid, kwargs

    def compare(self, projection, chunk_size):
        raw, valid, kwargs = self.fixture(projection=projection)
        batch = ProjectedJointReceiver(**kwargs, valid=valid)
        expected = batch.score(raw)
        stream = IncrementalGaussianReceiver(**kwargs)
        cursor = 0
        while cursor < len(raw):
            stop = min(cursor+chunk_size, len(raw))
            stream.push(cursor, raw[cursor:stop], valid[cursor:stop])
            cursor = stop
        actual = stream.finish()
        self.assertEqual(actual["status"], "scored")
        self.assertTrue(actual["complete_observation"])
        self.assertIsNone(actual["admission"])
        np.testing.assert_allclose(actual["hypothesis_log_evidence"],
            expected["hypothesis_log_evidence"][:, 0], rtol=2e-10, atol=2e-10)
        np.testing.assert_allclose(actual["bit_scores"], expected["bit_scores"][0],
            rtol=2e-10, atol=2e-10)
        self.assertEqual(actual["blocks"], len(batch.blocks))
        self.assertEqual(actual["processed_rows"], len(raw))

    def test_batch_equivalence_raw_rows_two_chunk_sizes_and_short_tail_merge(self):
        self.compare("valid-rows", 5)
        self.compare("valid-rows", 13)

    def test_parallel_global_model_updates_preserve_scores_posteriors_and_noise_statistic(self):
        allocation = {'threads': 1, 'linear_algebra_threads': 1}
        configure_numerical_threads(allocation)
        raw, valid, kwargs = self.fixture(projection='valid-rows')
        serial = IncrementalGaussianReceiver(**kwargs)
        serial.push(0, raw, valid)
        expected = serial.finish()
        with ThreadPoolExecutor(max_workers=4, initializer=initialize_numerical_worker,
                                initargs=(allocation,)) as executor:
            parallel = IncrementalGaussianReceiver(**kwargs, hypothesis_executor=executor,
                                                   hypothesis_shards=2)
            parallel.push(0, raw, valid)
            actual = parallel.finish()
        np.testing.assert_allclose(actual['bit_scores'], expected['bit_scores'], rtol=0, atol=1e-12)
        self.assertEqual(actual['observed_projection_modes'], int(valid.sum()))
        self.assertAlmostEqual(actual['projected_energy'],
            float(np.vdot(raw[valid.astype(bool)], raw[valid.astype(bool)]).real/kwargs['noise_variance']))
        for first, second in zip(serial.runtimes, parallel.runtimes):
            for left, right in zip(first.posterior(), second.posterior()):
                np.testing.assert_allclose(left.numpy(), right.numpy(), rtol=0, atol=1e-13)
        self.assertGreater(parallel.owned_bytes_estimate, serial.owned_bytes_estimate)

    def test_partial_executor_submission_joins_already_started_model_jobs(self):
        raw, valid, kwargs = self.fixture()
        future = mock.Mock()
        executor = mock.Mock()
        executor.submit.side_effect = [future, RuntimeError('executor stopped')]
        receiver = IncrementalGaussianReceiver(**kwargs, hypothesis_executor=executor)
        with self.assertRaisesRegex(RuntimeError, 'executor stopped'):
            receiver.push(0, raw, valid)
        future.result.assert_called_once_with()
        self.assertEqual(receiver.block_updates, 0)

    def test_quiet_aperture_uses_complete_blocks_without_changing_likelihood(self):
        raw, valid, kwargs = self.fixture(n=29, block=8, projection='dft')
        raw[:8] += 1e3  # Previous-slot tail in the public search-window prefix.
        full = IncrementalGaussianReceiver(**kwargs)
        clipped = IncrementalGaussianReceiver(**kwargs, quiet_sample_bounds=(9, 29))
        for receiver in (full, clipped):
            receiver.push(0, raw, valid)
        expected, actual = full.finish(), clipped.finish()
        np.testing.assert_array_equal(actual['bit_scores'], expected['bit_scores'])
        self.assertEqual(actual['processed_rows'], 29)
        self.assertEqual(actual['observed_projection_modes'], expected['observed_projection_modes'])
        self.assertEqual(actual['projected_energy'], expected['projected_energy'])
        # [0,8) and boundary-crossing [8,16) are excluded. The merged
        # terminal [16,29) contributes its original complete DFT projection.
        from triad_dynamic_joint import BlockProjection
        projection = BlockProjection.make(13, kwargs['sample_rate'], kwargs['required_bands'], ())
        y = projection.numpy(raw[16:29])
        self.assertEqual(actual['quiet_observed_projection_modes'], len(projection.indices))
        self.assertAlmostEqual(actual['quiet_projected_energy'],
            float(np.vdot(y, y).real/kwargs['noise_variance']))
        self.assertLess(actual['quiet_projected_energy'], actual['projected_energy']/100)

    def test_quiet_aperture_rejects_invalid_geometry_and_allows_empty(self):
        raw, valid, kwargs = self.fixture(projection='dft')
        for bounds in ((-1, 8), (9, 8), (0, 30), (False, 8), (0., 8)):
            with self.subTest(bounds=bounds), self.assertRaises(ValueError):
                IncrementalGaussianReceiver(**kwargs, quiet_sample_bounds=bounds)
        receiver = IncrementalGaussianReceiver(**kwargs, quiet_sample_bounds=(8, 8))
        receiver.push(0, raw, valid)
        self.assertEqual(receiver.finish()['quiet_observed_projection_modes'], 0)

    def test_batch_equivalence_dft_static_and_ou(self):
        raw, valid, kwargs = self.fixture(projection="dft")
        valid[:8] = 0  # A complete erased block advances state with zero evidence.
        batch = ProjectedJointReceiver(**kwargs, valid=valid)
        expected = batch.score(raw)
        stream = IncrementalGaussianReceiver(**kwargs)
        for cursor in range(0, len(raw), 7):
            stream.push(cursor, raw[cursor:cursor+7], valid[cursor:cursor+7])
        actual = stream.finish()
        self.assertEqual(actual["status"], "scored")
        np.testing.assert_allclose(actual["hypothesis_log_evidence"],
            expected["hypothesis_log_evidence"][:, 0], rtol=2e-10, atol=2e-10)
        np.testing.assert_allclose(actual["bit_scores"], expected["bit_scores"][0],
            rtol=2e-10, atol=2e-10)
        # Static and renewing coefficient hypotheses are both in the same
        # comparison; the fixed mixture is retained from sample zero.

    def test_prefix_is_diagnostic_and_early_end_is_incomplete(self):
        raw, valid, kwargs = self.fixture()
        receiver = IncrementalGaussianReceiver(**kwargs)
        receiver.push(0, raw[:16], valid[:16])
        prefix = receiver.progress()
        self.assertEqual(prefix["status"], "prefix_diagnostic")
        self.assertFalse(prefix["complete_observation"])
        self.assertIsNone(prefix["admission"])
        end = receiver.finish()
        self.assertEqual(end["status"], "incomplete")
        self.assertFalse(end["complete_observation"])
        self.assertIsNone(end["admission"])

    def test_long_declared_calendar_has_bounded_initialization_without_templates(self):
        called = []
        def forbidden(start, stop):
            called.append((start, stop))
            raise AssertionError("basis must not be synthesized during receiver preparation")
        receiver = IncrementalGaussianReceiver(sample_rate=32., samples=10**12,
            block_samples=64, basis=forbidden, required_bands=((-15., 15.),),
            candidate_count=1, projection_mode="valid-rows", memory_limit_bytes=1024*1024)
        self.assertEqual(receiver.block_count, 10**12//64)
        self.assertLess(receiver.owned_bytes_estimate, 1024*1024)
        self.assertEqual(called, [])
        self.assertEqual(receiver.finish()["status"], "incomplete")

    def test_short_observation_does_not_allocate_requested_huge_dft_block(self):
        raw, valid, kwargs = self.fixture(n=29, block=10**12, projection="dft")
        receiver = IncrementalGaussianReceiver(**kwargs)
        self.assertEqual(receiver.projection.samples, len(raw))
        receiver.push(0, raw)
        self.assertTrue(receiver.finish()["complete_observation"])

    def test_rejects_discontinuity_partial_dft_erasure_and_oversized_chunk(self):
        raw, valid, kwargs = self.fixture(projection="dft")
        receiver = IncrementalGaussianReceiver(**kwargs, maximum_input_chunk_samples=8)
        with self.assertRaisesRegex(ValueError, "contiguous"):
            receiver.push(1, raw[:1])
        mask = valid[:8].copy(); mask[0] = 0
        with self.assertRaisesRegex(ValueError, "partial erasures"):
            receiver.push(0, raw[:8], mask)
        another = IncrementalGaussianReceiver(**kwargs, maximum_input_chunk_samples=4)
        with self.assertRaisesRegex(ValueError, "bounded"):
            another.push(0, raw[:5])


if __name__ == "__main__":
    unittest.main()

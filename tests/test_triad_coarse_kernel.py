#!/usr/bin/env python3
"""Native coarse score/peak equivalence; no receiver throughput inference."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_coarse_kernel import CoarseRank2Kernel
from triad_realtime_fft import _quadratic_scores, _candidate_peak_indices
from test_triad_realtime_fft import screen, fixture, waveform

NATIVE = sys.argv.pop(1) if len(sys.argv) > 1 else None


class CoarseKernelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if NATIVE is None:
            raise RuntimeError('native exporter path is required')
        cls.kernel = CoarseRank2Kernel(NATIVE)

    def check(self, correlations, inverse, *, distance=3, maximum=2, variance=1.):
        reference = _quadratic_scores(correlations, inverse)/variance
        actual, indices, counts = self.kernel(correlations, inverse, variance, distance, maximum)
        np.testing.assert_allclose(actual, reference, rtol=4e-15, atol=1e-300)
        for i in range(len(actual)):
            expected = _candidate_peak_indices(reference[i], distance, maximum)
            selected = (indices[i, :counts[i]] if counts[i] >= 0 else
                        _candidate_peak_indices(reference[i], distance, maximum))
            np.testing.assert_array_equal(selected, expected)
        return actual, indices, counts

    def test_rank_two_float_inputs_strides_and_retained_order(self):
        rng = np.random.default_rng(913781)
        for dtype in (np.complex64, np.complex128):
            for size in (1, 2, 3, 7, 33, 259):
                raw = (rng.normal(size=(4, 2, size+11))+
                       1j*rng.normal(size=(4, 2, size+11))).astype(dtype)
                correlations = raw[..., :size]
                inverse = np.array([[1.2, .1+.3j], [.1-.3j, .9]], np.complex128)
                for distance in (1, 3, 20):
                    for maximum in (1, 2, 4):
                        self.check(correlations, inverse, distance=distance, maximum=maximum,
                                   variance=.37)

    def test_plateaus_ties_endpoints_and_distance_are_canonical(self):
        patterns = [[0, 1, 0], [1, 0, 1], [0, 2, 2, 0], [2, 2, 2, 2],
            [0, 3, 0, 3, 0, 2, 0], [4, 0, 3, 0, 2], [1, 0, 3, 0, 2, 0],
            [0, 4, 4, 4, 0, 3, 0, 2, 0], [3, 0, 2, 0, 1, 0, 4],
            [0, 4, 0, 3, 0, 2, 0, 1, 0]]
        inverse = np.eye(2, dtype=np.complex128)
        for pattern in patterns:
            values = np.zeros((1, 2, len(pattern)), complex)
            values[0, 0] = pattern
            for distance in (1, 2, 3, 10):
                for maximum in (1, 2, 4):
                    self.check(values, inverse, distance=distance, maximum=maximum)
        tied = np.zeros((1, 2, 5), complex)
        tied[0, 0] = [0, 2, 0, 2, 0]
        self.assertEqual(self.kernel(tied, inverse, 1., 1, 2)[2][0], -1)

    def test_small_frontiers_and_large_dynamic_range(self):
        rng = np.random.default_rng(592)
        for scale in (1e-120, 1., 1e120):
            values = scale*(rng.normal(size=(3, 2, 101))+1j*rng.normal(size=(3, 2, 101)))
            inverse = np.array([[1., 1.-1e-8], [1.-1e-8, 1.]], complex)
            self.check(values, inverse)
        values = np.ones((2, 2, 5), complex)
        values[0, 0, 2] = np.inf
        with np.errstate(invalid='ignore'):
            _, _, counts = self.kernel(values, np.eye(2, dtype=complex), 1., 1, 2)
        self.assertEqual(counts[0], -1)

    def test_rounding_ambiguous_rotated_peaks_and_underflow_fall_back(self):
        rng = np.random.default_rng(8624)
        points = rng.normal(size=(2000, 2))+1j*rng.normal(size=(2000, 2))
        rotation = np.exp(1j*rng.uniform(-np.pi, np.pi, size=len(points)))
        values = np.zeros((len(points), 2, 5), complex)
        values[:, :, 1], values[:, :, 3] = points, points*rotation[:, None]
        inverse = np.array([[1.2, .1+.3j], [.1-.3j, .9]], complex)
        _, _, counts = self.check(values, inverse, distance=1, maximum=1)
        # Equal mathematical energies can become equal only in NumPy's SIMD
        # arithmetic, while strict native arithmetic differs by a final bit.
        # All these comparisons belong to the ambiguity fallback.
        self.assertTrue(np.all(counts == -1))
        tiny = np.array([[[0., 1e-160, 0., 2e-160, 0.],
                          [0., 2e-160, 0., 1e-160, 0.]]], complex)
        _, _, counts = self.kernel(tiny, inverse, 1., 1, 2)
        self.assertEqual(counts[0], -1)

    def test_selected_canonical_scores_match_full_numpy_scores_bitwise(self):
        rng = np.random.default_rng(39701)
        inverse = np.array([[1.2, .1+.3j], [.1-.3j, .9]], complex)
        for dtype in (np.complex64, np.complex128):
            for rows, width in ((1, 7), (3, 33), (4, 259), (5, 10553)):
                storage = (rng.normal(size=(rows, 2, width+11))+
                    1j*rng.normal(size=(rows, 2, width+11))).astype(dtype)
                values = storage[..., :width]
                full = _quadratic_scores(values, inverse)/.37
                for maximum in (1, 2):
                    chosen = np.array([_candidate_peak_indices(row, 3, maximum) for row in full])
                    selected = np.take_along_axis(values, chosen[:, None, :], axis=-1)
                    narrow = _quadratic_scores(selected, inverse)/.37
                    np.testing.assert_array_equal(narrow,
                        np.take_along_axis(full, chosen, axis=-1))

    def test_invalid_descriptors_rejected_before_call(self):
        values = np.ones((2, 2, 7), complex)
        inverse = np.eye(2, dtype=complex)
        for wrong in (values[..., ::-1], values[:, :1], values.real,
                      np.empty((0, 2, 7), complex)):
            with self.assertRaises(ValueError):
                self.kernel(wrong, inverse, 1., 1, 2)
        for inverse_bad in (inverse.real, inverse[:, ::-1], np.full((2, 2), np.nan+0j)):
            with self.assertRaises(ValueError):
                self.kernel(values, inverse_bad, 1., 1, 2)
        for distance, maximum, variance in ((0, 2, 1.), (True, 2, 1.),
                (int(np.iinfo(np.uintp).max)+2, 2, 1.), (1, 65, 1.), (1, 2, 0.)):
            with self.assertRaises(ValueError):
                self.kernel(values, inverse, variance, distance, maximum)

    def test_complete_screen_peak_identities_and_ties_match_reference(self):
        m, source, epochs = fixture()
        s = screen(m, source, epochs, precision='fp64')
        raw = waveform(m, source, 1, 1, .035, .37, 0.)
        frequencies = np.linspace(-2., 2., 17)
        projected = s.prepare_observation(raw)
        for values in (projected, np.zeros_like(projected)):
            reference = s.score_projected(values, frequencies, top_per_bit=2)
            s.enable_native_coarse(NATIVE)
            self.assertEqual(s.policy['coarse_score_kernel']['status'],
                             'native_rank2_with_canonical_fallback')
            actual = s.score_projected(values, frequencies, top_per_bit=2)
            self.assertEqual(actual['status'], reference['status'])
            for a, b in zip(reference['candidates'], actual['candidates'], strict=True):
                self.assertEqual({k:v for k,v in a.items() if k != 'score'},
                                 {k:v for k,v in b.items() if k != 'score'})
                self.assertEqual(a['score'], b['score'])
            s.coarse_kernel = None


if __name__ == '__main__':
    unittest.main()

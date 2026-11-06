#!/usr/bin/env python3
"""Small actual-likelihood checks for correlated refinement coordinates."""
import copy
import itertools
import math
from pathlib import Path
import sys
import unittest

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_dynamic_joint import DynamicJointReceiver, public_spectral_bands
from triad_dynamic_variants import ScanJointReceiver
from test_triad_dynamic_joint import fixture


class SparseJointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")

    def setUp(self):
        self.manifest, source = fixture(False)
        self.source = np.concatenate((source, source[:, :, ::-1]), axis=0)
        self.epochs = (0, 1)
        self.kwargs = dict(block_seconds=.1, coherence_seconds=(.2, math.inf),
                           pilot_use="none", precision="fp64")

    def receiver(self, coordinates=None, *, cls=DynamicJointReceiver, **kwargs):
        options = dict(self.kwargs, **kwargs)
        if coordinates is not None:
            options["candidate_coordinates"] = coordinates
        return cls(self.manifest, self.source, self.epochs, **options)

    def test_explicit_cartesian_expansion_has_identical_basis_score_and_identity(self):
        banks = dict(selected_epochs=self.epochs, arrival_seconds=(-.002, .002),
                     frequencies_hz=(-1., 1.), clock_ppm=(-100., 100.))
        coordinates = tuple(itertools.product(*banks.values()))
        cartesian = self.receiver(**banks)
        sparse = self.receiver(iter(coordinates))
        self.assertEqual(sparse.candidate_coordinates, cartesian.candidate_coordinates)
        self.assertEqual(sparse.procedure_id, cartesian.procedure_id)
        np.testing.assert_array_equal(sparse.source_basis(0, 800), cartesian.source_basis(0, 800))
        raw = cartesian.source_basis(0, 800)[2, 3].sum(axis=0)
        np.testing.assert_array_equal(sparse.score(raw)["bit_scores"], cartesian.score(raw)["bit_scores"])

    def test_correlated_list_is_not_reexpanded_and_order_is_bound(self):
        coordinates = ((0, -.002, -1., -100.), (1, .002, 1., 100.))
        receiver = self.receiver(coordinates)
        reverse = self.receiver(coordinates[::-1])
        self.assertEqual(receiver.candidates, 2)
        self.assertEqual(receiver.candidate_coordinates, coordinates)
        self.assertNotEqual(receiver.procedure_id, reverse.procedure_id)
        basis = receiver.source_basis(0, 800)
        np.testing.assert_array_equal(basis[:, ::-1], reverse.source_basis(0, 800))
        raw = basis[1, 0].sum(axis=0)
        np.testing.assert_allclose(receiver.score(raw)["bit_scores"], reverse.score(raw)["bit_scores"],
                                   rtol=1e-12, atol=1e-12)

    def test_projection_uses_actual_sparse_coordinates_without_mutating_manifest(self):
        self.manifest["helper_enabled"] = True
        original = copy.deepcopy(self.manifest)
        coordinates = ((0, -.01, -7.5, 180.), (1, .01, 2., -250.))
        receiver = self.receiver(coordinates, auxiliary_half_width_hz=20.,
                                 arrival_seconds=(123.,), frequencies_hz=(9999.,), clock_ppm=(-2e6,))
        required, excluded = public_spectral_bands(self.manifest, pilot_use="none",
            cfo_bound_hz=7.5, clock_bound_ppm=250., auxiliary_half_width_hz=20.)
        self.assertEqual(receiver.required_bands_hz, required)
        self.assertEqual(receiver.auxiliary_exclusion_hz, excluded)
        self.assertEqual(self.manifest, original)

    def test_sparse_scan_and_fp32_score_both_bits_like_reference(self):
        coordinates = ((0, -.002, -1., -100.), (0, .002, 1., 100.))
        reference = self.receiver(coordinates)
        fp32 = self.receiver(coordinates, precision="fp32")
        scan = self.receiver(coordinates, cls=ScanJointReceiver, precision="fp32")
        basis = reference.source_basis(0, 800)
        raw = np.stack([basis[bit+1, 0].sum(axis=0) for bit in (0, 1)])
        expected = reference.score(raw)["bit_scores"]
        np.testing.assert_array_equal(expected.argmax(axis=1), [0, 1])
        for receiver in (fp32, scan):
            np.testing.assert_allclose(receiver.score(raw)["bit_scores"], expected,
                                       atol=3e-4, rtol=3e-5)

    def test_invalid_sparse_geometry_rejected(self):
        cases = [[], 1, [(0, 0., 0.)], [1], [(True, 0., 0., 0.)],
                 [(0.5, 0., 0., 0.)], [(2, 0., 0., 0.)],
                 [(0, math.nan, 0., 0.)], [(0, 0., math.inf, 0.)],
                 [(0, 0., 0., float("nan"))], [(0, False, 0., 0.)],
                 [(0, 0., 0., -1e6)], [(0, .02, 0., 0.)],
                 [(0, 0., 0., 0.), (0, 0., 0., 0.)]]
        for coordinates in cases:
            with self.subTest(coordinates=coordinates), self.assertRaises(ValueError):
                self.receiver(coordinates)
        with self.assertRaisesRegex(ValueError, "selected epochs"):
            self.receiver([(1, 0., 0., 0.)], selected_epochs=(0,))

    def test_coordinate_limit_stops_before_unbounded_iterable_or_numerical_preparation(self):
        visited = []
        def unbounded():
            for frequency in itertools.count():
                visited.append(frequency)
                yield (0, 0., float(frequency), 0.)
        with self.assertRaisesRegex(ValueError, "4096"):
            self.receiver(unbounded(), memory_limit_bytes=1)
        self.assertEqual(len(visited), 4097)


if __name__ == "__main__":
    unittest.main()

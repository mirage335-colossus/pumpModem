"""Exactness and bounds for the opt-in prepared Gaussian design cache."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
from test_triad_state_workspace import write_export
from triad_prepared_observation import PreparedGaussianObservation
from triad_state_workspace import BlockWorkspace, WorkspaceError


def make_workspace(directory, *, nonlinear=False, invalid_energy=False,
                   tile_blocks=1):
    rng = np.random.default_rng(801)
    basis = rng.standard_normal((3, 2, 3)) + 1j*rng.standard_normal((3, 2, 3))
    grams = np.einsum("bki,bkj->bij", basis.conj(), basis)
    means = rng.standard_normal((3, 3)) + 1j*rng.standard_normal((3, 3))
    intervals = np.array([[0, 8], [8, 16], [16, 19]], dtype=np.uint64)
    valid = np.array([8, 8, 3], dtype=np.uint64)
    energies = np.array([1., 2., -1. if invalid_energy else 3.])
    manifest = write_export(directory, grams, means, block_samples=8,
                             sample_rate=64., intervals=intervals, valid=valid,
                             energies=energies, mapping=[[0], [1], [2]])
    manifest["sample_count"] = 19
    if nonlinear:
        manifest["gaussian_projection_supported"] = False
    (Path(directory)/"manifest.json").write_text(json.dumps(manifest))
    return BlockWorkspace(directory, max_tile_blocks=tile_blocks)


class PreparedObservationTests(unittest.TestCase):
    def test_paired_blocks_are_bit_identical_to_reference_and_reusable(self):
        with tempfile.TemporaryDirectory() as temp:
            ws = make_workspace(temp)
            sim_args = dict(seed=91, batch=11, cn0_db_hz=-4.0)
            prepared = PreparedGaussianObservation(ws, **sim_args,
                memory_limit_bytes=1 << 20)
            h0 = list(ws.iter_simulator(**sim_args, signal=False, tile_blocks=1))
            h1 = list(ws.iter_simulator(**sim_args, signal=True, tile_blocks=1))
            first = list(prepared.iter_paired())
            second = list(prepared.iter_paired())
            varied = list(prepared.iter_paired(seed=92, cn0_db_hz=-2.0))
            self.assertEqual(len(first), 3)
            self.assertEqual(prepared.diagnostics()["rng_calls"], 9)
            self.assertEqual(prepared.diagnostics()["cached_noise_or_observation_bytes"], 0)
            self.assertEqual(prepared.diagnostics()["dtype"], "complex128/float64")
            retained_bytes = sum(array.nbytes for array in (
                prepared.grams, prepared.factors, prepared.means,
                prepared.intervals, prepared.valid_rows, prepared.centers,
                prepared.dts))
            self.assertEqual(prepared.diagnostics()["cache_bytes"], retained_bytes)
            for index, (pair, repeated) in enumerate(zip(first, second)):
                np.testing.assert_array_equal(pair.null.r, h0[index].r)
                np.testing.assert_array_equal(pair.signal.r, h1[index].r)
                np.testing.assert_array_equal(pair.null.gram, h0[index].gram)
                np.testing.assert_array_equal(pair.signal.gram, h1[index].gram)
                np.testing.assert_array_equal(repeated.null.r, pair.null.r)
                np.testing.assert_array_equal(repeated.signal.r, pair.signal.r)
                self.assertEqual(pair.null.interval, h0[index].interval)
                self.assertEqual(pair.signal.index, h1[index].index)
                self.assertEqual(pair.null.dt_seconds, h0[index].dt_seconds)
                self.assertEqual(pair.signal.center_seconds, h1[index].center_seconds)
            self.assertEqual(first[-1].signal.valid_rows, 3)
            self.assertEqual(first[-1].signal.dt_seconds, 11/128)
            self.assertFalse(np.array_equal(varied[0].null.r, first[0].null.r))
            fresh_snr_reference = next(ws.iter_simulator(seed=92, batch=11,
                cn0_db_hz=-2.0, signal=False))
            np.testing.assert_array_equal(varied[0].null.r, fresh_snr_reference.r)
            self.assertLessEqual(prepared.diagnostics()["estimated_peak_bytes"], 1 << 20)

    def test_subrange_keeps_absolute_calendar_and_transition(self):
        with tempfile.TemporaryDirectory() as temp:
            ws = make_workspace(temp)
            prepared = PreparedGaussianObservation(ws, seed=4, batch=3,
                cn0_db_hz=0., start=1, stop=3, memory_limit_bytes=1 << 20)
            expected = list(ws.iter_simulator(seed=4, batch=3, cn0_db_hz=0.,
                                               start=1, stop=3, signal=True))
            for pair, ref in zip(prepared.iter_paired(), expected):
                np.testing.assert_array_equal(pair.signal.r, ref.r)
                self.assertEqual(pair.signal.index, ref.index)
                self.assertEqual(pair.signal.dt_seconds, ref.dt_seconds)

    def test_rejects_nonlinear_frontend_bad_truth_and_memory_overflow(self):
        with tempfile.TemporaryDirectory() as temp:
            ws = make_workspace(temp, nonlinear=True)
            with self.assertRaisesRegex(WorkspaceError, "sampled frontend"):
                PreparedGaussianObservation(ws, seed=1, batch=1, cn0_db_hz=0.)
        with tempfile.TemporaryDirectory() as temp:
            ws = make_workspace(temp, invalid_energy=True)
            with self.assertRaisesRegex(WorkspaceError, "simulation truth"):
                PreparedGaussianObservation(ws, seed=1, batch=1, cn0_db_hz=0.,
                                           memory_limit_bytes=1 << 20)
        with tempfile.TemporaryDirectory() as temp:
            ws = make_workspace(temp, tile_blocks=3)
            with self.assertRaisesRegex(WorkspaceError, "exceeds memory_limit_bytes"):
                PreparedGaussianObservation(ws, seed=1, batch=1, cn0_db_hz=0.,
                                           memory_limit_bytes=4096)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Focused checks for the v3 block-statistics workspace."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.triad_state_workspace import BlockWorkspace, WorkspaceError


def write_export(directory, grams, means=None, *, sample_rate=64.0, block_samples=8,
                 intervals=None, valid=None, mapping=None, energies=None, observation=None):
    directory = Path(directory)
    G = np.asarray(grams, dtype="<c16")
    if G.ndim != 3 or G.shape[1] != G.shape[2]:
        raise ValueError("grams must have [blocks,D,D] shape")
    b, d, _ = G.shape
    M = np.zeros((b, d), dtype="<c16") if means is None else np.asarray(means, dtype="<c16")
    I = np.asarray([[i*block_samples, min((i+1)*block_samples, b*block_samples)] for i in range(b)], dtype="<u8") if intervals is None else np.asarray(intervals, dtype="<u8")
    V = (I[:, 1]-I[:, 0]).astype("<u8") if valid is None else np.asarray(valid, dtype="<u8")
    E = np.ones(b, dtype="<f8") if energies is None else np.asarray(energies, dtype="<f8")
    files = {
        "gram.c128": G, "mean.c128": M, "intervals.u64": I,
        "valid_rows.u64": V, "signal_energy.f64": E,
    }
    sizes = {}
    for name, array in files.items():
        array.tofile(directory / name)
        sizes[name] = (directory / name).stat().st_size
    colmap = [[0], [1]] if mapping is None else mapping
    candidates = [{"bit": i%2, "tap_count": len(row), "arrival_seconds": 0.0,
                  "frequency_hz": 0.0, "clock_ppm": 0.0, "frequency_drift_hz_per_second": 0.0,
                  "taps": [{"delay_seconds": float(j), "doppler_hz": 0.0} for j in range(len(row))]}
                 for i, row in enumerate(colmap)]
    descriptors = [{"candidate_index": i % len(candidates), "source_bit": i % len(candidates) % 2,
                    "arrival_seconds": 0.0, "frequency_hz": 0.0, "clock_ppm": 0.0,
                    "frequency_drift_hz_per_second": 0.0, "tap_delay_seconds": 0.0,
                    "tap_doppler_hz": 0.0} for i in range(d)]
    manifest = {
        "format": "triad-block-stats-v1", "byte_order": "little",
        "complex_type": "complex128", "real_type": "float64", "integer_type": "uint64",
        "layout": "block,row,column", "block_count": b, "dimension": d,
        "sample_rate": sample_rate, "noise_variance": 1.0, "sample_count": int(I[-1, 1]),
        "block_samples": block_samples, "gram_file": "gram.c128", "gram_shape": [b,d,d],
        "mean_file": "mean.c128", "mean_shape": [b,d], "intervals_file": "intervals.u64",
        "intervals_shape": [b,2], "valid_rows_file": "valid_rows.u64", "valid_rows_shape": [b],
        "signal_energy_file": "signal_energy.f64", "signal_energy_shape": [b],
        "column_mapping": colmap, "candidate_count": len(colmap),
        "candidates": candidates,
        "column_descriptors": descriptors, "file_sizes": sizes,
        "stats_noisy": observation is not None,
    }
    if observation is not None:
        obs = np.asarray(observation, dtype="<c16")
        obs.tofile(directory / "observation.c128")
        sizes["observation.c128"] = (directory / "observation.c128").stat().st_size
        manifest.update({"observation_file": "observation.c128", "observation_shape": [b,d]})
    (directory / "manifest.json").write_text(json.dumps(manifest))
    return manifest


class WorkspaceTests(unittest.TestCase):
    def test_full_covariance_and_paired_h0_h1(self):
        # A^H A for two nonorthogonal columns; preserve the cross-correlation.
        G0 = np.array([[2.0, .7+.2j], [.7-.2j, 1.5]], dtype=np.complex128)
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, G0[None], np.array([[1.2-.3j, -.4+.8j]]), block_samples=8)
            ws = BlockWorkspace(temp)
            h0 = next(ws.iter_simulator(seed=74, batch=50000, cn0_db_hz=18, signal=False))
            h1 = next(ws.iter_simulator(seed=74, batch=50000, cn0_db_hz=18, signal=True))
            variance = 64.0 / (10.0 ** 1.8)
            np.testing.assert_allclose(h1.r-h0.r, np.broadcast_to(np.array([[1.2-.3j, -.4+.8j]]) / variance, h0.r.shape), rtol=1e-11, atol=1e-11)
            # Undo normalization and test the proper complex covariance.
            z = h0.r * variance
            empirical = (z.T @ z.conj()) / len(z)
            np.testing.assert_allclose(empirical, variance*G0, atol=.06, rtol=.02)
            self.assertEqual(ws.column_mapping, ((0,), (1,)))
            self.assertEqual(next(ws.iter_public()).gram.shape, (1,2,2))
            self.assertFalse(hasattr(next(ws.iter_public()), "signal_projection"))

    def test_rank_deficient_and_rank_zero(self):
        rank1 = np.array([[1, 1j], [-1j, 1]], dtype=np.complex128)
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, np.stack([rank1, np.zeros((2,2))]), block_samples=8)
            ws = BlockWorkspace(temp)
            blocks = list(ws.iter_simulator(seed=1, batch=32, cn0_db_hz=0, signal=False))
            self.assertTrue(np.isfinite(blocks[0].r).all())
            self.assertTrue(np.array_equal(blocks[1].r, np.zeros((32,2))))
            self.assertTrue(np.array_equal(blocks[1].gram, np.zeros((2,2))))

    def test_public_candidate_mapping_and_malformed_data(self):
        G = np.broadcast_to(np.eye(3), (2,3,3)).copy()
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, G, mapping=[[0,1],[0,2]])
            ws = BlockWorkspace(temp, max_tile_blocks=1)
            self.assertEqual(ws.column_mapping, ((0,1),(0,2)))
            self.assertEqual(sum(len(t.intervals) for t in ws.iter_public()), 2)
            # Corrupt a later tile and ensure validation occurs while streaming.
            gram_path = Path(temp) / "gram.c128"
            bad = np.memmap(gram_path, mode="r+", dtype="<c16", shape=(2,3,3))
            bad[1,0,1] = 1.0+0j
            bad.flush()
            with self.assertRaises(WorkspaceError):
                list(ws.iter_public(tile_blocks=1))

    def test_public_path_does_not_read_truth(self):
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, np.eye(2, dtype=np.complex128)[None])
            ws = BlockWorkspace(temp)
            # Files remain stat-able for schema checks, but invalid hidden truth
            # is neither mapped nor required for a public Gram/calendar iterator.
            bad = np.memmap(Path(temp)/"mean.c128", mode="r+", dtype="<c16", shape=(1,2))
            bad[0,0] = complex(float("nan"), 0)
            bad.flush()
            self.assertEqual(len(list(ws.iter_public())), 1)
            with self.assertRaises(WorkspaceError):
                next(ws.iter_simulator(seed=2, batch=1, cn0_db_hz=0))

    def test_partial_ranges_are_partition_invariant(self):
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, np.broadcast_to(np.eye(2),(3,2,2)), block_samples=8)
            ws = BlockWorkspace(temp)
            full = list(ws.iter_simulator(seed=11, batch=7, cn0_db_hz=-2))
            later = list(ws.iter_simulator(seed=11, batch=7, cn0_db_hz=-2, start=1, stop=3))
            for expected, got in zip(full[1:], later):
                np.testing.assert_array_equal(expected.r, got.r)
                self.assertEqual(expected.index, got.index)
                self.assertEqual(expected.dt_seconds, got.dt_seconds)

    def test_actual_observation_uses_normalized_block_api(self):
        G = np.broadcast_to(np.eye(2),(2,2,2)).copy()
        raw = np.array([[2+1j, 3-2j],[4+5j, -1+2j]])
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, G, block_samples=8, observation=raw)
            ws = BlockWorkspace(temp)
            blocks = list(ws.iter_observation(tile_blocks=1))
            self.assertEqual(len(blocks), 2)
            self.assertEqual(blocks[1].index, 1)
            self.assertEqual(blocks[1].dt_seconds, 8/64)
            tail = list(ws.iter_observation(start=1))
            self.assertEqual(tail[0].dt_seconds, blocks[1].dt_seconds)
            variance = ws.noise_variance
            np.testing.assert_allclose(blocks[0].r[0], raw[0]/variance)
            np.testing.assert_allclose(blocks[0].gram, G[0]/variance)
            with self.assertRaises(ValueError):
                list(ws.iter_observation(tile_blocks=0))

    def test_joint_memory_limit_and_candidate_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, np.broadcast_to(np.eye(2),(2,2,2)), block_samples=8)
            ws = BlockWorkspace(temp, max_tile_blocks=2, memory_limit_bytes=5000)
            with self.assertRaises(WorkspaceError):
                next(ws.iter_simulator(seed=1,batch=20,cn0_db_hz=0))

        with tempfile.TemporaryDirectory() as temp:
            manifest = write_export(temp, np.eye(2,dtype=np.complex128)[None])
            manifest["candidates"][0]["tap_count"] = 9
            Path(temp,"manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(WorkspaceError):
                BlockWorkspace(temp)

    def test_rejects_bad_interval_and_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            write_export(temp, np.eye(2, dtype=np.complex128)[None])
            path = Path(temp) / "manifest.json"
            m = json.loads(path.read_text())
            m["dimension"] = 3
            path.write_text(json.dumps(m))
            with self.assertRaises(WorkspaceError):
                BlockWorkspace(temp)

        with tempfile.TemporaryDirectory() as temp:
            I = np.array([[0,8],[9,16]], dtype="<u8")
            write_export(temp, np.broadcast_to(np.eye(2),(2,2,2)), intervals=I)
            with self.assertRaises(WorkspaceError):
                list(BlockWorkspace(temp).iter_public())


if __name__ == "__main__":
    unittest.main()

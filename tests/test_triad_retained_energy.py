#!/usr/bin/env python3
"""Independent raw-complex projection oracles for the truth-only diagnostic."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
SPEC = importlib.util.spec_from_file_location("triad_retained_energy", ROOT/"tools"/"triad-retained-energy.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


def cn(rng, shape):
    return (rng.normal(size=shape)+1j*rng.normal(size=shape))/np.sqrt(2.)


def write_raw_workspace(directory, designs, signals, masks, *, control="signal", mapping=None):
    B, n, D = len(designs), *designs[0].shape
    if mapping is None:
        mapping = [[0], [1], [2]]
    gram, means, energy, valid = [], [], [], []
    for A, signal, mask in zip(designs, signals, masks):
        A, signal = A[mask], signal[mask]
        gram.append(A.conj().T@A)
        means.append(A.conj().T@signal)
        energy.append(float(np.vdot(signal, signal).real))
        valid.append(len(signal))
    arrays = {"gram.c128": np.asarray(gram, dtype="<c16"),
              "mean.c128": np.asarray(means, dtype="<c16"),
              "signal_energy.f64": np.asarray(energy, dtype="<f8"),
              "intervals.u64": np.asarray([[i*n,(i+1)*n] for i in range(B)], dtype="<u8"),
              "valid_rows.u64": np.asarray(valid, dtype="<u8")}
    sizes = {}
    for name, array in arrays.items():
        array.tofile(directory/name)
        sizes[name] = array.nbytes
    candidates, descriptors = [], [None]*D
    for i, columns in enumerate(mapping):
        bit = 1 if i == len(mapping)-1 else 0
        candidate = {"bit": bit, "epoch_offset_seconds": 0, "arrival_seconds": float(i),
            "frequency_hz": 0., "clock_ppm": 0., "frequency_drift_hz_per_second": 0.,
            "tap_count": len(columns), "taps": [{"delay_seconds": .001*k, "doppler_hz": .1*k} for k in range(len(columns))]}
        candidates.append(candidate)
        for k, col in enumerate(columns):
            descriptors[col] = {"candidate_index": i, "source_bit": bit,
                "arrival_seconds": float(i), "frequency_hz": 0., "clock_ppm": 0.,
                "frequency_drift_hz_per_second": 0., "tap_delay_seconds": .001*k,
                "tap_doppler_hz": .1*k, "linear_gain": False, "gain_column": 0}
    m = {"format": "triad-block-stats-v1", "byte_order": "little", "complex_type": "complex128",
         "real_type": "float64", "integer_type": "uint64", "layout": "block,row,column",
         "block_count": B, "dimension": D, "sample_rate": 64., "noise_variance": 2.,
         "sample_count": B*n, "block_samples": n,
         "gram_file": "gram.c128", "gram_shape": [B,D,D],
         "mean_file": "mean.c128", "mean_shape": [B,D],
         "signal_energy_file": "signal_energy.f64", "signal_energy_shape": [B],
         "intervals_file": "intervals.u64", "intervals_shape": [B,2],
         "valid_rows_file": "valid_rows.u64", "valid_rows_shape": [B],
         "candidate_count": len(mapping), "candidates": candidates,
         "column_mapping": mapping, "column_descriptors": descriptors, "file_sizes": sizes,
         "stats_noisy": False, "signal_bit": 0, "control": control, "cn0_db_hz": -24.}
    (directory/"manifest.json").write_text(json.dumps(m))
    return np.asarray(gram), np.asarray(means), np.asarray(energy)


class RetainedEnergyTests(unittest.TestCase):
    def test_complex_raw_svd_oracle_rank_deficiency_and_erasures(self):
        rng = np.random.default_rng(3521)
        As, ss, masks = [], [], []
        for i in range(3):
            base = cn(rng, (9, 2))
            As.append(np.column_stack([base, base[:, 0]+2*base[:, 1]]))
            ss.append(cn(rng, (9,)))
            masks.append(np.array([j % 3 != 0 for j in range(9)]) if i == 0 else np.full(9, i != 1))
        expected = 0.
        total = 0.
        for A, s, mask in zip(As, ss, masks):
            A, s = A[mask], s[mask]
            if len(s):
                U, singular, _ = np.linalg.svd(A, full_matrices=False)
                rank = np.count_nonzero(singular**2 > singular[0]**2*1e-10)
                expected += float(np.sum(abs(U[:, :rank].conj().T@s)**2))
                total += float(np.vdot(s, s).real)
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)
            write_raw_workspace(p, As, ss, masks)
            created = []
            original = tool.BlockWorkspace
            def tracked(*args, **kwargs):
                w = original(*args, **kwargs); created.append(w); return w
            with patch.object(tool, "BlockWorkspace", side_effect=tracked), \
                    patch.object(original, "_map", side_effect=AssertionError("whole-file mappings are forbidden")):
                result = tool.diagnose_path(p)
            self.assertEqual(result["status"], "completed")
            self.assertAlmostEqual(result["full_bank_retained_energy_samples"], expected, places=11)
            self.assertAlmostEqual(result["full_bank_retained_energy_fraction"], expected/total, places=12)
            self.assertEqual(result["fully_erased_blocks"], 1)
            self.assertEqual(result["valid_complex_samples"], 15)
            self.assertEqual(result["sampled_observation_seconds"], 27/64.)
            self.assertEqual(result["full_bank_rank_range"], [0,2])
            self.assertEqual(created[0]._maps, {})
            self.assertEqual(result["energy_reference_simulation_truth_only"]["declared_cn0_db_hz"], -24.)
            self.assertEqual(len(result["input_hashes"]), 6)

    def test_fixed_candidate_selected_after_whole_window_sum(self):
        # A per-block winner would retain 100%; either fixed true-bit timing
        # retains only half. This catches an optimistic hidden model switch.
        A = np.eye(4, 3, dtype=complex)
        signals = [np.array([1.,0.,0.,0.]), np.array([0.,1.,0.,0.])]
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)
            write_raw_workspace(p, [A,A], signals, [np.ones(4,dtype=bool)]*2)
            result = tool.diagnose_path(p)
            self.assertEqual(result["full_bank_retained_energy_fraction"], 1.)
            best = result["best_fixed_true_bit_candidate"]
            self.assertEqual(best["retained_energy_fraction"], .5)
            self.assertEqual(best["identity"]["bit"], 0)
            self.assertEqual(len(result["fixed_candidates"]), 3)

    def test_eigen_cutoff_and_invalid_energy_fail_closed(self):
        G = np.diag([1.,1e-12]).astype(complex)
        E, rank = tool.retained_energy(G, np.array([.5,1e-6]))
        self.assertEqual(rank, 1)
        self.assertEqual(E, .25)
        with self.assertRaisesRegex(ValueError, "positive semidefinite"):
            tool.retained_energy(np.diag([1.,-.1]), np.ones(2))
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)
            A = np.eye(4,3,dtype=complex)
            write_raw_workspace(p,[A],[np.ones(4)],[np.ones(4,dtype=bool)])
            np.asarray([.1],dtype="<f8").tofile(p/"signal_energy.f64")
            with self.assertRaisesRegex(ValueError,"exceeds actual signal"):
                tool.diagnose_path(p)

    def test_null_zero_signal_and_reproducible_output(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            paths = []
            for control in ("null", "signal"):
                p = base/control; p.mkdir(); paths.append(p)
                A = np.eye(4,3,dtype=complex)
                write_raw_workspace(p,[A],[np.zeros(4)],[np.ones(4,dtype=bool)],control=control)
            report = tool.run(paths,base/"result.json")
            self.assertEqual([x["status"] for x in report["paths"]],
                             ["unsupported_null_control","unsupported_zero_signal_energy"])
            self.assertEqual(len(report["source_sha256"]),64)
            self.assertEqual(report["rank_relative_eigenvalue_cutoff"],1e-10)
            self.assertNotIn("observation.c128",report["paths"][0]["input_hashes"])
            with self.assertRaisesRegex(ValueError,"fresh"):
                tool.run(paths,base/"result.json")


if __name__ == "__main__":
    unittest.main()

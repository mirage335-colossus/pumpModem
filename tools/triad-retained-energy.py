#!/usr/bin/env python3
"""SIMULATOR-TRUTH diagnostic of signal energy retained by exported bases.

This is not a receiver: it deliberately reads mean.c128=A.H@s_actual and
signal_energy.f64. Each block permits freely fitted complex coefficients. A
fixed candidate's block energies are summed BEFORE choosing a candidate, so
there is no per-block timing/epoch/model winner. The full-bank result is a
still more optimistic ceiling that also includes wrong-bit columns. Neither
quantity is an achievable sensitivity, correct-bit probability or LR statistic.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from triad_state_workspace import BlockWorkspace, WorkspaceError


RANK_RTOL = 1e-10


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def retained_energy(gram, mean, *, rank_rtol=RANK_RTOL):
    """Return mu.H G⁺ mu and rank for the fixed FP64 retained eigenspace."""
    G, mu = np.asarray(gram, dtype=np.complex128), np.asarray(mean, dtype=np.complex128)
    if G.ndim != 2 or G.shape[0] != G.shape[1] or mu.shape != (G.shape[0],):
        raise ValueError("retained-energy Gram and mean shapes disagree")
    if not np.isfinite(G).all() or not np.isfinite(mu).all():
        raise ValueError("retained-energy statistics must be finite")
    if not math.isfinite(rank_rtol) or not 0 < rank_rtol < .01:
        raise ValueError("invalid retained-energy rank tolerance")
    scale = max(float(np.max(np.abs(G))), np.finfo(np.float64).tiny)
    if np.max(np.abs(G-G.conj().T)) > 1e-10*scale:
        raise ValueError("retained-energy Gram is not Hermitian")
    values, vectors = np.linalg.eigh((G+G.conj().T)*.5)
    if values[0] < -1e-10*scale:
        raise ValueError("retained-energy Gram is not positive semidefinite")
    keep = values > max(0., float(values[-1]))*rank_rtol
    if keep.any():
        coordinate = vectors[:, keep].conj().T@mu
        # Division before squaring avoids reciprocal overflow for tiny units.
        energy = float(np.sum(np.abs(coordinate/np.sqrt(values[keep]))**2))
    else:
        energy = 0.
    if not math.isfinite(energy):
        raise ValueError("retained projection overflowed; rescale the exported statistics")
    return energy, int(keep.sum())


def _groups(workspace):
    groups = {}
    for index, (candidate, columns) in enumerate(zip(workspace.candidates, workspace.column_mapping)):
        identity = {key: candidate.get(key) for key in (
            "bit", "epoch_offset_seconds", "arrival_seconds", "frequency_hz",
            "clock_ppm", "frequency_drift_hz_per_second", "tap_count", "taps")}
        # The exporter also supports multiple gain columns for one tap. Keep
        # that basis geometry explicit rather than silently merging models.
        identity["gain_columns"] = [{key: workspace.manifest["column_descriptors"][c].get(key)
                                    for key in ("linear_gain", "gain_column")} for c in columns]
        key = json.dumps(identity, sort_keys=True, separators=(",", ":"))
        if key not in groups:
            groups[key] = {"identity": identity, "candidate_indices": [], "columns": set()}
        groups[key]["candidate_indices"].append(index)
        groups[key]["columns"].update(columns)
    result = []
    for group in groups.values():
        result.append({**group, "columns": sorted(group["columns"])})
    return result


def _release_maps(workspace):
    for mapping in workspace._maps.values():
        mapping._mmap.close()
    workspace._maps.clear()


def _raw_blocks(workspace):
    """Sequential reads also bound process RSS; no whole-file mapped pages."""
    names = ("gram.c128", "mean.c128", "signal_energy.f64", "intervals.u64", "valid_rows.u64")
    D = workspace.dimension
    with ExitStack() as stack:
        files = {name: stack.enter_context((workspace.directory/name).open("rb")) for name in names}
        for index in range(workspace.block_count):
            G = np.fromfile(files["gram.c128"], dtype="<c16", count=D*D)
            mu = np.fromfile(files["mean.c128"], dtype="<c16", count=D)
            energy = np.fromfile(files["signal_energy.f64"], dtype="<f8", count=1)
            interval = np.fromfile(files["intervals.u64"], dtype="<u8", count=2)
            valid = np.fromfile(files["valid_rows.u64"], dtype="<u8", count=1)
            if (G.size != D*D or mu.size != D or energy.size != 1 or
                    interval.size != 2 or valid.size != 1):
                raise WorkspaceError("statistics file changed or ended during sequential read")
            first = index*workspace.block_samples
            stop = min(first+workspace.block_samples, workspace.sample_count)
            if tuple(int(x) for x in interval) != (first, stop) or int(valid[0]) > stop-first:
                raise WorkspaceError("block interval/valid rows disagree with declared sample calendar")
            yield G.reshape(D,D), mu, float(energy[0]), int(valid[0])


def diagnose_path(directory, *, memory_limit_bytes=256*1024**2):
    """Read one block at a time; retain O(D² + fixed candidate count) state."""
    workspace = BlockWorkspace(directory, max_tile_blocks=1, memory_limit_bytes=memory_limit_bytes)
    try:
        m = workspace.manifest
        hashes = {name: sha256(workspace.directory/name) for name in (
            "manifest.json", "gram.c128", "mean.c128", "signal_energy.f64",
            "intervals.u64", "valid_rows.u64")}
        result = {"input": str(workspace.directory), "input_hashes": hashes,
            "diagnostic_only_simulation_truth": True,
            "sample_rate_hz": workspace.sample_rate, "sample_count": workspace.sample_count,
            "sampled_observation_seconds": workspace.sample_count/workspace.sample_rate,
            "block_count": workspace.block_count, "full_bank_columns": workspace.dimension,
            "signal_control": m.get("control"),
            "energy_reference_simulation_truth_only": {
                "noise_variance_per_complex_sample": workspace.noise_variance,
                "declared_cn0_db_hz": m.get("cn0_db_hz"),
                "source_samples": m.get("source_samples"),
                "requested_active_seconds": m.get("requested_active_seconds"),
                "actual_elapsed_seconds": m.get("actual_elapsed_seconds"),
                "source_format": m.get("source_format"),
                "channel_model": m.get("channel_model"),
                "oscillator_model": m.get("oscillator_model")}}
        groups = _groups(workspace)
        sums, compensation = np.zeros(len(groups)+2), np.zeros(len(groups)+2)
        rank_min, rank_max = workspace.dimension, 0
        valid_total = erased_blocks = blocks = repairs = 0
        max_excess = 0.
        for G, mu, signal_energy, valid in _raw_blocks(workspace):
            if not np.isfinite(mu).all() or not math.isfinite(signal_energy) or signal_energy < 0:
                raise ValueError(f"invalid simulation signal statistics at block {blocks}")
            if valid == 0 and (signal_energy != 0. or np.any(mu != 0.) or np.any(G != 0.)):
                raise ValueError("fully erased block has nonzero signal/Gram statistics")
            full, rank = retained_energy(G, mu)
            if rank > valid:
                raise ValueError("full-bank retained rank exceeds valid sample count")
            values = [signal_energy, full]
            for group in groups:
                ix = group["columns"]
                energy, _ = retained_energy(G[np.ix_(ix, ix)], mu[ix])
                values.append(energy)
            values = np.asarray(values, dtype=np.float64)
            for j in range(1, len(values)):
                excess = values[j]-signal_energy
                if excess > 0:
                    relative = excess/max(signal_energy, values[j], np.finfo(np.float64).tiny)
                    if relative > 2e-8:
                        raise ValueError(f"retained energy exceeds actual signal energy at block {blocks}")
                    max_excess = max(max_excess, relative)
                    values[j] = signal_energy
                    repairs += 1
            adjusted = values-compensation
            updated = sums+adjusted
            compensation = (updated-sums)-adjusted
            sums = updated
            rank_min, rank_max = min(rank_min, rank), max(rank_max, rank)
            valid_total += valid
            erased_blocks += int(valid == 0)
            blocks += 1
        result.update(valid_complex_samples=valid_total,
            valid_sample_seconds=valid_total/workspace.sample_rate,
            fully_erased_blocks=erased_blocks,
            total_signal_energy_samples=float(sums[0]),
            total_signal_energy_over_noise=float(sums[0]/workspace.noise_variance),
            full_bank_rank_range=[rank_min, rank_max],
            numerical_roundoff_repairs=repairs,
            maximum_relative_energy_excess_repaired=max_excess)
        control_null = m.get("control") == "null" or m.get("signal_enabled") is False
        if control_null or sums[0] == 0:
            result.update(status="unsupported_null_control" if control_null else "unsupported_zero_signal_energy",
                          reason="a retained signal-energy fraction is undefined without transmitted signal energy")
            return result
        truth_bit = m.get("signal_bit_simulation_truth", m.get("signal_bit"))
        if isinstance(truth_bit, bool) or truth_bit not in (0, 1):
            result.update(status="unsupported_missing_truth_bit",
                          reason="best true-bit fixed candidate requires explicit simulation truth bit")
            return result
        candidates = []
        for group, energy in zip(groups, sums[2:]):
            candidates.append({**group, "retained_energy_samples": float(energy),
                               "retained_energy_fraction": float(energy/sums[0])})
        true_candidates = [row for row in candidates if row["identity"]["bit"] == truth_bit]
        if not true_candidates:
            raise ValueError("exported bank has no candidate for the simulation truth bit")
        best = max(true_candidates, key=lambda row: row["retained_energy_samples"])
        result.update(status="completed", truth_bit_simulation_only=truth_bit,
            full_bank_retained_energy_samples=float(sums[1]),
            full_bank_retained_energy_fraction=float(sums[1]/sums[0]),
            best_fixed_true_bit_candidate=best, fixed_candidates=candidates,
            scope="optimistic per-block free complex channel fits; fixed candidate selected only after summing its full-window energy; full bank includes wrong-bit columns; not a receiver or sensitivity result")
        return result
    finally:
        _release_maps(workspace)


def run(inputs, output, *, memory_limit_bytes=256*1024**2):
    output = Path(output)
    if output.exists():
        raise ValueError("output path must be fresh")
    paths = []
    start = time.perf_counter()
    for directory in inputs:
        try:
            paths.append(diagnose_path(directory, memory_limit_bytes=memory_limit_bytes))
        except (ValueError, OSError, WorkspaceError) as error:
            paths.append({"input": str(directory), "status": "error",
                          "error_type": type(error).__name__, "error": str(error),
                          "diagnostic_only_simulation_truth": True})
    report = {"schema": "triad-retained-energy-v1", "source_sha256": sha256(__file__),
        "workspace_source_sha256": sha256(Path(__file__).with_name("triad_state_workspace.py")),
        "rank_relative_eigenvalue_cutoff": RANK_RTOL,
        "arithmetic": "NumPy complex128/FP64 eigensystems and compensated energy totals",
        "memory_policy": "one exported Gram block plus means and scalar candidate totals; file maps released after each path",
        "paths": paths, "wall_seconds": time.perf_counter()-start,
        "warning": "uses hidden noiseless simulation means/energies; must never feed receiver decisions or false-admission calibration"}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--memory-mib", type=int, default=256)
    args = parser.parse_args(argv)
    report = run(args.inputs, args.output, memory_limit_bytes=args.memory_mib*1024**2)
    print(json.dumps({"output": str(args.output), "paths": len(report["paths"]),
                     "failed": sum(row["status"] == "error" for row in report["paths"])}))
    return int(any(row["status"] == "error" for row in report["paths"]))


if __name__ == "__main__":
    raise SystemExit(main())

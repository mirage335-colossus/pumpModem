#!/usr/bin/env python3
"""Post-hoc wrong-key diagnostics; never a receiver or qualification result.

Reproduce saved-score threshold/margin tradeoffs and inspect six retained
simulator workspaces. Hidden means/energies/noise variance are used ONLY for
energy and expected-score diagnostics, never to change an admission decision.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

import numpy as np


REPOSITORY = next(p for p in Path(__file__).resolve().parents
                  if (p / "tools/triad_state_workspace.py").exists())
sys.path.insert(0, str(REPOSITORY / "tools"))
from triad_state_workspace import BlockWorkspace
from triad_gaussian_window import prepare_windowed_gaussian

_spec = importlib.util.spec_from_file_location("retained_energy_diagnostic",
    REPOSITORY / "tools/triad-retained-energy.py")
retention = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(retention)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def relative(path):
    try:
        return str(Path(path).resolve().relative_to(REPOSITORY))
    except ValueError:
        return str(path)


def read_scores(path):
    report = json.loads(Path(path).read_text())
    archive = Path(path).with_suffix(".scores.npz")
    arrays, truth = [], []
    with np.load(archive, allow_pickle=False) as data:
        for row in report["paths"]:
            if row["status"] != "completed":
                raise ValueError("diagnostic requires complete fixed score cells")
            condition = row["conditions"][0]
            method = condition["methods"]["gaussian-window-ucb"]
            score = np.asarray(data[method["score_array"]], dtype=np.float64)
            if score.shape != (1, 2) or not np.isfinite(score).all():
                raise ValueError("one finite two-bit actual score required per path")
            arrays.append(score[0])
            truth.append(row.get("truth_bit_simulation_only"))
    return np.asarray(arrays), truth, {
        "json": relative(path), "json_sha256": sha256(path),
        "npz": relative(archive), "npz_sha256": sha256(archive)}, report


def operating_point(signal, truth, wrong_key, threshold, margin):
    def admitted(values):
        return (np.max(values, axis=1) >= threshold) & (
            np.abs(values[:, 0] - values[:, 1]) >= margin)
    positive, negative = admitted(signal), admitted(wrong_key)
    correct = np.argmax(signal, axis=1) == np.asarray(truth)
    return {"log_lr_threshold": float(threshold), "log_lr_margin_minimum": float(margin),
        "signal_trials": len(signal), "correct_accepted": int(np.sum(positive & correct)),
        "wrong_bit_accepted": int(np.sum(positive & ~correct)),
        "missed": int(np.sum(~positive)), "wrong_key_trials": len(wrong_key),
        "wrong_key_admitted": int(np.sum(negative))}


def score_tradeoffs(scores_root):
    rows = []
    for profile, cn in (("g01slowrate", 24), ("g1slowrate", 14)):
        stem = f"{profile}-each-65-cn0m{cn}"
        signal_parts, truths, bindings = [], [], []
        for path in sorted(scores_root.glob(stem + "-signal-batch*.json")):
            values, truth, binding, report = read_scores(path)
            signal_parts.append(values); truths.extend(truth); bindings.append(binding)
        if not signal_parts:
            raise ValueError("missing signal score batches")
        signal = np.concatenate(signal_parts)
        wrong, _, binding, wrong_report = read_scores(scores_root / (stem + "-wrong-key-batch00.json"))
        bindings.append(binding)
        if len(signal) != 300 or len(wrong) != 32 or any(t not in (0, 1) for t in truths):
            raise ValueError("expected all300 signals and32 wrong-key controls")
        strict = float(wrong_report["family"]["fixed_window_log_threshold"])
        # The relaxed study uses720 attempts/12h and alpha=.01, versus
        #1440/day andalpha=.0001; every other frozen family allocation agrees.
        relaxed = strict - math.log(200.)
        points = [operating_point(signal, truths, wrong, threshold, margin)
            for threshold in (relaxed, strict)
            for margin in (0., 10., 15., 20., 25., 30.)]
        # Threshold-only examples are also post hoc, deliberately not selected.
        points += [operating_point(signal, truths, wrong, threshold, 0.)
                   for threshold in (25., 30., 35., 40.)]
        rows.append({"profile": profile, "input_bindings": bindings,
            "strict_log_threshold": strict, "relaxed_log_threshold": relaxed,
            "signal_score_maximum_quantiles": np.quantile(signal.max(1), [0, .01, .1, .5, 1]).tolist(),
            "signal_margin_quantiles": np.quantile(abs(signal[:, 0]-signal[:, 1]), [0, .01, .1, .5, 1]).tolist(),
            "wrong_key_score_maximum_quantiles": np.quantile(wrong.max(1), [0, .25, .5, .75, 1]).tolist(),
            "wrong_key_margin_quantiles": np.quantile(abs(wrong[:, 0]-wrong[:, 1]), [0, .25, .5, .75, 1]).tolist(),
            "operating_points": points,
            "scope": "post-hoc empirical tradeoff on these reused samples only; no selected new gate, confidence guarantee, continuous-operation wrong-key bound or new qualification"})
    return rows


def _project_full_bank(G, mean):
    values, vectors = np.linalg.eigh((G + G.conj().swapaxes(-1, -2)) * .5)
    keep = values > np.maximum(values[:, -1:], 0.) * 1e-10
    projector = (vectors * keep[:, None, :]) @ vectors.conj().swapaxes(-1, -2)
    projected_mean = (projector @ mean[..., None])[..., 0]
    projected_G = (vectors * np.where(keep, values, 0.)[:, None, :]) @ vectors.conj().swapaxes(-1, -2)
    return projected_G, projected_mean


def covariance_geometry(full_G, mapping, centers, width, window_blocks=64):
    """Hilbert-Schmidt overlap of two bit alternatives, independent windows.

    For K_b=A_b Q A_b.H, tr(K_0 K_1)=tr(Q H_01 Q H_01.H),
    H_01=blockdiag(A_0j.H A_1j). This needs only the full public Gram.
    """
    values = np.zeros(3, dtype=np.float64)
    root_power = np.sqrt(np.outer([.5, .5], [.5, .5]))
    for first in range(0, len(centers), window_blocks):
        stop = min(first + window_blocks, len(centers))
        time = centers[first:stop]
        correlation_squared = np.exp(-(math.pi * width * (time[:, None]-time[None, :]))**2)
        for j, (left, right) in enumerate(((0, 0), (1, 1), (0, 1))):
            H = full_G[first:stop][:, mapping[left]][:, :, mapping[right]]
            flat = (H * root_power[None]).reshape(stop-first, -1)
            term = np.vdot(flat, correlation_squared @ flat).real
            if term < -1e-8:
                raise ValueError("negative covariance inner product")
            values[j] += max(0., float(term))
    denominator = math.sqrt(values[0]*values[1])
    overlap = float(values[2]/denominator) if denominator else None
    if overlap is not None and not -1e-9 <= overlap <= 1+1e-8:
        raise ValueError("covariance overlap outside Cauchy-Schwarz bound")
    return {"trace_covariance_squared_bit0": float(values[0]),
        "trace_covariance_squared_bit1": float(values[1]),
        "trace_covariance_cross": float(values[2]),
        "normalized_covariance_hilbert_schmidt_overlap": overlap,
        "scope": "between exported receive-key bit covariance alternatives, not cross-key covariance; higher overlap is not itself a reception/error probability"}


def path_diagnostic(directory):
    result = retention.diagnose_path(directory)
    if result["status"] != "completed":
        raise ValueError("retention diagnostic did not complete")
    result["input"] = relative(directory)
    ws = BlockWorkspace(directory, max_tile_blocks=1)
    try:
        result["input_hashes"].update({name: sha256(ws.directory/name)
            for name in ("observation.c128", "received_energy.f64")})
        result["key_seed_simulation_only"] = ws.manifest["key_seed"]
        result["random_seed_roots_simulation_only"] = ws.manifest["random_seed_roots"]
        mapping = np.asarray([columns for candidate, columns in zip(ws.candidates, ws.column_mapping)
            if candidate["tap_count"] == 2], dtype=np.int64)
        bits = [candidate["bit"] for candidate in ws.candidates if candidate["tap_count"] == 2]
        if mapping.shape != (2, 2) or bits != [0, 1]:
            raise ValueError("expected one two-ray candidate per bit")
        D, B = ws.dimension, ws.block_count
        G = np.fromfile(ws.directory/"gram.c128", dtype="<c16").reshape(B, D, D)
        mean = np.fromfile(ws.directory/"mean.c128", dtype="<c16").reshape(B, D)
        intervals = np.fromfile(ws.directory/"intervals.u64", dtype="<u8").reshape(B, 2)
        centers = (intervals[:, 0].astype(float) + intervals[:, 1] - 1) / (2*ws.sample_rate)
        G, mean = _project_full_bank(G, mean)
        selected_G = G[:, mapping[:, :, None], mapping[:, None, :]]
        selected_mean = mean[:, mapping]
        width = float(ws.manifest["channel_model"]["path_evolution"]["gaussian_doppler_width_hz"])
        plan = prepare_windowed_gaussian(selected_G, centers, doppler_width_hz=width,
            window_blocks=64, stationary_power=(.5, .5), memory_limit_bytes=256*1024**2)
        variance = float(ws.noise_variance)  # Simulator diagnostic reference ONLY.
        signal_only_score = plan.score(selected_mean, variance_scale=variance, precision="fp64").numpy()
        determinant, expected_noise_quadratic, mean_quadratic, log_lr_variance = np.zeros((4, 2))
        for window in plan._windows:
            lam = window.eigenvalues
            determinant += np.log1p(lam/variance).sum(-1)
            expected_noise_quadratic += (lam/(variance+lam)).sum(-1)
            flat = selected_mean[window.first:window.stop].transpose(1, 0, 2).reshape(2, -1)
            z = np.einsum('cn,cnr->cr', flat, window.transform.conj())
            mean_quadratic += (abs(z)**2/(variance*(variance+lam))).sum(-1)
            log_lr_variance += ((lam/(variance+lam))**2 +
                2*abs(z)**2*lam/(variance*(variance+lam)**2)).sum(-1)
        if not np.allclose(signal_only_score, mean_quadratic-determinant, rtol=2e-12, atol=2e-10):
            raise ValueError("expected-score decomposition differs from actual FP64 score helper")
        result["known_variance_expected_lr_diagnostic"] = {
            "variance_simulation_truth_only": variance,
            "mean_quadratic_by_bit": mean_quadratic.tolist(),
            "expected_noise_quadratic_by_bit": expected_noise_quadratic.tolist(),
            "log_determinant_by_bit": determinant.tolist(),
            "expected_log_lr_conditioned_on_fixed_noiseless_waveform_by_bit": (mean_quadratic+expected_noise_quadratic-determinant).tolist(),
            "standard_deviation_over_thermal_noise_by_bit": np.sqrt(log_lr_variance).tolist(),
            "expected_noise_only_log_lr_by_bit": (expected_noise_quadratic-determinant).tolist(),
            "scope": "analytic expectation over fresh white Gaussian thermal noise for this fixed actual noiseless waveform and receiver alternative; uses noise truth only as diagnostic; not observed UCB score, admission probability, physical channel average or new gate"}
        result["public_bit_covariance_geometry"] = covariance_geometry(G, mapping, centers, width)
        result["fine_symbol_seconds_over_gaussian_efold_seconds"] = (
            math.pi*width/(math.sqrt(2)*ws.manifest["source_format"]["fine_rate_hz"]))
        result["memory_scope"] = "one small fullD=4 capture statistics cache plus bounded128-dimensional window factors; no raw sampled waveform history"
        return result
    finally:
        retention._release_maps(ws)


def self_check():
    """Tiny independent raw-observation covariance/mean oracle, no campaign data."""
    rng = np.random.default_rng(144)
    blocks, rows, dimension = 3, 7, 4
    times, width = np.arange(blocks, dtype=float), .12
    A = (rng.normal(size=(blocks, rows, dimension)) +
         1j*rng.normal(size=(blocks, rows, dimension)))/math.sqrt(2)
    G = np.einsum('bni,bnj->bij', A.conj(), A)
    mapping = np.asarray([[0, 2], [1, 3]])
    found = covariance_geometry(G, mapping, times, width, window_blocks=3)
    Q = np.kron(np.exp(-.5*(math.pi*width*(times[:, None]-times[None, :]))**2), np.eye(2)*.5)
    covariances = []
    for candidate in range(2):
        basis = np.zeros((blocks*rows, blocks*2), np.complex128)
        for j in range(blocks):
            basis[j*rows:(j+1)*rows, j*2:(j+1)*2] = A[j][:, mapping[candidate]]
        covariances.append(basis @ Q @ basis.conj().T)
    oracle = [np.trace(covariances[0]@covariances[0]).real,
              np.trace(covariances[1]@covariances[1]).real,
              np.trace(covariances[0]@covariances[1]).real]
    measured = [found["trace_covariance_squared_bit0"], found["trace_covariance_squared_bit1"],
                found["trace_covariance_cross"]]
    if not np.allclose(oracle, measured, rtol=3e-13, atol=1e-12):
        raise ValueError("raw covariance geometry self-check failed")
    waveform = (rng.normal(size=(blocks, rows)) + 1j*rng.normal(size=(blocks, rows)))/math.sqrt(2)
    mean = np.einsum('bni,bn->bi', A.conj(), waveform)
    variance = 2.3
    plan = prepare_windowed_gaussian(G[:, mapping[:, :, None], mapping[:, None, :]], times,
        doppler_width_hz=width, window_blocks=3)
    found_mean = plan.score(mean[:, mapping], variance_scale=variance, precision="fp64").numpy()
    oracle_mean = []
    for K in covariances:
        R = np.eye(blocks*rows) + K/variance
        W = (np.eye(blocks*rows)-np.linalg.inv(R))/variance
        flat = waveform.reshape(-1)
        oracle_mean.append(float(np.vdot(flat, W@flat).real-np.linalg.slogdet(R)[1]))
    if not np.allclose(found_mean, oracle_mean, rtol=3e-12, atol=1e-12):
        raise ValueError("raw Gaussian mean-quadratic self-check failed")
    counts = operating_point(np.asarray([[30., 0.], [40., 30.]]), [0, 1],
        np.asarray([[23., 22.]]), 22., 15.)
    if (counts["correct_accepted"], counts["missed"], counts["wrong_key_admitted"]) != (1, 1, 0):
        raise ValueError("score accounting self-check failed")
    return {"dense_raw_covariance_geometry": "passed", "dense_raw_gaussian_mean_score": "passed",
            "synthetic_operating_point_accounting": "passed"}


def paired_control_checks(diagnostics):
    pairs = []
    for profile in ("g01slowrate", "g1slowrate"):
        rows = [row for row in diagnostics if f"/{profile}-" in row["input"]]
        positive = next(row for row in rows if row["signal_control"] == "signal")
        wrong = next(row for row in rows if row["signal_control"] == "wrong-key")
        checks = {name: positive["input_hashes"][name] == wrong["input_hashes"][name]
            for name in ("signal_energy.f64", "received_energy.f64", "intervals.u64", "valid_rows.u64")}
        checks["key_seed"] = positive["key_seed_simulation_only"] == wrong["key_seed_simulation_only"]
        checks["seed_roots"] = positive["random_seed_roots_simulation_only"] == wrong["random_seed_roots_simulation_only"]
        checks["receive_basis_changed"] = positive["input_hashes"]["gram.c128"] != wrong["input_hashes"]["gram.c128"]
        if not all(checks.values()):
            raise ValueError("paired control binding differs beyond receive-key basis")
        pairs.append({"profile": profile, "checks": checks,
            "scope": "identical source/noise seed declarations and signal/received energy/calendars, changed receive basis; these energy hashes alone do not prove sample-by-sample raw waveform identity"})
    return pairs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-root", type=Path, required=True)
    parser.add_argument("--scores-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    inputs = sorted(args.raw_root.rglob("manifest.json"))
    if len(inputs) != 6:
        raise ValueError("expected exactly six retained diagnostic workspaces")
    checks = self_check()
    diagnostics = [path_diagnostic(p.parent) for p in inputs]
    report = {"schema": "triad-posthoc-wrong-key-diagnostic-v1",
        "source_sha256": sha256(__file__),
        "focused_self_checks": checks,
        "dependency_sha256": {name: sha256(REPOSITORY/"tools"/name) for name in (
            "triad-retained-energy.py", "triad_gaussian_window.py", "triad_state_workspace.py")},
        "warning": "Post-hoc diagnosis only. Primary gates/results remain unchanged. Noise-only LR calibration never promised rejection of arbitrary wrong-key signals.",
        "score_tradeoffs": score_tradeoffs(args.scores_root),
        "raw_workspace_diagnostics": diagnostics,
        "paired_control_integrity": paired_control_checks(diagnostics),
        "limitations": ["Retained-energy fits allow free complex coefficients per block and are optimistic ceilings, not achievable gains.",
            "The six raw examples are selected diagnostics, not an independent performance sample.",
            "Original-rate raw examples have different seeds/C/N0 than slower examples; comparisons do not isolate rate causally.",
            "A score-margin requirement is an extra rejection rule; its post-hoc tradeoff is not a wrong-key false-admission guarantee.",
            "A wider physical signal null or validated minimum distinguishable pattern rate/complexity is needed before claiming generic key identification at these slower rates."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    print(json.dumps({"output": str(args.output), "raw_paths": len(inputs)}))


if __name__ == "__main__":
    main()

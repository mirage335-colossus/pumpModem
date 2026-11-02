#!/usr/bin/env python3
"""Fixed-codeword/window-bit-switch contrast: an UNCALIBRATED proxy only.

UCB likelihoods and a post-data best group do not establish a wrong-key Bayes
factor or authentication. Predeclared Cwinner>=0 and >=log(100) AND guards
can only remove existing admissions; that subset preserves any valid existing
noise-only bound, without supplying a wrong-key or correct-reception guarantee.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import sys
import time
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT/"tools"))
import triad_group_acquire as group
from triad_gaussian_window import PreparedWindowedGaussian


def contrast(d):
    d = np.asarray(d, dtype=np.float64)
    if not np.isfinite(d).all():
        raise ValueError("nonfinite window contrasts")
    return np.array([(math.log(2)-np.logaddexp(0, d)).sum(),
                     (math.log(2)-np.logaddexp(0, -d)).sum()])


def window_scores(plan, correlations, upper):
    scores = []
    one = copy.copy(plan)  # Retain the same factors/calendar; change only loop membership.
    for window in plan._windows:
        one._windows = (window,)
        scores.append(one.score(correlations, variance_scale=upper, precision="fp32").numpy())
    return np.asarray(scores)


def selftest():
    assert np.array_equal(contrast(np.zeros(5)), [0., 0.])
    single = contrast([2.])
    assert np.allclose(single, [math.log(2)-np.logaddexp(0, 2), math.log(2)-np.logaddexp(0, -2)], atol=1e-15)
    assert np.allclose(contrast([2.]*7), 7*single, atol=1e-14)
    assert np.allclose(contrast([-3., 1., 2.]), contrast([3., -1., -2.])[::-1], atol=1e-14)
    grams = np.broadcast_to(np.eye(2), (8, 2, 2, 2)).copy().astype(complex)
    rng = np.random.default_rng(2026110231)
    r = (rng.standard_normal((8, 2, 2))+1j*rng.standard_normal((8, 2, 2)))/np.sqrt(2)
    plan = PreparedWindowedGaussian(grams, np.arange(8)*.01, doppler_width_hz=30., window_blocks=2)
    error = float(np.max(abs(window_scores(plan, r, 2.).sum(0)-plan.score(r, variance_scale=2., precision="fp32").numpy())))
    assert error < 1e-12
    return {"algebra_cases": 4, "whole_plan_aggregation_max_error": error}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workspace", type=Path, required=True)
    p.add_argument("--full-score", type=Path, required=True)
    p.add_argument("--full-score-sha256", required=True)
    p.add_argument("--doppler-width-hz", type=float, choices=(10., 30.), required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args(); started = time.perf_counter(); torch.set_num_threads(1)
    checks = selftest()
    full_hash = group.sha256(args.full_score)
    if full_hash != args.full_score_sha256:
        raise ValueError("full-score SHA256 differs from required binding")
    full = json.loads(args.full_score.read_text())
    assert full["format"] == "triad-group-acquisition-campaign-v1" and len(full["captures"]) == 1
    assert full["receiver_source_sha256"] == group.sha256(group.__file__)
    prior = full["captures"][0]; workspace = group.GroupedWorkspace(args.workspace)
    identity = {name: group.sha256(args.workspace/name) for name in group.INPUT_FILES}
    assert identity == prior["input_sha256"]
    assert prior["method"] == "group-local-gaussian-window-ucb" and prior["mode"] == "actual-noisy"
    geometry = prior["geometry"]
    assert geometry["reblock"] == 1 and geometry["base_block_samples"] == 16 and geometry["nominal_window_seconds"] == .25
    assert prior["arithmetic"]["bulk"] == "fp32" and prior["calibration"]["known_noise_variance_diagnostic"] is None
    all_scores = np.asarray(prior["group_bit_log_likelihoods"])
    mixed = group.bit_mixture(all_scores)
    assert np.max(abs(mixed-np.asarray(prior["bit_log_likelihoods"]))) < 1e-10
    winner = int(np.argmax(mixed)); best = int(np.argmax(all_scores[:, winner]))
    assert best == prior["best_scoring_group_for_winning_bit"]
    assert all(prior[g]["bit"] == winner for g in ("strict", "relaxed"))
    intervals, valid, energy = workspace.calendar(1)
    G, r = workspace.read_group_tile(best, best+1, 1)
    calibration = prior["calibration"]
    corr, grams, upper, residual, dof, ranks = group.prepare_group_tile(G, r, energy, valid,
        workspace.mapping[best:best+1], reference=calibration["reference_variance"], delta=calibration["delta_per_group"])
    assert np.allclose(upper[0], calibration["variance_upper_over_reference"][best], rtol=1e-12, atol=0)
    centers = (intervals[:, 0].astype(float)+intervals[:, 1].astype(float))/(2*workspace.sample_rate)
    plan = PreparedWindowedGaussian(grams[:, 0], centers, doppler_width_hz=args.doppler_width_hz,
        window_blocks=geometry["window_blocks"], stationary_power=(.5, .5))
    windows = window_scores(plan, corr[:, 0], upper[0])
    aggregate = windows.sum(0); difference = aggregate-all_scores[best]
    assert np.max(abs(difference)) < 1e-5
    direct = plan.score(corr[:, 0], variance_scale=upper[0], precision="fp32").numpy()
    assert np.max(abs(aggregate-direct)) < 1e-10
    d = windows[:, 1]-windows[:, 0]; C = contrast(d)
    guards = {label: {"threshold": threshold, "contrast_passes": bool(C[winner] >= threshold),
        "and_existing_gate": {g: bool(prior[g]["accepted"] and C[winner] >= threshold) for g in ("strict", "relaxed")}}
        for label, threshold in (("zero", 0.), ("ln100", math.log(100)))}
    workspace.verify_unchanged()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    npz = args.output.with_suffix(".windows.npz")
    np.savez(npz, window_log_likelihoods=windows, d=d, contrast=C)
    result = {"scope": __doc__, "status": "complete", "checks": checks, "winner_bit_unchanged": winner,
        "best_group": best, "window_count": len(windows), "contrast": C.tolist(), "guards": guards,
        "difference_sign_counts": {"positive": int((d > 0).sum()), "negative": int((d < 0).sum()), "zero": int((d == 0).sum())},
        "window_ll_sums": aggregate.tolist(), "baseline_difference": difference.tolist(), "sum_d": float(d.sum()),
        "configuration": {"doppler_width_hz": args.doppler_width_hz, "window_seconds": .25, "block_samples": 16, "precision": "fp32"},
        "labels_read_after_scoring": {"control": workspace.manifest.get("control"), "truth": workspace.manifest.get("signal_bit_simulation_truth")},
        "hashes": {"driver": group.sha256(__file__), "full_score": full_hash, "receiver": group.sha256(group.__file__),
            "inputs": identity, "per_window_npz": group.sha256(npz)}, "elapsed_seconds": time.perf_counter()-started}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    print(json.dumps({k: result[k] for k in ("winner_bit_unchanged", "best_group", "contrast", "guards", "elapsed_seconds")}))


if __name__ == "__main__":
    main()

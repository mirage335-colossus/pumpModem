#!/usr/bin/env python3
"""Optional focused Torch regressions for isolated triad study tools.

Run explicitly, outside the ordinary receiver/CMake campaign:
  python3 tests/test_triad_gpu.py --device cpu
  python3 tests/test_triad_gpu.py --device cuda:1
Requires an existing Torch installation; does not install dependencies.
"""
from __future__ import annotations

import argparse
import copy
from decimal import Decimal, localcontext
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import traceback


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def expect_value_error(function, message):
    try:
        function()
    except ValueError:
        return
    raise AssertionError(message)


def gamma_tails(gpu):
    # Independent integer-shape survival formulas, evaluated with 70 decimal
    # digits, check the numerical quantile without reusing gammaincc.
    rows = []
    with localcontext() as context:
        context.prec = 70
        for rank in (1, 2):
            for x in (math.log(100), math.log(1e9), 100.):
                threshold, method, reason = gpu._gamma_threshold(rank, x, "exact")
                t, exponent = Decimal.from_float(threshold), Decimal.from_float(x)
                tail = (-t).exp() * (1 + t if rank == 2 else 1)
                target = (-exponent).exp()
                relative = float(tail / target - 1)
                require(reason is None, f"unexpected Gamma fallback: {reason}")
                require(-5e-10 < relative < 2e-12,
                        f"Gamma({rank}) survival does not bracket requested tail: {relative}")
                conservative, _, _ = gpu._gamma_threshold(rank, x, "conservative")
                require(conservative > threshold, "concentration threshold did not exceed exact quantile")
                rows.append({"rank": rank, "log_reciprocal_tail": x,
                             "relative_survival_error": relative, "method": method})
        threshold, method, reason = gpu._gamma_threshold(1, 1000., "exact")
        require(threshold >= 1000 and threshold - 1000 < 1e-9 and reason is None,
                "rank-one log-tail handling lost extreme-tail accuracy")
        threshold, method, reason = gpu._gamma_threshold(2, 1000., "exact")
        require(method == "gamma_concentration_fallback" and reason is not None,
                "unrepresentable FP64 survival did not explicitly fall back")
        t = Decimal.from_float(threshold)
        require((-t).exp() * (1 + t) < Decimal(-1000).exp(),
                "extreme-tail fallback violated independent rank-two survival bound")
    expect_value_error(lambda: gpu._gamma_threshold(0, 4., "exact"), "zero rank accepted")
    return {"tail_checks": rows, "extreme_tail_fallback": method}


def outcome_counts(torch, gpu):
    best = gpu._new_best(6)
    best[0, 0] = torch.tensor([1., 1., 1., 1., 1., -1.])
    best[0, 1] = torch.tensor([-1., 0., -2., 1., -1., -1.])
    best[2, 0] = torch.tensor([0., 1., 3., 2., 0., 1.])
    best[3, 0] = torch.tensor([2., 2., 2., 2., 1., 2.])
    actual = gpu._metrics(best, 0, [0, 0, 1, 0], 0, 2, [0, 0, 0, 1])
    expected = {"positive_trials": 6, "correct_accepted": 4, "wrong_accepted": 1,
                "missed": 1, "same_attempt_epoch_correct_accepted": 2,
                "off_attempt_admissions": 1, "wrong_epoch_admissions": 1,
                "exact_nuisance_candidate_selected": 2,
                "exact_nuisance_candidate_accepted": 2, "null_admissions": 2}
    for key, value in expected.items():
        require(actual[key] == value, f"{key}: expected {value}, got {actual[key]}")
    unknown = gpu._metrics(best, None, [0, 0, 1, 0], None)
    require(unknown["correct_accepted"] is None and unknown["wrong_accepted"] is None and
            unknown["positive_admissions_unclassified"] == 5,
            "unknown source bit was incorrectly classified")
    return expected


def empirical_and_metadata(torch, paired):
    threshold, order = paired.empirical_threshold(torch.arange(4096, dtype=torch.float64), .01)
    require(threshold == 4056 and order == 4057,
            "independent 1% order statistic used the wrong rank")
    expect_value_error(lambda: paired.empirical_threshold(torch.arange(98), .01),
                       "insufficient calibration data accepted")
    original = {"source_format": {"version": 3}, "channel_model": {"delay": .0015},
                "sample_rate": 1024, "signal_shape": [8192], "signal_bit": 0,
                "seed": 1949, "epoch": 1800000000, "channel": "hf", "control": "signal",
                "command_argv": ["./triad_long", "--mode", "export", "--dropout-ms", "10", "--output", "a"],
                "truth_arrival_seconds": .05, "truth_frequency_hz": 0,
                "truth_clock_ppm": 0, "truth_frequency_drift_hz_per_second": 0}
    opposite = copy.deepcopy(original)
    opposite["signal_bit"] = 1
    opposite["command_argv"] = ["/other/triad_long", "--mode", "export", "--dropout-ms", "10",
                                "--output", "b", "--signal-bit", "1"]
    paired.validate_opposite_manifest(original, opposite)
    for field, value in (("sample_rate", 1025), ("seed", 1950), ("epoch", 1800000001),
                         ("signal_bit", 0), ("channel_model", {"delay": .003})):
        bad = copy.deepcopy(opposite)
        bad[field] = value
        expect_value_error(lambda bad=bad: paired.validate_opposite_manifest(original, bad),
                           f"opposite-bit {field} mismatch accepted")
    bad = copy.deepcopy(opposite)
    bad["command_argv"][4] = "11"
    expect_value_error(lambda: paired.validate_opposite_manifest(original, bad),
                       "unmatched dropout recipe was accepted")
    return {"order_statistic_rank": order, "metadata_mismatches_rejected": 6}


def near_collinear(torch, gpu, paired, device):
    # Original failure fixture: FP32 and FP64 both report rank two, while the
    # FP32 projection subspace is wrong by about 0.005 in Frobenius norm.
    generator = torch.Generator(device="cpu").manual_seed(1949)
    a = torch.complex(torch.randn((2, 4096), generator=generator),
                      torch.randn((2, 4096), generator=generator))
    b = torch.complex(torch.randn((2, 4096), generator=generator),
                      torch.randn((2, 4096), generator=generator))
    basis = torch.stack((a, a + 1e-5 * b), dim=-1)
    reference, reference_ranks = gpu._modified_gram_schmidt(basis.to(torch.complex128))
    selected, selected_ranks = gpu._modified_gram_schmidt(basis.to(device))
    before = gpu._subspace_distances(reference, selected.to(device="cpu", dtype=torch.complex128))
    tolerance = 3.2e-5
    require(bool((reference_ranks == 2).all()) and bool((selected_ranks == 2).all()),
            "near-collinear fixture no longer exercises same-rank geometry error")
    require(bool((before > tolerance).all()),
            f"fixture did not reproduce same-rank projection error on this backend: {before.tolist()}")
    fixed, fixed_ranks, checks = paired.verified_projector(basis.to(device))
    after = gpu._subspace_distances(reference, fixed.to(device="cpu", dtype=torch.complex128))
    require(bool((fixed_ranks == 2).all()) and bool((after < tolerance).all()),
            "verified projector did not preserve the FP64 subspace")
    require(checks["same_rank_fallback_candidates"] == 2,
            "same-rank geometry fallback did not cover both failed candidates")
    return {"ranks": fixed_ranks.cpu().tolist(), "distance_before": before.tolist(),
            "distance_after": after.tolist(), "same_rank_fallback_candidates": 2,
            "projector_distance_tolerance": tolerance}


def long_constant(torch, gpu, device):
    # N crosses the old eps*N cutoff. A rank-one constant observation must
    # retain unit projection energy regardless of observation length.
    samples = 2 ** 23 + 2
    basis = torch.ones((1, samples, 1), dtype=torch.complex64, device=device)
    q, ranks = gpu._modified_gram_schmidt(basis)
    require(int(ranks[0]) == 1, "long rank-one column was incorrectly discarded")
    norm = float(q.abs().square().sum(dtype=torch.float64))
    response = float(q.real.sum(dtype=torch.float64) / math.sqrt(samples))
    require(abs(norm - 1) < 2e-5 and abs(response - 1) < 2e-5,
            f"long constant projection lost energy: norm={norm}, response={response}")
    return {"samples": samples, "rank": int(ranks[0]),
            "projector_energy": norm, "unit_signal_response": response}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()
    try:
        import torch
    except ImportError:
        print(json.dumps({"status": "unavailable", "reason": "Torch must already be installed; no tensor checks ran"}))
        return 2
    root = Path(__file__).resolve().parents[1]
    gpu = load_module("triad_gpu_regression", root / "tools/triad-gpu-study.py")
    paired = load_module("triad_paired_regression", root / "tools/triad-detector-study.py")
    gpu._configure_torch()
    device = gpu._resolve_device(args.device)
    if device.type == "cuda":
        # Device discovery does not initialize the allocator/context. In a
        # fresh process reset_peak_memory_stats(cuda:N) can otherwise fail
        # before any regression fixture executes.
        torch.cuda.set_device(device)
        initialization = torch.empty(1, device=device)
        torch.cuda.synchronize(device)
        del initialization
        torch.cuda.reset_peak_memory_stats(device)
    tests = [("integer_gamma_tails", lambda: gamma_tails(gpu)),
             ("bit_attempt_epoch_outcomes", lambda: outcome_counts(torch, gpu)),
             ("calibration_and_opposite_provenance", lambda: empirical_and_metadata(torch, paired)),
             ("same_rank_subspace_fallback", lambda: near_collinear(torch, gpu, paired, device)),
             ("long_rank_one_column", lambda: long_constant(torch, gpu, device))]
    started = time.perf_counter()
    for name, function in tests:
        begin = time.perf_counter()
        try:
            details = function()
            if device.type == "cuda":
                torch.cuda.synchronize(device)
        except Exception as error:
            print(json.dumps({"test": name, "status": "failed", "error": str(error),
                              "seconds": time.perf_counter() - begin}), flush=True)
            traceback.print_exc()
            return 1
        print(json.dumps({"test": name, "status": "passed", "seconds": time.perf_counter() - begin,
                          "details": details}, allow_nan=False), flush=True)
    print(json.dumps({"status": "passed", "tests": len(tests), "device": str(device),
                      "torch_version": torch.__version__, "seconds": time.perf_counter() - started,
                      "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated(device) if device.type == "cuda" else None}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

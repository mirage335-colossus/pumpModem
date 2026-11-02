#!/usr/bin/env python3
"""Freeze receiver alternatives over five existing sampled development cases.

The exact-B/J64 jobs in screen-score-plan.json are the baseline; do not replay
them here. Preserve input ordering, seed, draw batch, controls, C/N0 grid and
calibration allocation so the conditional noise and UCB remain paired.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


CASES = ("g001-each-65", "g01-each-65", "g1-each-65",
         "g1-slowrate-each-65", "g1-fastam-each-65")
VARIANTS = (("gaussian-j16", 16, 1.), ("gaussian-j128", 128, 1.),
            ("gaussian-bhalf-j64", 64, .5), ("gaussian-bdouble-j64", 64, 2.))


def option(argv: list[str], name: str) -> str:
    return argv[argv.index(name) + 1]


def set_option(argv: list[str], name: str, value: str) -> None:
    argv[argv.index(name) + 1] = value


def make_plan(source: dict, source_hash: str) -> dict:
    baseline_jobs = {job["name"]: job for job in source["jobs"]}
    jobs = []
    comparisons = []
    for case in CASES:
        baseline = baseline_jobs[case]
        original = baseline["argv"]
        if original[original.index("--methods") + 1] != "gaussian-window-ucb":
            raise ValueError("expected Gaussian-window baseline: " + case)
        expected = {"--noise-trials": "16", "--trial-batch": "16", "--seed": "231203",
                    "--selection-methods": "16", "--duration-alternatives": "8",
                    "--calibration-alternatives": "2", "--attempt-spacing": "60",
                    "--window-blocks": "64", "--threads": "1", "--memory-gib": "8",
                    "--device": "cuda:0", "--precision": "fp32"}
        if any(option(original, key) != value for key, value in expected.items()):
            raise ValueError("screen seed/draw/calibration contract changed: " + case)
        inputs = original[2:original.index("--output")]
        if len(inputs) != 4 or [Path(p).name for p in inputs] != [f"path-{i:05d}" for i in range(40000, 40004)]:
            raise ValueError("expected four ordered screen paths: " + case)
        if original[original.index("--controls") + 1:original.index("--methods")] != ["signal", "null"]:
            raise ValueError("expected signal/null control order: " + case)
        width = float(option(original, "--window-doppler-width"))
        if not math.isfinite(width) or width <= 0:
            raise ValueError("expected positive Gaussian width: " + case)
        baseline_report = Path(option(original, "--output"))
        report_root = baseline_report.parent.parent / "alternatives-scores"
        alternatives = [{"name": "gaussian-j64", "methods": ["gaussian-window-ucb"],
                         "window_blocks": 64, "receiver_width_hz": width,
                         "report": str(baseline_report), "existing_baseline": True}]
        for variant, blocks, scale in VARIANTS:
            name = case + "-" + variant
            argv = original.copy()
            chosen_width = width * scale
            report = report_root / (name + ".json")
            set_option(argv, "--output", str(report))
            set_option(argv, "--window-blocks", str(blocks))
            set_option(argv, "--window-doppler-width", str(chosen_width))
            set_option(argv, "--coherence", str(math.sqrt(2) / (math.pi * chosen_width)))
            jobs.append({"name": name, "timeout_seconds": 600, "argv": argv})
            alternatives.append({"name": variant, "methods": ["gaussian-window-ucb"],
                                 "window_blocks": blocks, "receiver_width_hz": chosen_width,
                                 "report": str(report), "existing_baseline": False})
        argv = original.copy()
        method_index = argv.index("--methods") + 1
        argv[method_index:method_index + 1] = ["gaussian-ou-ucb", "static-fft-ucb"]
        name = case + "-ou-static"
        report = report_root / (name + ".json")
        set_option(argv, "--output", str(report))
        set_option(argv, "--device", "cpu")
        set_option(argv, "--precision", "fp64")
        # Both baselines ignore these Gaussian-window-only controls. Remove
        # them so their report configuration does not imply a window receiver.
        for flag in ("--window-blocks", "--window-doppler-width"):
            index = argv.index(flag)
            del argv[index:index + 2]
        jobs.append({"name": name, "timeout_seconds": 600, "argv": argv})
        alternatives.extend([
            {"name": "gaussian-ou", "methods": ["gaussian-ou-ucb"],
             "coherence_seconds": float(option(argv, "--coherence")), "report": str(report),
             "existing_baseline": False},
            {"name": "static-fft", "methods": ["static-fft-ucb"], "report": str(report),
             "existing_baseline": False},
        ])
        comparisons.append({"case": case, "inputs": inputs, "physical_gaussian_width_hz": width,
                            "alternatives": alternatives, "receiver_alternatives_including_baseline": 7})
    assert len(jobs) == 25
    return {
        "schema": "triad-selective-alternatives-plan-v1",
        "stage": "paired development receiver comparison; not qualification",
        "source_screen_score_plan_sha256": source_hash,
        "scope": "Five existing four-path screen workspaces, seven receiver alternatives per fixed case including its already-running exact-B/J64 baseline. No new physical exports.",
        "bounds": {"new_jobs": 25, "per_job_deadline_seconds": 600,
                   "sum_of_individual_deadline_seconds": 15000,
                   "threads_per_job": 1, "memory_gib_per_job": 8,
                   "selection_methods": 16, "duration_alternatives": 8,
                   "calibration_alternatives": 2, "noise_trials": 16, "trial_batch": 16,
                   "seed": 231203},
        "pairing_contract": [
            "Keep the exact four source paths and their order, seed231203, batch16, controls signal/null, C/N0 grid and admission/calibration allocation from screen-score-plan.json.",
            "Conditional draw seed addresses use SeedSequence([seed,path_index,first_trial]); they do not use receiver method, width, window length, control or C/N0.",
            "The source arrays, sampled design and whole-bank residual UCB remain fixed across receiver alternatives. Simulation generates the paired observations; receiver code does not consume waveform means or physical truth.",
            "OU and static methods share one generated conditional batch within their combined job. Separate Gaussian jobs reproduce the same draws from the preserved addresses and batch shape.",
        ],
        "interpretation_limits": [
            "These are conditional draws on the existing four physical paths, not extra independent channels or a holdout campaign.",
            "B/2 and 2B change only the receiver covariance width. The physical channel and exported block compression remain unchanged.",
            "Gaussian windows assume independent windows. Longer J tests that approximation; it does not recover information already discarded within each exported block.",
            "OU uses tau=sqrt(2)/(pi*B), matching the Gaussian covariance e-fold time only. Its exponential covariance and Lorentzian spectrum do not have the Gaussian physical spectrum or its finite RMS width.",
            "Gaussian-OU and Gaussian-window use zero residual CFO; static FFT integrates its existing public small CFO grid. The comparison includes those model differences.",
            "Gaussian alternatives preserve strict FP32 CUDA bulk and FP64 preparation/evidence. OU/static use CPU FP64 to avoid repeated tiny GPU state-update launches; this is not a controlled same-hardware throughput comparison.",
            "Seven receiver alternatives fit K16 for each fixed declared case. K16 does not cover unrestricted post-hoc model/rate/duration searches or simultaneous selection across all five different physical configurations.",
            "Known screen limitations remain: nominal initial timing branch, fixed public channel parameters, incomplete physical-path reliability and daily false-admission-tail qualification.",
        ],
        "cost_cautions": [
            "At fixed total block count, dense full-rank J128 preparation has approximate O(blocks*J^2) work: about 4x J64; retained factors scale approximately O(blocks*J): about 2x. These are scaling estimates, not timings or guaranteed effective rank.",
            "J16 reduces that asymptotic preparation work to about 1/16 of J64 and factor storage to about 1/4, at the cost of shorter independent covariance windows.",
            "OU processes every block sequentially for every condition; the roughly 10,000-block cases can hit their 600s job deadline. A deadline failure is an unresolved computational result, not a sensitivity failure.",
            "Per-job 8GiB is the receiver array allowance, not measured process RSS or aggregate parallel memory. Workspace loading performs the existing actual-memory preflight before scoring.",
            "25 individual 600s deadlines sum to 15000s sequentially. This is a hard-stop envelope, not a runtime forecast or an authorization to exceed the enclosing cloud reservation.",
        ],
        "comparisons": comparisons,
        "jobs": jobs,
    }


def main() -> None:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screen-score-plan", type=Path, default=here / "screen-score-plan.json")
    parser.add_argument("--output", type=Path, default=here / "alternatives-score-plan.json")
    args = parser.parse_args()
    source_bytes = args.screen_score_plan.read_bytes()
    plan = make_plan(json.loads(source_bytes), hashlib.sha256(source_bytes).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate frozen independent actual-noisy G1 export and score plans."""
import argparse
import json
import math
from pathlib import Path


def build(root: Path, python: str):
    exports, scores, cases = [], [], []
    cohorts = [(8640, (-14, -12, -10), 300),
               (86400, (-18, -16, -14), 32)]
    data_root = root / "local-evidence/spread/g1-holdout"
    score_root = root / "local-evidence/spread/g1-holdout-scores"
    for seconds, cn0_values, paths in cohorts:
        for cn0 in cn0_values:
            name = f"g1-t{seconds}-cn0m{-cn0}"
            case = {
                "name": name, "seconds": seconds, "cn0_db_hz": cn0,
                "paths": paths, "start_index": 20000,
                "rate_hz": 3.6, "added_symbols": 1,
                "sample_rate_hz": 16, "block_seconds": 1,
                "spread_preset": "sub9-best", "delay_ms": 0.5,
                "center_doppler_hz": 0.1, "gaussian_width_hz": 0.1,
                "receiver_tap_delay_ms": 0.5,
                "receiver_tap_doppler_hz": 0.1,
                "gaussian_correlation_e_fold_seconds": math.sqrt(2) / (math.pi * 0.1),
                "export_jobs": 2,
            }
            cases.append(case)
            output = data_root / name
            export_argv = [
                python, str(root / "tools/triad-lowrate-campaign.py"),
                "--binary", str(root / "build/dev-cli/triad_long"),
                "--output", str(output), "--split", "holdout",
                "--control", "signal", "--paths", str(paths),
                "--start-index", "20000", "--jobs", "2",
                "--timeout", "900", "--term-grace", "2",
                "--seconds", str(seconds), "--rate", "3.6",
                "--sample-rate", "16", "--sample-rate-denominator", "1",
                "--cn0", str(cn0), "--rf-hz", "8270",
                "--spread-preset", "sub9-best",
                "--spread-delay-ms", "0.5", "--spread-doppler-hz", "0.1",
                "--tap-delay-ms", "0.5", "--tap-doppler-hz", "0.1",
                "--ray-gaussian-width-hz", "0.1",
                "--selective-perturbation", "off",
                "--block-seconds", "1",
            ]
            exports.append({"name": name, "timeout_seconds": 1800,
                            "argv": export_argv})

            inputs = [str(output / f"path-{i:05d}")
                      for i in range(20000, 20000 + paths)]
            score_argv = [
                python, str(root / "tools/triad-conditional-sweep.py"),
                *inputs,
                "--output", str(score_root / f"{name}.json"),
                "--mode", "actual-noisy",
                "--methods", "gaussian-window-ucb",
                "--window-doppler-width", "0.1", "--window-blocks", "64",
                "--reference-variance", "1", "--frequency-bound", "0.000001",
                "--frequency-initialization", "midpoint-grid",
                "--phase-diffusion", "0", "--frequency-random-walk", "0",
                "--particles", "64", "--resampling", "none", "--taps", "2",
                "--device", "cpu", "--precision", "fp64", "--threads", "1",
                "--memory-gib", "4", "--seed", "231102",
                "--false-probability", "0.0001", "--horizon-seconds", "86400",
                "--correct-reception-target", "0.99",
                "--attempt-spacing", "60", "--duration-alternatives", "8",
                "--selection-methods", "16", "--calibration-alternatives", "1",
            ]
            scores.append({"name": name, "timeout_seconds": 1800,
                           "argv": score_argv})
    return cases, exports, scores


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    p.add_argument("--python", default="/workspace/triad-env/bin/python")
    p.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent)
    args = p.parse_args()
    root = args.root.resolve()
    out = args.output_dir.resolve()
    destinations = (out / "g1-export-plan.json", out / "g1-score-plan.json")
    if any(path.exists() for path in destinations):
        p.error("refusing to overwrite a G1 plan")
    cases, exports, scores = build(root, args.python)
    shared = {
        "schema": "triad-spread-g1-holdout-plan-v1",
        "selection": (
            "Independent holdout split with fresh start indices 20000+; balanced source bits. "
            "GUI-style Rate 3.6, Added Symbols 1, Fs=16 Hz, one-second correlation blocks, "
            "two-ray nominal delay/center matched to the frozen Gaussian window width 0.1 Hz. "
            "The C/N0 operating points were selected from development before these holdouts; "
            "holdout scores are not to be used for tuning or optional stopping."
        ),
        "seed_policy": (
            "triad-lowrate-campaign-v1 derives independent key/channel/oscillator/noise/frontend/path "
            "roots from split, requested duration, rate, sample-rate numerator, C/N0, global index, "
            "bit and domain. Duration/C/N0/index distinguish every G1 cohort; indices are 20000+ "
            "and each even-sized cohort balances bits."
        ),
        "cases": cases,
        "targets": {
            "strict_correct": 0.99,
            "strict_noise_any_per_day": 0.0001,
            "relaxed_correct": 0.90,
            "relaxed_noise_any_per_12hours": 0.01,
            "method_alternatives": 16,
            "duration_alternatives": 8,
            "attempt_seconds": 60,
            "score_gate": "strict 99% / 1e-4 per-day family; relaxed gate not separately emitted by this score job",
        },
        "execution": {
            "planned_outer_parallel_jobs": 3,
            "export_inner_jobs_per_cohort": 2,
            "all_jobs_have_1800_second_outer_timeout": True,
            "no_optional_stopping": True,
        },
        "sample_size_scope": (
            "The 300-path cohorts can support a pointwise 99% lower reception bound only if all "
            "300 are correct and accepted. The 32-path long-duration cohorts are exploratory and "
            "cannot establish 99% reliability even at 32/32."
        ),
    }
    out.mkdir(parents=True, exist_ok=True)
    destinations[0].write_text(json.dumps({**shared, "jobs": exports}, indent=2) + "\n")
    destinations[1].write_text(json.dumps({**shared, "jobs": scores}, indent=2) + "\n")


if __name__ == "__main__":
    main()

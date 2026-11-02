#!/usr/bin/env python3
"""Freeze 850-day sampled controls; this generator launches no experiments.

These use the previous stable-ray study's physical duration and block duration.
Fs is raised from 1/8 to 1/4 Hz so the active selective stage's capture allowance
is satisfied. The nominal timing branch is not known initial alignment.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


MODES = ("off", "each-65")
PATHS = 16
START_INDEX = 80000
SECONDS = 850 * 86400


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    exports = []
    scores = []
    retention = []
    for mode in MODES:
        name = f"stable850-{mode}-m60"
        source = root / "local-evidence/selective/long" / name
        command = [python, str(root / "tools/triad-lowrate-campaign.py"),
                   "--binary", str(binary), "--output", str(source),
                   "--split", "holdout", "--control", "signal",
                   "--paths", str(PATHS), "--start-index", str(START_INDEX),
                   "--jobs", "4", "--timeout", "300", "--seconds", str(SECONDS),
                   "--rate", ".0036", "--sample-rate", "1",
                   "--sample-rate-denominator", "4", "--block-seconds", "8192",
                   "--cn0", "-60", "--rf-hz", "8270", "--spread-preset", "sub9-best",
                   "--spread-delay-ms", "5", "--spread-doppler-hz", ".05",
                   "--tap-delay-ms", "5", "--tap-doppler-hz", ".05",
                   "--ray-gaussian-width-hz", "0", "--selective-perturbation", mode,
                   "--start", "0", "0", "1", "--epoch-offsets", "0"]
        exports.append({"name": name, "timeout_seconds": 1200, "argv": command})
        inputs = [str(source / f"path-{i:05d}") for i in range(START_INDEX, START_INDEX + PATHS)]
        command = [python, str(root / "tools/triad-conditional-sweep.py"), *inputs,
                   "--output", str(root / "local-evidence/selective/long-scores" / (name + ".json")),
                   "--mode", "actual-noisy", "--methods", "static-fft-ucb",
                   "--reference-variance", "1", "--frequency-bound", "1e-6",
                   "--device", "cpu", "--precision", "fp64", "--threads", "1", "--memory-gib", "8",
                   "--selection-methods", "16", "--duration-alternatives", "8",
                   "--attempt-spacing", "60", "--calibration-alternatives", "2",
                   "--horizon-seconds", "86400", "--false-probability", "1e-4",
                   "--correct-reception-target", ".99", "--seed", "231206"]
        scores.append({"name": name, "timeout_seconds": 1200, "argv": command})
        command = [python, str(root / "docs/validation-data/triad-v3/2026-11-02-selective/reproduction/retention.py"),
                   *inputs[:4], "--output",
                   str(root / "local-evidence/selective/long-retention" / (name + ".json"))]
        retention.append({"name": name + "-retention", "timeout_seconds": 600, "argv": command})
    metadata = {
        "schema": "triad-selective-stable850-control-plan-v1",
        "stage": "fixed-size long-span sampled controls; not 99% reliability qualification",
        "case_count": 2, "sampled_export_count": 32,
        "selection": "Frozen before observing these holdout outcomes; 16 balanced source bits per mode, with paired seed roots across modes.",
        "geometry": {
            "requested_active_seconds": SECONDS, "requested_active_days": 850,
            "gui_rate_hz": .0036, "fine_symbol_rate_per_carrier_hz": .0006,
            "carrier_spacing_hz": .00075, "ideal_triplet_bandwidth_hz": .00225,
            "sample_rate_hz": .25, "block_seconds": 8192, "block_samples": 2048,
            "nominal_active_rows_before_rounding_and_tails": SECONDS // 4,
            "spread_delay_ms": 5, "ray_center_separation_hz": .05,
            "ray_gaussian_twice_rms_width_hz": 0,
            "selective_slow_rate_hz": .005, "selective_fast_rate_hz": .05,
            "selective_slow_cycles_per_block": 40.96, "selective_fast_cycles_per_block": 409.6,
            "active_selective_capture_allowance_before_oscillator_hz": .04775,
        },
        "source_defaults": {
            "base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo", "rf_hz": 8270,
            "clock_sync": "GPS_10ms-1ms_region-0ms_offset", "audio_error_ms": 70,
            "margin": 1, "added_symbols": 1, "fhss": "off", "symbol_burst": "off", "encryption": True,
            "cn0_db_hz": -60, "control": "signal", "split": "holdout",
            "start_bank_seconds": [0, 0, 1], "epoch_offsets": [0],
            "paths_per_mode": PATHS, "start_index": START_INDEX, "modes": list(MODES),
        },
        "bounds": {
            "per_path_timeout_seconds": 300, "per_export_batch_deadline_seconds": 1200,
            "export_workers_per_batch": 4, "per_score_deadline_seconds": 1200,
            "score_threads_per_case": 1, "score_memory_gib_per_case": 8,
            "retention_paths_per_mode": 4, "per_retention_deadline_seconds": 600,
        },
        "targets": {
            "strict_correct": .99, "strict_noise_any_per_day": 1e-4,
            "relaxed_correct": .9, "relaxed_noise_any_per_12hours": .01,
            "method_alternatives": 16, "duration_alternatives": 8,
            "calibration_alternatives": 2, "attempt_seconds": 60,
        },
        "interpretation_limits": [
            "The complete physical duration is sampled with continuous shared channel and oscillator state; no waveform repetition or time scaling is authorized by this plan.",
            "Stable rays have deterministic center-frequency separation and no Gaussian or OU random gain renewal. This control does not establish sensitivity in stochastic Doppler spread over 850 days.",
            "The reciprocal low-rate sampled path models constant oscillator offsets and Wiener phase. It does not enable white, flicker or random-walk FM, white PM, sampled frontend, AGC, FHSS or dropouts; it is not a full long-term physical oscillator qualification.",
            "The zero start/epoch bank is a public nominal low-rate approximation over the tight-clock uncertainty, not a known-arrival or exhaustive acquisition result.",
            "Active modulation raises the sample rate relative to the preceding Fs1/8-Hz baseline. Only these two new modes share seed roots and Fs; their comparison is paired.",
            "Off and Each65 have different filter tails; paired seed roots do not establish identical whole noise arrays or durations.",
            "Each65 has additive sinusoidal component depths .65 and .65, can reverse polarity, and is not power normalized. Separate nominal C/N0, actual pre/post selective energy and retained projection energy.",
            "An 8192-second block contains many slow and fast AM cycles. Existing two-ray accumulation can discard sideband energy before the static likelihood; the retention diagnostic measures that loss without claiming achievability.",
            "Actual-noisy scoring reads observed correlations and energy with public Grams; realized noiseless means, source bit and selective phases are not receiver inputs.",
            "The 16 observed paths per mode, eight per source bit, cannot establish 99% reliability or a daily 1e-4 empirical false-admission tail. These are sampled long-span controls.",
            "Configured gate accounting retains the operational daily attempt and model family. A long reception does not receive an independent additional false-admission allowance for every preceding day.",
            "Requested active time and nominal rows are not actual rounded private-tile duration or complete sampled capture size; read those from each completed manifest.",
        ],
    }
    return ({**metadata, "operation": "sampled actual-noisy export", "jobs": exports},
            {**metadata, "operation": "actual-noisy static receiver", "jobs": scores,
             "retention_jobs": retention})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--binary", type=Path, default=Path("/workspace/triad/build/dev-cli/triad_long"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    exports, scores = make_plans(args.root.resolve(), args.python, args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in (("long-export-plan.json", exports), ("long-score-plan.json", scores)):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

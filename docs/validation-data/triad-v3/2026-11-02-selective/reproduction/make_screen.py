#!/usr/bin/env python3
"""Freeze the 48-case development screen; this generator launches no jobs.

Run the generated jobs with the bounded runner in the preceding spread study.
The receiver uses only public configuration, not realized channel coefficients.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import json
import math
from pathlib import Path


ALL_MODES = ("off", "each-1", "each-5", "each-10", "each-25", "each-65", "slow-65", "fast-65")
RATE_MODES = ("off", "each-25", "each-65")
FAST_MODES = ("off", "slow-65", "fast-65", "each-25", "each-65")
START_INDEX = 40000
PATHS = 4
CN0_EXPORT = -25


@dataclass(frozen=True)
class Profile:
    name: str
    seconds: int
    rate: float
    sample_rate: int
    sample_rate_denominator: int
    block_seconds: int
    delay_ms: float
    center_hz: float
    gaussian_width_hz: float
    cn0_grid: tuple[int, ...]
    modes: tuple[str, ...] = ALL_MODES


def profiles() -> tuple[Profile, ...]:
    g001 = Profile("g001", 86400, .036, 1, 4, 64, .005, .001, .001,
                   (-30, -28, -27, -26, -25, -24))
    g01 = Profile("g01", 86400, .36, 2, 1, 8, .05, .005, .01,
                  (-28, -26, -24, -23, -22, -20))
    g1 = Profile("g1", 8640, 3.6, 16, 1, 1, .5, .1, .1,
                 (-16, -14, -12, -10, -8))
    stable = Profile("stable", 86400, .036, 1, 4, 256, .05, .005, 0,
                     (-38, -36, -34, -32, -30, -28))
    return (g001, g01, g1, stable,
            replace(g01, name="g01-slowrate", rate=.036, modes=RATE_MODES),
            replace(g1, name="g1-slowrate", rate=.36, modes=RATE_MODES),
            replace(g001, name="g001-fastam", center_hz=.01, modes=FAST_MODES),
            replace(g1, name="g1-fastam", center_hz=1., modes=FAST_MODES))


def geometry(p: Profile) -> dict:
    fine_rate = p.rate / 6  # Existing triad_long mapping with Added=1.
    bandwidth = 3.75 * fine_rate
    fs = p.sample_rate / p.sample_rate_denominator
    return {
        "name": p.name,
        "nominal_seconds": p.seconds,
        "gui_rate_hz": p.rate,
        "fine_symbol_rate_per_carrier_hz": fine_rate,
        "carrier_spacing_hz": 1.25 * fine_rate,
        "ideal_triplet_bandwidth_hz": bandwidth,
        "sample_rate_hz": fs,
        "block_seconds": p.block_seconds,
        "samples_per_block": fs * p.block_seconds,
        "excess_delay_ms": p.delay_ms,
        "delay_times_ideal_bandwidth": p.delay_ms / 1000 * bandwidth,
        "ray_center_separation_hz": p.center_hz,
        "ray_gaussian_twice_rms_width_hz": p.gaussian_width_hz,
        "selective_slow_rate_hz": .1 * p.center_hz,
        "selective_fast_rate_hz": p.center_hz,
        "receiver_coherence_seconds": (math.sqrt(2) / (math.pi * p.gaussian_width_hz)
                                        if p.gaussian_width_hz else None),
        "receiver_coherence_limit": "finite" if p.gaussian_width_hz else "infinite",
        "score_cn0_db_hz": list(p.cn0_grid),
        "modes": list(p.modes),
    }


def export_command(root: Path, python: str, binary: Path, p: Profile, mode: str) -> list[str]:
    name = f"{p.name}-{mode}"
    return [python, str(root / "tools/triad-lowrate-campaign.py"),
            "--binary", str(binary),
            "--output", str(root / "local-evidence/selective/screen" / name),
            "--split", "development", "--paths", str(PATHS),
            "--start-index", str(START_INDEX), "--jobs", "2", "--timeout", "300",
            "--seconds", str(p.seconds), "--rate", str(p.rate),
            "--sample-rate", str(p.sample_rate),
            "--sample-rate-denominator", str(p.sample_rate_denominator),
            "--block-seconds", str(p.block_seconds), "--cn0", str(CN0_EXPORT),
            "--rf-hz", "8270", "--spread-preset", "sub9-best",
            "--spread-delay-ms", str(p.delay_ms), "--spread-doppler-hz", str(p.center_hz),
            "--tap-delay-ms", str(p.delay_ms), "--tap-doppler-hz", str(p.center_hz),
            "--ray-gaussian-width-hz", str(p.gaussian_width_hz),
            "--selective-perturbation", mode,
            "--start", "0", "0", "1", "--epoch-offsets", "0"]


def score_command(root: Path, python: str, p: Profile, mode: str) -> list[str]:
    name = f"{p.name}-{mode}"
    source = root / "local-evidence/selective/screen" / name
    gaussian = p.gaussian_width_hz > 0
    coherence = math.sqrt(2) / (math.pi * p.gaussian_width_hz) if gaussian else math.inf
    command = [python, str(root / "tools/triad-conditional-sweep.py"),
               *(str(source / f"path-{i:05d}") for i in range(START_INDEX, START_INDEX + PATHS)),
               "--output", str(root / "local-evidence/selective/scores" / (name + ".json")),
               "--cn0-grid=" + ",".join(map(str, p.cn0_grid)),
               "--noise-trials", "16", "--trial-batch", "16", "--controls", "signal", "null",
               "--methods", "gaussian-window-ucb" if gaussian else "static-fft-ucb",
               "--coherence", str(coherence), "--frequency-bound", "1e-6", "--particles", "128",
               "--device", "cuda:0" if gaussian else "cpu",
               "--precision", "fp32" if gaussian else "fp64", "--threads", "1", "--memory-gib", "8",
               "--selection-methods", "16", "--duration-alternatives", "8",
               "--attempt-spacing", "60", "--calibration-alternatives", "2",
               "--horizon-seconds", "86400", "--false-probability", "1e-4",
               "--correct-reception-target", ".99", "--seed", "231203"]
    if gaussian:
        command += ["--window-doppler-width", str(p.gaussian_width_hz), "--window-blocks", "64"]
    return command


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    configurations = profiles()
    metadata = {
        "schema": "triad-selective-screen-plan-v1",
        "stage": "development screen; not qualification",
        "scope": "48 configurations, four balanced sampled physical paths each, conditional signal/null noise replicas",
        "case_count": 48,
        "sampled_export_count": 192,
        "source_defaults": {
            "base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo",
            "rf_hz": 8270, "margin": 1, "clock_sync": "GPS_10ms-1ms_region-0ms_offset",
            "audio_error_ms": 70, "added_symbols": 1, "fhss": "off", "symbol_burst": "off",
            "encryption": True,
            "sampled_export_cn0_db_hz": CN0_EXPORT, "paths_per_case": PATHS,
            "start_index": START_INDEX, "split": "development",
        },
        "bounds": {
            "per_path_timeout_seconds": 300, "per_case_deadline_seconds": 600,
            "export_workers_per_case": 2, "score_threads_per_case": 1,
            "score_memory_gib_per_case": 8, "conditional_draws_per_path_per_control_per_cn0": 16,
            "selection_methods": 16, "duration_alternatives": 8, "calibration_alternatives": 2,
            "attempt_spacing_seconds": 60, "false_admission_horizon_seconds": 86400,
            "false_admission_probability_target": 1e-4, "correct_reception_target": .99,
        },
        "profiles": [geometry(p) for p in configurations],
        "interpretation_limits": [
            "The nominal start=0/epoch=0 bank is a declared public low-rate approximation, not known alignment or full blind acquisition.",
            "Four physical paths are balanced two per source bit; conditional noise replicas are not additional independent physical channels.",
            "At fixed rate/Fs/duration/export C/N0, depth modes share campaign seed roots. Off and On have different filter tails, so whole arrays and realized noisy tails are not identical.",
            "Changing RF, ray separation or delay can preserve seed roots while changing the physical channel. Changing rate or Fs changes roots; slow-rate comparisons are unpaired.",
            "Fastam profiles change ray-center separation as well as selective modulation rates. Gaussian twice-RMS ray width is held fixed; this is not an isolated modulation-rate intervention.",
            "Active selective gains are additive, seeded sinusoids. Each65 can reach -0.30; selected stress is not energy normalized.",
            "Source and receiver delay/center parameters agree by public configuration; realized ray and selective phases are not receiver inputs.",
            "Geometry is nominal. Read actual rounded source duration, complete filter tails, sampled energies and receiver retention from completed exports.",
            "Gaussian scoring uses strict FP32 bulk on CUDA with existing FP64 preparation/reductions; stable scoring uses the CPU FP64 static FFT receiver.",
            "Operational family accounting is configured, but this development screen does not establish 99% physical-path reliability or empirically measure a 1e-4/day tail.",
        ],
    }
    exports = []
    scores = []
    for p in configurations:
        for mode in p.modes:
            name = f"{p.name}-{mode}"
            exports.append({"name": name, "timeout_seconds": 600,
                            "argv": export_command(root, python, binary, p, mode)})
            scores.append({"name": name, "timeout_seconds": 600,
                           "argv": score_command(root, python, p, mode)})
    assert len(exports) == len(scores) == metadata["case_count"]
    assert len(exports) * PATHS == metadata["sampled_export_count"]
    return ({**metadata, "operation": "sampled export", "jobs": exports},
            {**metadata, "operation": "conditional receiver screen", "jobs": scores})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--binary", type=Path, default=Path("/workspace/triad/build/dev-cli/triad_long"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    exports, scores = make_plans(args.root.resolve(), args.python, args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in (("screen-export-plan.json", exports), ("screen-score-plan.json", scores)):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

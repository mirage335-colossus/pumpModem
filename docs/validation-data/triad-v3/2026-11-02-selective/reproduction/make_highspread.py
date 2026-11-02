#!/usr/bin/env python3
"""Generate a small known-initial-arrival high-spread diagnostic; launch no jobs.

Like the preceding spread study's make_aligned_screen.py, build the canonical
campaign command then explicitly set zero initial clock and audio error. The
endpoint oscillator processes remain active. This is not blind acquisition.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path


MODES = ("off", "each-25", "each-65", "slow-65", "fast-65")
CN0_GRID = (-2, 0, 2, 4, 6, 8, 10, 12)
START_INDEX = 50000
PATHS = 2


def campaign_module():
    path = Path(__file__).resolve().parents[5] / "tools/triad-lowrate-campaign.py"
    spec = importlib.util.spec_from_file_location("selective_highspread_campaign", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    campaign = campaign_module()
    cases = []
    exports = []
    scores = []
    for profile, width, center, delay in (("g10", 10., 10., 6.), ("g30", 30., 30., 7.)):
        for mode in MODES:
            name = f"{profile}-aligned-{mode}"
            source = root / "local-evidence/selective/highspread" / name
            args = campaign.parser().parse_args([
                "--binary", str(binary), "--output", str(source),
                "--paths", str(PATHS), "--start-index", str(START_INDEX), "--split", "development",
                "--seconds", "120", "--rate", "360", "--sample-rate", "2048",
                "--sample-rate-denominator", "1", "--block-seconds", ".015625", "--cn0", "-25",
                "--rf-hz", "8270", "--spread-preset", "sub9-best",
                "--spread-delay-ms", str(delay), "--spread-doppler-hz", str(center),
                "--tap-delay-ms", str(delay), "--tap-doppler-hz", str(center),
                "--ray-gaussian-width-hz", str(width), "--selective-perturbation", mode,
                "--start", "0", "0", "1", "--epoch-offsets", "0",
            ])
            for index in range(START_INDEX, START_INDEX + PATHS):
                command, truth = campaign.make_command(args, index, source / f"path-{index:05d}")
                command[command.index("--clock-sync") + 1] = "GPS_0ms-0ms_region-0ms_offset"
                command[command.index("--audio-error-ms") + 1] = "0"
                exports.append({"name": f"{name}-{index}", "timeout_seconds": 600,
                                "argv": command, "simulation_only_expected": truth})
            command = [python, str(root / "tools/triad-conditional-sweep.py"),
                       *(str(source / f"path-{i:05d}") for i in range(START_INDEX, START_INDEX + PATHS)),
                       "--output", str(root / "local-evidence/selective/highspread-scores" / (name + ".json")),
                       "--cn0-grid=" + ",".join(map(str, CN0_GRID)),
                       "--noise-trials", "16", "--trial-batch", "16", "--controls", "signal", "null",
                       "--methods", "gaussian-window-ucb", "--window-doppler-width", str(width),
                       "--window-blocks", "64", "--coherence", str(math.sqrt(2) / (math.pi * width)),
                       "--frequency-bound", "1e-6", "--particles", "128",
                       "--device", "cuda:0", "--precision", "fp32", "--threads", "1", "--memory-gib", "8",
                       "--selection-methods", "16", "--duration-alternatives", "8",
                       "--attempt-spacing", "60", "--calibration-alternatives", "2",
                       "--horizon-seconds", "86400", "--false-probability", "1e-4",
                       "--correct-reception-target", ".99", "--seed", "231204"]
            scores.append({"name": name, "timeout_seconds": 600, "argv": command})
            cases.append({
                "name": name, "profile": profile, "selective_perturbation": mode,
                "requested_seconds": 120, "gui_rate_hz": 360, "fine_symbol_rate_per_carrier_hz": 60,
                "sample_rate_hz": 2048, "block_seconds": .015625, "block_samples": 32,
                "carrier_spacing_hz": 75, "ideal_triplet_bandwidth_hz": 225,
                "delay_ms": delay, "delay_times_ideal_bandwidth": delay / 1000 * 225,
                "ray_center_separation_hz": center, "ray_gaussian_twice_rms_width_hz": width,
                "receiver_delay_ms": delay, "receiver_center_hz": center,
                "receiver_coherence_seconds": math.sqrt(2) / (math.pi * width),
                "selective_slow_rate_hz": .1 * center, "selective_fast_rate_hz": center,
                "capture_allowance_before_oscillator_hz": 2048 - 225 - (2 if mode == "off" else 4) * center - 6 * width,
                "window_blocks": 64, "window_seconds": 1, "score_cn0_db_hz": list(CN0_GRID),
            })
    metadata = {
        "schema": "triad-selective-highspread-aligned-plan-v1",
        "stage": "development aligned diagnostic; not qualification",
        "scope": "Two high-spread profiles, five selective modes each, two balanced sampled paths per case. Artificially known initial arrival0; endpoint oscillator/clock errors remain.",
        "case_count": 10, "sampled_export_count": 20,
        "source_defaults": {
            "base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo", "rf_hz": 8270,
            "clock_sync": "GPS_0ms-0ms_region-0ms_offset", "audio_error_ms": 0,
            "start_bank_seconds": [0, 0, 1], "epoch_offsets": [0],
            "margin": 1, "added_symbols": 1, "fhss": "off", "symbol_burst": "off", "encryption": True,
            "sampled_export_cn0_db_hz": -25, "start_index": START_INDEX,
            "paths_per_case": PATHS, "split": "development",
        },
        "bounds": {"per_export_deadline_seconds": 600, "per_score_deadline_seconds": 600,
                   "score_threads": 1, "score_memory_gib": 8,
                   "conditional_draws_per_path_per_control_per_cn0": 16},
        "interpretation_limits": [
            "Zero clock accuracy, zero region/offset and zero audio error remove initial arrival uncertainty. This differs deliberately from the nominal zero branch over the tight-clock +/-90.5ms arrival interval.",
            "Base and RF oscillator presets remain active; known initial arrival does not mean known clock rate, phase trajectory or channel.",
            "Only two independent physical paths per case, one per source bit. Conditional signal/null noise draws are not extra physical-path holdouts.",
            "All profiles and depth modes use the same seed roots because duration/rate/Fs/export C/N0 match. Changed delay/Doppler changes the physical path despite matching roots.",
            "Off and active selective modes have different filter tails. Paired roots do not imply whole-array or whole-noise identity.",
            "Nominal active capture allocation includes 4*D+6*B beyond the ideal triplet bandwidth, with oscillator allowance still required. Actual C++ configure output determines capture feasibility.",
            "Gaussian B and ray-center separation D both change across profiles; this comparison does not isolate either dimension alone.",
            "The Gaussian receiver width and delay/Doppler basis are public configuration. Realized channel and modulation phases remain hidden from reception.",
            "Each65 is additive amplitude stress, permits polarity reversal and is not power normalized. Read actual pre/post selective energy and receiver retention from sampled exports.",
            "Requested duration and nominal geometry are not measured rounded source duration, channel tails or receivable energy.",
            "Configured operational admission accounting does not make this small development sample a reliability or daily-tail qualification.",
        ],
        "cases": cases,
    }
    assert len(exports) == 20 and len(scores) == len(cases) == 10
    return ({**metadata, "operation": "sampled aligned export", "jobs": exports},
            {**metadata, "operation": "conditional aligned receiver screen", "jobs": scores})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--binary", type=Path, default=Path("/workspace/triad/build/dev-cli/triad_long"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    exports, scores = make_plans(args.root.resolve(), args.python, args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in (("highspread-export-plan.json", exports), ("highspread-score-plan.json", scores)):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

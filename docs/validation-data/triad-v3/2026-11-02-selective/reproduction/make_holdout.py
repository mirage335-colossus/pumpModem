#!/usr/bin/env python3
"""Freeze actual sampled-noise holdouts; generate commands without running them.

The selected operating points are fixed before observing these holdout paths.
Every scored record is one independently seeded sampled observation within its
cell. Paired depths/controls reuse roots and must not be pooled as extra paths.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path


START_INDEX = 60000
SIGNAL_PATHS = 300
SIGNAL_BATCH = 50
CONTROL_PATHS = 32
PROFILES = (
    {"name": "g001", "seconds": 86400, "rate": .036, "sample_rate": 1,
     "denominator": 4, "block_seconds": 64, "delay_ms": .005,
     "center_hz": .001, "width_hz": .001, "cn0_stronger": -26, "cn0_weaker": -27},
    {"name": "g01", "seconds": 86400, "rate": .36, "sample_rate": 2,
     "denominator": 1, "block_seconds": 8, "delay_ms": .05,
     "center_hz": .005, "width_hz": .01, "cn0_stronger": -22, "cn0_weaker": -23},
    {"name": "g1", "seconds": 8640, "rate": 3.6, "sample_rate": 16,
     "denominator": 1, "block_seconds": 1, "delay_ms": .5,
     "center_hz": .1, "width_hz": .1, "cn0_stronger": -10, "cn0_weaker": -11},
)


def campaign_module():
    path = Path(__file__).resolve().parents[5] / "tools/triad-lowrate-campaign.py"
    spec = importlib.util.spec_from_file_location("selective_holdout_campaign", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def source_command(root, python, binary, p, mode, control, cn0, output, start, paths):
    return [python, str(root / "tools/triad-lowrate-campaign.py"),
            "--binary", str(binary), "--output", str(output),
            "--split", "holdout", "--control", control, "--paths", str(paths),
            "--start-index", str(start), "--jobs", "2", "--timeout", "300",
            "--seconds", str(p["seconds"]), "--rate", str(p["rate"]),
            "--sample-rate", str(p["sample_rate"]),
            "--sample-rate-denominator", str(p["denominator"]),
            "--block-seconds", str(p["block_seconds"]), "--cn0", str(cn0),
            "--rf-hz", "8270", "--spread-preset", "sub9-best",
            "--spread-delay-ms", str(p["delay_ms"]), "--spread-doppler-hz", str(p["center_hz"]),
            "--tap-delay-ms", str(p["delay_ms"]), "--tap-doppler-hz", str(p["center_hz"]),
            "--ray-gaussian-width-hz", str(p["width_hz"]),
            "--selective-perturbation", mode,
            "--start", "0", "0", "1", "--epoch-offsets", "0"]


def score_command(root, python, p, source, output, start, paths):
    return [python, str(root / "tools/triad-conditional-sweep.py"),
            *(str(source / f"path-{i:05d}") for i in range(start, start + paths)),
            "--output", str(output), "--mode", "actual-noisy",
            "--methods", "gaussian-window-ucb", "--window-blocks", "64",
            "--window-doppler-width", str(p["width_hz"]),
            "--coherence", str(math.sqrt(2) / (math.pi * p["width_hz"])),
            "--frequency-bound", "1e-6", "--particles", "128",
            "--device", "cuda:0", "--precision", "fp32", "--threads", "1", "--memory-gib", "8",
            "--selection-methods", "16", "--duration-alternatives", "8",
            "--attempt-spacing", "60", "--calibration-alternatives", "2",
            "--horizon-seconds", "86400", "--false-probability", "1e-4",
            "--correct-reception-target", ".99", "--seed", "231203"]


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    campaign, campaign_path = campaign_module()
    exports = []
    scores = []
    cells = []
    seed_vectors = set()
    for p in PROFILES:
        settings = (("each-65", "signal", p["cn0_stronger"]),
                    ("each-65", "signal", p["cn0_weaker"]),
                    ("off", "signal", p["cn0_stronger"]),
                    ("each-65", "null", p["cn0_stronger"]),
                    ("each-65", "wrong-key", p["cn0_stronger"]))
        for mode, control, cn0 in settings:
            cell = f'{p["name"]}-{mode}-cn0m{-cn0}-{control}'
            count = SIGNAL_PATHS if control == "signal" else CONTROL_PATHS
            batch = SIGNAL_BATCH if control == "signal" else CONTROL_PATHS
            cell_jobs = []
            for offset in range(0, count, batch):
                start = START_INDEX + offset
                size = min(batch, count - offset)
                name = f"{cell}-batch{offset // batch:02d}"
                cell_jobs.append(name)
                source = root / "local-evidence/selective/holdout" / cell / f"batch{offset // batch:02d}"
                output = root / "local-evidence/selective/holdout-scores" / f"{name}.json"
                command = source_command(root, python, binary, p, mode, control, cn0, source, start, size)
                args = campaign.parser().parse_args(command[2:])
                expected = []
                for index in range(start, start + size):
                    _, truth = campaign.make_command(args, index, source / f"path-{index:05d}")
                    expected.append({"index": index, **truth})
                    seed_vectors.add(tuple(truth[domain + "_seed"] for domain in campaign.SEED_DOMAINS))
                common = {"name": name, "cell": cell, "control": control,
                          "path_count": size, "start_index": start, "timeout_seconds": 600}
                exports.append({**common, "argv": command, "simulation_only_expected": expected})
                scores.append({**common, "argv": score_command(root, python, p, source, output, start, size)})
            fs = p["sample_rate"] / p["denominator"]
            cells.append({"name": cell, "profile": p["name"], "selective_perturbation": mode,
                          "control": control, "sampled_cn0_db_hz": cn0,
                          "path_count": count, "source_bit_counts": {"0": count // 2, "1": count // 2},
                          "path_index_first": START_INDEX, "path_index_last": START_INDEX + count - 1,
                          "batch_jobs": cell_jobs, "requested_active_seconds": p["seconds"],
                          "gui_rate_hz": p["rate"], "fine_symbol_rate_per_carrier_hz": p["rate"] / 6,
                          "carrier_spacing_hz": 1.25 * p["rate"] / 6,
                          "ideal_triplet_bandwidth_hz": 3.75 * p["rate"] / 6,
                          "sample_rate_numerator": p["sample_rate"], "sample_rate_denominator": p["denominator"],
                          "sample_rate_hz": fs, "block_seconds": p["block_seconds"],
                          "samples_per_block": fs * p["block_seconds"], "excess_delay_ms": p["delay_ms"],
                          "ray_center_separation_hz": p["center_hz"],
                          "ray_gaussian_twice_rms_width_hz": p["width_hz"],
                          "selective_slow_rate_hz": .1 * p["center_hz"],
                          "selective_fast_rate_hz": p["center_hz"],
                          "receiver_coherence_seconds": math.sqrt(2) / (math.pi * p["width_hz"])})
    metadata = {
        "schema": "triad-selective-actual-holdout-plan-v1",
        "stage": "frozen actual sampled-noise holdout; outcome and qualification pending",
        "scope": "Nine signal operating points with 300 balanced paths each, plus six 32-path null/wrong-key controls; no conditional noise replicas.",
        "signal_cell_count": 9, "control_cell_count": 6, "cell_count": 15,
        "signal_export_count": 2700, "control_export_count": 192, "sampled_export_count": 2892,
        "job_count": 60, "distinct_seed_vectors_across_all_cells": len(seed_vectors),
        "seed_generation_source": {"path": "tools/triad-lowrate-campaign.py",
                                   "sha256": hashlib.sha256(campaign_path.read_bytes()).hexdigest(),
                                   "domains": list(campaign.SEED_DOMAINS),
                                   "frozen_expected_values": "Export jobs contain every index, source bit, epoch and six domain roots under simulation_only_expected; these are validation metadata, not receiver inputs."},
        "source_defaults": {"base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo",
                            "rf_hz": 8270, "margin": 1, "clock_sync": "GPS_10ms-1ms_region-0ms_offset",
                            "audio_error_ms": 70, "start_bank_seconds": [0, 0, 1], "epoch_offsets": [0],
                            "added_symbols": 1, "fhss": "off", "symbol_burst": "off", "encryption": True,
                            "split": "holdout", "start_index": START_INDEX},
        "receiver": {"mode": "actual-noisy", "methods": ["gaussian-window-ucb"],
                     "window_blocks": 64, "width": "public configured physical B, never realized channel",
                     "device": "cuda:0", "precision": "fp32", "frequency_bound_hz": 1e-6,
                     "selection_methods": 16, "duration_alternatives": 8, "calibration_alternatives": 2,
                     "attempt_spacing_seconds": 60, "false_admission_horizon_seconds": 86400,
                     "false_admission_probability_target": 1e-4, "correct_reception_target": .99},
        "bounds": {"per_path_timeout_seconds": 300, "per_batch_deadline_seconds": 600,
                   "signal_paths_per_batch": SIGNAL_BATCH, "control_paths_per_batch": CONTROL_PATHS,
                   "export_workers_per_batch": 2, "score_threads_per_batch": 1, "score_memory_gib_per_batch": 8},
        "cells": cells,
        "interpretation_limits": [
            "Every signal cell has 150 independently seeded cases per source-bit stratum. Report bitwise intervals and a justified balanced-mean bound; pooled iid-binomial intervals require an additional identical-distribution assumption.",
            "The same public nominal start=0 and epoch=0 bank as the development screen is retained. It is a low-rate acquisition approximation, not known initial alignment or an exhaustive search over the full +/-90.5ms prior.",
            "At fixed duration/rate/Fs/C/N0/index, Off and Each65 share six random roots; null/wrong-key controls reuse the first32 signal roots. Do not pool those variants as independent physical channels.",
            "There are 1800 distinct six-root vectors across the matrix. C/N0 is in the seed identity, so different C/N0 operating points are independent seeded sets rather than paired noise scalings.",
            "Off and active modulation have different filter tails, so shared roots do not imply identical full noise arrays or samples.",
            "Actual-noisy scoring consumes observed correlations/energy and public Gram/basis information. It does not synthesize noise, sweep C/N0, or use exported noiseless means, signal energy, source-bit truth or selective phases as receiver inputs.",
            "Each65 permits negative instantaneous band gain and is not energy normalized. Nominal injected C/N0 differs from realized post-selective energy; report both separately.",
            "Geometry and durations here are nominal. Completed manifests must establish rounded active duration, capture support, tails, actual seeds and complete job coverage.",
            "Strict operational family accounting is retained. Small null/wrong-key samples do not empirically establish a1e-4/day false-admission probability, and relaxing a gate does not create a new independent trial.",
            "This plan makes no reception or qualification claim. Missing, failed or timed-out signal observations must remain visible; missing controls prevent complete validation.",
        ],
    }
    assert len(exports) == len(scores) == metadata["job_count"]
    assert sum(j["path_count"] for j in exports) == metadata["sampled_export_count"]
    assert len(seed_vectors) == 1800
    return ({**metadata, "operation": "actual sampled-waveform export", "jobs": exports},
            {**metadata, "operation": "actual sampled-noise receiver validation", "jobs": scores})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--binary", type=Path, default=Path("/workspace/triad/build/dev-cli/triad_long"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    exports, scores = make_plans(args.root.resolve(), args.python, args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in (("holdout-export-plan.json", exports), ("holdout-score-plan.json", scores)):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

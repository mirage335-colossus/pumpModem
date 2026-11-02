#!/usr/bin/env python3
"""Freeze prospective lower-C/N0 and slower-rate followup holdout commands.

Operating points were selected after the primary matrix and development rate
screen, then frozen before sampling this disjoint index range. This generator
launches no simulations and makes no reception or qualification claim.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path


START_INDEX = 90000
SIGNAL_PATHS = 300
SIGNAL_BATCH = 50
CONTROL_PATHS = 32
PROFILES = (
    {"name": "g001edge", "seconds": 86400, "rate": .036, "sample_rate": 1,
     "denominator": 4, "block_seconds": 64, "delay_ms": .005,
     "center_hz": .001, "width_hz": .001, "cn0": -28, "controls": False},
    {"name": "g01edge", "seconds": 86400, "rate": .36, "sample_rate": 2,
     "denominator": 1, "block_seconds": 8, "delay_ms": .05,
     "center_hz": .005, "width_hz": .01, "cn0": -24, "controls": False},
    {"name": "g1edge", "seconds": 8640, "rate": 3.6, "sample_rate": 16,
     "denominator": 1, "block_seconds": 1, "delay_ms": .5,
     "center_hz": .1, "width_hz": .1, "cn0": -13, "controls": False},
    {"name": "g01slowrate", "seconds": 86400, "rate": .036, "sample_rate": 2,
     "denominator": 1, "block_seconds": 8, "delay_ms": .05,
     "center_hz": .005, "width_hz": .01, "cn0": -24, "controls": True},
    {"name": "g1slowrate", "seconds": 8640, "rate": .36, "sample_rate": 16,
     "denominator": 1, "block_seconds": 1, "delay_ms": .5,
     "center_hz": .1, "width_hz": .1, "cn0": -14, "controls": True},
)


def primary_module():
    path = Path(__file__).resolve().with_name("make_holdout.py")
    spec = importlib.util.spec_from_file_location("selective_primary_holdout_generator", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    primary, primary_path = primary_module()
    campaign, _ = primary.campaign_module()
    baseline, _ = primary.make_plans(root, python, binary)
    metadata = {name: copy.deepcopy(baseline[name]) for name in
                ("schema", "seed_generation_source", "source_defaults", "receiver", "bounds")}
    metadata["source_defaults"]["start_index"] = START_INDEX
    exports = []
    scores = []
    cells = []
    seed_vectors = set()
    for p in PROFILES:
        controls = ("signal", "null", "wrong-key") if p["controls"] else ("signal",)
        for control in controls:
            mode, cn0 = "each-65", p["cn0"]
            cell = f'{p["name"]}-{mode}-cn0m{-cn0}-{control}'
            count = SIGNAL_PATHS if control == "signal" else CONTROL_PATHS
            batch = SIGNAL_BATCH if control == "signal" else CONTROL_PATHS
            cell_jobs = []
            for offset in range(0, count, batch):
                start = START_INDEX + offset
                size = min(batch, count - offset)
                name = f"{cell}-batch{offset // batch:02d}"
                cell_jobs.append(name)
                source = root / "local-evidence/selective/followup" / cell / f"batch{offset // batch:02d}"
                output = root / "local-evidence/selective/followup-scores" / f"{name}.json"
                command = primary.source_command(root, python, binary, p, mode, control, cn0, source, start, size)
                args = campaign.parser().parse_args(command[2:])
                expected = []
                for index in range(start, start + size):
                    _, truth = campaign.make_command(args, index, source / f"path-{index:05d}")
                    expected.append({"index": index, **truth})
                    seed_vectors.add(tuple(truth[domain + "_seed"] for domain in campaign.SEED_DOMAINS))
                common = {"name": name, "cell": cell, "control": control,
                          "path_count": size, "start_index": start, "timeout_seconds": 600}
                exports.append({**common, "argv": command, "simulation_only_expected": expected})
                scores.append({**common, "argv": primary.score_command(root, python, p, source, output, start, size)})
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
    metadata.update({
        "stage": "prospectively frozen followup after primary outcomes; outcome and qualification pending",
        "scope": "Five Each65 signal operating points with 300 balanced actual sampled-noise paths each; 32 null and 32 wrong-key controls for each of the two slower-rate profiles only. No conditional noise replicas or repeated Off references.",
        "selection_rationale": "Primary observations motivate lower C/N0 boundary points; development conditional screens motivate slower-rate validation. Selection used earlier data, but no followup path outcomes. Followup indices 90000..90299 are disjoint from primary 60000..60299 and development indices.",
        "signal_cell_count": 5, "control_cell_count": 4, "cell_count": 9,
        "signal_export_count": 1500, "control_export_count": 128, "sampled_export_count": 1628,
        "job_count": 34, "distinct_seed_vectors_across_all_cells": len(seed_vectors),
        "generator_dependency": {"path": "docs/validation-data/triad-v3/2026-11-02-selective/reproduction/make_holdout.py",
                                 "sha256": hashlib.sha256(primary_path.read_bytes()).hexdigest(),
                                 "scope": "Reuses frozen primary export/scorer argument builders and common physical/calibration defaults; no primary scientific jobs are executed."},
        "cells": cells,
        "interpretation_limits": [
            "Each signal cell has 150 independently seeded cases per source-bit stratum. Report bitwise intervals and a justified balanced-mean bound; pooled iid-binomial intervals require an additional identical-distribution assumption.",
            "This is a prospective followup selected using preceding results, not an untouched initial design. Report this adaptive selection and pointwise rather than simultaneous confidence claims unless multiplicity is separately accounted.",
            "The public nominal start=0/epoch=0 bank is retained over the tight-clock +/-90.5ms arrival prior. It is a low-rate approximation, not known initial alignment or full blind acquisition.",
            "There are 1500 distinct six-root vectors. Within each slower-rate profile, null/wrong-key controls reuse the first 32 signal roots; do not pool them as extra independent physical channels.",
            "Rate is part of the seed identity: standard-rate and slower-rate results use different seeded channels/noise. They are not paired rate interventions or evidence that only rate caused any observed difference.",
            "Actual-noisy scoring reads observed correlations/energy and public Gram/basis information. It neither resamples noise nor reads exported noiseless means, signal energy, source truth or selective phases as receiver inputs.",
            "Each65 permits negative band gain and is not energy normalized. Report nominal injected C/N0 separately from realized post-selective energy and receiver retention.",
            "Requested duration and nominal geometry must be checked against completed manifests, full tails, actual seed identities and complete export/scoring coverage.",
            "The strict operational family accounting is unchanged; 32 null/wrong-key observations per slower-rate cell cannot empirically establish a 1e-4/day tail. Earlier controls are not repeated or silently counted as new exposure.",
            "No reception or qualification outcome is asserted by this plan. Failed/missing/timed-out observations remain explicit; incomplete controls prevent complete validation.",
        ],
    })
    assert len(exports) == len(scores) == metadata["job_count"]
    assert sum(j["path_count"] for j in exports) == metadata["sampled_export_count"]
    assert len(seed_vectors) == 1500
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
    for name, plan in (("followup-export-plan.json", exports), ("followup-score-plan.json", scores)):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

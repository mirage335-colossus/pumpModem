#!/usr/bin/env python3
"""Freeze independent corrected-model actual-noisy holdouts; launch nothing.

The holdout matrices use a versioned dispersive-dft model only. They do not
replay legacy comparisons or select operating points from their outcomes.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
ROOT_DEFAULT = HERE.parents[4]
BASE_REL = Path("docs/validation-data/triad-v3/2026-11-02-selective-correction/reproduction/make_campaign.py")
PARENT_PLAN_DEFAULT = Path("local-evidence/triad-selective-correction/plans/long-replay-final-plan.json")
MODULATIONS = ("each-25", "each-65")
DOMAINS = ("key", "channel", "oscillator", "noise", "frontend", "path")
CANONICAL_SCHEMA = "triad-selective-model-paired-plan-v1"
MAX_TIMEOUT_SECONDS = 1800


def bounded_timeout(seconds):
    if not 0 < seconds:
        raise ValueError("job timeout must be positive")
    return min(seconds, MAX_TIMEOUT_SECONDS)


def load_base(root: Path):
    path = root / BASE_REL
    spec = importlib.util.spec_from_file_location("triad_selective_base_campaign", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import base campaign helper: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module, path


def load_lowrate(root: Path):
    path = root / "tools/triad-lowrate-campaign.py"
    spec = importlib.util.spec_from_file_location("triad_lowrate_campaign_holdouts", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import low-rate seed helper: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module, path


def expected_control_counts(profile_name: str) -> tuple[int, int]:
    return (32, 32) if profile_name in ("g001", "g01", "g1") else (16, 16)


def actual_score(root: Path, python: str, profile: dict, inputs: list[Path],
                 output: Path, methods=("static-fft-ucb", "gaussian-window-ucb"),
                 memory_gib=2.0, timeout=MAX_TIMEOUT_SECONDS):
    argv = [python, str(root / "tools/triad-conditional-sweep.py"),
            *(str(path) for path in inputs),
            "--output", str(output), "--mode", "actual-noisy",
            "--methods", *methods,
            "--taps", "2", "--frequency-bound", "1e-6",
            "--selection-methods", "16", "--duration-alternatives", "8",
            "--calibration-alternatives", "2", "--attempt-spacing", "60",
            "--horizon-seconds", "86400", "--false-probability", "1e-4",
            "--correct-reception-target", ".99",
            "--device", "cpu", "--precision", "fp64", "--threads", "1",
            "--memory-gib", str(memory_gib), "--seed", "231209"]
    if "gaussian-window-ucb" in methods:
        argv += ["--window-doppler-width", str(profile["width"]),
                 "--window-blocks", "64"]
    return {"name": output.stem, "timeout_seconds": bounded_timeout(timeout), "argv": argv}


def add_export(base, campaign, root, binary, profile, modulation, control,
               index, paths, cn0, stage, timeout):
    source, argv, truth = base.parser_args(
        campaign, root, binary, profile, "dispersive-dft", modulation,
        stage, index, paths, cn0, "holdout", control=control)
    name = f"{profile['name']}-dispersive-dft-{modulation}-{control}-p{index:05d}"
    job = {"name": name, "timeout_seconds": bounded_timeout(timeout), "argv": argv,
           "simulation_only_expected": {"index": index, **truth}}
    return source, job, truth


def make_short_holdouts(base, campaign, root, python, binary):
    all_profiles = {p["name"]: p for p in base.profiles()}
    frozen_profiles = []
    cells, jobs, scorers = [], [], []
    signal_count = control_count = 0
    for name, cn0, paths in (
        ("g001", -27, 64), ("g01", -23, 64), ("g1", -11, 64),
        ("high10", 8, 32), ("high30", 10, 32),
    ):
        profile = copy.deepcopy(all_profiles[name])
        frozen_profile = copy.deepcopy(profile)
        frozen_profile["cn0"] = [cn0]
        frozen_profiles.append(frozen_profile)
        profile["name"] = f"{name}-cn0{'m' if cn0 < 0 else 'p'}{abs(cn0):g}"
        for modulation in MODULATIONS:
            signal_inputs, expected = [], []
            for offset in range(paths):
                index = 130000 + offset
                source, job, truth = add_export(
                    base, campaign, root, binary, profile, modulation, "signal",
                    index, paths, cn0, "independent-corrected", 1800)
                jobs.append(job); signal_inputs.append(source)
                expected.append({"index": index, **truth})
            signal_count += len(signal_inputs)
            cells.append({"profile": name, "cn0_db_hz": cn0,
                          "selective_model": "dispersive-dft", "modulation": modulation,
                          "control": "signal", "paths": paths,
                          "indices": [130000, 130000+paths-1],
                          "balanced_bits": {"0": paths//2, "1": paths//2},
                          "seed_domains": list(DOMAINS), "expected": expected,
                          "sources": [str(p) for p in signal_inputs]})
            scorers.append(actual_score(
                root, python, profile, signal_inputs,
                root / f"local-evidence/triad-selective-correction/independent-corrected/scores/{profile['name']}-{modulation}-signal.json"))
            null_n, wrong_n = expected_control_counts(name)
            for control, count in (("null", null_n), ("wrong-key", wrong_n)):
                control_inputs, control_expected = [], []
                for offset in range(count):
                    index = 130000 + offset
                    source, job, truth = add_export(
                        base, campaign, root, binary, profile, modulation, control,
                        index, count, cn0, "independent-corrected", 1800)
                    jobs.append(job); control_inputs.append(source)
                    control_expected.append({"index": index, **truth})
                control_count += len(control_inputs)
                cells.append({"profile": name, "cn0_db_hz": cn0,
                              "selective_model": "dispersive-dft", "modulation": modulation,
                              "control": control, "paths": count,
                              "indices": [130000, 130000+count-1],
                              "balanced_bits": {"0": count//2, "1": count//2},
                              "seed_domains": list(DOMAINS), "expected": control_expected,
                              "sources": [str(p) for p in control_inputs],
                              "paired_signal_indices": [130000, 130000+count-1]})
                scorers.append(actual_score(
                    root, python, profile, control_inputs,
                    root / f"local-evidence/triad-selective-correction/independent-corrected/scores/{profile['name']}-{modulation}-{control}.json"))
    if (signal_count, control_count, len(cells), len(scorers)) != (512, 512, 30, 30):
        raise AssertionError("independent short holdout design count changed")
    return {
        "schema": CANONICAL_SCHEMA,
        "comparison_scope": "independent-corrected-model-validation",
        "stage": "holdout", "experiment_stage": "independent-corrected",
        "status": "frozen actual-noisy holdout plan; no exports or scoring have run",
        "scope": "Corrected dispersive-dft-v1 only; Each25/Each65; five prespecified weak-signal operating points; paired control roots.",
        "counts": {"signal_exports": signal_count, "control_exports": control_count,
                   "sampled_exports": signal_count+control_count,
                   "signal_cells": 10, "control_cells": 20, "scoring_jobs": len(scorers),
                   "physical_model_cells": len(cells)},
        "selection": {"seed_split": "holdout", "signal_start_index": 130000,
                      "controls_start_index": 130000,
                      "signal_paths_per_cell": {"g001": 64, "g01": 64, "g1": 64,
                                                 "high10": 32, "high30": 32},
                      "control_paths_per_cell": {"g001": 32, "g01": 32, "g1": 32,
                                                 "high10": 16, "high30": 16},
                      "C/N0_is_in_seed_identity": True,
                      "control_id_is_omitted_from_seed_identity": True},
        "source_defaults": {"encrypted": True, "RF_Hz": 8270,
                            "base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo",
                            "clock_sync": "GPS_10ms-1ms_region-0ms_offset",
                            "audio_error_ms": 70, "margin": 1, "added_symbols": 1,
                            "fhss": "off", "symbol_burst": "off",
                            "timing_bank": [0, 0, 1], "epoch_offsets": [0]},
        "decision_targets": {"strict": {"correct": .99, "noise_any_per_day": 1e-4},
                             "relaxed": {"correct": .90, "noise_any_per_12_hours": .01},
                             "family": {"receivers": 16, "durations": 8,
                                        "calibrations": 2, "attempt_spacing_seconds": 60}},
        "scoring": {"mode": "actual-noisy", "device": "cpu", "precision": "fp64",
                    "threads": 1, "memory_gib": 2,
                    "methods": ["static-fft-ucb", "gaussian-window-ucb"]},
        "limits": ["Each capture is one independently sampled path; count signal, wrong-bit, null and wrong-key admissions separately.",
                   "Control indices share seed roots with the corresponding prefix of signal indices within the same C/N0/profile/modulation; C/N0 differs across profiles and is intentionally part of seed identity.",
                   "Small null/wrong-key counts are diagnostic confidence samples, not empirical proof of 1e-4/day false admission.",
                   "Nominal timing/epoch branch is not broad blind acquisition.",
                   "Existing receiver statistics use the public two-ray basis and may mismatch the corrected selective sidebands and long dispersive memory."],
        "profiles": frozen_profiles,
        "cells": cells, "jobs": jobs, "scoring_jobs": scorers,
    }


def make_long(base, campaign, root, python, binary):
    profile = copy.deepcopy(base.profiles()[-1])
    jobs, cells, scorers = [], [], []
    for modulation in MODULATIONS:
        inputs, expected = [], []
        for offset in range(4):
            index = 140000 + offset
            source, job, truth = add_export(
                base, campaign, root, binary, profile, modulation, "signal",
                index, 4, -60, "independent-corrected-long", MAX_TIMEOUT_SECONDS)
            jobs.append(job); inputs.append(source)
            expected.append({"index": index, **truth})
        cells.append({"profile": profile["name"], "cn0_db_hz": -60,
                      "selective_model": "dispersive-dft", "modulation": modulation,
                      "control": "signal", "paths": 4,
                      "indices": [140000, 140003],
                      "balanced_bits": {"0": 2, "1": 2},
                      "seed_domains": list(DOMAINS), "expected": expected,
                      "sources": [str(p) for p in inputs]})
        scorers.append(actual_score(
            root, python, profile, inputs,
            root / f"local-evidence/triad-selective-correction/independent-corrected-long/scores/{modulation}.json",
            methods=("static-fft-ucb",), memory_gib=2, timeout=MAX_TIMEOUT_SECONDS))
    assert len(jobs) == 8 and len(scorers) == 2
    return {
        "schema": CANONICAL_SCHEMA,
        "comparison_scope": "independent-corrected-model-validation",
        "stage": "long-holdout", "experiment_stage": "independent-corrected-long",
        "status": "frozen actual-noisy long holdout; no exports or scoring have run",
        "scope": "Fresh corrected-model 850-day stable-ray observations, −60 dB-Hz, Each25/Each65, with no legacy or Off comparison.",
        "counts": {"sampled_exports": len(jobs), "signal_exports": len(jobs),
                   "scoring_jobs": len(scorers), "signal_cells": 2,
                   "physical_model_cells": len(cells)},
        "selection": {"seed_split": "holdout", "start_index": 140000,
                      "paths_per_cell": 4, "duration_seconds": 850*86400,
                      "sample_rate_hz": .25, "gui_rate_hz": .0036,
                      "carrier_spacing_hz": .00075, "ray_separation_hz": .05,
                      "excess_delay_ms": 5, "C/N0_db_hz": -60},
        "limits": ["Stable deterministic rays do not qualify renewing Gaussian/OU Doppler over 850 days.",
                   "Actual noisy reception is scored once per path; four paths per mode do not establish reliability or rare false-admission bounds.",
                   "The nominal timing/epoch branch is a public approximation, not blind acquisition."],
        "profiles": [profile],
        "cells": cells, "jobs": jobs, "scoring_jobs": scorers,
    }


def reference_long_roots(parent_path: Path) -> tuple[dict[int, dict], str]:
    parent = json.loads(parent_path.read_text())
    by_index = {}
    for job in parent.get("jobs", []):
        truth = job.get("simulation_only_expected", {})
        if ("dispersive-dft-each-65" in job.get("name", "") and
                truth.get("index") in (80000, 80001)):
            argv = job.get("argv", [])
            if (argv[argv.index("--selective-model")+1] != "dispersive-dft" or
                    argv[argv.index("--selective-perturbation")+1] != "each-65" or
                    argv[argv.index("--rate")+1] != "0.0036" or
                    argv[argv.index("--cn0")+1] != "-60.0" or
                    argv[argv.index("--sample-rate-denominator")+1] != "4"):
                raise ValueError("sampling-refinement parent job has different rate, C/N0, Fs, or selective model")
            if truth.get("control") != "signal":
                raise ValueError("sampling-refinement parent jobs must be signal captures")
            if truth["index"] in by_index and truth != by_index[truth["index"]]:
                raise ValueError("duplicate parent jobs disagree on the original six seed roots")
            by_index[truth["index"]] = truth
    if set(by_index) != {80000, 80001}:
        raise ValueError("parent long-replay plan lacks corrected Each65 paths 80000 and 80001")
    sha = hashlib.sha256(parent_path.read_bytes()).hexdigest()
    return by_index, sha


def make_refinement(base, campaign, root, binary, parent_path):
    roots, parent_sha = reference_long_roots(parent_path)
    profile = copy.deepcopy(base.profiles()[-1])
    profile["fs"] = 1
    profile["denominator"] = 2
    jobs, cells = [], []
    for index in (80000, 80001):
        source, argv, derived = base.parser_args(
            campaign, root, binary, profile, "dispersive-dft", "each-65",
            "independent-corrected-refinement", index, 2, -60, "holdout", control="signal")
        if derived["source_bit"] != roots[index]["source_bit"]:
            raise ValueError("refinement path bit differs from frozen long-replay path")
        # Reuse the exact six source/channel/oscillator/noise/key/frontend roots
        # from the frozen long replay, while keeping Fs=.5 Hz in the new command.
        for domain in DOMAINS:
            option = f"--{domain}-seed"
            offset = argv.index(option)+1
            argv[offset] = str(roots[index][domain+"_seed"])
        job = {"name": f"stable850-m60-dispersive-dft-each-65-fs0p5-p{index:05d}",
               "timeout_seconds": MAX_TIMEOUT_SECONDS, "argv": argv,
               "simulation_only_expected": {"index": index, **roots[index]}}
        jobs.append(job)
        cells.append({"profile": "stable850-m60-fs0p5", "cn0_db_hz": -60,
                      "selective_model": "dispersive-dft", "modulation": "each-65",
                      "control": "signal", "index": index, "paths": 1,
                      "source_roots_copied_from_parent": True,
                      "expected": [job["simulation_only_expected"]],
                      "sources": [str(source)]})
    return {
        "schema": CANONICAL_SCHEMA,
        "comparison_scope": "independent-corrected-model-validation",
        "kind": "sampling-refinement-energy-diagnostic",
        "stage": "sampling-refinement", "experiment_stage": "independent-corrected-refinement",
        "status": "frozen 2-observation sampling-refinement plan; no exports have run",
        "source_seed_parent": {"path": str(parent_path), "sha256": parent_sha,
                               "model": "dispersive-dft", "modulation": "each-65",
                               "indices": [80000, 80001]},
        "counts": {"sampled_exports": 2, "paired_noise_arrays": 0,
                   "scoring_jobs": 0, "physical_model_cells": len(cells)},
        "comparison": {"reference_fs_hz": .25, "refined_fs_hz": .5,
                       "requested_active_seconds": 850*86400,
                       "same_physical_seed_roots": True,
                       "same_sampled_noise": False,
                       "purpose": "compare pre/post-stage energy, captured energy and projection retention over the same requested physical duration; do not pool as independent receiver trials"},
        "limits": ["The original six roots are deliberately copied because Fs participates in ordinary campaign seed derivation; this is a sampling-refinement coupling, not ordinary independent seed generation.",
                   "Higher sample rate changes discrete channel/noise realizations, so equal seeds do not make I/Q noise arrays paired.",
                   "This plan is only a diagnostic; its two observations do not qualify sensitivity."],
        "profiles": [{**profile, "name": "stable850-m60-fs0p5"}],
        "cells": cells, "jobs": jobs, "scoring_jobs": [],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT_DEFAULT)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--source-revision", required=True,
                        help="full source Git revision from the parent corrected-model plan")
    parser.add_argument("--parent-plan", type=Path, default=PARENT_PLAN_DEFAULT)
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="fresh directory; writes three separate plan JSON files")
    args = parser.parse_args()
    root = args.root.resolve()
    binary = args.binary.expanduser().resolve(strict=True)
    parent_path = args.parent_plan.expanduser().resolve(strict=True)
    base, base_path = load_base(root)
    campaign, campaign_path = load_lowrate(root)
    revision, origin = base.resolve_source_revision(root, args.source_revision)
    parent = json.loads(parent_path.read_text())
    parent_source = parent.get("source", {})
    expected_hash = parent_source.get("binary_sha256")
    actual_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    if expected_hash and actual_hash != expected_hash:
        raise SystemExit(f"binary SHA256 {actual_hash} does not match parent plan {expected_hash}")
    if revision != parent_source.get("revision"):
        raise SystemExit("explicit source revision does not match the parent frozen plan")
    for relative_path, expected_file_hash in parent_source.get("source_file_sha256", {}).items():
        source_path = root/relative_path
        if not source_path.is_file() or campaign.sha256_file(source_path) != expected_file_hash:
            raise SystemExit(f"source input differs from frozen parent plan: {relative_path}")
    output_dir = args.output_dir.expanduser().resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise SystemExit(f"refusing to write into nonempty output directory: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    plans = {
        "independent-corrected-holdouts.json": make_short_holdouts(
            base, campaign, root, args.python, binary),
        "independent-corrected-long.json": make_long(
            base, campaign, root, args.python, binary),
        "independent-corrected-refinement.json": make_refinement(
            base, campaign, root, binary, parent_path),
    }
    source_hashes = dict(parent_source.get("source_file_sha256", {}))
    source_hashes[str(Path(__file__).resolve().relative_to(root))] = campaign.sha256_file(Path(__file__).resolve())
    source_bundle_hash = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest()
    for name, plan in plans.items():
        plan["source"] = {
            "revision": revision, "revision_origin": origin,
            "parent_plan_path": str(parent_path),
            "parent_plan_sha256": hashlib.sha256(parent_path.read_bytes()).hexdigest(),
            "source_file_sha256": source_hashes,
            "source_bundle_sha256": source_bundle_hash,
            "base_campaign_helper_sha256": campaign.sha256_file(base_path),
            "lowrate_seed_helper_sha256": campaign.sha256_file(campaign_path),
            "binary_sha256": actual_hash,
        }
        path = output_dir/name
        if path.exists():
            raise SystemExit(f"refusing to overwrite: {path}")
        path.write_text(json.dumps(plan, indent=2, allow_nan=False)+"\n")
    print(json.dumps({name: plan["counts"] for name, plan in plans.items()}, indent=2))


if __name__ == "__main__":
    main()


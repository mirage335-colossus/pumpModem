#!/usr/bin/env python3
"""Freeze fresh sampled observations for the preselected consistency AND guard.

This generator launches nothing. It freezes full-bank acquisition jobs and the
ln(100) primary guard before these new samples exist. Runnable guard commands
must be bound to actual completed full-score hashes later; none are invented.
"""
from __future__ import annotations
import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
PARENT = ROOT / "local-evidence/triad-acquisition-improvement/campaign-plan/campaign-plan.json"
GUARD_HASH = "8b6d245dfa36acf24c57fcf2afee61777a3c1faab226a9fc3e602701c92cd9b5"


def helpers():
    path = HERE / "make_plan.py"
    spec = importlib.util.spec_from_file_location("final_validation_parent", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def make_plan(parent_path):
    h, helper_path = helpers()
    parent = json.loads(parent_path.read_text())
    for name, expected in parent["source_file_sha256"].items():
        h.require(h.sha256(ROOT / name) == expected, f"frozen source guard failed: {name}")
    h.require(h.sha256(HERE / "pattern_consistency.py") == GUARD_HASH,
              "preselected pattern-consistency implementation changed")
    root = Path(parent["root"])
    parent_hash = h.sha256(parent_path)
    source_key = parent["source_file_inventory_sha256"]
    export_allowlist = {"--seconds", "--cn0", "--control", "--signal-bit", "--epoch", "--output",
                        "--export-max-samples", "--export-max-mib",
                        *("--" + d + "-seed" for d in h.SEED_DOMAINS)}
    score_allowlist = {"--group-tile", "--output", "--scores-output"}
    strata = (("signal240-cn8", "signal", 240, 8, 250000, 16),
              ("wrongkey120-cn12", "wrong-key", 120, 12, 260000, 8),
              ("wrongkey120-cn20", "wrong-key", 120, 20, 270000, 8))
    exports, scores, guards, cells = [], [], [], []
    for label, control, seconds, cn0, first, count in strata:
        parent_name = ("paired-high30-each-65-cn8-p120000-b16-signal" if control == "signal"
                       else "control-high30-each-65-cn20-p230000-b16-wrong-key")
        original = next(j for j in parent["jobs"] if j["name"] == parent_name)
        original_score = next(j for j in parent["scoring_jobs"] if j["name"] == parent_name + "-r1")
        members = []
        for index in range(first, first + count):
            identity = h.independent_identity(index, control)
            name = f"final-{label}-p{index}"
            members.append(name)
            workspace = root / "local-evidence/triad-acquisition-improvement/final-validation/workspaces" / name
            report = root / "local-evidence/triad-acquisition-improvement/final-validation/scores" / (name + ".json")
            guard_report = root / "local-evidence/triad-acquisition-improvement/final-validation/guards" / (name + ".json")
            argv = list(original["argv"])
            settings = {"--seconds": seconds, "--cn0": cn0, "--control": control,
                        "--signal-bit": identity["source_bit"], "--epoch": identity["epoch"],
                        "--output": workspace, "--export-max-samples": 1100000, "--export-max-mib": 4096}
            settings.update({"--" + d + "-seed": identity[d + "_seed"] for d in h.SEED_DOMAINS})
            for flag, value in settings.items():
                h.set_option(argv, flag, value)
            before, after = h.options(original["argv"]), h.options(argv)
            changed = {k for k in before | after if before.get(k) != after.get(k)}
            h.require(changed <= export_allowlist, "export changed outside scientific allowlist")
            scientific = copy.deepcopy(after)
            scientific.pop("--output"); scientific.pop("--mode")
            input_key = {"scientific_options": scientific, "source_file_inventory_sha256": source_key}
            binding = {"capture_id": name, "capture_input_key_sha256": h.key_hash(input_key),
                       "source_file_inventory_sha256": source_key,
                       "simulation_only_expected": identity,
                       "seed_origin": {"namespace": h.NAMESPACE, "index": index,
                                       "algorithm": "make_plan.independent_identity; compact JSON [namespace,index,domain], SHA256 first8 bytes little-endian; no C/N0 dependence"},
                       "parent_campaign_sha256": parent_hash,
                       "parent_export_job_sha256": h.key_hash(original),
                       "parent_score_job_sha256": h.key_hash(original_score)}
            common = {"cell": label, "timeout_seconds": 600, "input_binding": binding}
            exports.append({"name": name, "argv": argv, "input_key": input_key,
                            "scientific_diff_flags": sorted(changed), **copy.deepcopy(common)})
            score = list(original_score["argv"])
            h.require(score[2] == before["--output"][0], "parent score input mismatch")
            score[2] = str(workspace)
            for flag, value in (("--group-tile", 2 if seconds == 240 else 4),
                                ("--output", report), ("--scores-output", report.with_suffix(".scores.npz"))):
                h.set_option(score, flag, value)
            old_score = h.options(["score", *original_score["argv"][3:]])
            new_score = h.options(["score", *score[3:]])
            score_changes = {k for k in old_score | new_score if old_score.get(k) != new_score.get(k)}
            h.require(score_changes <= score_allowlist, "receiver changed outside allowlist")
            scores.append({"name": name + "-score", "argv": score, "depends_on_export": name,
                           "receiver_diff_flags": sorted(score_changes), **copy.deepcopy(common)})
            guards.append({"capture_id": name, "workspace": str(workspace), "full_score": str(report),
                           "full_score_sha256": None, "output": str(guard_report),
                           "driver": str(root / "docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction/pattern_consistency.py"),
                           "driver_sha256": GUARD_HASH, "doppler_width_hz": 30,
                           "primary_guard": "ln100", "diagnostic_guard": "zero",
                           "requires_actual_full_score_hash_binding_before_execution": True,
                           "input_binding": copy.deepcopy(binding)})
        cells.append({"name": label, "control": control, "seconds": seconds, "cn0_db_hz": cn0,
                      "indices": [first, first + count - 1], "paths": count,
                      "source_bit_counts": {"0": count // 2, "1": count // 2}, "export_jobs": members})
    configure = []
    for cell in cells:
        job = next(j for j in exports if j["name"] == cell["export_jobs"][0])
        argv = list(job["argv"]); h.set_option(argv, "--mode", "configure")
        configure.append({"name": "configure-" + job["name"], "argv": argv, "timeout_seconds": 30,
                          "input_binding": copy.deepcopy(job["input_binding"])})
    vectors = {tuple(j["input_binding"]["simulation_only_expected"][d + "_seed"] for d in h.SEED_DOMAINS) for j in exports}
    h.require(len(exports) == len(scores) == len(guards) == len(vectors) == 32, "final matrix count or seed collision")
    budgets = {"receiver_probability_reference": copy.deepcopy(parent["receiver"]),
               "export_timeout_seconds": 600, "score_timeout_seconds": 600,
               "export_max_samples": 1100000, "export_max_mib": 4096,
               "export_memory_mib": 128, "score_memory_mib": 1024,
               "group_tile_by_duration": {"120": 4, "240": 2}, "runner_concurrency": "Execution owner admits work; existing runner supports at most 16. No 24-worker override is authorized by this plan."}
    return {"schema": "triad-acquisition-final-validation-plan-v1",
            "stage": "fresh physical cases; primary guard frozen before these observations",
            "parent_campaign": {"path": str(parent_path.resolve()), "sha256": parent_hash},
            "generator_dependencies": {"make_plan.py": h.sha256(helper_path), "pattern_consistency.py": GUARD_HASH},
            "source_file_sha256": copy.deepcopy(parent["source_file_sha256"]),
            "source_file_inventory_sha256": source_key,
            "source_guard": "All 11 frozen parent dependencies and the preselected guard implementation must match. Remote binary/source-bundle binding remains an execution prerequisite.",
            "counts": {"signal": 16, "wrong_key_cn12": 8, "wrong_key_cn20": 8,
                       "independent_physical_cases": 32, "exports": 32, "full_scores": 32, "pending_guards": 32, "configure_jobs": 3},
            "cells": cells, "budgets": budgets, "budgets_sha256": h.key_hash(budgets),
            "scientific_diff_allowlist": {"export_flags": sorted(export_allowlist),
                                          "receiver_flags": sorted(score_allowlist), "receiver_positional": "one workspace path only"},
            "primary_decision": {"existing_gate": "retain both strict and relaxed full-bank noise gates separately",
                                 "added_guard": "C_window for the unchanged winning bit at its best group >= ln(100)",
                                 "threshold": math.log(100), "combination": "AND existing noise admission",
                                 "zero_threshold": "diagnostic only; never substituted as the primary result"},
            "interpretation_limits": [
                "This is independent validation after selecting the guard on development cases. Every root and epoch is freshly derived with the existing independent_identity function; index ranges 250000/260000/270000 do not reuse earlier <240000 roots.",
                "Six seed roots, not simulator truth, govern each new observation. Source-bit labels are read only after decoding for evaluation; all 109 timing groups remain in the original full-bank receiver.",
                "The AND guard can only remove an existing admission. Its contrast is an uncalibrated pattern-consistency proxy, not authentication, a wrong-key Bayes factor or a correct-reception guarantee.",
                "Sixteen signal cases and eight wrong-key cases per cell yield wide intervals and do not establish 99% reliability or a rare operational wrong-key rejection rate. Fixed balanced strata and all misses/errors remain explicit.",
                "Guard full-score hashes are intentionally absent until actual full-score files exist. A later runnable guard plan must verify and bind those hashes, unchanged workspace hashes and the frozen driver SHA256.",
                "Nominal C/N0, actual duration/energy, source tile rounding, complete tails and all failures must be reported. No ordinary Robust receiver integration is included.",
            ],
            "jobs": exports, "scoring_jobs": scores, "pending_guard_bindings": guards, "configure_jobs": configure}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-plan", type=Path, default=PARENT)
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "local-evidence/triad-acquisition-improvement/final-validation-plan")
    args = parser.parse_args(); plan = make_plan(args.parent_plan.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    common = {k: v for k, v in plan.items() if k not in ("jobs", "scoring_jobs", "pending_guard_bindings", "configure_jobs")}
    outputs = {"final-validation-plan.json": plan, "export-plan.json": {**common, "jobs": plan["jobs"]},
               "score-plan.json": {**common, "jobs": plan["scoring_jobs"]},
               "configure-plan.json": {**common, "jobs": plan["configure_jobs"]},
               "pending-guard-bindings.json": {**common, "pending_guard_bindings": plan["pending_guard_bindings"]}}
    for name, value in outputs.items():
        (args.output_dir / name).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Freeze paired 240-second and conditional 480-second acquisition experiments.

No export or scorer is launched here. The default job list contains only the
240-second exports; the separate 480-second plans require an explicit later
decision after reviewing the 240-second outcomes and remaining run budget.
"""
from __future__ import annotations
import argparse
import copy
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
PARENT = ROOT / "local-evidence/triad-acquisition-improvement/campaign-plan/campaign-plan.json"


def helpers():
    path = HERE / "make_plan.py"
    spec = importlib.util.spec_from_file_location("duration_parent_generator", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def make_plan(parent_path):
    h, helper_path = helpers()
    parent = json.loads(parent_path.read_text())
    for name, expected in parent["source_file_sha256"].items():
        h.require(h.sha256(ROOT / name) == expected, f"frozen source guard failed: {name}")
    root = Path(parent["root"])
    parent_hash = h.sha256(parent_path)
    export_allowlist = {"--seconds", "--output", "--export-max-samples", "--export-max-mib"}
    score_allowlist = {"--group-tile", "--output", "--scores-output"}
    stages = {}
    for seconds, timeout, tile in ((240, 600, 2), (480, 1200, 1)):
        exports, scores = [], []
        for index in range(120000, 120004):
            parent_name = f"paired-high30-each-65-cn8-p{index}-b16-signal"
            selected = [j for j in parent["jobs"] if j["name"] == parent_name]
            selected_scores = [j for j in parent["scoring_jobs"] if j["name"] == parent_name + "-r1"]
            h.require(len(selected) == len(selected_scores) == 1, f"missing unique paired parent {parent_name}")
            original, original_score = selected[0], selected_scores[0]
            name = parent_name + f"-t{seconds}"
            workspace = root / "local-evidence/triad-acquisition-improvement/duration-extension/workspaces" / name
            report = root / "local-evidence/triad-acquisition-improvement/duration-extension/scores" / (name + "-r1.json")
            command = list(original["argv"])
            for flag, value in (("--seconds", seconds), ("--output", workspace),
                                ("--export-max-samples", 1100000), ("--export-max-mib", 4096)):
                h.set_option(command, flag, value)
            before, after = h.options(original["argv"]), h.options(command)
            changed = {k for k in before | after if before.get(k) != after.get(k)}
            h.require(changed == export_allowlist, "duration export changed outside its exact allowlist")
            score_command = list(original_score["argv"])
            h.require(score_command[2] == before["--output"][0], "parent score workspace mismatch")
            score_command[2] = str(workspace)
            for flag, value in (("--group-tile", tile), ("--output", report),
                                ("--scores-output", report.with_suffix(".scores.npz"))):
                h.set_option(score_command, flag, value)
            # The sole positional input is checked separately from receiver flags.
            score_before = h.options(["receiver", *original_score["argv"][3:]])
            score_after = h.options(["receiver", *score_command[3:]])
            score_changed = {k for k in score_before | score_after if score_before.get(k) != score_after.get(k)}
            h.require(score_changed == score_allowlist, "duration receiver changed outside its exact allowlist")
            scientific = copy.deepcopy(after)
            scientific.pop("--output"); scientific.pop("--mode")
            input_key = {"scientific_options": scientific,
                         "source_file_inventory_sha256": parent["source_file_inventory_sha256"]}
            binding = copy.deepcopy(original["input_binding"])
            binding.update(capture_id=name, capture_input_key_sha256=h.key_hash(input_key),
                           parent_capture_id=parent_name,
                           parent_capture_input_key_sha256=original["input_binding"]["capture_input_key_sha256"],
                           parent_campaign_sha256=parent_hash,
                           parent_export_job_sha256=h.key_hash(original),
                           parent_score_job_sha256=h.key_hash(original_score))
            common = {"timeout_seconds": timeout, "requested_seconds": seconds,
                      "conditional_second_stage": seconds == 480, "input_binding": binding}
            exports.append({"name": name, "argv": command, "input_key": input_key,
                            "scientific_diff_flags": sorted(changed), **copy.deepcopy(common)})
            scores.append({"name": name + "-r1", "argv": score_command, "depends_on_export": name,
                           "reblock": 1, "receiver_diff_flags": sorted(score_changed),
                           "changed_positional_input": True, **copy.deepcopy(common)})
        stages[str(seconds)] = {"jobs": exports, "scoring_jobs": scores,
                               "export_count": 4, "score_count": 4,
                               "timeout_seconds_per_job": timeout}
    configure_jobs = []
    for job in stages["240"]["jobs"]:
        argv = list(job["argv"]); h.set_option(argv, "--mode", "configure")
        configure_jobs.append({"name": "configure-" + job["name"], "argv": argv,
                               "timeout_seconds": 30, "input_binding": copy.deepcopy(job["input_binding"])})
    budgets = {"parent_receiver_reference": copy.deepcopy(parent["receiver"]),
               "export_max_samples": 1100000, "export_max_mib": 4096,
               "export_memory_mib": 128, "score_memory_mib": 1024,
               "240": {"timeout_seconds": 600, "group_tile": 2},
               "480": {"timeout_seconds": 1200, "group_tile": 1}}
    return {
        "schema": "triad-acquisition-duration-extension-plan-v1",
        "stage": "240 seconds first; 480 seconds is a separately authorized conditional followup",
        "parent_campaign": {"path": str(parent_path.resolve()), "sha256": parent_hash},
        "generator_dependency": {"path": str(helper_path), "sha256": h.sha256(helper_path)},
        "source_file_sha256": copy.deepcopy(parent["source_file_sha256"]),
        "source_file_inventory_sha256": parent["source_file_inventory_sha256"],
        "source_guard": "Generation fails unless every listed frozen parent source file still has its recorded SHA256. The execution owner must enforce this guard and binary/source-bundle binding on the remote host.",
        "scientific_diff_allowlist": {"export_flags": sorted(export_allowlist),
                                      "receiver_flags": sorted(score_allowlist),
                                      "receiver_positional": "one workspace path only"},
        "budgets": budgets, "budgets_sha256": h.key_hash(budgets),
        "counts": {"initial_exports": 4, "initial_scores": 4,
                   "conditional_additional_exports": 4, "conditional_additional_scores": 4,
                   "distinct_paired_seed_vectors": 4, "configure_jobs": 4},
        "execution_order": ["Run the four 240-second exports, then their four r1 scorers.",
                            "Review correct/wrong/missed outcomes and whether a longer duration would establish a useful bracket.",
                            "Only then explicitly admit 480-second exports/scoring if 240 seconds missed/decoded wrongly or the comparison remains useful, and the run deadline permits it. The generator never makes that decision."],
        "interpretation_limits": [
            "Six seed roots, source bit, epoch, nominal transmit power/CN0, waveform rate, all 109 public timing groups, selective channel, Doppler/delay, oscillator and receiver probability budgets are unchanged from each matching parent. These are paired duration variants, not new independent channels.",
            "Longer source durations generate genuinely longer sampled observations. They are not array repetition or noise rescaling. Duration-dependent source tile rounding and complete tails must be checked in completed manifests.",
            "Equal roots do not by themselves prove an identical waveform/noise prefix when source duration changes; any prefix or common-realization claim requires explicit sample evidence.",
            "Receiver r1 and physical likelihood window .25 seconds are fixed. Group tile changes only the computation tiling; it is not a changed channel or likelihood window.",
            "Optional 480-second selection is adaptive. No anytime, sequential or simultaneous qualification is inferred from this small paired duration bracket.",
            "All generated workspace hashes/manifests, commands, outcomes and scoring arrays must be retained according to the parent policy; full raw retention is reported only where verified.",
        ],
        "jobs": stages["240"]["jobs"], "scoring_jobs": stages["240"]["scoring_jobs"],
        "optional_stage_480": stages["480"], "configure_jobs": configure_jobs,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-plan", type=Path, default=PARENT)
    parser.add_argument("--output-dir", type=Path,
                        default=ROOT / "local-evidence/triad-acquisition-improvement/duration-plan")
    args = parser.parse_args()
    plan = make_plan(args.parent_plan.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    common = {k: v for k, v in plan.items() if k not in ("jobs", "scoring_jobs", "optional_stage_480", "configure_jobs")}
    outputs = {"duration-plan.json": plan,
               "240-export-plan.json": {**common, "jobs": plan["jobs"]},
               "240-score-plan.json": {**common, "jobs": plan["scoring_jobs"]},
               "480-export-plan.json": {**common, "jobs": plan["optional_stage_480"]["jobs"]},
               "480-score-plan.json": {**common, "jobs": plan["optional_stage_480"]["scoring_jobs"]},
               "configure-plan.json": {**common, "jobs": plan["configure_jobs"]}}
    for name, value in outputs.items():
        (args.output_dir / name).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()

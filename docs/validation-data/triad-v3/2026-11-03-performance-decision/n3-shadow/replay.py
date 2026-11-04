#!/usr/bin/env python3
"""Recheck the shadow calculation using the actual maintained decision helper.

Reads archived observations only. No receiver workload or timing is executed.
Requires the source-matched NumPy/SciPy environment and adaptive helper.
"""
import gzip
import hashlib
import json
from pathlib import Path
import sys
from functools import lru_cache

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
HELPER_SHA = "07e9e243e2e655b17887382712c7ff099855a07d6dc3985c03c430df2a113257"
helper = ROOT / "tools/triad_perf_adaptive.py"
assert hashlib.sha256(helper.read_bytes()).hexdigest() == HELPER_SHA, "restore the archived adaptive helper first"
sys.path.insert(0, str(ROOT / "tools"))
from triad_perf_adaptive import AdaptivePolicy, summarize


@lru_cache(maxsize=None)
def read(path):
    with gzip.open(path, "rt") as stream:
        return json.load(stream)


def observations(base, identifier):
    legacy = read(base / "native-fft.json.gz")
    case = next((r for r in legacy["cases"] if r["id"] == identifier), None)
    if case is None:
        case = read(base / "sampled" / identifier / "result.json.gz")
        values = {s: [r[s]["complete_pipeline_wall_seconds"] for r in case["training"]]
                  for s in ("reference", "candidate")}
        multipliers = [1] * len(values["reference"])
    elif "raw_full_seconds" in case:
        values = case["raw_full_seconds"]
        multipliers = [case["calls_per_outer_block"]] * len(values["reference"])
    elif isinstance(case["raw"]["reference"], list):
        values = {s: [r["full_test_wall_seconds"] for r in case["raw"][s]]
                  for s in ("reference", "candidate")}
        multipliers = [1] * len(values["reference"])
    else:
        values = {s: case["raw"][s]["direct"] for s in ("reference", "candidate")}
        multipliers = [r["invocation_count"] for r in case["raw"]["reference"]["blocks"]]
    costs = [(a+b)*m for a, b, m in zip(values["reference"], values["candidate"], multipliers, strict=True)]
    return values, costs


def decision(values, looks):
    policy = AdaptivePolicy(stopping_rule="decision", looks=looks)
    for n in looks:
        if n > len(values["reference"]):
            return None
        row = summarize(values["reference"][:n], values["candidate"][:n], policy,
                        model_validation="direct_full")
        if row["stop_reason"] != "continue":
            return row


def main():
    shadow = json.loads((HERE / "replay.json").read_text())
    old = HERE.parent.parent / "2026-11-03-performance-complete/routine/cpu-complete-001"
    new = HERE.parent / "routine/cpu-decision-001"
    totals = {"old_default": 0., "old_shadow": 0., "new_observed": 0., "new_shadow": 0.}
    corrections = []
    for item in shadow["case_results"]:
        for label, base in (("old", old), ("new", new)):
            values, costs = observations(base, item["case_id"])
            result = decision(values, (3, 5, 9, 17, 33))
            assert result is not None, (item["case_id"], "unavailable prefix")
            saved = item[label + "_n3_shadow"]
            assert result["performance_gate"] == saved["gate"]
            assert result["sample_count"] == saved["n"]
            upper = result["paired_candidate_over_reference"]["model_adjusted_gate_interval"][1]
            assert abs(upper - saved["upper"]) < 1e-10, (item["case_id"], upper, saved["upper"])
            cost = sum(costs[:result["sample_count"]])
            if abs(cost - item[label + "_n3_timer_cost_seconds"]) >= 1e-8:
                corrections.append({"case": item["case_id"], "capture": label,
                                    "reported_seconds": item[label + "_n3_timer_cost_seconds"],
                                    "recorded_multiplicity_seconds": cost})
            totals[label + "_shadow"] += cost
            if label == "old":
                original = decision(values, (5, 9, 17, 33))
                assert original["sample_count"] == item["old_default_stop_n"]
                totals["old_default"] += sum(costs[:original["sample_count"]])
            else:
                totals["new_observed"] += sum(costs)
    print(json.dumps({"verified_cases": len(shadow["case_results"]), "operation_timer_sums": totals,
                      "new_omitted_operation_seconds": totals["new_observed"]-totals["new_shadow"],
                      "cost_corrections": corrections,
                      "no_receiver_or_timing_work_executed": True}, indent=2))


if __name__ == "__main__":
    main()

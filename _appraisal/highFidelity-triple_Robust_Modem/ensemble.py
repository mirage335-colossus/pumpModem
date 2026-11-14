#!/usr/bin/env python3
"""Analyze native HF screens and materialize the predeclared bounded follow-ups.

No signal generation or receiver processing lives here. This consumes complete
native receipts and never converts a shadow likelihood into a message outcome.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import run as runner


def screen_reference(receipt: dict) -> dict:
    search = receipt["hf_public_search"]
    evaluation = receipt["hf_search_evaluation"]
    expected = evaluation["expected_receiver_bit"]
    pair = search["phase_refinement"]["combined_bit_log_e"]
    if expected not in (None, 0, 1) or isinstance(expected, bool):
        raise runner.CampaignError("screen requires a known evaluator bit")
    if len(pair) != 2 or any(x is not None and not runner._is_finite_number(x) for x in pair):
        raise runner.CampaignError("invalid screen evidence")
    scores = [float("-inf") if x is None else x for x in pair]
    peak = max(scores)
    presence = (peak + math.log(sum(math.exp(x - peak) for x in scores)) - math.log(2) - math.log(4)
                if math.isfinite(peak) else float("-inf"))
    gap = (scores[expected] - scores[1 - expected] if math.isfinite(scores[expected]) else float("-inf")) if expected is not None else None
    passes = (presence >= search["presence_reference_threshold"] and gap > 0 and
              gap >= search["bit_separation_reference"]) if gap is not None else None
    return {"passes_reference": passes,
            "presence_log_e_after_outer_weight": presence if math.isfinite(presence) else None,
            "presence_reference_exceeded": presence >= search["presence_reference_threshold"],
            "correct_bit_separation": gap if gap is not None and math.isfinite(gap) else None,
            "separation_status": "not_applicable" if gap is None else
                ("finite" if math.isfinite(gap) else ("positive_infinity" if gap > 0 else "negative_infinity")),
            "refinement_executed": search["phase_refinement"].get("executed", True),
            "refinement_unavailable_reason": search["phase_refinement"].get("unavailable_reason", ""),
            "scope": "shadow references only; not a native decision or message outcome"}


def followups(campaign: dict, run: dict, receipts: dict[str, dict]) -> tuple[dict | None, list[dict]]:
    """Select up to two misses in frozen manifest order, preserving censoring."""
    trials = {t["episode"]["id"]: t for t in run["trials"]}
    if len(trials) != len(run["trials"]):
        raise runner.CampaignError("duplicate native trial identity")
    chosen, audit, episodes = [], [], []
    for episode in campaign["episodes"]:
        trial = trials.get(episode["id"])
        if trial is None or trial.get("status") != "completed":
            audit.append({"id": episode["id"], "status": trial.get("status") if trial else "not_executed"})
            continue
        if trial["episode"] != episode:
            raise runner.CampaignError("native trial differs from frozen episode")
        receipt = receipts[episode["id"]]
        if receipt.get("status") != "completed":
            raise runner.CampaignError("completed trial needs a completed native receipt")
        runner._validate_completed_receipt(receipt, episode)
        result = screen_reference(receipt)
        audit.append({"id": episode["id"], "status": "completed", **result})
        if episode.get("control", "wanted") != "wanted" or result["passes_reference"] or len(chosen) == 2:
            continue
        chosen.append(episode["id"])
        variants = [("physical", 0, 1, True), ("plus3", 3, 1, False),
                    ("minus3", -3, 1, False), ("double-minus3", -3, 2, False)]
        for suffix, delta, scale, diagnostic in variants:
            item = dict(episode, id=episode["id"] + "-" + suffix,
                        cn0_db_hz=episode["cn0_db_hz"] + delta,
                        bit_seconds=episode["bit_seconds"] * scale,
                        hf_physical_diagnostic=diagnostic)
            episodes.append(item)
    if not episodes:
        return None, audit
    return {"schema_version": 1, "name": "hf-independent-boundaries",
            "purpose": "Conditional follow-ups in frozen manifest order: " + ", ".join(chosen) +
                ". Operational fixed-launch brackets; no monotonicity assumption or population estimate.",
            "max_wall_seconds": 800, "per_trial_wall_seconds": 100,
            "max_trials": 8, "max_samples": 8000000, "episodes": episodes}, audit


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    campaign = runner._read_campaign(args.campaign)
    if campaign["name"] != "hf-independent-screen":
        raise runner.CampaignError("this stopping rule applies only to the frozen independent screen")
    data = json.loads(args.run.read_text())
    if data["campaign_sha256"] != hashlib.sha256(runner._json_bytes(campaign)).hexdigest():
        raise runner.CampaignError("run does not bind the supplied frozen campaign")
    receipts = {}
    for trial in data["trials"]:
        if trial.get("status") == "completed":
            path = (args.run.parent / trial["raw_stdout"]).resolve()
            if path.parent != args.run.parent.resolve():
                raise runner.CampaignError("native receipt must remain in the run directory")
            receipts[trial["episode"]["id"]] = runner._parse_receipt(path)
    generated, audit = followups(campaign, data, receipts)
    print(json.dumps({"run_sha256": hashlib.sha256(args.run.read_bytes()).hexdigest(),
                      "screen_audit": audit, "followup_trials": len(generated["episodes"]) if generated else 0}, indent=2))
    if generated:
        destination = args.output.resolve()
        if not destination.is_relative_to(runner.HERE.resolve()):
            raise runner.CampaignError("follow-up manifest must remain in the appraisal subtree")
        with destination.open("x") as stream:
            json.dump(generated, stream, indent=2)
            stream.write("\n")
        runner._read_campaign(destination)


if __name__ == "__main__":
    main()

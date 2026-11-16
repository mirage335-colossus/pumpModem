#!/usr/bin/env python3
"""Retrospective finite audit of frozen protected-oblique repair receipts.

This reads completed native receipts and their six dependent admission replays.
It performs no DSP, does not select a threshold, and is not operational
qualification or a population reliability estimate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Any

import run


APP = Path(__file__).resolve().parent
EVIDENCE = APP / "local-evidence"
CAMPAIGNS = {
    "discriminator": APP / "campaigns/hf-protected-oblique-discriminator.json",
    "preservation": APP / "campaigns/hf-protected-oblique-preservation.json",
}
RUN_NAMES = {
    "discriminator": "hf-protected-oblique-current",
    "preservation": "hf-protected-oblique-current-preservation",
}
POLICIES = ("current", "guarded", "strict", "very-strict", "extreme", "maximum")
EXPECTED_IDS = {
    "discriminator": ("oblique-protected-e", "oblique-protected-foreign-key"),
    "preservation": ("oblique-protected-fresh-a", "oblique-protected-tone-noise"),
}
LONGER_CAMPAIGNS = {
    "discriminator": APP / "campaigns/hf-protected-oblique-duration-followup.json",
    "preservation": APP / "campaigns/hf-protected-oblique-duration-preservation.json",
}
LONGER_RUN_NAMES = {
    "discriminator": "hf-protected-oblique-current-duration",
    "preservation": "hf-protected-oblique-current-duration-preservation",
}
LONGER_EXPECTED_IDS = {
    "discriminator": ("oblique-protected-e-longer",),
    "preservation": ("oblique-protected-fresh-a-longer", "oblique-protected-foreign-key-longer",
                     "oblique-protected-tone-noise-longer"),
}


class AuditError(ValueError):
    """Frozen evidence is absent, inconsistent, or incomplete."""


def _sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AuditError(f"cannot read {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise AuditError(f"{label} must be a JSON object")
    return value


def _inside(base: Path, path: Path, label: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(base.resolve(strict=True))
    except (OSError, ValueError) as exc:
        raise AuditError(f"{label} must resolve beneath {base}") from exc
    if not resolved.is_file():
        raise AuditError(f"{label} is not a regular file")
    return resolved


def _load_one(key: str, run_dir: Path, *, longer: bool = False) -> dict[str, Any]:
    campaign_path = (LONGER_CAMPAIGNS if longer else CAMPAIGNS)[key]
    campaign = run._read_campaign(campaign_path)
    campaign_file_sha = _sha(campaign_path)
    campaign_sha = hashlib.sha256(run._json_bytes(campaign)).hexdigest()
    expected_ids = (LONGER_EXPECTED_IDS if longer else EXPECTED_IDS)[key]
    episodes = {episode["id"]: episode for episode in campaign["episodes"]}
    if tuple(episodes) != expected_ids:
        raise AuditError(f"maintained {key} campaign episode order/identity changed")
    if campaign.get("name") != campaign_path.stem:
        raise AuditError(f"maintained {key} campaign name changed")

    try:
        run_dir = run_dir.resolve(strict=True)
    except OSError as exc:
        raise AuditError(f"{key} completed run directory is unavailable: {exc}") from exc
    try:
        run_dir.relative_to(EVIDENCE.resolve(strict=True))
    except (OSError, ValueError) as exc:
        raise AuditError("run directories must remain inside this appraisal's local-evidence") from exc
    frozen_path = _inside(run_dir, run_dir / "frozen-input.json", "frozen campaign input")
    run_path = _inside(run_dir, run_dir / "run.json", "campaign run record")
    frozen = _json(frozen_path, "frozen campaign input")
    record = _json(run_path, "campaign run record")
    probe_sha = record.get("probe_sha256")
    if not isinstance(probe_sha, str) or re.fullmatch(r"[0-9a-f]{64}", probe_sha) is None:
        raise AuditError(f"{key} run has no valid probe SHA-256")
    if (record.get("status") != "completed" or record.get("campaign_sha256") != campaign_sha or
            record.get("planned_trials") != len(expected_ids) or record.get("executed_trials") != len(expected_ids) or
            record.get("execution_failures") != 0 or frozen.get("campaign_sha256") != campaign_sha or
            frozen.get("campaign") != campaign or frozen.get("probe_sha256") != probe_sha):
        raise AuditError(f"{key} run does not match the completed frozen campaign and executable")
    trials = record.get("trials")
    if not isinstance(trials, list) or len(trials) != len(expected_ids):
        raise AuditError(f"{key} run must contain exactly its {len(expected_ids)} frozen completed trials")

    seen: dict[str, Any] = {}
    for trial in trials:
        if not isinstance(trial, dict):
            raise AuditError(f"{key} run contains a malformed trial record")
        episode = trial.get("episode")
        if not isinstance(episode, dict) or episode.get("id") not in episodes:
            raise AuditError(f"{key} run contains an unexpected trial")
        episode_id = episode["id"]
        if episode_id in seen or episode != episodes[episode_id]:
            raise AuditError(f"{key} trial differs from the exact frozen campaign episode")
        elapsed = trial.get("elapsed_seconds")
        if (trial.get("status") != "completed" or trial.get("return_code") != 0 or
                trial.get("timed_out") is not False or not isinstance(elapsed, (int, float)) or
                isinstance(elapsed, bool) or not math.isfinite(elapsed) or elapsed < 0 or elapsed > 900):
            raise AuditError(f"{key} trial did not finish within its declared bounded execution")
        command = trial.get("command")
        if not isinstance(command, list) or not command or command[0] != frozen.get("probe"):
            raise AuditError(f"{key} trial command does not identify the frozen executable")
        for option in ("--max-samples", "--max-wall-seconds"):
            if command.count(option) != 1:
                raise AuditError(f"{key} trial command must bind {option} exactly once")
        try:
            sample_cap = int(command[command.index("--max-samples") + 1])
            wall_cap = float(command[command.index("--max-wall-seconds") + 1])
        except (ValueError, IndexError, TypeError) as exc:
            raise AuditError(f"{key} trial has malformed resource caps") from exc
        episode_cap = episode.get("max_samples", campaign["max_samples"])
        if (sample_cap != episode_cap or sample_cap > campaign["max_samples"] or
                not math.isfinite(wall_cap) or not 0 < wall_cap <= 900):
            raise AuditError(f"{key} trial resource caps differ from the frozen campaign")
        raw_name = trial.get("raw_stdout")
        if not isinstance(raw_name, str) or Path(raw_name).is_absolute():
            raise AuditError(f"{key} receipt path must be relative to its run directory")
        raw_path = _inside(run_dir, run_dir / raw_name, "native receipt")
        raw_sha = _sha(raw_path)
        receipt = run._parse_receipt(raw_path)
        if receipt.get("status") != "completed":
            raise AuditError(f"{episode_id} native receipt is not completed")
        try:
            run._validate_completed_receipt(receipt, episode)
        except Exception as exc:
            raise AuditError(f"{episode_id} strict native receipt validation failed: {exc}") from exc
        source = receipt.get("source_identity")
        if not isinstance(source, dict):
            raise AuditError(f"{episode_id} receipt has no source identity")
        for field in ("native_source_sha256", "appraisal_source_sha256"):
            if not isinstance(source.get(field), str) or re.fullmatch(r"[0-9a-fA-F]{64}", source[field]) is None:
                raise AuditError(f"{episode_id} source identity has invalid {field}")
        plan = receipt.get("plan", {})
        outcomes = receipt.get("outcomes", {})
        if (plan.get("max_samples") != sample_cap or
                outcomes.get("accepted_samples") != plan.get("horizon_samples") or
                outcomes.get("accepted_samples", 0) > sample_cap):
            raise AuditError(f"{episode_id} receipt does not prove full bounded input completion")
        protected = receipt.get("hf_protected_reassembly")
        replay = receipt.get("hf_protected_admission_replay")
        comparison = receipt.get("hf_predictive_quiet_comparison")
        candidate = comparison.get("candidate") if isinstance(comparison, dict) else None
        candidate_protected = candidate.get("hf_protected_reassembly") if isinstance(candidate, dict) else None
        candidate_replay = candidate.get("hf_protected_admission_replay") if isinstance(candidate, dict) else None
        if (not isinstance(protected, dict) or not isinstance(replay, dict) or
                not isinstance(candidate, dict) or candidate.get("status") != "completed" or
                not isinstance(candidate_protected, dict) or not isinstance(candidate_replay, dict)):
            raise AuditError(f"{episode_id} lacks protected-reassembly observations")
        tiers = replay.get("tiers")
        candidate_tiers = candidate_replay.get("tiers")
        if (not isinstance(tiers, list) or tuple(tier.get("policy") for tier in tiers) != POLICIES or
                not isinstance(candidate_tiers, list) or
                tuple(tier.get("policy") for tier in candidate_tiers) != POLICIES):
            raise AuditError(f"{episode_id} baseline and candidate must each retain all six fixed native admission tiers")
        seen[episode_id] = {
            "episode": episode,
            "receipt": receipt,
            "receipt_sha256": raw_sha,
            "source_identity": source,
            "trial_elapsed_seconds": elapsed,
            "trial_status": trial["status"],
            "probe_sha256": probe_sha,
            "protected": protected,
            "replay": replay,
            "candidate": candidate,
            "candidate_protected": candidate_protected,
            "candidate_replay": candidate_replay,
            "outcomes": outcomes,
        }
    if tuple(seen) != expected_ids:
        raise AuditError(f"{key} run is missing an expected frozen episode")
    return {
        "campaign": campaign,
        "campaign_file_sha256": campaign_file_sha,
        "campaign_sha256": campaign_sha,
        "run_directory": str(run_dir.relative_to(APP)),
        "run_record_sha256": _sha(run_path),
        "frozen_input_sha256": _sha(frozen_path),
        "probe_sha256": probe_sha,
        "cases": seen,
    }


def _nonnegative_count(value: Any, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise AuditError(f"native {label} must be a nonnegative integer")
    return value


def _case_report(case: dict[str, Any]) -> dict[str, Any]:
    episode = case["episode"]
    wanted = episode.get("control", "wanted") == "wanted"
    outcomes = case["outcomes"]
    baseline_report = case["protected"]
    replay = case["replay"]
    candidate = case["candidate"]
    candidate_protected = case["candidate_protected"]
    candidate_replay = case["candidate_replay"]
    baseline_tier = replay["tiers"][0]
    baseline = {
        "direct_native_complete": _nonnegative_count(
            baseline_tier.get("canonical_correct_messages"), "baseline canonical correct messages") == 1,
        "native_correct_messages": _nonnegative_count(
            baseline_tier.get("canonical_correct_messages"), "baseline canonical correct messages"),
        "native_incorrect_messages": _nonnegative_count(
            baseline_tier.get("canonical_incorrect_messages"), "baseline canonical incorrect messages"),
        "consumer_correct_messages": _nonnegative_count(
            baseline_report.get("correct_complete_messages"), "baseline consumer correct messages"),
        "consumer_incorrect_messages": _nonnegative_count(
            baseline_report.get("incorrect_messages"), "baseline consumer incorrect messages"),
        "consumer_decode_attempts": _nonnegative_count(
            baseline_report.get("decode_attempts"), "baseline consumer decode attempts"),
        "accepted_messages": _nonnegative_count(
            outcomes.get("accepted_messages"), "baseline accepted messages"),
        "correct_decoded_complete": outcomes.get("correct_decoded_complete") is True,
        "accepted_incorrect_messages": sum(
            1 for message in case["receipt"].get("messages", [])
            if isinstance(message, dict) and message.get("decoded") is True and
            (not wanted or message.get("matches_transmitted_message") is False)),
        "false_data": _nonnegative_count(outcomes.get("false_data"), "baseline false DATA"),
        "premature_physical_completions": _nonnegative_count(
            outcomes.get("premature_completions"), "baseline premature physical completions"),
    }
    tiers = []
    for tier in candidate_replay["tiers"]:
        native_correct = _nonnegative_count(tier.get("canonical_correct_messages"), "native correct messages")
        native_incorrect = _nonnegative_count(tier.get("canonical_incorrect_messages"), "native incorrect messages")
        consumer_correct = _nonnegative_count(tier.get("consumer_correct_messages"), "consumer correct messages")
        consumer_incorrect = _nonnegative_count(tier.get("consumer_incorrect_messages"), "consumer incorrect messages")
        attempts = _nonnegative_count(tier.get("consumer_decode_attempts"), "consumer decode attempts")
        false_data = _nonnegative_count(tier.get("false_data"), "false DATA")
        premature = _nonnegative_count(
            tier.get("canonical_premature_completions"), "premature physical completions")
        delivery_ok = not wanted or native_correct + consumer_correct == 1
        # A negative control has no intended content: even a contradictory
        # "correct" label cannot excuse accepting a native or recovered message.
        incorrect_ok = (native_incorrect + consumer_incorrect == 0 if wanted else
                        native_correct + consumer_correct + native_incorrect + consumer_incorrect == 0)
        false_data_ok = wanted or false_data == 0
        premature_ok = premature == 0
        tiers.append({
            "policy": tier["policy"],
            "direct_native_complete": native_correct == 1,
            "native_correct_messages": native_correct,
            "native_incorrect_messages": native_incorrect,
            "consumer_correct_messages": consumer_correct,
            "consumer_incorrect_messages": consumer_incorrect,
            "consumer_decode_attempts": attempts,
            "false_data": false_data,
            "premature_physical_completions": premature,
            "gates": {
                "exact_wanted_combined_delivery_one_path": delivery_ok,
                "no_native_or_consumer_incorrect_content": incorrect_ok,
                "negative_control_false_data_zero": false_data_ok,
                "no_premature_physical_completion": premature_ok,
                "numerical_and_bounded_complete_validation": True,
            },
        })
    return {
        "id": episode["id"],
        "control": episode.get("control", "wanted"),
        "transmitted_message": episode.get("message"),
        "receipt_sha256": case["receipt_sha256"],
        "source_identity": case["source_identity"],
        "trial_elapsed_seconds": case["trial_elapsed_seconds"],
        "strict_native_receipt_validation": "passed_for_baseline_and_embedded_candidate",
        "baseline": baseline,
        "candidate": {
            "status": candidate.get("status"),
            "direct_native_correct_messages": _nonnegative_count(
                candidate_replay["tiers"][0].get("canonical_correct_messages"),
                "candidate canonical correct messages"),
            "consumer_correct_messages": _nonnegative_count(
                candidate_protected.get("correct_complete_messages"),
                "candidate consumer correct messages"),
            "consumer_incorrect_messages": _nonnegative_count(
                candidate_protected.get("incorrect_messages"), "candidate consumer incorrect messages"),
            "consumer_decode_attempts": _nonnegative_count(
                candidate_protected.get("decode_attempts"), "candidate consumer decode attempts"),
            "false_data": _nonnegative_count(
                candidate.get("outcomes", {}).get("false_data"), "candidate baseline-policy false DATA"),
            "premature_physical_completions": _nonnegative_count(
                candidate.get("outcomes", {}).get("premature_completions"),
                "candidate baseline-policy premature completions"),
            "complete_frames": [
                {key: frame.get(key) for key in ("first_public_slot", "stop_public_slot", "bit_positions",
                                                 "decoded_payload_bytes", "matches_transmitted_complete_message")}
                for frame in candidate_protected.get("candidates", [])
            ],
        },
        "tiers": tiers,
    }


def build_audit(current_run: Path, preservation_run: Path, *, longer: bool = False) -> dict[str, Any]:
    loaded = {
        "discriminator": _load_one("discriminator", current_run, longer=longer),
        "preservation": _load_one("preservation", preservation_run, longer=longer),
    }
    all_cases = [case for source in loaded.values() for case in source["cases"].values()]
    source_identity = all_cases[0]["source_identity"]
    if any(case["source_identity"] != source_identity for case in all_cases[1:]):
        raise AuditError("four receipts do not share one exact native/appraisal source identity")
    probe_hashes = {source["probe_sha256"] for source in loaded.values()}
    if len(probe_hashes) != 1:
        raise AuditError("the two completed runs do not use the exact same executable hash")
    reports = []
    for key, source in loaded.items():
        for episode_id in (LONGER_EXPECTED_IDS if longer else EXPECTED_IDS)[key]:
            reports.append(_case_report(source["cases"][episode_id]))
    all_tiers = [tier for case in reports for tier in case["tiers"]]
    failed = [
        {"case": case["id"], "policy": tier["policy"], "gate": gate}
        for case in reports for tier in case["tiers"]
        for gate, passed in tier["gates"].items() if not passed
    ]
    policy_assessments = [{
        "policy": policy,
        "all_four_case_gates_pass": not any(item["policy"] == policy for item in failed),
        "failed_gates": [item for item in failed if item["policy"] == policy],
    } for policy in POLICIES]
    return {
        "schema_version": 1,
        "kind": "retrospective_finite_repair_regression_acceptance",
        "requested_bit_seconds": 16 if longer else 8,
        "status": "passed_for_four_frozen_cases" if not failed else "failed_for_frozen_cases",
        "scope": "four completed frozen receipts; six dependent native admission replays per receipt",
        "operational_qualification": False,
        "threshold_selection": False,
        "predeclared_primary_policy": "strict",
        "primary_policy_all_four_case_gates_pass": policy_assessments[2]["all_four_case_gates_pass"],
        "fixed_policy_assessments": policy_assessments,
        "original_rendered_capture_count": 4,
        "capture_scope": "four original rendered IQ captures, but they share physical trajectories within each frozen campaign; six candidate tiers per capture are dependent replay observations",
        "probe_sha256": next(iter(probe_hashes)),
        "source_identity": source_identity,
        "provenance": {
            key: {
                "campaign_file_sha256": source["campaign_file_sha256"],
                "campaign_sha256": source["campaign_sha256"],
                "run_directory": source["run_directory"],
                "run_record_sha256": source["run_record_sha256"],
                "frozen_input_sha256": source["frozen_input_sha256"],
                "probe_sha256": source["probe_sha256"],
            } for key, source in loaded.items()
        },
        "cases": reports,
        "failed_gates": failed,
        "all_fixed_tier_gates_pass": not failed and len(all_tiers) == 24,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--longer", action="store_true", help="audit the frozen sixteen-second follow-up and preservation cases")
    parser.add_argument("--current-run-dir", type=Path)
    parser.add_argument("--preservation-run-dir", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    run_names = LONGER_RUN_NAMES if args.longer else RUN_NAMES
    output = (args.output or EVIDENCE / (
        "hf-protected-oblique-duration-audit.json" if args.longer else
        "hf-protected-oblique-repair-audit.json")).resolve()
    try:
        output.relative_to(EVIDENCE.resolve(strict=True))
    except (OSError, ValueError):
        parser.error("output must remain in this appraisal's local-evidence directory")
    if output.exists():
        parser.error("refusing to overwrite retained audit evidence")
    try:
        result = build_audit(args.current_run_dir or EVIDENCE / run_names["discriminator"],
                             args.preservation_run_dir or EVIDENCE / run_names["preservation"],
                             longer=args.longer)
    except (AuditError, run.CampaignError) as exc:
        parser.error(str(exc))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(result["status"], "cases=" + str(len(result["cases"])),
          "failed_gates=" + str(len(result["failed_gates"])), "output=" + str(output))
    return 0 if result["all_fixed_tier_gates_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

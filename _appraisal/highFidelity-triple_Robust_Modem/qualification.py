"""Recompute a finite, hash-bound appraisal qualification without running DSP.

Campaign execution status is provenance only. Every declared cell is checked
against its exact campaign, receipt, executable, and source identity. The seven
required source/build fields are authoritative; when a receipt also provides
``source_commit_at_configure``, that optional field is retained and compared.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

import run

APP = Path(__file__).resolve().parent
IDENTITY_FIELDS = (
    "native_source_sha256", "appraisal_source_sha256", "build_options_sha256",
    "build_config", "compiler", "canonical_pattern_revision", "openssl_runtime",
)
OPTIONAL_IDENTITY_FIELDS = ("source_commit_at_configure",)
METADATA_POINTER = "/channel/native_chain/propagation_band_declared_not_analytically_proved"


class QualificationError(ValueError):
    """The qualification input is malformed or makes an unsupported claim."""


class EvidenceInvalid(QualificationError):
    """Available evidence contradicts the exact frozen qualification binding."""


def _sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _inside_app(path: Path, label: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise QualificationError(f"{label} is unavailable: {exc}") from exc
    if not resolved.is_relative_to(APP):
        raise QualificationError(f"{label} must remain inside {APP}")
    return resolved


def _load_json(path: Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise QualificationError(f"cannot read {label}: {exc}") from exc


def _identity(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or any(field not in value for field in IDENTITY_FIELDS):
        raise QualificationError(f"{label} must contain the complete source identity tuple")
    for field in IDENTITY_FIELDS[:3]:
        if not isinstance(value[field], str) or re.fullmatch(r"[0-9a-f]{64}", value[field]) is None:
            raise QualificationError(f"{label}.{field} must be a lowercase SHA-256")
    if any(not isinstance(value[field], str) or not value[field] for field in IDENTITY_FIELDS[3:]):
        raise QualificationError(f"{label} has an empty build identity field")
    result = {field: value[field] for field in IDENTITY_FIELDS}
    for field in OPTIONAL_IDENTITY_FIELDS:
        if field in value:
            if not isinstance(value[field], str) or not value[field]:
                raise QualificationError(f"{label}.{field} must be a nonempty string when present")
            result[field] = value[field]
    return result


def _relative(base: Path, item: Any, label: str) -> Path:
    if not isinstance(item, str) or not item:
        raise QualificationError(f"{label} must be a nonempty path")
    path = Path(item)
    return path if path.is_absolute() else base / path


def _metadata_amended_receipt(raw: dict[str, Any], cell: dict[str, Any], run_dir: Path) -> dict[str, Any]:
    amendment = cell.get("metadata_amendment")
    if amendment is None:
        try:
            run._validate_completed_receipt(raw, cell["episode"])
        except Exception as exc:
            raise EvidenceInvalid(f"strict validation of the original native receipt failed: {exc}") from exc
        return raw
    if not isinstance(amendment, dict) or set(amendment) != {"path", "sha256"}:
        raise QualificationError("metadata_amendment requires only path and sha256")
    audit_path = _inside_app(_relative(APP, amendment["path"], "metadata amendment audit"),
                             "metadata amendment audit")
    if _sha(audit_path) != amendment["sha256"]:
        raise EvidenceInvalid("metadata amendment audit hash does not match the frozen specification")
    audit = _load_json(audit_path, "metadata amendment audit")
    original = run_dir / Path(cell["raw_stdout"])
    audit_original = audit.get("original_receipt")
    if (not isinstance(audit_original, str) or
            Path(audit_original).resolve(strict=True) != original.resolve(strict=True)):
        raise EvidenceInvalid("metadata audit does not name the exact frozen native receipt path")
    if (audit.get("kind") != "explicit_native_receipt_metadata_amendment_audit" or
            audit.get("original_trial_status") != "invalid_receipt" or
            audit.get("original_campaign_status") != "incomplete_checks" or
            audit.get("original_raw_unchanged") is not True or
            audit.get("outcomes_unchanged") is not True or
            _sha(original) != audit.get("original_receipt_sha256")):
        raise EvidenceInvalid("metadata audit does not bind the unchanged originally invalid receipt")
    expected_original_identity = _identity(cell["source_identity"], "cell.source_identity")
    if _identity(audit.get("original_source_identity"), "audit.original_source_identity") != expected_original_identity:
        raise EvidenceInvalid("metadata audit source identity differs from the original receipt")
    changes = audit.get("amendments")
    if (not isinstance(changes, list) or len(changes) != 1 or changes[0] != {
            "json_pointer": METADATA_POINTER, "old": False, "new": True}):
        raise EvidenceInvalid("only the declared vector band-report flag amendment is admissible")
    amended = copy.deepcopy(raw)
    chain = amended.get("channel", {}).get("native_chain")
    if not isinstance(chain, dict) or chain.get("propagation_band_declared_not_analytically_proved") is not False:
        raise EvidenceInvalid("original receipt does not contain the audited false metadata value")
    chain["propagation_band_declared_not_analytically_proved"] = True
    if amended.get("source_identity") != raw.get("source_identity") or amended.get("outcomes") != raw.get("outcomes"):
        raise EvidenceInvalid("metadata amendment must preserve source identity and outcomes")
    try:
        run._validate_completed_receipt(amended, cell["episode"])
    except Exception as exc:
        raise EvidenceInvalid(f"strict validation of the audited amended receipt failed: {exc}") from exc
    return amended


def _dimension(status: str, reason: str) -> dict[str, str]:
    return {"status": status, "reason": reason}


def _observe_target(domain: dict[str, Any], base: Path) -> dict[str, Any]:
    """Observe the named binary now, without executing it or relabeling old evidence."""
    result = {"path": domain["target_probe_path"], "expected_sha256": domain["target_probe_sha256"]}
    try:
        path = _inside_app(_relative(base, domain["target_probe_path"], "target probe"), "target probe")
        actual = _sha(path)
        matches = actual == domain["target_probe_sha256"]
        result.update(observed_sha256=actual, **_dimension(
            "passed" if matches else "missing",
            "current target binary matches the declared hash" if matches else
            "target binary changed; declared source/binary applicability is stale"))
    except (OSError, QualificationError) as exc:
        result.update(observed_sha256=None, **_dimension("missing", str(exc)))
    return result


def _receive_work(receipt: dict[str, Any], cell: dict[str, Any]) -> tuple[str, str]:
    """Check completed full input/scoring; this is not a processor-capacity test."""
    plan, outcomes = receipt.get("plan"), receipt.get("outcomes")
    slots, scores, receiver = (plan.get("public_slots") if isinstance(plan, dict) else None,
                               receipt.get("slots"), receipt.get("receiver"))
    if not isinstance(plan, dict) or not isinstance(outcomes, dict) or not isinstance(slots, list):
        return "missing", "plan, public slots or outcomes are absent"
    horizon = plan.get("horizon_samples")
    if (not run._is_nonnegative_int(horizon) or horizon == 0 or
            outcomes.get("accepted_samples") != horizon or horizon > cell["max_samples"]):
        return "failed", "receiver did not accept the complete declared sample horizon or it exceeds the cell cap"
    count = plan.get("scored_slot_horizon")
    if (not run._is_nonnegative_int(count) or count == 0 or len(slots) != count or
            not isinstance(scores, list) or len(scores) != count or
            outcomes.get("actual_scored_slots") != count):
        return "failed", "not every planned public slot has a score"
    for index, (public, score) in enumerate(zip(slots, scores)):
        if (not isinstance(public, dict) or not isinstance(score, dict) or
                not run._is_nonnegative_int(public.get("index")) or public.get("index") != index or
                not run._is_nonnegative_int(score.get("index")) or score.get("index") != index or
                not run._is_nonnegative_int(public.get("symbol_ordinal")) or
                not run._is_nonnegative_int(score.get("symbol_ordinal")) or
                score.get("symbol_ordinal") != public["symbol_ordinal"] or
                score.get("pattern_epoch") != public.get("pattern_epoch")):
            return "failed", f"slot {index} lacks ordered matching public epoch/ordinal evidence"
        if score.get("complete") is not True or score.get("all_valid") is not True:
            return "failed", f"slot {index} is incomplete or contains invalid input"
    if not isinstance(receiver, dict):
        return "missing", "receiver completion state is absent"
    if (receiver.get("input_closed") is not True or receiver.get("input_stop") != horizon or
            receiver.get("next_public_slot") != count or receiver.get("state") not in {"input_ended", "closed"}):
        return "failed", "receiver has not finished the full declared input and public calendar"
    limit, reserved = receiver.get("memory_limit_bytes"), receiver.get("peak_reserved_bytes")
    if limit is None or reserved is None:
        return "missing", "complete-receiver workspace reservation or limit is absent"
    if (not run._is_nonnegative_int(limit) or limit == 0 or
            not run._is_nonnegative_int(reserved) or reserved > limit):
        return "failed", "complete-receiver workspace reservation exceeds its declared limit"
    if cell.get("require_full_automatic_workers", False):
        hardware, workers = receiver.get("hardware_threads"), receiver.get("workers")
        resources = plan.get("receiver_resources")
        if (not run._is_nonnegative_int(hardware) or hardware == 0 or workers != 2 * hardware or
                not isinstance(resources, dict) or resources.get("workers_explicit") is not False):
            return "failed", "full automatic W=2H software-worker budget was not demonstrated"
    return "passed", "all input samples and planned ordered valid receiver slots completed"


def _numerical_gate(receipt: dict[str, Any], gate: dict[str, Any]) -> tuple[str, str]:
    path = gate.get("physical_report_path")
    if not isinstance(path, str) or not path:
        return "missing", "numerical gate has no explicit physical report path"
    value: Any = receipt
    for key in path.split("."):
        if not isinstance(value, dict) or key not in value:
            return "missing", f"physical numerical report {path!r} is absent"
        value = value[key]
    if not isinstance(value, dict):
        return "missing", f"physical numerical report {path!r} is malformed"
    qualified_field = gate.get("qualified_field", "table_qualified")
    if value.get(qualified_field) is not True:
        return "failed", f"physical report {qualified_field!r} is not true"
    if gate.get("require_table_hash", True):
        digest = value.get(gate.get("table_hash_field", "table_sha256"))
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            return "failed", "qualified physical table has no valid coefficient hash"
    plan = receipt.get("plan", {})
    for flag in gate.get("required_plan_true", []):
        if plan.get(flag) is not True:
            return "failed", f"plan execution flag {flag!r} is not true"
    return "passed", "scenario validator and explicit numerical table witnesses pass"


def _cell_gates(receipt: dict[str, Any], cell: dict[str, Any], elapsed: float) -> dict[str, dict[str, str]]:
    settings = cell["gates"]
    outcomes = receipt.get("outcomes", {})
    role = cell["control_role"]
    dimensions: dict[str, dict[str, str]] = {}
    evidence_status = cell["_evidence_status"]
    dimensions["receipt_validation"] = _dimension(evidence_status, cell["_evidence_reason"])
    if evidence_status != "passed":
        for name in ("numerics", "delivery", "integrity", "false_data", "bounded_execution"):
            dimensions[name] = _dimension("missing", "receipt is not validated evidence")
        return dimensions
    if settings.get("numerics"):
        status, reason = _numerical_gate(receipt, settings)
        dimensions["numerics"] = _dimension(status, reason)
    else:
        dimensions["numerics"] = _dimension("not_applicable", "the finite domain declares no physical numerical gate")
    if role == "wanted":
        endpoint = run._communication_outcome(receipt, cell["episode"])
        dimensions["delivery"] = _dimension("passed" if endpoint is True else "failed",
                                             "observed complete communication endpoint is true" if endpoint is True else
                                             "observed complete communication endpoint is not true")
        wanted = run._wanted_outcome_counts({"status": "completed", "episode": cell["episode"], "receipt": receipt})
        accepted = outcomes.get("accepted_messages")
        incorrect = wanted.get("accepted_incorrect_messages") if wanted else None
        if not run._is_nonnegative_int(accepted) or incorrect is None:
            dimensions["integrity"] = _dimension("missing", "accepted content cannot be determined from decoded messages")
        else:
            dimensions["integrity"] = _dimension("failed" if incorrect else "passed",
                                                 f"incorrectly accepted wanted messages: {incorrect}")
        dimensions["false_data"] = _dimension("not_applicable", "false-DATA ceiling applies to declared negative controls")
    else:
        false_data, accepted = run._negative_control_counts(receipt)
        dimensions["delivery"] = _dimension("not_applicable", "negative control has no wanted delivery target")
        if not run._is_nonnegative_int(accepted):
            dimensions["integrity"] = _dimension("missing", "negative-control accepted-message count is absent")
        else:
            dimensions["integrity"] = _dimension("failed" if accepted else "passed",
                                                 f"accepted negative-control messages: {accepted}")
        ceiling = settings.get("false_data_max", 0)
        if not run._is_nonnegative_int(false_data):
            dimensions["false_data"] = _dimension("missing", "negative-control false-DATA count is absent")
        else:
            dimensions["false_data"] = _dimension("failed" if false_data > ceiling else "passed",
                                                  f"false-DATA decisions {false_data}; declared maximum {ceiling}")
    work_status, work_reason = _receive_work(receipt, cell)
    if elapsed > cell["max_wall_seconds"]:
        work_status, work_reason = "failed", "observed native trial time exceeds its declared wall cap"
    else:
        work_reason += f"; native trial wall {elapsed:.6g}s within {cell['max_wall_seconds']}s cap"
    dimensions["bounded_execution"] = _dimension(work_status, work_reason)
    return dimensions


def _validate_spec(spec: Any) -> None:
    if not isinstance(spec, dict) or spec.get("schema_version") != 1:
        raise QualificationError("qualification spec schema_version must be 1")
    domain = spec.get("qualification_domain")
    if not isinstance(domain, dict) or not all(isinstance(domain.get(key), str) and domain[key]
                                               for key in ("id", "scope")):
        raise QualificationError("qualification_domain requires an id and finite scope statement")
    if not isinstance(domain.get("excluded_strata"), list) or any(not isinstance(x, str) or not x for x in domain["excluded_strata"]):
        raise QualificationError("excluded_strata must explicitly describe unqualified conditions")
    if (not isinstance(domain.get("required_missing_strata", []), list) or
            any(not isinstance(x, str) or not x for x in domain.get("required_missing_strata", []))):
        raise QualificationError("required_missing_strata must be an array of named unqualified strata")
    _identity(domain.get("target_source_identity"), "qualification_domain.target_source_identity")
    if not isinstance(domain.get("target_probe_path"), str) or not domain["target_probe_path"]:
        raise QualificationError("qualification_domain.target_probe_path must name the observed binary")
    target_probe = domain.get("target_probe_sha256")
    if not isinstance(target_probe, str) or re.fullmatch(r"[0-9a-f]{64}", target_probe) is None:
        raise QualificationError("qualification_domain.target_probe_sha256 must be a lowercase SHA-256")
    cells = spec.get("cells")
    if not isinstance(cells, list) or not cells:
        raise QualificationError("cells must be a nonempty finite list")
    seen_ids: set[str] = set()
    seen_raw: set[str] = set()
    seen_episode: set[tuple[str, str]] = set()
    for cell in cells:
        if not isinstance(cell, dict) or not isinstance(cell.get("id"), str) or not cell["id"]:
            raise QualificationError("each cell requires a stable id")
        if cell["id"] in seen_ids:
            raise QualificationError(f"duplicate cell id: {cell['id']}")
        seen_ids.add(cell["id"])
        for field in ("campaign_file_sha256", "campaign_sha256", "receipt_sha256", "probe_sha256"):
            if not isinstance(cell.get(field), str) or re.fullmatch(r"[0-9a-f]{64}", cell[field]) is None:
                raise QualificationError(f"cell {cell['id']} requires an explicit {field}")
        if cell["receipt_sha256"] in seen_raw:
            raise QualificationError("one raw receipt cannot stand in for multiple independent qualification cells")
        seen_raw.add(cell["receipt_sha256"])
        for field in ("campaign_path", "run_directory", "episode_id"):
            if not isinstance(cell.get(field), str) or not cell[field]:
                raise QualificationError(f"cell {cell['id']} requires {field}")
        key = (cell["campaign_file_sha256"], cell["episode_id"])
        if key in seen_episode:
            raise QualificationError("the same frozen campaign episode cannot be counted twice")
        seen_episode.add(key)
        episode = cell.get("episode")
        if (not isinstance(episode, dict) or episode.get("id") != cell["episode_id"] or
                episode.get("mode") != "receive" or episode.get("control", "wanted") not in {"wanted", "noise", "wrong-key", "wrong-ordinal"}):
            raise QualificationError(f"cell {cell['id']} must bind one native receive episode")
        role = cell.get("control_role")
        expected = episode.get("control", "wanted")
        if (role not in {"wanted", "negative"} or
                (role == "wanted") != (expected == "wanted") or
                (role == "negative" and expected not in run.NEGATIVE_RECEIVE_CONTROLS)):
            raise QualificationError(f"cell {cell['id']} role does not match its frozen control")
        gates = cell.get("gates")
        if (not isinstance(gates, dict) or any(not isinstance(gates.get(key), bool)
                                               for key in ("numerics", "delivery", "integrity", "bounded_execution")) or
                not isinstance(gates.get("false_data_max", 0), int) or isinstance(gates.get("false_data_max", 0), bool) or
                gates.get("false_data_max", 0) < 0):
            raise QualificationError(f"cell {cell['id']} has invalid or missing gates")
        if role == "wanted" and not gates["integrity"]:
            raise QualificationError("wanted cells must require accepted-content integrity")
        if role == "wanted" and gates.get("delivery_requirement_basis") != (
                "declared_operational_anchor" if gates["delivery"] else "characterization"):
            raise QualificationError("wanted delivery needs an explicit anchor or characterization basis")
        if role == "negative" and (gates["delivery"] or not gates["integrity"]):
            raise QualificationError("negative cells require integrity and cannot require wanted delivery")
        if not gates["bounded_execution"]:
            raise QualificationError("every qualification cell must require bounded complete-receiver execution")
        if role == "negative" and (not gates["integrity"] or "false_data_max" not in gates):
            raise QualificationError("negative cells must declare accepted-message and false-DATA gates")
        for field in ("max_wall_seconds", "max_samples"):
            if (not isinstance(cell.get(field), (int, float)) or isinstance(cell[field], bool) or
                    not math.isfinite(cell[field]) or cell[field] <= 0):
                raise QualificationError(f"cell {cell['id']} requires a positive {field}")
        _identity(cell.get("source_identity"), f"cell {cell['id']}.source_identity")


def _load_cell(cell: dict[str, Any], base: Path, target_identity: dict[str, Any],
               target_probe: str) -> tuple[dict[str, Any], float, dict[str, str]]:
    result: dict[str, Any] = {"id": cell["id"], "control_role": cell["control_role"]}
    missing = {name: _dimension("missing", "validated completed evidence is not available")
               for name in ("receipt_validation", "numerics", "delivery", "integrity", "false_data", "bounded_execution")}
    result["gates"] = missing
    try:
        campaign_path = _inside_app(_relative(base, cell["campaign_path"], "campaign"), "campaign")
        run_dir = _inside_app(_relative(base, cell["run_directory"], "run directory"), "run directory")
        if _sha(campaign_path) != cell["campaign_file_sha256"]:
            raise EvidenceInvalid("frozen campaign file hash mismatch")
        campaign = run._read_campaign(campaign_path)
        campaign_sha = hashlib.sha256(run._json_bytes(campaign)).hexdigest()
        if campaign_sha != cell["campaign_sha256"]:
            raise EvidenceInvalid("canonical frozen campaign hash mismatch")
        matches = [episode for episode in campaign["episodes"] if episode["id"] == cell["episode_id"]]
        if len(matches) != 1 or matches[0] != cell["episode"]:
            raise EvidenceInvalid("cell episode is absent or differs from the exact frozen campaign episode")
        frozen = _load_json(run_dir / "frozen-input.json", "frozen campaign input")
        run_record = _load_json(run_dir / "run.json", "campaign run")
        if (not isinstance(frozen, dict) or frozen.get("campaign_sha256") != campaign_sha or
                frozen.get("campaign") != campaign or run_record.get("campaign_sha256") != campaign_sha or
                run_record.get("probe_sha256") != cell["probe_sha256"] or
                frozen.get("probe_sha256") != cell["probe_sha256"]):
            raise EvidenceInvalid("run provenance does not match the frozen campaign or executable hash")
        result["parent_campaign_status"] = run_record.get("status", "unavailable")
        result["parent_campaign_stop_reason"] = run_record.get("stop_reason")
        if not isinstance(run_record.get("trials"), list):
            raise QualificationError("run trial records are missing")
        trials = [trial for trial in run_record["trials"] if trial.get("episode", {}).get("id") == cell["episode_id"]]
        if len(trials) != 1:
            raise EvidenceInvalid("run does not contain exactly one record for this episode")
        trial = trials[0]
        result["original_trial_status"] = trial.get("status")
        result["original_trial_return_code"] = trial.get("return_code")
        result["original_trial_timed_out"] = trial.get("timed_out")
        result["original_trial_error"] = trial.get("error")
        if trial.get("episode") != cell["episode"]:
            raise EvidenceInvalid("run trial episode differs from the exact frozen campaign episode")
        allowed_status = trial.get("status") == "completed" or (
            trial.get("status") == "invalid_receipt" and cell.get("metadata_amendment") is not None)
        elapsed = trial.get("elapsed_seconds")
        if (not allowed_status or trial.get("return_code") != 0 or trial.get("timed_out") is not False or
                not isinstance(elapsed, (int, float)) or isinstance(elapsed, bool) or
                not math.isfinite(elapsed) or elapsed < 0):
            raise QualificationError("native receive did not finish as one successful, uncensored execution")
        raw_path = trial.get("raw_stdout")
        if not isinstance(raw_path, str) or Path(raw_path).is_absolute():
            raise QualificationError("native stdout path must be relative to the frozen run directory")
        raw_file = _inside_app(run_dir / raw_path, "native receipt")
        if _sha(raw_file) != cell["receipt_sha256"]:
            raise EvidenceInvalid("native receipt byte hash mismatch")
        receipt = run._parse_receipt(raw_file)
        expected_identity = _identity(cell["source_identity"], f"cell {cell['id']} source identity")
        if _identity(receipt.get("source_identity"), "native receipt source identity") != expected_identity:
            raise EvidenceInvalid("native receipt source identity does not match the explicitly frozen tuple")
        command = trial.get("command")
        if not isinstance(command, list):
            raise QualificationError("frozen native command is unavailable")
        if any(command.count(option) != 1 for option in ("--max-samples", "--max-wall-seconds")):
            raise EvidenceInvalid("frozen command does not contain exactly one declared sample and wall cap")
        try:
            sample_cap = int(command[command.index("--max-samples") + 1])
            wall_cap = float(command[command.index("--max-wall-seconds") + 1])
        except (ValueError, IndexError, TypeError) as exc:
            raise QualificationError("native command omits its sample/time resource caps") from exc
        expected_sample_cap = trial.get("episode", {}).get("max_samples", campaign["max_samples"])
        plan_sample_cap = receipt.get("plan", {}).get("max_samples")
        if (sample_cap != expected_sample_cap or plan_sample_cap != sample_cap or
                sample_cap > cell["max_samples"] or
                not math.isfinite(wall_cap) or wall_cap <= 0 or wall_cap > cell["max_wall_seconds"]):
            raise EvidenceInvalid("executed sample/time limits do not match the frozen cell caps")
        if command[0] != frozen.get("probe"):
            raise EvidenceInvalid("executed command does not name the hash-bound frozen probe")
        result["_evidence_status"], result["_evidence_reason"] = ("passed", "raw receipt, campaign and frozen executable identity match recorded hashes")
        if receipt.get("status") != "completed":
            raise EvidenceInvalid("native receipt itself is not completed")
        try:
            receipt = _metadata_amended_receipt(receipt, {**cell, "raw_stdout": raw_path}, run_dir)
        except QualificationError:
            raise
        except Exception as exc:
            raise EvidenceInvalid(f"strict native receipt validation failed: {exc}") from exc
        if cell.get("metadata_amendment"):
            result["receipt_evidence_kind"] = "metadata_amended"
            result["original_receipt_status"] = "invalid_receipt"
        else:
            result["receipt_evidence_kind"] = "strict_original"
        applicable = expected_identity == target_identity and cell["probe_sha256"] == target_probe
        result["current_implementation_applicability"] = _dimension(
            "passed" if applicable else "missing",
            "exact appraisal/native/build/binary identity matches qualification target" if applicable else
            "historical evidence; current implementation applicability is unverified")
        result["gates"] = _cell_gates(receipt, {**cell, "_evidence_status": result["_evidence_status"],
                                                      "_evidence_reason": result["_evidence_reason"]},
                                      float(elapsed))
        result["original_trial_status"] = trial.get("status")
        result["trial_elapsed_seconds"] = elapsed
        return result, float(elapsed), expected_identity
    except Exception as exc:
        status = "failed" if isinstance(exc, EvidenceInvalid) else "missing"
        result["_evidence_status"], result["_evidence_reason"] = status, str(exc)
        result["gates"] = {name: _dimension(status, str(exc)) for name in missing}
        result["current_implementation_applicability"] = _dimension("missing", "validated source identity unavailable")
        return result, 0., {}


def evaluate(spec: dict[str, Any], spec_path: Path) -> dict[str, Any]:
    _validate_spec(spec)
    domain = spec["qualification_domain"]
    target_observation = _observe_target(domain, spec_path.parent)
    cells = []
    case_failed = False
    case_missing = False
    for cell in spec["cells"]:
        item = dict(cell, domain_spec_path=str(spec_path.resolve()))
        result, _, _ = _load_cell(item, spec_path.parent, _identity(domain["target_source_identity"], "domain target source identity"),
                                  domain["target_probe_sha256"])
        if target_observation["status"] != "passed":
            result["current_implementation_applicability"] = _dimension("missing", target_observation["reason"])
        result["delivery_is_requirement"] = cell["gates"]["delivery"]
        result["delivery_requirement_basis"] = cell["gates"].get("delivery_requirement_basis", "negative_control")
        gates = result["gates"]
        required = ["receipt_validation", "current_implementation_applicability", "bounded_execution"]
        if cell["gates"].get("numerics"):
            required.append("numerics")
        if cell["control_role"] == "wanted":
            required.append("integrity")
            if cell["gates"]["delivery"]:
                required.append("delivery")
        else:
            required.extend(("integrity", "false_data"))
        required_statuses = [
            (result.get("current_implementation_applicability", {}).get("status", "missing")
             if name == "current_implementation_applicability" else
             gates.get(name, {}).get("status", "missing"))
            for name in required
        ]
        if "failed" in required_statuses:
            case_failed = True
            result["case_status"] = "failed"
        elif any(status == "missing" for status in required_statuses):
            case_missing = True
            result["case_status"] = "incomplete"
        else:
            result["case_status"] = "passed_for_declared_cell"
        result.pop("_evidence_status", None)
        result.pop("_evidence_reason", None)
        cells.append(result)
    missing_strata = domain.get("required_missing_strata", [])
    wanted = [cell for cell in cells if cell["control_role"] == "wanted"]
    delivered = sum(cell["gates"]["receipt_validation"]["status"] == "passed" and
                    cell["gates"]["delivery"]["status"] == "passed" for cell in wanted)
    missed = sum(cell["gates"]["receipt_validation"]["status"] == "passed" and
                 cell["gates"]["delivery"]["status"] == "failed" for cell in wanted)
    anchors = [cell for cell in wanted if cell["delivery_is_requirement"]]
    if not wanted:
        delivery_qualification = "not_applicable"
    elif not anchors:
        delivery_qualification = "incomplete"
    elif any(cell["case_status"] == "failed" for cell in anchors):
        delivery_qualification = "failed"
    elif any(cell["case_status"] == "incomplete" for cell in anchors):
        delivery_qualification = "incomplete"
    else:
        delivery_qualification = "passed_for_declared_anchors"
    if case_failed:
        status = "failed"
    elif case_missing or missing_strata or delivery_qualification == "incomplete":
        status = "incomplete"
    else:
        status = "passed_for_declared_cases"
    return {
        "schema_version": 1,
        "qualification_domain": domain["id"],
        "scope": domain["scope"],
        "target_binary_observation": target_observation,
        "status": status,
        "finite_case_status": "failed" if case_failed else "incomplete" if case_missing else "passed_for_declared_cases",
        "delivery_qualification_status": delivery_qualification,
        "required_delivery_anchors": len(anchors),
        "all_wanted_delivery": {
            "status": "not_applicable" if not wanted else "failed" if missed else "incomplete" if delivered < len(wanted) else "passed_for_observed_cases",
            "wanted_cells": len(wanted), "correct_deliveries": delivered, "failed_delivery_observations": missed,
            "unavailable_observations": len(wanted) - delivered - missed,
            "scope": "Descriptive historical/current communication outcomes, including characterization cells; neither independent trials nor evidence that a miss is receiver-induced or information-limited."},
        "qualified_reliability_probability": None,
        "required_missing_strata": missing_strata,
        "excluded_strata": domain["excluded_strata"],
        "cells": cells,
        "interpretation": "Passing status applies only to the explicitly named finite cells and the observed target binary with matching receipt source/build identity. It does not certify an unbuilt checkout, estimate natural-condition reliability, or establish receiver capacity.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        spec_path = _inside_app(args.spec if args.spec.is_absolute() else Path.cwd() / args.spec,
                                "qualification spec")
        output_path = args.output if args.output.is_absolute() else Path.cwd() / args.output
        if not output_path.resolve().is_relative_to(APP):
            raise QualificationError(f"output must remain inside {APP}")
        spec = _load_json(spec_path, "qualification specification")
        report = evaluate(spec, spec_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"status": report["status"], "finite_case_status": report["finite_case_status"],
                          "cells": len(report["cells"]), "output": str(output_path)}, sort_keys=True))
        return 0 if report["status"] == "passed_for_declared_cases" else 1
    except (QualificationError, OSError, KeyError, TypeError, ValueError) as exc:
        print(f"qualification input error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

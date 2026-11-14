#!/usr/bin/env python3
"""Bounded orchestration and reporting for the native high-fidelity appraisal.

This module contains no waveform, channel, or receiver DSP. Every episode is a
single invocation of the canonical native appraisal executable. Slot episodes
report one first public observation without message completion; diagnose
episodes report bounded VLF acquisition and abbreviated fine components without
bit decisions or complete receiver service. Oracle episodes report a separate
known-waveform bound, not a receiver communication outcome. HF-search episodes
report a shadow detector with public acquisition priors and no receiver decisions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime, timezone
from typing import Any

try:
    import resource
except ImportError:  # pragma: no cover - the native target is primarily tested on POSIX.
    resource = None  # type: ignore[assignment]

HERE = Path(__file__).resolve().parent
SCHEMA_VERSION = 1
MAX_OUTPUT_BYTES = 8 * 1024 * 1024
MAX_CAMPAIGN_OUTPUT_BYTES = 256 * 1024 * 1024
ROOT_KEYS = {"schema_version", "name", "purpose", "max_wall_seconds", "per_trial_wall_seconds",
             "max_trials", "max_samples", "episodes", "confirmatory_strata"}
EPISODE_KEYS = {"id", "scenario", "mode", "waveform", "seed", "cn0_db_hz", "control",
                "helper", "backend", "bit_seconds", "duration_seconds", "pulse", "stratum",
                "oracle_slot", "search_slot", "search_timing", "capture_digest_slot", "chunk", "message", "max_wall_seconds", "max_samples",
                "history_reference_delay_seconds", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "fec", "fade_slots", "capture_lead_seconds"}
MODES = {"plan", "channel", "receive", "slot", "oracle", "diagnose", "hf-search"}
CONTROLS = {"wanted", "noise", "wrong-key", "wrong-ordinal", "interior-fade", "capture-gap", "eof"}
BACKENDS = {"cpu", "cuda", "vulkan", "gpu"}
STATUSES = {"completed", "resource_limit", "time_limit", "unavailable", "error"}


class CampaignError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def _read_campaign(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CampaignError(f"cannot read campaign JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise CampaignError("campaign must be a JSON object")
    extra = set(value) - ROOT_KEYS
    missing = {"schema_version", "name", "max_wall_seconds", "per_trial_wall_seconds", "max_trials",
               "max_samples", "episodes"} - set(value)
    if extra or missing:
        raise CampaignError(f"campaign keys: unexpected={sorted(extra)}, missing={sorted(missing)}")
    if isinstance(value["schema_version"], bool) or value["schema_version"] != SCHEMA_VERSION:
        raise CampaignError(f"campaign schema_version must be {SCHEMA_VERSION}")
    _nonempty_str(value["name"], "name")
    if "purpose" in value and not isinstance(value["purpose"], str):
        raise CampaignError("purpose must be a string")
    for key, minimum, maximum in (("max_wall_seconds", 1, 86400),
                                  ("per_trial_wall_seconds", 1, 3600),
                                  ("max_trials", 1, 100), ("max_samples", 1, 100_000_000)):
        _bounded_number(value[key], key, minimum, maximum, integer=key in {"max_trials", "max_samples"})
    episodes = value["episodes"]
    if not isinstance(episodes, list) or not episodes:
        raise CampaignError("episodes must be a nonempty array")
    if len(episodes) > value["max_trials"]:
        raise CampaignError("episode count exceeds max_trials")
    ids: set[str] = set()
    for index, episode in enumerate(episodes):
        episode_wall_cap = min(value["per_trial_wall_seconds"], value["max_wall_seconds"])
        _validate_episode(episode, index, ids, episode_wall_cap, value["max_samples"])
    confirmatory = value.get("confirmatory_strata", {})
    if not isinstance(confirmatory, dict):
        raise CampaignError("confirmatory_strata must be an object")
    for name, count in confirmatory.items():
        _nonempty_str(name, "confirmatory stratum")
        if isinstance(count, bool) or not isinstance(count, int) or not 1 <= count <= 100:
            raise CampaignError(f"confirmatory stratum {name!r} requires a fixed sample count from 1 to 100")
        matched = [e for e in episodes if e.get("stratum") == name]
        if (len(matched) != count or
                any(e["mode"] != "receive" or e.get("control", "wanted") != "wanted" for e in matched) or
                len({e["seed"] for e in matched}) != count):
            raise CampaignError(f"stratum {name!r} must contain exactly {count} wanted receive episodes with distinct seeds")
        invariant_fields = ("scenario", "mode", "waveform", "cn0_db_hz", "control", "helper",
                            "backend", "bit_seconds", "duration_seconds", "pulse", "oracle_slot", "search_slot", "search_timing", "capture_digest_slot", "chunk",
                            "message", "max_wall_seconds", "max_samples", "history_reference_delay_seconds", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "fec", "fade_slots", "capture_lead_seconds")
        reference = matched[0]
        heterogeneous = [field for field in invariant_fields
                         if any(episode.get(field) != reference.get(field) for episode in matched[1:])]
        if heterogeneous:
            raise CampaignError(f"stratum {name!r} mixes episode settings: {', '.join(heterogeneous)}")
    return value


def _nonempty_str(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CampaignError(f"{name} must be a nonempty string")
    return value


def _bounded_number(value: Any, name: str, low: float, high: float, integer: bool = False) -> float | int:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise CampaignError(f"{name} must be a finite number")
    if value < low or value > high or (integer and not isinstance(value, int)):
        kind = "integer" if integer else "number"
        raise CampaignError(f"{name} must be a {kind} in [{low}, {high}]")
    return value


def _validate_episode(item: Any, index: int, ids: set[str],
                      campaign_per_trial_wall_seconds: float,
                      campaign_max_samples: int) -> None:
    if not isinstance(item, dict):
        raise CampaignError(f"episodes[{index}] must be an object")
    extra = set(item) - EPISODE_KEYS
    required = {"id", "scenario", "mode", "waveform", "seed"} - set(item)
    if extra or required:
        raise CampaignError(f"episodes[{index}] keys: unexpected={sorted(extra)}, missing={sorted(required)}")
    episode_id = _nonempty_str(item["id"], f"episodes[{index}].id")
    if episode_id in ids:
        raise CampaignError(f"duplicate episode id: {episode_id}")
    ids.add(episode_id)
    _nonempty_str(item["scenario"], f"episodes[{index}].scenario")
    if not isinstance(item["mode"], str) or item["mode"] not in MODES:
        raise CampaignError(f"episodes[{index}].mode must be one of {sorted(MODES)}")
    if not isinstance(item["waveform"], str) or item["waveform"] not in {"smoke", "reference"}:
        raise CampaignError(f"episodes[{index}].waveform must be smoke or reference")
    if "pulse" in item and (not isinstance(item["pulse"], str) or
                             item["pulse"] not in {"rrc", "tapered-rrc"}):
        raise CampaignError(f"episodes[{index}].pulse must be rrc or tapered-rrc")
    if "fec" in item and (not isinstance(item["fec"], str) or item["fec"] not in {"off", "rs20", "rs60"}):
        raise CampaignError(f"episodes[{index}].fec must be off, rs20 or rs60")
    if isinstance(item["seed"], bool) or not isinstance(item["seed"], int) or not 0 <= item["seed"] < 2**64:
        raise CampaignError(f"episodes[{index}].seed must be an unsigned 64-bit integer")
    if "control" in item and (not isinstance(item["control"], str) or item["control"] not in CONTROLS):
        raise CampaignError(f"episodes[{index}].control must be one of {sorted(CONTROLS)}")
    if "backend" in item and (not isinstance(item["backend"], str) or item["backend"] not in BACKENDS):
        raise CampaignError(f"episodes[{index}].backend must be one of {sorted(BACKENDS)}")
    for key in ("cn0_db_hz",):
        if key in item:
            _bounded_number(item[key], f"episodes[{index}].{key}", -200, 200)
    for key in ("bit_seconds", "duration_seconds"):
        if key in item:
            _bounded_number(item[key], f"episodes[{index}].{key}", .001, 10_000_000)
    if "oracle_slot" in item:
        if isinstance(item["oracle_slot"], bool) or not isinstance(item["oracle_slot"], int) or not 0 <= item["oracle_slot"] < 2**64:
            raise CampaignError(f"episodes[{index}].oracle_slot must be a nonnegative unsigned 64-bit integer")
    for field in ("search_slot", "capture_digest_slot"):
        if field in item and (not _is_nonnegative_int(item[field]) or item[field] >= 2**64):
            raise CampaignError(f"episodes[{index}].{field} must be a nonnegative unsigned 64-bit integer")
    if "search_timing" in item and (not isinstance(item["search_timing"], str) or item["search_timing"] not in {"selected", "full"}):
        raise CampaignError(f"episodes[{index}].search_timing must be selected or full")
    if "history_reference_delay_seconds" in item:
        _bounded_number(item["history_reference_delay_seconds"],
                        f"episodes[{index}].history_reference_delay_seconds", 0, sys.float_info.max)
    for field in ("hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement"):
        if field in item and not isinstance(item[field], bool):
            raise CampaignError(f"episodes[{index}].{field} must be boolean")
    if "chunk" in item:
        _bounded_number(item["chunk"], f"episodes[{index}].chunk", 1, 16384, integer=True)
    if "fade_slots" in item:
        _bounded_number(item["fade_slots"], f"episodes[{index}].fade_slots", 1, 2, integer=True)
    if "capture_lead_seconds" in item:
        _bounded_number(item["capture_lead_seconds"], f"episodes[{index}].capture_lead_seconds", .25, 10)
    if "message" in item:
        message = item["message"]
        if not isinstance(message, str):
            raise CampaignError(f"episodes[{index}].message must be printable ASCII")
        try:
            encoded_message = message.encode("ascii")
        except UnicodeEncodeError as exc:
            raise CampaignError(f"episodes[{index}].message must be printable ASCII") from exc
        if not 1 <= len(encoded_message) <= 16 or any(byte < 32 or byte >= 127 for byte in encoded_message):
            raise CampaignError(f"episodes[{index}].message must contain 1..16 printable ASCII bytes")
    if "max_wall_seconds" in item:
        _bounded_number(item["max_wall_seconds"], f"episodes[{index}].max_wall_seconds", .001,
                        campaign_per_trial_wall_seconds)
    if "max_samples" in item:
        _bounded_number(item["max_samples"], f"episodes[{index}].max_samples", 1,
                        campaign_max_samples, integer=True)
    if "helper" in item and not isinstance(item["helper"], bool):
        raise CampaignError(f"episodes[{index}].helper must be boolean")
    if "stratum" in item:
        _nonempty_str(item["stratum"], f"episodes[{index}].stratum")
    for field in ("id", "scenario"):
        if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", item[field]) is None:
            raise CampaignError(f"episodes[{index}].{field} must be a simple identifier")
    mode = item["mode"]
    control = item.get("control", "wanted")
    if "fade_slots" in item and control != "interior-fade":
        raise CampaignError(f"episodes[{index}]: fade_slots is valid only for interior-fade control")
    if mode not in {"receive", "slot", "diagnose", "hf-search"} and "control" in item:
        raise CampaignError(f"episodes[{index}]: control is valid only for receiver or diagnostic mode")
    if mode in {"receive", "slot", "diagnose", "hf-search"} and control not in CONTROLS:
        raise CampaignError(f"episodes[{index}]: invalid receiver control")
    if mode == "slot" and control not in {"wanted", "noise"}:
        raise CampaignError(f"episodes[{index}]: slot mode requires wanted or noise control")
    if mode == "diagnose":
        if control != "wanted":
            raise CampaignError(f"episodes[{index}]: diagnose mode requires wanted control")
        if item["waveform"] != "reference":
            raise CampaignError(f"episodes[{index}]: diagnose mode requires reference waveform")
        if item.get("helper", False):
            raise CampaignError(f"episodes[{index}]: diagnose mode requires Helper disabled")
    if mode == "hf-search":
        if control not in {"wanted", "noise", "wrong-key", "wrong-ordinal"}:
            raise CampaignError(f"episodes[{index}]: hf-search requires wanted, noise, wrong-key or wrong-ordinal control")
        if item.get("helper", False) or item.get("backend", "cpu") != "cpu":
            raise CampaignError(f"episodes[{index}]: hf-search requires Helper-off CPU")
    if item.get("hf_phase_refinement", False) and not (mode == "hf-search" or
            (mode in {"receive", "slot"} and item.get("hf_public_phase_receiver", False))):
        raise CampaignError(f"episodes[{index}]: phase refinement requires hf-search or the public phase receiver")
    if item.get("hf_physical_diagnostic", False) and (mode != "hf-search" or control != "wanted"):
        raise CampaignError(f"episodes[{index}]: physical diagnostic requires wanted hf-search mode")
    if item.get("hf_public_phase_receiver", False):
        if mode not in {"receive", "slot"} or item.get("helper", False) or item.get("backend", "cpu") != "cpu":
            raise CampaignError(f"episodes[{index}]: HF public phase receiver requires receive/slot with Helper-off CPU")
    if "capture_digest_slot" in item:
        if mode not in {"receive", "slot", "hf-search"}:
            raise CampaignError(f"episodes[{index}]: capture_digest_slot requires receive, slot or hf-search mode")
        if mode == "slot" and item["capture_digest_slot"] != 0:
            raise CampaignError(f"episodes[{index}]: slot mode can hash only public slot 0")
        if mode == "hf-search" and item["capture_digest_slot"] != item.get("search_slot", 0):
            raise CampaignError(f"episodes[{index}]: hf-search capture_digest_slot must match search_slot")
    if ({"search_slot", "search_timing"} & item.keys()) and mode != "hf-search":
        raise CampaignError(f"episodes[{index}]: search options are valid only for hf-search mode")
    if "oracle_slot" in item and mode != "oracle":
        raise CampaignError(f"episodes[{index}]: oracle_slot is valid only for oracle mode")
    if ({"history_reference_delay_seconds", "hf_phase_bank"} & item.keys()) and mode != "oracle":
        raise CampaignError(f"episodes[{index}]: HF history reference delay and phase bank are valid only for oracle mode")


def _command(probe: Path, episode: dict[str, Any], max_samples: int, max_wall_seconds: float) -> list[str]:
    command = [str(probe), "--mode", episode["mode"], "--scenario", episode["scenario"],
               "--waveform", episode["waveform"], "--seed", str(episode["seed"]),
               "--max-samples", str(max_samples), "--max-wall-seconds", str(max_wall_seconds)]
    for key, option in (("cn0_db_hz", "--cn0"), ("control", "--control"), ("helper", "--helper"),
                        ("backend", "--backend"), ("bit_seconds", "--bit-seconds"),
                        ("duration_seconds", "--duration-seconds"), ("oracle_slot", "--oracle-slot"),
                        ("search_slot", "--search-slot"), ("search_timing", "--search-timing"),
                        ("capture_digest_slot", "--capture-digest-slot"),
                        ("chunk", "--chunk"), ("message", "--message"), ("pulse", "--pulse"),
                        ("fec", "--fec"), ("fade_slots", "--fade-slots"),
                        ("capture_lead_seconds", "--capture-lead-seconds"),
                        ("history_reference_delay_seconds", "--history-reference-delay-seconds"),
                        ("hf_phase_bank", "--hf-phase-bank"),
                        ("hf_public_phase_receiver", "--hf-public-phase-receiver"),
                        ("hf_physical_diagnostic", "--hf-physical-diagnostic"),
                        ("hf_phase_refinement", "--hf-phase-refinement")):
        if key in episode:
            value = episode[key]
            if key in {"helper", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement"}:
                if value:
                    command.append(option)
            else:
                command.extend((option, str(value)))
    return command


def _stderr(path: Path) -> str:
    data = path.read_bytes()[:MAX_OUTPUT_BYTES]
    return data.decode("utf-8", errors="replace")


def _parse_receipt(stdout_path: Path) -> dict[str, Any]:
    size = stdout_path.stat().st_size
    if size > MAX_OUTPUT_BYTES:
        raise CampaignError(f"native JSON output exceeded {MAX_OUTPUT_BYTES} bytes")
    try:
        value = json.loads(stdout_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CampaignError(f"native output is not one valid JSON receipt: {exc}") from exc
    if not isinstance(value, dict) or isinstance(value.get("schema_version"), bool) or value.get("schema_version") != 1:
        raise CampaignError("native receipt must be a schema_version 1 JSON object")
    if value.get("status") not in STATUSES:
        raise CampaignError("native receipt status is missing or invalid")
    return value


def _is_finite_number(value: Any) -> bool:
    return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)


def _is_nonnegative_int(value: Any) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= 0


def _validate_refinement_trace(trace: Any, *, full: bool, timing_count: int,
                               block_count: Any = None, block_rows: Any = None,
                               sample_rate: Any = None, aperture_seconds: float | None = None) -> None:
    """Check fixed-mixture provenance and the bounded public refinement receipt."""
    if (not isinstance(trace, dict) or trace.get("enabled") is not True or
            trace.get("old_family_weight") != .5 or trace.get("new_family_weight") != .5):
        raise CampaignError("phase refinement is missing its enabled equal-prior public trace")
    scores = []
    for field in ("old_bit_log_e", "new_bit_log_e", "combined_bit_log_e"):
        values = trace.get(field)
        if (not isinstance(values, list) or len(values) != 2 or
                any(value is not None and not _is_finite_number(value) for value in values)):
            raise CampaignError("phase refinement requires finite or omitted-mass bit evidence")
        scores.append(values)
    for old, new, combined in zip(*scores):
        available = [value for value in (old, new) if value is not None]
        expected = None
        if available:
            peak = max(available)
            expected = peak + math.log(sum(math.exp(value - peak) for value in available)) - math.log(2)
        if ((expected is None) != (combined is None) or
                (expected is not None and not math.isclose(expected, combined, rel_tol=1e-10, abs_tol=1e-9))):
            raise CampaignError("phase refinement must preserve the equal old/new family prior")
    availability_fields = ("requested", "executed", "unavailable_reason")
    explicit_availability = any(field in trace for field in availability_fields)
    executed = trace.get("executed", True)
    if explicit_availability and (trace.get("requested") is not True or not isinstance(trace.get("executed"), bool) or
            not isinstance(trace.get("unavailable_reason"), str) or
            (executed and trace["unavailable_reason"] != "") or
            (not executed and not trace["unavailable_reason"].strip())):
        raise CampaignError("phase refinement must explicitly distinguish executed and unavailable requested work")
    work_fields = ("fft_row_stages", "spectrum_cells", "table_lookups", "direct_sweeps", "direct_rows",
                   "phase_rotations", "trigonometric_evaluations", "likelihood_cells")
    if not executed:
        if trace["unavailable_reason"] not in {"new_phase_planner_unsupported", "new_phase_work_count_overflow",
                "new_phase_fft_work_cap", "new_phase_table_lookup_cap", "new_phase_direct_row_cap"}:
            raise CampaignError("only public new-family planning limits permit the declared fixed-prior fallback")
        if scores[1] != [None, None]:
            raise CampaignError("unavailable new-family evidence must retain zero prior contribution")
        empty_fields = ["old_top_timing_indices", "segmented_top_timing_indices", "selected_timing_indices", "timing_ranks"]
        empty_fields.append("timing_results" if full else "selected_shape_counts")
        if any(trace.get(field) != [] for field in empty_fields):
            raise CampaignError("unavailable refinement must not retain selected timings, ranks, shapes or proposals")
        for field in ("timing_results", "selected_shape_counts"):
            if field in trace and trace[field] != []:
                raise CampaignError("unavailable refinement must have empty optional result arrays")
        if any(field in trace for field in ("shape_count", "nu_count", "nu_step_hz", "intermediate_group_rows",
                "intermediate_group_count", "intermediate_iteration_limit", "coherent_iteration_limit")):
            raise CampaignError("unavailable refinement must not claim an executed phase or stage plan")
        work = trace.get("work")
        if (not isinstance(work, dict) or any(not _is_nonnegative_int(work.get(field)) or work[field] != 0 for field in work_fields) or
                not _is_nonnegative_int(trace.get("retained_statistics_workspace_bytes")) or
                trace["retained_statistics_workspace_bytes"] != 0 or
                not _is_finite_number(trace.get("retained_new_prior_mass")) or trace["retained_new_prior_mass"] != 0):
            raise CampaignError("unavailable refinement must report zero actual new work, storage and retained mass")
        if "work_bound" in trace:
            bound = trace["work_bound"]
            if (not isinstance(bound, dict) or any(not _is_nonnegative_int(bound.get(field)) for field in work_fields) or
                    trace.get("work_bound_scope") != "full public timing preflight; unavailable family was not admitted or executed"):
                raise CampaignError("unavailable refinement attempted work bound must remain a valid public quote")
        return
    groups = []
    for field, limit in (("old_top_timing_indices", 3), ("segmented_top_timing_indices", 3), ("selected_timing_indices", 6)):
        values = trace.get(field)
        if (not isinstance(values, list) or len(values) > limit or
                any(not _is_nonnegative_int(value) or value >= timing_count for value in values) or len(set(values)) != len(values)):
            raise CampaignError("phase refinement timing frontier exceeds its fixed public bound")
        groups.append(values)
    if set(groups[2]) != set(groups[0]) | set(groups[1]):
        raise CampaignError("phase refinement frontier must preserve both proposal rankings")
    for field in ("shape_count", "nu_count"):
        if not _is_nonnegative_int(trace.get(field)) or trace[field] == 0:
            raise CampaignError("phase refinement requires the full fixed shape prior dimensions")
    if not _is_finite_number(trace.get("nu_step_hz")) or trace["nu_step_hz"] < 0:
        raise CampaignError("phase refinement requires its fixed nu lattice")
    stage_fields = ("intermediate_group_rows", "intermediate_group_count",
                    "intermediate_iteration_limit", "coherent_iteration_limit")
    staged = any(field in trace for field in stage_fields)
    grouped = False
    if staged:
        if any(not _is_nonnegative_int(trace.get(field)) for field in stage_fields):
            raise CampaignError("staged phase refinement requires complete integer public plan dimensions")
        if (not _is_nonnegative_int(block_count) or block_count == 0 or
                not _is_nonnegative_int(block_rows) or block_rows == 0 or
                not _is_finite_number(sample_rate) or sample_rate <= 0):
            raise CampaignError("staged phase refinement requires the enclosing public block geometry")
        group_rows, group_count = (trace[field] for field in stage_fields[:2])
        grouped = group_count > 0
        block_seconds = block_rows / sample_rate
        if ((group_rows > 0) != grouped or
                trace["intermediate_iteration_limit"] != (8 if grouped else 0) or
                trace["coherent_iteration_limit"] != (8 if grouped else 16)):
            raise CampaignError("staged phase refinement must preserve the bounded 8+8 or 0+16 proposal budget")
        if grouped:
            expected_rows = 4 * min(block_count, max(1, int(8 / block_seconds)))
            if group_rows != expected_rows or group_count != 1 + (block_count - 1) // group_rows:
                raise CampaignError("intermediate phase groups must partition the declared public blocks")
        # A partial last block does not establish an exact duration. Only
        # require the threshold when the public interval makes it certain.
        if aperture_seconds is not None:
            if not _is_finite_number(aperture_seconds) or grouped != (aperture_seconds >= 180):
                raise CampaignError("staged phase refinement disagrees with the public aperture threshold")
        elif ((block_count - 1) * block_seconds >= 180 and not grouped) or (block_count * block_seconds < 180 and grouped):
            raise CampaignError("staged phase refinement disagrees with the public block-duration bound")
    work, bound = trace.get("work"), trace.get("work_bound")
    if (not isinstance(work, dict) or not isinstance(bound, dict) or
            any(not _is_nonnegative_int(work.get(field)) or not _is_nonnegative_int(bound.get(field)) or
                work[field] > bound[field] for field in work_fields)):
        raise CampaignError("phase refinement work must obey every declared numerical bound")
    if full:
        rows = trace.get("timing_results")
        if (not isinstance(rows, list) or len(rows) != len(groups[2]) or
                any(not isinstance(row, dict) or not _is_nonnegative_int(row.get("timing_index")) for row in rows) or
                sorted(row["timing_index"] for row in rows) != sorted(groups[2])):
            raise CampaignError("shadow refinement must preserve all selected timing results")
        intermediate_fields = ("intermediate_used", "intermediate_initial_log_lr",
                               "intermediate_final_log_lr", "intermediate_accepted_steps")
        for row in rows:
            proposals = row.get("proposals")
            if staged and (not isinstance(proposals, list) or not _is_nonnegative_int(row.get("proposal_count")) or
                           row["proposal_count"] != len(proposals) or len(proposals) > 3):
                raise CampaignError("staged shadow refinement requires its bounded proposal count")
            for proposal in proposals if isinstance(proposals, list) else []:
                present = isinstance(proposal, dict) and any(field in proposal for field in intermediate_fields)
                if not staged:
                    if present:
                        raise CampaignError("intermediate proposal diagnostics require their public grouped plan")
                    continue
                if (not isinstance(proposal, dict) or proposal.get("intermediate_used") is not grouped or
                        any(not _is_finite_number(proposal.get(field)) for field in
                            ("intermediate_initial_log_lr", "intermediate_final_log_lr", "initial_log_lr", "continuous_log_lr")) or
                        not _is_nonnegative_int(proposal.get("accepted_steps")) or
                        proposal["accepted_steps"] > trace["coherent_iteration_limit"] or
                        not _is_nonnegative_int(proposal.get("intermediate_accepted_steps")) or
                        proposal["intermediate_accepted_steps"] > trace["intermediate_iteration_limit"]):
                    raise CampaignError("staged proposal diagnostics must be finite and obey both iteration limits")
                if (proposal["intermediate_final_log_lr"] < proposal["intermediate_initial_log_lr"] or
                        proposal["continuous_log_lr"] < proposal["initial_log_lr"]):
                    raise CampaignError("each proposal stage must preserve its own nondecreasing objective")
                if not grouped and (proposal["intermediate_initial_log_lr"] != 0 or proposal["intermediate_final_log_lr"] != 0):
                    raise CampaignError("unused intermediate proposal diagnostics must remain zero")


def _communication_endpoint(episode: dict[str, Any]) -> str:
    return "correct_decoded_complete" if episode.get("fec", "off") != "off" else "correct_complete"


def _validate_decoded_success(receipt: dict[str, Any], episode: dict[str, Any], fec: str) -> None:
    """Check the decoded endpoint from independent framing and payload fields."""
    outcomes, plan = receipt["outcomes"], receipt["plan"]
    wire = plan.get("wire_bits")
    if not _is_nonnegative_int(wire) or not 0 < wire <= (208 if fec == "off" else 304 if fec == "rs20" else 400):
        raise CampaignError("decoded success requires bounded wire-bit geometry")
    if (receipt.get("status") != "completed" or episode.get("control", "wanted") not in
            {"wanted", "capture-gap", "interior-fade"} or outcomes.get("physical_completions") != 1 or
            not _is_nonnegative_int(outcomes.get("accepted_messages")) or outcomes["accepted_messages"] != 1 or outcomes.get("premature_completions") != 0 or
            outcomes.get("false_data") != 0 or outcomes.get("missed_completion") is not False or
            not _is_nonnegative_int(outcomes.get("actual_scored_slots")) or outcomes["actual_scored_slots"] < wire + 2):
        raise CampaignError("decoded success conflicts with target or physical completion counts")
    messages = receipt.get("messages")
    if not isinstance(messages, list) or not all(isinstance(message, dict) for message in messages):
        raise CampaignError("decoded success requires native message receipts")
    if any(not isinstance(message.get(field), bool) for message in messages
           for field in ("physical_complete", "decoded", "premature")):
        raise CampaignError("decoded success requires boolean native message states")
    accepted = [message for message in messages if message["decoded"]]
    if (len(accepted) != 1 or sum(message["physical_complete"] for message in messages) != 1 or
            any(message["premature"] for message in messages)):
        raise CampaignError("decoded success requires exactly one accepted physical message")
    message = accepted[0]
    for field in ("first_public_slot", "stop_public_slot", "slots_consumed", "observed_bits", "missing_slots", "consumed_bytes"):
        if not _is_nonnegative_int(message.get(field)):
            raise CampaignError("decoded success requires integer native framing fields")
    if (message["first_public_slot"] != 0 or message["stop_public_slot"] != wire + 2 or
            message["slots_consumed"] != wire + 2 or message["observed_bits"] + message["missing_slots"] != wire or
            message.get("trailing_quiet_slots") != [wire, wire + 1] or message.get("quiet_slots") != 2 or
            message.get("long_form") is not False or message.get("message_ready_for_decode") is not True or
            message.get("physical_complete") is not True or message.get("matches_transmitted_message") is not True or
            message.get("decoded_endpoint_matches") is not True or message.get("fec") != fec or
            message["consumed_bytes"] * (1 if fec == "off" else 8) != wire):
        raise CampaignError("decoded success conflicts with exact short-frame physical extent")
    erasures = message.get("erasure_slot_indices")
    if (not isinstance(erasures, list) or len(erasures) != message["missing_slots"] or
            any(not _is_nonnegative_int(index) or index >= wire for index in erasures) or
            erasures != sorted(set(erasures))):
        raise CampaignError("decoded success requires distinct located erasures inside the frame")
    payload = message.get("decoded_payload_bytes")
    if (not isinstance(payload, list) or any(not _is_nonnegative_int(byte) or byte > 255 for byte in payload) or
            payload != list(plan["message"].encode("ascii"))):
        raise CampaignError("decoded success raw payload does not match the transmitted message")
    if fec == "off":
        return  # Native Off has no accepted-codeword FEC region accounting.
    if not _is_nonnegative_int(message.get("corrected_bytes")) or message.get("authenticated") is not False:
        raise CampaignError("short FEC success requires native changed-byte counts and public CRC authentication=false")
    # Schema-only accounting of the canonical short-frame profile, not an
    # encoder/decoder: the public wire extent determines body/parity lengths.
    layouts = []
    for body_bytes in range(6, 32):
        parity_bytes = (body_bytes + 4) // 5 if fec == "rs20" else (3 * body_bytes + 4) // 5
        if (body_bytes + parity_bytes) * 8 == wire:
            layouts.append({"data": (body_bytes - 4) * 8, "integrity": 32, "parity": parity_bytes * 8})
    if len(layouts) != 1:
        raise CampaignError("decoded success wire extent has no unique canonical short FEC layout")
    stats = message.get("fec_stats")
    fields = ("received_bits", "corrected_bits", "missing_bits", "corrected_bytes", "erased_bytes", "repaired_bytes")
    if not isinstance(stats, dict):
        raise CampaignError("decoded success requires native FEC region statistics")
    received = missing = corrected = corrected_bytes = repaired_bytes = first_bit = 0
    for region in ("data", "integrity", "parity"):
        area = stats.get(region)
        if not isinstance(area, dict) or any(not _is_nonnegative_int(area.get(field)) for field in fields):
            raise CampaignError("decoded success requires complete native FEC region statistics")
        if area["received_bits"] + area["missing_bits"] != layouts[0][region]:
            raise CampaignError("decoded success FEC region extent disagrees with the public short-frame layout")
        stop_bit = first_bit + layouts[0][region]
        region_erasures = [index for index in erasures if first_bit <= index < stop_bit]
        if (area["missing_bits"] != len(region_erasures) or
                area["erased_bytes"] != len({index // 8 for index in region_erasures})):
            raise CampaignError("decoded success FEC erasure regions disagree with located missing slots")
        first_bit = stop_bit
        if (area["corrected_bits"] > area["received_bits"] or
                not area["corrected_bytes"] <= area["corrected_bits"] <= 8 * area["corrected_bytes"] or
                not area["erased_bytes"] <= area["missing_bits"] <= 8 * area["erased_bytes"] or
                not max(area["corrected_bytes"], area["erased_bytes"]) <= area["repaired_bytes"] <=
                area["corrected_bytes"] + area["erased_bytes"]):
            raise CampaignError("decoded success has inconsistent FEC repair statistics")
        received += area["received_bits"]
        missing += area["missing_bits"]
        corrected += area["corrected_bits"]
        corrected_bytes += area["corrected_bytes"]
        repaired_bytes += area["repaired_bytes"]
    if not corrected_bytes <= message["corrected_bytes"] <= repaired_bytes:
        raise CampaignError("decoded success changed-byte count conflicts with native FEC repair regions")
    if (received != message["observed_bits"] or missing != message["missing_slots"] or
            corrected != outcomes["wrong_bits"] or missing != outcomes["ambiguous_active_slots"] + outcomes["quiet_active_slots"]):
        raise CampaignError("decoded success FEC statistics conflict with raw slot counts")


def _validate_completed_receipt(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    identity_fields = ("requested_scenario", "mode", "waveform", "seed", "control")
    missing_identity = [field for field in identity_fields if field not in receipt]
    if missing_identity:
        raise CampaignError("completed receipt is missing identity fields: " + ", ".join(missing_identity))

    for field, expected in (("requested_scenario", episode["scenario"]),
                            ("mode", episode["mode"]),
                            ("waveform", episode["waveform"]),
                            ("seed", episode["seed"]),
                            ("control", episode.get("control", "wanted"))):
        if receipt[field] != expected:
            raise CampaignError(f"native receipt {field} does not match the frozen episode")

    source_identity = receipt.get("source_identity")
    if not isinstance(source_identity, dict):
        raise CampaignError("completed receipt is missing source_identity")
    for field in ("appraisal_source_sha256", "native_source_sha256"):
        digest = source_identity.get(field)
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            raise CampaignError(f"completed receipt source_identity.{field} must be a SHA-256 hex digest")

    plan = receipt.get("plan")
    if not isinstance(plan, dict):
        raise CampaignError("completed receipt is missing plan")
    expected_digest_slot = episode.get("capture_digest_slot", episode.get("search_slot", 0) if episode["mode"] == "hf-search" else None)
    if "capture_digest_slot" in plan or expected_digest_slot is not None:
        actual_digest_slot = plan.get("capture_digest_slot")
        if (actual_digest_slot != expected_digest_slot or
                (actual_digest_slot is not None and not _is_nonnegative_int(actual_digest_slot)) or
                plan.get("capture_digest_slot_override") is not ("capture_digest_slot" in episode)):
            raise CampaignError("native capture digest slot does not match the frozen episode")
        if expected_digest_slot is not None:
            first, stop = plan.get("capture_digest_first_sample"), plan.get("capture_digest_stop_sample")
            if (not _is_nonnegative_int(first) or not _is_nonnegative_int(stop) or stop <= first or
                    not _is_nonnegative_int(plan.get("horizon_samples")) or stop > plan["horizon_samples"]):
                raise CampaignError("capture digest requires a whole observation within the declared horizon")
            channel = receipt.get("channel")
            if (not isinstance(channel, dict) or channel.get("selected_observation_complete") is not True or
                    not _is_nonnegative_int(channel.get("selected_observation_hashed_samples")) or
                    channel["selected_observation_hashed_samples"] != stop - first):
                raise CampaignError("capture digest must cover every selected IQ and validity sample")
            digest = channel.get("selected_observation_sha256")
            if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
                raise CampaignError("capture digest requires a SHA-256 of the selected observation")
    for field in ("cn0_db_hz", "requested_bit_seconds"):
        if not _is_finite_number(plan.get(field)):
            raise CampaignError(f"completed receipt plan.{field} must be finite")
    if plan["requested_bit_seconds"] <= 0:
        raise CampaignError("completed receipt plan.requested_bit_seconds must be positive")
    if plan.get("pulse") != episode.get("pulse", "rrc"):
        raise CampaignError("native plan pulse does not match the frozen episode")
    if "capture_lead_seconds" in episode or "capture_preroll_seconds" in plan:
        lead = plan.get("capture_preroll_seconds")
        if not _is_finite_number(lead) or lead != episode.get("capture_lead_seconds", .25):
            raise CampaignError("native capture lead does not match the frozen episode")
    if "capture_preroll_override" in plan and plan["capture_preroll_override"] is not ("capture_lead_seconds" in episode):
        raise CampaignError("native capture lead override does not match the frozen episode")
    fec = episode.get("fec", "off")
    if plan.get("fec", "off") != fec:
        raise CampaignError("native FEC mode does not match the frozen episode")
    if "fade_slots" in episode or "fade_slots" in plan:
        expected_fade_slots = episode.get("fade_slots", 2) if episode.get("control", "wanted") == "interior-fade" else None
        actual_fade_slots = plan.get("fade_slots")
        if (actual_fade_slots != expected_fade_slots or
                (expected_fade_slots is not None and not _is_nonnegative_int(actual_fade_slots))):
            raise CampaignError("native fade slots does not match the frozen episode")
    if fec != "off":
        limit = 304 if fec == "rs20" else 400
        wire = plan.get("wire_bits")
        if (not _is_nonnegative_int(wire) or not 0 < wire <= limit or wire % 8 or
                plan.get("short_wire_bit_limit") != limit or plan.get("content_limit_bytes") != 16 or
                plan.get("long_form") is not False):
            raise CampaignError("native FEC receipt has invalid bounded short-frame geometry")
    requested_message = episode.get("message", "e")
    if plan.get("requested_message") != requested_message:
        raise CampaignError("native requested message does not match the frozen episode")
    effective_message = requested_message
    if episode.get("control", "wanted") in {"interior-fade", "capture-gap"} and requested_message == "e":
        effective_message = "A"
    if plan.get("message") != effective_message:
        raise CampaignError("native effective message does not match the frozen episode control")
    if "cn0_db_hz" in episode and not math.isclose(
            float(plan["cn0_db_hz"]), float(episode["cn0_db_hz"]), rel_tol=1e-9, abs_tol=1e-9):
        raise CampaignError("native plan C/N0 does not match the frozen episode")
    if "bit_seconds" in episode and not math.isclose(
            float(plan["requested_bit_seconds"]), float(episode["bit_seconds"]), rel_tol=1e-9, abs_tol=1e-9):
        raise CampaignError("native plan bit duration does not match the frozen episode")
    if episode["mode"] == "channel" and "duration_seconds" in episode:
        sample_rate = plan.get("sample_rate_hz")
        represented = plan.get("represented_seconds")
        if not _is_finite_number(sample_rate) or sample_rate <= 0 or not _is_finite_number(represented):
            raise CampaignError("completed channel receipt is missing its requested duration geometry")
        if abs(float(represented) - float(episode["duration_seconds"])) > 1 / float(sample_rate) + 1e-12:
            raise CampaignError("native channel duration does not match the frozen episode")
    if plan.get("helper_enabled") is not episode.get("helper", False):
        raise CampaignError("native plan Helper setting does not match the frozen episode")
    refinement_requested = episode.get("hf_phase_refinement", False)
    if plan.get("hf_phase_refinement_requested", False) is not refinement_requested:
        raise CampaignError("native phase refinement request does not match the frozen episode")
    physical_requested = episode.get("hf_physical_diagnostic", False)
    if plan.get("hf_physical_diagnostic_requested", False) is not physical_requested:
        raise CampaignError("native physical diagnostic request does not match the frozen episode")
    if not physical_requested and "hf_physical_diagnostic" in receipt:
        raise CampaignError("unrequested physical diagnostic in native receipt")
    phase_receiver_requested = episode.get("hf_public_phase_receiver", False)
    if plan.get("hf_public_phase_receiver_requested", False) is not phase_receiver_requested:
        raise CampaignError("native public phase receiver request does not match the frozen episode")
    if phase_receiver_requested:
        phase_receiver = receipt.get("hf_public_phase_receiver")
        if (not isinstance(phase_receiver, dict) or phase_receiver.get("experimental") is not True or
                phase_receiver.get("truth_information_passed_to_search") is not False or
                phase_receiver.get("refinement_enabled", False) is not refinement_requested or
                phase_receiver.get("model_family") != ("equal_prior_fixed97_and_cartesian_refinement" if refinement_requested else "sinusoidal_phase_97") or
                not _is_finite_number(phase_receiver.get("fixed_weight")) or phase_receiver["fixed_weight"] != .25):
            raise CampaignError("experimental public phase receiver must report its fixed family/prior and no truth inputs")
        maximum = phase_receiver.get("maximum_slot_reports")
        executed = phase_receiver.get("executed_slots")
        reports = phase_receiver.get("slots")
        if (not _is_nonnegative_int(maximum) or maximum == 0 or maximum != plan.get("scored_slot_horizon") or
                not _is_nonnegative_int(executed) or executed > maximum or
                not isinstance(reports, list) or len(reports) != executed):
            raise CampaignError("public phase receiver telemetry must obey the public report bound")
        indices = []
        for report in reports:
            if (not isinstance(report, dict) or not _is_nonnegative_int(report.get("slot_index")) or
                    report["slot_index"] >= maximum or report.get("component_complete") is not True):
                raise CampaignError("public phase receiver must report complete bounded supplement callbacks")
            if refinement_requested:
                count = report.get("evaluated_timing_atoms")
                if not _is_nonnegative_int(count) or count == 0:
                    raise CampaignError("operational refinement must report evaluated timing geometry")
                _validate_refinement_trace(report.get("phase_refinement"), full=False, timing_count=count,
                    block_count=report.get("blocks"), block_rows=report.get("block_rows"), sample_rate=plan.get("sample_rate_hz"))
                if report.get("bit_log_e") != report["phase_refinement"]["combined_bit_log_e"]:
                    raise CampaignError("operational refinement callback must use the reported combined evidence")
            elif report.get("phase_refinement", {"enabled": False}) != {"enabled": False}:
                raise CampaignError("unrequested operational phase refinement trace")
            indices.append(report["slot_index"])
        if len(set(indices)) != len(indices):
            raise CampaignError("public phase receiver must not duplicate slot callback reports")
    elif "hf_public_phase_receiver" in receipt:
        raise CampaignError("unrequested public phase receiver telemetry in native receipt")

    backend = receipt.get("backend")
    if not isinstance(backend, dict):
        raise CampaignError("completed receipt is missing backend")
    requested_backend = episode.get("backend", "cpu")
    if backend.get("requested") != requested_backend:
        raise CampaignError("native requested backend does not match the frozen episode")
    device = backend.get("device")
    if not isinstance(backend.get("executed_gpu_work"), bool):
        raise CampaignError("completed receipt backend.executed_gpu_work must be boolean")
    if episode["mode"] in {"receive", "slot", "channel", "diagnose", "hf-search"}:
        if not isinstance(device, str) or not device:
            raise CampaignError("completed processing receipt backend.device must identify the actual device")
        if requested_backend == "cpu" and device != "cpu":
            raise CampaignError("CPU episode reports a non-CPU execution device")
        if requested_backend in {"cuda", "vulkan", "gpu"}:
            match = re.fullmatch(r"(cuda|vulkan):([0-9]+)", device)
            allowed_families = {requested_backend} if requested_backend in {"cuda", "vulkan"} else {"cuda", "vulkan"}
            if (match is None or match.group(1) not in allowed_families or
                    backend["executed_gpu_work"] is not True):
                requested_label = requested_backend if requested_backend != "gpu" else "CUDA or Vulkan"
                raise CampaignError(f"requested {requested_label} episode did not report executed GPU device work")

    if episode["mode"] == "receive":
        outcomes = receipt.get("outcomes")
        if not isinstance(outcomes, dict):
            raise CampaignError("completed receive receipt is missing outcomes")
        if outcomes.get("receiver_executed") is not True:
            raise CampaignError("completed receive receipt must report receiver_executed=true")
        if outcomes.get("component_only") is not False:
            raise CampaignError("completed receive receipt must report component_only=false")
        if outcomes.get("completion_evaluated") is not True:
            raise CampaignError("completed receive receipt must evaluate physical completion")
        if not isinstance(outcomes.get("correct_complete"), bool):
            raise CampaignError("completed receive receipt is missing boolean outcomes.correct_complete")
        if not _is_nonnegative_int(outcomes.get("physical_completions")):
            raise CampaignError("completed receive receipt is missing nonnegative physical_completions")
        if not _is_nonnegative_int(outcomes.get("actual_scored_slots")) or outcomes["actual_scored_slots"] == 0:
            raise CampaignError("completed receive receipt must score at least one public slot")
        count_fields = ("premature_completions", "wrong_bits", "false_data",
                        "ambiguous_active_slots", "quiet_active_slots")
        for field in count_fields:
            if not _is_nonnegative_int(outcomes.get(field)):
                raise CampaignError(f"completed receive receipt is missing nonnegative outcomes.{field}")
        if not isinstance(outcomes.get("missed_completion"), bool):
            raise CampaignError("completed receive receipt is missing boolean outcomes.missed_completion")
        if outcomes["correct_complete"] and (
                outcomes["physical_completions"] != 1 or outcomes["premature_completions"] != 0 or
                outcomes["wrong_bits"] != 0 or outcomes["false_data"] != 0 or
                outcomes["ambiguous_active_slots"] != 0 or outcomes["quiet_active_slots"] != 0 or
                outcomes["missed_completion"]):
            raise CampaignError("successful receive outcome conflicts with its physical slot/completion counts")
        if "correct_decoded_complete" in outcomes and not isinstance(outcomes["correct_decoded_complete"], bool):
            raise CampaignError("receive receipt requires boolean correct_decoded_complete when present")
        if fec != "off":
            if not isinstance(outcomes.get("correct_decoded_complete"), bool):
                raise CampaignError("FEC receive receipt requires boolean correct_decoded_complete")
            if outcomes.get("communication_endpoint") != "correct_decoded_complete":
                raise CampaignError("FEC receive receipt requires the decoded-message endpoint")
        if outcomes.get("correct_decoded_complete") is True:
            _validate_decoded_success(receipt, episode, fec)
    elif episode["mode"] == "channel":
        channel = receipt.get("channel")
        if not isinstance(channel, dict) or not isinstance(channel.get("contains_signal_energy"), bool):
            raise CampaignError("completed channel receipt is missing boolean channel energy result")
        if not _is_nonnegative_int(channel.get("samples")):
            raise CampaignError("completed channel receipt is missing nonnegative channel.samples")
    elif episode["mode"] == "slot":
        outcomes = receipt.get("outcomes")
        if not isinstance(outcomes, dict):
            raise CampaignError("completed slot receipt is missing outcomes")
        if outcomes.get("receiver_executed") is not True:
            raise CampaignError("completed slot receipt must report receiver_executed=true")
        if outcomes.get("component_only") is not True:
            raise CampaignError("completed slot receipt must report component_only=true")
        if not _is_nonnegative_int(outcomes.get("actual_scored_slots")) or outcomes["actual_scored_slots"] != 1:
            raise CampaignError("completed slot receipt must report exactly one scored slot")
        if outcomes.get("first_public_observation_complete") is not True:
            raise CampaignError("completed slot receipt must report a complete first public observation")
        if not isinstance(outcomes.get("first_public_observation_correct_data"), bool):
            raise CampaignError("completed slot receipt must report boolean first_public_observation_correct_data")
        if episode.get("control", "wanted") == "noise" and outcomes["first_public_observation_correct_data"]:
            raise CampaignError("noise-only slot must never report correct DATA")
        if outcomes.get("correct_complete") is not False:
            raise CampaignError("slot receipt must never report complete-message success")
        if outcomes.get("correct_decoded_complete", False) is not False:
            raise CampaignError("slot receipt must never report decoded-message success")
        if outcomes.get("completion_evaluated") is not False:
            raise CampaignError("slot receipt must not evaluate message completion")
        slots = receipt.get("slots")
        if not isinstance(slots, list) or len(slots) != 1 or not isinstance(slots[0], dict):
            raise CampaignError("completed slot receipt must include exactly one slot score")
        score = slots[0]
        if (isinstance(score.get("index"), bool) or not isinstance(score.get("index"), int) or
                score["index"] != 0 or score.get("decision") not in {"data", "quiet", "ambiguous"}):
            raise CampaignError("completed slot receipt has an invalid first slot decision")
        if score.get("complete") is not True or score.get("all_valid") is not True:
            raise CampaignError("completed slot receipt score must be complete and valid")
        if (outcomes["first_public_observation_correct_data"] and score["decision"] != "data"):
            raise CampaignError("slot reports correct DATA without a DATA decision")
    elif episode["mode"] == "diagnose":
        diagnostic = receipt.get("vlf_diagnostic")
        if not isinstance(diagnostic, dict):
            raise CampaignError("completed diagnose receipt is missing vlf_diagnostic")
        if diagnostic.get("component_complete") is not True or diagnostic.get("phase") != "completed":
            raise CampaignError("diagnose component must report completed phase")
        if diagnostic.get("capacity_qualified") is not False:
            raise CampaignError("diagnose component must not claim capacity qualification")
        if diagnostic.get("complete_receiver_service") is not False:
            raise CampaignError("diagnose component must not claim complete receiver service")
        outcomes = receipt.get("outcomes")
        if not isinstance(outcomes, dict):
            raise CampaignError("completed diagnose receipt is missing outcomes")
        if outcomes.get("component_only") is not True:
            raise CampaignError("diagnose receipt must report component_only=true")
        if outcomes.get("completion_evaluated") is not False:
            raise CampaignError("diagnose receipt must not evaluate message completion")
        if outcomes.get("correct_complete") is not False:
            raise CampaignError("diagnose receipt must never report complete-message success")
    elif episode["mode"] == "hf-search":
        selected = episode.get("search_slot", 0)
        if (not _is_nonnegative_int(plan.get("search_slot")) or plan["search_slot"] != selected or
                plan.get("search_timing") != episode.get("search_timing", "selected")):
            raise CampaignError("HF search receipt does not match the frozen search slot or timing policy")
        observation = plan.get("search_observation")
        if (not isinstance(observation, dict) or
                not all(_is_nonnegative_int(observation.get(field)) for field in ("first", "stop")) or
                observation["stop"] <= observation["first"] or observation["stop"] != plan.get("horizon_samples")):
            raise CampaignError("HF search requires the complete selected observation ending at the declared horizon")
        digest = plan.get("search_observation_sha256")
        if (plan.get("capture_digest_first_sample") != observation["first"] or
                plan.get("capture_digest_stop_sample") != observation["stop"] or
                receipt.get("channel", {}).get("selected_observation_sha256") != digest):
            raise CampaignError("HF search observation must match its incremental capture digest")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            raise CampaignError("HF search requires a hash of the complete selected observation")
        diagnostic = receipt.get("hf_public_search")
        if (not isinstance(diagnostic, dict) or diagnostic.get("component_complete") is not True or
                not _is_nonnegative_int(diagnostic.get("slot_index")) or diagnostic["slot_index"] != selected):
            raise CampaignError("HF search requires a completed public-search diagnostic for the selected slot")
        if diagnostic.get("refinement_enabled", False) is not refinement_requested:
            raise CampaignError("shadow phase refinement does not match the frozen request")
        if refinement_requested:
            if diagnostic.get("affine_comparison_evaluated") is not False or diagnostic.get("selected_timing_renormalized") is not False:
                raise CampaignError("refinement shadow must preserve operational geometry and full timing prior")
            count = diagnostic.get("evaluated_timing_atoms")
            if not _is_nonnegative_int(count) or count == 0:
                raise CampaignError("refinement shadow requires its evaluated timing geometry")
            sample_rate = plan.get("sample_rate_hz")
            aperture_seconds = ((observation["stop"] - observation["first"]) / sample_rate
                if _is_finite_number(sample_rate) and sample_rate > 0 else None)
            _validate_refinement_trace(diagnostic.get("phase_refinement"), full=True, timing_count=count,
                block_count=diagnostic.get("blocks"), block_rows=diagnostic.get("block_rows"),
                sample_rate=sample_rate, aperture_seconds=aperture_seconds)
            families = diagnostic.get("families")
            if (not isinstance(families, list) or len(families) != 3 or
                    not isinstance(families[2], dict) or
                    families[2].get("bit_log_e") != diagnostic["phase_refinement"]["old_bit_log_e"]):
                raise CampaignError("refinement must preserve the separately reported fixed97 evidence")
        elif diagnostic.get("phase_refinement", {"enabled": False}) != {"enabled": False}:
            raise CampaignError("unrequested shadow phase refinement trace")
        for field in ("receiver_executed", "completion_evaluated", "capacity_qualified", "truth_information_passed_to_search"):
            if diagnostic.get(field) is not False:
                raise CampaignError("HF public search must preserve shadow-only scope without truth inputs")
        outcomes = receipt.get("outcomes")
        if not isinstance(outcomes, dict):
            raise CampaignError("HF search receipt is missing outcomes")
        for field, value in (("receiver_executed", False), ("component_only", True),
                             ("public_search_executed", True), ("completion_evaluated", False),
                             ("correct_complete", False), ("correct_decoded_complete", False)):
            if outcomes.get(field) is not value:
                raise CampaignError("HF search must not claim native decisions or message completion")
        for field in ("actual_scored_slots", "physical_completions", "accepted_messages"):
            if not _is_nonnegative_int(outcomes.get(field)) or outcomes[field] != 0:
                raise CampaignError("HF search must not report operational receiver output")
        if receipt.get("slots") != [] or receipt.get("messages") != []:
            raise CampaignError("HF search must not publish operational slot or message receipts")
        evaluation = receipt.get("hf_search_evaluation")
        if (not isinstance(evaluation, dict) or not _is_nonnegative_int(evaluation.get("slot_index")) or
                evaluation["slot_index"] != selected or evaluation.get("truth_information_passed_to_search") is not False or
                not isinstance(evaluation.get("transmitted_active"), bool)):
            raise CampaignError("HF search requires a separate post-search evaluator")
        expected = evaluation.get("expected_receiver_bit")
        if expected is not None and (not _is_nonnegative_int(expected) or expected not in (0, 1)):
            raise CampaignError("HF search evaluator has an invalid expected bit")
        if episode.get("control", "wanted") != "wanted" and expected is not None:
            raise CampaignError("negative-control HF search must not expect a wanted receiver bit")
        if not evaluation["transmitted_active"] and expected is not None:
            raise CampaignError("quiet HF search must not expect a wanted receiver bit")
        if physical_requested:
            physical = receipt.get("hf_physical_diagnostic")
            if (not isinstance(physical, dict) or physical.get("component_complete") is not True or
                    physical.get("slot_index") != selected or physical.get("observation") != observation or
                    physical.get("observation_sha256") != digest or expected is None or
                    physical.get("prefix_samples_rendered_per_twin") != observation["stop"]):
                raise CampaignError("physical diagnostic must use the complete unchanged selected observation and full prefix")
            for flag in ("truth_information_passed_to_search", "receiver_executed", "completion_evaluated",
                         "capacity_qualified", "is_theoretical_upper_bound"):
                if physical.get(flag) is not False:
                    raise CampaignError("physical diagnostic must preserve finite evaluator-only scope")
            if physical.get("gain_prior_variance") != 1:
                raise CampaignError("physical diagnostic must use the fixed CN(0,1) gain prior")
            winner = physical.get("winning_public_cell")
            if not isinstance(winner, dict):
                raise CampaignError("physical diagnostic requires the selected public-cell replay")
            refined_available = physical.get("refinement_replay_available", False)
            refinement_trace = diagnostic.get("phase_refinement", {})
            refinement_executed = refinement_requested and refinement_trace.get("executed", True)
            if (physical.get("refinement_requested", False) is not refinement_requested or
                    not isinstance(refined_available, bool) or (refined_available and not refinement_executed)):
                raise CampaignError("physical refinement replay must match the requested public search")
            projection_groups = [physical.get("physical_templates"), winner.get("projection")]
            replay_cells = [winner]
            if refined_available:
                refined = physical.get("refined_public_cell")
                if not isinstance(refined, dict):
                    raise CampaignError("physical refinement replay needs its selected snapped public cell")
                shape = refined.get("shape_id")
                if (not isinstance(shape, dict) or any(isinstance(shape.get(field), bool) or not isinstance(shape.get(field), int)
                        for field in ("a", "b")) or not 0 < shape["a"]**2 + shape["b"]**2 <= 64**2 or
                        not _is_nonnegative_int(shape.get("nu")) or shape["nu"] >= diagnostic["phase_refinement"]["nu_count"]):
                    raise CampaignError("physical refinement must use a valid fixed-grid shape ID")
                bit = refined.get("selected_bit")
                timing_index = refined.get("timing_index")
                trace_rows = diagnostic["phase_refinement"]["timing_results"]
                matching = [row for row in trace_rows if row.get("timing_index") == timing_index]
                if (not _is_nonnegative_int(bit) or bit > 1 or not _is_nonnegative_int(timing_index) or len(matching) != 1):
                    raise CampaignError("physical refinement cell must identify an evaluated public timing and bit")
                public_row = matching[0]
                for field in ("maximum_shape", "maximum_log_lr", "maximum_frequency_hz"):
                    if not isinstance(public_row.get(field), list) or len(public_row[field]) != 2:
                        raise CampaignError("physical refinement needs the original public winning-cell trace")
                if (shape != public_row["maximum_shape"][bit] or
                        refined.get("frequency_hz") != public_row["maximum_frequency_hz"][bit] or
                        refined.get("search_maximum_single_cell_log_lr") != public_row["maximum_log_lr"][bit]):
                    raise CampaignError("physical refinement replay must preserve the public-selected snapped cell")
                proposals = physical.get("public_refinement_proposals")
                if (not isinstance(proposals, dict) or proposals.get("not_admitted_evidence") is not True or
                        proposals.get("uses_physical_truth") is not False or not isinstance(proposals.get("proposals"), list) or
                        len(proposals["proposals"]) > 3):
                    raise CampaignError("continuous proposals must remain separate public-only diagnostics")
                if proposals.get("timing_index") != timing_index or proposals["proposals"] != public_row.get("proposals"):
                    raise CampaignError("physical proposal diagnostics must preserve the public search trace")
                for proposal in proposals["proposals"]:
                    if (not isinstance(proposal, dict) or any(not _is_finite_number(proposal.get(field)) for field in
                            ("a", "b", "nu_hz", "cfo_hz", "initial_log_lr", "continuous_log_lr",
                             "best_scored_neighbor_log_lr", "snapped_loss_nats"))):
                        raise CampaignError("physical refinement proposal trace must be finite")
                projection_groups.append(refined.get("projection"));replay_cells.append(refined)
            elif refinement_requested:
                if (not isinstance(physical.get("refinement_replay_reason"), str) or
                        any(value is not None for value in diagnostic["phase_refinement"]["new_bit_log_e"])):
                    raise CampaignError("unavailable physical refinement replay must preserve omitted public mass")
                if ("refined_public_cell" in physical or "public_refinement_proposals" in physical or
                        (not refinement_executed and physical["refinement_replay_reason"] != refinement_trace["unavailable_reason"])):
                    raise CampaignError("unavailable physical refinement replay must preserve the public planning reason without invented cells")
            for group in projection_groups:
                if (not isinstance(group, dict) or group.get("block_count") != 1 or
                        group.get("samples") != observation["stop"] - observation["first"] or
                        group.get("wanted_bit") != expected or group.get("gain_prior_variance") != 1 or
                        not _is_finite_number(group.get("noise_free_energy")) or group["noise_free_energy"] < 0):
                    raise CampaignError("physical diagnostic must report one full-aperture projection")
                hypotheses = group.get("hypotheses")
                if not isinstance(hypotheses, list) or len(hypotheses) != 2:
                    raise CampaignError("physical diagnostic needs both finite bit templates")
                for hypothesis in hypotheses:
                    if (not isinstance(hypothesis, dict) or not _is_nonnegative_int(hypothesis.get("rank")) or
                            hypothesis["rank"] > 1 or any(not _is_finite_number(hypothesis.get(field)) for field in
                            ("template_energy", "clean_projected_energy", "clean_residual_energy", "noisy_log_likelihood_ratio")) or
                            any(hypothesis[field] < 0 for field in ("template_energy", "clean_projected_energy", "clean_residual_energy"))):
                        raise CampaignError("physical diagnostic projection energies and LR must be finite")
                    coherence = hypothesis.get("clean_coherence_power")
                    if group["noise_free_energy"] > 0:
                        if not _is_finite_number(coherence) or not 0 <= coherence <= 1 + 1e-10:
                            raise CampaignError("physical diagnostic coherence must be normalized to clean signal energy")
                    elif coherence is not None:
                        raise CampaignError("zero-energy physical diagnostic coherence must be absent")
            self_group = physical["physical_templates"]
            if self_group["noise_free_energy"] > 0 and not math.isclose(
                    self_group["hypotheses"][expected]["clean_coherence_power"], 1, rel_tol=1e-10, abs_tol=1e-10):
                raise CampaignError("physical wanted template must have perfect clean self coherence")
            hashes = self_group.get("template_sha256")
            if not isinstance(hashes, list) or len(hashes) != 2 or any(
                    not isinstance(value, str) or re.fullmatch(r"[0-9a-fA-F]{64}", value) is None for value in hashes):
                raise CampaignError("physical diagnostic must bind both clean template hashes")
            for cell in replay_cells:
                reference = cell.get("search_maximum_single_cell_log_lr")
                difference = cell.get("single_cell_log_lr_replay_difference")
                if (not _is_finite_number(reference) or not _is_finite_number(difference) or
                        abs(difference) > 2e-9 * max(1, abs(reference))):
                    raise CampaignError("physical evaluator must reproduce the selected public-cell likelihood")
            admission = physical.get("admission")
            if (not isinstance(admission, dict) or admission.get("channel_instances") != 2 or
                    admission.get("clean_capture_buffers") != 0 or
                    admission.get("retained_public_result_in_process_baseline") is not True or
                    any(not _is_nonnegative_int(admission.get(field)) for field in
                        ("process_baseline_bytes", "total_memory_limit_bytes", "future_reserved_bytes")) or
                    admission["process_baseline_bytes"] + admission["future_reserved_bytes"] > admission["total_memory_limit_bytes"]):
                raise CampaignError("physical diagnostic must reserve bounded twin rendering and evaluator storage")
    elif episode["mode"] == "oracle":
        oracle = receipt.get("oracle")
        if not isinstance(oracle, dict) or not _is_finite_number(oracle.get("probability_error")):
            raise CampaignError("completed oracle receipt is missing finite probability_error")
        if not 0 <= oracle["probability_error"] <= 1:
            raise CampaignError("completed oracle probability_error must be in [0, 1]")
        if "oracle_slot" in episode:
            selected_slot = episode["oracle_slot"]
            plan_slot = receipt["plan"].get("oracle_slot")
            oracle_slot = oracle.get("slot_index")
            if (isinstance(plan_slot, bool) or not isinstance(plan_slot, int) or plan_slot != selected_slot or
                    isinstance(oracle_slot, bool) or not isinstance(oracle_slot, int) or oracle_slot != selected_slot):
                raise CampaignError("oracle receipt does not match the selected oracle_slot")
        reference_delay = episode.get("history_reference_delay_seconds", 0)
        if "history_reference_delay_seconds" in plan:
            value = plan["history_reference_delay_seconds"]
            if not _is_finite_number(value) or not math.isclose(float(value), float(reference_delay), rel_tol=1e-12, abs_tol=1e-15):
                raise CampaignError("HF diagnostic reference delay does not match the frozen episode")
        if "history_reference_delay_seconds" in episode or episode.get("hf_phase_bank", False):
            history = receipt.get("hf_history")
            if (receipt.get("hf_history_applicable") is not True or not isinstance(history, dict) or
                    history.get("truth_information_passed_to_receiver") is not False):
                raise CampaignError("HF diagnostic receipt must report a private history evaluator")
            for value in (plan.get("history_reference_delay_seconds"), history.get("reference_delay_seconds")):
                if not _is_finite_number(value) or not math.isclose(float(value), float(reference_delay), rel_tol=1e-12, abs_tol=1e-15):
                    raise CampaignError("HF diagnostic reference delay does not match the frozen episode")
        if plan.get("hf_phase_bank_requested", False) is not episode.get("hf_phase_bank", False):
            raise CampaignError("native phase-bank request does not match the frozen episode")
        if "hf_phase_bank" in receipt and not episode.get("hf_phase_bank", False):
            raise CampaignError("unrequested phase-bank execution in native receipt")
        if episode.get("hf_phase_bank", False):
            phase = receipt.get("hf_phase_bank")
            if (not isinstance(phase, dict) or not _is_nonnegative_int(phase.get("model_count")) or phase["model_count"] == 0 or
                    not _is_nonnegative_int(phase.get("samples")) or phase["samples"] == 0):
                raise CampaignError("requested phase bank must report executed models and samples")
            for flag in ("truth_information_passed_to_receiver", "truth_information_passed_to_phase_bank",
                         "receiver_executed", "completion_evaluated", "capacity_qualified"):
                if phase.get(flag) is not False:
                    raise CampaignError("phase bank must preserve private evaluator scope")
            value = phase.get("reference_delay_seconds")
            if not _is_finite_number(value) or not math.isclose(float(value), float(reference_delay), rel_tol=1e-12, abs_tol=1e-15):
                raise CampaignError("phase-bank reference delay does not match the frozen episode")


def _trial(probe: Path, episode: dict[str, Any], max_samples: int, timeout: float,
           directory: Path, index: int) -> dict[str, Any]:
    prefix = f"{index:03d}-{episode['id']}"
    stdout_path, stderr_path = directory / f"{prefix}.stdout.json", directory / f"{prefix}.stderr.txt"
    # Let the native deadline produce a receipt before the outer process limit
    # terminates the invocation. The outer bound remains authoritative.
    native_budget = max(timeout / 2, timeout - min(.5, timeout / 4))
    command = _command(probe, episode, max_samples, native_budget)
    started = time.monotonic()
    timed_out = False
    return_code: int | None = None
    def limit_output() -> None:
        if resource is not None:
            resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT_BYTES, MAX_OUTPUT_BYTES))

    try:
        with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
            try:
                options: dict[str, Any] = {}
                if os.name == "posix" and resource is not None:
                    options["preexec_fn"] = limit_output
                result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                        timeout=timeout, check=False, shell=False,
                                        start_new_session=(os.name == "posix"), **options)
                return_code = result.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
    except OSError as exc:
        return {"episode": episode, "command": command, "status": "launch_error",
                "error": str(exc), "elapsed_seconds": time.monotonic() - started,
                "raw_stdout": stdout_path.name if stdout_path.exists() else None,
                "raw_stderr": stderr_path.name if stderr_path.exists() else None}
    result: dict[str, Any] = {"episode": episode, "command": command,
                              "elapsed_seconds": time.monotonic() - started,
                              "return_code": return_code, "timed_out": timed_out,
                              "raw_stdout": stdout_path.name, "raw_stderr": stderr_path.name,
                              "stderr": _stderr(stderr_path)}
    if timed_out:
        result["status"] = "timed_out"
        return result
    try:
        receipt = _parse_receipt(stdout_path)
        result["receipt"] = receipt
        for field, expected in (("requested_scenario", episode["scenario"]),
                                ("mode", episode["mode"]), ("waveform", episode["waveform"]),
                                ("seed", episode["seed"]), ("control", episode.get("control", "wanted"))):
            if field in receipt and receipt[field] != expected:
                result["status"] = "receipt_input_mismatch"
                result["error"] = f"native receipt {field} does not match the frozen episode"
                return result
        if receipt["status"] == "completed":
            _validate_completed_receipt(receipt, episode)
        # A completed wanted component-render is expected to contain a signal.
        # Keep explicit native limits/errors intact (they may have no channel
        # report), and do not reject zero-energy negative-control receipts.
        if (episode["mode"] == "channel" and receipt["status"] == "completed" and
                receipt.get("control", "wanted") == "wanted"):
            channel = receipt.get("channel")
            if not isinstance(channel, dict) or channel.get("contains_signal_energy") is not True:
                result["status"] = "channel_empty"
                result["error"] = "channel episode reported no transmitted signal energy"
                return result
        expected_exit = {"completed": 0, "error": 2, "resource_limit": 3,
                         "time_limit": 4, "unavailable": 5}[receipt["status"]]
        result["status"] = receipt["status"] if return_code == expected_exit else "receipt_exit_mismatch"
    except (OSError, CampaignError) as exc:
        result["status"] = "invalid_receipt"
        result["error"] = str(exc)
    return result


def _binomial_cdf(k: int, n: int, p: float) -> float:
    if k < 0:
        return 0.0
    if k >= n:
        return 1.0
    if p <= 0:
        return 1.0
    if p >= 1:
        return 0.0
    terms = [math.lgamma(n + 1) - math.lgamma(i + 1) - math.lgamma(n - i + 1)
             + i * math.log(p) + (n - i) * math.log1p(-p)
             for i in range(k + 1)]
    peak = max(terms)
    return math.exp(peak) * sum(math.exp(x - peak) for x in terms)


def clopper_pearson(successes: int, trials: int, confidence: float = .95) -> tuple[float, float]:
    """Exact fixed-n binomial interval; it says nothing about model mismatch."""
    if trials < 1 or trials > 100 or not 0 <= successes <= trials or not 0 < confidence < 1:
        raise ValueError("requires 1<=trials<=100, 0<=successes<=trials, and 0<confidence<1")
    alpha = 1 - confidence
    if successes == 0:
        lower = 0.0
        upper = 1 - (alpha / 2) ** (1 / trials)
        return lower, upper
    if successes == trials:
        lower = (alpha / 2) ** (1 / trials)
        upper = 1.0
        return lower, upper
    if successes == 0:
        lower = 0.0
    else:
        lo, hi = 0.0, 1.0
        for _ in range(70):
            mid = (lo + hi) / 2
            # P_p[X >= successes] increases in p.
            tail = 1 - _binomial_cdf(successes - 1, trials, mid)
            if tail < alpha / 2:
                lo = mid
            else:
                hi = mid
        lower = (lo + hi) / 2
    lo, hi = 0.0, 1.0
    for _ in range(70):
        mid = (lo + hi) / 2
        if _binomial_cdf(successes, trials, mid) > alpha / 2:
            lo = mid
        else:
            hi = mid
    upper = (lo + hi) / 2
    return lower, upper


def _statistical_summary(campaign: dict[str, Any], results: list[dict[str, Any]]) -> dict[str, Any]:
    groups: dict[str, list[dict[str, Any]]] = {}
    for item in campaign.get("confirmatory_strata", {}):
        groups[item] = [r for r in results if r["episode"].get("stratum") == item]
    summary: dict[str, Any] = {}
    for stratum, group in groups.items():
        expected = campaign["confirmatory_strata"][stratum]
        eligible = [r for r in group if r.get("status") == "completed" and
                    r.get("receipt", {}).get("status") == "completed" and
                    r["episode"].get("mode") == "receive" and
                    r["episode"].get("control", "wanted") == "wanted"]
        outcomes = [r.get("receipt", {}).get("outcomes", {}).get(_communication_endpoint(r["episode"])) for r in eligible]
        if len(eligible) != expected or not all(isinstance(x, bool) for x in outcomes):
            summary[stratum] = {"complete_fixed_n": False, "planned_n": expected,
                                "usable_n": len(eligible), "interval": None,
                                "reason": "fixed sample incomplete or a required outcome is unavailable"}
            continue
        successes = sum(outcomes)
        low, high = clopper_pearson(successes, expected)
        summary[stratum] = {"complete_fixed_n": True, "population": "the declared synthetic episode generator only",
                            "successes": successes, "trials": expected,
                            "communication_endpoint": _communication_endpoint(eligible[0]["episode"]),
                            "confidence": .95, "clopper_pearson": [low, high],
                            "iid_assumption": "Episodes must be independently sampled from the declared generator; distinct seeds alone do not establish independence.",
                            "scope_limit": "No natural-condition probability, model validation, acquisition, or capacity claim."}
    return summary


def _markdown(campaign: dict[str, Any], run: dict[str, Any]) -> str:
    rows = ["# Bounded appraisal campaign", "", f"Campaign: `{campaign['name']}`  ",
            f"Purpose: {campaign.get('purpose', 'not specified')}  ",
            f"Campaign hash: `{run['campaign_sha256']}`  ",
            f"Probe SHA-256: `{run['probe_sha256']}`  ",
            f"Executed: {run['executed_trials']} / {len(campaign['episodes'])}; elapsed: {run['elapsed_seconds']:.3f} s  ",
            f"Execution failures: {run['execution_failures']}; valid incomplete communications: {run['valid_communication_failures']}  ",
            f"Run status: **{run['status']}**; stop reason: {run['stop_reason'] or 'all declared episodes completed'}", "",
            "Every native receipt is one declared finite episode. Different physical models and channel strata are not pooled.",
            "Communication failures are observations, not runner failures. An incomplete fixed sample has no confidence interval.",
            "With FEC off, communication success uses the unchanged raw-clean `correct_complete` endpoint. Explicit rs20/rs60 episodes use `correct_decoded_complete`: exact decoded bytes and strict physical framing may include native FEC repairs. Raw slot errors and the raw-clean endpoint remain separate.", "",
            "| Episode | Scenario | Mode | Control | Trial status | Native status | Observation outcome | Slot decision | Correct DATA | Receive physical/premature/false/resumed | Wall (s) |",
            "|---|---|---|---|---|---|---|---|---|---|---:|"]
    for trial in run["trials"]:
        receipt = trial.get("receipt", {})
        outcomes = receipt.get("outcomes", {})
        episode = trial["episode"]
        native_status = receipt.get("status", "unavailable")
        slot_decision = "—"
        correct_data = "—"
        receive_counts = "—"
        if episode["mode"] == "receive" and isinstance(outcomes, dict):
            count_fields = ("physical_completions", "premature_completions", "false_data", "resumed_data_slots")
            if all(_is_nonnegative_int(outcomes.get(field)) for field in count_fields):
                receive_counts = "/".join(str(outcomes[field]) for field in count_fields)
        if trial.get("status") != "completed" or native_status != "completed":
            censor_reason = (trial.get("status") if trial.get("status") != "completed" else native_status)
            outcome = f"censored ({censor_reason})"
            if episode["mode"] == "slot":
                slot_decision = "censored"
                correct_data = "censored"
        elif episode["mode"] == "slot":
            score = (receipt.get("slots") or [{}])[0]
            slot_decision = score.get("decision", "unavailable")
            correct_data = "yes" if outcomes.get("first_public_observation_correct_data") else "no"
            outcome = "one public slot; message completion not evaluated"
        elif episode["mode"] == "receive":
            if episode.get("fec", "off") != "off":
                outcome = ("decoded physical message complete" if outcomes.get("correct_decoded_complete") is True else
                           "no correct decoded physical message completion")
                outcome += f" ({episode['fec']}; raw-clean={'yes' if outcomes.get('correct_complete') else 'no'})"
            else:
                outcome = ("physical message complete" if outcomes.get("correct_complete") is True else
                           "no correct physical message completion")
        elif episode["mode"] == "channel":
            outcome = "signal present" if receipt.get("channel", {}).get("contains_signal_energy") else "signal absent"
        elif episode["mode"] == "oracle":
            error_probability = receipt.get("oracle", {}).get("probability_error")
            outcome = f"oracle P(error)={error_probability:.6g}" if isinstance(error_probability, (int, float)) else "oracle bound unavailable"
        elif episode["mode"] == "diagnose":
            outcome = "component complete; no slot decision"
        elif episode["mode"] == "hf-search":
            outcome = "public shadow search complete; no receiver decision"
        else:
            outcome = "plan only"
        rows.append(f"| {trial['episode']['id']} | {trial['episode']['scenario']} | {trial['episode']['mode']} | "
                    f"{episode.get('control', '—')} | {trial.get('status')} | {native_status} | {outcome} | "
                    f"{slot_decision} | {correct_data} | {receive_counts} | "
                    f"{trial.get('elapsed_seconds', 0):.3f} |")
    rows.extend(["", "## Fixed-sample estimates", ""])
    if run["statistics"]:
        for key, value in run["statistics"].items():
            rows.append(f"- `{key}`: `{json.dumps(value, sort_keys=True)}`")
    else:
        rows.append("No confirmatory fixed-sample stratum was declared; pilot outcomes are descriptive.")
    rows.extend(["", "## Channel and receiver evidence", "",
                 "Raw native JSON receipts and per-episode stderr are stored beside this report. Consult each "
                 "receipt's scenario provenance, input/received energy, public bounds, actual stage work and backend "
                 "execution fields before comparing outcomes. This campaign summary does not pool those fields across "
                 "families or infer a universal sensitivity or computation curve."])
    rows.extend(["", "## Limits", "", "This report does not establish real-world occurrence rates, physical validation, "
                 "unknown-epoch/ordinal acquisition, real-time capacity, GPU speedup, or universal sensitivity. "
                 "Wall time includes source/channel/receiver work in the native invocation and is not a CPU ceiling measurement.", ""])
    return "\n".join(rows)


def run_campaign(probe: Path, campaign_path: Path, output_dir: Path) -> dict[str, Any]:
    campaign = _read_campaign(campaign_path)
    probe = probe.resolve(strict=True)
    if not probe.is_file() or not os.access(probe, os.X_OK):
        raise CampaignError("--probe must be an executable native appraisal binary")
    output_dir = output_dir.resolve()
    allowed_root = (HERE / "local-evidence").resolve()
    if not output_dir.is_relative_to(allowed_root):
        raise CampaignError(f"output directory must be under {allowed_root}")
    output_dir.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    budget = campaign["max_wall_seconds"]
    probe_hash = _sha256(probe)
    campaign_hash = hashlib.sha256(_json_bytes(campaign)).hexdigest()
    frozen = {"campaign": campaign, "campaign_sha256": campaign_hash,
              "probe": str(probe), "probe_sha256": probe_hash,
              "probe_size_bytes": probe.stat().st_size,
              "runtime": {"python": sys.version, "system": sys.platform, "os_name": os.name,
                          "machine": platform.machine(), "release": os.uname().release if hasattr(os, "uname") else None},
              "output_limit_bytes_per_stream": MAX_OUTPUT_BYTES,
              "statistical_rule": "Only the explicitly declared fixed-n strata receive exact binomial intervals."}
    (output_dir / "frozen-input.json").write_bytes(_json_bytes(frozen))
    trials: list[dict[str, Any]] = []
    status = "completed"
    stop_reason: str | None = None
    for index, episode in enumerate(campaign["episodes"]):
        remaining = budget - (time.monotonic() - started)
        if remaining <= 0:
            status = "incomplete_checks"
            stop_reason = "campaign_budget_exhausted"
            break
        try:
            current_probe_hash = _sha256(probe)
        except OSError:
            current_probe_hash = None
        if current_probe_hash != probe_hash:
            status = "incomplete_checks"
            stop_reason = "probe_changed"
            break
        retained_before = sum(path.stat().st_size for path in output_dir.iterdir()
                              if path.is_file() and path.name not in {"frozen-input.json", "run.json", "report.md"})
        if retained_before + 2 * MAX_OUTPUT_BYTES > MAX_CAMPAIGN_OUTPUT_BYTES:
            status = "incomplete_checks"
            stop_reason = "campaign_output_budget_exhausted"
            break
        timeout = min(float(campaign["per_trial_wall_seconds"]),
                      float(episode.get("max_wall_seconds", campaign["per_trial_wall_seconds"])), remaining)
        result = _trial(probe, episode, episode.get("max_samples", campaign["max_samples"]),
                        timeout, output_dir, index)
        trials.append(result)
        try:
            current_probe_hash = _sha256(probe)
        except OSError:
            current_probe_hash = None
        if current_probe_hash != probe_hash:
            # The just-finished result cannot be bound to the frozen executable
            # identity. Mark it unusable so it cannot contribute to fixed-n stats.
            result["status"] = "probe_changed"
            result["error"] = "probe hash changed during or immediately after this episode"
            result["probe_sha256_after"] = current_probe_hash
            status = "incomplete_checks"
            stop_reason = "probe_changed"
            break
        if result["status"] == "timed_out":
            status = "incomplete_checks"
            stop_reason = "trial_timeout"
            # A timed-out native process is not a receiver failure; continue only
            # with already frozen independent episodes and remaining wall budget.
            if time.monotonic() - started >= budget:
                status = "incomplete_checks"
                stop_reason = "campaign_budget_exhausted"
                break
        elif result["status"] in {"launch_error", "invalid_receipt", "receipt_input_mismatch"}:
            status = "incomplete_checks"
            stop_reason = result["status"]
            break
        elif result["status"] != "completed":
            status = "incomplete_checks"
            stop_reason = stop_reason or result["status"]
    stats = _statistical_summary(campaign, trials)
    if status == "completed" and len(trials) != len(campaign["episodes"]):
        status = "incomplete_checks"
    run = {"schema_version": 1, "status": status, "stop_reason": stop_reason,
           "executed_trials": len(trials),
           "planned_trials": len(campaign["episodes"]), "elapsed_seconds": time.monotonic() - started,
           "campaign_sha256": campaign_hash, "probe_sha256": probe_hash,
           "execution_failures": sum(trial.get("status") != "completed" for trial in trials),
           "valid_communication_failures": sum(
               trial.get("status") == "completed" and
               trial.get("receipt", {}).get("status") == "completed" and
               trial.get("episode", {}).get("mode") == "receive" and
               trial.get("episode", {}).get("control", "wanted") == "wanted" and
               trial.get("receipt", {}).get("outcomes", {}).get(_communication_endpoint(trial.get("episode", {}))) is False
               for trial in trials),
           "retained_output_bytes": sum(path.stat().st_size for path in output_dir.iterdir() if path.is_file()),
           "retained_output_limit_bytes": MAX_CAMPAIGN_OUTPUT_BYTES,
           "statistics": stats, "trials": trials}
    # The full native receipt remains in its raw stdout file. Keep run.json
    # compact so campaign metadata does not duplicate large native receipts.
    persisted = dict(run)
    persisted["trials"] = []
    for trial in trials:
        item = dict(trial)
        item.pop("stderr", None)  # Full bounded stderr is retained in its sidecar file.
        receipt = item.pop("receipt", None)
        if isinstance(receipt, dict):
            item["receipt_status"] = receipt.get("status")
            item["outcomes"] = receipt.get("outcomes")
        persisted["trials"].append(item)
    (output_dir / "run.json").write_bytes(_json_bytes(persisted))
    (output_dir / "report.md").write_text(_markdown(campaign, run), encoding="utf-8")
    return run


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--campaign", required=True, type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args(argv)
    output = args.output_dir or (HERE / "local-evidence" /
                                 (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]) /
                                 Path(args.campaign).stem)
    try:
        result = run_campaign(args.probe, args.campaign, output)
    except (CampaignError, OSError) as exc:
        print(f"campaign setup error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({key: result[key] for key in
                      ("status", "executed_trials", "planned_trials", "execution_failures",
                       "valid_communication_failures", "stop_reason", "elapsed_seconds", "statistics")},
                     sort_keys=True))
    return 0 if result["status"] == "completed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

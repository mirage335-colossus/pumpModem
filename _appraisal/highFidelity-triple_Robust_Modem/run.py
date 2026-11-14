#!/usr/bin/env python3
"""Bounded orchestration and reporting for the native high-fidelity appraisal.

This module contains no waveform, channel, or receiver DSP. Every episode is a
single invocation of the canonical native appraisal executable.
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
                "helper", "backend", "bit_seconds", "duration_seconds", "stratum"}
MODES = {"plan", "channel", "receive", "oracle"}
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
        _validate_episode(episode, index, ids)
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
                            "backend", "bit_seconds", "duration_seconds")
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


def _validate_episode(item: Any, index: int, ids: set[str]) -> None:
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
    if "helper" in item and not isinstance(item["helper"], bool):
        raise CampaignError(f"episodes[{index}].helper must be boolean")
    if "stratum" in item:
        _nonempty_str(item["stratum"], f"episodes[{index}].stratum")
    for field in ("id", "scenario"):
        if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", item[field]) is None:
            raise CampaignError(f"episodes[{index}].{field} must be a simple identifier")
    mode = item["mode"]
    control = item.get("control", "wanted")
    if mode != "receive" and "control" in item:
        raise CampaignError(f"episodes[{index}]: control is valid only for receive mode")
    if mode == "receive" and control not in CONTROLS:
        raise CampaignError(f"episodes[{index}]: invalid receive control")


def _command(probe: Path, episode: dict[str, Any], max_samples: int, max_wall_seconds: float) -> list[str]:
    command = [str(probe), "--mode", episode["mode"], "--scenario", episode["scenario"],
               "--waveform", episode["waveform"], "--seed", str(episode["seed"]),
               "--max-samples", str(max_samples), "--max-wall-seconds", str(max_wall_seconds)]
    for key, option in (("cn0_db_hz", "--cn0"), ("control", "--control"), ("helper", "--helper"),
                        ("backend", "--backend"), ("bit_seconds", "--bit-seconds"),
                        ("duration_seconds", "--duration-seconds")):
        if key in episode:
            value = episode[key]
            if key == "helper":
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
    for field in ("cn0_db_hz", "requested_bit_seconds"):
        if not _is_finite_number(plan.get(field)):
            raise CampaignError(f"completed receipt plan.{field} must be finite")
    if plan["requested_bit_seconds"] <= 0:
        raise CampaignError("completed receipt plan.requested_bit_seconds must be positive")
    if "cn0_db_hz" in episode and not math.isclose(
            float(plan["cn0_db_hz"]), float(episode["cn0_db_hz"]), rel_tol=1e-9, abs_tol=1e-9):
        raise CampaignError("native plan C/N0 does not match the frozen episode")
    if "bit_seconds" in episode and not math.isclose(
            float(plan["requested_bit_seconds"]), float(episode["bit_seconds"]), rel_tol=1e-9, abs_tol=1e-9):
        raise CampaignError("native plan bit duration does not match the frozen episode")
    if plan.get("helper_enabled") is not episode.get("helper", False):
        raise CampaignError("native plan Helper setting does not match the frozen episode")

    backend = receipt.get("backend")
    if not isinstance(backend, dict):
        raise CampaignError("completed receipt is missing backend")
    requested_backend = episode.get("backend", "cpu")
    if backend.get("requested") != requested_backend:
        raise CampaignError("native requested backend does not match the frozen episode")
    device = backend.get("device")
    if not isinstance(backend.get("executed_gpu_work"), bool):
        raise CampaignError("completed receipt backend.executed_gpu_work must be boolean")
    if episode["mode"] in {"receive", "channel"}:
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
        if not isinstance(outcomes.get("correct_complete"), bool):
            raise CampaignError("completed receive receipt is missing boolean outcomes.correct_complete")
        if not _is_nonnegative_int(outcomes.get("physical_completions")):
            raise CampaignError("completed receive receipt is missing nonnegative physical_completions")
    elif episode["mode"] == "channel":
        channel = receipt.get("channel")
        if not isinstance(channel, dict) or not isinstance(channel.get("contains_signal_energy"), bool):
            raise CampaignError("completed channel receipt is missing boolean channel energy result")
        if not _is_nonnegative_int(channel.get("samples")):
            raise CampaignError("completed channel receipt is missing nonnegative channel.samples")
    elif episode["mode"] == "oracle":
        oracle = receipt.get("oracle")
        if not isinstance(oracle, dict) or not _is_finite_number(oracle.get("probability_error")):
            raise CampaignError("completed oracle receipt is missing finite probability_error")
        if not 0 <= oracle["probability_error"] <= 1:
            raise CampaignError("completed oracle probability_error must be in [0, 1]")


def _trial(probe: Path, episode: dict[str, Any], max_samples: int, timeout: float,
           directory: Path, index: int) -> dict[str, Any]:
    prefix = f"{index:03d}-{episode['id']}"
    stdout_path, stderr_path = directory / f"{prefix}.stdout.json", directory / f"{prefix}.stderr.txt"
    # Let the native deadline produce a receipt before the outer process limit
    # terminates the invocation. The outer bound remains authoritative.
    native_budget = max(.001, timeout - min(.5, timeout / 4))
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
        outcomes = [r.get("receipt", {}).get("outcomes", {}).get("correct_complete") for r in eligible]
        if len(eligible) != expected or not all(isinstance(x, bool) for x in outcomes):
            summary[stratum] = {"complete_fixed_n": False, "planned_n": expected,
                                "usable_n": len(eligible), "interval": None,
                                "reason": "fixed sample incomplete or a required outcome is unavailable"}
            continue
        successes = sum(outcomes)
        low, high = clopper_pearson(successes, expected)
        summary[stratum] = {"complete_fixed_n": True, "population": "the declared synthetic episode generator only",
                            "successes": successes, "trials": expected,
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
            "Communication failures are observations, not runner failures. An incomplete fixed sample has no confidence interval.", "",
            "| Episode | Scenario | Mode | Control | Native status | Outcome | Wall (s) |", "|---|---|---|---|---|---|---:|"]
    for trial in run["trials"]:
        receipt = trial.get("receipt", {})
        outcomes = receipt.get("outcomes", {})
        outcome = ("complete" if outcomes.get("correct_complete") is True else
                   "incomplete" if outcomes.get("correct_complete") is False else "not reported")
        rows.append(f"| {trial['episode']['id']} | {trial['episode']['scenario']} | {trial['episode']['mode']} | "
                    f"{trial['episode'].get('control', '—')} | {trial.get('status')} | {outcome} | "
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
        timeout = min(float(campaign["per_trial_wall_seconds"]), remaining)
        result = _trial(probe, episode, campaign["max_samples"], timeout, output_dir, index)
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
               trial.get("receipt", {}).get("outcomes", {}).get("correct_complete") is False
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

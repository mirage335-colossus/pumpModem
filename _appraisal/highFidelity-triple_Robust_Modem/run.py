#!/usr/bin/env python3
"""Bounded orchestration and reporting for the native high-fidelity appraisal.

This module contains no waveform, channel, or receiver DSP. Every episode is a
single invocation of the canonical native appraisal executable. Slot episodes
report one first public observation without message completion; diagnose
episodes report bounded VLF acquisition and abbreviated fine components without
bit decisions or complete receiver service. Oracle episodes report a separate
known-waveform bound, not a receiver communication outcome. HF-search episodes
report a shadow detector with public acquisition priors and no receiver decisions.
GUI-varying episodes compare canonical affine and short varying components on
one complete observation from the original GUI capture, without receiver output.
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
import selectors
import signal
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
GUI_DEFAULTS = {
    "gui_spread": "hf-worst", "gui_selective": "each-65", "gui_arrival": "tight",
    "gui_margin": 3, "gui_audio_ppm": 100, "gui_rf_ppm": 1,
    "gui_audio_phase_degrees_sqrt_s": .5, "gui_rf_phase_degrees_sqrt_s": .05,
    "gui_target_cn0_db_hz": 32, "gui_clock_edge": False,
}
GUI_NUMERIC_BOUNDS = {
    "gui_margin": (1, 1000), "gui_audio_ppm": (0, 100000), "gui_rf_ppm": (0, 100000),
    "gui_audio_phase_degrees_sqrt_s": (0, 360), "gui_rf_phase_degrees_sqrt_s": (0, 360),
    "gui_target_cn0_db_hz": (-200, 200),
}
GUI_SPREAD_DOPPLER = {"none": 0, "hf-best": .1, "hf-moderate": 10, "hf-worst": 30,
                      "aurora-best": 10, "aurora-worst": 55}
GUI_SELECTIVE_DEPTHS = {"off": (0., 0.), **{
    f"{kind}-{percent}": (percent / 100 if kind != "fast" else 0., percent / 100 if kind != "slow" else 0.)
    for kind in ("slow", "fast", "each") for percent in (1, 5, 10, 25, 65)}}
HF_DESIGN_BOUNDS = {
    "primary_amplitude_hz": (0, 20000), "primary_period_seconds": (110, 480),
    "primary_phase_radians": (-math.pi, math.pi),
    "secondary_amplitude_hz": (0, 20000), "secondary_period_seconds": (480, 1400),
    "secondary_phase_radians": (-math.pi, math.pi),
    "knot_amplitude_hz": (0, 20000), "knot_seconds": (70, 200),
}
HF_DESIGN_FIELDS = {"hf_" + field for field in HF_DESIGN_BOUNDS}
FULLWAVE_REFLECTION = {
    "port": "reflection", "carrier_hz": 3400000, "sample_rate_hz": 1920, "occupied_half_band_hz": 768,
    "transition_first_seconds": 60, "transition_seconds": 120, "minimum_source_seconds": 540,
    "interpolation": "phase_factored_output_time_half_cosine", "modelling_delay_samples": 32,
    "taps": 129, "first_lag_samples": 1, "last_lag_samples": 129, "filter_halo_samples": 128,
    "maximum_rf_solves": 32070, "preparation_workspace_bound_bytes": 1048576,
}
MAGNETOIONIC_REFLECTION = {
    "hf-magnetoionic-x": (0, 60, 600, 0.),
    "hf-magnetoionic-y": (1, 60, 600, 0.),
    "hf-magnetoionic-x-holdout": (0, 180, 780, 0.),
    "hf-magnetoionic-x-spread": (0, 60, 600, .007),
    "hf-magnetoionic-parallel-x": (0, 60, 600, 0.),
    "hf-magnetoionic-parallel-y": (1, 60, 600, 0.),
    "hf-magnetoionic-parallel-x-holdout": (0, 180, 780, 0.),
}
MAGNETOIONIC_DESCRIPTOR = {
    "port": "reflection", "carrier_hz": 3400000, "sample_rate_hz": 1920,
    "occupied_half_band_hz": 384, "normal_incidence": True,
    "magnetic_field_tesla": [25e-6, 0., 43.30127018922193e-6],
    "transmit_polarization": "fixed_laboratory_x",
    "orthogonal_field_supplied_to_receiver": False,
    "profile_q0": {"f_collision_per_second": 60., "d_peak_electron_density_per_m3": 8e8},
    "profile_q1": {"f_collision_per_second": 60., "d_peak_electron_density_per_m3": 8e8},
    "interpolation": "fixed_lab_cubic_hermite_output_time_half_cosine",
    "modelling_delay_samples": 32, "taps_per_output": 81,
    "first_lag_samples": 1, "last_lag_samples": 81,
    "filter_halo_samples": 80, "profile_intervals": 16, "frequency_bins": 128,
    "maximum_rf_solves": 3444, "production_step_m": 10., "witness_step_m": 20.,
    "complex_error_tolerance": .005, "discarded_energy_tolerance": 1e-6,
    "rate_peak_tolerance": .005, "preparation_workspace_bound_bytes": 2097152,
}
GANNON_REPLAY = {"hf-gannon-1700": (17, 0.), "hf-gannon-2000": (20, 0.),
                 "hf-gannon-2000-late": (20, 1800.)}
GANNON_RECORDS = {
    17: (1715360400, "8397dc68dcae74222252ddc93162e331a96931d12db9a9e656af031c361dffc5",
         "2c56dd525dde8e58e3d78add0e8e9e5c08eddb28473748358aa38c42d30af8d0", 165231037),
    20: (1715371200, "5b9e38b1319cb172d1c6c161b28960e10607d69b951593cd1700df91517173c7",
         "c23ab7e470eaaeb1b501c397ac3552ea04e13cec13a07e497e1c1b41e7cb098b", 2473256894),
}
SERVICE_FIELDS = {"service_replay", "service_episodes", "service_spool_byte_limit", "owners",
                  "service_backlog_begin_slot", "service_backlog_end_slot"}
EPISODE_KEYS = {"id", "scenario", "mode", "waveform", "seed", "cn0_db_hz", "control",
                "helper", "backend", "bit_seconds", "duration_seconds", "pulse", "stratum",
                "oracle_slot", "search_slot", "search_timing", "capture_digest_slot", "chunk", "message", "max_wall_seconds", "max_samples",
                "history_reference_delay_seconds", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver", "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models", "hf_gui_clean_diagnostic", "fec", "fade_slots", "capture_lead_seconds",
                "hf_trajectory_seed", "hf_trajectory_offset_seconds", "noise_seed"} | GUI_DEFAULTS.keys() | HF_DESIGN_FIELDS | SERVICE_FIELDS
MODES = {"plan", "channel", "receive", "slot", "oracle", "diagnose", "hf-search", "gui-varying"}
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
                len({e["seed"] for e in matched}) != count or
                len({e.get("noise_seed", e["seed"]) for e in matched}) != count):
            raise CampaignError(f"stratum {name!r} must contain exactly {count} wanted receive episodes with distinct seeds")
        invariant_fields = ("scenario", "mode", "waveform", "cn0_db_hz", "control", "helper",
                            "backend", "bit_seconds", "duration_seconds", "pulse", "oracle_slot", "search_slot", "search_timing", "capture_digest_slot", "chunk",
                            "message", "max_wall_seconds", "max_samples", "history_reference_delay_seconds", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver", "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models", "hf_gui_clean_diagnostic", "fec", "fade_slots", "capture_lead_seconds",
                            "hf_trajectory_seed", "hf_trajectory_offset_seconds") + tuple(sorted(GUI_DEFAULTS.keys() | HF_DESIGN_FIELDS | SERVICE_FIELDS))
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
    gui_fields = GUI_DEFAULTS.keys() & item.keys()
    if gui_fields and item["scenario"] != "hf-gui-native":
        raise CampaignError(f"episodes[{index}]: GUI controls require hf-gui-native")
    for field, choices in (("gui_spread", GUI_SPREAD_DOPPLER.keys()),
                           ("gui_selective", GUI_SELECTIVE_DEPTHS.keys()),
                           ("gui_arrival", {"tight", "default"})):
        if field in item and (not isinstance(item[field], str) or item[field] not in choices):
            raise CampaignError(f"episodes[{index}].{field} must be one of {sorted(choices)}")
    for field, (low, high) in GUI_NUMERIC_BOUNDS.items():
        if field in item:
            _bounded_number(item[field], f"episodes[{index}].{field}", low, high)
    if "gui_clock_edge" in item and not isinstance(item["gui_clock_edge"], bool):
        raise CampaignError(f"episodes[{index}].gui_clock_edge must be boolean")
    design_fields = HF_DESIGN_FIELDS & item.keys()
    if item["scenario"] == "hf-ensemble-design":
        if design_fields != HF_DESIGN_FIELDS:
            raise CampaignError(f"episodes[{index}]: hf-ensemble-design requires all eight physical parameters")
    elif design_fields:
        raise CampaignError(f"episodes[{index}]: physical design controls require hf-ensemble-design")
    for field, (low, high) in HF_DESIGN_BOUNDS.items():
        if "hf_" + field in item:
            _bounded_number(item["hf_" + field], f"episodes[{index}].hf_{field}", low, high)
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
    for field in ("hf_trajectory_seed", "noise_seed"):
        if field in item and (not _is_nonnegative_int(item[field]) or item[field] >= 2**64):
            raise CampaignError(f"episodes[{index}].{field} must be an unsigned 64-bit integer")
    if "hf_trajectory_offset_seconds" in item:
        _bounded_number(item["hf_trajectory_offset_seconds"],
                        f"episodes[{index}].hf_trajectory_offset_seconds", -86400, 86400)
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
    for field in ("hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver", "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models", "hf_gui_clean_diagnostic"):
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
    if "service_replay" in item and not isinstance(item["service_replay"], bool):
        raise CampaignError(f"episodes[{index}].service_replay must be boolean")
    service_requested = item.get("service_replay", False)
    if (SERVICE_FIELDS - {"service_replay"}) & item.keys() and not service_requested:
        raise CampaignError(f"episodes[{index}]: service options require service_replay")
    if "service_episodes" in item:
        _bounded_number(item["service_episodes"], f"episodes[{index}].service_episodes", 1, 2, integer=True)
    if "owners" in item:
        _bounded_number(item["owners"], f"episodes[{index}].owners", 1, 65536, integer=True)
    if "service_spool_byte_limit" in item:
        _bounded_number(item["service_spool_byte_limit"], f"episodes[{index}].service_spool_byte_limit", 1, 2**64 - 1, integer=True)
    for field in ("service_backlog_begin_slot", "service_backlog_end_slot"):
        if field in item:
            _bounded_number(item[field], f"episodes[{index}].{field}", 0, 2**64 - 1, integer=True)
    backlog_fields = {"service_backlog_begin_slot", "service_backlog_end_slot"} & item.keys()
    if backlog_fields and (len(backlog_fields) != 2 or
            item["service_backlog_begin_slot"] >= item["service_backlog_end_slot"]):
        raise CampaignError(f"episodes[{index}]: service backlog requires paired begin < end slots")
    if "stratum" in item:
        _nonempty_str(item["stratum"], f"episodes[{index}].stratum")
    for field in ("id", "scenario"):
        if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", item[field]) is None:
            raise CampaignError(f"episodes[{index}].{field} must be a simple identifier")
    mode = item["mode"]
    control = item.get("control", "wanted")
    if service_requested:
        if mode not in {"receive", "plan"} or item.get("chunk", 4096) > 4096:
            raise CampaignError(f"episodes[{index}]: service replay requires receive/plan and chunks no larger than 4096")
        if item.get("service_episodes", 1) == 2 and control not in {"wanted", "noise", "wrong-key", "wrong-ordinal"}:
            raise CampaignError(f"episodes[{index}]: two service episodes require wanted/noise/wrong-key/wrong-ordinal")
    if item["scenario"] == "hf-gui-native":
        if (mode not in {"plan", "slot", "receive", "gui-varying"} or item["waveform"] != "reference" or
                item.get("pulse", "rrc") != "rrc" or
                control not in {"wanted", "noise", "wrong-key", "wrong-ordinal"}):
            raise CampaignError(f"episodes[{index}]: native GUI requires reference RRC plan/slot/receive/gui-varying and a supported control")
        forbidden = {"hf_trajectory_seed", "hf_trajectory_offset_seconds", "capture_lead_seconds",
                     "history_reference_delay_seconds", "hf_phase_bank", "hf_public_phase_receiver",
                     "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver", "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "duration_seconds"} & item.keys()
        if forbidden:
            raise CampaignError(f"episodes[{index}]: native GUI does not accept custom channel or phase controls: {', '.join(sorted(forbidden))}")
    if item["scenario"] == "hf-fullwave-reflection":
        if (mode not in {"plan", "channel", "receive"} or item["waveform"] != "reference" or
                item.get("backend", "cpu") != "cpu" or item.get("pulse", "rrc") != "rrc" or
                item.get("helper", False) or control not in {"wanted", "noise"} or item.get("fec", "off") != "off" or
                any(item.get(field, False) for field in ("service_replay", "hf_phase_bank", "hf_public_phase_receiver",
                    "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver",
                    "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models",
                    "hf_gui_clean_diagnostic")) or
                {"duration_seconds", "capture_lead_seconds", "hf_trajectory_seed", "hf_trajectory_offset_seconds"} & item.keys()):
            raise CampaignError(f"episodes[{index}]: full-wave reflection requires reference RRC CPU plan/channel/receive, wanted/noise Helper/FEC off and default capture geometry")
    if item["scenario"] in MAGNETOIONIC_REFLECTION:
        if (mode not in {"plan", "channel", "receive", "oracle"} or item["waveform"] != "reference" or
                item.get("backend", "cpu") != "cpu" or item.get("pulse", "rrc") != "rrc" or
                item.get("helper", False) or control not in {"wanted", "noise", "wrong-key"} or
                mode != "receive" and control != "wanted" or item.get("fec", "off") != "off" or
                any(item.get(field, False) for field in ("service_replay", "hf_phase_bank", "hf_public_phase_receiver",
                    "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver",
                    "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models",
                    "hf_gui_clean_diagnostic")) or
                {"duration_seconds", "capture_lead_seconds", "hf_trajectory_seed", "hf_trajectory_offset_seconds"} & item.keys()):
            raise CampaignError(f"episodes[{index}]: magnetoionic reflection requires reference RRC CPU plan/channel/receive or wanted-only oracle, wanted or receive-only noise/wrong-key, Helper/FEC off and fixed geometry")
    if item["scenario"] in GANNON_REPLAY:
        if (mode not in {"plan", "channel", "receive", "oracle"} or
                control != "wanted" and (mode not in {"channel", "receive"} or control not in {"noise", "wrong-key"}) or
                any(item.get(field, False) for field in ("service_replay", "hf_public_phase_receiver", "hf_compare_admitted_models"))):
            raise CampaignError(f"episodes[{index}]: Gannon replay requires plan/channel/receive or wanted-only oracle without service or experimental receiver policies")
    if "fade_slots" in item and control != "interior-fade":
        raise CampaignError(f"episodes[{index}]: fade_slots is valid only for interior-fade control")
    gannon_control = item["scenario"] in GANNON_REPLAY and mode in {"channel", "oracle"}
    magnetic_oracle_control = item["scenario"] in MAGNETOIONIC_REFLECTION and mode == "oracle" and control == "wanted"
    if mode not in {"receive", "slot", "diagnose", "hf-search", "gui-varying"} and "control" in item and not service_requested and not gannon_control and not magnetic_oracle_control:
        raise CampaignError(f"episodes[{index}]: control is valid only for receiver or diagnostic mode")
    if mode in {"receive", "slot", "diagnose", "hf-search", "gui-varying"} and control not in CONTROLS:
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
    if mode == "gui-varying":
        if (item["scenario"] != "hf-gui-native" or control not in {"wanted", "noise", "wrong-key"} or
                item.get("helper", False) or item.get("backend", "cpu") != "cpu" or
                "capture_digest_slot" not in item):
            raise CampaignError(f"episodes[{index}]: gui-varying requires native GUI, wanted/noise/wrong-key, Helper-off CPU and explicit capture_digest_slot")
    if "hf_gui_clean_diagnostic" in item and mode != "gui-varying":
        raise CampaignError(f"episodes[{index}]: clean GUI diagnostic requires gui-varying component mode")
    if item.get("hf_phase_refinement", False) and not (mode == "hf-search" or
            (mode in {"receive", "slot"} and item.get("hf_public_phase_receiver", False))):
        raise CampaignError(f"episodes[{index}]: phase refinement requires hf-search or the public phase receiver")
    if "hf_phase_filter" in item and (mode != "hf-search" or not item.get("hf_phase_refinement", False) or
            item.get("search_timing", "selected") != "full"):
        raise CampaignError(f"episodes[{index}]: phase filter requires full-timing hf-search with phase refinement")
    if "hf_phase_filter_receiver" in item and (mode != "receive" or not item.get("hf_public_phase_receiver", False) or
            not item.get("hf_phase_refinement", False) or item.get("hf_compare_admitted_models", False)):
        raise CampaignError(f"episodes[{index}]: receiver phase filter requires receive with public phase refinement and no admitted-model comparison")
    if "hf_phase_filter_paths" in item and (mode != "hf-search" or not item.get("hf_phase_filter", False) or
            not item.get("hf_physical_diagnostic", False)):
        raise CampaignError(f"episodes[{index}]: private phase paths require the phase filter and physical hf-search diagnostic")
    if "hf_phase_filter_sticky" in item and (not (item.get("hf_phase_filter", False) or item.get("hf_phase_filter_receiver", False)) or
            (item["hf_phase_filter_sticky"] and item.get("hf_phase_filter_paths", False))):
        raise CampaignError(f"episodes[{index}]: sticky phase prior requires a shadow/receiver phase filter and excludes private path diagnostics")
    if "hf_phase_bridge" in item and (not (item.get("hf_phase_filter", False) or item.get("hf_phase_filter_receiver", False)) or
            (item["hf_phase_bridge"] and item.get("hf_phase_filter_paths", False))):
        raise CampaignError(f"episodes[{index}]: phase bridge requires a shadow/receiver phase filter and excludes private path diagnostics")
    if "hf_compare_admitted_models" in item and (mode not in {"receive", "plan"} or
            item.get("backend", "cpu") != "cpu" or item.get("hf_public_phase_receiver", False)):
        raise CampaignError(f"episodes[{index}]: admitted-model comparison requires CPU receive/plan without the public phase supplement")
    if item.get("hf_physical_diagnostic", False) and (mode != "hf-search" or control != "wanted"):
        raise CampaignError(f"episodes[{index}]: physical diagnostic requires wanted hf-search mode")
    if item.get("hf_public_phase_receiver", False):
        if mode not in {"receive", "slot"} or item.get("helper", False) or item.get("backend", "cpu") != "cpu":
            raise CampaignError(f"episodes[{index}]: HF public phase receiver requires receive/slot with Helper-off CPU")
    if "capture_digest_slot" in item:
        if mode not in {"receive", "slot", "hf-search", "gui-varying"}:
            raise CampaignError(f"episodes[{index}]: capture_digest_slot requires receive, slot, hf-search or gui-varying mode")
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
                        ("hf_trajectory_seed", "--hf-trajectory-seed"),
                        ("hf_trajectory_offset_seconds", "--hf-trajectory-offset-seconds"),
                        ("noise_seed", "--noise-seed"),
                        ("history_reference_delay_seconds", "--history-reference-delay-seconds"),
                        ("hf_phase_bank", "--hf-phase-bank"),
                        ("hf_public_phase_receiver", "--hf-public-phase-receiver"),
                        ("hf_physical_diagnostic", "--hf-physical-diagnostic"),
                        ("hf_phase_refinement", "--hf-phase-refinement"),
                        ("hf_phase_filter", "--hf-phase-filter"),
                        ("hf_phase_filter_receiver", "--hf-phase-filter-receiver"),
                        ("hf_phase_filter_paths", "--hf-phase-filter-paths"),
                        ("hf_phase_filter_sticky", "--hf-phase-filter-sticky"),
                        ("hf_phase_bridge", "--hf-phase-bridge"),
                        ("hf_gui_clean_diagnostic", "--hf-gui-clean-diagnostic"),
                        ("hf_compare_admitted_models", "--hf-compare-admitted-models")) + tuple(
                            (key, "--" + key.replace("_", "-")) for key in sorted(GUI_DEFAULTS.keys() | HF_DESIGN_FIELDS | SERVICE_FIELDS)):
        if key in episode:
            value = episode[key]
            if key in {"helper", "hf_phase_bank", "hf_public_phase_receiver", "hf_physical_diagnostic", "hf_phase_refinement", "hf_phase_filter", "hf_phase_filter_receiver", "hf_phase_filter_paths", "hf_phase_filter_sticky", "hf_phase_bridge", "hf_compare_admitted_models", "hf_gui_clean_diagnostic", "gui_clock_edge", "service_replay"}:
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
    if episode.get("service_replay", False) and episode.get("service_episodes", 1) == 2:
        return "service_replay.communication_complete"
    return "correct_decoded_complete" if episode.get("fec", "off") != "off" else "correct_complete"


def _communication_outcome(receipt: dict[str, Any], episode: dict[str, Any]) -> Any:
    if _communication_endpoint(episode) == "service_replay.communication_complete":
        return receipt.get("service_replay", {}).get("communication_complete")
    return receipt.get("outcomes", {}).get(_communication_endpoint(episode))


def _validate_phase_filter(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    search = receipt["hf_public_search"]
    result = search.get("phase_filter")
    if not episode.get("hf_phase_filter", False):
        if result is not None:
            raise CampaignError("unrequested HF phase filter result")
        return
    _validate_phase_filter_result(search, receipt["plan"], receipt["plan"]["search_observation"])


def _validate_phase_filter_result(search: dict[str, Any], plan: dict[str, Any], observation: Any) -> None:
    result = search.get("phase_filter")
    if (not isinstance(result, dict) or result.get("enabled") is not True or result.get("executed") is not True or
            plan.get("sample_rate_hz") != 1920 or search.get("evaluated_timing_atoms") != 39):
        raise CampaignError("HF phase filter requires its supported full-timing geometry and executed receipt")
    sticky = plan.get("hf_phase_filter_sticky_requested", False)
    bridge = plan.get("hf_phase_bridge_requested", False)
    sticky_fields = {"sticky_enabled", "walk", "sticky", "correlated_prior_weights", "transition_probabilities"}
    if sticky:
        transitions = result.get("transition_probabilities")
        expected_transitions = {"walk": {"stay": .5, "step_each": .25, "edge_stay": .75},
                                "sticky": {"stay": .96875, "step_each": .015625, "edge_stay": .984375}}
        if (result.get("sticky_enabled") is not True or result.get("correlated_prior_weights") != [.5, .5] or
                not isinstance(transitions, dict) or set(transitions) != set(expected_transitions)):
            raise CampaignError("sticky phase filter must report its fixed equal correlated-family prior")
        for family, expected_transition in expected_transitions.items():
            values = transitions[family]
            if (not isinstance(values, dict) or set(values) != set(expected_transition) or
                    any(not _is_finite_number(values[field]) or values[field] != value for field, value in expected_transition.items())):
                raise CampaignError("sticky phase filter transition probabilities must match the frozen normalized priors")
    elif sticky_fields & result.keys():
        raise CampaignError("unrequested sticky phase filter telemetry")
    bridge_fields = {"bridge_enabled", "bridge", "legacy_correlated", "bridge_prior_weights", "bridge_work"}
    if bridge:
        if result.get("bridge_enabled") is not True or result.get("bridge_prior_weights") != [.5, .5]:
            raise CampaignError("phase bridge must retain its fixed equal bridge/legacy correlated prior")
    elif bridge_fields & result.keys():
        raise CampaignError("unrequested phase bridge telemetry")
    if (not isinstance(observation, dict) or any(not _is_nonnegative_int(observation.get(field)) for field in ("first", "stop")) or
            observation["stop"] <= observation["first"] or not _is_nonnegative_int(plan.get("horizon_samples")) or
            observation["stop"] > plan["horizon_samples"]):
        raise CampaignError("HF phase filter requires its complete public observation inside the declared horizon")
    samples = observation["stop"] - observation["first"]
    groups, blocks = (samples + 1919) // 1920, (samples + 127) // 128
    bounds = plan.get("public_bounds", {})
    base_frequency, atmosphere = bounds.get("frequency_bound_hz"), bounds.get("atmospheric_hz")
    if (search.get("block_rows") != 128 or search.get("blocks") != blocks or
            not _is_finite_number(base_frequency) or base_frequency < 0 or
            not _is_finite_number(atmosphere) or atmosphere < 0 or base_frequency + atmosphere != 1 or
            not _is_finite_number(search.get("configured_frequency_bound_hz")) or search["configured_frequency_bound_hz"] != 1):
        raise CampaignError("HF phase filter must preserve the supported public block and frequency geometry")
    expected = {"phase_bins": 32, "frequency_bins": 33, "frequency_step_hz": .0625,
                "group_seconds": 1, "groups": groups, "timing_atoms": 39, "timing_prior_mass": 1,
                "branch_weight_within_supplement": .5, "supplement_weight": .25,
                "input_samples": samples, "input_block_rows": 128, "input_blocks": blocks}
    if not 0 < samples <= 600 * 1920 or groups > 600 or blocks > 9000 or result.get("gain_radii") != [.5, 1]:
        raise CampaignError("HF phase filter exceeds its fixed aperture or gain-prior bounds")
    for field, value in expected.items():
        if not _is_finite_number(result.get(field)) or result[field] != value:
            raise CampaignError("HF phase filter geometry and mixture weights must match its fixed public contract")
        if field in {"phase_bins", "frequency_bins", "groups", "timing_atoms", "input_samples", "input_block_rows", "input_blocks"} and not _is_nonnegative_int(result[field]):
            raise CampaignError("HF phase filter count fields must be integers")
    if not _is_finite_number(result.get("stage_wall_seconds")) or result["stage_wall_seconds"] < 0:
        raise CampaignError("HF phase filter must report finite nonnegative stage time")
    thresholds = (search.get("presence_reference_threshold"), search.get("bit_separation_reference"))
    if any(not _is_finite_number(value) or value < 0 for value in thresholds):
        raise CampaignError("HF phase filter requires finite public decision references")

    def log_add(a: float, b: float) -> float:
        return max(a, b) + math.log1p(math.exp(-abs(a - b)))

    evidence = {}
    for name in (("baseline", "correlated", "combined") + (("walk", "sticky") if sticky else ()) +
                 (("legacy_correlated", "bridge") if bridge else ())):
        branch = result.get(name)
        if not isinstance(branch, dict):
            raise CampaignError("HF phase filter requires every separately reported branch")
        values = branch.get("bit_log_e")
        if not isinstance(values, list) or len(values) != 2 or any(not _is_finite_number(value) for value in values):
            raise CampaignError("HF phase filter requires finite paired bit evidence")
        presence, separation = log_add(*values) - math.log(2) + math.log(.25), abs(values[1] - values[0])
        if (not _is_finite_number(branch.get("weighted_presence_log_e")) or
                not math.isclose(branch["weighted_presence_log_e"], presence, rel_tol=1e-10, abs_tol=1e-9) or
                not _is_finite_number(branch.get("bit_log_separation")) or
                not math.isclose(branch["bit_log_separation"], separation, rel_tol=1e-10, abs_tol=1e-9) or
                not _is_nonnegative_int(branch.get("favored_bit")) or branch["favored_bit"] != int(values[1] > values[0]) or
                branch.get("passes_presence_reference") is not (presence >= thresholds[0]) or
                branch.get("passes_bit_separation_reference") is not (separation >= thresholds[1])):
            raise CampaignError("HF phase filter diagnostics must retain weighted evidence and public references")
        evidence[name] = values
    if evidence["baseline"] != search["phase_refinement"]["combined_bit_log_e"]:
        raise CampaignError("HF phase filter must preserve the separately reported refinement baseline")
    if any(not math.isclose(combined, log_add(old, new) - math.log(2), rel_tol=1e-10, abs_tol=1e-9)
           for old, new, combined in zip(evidence["baseline"], evidence["correlated"], evidence["combined"])):
        raise CampaignError("HF phase filter must preserve the fixed equal branch mixture")
    legacy = evidence["legacy_correlated"] if bridge else evidence["correlated"]
    if sticky and any(not math.isclose(correlated, log_add(walk, held) - math.log(2), rel_tol=1e-10, abs_tol=1e-9)
                      for correlated, walk, held in zip(legacy, evidence["walk"], evidence["sticky"])):
        raise CampaignError("sticky phase filter must preserve the fixed equal walk/sticky mixture")
    if bridge and any(not math.isclose(correlated, log_add(old, new) - math.log(2), rel_tol=1e-10, abs_tol=1e-9)
                      for correlated, old, new in zip(evidence["correlated"], legacy, evidence["bridge"])):
        raise CampaignError("phase bridge must preserve the fixed equal bridge/legacy mixture")
    work = result.get("work")
    expected_work = {"correlation_products": 39 * 2 * 33 * blocks,
                     "emission_states": 39 * 4 * 33 * 32 * groups,
                     "transition_edges": 39 * 4 * 32 * 97 * (groups - 1)}
    if not isinstance(work, dict):
        raise CampaignError("HF phase filter requires bounded native work accounting")
    for field, value in expected_work.items():
        if not _is_nonnegative_int(work.get(field)) or work[field] != value * (2 if sticky else 1):
            raise CampaignError("HF phase filter work must match every full public timing atom")
    if (not _is_nonnegative_int(work.get("workspace_reserved_bytes")) or work["workspace_reserved_bytes"] == 0 or
            not _is_finite_number(work.get("summed_worker_wall_seconds")) or work["summed_worker_wall_seconds"] < 0):
        raise CampaignError("HF phase filter requires actual bounded workspace and worker timing")
    if (sticky or bridge) and (not _is_nonnegative_int(search.get("stage_workers")) or not 1 <= search["stage_workers"] <= 39 or
            work["workspace_reserved_bytes"] != search["stage_workers"] * 131072):
        raise CampaignError("sticky phase passes must share the unchanged bounded per-worker workspace")
    if bridge:
        bridge_work = result.get("bridge_work")
        bridge_groups = (blocks + 59) // 60
        expected_bridge = {"group_seconds": 4, "groups": bridge_groups,
                           "correlation_products": 39 * 2 * 97 * blocks,
                           "edge_emissions": 39 * 4 * 97 * 32 * bridge_groups,
                           "forward_terms": 39 * 4 * 97 * 32 * bridge_groups,
                           "shared_rotator_values": 97 * 60,
                           "lane_workspace_bytes": 131072, "shared_workspace_bytes": 98304,
                           "workspace_reserved_bytes": search["stage_workers"] * 131072 + 98304}
        if (not isinstance(bridge_work, dict) or any(
                not (_is_finite_number(bridge_work.get(field)) if field == "group_seconds" else _is_nonnegative_int(bridge_work.get(field))) or
                bridge_work[field] != value for field, value in expected_bridge.items())):
            raise CampaignError("phase bridge work must retain every timing atom and final-group endpoint within its admitted buffers")
        transitions = bridge_work.get("transition_probabilities")
        expected_transitions = {"stay": .5, "step_each": .25, "edge_stay": .75}
        if (not isinstance(transitions, dict) or set(transitions) != set(expected_transitions) or
                any(not _is_finite_number(transitions[field]) or transitions[field] != value
                    for field, value in expected_transitions.items())):
            raise CampaignError("phase bridge transitions must retain the fixed normalized edge prior")
        for field in ("stage_wall_seconds", "summed_worker_wall_seconds"):
            if not _is_finite_number(bridge_work.get(field)) or bridge_work[field] < 0:
                raise CampaignError("phase bridge requires separate finite nonnegative stage and worker timing")


def _validate_private_phase_paths(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    physical, search, plan = receipt["hf_physical_diagnostic"], receipt["hf_public_search"], receipt["plan"]
    result = physical.get("phase_filter_paths")
    if not episode.get("hf_phase_filter_paths", False):
        if result is not None:
            raise CampaignError("unrequested private phase path diagnostic")
        return
    if (not isinstance(result, dict) or result.get("used_for_receiver_decisions") is not False or
            result.get("uses_physical_truth") is not True or result.get("observation_sha256") != physical["observation_sha256"] or
            not _is_nonnegative_int(result.get("wanted_bit")) or result["wanted_bit"] != physical["physical_templates"]["wanted_bit"]):
        raise CampaignError("private phase paths must preserve evaluator-only truth and observation binding")
    for field in ("epoch_offset_seconds", "clock_ppm", "arrival_seconds", "reference_path_delay_seconds",
                  "clean_coherence_power_lower_bound", "optimal_clean_correlation_real"):
        if not _is_finite_number(result.get(field)):
            raise CampaignError("private phase paths require finite coordinates and clean support diagnostics")
    bounds = plan.get("public_bounds", {})
    if (result["epoch_offset_seconds"] != 0 or result["clock_ppm"] != 0 or bounds.get("clock_bound_ppm") != 0 or
            not 0 <= result["clean_coherence_power_lower_bound"] <= 1 or result["optimal_clean_correlation_real"] < 0 or
            result.get("gain_radii") != [.5, 1]):
        raise CampaignError("private phase paths must retain zero-clock scope and bounded finite model support")
    paths = receipt.get("scenario", {}).get("channel", {}).get("paths")
    if (not isinstance(paths, list) or len(paths) != 1 or not isinstance(paths[0], dict) or
            paths[0].get("delay_seconds") != result["reference_path_delay_seconds"] or
            any(not _is_finite_number(paths[0].get(field)) or paths[0][field] != 0 for field in
                ("delay_rate", "residual_doppler_hz", "residual_drift_hz_s", "gaussian_width_2sigma_hz"))):
        raise CampaignError("private phase paths must bind their single analytic ray's declared reference delay")
    for field in ("arrival_low_seconds", "arrival_high_seconds"):
        if not _is_finite_number(bounds.get(field)):
            raise CampaignError("private phase paths require explicit public arrival bounds")
    reference = result["reference_path_delay_seconds"]
    if not bounds["arrival_low_seconds"] <= reference <= bounds["arrival_high_seconds"]:
        raise CampaignError("private phase reference delay must remain inside the public prior")
    coordinates = search.get("coordinates")
    if not isinstance(coordinates, list) or len(coordinates) != 39:
        raise CampaignError("private phase paths require the complete 39-atom public timing bank")
    for coordinate in coordinates:
        if not isinstance(coordinate, dict) or any(not _is_finite_number(coordinate.get(field))
                for field in ("epoch_offset_seconds", "clock_ppm", "arrival_seconds")):
            raise CampaignError("private phase paths require finite public timing coordinates")
    eligible = [(i, row) for i, row in enumerate(coordinates) if row["epoch_offset_seconds"] == 0 and row["clock_ppm"] == 0]
    if not eligible:
        raise CampaignError("private phase paths require a zero-epoch, zero-clock public atom")
    selected, coordinate = min(eligible, key=lambda item: abs(item[1]["arrival_seconds"] - reference))
    if (not _is_nonnegative_int(result.get("timing_index")) or result["timing_index"] != selected or
            result["arrival_seconds"] != coordinate["arrival_seconds"]):
        raise CampaignError("private phase paths must use the nearest reference-delay atom without noisy selection")
    scores = []
    for field in ("maximum_noisy_log_lr", "maximum_prior_weighted_noisy_path_log_e", "conditional_noisy_log_e"):
        values = result.get(field)
        if not isinstance(values, list) or len(values) != 2 or any(not _is_finite_number(value) for value in values):
            raise CampaignError("private phase paths require finite scores for both declared gain radii")
        scores.append(values)
    for maximum, prior_maximum, marginal in zip(*scores):
        tolerance = 2e-9 * max(1, abs(maximum), abs(prior_maximum), abs(marginal))
        if prior_maximum > marginal + tolerance or marginal > maximum + tolerance:
            raise CampaignError("private phase maximum and marginalized scores must preserve normalized path priors")
    public_filter = search["phase_filter"]
    blocks, groups = public_filter["input_blocks"], public_filter["groups"]
    work = result.get("work")
    expected = {"correlation_products": 2 * 33 * blocks, "emission_states": 7 * 33 * 32 * groups,
                "transition_edges": 7 * 32 * 97 * (groups - 1), "workspace_reserved_bytes": 128 * 1024,
                "block_statistics_reserved_bytes": 40 * blocks,
                "public_pair_reserved_bytes": physical["admission"].get("pair_workspace_bytes")}
    if not isinstance(work, dict) or any(not _is_nonnegative_int(work.get(field)) or work[field] != value for field, value in expected.items()):
        raise CampaignError("private phase path work must match its finite lattice and reserved native buffers")
    trace_bytes = 0
    if "clean_trace" in result:
        trace_bytes = _validate_private_clean_trace(result, physical, search, groups, blocks, public_filter["input_samples"])
    if "bridge_support" in result:
        trace_bytes += _validate_private_bridge_support(result, physical, search, blocks)
    if trace_bytes + sum(work[field] for field in ("workspace_reserved_bytes", "block_statistics_reserved_bytes", "public_pair_reserved_bytes")) > physical["admission"]["future_reserved_bytes"]:
        raise CampaignError("private phase path buffers must fit the physical diagnostic reservation")


def _validate_private_bridge_support(result: dict[str, Any], physical: dict[str, Any], search: dict[str, Any], blocks: int) -> int:
    """Validate additive evaluator-only support geometry, never public likelihood evidence."""
    if not _is_nonnegative_int(blocks) or not 0 < blocks <= 9000:
        raise CampaignError("private bridge support exceeds its fixed public aperture cap")
    support = result["bridge_support"]
    if (not isinstance(support, dict) or not _is_nonnegative_int(support.get("schema_version")) or support["schema_version"] != 1 or
            support.get("used_for_receiver_decisions") is not False or support.get("phase_invariant") != "p-4*k modulo 8"):
        raise CampaignError("private bridge support must retain versioned evaluator-only phase-lattice scope")
    groups = (blocks + 59) // 60
    expected = {"workspace_reserved_bytes": 262144, "groups": groups,
                "correlation_products": 159 * blocks, "max_plus_terms": (97 + 291 + 159) * 32 * groups}
    if (not _is_finite_number(support.get("group_seconds")) or support["group_seconds"] != 4 or
            any(not _is_nonnegative_int(support.get(field)) or support[field] != value for field, value in expected.items())):
        raise CampaignError("private bridge support must preserve its fixed cache, edge counts and workspace")
    clean_energy = physical["physical_templates"].get("noise_free_energy")
    energies = search["coordinates"][result["timing_index"]].get("template_energy")
    if (not _is_finite_number(clean_energy) or clean_energy <= 0 or not isinstance(energies, list) or len(energies) != 2 or
            any(not _is_finite_number(value) or value <= 0 for value in energies)):
        raise CampaignError("private bridge support must bind positive clean and public-template energies")
    normalization = math.sqrt(clean_energy) * math.sqrt(energies[result["wanted_bit"]])
    correlations = []
    for name, edges in (("original", 97), ("phase_relaxed", 291), ("rate_relaxed", 159)):
        variant = support.get(name)
        if (not isinstance(variant, dict) or not _is_nonnegative_int(variant.get("edge_terms_per_phase")) or
                variant["edge_terms_per_phase"] != edges or
                not _is_finite_number(variant.get("real_correlation")) or variant["real_correlation"] < 0 or
                not _is_finite_number(variant.get("clean_coherence_power")) or not 0 <= variant["clean_coherence_power"] <= 1):
            raise CampaignError("private bridge support variants require finite bounded projection and fixed edge geometry")
        fraction = variant["real_correlation"] / normalization
        if fraction > 1 + 1e-8 or not math.isclose(variant["clean_coherence_power"], min(1., fraction * fraction), rel_tol=2e-9, abs_tol=1e-12):
            raise CampaignError("private bridge support coherence must match its clean projection and energy normalization")
        correlations.append(variant["real_correlation"])
    if any(value + 2e-9 * max(1., abs(value), abs(correlations[0])) < correlations[0] for value in correlations[1:]):
        raise CampaignError("private bridge support relaxations must contain the original model support")
    return support["workspace_reserved_bytes"]


def _validate_private_clean_trace(result: dict[str, Any], physical: dict[str, Any], search: dict[str, Any],
                                  groups: int, blocks: int, samples: int) -> int:
    """Validate additive v1 evaluator paths; old receipts without a trace remain valid."""
    trace = result["clean_trace"]
    if not isinstance(trace, dict) or not _is_nonnegative_int(trace.get("schema_version")) or trace["schema_version"] != 1:
        raise CampaignError("private clean trace requires supported schema version 1")
    expected_work = {"workspace_reserved_bytes": 2 * 1056 * (groups - 1) + 262144,
                     "analytic_carrier_points": blocks + groups + 1,
                     "extra_state_evaluations": 1056 * groups,
                     "extra_transition_edges": 32 * 97 * (groups - 1),
                     "predecessor_labels": 2 * 1056 * (groups - 1)}
    if any(not _is_nonnegative_int(trace.get(field)) or trace[field] != value for field, value in expected_work.items()):
        raise CampaignError("private clean trace work must match its bounded discrete state and analytic carrier geometry")
    for field in ("penalty_correlation_units", "target_projection_power_fraction_of_best_real", "target_real_correlation",
                  "penalized_objective_correlation", "certificate_tolerance_correlation",
                  "carrier_frequency_total_variation_hz", "carrier_frequency_rms_step_hz"):
        if not _is_finite_number(trace.get(field)):
            raise CampaignError("private clean trace certificate and carrier diagnostics must be finite")
    if (trace["target_projection_power_fraction_of_best_real"] != .99 or
            trace["carrier_frequency_total_variation_hz"] < 0 or trace["carrier_frequency_rms_step_hz"] < 0 or
            not _is_nonnegative_int(trace.get("carrier_reversal_count")) or trace["carrier_reversal_count"] > max(0, groups - 2) or
            not isinstance(trace.get("carrier_reference_scope"), str) or not trace["carrier_reference_scope"].strip()):
        raise CampaignError("private clean trace must retain its fixed real-projection target and analytic carrier scope")
    clean_energy = physical["physical_templates"].get("noise_free_energy")
    energies = search["coordinates"][result["timing_index"]].get("template_energy")
    if (not _is_finite_number(clean_energy) or clean_energy <= 0 or not isinstance(energies, list) or len(energies) != 2 or
            any(not _is_finite_number(value) or value <= 0 for value in energies)):
        raise CampaignError("private clean trace must bind positive clean and public-template energy")
    normalization = math.sqrt(clean_energy) * math.sqrt(energies[result["wanted_bit"]])
    expected_penalty = normalization / (64 * groups)
    expected_tolerance = 128 * sys.float_info.epsilon * (groups + 1) * normalization
    target = math.sqrt(.99) * result["optimal_clean_correlation_real"]
    if (not math.isclose(trace["penalty_correlation_units"], expected_penalty, rel_tol=2e-9, abs_tol=1e-12) or
            not math.isclose(trace["certificate_tolerance_correlation"], expected_tolerance, rel_tol=2e-9, abs_tol=1e-15) or
            not math.isclose(trace["target_real_correlation"], target, rel_tol=2e-9, abs_tol=1e-12)):
        raise CampaignError("private clean trace target, penalty and certificate tolerance must bind declared energies")
    projection_rows = []
    for name in ("maximum_projection", "penalized_projection"):
        row = trace.get(name)
        if not isinstance(row, dict):
            raise CampaignError("private clean trace requires both reconstructed paths")
        frequency, phase = row.get("frequency_bins"), row.get("phase_bins")
        if (not isinstance(frequency, list) or not isinstance(phase, list) or len(frequency) != groups or len(phase) != groups or
                any(isinstance(value, bool) or not isinstance(value, int) or not -16 <= value <= 16 for value in frequency) or
                any(not _is_nonnegative_int(value) or value >= 32 for value in phase)):
            raise CampaignError("private clean paths must retain one bounded frequency/phase state per group")
        steps = [b - a for a, b in zip(frequency, frequency[1:])]
        if any(abs(step) > 1 for step in steps) or any(b != (a + 2 * k) % 32 for a, b, k in zip(phase, phase[1:], frequency)):
            raise CampaignError("private clean paths must follow the frozen frequency and phase transition graph")
        jumps = [step for step in steps if step]
        longest = dwell = 1
        for step in steps:
            dwell = 1 if step else dwell + 1
            longest = max(longest, dwell)
        counts = {"jumps": len(jumps), "up_jumps": steps.count(1), "down_jumps": steps.count(-1),
                  "adjacent_reversals": sum(a * b == -1 for a, b in zip(steps, steps[1:])),
                  "successive_jump_reversals": sum(a * b == -1 for a, b in zip(jumps, jumps[1:])),
                  "edge_stays": sum(step == 0 and abs(k) == 16 for k, step in zip(frequency, steps)),
                  "longest_dwell_groups": longest}
        if any(not _is_nonnegative_int(row.get(field)) or row[field] != value for field, value in counts.items()):
            raise CampaignError("private clean path counts must match the reconstructed state trace")
        if (not _is_nonnegative_int(row.get("reversals_below_half_bin_carrier_change")) or
                row["reversals_below_half_bin_carrier_change"] > counts["successive_jump_reversals"]):
            raise CampaignError("private carrier comparison cannot exceed reconstructed jump reversals")
        for prefix, stay, jump, edge in (("walk", .5, .25, .75), ("sticky", .96875, .015625, .984375)):
            log_prior = -math.log(1056) + sum(math.log(jump if step else edge if abs(k) == 16 else stay)
                                              for k, step in zip(frequency, steps))
            if not _is_finite_number(row.get(prefix + "_log_prior")) or not math.isclose(
                    row[prefix + "_log_prior"], log_prior, rel_tol=2e-9, abs_tol=1e-9):
                raise CampaignError("private clean path priors must include the initial-state and transition masses")
        for field in ("real_correlation", "clean_coherence_power", "effective_phase_rms_radians",
                      "effective_aligned_phase_rms_radians", "carrier_phase_rms_radians",
                      "carrier_aligned_phase_rms_radians", "carrier_frequency_rms_hz"):
            if not _is_finite_number(row.get(field)) or row[field] < 0:
                raise CampaignError("private clean projection and mismatch diagnostics must be finite and nonnegative")
        if any(row[field] > math.pi + 1e-12 for field in ("effective_phase_rms_radians", "effective_aligned_phase_rms_radians",
                                                       "carrier_phase_rms_radians", "carrier_aligned_phase_rms_radians")):
            raise CampaignError("private clean phase RMS diagnostics must use wrapped radians")
        rms_four = row.get("carrier_frequency_4_group_rms_hz")
        if ("carrier_frequency_4_group_rms_hz" not in row or (samples // 1920 < 4 and rms_four is not None) or
                (samples // 1920 >= 4 and (not _is_finite_number(rms_four) or rms_four < 0))):
            raise CampaignError("private four-group carrier comparison must retain its full-group availability")
        coherence = min(1., (row["real_correlation"] / normalization) ** 2)
        if row["clean_coherence_power"] > 1 + 2e-9 or not math.isclose(row["clean_coherence_power"], coherence, rel_tol=2e-9, abs_tol=1e-12):
            raise CampaignError("private clean coherence must match its declared real projection and energy")
        projection_rows.append(row)
    maximum, penalized = projection_rows
    tolerance = trace["certificate_tolerance_correlation"]
    objective = penalized["real_correlation"] - trace["penalty_correlation_units"] * penalized["jumps"]
    if (not math.isclose(maximum["real_correlation"], result["optimal_clean_correlation_real"], rel_tol=2e-9, abs_tol=max(tolerance, 1e-12)) or
            not _is_finite_number(result.get("clean_coherence_power_lower_bound")) or
            not math.isclose(maximum["clean_coherence_power"], result["clean_coherence_power_lower_bound"], rel_tol=2e-9, abs_tol=1e-12) or
            penalized["real_correlation"] > maximum["real_correlation"] + max(tolerance, 1e-12) or
            abs(trace["penalized_objective_correlation"] - objective) > max(tolerance, 1e-12) or
            objective + max(tolerance, 1e-12) < maximum["real_correlation"] - trace["penalty_correlation_units"] * maximum["jumps"]):
        raise CampaignError("private clean witnesses must preserve maximum projection and penalized optimality")
    lower = max(0, math.ceil((trace["target_real_correlation"] - trace["penalized_objective_correlation"] - tolerance) /
                            trace["penalty_correlation_units"]))
    if (not _is_nonnegative_int(trace.get("minimum_jumps_lower_bound")) or trace["minimum_jumps_lower_bound"] != lower or
            lower > maximum["jumps"] or not isinstance(trace.get("target_reached"), bool) or
            trace["target_reached"] is not (penalized["real_correlation"] - tolerance >= trace["target_real_correlation"])):
        raise CampaignError("private clean jump certificate must preserve its discrete real-projection target")
    if trace["target_reached"] and lower > penalized["jumps"]:
        raise CampaignError("private clean jump certificate contradicts its target-reaching witness")
    return trace["workspace_reserved_bytes"]


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


def _validate_profile_controls(plan: dict[str, Any], episode: dict[str, Any]) -> None:
    """Bind constructed physical parameters and canonical GUI preparation separately."""
    design = plan.get("hf_design")
    if episode["scenario"] == "hf-ensemble-design":
        if not isinstance(design, dict) or set(design) != set(HF_DESIGN_BOUNDS):
            raise CampaignError("native physical design receipt requires all eight resolved parameters")
        for field in HF_DESIGN_BOUNDS:
            if not _is_finite_number(design[field]) or design[field] != episode["hf_" + field]:
                raise CampaignError(f"native physical design {field} does not match the frozen episode")
    elif design is not None:
        raise CampaignError("unrequested native physical design receipt")
    gui = plan.get("gui_profile")
    if episode["scenario"] != "hf-gui-native":
        if gui is not None or plan.get("channel_renderer") == "canonical_gui_streaming_channel":
            raise CampaignError("unrequested canonical GUI profile receipt")
        return
    if (not isinstance(gui, dict) or plan.get("channel_renderer") != "canonical_gui_streaming_channel" or
            gui.get("profile_source") != "canonical_prepare_profile" or
            gui.get("realization_passed_to_receiver") is not False):
        raise CampaignError("native GUI receipt must identify canonical preparation and private realization")
    for field, default in GUI_DEFAULTS.items():
        actual = gui.get(field.removeprefix("gui_"))
        expected = episode.get(field, default)
        if (actual != expected or
                (field in GUI_NUMERIC_BOUNDS and not _is_finite_number(actual)) or
                (field == "gui_clock_edge" and not isinstance(actual, bool))):
            raise CampaignError(f"native GUI {field} does not match the frozen episode")
    fixture = gui.get("fixture_bit_seconds")
    if (fixture != episode.get("bit_seconds") or isinstance(fixture, bool) or
            (fixture is not None and not _is_finite_number(fixture))):
        raise CampaignError("native GUI fixture duration must remain separate from target planning")
    actual_cn0 = gui.get("actual_cn0_db_hz")
    if (not _is_finite_number(actual_cn0) or actual_cn0 != episode.get("cn0_db_hz", 32) or
            actual_cn0 != plan.get("cn0_db_hz")):
        raise CampaignError("native GUI actual noise must remain separate from target planning")
    preroll = gui.get("capture_preroll_seconds")
    if (not _is_finite_number(preroll) or preroll < .25 or
            preroll != plan.get("capture_preroll_seconds")):
        raise CampaignError("native GUI capture preroll must match canonical preparation")
    for field, expected_depth in zip(("selective_slow_depth", "selective_fast_depth"), GUI_SELECTIVE_DEPTHS[gui["selective"]]):
        if not _is_finite_number(gui.get(field)) or gui[field] != expected_depth:
            raise CampaignError("native GUI selective modulation requires separate slow and fast amplitude depths")
    endpoint = gui.get("endpoint_clock_bound_ppm")
    if not _is_finite_number(endpoint) or not math.isclose(endpoint, gui["audio_ppm"] * gui["margin"] / 2,
                                                          rel_tol=1e-12, abs_tol=1e-12):
        raise CampaignError("native GUI endpoint clock bound must retain the declared margin")
    spread = gui.get("spread_doppler_hz")
    expected_spread = GUI_SPREAD_DOPPLER[gui["spread"]] * gui["margin"]
    if not _is_finite_number(spread) or not math.isclose(spread, expected_spread, rel_tol=1e-12, abs_tol=1e-12):
        raise CampaignError("native GUI Doppler support must retain the selected preset and margin")
    realized = gui.get("realized")
    realized_fields = ("tx_audio_ppm", "rx_audio_ppm", "tx_rf_ppm", "rx_rf_ppm",
                       "timing_offset_seconds", "tx_phase_noise_rad_sqrt_s", "rx_phase_noise_rad_sqrt_s",
                       "relative_audio_ppm")
    if not isinstance(realized, dict) or any(not _is_finite_number(realized.get(field)) for field in realized_fields):
        raise CampaignError("native GUI receipt requires finite actual endpoint impairments")
    for band in ("audio", "rf"):
        bound = gui[band + "_ppm"] / 2
        for endpoint_name, sign in (("tx", 1), ("rx", -1)):
            actual = realized[f"{endpoint_name}_{band}_ppm"]
            if abs(actual) > bound + 1e-12 or (gui["clock_edge"] and actual != sign * bound):
                raise CampaignError("native GUI realized endpoint error must obey the unexpanded physical accuracy")
    relative = (realized["tx_audio_ppm"] - realized["rx_audio_ppm"]) / (1 + realized["rx_audio_ppm"] * 1e-6)
    if not math.isclose(realized["relative_audio_ppm"], relative, rel_tol=1e-10, abs_tol=1e-8):
        raise CampaignError("native GUI realized sample-clock ratio must use both physical endpoints")
    phase = math.hypot(gui["audio_phase_degrees_sqrt_s"], gui["rf_phase_degrees_sqrt_s"]) * math.pi / 180 / math.sqrt(2)
    if any(not math.isclose(realized[field], phase, rel_tol=1e-12, abs_tol=1e-12)
           for field in ("tx_phase_noise_rad_sqrt_s", "rx_phase_noise_rad_sqrt_s")):
        raise CampaignError("native GUI phase diffusion must retain both independent oscillator references")
    bounds = plan.get("public_bounds")
    if (not isinstance(bounds, dict) or any(not _is_finite_number(bounds.get(field)) for field in
            ("arrival_low_seconds", "arrival_high_seconds")) or
            not bounds["arrival_low_seconds"] <= realized["timing_offset_seconds"] <= bounds["arrival_high_seconds"] or
            not math.isclose(preroll, max(.25, -bounds["arrival_low_seconds"] + .25), rel_tol=1e-12, abs_tol=1e-12)):
        raise CampaignError("native GUI physical arrival and preroll must fit canonical public bounds")


def _validate_gain_partitions(physical: dict[str, Any], sample_rate: Any, sample_count: int,
                              wanted_bit: int, refined_available: bool) -> None:
    """Validate diagnostic block-gain curves without treating selected scores as evidence."""
    partitions = physical.get("gain_partitions")
    if partitions is None:  # Historical receipts retain their full-aperture-only scope.
        return
    if (not isinstance(partitions, dict) or partitions.get("used_for_receiver_decisions") is not False or
            partitions.get("template_selected_from_noisy_public_search") is not True or
            partitions.get("fixed_template_null_law_applies_after_selection") is not False or
            not _is_finite_number(sample_rate) or sample_rate <= 0):
        raise CampaignError("gain partitions must preserve selected-template diagnostic scope")
    pairs = [("physical_templates", physical["physical_templates"]),
             ("winning_public_cell", physical["winning_public_cell"]["projection"])]
    if refined_available:
        pairs.append(("refined_public_cell", physical["refined_public_cell"]["projection"]))
    elif partitions.get("refined_public_cell") is not None:
        raise CampaignError("gain partitions cannot invent an unavailable refined public cell")
    for name, projection in pairs:
        rows = partitions.get(name)
        if not isinstance(rows, list) or len(rows) != 5:
            raise CampaignError("gain partitions require all five fixed block lengths")
        for row, requested in zip(rows, (.25, 1, 4, 16, None)):
            samples = max(1, math.floor(requested * sample_rate + .5)) if requested is not None else sample_count
            blocks = (sample_count + samples - 1) // samples
            if (not isinstance(row, dict) or row.get("requested_block_seconds") != requested or
                    isinstance(row.get("requested_block_seconds"), bool) or
                    not _is_nonnegative_int(row.get("block_samples")) or row["block_samples"] != samples or
                    not _is_nonnegative_int(row.get("block_count")) or row["block_count"] != blocks or
                    not _is_finite_number(row.get("actual_block_seconds")) or
                    not math.isclose(row["actual_block_seconds"], samples / sample_rate, rel_tol=1e-12, abs_tol=1e-12)):
                raise CampaignError("gain partitions must cover the same aperture with fixed sample-rounded blocks")
            hypotheses = row.get("hypotheses")
            if not isinstance(hypotheses, list) or len(hypotheses) != 2:
                raise CampaignError("gain partitions require both bit hypotheses")
            for bit, hypothesis in enumerate(hypotheses):
                nonnegative = ("reference_energy", "projected_signal_energy", "residual_signal_energy", "noisy_projected_energy")
                if (not isinstance(hypothesis, dict) or not _is_nonnegative_int(hypothesis.get("rank")) or
                        hypothesis["rank"] > blocks or any(not _is_finite_number(hypothesis.get(field)) or hypothesis[field] < 0
                                                         for field in nonnegative) or
                        not _is_finite_number(hypothesis.get("noisy_log_likelihood_ratio")) or
                        hypothesis.get("noise_only_projected_energy") is not None or
                        hypothesis.get("noise_only_log_likelihood_ratio") is not None):
                    raise CampaignError("gain partitions require finite energies, likelihood and bounded complex rank")
                energy = projection["noise_free_energy"]
                projected, residual = hypothesis["projected_signal_energy"], hypothesis["residual_signal_energy"]
                fraction = hypothesis.get("retained_signal_fraction")
                if (not math.isclose(projected + residual, energy, rel_tol=1e-9, abs_tol=1e-9) or
                        (energy > 0 and (not _is_finite_number(fraction) or not 0 <= fraction <= 1 + 1e-10 or
                                         not math.isclose(fraction, projected / energy, rel_tol=1e-9, abs_tol=1e-10))) or
                        (energy == 0 and fraction is not None)):
                    raise CampaignError("gain partitions must preserve clean energy and normalized retained fraction")
                if requested is None:
                    full = projection["hypotheses"][bit]
                    for source, target in (("rank", "rank"), ("reference_energy", "template_energy"),
                                           ("projected_signal_energy", "clean_projected_energy"),
                                           ("residual_signal_energy", "clean_residual_energy"),
                                           ("noisy_log_likelihood_ratio", "noisy_log_likelihood_ratio")):
                        if not math.isclose(hypothesis[source], full[target], rel_tol=1e-9, abs_tol=1e-9):
                            raise CampaignError("gain partitions must reproduce the existing full-aperture projection")
            for field, hypothesis_field in (("correct_minus_wrong_projected_signal_energy", "projected_signal_energy"),
                                            ("correct_minus_wrong_noisy_log_likelihood_ratio", "noisy_log_likelihood_ratio")):
                expected = hypotheses[wanted_bit][hypothesis_field] - hypotheses[1 - wanted_bit][hypothesis_field]
                if not _is_finite_number(row.get(field)) or not math.isclose(row[field], expected, rel_tol=1e-9, abs_tol=1e-9):
                    raise CampaignError("gain partitions require a signed evaluator-only correct-bit difference")
            if row.get("correct_minus_wrong_noise_only_log_likelihood_ratio") is not None:
                raise CampaignError("gain partitions cannot report an unrendered noise-only twin")


def _validate_service_plan(receipt: dict[str, Any], episode: dict[str, Any]) -> dict[str, Any] | None:
    plan = receipt.get("plan")
    service = plan.get("service_replay") if isinstance(plan, dict) else None
    if not episode.get("service_replay", False):
        if service is not None or receipt.get("service_replay") is not None:
            raise CampaignError("unrequested native service replay receipt")
        return None
    if service is None and receipt.get("status") != "completed" and receipt.get("service_replay") is None:
        return None  # A pre-render failure has no admitted replay or service result.
    if not isinstance(plan, dict) or not isinstance(service, dict):
        raise CampaignError("requested service replay requires its native plan")
    if not _is_nonnegative_int(service.get("schema_version")) or service["schema_version"] != 1:
        raise CampaignError("service replay requires schema version 1")
    episodes = episode.get("service_episodes", 1)
    for field in ("episodes", "wire_bits_per_episode", "expected_message_count", "maximum_spool_bytes",
                  "planned_spool_bytes", "input_chunk_samples"):
        if not _is_nonnegative_int(service.get(field)):
            raise CampaignError("service replay plan requires integer geometry and byte bounds")
    wire, horizon, slots = service["wire_bits_per_episode"], plan.get("horizon_samples"), plan.get("scored_slot_horizon")
    if (service["episodes"] != episodes or wire <= 0 or wire != plan.get("wire_bits") or
            not _is_nonnegative_int(horizon) or horizon == 0 or
            not _is_nonnegative_int(slots) or slots == 0 or
            not _is_nonnegative_int(plan.get("max_samples")) or plan["max_samples"] == 0 or
            not _is_finite_number(plan.get("max_wall_seconds")) or plan["max_wall_seconds"] <= 0):
        raise CampaignError("service replay plan must bind its episode and complete source geometry")
    if "max_samples" in episode and plan["max_samples"] != episode["max_samples"]:
        raise CampaignError("service replay sample limit does not match the episode")
    if not isinstance(plan.get("public_slots"), list) or len(plan["public_slots"]) != slots:
        raise CampaignError("service replay requires its complete bounded public calendar")
    resources = plan.get("receiver_resources")
    if (not isinstance(resources, dict) or not _is_nonnegative_int(resources.get("hardware_threads")) or resources["hardware_threads"] == 0 or
            not _is_nonnegative_int(resources.get("workers")) or resources["workers"] != 2 * resources["hardware_threads"] or
            resources.get("workers_explicit") is not False or resources.get("owners_explicit") is not ("owners" in episode) or
            not _is_nonnegative_int(resources.get("slot_owners")) or
            resources["slot_owners"] != episode.get("owners", resources["workers"]) or resources["slot_owners"] > resources["workers"]):
        raise CampaignError("service replay must preserve full automatic workers and the exact explicit owner cap")
    target = episode.get("control", "wanted") in {"wanted", "interior-fade", "capture-gap"}
    intervals = [{"first_public_slot": i * (wire + 2), "stop_public_slot": (i + 1) * (wire + 2)}
                 for i in range(episodes)]
    expected_pattern = "".join("D" if episode.get("control", "wanted") != "noise" and
        any(row["first_public_slot"] <= i < row["stop_public_slot"] - 2 for row in intervals) else "Q"
        for i in range(slots))
    if (service["expected_message_count"] != (episodes if target else 0) or
            service.get("expected_message_intervals") != intervals or service.get("slot_pattern") != expected_pattern or
            (episodes == 2 and slots != 2 * (wire + 2) + 1)):
        raise CampaignError("service replay must preserve the declared global message and quiet-slot schedule")
    if any(not _is_nonnegative_int(row[field]) for row in service["expected_message_intervals"]
           for field in ("first_public_slot", "stop_public_slot")):
        raise CampaignError("service replay message interval fields must be integer slots")
    maximum = episode.get("service_spool_byte_limit", plan["max_samples"] * 17)
    if (service["maximum_spool_bytes"] != maximum or service["planned_spool_bytes"] != horizon * 17 or
            service.get("fits_spool_budget") is not (horizon * 17 <= maximum) or
            service["input_chunk_samples"] != episode.get("chunk", 4096)):
        raise CampaignError("service replay must bind the complete 17-byte-row spool and chunk limits")
    for field in ("backlog_begin_slot", "backlog_end_slot"):
        requested = episode.get("service_" + field)
        if service.get(field) != requested or (requested is not None and not _is_nonnegative_int(service[field])):
            raise CampaignError("service replay backlog interval does not match the frozen episode")
    if service["backlog_end_slot"] is not None and service["backlog_end_slot"] >= slots:
        raise CampaignError("service replay backlog interval exceeds the scored public horizon")
    represented = service.get("backlog_unique_baseband_seconds")
    if ((service["backlog_begin_slot"] is None and represented is not None) or
            (service["backlog_begin_slot"] is not None and (not _is_finite_number(represented) or represented <= 0))):
        raise CampaignError("service backlog must bind its exact canonical calendar duration")
    return service


def _validate_service_replay(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    service_plan = _validate_service_plan(receipt, episode)
    replay = receipt.get("service_replay")
    if replay is None:
        if episode.get("service_replay", False) and episode["mode"] == "receive" and receipt.get("status") == "completed":
            raise CampaignError("completed service receive requires its full replay measurement")
        return
    if episode["mode"] != "receive" or not isinstance(replay, dict) or service_plan is None:
        raise CampaignError("service execution measurements require an admitted receive replay")
    if any(replay.get(field) != value or type(replay.get(field)) is not type(value)
           for field, value in service_plan.items()):
        raise CampaignError("service result must repeat the complete frozen native replay plan")
    plan, channel = receipt["plan"], receipt.get("channel")
    horizon, fs = plan["horizon_samples"], plan.get("sample_rate_hz")
    if not _is_finite_number(fs) or fs <= 0:
        raise CampaignError("service replay requires positive sample-rate geometry")
    for field in ("wall_seconds", "spool_read_and_verification_seconds", "render_wall_seconds", "spool_write_seconds"):
        if not _is_finite_number(replay.get(field)) or replay[field] < 0:
            raise CampaignError("service replay wall timings must be finite and nonnegative")
    for field in ("process_cpu_seconds", "spool_read_and_verification_process_cpu_seconds",
                  "render_process_cpu_seconds", "spool_write_process_cpu_seconds"):
        if field not in replay or (replay[field] is not None and
                (not _is_finite_number(replay[field]) or replay[field] < 0)):
            raise CampaignError("service replay CPU timings must be explicit finite values or unavailable")
    for subset, whole in (("spool_read_and_verification_seconds", "wall_seconds"),
                          ("spool_write_seconds", "render_wall_seconds"),
                          ("spool_read_and_verification_process_cpu_seconds", "process_cpu_seconds"),
                          ("spool_write_process_cpu_seconds", "render_process_cpu_seconds")):
        if replay[subset] is not None and (replay[whole] is None or replay[subset] > replay[whole] + 1e-6):
            raise CampaignError("service spool subset timings must remain inside the measured full scope")
    represented = horizon / fs
    for field, expected in (("unique_baseband_seconds", represented),
                            ("service_seconds_per_baseband_second", replay["wall_seconds"] / represented)):
        if not _is_finite_number(replay.get(field)) or not math.isclose(replay[field], expected, rel_tol=1e-9, abs_tol=1e-9):
            raise CampaignError("service throughput must use the complete unique baseband horizon")
    for field in ("captured_input_sha256", "verified_input_sha256"):
        if not isinstance(replay.get(field), str) or re.fullmatch(r"[0-9a-fA-F]{64}", replay[field]) is None:
            raise CampaignError("service replay requires capture and verification SHA-256 digests")
    if (not isinstance(channel, dict) or channel.get("input_sha256") != replay["captured_input_sha256"] or
            not _is_nonnegative_int(channel.get("samples")) or channel["samples"] != horizon or
            not _is_nonnegative_int(channel.get("hashed_samples")) or channel["hashed_samples"] != horizon):
        raise CampaignError("service replay must preserve the complete pre-rendered capture independently of its verified prefix")
    if not _is_nonnegative_int(replay.get("replayed_samples")) or replay["replayed_samples"] > horizon:
        raise CampaignError("service replay sample count must stay within the original horizon")
    digest_match = replay["replayed_samples"] == horizon and replay["captured_input_sha256"] == replay["verified_input_sha256"]
    if replay.get("digest_match") is not digest_match:
        raise CampaignError("service digest match requires verified samples for the whole horizon")
    for field in ("work_complete", "communication_complete"):
        if not isinstance(replay.get(field), bool):
            raise CampaignError("service work and communication completion must be separate booleans")
    if replay.get("capacity_qualified") is not False:
        raise CampaignError("bounded service replay must not claim hardware capacity qualification")
    integer_fields = ("peak_active_owners", "receiver_peak_reserved_bytes", "outside_receiver_future_reserved_bytes",
                      "spool_residency_reserved_bytes", "peak_reserved_bytes", "spool_bytes", "spool_working_bytes",
                      "total_application_memory_limit_bytes", "receiver_memory_limit_bytes", "max_samples")
    if any(not _is_nonnegative_int(replay.get(field)) for field in integer_fields):
        raise CampaignError("service replay requires explicit integer caps and peak reservations")
    if (replay["spool_bytes"] != service_plan["planned_spool_bytes"] or
            replay["spool_bytes"] > service_plan["maximum_spool_bytes"] or replay["spool_working_bytes"] <= 0 or
            replay["peak_reserved_bytes"] != sum(replay[field] for field in
                ("receiver_peak_reserved_bytes", "outside_receiver_future_reserved_bytes", "spool_residency_reserved_bytes")) or
            replay["peak_reserved_bytes"] > replay["total_application_memory_limit_bytes"] or
            not 0 < replay["receiver_memory_limit_bytes"] <= replay["total_application_memory_limit_bytes"] or
            replay["receiver_peak_reserved_bytes"] > replay["receiver_memory_limit_bytes"] or
            replay["receiver_memory_limit_bytes"] + replay["outside_receiver_future_reserved_bytes"] +
                replay["spool_residency_reserved_bytes"] != replay["total_application_memory_limit_bytes"] or
            replay["max_samples"] != plan["max_samples"] or
            not _is_finite_number(replay.get("max_wall_seconds")) or replay["max_wall_seconds"] != plan["max_wall_seconds"] or
            replay.get("spool_backing_kind") not in {"known_disk", "memory_filesystem", "unknown"}):
        raise CampaignError("service replay reservations and spool must fit the declared application limits")
    if ((replay["spool_backing_kind"] == "known_disk" and replay["spool_residency_reserved_bytes"] != 0) or
            (replay["spool_backing_kind"] != "known_disk" and replay["spool_residency_reserved_bytes"] < replay["spool_bytes"])):
        raise CampaignError("service replay must reserve complete memory-backed or unknown spool residency")
    resources = plan.get("receiver_resources")
    if (not isinstance(resources, dict) or not _is_nonnegative_int(resources.get("hardware_threads")) or
            resources["hardware_threads"] == 0 or not _is_nonnegative_int(resources.get("workers")) or resources["workers"] == 0 or
            not _is_nonnegative_int(resources.get("slot_owners")) or resources["slot_owners"] == 0 or
            replay["peak_active_owners"] > min(resources["workers"], resources["slot_owners"]) or
            replay["total_application_memory_limit_bytes"] != resources.get("total_memory_limit_bytes")):
        raise CampaignError("service replay owner peaks must respect the declared receiver allocation")
    for field in ("workers_explicit", "owners_explicit"):
        if not isinstance(replay.get(field), bool) or replay[field] is not resources.get(field):
            raise CampaignError("service replay must retain explicit worker and owner controls")
    busy = replay.get("mean_busy_hardware_fraction")
    expected_busy = replay["process_cpu_seconds"] / replay["wall_seconds"] / resources["hardware_threads"] if replay["process_cpu_seconds"] is not None and replay["wall_seconds"] > 0 else None
    if ((expected_busy is None and busy is not None) or (expected_busy is not None and
            (not _is_finite_number(busy) or not math.isclose(busy, expected_busy, rel_tol=1e-9, abs_tol=1e-9)))):
        raise CampaignError("service CPU occupancy must use measured process CPU, wall time and hardware threads")
    slots, receiver, outcomes = receipt.get("slots"), receipt.get("receiver"), receipt.get("outcomes")
    if not isinstance(slots, list) or not isinstance(outcomes, dict):
        raise CampaignError("service replay requires native ordered slots and outcomes")
    if replay["work_complete"]:
        if (receipt.get("status") != "completed" or not digest_match or not isinstance(receiver, dict) or
                receiver.get("state") != "input_ended" or receiver.get("next_public_slot") != plan["scored_slot_horizon"] or
                outcomes.get("accepted_samples") != horizon or len(slots) != plan["scored_slot_horizon"] or
                outcomes.get("actual_scored_slots") != len(slots)):
            raise CampaignError("complete service requires input-ended processing of the verified full horizon")
        for index, (slot, public) in enumerate(zip(slots, plan["public_slots"])):
            if (not isinstance(slot, dict) or not isinstance(public, dict) or
                    not _is_nonnegative_int(slot.get("index")) or slot["index"] != index or public.get("index") != index or
                    slot.get("complete") is not True or slot.get("all_valid") is not True or
                    slot.get("pattern_epoch") != public.get("pattern_epoch") or slot.get("symbol_ordinal") != public.get("symbol_ordinal") or
                    not _is_nonnegative_int(slot.get("first_sample")) or not _is_nonnegative_int(slot.get("stop_sample")) or
                    public.get("observation") != {"first": slot["first_sample"], "stop": slot["stop_sample"]}):
                raise CampaignError("complete service requires every ordered canonical slot, epoch and ordinal")
    if service_plan["episodes"] == 2 and (outcomes.get("correct_complete") is not False or outcomes.get("correct_decoded_complete") is not False):
        raise CampaignError("paired service must not reuse single-message success endpoints")
    if replay["communication_complete"]:
        if (not replay["work_complete"] or outcomes.get("false_data") != 0 or outcomes.get("premature_completions") != 0 or
                outcomes.get("accepted_messages") != service_plan["expected_message_count"]):
            raise CampaignError("service communication completion requires complete work and every expected accepted message")
        if service_plan["expected_message_count"]:
            _validate_service_messages(receipt, episode, service_plan)
    _validate_service_backlog(receipt, service_plan, replay)


def _validate_service_messages(receipt: dict[str, Any], episode: dict[str, Any], service_plan: dict[str, Any]) -> None:
    messages, outcomes = receipt.get("messages"), receipt["outcomes"]
    if (not isinstance(messages, list) or not all(isinstance(message, dict) for message in messages) or
            outcomes.get("physical_completions") != service_plan["expected_message_count"] or outcomes.get("premature_completions") != 0):
        raise CampaignError("service message success requires exact physical completion counts")
    accepted = [message for message in messages if message.get("decoded") is True]
    if (len(accepted) != service_plan["expected_message_count"] or
            sum(message.get("physical_complete") is True for message in messages) != len(accepted) or
            any(message.get("premature") is not False for message in messages)):
        raise CampaignError("service message success requires all expected, nonpremature native messages")
    for interval in service_plan["expected_message_intervals"]:
        found = [message for message in accepted if message.get("first_public_slot") == interval["first_public_slot"]]
        if len(found) != 1 or found[0].get("stop_public_slot") != interval["stop_public_slot"]:
            raise CampaignError("service messages must preserve both global public frame intervals")
        message = found[0]
        raw_fields = ("wrong_bits", "ambiguous_active_slots", "quiet_active_slots")
        if any(not _is_nonnegative_int(message.get(field)) for field in raw_fields):
            raise CampaignError("service messages require separate per-frame raw slot counts")
        active = receipt["slots"][interval["first_public_slot"]:interval["stop_public_slot"] - 2]
        if (message["ambiguous_active_slots"] != sum(slot.get("decision") == "ambiguous" for slot in active) or
                message["quiet_active_slots"] != sum(slot.get("decision") == "quiet" for slot in active)):
            raise CampaignError("service per-frame erasure counts must match its actual global slot decisions")
        local = dict(message, first_public_slot=0, stop_public_slot=service_plan["wire_bits_per_episode"] + 2)
        per_frame = dict(outcomes, physical_completions=1, accepted_messages=1, actual_scored_slots=service_plan["wire_bits_per_episode"] + 2,
                         missed_completion=False, **{field: message[field] for field in raw_fields})
        _validate_decoded_success(dict(receipt, messages=[local], outcomes=per_frame), episode, episode.get("fec", "off"))


def _validate_service_backlog(receipt: dict[str, Any], service_plan: dict[str, Any], replay: dict[str, Any]) -> None:
    interval = replay.get("backlog_interval")
    begin, end = service_plan["backlog_begin_slot"], service_plan["backlog_end_slot"]
    if begin is None:
        if interval is not None:
            raise CampaignError("unrequested service backlog interval")
        return
    if (not isinstance(interval, dict) or interval.get("start_after_public_slot") != begin or
            interval.get("stop_after_public_slot") != end or not isinstance(interval.get("complete"), bool) or
            not isinstance(interval.get("reason"), str) or not interval["reason"].strip()):
        raise CampaignError("service backlog result must bind its predeclared commit interval")
    if not interval["complete"]:
        if interval.get("process_cpu_seconds") is not None or any(field in interval for field in
                ("wall_seconds", "unique_baseband_seconds", "service_seconds_per_baseband_second", "busy_hardware_fraction")):
            raise CampaignError("ineligible backlog intervals must not report qualified throughput")
        return
    if not replay["work_complete"] or interval["reason"] != "predeclared_ready_input_commit_interval":
        raise CampaignError("eligible backlog requires complete replay and the native ready-input proof")
    slots, plan = receipt["slots"], receipt["plan"]
    if end >= len(slots):
        raise CampaignError("service backlog commit boundaries were not observed")
    first, last = slots[begin], slots[end]
    if (not _is_nonnegative_int(first.get("commit_input_stop")) or first["commit_input_stop"] < last["stop_sample"] or
            not _is_nonnegative_int(first.get("commit_uncommitted_slots")) or end - begin <= first["commit_uncommitted_slots"] or
            not _is_finite_number(first.get("commit_wall_seconds")) or not _is_finite_number(last.get("commit_wall_seconds"))):
        raise CampaignError("service backlog must prove ready input beyond startup carryover")
    wall = last["commit_wall_seconds"] - first["commit_wall_seconds"]
    represented = service_plan["backlog_unique_baseband_seconds"]
    if wall <= 0 or not _is_finite_number(represented) or represented <= 0:
        raise CampaignError("service backlog requires positive measured and represented duration")
    for field, expected in (("wall_seconds", wall), ("unique_baseband_seconds", represented),
                            ("service_seconds_per_baseband_second", wall / represented),
                            ("commits", end - begin), ("initial_uncommitted_slots", first["commit_uncommitted_slots"])):
        if not _is_finite_number(interval.get(field)) or not math.isclose(interval[field], expected, rel_tol=1e-9, abs_tol=1e-9):
            raise CampaignError("service backlog throughput must match its actual commit boundaries")
    start_cpu, stop_cpu = first.get("commit_process_cpu_seconds"), last.get("commit_process_cpu_seconds")
    cpu = stop_cpu - start_cpu if _is_finite_number(start_cpu) and _is_finite_number(stop_cpu) and stop_cpu >= start_cpu else None
    if cpu is None:
        if interval.get("process_cpu_seconds") is not None or interval.get("busy_hardware_fraction") is not None:
            raise CampaignError("service backlog must not invent unavailable process CPU")
    elif (not _is_finite_number(interval.get("process_cpu_seconds")) or
            not math.isclose(interval["process_cpu_seconds"], cpu, rel_tol=1e-9, abs_tol=1e-9) or
            not _is_finite_number(interval.get("busy_hardware_fraction")) or
            not math.isclose(interval["busy_hardware_fraction"], cpu / wall / plan["receiver_resources"]["hardware_threads"], rel_tol=1e-9, abs_tol=1e-9)):
        raise CampaignError("service backlog CPU occupancy must match measured commit CPU")


def _validate_gui_varying(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    """Validate a bounded same-observation component comparison, never reception."""
    plan = receipt["plan"]
    scope = "shadow_canonical_varying_of_selected_complete_observation"
    outcomes, diagnostic = receipt.get("outcomes"), receipt.get("hf_gui_varying_diagnostic")
    if (not isinstance(outcomes, dict) or plan.get("observation_scope") != scope or
            outcomes.get("observation_scope") != scope or
            not _is_nonnegative_int(plan.get("planned_uncontaminated_quiet_slots")) or
            plan["planned_uncontaminated_quiet_slots"] != 0):
        raise CampaignError("GUI varying diagnostic must preserve component observation scope")
    for field, expected in (("receiver_executed", False), ("component_only", True),
                            ("completion_evaluated", False), ("correct_complete", False),
                            ("correct_decoded_complete", False), ("public_search_executed", False)):
        if outcomes.get(field) is not expected:
            raise CampaignError("GUI varying diagnostic must not claim receiver decisions or completion")
    for field in ("actual_scored_slots", "physical_completions", "accepted_messages",
                  "premature_completions", "wrong_bits", "false_data"):
        if not _is_nonnegative_int(outcomes.get(field)) or outcomes[field] != 0:
            raise CampaignError("GUI varying diagnostic must not count operational receiver output")
    if receipt.get("slots") != [] or receipt.get("messages") != [] or "receiver" in receipt:
        raise CampaignError("GUI varying diagnostic must not publish receiver or message receipts")
    if (receipt["backend"].get("device") != "cpu" or
            receipt["backend"].get("executed_gpu_work") is not False):
        raise CampaignError("GUI varying diagnostic is CPU-only component work")
    if (not isinstance(diagnostic, dict) or diagnostic.get("component_complete") is not True or
            diagnostic.get("all_valid") is not True or
            diagnostic.get("used_for_receiver_decisions") is not False):
        raise CampaignError("GUI varying diagnostic must report complete, valid, evaluator-only work")
    selected = episode["capture_digest_slot"]
    if (not _is_nonnegative_int(plan.get("wire_bits")) or selected >= plan["wire_bits"] or
            not _is_nonnegative_int(diagnostic.get("public_slot")) or diagnostic["public_slot"] != selected):
        raise CampaignError("GUI varying diagnostic must select the requested active public slot")
    first, stop = plan["capture_digest_first_sample"], plan["capture_digest_stop_sample"]
    samples = stop - first
    if (not 0 < samples <= 32768 or
            any(not _is_nonnegative_int(diagnostic.get(field)) or diagnostic[field] != expected
                for field, expected in (("observation_first_sample", first), ("observation_stop_sample", stop),
                                        ("observation_samples", samples)))):
        raise CampaignError("GUI varying observation must match the bounded complete capture digest")
    channel = receipt["channel"]
    if diagnostic.get("observation_sha256") != channel["selected_observation_sha256"]:
        raise CampaignError("GUI varying component must use the hashed selected IQ observation")
    public_slots = plan.get("public_slots")
    if not isinstance(public_slots, list) or not all(isinstance(row, dict) for row in public_slots):
        raise CampaignError("GUI varying diagnostic requires the public observation calendar")
    matching = [row for row in public_slots if row.get("index") == selected]
    if (len(matching) != 1 or not _is_nonnegative_int(matching[0].get("index")) or
            matching[0].get("observation") != {"first": first, "stop": stop} or
            matching[0].get("evaluator_transmitting") is not (episode.get("control", "wanted") != "noise")):
        raise CampaignError("GUI varying observation must match its public slot and control")
    if (any(not _is_nonnegative_int(channel.get(field)) or channel[field] != plan["horizon_samples"]
            for field in ("samples", "hashed_samples")) or
            not isinstance(channel.get("input_sha256"), str) or
            re.fullmatch(r"[0-9a-fA-F]{64}", channel["input_sha256"]) is None):
        raise CampaignError("GUI varying diagnostic must render and hash the full original horizon")

    prior = diagnostic.get("prior")
    if (not isinstance(prior, dict) or not isinstance(prior.get("drift_reserved"), bool) or
            not isinstance(prior.get("line_reserved"), bool) or
            prior.get("drift_scored") is not False or prior.get("line_scored") is not False or
            prior.get("coherent_gain_available") is not False or
            not isinstance(prior.get("coherent_gain_status"), str) or not prior["coherent_gain_status"].strip()):
        raise CampaignError("GUI varying diagnostic must retain unavailable coherent/drift/line qualification")
    for field in ("branch_weight", "ordinary_weight", "noise_variance", "frequency_lower_hz", "frequency_upper_hz",
                  "atmospheric_allowance_hz", "clock_bound_ppm", "arrival_lower_seconds", "arrival_upper_seconds",
                  "operational_threshold", "minimum_bit_log_separation"):
        if not _is_finite_number(prior.get(field)):
            raise CampaignError("GUI varying diagnostic requires finite canonical public priors")
    branch_weight = 1 / (2 + prior["drift_reserved"])
    ordinary_weight = .5 if prior["line_reserved"] else 1.
    if (not math.isclose(prior["branch_weight"], branch_weight, rel_tol=1e-12) or
            prior["ordinary_weight"] != ordinary_weight or
            prior["noise_variance"] <= 0 or prior["noise_variance"] != plan.get("noise_variance") or
            prior["frequency_lower_hz"] > prior["frequency_upper_hz"] or
            prior["arrival_lower_seconds"] > prior["arrival_upper_seconds"] or
            prior["atmospheric_allowance_hz"] < 0 or prior["clock_bound_ppm"] < 0 or
            prior["operational_threshold"] <= 0 or prior["minimum_bit_log_separation"] < 0):
        raise CampaignError("GUI varying diagnostic has inconsistent public prior accounting")
    bounds = plan["public_bounds"]
    for field, public_field, sign in (("frequency_lower_hz", "frequency_bound_hz", -1),
            ("frequency_upper_hz", "frequency_bound_hz", 1), ("atmospheric_allowance_hz", "atmospheric_hz", 1),
            ("clock_bound_ppm", "clock_bound_ppm", 1), ("arrival_lower_seconds", "arrival_low_seconds", 1),
            ("arrival_upper_seconds", "arrival_high_seconds", 1)):
        expected = bounds.get(public_field)
        if not _is_finite_number(expected) or not math.isclose(prior[field], sign * expected, rel_tol=1e-10, abs_tol=1e-9):
            raise CampaignError("GUI varying priors must match canonical public bounds without truth narrowing")

    coarse, resources = diagnostic.get("coarse"), diagnostic.get("resources")
    if not isinstance(coarse, dict) or not isinstance(resources, dict):
        raise CampaignError("GUI varying diagnostic requires coarse and resource receipts")
    for field in ("frequency_points", "arrival_points", "workspaces", "total_tiles", "completed_tiles", "workspace_bytes"):
        if not _is_nonnegative_int(coarse.get(field)) or coarse[field] == 0:
            raise CampaignError("GUI varying diagnostic requires completed positive coarse geometry")
    if (coarse["completed_tiles"] != coarse["total_tiles"] or coarse["total_tiles"] > 4096 or
            coarse["workspaces"] * coarse["frequency_points"] > 2**20 or
            not isinstance(coarse.get("fft_precision"), str) or not coarse["fft_precision"].strip()):
        raise CampaignError("GUI varying coarse work exceeds its fixed cap or is incomplete")
    for field in ("workers", "scratch_bound_bytes", "worker_stack_bound_bytes", "metadata_bound_bytes", "render_row_bound"):
        if not _is_nonnegative_int(resources.get(field)):
            raise CampaignError("GUI varying diagnostic requires explicit integer resource bounds")
    if (resources["workers"] == 0 or not 0 < resources["scratch_bound_bytes"] <= 512 * 2**20 or
            not 0 < resources["render_row_bound"] <= 32 * 2**20 or
            coarse["workspace_bytes"] > resources["scratch_bound_bytes"]):
        raise CampaignError("GUI varying diagnostic exceeds its bounded scratch or render work")

    def coordinate(value: Any) -> None:
        if not isinstance(value, dict) or any(not _is_finite_number(value.get(field)) for field in
                ("epoch_offset_seconds", "arrival_seconds", "frequency_hz", "clock_ppm")):
            raise CampaignError("GUI varying diagnostic requires finite public coordinates")

    proposals = coarse.get("proposals")
    if not isinstance(proposals, list) or not proposals:
        raise CampaignError("GUI varying diagnostic requires public coarse proposals")
    for proposal in proposals:
        if (not isinstance(proposal, dict) or not _is_finite_number(proposal.get("score")) or
                not _is_nonnegative_int(proposal.get("bit")) or proposal["bit"] not in (0, 1) or
                not _is_nonnegative_int(proposal.get("rank"))):
            raise CampaignError("GUI varying diagnostic has an invalid public coarse proposal")
        coordinate(proposal.get("coordinate"))
    for field in ("affine_coordinates", "varying_coordinates", "segmented_timing_coordinates", "epoch_timing_coordinates"):
        coordinates = coarse.get(field)
        if not isinstance(coordinates, list):
            raise CampaignError("GUI varying diagnostic requires its public coordinate sets")
        for value in coordinates:
            coordinate(value)

    _validate_gui_score_pair(diagnostic, prior, resources, coarse)
    _validate_gui_clean_comparison(receipt, episode)


def _validate_gui_score_pair(diagnostic: dict[str, Any], prior: dict[str, Any],
                             resources: dict[str, Any], coarse: dict[str, Any]) -> None:
    branch_weight, ordinary_weight = prior["branch_weight"], prior["ordinary_weight"]
    def evidence(value: Any) -> list[float]:
        if not isinstance(value, list) or len(value) != 2 or any(not _is_finite_number(x) for x in value):
            raise CampaignError("GUI varying diagnostic requires finite paired bit evidence")
        return value

    def log_add(a: float, b: float) -> float:
        peak = max(a, b)
        return peak + math.log(math.exp(a - peak) + math.exp(b - peak))

    stages = []
    for name in ("affine", "varying"):
        stage = diagnostic.get(name)
        if not isinstance(stage, dict):
            raise CampaignError("GUI varying diagnostic requires both native component stages")
        for field in ("candidates", "basis_rows", "workspace_bytes"):
            if not _is_nonnegative_int(stage.get(field)) or stage[field] == 0:
                raise CampaignError("GUI varying stage requires positive executed work")
        if (stage["workspace_bytes"] > resources["scratch_bound_bytes"] or
                stage["candidates"] != len(coarse[name + "_coordinates"])):
            raise CampaignError("GUI varying stage work must match admitted coordinates and workspace")
        for field in ("selected_log_prior_mass", "public_grid_log_count", "seconds"):
            if not _is_finite_number(stage.get(field)):
                raise CampaignError("GUI varying stage requires finite work and prior accounting")
        expected_mass = math.log(stage["candidates"]) - stage["public_grid_log_count"]
        if (stage["seconds"] < 0 or stage["public_grid_log_count"] < 0 or
                not math.isclose(stage["selected_log_prior_mass"], expected_mass, rel_tol=1e-10, abs_tol=1e-9)):
            raise CampaignError("GUI varying stage must preserve canonical selected prior mass")
        raw, corrected = evidence(stage.get("raw_bit_log_evidence")), evidence(stage.get("prior_corrected_bit_log_evidence"))
        for before, after in zip(raw, corrected):
            if not math.isclose(after, before + expected_mass + math.log(branch_weight * ordinary_weight), rel_tol=1e-10, abs_tol=1e-9):
                raise CampaignError("GUI varying stage must retain its public-grid, inner-branch and outer-line prior penalties")
        if (not _is_finite_number(stage.get("log_presence")) or
                not math.isclose(stage["log_presence"], log_add(*corrected) - math.log(2), rel_tol=1e-10, abs_tol=1e-9)):
            raise CampaignError("GUI component presence must match its prior-corrected evidence")
        if name == "varying":
            for field in ("blocks", "block_rows"):
                if not _is_nonnegative_int(stage.get(field)) or stage[field] == 0:
                    raise CampaignError("GUI varying stage requires positive short-block geometry")
            if not _is_nonnegative_int(stage.get("frequency_states")):
                raise CampaignError("GUI varying frequency-state count must report actual nonnegative work")
            if (stage["basis_rows"] > resources["render_row_bound"] or
                    not isinstance(stage.get("status"), str) or not stage["status"].strip()):
                raise CampaignError("GUI varying stage must report its bounded executed model status")
            admitted = stage.get("diagnostic_admits_data")
            bit = stage.get("diagnostic_data_bit")
            if (not isinstance(admitted, bool) or
                    (admitted and (not _is_nonnegative_int(bit) or bit not in (0, 1))) or
                    (not admitted and bit is not None)):
                raise CampaignError("GUI varying local admission must be a typed component diagnostic")
        stages.append(corrected)
    combined = diagnostic.get("combined")
    if not isinstance(combined, dict):
        raise CampaignError("GUI varying diagnostic requires combined component evidence")
    scores = evidence(combined.get("prior_corrected_bit_log_evidence"))
    if any(not math.isclose(value, log_add(a, b), rel_tol=1e-10, abs_tol=1e-9)
           for value, a, b in zip(scores, *stages)):
        raise CampaignError("GUI varying combined evidence must preserve both weighted component branches")
    if (not _is_finite_number(combined.get("log_presence")) or
            not math.isclose(combined["log_presence"], log_add(*scores) - math.log(2), rel_tol=1e-10, abs_tol=1e-9)):
        raise CampaignError("GUI varying combined presence must match its paired component evidence")


def _validate_gui_clean_comparison(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    diagnostic, plan = receipt["hf_gui_varying_diagnostic"], receipt["plan"]
    if not episode.get("hf_gui_clean_diagnostic", False):
        if "clean_comparison" in diagnostic:
            raise CampaignError("unrequested clean GUI comparison")
        return
    clean = diagnostic.get("clean_comparison")
    if (not isinstance(clean, dict) or not _is_nonnegative_int(clean.get("schema_version")) or clean["schema_version"] != 1 or
            clean.get("used_for_receiver_decisions") is not False or
            clean.get("transmitter_key_evaluator_only") is not True or
            clean.get("public_acquisition_input") != "noisy_observation" or
            clean.get("within_key_same_coordinates_for_clean_noisy") is not True or
            clean.get("clean_channel_noise_enabled") is not False or clean.get("noisy_clean_validity_equal") is not True):
        raise CampaignError("clean GUI comparison must retain versioned evaluator-only noisy-acquisition scope")
    control = episode.get("control", "wanted")
    key_count = 2 if control == "wrong-key" else 1
    if "quiet_control_clean_is_zero" not in clean or "wanted_bit" not in clean:
        raise CampaignError("clean GUI comparison must explicitly report nullable evaluator fields")
    if not _is_nonnegative_int(clean.get("coarse_searches")) or clean["coarse_searches"] != key_count:
        raise CampaignError("clean GUI comparison must acquire once from noisy IQ for each declared key")
    for field in ("clean_input_sha256", "clean_observation_sha256"):
        if not isinstance(clean.get(field), str) or re.fullmatch(r"[0-9a-fA-F]{64}", clean[field]) is None:
            raise CampaignError("clean GUI comparison must bind full capture and selected-observation hashes")
    for field, value in (("clean_hashed_samples", plan["horizon_samples"]),
                         ("clean_observation_hashed_samples", diagnostic["observation_samples"])):
        if not _is_nonnegative_int(clean.get(field)) or clean[field] != value:
            raise CampaignError("clean GUI comparison must render the same complete public horizon and observation")
    for field in ("clean_energy_sample_sum", "difference_energy_sample_sum"):
        if not _is_finite_number(clean.get(field)) or clean[field] < 0:
            raise CampaignError("clean GUI comparison energies must be finite nonnegative measurements")
    if control == "noise":
        if clean.get("quiet_control_clean_is_zero") is not True or clean["clean_energy_sample_sum"] != 0 or clean.get("wanted_bit") is not None:
            raise CampaignError("noise-only clean GUI comparison must retain its zero-signal evaluator control")
    elif (clean.get("quiet_control_clean_is_zero") is not None or not _is_nonnegative_int(clean.get("wanted_bit")) or
            clean["wanted_bit"] not in (0, 1)):
        raise CampaignError("active clean GUI comparison must report the evaluator's transmitted bit")
    resources = clean.get("resources")
    integer_fields = ("coarse_workspace_bound_bytes", "fine_pair_count", "varying_render_row_bound", "varying_render_rows",
                      "selected_capture_bytes", "extra_chunk_bytes", "clean_source_working_bytes", "native_source_channel_bound_bytes")
    if not isinstance(resources, dict) or any(not _is_nonnegative_int(resources.get(field)) for field in integer_fields):
        raise CampaignError("clean GUI comparison requires explicit bounded work and memory receipts")
    bounds = resources.get("key_render_row_bounds")
    source_bound = 65 * 2**20
    if (not isinstance(bounds, list) or len(bounds) != key_count or
            any(not _is_nonnegative_int(value) or value == 0 for value in bounds) or
            bounds[0] != diagnostic["resources"]["render_row_bound"] or
            resources["fine_pair_count"] != 2 * key_count or
            resources["varying_render_row_bound"] != 2 * sum(bounds) or
            not 0 < resources["varying_render_row_bound"] <= 32 * 2**20 or
            resources["selected_capture_bytes"] != 16 * diagnostic["observation_samples"] or
            resources["extra_chunk_bytes"] != 17 * episode.get("chunk", 4096) or
            resources["native_source_channel_bound_bytes"] != source_bound or
            not 0 < resources["clean_source_working_bytes"] <= source_bound or
            plan.get("native_source_channel_preflight_bound_bytes") != 2 * source_bound or
            not diagnostic["coarse"]["workspace_bytes"] <= resources["coarse_workspace_bound_bytes"] <= diagnostic["resources"]["scratch_bound_bytes"]):
        raise CampaignError("clean GUI comparison must preserve its additional source, capture and aggregate render reservations")
    keys = clean.get("keys")
    if not isinstance(keys, list) or len(keys) != key_count:
        raise CampaignError("clean GUI comparison requires only its declared receiver and counterfactual transmitter keys")
    rendered = diagnostic["varying"]["basis_rows"]
    for index, row in enumerate(keys):
        role = "receiver" if index == 0 else "transmitter"
        if not isinstance(row, dict) or row.get("role") != role:
            raise CampaignError("clean GUI comparison key order must preserve receiver and evaluator transmitter roles")
        coarse = diagnostic["coarse"] if index == 0 else row.get("coarse")
        if index == 0:
            if "coarse" not in row or "noisy" not in row or row["coarse"] is not None or row["noisy"] is not None:
                raise CampaignError("receiver clean comparison must reuse the original noisy coarse and score receipts")
        else:
            if not isinstance(coarse, dict):
                raise CampaignError("transmitter comparison requires its own noisy-input public acquisition receipt")
            for field in ("frequency_points", "arrival_points", "workspaces", "total_tiles", "completed_tiles", "fft_precision"):
                if ((field != "fft_precision" and not _is_nonnegative_int(coarse.get(field))) or
                        coarse.get(field) != diagnostic["coarse"].get(field)):
                    raise CampaignError("counterfactual acquisition must preserve the full public coarse geometry")
            if (not _is_nonnegative_int(coarse.get("workspace_bytes")) or
                    not 0 < coarse["workspace_bytes"] <= resources["coarse_workspace_bound_bytes"]):
                raise CampaignError("counterfactual coarse workspace must remain inside the common admission bound")
            if "seconds" in coarse and (not _is_finite_number(coarse["seconds"]) or coarse["seconds"] < 0):
                raise CampaignError("counterfactual coarse time must be finite and nonnegative")
            proposals = coarse.get("proposals")
            if not isinstance(proposals, list) or not proposals:
                raise CampaignError("counterfactual acquisition requires public noisy proposals")
            coordinates = []
            for proposal in proposals:
                if (not isinstance(proposal, dict) or not _is_finite_number(proposal.get("score")) or
                        not _is_nonnegative_int(proposal.get("bit")) or proposal["bit"] not in (0, 1) or
                        not _is_nonnegative_int(proposal.get("rank"))):
                    raise CampaignError("counterfactual acquisition requires valid public proposal metadata")
                coordinates.append(proposal.get("coordinate"))
            for field in ("affine_coordinates", "varying_coordinates", "segmented_timing_coordinates", "epoch_timing_coordinates"):
                values = coarse.get(field)
                if not isinstance(values, list):
                    raise CampaignError("counterfactual acquisition requires each public coordinate bank")
                coordinates.extend(values)
            if any(not isinstance(value, dict) or any(not _is_finite_number(value.get(field)) for field in
                    ("epoch_offset_seconds", "arrival_seconds", "frequency_hz", "clock_ppm")) for value in coordinates):
                raise CampaignError("counterfactual public coordinates must remain finite")
        pairs = [row.get("clean")] if index == 0 else [row.get("noisy"), row.get("clean")]
        noisy = diagnostic if index == 0 else row.get("noisy")
        for pair in pairs:
            if not isinstance(pair, dict):
                raise CampaignError("clean GUI comparison requires every declared fine score pair")
            _validate_gui_score_pair(pair, diagnostic["prior"], dict(diagnostic["resources"], render_row_bound=bounds[index]), coarse)
            for name in ("affine", "varying"):
                for field in ("workspace_bytes", "public_grid_log_count", "public_frequency_cells") + (
                        ("blocks", "block_rows", "frequency_states", "status") if name == "varying" else ()):
                    expected = diagnostic[name].get(field)
                    if field == "public_frequency_cells" and (not _is_nonnegative_int(expected) or expected == 0):
                        raise CampaignError("clean GUI comparisons require explicit public frequency-cell geometry")
                    if ((field == "public_frequency_cells" and not _is_nonnegative_int(pair[name].get(field))) or
                            pair[name].get(field) != expected):
                        raise CampaignError("clean GUI fine scores must preserve the same native public geometry and admitted workspace")
                for field in ("candidates", "selected_log_prior_mass"):
                    if pair[name].get(field) != noisy[name].get(field):
                        raise CampaignError("clean/noisy scores must share the same noisy-selected within-key coordinates and prior mass")
            rendered += pair["varying"]["basis_rows"]
    if resources["varying_render_rows"] != rendered or rendered > resources["varying_render_row_bound"]:
        raise CampaignError("clean GUI comparison must count all executed fine pairs within its aggregate render bound")


def _validate_fullwave_reflection(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    plan = receipt["plan"]
    scenario = receipt.get("scenario", {})
    configured = scenario.get("channel") if isinstance(scenario, dict) else None
    channel = receipt.get("channel")
    if episode["scenario"] != "hf-fullwave-reflection":
        if ("fullwave_reflection" in plan or plan.get("channel_renderer") == "hf_fullwave_reflection" or
                isinstance(configured, dict) and ("fullwave_reflection" in configured or configured.get("renderer") == "hf_fullwave_reflection") or
                isinstance(channel, dict) and "fullwave_reflection" in channel):
            raise CampaignError("unrequested full-wave reflection descriptor or renderer")
        return
    if (plan.get("channel_renderer") != "hf_fullwave_reflection" or not isinstance(configured, dict) or
            configured.get("renderer") != "hf_fullwave_reflection"):
        raise CampaignError("full-wave reflection must bind its native renderer in plan and scenario")

    def descriptor(value: Any) -> None:
        if not isinstance(value, dict):
            raise CampaignError("full-wave reflection requires its fixed physical descriptor")
        integer_fields = {"modelling_delay_samples", "taps", "first_lag_samples", "last_lag_samples", "filter_halo_samples",
                          "maximum_rf_solves", "preparation_workspace_bound_bytes"}
        for field, expected in FULLWAVE_REFLECTION.items():
            actual = value.get(field)
            if (actual != expected or isinstance(expected, (int, float)) and not _is_finite_number(actual) or
                    field in integer_fields and not _is_nonnegative_int(actual)):
                raise CampaignError("full-wave reflection descriptor differs from the fixed qualified model")

    descriptor(plan.get("fullwave_reflection"))
    descriptor(configured.get("fullwave_reflection"))
    fs, lead = plan.get("sample_rate_hz"), plan.get("capture_preroll_seconds")
    source_stop, horizon = plan.get("source_stop_sample"), plan.get("horizon_samples")
    if (not _is_finite_number(fs) or fs != 1920 or not _is_finite_number(lead) or lead != .25 or
            not _is_nonnegative_int(source_stop) or source_stop < math.ceil((540 + lead) * fs) or
            not _is_nonnegative_int(horizon) or horizon < source_stop + 129 or
            not _is_finite_number(plan.get("represented_seconds")) or
            not math.isclose(plan["represented_seconds"], horizon / fs, rel_tol=1e-12, abs_tol=1e-12) or
            not _is_finite_number(plan.get("channel_tail_seconds")) or plan["channel_tail_seconds"] != 129 / fs):
        raise CampaignError("full-wave reflection must preserve the full source span, finite tail and public capture horizon")
    bounds = plan.get("public_bounds")
    if (not isinstance(bounds, dict) or not _is_finite_number(bounds.get("arrival_low_seconds")) or
            bounds["arrival_low_seconds"] != 1 / fs or not _is_finite_number(bounds.get("arrival_high_seconds")) or
            bounds["arrival_high_seconds"] != 129 / fs or
            not _is_nonnegative_int(bounds.get("receiver_filter_halo_samples")) or bounds["receiver_filter_halo_samples"] != 128):
        raise CampaignError("full-wave reflection must retain the public arrival prior and 128-sample filter halo")
    quiet, count = plan.get("first_uncontaminated_quiet_slot"), plan.get("scored_slot_horizon")
    slots, wire = plan.get("public_slots"), plan.get("wire_bits")
    if (not _is_nonnegative_int(wire) or wire == 0 or not _is_nonnegative_int(quiet) or quiet < wire or
            not _is_nonnegative_int(count) or count != quiet + 2 or plan.get("planned_uncontaminated_quiet_slots") != 2 or
            not isinstance(slots, list) or len(slots) != count):
        raise CampaignError("full-wave capture must include the complete public message calendar and two uncontaminated quiet observations")
    for index, row in enumerate(slots):
        observation = row.get("observation") if isinstance(row, dict) else None
        if (not isinstance(observation, dict) or not _is_nonnegative_int(row.get("index")) or row["index"] != index or
                any(not _is_nonnegative_int(observation.get(field)) for field in ("first", "stop")) or
                not observation["first"] < observation["stop"] <= horizon or
                index >= quiet and (observation["first"] < source_stop + 129 or row.get("evaluator_transmitting") is not False)):
            raise CampaignError("full-wave capture observations must preserve ordered bounds and the complete reflected tail")
    if slots[-1]["observation"]["stop"] != horizon:
        raise CampaignError("full-wave capture must end after the last complete public observation")
    if (not _is_finite_number(plan.get("cn0_db_hz")) or not -200 <= plan["cn0_db_hz"] <= 200 or
            not _is_finite_number(plan.get("noise_variance")) or
            not math.isclose(plan["noise_variance"], fs / 10 ** (plan["cn0_db_hz"] / 10), rel_tol=1e-10) or
            plan.get("cn0_reference_plane") != "normalized incident DATA before reflected-port loss and fixed additive receiver noise"):
        raise CampaignError("full-wave input CN0 must preserve incident energy and fixed additive receiver noise")
    executed = episode["mode"] != "plan"
    if (plan.get("fullwave_reflection_source_band_checked") is not executed or
            plan.get("fullwave_reflection_table_built") is not executed):
        raise CampaignError("full-wave plan must distinguish unexecuted planning from actual source and table qualification")
    if not executed:
        if isinstance(channel, dict) and "fullwave_reflection" in channel:
            raise CampaignError("full-wave planning must not claim executed table or channel qualification")
        return
    if (not isinstance(channel, dict) or any(not _is_nonnegative_int(channel.get(field)) or channel[field] != horizon
            for field in ("samples", "hashed_samples")) or
            not _is_nonnegative_int(channel.get("source_samples")) or channel["source_samples"] != source_stop or
            not isinstance(channel.get("input_sha256"), str) or re.fullmatch(r"[0-9a-fA-F]{64}", channel["input_sha256"]) is None or
            not _is_finite_number(channel.get("tail_seconds")) or channel["tail_seconds"] != 129 / fs or
            not _is_finite_number(channel.get("maximum_delay_seconds")) or channel["maximum_delay_seconds"] != 1 / fs):
        raise CampaignError("full-wave execution must hash the whole planned capture and report actual source and tail extent")
    full = channel.get("fullwave_reflection")
    descriptor(full)
    if (full.get("table_qualified") is not True or full.get("witnesses_qualified") is not True or
            not isinstance(full.get("table_sha256"), str) or re.fullmatch(r"[0-9a-fA-F]{64}", full["table_sha256"]) is None or
            full.get("table_digest_scope") != "both ports in reflection/transmission order, ascending profile knots and integer lags, little-endian complex binary64 taps"):
        raise CampaignError("full-wave execution requires qualified table witnesses and exact table-hash provenance")
    for field, expected in (("rf_solves", 32070), ("knot_profiles_checked", 65), ("holdout_profiles_checked", 128)):
        if not _is_nonnegative_int(full.get(field)) or full[field] != expected:
            raise CampaignError("full-wave qualification must report every bounded RF solve and profile witness")
    for field, maximum in (("maximum_profile_holdout_absolute_error", .005),
                           ("maximum_witness_frequency_absolute_error", .005),
                           ("maximum_witness_mesh_absolute_error", .005), ("maximum_discarded_energy_fraction", 1e-6)):
        values = full.get(field)
        if not isinstance(values, list) or len(values) != 2 or any(not _is_finite_number(value) or not 0 <= value <= maximum for value in values):
            raise CampaignError("full-wave numerical qualification cannot accept missing, unknown or out-of-bound port errors")
    flux_limit = (math.sqrt(1 + 1e-8) + math.sqrt(2) * .005) ** 2 - 1
    for field, maximum in (("maximum_frozen_flux_excess", flux_limit), ("retarded_peak_error_bound", .005)):
        if not _is_finite_number(full.get(field)) or not 0 <= full[field] <= maximum:
            raise CampaignError("full-wave frozen-flux and retarded-coefficient error gates must both pass")
    for field in ("carrier_flight_phase_bound_radians", "preparation_seconds"):
        if not _is_finite_number(full.get(field)) or full[field] < 0:
            raise CampaignError("full-wave preparation time and carrier-flight proxy must remain finite explicit diagnostics")
    timing = receipt.get("timing")
    if (not isinstance(timing, dict) or any(not _is_finite_number(timing.get(field)) for field in
            ("channel_preparation_seconds", "wall_seconds")) or
            not full["preparation_seconds"] <= timing["channel_preparation_seconds"] <= timing["wall_seconds"] or
            timing.get("isolated_receiver_service_measured") is not False or timing.get("capacity_qualified") is not False):
        raise CampaignError("full-wave table construction must remain charged to preparation and total wall time, without a receiver service claim")
    low, high = full.get("source_minimum_frequency_hz"), full.get("source_maximum_frequency_hz")
    if not _is_finite_number(low) or not _is_finite_number(high) or not -768 <= low <= high <= 768:
        raise CampaignError("executed full-wave source spectrum must remain within the qualified occupied band")
    for field in ("table_workspace_bytes", "stream_workspace_bytes"):
        if not _is_nonnegative_int(full.get(field)) or full[field] == 0:
            raise CampaignError("full-wave qualification requires actual positive table and stream workspace")
    if (full["table_workspace_bytes"] + full["stream_workspace_bytes"] > 1048576 or
            not _is_nonnegative_int(channel.get("workspace_bytes")) or channel["workspace_bytes"] < 1048576):
        raise CampaignError("full-wave table and stream workspace must fit the admitted preparation bound")
    regions = full.get("regions")
    if not isinstance(regions, list) or len(regions) != 3:
        raise CampaignError("full-wave execution must retain before, transition and after capture accounting")
    sums = [0., 0.]
    for region, name, samples in zip(regions, ("before", "transition", "after"), (115232, 230400, horizon - 345632)):
        if (not isinstance(region, dict) or region.get("name") != name or
                not _is_nonnegative_int(region.get("samples")) or region["samples"] != samples):
            raise CampaignError("full-wave regional samples must cover the complete delayed transition and capture")
        for index, field in enumerate(("incident_energy_sample_sum", "reflected_energy_sample_sum")):
            if not _is_finite_number(region.get(field)) or region[field] < 0:
                raise CampaignError("full-wave regional energies must be finite nonnegative measurements")
            sums[index] += region[field]
    for value, field in zip(sums, ("input_energy_sample_sum", "signal_energy_sample_sum")):
        if not _is_finite_number(channel.get(field)) or not math.isclose(value, channel[field], rel_tol=1e-10, abs_tol=1e-9):
            raise CampaignError("full-wave regional energy accounting must match complete channel totals")
    if episode.get("control", "wanted") == "noise" and (any(value != 0 for value in sums) or
            channel["input_energy_sample_sum"] != 0 or channel["signal_energy_sample_sum"] != 0 or
            channel.get("contains_source_energy") is not False or channel.get("contains_signal_energy") is not False):
        raise CampaignError("full-wave noise control must contain exactly zero incident and reflected clean energy")


def _gannon_trace_descriptor(hour: int, offset: float) -> dict[str, Any]:
    epoch, source_hash, iq_hash, _ = GANNON_RECORDS[hour]
    return {
        "dataset": "HamSCI PSWS HF Doppler observations of the 2024 Gannon geomagnetic storm",
        "dataset_doi": "10.5281/zenodo.20466479", "samples": 36000, "sample_rate_hz": 10,
        "support_seconds": 3599.9, "part": "measured-complex", "record_offset_seconds": offset,
        "remaining_support_seconds": 3599.9-offset,
        "record_clock_mapping": "record_time = output_capture_time + record_offset_seconds; no repetition or extrapolation",
        "source_sha256": source_hash, "source_hdf5_sha256": source_hash, "serialized_iq_sha256": iq_hash,
        "record_utc_first_sample_epoch_seconds": epoch, "capture_utc_first_sample_epoch_seconds": epoch+offset,
        "utc_hour": hour, "stream_uuid": "0e51e045ca1d42868f3c9cd483112bd7",
        "source_archive_member": f"data/psws_grapeDRF/ch0_W2NAF_2024-05-10/2024-05-10T00-00-00/rf@{epoch}.000.h5",
        "source_subchannels": 9, "zero_based_column": 4, "nominal_center_frequency_hz": 10000000,
        "timing_assumption": "recorded Digital RF 10/1 sample-rate ratio and contiguous sample indices establish the stored UTC grid; independent clock metrology remains unqualified",
        "normalization_scope": "one joint RMS across both complete fixed 17 and 20 UTC records (72000 knots); no separate hour or observation normalization",
        "normalization_cohort_utc_hours": [17, 20], "normalization_cohort_knots": 72000,
        "normalization_preserves_inter_record_power_ratio": True,
        "statistics_scope": "selected observed complex voltage record after the shared two-record scale; includes beacon, receiver noise and interference",
        "observation_model": "recorded voltage = beacon response + receiver noise/interference; the replay multiplies the modem waveform by this voltage",
        "recorded_noise_becomes_signal_proportional": True,
        "transmitter_identification": "nominal 10MHz may contain both WWV and WWVH; no transmitter separation",
        "noise_free_channel_response": False, "wideband_channel_response_qualified": False,
        "field_false_alarm_or_availability_qualified": False, "absolute_link_level_qualified": False,
        "receiver_clock_discipline_qualified": False,
    }


def _validate_gannon_replay(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    if episode["scenario"] not in GANNON_REPLAY:
        return
    hour, offset = GANNON_REPLAY[episode["scenario"]]
    plan, scenario = receipt["plan"], receipt.get("scenario")
    configured = scenario.get("channel") if isinstance(scenario, dict) else None
    paths = configured.get("paths") if isinstance(configured, dict) else None
    if (not isinstance(paths, list) or len(paths) != 1 or configured.get("path_count") != 1 or
            configured.get("rf_hz") != 10000000 or not isinstance(paths[0], dict) or
            paths[0].get("power") != 1 or paths[0].get("delay_seconds") != .001):
        raise CampaignError("Gannon replay requires the fixed nominal 10MHz observed-voltage path and synthetic 1ms delay")
    hf = paths[0].get("hf")
    measured = hf.get("measured_envelope") if isinstance(hf, dict) else None
    if not isinstance(measured, dict) or hf.get("measured_offset_seconds") != offset:
        raise CampaignError("Gannon replay is missing its selected measured record and offset")
    for key, expected in _gannon_trace_descriptor(hour, offset).items():
        actual = measured.get(key)
        if (actual != expected or isinstance(expected, bool) and actual is not expected or
                isinstance(expected, (int, float)) and not isinstance(expected, bool) and not _is_finite_number(actual)):
            raise CampaignError(f"Gannon measured-voltage provenance or scope differs: {key}")
    raw_power = GANNON_RECORDS[hour][3]/36000
    gain = math.sqrt(72000/sum(record[3] for record in GANNON_RECORDS.values()))
    for key, expected in (("raw_mean_power", raw_power), ("normalization_gain", gain)):
        if not _is_finite_number(measured.get(key)) or not math.isclose(measured[key], expected, rel_tol=2e-12):
            raise CampaignError("Gannon replay must preserve the fixed raw power and shared two-hour RMS gain")
    adequate = scenario.get("numerical_adequacy")
    if (not isinstance(adequate, dict) or adequate.get("qualification_horizon_seconds") != 3599.9-offset or
            adequate.get("empirical_validation") is not False or adequate.get("empirical_record_provenance_verified") is not True or
            adequate.get("empirical_channel_calibration") is not False or
            adequate.get("scope") != "finite observed-voltage Cartesian interpolation on the verified stored UTC grid; recorded noise becomes signal-proportional" or
            scenario.get("physical_level_connected") is not False or
            plan.get("channel_renderer") != "observed_voltage_multiplicative_replay" or
            plan.get("cn0_reference_plane") != "incident normalized modem waveform before jointly scaled observed-voltage multiplication; independent synthetic white receiver noise" or
            plan.get("measured_noise_is_multiplicative") is not True or plan.get("field_noise_control_qualified") is not False):
        raise CampaignError("Gannon replay must distinguish observed voltage from calibrated field-channel or noise evidence")
    if episode["mode"] == "plan":
        return
    fs = plan.get("sample_rate_hz")
    if not _is_finite_number(fs) or fs <= 0:
        raise CampaignError("Gannon executed replay requires a finite positive waveform sample rate")
    if episode["mode"] == "oracle":
        oracle, presence = receipt.get("oracle"), receipt.get("presence_oracle")
        if (not isinstance(oracle, dict) or not isinstance(presence, dict) or
                oracle.get("kind") != "isolated single DATA bit, equal-prior binary known observed-voltage multiplier and synthetic added white Gaussian noise" or
                oracle.get("conditioning_scope") != "known deterministic observed-voltage multiplier with added synthetic white noise; the recorded beacon/noise/interference are inseparable" or
                presence.get("kind") != "isolated single DATA bit under a known observed-voltage multiplier versus synthetic added white Gaussian noise" or
                presence.get("conditioning_scope") != "the recorded beacon/noise/interference are inseparable; no same-input full-message or physical noise-free channel claim"):
            raise CampaignError("Gannon oracle must retain its conditional observed-voltage and added-noise meaning")
        for row in (oracle, presence):
            if any(row.get(key) is not False for key in ("same_input_full_message_replay", "physical_noise_free_channel_known", "field_sensitivity_bound")):
                raise CampaignError("Gannon isolated oracle cannot claim full-message or physical field sensitivity")
        if (oracle.get("includes_acquisition_or_completion") is not False or
                oracle.get("finite_blocklength_capacity_bound") is not False or oracle.get("realization_normalized") is not False):
            raise CampaignError("Gannon oracle cannot normalize the record or qualify acquisition, completion or capacity")
        samples = oracle.get("samples")
    else:
        channel = receipt.get("channel")
        samples = channel.get("samples") if isinstance(channel, dict) else None
        if episode.get("control", "wanted") == "noise" and (not isinstance(channel, dict) or
                channel.get("input_energy_sample_sum") != 0 or channel.get("signal_energy_sample_sum") != 0 or
                channel.get("contains_source_energy") is not False or channel.get("contains_signal_energy") is not False):
            raise CampaignError("Gannon synthetic noise control requires exactly zero modem source and multiplicative signal")
    if not _is_nonnegative_int(samples) or samples == 0 or (samples-1)/fs > 3599.9-offset:
        raise CampaignError("Gannon replay exceeds the finite selected record; looping or extrapolation is not admitted")


def _validate_magnetoionic_null_witness(value: Any) -> None:
    if (not isinstance(value, dict) or value.get("q") != (1+math.sqrt(3)/2)/16 or
            value.get("rf_offset_hz") != -118.7 or value.get("witness_meshes_m") != [10.,5.,2.5] or
            value.get("x_local_relative_tolerance") != .1 or value.get("x_resolved") is not True or
            value.get("scope") != "separate original near-null witness; sampled mesh differences and a point-specific practical interpolation guard, not a uniform physical error bound"):
        raise CampaignError("parallel magnetic table requires the fixed original-null mesh witness and local guard")
    fields=("fine_amplitude_xy","fir_amplitude_xy","fir_to_10m_absolute_error_xy",
            "mesh_10m_to_2p5m_absolute_difference_xy","mesh_5m_to_2p5m_absolute_difference_xy",
            "fir_to_2p5m_absolute_error_xy","x_guarded_power_interval")
    for field in fields:
        pair=value.get(field)
        if not isinstance(pair,list) or len(pair)!=2 or any(not _is_finite_number(x) or x<0 for x in pair):
            raise CampaignError("parallel magnetic null witness requires finite nonnegative two-component diagnostics")
    reference=value["fine_amplitude_xy"][0]
    guarded=value["fir_to_2p5m_absolute_error_xy"][0]+value["mesh_5m_to_2p5m_absolute_difference_xy"][0]
    if reference<=0 or guarded>.1*reference:
        raise CampaignError("parallel magnetic original-null error exceeds the fixed local amplitude guard")
    expected={"fine_vector_power":sum(x*x for x in value["fine_amplitude_xy"]),
              "x_guarded_absolute_error":guarded,"x_guarded_relative_error":guarded/reference}
    for field,target in expected.items():
        if not _is_finite_number(value.get(field)) or not math.isclose(value[field],target,rel_tol=1e-10,abs_tol=1e-20):
            raise CampaignError("parallel magnetic null witness derived error or power is inconsistent")
    expected_power=(max(0,reference-guarded)**2,(reference+guarded)**2)
    if any(not math.isclose(actual,target,rel_tol=1e-10,abs_tol=1e-20)
           for actual,target in zip(value["x_guarded_power_interval"],expected_power)):
        raise CampaignError("parallel magnetic null witness power interval is inconsistent")


def _validate_magnetoionic_reflection(receipt: dict[str, Any], episode: dict[str, Any]) -> None:
    """Require the fixed vector-plasma model and its actual numerical gates."""
    plan = receipt["plan"]
    scenario = receipt.get("scenario", {})
    configured = scenario.get("channel") if isinstance(scenario, dict) else None
    channel = receipt.get("channel")
    requested = episode["scenario"] in MAGNETOIONIC_REFLECTION
    if not requested:
        if ("magnetoionic_reflection" in plan or plan.get("channel_renderer") == "hf_magnetoionic_reflection" or
                isinstance(configured, dict) and ("magnetoionic_reflection" in configured or
                    configured.get("renderer") == "hf_magnetoionic_reflection") or
                isinstance(channel, dict) and "magnetoionic_reflection" in channel):
            raise CampaignError("unrequested magnetoionic reflection descriptor or renderer")
        return

    component, transition_first, transition_seconds, spread = MAGNETOIONIC_REFLECTION[episode["scenario"]]
    parallel = episode["scenario"].startswith("hf-magnetoionic-parallel-")
    oracle_mode = episode["mode"] == "oracle"
    intervals, rf_solves = (64, 12039) if parallel else (16, 3444)
    magnetic_field = [0., 0., 50e-6] if parallel else MAGNETOIONIC_DESCRIPTOR["magnetic_field_tesla"]
    renderer = "hf_magnetoionic_reflection"
    if (plan.get("channel_renderer") != renderer or not isinstance(configured, dict) or
            configured.get("renderer") != renderer):
        raise CampaignError("magnetoionic reflection must bind its native renderer in plan and scenario")

    def descriptor(value: Any) -> None:
        if not isinstance(value, dict):
            raise CampaignError("magnetoionic reflection requires its fixed physical and numerical descriptor")
        expected = dict(MAGNETOIONIC_DESCRIPTOR)
        expected.update(magnetic_field_tesla=magnetic_field, profile_intervals=intervals, maximum_rf_solves=rf_solves)
        if value.get("physical_path", "tilted_lower") != ("parallel_upper" if parallel else "tilted_lower"):
            raise CampaignError("magnetoionic descriptor differs from the requested physical path")
        expected.update({"receive_component": component,
                         "receive_polarization": "fixed_laboratory_x" if component == 0 else "fixed_laboratory_y",
                         "transition_first_seconds": transition_first,
                         "transition_seconds": transition_seconds,
                         "minimum_complete_source_seconds": transition_first + transition_seconds})
        endpoint_profiles = {name: expected.pop(name) for name in ("profile_q0", "profile_q1")}
        integer_fields = {"carrier_hz", "sample_rate_hz", "occupied_half_band_hz", "modelling_delay_samples",
                          "taps_per_output", "first_lag_samples", "last_lag_samples", "filter_halo_samples",
                          "profile_intervals", "frequency_bins", "maximum_rf_solves", "preparation_workspace_bound_bytes",
                          "receive_component"}
        for field, wanted in expected.items():
            actual = value.get(field)
            if (actual != wanted or isinstance(wanted, (int, float)) and not isinstance(wanted, bool) and
                    not _is_finite_number(actual) or
                    field in integer_fields and not _is_nonnegative_int(actual)):
                raise CampaignError("magnetoionic descriptor differs from the fixed qualified physical path")
        b = value.get("magnetic_field_tesla")
        if not isinstance(b, list) or len(b) != 3 or any(not _is_finite_number(x) for x in b):
            raise CampaignError("magnetoionic descriptor must retain its fixed geomagnetic vector")
        if b != magnetic_field:
            raise CampaignError("magnetoionic descriptor differs from the fixed qualified geomagnetic vector")
        for name, multiplier in (("profile_q0", .9999), ("profile_q1", 1.0001)):
            profile = value.get(name)
            if (not isinstance(profile, dict) or set(profile) !=
                    {"f_peak_plasma_frequency_hz", "f_collision_per_second", "d_peak_electron_density_per_m3"} or
                    not _is_finite_number(profile.get("f_peak_plasma_frequency_hz")) or
                    not _is_finite_number(profile.get("f_collision_per_second")) or
                    profile.get("f_collision_per_second") != 60. or
                    profile.get("d_peak_electron_density_per_m3") != 8e8):
                raise CampaignError("magnetoionic descriptor requires the fixed coupled cold-plasma endpoint profiles")
            electron_charge, electron_mass = 1.602176634e-19, 9.1093837139e-31
            b_magnitude = math.sqrt(sum(x*x for x in magnetic_field))
            gyro_hz = electron_charge*b_magnitude/(2*math.pi*electron_mass)
            cutoff = math.sqrt(1 + (1 if parallel else -1)*gyro_hz/MAGNETOIONIC_DESCRIPTOR["carrier_hz"])*MAGNETOIONIC_DESCRIPTOR["carrier_hz"]
            if not math.isclose(profile["f_peak_plasma_frequency_hz"], cutoff*multiplier,
                                rel_tol=2e-12, abs_tol=1e-7):
                raise CampaignError("magnetoionic endpoint misses the selected fixed cutoff crossing")

    planned = plan.get("magnetoionic_reflection")
    descriptor(planned)
    descriptor(configured.get("magnetoionic_reflection"))
    if planned != configured.get("magnetoionic_reflection"):
        raise CampaignError("magnetoionic plan and scenario descriptors differ")
    fs = MAGNETOIONIC_DESCRIPTOR["sample_rate_hz"]
    preroll, source_stop, horizon = plan.get("capture_preroll_seconds"), plan.get("source_stop_sample"), plan.get("horizon_samples")
    if (not _is_finite_number(preroll) or preroll != .25 or not _is_nonnegative_int(source_stop) or
            not oracle_mode and source_stop < math.ceil((transition_first + transition_seconds + preroll)*fs) or
            not _is_nonnegative_int(horizon) or horizon < source_stop + 81 or
            not _is_finite_number(plan.get("represented_seconds")) or
            not math.isclose(plan["represented_seconds"], horizon/fs, rel_tol=1e-12, abs_tol=1e-12) or
            not _is_finite_number(plan.get("channel_tail_seconds")) or
            not math.isclose(plan["channel_tail_seconds"], 81/fs, rel_tol=0, abs_tol=4*math.ulp(81/fs))):
        raise CampaignError("magnetoionic reflection must retain the full physical transition, causal FIR tail and capture horizon")
    if (plan.get("channel_tail_seconds_scope") != "complete causal reflected Jones-column FIR support, from first input through the last retained lag" or
            plan.get("cn0_reference_plane") != "normalized incident DATA before physical reflection and fixed antenna projection; fixed additive receiver noise" or
            plan.get("magnetoionic_reflection_source_band_checked") is not (episode["mode"] != "plan") or
            plan.get("magnetoionic_reflection_table_built") is not (episode["mode"] != "plan") or
            plan.get("receiver_dispersive_model") != ("native_sparse_delay_anchors" if spread else "native_single_ray") or
            plan.get("physical_kernel_supplied_to_receiver") is not False):
        raise CampaignError("magnetoionic plan must distinguish incident power, unexecuted work and receiver model scope")
    if plan.get("complete_transition_span_required", True) is not (not oracle_mode):
        raise CampaignError("magnetoionic plan must distinguish full-transition reception from the isolated oracle")
    bounds = plan.get("public_bounds")
    if (not isinstance(bounds, dict) or bounds.get("arrival_low_seconds") != 1/fs or
            bounds.get("arrival_high_seconds") != 81/fs or bounds.get("receiver_filter_halo_samples") != 80 or
            bounds.get("spread_seconds") != spread or bounds.get("time_varying") is not True or
            bounds.get("frequency_bound_hz") != 0 or bounds.get("atmospheric_hz") != 1):
        raise CampaignError("magnetoionic receive priors must match the full retained lag support and fixed receiver model")
    if (plan.get("planned_uncontaminated_quiet_slots") != 2 or
            not _is_nonnegative_int(plan.get("wire_bits")) or plan["wire_bits"] == 0 or
            not _is_nonnegative_int(plan.get("first_uncontaminated_quiet_slot")) or
            plan["first_uncontaminated_quiet_slot"] < plan["wire_bits"] or
            plan.get("scored_slot_horizon") != plan["first_uncontaminated_quiet_slot"] + 2 or
            not isinstance(plan.get("public_slots"), list) or
            len(plan["public_slots"]) != plan["scored_slot_horizon"]):
        raise CampaignError("magnetoionic message plan must retain its complete calendar and two quiet opportunities")
    quiet = plan["first_uncontaminated_quiet_slot"]
    for index, row in enumerate(plan["public_slots"]):
        obs = row.get("observation") if isinstance(row, dict) else None
        if (not isinstance(obs, dict) or row.get("index") != index or
                not _is_nonnegative_int(obs.get("first")) or not _is_nonnegative_int(obs.get("stop")) or
                not obs["first"] < obs["stop"] <= horizon or
                index >= quiet and (obs["first"] < source_stop + 81 or row.get("evaluator_transmitting") is not False)):
            raise CampaignError("magnetoionic public slots must preserve physical tail and ordered quiet completion")
    if (plan["public_slots"][-1]["observation"]["stop"] != horizon or
            not _is_finite_number(plan.get("cn0_db_hz")) or not -200 <= plan["cn0_db_hz"] <= 200 or
            not _is_finite_number(plan.get("noise_variance")) or
            not math.isclose(plan["noise_variance"], fs/10**(plan["cn0_db_hz"]/10), rel_tol=1e-10) or
            plan.get("message") != episode.get("message", "e")):
        raise CampaignError("magnetoionic capture must preserve the declared incident C/N0, message and full calendar")

    if episode["mode"] == "plan":
        if isinstance(channel, dict) and "magnetoionic_reflection" in channel:
            raise CampaignError("magnetoionic planning cannot claim executed numerical or channel evidence")
        return
    def validate_rendered_channel(channel: Any, horizon: int, source_stop: int) -> dict[str, Any]:
        if (not isinstance(channel, dict) or channel.get("samples") != horizon or
                channel.get("source_samples") != source_stop or
                not _is_finite_number(channel.get("tail_seconds")) or
                not math.isclose(channel["tail_seconds"], 81/fs, rel_tol=0, abs_tol=4*math.ulp(81/fs)) or
                channel.get("maximum_delay_seconds") != 1/fs):
            raise CampaignError("magnetoionic execution must hash the full capture and report its full source and causal extent")
        if not oracle_mode and (channel.get("hashed_samples") != horizon or
                not isinstance(channel.get("input_sha256"), str) or re.fullmatch(r"[0-9a-fA-F]{64}", channel["input_sha256"]) is None):
            raise CampaignError("magnetoionic receiver execution must hash the complete capture")
        full = channel.get("magnetoionic_reflection")
        descriptor(full)
        if (full.get("table_qualified") is not True or full.get("witnesses_qualified") is not True or
                full.get("qualification_scope") != "sampled frozen vector RF/profile and mesh witnesses; finite-FIR retarded coefficient bound; no evolving-Maxwell, 3D scattering or field calibration" or
                not isinstance(full.get("table_sha256"), str) or re.fullmatch(r"[0-9a-fA-F]{64}", full["table_sha256"]) is None or
                full.get("table_digest_scope") != "reflected laboratory x/y coefficients for fixed x launch, ascending component/profile knot/integer lag, little-endian complex binary64 taps; interpolation additionally bound by source identity"):
            raise CampaignError("magnetoionic execution requires qualified vector-table witnesses and exact digest provenance")
        for field, expected_value in (("rf_solves", rf_solves), ("knot_profiles_checked", intervals+1), ("holdout_profiles_checked", 2*intervals)):
            if not _is_nonnegative_int(full.get(field)) or full[field] != expected_value:
                raise CampaignError("magnetoionic qualification must report its fixed bounded RF solve and profile counts")
        for field in ("knot_grid_error", "knot_offgrid_error", "profile_holdout_error", "witness_frequency_error", "witness_mesh_error"):
            values = full.get(field)
            if (not isinstance(values, list) or len(values) != 2 or any(not isinstance(v, dict) or
                    not _is_finite_number(v.get("maximum")) or not 0 <= v["maximum"] <= .005 or
                    not _is_finite_number(v.get("profile_q")) or not 0 <= v["profile_q"] <= 1 or
                    not _is_finite_number(v.get("offset_hz")) or abs(v["offset_hz"]) > fs/2 for v in values)):
                raise CampaignError("magnetoionic sampled response errors require finite in-gate two-component witnesses")
        scale_scopes = {"occupied band raw independent solver amplitude versus approximation; scale diagnostics add no relative-error gate or gain normalization; the separate original-null witness has a point-specific guard"}
        if not parallel:
            scale_scopes.add("occupied band raw independent solver amplitude versus approximation; no relative-error gate or gain normalization")
        if full.get("scale_diagnostic_scope") not in scale_scopes:
            raise CampaignError("magnetoionic amplitude-scale diagnostics must retain their occupied-band scope")

        def validate_scale_diagnostics(field: str, expected_samples: int) -> None:
            rows = full.get(field)
            if not isinstance(rows, list) or len(rows) != 2:
                raise CampaignError("magnetoionic raw-amplitude diagnostics require both fixed laboratory components")
            for row in rows:
                if (not isinstance(row, dict) or not _is_nonnegative_int(row.get("samples")) or
                        row["samples"] != expected_samples or
                        not _is_nonnegative_int(row.get("exact_zero_raw")) or
                        not _is_nonnegative_int(row.get("absolute_error_at_least_raw")) or
                        row["exact_zero_raw"] > row["samples"] or
                        row["absolute_error_at_least_raw"] > row["samples"] or
                        row["absolute_error_at_least_raw"] < row["exact_zero_raw"]):
                    raise CampaignError("magnetoionic raw-amplitude witness counts are inconsistent with bounded sampling")
                for witness_name in ("minimum_raw_amplitude", "maximum_relative_error", "raw_at_maximum_absolute_error"):
                    witness = row.get(witness_name)
                    if witness_name == "maximum_relative_error" and row["exact_zero_raw"] == row["samples"]:
                        if witness_name not in row or witness is not None:
                            raise CampaignError("all-zero magnetoionic samples cannot claim a maximum relative-error witness")
                        continue
                    if (not isinstance(witness, dict) or
                            not _is_finite_number(witness.get("raw_amplitude")) or witness["raw_amplitude"] < 0 or
                            not _is_finite_number(witness.get("absolute_error")) or witness["absolute_error"] < 0 or
                            not _is_finite_number(witness.get("q")) or not 0 <= witness["q"] <= 1 or
                            not _is_finite_number(witness.get("rf_offset_hz")) or
                            abs(witness["rf_offset_hz"]) > MAGNETOIONIC_DESCRIPTOR["occupied_half_band_hz"]):
                        raise CampaignError("magnetoionic raw-amplitude witnesses must be finite and within the occupied band")
                    relative = witness.get("relative_error")
                    if (witness_name == "minimum_raw_amplitude" and
                            (witness["raw_amplitude"] == 0) != (row["exact_zero_raw"] > 0) or
                            witness["raw_amplitude"] == 0 and row["exact_zero_raw"] == 0 or
                            witness["raw_amplitude"] > 0 and row["exact_zero_raw"] == row["samples"] or
                            witness_name == "maximum_relative_error" and witness["raw_amplitude"] == 0):
                        raise CampaignError("magnetoionic zero-amplitude witnesses disagree with their sample counts")
                    if witness["raw_amplitude"] == 0:
                        if relative is not None:
                            raise CampaignError("zero-amplitude magnetoionic witnesses cannot claim a relative error")
                    elif (not _is_finite_number(relative) or relative < 0 or
                          not math.isclose(relative, witness["absolute_error"] / witness["raw_amplitude"],
                                           rel_tol=2e-10, abs_tol=1e-12)):
                        raise CampaignError("magnetoionic raw-amplitude relative error does not match its absolute error")

        # observe_scale excludes the six guard-band offsets. The eight fixed
        # occupied anchors and three moving-cutoff offsets stay within ±384 Hz
        # throughout the frozen profile; all 17 offsets still count as RF solves.
        # The upper-cutoff path moves three near-cutoff offsets outside the band
        # at some endpoint profiles; these are the exact frozen 64-interval counts.
        scale_counts = (703, 1395, 27) if parallel else (187, 352, 33)
        for field, expected_samples in zip(("knot_offgrid_scale", "profile_holdout_scale", "witness_mesh_scale"), scale_counts):
            validate_scale_diagnostics(field, expected_samples)
        if parallel:
            _validate_magnetoionic_null_witness(full.get("original_null_witness"))
        elif full.get("original_null_witness") is not None:
            raise CampaignError("tilted magnetic table cannot claim the parallel original-null witness")
        for field, maximum in (("maximum_discarded_energy_fraction", 1e-6), ("retarded_peak_error_bound", .005)):
            values = full.get(field)
            if not isinstance(values, list) or len(values) != 2 or any(not _is_finite_number(v) or not 0 <= v <= maximum for v in values):
                raise CampaignError("magnetoionic discarded-energy and retarded-error gates require finite in-bound values")
        solver_power_tolerance = 1e-8
        reflected_amplitude_bound = math.sqrt(1 + solver_power_tolerance) + math.sqrt(2)*.005
        reflected_power_excess_tolerance = reflected_amplitude_bound*reflected_amplitude_bound - 1
        if (not _is_finite_number(full.get("maximum_frozen_reflected_power_excess")) or
                not 0 <= full["maximum_frozen_reflected_power_excess"] <= reflected_power_excess_tolerance or
                not _is_finite_number(full.get("maximum_solver_output_power")) or
                not 0 <= full["maximum_solver_output_power"] <= 1 + solver_power_tolerance or
                not _is_finite_number(full.get("minimum_longitudinal_permittivity")) or
                not (0 if parallel else 1e-8) <= full["minimum_longitudinal_permittivity"] <= 1 or
                full.get("minimum_carrier_power_xy_total") is None or full.get("maximum_carrier_power_xy_total") is None):
            raise CampaignError("magnetoionic frozen-passivity and longitudinal-resonance diagnostics are missing or inadmissible")
        power_min, power_max = full["minimum_carrier_power_xy_total"], full["maximum_carrier_power_xy_total"]
        if (not isinstance(power_min, list) or not isinstance(power_max, list) or len(power_min) != 3 or len(power_max) != 3 or
                any(not _is_finite_number(v) or not 0 <= v <= 1+reflected_power_excess_tolerance for v in power_min+power_max) or
                any(a > b for a, b in zip(power_min, power_max))):
            raise CampaignError("magnetoionic fixed-basis carrier power diagnostics must be finite passive x/y bounds")
        for field in ("table_workspace_bytes", "stream_workspace_bytes"):
            if not _is_nonnegative_int(full.get(field)) or full[field] == 0:
                raise CampaignError("magnetoionic execution requires measured table and stream workspace")
        if full["table_workspace_bytes"] + full["stream_workspace_bytes"] > 2097152 or not _is_nonnegative_int(channel.get("workspace_bytes")) or channel["workspace_bytes"] < full["table_workspace_bytes"]+full["stream_workspace_bytes"]:
            raise CampaignError("magnetoionic table and stream must fit reserved and charged workspace")
        if not _is_finite_number(full.get("preparation_seconds")) or full["preparation_seconds"] <= 0:
            raise CampaignError("magnetoionic execution must report actual table preparation time")
        low, high = full.get("source_minimum_frequency_hz"), full.get("source_maximum_frequency_hz")
        if not _is_finite_number(low) or not _is_finite_number(high) or not -384 <= low <= high <= 384:
            raise CampaignError("magnetoionic executed source must remain within its qualified ±384 Hz band")
        timing = receipt.get("timing")
        if oracle_mode:
            if (not isinstance(timing, dict) or not _is_finite_number(timing.get("wall_seconds")) or
                    not full["preparation_seconds"] <= timing["wall_seconds"] or timing.get("capacity_qualified") is not False):
                raise CampaignError("magnetoionic oracle table construction must remain within its recorded wall time")
        elif (not isinstance(timing, dict) or any(not _is_finite_number(timing.get(k)) for k in ("channel_preparation_seconds", "wall_seconds")) or
                not full["preparation_seconds"] <= timing["channel_preparation_seconds"] <= timing["wall_seconds"] or
                timing.get("isolated_receiver_service_measured") is not False or timing.get("capacity_qualified") is not False):
            raise CampaignError("magnetoionic table construction remains charged to preparation and wall time")
        regions = full.get("regions")
        if not isinstance(regions, list) or len(regions) != 3:
            raise CampaignError("magnetoionic capture requires before, transition and after vector energy regions")
        start = round((transition_first*fs)) + 32
        finish = start + round(transition_seconds*fs)
        expected_counts = (min(start,horizon), max(0,min(finish,horizon)-start), max(0,horizon-finish))
        energy_fields = ("incident_energy_sample_sum", "reflected_x_energy_sample_sum",
                         "reflected_y_energy_sample_sum", "reflected_vector_energy_sample_sum",
                         "selected_antenna_energy_sample_sum")
        for region, name, count in zip(regions, ("before", "transition", "after"), expected_counts):
            if (not isinstance(region, dict) or region.get("name") != name or region.get("samples") != count):
                raise CampaignError("magnetoionic regional accounting must cover the complete physical profile transition")
            names = ("incident_energy_sample_sum", "reflected_x_energy_sample_sum", "reflected_y_energy_sample_sum",
                     "reflected_vector_energy_sample_sum", "selected_antenna_energy_sample_sum")
            if any(not _is_finite_number(region.get(name)) or region[name] < 0 for name in names):
                raise CampaignError("magnetoionic regional energies must be finite and nonnegative")
            if not math.isclose(region["reflected_vector_energy_sample_sum"],
                                region["reflected_x_energy_sample_sum"]+region["reflected_y_energy_sample_sum"],
                                rel_tol=1e-10, abs_tol=1e-9) or not math.isclose(
                    region["selected_antenna_energy_sample_sum"],
                    region["reflected_"+("x" if component == 0 else "y")+"_energy_sample_sum"],
                    rel_tol=1e-10, abs_tol=1e-9):
                raise CampaignError("magnetoionic vector and fixed antenna projection energies disagree")
        for key, region_key in zip(("input_energy_sample_sum", "reflected_x_energy_sample_sum",
                                    "reflected_y_energy_sample_sum", "reflected_vector_energy_sample_sum",
                                    "signal_energy_sample_sum"), energy_fields):
            # Native channel totals contain incident and selected-antenna energy.
            # Both component energies and their vector sum are reported by region;
            # validate any redundant aggregate claim without requiring one.
            if key not in channel and key not in ("input_energy_sample_sum", "signal_energy_sample_sum"):
                continue
            expected_energy = sum(region[region_key] for region in regions)
            if not _is_finite_number(channel.get(key)) or not math.isclose(channel[key], expected_energy, rel_tol=1e-10, abs_tol=1e-9):
                raise CampaignError("magnetoionic component energies must reconcile exactly with complete channel totals")
        if episode.get("control", "wanted") == "noise" and (
                any(any(region[field] != 0 for field in ("incident_energy_sample_sum", "reflected_x_energy_sample_sum",
                    "reflected_y_energy_sample_sum", "reflected_vector_energy_sample_sum", "selected_antenna_energy_sample_sum"))
                    for region in regions) or channel.get("input_energy_sample_sum") != 0 or
                channel.get("signal_energy_sample_sum") != 0 or channel.get("contains_source_energy") is not False or
                channel.get("contains_signal_energy") is not False):
            raise CampaignError("magnetoionic noise control must preserve exact zero clean incident and projected energy")
        return full

    if not oracle_mode:
        validate_rendered_channel(channel, horizon, source_stop)
        return
    oracle, presence = receipt.get("oracle"), receipt.get("presence_oracle")
    if (not isinstance(oracle, dict) or not isinstance(presence, dict) or
            plan.get("oracle_physical_channel_instances") != 3 or
            plan.get("magnetoionic_diagnostic_scope") != "isolated single DATA bit under the known vector-FIR operator; neither a same-input full-message replay nor a complete dynamic excursion" or
            oracle.get("kind") != "isolated single DATA bit, equal-prior binary known vector-FIR operator and synthetic white Gaussian noise" or
            presence.get("kind") != "isolated single DATA bit under a known vector-FIR operator versus synthetic white Gaussian noise"):
        raise CampaignError("magnetoionic oracle requires its isolated three-channel physical diagnostic scope")
    for row in (oracle, presence):
        if any(row.get(key) is not False for key in
               ("same_input_full_message_replay", "complete_dynamic_excursion_required", "field_sensitivity_bound")):
            raise CampaignError("magnetoionic isolated oracle cannot claim full-message or full-excursion field qualification")
    if any(oracle.get(key) is not False for key in
           ("includes_acquisition_or_completion", "finite_blocklength_capacity_bound", "realization_normalized")):
        raise CampaignError("magnetoionic oracle must preserve unnormalized isolated-bit evaluator scope")
    rendered, source_end = oracle.get("samples"), oracle.get("source_stop_sample")
    if (not _is_nonnegative_int(rendered) or not _is_nonnegative_int(source_end) or source_end == 0 or
            rendered < source_end+81 or plan.get("oracle_horizon_samples") != rendered or
            oracle.get("signal_and_tail_horizon_samples") != source_end+81 or
            oracle.get("capture_preroll_samples") != round(preroll*fs) or
            oracle.get("channel_clock_origin_sample") != 0 or oracle.get("hypotheses_share_channel_seed") is not True or
            not _is_finite_number(oracle.get("channel_tail_seconds")) or
            not math.isclose(oracle["channel_tail_seconds"],81/fs,rel_tol=0,abs_tol=4*math.ulp(81/fs))):
        raise CampaignError("magnetoionic oracle requires its actual isolated source, full causal tail and shared capture clock")
    tables = [validate_rendered_channel(oracle.get(key),rendered,source_end) for key in
              ("magnetoionic_channel_hypothesis_0", "magnetoionic_channel_hypothesis_1", "magnetoionic_channel_noisy")]
    if len({table["table_sha256"] for table in tables}) != 1:
        raise CampaignError("magnetoionic oracle hypotheses must share the same qualified coefficient table")
    preparation = oracle.get("physical_table_preparation_seconds")
    if (oracle.get("physical_table_rf_solves") != 3*rf_solves or
            not _is_finite_number(preparation) or
            not math.isclose(preparation,sum(table["preparation_seconds"] for table in tables),rel_tol=1e-10,abs_tol=1e-9) or
            preparation > receipt["timing"]["wall_seconds"]):
        raise CampaignError("magnetoionic oracle must charge all three physical RF tables and preparation times")
    outcomes = receipt.get("outcomes")
    if (not isinstance(outcomes, dict) or outcomes.get("receiver_executed") is not False or
            outcomes.get("component_only") is not True or outcomes.get("completion_evaluated") is not False or
            outcomes.get("correct_complete") is not False):
        raise CampaignError("magnetoionic oracle cannot claim receiver execution or message completion")


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
    _validate_profile_controls(plan, episode)
    _validate_fullwave_reflection(receipt, episode)
    _validate_magnetoionic_reflection(receipt, episode)
    _validate_gannon_replay(receipt, episode)
    if episode["mode"] != "gui-varying" and "hf_gui_varying_diagnostic" in receipt:
        raise CampaignError("unrequested GUI varying diagnostic in native receipt")
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
    if episode["scenario"] != "hf-gui-native" and ("capture_lead_seconds" in episode or "capture_preroll_seconds" in plan):
        lead = plan.get("capture_preroll_seconds")
        if not _is_finite_number(lead) or lead != episode.get("capture_lead_seconds", .25):
            raise CampaignError("native capture lead does not match the frozen episode")
    if "capture_preroll_override" in plan and plan["capture_preroll_override"] is not ("capture_lead_seconds" in episode):
        raise CampaignError("native capture lead override does not match the frozen episode")
    for field, default in (("hf_trajectory_seed", None), ("noise_seed", episode["seed"]),
                           ("hf_trajectory_offset_seconds", 0)):
        if field in episode or field in plan:
            actual = plan.get(field)
            expected = episode.get(field, default)
            if (actual != expected or isinstance(actual, bool) or
                    (actual is not None and (not _is_finite_number(actual) or
                     (field != "hf_trajectory_offset_seconds" and not _is_nonnegative_int(actual)))) or
                    plan.get(field.replace("_seconds", "") + "_override") is not (field in episode)):
                raise CampaignError(f"native {field} does not match the frozen episode")
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
    filter_requested = episode.get("hf_phase_filter", False)
    if plan.get("hf_phase_filter_requested", False) is not filter_requested:
        raise CampaignError("native phase filter request does not match the frozen episode")
    receiver_filter_requested = episode.get("hf_phase_filter_receiver", False)
    if plan.get("hf_phase_filter_receiver_requested", False) is not receiver_filter_requested:
        raise CampaignError("native receiver phase filter request does not match the frozen episode")
    if plan.get("hf_phase_filter_paths_requested", False) is not episode.get("hf_phase_filter_paths", False):
        raise CampaignError("native private phase path request does not match the frozen episode")
    sticky_requested = episode.get("hf_phase_filter_sticky", False)
    if plan.get("hf_phase_filter_sticky_requested", False) is not sticky_requested:
        raise CampaignError("native sticky phase prior request does not match the frozen episode")
    bridge_requested = episode.get("hf_phase_bridge", False)
    if plan.get("hf_phase_bridge_requested", False) is not bridge_requested:
        raise CampaignError("native phase bridge request does not match the frozen episode")
    if plan.get("hf_gui_clean_diagnostic_requested", False) is not episode.get("hf_gui_clean_diagnostic", False):
        raise CampaignError("native clean GUI diagnostic request does not match the frozen episode")
    comparison_requested = episode.get("hf_compare_admitted_models", False)
    policy = "compare_admitted" if comparison_requested else "fallback_only"
    if comparison_requested or episode.get("service_replay", False) or "receiver_evidence_policy" in plan:
        if (plan.get("hf_compare_admitted_models_requested") is not comparison_requested or
                plan.get("receiver_evidence_policy") != policy):
            raise CampaignError("native receiver evidence policy does not match the frozen episode")
        receiver_policy = receipt.get("receiver")
        if episode["mode"] == "receive" and (not isinstance(receiver_policy, dict) or receiver_policy.get("evidence_policy") != policy):
            raise CampaignError("executed receiver must report the requested native evidence policy")
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
        model_family = ("equal_prior_refined_baseline_and_correlated_phase_filter" if receiver_filter_requested else
                        "equal_prior_fixed97_and_cartesian_refinement" if refinement_requested else "sinusoidal_phase_97")
        if (not isinstance(phase_receiver, dict) or phase_receiver.get("experimental") is not True or
                phase_receiver.get("truth_information_passed_to_search") is not False or
                phase_receiver.get("refinement_enabled", False) is not refinement_requested or
                phase_receiver.get("phase_filter_enabled", False) is not receiver_filter_requested or
                phase_receiver.get("phase_filter_sticky_enabled", False) is not sticky_requested or
                phase_receiver.get("phase_bridge_enabled", False) is not bridge_requested or
                phase_receiver.get("model_family") != model_family or
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
                expected_evidence = report["phase_refinement"]["combined_bit_log_e"]
                if receiver_filter_requested:
                    public_slots = plan.get("public_slots")
                    if (not isinstance(public_slots, list) or len(public_slots) != maximum or
                            not isinstance(public_slots[report["slot_index"]], dict) or
                            public_slots[report["slot_index"]].get("index") != report["slot_index"]):
                        raise CampaignError("receiver phase filter must bind each callback to the complete public calendar")
                    _validate_phase_filter_result(report, plan, public_slots[report["slot_index"]].get("observation"))
                    expected_evidence = report["phase_filter"]["combined"]["bit_log_e"]
                elif "phase_filter" in report:
                    raise CampaignError("unrequested receiver phase filter result")
                if report.get("bit_log_e") != expected_evidence:
                    raise CampaignError("operational refinement callback must use the reported combined evidence")
            elif report.get("phase_refinement", {"enabled": False}) != {"enabled": False}:
                raise CampaignError("unrequested operational phase refinement trace")
            elif "phase_filter" in report:
                raise CampaignError("unrequested receiver phase filter result")
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
    if episode["mode"] in {"receive", "slot", "channel", "diagnose", "hf-search", "gui-varying"}:
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
    elif episode["mode"] == "gui-varying":
        _validate_gui_varying(receipt, episode)
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
        _validate_phase_filter(receipt, episode)
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
            _validate_gain_partitions(physical, plan.get("sample_rate_hz"), observation["stop"] - observation["first"],
                                      expected, refined_available)
            known = physical.get("known_binary")
            if known is None and "noise_seed" in plan:
                raise CampaignError("current physical diagnostic requires the known binary reference")
            if known is not None:
                if (not isinstance(known, dict) or not _is_nonnegative_int(known.get("wanted_bit")) or known.get("wanted_bit") != expected or
                        known.get("fixed_known_context") is not True or
                        known.get("truth_information_passed_to_search") is not False or
                        known.get("includes_acquisition_presence_or_completion") is not False or
                        any(not _is_finite_number(known.get(field)) for field in
                            ("squared_distance", "distance_over_noise_variance",
                             "observed_correct_vs_wrong_log_likelihood_ratio", "equal_prior_bit_error_probability")) or
                        known["squared_distance"] < 0 or known["distance_over_noise_variance"] < 0 or
                        not _is_finite_number(plan.get("noise_variance")) or plan["noise_variance"] <= 0 or
                        not math.isclose(known["distance_over_noise_variance"], known["squared_distance"] / plan["noise_variance"],
                                         rel_tol=1e-10, abs_tol=1e-12) or
                        not math.isclose(known["equal_prior_bit_error_probability"],
                            .5 * math.erfc(math.sqrt(known["distance_over_noise_variance"] / 4)),
                            rel_tol=1e-10, abs_tol=1e-300)):
                    raise CampaignError("known binary reference must preserve its fixed private context and Gaussian distance law")
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
            _validate_private_phase_paths(receipt, episode)
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


    _validate_service_replay(receipt, episode)


def _run_service_child(command: list[str], out: Any, err: Any, timeout: float,
                       options: dict[str, Any]) -> tuple[int, str | None]:
    """Bound captured streams independently of the child's larger spool-file cap."""
    deadline = time.monotonic() + timeout
    overflow = None
    with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          shell=False, start_new_session=(os.name == "posix"), **options) as child:
        def stop() -> None:
            try:
                if os.name == "posix":
                    os.killpg(child.pid, signal.SIGKILL)
                else:
                    child.kill()
            except ProcessLookupError:
                pass

        try:
            with selectors.DefaultSelector() as ready:
                ready.register(child.stdout, selectors.EVENT_READ, ("stdout", out))
                ready.register(child.stderr, selectors.EVENT_READ, ("stderr", err))
                retained = {"stdout": 0, "stderr": 0}
                while ready.get_map():
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(command, timeout)
                    for key, _ in ready.select(min(remaining, .1)):
                        data = os.read(key.fd, 64 * 1024)
                        if not data:
                            ready.unregister(key.fileobj)
                            continue
                        name, destination = key.data
                        available = MAX_OUTPUT_BYTES - retained[name]
                        kept = min(available, len(data))
                        if kept:
                            destination.write(data[:kept])
                            retained[name] += kept
                        if len(data) > available and overflow is None:
                            overflow = name
                            stop()
                child.wait(timeout=max(.001, deadline - time.monotonic()))
        except BaseException:
            stop()
            child.wait()
            raise
    return child.returncode, overflow


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
    output_overflow = None
    return_code: int | None = None
    # The native spool cannot contain more than the declared sample budget,
    # even if its separately declared byte limit is larger.
    child_file_limit = max(MAX_OUTPUT_BYTES, min(episode.get("service_spool_byte_limit", max_samples * 17),
        max_samples * 17)) if episode.get("service_replay", False) else MAX_OUTPUT_BYTES
    def limit_output() -> None:
        if resource is not None:
            resource.setrlimit(resource.RLIMIT_FSIZE, (child_file_limit, child_file_limit))

    try:
        with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
            try:
                options: dict[str, Any] = {}
                if os.name == "posix" and resource is not None:
                    options["preexec_fn"] = limit_output
                if episode.get("service_replay", False):
                    return_code, output_overflow = _run_service_child(command, out, err, timeout, options)
                else:
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
                              "child_file_limit_bytes": child_file_limit,
                              "raw_stdout": stdout_path.name, "raw_stderr": stderr_path.name,
                              "stderr": _stderr(stderr_path)}
    if timed_out:
        result["status"] = "timed_out"
        return result
    if output_overflow is not None:
        result["status"] = "invalid_receipt"
        result["error"] = f"native {output_overflow} exceeded the {MAX_OUTPUT_BYTES}-byte output cap"
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
        else:
            _validate_service_replay(receipt, episode)
        if episode.get("service_replay", False) and isinstance(receipt.get("plan"), dict) and receipt["plan"].get("service_replay") is not None:
            if (receipt["plan"].get("max_samples") != max_samples or
                    not _is_finite_number(receipt["plan"].get("max_wall_seconds")) or
                    not math.isclose(receipt["plan"]["max_wall_seconds"], native_budget, rel_tol=1e-12, abs_tol=1e-12)):
                raise CampaignError("service replay limits must match the actual frozen invocation")
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
        outcomes = [_communication_outcome(r.get("receipt", {}), r["episode"]) for r in eligible]
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


NEGATIVE_RECEIVE_CONTROLS = ("noise", "wrong-key", "wrong-ordinal")


def _negative_control_counts(receipt: dict[str, Any]) -> tuple[int | None, int | None]:
    """Count accepted messages once from the native aggregate, not again from events."""
    outcomes = receipt.get("outcomes", {})
    if not isinstance(outcomes, dict):
        return None, None
    return tuple(outcomes[field] if _is_nonnegative_int(outcomes.get(field)) else None
                 for field in ("false_data", "accepted_messages"))


def _negative_control_summary(trials: list[dict[str, Any]]) -> dict[str, Any]:
    """Recompute descriptive counts from validated trials with rehydrated receipts.

    This does not alter trial classifications. Missing or malformed count fields
    remain unavailable; no slot/message events are added to native aggregates.
    """
    fields = ("completed_trials", "counted_trials", "unavailable_count_trials",
              "trials_with_false_data", "false_data_decisions",
              "trials_with_accepted_false_messages", "accepted_false_messages")
    groups = {control: dict.fromkeys(fields, 0) for control in NEGATIVE_RECEIVE_CONTROLS}
    for trial in trials:
        episode, receipt = trial.get("episode", {}), trial.get("receipt", {})
        if (not isinstance(episode, dict) or not isinstance(receipt, dict) or
                trial.get("status") != "completed" or receipt.get("status") != "completed" or
                episode.get("mode") != "receive" or episode.get("control") not in groups):
            continue
        if episode.get("service_replay", False):
            service = receipt.get("service_replay")
            if not isinstance(service, dict) or service.get("work_complete") is not True:
                continue
        group = groups[episode["control"]]
        group["completed_trials"] += 1
        false_data, accepted = _negative_control_counts(receipt)
        if false_data is None or accepted is None:
            group["unavailable_count_trials"] += 1
            continue
        group["counted_trials"] += 1
        group["trials_with_false_data"] += int(false_data > 0)
        group["false_data_decisions"] += false_data
        group["trials_with_accepted_false_messages"] += int(accepted > 0)
        group["accepted_false_messages"] += accepted
    return {"scope": "Descriptive counts only; completed validated negative receive trials with both native counters. No occurrence-rate or false-alarm probability estimate.",
            **{field: sum(group[field] for group in groups.values()) for field in fields},
            "by_control": groups}


def _negative_control_label(receipt: dict[str, Any], episode: dict[str, Any]) -> str:
    false_data, accepted = _negative_control_counts(receipt)
    counts = f"false DATA={false_data if false_data is not None else 'unavailable'}; accepted false messages={accepted if accepted is not None else 'unavailable'}"
    if false_data is None or accepted is None:
        return "negative control counts incomplete; " + counts
    if false_data or accepted:
        return "negative control did not pass; " + counts
    if episode.get("service_replay", False) and receipt.get("service_replay", {}).get("communication_complete") is not True:
        return "negative control service endpoint did not pass; " + counts
    return "negative control passed; no accepted messages or false DATA; " + counts


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
            "Two-episode service replay uses its aggregate endpoint after complete replay work: both expected messages for wanted traffic, or no accepted messages or false DATA for negative controls.", "",
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
            if episode.get("control") in NEGATIVE_RECEIVE_CONTROLS:
                outcome = _negative_control_label(receipt, episode)
            elif _communication_endpoint(episode) == "service_replay.communication_complete":
                negative = (receipt.get("service_replay", {}).get("expected_message_count") == 0 or
                            episode.get("control", "wanted") in {"noise", "wrong-key", "wrong-ordinal"})
                if negative:
                    outcome = ("negative control passed; no accepted messages or false DATA" if _communication_outcome(receipt, episode) is True else
                               "negative control did not pass")
                else:
                    outcome = ("both physical messages complete" if _communication_outcome(receipt, episode) is True else
                               "paired physical-message completion failed")
            elif episode.get("fec", "off") != "off":
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
        elif episode["mode"] == "gui-varying":
            outcome = "canonical varying component complete; no receiver decision or message completion"
        else:
            outcome = "plan only"
        rows.append(f"| {trial['episode']['id']} | {trial['episode']['scenario']} | {trial['episode']['mode']} | "
                    f"{episode.get('control', '—')} | {trial.get('status')} | {native_status} | {outcome} | "
                    f"{slot_decision} | {correct_data} | {receive_counts} | "
                    f"{trial.get('elapsed_seconds', 0):.3f} |")
    negative = _negative_control_summary(run["trials"])
    rows.extend(["", "## Completed negative controls", "", negative["scope"],
                 "Counts exclude censored, invalid and execution-failed trials. Accepted messages are counted once from the native aggregate; false DATA decisions are a separate outcome.", "",
                 "| Control | Completed trials | Trials with both counts | Counts unavailable | Trials with false DATA | False DATA decisions | Trials accepting false messages | Accepted false messages |",
                 "|---|---:|---:|---:|---:|---:|---:|---:|"])
    for control, counts in negative["by_control"].items():
        rows.append("| " + control + " | " + " | ".join(str(value) for value in counts.values()) + " |")
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
               _communication_outcome(trial.get("receipt", {}), trial.get("episode", {})) is False
               for trial in trials),
           "retained_output_bytes": sum(path.stat().st_size for path in output_dir.iterdir() if path.is_file()),
           "retained_output_limit_bytes": MAX_CAMPAIGN_OUTPUT_BYTES,
           "statistics": stats, "negative_controls": _negative_control_summary(trials), "trials": trials}
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
                       "valid_communication_failures", "stop_reason", "elapsed_seconds", "statistics", "negative_controls")},
                     sort_keys=True))
    return 0 if result["status"] == "completed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

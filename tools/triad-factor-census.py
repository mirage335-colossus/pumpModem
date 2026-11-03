#!/usr/bin/env python3
"""Exact factorized census of the requested triad configuration intent grid.

This consumes a bounded, source-bound set of native ``configure`` receipts.
It classifies configuration geometry and the native deterministic capture
predicate only. It does not predict reception, waveform quality, or physical
frontend compatibility.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = REPO / "build/dev-cli/triad_long"
SOURCE_FILES = (
    "include/datapump/clock_sync.hpp",
    "src/triad_modem_v3.cpp",
    "src/tuning_presets.cpp",
    "tools/triad-long.cpp",
)

RATES = (0.001, 0.01, 0.1, 1.0, 3.6, 10.0, 36.0, 100.0, 360.0,
         1200.0, 2400.0, 3600.0, 12000.0, 18000.0, 24000.0,
         1_000_000.0, 30_000_000.0)
BASEBAND = ("1725-Hz", "half-rate", "8270-Hz")
SHIFTS = (0.0, 500_000.0, 1_000_000.0, 3_500_000.0, 7_000_000.0,
          10_000_000.0, 14_000_000.0, 30_000_000.0, 5_800_000_000.0)
TIMING = ("gps-10ms-1ms-0ms-audio70ms", "default-plus-minus-7.035s-search")
BASE_OSCILLATORS = ("free-running-crystal", "gpsdo-hobbyist-xo",
                    "gpsdo-tcxo", "gpsdo-ocxo")
SHIFT_OSCILLATORS = ("baseband-clock", "free-running-crystal", "cb-ssb-20ppm",
                     "ic-7100-0.5ppm", "gpsdo-hobbyist-xo", "gpsdo-tcxo",
                     "gpsdo-ocxo")
SPREAD = ("none", "sub9-best", "sub9-moderate", "sub9-worst", "hf-best",
          "hf-moderate", "hf-worst", "aurora-best", "aurora-worst", "eme-focused")
SELECTIVE = ("off", "slow-1", "slow-5", "slow-10", "slow-25", "slow-65",
             "fast-1", "fast-5", "fast-10", "fast-25", "fast-65",
             "each-1", "each-5", "each-10", "each-25", "each-65")
CN0 = (200.0, 140.0, 120.0, 100.0, 80.0, 60.0, 55.0, 40.0, 32.0,
       20.0, 6.0, -6.0, -10.0, -16.0, -20.0, -23.0, -26.0, -30.0, -60.0)
MARGINS = (1.0, 3.0)
ADDED = (1, 10, 100, 1000)
FHSS = (False, True)
BURST = ("off", "3x")
VALIDATION_SEED = 20261104
DIRECTED_NEAR_ZERO_COUNT = 6
SEEDED_RANDOM_COUNT = 90
TOPOLOGY_PROBES = {
    "zero-shift": ("gpsdo-ocxo", "cb-ssb", 0.0),
    "shared-baseband-clock": ("gpsdo-tcxo", "baseband-clock", 500000.0),
    "microwave-shared-baseband-clock": ("crystal", "baseband-clock", 5.8e9),
    "zero-shift-shared-baseband-clock": ("gpsdo-xo", "baseband-clock", 0.0),
}
VALIDATION_QUERY_COUNT = DIRECTED_NEAR_ZERO_COUNT + SEEDED_RANDOM_COUNT + len(TOPOLOGY_PROBES)

AXES = {
    "rate_hz": RATES,
    "baseband_carrier": BASEBAND,
    "shift_hz": SHIFTS,
    "timing": TIMING,
    "base_oscillator": BASE_OSCILLATORS,
    "shift_oscillator": SHIFT_OSCILLATORS,
    "spread": SPREAD,
    "selective": SELECTIVE,
    "cn0_db_hz": CN0,
    "margin": MARGINS,
    "added_symbols": ADDED,
    "fhss": FHSS,
    "symbol_burst": BURST,
}
TOTAL = math.prod(map(len, AXES.values()))
PRIMARY = 17 * 3 * 9 * 2 * 4 * 7 * 10 * 16 * 19
REPLICATION = len(TIMING) * len(CN0) * len(BURST)

OSC_PPM = {
    "crystal": 100.0, "cb-ssb": 40.0, "ic-7100": 1.0,
    "gpsdo-xo": 0.0001, "gpsdo-tcxo": 0.0001, "gpsdo-ocxo": 0.0001,
}
BASE_OSC_ID = {
    "free-running-crystal": "crystal", "gpsdo-hobbyist-xo": "gpsdo-xo",
    "gpsdo-tcxo": "gpsdo-tcxo", "gpsdo-ocxo": "gpsdo-ocxo",
}
SHIFT_OSC_ID = {**BASE_OSC_ID, "cb-ssb-20ppm": "cb-ssb", "ic-7100-0.5ppm": "ic-7100",
                "baseband-clock": "baseband-clock"}
SPREAD_DOPPLER = {
    "none": 0.0, "sub9-best": .001, "sub9-moderate": .005, "sub9-worst": .05,
    "hf-best": .1, "hf-moderate": 10.0, "hf-worst": 30.0,
    "aurora-best": 10.0, "aurora-worst": 55.0, "eme-focused": 6.0,
}
ASSUMPTIONS = {
    "requested_active_seconds": 8,
    "sample_rate": "automatic native sample rate",
    "internal_burst_wait_tiles": 0,
    "public_frequency_grid_hz": [0, 0],
    "truth_frequency_hz": 0,
    "truth_frequency_drift_hz_per_second": 0,
    "diurnal_phase_amplitude_radians": 0,
    "gaussian_doppler_width_hz": 0,
    "whole_bit_symbol_burst": ["off", "3x"],
    "timing": list(TIMING),
    "topology": "all RF shifts; zero shift ignores shift oscillator; baseband-clock shares base endpoint ppm",
    "interleave": "enabled for every Added Symbols level",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as src:
        for block in iter(lambda: src.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read JSON evidence {path}: {exc}") from exc


def validate_evidence(evidence_dir: Path, repo: Path = REPO,
                      binary: Path = DEFAULT_BINARY) -> tuple[dict[str, Any], list[dict], list[dict]]:
    """Validate immutable evidence identity and native comparisons before counting."""
    summary_path = evidence_dir / "summary.json"
    summary = _read_json(summary_path)
    if summary.get("schema") != "triad-native-config-factorization-diagnostic-v2":
        raise ValueError("unexpected native factorization schema")
    if summary.get("assumptions") != {
        "automatic_sample_rate": True, "diurnal_phase_radians": 0,
        "gaussian_doppler_width_hz": 0, "internal_burst_wait_tiles": 0,
        "public_frequency_grid": [0, 0], "requested_seconds": 8,
        "timing_choices": ["tight", "explicit-uniform +/-7.035s"], "truth_frequency_drift": 0,
        "truth_frequency_hz": 0, "whole_bit_burst_choices": ["off", "3"],
        "zero_shift_oscillator": "selected shift oscillator is unused; RF contribution is zero",
        "shared_baseband_clock": "shift ppm equals base endpoint ppm",
    }:
        raise ValueError("prototype assumptions differ from this census contract")
    if summary.get("total_intent_count") != TOTAL or summary.get("replication_factor") != REPLICATION:
        raise ValueError("prototype intent/replication dimensions differ")
    validation_design = summary.get("validation", {})
    validation_results = (validation_design.get("valid_compared"),
                          validation_design.get("source_rejected"))
    if (validation_design.get("seed") != VALIDATION_SEED
            or validation_design.get("queries") != VALIDATION_QUERY_COUNT
            or validation_design.get("directed_near_zero_queries") != DIRECTED_NEAR_ZERO_COUNT
            or any(not isinstance(value, int) for value in validation_results)
            or sum(validation_results) != VALIDATION_QUERY_COUNT):
        raise ValueError("native validation summary differs from the declared 6+90+4 input design")
    before, after = summary.get("source_sha256_before"), summary.get("source_sha256_after")
    if before != after or not summary.get("source_and_binary_unchanged"):
        raise ValueError("native prototype changed source or executable during collection")
    current_sources = {name: sha256(repo / name) for name in SOURCE_FILES}
    if current_sources != before:
        raise ValueError("prototype source identity does not match current source files")
    binary_before, binary_after = summary.get("binary_sha256_before"), summary.get("binary_sha256_after")
    if not binary_before or binary_before != binary_after or sha256(binary) != binary_before:
        raise ValueError("prototype executable identity does not match current executable")
    expected_artifacts = {Path(item["path"]).name: item for item in summary.get("artifacts", [])}
    for name in ("geometry.json", "validation.json", "near-zero.json"):
        path = evidence_dir / name
        record = expected_artifacts.get(name)
        if not record or path.stat().st_size != record.get("bytes") or sha256(path) != record.get("sha256"):
            raise ValueError(f"prototype artifact identity mismatch: {name}")
    geometry = _read_json(evidence_dir / "geometry.json")
    validation = _read_json(evidence_dir / "validation.json")
    near_zero = _read_json(evidence_dir / "near-zero.json")
    if (len(geometry) != len(RATES) * len(ADDED) * len(FHSS)
            or len(validation) != VALIDATION_QUERY_COUNT):
        raise ValueError("native evidence row counts differ from the declared bounded design")
    _validate_geometry(geometry, binary)
    _validate_validation(validation, binary)
    near_zero_delta = _validate_near_zero(near_zero, geometry)
    if len(near_zero) != summary.get("near_zero_factor_rows"):
        raise ValueError("near-zero factor row count differs from summary")
    summary["_verified_near_zero_rows"] = len(near_zero)
    summary["_near_zero_max_abs_delta_hz"] = near_zero_delta
    return summary, geometry, validation


def _validate_receipt_identity(receipt: dict[str, Any], binary: Path | None) -> None:
    argv = receipt.get("argv", [])
    if len(argv) < 2 or argv[1] != "--mode" or "configure" not in argv:
        raise ValueError("native receipt does not record a configure invocation")
    if binary is not None and Path(argv[0]).resolve() != binary.resolve():
        raise ValueError("native receipt executable path differs from the requested binary")
    stdout = receipt.get("stdout", "")
    if hashlib.sha256(stdout.encode()).hexdigest() != receipt.get("stdout_sha256"):
        raise ValueError("native receipt stdout hash mismatch")


def _validate_geometry(rows: list[dict[str, Any]], binary: Path | None = None) -> None:
    expected = set(itertools.product(RATES, ADDED, FHSS))
    seen: set[tuple[float, int, bool]] = set()
    for row in rows:
        key = (float(row["rate_hz"]), int(row["added"]), bool(row["fhss"]))
        if key not in expected or key in seen:
            raise ValueError(f"duplicate or out-of-grid geometry receipt {key}")
        seen.add(key)
        receipt = row.get("receipt", {})
        if not receipt or not isinstance(receipt.get("returncode"), int):
            raise ValueError("geometry row lacks native exit-status receipt")
        _validate_receipt_identity(receipt, binary)
        options = _argv_options(receipt)
        if (float(options.get("--rate", ["nan"])[0]) != key[0]
                or int(options.get("--added", ["-1"])[0]) != key[1]
                or ("--fhss" in options) != key[2]):
            raise ValueError("geometry factor labels differ from their recorded native arguments")
        if receipt["returncode"] == 0:
            cfg = row.get("configuration")
            if not isinstance(cfg, dict) or "capture" not in cfg:
                raise ValueError("exit-zero geometry row lacks parsed native configuration")
            stdout = json.loads(receipt["stdout"])
            if stdout.get("configuration") != cfg:
                raise ValueError("geometry parsed configuration differs from native stdout")
        elif row.get("configuration") is not None:
            raise ValueError("rejected native geometry row unexpectedly has configuration")
    if seen != expected:
        raise ValueError("geometry receipt grid is incomplete")


def _argv_options(receipt: dict[str, Any]) -> dict[str, list[str]]:
    args = receipt.get("argv", [])
    out: dict[str, list[str]] = {}
    i = 0
    while i < len(args):
        if args[i].startswith("--"):
            key = args[i]
            values = []
            i += 1
            while i < len(args) and not args[i].startswith("--"):
                values.append(args[i]); i += 1
            out[key] = values
        else:
            i += 1
    return out


def _recompute_capture(cfg: dict[str, Any], args: dict[str, list[str]]) -> float:
    capture = cfg["capture"]
    base_ppm = float(cfg["base_nominal_relative_bound_ppm"])
    rx_min, tx_max = 1 - base_ppm * .5e-6, 1 + base_ppm * .5e-6
    cfo = float(cfg["deterministic_oscillator_cfo_bound_hz"])
    truth = abs(float(args.get("--truth-frequency-hz", ["0"])[0]))
    public = max(map(abs, map(float, args.get("--frequency", ["0", "0"]))), default=0.0)
    oscillator = max(cfo + truth, public)
    spread = float(cfg["spread_doppler_hz"])
    if float(cfg.get("perturbation_slow_depth", 0)) > 0 or float(cfg.get("perturbation_fast_depth", 0)) > 0:
        spread *= 2
    drift = abs(float(args.get("--truth-frequency-drift", ["0"])[0])) * (
        float(cfg["duration"]["elapsed_source_calendar_seconds"])
        + 2 * float(cfg["initial_arrival_half_width_seconds"]) + 2)
    path = cfg["path_evolution"]
    diurnal = abs(float(path["diurnal_phase_amplitude_radians"])) / float(path["diurnal_period_seconds"])
    gaussian = float(path["gaussian_doppler_width_hz"])
    return (float(capture["sample_rate_hz"]) * rx_min
            - float(capture["nominal_triplet_bandwidth_hz"]) * tx_max
            - 2 * (oscillator + spread + drift + diurnal) - 6 * gaussian)


def _validate_validation(rows: list[dict[str, Any]], binary: Path | None = None) -> None:
    expected_indices = set(range(VALIDATION_QUERY_COUNT))
    indices = [r.get("index") for r in rows]
    if (len(rows) != VALIDATION_QUERY_COUNT or any(not isinstance(i, int) for i in indices)
            or len(set(indices)) != VALIDATION_QUERY_COUNT or set(indices) != expected_indices):
        raise ValueError("native validation set must contain 100 unique indices 0..99")
    directed = {r["index"] for r in rows if bool(r.get("near_zero_directed"))}
    if directed != set(range(DIRECTED_NEAR_ZERO_COUNT)):
        raise ValueError("native validation set must retain the six directed near-zero rows at indices 0..5")
    by_index = {r["index"]: r for r in rows}
    for index in range(DIRECTED_NEAR_ZERO_COUNT):
        args = _argv_options(by_index[index].get("receipt", {}))
        expected_base = ("gpsdo-xo", "gpsdo-tcxo", "gpsdo-ocxo")[index % 3]
        expected_if = (1725.0, 600.0, 8270.0)[index % 3]
        if (float(args.get("--rate", ["nan"])[0]) != 1200.0
                or int(args.get("--added", ["-1"])[0]) != 1
                or "--fhss" in args
                or args.get("--base-oscillator", [""])[0] != expected_base
                or args.get("--shift-oscillator", [""])[0] != "crystal"
                or float(args.get("--rf-hz", ["nan"])[0]) != 500000.0
                or float(args.get("--nominal-if-hz", ["nan"])[0]) != expected_if
                or args.get("--spread-preset", [""])[0] != "aurora-worst"
                or float(args.get("--margin", ["nan"])[0]) != 1.0
                or args.get("--selective-perturbation", [""])[0] != "off"):
            raise ValueError("directed near-zero receipt inputs differ from their declared design")
    topology_indices = range(DIRECTED_NEAR_ZERO_COUNT + SEEDED_RANDOM_COUNT,
                             VALIDATION_QUERY_COUNT)
    declared_topologies = {by_index[i].get("topology_case") for i in topology_indices}
    if declared_topologies != set(TOPOLOGY_PROBES):
        raise ValueError("native validation set must contain exactly the four declared topology probes")
    for index, expected_case in zip(topology_indices, TOPOLOGY_PROBES):
        if by_index[index].get("topology_case") != expected_case:
            raise ValueError("native topology probes do not match their declared index order")
    for index in range(DIRECTED_NEAR_ZERO_COUNT,
                       DIRECTED_NEAR_ZERO_COUNT + SEEDED_RANDOM_COUNT):
        if by_index[index].get("near_zero_directed") or by_index[index].get("topology_case"):
            raise ValueError("native validation random rows do not match the declared 90-row index range")
    for index in topology_indices:
        if by_index[index].get("near_zero_directed"):
            raise ValueError("native topology probes cannot replace directed near-zero rows")
    valid = rejected = 0
    for row in rows:
        rec = row.get("receipt", {})
        _validate_receipt_identity(rec, binary)
        if rec.get("returncode") != 0:
            if row.get("topology_case") in TOPOLOGY_PROBES:
                raise ValueError(f"required topology probe was rejected: {row['topology_case']}")
            rejected += 1
            continue
        valid += 1
        raw = json.loads(rec["stdout"])
        cfg = raw.get("configuration")
        if not isinstance(cfg, dict):
            raise ValueError("validation native stdout lacks configuration")
        args = _argv_options(rec)
        margin = _recompute_capture(cfg, args)
        reported = float(cfg["capture"]["deterministic_nyquist_margin_hz"])
        if abs(margin - reported) > 2e-12:
            raise ValueError("native capture margin failed independent formula check")
        factor_margin = _capture_margin(
            float(cfg["capture"]["sample_rate_hz"]),
            float(cfg["capture"]["nominal_triplet_bandwidth_hz"]),
            float(args.get("--nominal-if-hz", ["0"])[0]),
            float(args.get("--rf-hz", ["0"])[0]),
            args.get("--base-oscillator", [""])[0],
            args.get("--shift-oscillator", [""])[0],
            args.get("--spread-preset", ["none"])[0],
            float(args.get("--margin", ["1"])[0]),
            args.get("--selective-perturbation", ["off"])[0] != "off")
        if abs(factor_margin - reported) > 2e-12:
            raise ValueError("native capture margin disagrees with independent oscillator/topology formula")
        if row.get("near_zero_directed") and abs(factor_margin) > 0.01:
            raise ValueError("directed near-zero receipt is outside its declared boundary tolerance")
        if (margin > 0) != bool(cfg["capture"]["feasible_for_deterministic_prior"]):
            raise ValueError("native capture feasibility has incorrect strict-sign semantics")
        if abs(factor_margin - float(row["predicted_capture_margin_hz"])) > 2e-12:
            raise ValueError("stored factor prediction disagrees with native capture margin")
        if bool(row["sign_matches"]) != ((margin > 0) == bool(cfg["capture"]["feasible_for_deterministic_prior"])):
            raise ValueError("stored validation sign check is inconsistent")
        topology_case = row.get("topology_case")
        if topology_case:
            rf_hz = float(args.get("--rf-hz", ["0"])[0])
            base_id = args.get("--base-oscillator", [""])[0]
            shift = args.get("--shift-oscillator", [""])[0]
            expected_base, expected_shift, expected_rf = TOPOLOGY_PROBES[topology_case]
            if (base_id, shift, rf_hz) != (expected_base, expected_shift, expected_rf):
                raise ValueError(f"topology probe parameters do not match declared case: {topology_case}")
    if valid + rejected != len(rows):
        raise ValueError("native validation receipts were not fully classified")


def _validate_near_zero(rows: list[dict[str, Any]], geometry: list[dict[str, Any]]) -> float:
    geometry_by_key = {(float(r["rate_hz"]), int(r["added"]), bool(r["fhss"])): r
                       for r in geometry if r["receipt"]["returncode"] == 0}
    maximum = 0.0
    for row in rows:
        key = (float(row["rate_hz"]), int(row["added"]), bool(row["fhss"]))
        if key not in geometry_by_key:
            raise ValueError(f"near-zero row has no native-valid geometry receipt: {key}")
        cfg = geometry_by_key[key]["configuration"]["capture"]
        calculated = _capture_margin(
            float(cfg["sample_rate_hz"]), float(cfg["nominal_triplet_bandwidth_hz"]),
            float(row["if_hz"]), float(row["shift_hz"]), str(row["base"]), str(row["rf"]),
            str(row["spread"]), float(row["margin"]), bool(row["selective_active"]))
        delta = abs(calculated - float(row["capture_margin_hz"]))
        maximum = max(maximum, delta)
        if delta > 2e-12:
            raise ValueError("near-zero factor row disagrees with independently evaluated capture predicate")
    return maximum


def _baseband_hz(value: str, rate: float) -> float:
    if value == "1725-Hz": return 1725.0
    if value == "8270-Hz": return 8270.0
    if value == "half-rate": return rate / 2
    raise ValueError(f"unknown baseband placement {value}")


def _capture_margin(sample_rate: float, bandwidth: float, if_hz: float, rf_hz: float,
                    base_id: str, shift_id: str, spread_id: str, margin: float,
                    selective_active: bool) -> float:
    base_ppm = OSC_PPM[base_id]
    shift_ppm = base_ppm if shift_id == "baseband-clock" else OSC_PPM[shift_id]
    rx_min, tx_max = 1 - base_ppm * .5e-6, 1 + base_ppm * .5e-6
    cfo = (rf_hz * shift_ppm + if_hz * base_ppm) * 1e-6 / rx_min
    spread = SPREAD_DOPPLER[spread_id] * margin * (2 if selective_active else 1)
    return sample_rate * rx_min - bandwidth * tx_max - 2 * (cfo + spread)


def build_census(summary: dict[str, Any], geometry: list[dict], validation: list[dict],
                 repo: Path = REPO, binary: Path = DEFAULT_BINARY) -> dict[str, Any]:
    """Count exact intent outcomes from validated factor tables, without product expansion."""
    _validate_geometry(geometry)
    _validate_validation(validation)
    topology_factor = (len(BASEBAND) * len(TIMING) * len(SPREAD) * len(SELECTIVE)
                       * len(CN0) * len(MARGINS) * len(BURST))
    mult = (len(BASEBAND) * len(SHIFTS) * len(BASE_OSCILLATORS)
            * len(SHIFT_OSCILLATORS) * len(SPREAD) * len(SELECTIVE)
            * len(CN0) * len(MARGINS) * len(TIMING) * len(BURST))

    geometry_counts = Counter()
    capture_counts = Counter()
    capture_tables = {
        "by_margin": {}, "by_spread": {}, "by_selective_activity": {},
        "by_baseband_placement": {}, "by_shift_hz": {},
        "by_base_oscillator": {}, "by_shift_oscillator": {},
    }
    by_rate: dict[str, dict[str, int]] = {}
    by_added: dict[str, dict[str, int]] = {}
    by_fhss: dict[str, dict[str, int]] = {}
    selective_bypass_count = 0

    for row in geometry:
        rate, added, fhss = float(row["rate_hz"]), int(row["added"]), bool(row["fhss"])
        status = "geometry_valid" if row["receipt"]["returncode"] == 0 else "geometry_rejected"
        geometry_counts[status] += mult
        by_rate.setdefault(str(rate), {"geometry_valid": 0, "geometry_rejected": 0})[status] += mult
        by_added.setdefault(str(added), {"geometry_valid": 0, "geometry_rejected": 0})[status] += mult
        by_fhss.setdefault("envelope-only" if fhss else "off", {"geometry_valid": 0, "geometry_rejected": 0})[status] += mult
        if status != "geometry_valid":
            continue
        cfg = row["configuration"]
        fs = float(cfg["capture"]["sample_rate_hz"])
        bw = float(cfg["capture"]["nominal_triplet_bandwidth_hz"])
        for bb in BASEBAND:
            if_hz = _baseband_hz(bb, rate)
            for rf_hz in SHIFTS:
                for base_label, base_id in BASE_OSC_ID.items():
                    for shift_label, shift_id in SHIFT_OSC_ID.items():
                        for margin in MARGINS:
                            for spread in SPREAD:
                                for selective_active in (False, True):
                                    n = REPLICATION  # C/N0, timing, whole-bit burst replication.
                                    outcome = "capture_positive" if _capture_margin(
                                        fs, bw, if_hz, rf_hz, base_id, shift_id,
                                        spread, margin, selective_active) > 0 else "capture_nonpositive"
                                    # Each selective activity class represents 1 or 15 levels.
                                    weight = 1 if not selective_active else len(SELECTIVE)-1
                                    count = n * weight
                                    capture_counts[outcome] += count
                                    outcome_key = "positive" if outcome == "capture_positive" else "nonpositive"
                                    levels = (
                                        ("by_margin", str(int(margin))),
                                        ("by_spread", spread),
                                        ("by_selective_activity", "active" if selective_active else "off"),
                                        ("by_baseband_placement", bb),
                                        ("by_shift_hz", str(int(rf_hz))),
                                        ("by_base_oscillator", base_label),
                                        ("by_shift_oscillator", shift_label),
                                    )
                                    for table_name, level in levels:
                                        cell = capture_tables[table_name].setdefault(
                                            level, {"positive": 0, "nonpositive": 0})
                                        cell[outcome_key] += count
                                    if spread == "none" and selective_active:
                                        selective_bypass_count += count
    source_factor = mult
    geometry_rejected = geometry_counts["geometry_rejected"]
    geometry_valid = geometry_counts["geometry_valid"]
    selective_bypass_intent = TOTAL * (len(SELECTIVE) - 1) // (len(SPREAD) * len(SELECTIVE))
    if geometry_rejected + capture_counts["capture_nonpositive"] + capture_counts["capture_positive"] != TOTAL:
        raise AssertionError("factorized outcome classes do not partition the complete intent grid")
    if geometry_rejected + geometry_valid != (58 + 78) * mult:
        raise AssertionError("geometry factor table failed cardinality check")

    return {
        "schema": "triad-factorized-configuration-census-v2",
        "source_evidence": {
            "prototype_summary_sha256": None,
            "executable_sha256": sha256(binary),
            "source_sha256": {name: sha256(repo / name) for name in SOURCE_FILES},
            "source_files": list(SOURCE_FILES),
            "native_geometry_receipts": len(geometry),
            "independent_native_validation_receipts": len(validation),
            "independent_capture_margin_checks": sum(r["receipt"]["returncode"] == 0 for r in validation),
            "near_zero_factor_rows_rechecked": int(summary.get("_verified_near_zero_rows", 0)),
            "near_zero_factor_max_abs_error_hz": float(summary.get("_near_zero_max_abs_delta_hz", 0.0)),
            "capture_margin_max_abs_error_hz": max(
                abs(_recompute_capture(json.loads(r["receipt"]["stdout"])["configuration"], _argv_options(r["receipt"]))
                    - float(json.loads(r["receipt"]["stdout"])["configuration"]["capture"]["deterministic_nyquist_margin_hz"]))
                for r in validation if r["receipt"]["returncode"] == 0),
        },
        "axes": {name: list(values) for name, values in AXES.items()},
        "axis_cardinalities": {name: len(values) for name, values in AXES.items()},
        "total_intent_count": TOTAL,
        "primary_intent_count": PRIMARY,
        "replication_axes": {"timing": 2, "cn0_db_hz": 19, "symbol_burst": 2},
        "replication_factor": REPLICATION,
        "assumptions": ASSUMPTIONS,
        "outcome_partition": {
            "source_geometry_rejected": geometry_rejected,
            "capture_margin_nonpositive": capture_counts["capture_nonpositive"],
            "capture_margin_positive": capture_counts["capture_positive"],
        },
        "geometry_factor_table": {
            "by_rate_hz": by_rate, "by_added_symbols": by_added, "by_fhss": by_fhss,
            "native_geometry_cases": {
                "source_valid": sum(r["receipt"]["returncode"] == 0 for r in geometry),
                "protected_dwell_reject": sum("FHSS has no protected dwell cells" in r["receipt"]["stderr"] for r in geometry),
                "tile_workspace_reject": sum("protected tile exceeds bounded source workspace" in r["receipt"]["stderr"] for r in geometry),
                "uint32_sample_rate_reject": sum("integer in uint32 range" in r["receipt"]["stderr"] for r in geometry),
            },
        },
        "capture_factor_table": {
            **capture_tables,
            "within_spread_none_selective_selected_but_bypassed_supported_capture_cells": selective_bypass_count,
        },
        "capture_predicate": {
            "formula": "Fs*(1-base_ppm*0.5e-6) - BW*(1+base_ppm*0.5e-6) - 2*((RF*shift_ppm + IF*base_ppm)*1e-6/rx_min + spread_doppler*margin*(2 if selective active else 1)) > 0; shared baseband clock sets shift_ppm=base_ppm; RF=0 nulls its contribution",
            "strict_positive_only": True,
            "scope": "the native deterministic capture-margin predicate; Gaussian and diurnal terms are zero by assumptions",
            "validation": "All accepted native configurations are independently recomputed; includes directed near-zero, zero-shift, shared-clock and microwave-shared-clock probes plus seeded cases. Rejected native configurations remain separately counted.",
        },
        "nonexclusive_model_warnings": {
            "fhss_envelope_only_cells": TOTAL // 2,
            "symbol_burst_calendar_only_cells": TOTAL // 2,
            "selected_modulation_bypassed_by_none_spread_intent_cells": selective_bypass_intent,
            "note": "These are requested-coordinate counts, not reception failures or physical RF/processing qualification.",
        },
        "limitations": [
            "No physical reception, sampled waveform, low-C/N0 sensitivity, false-admission rate, or duration reliability is inferred.",
            "Capture-positive means only that the configured deterministic bound fits the current narrowband model; it is not physical frontend compatibility.",
            "The listed baseband placement maps approximately to --nominal-if-hz; waveform remains zero-centered complex baseband.",
            "At zero RF shift the shift oscillator is unused; baseband-clock shares the base endpoint's oscillator ppm.",
            "Automatic Fs and requested 8-second active span are fixed census assumptions; changing duration, sample rate, oscillator/noise priors, timing or path evolution needs a new census.",
            "FHSS is source-envelope shaping only. Symbol Burst selects whole-bit calendar spacing only. Added Symbols remain interleaved in this implementation.",
        ],
    }


def run(evidence_dir: Path, repo: Path = REPO,
        binary: Path = DEFAULT_BINARY) -> dict[str, Any]:
    source_before = {name: sha256(repo / name) for name in SOURCE_FILES}
    binary_before = sha256(binary)
    tool_before = sha256(Path(__file__).resolve())
    summary, geometry, validation = validate_evidence(evidence_dir, repo, binary)
    report = build_census(summary, geometry, validation, repo, binary)
    source_after = {name: sha256(repo / name) for name in SOURCE_FILES}
    binary_after = sha256(binary)
    tool_after = sha256(Path(__file__).resolve())
    if source_before != source_after or binary_before != binary_after or tool_before != tool_after:
        raise ValueError("source, executable, or census tool changed while building report")
    report["source_evidence"]["prototype_summary_sha256"] = sha256(evidence_dir / "summary.json")
    report["source_evidence"]["prototype_artifact_sha256"] = {
        name: sha256(evidence_dir / name) for name in ("geometry.json", "validation.json", "near-zero.json")
    }
    report["source_evidence"]["census_tool_sha256_before_after"] = [tool_before, tool_after]
    report["source_evidence"]["binary_sha256_before_after"] = [binary_before, binary_after]
    report["source_evidence"]["underlying_source_sha256_before_after"] = {
        name: [source_before[name], source_after[name]] for name in SOURCE_FILES
    }
    return report


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence-dir", type=Path, required=True,
                    help="output directory produced by triad_factor_native_inputs.py")
    ap.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    ap.add_argument("--output", type=Path, help="write a new JSON report; existing paths are never overwritten")
    args = ap.parse_args(argv)
    try:
        report = run(args.evidence_dir, REPO, args.binary)
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x", encoding="utf-8") as dst:
                dst.write(encoded)
        else:
            sys.stdout.write(encoded)
    except (ValueError, OSError, AssertionError, KeyError, TypeError) as exc:
        print(f"triad-factor-census: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

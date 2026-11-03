#!/usr/bin/env python3
"""Compact intent/configuration coverage plan for the isolated triad modem.

This tool enumerates the requested configuration space symbolically and emits
a deterministic pairwise witness set.  It does not run a waveform simulation
or infer reception success. Pairwise rows are constructed by enumerating
every pair/value combination, then filling other axes deterministically;
therefore every included level-pair has a direct witness.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterator


RATES = (0.001, 0.01, 0.1, 1.0, 3.6, 10.0, 36.0, 100.0, 360.0,
         1200.0, 2400.0, 3600.0, 12000.0, 18000.0, 24000.0,
         1_000_000.0, 30_000_000.0)
BASEBAND = ("1725-Hz", "half-rate", "8270-Hz")
SHIFTS = (0.0, 500_000.0, 1_000_000.0, 3_500_000.0, 7_000_000.0,
          10_000_000.0, 14_000_000.0, 30_000_000.0, 5_800_000_000.0)
TIMING = ("gps-10ms-1ms-0ms-audio70ms", "default-plus-minus-7.035s-search")
BASE_OSCILLATORS = ("free-running-crystal", "gpsdo-hobbyist-xo",
                    "gpsdo-tcxo", "gpsdo-ocxo")
SHIFT_OSCILLATORS = ("baseband-clock", "free-running-crystal",
                     "cb-ssb-20ppm", "ic-7100-0.5ppm",
                     "gpsdo-hobbyist-xo", "gpsdo-tcxo", "gpsdo-ocxo")
SPREAD = ("none", "sub9-best", "sub9-moderate", "sub9-worst", "hf-best",
          "hf-moderate", "hf-worst", "aurora-best", "aurora-worst",
          "eme-focused")
SELECTIVE = ("off", "slow-1", "slow-5", "slow-10", "slow-25", "slow-65",
             "fast-1", "fast-5", "fast-10", "fast-25", "fast-65",
             "each-1", "each-5", "each-10", "each-25", "each-65")
CN0 = (200.0, 140.0, 120.0, 100.0, 80.0, 60.0, 55.0, 40.0, 32.0,
       20.0, 6.0, -6.0, -10.0, -16.0, -20.0, -23.0, -26.0, -30.0, -60.0)
MARGINS = (1.0, 3.0)
ADDED = (1, 10, 100, 1000)
FHSS = ("off", "envelope-only")
BURST = ("off", "3x")
MAX_MATERIALIZED_ROWS = 100_000

AXES: dict[str, tuple[Any, ...]] = {
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

NATIVE_OSC = {
    "free-running-crystal": "crystal",
    "gpsdo-hobbyist-xo": "gpsdo-xo",
    "gpsdo-tcxo": "gpsdo-tcxo",
    "gpsdo-ocxo": "gpsdo-ocxo",
    "cb-ssb-20ppm": "cb-ssb",
    "ic-7100-0.5ppm": "ic-7100",
}


def product_count(axes: dict[str, tuple[Any, ...]] = AXES) -> int:
    return math.prod(len(values) for values in axes.values())


def mixed_radix_row(ordinal: int, axes: dict[str, tuple[Any, ...]] = AXES) -> dict[str, Any]:
    total = product_count(axes)
    if ordinal < 0 or ordinal >= total:
        raise IndexError(f"row id {ordinal} outside [0,{total})")
    names = tuple(axes)
    row: dict[str, Any] = {}
    n = ordinal
    for name in reversed(names):
        values = axes[name]
        n, index = divmod(n, len(values))
        row[name] = values[index]
    return {name: row[name] for name in names}


def row_id(row: dict[str, Any], axes: dict[str, tuple[Any, ...]] = AXES) -> int:
    ordinal = 0
    for name, values in axes.items():
        try:
            index = values.index(row[name])
        except (KeyError, ValueError) as exc:
            raise ValueError(f"invalid value for {name}: {row.get(name)!r}") from exc
        ordinal = ordinal * len(values) + index
    return ordinal


def pairwise_rows(axes: dict[str, tuple[Any, ...]] = AXES) -> list[dict[str, Any]]:
    """Construct rows whose set provably covers every pair of axis levels."""
    names = tuple(axes)
    rows: dict[tuple[int, ...], dict[str, Any]] = {}
    for i, j in itertools.combinations(range(len(names)), 2):
        left, right = axes[names[i]], axes[names[j]]
        for ai, av in enumerate(left):
            for bj, bv in enumerate(right):
                indices = []
                for k, name in enumerate(names):
                    width = len(axes[name])
                    if k == i:
                        indices.append(ai)
                    elif k == j:
                        indices.append(bj)
                    else:
                        # Deterministic, level-mixing fill.  Pair coverage
                        # follows from explicitly visiting each (i,j,ai,bj).
                        indices.append((ai * 17 + bj * 31 + i * 7 + j * 13 + k * 23) % width)
                rows.setdefault(tuple(indices), {
                    name: axes[name][index] for name, index in zip(names, indices)
                })
    return list(rows.values())


def pair_coverage(rows: list[dict[str, Any]], axes: dict[str, tuple[Any, ...]] = AXES) -> dict[str, Any]:
    names = tuple(axes)
    pairs: dict[str, dict[str, set[tuple[str, str]]]] = {}
    for i, j in itertools.combinations(range(len(names)), 2):
        left, right = names[i], names[j]
        covered = {(json.dumps(row[left], sort_keys=True), json.dumps(row[right], sort_keys=True))
                   for row in rows}
        expected = {(json.dumps(a, sort_keys=True), json.dumps(b, sort_keys=True))
                    for a in axes[left] for b in axes[right]}
        pairs[f"{left} x {right}"] = {"covered": covered, "expected": expected}
    covered_total = sum(len(value["covered"]) for value in pairs.values())
    expected_total = sum(len(value["expected"]) for value in pairs.values())
    missing = {key: len(value["expected"] - value["covered"])
               for key, value in pairs.items() if value["expected"] - value["covered"]}
    return {"covered_pairs": covered_total, "total_pairs": expected_total,
            "complete": not missing, "missing_by_axis_pair": missing,
            "axis_pair_counts": {key: {"covered": len(value["covered"]),
                                        "total": len(value["expected"])}
                                 for key, value in pairs.items()}}


def project_configuration(row: dict[str, Any]) -> dict[str, Any]:
    unsupported: list[dict[str, str]] = []
    warnings: list[str] = []
    if row["shift_hz"] == 0:
        unsupported.append({"dimension": "shift_hz", "requested": "0",
                            "reason": "triad_long --rf-hz requires a positive value and its meaning is oscillator-reference frequency, not a zero-shift carrier selector"})
    if row["shift_oscillator"] == "baseband-clock":
        unsupported.append({"dimension": "shift_oscillator",
                            "requested": "baseband-clock",
                            "reason": "no shared baseband-clock TX/RX oscillator topology; choosing identical preset IDs still creates independent endpoint draws"})
    if row["spread"] == "none" and row["selective"] != "off":
        warnings.append("selected added amplitude modulation is bypassed when spread Doppler is zero; this setting does not produce modulation in the sampled channel")
    if row["fhss"] == "envelope-only":
        warnings.append("the CLI FHSS mode shapes a source envelope; it does not retune the physical RF channel or model missed RF hops")
    if row["symbol_burst"] == "3x":
        warnings.append("whole-bit Symbol Burst changes UTC calendar spacing only; it does not test backlog processing inside a bit")
    args = ["--mode", "configure", "--seconds", "8",
            "--rate", format(float(row["rate_hz"]), ".17g"),
            "--added", str(row["added_symbols"]),
            "--cn0", format(float(row["cn0_db_hz"]), ".17g"),
            "--base-oscillator", NATIVE_OSC[row["base_oscillator"]],
            "--spread-preset", str(row["spread"]),
            "--selective-perturbation", str(row["selective"]),
            "--margin", format(float(row["margin"]), ".17g"),
            "--nominal-if-hz", format(baseband_hz(row), ".17g")]
    if row["shift_hz"] > 0:
        args += ["--rf-hz", format(float(row["shift_hz"]), ".17g")]
    if row["shift_oscillator"] in NATIVE_OSC:
        args += ["--shift-oscillator", NATIVE_OSC[row["shift_oscillator"]]]
    if row["timing"] == "gps-10ms-1ms-0ms-audio70ms":
        args += ["--clock-sync", "GPS_10ms-1ms_region-0ms_offset", "--audio-error-ms", "70",
                 "--start", "-0.090500000001", "0.090500000001",
                 "0.004166666666666667"]
    else:
        # Reproduce the observed default interval as both the actual arrival
        # prior and public receiver candidates. Configure does not score them.
        args += ["--clock-sync", "default", "--start", "-7.035", "7.035",
                 "0.004166666666666667", "--arrival-prior-seconds", "-7.035", "7.035"]
    if row["fhss"] == "envelope-only":
        args.append("--fhss")
    if row["symbol_burst"] == "3x":
        args += ["--symbol-burst", "3"]
    return {
        "cli_projection": ["triad_long", *args],
        "represented_dimensions": ["rate_hz", "shift_hz (positive values only)",
                                    "timing", "base_oscillator", "shift_oscillator (except shared baseband clock)",
                                    "spread", "selective", "cn0_db_hz", "margin", "added_symbols",
                                    "fhss (envelope only)", "symbol_burst (calendar only)"],
        "unsupported_dimensions": unsupported,
        "configuration_warnings": warnings,
        "frontend_interpretation": {
            "requested_baseband_carrier": row["baseband_carrier"],
            "nominal_if_hz": baseband_hz(row),
            "status": "approximate_reference_mapping_unvalidated_as_physical_carrier_placement",
            "note": "native configure uses nominal IF in oscillator-error calculations; waveform generation remains zero-centered complex baseband",
        },
        "receiver_grid_status": "bounded timing grid requested; likelihood-grid loss and reception not validated by configure",
        "configuration_probe_status": "not_run",
        "reception_status": "untested",
    }


def baseband_hz(row: dict[str, Any]) -> float:
    selected = row["baseband_carrier"]
    if selected == "1725-Hz":
        return 1725.0
    if selected == "8270-Hz":
        return 8270.0
    if selected == "half-rate":
        return float(row["rate_hz"]) / 2.0
    raise ValueError(f"unknown baseband carrier {selected!r}")


def manifest() -> dict[str, Any]:
    primary_axes = {name: values for name, values in AXES.items()
                    if name not in {"margin", "added_symbols", "fhss", "symbol_burst"}}
    return {
        "schema": "triad-broad-configuration-space-v1",
        "axes": {name: list(values) for name, values in AXES.items()},
        "axis_cardinalities": {name: len(values) for name, values in AXES.items()},
        "primary_parameter_combinations": product_count(primary_axes),
        "primary_parameter_count_formula": "17*3*9*2*4*7*10*16*19",
        "combination_count_with_requested_operating_axes": product_count(),
        "combination_count_formula": "17*3*9*2*4*7*10*16*19*2*4*2*2",
        "extra_axes": ["margin", "added_symbols", "fhss", "symbol_burst"],
        "notes": [
            "Count is a Cartesian intent space; it does not imply waveform validation or reception coverage.",
            "DSSS factors are the isolated v3 Added Symbols coordinate; v3 currently always interleaves it.",
            "FHSS envelope-only does not enable physical RF retuning.",
            "Symbol Burst changes whole-bit calendar spacing and does not model within-bit receiver backlog.",
            "Baseband clock and zero shift are explicit unsupported coordinates; nominal-IF mapping does not place an actual audio carrier.",
            "With the None spread preset, selected added modulation is bypassed because the effective Doppler is zero.",
            "Default timing configure rows declare the observed 14.07-second candidate interval; they do not randomize actual arrival or score candidates.",
            "The CLI projection is an explicit subset and must not be reported as validation of unsupported coordinates.",
        ],
    }


def build_plan(start: int, limit: int | None, covering: bool) -> dict[str, Any]:
    all_count = product_count()
    if covering:
        rows = pairwise_rows()
        if limit is None:
            limit = len(rows)
        if limit > MAX_MATERIALIZED_ROWS:
            raise ValueError(f"materialization limit exceeds safety cap {MAX_MATERIALIZED_ROWS}")
        if start < 0 or limit < 1 or start >= len(rows):
            raise ValueError("offset/limit outside pairwise witness set")
        selected = rows[start:start + limit]
        universe_coverage = pair_coverage(rows)
        selected_coverage = pair_coverage(selected)
        for row in selected:
            row["row_id"] = row_id(row)
        return {"schema": "triad-broad-coverage-plan-v1", "plan_type": "pairwise",
                "configuration_space": manifest(),
                "pairwise_universe_coverage": universe_coverage,
                "selected_row_pair_coverage": selected_coverage,
                "row_count": len(rows), "selected_count": len(selected),
                "offset": start, "rows": [{"row_id": row["row_id"], "requested": {k: v for k,v in row.items() if k != "row_id"},
                                           **project_configuration(row)} for row in selected]}
    if limit is None:
        limit = 1
    if limit > MAX_MATERIALIZED_ROWS:
        raise ValueError(f"materialization limit exceeds safety cap {MAX_MATERIALIZED_ROWS}")
    if start < 0 or limit < 1 or start >= all_count:
        raise ValueError("offset/limit outside configuration space")
    stop = min(all_count, start + limit)
    rows = []
    for ordinal in range(start, stop):
        row = mixed_radix_row(ordinal)
        rows.append({"row_id": ordinal, "requested": row, **project_configuration(row)})
    return {"schema": "triad-broad-coverage-plan-v1", "plan_type": "lazy-lexicographic-slice",
            "configuration_space": manifest(), "offset": start,
            "selected_count": len(rows), "rows": rows}


def file_sha256(path: Path) -> str | None:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def normalize_configure_result(returncode: int, stdout: str, stderr: str,
                               row_id_value: int, argv: list[str],
                               elapsed_seconds: float | None = None) -> dict[str, Any]:
    """Separate geometry acceptance from deterministic capture and search support."""
    try:
        parsed = json.loads(stdout)
        if not isinstance(parsed, dict) or not isinstance(parsed.get("configuration"), dict):
            raise ValueError("expected top-level configuration object")
        parse_error = None
    except (json.JSONDecodeError, ValueError) as exc:
        parsed, parse_error = None, str(exc)
    configuration = parsed["configuration"] if parsed else {}
    capture = configuration.get("capture", {})
    search = configuration.get("public_search", {})
    estimates = configuration.get("search_estimates", {})
    geometry = "accepted" if returncode == 0 and parsed else ("rejected" if returncode != 0 else "unknown")
    feasible = capture.get("feasible_for_deterministic_prior")
    support = "feasible" if feasible is True else ("infeasible" if feasible is False else "not_reported")
    result = {
        "row_id": row_id_value,
        "status": "source_geometry_accepted" if geometry == "accepted" else
                  ("source_geometry_rejected" if geometry == "rejected" else "invalid_output"),
        "process_returncode": returncode,
        "source_geometry_acceptance": geometry,
        "capture_support": support,
        "capture_feasible_for_deterministic_prior": feasible if isinstance(feasible, bool) else None,
        "public_search_coverage": {
            name: search.get(name) for name in (
                "covers_initial_timing_prior", "covers_static_oscillator_frequency_prior",
                "covers_audio_clock_prior", "covers_suggested_epochs",
                "timing_bounds_seconds", "frequency_bounds_hz", "clock_bounds_ppm",
                "epoch_alternatives", "estimated_hypotheses", "materialization_cap") if name in search
        },
        "search_materialization_status": (
            "not_reported" if not isinstance(search.get("estimated_hypotheses"), (int, float)) or
            not isinstance(search.get("materialization_cap"), (int, float)) else
            ("exceeds_declared_cap" if search["estimated_hypotheses"] > search["materialization_cap"]
             else "within_declared_cap")),
        "search_grid_diagnostics": {
            name: estimates.get(name) for name in (
                "timing_maximum_step_seconds", "timing_loss_budget_db",
                "timing_positions", "whole_bit_frequency_maximum_step_hz",
                "whole_bit_frequency_positions", "block_frequency_maximum_step_hz",
                "clock_maximum_step_ppm", "clock_positions",
                "timing_model_is_finite_waveform_certificate", "timing_model_limits")
            if name in estimates
        },
        "configuration_json_valid": parsed is not None,
        "configuration_parse_error": parse_error,
        "argv": argv, "stdout": stdout, "stderr": stderr,
        "stdout_sha256": hashlib.sha256(stdout.encode()).hexdigest(),
        "stderr_sha256": hashlib.sha256(stderr.encode()).hexdigest(),
        "measurement_scope": "configure geometry only; no reception performed",
    }
    if elapsed_seconds is not None:
        result["elapsed_seconds"] = elapsed_seconds
    return result


def native_probe(rows: list[dict[str, Any]], binary: Path, total_seconds: float = 120,
                 case_seconds: float = 5) -> dict[str, Any]:
    """Bounded exact configure probes; unsupported dimensions are never dropped."""
    if not binary.is_file():
        raise ValueError(f"triad_long binary does not exist: {binary}")
    binary_sha_before = file_sha256(binary)
    tool_path = Path(__file__).resolve()
    tool_sha_before = file_sha256(tool_path)
    start = time.monotonic()
    env = os.environ.copy()
    env.update({"OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
                "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1"})
    receipts = []
    stopped_reason = None
    for item in rows:
        rowid = item["row_id"]
        projection = project_configuration(item["requested"])
        argv = [str(binary), *projection["cli_projection"][1:]]
        if projection["unsupported_dimensions"]:
            receipts.append({"row_id": rowid, "status": "not_run_unsupported",
                             "unsupported_dimensions": projection["unsupported_dimensions"],
                             "argv": argv})
            continue
        remaining = total_seconds - (time.monotonic() - start)
        if remaining <= 0:
            stopped_reason = "global probe deadline exhausted"
            receipts.append({"row_id": rowid, "status": "not_started_deadline", "argv": argv})
            continue
        timeout = min(case_seconds, remaining)
        case_started = time.monotonic()
        try:
            result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout,
                                    check=False, env=env)
            receipts.append(normalize_configure_result(
                result.returncode, result.stdout, result.stderr, rowid, argv,
                time.monotonic() - case_started))
        except subprocess.TimeoutExpired as exc:
            receipts.append({"row_id": rowid, "status": "timeout", "argv": argv,
                             "timeout_seconds": timeout,
                             "stdout": exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or ""),
                             "stderr": exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")})
        if time.monotonic() - start >= total_seconds:
            stopped_reason = "global probe deadline exhausted"
            remaining_items = rows[len(receipts):]
            receipts.extend({"row_id": future["row_id"], "status": "not_started_deadline"}
                            for future in remaining_items)
            break
    counts: dict[str, int] = {}
    for receipt in receipts:
        counts[receipt["status"]] = counts.get(receipt["status"], 0) + 1
    binary_sha_after = file_sha256(binary)
    tool_sha_after = file_sha256(tool_path)
    identity_stable = (binary_sha_before is not None and binary_sha_before == binary_sha_after
                       and tool_sha_before is not None and tool_sha_before == tool_sha_after)
    capture_counts = {"feasible": 0, "infeasible": 0, "not_reported": 0}
    search_counts: dict[str, dict[str, int]] = {}
    materialization_counts: dict[str, int] = {}
    for receipt in receipts:
        if receipt.get("source_geometry_acceptance") != "accepted":
            continue
        capture_counts[receipt["capture_support"]] += 1
        materialization = receipt.get("search_materialization_status", "not_reported")
        materialization_counts[materialization] = materialization_counts.get(materialization, 0) + 1
        for name, value in receipt["public_search_coverage"].items():
            key = json.dumps(value, sort_keys=True)
            per_dimension = search_counts.setdefault(name, {})
            per_dimension[key] = per_dimension.get(key, 0) + 1
    invalid = any(receipt["status"] in {"timeout", "not_started_deadline", "invalid_output"}
                  for receipt in receipts)
    return {"schema": "triad-native-configure-probe-v1", "binary": str(binary),
            "binary_sha256_before": binary_sha_before, "binary_sha256_after": binary_sha_after,
            "tool_source": str(tool_path), "tool_source_sha256_before": tool_sha_before,
            "tool_source_sha256_after": tool_sha_after, "identity_stable": identity_stable,
            "comparison_valid": identity_stable and not invalid,
            "total_seconds_bound": total_seconds, "per_case_seconds_bound": case_seconds,
            "elapsed_seconds": time.monotonic() - start, "stopped_reason": stopped_reason,
            "counts": counts, "capture_support_counts": capture_counts,
            "search_materialization_counts": materialization_counts,
            "public_search_coverage_counts": search_counts,
            "probe_outcome_pair_coverage": {
                status: pair_coverage([item["requested"]
                                      for item, receipt in zip(rows, receipts)
                                      if receipt["status"] == status])
                for status in counts if status not in {"not_started_deadline"}
            },
            "receipts": receipts}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offset", type=int, default=0, help="lazy Cartesian row offset")
    parser.add_argument("--limit", type=int, help="rows to materialize; defaults to 1 for lazy slices, all witnesses for --covering")
    parser.add_argument("--covering", action="store_true", help="emit deterministic all-axis pairwise witnesses")
    parser.add_argument("--probe", action="store_true", help="run bounded exact native configure checks for selected represented rows")
    parser.add_argument("--triad-long", type=Path, help="required binary path with --probe")
    parser.add_argument("--probe-global-seconds", type=float, default=120.0)
    parser.add_argument("--probe-case-seconds", type=float, default=5.0)
    parser.add_argument("--output", type=Path, help="write JSON to this path instead of stdout")
    args = parser.parse_args(argv)
    if args.probe and not args.triad_long:
        parser.error("--probe requires --triad-long")
    if args.probe and not (0 < args.probe_case_seconds <= 5 and
                           0 < args.probe_global_seconds <= 120):
        parser.error("probe limits must be positive and no more than 5 seconds per case / 120 seconds total")
    if args.limit is not None and args.limit > MAX_MATERIALIZED_ROWS:
        parser.error(f"--limit may not exceed the {MAX_MATERIALIZED_ROWS}-row materialization safety cap")
    if args.output and args.output.exists():
        parser.error(f"refusing to overwrite existing evidence file: {args.output}")
    try:
        result = build_plan(args.offset, args.limit, args.covering)
        if args.probe:
            result["native_probe"] = native_probe(result["rows"], args.triad_long,
                                                   args.probe_global_seconds,
                                                   args.probe_case_seconds)
            by_id = {receipt["row_id"]: receipt["status"]
                     for receipt in result["native_probe"]["receipts"]}
            for row in result["rows"]:
                row["configuration_probe_status"] = by_id[row["row_id"]]
    except (ValueError, IndexError) as exc:
        parser.error(str(exc))
    encoded = json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Validate and summarize receiver FFT work-capture rows from receiver-feed JSONL."""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

WORK_FIELDS = (
    "input_transforms", "template_jobs", "exact_reused_template_jobs",
    "skipped_batches", "skipped_start_trials", "threshold_trials",
    "retained_start_trials", "scored_start_trials", "early_batches",
    "direct_jobs", "fft_jobs", "skipped_template_jobs", "partitioned_jobs",
    "partitioned_input_transforms", "partitioned_transform_points",
    "partitioned_fallbacks", "arithmetic_fallback_jobs", "native_fp32_transforms",
    "tabulated_fp64_transforms", "fp32_retry_fp64_transforms",
    "private_template_builds", "private_template_lane_jobs",
)
GEOMETRY_FIELDS = (
    "bin_samples", "length", "transform", "hop", "initial_stream_symbols",
    "frequency_count", "requested_workers", "drift_sections", "sample_fit",
    "streamed_templates", "exact_template_cache_rows", "parallel_workspace_count",
    "restricted_timing_active", "timing_window",
)
WORK_ROW_FIELDS = (
    "snapshots", "sample_before", "sample_after", "bins_before", "bins_after",
    "next_start_before", "next_start_after", "tracks_before", "tracks_after",
)


def _uint(value: Any, where: str, errors: list[str]) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        errors.append(f"{where}: expected a nonnegative integer")
        return None
    return value


def check_work_capture(rows: list[dict]) -> dict:
    """Validate capture relationships and return counts, totals, and validation flags.

    Unknown row types are ignored so the checker can consume the complete feed
    while remaining independent of unrelated feed schemas.
    """
    errors: list[str] = []
    blocks: dict[int, dict] = {}
    receivers: dict[tuple[int, int], dict] = {}
    work: dict[tuple[int, int], dict] = {}
    stages: list[dict] = []
    recognized = Counter()

    for pos, row in enumerate(rows):
        if not isinstance(row, dict):
            errors.append(f"row {pos}: expected an object")
            continue
        kind = row.get("type")
        if kind not in {"block", "receiver", "receiver_work", "receiver_stage"}:
            continue
        recognized[kind] += 1
        for field, value in row.items():
            if isinstance(value, int) and not isinstance(value, bool) and value < 0:
                errors.append(f"row {pos} {kind}.{field}: expected a nonnegative integer")
        if kind == "block":
            index = _uint(row.get("index"), f"row {pos} block.index", errors)
            if index is not None:
                if index in blocks:
                    errors.append(f"row {pos}: duplicate block index {index}")
                else:
                    blocks[index] = row
        elif kind in {"receiver", "receiver_work", "receiver_stage"}:
            block = _uint(row.get("block"), f"row {pos} {kind}.block", errors)
            receiver_index = _uint(row.get("receiver_index"), f"row {pos} {kind}.receiver_index", errors)
            if block is None or receiver_index is None:
                continue
            key = (block, receiver_index)
            if kind == "receiver":
                if key in receivers:
                    errors.append(f"row {pos}: duplicate receiver metadata for {key}")
                else:
                    receivers[key] = row
            elif kind == "receiver_work":
                if key in work:
                    errors.append(f"row {pos}: duplicate receiver_work row for {key}")
                else:
                    work[key] = row
            else:
                stages.append(row)

    for key in receivers:
        if key[0] not in blocks:
            errors.append(f"receiver metadata {key}: references missing block")
    for key in work:
        if key[0] not in blocks:
            errors.append(f"receiver_work {key}: references missing block")
        if key not in receivers:
            errors.append(f"receiver_work {key}: missing receiver metadata")
    for pos, row in enumerate(stages):
        block = row.get("block")
        receiver_index = row.get("receiver_index")
        calls = _uint(row.get("calls"), f"receiver_stage[{pos}].calls", errors)
        if block not in blocks:
            errors.append(f"receiver_stage[{pos}]: references missing block")
        if (block, receiver_index) not in receivers:
            errors.append(f"receiver_stage[{pos}]: missing receiver metadata")
        if row.get("stage") == "input_binning" and calls and calls > 0:
            if (block, receiver_index) not in work:
                errors.append(f"receiver_stage[{pos}]: input_binning calls require matching receiver_work")

    totals = {name: 0 for name in WORK_FIELDS}
    per_block: dict[str, dict[str, int]] = defaultdict(lambda: {name: 0 for name in WORK_FIELDS})
    geometries: Counter[tuple[int, ...]] = Counter()
    valid_work_rows = 0
    for key, row in work.items():
        valid_work_rows += 1
        prefix = f"receiver_work {key}"
        for field in WORK_ROW_FIELDS:
            _uint(row.get(field), f"{prefix}.{field}", errors)
        snapshots = row.get("snapshots")
        if isinstance(snapshots, int) and not isinstance(snapshots, bool) and snapshots <= 0:
            errors.append(f"{prefix}.snapshots: expected a positive integer")
        for before, after in (("sample_before", "sample_after"), ("bins_before", "bins_after"),
                              ("next_start_before", "next_start_after")):
            a, b = row.get(before), row.get(after)
            if (isinstance(a, int) and not isinstance(a, bool) and a >= 0 and
                    isinstance(b, int) and not isinstance(b, bool) and b >= 0 and b < a):
                errors.append(f"{prefix}.{after}: must be monotone from {before}")

        delta = row.get("delta")
        if not isinstance(delta, dict):
            errors.append(f"{prefix}.delta: expected an object")
            delta = {}
        if set(delta) != set(WORK_FIELDS):
            errors.append(f"{prefix}.delta: counter fields must exactly match schema")
        for field in WORK_FIELDS:
            value = _uint(delta.get(field), f"{prefix}.delta.{field}", errors)
            if value is not None:
                totals[field] += value
                per_block[str(key[0])][field] += value

        geometry = row.get("last_geometry")
        if not isinstance(geometry, dict):
            errors.append(f"{prefix}.last_geometry: expected an object")
            geometry = {}
        if set(geometry) != set(GEOMETRY_FIELDS):
            errors.append(f"{prefix}.last_geometry: fields must exactly match schema")
        geometry_values: dict[str, int] = {}
        for field in GEOMETRY_FIELDS:
            value = _uint(geometry.get(field), f"{prefix}.last_geometry.{field}", errors)
            if value is not None:
                geometry_values[field] = value
        if len(geometry_values) == len(GEOMETRY_FIELDS):
            length = geometry_values["length"]
            transform = geometry_values["transform"]
            if length <= 0:
                errors.append(f"{prefix}.last_geometry.length: must be positive")
            if transform < length or transform <= 0 or transform & (transform - 1):
                errors.append(f"{prefix}.last_geometry.transform: must be a power of two at least length")
            for field in ("hop", "bin_samples", "initial_stream_symbols", "frequency_count"):
                if geometry_values[field] <= 0:
                    errors.append(f"{prefix}.last_geometry.{field}: must be positive")
            geometries[tuple(geometry_values[field] for field in GEOMETRY_FIELDS)] += 1

    if valid_work_rows == 0:
        errors.append("capture contains zero receiver_work records")

    # Totals are derived once from every per-receiver work row, then grouped by
    # block. Verify grouping preserves the same totals before exposing both views.
    grouped_totals = {name: sum(values[name] for values in per_block.values()) for name in WORK_FIELDS}
    reconciled = grouped_totals == totals
    if not reconciled:
        errors.append("per-block and global work totals do not reconcile")

    geometry_counts = [
        {"geometry": dict(zip(GEOMETRY_FIELDS, key)), "occurrences": count}
        for key, count in sorted(geometries.items())
    ]
    flags = {
        "blocks_present": bool(blocks),
        "receiver_work_present": valid_work_rows > 0,
        "references_valid": not any("missing" in error or "duplicate" in error for error in errors),
        "values_valid": not any("integer" in error or "monotone" in error or "positive" in error or "power of two" in error or "exactly match schema" in error or "fields must exactly" in error for error in errors),
        "work_totals_reconciled": reconciled,
    }
    return {
        "valid": not errors,
        "errors": errors,
        "counts": {
            "blocks": len(blocks), "receivers": len(receivers),
            "receiver_work_rows": len(work), "useful_records": valid_work_rows,
            "receiver_stage_rows": recognized["receiver_stage"],
            "input_binning_stage_rows": sum(row.get("stage") == "input_binning" and
                                                isinstance(row.get("calls"), int) and
                                                not isinstance(row.get("calls"), bool) and row["calls"] > 0
                                                for row in stages),
        },
        "work_totals": totals,
        "work_totals_by_block": dict(sorted(per_block.items(), key=lambda item: int(item[0]))),
        "geometry_occurrences": geometry_counts,
        "validation": flags,
    }


def _read_jsonl(path: Path) -> list[dict]:
    rows = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {exc.msg}") from exc
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path, help="receiver-feed.jsonl")
    parser.add_argument("--output", required=True, type=Path, help="new JSON summary path")
    args = parser.parse_args(argv)
    try:
        result = check_work_capture(_read_jsonl(args.capture))
        # Exclusive creation enforces the CLI's fresh-output contract.
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write("\n")
    except (OSError, ValueError) as exc:
        print(f"work_capture_check: {exc}", file=sys.stderr)
        return 2
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

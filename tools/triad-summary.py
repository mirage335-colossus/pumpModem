#!/usr/bin/env python3
"""Summarize completed triad-study JSONL runs without statistical pooling errors."""

from __future__ import annotations

import argparse
import gzip
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


CASE_FIELDS = (
    "case", "seed", "epoch", "snr_db", "receiver", "arithmetic", "rate_hz",
    "sample_rate", "bits_per_frame", "source_seconds", "samples", "tail_samples", "bits",
    "frequency_bound_hz", "frequency_banks", "frequency_step_hz",
    "rf_hz", "tx_ppm", "rx_ppm", "tx_audio_ppm", "rx_audio_ppm",
    "tx_phase_diffusion", "rx_phase_diffusion", "true_timing_seconds",
    "capture_lead_seconds", "delay_ms", "spread_hz", "selective_slow",
    "selective_fast", "fhss", "dropout_ms", "dropout_start_ms", "dropout_every_n_dwells",
    "added_symbols", "added_interleave", "burst_wait_frames", "threads_used",
    "segment_cells", "memory_limit_bytes", "minimum_score", "minimum_margin",
    "capture_trail_seconds", "static_doppler_hz", "tx_drift_ppm_per_second",
    "rx_drift_ppm_per_second", "complete_observation", "noise_enabled",
)
REQUIRED_CASE = set(CASE_FIELDS) | {
    "control", "errors", "accepted", "frame_recovered", "false_accept",
    "score", "margin", "hypotheses", "wall_seconds", "cpu_seconds", "working_bytes",
}
NUMERIC_FIELDS = {
    "seed", "epoch", "snr_db", "rate_hz", "sample_rate", "bits_per_frame", "source_seconds", "samples", "tail_samples", "bits",
    "frequency_bound_hz", "frequency_banks", "frequency_step_hz", "rf_hz", "tx_ppm", "rx_ppm",
    "tx_audio_ppm", "rx_audio_ppm", "tx_phase_diffusion", "rx_phase_diffusion",
    "true_timing_seconds", "capture_lead_seconds", "delay_ms", "spread_hz",
    "selective_slow", "selective_fast", "dropout_ms", "dropout_start_ms", "dropout_every_n_dwells",
    "added_symbols", "burst_wait_frames", "threads_used", "segment_cells",
    "memory_limit_bytes", "errors", "score", "margin", "hypotheses",
    "minimum_score", "minimum_margin", "capture_trail_seconds", "static_doppler_hz",
    "tx_drift_ppm_per_second", "rx_drift_ppm_per_second", "wall_seconds", "cpu_seconds", "working_bytes",
}
BOOL_FIELDS = {
    "fhss", "added_interleave", "complete_observation", "noise_enabled", "control", "accepted",
    "frame_recovered", "false_accept",
}


def fail(path: Path, line: int, message: str) -> ValueError:
    return ValueError(f"{path}:{line}: {message}")


def read_log(path: Path, seen: set[tuple[Any, ...]]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    studies: list[dict[str, Any]] = []
    cases: list[dict[str, Any]] = []
    complete = False
    completion: dict[str, Any] | None = None
    try:
        stream = gzip.open(path, "rt", encoding="utf-8") if path.suffix == ".gz" else path.open(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    with stream:
        for lineno, raw in enumerate(stream, 1):
            if not raw.strip():
                continue
            try:
                row = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise fail(path, lineno, f"invalid JSON: {exc.msg}") from exc
            if not isinstance(row, dict):
                raise fail(path, lineno, "JSONL record is not an object")
            record_type = row.get("type")
            if record_type == "study":
                if complete or cases:
                    raise fail(path, lineno, "study header must precede case rows and completion")
                if row.get("schema") != 1:
                    raise fail(path, lineno, "unsupported study schema")
                studies.append(row)
            elif record_type == "case":
                if complete or not studies:
                    raise fail(path, lineno, "case row is outside an open study")
                missing = sorted(REQUIRED_CASE - row.keys())
                if missing:
                    raise fail(path, lineno, f"case row lacks fields: {', '.join(missing)}")
                for field in NUMERIC_FIELDS:
                    value = row[field]
                    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                        raise fail(path, lineno, f"case field {field} must be a finite number")
                for field in BOOL_FIELDS:
                    if row[field] not in (False, True, 0, 1) or not isinstance(row[field], (bool, int)):
                        raise fail(path, lineno, f"case field {field} must be a JSON boolean or 0/1 integer")
                    row[field] = bool(row[field])
                if row["receiver"] not in {"differential", "segmented-complex", "amplitude-only"}:
                    raise fail(path, lineno, "unknown receiver")
                if row["arithmetic"] not in {"automatic", "fp32", "fp64"}:
                    raise fail(path, lineno, "unknown arithmetic")
                if row["bits"] <= 0 or row["errors"] < 0 or row["errors"] > row["bits"]:
                    raise fail(path, lineno, "invalid bit/error count")
                if row["threads_used"] < 0 or row["working_bytes"] < 0 or row["hypotheses"] < 0:
                    raise fail(path, lineno, "invalid resource/search count")
                if (row["hypotheses"] == 0) != (row["threads_used"] == 0):
                    raise fail(path, lineno, "zero-thread work must have zero hypotheses")
                if row["wall_seconds"] < 0 or row["cpu_seconds"] < 0:
                    raise fail(path, lineno, "negative elapsed time")
                if row["false_accept"] != (row["control"] and row["accepted"]):
                    raise fail(path, lineno, "false_accept does not match control and accepted fields")
                if row["frame_recovered"] != (
                    not row["control"] and row["errors"] == 0 and row["accepted"]
                ):
                    raise fail(path, lineno, "frame_recovered does not match case outcome")
                # The same scientific input and seed cannot be silently counted twice,
                # even if timings differ on replay. Arithmetic and thread count stay
                # in the key because these are separate numerical/resource runs.
                identity = tuple(row[field] for field in CASE_FIELDS if field not in {
                    "complete_observation", "segment_cells", "memory_limit_bytes", "threads_used",
                }) + (row["segment_cells"], row["memory_limit_bytes"], row["threads_used"])
                if identity in seen:
                    raise fail(path, lineno, "duplicate experimental case identity")
                seen.add(identity)
                cases.append(row)
            elif record_type == "completion":
                if complete or not studies:
                    raise fail(path, lineno, "completion is outside an open study")
                if row.get("complete") is not True or not isinstance(row.get("strict_failures"), int):
                    raise fail(path, lineno, "invalid completion record")
                completion = row
                complete = True
            else:
                raise fail(path, lineno, f"unknown record type: {record_type!r}")
    if not studies:
        raise ValueError(f"{path}: missing study header")
    if not complete or completion is None:
        raise ValueError(f"{path}: incomplete JSONL run (missing completion record)")
    return {"path": str(path), "headers": studies, "completion": completion}, cases


def wilson(successes: int, total: int, z: float = 1.959963984540054) -> tuple[float, float] | None:
    if total == 0:
        return None
    p = successes / total
    d = 1 + z * z / total
    center = (p + z * z / (2 * total)) / d
    radius = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / d
    return max(0.0, center - radius), min(1.0, center + radius)


def summarize(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    # Rates, channel truth, timing grids and search/resource policy remain in
    # each group key. Threads are explicit because CPU timing cannot be pooled
    # across unlike allocations.
    keys = (
        "case", "snr_db", "receiver", "arithmetic", "rate_hz", "sample_rate",
        "source_seconds", "bits", "frequency_bound_hz", "frequency_banks", "frequency_step_hz", "rf_hz",
        "tx_ppm", "rx_ppm", "tx_audio_ppm", "rx_audio_ppm", "tx_phase_diffusion",
        "rx_phase_diffusion", "tx_drift_ppm_per_second", "rx_drift_ppm_per_second",
        "true_timing_seconds", "capture_lead_seconds", "capture_trail_seconds", "delay_ms",
        "spread_hz", "static_doppler_hz", "selective_slow", "selective_fast", "fhss", "noise_enabled", "dropout_ms",
        "dropout_start_ms", "dropout_every_n_dwells", "added_symbols", "added_interleave",
        "burst_wait_frames", "bits_per_frame", "threads_used", "segment_cells", "memory_limit_bytes",
        "minimum_score", "minimum_margin",
    )
    buckets: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        buckets[tuple(row[field] for field in keys)].append(row)
    results = []
    for key, group in buckets.items():
        record = dict(zip(keys, key))
        controls = [row for row in group if row["control"]]
        signals = [row for row in group if not row["control"]]
        recovered = sum(bool(row["frame_recovered"]) for row in signals)
        false_accepts = sum(bool(row["false_accept"]) for row in controls)
        wrong_accepted = sum(row["accepted"] and row["errors"] > 0 for row in signals)
        errors = sum(row["errors"] for row in signals)
        bits = sum(row["bits"] for row in signals)
        record.update({
            "trials": len(group),
            "signal_trials": len(signals),
            "control_trials": len(controls),
            "correct_accepted_frames": recovered,
            "frame_recovery_fraction": recovered / len(signals) if signals else None,
            "frame_recovery_wilson95": wilson(recovered, len(signals)),
            "control_false_accepts": false_accepts,
            "control_false_accept_fraction": false_accepts / len(controls) if controls else None,
            "control_false_accept_wilson95": wilson(false_accepts, len(controls)),
            "accepted_wrong_frames": wrong_accepted,
            "raw_bit_errors": errors,
            "raw_payload_bits": bits,
            "raw_ber": errors / bits if bits else None,
            "median_cpu_seconds": statistics.median(row["cpu_seconds"] for row in group),
            "median_wall_seconds": statistics.median(row["wall_seconds"] for row in group),
            "max_working_mib": max(row["working_bytes"] for row in group) / (1024 * 1024),
            "distinct_seeds": len({row["seed"] for row in group}),
        })
        results.append(record)
    return sorted(results, key=lambda row: (
        row["case"], float(row["snr_db"]), row["receiver"], row["arithmetic"],
        float(row["rate_hz"]), int(row["sample_rate"]),
        float(row["frequency_bound_hz"]), int(row["threads_used"]),
    ))


def pct(value: float | None) -> str:
    return "n/a" if value is None else f"{100 * value:.1f}%"


def render_markdown(results: list[dict[str, Any]], manifests: list[dict[str, Any]]) -> str:
    lines = [
        "| Case | SNR | Receiver | Rate / Fs | Search ±Hz | Threads | Accepted frames (Wilson 95%) | False accepts (Wilson 95%) | Raw errors / bits | Median CPU / wall s | Max workspace MiB |",
        "|---|---:|---|---:|---:|---:|---|---|---:|---:|---:|",
    ]
    for row in results:
        interval = row["frame_recovery_wilson95"]
        frame_text = f"{row['correct_accepted_frames']}/{row['signal_trials']} ({pct(row['frame_recovery_fraction'])})"
        if interval:
            frame_text += f" [{pct(interval[0])}–{pct(interval[1])}]"
        false_interval = row["control_false_accept_wilson95"]
        false_text = f"{row['control_false_accepts']}/{row['control_trials']} ({pct(row['control_false_accept_fraction'])})"
        if false_interval:
            false_text += f" [{pct(false_interval[0])}–{pct(false_interval[1])}]"
        lines.append(
            f"| {row['case']} | {row['snr_db']:g} | {row['receiver']} / {row['arithmetic']} | "
            f"{row['rate_hz']:g} / {row['sample_rate']:g} | ±{row['frequency_bound_hz']:g} | "
            f"{row['threads_used']} | {frame_text} | {false_text} | "
            f"{row['raw_bit_errors']} / {row['raw_payload_bits']} | "
            f"{row['median_cpu_seconds']:.4g} / {row['median_wall_seconds']:.4g} | {row['max_working_mib']:.3g} |"
        )
    lines.extend([
        "",
        "Frame intervals use Wilson 95% intervals across independent captured trials. "
        "Raw bit errors are reported exactly; bits within a frame are correlated, so no binomial confidence interval is attached to BER.",
        "Accepted wrong frames count signal rows accepted with at least one erroneous bit; inspect the JSON result for their count.",
        "",
        "Runs: " + ", ".join(f"{manifest['path']} ({manifest['completion']['strict_failures']} strict failures)" for manifest in manifests),
    ])
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit structured JSON instead of Markdown")
    parser.add_argument("paths", nargs="+", type=Path, help="completed triad-study JSONL logs")
    args = parser.parse_args(argv)
    seen: set[tuple[Any, ...]] = set()
    manifests = []
    rows = []
    try:
        for path in args.paths:
            manifest, cases = read_log(path, seen)
            manifests.append(manifest)
            rows.extend(cases)
        if not rows:
            raise ValueError("logs contain no case rows")
        summary = summarize(rows)
    except (OSError, ValueError) as exc:
        print(f"triad-summary: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"schema": 1, "runs": manifests, "groups": summary}, indent=2, sort_keys=True))
    else:
        print(render_markdown(summary, manifests), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Render a derived resident-time/success plot from consumer analysis summary."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


POLICIES = [
    "fp64", "fp32", "tensor:fp16", "tensor:bf16", "tensor:tf32",
    "tensor:fp8-e4m3", "tensor:fp8-e5m2", "tensor:int8",
]
GATES = [
    ("default_99pct_1e-4_day", "Default threshold setting"),
    ("relaxed_90pct_1pct_12h", "Relaxed threshold setting"),
]
COLORS = ["#3a3a3a", "#777777", "#197b8a", "#28a48f", "#d0a43a", "#ef9b50", "#e46e50", "#7951a8"]
DISPLAY = {
    "fp64": "FP64 correlation; shared inference",
    "fp32": "Strict FP32 correlation; shared inference",
    "tensor:fp16": "FP16",
    "tensor:bf16": "BF16",
    "tensor:tf32": "TF32",
    "tensor:fp8-e4m3": "FP8 E4M3",
    "tensor:fp8-e5m2": "FP8 E5M2",
    "tensor:int8": "INT8",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def input_stratum(name: str) -> str | None:
    for label in ("boundary", "target", "null"):
        if name.startswith(label + "-"):
            return label
    return None


def derive(summary: dict) -> list[dict]:
    groups: dict[tuple[str, float, str], list[dict]] = defaultdict(list)
    pooled_groups: dict[tuple[str, float], list[dict]] = defaultdict(list)
    seen: set[tuple[str, str, float]] = set()
    for row in summary["runtime"]:
        if row.get("stage") != "screening-holdout" or row.get("variant") != "holdout-verified":
            continue
        if row.get("complete_resident_s") is None or row.get("cn0_db_hz") is None:
            continue
        pooled_groups[(row["policy"], float(row["cn0_db_hz"]))].append(row)
        stratum = input_stratum(str(row.get("input", "")))
        if stratum == "null":
            continue
        if stratum not in ("boundary", "target"):
            raise ValueError(f"unclassified holdout input name: {row.get('input')!r}")
        key = (row["policy"], float(row["cn0_db_hz"]), stratum)
        capture_key = (row["policy"], str(row["input"]), key[1])
        if capture_key in seen:
            raise ValueError(f"duplicate runtime capture record: {capture_key}")
        seen.add(capture_key)
        groups[key].append(row)

    points = []
    for row in summary["holdout_pointwise"]:
        if row.get("variant") != "holdout-verified" or row.get("stratum") not in ("boundary", "target"):
            continue
        policy = row["policy"]
        if policy not in POLICIES:
            continue
        cn0 = float(row["cn0_db_hz"])
        stratum = row["stratum"]
        times = groups.get((policy, cn0, stratum), [])
        if not times:
            raise ValueError(f"no matching resident timing for {policy} at C/N0 {cn0}, stratum {stratum}")
        expected_count = int(row["trials"])
        if len(times) != expected_count or len({x["input"] for x in times}) != int(row["distinct_inputs"]):
            raise ValueError(
                f"timing/outcome count mismatch for {policy} at C/N0 {cn0}, stratum {stratum}: "
                f"timings={len(times)}, trials={expected_count}, distinct_inputs={row['distinct_inputs']}"
            )
        time_values = [float(x["complete_resident_s"]) for x in times]
        median_s = float(np.median(time_values))
        rate = float(row["rate"])
        if rate <= 0:
            central = None
        else:
            central = median_s / rate
        high_p = float(row["ci95_high"])
        low_p = float(row["ci95_low"])
        points.append({
            "variant": row["variant"], "policy": policy,
            "policy_display": DISPLAY[policy], "gate": row["gate"],
            "cn0_db_hz": cn0, "stratum": row["stratum"],
            "trials": int(row["trials"]), "successes": int(row["successes_or_null_admissions"]),
            "point_success_fraction": rate,
            "interval_method": row["interval_method"],
            "resident_timing_count": len(time_values),
            "median_complete_resident_s_per_capture": median_s,
            "time_per_observed_correct_admission_s": central,
            "time_interval_low_s": median_s / high_p if high_p > 0 else None,
            "time_interval_high_s": median_s / low_p if low_p > 0 else None,
            "upper_interval_unbounded": low_p == 0,
            "timing_scope": "complete_resident_s; includes per-input receiver/bank preparation; excludes capture generation and provisioning/install",
            "interpretation": "median resident time divided by observed correct-admission fraction; interval transforms pointwise reception-rate Hoeffding bounds with median timing held fixed; not a latency CI or qualification",
            "illustrative_compute_cost_usd_per_observed_correct_admission": None,
            "illustrative_compute_cost_interval_low_usd": None,
            "illustrative_compute_cost_interval_high_usd": None,
        })
    points.sort(key=lambda x: (x["gate"], x["policy"], x["cn0_db_hz"]))
    corrections = []
    for policy in POLICIES:
        signal_rows = groups.get((policy, -10.0, "boundary"), [])
        pooled_rows = pooled_groups.get((policy, -10.0), [])
        if not signal_rows or not pooled_rows:
            continue
        signal_median = float(np.median([float(r["complete_resident_s"]) for r in signal_rows]))
        pooled_median = float(np.median([float(r["complete_resident_s"]) for r in pooled_rows]))
        corrections.append({
            "policy": policy,
            "cn0_db_hz": -10.0,
            "signal_stratum": "boundary",
            "signal_timing_count": len(signal_rows),
            "pooled_timing_count_including_null": len(pooled_rows),
            "signal_only_median_complete_resident_s": signal_median,
            "pooled_median_complete_resident_s": pooled_median,
            "pooled_minus_signal_percent_of_signal_median": 100.0 * (pooled_median - signal_median) / signal_median,
            "correction_note": "Signal-only point timings exclude the 32 null captures whose input metadata C/N0 also equals -10 dB-Hz.",
        })
    return points, corrections, pooled_groups


def render(points: list[dict], output: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(14.2, 6.0), sharey=True)
    markers = ["o", "s", "^", "D", "v", "P", "X", "*"]
    for ax, (gate, title) in zip(axes, GATES):
        for idx, policy in enumerate(POLICIES):
            rows = sorted((p for p in points if p["gate"] == gate and p["policy"] == policy), key=lambda p: p["cn0_db_hz"])
            if not rows:
                continue
            xs, ys, lows, highs = [], [], [], []
            unbounded_x = []
            for row in rows:
                if row["time_per_observed_correct_admission_s"] is None:
                    continue
                x = row["cn0_db_hz"]
                y = row["time_per_observed_correct_admission_s"]
                lo = row["time_interval_low_s"]
                hi = row["time_interval_high_s"]
                xs.append(x); ys.append(y)
                lows.append(max(0., y - lo) if lo is not None else y)
                highs.append(max(0., hi - y) if hi is not None else 0.)
                if row["upper_interval_unbounded"]:
                    unbounded_x.append(x)
            if not xs:
                continue
            ax.errorbar(xs, ys, yerr=np.array([lows, highs]), color=COLORS[idx], marker=markers[idx],
                        linewidth=1.6, capsize=3, label=DISPLAY[policy])
            if unbounded_x:
                ax.scatter(unbounded_x, [ys[xs.index(x)] for x in unbounded_x], marker="^", s=50,
                           facecolors="none", edgecolors=COLORS[idx], zorder=4)
        ax.set_title(title)
        ax.set_xlabel("C/N₀ (dB-Hz)")
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Median resident seconds per observed correct admission")
    axes[1].legend(frameon=False, fontsize=8, loc="upper right")
    fig.suptitle("Derived screening view; pointwise Hoeffding intervals transformed through success fraction\n"
                 "An open triangle marks an unbounded upper interval; no interpolation or sensitivity qualification", y=1.04)
    fig.text(.5, .01, "Threshold settings evaluated targets of 99% / 10⁻⁴ per day and 90% / 1% per 12 hours; neither is qualified. "
             "Both correlation policies use shared inference (FP32 bulk + FP64 reductions). Timing includes per-input receiver "
             "preparation; excludes generation and provisioning/install. Not latency CI or operational cost.",
             ha="center", fontsize=8)
    fig.tight_layout(rect=(0, .04, 1, .94))
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)


def render_cost(points: list[dict], output: Path, hourly_rate_usd: float) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(14.2, 6.0), sharey=True)
    markers = ["o", "s", "^", "D", "v", "P", "X", "*"]
    for ax, (gate, title) in zip(axes, GATES):
        for idx, policy in enumerate(POLICIES):
            rows = sorted((p for p in points if p["gate"] == gate and p["policy"] == policy), key=lambda p: p["cn0_db_hz"])
            rows = [p for p in rows if p["illustrative_compute_cost_usd_per_observed_correct_admission"] is not None]
            if not rows:
                continue
            xs = [p["cn0_db_hz"] for p in rows]
            ys = [p["illustrative_compute_cost_usd_per_observed_correct_admission"] for p in rows]
            lows = [max(0., y - p["illustrative_compute_cost_interval_low_usd"]) for y, p in zip(ys, rows)]
            highs = [max(0., p["illustrative_compute_cost_interval_high_usd"] - y)
                     if p["illustrative_compute_cost_interval_high_usd"] is not None else 0. for y, p in zip(ys, rows)]
            ax.errorbar(xs, ys, yerr=np.array([lows, highs]), color=COLORS[idx], marker=markers[idx],
                        linewidth=1.6, capsize=3, label=DISPLAY[policy])
            for x, p in zip(xs, rows):
                if p["illustrative_compute_cost_interval_high_usd"] is None:
                    ax.scatter([x], [p["illustrative_compute_cost_usd_per_observed_correct_admission"]], marker="^", s=50,
                               facecolors="none", edgecolors=COLORS[idx], zorder=4)
        ax.set_title(title)
        ax.set_xlabel("C/N₀ (dB-Hz)")
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Illustrative GPU compute USD per observed correct admission")
    axes[1].legend(frameon=False, fontsize=8, loc="upper right")
    fig.suptitle(f"Derived screening estimate at an assumed GPU rate of ${hourly_rate_usd:.2f}/hour\n"
                 "Pointwise reception-rate bounds transformed through resident time; not operationally qualified cost", y=1.04)
    fig.text(.5, .01, "Uses signal-only resident timing for each C/N₀/stratum. Includes per-input receiver preparation; excludes generation, "
             "provisioning/install and engineering. Open triangle marks an unbounded upper interval; no sensitivity qualification.",
             ha="center", fontsize=8)
    fig.tight_layout(rect=(0, .04, 1, .94))
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--summary", required=True, type=Path, help="archived consumer analysis/summary.json")
    ap.add_argument("--output", required=True, type=Path, help="output PNG path")
    ap.add_argument("--points", required=True, type=Path, help="derived point data JSON path")
    ap.add_argument("--cost-output", type=Path, help="optional illustrative cost plot output PNG")
    ap.add_argument("--hourly-rate-usd", type=float, default=0.90, help="assumed GPU hourly rate for cost plot (default: 0.90)")
    args = ap.parse_args()
    summary_path = args.summary.resolve()
    summary = json.loads(summary_path.read_text())
    points, corrections, pooled_groups = derive(summary)
    for point in points:
        latency = point["median_complete_resident_s_per_capture"]
        success = point["point_success_fraction"]
        cost_per_capture = latency * args.hourly_rate_usd / 3600.0
        if success > 0:
            point["illustrative_compute_cost_usd_per_observed_correct_admission"] = cost_per_capture / success
            high_p = float(next(r["ci95_high"] for r in summary["holdout_pointwise"]
                                if r.get("variant") == point["variant"] and r["policy"] == point["policy"]
                                and r["gate"] == point["gate"] and float(r["cn0_db_hz"]) == point["cn0_db_hz"]
                                and r["stratum"] == point["stratum"]))
            low_p = float(next(r["ci95_low"] for r in summary["holdout_pointwise"]
                               if r.get("variant") == point["variant"] and r["policy"] == point["policy"]
                               and r["gate"] == point["gate"] and float(r["cn0_db_hz"]) == point["cn0_db_hz"]
                               and r["stratum"] == point["stratum"]))
            point["illustrative_compute_cost_interval_low_usd"] = cost_per_capture / high_p if high_p > 0 else 0.0
            point["illustrative_compute_cost_interval_high_usd"] = cost_per_capture / low_p if low_p > 0 else None
        point["hourly_rate_usd_assumption"] = args.hourly_rate_usd
    source_hashes = {str(summary_path): sha256(summary_path)}
    renderer_path = Path(__file__).resolve()
    source_hashes[str(renderer_path)] = sha256(renderer_path)
    sibling_analyzer = summary_path.parent.parent / "analyze.py"
    if sibling_analyzer.is_file():
        source_hashes[str(sibling_analyzer.resolve())] = sha256(sibling_analyzer.resolve())
    derived = {
        "artifact_kind": "derived_plot_points_not_raw_measurements",
        "renderer_note": "New standalone rendering from archived summary values; original PNG renderer is unknown and is not claimed to be recreated.",
        "source_input_sha256": source_hashes,
        "hourly_rate_usd_assumption": args.hourly_rate_usd,
        "cost_assumption": "GPU compute charge proxy = median complete resident seconds * assumed hourly rate / 3600 / observed correct-admission fraction; not billed or operationally qualified cost.",
        "audit_corrections": corrections,
        "point_count": len(points),
        "points": points,
    }
    args.points.parent.mkdir(parents=True, exist_ok=True)
    args.points.write_text(json.dumps(derived, indent=2, sort_keys=True) + "\n")
    render(points, args.output)
    if args.cost_output:
        render_cost(points, args.cost_output, args.hourly_rate_usd)
    print(f"wrote {len(points)} derived points and {args.output}" + (f" plus {args.cost_output}" if args.cost_output else ""))


if __name__ == "__main__":
    main()

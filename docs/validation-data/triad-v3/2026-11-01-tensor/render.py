#!/usr/bin/env python3
"""Render reproducible static plots and an exact-metric JSON summary for triad tensor evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
from pathlib import Path
from typing import Any

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

POLICIES = ("baseline", "bf16-split2", "tf32x3")
POLICY_LABELS = {"baseline": "graph-fp64", "bf16-split2": "BF16 split2, BC64 fused", "tf32x3": "TF32x3, BC64 fused"}
CN0S = (-26, -24, -22, -20, -18, -17, -16)
FINAL_GROUPS = ("validation-noise-v2", "validation-noise-high-v2", "validation-latency-v2", "validation-physical-v2", "validation-profile-v2", "physical-holdout", "long-boundary")
BUDGETS_DB = (0.1, 0.25, 0.5, 1.0)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def load_json(path: Path) -> Any:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def get(obj: Any, *keys: str, default=None):
    cur = obj
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def finite_number(value: Any) -> float | None:
    try:
        x = float(value)
    except (TypeError, ValueError):
        return None
    return x if math.isfinite(x) else None


def find_values(obj: Any, keys: set[str], prefix: str = "") -> dict[str, Any]:
    found = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            path = f"{prefix}.{k}" if prefix else k
            if k in keys and not isinstance(v, (dict, list)):
                found[path] = v
            found.update(find_values(v, keys, path))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            found.update(find_values(v, keys, f"{prefix}[{i}]"))
    return found


def source_hashes(report: dict) -> dict:
    return report.get("source_sha256", report.get("source_hashes", {})) or {}


def candidate_policy(report: dict, filename: str) -> str | None:
    text = (filename + " " + str(get(report, "configuration", "backend", default="")) + " " +
            str(get(report, "configuration", "tensor_mode", default=""))).lower()
    if "bf16-split2" in text or "bf16_split2" in text:
        return "bf16-split2"
    if "tf32x3" in text:
        return "tf32x3"
    if "baseline" in text or "graph-fp64" in text or "strict-fp64" in text:
        return "baseline"
    return None


def fusion_inputs(evidence: Path) -> tuple[list[dict], dict[str, str], list[dict]]:
    folder = evidence / "fusion/results/fusion"
    driver_path = folder / "driver.json"
    if not driver_path.exists():
        return [], {}, []
    driver = load_json(driver_path)
    if isinstance(driver, dict):
        runs = driver.get("runs", driver.get("results", []))
        if isinstance(runs, dict):
            runs = [dict(v, name=k) if isinstance(v, dict) else {"name": k} for k, v in runs.items()]
    else:
        runs = driver
    by_name = {r.get("name"): r for r in runs if isinstance(r, dict) and r.get("name")}
    records = []
    baseline_end = []
    for path in sorted(folder.glob("r[0-2]-*.json")):
        name = path.stem
        m = re.match(r"r([0-2])-(.*)", name)
        if not m:
            continue
        rep, label = int(m.group(1)), m.group(2)
        report = load_json(path)
        if label == "baseline-end":
            timing = report.get("timing", {})
            driver_record = by_name.get(name, {})
            parent_wall = finite_number(driver_record.get("parent_wall_seconds"))
            if parent_wall is None:
                parent_wall = finite_number(timing.get("parent_wall_seconds"))
            baseline_end.append({"name": name, "path": str(path), "sha256": sha256(path),
                                 "inference_seconds": finite_number(timing.get("inference_seconds")),
                                 "complete_processing_pipeline_seconds": finite_number(timing.get("complete_processing_pipeline_seconds")),
                                 "parent_wall_seconds": parent_wall,
                                 "paired_observations": get(report, "workload", "paired_observations_scored")})
            continue
        # Only the fixed B12, 8-batch, fused BC64 validation policies are comparable.
        cfg = report.get("configuration", {})
        if cfg.get("batch") != 12 or cfg.get("batches") != 8:
            continue
        if label == "baseline":
            policy = "baseline"
        elif label.startswith("bf16-split2-bc64-f1"):
            policy = "bf16-split2"
        elif label.startswith("tf32x3-bc64-f1"):
            policy = "tf32x3"
        else:
            continue
        if report.get("status") not in ("conditional_screening_only", "completed", "completed_with_case_results"):
            continue
        timing = report.get("timing", {})
        driver_record = by_name.get(name, {})
        parent_wall = finite_number(driver_record.get("parent_wall_seconds"))
        if parent_wall is None:
            parent_wall = finite_number(timing.get("parent_wall_seconds"))
        records.append({"name": name, "repeat": rep, "policy": policy, "path": str(path), "report": report,
                        "sha256": sha256(path), "driver_parent_wall_seconds": parent_wall,
                        "paired_observations": get(report, "workload", "paired_observations_scored")})
    return records, {str(driver_path): sha256(driver_path)}, baseline_end


def plot_fusion(records: list[dict], output: Path) -> dict:
    names = ("inference_seconds", "complete_processing_pipeline_seconds", "parent_wall")
    titles = ("Receiver inference (synchronized wall)", "Complete processing pipeline", "Parent process wall")
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.7), constrained_layout=True)
    palette = {"baseline": "#536d8a", "bf16-split2": "#159b86", "tf32x3": "#bc7b18"}
    metric_values = {}
    for ax, key, title in zip(axes, names, titles):
        observed = []
        for i, policy in enumerate(POLICIES):
            vals, rep0 = [], None
            for row in records:
                if row["policy"] != policy:
                    continue
                t = row["report"].get("timing", {})
                value = row["driver_parent_wall_seconds"] if key == "parent_wall" else finite_number(t.get(key))
                if value is None:
                    continue
                vals.append(value)
                observed.append(value)
                if row["repeat"] == 0:
                    rep0 = value
            metric_values.setdefault(key, {})[policy] = vals
            if not vals:
                continue
            med = statistics.median(vals)
            lo, hi = min(vals), max(vals)
            ax.errorbar(i, med, yerr=[[med-lo], [hi-med]], fmt="o", capsize=6, color=palette[policy],
                        markersize=8, linewidth=2, zorder=3)
            if rep0 is not None:
                ax.scatter([i], [rep0], marker="D", facecolors="none", edgecolors="#222222", s=70,
                           label="cold/capture repeat 0" if i == 0 else None, zorder=4)
            ax.text(i, hi, f"median {med:.2f}s\nrange {lo:.2f}–{hi:.2f}s", ha="center", va="bottom", fontsize=8)
        if observed:
            span = max(observed) - min(observed)
            ax.set_ylim(min(observed) - max(span * .22, max(observed) * .025),
                        max(observed) + max(span * .35, max(observed) * .10))
        ax.set_title(title)
        ax.set_xlim(-.65, 2.65)
        ax.set_xticks(range(3), ["strict FP32\nFP64 sums", "BF16\nsplit2", "TF32x3"])
        ax.set_ylabel("seconds per 192 total captures (96 H0/H1 pairs)")
        ax.grid(axis="y", alpha=.25)
    fig.suptitle("Repeated B12 fusion workload: three repeats; whiskers show observed min–max", fontsize=12)
    fig.text(.5, -.015, "Open diamond marks repeat 0; first compilation/capture remains charged. Three-run median and observed min–max whiskers.", ha="center", fontsize=9)
    path = output / "fusion-b12-timings.png"
    fig.savefig(path, dpi=170, bbox_inches="tight")
    plt.close(fig)
    paired = sorted({r.get("paired_observations") for r in records if r.get("paired_observations") is not None})
    return {"path": str(path), "paired_observations_per_run": paired, "repeat_count_by_policy": {
            p: sum(r["policy"] == p for r in records) for p in POLICIES},
            "metrics_seconds": metric_values,
            "note": "Three repeat median and observed min/max; repeat 0 is separately marked and its first compilation/capture cost remains charged. Parent wall uses external driver measurements only; missing parent wall remains null."}


def noise_identifiers(path: Path, report: dict) -> tuple[float | None, str | None]:
    cn = finite_number(get(report, "configuration", "cn0", default=get(report, "cn0_db_hz")))
    if cn is None:
        m = re.search(r"cn(-?\d+)", path.stem)
        if m:
            cn = float(m.group(1))
    pol = candidate_policy(report, path.stem)
    if pol is None:
        low = path.stem.lower()
        for p in POLICIES:
            if p in low:
                pol = p
    return cn, pol


def parse_pointwise_ci(decision: dict, successes: int, n: int) -> tuple[float, float] | None:
    # Campaign reports store one-sided 95% exact Clopper–Pearson lower/upper bounds.
    ci = decision.get("correct_one_sided_95")
    if isinstance(ci, list) and len(ci) == 2 and all(finite_number(v) is not None for v in ci):
        return float(ci[0]), float(ci[1])
    return None


def noise_records(evidence: Path, group="validation-noise-v2") -> tuple[list[dict], list[str]]:
    root = evidence / "final/results" / group
    if not root.exists():
        return [], [str(root)]
    files = sorted(p for p in root.rglob("*.json") if "driver" not in p.name.lower())
    driver_path = root / "driver.json"
    try:
        driver = load_json(driver_path) if driver_path.exists() else []
    except (OSError, json.JSONDecodeError):
        driver = []
    records = []
    for p in files:
        try:
            report = load_json(p)
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(report, dict):
            continue
        cn, policy = noise_identifiers(p, report)
        decisions = report.get("decisions", {})
        if cn is None or policy is None or not isinstance(decisions, dict):
            continue
        row = {"path": str(p), "sha256": sha256(p), "cn0_db_hz": cn, "policy": policy, "report": report,
               "driver_parent_wall_seconds": parent_wall_from(report, driver, p.stem)}
        for target in ("default", "relaxed"):
            d = decisions.get(target, {})
            n = int(d.get("conditional_noise_trials_per_kind", get(report, "workload", "conditional_independent_noise_trials_per_kind", default=0)) or 0)
            k = int(d.get("correct_bit_accepted", 0) or 0)
            row[target] = {"successes": k, "trials": n, "rate": k/n if n else None,
                           "one_sided_95_cp": parse_pointwise_ci(d, k, n),
                           "target": d.get("correct_target")}
        records.append(row)
    missing = []
    expected = {(cn, pol) for cn in (CN0S[:5] if group == "validation-noise-v2" else CN0S[5:]) for pol in POLICIES}
    actual = {(r["cn0_db_hz"], r["policy"]) for r in records}
    missing.extend(f"{cn:g} dB-Hz / {pol}" for cn, pol in sorted(expected-actual))
    return records, missing


def plot_noise(records: list[dict], output: Path) -> dict:
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.8), sharex=True, sharey=True, constrained_layout=True)
    colors = {"baseline": "#536d8a", "bf16-split2": "#159b86", "tf32x3": "#bc7b18"}
    markers = {"baseline": "o", "bf16-split2": "s", "tf32x3": "^"}
    linestyles = {"baseline": "-", "bf16-split2": "--", "tf32x3": ":"}
    panels = (("default", "Default target (99% correct)"), ("relaxed", "Relaxed target (90% correct)"))
    plotted = {}
    for ax, (kind, title) in zip(axes, panels):
        plotted[kind] = {}
        for policy in POLICIES:
            rows = sorted((r for r in records if r["policy"] == policy), key=lambda x: x["cn0_db_hz"])
            xs, ys, low, high = [], [], [], []
            for row in rows:
                datum = row[kind]
                ci = datum["one_sided_95_cp"]
                if datum["rate"] is None or ci is None:
                    continue
                xs.append(row["cn0_db_hz"]); ys.append(datum["rate"])
                low.append(datum["rate"] - ci[0]); high.append(ci[1] - datum["rate"])
            if xs:
                ax.errorbar(xs, ys, yerr=[low, high], marker=markers[policy], linestyle=linestyles[policy],
                            capsize=3, linewidth=1.5, alpha=.78, color=colors[policy], label=POLICY_LABELS[policy])
            plotted[kind][policy] = [{"cn0_db_hz": x, "rate": y, "lower": y-l,
                                      "upper": y+h} for x, y, l, h in zip(xs, ys, low, high)]
        ax.axhline(.99 if kind == "default" else .90, color="#555", linestyle="--", linewidth=1)
        ax.set_title(title)
        ax.set_xlabel("C/N0 (dB-Hz)")
        ax.set_xticks(CN0S)
        ax.set_ylim(-.02, 1.04)
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Conditional correct reference-gate fraction")
    if not records:
        for ax in axes:
            ax.text(.5, .5, "Final noise validation data not yet available", transform=ax.transAxes, ha="center", va="center")
    else:
        axes[1].legend(fontsize=8, loc="lower right")
    fig.suptitle("Fresh noise, fixed sampled physical path; pointwise one-sided 95% Clopper–Pearson bounds", fontsize=11)
    fig.get_layout_engine().set(rect=(0, .11, 1, .89))
    fig.text(.5, .045, "Series share exact C/N0 coordinates; marker shape and line style distinguish policies.", ha="center", fontsize=8)
    fig.text(.5, .015, "384 trials per point, one physical path. Noise is paired within each root:180331(-26..-18),180371(-17,-16); do not pool correlated points.", ha="center", fontsize=8)
    path = output / "fresh-noise-correct-fraction.png"
    fig.savefig(path, dpi=170, bbox_inches="tight")
    plt.close(fig)
    return {"path": str(path), "series": plotted,
            "scope": "Conditional on one shared physical path. Per-C/N0 trials are independent within point; noise root180331 pairs -26 through -18 and fresh root180371 pairs -17/-16. Do not pool correlated sweep points."}


def parent_wall_from(report: dict, driver: Any, stem: str) -> float | None:
    for key in ("parent_wall_seconds", "parent_runtime_seconds"):
        v = finite_number(report.get(key))
        if v is not None:
            return v
    v = finite_number(get(report, "timing", "parent_wall_seconds"))
    if v is not None:
        return v
    entries = driver.get("runs", driver.get("results", driver)) if isinstance(driver, dict) else driver
    if isinstance(entries, dict):
        entries = [dict(v, name=k) if isinstance(v, dict) else {"name": k} for k, v in entries.items()]
    if isinstance(entries, list):
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            name = str(entry.get("name", ""))
            command = " ".join(entry.get("command", [])) if isinstance(entry.get("command"), list) else str(entry.get("output", ""))
            if stem in name or stem in command:
                return finite_number(entry.get("parent_wall_seconds"))
    return None


def plot_cost(records: list[dict], output: Path, usd_hour: float) -> dict:
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8), sharex=True, constrained_layout=True)
    colors = {"baseline": "#536d8a", "bf16-split2": "#159b86", "tf32x3": "#bc7b18"}
    panels = (("default", "Default target (99%)"), ("relaxed", "Relaxed target (90%)"))
    result = {"default": [], "relaxed": []}
    for ax, (target, title) in zip(axes, panels):
        for pol in POLICIES:
            rows = sorted((r for r in records if r["policy"] == pol), key=lambda x: x["cn0_db_hz"])
            xs, ys = [], []
            for row in rows:
                wall = row.get("driver_parent_wall_seconds")
                k = row[target]["successes"]
                total = row[target]["trials"]
                point = {"path": row["path"], "cn0_db_hz": row["cn0_db_hz"], "policy": pol,
                         "parent_wall_seconds": wall, "correct_gate_crossings": k, "conditional_h1_trials": total}
                if wall is None or total <= 0 or k <= 0:
                    point["cost_usd"] = None
                    result[target].append(point)
                    continue
                cost = ((wall * usd_hour / 3600.0) / k)
                xs.append(row["cn0_db_hz"]); ys.append(cost)
                point.update({"cost_usd": cost, "cost_per_conditional_trial_usd": (wall * usd_hour / 3600.0) / total})
                result[target].append(point)
            if xs:
                ax.plot(xs, ys, marker="o", color=colors[pol], label=POLICY_LABELS[pol])
        ax.set_title(title)
        ax.set_yscale("log")
        ax.set_xticks(CN0S)
        ax.set_xlabel("C/N0 (dB-Hz)")
        ax.set_ylabel("USD per correct H1 gate (whole H0+H1 run)")
        ax.grid(True, which="both", alpha=.25)
    if any(ax.lines for ax in axes):
        axes[1].legend(fontsize=8)
    else:
        for ax in axes:
            ax.text(.5, .5, "Final noise validation data not yet available", transform=ax.transAxes, ha="center", va="center")
    fig.suptitle("Whole parent H0+H1 run cost per correct H1 reference-gate crossing", fontsize=11)
    fig.text(.5, -.02, f"Price: ${usd_hour:g}/GPU-hour. Numerator charges whole paired H0+H1 parent run (including null work); excludes export, provisioning, calibration. NOT operationally qualified.", ha="center", fontsize=8)
    path = output / "fresh-noise-cost-per-correct-gate.png"
    fig.savefig(path, dpi=170, bbox_inches="tight")
    plt.close(fig)
    return {"path": str(path), "usd_per_gpu_hour": usd_hour, "points": result,
            "interpretation": "Whole parent H0+H1 run cost per correct H1 reference-gate crossing under each target; null work is charged, export/provisioning/calibration are excluded; NOT operationally validated."}


def plot_throughput(records: list[dict], output: Path) -> dict:
    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharex=True, constrained_layout=True)
    colors = {"baseline": "#536d8a", "bf16-split2": "#159b86", "tf32x3": "#bc7b18"}
    targets = {"default": "default 99% target", "relaxed": "relaxed 90% target"}
    marker = {"baseline": "o", "bf16-split2": "s", "tf32x3": "^"}
    series = {"paired_observations_per_second": [], "correct_h1_gate_crossings_per_second": []}
    for pol in POLICIES:
        rows = sorted((r for r in records if r["policy"] == pol), key=lambda x: x["cn0_db_hz"])
        used = []
        for row in rows:
            wall = row.get("driver_parent_wall_seconds")
            count = finite_number(get(row["report"], "workload", "paired_observations_scored"))
            if wall is None or wall <= 0 or count is None:
                continue
            used.append({"row": row, "cn0": row["cn0_db_hz"], "paired": count / wall,
                         "default": row["default"]["successes"] / wall,
                         "relaxed": row["relaxed"]["successes"] / wall})
        if used:
            xs = [p["cn0"] for p in used]
            axes[0].plot(xs, [p["paired"] for p in used], marker=marker[pol], color=colors[pol], label=POLICY_LABELS[pol])
            series["paired_observations_per_second"].append({"policy": pol, "points": [
                {"cn0_db_hz": p["cn0"], "paired_observations": finite_number(get(p["row"]["report"], "workload", "paired_observations_scored")),
                 "parent_wall_seconds": p["row"]["driver_parent_wall_seconds"], "observations_per_second": p["paired"]}
                for p in used]})
            for target in targets:
                axes[1].plot(xs, [p[target] for p in used], marker=marker[pol], color=colors[pol],
                             linestyle="-" if target == "default" else "--",
                             label=f"{POLICY_LABELS[pol]} · {targets[target]}")
                series["correct_h1_gate_crossings_per_second"].append({"policy": pol, "target": target, "points": [
                    {"cn0_db_hz": p["cn0"], "correct_h1_gate_crossings": p["row"][target]["successes"],
                     "conditional_h1_trials": p["row"][target]["trials"], "parent_wall_seconds": p["row"]["driver_parent_wall_seconds"],
                     "crossings_per_second": p[target]}
                    for p in used]})
    axes[0].set_title("Total H0+H1 captures / parent wall")
    axes[0].set_ylabel("H0+H1 total captures / second")
    axes[0].legend(fontsize=7)
    axes[1].set_title("Correct H1 reference-gate crossings / parent wall")
    axes[1].set_ylabel("Correct H1 crossings / second")
    axes[1].legend(fontsize=6, ncol=2, loc="upper left")
    for ax in axes:
        ax.set_xticks(CN0S)
        ax.set_xlabel("C/N0 (dB-Hz)")
        ax.grid(alpha=.25)
    fig.suptitle("Conditional throughput from external parent-process wall time", fontsize=11)
    fig.text(.5, -.025, "Left numerator is 768 total captures = 384 H0/H1 pairs; right numerator is correct H1 gates only (≤384 per target). Parent wall charges H0+H1, excludes export/provisioning/calibration. NOT operationally qualified. Roots: 180331 (-26..-18), fresh 180371 (-17/-16).", ha="center", fontsize=8)
    path = output / "fresh-noise-throughput.png"
    fig.savefig(path, dpi=170, bbox_inches="tight")
    plt.close(fig)
    return {"path": str(path), "series": series,
            "denominator": "External parent-process wall seconds from the stage driver; charges both H0 and H1 scoring, excludes input export, provisioning and calibration.",
            "numerators": {"paired_observations_per_second": "768 total captures per report = 384 H0/H1 pairs.",
                           "correct_h1_gate_crossings_per_second": "Correct H1 reference-gate crossings only, at most 384 per report and target."},
            "qualification": "Diagnostic throughput; performance and policy gains are NOT operationally qualified."}


def micro_summary(micro: dict) -> tuple[dict, list[dict]]:
    by_mode: dict[str, list[dict]] = {}
    for c in micro.get("cases", []):
        if c.get("status") != "executed":
            continue
        mode = c.get("mode", "unknown")
        timing = c.get("timing", {})
        error = c.get("against_fp64_contraction_same_fp32_operands", {})
        diag = c.get("operand_diagnostics", {})
        raw_representation_l1 = finite_number(diag.get("state_representation_l1_error"))
        original_mass = finite_number(diag.get("state_original_mass"))
        kernel_metadata = c.get("kernel_artifacts", {}).get("metadata", {})
        row = {"mode": mode, "pattern": c.get("pattern"), "batch": c.get("batch"),
               "median_seconds": timing.get("median_seconds"),
               "relative_l1_error": error.get("relative_l1_error"),
               "representation_l1_error_raw_sum": raw_representation_l1,
               "representation_l1_error": raw_representation_l1 / original_mass if raw_representation_l1 is not None and original_mass else None,
               "state_original_mass": original_mass,
               "zeroed_mass_fraction": diag.get("state_zeroed_mass_fraction"),
               "kernel_artifacts_available": c.get("kernel_artifacts", {}).get("available", []),
               "kernel_metadata": kernel_metadata}
        by_mode.setdefault(mode, []).append(row)
    rows = []
    for mode, items in sorted(by_mode.items()):
        timings = [finite_number(r["median_seconds"]) for r in items]
        timings = [x for x in timings if x is not None]
        def mx(key):
            vals = [finite_number(r[key]) for r in items]
            vals = [v for v in vals if v is not None]
            return max(vals) if vals else None
        rows.append({"mode": mode, "executed_cases": len(items),
                     "median_kernel_seconds_median_across_cases": statistics.median(timings) if timings else None,
                     "max_fp64_relative_l1_error": mx("relative_l1_error"),
                     "max_state_representation_l1_error_normalized": mx("representation_l1_error"),
                     "max_state_representation_l1_error_raw_sum": mx("representation_l1_error_raw_sum"),
                     "max_state_zeroed_mass_fraction": mx("zeroed_mass_fraction"),
                     "instruction_artifact_types": sorted({a for r in items for a in r["kernel_artifacts_available"]}),
                     "kernel_metadata_kernel_n_spills_max": mx_nested(items, "kernel_n_spills")})
    return {"schema": micro.get("schema"), "status": micro.get("status"), "case_count": len(micro.get("cases", [])),
            "case_counts": micro.get("case_counts", {}), "device": micro.get("device"), "torch_version": micro.get("torch_version"),
            "cuda_runtime_version": micro.get("cuda_runtime_version"), "methods": micro.get("methods"),
            "source_hashes": micro.get("source_hashes", {}), "modes": rows,
            "metric_notes": {"relative_l1_error": "sum absolute output error divided by original/reference output mass as reported by each case.",
                             "representation_l1_error": "raw state representation L1 error sum divided by state_original_mass.",
                             "representation_l1_error_raw_sum": "state_representation_l1_error copied without normalization.",
                             "zeroed_mass_fraction": "report field is used verbatim.",
                             "kernel_n_spills": "kernel compiler metadata value; not PTX instruction verification."}}, [r for vals in by_mode.values() for r in vals]


def mx_nested(rows: list[dict], key: str) -> float | None:
    vals = []
    for r in rows:
        vals.extend(numeric_leaves(r.get("kernel_metadata", {}).get(key)))
    return max(vals) if vals else None


def numeric_leaves(x: Any) -> list[float]:
    if isinstance(x, dict):
        return sum((numeric_leaves(v) for v in x.values()), [])
    if isinstance(x, list):
        return sum((numeric_leaves(v) for v in x), [])
    v = finite_number(x)
    return [] if v is None else [v]


def plot_micro(rows: list[dict], output: Path) -> dict:
    fig, ax = plt.subplots(figsize=(8.5, 5), constrained_layout=True)
    modes = sorted({r["mode"] for r in rows})
    cmap = plt.get_cmap("tab10")
    plotted = {}
    for i, mode in enumerate(modes):
        items = [r for r in rows if r["mode"] == mode]
        xs = [finite_number(r["median_seconds"]) for r in items]
        ys = [finite_number(r["relative_l1_error"]) for r in items]
        pts = [(x, y) for x, y in zip(xs, ys) if x is not None and y is not None]
        if pts:
            ax.scatter([x for x, _ in pts], [y for _, y in pts], s=22, alpha=.7, color=cmap(i), label=mode)
        plotted[mode] = [{"median_seconds": x, "relative_l1_error": y} for x, y in pts]
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Median CUDA-event interval per contraction (seconds; log scale)")
    ax.set_ylabel("Output relative L1 error vs FP64 contraction (log scale)")
    ax.set_title("Micro diagnostics: error vs event time; host launch gaps may be included")
    ax.grid(True, which="both", alpha=.25)
    if ax.collections:
        ax.legend(fontsize=7, ncol=2)
    path = output / "micro-error-vs-time.png"
    fig.savefig(path, dpi=170, bbox_inches="tight")
    plt.close(fig)
    return {"path": str(path), "points": plotted,
            "note": "Timing covers diagnostic CUDA kernel calls; it does not establish full-workload throughput."}


def final_reports(evidence: Path) -> tuple[dict, list[dict]]:
    base = evidence / "final/results"
    groups = {}
    all_files = []
    for group in FINAL_GROUPS:
        folder = base / group
        if not folder.exists():
            groups[group] = {"status": "missing", "expected_directory": str(folder), "reports": []}
            continue
        reports = []
        artifacts = []
        for artifact in sorted(p for p in folder.rglob("*") if p.is_file()):
            artifact_record = {"path": str(artifact), "sha256": sha256(artifact), "bytes": artifact.stat().st_size}
            artifacts.append(artifact_record)
            all_files.append({**artifact_record, "kind": "artifact"})
        for p in sorted(folder.rglob("*.json")):
            try:
                data = load_json(p)
            except (OSError, json.JSONDecodeError):
                continue
            if "driver" in p.name.lower():
                reports.append({"path": str(p), "sha256": sha256(p), "kind": "driver", "exact_metrics": find_values(data, {"returncode", "parent_wall_seconds", "status", "run_count"})})
                continue
            metrics = find_values(data, {"cn0", "cn0_db_hz", "correct_bit_accepted", "wrong_bit_accepted", "missed", "null_admitted",
                                         "conditional_noise_trials_per_kind", "paired_observations_scored", "scored_blocks", "full_blocks",
                                         "physical_paths", "physical_capture_seconds", "inference_seconds", "complete_processing_pipeline_seconds",
                                         "complete_campaign_wall_before_json_seconds", "parent_wall_seconds", "threshold_nats", "maximum_absolute_log_lr_error",
                                         "mean_signed_log_lr_error", "ranking_differences", "candidate_ranking_differences", "bit_ranking_differences",
                                         "sensitivity_loss_db_vs_strict_fp64_reference", "estimated_crossing_db_hz", "status", "tensor_mode", "frequency_tensor_instruction_verified"})
            record = {"path": str(p), "sha256": sha256(p), "schema": data.get("schema") if isinstance(data, dict) else None,
                      "status": data.get("status") if isinstance(data, dict) else None, "source_sha256": source_hashes(data) if isinstance(data, dict) else {},
                      "input_hashes": get(data, "input_identity", "files", default=get(data, "input_hashes", default={})),
                      "exact_metrics": metrics}
            reports.append(record)
            all_files.append(record)
        groups[group] = {"status": "available" if reports else "empty", "reports": reports,
                         "artifact_inventory": artifacts}
    return groups, all_files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence", type=Path, required=True, help="Evidence root, e.g. local-evidence/triad-tensor-20261101")
    ap.add_argument("--output", type=Path, required=True, help="Generated output directory (separate from evidence)")
    ap.add_argument("--usd-hour", type=float, default=1.197, help="GPU hourly price used in diagnostic cost estimate")
    args = ap.parse_args()
    evidence = args.evidence.resolve()
    output = args.output.resolve()
    if evidence == output:
        raise SystemExit("--output must be a dedicated generated-artifact directory, not the evidence root")
    output.mkdir(parents=True, exist_ok=True)
    fusion, fusion_drivers, baseline_end = fusion_inputs(evidence)
    fusion_result = plot_fusion(fusion, output)
    fusion_result["baseline_end_bracketing_evidence"] = baseline_end
    fusion_result["baseline_end_usage"] = "Separately reported bracketing controls; excluded from the three-repeat baseline median/range."
    noise, missing_noise = noise_records(evidence)
    high_noise, missing_high = noise_records(evidence, "validation-noise-high-v2")
    noise.extend(high_noise)
    missing_noise.extend(missing_high)
    noise_result = plot_noise(noise, output)
    cost_result = plot_cost(noise, output, args.usd_hour)
    throughput_result = plot_throughput(noise, output)
    micro_path = evidence / "micro.json"
    if micro_path.exists():
        micro = load_json(micro_path)
        micro_summary_data, micro_rows = micro_summary(micro)
        micro_result = plot_micro(micro_rows, output)
        micro_summary_data["report_path"] = str(micro_path)
        micro_summary_data["report_sha256"] = sha256(micro_path)
    else:
        micro_summary_data = {"status": "missing", "expected_path": str(micro_path)}
        micro_result = {"status": "missing"}
    final, final_files = final_reports(evidence)
    own = Path(__file__).resolve()
    summary = {
        "schema": "triad-tensor-render-summary-v1",
        "evidence_root": str(evidence),
        "output_directory": str(output),
        "renderer": {"path": str(own), "sha256": sha256(own)},
        "pricing": {"usd_per_gpu_hour": args.usd_hour},
        "fusion": fusion_result,
        "noise_validation": {"status": "available" if noise else "missing", "reports": noise,
                             "missing_expected_points": missing_noise,
                             "independence_note": "384 independent noise trials per kind per C/N0 in final protocol; same noise across C/N0 is paired/correlated; all points share the sampled physical path."},
        "diagnostic_cost": cost_result,
        "diagnostic_throughput": throughput_result,
        "micro": micro_summary_data,
        "final_groups": final,
        "limits": [
            "Sensitivity-loss budgets 0.1, 0.25, 0.5, and 1 dB remain unqualified unless independently proven; this renderer does not infer or certify a loss.",
            "GPU micro timings and conditional fixed-path decisions do not establish operational false-admission calibration or field sensitivity.",
            "Compiler n_spills metadata is not a PTX spill count; artifact availability is reported separately from instruction verification.",
            "Cost is per diagnostic correct gate crossing and is not an operationally validated result.",
        ],
        "source_hash_inputs": {"fusion_driver_hashes": fusion_drivers,
                               "fusion_report_hashes": {r["path"]: r["sha256"] for r in fusion},
                               "noise_report_hashes": {r["path"]: r["sha256"] for r in noise},
                               "final_artifact_hashes": {r["path"]: r["sha256"] for r in final_files},
                               "micro_report_sha256": micro_summary_data.get("report_sha256")},
        "plots": [fusion_result.get("path"), noise_result.get("path"), cost_result.get("path"), throughput_result.get("path"), micro_result.get("path")],
    }
    summary_path = output / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"summary": str(summary_path), "plots": summary["plots"], "noise_reports": len(noise),
                      "missing_final_groups": [k for k, v in final.items() if v["status"] == "missing"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

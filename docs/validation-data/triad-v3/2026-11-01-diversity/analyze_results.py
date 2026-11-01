#!/usr/bin/env python3
"""Aggregate triad diversity result JSON/NPZ shards without counting indexes as trials.

Usage: python analyze_results.py --root remote-preliminary --output summary.json
The same command accepts the later remote-final tree. This is evidence collation,
not qualification. NPZ arrays are used to independently reclassify actual captures.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import numpy as np


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def cp_one_sided(k: int, n: int, alpha: float = .05) -> dict[str, float]:
    """Exact one-sided Clopper-Pearson limits (SciPy when available)."""
    if n < 1 or not 0 <= k <= n:
        raise ValueError(f"invalid binomial count {k}/{n}")
    try:
        from scipy.stats import beta
        lo = 0.0 if k == 0 else float(beta.ppf(alpha, k, n-k+1))
        hi = 1.0 if k == n else float(beta.ppf(1-alpha, k+1, n-k))
        method = "scipy.stats.beta.ppf; exact one-sided Clopper-Pearson"
    except ImportError:
        # Invert the exact binomial tail. This is algebraically identical to
        # the beta quantile and keeps the offline analyzer usable without SciPy.
        def cdf(x: float, successes: int) -> float:
            if x <= 0: return 1.0 if successes == 0 else 0.0
            if x >= 1: return 1.0
            # Log-sum-exp avoids underflow near p≈1 for large k.
            logs = [math.lgamma(n+1)-math.lgamma(j+1)-math.lgamma(n-j+1)
                    + j*math.log(x) + (n-j)*math.log1p(-x)
                    for j in range(successes+1)]
            peak = max(logs)
            log_total = peak + math.log(sum(math.exp(v-peak) for v in logs))
            return math.exp(log_total) if log_total > -745 else 0.0
        def bisect(fn, target: float) -> float:
            a, b = 0.0, 1.0
            for _ in range(90):
                m = (a+b)/2
                if fn(m) > target: a = m
                else: b = m
            return (a+b)/2
        lo = 0.0 if k == 0 else bisect(lambda p: cdf(p, k-1), 1-alpha)
        # upper solves P_p[X <= k] = alpha
        hi = 1.0 if k == n else bisect(lambda p: cdf(p, k), alpha)
        method = "exact binomial-tail inversion fallback (SciPy unavailable); Clopper-Pearson equivalent"
    return {"lower_one_sided_95": lo, "upper_one_sided_95": hi,
            "method": method, "successes": k, "trials": n}


def load_npz(report: Path, d: dict[str, Any]) -> tuple[dict[str, np.ndarray], Path | None]:
    name = d.get("scores_file")
    p = (report.parent / name) if name else report.with_suffix(".scores.npz")
    if not p.exists():
        return {}, None
    expected = d.get("scores_sha256")
    if expected and sha256(p) != expected:
        raise ValueError(f"score SHA-256 mismatch: {p}")
    with np.load(p, allow_pickle=False) as z:
        return {k: z[k] for k in z.files}, p


def counts_from_scores(a: np.ndarray, truth: int, gate: float) -> dict[str, int]:
    x = np.asarray(a, dtype=np.float64)
    if x.ndim != 2 or x.shape[1] != 2 or np.isnan(x).any():
        raise ValueError(f"expected finite/non-NaN trials x 2 scores, got {x.shape}")
    winners = np.argmax(x, axis=1)
    admitted = np.max(x, axis=1) >= gate
    correct = int(np.count_nonzero(admitted & (winners == truth)))
    wrong = int(np.count_nonzero(admitted & (winners != truth)))
    return {"captures": int(x.shape[0]), "correct_accepted": correct,
            "wrong_accepted": wrong, "missed": int(x.shape[0])-correct-wrong}


def analyze_report(p: Path) -> dict[str, Any]:
    d = json.loads(p.read_text())
    if d.get("schema") not in ("triad-noise-study-v1", "triad-noise-batch-study-v1",
                               "triad-static-frequency-study-v1"):
        raise ValueError("unsupported report schema")
    scores, score_path = load_npz(p, d)
    cfg = d.get("configuration", {})
    is_actual = cfg.get("mode") == "actual" or d.get("schema") == "triad-static-frequency-study-v1"
    trials_per_path = int(cfg.get("trials", 1) or 1)
    captures = d.get("captures", [])
    methods = list(cfg.get("methods") or [])
    runtime_by_method: dict[str, list[float]] = {m: [] for m in methods}
    preparation_runtime: list[float] = []
    row_stats: dict[str, dict[str, Any]] = {}
    group_ids: dict[str, set[tuple[str, str]]] = {m: set() for m in methods}
    null_ids: dict[str, set[tuple[str, str]]] = {m: set() for m in methods}
    duplicate_ids: dict[str, list[str]] = {m: [] for m in methods}
    strict_gate = None
    score_mismatches = []
    actual_ids = 0
    for cap in captures:
        if cap.get("status") != "completed":
            continue
        if isinstance(cap.get("preparation_seconds"), (int, float)):
            preparation_runtime.append(float(cap["preparation_seconds"]))
        control = cap.get("control")
        truth = cap.get("truth_bit_simulation_only")
        mh = (cap.get("input_hashes") or {}).get("manifest.json") or cap.get("manifest_sha256")
        if control == "signal" and truth in (0, 1) and mh:
            actual_ids += 1
        for method, m in (cap.get("methods") or {}).items():
            if m.get("status") not in (None, "scored") or not isinstance(m.get("counts"), dict):
                continue
            method_time = m.get("update_wall_seconds", m.get("transfer_fft_and_scoring_seconds"))
            if isinstance(method_time, (int, float)):
                runtime_by_method.setdefault(method, []).append(float(method_time))
            if isinstance(m.get("cpu_fp64_whitening_seconds"), (int, float)):
                runtime_by_method.setdefault(method + "_cpu_fp64_whitening", []).append(float(m["cpu_fp64_whitening_seconds"]))
            c = m.get("counts") or {}
            ent = row_stats.setdefault(method, {"capture_records": 0, "correct": 0, "wrong": 0,
                                                "missed": 0, "raw_correct": 0,
                                                "false_admitted_noise": 0,
                                                "null_capture_records": 0,
                                                "calibration_statuses": set(),
                                                "score_npz_capture_rows": 0,
                                                "gate_classifications": {}})
            ent["calibration_statuses"].add(m.get("calibration_status", "not_provided"))
            if control == "signal":
                for key, dest in (("correct_accepted", "correct"), ("wrong_accepted", "wrong"),
                                  ("missed", "missed"), ("raw_correct", "raw_correct")):
                    ent[dest] += int(c.get(key, 0))
                ent["capture_records"] += int(c.get("captures", 0))
                if mh:
                    ident = (str(mh), str(cap.get("input_hashes", {}).get("observation.c128", "")))
                    if ident in group_ids.setdefault(method, set()): duplicate_ids.setdefault(method, []).append(str(ident))
                    group_ids[method].add(ident)
                # Score arrays are actual saved strict scores; compare to JSON.
                i = int(cap.get("physical_path_index", -1))
                key = f"path{i:05d}_signal_{method}"
                if key not in scores:
                    key = f"path{i:05d}_{control}_{method}"
                score_values = scores.get(key)
                if score_values is None and isinstance(m.get("bit_log_scores"), list):
                    score_values = np.asarray([m["bit_log_scores"]], dtype=np.float64)
                th = m.get("threshold")
                if isinstance(th, dict): th = th.get("threshold_log_likelihood_ratio")
                th = float(th) if th is not None else None
                # The shared false-admission allocation is method-independent.
                # Correct a native K-method score-family threshold to K9 by
                # adding ln(9/K); do not infer K when the report omits it.
                native_k = int(cfg.get("selection_methods", 0) or 0)
                k9_strict = (float(th) + math.log(9/native_k) if native_k > 0 else None) if th is not None else None
                # The requested K is the selectable method-family size (nine
                # methods), not the bank's retained rank/dimension.
                relaxed_k_methods = 9
                relaxed = math.log(720 * 3 * relaxed_k_methods * 2 / .005)
                native_label = f"native_strict_K{native_k}" if native_k > 0 else "native_strict_K_unknown"
                gates = {native_label: float(th) if th is not None else None}
                if k9_strict is not None:
                    k9_label = "K9_native_strict" if native_k == 9 else f"K9_strict_derived_from_K{native_k}"
                    gates[k9_label] = k9_strict
                gates["relaxed_counterfactual_log_720x3xKx2_over_0.005"] = relaxed
                ucb_budget = (m.get("threshold", {}).get("ucb_budget", {})
                              if isinstance(m.get("threshold"), dict) else {})
                calibration_alternatives = ucb_budget.get("calibration_alternatives", cfg.get("calibration_alternatives"))
                calibration_alternatives_inferred = False
                if calibration_alternatives is None and method.endswith("-ucb") and native_k == 5 and ucb_budget.get("delta_per_segment") is not None:
                    # Legacy K5 screen reports omit this field; their stored
                    # delta uses the one-alternative budget (the K9 combined
                    # family requires two alternatives for the UCB split).
                    calibration_alternatives = 1
                    calibration_alternatives_inferred = True
                if method.endswith("-ucb"):
                    if calibration_alternatives is None:
                        native_cal_qualification = "calibration-alternative count not reported"
                    elif int(calibration_alternatives) >= 2:
                        native_cal_qualification = f"reported {int(calibration_alternatives)} allocated calibration alternatives"
                    else:
                        detail = "inferred from legacy stored delta_per_segment" if calibration_alternatives_inferred else "reported"
                        native_cal_qualification = f"{detail}: only {int(calibration_alternatives)} calibration alternative(s); combined K9 allocation requires at least 2 here"
                    if calibration_alternatives is not None and int(calibration_alternatives) < 2:
                        native_gate_qualification = (f"native K{native_k} threshold retains its original allocation ({native_cal_qualification}); "
                                                    "this allocation does not qualify the combined K9 family")
                    elif calibration_alternatives is not None:
                        native_gate_qualification = f"native K{native_k} gate has {int(calibration_alternatives)} allocated calibration alternatives"
                    else:
                        native_gate_qualification = "native gate calibration allocation not reported"
                else:
                    native_cal_qualification = "receiver does not use the UCB calibration-failure allocation; retain its likelihood/noise-model assumptions"
                    native_gate_qualification = native_cal_qualification
                if key in scores and truth in (0, 1):
                    if th is not None:
                        calc = counts_from_scores(score_values, int(truth), float(th))
                        ent["score_npz_capture_rows"] += calc["captures"]
                        declared = {k: int(c.get(k, 0)) for k in ("captures", "correct_accepted", "wrong_accepted", "missed")}
                        if calc != declared:
                            score_mismatches.append({"capture_index": i, "method": method,
                                                     "derived": calc, "reported": declared})
                        if strict_gate is None: strict_gate = float(th)
                if score_values is not None and truth in (0, 1):
                    for gate_name, gate in gates.items():
                        if gate is None: continue
                        calc = counts_from_scores(score_values, int(truth), gate)
                        dest = ent["gate_classifications"].setdefault(gate_name,
                            {"threshold_min": gate, "threshold_max": gate, "captures": 0,
                             "correct_accepted": 0, "wrong_accepted": 0, "missed": 0})
                        dest["threshold_min"] = min(dest["threshold_min"], gate)
                        dest["threshold_max"] = max(dest["threshold_max"], gate)
                        for k in ("captures", "correct_accepted", "wrong_accepted", "missed"):
                            dest[k] += calc[k]
                        if gate_name.startswith("K9_strict_derived_from_K") and method.endswith("-ucb"):
                            dest["calibration_qualification"] = ("derived score counts only; no combined K9 FAP claim: "
                                + native_cal_qualification)
                        elif gate_name.startswith("K9_strict_derived_from_K"):
                            dest["calibration_qualification"] = native_cal_qualification
                        elif gate_name.startswith("relaxed_counterfactual"):
                            dest["calibration_qualification"] = "same-score counterfactual only; not a recalibrated FAP guarantee"
                        elif gate_name.startswith("native_strict") or gate_name == "K9_native_strict":
                            dest["calibration_qualification"] = native_gate_qualification
            elif control in ("noise", "null"):
                ent["false_admitted_noise"] += int(c.get("false_admitted", c.get("wrong_accepted", 0)))
                ent["null_capture_records"] += int(c.get("captures", 0))
                if mh:
                    null_ids.setdefault(method, set()).add((str(mh), str(cap.get("input_hashes", {}).get("observation.c128", ""))))
    for m, e in row_stats.items():
        e["calibration_statuses"] = sorted(str(x) for x in e["calibration_statuses"])
        ids = len(group_ids.get(m, set()))
        e["unique_signal_path_identities"] = ids
        e["signal_path_identities"] = [list(x) for x in sorted(group_ids.get(m, set()))]
        e["duplicate_path_identities_within_report"] = duplicate_ids.get(m, [])
        e["unique_null_path_identities"] = len(null_ids.get(m, set()))
        if (len(null_ids.get(m, set())) > 0 and is_actual and trials_per_path == 1
                and len(null_ids[m]) == e["null_capture_records"]):
            e["false_admission_cp95"] = cp_one_sided(e["false_admitted_noise"], len(null_ids[m]))
            e["false_admission_inference_unit"] = "independent actual null physical paths; one capture per path"
        else:
            e["false_admission_cp95"] = None
            e["false_admission_inference_unit"] = "not independent actual null paths"
        # CP is meaningful as capture/binomial CI only for actual one-capture-per-path runs.
        if (ids > 0 and is_actual and trials_per_path == 1 and ids == e["capture_records"]
                and not duplicate_ids.get(m)):
            e["correct_cp95"] = cp_one_sided(e["correct"], ids)
            e["wrong_cp95"] = cp_one_sided(e["wrong"], ids)
            e["inference_unit"] = "independent actual paths among completed captures only; failed records excluded"
        else:
            e["correct_cp95"] = None
            e["wrong_cp95"] = None
            e["inference_unit"] = "conditional/projected or repeated capture records; not independent-path CP"
        attempted = int(d.get("attempted_physical_capture_records", len(captures)))
        if is_actual and trials_per_path == 1 and attempted >= e["correct"] + e["wrong"]:
            e["all_attempts_fail_as_miss"] = {
                "attempted": attempted, "completed_signal_capture_records": e["capture_records"],
                "processing_failures": int(d.get("failed_physical_capture_records", 0)),
                "correct_accepted": e["correct"], "wrong_accepted": e["wrong"],
                "missed_or_failed": attempted-e["correct"]-e["wrong"],
                "correct_cp95": cp_one_sided(e["correct"], attempted),
                "wrong_cp95": cp_one_sided(e["wrong"], attempted),
                "scope": "all attempted captures; processing failures count as missed; one-sided pointwise exact 95% CP"}
        for gate_name, g in e["gate_classifications"].items():
            if ids > 0 and is_actual and trials_per_path == 1 and g["captures"] == ids and not duplicate_ids.get(m):
                g["correct_cp95"] = cp_one_sided(g["correct_accepted"], ids)
                g["wrong_cp95"] = cp_one_sided(g["wrong_accepted"], ids)
                g["confidence_unit"] = "independent actual paths; one capture per unique path"
            else:
                g["correct_cp95"] = None; g["wrong_cp95"] = None
                g["confidence_unit"] = "descriptive only: not one independent actual capture per path"
        # Strict-threshold counterfactual requested by owner, calculated only from saved actual score arrays.
        if strict_gate is not None and score_path and "residual-ucb" in m:
            k = 9
            threshold = math.log(720 * 3 * k * 2 / .005)
            correct = wrong = missed = nrow = 0
            for cap in captures:
                if cap.get("status") != "completed" or cap.get("control") != "signal": continue
                i, truth = int(cap.get("physical_path_index", -1)), cap.get("truth_bit_simulation_only")
                key = f"path{i:05d}_signal_{m}"
                if key not in scores or truth not in (0, 1): continue
                cc = counts_from_scores(scores[key], int(truth), threshold)
                correct += cc["correct_accepted"]; wrong += cc["wrong_accepted"]
                missed += cc["missed"]; nrow += cc["captures"]
            e["counterfactual_gate"] = {"threshold": threshold, "formula": "log(720*3*K*2/.005)", "K": k,
                                        "counts_from_same_saved_observations": {"captures": nrow,
                                        "correct_accepted": correct, "wrong_accepted": wrong, "missed": missed},
                                        "K_definition": "nine selectable methods, not bank rank",
                                        "interpretation": "counterfactual reclassification of same scores; not new trials, calibration, or FAP guarantee"}
    return {"source_report": str(p), "sha256": sha256(p), "schema": d.get("schema"),
            "split": cfg.get("split"), "mode": cfg.get("mode"), "cn0_db_hz": cfg.get("cn0"),
            "device": cfg.get("device"), "precision": cfg.get("precision"),
            "methods": methods, "configuration": cfg,
            "measured_method_runtime_seconds": {m: {"n_completed_captures": len(v),
                "median": float(np.median(v)) if v else None,
                "p95": float(np.quantile(v, .95)) if v else None,
                "minimum": min(v) if v else None, "maximum": max(v) if v else None,
                "scope": "per-capture method update time; actual completed observations only"}
                for m, v in runtime_by_method.items()},
            "measured_preparation_runtime_seconds": {"n_completed_captures": len(preparation_runtime),
                "median": float(np.median(preparation_runtime)) if preparation_runtime else None,
                "scope": "per-capture setup time where recorded"},
            "score_file": str(score_path) if score_path else None,
            "score_sha256": sha256(score_path) if score_path else None,
            "actual_signal_capture_records_with_manifest": actual_ids,
            "attempted_records": d.get("attempted_physical_capture_records", len(captures)),
            "completed_records": d.get("completed_physical_capture_records", sum(c.get("status") == "completed" for c in captures)),
            "failed_records": d.get("failed_physical_capture_records", sum(c.get("status") != "completed" for c in captures)),
            "reported_rows": row_stats, "strict_npz_vs_report_mismatches": score_mismatches,
            "wall_seconds_joint": d.get("wall_seconds_joint"),
            "limitations": d.get("limitations", [])}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True, help="remote-preliminary or remote-final evidence tree")
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    reports = []
    errors = []
    for p in sorted(a.root.rglob("*.json")):
        if p.name in ("outcomes.json",) or p.name.endswith("-status.json") or p.name in ("manifest.json", "campaign.json"):
            continue
        try:
            d = json.loads(p.read_text())
            if isinstance(d, dict) and d.get("schema") in ("triad-noise-study-v1", "triad-noise-batch-study-v1",
                                                            "triad-static-frequency-study-v1"):
                reports.append(analyze_report(p))
        except Exception as ex:
            errors.append({"file": str(p), "error": f"{type(ex).__name__}: {ex}"})
    # Outcomes ledgers are indexes/summaries, never trial data. Keep them as execution accounting.
    ledgers = []
    for p in sorted(a.root.rglob("outcomes.json")):
        try:
            d = json.loads(p.read_text())
            if isinstance(d, dict) and "records" in d:
                ledgers.append({"file": str(p), "required_paths": d.get("required_paths"),
                                "attempted_paths": d.get("attempted_paths"), "wall_seconds": d.get("wall_seconds"),
                                "shards": len(d.get("records", [])),
                                "record_failures": sum(int(x.get("failed", 0)) for x in d.get("records", [])),
                                "attempted_path_count_from_records": sum(len(x.get("paths", [])) for x in d.get("records", []))})
            elif isinstance(d, list):
                ledgers.append({"file": str(p), "records": len(d),
                                "nonzero_returncodes": sum(int(x.get("returncode", 0) or 0) != 0 for x in d if isinstance(x, dict)),
                                "note": "execution index only; records not counted as reception trials"})
        except Exception as ex:
            errors.append({"file": str(p), "error": f"{type(ex).__name__}: {ex}"})
    export_campaigns = []
    for p in sorted(a.root.rglob("campaign.json*")):
        try:
            d = json.loads(p.read_text())
            path_rows = d.get("paths", [])
            counts: dict[str, int] = {}
            for x in path_rows:
                s = str(x.get("status", "unknown")); counts[s] = counts.get(s, 0) + 1
            export_campaigns.append({"file": str(p), "case": d.get("case"), "split": d.get("split"),
                "status": d.get("status"), "paths_requested": d.get("paths_requested"),
                "completed_paths": d.get("completed_paths"), "missing_paths_count": len(d.get("missing_paths") or []),
                "path_status_counts": counts, "incomplete_path_indices": [x.get("path_index") for x in path_rows if x.get("status") != "completed"],
                "source_binary_sha256": d.get("binary_sha256"), "export_driver_sha256": d.get("driver_sha256")})
        except Exception as ex:
            errors.append({"file": str(p), "error": f"{type(ex).__name__}: {ex}"})
    groups = []
    group_keys = sorted({part for r in reports for part in Path(r["source_report"]).parts
                         if part.startswith(("gaussian", "phase")) and
                         ("twoday-holdout" in part or ("hour" in part and "holdout" in part))})
    for key in group_keys:
        items = [r for r in reports if key in Path(r["source_report"]).parts]
        unique: dict[str, dict[tuple[str, str], tuple[int, int, int, int]]] = {}
        duplicate_ids: dict[str, list[list[str]]] = {}
        by_method: dict[str, dict[str, int]] = {}
        for report in items:
            for m, e in report["reported_rows"].items():
                ids = {tuple(x): (e["capture_records"] // max(e["unique_signal_path_identities"], 1),
                                  0, 0, 0) for x in e.get("signal_path_identities", [])}
                # Per-path outcomes are preserved in captures in the report source;
                # the strict aggregate can only be deduplicated safely when an
                # overlapping identity has identical score-derived outcomes.
                pool = unique.setdefault(m, {})
                for ident, outcome in ids.items():
                    if ident in pool:
                        duplicate_ids.setdefault(m, []).append(list(ident))
                    else:
                        pool[ident] = outcome
                agg = by_method.setdefault(m, {"captures": 0, "correct": 0, "wrong": 0, "missed": 0,
                                               "failures": 0, "attempted_records": 0,
                                               "completed_records": 0})
                agg["attempted_records"] += int(report.get("attempted_records", 0))
                agg["completed_records"] += int(report.get("completed_records", 0))
                agg["failures"] += int(report.get("failed_records", 0))
        for m, paths in unique.items():
            agg = by_method[m]
            agg["unique_input_paths"] = len(paths)
            agg["duplicate_input_path_identities_rejected"] = duplicate_ids.get(m, [])
            agg["capture_records"] = sum(v[0] for v in paths.values())
            # Per-path success/error counts are not yet in group identity rows;
            # use shard sums only if no overlap, and make this explicit.
            if not duplicate_ids.get(m):
                agg["captures"] = sum(r["reported_rows"].get(m, {}).get("capture_records", 0) for r in items)
                agg["correct"] = sum(r["reported_rows"].get(m, {}).get("correct", 0) for r in items)
                agg["wrong"] = sum(r["reported_rows"].get(m, {}).get("wrong", 0) for r in items)
                agg["missed"] = sum(r["reported_rows"].get(m, {}).get("missed", 0) for r in items)
            else:
                agg["counts"] = None
            gate_names = sorted({name for r in items for name in
                                 r["reported_rows"].get(m, {}).get("gate_classifications", {})})
            agg["gate_classifications"] = {}
            for gate_name in gate_names:
                vals = [r["reported_rows"][m]["gate_classifications"][gate_name]
                        for r in items if gate_name in r["reported_rows"].get(m, {}).get("gate_classifications", {})]
                total = {k: sum(int(v.get(k, 0)) for v in vals)
                         for k in ("captures", "correct_accepted", "wrong_accepted", "missed")}
                total["threshold_min"] = min(v["threshold_min"] for v in vals)
                total["threshold_max"] = max(v["threshold_max"] for v in vals)
                total["correct_cp95"] = cp_one_sided(total["correct_accepted"], len(paths)) if len(paths) and not duplicate_ids.get(m) else None
                total["wrong_cp95"] = cp_one_sided(total["wrong_accepted"], len(paths)) if len(paths) and not duplicate_ids.get(m) else None
                total["confidence_unit"] = "unique independent actual paths, pointwise one-sided exact 95% CP"
                agg["gate_classifications"][gate_name] = total
        ledger = next((x for x in ledgers if f"/{key}/outcomes.json" in x["file"]), None)
        for m, e in by_method.items():
            n = e.get("unique_input_paths", 0)
            e["correct_cp95"] = cp_one_sided(e["correct"], n) if n and not duplicate_ids.get(m) and e["wrong"] + e["correct"] + e["missed"] == n else None
            e["wrong_cp95"] = cp_one_sided(e["wrong"], n) if n and not duplicate_ids.get(m) else None
            e["unit"] = "unique (manifest hash, observation hash) actual physical paths; overlaps rejected"
            e["processing_failures_counted_as_misses"] = e["failures"]
            e["misses_including_processing_failures"] = e["missed"] + e["failures"]
            if e["attempted_records"] >= e["correct"] + e["wrong"]:
                e["all_attempt_success_cp95_failures_as_miss"] = cp_one_sided(e["correct"], e["attempted_records"])
                e["all_attempt_wrong_cp95_failures_as_no_admission"] = cp_one_sided(e["wrong"], e["attempted_records"])
                e["completed_only_cp_scope"] = "separate completed-path diagnostic; not the all-attempt estimate"
        unique_observed = max((v.get("unique_input_paths", 0) for v in by_method.values()), default=0)
        tail_reports = sum(Path(r["source_report"]).name == "tail.json" for r in items)
        required_paths = (ledger or {}).get("required_paths")
        missing_paths = max(0, int(required_paths) - unique_observed) if required_paths is not None else None
        related_campaign = (next((c for c in export_campaigns if "holdout-twoday" in Path(c["file"]).parts and c.get("case") == "ocxo_twoday"), None)
                            if "twoday" in key and "dense" not in key else None)
        cause = ("all required source paths have unique actual score records"
                 if missing_paths == 0 else
                 f"export was still in progress when the 2700-second outer timeout occurred; status snapshot had {related_campaign.get('path_status_counts', {}).get('running', 0)} running and {related_campaign.get('path_status_counts', {}).get('pending', 0)} pending paths, not individually reported as failed"
                 if related_campaign and related_campaign.get("status") == "running" else
                 "generation or acquisition/export failure not distinguishable from this summary alone")
        groups.append({"group": key, "report_count": len(items), "methods": by_method,
                       "execution_ledger": ledger,
                       "required_source_paths": (ledger or {}).get("required_paths"),
                       "attempted_source_paths": (ledger or {}).get("attempted_paths"),
                       "cohort_accounting": {"required_paths": required_paths,
                           "shard_attempted_paths_from_ledger": (ledger or {}).get("attempted_paths"),
                           "separately_reported_tail_files": tail_reports,
                           "unique_actual_paths_scored": unique_observed,
                           "unobserved_paths": missing_paths,
                           "unobserved_path_cause": cause,
                           "related_export_campaign": related_campaign},
                       "source_completeness": (f"{unique_observed}/{required_paths} unique actual paths scored" if ledger else "no outcomes ledger"),
                       "critical_caveat": "Group identities deduplicated by manifest and observation hashes. If any overlap occurs, strict count totals/CP are withheld because per-path duplicate outcomes cannot be safely inferred from aggregates. Outcomes ledgers are execution accounting, not additional trials."})
    statuses = []
    for p in sorted(a.root.rglob("*-status.json")):
        try:
            d = json.loads(p.read_text())
            if d.get("schema") != "triad-bounded-run-v1": continue
            timeout = bool(d.get("timed_out"))
            statuses.append({"file": str(p), "outcome": d.get("outcome"),
                             "elapsed_seconds": d.get("elapsed_seconds"),
                             "timeout_seconds": d.get("timeout_seconds"),
                             "timed_out": timeout,
                             "runtime_record": ({"kind": "right_censored_lower_bound",
                                                 "seconds_at_least": d.get("timeout_seconds")}
                                                if timeout else {"kind": "completed_elapsed_time",
                                                                 "seconds": d.get("elapsed_seconds")}),
                             "exit_code": d.get("exit_code")})
        except Exception as ex:
            errors.append({"file": str(p), "error": f"{type(ex).__name__}: {ex}"})
    runtimes = []
    for r in reports:
        runtimes.append({"source_report": r["source_report"], "device": r.get("device"),
                         "precision": r.get("precision"), "wall_seconds_joint": r.get("wall_seconds_joint"),
                         "note": "completed report runtime; not normalized throughput and not extrapolated"})
    out = {"schema": "triad-diversity-aggregation-v1", "root": str(a.root),
           "reports": reports, "execution_ledgers_not_trials": ledgers,
           "holdout_groups": groups, "bounded_run_statuses": statuses,
           "export_campaigns": export_campaigns,
           "completed_report_runtime_evidence": runtimes, "errors": errors,
           "known_variance_false_admission": "unsupported when noise changes from the calibrated reference; strict JSON calibration statuses are retained per method",
           "runtime_censoring": "completed bounded-run elapsed times are point observations; timed-out jobs carry only the declared timeout as a right-censored lower bound, never a runtime point estimate",
           "scope": "No aggregate acceptance claim unless unique independent input paths, full required cohort, matching score/truth verification, and calibrated FAP assumptions all pass."}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(f"reports={len(reports)} ledgers={len(ledgers)} errors={len(errors)} output={a.output}")


if __name__ == "__main__":
    main()

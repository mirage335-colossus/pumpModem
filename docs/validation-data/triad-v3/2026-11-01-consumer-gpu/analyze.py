#!/usr/bin/env python3
"""Summarize consumer-GPU campaign evidence and generate scoped plots.

Only input-rooted JSON/NPZ evidence is consumed. Tensor probe timings are
reported separately from sampled-capture throughput. Holdout captures are
the independent statistical units; timing replays and candidate columns never
increase trial counts. No policy is selected by this analysis.
"""
from __future__ import annotations

import argparse
import collections
import csv
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import statistics

# Keep Matplotlib's font/cache writes out of the read-only home directory.
os.environ.setdefault("MPLCONFIGDIR", "/tmp/triad-consumer-gpu-mpl")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


POLICY_ORDER = ("fp64", "fp32", "tensor:fp16", "tensor:bf16", "tensor:tf32",
                "tensor:fp8-e4m3", "tensor:fp8-e5m2", "tensor:int8")
POLICY_LABEL = {"fp64": "FP64 correlation", "fp32": "Strict FP32 correlation",
                "tensor:fp16": "FP16", "tensor:bf16": "BF16",
                "tensor:tf32": "TF32", "tensor:fp8-e4m3": "FP8 E4M3",
                "tensor:fp8-e5m2": "FP8 E5M2", "tensor:int8": "INT8"}
COLORS = {"fp64": "#333333", "fp32": "#777777", "tensor:fp16": "#177e89",
          "tensor:bf16": "#2a9d8f", "tensor:tf32": "#e9c46a",
          "tensor:fp8-e4m3": "#f4a261", "tensor:fp8-e5m2": "#e76f51",
          "tensor:int8": "#7251a3"}


def json_load(path: Path):
    return json.loads(path.read_text())


def mean_ci(successes: int, total: int, confidence: float = .95):
    """Two-sided Hoeffding interval for independent bounded outcomes.

    This remains valid for the holdout's fixed balanced-bit design; it does not
    assume identically distributed Bernoulli draws.
    """
    if total <= 0:
        return (None, None)
    alpha = 1.0-confidence
    p = successes/total
    eps = math.sqrt(math.log(2.0/alpha)/(2.0*total))
    return max(0.0, p-eps), min(1.0, p+eps)


def policy_label(policy, data=None):
    """Name correlation policy separately from the shared inference path."""
    if data:
        precision = data.get("precision", {})
        correlation = precision.get("correlation")
        inference = precision.get("inference")
        implementation = precision.get("inference_implementation")
        if correlation:
            corr_name = {"fp64": "FP64", "fp32": "strict FP32", "tensor": data.get("arithmetic_backend", {}).get("format", "tensor")}.get(correlation, correlation)
            suffix = f"; {inference} inference" if inference else ""
            if implementation:
                suffix += f"/{implementation}"
            return corr_name + " correlation" + suffix
    return POLICY_LABEL.get(policy, policy)


def find_indices(root: Path):
    for path in sorted(root.rglob("index.json")):
        try:
            item = json_load(path)
        except Exception:
            continue
        if item.get("schema") == "consumer-gpu-campaign-index-v1":
            yield path, item


def find_report(root: Path, record: dict):
    candidate_names = []
    for key in ("output_json", "output"):
        value = record.get(key)
        if value:
            candidate_names.append(Path(value).name)
    for name in candidate_names:
        direct = root.rglob(name)
        for match in direct:
            if match.is_file() and not match.name.endswith(".failure.json"):
                return match
    return None


def find_npz(root: Path, record: dict, report: Path | None):
    value = record.get("output_npz") or record.get("score_archive")
    expected_sha256 = None
    if report and report.is_file():
        try:
            expected_sha256 = json_load(report).get("score_sha256")
        except Exception:
            return None
    local = report.with_suffix(".npz") if report else None
    candidates = []
    if local and local.is_file():
        candidates.append(local)
    elif value:
        candidates.extend(p for p in root.rglob(Path(value).name) if p.is_file())
    # The report-adjacent sidecar is preferred, but never trusted without the
    # archived digest when one is declared. A stale or mismatched sidecar is
    # rejected; basename-only fallback must be unique and digest-matched.
    if expected_sha256:
        matches = []
        for candidate in candidates:
            digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
            if digest == expected_sha256:
                matches.append(candidate)
        unique = list(dict.fromkeys(matches))
        if len(unique) == 1:
            return unique[0]
        if len(unique) != 1:
            return None
        # If the adjacent path was absent, search all basename matches, but
        # reject ambiguity across different contents.
        fallback = [p for p in root.rglob(Path(value).name) if p.is_file()] if value else []
        matches = [p for p in fallback if hashlib.sha256(p.read_bytes()).hexdigest() == expected_sha256]
        unique = list(dict.fromkeys(matches))
        return unique[0] if len(unique) == 1 else None
    if local and local.is_file():
        return local
    unique = list(dict.fromkeys(candidates))
    return unique[0] if len(unique) == 1 else None


def policy_from_record(record: dict, data: dict | None = None):
    policy = record.get("policy")
    if policy:
        return policy
    if data:
        precision = data.get("precision", {}).get("correlation")
        fmt = data.get("arithmetic_backend", {}).get("format")
        return f"tensor:{fmt}" if precision == "tensor" and fmt else precision
    return None


def scan_first(data: dict):
    scans = data.get("scans") or []
    return scans[0] if scans else {}


def final_decision(data: dict, gate_name: str):
    decisions = data.get("decisions", {}).get("final", [])
    for row in decisions:
        if row.get("gate") == gate_name:
            return row
    return None


def probe_summaries(root: Path):
    out = []
    for path in sorted(root.glob("probe-*.json")):
        data = json_load(path)
        timings = data.get("timings", {})
        def median(stage, field="median_cuda_ms"):
            return timings.get(stage, {}).get(field)
        strict = median("strict_fp32_product")
        product = median("cached_product")
        full = median("both_pack_product")
        entry = {
            "file": path.name, "format": data.get("contract", {}).get("format"),
            "fused_pack": "fused-fixed" in path.name,
            "pack_implementation": data.get("contract", {}).get("pack_implementation"),
            "shape_mnk": data.get("shape_mnk"), "seed": data.get("seed"),
            "gpu": data.get("environment", {}).get("gpu"),
            "compute_capability": data.get("environment", {}).get("compute_capability"),
            "execution": data.get("execution", {}).get("resolved_implementations"),
            "native_ptx_verified": data.get("execution", {}).get("native_ptx_verified"),
            "cold_seconds": data.get("cold_seconds"),
            "pack_a_cuda_ms": median("pack_a"), "pack_b_cuda_ms": median("pack_b"),
            "cached_product_cuda_ms": product,
            "cached_a_pack_b_product_cuda_ms": median("cached_a_pack_b_product"),
            "both_pack_product_cuda_ms": full, "strict_fp32_product_cuda_ms": strict,
            "cached_product_speedup_vs_strict_fp32": strict/product if strict and product else None,
            "full_pack_product_speedup_vs_strict_fp32_product_only": strict/full if strict and full else None,
            "oracle_max_abs_error": data.get("represented_fp64_oracle", {}).get("max_abs_error"),
            "oracle_error_bound_pass": data.get("represented_fp64_oracle", {}).get("error_bound_pass"),
            "contract": data.get("contract"), "execution_contract": data.get("execution"),
            "paired_numerical_cases": data.get("paired_numerical_cases"),
            "scope": data.get("scope"),
        }
        if data.get("contract", {}).get("format") == "fp16":
            implementation = next(iter((data.get("execution", {}).get("resolved_implementations") or {}).values()), "unknown")
            entry["plot_label"] = "FP16 (Torch; PTX unverified)" if implementation == "torch" else "FP16 (Triton)"
        elif "maskfix" in path.name:
            entry["plot_label"] = f"{data.get('contract', {}).get('format')} (mask fix)"
        elif "fused-fixed" in path.name:
            fmt = data.get("contract", {}).get("format")
            entry["plot_label"] = f"{fmt} (fused pack; diagnostic only)" if str(fmt).startswith("fp8") else f"{fmt} (fused pack)"
        out.append(entry)
    return out


def campaign_rows(root: Path):
    rows, indexes = [], []
    for index_path, index in find_indices(root):
        indexes.append({"path": str(index_path), "stage": index.get("stage"),
                        "status": index.get("status"), "elapsed_seconds": index.get("elapsed_seconds"),
                        "record_count": index.get("record_count"), "completed": index.get("completed"),
                        "failed": index.get("failed"), "source_revision": index.get("source_revision"),
                        "device": index.get("device"), "calibration_status": index.get("calibration_status")})
        for record in index.get("records", []):
            row = dict(index_path=index_path, index=index, record=record,
                       variant=index_path.parent.name, policy=record.get("policy"),
                       data=None, report_path=None, npz_path=None)
            local_output = record.get("output_json") or record.get("output")
            sibling = index_path.parent/Path(local_output).name if local_output else None
            report = sibling if sibling and sibling.is_file() else (find_report(root, record) if record.get("status") == "completed" else None)
            if report:
                try:
                    data = json_load(report)
                except Exception:
                    data = None
                row.update(data=data, report_path=report,
                           npz_path=find_npz(root, record, report))
                row["policy"] = policy_from_record(record, data)
            rows.append(row)
    return rows, indexes


def load_score(row):
    path = row.get("npz_path")
    if not path:
        return None
    try:
        archive = np.load(path)
        if "score_0" in archive.files:
            return np.asarray(archive["score_0"], dtype=np.float64)
        if "signal" in archive.files:
            return np.asarray(archive["signal"], dtype=np.float64)
        return None
    except Exception:
        return None


def bit_scores_from_candidates(scores: np.ndarray, report: dict):
    """Reproduce the declared equal-weight candidate mixture and CFO/lag max."""
    labels = report.get("sampled_manifest", {}).get("candidate_bits")
    if labels is None:
        labels = report.get("sampled_manifest", {}).get("candidates")
        if labels and isinstance(labels[0], dict):
            labels = [item.get("bit") for item in labels]
    if labels is None or len(labels) != scores.shape[-2]:
        return None
    labels = np.asarray(labels)
    # The stored score tensor is [capture, CFO, candidate, lag]. Restore the
    # receiver's [capture, CFO, lag, candidate] ordering. Keep its
    # log-mean-exp across equal-prior candidates of each bit, then max over
    # every searched CFO/lag alternative.
    scores = np.transpose(scores, (0, 1, 3, 2))
    output = []
    for bit in (0, 1):
        values = scores[..., labels == bit]
        if values.shape[-1] == 0:
            return None
        peak = np.max(values, axis=-1, keepdims=True)
        logmean = np.squeeze(peak, axis=-1) + np.log(np.mean(np.exp(values-peak), axis=-1))
        output.append(np.max(logmean, axis=(1, 2)))
    return np.stack(output, axis=1)


def paired_numerics(rows):
    by_stage_input = {}
    for row in rows:
        if row.get("record", {}).get("status") != "completed" or not row.get("data"):
            continue
        stage = row["record"].get("stage") or row["index"].get("stage")
        inp = row["record"].get("input")
        if stage not in ("screen", "screening-holdout") or not inp:
            continue
        by_stage_input.setdefault((row.get("variant"), stage, inp), {})[row.get("policy")] = row
    comparisons = []
    for (variant, stage, inp), policies in sorted(by_stage_input.items()):
        ref = policies.get("fp64")
        if not ref:
            continue
        ref_scores = load_score(ref)
        if ref_scores is None:
            continue
        ref_report = ref["data"]
        for policy, row in policies.items():
            if policy == "fp64":
                continue
            scores = load_score(row)
            if scores is None or scores.shape != ref_scores.shape:
                continue
            delta = scores-ref_scores
            exact = np.array_equal(scores, ref_scores)
            # NPZ axis order is [capture, CFO, candidate, lag]. Candidate rank
            # checks retain every CFO/lag cell; admission is separately
            # compared using the saved final decision rows.
            mode_cells = np.transpose(scores, (0, 1, 3, 2))
            ref_cells = np.transpose(ref_scores, (0, 1, 3, 2))
            rank_change = int(np.sum(np.argmax(mode_cells, axis=-1) != np.argmax(ref_cells, axis=-1)))
            ref_bit_scores = bit_scores_from_candidates(ref_scores, ref_report)
            mode_bit_scores = bit_scores_from_candidates(scores, row["data"])
            if ref_bit_scores is not None and mode_bit_scores is not None:
                bit_winner_changes = int(np.sum(np.argmax(mode_bit_scores, axis=-1) != np.argmax(ref_bit_scores, axis=-1)))
                bit_score_max_delta = float(np.max(np.abs(mode_bit_scores-ref_bit_scores)))
            else:
                bit_winner_changes, bit_score_max_delta = None, None
            baseline_decisions, mode_decisions = {}, {}
            for gate in ("default_99pct_1e-4_day", "relaxed_90pct_1pct_12h"):
                b, m = final_decision(ref_report, gate), final_decision(row["data"], gate)
                if b is not None:
                    baseline_decisions[gate] = b
                if m is not None:
                    mode_decisions[gate] = m
            decision_diff = {}
            for gate in set(baseline_decisions) & set(mode_decisions):
                b, m = baseline_decisions[gate], mode_decisions[gate]
                fields = ("admitted", "correct_accepted", "wrong_accepted", "missed", "noise_admitted")
                changed = {field: (b.get(field), m.get(field))
                           for field in fields if b.get(field) != m.get(field)}
                if changed:
                    decision_diff[gate] = changed
            f = row["record"].get("cn0_db_hz")
            if f is None:
                f = row["data"].get("sampled_manifest", {}).get("cn0_db_hz")
            comparisons.append({
                "variant": variant, "stage": stage, "input": inp, "cn0_db_hz": f, "policy": policy,
                "score_shape": list(scores.shape), "max_abs_candidate_score_delta": float(np.max(np.abs(delta))),
                "mean_abs_candidate_score_delta": float(np.mean(np.abs(delta))),
                "rms_candidate_score_delta": float(np.sqrt(np.mean(delta*delta))),
                "max_abs_bit_score_delta": bit_score_max_delta,
                "candidate_winner_changes_by_capture_cfo_lag_cell": rank_change,
                "candidate_winner_cells": int(np.prod(mode_cells.shape[:-1])),
                "bit_winner_changes": bit_winner_changes,
                "score_tensor_bit_identical": bool(exact), "decision_differences": decision_diff,
                "policy_display": policy_label(policy, row["data"]),
                "reference_display": policy_label("fp64", ref_report),
                "baseline_admission_calibration": ref_report.get("precision", {}).get("admission_calibration"),
                "mode_admission_calibration": row["data"].get("precision", {}).get("admission_calibration"),
                "timing_replays_are_not_trials": True,
            })
    return comparisons


def decision_summaries(rows):
    # One independently sampled, content-hashed capture contributes one
    # bounded outcome per variant/source-policy group. Duplicate reruns and
    # incomplete outcomes are excluded rather than counted as failures.
    points = {}
    exclusions = {"missing_capture_hash": 0, "duplicate_capture_hash": 0,
                  "missing_decision": 0, "missing_outcome": 0}
    eligible = []
    seen = {}
    for row in rows:
        rec, data = row.get("record", {}), row.get("data")
        if rec.get("stage") != "screening-holdout" or not data or rec.get("status") != "completed":
            continue
        policy = row.get("policy")
        hashes = data.get("input_hashes")
        if not isinstance(hashes, dict) or not hashes:
            exclusions["missing_capture_hash"] += 1
            continue
        capture_hash = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        source_revision = data.get("source_revision") or row.get("index", {}).get("source_revision")
        duplicate_key = (row.get("variant"), source_revision, policy, capture_hash)
        if duplicate_key in seen:
            exclusions["duplicate_capture_hash"] += 1 if seen[duplicate_key].get("duplicate") else 2
            seen[duplicate_key]["duplicate"] = True
            eligible.append({"duplicate": True})
            continue
        item = {"duplicate": False, "row": row, "capture_hash": capture_hash, "duplicate_key": duplicate_key}
        seen[duplicate_key] = item
        eligible.append(item)

    # Mark and remove every occurrence in a duplicated group, including the
    # first, so repeated report copies cannot inflate confidence.
    eligible = [item for item in eligible if item.get("row") and not item.get("duplicate") and not seen[item["duplicate_key"]].get("duplicate")]
    for item in eligible:
        row = item["row"]
        rec, data = row["record"], row["data"]
        policy = row.get("policy")
        cn0 = rec.get("cn0_db_hz")
        if cn0 is None:
            cn0 = data.get("sampled_manifest", {}).get("cn0_db_hz")
        if cn0 is None:
            continue
        stratum = rec.get("stratum")
        if stratum not in ("boundary", "target", "noise-only"):
            ctl = data.get("sampled_manifest", {}).get("control")
            stratum = "noise-only" if ctl == "null" else ("target" if str(rec.get("input", "")).startswith("target-") else "boundary")
        for gate in ("default_99pct_1e-4_day", "relaxed_90pct_1pct_12h"):
            d = final_decision(data, gate)
            if d is None:
                exclusions["missing_decision"] += 1
                continue
            if stratum == "noise-only":
                outcome = d.get("noise_admitted")
            else:
                outcome = d.get("correct_accepted")
            if outcome is None:
                exclusions["missing_outcome"] += 1
                continue
            variant = row.get("variant")
            key = (variant, policy, stratum, float(cn0), gate)
            entry = points.setdefault(key, {"successes": 0, "total": 0, "wrong": 0, "admissions": 0, "inputs": [], "policy_display": policy_label(policy, data)})
            entry["total"] += 1
            entry["inputs"].append(rec.get("input"))
            if stratum == "noise-only":
                entry["admissions"] += int(bool(outcome))
            else:
                entry["successes"] += int(bool(outcome))
                entry["wrong"] += int(d.get("wrong_accepted", 0) or 0)
                entry["admissions"] += int(d.get("admitted", 0) or 0)
    out = []
    for (variant, policy, stratum, cn0, gate), e in sorted(points.items(), key=lambda x: (x[0][0], POLICY_ORDER.index(x[0][1]) if x[0][1] in POLICY_ORDER else 99, x[0][2], x[0][3], x[0][4])):
        n, k = e["total"], e["admissions"] if stratum == "noise-only" else e["successes"]
        low, high = mean_ci(k, n)
        out.append({"variant": variant, "policy": policy, "stratum": stratum, "cn0_db_hz": cn0, "gate": gate,
                    "successes_or_null_admissions": k, "trials": n, "rate": k/n if n else None,
                    "ci95_low": low, "ci95_high": high,
                    "wrong_acceptances": e["wrong"], "accepted": e["admissions"],
                    "distinct_inputs": len(set(e["inputs"])),
                    "policy_display": e["policy_display"],
                    "interval_method": "two-sided Hoeffding; independent bounded capture outcomes; fixed balanced-bit design",
                    "note": "pointwise interval; no horizon-tail qualification"})
    return out, exclusions


def runtime_summaries(rows):
    out = []
    for row in rows:
        data = row.get("data")
        rec = row.get("record", {})
        if not data or rec.get("status") != "completed":
            continue
        stage = rec.get("stage") or row["index"].get("stage")
        if stage not in ("screen", "screening-holdout"):
            continue
        scan = scan_first(data)
        bank = scan.get("bank", {})
        out.append({"variant": row.get("variant"), "stage": stage, "input": rec.get("input"), "cn0_db_hz": rec.get("cn0_db_hz"),
                    "policy": row.get("policy"), "policy_display": policy_label(row.get("policy"), data), "single_capture_latency_s": scan.get("single_capture_latency_seconds"),
                    "complete_resident_s": scan.get("complete_resident_seconds"),
                    "resident_hypotheses_per_s": scan.get("exhaustive_hypotheses_per_second"),
                    "capture_seconds": scan.get("capture_duration_seconds"),
                    "streaming_realtime_factor": scan.get("streaming_realtime_factor"),
                    "correlation_device_event_s": scan.get("correlation_device_event_seconds"),
                    "correlation_host_enqueue_s": scan.get("correlation_host_enqueue_seconds"),
                    "inference_update_device_event_s": scan.get("inference_update_device_event_seconds"),
                    "inference_update_host_enqueue_s": scan.get("inference_update_host_enqueue_seconds"),
                    "inference_finish_device_event_s": scan.get("inference_finish_seconds") if scan.get("cuda_event_timing_is_instrumented") else None,
                    "inference_finish_host_enqueue_s": scan.get("inference_finish_host_enqueue_seconds"),
                    "bank_matmul_seconds_accounted": bank.get("matmul_seconds"),
                    "bank_a_prepare_seconds_accounted": bank.get("a_prepare_seconds"),
                    "bank_b_pack_seconds_accounted": bank.get("b_pack_seconds"),
                    "bank_reconstruct_seconds_accounted": bank.get("reconstruct_seconds"),
                    "bank_gram_seconds_accounted": bank.get("gram_seconds"),
                    "device_event_timing_instrumented": scan.get("cuda_event_timing_is_instrumented"),
                    "driver_call_wall_seconds": rec.get("driver_call_wall_seconds"),
                    "campaign_wall_after_imports_s": row["index"].get("elapsed_seconds"),
                    "within_process_s": data.get("timing", {}).get("within_process_seconds"),
                    "calibration": data.get("precision", {}).get("admission_calibration")})
    return out


def performance_summaries(rows):
    out = []
    for row in rows:
        rec = row.get("record", {})
        if rec.get("stage") != "performance" or rec.get("status") != "completed":
            continue
        data = row.get("data") or {}
        aba = rec.get("baseline_aba_timings") or data.get("baseline_aba_timings") or []
        replays = rec.get("timing_replays") or data.get("timing_replays") or []
        prep = rec.get("fixed_workspace_preparation_seconds", data.get("fixed_workspace_preparation_seconds"))
        def vals(items, key):
            return [float(x[key]) for x in items if isinstance(x, dict) and isinstance(x.get(key), (int, float))]
        baseline_a = aba[0] if len(aba) > 0 else {}
        baseline_b = aba[1] if len(aba) > 1 else {}
        baseline_a_warm = aba[2] if len(aba) > 2 else {}
        warm_seconds = baseline_a_warm.get("complete_receiver_compute_seconds")
        replay_values = vals(replays, "complete_receiver_compute_seconds")
        replay_median = statistics.median(replay_values) if replay_values else None
        savings = warm_seconds-replay_median if warm_seconds is not None and replay_median is not None else None
        out.append({"variant": row.get("variant"), "policy": rec.get("policy"), "policy_display": policy_label(rec.get("policy"), data), "input": rec.get("input"),
                    "preparation_seconds": prep,
                    "baseline_cold_a_seconds": baseline_a.get("complete_receiver_compute_seconds"),
                    "baseline_b_seconds": baseline_b.get("complete_receiver_compute_seconds"),
                    "baseline_warm_a_seconds": warm_seconds,
                    "workspace_replay_median_complete_seconds": replay_median,
                    "workspace_replay_median_device_seconds": statistics.median(vals(replays, "graph_device_seconds")) if vals(replays, "graph_device_seconds") else None,
                    "workspace_replay_speedup_vs_warm_a": warm_seconds/replay_median if warm_seconds and replay_median else None,
                    "setup_break_even_replays_estimate": prep/savings if prep is not None and savings and savings > 0 else None,
                    "timing_replay_count": len(replays),
                    "all_baseline_comparisons_within_2e_5": rec.get("all_baseline_comparisons_within_2e_5", data.get("all_baseline_comparisons_within_2e_5")),
                    "a_after_b_bit_identical": rec.get("a_after_b_bit_identical", data.get("a_after_b_bit_identical")),
                    "second_observation_kind": data.get("second_observation_kind"),
                    "peak_allocated_bytes": data.get("actual_peak_allocated_bytes"),
                    "peak_reserved_bytes": data.get("actual_peak_reserved_bytes"),
                    "scope": data.get("scope")})
    return out


def failed_summaries(indexes):
    failed = []
    for index in indexes:
        path = Path(index["path"])
        data = json_load(path)
        for rec in data.get("records", []):
            if rec.get("status") == "failed":
                failed.append({"stage": rec.get("stage") or data.get("stage"),
                               "policy": rec.get("policy"), "format": rec.get("format"),
                               "input": rec.get("input"), "error": rec.get("error"),
                               "stderr_tail": rec.get("stderr_tail")})
    return failed


def hardware_summary(root: Path):
    path = root/"hardware.json"
    if not path.is_file():
        return None, []
    data = json_load(path)
    rows = []
    for size in data.get("transfers", {}).get("results", []):
        for direction in ("h2d", "d2h"):
            for item in size.get(direction, []):
                event, wall = item.get("cuda_event", {}), item.get("wall", {})
                rows.append({"size_mib": size.get("size_mib"), "direction": direction,
                             "host_memory_kind": item.get("host_memory_kind"),
                             "event_median_seconds": event.get("median_seconds"),
                             "event_mean_GiB_per_second": event.get("mean_GiB_per_second"),
                             "wall_median_seconds": wall.get("median_seconds"),
                             "wall_mean_GiB_per_second": wall.get("mean_GiB_per_second"),
                             "scope": item.get("measurement_scope")})
    meta = {"gpu": data.get("cuda_device", {}).get("name"),
            "compute_capability": data.get("cuda_device", {}).get("compute_capability"),
            "cpu": data.get("host", {}).get("cpu", {}).get("model name"),
            "cpu_quota_cores": data.get("host", {}).get("cpu_limits", {}).get("cgroup_cpu_quota_cores"),
            "telemetry": data.get("physical_gpu_telemetry", {}).get("values"),
            "transfer_contract": data.get("transfers", {}).get("interpretation")}
    return meta, rows


def _interval_union(intervals):
    merged = []
    for start, stop in sorted((a, b) for a, b in intervals if b > a):
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], stop))
        else:
            merged.append((start, stop))
    return merged


def profile_kernel_summaries(root: Path):
    """Summarize only Chrome trace `kernel` X-events; CPU/API rows are separate."""
    out = []
    for trace_path in sorted(root.glob("profile-*/torch-trace.json.gz")):
        mode = trace_path.parent.name.removeprefix("profile-")
        try:
            trace = json.loads(gzip.open(trace_path, "rt", encoding="utf-8").read())
        except Exception:
            continue
        events = [e for e in trace.get("traceEvents", [])
                  if e.get("cat") == "kernel" and e.get("ph") == "X"
                  and isinstance(e.get("dur"), (int, float)) and isinstance(e.get("ts"), (int, float))]
        if not events:
            continue
        counts = collections.Counter(e.get("name", "") for e in events)
        durations = collections.Counter()
        for event in events:
            durations[event.get("name", "")] += float(event["dur"])
        total_us = sum(float(e["dur"]) for e in events)
        kernel_intervals = [(float(e["ts"]), float(e["ts"])+float(e["dur"])) for e in events]
        device_union = _interval_union(kernel_intervals)
        device_span = max(b for _, b in kernel_intervals)-min(a for a, _ in kernel_intervals)
        host_events = [e for e in trace.get("traceEvents", []) if e.get("ph") == "X"
                       and e.get("cat") in ("cpu_op", "cuda_runtime", "cuda_driver", "overhead")
                       and e.get("pid") != 0 and isinstance(e.get("dur"), (int, float))
                       and isinstance(e.get("ts"), (int, float))]
        host_intervals = [(float(e["ts"]), float(e["ts"])+float(e["dur"])) for e in host_events]
        host_union = _interval_union(host_intervals)
        host_span = max((b for _, b in host_intervals), default=0)-min((a for a, _ in host_intervals), default=0)

        def selected(predicate):
            names = [name for name in counts if predicate(name)]
            return {"count": sum(counts[name] for name in names),
                    "duration_ms": sum(durations[name] for name in names)/1000,
                    "names": names}

        stencil = selected(lambda n: n == "stencil")
        phase_frequency = selected(lambda n: n == "ampere_sgemm_128x128_nt")
        sums = selected(lambda n: n in ("partial_sum", "final_sum") or "sum_functor" in n)
        packing = selected(lambda n: n == "pack")
        if mode == "fp16":
            product = selected(lambda n: n == "product")
            product_scope = "distinct Triton product kernel"
        else:
            # Source-identified strict-FP32 correlation matmul emits one
            # cuBLAS GEMM and one split-K reduction per aten::mm call.
            product = selected(lambda n: n == "ampere_sgemm_32x32_sliced1x4_tn" or "splitKreduce_kernel" in n)
            product_scope = "matched strict-FP32 correlation GEMM + split-K reduction"
        removable_ms = product["duration_ms"]+packing["duration_ms"]
        amdahl = total_us/1000/(total_us/1000-removable_ms) if 0 < removable_ms < total_us/1000 else None
        profile_meta = {}
        profile_json = trace_path.parent/"profile.json"
        if profile_json.exists():
            try:
                profile_meta = json_load(profile_json)
            except Exception:
                pass
        top = [{"name": name, "count": counts[name], "duration_ms": durations[name]/1000,
                "share_percent": 100*durations[name]/total_us}
               for name in sorted(durations, key=durations.get, reverse=True)[:8]]
        out.append({"mode": mode, "trace": str(trace_path), "kernel_events": len(events),
                    "kernel_duration_sum_ms": total_us/1000,
                    "device_kernel_union_ms": sum(b-a for a, b in device_union)/1000,
                    "device_first_to_last_kernel_span_ms": device_span/1000,
                    "device_kernel_free_gaps_union_ms": (device_span-sum(b-a for a, b in device_union))/1000,
                    "host_visible_event_span_ms": host_span/1000,
                    "host_visible_event_union_ms": sum(b-a for a, b in host_union)/1000,
                    "host_visible_event_gap_union_ms": (host_span-sum(b-a for a, b in host_union))/1000,
                    "host_visible_max_gap_ms": max((host_union[i+1][0]-host_union[i][1]
                        for i in range(len(host_union)-1)), default=0)/1000,
                    "repeats": profile_meta.get("instrumentation", {}).get("repeats"),
                    "stencil": stencil, "phase_frequency_sgemm": phase_frequency,
                    "sum_kernels": sums, "correlation_product": product,
                    "product_scope": product_scope, "packing": packing,
                    "product_plus_pack_kernel_only_amdahl_ceiling": amdahl,
                    "top_kernel_names": top,
                    "limitations": profile_meta.get("limitations", [])})
    return out


def _distribution(values):
    values = sorted(float(v) for v in values if isinstance(v, (int, float)))
    if not values:
        return None
    def quantile(q):
        return values[min(len(values)-1, int(q*(len(values)-1)))]
    return {"count": len(values), "mean": statistics.mean(values),
            "p10": quantile(.10), "median": statistics.median(values), "p90": quantile(.90)}


def _gpu_telemetry_summary(path: Path):
    # nvidia-smi query CSV has no header in this artifact. Column order is
    # recorded in sustain.json and matches this fixed command query contract.
    columns = {"gpu_util_pct": 1, "memory_util_pct": 2, "memory_used_mib": 3,
               "power_w": 4, "sm_clock_mhz": 5, "memory_clock_mhz": 6}
    values = {name: [] for name in columns}
    if not path.is_file():
        return values
    with path.open(newline="") as stream:
        for row in csv.reader(stream):
            for name, index in columns.items():
                try:
                    values[name].append(float(row[index]))
                except (IndexError, ValueError):
                    continue
    return {name: _distribution(samples) for name, samples in values.items()}


def sustained_summaries(root: Path):
    out = []
    for path in sorted(root.glob("sustain-*/sustain.json")):
        data = json_load(path)
        result_path = path.parent/(data.get("result_file") or "sustained-receiver.json")
        if not result_path.is_file():
            continue
        receiver = json_load(result_path)
        contract = data.get("replay_contract", {})
        copies = int(contract.get("repeat_observations", 1))
        replays = receiver.get("timing_replays", [])
        batch_latency = _distribution([r.get("complete_receiver_compute_seconds") for r in replays])
        batch_device = _distribution([r.get("graph_device_seconds") for r in replays])
        baseline = receiver.get("baseline_aba_timings", [])
        warm_a = baseline[2].get("complete_receiver_compute_seconds") if len(baseline) > 2 else None
        median_batch = batch_latency["median"] if batch_latency else None
        main_wall = receiver.get("complete_inside_main_benchmark_seconds")
        physical_copy_count = int(contract.get("repeats", len(replays)))*copies
        summed_complete_replay_s = sum(float(r["complete_receiver_compute_seconds"])
                                       for r in replays
                                       if isinstance(r.get("complete_receiver_compute_seconds"), (int, float)))
        replay_timing_count = sum(isinstance(r.get("complete_receiver_compute_seconds"), (int, float))
                                  for r in replays)
        telemetry_file = data.get("gpu_telemetry", {}).get("csv_file")
        telemetry = _gpu_telemetry_summary(path.parent/telemetry_file) if telemetry_file else {}
        out.append({"name": path.parent.name, "policy": data.get("policy"),
                    "status": data.get("status"), "returncode": data.get("returncode"),
                    "timed_out": data.get("timed_out"), "repeat_count": contract.get("repeats"),
                    "repeat_observations": copies,
                    "independent_physical_captures": contract.get("physical_sampled_captures"),
                    "copies_are_independent_trials": contract.get("repeats_and_observation_copies_are_independent_trials"),
                    "timing_replays_count": len(replays), "batch_latency_s": batch_latency,
                    "amortized_service_time_per_copy_s": ({k: v/copies for k, v in batch_latency.items() if k != "count"} if batch_latency else None),
                    "amortized_graph_device_time_per_copy_s": ({k: v/copies for k, v in batch_device.items() if k != "count"} if batch_device else None),
                    "batch_throughput_per_s": copies/median_batch if median_batch else None,
                    "summed_complete_replay_seconds": summed_complete_replay_s,
                    "summed_complete_replay_timing_count": replay_timing_count,
                    "summed_complete_replay_copies_per_s": physical_copy_count/summed_complete_replay_s if summed_complete_replay_s else None,
                    "summed_complete_replay_amortized_seconds_per_copy": summed_complete_replay_s/physical_copy_count if physical_copy_count else None,
                    "main_benchmark_seconds": main_wall,
                    "main_benchmark_per_copy_throughput_per_s": physical_copy_count/main_wall if main_wall else None,
                    "baseline_warm_a_s": warm_a,
                    "workspace_median_speedup_vs_warm_a": warm_a/median_batch if warm_a and median_batch else None,
                    "fixed_workspace_preparation_s": receiver.get("fixed_workspace_preparation_seconds"),
                    "external_wall_s": data.get("wall_seconds"),
                    "cpu_quota_cores": data.get("cpu_cgroup", {}).get("after", {}).get("quota_cores"),
                    "average_cpu_cores_used": data.get("cpu_cgroup", {}).get("average_cpu_cores_used"),
                    "cpu_scope": data.get("cpu_cgroup", {}).get("average_cpu_cores_used_scope"),
                    "gpu_telemetry": telemetry,
                    "telemetry_sampling_interval_s": data.get("gpu_telemetry", {}).get("sampling_interval_seconds"),
                    "telemetry_limitations": data.get("sampling_note"),
                    "input_hashes": receiver.get("input_hashes"),
                    "receiver": receiver})
    return out


def tile_sweep_summary(root: Path):
    fixed_path, initial_path = root/"tile-sweep-fixed"/"index.json", root/"tile-sweep"/"index.json"
    fixed = json_load(fixed_path) if fixed_path.is_file() else None
    initial = json_load(initial_path) if initial_path.is_file() else None
    groups = []
    if fixed:
        records = fixed.get("records", [])
        for shape in fixed.get("shapes_MNK", sorted({tuple(r.get("shape_MNK", [])) for r in records if r.get("shape_MNK")})):
            shape = tuple(shape)
            subset = [r for r in records if tuple(r.get("shape_MNK", [])) == shape]
            out_records = []
            for row in subset:
                gate = row.get("represented_fp64_oracle_8x8") or {}
                strict = row.get("strict_fp32_reference_check_8x8") or {}
                stages = row.get("stages", {})
                def median_ms(name):
                    value = (stages.get(name, {}).get("cuda_event", {}) or {}).get("median_seconds")
                    return float(value)*1000 if value is not None else None
                out_records.append({"status": row.get("status"),
                    "tile": row.get("tile_BM_BN_BK_warps"),
                    "accuracy_gate_passed": bool(row.get("status") == "completed" and gate.get("allclose_atol_5e-4_rtol_2e-4") is True and gate.get("native_ptx_evidence") is True and row.get("backend_runtime", {}).get("native_ptx_verified")),
                    "oracle_allclose": gate.get("allclose_atol_5e-4_rtol_2e-4"),
                    "oracle_max_abs_error": gate.get("max_abs_error"),
                    "oracle_max_relative_error": gate.get("max_relative_error"),
                    "strict_fp32_max_abs_error": strict.get("max_abs_error"),
                    "cached_product_ms": median_ms("cached_product"),
                    "fresh_b_pack_product_ms": median_ms("cached_A_fresh_B_pack_product"),
                    "both_pack_product_ms": median_ms("both_pack_product"),
                    "strict_fp32_product_ms": median_ms("strict_fp32_allocate_output_product"),
                    "reason": row.get("reason")})
            groups.append({"shape_MNK": list(shape), "records": out_records,
                           "accuracy_passed": sum(bool(r["accuracy_gate_passed"]) for r in out_records),
                           "completed": sum(r["status"] == "completed" for r in out_records),
                           "unsupported": sum(r["status"] == "unsupported_tile" for r in out_records),
                           "failed": sum(r["status"] == "failed" for r in out_records)})
    initial_log = root/"tile-sweep.log"
    initial_failure = None
    if initial_log.is_file():
        initial_failure = next((line.strip() for line in initial_log.read_text(errors="replace").splitlines()
                                if "Error" in line or "FileNotFound" in line), None)
    return {"fixed_status": fixed.get("status") if fixed else None,
            "fixed_elapsed_seconds": fixed.get("elapsed_seconds") if fixed else None,
            "fixed_records": groups,
            "initial_status": initial.get("status") if initial else None,
            "initial_record_statuses": [r.get("status") for r in initial.get("records", [])] if initial else [],
            "initial_failure": initial_failure,
            "timing_claim": fixed.get("timing_claim") if fixed else None}


def holdout_admission_totals(point_rows):
    """Aggregate already-deduplicated holdout outcomes, retaining gate scope."""
    out = []
    for variant in sorted({r["variant"] for r in point_rows if r["variant"] == "holdout-verified"}):
        for policy in POLICY_ORDER:
            for gate in ("default_99pct_1e-4_day", "relaxed_90pct_1pct_12h"):
                signal = [r for r in point_rows if r["variant"] == variant and r["policy"] == policy and r["gate"] == gate and r["stratum"] in ("boundary", "target")]
                noise = [r for r in point_rows if r["variant"] == variant and r["policy"] == policy and r["gate"] == gate and r["stratum"] == "noise-only"]
                if signal or noise:
                    out.append({"variant": variant, "policy": policy, "gate": gate,
                                "signal_n": sum(r["trials"] for r in signal),
                                "wrong_accepted": sum(r["wrong_acceptances"] for r in signal),
                                "signal_admitted": sum(r["accepted"] for r in signal),
                                "noise_n": sum(r["trials"] for r in noise),
                                "noise_admitted": sum(r["successes_or_null_admissions"] for r in noise)})
    return out


def scale_diagnostics(root: Path):
    out = []
    for path in sorted(root.glob("scaling-diagnostic*.json")):
        data = json_load(path)
        selector = data.get("raw_mma_selector", {})
        out.append({"file": path.name, "format": data.get("format"),
                    "rows": len(data.get("rows", [])),
                    "mismatches_vs_cpu_fp64_rounded_fp32": data.get("mismatches_vs_cpu_fp64_rounded_fp32"),
                    "raw_copy_bit_mismatches": data.get("raw_copy_bit_mismatches"),
                    "selector_expected": selector.get("represented_fp64"),
                    "selector_raw_fp32": selector.get("raw_fp32"),
                    "selector_backend_restored_fp32": selector.get("backend_restored_fp32"),
                    "scope": data.get("scope")})
    return out


def partial_summaries(root: Path):
    path = root/"partial-accumulation.json"
    if not path.exists():
        return []
    data = json_load(path)
    out = []
    for case in data.get("cases", []):
        if case.get("case") != "random-timing":
            continue
        for method in case.get("methods", []):
            timing = method.get("timing") or {}
            raw = method.get("raw_product_vs_cpu_fp64") or {}
            physical = method.get("physical_product_vs_cpu_fp64") or {}
            out.append({"format": case.get("format"), "shape_mnk": case.get("shape_mnk"),
                        "case": case.get("case"), "method": method.get("method"),
                        "chunk_k": method.get("chunk_k"), "outer_precision": method.get("outer_precision"),
                        "output_precision": method.get("output_precision"),
                        "median_cuda_ms": timing.get("median_cuda_ms"),
                        "median_wall_seconds": timing.get("median_wall_seconds"),
                        "cold_seconds": method.get("cold_seconds"),
                        "raw_relative_l2_error": raw.get("relative_l2_error"),
                        "raw_max_abs_error": raw.get("max_abs_error"),
                        "physical_relative_l2_error": physical.get("relative_l2_error"),
                        "physical_max_abs_error": physical.get("max_abs_error"),
                        "pack_seconds": case.get("pack_seconds"),
                        "native_ptx_verified": method.get("instruction_evidence", {}).get("native_ptx_verified"),
                        "status": method.get("status")})
    return out


def fused_pack_comparisons(probes):
    by_file = {p["file"]: p for p in probes}
    pairs = (("fp16", "probe-fp16.json", "probe-fp16-fused-fixed.json"),
             ("bf16", "probe-bf16.json", "probe-bf16-fused-fixed.json"),
             ("tf32", "probe-tf32.json", "probe-tf32-fused-fixed.json"),
             ("int8", "probe-int8.json", "probe-int8-fused-fixed.json"),
             ("fp8-e4m3", "probe-fp8-e4m3-maskfix.json", "probe-fp8-e4m3-fused-fixed.json"),
             ("fp8-e5m2", "probe-fp8-e5m2-maskfix.json", "probe-fp8-e5m2-fused-fixed.json"))
    out = []
    for fmt, base_name, fused_name in pairs:
        base, fused = by_file.get(base_name), by_file.get(fused_name)
        if not base or not fused:
            continue
        b, f = base.get("both_pack_product_cuda_ms"), fused.get("both_pack_product_cuda_ms")
        out.append({"format": fmt, "baseline_file": base_name, "fused_file": fused_name,
                    "base_both_pack_cuda_ms": b, "fused_both_pack_cuda_ms": f,
                    "both_pack_speedup": b/f if b and f else None,
                    "base_pack_a_cuda_ms": base.get("pack_a_cuda_ms"),
                    "fused_pack_a_cuda_ms": fused.get("pack_a_cuda_ms"),
                    "pack_a_speedup": (base.get("pack_a_cuda_ms")/fused.get("pack_a_cuda_ms")
                                        if base.get("pack_a_cuda_ms") and fused.get("pack_a_cuda_ms") else None),
                    "status": "diagnostic_unqualified_byte_parity_failure" if fmt.startswith("fp8") else "fused_path_screen_A_B_A_checks_passed",
                    "scope": "synthetic matrix pack+product; not modem end-to-end"})
    return out


def fused_pack_byte_parity_failures(root: Path):
    path = root/"tensor-tests-fp8-details.log"
    if not path.exists():
        return []
    found = []
    pattern = re.compile(r'"format":\s*"([^"]+)",\s*"data_byte_mismatch_count":\s*(\d+)')
    for fmt, count in pattern.findall(path.read_text(errors="replace")):
        found.append({"format": fmt, "byte_mismatches": int(count)})
    return found


def plot_probe(probes, out: Path):
    if not probes:
        return
    chosen = [x for x in probes if x.get("cached_product_cuda_ms") is not None]
    if not chosen:
        return
    ref = next((x for x in chosen if x["format"] == "fp32"), None)
    if not ref:
        # Strict reference time is repeated in each probe; use the first.
        strict_ms = chosen[0].get("strict_fp32_product_cuda_ms")
    else:
        strict_ms = ref.get("strict_fp32_product_cuda_ms")
    fig, ax = plt.subplots(figsize=(10, 5.6))
    x = np.arange(len(chosen)); width = .22
    for offset, key, label, color in [(-width, "cached_product_cuda_ms", "Cached operands: product", "#35618f"),
                                      (0, "cached_a_pack_b_product_cuda_ms", "Cached A, pack B + product", "#5b9a8b"),
                                      (width, "both_pack_product_cuda_ms", "Pack both + product", "#c17c3c")]:
        values = [r.get(key) for r in chosen]
        ax.bar(x+offset, values, width, label=label, color=color)
    if strict_ms is not None:
        ax.axhline(strict_ms, color="#333333", linestyle="--", linewidth=1.3,
                   label=f"Strict FP32 product ({strict_ms:.3f} ms)")
    ax.set_xticks(x, [r.get("plot_label", POLICY_LABEL.get(f"tensor:{r['format']}", r["format"])) for r in chosen])
    ax.set_ylabel("CUDA event time (ms)")
    ax.set_title("RTX 4090 synthetic GEMM probe; timings are not waveform throughput")
    ax.grid(axis="y", alpha=.25); ax.legend(frameon=False, ncol=2)
    fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_sensitivity(point_rows, out: Path):
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.3), sharey=True)
    gates = (("default_99pct_1e-4_day", "Default target (99%, 10⁻⁴ / day)"),
             ("relaxed_90pct_1pct_12h", "Relaxed target (90%, 1% / 12 h)"))
    for ax, (gate, title) in zip(axes, gates):
        plotted = False
        variants = sorted({r["variant"] for r in point_rows if r["gate"] == gate and r["stratum"] in ("boundary", "target")})
        markers = ("o", "s", "^", "D", "v", "P")
        for vi, variant in enumerate(variants):
            for policy in POLICY_ORDER:
                subset = [r for r in point_rows if r["variant"] == variant and r["policy"] == policy and r["gate"] == gate and r["stratum"] in ("boundary", "target")]
                subset.sort(key=lambda r: r["cn0_db_hz"])
                if not subset:
                    continue
                plotted = True
                x = np.array([r["cn0_db_hz"] for r in subset])
                y = np.array([r["rate"] for r in subset])
                lo = np.array([r["ci95_low"] for r in subset])
                hi = np.array([r["ci95_high"] for r in subset])
                display = subset[0].get("policy_display", policy_label(policy))
                label = f"{display} ({variant})" if len(variants) > 1 else display
                ax.errorbar(x, y, yerr=np.vstack((y-lo, hi-y)), marker=markers[vi % len(markers)], capsize=3,
                            label=label, color=COLORS.get(policy))
        if not plotted:
            ax.text(.5, .5, "No completed fresh holdout records yet", ha="center", va="center", transform=ax.transAxes)
        ax.set_title(title); ax.set_xlabel("C/N₀ (dB-Hz)"); ax.set_ylim(-.04, 1.04)
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Correctly admitted bit per capture (pointwise 95% Hoeffding interval)")
    if any(ax.lines for ax in axes):
        axes[1].legend(frameon=False, fontsize=8, loc="lower right")
    fig.suptitle("Independent fresh captures with fixed balanced-bit composition; no dB-loss or tail qualification", y=1.02)
    fig.tight_layout(); fig.savefig(out, dpi=180, bbox_inches="tight"); plt.close(fig)


def plot_errors(comparisons, out: Path):
    hold = [r for r in comparisons if r["stage"] == "screening-holdout" and r["cn0_db_hz"] is not None]
    fig, ax = plt.subplots(figsize=(9, 5.5))
    plotted = False
    variants = sorted({r["variant"] for r in hold})
    markers = ("o", "s", "^", "D", "v", "P")
    for vi, variant in enumerate(variants):
        for policy in POLICY_ORDER:
            subset = [r for r in hold if r["variant"] == variant and r["policy"] == policy]
            if not subset:
                continue
            plotted = True
            grouped = {}
            for row in subset:
                grouped.setdefault(float(row["cn0_db_hz"]), []).append(row["max_abs_candidate_score_delta"])
            xs = sorted(grouped)
            ys = [max(grouped[x]) for x in xs]
            display = subset[0].get("policy_display", policy_label(policy))
            label = f"{display} ({variant})" if len(variants) > 1 else display
            ax.plot(xs, ys, marker=markers[vi % len(markers)], label=label, color=COLORS.get(policy))
    if not plotted:
        ax.text(.5, .5, "No paired fresh holdout scores yet", ha="center", va="center", transform=ax.transAxes)
    ax.set_xlabel("C/N₀ (dB-Hz)"); ax.set_ylabel("Maximum absolute candidate-score change vs FP64-correlation reference")
    ax.set_title("Paired score perturbation; inference is shared FP32 via graph-fp64")
    ax.grid(alpha=.25)
    if plotted:
        ax.legend(frameon=False, fontsize=8)
    fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_throughput(runtime, perf, out: Path):
    hold = [r for r in runtime if r["stage"] == "screening-holdout" and r.get("resident_hypotheses_per_s")]
    if not hold:
        hold = [r for r in runtime if r["variant"] == "screen" and r["stage"] == "screen" and r.get("resident_hypotheses_per_s")]
    fig, ax = plt.subplots(figsize=(9, 5.5))
    means = {}
    variants = sorted({r["variant"] for r in hold})
    for variant in variants:
        for policy in POLICY_ORDER:
            vals = [r["resident_hypotheses_per_s"] for r in hold
                    if r["variant"] == variant and r["policy"] == policy]
            if vals:
                means[(variant, policy)] = float(np.median(vals))
    entries = []
    for variant in variants:
        ref = means.get((variant, "fp32"))
        if ref:
            for policy in POLICY_ORDER:
                value = means.get((variant, policy))
                if value:
                    entries.append((variant, policy, value/ref))
    if entries:
        labels = []
        for variant, policy, _ in entries:
            matching = next(r for r in hold if r["variant"] == variant and r["policy"] == policy)
            labels.append(f"{variant}\n{matching.get('policy_display', policy_label(policy))}")
        vals = [ratio for _, _, ratio in entries]
        ax.bar(np.arange(len(labels)), vals, color=[COLORS.get(policy) for _, policy, _ in entries])
        ax.axhline(1, color="#333333", lw=1)
        ax.set_xticks(np.arange(len(labels)), labels, rotation=18, ha="right")
        ax.set_ylabel("Median resident exhaustive hypotheses/s, ratio to strict FP32 in same variant")
    else:
        ax.text(.5, .5, "Awaiting paired performance/holdout timings", ha="center", va="center", transform=ax.transAxes)
    scope = "fresh holdout" if any(r["stage"] == "screening-holdout" for r in hold) else "single development-screen capture"
    ax.set_title(f"Resident full-search throughput ({scope}); resident ratios are not end-to-end/job speedups")
    ax.grid(axis="y", alpha=.25); fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_compute_cost_screening(point_rows, runtime, out: Path, hourly_rate_usd: float):
    """Plot illustrative config-rate cost per observed correct admission.

    The intervals transform pointwise Hoeffding limits on correct reception
    probability. They exclude timing dispersion, capture generation, cloud and
    engineering costs; cost per operationally qualified result is undefined.
    """
    runtime_groups = {}
    for row in runtime:
        if row.get("stage") != "screening-holdout" or row.get("variant") != "holdout-verified":
            continue
        latency = row.get("complete_resident_s")
        cn0 = row.get("cn0_db_hz")
        if latency is None or cn0 is None:
            continue
        key = (row["variant"], row["policy"], float(cn0))
        runtime_groups.setdefault(key, []).append(float(latency))
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), sharey=True)
    gates = (("default_99pct_1e-4_day", "Default gate"),
             ("relaxed_90pct_1pct_12h", "Relaxed gate"))
    for ax, (gate, title) in zip(axes, gates):
        plotted = False
        unbounded_xs = set()
        for policy in POLICY_ORDER:
            subset = [r for r in point_rows if r["variant"] == "holdout-verified" and r["policy"] == policy
                      and r["gate"] == gate and r["stratum"] in ("boundary", "target")]
            subset.sort(key=lambda r: r["cn0_db_hz"])
            points = []
            for row in subset:
                times = runtime_groups.get((row["variant"], policy, float(row["cn0_db_hz"])))
                if not times or row["rate"] is None or row["rate"] <= 0:
                    continue
                latency = float(np.median(times))
                cost_per_capture = latency * hourly_rate_usd / 3600.0
                estimate = cost_per_capture / row["rate"]
                lower = cost_per_capture / row["ci95_high"] if row["ci95_high"] > 0 else 0.
                points.append((row["cn0_db_hz"], estimate, max(0., estimate-lower)))
                if row["ci95_low"] > 0:
                    upper = cost_per_capture / row["ci95_low"]
                    points[-1] += (max(0., upper-estimate),)
                else:
                    points[-1] += (0.,)
                    unbounded_xs.add(row["cn0_db_hz"])
            if not points:
                continue
            plotted = True
            x = np.array([p[0] for p in points]); y = np.array([p[1] for p in points])
            yerr = np.array([[p[2] for p in points], [p[3] for p in points]])
            ax.errorbar(x, y, yerr=yerr, marker="o", capsize=3, label=policy_label(policy), color=COLORS.get(policy))
        if unbounded_xs:
            levels = ", ".join(f"{cn0:g}" for cn0 in sorted(unbounded_xs))
            ax.text(.02, .97, f"No finite upper cost bound at {levels} dB-Hz",
                    transform=ax.transAxes, ha="left", va="top", fontsize=8)
        if not plotted:
            ax.text(.5, .5, "No verified holdout/runtime pairs", ha="center", va="center", transform=ax.transAxes)
        ax.set_title(title); ax.set_xlabel("C/N₀ (dB-Hz)"); ax.grid(alpha=.25)
    axes[0].set_ylabel(f"Illustrative USD per observed correct admission (${hourly_rate_usd:.2f}/resident hour)")
    if any(ax.lines for ax in axes):
        axes[1].legend(frameon=False, fontsize=7)
    fig.suptitle("Illustrative config-rate cost; pointwise Hoeffding interval; upper unbounded when success lower bound is 0\nExcludes generation, provisioning/install and engineering time; operationally qualified-result cost remains undefined", y=1.02)
    fig.tight_layout(); fig.savefig(out, dpi=180, bbox_inches="tight"); plt.close(fig)


def plot_sustained_throughput(rows, out: Path):
    usable = [r for r in rows if r.get("status") == "completed" and r.get("summed_complete_replay_copies_per_s")]
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.2))
    if not usable:
        for ax in axes:
            ax.text(.5, .5, "No completed sustained replay records", ha="center", va="center", transform=ax.transAxes)
        fig.savefig(out, dpi=180); plt.close(fig); return
    labels = [f"{r['policy']}\n{r['repeat_observations']} copy/call" for r in usable]
    batch_ms = [r["batch_latency_s"]["median"]*1000 for r in usable]
    amortized_ms = [r["amortized_service_time_per_copy_s"]["median"]*1000 for r in usable]
    colors = ["#777777" if r["policy"] == "fp32" else "#177e89" for r in usable]
    x = np.arange(len(usable)); width = .34
    axes[0].bar(x-width/2, [r["summed_complete_replay_copies_per_s"] for r in usable], width,
                color=colors, label="Sum of complete replay times")
    axes[0].bar(x+width/2, [r["main_benchmark_per_copy_throughput_per_s"] for r in usable], width,
                color=colors, alpha=.42, hatch="//", label="Main benchmark wall scope")
    axes[0].set_ylabel("Repeated timing copies per second")
    axes[0].set_title("Throughput by timing scope")
    axes[0].legend(frameon=False, fontsize=8)
    axes[1].bar(x-width/2, batch_ms, width, color=colors, label="Observed complete batch-call median")
    axes[1].bar(x+width/2, amortized_ms, width, color=colors, alpha=.42, hatch="//",
                label="Amortized complete service time per copy")
    axes[1].set_ylabel("Complete receiver compute time (ms)")
    axes[1].set_title("Batch latency and normalized service time")
    axes[1].legend(frameon=False, fontsize=8)
    for ax in axes:
        ax.set_xticks(x, labels)
        ax.grid(axis="y", alpha=.25)
    fig.suptitle("One fixed physical capture replayed repeatedly; B8 time is batch latency, not single-capture latency", y=1.02)
    fig.tight_layout(); fig.savefig(out, dpi=180, bbox_inches="tight"); plt.close(fig)


def plot_tile_sweep(summary, out: Path):
    groups = summary.get("fixed_records", [])
    if not groups:
        fig, ax = plt.subplots(figsize=(8, 4)); ax.text(.5, .5, "No completed fixed tile-sweep records", ha="center", va="center", transform=ax.transAxes)
        fig.savefig(out, dpi=180); plt.close(fig); return
    fig, axes = plt.subplots(1, len(groups), figsize=(8*len(groups), 5.5))
    if len(groups) == 1:
        axes = [axes]
    for ax, group in zip(axes, groups):
        valid = [r for r in group["records"] if r["status"] == "completed"]
        x = np.arange(len(valid)); width = .26
        ax.bar(x-width, [r["cached_product_ms"] for r in valid], width,
               label="Cached packed operands: product", color="#35618f")
        ax.bar(x, [r["both_pack_product_ms"] for r in valid], width,
               label="Pack both + product", color="#177e89")
        strict = valid[0]["strict_fp32_product_ms"] if valid else None
        if strict is not None:
            ax.axhline(strict, color="#c44536", linestyle="--", label=f"Strict FP32 product only ({strict:.3f} ms)")
        ticks = ["×".join(str(v) for v in r["tile"][:3])+f"/w{r['tile'][3]}" for r in valid]
        ax.set_xticks(x, ticks, rotation=28, ha="right")
        ax.set_ylabel("Median CUDA-event time (ms)")
        ax.set_title(f"{group['shape_MNK']}; accuracy gate {group['accuracy_passed']}/{group['completed']} passed; unsupported={group['unsupported']}")
        ax.grid(axis="y", alpha=.25); ax.legend(frameon=False, fontsize=7)
    fig.suptitle("Tile microbenchmarks on fixed arrays; FP16 products passed the represented-FP64 oracle; not receiver throughput", y=1.02)
    fig.tight_layout(); fig.savefig(out, dpi=180, bbox_inches="tight"); plt.close(fig)


def plot_workspace_speedup(perf, out: Path):
    fig, ax = plt.subplots(figsize=(9, 5.5))
    valid = [p for p in perf if p.get("workspace_replay_speedup_vs_warm_a") is not None]
    if valid:
        x = np.arange(len(valid))
        labels = [f"{p['variant']}\n{p.get('policy_display', policy_label(p['policy']))}" for p in valid]
        ax.bar(x, [p["workspace_replay_speedup_vs_warm_a"] for p in valid],
               color=[COLORS.get(p["policy"], "#777777") for p in valid])
        ax.axhline(1, color="#333333", linewidth=1)
        ax.axhline(5, color="#9b2226", linestyle="--", linewidth=1.3, label="5× replay-only reference; one-time workspace preparation excluded")
        ax.set_xticks(x, labels, rotation=18, ha="right")
        ax.set_ylabel("Warmed baseline A / median complete workspace replay")
        ax.legend(frameon=False)
    else:
        ax.text(.5, .5, "No workspace performance records yet", ha="center", va="center", transform=ax.transAxes)
    ax.set_title("Fixed-workspace replay throughput; setup and reuse break-even reported separately")
    ax.grid(axis="y", alpha=.25); fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_null_admissions(point_rows, out: Path):
    data = [r for r in point_rows if r["stratum"] == "noise-only"]
    fig, ax = plt.subplots(figsize=(9, 5.5))
    plotted = False
    # Horizon targets are shown as per-attempt union-bound budgets. Pointwise
    # empirical intervals are many orders wider and remain visible on linear axes.
    target = {"default_99pct_1e-4_day": 1e-4/1440,
              "relaxed_90pct_1pct_12h": .01/720}
    for gate, marker, label in (("default_99pct_1e-4_day", "o", "Default per-attempt budget"),
                                ("relaxed_90pct_1pct_12h", "s", "Relaxed per-attempt budget")):
        ax.axhline(target[gate], linestyle="--", linewidth=1.2,
                   label=f"{label} ({target[gate]:.2g})", color="#9b2226" if marker == "o" else "#e9c46a")
    categories = sorted({(r["variant"], r["policy"]) for r in data}, key=lambda pair: (pair[0], POLICY_ORDER.index(pair[1]) if pair[1] in POLICY_ORDER else 99))
    x_for = {category: i for i, category in enumerate(categories)}
    for gate, marker in (("default_99pct_1e-4_day", "o"), ("relaxed_90pct_1pct_12h", "s")):
        for category in categories:
            variant, policy = category
            subset = [r for r in data if r["variant"] == variant and r["policy"] == policy and r["gate"] == gate]
            if not subset:
                continue
            plotted = True
            row = subset[0]
            y = row["rate"]
            lo, hi = row["ci95_low"], row["ci95_high"]
            # Small visual floor only for zero empirical admissions on this
            # linear plot; the exact rate remains 0 in console/report output.
            visual_y = max(y, .003)
            ax.errorbar(x_for[category] + (-.08 if marker == "o" else .08), visual_y,
                        yerr=np.array([[max(0., visual_y-lo)], [max(0., hi-visual_y)]]),
                        fmt=marker, capsize=3, color=COLORS.get(policy),
                        label=f"{subset[0].get('policy_display', policy_label(policy))} ({variant})" if gate == "default_99pct_1e-4_day" else None)
    ax.set_xticks(np.arange(len(categories)), [f"{next(r for r in data if r['variant']==v and r['policy']==p).get('policy_display', policy_label(p))}\n{v}" for v,p in categories], rotation=20, ha="right")
    ax.set_ylim(0, 1); ax.set_ylabel("Noise-only admissions / null captures; pointwise 95% Hoeffding interval")
    ax.set_title("Null capture outcomes do not certify a daily or 12-hour false-admission rate")
    if not plotted:
        ax.text(.5, .5, "No fresh null holdout records yet", ha="center", va="center", transform=ax.transAxes)
    ax.grid(axis="y", alpha=.25)
    if plotted:
        ax.legend(frameon=False, fontsize=7, ncol=2)
    fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_transfers(rows, out: Path):
    fig, ax = plt.subplots(figsize=(9, 5.5))
    if rows:
        labels = [f"{r['size_mib']} MiB {r['direction'].upper()}\n{r['host_memory_kind']}" for r in rows]
        vals = [r["event_mean_GiB_per_second"] for r in rows]
        colors = ["#35618f" if r["host_memory_kind"] == "pageable" else "#2a9d8f" for r in rows]
        ax.bar(np.arange(len(rows)), vals, color=colors)
        ax.set_xticks(np.arange(len(rows)), labels, rotation=18, ha="right")
        ax.set_ylabel("Transfer-only mean GiB/s (CUDA event)")
    else:
        ax.text(.5, .5, "No hardware transfer record", ha="center", va="center", transform=ax.transAxes)
    ax.set_title("RTX 4090 transfer probe with persistent buffers; excludes modem pipeline and host staging")
    ax.grid(axis="y", alpha=.25); fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_pack_path_comparison(pairs, probes, out: Path):
    if not pairs:
        return
    fig, ax = plt.subplots(figsize=(11, 6))
    x = np.arange(len(pairs)); width = .23
    base = [p.get("base_both_pack_cuda_ms") for p in pairs]
    fused = [p.get("fused_both_pack_cuda_ms") for p in pairs]
    by_file = {p["file"]: p for p in probes}
    strict = []
    # Strict-FP32 product time for each fused-format probe is used as a
    # separate product-only reference. It excludes packing.
    for p in pairs:
        f = by_file.get(p["fused_file"])
        strict.append(f.get("strict_fp32_product_cuda_ms") if f else None)
    bars0 = ax.bar(x-width, base, width, label="Reference pack path + product", color="#777777")
    bars1 = ax.bar(x, fused, width, label="Fused-pack path + product", color="#177e89")
    bars2 = ax.bar(x+width, strict, width, label="Strict FP32 product only", color="#e9c46a")
    for i, p in enumerate(pairs):
        if p["status"] != "fused_path_screen_A_B_A_checks_passed":
            for bars in (bars0, bars1):
                bars[i].set_hatch("///"); bars[i].set_edgecolor("#9b2226")
                bars[i].set_alpha(.55)
    labels = [p["format"] + ("\n(diagnostic: byte mismatch)" if "unqualified" in p["status"] else "\n(A/B/A parity passed)") for p in pairs]
    ax.set_xticks(x, labels); ax.set_ylabel("CUDA event time (ms)")
    ax.set_title("Fused packing shortens synthetic preparation; FP8 results remain diagnostic")
    ax.grid(axis="y", alpha=.25); ax.legend(frameon=False, ncol=3)
    fig.tight_layout(); fig.savefig(out, dpi=180); plt.close(fig)


def plot_partial(partial, out: Path):
    rows = [r for r in partial if r["format"].startswith("fp8") and r.get("median_cuda_ms") and r.get("raw_relative_l2_error")]
    if not rows:
        fig, ax = plt.subplots(figsize=(8, 4)); ax.text(.5, .5, "No FP8 partial-product records", ha="center", va="center", transform=ax.transAxes)
        fig.savefig(out, dpi=180); plt.close(fig); return
    formats = sorted(set(r["format"] for r in rows))
    fig, axes = plt.subplots(1, len(formats), figsize=(6*len(formats), 5.2), sharey=True)
    if len(formats) == 1:
        axes = [axes]
    markers = {"long-chain": "o", "partial-32-outer-fp32": "s", "partial-32-outer-fp64": "X",
               "partial-128-outer-fp32": "^", "partial-128-outer-fp64": "P",
               "partial-256-outer-fp32": "D", "partial-256-outer-fp64": "v"}
    for ax, fmt in zip(axes, formats):
        subset = [r for r in rows if r["format"] == fmt]
        for r in subset:
            method = r["method"]
            ax.scatter(r["raw_relative_l2_error"], r["median_cuda_ms"],
                       marker=markers.get(method, "o"), s=72,
                       label=method.replace("partial-", "K=").replace("-outer-", ", outer="),
                       color="#177e89" if "fp32" in method or method == "long-chain" else "#c44536")
        ax.set_xscale("log"); ax.set_title(fmt)
        ax.set_xlabel("Raw normalized product relative L2 error vs CPU FP64")
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Kernel CUDA event median (ms)")
    axes[-1].legend(frameon=False, fontsize=7, loc="best")
    fig.suptitle("Partial accumulation on actual packed operands; synthetic diagnostic, not reception evidence", y=1.02)
    fig.tight_layout(); fig.savefig(out, dpi=180, bbox_inches="tight"); plt.close(fig)


def print_probe_table(probes):
    if not probes:
        print("No tensor probes found.")
        return
    print("\nPROBE CUDA-event medians (synthetic GEMM, not waveform throughput)")
    print("file format cold_s packA_ms packB_ms cached_ms cachedA+packB_ms fullpack_ms strictFP32_ms speedup_cached strict_err")
    for p in probes:
        print("{file} {format} {cold_seconds:.4f} {pack_a_cuda_ms!s} {pack_b_cuda_ms!s} {cached_product_cuda_ms!s} {cached_a_pack_b_product_cuda_ms!s} {both_pack_product_cuda_ms!s} {strict_fp32_product_cuda_ms!s} {cached_product_speedup_vs_strict_fp32!s} {oracle_max_abs_error!s}".format(**p))
        small = (p.get("paired_numerical_cases") or {}).get("within_row_small_selected_products", {}).get("isolated_small_products", {})
        if small:
            print(f"  isolated small selected products: representation-zeroed={small.get('representation_zeroed_nonzero_products')} arithmetic-zeroed-represented={small.get('arithmetic_zeroed_nonzero_represented_products')} fp32-reference-zeroed={small.get('arithmetic_zeroed_nonzero_fp32_representable_products')} exact-fp32-rounding={small.get('exact_fp32_rounding_pass')}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="RTX 4090 artifact directory")
    parser.add_argument("--plot-dir", type=Path, default=Path(__file__).with_name("plots"))
    parser.add_argument("--summary-json", type=Path,
                        default=Path(__file__).with_name("analysis")/"summary.json",
                        help="write machine-readable derived summaries")
    parser.add_argument("--hourly-rate-usd", type=float, default=.90,
                        help="illustrative public configuration rate for cost screening; defaults to $0.90/hour")
    args = parser.parse_args(argv)
    root = args.root.resolve(); out = args.plot_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    probes = probe_summaries(root)
    rows, indexes = campaign_rows(root)
    comparisons = paired_numerics(rows)
    points, point_exclusions = decision_summaries(rows)
    runtime = runtime_summaries(rows)
    perf = performance_summaries(rows)
    failures = failed_summaries(indexes)
    hardware, transfers = hardware_summary(root)
    gpu_profiles = profile_kernel_summaries(root)
    scales = scale_diagnostics(root)
    partial = partial_summaries(root)
    pack_pairs = fused_pack_comparisons(probes)
    parity_failures = fused_pack_byte_parity_failures(root)
    sustained = sustained_summaries(root)
    tiles = tile_sweep_summary(root)
    admission_totals = holdout_admission_totals(points)
    print_probe_table(probes)
    print("\nCAMPAIGN INDEXES")
    for idx in indexes:
        print(f"{Path(idx['path']).parent.name}/{idx['stage']}: {idx['status']}; records={idx['record_count']} complete={idx['completed']} failed={idx['failed']} elapsed={idx['elapsed_seconds']}")
    print("\nFAILURES (retained as failures)")
    for item in failures:
        err = str(item.get("error") or item.get("stderr_tail") or "failed")
        print(f"{item['stage']} {item.get('policy')}: {err.splitlines()[0][:220]} (full original failure retained)")
    if hardware:
        print(f"\nHARDWARE: {hardware['gpu']} CC{hardware['compute_capability']}; host={hardware['cpu']}; quota≈{hardware['cpu_quota_cores']} cores; GPU telemetry={hardware['telemetry']}")
        print("transfer probe (persistent allocations; event timing excludes CPU staging)")
        for r in transfers:
            print(f"{r['size_mib']}MiB {r['direction']} {r['host_memory_kind']}: event={r['event_median_seconds']:.6f}s / {r['event_mean_GiB_per_second']:.2f}GiB/s; wall={r['wall_median_seconds']:.6f}s / {r['wall_mean_GiB_per_second']:.2f}GiB/s")
    if gpu_profiles:
        print("\nPROFILE GPU KERNEL CATEGORY ONLY (sanitized Chrome trace; device event durations, repeats/profile overhead; not end-to-end timings)")
        for p in gpu_profiles:
            print(f"{p['mode']}: {p['kernel_events']} kernel events across {p['repeats']} profiled repeats, summed device duration={p['kernel_duration_sum_ms']:.3f}ms; device kernel union={p['device_kernel_union_ms']:.3f}ms across {p['device_first_to_last_kernel_span_ms']:.3f}ms first-to-last span; kernel-free event-gap union={p['device_kernel_free_gaps_union_ms']:.3f}ms")
            print(f"  phase-known stencil={p['stencil']['count']} / {p['stencil']['duration_ms']:.3f}ms; phase-frequency SGEMM={p['phase_frequency_sgemm']['count']} / {p['phase_frequency_sgemm']['duration_ms']:.3f}ms; phase sum kernels={p['sum_kernels']['count']} / {p['sum_kernels']['duration_ms']:.3f}ms")
            print(f"  {p['product_scope']}={p['correlation_product']['count']} / {p['correlation_product']['duration_ms']:.3f}ms; pack={p['packing']['count']} / {p['packing']['duration_ms']:.3f}ms; conditional product+pack-only kernel ceiling={p['product_plus_pack_kernel_only_amdahl_ceiling']:.4f}x")
            print(f"  visible host event span/union/gap={p['host_visible_event_span_ms']:.1f}/{p['host_visible_event_union_ms']:.1f}/{p['host_visible_event_gap_union_ms']:.1f}ms; largest internal no-event gap={p['host_visible_max_gap_ms']:.1f}ms (trace gaps are not a CPU-idle counter)")
            for k in p["top_kernel_names"][:6]:
                print(f"    top: {k['count']} × {k['duration_ms']:.3f}ms ({k['share_percent']:.1f}%): {k['name'][:115]}")
            if p["limitations"]:
                print("  profile limitations: " + "; ".join(p["limitations"]))
    print("\nSCALING/RESTORATION DIAGNOSTICS (not policy selection)")
    for r in scales:
        print(f"{r['file']} {r['format']} rows={r['rows']} mismatch counts={r['mismatches_vs_cpu_fp64_rounded_fp32']} raw-copy-bit-mismatches={r['raw_copy_bit_mismatches']} selector expected={r['selector_expected']} rawFP32={r['selector_raw_fp32']} restored={r['selector_backend_restored_fp32']}")
    print("\nFUSED-PACK synthetic probe: full pack+product vs reference path; FP8 is byte-parity-unqualified")
    for r in pack_pairs:
        print(f"{r['format']}: reference={r['base_both_pack_cuda_ms']:.4f}ms fused={r['fused_both_pack_cuda_ms']:.4f}ms speedup={r['both_pack_speedup']:.2f}x; A-pack speedup={r['pack_a_speedup']:.2f}x; {r['status']}")
    if parity_failures:
        print("FP8 fused-pack byte parity failures retained from test log: " + ", ".join(f"{x['format']}={x['byte_mismatches']} differing byte(s)" for x in parity_failures))
    print("\nPAIRED HOLDOUT: by artifact variant and policy; inputs are not pooled across variants")
    for policy in POLICY_ORDER:
        variants = sorted({r["variant"] for r in comparisons if r["stage"] == "screening-holdout" and r["policy"] == policy})
        for variant in variants:
            subset = [r for r in comparisons if r["stage"] == "screening-holdout" and r["variant"] == variant and r["policy"] == policy]
            if subset:
                changed_gates = sum(any(bool(fields) for fields in x["decision_differences"].values()) for x in subset)
                admission_changes = sum(any(("admitted" in fields or "noise_admitted" in fields) for fields in x["decision_differences"].values()) for x in subset)
                classification_changes = sum(any(("correct_accepted" in fields or "wrong_accepted" in fields) for fields in x["decision_differences"].values()) for x in subset)
                print(f"{variant}/{policy_label(policy)}: n={len(subset)} maxΔ={max(x['max_abs_candidate_score_delta'] for x in subset):.6g}; rank-cell changes={sum(x['candidate_winner_changes_by_capture_cfo_lag_cell'] for x in subset)}; changed gates={changed_gates}; admission-flag changes={admission_changes}; acceptance-classification changes={classification_changes}")
    print("\nPAIRED DEVELOPMENT SCREEN (single capture; not holdout)")
    for row in comparisons:
        if row["stage"] == "screen":
            print(f"{row['variant']}/{row['policy_display']} {row['input']}: max abs candidate score Δ={row['max_abs_candidate_score_delta']:.6g}; max abs bit score Δ={row['max_abs_bit_score_delta']}; candidate rank-cell changes={row['candidate_winner_changes_by_capture_cfo_lag_cell']}/{row['candidate_winner_cells']}; bit-winner changes={row['bit_winner_changes']}; decisions={row['decision_differences']}")
    print(f"\nHOLDOUT POINTS: variant, stratum, C/N0, gate, successes/admissions, n, two-sided 95% Hoeffding interval; exclusions={point_exclusions}")
    for p in points:
        k, n = p["successes_or_null_admissions"], p["trials"]
        print(f"{p['variant']}/{p['policy_display']} {p['stratum']} {p['cn0_db_hz']:g} {p['gate']}: {k}/{n} [{p['ci95_low']:.4g},{p['ci95_high']:.4g}]")
    print("\nHOLDOUT AGGREGATE ADMISSIONS (holdout-verified only; failed final rerun excluded; gate rows are separate) ")
    for r in admission_totals:
        print(f"{r['variant']}/{policy_label(r['policy'])}/{r['gate']}: wrong accepted={r['wrong_accepted']}/{r['signal_n']} signal; signal admitted={r['signal_admitted']}/{r['signal_n']}; noise-only admitted={r['noise_admitted']}/{r['noise_n']}")
    print("\nRUNTIME: full resident capture timings; device-event columns are null unless explicitly instrumented")
    for r in runtime:
        print(f"{r['variant']}/{r['stage']} {r['input']} {r.get('policy_display', r['policy'])}: resident={r['complete_resident_s']}s; hypotheses/s={r['resident_hypotheses_per_s']}; driver-call={r['driver_call_wall_seconds']}s; process={r['within_process_s']}s; stage-wall-after-imports={r['campaign_wall_after_imports_s']}s; correlation-event={r['correlation_device_event_s']}; bank-matmul-accounted={r['bank_matmul_seconds_accounted']}")
    print("\nPERFORMANCE A/B/A workspace records")
    for r in perf:
        print(f"{r['variant']}/{r.get('policy_display', r['policy'])} {r['input']}: prep={r['preparation_seconds']:.4f}s; baseline coldA={r['baseline_cold_a_seconds']:.4f}s, B={r['baseline_b_seconds']:.4f}s, warmedA={r['baseline_warm_a_seconds']:.4f}s; workspace median complete={r['workspace_replay_median_complete_seconds']:.4f}s, device={r['workspace_replay_median_device_seconds']:.4f}s; speedup={r['workspace_replay_speedup_vs_warm_a']:.2f}x; prep break-even≈{r['setup_break_even_replays_estimate']:.1f} replays; A/B/A-bit-identical={r['a_after_b_bit_identical']}; B={r['second_observation_kind']}")
    print("\nSUSTAINED FIXED-CAPTURE REPLAY (same sampled input repeated; timing copies are not independent trials)")
    for r in sustained:
        if r.get("status") != "completed" or r.get("returncode") != 0 or r.get("timed_out"):
            print(f"{r['name']}: status={r['status']} returncode={r['returncode']} timed_out={r.get('timed_out')}; excluded from throughput")
            continue
        tel = r.get("gpu_telemetry", {})
        def med(category, field):
            d = tel.get(category)
            return f"{d[field]:.1f}" if d and d.get(field) is not None else "n/a"
        pc = r.get("amortized_service_time_per_copy_s") or {}
        dev = r.get("amortized_graph_device_time_per_copy_s") or {}
        batch = r.get("batch_latency_s") or {}
        print(f"{r['name']} policy={r['policy']} status=completed: {r['timing_replays_count']} timed batches, {r['repeat_count']}×{r['repeat_observations']}={r['repeat_count']*r['repeat_observations']} timing copies; sum complete receiver time={r['summed_complete_replay_seconds']:.3f}s => {r['summed_complete_replay_copies_per_s']:.4f} copies/s ({1000*r['summed_complete_replay_amortized_seconds_per_copy']:.3f}ms amortized/copy); median observed batch-call={1000*batch['median']:.3f}ms; median amortized service/copy={1000*pc['median']:.3f}ms, graph device/copy={1000*dev['median']:.3f}ms; median-batch normalized rate={r['batch_throughput_per_s']:.4f} copies/s; main benchmark wall={r['main_benchmark_seconds']:.3f}s => {r['main_benchmark_per_copy_throughput_per_s']:.2f} timing copies/s")
        print(f"  main-benchmark scope includes file load/hash, baseline A/B/A, workspace preparation and workspace A/B/A plus repeat loop; excludes startup, export and serialization. Workspace prep={r['fixed_workspace_preparation_s']:.3f}s; warm-A complete={r['baseline_warm_a_s']:.3f}s/batch; warm-A to workspace median-batch speedup={r['workspace_median_speedup_vs_warm_a']:.2f}x")
        print(f"  telemetry {r['telemetry_sampling_interval_s']}s polls: GPU util mean/median={med('gpu_util_pct','mean')}/{med('gpu_util_pct','median')}%; memory-util mean/median={med('memory_util_pct','mean')}/{med('memory_util_pct','median')}%; used median={med('memory_used_mib','median')}MiB; power median={med('power_w','median')}W; cgroup CPU={r['average_cpu_cores_used']:.2f}/{r['cpu_quota_cores']:.1f} cores ({r['cpu_scope']})")
    if len(sustained) >= 3:
        byname = {r["name"]: r for r in sustained}
        a, b, c = (byname.get(n) for n in ("sustain-fp32-b1", "sustain-fp16-b1", "sustain-fp16-b8"))
        if a and b and c and all(x.get("summed_complete_replay_copies_per_s") for x in (a,b,c)):
            print(f"  summed-replay throughput: FP32 B1={a['summed_complete_replay_copies_per_s']:.4f}, FP16 B1={b['summed_complete_replay_copies_per_s']:.4f}, FP16 B8={c['summed_complete_replay_copies_per_s']:.4f} timing copies/s; B8/B1={c['summed_complete_replay_copies_per_s']/b['summed_complete_replay_copies_per_s']:.3f}×, FP16 B1/FP32 B1={b['summed_complete_replay_copies_per_s']/a['summed_complete_replay_copies_per_s']:.3f}×. Main-benchmark normalized rates include file-load/hash and baseline/validation work. None are independent acquisition throughput.")
    print("  Telemetry is one-second nvidia-smi polling and can miss transients; GPU-util is not tensor-core occupancy and memory-util is not DRAM bandwidth. CPU cgroup includes telemetry sampling. Complete receiver replay time includes CPU FP64 posterior finish; sustained replay excludes physical capture acquisition and real-stream behavior. For B8, batch-call latency is the observed latency; amortized time/copy is not single-capture latency.")
    print("\nTILE SWEEP (microbenchmark only; not an operational sensitivity or receiver-throughput claim)")
    print(f"fixed status={tiles['fixed_status']} elapsed={tiles['fixed_elapsed_seconds']}s; initial sweep status={tiles['initial_status']}; initial log failure={tiles['initial_failure']}")
    for group in tiles["fixed_records"]:
        valid = [x for x in group["records"] if x["status"] == "completed"]
        print(f"shape M,N,K={group['shape_MNK']}: accuracy-gated={group['accuracy_passed']}/{group['completed']} completed; unsupported={group['unsupported']}; failed={group['failed']}")
        for timing_key, label in (("cached_product_ms", "cached product"), ("both_pack_product_ms", "pack-both+product"), ("strict_fp32_product_ms", "strict-FP32-product-only")):
            available = [x for x in valid if x.get(timing_key) is not None]
            best = min(available, key=lambda x: x[timing_key]) if available else None
            if best:
                print(f"  fastest {label}: {best[timing_key]:.4f}ms tile={best['tile']}; gate={best['accuracy_gate_passed'] if label != 'strict-FP32-product-only' else 'separate reference'}")
        print("  per-tile event ms: " + "; ".join(f"{x['tile']} product={x['cached_product_ms']:.4f}, pack+product={x['both_pack_product_ms']:.4f}" for x in valid if x.get("cached_product_ms") is not None and x.get("both_pack_product_ms") is not None))
    print("  Oracle checks compare actual packed FP16 operands against CPU FP64 products on sampled 8×8 outputs; strict-FP32 timing is product-only. Unsupported BM/BN>128 configurations are retained, not counted as accuracy passes.")
    print("\nFP8 partial-accumulation synthetic random-timing products")
    for fmt in ("fp8-e4m3", "fp8-e5m2"):
        subset = [x for x in partial if x["format"] == fmt]
        modes = {x["method"]: x for x in subset}
        full, k32 = modes.get("long-chain"), modes.get("partial-32-outer-fp32")
        if full and k32:
            improvement = full["raw_relative_l2_error"]/k32["raw_relative_l2_error"]
            print(f"{fmt}: full-chain {full['median_cuda_ms']:.4f}ms, error={full['raw_relative_l2_error']:.6g}; K32 FP32 outer {k32['median_cuda_ms']:.4f}ms, error={k32['raw_relative_l2_error']:.6g}, error reduction={improvement:.1f}x")
        for k in (32, 128, 256):
            fp32 = modes.get(f"partial-{k}-outer-fp32")
            fp64 = modes.get(f"partial-{k}-outer-fp64")
            if fp32:
                print(f"  K={k} outer FP32: {fp32['median_cuda_ms']:.4f}ms, raw L2={fp32['raw_relative_l2_error']:.6g}")
            if fp64 and fp32:
                print(f"  K={k} outer FP64: {fp64['median_cuda_ms']:.4f}ms, raw L2={fp64['raw_relative_l2_error']:.6g}, slowdown={fp64['median_cuda_ms']/fp32['median_cuda_ms']:.2f}x vs FP32 outer")
    plot_probe(probes, out/"probe-products.png")
    plot_sensitivity(points, out/"holdout-detection.png")
    plot_errors(comparisons, out/"paired-score-error.png")
    plot_throughput(runtime, perf, out/"resident-throughput.png")
    plot_compute_cost_screening(points, runtime, out/"illustrative-cost-per-observed-correct.png", args.hourly_rate_usd)
    plot_workspace_speedup(perf, out/"workspace-replay-speedup.png")
    plot_null_admissions(points, out/"null-admissions.png")
    plot_transfers(transfers, out/"transfer-probe.png")
    plot_pack_path_comparison(pack_pairs, probes, out/"fused-pack-paths.png")
    plot_partial(partial, out/"partial-accumulation.png")
    plot_sustained_throughput(sustained, out/"sustained-replay-throughput.png")
    plot_tile_sweep(tiles, out/"tile-sweep-microbenchmarks.png")
    args.summary_json.parent.mkdir(parents=True, exist_ok=True)
    machine_sustained = [{k: v for k, v in row.items() if k != "receiver"} for row in sustained]
    summary = {"source_root": str(root), "hourly_rate_usd_assumption": args.hourly_rate_usd,
               "indexes": indexes, "failures": failures, "holdout_pointwise": points,
               "holdout_admission_totals": admission_totals, "paired_numerics": comparisons,
               "runtime": runtime, "performance_ab_a": perf, "sustained_replay": machine_sustained,
               "tile_sweep": tiles, "gpu_profiles": gpu_profiles,
               "interpretation_limits": ["timing replays are not independent receptions",
                   "Hoeffding intervals are pointwise for independent bounded captures at fixed balanced-bit composition",
                   "holdout false-admission counts do not qualify operational time horizons",
                   "no 0.1, 0.25, 0.5, or 1 dB loss budget is established",
                   "cost per operationally qualified result is undefined"]}
    args.summary_json.write_text(json.dumps(summary, indent=2, sort_keys=True)+"\n")
    print(f"\nPlots written to {out}")
    print(f"Machine-readable summary written to {args.summary_json.resolve()}")
    print(f"Illustrative cost rate: ${args.hourly_rate_usd:.2f}/hour; the resident per-input time includes receiver/workspace preparation; excludes capture generation, provisioning/install and engineering time. Operationally qualified-result cost is undefined because no policy meets the reception/admission qualification here.")
    print("Interpretation: the 5× target must be assessed from resident/full-job throughput; synthetic GEMM timings do not establish it. Saved capture score arrays are final candidate likelihood scores, not raw complex correlations; correlation-stage timing exists but paired correlation values do not. Holdout correctness is the correctly admitted bit/capture outcome; timing and epoch synchronization are not separate scored endpoints here. Hoeffding intervals are pointwise over independent bounded captures with fixed balanced-bit composition. Neither 128 inputs nor 32 nulls qualify operational horizon-level false-admission targets. No 0.1/0.25/0.5/1 dB sensitivity-loss budget is established by these plots.")


if __name__ == "__main__":
    main()

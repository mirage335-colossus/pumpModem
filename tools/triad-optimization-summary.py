#!/usr/bin/env python3
"""Summarize fixed-workload GPU optimization and sampled precision JSON.

The report preserves evidence scope: screening timing is never promoted to
calibrated reception or an operational false-admission result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

import numpy as np


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def wilson(successes: int, trials: int, z: float = 1.959963984540054) -> list[float] | None:
    if trials < 1:
        return None
    p = successes / trials
    den = 1 + z*z/trials
    center = (p + z*z/(2*trials))/den
    half = z*math.sqrt(p*(1-p)/trials + z*z/(4*trials*trials))/den
    return [max(0., center-half), min(1., center+half)]


def files_in(inputs: list[Path]) -> list[Path]:
    found: set[Path] = set()
    for root in inputs:
        if root.is_file():
            if root.suffix == ".json":
                found.add(root.resolve())
            continue
        found.update(p.resolve() for p in root.rglob("*.json")
                     if ".git" not in p.parts)
    return sorted(found)


def _safe_float(value):
    if value is None:
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def read_evidence(inputs: list[Path]):
    loaded = []
    rejected = []
    seen_hashes: dict[str, Path] = {}
    for path in files_in(inputs):
        try:
            data = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            rejected.append({"path": str(path), "reason": f"unreadable JSON: {exc}"})
            continue
        digest = sha256(path)
        if digest in seen_hashes:
            rejected.append({"path": str(path), "reason": "duplicate content hash; already included once",
                             "duplicate_of": str(seen_hashes[digest]), "sha256": digest})
            continue
        seen_hashes[digest] = path
        if data.get("schema") == "triad-gpu-optimization-report-v1":
            loaded.append((path, digest, data, "gpu"))
        elif data.get("schema") == "triad-sampled-precision-probe-v1":
            loaded.append((path, digest, data, "precision"))
        elif data.get("status") in ("failed", "unsupported"):
            rejected.append({"path": str(path), "reason": "unsupported or failed unrelated schema", "status": data.get("status")})
    return loaded, rejected


def input_identity(data: dict) -> str | None:
    return data.get("provenance", {}).get("input_manifest_sha256")


def full_input_identity(data: dict) -> tuple:
    files = data.get("provenance", {}).get("input_files", {})
    return tuple(sorted((name, value.get("sha256"), value.get("bytes"))
                        for name, value in files.items()))


def _freeze(value):
    if isinstance(value, dict):
        return tuple(sorted((key, _freeze(item)) for key, item in value.items()))
    if isinstance(value, (list, tuple)):
        return tuple(_freeze(item) for item in value)
    return value


def source_contract(data: dict) -> tuple:
    """Common numerical sources, excluding the selected phase backend file."""
    hashes = data.get("provenance", {}).get("source_sha256", {})
    return tuple(sorted((Path(name).name, digest) for name, digest in hashes.items()
                        if Path(name).name != "triad_phase_optimized.py"))


def selected_backend_source(data: dict) -> str | None:
    hashes = data.get("provenance", {}).get("source_sha256", {})
    return next((digest for name, digest in hashes.items()
                 if Path(name).name == "triad_phase_optimized.py"), None)


def model_identity(data: dict) -> tuple:
    c, w, f = (data.get("configuration", {}), data.get("workload", {}),
               data.get("threshold_family", {}))
    config = tuple((key, _freeze(c.get(key))) for key in (
        "split", "seed", "cn0", "max_blocks", "batch", "trials", "taps",
        "phase_states", "relative_phase_states", "frequency_bound_hz", "nominal_dt",
        "frequency_std_hz", "frequency_correlation_seconds", "phase_diffusion",
        "duration_alternatives", "selection_methods", "actual_observation",
        "observation_preparation"))
    workload = tuple((key, _freeze(w.get(key))) for key in (
        "phase_states", "frequency_states", "gain_quadrature_states", "taps",
        "scored_blocks", "candidate_count_scored", "candidate_labels", "candidate_mappings",
        "physical_observation_seconds", "sample_rate_hz", "source_sample_count",
        "actual_noisy_projection", "gaussian_projection_supported"))
    family = tuple((key, _freeze(f.get(key))) for key in (
        "attempt_spacing_seconds", "duration_alternatives", "receiver_selection_methods", "bit_hypotheses"))
    return config, workload, family


def environment_identity(data: dict) -> tuple:
    e = data.get("environment", {})
    props = e.get("gpu_properties", {}) or {}
    return tuple((key, _freeze(e.get(key))) for key in (
        "device_name", "torch", "cuda_version", "numpy", "python", "tf32_allowed",
        "float32_matmul_precision")) + tuple(("gpu_"+key, _freeze(props.get(key))) for key in (
            "compute_capability", "multiprocessor_count"))


def timing_report(loaded, rate: float):
    rows = [item for item in loaded if item[3] == "gpu"]
    usable = []
    excluded = []
    for path, digest, d, _ in rows:
        t = d.get("timing_seconds", {})
        cfg = d.get("configuration", {})
        if t.get("timing_protocol_version") != 2:
            excluded.append({"path": str(path), "sha256": digest,
                             "reason": "timing protocol is absent or not version 2; preserved as numerical/screening evidence, excluded from throughput aggregation"})
            continue
        if d.get("status") != "screening_only":
            excluded.append({"path": str(path), "sha256": digest, "reason": "result is not marked screening_only"})
            continue
        if not full_input_identity(d) or not source_contract(d):
            excluded.append({"path": str(path), "sha256": digest,
                             "reason": "source hashes or per-file input hashes are absent; cannot bind throughput to exact source and input content"})
            continue
        work = t.get("candidate_scoring_work", {})
        usable.append({"path": path, "sha256": digest, "data": d, "batch": cfg.get("batch"),
                       "backend": cfg.get("backend"), "precision": cfg.get("precision"),
                       "seed": cfg.get("seed"), "input": input_identity(d), "work": work, "timing": t,
                       "source_contract": source_contract(d), "selected_backend_source": selected_backend_source(d),
                       "model_identity": model_identity(d), "environment_identity": environment_identity(d)})

    groups = {}
    for row in usable:
        key = (row["input"], full_input_identity(row["data"]), row["model_identity"],
               row["source_contract"], row["selected_backend_source"], row["environment_identity"],
               row["batch"], row["backend"], row["precision"],
               row["timing"].get("timing_protocol_version"))
        g = groups.setdefault(key, {"input_manifest_sha256": key[0], "input_file_hashes": key[1],
                                    "model_identity": key[2], "common_source_contract": key[3],
                                    "selected_backend_source_sha256": key[4], "environment_identity": key[5],
                                    "batch": key[6], "backend": key[7], "precision": key[8],
                                    "timing_protocol_version": key[9], "runs": []})
        g["runs"].append(row)
    grouped = []
    for g in groups.values():
        source = g["runs"]
        def med(path):
            vals = [_safe_float(path(r)) for r in source]
            vals = [v for v in vals if v is not None and v >= 0]
            return ({"median_seconds": statistics.median(vals), "min_seconds": min(vals),
                     "max_seconds": max(vals), "n": len(vals)} if vals else None)
        resident_pipeline = med(lambda r: r["work"].get("resident_warm_pipeline_seconds"))
        signal = med(lambda r: r["timing"].get("signal_pretransferred_resident_inference", {}).get("synchronized_wall_seconds"))
        null = med(lambda r: r["timing"].get("null_pretransferred_resident_inference", {}).get("synchronized_wall_seconds"))
        event = med(lambda r: (r["timing"].get("signal_pretransferred_resident_inference", {}).get("cuda_event_seconds") or 0) +
                              (r["timing"].get("null_pretransferred_resident_inference", {}).get("cuda_event_seconds") or 0))
        streamed_pipeline = med(lambda r: r["work"].get("streamed_warm_pipeline_seconds"))
        full = med(lambda r: r["timing"].get("complete_job_wall_seconds"))
        streamed_evals = med(lambda r: (r["work"].get("candidate_block_score_evaluations") /
                                        r["work"].get("streamed_warm_pipeline_seconds")
                                        if r["work"].get("candidate_block_score_evaluations") is not None and
                                           r["work"].get("streamed_warm_pipeline_seconds") else None))
        resident_evals = med(lambda r: (r["work"].get("candidate_block_score_evaluations") /
                                        r["work"].get("resident_warm_pipeline_seconds")
                                        if r["work"].get("candidate_block_score_evaluations") is not None and
                                           r["work"].get("resident_warm_pipeline_seconds") else None))
        def combined_stage(run, names):
            values = [_safe_float(run["timing"].get(name)) for name in names]
            values = [v for v in values if v is not None]
            return sum(values) if values else None
        def transfer_stage(run, names):
            values = [_safe_float(run["timing"].get(name, {}).get("synchronized_wall_seconds"))
                      for name in names]
            values = [v for v in values if v is not None]
            return sum(values) if values else None
        def inference_stage(run, names):
            values = [_safe_float(run["timing"].get(name, {}).get("synchronized_wall_seconds"))
                      for name in names]
            values = [v for v in values if v is not None]
            return sum(values) if values else None
        stage_getters = {
            "workspace_setup_seconds": lambda r: combined_stage(r, ["workspace_setup_schema_and_file_validation"]),
            "public_design_validation_seconds": lambda r: combined_stage(r, ["public_gram_interval_psd_validation"]),
            "shared_observation_preparation_seconds": lambda r: combined_stage(r, ["shared_paired_observation_preparation_seconds"]),
            "signal_and_null_rng_or_sample_iteration_seconds": lambda r: combined_stage(r, ["signal_statistics_noise_rng_generation", "null_statistics_noise_rng_generation", "signal_actual_observation_iteration"]),
            "host_to_device_transfer_seconds": lambda r: transfer_stage(r, ["signal_host_to_device", "null_host_to_device"]),
            "receiver_construction_seconds": lambda r: combined_stage(r, ["signal_receiver_construction", "null_receiver_construction", "signal_streamed_receiver_construction_and_transition_cache", "null_streamed_receiver_construction_and_transition_cache"]),
            "transition_cache_preparation_seconds": lambda r: combined_stage(r, ["signal_transition_cache_preparation", "null_transition_cache_preparation", "signal_streamed_transition_cache_preparation", "null_streamed_transition_cache_preparation"]),
            "excluded_warmup_seconds": lambda r: _safe_float(r["timing"].get("warmup_separate_excluded", {}).get("seconds")),
            "resident_signal_and_null_inference_seconds": lambda r: inference_stage(r, ["signal_pretransferred_resident_inference", "null_pretransferred_resident_inference"]),
            "streamed_signal_and_null_transfer_inference_seconds": lambda r: inference_stage(r, ["signal_streamed_host_transfer_plus_inference", "null_streamed_host_transfer_plus_inference"]),
            "decision_verification_cpu_seconds": lambda r: combined_stage(r, ["decision_verification_cpu_seconds"]),
            "crosscheck_verification_cpu_seconds": lambda r: combined_stage(r, ["verification_cross_checks_and_cold_decision_cpu_seconds"]),
            "provenance_hashing_seconds": lambda r: combined_stage(r, ["source_and_input_hashing_seconds"]),
            "score_archive_serialization_seconds": lambda r: combined_stage(r, ["score_archive_serialization_seconds"]),
            "complete_driver_job_wall_seconds": lambda r: combined_stage(r, ["complete_job_wall_seconds"]),
        }
        stage_summary = {name: med(getter) for name, getter in stage_getters.items()}
        g.update({"replicate_count": len(source), "replicate_seeds": sorted({r["seed"] for r in source}),
                  "resident_full_scored_observation_pipeline_seconds": resident_pipeline,
                  "resident_signal_inference_seconds": signal, "resident_null_inference_seconds": null,
                  "resident_cuda_event_seconds_signal_plus_null": event,
                  "streamed_full_scored_observation_pipeline_seconds": streamed_pipeline,
                  "scored_observation_sets": (source[0]["work"].get("observation_sets_scored") if source else None),
                  "complete_driver_job_wall_seconds": full,
                  "resident_candidate_block_score_evaluations_per_second": resident_evals["median_seconds"] if resident_evals else None,
                  "streamed_candidate_block_score_evaluations_per_second": streamed_evals["median_seconds"] if streamed_evals else None,
                  "pipeline_scope": "one resident or streamed organization; total includes all scored observation sets, shared preparation charged once, decision verification; check scored_observation_sets for signal/null count",
                  "complete_job_wall_scope": "source reports define; includes hashing and score archive/verification under timing protocol v2",
                  "stage_seconds": stage_summary,
                  "throughput_scope": "fixed-family sufficient-statistic screening only; not acquisition attempts/s or a validated operational result",
                  "cost_estimate_usd_per_complete_driver_job": (rate*full["median_seconds"]/3600 if full else None),
                  "cost_estimate_rate_usd_hour": rate,
                  "cost_per_operationally_validated_result_usd": None,
                  "cost_limit": "screening cost estimate only; excludes rental rounding, export/calibration/holdout, transfer and idle time; no target-validated result is present"})
        g["runs"] = [{"path": str(r["path"]), "sha256": r["sha256"], "seed": r["seed"],
                       "status": r["data"].get("status"),
                       "timing_protocol_version": r["timing"].get("timing_protocol_version"),
                       "paired_score_archive": r["data"].get("results", {}).get("signal", {}).get("scores_file")}
                      for r in source]
        grouped.append(g)

    # Same archive family, root seed, split, input hash, candidate map and math
    # precision are necessary before comparing score arrays.
    score_pairs = []
    by_match = {}
    for row in usable:
        d = row["data"]
        c = d.get("configuration", {})
        match = (row["input"], full_input_identity(d), row["model_identity"],
                 row["source_contract"], row["environment_identity"], c.get("seed"), c.get("split"), c.get("batch"),
                 c.get("precision"), d.get("timing_seconds", {}).get("timing_protocol_version"),
                 tuple(d.get("workload", {}).get("candidate_labels", [])),
                 tuple(tuple(x) for x in d.get("workload", {}).get("candidate_mappings", [])))
        by_match.setdefault(match, []).append(row)
    for match, subset in by_match.items():
        baseline = next((r for r in subset if r["backend"] == "reference"), None)
        if baseline is None:
            continue
        for candidate in subset:
            if candidate["backend"] in ("reference", baseline["backend"]):
                continue
            left = _archive_path(baseline)
            right = _archive_path(candidate)
            if left is None or right is None:
                score_pairs.append({"reference": str(baseline["path"]), "candidate": str(candidate["path"]),
                                    "status": "not_compared", "reason": "score NPZ missing from local input directory"})
                continue
            try:
                with np.load(left) as a, np.load(right) as b:
                    record = {"reference": str(baseline["path"]), "candidate": str(candidate["path"]),
                              "input_manifest_sha256": match[0], "input_file_hashes": match[1],
                              "model_identity": match[2], "common_source_contract": match[3],
                              "environment_identity": match[4], "root_seed": match[5], "split": match[6],
                              "batch": match[7], "precision": match[8],
                              "timing_protocol_version": match[9], "status": "compared"}
                    for label in ("signal", "null"):
                        if label not in a or label not in b:
                            continue
                        x, y = a[label], b[label]
                        if x.shape != y.shape:
                            raise ValueError(f"{label} shapes differ: {x.shape} vs {y.shape}")
                        record[f"{label}_max_absolute_log_lr_error"] = float(np.max(np.abs(x-y)))
                        record[f"{label}_ranking_disagreements"] = int(np.sum(np.argmax(x, axis=-1) != np.argmax(y, axis=-1)))
                        record[f"{label}_admission_comparison"] = _admission_summary(
                            baseline["data"], candidate["data"], a, b, label)
                    score_pairs.append(record)
            except (OSError, ValueError, KeyError) as exc:
                score_pairs.append({"reference": str(baseline["path"]), "candidate": str(candidate["path"]),
                                    "status": "not_compared", "reason": str(exc)})

    return {"groups": grouped, "excluded_from_timing_aggregation": excluded,
            "paired_score_comparisons": score_pairs,
            "scope": "only timing protocol v2 contributes to throughput; every input must have the same hash/seed/family/precision for a paired score comparison"}


def _archive_path(row):
    ref = row["data"].get("results", {}).get("signal", {}).get("scores_file")
    if not ref:
        return None
    candidate = Path(ref)
    if candidate.is_absolute() and candidate.exists():
        return candidate
    local = row["path"].parent / candidate.name
    return local if local.exists() else None


def _bit_scores(scores: np.ndarray, labels: np.ndarray) -> np.ndarray:
    columns = []
    for bit in (0, 1):
        values = scores[:, labels == bit]
        if values.shape[1] == 0:
            columns.append(np.full(scores.shape[0], -np.inf))
        else:
            top = np.max(values, axis=1)
            columns.append(top + np.log(np.exp(values - top[:, None]).mean(axis=1)))
    return np.stack(columns, axis=1)


def _admission_summary(base_data, candidate_data, scores_a, scores_b, label):
    if label not in scores_a or label not in scores_b:
        return None
    labels = np.asarray(base_data.get("workload", {}).get("candidate_labels", []), dtype=np.int64)
    a, b = scores_a[label], scores_b[label]
    if labels.size != a.shape[-1] or labels.size != b.shape[-1]:
        return {"status": "not_compared", "reason": "candidate labels do not match archived score columns"}
    abits, bbits = _bit_scores(a, labels), _bit_scores(b, labels)
    awinner, bwinner = np.argmax(abits, axis=1), np.argmax(bbits, axis=1)
    row = {"status": "compared",
           "candidate_rank_disagreements": int(np.sum(np.argmax(a, axis=1) != np.argmax(b, axis=1))),
           "bit_rank_disagreements": int(np.sum(awinner != bwinner))}
    for target in ("operational_default", "relaxed"):
        ra = base_data.get("results", {}).get("signal", {}).get(target, {})
        rb = candidate_data.get("results", {}).get("signal", {}).get(target, {})
        ta, tb = ra.get("threshold_nats"), rb.get("threshold_nats")
        if ta is None or tb is None or not math.isclose(float(ta), float(tb), rel_tol=0, abs_tol=1e-12):
            row[target] = {"status": "not_compared", "reason": "threshold missing or differs between reports"}
            continue
        admit_a, admit_b = np.max(abits, axis=1) >= float(ta), np.max(bbits, axis=1) >= float(tb)
        bit_a, bit_b = np.where(admit_a, awinner, -1), np.where(admit_b, bwinner, -1)
        row[target] = {"threshold_nats": float(ta),
                       "admission_disagreements": int(np.sum(admit_a != admit_b)),
                       "admitted_bit_decision_disagreements": int(np.sum(bit_a != bit_b)),
                       "reference_admissions": int(admit_a.sum()),
                       "candidate_admissions": int(admit_b.sum())}
    return row


def _precision_report(loaded, rate: float):
    reports = []
    for path, digest, d, kind in loaded:
        if kind != "precision":
            continue
        cn0s = sorted({float(r["cn0_db_hz"]) for r in d.get("results", []) if r.get("status") == "screening_complete"})
        modes = sorted({r["mode"] for r in d.get("results", []) if r.get("status") == "screening_complete"})
        report_modes = []
        rates_by_mode = {}
        for mode in modes:
            rows = [r for r in d["results"] if r.get("mode") == mode and r.get("status") == "screening_complete"]
            mode_info = d.get("mode_details", {}).get(mode, {})
            execution_device = mode_info.get("device")
            gpu_mode = isinstance(execution_device, str) and execution_device.startswith("cuda")
            points = []
            for row in rows:
                metrics = row.get("metrics", {})
                counts = metrics.get("diagnostic_counts_at_hypothetical_threshold", {})
                n = int(metrics.get("trials", 0))
                k = int(counts.get("correct_bit_accepted", 0))
                mode_info = d.get("mode_details", {}).get(mode, {})
                points.append({"cn0_db_hz": float(row["cn0_db_hz"]), "trials": n,
                               "correct_bit_accepted": k, "diagnostic_probability": k/n if n else None,
                               "wilson_95_ci": wilson(k, n),
                               "wrong_bit_accepted": counts.get("wrong_bit_accepted"),
                               "missed": counts.get("missed"),
                               "null_gate_passes": counts.get("null_gate_passes"),
                               "ranking_changes_vs_unquantized_input_reference": metrics.get("ranking_changes_vs_unquantized_input_reference"),
                               "near_boundary_candidate_rank_changes": metrics.get("near_boundary_candidate_rank_changes"),
                               "paired_score_seconds": row.get("timing", {}).get("paired_batch_inference_wall_seconds"),
                               "paired_score_hypotheses_per_second": row.get("timing", {}).get("paired_score_hypotheses_per_second"),
                               "gate_calibration": metrics.get("gate_validity"),
                               "supported": mode_info.get("status") == "supported"})
            if not rows or any(not p["supported"] for p in points):
                continue
            rates_by_mode[mode] = statistics.median(p["paired_score_hypotheses_per_second"] for p in points
                                                   if p["paired_score_hypotheses_per_second"])
            report_modes.append({"mode": mode, "points": points,
                                 "execution_device": execution_device,
                                 "gpu_throughput_cost_eligible": gpu_mode,
                                 "sensitivity_crossings": {str(target): _crossing(points, target) for target in (.90, .99)},
                                 "median_paired_score_hypotheses_per_second": rates_by_mode.get(mode),
                                 "estimated_inference_cost_usd_per_1000_paired_trial_conditions":
                                     (rate * sum(p["paired_score_seconds"] for p in points if p["paired_score_seconds"] is not None)
                                      /3600/(sum(p["trials"] for p in points)*1.)*1000 if points and gpu_mode else None),
                                 "cost_rate_usd_per_hour": rate if gpu_mode else None,
                                 "cost_status": ("screening inference cost estimate only; no mode has an operationally calibrated admission gate"
                                                 if gpu_mode else "CPU or unverified execution device: GPU price is not assigned; timing retained as reference diagnostics only")})
        reference_mode = next((m for m in report_modes if m["mode"] == "strict-fp64-reference"), None)
        ref_cross = reference_mode["sensitivity_crossings"] if reference_mode else None
        for mode in report_modes:
            mode["sensitivity_loss_db_vs_strict_fp64_reference"] = {}
            mode["throughput_vs_sensitivity_budget"] = []
            for target in (.90, .99):
                own = mode["sensitivity_crossings"][str(target)]
                ref = ref_cross[str(target)] if ref_cross else None
                loss = (own["estimated_crossing_db_hz"]-ref["estimated_crossing_db_hz"]
                        if own and ref and own["estimated_crossing_db_hz"] is not None and ref["estimated_crossing_db_hz"] is not None else None)
                mode["sensitivity_loss_db_vs_strict_fp64_reference"][str(target)] = loss
                for budget in (.1, .25, .5, 1.0):
                    if mode["gpu_throughput_cost_eligible"] and loss is not None and loss <= budget:
                        mode["throughput_vs_sensitivity_budget"].append({"target_correct_probability": target,
                             "allowable_sensitivity_loss_db": budget,
                             "estimated_paired_score_hypotheses_per_second": mode["median_paired_score_hypotheses_per_second"],
                             "screening_only": True,
                             "confidence_interval_resolves_budget": _budget_resolved(mode, reference_mode, target, budget)})
        reports.append({"path": str(path), "sha256": digest,
                        "input_manifest_sha256": d.get("inputs", {}).get("input_hashes", {}).get("manifest.json"),
                        "root_seed": d.get("root_seed"), "split": d.get("split"),
                        "requested_modes": d.get("requested_modes"), "available_cn0_db_hz": cn0s,
                        "trials_per_point": d.get("results", [{}])[0].get("metrics", {}).get("trials"),
                        "whole_screen_evaluate_wall_seconds": d.get("timing", {}).get("complete_evaluate_wall_seconds"),
                        "whole_screen_inference_compute_cost_estimate_usd":
                            (rate*d["timing"]["complete_evaluate_wall_seconds"]/3600
                             if d.get("timing", {}).get("complete_evaluate_wall_seconds") is not None else None),
                        "whole_screen_cost_scope": "observed-rate estimate for all requested precision modes and C/N0 points within evaluate; excludes process startup, cloud idle/rental rounding, and does not describe a validated result",
                        "scope": "single fixed public template bank and its declared white Gaussian observations; sweeps may reuse the same standard-normal draws at scaled noise; observations are paired across modes within each C/N0 but C/N0 points are not pooled independent trials",
                        "modes": report_modes,
                        "tail_status": "not operationally calibrated; precision report records hypothetical fixed-family gates only"})
    return reports


def _crossing(points, target):
    usable = sorted((p for p in points if p["diagnostic_probability"] is not None), key=lambda p: p["cn0_db_hz"])
    brackets = []
    for lo, hi in zip(usable, usable[1:]):
        p0, p1 = lo["diagnostic_probability"], hi["diagnostic_probability"]
        if p0 < target <= p1 and p1 > p0:
            x = lo["cn0_db_hz"] + (target-p0)*(hi["cn0_db_hz"]-lo["cn0_db_hz"])/(p1-p0)
            brackets.append((x, lo, hi))
    if not brackets:
        return {"estimated_crossing_db_hz": None, "target": target,
                "status": "unresolved; target not bracketed by adjacent sampled C/N0 points", "bracket_ci": None}
    x, lo, hi = brackets[0]
    lo_ci, hi_ci = lo["wilson_95_ci"], hi["wilson_95_ci"]
    return {"estimated_crossing_db_hz": x, "target": target,
            "status": "descriptive linear interpolation of first adjacent crossing; fixed path only",
            "bracket_cn0_db_hz": [lo["cn0_db_hz"], hi["cn0_db_hz"]],
            "bracket_ci": [lo_ci, hi_ci],
            "confidence_resolution": "unresolved unless both points' binomial intervals establish below-target then at-or-above-target"}


def _budget_resolved(mode, reference_mode, target, budget):
    own = mode["sensitivity_crossings"][str(target)]
    ref = reference_mode["sensitivity_crossings"][str(target)] if reference_mode else None
    if not own or not ref or own.get("estimated_crossing_db_hz") is None or ref.get("estimated_crossing_db_hz") is None:
        return False
    # No paired-bootstrap interval is computed here. Per-point Wilson intervals
    # remain available; declaring a budget resolved needs a fresh paired model.
    return False


def plots(output: Path, timing: dict, precision: list[dict]):
    files = []
    groups = [g for g in timing["groups"] if g["replicate_count"] >= 2]
    if groups:
        series=[]
        identities = {}
        for g in groups:
            config = dict(g["model_identity"][0])
            # Batch/trial count is the x axis. Every other source, model,
            # observation and environment identity must match before points
            # can share a line. Keep cross-device/cross-model series distinct.
            model = (tuple((k, v) for k, v in g["model_identity"][0] if k not in ("batch", "trials")),
                     *g["model_identity"][1:])
            key = _freeze((g["input_manifest_sha256"], g["input_file_hashes"], model,
                g["common_source_contract"], g["selected_backend_source_sha256"],
                g["environment_identity"], g["backend"], g["precision"], g["timing_protocol_version"]))
            identity_short = hashlib.sha256(repr(key).encode("utf-8")).hexdigest()[:8]
            device_name = dict(g["environment_identity"]).get("device_name") or "unknown device"
            label = (f'{device_name}: {g["backend"]}, '
                     f'{config.get("observation_preparation") or "reference"} preparation, '
                     f'{g["precision"]}, series {identity_short}')
            identities.setdefault(key, {"label": label, "groups": []})["groups"].append(g)
        for item in identities.values():
            label, selected = item["label"], item["groups"]
            selected = sorted(selected, key=lambda g:g["batch"])
            series.append((label, [(g["batch"], g["resident_candidate_block_score_evaluations_per_second"])
                                   for g in selected if g.get("resident_candidate_block_score_evaluations_per_second")]))
        svg=output/"gpu-throughput.svg"
        _write_svg(svg,"Fixed-family hour-path screening throughput","Independent trials per batch","Candidate-block score evaluations / second",series)
        files.append(svg.name)
    for report in precision:
        series=[]
        for mode in report["modes"]:
            points=[p for p in mode["points"] if p["supported"]]
            if not points: continue
            series.append((f'{mode["mode"]} ({mode.get("execution_device") or "unverified device"})',
                           [(p["cn0_db_hz"],p["diagnostic_probability"],p["wilson_95_ci"]) for p in points]))
        # Different directories can contain distinct evidence with the same
        # basename. Bind every plot filename to the report content hash.
        plot_stem = Path(report["path"]).stem+"-"+report["sha256"][:12]
        svg=output/(plot_stem+"-precision.svg")
        _write_svg(svg,"Precision screening at hypothetical fixed-family threshold","C/N0 (dB-Hz)","Correct bit accepted fraction (diagnostic)",series,x_limits=(min(report["available_cn0_db_hz"]),max(report["available_cn0_db_hz"])),y_limits=(0,1),horizontals=[(.90,"90% target"),(.99,"99% target")])
        files.append(svg.name)
        trade=[]
        reference=next((m for m in report["modes"] if m["mode"]=="strict-fp64-reference"),None)
        if reference:
            for target in (.90,.99):
                for mode in report["modes"]:
                    if not mode.get("gpu_throughput_cost_eligible", False):
                        continue
                    loss=mode["sensitivity_loss_db_vs_strict_fp64_reference"][str(target)]
                    through=mode["median_paired_score_hypotheses_per_second"]
                    if loss is not None and through is not None:
                        trade.append((f'{mode["mode"]} @ {target:.0%}',[(loss,through,None)]))
            svg=output/(plot_stem+"-throughput-sensitivity.svg")
            _write_svg(svg,"GPU screening throughput; sensitivity budgets remain unresolved",
                       "Estimated loss vs CPU FP64 reference (dB; point estimates only)",
                       "Paired observations scored / second (GPU inference only)",trade,
                       x_limits=(-.05,1.05),verticals=[(x,f"{x:g} dB") for x in (.1,.25,.5,1.)])
            files.append(svg.name)
            # trade contains GPU modes only; CPU-reference timing has no GPU
            # price and never becomes a cost/throughput comparison point.
            gpu_rate = next((m["cost_rate_usd_per_hour"] for m in report["modes"]
                             if m.get("gpu_throughput_cost_eligible", False)), None)
            cost = [(label, [(x, 2000*gpu_rate/3600/y, None)
                              for x, y, _ in points if y > 0]) for label, points in trade]
            svg=output/(plot_stem+"-cost-sensitivity.svg")
            _write_svg(svg,"Conditional screening cost; sensitivity budgets remain unresolved",
                       "Estimated loss vs CPU FP64 reference (dB; point estimates only)",
                       "USD / 1000 paired H0+H1 trials (inference only)",cost,
                       x_limits=(-.05,1.05),verticals=[(x,f"{x:g} dB") for x in (.1,.25,.5,1.)])
            files.append(svg.name)
    files += [str(Path(f).with_suffix(".png")) for f in files]
    return {"status":"written","files":files,"renderer":"Matplotlib SVG and PNG; per-point confidence intervals, no simultaneous sensitivity-loss interval"}


def _write_svg(path, title, xlabel, ylabel, series, *, x_limits=None, y_limits=None, horizontals=(), verticals=()):
    """Standard scientific plotting; vector and raster exports share one figure."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    with plt.rc_context({"font.size": 10, "svg.hashsalt": "triad-optimization-v1"}):
        fig, ax = plt.subplots(figsize=(12.5, 7.0), layout="constrained")
        for label, items in series:
            valid = [v for v in items if v[0] is not None and v[1] is not None
                     and math.isfinite(v[0]) and math.isfinite(v[1])]
            if not valid:
                continue
            x, y = np.asarray([v[0] for v in valid]), np.asarray([v[1] for v in valid])
            if all(len(v) > 2 and v[2] is not None for v in valid):
                error = np.asarray([[v[1]-v[2][0], v[2][1]-v[1]] for v in valid]).T
                ax.errorbar(x, y, yerr=np.maximum(error, 0), marker="o", markersize=3,
                            linewidth=1, capsize=2, label=label, alpha=.8)
            else:
                ax.plot(x, y, "o-", markersize=4, linewidth=1, label=label)
        for y, label in horizontals:
            ax.axhline(y, linestyle="--", linewidth=.8, color="gray", label=label)
        for x, label in verticals:
            ax.axvline(x, linestyle="--", linewidth=.8, color="gray")
            ax.text(x, .98, label, transform=ax.get_xaxis_transform(), rotation=90,
                    va="top", ha="right", color="gray", fontsize=8)
        if x_limits and x_limits[0] < x_limits[1]:
            ax.set_xlim(*x_limits)
        if y_limits and y_limits[0] < y_limits[1]:
            ax.set_ylim(*y_limits)
        ax.set(title=title, xlabel=xlabel, ylabel=ylabel)
        ax.grid(alpha=.2)
        ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=7)
        fig.savefig(path, metadata={"Date": None})
        fig.savefig(path.with_suffix(".png"), dpi=150)
        plt.close(fig)


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("inputs",nargs="+",type=Path,help="JSON result file(s) or directories searched recursively")
    p.add_argument("--output",required=True,type=Path,help="new output directory")
    p.add_argument("--hourly-rate-usd",type=float,default=1.197,help="observed cloud GPU display rate for screening estimates; default RTX5090 $1.197/hour")
    a=p.parse_args(argv)
    if a.hourly_rate_usd<0 or not math.isfinite(a.hourly_rate_usd): p.error("hourly rate must be finite and nonnegative")
    if a.output.exists(): p.error("output directory must not already exist")
    loaded,rejected=read_evidence(a.inputs)
    if not loaded: p.error("no supported triad optimization or precision JSON found")
    a.output.mkdir(parents=True)
    timing=timing_report(loaded,a.hourly_rate_usd)
    precision=_precision_report(loaded,a.hourly_rate_usd)
    manifest={"schema":"triad-optimization-summary-v1","command_argv":sys.argv if argv is None else argv,
              "source_sha256":sha256(Path(__file__).resolve()),
              "legacy_precision_rate_field_units": "paired_score_hypotheses_per_second in probe-v1 is2*noise_trials/seconds: paired observations/full fixed families scored, not individual candidate hypotheses","input_files":[{"path":str(path),"sha256":digest,"schema":data.get("schema"),"kind":kind} for path,digest,data,kind in loaded],
              "rejected_or_unrelated_json_files":rejected,
              "hourly_rate_usd":a.hourly_rate_usd,
              "timing":timing,"precision_screens":precision,
              "plot_artifacts":plots(a.output,timing,precision),
              "operational_status":{"default_target":{"correct_probability":.99,"any_noise_admission_probability_per_24h":1e-4,"qualified_result_count":0},
                                    "relaxed_target":{"correct_probability":.90,"any_noise_admission_probability_per_12h":.01,"qualified_result_count":0},
                                    "cost_per_qualified_result_usd":None,
                                    "reason":"all available precision gates are hypothetical fixed-family screening, and timing workload is fixed exported candidate scoring; no full operational acquisition holdout is included"}}
    target=a.output/"summary.json"
    target.write_text(json.dumps(manifest,indent=2,allow_nan=False)+"\n")
    print(json.dumps({"summary":str(target),"gpu_timing_groups":len(timing["groups"]),
                      "timing_excluded":len(timing["excluded_from_timing_aggregation"]),
                      "precision_screens":len(precision),"plots":manifest["plot_artifacts"]},indent=2))
    return 0


if __name__=="__main__": raise SystemExit(main())

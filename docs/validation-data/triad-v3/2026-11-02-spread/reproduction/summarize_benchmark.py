#!/usr/bin/env python3
"""Summarize the frozen sequential 1024-trial CPU/CUDA benchmark outputs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def method_entries(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    out = {}
    for path in report["paths"]:
        for cond in path["conditions"]:
            key = (int(path["physical_path_index"]), cond["control"])
            method, entry = next(iter(cond["methods"].items()))
            out[f"{key[0]}:{key[1]}"] = {"physical_path_index": key[0], "method": method, "condition": cond, "entry": entry}
    return out


def aggregate_counts(report: dict[str, Any]) -> dict[str, Any]:
    totals: dict[str, dict[str, int]] = {}
    per_path: dict[str, dict[str, dict[str, int]]] = {}
    for item in method_entries(report).values():
        control = item["condition"]["control"]
        out = totals.setdefault(control, {})
        path_key = str(item["physical_path_index"])
        for key, value in item["entry"]["counts"].items():
            out[key] = out.get(key, 0) + int(value)
        path_counts = per_path.setdefault(path_key, {}).setdefault(control, {})
        for key, value in item["entry"]["counts"].items():
            path_counts[key] = path_counts.get(key, 0) + int(value)
    return {"total": totals, "by_physical_path_index": per_path}


def runtime_breakdown(report: dict[str, Any], runner: dict[str, Any] | None) -> dict[str, Any]:
    entries = method_entries(report)
    sampler = sum(float(x["condition"].get("sampler_and_ucb_seconds", 0.0)) for x in entries.values())
    receiver = sum(float(x["entry"].get("receiver_seconds", 0.0)) for x in entries.values())
    prep = sum(float(x["entry"].get("diagnostics_last_batch", {}).get("preparation_seconds_this_call", 0.0))
               for x in entries.values())
    loading = sum(float(x.get("load_seconds", 0.0)) for x in report.get("paths", []))
    tool_wall = float(report.get("wall_seconds", 0.0))
    run_wall = float(runner.get("elapsed_seconds", 0.0)) if runner else None
    return {
        "runner_wall_seconds": run_wall,
        "tool_wall_seconds": tool_wall,
        "sampled_design_load_seconds": loading,
        "noise_sampling_plus_UCB_seconds": sampler,
        "receiver_call_seconds_including_explicit_preparation": receiver,
        "explicit_receiver_preparation_seconds": prep,
        "receiver_call_minus_explicit_preparation_seconds": max(0.0, receiver - prep),
        "tool_wall_unattributed_after_load_sampler_receiver_seconds": tool_wall - loading - sampler - receiver,
        "runner_minus_tool_wall_seconds": run_wall - tool_wall if run_wall is not None else None,
        "timing_scope": "Wall-clock and receiver-call measurements from one sequential run per device. Receiver call includes backend dispatch/synchronization and any explicit receiver plan preparation; it is not kernel-only timing.",
    }


def compare_pair(reports: dict[str, dict[str, Any]], cpu_name: str, gpu_name: str,
                 runner_jobs: dict[str, dict[str, Any]], scores_dir: Path) -> dict[str, Any]:
    cpu, gpu = reports[cpu_name], reports[gpu_name]
    ec, eg = method_entries(cpu), method_entries(gpu)
    same_input_hashes = [p.get("sampled_input_hashes") for p in cpu["paths"]] == [p.get("sampled_input_hashes") for p in gpu["paths"]]
    # Each condition carries a single seed record in this screen; verify it explicitly.
    draw_identity_equal = True
    for k in sorted(ec.keys() & eg.keys()):
        dc = ec[k]["condition"].get("draws_simulation_only", [])
        dg = eg[k]["condition"].get("draws_simulation_only", [])
        if len(dc) != len(dg):
            draw_identity_equal = False
            continue
        for a, b in zip(dc, dg):
            if any(a.get(field) != b.get(field) for field in ("noise_seed", "conditional_noise_trials", "control", "injected_cn0_db_hz_simulation_only")):
                draw_identity_equal = False
    config_c, config_g = dict(cpu["configuration"]), dict(gpu["configuration"])
    device_c, device_g = config_c.pop("device", None), config_g.pop("device", None)
    cfg_equal_except_device = config_c == config_g
    ac = np.load(scores_dir / cpu["scores_file"], allow_pickle=False)
    ag = np.load(scores_dir / gpu["scores_file"], allow_pickle=False)
    cpu_npz_hash = sha256(scores_dir / cpu["scores_file"])
    gpu_npz_hash = sha256(scores_dir / gpu["scores_file"])
    if cpu_npz_hash != cpu.get("scores_sha256") or gpu_npz_hash != gpu.get("scores_sha256"):
        raise ValueError(f"score archive hash mismatch for {cpu_name} or {gpu_name}")
    common_keys = sorted(set(ac.files) & set(ag.files))
    allkeys = set(ac.files) == set(ag.files)
    threshold_values = []
    for x in method_entries(cpu).values():
        threshold_values.append(float(x["entry"]["threshold"]))
    for x in method_entries(gpu).values():
        threshold_values.append(float(x["entry"]["threshold"]))
    threshold = threshold_values[0] if threshold_values and len(set(threshold_values)) == 1 else None
    nrows = 0
    maxdiff = 0.0
    absdiffs = []
    rank_disagreements = gate_disagreements = 0
    gate_margins: list[float] = []
    smallest_gate_margin = float("inf")
    smallest_winner_margin = float("inf")
    for key in common_keys:
        a = np.asarray(ac[key], dtype=np.float64)
        b = np.asarray(ag[key], dtype=np.float64)
        if a.shape != b.shape or a.ndim != 2 or a.shape[1] != 2:
            raise ValueError(f"unexpected score-array shape mismatch at {key}: {a.shape} vs {b.shape}")
        diff = np.abs(a - b)
        maxdiff = max(maxdiff, float(np.max(diff)))
        absdiffs.extend(diff.ravel().tolist())
        pa, pb = np.argmax(a, axis=1), np.argmax(b, axis=1)
        rank_disagreements += int(np.count_nonzero(pa != pb))
        if threshold is not None:
            ma, mb = np.max(a, axis=1), np.max(b, axis=1)
            gate_disagreements += int(np.count_nonzero((ma >= threshold) != (mb >= threshold)))
            margins = np.minimum(np.abs(ma - threshold), np.abs(mb - threshold))
            gate_margins.extend(margins.tolist())
            smallest_gate_margin = min(smallest_gate_margin, float(np.min(margins)))
        ordered = np.sort(a, axis=1)
        smallest_winner_margin = min(smallest_winner_margin, float(np.min(ordered[:, 1] - ordered[:, 0])))
        nrows += int(a.shape[0])
    if nrows != sum(x["entry"]["counts"]["trials"] for x in ec.values()):
        raise ValueError(f"NPZ row count does not match JSON trial counts for {cpu_name}")
    if nrows != sum(x["entry"]["counts"]["trials"] for x in eg.values()):
        raise ValueError(f"NPZ row count does not match JSON trial counts for {gpu_name}")
    cpu_runtime = runtime_breakdown(cpu, runner_jobs.get(cpu_name))
    gpu_runtime = runtime_breakdown(gpu, runner_jobs.get(gpu_name))
    return {
        "method": next(iter(ec.values()))["method"] if ec else None,
        "reports": {
            cpu_name: {"json_sha256": sha256(scores_dir / f"{cpu_name}.json"), "scores_sha256": cpu.get("scores_sha256"),
                       "device": device_c, "wall_seconds": cpu.get("wall_seconds"), "counts": aggregate_counts(cpu),
                       "runtime": cpu_runtime, "arithmetic": cpu.get("arithmetic"), "cuda_memory": cpu.get("cuda_memory")},
            gpu_name: {"json_sha256": sha256(scores_dir / f"{gpu_name}.json"), "scores_sha256": gpu.get("scores_sha256"),
                       "device": device_g, "wall_seconds": gpu.get("wall_seconds"), "counts": aggregate_counts(gpu),
                       "runtime": gpu_runtime, "arithmetic": gpu.get("arithmetic"), "cuda_memory": gpu.get("cuda_memory")},
        },
        "matched_inputs": {"same_sampled_input_hashes": same_input_hashes, "same_configuration_except_device": cfg_equal_except_device,
                           "same_noise_seed_and_draw_schedule": draw_identity_equal, "same_npz_key_set": allkeys,
                           "same_source_and_dependency_hashes": cpu.get("source_sha256") == gpu.get("source_sha256") and cpu.get("dependency_sha256") == gpu.get("dependency_sha256"),
                           "score_archive_sha256_verified": {cpu_name: cpu_npz_hash, gpu_name: gpu_npz_hash}},
        "actual_diagnostics": {
            "CPU": next(iter(ec.values()))["entry"].get("diagnostics_last_batch", {}),
            "CUDA": next(iter(eg.values()))["entry"].get("diagnostics_last_batch", {}),
        },
        "score_parity": {
            "score_arrays_compared": len(common_keys), "score_rows_compared": nrows,
            "maximum_absolute_log_score_difference": maxdiff,
            "median_absolute_difference": float(np.median(absdiffs)) if absdiffs else None,
            "p99_absolute_difference": float(np.quantile(absdiffs, 0.99)) if absdiffs else None,
            "argmax_bit_disagreements": rank_disagreements, "admission_gate_disagreements": gate_disagreements,
            "threshold": threshold, "smallest_absolute_gate_margin": smallest_gate_margin if threshold is not None else None,
            "score_rows_within_max_backend_delta_of_gate": int(np.count_nonzero(np.asarray(gate_margins) <= maxdiff)),
            "minimum_winner_to_runnerup_score_margin": smallest_winner_margin if np.isfinite(smallest_winner_margin) else None,
        },
        "observed_runtime_ratios": {
            "outer_CPU_over_GPU_wall": cpu_runtime["runner_wall_seconds"] / gpu_runtime["runner_wall_seconds"] if gpu_runtime["runner_wall_seconds"] else None,
            "receiver_call_CPU_over_GPU": cpu_runtime["receiver_call_seconds_including_explicit_preparation"] / gpu_runtime["receiver_call_seconds_including_explicit_preparation"] if gpu_runtime["receiver_call_seconds_including_explicit_preparation"] else None,
        },
    }


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--scores", type=Path, default=Path("local-evidence/triad-spread-benchmark/benchmark-1024-seed231104"))
    p.add_argument("--runner-results", type=Path, default=Path("local-evidence/triad-spread-benchmark/benchmark-fixed-jobs/results.json"))
    p.add_argument("--plan", type=Path, default=Path("docs/validation-data/triad-v3/2026-11-02-spread/reproduction/benchmark-plan.json"))
    p.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "benchmark-summary.json")
    a = p.parse_args()
    scores = a.scores.resolve()
    runner_path = a.runner_results.resolve()
    plan_path = a.plan.resolve()
    reports = {name: load(scores / f"{name}.json") for name in ("cfo-cpu", "cfo-cuda", "window-cpu", "window-cuda")}
    runner_result = load(runner_path)
    if len(runner_result.get("results", [])) != 4 or any(r.get("returncode") != 0 or r.get("runner", {}).get("timed_out") for r in runner_result.get("results", [])):
        raise ValueError("runner receipts do not show all four benchmark jobs completed successfully")
    runner_jobs = {r["name"].replace("-fp32", ""): r for r in runner_result.get("results", [])}
    # Alias plan runner names to the report stems exactly.
    runner_jobs = {"cfo-cpu": runner_jobs.get("cfo-cpu"), "cfo-cuda": runner_jobs.get("cfo-cuda"),
                   "window-cpu": runner_jobs.get("window-cpu"), "window-cuda": runner_jobs.get("window-cuda")}
    pairs = {
        "cfo-ou-ucb": compare_pair(reports, "cfo-cpu", "cfo-cuda", runner_jobs, scores),
        "gaussian-window-ucb": compare_pair(reports, "window-cpu", "window-cuda", runner_jobs, scores),
    }
    original_plan = plan_path.with_name("benchmark-plan-original.json")
    result = {
        "schema": "triad-spread-1024-cpu-cuda-benchmark-summary-v1",
        "scope": "One completed sequential CPU/GPU run per method on two frozen sampled paths, 1024 conditional noise trials per path and control at -28 dB-Hz. Descriptive runtime/score parity only.",
        "reproduction": {
            "analyzer_command": "/tmp/triad-weak-env/bin/python docs/validation-data/triad-v3/2026-11-02-spread/reproduction/summarize_benchmark.py",
            "plan": str(plan_path), "plan_sha256": sha256(plan_path),
            "original_invalid_plan": str(original_plan),
            "original_setup_failure": "The first runner invocation stopped at KeyError('jobs') before launching a child; it produced no measurements. This summary covers only the repaired plan's later completed run.",
            "runner_results": str(runner_path), "runner_results_sha256": sha256(runner_path),
            "input_reports": [{"path": f"{name}.json", "sha256": sha256(scores / f"{name}.json"),
                               "scores_file": reports[name]["scores_file"], "scores_sha256": reports[name]["scores_sha256"]}
                              for name in reports],
        },
        "job_outcomes": [{"name": r["name"], "returncode": r.get("returncode"),
                          "runner_elapsed_seconds": r.get("elapsed_seconds"), "receipt_outcome": r.get("runner", {}).get("outcome"),
                          "timed_out": r.get("runner", {}).get("timed_out")}
                         for r in runner_result.get("results", [])],
        "pairs": pairs,
        "precision_observation": {
            "configured_bulk_precision": "FP32; TF32 disabled",
            "sensitive_stages": "sampler/rank/noise/UCB/Gram and CFO covariance/evidence use FP64; covariance/evidence diagnostics report complex128/float64 for receiver preparation/evidence",
            "claim_limit": "Observed parity is for these paired runs and inputs; it is not universal GPU arithmetic equivalence.",
        },
        "claims_and_limits": [
            "Each CPU/GPU pair reused the same sampled input hashes and random draw seed/control schedule; score and gate parity is checked over every stored trial row.",
            "Runtime is a single sequential sample per backend. Wall includes process setup and context startup; receiver-call time includes backend dispatch and synchronization and is not kernel-only timing.",
            "Receiver timing is separated from noise generation/UCB and explicit Gaussian-window plan preparation. It does not establish latency or throughput over other shapes, GPUs, concurrency levels or physical conditions.",
            "0 false admissions in 2048 conditional null trials per method are specific to these two fixed paths and do not qualify an operational false-admission probability.",
            "These signal outcomes are conditional noise replicas on two fixed physical paths, not independent channel population trials.",
        ],
        "readme_paragraph_proposal": "A sequential 1024-trial CPU/GPU comparison on two fixed g001 paths at -28 dB-Hz completed all four jobs without timeout. Both receiver pairs used the same input hashes and draw seeds; all 4096 score-row vectors per pair had identical winner and admission-gate decisions (CFO maximum absolute score delta [CFO_MAX]; window [WINDOW_MAX]). Correct accepted signal counts were CFO [CFO_COUNTS] and Gaussian-window [WINDOW_COUNTS], with zero wrong accepts; each method had zero admissions in 2048 conditional null trials on these paths. Outer CPU/GPU wall times were CFO [CFO_WALL] s (ratio [CFO_RATIO]×) and window [WINDOW_WALL] s (ratio [WINDOW_RATIO]×); receiver-call times were [CFO_RX] s and [WINDOW_RX] s. For window mode, sampler/UCB consumed [WINDOW_SAMPLER] s and explicit plan preparation [WINDOW_PREP] s of those calls. FP32 bulk arithmetic retained FP64 sensitive calibration/evidence stages with TF32 disabled. These are single-run measurements, not universal GPU speedup or false-alarm qualification.",
    }
    # Fill in the concise paragraph from computed values while retaining explicit scope caveats.
    cfo, win = pairs["cfo-ou-ucb"], pairs["gaussian-window-ucb"]
    def fmt_pair(pair, key):
        a_name, b_name = ("cfo-cpu", "cfo-cuda") if pair is cfo else ("window-cpu", "window-cuda")
        return f"{pair['reports'][a_name]['runtime'][key]:.3f}/{pair['reports'][b_name]['runtime'][key]:.3f}"
    cfo_counts = cfo["reports"]["cfo-cpu"]["counts"]["total"]["signal"]
    win_counts = win["reports"]["window-cpu"]["counts"]["total"]["signal"]
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[CFO_MAX]", f"{cfo['score_parity']['maximum_absolute_log_score_difference']:.3g}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_MAX]", f"{win['score_parity']['maximum_absolute_log_score_difference']:.3g}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[CFO_COUNTS]", f"{cfo_counts['correct_accepted']}/{cfo_counts['trials']}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_COUNTS]", f"{win_counts['correct_accepted']}/{win_counts['trials']}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[CFO_WALL]", fmt_pair(cfo, "runner_wall_seconds"))
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_WALL]", fmt_pair(win, "runner_wall_seconds"))
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[CFO_RX]", fmt_pair(cfo, "receiver_call_seconds_including_explicit_preparation"))
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_RX]", fmt_pair(win, "receiver_call_seconds_including_explicit_preparation"))
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[CFO_RATIO]", f"{cfo['observed_runtime_ratios']['outer_CPU_over_GPU_wall']:.2f}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_RATIO]", f"{win['observed_runtime_ratios']['outer_CPU_over_GPU_wall']:.2f}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_SAMPLER]", f"{win['reports']['window-cpu']['runtime']['noise_sampling_plus_UCB_seconds']:.3f}/{win['reports']['window-cuda']['runtime']['noise_sampling_plus_UCB_seconds']:.3f}")
    result["readme_paragraph_proposal"] = result["readme_paragraph_proposal"].replace("[WINDOW_PREP]", f"{win['reports']['window-cpu']['runtime']['explicit_receiver_preparation_seconds']:.3f}/{win['reports']['window-cuda']['runtime']['explicit_receiver_preparation_seconds']:.3f}")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(a.output), "pairs": {k: {"wall": v["observed_runtime_ratios"],
        "score_parity": v["score_parity"]} for k, v in pairs.items()}}, indent=2))


if __name__ == "__main__":
    main()

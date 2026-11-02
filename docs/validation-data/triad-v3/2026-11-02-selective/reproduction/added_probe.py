#!/usr/bin/env python3
"""Prepare, then optionally run, a paired Added Symbols ×10 diagnostic.

Default invocation is preflight only. Execution requires --execute after review.
It reuses the exact retained native per-path export commands, changing only the
binary path, output directory, and --added value (1 to 10). No samples are
resimulated for the original Added=1 baseline.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import numpy as np


REPO = Path(__file__).resolve().parents[5]
REPRO = Path(__file__).resolve().parent
EVIDENCE = REPO / "local-evidence/triad-selective/remote-final"
OUT = REPO / "local-evidence/triad-selective/added-probe"
BINARY = REPO / "build/dev/triad_long"
INDICES = range(90000, 90008)
PROFILE_PREFIXES = ("g01slowrate-each-65-cn0m24", "g1slowrate-each-65-cn0m14")
CONTROLS = ("signal", "wrong-key")
SEED_KEYS = ("key_seed", "channel_seed", "oscillator_seed", "noise_seed", "frontend_seed", "path_seed")
HASH_FILES = ("manifest.json", "gram.c128", "observation.c128", "received_energy.f64", "intervals.u64", "valid_rows.u64")


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def remote_path(stage, cell, index):
    return f"/workspace/triad/local-evidence/selective/{stage}/{cell}/batch00/path-{index}"


def local_path(stage, cell, index, output=False):
    if output:
        return OUT / "exports" / cell / f"path-{index}"
    return EVIDENCE / stage / cell / "batch00" / f"path-{index}"


def find_job(plan, cell, index):
    return next((j for j in plan["jobs"] if j.get("cell") == cell and j.get("start_index") == 90000 and j.get("control") in CONTROLS), None)


def replace_option(argv, option, value):
    i = argv.index(option)
    argv[i + 1] = str(value)


def changed_native_argv(original, output):
    argv = list(original)
    need(Path(argv[0]).name == "triad_long", "unexpected native export executable")
    added_i = argv.index("--added") + 1
    need(argv[added_i] == "1", "original Added Symbols value was not one")
    replace_option(argv, "--output", output)
    argv[0] = str(BINARY)
    argv[added_i] = "10"
    return argv


def saved_baseline(stage, cell, index):
    plan = read(REPRO / f"{stage}-score-plan.json")
    target = remote_path(stage, cell, index)
    job = next((j for j in plan["jobs"] if target in j["argv"]), None)
    need(job is not None, f"missing frozen scorer command for {target}")
    report_path = EVIDENCE / f"{stage}-scores/{job['name']}.json"
    npz_path = report_path.with_name(report_path.stem + ".scores.npz")
    report = read(report_path)
    rows = [r for r in report["paths"] if r["input"] == target]
    need(len(rows) == 1, f"missing baseline score row {target}")
    row = rows[0]
    need(sha(npz_path) == report["scores_sha256"], f"baseline scores archive hash mismatch: {npz_path}")
    return job, report_path, npz_path, report, row


def preflight():
    export_plan = read(REPRO / "followup-export-plan.json")
    score_plan = read(REPRO / "followup-score-plan.json")
    tradeoffs = read(REPRO / "wrong-key-diagnostic.json")["score_tradeoffs"]
    records = []
    paired = {}
    for profile in PROFILE_PREFIXES:
        canonical_profile = None
        canonical_score = None
        canonical_gate = None
        for control in CONTROLS:
            cell = profile + "-" + control
            stage_job = next((j for j in export_plan["jobs"] if j["cell"] == cell and j["start_index"] == 90000), None)
            need(stage_job is not None, f"missing original export job for {cell}")
            expected = {e["index"]: e for e in stage_job["simulation_only_expected"]}
            for idx in INDICES:
                original = local_path("followup", cell, idx)
                need(original.is_dir(), f"retained source workspace missing: {original}")
                manifest_path = original / "manifest.json"
                manifest = read(manifest_path)
                original_command = manifest["command_argv"]
                exp = expected[idx]
                need(exp["control"] == control, f"control mismatch: {cell}/{idx}")
                need(manifest["control"] == control and manifest["key_seed"] == exp["key_seed"],
                     f"seed/control binding mismatch: {cell}/{idx}")
                roots = manifest["random_seed_roots"]
                for key in SEED_KEYS:
                    actual_seed = manifest["key_seed"] if key == "key_seed" else roots[key.removesuffix("_seed")]
                    need(actual_seed == exp[key], f"{key} mismatch: {cell}/{idx}")
                need(manifest["signal_bit"] == exp["source_bit"] and manifest["epoch"] == exp["epoch"],
                     f"bit/epoch mismatch: {cell}/{idx}")
                cfg = {"gui_rate_hz": manifest["source_format"]["inner_rate_hz"],
                       "requested_active_seconds": manifest["requested_active_seconds"],
                       "sample_rate_hz": manifest["sample_rate"], "cn0_db_hz": manifest["cn0_db_hz"],
                       "block_samples": manifest["block_samples"], "channel": manifest["channel"],
                       "selective_perturbation": manifest["resolved_configuration"]["selective_perturbation"],
                       "spread_delay_ms": manifest["resolved_configuration"]["spread_delay_ms"],
                       "spread_doppler_hz": manifest["resolved_configuration"]["spread_doppler_hz"],
                       "oscillators": [manifest["resolved_configuration"]["base_oscillator"],
                                       manifest["resolved_configuration"]["shift_oscillator"]],
                       "clock_sync": manifest["resolved_configuration"]["clock_sync"],
                       "audio_error_seconds_per_station": manifest["resolved_configuration"]["audio_error_seconds_per_station"],
                       "source_format": manifest["source_format"], "noise_variance": manifest["noise_variance"]}
                if canonical_profile is None:
                    canonical_profile = cfg
                need(cfg == canonical_profile, f"profile config drift across selected controls/indices: {cell}/{idx}")
                need(original_command[original_command.index("--added") + 1] == "1", "baseline --added must be one")
                need(manifest["resolved_configuration"]["selective_perturbation"] == "each-65", "frequency-selective perturbation changed")
                added_interleave = manifest["source_format"]["added_interleave"]
                need(added_interleave is True, f"baseline Added Symbols interleave is not enabled: {cell}/{idx}")
                output = local_path("added10", cell, idx, output=True)
                need(not output.exists(), f"probe output already exists: {output}")
                child = changed_native_argv(original_command, output)
                diff = []
                for pos, (a, b) in enumerate(zip(original_command, child)):
                    if a != b:
                        diff.append({"position": pos, "before": a, "after": b})
                need(len(original_command) == len(child) and len(diff) == 3,
                     f"native argv changes must be exactly binary/output/added ({cell}/{idx}): {diff}")
                need(any(d["before"] == "1" and d["after"] == "10" for d in diff), "missing Added Symbols change")
                need("--stats-noisy" in child and "--mode" in child and child[child.index("--mode") + 1] == "export-stats",
                     "export must retain actual noisy statistics mode")
                _, baseline_report_path, baseline_npz, baseline_report, baseline_row = saved_baseline("followup", cell, idx)
                score_job = next(j for j in score_plan["jobs"] if j["name"] == f"{cell}-batch00")
                score_argv = score_job["argv"]
                score_config = {"methods": score_argv[score_argv.index("--methods") + 1],
                                "window_blocks": int(score_argv[score_argv.index("--window-blocks") + 1]),
                                "window_doppler_width_hz": float(score_argv[score_argv.index("--window-doppler-width") + 1]),
                                "coherence_seconds": float(score_argv[score_argv.index("--coherence") + 1]),
                                "duration_alternatives": int(score_argv[score_argv.index("--duration-alternatives") + 1]),
                                "selection_methods": int(score_argv[score_argv.index("--selection-methods") + 1]),
                                "calibration_alternatives": int(score_argv[score_argv.index("--calibration-alternatives") + 1]),
                                "horizon_seconds": float(score_argv[score_argv.index("--horizon-seconds") + 1]),
                                "attempt_spacing_seconds": float(score_argv[score_argv.index("--attempt-spacing") + 1]),
                                "false_probability": float(score_argv[score_argv.index("--false-probability") + 1]),
                                "correct_reception_target": float(score_argv[score_argv.index("--correct-reception-target") + 1]),
                                "remaining_frozen_receiver_argv": score_argv[score_argv.index("--mode"): ]}
                trade_name = profile.split("-each-")[0]
                trade = next(t for t in tradeoffs if t["profile"] == trade_name)
                gates = {"strict_log_threshold": float(baseline_report["family"]["fixed_window_log_threshold"]),
                         "relaxed_log_threshold": float(trade["relaxed_log_threshold"])}
                if canonical_score is None:
                    canonical_score, canonical_gate = score_config, gates
                need(score_config == canonical_score and gates == canonical_gate,
                     f"frozen score/gate settings differ across paired cases: {cell}/{idx}")
                receipt = baseline_row.get("input_hashes", {})
                need(set(receipt) == set(HASH_FILES), f"baseline receipt incomplete: {cell}/{idx}")
                need(sha(manifest_path) == receipt["manifest.json"], f"baseline manifest receipt mismatch: {cell}/{idx}")
                verified_local = []
                missing_local = []
                for name, expected_hash in receipt.items():
                    if (original / name).is_file():
                        need(sha(original / name) == expected_hash, f"baseline source hash mismatch {cell}/{idx}/{name}")
                        verified_local.append(name)
                    else:
                        missing_local.append(name)
                profile_bits = paired.setdefault(profile + f"-{idx}", {})
                profile_bits[control] = {k.removesuffix("_seed"): (manifest["key_seed"] if k == "key_seed" else roots[k.removesuffix("_seed")]) for k in SEED_KEYS}
                records.append({"profile": profile, "profile_configuration": cfg,
                                "score_configuration": score_config, "gate_configuration": gates,
                                "control": control, "index": idx,
                                "baseline_workspace": str(original.relative_to(REPO)),
                                "baseline_manifest_sha256": sha(manifest_path),
                                "baseline_input_hashes": receipt,
                                "baseline_input_files_rehashed_locally": verified_local,
                                "baseline_input_files_not_retained_locally": missing_local,
                                "baseline_report": str(baseline_report_path.relative_to(REPO)),
                                "baseline_report_sha256": sha(baseline_report_path),
                                "baseline_scores_sha256": sha(baseline_npz),
                                "binary_sha256": sha(BINARY), "original_native_argv": original_command,
                                "added10_native_argv": child, "argv_changes": diff,
                                "added_interleave": added_interleave,
                                "baseline_geometry": {"requested_active_seconds": manifest["requested_active_seconds"],
                                                      "actual_elapsed_seconds": manifest["actual_elapsed_seconds"],
                                                      "nominal_payload_energy_seconds_lower": manifest["nominal_payload_energy_seconds_lower"],
                                                      "sample_count": manifest["sample_count"], "sample_rate_hz": manifest["sample_rate"],
                                                      "capture_seconds": manifest["sample_count"] / manifest["sample_rate"],
                                                      "source_tile_seconds": manifest["source_format"]["tile_seconds"],
                                                      "source_tile_count": manifest["source_format"]["tile_count"],
                                                      "source_occupied_bandwidth_hz": manifest["source_format"]["occupied_bandwidth_hz"],
                                                      "source_support_seconds": manifest["resolved_configuration"]["duration"]["source_support_seconds"],
                                                      "channel_samples": manifest["channel_samples"],
                                                      "channel_tail_samples": manifest["channel_tail_samples"]},
                                "expected_source_bit": exp["source_bit"], "expected_epoch": exp["epoch"],
                                "seed_roots": profile_bits[control], "score_family": baseline_report["family"],
                                "baseline_path_index_in_score_archive": baseline_row["physical_path_index"],
                                "baseline_score_record": baseline_row["conditions"][0]["methods"]["gaussian-window-ucb"].get("score_array")})
    for paired_key, controls in paired.items():
        need(controls["signal"] == controls["wrong-key"], f"signal/wrong-key seed pairing broken: {paired_key}")
    need(len(records) == 32, f"expected 32 preflight records, got {len(records)}")
    return records


def score_values(npz_path, row, gates):
    method = row["conditions"][0]["methods"]["gaussian-window-ucb"]
    vals = np.load(npz_path)[method["score_array"]]
    need(vals.shape == (1, 2), f"expected two bit scores, got {vals.shape}")
    truth = row.get("truth_bit_simulation_only")
    bit = int(truth) if truth is not None else None
    scores = [float(v) for v in vals[0]]
    best = max(scores)
    return {"scores_by_bit": scores, "winner_bit": int(np.argmax(scores)), "truth_bit_simulation_only": bit,
            "strict_admitted": best >= gates["strict_log_threshold"], "relaxed_admitted": best >= gates["relaxed_log_threshold"],
            "strict_threshold": gates["strict_log_threshold"], "relaxed_threshold": gates["relaxed_log_threshold"],
            "original_report_counts": method.get("counts")}


def run_export(record):
    output = REPO / record["added10_native_argv"][record["added10_native_argv"].index("--output") + 1]
    output.parent.mkdir(parents=True, exist_ok=True)
    argv = record["added10_native_argv"]
    env = os.environ.copy()
    env.update({"OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1", "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1"})
    start = time.monotonic()
    try:
        done = subprocess.run(argv, cwd=REPO, env=env, text=True, capture_output=True, timeout=300, check=False)
        elapsed = time.monotonic() - start
        result = {"status": "completed" if done.returncode == 0 else "failed", "seconds": elapsed,
                "returncode": done.returncode, "stdout_tail": done.stdout[-1500:], "stderr_tail": done.stderr[-1500:],
                "output": str(output.relative_to(REPO))}
        if done.returncode == 0:
            manifest_path = output / "manifest.json"
            manifest = read(manifest_path)
            need(manifest["source_format"]["added_symbols"] == 10 and manifest["source_format"]["added_interleave"] is True,
                 f"Added=10/interleave verification failed: {output}")
            cfg = record["profile_configuration"]
            need(manifest["sample_rate"] == cfg["sample_rate_hz"]
                 and manifest["requested_active_seconds"] == cfg["requested_active_seconds"]
                 and manifest["source_format"]["inner_rate_hz"] == cfg["gui_rate_hz"]
                 and manifest["cn0_db_hz"] == cfg["cn0_db_hz"]
                 and manifest["resolved_configuration"]["selective_perturbation"] == cfg["selective_perturbation"]
                 and manifest["resolved_configuration"]["spread_delay_ms"] == cfg["spread_delay_ms"]
                 and manifest["resolved_configuration"]["spread_doppler_hz"] == cfg["spread_doppler_hz"]
                 and manifest["channel"] == cfg["channel"]
                 and manifest["block_samples"] == cfg["block_samples"], f"Added=10 changed physical capture settings: {output}")
            actual_roots = {"key": manifest["key_seed"], **manifest["random_seed_roots"]}
            need(all(actual_roots.get(k) == v for k, v in record["seed_roots"].items()),
                 f"Added=10 changed a paired seed root: {output}")
            need(manifest["signal_bit"] == record["expected_source_bit"] and manifest["epoch"] == record["expected_epoch"],
                 f"Added=10 changed bit or epoch: {output}")
            result.update({"manifest_sha256": sha(manifest_path),
                           "added10_geometry": {"requested_active_seconds": manifest["requested_active_seconds"],
                                                "actual_elapsed_seconds": manifest["actual_elapsed_seconds"],
                                                "nominal_payload_energy_seconds_lower": manifest["nominal_payload_energy_seconds_lower"],
                                                "sample_count": manifest["sample_count"], "sample_rate_hz": manifest["sample_rate"],
                                                "capture_seconds": manifest["sample_count"] / manifest["sample_rate"],
                                                "source_tile_seconds": manifest["source_format"]["tile_seconds"],
                                                "source_tile_count": manifest["source_format"]["tile_count"],
                                                "source_occupied_bandwidth_hz": manifest["source_format"]["occupied_bandwidth_hz"],
                                                "source_support_seconds": manifest["resolved_configuration"]["duration"]["source_support_seconds"],
                                                "channel_samples": manifest["channel_samples"],
                                                "channel_tail_samples": manifest["channel_tail_samples"],
                                                "added_symbols": manifest["source_format"]["added_symbols"],
                                                "added_interleave": manifest["source_format"]["added_interleave"]}})
        return result
    except subprocess.TimeoutExpired as exc:
        return {"status": "timeout", "seconds": time.monotonic() - start, "returncode": None,
                "stdout_tail": str(exc.stdout or b"")[-1500:].decode(errors="replace") if isinstance(exc.stdout, bytes) else str(exc.stdout or "")[-1500:],
                "stderr_tail": str(exc.stderr or b"")[-1500:].decode(errors="replace") if isinstance(exc.stderr, bytes) else str(exc.stderr or "")[-1500:],
                "output": str(output.relative_to(REPO))}


def execute(records):
    results = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = {pool.submit(run_export, rec): rec for rec in records}
        for future in as_completed(futures):
            rec = futures[future]
            try:
                outcome = future.result()
            except Exception as exc:
                outcome = {"status": "worker_error", "seconds": None, "returncode": None,
                           "error": f"{type(exc).__name__}: {exc}",
                           "output": rec["added10_native_argv"][rec["added10_native_argv"].index("--output") + 1]}
            results.append({"profile": rec["profile"], "control": rec["control"], "index": rec["index"],
                            "profile_configuration": rec["profile_configuration"],
                            "score_configuration": rec["score_configuration"],
                            "gate_configuration": rec["gate_configuration"], **outcome})
    return sorted(results, key=lambda x: (x["profile"], x["control"], x["index"]))


def score_added(records):
    score_plan = read(REPRO / "followup-score-plan.json")
    scored = []
    for profile in PROFILE_PREFIXES:
        for control in CONTROLS:
            cell = profile + "-" + control
            job = next(j for j in score_plan["jobs"] if j["name"] == f"{cell}-batch00")
            argv = list(job["argv"])
            selected = [r for r in records if r["profile"] == profile and r["control"] == control]
            input_args = [str(REPO / r["output"]) for r in selected]
            output_index = argv.index("--output")
            argv = argv[:2] + input_args + argv[output_index:]
            out = OUT / "scores" / f"{cell}-added10.json"
            out.parent.mkdir(parents=True, exist_ok=True)
            argv[argv.index("--output") + 1] = str(out)
            argv[0] = sys.executable
            argv[1] = str(REPO / "tools/triad-conditional-sweep.py")
            di = argv.index("--device") + 1
            pi = argv.index("--precision") + 1
            need(argv[di] == "cuda:0" and argv[pi] == "fp32", f"unexpected baseline score precision: {cell}")
            argv[di] = "cpu"
            argv[pi] = "fp64"
            env = os.environ.copy()
            env.update({"OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1", "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1", "CUDA_VISIBLE_DEVICES": ""})
            done = subprocess.run(argv, cwd=REPO, env=env, text=True, capture_output=True, timeout=300, check=False)
            need(done.returncode == 0, f"added10 score failed {cell}: {done.stderr[-2000:]}")
            report = read(out)
            npz = out.with_name(out.stem + ".scores.npz")
            need(sha(npz) == report["scores_sha256"], f"added10 NPZ hash mismatch {cell}")
            for record in selected:
                local = REPO / record["output"]
                original_cell = record["profile"] + "-" + record["control"]
                row = next(r for r in report["paths"] if r["input"] == str(local))
                base_job, base_report_path, base_npz, base_report, base_row = saved_baseline("followup", original_cell, record["index"])
                before = score_values(base_npz, base_row, record["gate_configuration"])
                after = score_values(npz, row, record["gate_configuration"])
                scored.append({"profile": profile, "control": control, "index": record["index"],
                               "added10_report": str(out.relative_to(REPO)), "added10_report_sha256": sha(out),
                               "added10_scores_sha256": sha(npz), "baseline": before, "added10": after,
                               "delta_by_bit": [after["scores_by_bit"][k] - before["scores_by_bit"][k] for k in (0, 1)],
                               "maximum_absolute_score_delta": max(abs(after["scores_by_bit"][k] - before["scores_by_bit"][k]) for k in (0, 1)),
                               "winner_unchanged": before["winner_bit"] == after["winner_bit"],
                               "strict_admission_unchanged": before["strict_admitted"] == after["strict_admitted"],
                               "relaxed_admission_unchanged": before["relaxed_admitted"] == after["relaxed_admitted"]})
    return scored


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--execute", action="store_true", help="run exports/scoring after independent plan review")
    args = parser.parse_args()
    need(BINARY.is_file() and os.access(BINARY, os.X_OK), f"local binary unavailable: {BINARY}")
    records = preflight()
    profiles = {p: next(r["profile_configuration"] for r in records if r["profile"] == p) for p in PROFILE_PREFIXES}
    score_settings = {p: next(r["score_configuration"] for r in records if r["profile"] == p) for p in PROFILE_PREFIXES}
    gates = {p: next(r["gate_configuration"] for r in records if r["profile"] == p) for p in PROFILE_PREFIXES}
    summary = {"schema": "triad-selective-added-symbols-probe-v1",
               "state": "preflight_complete_execution_not_run",
               "interpretation": "Post-hoc paired diagnostic over the first eight followup indices; not an independent holdout or causal estimate across configurations.",
               "source": {"native_exporter": "tools/triad-long.cpp", "added_interleave_assignment": "f.added_interleave=true", "added_interleave_source_line": 560,
                          "campaign_wrapper_added_value": 1, "wrapper_limitation": "tools/triad-lowrate-campaign.py hard-codes --added 1; probe executes the exact retained native child command directly."},
               "design": {"profiles_from_original_path_manifests": profiles, "controls": list(CONTROLS), "indices": list(INDICES),
                          "export_count": len(records), "added_values": [1, 10], "frozen_score_settings_by_profile": score_settings,
                          "strict_relaxed_gates_by_profile": gates,
                          "same_original_six_seed_roots_per_path": True, "bit_balance_per_cell": {k: sum(r["expected_source_bit"] for r in records if r["profile"] + "-" + r["control"] == k) for k in (p + "-" + c for p in PROFILE_PREFIXES for c in CONTROLS)},
                          "export_parallelism": 2, "threads_per_export": 1, "timeout_seconds_per_path": 300,
                          "only_export_argv_changes": ["binary path", "fresh output directory", "--added 1 to --added 10"],
                          "score_argv_changes": ["input workspaces", "fresh output path", "device CUDA to CPU", "precision FP32 to FP64"]},
               "preflight": records,
               "execution": None,
               "score_comparisons": None,
               "limitations": ["Added Symbols changes the waveform and rate distribution; fixed Fs and requested duration do not guarantee equal useful energy after shaping or identical retained-sample support.",
                               "Control comparisons share source/channel/noise roots where verified but the keyed waveform differs; these selected indices were chosen after prior results.",
                               "CPU FP64 scores compare with existing GPU FP32 baselines; arithmetic/backend differences are included.",
                               "No new independent Gaussian-null trials or daily false-admission qualification are included."]}
    OUT.mkdir(parents=True, exist_ok=True)
    target = REPRO / "added-probe-summary.json"
    def save():
        target.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    save()
    if args.execute:
        exports = execute(records)
        summary["execution"] = exports
        if len(exports) != 32 or not all(r["status"] == "completed" for r in exports):
            summary["state"] = "exports_incomplete_scoring_skipped"
            save()
        else:
            summary["state"] = "exports_complete_scoring"
            save()
            try:
                summary["score_comparisons"] = score_added(exports)
                summary["state"] = "completed"
            except Exception as exc:
                summary["state"] = "scoring_failed"
                summary["score_error"] = f"{type(exc).__name__}: {exc}"
            save()
    print(json.dumps({"summary": str(target.relative_to(REPO)), "state": summary["state"],
                      "paths": len(records), "binary_sha256": sha(BINARY)}, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Recount matched receiver, precision and long-blocking comparisons.

This is a compact, descriptive summary of already-completed development
experiments. It verifies the accepted analyses against report/NPZ hashes and
recounts their arrays; it does not create new trials or make operational,
continuous-duration, or reception-probability qualifications.
"""
from __future__ import annotations

from collections import Counter
import hashlib
from itertools import combinations
import json
import math
from pathlib import Path
import re

import numpy as np


ROOT = Path(__file__).resolve().parents[5]
EVIDENCE = ROOT / "local-evidence/triad-selective/development"
OUT = Path(__file__).resolve().with_name("comparisons-summary.json")
SELECTIVE = ROOT / "docs/validation-data/triad-v3/2026-11-02-selective/reproduction"
SPREAD_ANALYZER = ROOT / "docs/validation-data/triad-v3/2026-11-02-spread/analyze.py"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text())


def require(ok: bool, message: str):
    if not ok:
        raise ValueError(message)


def module_from(path: Path):
    import importlib.util
    spec = importlib.util.spec_from_file_location("selective_spread_reference", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


REFERENCE = module_from(SPREAD_ANALYZER)
INPUT_HASHES: dict[str, str] = {}


def record_hash(path: Path) -> str:
    path = path.resolve()
    digest = sha256(path)
    rel = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
    previous = INPUT_HASHES.get(rel)
    require(previous is None or previous == digest, f"input changed during summary: {rel}")
    INPUT_HASHES[rel] = digest
    return digest


def workspace_path(original: str) -> Path:
    prefix = "/workspace/triad/local-evidence/selective/"
    require(original.startswith(prefix), f"unexpected workspace path prefix: {original}")
    local = EVIDENCE / original[len(prefix):]
    local = local.resolve()
    require(local.is_relative_to(EVIDENCE.resolve()), f"workspace path escapes evidence root: {original}")
    return local


def validate_analysis(name: str, path: Path) -> tuple[dict, dict[str, dict]]:
    doc = read_json(path)
    record_hash(path)
    require(doc.get("schema") == "triad-selective-screen-analysis-v1", f"{name}: schema mismatch")
    require(doc.get("passed_integrity_and_complete_execution") is True, f"{name}: accepted analysis not complete")
    analysis_source = SELECTIVE / "analyze_screen.py"
    require(doc.get("source_sha256") == sha256(analysis_source), f"{name}: analysis script SHA mismatch")
    record_hash(analysis_source)
    expected_analysis_deps = {
        "spread_reference_analyzer": ROOT / "docs/validation-data/triad-v3/2026-11-02-spread/analyze.py",
        "retention_diagnostic": SELECTIVE / "retention.py",
    }
    for key, source in expected_analysis_deps.items():
        require(doc.get("dependency_sha256", {}).get(key) == sha256(source), f"{name}: analysis dependency hash mismatch: {key}")
        record_hash(source)
    plan_path = ROOT / doc["score_plan"]["path"]
    require(plan_path.is_file(), f"{name}: score plan missing: {plan_path}")
    require(sha256(plan_path) == doc["score_plan"]["sha256"], f"{name}: score plan hash mismatch")
    record_hash(plan_path)
    cases = {}
    for case in doc["cases"]:
        require(case.get("status") == "completed", f"{name}/{case.get('name')}: analysis case incomplete")
        report_path = EVIDENCE / case["report"]
        require(report_path.is_file(), f"{name}/{case['name']}: score JSON missing")
        report_sha = record_hash(report_path)
        require(report_sha == case.get("report_sha256"), f"{name}/{case['name']}: score JSON hash differs from accepted analysis")
        report = read_json(report_path)
        source_path = ROOT / "tools/triad-conditional-sweep.py"
        require(report.get("source_sha256") == sha256(source_path), f"{name}/{case['name']}: scorer source hash mismatch")
        record_hash(source_path)
        for dependency, expected in report.get("dependency_sha256", {}).items():
            dependency_path = ROOT / "tools" / dependency
            require(dependency_path.is_file() and sha256(dependency_path) == expected,
                    f"{name}/{case['name']}: scorer dependency hash mismatch: {dependency}")
            record_hash(dependency_path)
        npz_path = report_path.parent / report["scores_file"]
        require(npz_path.is_file(), f"{name}/{case['name']}: score NPZ missing")
        npz_sha = record_hash(npz_path)
        require(npz_sha == report.get("scores_sha256") == case.get("scores_sha256"),
                f"{name}/{case['name']}: score NPZ hash differs from report/analysis")
        require(report.get("failed_physical_path_records") == 0 and
                all(r.get("status") == "completed" for r in report.get("paths", [])),
                f"{name}/{case['name']}: raw report contains failed paths")
        cases[case["name"]] = {"analysis": case, "report": report, "report_path": report_path, "npz_path": npz_path}
    require(len(cases) == doc["summary"]["completed_jobs"], f"{name}: analyzed case count mismatch")
    return doc, cases


def verify_raw_receipts(report: dict) -> dict[str, object]:
    checks = []; unavailable = []
    for row in report["paths"]:
        workspace = workspace_path(row["input"])
        receipts = row.get("sampled_input_hashes", row.get("input_hashes", {}))
        require(isinstance(receipts, dict) and receipts, "score report missing retained workspace hash receipts")
        for filename, expected in receipts.items():
            file = workspace / filename
            if not file.is_file():
                unavailable.append(str(file.relative_to(ROOT)) if file.is_relative_to(ROOT) else str(file))
                continue
            actual = sha256(file)
            require(actual == expected, f"retained workspace file SHA mismatch: {file}")
            record_hash(file)
            checks.append(str(file.relative_to(ROOT)) if file.is_relative_to(ROOT) else str(file))
    return {"files_rehashed": checks, "receipt_files_missing_from_local_snapshot": unavailable}


def recount_report(item: dict) -> dict:
    report = item["report"]
    cfg = report["configuration"]
    gates = REFERENCE.gates(cfg)
    archive = np.load(item["npz_path"], allow_pickle=False)
    used: set[str] = set()
    points = {}
    timing = {"draw_and_calibration_seconds": 0.0, "receiver_evaluation_seconds": {},
              "lazy_preparation_seconds_subcomponent": {}, "conditional_trials": 0}
    for path in report["paths"]:
        require(path.get("status") == "completed", "completed report has incomplete path")
        truth = path.get("truth_bit_simulation_only")
        if truth is None:
            truth = read_json(workspace_path(path["input"]) / "manifest.json").get("signal_bit")
        for condition in path["conditions"]:
            control = condition["control"]
            cn0 = condition["cn0_db_hz"]
            require(type(cn0) in (int, float) and math.isfinite(cn0), "invalid conditional C/N0")
            trials = condition["noise_trials"]
            timing["conditional_trials"] += trials
            timing["draw_and_calibration_seconds"] += condition.get("sampler_and_ucb_seconds", 0.0)
            identity = (float(cn0), control)
            point = points.setdefault(identity, {"trials": 0, "physical_paths": set(), "strict": {}, "relaxed": {}})
            point["trials"] += trials
            point["physical_paths"].add(path["input"])
            seed_meta = condition.get("draws_simulation_only", [])
            require(seed_meta, "conditional condition lacks RNG schedule metadata")
            for method, method_doc in condition["methods"].items():
                key = method_doc["score_array"]
                require(key in archive.files and key not in used, f"missing or reused score array: {key}")
                used.add(key)
                scores = np.asarray(archive[key], dtype=np.float64)
                require(scores.shape == (trials, 2) and np.isfinite(scores).all(), f"invalid score matrix: {key}")
                require(math.isclose(method_doc["threshold"], gates["strict"], rel_tol=0, abs_tol=1e-9),
                        f"strict threshold mismatch: {key}")
                strict = REFERENCE.recount(scores, truth, control, gates["strict"], "trials")
                require(strict == method_doc["counts"], f"strict array recount mismatch: {key}")
                relaxed = REFERENCE.recount(scores, truth, control, gates["relaxed"], "trials")
                point["strict"].setdefault(method, Counter()).update(strict)
                point["relaxed"].setdefault(method, Counter()).update(relaxed)
                timing["receiver_evaluation_seconds"][method] = timing["receiver_evaluation_seconds"].get(method, 0.) + method_doc.get("receiver_seconds", 0.)
                diag = method_doc.get("diagnostics_last_batch", {})
                timing["lazy_preparation_seconds_subcomponent"][method] = timing["lazy_preparation_seconds_subcomponent"].get(method, 0.) + diag.get("preparation_seconds_this_call", 0.)
    require(set(archive.files) == used, f"NPZ contains unreferenced arrays in {item['npz_path']}")
    archive.close()
    points_out = {}
    for (cn0, control), value in sorted(points.items()):
        methods = {}
        for method in value["strict"]:
            strict = dict(value["strict"][method]); relaxed = dict(value["relaxed"][method])
            entry = {"strict": strict, "relaxed": relaxed}
            if control == "signal":
                entry["raw_bit_errors"] = strict["trials"] - strict["raw_correct"]
            methods[method] = entry
        points_out[f"{cn0:g}/{control}"] = {"cn0_db_hz": cn0, "control": control,
            "physical_path_count": len(value["physical_paths"]), "noise_draw_trials": value["trials"], "methods": methods}
    timing["report_wall_seconds"] = report.get("wall_seconds")
    timing["scope"] = "Recorded serial stage timings. Lazy preparation is included inside receiver evaluation; it is shown as a subcomponent. Wall timing includes source loading, writes and other overhead and was measured under shared contention."
    return {"points": points_out, "timing": timing, "configuration": report["configuration"],
            "rng_by_path_condition": _rng_schedule(report),
            "input_hashes_by_path": {r["input"]: r.get("sampled_input_hashes", r.get("input_hashes", {})) for r in report["paths"]}}


def _rng_schedule(report: dict) -> dict[str, dict[str, list[dict]]]:
    result = {}
    for path in report["paths"]:
        conditions = {}
        for condition in path["conditions"]:
            key = f"{condition['cn0_db_hz']:g}/{condition['control']}"
            conditions[key] = condition.get("draws_simulation_only", [])
        result[path["input"]] = conditions
    return result


ALT_SUFFIXES = {
    "-gaussian-j16": [("gaussian-window-J16", "gaussian-window-ucb")],
    "-gaussian-j128": [("gaussian-window-J128", "gaussian-window-ucb")],
    "-gaussian-bhalf-j64": [("gaussian-window-B-half-J64", "gaussian-window-ucb")],
    "-gaussian-bdouble-j64": [("gaussian-window-B-double-J64", "gaussian-window-ucb")],
    "-ou-static": [("Gaussian-OU-UCB", "gaussian-ou-ucb"),
                   ("static-FFT-UCB", "static-fft-ucb")],
}


def verify_matched(baseline: dict, alternative: dict, baseline_results: dict, alt_results: dict) -> dict:
    base = baseline["report"]; alt = alternative["report"]
    require(baseline_results["input_hashes_by_path"] == alt_results["input_hashes_by_path"],
            "matched receiver comparisons do not share exact retained-input hash receipts")
    require(baseline_results["rng_by_path_condition"] == alt_results["rng_by_path_condition"],
            "matched receiver comparisons use different noise RNG schedule/data")
    require([p["input"] for p in base["paths"]] == [p["input"] for p in alt["paths"]],
            "matched receiver comparisons differ in physical path sequence")
    return {"same_ordered_physical_paths": True, "same_retained_input_hash_receipts": True,
            "same_rng_schedule_and_draw_metadata": True,
            "physical_paths": len(base["paths"]), "rng_cells_checked": sum(len(x) for x in baseline_results["rng_by_path_condition"].values())}


def summarize_alternatives(screen_doc: dict, screen_cases: dict, alternatives_doc: dict, alternatives_cases: dict) -> dict:
    base_names = ("g001-each-65", "g01-each-65", "g1-each-65", "g1-slowrate-each-65", "g1-fastam-each-65")
    require(len(alternatives_cases) == 25, "expected all 25 receiver-alternative jobs")
    baseline_results = {}
    baseline_raw_hash_status = {}
    for name in base_names:
        require(name in screen_cases, f"baseline case missing: {name}")
        baseline_results[name] = recount_report(screen_cases[name])
        baseline_raw_hash_status[name] = verify_raw_receipts(screen_cases[name]["report"])
    alt_results = {name: recount_report(item) for name, item in alternatives_cases.items()}
    alternative_raw_hash_status = {name: verify_raw_receipts(item["report"])
                                   for name, item in alternatives_cases.items()}
    cases_out = []
    global_counts = Counter()
    matched_jobs = 0
    for alt_name, item in alternatives_cases.items():
        suffix = next((s for s in ALT_SUFFIXES if alt_name.endswith(s)), None)
        require(suffix is not None, f"unrecognized receiver alternative: {alt_name}")
        baseline_name = alt_name[:-len(suffix)]
        require(baseline_name in base_names, f"alternative is not one of the planned matched cells: {alt_name}")
        baseline_item = screen_cases[baseline_name]
        match = verify_matched(baseline_item, item, baseline_results[baseline_name], alt_results[alt_name])
        match["baseline_raw_hash_status"] = baseline_raw_hash_status[baseline_name]
        match["alternative_raw_hash_status"] = alternative_raw_hash_status[alt_name]
        matched_jobs += 1
        base_points = baseline_results[baseline_name]["points"]
        alt_points = alt_results[alt_name]["points"]
        require(set(base_points) == set(alt_points), f"{alt_name}: C/N0/control cells differ from baseline")
        for method_label, method in ALT_SUFFIXES[suffix]:
            case_out = {"alternative_case": alt_name, "baseline_case": baseline_name,
                        "alternative_label": method_label, "compared_method": method,
                        "matched_data_audit": match, "outcomes": []}
            for key in sorted(base_points, key=lambda x: (float(x.split("/")[0]), x.split("/")[1])):
                bpoint = base_points[key]; apoint = alt_points[key]
                require(method in apoint["methods"], f"{alt_name}: planned method output missing: {method}")
                base_method = bpoint["methods"]["gaussian-window-ucb"]
                alt_method = apoint["methods"][method]
                require(apoint["noise_draw_trials"] == bpoint["noise_draw_trials"] and
                        apoint["physical_path_count"] == bpoint["physical_path_count"],
                        f"{alt_name}: physical-path/noise-trial denominators differ")
                delta = {}
                count_field = "false_admitted" if bpoint["control"] == "null" else "correct_accepted"
                for gate in ("strict", "relaxed"):
                    delta[gate] = alt_method[gate][count_field] - base_method[gate][count_field]
                    global_counts[f"{bpoint['control']}/{gate}/{count_field}"] += delta[gate]
                case_out["outcomes"].append({"cn0_db_hz": bpoint["cn0_db_hz"], "control": bpoint["control"],
                    "physical_path_count": bpoint["physical_path_count"], "conditional_noise_trials": bpoint["noise_draw_trials"],
                    "baseline_gaussian_window": base_method, "alternative": alt_method,
                    "alternative_minus_baseline_correct_or_false_admission_count": delta})
            case_out["timings"] = {"baseline": baseline_results[baseline_name]["timing"],
                                    "alternative": alt_results[alt_name]["timing"]}
            cases_out.append(case_out)
    # The six alternative receiver modes and screen baseline are present for all
    # five matched physical cases; this count is descriptive only.
    require(matched_jobs == 25 and len(cases_out) == 30, "matched comparison job/method coverage incomplete")
    return {"selected_matched_cells": list(base_names), "baseline_receiver": "gaussian-window-ucb",
            "alternative_receiver_families": sorted({label for variants in ALT_SUFFIXES.values() for label, _ in variants}),
            "comparisons": cases_out, "matched_export_job_count": matched_jobs,
            "matched_receiver_comparison_count": len(cases_out),
            "interpretation": "Seven receiver choices per matched case means the screen baseline plus six alternatives. These are conditional noise draws on four fixed physical paths, not independent channel trials. Raw bit errors, wrong accepted bits, misses, null false admissions and noise-draw counts remain separate. No pooled confidence interval or new-channel claim is made."}


def summarize_bench():
    names = ("g001-cpu-fp32-blas1", "g001-cuda-0-fp32-blas1", "g001-cpu-fp64-blas1")
    reports = {}
    results = {}
    for name in names:
        path = EVIDENCE / "bench" / f"{name}.json"
        require(path.is_file(), f"benchmark report missing: {path}")
        report = read_json(path); record_hash(path)
        source_path = ROOT / "tools/triad-conditional-sweep.py"
        require(report.get("source_sha256") == sha256(source_path), f"benchmark scorer source hash mismatch: {name}")
        record_hash(source_path)
        for dependency, expected in report.get("dependency_sha256", {}).items():
            dependency_path = ROOT / "tools" / dependency
            require(dependency_path.is_file() and sha256(dependency_path) == expected,
                    f"benchmark scorer dependency hash mismatch: {dependency}")
            record_hash(dependency_path)
        npz = path.with_suffix(".scores.npz")
        require(npz.is_file() and sha256(npz) == report["scores_sha256"], f"benchmark score NPZ hash mismatch: {npz}")
        record_hash(npz)
        item = {"report": report, "report_path": path, "npz_path": npz}
        results[name] = recount_report(item)
        raw_hash_status = verify_raw_receipts(report)
        reports[name] = report
        results[name]["raw_hash_status"] = raw_hash_status
    comparisons = []
    for baseline, other in combinations(names, 2):
        a = results[baseline]; b = results[other]
        require(a["input_hashes_by_path"] == b["input_hashes_by_path"], f"benchmark input hashes differ: {baseline} vs {other}")
        require(a["rng_by_path_condition"] == b["rng_by_path_condition"], f"benchmark RNG schedule differs: {baseline} vs {other}")
        base_npz = np.load(EVIDENCE / "bench" / f"{baseline}.scores.npz", allow_pickle=False)
        other_npz = np.load(EVIDENCE / "bench" / f"{other}.scores.npz", allow_pickle=False)
        require(set(base_npz.files) == set(other_npz.files), f"benchmark NPZ key set differs: {other}")
        score_abs = 0.0; same_rank = same_gate = n = 0
        thresholds = set()
        for report in (reports[baseline], reports[other]):
            for path in report["paths"]:
                for condition in path["conditions"]:
                    for method in condition["methods"].values(): thresholds.add(method["threshold"])
        require(len(thresholds) == 1, "benchmark family uses different strict gates")
        threshold = next(iter(thresholds))
        for key in base_npz.files:
            x = np.asarray(base_npz[key], dtype=np.float64); y = np.asarray(other_npz[key], dtype=np.float64)
            require(x.shape == y.shape, f"benchmark score shape differs: {key}")
            score_abs = max(score_abs, float(np.max(np.abs(x-y))))
            same_rank += int(np.count_nonzero(np.argmax(x, axis=1) == np.argmax(y, axis=1)))
            same_gate += int(np.count_nonzero((x.max(axis=1) >= threshold) == (y.max(axis=1) >= threshold)))
            n += x.shape[0]
        base_npz.close(); other_npz.close()
        comparisons.append({"baseline": baseline, "other": other,
            "same_retained_input_hash_receipts": True, "same_noise_RNG_schedule": True,
            "score_arrays_compared": len(base_npz.files), "score_trials_compared": n,
            "maximum_absolute_log_score_difference": score_abs,
            "winner_rank_agreement_fraction": same_rank/n, "strict_gate_agreement_fraction": same_gate/n,
            "strict_gate_log_threshold": threshold})
    timing = {name: results[name]["timing"] for name in names}
    return {"reports": names, "configuration_by_report": {n: {k: reports[n]["configuration"].get(k)
            for k in ("device", "precision", "threads", "window_blocks", "seed", "noise_trials", "trial_batch")}
            for n in names}, "all_pairwise_comparisons": comparisons, "timing": timing,
        "interpretation": "Same input hash receipts and RNG schedule were confirmed. Rank/gate agreement and score deltas are measured on these exact arrays. Timings were contended and are not an acceleration claim or a representative isolated hardware benchmark."}


def db_ratio(a: float, b: float) -> float:
    require(a > 0 and b > 0, "retention fractions must be positive")
    return 10 * math.log10(a / b)


def summarize_long():
    audit_path = EVIDENCE / "long-refinement-audit.json"
    audit = read_json(audit_path); record_hash(audit_path)
    audit_tool = SELECTIVE / "audit_refinement.py"
    helper_tool = ROOT / "docs/validation-data/triad-v3/2026-11-02-spread/reproduction/audit_compression.py"
    require(audit.get("source_sha256") == sha256(audit_tool) and audit.get("helper_sha256") == sha256(helper_tool),
            "long refinement audit source/helper hash mismatch")
    record_hash(audit_tool); record_hash(helper_tool)
    require(audit.get("passed") is True and audit.get("pairs_expected") == 8 and audit.get("pairs_passed") == 8,
            "8192-to-4096 refinement audit did not pass all eight matched paths")
    retention = {}
    for folder in ("long-retention", "long-refinement-retention"):
        for mode in ("stable850-off-m60", "stable850-each-65-m60"):
            path = EVIDENCE / folder / f"{mode}.json"
            doc = read_json(path); record_hash(path)
            require(doc.get("passed") is True, f"retention report failed: {path}")
            require(doc.get("source_sha256") == sha256(SELECTIVE / "retention.py"), "retention source hash mismatch")
            retention[(folder, mode)] = {w["input"]: w for w in doc["workspaces"]}
    pair_results = []
    max_correct_db = 0.0; max_fullbank_db = 0.0
    for pair in audit["pairs"]:
        require(pair.get("passed") is True and pair.get("complete_capture_coverage") is True and
                pair.get("metadata_parity") is True and pair.get("command_parity_except_block_and_output") is True,
                "long reblocking pair lacks complete matching coverage")
        require(pair.get("parent_block_seconds") == 8192 and pair.get("refinement_block_seconds") == 4096,
                "unexpected long-refinement block duration")
        for metric in pair["statistics"].values():
            require(metric.get("passed") is True, "long-refinement sufficient-statistics comparison failed")
        for side in ("parent", "refinement"):
            for name, value in pair["file_provenance"][side].items():
                require(re.fullmatch(r"[0-9a-f]{64}", value.get("sha256", "")) is not None,
                        f"invalid retained source file hash receipt: {name}")
        mode = pair["mode"]
        key = "stable850-" + mode + "-m60"
        a = retention[("long-retention", key)][pair["parent_input"]]
        b = retention[("long-refinement-retention", key)][pair["refinement_input"]]
        require(a["status"] == b["status"] == "completed" and a["truth_bit_simulation_only"] == b["truth_bit_simulation_only"],
                "long retention truth/status mismatch")
        require(a["sample_count"] == b["sample_count"] == pair["parent_samples"],
                "long retention capture sample calendar differs")
        correct_db = db_ratio(b["correct_label_retained_fraction"], a["correct_label_retained_fraction"])
        full_db = db_ratio(b["full_bank_retained_fraction"], a["full_bank_retained_fraction"])
        max_correct_db = max(max_correct_db, abs(correct_db)); max_fullbank_db = max(max_fullbank_db, abs(full_db))
        pair_results.append({"mode": mode, "physical_path_index": int(pair["parent_input"].rsplit("-", 1)[1]),
            "sample_count": pair["parent_samples"], "parent_block_seconds": pair["parent_block_seconds"],
            "refinement_block_seconds": pair["refinement_block_seconds"],
            "capture_coverage_complete": pair["complete_capture_coverage"],
            "same_metadata_and_source_identity": pair["metadata_parity"],
            "exact_file_hash_receipts_present_for_both_outputs": True,
            "all_sufficient_statistics_aggregate_checks_passed": all(v["passed"] for v in pair["statistics"].values()),
            "correct_label_retained_fraction_parent": a["correct_label_retained_fraction"],
            "correct_label_retained_fraction_4096": b["correct_label_retained_fraction"],
            "correct_label_retention_change_db_4096_vs_8192": correct_db,
            "full_bank_retained_fraction_parent": a["full_bank_retained_fraction"],
            "full_bank_retained_fraction_4096": b["full_bank_retained_fraction"],
            "full_bank_retention_change_db_4096_vs_8192": full_db,
            "reported_source_hashes_rehashed_here": False})
    require(len(pair_results) == 8, "expected eight matched long reblocking observations")
    long_scores = {}
    for folder in ("long-scores", "long-refinement-scores"):
        for mode in ("stable850-off-m60", "stable850-each-65-m60"):
            path = EVIDENCE / folder / f"{mode}.json"
            doc = read_json(path); record_hash(path)
            scorer_source = ROOT / "tools/triad-conditional-sweep.py"
            require(doc.get("source_sha256") == sha256(scorer_source), f"long score source hash mismatch: {path}")
            record_hash(scorer_source)
            for dependency, expected in doc.get("dependency_sha256", {}).items():
                dependency_path = ROOT / "tools" / dependency
                require(dependency_path.is_file() and sha256(dependency_path) == expected,
                        f"long score dependency hash mismatch: {dependency}")
                record_hash(dependency_path)
            npz = path.with_suffix(".scores.npz")
            require(sha256(npz) == doc["scores_sha256"], f"long score NPZ hash mismatch: {npz}")
            record_hash(npz)
            long_scores[(folder, mode)] = (doc, npz)
    score_reblocking = []
    for mode in ("stable850-off-m60", "stable850-each-65-m60"):
        coarse, coarse_npz_path = long_scores[("long-scores", mode)]
        fine, fine_npz_path = long_scores[("long-refinement-scores", mode)]
        coarse_npz = np.load(coarse_npz_path, allow_pickle=False); fine_npz = np.load(fine_npz_path, allow_pickle=False)
        same_winner = same_gate = 0; n = 0; max_score_error = 0.0
        coarse_by_index = {p["physical_path_index"]: p for p in coarse["paths"]}
        for path in fine["paths"]:
            base = coarse_by_index[path["physical_path_index"]]
            bit_a = base["truth_bit_simulation_only"]; bit_b = path["truth_bit_simulation_only"]
            require(bit_a == bit_b and base["input"].rsplit("/", 1)[-1] == path["input"].rsplit("/", 1)[-1],
                    "long score overlap is not the same indexed physical path")
            ca = base["conditions"][0]["methods"]["static-fft-ucb"]
            cb = path["conditions"][0]["methods"]["static-fft-ucb"]
            xa = np.asarray(coarse_npz[ca["score_array"]], dtype=np.float64)
            xb = np.asarray(fine_npz[cb["score_array"]], dtype=np.float64)
            require(xa.shape == xb.shape == (1, 2) and ca["threshold"] == cb["threshold"], "long score array/gate mismatch")
            max_score_error = max(max_score_error, float(np.max(np.abs(xa-xb))))
            same_winner += int(np.argmax(xa) == np.argmax(xb))
            same_gate += int((xa.max() >= ca["threshold"]) == (xb.max() >= cb["threshold"]))
            n += 1
        score_reblocking.append({"mode": mode, "parent_paths": len(coarse["paths"]), "refined_paths": len(fine["paths"]),
            "overlapping_paths_compared": n, "winner_agreement": same_winner, "admission_agreement": same_gate,
            "max_absolute_log_score_difference": max_score_error,
            "parent_report_wall_seconds": coarse["wall_seconds"], "refinement_report_wall_seconds": fine["wall_seconds"]})
        coarse_npz.close(); fine_npz.close()
    return {"requested_observation_seconds": 850 * 86400,
            "scope": "Compressed simulator sufficient-statistics observations at one requested 850-day duration; neither a continuous field observation nor an 850-day reception qualification.",
            "refinement_audit_sha256": sha256(audit_path), "passed_pairs": 8,
            "maximum_absolute_correct_label_retention_change_db": max_correct_db,
            "maximum_absolute_full_bank_retention_change_db": max_fullbank_db,
            "matched_pairs": pair_results, "static_fft_score_reblocking": score_reblocking,
            "source_hash_scope": "The refinement audit records complete per-file SHA-256 receipts for parent/refined exports. Raw retained arrays were not included in the local evidence snapshot, so those receipts were not rehashed here."}


def build():
    record_hash(Path(__file__))
    screen_path = ROOT / "local-evidence/triad-selective/screen-analysis.json"
    alternatives_path = ROOT / "local-evidence/triad-selective/alternatives-analysis.json"
    screen_doc, screen_cases = validate_analysis("screen", screen_path)
    alternatives_doc, alternatives_cases = validate_analysis("alternatives", alternatives_path)
    alt_summary = summarize_alternatives(screen_doc, screen_cases, alternatives_doc, alternatives_cases)
    benchmark = summarize_bench()
    long_summary = summarize_long()
    return {"schema": "triad-selective-matched-comparisons-summary-v1",
        "scientific_qualification": False,
        "source_artifacts": {"accepted_screen_analysis": {"sha256": sha256(screen_path), "completed_cases": len(screen_cases)},
            "accepted_alternatives_analysis": {"sha256": sha256(alternatives_path), "completed_cases": len(alternatives_cases)},
            "all_report_and_npz_hashes_verified": True,
            "comparison_workspace_hash_receipts_matched_exactly": True,
            "raw_workspace_hash_rehash_scope": "Manifest bytes were available and rehashed for selected comparisons; retained array bytes were not in this local snapshot. Their scorer receipts are compared exactly between paired reports and remain bound by report/NPZ hashes.",
            "hashes": dict(sorted(INPUT_HASHES.items()))},
        "alternative_receiver_comparison": alt_summary,
        "precision_device_benchmark": benchmark,
        "long_duration_block_refinement": long_summary,
        "global_limits": [
            "All receiver outcome counts in the 5x alternative matrix are conditional noise draws on matched, reused development physical paths; do not treat the 16 noise replicates per path as independent channel trials.",
            "Precision/device timing runs used identical inputs and noise RNG schedules, but their shared-resource wall times are not a GPU acceleration benchmark.",
            "The 850-day rows are simulator-produced sufficient statistics and cannot establish continuous 850-day field reception, reliability or false-admission behavior.",
            "No default receiver, duration planner, channel model or operational probability target is changed by this descriptive comparison.",
        ]}


def main():
    result = build()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("x") as stream:
        json.dump(result, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"output": str(OUT), "input_hashes": len(INPUT_HASHES),
                      "matched_receiver_comparisons": result["alternative_receiver_comparison"]["matched_receiver_comparison_count"],
                      "benchmark_pairs": len(result["precision_device_benchmark"]["all_pairwise_comparisons"]),
                      "retention_pairs": result["long_duration_block_refinement"]["passed_pairs"]}))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Recount the sub-9-kHz study from receiver reports, score archives and manifests.

Usage: python analyze.py --input EVIDENCE_ROOT --output analysis.json
Requires NumPy and SciPy. No receiver is run and no simulation truth enters scoring.
An explicit --diagnostic-report RELATIVE_PATH excludes a duplicate diagnostic from
qualification; it remains listed in provenance. Missing score archives are allowed
only when the report itself retains both bit scores. Other integrity errors abort.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

import numpy as np
from scipy.stats import beta


SCHEMAS = {
    "triad-static-frequency-study-v1",
    "triad-rbpf-study-v1",
    "triad-noise-study-v1",
}
STRICT_GATE = 21.653222759584384
RELAXED_GATE = math.log(720 * 4 * 11 * 2 / (0.01 / 2))
ANCHORS = {"sub9-screen", "sub9-holdout", "month-pilot"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def close(a, b, message, atol=1e-10):
    require(math.isfinite(float(a)) and math.isfinite(float(b))
            and math.isclose(a, b, rel_tol=1e-10, abs_tol=atol), message)


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path):
    def invalid(value):
        raise ValueError(f"{path.name}: nonfinite JSON constant {value}")
    return json.loads(path.read_text(), parse_constant=invalid)


def identity(path):
    parts = Path(path).parts
    for i, part in enumerate(parts):
        if part in ANCHORS:
            return "/".join(parts[i:])
    return None


def manifest_identity(source_path, manifests):
    """Resolve relocated workspaces without guessing among repeated basenames."""
    anchored = identity(source_path)
    if anchored is not None:
        require(anchored in manifests, f"manifest not found for {source_path}")
        return anchored
    source_parts = Path(source_path).parts
    matches = [key for key in manifests
               if source_parts[-len(Path(key).parts):] == Path(key).parts]
    require(len(matches) == 1,
            f"expected one relocated manifest for {source_path}; found {len(matches)}")
    return matches[0]


def cp(k, n, alpha=0.05):
    """Each endpoint separately has one-sided 1-alpha coverage, not a joint CI."""
    require(0 <= k <= n, "invalid binomial counts")
    return {
        "successes": k, "trials": n,
        "fraction": k / n if n else None,
        "lower_one_sided_95": float(beta.ppf(alpha, k, n-k+1)) if k else 0.0,
        "upper_one_sided_95": float(beta.ppf(1-alpha, k+1, n-k)) if k < n else 1.0,
    }


def recount(scores, truth, control, gate):
    scores = np.asarray(scores, dtype=np.float64)
    require(scores.shape == (1, 2) and np.isfinite(scores).all(),
            "expected one actual capture with two finite bit log-likelihoods")
    winner = int(np.argmax(scores[0]))  # Match the receiver's bit-0 tie rule.
    admitted = bool(scores[0, winner] >= gate)
    if control == "signal":
        require(truth in (0, 1), "signal capture lacks binary truth")
        return {"captures": 1, "correct_accepted": int(admitted and winner == truth),
                "wrong_accepted": int(admitted and winner != truth),
                "missed": int(not admitted), "raw_correct": int(winner == truth)}
    require(control in ("null", "noise", "noise-only", "wrong-key", "wrong_key"),
            f"unknown control {control}")
    return {"captures": 1, "false_admitted": int(admitted),
            "not_admitted": int(not admitted)}


def bounds(records, gate_name, control):
    counts = Counter()
    for r in records:
        counts.update(r[gate_name])
    n = counts["captures"]
    result = {"counts": dict(counts)}
    if control != "signal":
        result["false_admission_per_capture_cp"] = cp(counts["false_admitted"], n)
        result["operational_rate_qualification"] = False
        return result
    result["pooled_correct_cp_if_iid"] = cp(counts["correct_accepted"], n)
    result["pooled_wrong_cp_if_iid"] = cp(counts["wrong_accepted"], n)
    strata = {}
    for bit in (0, 1):
        selected = [r for r in records if r["truth"] == bit]
        subtotal = Counter()
        for r in selected:
            subtotal.update(r[gate_name])
        strata[str(bit)] = {
            "counts": dict(subtotal),
            "correct_cp": cp(subtotal["correct_accepted"], len(selected)),
            "wrong_cp": cp(subtotal["wrong_accepted"], len(selected)),
        }
    result["by_transmitted_bit"] = strata
    n0, n1 = (strata[str(b)]["correct_cp"]["trials"] for b in (0, 1))
    balanced = bool(n0 and n0 == n1)
    design_known = all(r["fixed_bit_design_verified"] for r in records)
    conservative = None
    if n0 and n1:
        lowers = []
        for bit in (0, 1):
            s = strata[str(bit)]["correct_cp"]
            lowers.append(float(beta.ppf(.025, s["successes"],
                                           s["trials"]-s["successes"]+1))
                          if s["successes"] else 0.0)
        conservative = sum(lowers) / 2
    # With fixed balanced counts and independent within-stratum trials, all
    # successes has probability p0**n0*p1**n1 <= ((p0+p1)/2)**n by AM-GM.
    # This particular inversion is not valid for a general nonzero-error count.
    all_success = (0.05 ** (1/n) if balanced and design_known
                   and counts["correct_accepted"] == n else None)
    lower = all_success if all_success is not None else conservative
    result["fixed_balanced_bit_design"] = {
        "balanced_observed": balanced,
        "campaign_source_bits_verified": design_known,
        "balanced_population_mean_lower_95_bonferroni": conservative,
        "all_success_amgm_mean_lower_95": all_success,
        "applicable_mean_lower_95": lower if design_known else None,
        "pointwise_99_percent_mean_qualified": bool(design_known and lower is not None and lower >= .99),
        "pointwise_90_percent_mean_qualified": bool(design_known and lower is not None and lower >= .9),
        "assumptions": "See confidence_conventions.fixed_strata.",
    }
    return result


def interval(values):
    values = list(values)
    return {"min": min(values), "max": max(values)} if values else None


def manifest_metrics(manifest):
    rate = manifest["sample_rate"]
    result = {"sample_rate_hz": rate,
              "sampled_capture_seconds": manifest["sample_count"] / rate}
    for key in ("requested_active_seconds", "actual_elapsed_seconds",
                "nominal_payload_energy_seconds_lower", "cn0_db_hz"):
        if key in manifest:
            result[key] = manifest[key]
    for key in ("channel_signal_energy_samples", "channel_observed_energy_samples"):
        if key in manifest:
            result[key.replace("_samples", "_seconds")] = manifest[key] / rate
    if "channel_signal_energy_samples" in manifest:
        result["actual_signal_energy_over_reference_n0"] = (
            manifest["channel_signal_energy_samples"] / manifest["noise_variance"])
    fmt = manifest.get("source_format", {})
    for key in ("inner_rate_hz", "fine_rate_hz", "carrier_spacing_hz", "occupied_bandwidth_hz", "tile_count"):
        if key in fmt:
            result[key] = fmt[key]
    return result


def analyze_benchmark(report):
    output = {"schema": report["schema"], "scope": report["scope"],
              "arithmetic": report["arithmetic"],
              "campaign_seconds_including_warmups": report["campaign_seconds_including_warmups"],
              "cases": []}
    for case in report["cases"]:
        runs = {(r["device"], r["precision"]): r for r in case["runs"]}
        require(len(runs) == len(case["runs"]), "duplicate benchmark run")
        reference = np.array(runs[("cpu", "fp64")]["repeats"][0]["scores"])
        gate = report["configuration"]["gate_log_lr"]
        comparisons = []
        for key, run in runs.items():
            require(run["input_sha256"] == case["input_sha256"], "benchmark input identity mismatch")
            medians = run["median"]
            for field in ("core_seconds", "finish_seconds", "resident_total_seconds"):
                close(medians[field], statistics.median(x[field] for x in run["repeats"]),
                      f"benchmark {key} median {field}")
            close(medians["single_use_seconds"], case["preparation_seconds"] +
                  run["transfer_seconds"] + medians["resident_total_seconds"],
                  "benchmark single-use accounting")
            candidate = np.array(run["repeats"][0]["scores"])
            require(np.isfinite(candidate).all() and candidate.shape == reference.shape,
                    "invalid benchmark scores")
            difference = candidate-reference
            summary = {"device": key[0], "precision": key[1],
                       "maximum_absolute_log_lr_error": float(np.max(np.abs(difference))),
                       "rms_log_lr_error": float(np.sqrt(np.mean(difference**2))),
                       "top_rank_changed": bool(candidate.argmax() != reference.argmax()),
                       "rank_positions_changed": int(np.count_nonzero(np.argsort(candidate) != np.argsort(reference))),
                       "gate_decisions_changed": int(np.count_nonzero((candidate >= gate) != (reference >= gate))),
                       "minimum_reference_distance_to_gate": float(np.min(np.abs(reference-gate))),
                       "reference_passes_gate": int(np.count_nonzero(reference >= gate)),
                       "candidate_passes_gate": int(np.count_nonzero(candidate >= gate))}
            published = next(c for c in case["comparisons"]
                             if c["candidate"] == ":".join(key))
            for field, value in summary.items():
                if field in ("device", "precision"):
                    continue
                if isinstance(value, float):
                    close(published[field], value, f"benchmark comparison {field}")
                else:
                    require(published[field] == value, f"benchmark comparison {field}")
            # Compare matching-precision CPU and GPU, not FP64 CPU against FP32 GPU.
            if key[0] != "cpu":
                cpu = runs[("cpu", key[1])]["median"]
                speeds = {field: cpu[field]/medians[field] for field in
                          ("core_seconds", "resident_total_seconds", "single_use_seconds")}
                published_speed = next(s for s in case["speedups"]
                                       if (s["device"], s["precision"]) == key)
                for field, value in speeds.items():
                    close(published_speed[field], value, f"benchmark speedup {field}")
                summary["speedup_cpu_over_gpu_same_precision"] = speeds
            summary["median_seconds"] = medians
            summary["repeat_scores_identical"] = all(
                np.array_equal(candidate, np.array(repeat["scores"]))
                for repeat in run["repeats"])
            comparisons.append(summary)
        output["cases"].append({k: case[k] for k in ("batch", "particles", "blocks", "taps", "input_sha256")} |
                               {"comparisons": comparisons, "gate_log_lr": gate})
    output["qualification_limit"] = "Synthetic resident-kernel and single-use measurements; not end-to-end sampled receiver throughput or an exhaustive near-threshold precision qualification."
    return output


def analyze(root, diagnostic_reports=()):
    documents = {}
    for path in sorted(root.rglob("*.json")):
        # Our generated output is harmless to read; only recognized schema/format
        # documents and specifically named invocation histories are processed.
        documents[path] = read_json(path)
    relative = lambda p: p.relative_to(root).as_posix()
    manifests = {}
    campaigns = {}
    fixed_bits = {}
    for path, doc in documents.items():
        if not isinstance(doc, dict):
            continue
        if doc.get("format") == "triad-block-stats-v1":
            key = identity(path.parent) or relative(path.parent)
            require(key not in manifests, f"duplicate manifest {key}")
            manifests[key] = (doc, path)
        if doc.get("format") == "triad-lowrate-campaign-v1":
            key = identity(path.parent) or relative(path.parent)
            require(key not in campaigns, f"duplicate campaign {key}")
            campaigns[key] = (doc, path)
            completed = [row for row in doc["results"] if row.get("outcome") == "completed"]
            for seed in ("noise_seed", "channel_seed", "key_seed", "oscillator_seed", "frontend_seed", "path_seed"):
                require(len({row[seed] for row in completed}) == len(completed),
                        f"repeated physical-path {seed} within {key}")
            for row in doc["results"]:
                input_key = key + "/" + row["directory"]
                if row.get("outcome") == "completed":
                    require(row.get("source_bit") in (0, 1), f"missing campaign source bit {input_key}")
                    fixed_bits[input_key] = row["source_bit"]
    require(manifests, "no sampled manifests found")
    records, report_provenance, exclusions = [], [], []
    seen, seen_hashes = set(), set()
    physical = {}
    failed = []
    benchmarks = []
    for path, doc in documents.items():
        if not isinstance(doc, dict):
            continue
        schema = doc.get("schema")
        if schema == "triad-rbpf-synthetic-benchmark-v1":
            benchmarks.append(analyze_benchmark(doc) | {"report": relative(path), "sha256": sha256(path)})
        if schema not in SCHEMAS:
            continue
        if relative(path) in diagnostic_reports:
            exclusions.append({"report": relative(path), "sha256": sha256(path),
                               "reason": "explicit command-line diagnostic exclusion; not qualified"})
            continue
        config = doc["configuration"]
        if schema == "triad-noise-study-v1":
            require(config["mode"] == "actual", f"projected trials are not physical validation: {relative(path)}")
        for key, value in {"false_probability": .0001, "horizon_seconds": 86400,
                           "attempt_spacing": 60, "duration_alternatives": 4,
                           "selection_methods": 11, "calibration_alternatives": 2}.items():
            close(config[key], value, f"unexpected family {key}: {relative(path)}")
        scores_path = path.parent / Path(doc["scores_file"]).name
        archive = None
        if scores_path.is_file():
            require(sha256(scores_path) == doc["scores_sha256"], f"score archive hash mismatch: {relative(path)}")
            archive = np.load(scores_path, allow_pickle=False)
        used_keys = set()
        try:
            for capture in doc["captures"]:
                input_key = manifest_identity(capture["input"], manifests)
                manifest, manifest_path = manifests[input_key]
                expected_hash = capture.get("manifest_sha256", capture.get("input_hashes", {}).get("manifest.json"))
                if capture["status"] != "completed":
                    require(not capture["methods"],
                            f"failed capture retains partial method results; explicit reconciliation required: {input_key}")
                    supplied_hashes = [value for value in
                                       (capture.get("manifest_sha256"),
                                        capture.get("input_hashes", {}).get("manifest.json"))
                                       if value is not None]
                    if supplied_hashes:
                        actual_hash = sha256(manifest_path)
                        require(all(value == actual_hash for value in supplied_hashes),
                                f"failed capture manifest hash mismatch: {input_key}")
                    failed.append({"input": input_key, "report": relative(path),
                                   "physical_path_index": capture["physical_path_index"],
                                   "control": capture["control"], "status": capture["status"],
                                   "methods_requested": config["methods"],
                                   "manifest_hash_status": "verified" if supplied_hashes else "unavailable",
                                   "manifest_sha256": supplied_hashes[0] if supplied_hashes else None,
                                   "error_type": capture.get("error_type"),
                                   "error": capture.get("error"), "reason": capture.get("reason"),
                                   "included_in_scored_denominator": False})
                    continue
                require(expected_hash == sha256(manifest_path), f"manifest hash mismatch: {input_key}")
                control = capture["control"]
                require(manifest.get("control", "signal") == control, f"control mismatch: {input_key}")
                truth = capture.get("truth_bit_simulation_only")
                if control == "signal":
                    require(truth == manifest["signal_bit"], f"truth mismatch: {input_key}")
                    if input_key in fixed_bits:
                        require(truth == fixed_bits[input_key], f"campaign bit mismatch: {input_key}")
                close(config["cn0"], manifest["cn0_db_hz"], f"CN0 mismatch: {input_key}")
                physical[input_key] = {"cohort": str(Path(input_key).parent) if Path(input_key).name.startswith("path-") else input_key,
                                       "control": control, "truth": truth,
                                       "manifest_sha256": expected_hash,
                                       "metrics": manifest_metrics(manifest)}
                require(capture.get("conditional_noise_trials", 1) == 1,
                        "conditional replicates cannot enter the physical-path denominator")
                for method, detail in capture["methods"].items():
                    require((input_key, method) not in seen, f"duplicate input/method: {input_key} {method}")
                    seen.add((input_key, method))
                    raw_hash = capture.get("input_hashes", {}).get("observation.c128")
                    hash_key = (expected_hash, raw_hash, method)
                    if raw_hash:
                        require(hash_key not in seen_hashes, f"duplicate physical capture copied to {input_key}: {method}")
                        seen_hashes.add(hash_key)
                    threshold = detail["threshold"]
                    if isinstance(threshold, dict):
                        threshold = threshold["threshold_log_likelihood_ratio"]
                    close(threshold, STRICT_GATE, f"unexpected threshold: {input_key} {method}")
                    scores_key = f"path{capture['physical_path_index']:05d}_{control}_{method}"
                    inline = detail.get("bit_log_scores")
                    if archive is not None:
                        require(scores_key in archive, f"missing score key {scores_key}")
                        scores = archive[scores_key]
                        used_keys.add(scores_key)
                        if inline is not None:
                            require(np.array_equal(scores, np.array([inline])), f"inline/archive scores differ: {input_key} {method}")
                    else:
                        require(inline is not None, f"neither score archive nor inline scores: {relative(path)}")
                        scores = np.array([inline])
                    strict = recount(scores, truth, control, STRICT_GATE)
                    require(detail["counts"] == strict, f"published counts disagree: {input_key} {method}")
                    records.append({"input": input_key, "cohort": physical[input_key]["cohort"],
                                    "method": method, "control": control, "truth": truth,
                                    "fixed_bit_design_verified": input_key in fixed_bits,
                                    "strict": strict, "relaxed": recount(scores, truth, control, RELAXED_GATE),
                                    "score_max": float(np.max(scores)),
                                    "score_margin": float(abs(scores[0, 0]-scores[0, 1]))})
            if archive is not None:
                require(set(archive.files) == used_keys, f"unaccounted score archive keys: {relative(path)}")
        finally:
            if archive is not None:
                archive.close()
        report_provenance.append({"report": relative(path), "sha256": sha256(path),
                                  "schema": schema, "configuration": config,
                                  "capture_attempts": len(doc["captures"]),
                                  "completed_capture_records": sum(c["status"] == "completed" for c in doc["captures"]),
                                  "failed_capture_records": sum(c["status"] != "completed" for c in doc["captures"]),
                                  "scores_sha256": doc["scores_sha256"],
                                  "scores_archive_verified": scores_path.is_file(),
                                  "score_source": ("no scored captures; failure-only report"
                                                   if all(c["status"] != "completed" for c in doc["captures"])
                                                   else "verified NPZ" if scores_path.is_file()
                                                   else "inline report scores; archive unavailable"),
                                  "wall_seconds_joint": doc.get("wall_seconds_joint"),
                                  "source_sha256": doc.get("source_sha256")})
    require(records, "no completed actual receiver records found")
    require(set(diagnostic_reports) == {x["report"] for x in exclusions}, "diagnostic exclusion did not match a receiver report")
    cohorts = {}
    for cohort in sorted({r["cohort"] for r in records}):
        rows = [r for r in records if r["cohort"] == cohort]
        inputs = [p for p in physical.values() if p["cohort"] == cohort]
        metrics = {key: interval(p["metrics"][key] for p in inputs if key in p["metrics"])
                   for key in sorted({key for p in inputs for key in p["metrics"]})}
        methods = {}
        for method in sorted({r["method"] for r in rows}):
            controls = {}
            for control in sorted({r["control"] for r in rows if r["method"] == method}):
                selected = [r for r in rows if r["method"] == method and r["control"] == control]
                controls[control] = {"independent_physical_paths": len(selected),
                                     "maximum_bit_score_range": interval(r["score_max"] for r in selected),
                                     "absolute_bit_score_difference_range": interval(r["score_margin"] for r in selected),
                                     **{gate: bounds(selected, gate, control) for gate in ("strict", "relaxed")}}
            methods[method] = controls
        campaigns_for_cohort = [(d, p) for key, (d, p) in campaigns.items() if key == cohort]
        campaign_evidence = []
        for campaign, path in campaigns_for_cohort:
            configuration = {k: v for k, v in campaign["arguments"].items() if k not in ("binary", "output")}
            campaign_evidence.append({"path": relative(path), "sha256": sha256(path),
                                      "binary_sha256": campaign["binary_sha256"], "arguments": configuration,
                                      "path_accounting": {k: campaign[k] for k in
                                          ("paths_requested", "paths_dispatched", "paths_attempted", "paths_not_started", "paths_start_unknown", "paths_succeeded", "cancelled")},
                                      "seed_derivation": campaign["seed_derivation"]})
        cohorts[cohort] = {"physical_inputs": len(inputs), "physical_metric_ranges": metrics,
                           "campaigns": campaign_evidence, "methods": methods}
    history = []
    for path, doc in documents.items():
        if path.name not in ("analysis-runs.json", "gaussian-reruns.json"):
            continue
        require(isinstance(doc, list), f"invalid invocation history {relative(path)}")
        counts = Counter(r["returncode"] for r in doc)
        errors = []
        for row in doc:
            if row["returncode"] == 0:
                continue
            log = path.parent / f"{row['case']}-{row.get('method', 'gaussian')}.log"
            matching = [line for line in log.read_text().splitlines()
                        if "Out of range float values are not JSON compliant: inf" in line] if log.exists() else []
            errors.append({"case": row["case"], "method": row.get("method", "gaussian"),
                           "returncode": row["returncode"], "log": relative(log) if log.exists() else None,
                           "serialization_failure_verified": bool(matching),
                           "notice": matching[-1] if matching else "exact failure text unavailable"})
        history.append({"path": relative(path), "sha256": sha256(path),
                        "invocations": len(doc), "returncode_counts": dict(counts),
                        "wall_seconds_sum": sum(r["seconds"] for r in doc),
                        "failed_invocations": errors,
                        "stage": "original screen" if path.name == "analysis-runs.json" else "corrected Gaussian reruns"})
    return {
        "schema": "triad-sub9-evidence-analysis-v1",
        "analyzer_sha256": sha256(Path(__file__)),
        "gates": {
            "strict": {"log_threshold": STRICT_GATE, "false_probability_per_horizon": .0001,
                       "horizon_seconds": 86400, "scheduled_attempts": 1440},
            "relaxed": {"log_threshold": RELAXED_GATE, "false_probability_per_horizon": .01,
                        "horizon_seconds": 43200, "scheduled_attempts": 720},
            "shared": {"attempt_spacing_seconds": 60, "duration_alternatives": 4,
                       "method_alternatives": 11, "bit_alternatives": 2,
                       "score_alpha_fraction": .5, "calibration_alpha_fraction": .5,
                       "formula": "log(attempts * durations * methods * bits / (alpha / 2))",
                       "relaxed_calibration": "Re-gates existing scores only; retains stricter original calibration, without refitting or recomputing a relaxed calibration bound.",
                       "validity": "Family accounting under each method's stated likelihood/noise/calibration assumptions. Not an empirical proof of operational false-admission probability or arbitrary adaptive monitoring."}},
        "confidence_conventions": {
            "confidence": .95,
            "cp": "Exact pointwise binomial Clopper-Pearson; each endpoint is separately one-sided 95%, not a joint two-sided 95% interval.",
            "pooled": "Pooled CP is conditional on iid trials. Per-bit CP allows different bit-stratum probabilities. The balanced all-success AM-GM bound does not require equal probabilities across bits.",
            "fixed_strata": "Independent physical paths, fixed source-bit strata; constant success probability within each bit stratum. Equal bit weights. Campaign source bits and distinct seed roots are checked. Pointwise, not simultaneous over methods/cohorts or optional stopping.",
            "selection": "No simultaneous coverage across reported cohorts/methods/gates; no optional-stopping qualification. Screen, pilot, holdout and paired stress configurations are never pooled.",
            "null": "Control bounds concern one completed capture. Sparse null/wrong-key captures cannot empirically establish a daily false-admission rate."},
        "integrity": {"receiver_reports": len(report_provenance), "physical_inputs_scored": len(physical),
                      "input_method_scores": len(records), "failed_capture_records": failed,
                      "failed_capture_attempts": len(failed),
                      "receiver_reports_with_failed_captures": len({f["report"] for f in failed}),
                      "failed_receiver_invocations": sum(bool(r["failed_capture_records"]) and not r["completed_capture_records"] for r in report_provenance),
                      "duplicate_input_method_scores": 0,
                      "published_count_disagreements": 0, "unverified_score_archives": sum(not r["scores_archive_verified"] for r in report_provenance),
                      "input_verification_scope": "Manifest bytes and available score archives are hash-verified; physical input-array hashes are report provenance, not an independent recomputation of omitted I/Q or block-statistic files."},
        "cohorts": cohorts, "invocation_history": history,
        "benchmarks": benchmarks, "receiver_reports": report_provenance,
        "diagnostic_exclusions": exclusions,
        "energy_scope": "Actual elapsed is transmitted source calendar duration; sampled capture duration also includes capture padding/tails. Signal-energy seconds divide manifest sampled energy by sample rate; E/N0 uses reference manifest noise variance. Evaluation-only signal truth is not used to score candidates.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--diagnostic-report", action="append", default=[], metavar="RELATIVE_PATH")
    args = parser.parse_args()
    require(args.input.is_dir(), "input must be an existing evidence directory")
    if args.output.exists():
        previous = read_json(args.output)
        require(isinstance(previous, dict) and previous.get("schema") == "triad-sub9-evidence-analysis-v1",
                "output must not overwrite another artifact")
    result = analyze(args.input.resolve(), args.diagnostic_report)
    encoded = json.dumps(result, separators=(",", ":"), sort_keys=True, allow_nan=False) + "\n"
    require(len(encoded.encode()) < 1_000_000, "analysis exceeds compact 1 MB evidence limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded)
    print(json.dumps({"output": str(args.output), "bytes": len(encoded.encode()),
                      "cohorts": len(result["cohorts"]), **result["integrity"]}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f"analysis failed: {error}", file=sys.stderr)
        sys.exit(1)

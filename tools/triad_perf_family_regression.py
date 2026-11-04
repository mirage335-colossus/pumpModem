#!/usr/bin/env python3
"""Paired regression measurements for the sampled Triad family workloads."""
from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import replace
import json
import math
from pathlib import Path
import runpy
import secrets
import statistics
import subprocess
import tempfile
import time

import numpy as np

import triad_perf_families as sample
from triad_perf_adaptive import (AdaptivePolicy, model_confirmation_policy, qualify_log_residuals,
                                 qualification_policy, regression_profile, summarize)
from triad_perf_conditional import (build_conditional_contract, check_conditional_sentinel,
                                   conditional_comparison, conditional_gate_readiness,
                                   conditional_model_log_bounds, conditional_training_confidence)


SOURCES = ("reference", "candidate")
STAGES = ("pipeline", "receiver", "generation")
SCHEMA = "triad-performance-sampled-family-pair-v1"


def canonical_library_call(call):
    """Compare the complete loaded-library multiset, independent of inspection order.

    This only changes a derived validation copy. Actual arithmetic, library
    paths/versions, thread counts, duplicates and every other regime field stay
    strict; raw receiver observations are retained unchanged.
    """
    value = deepcopy(call)
    threads = value["receiver"]["regime"].get("threads", {})
    if "library_pools" in threads:
        pools = threads["library_pools"]
        if not isinstance(pools, list) or any(not isinstance(pool, dict) for pool in pools):
            raise ValueError("thread library inspection must be a list of metadata mappings")
        threads["library_pools"] = sorted(pools, key=sample.digest)
    return value


def numerical_agreement(pair, precision):
    first, second = (pair[name]["receiver_runs"] for name in SOURCES)
    if len(first) != len(second):
        raise ValueError("receiver measurement count differs across sources")
    maximum = 0.
    tol = 1e-5 if precision == "fp32" else 1e-9
    for a, b in zip(first, second, strict=True):
        a, b = canonical_library_call(a), canonical_library_call(b)
        ar, br = a["receiver"], b["receiver"]
        if (ar["work"]["count"] != br["work"]["count"] or
            ar["work"]["target_count"] != br["work"]["target_count"] or
            ar["work"]["unit"] != br["work"]["unit"] or
            a["complete_receiver"] != b["complete_receiver"]):
            raise ValueError("paired sampled receiver work differs across sources")
        if a["complete_receiver"] and ar["work"]["count"] != ar["work"]["target_count"]:
            raise ValueError("paired full sampled receiver did not complete declared target work")
        if sample.digest(ar["regime"]) != sample.digest(br["regime"]):
            raise ValueError("paired sampled receiver work regimes differ across sources")
        ar, br = ar["result"], br["result"]
        x, y = (np.asarray(v["scores"], dtype=np.float64) for v in (ar, br))
        if x.shape != y.shape or not x.size or not np.isfinite(x).all() or not np.isfinite(y).all():
            raise ValueError("paired sampled receiver scores are invalid")
        maximum = max(maximum, float(np.max(np.abs(x-y))))
        if not np.allclose(x, y, rtol=tol, atol=tol):
            raise ValueError("paired sampled receiver scores disagree")
        for field in ("winner", "admissions"):
            if ar.get(field) != br.get(field):
                raise ValueError(f"paired sampled receiver {field} disagree")
        if ar.get("decisions") or br.get("decisions"):
            def decisions(r):
                return {name: (d["bit"], d["accepted"]) for name, d in r.get("decisions", {}).items()}
            if decisions(ar) != decisions(br):
                raise ValueError("paired sampled receiver decisions disagree")
    return {"maximum_absolute_score_difference": maximum, "relative_and_absolute_tolerance": tol}


def _regimes(pair):
    return {name: {(sample.digest(canonical_library_call(row)["receiver"]["regime"]), row["receiver"]["work"]["unit"],
                    row["receiver"]["work"]["target_count"]) for row in pair[name]["receiver_runs"]}
            for name in SOURCES}


def _forecast(training, counts):
    answer = {}
    for name in SOURCES:
        rows = [r[name] for r in training]
        target = rows[0]["receiver_runs"][0]["receiver"]["work"]["target_count"]
        answer[name] = sample.affine_prediction(rows, counts, target)
    return answer


def _residuals(rows, forecasts):
    signed = {stage: {metric: [] for metric in (*SOURCES, "paired")} for stage in STAGES}
    for pair in rows:
        hold = {name: sample.validation_row(pair[name], forecasts[name]) for name in SOURCES}
        for stage in STAGES:
            errors = signed[stage]
            for name in SOURCES:
                errors[name].append(hold[name][f"{stage}_log_error"])
            errors["paired"].append(errors["candidate"][-1]-errors["reference"][-1])
    return signed


def _confirmation(residuals):
    confidence = model_confirmation_policy().confidence
    return {stage: {metric: qualify_log_residuals(values, maximum_relative_error=.05, confidence=confidence)
                    for metric, values in metrics.items()} for stage, metrics in residuals.items()}


def _status(checks):
    states = {v["status"] for metrics in checks.values() for v in metrics.values()}
    return "validated" if states == {"validated"} else "invalid" if "invalid" in states else "inconclusive"


def load_anchor(path, identity):
    """Replay a historical source-matched proof before allowing conditional reuse."""
    try:
        data = json.loads(Path(path).read_text())
        proof = dict(data)
        proof_sha = proof.pop("record_sha256")
        if proof_sha != sample.digest(proof) or proof["schema"] != SCHEMA or proof["status"] != "validated":
            return None
        if sample.digest(proof["identity"]) != sample.digest(identity):
            return None
        frozen = dict(proof["frozen"])
        commitment = frozen.pop("commitment_sha256")
        if commitment != sample.digest(frozen):
            return None
        if frozen["confirmation_policy"] != model_confirmation_policy().to_dict():
            return None
        if frozen["identity_sha256"] != sample.digest(identity):
            return None
        recomputed = _forecast(proof["training"], proof["case"]["training"])
        if recomputed != frozen["forecast"]:
            return None
        holds = proof["full_pairs"]
        ids = [p["pair_id"] for p in holds]
        training_ids = [p["pair_id"] for p in proof["training"]]
        seeds = [r["reference"]["generation"]["seed"] for r in (*holds, *proof["training"])]
        if (frozen["training_pair_ids"] != training_ids or len(holds) not in model_confirmation_policy().looks or
            len(set([*ids, *training_ids])) != len(ids)+len(training_ids) or len(set(seeds)) != len(seeds)):
            return None
        for pair in (*holds, *proof["training"]):
            numerical_agreement(pair, identity["precision"])
        residuals = _residuals(holds, recomputed)
        if _status(_confirmation(residuals)) != "validated":
            return None
        return {"historical_pair_ids": ids, "residuals": residuals, "proof_sha256": proof_sha}
    except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError):
        return None


def _pipeline(row):
    return row["complete_pipeline_wall_seconds"]


def _stage_training(forecast, training, stage):
    if stage == "pipeline":
        return {name: forecast[name]["outer_pipeline_predictions_seconds"] for name in SOURCES}
    if stage == "generation":
        return {name: [row[name]["generation"]["wall_seconds"] for row in training] for name in SOURCES}
    return {name: [pred-row[name]["generation"]["wall_seconds"] for pred, row in
                   zip(forecast[name]["outer_pipeline_predictions_seconds"], training, strict=True)] for name in SOURCES}


def _actual_stage(pair, stage):
    if stage == "pipeline":
        return {name: _pipeline(pair[name]) for name in SOURCES}
    if stage == "generation":
        return {name: pair[name]["generation"]["wall_seconds"] for name in SOURCES}
    return {name: pair[name]["receiver_runs"][0]["request_wall_seconds"] for name in SOURCES}


def _candidate_work(report):
    if report["selected_measurement"] == "direct":
        return sample.complete_work_signature(report["training"][0]["candidate"])
    forecast = report["frozen"]["forecast"]["candidate"]
    return {name: forecast[name] for name in ("unit", "target_count", "regime_sha256")}


def _measurement_dependencies_match(identity):
    driver = Path(__file__).resolve()
    return all(sample.file_sha(driver.with_name(name)) == hash_
               for name, hash_ in identity["measurement_dependencies"].items())


def measure_case(workers, binaries, case, profile, output, *, device, precision, native_threads,
                 deadline, policy, mode="auto", anchor=None, maximum_holds=9, sustained_seconds=0.,
                 seed_offset=None):
    """Run paired actual-work measurements, once per preregistered look."""
    if mode == "qualify":
        policy = qualification_policy(policy)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    setup = {name: sample.case_worker_setup(workers[name], case, device) for name in SOURCES}
    driver = Path(__file__).resolve()
    identity = {"case": case, "fixture_profile": profile, "native_threads": native_threads,
                "precision": precision, "device": device,
                "adapters": {name: setup[name]["source_sha256"] for name in SOURCES},
                "environment": {name: setup[name]["environment"] for name in SOURCES},
                "binaries": {name: sample.file_sha(binaries[name]) for name in SOURCES},
                "measurement_dependencies": {name: sample.file_sha(driver.with_name(name))
                    for name in (*sample.MEASUREMENT_DEPENDENCIES, driver.name, "triad_perf_conditional.py")}}
    if mode not in ("auto", "reuse", "qualify", "direct"):
        raise ValueError("unknown paired measurement mode")
    if seed_offset is None:
        seed_offset = secrets.randbits(40)
    counts = case["training"]
    historical = load_anchor(anchor, identity) if anchor and mode in ("auto", "reuse") else None
    report = {"schema": SCHEMA, "id": case["id"], "adapter": "sampled_family",
              "case": case, "identity": identity, "setup": setup, "seed_offset": seed_offset,
              "worker_startup_seconds": {name: workers[name].external_startup_seconds for name in SOURCES},
              "training": [], "full_pairs": [], "timing_looks": [], "confirmation": [],
              "mode_requested": mode, "historical_anchor_sha256": historical["proof_sha256"] if historical else None,
              "timing_scope": "fresh full native export and complete receiver request in a persistent worker; Python worker startup and import setup reported separately",
              "status": "running"}
    base = case["seed"] + seed_offset

    with tempfile.TemporaryDirectory(prefix="captures-", dir=output) as scratch:
        scratch = Path(scratch)
        def pair(kind, index, seed, limits):
            order = SOURCES if index % 2 == 0 else SOURCES[::-1]
            measurements = {}
            for offset, name in enumerate(order):
                ordered = limits[::-1] if (index+offset) % 2 else limits
                job = sample.measure_job(workers[name], binaries[name], profile, case, seed, native_threads,
                                         scratch/f"{kind}-{index}-{name}", device, precision, deadline,
                                         limits=ordered)
                if ordered != limits:
                    job["receiver_runs"].reverse()
                measurements[name] = job
            return {**measurements, "pair_id": sample.digest({"seed": seed, "case": case["id"]}),
                    "source_order": list(order), "numeric_agreement": numerical_agreement(measurements, precision)}

        missing_history = mode == "reuse" and historical is None and policy.stopping_rule == "decision"
        direct = counts is None or mode == "direct" or missing_history
        if missing_history:
            report["direct_fallback_reason"] = "no applicable historical predictor; measure full workload directly"
        warmup = pair("warmup", 0, base, (None,) if direct else tuple(counts))
        report["discarded_warmup"] = warmup
        if not direct:
            abbreviation = sum(warmup[n]["training_pipeline_wall_seconds"] for n in SOURCES)
            try:
                pilot = _forecast([warmup], counts)
            except ValueError as error:
                direct = True
                report["pilot_direct_choice"] = {"abbreviated_pair_seconds": abbreviation,
                                                  "direct_reason": str(error)}
            else:
                full = sum(pilot[n]["complete_pipeline_seconds_point"] for n in SOURCES)
                # Decide before the statistical samples. Include both abbreviated
                # receiver calls and the one full fresh native export they share.
                report["pilot_direct_choice"] = {"abbreviated_pair_seconds": abbreviation,
                                                  "predicted_full_pair_seconds": full,
                                                  "threshold_fraction": .8, "minimum_full_pair_seconds": 2.}
                direct = abbreviation >= .8*full or full < 2.
        report["selected_measurement"] = "direct" if direct else "modeled"

        # An unqualified predictor cannot resolve a routine decision. Keep
        # fresh model training on its legacy qualification schedule.
        timing_policy = policy if direct or historical else qualification_policy(policy)
        decision = timing_policy.stopping_rule == "decision"
        report["regression_policy"] = {"pass_alpha": timing_policy.pass_error_budget,
                                      "failure_alpha": timing_policy.failure_error_budget,
                                      "slowdown_threshold": timing_policy.slowdown_threshold,
                                      "model_guard_alpha": timing_policy.alpha}
        values = {name: [] for name in SOURCES}
        regimes = _regimes(warmup)

        def conditional_plans(forecast, timing):
            confidence = conditional_training_confidence(timing_policy, timing)
            return {stage: build_conditional_contract(
                historical["residuals"][stage], _stage_training(forecast, report["training"], stage),
                applicability_key=sample.digest(identity),
                historical_anchor_ids=historical["historical_pair_ids"],
                training_pair_ids=[p["pair_id"] for p in report["training"]],
                timing_confidence=confidence)
                for stage in STAGES}

        plans = None
        for i in range(policy.looks[-1]):
            row = pair("training", i, base+16*(i+1), (None,) if direct else tuple(counts))
            report["training"].append(row)
            if _regimes(row) != regimes:
                report["status"] = "work_regime_changed"
                report["prediction_valid"] = False
                report["comparison"] = {"gate": "inconclusive"}
                sample.write_json(output/"result.json", report)
                return report
            if direct:
                for name in SOURCES:
                    values[name].append(_pipeline(row[name]))
            else:
                fitted = _forecast([row], counts)
                for name in SOURCES:
                    values[name].append(fitted[name]["complete_pipeline_seconds_point"])
            if i+1 in policy.looks:
                timing = summarize(values["reference"], values["candidate"], timing_policy,
                                   model_validation="direct_full" if direct else "unvalidated")
                if historical and decision and not direct:
                    plans = conditional_plans(_forecast(report["training"], counts), timing)
                    ratio = timing["paired_candidate_over_reference"]["timing_only_one_sided_bounds"]
                    readiness = {}
                    for stage, plan in plans.items():
                        stage_ratio = plan["training_forecasts"]["paired"]["timing_mean_confidence_interval_seconds"]
                        readiness[stage] = conditional_gate_readiness(plan, ratio if stage == "pipeline" else stage_ratio,
                            slowdown_threshold=policy.slowdown_threshold)
                    timing["conditional_gate_readiness"] = readiness
                    if (all(row["sentinel_feasible"] for row in readiness.values()) and
                            readiness["pipeline"]["provisional_gate"] in ("pass", "performance_regression")):
                        timing["stop_reason"] = "provisional_decision_ready_for_full_endpoint"
                report["timing_looks"].append(timing)
                if timing["stop_reason"] != "continue":
                    break

        report["adaptive"] = timing
        report["status"] = "direct_full" if direct else "unqualified"
        report["prediction_valid"] = direct
        report["comparison"] = {"gate": timing["performance_gate"] if direct else "inconclusive"}
        if direct and decision:
            report["comparison"].update(
                timing_only_one_sided_bounds=timing["paired_candidate_over_reference"]["timing_only_one_sided_bounds"],
                pass_one_sided_confidence_across_looks=1-policy.pass_error_budget,
                failure_one_sided_confidence_across_looks=1-policy.failure_error_budget,
                confidence_applies_to="paired geometric mean complete-pipeline runtime ratio")
        if not direct:
            forecast = _forecast(report["training"], counts)
            fixed = {"identity_sha256": sample.digest(identity), "forecast": forecast,
                     "confirmation_policy": model_confirmation_policy().to_dict(),
                     "training_pair_ids": [p["pair_id"] for p in report["training"]]}
            if historical:
                if plans is None:
                    plans = conditional_plans(forecast, timing)
                fixed["conditional_plans"] = plans
            report["frozen"] = {**fixed, "commitment_sha256": sample.digest(fixed)}
            sample.write_json(output/"forecast.json", report["frozen"])
            point = sum(forecast[n]["complete_pipeline_seconds_point"] for n in SOURCES)
            if historical and decision and timing["stop_reason"] != "provisional_decision_ready_for_full_endpoint":
                report["confirmation_stop_reason"] = "frozen_decision_unresolved_or_endpoint_infeasible"
            elif historical or mode != "reuse":
                for i in range(1 if historical else maximum_holds):
                    if deadline-time.monotonic() < 1.2*point:
                        report["confirmation_stop_reason"] = "deadline"
                        break
                    try:
                        full = pair("full", i, base+100000+16*i, (None,))
                    except (ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                        report["confirmation_stop_reason"] = "full_measurement_failed"
                        report["confirmation_error"] = f"{type(error).__name__}: {error}"
                        break
                    report["full_pairs"].append(full)
                    try:
                        residuals = _residuals(report["full_pairs"], forecast)
                    except ValueError as error:
                        report["confirmation_stop_reason"] = "work_regime_changed"
                        report["confirmation_error"] = str(error)
                        break
                    if historical:
                        checks = {stage: check_conditional_sentinel(
                            plans[stage], _actual_stage(full, stage),
                            applicability_key=fixed["identity_sha256"], sentinel_id=full["pair_id"])
                            for stage in STAGES}
                        report["conditional_stage_checks"] = checks
                        # A gross contradiction in a component must remain
                        # visible to the existing calibration guard as well.
                        bad = next((c for c in checks.values() if c["status"] ==
                                    "unvalidated_request_focused_qualification"), checks["pipeline"])
                        report["anchors"] = {"conditional_check": bad}
                        bounds = conditional_model_log_bounds(plans["pipeline"], checks["pipeline"])
                        supported = all(c["status"] == "historically_supported_conditional" for c in checks.values())
                        if supported:
                            ratio = timing["paired_candidate_over_reference"]["timing_mean_confidence_interval_seconds"]
                            report["comparison"] = conditional_comparison(ratio, bounds,
                                                    slowdown_threshold=policy.slowdown_threshold)
                            if decision:
                                report["comparison"].update(timing_only_one_sided_bounds=ratio,
                                    timing_only_pass_confidence_across_looks=1-policy.pass_error_budget,
                                    timing_only_failure_confidence_across_looks=1-policy.failure_error_budget,
                                    confidence_includes_unverified_model_error=False)
                            report["status"] = "historically_supported_conditional"
                            report["prediction_valid"] = True
                    elif i+1 in model_confirmation_policy().looks:
                        check = _confirmation(residuals)
                        report["confirmation"].append(check)
                        outcome = _status(check)
                        if outcome == "validated":
                            r = residuals["pipeline"]
                            final_timing = summarize(values["reference"], values["candidate"], timing_policy,
                                model_validation="validated", reference_model_log_errors=r["reference"],
                                candidate_model_log_errors=r["candidate"], paired_model_log_errors=r["paired"])
                            report["adaptive"] = final_timing
                            report["comparison"] = {"gate": final_timing["performance_gate"]}
                            report["status"] = "validated"
                            report["prediction_valid"] = True
                            break
                        if outcome == "invalid":
                            report["confirmation_stop_reason"] = "stage_mean_mismatch"
                            break
            else:
                report["confirmation_stop_reason"] = "no_applicable_paired_anchor"

        if sustained_seconds and report["prediction_valid"]:
            name = "candidate"
            duration = (statistics.fmean(values[name]) if direct else
                        forecast[name]["complete_pipeline_arithmetic_mean_seconds"])
            jobs = max(2, math.ceil(sustained_seconds/duration))
            if 1.2*jobs*duration < sample.remaining(deadline):
                report["campaign"] = sample.sustain(
                    workers[name], binaries[name], profile, case, base+200000, native_threads,
                    scratch, device, precision, deadline, jobs, duration, output, _candidate_work(report))
            else:
                report["campaign_status"] = "insufficient_declared_budget"

    for name in SOURCES:
        workers[name].request({"action": "family-verify", "case": case})
        if sample.file_sha(binaries[name]) != identity["binaries"][name]:
            raise ValueError("native sampled exporter changed during paired timing")
    if not _measurement_dependencies_match(identity):
        raise ValueError("sampled paired measurement source changed during timing")
    report["wall_seconds"] = time.perf_counter()-start
    report["record_sha256"] = sample.digest(report)
    sample.write_json(output/"result.json", report)
    return report


def cold_single(report, factory, source, binary, output, *, threads, deadline):
    """Check a committed startup-plus-job estimate with a fresh worker."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    if not report["prediction_valid"]:
        raise ValueError("a cold observation needs a supported warm forecast or direct timing")
    identity, case = report["identity"], report["case"]
    expected_binary = identity["binaries"]["candidate"]
    if sample.file_sha(binary) != expected_binary or not _measurement_dependencies_match(identity):
        raise ValueError("cold observation binary or harness differs from the committed forecast")
    expected_work = _candidate_work(report)
    if report["selected_measurement"] == "direct":
        warm = statistics.geometric_mean(_pipeline(r["candidate"]) for r in report["training"])
    else:
        warm = report["frozen"]["forecast"]["candidate"]["complete_pipeline_seconds_point"]
    startup = report["worker_startup_seconds"]["candidate"]
    setup = report["setup"]["candidate"]["request_wall_seconds"]
    plan = {"scope": "fresh worker launch, imports, native sampled export and complete result IPC",
            "python_process_and_handshake_seconds": startup, "receiver_module_setup_seconds": setup,
            "warm_complete_pipeline_seconds": warm, "predicted_cold_seconds": startup+setup+warm,
            "forecast_status": "unvalidated_diagnostic",
            "prediction_excludes": "first-use kernel compilation, graph capture/JIT and any other work avoided by a warmed invocation",
            "expected_complete_work": expected_work,
            "parent_report_sha256": report["record_sha256"],
            "seed": case["seed"]+report["seed_offset"]+300000}
    sample.write_json(output/"cold-forecast.json", plan)
    worker = None
    with tempfile.TemporaryDirectory(prefix="captures-", dir=output) as directory:
        started = time.perf_counter()
        try:
            worker = factory(source, threads=threads, deadline=deadline,
                             log_directory=output, name="cold-candidate", adapter=sample.WORKER)
            setup = sample.case_worker_setup(worker, case, identity["device"])
            if setup["source_sha256"] != identity["adapters"]["candidate"]:
                raise ValueError("fresh worker source differs from the committed forecast")
            if sample.digest(setup["environment"]) != sample.digest(identity["environment"]["candidate"]):
                raise ValueError("fresh worker environment differs from the committed forecast")
            through_setup = time.perf_counter()-started
            row = sample.measure_job(worker, binary, identity["fixture_profile"], case, plan["seed"],
                identity["native_threads"], Path(directory), identity["device"], identity["precision"], deadline)
            # Both serial intervals stop before fixture hashing, which is
            # qualification overhead. The warm estimate ends at the same IPC.
            actual = through_setup + row["complete_pipeline_wall_seconds"]
            result = {**plan, "actual_cold_seconds": actual,
                      "relative_error": actual/plan["predicted_cold_seconds"]-1.,
                      "actual_launch_and_setup_seconds": through_setup,
                      "actual_receiver_setup": setup, "job": row,
                      "single_observation_only": True,
                      "cold_prediction_qualified": False}
            verification = {
                "native_binary_matches": sample.file_sha(binary) == expected_binary,
                "measurement_dependencies_match": _measurement_dependencies_match(identity),
                "full_work_matches": sample.complete_work_signature(row) == expected_work,
            }
            try:
                checked = worker.request({"action": "family-verify", "case": case, "verify_environment": True})
            except (RuntimeError, TimeoutError) as error:
                verification["worker_verify_error"] = str(error)
                verification["stage_source_matches"] = verification["environment_matches"] = False
            else:
                verification["stage_source_matches"] = checked["source_sha256"] == identity["adapters"]["candidate"]
                verification["environment_matches"] = checked["environment_matches_setup"]
            result["endpoint_verification"] = verification
            result["endpoint_verified"] = all(v is True for v in verification.values())
        finally:
            if worker is not None:
                worker.close()
    sample.write_json(output/"result.json", result)
    return result


def cli(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, default=sample.ROOT)
    parser.add_argument("--candidate-root", type=Path, default=sample.ROOT)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--anchor-directory", type=Path)
    parser.add_argument("--case", action="append", required=True, dest="cases")
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float, default=600.)
    parser.add_argument("--looks", default="5,9,17,33")
    parser.add_argument("--stopping-rule", choices=("decision", "precision"),
                        help="routine defaults to decision; qualification defaults to precision")
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal",
                        help="routine decision criteria; precision stopping uses historical criteria")
    parser.add_argument("--maximum-holds", type=int, choices=(3,5,9,17), default=9)
    parser.add_argument("--mode", choices=("auto", "reuse", "qualify", "direct"), default="auto")
    parser.add_argument("--sustained-seconds", type=float, default=0.)
    parser.add_argument("--cold-single", action="store_true",
                        help="observe one fresh-worker endpoint for the first supported selected case")
    args = parser.parse_args(argv)
    config = sample.manifest()
    cases = {case["id"]: case for case in config["cases"]}
    if set(args.cases)-cases.keys() or len(set(args.cases)) != len(args.cases):
        parser.error("unknown or repeated case")
    if not 1 <= args.threads <= 256 or not 1 <= args.native_threads <= 256:
        parser.error("invalid thread count")
    if not 0 < args.budget_seconds <= 28800 or not 0 <= args.sustained_seconds <= 3600:
        parser.error("invalid suite budget or sustained duration")
    stopping_rule = args.stopping_rule or ("precision" if args.mode == "qualify" else "decision")
    policy = AdaptivePolicy(looks=tuple(map(int, args.looks.split(","))), family_count=len(cases),
                            stopping_rule=stopping_rule,
                            **(regression_profile(args.regression_profile) if stopping_rule == "decision" else {}))
    common = runpy.run_path(str(sample.WORKER.with_name("triad-performance.py")))
    factory = common["ReceiverWorker"]
    start = time.perf_counter()
    deadline = time.monotonic()+args.budget_seconds
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    binaries = {"reference": args.reference_binary.resolve(strict=True), "candidate": args.binary.resolve(strict=True)}
    roots = {"reference": args.reference_root.resolve(strict=True), "candidate": args.candidate_root.resolve(strict=True)}
    workers, statuses = {}, {}
    cold_pending = args.cold_single
    try:
        for name in SOURCES:
            workers[name] = factory(roots[name], threads=args.threads, deadline=deadline,
                                    log_directory=root, name=name, adapter=sample.WORKER)
        for name in args.cases:
            case = cases[name]
            if args.device not in case["devices"] or (args.device == "cuda" and
                not all(w.identity["cuda_available"] for w in workers.values())):
                statuses[name] = {"status": "unsupported_backend"}
                continue
            path = args.anchor_directory / f"{name}.sampled-anchor.json" if args.anchor_directory else None
            try:
                row = measure_case(workers, binaries, case, config["fixtures"][case["fixture"]], root/name,
                    device=args.device, precision=args.precision, native_threads=args.native_threads,
                    deadline=deadline, policy=policy, mode=args.mode, anchor=path,
                    maximum_holds=args.maximum_holds, sustained_seconds=args.sustained_seconds)
                if path and row["status"] == "validated" and not path.exists():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    sample.write_json(path, row)
                statuses[name] = {"status": row["status"], "gate": row["comparison"]["gate"],
                                  "timing_precision_reached": row.get("adaptive", {}).get("timing_precision_reached", False)}
                if cold_pending and row["prediction_valid"]:
                    cold_pending = False
                    cold = cold_single(row, factory, roots["candidate"], binaries["candidate"], root/(name+"-cold"),
                                       threads=args.threads, deadline=deadline)
                    statuses[name]["cold_relative_error"] = cold["relative_error"]
                    statuses[name]["cold_prediction_qualified"] = cold["cold_prediction_qualified"]
                    statuses[name]["cold_endpoint_verified"] = cold["endpoint_verified"]
            except (ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                statuses[name] = {"status": "error", "detail": f"{type(error).__name__}: {error}"}
    finally:
        for worker in workers.values():
            worker.close()
        sample.write_json(root/"summary.json", {"schema": SCHEMA, "case_status": statuses,
                          "wall_seconds": time.perf_counter()-start, "complete_scope_qualification": False,
                          "cold_latency_qualification": "unqualified_diagnostic" if args.cold_single else "not_requested_unqualified"})
    return 0 if all(row.get("gate") == "pass" for row in statuses.values()) else 2


if __name__ == "__main__":
    raise SystemExit(cli())

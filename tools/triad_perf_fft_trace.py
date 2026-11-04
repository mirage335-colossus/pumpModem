#!/usr/bin/env python3
"""Traceable adaptive timing of the complete, resident FFT acquisition bank.

Only the CFO count is abbreviated. Full sampled arrays, timing lattices,
template preparation and final score construction remain in every call. A
source/host-bound full-bank anchor and one current full endpoint are required
before a conditional routine model can support a regression decision.
"""
from __future__ import annotations

from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import statistics
import time

from triad_perf_adaptive import (MODEL_CONFIRMATION_CONFIDENCE, MODEL_CONFIRMATION_LOOKS,
                                 model_confirmation_policy, qualification_policy, qualify_log_residuals, summarize)
from triad_perf_conditional import (build_conditional_contract, check_conditional_sentinel,
                                    conditional_comparison, conditional_gate_readiness,
                                    conditional_model_log_bounds, conditional_training_confidence)


SCHEMA = "triad-performance-fft-trace-v3"
MEASUREMENT_CONTRACT = "full-geometry-resident-fft-affine-cfo-v3"
# Reviewed CPU002 protocol: the same complete preparation, wrapped scoring,
# geometry and numerical checks with finite-look full-bank mean qualification.
# These hashes identify the original record; they are never rewritten when a
# reporting-only harness change reuses its evidence.
LEGACY_V3_MEASUREMENT_CONTRACTS = frozenset({(
    "aa55de6a50b07b6b6dad2113317feec68ea2a49c1da74cf364fc20f92345feb1",
    "3ad99d54b955e735f0db832bd6ac3afe91b016d993de80909827efd491174106",
)})
MODEL_ERROR_LIMIT = .05
QUALIFICATION_COUNTS = MODEL_CONFIRMATION_LOOKS
MINIMUM_BLOCK_SECONDS = .10
MAXIMUM_BLOCK_INVOCATIONS = 16
CHEAP_FULL_SECONDS = 1.
GEOMETRY = ("sample_count", "sample_rate", "nfft", "lags", "lag_first_sample",
            "lag_last_sample", "lag_step_samples", "group_tile", "precision", "device")


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def _save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".writing")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def _remaining(deadline):
    if deadline is not None and time.monotonic() >= deadline:
        raise TimeoutError("FFT performance measurement deadline exhausted")


def _positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value <= 0:
        raise ValueError(name + " must be finite positive seconds")
    return float(value)


def measurement_plan(target, minimum_repeats=5):
    """Freeze the bank sizes using work counts, never observed timing values."""
    if isinstance(target, bool) or not isinstance(target, int) or target < 1:
        raise ValueError("target CFO count must be a positive integer")
    large = max(2, math.ceil(target / 8))
    hold = max(large + 1, math.ceil(target / 4))
    routine_work = minimum_repeats * (1 + large) + 3 * hold
    direct_work = minimum_repeats * target
    direct = target <= 3 or hold >= target or routine_work >= .8 * direct_work
    return {"direct": direct, "training_counts": [] if direct else [1, large],
            "intermediate_count": None if direct else hold, "target_count": target,
            "maximum_extrapolation": 8., "minimum_repeats": minimum_repeats,
            "routine_cfo_work": direct_work if direct else routine_work,
            "direct_cfo_work": direct_work,
            "minimum_qualification_full_holdouts": 0 if direct else 3,
            "qualification_full_holdout_looks": [] if direct else list(QUALIFICATION_COUNTS),
            "qualification_overhead_amortized_separately": True}


def _fit(prepare, counts, timings, target):
    """Two-point, nonnegative fixed-plus-CFO fit with explicit boundary fits."""
    if len(counts) != 2 or counts[0] >= counts[1] or target < counts[1]:
        raise ValueError("invalid FFT training geometry")
    prep = _positive(prepare, "preparation")
    y = [_positive(t, "score") for t in timings]
    slope = (y[1] - y[0]) / (counts[1] - counts[0])
    fixed = y[0] - slope * counts[0]
    options = [(sum(y) / 2., 0.),
               (0., sum(c*t for c, t in zip(counts, y)) / sum(c*c for c in counts))]
    if fixed >= 0 and slope >= 0:
        options.append((fixed, slope))
    fixed, slope = min(options, key=lambda p: sum((p[0] + p[1]*c-t)**2 for c, t in zip(counts, y)))
    full = prep + fixed + slope * target
    return {"preparation_seconds": prep, "fixed_score_seconds": fixed,
            "per_cfo_seconds": slope, "predicted_full_seconds": full}


def _mean_model(models, target):
    """Freeze the same geometric-mean estimand used by timing inference."""
    pooled = {k: statistics.fmean(m[k] for m in models) for k in models[0]}
    point = math.exp(statistics.fmean(math.log(_prediction(m, target)) for m in models))
    scale = point / _prediction(pooled, target)
    return {**{k: pooled[k] * scale for k in ("preparation_seconds", "fixed_score_seconds", "per_cfo_seconds")},
            "predicted_full_seconds": point}


def _prediction(model, count):
    return model["preparation_seconds"] + model["fixed_score_seconds"] + model["per_cfo_seconds"] * count


def _qualification(rows):
    errors = {n: [row["log_errors"][n] for row in rows] for n in ("reference", "candidate")}
    errors["paired"] = [row["paired_log_error"] for row in rows]
    intervals = {n: qualify_log_residuals(values, maximum_relative_error=MODEL_ERROR_LIMIT,
                                        confidence=MODEL_CONFIRMATION_CONFIDENCE)
                 for n, values in errors.items()}
    statuses = {value["status"] for value in intervals.values()}
    status = "invalid" if "invalid" in statuses else "validated" if statuses == {"validated"} else "inconclusive"
    return {"status": status, "log_errors": errors, "intervals": intervals,
            "look_count": len(rows), "confirmation_policy": model_confirmation_policy().to_dict(),
            "scope": "finite predeclared confirmation looks; both source means and paired ratio must establish equivalence; no simultaneous interval claim across metrics or cases"}


def _confirmation_plan(models, count, available_seconds):
    """Freeze a feasible maximum without using any holdout observations."""
    logs = {n: [math.log(_prediction(m, count)) for m in models[n]] for n in models}
    spread = max(statistics.stdev(logs["reference"]), statistics.stdev(logs["candidate"]),
                 statistics.stdev(c-r for r, c in zip(logs["reference"], logs["candidate"], strict=True)))
    pair_seconds = sum(math.exp(statistics.fmean(values)) for values in logs.values())
    feasible = [n for n in QUALIFICATION_COUNTS
                if available_seconds is None or 1.25*n*pair_seconds < available_seconds]
    return {"maximum_repeats_per_source": max(feasible, default=0),
            "allowed_looks": feasible, "confirmation_policy": model_confirmation_policy().to_dict(),
            "estimated_seconds_per_pair": pair_seconds, "training_log_sd": spread,
            "scope": "training-only budget cap; fixed forecast and alpha divisor across all four scheduled looks, even when the cap permits fewer"}


def _validation_row(frozen, observed, *, count, repeat, purpose):
    """Rebuild every derived holdout field from its recorded original times."""
    if set(observed) != {"reference", "candidate"}:
        raise ValueError("holdout must record both sources")
    for name, value in observed.items():
        if set(value) != {"preparation_seconds", "score_including_finish_seconds", "full_seconds"}:
            raise ValueError("unexpected holdout timing fields")
        prep = _positive(value["preparation_seconds"], name + " preparation")
        score = _positive(value["score_including_finish_seconds"], name + " score")
        if _positive(value["full_seconds"], name + " full") != prep + score:
            raise ValueError("holdout total does not equal recorded stages")
    predicted = {n: _prediction(frozen["validation_models"][n], count) for n in observed}
    for name, value in predicted.items():
        _positive(value, name + " prediction")
    errors = {n: math.log(observed[n]["full_seconds"] / predicted[n]) for n in observed}
    relative = {n: abs(observed[n]["full_seconds"] - predicted[n])/predicted[n] for n in observed}
    paired_error = errors["candidate"] - errors["reference"]
    return {"purpose": purpose, "repeat": repeat, "count": count,
            "frozen_prediction_sha256": _digest(frozen), "predicted_seconds": predicted,
            "observed": observed, "log_errors": errors, "relative_errors": relative,
            "paired_log_error": paired_error, "paired_relative_error": abs(math.expm1(paired_error)),
            "single_run_within_bound": max(*relative.values(), abs(math.expm1(paired_error))) <= MODEL_ERROR_LIMIT,
            "error_limit": MODEL_ERROR_LIMIT}


def _adaptive_protocol_hash():
    return hashlib.sha256(Path(__file__).with_name("triad_perf_adaptive.py").read_bytes()).hexdigest()


def _identity(case, workers, precision, device, geometry):
    identities = {}
    for name, worker in workers.items():
        value = getattr(worker, "identity", {})
        identities[name] = {k: value.get(k) for k in ("source_hashes", "python", "numpy", "torch", "threads", "tf32_allowed")}
    return {"schema": SCHEMA, "protocol_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "adaptive_protocol_sha256": _adaptive_protocol_hash(),
            "measurement_contract": MEASUREMENT_CONTRACT,
            "case": case["id"], "input_sha256": case.get("fixture_hashes", {}),
            "frequency_offsets_hz": case["frequency_offsets_hz"], "geometry": geometry,
            "worker_implementations": identities, "precision": precision, "device": device,
            "performance_environment": case.get("performance_environment"),
            "model_error_limit": MODEL_ERROR_LIMIT}


def anchor_valid(value, identity):
    """Validate raw full-run evidence, not just its stored status or checksum."""
    if not isinstance(value, dict) or not isinstance(identity, dict):
        return False
    try:
        if (value.get("identity_sha256") != _digest(identity) or value.get("identity") != identity or
                value.get("schema") != SCHEMA or value.get("status") != "validated"):
            return False
        if value.get("payload_sha256") != _digest({k: v for k, v in value.items() if k != "payload_sha256"}):
            return False
        frozen, held = value["frozen_prediction"], value["full_validation"]
        target = len(identity["frequency_offsets_hz"])
        maximum = frozen["maximum_validation_repeats"]
        if (frozen["schema"] != SCHEMA or frozen["identity_sha256"] != _digest(identity) or
                frozen["target_count"] != target or frozen["validation_count"] != target or
                frozen["model_error_limit"] != MODEL_ERROR_LIMIT or
                frozen["confirmation_policy"] != model_confirmation_policy().to_dict() or
                frozen["prediction_frozen_before_validation"] is not True or
                isinstance(maximum, bool) or maximum not in QUALIFICATION_COUNTS or
                len(held) not in QUALIFICATION_COUNTS or len(held) > maximum):
            return False
        if (isinstance(frozen["training_outer_count"], bool) or
                not isinstance(frozen["training_outer_count"], int) or frozen["training_outer_count"] < 3):
            return False
        expected_looks = [n for n in QUALIFICATION_COUNTS if n <= maximum]
        if (frozen["confirmation_plan"]["maximum_repeats_per_source"] != maximum or
                frozen["confirmation_plan"]["allowed_looks"] != expected_looks or
                frozen["confirmation_plan"]["confirmation_policy"] != model_confirmation_policy().to_dict()):
            return False
        if frozen["models"] != frozen["validation_models"] or set(frozen["models"]) != {"reference", "candidate"}:
            return False
        for model in frozen["models"].values():
            if set(model) != {"preparation_seconds", "fixed_score_seconds", "per_cfo_seconds", "predicted_full_seconds"}:
                return False
            if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in model.values()):
                return False
            if not math.isclose(_prediction(model, target), _positive(model["predicted_full_seconds"], "full prediction"), rel_tol=1e-12):
                return False
        for index, row in enumerate(held):
            expected = _validation_row(frozen, row["observed"], count=target, repeat=index, purpose="independent_full")
            if row != expected:
                return False
        expected_qualification_looks = [_qualification(held[:n]) for n in QUALIFICATION_COUNTS if n <= len(held)]
        if (value["qualification_looks"] != expected_qualification_looks or
                any(q["status"] != "inconclusive" for q in expected_qualification_looks[:-1]) or
                value["qualification"] != expected_qualification_looks[-1] or
                value["qualification"]["status"] != "validated"):
            return False
        return True
    except (KeyError, TypeError, ValueError, ArithmeticError):
        return False


def anchor_proof_valid(value):
    """Check the old record under its own original identity and full raw data."""
    return isinstance(value, dict) and anchor_valid(value, value.get("identity"))


def _measurement_regime(identity):
    """Remove only explicitly reviewed measurement-harness identity fields."""
    if not isinstance(identity, dict) or identity.get("schema") != SCHEMA:
        return None, None
    declared = identity.get("measurement_contract")
    pair = (identity.get("protocol_sha256"), identity.get("adaptive_protocol_sha256"))
    if declared == MEASUREMENT_CONTRACT:
        mapping = "declared reviewed v3 measurement contract"
    elif declared is None and pair in LEGACY_V3_MEASUREMENT_CONTRACTS:
        mapping = "reviewed CPU002 v3 legacy measurement contract"
    else:
        return None, None
    omitted = {"protocol_sha256", "adaptive_protocol_sha256", "measurement_contract"}
    return {k: v for k, v in identity.items() if k not in omitted}, mapping


def anchor_applicability(value, current_identity):
    """Keep historical proof validity separate from current-workload applicability."""
    proof = anchor_proof_valid(value)
    old_identity = value.get("identity", {}) if isinstance(value, dict) else {}
    historical, mapping = _measurement_regime(old_identity)
    current, current_mapping = _measurement_regime(current_identity)
    compatible = (proof and historical is not None and current is not None and historical == current
                  and bool(current.get("performance_environment")))
    differences = (sorted(k for k in set(historical) | set(current) if historical.get(k) != current.get(k))
                   if historical is not None and current is not None else ["unreviewed_measurement_contract"])
    return {"historical_proof_valid": proof, "compatible": compatible,
            "measurement_contract": MEASUREMENT_CONTRACT, "historical_contract_mapping": mapping,
            "current_contract_mapping": current_mapping, "different_timed_regime_fields": differences,
            "applicability_key": _digest(current) if current is not None else None,
            "original_identity_sha256": value.get("identity_sha256") if isinstance(value, dict) else None,
            "original_protocol_sha256": old_identity.get("protocol_sha256"),
            "original_adaptive_protocol_sha256": old_identity.get("adaptive_protocol_sha256"),
            "current_protocol_sha256": current_identity.get("protocol_sha256") if isinstance(current_identity, dict) else None,
            "current_adaptive_protocol_sha256": current_identity.get("adaptive_protocol_sha256") if isinstance(current_identity, dict) else None,
            "scope": "only reviewed adapter/adaptive harness byte hashes may differ; timed worker sources, fixture, bank, geometry, precision, software and environment remain exact; historical evidence is not rewritten"}


def _find_anchor(directory, identity):
    if directory is None or not identity.get("performance_environment"):
        return None, "no supplied stable hardware/runtime identity or anchor directory"
    root = Path(directory)
    # Data, not filenames, authorize reuse. Malformed/incomplete records never
    # become an applicable anchor merely because their names match a case.
    for path in sorted(root.glob("*.anchor.json"), reverse=True):
        try:
            value = json.loads(path.read_text())
            applicability = anchor_applicability(value, identity)
            if not applicability["compatible"]:
                continue
            return {"path": str(path), "record": value, "applicability": applicability}, None
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return None, "no exact implementation, geometry, fixture and environment match"


def measure_fft(case, workers, precision, device, *, policy, deadline, api,
                anchor_directory=None, validation_mode="qualify"):
    """Adaptively time, freeze, then independently validate one full-bank model.

    ``auto`` reuses a compatible proven anchor or qualifies the complete bank.
    ``reuse`` requires historical proof and one independent full endpoint pair. A training
    pilot selects inner batching only; all inner calls form one outer sample.
    Holdout schedule, budget cap and predictions freeze before any holdout execution. Model
    qualification tests mean log bias, separately from individual-run variation.
    """
    if validation_mode not in ("auto", "reuse", "qualify"):
        raise ValueError("validation_mode must be auto, reuse or qualify")
    if validation_mode == "qualify" and policy.stopping_rule == "decision":
        policy = qualification_policy(policy)
    if set(workers) != {"reference", "candidate"}:
        raise ValueError("exactly reference and candidate receiver workers are required")
    target = len(case["frequency_offsets_hz"])
    plan = measurement_plan(target, policy.looks[0])
    direct = plan["direct"]
    raw = {n: {"preparation": [], "scores": [], "models": [], "direct": [], "blocks": []} for n in workers}
    checks, looks, validations, qualification_looks = [], [], [], []
    expected_geometry = None
    frozen, anchor, qualification = None, None, None
    historical_anchor, conditional_plan, conditional_check, conditional_bounds = None, None, None, None
    run_id = str(time.time_ns())
    anchor_reason, anchor_path, freeze_path = None, None, None
    model_state = "direct_full" if direct else "unvalidated"
    error_rows = {n: [] for n in workers}
    paired_errors = []
    full_score_calls = {n: 0 for n in workers}
    started = time.perf_counter()

    def prepare(name):
        nonlocal expected_geometry
        _remaining(deadline)
        result = workers[name].request(api.prepare_request(case, precision, device))
        _positive(result.get("wall_seconds"), "preparation")
        geometry = {k: result["geometry"][k] for k in GEOMETRY}
        if expected_geometry is None:
            expected_geometry = geometry
        if geometry != expected_geometry:
            raise api.SuiteError(case["id"] + ": FFT geometry changed across sources or measurements")
        return result

    def score(name, count):
        _remaining(deadline)
        result = workers[name].request(api.score_request(case, count))
        _positive(result.get("wall_seconds_including_finish"), "wrapped score")
        if result.get("measured_group_count") != count * case["configuration"]["lag_group_count"]:
            raise api.SuiteError(case["id"] + ": measured CFO/lag count differs from declared work")
        if count == target:
            full_score_calls[name] += 1
        return result

    def check_outputs(outputs, count, purpose, repeat):
        ref, cand = outputs["reference"], outputs["candidate"]
        agreement = api.numeric_agreement(ref, cand)
        if not agreement["eligible"] or ref.get("geometry") != cand.get("geometry"):
            raise api.SuiteError(case["id"] + ": measured-bank score, rank, gate or geometry mismatch")
        checks.append({"purpose": purpose, "repeat": repeat, "cfo_count": count, "agreement": agreement})

    def measure_pair(counts, purpose, index):
        result = {}
        for name in (("reference", "candidate") if index % 2 == 0 else ("candidate", "reference")):
            prepared = prepare(name)
            scores = {count: score(name, count) for count in counts}
            result[name] = {"prepare": prepared, "scores": scores}
        for count in counts:
            check_outputs({n: result[n]["scores"][count] for n in workers}, count, purpose, index)
        return result

    counts = [target] if direct else plan["training_counts"]
    pilot_raw = measure_pair(counts, "discarded_batch_pilot", 0)
    pilot_seconds = {n: pilot_raw[n]["prepare"]["wall_seconds"] + sum(
        s["wall_seconds_including_finish"] for s in pilot_raw[n]["scores"].values()) for n in workers}
    cost_guard = {"applied": False, "original_count_plan": dict(plan)}
    batch_seconds = dict(pilot_seconds)
    if not direct:
        pilot_models = {n: _fit(pilot_raw[n]["prepare"]["wall_seconds"], counts,
                               [pilot_raw[n]["scores"][c]["wall_seconds_including_finish"] for c in counts], target)
                        for n in workers}
        estimated_full = {n: pilot_models[n]["predicted_full_seconds"] for n in workers}
        ratios = {n: pilot_seconds[n]/estimated_full[n] for n in workers}
        cheap_full = max(estimated_full.values()) < CHEAP_FULL_SECONDS
        switch = cheap_full or max(ratios.values()) >= .8
        cost_guard = {"applied": True, "original_count_plan": dict(plan),
                      "abbreviated_cycle_seconds": dict(pilot_seconds), "estimated_full_seconds": estimated_full,
                      "abbreviated_over_estimated_full": ratios, "switch_to_direct": switch,
                      "direct_threshold": .8, "cheap_full_seconds_threshold": CHEAP_FULL_SECONDS,
                      "cheap_full_direct": cheap_full, "inference_eligible": False,
                      "scope": "prospective pilot cost heuristic including actual complete preparation; never retuned from holdouts"}
        if switch:
            direct = True
            plan = {**plan, "direct": True, "selection_reason": ("pilot_full_under_one_second" if cheap_full
                    else "pilot_preparation_and_score_cost_guard")}
            counts = [target]
            batch_seconds = estimated_full
    model_state = "direct_full" if direct else "unvalidated"
    effective_policy = replace(policy, model_allowance_log=0. if direct else max(
        policy.model_allowance_log, -math.log1p(-MODEL_ERROR_LIMIT)))
    requested = max(1, math.ceil(MINIMUM_BLOCK_SECONDS/min(batch_seconds.values())))
    work_cap = MAXIMUM_BLOCK_INVOCATIONS if direct else max(1, math.floor(.8*target/sum(counts)))
    block_invocations = min(requested, work_cap, MAXIMUM_BLOCK_INVOCATIONS)
    pilot = {"inference_eligible": False, "seconds_per_source": pilot_seconds,
             "minimum_training_block_seconds_target": MINIMUM_BLOCK_SECONDS,
             "requested_block_invocations": requested, "work_cap_invocations": work_cap,
             "selected_block_invocations": block_invocations,
             "raw": {n: {"preparation_seconds": pilot_raw[n]["prepare"]["wall_seconds"],
                          "score_seconds": {str(c): s["wall_seconds_including_finish"] for c, s in pilot_raw[n]["scores"].items()}}
                     for n in workers}}
    identity = _identity(case, workers, precision, device, expected_geometry)
    if not direct and validation_mode in ("reuse", "auto"):
        anchor, anchor_reason = _find_anchor(anchor_directory, identity)
        historical_anchor = anchor
        if anchor is None and validation_mode == "auto" and effective_policy.stopping_rule == "decision":
            effective_policy = qualification_policy(effective_policy)
    decision_sampling = effective_policy.stopping_rule == "decision"
    readiness = None
    if historical_anchor:
        historical_record = historical_anchor["record"]
        historical_key = historical_anchor["applicability"]["applicability_key"]

        def build_plan(values, summary):
            return build_conditional_contract(historical_record["qualification"]["log_errors"], values,
                applicability_key=historical_key,
                historical_anchor_ids=[historical_record["payload_sha256"]+":full:"+str(i)
                                       for i in range(len(historical_record["full_validation"]))],
                training_pair_ids=[run_id+":training:"+str(i) for i in range(len(values["reference"]))],
                timing_confidence=conditional_training_confidence(effective_policy, summary))

    validation_count = None if direct or (validation_mode == "reuse" and anchor is None) else target
    confirmation_plan = None
    sampling_started = time.monotonic()
    sampling_stop = None

    for look in policy.looks:
        for repeat in range(len(raw["reference"]["preparation"]), look):
            pairs = [measure_pair(counts, "timing", repeat*block_invocations+j) for j in range(block_invocations)]
            for name in workers:
                invocations = [pair[name] for pair in pairs]
                preparation = statistics.fmean(v["prepare"]["wall_seconds"] for v in invocations)
                times = [statistics.fmean(v["scores"][c]["wall_seconds_including_finish"] for v in invocations)
                         for c in counts]
                raw[name]["preparation"].append({"seconds": preparation,
                                                "geometry": invocations[0]["prepare"]["geometry"],
                                                "stages": invocations[0]["prepare"].get("stages", {})})
                for c, seconds in zip(counts, times):
                    first = invocations[0]["scores"][c]
                    raw[name]["scores"].append({"repeat": repeat, "count": c, "seconds": seconds,
                                                "receiver_stages": first.get("timing", {}),
                                                "arithmetic": first.get("arithmetic", {}),
                                                "resources": first.get("resources", {})})
                raw[name]["blocks"].append({"outer_repeat": repeat, "invocation_count": len(invocations),
                                            "preparation_seconds": [v["prepare"]["wall_seconds"] for v in invocations],
                                            "score_seconds_by_count": {str(c): [v["scores"][c]["wall_seconds_including_finish"] for v in invocations]
                                                                       for c in counts}})
                if direct:
                    raw[name]["direct"].append(preparation + times[0])
                else:
                    raw[name]["models"].append(_fit(preparation, counts, times, target))
        values = {n: raw[n]["direct"] if direct else [m["predicted_full_seconds"] for m in raw[n]["models"]] for n in workers}
        summary = summarize(values["reference"], values["candidate"], effective_policy,
                            model_validation="direct_full" if direct else "unvalidated")
        looks.append(summary)
        sampling_stop = summary["stop_reason"]
        if validation_count is not None:
            if historical_anchor:
                pair_seconds = sum(math.exp(statistics.fmean(math.log(x) for x in values[n])) for n in workers)
                feasible = deadline is None or time.monotonic() + 1.25*pair_seconds < deadline
                confirmation_plan = {"maximum_repeats_per_source": 1 if feasible else 0,
                                     "allowed_looks": [1] if feasible else [],
                                     "estimated_seconds_per_pair": pair_seconds,
                                     "scope": "one precommitted full endpoint sentinel; historical support only, no fresh mean-equivalence inference"}
            else:
                confirmation_plan = _confirmation_plan({n: raw[n]["models"] for n in workers}, validation_count,
                    None if deadline is None else deadline-time.monotonic())
        if historical_anchor and decision_sampling:
            conditional_plan = build_plan(values, summary)
            readiness = conditional_gate_readiness(conditional_plan,
                summary["paired_candidate_over_reference"]["timing_mean_confidence_interval_seconds"],
                slowdown_threshold=policy.slowdown_threshold)
            summary["conditional_readiness"] = readiness
            if readiness["sentinel_feasible"] and readiness["provisional_gate"] in ("pass", "performance_regression"):
                sampling_stop = "conditional_decision_ready_for_endpoint"
                break
            if readiness["provisional_gate"] == "unvalidated":
                sampling_stop = "historical_contract_unavailable"
                break
        if sampling_stop != "continue":
            break
        if not direct and validation_count is None:
            sampling_stop = "historical_anchor_unavailable"
            break
        if (not direct and not decision_sampling and summary["paired_candidate_over_reference"]["model_adjusted_gate_interval"][0]
                > policy.slowdown_threshold):
            sampling_stop = "freeze_for_potential_regression_validation"
            break
        # Reserve only the first declared confirmation look while deciding
        # whether more training fits. Later validation looks have their own
        # frozen maximum, and stop as soon as the mean model is resolved.
        reserve = ((1 if historical_anchor else min(QUALIFICATION_COUNTS))*confirmation_plan["estimated_seconds_per_pair"]
                   if confirmation_plan is not None else 0.)
        next_cost = ((summary["next_look"]-look)*(time.monotonic()-sampling_started)/look + reserve)
        if deadline is not None and time.monotonic()+1.25*next_cost >= deadline:
            sampling_stop = "budget_limit"
            break

    if not direct:
        repeats = 0 if confirmation_plan is None else confirmation_plan["maximum_repeats_per_source"]
        frozen = {"schema": SCHEMA, "identity_sha256": _digest(identity),
                  "training_outer_count": len(values["reference"]), "target_count": target,
                  "models": {n: _mean_model(raw[n]["models"], target) for n in workers},
                  "validation_models": {n: _mean_model(raw[n]["models"], validation_count) for n in workers} if validation_count else None,
                  "validation_count": validation_count, "maximum_validation_repeats": repeats,
                  "confirmation_policy": model_confirmation_policy().to_dict(),
                  "confirmation_plan": confirmation_plan, "model_error_limit": MODEL_ERROR_LIMIT,
                  "prediction_frozen_before_validation": True}
        if historical_anchor:
            if conditional_plan is None:
                conditional_plan = build_plan(values, summary)
            frozen.update(routine_kind="historical_full_endpoint_sentinel", conditional_contract=conditional_plan,
                          sentinel_id=run_id+":full-endpoint", current_run_id=run_id)
            if readiness is not None:
                frozen["conditional_readiness"] = readiness
        if anchor_directory is not None:
            token = run_id + "-" + _digest(identity)[:16]
            freeze_path = Path(anchor_directory) / (token + ".prediction.json")
            _save(freeze_path, frozen)
        if historical_anchor:
            anchor_path = historical_anchor["path"]
            if conditional_plan["status"] != "conditional_contract_ready":
                model_state = "unvalidated"
                conditional_check = {"status": model_state, "conditional_performance_pass_eligible": False,
                                     "reason": conditional_plan["status"]}
            elif readiness is not None and readiness["timing_blockers"]:
                model_state = "inconclusive"
                conditional_check = {"status": model_state, "conditional_performance_pass_eligible": False,
                                     "reason": "training uncertainty precludes a passing full endpoint",
                                     "timing_blockers": readiness["timing_blockers"]}
            elif readiness is not None and readiness["provisional_gate"] == "inconclusive":
                model_state = "inconclusive"
                conditional_check = {"status": model_state, "conditional_performance_pass_eligible": False,
                                     "reason": "decision_inconclusive_at_frozen_look"}
            elif repeats:
                pair = measure_pair([target], "historical_endpoint_sentinel", 0)
                observed = {n: {"preparation_seconds": pair[n]["prepare"]["wall_seconds"],
                                "score_including_finish_seconds": pair[n]["scores"][target]["wall_seconds_including_finish"],
                                "full_seconds": pair[n]["prepare"]["wall_seconds"] + pair[n]["scores"][target]["wall_seconds_including_finish"]}
                            for n in workers}
                validations.append(_validation_row(frozen, observed, count=target, repeat=0,
                                                    purpose="historical_endpoint_sentinel"))
                conditional_check = check_conditional_sentinel(conditional_plan,
                    {n: observed[n]["full_seconds"] for n in workers},
                    applicability_key=historical_key,
                    sentinel_id=frozen["sentinel_id"])
                conditional_bounds = conditional_model_log_bounds(conditional_plan, conditional_check)
                model_state = conditional_check["status"]
            else:
                model_state = "unvalidated"
                conditional_check = {"status": "unvalidated", "conditional_performance_pass_eligible": False,
                                     "reason": "full endpoint budget unavailable"}
            # A single sentinel is deliberately not fed to fresh mean-equivalence
            # inference. Its evidence may support only the separate conditional gate.
            summary = summarize(values["reference"], values["candidate"], effective_policy,
                                model_validation="unvalidated")
        else:
            for r in range(repeats):
                pair = measure_pair([validation_count], "independent_full", r)
                observed = {n: {"preparation_seconds": pair[n]["prepare"]["wall_seconds"],
                                "score_including_finish_seconds": pair[n]["scores"][validation_count]["wall_seconds_including_finish"],
                                "full_seconds": pair[n]["prepare"]["wall_seconds"] + pair[n]["scores"][validation_count]["wall_seconds_including_finish"]}
                            for n in workers}
                validations.append(_validation_row(frozen, observed, count=validation_count, repeat=r,
                                                    purpose="independent_full"))
                if len(validations) in QUALIFICATION_COUNTS:
                    qualification = _qualification(validations)
                    qualification_looks.append(qualification)
                    if qualification["status"] in ("validated", "invalid"):
                        break
                    next_count = next((n for n in QUALIFICATION_COUNTS if len(validations) < n <= repeats), None)
                    if next_count is not None and deadline is not None:
                        projected = 1.25*(next_count-len(validations))*confirmation_plan["estimated_seconds_per_pair"]
                        if time.monotonic()+projected >= deadline:
                            break
            if validations:
                model_state = qualification["status"] if qualification["status"] != "inconclusive" else "unvalidated"
            error_rows = {n: [row["log_errors"][n] for row in validations] for n in workers}
            paired_errors = [row["paired_log_error"] for row in validations]
            if validation_count == target and validations:
                record = {"schema": SCHEMA, "status": model_state, "identity": identity,
                          "identity_sha256": _digest(identity), "frozen_prediction": frozen,
                          "full_validation": validations, "qualification": qualification,
                          "qualification_looks": qualification_looks,
                          "timing_boundary": "full preparation plus wrapped scoring on resident fixture",
                          "model_validation_is_conditional_mean_equivalence": True,
                          "individual_prediction_coverage_claim": False}
                record["payload_sha256"] = _digest(record)
                if freeze_path is not None:
                    anchor_path = str(freeze_path.with_name(freeze_path.name.replace(".prediction.json", ".anchor.json")))
                    _save(anchor_path, record)
                anchor = {"record": record, "path": anchor_path}
            summary = summarize(values["reference"], values["candidate"], effective_policy,
                                model_validation=model_state,
                                reference_model_log_errors=error_rows["reference"],
                                candidate_model_log_errors=error_rows["candidate"],
                                paired_model_log_errors=paired_errors)
        if summary["performance_gate"] != "performance_regression":
            summary["stop_reason"] = sampling_stop
        if readiness is not None:
            summary["conditional_readiness"] = readiness
        looks[-1] = summary

    valid = model_state in ("direct_full", "validated", "historically_supported_conditional")
    comparison = {"gate": summary["performance_gate"] if valid else "inconclusive",
                  "performance_pass_eligible": summary["gate_performance_pass_eligible"],
                  "expanded_full_prediction_ratio_interval": summary["paired_candidate_over_reference"]["model_adjusted_gate_interval"],
                  "observed_prediction_ratio": summary["paired_candidate_over_reference"]["point_seconds"],
                  "mean_reference_full_seconds": statistics.fmean(values["reference"]),
                  "mean_candidate_full_seconds": statistics.fmean(values["candidate"])}
    if historical_anchor:
        conditional_gate = conditional_comparison(
            summary["paired_candidate_over_reference"]["timing_mean_confidence_interval_seconds"],
            conditional_bounds or {"eligible": False}, slowdown_threshold=policy.slowdown_threshold)
        comparison.update(gate=conditional_gate["gate"],
                          performance_pass_eligible=conditional_gate["conditional_performance_pass_eligible"],
                          expanded_full_prediction_ratio_interval=conditional_gate["model_adjusted_ratio_interval"],
                          conditional=conditional_gate)
    comparison["absolute_mean_full_seconds_difference"] = comparison["mean_candidate_full_seconds"] - comparison["mean_reference_full_seconds"]
    warnings = []
    if not valid:
        warnings.append("full-bank mean-equivalence model invalid or unqualified; no supported performance pass")
    if not decision_sampling and not summary["timing_precision_reached"]:
        warnings.append("requested full-runtime timing precision remains unresolved")
    if comparison["gate"] == "inconclusive":
        warnings.append("full-runtime regression decision remains inconclusive")
    prediction = {}
    for name in workers:
        full_time = dict(summary[name])
        if conditional_bounds and conditional_bounds.get("eligible"):
            allowance = conditional_bounds["model_log_error_bounds"][name]
            lo, hi = full_time["timing_mean_confidence_interval_seconds"]
            full_time.update(conditional_model_mean_scenario_interval_seconds=[lo*math.exp(-allowance), hi*math.exp(allowance)],
                             conditional_model_log_error_bound=allowance,
                             conditional_model_status="historically_supported_conditional",
                             conditional_model_future_coverage_qualified=False)
        prediction[name] = {"predicted_full_seconds_by_repeat": values[name],
                            "predicted_full_time": full_time, "status": model_state}
    return {"schema": SCHEMA, "id": case["id"], "adapter": "fft",
            "scope": "full resident FFT acquisition; excludes fixture generation and interpreter startup",
            "configuration": case["configuration"], "precision": precision, "raw": raw,
            "measurement_plan": {**plan, "timing_block_invocations": block_invocations, "discarded_pilot": pilot,
                                 "pilot_cost_guard": cost_guard,
                                 "confirmation_plan": confirmation_plan}, "full_cfo_count": target,
            "full_target_measured": full_score_calls["reference"] > 0,
            "full_target_measurement_count_per_source": full_score_calls["reference"],
            "prediction": prediction,
            "prediction_valid": valid, "comparison": comparison,
            "adaptive": summary, "looks": looks, "timing_precision_reached": summary["timing_precision_reached"],
            "anchors": {"validation_mode": validation_mode, "status": model_state,
                        "identity": identity, "path": anchor_path, "lookup_reason": anchor_reason,
                        "prediction_commitment_path": str(freeze_path) if freeze_path else None,
                        "frozen_prediction": frozen, "current_validation": validations,
                        "qualification": qualification, "qualification_looks": qualification_looks,
                        "historical_record": historical_anchor["record"] if historical_anchor else None,
                        "applicability": historical_anchor["applicability"] if historical_anchor else None,
                        "conditional_check": conditional_check, "conditional_model_bounds": conditional_bounds,
                        "historical_full_validation": anchor["record"]["full_validation"] if anchor else []},
            "coverage": {"full_sample_timing_geometry": expected_geometry,
                         "measured_bank_numerical_checks": len(checks), "all_measured_counts_verified": True},
            "measured_bank_numerical_checks": checks, "warnings": warnings,
            "fixture_hashes": case.get("fixture_hashes", {}),
            "source_bound_metadata": case.get("source_bound_metadata", {}),
            "elapsed_seconds": time.perf_counter() - started,
            "reduced_bank_admissions_valid": False,
            "prediction_interval_target": "future outer timing-block mean per original call; not an individual cold invocation",
            "independence_scope": "paired outer timing blocks in persistent interpreters; inner calls are averaged, not independent samples; cold-process variance is outside this target"}

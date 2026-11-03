#!/usr/bin/env python3
"""One-shot sustained consistency checks for qualified triad benchmarks.

These checks do not create new qualification anchors or confidence intervals.
They compare an already frozen forecast with one predeclared, longer execution
and report the actually observed timing span.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import secrets
import time


SCHEMA = "triad-performance-sustained-v1"
MAX_NATIVE_TRIALS = 10_000
MINIMUM_SECONDS = 60.0
SAFETY_FACTOR = 1.5


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def _positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label} must be finite and positive")
    return float(value)


def _require_qualified(result):
    if not isinstance(result, dict) or result.get("prediction_valid") is not True:
        raise ValueError("sustained check requires a valid qualified prediction")


def _verify_native_look_series(frozen, holds, looks, terminal, case, identities, *, kind,
                               target_seconds, allow_source_only):
    """Recompute qualification from its frozen per-source holds and look ledger."""
    from triad_perf_native import digest, validate_holdout, _qualification as native_qualification
    from triad_perf_adaptive import (MODEL_CONFIRMATION_CONFIDENCE, MODEL_CONFIRMATION_LOOKS,
                                     model_confirmation_policy)

    if (not isinstance(frozen, dict)
            or frozen.get("sha256") != digest({key: value for key, value in frozen.items() if key != "sha256"})
            or frozen.get("case_sha256") != digest(case)
            or frozen.get("identities") != identities
            or frozen.get("validation_duration_seconds") != target_seconds):
        raise ValueError("native frozen prediction identity or duration linkage is invalid")
    candidate_prediction = frozen.get("validation_predictions", {}).get("candidate")
    if not isinstance(candidate_prediction, dict):
        raise ValueError("native frozen candidate validation prediction is missing")
    allowance = float(case.get("model_relative_error_limit", .05))

    if "model_confirmation_policy" not in frozen:
        raise ValueError("legacy native fixed-look reports cannot authorize a current sustained run")

    if frozen["model_confirmation_policy"] != model_confirmation_policy().to_dict():
        raise ValueError("native frozen confirmation policy identity mismatch")
    count = len(holds)//2
    if count not in MODEL_CONFIRMATION_LOOKS or len(holds) != 2*count:
        raise ValueError("native confirmation holds do not match a predeclared cumulative look")
    expected_order = [(source, index) for index in range(count)
                      for source in (("reference", "candidate") if index % 2 == 0 else ("candidate", "reference"))]
    if [(row.get("source"), row.get("index")) for row in holds] != expected_order:
        raise ValueError("native confirmation holdout order or indices differ from the frozen ledger")
    for row in holds:
        prediction = frozen.get("validation_predictions", {}).get(row.get("source"))
        if (row.get("kind") != kind or row.get("frozen_prediction_sha256") != frozen["sha256"]
                or row.get("prediction") != prediction or not isinstance(row.get("actual"), dict)
                or row["actual"].get("requested_seconds") != target_seconds):
            raise ValueError("native holdout is not linked to its frozen duration-specific prediction")
        expected_seed = (840_000 if kind == "qualify" else 900_000) + 1000*row["index"]
        if row["actual"].get("seed") != expected_seed:
            raise ValueError("native holdout seed differs from the frozen confirmation contract")
        check = validate_holdout(prediction, row["actual"], maximum_relative_error=allowance)
        if check != row.get("validation") or check.get("geometry_matched") is not True:
            raise ValueError("native holdout no longer matches its recorded validation")
    counts = [n for n in MODEL_CONFIRMATION_LOOKS if n <= count]
    if len(looks) != len(counts):
        raise ValueError("native confirmation ledger has missing or extra looks")
    recomputed = [native_qualification(holds[:2*n], allowance, confidence=MODEL_CONFIRMATION_CONFIDENCE)
                  for n in counts]
    if recomputed != looks or recomputed[-1] != terminal:
        raise ValueError("native confirmation look ledger cannot be reproduced from frozen holds")
    if any(row["status"] != "inconclusive" for row in recomputed[:-1]):
        raise ValueError("native confirmation continued after a terminal qualification look")
    candidate_valid = recomputed[-1]["intervals"]["candidate"]["status"] == "validated"
    if allow_source_only and candidate_valid:
        return recomputed[-1], True
    return recomputed[-1], recomputed[-1]["status"] == "validated"


def _require_native_candidate(result, case):
    """Verify the historical full anchor and, in routine mode, the new forecast."""
    if not isinstance(result, dict):
        raise ValueError("sustained check requires a native qualification report")
    from triad_perf_native import SCHEMA as NATIVE_SCHEMA, digest, anchor_compatible

    anchors = result.get("anchors", {})
    identities = result.get("identities")
    plans = result.get("frozen_predictions", [])
    if not isinstance(identities, dict) or not plans:
        raise ValueError("native sustained check requires a frozen prediction and source identities")
    current_frozen = plans[-1]
    mode = result.get("measurement_plan", {}).get("validation_mode")
    if mode == "routine":
        if not anchor_compatible(anchors, identities, case):
            raise ValueError("native routine sustained check lacks an exact validated historical full anchor")
        historical = anchors["frozen_prediction"]
        if (not isinstance(current_frozen, dict)
                or current_frozen.get("sha256") != digest({k: v for k, v in current_frozen.items() if k != "sha256"})
                or current_frozen.get("case_sha256") != digest(case)
                or current_frozen.get("identities") != identities):
            raise ValueError("native routine current frozen prediction identity or checksum is invalid")
        if current_frozen["sha256"] == historical.get("sha256"):
            raise ValueError("native routine result must bind its new model separately from its historical anchor")
        current = result.get("current_validation", {})
        from triad_perf_native import historical_anchor_proof, _measurement_identity, _confirmation_seed, validate_holdout
        from triad_perf_conditional import build_conditional_contract, check_conditional_sentinel

        if not historical_anchor_proof(anchors, case):
            raise ValueError("native routine historical full anchor proof is invalid")
        if current_frozen.get("historical_anchor_sha256") != anchors.get("anchor_sha256"):
            raise ValueError("native routine forecast is not linked to its exact historical anchor")
        plan = current_frozen.get("conditional_model")
        if not isinstance(plan, dict):
            raise ValueError("native routine frozen conditional model is missing")
        holds = current.get("holds")
        duration = case.get("target_seconds")
        if not isinstance(holds, list) or len(holds) != 2 or duration is None:
            raise ValueError("native routine requires exactly one full reference/candidate endpoint pair")
        if current.get("confirmation_stop_reason") != "historically_supported_conditional":
            raise ValueError("native routine endpoint did not complete its conditional check")

        # Reconstruct the frozen contract from its source anchor and current
        # timing samples. This binds every forecast, identifier and residual
        # envelope before accepting the separately observed endpoint.
        values = result.get("predicted_full_seconds", {})
        if not isinstance(values, dict) or set(values) != {"reference", "candidate"}:
            raise ValueError("native routine current training forecasts are missing")
        training_count = len(values["reference"])
        training_ids = [f"native-training:{1_000_000 + 1000*i}" for i in range(training_count)]
        anchor_ids = [f"{anchors['anchor_sha256']}:{hold['actual']['seed']}"
                      for hold in anchors["full_holdouts"] if hold["source"] == "reference"]
        applicability_key = digest({name: _measurement_identity(value) for name, value in identities.items()})
        latest_look = result.get("adaptive_looks", [])[-1]
        timing_confidence = latest_look["policy"]["interval_confidence_per_look"]
        expected_plan = build_conditional_contract(
            anchors["log_errors"], values, applicability_key=applicability_key,
            historical_anchor_ids=anchor_ids, training_pair_ids=training_ids,
            timing_confidence=timing_confidence)
        if plan != expected_plan or plan.get("status") != "conditional_contract_ready":
            raise ValueError("native routine conditional contract does not match its frozen history/training data")
        expected_sources = ["reference", "candidate"]
        if [row.get("source") for row in holds] != expected_sources:
            raise ValueError("native routine endpoint must contain one reference then candidate observation")
        actual_full_seconds = {}
        for row in holds:
            source = row["source"]
            actual = row.get("actual", {})
            prediction = current_frozen.get("predictions", {}).get(source)
            if (row.get("kind") != "routine" or row.get("index") != 0
                    or row.get("frozen_prediction_sha256") != current_frozen["sha256"]
                    or row.get("prediction") != prediction
                    or actual.get("requested_seconds") != duration
                    or actual.get("seed") != _confirmation_seed("routine", 0)):
                raise ValueError("native routine endpoint is not linked to the frozen full-duration forecast")
            validation = validate_holdout(prediction, actual,
                                          maximum_relative_error=float(case.get("model_relative_error_limit", .05)))
            if row.get("validation") != validation or validation.get("geometry_matched") is not True:
                raise ValueError("native routine endpoint no longer matches its recorded full geometry")
            actual_full_seconds[source] = actual.get("full_test_wall_seconds")
        expected_check = check_conditional_sentinel(
            plan, actual_full_seconds, applicability_key=applicability_key,
            sentinel_id=f"native-full-sentinel:{_confirmation_seed('routine', 0)}")
        if (expected_check.get("status") != "historically_supported_conditional"
                or current.get("conditional_model") != expected_check
                or result.get("prediction", {}).get("model_validation") != expected_check["status"]
                or result.get("prediction_valid") is not True):
            raise ValueError("native routine full endpoint does not reproduce its conditional historical check")
        return "historical full anchor plus one full endpoint pair; historically supported conditional"

    if mode != "qualify" or anchors.get("case_sha256") != digest(case):
        raise ValueError("native sustained check requires an exact full qualification case")
    if (anchors.get("schema") != NATIVE_SCHEMA
            or anchors.get("identities") != identities
            or anchors.get("anchor_sha256") != digest({k: v for k, v in anchors.items() if k != "anchor_sha256"})
            or anchors.get("log_errors") != anchors.get("qualification", {}).get("log_errors")):
        raise ValueError("native full qualification anchor envelope checksum or identity is invalid")
    frozen = anchors.get("frozen_prediction")
    if frozen != current_frozen:
        raise ValueError("native full qualification anchor differs from its current frozen prediction")
    verified, candidate_valid = _verify_native_look_series(
        frozen, anchors.get("full_holdouts", []), anchors.get("confirmation_looks", []),
        anchors.get("qualification"), case, identities, kind="qualify",
        target_seconds=case.get("target_seconds"), allow_source_only=True)
    if verified != anchors.get("qualification") or not candidate_valid:
        raise ValueError("native candidate source mean-equivalence is not qualified")
    return ("joint_case_qualified" if result.get("prediction_valid") is True
            else "candidate_source_independently_qualified; paired/reference qualification inconclusive")


def _check_budget(expected_seconds, deadline):
    required = SAFETY_FACTOR * expected_seconds
    if deadline is not None:
        remaining = deadline - time.monotonic()
        if remaining < required:
            raise TimeoutError(f"sustained check needs at least {required:.3f}s budget; {max(0., remaining):.3f}s remains")
    return required


def _validated_fft_anchor(case, anchors, identity):
    """Use the receiver adapter's public validator for the full-bank proof."""
    from pathlib import Path
    from triad_perf_fft_trace import SCHEMA as FFT_SCHEMA, _digest, anchor_valid

    path = anchors.get("path")
    if path:
        record = json.loads(Path(path).read_text())
    else:
        # A newly qualified report retains the exact components of its anchor
        # even when persistence was not requested. Reassemble the public record
        # so the adapter can verify the same derived fields and checksum.
        if anchors.get("validation_mode") == "routine":
            raise ValueError("FFT routine sustain requires its persisted historical full-bank anchor")
        record = {"schema": FFT_SCHEMA, "status": anchors.get("status"), "identity": identity,
                  "identity_sha256": _digest(identity),
                  "frozen_prediction": anchors.get("frozen_prediction"),
                  "full_validation": anchors.get("current_validation", []),
                  "qualification": anchors.get("qualification"),
                  "qualification_looks": anchors.get("qualification_looks", []),
                  "timing_boundary": "full preparation plus wrapped scoring on resident fixture",
                  "model_validation_is_conditional_mean_equivalence": True,
                  "individual_prediction_coverage_claim": False}
        record["payload_sha256"] = _digest(record)
    if not anchor_valid(record, identity):
        raise ValueError("FFT full-bank anchor failed its source-bound confirmation ledger")
    return {"schema": record.get("schema"), "identity_sha256": record.get("identity_sha256"),
            "payload_sha256": record.get("payload_sha256"), "path": str(path) if path else None}


def _require_fft_conditional_routine(case, result, anchors, identity, full_count):
    """Reproduce the shared one-full-bank conditional check for routine FFT data."""
    from pathlib import Path
    from triad_perf_fft_trace import (_digest, _validation_row, anchor_applicability,
                                      anchor_proof_valid)
    from triad_perf_conditional import (build_conditional_contract,
                                        check_conditional_sentinel,
                                        conditional_model_log_bounds)

    record = anchors.get("historical_record")
    if not isinstance(record, dict) and anchors.get("path"):
        record = json.loads(Path(anchors["path"]).read_text())
    if not anchor_proof_valid(record):
        raise ValueError("FFT routine historical full-bank proof is invalid")
    applicability = anchor_applicability(record, identity)
    if not applicability.get("compatible") or applicability != anchors.get("applicability"):
        raise ValueError("FFT routine historical full-bank workload applicability is invalid")
    frozen = anchors.get("frozen_prediction")
    if (not isinstance(frozen, dict) or frozen.get("schema") != identity.get("schema")
            or frozen.get("identity_sha256") != _digest(identity)
            or frozen.get("target_count") != full_count
            or frozen.get("validation_count") != full_count
            or frozen.get("models") != frozen.get("validation_models")
            or frozen.get("routine_kind") != "historical_full_endpoint_sentinel"):
        raise ValueError("FFT routine conditional forecast does not cover the exact full bank")
    run_id = frozen.get("current_run_id")
    sentinel_id = frozen.get("sentinel_id")
    if not isinstance(run_id, str) or not run_id.strip() or sentinel_id != run_id + ":full-endpoint":
        raise ValueError("FFT routine sentinel is not linked to its frozen run identity")

    historical_ids = [record["payload_sha256"] + ":full:" + str(i)
                      for i in range(len(record["full_validation"]))]
    training_ids = [run_id + ":training:" + str(i)
                    for i in range(frozen.get("training_outer_count", 0))]
    training = {}
    for source in ("reference", "candidate"):
        rows = result.get("prediction", {}).get(source, {}).get("predicted_full_seconds_by_repeat")
        if (not isinstance(rows, list) or len(rows) != frozen.get("training_outer_count")
                or any(isinstance(value, bool) or not isinstance(value, (int, float))
                       or not math.isfinite(value) or value <= 0 for value in rows)):
            raise ValueError("FFT routine current training forecast samples are incomplete")
        training[source] = rows
    expected_plan = build_conditional_contract(
        record["qualification"]["log_errors"], training,
        applicability_key=applicability["applicability_key"],
        historical_anchor_ids=historical_ids, training_pair_ids=training_ids,
        timing_confidence=result["adaptive"]["policy"]["interval_confidence_per_look"])
    plan = frozen.get("conditional_contract")
    if plan != expected_plan or plan.get("status") != "conditional_contract_ready":
        raise ValueError("FFT routine conditional contract differs from its historical anchor/current forecasts")

    rows = anchors.get("current_validation")
    if not isinstance(rows, list) or len(rows) != 1:
        raise ValueError("FFT routine requires exactly one full-bank endpoint sentinel")
    row = rows[0]
    observed = row.get("observed")
    expected_row = _validation_row(frozen, observed, count=full_count, repeat=0,
                                   purpose="historical_endpoint_sentinel")
    if row != expected_row or row.get("frozen_prediction_sha256") != _digest(frozen):
        raise ValueError("FFT routine full endpoint row is not linked to its exact frozen forecast")
    actual = {source: observed[source]["full_seconds"] for source in ("reference", "candidate")}
    expected_check = check_conditional_sentinel(
        plan, actual, applicability_key=applicability["applicability_key"],
        sentinel_id=sentinel_id)
    expected_bounds = conditional_model_log_bounds(plan, expected_check)
    if (expected_check.get("status") != "historically_supported_conditional"
            or anchors.get("conditional_check") != expected_check
            or anchors.get("conditional_model_bounds") != expected_bounds
            or anchors.get("status") != "historically_supported_conditional"
            or result.get("prediction_valid") is not True):
        raise ValueError("FFT routine full endpoint does not reproduce its conditional historical check")
    return {"record": record, "applicability": applicability, "conditional_plan": plan,
            "sentinel_id": sentinel_id,
            "anchor_proof": {"identity_sha256": record["identity_sha256"],
                             "payload_sha256": record["payload_sha256"]}}


def native_sustained(binary, case, qualified_result, minimum_seconds=MINIMUM_SECONDS,
                     threads=1, deadline=None, freeze_callback=None):
    """Run one frozen full-duration native campaign sized from its forecast.

    The native pipeline is receiver plus generation/setup/other work. Process
    startup/output is paid once and is deliberately excluded from per-trial
    sizing. The case's target duration and all physical arguments are retained.
    """
    qualification_scope = _require_native_candidate(qualified_result, case)
    minimum_seconds = _positive(minimum_seconds, "minimum_seconds")
    if not isinstance(case, dict) or not isinstance(case.get("args"), list):
        raise ValueError("native sustained check requires a native case with args")
    frozen_rows = qualified_result.get("frozen_predictions")
    if not frozen_rows:
        raise ValueError("qualified native result has no frozen prediction")
    frozen = frozen_rows[-1]
    if not isinstance(frozen, dict) or not frozen.get("sha256"):
        raise ValueError("native frozen prediction is malformed")
    from triad_perf_native import digest, identity, run_native
    if frozen.get("case_sha256") != digest(case):
        raise ValueError("native frozen prediction case identity mismatch")
    if frozen["sha256"] != digest({key: value for key, value in frozen.items() if key != "sha256"}):
        raise ValueError("native frozen prediction checksum mismatch")
    current_identity = identity(binary, case, threads)
    qualified_identity = qualified_result.get("identities", {}).get("candidate")
    if qualified_identity != current_identity:
        raise ValueError("native qualified prediction source/environment identity mismatch")
    prediction = frozen.get("predictions", {}).get("candidate")
    if not isinstance(prediction, dict):
        raise ValueError("native frozen candidate prediction is missing")
    stages = prediction.get("stage_seconds", {})
    original_trials = case.get("batch_trials", 1)
    if isinstance(original_trials, bool) or not isinstance(original_trials, int) or original_trials < 1:
        raise ValueError("native original batch_trials must be a positive integer")
    pipeline_total = math.fsum(_positive(stages.get(name), f"native {name} prediction")
                               for name in ("receiver", "generation_setup_other"))
    per_trial_pipeline = pipeline_total / original_trials
    trials = max(1, math.ceil(minimum_seconds / per_trial_pipeline))
    if trials > MAX_NATIVE_TRIALS:
        raise ValueError(f"native sustained check requires {trials} trials, above cap {MAX_NATIVE_TRIALS}")
    # Startup/output is a single process cost. Reserve it once, while every
    # repeated trial contributes only pipeline work.
    startup = _positive(stages.get("process_startup_output"), "native startup prediction")
    expected_pipeline = per_trial_pipeline * trials
    expected_external = expected_pipeline + startup
    required_budget = _check_budget(expected_external, deadline)

    campaign_case = copy.deepcopy(case)
    campaign_case["batch_trials"] = trials
    seed = secrets.randbelow(2**31 - 1) + 1
    commitment = {"schema": SCHEMA, "kind": "native", "case_sha256": digest(case),
                 "campaign_case_sha256": digest(campaign_case), "frozen_prediction_sha256": frozen["sha256"],
                 "binary_identity": current_identity, "threads": threads,
                 "target_seconds": case.get("target_seconds"), "minimum_pipeline_seconds": minimum_seconds,
                 "original_batch_trials": original_trials, "sustained_batch_trials": trials,
                 "predicted_per_trial_pipeline_seconds": per_trial_pipeline,
                 "predicted_pipeline_seconds": expected_pipeline,
                 "predicted_process_startup_output_seconds": startup,
                 "predicted_external_seconds": expected_external,
                 "budget_required_with_safety_factor_seconds": required_budget,
                 "seed": seed, "frozen_before_run": True,
                 "source_qualification_scope": qualification_scope,
                 "interpretation": "single compositional consistency check; not a new qualification or independent confidence interval"}
    commitment["commitment_sha256"] = _digest(commitment)
    if freeze_callback:
        freeze_callback(commitment)

    observed = run_native(binary, campaign_case, case["target_seconds"], seed,
                          threads=threads, trace=False, deadline=deadline,
                          timeout=required_budget)
    actual_stages = observed["stage_seconds"]
    actual_pipeline = observed["native_pipeline_seconds"]
    receiver = actual_stages["receiver"]
    other = actual_stages["generation_setup_other"]
    external = observed["full_test_wall_seconds"]
    trial_rows = observed["native_results"]
    base, extra = divmod(trials, 4)
    quarter_sizes = [base + (1 if i < extra else 0) for i in range(4)]
    quarters, start = [], 0
    for size in quarter_sizes:
        quarters.append(trial_rows[start:start + size])
        start += size
    quarter_counts = [len(part) for part in quarters]
    quarter_means = [math.fsum(row["end_to_end_wall_seconds"] for row in part) / len(part)
                     if part else None for part in quarters]
    result = {"schema": SCHEMA, "kind": "native", "commitment": commitment,
              "observed_batch_trials": observed["batch_trials"],
              "observed_pipeline_seconds": actual_pipeline,
              "observed_receiver_seconds": receiver,
              "observed_generation_setup_other_seconds": other,
              "observed_process_startup_output_seconds": actual_stages["process_startup_output"],
              "observed_external_wall_seconds": external,
              "observed_pipeline_span_seconds": actual_pipeline,
              "minimum_seconds": minimum_seconds,
              "sustained_threshold_reached": actual_pipeline >= minimum_seconds,
              "predicted_vs_actual": {"pipeline_seconds": {"predicted": expected_pipeline, "actual": actual_pipeline},
                                      "receiver_seconds": {"predicted": stages["receiver"] / original_trials * trials,
                                                           "actual": receiver},
                                      "generation_setup_other_seconds": {
                                          "predicted": stages["generation_setup_other"] / original_trials * trials,
                                          "actual": other},
                                      "process_startup_output_seconds": {"predicted": startup,
                                                                          "actual": actual_stages["process_startup_output"]},
                                      "external_wall_seconds": {"predicted": expected_external, "actual": external}},
              "quarter_trial_counts": quarter_counts,
              "quarter_mean_pipeline_seconds_per_trial": quarter_means,
              "first_to_last_quarter_mean_ratio": (quarter_means[-1] / quarter_means[0]
                                                    if quarter_means[0] and quarter_means[-1] else None),
              "coverage": observed["coverage"], "counter_summary": observed["counter_summary"],
              "native_results": observed["native_results"],
              "qualification_claim": False,
              "interpretation": "the single observed span may be shorter than requested; no automatic rerun"}
    return result


def fft_sustained(case, worker, qualified_result, api, minimum_seconds=MINIMUM_SECONDS,
                  deadline=None, freeze_callback=None):
    """Repeat full resident FFT preparation and full-CFO scoring once planned."""
    _require_qualified(qualified_result)
    minimum_seconds = _positive(minimum_seconds, "minimum_seconds")
    if not isinstance(case, dict) or not case.get("frequency_offsets_hz"):
        raise ValueError("FFT sustained check requires its complete frequency bank")
    anchors = qualified_result.get("anchors", {})
    frozen = anchors.get("frozen_prediction")
    # `measure_fft` keeps the caller's requested auto/reuse/qualify mode in the
    # report. Historical reuse is an effective routine operation even when the
    # request was `auto` or `reuse`; use the frozen protocol marker and evidence
    # status to detect it instead of the requested mode string.
    routine = (frozen.get("routine_kind") == "historical_full_endpoint_sentinel"
               or anchors.get("status") == "historically_supported_conditional")
    if (anchors.get("status") not in (("historically_supported_conditional",) if routine else ("validated",))
            or not isinstance(frozen, dict)):
        raise ValueError("qualified FFT result has no validated frozen prediction with supported status")
    candidate_prediction = qualified_result.get("prediction", {}).get("candidate", {})
    predicted_time = candidate_prediction.get("predicted_full_time", {})
    point_seconds = _positive(predicted_time.get("point_seconds") if isinstance(predicted_time, dict)
                               else predicted_time, "FFT full-point prediction")
    repetitions = max(1, math.ceil(minimum_seconds / point_seconds))
    expected_total = point_seconds * repetitions
    required_budget = _check_budget(expected_total, deadline)
    identity = anchors.get("identity", {})
    if identity.get("case") != case.get("id"):
        raise ValueError("FFT qualified prediction case identity mismatch")
    full_count = len(case["frequency_offsets_hz"])
    if identity.get("geometry") is None or identity.get("frequency_offsets_hz") != case["frequency_offsets_hz"]:
        raise ValueError("FFT qualified prediction bank identity mismatch")
    current_fixtures = case.get("fixture_hashes", {})
    if (identity.get("input_sha256") != current_fixtures
            or qualified_result.get("fixture_hashes", {}) != current_fixtures):
        raise ValueError("FFT qualified prediction fixture identity mismatch")
    if identity.get("performance_environment") != case.get("performance_environment"):
        raise ValueError("FFT qualified prediction performance environment mismatch")
    from triad_perf_fft_trace import _digest
    if (frozen.get("identity_sha256") != _digest(identity)
            or frozen.get("target_count") != full_count
            or frozen.get("schema") != identity.get("schema")):
        raise ValueError("FFT current prediction is not linked to its full-bank source identity")
    routine_proof = (_require_fft_conditional_routine(case, qualified_result, anchors, identity, full_count)
                     if routine else None)
    anchor_proof = (routine_proof["anchor_proof"] if routine
                    else _validated_fft_anchor(case, anchors, identity))
    if routine and not math.isclose(
            point_seconds,
            frozen["conditional_contract"]["training_forecasts"]["candidate"]["point_seconds"],
            rel_tol=1e-12):
        raise ValueError("FFT routine prediction summary differs from its frozen conditional forecast")
    worker_identity = getattr(worker, "identity", {})
    expected_worker_identity = identity.get("worker_implementations", {}).get("candidate")
    if expected_worker_identity is not None and any(worker_identity.get(key) != value
                                                   for key, value in expected_worker_identity.items()):
        raise ValueError("FFT qualified prediction worker identity mismatch")
    precision = qualified_result.get("precision")
    device = identity.get("device")
    if not precision or not device:
        raise ValueError("FFT qualified prediction precision/device is missing")
    commitment = {"schema": SCHEMA, "kind": "fft", "case_id": case["id"],
                 "case_sha256": _digest({k: v for k, v in case.items() if k != "input_directory"}),
                 "fixture_hashes": current_fixtures,
                 "performance_environment": case.get("performance_environment"),
                 "source_bound_metadata": qualified_result.get("source_bound_metadata", {}),
                 "qualified_source_identity": identity,
                 "validated_full_anchor": anchor_proof,
                 "conditional_routine_evidence": ({"applicability_key": routine_proof["applicability"]["applicability_key"],
                                                      "conditional_plan_sha256": routine_proof["conditional_plan"]["plan_sha256"],
                                                      "sentinel_id": routine_proof["sentinel_id"]}
                                                     if routine else None),
                 "frozen_prediction_sha256": _digest(frozen), "full_geometry": identity["geometry"],
                 "full_cfo_count": full_count, "precision": precision, "device": device,
                 "repetitions": repetitions, "minimum_seconds": minimum_seconds,
                 "predicted_full_point_seconds": point_seconds,
                 "predicted_work_seconds": expected_total,
                 "budget_required_with_safety_factor_seconds": required_budget,
                 "timing_boundary": "resident full FFT preparation plus full-CFO scoring; excludes interpreter and fixture generation",
                 "frozen_before_run": True,
                 "interpretation": "compositional consistency check; not a new qualification or independent confidence interval"}
    commitment["commitment_sha256"] = _digest(commitment)
    if freeze_callback:
        freeze_callback(commitment)

    prep_request = api.prepare_request(case, precision, device)
    score_request = api.score_request(case, full_count)
    expected_groups = full_count * case["configuration"]["lag_group_count"]
    repetitions_observed = []
    external_started = time.perf_counter()
    for index in range(repetitions):
        if deadline is not None and time.monotonic() >= deadline:
            raise TimeoutError("FFT sustained check deadline exhausted during its frozen single campaign")
        prepared = worker.request(prep_request)
        scored = worker.request(score_request)
        prep_seconds = _positive(prepared.get("wall_seconds"), "FFT preparation timing")
        score_seconds = _positive(scored.get("wall_seconds_including_finish"), "FFT score timing")
        if scored.get("measured_group_count") != expected_groups:
            raise ValueError("FFT sustained scoring did not execute the complete declared CFO/lag bank")
        geometry = {key: prepared.get("geometry", {}).get(key) for key in identity["geometry"]}
        if geometry != identity["geometry"]:
            raise ValueError("FFT sustained preparation geometry differs from its qualified anchor")
        repetitions_observed.append({"index": index, "preparation_seconds": prep_seconds,
                                     "score_including_finish_seconds": score_seconds,
                                     "full_point_seconds": prep_seconds + score_seconds,
                                     "measured_group_count": scored["measured_group_count"],
                                     "arithmetic": scored.get("arithmetic"), "resources": scored.get("resources")})
    total = math.fsum(row["full_point_seconds"] for row in repetitions_observed)
    external_elapsed = time.perf_counter() - external_started
    return {"schema": SCHEMA, "kind": "fft", "commitment": commitment,
            "full_cfo_count": full_count, "expected_group_count_per_repetition": expected_groups,
            "repetitions": repetitions_observed,
            "observed_receiver_seconds": total,
            "observed_external_elapsed_seconds": external_elapsed,
            "minimum_seconds": minimum_seconds,
            "sustained_threshold_reached": total >= minimum_seconds,
            "source_identity": identity, "fixture_hashes": commitment["fixture_hashes"],
            "numeric_comparison": {"available": False,
                                   "reason": "the qualified FFT report retains aggregate numerical-check metadata, not reusable per-bank score vectors"},
            "qualification_claim": False,
            "interpretation": "the single observed span may be shorter than requested; no automatic rerun"}

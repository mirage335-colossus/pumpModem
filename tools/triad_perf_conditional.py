#!/usr/bin/env python3
"""Freeze a conditional historical model and check one full paired endpoint.

This protocol is an engineering applicability check. It does not qualify
future-run prediction coverage or guarantee every future residual is bounded.
"""
from __future__ import annotations

import hashlib
import json
import math
import statistics
from typing import Iterable, Mapping

from scipy.stats import t as student_t

from triad_perf_adaptive import (
    MODEL_CONFIRMATION_CONFIDENCE,
    MODEL_CONFIRMATION_LOOKS,
    model_confirmation_policy,
    qualify_log_residuals,
)


SCHEMA = "triad-performance-conditional-v2"
DEFAULT_RELATIVE_ERROR_FLOOR = 0.10
DEFAULT_MAXIMUM_MODEL_ERROR = 0.20
DEFAULT_TIMING_CONFIDENCE = 0.95
_METRICS = ("reference", "candidate", "paired")


class ConditionalModelError(ValueError):
    """Invalid frozen model inputs or tampered contract."""


def conditional_training_confidence(policy, summary: Mapping) -> float:
    """Two-sided endpoint feasibility intervals, separate from the ratio gate."""
    if policy.stopping_rule == "decision":
        return 1. - policy.alpha / (len(policy.looks) * len(_METRICS))
    return summary["policy"]["interval_confidence_per_look"]


def _hash(value: Mapping) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)
    return hashlib.sha256(payload.encode()).hexdigest()


def _numbers(values: Iterable[float], *, name: str, minimum_count: int = 1,
             positive: bool = False) -> list[float]:
    try:
        result = [float(value) for value in values]
    except (TypeError, ValueError, OverflowError) as exc:
        raise ConditionalModelError(f"{name} must contain finite numbers") from exc
    if len(result) < minimum_count or any(not math.isfinite(value) for value in result):
        raise ConditionalModelError(f"{name} must contain at least {minimum_count} finite values")
    if positive and any(value <= 0. for value in result):
        raise ConditionalModelError(f"{name} must contain only positive values")
    return result


def _ids(values: Iterable[str], *, name: str, expected_count: int) -> list[str]:
    if isinstance(values, (str, bytes)):
        raise ConditionalModelError(f"{name} must be a list of independent run identifiers")
    result = list(values)
    if (len(result) != expected_count or
            any(not isinstance(value, str) or not value.strip() for value in result) or
            len(set(result)) != len(result)):
        raise ConditionalModelError(f"{name} must contain {expected_count} unique nonempty identifiers")
    return result


def _time_summary(values: list[float], confidence: float) -> dict:
    logs = [math.log(value) for value in values]
    center = statistics.fmean(logs)
    log_se = statistics.stdev(logs) / math.sqrt(len(logs))
    critical = float(student_t.ppf((1. + confidence) / 2., len(logs) - 1))
    half = critical * log_se
    return {
        "point_seconds": math.exp(center),
        "timing_mean_confidence_interval_seconds": [
            math.exp(center - half), math.exp(center + half)],
        "timing_log_half_width": half,
        "replicate_count": len(values),
        "timing_confidence": confidence,
        "scope": "conditional Student-t confidence interval for the mean of independent training prediction runtimes",
    }


def _mean_error_log_envelope(validation: dict, relative_floor: float) -> float:
    # Use a symmetric log radius whose upper ratio endpoint is exactly the
    # stated maximum percentage. The corresponding lower endpoint is
    # conservative (e.g. [0.9091, 1.10] for a 10% limit).
    floor = math.log1p(relative_floor)
    lo, hi = validation["log_residual_interval"]
    # This allowance describes uncertainty/bias in the conditional mean only.
    # Individual residual scatter is retained in a distinct sentinel envelope.
    return max(floor, abs(lo), abs(hi))


def build_conditional_contract(
        historical_log_errors: Mapping[str, Iterable[float]],
        training_predictions: Mapping[str, Iterable[float]], *,
        applicability_key: str,
        historical_anchor_ids: Iterable[str],
        training_pair_ids: Iterable[str],
        relative_error_floor: float = DEFAULT_RELATIVE_ERROR_FLOOR,
        maximum_model_error: float = DEFAULT_MAXIMUM_MODEL_ERROR,
        confidence: float = MODEL_CONFIRMATION_CONFIDENCE,
        timing_confidence: float = DEFAULT_TIMING_CONFIDENCE) -> dict:
    """Build a pre-sentinel conditional contract from old full anchors.

    Historical errors are `log(actual complete-run seconds / frozen predicted
    complete-run seconds)`. Training values are independent predicted full
    seconds paired by run ID; they estimate current timing spread and means.
    The 10% floor and observed historical residual envelope are engineering
    assumptions, not future-coverage guarantees. The contract hash binds the
    applicability key and every frozen input.
    """
    if not isinstance(applicability_key, str) or not applicability_key.strip():
        raise ConditionalModelError("applicability_key must be a nonempty workload/environment signature")
    if not (math.isfinite(relative_error_floor) and
            0. < relative_error_floor < 1.):
        raise ConditionalModelError("relative_error_floor must lie strictly between zero and one")
    if not (math.isfinite(maximum_model_error) and
            relative_error_floor <= maximum_model_error < 1.):
        raise ConditionalModelError("maximum_model_error must cover the floor and be less than one")
    for value, name in ((confidence, "confidence"), (timing_confidence, "timing_confidence")):
        if not math.isfinite(value) or not 0. < value < 1.:
            raise ConditionalModelError(f"{name} must lie strictly between zero and one")
    if confidence != MODEL_CONFIRMATION_CONFIDENCE:
        raise ConditionalModelError("model confirmation must use the shared predeclared confidence")

    if not isinstance(historical_log_errors, Mapping) or set(historical_log_errors) != set(_METRICS):
        raise ConditionalModelError("historical_log_errors must contain reference, candidate, and paired families")
    if not isinstance(training_predictions, Mapping) or set(training_predictions) != {"reference", "candidate"}:
        raise ConditionalModelError("training_predictions must contain reference and candidate full-time forecasts")

    history_values = {name: _numbers(historical_log_errors[name], name=f"{name} historical log errors",
                                     minimum_count=3)
                      for name in _METRICS}
    counts = {len(values) for values in history_values.values()}
    if len(counts) != 1 or next(iter(counts)) not in MODEL_CONFIRMATION_LOOKS:
        raise ConditionalModelError("historical residual families must share a declared confirmation look")
    history_count = counts.pop()
    anchors = _ids(historical_anchor_ids, name="historical_anchor_ids",
                   expected_count=history_count)

    training = {name: _numbers(training_predictions[name], name=f"{name} training predictions",
                               minimum_count=3, positive=True)
                for name in ("reference", "candidate")}
    if len(training["reference"]) != len(training["candidate"]):
        raise ConditionalModelError("reference and candidate training predictions must be paired")
    training_count = len(training["reference"])
    training_ids = _ids(training_pair_ids, name="training_pair_ids",
                         expected_count=training_count)
    if set(anchors) & set(training_ids):
        raise ConditionalModelError("historical anchors and current training runs must be independent")

    history = {}
    mean_envelopes = {}
    observation_envelopes = {}
    for metric in _METRICS:
        result = qualify_log_residuals(history_values[metric],
                                       maximum_relative_error=relative_error_floor,
                                       confidence=confidence)
        history[metric] = result
        mean_envelopes[metric] = _mean_error_log_envelope(result, relative_error_floor)
        observation_envelopes[metric] = max(
            mean_envelopes[metric], max(abs(value) for value in history_values[metric]))

    forecasts = {name: _time_summary(training[name], timing_confidence)
                 for name in ("reference", "candidate")}
    pair_logs = [math.log(candidate) - math.log(reference)
                 for reference, candidate in zip(training["reference"], training["candidate"], strict=True)]
    forecasts["paired"] = _time_summary([math.exp(value) for value in pair_logs], timing_confidence)

    # A symmetric radius of log1p(limit) keeps the upper modelled ratio at or
    # below the stated 20%; using -log1p(-limit) would allow a 25% upper ratio.
    maximum_log = math.log1p(maximum_model_error)
    max_envelope = max(mean_envelopes.values())
    history_statuses = {metric: history[metric]["status"] for metric in _METRICS}
    if "invalid" in history_statuses.values():
        status = "historical_model_invalid"
    elif any(value != "validated" for value in history_statuses.values()):
        status = "historical_model_inconclusive"
    elif max_envelope > maximum_log:
        status = "historical_model_error_exceeds_maximum"
    else:
        status = "conditional_contract_ready"

    core = {
        "schema": SCHEMA,
        "status": status,
        "applicability_key": applicability_key,
        "historical_anchor_ids": anchors,
        "training_pair_ids": training_ids,
        "historical_log_errors": history_values,
        "historical_qualification": history,
        "model_log_error_envelopes": mean_envelopes,
        "mean_model_log_error_envelopes": mean_envelopes,
        "sentinel_observation_log_envelopes": observation_envelopes,
        "paired_model_log_envelope": mean_envelopes["paired"],
        "model_relative_error_floor": relative_error_floor,
        "maximum_model_error_relative": maximum_model_error,
        "maximum_model_error_log_envelope": maximum_log,
        "training_forecasts": forecasts,
        "confirmation_policy": model_confirmation_policy().to_dict(),
        "timing_and_model_uncertainty_separate": True,
        "future_coverage_qualified": False,
        "engineering_assumptions": [
            "historical forecast residuals and training timings are independent and applicable to the exact signature",
            "the maximum observed historical residual and its mean confidence interval define a conditional engineering envelope",
            "the relative-error floor is fixed before the endpoint sentinel and is not a statistical future-tail bound",
            "one sentinel checks applicability but cannot establish future residual coverage",
        ],
    }
    return {**core, "plan_sha256": _hash(core)}


def conditional_model_log_bounds(plan: Mapping, sentinel: Mapping) -> dict:
    """Return frozen per-source and paired model allowances after one sentinel.

    Current training timing uncertainty is deliberately omitted here because it
    already belongs in the caller's timing confidence interval. Do not add it a
    second time to the regression gate.
    """
    if not isinstance(plan, Mapping) or not isinstance(sentinel, Mapping):
        raise ConditionalModelError("plan and sentinel must be mapping objects")
    if plan.get("schema") != SCHEMA or sentinel.get("schema") != SCHEMA:
        return {"status": "unvalidated_request_focused_qualification",
                "eligible": False, "model_log_error_bounds": None,
                "reason": "plan or sentinel schema is unsupported; historical semantics cannot be reused"}
    if sentinel.get("status") != "historically_supported_conditional" or not sentinel.get(
            "conditional_performance_pass_eligible"):
        return {"status": sentinel.get("status", "unvalidated"),
                "eligible": False, "model_log_error_bounds": None,
                "reason": "full endpoint sentinel did not support the frozen contract"}
    if sentinel.get("plan_sha256") != plan.get("plan_sha256") or plan.get("plan_sha256") != _hash(
            {key: value for key, value in plan.items() if key != "plan_sha256"}):
        return {"status": "unvalidated_request_focused_qualification",
                "eligible": False, "model_log_error_bounds": None,
                "reason": "sentinel is not bound to the intact frozen contract"}
    return {
        "status": "historically_supported_conditional",
        "eligible": True,
        "model_log_error_bounds": dict(plan["model_log_error_envelopes"]),
        "training_timing_uncertainty_included": False,
        "future_coverage_qualified": False,
        "scope": "conditional engineering bounds from historical observed residuals and mean intervals; no future-tail guarantee",
        "plan_sha256": plan["plan_sha256"],
        "sentinel_id": sentinel["sentinel_id"],
    }


def conditional_comparison(timing_ratio_interval: Iterable[float],
                           conditional_bounds: Mapping, *,
                           slowdown_threshold: float = 1.30) -> dict:
    """Combine one paired timing interval with a verified model bound once."""
    if not math.isfinite(slowdown_threshold) or slowdown_threshold <= 1.:
        raise ConditionalModelError("slowdown_threshold must be finite and greater than one")
    try:
        low, high = (float(value) for value in timing_ratio_interval)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ConditionalModelError("timing_ratio_interval must contain two finite positive values") from exc
    if not (math.isfinite(low) and math.isfinite(high) and 0. < low <= high):
        raise ConditionalModelError("timing_ratio_interval must contain two finite positive ordered values")
    if not isinstance(conditional_bounds, Mapping) or not conditional_bounds.get("eligible"):
        return {"status": "unvalidated", "gate": "inconclusive",
                "conditional_performance_pass_eligible": False,
                "timing_ratio_interval": [low, high],
                "model_adjusted_ratio_interval": None,
                "reason": "no historically supported conditional model allowance"}
    envelopes = conditional_bounds.get("model_log_error_bounds")
    if not isinstance(envelopes, Mapping) or set(envelopes) != set(_METRICS):
        raise ConditionalModelError("conditional bounds need reference/candidate/paired envelopes")
    paired = envelopes["paired"]
    if not math.isfinite(paired) or paired < 0.:
        raise ConditionalModelError("paired model log-error bound must be finite and nonnegative")
    adjusted = [low * math.exp(-paired), high * math.exp(paired)]
    if adjusted[0] > slowdown_threshold:
        gate = "performance_regression"
    elif adjusted[1] <= slowdown_threshold:
        gate = "pass"
    else:
        gate = "inconclusive"
    return {
        "status": "conditional_gate_" + gate,
        "gate": gate,
        "conditional_performance_pass_eligible": gate == "pass",
        "timing_ratio_interval": [low, high],
        "paired_model_log_error_bound": paired,
        "model_adjusted_ratio_interval": adjusted,
        "training_timing_uncertainty_included": False,
        "future_coverage_qualified": False,
        "threshold": slowdown_threshold,
        "scope": "conditional scenario gate; paired timing interval receives the prevalidated historical envelope once",
    }


def conditional_gate_readiness(plan: Mapping, timing_ratio_interval: Iterable[float], *,
                               slowdown_threshold: float = 1.30) -> dict:
    """Decide whether to freeze the forecast before a single full endpoint.

    This look uses training and history only. It cannot admit a performance
    pass. If the timing interval is wider than the observation envelope or
    exceeds the combined mean cap, even an exactly centered endpoint cannot
    validate that metric. The independent endpoint still decides applicability.
    """
    ready = plan["status"] == "conditional_contract_ready"
    bounds = plan["model_log_error_envelopes"]
    blockers = []
    for metric in _METRICS:
        half = plan["training_forecasts"][metric]["timing_log_half_width"]
        if (half > plan["sentinel_observation_log_envelopes"][metric] or
                half + bounds[metric] > plan["maximum_model_error_log_envelope"]):
            blockers.append(metric)
    comparison = conditional_comparison(timing_ratio_interval,
        {"eligible": ready, "model_log_error_bounds": bounds}, slowdown_threshold=slowdown_threshold)
    return {"sentinel_feasible": ready and not blockers, "timing_blockers": blockers,
            "provisional_gate": comparison["gate"] if ready else "unvalidated",
            "provisional_model_adjusted_ratio_interval": comparison["model_adjusted_ratio_interval"],
            "performance_pass_eligible": False}


def check_conditional_sentinel(plan: Mapping, actual_full_seconds: Mapping[str, float], *,
                               applicability_key: str, sentinel_id: str) -> dict:
    """Check one independently paired full endpoint against a frozen envelope.

    This routine never widens/relearns the model from the sentinel. The
    training timing-mean uncertainty is included only when checking the
    sentinel. It remains separate from the historical model allowance used by
    downstream performance gates.
    """
    if not isinstance(plan, Mapping):
        raise ConditionalModelError("plan must be a conditional model object")
    if plan.get("schema") != SCHEMA:
        return {"schema": SCHEMA, "status": "unvalidated_request_focused_qualification",
                "reason": "conditional plan schema is unsupported; historical semantics cannot be reused",
                "frozen_contract_verified": False,
                "conditional_performance_pass_eligible": False}
    core = {key: value for key, value in plan.items() if key != "plan_sha256"}
    if plan.get("plan_sha256") != _hash(core):
        return {"status": "unvalidated_request_focused_qualification",
                "reason": "conditional plan hash does not match its frozen contents",
                "frozen_contract_verified": False,
                "conditional_performance_pass_eligible": False}
    if not isinstance(sentinel_id, str) or not sentinel_id.strip():
        raise ConditionalModelError("sentinel_id must be a nonempty independent run identifier")
    if applicability_key != plan.get("applicability_key"):
        return {"status": "unvalidated_request_focused_qualification",
                "reason": "sentinel workload/environment signature differs from the frozen contract",
                "frozen_contract_verified": True,
                "conditional_performance_pass_eligible": False}
    if sentinel_id in set(plan["historical_anchor_ids"]) | set(plan["training_pair_ids"]):
        return {"status": "unvalidated_request_focused_qualification",
                "reason": "sentinel identifier is not independent of historical/training runs",
                "frozen_contract_verified": True,
                "conditional_performance_pass_eligible": False}
    if plan.get("status") != "conditional_contract_ready":
        return {"status": "unvalidated_request_focused_qualification",
                "reason": "historical residuals do not qualify the conditional contract",
                "frozen_contract_verified": True,
                "conditional_performance_pass_eligible": False}
    if not isinstance(actual_full_seconds, Mapping) or set(actual_full_seconds) != {"reference", "candidate"}:
        raise ConditionalModelError("actual_full_seconds must contain one reference/candidate endpoint pair")

    actual = {}
    for name in ("reference", "candidate"):
        value = actual_full_seconds[name]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0.:
            raise ConditionalModelError(f"actual {name} full seconds must be finite and positive")
        actual[name] = float(value)
    predicted = {name: plan["training_forecasts"][name]["point_seconds"]
                 for name in ("reference", "candidate")}
    residuals = {
        "reference": math.log(actual["reference"] / predicted["reference"]),
        "candidate": math.log(actual["candidate"] / predicted["candidate"]),
        "paired": math.log(actual["candidate"] / actual["reference"])
                   - math.log(predicted["candidate"] / predicted["reference"]),
    }
    training_half_widths = {
        "reference": plan["training_forecasts"]["reference"]["timing_log_half_width"],
        "candidate": plan["training_forecasts"]["candidate"]["timing_log_half_width"],
        "paired": plan["training_forecasts"]["paired"]["timing_log_half_width"],
    }
    mean_envelopes = plan["model_log_error_envelopes"]
    observation_envelopes = plan["sentinel_observation_log_envelopes"]
    maximum_log = plan["maximum_model_error_log_envelope"]
    checks = {}
    for metric in _METRICS:
        residual = residuals[metric]
        timing_half = training_half_widths[metric]
        mean_allowance = mean_envelopes[metric]
        allowance = observation_envelopes[metric]
        interval = [residual - timing_half, residual + timing_half]
        combined_mean = mean_allowance + timing_half
        if combined_mean > maximum_log:
            outcome = "inconclusive_training_uncertainty"
        elif interval[0] >= -allowance and interval[1] <= allowance:
            outcome = "within_frozen_envelope"
        elif interval[1] < -allowance or interval[0] > allowance:
            outcome = "outside_frozen_envelope"
        else:
            outcome = "inconclusive_training_uncertainty"
        checks[metric] = {
            "log_actual_over_predicted": residual,
            "training_timing_log_half_width": timing_half,
            "sentinel_residual_timing_interval": interval,
            "pre_sentinel_historical_mean_model_log_envelope": mean_allowance,
            "pre_sentinel_historical_observation_log_envelope": allowance,
            "mean_model_plus_timing_uncertainty_for_sentinel": combined_mean,
            "mean_model_plus_timing_uncertainty_exceeds_maximum": combined_mean > maximum_log,
            "classification": outcome,
        }

    classifications = [checks[name]["classification"] for name in _METRICS]
    if "outside_frozen_envelope" in classifications:
        status = "unvalidated_request_focused_qualification"
    elif all(value == "within_frozen_envelope" for value in classifications):
        status = "historically_supported_conditional"
    else:
        status = "inconclusive"
    return {
        "schema": SCHEMA,
        "status": status,
        "conditional_performance_pass_eligible": status == "historically_supported_conditional",
        "applicability_key": applicability_key,
        "sentinel_id": sentinel_id,
        "plan_sha256": plan["plan_sha256"],
        "frozen_contract_verified": True,
        "historical_status": plan["status"],
        "checks": checks,
        "model_log_error_envelopes": mean_envelopes,
        "sentinel_observation_log_envelopes": observation_envelopes,
        "future_coverage_qualified": False,
        "sentinel_changes_model_envelope": False,
        "focused_qualification_required": status == "unvalidated_request_focused_qualification",
        "timing_and_model_uncertainty_separate": True,
        "scope": "one full paired endpoint conditioned on matching frozen history/signature; not an empirical future-coverage claim",
    }


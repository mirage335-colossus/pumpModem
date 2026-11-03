#!/usr/bin/env python3
"""Conditional full-workload prediction and paired performance gates.

Intervals here describe the stated repeated-run and linear-scaling model. They
are not distribution-free guarantees, and an unvalidated prediction is never a
performance pass.
"""
from __future__ import annotations

import math
from collections import defaultdict
from typing import Any, Iterable

import numpy as np
from scipy.stats import t as student_t


SCHEMA = "triad-performance-statistics-v1"


def _finite(value: Any, name: str, *, minimum: float | None = None,
            strict_minimum: bool = False) -> float:
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be finite numeric data")
    try:
        result = float(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ValueError(f"{name} must be finite numeric data") from exc
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    if minimum is not None and (result <= minimum if strict_minimum else result < minimum):
        op = "greater than" if strict_minimum else "at least"
        raise ValueError(f"{name} must be {op} {minimum}")
    return result


def _positive_integer(value: Any, name: str) -> int:
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or value < 1:
        raise ValueError(f"{name} must be a positive integer")
    return int(value)


def _confidence(confidence: Any) -> float:
    value = _finite(confidence, "confidence", minimum=0., strict_minimum=True)
    if value >= 1.:
        return _invalid_confidence()
    return value


def _exp_finite(value: float, name: str) -> float:
    try:
        result = math.exp(value)
    except OverflowError as exc:
        raise ValueError(f"{name} is outside the finite JSON number range") from exc
    if not math.isfinite(result):
        raise ValueError(f"{name} is outside the finite JSON number range")
    return result


def _invalid_confidence():
    raise ValueError("confidence must lie strictly between zero and one")


def _outer_t_interval(values: Iterable[float], confidence: float) -> tuple[float, float, float, float]:
    """Return mean, standard error, and a two-sided Student-t interval."""
    x = np.asarray(tuple(values), dtype=np.float64)
    if x.ndim != 1 or x.size < 3 or not np.isfinite(x).all():
        raise ValueError("at least three finite independent outer repetitions are required")
    mean = float(np.mean(x))
    if x.size == 3:
        # ddof=1 remains well-defined with the minimum allowed replication.
        pass
    se = float(np.std(x, ddof=1) / math.sqrt(x.size))
    critical = float(student_t.ppf((1. + confidence) / 2., x.size - 1))
    if not math.isfinite(critical):
        raise ValueError("could not form a finite Student-t interval")
    radius = critical * se
    return mean, se, mean - radius, mean + radius


def _measurement_rows(rows: Any, *, label: str) -> list[dict[str, Any]]:
    if not isinstance(rows, (list, tuple)) or not rows:
        raise ValueError(f"{label} must be a nonempty list")
    checked = []
    seen = set()
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"{label}[{index}] must be an object")
        repeat = row.get("repeat")
        if isinstance(repeat, (bool, np.bool_)) or not isinstance(repeat, (int, np.integer)) or repeat < 0:
            raise ValueError(f"{label}[{index}].repeat must be a nonnegative integer")
        count = _positive_integer(row.get("count"), f"{label}[{index}].count")
        seconds = _finite(row.get("seconds"), f"{label}[{index}].seconds", minimum=0., strict_minimum=True)
        key = (int(repeat), count)
        if key in seen:
            raise ValueError(f"{label} contains duplicate outer repeat/count {key}")
        seen.add(key)
        checked.append({"repeat": int(repeat), "count": count, "seconds": seconds})
    return checked


def _nonnegative_fit(counts: list[int], seconds: list[float]) -> tuple[float, float]:
    """Fit nonnegative fixed and per-CFO seconds to actual measured calls."""
    if len(set(counts)) < 2:
        raise ValueError("each outer repeat needs measurements at two distinct CFO counts")
    design = np.column_stack((np.ones(len(counts), dtype=np.float64),
                              np.asarray(counts, dtype=np.float64)))
    coefficients, _ = _nnls_two(design, np.asarray(seconds, dtype=np.float64))
    fixed, per_count = (float(coefficients[0]), float(coefficients[1]))
    if not (math.isfinite(fixed) and math.isfinite(per_count)):
        raise ValueError("full-time scaling fit was not finite")
    return fixed, per_count


def _nnls_two(design: np.ndarray, observations: np.ndarray) -> tuple[np.ndarray, float]:
    # Two parameter nonnegative least squares, implemented with a bounded
    # enumeration of the interior and the two one-dimensional boundaries.
    candidates: list[tuple[np.ndarray, float]] = []
    unconstrained, _, _, _ = np.linalg.lstsq(design, observations, rcond=None)
    if np.all(unconstrained >= 0.):
        candidates.append((unconstrained, float(np.linalg.norm(design @ unconstrained - observations))))
    for index in (0, 1):
        column = design[:, index]
        denom = float(column @ column)
        value = max(0., float(column @ observations) / denom) if denom > 0. else 0.
        coef = np.zeros(2, dtype=np.float64)
        coef[index] = value
        candidates.append((coef, float(np.linalg.norm(design @ coef - observations))))
    candidates.append((np.zeros(2, dtype=np.float64), float(np.linalg.norm(observations))))
    return min(candidates, key=lambda row: row[1])


def _full_interval(log_predictions: list[float], confidence: float,
                   allowance: float) -> dict[str, Any]:
    mean, se, low, high = _outer_t_interval(log_predictions, confidence)
    point = _exp_finite(mean, "geometric mean prediction")
    timing_lower = _exp_finite(low, "lower timing interval")
    timing_upper = _exp_finite(high, "upper timing interval")
    # Holdout residual is |prediction - actual| / actual: invert that
    # denominator to bound actual time, rather than widening by +/- allowance.
    model_lower = -math.log1p(allowance)
    model_upper = -math.log1p(-allowance)
    n = len(log_predictions)
    critical = float(student_t.ppf((1. + confidence) / 2., n - 1))
    predictive_radius = critical * se * math.sqrt(n + 1.)
    return {
        "point_seconds": point,
        "timing_only_interval_seconds": [timing_lower, timing_upper],
        "conditional_mean_confidence_interval_seconds": [
            _exp_finite(low + model_lower, "lower conditional prediction interval"),
            _exp_finite(high + model_upper, "upper conditional prediction interval"),
        ],
        "conditional_mean_confidence_interval_log_seconds": [low + model_lower, high + model_upper],
        "conditional_prediction_interval_seconds": [
            _exp_finite(mean - predictive_radius + model_lower, "lower future execution interval"),
            _exp_finite(mean + predictive_radius + model_upper, "upper future execution interval"),
        ],
        "prediction_interval_assumptions": "independent stationary normally distributed outer log runtimes; fixed model allowance is an engineering bound, not statistical coverage",
        "log_standard_error_between_outer_predictions": se,
        "model_allowance_fraction": allowance,
        "model_allowance_factors": [1. / (1. + allowance), 1. / (1. - allowance)],
    }


def predict_full(prepare_seconds, score_measurements, target_count, holdouts=(),
                 confidence=.95, model_allowance=.20, maximum_extrapolation=8.0):
    """Predict full-time from abbreviated CFO counts and explicit full-count holds.

    `prepare_seconds` has one full-geometry preparation observation per outer
    repeat. `score_measurements` supplies at least two distinct CFO counts per
    repeat. Each holdout is a full receiver score call at a count larger than
    the training maximum. Confidence intervals are conditional on independent,
    stationary outer runs and the affine fixed-plus-per-CFO cost model.
    """
    target = _positive_integer(target_count, "target_count")
    confidence = _confidence(confidence)
    allowance = _finite(model_allowance, "model_allowance", minimum=0., strict_minimum=True)
    if allowance >= 1.:
        raise ValueError("model_allowance must be less than one")
    max_scale = _finite(maximum_extrapolation, "maximum_extrapolation", minimum=1.)
    if not isinstance(prepare_seconds, (list, tuple)) or not prepare_seconds:
        raise ValueError("prepare_seconds must be a nonempty list, one value per outer repeat")
    preparation = [_finite(x, f"prepare_seconds[{i}]", minimum=0., strict_minimum=True)
                   for i, x in enumerate(prepare_seconds)]
    rows = _measurement_rows(score_measurements, label="score_measurements")
    repeats = set(range(len(preparation)))
    row_repeats = {row["repeat"] for row in rows}
    if row_repeats != repeats:
        raise ValueError("score measurements must include every preparation repeat exactly by its index")
    by_repeat: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_repeat[row["repeat"]].append(row)
    for repeat in range(len(preparation)):
        counts = [r["count"] for r in by_repeat[repeat]]
        if len(set(counts)) < 2:
            raise ValueError(f"outer repeat {repeat} needs two distinct training CFO counts")
    training_max = max(row["count"] for row in rows)
    if target < training_max:
        raise ValueError("target_count must be at least the largest training CFO count")
    extrapolation = target / training_max
    if extrapolation > max_scale:
        raise ValueError("target CFO count exceeds the declared extrapolation bound")

    holdout_rows = [] if holdouts is None or holdouts == () else _measurement_rows(holdouts, label="holdouts")
    if holdout_rows:
        if {row["repeat"] for row in holdout_rows} != repeats:
            raise ValueError("holdouts must include every outer repeat")
        for row in holdout_rows:
            if row["count"] <= training_max:
                raise ValueError("every holdout CFO count must exceed all training counts")

    fixed_by_repeat, slope_by_repeat, predictions = [], [], []
    for repeat in range(len(preparation)):
        repeat_rows = by_repeat[repeat]
        counts = [r["count"] for r in repeat_rows]
        seconds = [r["seconds"] for r in repeat_rows]
        fixed, slope = _nonnegative_fit(counts, seconds)
        full_score = fixed + slope * target
        full_seconds = preparation[repeat] + full_score
        if not math.isfinite(full_seconds) or full_seconds <= 0.:
            raise ValueError("predicted full-workload time is not finite positive")
        fixed_by_repeat.append(fixed)
        slope_by_repeat.append(slope)
        predictions.append(full_seconds)

    residuals = []
    valid_scale = True
    if holdout_rows:
        for row in holdout_rows:
            repeat = row["repeat"]
            expected = fixed_by_repeat[repeat] + slope_by_repeat[repeat] * row["count"]
            actual = row["seconds"]
            error = abs(expected - actual) / actual
            residuals.append({"repeat": repeat, "count": row["count"],
                              "predicted_score_seconds": expected,
                              "observed_score_seconds": actual,
                              "absolute_relative_error": error})
            valid_scale = valid_scale and error <= allowance

    enough = len(predictions) >= 3
    if enough:
        interval = _full_interval([math.log(x) for x in predictions], confidence, allowance)
        status = "validated_prediction" if holdout_rows and valid_scale else (
            "invalid_scaling" if holdout_rows else "unvalidated_prediction")
    else:
        interval = None
        status = "insufficient_replicates"
    # Held-out scaling failures invalidate the model even if timing replication
    # was too small to form a useful interval.
    if holdout_rows and not valid_scale:
        status = "invalid_scaling"
    return {
        "schema": SCHEMA,
        "status": status,
        "performance_pass_eligible": status == "validated_prediction",
        "target_full_cfo_count": target,
        "target_over_largest_training_count": extrapolation,
        "outer_repeat_count": len(preparation),
        "preparation_full_geometry_seconds": preparation,
        "training_score_measurements": rows,
        "fixed_score_seconds_by_repeat": fixed_by_repeat,
        "per_cfo_seconds_by_repeat": slope_by_repeat,
        "predicted_full_seconds_by_repeat": predictions,
        "arithmetic_mean_predicted_full_seconds": math.fsum(predictions) / len(predictions),
        "median_predicted_full_seconds": float(np.median(predictions)),
        "predicted_full_time": interval,
        "holdout_measurements": holdout_rows,
        "holdout_validation": {
            "validated": bool(holdout_rows) and valid_scale,
            "maximum_absolute_relative_error": max((r["absolute_relative_error"] for r in residuals), default=None),
            "allowed_relative_error": allowance,
            "residuals": residuals,
        },
        "confidence": confidence,
        "inference": {
            "method": "separate Student-t mean confidence and future-execution prediction intervals over log full seconds, widened by a heldout-checked engineering allowance",
            "conditional_assumptions": [
                "outer repeats are independent and stationary within this host, software, and power state; log times are approximately normal",
                "preparation and fixed score overhead are additive; repeated CFO score cost is linear through the target count",
                "sample count, FFT length, full lag lattice, Gram bank, group tile, rank, mask, device, and algorithm path match the target",
                "the held-out full-count score measurements are independent validation observations",
            ],
            "distribution_free_coverage_claim": False,
            "target_is_full_workload_seconds": True,
            "reduced_cfo_bank_emits_no_complete_bank_admission": True,
            "multiple_family_adjustment": "caller must Bonferroni-adjust confidence across declared case families",
            "performance_pass_requires_status_validated_prediction": True,
        },
    }


def compare_paired(reference_predictions, candidate_predictions, *,
                   paired_model_log_errors=(), confidence=.95,
                   relative_allowance=.20, slowdown_threshold=1.30):
    """Compare matched full-time predictions with a practical 30% threshold.

    `paired_model_log_errors`, when present, are the per-holdout log errors of
    candidate/reference full-time ratio predictions. They widen the interval
    by their largest absolute observed value; small holdouts never narrow the
    declared allowance.
    """
    confidence = _confidence(confidence)
    allowance = _finite(relative_allowance, "relative_allowance", minimum=0.)
    if allowance >= 1.:
        raise ValueError("relative_allowance must be less than one")
    threshold = _finite(slowdown_threshold, "slowdown_threshold", minimum=1., strict_minimum=True)
    if not isinstance(reference_predictions, (list, tuple)) or not isinstance(candidate_predictions, (list, tuple)):
        raise ValueError("paired predictions must be lists")
    if len(reference_predictions) != len(candidate_predictions):
        raise ValueError("reference and candidate predictions must have identical paired lengths")
    reference = [_finite(x, f"reference_predictions[{i}]", minimum=0., strict_minimum=True)
                 for i, x in enumerate(reference_predictions)]
    candidate = [_finite(x, f"candidate_predictions[{i}]", minimum=0., strict_minimum=True)
                 for i, x in enumerate(candidate_predictions)]
    if len(reference) < 3:
        return {
            "schema": SCHEMA,
            "status": "insufficient_replicates",
            "gate": "inconclusive",
            "performance_pass_eligible": False,
            "paired_outer_repeat_count": len(reference),
            "reason": "at least three independent paired outer repeats are required",
            "correctness_status_is_separate": True,
        }
    log_ratios = [math.log(c) - math.log(r) for r, c in zip(reference, candidate)]
    mean, se, low, high = _outer_t_interval(log_ratios, confidence)
    raw_ratio = _exp_finite(mean, "paired geometric runtime ratio")
    raw_low = _exp_finite(low, "lower paired runtime ratio")
    raw_high = _exp_finite(high, "upper paired runtime ratio")
    errors = [_finite(x, f"paired_model_log_errors[{i}]")
              for i, x in enumerate(paired_model_log_errors)]
    # Preserve at least the stated absolute prediction allowance while
    # incorporating the worst observed paired model error.
    log_allowance = max(-math.log1p(-allowance), math.log1p(allowance),
                        max((abs(e) for e in errors), default=0.))
    expanded_low = _exp_finite(low - log_allowance, "lower expanded runtime ratio")
    expanded_high = _exp_finite(high + log_allowance, "upper expanded runtime ratio")
    if expanded_low > threshold:
        gate = "performance_regression"
    elif expanded_high <= threshold:
        gate = "pass"
    else:
        gate = "inconclusive"
    mean_reference = math.fsum(reference) / len(reference)
    mean_candidate = math.fsum(candidate) / len(candidate)
    return {
        "schema": SCHEMA,
        "status": "compared",
        "gate": gate,
        "performance_pass_eligible": gate == "pass",
        "slowdown_threshold_ratio": threshold,
        "confidence": confidence,
        "paired_outer_repeat_count": len(reference),
        "reference_full_seconds_by_repeat": reference,
        "candidate_full_seconds_by_repeat": candidate,
        "paired_log_slowdown_by_repeat": log_ratios,
        "mean_paired_log_slowdown": mean,
        "log_slowdown_standard_error": se,
        "observed_prediction_ratio": raw_ratio,
        "unexpanded_timing_ratio_interval": [raw_low, raw_high],
        "paired_model_log_errors": errors,
        "log_allowance_used": log_allowance,
        "expanded_full_prediction_ratio_interval": [expanded_low, expanded_high],
        "mean_reference_full_seconds": mean_reference,
        "mean_candidate_full_seconds": mean_candidate,
        "absolute_mean_full_seconds_difference": mean_candidate - mean_reference,
        "absolute_mean_full_seconds_difference_magnitude": abs(mean_candidate - mean_reference),
        "correctness_status_is_separate": True,
        "inference": {
            "method": "two-sided Student-t interval on matched outer-repeat log full-time ratios, widened for held-out paired model error",
            "conditional_assumptions": [
                "paired outer repetitions share comparable host conditions and are independent across repeats",
                "reference and candidate use the same full-workload scenario, thread limit, and declared arithmetic policy",
                "the paired ratio model-error allowance is a separate engineering assumption checked on paired holdouts, not implied by the individual model allowances",
            ],
            "distribution_free_coverage_claim": False,
            "multiple_family_adjustment": "caller must Bonferroni-adjust confidence across declared case families",
        },
    }


def transferable_stage_estimate(stage_seconds, source_rates, target_rates,
                                validation_log_errors=None, *, confidence=.95,
                                maximum_validation_error=.20):
    """Estimate target-host seconds from measured stage rates and target holds.

    Without matched target-host stage rates and independently observed target
    full-run residuals, the point estimate is explicitly unvalidated and has no
    interval. Hardware specifications alone are never used to invent a rate.
    """
    confidence = _confidence(confidence)
    allowance = _finite(maximum_validation_error, "maximum_validation_error", minimum=0., strict_minimum=True)
    if allowance >= 1.:
        raise ValueError("maximum_validation_error must be less than one")
    if not isinstance(stage_seconds, dict) or not stage_seconds:
        raise ValueError("stage_seconds must be a nonempty stage-to-seconds object")
    if not isinstance(source_rates, dict) or not isinstance(target_rates, dict):
        raise ValueError("source_rates and target_rates must be stage-to-units-per-second objects")
    stages = set(stage_seconds)
    if stages != set(source_rates) or stages != set(target_rates):
        raise ValueError("stage names must match across measured times and source/target throughput profiles")
    times = {name: _finite(value, f"stage_seconds[{name}]", minimum=0., strict_minimum=True)
             for name, value in stage_seconds.items()}
    source = {name: _finite(source_rates[name], f"source_rates[{name}]", minimum=0., strict_minimum=True)
              for name in stages}
    target = {name: _finite(target_rates[name], f"target_rates[{name}]", minimum=0., strict_minimum=True)
              for name in stages}
    transferred = {name: times[name] * source[name] / target[name] for name in stages}
    point = math.fsum(transferred.values())
    errors = [] if validation_log_errors is None else [
        _finite(value, f"validation_log_errors[{i}]")
        for i, value in enumerate(validation_log_errors)
    ]
    valid = len(errors) >= 3 and max(abs(error) for error in errors) <= allowance
    interval = None
    if valid:
        mean, se, low, high = _outer_t_interval(errors, confidence)
        interval = [point * _exp_finite(low - allowance, "lower target validation multiplier"),
                    point * _exp_finite(high + allowance, "upper target validation multiplier")]
        if not all(math.isfinite(x) for x in interval):
            raise ValueError("target validation interval is outside the finite JSON number range")
    status = "validated_target_profile" if valid else (
        "invalid_scaling" if len(errors) >= 3 else "unvalidated_transfer_estimate")
    return {
        "schema": SCHEMA,
        "status": status,
        "performance_pass_eligible": False,
        "target_seconds_point_estimate": point,
        "target_seconds_validation_interval": interval,
        "stage_seconds_source": times,
        "source_measured_units_per_second": source,
        "target_measured_units_per_second": target,
        "target_stage_seconds_estimates": transferred,
        "holdout_log_errors": errors,
        "maximum_validation_error": allowance,
        "target_profile_measured": True,
        "prediction_has_full_receiver_holdout_validation": valid,
        "inference": {
            "method": "measured per-stage throughput ratios; conditional Student-t interval on independent target full-run log residuals",
            "conditional_assumptions": [
                "source and target profiles cover identical measured stage shapes and software/backend semantics",
                "target full-run holdouts match the modeled scenario and are independent outer runs",
            ],
            "distribution_free_coverage_claim": False,
            "hardware_specifications_alone_used": False,
        },
    }


if __name__ == "__main__":
    raise SystemExit("Import this module from the triad performance runner; no implicit benchmark is executed.")

#!/usr/bin/env python3
"""Sequential precision summaries for independently sampled full-time runs.

The look schedule is fixed before measurement. Intervals are conditional on
independent stationary outer log-runtimes; model-validation uncertainty is a
separate input and never shrinks when more timing repetitions are collected.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
import math
import statistics
from typing import Iterable

import numpy as np
from scipy.stats import t as student_t


MODEL_CONFIRMATION_LOOKS = (3, 5, 9, 17)
MODEL_CONFIRMATION_ALPHA = 0.10
MODEL_CONFIRMATION_FAMILY_ALPHA = MODEL_CONFIRMATION_ALPHA


@dataclass(frozen=True)
class ModelConfirmationPolicy:
    """Immutable fixed-look ledger for one model-error family."""

    looks: tuple[int, ...] = MODEL_CONFIRMATION_LOOKS
    family_alpha: float = MODEL_CONFIRMATION_FAMILY_ALPHA
    scope: str = (
        "Bonferroni family-wise control across the four predeclared looks for one "
        "error family; not simultaneous across reference, candidate, paired metrics, or cases"
    )

    @property
    def per_look_two_sided_alpha(self) -> float:
        return self.family_alpha / len(self.looks)

    @property
    def confidence(self) -> float:
        return 1. - self.per_look_two_sided_alpha

    def to_dict(self) -> dict:
        """Return a fresh JSON-compatible snapshot for a durable report."""
        return {
            "looks": list(self.looks),
            "family_alpha": self.family_alpha,
            "per_look_two_sided_alpha": self.per_look_two_sided_alpha,
            "confidence": self.confidence,
            "confidence_sidedness": "two-sided",
            "scope": self.scope,
            "metric": "mean log(actual full-run time / frozen predicted full-run time)",
            "cross_metric_or_case_simultaneous_claim": False,
        }


MODEL_CONFIRMATION_POLICY = ModelConfirmationPolicy()
MODEL_CONFIRMATION_PER_LOOK_ALPHA = MODEL_CONFIRMATION_POLICY.per_look_two_sided_alpha
MODEL_CONFIRMATION_CONFIDENCE = MODEL_CONFIRMATION_POLICY.confidence


def model_confirmation_policy() -> ModelConfirmationPolicy:
    """Return the immutable confirmation schedule shared by native and FFT runs."""
    return MODEL_CONFIRMATION_POLICY

@dataclass(frozen=True)
class AdaptivePolicy:
    """Predeclared stopping and family-wise error policy.

    `relative_precision` is the maximum multiplicative half-width around the
    geometric-mean runtime, not the error of an abbreviated/full predictor.
    `model_allowance_log` is a non-shrinking engineering allowance in natural
    log units, supplied only when an independently justified bound exists.
    """

    relative_precision: float = 0.05
    looks: tuple[int, ...] = (5, 9, 17, 33)
    family_count: int = 1
    alpha: float = 0.05
    slowdown_threshold: float = 1.30
    model_allowance_log: float = 0.0
    # Keep the historical qualification protocol as the default so old
    # source-bound proof replay does not acquire a different stopping rule.
    stopping_rule: str = "precision"
    pass_alpha: float | None = None
    failure_alpha: float | None = None

    def __post_init__(self) -> None:
        if self.stopping_rule not in ("precision", "decision"):
            raise ValueError("stopping_rule must be precision or decision")
        if not math.isfinite(self.relative_precision) or not 0. < self.relative_precision < 1.:
            raise ValueError("relative_precision must lie strictly between zero and one")
        if (not isinstance(self.looks, tuple) or not self.looks or
                any(isinstance(n, bool) or not isinstance(n, int) or n < 3 for n in self.looks) or
                tuple(sorted(set(self.looks))) != self.looks):
            raise ValueError("looks must be a strictly increasing tuple of integers at least three")
        if isinstance(self.family_count, bool) or not isinstance(self.family_count, int) or self.family_count < 1:
            raise ValueError("family_count must be a positive integer")
        if not math.isfinite(self.alpha) or not 0. < self.alpha < 1.:
            raise ValueError("alpha must lie strictly between zero and one")
        for name in ("pass_alpha", "failure_alpha"):
            value = getattr(self, name)
            if value is not None and (not math.isfinite(value) or not 0. < value < .5):
                raise ValueError(f"{name} must lie strictly between zero and one half")
        if not math.isfinite(self.slowdown_threshold) or self.slowdown_threshold <= 1.:
            raise ValueError("slowdown_threshold must be finite and greater than one")
        if not math.isfinite(self.model_allowance_log) or self.model_allowance_log < 0.:
            raise ValueError("model_allowance_log must be finite and nonnegative")

    @property
    def pass_error_budget(self) -> float:
        return self.alpha if self.pass_alpha is None else self.pass_alpha

    @property
    def failure_error_budget(self) -> float:
        return self.alpha if self.failure_alpha is None else self.failure_alpha


def regression_profile(name: str) -> dict[str, float]:
    """Resolve routine inference controls without changing model qualification."""
    profiles = {"normal": (.15, 1.30), "ci": (.30, 1.70), "strict": (.05, 1.30)}
    if name not in profiles:
        raise ValueError("regression profile must be normal, ci or strict")
    pass_alpha, threshold = profiles[name]
    return {"pass_alpha": pass_alpha, "failure_alpha": .05, "slowdown_threshold": threshold}


def qualification_policy(policy: AdaptivePolicy) -> AdaptivePolicy:
    """Retain the original inference protocol when an automatic route qualifies."""
    threshold = (1.30 if policy.pass_alpha is not None or policy.failure_alpha is not None
                 else policy.slowdown_threshold)
    return replace(policy, stopping_rule="precision", pass_alpha=None, failure_alpha=None,
                   slowdown_threshold=threshold)


def next_look(sample_count: int, policy: AdaptivePolicy) -> int | None:
    """Return the next allowed cumulative sample count, never an interim look."""
    if isinstance(sample_count, bool) or not isinstance(sample_count, int) or sample_count < 0:
        raise ValueError("sample_count must be a nonnegative integer")
    if sample_count not in (0, *policy.looks):
        raise ValueError("sample_count must be zero or a predeclared look")
    return next((look for look in policy.looks if look > sample_count), None)


def _observations(values: Iterable[float], *, name: str, count: int,
                  positive: bool = True) -> np.ndarray:
    try:
        result = np.asarray(tuple(values), dtype=np.float64)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ValueError(f"{name} must contain finite numeric seconds") from exc
    if result.ndim != 1 or result.size != count or not np.isfinite(result).all():
        raise ValueError(f"{name} must contain exactly {count} finite outer observations")
    if positive and np.any(result <= 0.):
        raise ValueError(f"{name} observations must be positive")
    return result


def _log_interval(log_values: np.ndarray, confidence: float) -> dict[str, float | list[float]]:
    n = int(log_values.size)
    center = float(np.mean(log_values))
    spread = float(np.std(log_values, ddof=1))
    se = spread / math.sqrt(n)
    crit = float(student_t.ppf((1. + confidence) / 2., n - 1))
    mean_half = crit * se
    prediction_half = crit * spread * math.sqrt(1. + 1. / n)
    point = math.exp(center)
    mean = [math.exp(center - mean_half), math.exp(center + mean_half)]
    predictive = [math.exp(center - prediction_half), math.exp(center + prediction_half)]
    relative_half = max(mean[1] / point - 1., 1. - mean[0] / point)
    return {
        "point_seconds": point,
        "timing_mean_confidence_interval_seconds": mean,
        "future_execution_prediction_interval_seconds": predictive,
        "log_standard_error": se,
        "relative_timing_half_width": relative_half,
    }


def _widen(interval: list[float], allowance: float) -> list[float]:
    """Apply a fixed multiplicative uncertainty allowance in log units."""
    return [interval[0] * math.exp(-allowance), interval[1] * math.exp(allowance)]


def qualify_log_residuals(log_errors, *, maximum_relative_error, confidence=0.90):
    """Fixed-look equivalence of a frozen forecast and holdout log mean.

    Each residual is log(actual complete-run time / its frozen predicted time).
    This qualifies mean model bias separately from timing precision. It does
    not prove individual or future predictions stay inside the relative-error
    bounds, or qualify a future-run prediction interval.
    """
    from scipy.stats import t

    try:
        values = tuple(float(value) for value in log_errors)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ValueError("at least three finite independent holdout log residuals are required") from exc
    if len(values) < 3 or any(not math.isfinite(value) for value in values):
        raise ValueError("at least three finite independent holdout log residuals are required")
    if not math.isfinite(maximum_relative_error) or not math.isfinite(confidence) or not 0 < maximum_relative_error < 1 or not 0 < confidence < 1:
        raise ValueError("invalid prediction equivalence limit or confidence")
    center = statistics.fmean(values)
    half_width = float(t.ppf((1 + confidence) / 2, len(values) - 1)) * (
        statistics.stdev(values) / math.sqrt(len(values)))
    lower, upper = center - half_width, center + half_width
    limit_low, limit_high = math.log1p(-maximum_relative_error), math.log1p(maximum_relative_error)
    if limit_low <= lower and upper <= limit_high:
        status = "validated"
    elif upper < limit_low or lower > limit_high:
        status = "invalid"
    else:
        status = "inconclusive"
    return {"status": status, "mean_log_residual": center, "log_residual_interval": [lower, upper],
            "confidence": confidence, "relative_mean_error_interval": [math.expm1(lower), math.expm1(upper)],
            "allowed_relative_mean_error": [-maximum_relative_error, maximum_relative_error],
            "sample_count": len(values),
            "scope": "conditional on the frozen forecast and independent stationary normal holdout log times"}


def summarize(reference_seconds: Iterable[float], candidate_seconds: Iterable[float],
              policy: AdaptivePolicy = AdaptivePolicy(), *,
              model_validation: str = "unvalidated",
              reference_model_log_errors: Iterable[float] = (),
              candidate_model_log_errors: Iterable[float] = (),
              paired_model_log_errors: Iterable[float] = ()) -> dict:
    """Summarize one exact, predeclared look of paired full-runtime predictions.

    Every element is an independent outer prediction replicate for a full
    workload. Inner timing loops are not independent observations. For an
    abbreviated estimator, callers should pass predicted full runtimes and
    independently obtained model residuals. Set `model_validation` to
    `validated` only when separate held-out complete runs support that label;
    use `direct_full` for direct full-workload timing.
    """
    ref_seq = tuple(reference_seconds)
    cand_seq = tuple(candidate_seconds)
    count = len(ref_seq)
    if len(cand_seq) != count:
        raise ValueError("reference and candidate must have identical paired lengths")
    if count not in policy.looks:
        raise ValueError("sample count must equal a predeclared look")
    ref = _observations(ref_seq, name="reference_seconds", count=count)
    cand = _observations(cand_seq, name="candidate_seconds", count=count)
    if model_validation not in ("unvalidated", "validated", "direct_full", "invalid"):
        raise ValueError("unknown model_validation state")

    def residuals(values: Iterable[float], name: str) -> np.ndarray:
        data = tuple(values)
        if not data:
            return np.empty(0, dtype=np.float64)
        result = np.asarray(data, dtype=np.float64)
        if result.ndim != 1 or result.size < 3 or not np.isfinite(result).all():
            raise ValueError(f"{name} requires at least three finite independent log residuals")
        return result

    ref_errors = residuals(reference_model_log_errors, "reference_model_log_errors")
    cand_errors = residuals(candidate_model_log_errors, "candidate_model_log_errors")
    pair_errors = residuals(paired_model_log_errors, "paired_model_log_errors")
    if model_validation == "validated" and (ref_errors.size < 3 or cand_errors.size < 3):
        raise ValueError("validated model status requires independent reference and candidate residuals")
    if model_validation == "direct_full" and any(x.size for x in (ref_errors, cand_errors, pair_errors)):
        raise ValueError("direct_full measurements do not accept abbreviation residuals")

    # One Bonferroni ledger protects all three timing intervals (reference,
    # candidate, paired ratio) across every look and family. The regression
    # gate reuses that same paired interval after model-error widening, so it
    # does not spend a second, unstated alpha budget.
    interval_alpha = policy.alpha / (policy.family_count * len(policy.looks) * 3)
    interval_confidence = 1. - interval_alpha
    gate_alpha = interval_alpha
    gate_confidence = interval_confidence
    if policy.stopping_rule == "decision":
        # Each direction has its own per-test error ledger. An upper
        # endpoint is a one-sided (1-alpha/L) bound; the displayed pair is
        # NOT a simultaneous 95% two-sided or whole-suite interval.
        gate_alpha = policy.failure_error_budget / len(policy.looks)
        gate_confidence = 1. - 2. * gate_alpha
    ref_mean = _log_interval(np.log(ref), interval_confidence)
    cand_mean = _log_interval(np.log(cand), interval_confidence)
    log_ratio = np.log(cand) - np.log(ref)
    pair_mean = _log_interval(log_ratio, gate_confidence)
    if policy.stopping_rule == "decision" and policy.pass_error_budget != policy.failure_error_budget:
        upper = _log_interval(log_ratio, 1. - 2. * policy.pass_error_budget / len(policy.looks))
        for key in ("timing_mean_confidence_interval_seconds", "future_execution_prediction_interval_seconds"):
            pair_mean[key][1] = upper[key][1]
        point = pair_mean["point_seconds"]
        low, high = pair_mean["timing_mean_confidence_interval_seconds"]
        pair_mean["relative_timing_half_width"] = max(high / point - 1., 1. - low / point)
    ref_model_bound = max(policy.model_allowance_log,
                          float(np.max(np.abs(ref_errors))) if ref_errors.size else 0.)
    cand_model_bound = max(policy.model_allowance_log,
                           float(np.max(np.abs(cand_errors))) if cand_errors.size else 0.)
    if pair_errors.size:
        pair_model_bound = max(policy.model_allowance_log,
                               float(np.max(np.abs(pair_errors))))
        pair_model_bound_source = "independent paired full-run residuals"
    else:
        pair_model_bound = ref_model_bound + cand_model_bound
        pair_model_bound_source = "sum of separate reference and candidate model-error bounds"
    ref_future = _widen(ref_mean["future_execution_prediction_interval_seconds"], ref_model_bound)
    cand_future = _widen(cand_mean["future_execution_prediction_interval_seconds"], cand_model_bound)
    pair_gate = _widen(pair_mean["timing_mean_confidence_interval_seconds"], pair_model_bound)

    ref_width = ref_mean["relative_timing_half_width"]
    cand_width = cand_mean["relative_timing_half_width"]
    pair_width = pair_mean["relative_timing_half_width"]
    precision_reached = max(ref_width, cand_width, pair_width) <= policy.relative_precision
    model_ok = model_validation in ("validated", "direct_full")
    if not model_ok:
        gate = "unvalidated"
    elif pair_gate[0] > policy.slowdown_threshold:
        gate = "performance_regression"
    elif pair_gate[1] <= policy.slowdown_threshold:
        gate = "pass"
    else:
        gate = "inconclusive"
    if gate == "performance_regression":
        stop_reason = "performance_regression"
    elif policy.stopping_rule == "decision" and gate == "pass":
        stop_reason = "regression_decision_reached"
    elif policy.stopping_rule == "decision" and count == policy.looks[-1]:
        stop_reason = "maximum_samples"
    elif policy.stopping_rule == "decision":
        stop_reason = "continue"
    elif precision_reached:
        stop_reason = "precision_reached"
    elif count == policy.looks[-1]:
        stop_reason = "maximum_samples"
    else:
        stop_reason = "continue"

    result = {
        "schema": "triad-performance-adaptive-v1",
        "sample_count": count,
        "next_look": next_look(count, policy),
        "policy": {
            "relative_precision_target": policy.relative_precision,
            "predeclared_looks": list(policy.looks),
            "family_count": policy.family_count,
            "alpha": policy.alpha,
            "interval_confidence_per_look": interval_confidence,
            "gate_confidence_per_look": gate_confidence,
            "alpha_allocation": {
                "timing_intervals_per_family_look_metric": interval_alpha,
                "three_timing_intervals_total_across_all_looks_and_families": policy.alpha,
                "gate_reuses_paired_timing_interval": True,
            },
            "slowdown_threshold": policy.slowdown_threshold,
            "fixed_model_log_allowance": policy.model_allowance_log,
        },
        "reference": {**ref_mean, "future_execution_prediction_interval_seconds": ref_future,
                      "bounded_model_error_scenario_interval_seconds": ref_future,
                      "future_execution_interval_scope": "scenario range: timing interval widened by fixed allowance and maximum observed independent residual; future model-error coverage is unverified",
                      "statistical_future_coverage_qualified": False,
                      "independent_model_log_error_bound": ref_model_bound,
                      "observed_independent_model_residual_max_log": float(np.max(np.abs(ref_errors))) if ref_errors.size else None,
                      "model_error_bound_is_verified_for_future_runs": False},
        "candidate": {**cand_mean, "future_execution_prediction_interval_seconds": cand_future,
                      "bounded_model_error_scenario_interval_seconds": cand_future,
                      "future_execution_interval_scope": "scenario range: timing interval widened by fixed allowance and maximum observed independent residual; future model-error coverage is unverified",
                      "statistical_future_coverage_qualified": False,
                      "independent_model_log_error_bound": cand_model_bound,
                      "observed_independent_model_residual_max_log": float(np.max(np.abs(cand_errors))) if cand_errors.size else None,
                      "model_error_bound_is_verified_for_future_runs": False},
        "paired_candidate_over_reference": {
            **pair_mean,
            "model_adjusted_gate_interval": pair_gate,
            "paired_model_log_error_bound": pair_model_bound,
            "paired_model_log_error_bound_source": pair_model_bound_source,
        },
        "timing_precision_reached": precision_reached,
        "timing_intervals_simultaneously_within_target": precision_reached,
        "model_validation": model_validation,
        "performance_gate": gate,
        "gate_performance_pass_eligible": gate == "pass",
        "stop_reason": stop_reason,
        "inference": {
            "method": "predeclared-look Student-t intervals on outer log runtimes; paired log-runtime ratio",
            "simultaneous_family_scope": "reference, candidate, and paired timing intervals across declared families and predeclared looks; the regression gate reuses the paired interval and adds a separate model-error bound",
            "conditional_assumptions": [
                "outer runtimes are independent and stationary within the measured hardware, software, thread, and power regime",
                "outer log runtimes are approximately normally distributed",
                "each look is evaluated only at its declared cumulative sample count",
                "the future-execution interval adds run-to-run spread and independently measured log model-error bounds",
                "model allowances do not shrink with more timing repetitions",
            ],
            "distribution_free_coverage_claim": False,
            "timing_precision_is_not_model_accuracy": True,
            "statistical_future_coverage_qualified": False,
            "model_validation_scope": "qualify_log_residuals tests a fixed-look confidence interval for mean log bias only; it does not validate individual-error tails or future-interval coverage",
            "future_execution_intervals_are_engineering_scenario_ranges": True,
            "bounded_model_error_assumption_verified_for_future_runs": False,
        },
    }
    if policy.stopping_rule == "decision":
        result["schema"] = "triad-performance-adaptive-decision-v2"
        result["policy"].update(stopping_rule="decision", per_test_directional_alpha=policy.alpha,
                                one_sided_confidence_across_looks=1. - policy.alpha)
        result["policy"]["alpha_allocation"].update(
            gate_reuses_paired_timing_interval=False,
            gate_per_direction_per_test_look=gate_alpha,
            gate_per_direction_total_across_looks=policy.alpha,
            gate_case_multiplicity_adjustment=False)
        del result["policy"]["alpha_allocation"]["three_timing_intervals_total_across_all_looks_and_families"]
        result["policy"]["alpha_allocation"]["reference_candidate_diagnostic_intervals_total"] = 2. * policy.alpha / 3.
        result["paired_candidate_over_reference"].update(
            estimand="ratio of geometric mean full-workload runtimes",
            one_sided_lower_bound=pair_gate[0], one_sided_upper_bound=pair_gate[1],
            timing_only_one_sided_bounds=pair_mean["timing_mean_confidence_interval_seconds"],
            mean_runtime_not_individual_execution=True)
        result["inference"].update(
            simultaneous_family_scope="reference/candidate timing diagnostics retain their legacy conservative allocation; the paired decision has a separate per-test directional ledger",
            regression_scope="each test separately: one-sided upper bounds spend alpha across fixed looks; lower bounds use a separate directional ledger",
            simultaneous_95_percent_suite_or_two_sided_claim=False,
            confidence_includes_unverified_model_error=False,
            regression_interval_is_conditional_on_model_envelope=model_validation != "direct_full",
            timing_width_is_diagnostic_only=True)
        if policy.pass_alpha is not None or policy.failure_alpha is not None:
            result["policy"].pop("per_test_directional_alpha", None)
            result["policy"].pop("one_sided_confidence_across_looks", None)
            result["policy"].pop("gate_confidence_per_look", None)
            result["policy"].update(pass_alpha=policy.pass_error_budget,
                                    failure_alpha=policy.failure_error_budget,
                                    pass_one_sided_confidence_across_looks=1. - policy.pass_error_budget,
                                    failure_one_sided_confidence_across_looks=1. - policy.failure_error_budget,
                                    pass_one_sided_confidence_per_look=1. - policy.pass_error_budget / len(policy.looks),
                                    failure_one_sided_confidence_per_look=1. - policy.failure_error_budget / len(policy.looks))
            ledger = result["policy"]["alpha_allocation"]
            ledger.pop("gate_per_direction_per_test_look", None)
            ledger.pop("gate_per_direction_total_across_looks", None)
            ledger.update(gate_pass_per_test_look=policy.pass_error_budget / len(policy.looks),
                          gate_failure_per_test_look=policy.failure_error_budget / len(policy.looks),
                          gate_pass_total_across_looks=policy.pass_error_budget,
                          gate_failure_total_across_looks=policy.failure_error_budget)
            result["paired_candidate_over_reference"].update(
                upper_confidence_across_looks=1. - policy.pass_error_budget,
                lower_confidence_across_looks=1. - policy.failure_error_budget)
    return result


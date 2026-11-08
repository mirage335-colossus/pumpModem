"""Compact service, workspace, and exact-success summaries for Robust cases.

Timing values are dimensionless processing/baseband duration ratios. These
conditional summaries support bounded case decisions; they are not hardware,
physical-channel, or deployment qualification.
"""
from __future__ import annotations

import math
import platform
from pathlib import Path

from triad_abbreviated import BATCHES, LOOKS, log_interval

ALPHA = .05
MODEL_LIMIT = .30
PRECISION = .15
REALTIME_LIMIT = 1.0


def _finite_number(value, name, *, positive=False, nonnegative=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(name + " must be a finite number")
    value = float(value)
    if not math.isfinite(value) or (positive and value <= 0) or (nonnegative and value < 0):
        raise ValueError(name + " is outside its valid range")
    return value


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + " must be a positive integer")
    return value


def arithmetic_interval(values, *, alpha):
    """Return an arithmetic mean and conditional Student-t interval.

    Assumes IID outer observations with approximately normal sampling error.
    The lower endpoint is clamped to zero because service ratios are positive.
    This conditional interval does not establish independence or generalize
    beyond the measured fixed-geometry regime.
    """
    values = tuple(_finite_number(x, "observation", positive=True) for x in values)
    if len(values) < 3:
        raise ValueError("at least three finite positive outer observations required")
    alpha = _finite_number(alpha, "alpha", positive=True)
    if alpha >= 1:
        raise ValueError("alpha must be in (0,1)")
    from scipy.stats import t
    center = sum(values) / len(values)
    half = float(t.ppf(1 - alpha / 2, len(values) - 1)) * math.sqrt(
        sum((value - center) ** 2 for value in values) / (len(values) - 1) / len(values)
    )
    return center, [max(0.0, center - half), center + half]


def summarize_service(rows, *, family_size=1):
    """Summarize a predeclared service-ratio look.

    Each outer row contains ratios for 1, 2, and 4 repeated units. The first
    two sizes train the fixed-geometry model and the four-unit size is a
    holdout. No raw elapsed CPU totals or absolute runtime projections escape.
    """
    family_size = _positive_integer(family_size, "family_size")
    if not isinstance(rows, (list, tuple)) or len(rows) not in LOOKS:
        raise ValueError("observations must end at a predeclared look")
    checked = []
    for row in rows:
        if not isinstance(row, (list, tuple)) or len(row) != len(BATCHES):
            raise ValueError("each outer row needs ratios for units 1, 2, and 4")
        checked.append([_finite_number(x, "service ratio", positive=True) for x in row])

    training = [(row[0] + 2 * row[1]) / 3 for row in checked]
    holdout = [row[2] / estimate for row, estimate in zip(checked, training)]
    alpha = ALPHA / (family_size * len(LOOKS) * 3)
    ratio, interval = arithmetic_interval(training, alpha=alpha)
    _, holdout_interval = log_interval(holdout, alpha=alpha)
    error_floor = max(abs(math.log(x)) for x in holdout)
    model_supported = (
        holdout_interval[0] >= 1 / (1 + MODEL_LIMIT)
        and holdout_interval[1] <= 1 + MODEL_LIMIT
        and error_floor <= math.log1p(MODEL_LIMIT)
    )
    training_interval = interval
    allowance = max(error_floor, *(abs(math.log(x)) for x in holdout_interval))
    interval = [training_interval[0] * math.exp(-allowance),
                training_interval[1] * math.exp(allowance)]
    relative_width = max(interval[1] / ratio - 1, 1 - interval[0] / ratio)
    margin = 1 - ratio
    margin_interval = [1 - interval[1], 1 - interval[0]]

    if ratio >= REALTIME_LIMIT:
        status = "fail"
        reason = "point_at_or_above_realtime_limit"
    elif interval[1] < REALTIME_LIMIT and model_supported:
        status = "pass"
        reason = "upper_interval_below_limit_and_holdout_supported"
    else:
        status = "unresolved"
        reason = "conditional_evidence_does_not_resolve_realtime_limit"

    resolved = status != "unresolved"
    precise = model_supported and relative_width <= PRECISION
    final_look = len(checked) == LOOKS[-1]
    stop = resolved or precise or final_look
    if resolved:
        stop_reason = reason
    elif precise:
        stop_reason = "relative_precision"
    elif final_look:
        stop_reason = "bounded_limit"
    else:
        stop_reason = "next_abbreviated_look"
    return {
        "outer_trials": len(checked),
        "ratios": checked,
        "ratio_processing_seconds_per_baseband_second": ratio,
        "ratio_processing_seconds_per_baseband_second_interval": interval,
        "training_ratio_interval": training_interval,
        "margin_fraction": margin,
        "margin_interval": margin_interval,
        "holdout_ratio_interval": holdout_interval,
        "observed_model_log_error_floor": error_floor,
        "model_status": "abbreviated_holdout_supported" if model_supported else "unresolved",
        "realtime_status": status,
        "relative_precision_fraction": relative_width,
        "stop": stop,
        "stop_reason": stop_reason,
    }


def workspace_summary(estimated_peak_bytes, budget_bytes, observed_peak_bytes=None):
    """Compare a workspace estimate and optional observed peak with a budget.

    The conservative peak is the maximum available input. The margin is only
    reported when both an estimate and observation are supplied, and is never
    presented as a memory-safety certificate.
    """
    estimate = _finite_number(estimated_peak_bytes, "estimated_peak_bytes", nonnegative=True)
    budget = _finite_number(budget_bytes, "budget_bytes", positive=True)
    observed = (None if observed_peak_bytes is None else
                _finite_number(observed_peak_bytes, "observed_peak_bytes", nonnegative=True))
    conservative = max(estimate, observed) if observed is not None else estimate
    headroom = budget - conservative
    margin = headroom / budget if observed is not None else None
    failed = conservative >= budget
    return {
        "estimated_peak_bytes": estimate,
        "observed_peak_bytes": observed,
        "budget_bytes": budget,
        "conservative_peak_bytes": conservative,
        "headroom_bytes": headroom,
        "margin_fraction": margin,
        "estimated_margin_fraction": 1 - estimate / budget,
        "status": "failure" if failed else "within_budget",
        "qualification": "not_a_certificate",
    }


def success_interval(successes, trials, *, alpha=.05):
    """Return equal-tail Clopper-Pearson bounds, each with 1-alpha coverage.

    The lower and upper bounds are individually one-sided 1-alpha bounds;
    together they form a central two-sided interval with 1-2*alpha coverage.
    """
    trials = _positive_integer(trials, "trials")
    if isinstance(successes, bool) or not isinstance(successes, int) or not 0 <= successes <= trials:
        raise ValueError("successes must be an integer between zero and trials")
    alpha = _finite_number(alpha, "alpha", positive=True)
    if alpha >= 1:
        raise ValueError("alpha must be in (0,1)")
    from scipy.stats import beta
    lower = 0.0 if successes == 0 else float(beta.ppf(alpha, successes, trials - successes + 1))
    upper = 1.0 if successes == trials else float(beta.ppf(1 - alpha, successes + 1, trials - successes))
    return [lower, upper]


def machine_resources():
    """Take a visible CPU/memory snapshot using existing project detectors."""
    import build_capacity
    import triad_cpu

    warnings = []
    try:
        cpu = triad_cpu.detect_cpu_budget()
    except Exception as error:  # Resource introspection is advisory.
        cpu = None
        warnings.append("CPU detection: " + str(error))
    try:
        available = build_capacity.available_memory()
    except Exception as error:
        available = None
        warnings.append("available-memory detection: " + str(error))

    cgroup = {"status": "not_applicable", "memory_headroom_bytes": []}
    if __import__("sys").platform.startswith("linux"):
        try:
            _, headrooms = build_capacity.linux_limits()
            cgroup = {"status": "visible", "memory_headroom_bytes": headrooms}
            if headrooms:
                available = min([available, *headrooms]) if available is not None else min(headrooms)
        except Exception as error:
            cgroup = {"status": "unknown", "memory_headroom_bytes": []}
            warnings.append("Linux cgroup visibility: " + str(error))

    processor = platform.processor()
    if Path("/proc/cpuinfo").exists():
        try:
            processor = next((line.split(":", 1)[1].strip()
                              for line in Path("/proc/cpuinfo").read_text().splitlines()
                              if line.startswith("model name")), processor)
        except OSError as error:
            warnings.append("CPU model name: " + str(error))
    return {
        "cpu": cpu,
        "cpu_model": processor or None,
        "available_memory_bytes": available,
        "memory_budget_bytes": None if available is None else available // 2,
        "memory_budget_policy": "50_percent_of_currently_available_memory",
        "cgroup_memory": cgroup,
        "warnings": warnings,
    }

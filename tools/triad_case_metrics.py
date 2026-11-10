"""Compact service, workspace, and exact-success summaries for Robust cases.

Timing values are dimensionless processing/baseband duration ratios. These
conditional summaries support bounded case decisions; they are not hardware,
physical-channel, or deployment qualification.
"""
from __future__ import annotations

import math
import statistics

LOOKS = (3, 5, 9)
BATCHES = (1, 2, 4)
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


def _beta_fraction(a, b, x):
    """Continued fraction evaluated by the modified Lentz recurrence."""
    tiny = 1e-300

    def invert(value):
        return 1 / (value if abs(value) >= tiny else math.copysign(tiny, value))

    c = 1.0
    d = invert(1 - (a + b) * x / (a + 1))
    fraction = d
    for m in range(1, 257):
        even = 2 * m
        for numerator in (m * (b - m) * x / ((a + even - 1) * (a + even)),
                          -(a + m) * (a + b + m) * x / ((a + even) * (a + even + 1))):
            d = invert(1 + numerator * d)
            c = 1 + numerator * invert(c)
            if abs(c) < tiny:
                c = math.copysign(tiny, c)
            change = c * d
            fraction *= change
        if abs(change - 1) <= 4e-15:
            return fraction
    raise ValueError("Beta fraction did not converge")


def _beta_cdf(x, a, b):
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    factor = math.exp(math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b)
                      + a * math.log(x) + b * math.log1p(-x))
    if x < (a + 1) / (a + b + 2):
        return factor * _beta_fraction(a, b, x) / a
    return 1 - factor * _beta_fraction(b, a, 1 - x) / b


def _beta_quantile(p, a, b):
    lower, upper = 0.0, 1.0
    for _ in range(60):
        middle = (lower + upper) / 2
        if _beta_cdf(middle, a, b) < p:
            lower = middle
        else:
            upper = middle
    return lower, upper


def t_critical(alpha, degrees):
    """Positive two-sided Student-t critical value for total tail area alpha."""
    if not 0 < alpha < 1 or not math.isfinite(degrees) or degrees <= 0:
        raise ValueError("Invalid Student interval parameters")
    # For t>0, the two-sided tail area is I_{df/(df+t²)}(df/2,1/2).
    # Taking the lower bisection endpoint rounds the critical value outward.
    x, _ = _beta_quantile(alpha, degrees / 2, .5)
    if x <= 0:
        raise ValueError("Student critical value outside supported numerical range")
    return math.sqrt(degrees * (1 - x) / x)


def log_interval(values, *, alpha):
    """Conditional Student interval for the geometric mean of outer observations."""
    values = tuple(_finite_number(x, "observation", positive=True) for x in values)
    if len(values) < 3:
        raise ValueError("at least three finite positive outer observations required")
    logs = tuple(math.log(x) for x in values)
    center = statistics.fmean(logs)
    half = t_critical(alpha, len(logs) - 1) * statistics.stdev(logs) / math.sqrt(len(logs))
    return math.exp(center), [math.exp(center - half), math.exp(center + half)]


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
    center = sum(values) / len(values)
    half = t_critical(alpha, len(values) - 1) * math.sqrt(
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
    if successes == 0:
        return [0., min(1., math.nextafter(-math.expm1(math.log(alpha) / trials), 1.))]
    if successes == trials:
        return [math.nextafter(math.exp(math.log(alpha) / trials), 0.), 1.]
    lower = _beta_quantile(alpha, successes, trials - successes + 1)[0]
    # Invert the upper tail by symmetry. Forming 1-alpha first loses many
    # relative digits of the tail area and can shrink a confidence bound.
    upper = min(1.0, math.nextafter(1 - _beta_quantile(alpha, trials - successes, successes + 1)[0], 1.0))
    return [lower, upper]

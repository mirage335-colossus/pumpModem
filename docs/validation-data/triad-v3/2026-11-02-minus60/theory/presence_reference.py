"""Small historical analytical references for frontier.py.

Recovered from revision 357a50970a0c6214eeaade7c5704bbc39abd4b79:
- tools/triad-link-budget.py: family-adjusted known-waveform Gaussian energy.
- 2026-11-01-sub9/theory/benchmark_math.py: two FP64 OU moment formulas.
Only these formulas and their input checks are retained here. There is no
receiver, waveform simulator, archive loader, or retired operating dependency.
The original files and hashes remain identified in frontier.json and the README.
"""
import math

from scipy.stats import norm

ATTEMPT_SPACING_SECONDS = 60.0
MAX_FAMILY_ALTERNATIVES = 1_000_000_000
# Original default for the two reference formulas; frontier passes its -60 dB-Hz
# C/N0 explicitly, preserving the original comparison.
CN0 = 10 ** (-36.0 / 10)


def _positive_finite(name: str, value: float) -> float:
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be positive and finite")
    return value


def _derived_finite(name: str, value: float, *, positive: bool = False) -> float:
    if not math.isfinite(value) or (positive and value <= 0):
        qualifier = "positive finite" if positive else "finite"
        raise ValueError(f"derived {name} is outside the supported {qualifier} range")
    return value


def _count(name: str, value: int) -> int:
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not 1 <= value <= MAX_FAMILY_ALTERNATIVES):
        raise ValueError(f"{name} must be an integer in [1, {MAX_FAMILY_ALTERNATIVES}]")
    if int(value) != value or not 1 <= value <= MAX_FAMILY_ALTERNATIVES:
        raise ValueError(f"{name} must be an integer in [1, {MAX_FAMILY_ALTERNATIVES}]")
    return int(value)


def required_energy(detection_probability: float, total_alpha: float,
                    horizon_seconds: float, methods: int = 11,
                    durations: int = 4, bit_hypotheses: int = 2,
                    attempt_spacing_seconds: float = ATTEMPT_SPACING_SECONDS) -> dict:
    """Return the strict/relaxed family-adjusted known-waveform E/N0 reference."""
    methods = _count("methods", methods)
    durations = _count("durations", durations)
    bit_hypotheses = _count("bit hypotheses", bit_hypotheses)
    if not 0 < detection_probability < 1:
        raise ValueError("detection probability must be strictly between 0 and 1")
    if not 0 < total_alpha < 1:
        raise ValueError("total alpha must be strictly between 0 and 1")
    horizon_seconds = _positive_finite("horizon seconds", horizon_seconds)
    attempt_spacing_seconds = _positive_finite("attempt spacing seconds", attempt_spacing_seconds)
    ratio = _derived_finite("attempt count", horizon_seconds / attempt_spacing_seconds,
                            positive=True)
    if ratio > MAX_FAMILY_ALTERNATIVES:
        raise ValueError("attempt count exceeds supported family bound")
    attempts = math.ceil(ratio)
    p_false = (total_alpha * 0.5) / (attempts * methods * durations * bit_hypotheses)
    _derived_finite("per-hypothesis false tail", p_false, positive=True)
    z_false = float(norm.isf(p_false))
    z_detection = float(norm.ppf(detection_probability))
    rho = _derived_finite("required known-waveform E/N0", 0.5 * (z_false + z_detection) ** 2,
                          positive=True)
    achieved = float(norm.cdf(math.sqrt(2 * rho) - z_false))
    if abs(achieved - detection_probability) > 2e-14:
        raise ArithmeticError("known-waveform energy inversion self-check failed")
    return {
        "target_detection_probability": detection_probability,
        "total_false_admission_target": total_alpha,
        "score_alpha": total_alpha * 0.5,
        "horizon_seconds": horizon_seconds,
        "attempt_spacing_seconds": attempt_spacing_seconds,
        "scheduled_attempts": attempts,
        "method_alternatives_K": methods,
        "duration_alternatives_D": durations,
        "bit_hypotheses_N": bit_hypotheses,
        "per_hypothesis_false_tail": p_false,
        "known_waveform_Z_false_threshold": z_false,
        "known_waveform_Z_detection_quantile": z_detection,
        "required_known_waveform_E_N0_linear": rho,
        "required_known_waveform_E_N0_db": 10 * math.log10(rho),
        "inversion_check_detection_probability": achieved,
    }


def ou_logdet_one_ray(duration, coherence, cn0=CN0):
    """Exact continuous-time Fredholm log determinant of I + K for one ray."""
    z = math.sqrt(1 + cn0 * coherence)
    x = z * duration / coherence
    a = (1 + cn0 * coherence / 2) / z
    return (-duration / coherence + x
            + math.log((1 + a) / 2 + (1 - a) / 2 * math.exp(-2 * x)))


def ou_variance_h1(duration, coherence, cn0=CN0):
    return cn0 * cn0 / 2 * (
        duration * coherence
        - coherence * coherence / 2 * (-math.expm1(-2 * duration / coherence)))

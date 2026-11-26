#!/usr/bin/env python3
"""Cheap, explicitly favorable benchmarks for the isolated triad coverage census.

These are mathematical reference models, not a duration planner, receiver,
sampled-channel simulation or proof that an unexcluded configuration works.
No spread preset is interpreted as a random Gaussian coherence width.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import NormalDist


CN0_GRID = (200., 140., 120., 100., 80., 60., 55., 40., 32., 20., 6.,
            -6., -10., -16., -20., -23., -26., -30., -60.)
RATE_GRID = (.001, .01, .1, 1., 3.6, 10., 36., 100., 360., 1200., 2400.,
             3600., 12000., 18000., 24000., 1e6, 30e6)
TARGETS = (("strict", .99, 1e-4, 86400.), ("relaxed", .90, .01, 43200.))
NORMAL = NormalDist()


def _positive(value: float, name: str) -> float:
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be finite and positive")
    return value


def _probability(value: float, name: str) -> float:
    value = _positive(value, name)
    if value >= 1:
        raise ValueError(f"{name} must be strictly between zero and one")
    return value


def _count(value: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"{name} must be a positive integer")
    return value


def linear_cn0(db_hz: float) -> float:
    value = float(db_hz)
    # This range includes the requested extreme cases without infinities or
    # silently underflowing the reference noise/power ratio.
    if not math.isfinite(value) or not -300 <= value <= 300:
        raise ValueError("benchmark C/N0 must lie in [-300, 300] dB-Hz")
    return 10. ** (value / 10.)


def known_waveform_presence_energy(pd: float, alpha: float) -> float:
    """Minimum rho=||mu||²/sigma² for known mu in CN(0,sigma² I).

    With known phase, timing, channel and independent white noise, the optimum
    test has Pd = Phi(sqrt(2*rho) - Phi^-1(1-alpha)). A known transmitted
    waveform is supplied even though a real receiver must identify its bit.
    This favorable presence benchmark is not achievable performance with
    unknown synchronization/channel parameters.
    """
    pd, alpha = _probability(pd, "Pd"), _probability(alpha, "alpha")
    # -inv_cdf(alpha) avoids cancellation in forming 1-alpha at tiny tails.
    separation = NORMAL.inv_cdf(pd) - NORMAL.inv_cdf(alpha)
    return .5 * max(0., separation) ** 2


def known_mean_bit_error(distance_squared_over_noise: float) -> float:
    """Equal-prior optimum error Q(sqrt(||mu0-mu1||²/(2*sigma²)))."""
    value = float(distance_squared_over_noise)
    if not math.isfinite(value) or value < 0:
        raise ValueError("squared mean distance/noise must be finite and nonnegative")
    return .5 * math.erfc(.5 * math.sqrt(value))


def equal_energy_bit_requirement(correct: float, real_correlation: float) -> float:
    """Per-bit rho for equal-energy known means with normalized Re correlation.

    Squared distance/noise is 2*rho*(1-Re correlation). Antipodal means use
    -1; orthogonal means use 0. Antipodal/global-phase alternatives need not be
    distinguishable when receiver phase is unknown; this is a known-mean bound.
    """
    correct = _probability(correct, "correct probability")
    correlation = float(real_correlation)
    if correct <= .5 or not math.isfinite(correlation) or not -1 <= correlation < 1:
        raise ValueError("correct probability > 0.5 and correlation in [-1,1) required")
    return NORMAL.inv_cdf(correct) ** 2 / (1 - correlation)


def energy_to_duration(rho: float, cn0_db_hz: float, *, duty_cycle: float = 1.,
                       retained_energy_fraction: float = 1.) -> dict:
    """Map energy to elapsed time with constant active-time received C/N0.

    rho = c * retained_energy_fraction * active_seconds, and
    active_seconds = duty_cycle * wall_seconds. Selected channel energy gain
    is not inferred: c must be the stated active-time received power/noise
    reference and the retention fraction must be justified separately.
    """
    rho = float(rho)
    if not math.isfinite(rho) or rho < 0:
        raise ValueError("energy SNR must be finite and nonnegative")
    duty = _positive(duty_cycle, "duty cycle")
    fraction = _positive(retained_energy_fraction, "retained energy fraction")
    if duty > 1 or fraction > 1:
        raise ValueError("duty cycle and retained energy fraction must be at most one")
    useful = rho / linear_cn0(cn0_db_hz)
    active = useful / fraction
    wall = active / duty
    if not math.isfinite(wall):
        raise ValueError("requested duration is outside finite arithmetic")
    return {"useful_energy_equivalent_seconds": useful,
            "active_seconds_lower_bound": active, "wall_seconds_lower_bound": wall}


def allocated_alpha(alpha: float, *, attempts: int, candidates: int,
                    durations: int, models: int, score_budget_fraction: float) -> tuple[float, int]:
    """A chosen union-bound component allocation, not a necessary search penalty."""
    alpha = _probability(alpha, "horizon alpha")
    fraction = _positive(score_budget_fraction, "score budget fraction")
    if fraction > 1:
        raise ValueError("score budget fraction must be at most one")
    family = math.prod(_count(n, label) for n, label in (
        (attempts, "attempts"), (candidates, "candidates"),
        (durations, "duration alternatives"), (models, "model alternatives")))
    result = alpha * fraction / family
    if not 0 < result < 1:
        raise ValueError("allocated alpha is not numerically representable")
    return result, family


def representation_diagnostic(cn0_db_hz: float, sample_rate_hz: float) -> dict:
    """Scale warnings, not hardware pass/fail or fundamental sensitivity limits.

    Unit-reference sample power/noise = c/Fs. Raw signal+noise addition risks
    losing noise when its amplitude ratio approaches unit roundoff. Covariance
    addition or total-minus-projection residuals can lose it earlier, at the
    power ratio. Conditioning, reduction order and actual sample values matter.
    """
    fs = _positive(sample_rate_hz, "sample rate")
    snr_db = 10 * math.log10(linear_cn0(cn0_db_hz) / fs)
    rows = {}
    for label, bits in (("fp32", 24), ("fp64", 53)):
        power_db = 10 * bits * math.log10(2)
        amplitude_db = 2 * power_db
        rows[label] = {
            "significand_bits": bits,
            "covariance_or_subtractive_residual_scale_db": power_db,
            "raw_signal_plus_noise_scale_db": amplitude_db,
            "covariance_or_subtractive_residual_risk": snr_db >= power_db,
            "raw_signal_plus_noise_risk": snr_db >= amplitude_db,
        }
    return {"sample_snr_db": snr_db, "sample_rate_hz": fs, "formats": rows,
            "classification": "numerical_scale_diagnostic_not_physical_exclusion"}


def gaussian_covariance_square_integral(seconds: float, width_hz: float) -> float:
    """Integral of |R(t-s)|² over [0,T]² for R=exp(-(pi*B*t)²/2).

    B is explicitly a zero-mean Gaussian ray's 2*sigma Doppler width. It is
    never supplied by a deterministic ray-center-separation preset. This is
    a continuous-time model, not the finite sampled C++ FIR covariance.
    """
    t, b = _positive(seconds, "duration"), _positive(width_hz, "Gaussian width")
    u = math.pi * b * t
    if not math.isfinite(u):
        raise ValueError("Gaussian time-width product is outside finite arithmetic")
    if u < .1:
        # Exact convergent series avoids the cancellation of two O(T²) terms.
        terms = [2 * (-u*u)**k / ((2*k+1)*(2*k+2)*math.factorial(k)) for k in range(10)]
        ratio = math.fsum(terms)
    else:
        inverse = 1/u
        exponential_difference = math.expm1(-u*u) if u < 1e150 else -1.
        ratio = math.sqrt(math.pi)*math.erf(u)*inverse + exponential_difference*inverse*inverse
    result = t*t*ratio
    if not math.isfinite(result) or result <= 0:
        raise ValueError("Gaussian covariance integral is outside finite arithmetic")
    return result


def gaussian_presence_upper(cn0_db_hz: float, seconds: float, width_hz: float,
                            peak_power_upper: float = 1.) -> dict:
    """Conditional necessary-information screen for one explicitly named law.

    Assume zero-mean proper-complex Gaussian fading, continuous white noise,
    and |K(t,s)| <= c*P*exp(-(pi*B*(t-s))²/2). No selective operator is added.
    Then KL <= min(tr K, tr K²/2). Smaller values than binary decision KL
    exclude that decision target in THIS observation model only. Larger values
    are inconclusive. Stable deterministic rays and random non-Gaussian
    selective-operator mixtures are outside this screen.
    """
    t = _positive(seconds, "duration")
    peak = _positive(peak_power_upper, "peak-power upper bound")
    scale = linear_cn0(cn0_db_hz)*peak
    integral = gaussian_covariance_square_integral(t, width_hz)
    upper = min(scale*t, .5*scale*scale*integral)
    targets = {}
    for name, pd, alpha, _ in TARGETS:
        required = pd*math.log(pd/alpha)+(1-pd)*math.log((1-pd)/(1-alpha))
        targets[name] = {"required_binary_kl_nats": required,
                         "excluded_in_this_continuous_gaussian_model": upper < required}
    return {"cn0_db_hz": cn0_db_hz, "seconds": t, "B_2sigma_hz": width_hz,
            "peak_power_upper": peak, "kl_upper_nats": upper, "targets": targets,
            "law": "continuous_zero_mean_Gaussian_covariance_envelope_without_selective_modulation",
            "not_a_spread_preset_or_sampled_channel_prediction": True}


def build_report(*, cn0_grid=CN0_GRID, rates=RATE_GRID, spreading=1,
                 sample_rate_hz=2048., duty_cycle=1., retained_energy_fraction=1.,
                 attempt_spacing_seconds=60., candidates=2, durations=8, models=16,
                 score_budget_fraction=.5, gaussian_widths=(),
                 gaussian_seconds=315576000., gaussian_peak_power_upper=1.) -> dict:
    grid = sorted(set(float(v) for v in cn0_grid))
    rate_grid = sorted(set(_positive(v, "rate") for v in rates))
    if not grid or not rate_grid:
        raise ValueError("nonempty C/N0 and rate grids required")
    for value in grid:
        linear_cn0(value)
    spreading = _count(spreading, "spreading")
    spacing = _positive(attempt_spacing_seconds, "attempt spacing")
    rows = []
    for cn0 in grid:
        targets = {}
        for name, pd, alpha, horizon in TARGETS:
            component_alpha, family = allocated_alpha(alpha,
                attempts=math.ceil(horizon/spacing), candidates=candidates,
                durations=durations, models=models, score_budget_fraction=score_budget_fraction)
            one = known_waveform_presence_energy(pd, alpha)
            allocated = known_waveform_presence_energy(pd, component_alpha)
            def duration(energy):
                return {"required_energy_snr": energy, **energy_to_duration(energy, cn0,
                    duty_cycle=duty_cycle, retained_energy_fraction=retained_energy_fraction)}
            targets[name] = {"correct_probability": pd, "horizon_alpha": alpha,
                "horizon_seconds": horizon,
                "known_waveform_one_completion_presence_floor": duration(one),
                "known_waveform_chosen_family_allocation": {
                    "attempt_count": math.ceil(horizon/spacing), "family_count": family,
                    "component_alpha": component_alpha, **duration(allocated)},
                "known_antipodal_bit_classification_floor": duration(equal_energy_bit_requirement(pd,-1)),
                "known_orthogonal_bit_classification_reference": duration(equal_energy_bit_requirement(pd,0))}
        rows.append({"cn0_db_hz": cn0, "targets": targets,
                     "representation": representation_diagnostic(cn0,sample_rate_hz)})
    geometry = [{"gui_rate_hz": r, "inner_rate_per_carrier_hz": r/6,
        "fine_rate_per_carrier_hz": r*spreading/6,
        "spacing_hz": 1.25*r*spreading/6,
        "nominal_triplet_bandwidth_hz": .625*r*spreading,
        "minimum_pattern_active_seconds_lower_bound": max(8.,384./r),
        "note": "64 inner coefficients and 8-second floor only; omits halos/tile rounding/native limits"}
        for r in rate_grid]
    return {"schema": "triad-coverage-bounds-v1",
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "cn0_grid_sorted": grid,
        "rate_geometry": geometry, "cn0_rows": rows,
        "configuration": {"spreading":spreading,"sample_rate_hz":sample_rate_hz,
            "duty_cycle":duty_cycle,"retained_energy_fraction":retained_energy_fraction,
            "attempt_spacing_seconds":spacing,"candidates":candidates,"durations":durations,
            "models":models,"score_budget_fraction":score_budget_fraction},
        "optional_gaussian_screens": [gaussian_presence_upper(c,gaussian_seconds,b,gaussian_peak_power_upper)
            for b in gaussian_widths for c in grid],
        "limits": [
            "These are favorable benchmarks and conditional exclusions, not achieved reception, minimum feasible duration or native configuration validation.",
            "C/N0 is total triplet received power per noise density during active transmission. Actual useful energy includes measured channel, shaping, outages and capture effects.",
            "Known-mean bit classification, signal presence and correct admission are different decisions; antipodal means are not identifiable under arbitrary unknown common phase.",
            "The one-completion presence floor gives a known waveform the whole horizon allowance. The separately reported component-family allocation is chosen Bonferroni accounting, not a universal necessary search penalty.",
            "Duty-cycle conversion holds active-time power fixed and does not model reacquisition, oscillator evolution, erasure position or receiver backlog.",
            "Rate/spreading geometry does not establish hardware passband, representability, anti-aliasing, minimum distinguishable keyed pattern or complete interleave support.",
            "Optional Gaussian widths are explicit additional laws, never inferred from preset ray separation. Their continuous model does not include the sampled Gaussian FIR, arbitrary clock interpolation, selective mixtures or physical field calibration.",
            "Floating-point scale warnings are neither guaranteed numerical failure nor physical impossibility. Structured low-rank likelihoods/direct residuals may avoid subtractive loss without globally wider arithmetic."]}


def _grid(text: str) -> list[float]:
    return [float(v) for v in text.split(",") if v.strip()]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--cn0-grid", type=_grid, default=CN0_GRID)
    p.add_argument("--rates", type=_grid, default=RATE_GRID)
    p.add_argument("--spreading", type=int, default=1)
    p.add_argument("--sample-rate", type=float, default=2048.)
    p.add_argument("--duty-cycle", type=float, default=1.)
    p.add_argument("--retained-energy-fraction", type=float, default=1.)
    p.add_argument("--attempt-spacing-seconds", type=float, default=60.)
    p.add_argument("--candidates", type=int, default=2)
    p.add_argument("--duration-alternatives", type=int, default=8)
    p.add_argument("--model-alternatives", type=int, default=16)
    p.add_argument("--score-budget-fraction", type=float, default=.5)
    p.add_argument("--gaussian-widths", type=_grid, default=())
    p.add_argument("--gaussian-seconds", type=float, default=315576000.)
    p.add_argument("--gaussian-peak-power-upper", type=float, default=1.)
    a = p.parse_args()
    try:
        report = build_report(cn0_grid=a.cn0_grid,rates=a.rates,spreading=a.spreading,
            sample_rate_hz=a.sample_rate,duty_cycle=a.duty_cycle,
            retained_energy_fraction=a.retained_energy_fraction,
            attempt_spacing_seconds=a.attempt_spacing_seconds,candidates=a.candidates,
            durations=a.duration_alternatives,models=a.model_alternatives,
            score_budget_fraction=a.score_budget_fraction,gaussian_widths=a.gaussian_widths,
            gaussian_seconds=a.gaussian_seconds,gaussian_peak_power_upper=a.gaussian_peak_power_upper)
        if a.output.exists():
            raise ValueError("output exists; choose a fresh evidence path")
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    except (ValueError, OverflowError, OSError) as error:
        p.error(str(error))
    print(json.dumps({"status":"benchmark_only","cn0_rows":len(report['cn0_rows']),
        "rates":len(report['rate_geometry']),"gaussian_screens":len(report['optional_gaussian_screens']),
        "output":str(a.output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

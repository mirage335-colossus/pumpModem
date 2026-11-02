#!/usr/bin/env python3
"""Analytical -60 dB-Hz presence references, not receiver measurements.

Reproduce with the project's scientific Python environment:
  python frontier.py --output frontier.json
  python -m unittest discover -s . -p test_frontier.py

The OU equations are those in the archived sub-9-kHz benchmark_math.py. This
version evaluates algebraically equivalent expressions at80 decimal digits to
avoid cancellation at small C*tau or T/tau. The archived functions are imported
only for an independent numeric parity check on the requested finite grid.

Cantelli bounds concern this matched Gaussian presence likelihood at the stated
conservative likelihood-ratio gate. They are not impossibility bounds for all
detectors, an exact Neyman-Pearson operating curve, or correct-bit probabilities.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

import mpmath as mp
import scipy
from scipy.stats import gamma


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
ARCHIVED_MATH = HERE.parents[1] / "2026-11-01-sub9/theory/benchmark_math.py"
LINK_BUDGET = ROOT / "tools/triad-link-budget.py"
CN0_DB_HZ = -60.0
CN0 = 10.0 ** (CN0_DB_HZ / 10.0)
DAYS = (38., 250., 400., 750., 1000., 1500., 3652.5)
COHERENCES = (1e4, 1e5, 1e6, 1e7, 1e8, math.inf)
SCHEDULES = (("minute", 60.), ("hourly", 3600.), ("daily", 86400.))
TARGETS = (("strict", .99, 1e-4, 86400.), ("relaxed", .90, .01, 43200.))
METHODS = 16
DURATION_ALTERNATIVES = 8


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def moments(duration, coherence, cn0=CN0):
    """Exact-model H1 moments; returned floats are not interval arithmetic.

    Two independent orthogonal ray observation channels each have covariance
    K(t,s)=(C/2)*exp(-abs(t-s)/tau). They share a known transmitted pattern.
    Under H1, log LR = sum_j lambda_j*Exp_j(1)-logdet(I+K_total).
    Thus E(log LR)=C*T-logdet and Var(log LR)=tr(K_total**2).
    """
    if not math.isfinite(duration) or duration <= 0 or not math.isfinite(cn0) or cn0 <= 0:
        raise ValueError("duration and C/N0 must be finite positive")
    if not (coherence == math.inf or math.isfinite(coherence) and coherence > 0):
        raise ValueError("coherence must be positive finite or positive infinity")
    with mp.workdps(80):
        t, c = mp.mpf(str(duration)), mp.mpf(str(cn0))
        energy = c*t
        if coherence == math.inf:
            logdet = 2*mp.log1p(energy/2)
            variance = energy*energy/2
            rate = c
        else:
            tau = mp.mpf(str(coherence))
            q, u = c*tau, t/tau
            z = mp.sqrt(1+q)
            zm1 = q/(z+1)
            one_ray_logdet = zm1*u + mp.log1p(zm1*zm1/(4*z)*(-mp.expm1(-2*z*u)))
            logdet = 2*one_ray_logdet
            variance = c*c*tau*tau/4*(2*u+mp.expm1(-2*u))
            rate = c*q/(z+1)**2
        mean = energy-logdet
        result = {"nominal_mean_energy_E_N0": float(energy),
                  "two_ray_log_determinant": float(logdet),
                  "H1_logLR_mean": float(mean), "H1_logLR_variance": float(variance),
                  "asymptotic_KL_rate_nats_per_second": float(rate)}
    if any(not math.isfinite(v) or v < 0 for v in result.values()):
        raise ArithmeticError("derived moments are outside supported numeric range")
    return result


def cantelli(mean, variance, gate):
    """Analytical one-sided moment bounds, with a trivial opposite endpoint."""
    if not all(math.isfinite(v) for v in (mean, variance, gate)) or variance <= 0:
        raise ValueError("Cantelli inputs must be finite with positive variance")
    delta = gate-mean
    if delta > 0:
        return {"lower": 0., "upper": variance/(variance+delta*delta)}
    if delta < 0:
        return {"lower": delta*delta/(variance+delta*delta), "upper": 1.}
    return {"lower": 0., "upper": 1.}


def static_presence(energy, gate):
    """Exact survival under a stationary two-ray Gaussian-gain prior.

    Each ray has lambda=E/2. H1 logLR=lambda*Gamma(2,1)-2*log1p(lambda);
    H0 replaces lambda by lambda/(1+lambda). These are averaged over Gaussian
    gains, including deep fades; they do not assume fixed realized ray power.
    """
    if not math.isfinite(energy) or energy <= 0 or not math.isfinite(gate):
        raise ValueError("invalid static probability inputs")
    lam = energy/2
    shifted_gate = gate+2*math.log1p(lam)
    return {"exact_H1_presence_probability": float(gamma.sf(shifted_gate/lam, 2)),
            "exact_H0_false_probability_per_component": float(gamma.sf(shifted_gate*(1+lam)/lam, 2))}


def gates_and_known_waveform():
    link_budget = load_module("triad_link_budget_frontier", LINK_BUDGET)
    result = []
    for schedule, spacing in SCHEDULES:
        for target, pd, alpha, horizon in TARGETS:
            reference = link_budget.required_energy(pd, alpha, horizon,
                methods=METHODS, durations=DURATION_ALTERNATIVES, bit_hypotheses=2,
                attempt_spacing_seconds=spacing)
            result.append({"id": schedule+"-"+target,
                "declared_schedule": schedule,
                "target_profile": target,
                "matched_LR_log_gate": -math.log(reference["per_hypothesis_false_tail"]),
                "known_waveform_reference": reference,
                "required_known_waveform_useful_seconds_at_minus60":
                    reference["required_known_waveform_E_N0_linear"]/CN0,
                "required_known_waveform_useful_days_at_minus60":
                    reference["required_known_waveform_E_N0_linear"]/CN0/86400})
    return result


def archived_parity():
    previous = load_module("triad_archived_ou_math", ARCHIVED_MATH)
    logdet_errors, variance_errors = [], []
    for days in DAYS:
        for coherence in COHERENCES[:-1]:
            t = days*86400
            current = moments(t, coherence)
            ld = 2*previous.ou_logdet_one_ray(t, coherence, CN0)
            variance = previous.ou_variance_h1(t, coherence, CN0)
            logdet_errors.append(abs(ld-current["two_ray_log_determinant"])/current["two_ray_log_determinant"])
            variance_errors.append(abs(variance-current["H1_logLR_variance"])/current["H1_logLR_variance"])
    if max(logdet_errors) > 5e-12 or max(variance_errors) > 5e-12:
        raise ArithmeticError("archived OU formulas disagree on the requested grid")
    return {"finite_grid_points": len(logdet_errors),
            "maximum_relative_logdet_difference": max(logdet_errors),
            "maximum_relative_variance_difference": max(variance_errors),
            "scope": "same analytical formulas, independently arranged numerical evaluation; not independent physical validation"}


def report():
    gates = gates_and_known_waveform()
    rows = []
    for days in DAYS:
        for coherence in COHERENCES:
            values = moments(days*86400, coherence)
            bounds = []
            for profile in gates:
                gate = profile["matched_LR_log_gate"]
                admission = {"profile": profile["id"], "log_gate": gate,
                    "H1_presence_Cantelli_bounds": cantelli(values["H1_logLR_mean"],
                                                            values["H1_logLR_variance"], gate)}
                if coherence == math.inf:
                    admission.update(static_presence(values["nominal_mean_energy_E_N0"], gate))
                bounds.append(admission)
            q = None if coherence == math.inf else CN0*coherence
            rows.append({"useful_days": days, "useful_seconds": days*86400,
                "coherence_seconds": "infinity" if coherence == math.inf else coherence,
                "C_times_coherence": q, **values,
                "low_Ctau_rate_approximation_nats_per_second": None if q is None else CN0*q/4,
                "admission": bounds})
    paths = [Path(__file__), ARCHIVED_MATH, LINK_BUDGET]
    return {"schema": "triad-minus60-presence-frontier-v1", "cn0_db_hz": CN0_DB_HZ,
        "cn0_linear_hz": CN0,
        "scope": "Analytical presence evidence for specified ideal signal models; no sampled-waveform or practical receiver measurement.",
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        "numeric_environment": {"mpmath_version": mp.__version__, "mpmath_decimal_digits": 80,
                                "scipy_version": scipy.__version__},
        "known_waveform_and_gate_profiles": gates,
        "OU_and_static_Gaussian_rows": rows,
        "archived_formula_parity": archived_parity(),
        "equations": {
            "ray_kernel": "K_c(t,s)=(C/2)*exp(-abs(t-s)/tau), c=1,2 independently observed orthogonal rays",
            "H1_logLR": "sum_j lambda_j*Exp_j(1)-sum_j log(1+lambda_j), summed across both rays",
            "mean": "C*T-2*logdet_one_ray",
            "variance": "C^2/2*(T*tau-tau^2/2*(1-exp(-2*T/tau)))",
            "logdet_one_ray": "-T/tau+log(cosh(z*T/tau)+(1+C*tau/2)/z*sinh(z*T/tau)); z=sqrt(1+C*tau)",
            "asymptotic_KL_rate": "C*(C*tau)/(sqrt(1+C*tau)+1)^2; tends to C^2*tau/4 only when C*tau << 1",
            "static_limit": "logLR_H1=(C*T/2)*Gamma(2,1)-2*log(1+C*T/2)",
            "Cantelli": "P(X>=g)<=v/(v+(g-m)^2) when g>m; P(X>=g)>=1-v/(v+(m-g)^2) when m>g"},
        "assumptions_and_limits": [
            "C is total-triplet C/N0 in Hz (inverse seconds), not an information rate; nominal expected E/N0=C*T.",
            "All T is usable full-power-equivalent time; no outage, shaping, start/stop or interleaving overhead is charged.",
            "The OU presence observer knows the transmitted waveform, timing, epoch, deterministic ray Dopplers and common oscillator phase; only stationary proper-complex Gaussian ray coefficients are marginalized.",
            "Two ray observation channels are exactly orthogonal, each with nominal power1/2. This is an ideal observation model, not the summed sampled triad waveform.",
            "Gaussian noise is white with exactly known stationary spectral density. No noise-scale calibration error is simulated.",
            "The static Gaussian prior includes random realized total power and deep fades; it is distinct from the physical simulator's fixed equal-power rays.",
            "The likelihood gate uses Markov's null bound and union accounting over16 method,8 duration,2 bit alternatives and the declared attempts. Half of the total alpha goes to scores and half remains reserved for calibration. It is not an exact Neyman-Pearson null quantile for the OU law.",
            "Daily means one scheduled attempt in either the24-hour strict horizon or12-hour relaxed horizon. A sparser schedule is an operational restriction, not a receiver gain.",
            "OU moments are evaluated at80 decimal digits, then rounded to FP64. Cantelli bounds are evaluated in FP64 from those rounded moments; no interval-arithmetic certificate is claimed.",
            "These presence probabilities and moment bounds do not include a competing wrong-bit likelihood or establish correct-bit reception. An upper bound here does not exclude all alternative detectors at the same overall false-admission probability.",
            "Known-waveform energy requirements apply only to their assigned per-component Gaussian false tail. The family union allocation is conservative and not a universal fundamental minimum duration.",
            "No normal approximation, Monte Carlo trial, sampled channel simulation or empirical rare-tail extrapolation is used."],
        "research_context": [
            {"title": "On Channel Coherence in the Low SNR Regime",
             "url": "https://www.mit.edu/~medard/allerton1.pdf",
             "relevance": "Analyzes how low-SNR channel estimation and achievable rates depend jointly on coherence and SNR; it does not validate this modem or these finite-duration detection targets."},
            {"title": "On the Sensitivity of Continuous-Time Noncoherent Fading Channel Capacity",
             "url": "https://arxiv.org/abs/1107.2527",
             "relevance": "Studies underspread continuous-time fading capacity and channel-model sensitivity; capacity conclusions are not directly transferred into one-bit presence probabilities."}]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(report(), indent=2, sort_keys=True, allow_nan=False)+"\n")


if __name__ == "__main__":
    main()

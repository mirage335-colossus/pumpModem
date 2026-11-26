#!/usr/bin/env python3
"""Conditional information limits for stochastic Doppler models.

These calculations are analytical model evidence, not triad receiver trials.
Reproduce with: python spread_frontier.py --output spread_frontier.json
Tests: python -m unittest discover -s THIS_DIRECTORY -p test_spread_frontier.py

For proper-complex Gaussian signal covariance K and unit white noise,
D(P_signal||P_noise)=tr(K)-logdet(I+K). A decision event with probability at
least p under signal and at most alpha under noise requires D>=d(p||alpha),
by data processing. This necessity holds for ANY detector of that specified
observation model. It is stronger in scope than a bound on one LR threshold,
but it is not an achievable-performance or universal physical-channel claim.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import mpmath as mp
import numpy as np
import scipy
from scipy.integrate import quad
from scipy.linalg import cho_factor, cho_solve
from scipy.optimize import brentq
from scipy.special import erfc
from scipy.stats import norm


CN0_DB_VALUES = (-60., -50., -40., -30., -20., -10., 0.)
GAUSSIAN_WIDTHS_2SIGMA_HZ = (1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4,
                            .001, .005, .05, .1, 1., 10., 30.)
MAX_SECONDS = 10*365.25*86400.
METHODS, DURATIONS, BITS = 16, 8, 2
PAPR_EXAMPLES = (1., 3., 10.)
MP_DIGITS = 70


def positive(value, label):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(label+" must be positive finite")
    return value


def binary_kl(p, q):
    if not 0 < p < 1 or not 0 < q < 1:
        raise ValueError("binary probabilities must lie strictly between zero and one")
    return p*math.log(p/q)+(1-p)*math.log((1-p)/(1-q))


def required_known_waveform_energy(pd, alpha):
    """Exact fixed deterministic-waveform Gaussian NP presence reference."""
    if not 0 < alpha < pd < 1:
        raise ValueError("require 0 < alpha < pd < 1")
    return .5*(float(norm.isf(alpha))+float(norm.ppf(pd)))**2


def profiles():
    rows = []
    for name, pd, alpha, horizon in (("strict", .99, 1e-4, 86400.),
                                    ("relaxed", .9, .01, 43200.)):
        attempts = math.ceil(horizon/60)
        for allocation in ("one-completion-global-alpha", "minute-family-allocation"):
            component_alpha = alpha if allocation.startswith("one-") else alpha*.5/(attempts*METHODS*DURATIONS*BITS)
            rows.append({"id": name+"-"+allocation, "target_correct_probability": pd,
                "horizon_seconds": horizon, "total_false_admission_probability": alpha,
                "false_probability_used_in_necessity": component_alpha,
                "required_binary_KL_nats": binary_kl(pd, component_alpha),
                "known_fixed_waveform_required_E_N0": required_known_waveform_energy(pd, component_alpha),
                "scope": ("Necessary for one completed decision event whose false admission is bounded by the whole horizon alpha; no division by search count is presumed."
                          if allocation.startswith("one-") else
                          "Necessary only if this component is individually assigned half-alpha divided across minute attempts,16 methods,8 durations,2 bits. Such an allocation is sufficient family accounting, not necessary for every family-wise detector.")})
    return rows


def spectral_parameters(width_2sigma):
    """Lorentzian is matched in HWHM, since its RMS frequency does not exist."""
    width = positive(width_2sigma, "Gaussian 2sigma width")
    sigma = width/2
    half_width = sigma*math.sqrt(2*math.log(2))
    return {"Gaussian_width_2sigma_hz": width, "Gaussian_sigma_hz": sigma,
        "matched_spectral_HWHM_hz": half_width,
        "Gaussian_correlation_efold_seconds": 1/(math.sqrt(2)*math.pi*sigma),
        "OU_correlation_efold_seconds": 1/(2*math.pi*half_width),
        "Gaussian_squared_correlation_integral_seconds": 1/(2*math.sqrt(math.pi)*sigma),
        "OU_squared_correlation_integral_seconds": 1/(2*math.pi*half_width)}


def correlation_integrals(duration, width_2sigma, spectrum):
    """J=integral_0^T |r(u)|²du, I=integral_0^T (T-u)|r(u)|²du."""
    t = positive(duration, "duration")
    p = spectral_parameters(width_2sigma)
    with mp.workdps(MP_DIGITS):
        T = mp.mpf(str(t))
        if spectrum == "gaussian":
            sigma = mp.mpf(str(width_2sigma))/2
            a = 4*mp.pi**2*sigma**2
            J = mp.sqrt(mp.pi)/(2*mp.sqrt(a))*mp.erf(mp.sqrt(a)*T)
            I = T*J+mp.expm1(-a*T*T)/(2*a)
            peak = 1/(sigma*mp.sqrt(2*mp.pi))
        elif spectrum == "ou":
            tau = mp.mpf(str(p["OU_correlation_efold_seconds"]))
            kappa = 2/tau
            J = -mp.expm1(-kappa*T)/kappa
            I = T/kappa+mp.expm1(-kappa*T)/(kappa*kappa)
            peak = 2*tau
        else:
            raise ValueError("spectrum must be gaussian or ou")
        return float(J), float(I), float(peak)


def ou_exact_kl(duration, tau, cn0, channels=2):
    """Exact finite-T Fredholm KL; channels equal-power orthogonal processes."""
    T, tau, C = (positive(duration, "duration"), positive(tau, "coherence"),
                 positive(cn0, "C/N0"))
    if channels not in (1, 2):
        raise ValueError("channels must be1 or2")
    with mp.workdps(MP_DIGITS):
        t, tc, c = map(lambda x: mp.mpf(str(x)), (T, tau, C))
        q = 2*c*tc/channels
        z = mp.sqrt(1+q)
        zm1 = q/(z+1)
        ld = zm1*t/tc+mp.log1p(zm1*zm1/(4*z)*(-mp.expm1(-2*z*t/tc)))
        return float(c*t-channels*ld)


def finite_kl_bounds(duration, width_2sigma, spectrum, cn0, channels=2):
    """Finite-T exact OU KL or analytic Gaussian covariance bounds.

    For eigenvalues lambda in [0,L], lambda²/(2*(1+L)) <=
    lambda-log(1+lambda) <=lambda²/2. Also logdet(I+K)>=
    log(1+tr K) separately in each independent equal-power channel.
    """
    T, C = positive(duration, "duration"), positive(cn0, "C/N0")
    if channels not in (1, 2):
        raise ValueError("channels must be1 or2")
    J, I, peak = correlation_integrals(T, width_2sigma, spectrum)
    energy = C*T
    trace_square = 2*C*C*I/channels
    lambda_bound = C/channels*min(T, peak)
    with mp.workdps(MP_DIGITS):
        e = mp.mpf(str(C))*mp.mpf(str(T))
        static_bound = float(e-channels*mp.log1p(e/channels))
    lower = trace_square/(2*(1+lambda_bound))
    upper = min(trace_square/2, static_bound)
    exact = None
    if spectrum == "ou":
        tau = spectral_parameters(width_2sigma)["OU_correlation_efold_seconds"]
        exact = ou_exact_kl(T, tau, C, channels)
        tolerance = 1e-12*max(exact, upper, 1e-100)
        if not lower-tolerance <= exact <= upper+tolerance:
            raise ArithmeticError("OU exact KL escaped covariance bounds")
    return {"finite_KL_lower_nats": lower, "finite_KL_upper_nats": upper,
        "finite_KL_exact_nats": exact, "covariance_trace": energy,
        "covariance_trace_square": trace_square, "maximum_eigenvalue_upper": lambda_bound,
        "squared_correlation_J_seconds": J}


def g(x):
    """x-log1p(x), avoiding low-x cancellation."""
    if x < 1e-3:
        return x*x*(.5+x*(-1/3+x*(.25+x*(-.2+x/6))))
    return x-math.log1p(x)


def spectral_kl_rate(width_2sigma, spectrum, cn0, channels=2):
    """Long-time rate only; multiplying by finite T is NOT a finite-T bound."""
    C = positive(cn0, "C/N0")
    p = spectral_parameters(width_2sigma)
    if channels not in (1, 2):
        raise ValueError("channels must be1 or2")
    if spectrum == "ou":
        q = 2*C*p["OU_correlation_efold_seconds"]/channels
        rate = C*q/(math.sqrt(1+q)+1)**2
        return {"KL_rate_nats_per_second": rate, "quadrature_estimated_absolute_error": 0.,
                "omitted_tail_analytic_upper": 0.}
    if spectrum != "gaussian":
        raise ValueError("spectrum must be gaussian or ou")
    sigma = p["Gaussian_sigma_hz"]
    a = C/(channels*sigma*math.sqrt(2*math.pi))
    scale = g(a)
    # Tail uses g(x)<=x²/2; frequency is standardized by sigma.
    limit = math.sqrt(max(80., 2*math.log(a)+80.))
    integral, error = quad(lambda u: g(a*math.exp(-u*u/2))/scale,
                           0., limit, epsabs=1e-11, epsrel=1e-11, limit=200)
    prefactor = 2*channels*sigma*scale
    tail = channels*sigma*a*a*math.sqrt(math.pi)*float(erfc(limit))/2
    return {"KL_rate_nats_per_second": prefactor*integral,
            "quadrature_estimated_absolute_error": prefactor*error,
            "omitted_tail_analytic_upper": tail}


def upper_power_given_kl(kl, alpha):
    """Largest possible event probability allowed by binary-KL necessity."""
    if not math.isfinite(kl) or kl < 0 or not 0 < alpha < 1:
        raise ValueError("invalid KL or false probability")
    if kl >= -math.log(alpha):
        return 1.
    if kl == 0:
        return alpha
    return float(brentq(lambda p: binary_kl(p, alpha)-kl, alpha+1e-16,
                         1-1e-15, xtol=1e-14))


def necessary_duration(upper_function, required_kl):
    """Lower bound on feasible duration, never a sufficient reception plan."""
    if upper_function(MAX_SECONDS) < required_kl:
        return {"necessary_seconds_lower_bound": MAX_SECONDS,
                "ten_year_target_excluded": True, "boundary_kind": "strictly_more_than_ten_years"}
    low, high = 0., MAX_SECONDS
    for _ in range(48):
        middle = (low+high)/2
        if upper_function(middle) < required_kl:
            low = middle
        else:
            high = middle
    return {"necessary_seconds_lower_bound": low,
            "ten_year_target_excluded": False,
            "boundary_kind": "KL_necessary_only_not_an_achievable_duration"}


def shaped_envelope_kl_upper(duration, width_2sigma, spectrum, cn0, papr):
    """Conservative upper bound for a single noisy sum of independent rays.

    Assume each deterministic delayed/unit-normalized envelope has energy<=T
    on the observation and squared amplitude<=PAPR, both rays share |r(u)|,
    powers C/2, and noise is unit white complex Gaussian. Triangle/Cauchy gives
    tr(K²)<=min(2*C²*PAPR*T*J, 2*C²*PAPR²*I). D<=min(trK,tr(K²)/2).
    This bound allows arbitrary stable Doppler centers and envelope phases.
    No numerical PAPR value here is asserted for the actual triad transmitter.
    """
    T, C, P = (positive(duration, "duration"), positive(cn0, "C/N0"),
               positive(papr, "PAPR"))
    if P < 1:
        raise ValueError("PAPR must be at least1")
    J, I, _ = correlation_integrals(T, width_2sigma, spectrum)
    return min(C*T, C*C*P*T*J, C*C*P*P*I)


def gaussian_bit_kl(cov0, cov1):
    """Proper-complex Gaussian D(CN(0,cov0)||CN(0,cov1)); tiny diagnostics."""
    a, b = np.asarray(cov0, np.complex128), np.asarray(cov1, np.complex128)
    if a.ndim != 2 or a.shape != b.shape or a.shape[0] != a.shape[1]:
        raise ValueError("covariances must be equally sized square matrices")
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("covariances must be finite")
    if not np.allclose(a, a.conj().T) or not np.allclose(b, b.conj().T):
        raise ValueError("covariances must be Hermitian")
    la, lb = cho_factor(a, lower=True), cho_factor(b, lower=True)
    logdet_a = 2*np.log(np.diag(la[0]).real).sum()
    logdet_b = 2*np.log(np.diag(lb[0]).real).sum()
    value = float(np.trace(cho_solve(lb, a)).real-len(a)+logdet_b-logdet_a)
    if value < -1e-10:
        raise ArithmeticError("negative Gaussian bit KL")
    return max(0., value)


def bit_identifiability_examples():
    n = 8
    k = np.arange(n)
    phase0 = np.ones(n, complex)
    phase1 = np.exp(1j*np.pi/2*(k%4))
    amplitude1 = np.sqrt(np.where(k%2, 1.6, .4)).astype(complex)
    records = []
    for rho in (0., .9):
        R = rho**np.abs(k[:, None]-k[None, :])
        for name, alternative in (("global-phase", phase0*np.exp(.7j)),
                                  ("varying-phase", phase1),
                                  ("varying-amplitude", amplitude1)):
            cov0 = np.eye(n)+phase0[:, None]*R*phase0.conj()[None, :]
            cov1 = np.eye(n)+alternative[:, None]*R*alternative.conj()[None, :]
            records.append({"independent_channel_cells": rho == 0, "correlation_rho": rho,
                "alternative": name, "KL_bit0_to_bit1_nats": gaussian_bit_kl(cov0, cov1),
                "KL_bit1_to_bit0_nats": gaussian_bit_kl(cov1, cov0)})
    return {"scope": "Eight-cell mathematical examples at unit per-cell energy, not measured triad patterns or -60dB-Hz trials.",
        "necessary_per_bit_KL": {str(p): binary_kl(p, 1-p) for p in (.9, .99)},
        "necessary_balanced_mean_KL_via_Pinsker": {str(p): 2*(2*p-1)**2 for p in (.9, .99)},
        "examples": records,
        "interpretation": [
            "A global phase change leaves a proper-complex zero-mean Gaussian channel covariance unchanged: those codewords are unidentifiable in this model.",
            "Independent per-cell circular gains erase phase-only pattern distinctions. Different amplitude patterns can remain identifiable.",
            "Correlated gains preserve off-diagonal phase relationships; widening stochastic Doppler can reduce this information even while the signal remains detectable.",
            "Per-bit KL necessity d(p||1-p) assumes each bit separately achieves p correct. The weaker Pinsker requirement applies to balanced mean correctness. Neither is sufficient.",
            "Actual keyed patterns, shaping, channel nuisance equivalence and amplitude distinctness require a separate sampled-waveform study."]}


def report():
    budgets = profiles()
    records = []
    for db in CN0_DB_VALUES:
        C = 10**(db/10)
        for width in GAUSSIAN_WIDTHS_2SIGMA_HZ:
            params = spectral_parameters(width)
            for spectrum in ("gaussian", "ou"):
                record = {"cn0_db_hz": db, "cn0_linear_hz": C, "spectrum": spectrum,
                          **params, "observation_seconds": MAX_SECONDS, "channel_models": []}
                for channels in (2, 1):
                    values = finite_kl_bounds(MAX_SECONDS, width, spectrum, C, channels)
                    def usable_upper(t):
                        v = finite_kl_bounds(t, width, spectrum, C, channels)
                        return v["finite_KL_exact_nats"] if v["finite_KL_exact_nats"] is not None else v["finite_KL_upper_nats"]
                    upper = usable_upper(MAX_SECONDS)
                    admissions = []
                    for profile in budgets:
                        result = necessary_duration(usable_upper, profile["required_binary_KL_nats"])
                        admissions.append({"profile": profile["id"], **result,
                            "probability_upper_from_KL_at_ten_years": upper_power_given_kl(upper, profile["false_probability_used_in_necessity"])})
                    rate = spectral_kl_rate(width, spectrum, C, channels)
                    record["channel_models"].append({"orthogonal_equal_power_channels": channels,
                        "meaning": ("two independent orthogonal ray observations" if channels == 2 else
                                    "fully overlapping identical ray covariance; also KL upper extreme over stable center offsets for this constant-envelope model"),
                        **values, "spectral_asymptotic": rate,
                        "low_power_asymptotic_rate": C*C*params[("Gaussian" if spectrum == "gaussian" else "OU")+"_squared_correlation_integral_seconds"]/(2*channels),
                        "T_times_spectral_rate_diagnostic_only": MAX_SECONDS*rate["KL_rate_nats_per_second"],
                        "necessary_conditions": admissions})
                record["arbitrary_envelope_declared_PAPR_bounds"] = [
                    {"assumed_PAPR_upper": P, "KL_upper_at_ten_years": shaped_envelope_kl_upper(MAX_SECONDS, width, spectrum, C, P),
                     "excluded_profiles": [p["id"] for p in budgets if shaped_envelope_kl_upper(MAX_SECONDS, width, spectrum, C, P)<p["required_binary_KL_nats"]]}
                    for P in PAPR_EXAMPLES]
                records.append(record)
    known = []
    for Cdb in CN0_DB_VALUES:
        C = 10**(Cdb/10)
        for profile in budgets:
            known.append({"cn0_db_hz": Cdb, "profile": profile["id"],
                          "known_fixed_waveform_required_useful_seconds": profile["known_fixed_waveform_required_E_N0"]/C})
    return {"schema": "triad-stochastic-spread-information-frontier-v1",
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "numeric_environment": {"mpmath_version": mp.__version__, "scipy_version": scipy.__version__,
                                "numpy_version": np.__version__, "moment_decimal_digits": MP_DIGITS},
        "maximum_observation_seconds": MAX_SECONDS, "profiles": budgets,
        "known_fixed_waveform_references": known, "frontier": records,
        "bit_identifiability": bit_identifiability_examples(),
        "definitions_and_derivations": [
            "C=C/N0 in inverse seconds is total mean received power across both rays divided by noise density. It is not in-band SNR or a data rate.",
            "Gaussian unit-power PSD p(f)=exp(-f²/(2sigma²))/(sigma*sqrt(2pi)), correlation r(u)=exp(-2pi²sigma²u²). The stated spread is2sigma in Hz, following the cited ITU convention.",
            "The comparison Lorentzian PSD is p(f)=gamma/(pi*(f²+gamma²)), with gamma equal to Gaussian HWHM=sigma*sqrt(2ln2). Its correlation is exp(-2pi*gamma*abs(u)). Lorentzian RMS Doppler is infinite; matched HWHM is not matched RMS or matched correlation time.",
            "For m independent orthogonal channels, K_j=(C/m)R and D=sum_j[trK_j-logdet(I+K_j)]. Its long-time rate is m*integral[(C/m)*p(f)-log(1+(C/m)*p(f))]df.",
            "At low spectral SNR, rate approaches C²/(2m)*integral|r(u)|²du. Thus duration can scale as C^-2 times stochastic width for fixed target information. This asymptotic does not identify a correct-reception probability or apply when spectral SNR is large.",
            "Finite covariance bounds use I=integral_0^T(T-u)|r(u)|²du, trK²=2C²I/m, eigenvalue boundL=(C/m)*min(T,max p). Then trK²/(2(1+L))<=D<=min(trK²/2,CT-m*log(1+CT/m)).",
            "A stable deterministic Doppler center conjugates an individual ray covariance by a unitary modulation and leaves its KL unchanged. It is not a stochastic linewidth or a coherence reset.",
            "For the constant-envelope summed observation with two equally powered independent rays and identical stochastic shapes, determinant subadditivity gives D_separated<=D_sum; operator convexity of X-log(I+X) gives D_sum<=D_fully_overlapping. Center separation affects overlap but cannot exceed the stated fully overlapping upper extreme.",
            "Data processing maps any detector to its admission event: D(P1||P0)>=d(P1(A)||P0(A))>=d(p||alpha) when P1(A)>=p>alpha>=P0(A). Violation excludes the target for every detector of that observation model, including randomized tests.",
            "For correctness, the event can be 'admit the true bit'. Presence is necessary but not sufficient: wrong-bit rejection and nuisance identifiability remain separate.",
            "A Gaussian covariance model with fixed-center stochastic taps is different from deterministic equal-power rays. Taking linewidth to zero leaves unknown Gaussian gains and possible deep fades, not a fixed known channel."],
        "limits": [
            "All bounds are conditional on the explicitly stated Gaussian channel, white known noise, power, deterministic-envelope and observation assumptions. They do not establish universal physical floors or calibrate sub9kHz propagation.",
            "No ordinary or isolated triad receiver was run. Delay, shaping, guards, clock search, oscillator phase noise, outages and interference are not simulated here.",
            "The separated-ray model is not automatically a more informative oracle than a summed noisy observation. The overlapping and declared-PAPR bounds are provided to avoid that inference.",
            "PAPR1,3,10 are hypothetical upper bounds, not measurements of any transmitter. The envelope-energy assumption<=T and peak-power assumption must both be justified before transferring the envelope bound.",
            "Global alpha results use one completed decision and the entire horizon allowance. Family allocation results are conditional on the stated per-component allocation; search multiplicity cannot be divided into alpha and then called a universal necessity.",
            "OU finite-T KL is closed form; Gaussian finite-T KL is bounded, not numerically diagonalized. Spectral integrals are asymptotic rates only and are never used as finite-T exclusion bounds.",
            "Exact-model expressions are evaluated with70-digit intermediate arithmetic where indicated, then rounded to FP64; root searches, probability inversion, quadrature and final inequalities use FP64. No interval-arithmetic certificate is claimed.",
            "A necessary duration is a lower bound, not a duration planner. Not excluded means only that this information test cannot rule the configuration out; it does not demonstrate99% or90% reception.",
            "The analyses assume fixed hypotheses and decisions; arbitrary adaptive stopping requires separate sequential accounting."],
        "sources": [
            {"title": "ITU-R F.1487, Testing of HF modems with bandwidths of up to about12kHz using ionospheric channel simulators",
             "url": "https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf",
             "use": "Annex1 separately parameterizes Gaussian scatter-spectrum centers and widths2sigma. Its HF scope is not treated as field calibration for other bands."},
            {"title": "Polyanskiy and Wu, Information Theory: From Coding to Learning, author draft",
             "url": "https://people.lids.mit.edu/yp/homepage/data/itbook-2022.pdf",
             "use": "Relative entropy data processing and binary hypothesis testing; the admission-event specialization is derived explicitly above."},
            {"title": "Ferrante, Masiero and Pavon, Time and spectral domain relative entropy",
             "url": "https://arxiv.org/abs/1103.5602",
             "use": "Time/spectral relative entropy-rate relationship for stationary Gaussian processes; our continuous proper-complex expressions specify their own normalization and finite-time limitations."}]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(report(), indent=2, sort_keys=True, allow_nan=False)+"\n")


if __name__ == "__main__":
    main()

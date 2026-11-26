#!/usr/bin/env python3
"""Ideal OU presence benchmarks, with explicit quadrature and model limits.

Reproduce: OPENBLAS_NUM_THREADS=2 python ou_bound_diagnostic.py --output RESULT.json
Target search reproduces original seed 926183 / 768 modes / 50,000 draws.
The 38-day summary is refined separately to 3072 modes with seed 926184.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from scipy.optimize import brentq
from scipy.stats import beta

from benchmark_math import (CN0, CN0_DB_HZ, GATE, FREQUENCY_CAVEAT,
                            PRESENCE_CAVEAT, grid_count, ou_eigenvalues,
                            ou_logdet_one_ray, ou_variance_h1, source_hashes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    modes, trials, seed = 768, 50000, 926183
    # Each independent Gamma(2,1) draw sums one complex mode from each ray.
    # Common random numbers are reused across durations; this is a fitted target.
    draws = np.random.default_rng(seed).gamma(2., 1., size=(modes, trials))

    def samples(duration, coherence, cn0=CN0):
        eigenvalues = ou_eigenvalues(duration, coherence, modes, cn0)
        return (eigenvalues @ draws - 2 * ou_logdet_one_ray(duration, coherence, cn0),
                eigenvalues)

    records = []
    for coherence in (1e4, 1e5, 1e6):
        for presence_target in (.9, .99):
            for hypothetical_frequency in (False, True):
                cn0 = CN0 * 10 ** (-.22 / 10) if hypothetical_frequency else CN0

                def threshold(duration):
                    return GATE + (math.log(grid_count(duration)) if hypothetical_frequency else 0)

                duration = brentq(lambda t: np.quantile(samples(t, coherence, cn0)[0],
                                                       1 - presence_target) - threshold(t),
                                  1000, 38 * 86400, xtol=1)
                scores, eigenvalues = samples(duration, coherence, cn0)
                successes = int((scores >= threshold(duration)).sum())
                exact_ld = 2 * ou_logdet_one_ray(duration, coherence, cn0)
                variance = ou_variance_h1(duration, coherence, cn0)
                records.append({
                    "tc_seconds": coherence,
                    "target_presence_probability": presence_target,
                    "ideal_seconds": duration, "ideal_days": duration / 86400,
                    "threshold": threshold(duration),
                    "frequency_grid_bins": grid_count(duration) if hypothetical_frequency else 1,
                    "frequency_penalty_model": FREQUENCY_CAVEAT if hypothetical_frequency else "known common CFO and phase",
                    "hypothetical_energy_derating_db": .22 if hypothetical_frequency else 0,
                    "sample_count": trials, "presence_admissions": successes,
                    "descriptive_pointwise_95_interval": [
                        float(beta.ppf(.025, successes, trials - successes + 1)),
                        float(beta.ppf(.975, successes + 1, trials - successes))],
                    "exact_logdet_two_rays": exact_ld,
                    "discrete_minus_exact_logdet": float(2 * np.log1p(eigenvalues).sum() - exact_ld),
                    "KL_expected": cn0 * duration - exact_ld,
                    "variance_H1_exact": variance,
                    "relative_discrete_variance_error": float(2 * (eigenvalues ** 2).sum() / variance - 1),
                    "scope": "Fitted H1 presence quantile of an ideal covariance oracle; reused draws make these intervals descriptive, not independent validation",
                    "probability_scope": PRESENCE_CAVEAT})

    coarse = []
    duration = 38 * 86400
    for coherence in (1e4, 1e5, 1e6):
        scores, eigenvalues = samples(duration, coherence)
        variance = ou_variance_h1(duration, coherence)
        coarse.append({
            "tc_seconds": coherence, "eigenmode_count": modes, "sample_count": trials,
            "seed": seed, "days": 38,
            "presence_admission_fraction": float(np.mean(scores >= GATE)),
            "loglr_percentiles": np.quantile(scores, [.001, .01, .1, .5]).tolist(),
            "relative_discrete_variance_error": float(2 * (eigenvalues ** 2).sum() / variance - 1),
            "status": "superseded by refined numerical summary; retained to expose the original discretization error"})
    del draws

    # Refinement is a separate numerical calculation, not another physical trial.
    summary_modes, summary_seed = 3072, 926184
    coherences = (1e4, 1e5, 1e6)
    eigenvalues = np.stack([ou_eigenvalues(duration, tc, summary_modes) for tc in coherences])
    logdets = np.array([2 * ou_logdet_one_ray(duration, tc) for tc in coherences])
    summary_scores = np.empty((len(coherences), trials))
    rng = np.random.default_rng(summary_seed)
    for start in range(0, trials, 2048):
        stop = min(trials, start + 2048)
        summary_scores[:, start:stop] = (
            eigenvalues @ rng.gamma(2., 1., size=(summary_modes, stop - start))
            - logdets[:, None])
    summary = []
    for i, coherence in enumerate(coherences):
        scores = summary_scores[i]
        admissions = int((scores >= GATE).sum())
        summary.append({
            "tc_seconds": coherence, "days": 38,
            "eigenmode_count": summary_modes, "sample_count": trials, "seed": summary_seed,
            "mean_loglr_exact": CN0 * duration - logdets[i],
            "presence_admissions_above_fixed_gate": admissions,
            "presence_admission_fraction": admissions / trials,
            "presence_one_sided_95_lower": float(beta.ppf(.05, admissions, trials - admissions + 1)),
            "presence_fraction_above_gate_plus_log_grid_count": float(np.mean(scores >= GATE + math.log(grid_count(duration)))),
            "log_grid_count_scope": "exact mixture inequality only if a matched component is included; no continuous-CFO mismatch guarantee",
            "loglr_percentiles": np.quantile(scores, [.001, .01, .1, .5]).tolist(),
            "discrete_minus_exact_logdet": float(2 * np.log1p(eigenvalues[i]).sum() - logdets[i]),
            "relative_discrete_variance_error": float(2 * (eigenvalues[i] ** 2).sum() / ou_variance_h1(duration, coherence) - 1),
            "KL_rate_per_second_asymptotic": CN0 - 2 * (math.sqrt(1 + CN0 * coherence) - 1) / coherence,
            "low_snr_scaling": "KL rate approaches C^2*tc/4 when C*tc << 1; present C*tc values are 2.51,25.12,251.19, outside that limit",
            "probability_scope": PRESENCE_CAVEAT})
    out = {
        "scope": "Ideal mathematical presence benchmark; no sampled-waveform or receiver-performance claim",
        "probability_scope": PRESENCE_CAVEAT,
        "source_sha256": source_hashes(Path(__file__).parent),
        "cn0_db_hz": CN0_DB_HZ, "gate": GATE,
        "seed": seed, "eigenmode_count": modes, "sample_count": trials,
        "targets": records, "original_coarse_38day_predictions": coarse,
        "refined_38day_predictions": summary,
        "assumptions": ["two exactly orthogonal equal-power ray observation channels",
                        "known timing, epoch, delay/Doppler model and common phase",
                        "proper Gaussian stationary OU coefficients; known stationary white noise",
                        "continuous OU Fredholm determinant with point-sampled covariance eigenvalues",
                        FREQUENCY_CAVEAT],
        "exact_formula": "E logLR = C*T - 2*logdet; logdet=-T/tc+log(cosh(z*T/tc)+(1+C*tc/2)/z*sinh(z*T/tc)), z=sqrt(1+C*tc)",
        "variance_formula": "Var_H1 logLR=C^2/2*(T*tc-tc^2/2*(1-exp(-2*T/tc)))"}
    args.output.write_text(json.dumps(out, indent=2) + "\n")


if __name__ == "__main__":
    main()

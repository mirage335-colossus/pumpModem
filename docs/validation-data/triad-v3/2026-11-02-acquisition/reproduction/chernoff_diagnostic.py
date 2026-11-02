#!/usr/bin/env python3
"""Post-hoc group59 diagnostic, NOT exhaustive acquisition or an e-value.

Keep the existing projection, strict noise UCB, Gaussian factors and FP32
correlation score. Bound the marginal null tail by optimizing a valid Chernoff
inequality; charge all109 groups, both bits and the stated operational family.
Conditional on independent residual calibration covering stationary Gaussian
noise, actual variance below the UCB only decreases the positive quadratic.
No assumption that the physical SIGNAL follows the Gaussian alternative.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import numpy as np
from scipy.optimize import brentq
from scipy.stats import beta, gamma
import torch

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT/"tools"))
import triad_group_acquire as group
import triad_gaussian_window as gaussian


def log1pmx(x):
    x = np.asarray(x, dtype=np.float64)
    small = abs(x) < .001
    out = np.log1p(x)-x
    z = x[small]
    out[small] = z*z*(-.5+z*(1/3+z*(-.25+z*(.2+z*(-1/6+z/7)))))
    return out


def tail_bound(x, observed):
    x = np.asarray(x, dtype=np.float64)
    if not np.isfinite(x).all() or np.any(x < 0) or not math.isfinite(observed):
        raise ValueError("finite nonnegative covariance ratios and finite score required")
    x = x[x > 0]
    if not len(x):
        return {"log_bound": 0. if observed <= 0 else -math.inf, "t": 0., "log_bound_t1": -observed}
    def cumulant(t):
        a = 1-t
        if np.any(1+a*x <= 0):
            raise ValueError("Chernoff parameter outside moment-generating domain")
        return float(np.sum(a*log1pmx(x)-log1pmx(a*x)))
    def derivative(t):
        a = 1-t
        return float(np.sum(-log1pmx(x)-a*x*x/(1+a*x)))
    hi = np.nextafter(1+1/float(x.max()), 0.)
    while np.any(1+(1-hi)*x <= 0):
        hi = np.nextafter(hi, 0.)
    candidates = [0., 1.]
    if derivative(0.) < observed:
        candidates.append(brentq(lambda t: derivative(t)-observed, 0., hi,
                                 xtol=1e-12, rtol=1e-12) if derivative(hi) > observed else hi)
    values = [cumulant(t)-t*observed for t in candidates]
    best = int(np.argmin(values))
    return {"log_bound": values[best], "t": candidates[best], "log_bound_t1": -observed,
            "t_domain_upper_exclusive": 1+1/float(x.max())}


def validate():
    comparisons = []
    for n in (1, 4, 16):
        for x in (.01, .5, 10.):
            for a in (.5*n, n, 2*n, 4*n):
                score = x/(1+x)*a-n*np.log1p(x)
                bound = tail_bound(np.full(n, x), score)["log_bound"]
                exact = float(gamma.logsf(a, n))
                assert bound >= exact-1e-11, (n, x, a, bound, exact)
                comparisons.append(bound-exact)
    assert tail_bound(np.zeros(3), 0.)["log_bound"] == 0
    assert tail_bound(np.zeros(3), 1.)["log_bound"] == -math.inf
    x = np.array([.03, .2, .7, 1.4]); w = x/(1+x); d = np.log1p(x).sum()
    threshold = w.sum()-d+2*np.linalg.norm(w)
    draws = np.random.default_rng(2026110230).standard_exponential((20000, 4)) @ w-d
    exceed = int(np.count_nonzero(draws >= threshold))
    upper = float(beta.ppf(.99, exceed+1, len(draws)-exceed))
    bound = math.exp(tail_bound(x, threshold)["log_bound"])
    assert upper <= bound
    return {"exact_exponential_gamma_cases": len(comparisons), "minimum_log_slack": min(comparisons),
            "mc_trials": len(draws), "mc_exceedances": exceed, "mc_99_upper": upper, "chernoff_bound": bound}


def array_hash(array):
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args(); started = time.perf_counter(); torch.set_num_threads(1)
    checks = validate()
    base = ROOT/"local-evidence/triad-acquisition-improvement"
    name = "paired-high30-each-65-cn8-p120002-b16-signal"
    workspace = group.GroupedWorkspace(base/"campaign/workspaces"/name)
    assert workspace.groups == 109 and workspace.block_samples == 16 and workspace.sample_rate == 2048
    config = group.AcquisitionConfig(30., window_seconds=.25, precision="fp32")
    strict, relaxed = config.budgets()
    delta = math.nextafter(strict.delta_per_segment/workspace.groups, 0.)
    intervals, valid, energy = workspace.calendar(1)
    G, r = workspace.read_group_tile(59, 60, 1)
    corr, grams, upper, residual, dof, ranks = group.prepare_group_tile(
        G, r, energy, valid, workspace.mapping[59:60], reference=1., delta=delta)
    centers = (intervals[:, 0].astype(float)+intervals[:, 1].astype(float))/(2*workspace.sample_rate)
    plan = gaussian.PreparedWindowedGaussian(grams[:, 0], centers, doppler_width_hz=30.,
        window_blocks=32, stationary_power=(.5, .5))
    scores = plan.score(corr[:, 0], variance_scale=upper[0], precision="fp32").numpy()
    baseline_path = base/"prior-truncation/results/weak-physical-window-v2"/(name+".paired-scores.npz")
    with np.load(baseline_path) as baseline:
        difference = scores-baseline["eps_0_group_scores"][59]
        assert np.max(abs(difference)) < 1e-5
        assert int(np.argmax(baseline["eps_0_bit_scores"])) == 0
    x = np.concatenate([window.eigenvalues for window in plan._windows], axis=1)/upper[0]
    w = x/(1+x); d = np.log1p(x).sum(axis=1)
    tails = [tail_bound(x[bit], float(scores[bit])) for bit in (0, 1)]
    thresholds = {label: {"family_size": workspace.groups*2*budget.attempts*8*16,
        "score_alpha": budget.score_probability,
        "log_tail_threshold": math.log(budget.score_probability)-math.log(workspace.groups*2*budget.attempts*8*16)}
        for label, budget in (("strict", strict), ("relaxed", relaxed))}
    for item in thresholds.values():
        item["fixed_bit0_group59_passes"] = tails[0]["log_bound"] <= item["log_tail_threshold"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    weights_path = args.output.with_suffix(".weights.npz")
    np.savez(weights_path, x=x, weights=w, determinant=d, scores=scores, variance_upper=upper)
    workspace.verify_unchanged()
    result = {"scope": __doc__, "status": "complete", "checks": checks, "group": 59, "winner_bit_fixed": 0,
        "config": {"window_seconds": .25, "window_blocks": 32, "reblock": 1, "doppler_width_hz": 30.,
            "tap_powers": [.5, .5], "correlation_precision": "fp32", "factor_tail_precision": "fp64", "cpu_threads": 1},
        "scores": scores.tolist(), "baseline_difference": difference.tolist(), "tails": tails, "thresholds": thresholds,
        "null_moments": {"modes_per_bit": x.shape[1], "mean": (w.sum(axis=1)-d).tolist(), "variance": (w*w).sum(axis=1).tolist()},
        "ucb": {"strict_delta_per_group": delta, "variance_upper": upper.tolist(), "residual": residual.tolist(), "dof": dof.tolist()},
        "null_law": "LL=sum_j [x_j/(1+x_j)] Exp_j(1)-sum_j log1p(x_j); independent disjoint-window null projections",
        "log_cgf": "K(t)=sum[(1-t)log1p(x)-log1p((1-t)x)]",
        "hashes": {"driver": group.sha256(__file__), "group_helper": group.sha256(group.__file__),
            "noise_helper": group.sha256(ROOT/"tools/triad_noise_likelihood.py"),
            "gaussian_helper": group.sha256(gaussian.__file__), "manifest": group.sha256(workspace.directory/"manifest.json"),
            "baseline": group.sha256(baseline_path), "weights": group.sha256(weights_path),
            "selected_gram": array_hash(G), "selected_correlation": array_hash(r), "calendar": array_hash(intervals),
            "valid": array_hash(valid), "energy": array_hash(energy)}, "elapsed_seconds": time.perf_counter()-started}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    print(json.dumps({k: result[k] for k in ("scores", "tails", "thresholds", "elapsed_seconds")}))


if __name__ == "__main__":
    main()

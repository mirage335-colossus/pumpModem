#!/usr/bin/env python3
"""Compare finite CFO priors and continuous quadrature on retained observations.

Reproduce: python day_frequency_diagnostic.py WORKSPACE --output RESULT.json
No hidden signal means or energies are read. The manifest simulation noise
variance supplies a declared known-variance diagnostic, not noise estimation.
Clipped-cell mixtures are normalized finite mixtures, not exact continuous LRs.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from scipy.integrate import quad
from scipy.linalg import cholesky, solve_triangular
from scipy.special import logsumexp

from benchmark_math import PRESENCE_CAVEAT, source_hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frequency-bound", type=float, default=1e-6)
    args = parser.parse_args()
    if not math.isfinite(args.frequency_bound) or args.frequency_bound <= 0:
        raise ValueError("frequency bound must be positive and finite")
    p = args.input
    a = json.loads((p / "manifest.json").read_text())
    blocks, dimension, variance = a["block_count"], a["dimension"], a["noise_variance"]
    g = np.fromfile(p / "gram.c128", dtype="<c16").reshape(blocks, dimension, dimension) / variance
    r = np.fromfile(p / "observation.c128", dtype="<c16").reshape(blocks, dimension) / variance
    t = np.fromfile(p / "intervals.u64", dtype="<u8").reshape(blocks, 2).mean(1) / a["sample_rate"]
    dt = a["block_samples"] / a["sample_rate"]
    bound = args.frequency_bound
    results = []
    for bit in (0, 1):
        matches = [a["column_mapping"][i] for i, candidate in enumerate(a["candidates"])
                   if candidate["bit"] == bit and candidate["tap_count"] == 2]
        if len(matches) != 1:
            raise ValueError("this diagnostic requires one two-ray candidate per bit")
        idx = matches[0]
        gram, received = g[:, idx, :][:, :, idx].sum(0), r[:, idx]
        lower = cholesky(gram + 2 * np.eye(2), lower=True)
        whitened = solve_triangular(lower, received.T, lower=True).T
        logdet = 2 * np.log(lower.diagonal().real).sum() - 2 * math.log(2)

        def score(frequencies):
            values = np.exp(-2j * np.pi * np.asarray(frequencies)[..., None] * t) @ whitened
            return (abs(values) ** 2).sum(-1) - logdet

        finite = []
        for edge, count in ((bound, 3), (bound, 101), (bound, 1001),
                            (1 / (512 * dt), 3), (1 / (512 * dt), 1001)):
            values = score(np.linspace(-edge, edge, count))
            finite.append({"bound_hz": edge, "count": count,
                           "uniform_discrete_score": float(logsumexp(values) - math.log(count)),
                           "score_min_max": [float(values.min()), float(values.max())]})
        scale = float(score(np.linspace(-bound, bound, 1001)).max())
        integral, error = quad(lambda u: math.exp(float(score(u * bound)) - scale),
                               -1, 1, epsabs=1e-13, epsrel=1e-13)
        continuous = math.log(integral / 2) + scale
        weighted = []
        for nfft in (512, 1024, 2048, 4096, 8192, 16384):
            step = 1 / (nfft * dt)
            half = math.ceil(bound / step)
            f = np.arange(-half, half + 1) * step
            weights = np.maximum(0, np.minimum(bound, f + step / 2)
                                 - np.maximum(-bound, f - step / 2)) / (2 * bound)
            keep = weights > 0
            result = float(logsumexp(score(f[keep]) + np.log(weights[keep])))
            weighted.append({"nfft": nfft, "spacing_hz": step,
                             "active_bins": int(keep.sum()), "weight_sum": float(weights.sum()),
                             "score": result, "difference_from_continuous_quadrature": result - continuous})
        results.append({"candidate_bit": bit, "uniform_finite_priors": finite,
                        "continuous_uniform_score": continuous,
                        "continuous_integral_scaled_absolute_error_estimate": error,
                        "clipped_cell_finite_mixtures": weighted})
    out = {
        "scope": "Known-variance static Gaussian-channel retained-observation score diagnostic; no receiver changes",
        "probability_scope": PRESENCE_CAVEAT,
        "source_sha256": source_hashes(Path(__file__).parent),
        "input_sha256": {name: hashlib.sha256((p / name).read_bytes()).hexdigest()
                         for name in ("manifest.json", "gram.c128", "observation.c128", "intervals.u64")},
        "declared_frequency_bound_hz": bound,
        "noise_variance_source": "manifest simulation variance, treated as known for this diagnostic",
        "quadrature_scope": "Finite normalized positive weights preserve H0 mixture normalization. Clipped cells approximate a continuous uniform prior; their nodes can lie outside its support. Integration error estimates are numerical, not certified physical-model bounds.",
        "results": results}
    args.output.write_text(json.dumps(out, indent=2) + "\n")


if __name__ == "__main__":
    main()

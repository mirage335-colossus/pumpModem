#!/usr/bin/env python3
"""Independent H1 presence validation of previously fixed ideal OU durations.

Reproduce: OPENBLAS_NUM_THREADS=2 python ou_bound_validation.py TARGETS.json --output RESULT.json
Preserves original independent seed 7719284, 1536 modes and 100,000 draws.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.stats import beta

from benchmark_math import (CN0, FREQUENCY_CAVEAT, PRESENCE_CAVEAT,
                            ou_eigenvalues, source_hashes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    targets = json.loads(args.input.read_text())["targets"]
    modes, trials, seed = 1536, 100000, 7719284
    eigenvalues, logdets = [], []
    for row in targets:
        cn0 = CN0 * 10 ** (-row["hypothetical_energy_derating_db"] / 10)
        eigenvalues.append(ou_eigenvalues(row["ideal_seconds"], row["tc_seconds"], modes, cn0))
        logdets.append(row["exact_logdet_two_rays"])
    eigenvalues, logdets = np.stack(eigenvalues), np.asarray(logdets)
    rng = np.random.default_rng(seed)
    scores = np.empty((len(targets), trials))
    for start in range(0, trials, 2048):
        stop = min(trials, start + 2048)
        scores[:, start:stop] = (eigenvalues @ rng.gamma(2., 1., size=(modes, stop - start))
                                - logdets[:, None])
    rows = []
    for i, target in enumerate(targets):
        successes = int((scores[i] >= target["threshold"]).sum())
        rows.append({
            "tc_seconds": target["tc_seconds"],
            "target_presence_probability": target["target_presence_probability"],
            "days": target["ideal_days"], "frequency_bins": target["frequency_grid_bins"],
            "hypothetical_energy_derating_db": target["hypothetical_energy_derating_db"],
            "independent_validation_trials": trials, "presence_admissions": successes,
            "presence_probability_estimate": successes / trials,
            "pointwise_95_interval": [float(beta.ppf(.025, successes, trials - successes + 1)),
                                      float(beta.ppf(.975, successes + 1, trials - successes))],
            "discrete_minus_exact_logdet": float(2 * np.log1p(eigenvalues[i]).sum() - logdets[i]),
            "q01": float(np.quantile(scores[i], .01))})
    out = {
        "scope": "Independent ideal covariance-oracle H1 presence validation; not physical waveform or receiver validation",
        "probability_scope": PRESENCE_CAVEAT,
        "frequency_scenario_scope": FREQUENCY_CAVEAT,
        "source_sha256": source_hashes(Path(__file__).parent),
        "targets_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
        "seed": seed, "modes": modes, "results": rows}
    args.output.write_text(json.dumps(out, indent=2) + "\n")


if __name__ == "__main__":
    main()

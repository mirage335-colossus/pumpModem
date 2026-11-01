#!/usr/bin/env python3
"""Recompute scoped benchmark comparisons from recorded observations.

Requires SciPy. Inputs: an evidence directory or evidence.tar.gz. No extraction.
Single-point admission probabilities are not full-bank correct probabilities.
"""
import argparse
import json
import math
from pathlib import Path
import tarfile
from scipy.optimize import brentq
from scipy.stats import beta, ncx2, norm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    archive = tarfile.open(args.evidence) if args.evidence.is_file() else None

    def rows(name):
        if archive:
            stream = archive.extractfile(name)
            if stream is None:
                raise ValueError(f"Missing {name}")
            text = stream.read().decode()
        else:
            text = (args.evidence / name).read_text()
        return [json.loads(line) for line in text.splitlines() if line.strip()]

    oracle = []
    for row in rows("triad-paired-flat04.jsonl"):
        reference = row["known_channel_benchmark"]
        result = reference["empirical_binary_decisions"]["full_band"]
        k, n = result["raw_bit_errors"], result["trials"]
        probability = reference["known_channel_binary_error"]
        if k <= 20 or probability <= .01:
            continue  # Avoid rare/zero-error inversion; state the selected diagnostic points.
        low, high = beta.ppf(.025, k, n-k+1), beta.ppf(.975, k+1, n-k)
        if not 0 < low <= high < .5:
            raise ValueError("Q-squared energy inversion requires an error interval below 0.5")
        loss = lambda p: 10*math.log10(norm.isf(probability)**2/norm.isf(p)**2)
        oracle.append({"cn0_db_hz": row["cn0_db_hz"], "errors": k, "trials": n,
                       "known_channel_binary_error": probability,
                       "equivalent_energy_loss_db": loss(k/n),
                       "pointwise_95_loss_interval_db": [loss(low), loss(high)]})

    acquired = {r["cn0_db_hz"]: r for r in rows("triad-fft-full.jsonl")}
    diagnostics = rows("triad-fft-benchmark.jsonl")
    comparisons = []
    for row in diagnostics:
        b = row["noiseless_projection_benchmark"]
        full = acquired[row["cn0_db_hz"]]
        for field in ("source_manifest", "template_params", "threshold", "total_horizon_hypotheses",
                      "sample_rate", "capture_samples", "channel"):
            if full[field] != row[field]:
                raise ValueError(f"Benchmark and acquisition differ in {field}")
        measured = full["positive"]
        k, n = measured["correct_bit_and_epoch_accepted"], measured["trials"]
        comparisons.append({"cn0_db_hz": row["cn0_db_hz"],
            "fixed_mean_selected_point_pass_probability": float(ncx2.sf(
                b["chi_square_score_threshold"], b["chi_square_degrees_of_freedom"], b["chi_square_noncentrality"])),
            "whole_bank_correct_bit_epoch_count": k, "trials": n,
            "whole_bank_correct_one_sided_95_lower": 0 if k == 0 else float(beta.ppf(.05, k, n-k+1))})
    first = diagnostics[0]; b = first["noiseless_projection_benchmark"]
    targets = []
    for target in (.9, .99):
        required = brentq(lambda energy: ncx2.sf(2*first["threshold"], 2, 2*energy)-target, 0, 1000)
        cn0 = first["cn0_db_hz"] + 10*math.log10(required/b["selected_energy_noncentrality"])
        known_mean = .5*(norm.isf(first["false_admission_probability_per_horizon"]/
                        first["total_horizon_hypotheses"])+norm.isf(1-target))**2
        targets.append({"pass_probability": target, "rank_one_required_projected_energy": required,
                        "fixed_point_required_cn0_db_hz": cn0,
                        "known_mean_required_energy_same_family_pass_only": float(known_mean)})
    result = {"oracle_binary_scope": "known sampled means; no admission gate; pointwise binomial uncertainty",
              "oracle_binary_checks": oracle,
              "fft_scope": "one exported channel and full fixed grid; analytic white-noise daily gate; fixed-point pass and whole-bank correct probabilities are different quantities",
              "fft_selected_projection_fraction": b["selected_projection_fraction"],
              "fft_projection_mismatch_loss_db": -10*math.log10(b["selected_projection_fraction"]),
              "fft_comparisons": comparisons, "fft_single_point_targets": targets}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    if archive:
        archive.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Evaluate hidden signal information, never receiver performance.

Reproduce: python day_oracle_diagnostic.py WORKSPACE --output RESULT.json
WORKSPACE must contain the original day-mismatch triad-block-stats-v1 export.
mean.c128, signal_energy.f64 and manifest noise variance are ORACLE inputs.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from scipy import linalg, optimize, stats

from benchmark_math import CN0, GATE, PRESENCE_CAVEAT, source_hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    p = args.input
    manifest = json.loads((p / "manifest.json").read_text())
    b, d = manifest["block_count"], manifest["dimension"]
    variance = manifest["noise_variance"]
    gram = np.fromfile(p / "gram.c128", dtype="<c16").reshape(b, d, d) / variance
    mean = np.fromfile(p / "mean.c128", dtype="<c16").reshape(b, d) / variance
    received = np.fromfile(p / "observation.c128", dtype="<c16").reshape(b, d) / variance
    energy = np.fromfile(p / "signal_energy.f64", dtype="<f8") / variance
    bit = manifest["signal_bit"]

    def mapping(wanted_bit, taps):
        matches = [manifest["column_mapping"][i]
                   for i, c in enumerate(manifest["candidates"])
                   if c["bit"] == wanted_bit and c["tap_count"] == taps]
        if len(matches) != 1:
            raise ValueError("this diagnostic requires one candidate per bit/tap-count")
        return matches[0]

    cases = [("full_bank", list(range(d))),
             ("correct_bit_mismatched_two_ray", mapping(bit, 2)),
             ("correct_bit_flat", mapping(bit, 1)),
             ("wrong_bit_mismatched_two_ray", mapping(1 - bit, 2))]
    candidate = next(c for c in manifest["candidates"]
                     if c["bit"] == bit and c["tap_count"] == 2)
    out = {
        "scope": "SIMULATION ORACLE: hidden means/energies evaluate retained information only",
        "probability_scope": PRESENCE_CAVEAT,
        "noise_variance_source": "manifest simulation oracle, not receiver noise estimation",
        "source_sha256": source_hashes(Path(__file__).parent),
        "input_sha256": {name: hashlib.sha256((p / name).read_bytes()).hexdigest()
                         for name in ("manifest.json", "gram.c128", "mean.c128",
                                      "observation.c128", "signal_energy.f64")},
        "actual_energy_snr": float(energy.sum()),
        "actual_energy_snr_db": float(10 * np.log10(energy.sum())),
        "sampled_seconds": manifest["sample_count"] / manifest["sample_rate"],
        "requested_seconds": manifest["requested_active_seconds"],
        "actual_elapsed_seconds": manifest["actual_elapsed_seconds"],
        "nominal_payload_energy_seconds_lower": manifest["nominal_payload_energy_seconds_lower"],
        "physical_spread": manifest["channel_model"],
        "candidate_taps": candidate["taps"],
        "mismatch_note": "correct_bit names the bit only; exported 1.5ms/.3Hz ray mismatches physical 5us/.001Hz ray",
        "projections": {}}
    for name, idx in cases:
        g = gram[:, idx, :][:, :, idx]
        m, r = mean[:, idx], received[:, idx]
        retained = oracle_score = 0.0
        for gi, mi, ri in zip(g, m, r):
            h = linalg.pinvh(gi, rtol=1e-12) @ mi
            en = np.vdot(mi, h).real
            retained += en
            oracle_score += 2 * np.vdot(h, ri).real - en
        gs, ms, rs = g.sum(0), m.sum(0), r.sum(0)
        h = linalg.pinvh(gs, rtol=1e-12) @ ms
        static_energy = np.vdot(ms, h).real
        static_score = 2 * np.vdot(h, rs).real - static_energy
        powers = np.full(len(idx), 1 / len(idx))
        lower = linalg.cholesky(gs + np.diag(1 / powers), lower=True)
        wr = linalg.solve_triangular(lower, rs, lower=True)
        wm = linalg.solve_triangular(lower, ms, lower=True)
        logdet = 2 * np.log(lower.diagonal().real).sum() + np.log(powers).sum()
        pn = linalg.solve_triangular(lower, gs, lower=True)
        pn = linalg.solve_triangular(lower, pn.conj().T, lower=True).conj().T
        out["projections"][name] = {
            "blockwise_oracle_energy_snr": float(retained),
            "blockwise_energy_fraction": float(retained / energy.sum()),
            "blockwise_oracle_loglr_actual": float(oracle_score),
            "static_oracle_energy_snr": float(static_energy),
            "static_energy_fraction": float(static_energy / energy.sum()),
            "static_oracle_loglr_actual": float(static_score),
            "gram_eigenvalues": np.linalg.eigvalsh(gs).tolist(),
            "fitted_static_tap_magnitude": abs(h).tolist(),
            "gaussian_logdet": float(logdet),
            "gaussian_actual_quadratic": float(np.vdot(wr, wr).real),
            "gaussian_actual_zero_cfo_loglr": float(np.vdot(wr, wr).real - logdet),
            "gaussian_signal_quadratic": float(np.vdot(wm, wm).real),
            "gaussian_expected_loglr_given_oracle_mean": float(
                np.vdot(wm, wm).real + np.trace(pn).real - logdet)}

    alpha = math.exp(-GATE)
    benchmarks, fading = [], []
    for presence_target in (.9, .99):
        z = stats.norm.ppf(presence_target)
        known_np = .5 * (stats.norm.isf(alpha) + z) ** 2
        fixed_gate = .5 * (z + math.sqrt(z * z + 2 * GATE)) ** 2
        for rank in (1, 2):
            gamma_gate = stats.gamma.isf(alpha, rank)
            rank_np = optimize.brentq(lambda e: stats.ncx2.sf(
                2 * gamma_gate, 2 * rank, 2 * e) - presence_target, .0001, 1000)

            def conditional_presence(e):
                weight = e / (e + rank)
                logdet = rank * math.log1p(e / rank)
                return stats.ncx2.sf(2 * (GATE + logdet) / weight, 2 * rank, 2 * e)

            conditional_energy = optimize.brentq(
                lambda e: conditional_presence(e) - presence_target, .0001, 1000)
            benchmarks.append({
                "target_presence_probability": presence_target, "rank": rank,
                "h1_model": "fixed total signal energy; orthogonal equal-energy basis columns",
                "receiver_model": "proper Gaussian coefficient prior; H1 performance is conditional on fixed energy, not averaged over Gaussian fading",
                "known_channel_NP_energy": known_np,
                "known_channel_fixed_evalue_energy": fixed_gate,
                "unknown_gain_rank_gamma_NP_energy": rank_np,
                "Gaussian_prior_receiver_fixed_energy_H1_fixed_evalue_energy": conditional_energy,
                "ideal_seconds_at_minus36": conditional_energy / CN0,
                "ideal_days_at_minus36": conditional_energy / CN0 / 86400,
                "hypothetical_matched_day_energy_presence_probability": conditional_presence(float(energy.sum())),
                "hypothetical_matched_half_energy_presence_probability": conditional_presence(float(energy.sum()) / 2),
                "hypothetical_probability_note": "ideal matched models at these energies, not predictions for the mismatched actual Gram matrices"})
        fading_energy = optimize.brentq(lambda e: stats.gamma.sf(
            2 * (GATE + 2 * math.log1p(e / 2)) / e, 2) - presence_target, 1, 1e5)
        fading.append({"target_presence_probability": presence_target,
                       "h1_model": "two independent static proper Gaussian ray gains; averaged over fading and noise",
                       "rank": 2, "mean_energy_snr": fading_energy,
                       "ideal_days_at_minus36": fading_energy / CN0 / 86400})
    out.update(threshold=GATE, alpha_per_score=alpha,
               fixed_energy_presence_benchmarks=benchmarks,
               gaussian_static_fading_presence_benchmarks=fading)
    retained = out["projections"]["full_bank"]["blockwise_oracle_energy_snr"]
    out["known_retained_oracle_presence_probability_fixed_gate"] = float(
        stats.norm.sf((GATE - retained) / math.sqrt(2 * retained)))
    args.output.write_text(json.dumps(out, indent=2) + "\n")


if __name__ == "__main__":
    main()

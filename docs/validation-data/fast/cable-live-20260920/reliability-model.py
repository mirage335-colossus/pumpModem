#!/usr/bin/env python3
"""Offline planning calculations; neither live measurements nor a modem change.

Run from this directory (or anywhere):
    python3 reliability-model.py > reliability-model.json

Uses the archived production-estimator airtime.csv and Python standard library.
The exact-QPSK marker calculation is a surrogate, not a production APSK bound.
"""
import csv
import json
import math
from pathlib import Path


def binomial_cdf(n, k, q):
    if k >= n or q <= 0:
        return 1.0
    if q >= 1:
        return 0.0
    terms = [
        math.lgamma(n + 1) - math.lgamma(j + 1) - math.lgamma(n - j + 1)
        + j * math.log(q) + (n - j) * math.log1p(-q)
        for j in range(k + 1)
    ]
    high = max(terms)
    return min(1.0, math.exp(high) * sum(math.exp(x - high) for x in terms))


def threshold_fer(k, repairs, target=0.8):
    lo, hi = 0.0, 1.0
    for _ in range(70):
        mid = (lo + hi) / 2
        if binomial_cdf(k + repairs, repairs, mid) > target:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def best_repairs(k, q):
    # Finite search is the same 0..ceil(10% * K) scope as the earlier study.
    repairs = max(
        range(math.ceil(k * 0.1) + 1),
        key=lambda r: binomial_cdf(k + r, r, q) * k / (k + r),
    )
    success = binomial_cdf(k + repairs, repairs, q)
    return {
        "repairs": repairs,
        "parity_per_data": repairs / k,
        "success_probability": success,
        "relative_goodput": success * k / (k + repairs),
    }


def qpsk_false_marker(length):
    """Exact count for independent uniform unit QPSK, symbol-aligned window.

    Multiplication by the known QPSK marker leaves four equiprobable phases.
    The production detector compares SQUARED normalized coherence with 0.72:
        quality = |sum(point * conj(marker))|^2 / (N * sum(|point|^2)).
    Under this surrogate it equals ((a-c)^2 + (b-d)^2) / N^2.
    An integer test uses 0.72 == 18/25 and preserves the strict inequality.
    Summing multinomial counts also includes the phase rotation ambiguity.
    """
    factorial = [math.factorial(i) for i in range(length + 1)]
    accepted = 0
    for a in range(length + 1):
        for b in range(length - a + 1):
            for c in range(length - a - b + 1):
                d = length - a - b - c
                if 25 * ((a - c)**2 + (b - d)**2) > 18 * length**2:
                    accepted += factorial[length] // (
                        factorial[a] * factorial[b] * factorial[c] * factorial[d]
                    )
    probability = accepted / 4**length
    return {
        "qpsk_symbols": length,
        "squared_coherence_threshold": 0.72,
        "accepted_sequences": str(accepted),
        "all_sequences": str(4**length),
        "false_match_per_symbol_aligned_window": probability,
        "negative_log2_probability": -math.log2(probability),
    }


def main():
    folder = Path(__file__).resolve().parent
    with (folder / "airtime.csv").open(newline="") as source:
        rows = list(csv.DictReader(source))

    def row(size, order=256, rate="0.875", robust="0", depth=62):
        return next(r for r in rows if
                    int(r["bytes"]) == size and r["encrypted"] == "0"
                    and int(r["apsk"]) == order and r["rate"] == rate
                    and r["robust"] == robust and int(r["depth"]) == depth)

    result = {
        "scope": "Analytical planning only; not observed hardware reliability",
        "assumptions": [
            "Decimal file bytes; production v1 airtime includes 6.25 s silence",
            "Cycle/word/file success models require stationary independent trials",
            "LDPC model uses 6292 source bytes per word and detected erasures",
            "LDPC model has no bursts, sync failure, CPU overrun or interruptions",
            "QPSK-marker surrogate does not establish production APSK false locks",
        ],
        "confidence": {
            "one_sided_level": 0.95,
            "lower_success_after_13_of_13": 0.05**(1 / 13),
            "lower_success_after_14_of_14": 0.05**(1 / 14),
            "fer_upper_after_zero_of_800": -math.expm1(math.log(0.05) / 800),
        },
        "existing_codec": [],
        "ldpc_sparse_outer_model": [],
        "airtime_success_break_even": [],
        "ldpc_goodput_optimum": [],
        "qpsk_marker_surrogate": [],
    }
    upper = result["confidence"]["fer_upper_after_zero_of_800"]
    for size in (100_000, 5_000_000, 50_000_000):
        selected = row(size)
        cycles = int(selected["cycles"])
        q_cycle = -math.expm1(math.log(0.8) / cycles)
        result["existing_codec"].append({
            "source_bytes": size,
            "profile": "wire/256-APSK/7/8/high-rate-RS/depth62/public",
            "cycles_including_bootstrap": cycles,
            "seconds": float(selected["seconds"]),
            "max_independent_cycle_failure_for_80pct_file": q_cycle,
            "zero_failure_cycles_for_95pct_upper_below_threshold": math.ceil(
                math.log(0.05) / math.log1p(-q_cycle)),
            "hypothetical_uncoded_ber_for_80pct_file": -math.expm1(
                math.log(0.8) / (8 * size)),
        })
        k = math.ceil(size / 6292)
        repairs = math.ceil(k * 0.003)
        q = threshold_fer(k, repairs)
        result["ldpc_sparse_outer_model"].append({
            "source_bytes": size, "data_words": k, "repair_words": repairs,
            "parity_per_data": repairs / k,
            "max_independent_word_fer_for_80pct_file": q,
            "zero_failure_words_for_95pct_upper_below_threshold": math.ceil(
                math.log(0.05) / math.log1p(-q)),
            "success_at_word_fer_0_001": binomial_cdf(k + repairs, repairs, 0.001),
            "success_at_word_fer_0_003": binomial_cdf(k + repairs, repairs, 0.003),
            "success_at_zero_of_800_fer_upper95": binomial_cdf(
                k + repairs, repairs, upper),
        })
        states = [
            ("default", row(size, 16, "0.75", "1", 16)),
            ("256-APSK", row(size, 256, "0.75", "1", 16)),
            ("7/8", row(size, 256, "0.875", "1", 16)),
            ("high-rate-RS", row(size, 256, "0.875", "0", 16)),
            ("depth62", row(size)),
        ]
        for (old_name, old), (new_name, new) in zip(states, states[1:]):
            result["airtime_success_break_even"].append({
                "source_bytes": size, "from": old_name, "to": new_name,
                "new_success_divided_by_old_must_exceed": (
                    float(new["seconds"]) / float(old["seconds"])),
            })
        for q in (0.0001, 0.0003, 0.001, 0.003, upper, 0.01):
            result["ldpc_goodput_optimum"].append({
                "source_bytes": size, "data_words": k, "word_fer": q,
                **best_repairs(k, q),
            })

    # This is a *conditional* union bound for ideal symbol-aligned uniform
    # QPSK windows. Overlap does not invalidate a union bound. Oversampled or
    # refined production decisions do not have the same marginal distribution.
    # H=2^30 is an explicit planning search budget, not a measured search count.
    windows = 2**30
    for n in (48, 56, 64, 72, 80, 88, 96):
        marker = qpsk_false_marker(n)
        marker["surrogate_search_windows"] = windows
        marker["conditional_union_upper_bound"] = min(
            1.0, windows * marker["false_match_per_symbol_aligned_window"])
        marker["conditional_union_negative_log2_bound"] = -math.log2(
            marker["conditional_union_upper_bound"])
        result["qpsk_marker_surrogate"].append(marker)

    # At N=1 every QPSK point passes a phase-invariant match, a simple exact
    # check that this is squared magnitude matching rather than one phase label.
    assert qpsk_false_marker(1)["false_match_per_symbol_aligned_window"] == 1
    assert result["confidence"]["lower_success_after_13_of_13"] < 0.8
    assert result["confidence"]["lower_success_after_14_of_14"] > 0.8
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()

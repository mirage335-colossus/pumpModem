#!/usr/bin/env python3
"""Analyze offline LDPC trials; no modem or wire-format changes.

Uses only Python's standard library. FER upper bounds are one-sided 95%
Clopper-Pearson bounds, not an assertion that unobserved errors are absent.
RS calculations assume independent, detected whole-LDPC-word erasures.
"""
import argparse
import csv
import math


def binomial_cdf(n, k, q):
    if k < 0:
        return 0.0
    if k >= n or q <= 0:
        return 1.0
    if q >= 1:
        return 0.0
    logs = [
        math.lgamma(n + 1) - math.lgamma(j + 1) - math.lgamma(n - j + 1)
        + j * math.log(q) + (n - j) * math.log1p(-q)
        for j in range(k + 1)
    ]
    high = max(logs)
    return min(1.0, math.exp(high) * sum(math.exp(x - high) for x in logs))


def upper_fer(failures, trials, alpha=0.05):
    if failures == trials:
        return 1.0
    if failures == 0:
        return -math.expm1(math.log(alpha) / trials)
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if binomial_cdf(trials, failures, mid) > alpha:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def best_repair(k, q, floor_fraction=0.003):
    # The floor is rounded UP; never relabel 3/688 as 0.3%.
    first = math.ceil(k * floor_fraction)
    # The bounded candidate set includes up to 10% parity, not arbitrary rates.
    values = range(first, max(first + 1, math.ceil(k * 0.10) + 1))
    r = max(values, key=lambda r: binomial_cdf(k + r, r, q) * k / (k + r))
    success = binomial_cdf(k + r, r, q)
    return r, success, success * k / (k + r)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", help="LDPC benchmark CSV")
    parser.add_argument("--bytes", type=int, default=5_000_000)
    parser.add_argument("--integrity-bytes", type=int, default=8,
                        help="modeled integrity bytes inside each LDPC input word (default: 8)")
    parser.add_argument("--min-repair-fraction", type=float, default=0.003,
                        help="minimum repair/data fraction; use 0 for short-file analysis")
    args = parser.parse_args()
    if args.bytes <= 0 or args.integrity_bytes < 0:
        parser.error("file size must be positive and integrity size nonnegative")
    if not 0 <= args.min_repair_fraction <= 0.1:
        parser.error("minimum repair fraction must be between 0 and 0.1")
    fields = ["geometry", "order", "rate", "snr_in_band_db", "frames", "failures",
              "observed_fer", "fer_upper95", "source_bytes_per_word", "data_words", "repair_at_observed_fer",
              "repair_at_upper95", "parity_percent_at_upper95", "modeled_success_at_upper95",
              "coded_payload_bits_per_symbol"]
    import sys
    writer = csv.DictWriter(sys.stdout, fieldnames=fields)
    writer.writeheader()
    with open(args.csv, newline="") as source:
        for row in csv.DictReader(source):
            count, failed = int(row["frames"]), int(row["failed_frames"])
            if count <= 0 or not 0 <= failed <= count:
                raise ValueError("invalid trial/failure count")
            q = failed / count
            high = upper_fer(failed, count)
            payload = int(row["k"]) // 8 - args.integrity_bytes
            if payload <= 0:
                raise ValueError("integrity field leaves no source payload")
            k = (args.bytes + payload - 1) // payload
            r_observed, _, _ = best_repair(k, q, args.min_repair_fraction)
            r_high, success, _ = best_repair(k, high, args.min_repair_fraction)
            writer.writerow(dict(zip(fields, [
                row["geometry"], row["order"], row["rate"],
                float(row["EsN0_dB"]) - 10 * math.log10(1.2), count, failed,
                q, high, payload, k, r_observed, r_high, 100 * r_high / k, success,
                math.log2(int(row["order"])) * int(row["k"]) / int(row["n"]),
            ])))


if __name__ == "__main__":
    main()

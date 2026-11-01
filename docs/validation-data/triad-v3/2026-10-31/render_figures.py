#!/usr/bin/env python3
"""Render the recorded study; accepts a directory or evidence.tar.gz, no extraction.

Requires matplotlib, NumPy and SciPy. Run with --evidence PATH --output DIR.
All curves join measured points; they are not interpolated performance claims.
"""
import argparse
import collections
import json
from pathlib import Path
import tarfile

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import beta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    archive = tarfile.open(args.evidence) if args.evidence.is_file() else None

    def read(name):
        if archive:
            member = archive.extractfile(name)
            if member is None:
                raise ValueError(f"Missing evidence: {name}")
            return member.read().decode()
        return (args.evidence / name).read_text()

    def rows(name):
        return [json.loads(line) for line in read(name).splitlines() if line.strip()]

    def interval(k, n):
        return (0 if k == 0 else beta.ppf(.025, k, n-k+1),
                1 if k == n else beta.ppf(.975, k+1, n-k))

    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.size": 10, "axes.spines.top": False,
                         "axes.spines.right": False, "figure.dpi": 150})
    fig, ax = plt.subplots(figsize=(8, 4.7), layout="constrained")
    paired = rows("triad-paired-hf16-six.jsonl")
    names = {"coupled_masked_glrt": "Coupled two-ray GLRT",
             "flat_masked_glrt": "Flat whole-window GLRT",
             "block_first_shared_gain": "Block-first shared gain",
             "block_first_per_carrier": "Block-first per-carrier gains",
             "product_first_three_pairs": "Product-first three pairs",
             "amplitude_only_positive_gain": "Amplitude-only power fit"}
    for key, label in names.items():
        x, y, low, high = [], [], [], []
        for row in sorted(paired, key=lambda r: r["cn0_db_hz"]):
            result = row["methods"][key]["positive"]
            k, n = result["correct_accepted"], result["trials"]
            a, b = interval(k, n)
            x.append(row["cn0_db_hz"]); y.append(k/n)
            low.append(k/n-a); high.append(b-k/n)
        ax.errorbar(x, y, yerr=[low, high], marker="o", markersize=3,
                    capsize=2, linewidth=1.2, label=label)
    ax.set(xlabel="C/N₀ (dB-Hz; total three-carrier power)",
           ylabel="Correct accepted fraction", ylim=(-.03, 1.04),
           title="Same sampled HF realization, 69.6 s pattern, 4,096 holdout trials")
    ax.legend(fontsize=8, loc="center right"); ax.grid(alpha=.2)
    fig.text(.12, -.035, "Separate detectors; 1% one-bank false budget. Block methods: 1.6 s. Bars: pointwise 95% intervals.", fontsize=8)
    fig.savefig(args.output / "receiver-comparison.png", bbox_inches="tight")
    plt.close(fig)

    outcomes = rows("long-campaign/outcomes.jsonl")
    groups = collections.defaultdict(list)
    for case in outcomes:
        if "colored_hf" not in case["case"]:
            continue
        result = rows(f"long-campaign/case-{case['index']:05d}/results.jsonl")[0]
        groups[result["cn0_db_hz"], result["bit_seconds"]].append(int(result["correct"]))
    fig, ax = plt.subplots(figsize=(8, 4.6), layout="constrained")
    for cn0 in sorted({key[0] for key in groups}, reverse=True):
        x, y, low, high = [], [], [], []
        for (point_cn0, duration), outcomes in sorted(groups.items()):
            if point_cn0 != cn0:
                continue
            k, n = sum(outcomes), len(outcomes); a, b = interval(k, n)
            x.append(duration/3600); y.append(k/n); low.append(k/n-a); high.append(b-k/n)
        ax.errorbar(x, y, yerr=[low, high], marker="o", capsize=4, label=f"{cn0:g} dB-Hz")
    ax.set(xscale="log", xlabel="Actual transmitted pattern span (hours)",
           ylabel="Correct accepted fraction", ylim=(-.03, 1.04),
           title="Sequential sampled HF + colored-oscillator observations")
    ax.set_xticks([1, 4, 12, 48], labels=["1", "4", "12", "48"])
    ax.legend(); ax.grid(alpha=.2)
    fig.text(.12, -.035, "Four trials per point; 95% binomial intervals are wide. These curves do not qualify 99% reliability.", fontsize=8)
    fig.savefig(args.output / "long-observations.png", bbox_inches="tight")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 4.6), layout="constrained")
    acquired = sorted(rows("triad-fft-full.jsonl"), key=lambda r: r["cn0_db_hz"])
    x, y, low, high = [], [], [], []
    for row in acquired:
        result = row["positive"]; k = result["correct_bit_and_epoch_accepted"]; n = result["trials"]
        a, b = interval(k, n); x.append(row["cn0_db_hz"]); y.append(k/n); low.append(k/n-a); high.append(b-k/n)
    ax.errorbar(x, y, yerr=[low, high], marker="o", capsize=3, label="Correct bit and epoch")
    ax.axhline(.99, color="gray", linestyle=":", label="99% target")
    ax.set(xlabel="C/N₀ (dB-Hz; total three-carrier power)",
           ylabel="Correct accepted fraction", ylim=(-.03, 1.04),
           title="Exhaustive FFT acquisition: 5.53 million hypotheses/capture")
    ax.legend(); ax.grid(alpha=.2)
    fig.text(.12, -.035, "1,024 noise trials/point; one fixed sampled channel. Daily 10⁻⁴ gate is analytic, not empirically qualified here.", fontsize=8)
    fig.savefig(args.output / "full-acquisition.png", bbox_inches="tight")
    plt.close(fig)
    if archive:
        archive.close()


if __name__ == "__main__":
    main()

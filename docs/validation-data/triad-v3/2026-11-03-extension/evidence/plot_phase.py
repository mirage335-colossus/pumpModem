#!/usr/bin/env python3
"""Plot the retained synthetic holdout; never label it sampled validation."""
import json
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/triad-extension-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
data = json.loads((HERE.parent / "phase/independent-validation.json").read_text())
methods = [("global_unknown_phase", "One\nglobal phase"),
           ("segmented_1_tau", "Segmented\nphase"),
           ("finite_phase_32_reblock_1", "Phase\ntracking"),
           ("oracle_phase", "Known\nphase oracle")]
fig, axes = plt.subplots(1, 2, figsize=(9, 3.8), sharey=True)
for ax, cn0 in zip(axes, [-60, -57]):
    for i, (method, label) in enumerate(methods):
        row = next(r for r in data["rows"]
                   if r["method"] == method and r["cn0_db_hz"] == cn0)
        result = row["outcomes"]["strict"]
        value = result["correct_accepted"] / result["signal_trials"]
        lower, upper = result["correct_interval95"]
        ax.bar(i, value, color=["#7b8793", "#ba8d40", "#246e9c", "#8bbfa0"][i])
        ax.errorbar(i, value, yerr=[[value - lower], [upper - value]],
                    fmt="none", color="#222", capsize=3)
    ax.set_xticks(range(4), [x[1] for x in methods], fontsize=9)
    ax.set_ylim(0, 1.07)
    ax.set_title(f"{cn0} dB-Hz")
    ax.axhline(.99, color="#555", linestyle=":", linewidth=.8)
    ax.grid(axis="y", alpha=.2)
axes[0].set_ylabel("Correct admission fraction (strict gate)")
fig.suptitle("Controlled common-phase model · 850-day equivalent · 256 independent paths",
             fontsize=11)
fig.text(.5, .015,
         "Known amplitude/alignment, orthogonal patterns; not sampled triad validation. "
         "Whiskers: two one-sided 95% bounds.", ha="center", fontsize=8)
fig.tight_layout(rect=(0, .06, 1, .95))
fig.savefig(HERE / "phase-diagnostic.png", dpi=160)

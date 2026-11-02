#!/usr/bin/env python3
"""Plot frozen tone-response evidence; never run or substitute a new probe.

Run from any directory with a Python environment containing Matplotlib.
All four frozen inputs live beside this script and must match the SHA256
values below; optional CLI arguments can select exact archived copies.

Metric audit against both bound C++ probes:
  complex: sqrt(mean_t((|g0-g1|^2 + |g1-g2|^2)/2))
  amplitude: sqrt(mean_t(((|g0|-|g1|)^2 + (|g1|-|g2|)^2)/2))
  phase: sqrt(mean_t((wrap(arg(g0/g1))^2 + wrap(arg(g1/g2))^2)/2))
Legacy saves complex and phase differences, but not amplitude differences.
Per-tone magnitude RMS cannot recover the missing amplitude-difference RMS.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys


HASHES = {
    "legacy": "89b4463a9e0003faa593d29c8727843eadbcbe6f56f3826fb3ae68435fe40a48",
    "corrected": "bf1edb64821c6c8d37e72e6e1cbfaf1f8d0611cb986672aa1cf255cfab6175d4",
    "legacy_source": "26fecc0ae5f8e9e2e3be0dc72a3d097095b45496d4214430f79a19895c362dd0",
    "corrected_source": "c9fed6aab032dc44425858b33f07f3f7bf0652c2fbdd29549937c0583ee5c29a",
}
OFFSETS = (0., .025, .05)
DEPTHS = (.25, .65)
SEED_A = 396604873
SEED_B = 16053920542468888184
SPACING = .00075
METRICS = ("adjacent_complex_response_difference_rms",
           "adjacent_amplitude_difference_rms",
           "adjacent_phase_difference_rms_rad")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def checked_input(path, label):
    data = path.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    require(actual == HASHES[label], f"{label}: SHA256 mismatch for {path}: {actual}")
    return data, {"path": str(path.resolve()), "sha256": actual}


def read_evidence(args):
    payload, provenance = {}, {}
    for name in HASHES:
        payload[name], provenance[name] = checked_input(getattr(args, name), name)
    legacy = [json.loads(line) for line in payload["legacy"].splitlines() if line.strip()]
    corrected = [json.loads(line) for line in payload["corrected"].splitlines() if line.strip()]
    require(len(legacy) == 30 and len(corrected) == 58, "frozen input row count differs")
    selected_legacy, selected_corrected = {}, {}
    for row in legacy:
        if row["spacing_hz"] != SPACING or row["offset_hz"] not in OFFSETS:
            continue
        depth = row["depth_slow"]
        require(depth in DEPTHS and row["depth_fast"] == depth, "legacy depth is not selected Each mode")
        require(row["duration_s"] == 24000 and row["transient_discard_s"] == 5000
                and row["n_measure"] == 19000, "legacy measurement window differs")
        key = (depth, row["offset_hz"])
        require(key not in selected_legacy, "duplicate legacy depth/offset")
        # Fs=1 and seed A come from the hash-bound legacy C++ source, since
        # this historical JSONL did not record them.
        selected_legacy[key] = {
            METRICS[0]: row["adjacent_response_difference_rms"],
            METRICS[2]: row["adjacent_phase_difference_rms_rad"],
        }
    for row in corrected:
        if row["spacing_hz"] != SPACING or row["fs_hz"] != 1:
            continue
        require(row["model"] == "corrected-dispersive-v2", "unexpected corrected model")
        require(row["depth"] in DEPTHS and row["seed"] in (SEED_A, SEED_B)
                and row["offset_hz"] in OFFSETS, "unexpected corrected case")
        require(row["measured_seconds"] == 20000 and row["discard_s"] == 1890
                and row["discard_s"] >= row["tail_samples"] / row["fs_hz"],
                "corrected probe measurement/warm-up contract differs")
        key = (row["depth"], row["seed"], row["offset_hz"])
        require(key not in selected_corrected, "duplicate corrected depth/seed/offset")
        selected_corrected[key] = {metric: row[metric] for metric in METRICS}
    require(set(selected_legacy) == {(d, o) for d in DEPTHS for o in OFFSETS},
            "missing legacy comparison cases")
    require(set(selected_corrected) == {(d, s, o) for d in DEPTHS for s in (SEED_A, SEED_B) for o in OFFSETS},
            "missing corrected comparison cases")
    for group in (selected_legacy, selected_corrected):
        for row in group.values():
            require(all(math.isfinite(v) and v >= 0 for v in row.values()), "invalid measured RMS")
    return selected_legacy, selected_corrected, provenance


def render(legacy, corrected, provenance, output):
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/triad-selective-mpl")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10, "axes.titlesize": 12,
                         "axes.spines.top": False, "axes.spines.right": False})
    fig, axes = plt.subplots(1, 3, figsize=(15.8, 7.2))
    colors = {.25: "#0072B2", .65: "#D55E00"}
    for depth in DEPTHS:
        for seed, seed_name, marker, style in ((SEED_A, "A", "o", "-"), (SEED_B, "B", "^", "--")):
            label = f"Corrected, Each {depth:.0%}, seed {seed_name}"
            for axis, metric in zip(axes, METRICS):
                axis.plot(OFFSETS, [corrected[depth, seed, x][metric] for x in OFFSETS],
                          color=colors[depth], marker=marker, linestyle=style,
                          linewidth=1.7, markersize=6, label=label)
        label = f"Legacy, Each {depth:.0%}, seed A"
        for index in (0, 2):
            metric = METRICS[index]
            axes[index].plot(OFFSETS, [legacy[depth, x][metric] for x in OFFSETS],
                             color=colors[depth], marker="x", linestyle=":",
                             linewidth=1.8, markersize=7, label=label)
    axes[0].set_title("Adjacent complex response difference")
    axes[0].set_ylabel("RMS complex-gain difference (log scale)")
    axes[0].set_yscale("log")
    axes[0].set_ylim(3e-5, 2)
    axes[1].set_title("Adjacent amplitude difference")
    axes[1].set_ylabel("RMS magnitude difference")
    axes[1].set_ylim(0, 1.2)
    axes[1].text(.5, -.24, "Legacy: no adjacent-amplitude\nmeasurement saved",
                 transform=axes[1].transAxes, ha="center", va="top", color="#555555", fontsize=9)
    axes[2].set_title("Adjacent wrapped phase difference")
    axes[2].set_ylabel("RMS phase difference (rad; symlog)")
    axes[2].set_yscale("symlog", linthresh=.001, linscale=.6)
    axes[2].set_ylim(0, 1.5)
    axes[2].set_yticks((0, .001, .01, .1, 1), labels=("0", "0.001", "0.01", "0.1", "1"))
    axes[2].text(.5, -.24, "Linear below 0.001 rad;\nzero-offset Each25 ≈ 10⁻¹² rad",
                 transform=axes[2].transAxes, ha="center", va="top", color="#555555", fontsize=9)
    for axis in axes:
        axis.set_xlabel("Common frequency offset (Hz)")
        axis.set_xticks(OFFSETS, labels=("0", "0.025", "0.05"))
        axis.set_xlim(-.002, .052)
        axis.grid(True, color="#dddddd", linewidth=.7)
        axis.set_axisbelow(True)
    fig.suptitle("Measured adjacent-carrier response across common frequency offsets", fontsize=16, y=.98)
    fig.text(.5, .923, "Carrier spacing 0.00075 Hz · Fs 1 Hz · slow/fast modulation 0.005/0.05 Hz",
             ha="center", fontsize=11)
    handles, labels = axes[0].get_legend_handles_labels()
    order = (0, 3, 1, 4, 2, 5)  # Matplotlib fills columns; keep one depth per displayed row.
    fig.legend([handles[i] for i in order], [labels[i] for i in order],
               loc="upper center", bbox_to_anchor=(.5, .885),
               ncol=3, frameon=False, fontsize=9)
    notes = [
        "Actual isolated-tone response measurements. Points are measured; lines only connect the three common tested offsets.",
        "Legacy: one seed, 19,000 s measured after 5,000 s discard. Corrected: two seeds, 20,000 s measured after 1,890 s adaptive discard.",
        "Complex/phase pair definitions match; legacy amplitude differences were not recorded. Wrapped phase includes Each65 polarity reversals.",
        "Tone-operator diagnostic only: no receiver-sensitivity or calibrated-propagation claim. Seeds are illustrative cases, not confidence intervals.",
    ]
    fig.text(.5, .025, "\n".join(notes), ha="center", va="bottom", fontsize=9, linespacing=1.45)
    fig.subplots_adjust(left=.065, right=.985, top=.70, bottom=.28, wspace=.32)
    metadata = {
        "input_files": provenance,
        "plot_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "spacing_hz": SPACING, "fs_hz": 1, "offsets_hz": OFFSETS,
        "legacy_seed_from_bound_source": SEED_A, "corrected_seeds": [SEED_A, SEED_B],
        "excluded": "legacy adjacent amplitude: not saved; legacy negative offsets: outside corrected grid",
        "measurement_limits": notes, "matplotlib_version": matplotlib.__version__,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=170, facecolor="white",
                metadata={"Description": json.dumps(metadata, sort_keys=True)})
    plt.close(fig)
    return metadata


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", type=Path, default=here / "legacy-response-results.jsonl")
    parser.add_argument("--legacy-source", type=Path, default=here / "legacy_probe.cpp")
    parser.add_argument("--corrected", type=Path, default=here / "response-results.jsonl")
    parser.add_argument("--corrected-source", type=Path, default=here / "dispersive_probe.cpp")
    parser.add_argument("--output", type=Path, default=here / "response-comparison.png")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".png":
        parser.error("--output must be a PNG path")
    try:
        legacy, corrected, provenance = read_evidence(args)
        metadata = render(legacy, corrected, provenance, args.output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("response plot failed: " + str(error), file=sys.stderr)
        return 2
    print(json.dumps({"output": str(args.output), "legacy_rows_plotted": len(legacy),
                      "corrected_rows_plotted": len(corrected), "input_files": provenance,
                      "plot_source_sha256": metadata["plot_source_sha256"],
                      "png_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                      "new_experiments": 0}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Verify and plot six completed conditional development screens.

--root contains scores/, screen/, highspread-scores/ and highspread/.
No score, manifest or source data is edited.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys

import numpy as np


STRICT_GATE = 22.72106338958574
PROFILES = ("g001", "g01", "g1", "stable", "g10-aligned", "g30-aligned")
MODES = ("off", "each-25", "each-65")
LABELS = {"off": "Off", "each-25": "Each 25%", "each-65": "Each 65%"}
COLORS = {"off": "#0072B2", "each-25": "#D55E00", "each-65": "#8C4FA3"}
MARKERS = {"off": "o", "each-25": "s", "each-65": "^"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def report_path(root, profile, mode):
    directory = "highspread-scores" if profile.endswith("-aligned") else "scores"
    return root / directory / f"{profile}-{mode}.json"


def recount(scores, truth, control):
    winners = np.argmax(scores, axis=1)
    accepted = scores[np.arange(len(scores)), winners] >= STRICT_GATE
    if control == "signal":
        return {"trials": len(scores), "correct_accepted": int(np.sum(accepted & (winners == truth))),
                "wrong_accepted": int(np.sum(accepted & (winners != truth))),
                "missed": int(np.sum(~accepted)), "raw_correct": int(np.sum(winners == truth))}
    return {"trials": len(scores), "false_admitted": int(np.sum(accepted)),
            "not_admitted": int(np.sum(~accepted))}


def load_case(root, profile, mode):
    """Return independently recounted plotted values and bound metadata."""
    path = report_path(root, profile, mode)
    report = json.loads(path.read_text())
    prefix = str(path)
    require(report["schema"] == "triad-conditional-sweep-v1", prefix + ": unsupported score schema")
    require(report["observation_mode"] == "conditional", prefix + ": expected conditional observations")
    require(report["failed_physical_path_records"] == 0, prefix + ": contains failed physical paths")
    require(math.isclose(report["family"]["fixed_window_log_threshold"], STRICT_GATE,
                         rel_tol=0, abs_tol=1e-12), prefix + ": strict gate differs")
    config = report["configuration"]
    expected_method = "static-fft-ucb" if profile == "stable" else "gaussian-window-ucb"
    require(config["receiver_methods"] == [expected_method], prefix + ": wrong receiver baseline")
    require(config["noise_trials"] == config["trial_batch"] == 16,
            prefix + ": expected sixteen conditional draws in one batch")
    require(config["controls"] == ["signal", "null"], prefix + ": expected signal and null controls")
    require(config["selection_methods"] == 16 and config["duration_alternatives"] == 8
            and config["calibration_alternatives"] == 2, prefix + ": admission family differs")
    require(config["false_probability"] == 1e-4 and config["horizon_seconds"] == 86400
            and config["attempt_spacing"] == 60, prefix + ": operational admission budget differs")
    if expected_method == "gaussian-window-ucb":
        require(config["window_blocks"] == 64, prefix + ": expected Gaussian J64 baseline")
    grid = [float(v) for v in config["cn0_grid"]]
    require(grid and all(math.isfinite(v) for v in grid) and len(set(grid)) == len(grid),
            prefix + ": invalid C/N0 grid")
    expected_paths = 2 if profile.endswith("-aligned") else 4
    records = report["paths"]
    require(report["sampled_physical_paths"] == len(records) == expected_paths,
            prefix + ": wrong physical path count")
    score_name = report["scores_file"]
    require(Path(score_name).name == score_name and score_name.endswith(".npz"),
            prefix + ": score array must be a local NPZ basename")
    score_path = path.parent / score_name
    require(sha256(score_path) == report["scores_sha256"], str(score_path) + ": SHA256 mismatch")
    source_dir = "highspread" if profile.endswith("-aligned") else "screen"
    expected_indices = range(50000, 50002) if expected_paths == 2 else range(40000, 40004)
    expected_names = {f"path-{i:05d}" for i in expected_indices}
    path_names, used_arrays = set(), set()
    probabilities, elapsed, captures, physical_parameters, truths = [], [], [], [], []
    with np.load(score_path, allow_pickle=False) as arrays:
        for ordinal, record in enumerate(records):
            require(record["status"] == "completed", prefix + ": incomplete physical path")
            require(record["physical_path_index"] == ordinal, prefix + ": unexpected path ordering")
            incoming = Path(record["input"])
            require(incoming.parent.name == f"{profile}-{mode}" and incoming.name in expected_names,
                    prefix + ": unexpected sampled path identity")
            require(incoming.name not in path_names, prefix + ": duplicate sampled path")
            path_names.add(incoming.name)
            manifest_path = root / source_dir / f"{profile}-{mode}" / incoming.name / "manifest.json"
            require(sha256(manifest_path) == record["sampled_input_hashes"]["manifest.json"],
                    str(manifest_path) + ": manifest SHA256 mismatch")
            manifest = json.loads(manifest_path.read_text())
            truth = manifest["signal_bit"]
            require(type(truth) is int and truth in (0, 1), str(manifest_path) + ": invalid bit truth")
            truths.append(truth)
            require(manifest["control"] == "signal", str(manifest_path) + ": expected sampled signal design")
            duration = float(manifest["actual_elapsed_seconds"])
            capture = manifest["sample_count"] / manifest["sample_rate"]
            require(math.isfinite(duration) and duration > 0 and math.isfinite(capture) and capture > 0,
                    str(manifest_path) + ": invalid duration")
            require(math.isclose(capture, record["sampled_seconds"], rel_tol=1e-12, abs_tol=1e-9),
                    str(manifest_path) + ": sampled duration differs from receiver receipt")
            elapsed.append(duration)
            captures.append(capture)
            model = manifest["channel_model"]
            width = model["path_evolution"]["gaussian_doppler_width_hz"]
            physical_parameters.append((float(width), float(model["spread_doppler_hz"]),
                                        float(model["spread_delay_ms"])))
            require(manifest["resolved_configuration"]["selective_perturbation"] == mode,
                    str(manifest_path) + ": selective mode mismatch")
            if expected_method == "gaussian-window-ucb":
                require(width == config["window_doppler_width"], prefix + ": physical and receiver B differ")
            if profile.endswith("-aligned"):
                require(manifest["resolved_configuration"]["initial_arrival_bounds_seconds"] == [0, 0],
                        str(manifest_path) + ": aligned case has initial timing uncertainty")
            rows, conditions = {}, set()
            for condition in record["conditions"]:
                cn0, control = float(condition["cn0_db_hz"]), condition["control"]
                require((cn0, control) not in conditions, prefix + ": duplicate condition")
                conditions.add((cn0, control))
                require(condition["noise_trials"] == 16 and set(condition["methods"]) == {expected_method},
                        prefix + ": condition draw count or receiver differs")
                scored = condition["methods"][expected_method]
                require(math.isclose(scored["threshold"], STRICT_GATE, rel_tol=0, abs_tol=1e-12),
                        prefix + ": per-condition strict gate differs")
                key = scored["score_array"]
                require(key not in used_arrays, prefix + ": score array reused by different conditions")
                used_arrays.add(key)
                values = arrays[key]
                require(values.shape == (16, 2) and np.isfinite(values).all(),
                        prefix + ": nonfinite or wrongly shaped score array " + key)
                require(control in ("signal", "null"), prefix + ": unexpected condition control")
                counts = recount(values, truth, control)
                require(counts == scored["counts"], prefix + ": strict score recount differs for " + key)
                if control == "signal":
                    rows[cn0] = counts["correct_accepted"] / 16
            require(conditions == {(cn0, control) for cn0 in grid for control in ("signal", "null")},
                    prefix + ": incomplete C/N0/control grid")
            probabilities.append([rows[cn0] for cn0 in sorted(grid)])
        require(used_arrays == set(arrays.files), prefix + ": NPZ array coverage differs from report")
    require(path_names == expected_names, prefix + ": missing physical path")
    require(truths.count(0) == truths.count(1) == expected_paths // 2, prefix + ": source bits are not balanced")
    require(len(set(physical_parameters)) == 1, prefix + ": path channel configurations differ")
    values = np.asarray(probabilities)
    return {"profile": profile, "mode": mode, "cn0": np.asarray(sorted(grid)),
            "mean": values.mean(axis=0), "minimum": values.min(axis=0), "maximum": values.max(axis=0),
            "paths": expected_paths, "elapsed": elapsed, "captures": captures,
            "physical_parameters": physical_parameters[0], "method": expected_method,
            "score_sha256": report["scores_sha256"]}


def load_all(root):
    missing = [str(report_path(root, p, m)) for p in PROFILES for m in MODES
               if not report_path(root, p, m).is_file()]
    require(not missing, "missing required score reports:\n  " + "\n  ".join(missing))
    panels = {}
    for profile in PROFILES:
        cases = [load_case(root, profile, mode) for mode in MODES]
        require(len({c["physical_parameters"] for c in cases}) == 1,
                profile + ": channel geometry differs across selective modes")
        require(all(np.array_equal(c["cn0"], cases[0]["cn0"]) for c in cases),
                profile + ": C/N0 grids differ across selective modes")
        panels[profile] = cases
    return panels


def duration_text(seconds):
    low, high = min(seconds), max(seconds)
    scale, unit = (3600, "h") if high >= 3600 else (1, "s")
    if math.isclose(low, high, rel_tol=1e-10, abs_tol=1e-8):
        return f"{low / scale:.3f} {unit}"
    return f"{low / scale:.3f}–{high / scale:.3f} {unit}"


def plot(panels, output, path_range=False):
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/triad-selective-mpl")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10, "axes.titlesize": 11, "axes.labelsize": 10,
                         "figure.facecolor": "white", "axes.spines.top": False, "axes.spines.right": False})
    figure, axes = plt.subplots(2, 3, figsize=(16, 10.4), sharey=True)
    for axis, profile in zip(axes.flat, PROFILES):
        cases = panels[profile]
        for case in cases:
            mode = case["mode"]
            if path_range:
                axis.fill_between(case["cn0"], case["minimum"], case["maximum"], color=COLORS[mode], alpha=.12)
            axis.plot(case["cn0"], case["mean"], color=COLORS[mode], marker=MARKERS[mode],
                      markersize=5.5, linewidth=1.7, label=LABELS[mode])
        width, center, delay = cases[0]["physical_parameters"]
        source_time = duration_text([v for c in cases for v in c["elapsed"]])
        capture_time = duration_text([v for c in cases for v in c["captures"]])
        receiver = "Static FFT" if profile == "stable" else "Gaussian J64"
        axis.set_title(f"{profile}  |  {receiver}\nB={width:g} Hz; D={center:g} Hz; delay={delay:g} ms\n"
                       f"Source elapsed {source_time}; capture {capture_time}\n"
                       f"{cases[0]['paths']} physical paths × 16 conditional draws per point", pad=10)
        axis.set_ylim(0, 1)
        axis.set_yticks(np.linspace(0, 1, 6))
        axis.set_xlabel("Total triplet C/N₀ (dB-Hz)")
        axis.grid(True, color="#dddddd", linewidth=.65)
        axis.set_axisbelow(True)
    for axis in axes[:, 0]:
        axis.set_ylabel("Conditional correct-accepted fraction")
    handles, labels = axes[0, 0].get_legend_handles_labels()
    figure.legend(handles, labels, loc="upper center", bbox_to_anchor=(.5, .945), ncol=3, frameon=False)
    figure.suptitle("Frequency-selective amplitude stress: conditional reception", fontsize=16, y=.99)
    note = ("Strict log gate 22.72106338958574; NPZ hashes, manifest hashes and signal/null counts verified. "
            "Markers are tested points; lines only connect them.\n"
            "B is Gaussian twice-RMS ray width; D is ray-center separation. First four panels use nominal timing; last two have known initial arrival.\n"
            "Means average fixed physical paths; conditional replicas are not independent channels. Development evidence, not reliability or daily false-admission qualification.")
    if path_range:
        note += "\nShading is the per-point minimum–maximum across physical paths, not a confidence interval."
    figure.text(.5, .015, note, ha="center", va="bottom", fontsize=9, linespacing=1.45)
    figure.subplots_adjust(left=.065, right=.985, bottom=.17, top=.82, hspace=.75, wspace=.20)
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=170, facecolor="white", metadata={"Description": note})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="PNG output path")
    parser.add_argument("--path-range", action="store_true", help="shade per-path min/max; not a confidence interval")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".png":
        parser.error("--output must have a .png suffix")
    try:
        panels = load_all(args.root.resolve())
        plot(panels, args.output, args.path_range)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("plot validation failed: " + str(error), file=sys.stderr)
        return 2
    print(json.dumps({"output": str(args.output), "panels": 6, "verified_score_npz_files": 18,
                      "verified_manifests": 60, "strict_gate": STRICT_GATE,
                      "ranges_are_confidence_intervals": False,
                      "scope": "conditional development evidence; no qualification claim"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

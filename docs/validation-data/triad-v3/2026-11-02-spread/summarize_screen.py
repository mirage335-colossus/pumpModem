#!/usr/bin/env python3
"""Rebuild the descriptive screen summary and figure from the frozen snapshot."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def controls_for(report: dict[str, Any], control: str = "signal") -> list[dict[str, Any]]:
    out = []
    for path in report.get("paths", []):
        for condition in path.get("conditions", []):
            if condition.get("control") == control:
                out.append((path, condition))
    return out


def summarize_report(path: Path, label: str, snapshot: Path) -> dict[str, Any]:
    d = read_json(path)
    per_path: dict[float, list[dict[str, Any]]] = {}
    for path_rec, condition in controls_for(d):
        cn0 = float(condition["cn0_db_hz"])
        methods = {}
        for name, item in condition["methods"].items():
            c = item["counts"]
            methods[name] = {k: int(c.get(k, 0)) for k in (
                "trials", "correct_accepted", "wrong_accepted", "missed", "raw_correct"
            )}
        per_path.setdefault(cn0, []).append({
            "physical_path_index": path_rec.get("physical_path_index"),
            "input_hashes": path_rec.get("sampled_input_hashes", {}),
            "methods": methods,
        })
    points = []
    for cn0, records in sorted(per_path.items()):
        method_names = sorted({m for r in records for m in r["methods"]})
        for method in method_names:
            totals = {k: 0 for k in ("trials", "correct_accepted", "wrong_accepted", "missed", "raw_correct")}
            for rec in records:
                counts = rec["methods"].get(method)
                if counts:
                    for k in totals:
                        totals[k] += counts[k]
            points.append({
                "cn0_db_hz": cn0,
                "method": method,
                **totals,
                "sampled_physical_path_count": len(records),
                "conditional_correct_acceptance_fraction": (
                    totals["correct_accepted"] / totals["trials"] if totals["trials"] else None
                ),
                "wrong_acceptance_fraction": totals["wrong_accepted"] / totals["trials"] if totals["trials"] else None,
                "scope": "descriptive total over conditional noise trials on these fixed sampled paths; not pooled independent-channel confidence",
            })
    scores_path = path.parent / d.get("scores_file", "")
    physical_path_details = []
    for p in d.get("paths", []):
        input_name = str(p.get("input", ""))
        relative = input_name.split("/spread/", 1)[-1] if "/spread/" in input_name else ""
        manifest = snapshot / relative / "manifest.json" if relative else None
        manifest_doc = read_json(manifest) if manifest is not None and manifest.is_file() else {}
        physical_path_details.append({
            "index": p.get("physical_path_index"),
            "sampled_input_hashes": p.get("sampled_input_hashes", {}),
            "sampled_seconds": p.get("sampled_seconds"),
            "blocks": p.get("block_count"),
            "manifest_relative_path": str(manifest.relative_to(snapshot)) if manifest is not None and manifest.is_file() else None,
            "manifest_sha256": sha256(manifest) if manifest is not None and manifest.is_file() else None,
            "manifest_seed_and_capture_fields": {
                k: manifest_doc.get(k) for k in ("key_seed", "channel_seed", "oscillator_seed", "noise_seed",
                                                   "frontend_seed", "path_seed", "sample_rate", "sample_rate_denominator")
                if k in manifest_doc
            },
        })
    return {
        "label": label,
        "report": str(path.relative_to(snapshot)),
        "report_sha256": sha256(path),
        "scores_file": d.get("scores_file"),
        "scores_sha256_declared": d.get("scores_sha256"),
        "scores_sha256_actual": sha256(scores_path) if scores_path.is_file() else None,
        "source_sha256": d.get("source_sha256"),
        "dependency_sha256": d.get("dependency_sha256"),
        "configuration": d.get("configuration"),
        "family_gate": d.get("family"),
        "sampled_physical_path_count": len(d.get("paths", [])),
        "sampled_physical_paths": physical_path_details,
        "points": points,
    }


def selected_summary(scores: Path, snapshot: Path) -> dict[str, Any]:
    files = {
        "g001_j16": "g001-j16.json", "g001_j32": "g001-j32.json", "g001_j64": "g001-j64.json",
        "g001_width_half": "g001-width0p5.json", "g001_width_double": "g001-width2.json",
        "g001_fine": "g001-fine-j32.json",
        "g01_j16": "g01-j16.json", "g01_j32": "g01-j32.json", "g01_j64": "g01-j64.json",
        "g01_slowrate": "g01-slowrate-j32.json", "g01_width_half": "g01-width0p5.json",
        "g01_width_double": "g01-width2.json",
        "g1_j16": "g1-j16.json", "g1_j32": "g1-j32.json", "g1_j64": "g1-j64.json",
        "g1_coarse": "g1-coarse-j32.json", "g1_fine": "g1-fine-j32.json",
        "g1_width_half": "g1-width0p5.json", "g1_width_double": "g1-width2.json",
        "g10": "g10-j32.json", "g30": "g30-j32.json",
        "stable_day_1": "stable-day-1-fft.json", "stable_day_10": "stable-day-10-fft.json",
        "stable_day_30": "stable-day-30-fft.json",
        "stable_long_0p005": "stable-long-0p005-fft.json", "stable_long_0p05": "stable-long-0p05-fft.json",
        "stable_long_0p1": "stable-long-0p1-fft.json",
        "delay_fast_0": "delay-fast-0-fft.json", "delay_fast_6": "delay-fast-6-fft.json",
        "delay_fast_7": "delay-fast-7-fft.json", "delay_fast_7_mismatch": "delay-fast-7-mismatch-fft.json",
    }
    reports = {}
    for label, name in files.items():
        p = scores / name
        if p.is_file():
            reports[label] = summarize_report(p, label, snapshot)
    return reports


def all_summary(score_dir: Path, snapshot: Path, prefix: str = "") -> dict[str, Any]:
    return {
        prefix + p.stem.replace("-", "_"): summarize_report(p, prefix + p.stem, snapshot)
        for p in sorted(score_dir.glob("*.json"))
    }


def information_summary(info_dir: Path) -> dict[str, Any]:
    outputs = {}
    for name in ("g001", "g01", "g1"):
        path = info_dir / f"{name}.json"
        if not path.is_file():
            continue
        d = read_json(path)
        selected: dict[str, list[float]] = {}
        for workspace in d["workspaces"]:
            for result in workspace.get("window_results", []):
                if int(result.get("window_blocks", -1)) != 64:
                    continue
                for candidate in result.get("candidate_results", []):
                    for crossing in candidate.get("necessary_cn0_crossings", []):
                        selected.setdefault(crossing["profile"], []).append(float(crossing["necessary_cn0_db_hz"]))
        outputs[name] = {
            "input_sha256": sha256(path),
            "scope": d.get("scope"),
            "target_profiles": d.get("target_profiles"),
            "J64_necessary_crossing_range_db_hz_by_profile": {
                profile: {"min": min(values), "max": max(values), "candidate_path_crossings": len(values)}
                for profile, values in sorted(selected.items()) if values
            },
            "crossing_interpretation": "necessary information bound under the stated known-noise/channel covariance hypothesis; not sufficient and not an achievable-receiver prediction",
        }
    return outputs


def observed_information_comparison(reports: dict[str, Any], information: dict[str, Any]) -> dict[str, Any]:
    mapping = {
        "g001": ("g001_j64", -28.0, -26.0),
        "g01": ("g01_j64", -24.0, -22.0),
        "g1": ("g1_j64", -14.0, -12.0),
    }
    result = {}
    for name, (curve, lower_cn0, upper_cn0) in mapping.items():
        record = reports.get(curve)
        observed = []
        if record:
            observed = [p for p in record["points"] if p["method"] == "gaussian-window-ucb"
                        and p["cn0_db_hz"] in (lower_cn0, upper_cn0)]
        crossings = information.get(name, {}).get("J64_necessary_crossing_range_db_hz_by_profile", {})
        result[name] = {
            "observed_conditional_curve": [{"cn0_db_hz": p["cn0_db_hz"], "correct_accepted": p["correct_accepted"],
                                            "trials": p["trials"], "fraction": p["conditional_correct_acceptance_fraction"]}
                                           for p in observed],
            "J64_necessary_known_covariance_crossing_ranges_db_hz": {
                profile: crossings[profile] for profile in ("strict-one-completion-global-alpha", "strict-minute-family-allocation")
                if profile in crossings
            },
            "interpretation": "Model-based necessary-information values are lower limits under different assumptions; compare with the measured interval descriptively only. They do not predict achievable correct-bit reception.",
        }
    return result


def compare_path_identity(reports: dict[str, Any], left: str, right: str) -> dict[str, Any]:
    a, b = reports.get(left), reports.get(right)
    if not a or not b:
        return {"left": left, "right": right, "available": False}
    ah = [p.get("sampled_input_hashes", {}) for p in a["sampled_physical_paths"]]
    bh = [p.get("sampled_input_hashes", {}) for p in b["sampled_physical_paths"]]
    am = [p.get("manifest_seed_and_capture_fields", {}) for p in a["sampled_physical_paths"]]
    bm = [p.get("manifest_seed_and_capture_fields", {}) for p in b["sampled_physical_paths"]]
    return {
        "left": left, "right": right,
        "same_ordered_sampled_input_hashes": ah == bh,
        "same_ordered_manifest_seed_and_capture_fields": am == bm,
        "path_count_left": len(ah), "path_count_right": len(bh),
        "claim_limit": "Exact sampled-input hash identity supports a matched observation comparison. Matching manifest seeds with differing block hashes support only a paired-seed/configuration comparison; stored block summaries may differ due segmentation. Neither comparison adds independent channel replicates.",
    }


def precision_parity(scores: Path) -> dict[str, Any]:
    groups = [
        ("window", ["g001-window-cpu", "g001-window-cuda0", "g001-window-cpu-fp64"]),
        ("cfo", ["g001-cfo-cpu", "g001-cfo-cuda0"]),
    ]
    result = {}
    for group, names in groups:
        docs = {n: read_json(scores / f"{n}.json") for n in names}
        arrays = {n: np.load(scores / f"{n}.scores.npz") for n in names}
        reference = names[0]
        identity_equal = all(
            [p.get("sampled_input_hashes") for p in docs[n]["paths"]]
            == [p.get("sampled_input_hashes") for p in docs[reference]["paths"]]
            for n in names[1:]
        )
        files_equal = set(arrays[reference].files) == set.intersection(*(set(arrays[n].files) for n in names))
        by_candidate = {}
        all_decisions_equal = True
        max_abs = 0.0
        max_threshold = 0.0
        same_threshold = True
        for n in names[1:]:
            compared = 0
            decisions_same = True
            gate_disagreements = 0
            threshold_deltas = []
            for key in sorted(set(arrays[reference].files) & set(arrays[n].files)):
                a = np.asarray(arrays[reference][key], dtype=np.float64)
                b = np.asarray(arrays[n][key], dtype=np.float64)
                if a.shape != b.shape:
                    decisions_same = False
                    continue
                max_abs = max(max_abs, float(np.max(np.abs(a - b))))
                # Two columns are the two candidate bit scores; the detected bit is argmax.
                if not np.array_equal(np.argmax(a, axis=1), np.argmax(b, axis=1)):
                    decisions_same = False
                threshold = docs[reference]["family"].get("fixed_window_log_threshold")
                if threshold is not None:
                    gate_disagreements += int(np.count_nonzero((np.max(a, axis=1) >= threshold) != (np.max(b, axis=1) >= threshold)))
                if docs[reference]["family"].get("fixed_window_log_threshold") != docs[n]["family"].get("fixed_window_log_threshold"):
                    same_threshold = False
                compared += a.shape[0]
            all_decisions_equal &= decisions_same
            by_candidate[n] = {"compared_score_rows": compared, "same_argmax_ranking": decisions_same,
                               "gate_decision_disagreements": gate_disagreements}
        result[group] = {
            "reports": {n: {"sha256": sha256(scores / f"{n}.json"), "device": docs[n]["configuration"].get("device"),
                             "precision": docs[n]["configuration"].get("precision"), "wall_seconds": docs[n].get("wall_seconds"),
                             "score_sha256": docs[n].get("scores_sha256")} for n in names},
            "same_sampled_input_hashes": identity_equal,
            "same_score_array_key_set": files_equal,
            "same_gate_threshold": same_threshold,
            "maximum_absolute_log_score_difference_vs_first_report": max_abs,
            "comparisons": by_candidate,
            "claim_limit": "matched precision/ranking/gate parity only; timing was measured under concurrent load and is not an isolated acceleration comparison",
        }
    return result


def plot(reports: dict[str, Any], aligned: dict[str, Any], output: Path) -> None:
    fig, axes = plt.subplots(3, 2, figsize=(15, 15))

    def add_curve(ax, rec, method, label):
        pts = [p for p in rec["points"] if p["method"] == method and p["conditional_correct_acceptance_fraction"] is not None]
        if pts:
            pts.sort(key=lambda p: p["cn0_db_hz"])
            ax.plot([p["cn0_db_hz"] for p in pts], [p["conditional_correct_acceptance_fraction"] for p in pts],
                    marker="o", label=label)

    ax = axes[0, 0]
    for label, name, source in [("B=.001, 24h requested, J64 ×64s", "g001_j64", reports),
                                ("B=.01, 24h requested, J64 ×8s", "g01_j64", reports),
                                ("B=.1, 2.4h requested, J64 ×1s", "g1_j64", reports),
                                ("B=10, 14.4min requested, known time, J32 ×.0625s", "aligned_g10_aligned", aligned),
                                ("B=30, 120s requested, known time, J32 ×.015625s", "aligned_g30_aligned", aligned)]:
        rec = source.get(name)
        if rec:
            add_curve(ax, rec, "gaussian-window-ucb", label)
    ax.set(title="Doppler-width screen (requested duration; high-B known-time cases)", xlabel="C/N₀ (dB-Hz)", ylabel="P(correct accepted | conditional)", ylim=(-.03, 1.03))
    ax.grid(True, alpha=.3); ax.legend(fontsize=8)

    ax = axes[0, 1]
    for label, name in [("J16", "g001_j16"), ("J32", "g001_j32"), ("J64", "g001_j64"),
                        ("B ×0.5", "g001_width_half"), ("B ×2", "g001_width_double")]:
        rec = reports.get(name)
        if rec:
            method = "gaussian-ou-ucb" if label == "J32" else "gaussian-window-ucb"
            add_curve(ax, rec, method, label)
    ax.set(title="B=.001 Hz, 24h: J × 64s block = covariance window", xlabel="C/N₀ (dB-Hz)", ylabel="P(correct accepted | conditional)", ylim=(-.03, 1.03))
    ax.grid(True, alpha=.3); ax.legend(fontsize=8)

    ax = axes[1, 0]
    for label, name in [("B=.01 J16 ×8s", "g01_j16"), ("B=.01 J32 ×8s OU", "g01_j32"),
                        ("B=.01 J64 ×8s", "g01_j64"), ("B=.01 rate .036 J32 ×8s", "g01_slowrate"),
                        ("B=.1 J16 ×1s", "g1_j16"), ("B=.1 J32 ×1s", "g1_j32"),
                        ("B=.1 J64 ×1s", "g1_j64"), ("B=.1 coarse block 4s J32", "g1_coarse"),
                        ("B=.1 fine block .5s J32", "g1_fine")]:
        rec = reports.get(name)
        if rec:
            method = "gaussian-ou-ucb" if "OU" in label else "gaussian-window-ucb"
            add_curve(ax, rec, method, label)
    ax.set(title="Intermediate widths, rate and J screens", xlabel="C/N₀ (dB-Hz)", ylabel="P(correct accepted | conditional)", ylim=(-.03, 1.03))
    ax.grid(True, alpha=.3); ax.legend(fontsize=7, ncol=2)

    ax = axes[1, 1]
    for label, name in [("nominal delay 0 ms", "delay_fast_0"), ("nominal delay 6 ms", "delay_fast_6"),
                        ("nominal delay 7 ms", "delay_fast_7"), ("nominal delay 7 mismatch", "delay_fast_7_mismatch"),
                        ("aligned initial time 0 ms", "aligned_delay_fast_0_aligned"),
                        ("aligned initial time 6 ms", "aligned_delay_fast_6_aligned"),
                        ("aligned initial time 7 ms", "aligned_delay_fast_7_aligned"),
                        ("aligned timing mismatch", "aligned_delay_fast_7_mismatch_aligned")]:
        rec = reports.get(name) or aligned.get(name)
        if rec:
            add_curve(ax, rec, "static-fft-ucb", label)
    ax.set(title="Fast-delay cases; timing assumptions differ", xlabel="C/N₀ (dB-Hz)", ylabel="P(correct accepted | conditional)", ylim=(-.03, 1.03))
    ax.grid(True, alpha=.3); ax.legend(fontsize=8)

    ax = axes[2, 0]
    for label, name in [("day, center 1 Hz", "stable_day_1"), ("day, center 10 Hz", "stable_day_10"),
                        ("day, center 30 Hz", "stable_day_30"), ("long, center .005 Hz", "stable_long_0p005"),
                        ("long, center .05 Hz", "stable_long_0p05"), ("long, center .1 Hz", "stable_long_0p1")]:
        rec = reports.get(name)
        if rec:
            add_curve(ax, rec, "static-fft-ucb", label)
    ax.set(title="Stable-channel offsets (850-day / one-day requested; unpaired captures)", xlabel="C/N₀ (dB-Hz)", ylabel="P(correct accepted | conditional)", ylim=(-.03, 1.03))
    ax.grid(True, alpha=.3); ax.legend(fontsize=7, ncol=2)

    ax = axes[2, 1]
    ax.axis("off")
    ax.text(0, 1, "How to read these screens\n\n"
            "Each marker is correct admissions divided by conditional noise trials on the listed fixed physical paths.\n"
            "Aggregates are descriptive; channel-path counts are small and are not confidence-poolable.\n\n"
            "The information crossing calculations are necessary under the assumed known-noise covariance model.\n"
            "They are neither an achievable-receiver bound nor a correct-bit guarantee.\n\n"
            "Timing-aligned fast-delay rows set initial time to known truth. Padding and timing hypotheses differ,\n"
            "so they do not isolate the decoder's gain from the nominal-time rows.\n\n"
            "Window lengths, durations, rates and sample rates vary across several panels.",
            va="top", ha="left", wrap=True, fontsize=11)
    fig.suptitle("Triad Gaussian/delay screen — descriptive fixed-path conditional trials\nCurves use selected duration/window configuration; not channel-population confidence intervals", y=.995)
    fig.tight_layout(rect=[0, 0, 1, .975])
    fig.savefig(output, dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-png", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    here = Path(__file__).resolve().parent
    snapshot = (args.snapshot or repo / "local-evidence/triad-spread-snapshot").resolve()
    output_json = (args.output_json or here / "screen-summary.json").resolve()
    output_png = (args.output_png or here / "screen-curves.png").resolve()
    scores_dir = snapshot / "scores"
    reports = selected_summary(scores_dir, snapshot)
    aligned = all_summary(snapshot / "aligned-scores", snapshot, "aligned_")
    all_reports = {**reports, **aligned}
    plan_path = repo / "docs/validation-data/triad-v3/2026-11-02-spread/reproduction/screen-plan.json"
    aligned_plan = repo / "docs/validation-data/triad-v3/2026-11-02-spread/reproduction/aligned-export-plan.json"
    result = {
        "schema": "triad-spread-screen-summary-v1",
        "purpose": "Descriptive research summary from frozen simulated sampled-waveform and conditional-score exports.",
        "reproduction": {
            "command": "/tmp/triad-weak-env/bin/python docs/validation-data/triad-v3/2026-11-02-spread/summarize_screen.py",
            "snapshot_relative_path": str(snapshot.relative_to(repo)) if snapshot.is_relative_to(repo) else str(snapshot),
            "screen_plan_sha256": sha256(plan_path),
            "aligned_export_plan_sha256": sha256(aligned_plan),
            "input_files": [],
        },
        "screen_plan_cases": read_json(plan_path)["cases"],
        "interpretation": {
            "conditional_trials": "Trials resample noise conditional on each listed sampled physical path; totals across paths are descriptive only and no binomial confidence interval is pooled across paths.",
            "acceptance_probability": "correct_accepted / conditional noise trials, with duration and covariance-window choice identified in each source configuration.",
            "confidence_claims": "No channel-population confidence guarantee is made from these fixed-path conditional trials.",
            "timing_confound": "Aligned screen cases preserve campaign seed roots, sample rate and nominal channel parameters for same-rate/index pairs while forcing known initial time (GPS 0 ms, 0 ms region/offset, audio 0 ms). Timing information changes; capture padding/timeline can also change, so this is not a decoder-only paired gain. High-rate nominal-time screens have unsearched timing ambiguity and should not be read as Doppler-width limits. Low-rate nominal timing banks are an approximate search, not a fully blind exhaustive acquisition proof. Separately finer-Fs cases use different path roots.",
            "unknown_information": "The necessary information crossings are known-noise, specified covariance-model calculations; they are not receiver-achievable or correct-bit guarantees.",
        },
        "curves": all_reports,
        "information": information_summary(snapshot / "information"),
        "precision_parity": precision_parity(scores_dir),
        "limitations": [
            "Screen rates, durations, sampling rates, and window sizes vary across the B-width sweep; the plot is a feasibility screen, not a controlled rate/duration comparison.",
            "The OU and Gaussian-window likelihoods use distinct temporal models; observed differences are model-fit evidence, not a universal ranking.",
            "The path samples are few and fixed per condition. The many conditional noise trials do not increase the number of independent physical paths.",
            "The plotted 24-hour and 2.4-hour labels are requested active durations; actual simulated captures run longer due to waveform rounding, timing lead and retained tails. Stable long-duration controls are 850 days requested.",
            "High-rate original nominal-time rows have an unsearched timing dimension; aligned rows condition on known initial time and cannot be used as an apples-to-apples acquisition result.",
            "The nominal timing/receiver timing mismatch cohort is not an aligned waveform pair; acquisition-versus-channel loss cannot be isolated by direct subtraction.",
            "Recorded runtime values were collected under concurrent workload; no CPU/GPU speedup claim is made.",
        ],
        "key_observations": [
            "At B=0.001 Hz, 86,400 s, GUI inner rate 0.036 Hz and J=64, the fixed-path conditional score is 83/128 accepted at -28 dB-Hz and 128/128 at -26 dB-Hz; at -28, J=16 and J=32 Gaussian-window scores are 73/128 and 79/128 (OU J=32 is 78/128).",
            "At B=0.01 Hz, J=64 gives 64/128 at -24 dB-Hz; B=0.1 Hz gives 21/128 at -14 and 123/128 at -12. These are distinct sampled conditions with varying durations/rates.",
            "At B=0.001 Hz and -28 dB-Hz, assumed covariance width 0.5x and 2x yields 73/128 and 64/128 respectively, versus matched width 83/128; the small fixed path set does not establish robust mismatch tolerance.",
            "For B=0.1 Hz at -14 dB-Hz, J=32 with 4-second, 1-second and 0.5-second base blocks produced 21/128, 21/128 and 11/128. These use the same path seed fields but different block-statistic inputs; this is a paired-seed refinement screen, not identical observation-array parity.",
            "The B=0.01 Hz slow-rate screen scores 5/128 at -26 dB-Hz versus 2/128 for the higher-rate J=32 screen, but manifests show different key/channel seeds, so the small difference is not a paired rate effect.",
            "The static-FFT stable-day controls (24h, GUI rate .036 Hz) at 1/10/30 Hz center score 152/256, 134/256 and 134/256 at -35 dB-Hz, then 256/256 at -30; long-duration stable controls at .005/.05/.1 Hz center score 94/256, 118/256 and 121/256 at -63, then 256/256 at -60. These share duration/rate within cohort, but use distinct sample rates and unpaired physical path roots.",
            "For the high-width known-initial-time aligned screens, B=10 Hz gives 64/64 at +5 dB-Hz; B=30 Hz gives 64/64 at +15 dB-Hz (its finer-block cohort gives 63/64 at +10). These are conditional trials on four sampled paths and explicitly assume initial-time knowledge.",
            "For fast delay at -10 dB-Hz, nominal-time receiver conditions score 32/128 for modeled 0, 6 and 7 ms; the 7 ms path with receiver delay mismatch scores 8/128. Artificial known-initial-time aligned sets score 64/64 for 0, 6 and 7 ms; timing truth, sample timeline/padding and cohorts preclude interpreting this as pure acquisition gain.",
            "CPU FP32, CPU FP64 and CUDA FP32 matched-input window scores preserve ranking and gate decisions across 1,024 score rows per comparison; max absolute log-score delta is recorded below. These runs were under concurrent load; timing is not an isolated speed comparison.",
        ],
    }
    result["information_vs_observed"] = observed_information_comparison(reports, result["information"])
    result["matched_receiver_comparisons"] = [
        compare_path_identity(reports, "g001_j16", "g001_j32"),
        compare_path_identity(reports, "g001_j32", "g001_j64"),
        compare_path_identity(reports, "g001_j64", "g001_width_half"),
        compare_path_identity(reports, "g001_j64", "g001_width_double"),
        compare_path_identity(reports, "g01_j16", "g01_j32"),
        compare_path_identity(reports, "g01_j32", "g01_j64"),
        compare_path_identity(reports, "g1_j16", "g1_j32"),
        compare_path_identity(reports, "g1_j32", "g1_j64"),
        compare_path_identity(reports, "g1_j32", "g1_coarse"),
        compare_path_identity(reports, "g1_j32", "g1_fine"),
        compare_path_identity(reports, "g01_j32", "g01_slowrate"),
    ]
    aligned_pairs = []
    for left, right in [
        ("delay_fast_0", "aligned_delay_fast_0_aligned"),
        ("delay_fast_6", "aligned_delay_fast_6_aligned"),
        ("delay_fast_7", "aligned_delay_fast_7_aligned"),
        ("delay_fast_7_mismatch", "aligned_delay_fast_7_mismatch_aligned"),
        ("g10", "aligned_g10_aligned"), ("g30", "aligned_g30_aligned"),
    ]:
        aligned_pairs.append(compare_path_identity({**reports, **aligned}, left, right))
    result["nominal_vs_known_time_pairs"] = aligned_pairs
    case_by_name = {c["name"]: c for c in result["screen_plan_cases"]}
    result["stable_center_comparisons"] = {
        "day": {name: case_by_name[name] for name in ("stable-day-1", "stable-day-10", "stable-day-30")},
        "long": {name: case_by_name[name] for name in ("stable-long-0.005", "stable-long-0.05", "stable-long-0.1")},
        "pairing_scope": "Duration and pattern rate are matched within each set, but sample rate and physical seed roots differ across center offsets; compare curves as matched configuration screens, not as paired channel-realization differences.",
    }
    # Hash all source reports and NPZ files referenced by the output, plus frozen plans and information JSON.
    input_paths = {plan_path, aligned_plan}
    for r in all_reports.values():
        input_paths.add(snapshot / r["report"])
        for physical_path in r.get("sampled_physical_paths", []):
            if physical_path.get("manifest_relative_path"):
                input_paths.add(snapshot / physical_path["manifest_relative_path"])
        if r.get("scores_file"):
            p = snapshot / Path(r["report"]).parent / r["scores_file"]
            if p.is_file(): input_paths.add(p)
    input_paths.update((snapshot / "information").glob("*.json"))
    for name in ("g001-window-cpu", "g001-window-cuda0", "g001-window-cpu-fp64", "g001-cfo-cpu", "g001-cfo-cuda0"):
        for suffix in (".json", ".scores.npz"):
            p = scores_dir / f"{name}{suffix}"
            if p.is_file(): input_paths.add(p)
    result["reproduction"]["input_files"] = [
        {"path": str(p.relative_to(repo)) if p.is_relative_to(repo) else str(p), "sha256": sha256(p)}
        for p in sorted(input_paths) if p.is_file()
    ]
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_png.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    plot(reports, aligned, output_png)
    print(json.dumps({"json": str(output_json), "png": str(output_png), "screen_curves": len(reports),
                      "aligned_curves": len(aligned), "hashed_inputs": len(result["reproduction"]["input_files"])}, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Reclassify path-study score exports at alternate fixed admission gates.

This is an evidence summarizer, not a calibration or qualification campaign.
It preserves the report's confidence intervals for the originally reported
gate and marks alternate-gate counts as point estimates.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import numpy as np


REPORT_SCHEMA = "triad-path-study-report-v1"
OUTPUT_SCHEMA = "triad-path-study-analysis-v1"
STRICT_TWO_DURATION_DELTA = math.log(2.0)
RELAXED_DELTA = math.log(0.5) - math.log(100.0)  # original / 200


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def score_counts(scores: np.ndarray, truths: list[int], gate: float) -> dict[str, int]:
    values = np.asarray(scores, dtype=np.float64)
    # The saved columns are already the two bit hypotheses. The state-study
    # exporter deliberately preserves +/- infinity for likelihood extremes;
    # NaN has no meaningful admission/ranking semantics and is rejected.
    if values.ndim != 2 or values.shape[1] != 2:
        raise ValueError(f"signal/actual score matrix must be exactly N x 2, got {values.shape}")
    if np.isnan(values).any():
        raise ValueError("signal/actual score matrix contains NaN")
    if values.shape[0] != len(truths):
        raise ValueError(f"score/path count mismatch: {values.shape[0]} vs {len(truths)}")
    winners = np.argmax(values, axis=1)
    admitted = np.max(values, axis=1) >= gate
    truth = np.asarray(truths, dtype=np.int64)
    correct = int(np.count_nonzero(admitted & (winners == truth)))
    wrong = int(np.count_nonzero(admitted & (winners != truth)))
    return {"paths": len(truths), "accepted": correct + wrong,
            "correct_accepted": correct, "wrong_accepted": wrong,
            "missed": len(truths) - correct - wrong}


def null_count(scores: np.ndarray, expected_paths: int, gate: float) -> dict[str, int]:
    values = np.asarray(scores, dtype=np.float64)
    if values.ndim != 2 or values.shape != (expected_paths, 2):
        raise ValueError(f"null score matrix must be {expected_paths} x 2, got {values.shape}")
    if np.isnan(values).any():
        raise ValueError("null score matrix contains NaN")
    admitted = int(np.count_nonzero(np.max(values, axis=1) >= gate))
    return {"paths": expected_paths, "admitted": admitted}


def verify_operating_contract(config: dict[str, Any]) -> None:
    """The gate transformations below are valid only for the stated family."""
    expected = {
        "false_probability": 1e-4,
        "horizon_seconds": 86400.0,
        "attempt_spacing": 60.0,
        "duration_alternatives": 1,
        "selection_methods": 3,
    }
    for key, wanted in expected.items():
        got = config.get(key)
        if got is None or not math.isclose(float(got), wanted, rel_tol=0, abs_tol=1e-12):
            raise ValueError(f"unsupported {key}={got!r}; expected {wanted!r} for gate transforms")
    attempts = math.ceil(float(config["horizon_seconds"]) / float(config["attempt_spacing"]))
    if attempts != 1440:
        raise ValueError(f"expected 1440 attempts/day from configuration, got {attempts}")


def analyze_report(report_path: Path) -> dict[str, Any]:
    report = json.loads(report_path.read_text())
    if report.get("schema") != REPORT_SCHEMA:
        raise ValueError(f"{report_path}: unsupported schema {report.get('schema')!r}")
    config = report["configuration"]
    verify_operating_contract(config)
    score_path = report_path.with_suffix(".scores.npz")
    if not score_path.exists():
        raise FileNotFoundError(f"missing paired score export: {score_path}")
    with np.load(score_path, allow_pickle=False) as archive:
        score_arrays = {name: archive[name] for name in archive.files}

    cn0s = config.get("cn0")
    if not isinstance(cn0s, list):
        raise ValueError(f"{report_path}: configuration.cn0 must be a list")
    paths_count = int(report.get("independent_path_count", 0))
    if paths_count <= 0:
        raise ValueError(f"{report_path}: invalid independent_path_count={paths_count}")

    output_rows: list[dict[str, Any]] = []
    for source_row in report.get("rows", []):
        summary = source_row["summary"]
        method = str(summary["method"])
        cn0 = float(summary["cn0_db_hz"])
        matches = [i for i, value in enumerate(cn0s) if math.isclose(float(value), cn0, abs_tol=1e-9)]
        if len(matches) != 1:
            raise ValueError(f"{report_path}: cannot uniquely map C/N0 {cn0} to config.cn0")
        ci = matches[0]
        prefix = f"cn{ci}_{method}"
        signal_key, null_key = f"{prefix}_signal", f"{prefix}_null"
        actual_key = f"{prefix}_actual"
        if signal_key not in score_arrays or null_key not in score_arrays:
            raise ValueError(f"{report_path}: missing {signal_key} or {null_key} in score NPZ")
        path_rows = source_row.get("paths", [])
        if len(path_rows) != paths_count:
            raise ValueError(f"{report_path}: row has {len(path_rows)} paths, expected {paths_count}")
        receiver_diagnostics = source_row.get("receiver_diagnostics") or {}
        signal_diagnostics = receiver_diagnostics.get("signal") if isinstance(receiver_diagnostics, dict) else None
        receiver_model_fields = (
            "algorithm", "phase_states", "frequency_states", "frequency_grid_hz",
            "frequency_spacing_hz", "initial_frequency_weights", "initial_frequency_mean_hz",
            "initial_frequency_variance_hz2", "generator_stationary_frequency_variance_hz2",
            "generator_stationary_initial_correlation_decay_per_second", "quadrature_states",
            "frequency_model", "frequency_std_hz", "frequency_correlation_seconds",
            "random_walk_hz_per_sqrt_second", "phase_diffusion_rad_per_sqrt_second",
            "nominal_dt_seconds", "transition_order", "initial_phase_prior",
            "gain_prior_static_through_observation", "amplitude_nodes", "amplitude_weights_normalized",
            "relative_phase_states", "ray_amplitudes", "phase_kernel_tail_probability",
            "bulk_dtype", "log_evidence_dtype", "tf32_allowed",
        )
        receiver_model = ({key: signal_diagnostics.get(key) for key in receiver_model_fields
                           if key in signal_diagnostics}
                          if isinstance(signal_diagnostics, dict) else {})
        use_actual_sample = bool(config.get("actual_observation"))
        if use_actual_sample and any(item.get("actual_sampled_noise") is None for item in path_rows):
            raise ValueError(f"{report_path}: actual-observation profile lacks per-path actual scores")
        truths = [int(item["transmitted_bit"]) for item in path_rows]
        if any(bit not in (0, 1) for bit in truths):
            raise ValueError(f"{report_path}: path labels must be binary")
        if len(set(truths)) != 2:
            raise ValueError(f"{report_path}: path batch must include both bit labels")
        gate = float(summary["threshold"])
        gates = {
            "reported_strict_one_duration": gate,
            "strict_two_duration_family_point_estimate": gate + STRICT_TWO_DURATION_DELTA,
            "relaxed_90pct_1pct_per_12h_point_estimate": gate + RELAXED_DELTA,
        }
        classifications: dict[str, Any] = {}
        for name, threshold in gates.items():
            signal = score_counts(score_arrays[signal_key], truths, threshold)
            null = null_count(score_arrays[null_key], paths_count, threshold)
            result: dict[str, Any] = {
                "gate_score": threshold,
                "score_statistic": ("negative_log_Gamma_survival_probability"
                                    if method == "block-energy" else "log_likelihood_ratio"),
                "signal": signal,
                "null": null,
                "alternate_gate_confidence_interval": None,
            }
            if actual_key in score_arrays:
                actual = score_counts(score_arrays[actual_key], truths, threshold)
                result["actual_sampled_noise"] = actual
            else:
                result["actual_sampled_noise"] = None
            classifications[name] = result

        # Check that the retained path-level original-gate summary agrees with
        # the score export. This guards score ordering and truth-label mapping.
        calc = classifications["reported_strict_one_duration"]
        if (calc["signal"]["correct_accepted"] != int(summary["correct_accepted"])
                or calc["signal"]["wrong_accepted"] != int(summary["wrong_accepted"])
                or calc["signal"]["missed"] != int(summary["missed"])
                or calc["null"]["admitted"] != int(summary["false_admitted"])):
            raise ValueError(f"{report_path}: score-derived original-gate counts disagree with report row")

        # At the originally reported threshold the separate path summaries
        # preserve actual-observation counts, even when the NPZ lacks actual scores.
        path_actual = [item.get("actual_sampled_noise") for item in path_rows]
        if all(item is not None for item in path_actual):
            actual_gate_counts = {
                "paths": paths_count,
                "accepted": sum(int(x["correct_accepted"]) + int(x["wrong_accepted"]) for x in path_actual),
                "correct_accepted": sum(int(x["correct_accepted"]) for x in path_actual),
                "wrong_accepted": sum(int(x["wrong_accepted"]) for x in path_actual),
                "missed": sum(int(x["missed"]) for x in path_actual),
            }
            if actual_key in score_arrays:
                recalculated_actual = score_counts(score_arrays[actual_key], truths, gate)
                if recalculated_actual != actual_gate_counts:
                    raise ValueError(
                        f"{report_path}: actual NPZ original-gate counts disagree with path summaries"
                    )
            calc["actual_sampled_noise"] = actual_gate_counts

        output_rows.append({
            "method": method,
            "cn0_db_hz": cn0,
            "paths": paths_count,
            "balanced_signal_bits": summary.get("balanced_signal_bits"),
            "receiver_model_signature": receiver_model,
            "source_report_summary": summary,
            "classifications": classifications,
            "path_outcomes": [{
                "manifest_sha256": item["manifest_sha256"],
                "noise_seed": int(item["noise_seed"]),
                "transmitted_bit": int(item["transmitted_bit"]),
                "signal_source": ("actual_sampled_noise" if use_actual_sample else "projected_signal"),
                "correct_accepted": int((item["actual_sampled_noise"] if use_actual_sample else item["signal"])["correct_accepted"]),
                "wrong_accepted": int((item["actual_sampled_noise"] if use_actual_sample else item["signal"])["wrong_accepted"]),
                "missed": int((item["actual_sampled_noise"] if use_actual_sample else item["signal"])["missed"]),
                "null_admitted": bool(item["null_admitted"]),
            } for item in path_rows],
        })

    return {
        "source_report": report_path.name,
        "source_report_sha256": sha256(report_path),
        "source_scores": score_path.name,
        "source_scores_sha256": sha256(score_path),
        "split": config.get("split"),
        "configuration": {k: config.get(k) for k in (
            "cn0", "methods", "false_probability", "horizon_seconds",
            "attempt_spacing", "duration_alternatives", "selection_methods", "split",
            "actual_observation", "device", "precision", "coherences", "taps",
            "phase_states", "relative_phase_states", "frequency_bound", "frequency_step",
            "frequency_std", "frequency_coherence", "phase_diffusion")},
        "independent_path_count": paths_count,
        "path_manifest_sha256": report.get("independent_path_manifest_hashes"),
        "independent_noise_seeds": report.get("independent_noise_seeds"),
        "rows": output_rows,
    }


def independent_chernoff_interval(successes: int, total: int, alpha: float = .05) -> list[float]:
    """Two one-sided Chernoff bounds for independent, possibly unequal paths."""
    if not 0 <= successes <= total or total < 1:
        raise ValueError("invalid independent path count")
    observed, target = successes / total, -math.log(alpha) / total
    if successes == 0:
        return [0.0, -math.expm1(math.log(alpha) / total)]
    if successes == total:
        return [math.exp(math.log(alpha) / total), 1.0]

    def divergence(p: float) -> float:
        return observed * math.log(observed / p) + (1 - observed) * math.log((1 - observed) / (1 - p))

    lo, hi = 0.0, observed
    for _ in range(64):
        mid = (lo + hi) / 2
        if divergence(mid) > target:
            lo = mid
        else:
            hi = mid
    lower = lo
    lo, hi = observed, 1.0
    for _ in range(64):
        mid = (lo + hi) / 2
        if mid == 1.0 or divergence(mid) > target:
            hi = mid
        else:
            lo = mid
    return [lower, hi]


def pooled_plot_points(reports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Pool only matching profiles; deduplicate identities and verify overlaps."""
    pool: dict[str, dict[str, Any]] = {}
    profile_fields = (
        "precision", "coherences", "taps", "phase_states", "relative_phase_states",
        "frequency_bound", "frequency_step", "frequency_std", "frequency_coherence",
        "phase_diffusion", "actual_observation", "split", "duration_alternatives",
        "selection_methods", "false_probability", "horizon_seconds", "attempt_spacing",
    )
    for report in reports:
        source_name = str(report["source_report"]).lower()
        if source_name.startswith("holdout-hour-"):
            period = "1 h HF"
        elif source_name.startswith("holdout-two-day-"):
            period = "2 d HF"
        else:
            continue
        config = report["configuration"]
        profile = {field: config.get(field) for field in profile_fields}
        for row in report["rows"]:
            summary = row["source_report_summary"]
            key_object = {"period": period, "method": row["method"],
                          "cn0_db_hz": row["cn0_db_hz"], "gate": summary["threshold"],
                          "profile": profile,
                          "receiver_model_signature": row.get("receiver_model_signature", {})}
            key = json.dumps(key_object, sort_keys=True, separators=(",", ":"))
            group = pool.setdefault(key, {
                "period": period, "method": row["method"], "cn0_db_hz": row["cn0_db_hz"],
                "threshold": summary["threshold"], "profile": profile,
                "receiver_model_signature": row.get("receiver_model_signature", {}),
                "profile_id": key, "paths": {}, "source_reports": [],
            })
            source_ref = {"report": report["source_report"],
                          "report_sha256": report["source_report_sha256"],
                          "scores": report["source_scores"],
                          "scores_sha256": report["source_scores_sha256"],
                          "paths": len(row["path_outcomes"])}
            if source_ref not in group["source_reports"]:
                group["source_reports"].append(source_ref)
            for path in row["path_outcomes"]:
                identity = (path["manifest_sha256"], int(path["noise_seed"]))
                outcome = {field: path[field] for field in (
                    "transmitted_bit", "signal_source", "correct_accepted", "wrong_accepted", "missed", "null_admitted")}
                for old_identity in group["paths"]:
                    if old_identity[0] == identity[0] and old_identity[1] != identity[1]:
                        raise ValueError(f"same physical path appears under different noise seeds: {identity[0]}")
                    if old_identity[1] == identity[1] and old_identity[0] != identity[0]:
                        raise ValueError(f"same noise seed reused across physical paths: {identity[1]}")
                if identity in group["paths"]:
                    if group["paths"][identity] != outcome:
                        raise ValueError(
                            f"overlapping path identity has inconsistent results: {identity} in {group['source_reports']}"
                        )
                else:
                    group["paths"][identity] = outcome

    points = []
    for group in pool.values():
        path_values = list(group["paths"].values())
        n = len(path_values)
        correct = sum(x["correct_accepted"] for x in path_values)
        wrong = sum(x["wrong_accepted"] for x in path_values)
        missed = sum(x["missed"] for x in path_values)
        null = sum(int(x["null_admitted"]) for x in path_values)
        if correct + wrong + missed != n:
            raise ValueError("pooled path outcome counts do not sum to path count")
        group["pooled_counts"] = {"independent_unique_paths": n, "correct_accepted": correct,
                                  "wrong_accepted": wrong, "missed": missed,
                                  "null_admitted": null}
        group["correct_fraction"] = correct / n
        group["correct_pointwise_independent_chernoff95"] = independent_chernoff_interval(correct, n)
        group["pooling"] = "unique (manifest_sha256, noise_seed) identities; overlapping outcomes verified equal"
        group["overlap_paths_removed"] = sum(int(r["paths"]) for r in group["source_reports"]) - n
        points.append({k: v for k, v in group.items() if k != "paths"})
    points.sort(key=lambda x: (x["period"], x["cn0_db_hz"], x["method"], json.dumps(x["profile"], sort_keys=True)))
    return points


def plot_results(points: list[dict[str, Any]], output: Path) -> bool:
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return False
    families = [(label, [p for p in points if p["period"] == label]) for label in ("1 h HF", "2 d HF")]
    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharey=True)
    colors = {"gaussian": "#1f77b4", "phase-frequency": "#d62728", "block-energy": "#2ca02c"}
    markers = {"gaussian": "o", "phase-frequency": "s", "block-energy": "^"}
    for ax, (label, items) in zip(axes, families):
        if not items:
            ax.text(.5, .5, "No completed report available", ha="center", va="center", transform=ax.transAxes)
            ax.set_title(label)
            ax.set_axis_off()
            continue
        for point in items:
            y = point["correct_fraction"]
            ci = point["correct_pointwise_independent_chernoff95"]
            color = colors.get(point["method"], "#555555")
            lower, upper = max(0.0, y - float(ci[0])), max(0.0, float(ci[1]) - y)
            ax.errorbar([point["cn0_db_hz"]], [y], yerr=[[lower], [upper]],
                        fmt=markers.get(point["method"], "o"), color=color, capsize=3,
                        markerfacecolor="none" if point["profile"].get("actual_observation") else color)
        ax.set_title(label)
        ax.set_xlabel("C/N₀ (dB-Hz)")
        ax.grid(True, alpha=.25)
    axes[0].set_ylabel("Correct accepted fraction at reported strict gate")
    fig.suptitle("Independent sampled-path results; pointwise one-sided 95% bounds; counts and pooling in summary.json")
    handles = [plt.Line2D([], [], marker=markers[method], linestyle="", color=color, label=method)
               for method, color in colors.items()]
    fig.legend(handles=handles, loc="lower center", bbox_to_anchor=(.5, .015), ncol=3, fontsize=8)
    fig.text(.5, .075, "Color/marker identifies receiver method; hollow markers are actual sampled observations. Separate model profiles remain distinct in the JSON.",
             ha="center", fontsize=8)
    fig.tight_layout(rect=(0, .14, 1, .94))
    fig.savefig(output, dpi=160)
    plt.close(fig)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True, help="directory containing path-study JSON and score NPZ pairs")
    parser.add_argument("--output", type=Path, required=True, help="fresh output directory")
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit(f"output directory must be empty: {args.output}")
    args.output.mkdir(parents=True, exist_ok=True)
    reports: list[dict[str, Any]] = []
    skipped: list[dict[str, str]] = []
    for report_path in sorted(args.inputs.glob("*.json")):
        try:
            probe = json.loads(report_path.read_text())
        except Exception as exc:
            skipped.append({"file": report_path.name, "reason": f"invalid JSON: {exc}"})
            continue
        if not isinstance(probe, dict) or probe.get("schema") != REPORT_SCHEMA:
            schema = probe.get("schema") if isinstance(probe, dict) else type(probe).__name__
            skipped.append({"file": report_path.name, "reason": f"schema {schema!r} is not {REPORT_SCHEMA}"})
            continue
        reports.append(analyze_report(report_path))
    if not reports:
        raise SystemExit(f"no {REPORT_SCHEMA} reports found in {args.inputs}")
    plot_points = pooled_plot_points(reports)
    result = {
        "schema": OUTPUT_SCHEMA,
        "scope": "offline fixed-score reclassification; not operational qualification",
        "input_directory": str(args.inputs),
        "reports": reports,
        "skipped_json": skipped,
        "gate_definitions": {
            "reported_strict_one_duration": "source report threshold for its declared 1e-4/day, 60-second-attempt, 3-method family",
            "strict_two_duration_family_point_estimate": "source threshold + ln(2), assuming the same family with two duration alternatives",
            "relaxed_90pct_1pct_per_12h_point_estimate": "source threshold + ln(0.5) - ln(100) = source threshold - ln(200), same 60-second attempts and 3 methods",
        },
        "alternate_gate_intervals": None,
        "limitations": [
            "Alternate-gate classifications are fixed-sample point counts only; they do not establish the requested operational probabilities.",
            "Existing source confidence intervals apply only to the originally reported strict gate and are copied without reinterpretation.",
            "No interpolation between C/N0 points is made; plot panels are restricted to filename-labeled one-hour and two-day holdout families.",
        ],
        "plot_pooled_points": plot_points,
    }
    out_json = args.output / "summary.json"
    plotted = plot_results(plot_points, args.output / "reception.png")
    out_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    if plotted:
        print(f"wrote {out_json} and {args.output / 'reception.png'}")
    else:
        print(f"wrote {out_json}; matplotlib unavailable, plot omitted")


if __name__ == "__main__":
    main()

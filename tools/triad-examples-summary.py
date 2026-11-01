#!/usr/bin/env python3
"""Validate and summarize completed triad-examples-v2 JSONL studies."""

from __future__ import annotations

import argparse
import gzip
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


RECEIVERS = {"coherent", "differential", "amplitude-only"}
CONTROLS = {"signal", "null", "wrong-key", "wrong-epoch"}

# These values identify an experiment configuration. Realized timing and
# endpoint oscillator errors vary by seed, so they remain per-trial outcomes.
CONFIG_FIELDS = (
    "case", "control", "receiver", "now", "rate_hz", "added_symbols", "interleave", "encrypted",
    "bits_per_frame", "burst_wait_frames", "bits", "minimum_bit_energy_seconds",
    "payload_energy_seconds_per_bit", "frame_seconds", "dwells", "format_quiet_seconds",
    "format_ramp_seconds", "format_margin_seconds", "format_fake_fhss_envelope",
    "sample_rate", "fine_chip_rate_hz", "spacing_hz", "occupied_bandwidth_hz",
    "rf_hz", "target_cn0_db_hz", "physical_cn0_db_hz", "inband_snr_db", "channel_fhss_rf_retune",
    "fake_fcc_metadata_channels", "fake_fcc_channel_spacing_hz", "fake_fcc_first_center_hz",
    "fake_fcc_last_center_hz", "public_arrival_min_capture_seconds",
    "public_arrival_max_capture_seconds", "base_relative_bound_ppm", "rf_relative_bound_ppm",
    "if_hz", "search_frequency_half_width_hz", "search_frequency_step_hz",
    "search_minimum_score", "search_minimum_margin", "maximum_hypotheses",
    "search_clock_offsets_ppm", "epoch_candidates",
    "arithmetic_requested", "arithmetic_used", "epoch_offsets_seconds",
    "search_segment_seconds", "requested_threads", "workspace_limit_bytes",
    "dropout_ms", "dropout_start_ms", "dropout_every_n_dwells", "slow_agc",
    "noise_enabled", "signal_enabled", "channel_none", "spread_delay_ms", "spread_doppler_hz",
    "perturbation_slow_depth", "perturbation_fast_depth", "tx_phase_noise_rad_sqrt_second",
    "rx_phase_noise_rad_sqrt_second", "diagnostic_clean",
)

NUMERIC = {
    "trial", "seed", "seed_base", "stream_epoch", "receiver_local_epoch",
    "receiver_epoch_offset_seconds", "case", "expected_case_count", "trials_requested",
    "rate_hz", "added_symbols", "bits_per_frame", "burst_wait_frames", "bits",
    "ordinary_planned_symbol_seconds", "minimum_bit_energy_seconds", "payload_energy_seconds_per_bit",
    "frame_seconds", "dwells", "format_quiet_seconds", "format_ramp_seconds", "format_margin_seconds",
    "sample_rate", "fine_chip_rate_hz", "spacing_hz", "occupied_bandwidth_hz", "sample_count",
    "mean_power", "peak_power", "bw99_hz", "outside_ideal_fraction", "crest_db", "kurtosis",
    "tx_dbm_total", "path_loss_db", "noise_density_dbm_hz", "target_cn0_db_hz", "physical_cn0_db_hz",
    "rf_hz", "inband_snr_db", "fake_fcc_metadata_channels", "fake_fcc_channel_spacing_hz",
    "fake_fcc_first_center_hz", "fake_fcc_last_center_hz", "default_audio_lag_tx_ms",
    "default_audio_lag_rx_ms", "default_gps_error_tx_ms", "default_gps_error_rx_ms",
    "actual_arrival_seconds", "broad_clock_start_error_seconds", "audio_timing_difference_seconds",
    "gps_timing_difference_seconds", "capture_lead_seconds", "capture_trail_seconds",
    "public_arrival_min_capture_seconds", "public_arrival_max_capture_seconds", "base_relative_bound_ppm",
    "rf_relative_bound_ppm", "audio_clock_tx_ppm", "audio_clock_rx_ppm", "rf_tx_ppm", "rf_rx_ppm",
    "if_hz", "realized_cfo_bound_hz", "search_frequency_half_width_hz", "search_frequency_step_hz",
    "search_minimum_score", "search_minimum_margin", "maximum_hypotheses",
    "epoch_candidates", "search_segment_seconds", "requested_threads", "workspace_limit_bytes",
    "dropout_ms", "dropout_start_ms", "dropout_every_n_dwells", "spread_delay_ms", "spread_doppler_hz",
    "perturbation_slow_depth", "perturbation_fast_depth", "tx_phase_noise_rad_sqrt_second",
    "rx_phase_noise_rad_sqrt_second", "noise_power", "sample_noise_variance", "observed_power",
    "capture_samples", "signal_erased_samples", "bit_errors", "score", "margin", "frequency_hz",
    "timing_seconds", "clock_ppm", "epoch_offset_seconds", "coarse_hypotheses", "refined_hypotheses",
    "total_hypotheses", "frequency_banks", "fft_count", "projection_values", "working_bytes",
    "retained_samples", "threads_used", "cpu_seconds", "wall_seconds",
}

REQUIRED = set(CONFIG_FIELDS) | NUMERIC | {
    "schema", "trial", "seed", "seed_base", "stream_epoch", "receiver_local_epoch",
    "receiver_epoch_offset_seconds", "now", "requested_case", "expected_case_count",
    "trials_requested", "requested_receiver", "receiver", "source_bits", "capture_lead_seconds",
    "capture_trail_seconds", "audio_clock_tx_ppm", "audio_clock_rx_ppm", "rf_tx_ppm",
    "rf_rx_ppm", "default_audio_lag_tx_ms", "default_audio_lag_rx_ms", "default_gps_error_tx_ms",
    "default_gps_error_rx_ms", "actual_arrival_seconds", "broad_clock_start_error_seconds",
    "audio_timing_difference_seconds", "gps_timing_difference_seconds", "accepted",
    "complete_observation", "decode_ok", "false_accept", "bit_errors", "rx_bits", "score",
    "margin", "frequency_hz", "timing_seconds", "clock_ppm", "epoch_offset_seconds",
    "total_hypotheses", "coarse_hypotheses", "refined_hypotheses", "fine_likelihood_exhaustive",
    "frequency_banks", "fft_count", "projection_values", "working_bytes", "retained_samples",
    "threads_used", "cpu_seconds", "wall_seconds",
}

BOOLEANS = {
    "interleave", "encrypted", "format_fake_fhss_envelope", "channel_fhss_rf_retune", "now",
    "slow_agc", "noise_enabled", "signal_enabled", "channel_none", "diagnostic_clean", "accepted",
    "complete_observation", "decode_ok", "false_accept", "fine_likelihood_exhaustive",
}
INTEGER_FIELDS = {
    "trial", "seed", "seed_base", "stream_epoch", "receiver_local_epoch", "case",
    "receiver_epoch_offset_seconds", "expected_case_count", "trials_requested", "added_symbols",
    "bits_per_frame", "burst_wait_frames", "bits", "dwells", "sample_rate", "sample_count",
    "fake_fcc_metadata_channels", "epoch_candidates", "maximum_hypotheses", "requested_threads", "workspace_limit_bytes",
    "dropout_every_n_dwells", "capture_samples", "signal_erased_samples", "bit_errors",
    "total_hypotheses", "coarse_hypotheses", "refined_hypotheses", "frequency_banks",
    "fft_count", "projection_values", "working_bytes", "retained_samples", "threads_used",
}


def wilson(successes: int, trials: int) -> list[float] | None:
    if not trials:
        return None
    z = 1.959963984540054
    p = successes / trials
    d = 1 + z * z / trials
    centre = (p + z * z / (2 * trials)) / d
    radius = z * math.sqrt(p * (1 - p) / trials + z * z / (4 * trials * trials)) / d
    return [max(0.0, centre - radius), min(1.0, centre + radius)]


def _normalise(v: Any) -> Any:
    if isinstance(v, list):
        return tuple(_normalise(x) for x in v)
    if isinstance(v, dict):
        return tuple(sorted((k, _normalise(x)) for k, x in v.items()))
    return v


def _open(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open(encoding="utf-8")


def _check(path: Path, line: int, row: dict[str, Any]) -> None:
    # Earlier v2 pilot captures predate these emitted fields. Preserve the
    # defaults used when those captures were produced; current rows carry the
    # actual requested values.
    row.setdefault("search_minimum_score", 0.03)
    row.setdefault("search_minimum_margin", 0.005)
    row.setdefault("maximum_hypotheses", 2_000_000)
    row.setdefault("channel_none", False)
    row.setdefault("rf_hz", 30_000_000)
    missing = REQUIRED - row.keys()
    if missing:
        raise ValueError(f"{path}:{line}: missing fields: {', '.join(sorted(missing))}")
    if row["schema"] != "triad-examples-v2":
        raise ValueError(f"{path}:{line}: expected triad-examples-v2 schema")
    for field in NUMERIC:
        value = row[field]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f"{path}:{line}: {field} must be a finite number")
    for field in BOOLEANS:
        if not isinstance(row[field], bool):
            raise ValueError(f"{path}:{line}: {field} must be boolean")
    for field in INTEGER_FIELDS:
        if int(row[field]) != row[field]:
            raise ValueError(f"{path}:{line}: {field} must be an integer")
    clocks = row["search_clock_offsets_ppm"]
    if not isinstance(clocks, list) or not clocks or any(
        isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) for x in clocks
    ):
        raise ValueError(f"{path}:{line}: search_clock_offsets_ppm must be a nonempty finite-number array")
    if row["receiver"] not in RECEIVERS or row["requested_receiver"] not in RECEIVERS | {"all"}:
        raise ValueError(f"{path}:{line}: invalid receiver selection")
    if row["control"] not in CONTROLS:
        raise ValueError(f"{path}:{line}: invalid control class")
    if row["requested_case"] not in {"1", "2", "all"}:
        raise ValueError(f"{path}:{line}: invalid requested case")
    expected_cases = 2 if row["requested_case"] == "all" else 1
    if row["expected_case_count"] != expected_cases:
        raise ValueError(f"{path}:{line}: expected_case_count conflicts with requested_case")
    if not 0 <= row["trial"] < row["trials_requested"] or row["trials_requested"] < 1:
        raise ValueError(f"{path}:{line}: trial is outside its requested range")
    if row["case"] not in {1, 2} or (row["requested_case"] != "all" and row["case"] != int(row["requested_case"])):
        raise ValueError(f"{path}:{line}: emitted case conflicts with requested_case")
    if not 1e6 <= row["rf_hz"] <= 10e9 or (row["rf_hz"] != 30e6 and row["case"] != 2):
        raise ValueError(f"{path}:{line}: rf_hz is outside the supported case/frequency range")
    if row["requested_receiver"] != "all" and row["requested_receiver"] != row["receiver"]:
        raise ValueError(f"{path}:{line}: emitted receiver conflicts with requested_receiver")
    bits = row["bits"]
    source = row["source_bits"]
    decoded = row["rx_bits"]
    if bits < 1 or not isinstance(source, str) or len(source) != bits or any(x not in "01" for x in source):
        raise ValueError(f"{path}:{line}: malformed source bits")
    if not isinstance(decoded, str) or any(x not in "01" for x in decoded) or len(decoded) > bits:
        raise ValueError(f"{path}:{line}: malformed decoded bits")
    bit_errors = sum(a != b for a, b in zip(source, decoded)) + bits - len(decoded)
    if row["bit_errors"] != bit_errors:
        raise ValueError(f"{path}:{line}: bit_errors disagrees with source_bits and rx_bits")
    if row["decode_ok"] != (
        row["control"] == "signal" and row["accepted"] and row["complete_observation"] and source == decoded
    ):
        raise ValueError(f"{path}:{line}: decode_ok disagrees with the signal outcome")
    if row["false_accept"] != (row["control"] != "signal" and row["accepted"]):
        raise ValueError(f"{path}:{line}: false_accept disagrees with the control and accepted fields")
    if row["signal_enabled"] != (row["control"] != "null"):
        raise ValueError(f"{path}:{line}: signal_enabled disagrees with the null control")
    if row["noise_enabled"] == row["diagnostic_clean"]:
        raise ValueError(f"{path}:{line}: noise_enabled disagrees with diagnostic-clean mode")
    if row["threads_used"] < 0 or row["working_bytes"] < 0 or row["wall_seconds"] < 0 or row["cpu_seconds"] < 0:
        raise ValueError(f"{path}:{line}: negative resource measurement")
    if min(row["total_hypotheses"], row["frequency_banks"], row["fft_count"], row["projection_values"], row["retained_samples"]) < 0:
        raise ValueError(f"{path}:{line}: negative search count")


def _run_key(row: dict[str, Any]) -> tuple[Any, ...]:
    # Receiver is intentionally excluded: a single --receiver all run emits
    # one row for each mode over one capture. Config changes form separate runs.
    fields = (
        "seed_base", "requested_case", "expected_case_count", "trials_requested", "requested_receiver",
        "bits", "bits_per_frame", "burst_wait_frames", "control", "now", "diagnostic_clean",
        "arithmetic_requested", "search_segment_seconds", "search_frequency_step_hz",
        "requested_threads", "workspace_limit_bytes", "dropout_ms", "dropout_start_ms",
        "dropout_every_n_dwells", "slow_agc",
    )
    return tuple(_normalise(row[field]) for field in fields)


def _config_key(row: dict[str, Any]) -> tuple[Any, ...]:
    return tuple(_normalise(row[field]) for field in CONFIG_FIELDS)


def load(paths: list[Path]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    manifests: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    for path in paths:
        try:
            stream = _open(path)
        except OSError as exc:
            raise ValueError(f"cannot read {path}: {exc}") from exc
        count = 0
        with stream:
            for line_no, raw in enumerate(stream, 1):
                if not raw.strip():
                    continue
                try:
                    row = json.loads(raw)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"{path}:{line_no}: invalid JSON: {exc.msg}") from exc
                if not isinstance(row, dict):
                    raise ValueError(f"{path}:{line_no}: JSONL row must be an object")
                _check(path, line_no, row)
                expected_receivers = RECEIVERS if row["requested_receiver"] == "all" else {row["requested_receiver"]}
                if row["receiver"] not in expected_receivers:
                    raise ValueError(f"{path}:{line_no}: receiver is not in the requested receiver set")
                # Configurations may share a base seed for matched comparisons.
                # The signature includes actual format, channel, C/N0 and search
                # settings so only replayed receiver evaluations collide.
                identity = (_config_key(row), row["seed_base"], row["seed"], row["trial"],
                            row["stream_epoch"], row["receiver"])
                if identity in seen:
                    raise ValueError(f"{path}:{line_no}: duplicate receiver evaluation for the same capture/configuration")
                seen.add(identity)
                row["_run_key"] = _run_key(row)
                row["_config_key"] = _config_key(row)
                row["_source_path"] = str(path)
                rows.append(row)
                count += 1
        if not count:
            raise ValueError(f"{path}: no JSONL rows")
        manifests.append({"path": str(path), "rows": count})
    if not rows:
        raise ValueError("no rows supplied")
    validate_completeness(rows)
    return rows, manifests


def validate_completeness(rows: list[dict[str, Any]]) -> None:
    runs: dict[tuple[str, tuple[Any, ...]], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        runs[(row["_source_path"], row["_run_key"])].append(row)
    for _, runrows in runs.items():
        first = runrows[0]
        if first["requested_case"] == "all":
            cases = {1, 2}
        else:
            cases = {int(first["requested_case"])}
        receivers = RECEIVERS if first["requested_receiver"] == "all" else {first["requested_receiver"]}
        expected = {
            (case, trial, receiver)
            for case in cases
            for trial in range(first["trials_requested"])
            for receiver in receivers
        }
        observed: set[tuple[int, int, str]] = set()
        config_trials: dict[tuple[Any, ...], set[tuple[int, str]]] = defaultdict(set)
        for row in runrows:
            key = (row["case"], row["trial"], row["receiver"])
            observed.add(key)
            config_signature = tuple(
                _normalise(row[field]) for field in CONFIG_FIELDS if field != "receiver"
            )
            config_trials[config_signature].add((row["trial"], row["receiver"]))
        missing = expected - observed
        if missing:
            sample = sorted(missing)[:4]
            raise ValueError(f"incomplete requested run: {len(missing)} expected rows absent, examples {sample}")
        unexpected = observed - expected
        if unexpected:
            raise ValueError(f"run contains unexpected case/trial/receiver rows: {sorted(unexpected)[:4]}")
        expected_per_config = {
            (trial, receiver)
            for trial in range(first["trials_requested"])
            for receiver in receivers
        }
        for signature, actual in config_trials.items():
            if actual != expected_per_config:
                missing_rows = sorted(expected_per_config - actual)[:4]
                raise ValueError(
                    "incomplete configured trial set: receiver/configuration has "
                    f"{len(expected_per_config - actual)} expected rows absent, examples {missing_rows}"
                )


def summarize(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    buckets: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    # This carries the requested physical, format, acquisition, and resource
    # conditions. In particular, CPU measurements stay separate by thread and
    # arithmetic selection, while matched receivers use identical captures.
    for row in rows:
        key = _config_key(row) + (row["arithmetic_requested"], row["requested_threads"], row["workspace_limit_bytes"])
        buckets[key].append(row)
    groups = []
    for key, group in buckets.items():
        first = group[0]
        control = first["control"]
        signal = control == "signal"
        decoded = sum(row["decode_ok"] for row in group) if signal else 0
        accepted = sum(row["accepted"] for row in group) if signal else None
        wrong = sum(row["accepted"] and not row["decode_ok"] for row in group) if signal else None
        rejected = len(group) - accepted if signal else None
        false_accepts = sum(row["false_accept"] for row in group) if not signal else 0
        bit_errors = sum(row["bit_errors"] for row in group) if signal else None
        total_bits = sum(row["bits"] for row in group) if signal else None
        group_row = {
            "case": first["case"], "control": control, "receiver": first["receiver"],
            "now": first["now"],
            "rate_hz": first["rate_hz"], "added_symbols": first["added_symbols"],
            "interleave": first["interleave"], "channel_none": first["channel_none"],
            "dropout_ms": first["dropout_ms"],
            "slow_agc": first["slow_agc"], "rf_hz": first["rf_hz"],
            "physical_cn0_db_hz": first["physical_cn0_db_hz"],
            "inband_snr_db": first["inband_snr_db"], "diagnostic_clean": first["diagnostic_clean"],
            "search_segment_seconds": first["search_segment_seconds"],
            "search_frequency_half_width_hz": first["search_frequency_half_width_hz"],
            "search_frequency_step_hz": first["search_frequency_step_hz"],
            "search_minimum_score": first["search_minimum_score"],
            "search_minimum_margin": first["search_minimum_margin"],
            "maximum_hypotheses": first["maximum_hypotheses"],
            "search_clock_offsets_ppm": first["search_clock_offsets_ppm"],
            "arithmetic_requested": first["arithmetic_requested"], "arithmetic_used": first["arithmetic_used"],
            "requested_threads": first["requested_threads"], "threads_used_median": statistics.median(r["threads_used"] for r in group),
            "frame_seconds": first["frame_seconds"], "bits": first["bits"],
            "bits_per_frame": first["bits_per_frame"], "burst_wait_frames": first["burst_wait_frames"],
            "payload_energy_seconds_per_bit": first["payload_energy_seconds_per_bit"],
            "median_actual_arrival_seconds": statistics.median(r["actual_arrival_seconds"] for r in group),
            "trials": len(group), "captured_frames": len(group),
            "accepted_correct_signal_frames": decoded,
            "correct_frame_fraction": decoded / len(group) if signal else None,
            "correct_frame_wilson95": wilson(decoded, len(group)) if signal else None,
            "accepted_signal_frames": accepted,
            "accepted_wrong_frames": wrong,
            "accepted_wrong_frame_fraction": wrong / len(group) if signal else None,
            "accepted_wrong_frame_wilson95": wilson(wrong, len(group)) if signal else None,
            "rejected_signal_frames": rejected,
            "negative_control_false_accepts": false_accepts if not signal else None,
            "negative_control_false_accept_fraction": false_accepts / len(group) if not signal else None,
            "negative_control_false_accept_wilson95": wilson(false_accepts, len(group)) if not signal else None,
            "raw_bit_errors": bit_errors, "raw_bits_scored": total_bits,
            "raw_ber": bit_errors / total_bits if signal and total_bits else None,
            "accepted_wrong_signal_frames": sum(r["accepted"] and not r["decode_ok"] for r in group) if signal else None,
            "median_cpu_seconds": statistics.median(r["cpu_seconds"] for r in group),
            "median_wall_seconds": statistics.median(r["wall_seconds"] for r in group),
            "max_working_bytes": max(r["working_bytes"] for r in group),
            "median_total_hypotheses": statistics.median(r["total_hypotheses"] for r in group),
            "median_frequency_banks": statistics.median(r["frequency_banks"] for r in group),
            "median_fft_count": statistics.median(r["fft_count"] for r in group),
            "median_projection_values": statistics.median(r["projection_values"] for r in group),
            "median_retained_samples": statistics.median(r["retained_samples"] for r in group),
            "stream_epoch": first["stream_epoch"], "seed_base": first["seed_base"],
        }
        groups.append(group_row)
    return sorted(groups, key=lambda x: (
        x["case"], x["control"], x["physical_cn0_db_hz"], x["receiver"],
        x["rate_hz"], x["search_segment_seconds"], x["requested_threads"], x["seed_base"],
    ))


def _percent(value: float | None) -> str:
    return "n/a" if value is None else f"{100 * value:.1f}%"


def _rf(value: float) -> str:
    if value >= 1e9:
        return f"{value / 1e9:g} GHz"
    return f"{value / 1e6:g} MHz"


def markdown(groups: list[dict[str, Any]], manifests: list[dict[str, Any]]) -> str:
    lines = [
        "| Case | Control | Receiver | Clock-now | RF | Physical C/N₀ | In-band SNR | Configured rate / payload bit/s | Added/interleave/channel-none | Search segment / frequency / gates / cap | Threads | Correct accepted frames (Wilson 95%) | Wrong accepted / rejected (Wilson 95%) | False accepts (Wilson 95%) | Raw errors / bits | Median CPU / wall s | Median hypotheses | Max workspace MiB |",
        "|---:|---|---|---:|---:|---:|---:|---:|---|---|---:|---|---|---|---:|---:|---:|---:|",
    ]
    for r in groups:
        correct = "n/a"
        if r["control"] == "signal":
            correct = f"{r['accepted_correct_signal_frames']}/{r['captured_frames']} ({_percent(r['correct_frame_fraction'])})"
            interval = r["correct_frame_wilson95"]
            if interval:
                correct += f" [{_percent(interval[0])}–{_percent(interval[1])}]"
        false = "n/a"
        if r["control"] != "signal":
            false = f"{r['negative_control_false_accepts']}/{r['captured_frames']} ({_percent(r['negative_control_false_accept_fraction'])})"
            interval = r["negative_control_false_accept_wilson95"]
            if interval:
                false += f" [{_percent(interval[0])}–{_percent(interval[1])}]"
        wrong = "n/a"
        if r["control"] == "signal":
            wrong = f"{r['accepted_wrong_frames']}/{r['captured_frames']} / {r['rejected_signal_frames']}"
            interval = r["accepted_wrong_frame_wilson95"]
            if interval:
                wrong += f" [{_percent(interval[0])}–{_percent(interval[1])}]"
        lines.append(
            f"| {r['case']} | {r['control']} | {r['receiver']} / {r['arithmetic_used']} | {r['now']} | "
            f"{_rf(r['rf_hz'])} | {r['physical_cn0_db_hz']:.3f} dB-Hz | {r['inband_snr_db']:.3f} dB | "
            f"{r['rate_hz']:g} / {r['bits_per_frame'] / (r['frame_seconds'] * (r['burst_wait_frames'] + 1)):.6g} | "
            f"{r['added_symbols']} / {r['interleave']} / {r['channel_none']} | "
            f"{r['search_segment_seconds']:g} s / ±{r['search_frequency_half_width_hz']:g} Hz / "
            f"{r['search_minimum_score']:g},{r['search_minimum_margin']:g} / {r['maximum_hypotheses']:g} | "
            f"{r['requested_threads']} requested, {r['threads_used_median']:g} used | "
            f"{correct} | {wrong} | {false} | "
            f"{r['raw_bit_errors'] if r['raw_bit_errors'] is not None else 'n/a'} / "
            f"{r['raw_bits_scored'] if r['raw_bits_scored'] is not None else 'n/a'} | "
            f"{r['median_cpu_seconds']:.4g} / {r['median_wall_seconds']:.4g} | "
            f"{r['median_total_hypotheses']:g} | {r['max_working_bytes'] / (1024 * 1024):.3g} |"
        )
    lines.extend([
        "",
        "Wilson intervals use captured trial frames. Raw BER is reported as exact counts; bits within a frame are correlated, so no binomial BER interval is attached.",
        "False accepts are measured only on null, wrong-key, and wrong-epoch controls. Control output bits are not treated as payload-bit comparisons.",
        "These empirical gates and intervals do not calibrate a global false-alarm probability.",
        "",
        "Inputs: " + ", ".join(f"{m['path']} ({m['rows']} rows)" for m in manifests),
    ])
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("paths", nargs="+", type=Path, help="triad-examples-v2 JSONL files, optionally gzip compressed")
    args = parser.parse_args(argv)
    try:
        rows, manifests = load(args.paths)
        groups = summarize(rows)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"triad-examples-summary: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"schema": 1, "runs": manifests, "groups": groups}, indent=2, sort_keys=True))
    else:
        print(markdown(groups, manifests), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

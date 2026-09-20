#!/usr/bin/env python3
"""Summarize exact-known-symbol final margin trials without audio access.

Run from any directory; writes JSON to stdout. The CSV input comes from the
archived diagnostic and frozen long-preamble receiver. No third-party modules.
"""
import csv
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent


def summarize(rows):
    signal = sum(row["signal_energy"] for row in rows)
    error = sum(row["error_energy"] for row in rows)
    symbols = sum(row["symbols"] for row in rows)
    bits = len(rows) * 2048
    common_error = sum(
        row["signal_energy"] * (
            row["gain"] ** 2 + 1 - 2 * row["gain"] * math.cos(row["phase"])
        )
        for row in rows
    )
    estimated_variance = sum(row["variance"] for row in rows) / len(rows)
    actual_variance = error / symbols
    return {
        "first_interval": int(rows[0]["interval"]),
        "intervals": len(rows),
        "whole_coding_cycles": len(rows) / 127,
        "raw_errors": int(sum(row["raw_errors"] for row in rows)),
        "compared_bits": bits,
        "raw_ber": sum(row["raw_errors"] for row in rows) / bits,
        "rms_evm": math.sqrt(error / signal),
        "signal_to_error_db": 10 * math.log10(signal / error),
        "gmi_at_unit_llr_scale_per_payload_symbol": (
            bits - sum(row["bit_log_loss_nats"] for row in rows) / math.log(2)
        ) / symbols,
        "common_interval_gain_phase_fraction_of_error": common_error / error,
        "maximum_interval_mean_phase_rad": max(abs(row["phase"]) for row in rows),
        "maximum_interval_evm": max(row["evm"] for row in rows),
        "initial_clock_ppm": rows[0]["clock_ppm"],
        "minimum_clock_ppm": min(row["clock_ppm"] for row in rows),
        "maximum_clock_ppm": max(row["clock_ppm"] for row in rows),
        "mean_demapper_variance": estimated_variance,
        "actual_symbol_error_variance": actual_variance,
        "demapper_variance_to_actual_db": 10 * math.log10(estimated_variance / actual_variance),
    }


results = {}
for csv_path in sorted(HERE.glob("4m-r*-a*-100k-known-symbols.csv")):
    stem = csv_path.name.removesuffix("-known-symbols.csv")
    with csv_path.open() as source:
        rows = [{key: float(value) for key, value in row.items()}
                for row in csv.DictReader(source)]
    trial = json.loads((HERE / "live-runs" / ("v3-" + stem + ".json")).read_text())
    rate = trial["code_rate"]
    k_per_frame = 58320 if "r910" in stem else 57600
    aggregate = summarize(rows)
    aggregate["amplitude_times_rms_evm"] = trial["amplitude"] * aggregate["rms_evm"]
    results[stem] = {
        "live_result_file": "live-runs/v3-" + stem + ".json",
        "amplitude": trial["amplitude"],
        "code_rate": rate,
        "live_exact_file": bool(trial["exact"]),
        "live_ldpc_frames": trial["ldpc_frames"],
        "live_ldpc_failed_frames": trial["ldpc_failed_frames"],
        "actual_ldpc_input_bits_per_payload_symbol": 4 * k_per_frame / (127 * 94),
        "nominal_conservative_load_bits_per_payload_symbol": 2048 / 94 * rate,
        "aggregate": aggregate,
        "cycles": [summarize(rows[start:start + 127])
                   for start in range(0, len(rows), 127)],
    }

print(json.dumps(results, indent=2))

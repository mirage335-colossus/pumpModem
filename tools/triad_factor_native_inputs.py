#!/usr/bin/env python3
"""Generate bounded, reproducible native inputs for the triad factor census.

This performs 232 short ``triad_long --mode configure`` calls (no waveforms).
It writes geometry.json, validation.json, near-zero.json, and summary.json to
a new output directory, binding every receipt to the current source and binary.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import itertools
import json
import math
import os
import random
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / "build/dev-cli/triad_long"
SOURCE_PATHS = (
    "include/datapump/clock_sync.hpp", "src/triad_modem_v3.cpp",
    "src/tuning_presets.cpp", "tools/triad-long.cpp",
)
RATES = (.001, .01, .1, 1, 3.6, 10, 36, 100, 360, 1200, 2400, 3600,
         12000, 18000, 24000, 1e6, 30e6)
ADDED = (1, 10, 100, 1000)
BASE = (("crystal", 100), ("gpsdo-xo", .0001), ("gpsdo-tcxo", .0001),
        ("gpsdo-ocxo", .0001))
RF = (("crystal", 100), ("cb-ssb", 40), ("ic-7100", 1), ("gpsdo-xo", .0001),
      ("gpsdo-tcxo", .0001), ("gpsdo-ocxo", .0001))
SHIFTS = (500000, 1e6, 3.5e6, 7e6, 10e6, 14e6, 30e6, 5.8e9)
SPREAD = tuple(zip(("none", "sub9-best", "sub9-moderate", "sub9-worst", "hf-best",
                    "hf-moderate", "hf-worst", "aurora-best", "aurora-worst", "eme-focused"),
                   (0, .001, .005, .05, .1, 10, 30, 10, 55, 6)))
SELECTORS = ("off", *(f"{kind}-{depth}" for kind in ("slow", "fast", "each")
                         for depth in (1, 5, 10, 25, 65)))
SOURCE_DIRS = tuple(ROOT / p for p in SOURCE_PATHS)


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def capture_margin(fs: float, bw: float, if_hz: float, rf_hz: float,
                   base_ppm: float, shift_ppm: float, doppler: float,
                   margin: float, selective_active: bool) -> float:
    rx = 1 - base_ppm * .5e-6
    tx = 1 + base_ppm * .5e-6
    cfo = (rf_hz * shift_ppm + if_hz * base_ppm) * 1e-6 / rx
    spread = doppler * margin * (2 if selective_active else 1)
    return fs * rx - bw * tx - 2 * (cfo + spread)


def invoke(binary: Path, root: Path, args: list[str], deadline: float) -> tuple[dict[str, Any], dict[str, Any] | None]:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("native factor-input generation exceeded its total time budget")
    argv = [str(binary), *args]
    proc = subprocess.run(argv, cwd=root, capture_output=True, text=True,
                          timeout=min(5.0, remaining), check=False)
    receipt = {
        "argv": argv, "returncode": proc.returncode, "stdout": proc.stdout,
        "stderr": proc.stderr,
        "stdout_sha256": hashlib.sha256(proc.stdout.encode()).hexdigest(),
    }
    try:
        decoded = json.loads(proc.stdout) if proc.stdout else None
    except json.JSONDecodeError as exc:
        raise ValueError(f"native configure emitted invalid JSON: {argv}: {exc}") from exc
    if proc.returncode == 0 and (not isinstance(decoded, dict) or "configuration" not in decoded):
        raise ValueError("native configure exited zero without a configuration object")
    return receipt, decoded["configuration"] if proc.returncode == 0 else None


def _write(output: Path, name: str, value: Any) -> dict[str, Any]:
    path = output / name
    payload = json.dumps(value, indent=2, sort_keys=True) + "\n"
    with path.open("x", encoding="utf-8") as dst:
        dst.write(payload)
    return {"path": str(path.resolve()), "sha256": sha(path), "bytes": path.stat().st_size}


def generate(output: Path, binary: Path = DEFAULT_BINARY, root: Path = ROOT,
             max_seconds: float = 120.0) -> dict[str, Any]:
    root, binary, output = root.resolve(), binary.resolve(), output.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise FileNotFoundError(f"compatible executable triad_long not found/executable: {binary}")
    if not all((root / name).is_file() for name in SOURCE_PATHS):
        raise FileNotFoundError(f"triad source tree is incomplete under --root {root}")
    if max_seconds <= 0:
        raise ValueError("--max-seconds must be positive")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise FileExistsError(f"output directory is not empty; preserving existing evidence: {output}")
    started = time.monotonic()
    deadline = started + max_seconds
    source_before = {name: sha(root / name) for name in SOURCE_PATHS}
    binary_before = sha(binary)

    geometry: list[dict[str, Any]] = []
    errors: collections.Counter[str] = collections.Counter()
    for rate, added, fhss in itertools.product(RATES, ADDED, (False, True)):
        argv = ["--mode", "configure", "--seconds", "8", "--rate", str(rate),
                "--added", str(added), "--base-oscillator", "gpsdo-ocxo",
                "--shift-oscillator", "gpsdo-ocxo", "--rf-hz", "500000",
                "--nominal-if-hz", "1725", "--clock-sync",
                "GPS_10ms-1ms_region-0ms_offset", "--spread-preset", "none",
                "--selective-perturbation", "off"]
        if fhss:
            argv.append("--fhss")
        receipt, config = invoke(binary, root, argv, deadline)
        geometry.append({"rate_hz": rate, "added": added, "fhss": fhss,
                         "receipt": receipt, "configuration": config})
        if config is None:
            errors[receipt["stderr"].strip()] += 1
    geometry_artifact = _write(output, "geometry.json", geometry)

    capture_counts: collections.Counter[str] = collections.Counter()
    by_margin: collections.Counter[tuple[int, str]] = collections.Counter()
    near_zero: list[dict[str, Any]] = []
    for row in geometry:
        config = row["configuration"]
        if config is None:
            continue
        capture = config["capture"]
        fs, bw = capture["sample_rate_hz"], capture["nominal_triplet_bandwidth_hz"]
        for if_hz, shift, (base_id, base_ppm), (shift_id, shift_ppm), (spread_id, doppler), margin, active in itertools.product(
                (1725, row["rate_hz"] / 2, 8270), SHIFTS, BASE, RF, SPREAD, (1, 3), (False, True)):
            value = capture_margin(fs, bw, if_hz, shift, base_ppm, shift_ppm, doppler, margin, active)
            outcome = "positive" if value > 0 else "nonpositive"
            capture_counts[outcome] += 15 if active else 1
            by_margin[(margin, outcome)] += 15 if active else 1
            if abs(value) < .01:
                near_zero.append({
                    "rate_hz": row["rate_hz"], "added": row["added"], "fhss": row["fhss"],
                    "if_hz": if_hz, "shift_hz": shift, "base": base_id, "rf": shift_id,
                    "spread": spread_id, "margin": margin, "selective_active": active,
                    "capture_margin_hz": value,
                })
    near_artifact = _write(output, "near-zero.json", near_zero)

    rng = random.Random(20261104)
    validation: list[dict[str, Any]] = []
    max_delta = 0.0
    valid = 0
    sign_mismatches = 0
    for index in range(96):
        rate, added, fhss = rng.choice(RATES), rng.choice(ADDED), rng.choice((False, True))
        (base_id, base_ppm), (shift_id, shift_ppm) = rng.choice(BASE), rng.choice(RF)
        spread_id, doppler = rng.choice(SPREAD)
        selector, margin = rng.choice(SELECTORS), rng.choice((1, 3))
        if_hz = rng.choice((1725, rate / 2, 8270))
        shift = rng.choice(SHIFTS)
        default_timing, burst = rng.choice((False, True)), rng.choice(("off", "3"))
        if index < 6:
            rate, added, fhss = 1200, 1, False
            base_id, base_ppm = BASE[1 + index % 3]
            shift_id, shift_ppm = RF[0]
            spread_id, doppler, selector, margin, shift = "aurora-worst", 55, "off", 1, 500000
            if_hz = (1725, 600, 8270)[index % 3]
        argv = ["--mode", "configure", "--seconds", "8", "--rate", str(rate),
                "--added", str(added), "--base-oscillator", base_id,
                "--shift-oscillator", shift_id, "--rf-hz", str(shift),
                "--nominal-if-hz", str(if_hz), "--margin", str(margin),
                "--spread-preset", spread_id, "--selective-perturbation", selector,
                "--symbol-burst", burst, "--cn0", rng.choice(("-60", "200", "6"))]
        if default_timing:
            argv += ["--clock-sync", "default", "--arrival-prior-seconds", "-7.035", "7.035"]
        else:
            argv += ["--clock-sync", "GPS_10ms-1ms_region-0ms_offset", "--audio-error-ms", "70"]
        if fhss:
            argv.append("--fhss")
        receipt, config = invoke(binary, root, argv, deadline)
        item: dict[str, Any] = {"index": index, "near_zero_directed": index < 6,
                                "receipt": receipt, "configuration": config}
        if config is not None:
            capture = config["capture"]
            predicted = capture_margin(
                capture["sample_rate_hz"], capture["nominal_triplet_bandwidth_hz"],
                if_hz, shift, base_ppm, shift_ppm, doppler, margin, selector != "off")
            actual = capture["deterministic_nyquist_margin_hz"]
            matched = (predicted > 0) == config["capture"]["feasible_for_deterministic_prior"]
            item.update(predicted_capture_margin_hz=predicted,
                        absolute_delta_hz=abs(actual - predicted), sign_matches=matched)
            max_delta = max(max_delta, abs(actual - predicted))
            sign_mismatches += not matched
            valid += 1
        validation.append(item)
    validation_artifact = _write(output, "validation.json", validation)

    replication = len(("tight", "default")) * 19 * len(("off", "3"))
    total = math.prod((17, 3, 9, 2, 4, 7, 10, 16, 19, 2, 4, 2, 2))
    supported = total * 48 // 63
    per_geometry = 3 * 8 * 4 * 6 * 10 * 2 * 16 * replication
    geometry_valid = sum(row["configuration"] is not None for row in geometry)
    source_after = {name: sha(root / name) for name in SOURCE_PATHS}
    binary_after = sha(binary)
    unchanged = source_before == source_after and binary_before == binary_after
    summary = {
        "schema": "triad-native-config-factorization-diagnostic-v1",
        "scope": "source geometry and deterministic capture predicates only; no reception or channel-stream qualification",
        "assumptions": {
            "requested_seconds": 8, "automatic_sample_rate": True,
            "internal_burst_wait_tiles": 0, "public_frequency_grid": [0, 0],
            "truth_frequency_hz": 0, "truth_frequency_drift": 0,
            "diurnal_phase_radians": 0, "gaussian_doppler_width_hz": 0,
            "whole_bit_burst_choices": ["off", "3"],
            "timing_choices": ["tight", "explicit-uniform +/-7.035s"],
        },
        "primary_partition": {
            "unsupported_topology": total - supported,
            "source_geometry_rejected": (len(geometry) - geometry_valid) * per_geometry,
            "capture_margin_nonpositive": capture_counts["nonpositive"] * replication,
            "capture_margin_positive": capture_counts["positive"] * replication,
        },
        "total_intent_count": total, "supported_topology_count": supported,
        "geometry_table_count": len(geometry), "geometry_valid_count": geometry_valid,
        "geometry_error_counts": dict(errors), "near_zero_factor_rows": len(near_zero),
        "replication_factor": replication,
        "validation": {
            "seed": 20261104, "queries": len(validation), "directed_near_zero_queries": 6,
            "valid_compared": valid, "source_rejected": len(validation) - valid,
            "maximum_margin_delta_hz": max_delta, "capture_sign_mismatches": sign_mismatches,
        },
        "artifacts": [geometry_artifact, near_artifact, validation_artifact],
        "binary_path": str(binary), "binary_sha256_before": binary_before,
        "binary_sha256_after": binary_after,
        "source_sha256_before": source_before, "source_sha256_after": source_after,
        "source_and_binary_unchanged": unchanged,
        "script_sha256": sha(Path(__file__).resolve()),
        "elapsed_seconds": time.monotonic() - started,
    }
    _write(output, "summary.json", summary)
    if not unchanged:
        raise RuntimeError("source or binary changed during native input generation")
    if sum(summary["primary_partition"].values()) != total:
        raise AssertionError("generated factor-input partition does not sum to the intent total")
    if (valid, sign_mismatches) != (62, 0) or max_delta > 2e-12:
        raise AssertionError("independent native capture validation did not match expected boundary checks")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="current source checkout root")
    parser.add_argument("--binary", type=Path, default=Path(os.environ.get("TRIAD_LONG_BINARY", DEFAULT_BINARY)),
                        help="compatible built triad_long executable (or env TRIAD_LONG_BINARY)")
    parser.add_argument("--output", type=Path, required=True, help="new empty output directory")
    parser.add_argument("--max-seconds", type=float, default=120.0,
                        help="overall wall-time bound for all 232 native configure calls")
    options = parser.parse_args(argv)
    try:
        report = generate(options.output, options.binary, options.root, options.max_seconds)
    except (OSError, ValueError, TimeoutError, RuntimeError, AssertionError, subprocess.SubprocessError) as exc:
        print(f"triad-factor-native-inputs: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({"output": str(options.output.resolve()),
                      "elapsed_seconds": report["elapsed_seconds"],
                      "intent_count": report["total_intent_count"],
                      "geometry_rows": report["geometry_table_count"],
                      "validation": report["validation"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

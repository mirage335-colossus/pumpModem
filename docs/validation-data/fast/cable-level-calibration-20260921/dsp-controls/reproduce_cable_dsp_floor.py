#!/usr/bin/env python3
"""Reconstructed reproduction of the 2026-09-21 noiseless DSP controls.

Requires Python 3 and NumPy, plus this repository's built fast_cable_probe.
These are reconstructed commands with settings pinned from the saved probe
reports, not a shell-history transcript. Original baud arguments retained:
17647.0588235294, 1764.70588, and 176.470588.

Reading existing controls (no probe execution):
  PYTHON /tmp/reproduce_cable_dsp_floor.py --repo REPO --offline-dir /tmp \
      --live-dir /tmp/cable-level-20260921 --output /tmp/dsp-floor-recomputed.json

Print the reconstructed probe commands only:
  PYTHON /tmp/reproduce_cable_dsp_floor.py --repo REPO \
      --offline-dir /tmp/new-floor-controls --print-commands

Regenerate in a fresh directory, then analyze (CPU only, never audio):
  PYTHON /tmp/reproduce_cable_dsp_floor.py --repo REPO \
      --offline-dir /tmp/new-floor-controls --run-offline \
      --live-dir /tmp/cable-level-20260921 --output /tmp/new-floor-summary.json

Every launched probe explicitly has --offline. No hardware capture, playback,
mixer operation, S16 conversion, external noise or clock mismatch is included.
The live files must be the original archived .json/.bits/.iq observations.
The mid/narrow source fixtures match exactly; the wide source/length differ.
"""
import argparse
import hashlib
import importlib.util
import json
import shlex
import subprocess
from pathlib import Path

import numpy as np


CONTROLS = (("wide", "17647.0588235294", 64),
            ("mid", "1764.70588", 64),
            ("narrow", "176.470588", 16))


def command(repo, prefix, baud, count):
    # Pin all signal-relevant settings so subsequent preset defaults cannot
    # silently alter the control. Raw mode does not apply the inner/outer code.
    return [str(repo / "build/fast_cable_probe"), "--offline", "--profile", "wire",
            "--capacity", "--single-carrier", "--mode", "raw", "--qam", "4194304",
            "--code-rate", "8/9", "--depth", "4", "--rs", "0.3%",
            "--marker-spacing", "16", "--pilot-spacing", "256",
            "--sample-rate", "48000", "--carrier", "9300", "--rolloff", ".02",
            "--amplitude", ".3", "--symbol-rate", baud, "--intervals", str(count),
            "--seed", "417", "--pre", "1", "--tail", "9", "--quiet",
            "--tx-bits-save", str(prefix) + ".bits",
            "--rx-symbols-save", str(prefix) + ".iq"]


def load_trial(helper, prefix):
    report_path = Path(str(prefix) + ".json")
    bits_path = Path(str(prefix) + ".bits")
    iq_path = Path(str(prefix) + ".iq")
    report = json.loads(report_path.read_text())
    bits = np.fromfile(bits_path, dtype=np.uint8)
    iq = np.fromfile(iq_path, dtype="<c8").astype(np.complex128)
    result, _ = helper.analyze(report, bits, iq)
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest()
              for name, path in (("probe", report_path), ("bits", bits_path), ("iq", iq_path))}
    return report, bits, iq, result, hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--offline-dir", type=Path, default=Path("/tmp"))
    parser.add_argument("--live-dir", type=Path, default=Path("/tmp/cable-level-20260921"))
    parser.add_argument("--run-offline", action="store_true")
    parser.add_argument("--print-commands", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    if args.print_commands:
        for band, baud, count in CONTROLS:
            prefix = args.offline_dir / ("cable-dsp-floor-" + band)
            print(shlex.join(command(repo, prefix, baud, count)))
        return
    if args.run_offline:
        # Do not overwrite original evidence or reuse partially generated files.
        prefixes = [args.offline_dir / ("cable-dsp-floor-" + band) for band, _, _ in CONTROLS]
        for prefix in prefixes:
            if any(Path(str(prefix) + suffix).exists() for suffix in (".json", ".bits", ".iq", ".log")):
                parser.error("--run-offline requires fresh destination filenames")
        args.offline_dir.mkdir(parents=True, exist_ok=True)
        for (band, baud, count), prefix in zip(CONTROLS, prefixes):
            with Path(str(prefix) + ".json").open("w") as stdout, Path(str(prefix) + ".log").open("w") as stderr:
                subprocess.run(command(repo, prefix, baud, count), check=True, stdout=stdout, stderr=stderr)

    spec = importlib.util.spec_from_file_location("known_evm", repo / "tools/fast_known_evm.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    measurements = []
    for band, _, _ in CONTROLS:
        offline_prefix = args.offline_dir / ("cable-dsp-floor-" + band)
        live_prefix = args.live_dir / (band + "-a030")
        off_report, off_bits, off_iq, offline, off_hashes = load_trial(helper, offline_prefix)
        live_report, live_bits, live_iq, live, live_hashes = load_trial(helper, live_prefix)
        if not off_report.get("offline") or live_report.get("offline"):
            parser.error("Expected an explicitly offline control and a live cable comparison")
        off_db = offline["summary"]["known_signal_to_residual_db"]
        live_db = live["summary"]["known_signal_to_residual_db"]
        same = np.array_equal(off_bits, live_bits)
        item = dict(band=band, baud=off_report["symbol_rate"], intervals=off_report["tx_intervals"],
                    offline_known_residual_db=off_db, live_known_residual_db=live_db,
                    offline_known_evm=offline["summary"]["known_evm"],
                    offline_raw_bit_errors=off_report["raw_wrong_bits"],
                    offline_known_payload_bit_errors=offline["summary"]["known_payload_bit_errors"],
                    wall_seconds=off_report["wall_seconds"],
                    illustrative_offline_to_live_error_power_ratio=10 ** ((live_db - off_db) / 10),
                    identical_tx_bits=same,
                    input_sha256=dict(offline=off_hashes, live=live_hashes))
        if same:
            if off_report["apsk"] != live_report["apsk"] or off_iq.size != live_iq.size:
                parser.error("Identical source fixture has different constellation or observation geometry")
            expected = helper.expected_symbols(off_bits, off_report["apsk"]).ravel()
            e_off, e_live = off_iq - expected, live_iq - expected
            off_energy = np.vdot(e_off, e_off).real
            live_energy = np.vdot(e_live, e_live).real
            cross = np.vdot(e_off, e_live)
            coefficient = cross / off_energy
            delta = e_live - e_off
            item.update(known_error_squared_coherence=float(abs(cross) ** 2 / (off_energy * live_energy)),
                        known_error_projection_gain_real=float(coefficient.real),
                        known_error_projection_gain_imag=float(coefficient.imag),
                        live_minus_noiseless_error_ratio_db=float(10 * np.log10(
                            np.vdot(expected, expected).real / np.vdot(delta, delta).real)))
        measurements.append(item)
    result = dict(
        reproduction="Reconstructed pinned commands, not a shell-history transcript; calculations use original full-precision live reports.",
        conditions="Direct float32 noiseless TX-to-RX excludes S16 conversion, hardware filtering, sample-clock mismatch and external noise. Error-power ratios are indicative, not a calibrated independent-noise decomposition. Wideband uses a shorter, different raw fixture than its live codec run.",
        interpretation="Squared coherence describes projection of the aligned known-error vectors onto each other; it does not establish a general hardware SNR. Subtracting the aligned offline residual is a diagnostic using known transmitted data, not a receiver correction.",
        measurements=measurements)
    text = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()

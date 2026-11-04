#!/usr/bin/env python3
"""Focused contract checks for bounded, random-access triad source windows."""
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile


BINARY = sys.argv[1]
BASE = ["--rate", "36", "--added", "10", "--sample-rate", "960",
        "--seconds", "8", "--no-encrypt", "--seed", "71231",
        "--epoch", "1800000000", "--phase-diffusion", "0"]


def invoke(*args, ok=True):
    result = subprocess.run([BINARY, *args], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=45)
    if ok and result.returncode:
        raise AssertionError(f"command failed: {result.args}\n{result.stderr[-2000:]}")
    if not ok and result.returncode == 0:
        raise AssertionError(f"invalid command unexpectedly succeeded: {result.args}")
    return result


def read_c64(path):
    raw = path.read_bytes()
    if len(raw) % 8:
        raise AssertionError(f"partial complex64 sample in {path}")
    return list(struct.iter_unpack("=ff", raw))


def run_window(path, *extra):
    invoke("--mode", "export-source-window", *BASE, "--source-window-seconds", "1.25",
           "--signal-bit", "1", "--output", str(path), "--chunk", "37", *extra)
    return json.loads((path / "manifest.json").read_text())


def test_matches_full_export_and_chunking(root):
    full = root / "full"
    window = root / "window"
    alternate = root / "alternate"
    invoke("--mode", "export", *BASE, "--channel", "none", "--output", str(full),
           "--chunk", "257", "--signal-bit", "1")
    manifest = run_window(window)
    source = json.loads((full / "manifest.json").read_text())
    first = manifest["first_sample"]
    rows = manifest["window_samples"]
    assert manifest["format"] == "triad-source-window-v1"
    assert manifest["source_only"] and not manifest["channel_applied"]
    assert manifest["full_source_samples"] == source["source_samples"]
    assert manifest["minimum_pattern_cells"] >= 64
    assert manifest["source_fraction"] < 1
    # export's physical capture starts with its 50-ms lead; at this 960-Hz
    # fixture that offset is an exact integer number of samples.
    full_first = first + round(source["truth_arrival_seconds"] * source["sample_rate"])
    full_signal = read_c64(full / "signal.c64")[full_first:full_first + rows]
    window_signal = read_c64(window / "signal.c64")
    assert len(full_signal) == len(window_signal)
    for actual, expected in zip(window_signal, full_signal):
        assert math.isclose(actual[0], expected[0], rel_tol=2e-6, abs_tol=2e-7)
        assert math.isclose(actual[1], expected[1], rel_tol=2e-6, abs_tol=2e-7)
    candidates = read_c64(window / "source_candidates.c64")
    assert len(candidates) == 2 * rows
    assert candidates[rows:2 * rows] == read_c64(window / "signal.c64")

    invoke("--mode", "export-source-window", *BASE, "--source-window-seconds", "1.25",
           "--output", str(alternate), "--chunk", "1024", "--signal-bit", "1", "--source-first-sample", str(first))
    assert (alternate / "signal.c64").read_bytes() == (window / "signal.c64").read_bytes()
    assert (alternate / "source_candidates.c64").read_bytes() == (window / "source_candidates.c64").read_bytes()


def test_encryption_epoch_and_bit_are_deterministic(root):
    one = root / "enc1"
    repeat = root / "enc2"
    changed = root / "enc3"
    base = ["--rate", "36", "--added", "10", "--sample-rate", "512", "--seconds", "8",
            "--seed", "331", "--key-seed", "991", "--epoch", "1800000000",
            "--source-window-seconds", "1.0", "--signal-bit", "1"]
    invoke("--mode", "export-source-window", *base, "--output", str(one))
    invoke("--mode", "export-source-window", *base, "--output", str(repeat), "--chunk", "17")
    assert (one / "signal.c64").read_bytes() == (repeat / "signal.c64").read_bytes()
    m = json.loads((one / "manifest.json").read_text())
    assert m["encrypted"] is True and m["signal_bit"] == 1 and m["candidate_bits"] == [0, 1]
    assert read_c64(one / "source_candidates.c64")[m["window_samples"]:] == read_c64(one / "signal.c64")
    changed_base = list(base)
    changed_base[changed_base.index("1800000000")] = "1800000030"
    invoke("--mode", "export-source-window", *changed_base, "--output", str(changed))
    assert (one / "signal.c64").read_bytes() != (changed / "signal.c64").read_bytes()


def test_low_rate_partial_calendar_and_center(root):
    out = root / "low"
    invoke("--mode", "export-source-window", "--rate", "0.001", "--added", "10",
           "--sample-rate", "960", "--seconds", "8", "--source-window-seconds", "16",
           "--source-window-center", "--seed", "5", "--no-encrypt", "--output", str(out),
           "--chunk", "1024")
    m = json.loads((out / "manifest.json").read_text())
    assert m["full_calendar_seconds"] > 86400
    assert m["source_fraction"] < 0.001
    assert m["first_sample"] > m["active_payload_first_sample"]
    assert m["window_samples"] == 15360


def test_bounds_and_source_only_contract(root):
    base = ["--mode", "export-source-window", *BASE, "--source-window-seconds", "1",
            "--output", str(root / "bad")]
    invoke(*base, "--source-first-sample", "999999999", ok=False)
    invoke(*base, "--source-first-sample", "1", "--source-window-center", ok=False)
    invoke(*base, "--spread-doppler-hz", "0.1", ok=False)
    invoke(*base, "--adc-bits", "12", ok=False)
    invoke(*base, "--control", "wrong-key", ok=False)
    invoke(*base, "--source-window-seconds", "nan", ok=False)

    disk = ["--mode", "export-source-window", "--rate", "36", "--added", "10",
            "--sample-rate", "10000", "--seconds", "8", "--source-window-seconds", "8",
            "--export-max-mib", "1", "--output", str(root / "disk-limit")]
    invoke(*disk, ok=False)
    assert not (root / "disk-limit" / "signal.c64.partial").exists()

    partial = root / "partial-preserve"
    partial.mkdir()
    sentinel = partial / "signal.c64.partial"
    sentinel.write_bytes(b"keep-existing-partial")
    invoke("--mode", "export-source-window", *BASE, "--source-window-seconds", "1",
           "--output", str(partial), ok=False)
    assert sentinel.read_bytes() == b"keep-existing-partial"
    assert not (partial / "source_candidates.c64.partial").exists()


def test_workspace_budget_with_sequential_sources(root):
    out = root / "workspace-budget"
    # At this Added Symbols count, one source cache fits the 16-MiB budget but
    # two simultaneous caches do not. The signal source must be destroyed
    # before either bit-candidate source is constructed.
    invoke("--mode", "export-source-window", "--rate", "36", "--added", "1000",
           "--seconds", "8", "--source-window-seconds", "0.1", "--chunk", "1024",
           "--memory-mib", "16", "--no-encrypt", "--output", str(out))
    m = json.loads((out / "manifest.json").read_text())
    assert m["source_candidates_shape"][0] == 2


def main():
    with tempfile.TemporaryDirectory(prefix="triad-source-window-") as scratch:
        root = pathlib.Path(scratch)
        test_matches_full_export_and_chunking(root)
        test_encryption_epoch_and_bit_are_deterministic(root)
        test_low_rate_partial_calendar_and_center(root)
        test_bounds_and_source_only_contract(root)
        test_workspace_budget_with_sequential_sources(root)
    print("triad source-window CLI regression passed")


if __name__ == "__main__":
    main()

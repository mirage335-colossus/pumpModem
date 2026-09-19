#!/usr/bin/env python3
"""Sampled encrypted-file regressions for the complete fast SNR matrix."""

import csv
import io
import subprocess
import sys


def run(tool, *arguments):
    completed = subprocess.run(
        [tool, *arguments], check=True, capture_output=True, text=True, timeout=300
    )
    return list(csv.DictReader(io.StringIO(completed.stdout)))


def main(tool):
    for profile in ("wire", "ssb", "fm", "acoustic"):
        rows = run(tool, "--profile", profile, "--bytes", "1024", "--seed", "417")
        assert len(rows) == 23, (profile, "missing SNR levels", len(rows))
        assert [float(row["snr_db"]) for row in rows] == list(range(10, 121, 5))
        for row in rows:
            assert row["profile"] == profile and row["source_bytes"] == "1024"
            assert row["seed"] == "417"
            if row["complete"] == "1":
                assert row["exact"] == "1" and row["physical_end"] == "1", row
            if row["exact"] == "1":
                assert row["complete"] == "1" and row["acquired"] == "1", row
            if float(row["snr_db"]) >= 15 or profile in ("fm", "acoustic"):
                assert row["exact"] == "1", row
        print(f"{profile}: all 23 SNR levels checked", flush=True)

    # Profile selection must not reset explicitly selected local geometry.
    geometry = ["--apsk", "64", "--depth", "3", "--code-rate", "7/8"]
    fixture = ["--bytes", "0", "--snr", "60", "--seed", "417", "--require-success"]
    before = run(tool, *geometry, "--profile", "fm", *fixture)
    after = run(tool, "--profile", "fm", *geometry, *fixture)
    assert len(before) == len(after) == 1
    for field in ("profile", "apsk", "code_rate", "tx_intervals", "rx_intervals", "signal_power", "exact"):
        assert before[0][field] == after[0][field], (field, before, after)
    assert before[0]["apsk"] == "64" and before[0]["code_rate"] == "0.875"

    invalid = (
        ("--depth", "65"), ("--depth", "4294967297"),
        ("--apsk", "4294967300"), ("--apsk", "16junk"),
        ("--bytes", "-1"), ("--bytes", "67108865"), ("--bytes", "1.5"),
        ("--seed", "18446744073709551616"), ("--seed", "417junk"),
        ("--snr", "nan"), ("--snr", "inf"), ("--snr", "60junk"),
        ("--snr", "9.9"), ("--snr", "120.1"),
    )
    for arguments in invalid:
        rejected = subprocess.run([tool, *arguments], capture_output=True, text=True, timeout=10)
        assert rejected.returncode != 0, ("invalid arguments accepted", arguments)
    print("Fast 92-case SNR matrix and bounded argument checks passed", flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_fast_snr.py /path/to/fast_regression")
    main(sys.argv[1])

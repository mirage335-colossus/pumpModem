#!/usr/bin/env python3
"""Check calibration CLI parsing and cheap plans without running captures."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time


TIMEOUT_SECONDS = 10
PLAN_MARKER = "worker partitions 1..16 and both supported shaped shard plans preserve all fixed seeds"
OPEN_ERROR = "could not open required shaped calibration result"
FATAL_MARKERS = ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:", "SUMMARY:")


def cases(directory):
    """Return the finite direct-main CLI cases; result files remain absent."""
    missing = [str(directory / f"missing-result-{index}.txt") for index in range(5)]
    assert all(not Path(path).exists() for path in missing)
    return [
        {"name": "combined-plan-checks", "args": ["--check-worker-plan", "--check-shard-plan"],
         "returncode": 0, "contains": PLAN_MARKER},
        {"name": "legacy-two-path-aggregate", "args": ["--aggregate-shaped", *missing[:2]],
         "returncode": 1, "contains": OPEN_ERROR},
        {"name": "four-path-count-before", "args": ["--shaped-shards", "4", "--aggregate-shaped", *missing[:4]],
         "returncode": 1, "contains": OPEN_ERROR},
        {"name": "four-path-count-after", "args": ["--aggregate-shaped", *missing[:4], "--shaped-shards", "4"],
         "returncode": 1, "contains": OPEN_ERROR},
        {"name": "missing-fourth-path", "args": ["--shaped-shards", "4", "--aggregate-shaped", *missing[:3]],
         "returncode": 1, "contains": "requires exactly the selected number of result paths"},
        {"name": "extra-fifth-path", "args": ["--aggregate-shaped", *missing, "--shaped-shards", "4"],
         "returncode": 1, "contains": "Usage: test_differential_receiver_probability"},
        {"name": "unsupported-three-path-count", "args": ["--aggregate-shaped", *missing[:3], "--shaped-shards", "3"],
         "returncode": 1, "contains": "calibration --shaped-shards must be 2 or 4"},
        {"name": "duplicate-count-option", "args": ["--shaped-shards", "4", "--aggregate-shaped", *missing[:4], "--shaped-shards", "4"],
         "returncode": 1, "contains": "Usage: test_differential_receiver_probability"},
        {"name": "capture-option-before-plan-check", "args": [
             "--section", "shaped", "--shaped-shard", "3", "--shaped-result", str(directory / "never-written.txt"),
             "--check-shard-plan", "--shaped-shards", "4"],
         "returncode": 0, "contains": "shaped shard 3/4"},
    ]


def invoke(binary, report_path):
    binary = Path(binary).resolve()
    report_path = Path(report_path)
    report = {"schema": 1, "binary": str(binary), "binary_sha256": None,
              "timeout_seconds": TIMEOUT_SECONDS,
              "qualification": "CLI parsing and no-capture plan checks only; no sampled calibration qualification",
              "passed": False, "cases": [], "error": None}
    try:
        if not binary.is_file():
            raise FileNotFoundError(f"calibration test binary does not exist: {binary}")
        digest = hashlib.sha256()
        with binary.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        report["binary_sha256"] = digest.hexdigest()
        with tempfile.TemporaryDirectory(prefix="calibration-cli-") as temporary:
            for case in cases(Path(temporary)):
                command = [str(binary), *case["args"]]
                started = time.monotonic()
                try:
                    completed = subprocess.run(command, capture_output=True, timeout=TIMEOUT_SECONDS)
                    duration = time.monotonic() - started
                    stdout = completed.stdout.decode("utf-8", errors="replace")
                    stderr = completed.stderr.decode("utf-8", errors="replace")
                    combined = stdout + stderr
                    expected = (completed.returncode == case["returncode"] and case["contains"] in combined
                                and not any(marker in combined for marker in FATAL_MARKERS))
                    # A negative parser case is valid only when main reaches the
                    # intended missing-file check. Exit 1 is required above.
                    record = {"name": case["name"], "command": command,
                              "returncode": completed.returncode, "stdout": stdout, "stderr": stderr,
                              "duration_seconds": duration, "expected": expected,
                              "expectation": {"returncode": case["returncode"], "contains": case["contains"]}}
                except subprocess.TimeoutExpired as error:
                    record = {"name": case["name"], "command": command, "returncode": None,
                              "stdout": _decode(error.stdout), "stderr": _decode(error.stderr),
                              "duration_seconds": time.monotonic() - started, "expected": False,
                              "error": f"timed out after {TIMEOUT_SECONDS}s",
                              "expectation": {"returncode": case["returncode"], "contains": case["contains"]}}
                except OSError as error:
                    record = {"name": case["name"], "command": command, "returncode": None,
                              "stdout": "", "stderr": "", "duration_seconds": time.monotonic() - started,
                              "expected": False, "error": str(error),
                              "expectation": {"returncode": case["returncode"], "contains": case["contains"]}}
                report["cases"].append(record)
            report["passed"] = len(report["cases"]) == 9 and all(item["expected"] for item in report["cases"])
    except (OSError, AssertionError) as error:
        report["error"] = str(error)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def _decode(value):
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="built test_differential_receiver_probability executable")
    parser.add_argument("--report", required=True, help="JSON report destination")
    args = parser.parse_args(argv)
    result = invoke(args.binary, args.report)
    print(json.dumps({"passed": result["passed"], "binary_sha256": result["binary_sha256"],
                      "executed": len(result["cases"]), "report": str(Path(args.report).resolve()),
                      "error": result["error"]}, sort_keys=True))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

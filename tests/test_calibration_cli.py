"""Unit tests for the bounded calibration CLI checker; no C++ captures run."""

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_calibration_cli", ROOT / "tools/check-calibration-cli.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


FAKE_BINARY = r'''#!/usr/bin/env python3
import sys

args = sys.argv[1:]
if "--check-worker-plan" in args or "--check-shard-plan" in args:
    if "--check-worker-plan" in args and "--check-shard-plan" in args:
        print("worker partitions 1..16 and both supported shaped shard plans preserve all fixed seeds")
    else:
        print("shaped shard 3/4")
    raise SystemExit(0)

if "--aggregate-shaped" in args:
    start = args.index("--aggregate-shaped") + 1
    count = 0
    while start + count < len(args) and not args[start + count].startswith("--"):
        count += 1
    if "--shaped-shards" in args:
        index = args.index("--shaped-shards")
        if index + 1 < len(args) and args[index + 1] == "3":
            print("differential receiver probability tests failed: calibration --shaped-shards must be 2 or 4", file=sys.stderr)
            raise SystemExit(1)
        if args.count("--shaped-shards") > 1:
            print("Usage: test_differential_receiver_probability", file=sys.stderr)
            raise SystemExit(1)
        selected = int(args[index + 1]) if index + 1 < len(args) else 2
    else:
        selected = 2
    if count > 4:
        print("Usage: test_differential_receiver_probability", file=sys.stderr)
    elif count != selected:
        print("differential receiver probability tests failed: calibration --aggregate-shaped requires exactly the selected number of result paths", file=sys.stderr)
    else:
        print("differential receiver probability tests failed: could not open required shaped calibration result", file=sys.stderr)
    raise SystemExit(1)

print("Usage: test_differential_receiver_probability", file=sys.stderr)
raise SystemExit(1)
'''


class CalibrationCliChecker(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="test-calibration-cli-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.binary = self.directory / "fake-calibration-test"
        self.binary.write_text(FAKE_BINARY, encoding="utf-8")
        self.binary.chmod(0o755)
        self.report_path = self.directory / "report.json"

    def run_checker(self, binary=None):
        return checker.invoke(binary or self.binary, self.report_path)

    def test_direct_cli_case_loop_passes_all_cases_and_records_identity(self):
        result = self.run_checker()
        self.assertTrue(result["passed"])
        self.assertEqual(len(result["cases"]), 9)
        self.assertTrue(all(case["expected"] for case in result["cases"]))
        self.assertEqual(len(result["binary_sha256"]), 64)
        self.assertIn("no sampled calibration qualification", result["qualification"])
        self.assertEqual(json.loads(self.report_path.read_text()) ["passed"], True)

    def test_usage_error_does_not_masquerade_as_expected_missing_result(self):
        real_run = subprocess.run

        def wrong_negative(command, *args, **kwargs):
            if "--aggregate-shaped" in command and "--check-worker-plan" not in command:
                return subprocess.CompletedProcess(command, 1, b"", b"Usage: test_differential_receiver_probability")
            return real_run(command, *args, **kwargs)

        with patch.object(checker.subprocess, "run", side_effect=wrong_negative):
            result = self.run_checker()
        self.assertFalse(result["passed"])
        self.assertFalse(result["cases"][1]["expected"])

    def test_timeout_and_signal_never_count_as_expected_failures(self):
        real_run = subprocess.run

        def timeout_first(command, *args, **kwargs):
            if "--check-worker-plan" in command:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"], output=b"partial")
            return real_run(command, *args, **kwargs)

        with patch.object(checker.subprocess, "run", side_effect=timeout_first):
            timed = self.run_checker()
        self.assertFalse(timed["passed"])
        self.assertIn("timed out", timed["cases"][0]["error"])

        def signal_first(command, *args, **kwargs):
            if "--check-worker-plan" in command:
                return subprocess.CompletedProcess(command, -11, b"", b"")
            return real_run(command, *args, **kwargs)

        with patch.object(checker.subprocess, "run", side_effect=signal_first):
            crashed = self.run_checker()
        self.assertFalse(crashed["passed"])
        self.assertEqual(crashed["cases"][0]["returncode"], -11)

    def test_missing_binary_is_reported_and_fails_without_skipped_cases(self):
        result = self.run_checker(self.directory / "absent")
        self.assertFalse(result["passed"])
        self.assertEqual(result["cases"], [])
        self.assertIn("does not exist", result["error"])
        self.assertIsNone(result["binary_sha256"])


if __name__ == "__main__":
    unittest.main()

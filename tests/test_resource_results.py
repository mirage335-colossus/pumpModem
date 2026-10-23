#!/usr/bin/env python3
"""CLI acceptance tests for the latest resource-results metadata helper."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "resource-results.py"
PYTHON = sys.executable


def record(source="a" * 40, runner="localrunner", provenance=None, metrics=None):
    return {
        "source": source,
        "configuration": {
            "compiler": "clang 19",
            "build_type": "Release",
            "sanitizers": "none",
            "runner": runner,
            "fixture": "receiver-smoke-v1",
        },
        "provenance": provenance or {"local_command": "./build.sh test contract"},
        "metrics": metrics or {
            "wall": {"value": 12.5, "unit": "wall_seconds", "status": "passed"},
            "cpu": {"value": 11.75, "unit": "process_cpu_seconds", "status": "passed"},
            "workspace": {"value": None, "unit": "workspace_bytes", "status": "unavailable"},
        },
    }


class ResourceResultsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="resource-results-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "latest.json"

    def run_cli(self, *args):
        return subprocess.run([PYTHON, "-B", str(TOOL), *map(str, args)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, check=False)

    def update(self, profile, value):
        return self.run_cli("update", self.path, "--profile", profile,
                            "--input", json.dumps(value, separators=(",", ":"), allow_nan=True))

    def test_local_and_ci_profiles_round_trip_and_replace(self):
        local = record()
        ci = record(source="b" * 64, runner="ubuntu-hosted",
                    provenance={"run_id": "actions-123", "report_reference": "job/7/report"},
                    metrics={"cpu": {"value": 23.125, "unit": "process_cpu_seconds", "status": "warning"},
                             "wall": {"value": 25.5, "unit": "wall_seconds", "status": "passed"}})
        self.assertEqual(self.update("local", local).returncode, 0)
        self.assertEqual(self.update("linux-ci", ci).returncode, 0)
        self.assertEqual(self.run_cli("check", self.path).returncode, 0)

        replacement = record(source="c" * 40, metrics={
            "wall": {"value": 9.0, "unit": "wall_seconds", "status": "passed"},
        })
        self.assertEqual(self.update("local", replacement).returncode, 0)
        document = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(set(document["profiles"]), {"local", "linux-ci"})
        self.assertEqual(document["profiles"]["local"]["source"], "c" * 40)
        self.assertEqual(document["profiles"]["linux-ci"]["metrics"]["cpu"]["value"], 23.125)
        self.assertLessEqual(self.path.stat().st_size, 32 * 1024)

    def test_rejects_invalid_units_source_status_value_nan_and_duplicate_keys(self):
        cases = []
        item = record(); item["metrics"]["wall"]["unit"] = "milliseconds"; cases.append(item)
        item = record(source="abc"); cases.append(item)
        item = record(); item["metrics"]["wall"]["status"] = "maybe"; cases.append(item)
        item = record(); item["metrics"]["wall"]["status"] = "omitted"; cases.append(item)
        item = record(); item["metrics"]["wall"]["value"] = True; cases.append(item)
        item = record(); item["metrics"]["wall"]["value"] = float("nan"); cases.append(item)
        for invalid in cases:
            with self.subTest(invalid=invalid):
                self.assertNotEqual(self.update("bad", invalid).returncode, 0)

        duplicate = '{"source":"' + "a" * 40 + '","source":"' + "b" * 40 + '",' \
                    '"configuration":{"compiler":"x","build_type":"Release",' \
                    '"sanitizers":"none","runner":"localrunner","fixture":"x"},' \
                    '"provenance":{"local_command":"x"},"metrics":{"wall":' \
                    '{"value":1,"unit":"wall_seconds","status":"passed"}}}'
        result = self.run_cli("update", self.path, "--profile", "duplicate", "--input", duplicate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate JSON key", result.stderr)

    def test_bad_input_preserves_original_file(self):
        self.assertEqual(self.update("local", record()).returncode, 0)
        before = self.path.read_bytes()
        invalid = record()
        invalid["metrics"]["wall"]["value"] = -0.25
        self.assertNotEqual(self.update("local", invalid).returncode, 0)
        self.assertEqual(self.path.read_bytes(), before)

    def test_size_bound_rejects_oversized_existing_and_proposed_file(self):
        self.path.write_bytes(b" " * (32 * 1024 + 1))
        self.assertNotEqual(self.run_cli("check", self.path).returncode, 0)
        before = self.path.read_bytes()
        self.assertNotEqual(self.update("local", record()).returncode, 0)
        self.assertEqual(self.path.read_bytes(), before)

        self.path.unlink()
        huge = record(metrics={"x" * 33000: {
            "value": 1, "unit": "ratio", "status": "passed"}})
        self.assertNotEqual(self.update("local", huge).returncode, 0)
        self.assertFalse(self.path.exists())

    def test_check_rejects_bad_document_and_profile_metric_limits(self):
        self.path.write_text('{"schema_version":1,"profiles":{"x":{}}}', encoding="utf-8")
        self.assertNotEqual(self.run_cli("check", self.path).returncode, 0)


if __name__ == "__main__":
    unittest.main()

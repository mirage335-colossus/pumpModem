"""Offline contract tests for native copied-package recovery metadata."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/ci-native-copy-inputs.py"
_spec = importlib.util.spec_from_file_location("ci_native_copy_inputs", TOOL)
native = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
_spec.loader.exec_module(native)


class NativeCopyInputsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.run_id = 39826272767
        self.sha = "5c631a9e816715c26cd73c0bac98d036b674a812"
        self.run = {
            "id": self.run_id,
            "repository": {"full_name": "mirage335-colossus/pumpModem"},
            "head_repository": {"full_name": "mirage335-colossus/pumpModem"},
            "head_sha": self.sha,
            "path": native.WORKFLOW_PATH,
            "event": "workflow_dispatch",
        }
        self.jobs = [{
            "id": 119990278363, "run_id": self.run_id,
            "head_sha": self.sha, "name": "Linux Release / package / all",
            "status": "completed", "conclusion": "success",
        }]
        self.artifacts = [{
            "id": 12448956989, "name": "DataPump-Linux-x86_64-glibc-2.35",
            "size_in_bytes": 98408407,
            "digest": "sha256:cce3951abd7a212b3bda26a12f1f555655c072ce23015eba86191ccfc7aa711f",
            "expired": False,
            "workflow_run": {"id": self.run_id, "head_sha": self.sha},
        }]

    def validate(self):
        return native.validate_metadata(
            "mirage335-colossus/pumpModem", self.run_id, self.sha,
            self.run, self.jobs, self.artifacts,
        )

    def test_accepts_actual_native_producer_metadata(self) -> None:
        result = self.validate()["native"]
        self.assertEqual(result["id"], 12448956989)
        self.assertEqual(result["name"], "DataPump-Linux-x86_64-glibc-2.35")
        self.assertEqual(result["producer_job_id"], 119990278363)
        self.assertEqual(result["digest"], "sha256:cce3951abd7a212b3bda26a12f1f555655c072ce23015eba86191ccfc7aa711f")

    def test_rejects_other_source_profiles(self) -> None:
        for run, jobs, artifacts in (
            ({**self.run, "path": ".github/workflows/sdk.yml"}, self.jobs, self.artifacts),
            (self.run, [{**self.jobs[0], "name": "SDK packages, fltk / SDK packages / fltk / debian:bookworm-slim"}], self.artifacts),
            (self.run, self.jobs, [{**self.artifacts[0], "name": "DataPump-SDK-fltk-Linux-x86_64-glibc-2.36"}]),
        ):
            with self.subTest(run=run, jobs=jobs, artifacts=artifacts), self.assertRaises(ValueError):
                native.validate_metadata("mirage335-colossus/pumpModem", self.run_id, self.sha, run, jobs, artifacts)

    def test_rejects_unbound_or_unqualified_sources(self) -> None:
        variants = [
            ({**self.run, "repository": {"full_name": "someone/fork"}}, self.jobs, self.artifacts, self.sha),
            (self.run, self.jobs, self.artifacts, "0" * 40),
            (self.run, [{**self.jobs[0], "conclusion": "failure"}], self.artifacts, self.sha),
            (self.run, self.jobs, [{**self.artifacts[0], "expired": True}], self.sha),
            (self.run, self.jobs, self.artifacts + self.artifacts, self.sha),
        ]
        for run, jobs, artifacts, sha in variants:
            with self.subTest(run=run, jobs=jobs, artifacts=artifacts, sha=sha), self.assertRaises(ValueError):
                native.validate_metadata("mirage335-colossus/pumpModem", self.run_id, sha, run, jobs, artifacts)


if __name__ == "__main__":
    unittest.main()

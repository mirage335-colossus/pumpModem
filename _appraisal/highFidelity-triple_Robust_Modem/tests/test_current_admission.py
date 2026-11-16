"""Regression coverage for current_admission's receipt boundary."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


APP = Path(__file__).resolve().parents[1]
SCRIPT = APP / "current_admission.py"
SPEC = importlib.util.spec_from_file_location("current_admission_under_test", SCRIPT)
current_admission = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(current_admission)


def valid_receipt():
    return {
        "status": "failed",
        "source_identity": {
            "native_source_sha256": "a" * 64,
            "appraisal_source_sha256": "b" * 64,
            "native_source_commit_at_configure": "c" * 40,
        },
        "accepted_samples": 1,
        "rendered_samples": 1,
        "input_open_at_completion": True,
        "observed_native_recovery": False,
        "false_data_slots": 0,
        "admission_replay": {
            "executed": True,
            "baseline_semantic_parity": True,
        },
    }


class CurrentAdmissionBoundaryTests(unittest.TestCase):
    def setUp(self):
        (APP / "local-evidence").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(
            prefix="test-current-admission-", dir=APP / "local-evidence")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.probe = self.root / "fake-probe"
        self.probe.write_bytes(b"probe-before")
        self.output = self.root / "results"
        self.argv = [str(SCRIPT), "--probe", str(self.probe),
                     "--output-dir", str(self.output), "--stage", "boundary"]

    def invoke_main(self, expected_exit=None):
        with mock.patch.object(current_admission, "ROOT", APP), \
                mock.patch.object(sys, "argv", self.argv), \
                contextlib.redirect_stdout(io.StringIO()):
            if expected_exit is None:
                return current_admission.main()
            with self.assertRaises(SystemExit) as raised:
                current_admission.main()
            return raised.exception

    def read_summary(self):
        return json.loads((self.output / "boundary-summary.json").read_text())

    def test_binary_change_invalidates_otherwise_valid_receipt(self):
        result = subprocess.CompletedProcess(
            args=self.argv, returncode=1, stdout=json.dumps(valid_receipt()), stderr="")

        def run_and_change_binary(*_args, **_kwargs):
            self.probe.write_bytes(b"probe-after")
            return result

        with mock.patch.object(current_admission.subprocess, "run", side_effect=run_and_change_binary):
            self.invoke_main(expected_exit=True)

        summary = self.read_summary()
        self.assertFalse(summary["complete"])
        self.assertEqual(len(summary["cases"]), 1)
        row = summary["cases"][0]
        self.assertFalse(row["execution_and_replay_valid"])
        self.assertFalse(row["binary_unchanged"])
        self.assertNotEqual(row["binary_sha256"], row["binary_sha256_after_case"])

    def test_timeout_preserves_partial_streams_and_failed_summary(self):
        timeout = subprocess.TimeoutExpired(
            cmd=self.argv, timeout=135, output=b"partial-json-bytes", stderr=b"partial-error-bytes")
        with mock.patch.object(current_admission.subprocess, "run", side_effect=timeout):
            self.invoke_main(expected_exit=True)

        summary = self.read_summary()
        self.assertFalse(summary["complete"])
        self.assertEqual(len(summary["cases"]), 1)
        row = summary["cases"][0]
        self.assertFalse(row["execution_and_replay_valid"])
        self.assertEqual(row["execution_error"], "subprocess timeout")
        self.assertEqual((self.output / "clean-8s-v768.partial.stdout.txt").read_bytes(),
                         b"partial-json-bytes")
        self.assertEqual((self.output / "clean-8s-v768.partial.stderr.txt").read_bytes(),
                         b"partial-error-bytes")

    def test_unchanged_binary_accepts_valid_execution_receipt(self):
        result = subprocess.CompletedProcess(
            args=self.argv, returncode=1, stdout=json.dumps(valid_receipt()), stderr="")
        with mock.patch.object(current_admission.subprocess, "run", return_value=result):
            self.assertEqual(self.invoke_main(), 0)

        summary = self.read_summary()
        self.assertTrue(summary["complete"])
        row = summary["cases"][0]
        self.assertTrue(row["execution_and_replay_valid"])
        self.assertTrue(row["binary_unchanged"])
        self.assertEqual(row["native_requirements_status"], "failed")


if __name__ == "__main__":
    unittest.main()

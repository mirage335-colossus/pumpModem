import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).parents[1] / "tools" / "ci-build-probe.py"
SPEC = importlib.util.spec_from_file_location("ci_build_probe", MODULE_PATH)
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class BuildProbeTests(unittest.TestCase):
    def test_snapshot_git_trust_is_scoped_and_failure_retains_stderr(self):
        root = Path('/fixture/checkout')
        failure = subprocess.CompletedProcess([], 128, b'', b'fatal: snapshot unavailable')
        with mock.patch.object(probe.subprocess, 'run', return_value=failure) as run:
            with self.assertRaisesRegex(RuntimeError, 'fatal: snapshot unavailable'):
                probe.git_output(root, 'archive', '--format=tar', 'abc123')
        self.assertEqual(run.call_args.args[0], ['git', '-c',
            'safe.directory=' + str(root), 'archive', '--format=tar', 'abc123'])

    def test_source_identity_binds_commit_and_dirty_digest(self):
        identity = probe.source_identity("abc123", " M src/a.cpp\n")
        self.assertEqual(identity["source_commit"], "abc123")
        self.assertEqual(identity["dirty_tracked_paths"], [" M src/a.cpp"])
        self.assertEqual(len(identity["dirty_digest"]), 64)
        self.assertEqual(identity["classification"], "diagnostic only")

    def test_cache_stats_parses_json_or_reports_unavailable(self):
        completed = subprocess.CompletedProcess([], 0, '{"cache hit": 3}', "")
        with mock.patch.object(probe, "run", return_value=completed):
            self.assertEqual(probe.cache_stats("ccache", Path(".")),
                             {"available": True, "format": "json",
                              "stats": {"cache hit": 3}})
        completed = subprocess.CompletedProcess([], 0, "not-json", "")
        with mock.patch.object(probe, "run", return_value=completed):
            self.assertFalse(probe.cache_stats("ccache", Path("."))["available"])
        with mock.patch.object(probe, "run", side_effect=FileNotFoundError):
            self.assertFalse(probe.cache_stats("missing", Path("."))["available"])

    def test_failed_build_closes_stage_and_records_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            calls = [subprocess.CompletedProcess([], 0, "cleaned", ""),
                     subprocess.CompletedProcess([], 0, "cache stats reset", ""),
                     subprocess.CompletedProcess([], 2, "compiler failed", "")]
            with mock.patch.object(probe, "run", side_effect=calls), mock.patch.object(
                    probe, "cache_stats", return_value={"available": False}):
                with self.assertRaisesRegex(RuntimeError, "cold build failed"):
                    probe.stage("cold", root, root, {}, "ccache", root)
            self.assertIn("compiler failed", (root / "cold.log").read_text())
            self.assertEqual(json.loads((root / "cold-failure.json").read_text())["returncode"], 2)


if __name__ == "__main__":
    unittest.main()

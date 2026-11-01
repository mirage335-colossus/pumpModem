"""Focused process-tree lifecycle checks for the isolated campaign runner."""

from __future__ import annotations

import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from argparse import Namespace
from unittest import mock


RUNNER = pathlib.Path(__file__).resolve().parents[1] / "tools" / "triad_bounded_run.py"
sys.path.insert(0, str(RUNNER.parent))
import triad_bounded_run


class BoundedRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="triad-bounded-run-")
        self.root = pathlib.Path(self.tmp.name)
        self.cwd = self.root / "work"
        self.cwd.mkdir()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def launch(self, command: list[str], *, timeout: float = 3,
               maximum: float = 3, grace: float = 0.2) -> tuple[subprocess.CompletedProcess[str], pathlib.Path, pathlib.Path]:
        log = self.root / "run.log"
        status = self.root / "status.json"
        argv = [sys.executable, str(RUNNER), "--cwd", str(self.cwd),
                "--log", str(log), "--status", str(status),
                "--timeout", str(timeout), "--max-seconds", str(maximum),
                "--term-grace", str(grace), "--", *command]
        completed = subprocess.run(argv, text=True, capture_output=True, timeout=15)
        return completed, log, status

    def process_tree_command(self, pid_file: pathlib.Path) -> list[str]:
        """Three same-group processes that deliberately ignore graceful TERM."""
        append_pid = (
            f"open({str(pid_file)!r}, 'a').write(str(os.getpid()) + '\\n')"
        )
        grandchild = (
            "import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            + append_pid + "; time.sleep(60)"
        )
        child = (
            "import os,signal,subprocess,sys,time; "
            "signal.signal(signal.SIGTERM,signal.SIG_IGN); " + append_pid + "; "
            f"subprocess.Popen([sys.executable,'-c',{grandchild!r}],"
            "stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
            "time.sleep(60)"
        )
        parent = (
            "import os,signal,subprocess,sys,time; "
            "signal.signal(signal.SIGTERM,signal.SIG_IGN); " + append_pid + "; "
            f"subprocess.Popen([sys.executable,'-c',{child!r}],"
            "stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
            "time.sleep(60)"
        )
        return [sys.executable, "-c", parent]

    def wait_for_pids(self, path: pathlib.Path, count: int = 3) -> list[int]:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if path.exists():
                rows = path.read_text().splitlines()
                if len(rows) >= count:
                    return [int(row) for row in rows[:count]]
            time.sleep(0.02)
        self.fail(f"process tree did not publish {count} PIDs: {path}")

    def assert_processes_gone(self, pids: list[int]) -> None:
        for pid in pids:
            with self.subTest(pid=pid):
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)

    def test_success_runs_argument_array_and_records_result(self) -> None:
        completed, log, status = self.launch(
            [sys.executable, "-c", "import sys; print(repr(sys.argv[1]))", "two words"]
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(status.read_text())
        self.assertEqual(report["schema"], "triad-bounded-run-v1")
        self.assertEqual(report["outcome"], "completed")
        self.assertEqual(report["exit_code"], 0)
        self.assertTrue(report["termination"]["group_empty"])
        self.assertIn("'two words'", log.read_text())

    def test_timeout_kills_and_reaps_term_ignoring_parent_child_and_grandchild(self) -> None:
        pid_file = self.root / "pids"
        command = self.process_tree_command(pid_file)
        runner = subprocess.Popen(
            [sys.executable, str(RUNNER), "--cwd", str(self.cwd),
             "--log", str(self.root / "timeout.log"),
             "--status", str(self.root / "timeout.json"),
             "--timeout", "0.45", "--max-seconds", "1", "--term-grace", "0.15",
             "--", *command], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True
        )
        pids = self.wait_for_pids(pid_file)
        _, stderr = runner.communicate(timeout=10)
        self.assertEqual(runner.returncode, 124, stderr)
        report = json.loads((self.root / "timeout.json").read_text())
        self.assertEqual(report["outcome"], "timeout")
        self.assertTrue(report["timed_out"])
        self.assertFalse(report["cancelled"])
        self.assertTrue(report["termination"]["term_sent"])
        self.assertTrue(report["termination"]["kill_sent"])
        self.assertTrue(report["termination"]["group_empty"])
        self.assert_processes_gone(pids)

    def test_external_sigint_and_sigterm_cancel_and_reap_the_tree(self) -> None:
        for signum, label in ((signal.SIGINT, "int"), (signal.SIGTERM, "term")):
            with self.subTest(signal=label):
                pid_file = self.root / f"cancel-{label}-pids"
                command = self.process_tree_command(pid_file)
                runner = subprocess.Popen(
                    [sys.executable, str(RUNNER), "--cwd", str(self.cwd),
                     "--log", str(self.root / f"cancel-{label}.log"),
                     "--status", str(self.root / f"cancel-{label}.json"),
                     "--timeout", "5", "--max-seconds", "5", "--term-grace", "0.15",
                     "--", *command], stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE, text=True
                )
                pids = self.wait_for_pids(pid_file)
                runner.send_signal(signum)
                _, stderr = runner.communicate(timeout=10)
                self.assertEqual(runner.returncode, 128 + signum, stderr)
                report = json.loads((self.root / f"cancel-{label}.json").read_text())
                self.assertEqual(report["outcome"], "cancelled")
                self.assertEqual(report["cancel_signal"], signum)
                self.assertTrue(report["termination"]["kill_sent"])
                self.assertTrue(report["termination"]["group_empty"])
                self.assert_processes_gone(pids)

    def test_success_cannot_hide_a_leftover_descendant(self) -> None:
        pid_file = self.root / "leftover-pids"
        child = (
            "import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            f"open({str(pid_file)!r},'w').write(str(os.getpid())); time.sleep(60)"
        )
        parent = (
            "import os,subprocess,sys,time; "
            f"subprocess.Popen([sys.executable,'-c',{child!r}],"
            "stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
            f"deadline=time.monotonic()+2\n"
            f"while not os.path.exists({str(pid_file)!r}) and time.monotonic()<deadline:\n"
            " time.sleep(.01)"
        )
        completed, _, status = self.launch([sys.executable, "-c", parent], grace=0.15)
        self.assertTrue(pid_file.exists(), "child failed to publish its PID before the parent exited")
        pid = int(pid_file.read_text())
        report = json.loads(status.read_text())
        self.assertEqual(completed.returncode, 125)
        self.assertEqual(report["outcome"], "left_descendants")
        self.assertTrue(report["termination"]["group_empty"])
        self.assert_processes_gone([pid])

    def test_timeout_cannot_exceed_declared_run_limit(self) -> None:
        completed, log, status = self.launch(
            [sys.executable, "-c", "raise SystemExit('must not execute')"],
            timeout=2, maximum=1
        )
        self.assertEqual(completed.returncode, 125)
        self.assertIn("exceeds the declared", completed.stderr)
        self.assertFalse(log.exists())
        self.assertFalse(status.exists())

        completed, log, status = self.launch(
            [sys.executable, "-c", "raise SystemExit('must not execute')"], grace=31
        )
        self.assertEqual(completed.returncode, 125)
        self.assertIn("--term-grace cannot exceed", completed.stderr)
        self.assertFalse(log.exists())
        self.assertFalse(status.exists())

    def test_child_failure_is_not_reported_as_success(self) -> None:
        completed, _, status = self.launch([sys.executable, "-c", "raise SystemExit(7)"])
        report = json.loads(status.read_text())
        self.assertEqual(completed.returncode, 7)
        self.assertEqual(report["outcome"], "child_failure")
        self.assertEqual(report["child_exit_code"], 7)

    def test_unreaped_direct_child_records_failure_without_unbounded_wait(self) -> None:
        process = mock.Mock()
        process.pid = 765432
        process.returncode = None
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired("mock-child", 1)
        log = self.root / "stuck.log"
        status = self.root / "stuck.json"
        args = Namespace(cwd=self.cwd, log=log, status=status, timeout=0.01,
                         max_seconds=1.0, term_grace=0.01,
                         command=[sys.executable, "-c", "pass"])
        not_empty = {"term_sent": True, "kill_sent": True, "group_empty": False,
                     "direct_child_reaped": False}
        with (mock.patch.object(triad_bounded_run, "_enable_subreaper"),
              mock.patch.object(triad_bounded_run.subprocess, "Popen", return_value=process),
              mock.patch.object(triad_bounded_run, "_group_exists", return_value=True),
              mock.patch.object(triad_bounded_run, "_terminate_owned_group",
                                return_value=not_empty)):
            exit_code = triad_bounded_run.run(args)

        report = json.loads(status.read_text())
        self.assertEqual(exit_code, 125)
        self.assertEqual(report["outcome"], "cleanup_failure")
        self.assertEqual(report["direct_pid"], process.pid)
        self.assertFalse(report["termination"]["group_empty"])
        self.assertFalse(report["termination"]["direct_child_reaped"])
        self.assertIn("remained after bounded", report["cleanup_error"])
        process.wait.assert_not_called()


if __name__ == "__main__":
    unittest.main()

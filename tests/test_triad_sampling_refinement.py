"""Focused actual-runner lifecycle checks for the refinement exporter driver."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "triad-sampling-refinement.py"
SPEC = importlib.util.spec_from_file_location("sampling_refinement", SCRIPT)
refinement = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(refinement)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux owned-group runner")
class RefinementLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="triad-refinement-cleanup-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def fake_tree(self, label):
        pids = self.root / (label + "-pids")
        descendant = ("import os, signal, time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                      f"open({str(pids)!r},'a').write(str(os.getpid())+'\\n'); time.sleep(60)")
        binary = self.root / (label + "-exporter")
        binary.write_text("#!" + sys.executable + "\n"
                          "import os, signal, subprocess, sys, time\n"
                          "signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
                          f"open({str(pids)!r},'a').write(str(os.getpid())+'\\n')\n"
                          f"subprocess.Popen([sys.executable,'-c',{descendant!r}])\n"
                          "time.sleep(60)\n")
        binary.chmod(0o755)
        return binary, pids

    def wait_ready(self, pidfile):
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            if pidfile.exists() and len(pidfile.read_text().splitlines()) == 2:
                return
            time.sleep(.02)
        self.fail("fake exporter and child did not become ready")

    def assert_gone(self, pidfile):
        pids = [int(row) for row in pidfile.read_text().splitlines()]
        self.assertEqual(len(pids), 2)
        for pid in pids:
            self.assertFalse(Path(f"/proc/{pid}").exists(), f"owned child {pid} remains")

    def test_success_requires_runner_cleanup_receipt(self):
        result = refinement.run([sys.executable, "-c", "print('complete')"], self.root/"success", 2, .1)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["returncode"], 0)
        self.assertTrue(result["runner_status"]["termination"]["group_empty"])
        self.assertIn("complete", (self.root/"success"/"export.log").read_text())

    def test_timeout_reaps_term_ignoring_exporter_and_descendant(self):
        binary, pids = self.fake_tree("timeout")
        result = refinement.run([str(binary)], self.root/"timeout", .5, .1)
        self.assertEqual((result["status"], result["returncode"]), ("timeout", 124))
        self.assertTrue(result["runner_status"]["termination"]["kill_sent"])
        self.assertTrue(result["runner_status"]["termination"]["group_empty"])
        self.assert_gone(pids)

    def test_driver_int_and_term_forward_and_stop_new_exports(self):
        for sig in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=sig):
                binary, pids = self.fake_tree(str(sig))
                output = self.root / ("cancelled-" + str(sig))
                env = dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1")
                process = subprocess.Popen([sys.executable, str(SCRIPT), "--binary", str(binary),
                    "--output", str(output), "--rates", ".36", "--sample-rates", "1", "64",
                    "--profiles", "baseline", "--timeout", "10", "--term-grace", ".1"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
                try:
                    self.wait_ready(pids)
                    process.send_signal(sig)
                    stdout, stderr = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 128+sig, (stdout, stderr))
                    report = json.loads((output/"results.json").read_text())
                    self.assertTrue(report["cancelled"])
                    self.assertEqual(len(report["cases"]), 1)
                    case = report["cases"][0]
                    self.assertEqual(case["status"], "cancelled")
                    self.assertEqual(case["runner_status"]["cancel_signal"], sig)
                    self.assertTrue(case["runner_status"]["termination"]["group_empty"])
                    self.assert_gone(pids)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.communicate(timeout=10)


if __name__ == "__main__":
    unittest.main()

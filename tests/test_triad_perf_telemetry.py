"""Cheap telemetry parser and subprocess-lifecycle checks; no GPU needed."""
from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("triad_perf_telemetry", ROOT / "tools/triad_perf_telemetry.py")
telemetry = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(telemetry)


class TelemetryTests(unittest.TestCase):
    def test_strict_csv_fields_and_unavailable_counters(self):
        row = telemetry._parse_line("80, 35, 1800, 144.50, 57, 570.86.16")
        self.assertEqual(row["gpu_busy_percent"], 80.)
        self.assertEqual(row["driver_version"], "570.86.16")
        self.assertIsNone(telemetry._parse_line("101, 35, 1800, 144.50, 57, 570.86.16"))
        self.assertIsNone(telemetry._parse_line("80, 35, nan, 144.50, 57, 570.86.16"))
        self.assertIsNone(telemetry._parse_line("unexpected driver diagnostics"))
        unavailable = telemetry._parse_line("[N/A], N/A, [Not Supported], N/A, N/A, N/A")
        self.assertTrue(all(value is None for value in unavailable.values()))

    def test_uuid_binding_does_not_publish_hardware_identifier(self):
        identifier = "GPU-12345678-1234-1234-1234-123456789abc"
        sampler = telemetry._Sampler(0, device_uuid=identifier)
        self.assertEqual(sampler._selector, identifier)
        encoded = json.dumps(sampler.summarize(0., 1.))
        self.assertNotIn(identifier, encoded)
        self.assertIn("selected CUDA device UUID", encoded)
        with self.assertRaises(ValueError):
            telemetry._Sampler(0, device_uuid="unsafe-selector")

    def test_missing_binary_is_optional_and_invalid_device_fails(self):
        with mock.patch.object(telemetry.shutil, "which", return_value=None):
            with telemetry.start_sampler(0) as sampler:
                result = sampler.summarize(0., 1.)
        self.assertEqual(result["status"], "unavailable")
        self.assertIn("executable unavailable", result["reason"])
        self.assertIsNone(result["confidence_interval"])
        json.dumps(result, allow_nan=False)
        for bad in (-1, True, "0", 256):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                with telemetry.start_sampler(bad):
                    pass

    def test_ring_and_window_summary_preserve_unavailable_fields(self):
        sampler = telemetry._Sampler(0)
        row = telemetry._parse_line("80, N/A, 1800, 140, 57, 570.86")
        sampler._samples.extend([(10., row), (11., {**row, "gpu_busy_percent": 60.}), (12., row)])
        result = sampler.summarize(10., 11.)
        self.assertEqual(result["sample_count"], 2)
        self.assertEqual(result["metrics"]["gpu_busy_percent"], {
            "status": "available", "sample_count": 2, "min": 60., "max": 80., "mean": 70.})
        self.assertEqual(result["metrics"]["memory_busy_percent"]["status"], "unavailable")
        self.assertEqual(result["samples"][1]["seconds_from_window_start"], 1.)
        with self.assertRaises(ValueError):
            sampler.summarize(2., 1.)

    @unittest.skipUnless(os.name == "posix", "bounded POSIX fake producer")
    def test_fake_producer_has_bounded_output_and_is_killed_and_reaped(self):
        real_popen = subprocess.Popen
        captured = {}
        code = ("import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                "os.write(2,b'x'*20000); os.write(1,b'x'*5000+b'\\n'); "
                "os.write(1,b'80, 35, 1800, 144.5, 57, 570.86\\n'); time.sleep(60)")
        def fake_popen(command, **kwargs):
            captured["command"] = command
            captured["process"] = real_popen([sys.executable, "-u", "-c", code], **kwargs)
            return captured["process"]
        start = time.monotonic()
        with mock.patch.object(telemetry.shutil, "which", return_value="/mock/nvidia-smi"), \
             mock.patch.object(telemetry.subprocess, "Popen", side_effect=fake_popen):
            with telemetry.start_sampler(3) as sampler:
                deadline = time.monotonic() + 2.
                while time.monotonic() < deadline:
                    result = sampler.summarize(start, time.monotonic())
                    if result["sample_count"]:
                        break
                    time.sleep(.01)
                self.assertEqual(result["sample_count"], 1)
                self.assertEqual(result["discarded_invalid_lines"], 1)
                self.assertGreater(result["stderr_bytes_discarded"], 0)
        self.assertEqual(sampler._cleanup, "reaped")
        self.assertEqual(captured["process"].returncode, -9)
        self.assertLess(time.monotonic() - start, 3.)
        self.assertIn("--id=3", captured["command"])
        self.assertIn("--loop-ms=200", captured["command"])
        self.assertNotIn("uuid", " ".join(captured["command"]))

    def test_inherited_watchdog_group_never_signals_the_runner_group(self):
        process = mock.Mock()
        process.stdout = io.BytesIO()
        process.stderr = io.BytesIO()
        process.poll.side_effect = [None, 0]
        process.wait.return_value = 0
        with mock.patch.object(telemetry.shutil, "which", return_value="/mock/nvidia-smi"), \
             mock.patch.object(telemetry.subprocess, "Popen", return_value=process) as popen, \
             mock.patch.object(telemetry.os, "killpg") as killpg:
            with telemetry.start_sampler(0, inherit_process_group=True) as sampler:
                pass
        self.assertFalse(popen.call_args.kwargs["start_new_session"])
        process.terminate.assert_called_once_with()
        process.kill.assert_not_called()
        killpg.assert_not_called()
        self.assertEqual(sampler._cleanup, "reaped")
        with self.assertRaises(ValueError):
            with telemetry.start_sampler(0, inherit_process_group=1):
                pass

    def test_nonzero_query_exit_reports_unavailable(self):
        real_popen = subprocess.Popen
        def fake_popen(command, **kwargs):
            return real_popen([sys.executable, "-c", "raise SystemExit(2)"], **kwargs)
        with mock.patch.object(telemetry.shutil, "which", return_value="/mock/nvidia-smi"), \
             mock.patch.object(telemetry.subprocess, "Popen", side_effect=fake_popen):
            with telemetry.start_sampler() as sampler:
                sampler._process.wait(timeout=2.)
                result = sampler.summarize(0., time.monotonic())
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["process_returncode"], 2)
        self.assertIn("query exited", result["reason"])


if __name__ == "__main__":
    unittest.main()

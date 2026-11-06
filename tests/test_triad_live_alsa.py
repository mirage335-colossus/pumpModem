"""Device-free contract checks for the explicit stereo ALSA adapter."""
import importlib.util
import json
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest
from unittest import mock

import numpy as np


SPEC = importlib.util.spec_from_file_location("triad_live_alsa", Path(__file__).resolve().parents[1] / "tools/triad_live_alsa.py")
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


def log(direction="CAPTURE", rate=48000):
    return f"""Hardware PCM card 1 'HD-Audio Generic' device 0 subdevice 0
Its setup is:
  stream       : {direction}
  access       : RW_INTERLEAVED
  format       : S16_LE
  channels     : 2
  rate         : {rate}
  exact rate   : {rate} ({rate}/1)
  msbits       : 16
  buffer_size  : 24000
  period_size  : 6000
"""


class AlsaToolsTests(unittest.TestCase):
    def config(self, root):
        (root / "input.f32").write_bytes(np.full(4800, .01, "<f4").tobytes())
        return m.parser().parse_args(["--live", "--exclusive", "--input", str(root / "input.f32"),
            "--output", str(root / "output.f32"), "--metadata", str(root / "capture.json"),
            "--rate", "48000", "--capture-device", "hw:1,0", "--playback-device", "hw:1,0",
            "--max-seconds", "30"])

    def test_exact_pcm_rounding_and_guard(self):
        x = np.array([-.98, 0, .1, .98, 1. / 32767], np.float32)
        expected = np.trunc(x * np.float32(32767)).astype("<i2")
        np.testing.assert_array_equal(m.pcm_s16(x), expected)
        for x in ([np.nan], [np.inf], [.981], []):
            with self.assertRaises(ValueError):
                m.pcm_s16(x)

    def test_negotiated_format_not_exit_code(self):
        self.assertEqual(m.parse_hw_format(log(), 48000, "CAPTURE")["channels"], "2")
        for bad in (log(rate=44100), log().replace("S16_LE", "S32_LE"),
                    log().replace("channels     : 2", "channels     : 1"),
                    log().replace("Hardware PCM", "Plug PCM"), ""):
            with self.assertRaises(ValueError):
                m.parse_hw_format(bad, 48000, "CAPTURE")

    def test_explicit_route_and_authorization(self):
        with tempfile.TemporaryDirectory() as d:
            c = self.config(Path(d))
            for key, value in (("live", False), ("exclusive", False), ("capture_device", "default"),
                               ("playback_device", "plughw:1,0"), ("capture_device", "alsa.monitor"),
                               ("rate", 1000), ("max_seconds", float("nan"))):
                old = getattr(c, key)
                setattr(c, key, value)
                with self.assertRaises(ValueError):
                    m.validate(c)
                setattr(c, key, old)

    def test_refuses_overwrite_before_start(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            c = self.config(root)
            (root / "output.f32").write_bytes(b"keep")
            with mock.patch.object(m.subprocess, "Popen") as popen:
                with self.assertRaises(FileExistsError):
                    m.run(c)
                popen.assert_not_called()
            self.assertEqual((root / "output.f32").read_bytes(), b"keep")

    def fake_processes(self, c, *, bad_format=False, playback_failure=False,
                       wait_failure=None, ignore_terminate=False):
        capture_holder = []

        class Process:
            instances = []

            def __init__(self, argv, stdin, stdout, stderr):
                self.is_capture = "arecord" in Path(argv[0]).name
                self.returncode = None
                self.stdout, self.stderr = stdout, stderr
                self.killed = self.terminated = False
                self.reaped = False
                self.argv = argv
                self.events = []
                self.instances.append(self)
                self.frames = int(argv[argv.index("--samples") + 1]) if self.is_capture else 0
                stderr.write(log("CAPTURE" if self.is_capture else "PLAYBACK",
                                 44100 if bad_format and self.is_capture else 48000).encode())
                stderr.flush()
                if self.is_capture:
                    capture_holder.append(self)
                    self.write_frames(round(c.preroll_seconds * c.rate))

            def write_frames(self, frames):
                # Distinct channel values prove fixed selection, not averaging.
                current = self.stdout.tell() // 4
                a = np.tile(np.array([1200, -800], "<i2"), (frames - current, 1))
                self.stdout.write(a.tobytes()); self.stdout.flush()

            def poll(self):
                return self.returncode

            def wait(self, timeout):
                self.events.append("wait")
                if self.returncode is not None:
                    self.reaped = True
                    return self.returncode
                if self.terminated and ignore_terminate:
                    raise subprocess.TimeoutExpired(self.argv, timeout)
                if not self.is_capture and wait_failure is not None:
                    wait_failure(self.argv, timeout)
                if self.is_capture:
                    self.write_frames(self.frames)
                    self.returncode = 0
                else:
                    capture_holder[0].write_frames(round((c.preroll_seconds + .1) * c.rate))
                    self.returncode = 1 if playback_failure else 0
                self.reaped = True
                return self.returncode

            def terminate(self):
                self.events.append("terminate")
                self.terminated = True
                if not ignore_terminate:
                    self.returncode = -15

            def kill(self):
                self.events.append("kill")
                self.killed = True
                self.returncode = -9

        return Process

    def test_mock_complete_capture_preserves_two_channels(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); c = self.config(root)
            c.capture_channel = 1
            with mock.patch.object(m.subprocess, "Popen", self.fake_processes(c)), \
                 mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "alsa-utils test", "")):
                result = m.run(c)
            self.assertEqual(result["status"], "complete", result["failure"])
            y = np.fromfile(c.output, "<f4")
            np.testing.assert_array_equal(y, np.full(len(y), -800 / 32768, np.float32))
            raw = np.fromfile(str(c.output) + ".stereo.s16le", "<i2").reshape(-1, 2)
            self.assertTrue(np.all(raw[:, 0] == 1200))
            self.assertEqual(json.loads(Path(c.metadata).read_text())["backend"], "alsa-utils-raw-stereo-v1")
            tx = np.fromfile(str(c.output) + ".tx.stereo.s16le", "<i2").reshape(-1, 2)
            np.testing.assert_array_equal(tx[:, 0], tx[:, 1])

    def test_background_noise_creates_separate_fresh_tx_artifact_before_playback(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            c = self.config(root)
            c.background_noise_bits = 16

            def mix(args, **kwargs):
                clean = Path(kwargs['input_path'])
                noisy = Path(kwargs['output_path'])
                self.assertNotEqual(clean, noisy)
                self.assertTrue(clean.is_file())
                noisy.write_bytes(clean.read_bytes())
                Path(kwargs['metadata_path']).write_text('{"noise_variance":1e-8}\n')
                return {'file': str(noisy), 'sha256': 'synthetic-test-hash',
                        'metadata': str(kwargs['metadata_path'])}

            with mock.patch.object(m.subprocess, "Popen", self.fake_processes(c)), \
                 mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "version", "")), \
                 mock.patch.object(m, "apply_background_noise", side_effect=mix) as mixer:
                result = m.run(c)
            self.assertEqual(result["status"], "complete", result["failure"])
            mixer.assert_called_once()
            clean = Path(str(c.output) + ".tx.clean.stereo.s16le")
            noisy = Path(str(c.output) + ".tx.stereo.s16le")
            self.assertTrue(clean.exists())
            self.assertTrue(noisy.exists())
            self.assertNotEqual(clean, noisy)
            self.assertEqual(result['background_noise']['file'], str(noisy))
            self.assertIn(str(noisy), result['argv']['playback'])

    def test_failed_negotiation_and_process_preserve_incomplete(self):
        for bad_format, playback_failure in ((True, False), (False, True)):
            with self.subTest(bad_format=bad_format), tempfile.TemporaryDirectory() as d:
                c = self.config(Path(d))
                with mock.patch.object(m.subprocess, "Popen", self.fake_processes(c, bad_format=bad_format,
                                                                                playback_failure=playback_failure)), \
                     mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "version", "")):
                    result = m.run(c)
                self.assertEqual(result["status"], "incomplete")
                self.assertTrue(result["failure"])
                self.assertTrue(Path(c.metadata).is_file())
                if bad_format:
                    self.assertIsNone(result["playback_returncode"])

    def test_timeout_kills_and_reaps_both_children(self):
        def timeout(argv, seconds):
            raise subprocess.TimeoutExpired(argv, seconds)

        with tempfile.TemporaryDirectory() as d:
            c = self.config(Path(d))
            processes = self.fake_processes(c, wait_failure=timeout, ignore_terminate=True)
            with mock.patch.object(m.subprocess, "Popen", processes), \
                 mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "version", "")):
                result = m.run(c)
            self.assertEqual(result["status"], "incomplete")
            self.assertIn("TimeoutExpired", result["failure"])
            self.assertEqual(result["cleanup_errors"], [])
            self.assertEqual(len(processes.instances), 2)
            for child in processes.instances:
                self.assertTrue(child.terminated)
                self.assertTrue(child.killed)
                self.assertTrue(child.reaped)
                self.assertEqual(child.returncode, -9)
                self.assertEqual(child.events[-4:], ["terminate", "wait", "kill", "wait"])
            retained = json.loads(Path(c.metadata).read_text())
            self.assertEqual(retained["status"], "incomplete")
            self.assertEqual(retained["capture_returncode"], -9)
            self.assertEqual(retained["playback_returncode"], -9)

    def test_signal_cancellation_terminates_reaps_and_restores_handlers(self):
        for number in (signal.SIGTERM, signal.SIGINT):
            def cancel(argv, timeout):
                signal.getsignal(number)(number, None)

            with self.subTest(signal=number), tempfile.TemporaryDirectory() as d:
                c = self.config(Path(d))
                processes = self.fake_processes(c, wait_failure=cancel)
                old_handlers = {n: signal.getsignal(n) for n in (signal.SIGTERM, signal.SIGINT)}
                args = ["--live", "--exclusive", "--input", c.input, "--output", c.output,
                        "--metadata", c.metadata, "--rate", str(c.rate),
                        "--capture-device", c.capture_device, "--playback-device", c.playback_device,
                        "--max-seconds", str(c.max_seconds)]
                with mock.patch.object(m.subprocess, "Popen", processes), \
                     mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "version", "")), \
                     mock.patch("builtins.print"):
                    code = m.main(args)
                self.assertEqual(code, 1)
                for n, old in old_handlers.items():
                    self.assertIs(signal.getsignal(n), old)
                self.assertEqual(len(processes.instances), 2)
                for child in processes.instances:
                    self.assertTrue(child.terminated)
                    self.assertTrue(child.reaped)
                    self.assertFalse(child.killed)
                    self.assertEqual(child.returncode, -15)
                retained = json.loads(Path(c.metadata).read_text())
                self.assertEqual(retained["status"], "incomplete")
                self.assertIn(f"signal {number}", retained["failure"])

    def test_data_close_failure_is_not_reported_complete(self):
        class FailingClose:
            def __init__(self, handle):
                self.handle = handle

            def __getattr__(self, name):
                return getattr(self.handle, name)

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return self.handle.__exit__(*args)

            def close(self):
                self.handle.close()
                raise OSError("injected delayed data close failure")

        with tempfile.TemporaryDirectory() as d:
            c = self.config(Path(d))
            original = Path.open

            def opened(path, *args, **kwargs):
                handle = original(path, *args, **kwargs)
                return FailingClose(handle) if path == Path(c.output) and args == ("x+b",) else handle

            with mock.patch.object(Path, "open", opened), \
                 mock.patch.object(m.subprocess, "Popen", self.fake_processes(c)), \
                 mock.patch.object(m.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "version", "")):
                result = m.run(c)
            self.assertEqual(result["status"], "incomplete")
            self.assertIn("injected delayed data close failure", result["failure"])
            self.assertEqual(len(result["finalization_errors"]), 1)
            self.assertEqual(json.loads(Path(c.metadata).read_text())["status"], "incomplete")


if __name__ == "__main__":
    unittest.main()

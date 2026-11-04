"""Hardware-free guards for the opt-in triad cable-capture CLI.

Set TRIAD_AUDIO_CAPTURE_BINARY to the built executable. Every case below must
reject before an audio endpoint is opened; this suite never authorizes live I/O.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


class TriadAudioCaptureCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = os.environ.get("TRIAD_AUDIO_CAPTURE_BINARY")
        if not binary:
            raise unittest.SkipTest("set TRIAD_AUDIO_CAPTURE_BINARY to run CLI guard tests")
        cls.binary = str(Path(binary).resolve())
        if not Path(cls.binary).is_file():
            raise unittest.SkipTest("TRIAD_AUDIO_CAPTURE_BINARY does not name a built executable")

    def invoke(self, *args):
        return subprocess.run([self.binary, *map(str, args)], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=10, check=False)

    @staticmethod
    def valid_args(root, *, live=True):
        source = root / "input.f32le"
        source.write_bytes(struct.pack("<4800f", *([0.01] * 4800)))
        args = ["--input", source, "--output", root / "capture.f32le",
                "--metadata", root / "capture.json", "--rate", "48000",
                "--capture-device", "hw:1,0", "--playback-device", "hw:1,0",
                "--exclusive", "--max-seconds", "30"]
        if live:
            args.insert(0, "--live")
        return args, source

    def test_help_is_device_free(self):
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--live", result.stdout)

    def test_live_opt_in_is_mandatory(self):
        with tempfile.TemporaryDirectory() as directory:
            args, _ = self.valid_args(Path(directory), live=False)
            result = self.invoke(*args)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--live", result.stderr)

    def test_invalid_rate_and_deadline_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            args, _ = self.valid_args(Path(directory))
            rate = args.copy()
            rate[rate.index("48000")] = "44099"
            bad_rate = self.invoke(*rate)
            deadline = args.copy()
            deadline[deadline.index("30")] = "2801"
            bad_deadline = self.invoke(*deadline)
        self.assertNotEqual(bad_rate.returncode, 0)
        self.assertIn("rate", bad_rate.stderr)
        self.assertNotEqual(bad_deadline.returncode, 0)
        self.assertIn("max-seconds", bad_deadline.stderr)

    def test_nonfinite_and_overrange_pcm_are_rejected_before_device_open(self):
        for sample, message in ((float("nan"), "nonfinite"), (0.981, "0.98")):
            with self.subTest(sample=sample), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                args, source = self.valid_args(root)
                source.write_bytes(struct.pack("<f", sample))
                result = self.invoke(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse((root / "capture.json").exists())

    def test_misaligned_and_oversized_pcm_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, source = self.valid_args(root)
            source.write_bytes(b"abc")
            misaligned = self.invoke(*args)
            source.write_bytes(b"")
            with source.open("wb") as stream:
                stream.truncate(512 * 1024 * 1024 + 4)
            oversized = self.invoke(*args)
        self.assertNotEqual(misaligned.returncode, 0)
        self.assertNotEqual(oversized.returncode, 0)
        self.assertIn("aligned", misaligned.stderr)
        self.assertIn("aligned", oversized.stderr)

    def test_existing_output_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, _ = self.valid_args(root)
            output = root / "capture.f32le"
            output.write_bytes(b"keep")
            result = self.invoke(*args)
            self.assertEqual(output.read_bytes(), b"keep")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("existing/uncreatable output", result.stderr)


if __name__ == "__main__":
    unittest.main()

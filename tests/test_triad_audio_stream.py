#!/usr/bin/env python3
"""Device-free boundary tests for the bounded triad audio pipe bridge."""
import json
import struct
import subprocess
import sys
import unittest

BINARY = sys.argv[1] if len(sys.argv) == 2 else None
sys.argv = sys.argv[:1]


class AudioStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if BINARY is None:
            raise RuntimeError("usage: test_triad_audio_stream.py PATH_TO_BINARY")
        cls.binary = BINARY

    def run_copy(self, data, frames, *args):
        return subprocess.run([self.binary, "--mode", "copy", "--frames", str(frames), *args],
                              input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)

    def test_bounded_copy_preserves_float_bytes_and_receipt(self):
        values = [0., -0.125, 0.25, 0.5, -1.]
        payload = struct.pack("<5f", *values)
        result = self.run_copy(payload, len(values), "--sample-rate", "48000")
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(result.stdout, payload)
        receipt = json.loads(result.stderr)
        self.assertEqual(receipt["schema"], "triad-audio-stream-v1")
        self.assertTrue(receipt["complete"])
        self.assertEqual(receipt["requested_frames"], len(values))
        self.assertEqual(receipt["completed_frames"], len(values))

    def test_short_stream_does_not_claim_completion(self):
        payload = struct.pack("<2f", .25, -.5)
        result = self.run_copy(payload, 3)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"before declared frame count", result.stderr)
        self.assertIn(b'"complete":false', result.stderr)

    def test_partial_float_frame_is_rejected(self):
        result = self.run_copy(b"\x00\x00\x00", 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"inside a float32 frame", result.stderr)

    def test_nonfinite_pcm_is_rejected_before_output(self):
        result = self.run_copy(struct.pack('<f', float('nan')), 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'')
        self.assertIn(b'nonfinite PCM', result.stderr)

    def test_unimplemented_capture_channel_is_rejected_without_device(self):
        result = subprocess.run([self.binary, '--mode', 'capture', '--frames', '1',
            '--channels', 'right'], capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'left logical mono', result.stderr)

    def test_large_declared_runtime_has_no_duration_guard(self):
        # Immediate EOF proves the frame count is parsed and accepted without
        # allocating a duration-sized buffer or waiting for the requested time.
        result = self.run_copy(b"", 86400 * 48_000, "--sample-rate", "48000")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"before declared frame count", result.stderr)

    def test_noise_options_are_only_allowed_for_final_playback(self):
        result = self.run_copy(b"", 0, "--background-noise-bits", "10")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"copy mode does not model", result.stderr)

    def test_cli_rejects_missing_count_and_invalid_audio_mode(self):
        missing = subprocess.run([self.binary, "--mode", "copy"], capture_output=True, timeout=5)
        bad_mode = subprocess.run([self.binary, "--mode", "invalid", "--frames", "0"],
                                  capture_output=True, timeout=5)
        self.assertNotEqual(missing.returncode, 0)
        self.assertNotEqual(bad_mode.returncode, 0)


if __name__ == "__main__":
    unittest.main()

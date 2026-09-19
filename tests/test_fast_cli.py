"""Fast CLI integration: real keyring, fixed intervals and physical completion."""
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


PUMP = str(pathlib.Path(sys.argv.pop(1)).resolve())
SOURCE_BYTES = b"\x00\x00fast fixed intervals\xff\x80" + bytes(range(32)) + b"\x00\x00"
PROFILE = ("--profile", "wire", "--apsk", "4", "--interleave", "1")


class FastCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="datapump-fast-cli-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = pathlib.Path(cls.temporary.name)
        cls.key = cls.directory / "fast.key"
        cls.source = cls.directory / "source.bin"
        cls.wave = cls.directory / "source.wav"
        cls.source.write_bytes(SOURCE_BYTES)
        # One production-size keyring provides independent right/wrong keys.
        # No reduced keyfile policy or plaintext export is exposed to the CLI.
        generated = subprocess.run(
            [PUMP, "keygen", "--output", str(cls.key), "--key-names", "Fast,Other"],
            capture_output=True, timeout=90)
        if generated.returncode:
            raise AssertionError(generated.stderr.decode(errors="replace"))
        cls.key_options = ("--keyfile", cls.key, "--key-name", "Fast")
        transmitted = subprocess.run(
            [PUMP, "fast-tx", "--input", str(cls.source), "--output", str(cls.wave),
             *map(str, cls.key_options), *PROFILE, "--json"],
            capture_output=True, timeout=90)
        if transmitted.returncode:
            raise AssertionError(transmitted.stderr.decode(errors="replace"))
        cls.tx = json.loads(transmitted.stdout)

    def run_pump(self, *args, ok=True):
        result = subprocess.run([PUMP, *map(str, args)], capture_output=True, timeout=90)
        self.assertEqual(result.returncode, 0 if ok else 2,
                         result.stderr.decode(errors="replace") + result.stdout.decode(errors="replace"))
        return result

    def test_profiles_and_fixed_geometry(self):
        for profile in ("wire", "ssb", "fm", "acoustic"):
            with self.subTest(profile=profile):
                report = json.loads(self.run_pump("fast-info", "--profile", profile).stdout)
                self.assertEqual(report["profile"], profile)
                self.assertEqual(report["physical_interval_bits"], 2048)
                self.assertGreater(report["cycle_intervals"], 0)
                self.assertIn(report["ciphertext_bytes"], (176, 192))
                self.assertNotIn("packet_bytes", report)
        for modulation in (4, 16, 64, 256):
            report = json.loads(self.run_pump("fast-info", "--apsk", modulation).stdout)
            self.assertEqual(report["constellation"], modulation)
            self.assertEqual(report["physical_interval_bits"], 2048)
        cycles = []
        for rate in ("1/2", "3/4", "7/8"):
            report = json.loads(self.run_pump("fast-info", "--code-rate", rate).stdout)
            cycles.append(report["cycle_intervals"])
        self.assertEqual(cycles, [33, 22, 19])

    def test_help_and_invalid_local_options(self):
        help_text = self.run_pump("fast-tx", "--help").stdout
        self.assertIn(b"2048", help_text)
        self.assertIn(b"EOF/cancellation is not physical end", help_text)
        for option, value in (("profile", "unknown"), ("apsk", "32"),
                              ("interleave", "0"), ("interleave", "65"),
                              ("sample-rate", "8000"), ("sample-rate", "192001"),
                              ("code-rate", "2/3"), ("rs", "off"),
                              ("quota-mb", "0"), ("quota-mb", "16385"),
                              ("apsk", "18446744073709551616")):
            with self.subTest(option=option, value=value):
                self.run_pump("fast-info", "--" + option, value, ok=False)
        self.run_pump("fast-info", "--snr", "40", ok=False)
        self.run_pump("fast-info", "--apsk", "4", "--apsk", "4", ok=False)
        self.run_pump("fast-info", "--interleave", ok=False)
        self.run_pump("fast-tx", "--input", self.source, "--output", self.directory / "no-key.wav", ok=False)

    def test_streamed_encrypted_wav_and_explicit_save(self):
        self.assertGreaterEqual(self.key.stat().st_size, 128 * 1024 * 1024)
        self.assertEqual(self.tx["source_bytes"], len(SOURCE_BYTES))
        self.assertFalse(self.tx["complete"])
        destination = self.directory / "received.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", self.wave,
            "--save", destination, *self.key_options, *PROFILE, "--json").stdout)
        self.assertTrue(result["physical_complete"])
        self.assertTrue(result["complete"])
        self.assertFalse(result["cancelled"])
        self.assertEqual(result["source_bytes"], len(SOURCE_BYTES))
        self.assertEqual(destination.read_bytes(), SOURCE_BYTES)
        self.assertGreater(result["authenticated_groups"], 0)
        self.assertGreater(result["intervals"], 0)

    def test_wrong_key_never_saves(self):
        destination = self.directory / "wrong-key.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", self.wave,
            "--keyfile", self.key, "--key-name", "Other", "--save", destination,
            *PROFILE, "--json", ok=False).stdout)
        self.assertFalse(result["complete"])
        self.assertEqual(result["source_bytes"], 0)
        self.assertFalse(destination.exists())

    def test_valid_container_eof_does_not_complete(self):
        path = self.directory / "no-absence.wav"
        shutil.copyfile(self.wave, path)
        with path.open("r+b") as file:
            header = file.read(44)
            self.assertEqual(header[:4], b"RIFF")
            self.assertEqual(header[36:40], b"data")
            rate = struct.unpack_from("<I", header, 24)[0]
            source_size = struct.unpack_from("<I", header, 40)[0]
            retained = source_size - rate * 6 * 2  # leave only 0.25s silence
            self.assertGreater(retained, 0)
            file.truncate(44 + retained)
            file.seek(4)
            file.write(struct.pack("<I", 36 + retained))
            file.seek(40)
            file.write(struct.pack("<I", retained))
        destination = self.directory / "incomplete.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", path, "--save", destination,
            *self.key_options, *PROFILE, "--json", ok=False).stdout)
        self.assertFalse(result["physical_complete"])
        self.assertFalse(result["complete"])
        self.assertFalse(destination.exists())

    def test_never_overwrite_outputs(self):
        destination = self.directory / "keep.bin"
        destination.write_bytes(b"keep existing file")
        self.run_pump("fast-rx", "--input", self.wave, "--save", destination,
            *self.key_options, *PROFILE, ok=False)
        self.assertEqual(destination.read_bytes(), b"keep existing file")
        size = self.wave.stat().st_size
        self.run_pump("fast-tx", "--input", self.source, "--output", self.wave,
            *self.key_options, *PROFILE, ok=False)
        self.assertEqual(self.wave.stat().st_size, size)


if __name__ == "__main__":
    unittest.main()

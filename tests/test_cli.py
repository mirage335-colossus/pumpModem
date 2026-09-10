"""Exercise the installed CLI boundary, not only its implementation functions."""
import base64
import errno
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
import wave
import xml.etree.ElementTree as ET

PUMP = str(pathlib.Path(sys.argv.pop(1)).resolve())
EPOCH = 1_800_000_000
# Small, realistic PCM captures keep the end-to-end suite fast even in Debug.
AUDIO = ("--sample-rate", "8000", "--bw", "1000", "--carrier", "1500")


class PumpCase(unittest.TestCase):
    def run_pump(self, *args, data=None, ok=True):
        result = subprocess.run([PUMP, *map(str, args)], input=data, capture_output=True, timeout=90)
        self.assertEqual(result.returncode, 0 if ok else 2,
                         f"pump {' '.join(map(str, args))}: {result.stderr.decode(errors='replace')}")
        if not ok:
            self.assertEqual(result.stdout, b"", "failed commands must not release partial content")
        return result


class CommandTests(PumpCase):
    def test_help_and_invalid_options(self):
        self.assertIn(b"simulate", self.run_pump("--help").stdout)
        self.assertEqual(self.run_pump("--version").stdout, b"Data Pump 0.1.0\n")
        self.run_pump("simulate", "--text", "x", "--nonsense", "yes", ok=False)
        self.run_pump("simulate", "--text", "x", "--snr", "nan", ok=False)
        self.run_pump("tx", "--text", "x", ok=False)
        self.run_pump("simulate", "--text", "x", "--bw", "0", ok=False)
        for options in (("--sample-rate", "0"), ("--sample-rate", "192001"),
                        ("--spreading", "0"), ("--spreading", "16385"),
                        ("--carrier", "nan"), ("--carrier", "inf"),
                        ("--fec", "21"), ("--memory-mb", "4097"),
                        ("--memory-mb", "-1"), ("--memory-mb", "1.5"),
                        ("--time", "18446744073709551616"),
                        ("--scramble",), ("--dsss",),
                        ("--text", "duplicate"), ("--json=true",)):
            with self.subTest(options=options):
                self.run_pump("pack", "--text", "x", *options, ok=False)
        self.run_pump("pack", "--text", "x", "--input", "-", data=b"ambiguous", ok=False)
        self.run_pump("simulate", "--text", "x", "--device", "default", ok=False)
        self.run_pump("simulate", "--text", "x", "--search-seconds", "121", ok=False)

    def test_command_specific_options_fail_before_side_effects(self):
        with tempfile.TemporaryDirectory() as folder:
            target = pathlib.Path(folder) / "must-not-exist.bin"
            for options in (("--keyfile", "missing.key"), ("--pad", "missing.pad"),
                            ("--scramble",), ("--dsss",), ("--repeatable",),
                            ("--device", "default")):
                with self.subTest(qr_options=options):
                    failure = self.run_pump("qr", "--text", "secret", "--output", target, *options, ok=False)
                    self.assertIn(b"QR", failure.stderr)
                    self.assertFalse(target.exists(), "rejected QR options must not publish plaintext")
            packed = self.run_pump("pack", "--text", "received").stdout
            for command in ("rx", "unpack", "status-rx"):
                with self.subTest(command=command):
                    failure = self.run_pump(command, "--output", target, data=packed, ok=False)
                    self.assertIn(b"--output", failure.stderr)
                    self.assertFalse(target.exists())
            failure = self.run_pump("tx", "--text", "x", "--output", target,
                                    "--device", "default", ok=False)
            self.assertIn(b"destination", failure.stderr)
            self.assertFalse(target.exists(), "ambiguous TX must not write a file or open audio")
            self.run_pump("pack", "--text", "x", "--save", target, ok=False)
            self.assertFalse(target.exists())

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux PTY terminal boundary")
    def test_terminal_control_injection_and_binary_stdout(self):
        import pty
        import tty

        def run_tty(*args, data=None, ok=True):
            master, slave = pty.openpty()
            try:
                # Disable the terminal's newline translation so byte comparisons
                # measure pump's escaping, not the PTY line discipline.
                tty.setraw(slave)
                try:
                    result = subprocess.run([PUMP, *args], input=data, stdout=slave,
                                            stderr=subprocess.PIPE, timeout=90)
                finally:
                    os.close(slave)
                output = bytearray()
                while True:
                    try:
                        chunk = os.read(master, 4096)
                    except OSError as error:
                        if error.errno == errno.EIO:  # The PTY slave is closed.
                            break
                        raise
                    if not chunk:
                        break
                    output.extend(chunk)
            finally:
                os.close(master)
            self.assertEqual(result.returncode, 0 if ok else 2, result.stderr.decode(errors="replace"))
            return result, bytes(output)

        visible = "Unicode café 世界 🌍".encode()
        controls = b"\x1b]52;c;YWJj\x07\r\x00\x7f\x9b\xc2\x9b\xff\n"
        text = visible + controls
        packet = self.run_pump("pack", data=text).stdout
        self.assertEqual(self.run_pump("unpack", data=packet).stdout, text,
                         "pipes must preserve every received byte")
        _, terminal = run_tty("unpack", data=packet)
        self.assertEqual(terminal, visible + b"\\x1b]52;c;YWJj\\x07\\x0d\\x00\\x7f\\x9b\\xc2\\x9b\\xff\n")
        self.assertNotIn(b"\x1b", terminal, "OSC52 must never reach a terminal parser")
        self.assertNotIn(b"\x07", terminal)
        terminal.decode("utf-8", errors="strict")
        failure, output = run_tty("pack", "--text", "binary output", ok=False)
        self.assertEqual(output, b"")
        self.assertIn(b"redirection", failure.stderr)

    def test_text_noisy_loopback(self):
        text = "Clipboard test: café — 12345\nsecond line"
        result = self.run_pump("simulate", "--text", text, "--snr", "16", "--delay-samples", "731", "--json")
        packet = json.loads(result.stdout)
        self.assertEqual(base64.b64decode(packet["data_base64"]).decode(), text)
        self.assertTrue(packet["validated"])
        self.assertFalse(packet["authenticated"])
        self.assertGreater(packet["diagnostics"]["bit_rate"], 0)

    def test_wav_file_workflow_and_exclusive_save(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            original = bytes(range(256)) * 2
            source, wave, target = root / "source.bin", root / "transfer.wav", root / "copy.bin"
            source.write_bytes(original)
            self.run_pump("tx", "--input", source, "--output", wave, "--fec", "60")
            result = self.run_pump("rx", "--input", wave, "--json")
            packet = json.loads(result.stdout)
            self.assertEqual(base64.b64decode(packet["data_base64"]), original)
            self.assertEqual(packet["filename"], "source.bin")
            self.assertEqual(sorted(p.name for p in root.iterdir()), ["source.bin", "transfer.wav"])
            self.run_pump("rx", "--input", wave, "--save", target)
            self.assertEqual(target.read_bytes(), original)
            self.run_pump("rx", "--input", wave, "--save", target, ok=False)
            self.run_pump("tx", "--text", "overwrite", "--output", wave, ok=False)

    def test_packet_pipe_and_corruption(self):
        data = b"CQ CQ hello radio"
        packed = self.run_pump("pack", "--input", "-", "--kind", "text", data=data).stdout
        result = self.run_pump("unpack", "--input", "-", data=packed)
        self.assertEqual(result.stdout, data)
        broken = bytearray(packed)
        broken[-40:] = b"\xff" * 40
        self.run_pump("unpack", "--input", "-", data=broken, ok=False)

    def test_repeatable_and_memory_limit(self):
        result = self.run_pump("simulate", "--text", "repeat me", "--repeatable", "--json")
        self.assertTrue(json.loads(result.stdout)["repeatable"])
        self.run_pump("simulate", "--text", "x", "--memory-mb", "0", ok=False)
        self.run_pump("simulate", "--text", "x", "--memory-mb", "1", ok=False)
        self.run_pump("rx", "--device-type", "ethernet", ok=False)

    def test_packet_boundaries_and_metadata(self):
        for length in (0, 1, 255, 256, 257):
            with self.subTest(length=length):
                data = bytes(index % 256 for index in range(length))
                packed = self.run_pump("pack", "--input", "-", data=data).stdout
                self.assertEqual(self.run_pump("unpack", "--input", "-", data=packed).stdout, data)
        data = b"a" * 65536
        packet = self.run_pump("pack", "--input", "-", "--repeatable", data=data).stdout
        decoded = json.loads(self.run_pump("unpack", "--json", data=packet).stdout)
        self.assertEqual(base64.b64decode(decoded["data_base64"]), data)
        self.assertTrue(decoded["repeatable"])
        self.assertRegex(decoded["id"], r"^[0-9a-f]{32}$")
        self.run_pump("pack", "--repeatable", data=data + b"a", ok=False)
        self.run_pump("pack", "--memory-mb", "1", data=b"a" * (1024 * 1024 + 1), ok=False)
        self.run_pump("pack", "--memory-mb", "1", data=b"a" * (200 * 1024), ok=False)

        filename, callsign, grid = "café-地球-🌍.bin", "台灣-é", "AA00aa"
        packed = self.run_pump("pack", "--kind", "file", "--filename", filename,
                               "--callsign", callsign, "--grid", grid, data=b"\x00\xffdata").stdout
        decoded = json.loads(self.run_pump("unpack", "--json", data=packed).stdout)
        self.assertEqual((decoded["filename"], decoded["callsign"], decoded["grid"]),
                         (filename, callsign, grid))
        self.assertEqual(base64.b64decode(decoded["data_base64"]), b"\x00\xffdata")
        self.run_pump("pack", "--kind", "file", "--filename", "é" * 127 + "a",
                      "--callsign", "é" * 32, "--grid", "a" * 32, data=b"x")
        for options in (("--kind", "file", "--filename", "../outside.bin"),
                        ("--kind", "file", "--filename", "CON.txt"),
                        ("--kind", "file", "--filename", "é" * 128),
                        ("--callsign", "é" * 33), ("--grid", "a" * 33),
                        ("--callsign", "line\nbreak"), ("--kind", "unknown")):
            with self.subTest(options=options):
                self.run_pump("pack", *options, data=b"x", ok=False)

    def test_qr_svg_pbm_and_limits(self):
        text = "Clipboard café 🌍 <script>alert(1)</script>"
        svg = self.run_pump("qr", "--text", text, "--format", "svg").stdout
        root = ET.fromstring(svg)
        self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
        self.assertEqual(root.attrib["width"], root.attrib["height"])
        self.assertTrue(root.find("{http://www.w3.org/2000/svg}path").attrib["d"])
        self.assertNotIn(b"<script>", svg)
        pbm = self.run_pump("qr", "--text", "CQ hello", "--format", "pbm").stdout.split()
        self.assertEqual(pbm[0], b"P1")
        width, height = map(int, pbm[1:3])
        self.assertEqual(width, height)
        self.assertGreaterEqual(width, 29)
        pixels = list(map(int, pbm[3:]))
        self.assertEqual(len(pixels), width * height)
        self.assertEqual(set(pixels), {0, 1})
        self.assertEqual(sum(pixels[:4 * width]), 0, "QR needs a four-module quiet zone")
        self.assertEqual(sum(pixels[-4 * width:]), 0)
        for y in range(height):
            self.assertEqual(sum(pixels[y * width:y * width + 4]), 0)
            self.assertEqual(sum(pixels[(y + 1) * width - 4:(y + 1) * width]), 0)
        for y in range(7):
            for x in range(7):
                expected = x in (0, 6) or y in (0, 6) or (2 <= x <= 4 and 2 <= y <= 4)
                self.assertEqual(bool(pixels[(y + 4) * width + x + 4]), expected, "QR finder pattern")
        self.run_pump("qr", "--text", "🌍" * 500, "--format", "pbm")
        self.run_pump("qr", "--text", "🌍" * 501, ok=False)
        self.run_pump("qr", "--text", "x", "--format", "png", ok=False)
        self.run_pump("qr", data=b"\xff", ok=False)
        with tempfile.TemporaryDirectory() as folder:
            target = pathlib.Path(folder) / "code.svg"
            self.run_pump("qr", "--text", text, "--output", target)
            self.assertEqual(target.read_bytes(), svg)
            self.run_pump("qr", "--text", "replacement", "--output", target, ok=False)
            self.assertEqual(target.read_bytes(), svg)

    def test_status_exact_duration_and_malformed_wav(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            status, truncated = root / "status.wav", root / "short.wav"
            self.run_pump("status-tx", "--bits", "010", "--output", status, *AUDIO)
            with wave.open(str(status), "rb") as reader:
                params = reader.getparams()
                frames = reader.readframes(reader.getnframes())
                self.assertEqual(params.nframes, 3 * 16, "few-bit status has no preamble or byte padding")
            result = json.loads(self.run_pump("status-rx", "--bits", "010", "--input", status, *AUDIO).stdout)
            self.assertFalse(result["authenticated"])
            self.assertEqual(result["known_bits"], "010")
            self.assertGreater(result["correlation"], .99)
            with wave.open(str(truncated), "wb") as writer:
                writer.setparams(params)
                writer.writeframes(frames[:-2])
            self.run_pump("status-rx", "--bits", "010", "--input", truncated, *AUDIO, ok=False)
            self.run_pump("rx", "--input", status, *AUDIO, ok=False)
            for bits in ("", "012", "0" * 4097):
                self.run_pump("status-tx", "--bits", bits, "--output", root / "invalid.wav", *AUDIO, ok=False)
            self.run_pump("status-tx", "--bits", "010", ok=False)
            malformed = bytearray(status.read_bytes())
            malformed[4:8] = b"\xff" * 4
            self.run_pump("rx", "--input", "-", data=malformed, ok=False)


class EncryptedCommandTests(PumpCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        cls.root = pathlib.Path(cls.folder.name)
        cls.key = cls.root / "shared.key"
        cls.other_key = cls.root / "other.key"
        for key in (cls.key, cls.other_key):
            result = subprocess.run([PUMP, "keygen", "--output", str(key)], capture_output=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors="replace"))

    def test_production_keyfile_and_exclusive_creation(self):
        self.assertEqual(self.key.stat().st_size, 128 * 1024 * 1024 + 96)
        if os.name != "nt":
            self.assertEqual(stat.S_IMODE(self.key.stat().st_mode), 0o600)
        with self.key.open("rb") as source:
            prefix = source.read(48)
            random_header_sample = source.read(1024)
        self.assertEqual(prefix[:8], b"DPMKEY01")
        self.assertEqual(int.from_bytes(prefix[8:16], "big"), 128 * 1024 * 1024)
        self.assertGreater(len(set(random_header_sample)), 128)
        self.run_pump("keygen", "--output", self.key, ok=False)
        with self.key.open("rb") as source:
            self.assertEqual(source.read(48), prefix)
        truncated = self.root / "truncated.key"
        truncated.write_bytes(prefix + random_header_sample)
        self.run_pump("pack", "--text", "x", "--keyfile", truncated, ok=False)
        self.run_pump("pack", "--text", "x", "--pad", self.key, ok=False)

    def test_encrypted_noisy_simulation_all_layers(self):
        result = self.run_pump("simulate", "--text", "Secure café 🌍", "--keyfile", self.key,
                               "--time", EPOCH, "--scramble", "--dsss", "--spreading", "4",
                               "--snr", "18", "--delay-samples", "313", "--json", *AUDIO)
        packet = json.loads(result.stdout)
        self.assertTrue(packet["validated"])
        self.assertTrue(packet["authenticated"])
        self.assertEqual(packet["timestamp"], EPOCH)
        self.assertEqual(base64.b64decode(packet["data_base64"]), "Secure café 🌍".encode())

    def test_encrypted_wav_positive_negative_epoch_drift(self):
        path = self.root / "encrypted.wav"
        text = "Clock drift test 🌍"
        self.run_pump("tx", "--text", text, "--keyfile", self.key, "--time", EPOCH,
                      "--output", path, *AUDIO)
        for drift in (-2, 2):
            with self.subTest(drift=drift):
                result = self.run_pump("rx", "--input", path, "--keyfile", self.key,
                                       "--time", EPOCH + drift, "--search-seconds", "2",
                                       "--json", "--progress", *AUDIO)
                packet = json.loads(result.stdout)
                self.assertEqual(packet["timestamp"], EPOCH)
                self.assertTrue(packet["authenticated"])
                self.assertEqual(base64.b64decode(packet["data_base64"]).decode(), text)
                self.assertIn(f"Searching epoch {EPOCH}".encode(), result.stderr)
        self.run_pump("rx", "--input", path, "--keyfile", self.key, "--time", EPOCH + 3,
                      "--search-seconds", "2", *AUDIO, ok=False)
        self.run_pump("rx", "--input", path, "--keyfile", self.other_key, "--time", EPOCH,
                      "--search-seconds", "0", *AUDIO, ok=False)

    def test_mac_rejects_ciphertext_malleability_and_downgrade(self):
        text = b"attacker knows this plaintext"
        packed = self.run_pump("pack", "--keyfile", self.key, "--time", EPOCH,
                               "--fec", "off", "--no-compression", data=text).stdout
        result = self.run_pump("unpack", "--keyfile", self.key, "--time", EPOCH, "--json", data=packed)
        packet = json.loads(result.stdout)
        self.assertTrue(packet["authenticated"])
        self.assertEqual(base64.b64decode(packet["data_base64"]), text)
        self.assertNotIn(text, packed)
        for offset in (72 + 26, len(packed) - 1):
            # AES-CTR lets an attacker flip a chosen known-plaintext bit. With
            # body FEC off this reaches MAC verification and must be rejected.
            modified = bytearray(packed)
            modified[offset] ^= 1
            failure = self.run_pump("unpack", "--keyfile", self.key, "--time", EPOCH, data=modified, ok=False)
            self.assertIn(b"authentication failed", failure.stderr)
        self.run_pump("unpack", "--keyfile", self.other_key, "--time", EPOCH, data=packed, ok=False)
        self.run_pump("unpack", "--keyfile", self.key, "--time", EPOCH + 1, data=packed, ok=False)
        self.run_pump("unpack", data=packed, ok=False)
        plaintext_packet = self.run_pump("pack", data=text).stdout
        self.run_pump("unpack", "--keyfile", self.key, "--time", EPOCH, data=plaintext_packet, ok=False)


if __name__ == "__main__":
    unittest.main()

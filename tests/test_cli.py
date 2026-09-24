"""Exercise the installed CLI boundary, not only its implementation functions."""
import base64
import errno
import json
import math
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
    def run_pump(self, *args, data=None, ok=True, env=None):
        result = subprocess.run([PUMP, *map(str, args)], input=data, capture_output=True, timeout=90, env=env)
        self.assertEqual(result.returncode, 0 if ok else 2,
                         f"pump {' '.join(map(str, args))}: {result.stderr.decode(errors='replace')}")
        if not ok:
            self.assertEqual(result.stdout, b"", "failed commands must not release partial content")
        return result


class CommandTests(PumpCase):
    def test_three_pattern_bits_without_framing(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "three-bits.wav"
            self.run_pump("status-tx", "--bits", "001", "--time", EPOCH, "--output", path)
            plan = json.loads(self.run_pump("estimate", "--text", "e").stdout)
            with wave.open(str(path), "rb") as wav:
                symbol_samples = round(plan["symbol_seconds"] * wav.getframerate())
                hardware_symbols = (5 * wav.getframerate() + symbol_samples // 2) // symbol_samples
                self.assertEqual(wav.getnframes(), (hardware_symbols + 3) * symbol_samples)
            for comparison, matches in (("001", True), ("110", False)):
                received = json.loads(self.run_pump("status-rx", "--bits", comparison,
                    "--time", EPOCH, "--search-seconds", "0", "--input", path).stdout)
                self.assertEqual(received["raw_bits"], "001")
                self.assertEqual(received["raw_bit_count"], 3)
                self.assertEqual(received["known_bits_match"], matches)
                self.assertFalse(received["packet_validated"])
                self.assertFalse(received["authenticated"])
                self.assertGreater(received["pattern_score"], 0)
                self.assertEqual(received["pattern_score_units"], "model log evidence")

    def test_receive_target_list_is_separate_from_transmit(self):
        baseline = json.loads(self.run_pump("estimate", "--text", "e").stdout)
        for targets in ("40, +6, -6, 40", "40,,6", "nan", "201", ""):
            result = self.run_pump("estimate", "--text", "e", "--receive-targets", targets)
            self.assertEqual(json.loads(result.stdout), baseline)
            if targets != "40, +6, -6, 40":
                self.assertIn(b"reset to 40 dB-Hz", result.stderr)
        self.run_pump("estimate", "--text", "e", "--spreading", "64", "--receive-targets", "40", ok=False)

    def test_help_and_invalid_options(self):
        self.assertIn(b"simulate", self.run_pump("--help").stdout)
        self.assertEqual(self.run_pump("--version").stdout, b"Data Pump 0.7.2\n")
        self.run_pump("simulate", "--text", "x", "--nonsense", "yes", ok=False)
        self.run_pump("simulate", "--text", "x", "--snr", "nan", ok=False)
        self.run_pump("tx", "--text", "x", ok=False)
        self.run_pump("simulate", "--text", "x", "--bw", "0", ok=False)
        for options in (("--sample-rate", "0"), ("--sample-rate", "120000001"),
                        ("--spreading", "0"), ("--spreading", "16385"),
                        ("--carrier", "nan"), ("--carrier", "inf"),
                        ("--fec", "21"), ("--memory-mb", "4097"),
                        ("--memory-mb", "-1"), ("--memory-mb", "1.5"),
                        ("--cache-mb", "0"), ("--dsp-mb", "0"),
                        ("--time", "18446744073709551616"),
                        ("--scramble",), ("--dsss",),
                        ("--text", "duplicate"), ("--json=true",)):
            with self.subTest(options=options):
                self.run_pump("pack", "--text", "x", *options, ok=False)
        self.run_pump("pack", "--text", "x", "--input", "-", data=b"ambiguous", ok=False)
        self.run_pump("simulate", "--text", "x", "--device", "default", ok=False)
        self.run_pump("simulate", "--text", "x", "--search-seconds", "121", ok=False)
        self.run_pump("simulate", "--text", "x", "--clock-error-ppm", "nan", ok=False)
        self.run_pump("simulate", "--text", "x", "--phase-noise", "-1", ok=False)
        self.run_pump("tx", "--text", "x", "--receiver-time", EPOCH, ok=False)
        self.run_pump("simulate", "--text", "x", "--receiver-time", "-1", ok=False)

    def test_bandwidth_clock_and_impaired_channel(self):
        default=json.loads(self.run_pump("estimate","--text","x").stdout)
        requested=json.loads(self.run_pump("estimate","--text","x","--target-snr","40").stdout)
        for field in ("sample_rate","carrier_hz","bit_rate","constellation_bits","spreading","symbol_seconds"):
            self.assertEqual(default[field],requested[field],"default CLI must use automatic tuning")
        self.assertTrue(default["target_supported"])
        manual=json.loads(self.run_pump("estimate","--text","x","--spreading","1").stdout)
        self.assertEqual(manual["spreading"],1)
        self.assertEqual(manual["constellation_bits"],1)
        for band, rate, carrier in (("100",6000,1500),("1.2kHz",6000,1500),("30MHz",120000000,22500000)):
            plan=json.loads(self.run_pump("estimate","--text","x","--bw",band,"--target-snr","110").stdout)
            self.assertEqual(plan["sample_rate"],rate)
            self.assertEqual(plan["carrier_hz"],carrier)
        manual=json.loads(self.run_pump("estimate","--text","x","--bw","100","--sample-rate","400","--carrier","75").stdout)
        self.assertEqual((manual["sample_rate"],manual["carrier_hz"]),(400,75))
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "low-rate.wav"
            self.run_pump("tx", "--text", "independent clock", "--bw", "100", "--output", path)
            with wave.open(str(path), "rb") as wav:
                self.assertEqual(wav.getframerate(), 6000)
            result = self.run_pump("rx", "--input", path, "--bw", "100", "--json")
            self.assertEqual(base64.b64decode(json.loads(result.stdout)["data_base64"]), b"independent clock")
        result = self.run_pump("simulate", "--text", "bad crystal", "--bw", "2400",
                               "--spreading", "64", "--snr", "30", "--json",
                               "--time", EPOCH, "--receiver-time", EPOCH + 2)
        self.assertEqual(base64.b64decode(json.loads(result.stdout)["data_base64"]), b"bad crystal")
        self.assertEqual(json.loads(result.stdout)["timestamp"], EPOCH + 2)
        self.run_pump("listen", "--bw", "30MHz", "--seconds", "0.1", ok=False)

    @unittest.skipUnless(sys.platform.startswith("linux"), "uses the deterministic ALSA fixture")
    def test_negotiated_audio_passband(self):
        fixture = pathlib.Path(PUMP).parent / "audio-test-lib"
        if not (fixture / "libasound.so.2").exists():
            self.skipTest("ALSA fixture is available in native build trees")
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(fixture) + (os.pathsep + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
        # The fake endpoint only supports 48 kHz. A 96 kHz modem clock therefore
        # negotiates 48 kHz, whose converter cannot carry the requested 30 kHz edge.
        wide = ("--bw", "24000", "--target-snr", "110", "--device", "default")
        for command in (("tx", "--text", "passband", "--tx-delay", "0"),
                        ("rx", "--seconds", "0.01"),
                        ("status-tx", "--bits", "101", "--tx-delay", "0")):
            with self.subTest(command=command[0]):
                result = self.run_pump(*command, *wide, env=env, ok=False)
                self.assertIn(b"exceeds this audio path's usable passband", result.stderr)
                self.assertIn(b"30000.000000", result.stderr)
        # A supported resampled band still plays, and WAV output is independent
        # of the endpoint even when its requested band is wider than hardware.
        self.run_pump("tx", "--text", "passband", "--bw", "1200", "--device", "default", "--tx-delay", "0", env=env)
        self.run_pump("status-tx", "--bits", "101", "--bw", "1200", "--device", "default", "--tx-delay", "0", env=env)
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "wide.wav"
            self.run_pump("tx", "--text", "passband", "--bw", "24000", "--target-snr", "110", "--output", path, env=env)
            with wave.open(str(path), "rb") as wav:
                self.assertEqual(wav.getframerate(), 96000)

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

    def test_word_uses_four_byte_bootstrap_without_rs(self):
        for mode in ("off", "20", "60"):
            with self.subTest(mode=mode):
                packet = self.run_pump("pack", "--text", "help", "--fec", mode).stdout
                self.assertEqual(packet[0] & 3, 0, "under16-byte input overrides requested RS")
                self.assertEqual(packet[1], 59, "one-byte canonical body length")
                self.assertEqual(len(packet), 4 + 24 + 3 + 32,
                                 "four-byte header and no header/body parity or tail")
                self.assertEqual(self.run_pump("unpack", data=packet).stdout, b"help")

    def test_repeatable_and_memory_limit(self):
        # Packet byte APIs retain metadata; tiny on-air text sends only dictionary bits.
        packed = self.run_pump("pack", "--text", "e", "--repeatable").stdout
        result = self.run_pump("unpack", "--json", data=packed)
        self.assertTrue(json.loads(result.stdout)["repeatable"])
        self.run_pump("simulate", "--text", "x", "--memory-mb", "0", ok=False)
        # Batch PCM memory must not limit a streamed transfer.
        self.run_pump("simulate", "--text", "x", "--memory-mb", "1")
        self.run_pump("pack", "--input", "-", "--cache-mb", "1",
                      data=b"x" * (1024 * 1024 + 1), ok=False)
        self.run_pump("rx", "--device-type", "ethernet", ok=False)

    def test_slow_pattern_estimate(self):
        slow = json.loads(self.run_pump("estimate", "--text", "!", "--bw", "2400",
                         "--target-snr", "-20", "--pattern", "auto-pattern", "--repeatable").stdout)
        self.assertTrue(slow["target_supported"])
        self.assertTrue(slow["memory_supported"])
        self.assertFalse(slow["batch_memory_supported"])
        self.assertTrue(slow["repeatable_allowed"])
        self.assertGreater(slow["symbol_seconds"], 5)
        self.assertEqual(slow["total_seconds"], slow["packet_seconds"])
        self.assertEqual(slow["constellation_bits"], 1)

    def test_content_capacity_excludes_packet_parity(self):
        payload = bytes(range(256)) * 4096
        packet = self.run_pump("pack", "--fec", "60", "--no-compression", "--cache-mb", "1", data=payload).stdout
        self.assertGreater(len(packet), len(payload))
        self.assertEqual(self.run_pump("unpack", "--cache-mb", "1", data=packet).stdout, payload)

    def test_automatic_tuning_and_estimate(self):
        normal = json.loads(self.run_pump("estimate", "--text", "hello", "--target-snr", "40").stdout)
        slow = json.loads(self.run_pump("estimate", "--text", "hello", "--target-snr", "6").stdout)
        self.assertGreater(slow["spreading"], normal["spreading"])
        self.assertGreater(slow["total_seconds"], normal["total_seconds"])
        settling_symbols = math.floor(5 / normal["symbol_seconds"] + .5)
        self.assertAlmostEqual(normal["total_seconds"] - normal["content_seconds"],
                               settling_symbols * normal["symbol_seconds"])
        self.assertEqual(normal["constellation_bits"], 1)
        self.assertGreaterEqual(normal["spreading"], 64)
        self.assertFalse(normal["repeatable_allowed"])
        compressed = json.loads(self.run_pump("estimate", "--text", "e" * 80).stdout)
        raw = json.loads(self.run_pump("estimate", "--text", "e" * 80, "--no-compression").stdout)
        self.assertLess(compressed["packet_bytes"], raw["packet_bytes"])
        self.run_pump("estimate", "--text", "x", "--pattern", "bad", ok=False)
        self.run_pump("estimate", "--text", "x", "--target-snr", "40", "--spreading", "2", ok=False)
        self.run_pump("estimate", "--text", "x", "--target-snr", "37.5", "--bw", "3000", "--sample-rate", "8000", ok=False)
        unsupported = self.run_pump("estimate", "--text", "!", "--target-snr", "-270", ok=False)
        self.assertIn(b"duration", unsupported.stderr)
        forced = json.loads(self.run_pump("estimate", "--text", "e", "--pattern", "pattern-3").stdout)
        self.assertEqual(forced["spreading"], 3)
        self.assertFalse(forced["target_supported"])
        result = self.run_pump("simulate", "--text", "pattern test", "--pattern", "auto-pattern",
                               "--simulation", "3dBm -120dB", "--json", "--bw", "1000")
        self.assertEqual(base64.b64decode(json.loads(result.stdout)["data_base64"]), b"pattern test")

    def test_continuous_simulation(self):
        # Tiny packets have no FEC; this lifecycle fixture needs a healthy channel.
        result = self.run_pump("listen", "--simulation", "3dBm -90dB", "--text", "stream packet content",
                               "--seconds", "10", "--json", "--progress", *AUDIO)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        frames = [event for event in events if event.get("event") == "signal"]
        self.assertGreater(len(frames), 10)
        self.assertGreater(frames[-1]["samples_received"], frames[2]["samples_received"])
        verified = [event for event in events if event.get("validated")]
        self.assertEqual(len(verified), 1)
        self.assertEqual(base64.b64decode(verified[0]["data_base64"]), b"stream packet content")

    def test_packet_boundaries_and_metadata(self):
        for length in (0, 1, 255, 256, 257):
            with self.subTest(length=length):
                data = bytes(index % 256 for index in range(length))
                packed = self.run_pump("pack", "--input", "-", data=data).stdout
                self.assertEqual(self.run_pump("unpack", "--input", "-", data=packed).stdout, data)
        data = b"a" * 100
        packet = self.run_pump("pack", "--input", "-", "--repeatable", "--spreading", "1", *AUDIO, data=data).stdout
        decoded = json.loads(self.run_pump("unpack", "--json", data=packet).stdout)
        self.assertEqual(base64.b64decode(decoded["data_base64"]), data)
        self.assertTrue(decoded["repeatable"])
        self.assertRegex(decoded["id"], r"^[0-9a-f]{32}$")
        self.run_pump("pack", "--repeatable", "--no-compression", "--spreading", "1", *AUDIO, data=b"a" * 65537, ok=False)
        compressed = self.run_pump("pack", "--repeatable", "--spreading", "1", *AUDIO, data=b"a" * 65537).stdout
        self.assertEqual(self.run_pump("unpack", data=compressed).stdout, b"a" * 65537)
        self.run_pump("pack", "--repeatable", "--spreading", "16384", data=b"!" )
        self.run_pump("pack", "--repeatable", "--spreading", "16384", data=b"!?", ok=False)
        self.run_pump("pack", "--cache-mb", "1", data=b"a" * (1024 * 1024 + 1), ok=False)
        self.run_pump("pack", "--memory-mb", "1", "--cache-mb", "1", data=b"a" * (200 * 1024))

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
                self.assertEqual(params.nframes, (39 + 3) * 1024, "status contains only rounded hardware settling and exact one-bit symbols")
            result = json.loads(self.run_pump("status-rx", "--bits", "010", "--input", status, *AUDIO).stdout)
            self.assertFalse(result["authenticated"])
            self.assertEqual(result["known_bits"], "010")
            self.assertTrue(result["known_bits_match"])
            self.assertEqual(result["raw_bits"], "010")
            self.assertGreater(result["pattern_score"], 0)
            with wave.open(str(truncated), "wb") as writer:
                writer.setparams(params)
                writer.writeframes(frames[:2])
            self.run_pump("status-rx", "--bits", "010", "--input", truncated, *AUDIO, ok=False)
            raw = json.loads(self.run_pump("rx", "--input", status, *AUDIO, "--json").stdout)
            self.assertEqual(raw["raw_bits"], "010")
            self.assertFalse(raw["validated"])
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
            result = subprocess.run([PUMP, "keygen", "--output", str(key), "--key-names", "Default,Backup"], capture_output=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors="replace"))

    def test_production_keyfile_and_exclusive_creation(self):
        self.assertEqual(self.key.stat().st_size, 128 * 1024 * 1024 + 48 + 16 + 2 * (2 + 160) + 7 + 6)
        if os.name != "nt":
            self.assertEqual(stat.S_IMODE(self.key.stat().st_mode), 0o600)
        with self.key.open("rb") as source:
            prefix = source.read(48)
            random_header_sample = source.read(1024)
        self.assertEqual(prefix[:8], b"DPMKEY02")
        self.assertEqual(int.from_bytes(prefix[8:16], "big"), 128 * 1024 * 1024)
        self.assertGreater(len(set(random_header_sample)), 128)
        self.run_pump("keygen", "--output", self.key, ok=False)
        with self.key.open("rb") as source:
            self.assertEqual(source.read(48), prefix)
        truncated = self.root / "truncated.key"
        truncated.write_bytes(prefix + random_header_sample)
        self.run_pump("pack", "--text", "x", "--keyfile", truncated, ok=False)
        self.run_pump("pack", "--text", "x", "--pad", self.key, ok=False)

    def test_named_key_selection(self):
        self.assertEqual(self.run_pump("keys", "--keyfile", self.key).stdout, b"Default\nBackup\n")
        packet = self.run_pump("pack", "--text", "second key", "--keyfile", self.key,
                               "--key-name", "Backup", "--time", EPOCH).stdout
        decoded = self.run_pump("unpack", "--keyfile", self.key, "--key-name", "Backup",
                                "--time", EPOCH, data=packet)
        self.assertEqual(decoded.stdout, b"second key")
        self.run_pump("unpack", "--keyfile", self.key, "--key-name", "Default", "--time", EPOCH, data=packet, ok=False)
        self.run_pump("pack", "--keyfile", self.key, "--key-name", "missing", "--text", "x", ok=False)

    def test_encrypted_noisy_simulation_all_layers(self):
        result = self.run_pump("simulate", "--text", "Secure café 🌍", "--keyfile", self.key,
                               "--time", EPOCH, "--scramble", "--dsss", "--spreading", "64",
                               "--snr", "18", "--delay-samples", "313", "--json", *AUDIO)
        packet = json.loads(result.stdout)
        self.assertTrue(packet["validated"])
        self.assertTrue(packet["authenticated"])
        self.assertEqual(packet["timestamp"], EPOCH)
        self.assertEqual(base64.b64decode(packet["data_base64"]), "Secure café 🌍".encode())

    def test_long_pseudorandom_pattern_simulation(self):
        # A keyed code provides frequent phase changes throughout integration.
        options = ("--text", "long pattern", "--bw", "2400", "--keyfile", self.key,
                   "--time", EPOCH, "--search-seconds", "0", "--scramble",
                   "--spreading", "1024", "--memory-mb", "1")
        estimate = json.loads(self.run_pump("estimate", *options).stdout)
        self.assertTrue(estimate["memory_supported"])
        self.assertFalse(estimate["batch_memory_supported"])
        result = self.run_pump("simulate", *options, "--json",
                               "--clock-error-ppm", "0", "--phase-noise", "0")
        packet = json.loads(result.stdout)
        self.assertFalse(packet["authenticated"], "short dictionary text has no authentication field")
        self.assertEqual(base64.b64decode(packet["data_base64"]), b"long pattern")

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

    def test_tone_forces_encryption_off(self):
        for mode in ("auto-tone", "tone-1", "tone-128"):
            args = ("--pattern", mode, "--time", EPOCH)
            plain = self.run_pump("pack", "--text", "tone plaintext", *args).stdout
            keyed = self.run_pump("pack", "--text", "tone plaintext", *args,
                                  "--keyfile", self.key).stdout
            for packet in (plain, keyed):
                decoded = json.loads(self.run_pump("unpack", "--json", data=packet).stdout)
                self.assertFalse(decoded["authenticated"])
                self.assertEqual(base64.b64decode(decoded["data_base64"]), b"tone plaintext")
        plain_path, keyed_path = self.root / "tone-plain.wav", self.root / "tone-keyed.wav"
        args = ("--pattern", "tone-128", "--time", EPOCH, "--bits", "001")
        self.run_pump("status-tx", *args, "--output", plain_path)
        forced = self.run_pump("status-tx", *args, "--keyfile", self.key, "--output", keyed_path)
        self.assertEqual(plain_path.read_bytes(), keyed_path.read_bytes())
        self.assertIn(b"Tone", forced.stderr)

    def test_mac_rejects_ciphertext_malleability_and_downgrade(self):
        text = b"attacker knows this plaintext"
        packed = self.run_pump("pack", "--keyfile", self.key, "--time", EPOCH,
                               "--fec", "off", "--no-compression", data=text).stdout
        result = self.run_pump("unpack", "--keyfile", self.key, "--time", EPOCH, "--json", data=packed)
        packet = json.loads(result.stdout)
        self.assertTrue(packet["authenticated"])
        self.assertEqual(base64.b64decode(packet["data_base64"]), text)
        self.assertNotIn(text, packed)
        for offset in (4 + 24, len(packed) - 1):
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

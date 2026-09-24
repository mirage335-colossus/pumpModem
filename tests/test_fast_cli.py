"""Fast CLI text/files with optional encryption and observed physical completion."""
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
        cls.plain_wave = cls.directory / "source-plain.wav"
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
        plain = subprocess.run(
            [PUMP, "fast-tx", "--input", str(cls.source), "--output", str(cls.plain_wave),
             *PROFILE, "--json"], capture_output=True, timeout=90)
        if plain.returncode:
            raise AssertionError(plain.stderr.decode(errors="replace"))
        cls.plain_tx = json.loads(plain.stdout)

    def test_classic_bulk_defaults(self):
        for profile, intervals in (("wire", 71), ("ssb", 22), ("fm", 22), ("acoustic", 7)):
            result = self.run_pump("fast-info", "--format", "classic", "--profile", profile)
            info = json.loads(result.stdout)
            self.assertEqual(info["cycle_intervals"], intervals)
            self.assertEqual(info["source_bytes_per_group"], 208 if profile == "wire" else 192)
            self.assertEqual(info["constellation"], {"wire": 256, "ssb": 16, "fm": 4, "acoustic": 4}[profile])
            self.assertEqual(info["amplitude"], 0.35 if profile in ("wire", "acoustic") else 0.5)
            self.assertTrue(info["mono"])
            self.assertEqual(info["audio_channels"], "left")

    def test_capacity_bulk_default(self):
        info = json.loads(self.run_pump("fast-info", "--estimate-bytes", "50000000").stdout)
        self.assertEqual(info["format"], "capacity")
        self.assertEqual(info["constellation"], 4194304)
        self.assertEqual(info["code_rate"], "8/9")
        self.assertEqual(info["marker_spacing_intervals"], 16)
        self.assertEqual(info["cycle_intervals"], 127)
        self.assertAlmostEqual(info["occupied_bandwidth_hz"], 18000, delta=1)
        self.assertEqual(info["symbol_rate"], 18000 / 1.02)
        self.assertLess(info["rs_parity_data_ratio"], 0.0031)
        self.assertGreater(info["estimated_source_bps"], 310000)

    def test_local_output_routing(self):
        for profile in ("wire", "ssb", "fm", "acoustic", "acoustic-short"):
            for flag, mono, channels in (("--mono", True, "left"), ("--right-mono", True, "right"), ("--stereo", False, "stereo")):
                report = json.loads(self.run_pump("fast-info", "--profile", profile, flag).stdout)
                self.assertEqual(report["mono"], mono)
                self.assertEqual(report["audio_channels"], channels)
        self.run_pump("fast-info", "--mono", "--stereo", ok=False)
        self.run_pump("fast-info", "--mono", "--right-mono", ok=False)
        self.run_pump("fast-info", "--right-mono", "--stereo", ok=False)

    def test_short_acoustic_profile_is_separate(self):
        options = ("--expected-snr", "3", "--estimate-bytes", "2560")
        existing = json.loads(self.run_pump("fast-info", "--profile", "acoustic", *options).stdout)
        short = json.loads(self.run_pump("fast-info", "--profile", "acoustic-short", *options).stdout)
        self.assertEqual(short["profile"], "acoustic-short")
        self.assertEqual(short["format"], "capacity")
        self.assertEqual(short["waveform"], "ofdm")
        self.assertEqual(short["expected_snr_db"], 3)
        self.assertTrue(short["snr_preset_unmodified"])
        self.assertEqual(short["ldpc_blocks_per_cycle"], 1)
        self.assertEqual(short["ldpc_frame_bits"], 16200)
        self.assertEqual(short["ofdm_training_blocks"], 6)
        self.assertLess(short["estimated_seconds"], existing["estimated_seconds"])
        self.assertEqual(existing["ldpc_blocks_per_cycle"], 2)
        self.assertNotIn("ldpc_frame_bits", existing)
        for rate, systematic_bits in (("1/2", 7200), ("2/3", 10800), ("3/4", 11880)):
            report = json.loads(self.run_pump("fast-info", "--profile", "acoustic-short", "--code-rate", rate).stdout)
            parity = report["rs_parity_bytes"]
            aligned_bytes = (systematic_bits // 8) & ~1
            self.assertAlmostEqual(report["rs_parity_data_ratio"], parity / (aligned_bytes - parity))
        for rate in ("7/9", "8/9", "9/10"):
            self.run_pump("fast-info", "--profile", "acoustic-short", "--code-rate", rate, ok=False)
        classic = json.loads(self.run_pump("fast-info", "--profile", "acoustic-short", "--format", "classic").stdout)
        self.assertEqual(classic["profile"], "acoustic-short")
        self.assertEqual(classic["format"], "classic")
        self.assertEqual(classic["symbol_rate"], 500)

    def test_weak_short_acoustic_minimum(self):
        args = ("--profile", "acoustic-short", "--expected-snr", "-6")
        info = json.loads(self.run_pump("fast-info", *args, "--estimate-bytes", "60").stdout)
        self.assertEqual(info["waveform"], "single-carrier")
        self.assertEqual(info["code_rate"], "3/4")
        self.assertEqual(info["ldpc_blocks_per_cycle"], 1)
        self.assertEqual(info["ldpc_frame_bits"], 1944)
        self.assertEqual(info["source_bytes_per_cycle"], 145)
        self.assertEqual(info["cycle_intervals"], 1)
        self.assertEqual(info["estimated_intervals"], 2)
        self.assertLessEqual(info["estimated_seconds"], 10.5)
        self.assertGreater(info["estimated_seconds"], 6)
        self.assertNotIn("inner_code", info)
        self.assertTrue(info["snr_preset_unmodified"])
        stronger = json.loads(self.run_pump("fast-info", *args, "--code-rate", "2/3").stdout)
        self.assertEqual(stronger["code_rate"], "2/3")
        self.assertEqual(stronger["ldpc_frame_bits"], 1944)
        self.assertFalse(stronger["snr_preset_unmodified"])
        for rate in ("7/9", "8/9", "9/10"):
            self.run_pump("fast-info", *args, "--code-rate", rate, ok=False)
        self.run_pump("fast-info", *args, "--interleave", "17", ok=False)
        self.run_pump("fast-info", *args, "--qam", "16", ok=False)

    def test_explicit_acoustic_capacity_profile(self):
        legacy = json.loads(self.run_pump("fast-info", "--profile", "acoustic", "--format", "classic").stdout)
        self.assertEqual(legacy["format"], "classic")
        self.assertEqual(legacy["symbol_rate"], 500)
        info = json.loads(self.run_pump("fast-info", "--profile", "acoustic",
                                      "--format", "capacity").stdout)
        self.assertEqual(info["profile"], "acoustic")
        self.assertEqual(info["format"], "capacity")
        self.assertEqual(info["constellation"], 16)
        self.assertEqual(info["code_rate"], "3/4")
        self.assertEqual(info["ldpc_blocks_per_cycle"], 8)
        self.assertEqual(info["symbol_rate"], 48000 / (32768 + 4096))
        self.assertEqual(info["waveform"], "ofdm")
        self.assertEqual(info["ofdm_fft_size"], 32768)
        self.assertEqual(info["ofdm_prefix_samples"], 4096)
        self.assertGreater(info["occupied_bandwidth_hz"], 17480)
        self.assertLessEqual(info["occupied_bandwidth_hz"], 17500)
        self.assertEqual(info["amplitude"], .40)
        self.assertNotIn("marker_spacing_intervals", info)
        self.assertNotIn("pilot_spacing_symbols", info)
        self.assertTrue(info["mono"])
        implied = json.loads(self.run_pump("fast-info", "--profile", "acoustic",
                                         "--qam", "16").stdout)
        self.assertEqual(implied, info)
        self.assertEqual(json.loads(self.run_pump("fast-info", "--profile", "acoustic").stdout), info)
        self.assertEqual(info["ofdm_pilot_stride"], 16)
        dense = json.loads(self.run_pump("fast-info", "--profile", "acoustic",
                                        "--qam", "64", "--stereo").stdout)
        self.assertEqual(dense["constellation"], 64)
        self.assertEqual(dense["code_rate"], "3/4")
        self.assertEqual(dense["waveform"], "ofdm")
        self.assertFalse(dense["mono"])
        half = json.loads(self.run_pump("fast-info", "--profile", "acoustic",
                                      "--format", "capacity", "--code-rate", "1/2", "--interleave", "1").stdout)
        self.assertEqual(half["profile"], "acoustic")
        self.assertEqual(half["code_rate"], "1/2")
        self.assertEqual(half["rs_parity_bytes"], 16)
        self.assertAlmostEqual(half["rs_parity_data_ratio"], 16 / 4034, delta=5e-9)
        two_thirds = json.loads(self.run_pump("fast-info", "--profile", "acoustic",
            "--format", "capacity", "--code-rate", "2/3", "--interleave", "4").stdout)
        self.assertEqual(two_thirds["code_rate"], "2/3")
        self.assertEqual(two_thirds["rs_parity_bytes"], 68)
        self.assertEqual(two_thirds["source_bytes_per_cycle"], 21499)
        self.run_pump("fast-info", "--format", "classic", "--code-rate", "2/3", ok=False)

    def test_radio_capacity_defaults(self):
        for profile in ("ssb", "fm"):
            info = json.loads(self.run_pump("fast-info", "--profile", profile,
                                          "--estimate-bytes", "50000000").stdout)
            self.assertEqual(info["format"], "capacity")
            self.assertEqual(info["constellation"], 64)
            self.assertEqual(info["code_rate"], "3/4")
            self.assertAlmostEqual(info["occupied_bandwidth_hz"], 2400)
            self.assertAlmostEqual(info["symbol_rate"], 2400 / 1.1)
            self.assertGreater(info["estimated_source_bps"], 8600)
            self.assertEqual(info["snr_reference_bandwidth_hz"], 2400)
            self.assertEqual(info["expected_snr_db"], 20)

    def test_expected_snr_presets_and_overrides(self):
        for profile, low, reference in (("wire", 25, 18000), ("acoustic", -27, 17500),
                                         ("ssb", -20, 2400), ("fm", -20, 2400)):
            with self.subTest(profile=profile):
                args = ("--profile", profile, "--expected-snr", low)
                info = json.loads(self.run_pump("fast-info", *args).stdout)
                self.assertEqual(info["expected_snr_db"], low)
                self.assertEqual(info["snr_reference_bandwidth_hz"], reference)
                self.assertTrue(info["snr_preset_unmodified"])
                self.assertLessEqual(info["occupied_bandwidth_hz"], reference)
                if profile != "wire":
                    self.assertLess(info["symbol_rate"], 10)
                    self.assertEqual(info["waveform"], "single-carrier")
                    self.assertGreaterEqual(info["selected_band_snr_db_assumed"], 12.99)
                manual = json.loads(self.run_pump("fast-info", "--qam", "4", *args).stdout)
                self.assertEqual(manual["constellation"], 4)
                self.assertEqual(manual["symbol_rate"], info["symbol_rate"])
        for args in (("--format", "classic", "--expected-snr", "20"),
                     ("--profile", "acoustic", "--expected-snr", "14"),
                     ("--profile", "fm", "--expected-snr", "-21"),
                     ("--expected-snr", "nan")):
            self.run_pump("fast-info", *args, ok=False)

    def test_acoustic_ofdm_geometry_validation(self):
        acoustic = ("--profile", "acoustic", "--format", "capacity")
        for name, value in (("ofdm-fft", "3000"), ("ofdm-prefix", "0"),
                            ("ofdm-prefix", "65536"), ("ofdm-low", "19000"),
                            ("ofdm-high", "24000"), ("ofdm-pilots", "1"),
                            ("ofdm-pilots", "33"), ("sample-rate", "44100"),
                            ("symbol-rate", "2000"), ("marker-spacing", "4")):
            with self.subTest(name=name, value=value):
                self.run_pump("fast-info", *acoustic, "--" + name, value, ok=False)
        self.run_pump("fast-info", "--profile", "wire", "--ofdm-fft", "8192", ok=False)
        info = json.loads(self.run_pump("fast-info", *acoustic, "--ofdm-fft", "16384",
                                       "--ofdm-prefix", "2048", "--estimate-bytes", "100000").stdout)
        self.assertEqual(info["ofdm_fft_size"], 16384)
        self.assertGreater(info["estimated_seconds"], 6.25)

    def test_capacity_source_and_geometry(self):
        # Exercise the public CLI, S16 WAV, LDPC/RS, source endpoint, and an
        # independent source-byte comparison rather than only codec roundtrips.
        source = self.directory / "capacity-source.bin"
        payload = bytes(range(256)) * 17 + bytes(1000)
        source.write_bytes(payload)
        for order, rate, keyed in ((4096, "7/9", False), (16384, "9/10", True)):
            options = ("--format", "capacity", "--qam", str(order), "--code-rate", rate)
            keys = self.key_options if keyed else ()
            info = json.loads(self.run_pump("fast-info", *options, *keys,
                                          "--estimate-bytes", len(payload)).stdout)
            self.assertEqual(info["format"], "capacity")
            self.assertEqual(info["cycle_intervals"], 127)
            self.assertEqual(info["ldpc_blocks_per_cycle"], 4)
            self.assertLess(info["rs_parity_data_ratio"], .0031)
            self.assertGreaterEqual(info["rs_parity_data_ratio"], .003)
            wave = self.directory / f"capacity-{order}.wav"
            out = self.directory / f"capacity-{order}.bin"
            self.run_pump("fast-tx", *options, *keys, "--input", source, "--output", wave)
            self.assertEqual((wave.stat().st_size - 44) // 2, info["estimated_samples"])
            received = json.loads(self.run_pump("fast-rx", *options, *keys,
                                              "--input", wave, "--save", out, "--json").stdout)
            self.assertTrue(received["complete"] and received["physical_complete"])
            self.assertEqual(received["authenticated"], keyed)
            self.assertEqual(out.read_bytes(), payload)
        for args in (("--format", "capacity", "--apsk", "256"),
                     ("--format", "classic", "--qam", "4096"),
                     ("--format", "capacity", "--interleave", "17"),
                     ("--format", "capacity", "--code-rate", "7/8"),
                     ("--format", "capacity", "--rs", "high-rate"),
                     ("--qam", "4096", "--marker-spacing", "0")):
            self.run_pump("fast-info", *args, ok=False)

    def test_capacity_dense_qam_s16_wav(self):
        # Keep the production baud, rolloff, pilot/marker spacing and depth.
        # Both the bootstrap and final source cycle must survive actual S16
        # quantization at each advertised audio rate, with either integrity mode.
        source = self.directory / "dense-source.bin"
        payload = bytes(range(98)) + b"\x80\x00"
        source.write_bytes(payload)
        for order, rate in ((1048576, "9/10"), (4194304, "8/9")):
            for sample_rate in (48000, 44100):
                for keyed in (False, True):
                    with self.subTest(order=order, rate=rate, sample_rate=sample_rate, keyed=keyed):
                        options = ("--format", "capacity", "--qam", order,
                                   "--code-rate", rate, "--sample-rate", sample_rate)
                        keys = self.key_options if keyed else ()
                        name = f"dense-{order}-{sample_rate}-{keyed}"
                        wave = self.directory / f"{name}.wav"
                        out = self.directory / f"{name}.bin"
                        info = json.loads(self.run_pump("fast-info", *options, *keys,
                                                      "--estimate-bytes", len(payload)).stdout)
                        self.run_pump("fast-tx", *options, *keys,
                                      "--input", source, "--output", wave)
                        with wave.open("rb") as stream:
                            header = stream.read(44)
                        self.assertEqual(struct.unpack_from("<HHIIHH", header, 20),
                                         (1, 1, sample_rate, sample_rate * 2, 2, 16))
                        self.assertEqual((wave.stat().st_size - 44) // 2, info["estimated_samples"])
                        received = json.loads(self.run_pump("fast-rx", *options, *keys,
                                                          "--input", wave, "--save", out, "--json").stdout)
                        self.assertTrue(received["complete"] and received["physical_complete"])
                        self.assertEqual(received["encrypted"], keyed)
                        self.assertEqual(received["authenticated"], keyed)
                        self.assertEqual(received["ldpc_frames"], 8)
                        self.assertEqual(received["ldpc_failed_frames"], 0)
                        self.assertEqual(received["source_bytes"], len(payload))
                        self.assertEqual(out.read_bytes(), payload)

    def test_capacity_rs_ratio_excludes_alignment_byte(self):
        info = json.loads(self.run_pump("fast-info", "--format", "capacity",
                                      "--code-rate", "3/4", "--interleave", "1").stdout)
        # K=48600 gives 6075 systematic bytes. GF(65536) uses 6074 bytes:
        # 20 parity bytes and 6054 data bytes, with one separate alignment byte.
        self.assertEqual(info["rs_parity_bytes"], 20)
        self.assertAlmostEqual(info["rs_parity_data_ratio"], 20 / 6054, delta=5e-9)

    def run_pump(self, *args, ok=True):
        result = subprocess.run([PUMP, *map(str, args)], capture_output=True, timeout=90)
        self.assertEqual(result.returncode, 0 if ok else 2,
                         result.stderr.decode(errors="replace") + result.stdout.decode(errors="replace"))
        return result

    def test_profiles_and_fixed_geometry(self):
        for profile in ("wire", "ssb", "fm", "acoustic"):
            with self.subTest(profile=profile):
                report = json.loads(self.run_pump("fast-info", "--format", "classic", "--profile", profile).stdout)
                self.assertEqual(report["profile"], profile)
                self.assertEqual(report["physical_interval_bits"], 2048)
                self.assertGreater(report["cycle_intervals"], 0)
                self.assertIn(report["ciphertext_bytes"], (176, 192))
                self.assertFalse(report["encrypted"])
                self.assertEqual(report["source_bytes_per_group"], 208 if profile == "wire" else 192)
                self.assertNotIn("packet_bytes", report)
        for modulation in (4, 16, 64, 256):
            report = json.loads(self.run_pump("fast-info", "--apsk", modulation).stdout)
            self.assertEqual(report["constellation"], modulation)
            self.assertEqual(report["physical_interval_bits"], 2048)
        cycles = []
        for rate in ("1/2", "3/4", "7/8"):
            report = json.loads(self.run_pump("fast-info", "--format", "classic", "--code-rate", rate, "--interleave", "16").stdout)
            cycles.append(report["cycle_intervals"])
        self.assertEqual(cycles, [33, 22, 19])
        encrypted = json.loads(self.run_pump("fast-info", "--format", "classic", *self.key_options).stdout)
        self.assertTrue(encrypted["encrypted"])
        self.assertEqual(encrypted["source_bytes_per_group"], 192)
        public = json.loads(self.run_pump("fast-info", "--format", "classic", *self.key_options, "--no-encryption", "--rs", "high-rate").stdout)
        self.assertFalse(public["encrypted"])
        self.assertEqual(public["source_bytes_per_group"], 208)

    def test_help_and_invalid_local_options(self):
        help_text = self.run_pump("fast-tx", "--help").stdout
        self.assertIn(b"2048", help_text)
        self.assertIn(b"EOF/cancellation is not physical end", help_text)
        self.assertIn(b"--mono", help_text)
        self.assertIn(b"--stereo", help_text)
        self.assertIn(b"acoustic-short", help_text)
        for option, value in (("profile", "unknown"), ("apsk", "32"),
                              ("interleave", "0"), ("interleave", "65"),
                              ("sample-rate", "8000"), ("sample-rate", "192001"),
                              ("code-rate", "2/5"), ("rs", "off"),
                              ("quota-mb", "0"), ("quota-mb", "16385"),
                              ("apsk", "18446744073709551616")):
            with self.subTest(option=option, value=value):
                self.run_pump("fast-info", "--" + option, value, ok=False)
        self.run_pump("fast-info", "--snr", "40", ok=False)
        self.run_pump("fast-info", "--apsk", "4", "--apsk", "4", ok=False)
        self.run_pump("fast-info", "--interleave", ok=False)
        self.run_pump("fast-tx", "--input", self.source, "--output", self.directory / "no-key.wav", "--encrypt", ok=False)
        self.run_pump("fast-info", "--encrypt", ok=False)
        self.run_pump("fast-info", "--encrypt", "--no-encryption", *self.key_options, ok=False)
        self.run_pump("fast-info", "--key-name", "Fast", ok=False)
        self.run_pump("fast-info", "--pad", self.directory / "orphan.pad", ok=False)
        ignored = json.loads(self.run_pump("fast-info", "--no-encryption", "--key-name", "ignored",
                                         "--pad", self.directory / "ignored.pad").stdout)
        self.assertFalse(ignored["encrypted"])
        self.run_pump("fast-tx", "--text", "one", "--input", self.source,
                      "--output", self.directory / "two-sources.wav", ok=False)
        self.run_pump("fast-tx", "--output", self.directory / "no-source.wav", ok=False)
        self.run_pump("fast-rx", "--input", self.wave, "--text", "invalid", ok=False)
        self.run_pump("fast-tx", "--text", "x" * 32769,
                      "--output", self.directory / "too-large.wav", ok=False)
        self.run_pump("fast-tx", "--text", "é" * 16385,
                      "--output", self.directory / "utf8-too-large.wav", ok=False)

    def test_streamed_encrypted_wav_and_explicit_save(self):
        self.assertGreaterEqual(self.key.stat().st_size, 128 * 1024 * 1024)
        self.assertEqual(self.tx["source_bytes"], len(SOURCE_BYTES))
        self.assertFalse(self.tx["complete"])
        self.assertTrue(self.tx["encrypted"])
        destination = self.directory / "received.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", self.wave,
            "--save", destination, *self.key_options, *PROFILE, "--json").stdout)
        self.assertTrue(result["physical_complete"])
        self.assertTrue(result["complete"])
        self.assertFalse(result["cancelled"])
        self.assertEqual(result["source_bytes"], len(SOURCE_BYTES))
        self.assertEqual(destination.read_bytes(), SOURCE_BYTES)
        self.assertTrue(result["is_attachment"])
        self.assertEqual(result["filename"], self.source.name)
        self.assertGreater(result["authenticated_groups"], 0)
        self.assertTrue(result["encrypted"])
        self.assertTrue(result["authenticated"])
        self.assertEqual(result["checksum_groups"], 0)
        self.assertFalse(result["decoding_stopped"])
        self.assertEqual(result["failed_cycles"], 0)
        self.assertGreater(result["coding_cycles"], 1)
        self.assertGreaterEqual(result["verified_bytes"], len(SOURCE_BYTES))
        self.assertNotIn("text_preview", result)
        self.assertGreater(result["intervals"], 0)

    def test_plaintext_file_and_ignored_key_options(self):
        self.assertFalse(self.plain_tx["encrypted"])
        destination = self.directory / "received-plain.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", self.plain_wave,
            "--save", destination, "--no-encryption", "--keyfile", self.directory / "does-not-exist.key",
            "--key-name", "ignored", "--pad", self.directory / "does-not-exist.pad",
            *PROFILE, "--json").stdout)
        self.assertTrue(result["physical_complete"])
        self.assertTrue(result["complete"])
        self.assertFalse(result["encrypted"])
        self.assertFalse(result["authenticated"])
        self.assertEqual(result["authenticated_groups"], 0)
        self.assertGreater(result["checksum_groups"], 0)
        self.assertFalse(result["decoding_stopped"])
        self.assertEqual(result["failed_cycles"], 0)
        self.assertGreater(result["coding_cycles"], 1)
        self.assertGreaterEqual(result["verified_bytes"], len(SOURCE_BYTES))
        self.assertEqual(destination.read_bytes(), SOURCE_BYTES)
        self.assertTrue(result["is_attachment"])
        self.assertEqual(result["filename"], self.source.name)
        human = self.run_pump("fast-rx", "--input", self.plain_wave, *PROFILE).stdout
        human.decode("utf-8")  # Invalid source bytes must not corrupt console text.
        self.assertNotIn(b"\x1b", human)
        self.assertNotIn(b"\xff", human)
        self.assertNotIn(b"\\x1b", human)

    def test_received_attachment_name_uses_restricted_ascii(self):
        source = self.directory / "payload;(x)&caf\u00e9.bin"
        source.write_bytes(SOURCE_BYTES)
        wave = self.directory / "restricted-name.wav"
        destination = self.directory / "restricted-name-saved.bin"
        self.run_pump("fast-tx", "--input", source, "--output", wave, *PROFILE)
        result = json.loads(self.run_pump("fast-rx", "--input", wave, "--save", destination,
                                        *PROFILE, "--json").stdout)
        self.assertTrue(result["complete"])
        self.assertEqual(result["filename"], "payload__x__caf__.bin")
        self.assertEqual(destination.read_bytes(), SOURCE_BYTES)

    def test_text_utf8_newlines_and_empty_text_in_both_modes(self):
        for encrypted in (False, True):
            options = (*self.key_options, "--encrypt") if encrypted else ()
            for index, text in enumerate(("Hello, pump!\nΚαλημέρα\t世界 🙂\n", "")):
                with self.subTest(encrypted=encrypted, text=text):
                    wave = self.directory / f"text-{encrypted}-{index}.wav"
                    destination = self.directory / f"text-{encrypted}-{index}.txt"
                    tx = json.loads(self.run_pump("fast-tx", "--text", text,
                        "--output", wave, *options, *PROFILE, "--json").stdout)
                    self.assertEqual(tx["source_bytes"], len(text.encode("utf-8")))
                    self.assertEqual(tx["encrypted"], encrypted)
                    result = json.loads(self.run_pump("fast-rx", "--input", wave,
                        "--save", destination, *options, *PROFILE, "--json").stdout)
                    self.assertTrue(result["complete"])
                    self.assertEqual(result["authenticated"], encrypted)
                    self.assertEqual(result["encrypted"], encrypted)
                    self.assertNotIn("text_preview", result)
                    self.assertEqual(destination.read_bytes(), text.encode("utf-8"))
                    self.assertFalse(result["is_attachment"])
                    self.assertEqual(result["filename"], "")
        plain_text = self.directory / "text-False-0.wav"
        human = self.run_pump("fast-rx", "--input", plain_text, *PROFILE).stdout.decode("utf-8")
        self.assertNotIn("Received text (escaped):", human)
        self.assertNotIn("Hello, pump!\\n", human)
        self.assertIn("encryption off", human)
        self.assertIn("checksum", human.lower())
        self.assertNotIn("; authenticated", human)

    def test_attachment_marker_only_at_byte_zero_and_empty_file(self):
        marker = "#ATTACHMENT### café.bin #ATTACHMENT### "
        for index, text in enumerate((marker + "payload", "prefix " + marker + "payload",
                                     "#ATTACHMENT### ../bad #ATTACHMENT### payload")):
            wave = self.directory / f"attachment-marker-{index}.wav"
            destination = self.directory / f"attachment-marker-{index}.bin"
            self.run_pump("fast-tx", "--text", text, "--output", wave, *PROFILE)
            result = json.loads(self.run_pump("fast-rx", "--input", wave, "--save", destination,
                                             *PROFILE, "--json").stdout)
            self.assertEqual(result["is_attachment"], index == 0)
            self.assertEqual(result["filename"], "caf__.bin" if index == 0 else "")
            self.assertEqual(destination.read_bytes(), b"payload" if index == 0 else text.encode("utf-8"))
        source = self.directory / "empty-attachment.bin"
        source.write_bytes(b"")
        wave = self.directory / "empty-attachment.wav"
        destination = self.directory / "empty-attachment-out.bin"
        tx = json.loads(self.run_pump("fast-tx", "--input", source, "--output", wave, *PROFILE, "--json").stdout)
        result = json.loads(self.run_pump("fast-rx", "--input", wave, "--save", destination,
                                         *PROFILE, "--json").stdout)
        self.assertEqual(tx["source_bytes"], 0)
        self.assertEqual(result["source_bytes"], 0)
        self.assertTrue(result["is_attachment"])
        self.assertEqual(result["filename"], source.name)
        self.assertEqual(destination.read_bytes(), b"")

    def test_explicit_plaintext_tx_without_preview(self):
        text = "x" * 4095 + "🙂 trailing text\n"
        wave = self.directory / "long-text.wav"
        tx = json.loads(self.run_pump("fast-tx", "--text", text, "--output", wave,
            "--no-encryption", "--keyfile", self.directory / "missing.key", *PROFILE, "--json").stdout)
        self.assertFalse(tx["encrypted"])
        result = json.loads(self.run_pump("fast-rx", "--input", wave, *PROFILE, "--json").stdout)
        self.assertTrue(result["complete"])
        self.assertNotIn("text_preview", result)
        self.assertNotIn("preview_truncated", result)

    def test_encryption_mismatch_never_falls_back(self):
        for wave, options, name in (
            (self.wave, (), "encrypted-as-plain"),
            (self.plain_wave, self.key_options, "plain-as-encrypted"),
        ):
            destination = self.directory / (name + ".bin")
            result = json.loads(self.run_pump("fast-rx", "--input", wave, "--save", destination,
                *options, *PROFILE, "--json", ok=False).stdout)
            self.assertFalse(result["complete"])
            self.assertFalse(result["authenticated"])
            self.assertNotIn("text_preview", result)
            self.assertFalse(destination.exists())

    def test_wrong_key_never_saves(self):
        destination = self.directory / "wrong-key.bin"
        result = json.loads(self.run_pump("fast-rx", "--input", self.wave,
            "--keyfile", self.key, "--key-name", "Other", "--save", destination,
            *PROFILE, "--json", ok=False).stdout)
        self.assertFalse(result["complete"])
        self.assertFalse(result["authenticated"])
        self.assertTrue(result["decoding_stopped"])
        self.assertEqual(result["failed_cycles"], 1)
        self.assertEqual(result["coding_cycles"], 1)
        self.assertEqual(result["verified_bytes"], 0)
        self.assertEqual(result["source_bytes"], 0)
        self.assertFalse(destination.exists())

    def test_valid_container_eof_does_not_complete(self):
        for encrypted in (False, True):
            path = self.directory / f"no-absence-{encrypted}.wav"
            shutil.copyfile(self.wave if encrypted else self.plain_wave, path)
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
            destination = self.directory / f"incomplete-{encrypted}.bin"
            options = self.key_options if encrypted else ()
            result = json.loads(self.run_pump("fast-rx", "--input", path, "--save", destination,
                *options, *PROFILE, "--json", ok=False).stdout)
            self.assertFalse(result["physical_complete"])
            self.assertFalse(result["complete"])
            self.assertFalse(result["authenticated"])
            self.assertNotIn("text_preview", result)
            self.assertFalse(destination.exists())

    def test_never_overwrite_outputs(self):
        destination = self.directory / "keep.bin"
        destination.write_bytes(b"keep existing file")
        for wave, options in ((self.wave, self.key_options), (self.plain_wave, ())):
            self.run_pump("fast-rx", "--input", wave, "--save", destination,
                *options, *PROFILE, ok=False)
            self.assertEqual(destination.read_bytes(), b"keep existing file")
            contents = wave.read_bytes()
            self.run_pump("fast-tx", "--input", self.source, "--output", wave,
                *options, *PROFILE, ok=False)
            self.assertEqual(wave.read_bytes(), contents)
            self.run_pump("fast-tx", "--text", "do not overwrite", "--output", wave,
                *options, *PROFILE, ok=False)
            self.assertEqual(wave.read_bytes(), contents)


if __name__ == "__main__":
    unittest.main()

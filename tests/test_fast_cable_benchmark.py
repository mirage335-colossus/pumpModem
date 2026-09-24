"""Non-audio checks of benchmark evidence, estimates and process cleanup."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fast_cable_benchmark", ROOT / "tools/fast_cable_benchmark.py")
BENCH = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BENCH
SPEC.loader.exec_module(BENCH)
PUMP = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else ROOT / "build/pump"


class EvidenceTests(unittest.TestCase):
    def test_routing_provenance_never_infers_old_default(self):
        args = SimpleNamespace(stereo=False, mono=False)
        self.assertEqual(BENCH.routing_provenance(args, {}),
                         {"requested": "profile-default", "effective": None, "evidence": "unreported"})
        self.assertEqual(BENCH.routing_provenance(args, {"mono": False})["effective"], "both-channels")
        self.assertEqual(BENCH.routing_provenance(args, {"mono": True})["effective"], "mono-unspecified")
        for channel, expected in (("left", "left-only"), ("right", "right-only"), ("stereo", "both-channels")):
            self.assertEqual(BENCH.routing_provenance(args, {"audio_channels": channel})["effective"], expected)
        args.stereo = True
        self.assertEqual(BENCH.routing_options(args), ["--stereo"])
        self.assertEqual(BENCH.routing_provenance(args, {}),
                         {"requested": "both-channels", "effective": "both-channels", "evidence": "explicit-option"})
        args.stereo, args.mono = False, True
        self.assertEqual(BENCH.routing_options(args), ["--mono"])
        self.assertEqual(BENCH.routing_provenance(args, {})["effective"], "left-only")

    def test_exact_probability_bounds(self):
        self.assertEqual(BENCH.exact_bounds(0, 0), (None, None))
        lower, upper = BENCH.exact_bounds(1, 1)
        self.assertAlmostEqual(lower, 0.05)
        self.assertEqual(upper, 1.0)
        lower, upper = BENCH.exact_bounds(0, 1)
        self.assertEqual(lower, 0.0)
        self.assertAlmostEqual(upper, 0.95)
        self.assertLess(BENCH.exact_bounds(13, 13)[0], 0.8)
        self.assertGreater(BENCH.exact_bounds(14, 14)[0], 0.8)
        self.assertAlmostEqual(BENCH.exact_bounds(14, 14)[0], 0.05 ** (1 / 14))
        lower, upper = BENCH.exact_bounds(5, 10)
        self.assertAlmostEqual(lower + upper, 1.0)
        self.assertAlmostEqual(BENCH.binomial_tail(10, 5, lower, True), 0.05)
        self.assertAlmostEqual(BENCH.binomial_tail(10, 5, upper, False), 0.05)

    def test_no_cross_size_probability_extrapolation(self):
        candidate = BENCH.parse_candidate("256:7/8:high-rate:62")
        records = [{"candidate": candidate.name, "source_bytes": 100000,
                    "attempted": True, "exact": True, "wall_seconds": 20} for _ in range(14)]
        records.append({"candidate": candidate.name, "source_bytes": 100000,
                        "attempted": False, "exact": False, "wall_seconds": 1})
        summary = BENCH.summarize(records, [candidate], [100000, 5000000, 50000000])
        self.assertEqual(summary[0]["attempted_trials"], 14)
        self.assertEqual(summary[0]["setup_failures"], 1)
        self.assertTrue(summary[0]["meets_80pct_at_95pct_confidence"])
        for row in summary[1:]:
            self.assertEqual(row["attempted_trials"], 0)
            self.assertIsNone(row["empirical_success_probability"])
            self.assertIsNone(row["success_probability_95pct_lower_one_sided"])
            self.assertFalse(row["meets_80pct_at_95pct_confidence"])

    def test_deterministic_fixture_and_exclusive_create(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("first", "same", "other")]
            first = BENCH.write_fixture(paths[0], 65539, 417)
            same = BENCH.write_fixture(paths[1], 65539, 417)
            other = BENCH.write_fixture(paths[2], 65539, 418)
            self.assertEqual(paths[0].stat().st_size, 65539)
            self.assertEqual(first, same)
            self.assertEqual(first, BENCH.hash_file(paths[0]))
            self.assertNotEqual(first, other)
            with self.assertRaises(FileExistsError):
                BENCH.write_fixture(paths[0], 1, 0)


class ProcessTests(unittest.TestCase):
    def run_fake(self, rx_delay, rx_code, setup_seconds=0.01):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fake = directory / "fake-pump"
            fake.write_text(f"""#!{sys.executable}
import json, sys, time
if sys.argv[1] == 'fast-listen':
    time.sleep({rx_delay})
    print(json.dumps({{'complete': {rx_code == 0}, 'physical_complete': {rx_code == 0}, 'error': {'failure' if rx_code else ''!r}}}), flush=True)
    sys.exit({rx_code})
time.sleep(20)
""")
            fake.chmod(0o700)
            args = SimpleNamespace(output=directory, pump=fake, startup_seconds=setup_seconds,
                                   timeout_factor=1.5, timeout_margin=2, sample_rate=48000,
                                   stereo=False, mono=False, input_device="unused", output_device="unused", keep_received=False)
            candidate = BENCH.parse_candidate("256:7/8:high-rate:64")
            info = {"source_bytes_per_group": 208, "cycle_intervals": 74,
                    "interval_symbols": 352, "sample_rate": 48000, "symbol_rate": 15000}
            started = time.monotonic()
            record = BENCH.run_trial(args, candidate, 4096, 1, info, directory / "unused.bin", "unused")
            self.assertLess(time.monotonic() - started, 3)
            self.assertFalse(record["exact"])
            self.assertIsNotNone(record["rx_returncode"])
            return record

    def test_receiver_error_stops_long_playback(self):
        record = self.run_fake(0.2, 2)
        self.assertTrue(record["attempted"])
        self.assertTrue(record["aborted_on_transfer_error"])
        self.assertIsNotNone(record["tx_returncode"])
        self.assertFalse(record["timeout"])

    def test_early_receiver_completion_is_rejected(self):
        record = self.run_fake(0.2, 0)
        self.assertTrue(record["attempted"])
        self.assertTrue(record["premature_receive_completion"])
        self.assertIsNotNone(record["tx_returncode"])

    def test_input_open_failure_does_not_start_transmitter(self):
        record = self.run_fake(0, 2, 0.3)
        self.assertFalse(record["attempted"])
        self.assertIsNone(record["tx_returncode"])
        self.assertIn("setup_error", record)


@unittest.skipUnless(PUMP.is_file(), "build pump for geometry cross-check")
class GeometryTests(unittest.TestCase):
    def test_plan_records_effective_cli_routing(self):
        base = [sys.executable, str(ROOT / "tools/fast_cable_benchmark.py"),
                "--pump", str(PUMP), "--sizes", "4096", "--candidate", "256:7/8:high-rate:62"]
        for flags, expected_request, expected_effective in (
                ([], "profile-default", "left-only"),
                (["--stereo"], "both-channels", "both-channels"),
                (["--mono"], "left-only", "left-only"),
                (["--right-mono"], "right-only", "right-only")):
            result = subprocess.run([*base, *flags], check=True, capture_output=True, text=True, timeout=15)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["mode"], "plan")
            self.assertEqual(plan["output_routing"]["apsk256-r7_8-high-rate-d62"],
                             {"requested": expected_request, "effective": expected_effective, "evidence": "fast-info"})
        result = subprocess.run([*base, "--mono", "--stereo"], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not allowed", result.stderr)

    def test_estimates_match_actual_waveform_generation(self):
        with tempfile.TemporaryDirectory() as directory:
            for index, specification in enumerate(("16:3/4:robust:16", "64:1/2:robust:5", "256:7/8:high-rate:62")):
                candidate = BENCH.parse_candidate(specification)
                common = ["--profile", "wire", *candidate.options(), "--no-encryption"]
                result = subprocess.run([str(PUMP), "fast-info", *common], check=True, capture_output=True, text=True, timeout=15)
                estimate = BENCH.airtime(json.loads(result.stdout), candidate, 60)
                wave = Path(directory) / f"{index}.wav"
                result = subprocess.run([str(PUMP), "fast-tx", *common, "--text", "x" * 60,
                                         "--output", str(wave), "--json"], check=True, capture_output=True, text=True, timeout=30)
                actual = json.loads(result.stdout)
                self.assertAlmostEqual(estimate["seconds"], actual["estimated_seconds"], delta=0.0001)
                self.assertEqual(estimate["intervals"], actual["intervals"])


if __name__ == "__main__":
    unittest.main()

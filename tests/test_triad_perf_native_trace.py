"""Focused checks for the opt-in native triad run stage trace."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("TRIAD_LONG_BINARY", ROOT / "build/dev-cli/triad_long"))
SOURCE_ROOT = Path(os.environ.get("TRIAD_SOURCE_ROOT", ROOT)).resolve()
TRACE_CAPABILITY = "triad-native-run-timing-v1"


def source_declares_trace(root: Path) -> bool:
    """Older immutable triad sources predate the opt-in trace interface."""
    manifest = root / "tools/triad-performance-native-cases.json"
    if not manifest.is_file():
        return False
    data = json.loads(manifest.read_text())
    if not isinstance(data, dict):
        raise ValueError("native performance cases manifest must be an object")
    capabilities = data.get("capabilities", {})
    if not isinstance(capabilities, dict):
        raise ValueError("native performance capabilities must be an object")
    return capabilities.get("native_performance_trace") == TRACE_CAPABILITY


TRACE_SUPPORTED_BY_SOURCE = source_declares_trace(SOURCE_ROOT)


@unittest.skipUnless(
    TRACE_SUPPORTED_BY_SOURCE,
    "candidate source does not declare native trace capability triad-native-run-timing-v1",
)
@unittest.skipUnless(BINARY.is_file(), "build candidate triad_long before checking its declared trace capability")
class NativeTraceTests(unittest.TestCase):
    def command(self, *extra: str) -> list[str]:
        return [str(BINARY), "--mode", "run", "--seconds", "8", "--rate", "36",
                "--added", "1", "--channel", "none", "--phase-diffusion", "0",
                "--seed", "713", "--threads", "1", "--chunk", "128",
                "--start", "0", "0", "1", "--frequency", "0", "0", ".1", *extra]

    def run_json(self, *extra: str) -> dict:
        completed = subprocess.run(self.command(*extra), text=True, capture_output=True, timeout=30)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        rows = [json.loads(line) for line in completed.stdout.splitlines() if line.startswith("{")]
        self.assertEqual(len(rows), 1, completed.stdout)
        return rows[0]

    @staticmethod
    def without_timings(value):
        if isinstance(value, dict):
            return {k: NativeTraceTests.without_timings(v) for k, v in value.items()
                    if k not in {"end_to_end_wall_seconds", "end_to_end_cpu_seconds",
                                 "wall_seconds", "cpu_seconds", "performance_trace",
                                 "command_argv"}}
        if isinstance(value, list):
            return [NativeTraceTests.without_timings(v) for v in value]
        return value

    def test_trace_preserves_the_full_run_results_and_reconciles_work(self):
        plain = self.run_json()
        traced = self.run_json("--performance-trace", "yes")
        self.assertEqual(self.without_timings(plain), self.without_timings(traced))
        trace = traced["performance_trace"]
        self.assertEqual(trace["schema"], "triad-native-run-timing-v1")
        stages = trace["stage_seconds"]
        self.assertEqual(set(stages), {"setup", "channel_read", "receiver_push", "receiver_finish", "other"})
        self.assertTrue(all(value >= 0 for value in stages.values()))
        self.assertAlmostEqual(trace["stage_reconciliation_wall_seconds"],
                               traced["end_to_end_wall_seconds"], delta=1e-9)
        self.assertGreater(trace["chunk_count"], 0)
        self.assertGreater(trace["minimum_chunk_samples"], 0)
        self.assertGreaterEqual(trace["maximum_chunk_samples"], trace["minimum_chunk_samples"])
        self.assertEqual(trace["processed_rows"], traced["capture_samples"])
        self.assertEqual(trace["channel_samples"], traced["capture_samples"])
        self.assertTrue(trace["complete_observation"])
        self.assertGreater(trace["candidate_count"], 0)
        self.assertGreater(trace["basis_values"], 0)
        self.assertGreater(trace["scored_blocks"], 0)
        self.assertEqual(trace["arithmetic_used"], "fp64")
        receiver_total = trace["receiver_timing_crosscheck"]["push_plus_finish_seconds"]
        receiver_report = trace["receiver_timing_crosscheck"]["receiver_report_wall_seconds"]
        self.assertLess(abs(receiver_total - receiver_report), max(.05, .1 * receiver_report))

    def test_trace_flag_rejects_bad_values_and_non_run_modes(self):
        invalid = subprocess.run([str(BINARY), "--mode", "run", "--performance-trace", "sometimes"],
                                 text=True, capture_output=True, timeout=5)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("performance trace must be yes or no", invalid.stderr)
        wrong_mode = subprocess.run([str(BINARY), "--mode", "plan", "--performance-trace", "yes"],
                                    text=True, capture_output=True, timeout=5)
        self.assertNotEqual(wrong_mode.returncode, 0)
        self.assertIn("supported only by --mode run", wrong_mode.stderr)

    def test_configure_geometry_matches_completed_run_without_creating_samples(self):
        traced = self.run_json("--performance-trace", "yes")
        command = self.command()
        command[2] = "configure"
        completed = subprocess.run(command, text=True, capture_output=True, timeout=15)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        configured = json.loads(completed.stdout)
        geometry = configured["performance_geometry"]
        self.assertEqual(geometry["capture_samples"], traced["capture_samples"])
        self.assertEqual(geometry["source_samples"], traced["source_samples"])
        self.assertEqual(geometry["candidate_count"], traced["candidate_count"])
        self.assertEqual(geometry["basis_values"], traced["basis_values"])
        self.assertEqual(geometry["maximum_chunk_samples"], traced["performance_trace"]["maximum_chunk_samples"])
        self.assertEqual(geometry["arithmetic_used"], "fp64")
        self.assertIn("creates no sampled observation", geometry["geometry_scope"])


class TraceCapabilityTests(unittest.TestCase):
    def test_old_or_undeclared_source_is_explicitly_out_of_trace_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertFalse(source_declares_trace(root))
            tools = root / "tools"
            tools.mkdir()
            manifest = tools / "triad-performance-native-cases.json"
            manifest.write_text('{"schema":"triad-performance-native-cases-v1"}')
            self.assertFalse(source_declares_trace(root))

    def test_declared_trace_contract_requires_binary_tests(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "tools"
            tools.mkdir()
            manifest = tools / "triad-performance-native-cases.json"
            manifest.write_text(json.dumps({"capabilities": {
                "native_performance_trace": TRACE_CAPABILITY}}))
            self.assertTrue(source_declares_trace(root))


if __name__ == "__main__":
    unittest.main()

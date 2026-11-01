import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

import torch

TOOLS = Path(__file__).resolve().parents[1]/"tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("rbpf_benchmark", TOOLS/"triad-rbpf-benchmark.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
torch.set_num_threads(2)


class RBPFBenchmarkTests(unittest.TestCase):
    def test_prepared_bank_is_identical_and_has_valid_joint_statistics(self):
        kwargs = dict(batch=3, particles=7, blocks=4, taps=2, seed=184,
                      dt=1., phase_sigma=.03, frequency_bound=.005, memory_limit=2**20)
        first, second = benchmark.prepare(**kwargs), benchmark.prepare(**kwargs)
        self.assertEqual(first["input_sha256"], second["input_sha256"])
        for key in ("r", "G", "increments"):
            torch.testing.assert_close(first[key], second[key], atol=0., rtol=0.)
            self.assertEqual(first[key].device.type, "cpu")
        self.assertEqual(float(first["increments"][0].abs().max()), 0.)
        self.assertGreater(float(torch.linalg.eigvalsh(first["G"]).min()), 0.)

    def test_memory_preflight_and_fresh_output(self):
        with self.assertRaisesRegex(ValueError, "preflight"):
            benchmark.prepare(256, 65536, 64, 16, 42, 1., .03, .005, 2**20)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/"existing.json"
            output.write_text("do not overwrite")
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.main(["--output", str(output)])
            self.assertEqual(output.read_text(), "do not overwrite")

    def test_tiny_cpu_run_records_stages_pairing_and_precision(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/"fresh.json"
            with contextlib.redirect_stderr(io.StringIO()):
                report = benchmark.main(["--batches", "3", "--particles", "7", "--blocks", "4",
                                         "--repeats", "2", "--output", str(output)])
            self.assertEqual(json.loads(output.read_text()), json.loads(json.dumps(report)))
            self.assertEqual(report["configuration"]["devices"], ["cpu"])
            case = report["cases"][0]
            self.assertEqual(len(case["runs"]), 2)
            self.assertEqual(case["speedups"], [])
            for row in case["runs"]:
                self.assertEqual(row["input_sha256"], case["input_sha256"])
                self.assertGreater(row["median"]["single_use_seconds"], row["median"]["resident_total_seconds"])
                self.assertGreater(row["median"]["core_seconds"], 0.)
                self.assertGreater(row["median"]["finish_seconds"], 0.)
                self.assertTrue(row["repeats"][0]["diagnostics"]["phase_increment_override_used"])
                self.assertIsNone(row["repeats"][0]["cuda_peak_allocated_bytes"])
            self.assertLess(case["comparisons"][1]["maximum_absolute_log_lr_error"], 1e-4)
            self.assertFalse(case["comparisons"][1]["top_rank_changed"])
            self.assertEqual(case["comparisons"][1]["gate_decisions_changed"], 0)


if __name__ == "__main__":
    unittest.main()

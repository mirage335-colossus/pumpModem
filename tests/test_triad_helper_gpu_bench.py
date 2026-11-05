"""Cheap interface checks for the actual-comparison benchmark driver."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
SPEC = importlib.util.spec_from_file_location("triad_helper_gpu_bench", TOOLS / "triad_helper_gpu_bench.py")
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


def result(device, index):
    bank = [0.0, 0.1, 0.2]
    narrow = [0.0, 0.1]
    return {
        "procedure_id": "procedure", "input_sha256": "input", "template_sha256": "templates",
        "source_hashes": {"study": "source"},
        "timing": {"helper_seconds": .2, "command_score_seconds": .5},
        "assistance": {"selected_frequencies": narrow},
        "baseline": {"bit": 1, "score": 2.5 + index * 1e-7,
                     "bit_log_likelihoods": [1.5, 2.5 + index * 1e-7],
                     "best_epoch_offset_seconds": -1,
                     "best_group_public_parameters": {"timing_seconds": .125, "frequency_hz": .05},
                     "timing": {"total_score_prepared_seconds": .2,
                                "aggregate_fft_seconds": .07, "aggregate_scoring_seconds": .12},
                     "arithmetic": {"products": "FP32"},
                     "geometry": {"frequency_offsets_hz": bank}},
        "assisted": {"bit": 1, "score": 3.5 + index * 1e-7,
                     "bit_log_likelihoods": [1.5, 3.5 + index * 1e-7],
                     "best_epoch_offset_seconds": 2,
                     "best_group_public_parameters": {"timing_seconds": .25, "frequency_hz": .1},
                     "timing": {"total_score_prepared_seconds": .1,
                                "aggregate_fft_seconds": .04, "aggregate_scoring_seconds": .06},
                     "geometry": {"frequency_offsets_hz": narrow}},
    }


class FakeComparison:
    def __init__(self, fixture, templates, *, device, precision, fft_backend,
                 frontend_profile, frequency_batch_size, max_cached_hypotheses,
                 memory_limit_bytes):
        self.device = device
        self.preparation_seconds = .1
        factory_options.append((device, precision, fft_backend, frontend_profile, frequency_batch_size,
                                max_cached_hypotheses, memory_limit_bytes))
        events.append(("setup", device))

    def score(self, fixture):
        index = sum(1 for event in events if event[0] == "score" and event[1] == self.device)
        events.append(("score", self.device))
        return result(self.device, index)


events = []
factory_options = []


class TriadHelperGpuBenchTests(unittest.TestCase):
    def test_cpu_smoke_pairs_actual_comparison_interface_and_alternates(self):
        events.clear()
        factory_options.clear()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            with contextlib.redirect_stderr(io.StringIO()):
                report = bench.main(["--fixture", directory, "--output", str(output),
                                     "--devices", "cpu", "--repeats", "3"], FakeComparison)
            self.assertEqual(json.loads(output.read_text()), json.loads(json.dumps(report)))
            self.assertEqual([e for e in events if e[0] == "score"],
                             [("score", "cpu")] * 3)
            self.assertEqual(len(report["devices"]["cpu"]["runs"]), 3)
            self.assertIsNone(report["paired_cpu_cuda"]["median_cpu_over_cuda_speedup"])
            self.assertEqual(report["configuration"]["precision"], "fp32")
            self.assertFalse(report["configuration"]["tf32_enabled"])
            self.assertEqual(report["configuration"]["fft_backend"], "legacy")
            self.assertEqual(report["configuration"]["frontend_profile"], "legacy")
            self.assertEqual(report["configuration"]["max_cached_hypotheses"], 1)
            self.assertEqual(report["configuration"]["memory_limit_bytes"], 2 * 1024**3)
            self.assertEqual(factory_options, [("cpu", "fp32", "legacy", "legacy", 8, 1, 2 * 1024**3)])

    def test_cpu_cuda_order_alternates_and_parity_is_reported(self):
        events.clear()
        factory_options.clear()
        with tempfile.TemporaryDirectory() as directory:
            args = type("Args", (), {"fixture": Path(directory), "templates": None,
                                     "output": Path(directory) / "new.json", "repeats": 3,
                                     "cpu_threads": 2, "devices": ["cpu", "cuda"],
                                     "fft_backend": "batched", "frontend_profile": "wide",
                                     "frequency_batch_size": 4, "max_cached_hypotheses": 13,
                                     "memory_limit_bytes": 8 * 1024**3})
            available = bench.torch.cuda.is_available
            sync = bench.torch.cuda.synchronize
            device_name = bench.torch.cuda.get_device_name
            try:
                bench.torch.cuda.is_available = lambda: True
                bench.torch.cuda.synchronize = lambda *args, **kwargs: None
                bench.torch.cuda.get_device_name = lambda *args, **kwargs: "mock-device-for-interface-test"
                report = bench.run(args, FakeComparison)
            finally:
                bench.torch.cuda.is_available = available
                bench.torch.cuda.synchronize = sync
                bench.torch.cuda.get_device_name = device_name
            self.assertEqual([p["execution_order"] for p in report["paired_cpu_cuda"]['pairs']],
                             [["cpu", "cuda"], ["cuda", "cpu"], ["cpu", "cuda"]])
            for pair in report["paired_cpu_cuda"]["pairs"]:
                self.assertTrue(pair["parity"]["assisted_bit_equal"])
                self.assertTrue(pair["parity"]["narrowed_bank_equal"])
                self.assertTrue(pair["parity"]["baseline_winning_epoch_equal"])
                self.assertTrue(pair["parity"]["assisted_winning_timing_cfo_equal"])
                self.assertEqual(pair["parity"]["baseline_data_cuda_over_cpu_speedup"], 1.0)
                self.assertEqual(pair["parity"]["assisted_data_cuda_over_cpu_speedup"], 1.0)
                self.assertEqual(pair["baseline_data_stage_ratios"]["aggregate_fft_seconds"]["cuda_over_cpu_speedup"], 1.0)
                self.assertGreater(pair["cpu_over_cuda_speedup"], 0.)
                self.assertEqual(pair["cpu_over_cuda_speedup"], pair["cpu_seconds"] / pair["cuda_seconds"])
            self.assertIsNone(report["paired_cpu_cuda"]["interval_claim"])
            self.assertEqual(report["configuration"]["fft_backend"], "batched")
            self.assertEqual(report["configuration"]["frontend_profile"], "wide")
            self.assertEqual(report["configuration"]["frequency_batch_size"], 4)
            self.assertEqual(report["configuration"]["max_cached_hypotheses"], 13)
            self.assertEqual(report["configuration"]["memory_limit_bytes"], 8 * 1024**3)
            self.assertEqual(factory_options, [("cpu", "fp32", "batched", "wide", 4, 13, 8 * 1024**3),
                                               ("cuda", "fp32", "batched", "wide", 4, 13, 8 * 1024**3)])

    def test_rejects_existing_output_and_unavailable_cuda(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "existing.json"
            output.write_text("preserve")
            args = type("Args", (), {"fixture": Path(directory), "templates": None,
                                     "output": output, "repeats": 3,
                                     "cpu_threads": 2, "devices": ["cpu"],
                                     "fft_backend": "legacy", "frontend_profile": "legacy",
                                     "frequency_batch_size": 8, "max_cached_hypotheses": 1,
                                     "memory_limit_bytes": 2 * 1024**3})
            with self.assertRaisesRegex(ValueError, "already exists"):
                bench.run(args, FakeComparison)
            output.unlink()
            args.devices = ["cpu", "cuda"]
            available = bench.torch.cuda.is_available
            try:
                bench.torch.cuda.is_available = lambda: False
                with self.assertRaisesRegex(ValueError, "CUDA requested but unavailable"):
                    bench.run(args, FakeComparison)
            finally:
                bench.torch.cuda.is_available = available


if __name__ == "__main__":
    unittest.main()

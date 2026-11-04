"""Focused contract tests for the bounded Triad hardware profiler."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import unittest
import sys
import contextlib
from types import SimpleNamespace
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "triad_perf_hardware.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("triad_perf_hardware", MODULE_PATH)
hardware = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(hardware)


class HardwareProfilerContractTests(unittest.TestCase):
    def test_affinity_identity_distinguishes_equal_size_cpu_masks(self):
        with mock.patch.object(hardware.os, "sched_getaffinity", return_value={2, 4}, create=True):
            first = hardware._cpu_limits()
        with mock.patch.object(hardware.os, "sched_getaffinity", return_value={2, 3}, create=True):
            second = hardware._cpu_limits()
        self.assertEqual(first["affinity_cpu_count"], second["affinity_cpu_count"])
        self.assertNotEqual(first["affinity_cpus"], second["affinity_cpus"])
        self.assertEqual([row["cpu"] for row in first["affinity_topology"]], [2, 4])

    def test_validation_rejects_unbounded_or_ambiguous_inputs(self):
        for kwargs in (
            {"seconds": 0},
            {"seconds": float("nan")},
            {"threads": 0},
            {"threads": True},
            {"device": "host"},
            {"memory_limit_bytes": 1024},
        ):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                hardware.collect_profile(**kwargs)

    def test_discovery_is_json_serializable_and_omits_host_identifiers(self):
        data = hardware.discover_hardware("cpu", 2)
        encoded = json.dumps(data, allow_nan=False)
        self.assertIn("effective_core_limit_estimate", data["cpu"])
        self.assertEqual(data["requested_threads"], 2)
        for forbidden in ("hostname", "host_name", "uuid", "serial"):
            self.assertNotIn(forbidden, encoded.lower())

    def test_deadline_interrupts_before_first_warm_iteration(self):
        # One indivisible cold dispatch is measured. The next deadline check
        # prevents a timed loop from starting and the case stays incomplete.
        fn = mock.Mock(return_value=None)
        with mock.patch.object(hardware.time, "monotonic", side_effect=(1.0, 2.0)):
            result = hardware._time_cpu_case(
                "fake", fn, lambda: {}, seconds=0.01, threads=1,
                workspace=0, deadline=1.5)
        self.assertEqual(result["status"], "incomplete")
        self.assertIn("deadline", result["reason"])
        self.assertEqual(fn.call_count, 1)

    def test_cuda_batches_bound_queued_work_by_completed_time(self):
        state = {"wall": 0., "queued": 0., "max_queue": 0.}
        class Event:
            def __init__(self, **kwargs):
                pass
            def record(self):
                self.at = state["queued"]
            def synchronize(self):
                state["wall"] += state["queued"]
                state["max_queue"] = max(state["max_queue"], state["queued"])
                state["queued"] = 0.
            def elapsed_time(self, other):
                return (other.at - self.at)*1000
        def fn():
            state["queued"] += .04  # Deliberately slow, indivisible GPU work.
        def sync(device):
            state["wall"] += state["queued"]
            state["queued"] = 0.
        fake = mock.Mock()
        fake.cuda.Event = Event
        fake.cuda.synchronize = sync
        fake.Tensor = type("FakeTensor", (), {})
        with mock.patch.object(hardware, "torch", fake), \
             mock.patch.object(hardware.time, "perf_counter", side_effect=lambda: state["wall"]), \
             mock.patch.object(hardware.time, "monotonic", side_effect=lambda: state["wall"]):
            result = hardware._cuda_event_case("bounded", fn, lambda: {}, device="cuda",
                seconds=.1, threads=1, workspace=1, deadline=1.)
        self.assertEqual(result["status"], "complete")
        self.assertLessEqual(state["max_queue"], .04)
        self.assertLess(state["wall"], .2)

    def test_telemetry_uses_current_cuda_device_and_failed_cleanup_is_incomplete(self):
        import triad_perf_telemetry
        identifier = "GPU-12345678-1234-1234-1234-123456789abc"
        sampler = SimpleNamespace(summarize=lambda *a: {"status": "unavailable", "cleanup": "unavailable: process termination not verified"})
        fake = mock.Mock()
        fake.cuda.is_available.return_value = True
        fake.cuda.current_device.return_value = 1
        fake.cuda.get_device_properties.return_value = SimpleNamespace(uuid=identifier)
        with mock.patch.object(hardware, "torch", fake), \
             mock.patch.object(hardware, "_gpu_cases", return_value={"probe": {"status": "complete"}}), \
             mock.patch.object(hardware, "discover_hardware", return_value={}), \
             mock.patch.object(triad_perf_telemetry, "start_sampler", return_value=contextlib.nullcontext(sampler)) as start:
            result = hardware.collect_profile(device="cuda", seconds=.01)
        fake.cuda.get_device_properties.assert_called_with(1)
        start.assert_called_once_with(1, inherit_process_group=True, device_uuid=identifier)
        self.assertEqual(result["status"], "incomplete")

    def test_gpu_unavailability_is_explicit_and_profile_json_safe(self):
        with mock.patch.object(hardware, "_gpu_cases", return_value={
                name: {"status": "unavailable", "reason": "mocked"}
                for name in hardware.GPU_CASES + tuple(
                    f"tensor-{fmt}-cuda" for fmt in hardware.GPU_TENSOR_FORMATS)}):
            profile = hardware.collect_profile(device="cuda", seconds=0.01)
        self.assertEqual(profile["status"], "unavailable")
        self.assertIsNone(profile["confidence_interval"])
        json.dumps(profile, allow_nan=False)


if __name__ == "__main__":
    unittest.main()

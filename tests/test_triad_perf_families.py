"""Checks for sampled-family timing scope and frozen-model validation."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_families as families


def run(count, seconds, *, target=24, complete=False, shape=64):
    return {"receiver": {"work": {"unit": "cfo", "count": count, "target_count": target},
                         "regime": {"shape": shape}},
            "complete_receiver": complete, "request_wall_seconds": seconds}


def training():
    return {"generation": {"wall_seconds": .2},
            "receiver_runs": [run(4, .5), run(8, .9)]}


class ForecastTests(unittest.TestCase):
    def test_affine_prediction_charges_generation_and_fixed_stage_once(self):
        forecast = families.affine_prediction([training()] * 3, (4, 8), 24)
        self.assertAlmostEqual(forecast["complete_pipeline_seconds_point"], 2.7)
        self.assertAlmostEqual(forecast["receiver_request_seconds_point"], 2.5)
        self.assertAlmostEqual(forecast["generation_seconds_point"], .2)

    def test_changed_work_regime_cannot_reuse_prediction(self):
        forecast = families.affine_prediction([training()] * 3, (4, 8), 24)
        endpoint = {"generation": {"wall_seconds": .2}, "complete_pipeline_wall_seconds": 2.7,
                    "receiver_runs": [run(24, 2.5, complete=True)]}
        self.assertAlmostEqual(families.validation_row(endpoint, forecast)["pipeline_relative_error"], 0.)
        for field, value in (("regime", {"shape": 128}), ("work", {"count": 23})):
            changed = copy.deepcopy(endpoint)
            changed["receiver_runs"][0]["receiver"][field] = value
            with self.assertRaises(ValueError):
                families.validation_row(changed, forecast)

    def test_unstable_or_unbounded_model_is_rejected(self):
        row = training()
        row["receiver_runs"][1]["request_wall_seconds"] = .4
        with self.assertRaisesRegex(ValueError, "unstable"):
            families.affine_prediction([row], (4, 8), 24)
        with self.assertRaisesRegex(ValueError, "extrapolation"):
            families.affine_prediction([training()], (4, 8), 65)

    def test_full_jobs_cannot_be_substituted_as_prefixes(self):
        row = training()
        row["receiver_runs"][1]["complete_receiver"] = True
        with self.assertRaisesRegex(ValueError, "abbreviated"):
            families.affine_prediction([row], (4, 8), 24)

    def test_commitment_precedes_independent_complete_validation(self):
        class Worker:
            identity = {"threads": 2}
            external_startup_seconds = 1.
            def request(self, request):
                return {"source_sha256": {"test": "frozen"}}

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "proof"
            binary = Path(tmp) / "binary"
            binary.write_bytes(b"identity")
            def job(worker, binary, profile, case, seed, threads, directory, device, precision,
                    deadline, *, limits=(None,)):
                if limits == (None,):
                    frozen = json.loads((output / "forecast.json").read_text())
                    self.assertEqual(frozen["holdouts"], [])
                    commitment = frozen.pop("commitment_sha256")
                    self.assertEqual(commitment, families.digest(frozen))
                    self.assertGreaterEqual(seed, 100000)
                    return {"generation": {"wall_seconds": .2},
                            "receiver_runs": [run(24, 2.5, complete=True)],
                            "complete_pipeline_wall_seconds": 2.7}
                return {"generation": {"wall_seconds": .2},
                        "receiver_runs": [run(n, .1 + .1*n) for n in limits]}
            with patch.object(families, "measure_job", job):
                result = families.qualify_case(
                    Worker(), binary, {}, {"training": [4, 8], "seed": 1}, output,
                    device="cpu", precision="fp32", native_threads=1,
                    deadline=time.monotonic()+30, maximum_holds=3)
            self.assertEqual(result["status"], "validated")
            self.assertEqual(len(result["holdouts"]), 3)

    def test_manifest_has_one_entry_for_each_maintained_family(self):
        entries = families.manifest()
        self.assertEqual({case["family"] for case in entries["cases"]}, set(families.ADAPTERS))
        for case in entries["cases"]:
            for source in case["dependencies"]:
                self.assertTrue((ROOT / "tools" / source).is_file())
        self.assertEqual(next(c for c in entries["cases"] if c["family"] == "cfo-batch")["captures"], 3)

    def test_reference_source_path_cannot_shadow_benchmark_modules(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            current = root/"current"
            reference = root/"reference"
            current.mkdir()
            reference.mkdir()
            name = "_sampled_harness_source_test"
            (current/(name+".py")).write_text("origin = 'current'\n")
            (reference/(name+".py")).write_text("origin = 'reference'\n")
            with patch.object(families, "WORKER", current/"triad_perf_families.py"):
                sys.path.insert(0, str(reference))
                try:
                    self.assertEqual(families.load_harness_module(name).origin, "current")
                finally:
                    sys.path.remove(str(reference))
                    sys.modules.pop(name, None)

    def test_full_receiver_requires_measured_count_to_match_public_target(self):
        result = {"seconds": {"score": 1.}, "work": {"unit": "blocks", "count": 12, "target_count": 16},
                  "regime": {"tile": 4}, "result": {"scores": [[0., 1.]]}}
        self.assertIs(families.validate_measurement({"unit": "blocks"}, result, 12), result)
        with self.assertRaisesRegex(ValueError, "measured work count"):
            families.validate_measurement({"unit": "blocks"}, result, None)
        result["work"]["count"] = 16
        self.assertIs(families.validate_measurement({"unit": "blocks"}, result, None), result)

    def test_triton_policy_binds_version_and_cache_configuration(self):
        case = {"family": "phase-graph"}
        self.assertIsNone(families.triton_policy(case, "cpu"))
        with patch.object(families.importlib.metadata, "version", return_value="3.4.0"), \
                patch.dict(families.os.environ, {"TRITON_CACHE_DIR": "/tmp/triad-test-cache"}):
            policy = families.triton_policy(case, "cuda")
        self.assertEqual(policy["distribution_version"], "3.4.0")
        self.assertEqual(len(policy["explicit_cache_directory_sha256"]), 64)
        self.assertNotIn("/tmp/triad-test-cache", json.dumps(policy))
        self.assertIn("does not clear", policy["disk_cache"])

    def test_unspecified_cuda_device_is_resolved_before_set_device(self):
        class Device:
            def __init__(self, name, index=None):
                self.type, _, suffix = name.partition(":")
                self.index = int(suffix) if suffix else index

            def __str__(self):
                return self.type if self.index is None else f"{self.type}:{self.index}"

        def set_device(device):
            if device.index is None:
                raise ValueError("expected a CUDA device with a specified index")

        cuda = SimpleNamespace(is_available=lambda: True, current_device=Mock(return_value=2),
                               set_device=Mock(side_effect=set_device))
        torch = SimpleNamespace(device=Device, cuda=cuda)
        self.assertEqual(families.resolve_worker_device(torch, "cpu"), "cpu")
        cuda.set_device.assert_not_called()
        self.assertEqual(families.resolve_worker_device(torch, "cuda"), "cuda:2")
        self.assertEqual(cuda.set_device.call_args.args[0].index, 2)
        self.assertEqual(families.resolve_worker_device(torch, "cuda:1"), "cuda:1")
        self.assertEqual(cuda.set_device.call_args.args[0].index, 1)
        cuda.current_device.assert_called_once()


if __name__ == "__main__":
    unittest.main()

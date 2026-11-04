"""Paired sampled-family model and anchor checks."""
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_family_regression as regression
import triad_perf_families as sample
from triad_perf_adaptive import AdaptivePolicy


class Worker:
    external_startup_seconds = 1.
    identity = {"threads": 2}

    def request(self, request):
        return {"source_sha256": {"test": "a"}, "environment": {"platform": "test"}}


def receiver(count, seconds, complete):
    return {"receiver": {"work": {"count": count, "target_count": 24, "unit": "cfo"},
                         "regime": {"tile": 64}, "result": {"scores": [[.01, .15]]}},
            "complete_receiver": complete, "request_wall_seconds": seconds}


class PairedSampledTests(unittest.TestCase):
    def test_full_confirmation_and_independent_single_sentinel_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            binary = directory / "native"
            binary.write_bytes(b"fixed native")
            case = {"id": "fixture", "training": [4, 8], "seed": 17}
            workers = {name: Worker() for name in regression.SOURCES}
            binaries = dict.fromkeys(regression.SOURCES, binary)
            direct_calls = []
            out = directory / "qualification"
            def job(worker, binary, profile, case, seed, threads, directory, device, precision,
                    deadline, *, limits=(None,)):
                if limits == (None,):
                    frozen = json.loads((out / "forecast.json").read_text())
                    commitment = frozen.pop("commitment_sha256")
                    self.assertEqual(commitment, sample.digest(frozen))
                    direct_calls.append(seed)
                    return {"generation": {"wall_seconds": .2, "seed": seed},
                            "receiver_runs": [receiver(24, 2.5, True)],
                            "complete_pipeline_wall_seconds": 2.7}
                return {"generation": {"wall_seconds": .2, "seed": seed},
                        "receiver_runs": [receiver(n, .1+.1*n, False) for n in limits],
                        "training_pipeline_wall_seconds": .2+sum(.1+.1*n for n in limits)}
            policy = AdaptivePolicy(looks=(3,))
            with patch.object(sample, "measure_job", job):
                qualified = regression.measure_case(workers, binaries, case, {}, out,
                    device="cpu", precision="fp32", native_threads=1,
                    deadline=time.monotonic()+30, policy=policy, mode="qualify", maximum_holds=3,
                    seed_offset=1000)
                self.assertEqual(qualified["status"], "validated")
                self.assertEqual(qualified["comparison"]["gate"], "pass")
                self.assertEqual(len(direct_calls), 6)
                self.assertEqual(set(qualified["confirmation"][0]), set(regression.STAGES))
                anchor = out / "result.json"
                self.assertIsNotNone(regression.load_anchor(anchor, qualified["identity"]))
                previous = list(direct_calls)
                out = directory / "routine"
                reused = regression.measure_case(workers, binaries, case, {}, out,
                    device="cpu", precision="fp32", native_threads=1,
                    deadline=time.monotonic()+30, policy=policy, mode="reuse",
                    anchor=anchor, seed_offset=400000)
                self.assertEqual(reused["status"], "historically_supported_conditional")
                self.assertEqual(reused["comparison"]["gate"], "pass")
                self.assertEqual(len(direct_calls), 8)
                self.assertNotIn(direct_calls[-1], previous)

                tampered = json.loads(anchor.read_text())
                tampered["full_pairs"][0]["candidate"]["receiver_runs"][0]["request_wall_seconds"] *= 2
                tampered.pop("record_sha256")
                tampered["record_sha256"] = sample.digest(tampered)
                invalid = directory / "invalid.json"
                sample.write_json(invalid, tampered)
                self.assertIsNone(regression.load_anchor(invalid, qualified["identity"]))

    def test_component_slowdown_cannot_hide_behind_unchanged_total(self):
        fits = {"pipeline": [0., 0., 0.], "generation": [-.2, -.2, -.2], "receiver": [.3, .3, .3]}
        checks = regression._confirmation(
            {stage: {metric: residuals for metric in (*regression.SOURCES, "paired")}
             for stage, residuals in fits.items()})
        self.assertEqual(regression._status(checks), "invalid")
        self.assertEqual(checks["pipeline"]["paired"]["status"], "validated")

    def test_numerical_disagreement_is_not_a_timing_observation(self):
        jobs = {name: {"receiver_runs": [receiver(24, 2.5, True)]} for name in regression.SOURCES}
        jobs["candidate"]["receiver_runs"][0]["receiver"]["result"]["scores"][0][0] = 10.
        with self.assertRaisesRegex(ValueError, "scores disagree"):
            regression.numerical_agreement(jobs, "fp32")

    def test_same_scores_do_not_admit_a_changed_pair_work_regime(self):
        jobs = {name: {"receiver_runs": [receiver(24, 2.5, True)]} for name in regression.SOURCES}
        jobs["candidate"]["receiver_runs"][0]["receiver"]["regime"]["tile"] = 128
        with self.assertRaisesRegex(ValueError, "work regimes differ"):
            regression.numerical_agreement(jobs, "fp32")

    def test_cold_measurement_preserves_unqualified_forecast_and_checks_work(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            binary = directory/"native"
            binary.write_bytes(b"measured native")
            expected = {"source_sha256": {"phase": "a"}, "environment": {"runtime": "a"}}
            row = {"complete_pipeline_wall_seconds": 1.7, "receiver_runs": [receiver(24, 1.5, True)]}
            wrong = {"complete_pipeline_wall_seconds": 1.8, "receiver_runs": [receiver(24, 1.6, True)]}
            wrong["receiver_runs"][0]["receiver"]["regime"]["tile"] = 128
            report = {
                "prediction_valid": True, "selected_measurement": "direct",
                "training": [{"candidate": row}], "case": {"seed": 7},
                "identity": {"binaries": {"candidate": sample.file_sha(binary)},
                             "measurement_dependencies": {}, "adapters": {"candidate": expected["source_sha256"]},
                             "environment": {"candidate": expected["environment"]}, "device": "cpu",
                             "fixture_profile": {}, "native_threads": 1, "precision": "fp32"},
                "worker_startup_seconds": {"candidate": 1.}, "setup": {"candidate": {"request_wall_seconds": .3}},
                "record_sha256": "warm", "seed_offset": 0,
            }
            class ColdWorker(Worker):
                def request(self, request):
                    return {**expected, "environment_matches_setup": True}

                def close(self):
                    pass
            with patch.object(sample, "measure_job", return_value=wrong):
                result = regression.cold_single(report, lambda *a, **k: ColdWorker(), directory, binary,
                    directory/"cold", threads=1, deadline=time.monotonic()+30)
            self.assertFalse(result["endpoint_verified"])
            self.assertFalse(result["cold_prediction_qualified"])
            self.assertFalse(result["endpoint_verification"]["full_work_matches"])
            self.assertTrue(result["endpoint_verification"]["stage_source_matches"])
            self.assertEqual(result["forecast_status"], "unvalidated_diagnostic")
            self.assertEqual(json.loads((directory/"cold/cold-forecast.json").read_text())["parent_report_sha256"], "warm")


if __name__ == "__main__":
    unittest.main()

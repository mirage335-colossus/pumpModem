#!/usr/bin/env python3
"""Bounded synthetic checks of the prospective diagnostic, never timed DSP."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from triad_perf_fft_diagnostic import measure_diagnostic, schedule

spec = importlib.util.spec_from_file_location("fft_diagnostic_test_api", ROOT / "tools/triad-performance.py")
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)


def case():
    return {"id": "diagnostic", "input_directory": "/unused/fake", "lag_max_seconds": .1,
            "frequency_offsets_hz": [i*.01 for i in range(64)], "group_tile": 32,
            "configuration": {"lag_group_count": 97}, "fixture_hashes": {"observation": "fixed"},
            "performance_environment": {"cpu": "test", "threads": 2}}


class Worker:
    def __init__(self, *, factor=1., bad=None, full_callback=None):
        self.factor, self.bad, self.full_callback = factor, bad, full_callback
        self.requests = []
        self.identity = {"source_hashes": {"receiver": "fixed"}, "threads": 2}

    def request(self, request):
        self.requests.append(request)
        if request["action"] == "prepare":
            return {"wall_seconds": 2.*self.factor, "input_hashes": case()["fixture_hashes"],
                    "source_hashes": self.identity["source_hashes"],
                    "geometry": {"sample_count": 10000, "sample_rate": 1024., "nfft": 20000,
                                 "lags": 97, "lag_first_sample": 0, "lag_last_sample": 96,
                                 "lag_step_samples": 1, "group_tile": 32,
                                 "precision": request["precision"], "device": request["device"]}}
        count = len(request["frequency_offsets_hz"])
        if count == 64 and self.full_callback:
            self.full_callback()
        score = self.factor*(.5+.25*count)
        if self.bad == "full-only" and count == 64:
            score *= 1.5
        result = {"wall_seconds_including_finish": score, "timing": {
                      "observation_fft_seconds": self.factor*.08*count,
                      "direct_correlation_refinement_seconds": self.factor*.01*count,
                      "residual_and_likelihood_seconds": self.factor*.11*count},
                  "geometry": {"group_count": 97*count}, "measured_group_count": 97*count,
                  "bit_log_likelihoods": [1., 2.], "strict": {"accepted": False, "bit": 1},
                  "relaxed": {"accepted": False, "bit": 1}, "rank_histogram": [1, 2]}
        if self.bad == "stages":
            result["timing"]["observation_fft_seconds"] = score*2
        if self.bad == "groups":
            result["measured_group_count"] -= 1
        if self.bad == "scores":
            result["bit_log_likelihoods"][0] = 100.
        return result


class DiagnosticTests(unittest.TestCase):
    def test_four_balanced_blocks_freeze_both_models_before_one_full_abba(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frozen.json"
            def frozen_before_full():
                frozen = json.loads(path.read_text())
                self.assertTrue(frozen["prediction_frozen_before_endpoint"])
                self.assertEqual(len(frozen["training_blocks"]), 4)
                self.assertEqual(set(frozen["forecasts"]), {"affine", "component"})
                self.assertFalse(frozen["qualification_claim"])
            workers = {name: Worker(full_callback=frozen_before_full) for name in ("reference", "candidate")}
            result = measure_diagnostic(case(), workers, "fp32", "cpu", api=api, commitment_path=path)
        frozen = result["frozen_commitment"]
        self.assertEqual(result["status"], "diagnostic_complete")
        self.assertIsNone(result["regression_gate"])
        for name in workers:
            calls = [len(r["frequency_offsets_hz"]) for r in workers[name].requests if r["action"] == "score"]
            self.assertEqual(calls.count(1), 8)
            self.assertEqual(calls.count(8), 8)
            self.assertEqual(calls.count(64), 2)
            for block in frozen["training_blocks"]:
                rows = [row for row in block if row["source"] == name]
                self.assertEqual(sorted(tuple(s["cfo_count"] for s in row["scores"]) for row in rows), [(1, 8), (8, 1)])
            for model in ("affine", "component"):
                self.assertAlmostEqual(frozen["forecasts"][model][name]["predicted_full_seconds"], 18.5)
                self.assertAlmostEqual(result["model_comparisons"][model]["log_actual_over_predicted"][name], 0.)
            component = frozen["block_models"][0][name]["component"]
            self.assertGreater(component["residual_affine_model"]["fixed_score_seconds"], .49)
            self.assertGreater(component["residual_affine_model"]["per_cfo_seconds"], .049)
        self.assertEqual(len(result["numerical_checks"]), 18)

    def test_component_negative_residual_is_unavailable_not_clamped(self):
        with tempfile.TemporaryDirectory() as directory:
            result = measure_diagnostic(case(), {n: Worker(bad="stages") for n in ("reference", "candidate")},
                                        "fp32", "cpu", api=api, commitment_path=Path(directory)/"frozen.json")
        self.assertFalse(result["model_comparisons"]["component"]["available"])
        self.assertTrue(result["model_comparisons"]["affine"]["available"])
        self.assertFalse(result["qualification_claim"])

    def test_full_only_gap_remains_reported_without_retuning_or_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            result = measure_diagnostic(case(), {"reference": Worker(), "candidate": Worker(bad="full-only")},
                                        "fp32", "cpu", api=api, commitment_path=Path(directory)/"frozen.json")
        for model in ("affine", "component"):
            self.assertAlmostEqual(result["frozen_commitment"]["forecasts"][model]["candidate"]["predicted_full_seconds"], 18.5)
            self.assertGreater(result["model_comparisons"][model]["relative_actual_over_predicted_error"]["candidate"], .4)
        self.assertFalse(result["frozen_commitment"]["model_selection_permitted"])
        self.assertEqual(len(result["endpoint_block"]), 4)

    def test_work_score_and_fixture_guards(self):
        for bad in ("groups", "scores", "fixture"):
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as directory:
                selected = case()
                if bad == "fixture":
                    selected["fixture_hashes"] = {"observation": "changed"}
                with self.assertRaises(ValueError):
                    measure_diagnostic(selected, {"reference": Worker(), "candidate": Worker(bad=bad)},
                                       "fp32", "cpu", api=api, commitment_path=Path(directory)/"frozen.json")

    def test_source_guard_and_exclusive_commitment(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"frozen.json"
            def changed():
                raise ValueError("source changed")
            with self.assertRaisesRegex(ValueError, "source changed"):
                measure_diagnostic(case(), {n: Worker() for n in ("reference", "candidate")}, "fp32", "cpu",
                                   api=api, commitment_path=path, source_guard=changed)
            path.write_text("existing evidence")
            with self.assertRaises(FileExistsError):
                measure_diagnostic(case(), {n: Worker() for n in ("reference", "candidate")}, "fp32", "cpu",
                                   api=api, commitment_path=path)
            self.assertEqual(path.read_text(), "existing evidence")

    def test_two_large_banks_share_one_endpoint_and_cannot_select_after_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"frozen.json"
            def both_frozen():
                frozen = json.loads(path.read_text())
                self.assertEqual(set(frozen["forecasts_by_large_count"]), {"4", "8"})
                self.assertIsNone(frozen["forecasts"])
                self.assertFalse(frozen["model_selection_permitted"])
            workers = {"reference": Worker(full_callback=both_frozen),
                       "candidate": Worker(full_callback=both_frozen, bad="full-only")}
            result = measure_diagnostic(case(), workers, "fp32", "cpu", api=api,
                                        commitment_path=path, large_counts=[4, 8])
        frozen = result["frozen_commitment"]
        self.assertEqual(set(result["model_comparisons_by_large_count"]), {"4", "8"})
        self.assertIsNone(result["model_comparisons"])
        for name, worker in workers.items():
            calls = [len(r["frequency_offsets_hz"]) for r in worker.requests if r["action"] == "score"]
            for count in (1, 4, 8):
                self.assertEqual(calls.count(count), 8)
            self.assertEqual(calls.count(64), 2)
            for block in frozen["training_blocks"]:
                orders = [[score["cfo_count"] for score in row["scores"]] for row in block if row["source"] == name]
                self.assertEqual(orders[0], list(reversed(orders[1])))
                self.assertEqual(set(orders[0]), {1, 4, 8})
            costs = frozen["attributed_training_call_seconds_by_large_count"]
            self.assertLess(costs["4"][name], costs["8"][name])
        for count in ("4", "8"):
            for model in ("affine", "component"):
                self.assertAlmostEqual(frozen["forecasts_by_large_count"][count][model]["candidate"]["predicted_full_seconds"], 18.5)
                self.assertGreater(result["model_comparisons_by_large_count"][count][model][
                    "relative_actual_over_predicted_error"]["candidate"], .4)
        self.assertEqual(len(result["endpoint_block"]), 4)
        self.assertEqual(len(result["numerical_checks"]), 26)
        self.assertFalse(result["qualification_claim"])

    def test_schedule_is_fixed_and_rejects_tiny_banks(self):
        self.assertEqual(schedule(992)["training_counts"], [1, 124])
        plan = schedule(992, [32, 124])
        self.assertEqual(plan["training_counts"], [1, 32, 124])
        self.assertEqual(plan["extrapolation_factors"], {"32": 31., "124": 8.})
        self.assertIn("diagnostic only", plan["extrapolation_scope"])
        for counts in ([], [1], [32, 32], [True], [992], [32.5]):
            with self.assertRaises(ValueError):
                schedule(992, counts)
        for target in (0, 8, True):
            with self.assertRaises(ValueError):
                schedule(target)


if __name__ == "__main__":
    unittest.main()

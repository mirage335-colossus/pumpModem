#!/usr/bin/env python3
"""Cheap model/trace checks using an instrumented deterministic worker."""
import importlib.util
import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from triad_perf_adaptive import AdaptivePolicy, regression_profile
from triad_perf_fft_trace import (_digest, anchor_valid, anchor_proof_valid, anchor_applicability,
                                  measure_fft, measurement_plan, MEASUREMENT_CONTRACT)

spec = importlib.util.spec_from_file_location("fft_trace_runner", ROOT / "tools/triad-performance.py")
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)


def case(count=64):
    return {"id": "trace-case", "input_directory": "/unused/fake", "lag_max_seconds": .1,
            "frequency_offsets_hz": [i * .01 for i in range(count)], "group_tile": 32,
            "configuration": {"lag_group_count": 97}, "fixture_hashes": {"observation": "fixed"},
            "source_bound_metadata": {}, "performance_environment": {"cpu": "test", "threads": 2}}


class Worker:
    def __init__(self, factor=1., *, target=64, curve=0., corruption=None, on_full=None):
        self.factor, self.target, self.curve = factor, target, curve
        self.corruption, self.on_full = corruption, on_full
        self.requests = []
        self.identity = {"source_hashes": {"receiver.py": "unchanged"}, "python": "test", "numpy": "test",
                         "torch": "test", "threads": 2, "tf32_allowed": False}

    def request(self, request):
        self.requests.append(request)
        if request["action"] == "prepare":
            return {"wall_seconds": self.factor * 2., "stages": {"preparation": 2.}, "geometry": {
                "sample_count": 100000, "sample_rate": 1024., "nfft": 200000, "lags": 97,
                "lag_first_sample": 0, "lag_last_sample": 96, "lag_step_samples": 1,
                "group_tile": 32, "precision": request["precision"], "device": request["device"],
                "array_bound_bytes": 1000000}}
        count = len(request["frequency_offsets_hz"])
        if count == self.target and self.on_full:
            self.on_full()
        value = {"wall_seconds_including_finish": self.factor * (.5 + .25 * count + self.curve * count**2),
                 "bit_log_likelihoods": [1., 2.], "strict": {"accepted": False, "bit": 1},
                 "relaxed": {"accepted": False, "bit": 1}, "rank_histogram": [1, 2],
                 "geometry": {"group_count": 97 * count}, "measured_group_count": 97 * count,
                 "timing": {"score_seconds": .25 * count}, "arithmetic": {"bulk": "fp32"}, "resources": {}}
        if self.corruption == "groups" and count == self.target:
            value["measured_group_count"] -= 1
        if self.corruption == "scores" and count == self.target:
            value["bit_log_likelihoods"][0] = 100.
        return value


def run(selected=None, workers=None, *, policy=AdaptivePolicy(), **kwargs):
    selected = case() if selected is None else selected
    workers = {"reference": Worker(), "candidate": Worker()} if workers is None else workers
    return measure_fft(selected, workers, "fp32", "cpu", policy=policy,
                       deadline=None, api=api, **kwargs)


class TraceTests(unittest.TestCase):
    def test_affine_prediction_has_frozen_independent_full_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            def frozen_exists():
                files = list(Path(directory).glob("*.prediction.json"))
                self.assertEqual(len(files), 1)
                committed = json.loads(files[0].read_text())
                self.assertTrue(committed["prediction_frozen_before_validation"])
                self.assertAlmostEqual(committed["models"]["reference"]["predicted_full_seconds"], 18.5)
            workers = {n: Worker(on_full=frozen_exists) for n in ("reference", "candidate")}
            result = run(workers=workers, anchor_directory=directory)
        self.assertTrue(result["prediction_valid"])
        self.assertTrue(result["timing_precision_reached"])
        self.assertEqual(result["comparison"]["gate"], "pass")
        self.assertEqual(result["adaptive"]["sample_count"], 5)
        self.assertEqual(result["full_target_measurement_count_per_source"], 3)
        self.assertEqual(len(result["anchors"]["current_validation"]), 3)
        self.assertAlmostEqual(result["prediction"]["candidate"]["predicted_full_time"]["point_seconds"], 18.5)
        self.assertEqual(result["measurement_plan"]["training_counts"], [1, 8])
        self.assertEqual(result["anchors"]["qualification"]["status"], "validated")
        self.assertEqual(result["anchors"]["qualification"]["intervals"]["reference"]["sample_count"], 3)

    def test_full_bank_curvature_invalidates_predictor_without_widening_bound(self):
        workers = {"reference": Worker(), "candidate": Worker(curve=.02)}
        result = run(workers=workers)
        self.assertFalse(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "invalid")
        self.assertEqual(result["comparison"]["gate"], "inconclusive")
        self.assertEqual(result["adaptive"]["sample_count"], 5)
        self.assertTrue(any(r["relative_errors"]["candidate"] > .05 for r in result["anchors"]["current_validation"]))
        self.assertTrue(all(r["error_limit"] == .05 for r in result["anchors"]["current_validation"]))

    def test_exact_anchor_reuse_checks_one_frozen_full_endpoint_without_requalification(self):
        with tempfile.TemporaryDirectory() as directory:
            original = run(anchor_directory=directory)
            workers = {n: Worker() for n in ("reference", "candidate")}
            anchor_bytes = Path(original["anchors"]["path"]).read_bytes()
            before_anchors = sorted(Path(directory).glob("*.anchor.json"))
            def frozen_before_full():
                files = [p for p in Path(directory).glob("*.prediction.json")
                         if p != Path(original["anchors"]["prediction_commitment_path"])]
                self.assertEqual(len(files), 1)
                frozen = json.loads(files[0].read_text())
                self.assertEqual(frozen["validation_count"], 64)
                self.assertEqual(frozen["maximum_validation_repeats"], 1)
                self.assertEqual(frozen["conditional_contract"]["status"], "conditional_contract_ready")
            for worker in workers.values():
                worker.on_full = frozen_before_full
            result = run(workers=workers, anchor_directory=directory, validation_mode="reuse")
            self.assertEqual(Path(original["anchors"]["path"]).read_bytes(), anchor_bytes)
            self.assertEqual(sorted(Path(directory).glob("*.anchor.json")), before_anchors)
        self.assertTrue(original["prediction_valid"])
        self.assertTrue(result["prediction_valid"])
        self.assertTrue(result["full_target_measured"])
        self.assertEqual(len(result["anchors"]["historical_full_validation"]), 3)
        self.assertEqual([r["count"] for r in result["anchors"]["current_validation"]], [64])
        self.assertEqual(result["anchors"]["status"], "historically_supported_conditional")
        self.assertEqual(result["adaptive"]["model_validation"], "unvalidated")
        confidence = result["adaptive"]["policy"]["interval_confidence_per_look"]
        self.assertGreater(confidence, .95)
        for metric in ("reference", "candidate", "paired"):
            forecast = result["anchors"]["frozen_prediction"]["conditional_contract"]["training_forecasts"][metric]
            self.assertEqual(forecast["timing_confidence"], confidence)
        self.assertIsNone(result["anchors"]["qualification"])
        self.assertEqual(result["anchors"]["qualification_looks"], [])
        self.assertEqual(result["comparison"]["gate"], "pass")
        self.assertTrue(result["comparison"]["performance_pass_eligible"])
        self.assertFalse(result["anchors"]["conditional_model_bounds"]["future_coverage_qualified"])
        self.assertAlmostEqual(result["comparison"]["expanded_full_prediction_ratio_interval"][0], 1/1.1)
        self.assertAlmostEqual(result["prediction"]["candidate"]["predicted_full_time"][
            "conditional_model_mean_scenario_interval_seconds"][0], 18.5/1.1)
        for worker in workers.values():
            counts = [len(r["frequency_offsets_hz"]) for r in worker.requests if r["action"] == "score"]
            self.assertEqual(counts.count(64), 1)
            self.assertNotIn(16, counts)

    def test_conditional_decision_does_not_chase_timing_width(self):
        class CommonTimingJitter(Worker):
            def __init__(self, jitter):
                super().__init__()
                self.cycle = -1
                self.jitter = jitter

            def request(self, request):
                value = super().request(request)
                if request["action"] == "prepare":
                    self.cycle += 1
                    field = "wall_seconds"
                else:
                    field = "wall_seconds_including_finish"
                value[field] *= (1., 1.-self.jitter, 1.+self.jitter)[self.cycle % 3]
                return value

        policy = AdaptivePolicy(looks=(3, 5), family_count=12, stopping_rule="decision")
        with tempfile.TemporaryDirectory() as directory:
            run(anchor_directory=directory)
            result = run(workers={name: CommonTimingJitter(.01) for name in ("reference", "candidate")},
                         anchor_directory=directory, validation_mode="reuse", policy=policy)
            infeasible = run(workers={name: CommonTimingJitter(.10) for name in ("reference", "candidate")},
                             anchor_directory=directory, validation_mode="reuse", policy=policy)
            undecided = run(workers={"reference": Worker(), "candidate": Worker(1.24)},
                            anchor_directory=directory, validation_mode="reuse", policy=policy)
        self.assertEqual(result["adaptive"]["sample_count"], 3)
        self.assertFalse(result["timing_precision_reached"])
        self.assertEqual(result["adaptive"]["stop_reason"], "conditional_decision_ready_for_endpoint")
        self.assertFalse(result["adaptive"]["conditional_readiness"]["performance_pass_eligible"])
        self.assertEqual(result["full_target_measurement_count_per_source"], 1)
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["comparison"]["gate"], "pass")
        self.assertEqual(infeasible["adaptive"]["sample_count"], 5)
        self.assertEqual(infeasible["full_target_measurement_count_per_source"], 0)
        self.assertFalse(infeasible["prediction_valid"])
        self.assertEqual(infeasible["comparison"]["gate"], "inconclusive")
        self.assertEqual(infeasible["anchors"]["conditional_check"]["timing_blockers"],
                         ["reference", "candidate"])
        self.assertTrue(undecided["adaptive"]["conditional_readiness"]["sentinel_feasible"])
        self.assertEqual(undecided["adaptive"]["sample_count"], 5)
        self.assertEqual(undecided["full_target_measurement_count_per_source"], 0)
        self.assertFalse(undecided["prediction_valid"])
        self.assertEqual(undecided["anchors"]["conditional_check"]["reason"],
                         "decision_inconclusive_at_frozen_look")

    def test_hardware_and_source_mismatch_do_not_reuse_anchor(self):
        with tempfile.TemporaryDirectory() as directory:
            run(anchor_directory=directory)
            changed = case(); changed["performance_environment"]["cpu"] = "different"
            result = run(changed, anchor_directory=directory, validation_mode="reuse")
            self.assertFalse(result["prediction_valid"])
            self.assertEqual(result["anchors"]["status"], "unvalidated")
            workers = {n: Worker() for n in ("reference", "candidate")}
            workers["candidate"].identity["source_hashes"]["receiver.py"] = "new"
            result = run(workers=workers, anchor_directory=directory, validation_mode="reuse")
            self.assertFalse(result["prediction_valid"])
            self.assertEqual(result["full_target_measurement_count_per_source"], 0)

    def test_current_full_endpoint_curvature_invalidates_conditional_support(self):
        with tempfile.TemporaryDirectory() as directory:
            run(anchor_directory=directory)
            workers = {"reference": Worker(), "candidate": Worker(curve=.02)}
            result = run(workers=workers, anchor_directory=directory, validation_mode="reuse")
        self.assertFalse(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "unvalidated_request_focused_qualification")
        self.assertEqual(result["full_target_measurement_count_per_source"], 1)

    def test_implementation_slowdown_is_in_full_time_units(self):
        result = run(workers={"reference": Worker(), "candidate": Worker(1.5)})
        self.assertEqual(result["comparison"]["gate"], "performance_regression")
        self.assertEqual(result["adaptive"]["stop_reason"], "performance_regression")
        self.assertAlmostEqual(result["comparison"]["mean_candidate_full_seconds"], 27.75)
        self.assertAlmostEqual(result["comparison"]["absolute_mean_full_seconds_difference"], 9.25)

    def test_small_banks_are_direct_and_no_model_anchor_is_required(self):
        selected = case(2)
        workers = {n: Worker(target=2) for n in ("reference", "candidate")}
        result = run(selected, workers, validation_mode="reuse")
        self.assertTrue(result["measurement_plan"]["direct"])
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "direct_full")
        self.assertEqual(result["full_target_measurement_count_per_source"], 6)
        self.assertAlmostEqual(result["prediction"]["reference"]["predicted_full_time"]["point_seconds"], 3.)

    def test_full_validation_cannot_skip_work_or_change_answers(self):
        for corruption in ("groups", "scores"):
            with self.subTest(corruption=corruption):
                with self.assertRaises(api.SuiteError):
                    run(workers={"reference": Worker(), "candidate": Worker(corruption=corruption)})

    def test_auto_qualifies_missing_anchor_and_rejects_invalid_modes(self):
        result = run(validation_mode="auto", policy=AdaptivePolicy(stopping_rule="decision", **regression_profile("ci")))
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["adaptive"]["stop_reason"], "precision_reached")
        self.assertEqual(result["adaptive"]["policy"]["slowdown_threshold"], 1.30)
        self.assertEqual(result["full_target_measurement_count_per_source"], 3)
        with self.assertRaises(ValueError):
            run(validation_mode="silently-assume")
        with self.assertRaises(ValueError):
            measurement_plan(0)

    def test_holdout_scatter_is_inconclusive_not_a_proven_model_bias(self):
        class ScatteredWorker(Worker):
            def __init__(self):
                super().__init__()
                self.hold_index = 0
            def request(self, request):
                value = super().request(request)
                if request["action"] == "score" and len(request["frequency_offsets_hz"]) == self.target:
                    log_error = (-.25, 0., .25)[self.hold_index % 3]
                    self.hold_index += 1
                    value["wall_seconds_including_finish"] = 18.5*math.exp(log_error)-2.
                return value
        result = run(workers={n: ScatteredWorker() for n in ("reference", "candidate")})
        self.assertFalse(result["prediction_valid"])
        self.assertEqual(result["anchors"]["qualification"]["status"], "inconclusive")
        self.assertEqual(result["anchors"]["status"], "unvalidated")
        self.assertEqual(result["full_target_measurement_count_per_source"], 17)
        self.assertEqual([q["look_count"] for q in result["anchors"]["qualification_looks"]], [3, 5, 9, 17])

    def test_mean_equivalence_can_pass_despite_individual_outside_five_percent(self):
        class ScatteredWorker(Worker):
            def __init__(self):
                super().__init__()
                self.hold_index = 0
            def request(self, request):
                value = super().request(request)
                if request["action"] == "score" and len(request["frequency_offsets_hz"]) == self.target:
                    log_error = (-.06, .06)[self.hold_index % 2]
                    self.hold_index += 1
                    value["wall_seconds_including_finish"] = 18.5*math.exp(log_error)-2.
                return value
        result = run(workers={n: ScatteredWorker() for n in ("reference", "candidate")})
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["qualification"]["status"], "validated")
        self.assertTrue(any(not row["single_run_within_bound"] for row in result["anchors"]["current_validation"]))
        self.assertEqual(result["anchors"]["frozen_prediction"]["maximum_validation_repeats"], 17)
        self.assertTrue(all(q["intervals"]["reference"]["confidence"] == .975
                            for q in result["anchors"]["qualification_looks"]))

    def test_training_finishes_before_one_frozen_holdout_look(self):
        class NoisyTrainingWorker(Worker):
            def __init__(self):
                super().__init__()
                self.prepares = 0
            def request(self, request):
                if request["action"] == "prepare":
                    self.prepares += 1
                    self.factor = (1., .9, 1.1, .9, 1.1, 1.)[self.prepares-1] if self.prepares <= 6 else 1.
                return super().request(request)
        result = run(workers={n: NoisyTrainingWorker() for n in ("reference", "candidate")})
        frozen = result["anchors"]["frozen_prediction"]
        self.assertGreater(frozen["training_outer_count"], 5)
        self.assertIn(frozen["training_outer_count"], [9, 17, 33])
        self.assertEqual(frozen["training_outer_count"], result["adaptive"]["sample_count"])
        self.assertLessEqual(len(result["anchors"]["current_validation"]), frozen["maximum_validation_repeats"])
        self.assertIn(len(result["anchors"]["current_validation"]), [3, 5, 9, 17])

    def test_small_inner_batch_calls_are_not_independent_outer_samples(self):
        result = run(case(2), workers={n: Worker(.001, target=2) for n in ("reference", "candidate")}, validation_mode="reuse")
        self.assertGreater(result["measurement_plan"]["timing_block_invocations"], 1)
        self.assertEqual(result["adaptive"]["sample_count"], 5)
        self.assertEqual(len(result["raw"]["reference"]["blocks"]), 5)
        self.assertGreater(result["full_target_measurement_count_per_source"], 5)
        self.assertFalse(result["measurement_plan"]["discarded_pilot"]["inference_eligible"])

    def test_pilot_actual_preparation_cost_selects_direct_before_inference(self):
        class PreparationHeavy(Worker):
            def request(self, request):
                value = super().request(request)
                if request["action"] == "prepare":
                    value["wall_seconds"] = 100.
                return value
        result = run(workers={n: PreparationHeavy() for n in ("reference", "candidate")})
        guard = result["measurement_plan"]["pilot_cost_guard"]
        self.assertFalse(guard["original_count_plan"]["direct"])
        self.assertTrue(guard["switch_to_direct"])
        self.assertTrue(result["measurement_plan"]["direct"])
        self.assertEqual(result["anchors"]["status"], "direct_full")
        self.assertFalse(guard["inference_eligible"])
        self.assertEqual(result["full_target_measurement_count_per_source"], 5)
        self.assertAlmostEqual(result["prediction"]["candidate"]["predicted_full_time"]["point_seconds"], 116.5)

    def test_score_heavy_pilot_keeps_abbreviation(self):
        result = run()
        guard = result["measurement_plan"]["pilot_cost_guard"]
        self.assertTrue(guard["applied"])
        self.assertFalse(guard["switch_to_direct"])
        self.assertLess(guard["abbreviated_over_estimated_full"]["reference"], .8)
        self.assertEqual(result["adaptive"]["sample_count"], 5)

    def test_anchor_recomputes_raw_rows_and_frozen_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            result = run(anchor_directory=directory)
            record = json.loads(Path(result["anchors"]["path"]).read_text())
            identity = result["anchors"]["identity"]
            self.assertTrue(anchor_valid(record, identity))
            mutations = [
                lambda a: a["full_validation"][0]["observed"]["reference"].update(preparation_seconds=2.1),
                lambda a: a["full_validation"][0]["predicted_seconds"].update(reference=100.),
                lambda a: a["full_validation"][0]["log_errors"].update(reference=.01),
                lambda a: a["frozen_prediction"].update(identity_sha256="0"*64),
                lambda a: a["frozen_prediction"].update(validation_count=63),
                lambda a: a["frozen_prediction"].update(target_count=63),
                lambda a: a["full_validation"][1].update(repeat=0),
                lambda a: a["qualification"]["intervals"]["reference"].update(confidence=.9),
            ]
            for mutation in mutations:
                with self.subTest(mutation=mutation):
                    bad = copy.deepcopy(record)
                    mutation(bad)
                    bad["payload_sha256"] = _digest({k: v for k, v in bad.items() if k != "payload_sha256"})
                    self.assertFalse(anchor_valid(bad, identity))

    def test_reporting_harness_protocol_change_preserves_historical_proof_and_applicability(self):
        with tempfile.TemporaryDirectory() as directory:
            prior = run(anchor_directory=directory)
            self.assertIn("adaptive_protocol_sha256", prior["anchors"]["identity"])
            with mock.patch("triad_perf_fft_trace._adaptive_protocol_hash", return_value="0"*64):
                result = run(anchor_directory=directory, validation_mode="reuse")
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "historically_supported_conditional")
        original = result["anchors"]["historical_record"]
        self.assertTrue(anchor_proof_valid(original))
        self.assertNotEqual(original["identity"]["adaptive_protocol_sha256"], "0"*64)
        self.assertEqual(result["anchors"]["applicability"]["current_adaptive_protocol_sha256"], "0"*64)
        self.assertEqual(result["anchors"]["applicability"]["measurement_contract"], MEASUREMENT_CONTRACT)

    def test_prospective_subsecond_full_cost_uses_direct_timing(self):
        result = run(workers={n: Worker(.02) for n in ("reference", "candidate")}, validation_mode="reuse")
        guard = result["measurement_plan"]["pilot_cost_guard"]
        self.assertTrue(guard["cheap_full_direct"])
        self.assertEqual(result["measurement_plan"]["selection_reason"], "pilot_full_under_one_second")
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "direct_full")
        self.assertEqual(result["full_target_measurement_count_per_source"], 5)
        self.assertIsNone(result["anchors"]["frozen_prediction"])

    def test_full_size_only_slowdown_cannot_hide_behind_unchanged_training(self):
        class FullOnlySlowdown(Worker):
            def request(self, request):
                value = super().request(request)
                if request["action"] == "score" and len(request["frequency_offsets_hz"]) == self.target:
                    value["wall_seconds_including_finish"] *= 1.6
                return value
        with tempfile.TemporaryDirectory() as directory:
            run(anchor_directory=directory)
            result = run(workers={"reference": Worker(), "candidate": FullOnlySlowdown()},
                         anchor_directory=directory, validation_mode="reuse",
                         policy=AdaptivePolicy(stopping_rule="decision"))
        self.assertEqual(result["adaptive"]["conditional_readiness"]["provisional_gate"], "pass")
        self.assertEqual(result["full_target_measurement_count_per_source"], 1)
        self.assertFalse(result["prediction_valid"])
        self.assertFalse(result["comparison"]["performance_pass_eligible"])
        self.assertEqual(result["comparison"]["gate"], "inconclusive")
        self.assertTrue(result["anchors"]["conditional_check"]["focused_qualification_required"])
        self.assertEqual(result["anchors"]["conditional_check"]["checks"]["paired"]["classification"],
                         "outside_frozen_envelope")

    def test_uniform_large_slowdown_retains_conditional_model_but_fails_performance_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            run(anchor_directory=directory)
            result = run(workers={"reference": Worker(), "candidate": Worker(1.5)},
                         anchor_directory=directory, validation_mode="reuse")
        self.assertTrue(result["prediction_valid"])
        self.assertEqual(result["anchors"]["status"], "historically_supported_conditional")
        self.assertEqual(result["comparison"]["gate"], "performance_regression")
        self.assertFalse(result["comparison"]["performance_pass_eligible"])
        self.assertAlmostEqual(result["comparison"]["expanded_full_prediction_ratio_interval"][0], 1.5/1.1)

    def test_applicability_ignores_only_reviewed_harness_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            prior = run(anchor_directory=directory)
            record = json.loads(Path(prior["anchors"]["path"]).read_text())
        identity = copy.deepcopy(record["identity"])
        identity.update(protocol_sha256="changed-reporting", adaptive_protocol_sha256="changed-reporting")
        self.assertTrue(anchor_applicability(record, identity)["compatible"])
        changes = [
            lambda i: i["geometry"].update(nfft=200001),
            lambda i: i["performance_environment"].update(cpu="different"),
            lambda i: i["worker_implementations"]["candidate"]["source_hashes"].update(receiver="changed"),
            lambda i: i["input_sha256"].update(observation="changed"),
            lambda i: i["frequency_offsets_hz"].append(1.),
            lambda i: i.update(precision="fp64"),
            lambda i: i.pop("measurement_contract"),
        ]
        for change in changes:
            current = copy.deepcopy(identity)
            change(current)
            with self.subTest(current=current):
                self.assertFalse(anchor_applicability(record, current)["compatible"])
                self.assertTrue(anchor_proof_valid(record))

    def test_confirmation_stops_at_first_predeclared_equivalent_look(self):
        result = run()
        self.assertEqual(result["anchors"]["frozen_prediction"]["maximum_validation_repeats"], 17)
        self.assertEqual(len(result["anchors"]["current_validation"]), 3)
        looks = result["anchors"]["qualification_looks"]
        self.assertEqual([q["look_count"] for q in looks], [3])
        self.assertEqual(looks[0]["confirmation_policy"]["looks"], [3, 5, 9, 17])
        self.assertEqual(looks[0]["intervals"]["paired"]["confidence"], .975)


if __name__ == "__main__":
    unittest.main()

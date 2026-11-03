"""Mock-only contracts for the one-shot sustained performance helpers."""
from __future__ import annotations

from pathlib import Path
import math
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import triad_perf_native as native
import triad_perf_adaptive as adaptive
import triad_perf_fft_trace as fft_trace
import triad_perf_sustained as sustained
import triad_perf_conditional as conditional
import tests.test_triad_perf_fft_trace as fft_fixture


def native_qualified(case, count=3, reference_variation=True):
    regime = {"shape": "fixed"}
    prediction = {"target_total_rows": 100, "regime_sha256": native.digest(regime),
                  "stage_seconds": {"receiver": 8., "generation_setup_other": 2.,
                                    "process_startup_output": .5},
                  "full_test_wall_seconds": 10.5}
    identities = {"reference": {"binary": "reference"}, "candidate": {"binary": "same"}}
    frozen = {"case_sha256": native.digest(case), "identities": identities,
              "predictions": {"reference": prediction, "candidate": prediction},
              "validation_predictions": {"reference": prediction, "candidate": prediction},
              "validation_duration_seconds": case["target_seconds"],
              "model_confirmation_policy": adaptive.model_confirmation_policy().to_dict()}
    frozen["sha256"] = native.digest(frozen)
    holds = []
    for index in range(count):
        sources = ("reference", "candidate") if index % 2 == 0 else ("candidate", "reference")
        for source in sources:
            # Paired reference variation leaves the joint gate inconclusive at
            # the first looks, while candidate residuals remain centered.
            log_error = ((-.075, .075, 0.)[index % 3] if reference_variation else 0.) if source == "reference" else 0.
            scale = math.exp(log_error)
            actual = {"total_rows": 100, "regime": regime,
                      "stage_seconds": {"receiver": 8.*scale, "generation_setup_other": 2.*scale,
                                        "process_startup_output": .5},
                      "full_test_wall_seconds": 10.5*scale,
                      "requested_seconds": case["target_seconds"],
                      "seed": native._confirmation_seed("qualify", index)}
            holds.append({"source": source, "index": index, "kind": "qualify",
                          "frozen_prediction_sha256": frozen["sha256"], "prediction": prediction,
                          "actual": actual,
                          "validation": native.validate_holdout(prediction, actual, maximum_relative_error=.05)})
    look_counts = [n for n in adaptive.MODEL_CONFIRMATION_LOOKS if n <= count]
    looks = [native._qualification(holds[:2*n], .05,
                                   confidence=adaptive.MODEL_CONFIRMATION_CONFIDENCE)
             for n in look_counts]
    qualification = looks[-1]
    stop_reason = qualification["status"] if qualification["status"] in ("validated", "invalid") else "maximum_looks"
    anchor = {"schema": native.SCHEMA, "identities": identities, "case_sha256": native.digest(case),
              "validated": qualification["status"] == "validated", "log_errors": qualification["log_errors"],
              "qualification": qualification, "full_holdouts": holds, "frozen_prediction": frozen,
              "confirmation_looks": looks, "confirmation_stop_reason": stop_reason}
    anchor["anchor_sha256"] = native.digest(anchor)
    return {"prediction_valid": qualification["status"] == "validated", "identities": identities,
            "measurement_plan": {"validation_mode": "qualify", "case": case},
            "frozen_predictions": [frozen], "anchors": anchor}


def native_routine_qualified(case):
    historical = native_qualified(case, reference_variation=False)
    identities = {source: {"binary_sha256": source, "environment": {"cpu": "test"},
                           "case_sha256": native.digest(case), "adapter_sha256": "adapter",
                           "adaptive_protocol_sha256": "adaptive", "protocol": "complete-native-duration-siblings-v4",
                           "arithmetic": "fp64", "measurement_contract": native.MEASUREMENT_CONTRACT,
                           "timing_boundary": "external complete CLI test including startup and output; stages inside native trial separately retained"}
                  for source in ("reference", "candidate")}
    old_anchor = historical["anchors"]
    old_anchor["identities"] = identities
    old_anchor["frozen_prediction"]["identities"] = identities
    old_anchor["frozen_prediction"]["sha256"] = native.digest(
        {k: v for k, v in old_anchor["frozen_prediction"].items() if k != "sha256"})
    for row in old_anchor["full_holdouts"]:
        row["frozen_prediction_sha256"] = old_anchor["frozen_prediction"]["sha256"]
    old_anchor["anchor_sha256"] = native.digest({k: v for k, v in old_anchor.items() if k != "anchor_sha256"})
    regime = {"shape": "fixed"}
    full = {"target_total_rows": 100, "regime_sha256": native.digest({"shape": "fixed"}),
            "stage_seconds": {"receiver": 8., "generation_setup_other": 2.,
                              "process_startup_output": .5},
            "full_test_wall_seconds": 10.5}
    values = {"reference": [10.5, 10.5, 10.5], "candidate": [10.5, 10.5, 10.5]}
    applicability_key = native.digest({name: native._measurement_identity(value)
                                        for name, value in identities.items()})
    anchor_ids = [f"{old_anchor['anchor_sha256']}:{row['actual']['seed']}"
                  for row in old_anchor["full_holdouts"] if row["source"] == "reference"]
    training_ids = [f"native-training:{1_000_000 + 1000*i}" for i in range(3)]
    plan = conditional.build_conditional_contract(
        old_anchor["log_errors"], values, applicability_key=applicability_key,
        historical_anchor_ids=anchor_ids, training_pair_ids=training_ids,
        timing_confidence=.95)
    frozen = {"case_sha256": native.digest(case), "identities": identities,
              "predictions": {"reference": full, "candidate": full},
              "validation_predictions": {"reference": full, "candidate": full},
              "validation_duration_seconds": case["target_seconds"],
              "model_confirmation_policy": None,
              "historical_anchor_sha256": old_anchor["anchor_sha256"],
              "conditional_model": plan}
    frozen["sha256"] = native.digest(frozen)
    holds = []
    for source in ("reference", "candidate"):
        actual = {"total_rows": 100, "regime": regime,
                  "stage_seconds": {"receiver": 8., "generation_setup_other": 2.,
                                    "process_startup_output": .5},
                  "full_test_wall_seconds": 10.5,
                  "requested_seconds": case["target_seconds"],
                  "seed": native._confirmation_seed("routine", 0)}
        holds.append({"source": source, "index": 0, "kind": "routine",
                      "frozen_prediction_sha256": frozen["sha256"],
                      "prediction": full, "actual": actual,
                      "validation": native.validate_holdout(full, actual, maximum_relative_error=.05)})
    check = conditional.check_conditional_sentinel(
        plan, {source: row["actual"]["full_test_wall_seconds"] for source, row in zip(("reference", "candidate"), holds)},
        applicability_key=applicability_key, sentinel_id="native-full-sentinel:900000")
    return {"prediction_valid": check["status"] == "historically_supported_conditional", "identities": identities,
            "measurement_plan": {"validation_mode": "routine", "case": case,
                                 "compatible_historical_anchor": True},
            "frozen_predictions": [frozen], "anchors": old_anchor,
            "predicted_full_seconds": values,
            "adaptive_looks": [{"policy": {"interval_confidence_per_look": .95}}],
            "prediction": {"model_validation": check["status"]},
            "current_validation": {"holds": holds, "qualification": None,
                                   "confirmation_looks": [],
                                   "confirmation_stop_reason": check["status"],
                                   "conditional_model": check}}


def native_actual(trials, pipeline=7.):
    rows = [{"end_to_end_wall_seconds": pipeline/trials, "wall_seconds": pipeline/(2*trials)}
            for _ in range(trials)]
    return {"batch_trials": trials, "native_pipeline_seconds": pipeline,
            "full_test_wall_seconds": pipeline+.6,
            "stage_seconds": {"receiver": pipeline/2, "generation_setup_other": pipeline/2,
                              "process_startup_output": .6},
            "native_results": rows, "coverage": {"complete": True},
            "counter_summary": {"basis_values": 100*trials}}


class NativeSustainedTests(unittest.TestCase):
    def setUp(self):
        self.case = {"id": "x", "args": ["--rate", "1"], "target_seconds": 120.,
                     "batch_trials": 2, "witnesses": ["complete"]}

    def test_freezes_size_before_single_run_and_startup_is_not_multiplied(self):
        commitments, calls = [], []

        def run(binary, case, seconds, seed, **kwargs):
            self.assertEqual(len(commitments), 1)
            calls.append((case, seconds, seed, kwargs))
            return native_actual(case["batch_trials"], pipeline=7.)

        with (patch.object(native, "run_native", side_effect=run),
              patch.object(native, "identity", return_value={"binary": "same"})):
            result = sustained.native_sustained("fake-bin", self.case, native_qualified(self.case),
                                                minimum_seconds=11., threads=3,
                                                deadline=time.monotonic()+60,
                                                freeze_callback=commitments.append)
        self.assertEqual(len(calls), 1)
        campaign, seconds, _, kwargs = calls[0]
        self.assertEqual(campaign["batch_trials"], 3)  # ceil(11 / ((8+2)/2))
        self.assertEqual(self.case["batch_trials"], 2)
        self.assertEqual(seconds, 120.)
        self.assertFalse(kwargs["trace"])
        self.assertEqual(kwargs["timeout"], commitments[0]["budget_required_with_safety_factor_seconds"])
        self.assertEqual(commitments[0]["predicted_pipeline_seconds"], 15.)
        self.assertEqual(commitments[0]["predicted_process_startup_output_seconds"], .5)
        self.assertEqual(commitments[0]["predicted_external_seconds"], 15.5)
        self.assertEqual(result["observed_pipeline_span_seconds"], 7.)
        self.assertEqual(result["quarter_trial_counts"], [1, 1, 1, 0])
        self.assertIsNone(result["first_to_last_quarter_mean_ratio"])
        self.assertFalse(result["sustained_threshold_reached"])
        self.assertFalse(result["qualification_claim"])

    def test_rejects_unqualified_and_unfrozen_results_before_run(self):
        with self.assertRaisesRegex(ValueError, "frozen prediction"):
            sustained.native_sustained("fake", self.case, {"prediction_valid": False})
        with self.assertRaisesRegex(ValueError, "frozen prediction"):
            sustained.native_sustained("fake", self.case, {"prediction_valid": True})

    def test_independent_candidate_qualification_survives_inconclusive_pair_gate(self):
        result = native_qualified(self.case)
        self.assertFalse(result["prediction_valid"])
        seen = []

        def run(binary, case, seconds, seed, **kwargs):
            self.assertEqual(len(seen), 1)
            return native_actual(case["batch_trials"])

        with (patch.object(native, "run_native", side_effect=run),
              patch.object(native, "identity", return_value={"binary": "same"})):
            output = sustained.native_sustained("fake", self.case, result, minimum_seconds=11,
                                                deadline=time.monotonic()+60,
                                                freeze_callback=seen.append)
        self.assertIn("candidate_source_independently_qualified", output["commitment"]["source_qualification_scope"])

    def test_all_predeclared_holdout_counts_are_supported(self):
        for count in (3, 5, 9, 17):
            with self.subTest(count=count):
                result = native_qualified(self.case, count)
                scope = sustained._require_native_candidate(result, self.case)
                self.assertEqual(result["anchors"]["qualification"]["intervals"]["candidate"]["status"],
                                 "validated")
                self.assertIn("qualified", scope)

    def test_routine_mode_links_historical_anchor_and_recomputes_full_conditional_endpoint(self):
        case = dict(self.case)
        result = native_routine_qualified(case)
        scope = sustained._require_native_candidate(result, case)
        self.assertIn("historical full anchor", scope)
        self.assertIn("one full endpoint pair", scope)

        corrupted = native_routine_qualified(case)
        corrupted["current_validation"]["conditional_model"]["status"] = "inconclusive"
        with self.assertRaisesRegex(ValueError, "conditional"):
            sustained._require_native_candidate(corrupted, case)

    def test_budget_and_trial_cap_checked_before_freeze_or_run(self):
        commitments = []
        with (patch.object(native, "run_native") as run,
              patch.object(native, "identity", return_value={"binary": "same"})):
            with self.assertRaises(TimeoutError):
                sustained.native_sustained("fake", self.case, native_qualified(self.case), minimum_seconds=11,
                                            deadline=time.monotonic()+1,
                                            freeze_callback=commitments.append)
            with self.assertRaisesRegex(ValueError, "above cap"):
                sustained.native_sustained("fake", self.case, native_qualified(self.case), minimum_seconds=60_000,
                                            freeze_callback=commitments.append)
        self.assertFalse(commitments)
        run.assert_not_called()


class FakeApi:
    @staticmethod
    def prepare_request(case, precision, device):
        return {"action": "prepare", "precision": precision, "device": device}

    @staticmethod
    def score_request(case, count):
        return {"action": "score", "count": count}


class FakeWorker:
    def __init__(self, geometry, groups, commitments):
        self.geometry, self.groups, self.commitments = geometry, groups, commitments
        self.requests = []
        self.identity = {"source_hashes": {"worker": "sha"}, "python": "3.x",
                         "numpy": "2.x", "torch": "2.x", "threads": 1, "tf32_allowed": False}

    def request(self, request):
        self.assert_frozen()
        self.requests.append(request)
        if request["action"] == "prepare":
            return {"wall_seconds": 6., "geometry": self.geometry}
        return {"wall_seconds_including_finish": 2., "measured_group_count": self.groups,
                "arithmetic": {"multiply": "fp32"}, "resources": {"workspace_bytes": 12}}

    def assert_frozen(self):
        if not self.commitments:
            raise AssertionError("FFT execution started before commitment freeze")


class FftSustainedTests(unittest.TestCase):
    def setUp(self):
        self.case = {"id": "fft-x", "input_directory": "/retained/fixture",
                     "fixture_hashes": {"iq": "fixture-hash"},
                     "performance_environment": {"host": "test-cpu"},
                     "frequency_offsets_hz": [-2., -1., 1., 2.],
                     "configuration": {"lag_group_count": 3}}
        self.geometry = {key: value for key, value in {
            "sample_count": 1024, "sample_rate": 100., "nfft": 2048, "lags": 3,
            "lag_first_sample": 0, "lag_last_sample": 2, "lag_step_samples": 1,
            "group_tile": 128, "precision": "fp32", "device": "cpu"}.items()}

    def qualified(self):
        identity = {"schema": fft_trace.SCHEMA, "protocol_sha256": "protocol",
                    "case": "fft-x", "input_sha256": {"iq": "fixture-hash"},
                    "frequency_offsets_hz": self.case["frequency_offsets_hz"],
                    "geometry": dict(self.geometry),
                    "worker_implementations": {"candidate": {"source_hashes": {"worker": "sha"},
                                           "python": "3.x", "numpy": "2.x", "torch": "2.x",
                                           "threads": 1, "tf32_allowed": False}},
                    "performance_environment": self.case["performance_environment"],
                    "measurement_contract": fft_trace.MEASUREMENT_CONTRACT, "model_error_limit": .05,
                    "device": "cpu"}
        identity["adaptive_protocol_sha256"] = "adaptive-hash"
        identity_hash = fft_trace._digest(identity)
        model = {"preparation_seconds": 15., "fixed_score_seconds": 10.,
                 "per_cfo_seconds": 1.25, "predicted_full_seconds": 30.}
        frozen = {"schema": fft_trace.SCHEMA, "identity_sha256": identity_hash,
                  "training_outer_count": 3, "target_count": 4,
                  "models": {"reference": model, "candidate": model},
                  "validation_models": {"reference": model, "candidate": model},
                  "validation_count": 4, "maximum_validation_repeats": 3,
                  "confirmation_policy": adaptive.model_confirmation_policy().to_dict(),
                  "confirmation_plan": {"maximum_repeats_per_source": 3, "allowed_looks": [3],
                                        "confirmation_policy": adaptive.model_confirmation_policy().to_dict()},
                  "model_error_limit": .05, "prediction_frozen_before_validation": True}
        held = []
        for index in range(3):
            observed = {name: {"preparation_seconds": 15., "score_including_finish_seconds": 15.,
                               "full_seconds": 30.} for name in ("reference", "candidate")}
            held.append(fft_trace._validation_row(frozen, observed, count=4, repeat=index,
                                                   purpose="independent_full"))
        qualification_looks = [fft_trace._qualification(held[:count])
                               for count in fft_trace.QUALIFICATION_COUNTS if count <= len(held)]
        qualification = qualification_looks[-1]
        anchor_record = {"schema": fft_trace.SCHEMA, "status": "validated", "identity": identity,
                         "identity_sha256": identity_hash, "frozen_prediction": frozen,
                         "full_validation": held, "qualification": qualification,
                         "qualification_looks": qualification_looks,
                         "timing_boundary": "full preparation plus wrapped scoring on resident fixture",
                         "model_validation_is_conditional_mean_equivalence": True,
                         "individual_prediction_coverage_claim": False}
        anchor_record["payload_sha256"] = fft_trace._digest(anchor_record)
        return {"prediction_valid": True, "precision": "fp32", "fixture_hashes": {"iq": "fixture-hash"},
                "source_bound_metadata": {"source": "adapter hash"},
                "prediction": {"candidate": {"predicted_full_time": {"point_seconds": 30.}},
                               "reference": {"predicted_full_time": {"point_seconds": 30.}}},
                "anchors": {"status": "validated", "identity": identity,
                            "validation_mode": "qualify", "path": None,
                            "frozen_prediction": frozen, "current_validation": held,
                            "historical_record": anchor_record,
                            "qualification": qualification,
                            "qualification_looks": qualification_looks}}

    def routine_qualified(self):
        base = self.qualified()
        historical = base["anchors"]["historical_record"]
        identity = base["anchors"]["identity"]
        applicability = fft_trace.anchor_applicability(historical, identity)
        run_id = "fft-routine-run-001"
        training = {name: [30., 30., 30.] for name in ("reference", "candidate")}
        plan = conditional.build_conditional_contract(
            historical["qualification"]["log_errors"], training,
            applicability_key=applicability["applicability_key"],
            historical_anchor_ids=[historical["payload_sha256"] + ":full:" + str(i)
                                   for i in range(len(historical["full_validation"]))],
            training_pair_ids=[run_id + ":training:" + str(i) for i in range(3)])
        frozen = dict(base["anchors"]["frozen_prediction"])
        frozen.update(routine_kind="historical_full_endpoint_sentinel",
                      conditional_contract=plan, sentinel_id=run_id + ":full-endpoint",
                      current_run_id=run_id)
        observed = {name: {"preparation_seconds": 15., "score_including_finish_seconds": 15.,
                           "full_seconds": 30.} for name in ("reference", "candidate")}
        row = fft_trace._validation_row(frozen, observed, count=4, repeat=0,
                                        purpose="historical_endpoint_sentinel")
        check = conditional.check_conditional_sentinel(
            plan, {name: observed[name]["full_seconds"] for name in observed},
            applicability_key=applicability["applicability_key"], sentinel_id=frozen["sentinel_id"])
        bounds = conditional.conditional_model_log_bounds(plan, check)
        base["anchors"].update(validation_mode="routine", status=check["status"],
                                frozen_prediction=frozen, historical_record=historical,
                                applicability=applicability, current_validation=[row],
                                conditional_check=check, conditional_model_bounds=bounds)
        base["adaptive"] = {"policy": {"interval_confidence_per_look": .95}}
        base["prediction_valid"] = check["status"] == "historically_supported_conditional"
        base["prediction"]["candidate"]["predicted_full_seconds_by_repeat"] = training["candidate"]
        base["prediction"]["reference"]["predicted_full_seconds_by_repeat"] = training["reference"]
        return base

    def test_freezes_repetitions_then_scores_complete_bank_and_reports_short_span(self):
        commitments = []
        worker = FakeWorker(self.geometry, 12, commitments)
        result = sustained.fft_sustained(self.case, worker, self.qualified(), FakeApi(),
                                         minimum_seconds=60., deadline=time.monotonic()+120,
                                         freeze_callback=commitments.append)
        self.assertEqual(commitments[0]["repetitions"], 2)
        self.assertEqual(len(worker.requests), 4)
        self.assertTrue(all(r.get("count") == 4 for r in worker.requests if r["action"] == "score"))
        self.assertEqual(result["observed_receiver_seconds"], 16.)
        self.assertFalse(result["sustained_threshold_reached"])
        self.assertFalse(result["numeric_comparison"]["available"])
        self.assertFalse(result["qualification_claim"])

    def test_rejects_unqualified_and_unvalidated_prediction(self):
        with self.assertRaisesRegex(ValueError, "valid qualified"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, []),
                                    {"prediction_valid": False}, FakeApi())
        invalid = self.qualified()
        invalid["anchors"]["status"] = "unvalidated"
        with self.assertRaisesRegex(ValueError, "validated frozen"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, []), invalid, FakeApi())

    def test_budget_is_checked_before_freeze_or_fft_requests(self):
        frozen = []
        worker = FakeWorker(self.geometry, 12, frozen)
        with self.assertRaises(TimeoutError):
            sustained.fft_sustained(self.case, worker, self.qualified(), FakeApi(),
                                    minimum_seconds=60., deadline=time.monotonic()+20,
                                    freeze_callback=frozen.append)
        self.assertFalse(frozen)
        self.assertFalse(worker.requests)

    def test_rejects_incomplete_fft_work_and_anchor_geometry_change(self):
        frozen = []
        with self.assertRaisesRegex(ValueError, "complete declared"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 11, frozen), self.qualified(), FakeApi(),
                                    minimum_seconds=1., deadline=time.monotonic()+60,
                                    freeze_callback=frozen.append)
        changed = self.qualified()
        changed["anchors"]["identity"]["geometry"]["nfft"] += 1
        frozen.clear()
        with self.assertRaisesRegex(ValueError, "identity"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, frozen), changed, FakeApi(),
                                    minimum_seconds=1., deadline=time.monotonic()+60,
                                    freeze_callback=frozen.append)

    def test_rejects_fixture_or_environment_identity_change(self):
        changed_fixture = dict(self.case)
        changed_fixture["fixture_hashes"] = {"iq": "different"}
        with self.assertRaisesRegex(ValueError, "fixture identity"):
            sustained.fft_sustained(changed_fixture, FakeWorker(self.geometry, 12, []),
                                    self.qualified(), FakeApi(), minimum_seconds=1.,
                                    deadline=time.monotonic()+60)
        changed_environment = dict(self.case)
        changed_environment["performance_environment"] = {"host": "other-cpu"}
        with self.assertRaisesRegex(ValueError, "performance environment"):
            sustained.fft_sustained(changed_environment, FakeWorker(self.geometry, 12, []),
                                    self.qualified(), FakeApi(), minimum_seconds=1.,
                                    deadline=time.monotonic()+60)

    def test_rejects_corrupted_fft_full_holdout(self):
        corrupted = self.qualified()
        corrupted["anchors"]["current_validation"][0]["observed"]["reference"]["full_seconds"] += 1.
        with self.assertRaisesRegex(ValueError, "full-bank anchor"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, []), corrupted, FakeApi(),
                                    minimum_seconds=1., deadline=time.monotonic()+60)

    def test_routine_mode_recomputes_historical_anchor_applicability_and_endpoint(self):
        qualified = self.routine_qualified()
        frozen = []
        output = sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, frozen), qualified,
                                         FakeApi(), minimum_seconds=1., deadline=time.monotonic()+60,
                                         freeze_callback=frozen.append)
        self.assertEqual(output["commitment"]["conditional_routine_evidence"]["sentinel_id"],
                         "fft-routine-run-001:full-endpoint")
        corrupted = self.routine_qualified()
        corrupted["anchors"]["current_validation"][0]["observed"]["candidate"]["full_seconds"] = 31.
        with self.assertRaisesRegex(ValueError, "holdout total|endpoint row"):
            sustained.fft_sustained(self.case, FakeWorker(self.geometry, 12, []), corrupted, FakeApi(),
                                    minimum_seconds=1., deadline=time.monotonic()+60)

    def test_measure_fft_auto_reuse_result_uses_effective_routine_protocol(self):
        selected = fft_fixture.case()
        with tempfile.TemporaryDirectory() as directory:
            fft_fixture.run(selected, anchor_directory=directory)
            current = fft_fixture.run(selected, anchor_directory=directory, validation_mode="auto")
        self.assertEqual(current["anchors"]["validation_mode"], "auto")
        self.assertEqual(current["anchors"]["status"], "historically_supported_conditional")
        worker = fft_fixture.Worker()
        result = sustained.fft_sustained(selected, worker, current, fft_fixture.api,
                                         minimum_seconds=1., deadline=time.monotonic()+60)
        self.assertTrue(result["commitment"]["conditional_routine_evidence"])


if __name__ == "__main__":
    unittest.main()

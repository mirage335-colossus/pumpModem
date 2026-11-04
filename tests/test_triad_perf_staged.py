"""Checks for staged arithmetic timing, anchor replay, and failure decisions."""
import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import triad_perf_families as sample
from triad_perf_adaptive import AdaptivePolicy, regression_profile
import triad_perf_staged as staged


def endpoint(generation, receiver, seed=1, *, tile=16):
    work = {"unit": "blocks", "count": 24, "target_count": 24}
    receipt = {"complete_receiver": True, "request_wall_seconds": receiver,
               "receiver": {"work": work, "regime": {"tile": tile}, "result": {"scores": [0., 1.]}}}
    return {"generation": {"wall_seconds": generation, "seed": seed}, "receiver_runs": [receipt],
            "generation_work": {"samples": 960}, "complete_pipeline_wall_seconds": generation+receiver}


def native_receipt(profile, seed, n, seconds):
    block = 40
    blocks = (n+block-1)//block
    work = {"samples": n, "calendars": [{"samples": n, "blocks": blocks, "source_samples": n-8,
                                        "tail_samples": 8, "regime": {"block_samples": block}}]}
    return {"work": work, "fixture": [{"sample_count": n, "block_count": blocks,
                                      "sha256": {"manifest.json": "0"*64}}],
            "generation": {"wall_seconds": seconds, "invocation_wall_seconds": [seconds],
                           "seed": seed, "count": 1,
                           "commands": [sample.native_arguments("/native", profile, seed, 1, "/fixture")]}}


class StagedTests(unittest.TestCase):
    def test_profile_directional_decisions_preserve_model_guard(self):
        for name in ("normal", "ci", "strict"):
            policy = AdaptivePolicy(looks=(3, 5), stopping_rule="decision", **regression_profile(name))
            threshold = policy.slowdown_threshold
            factor = 1+staged.CONDITIONAL_FLOOR
            cases = ((threshold/factor-.01, "pass"), (threshold/factor+.01, "inconclusive"),
                     (threshold*factor-.01, "inconclusive"), (threshold*factor+.01, "performance_regression"))
            for ratio, expected in cases:
                rows = [{"reference": {"predicted_seconds": 1.},
                         "candidate": {"predicted_seconds": ratio}} for _ in range(5)]
                _, decision = staged._decision_timing(rows, rows, {"generation": 3, "receiver": 5}, policy)
                self.assertEqual(decision["provisional_gate"], expected)
                self.assertEqual(decision["pass_alpha_per_margin_and_look"]*4*len(policy.looks),
                                 policy.pass_error_budget)
                self.assertEqual(decision["failure_alpha_per_margin_and_look"]*4*len(policy.looks), .05)
                self.assertEqual(staged.training_policy(policy)["alpha"], .05)

    def test_compose_arithmetic_means_and_shared_uncertainty_once(self):
        gen = {"reference": [1., 2., 9., 4.], "candidate": [1., 2., 9., 4.]}
        rx = {"reference": [2., 3., 4., 5.], "candidate": [2., 3., 4., 5.]}
        timing = staged.compose_timing(gen, rx, .90)
        self.assertEqual(timing["pipeline"]["reference"]["mean"], 7.5)
        self.assertEqual(timing["stages"]["generation"]["reference"]["count"], 4)
        self.assertEqual(timing["pipeline"]["paired"]["interval"], [1., 1.])
        self.assertEqual(timing["pipeline"]["reference"]["interval"],
                         [sum(timing["stages"][stage]["reference"]["interval"][i]
                              for stage in ("generation", "receiver")) for i in range(2)])

    def test_arithmetic_ratio_interval_keeps_paired_common_slowdown(self):
        ref = [1.0, 1.5, 2.0, 2.5]
        cand = [2.0*x for x in ref]
        interval = staged.ratio_interval(ref, cand, .90)
        self.assertAlmostEqual(interval["mean"], 2.)
        self.assertAlmostEqual(interval["interval"][0], 2.)
        self.assertAlmostEqual(interval["interval"][1], 2.)

    def test_directional_arithmetic_decision_separates_model_allowance(self):
        counts = {"generation": 3, "receiver": 5}
        policy = AdaptivePolicy(looks=(3, 5), family_count=12, stopping_rule="decision")
        for ratio, expected in ((.95, "pass"), (1.17, "pass"), (1.30, "inconclusive"),
                                (1.45, "performance_regression")):
            rows = [{"reference": {"predicted_seconds": 1.},
                     "candidate": {"predicted_seconds": ratio}} for _ in range(5)]
            _, decision = staged._decision_timing(rows, rows, counts, policy)
            self.assertEqual(decision["provisional_gate"], expected)
            self.assertEqual(decision["timing_only_one_sided_confidence_across_looks"], .95)
            self.assertEqual(decision["per_direction_alpha_per_margin_and_look"], .05/8)
            self.assertAlmostEqual(decision["timing_only_one_sided_bounds"][1], max(1., ratio))
            self.assertFalse(decision["confidence_includes_unverified_model_error"])

    def test_full_generation_slowdown_cannot_hide_in_unchanged_pipeline(self):
        full = [{"reference": endpoint(3.5, 1.5), "candidate": endpoint(4.5, .5)} for _ in range(3)]
        point = {"pipeline": {"reference": 5., "candidate": 5.},
                 "generation": {"reference": 3.5, "candidate": 3.5},
                 "receiver": {"reference": 1.5, "candidate": 1.5}}
        checks = staged.validation(full, point)
        self.assertEqual(checks["pipeline"]["paired"]["status"], "validated")
        self.assertEqual(checks["generation"]["candidate"]["status"], "invalid")
        self.assertEqual(staged.outcome(checks), "invalid")

    def test_sentinel_detects_full_only_generation_underprediction(self):
        gen = {"reference": [3.5]*3, "candidate": [3.5]*3}
        rx = {"reference": [1.5]*3, "candidate": [1.5]*3}
        timing = staged.compose_timing(gen, rx, .90)
        history = {"record_sha256": "frozen", "forecast": staged.stage_forecast(timing),
                   "full_pairs": [{"reference": endpoint(3.5, 1.5), "candidate": endpoint(3.5, 1.5)}]*3}
        plan = staged.conditional_plan(history, timing, {})
        untouched = {"reference": endpoint(3.5, 1.5), "candidate": endpoint(3.5, 1.5)}
        control = {"reference": endpoint(3.5, 1.5), "candidate": endpoint(5.6, 1.5)}
        self.assertTrue(staged.check_sentinel(plan, untouched)["eligible"])
        result = staged.check_sentinel(plan, control)
        self.assertFalse(result["eligible"])
        self.assertTrue(result["stage_checks"]["generation"]["candidate"]["severe_underprediction"])

    def test_native_duration_abbreviation_keeps_selected_channel_args(self):
        profile = {"mode": "export-stats", "args": ["--seconds", "3600", "--channel", "hf",
                                                   "--white-fm", ".002", "--stats-noisy"]}
        reduced = staged.short_profile(profile, 450)
        self.assertEqual(reduced["args"], ["--seconds", "450", "--channel", "hf",
                                           "--white-fm", ".002", "--stats-noisy"])
        self.assertEqual(profile["args"][1], "3600")
        self.assertAlmostEqual(staged.extrapolate(100, 500, 3000, .12, .52), 3.02)
        with self.assertRaisesRegex(ValueError, "eightfold"):
            staged.extrapolate(100, 500, 4100, .12, .52)

    def test_generation_cache_does_not_repeat_identical_full_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            binary = directory/"native"
            binary.write_bytes(b"fixed identity")
            (directory/"scratch").mkdir()
            calibration = staged.Calibration({}, dict.fromkeys(staged.SOURCES, binary),
                directory/"scratch", directory, native_threads=1, device="cpu", precision="fp32",
                deadline=time.monotonic()+20, seed=10)
            capture = {"generation": {"wall_seconds": 3.5}, "work": {"samples": 960}, "fixture": []}
            with patch.object(calibration, "_export", return_value=([directory/"fixture"], capture)) as export:
                first = calibration.group("hf-hour-state", {"mode": "export-stats", "args": ["--seconds", "3600"]}, 1)
                second = calibration.group("other-case-same-work", {"mode": "export-stats", "args": ["--seconds", "3600"]}, 1)
            self.assertIs(first, second)
            self.assertEqual(export.call_count, 2)
            self.assertTrue((first["directory"]/"cached-receipt.json").is_file())
            self.assertNotEqual(first["directory"].parent, directory/"scratch")

    def test_small_uncertain_receiver_refines_without_more_generation(self):
        # The total is precise because generation dominates, but the same
        # small receiver has a broad stage-specific ratio.
        gen = {"reference": [2.92]*9, "candidate": [2.92]*9}
        rx = {"reference": [.353, .354, .340, .357, .342, .357, .340, .375, .342],
              "candidate": [.356, .338, .356, .345, .359, .338, .355, .339, .357]}
        policy = AdaptivePolicy(looks=(9, 17, 33), family_count=15)
        timing = staged.compose_timing(gen, rx, staged.component_confidence(staged.training_policy(policy)))
        self.assertTrue(staged._precision(timing, .05))
        self.assertEqual(staged._next_stage_counts(timing, {"generation": 9, "receiver": 9}, policy),
                         {"generation": 9, "receiver": 17})
        self.assertIsNone(staged._next_stage_counts(timing, {"generation": 9, "receiver": 33}, policy))

    def test_capped_stage_uses_conditional_cap_not_five_percent_target(self):
        gen = {"reference": [3.]*3, "candidate": [3.]*3}
        rx = {"reference": [.35]*5, "candidate": [.35]*5}
        timing = staged.compose_timing(gen, rx, .95)
        candidate = timing["stages"]["receiver"]["candidate"]
        policy = AdaptivePolicy(looks=(3, 5), family_count=2)
        counts = {"generation": 3, "receiver": 5}
        candidate["interval"] = [.35*.94, .35*1.06]
        self.assertEqual(staged._capped_conditional_blockers(timing, counts, policy), [])
        candidate["interval"] = [.2117, .60935]
        self.assertEqual(staged._capped_conditional_blockers(timing, {**counts, "receiver": 3}, policy), [])
        blockers = staged._capped_conditional_blockers(timing, counts, policy)
        self.assertEqual([(row["stage"], row["metric"], row["reason"]) for row in blockers],
                         [("receiver", "candidate", "mean_uncertainty_above_cap")])

    def test_model_freeze_and_historical_single_endpoint(self):
        profile = {"mode": "export-stats", "args": ["--seconds", "3600"]}

        class Worker:
            external_startup_seconds = 1.
            def request(self, request):
                return {"source_sha256": {"state.py": "bound"}, "environment": {"runtime": "fixed"}}

        class Calibration:
            workers = dict.fromkeys(staged.SOURCES, Worker())
            binary_hashes = dict.fromkeys(staged.SOURCES, "built")
            device, precision, native_threads = "cpu", "fp32", 1
            deadline = time.monotonic()+30
            def __init__(self, seed):
                self.seed = seed
                self.full_calls = 0
                self.ledger = {"id": "shared", "cached": {n: native_receipt(profile, seed, 960, 3.5)
                                                            for n in staged.SOURCES},
                               "siblings": (128, 450), "calibration": []}
            def group(self, *args):
                return self.ledger
            def verify_cache(self, *args):
                pass
            def verify_binaries(self):
                pass
            def generation_pair(self, group):
                seed = self.seed+100+len(group["calibration"])
                siblings = [native_receipt(staged.short_profile(profile, duration), seed, samples, 3.5*samples/960)
                            for duration, samples in ((128, 150), (450, 400))]
                predicted = staged.extrapolate(150, 400, 960, *(r["generation"]["wall_seconds"] for r in siblings))
                group["calibration"].append({n: {"predicted_seconds": predicted, "runs": siblings,
                                                   "target_work": group["cached"][n]["work"]} for n in staged.SOURCES})
            def receiver_pair(self, group, case, index):
                row = endpoint(3.5, 1.5)
                value = {**row, "predicted_seconds": 1.5, "target_work": sample.complete_work_signature(row)}
                return dict.fromkeys(staged.SOURCES, value)
            def full_pair(self, group, case, index, out, seed):
                frozen = json.loads((out/"forecast.json").read_text())
                self.full_calls += 1
                assert math.isclose(frozen["forecast"]["pipeline"]["candidate"], 5.)
                row = endpoint(3.5, 1.5, seed)
                receipt = native_receipt(profile, seed, 960, 3.5)
                row.update(generation=receipt["generation"], generation_work=receipt["work"], fixture=receipt["fixture"])
                return {**dict.fromkeys(staged.SOURCES, row), "pair_id": str(seed)}

        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            policy = AdaptivePolicy(looks=(3,), family_count=2)
            first = Calibration(11)
            case = {"id": "gaussian-hour", "fixture": "hf-hour-state", "captures": 1, "seed": 2}
            result = staged.staged_case(first, case, profile, directory/"qualify", policy=policy, mode="qualify", maximum_holds=3)
            self.assertEqual(first.full_calls, 3)
            self.assertEqual(result["status"], "validated")
            second = Calibration(12)
            anchor = directory/"qualify/result.json"
            routine_policy = AdaptivePolicy(looks=(3, 5), family_count=2, stopping_rule="decision")
            reused = staged.staged_case(second, case, profile, directory/"reuse", policy=routine_policy,
                                        mode="reuse", anchor=anchor)
            self.assertEqual(second.full_calls, 1)
            self.assertEqual(reused["status"], "historically_supported_conditional")
            self.assertEqual(reused["comparison"]["gate"], "pass")
            self.assertEqual(reused["frozen"]["timing_schedule"], [{"generation": 3, "receiver": 3}])
            self.assertEqual(reused["frozen"]["routine_refinement_stop"]["reason"],
                             "provisional_decision_ready_for_full_endpoint")

            class InconclusiveCalibration(Calibration):
                def receiver_pair(self, group, case, index):
                    pair = super().receiver_pair(group, case, index)
                    row = endpoint(3.5, 2.5)
                    pair["candidate"] = {**row, "predicted_seconds": 2.5,
                                         "target_work": sample.complete_work_signature(row)}
                    return pair

            uncertain = InconclusiveCalibration(13)
            unresolved = staged.staged_case(uncertain, case, profile, directory/"inconclusive",
                                            policy=routine_policy, mode="reuse", anchor=anchor)
            self.assertEqual(uncertain.full_calls, 0)
            self.assertEqual(unresolved["status"], "unqualified")
            self.assertEqual(unresolved["comparison"]["gate"], "inconclusive")
            self.assertFalse(unresolved["prediction_valid"])
            self.assertEqual(unresolved["frozen"]["timing_schedule"][-1], {"generation": 5, "receiver": 5})
            self.assertEqual(unresolved["confirmation_stop_reason"],
                             "frozen_decision_unresolved_or_endpoint_infeasible")

            def rehash_and_refuse(mutate, name):
                changed = copy.deepcopy(result)
                mutate(changed)
                frozen = changed["frozen"]
                frozen.pop("commitment_sha256")
                frozen["commitment_sha256"] = sample.digest(frozen)
                changed.pop("record_sha256")
                changed["record_sha256"] = sample.digest(changed)
                path = directory/f"{name}.json"
                sample.write_json(path, changed)
                self.assertIsNone(staged._historical(path, result["identity"]))

            rehash_and_refuse(lambda report: report["frozen"]["confirmation_policy"].update(
                source_estimand="mean log error"), "wrong-policy")
            rehash_and_refuse(lambda report: report["frozen"]["training_policy"].update(alpha=.5), "wrong-confidence")
            rehash_and_refuse(lambda report: report["frozen"]["timing_schedule"][0].update(receiver=9), "wrong-schedule")
            rehash_and_refuse(lambda report: report["generation_calibration"][0]["reference"]["runs"][0]
                              ["work"]["calendars"][0]["regime"].update(new_algorithm=True), "changed-native-regime")
            def overlapping_seeds(report):
                for name in staged.SOURCES:
                    receipt = native_receipt(profile, first.seed, 960, 3.5)
                    report["full_pairs"][0][name]["generation"] = receipt["generation"]
            rehash_and_refuse(overlapping_seeds, "dependent-hold")

            legacy = copy.deepcopy(result)
            driver = Path(staged.__file__).name
            legacy["identity"]["measurement_dependencies"][driver] = staged.LEGACY_DRIVER_SHA256
            legacy.pop("stage_timing_precision_reached")
            frozen = legacy["frozen"]
            for key in ("stopping_policy", "timing_schedule", "commitment_sha256"):
                frozen.pop(key)
            frozen["identity_sha256"] = sample.digest(legacy["identity"])
            frozen["commitment_sha256"] = sample.digest(frozen)
            legacy.pop("record_sha256")
            legacy["record_sha256"] = sample.digest(legacy)
            path = directory/"legacy.json"
            sample.write_json(path, legacy)
            old = staged._historical(path, result["identity"])
            self.assertEqual(old["source_applicability"]["method"], "b11_stopping_policy_only")
            self.assertTrue(old["source_applicability"]["fresh_endpoint_required"])
            with patch.object(staged, "LEGACY_MODEL_CORE_SHA256", "incompatible"):
                self.assertIsNone(staged._historical(path, result["identity"]))
            with patch.object(staged, "CONDITIONAL_FLOOR", .11):
                self.assertIsNone(staged._historical(path, result["identity"]))

            legacy_split = copy.deepcopy(result)
            legacy_split["identity"]["measurement_dependencies"][driver] = staged.LEGACY_SPLIT_DRIVER_SHA256
            for name, (previous, _) in staged.REVIEWED_ROUTINE_DEPENDENCIES.items():
                legacy_split["identity"]["measurement_dependencies"][name] = previous
            frozen = legacy_split["frozen"]
            frozen.pop("commitment_sha256")
            frozen["identity_sha256"] = sample.digest(legacy_split["identity"])
            frozen["commitment_sha256"] = sample.digest(frozen)
            legacy_split.pop("record_sha256")
            legacy_split["record_sha256"] = sample.digest(legacy_split)
            path = directory/"legacy-split.json"
            sample.write_json(path, legacy_split)
            old = staged._historical(path, result["identity"])
            self.assertEqual(old["source_applicability"]["method"], "9f_routine_effort_only")
            self.assertEqual(set(old["source_applicability"]["reviewed_routine_dependency_transitions"]),
                             set(staged.REVIEWED_ROUTINE_DEPENDENCIES))
            changed_identity = {**result["identity"], "native_threads": 3}
            self.assertIsNone(staged._historical(path, changed_identity))
            changed_dependencies = dict(result["identity"]["measurement_dependencies"],
                                        **{"triad-performance.py": "unreviewed"})
            self.assertIsNone(staged._historical(path, {**result["identity"],
                                                       "measurement_dependencies": changed_dependencies}))

            decision_prior = copy.deepcopy(result)
            decision_prior["identity"]["measurement_dependencies"][driver] = staged.LEGACY_DECISION_DRIVER_SHA256
            decision_prior["identity"]["measurement_dependencies"].update(staged.REVIEWED_DECISION_DEPENDENCIES)
            frozen = decision_prior["frozen"]
            frozen.pop("commitment_sha256")
            frozen["identity_sha256"] = sample.digest(decision_prior["identity"])
            frozen["commitment_sha256"] = sample.digest(frozen)
            decision_prior.pop("record_sha256")
            decision_prior["record_sha256"] = sample.digest(decision_prior)
            decision_path = directory/"decision-prior.json"
            sample.write_json(decision_path, decision_prior)
            prior = staged._historical(decision_path, result["identity"])
            self.assertEqual(prior["source_applicability"]["method"], "4395_regression_profile_only")
            profile_identity = copy.deepcopy(result["identity"])
            profile_identity["measurement_dependencies"][driver] = staged.LEGACY_PROFILE_DRIVER_SHA256
            profile_identity["measurement_dependencies"].update(staged.REVIEWED_PROFILE_DEPENDENCIES)
            mapping = staged._history_source_applicability(profile_identity, result["identity"])
            self.assertEqual(mapping["method"], "40e_explicit_native_route_only")
            direct_identity = copy.deepcopy(result["identity"])
            direct_identity["measurement_dependencies"][driver] = staged.LEGACY_NATIVE_DIRECT_DRIVER_SHA256
            direct_identity["measurement_dependencies"].update(staged.REVIEWED_NATIVE_DIRECT_DEPENDENCIES)
            mapping = staged._history_source_applicability(direct_identity, result["identity"])
            self.assertEqual(mapping["method"], "114_fixture_cache_and_arithmetic_policy_only")

            class ExhaustedCalibration(Calibration):
                def receiver_pair(self, group, case, index):
                    pair = super().receiver_pair(group, case, index)
                    seconds = (1.4, 1.6, 2.7, .8, 3.6)[index]
                    row = endpoint(3.5, seconds)
                    pair["candidate"] = {**row, "predicted_seconds": seconds,
                                         "target_work": sample.complete_work_signature(row)}
                    return pair

                def full_pair(self, group, case, index, out, seed):
                    frozen = json.loads((out/"forecast.json").read_text())
                    assert frozen["routine_refinement_stop"]["reason"] == "capped_stage_precludes_conditional_endpoint"
                    self.full_calls += 1
                    row = endpoint(3.5, 1.5, seed)
                    receipt = native_receipt(profile, seed, 960, 3.5)
                    row.update(generation=receipt["generation"], generation_work=receipt["work"], fixture=receipt["fixture"])
                    return {**dict.fromkeys(staged.SOURCES, row), "pair_id": str(seed)}

            exhausted = ExhaustedCalibration(15)
            stopped = staged.staged_case(exhausted, case, profile, directory/"capped",
                policy=AdaptivePolicy(looks=(3, 5), family_count=2), mode="reuse", anchor=anchor)
            self.assertEqual(stopped["frozen"]["timing_schedule"][-1], {"generation": 3, "receiver": 5})
            self.assertEqual(len(stopped["generation_calibration"]), 3)
            self.assertEqual(exhausted.full_calls, 1)
            self.assertEqual(stopped["status"], "unqualified")
            exhausted_decision = ExhaustedCalibration(16)
            skipped = staged.staged_case(exhausted_decision, case, profile, directory/"capped-decision",
                policy=routine_policy, mode="reuse", anchor=anchor)
            self.assertEqual(skipped["frozen"]["timing_schedule"][-1], {"generation": 3, "receiver": 5})
            self.assertEqual(skipped["frozen"]["routine_refinement_stop"]["reason"],
                             "capped_stage_precludes_conditional_endpoint")
            self.assertEqual(exhausted_decision.full_calls, 0)
            self.assertEqual(skipped["status"], "unqualified")


if __name__ == "__main__":
    unittest.main()

"""Cheap protocol and inference checks; no receiver campaign is executed."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import triad_perf_lifecycle as lifecycle
from triad_perf_adaptive import AdaptivePolicy, regression_profile


def observation(fixed=1., warm=2., cycles=1, value=None):
    call = {"receiver": {"work": {"count": 5, "target_count": 5, "unit": "blocks"},
                         "regime": {"same": True}, "result": {"scores": [1., 0.], "winner": 0, "admissions": [True, False]}},
            "complete_receiver": True}
    signature = lifecycle.sample.complete_work_signature({"receiver_runs": [call]})
    return {"fixed_seconds": fixed, "warm_seconds": warm,
            "complete_seconds": fixed+cycles*warm if value is None else value,
            "cycles": cycles, "environment": {"same": True}, "cycle_seconds": [warm]*cycles,
            "first": [{"id": "one", "call": deepcopy(call)}],
            "warm": [[{"id": "one", "call": deepcopy(call)}] for _ in range(cycles)],
            "work_signature": {"one": signature}}


def pair(index, *, fixed=1., warm=2., cycles=1, factor=1.):
    value = {"reference": observation(fixed, warm, cycles),
            "candidate": observation(fixed*factor, warm*factor, cycles),
            "pair_id": "pair-"+str(index)}
    for name in lifecycle.SOURCES:
        value[name]["generation"] = {"profile": {"count": 1, "seed": index, "commands": [["binary", "--seed", str(index)]]}}
        value[name]["warm_seconds"] *= cycles
    return value


def anchor():
    training = [pair(i, fixed=10.+i*.001, warm=20.+i*.001) for i in range(3)]
    frozen = lifecycle.forecast(training, 4)
    frozen.update(work_signature={n: training[0][n]["work_signature"] for n in lifecycle.SOURCES},
                  environment={n: training[0][n]["environment"] for n in lifecycle.SOURCES})
    full = [pair(i+100, fixed=10.+i*.001, warm=20.+i*.001, cycles=4) for i in range(3)]
    check = lifecycle.model_validation(full, frozen, .1)
    value = {"schema": lifecycle.SCHEMA, "status": "validated", "forecast": frozen,
             "training": training, "training_pair_ids": [r["pair_id"] for r in training],
             "full_pairs": full, "confirmation": [check], "model_relative_error": .1,
             "minimum_campaign_seconds": 60., "sustained": lifecycle.sustained_evidence(full, 60.),
             "static_binding": {"case": {"members": ["one"]}, "precision": "fp32",
                 "model_contract": {"relative_mean_error_band": .1, "minimum_warm_campaign_seconds": 60.}}}
    value["record_sha256"] = lifecycle.digest(value)
    return value


class LifecycleInferenceTests(unittest.TestCase):
    def test_comparison_work_preserves_raw_signatures_across_process_pool_order(self):
        rows = [pair(i, cycles=2) for i in range(3)]
        for index, row in enumerate(rows):
            for side in lifecycle.SOURCES:
                value = row[side]
                calls = [value["first"], *value["warm"]]
                for cycle, members in enumerate(calls):
                    pools = [{"prefix": "numpy", "num_threads": 2}, {"prefix": "scipy", "num_threads": 2}]
                    if (index + cycle + (side == "candidate")) % 2:
                        pools.reverse()
                    members[0]["call"]["receiver"]["regime"]["threads"] = {"library_pools": pools}
                value["work_signature"]["one"] = lifecycle.sample.complete_work_signature(
                    {"receiver_runs": [value["first"][0]["call"]]})
        original = deepcopy(rows)
        canonical = lifecycle._row_work(rows[0])
        environment = lifecycle._environment_signature(rows[0])
        lifecycle._check_rows(rows, ["one"], canonical, environment, 2, "fp32")
        legacy = {n: rows[0][n]["work_signature"] for n in lifecycle.SOURCES}
        lifecycle._check_rows(rows, ["one"], legacy, environment, 2, "fp32")
        self.assertEqual(rows, original)
        for changed in ("raw_signature", "duplicate_pool", "work_count", "memory"):
            bad = deepcopy(rows)
            target = bad[1]["candidate"]
            if changed == "raw_signature":
                target["work_signature"]["one"]["regime_sha256"] = "invalid"
            elif changed == "duplicate_pool":
                pools = target["warm"][0][0]["call"]["receiver"]["regime"]["threads"]["library_pools"]
                pools.append(deepcopy(pools[0]))
            elif changed == "work_count":
                target["warm"][0][0]["call"]["receiver"]["work"]["count"] = 4
            else:
                target["warm"][0][0]["call"]["receiver"]["regime"]["memory_limit_bytes"] = 1024
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                lifecycle._check_rows(bad, ["one"], canonical, environment, 2, "fp32")

    def test_manifest_mixed_covers_each_applicable_family_once(self):
        config, _, known = lifecycle.load_manifest()
        for case in config["cases"]:
            if case["kind"] == "mixed":
                expected = {r["id"] for r in known.values() if case["device"] in r["devices"]}
                self.assertEqual(set(case["members"]), expected)
                self.assertEqual(len(case["members"]), len(expected))

    def test_generation_is_shared_only_with_identical_profile(self):
        known = {"one": {"fixture": "a", "captures": 1}, "three": {"fixture": "a", "captures": 3},
                 "different": {"fixture": "b", "captures": 1}}
        self.assertEqual(lifecycle.group_plan(list(known), known), {"a": 3, "b": 1})

    def test_forecast_counts_joint_fixed_cost_once(self):
        rows = [pair(i, fixed=i+1., warm=2*(i+1.)) for i in range(3)]
        result = lifecycle.forecast(rows, 4)
        self.assertEqual(result["outer_predictions"]["reference"], [9., 18., 27.])
        self.assertEqual(result["point_seconds"]["reference"], 18.)

    def test_profiles_have_separate_pass_and_failure_decisions(self):
        values = [9.99, 10., 10.01]
        candidate = [1.5*v for v in values]
        normal = AdaptivePolicy(stopping_rule="decision", looks=lifecycle.LOOKS, **regression_profile("normal"))
        relaxed = AdaptivePolicy(stopping_rule="decision", looks=lifecycle.LOOKS, **regression_profile("ci"))
        self.assertEqual(lifecycle.comparison(values, candidate, normal)["gate"], "performance_regression")
        row = lifecycle.comparison(values, candidate, relaxed)
        self.assertEqual(row["gate"], "pass")
        self.assertEqual(row["failure_error_budget"], .05)
        self.assertEqual(row["pass_error_budget"], .3)

    def test_symmetric_model_band_does_not_overstate_lower_bound(self):
        policy = AdaptivePolicy(stopping_rule="decision", looks=lifecycle.LOOKS, **regression_profile("normal"))
        row = lifecycle.comparison([1., 1., 1.], [1.44, 1.44, 1.44], policy, allowance=.1)
        self.assertAlmostEqual(row["model_adjusted_ratio_interval"][0], 1.296)
        self.assertEqual(row["gate"], "inconclusive")

    def test_centered_fieller_keeps_identical_large_times_finite(self):
        values = [1e9, 1e9+1, 1e9-1]
        self.assertEqual(lifecycle.ratio_interval(values, values, .01), [1., 1.])

    def test_arithmetic_validation_detects_complete_pipeline_error(self):
        value = anchor()
        self.assertEqual(value["confirmation"][0]["status"], "validated")
        full = deepcopy(value["full_pairs"])
        for row in full:
            row["candidate"]["complete_seconds"] *= 1.4
        self.assertEqual(lifecycle.model_validation(full, value["forecast"], .1)["status"], "invalid")

    def test_anchor_replays_training_and_independent_complete_work(self):
        value = anchor()
        self.assertTrue(lifecycle._validate_anchor(value))
        for target in ("forecast", "work", "independence"):
            bad = deepcopy(value)
            if target == "forecast":
                bad["forecast"]["point_seconds"]["candidate"] *= 1.02
            elif target == "work":
                bad["full_pairs"][0]["candidate"]["work_signature"] = {"changed": True}
            else:
                bad["full_pairs"][0]["pair_id"] = bad["training_pair_ids"][0]
            bad["record_sha256"] = lifecycle.digest({k: v for k, v in bad.items() if k != "record_sha256"})
            with self.subTest(target=target):
                self.assertFalse(lifecycle._validate_anchor(bad))

    def test_modeled_anchor_keeps_unchanged_sustained_stability_guard(self):
        value = anchor()
        value["full_pairs"][0]["reference"]["cycle_seconds"] = [17., 20., 20., 23.]
        value["sustained"] = lifecycle.sustained_evidence(value["full_pairs"], 60.)
        value["record_sha256"] = lifecycle.digest({k:v for k,v in value.items() if k != "record_sha256"})
        self.assertTrue(value["sustained"]["minimum_reached"])
        self.assertTrue(value["sustained"]["trend_review_required"])
        self.assertFalse(lifecycle._validate_anchor(value))

    def test_full_only_slowdown_falsifies_model_sentinel(self):
        value = anchor()
        current = value["forecast"]
        self.assertTrue(lifecycle.sentinel_readiness(value, current)["feasible"])
        full = deepcopy(value["full_pairs"][0])
        self.assertTrue(lifecycle._sentinel(value, current, full)["eligible"])
        full["candidate"]["complete_seconds"] *= 1.5
        self.assertFalse(lifecycle._sentinel(value, current, full)["eligible"])

    def test_joint_sentinel_catches_opposite_small_source_errors(self):
        value = anchor()
        full = deepcopy(value["full_pairs"][0])
        full["reference"]["complete_seconds"] *= .92
        full["candidate"]["complete_seconds"] *= 1.08
        check = lifecycle._sentinel(value, value["forecast"], full)
        self.assertFalse(check["eligible"])
        self.assertFalse(check["sources"]["paired"]["eligible"])

    def test_budget_guard_reserves_the_entire_next_statistical_look(self):
        row = lifecycle.next_look_budget(5, looks=(3,5,9,17), maximum=9,
            remaining_seconds=240., estimated_pair_seconds=230.)
        self.assertFalse(row["allowed"])
        self.assertEqual(row["next_look"], 9)
        self.assertAlmostEqual(row["estimated_additional_seconds_with_margin"], 1104.)
        row = lifecycle.next_look_budget(3, looks=(3,5,9,17), maximum=9,
            remaining_seconds=600., estimated_pair_seconds=230.)
        self.assertTrue(row["allowed"])

    def test_direct_auto_plan_protects_faster_source_minimum_wall(self):
        pilot = {"reference": {"warm_seconds": 20.}, "candidate": {"warm_seconds": 10.}}
        plan = lifecycle.direct_cycle_plan("auto", pilot, 60.)
        self.assertEqual(plan["cycles"],8)
        self.assertEqual(plan["selection"],"auto")
        self.assertFalse(plan["forecast_applied"])
        self.assertFalse(plan["pilot_is_statistical_sample"])
        self.assertEqual(lifecycle.direct_cycle_plan(6,None,60.)["cycles"],6)
        for bad in ("1","257","auto"):
            with self.subTest(value=bad), self.assertRaises(ValueError):
                lifecycle.direct_cycle_plan(bad,None,60.)


class LifecycleBoundaryTests(unittest.TestCase):
    def test_failed_pair_keeps_complete_raw_observations_before_validation(self):
        import time
        case = {"id": "retained-failure", "kind": "mixed", "device": "cpu", "cache": "process-cold",
                "members": ["one"], "direct_cycles": 6}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/"run"
            def measured(root, binary, case, families, known, **kwargs):
                value = pair(kwargs["seed"], cycles=kwargs["cycles"])["reference"]
                if str(root).endswith("candidate"):
                    value["first"][0]["call"]["receiver"]["regime"]["tile"] = 512
                value["compiled_cache"] = {"before_count": 0, "after_count": 0, "new_artifacts": []}
                return value
            args = SimpleNamespace(mixed_method="direct",direct_cycles="6",device="cpu",precision="fp32",threads=2,
                native_threads=1,mode="reuse",anchor_directory=Path(directory)/"anchors",regression_profile="normal",
                diagnostic=False,minimum_campaign_seconds=60.,maximum_holds=9,model_relative_error=.1)
            with patch.object(lifecycle, "static_binding", return_value={"same": True}), \
                 patch.object(lifecycle, "observe", side_effect=measured), \
                 self.assertRaisesRegex(ValueError, "work regimes differ"):
                lifecycle.run_case(case, {}, {}, {n: Path(directory)/n for n in lifecycle.SOURCES},
                    {n: Path(directory)/n for n in lifecycle.SOURCES}, output, args, time.monotonic()+1000)
            retained = json.loads((output/"mixed-direct-001.json").read_text())
            self.assertEqual(set(retained), {"reference", "candidate", "pair_id"})
            self.assertEqual(retained["candidate"]["first"][0]["call"]["receiver"]["regime"]["tile"], 512)

    def test_explicit_direct_mixed_executes_full_cycles_without_model_history(self):
        import time
        case = {"id": "direct", "kind": "mixed", "device": "cpu", "cache": "process-cold",
                "members": ["one"], "direct_cycles": 6}
        binding = {"case": case, "direct_target": {"cycles": 6}, "precision": "fp32"}
        calls = []
        def observe(root, binary, case, families, known, **kwargs):
            calls.append(kwargs["cycles"])
            row = pair(kwargs["seed"], fixed=10., warm=20., cycles=kwargs["cycles"])["reference"]
            row["compiled_cache"] = {"before_count": 0, "after_count": 0, "new_artifacts": []}
            row["cycle_seconds"] = [18., 20., 20., 20., 20., 22.]
            return row
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(mixed_method="direct",device="cpu",precision="fp32",threads=2,native_threads=1,
                mode="reuse", anchor_directory=Path(directory)/"empty",regression_profile="normal",
                diagnostic=False,minimum_campaign_seconds=60.,maximum_holds=9,model_relative_error=.1)
            with patch.object(lifecycle,"static_binding",return_value=binding), \
                 patch.object(lifecycle,"observe",side_effect=observe), \
                 patch.object(lifecycle,"_validate_anchor",side_effect=AssertionError("direct route read a model")):
                result = lifecycle.run_case(case,{}, {}, {n:Path(directory)/n for n in lifecycle.SOURCES},
                    {n:Path(directory)/n for n in lifecycle.SOURCES},Path(directory)/"run",args,time.monotonic()+1000)
            self.assertEqual(calls,[6]*6)
            self.assertEqual(result["status"],"direct_full")
            self.assertEqual(result["comparison"]["gate"],"pass")
            self.assertFalse(result["historical_model_applied"])
            self.assertFalse(result["model_contract_applied"])
            self.assertEqual(result["history_status"],"inconclusive_not_applied")
            self.assertEqual(result["computational_savings"]["seconds"],0.)
            self.assertTrue(result["prediction_valid"])
            self.assertEqual(result["sustained_observation"], "complete")
            self.assertEqual(result["sustained_stability"], "review_required")
            self.assertTrue(result["sustained"]["trend_review_required"])
            self.assertNotIn("forecast",result)

    def test_direct_auto_pilot_is_discarded_and_plan_precedes_full_samples(self):
        import time
        case={"id":"direct-auto","kind":"mixed","device":"cpu","cache":"process-cold","members":["one"],"direct_cycles":6}
        calls=[]
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/"run"
            def observe(root,binary,case,families,known,**kwargs):
                cycles=kwargs["cycles"]
                if cycles!=1:
                    self.assertTrue((output/"direct-work-plan.json").is_file())
                calls.append(cycles)
                row=pair(kwargs["seed"],fixed=10.,warm=20.,cycles=cycles)["reference"]
                row["compiled_cache"]={"before_count":0,"after_count":0,"new_artifacts":[]}
                return row
            args=SimpleNamespace(mixed_method="direct",direct_cycles="auto",device="cpu",precision="fp32",threads=2,native_threads=1,
                mode="reuse",anchor_directory=Path(directory)/"empty",regression_profile="normal",diagnostic=False,
                minimum_campaign_seconds=60.,maximum_holds=9,model_relative_error=.1)
            with patch.object(lifecycle,"static_binding",return_value={"same":True}),patch.object(lifecycle,"observe",side_effect=observe):
                row=lifecycle.run_case(case,{}, {},{n:Path(directory)/n for n in lifecycle.SOURCES},
                    {n:Path(directory)/n for n in lifecycle.SOURCES},output,args,time.monotonic()+1000)
            self.assertEqual(calls,[1,1,4,4,4,4,4,4])
            self.assertEqual(len(row["full_pairs"]),3)
            self.assertEqual(row["direct_work_plan"]["cycles"],4)
            self.assertNotIn(row["discarded_direct_pilot"]["pair_id"],[r["pair_id"] for r in row["full_pairs"]])
            case["routine"]=True
            context=dict(candidate_root=directory,reference_root=directory,binary=directory,reference_binary=directory,
                device="cpu",precision="fp32",threads=2,native_threads=1,regression_profile="normal",
                anchor_directory=Path(directory)/"empty",mixed_method="direct",direct_cycles="auto")
            report={"schema":lifecycle.SCHEMA,"mode":"reuse","status":"passed","errors":[],"diagnostic":False,
                "protocol_sha256":lifecycle.sha(lifecycle.WORKER),"manifest_sha256":lifecycle.sha(lifecycle.MANIFEST),
                "comparison_contract":lifecycle.comparison_contract(),
                "regression_profile":"normal","regression_policy":regression_profile("normal"),
                "mixed_method":"direct","direct_cycles_selection":"auto","cases":[row]}
            with patch.object(lifecycle,"load_manifest",return_value=({"cases":[case],"minimum_campaign_seconds":60.}, {}, {})), \
                 patch.object(lifecycle,"static_binding",return_value={"same":True}):
                self.assertEqual(lifecycle.validate_routine_report(report,context)["status"],"passed")
                trending=deepcopy(report)
                target=trending["cases"][0]
                target["full_pairs"][0]["reference"]["cycle_seconds"]=[17.,20.,20.,23.]
                target["sustained"]=lifecycle.sustained_evidence(target["full_pairs"],60.)
                target.update(lifecycle.direct_sustained_outcomes(target["sustained"]))
                result=lifecycle.validate_routine_report(trending,context)
                self.assertTrue(result["valid"])
                self.assertEqual(result["status"],"passed")
                self.assertEqual(result["outcomes"]["sustained_observation"],"complete")
                self.assertEqual(result["outcomes"]["sustained_stability"],"review_required")
                self.assertTrue(result["review_required"][0]["release_review_required"])
                legacy=deepcopy(trending)
                legacy["protocol_sha256"]=lifecycle.REVIEWED_DIRECT_PROTOCOL5DFA
                legacy["manifest_sha256"]=lifecycle.REVIEWED_DIRECT_MANIFEST5DFA
                legacy["status"]="incomplete"
                legacy_row=legacy["cases"][0]
                legacy_row.update(status="sustained_evidence_requires_review",prediction_valid=False,
                    comparison={"gate":"inconclusive"})
                for name in ("sustained_observation","sustained_stability"):
                    legacy_row.pop(name)
                raw_digest=lifecycle.digest(legacy)
                replay=lifecycle.validate_routine_report(legacy,context)
                self.assertEqual(replay["status"],"passed")
                self.assertTrue(replay["valid"])
                self.assertEqual(replay["derived_decisions"][case["id"]]["comparison"]["gate"],"pass")
                self.assertEqual(replay["source_transfer"]["raw_report_sha256"],raw_digest)
                self.assertEqual(lifecycle.digest(legacy),raw_digest)
                unknown=deepcopy(legacy);unknown["protocol_sha256"]="0"*64
                self.assertFalse(lifecycle.validate_routine_report(unknown,context)["valid"])
                short=deepcopy(legacy)
                short["cases"][0]["full_pairs"][0]["reference"]["warm_seconds"]=59.
                short["cases"][0]["sustained"]=lifecycle.sustained_evidence(short["cases"][0]["full_pairs"],60.)
                self.assertFalse(lifecycle.validate_routine_report(short,context)["valid"])
                for count in (1,4):
                    bad=deepcopy(report)
                    bad["cases"][0]["full_pairs"]=(row["full_pairs"]+[pair(9999999999,fixed=10.,warm=20.,cycles=4)])[:count]
                    self.assertFalse(lifecycle.validate_routine_report(bad,context)["valid"])
                promoted=deepcopy(report);promoted["cases"][0]["historical_model_applied"]=True
                self.assertFalse(lifecycle.validate_routine_report(promoted,context)["valid"])
                resized=deepcopy(report);resized["cases"][0]["direct_work_plan"]["cycles"]=6
                self.assertFalse(lifecycle.validate_routine_report(resized,context)["valid"])

    def test_wrapper_replays_current_proof_and_preserves_regression_failure(self):
        cold_case = {"id": "cold", "kind": "cold", "device": "cpu", "routine": True, "members": ["one"]}
        mixed_case = {"id": "mixed", "kind": "mixed", "device": "cpu", "routine": True, "members": ["one"]}
        original = anchor()
        original["static_binding"]["case"] = mixed_case
        original["record_sha256"] = lifecycle.digest({k: v for k, v in original.items() if k != "record_sha256"})
        policy = AdaptivePolicy(stopping_rule="decision", looks=lifecycle.LOOKS, **regression_profile("normal"))
        current = [pair(i+1000, fixed=10.+i*.001, warm=20.+i*.001) for i in range(3)]
        full = [pair(2000, fixed=10., warm=20., cycles=4)]
        frozen = lifecycle.forecast(current, 4)
        frozen.update(work_signature=original["forecast"]["work_signature"], environment=original["forecast"]["environment"])
        mixed = {"id": "mixed", "kind": "mixed", "static_binding": original["static_binding"],
            "static_binding_sha256": lifecycle.digest(original["static_binding"]), "prediction_valid": True,
            "environment": frozen["environment"], "expected_work": frozen["work_signature"],
            "forecast": frozen, "training": current, "full_pairs": full,
            "comparison": lifecycle.comparison(frozen["outer_predictions"]["reference"], frozen["outer_predictions"]["candidate"], policy, allowance=.1),
            "sentinel": lifecycle._sentinel(original, frozen, full[0]),
            "sustained": lifecycle.sustained_evidence(full, 60.),
            "historical_anchor_sha256": original["record_sha256"],
            "independent_validation": {"work_regime_checked": True}}
        cold_binding = {**deepcopy(original["static_binding"]), "case": cold_case}
        cold_pairs = [pair(i+3000, fixed=10., warm=20., cycles=0) for i in range(3)]
        cold = {"id": "cold", "kind": "cold", "static_binding": cold_binding,
            "static_binding_sha256": lifecycle.digest(cold_binding), "prediction_valid": True,
            "environment": frozen["environment"], "expected_work": frozen["work_signature"],
            "full_pairs": cold_pairs, "comparison": lifecycle.comparison([10.]*3, [10.]*3, policy),
            "independent_validation": {"work_regime_checked": True}}
        for row in (cold, mixed):
            row["source_binding_sha256"] = lifecycle.digest({"static": row["static_binding"],
                "environment": row["environment"], "work": row["expected_work"]})
        report = {"schema": lifecycle.SCHEMA, "mode": "reuse", "diagnostic": False,
            "status": "passed", "errors": [],
            "protocol_sha256": lifecycle.sha(lifecycle.WORKER), "manifest_sha256": lifecycle.sha(lifecycle.MANIFEST),
            "comparison_contract": lifecycle.comparison_contract(),
            "regression_profile": "normal", "regression_policy": regression_profile("normal"), "cases": [cold, mixed]}
        with tempfile.TemporaryDirectory() as directory:
            lifecycle.save(Path(directory)/"mixed.lifecycle-anchor.json", original)
            context = dict(device="cpu", precision="fp32", threads=2, native_threads=1,
                candidate_root=directory, reference_root=directory, binary=directory, reference_binary=directory,
                regression_profile="normal", anchor_directory=directory)
            def binding(roots, binaries, case, *a, **kw):
                return cold_binding if case["id"] == "cold" else original["static_binding"]
            with patch.object(lifecycle, "load_manifest", return_value=({"cases": [cold_case, mixed_case], "minimum_campaign_seconds": 60.}, {}, {})), \
                 patch.object(lifecycle, "static_binding", side_effect=binding):
                self.assertEqual(lifecycle.validate_routine_report(report, context)["status"], "passed")
                for count in (1,4):
                    bad_count=deepcopy(report)
                    extra=pair(4999,fixed=10.,warm=20.,cycles=0)
                    bad_count["cases"][0]["full_pairs"]=(cold_pairs+[extra])[:count]
                    self.assertFalse(lifecycle.validate_routine_report(bad_count,context)["valid"])
                bad_completion=deepcopy(report);bad_completion["errors"]=["post-source guard failed"]
                self.assertFalse(lifecycle.validate_routine_report(bad_completion,context)["valid"])
                forged = deepcopy(report)
                forged["cases"][1]["comparison"]["timing_only_upper_bound"] = .5
                self.assertFalse(lifecycle.validate_routine_report(forged, context)["valid"])
                repeated = deepcopy(report)
                repeated["cases"][1]["full_pairs"][0]["reference"]["generation"] = deepcopy(current[0]["reference"]["generation"])
                repeated["cases"][1]["full_pairs"][0]["candidate"]["generation"] = deepcopy(current[0]["candidate"]["generation"])
                self.assertFalse(lifecycle.validate_routine_report(repeated, context)["valid"])
                failed = deepcopy(report)
                for row in failed["cases"][0]["full_pairs"]:
                    row["candidate"]["complete_seconds"] *= 1.5
                failed["cases"][0]["comparison"] = lifecycle.comparison([10.]*3, [15.]*3, policy)
                result = lifecycle.validate_routine_report(failed, context)
                self.assertTrue(result["valid"])
                self.assertEqual(result["status"], "failed")
                self.assertFalse(result["cold_latency_qualified"])
                self.assertEqual(result["cold_validation"]["status"], "failed")
                self.assertEqual(result["outcomes"]["fixed_mixed_campaign"], "qualified")
                mixed_failure=deepcopy(report)
                mixed_failure["cases"][1]["sentinel"]["eligible"]=False
                result=lifecycle.validate_routine_report(mixed_failure,context)
                self.assertFalse(result["valid"])
                self.assertEqual(result["status"],"unqualified")
                self.assertTrue(result["cold_latency_qualified"])
                self.assertEqual(result["cold_validation"]["status"],"passed")
                self.assertTrue(result["cold_validation"]["valid"])

    def test_complete_timer_contains_startup_generation_first_use_and_close(self):
        clock = [0.]
        workers = []
        def advance(seconds):
            clock[0] += seconds
        class Worker:
            def __init__(self, *a, **kw):
                advance(.1)
                self.closed = False
                workers.append(self)
            def request(self, request):
                return {"environment_matches_setup": True}
            def close(self):
                advance(.1)
                self.closed = True
        def setup(*a):
            advance(.2)
            return {"environment": {"same": True}}
        def export(binary, profile, seed, threads, path, count, deadline):
            advance(.3)
            return [Path(path)/str(i) for i in range(count)], {"wall_seconds": .3}
        def receive(*a):
            advance(.4)
            return {"receiver": {"work": {"count": 5, "target_count": 5, "unit": "blocks"},
                                 "regime": {"same": True}}, "complete_receiver": True}
        def audit(paths):
            advance(.05)
            return [{"input_sha256": "test", "count": len(paths)}]
        known = {"one": {"id": "one", "fixture": "profile", "captures": 1}}
        case = {"members": ["one"], "cache": "process-cold"}
        families = {"fixtures": {"profile": {}}}
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(lifecycle, "_api", return_value=SimpleNamespace(ReceiverWorker=Worker)), \
             patch.object(lifecycle.time, "perf_counter", side_effect=lambda: clock[0]), \
             patch.object(lifecycle.time, "sleep", side_effect=advance), \
             patch.object(lifecycle.sample, "case_worker_setup", side_effect=setup), \
             patch.object(lifecycle.sample, "export_fixtures", side_effect=export), \
             patch.object(lifecycle.sample, "receive", side_effect=receive), \
             patch.object(lifecycle.sample, "fixture_evidence", side_effect=audit):
            row = lifecycle.observe(Path(directory), Path(directory)/"unused", case, families, known,
                cycles=2, seed=1, threads=1, native_threads=1, device="cpu", precision="fp32",
                deadline=100., directory=directory, cache_directory=directory)
            self.assertAlmostEqual(row["complete_seconds"], 1.9)
            self.assertAlmostEqual(row["fixed_seconds"], 1.1)
            self.assertAlmostEqual(row["warm_seconds"], .8)
            self.assertAlmostEqual(row["excluded_fixture_audit_seconds"], .05)
            self.assertTrue(row["fixture"])
            baseline = row["complete_seconds"]
            delayed = lifecycle.observe(Path(directory), Path(directory)/"unused", case, families, known,
                cycles=2, seed=2, threads=1, native_threads=1, device="cpu", precision="fp32",
                deadline=100., directory=directory, cache_directory=directory, delay={"simulation": .5})
            self.assertAlmostEqual(delayed["complete_seconds"]-baseline, .5)
            self.assertAlmostEqual(delayed["warm_seconds"], row["warm_seconds"])
            self.assertTrue(all(w.closed for w in workers))

    def test_cache_environment_restores_parent_and_private_cache_is_distinct(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            previous = os.environ.get("TRITON_CACHE_DIR")
            with lifecycle.cache_environment("private-empty-compiled", Path(directory)/"private"):
                self.assertEqual(os.environ["TRITON_CACHE_DIR"], str(Path(directory)/"private"))
                self.assertEqual(lifecycle._compiled_cache_files(Path(directory)/"private"), [])
            self.assertEqual(os.environ.get("TRITON_CACHE_DIR"), previous)

    def test_wrapper_rejects_arbitrary_success_boolean(self):
        value = lifecycle.validate_routine_report({"prediction_valid": True, "status": "passed"}, {"device": "cpu"})
        self.assertFalse(value["valid"])
        self.assertEqual(value["status"], "unqualified")
        self.assertFalse(value["cold_latency_qualified"])

    def test_missing_private_cold_anchor_is_unqualified(self):
        self.assertFalse(lifecycle._validate_cold_anchor(None))

    def test_short_or_trending_warm_processing_is_visible(self):
        rows = [pair(0, fixed=1., warm=20., cycles=4)]
        result = lifecycle.sustained_evidence(rows, 60.)
        self.assertTrue(result["minimum_reached"])
        self.assertFalse(result["trend_review_required"])
        rows[0]["candidate"]["cycle_seconds"] = [20., 21., 22., 23.]
        self.assertTrue(lifecycle.sustained_evidence(rows, 60.)["trend_review_required"])
        rows[0]["reference"]["warm_seconds"] = 59.9
        self.assertFalse(lifecycle.sustained_evidence(rows, 60.)["minimum_reached"])


if __name__ == "__main__":
    unittest.main()

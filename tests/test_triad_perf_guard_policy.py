from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import sys
import tarfile
import unittest

import numpy as np
from scipy.stats import beta, binomtest, t

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_perf_guard_policy import (GuardPolicy, PairObservation, BlockObservation, build_guard_plan as current_plan,
                                    build_guard_report, confirmation_futility, confirm_case, screen_case)


def pairs(ratio, n=12, *, prefix="confirmation"):
    return [PairObservation(f"{prefix}-{i}", 10.+i/10, ratio*(10.+i/10)) for i in range(n)]


def reseal(value):
    value = copy.deepcopy(value)
    value.pop("record_sha256", None)
    value["record_sha256"] = hashlib.sha256(json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    return value


def build_guard_plan(*args, **kwargs):
    """Existing v2 checks remain explicit legacy replay checks."""
    return current_plan(*args, measurement_protocol="legacy-pair-v2", **kwargs)


def block(run_id, reference, candidate, *, first_order=("reference", "candidate")):
    return BlockObservation(run_id,
        (PairObservation(run_id+":p0", reference/2, candidate/2),
         PairObservation(run_id+":p1", reference/2, candidate/2)),
        (first_order, first_order[::-1]),
        ((run_id+":r0", run_id+":c0"), (run_id+":r1", run_id+":c1")))


class LegacyGuardPolicyTests(unittest.TestCase):
    def test_profiles_have_workflow_not_per_test_budgets(self):
        ci = build_guard_plan(["a", "b", "c"], GuardPolicy())
        dev = build_guard_plan(["a", "b", "c"], GuardPolicy("gpu-dev", workflow_job_count=2))
        self.assertEqual(ci["looks"], [12])
        self.assertEqual(ci["threshold"], 1.7)
        self.assertEqual(ci["workflow_false_rejection_budget"], .01)
        self.assertEqual(dev["looks"], [12, 20])
        self.assertAlmostEqual(dev["per_confirmation_look_alpha"]*3*2*2, .05)
        self.assertFalse(dev["cross_hypothesis_independence_required"])

    def test_exact_sign_matches_independent_scipy_binomial_test(self):
        plan = build_guard_plan(["a"])
        reference = np.array([10., 15., 9., 12., 11., 8., 14., 13., 7., 11., 12., 13.])
        candidate = np.array([18.2, 26., 16.5, 21., 19.1, 14., 25., 24., 13., 20., 21., 21.])
        rows = [PairObservation(f"c-{i}", float(a), float(b)) for i, (a,b) in enumerate(zip(reference,candidate))]
        result = confirm_case(plan, "a", rows, screen_run_ids=["screen"])
        contrasts = candidate-1.7*reference
        oracle = binomtest(int(np.sum(contrasts>0.)),12,.5,alternative="greater")
        self.assertAlmostEqual(result["exact_one_sided_p_value"],oracle.pvalue)
        self.assertEqual(result["confirmed_regression"], oracle.pvalue <= plan["per_confirmation_look_alpha"])
        self.assertAlmostEqual(result["arithmetic_mean_ratio"], float(np.mean(candidate)/np.mean(reference)))
        expected_lower = np.mean(contrasts)-t.isf(plan["per_confirmation_look_alpha"],11)*np.std(contrasts,ddof=1)/np.sqrt(12)
        diagnostic = result["arithmetic_mean_contrast_diagnostic"]
        self.assertAlmostEqual(diagnostic["gaussian_one_sided_lower_bound_seconds"], expected_lower)
        self.assertFalse(diagnostic["blocking"])

    def test_exact_threshold_does_not_reject_and_large_slowdown_does(self):
        plan = build_guard_plan(["a"])
        self.assertFalse(confirm_case(plan,"a",pairs(1.7),screen_run_ids=["s"])["confirmed_regression"])
        self.assertTrue(confirm_case(plan,"a",pairs(3.),screen_run_ids=["s"])["confirmed_regression"])

    def test_engineering_warnings_are_visible_advisory_without_nomination(self):
        plan = build_guard_plan(["a"])
        screen = screen_case(plan,"a",PairObservation("s",10.,10.),
                             warnings=["hardware one-shot warning", "prediction anchor missing"])
        self.assertFalse(screen["nominate_confirmation"])
        report = build_guard_report(plan,[screen],[])
        self.assertEqual(report["exit_code"],0)
        self.assertTrue(any("anchor missing" in x for x in report["advisory"]))
        self.assertFalse(report["affirmative_non_regression_certification"])
        self.assertEqual(report["qualification_status"],"not_evaluated_by_fast_guard")

    def test_suspicion_and_incomplete_coverage_do_not_block(self):
        plan = build_guard_plan(["a","unavailable"])
        screen = screen_case(plan,"a",PairObservation("s",10.,30.))
        report = build_guard_report(plan,[screen],[],advisory=["budget exhausted"])
        self.assertEqual(report["exit_code"],0)
        self.assertEqual(report["missing_screens"],["unavailable"])
        self.assertEqual(report["unconfirmed_nominations"],["a"])

    def test_only_full_fresh_confirmation_can_block_performance(self):
        plan = build_guard_plan(["a"])
        screen = screen_case(plan,"a",PairObservation("s",10.,30.))
        confirmation = confirm_case(plan,"a",pairs(3.),screen_run_ids=["s"])
        report = build_guard_report(plan,[screen],[confirmation])
        self.assertEqual(report["exit_code"],2)
        self.assertEqual(report["confirmed_regressions"],["a"])

    def test_false_alarm_in_screen_can_clear_without_affirmative_certificate(self):
        plan = build_guard_plan(["a"])
        screen = screen_case(plan,"a",PairObservation("s",10.,30.))
        confirmation = confirm_case(plan,"a",pairs(1.),screen_run_ids=["s"])
        report = build_guard_report(plan,[screen],[confirmation])
        self.assertEqual(report["exit_code"],0)
        self.assertEqual(report["performance_status"],"no_confirmed_regression")

    def test_functional_failure_remains_blocking(self):
        plan = build_guard_plan(["a"])
        report = build_guard_report(plan,[],[],functional_failures=["numeric result mismatch"])
        self.assertEqual(report["exit_code"],2)
        self.assertEqual(report["functional_status"],"failed")
        self.assertFalse(report["confirmed_regressions"])

    def test_freshness_fixed_looks_and_complete_family_are_enforced(self):
        plan = build_guard_plan(["a"])
        for rows in (pairs(3.,3), [PairObservation("s",10.,30.)]+pairs(3.,11)):
            with self.assertRaises(ValueError):
                confirm_case(plan,"a",rows,screen_run_ids=["s"])
        with self.assertRaises(ValueError):
            confirm_case(plan,"undeclared",pairs(3.),screen_run_ids=["s"])
        with self.assertRaises(ValueError):
            confirm_case(plan,"a",[pairs(3.)[0]]*12,screen_run_ids=["s"])

    def test_look_extension_is_prefix_bound_and_retry_cannot_erase_rejection(self):
        plan = build_guard_plan(["a"], GuardPolicy("gpu-dev"))
        screen = screen_case(plan,"a",PairObservation("s",10.,30.))
        first = confirm_case(plan,"a",pairs(1.,12),screen_run_ids=["s"])
        final = confirm_case(plan,"a",pairs(1.,20),screen_run_ids=["s"])
        self.assertEqual(build_guard_report(plan,[screen],[first,final])["exit_code"],0)
        report = build_guard_report(plan,[screen],[first])
        self.assertEqual(report["confirmation_effort_limited"],["a"])
        changed = confirm_case(plan,"a",pairs(1.,20,prefix="new"),screen_run_ids=["s"])
        with self.assertRaises(ValueError):
            build_guard_report(plan,[screen],[first,changed])
        reject = confirm_case(plan,"a",pairs(3.,12),screen_run_ids=["s"])
        with self.assertRaises(ValueError):
            build_guard_report(plan,[screen],[reject,final])

    def test_plan_and_decision_replay_reject_resealed_tampering(self):
        plan = build_guard_plan(["a"])
        changed = copy.deepcopy(plan); changed["per_confirmation_look_alpha"] = .49
        with self.assertRaises(ValueError):
            screen_case(reseal(changed),"a",PairObservation("s",10.,10.))
        screen = screen_case(plan,"a",PairObservation("s",10.,30.))
        confirmation = confirm_case(plan,"a",pairs(1.),screen_run_ids=["s"])
        changed = copy.deepcopy(confirmation); changed["confirmed_regression"] = True
        with self.assertRaises(ValueError):
            build_guard_report(plan,[screen],[reseal(changed)])

    def test_invalid_inputs_do_not_become_performance_rejections(self):
        for kwargs in ({"suite_alpha":.06},{"suite_alpha":float("nan")},{"workflow_job_count":0},{"workflow_job_count":True}):
            with self.assertRaises(ValueError): GuardPolicy(**kwargs)
        for value in (0.,-1.,float("nan"),float("inf"),True):
            with self.assertRaises(ValueError): PairObservation("x",value,1.)
        with self.assertRaises(ValueError): build_guard_plan(["same","same"])

    def test_mean_and_median_are_explicitly_different_estimands(self):
        plan = build_guard_plan(["a"])
        rows = [PairObservation(f"c-{i}",10.,18. if i<11 else 1.) for i in range(12)]
        result = confirm_case(plan,"a",rows,screen_run_ids=["s"])
        self.assertLess(result["arithmetic_mean_ratio"],1.7)
        self.assertTrue(result["confirmed_regression"])
        self.assertIn("median",result["blocking_estimand"])
        self.assertFalse(result["arithmetic_mean_contrast_diagnostic"]["blocking"])

    def test_dynamic_family_floor_preserves_attainable_exact_test(self):
        plan = build_guard_plan([f"case-{i}" for i in range(4096)],GuardPolicy(workflow_job_count=2))
        self.assertEqual(plan["looks"],[20])
        self.assertLessEqual(2.**-plan["looks"][0],plan["per_confirmation_look_alpha"])

    def test_valid_futility_and_report_replay(self):
        plan = build_guard_plan([f"case-{i}" for i in range(36)])
        screen = screen_case(plan,"case-0",PairObservation("s",10.,30.))
        rows = [PairObservation("c-0",10.,30.),PairObservation("c-1",10.,10.)]
        stop = confirmation_futility(plan,"case-0",rows,screen_run_ids=["s"])
        self.assertTrue(stop["futile"])
        report = build_guard_report(plan,[screen],[],futility_records=[stop])
        self.assertEqual(report["exit_code"],0)
        self.assertEqual(report["futile_confirmations"],["case-0"])
        self.assertFalse(report["unconfirmed_nominations"])
        attainable = confirmation_futility(plan,"case-0",pairs(3.,11),screen_run_ids=["s"])
        self.assertFalse(attainable["futile"])
        with self.assertRaises(ValueError):
            build_guard_report(plan,[screen],[],futility_records=[attainable])

    def test_dev_futility_preserves_later_recovery(self):
        plan = build_guard_plan([f"case-{i}" for i in range(39)],GuardPolicy("gpu-dev"))
        two_losses = confirmation_futility(plan,"case-0",pairs(1.,2),screen_run_ids=["s"])
        self.assertFalse(two_losses["futile"])
        three_losses = confirmation_futility(plan,"case-0",pairs(1.,3),screen_run_ids=["s"])
        self.assertTrue(three_losses["futile"])

    def test_omitted_earlier_rejection_cannot_be_erased_by_final_or_futility(self):
        plan = build_guard_plan([f"case-{i}" for i in range(39)],GuardPolicy("gpu-dev"))
        screen = screen_case(plan,"case-0",PairObservation("s",10.,30.))
        first = pairs(3.,12)
        later = first+[PairObservation(f"later-{i}",10.,10.) for i in range(8)]
        # The first twelve reject. Omitting that receipt cannot turn a later
        # twenty-pair non-rejection or fifteen-pair futility stop green.
        final = confirm_case(plan,"case-0",later,screen_run_ids=["s"])
        self.assertFalse(final["confirmed_regression"])
        stop = confirmation_futility(plan,"case-0",later[:15],screen_run_ids=["s"])
        self.assertTrue(stop["futile"])
        for confirmations,futility in (([final],[]),([],[stop])):
            with self.assertRaisesRegex(ValueError,"earlier fixed-look rejection"):
                build_guard_report(plan,[screen],confirmations,futility_records=futility)

    def test_many_correlated_nonnormal_null_signs_control_whole_workflow(self):
        # This deterministic diagnostic exercises actual plan/decision/aggregator
        # paths. It is evidence under the stated model, never a universal bound.
        rng = np.random.default_rng(79431)
        h, repetitions = 8, 512
        plan = build_guard_plan([f"case-{i}" for i in range(h)])
        screens = [screen_case(plan,name,PairObservation(f"screen-{name}",10.,30.))
                   for name in plan["hypothesis_ids"]]
        failures = 0
        for trial in range(repetitions):
            common = rng.standard_normal(12)
            confirmations = []
            for index,name in enumerate(plan["hypothesis_ids"]):
                latent = .6*common+.8*rng.standard_normal(12)
                contrast = np.where(latent>=0.,1.,-1.)+1e-6*rng.standard_normal(12)
                rows = [PairObservation(f"c-{trial}-{index}-{j}",10.,17.+float(d))
                        for j,d in enumerate(contrast)]
                confirmations.append(confirm_case(plan,name,rows,screen_run_ids=[f"screen-{name}"]))
            failures += build_guard_report(plan,screens,confirmations)["exit_code"] != 0
        upper = float(beta.ppf(.95,failures+1,repetitions-failures))
        self.assertLess(upper,.05,(failures,upper))


class CrossoverPolicyTests(unittest.TestCase):
    def test_full_block_totals_exact_sign_oracle_and_json_replay(self):
        plan = current_plan(["a"])
        candidate = [18.2,26.,16.5,21.,19.1,14.,25.,24.,13.,20.,21.,21.]
        reference = [10.,15.,9.,12.,11.,8.,14.,13.,7.,11.,12.,13.]
        rows = [block(f"b-{i}",r,c) for i,(r,c) in enumerate(zip(reference,candidate))]
        result = confirm_case(plan,"a",rows,screen_run_ids=["screen"])
        k = sum(c>1.7*r for r,c in zip(reference,candidate))
        self.assertAlmostEqual(result["exact_one_sided_p_value"],binomtest(k,12,.5,alternative="greater").pvalue)
        self.assertEqual(result["complete_pair_count"],24)
        self.assertEqual(result["block_count"],12)
        self.assertAlmostEqual(result["arithmetic_mean_ratio"],sum(candidate)/sum(reference))
        screen = screen_case(plan,"a",PairObservation("screen",10.,30.))
        result = json.loads(json.dumps(result))
        replay = build_guard_report(json.loads(json.dumps(plan)),[screen],[result])
        self.assertEqual(replay["exit_code"], 2 if result["confirmed_regression"] else 0)
        self.assertFalse(plan["independence_required"])
        self.assertTrue(plan["conditional_sign_bound_required"])

    def test_unblocked_raw_pairs_are_not_statistically_eligible(self):
        plan = current_plan(["a"])
        with self.assertRaisesRegex(ValueError,"balanced-block"):
            confirm_case(plan,"a",pairs(3.),screen_run_ids=["s"])
        with self.assertRaisesRegex(ValueError,"opposite"):
            BlockObservation("b",(PairObservation("p0",1.,2.),PairObservation("p1",1.,2.)),
                (("reference","candidate"),("reference","candidate")),(("r0","c0"),("r1","c1")))
        with self.assertRaisesRegex(ValueError,"distinct"):
            BlockObservation("b",(PairObservation("p0",1.,2.),PairObservation("p1",1.,2.)),
                (("reference","candidate"),("candidate","reference")),(("r0","c0"),("r0","c1")))

    def test_context_reuse_and_cross_hypothesis_reuse_are_rejected(self):
        plan = current_plan(["a","b"])
        rows = [block(f"block-{i}",10.,30.) for i in range(12)]
        reused = [rows[0], BlockObservation("different",rows[1].pairs,rows[1].orders,rows[0].contexts)]
        with self.assertRaisesRegex(ValueError,"cannot be reused"):
            confirmation_futility(plan,"a",reused,screen_run_ids=["screen-a"])
        screens = [screen_case(plan,name,PairObservation("screen-"+name,10.,30.)) for name in ["a","b"]]
        receipts = [confirm_case(plan,name,rows,screen_run_ids=["screen-"+name]) for name in ["a","b"]]
        with self.assertRaisesRegex(ValueError,"across hypotheses"):
            build_guard_report(plan,screens,receipts)

    def test_fixed_slot_bias_cancels_and_arbitrary_slot_interaction_is_coin_balanced(self):
        # The same two contexts stay fast/slow. Real source-to-slot crossover
        # assigns each source once to each slot, eliminating their old 19x AA bias.
        slots = [19.,1.,19.,1.]
        for coin in [0,1]:
            orders = (("reference","candidate"),("candidate","reference")) if coin==0 else (("candidate","reference"),("reference","candidate"))
            values = [{source:slots[2*j+i] for i,source in enumerate(order)} for j,order in enumerate(orders)]
            row = BlockObservation(f"b-{coin}",tuple(PairObservation(f"p-{coin}-{j}",v["reference"],v["candidate"]) for j,v in enumerate(values)),
                orders,((f"r-{coin}-0",f"c-{coin}-0"),(f"r-{coin}-1",f"c-{coin}-1")))
            self.assertEqual(row.candidate_seconds,row.reference_seconds)
        for slots in ([1.,19.,3.,17.],[1.,2.,3.,4.],[100.,.01,7.,8.]):
            contrasts=[]
            for coin in [0,1]:
                chosen = (0,3) if coin else (1,2)
                c = 1.7*sum(slots[i] for i in chosen)
                r = sum(slots[i] for i in range(4) if i not in chosen)
                contrasts.append(c-1.7*r)
            self.assertLessEqual(sum(value>1e-12 for value in contrasts),1)

    def test_complement_assignments_bound_arbitrary_slot_potential_outcomes(self):
        rng=np.random.default_rng(937125)
        for _ in range(100):
            reference=np.exp(rng.normal(0.,3.,4))
            candidate=1.7*reference*rng.uniform(0.,1.,4)
            contrasts=[]
            for chosen in ((0,3),(1,2)):
                contrasts.append(sum(candidate[i] for i in chosen)-1.7*sum(reference[i] for i in range(4) if i not in chosen))
            self.assertLessEqual(sum(value>0. for value in contrasts),1)
            self.assertLessEqual(sum(contrasts),1e-9)

    def test_futility_retains_complete_blocks_and_cannot_erase_prefix_rejection(self):
        plan=current_plan([f"case-{i}" for i in range(39)],GuardPolicy("gpu-dev"))
        screen=screen_case(plan,"case-0",PairObservation("s",10.,30.))
        rows=[block(f"b-{i}",10.,30. if i<12 else 10.) for i in range(20)]
        stop=confirmation_futility(plan,"case-0",rows[:15],screen_run_ids=["s"])
        with self.assertRaisesRegex(ValueError,"earlier fixed-look"):
            build_guard_report(plan,[screen],[],futility_records=[stop])

    def test_report_retains_sealed_futility_for_json_replay_and_rejects_nested_tampering(self):
        plan=current_plan(["a"])
        screen=screen_case(plan,"a",PairObservation("screen",10.,30.))
        stop=confirmation_futility(plan,"a",[block("b0",10.,10.),block("b1",10.,10.)],
            screen_run_ids=["screen"])
        self.assertTrue(stop["futile"])
        report=build_guard_report(plan,[screen],[],futility_records=[stop])
        self.assertEqual(report["futility_records"],[stop])
        retained=json.loads(json.dumps(report))
        replay=build_guard_report(plan,retained["screens"],retained["confirmations"],
            functional_failures=retained["functional_failures"],advisory=retained["advisory"],
            futility_records=retained["futility_records"])
        self.assertEqual(replay,report)
        altered=copy.deepcopy(retained)
        altered["futility_records"][0]["futile"]=False
        altered=reseal(altered)  # A valid outer hash must not bless a damaged inner proof.
        with self.assertRaisesRegex(ValueError,"identity mismatch"):
            build_guard_report(plan,altered["screens"],altered["confirmations"],
                futility_records=altered["futility_records"])

    def test_legacy_futility_serialization_is_retained_as_historical_only(self):
        plan=build_guard_plan(["a"])
        screen=screen_case(plan,"a",PairObservation("screen",10.,30.))
        stop=confirmation_futility(plan,"a",pairs(1.,2),screen_run_ids=["screen"])
        report=build_guard_report(plan,[screen],[],futility_records=[stop])
        self.assertNotIn("record_sha256",report["futility_records"][0])
        self.assertIn("v2",report["schema"])
        self.assertFalse(report["affirmative_non_regression_certification"])

    def test_real_frozen_v2_report_replays_without_promotion(self):
        archive=Path(__file__).resolve().parents[1]/"docs/validation-data/triad-v3/2026-11-04-performance-guard/cpu-004.tar.gz"
        with tarfile.open(archive) as source:
            plan=json.load(source.extractfile("cpu-004/result/plan.json"))
            original=json.load(source.extractfile("cpu-004/result/summary.json"))["guard_decision"]
        replay=build_guard_report(plan,original["screens"],original["confirmations"],
            functional_failures=original["functional_failures"],advisory=original["advisory"],futility_records=original["futility_records"])
        self.assertEqual(replay,original)
        self.assertIn("v2",replay["schema"])
        self.assertFalse(replay["affirmative_non_regression_certification"])

    def test_legacy_gpu_sign_decisions_preserve_nonblocking_cross_stack_diagnostic_limits(self):
        archive=Path(__file__).resolve().parents[1]/"docs/validation-data/triad-v3/2026-11-04-performance-guard/gpu-003.tar.gz"
        with tarfile.open(archive) as source:
            plan=json.load(source.extractfile("gpu-003/result/plan.json"))
            original=json.load(source.extractfile("gpu-003/result/summary.json"))["guard_decision"]
        self.assertEqual(reseal(original),original)
        for record in original["confirmations"]:
            self.assertEqual(reseal(record),record)
            screen=next(row for row in original["screens"] if row["hypothesis_id"]==record["hypothesis_id"])
            replay=confirm_case(plan,record["hypothesis_id"],record["observations"],
                screen_run_ids=[screen["observation"]["run_id"]])
            for field in ["observations","confirmed_regression","exact_one_sided_p_value","positive_pair_count","sample_count","status"]:
                self.assertEqual(replay[field],record[field])
            # The archived GPU used SciPy1.16.3, whereas another replay stack
            # may produce last-bit differences in its NONBLOCKING Student-t
            # diagnostic. Do not rewrite that receipt or claim exact full
            # cross-stack byte replay. The sign decision above remains exact.
            for field,value in record["arithmetic_mean_contrast_diagnostic"].items():
                actual=replay["arithmetic_mean_contrast_diagnostic"][field]
                if isinstance(value,float):
                    self.assertTrue(math.isclose(actual,value,rel_tol=1e-12,abs_tol=1e-12),(field,actual,value))
                else:
                    self.assertEqual(actual,value)
            self.assertFalse(record["arithmetic_mean_contrast_diagnostic"]["blocking"])


if __name__ == "__main__":
    unittest.main()

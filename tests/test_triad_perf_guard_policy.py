from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.stats import beta, binomtest, t

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_perf_guard_policy import (GuardPolicy, PairObservation, build_guard_plan,
                                    build_guard_report, confirmation_futility, confirm_case, screen_case)


def pairs(ratio, n=12, *, prefix="confirmation"):
    return [PairObservation(f"{prefix}-{i}", 10.+i/10, ratio*(10.+i/10)) for i in range(n)]


def reseal(value):
    value = copy.deepcopy(value)
    value.pop("record_sha256", None)
    value["record_sha256"] = hashlib.sha256(json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    return value


class GuardPolicyTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()

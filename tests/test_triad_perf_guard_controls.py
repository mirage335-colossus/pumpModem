from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from triad_perf_guard_controls import (binomial_interval, crossover_summary,
    randomized_crossover_blocks, simulate_case, simulate_crossover_case)


class GuardDistributionTests(unittest.TestCase):
    def spec(self,distribution,**extra):
        return {"profile":"ci","suite_alpha":.01,"hypotheses_per_job":8,
            "workflow_jobs":1,"ratio":1.7,"distribution":distribution,
            "latent_cross_case_correlation":0.,**extra}

    def test_exact_binomial_interval_handles_endpoints(self):
        self.assertEqual(binomial_interval(0,100)[0],0.)
        self.assertEqual(binomial_interval(100,100)[1],1.)

    def test_actual_aggregator_replays_vectorized_ci_and_dev_decisions(self):
        for spec in (self.spec("gaussian-contrast"),
                     self.spec("gaussian-contrast",profile="gpu-dev",suite_alpha=.05,ratio=1.3,workflow_jobs=2)):
            result = simulate_case(spec,trials=64,seed=17412,parity_workflows=4)
            self.assertEqual(result["actual_aggregator_parity"],"all matched")

    def test_non_normal_assumption_failure_is_reported_not_hidden(self):
        result = simulate_case(self.spec("rademacher-jitter"),trials=512,seed=2231,parity_workflows=4)
        self.assertLess(result["false_block_rate"],.05)
        self.assertTrue(result["median_sign_null"])
        self.assertTrue(result["pair_count_is_not_processor_wall_time"])

    def test_mean_null_bimodal_population_can_be_true_median_regression(self):
        result = simulate_case(self.spec("rare-fast-bimodal"),trials=512,seed=2311,parity_workflows=4)
        self.assertTrue(result["arithmetic_mean_null"])
        self.assertFalse(result["median_sign_null"])
        self.assertIsNone(result["false_block_rate"])
        self.assertEqual(result["truly_regressed_hypotheses"],8)

    def test_serial_unchanged_population_retains_independence_pitfall(self):
        result = simulate_case(self.spec("serial-bimodal-scheduling",ratio=1.),trials=512,seed=3211,parity_workflows=4)
        self.assertTrue(result["median_sign_null"])
        self.assertGreater(result["false_block_rate"],.5)
        self.assertIn("violating pair independence",result["assumption_status"])

    def test_randomized_crossover_breaks_same_persistent_slot_bias(self):
        rows = randomized_crossover_blocks(trials=2048,blocks=12,seed=3211,
            threshold=1.7,control="fixed-slot")
        self.assertTrue(all(tuple(row["order"]) in {
            ("candidate","reference","reference","candidate"),
            ("reference","candidate","candidate","reference")} for workflow in rows for row in workflow))
        self.assertTrue(all(len({arm["position"] for arm in workflow[0]["arms"]}) == 4 for workflow in rows))
        result = crossover_summary(rows,threshold=1.7,alpha=.05,looks=(12,))
        # The same 19x persistent execution-slot contrast is present in every
        # block. Reassignment changes only which source occupies those slots.
        self.assertLess(result["blocked_rate"],.05)
        self.assertGreater(result["blocked_rate_interval"][1],.001)
        self.assertEqual(result["inferential_unit"],
            "fair randomized four-context crossover block; two runtimes per source summed")
        self.assertFalse(result["universal_guarantee"])

    def test_crossover_detects_large_slowdown_and_retains_near_boundary_uncertainty(self):
        large = randomized_crossover_blocks(trials=256,blocks=12,seed=881,
            threshold=1.7,slowdown=2.,control="fixed-slot")
        detected = crossover_summary(large,threshold=1.7,alpha=.05,looks=(12,))
        self.assertGreaterEqual(detected["blocked_rate"],.95)
        near = randomized_crossover_blocks(trials=512,blocks=12,seed=882,
            threshold=1.7,slowdown=1.71,control="fixed-slot",jitter=.05)
        near_result = crossover_summary(near,threshold=1.7,alpha=.05,looks=(12,))
        self.assertLess(near_result["blocked_rate"],.20)

    def test_linear_drift_and_slot_period_interaction_remain_in_the_model(self):
        for control in ("linear-drift","slot-period","general"):
            rows = randomized_crossover_blocks(trials=2048,blocks=12,seed=994,
                threshold=1.7,control=control)
            result = crossover_summary(rows,threshold=1.7,alpha=.05,looks=(12,))
            self.assertLess(result["blocked_rate"],.05,control)

    def test_full_target_prep_and_work_regime_controls_are_distinct(self):
        full = randomized_crossover_blocks(trials=2,blocks=1,seed=7,
            threshold=1.7,slowdown=2.,control="full-only")[0][0]
        candidate = [arm for arm in full["arms"] if arm["source"] == "candidate"]
        self.assertTrue(all(arm["target_seconds"] == 2.*arm["screen_seconds"] for arm in candidate))
        prep = randomized_crossover_blocks(trials=2,blocks=1,seed=7,
            threshold=1.7,slowdown=2.,control="preparation-only")[0][0]
        candidate = [arm for arm in prep["arms"] if arm["source"] == "candidate"]
        self.assertTrue(all(arm["target_seconds"] == arm["screen_seconds"] for arm in candidate))
        self.assertTrue(all(arm["preparation_seconds"] > .1*arm["target_seconds"] for arm in candidate))
        changed_work = randomized_crossover_blocks(trials=1,blocks=1,seed=8,
            threshold=1.7,control="work-regime")[0][0]
        self.assertFalse(changed_work["work_valid"])
        self.assertFalse(crossover_summary([ [changed_work] ],threshold=1.7,
            looks=(1,))["all_work_counters_valid"])

    def test_complete_workflow_crossover_replays_actual_aggregator(self):
        spec={"profile":"ci","suite_alpha":.01,"hypotheses_per_job":8,
            "workflow_jobs":1,"ratio":1.,"distribution":"persistent-slot-crossover",
            "latent_cross_case_correlation":0.,"control":"fixed-slot"}
        result=simulate_crossover_case(spec,trials=1024,seed=99231,parity_workflows=8)
        self.assertEqual(result["actual_aggregator_parity"],"all matched")
        self.assertLess(result["false_block_rate"],.03)
        self.assertEqual(result["statistical_unit"],
            "one fresh randomized four-context block with two complete pairs")

    def test_complete_workflow_detects_large_and_reports_near_threshold_effects(self):
        large={"profile":"ci","suite_alpha":.01,"hypotheses_per_job":8,
            "workflow_jobs":1,"ratio":2.,"effect_scope":"all",
            "distribution":"persistent-slot-crossover","latent_cross_case_correlation":0.,
            "control":"fixed-slot"}
        result=simulate_crossover_case(large,trials=256,seed=99232,parity_workflows=4)
        self.assertGreater(result["detection_rate"],.98)
        near={**large,"ratio":1.7}
        near_result=simulate_crossover_case(near,trials=512,seed=99233,parity_workflows=4)
        self.assertIsNone(near_result["detection_rate"])
        self.assertLess(near_result["false_block_rate"],.03)
        marginal={**large,"ratio":1.71,"arm_jitter":.05}
        marginal_result=simulate_crossover_case(marginal,trials=512,seed=8831,parity_workflows=4)
        self.assertLess(marginal_result["detection_rate"],.05)
        self.assertLess(marginal_result["detection_rate_95_percent_interval"][1],.05)

    def test_source_independent_drift_and_context_interaction_control_workflows(self):
        base={"profile":"ci","suite_alpha":.01,"hypotheses_per_job":8,
            "workflow_jobs":1,"ratio":1.,"distribution":"persistent-slot-crossover",
            "latent_cross_case_correlation":0.}
        for control in ("linear-drift","slot-period","general"):
            result=simulate_crossover_case({**base,"control":control},trials=512,
                seed=8311,parity_workflows=2)
            self.assertLess(result["false_block_rate"],.05,control)

    def test_full_workload_preparation_and_work_counter_negative_controls(self):
        base={"profile":"ci","suite_alpha":.01,"hypotheses_per_job":8,
            "workflow_jobs":1,"ratio":2.,"distribution":"persistent-slot-crossover",
            "latent_cross_case_correlation":0.,"effect_scope":"all"}
        full=simulate_crossover_case({**base,"control":"full-only"},trials=128,
            seed=8412,parity_workflows=4)
        self.assertGreater(full["performance_rejection_rate"],.98)
        preparation=simulate_crossover_case({**base,"control":"preparation-only"},
            trials=512,seed=8413,parity_workflows=4)
        self.assertTrue(preparation["out_of_target_preparation_slowdown"])
        self.assertLess(preparation["performance_rejection_rate"],.03)
        changed=simulate_crossover_case({**base,"ratio":1.,"control":"work-regime"},trials=32,
            seed=8414,parity_workflows=4)
        self.assertEqual(changed["functional_failure_workflows"],32)
        self.assertEqual(changed["performance_rejected_workflows"],0)


if __name__ == "__main__":
    unittest.main()

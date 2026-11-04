from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from triad_perf_guard_controls import binomial_interval, simulate_case


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


if __name__ == "__main__":
    unittest.main()

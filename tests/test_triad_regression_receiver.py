"""Deterministic coverage for the fine receiver regression workload."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_cpu import detect_cpu_budget
from triad_regression_receiver import prepare_fine_receiver


class FineReceiverRegressionTests(unittest.TestCase):
    def test_full_available_logical_worker_allocation_runs_same_stream_work(self):
        budget = detect_cpu_budget()['available_cpus']
        fixture = prepare_fine_receiver()
        try:
            self.assertEqual(fixture['worker_allocation'], budget)
            self.assertEqual(fixture['receiver'].executor._max_workers, budget)
            first = fixture['call']()
            second = fixture['call']()
            for result in (first, second):
                self.assertEqual(result['schema'], 'triad-complete-bit-receiver-v1')
                self.assertEqual(result['status'], 'scored')
                self.assertEqual(result['slot_classification'], 'DATA')
                self.assertEqual(result['decision_bit'], 1)
                self.assertTrue(result['coverage']['coarse_evidence_used_only_for_selection'])
                self.assertTrue(result['coverage']['global_timing_pruning_once'])
                self.assertTrue(result['coverage']['zero_h0_verified'])
                self.assertEqual(result['coverage']['consumed_samples'], 2000)
                self.assertGreater(result['tracking']['work_counts']['candidate_block_updates'], 0)
                self.assertGreater(result['tracking']['native_template_work']['returned_elements'], 0)
            np.testing.assert_allclose(first['bit_scores'], second['bit_scores'], rtol=0, atol=1e-12)
            self.assertEqual(fixture['geometry']['worker_allocation'], budget)
            self.assertIn('topology', fixture['geometry']['cpu_budget'])
            self.assertIn('geometry_sha256', fixture['geometry'])
        finally:
            fixture['close']()
        self.assertIsNone(fixture['receiver'].executor)

    def test_explicit_worker_override_is_bounded_by_visible_cpu_budget(self):
        budget = detect_cpu_budget()['available_cpus']
        with self.assertRaises(ValueError):
            prepare_fine_receiver(workers=budget+1)
        with self.assertRaises(ValueError):
            prepare_fine_receiver(workers=0)


if __name__ == '__main__':
    unittest.main()

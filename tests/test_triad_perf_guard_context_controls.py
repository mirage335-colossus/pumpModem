import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from triad_perf_guard_context_controls import build_block
from triad_perf_guard_policy import (BlockObservation, GuardPolicy, PairObservation,
    build_guard_plan, confirm_case, screen_case)


def observation(run_id, order, offset):
    ids = {side: f"context-{offset}-{side}" for side in ("reference", "candidate")}
    return {"run_id": run_id, "reference_seconds": 10.0, "candidate_seconds": 10.0,
        "details": {"order": list(order), "confirmation_context": {
            "protocol": "fresh-source-context-pair-v1", "creation_order": list(order),
            "context_ids": ids, "target_boundary_preserved": True,
            "workers": {side: {"pid": offset*2+i+100} for i, side in enumerate(("reference", "candidate"))}}}}


class FreshContextControls(unittest.TestCase):
    def test_fresh_slot_crossover_cancels_fixed_19_to_1_slot_bias(self):
        first, second = observation("period-a", ("reference", "candidate"), 1), observation(
            "period-b", ("candidate", "reference"), 2)
        block, row = build_block("fft:toy", first, second, mode="crossover")
        self.assertEqual(block.reference_seconds, block.candidate_seconds)
        self.assertAlmostEqual(row["ratio"], 1.0)
        self.assertEqual(len(set(v for pair in block.contexts for v in pair)), 4)
        self.assertEqual(row["pairs"][0]["details"]["slot_delay_control"]["slot_by_source"],
                         {"reference": "slow", "candidate": "fast"})
        self.assertEqual(row["pairs"][1]["details"]["slot_delay_control"]["slot_by_source"],
                         {"candidate": "slow", "reference": "fast"})

    def test_source_persistent_context_control_replays_bias_on_same_targets(self):
        first, second = observation("period-a", ("reference", "candidate"), 1), observation(
            "period-b", ("candidate", "reference"), 2)
        block, row = build_block("fft:toy", first, second, mode="persistent",
            permanent_source_slot={"reference": "fast", "candidate": "slow"})
        self.assertEqual(row["pairs"][0]["reference_seconds"], 10.010)
        self.assertEqual(row["pairs"][0]["candidate_seconds"], 10.190)
        self.assertEqual(row["pairs"][1]["reference_seconds"], 10.010)
        self.assertEqual(row["pairs"][1]["candidate_seconds"], 10.190)
        self.assertGreater(block.candidate_seconds / block.reference_seconds, 1.0)

    def test_invalid_or_reused_contexts_do_not_form_a_block(self):
        first, second = observation("period-a", ("reference", "candidate"), 1), observation(
            "period-b", ("candidate", "reference"), 2)
        second["details"]["confirmation_context"]["context_ids"]["reference"] = "context-1-reference"
        with self.assertRaises(ValueError):
            build_block("fft:toy", first, second, mode="crossover")

    def test_real_aggregator_counts_only_complete_balanced_blocks(self):
        plan = build_guard_plan(["fft:toy"], GuardPolicy("ci", workflow_job_count=33))
        screen = screen_case(plan, "fft:toy", PairObservation("screen", 10, 10), force_confirmation=True)
        blocks = []
        for index in range(12):
            first = observation(f"period-{index}-a", ("reference", "candidate"), 10+index*4)
            second = observation(f"period-{index}-b", ("candidate", "reference"), 11+index*4)
            block, _ = build_block("fft:toy", first, second, mode="crossover")
            blocks.append(block)
        receipt = confirm_case(plan, "fft:toy", blocks, screen_run_ids=[screen["observation"]["run_id"]])
        self.assertEqual(receipt["experimental_unit"], "balanced four-arm block")
        self.assertEqual(receipt["block_count"], 12)
        self.assertEqual(receipt["positive_block_count"], 0)
        self.assertFalse(receipt["confirmed_regression"])


if __name__ == "__main__":
    unittest.main()

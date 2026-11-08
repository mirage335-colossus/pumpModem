#!/usr/bin/env python3
"""Cheap count and bound checks for triad tracking."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_tracking_budget import count_tracking_work


def plan(**changes):
    args = dict(samples=33, sample_rate_hz=16, block_samples=6,
        segment_samples=16, initial_states=3, maximum_states=7,
        maximum_proposals=11, rate_index_bound=2, models=2, taps=2,
        precision="fp64", projection_mode="valid-rows", workspace_bytes=123456,
        fine_symbol_rate_hz=.01, data_if_hz=1725., maximum_relative_rate_ppm=200.)
    args.update(changes)
    return count_tracking_work(**args)


class TrackingBudgetTests(unittest.TestCase):
    def test_exact_segment_and_terminal_block_calendar(self):
        result = plan()
        self.assertEqual(result["receiver"]["segments"], 3)
        # [16,16,1] rows are independently segmented: [6,10], [6,10], [1].
        self.assertEqual(result["receiver"]["likelihood_blocks"], 5)
        self.assertEqual(result["work"]["basis_row_samples"], 2*2*16*3 + 2*2*17*11)
        self.assertEqual(result["work"]["gaussian_updates"], 3*2*(2*3 + 3*11))
        self.assertEqual(result["work"]["transition_edges"], 4*5*7*2)
        self.assertEqual(result["workspace"]["receiver_estimate_bytes"], 123456)

    def test_short_observation_has_one_block_and_no_transitions(self):
        result = plan(samples=3, segment_samples=100, block_samples=64)
        self.assertEqual(result["receiver"]["segments"], 1)
        self.assertEqual(result["receiver"]["likelihood_blocks"], 1)
        self.assertEqual(result["work"]["transition_edges"], 0)
        self.assertEqual(result["work"]["basis_row_samples"], 2*2*3*3)

    def test_calendar_count_is_constant_memory_and_proposal_cap_covers_postpredict_peak(self):
        huge = plan(samples=10**12, segment_samples=16, block_samples=6)
        count = 10**12//16
        self.assertEqual(huge["receiver"]["segments"], count)
        self.assertEqual(huge["receiver"]["likelihood_blocks"], count*2)
        self.assertEqual(huge["work"]["transition_edges"], 4*5*7*(count-1))
        self.assertEqual(huge["work"]["gaussian_updates"],
                         3*2*(2*3+((count*2)-2)*11))
        self.assertEqual(huge["receiver"]["maximum_active_states"], 11)

    def test_if_and_rate_bounds_expose_phase_and_symbol_motion(self):
        result = plan()["timing_uncertainty"]
        self.assertAlmostEqual(result["maximum_audio_if_shift_hz"], .345)
        self.assertAlmostEqual(result["maximum_fine_symbol_rate_shift_hz"], 2e-6)
        self.assertAlmostEqual(result["maximum_envelope_timing_drift_seconds"],
                               33/16*200e-6)




    def test_invalid_bounds_are_rejected(self):
        for changes in (dict(samples=0), dict(samples=True), dict(block_samples=0),
                        dict(segment_samples=0), dict(maximum_states=12, maximum_proposals=11),
                        dict(initial_states=12, maximum_proposals=11), dict(taps=17),
                        dict(rate_index_bound=2049), dict(precision="tf32"),
                        dict(projection_mode="approximate"), dict(workspace_bytes=0),
                        dict(data_if_hz=float("nan")),
                        dict(maximum_relative_rate_ppm=1_000_000)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                plan(**changes)
        with self.assertRaisesRegex(ValueError, "duration is not representable"):
            plan(samples=(1 << 64)-1, sample_rate_hz=1e-300,
                 segment_samples=(1 << 64)-1, block_samples=(1 << 64)-1)


if __name__ == "__main__":
    unittest.main()

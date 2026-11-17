"""Pure cost and coverage gates for native manual measurements."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_case_capacity import (INTERVAL, RATIO, _compatible_work, _continued_component, _stream_plan, _stream_ratios,
                                 combine_components, direct_fine, fine_continuation, slot_costs, worker_plan)
from triad_case_capacity import varying_scope_gap
from triad_native import ProbeError


def continuation_receipt():
    """A fixed successful native-protocol example. It contains no waveform."""
    def work(seconds, fills):
        lane = {"states": 8, "last_emission_states": 8, "state_cap": 64, "states_sha256": "a" * 64,
                "proposal_cap": 256, "projected_template_energy": 2 * seconds}
        return {"input_sha256": "f" * 64, "observation_first_sample": 100_000,
                "observation_rows": 9000, "public_slot": 6, "pattern_epoch": 77,
                "symbol_ordinal": 27, "workers": 24, "sample_rate_hz": 1500,
                "block_rows": 375, "segment_rows": 750, "tap_count": 2,
                "delay_step_seconds": .0001, "rate_step_ppm": .001, "rate_radius": 2,
                "checkpoint_seconds": seconds, "processed_rows": seconds * 1500,
                "representative_work": True, "initial_affine": lane.copy(),
                "adaptive_diagnostics": False, "adaptive": None,
                "cache": {"tile_samples": 1024, "cache_limit_bytes": 2**20, "dense_factor": 1,
                          "dense_sample_rate_hz": 1500, "fallback_reason": None,
                          "cache_misses": fills, "native_preparation_calls": fills,
                          "native_preparation_samples": fills * 1024}}

    def phase(name, wall, seconds, work):
        return {"name": name, "wall_seconds": wall, "represented_seconds": seconds, "work": work}

    return {"status": "complete", "configuration": {
                "clock_bound_ppm": 1, "sample_rate_hz": 1500, "public_window_samples": 12_000},
            "result": {"work_complete": True, "checkpoints_complete": True,
                       "continued_cache_fill": True, "receiver_decision_eligible": False,
                       "score": {"adaptive_diagnostics": False, "adaptive_bit_evidence": None,
                                 "combined_bit_evidence": None, "adaptive_blocks": 0,
                                 "adaptive_peak_states": 0, "adaptive_transition_edges": 0}},
            "phases": [
                phase("input_validation", .01, 8, {}),
                phase("helper", .02, 8, {}),
                phase("coarse_prepare", .03, 8, {}),
                phase("coarse_tiles", .02, 8, {"step_tiles": 1, "completed_tiles": 1, "planned_tiles": 3}),
                phase("coarse_tiles", .04, 8, {"step_tiles": 2, "completed_tiles": 3, "planned_tiles": 3}),
                phase("coarse_selection", .01, 8, {"planned_tiles": 3, "completed_tiles": 3}),
                phase("audio_frontend", .02, 8, {"input_rows": 12_000, "output_rows": 12_000}),
                phase("fine_prefix", .2, 2, work(2, 2)),
                phase("fine_continuation", .1, 1, work(3, 3)),
                phase("fine_continuation", .1, 1, work(4, 4)),
                phase("fine_continuation", .2, 2, work(6, 6)),
                phase("fine_finish", .05, 0, work(6, 6) | {"finished": True, "observation_complete": True}),
            ]}


class NativeWorkTests(unittest.TestCase):
    def test_fixed_affine_component_requires_explicit_varying_work_scope(self):
        self.assertIsNone(varying_scope_gap({"fine_component_omits_varying_operational_work": False}))
        self.assertIn("Configured varying", varying_scope_gap(
            {"fine_component_omits_varying_operational_work": True}))
        self.assertIn("lacks an explicit", varying_scope_gap({}))

    def test_same_session_continuation_charges_prefix_and_finish_once(self):
        row = fine_continuation(continuation_receipt())
        self.assertEqual(row["continuation_rates"], [.1, .1, .1])
        summary = _continued_component([row] * 3, window_seconds=8, cadence=20, family_size=1)
        self.assertAlmostEqual(summary[RATIO], (.2 + .05 + .1 * 6) / 20)
        self.assertEqual(summary["realtime_status"], "pass")

    def test_fixed_affine_state_counts_remain_exact_across_continuation(self):
        def receipt_with_counts(retained, emitted):
            receipt = continuation_receipt()
            for phase in receipt["phases"][-5:]:
                phase["work"]["initial_affine"].update(states=retained, last_emission_states=emitted)
            return receipt

        stable = fine_continuation(receipt_with_counts(64, 64))
        self.assertTrue(_compatible_work(stable, fine_continuation(receipt_with_counts(64, 64))))
        for key, retained, emitted in (("states", 48, 64), ("last_emission_states", 64, 80)):
            with self.subTest(counter=key):
                drift = receipt_with_counts(64, 64)
                drift["phases"][-3]["work"]["initial_affine"][key] = (
                    retained if key == "states" else emitted)
                with self.assertRaisesRegex(ProbeError, "initial_affine state geometry drifted"):
                    fine_continuation(drift)
        invalid_affine = receipt_with_counts(64, 64)
        for phase in invalid_affine["phases"][-5:]:
            phase["work"]["initial_affine"]["last_emission_states"] = 16
        with self.assertRaisesRegex(ProbeError, "initial_affine state geometry drifted"):
            fine_continuation(invalid_affine)

    def test_changed_state_geometry_checkpoint_or_cache_cannot_support_projection(self):
        for change in ("state", "fixed_state_identity", "finish_state_identity", "address", "cache", "input", "cadence"):
            receipt = continuation_receipt()
            phase = next(row for row in receipt["phases"] if row["name"] == "fine_continuation")
            work = phase["work"]
            if change == "state":
                work["initial_affine"]["last_emission_states"] = 20
            elif change == "fixed_state_identity":
                work["initial_affine"]["states_sha256"] = "b" * 64
            elif change == "finish_state_identity":
                receipt["phases"][-1]["work"]["initial_affine"]["states_sha256"] = "b" * 64
            elif change == "address":
                work["pattern_epoch"] += 1
            elif change == "cache":
                work["cache"]["tile_samples"] = 4096
            elif change == "input":
                work["input_sha256"] = "1" * 64
            else:
                phase["represented_seconds"] = 2
            with self.subTest(change=change), self.assertRaises(ProbeError):
                fine_continuation(receipt)

    def test_capacity_continuation_requires_explicit_fixed_only_mode(self):
        mutations = (
            lambda receipt: receipt["phases"][-1]["work"].pop("adaptive_diagnostics"),
            lambda receipt: receipt["phases"][-1]["work"].pop("adaptive"),
            lambda receipt: receipt["phases"][-1]["work"].update(adaptive_diagnostics=True),
            lambda receipt: receipt["phases"][-1]["work"].update(adaptive={}),
            lambda receipt: receipt["result"]["score"].pop("combined_bit_evidence"),
            lambda receipt: receipt["result"]["score"].update(adaptive_diagnostics=True),
            lambda receipt: receipt["result"]["score"].update(adaptive_transition_edges=1),
        )
        for mutate in mutations:
            receipt = continuation_receipt()
            mutate(receipt)
            with self.subTest(mutation=mutate), self.assertRaises(ProbeError):
                fine_continuation(receipt)

    def test_cold_maximum_requires_two_segments_of_fixed_256_state_work(self):
        def receipt():
            fine_work = {"segments": 2, "initial_affine": {"initial_states": 256,
                          "peak_emission_states": 256}, "adaptive_diagnostics": False, "adaptive": None}
            finish = fine_work | {"finished": True, "observation_complete": True}
            return {"status": "complete", "result": {"maximum_fixed_affine_states_covered": True,
                    "explicit_erasure_covered": False, "score": {"adaptive_diagnostics": False,
                    "adaptive_bit_evidence": None, "combined_bit_evidence": None, "adaptive_blocks": 0,
                    "adaptive_peak_states": 0, "adaptive_transition_edges": 0}},
                    "phases": [{"name": "fine_work", "wall_seconds": .2, "work": fine_work},
                               {"name": "fine_finish", "wall_seconds": .05, "work": finish}]}
        current = receipt()
        self.assertAlmostEqual(direct_fine(current, "cold-maximum")[0], .25)
        for mutation in (lambda row: row["phases"][0]["work"].update(segments=1),
                         lambda row: row["phases"][0]["work"]["initial_affine"].update(peak_emission_states=255),
                         lambda row: row["result"].pop("maximum_fixed_affine_states_covered")):
            damaged = receipt()
            mutation(damaged)
            with self.subTest(mutation=mutation), self.assertRaises(ProbeError):
                direct_fine(damaged, "cold-maximum")

    def test_coarse_cost_counts_every_completed_tile_once(self):
        original = continuation_receipt()
        self.assertAlmostEqual(slot_costs(original)["coarse_tiles"], .06)
        partial = copy.deepcopy(original)
        partial["phases"].pop(4)
        with self.assertRaises(ProbeError):
            slot_costs(partial)

    def test_incomplete_work_cannot_pass_and_exact_numeric_limit_fails(self):
        measured = {RATIO: .25, INTERVAL: [.2, .3], "model_status": "measured_fixed_geometry"}
        missing = {RATIO: None, INTERVAL: None, "model_status": "unresolved"}
        row = combine_components({"receiver": measured, "unmeasured": missing}, complete_work_covered=False)
        self.assertIsNone(row[RATIO])
        self.assertEqual(row["realtime_status"], "unresolved")
        at_limit = combine_components({"one": measured | {RATIO: 1, INTERVAL: [.7, 1.3]}},
                                      complete_work_covered=False)
        self.assertEqual(at_limit["realtime_status"], "fail")

    def test_native_hardware_worker_budget_and_exact_overcommit(self):
        automatic = worker_plan(12, 6)
        self.assertEqual(automatic["pilots"], [1, 6, 12])
        self.assertEqual(automatic["inference_workers"], 24)
        self.assertEqual(worker_plan(12, 6, threads=27)["inference_workers"], 27)
        self.assertEqual(worker_plan(12, 6, full_logical_inference=True)["inference_workers"], 12)
        self.assertIsNone(worker_plan(12, 6, full_logical_inference=False)["inference_workers"])
        self.assertEqual(worker_plan(2, 6)["pilots"], [1, 2])

    def test_stream_preflight_uses_smallest_declared_horizon_and_budget(self):
        baseline = {"minimum_backlog_slots": 10, "requested_slots": 10, "fits_sample_budget": True,
                    "fits_spool_budget": True, "maximum_startup_carryover_slots": 4}
        plan = {"configuration": {"stream_plan": baseline}}
        calls = []

        def probe(command, *, workers, slots):
            calls.append((command, workers, slots))
            return {"status": "complete", "configuration": {"stream_plan": baseline | {
                "requested_slots": slots, "fits_spool_budget": allowed}}}

        for allowed, expected in ((True, 14), (False, 10)):
            records = []
            chosen = _stream_plan(probe, plan, {"backlog_slots": None}, 24, records)
            self.assertEqual(chosen["requested_slots"], expected)
            self.assertEqual(len(records), 1)
        self.assertEqual(calls, [("plan", 24, 14), ("plan", 24, 14)])

    def test_scored_failed_message_still_provides_native_stream_timing(self):
        receipt = {"status": "incomplete", "observation": {"input_sha256": "a" * 64}, "result": {
            "message_and_physical_quiet_verified": False, "configured_calendar_reacquired": False,
            "stream": {"state": "input_ended", "work_complete": True, "committed_slots": 10,
                       "verified_input_sha256": "a" * 64,
                       "unique_baseband_seconds": 25, "slots": [{"complete": True}] * 10},
            "backlog_interval": {"complete": True, "wall_seconds": 1.25, "unique_baseband_seconds": 5}},
            "phases": [{"name": "receiver_service", "wall_seconds": 5, "represented_seconds": 25}]}
        self.assertEqual(_stream_ratios(receipt, 10), (.2, .25))
        with self.assertRaises(ProbeError):
            _stream_ratios(receipt, 11)


if __name__ == "__main__":
    unittest.main()

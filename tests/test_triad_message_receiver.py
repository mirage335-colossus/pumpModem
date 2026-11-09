"""The Robust message boundary requires scored DATA/QUIET, never transport EOF."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_message_receiver import QuietSlotMessageReceiver

_next_sample = 0


def slot(kind, bit=0, *, complete=True, valid=True, zero_h0=True,
         noise_stats=True, quiet_consistent=True, energy=1.0, threshold=2.0,
         tail=.001, status="scored", ratio=.1, represented=10.0,
         omitted_stage=None, first=None, sample_rate=10):
    global _next_sample
    if first is None:
        first = _next_sample
    _next_sample = max(_next_sample, first + 100)
    stages = {name: {"included_in_service": True,
                     "ratio_processing_seconds_per_baseband_second": .01,
                     "represented_baseband_seconds": represented}
              for name in ("helper", "coarse", "refinement_selection",
                           "fine_preparation", "tracking", "decision",
                           "recurring_bank_preparation")}
    if omitted_stage is not None:
        stages.pop(omitted_stage)
    return {
        "status": status,
        "slot_classification": kind,
        "decision_bit": bit if kind == "DATA" else None,
        "operational": {"passes_conditional_bound": kind == "DATA"},
        "quiet": {
            "noise_consistent": quiet_consistent,
            "projected_energy": energy,
            "threshold": threshold,
            "observed_projection_modes": 20,
            "noise_upper_tail_probability": tail,
        },
        "coverage": {
            "complete_observation": complete,
            "all_input_valid": valid,
            "zero_h0_verified": zero_h0,
            "valid_noise_statistics": noise_stats,
            "consumed_samples": 100,
            "expected_samples": 100,
            "physical_first_sample": first,
            "physical_stop_sample": first + 100,
        },
        "sample_rate_hz": sample_rate,
        "ratio_processing_seconds_per_baseband_second": ratio,
        "represented_baseband_seconds": represented,
        "stages": stages,
    }


class QuietSlotMessageReceiverTests(unittest.TestCase):
    def setUp(self):
        global _next_sample
        _next_sample = 0
        self.calls = []

        def decoder(bits):
            self.calls.append(bits)
            return {"decoded_bytes": 3, "filtered_text": "fox"}

        self.receiver = QuietSlotMessageReceiver(decoder)

    def test_noise_only_quiet_slots_never_open_or_complete_a_message(self):
        first = self.receiver.consume_slot(0, slot("QUIET"))
        second = self.receiver.consume_slot(1, slot("QUIET"))
        self.assertEqual(first["status"], "listening")
        self.assertEqual(second["status"], "listening")
        self.assertFalse(second["physical_complete"])
        self.assertFalse(second["message_ready_for_decode"])
        self.assertEqual(second["data_bit_count"], 0)
        self.assertEqual(self.calls, [])

    def test_data_then_two_fully_scored_quiet_slots_decodes_the_exact_bit_sequence(self):
        self.receiver.consume_slot(0, slot("DATA", 1))
        pending = self.receiver.consume_slot(1, slot("QUIET"))
        self.assertEqual(pending["status"], "awaiting_end_or_resume")
        self.assertFalse(pending["physical_complete"])
        done = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(done["status"], "complete")
        self.assertTrue(done["physical_complete"])
        self.assertTrue(done["message_ready_for_decode"])
        self.assertEqual(done["decoded_content"], {
            "status": "decoded", "decoded_bytes": 3, "filtered_text": "fox"})
        self.assertEqual(self.calls, [(1,)])
        self.assertEqual(done["trailing_quiet_slots"], [1, 2])

    def test_resumed_data_turns_the_pending_quiet_slot_into_an_erasure(self):
        self.receiver.consume_slot(0, slot("DATA", 0))
        self.receiver.consume_slot(1, slot("QUIET"))
        resumed = self.receiver.consume_slot(2, slot("DATA", 1))
        self.assertEqual(resumed["status"], "receiving")
        self.assertEqual(resumed["erasure_slot_indices"], [1])
        self.assertTrue(resumed["reacquisition_required"])
        self.assertEqual(resumed["reacquisition_count"], 1)
        self.receiver.consume_slot(3, slot("QUIET"))
        final = self.receiver.consume_slot(4, slot("QUIET"))
        self.assertEqual(final["status"], "incomplete_message")
        self.assertTrue(final["physical_complete"])
        self.assertFalse(final["message_ready_for_decode"])
        self.assertEqual(final["decoded_content"]["reason"], "message_contains_erasures")
        self.assertEqual(self.calls, [])

    def test_ambiguous_slots_do_not_advance_quiet_end_and_resumed_data_is_erased(self):
        self.receiver.consume_slot(0, slot("DATA", 0))
        self.receiver.consume_slot(1, slot("QUIET"))
        ambiguous = self.receiver.consume_slot(2, slot("AMBIGUOUS", status="unresolved"))
        self.assertEqual(ambiguous["status"], "ambiguous")
        self.assertFalse(ambiguous["physical_complete"])
        resumed = self.receiver.consume_slot(3, slot("DATA", 1))
        self.assertTrue(resumed["reacquisition_required"])
        self.assertEqual(resumed["erasure_slot_indices"], [1, 2])
        self.assertEqual(self.calls, [])

    def test_noise_consistency_must_be_proved_for_each_quiet_slot(self):
        self.receiver.consume_slot(0, slot("DATA", 0))
        invalid_quiet_results = [
            slot("QUIET", quiet_consistent=False),
            slot("QUIET", energy=3.0, threshold=2.0),
            slot("QUIET", tail=.002),
            slot("QUIET", noise_stats=False),
            slot("QUIET", zero_h0=False),
            slot("QUIET", complete=False),
            slot("QUIET", valid=False),
        ]
        for index, row in enumerate(invalid_quiet_results, start=1):
            with self.subTest(index=index):
                result = self.receiver.consume_slot(index, row)
                self.assertEqual(result["status"], "ambiguous")
                self.assertFalse(result["physical_complete"])
        # Timing receipts are intentionally not signal evidence; the tests
        # above cover only incomplete or invalid signal observations.

    def test_work_limit_missing_h0_and_transport_end_never_count_as_quiet(self):
        self.receiver.consume_slot(0, slot("DATA", 1))
        for index, bad in enumerate((
                slot("QUIET", status="unresolved"),
                slot("QUIET", zero_h0=False),
                slot("DATA", zero_h0=False),
                slot("AMBIGUOUS", status="unresolved")), start=1):
            self.receiver.consume_slot(index, bad)
        result = self.receiver.finish_input()
        self.assertEqual(result["status"], "incomplete")
        self.assertFalse(result["physical_complete"])
        self.assertIn("transport_ended", result["reason"])
        self.assertEqual(self.calls, [])

    def test_cancel_always_reports_incomplete_until_scored_physical_end(self):
        self.receiver.consume_slot(0, slot("DATA"))
        self.receiver.consume_slot(1, slot("QUIET"))
        result = self.receiver.cancel()
        self.assertEqual(result["status"], "incomplete")
        self.assertFalse(result["physical_complete"])
        self.assertEqual(self.calls, [])

    def test_local_data_and_slot_quotas_fail_without_completing(self):
        limited = QuietSlotMessageReceiver(lambda bits: {"decoded_bytes": 0, "filtered_text": ""},
                                           maximum_data_bits=1, maximum_slots=2)
        limited.consume_slot(0, slot("DATA", 0))
        result = limited.consume_slot(1, slot("DATA", 1))
        self.assertEqual(result["status"], "resource_limit")
        self.assertFalse(result["physical_complete"])
        self.assertEqual(result["received_bits"], [0])
        with self.assertRaises(RuntimeError):
            limited.consume_slot(2, slot("QUIET"))

        slots = QuietSlotMessageReceiver(lambda bits: {"decoded_bytes": 0, "filtered_text": ""},
                                         quiet_slots=1, maximum_slots=1)
        slots.consume_slot(0, slot("DATA"))
        result = slots.consume_slot(1, slot("QUIET"))
        self.assertEqual(result["status"], "resource_limit")
        self.assertFalse(result["physical_complete"])

    def test_bad_slot_indices_stop_without_silently_skipping_missing_samples(self):
        self.receiver.consume_slot(0, slot("DATA"))
        result = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["reason"], "slot_sequence_discontinuity")
        self.assertFalse(result["physical_complete"])
        self.assertEqual(self.calls, [])

    def test_bad_data_decisions_are_downgraded_to_ambiguous(self):
        self.receiver.consume_slot(0, slot("DATA", True))  # bool is not a source bit.
        self.assertEqual(self.receiver.result()["status"], "listening")
        self.receiver.consume_slot(1, slot("DATA", 2))
        self.assertEqual(self.receiver.result()["status"], "listening")
        self.receiver.consume_slot(2, slot("DATA", 1, status="partial"))
        self.assertEqual(self.receiver.result()["data_bit_count"], 0)

    def test_data_admission_does_not_depend_on_quiet_noise_statistics(self):
        result = self.receiver.consume_slot(0, slot("DATA", 1, noise_stats=False))
        self.assertEqual(result["status"], "receiving")
        self.assertEqual(result["received_bits"], [1])

    def test_invalid_quiet_geometry_and_decision_receipts_do_not_raise(self):
        self.receiver.consume_slot(0, slot("DATA"))
        malformed = [
            None,
            {},
            slot("QUIET", tail=float("nan")),
            slot("QUIET", energy=-1.0),
            slot("QUIET", threshold=float("inf")),
        ]
        for index, result in enumerate(malformed, start=1):
            with self.subTest(index=index):
                row = self.receiver.consume_slot(index, result)
                self.assertEqual(row["status"], "ambiguous")
                self.assertFalse(row["physical_complete"])

    def test_decoder_output_is_bounded_and_filtered(self):
        self.receiver.consume_slot(0, slot("DATA"))
        self.receiver.consume_slot(1, slot("QUIET"))
        self.receiver._decoder = lambda bits: {
            "decoded_bytes": 1, "filtered_text": "\u00e9"}
        invalid = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(invalid["status"], "invalid_message")
        self.assertTrue(invalid["physical_complete"])
        self.assertEqual(invalid["decoded_content"]["reason"], "invalid_native_decoder_result")

    def test_canonical_codec_rejection_and_filter_callback_errors_stay_bounded(self):
        receiver = QuietSlotMessageReceiver(lambda bits: (_ for _ in ()).throw(
            ValueError("untrusted detail must not be returned")))
        receiver.consume_slot(0, slot("DATA", 1))
        receiver.consume_slot(1, slot("QUIET"))
        invalid = receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(invalid["status"], "invalid_message")
        self.assertEqual(invalid["decoded_content"]["reason"], "short_codec_rejected_message")

    def test_terminal_results_cannot_accept_later_slots_or_turn_eof_into_end(self):
        self.receiver.consume_slot(0, slot("DATA", 0))
        self.receiver.consume_slot(1, slot("QUIET"))
        final = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(self.receiver.finish_input(), final)
        self.assertEqual(self.receiver.cancel(), final)
        with self.assertRaises(RuntimeError):
            self.receiver.consume_slot(3, slot("QUIET"))

    def test_message_level_service_ratio_uses_serial_same_stream_weighting(self):
        with patch("triad_message_receiver.time.perf_counter", return_value=1.0):
            self.receiver.consume_slot(0, slot("DATA", 0, ratio=.1, represented=10.0))
            self.receiver.consume_slot(1, slot("QUIET", ratio=.2, represented=10.0))
            result = self.receiver.consume_slot(2, slot("QUIET", ratio=.3, represented=10.0))
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], .2)
        self.assertEqual(result["represented_baseband_seconds"], 30.)
        self.assertEqual(result["stage_call_counts"], {
            "helper": 3, "coarse": 3, "refinement_selection": 3,
            "fine_preparation": 3, "tracking": 3, "decision": 3,
            "recurring_bank_preparation": 3, "message_completion": 1, "slot_state_machine": 3})

    def test_overlapping_physical_windows_use_union_as_capacity_denominator(self):
        receiver = QuietSlotMessageReceiver(lambda bits: {
            "decoded_bytes": 3, "filtered_text": "fox"})
        with patch("triad_message_receiver.time.perf_counter", return_value=1.0):
            receiver.consume_slot(0, slot("DATA", 1, first=0, sample_rate=100,
                                          represented=1., ratio=.1))
            receiver.consume_slot(1, slot("QUIET", first=80, sample_rate=100,
                                          represented=1., ratio=.2))
            result = receiver.consume_slot(2, slot("QUIET", first=160, sample_rate=100,
                                                   represented=1., ratio=.3))
        self.assertEqual(result["represented_baseband_seconds"], 2.6)
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"],
                               .6 / 2.6)

    def test_missing_capacity_cost_keeps_signal_decision_but_suppresses_ratio(self):
        self.receiver.consume_slot(0, slot("DATA", 1, omitted_stage="recurring_bank_preparation"))
        self.receiver.consume_slot(1, slot("QUIET"))
        result = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(result["status"], "complete")
        self.assertFalse(result["complete_capacity_receipts"])
        self.assertIsNone(result["ratio_processing_seconds_per_baseband_second"])
        self.assertEqual(self.calls, [(1,)])

    def test_invalid_sample_geometry_cannot_advance_physical_completion(self):
        self.receiver.consume_slot(0, slot("DATA", 1, first=0))
        self.receiver.consume_slot(1, slot("QUIET", first=100, sample_rate=11))
        result = self.receiver.consume_slot(2, slot("QUIET", first=200))
        self.assertEqual(result["status"], "awaiting_end_or_resume")
        self.assertFalse(result["complete_capacity_receipts"])
        self.assertIsNone(result["ratio_processing_seconds_per_baseband_second"])
        self.assertEqual(self.calls, [])

    def test_sample_gaps_are_not_counted_in_physical_union(self):
        receiver = QuietSlotMessageReceiver(lambda bits: {
            "decoded_bytes": 3, "filtered_text": "fox"})
        with patch("triad_message_receiver.time.perf_counter", return_value=1.0):
            receiver.consume_slot(0, slot("DATA", 1, first=0, sample_rate=100,
                                          represented=1., ratio=.1))
            receiver.consume_slot(1, slot("QUIET", first=150, sample_rate=100,
                                          represented=1., ratio=.1))
            result = receiver.consume_slot(2, slot("QUIET", first=300, sample_rate=100,
                                                   represented=1., ratio=.1))
        self.assertEqual(result["represented_baseband_seconds"], 3.)
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], .1)

    def test_missing_physical_metadata_cannot_advance_physical_completion(self):
        self.receiver.consume_slot(0, slot("DATA", 1))
        missing = slot("QUIET")
        missing["coverage"].pop("physical_first_sample")
        missing["coverage"].pop("physical_stop_sample")
        self.receiver.consume_slot(1, missing)
        result = self.receiver.consume_slot(2, slot("QUIET"))
        self.assertEqual(result["status"], "awaiting_end_or_resume")
        self.assertFalse(result["complete_capacity_receipts"])
        self.assertIsNone(result["ratio_processing_seconds_per_baseband_second"])
        self.assertEqual(self.calls, [])

    def test_nonmonotonic_interval_cannot_advance_physical_completion(self):
        self.receiver.consume_slot(0, slot("DATA", 1, first=100))
        self.receiver.consume_slot(1, slot("QUIET", first=50))
        result = self.receiver.consume_slot(2, slot("QUIET", first=200))
        self.assertEqual(result["status"], "awaiting_end_or_resume")
        self.assertFalse(result["complete_capacity_receipts"])
        self.assertIsNone(result["ratio_processing_seconds_per_baseband_second"])

    def test_pending_ambiguous_slot_before_quiet_suffix_blocks_decode(self):
        self.receiver.consume_slot(0, slot("DATA", 1))
        self.receiver.consume_slot(1, slot("AMBIGUOUS", status="unresolved"))
        self.receiver.consume_slot(2, slot("QUIET"))
        result = self.receiver.consume_slot(3, slot("QUIET"))
        self.assertEqual(result["status"], "incomplete_message")
        self.assertEqual(result["erasure_slot_indices"], [1])
        self.assertEqual(result["trailing_quiet_slots"], [2, 3])
        self.assertEqual(self.calls, [])

    def test_callback_runtime_is_included_once_in_message_service(self):
        ticks = iter((0., 0., 0., 0., 10., 10., 10.25, 10.25))
        with patch("triad_message_receiver.time.perf_counter", side_effect=lambda: next(ticks)):
            self.receiver.consume_slot(0, slot("DATA", 1, ratio=.1))
            self.receiver.consume_slot(1, slot("QUIET", ratio=.2))
            result = self.receiver.consume_slot(2, slot("QUIET", ratio=.3))
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"],
                               (6.0 + .25) / 30.0)

    def test_leading_ambiguous_slot_cannot_be_dropped_from_message_prefix(self):
        self.receiver.consume_slot(0, slot("AMBIGUOUS"))
        self.receiver.consume_slot(1, slot("DATA", 1))
        self.receiver.consume_slot(2, slot("QUIET"))
        result = self.receiver.consume_slot(3, slot("QUIET"))
        self.assertEqual(result["status"], "incomplete_message")
        self.assertEqual(result["erasure_slot_indices"], [0])
        self.assertEqual(self.calls, [])

    def test_state_machine_elapsed_is_included_once(self):
        with patch("triad_message_receiver.time.perf_counter", side_effect=(1., 1.25)):
            result = self.receiver.consume_slot(0, slot("DATA", 1, ratio=.1))
        self.assertAlmostEqual(result["ratio_processing_seconds_per_baseband_second"], .125)


if __name__ == "__main__":
    unittest.main()

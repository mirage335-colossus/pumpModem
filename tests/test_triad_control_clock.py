"""Deterministic tests for the isolated triad UTC/control-clock association."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_control_clock import (ClockReading, ClockUnavailable, ControlClock,
                                 DEFAULT_MAX_STEP_NS, DEFAULT_POLL_INTERVAL_NS)


NS = 1_000_000_000


class FakeClock:
    def __init__(self, reference=10 * NS, utc=1_800_000_000 * NS, width=10_000):
        self.reference = reference
        self.utc = utc
        self.width = width
        self.reads = 0
        self.bad_read = False
        self.reference_skew = 0
        self.next_width = None
        self.next_reading = None

    def read(self):
        self.reads += 1
        if self.bad_read:
            raise OSError("injected clock read failure")
        if self.next_reading is not None:
            reading, self.next_reading = self.next_reading, None
            return reading
        width = self.width if self.next_width is None else self.next_width
        self.next_width = None
        midpoint = self.reference + self.reference_skew
        return ClockReading(self.utc, midpoint - width // 2,
                            midpoint + (width - width // 2))

    def move(self, seconds=1, utc_seconds=None, **kw):
        self.reference += seconds * NS
        self.utc += (seconds if utc_seconds is None else utc_seconds) * NS
        for name, value in kw.items():
            setattr(self, name, value)

    def move_ns(self, reference_ns, utc_ns):
        self.reference += reference_ns
        self.utc += utc_ns


class ControlClockTests(unittest.TestCase):
    def make_clock(self, fake, **kwargs):
        return ControlClock(reader=fake.read, reference_now=lambda: fake.reference, **kwargs)

    def test_small_forward_and_backward_steps_update_new_mapping_only(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        first = clock.snapshot
        self.assertIsNotNone(first)
        self.assertEqual(first.offset_ns, fake.utc - fake.reference)
        cumulative = 0
        for step in (1, -1, 2, -2):
            fake.move(seconds=1, utc_seconds=1 + step)
            current = clock.poll(force=True)
            self.assertIsNotNone(current)
            cumulative += step
            self.assertEqual(current.offset_ns - first.offset_ns, cumulative * NS)
            self.assertEqual(first.utc_at(first.reference_ns), first.utc_reference_ns)
        self.assertEqual(clock.current_utc_ns(), fake.utc)
        self.assertTrue(clock.report()["available"])
        self.assertEqual(clock.report()["correction_events"], 4)

    def test_two_second_step_includes_pairing_uncertainty_and_larger_step_degrades(self):
        fake = FakeClock(width=40_000)
        clock = self.make_clock(fake)
        original = clock.snapshot
        fake.move(seconds=0, utc_seconds=2)
        # A nominal +2-second step plus the old and new half-bracket errors
        # remains a valid +2-second correction.
        fake.next_width = 9_000_000
        accepted = clock.poll(force=True)
        self.assertTrue(clock.available)
        self.assertEqual(accepted.offset_ns - original.offset_ns, 2 * NS)
        saved = accepted

        fake.move_ns(NS, 3_100_000_000)
        self.assertIsNone(clock.poll(force=True))
        self.assertFalse(clock.available)
        self.assertEqual(clock.snapshot, saved)
        with self.assertRaises(ClockUnavailable):
            clock.current_utc_ns()
        self.assertEqual(clock.report()["reason"], "UTC correction exceeds supported step")

    def test_large_step_does_not_ratchet_bound_and_can_recover(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        anchor = clock.snapshot
        fake.move_ns(0, 3_100_000_000)
        self.assertIsNone(clock.poll(force=True))
        self.assertEqual(clock.snapshot, anchor)
        fake.move_ns(NS, 2_900_000_000)  # another 1.9 s step, but net offset is 5 s
        self.assertIsNone(clock.poll(force=True))
        self.assertEqual(clock.snapshot, anchor)
        fake.move_ns(NS, -4_000_000_000)  # returns to the original association
        recovered = clock.poll(force=True)
        self.assertTrue(clock.available)
        self.assertEqual(recovered.offset_ns, anchor.offset_ns)

    def test_corrections_accumulate_across_long_session_and_wall_slew_is_followed(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        start = clock.snapshot
        for _ in range(6):
            fake.move(seconds=1, utc_seconds=1, reference_skew=0)
            fake.utc += 500_000  # slow UTC-only slew (0.5 ms per second)
            snap = clock.poll(force=True)
            self.assertIsNotNone(snap)
        self.assertEqual(clock.snapshot.offset_ns - start.offset_ns, 3_000_000)
        self.assertTrue(clock.available)
        self.assertEqual(clock.report()["net_change_from_initial_ns"], 3_000_000)
        self.assertGreaterEqual(clock.report()["correction_events"], 3)

        # If both clocks advance together, their association stays stable while
        # the UTC query still advances with the corrected wall clock.
        expected = fake.utc
        fake.move(seconds=5, utc_seconds=5)
        self.assertEqual(clock.current_utc_ns(), expected + 5 * NS)

    def test_poll_cadence_and_report_do_not_read_clock(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        reads = fake.reads
        report = clock.report()
        self.assertEqual(fake.reads, reads)
        fake.move(seconds=0, utc_seconds=1)
        self.assertIsNotNone(clock.poll())
        self.assertEqual(fake.reads, reads)  # not due yet
        fake.reference += DEFAULT_POLL_INTERVAL_NS
        fake.utc += DEFAULT_POLL_INTERVAL_NS
        self.assertIsNotNone(clock.poll())
        self.assertEqual(fake.reads, reads + 1)

    def test_wide_or_failed_paired_read_degrades_without_rewriting_snapshot(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        accepted = clock.snapshot
        fake.move()
        fake.next_width = 11_000_000
        self.assertIsNone(clock.poll(force=True))
        self.assertEqual(clock.snapshot, accepted)
        self.assertIn("bracket", clock.reason)
        fake.move()
        fake.bad_read = True
        self.assertIsNone(clock.poll(force=True))
        self.assertEqual(clock.snapshot, accepted)
        fake.bad_read = False
        fake.move(seconds=1, utc_seconds=1)
        self.assertIsNotNone(clock.poll(force=True))
        self.assertTrue(clock.available)

    def test_reference_clock_reversal_is_permanent_and_does_not_change_anchor(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        accepted = clock.snapshot
        fake.reference -= 1
        self.assertIsNone(clock.poll(force=True))
        self.assertEqual(clock.snapshot, accepted)
        fake.reference += 2 * NS
        fake.utc += 2 * NS
        self.assertIsNone(clock.poll(force=True))
        self.assertFalse(clock.available)
        with self.assertRaises(ClockUnavailable):
            clock.utc_at(fake.reference)

    def test_integer_conversions_keep_nanoseconds_above_float_precision(self):
        fake = FakeClock(reference=2**54 + 123,
                         utc=2**60 + 2345, width=3)
        clock = self.make_clock(fake)
        snapshot = clock.snapshot
        target = fake.reference + 7_654_321
        converted = snapshot.utc_at(target)
        self.assertEqual(snapshot.reference_at(converted), target)
        self.assertEqual(converted - target, snapshot.offset_ns)

    def test_event_history_is_bounded_and_larger_steps_never_change_waveform_state(self):
        fake = FakeClock()
        clock = self.make_clock(fake)
        for i in range(25):
            fake.move(seconds=1, utc_seconds=1 + (1 if i % 2 == 0 else -1))
            snapshot = clock.poll(force=True)
            self.assertIsNotNone(snapshot)
        report = clock.report()
        self.assertEqual(report["retained_event_count"], 16)
        self.assertGreater(report["truncated_event_count"], 0)
        self.assertLessEqual(len(report["events"]), 16)
        with self.assertRaises(AttributeError):
            clock.snapshot.offset_ns = 0

    def test_settings_and_reader_values_are_validated(self):
        fake = FakeClock()
        for field in ("max_step_ns", "poll_interval_ns"):
            with self.assertRaises(ValueError):
                self.make_clock(fake, **{field: 0})
        with self.assertRaises(ValueError):
            self.make_clock(fake, max_bracket_ns=-1)
        fake = FakeClock()
        clock = self.make_clock(fake)
        fake.move()
        fake.next_reading = ClockReading(fake.utc, True, fake.reference + 1)
        self.assertIsNone(clock.poll(force=True))
        self.assertFalse(clock.available)


if __name__ == "__main__":
    unittest.main()

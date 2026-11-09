"""Bounded quiet-slot message completion for the headless Robust receiver.

The slot result is produced by the complete signal receiver. A failed or
incomplete slot is never interpreted as silence. The callback passed to
``decode_short_message`` is the canonical native short decoder and must return
only its already-filtered text and decoded byte count; plaintext bytes never
cross this Python boundary.
"""
from __future__ import annotations

import math
import time
from collections.abc import Callable, Mapping


_MAX_DECODED_BYTES = 16
_MAX_DATA_BITS = 13 * _MAX_DECODED_BYTES
_MAX_SLOT_COUNT = 2 * _MAX_DATA_BITS + 2
_MAX_SAFE_TEXT = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.@ -_/=\n"
)
_STAGES = ("helper", "coarse", "refinement_selection", "fine_preparation",
           "tracking", "decision", "recurring_bank_preparation")


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + " must be a positive integer")
    return value


def _finite_nonnegative(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(name + " must be finite and nonnegative")
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise ValueError(name + " must be finite and nonnegative")
    return value


class QuietSlotMessageReceiver:
    """Finish one short message from complete, consecutively scored bit slots.

    All bounds are local construction policy. This API accepts no received
    payload length or end marker. Two consecutive eligible scored-quiet slots
    after DATA establish physical completion. A quiet candidate is retained as
    a possible erasure until later DATA proves it was not the trailing end.
    """

    def __init__(self, decode_short_message: Callable[[tuple[int, ...]], Mapping], *,
                 quiet_slots=2, maximum_slots=_MAX_SLOT_COUNT,
                 maximum_data_bits=_MAX_DATA_BITS, sample_rate_hz=None):
        if not callable(decode_short_message):
            raise TypeError("canonical short-message decoder callback is required")
        self._decoder = decode_short_message
        self.quiet_slots = _positive_integer(quiet_slots, "quiet slots")
        self.maximum_slots = _positive_integer(maximum_slots, "maximum slots")
        self.maximum_data_bits = _positive_integer(maximum_data_bits, "maximum data bits")
        if self.maximum_slots > _MAX_SLOT_COUNT:
            raise ValueError("maximum slots exceed the local bounded policy")
        if self.maximum_data_bits > _MAX_DATA_BITS:
            raise ValueError("maximum data bits exceed the local bounded policy")
        if self.quiet_slots > self.maximum_slots:
            raise ValueError("quiet slots exceed the local slot budget")
        self._sample_rate = (None if sample_rate_hz is None else
                             _positive_integer(sample_rate_hz, "public sample rate"))

        self._next_slot = 0
        self._received_bits: list[int] = []
        self._interior_erasures: list[int] = []
        self._pending_nondata: list[tuple[int, str]] = []
        self._quiet_run: list[int] = []
        self._service_weighted = 0.0
        self._represented_baseband = 0.0  # Union of physical sample intervals.
        self._covered_samples = 0
        self._intervals: list[list[int]] = []
        self._previous_first_sample = None
        self._previous_stop_sample = None
        self._capacity_receipt_complete = True
        self._stage_calls = {name: 0 for name in (*_STAGES, "message_completion", "slot_state_machine")}
        self._classification_counts = {name: 0 for name in ("DATA", "QUIET", "AMBIGUOUS")}
        self._reacquisitions = 0
        self._callback_service = 0.0
        self._trailing_quiet_slots: list[int] = []
        self._state = "listening"
        self._reason = None
        self._decoded = None

    def _fail(self, state, reason):
        self._state = state
        self._reason = reason

    @staticmethod
    def _signal_eligible_slot(result):
        """Check signal coverage independently of timing/workspace receipts."""
        if not isinstance(result, Mapping) or result.get("status") != "scored":
            return False
        coverage = result.get("coverage")
        if not isinstance(coverage, Mapping):
            return False
        if (coverage.get("complete_observation") is not True or
                coverage.get("all_input_valid") is not True):
            return False
        consumed, expected = coverage.get("consumed_samples"), coverage.get("expected_samples")
        if (isinstance(consumed, bool) or not isinstance(consumed, int) or consumed < 1 or
                isinstance(expected, bool) or not isinstance(expected, int) or expected < 1 or
                consumed != expected):
            return False
        return True

    def _physical_geometry(self, result):
        """Validate and add the public physical interval; do not infer stride."""
        coverage = result.get("coverage") if isinstance(result, Mapping) else None
        if not isinstance(coverage, Mapping):
            return False
        first = coverage.get("physical_first_sample")
        stop = coverage.get("physical_stop_sample")
        rate = result.get("sample_rate_hz")
        count = coverage.get("expected_samples")
        if (isinstance(first, bool) or not isinstance(first, int) or first < 0 or
                isinstance(stop, bool) or not isinstance(stop, int) or stop <= first or
                isinstance(rate, bool) or not isinstance(rate, int) or rate < 1 or
                isinstance(count, bool) or not isinstance(count, int) or count < 1 or
                stop-first != count):
            return False
        if self._sample_rate is None:
            self._sample_rate = rate
        if rate != self._sample_rate:
            return False
        if (self._previous_first_sample is not None and
                (first <= self._previous_first_sample or stop <= self._previous_stop_sample)):
            return False
        # The windows move forward in physical time. If a window overlaps the
        # current union endpoint, only its new tail adds represented samples.
        # Otherwise it opens a distinct later interval.
        if self._intervals and first <= self._intervals[-1][1]:
            if stop <= self._intervals[-1][1]:
                # Strict endpoint advancement above makes this unreachable for
                # the latest interval, but retain an explicit guard.
                return False
            self._covered_samples += stop-self._intervals[-1][1]
            self._intervals[-1][1] = stop
        else:
            self._covered_samples += stop-first
            self._intervals.append([first, stop])
        self._previous_first_sample = first
        self._previous_stop_sample = stop
        self._represented_baseband = self._covered_samples/self._sample_rate
        return True

    @staticmethod
    def _valid_service_ratio(result):
        """Extract one outer complete-slot elapsed service ratio."""
        try:
            ratio = _finite_nonnegative(
                result.get("ratio_processing_seconds_per_baseband_second"),
                "slot service ratio")
        except ValueError:
            return None
        return ratio

    def _capacity_eligible_slot(self, result, geometry_valid):
        """Check timing receipts independently of valid DATA/QUIET decisions."""
        if (not self._signal_eligible_slot(result) or not geometry_valid or
                self._valid_service_ratio(result) is None):
            return False
        stages = result.get("stages")
        if not isinstance(stages, Mapping) or any(
                not isinstance(stages.get(name), Mapping) or
                stages[name].get("included_in_service") is not True
                for name in _STAGES):
            return False
        try:
            window_seconds = _finite_nonnegative(
                result.get("represented_baseband_seconds"), "slot baseband duration")
            if not math.isclose(window_seconds,
                    result["coverage"]["expected_samples"]/self._sample_rate,
                    rel_tol=1e-12, abs_tol=1e-12):
                return False
            for name in _STAGES:
                duration = _finite_nonnegative(
                    stages[name].get("represented_baseband_seconds"),
                    name + " represented duration")
                _finite_nonnegative(
                    stages[name].get("ratio_processing_seconds_per_baseband_second"),
                    name + " service ratio")
                if not math.isclose(duration, window_seconds, rel_tol=1e-12, abs_tol=1e-12):
                    return False
        except ValueError:
            return False
        return True

    @classmethod
    def _classification(cls, result):
        """Validate the backend's explicit signal/quiet/uncertain decision."""
        if not cls._signal_eligible_slot(result):
            return "AMBIGUOUS"
        coverage = result["coverage"]
        if coverage.get("zero_h0_verified") is not True:
            return "AMBIGUOUS"
        kind = result.get("slot_classification")
        if kind == "DATA":
            bit = result.get("decision_bit")
            operational = result.get("operational")
            if (isinstance(bit, bool) or bit not in (0, 1) or
                    not isinstance(operational, Mapping) or
                    operational.get("passes_conditional_bound") is not True):
                return "AMBIGUOUS"
            return "DATA"
        if kind == "QUIET":
            if coverage.get("valid_noise_statistics") is not True:
                return "AMBIGUOUS"
            quiet = result.get("quiet")
            if not isinstance(quiet, Mapping) or quiet.get("noise_consistent") is not True:
                return "AMBIGUOUS"
            energy = quiet.get("projected_energy")
            threshold = quiet.get("threshold")
            modes = quiet.get("observed_projection_modes")
            tail = quiet.get("noise_upper_tail_probability")
            try:
                energy = _finite_nonnegative(energy, "quiet projected energy")
                threshold = _finite_nonnegative(threshold, "quiet energy threshold")
                tail = _finite_nonnegative(tail, "quiet noise tail probability")
            except ValueError:
                return "AMBIGUOUS"
            if (isinstance(modes, bool) or not isinstance(modes, int) or modes < 1 or
                    not 0 < tail <= 0.001 or energy > threshold):
                return "AMBIGUOUS"
            return "QUIET"
        return "AMBIGUOUS"

    def _record_cost(self, result):
        represented = float(result["represented_baseband_seconds"])
        ratio = self._valid_service_ratio(result)
        self._service_weighted += ratio * represented
        stages = result.get("stages", {})
        if isinstance(stages, Mapping):
            for name in _STAGES:
                stage = stages.get(name)
                if isinstance(stage, Mapping) and stage.get("included_in_service") is True:
                    self._stage_calls[name] += 1

    def _finish_decode(self):
        final_quiet = set(self._quiet_run[-self.quiet_slots:])
        self._interior_erasures.extend(index for index, _ in self._pending_nondata
                                       if index not in final_quiet)
        self._trailing_quiet_slots = list(self._quiet_run[-self.quiet_slots:])
        self._pending_nondata.clear()
        if self._interior_erasures:
            self._decoded = dict(status="unavailable", reason="message_contains_erasures")
            self._state = "incomplete_message"
            return
        bits = tuple(self._received_bits)
        if not bits:
            # Before-first-DATA silence is a listening state, not an empty
            # message terminator. This is a defensive guard if the state rules
            # change later.
            self._fail("incomplete", "no_data_received")
            return
        try:
            callback_began = time.perf_counter()
            decoded = self._decoder(bits)
        except Exception:
            # The native codec may reject a truncated/noncanonical bit stream.
            # Do not forward exception text: it might contain received content.
            self._decoded = dict(status="invalid", reason="short_codec_rejected_message")
            self._state = "invalid_message"
            # A failed native decode still consumes work in this receiver.
            if "callback_began" in locals():
                self._callback_service += max(0.0, time.perf_counter()-callback_began)
                self._service_weighted += self._callback_service
                self._stage_calls["message_completion"] += 1
            return
        self._callback_service += max(0.0, time.perf_counter()-callback_began)
        self._service_weighted += self._callback_service
        self._stage_calls["message_completion"] += 1
        if not isinstance(decoded, Mapping):
            self._decoded = dict(status="invalid", reason="invalid_native_decoder_result")
            self._state = "invalid_message"
            return
        count, text = decoded.get("decoded_bytes"), decoded.get("filtered_text")
        if (isinstance(count, bool) or not isinstance(count, int) or
                not 0 <= count <= _MAX_DECODED_BYTES or
                not isinstance(text, str) or len(text) > _MAX_DECODED_BYTES or
                count != len(text) or
                any(ord(char) > 0x7e or char not in _MAX_SAFE_TEXT for char in text)):
            self._decoded = dict(status="invalid", reason="invalid_native_decoder_result")
            self._state = "invalid_message"
            return
        self._decoded = dict(status="decoded", decoded_bytes=count,
                             filtered_text=text)
        self._state = "complete"

    def consume_slot(self, slot_index, result):
        """Include state-machine and canonical decode work exactly once."""
        began = time.perf_counter()
        callback_before = self._callback_service
        row = self._consume_slot(slot_index, result)
        elapsed = max(0., time.perf_counter()-began)
        self._service_weighted += max(0., elapsed-(self._callback_service-callback_before))
        self._stage_calls["slot_state_machine"] += 1
        return self.result(reacquisition_required=row["reacquisition_required"])

    def _consume_slot(self, slot_index, result):
        """Consume exactly one receiver-produced fixed slot result.

        The caller processes the actual samples first. Slot indices only check
        public local chronology; the index never sets sample count, DSP shape,
        receive duration or an EOF condition.
        """
        if self._state in ("complete", "invalid_message", "incomplete_message",
                           "incomplete", "resource_limit", "failed"):
            raise RuntimeError("message receiver is terminal")
        if isinstance(slot_index, bool) or not isinstance(slot_index, int) or slot_index < 0:
            raise ValueError("slot index must be a nonnegative integer")
        if slot_index != self._next_slot:
            self._fail("failed", "slot_sequence_discontinuity")
            return self.result()
        if self._next_slot >= self.maximum_slots:
            self._fail("resource_limit", "local_slot_limit_exceeded")
            return self.result()
        self._next_slot += 1
        geometry_valid = self._physical_geometry(result)
        category = self._classification(result) if geometry_valid else "AMBIGUOUS"
        self._classification_counts[category] += 1
        service_eligible = self._capacity_eligible_slot(result, geometry_valid)
        if service_eligible:
            self._record_cost(result)
        else:
            self._capacity_receipt_complete = False
            stages = result.get("stages") if isinstance(result, Mapping) else None
            if isinstance(stages, Mapping):
                for name in _STAGES:
                    stage = stages.get(name)
                    if isinstance(stage, Mapping) and stage.get("included_in_service") is True:
                        self._stage_calls[name] += 1

        if category == "DATA":
            reacquisition = bool(self._pending_nondata)
            if reacquisition:
                self._reacquisitions += 1
                self._interior_erasures.extend(index for index, _ in self._pending_nondata)
                self._pending_nondata.clear()
            self._quiet_run.clear()
            bit = result["decision_bit"]
            if len(self._received_bits) >= self.maximum_data_bits:
                self._fail("resource_limit", "local_data_bit_limit_exceeded")
                return self.result(reacquisition_required=reacquisition)
            self._received_bits.append(bit)
            self._state = "receiving"
            self._reason = None
            return self.result(reacquisition_required=reacquisition)

        if category == "AMBIGUOUS":
            # An undecoded leading slot can contain a message prefix. Keep it
            # as an erasure rather than later decoding a truncated suffix.
            self._pending_nondata.append((slot_index, category))
            self._quiet_run.clear()
            self._state = "ambiguous" if self._received_bits else "listening"
            self._reason = result.get("reason") if isinstance(result, Mapping) else "unscored_slot"
            return self.result()

        # A scored quiet slot is only a possible end after DATA. Leading
        # silence never opens or completes an empty message.
        if self._received_bits:
            self._pending_nondata.append((slot_index, category))
            self._quiet_run.append(slot_index)
            if len(self._quiet_run) >= self.quiet_slots:
                self._finish_decode()
                return self.result()
            self._state = "awaiting_end_or_resume"
        else:
            self._quiet_run.clear()
            self._state = "listening"
        self._reason = None
        return self.result()

    def finish_input(self):
        """Transport EOF does not establish physical completion."""
        if self._state not in ("complete", "invalid_message", "incomplete_message",
                               "incomplete", "resource_limit", "failed"):
            self._fail("incomplete", "transport_ended_before_physical_completion")
        return self.result()

    def cancel(self):
        """Cancellation remains incomplete, regardless of received lengths."""
        if self._state not in ("complete", "invalid_message", "incomplete_message",
                               "incomplete", "resource_limit", "failed"):
            self._fail("incomplete", "receive_cancelled_before_physical_completion")
        return self.result()

    def result(self, *, reacquisition_required=False):
        """Return a bounded serializable message/physical-completion receipt."""
        physical_complete = self._state in ("complete", "invalid_message", "incomplete_message")
        return dict(
            schema="triad-complete-message-v1",
            status=self._state,
            reason=self._reason,
            physical_complete=physical_complete,
            decoded_content=self._decoded,
            received_bits=list(self._received_bits),
            data_bit_count=len(self._received_bits),
            erasure_slot_indices=list(self._interior_erasures),
            erasure_count=len(self._interior_erasures),
            trailing_quiet_slots=(list(self._trailing_quiet_slots) if physical_complete else
                                  list(self._pending_nondata)),
            quiet_slots_required=self.quiet_slots,
            slots_consumed=self._next_slot,
            next_slot_index=self._next_slot,
            message_ready_for_decode=self._state == "complete",
            reacquisition_required=bool(reacquisition_required),
            reacquisition_count=self._reacquisitions,
            stage_call_counts=dict(self._stage_calls),
            slot_classification_counts=dict(self._classification_counts),
            represented_baseband_seconds=self._represented_baseband,
            ratio_processing_seconds_per_baseband_second=(
                self._service_weighted / self._represented_baseband
                if self._capacity_receipt_complete and self._represented_baseband > 0 else None),
            complete_capacity_receipts=self._capacity_receipt_complete,
            service_metric_scope=(
                "Sequential complete-bit receiver service through quiet-slot completion; "
                "processing seconds per represented received-baseband second. "
                "Worker-thread seconds are not summed."),
        )

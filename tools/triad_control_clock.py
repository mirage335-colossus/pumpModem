"""Bounded wall-clock association for isolated triad control events.

This maps UTC to Python's monotonic control clock. It does not timestamp ADC or
DAC frames, discipline either clock, alter waveform samples, or resample audio.
The mapping is useful for delivery/control timestamps and future control work;
freeze a returned snapshot when an active search or protocol epoch is chosen.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import time
from typing import Callable, Optional


NANOSECONDS = 1_000_000_000
DEFAULT_MAX_STEP_NS = 2 * NANOSECONDS
DEFAULT_MAX_BRACKET_NS = 10_000_000
DEFAULT_POLL_INTERVAL_NS = 250_000_000
CORRECTION_EVENT_THRESHOLD_NS = 1_000_000
MAX_RETAINED_EVENTS = 16


class ClockUnavailable(RuntimeError):
    """No current supported UTC association is available."""


@dataclass(frozen=True)
class ClockReading:
    """One bracketed wall-clock observation."""

    utc_ns: int
    reference_before_ns: int
    reference_after_ns: int


@dataclass(frozen=True)
class ClockSnapshot:
    """Immutable UTC/reference-clock affine mapping captured at one revision."""

    utc_reference_ns: int
    reference_ns: int
    offset_ns: int
    uncertainty_ns: int
    revision: int

    def utc_at(self, reference_ns: int) -> int:
        _integer(reference_ns, "reference timestamp")
        return reference_ns + self.offset_ns

    def reference_at(self, utc_ns: int) -> int:
        _integer(utc_ns, "UTC timestamp")
        return utc_ns - self.offset_ns


def _integer(value, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer number of nanoseconds")
    return value


def _system_reading() -> ClockReading:
    before = time.monotonic_ns()
    utc = time.time_ns()
    after = time.monotonic_ns()
    return ClockReading(utc, before, after)


class ControlClock:
    """Follow local UTC corrections in control time, with bounded step handling.

    Corrections up to ``max_step_ns`` per accepted observation are applied to
    new UTC conversions. Repeated corrections may accumulate over long sessions;
    this does not expand or alter an already-established receiver bank. A larger
    step marks future UTC queries unavailable, while the most recent immutable
    mapping remains available for historical interpretation. A later observation
    within the original step bound can restore the current mapping.

    ``reader`` returns :class:`ClockReading` (or an equivalent three-tuple).
    ``reference_now`` supplies the same monotonic domain for poll cadence and
    current-control-time queries. Both are injectable for deterministic tests.
    """

    domain = "python-monotonic"
    scope = "UTC association for control/delivery time only; not ADC/DAC frame time"

    def __init__(
        self,
        *,
        reader: Optional[Callable[[], ClockReading]] = None,
        reference_now: Optional[Callable[[], int]] = None,
        max_step_ns: int = DEFAULT_MAX_STEP_NS,
        max_bracket_ns: int = DEFAULT_MAX_BRACKET_NS,
        poll_interval_ns: int = DEFAULT_POLL_INTERVAL_NS,
    ) -> None:
        self._reader = reader or _system_reading
        self._reference_now = reference_now or time.monotonic_ns
        self._max_step_ns = _positive_integer(max_step_ns, "maximum clock step")
        self._max_bracket_ns = _nonnegative_integer(max_bracket_ns, "maximum read bracket")
        self._poll_interval_ns = _positive_integer(poll_interval_ns, "clock poll interval")
        self._snapshot: Optional[ClockSnapshot] = None
        self._initial_offset_ns: Optional[int] = None
        self._last_observed_reference_ns: Optional[int] = None
        self._last_poll_reference_ns: Optional[int] = None
        self._last_event_offset_ns: Optional[int] = None
        self._available = False
        self._reason = "no valid clock observation"
        self._observations = 0
        self._accepted = 0
        self._rejected = 0
        self._correction_events = 0
        self._largest_accepted_step_ns = 0
        self._history_truncated = 0
        self._history = deque(maxlen=MAX_RETAINED_EVENTS)
        self._permanent_failure = False
        self.poll(force=True)

    @property
    def snapshot(self) -> Optional[ClockSnapshot]:
        """Return the most recent accepted immutable mapping, if one exists."""
        return self._snapshot

    @property
    def available(self) -> bool:
        """Whether new control-time conversions are currently supported."""
        return self._available and self._snapshot is not None

    @property
    def reason(self) -> str:
        return self._reason

    def poll(self, *, force: bool = False) -> Optional[ClockSnapshot]:
        """Observe clock association when due and return its last accepted map."""
        try:
            now = _integer(self._reference_now(), "reference clock")
        except Exception as error:
            self._rejected += 1
            self._available = False
            self._reason = f"reference clock read failed: {type(error).__name__}"
            return None
        if self._permanent_failure:
            self._available = False
            return None
        if now < 0:
            self._fail("negative reference clock", permanent=True)
            return None
        if self._last_poll_reference_ns is not None:
            if now < self._last_poll_reference_ns:
                self._fail("reference clock moved backward", permanent=True)
                return None
            if not force and now - self._last_poll_reference_ns < self._poll_interval_ns:
                return self._snapshot if self.available else None
        self._last_poll_reference_ns = now
        try:
            reading = _coerce_reading(self._reader())
        except Exception as error:
            self._rejected += 1
            self._available = False
            self._reason = f"clock read failed: {type(error).__name__}"
            return None
        try:
            self._process_reading(reading)
        except (TypeError, ValueError) as error:
            self._rejected += 1
            self._available = False
            self._reason = f"invalid clock reading: {error}"
            return None
        return self._snapshot if self.available else None

    def _process_reading(self, reading: ClockReading) -> None:
        self._observations += 1
        before = _integer(reading.reference_before_ns, "reference timestamp")
        after = _integer(reading.reference_after_ns, "reference timestamp")
        utc = _integer(reading.utc_ns, "UTC timestamp")
        if before < 0 or after < before:
            self._rejected += 1
            self._fail("invalid reference-clock bracket", permanent=after < before)
            return
        midpoint = before + (after - before) // 2
        uncertainty = (after - before + 1) // 2
        if after - before > self._max_bracket_ns:
            self._rejected += 1
            self._available = False
            self._reason = "clock read bracket exceeds supported uncertainty"
            return
        if self._last_observed_reference_ns is not None and midpoint < self._last_observed_reference_ns:
            self._rejected += 1
            self._fail("reference clock moved backward", permanent=True)
            return
        self._last_observed_reference_ns = midpoint

        offset = utc - midpoint
        previous = self._snapshot
        if previous is not None:
            step = offset - previous.offset_ns
            # Include measured uncertainty at both endpoints so an exact step
            # is not rejected merely because of the finite paired-read bracket.
            allowance = self._max_step_ns + previous.uncertainty_ns + uncertainty
            if abs(step) > allowance:
                self._rejected += 1
                self._available = False
                self._reason = "UTC correction exceeds supported step"
                return
            self._largest_accepted_step_ns = max(self._largest_accepted_step_ns, abs(step))
            if abs(offset - self._last_event_offset_ns) >= CORRECTION_EVENT_THRESHOLD_NS:
                self._record_event(midpoint, utc, offset,
                                   offset - self._last_event_offset_ns, uncertainty)
        else:
            self._initial_offset_ns = offset
            self._last_event_offset_ns = offset

        revision = 1 if previous is None else previous.revision + 1
        self._snapshot = ClockSnapshot(utc, midpoint, offset, uncertainty, revision)
        self._accepted += 1
        self._available = True
        self._reason = "following bounded UTC corrections"

    def current_utc_ns(self) -> int:
        """Return current corrected control UTC, polling first if its cadence is due."""
        now = _integer(self._reference_now(), "reference clock")
        if self._permanent_failure:
            raise ClockUnavailable(self._reason)
        if now < 0 or (self._last_poll_reference_ns is not None and now < self._last_poll_reference_ns):
            self._fail("reference clock moved backward", permanent=True)
        elif self._last_poll_reference_ns is None or now - self._last_poll_reference_ns >= self._poll_interval_ns:
            self._last_poll_reference_ns = now
            try:
                self._process_reading(_coerce_reading(self._reader()))
            except Exception as error:
                self._rejected += 1
                self._available = False
                self._reason = f"clock read failed: {type(error).__name__}"
        if not self.available:
            raise ClockUnavailable(self._reason)
        assert self._snapshot is not None
        return self._snapshot.utc_at(now)

    def utc_at(self, reference_ns: int) -> int:
        """Convert a future/current control coordinate using the current mapping."""
        if not self.available:
            raise ClockUnavailable(self._reason)
        assert self._snapshot is not None
        return self._snapshot.utc_at(reference_ns)

    def reference_at(self, utc_ns: int) -> int:
        """Convert a UTC target using the current mapping for future control work."""
        if not self.available:
            raise ClockUnavailable(self._reason)
        assert self._snapshot is not None
        return self._snapshot.reference_at(utc_ns)

    def report(self) -> dict:
        """Return bounded control-clock status; no per-chunk event log is kept."""
        snapshot = self._snapshot
        return dict(
            schema="triad-control-clock-v1",
            domain=self.domain,
            scope=self.scope,
            following=self.available,
            available=self.available,
            reason=self._reason,
            max_step_ns=self._max_step_ns,
            max_bracket_ns=self._max_bracket_ns,
            poll_interval_ns=self._poll_interval_ns,
            offset_ns=None if snapshot is None else snapshot.offset_ns,
            net_change_from_initial_ns=(None if snapshot is None or self._initial_offset_ns is None
                                        else snapshot.offset_ns - self._initial_offset_ns),
            latest_read_uncertainty_ns=None if snapshot is None else snapshot.uncertainty_ns,
            revision=None if snapshot is None else snapshot.revision,
            observations=self._observations,
            accepted_observations=self._accepted,
            rejected_observations=self._rejected,
            correction_events=self._correction_events,
            largest_accepted_step_ns=self._largest_accepted_step_ns,
            retained_event_count=len(self._history),
            truncated_event_count=self._history_truncated,
            events=list(self._history),
        )

    def _record_event(self, reference_ns: int, utc_ns: int, offset_ns: int,
                      step_ns: int, uncertainty_ns: int) -> None:
        if len(self._history) == self._history.maxlen:
            self._history_truncated += 1
        self._correction_events += 1
        self._last_event_offset_ns = offset_ns
        self._history.append(dict(reference_ns=reference_ns, utc_ns=utc_ns,
                                  offset_ns=offset_ns, step_ns=step_ns,
                                  uncertainty_ns=uncertainty_ns))

    def _fail(self, reason: str, *, permanent: bool) -> None:
        self._available = False
        self._reason = reason
        if permanent:
            self._permanent_failure = True


def _coerce_reading(reading) -> ClockReading:
    if isinstance(reading, ClockReading):
        return reading
    try:
        utc, before, after = reading
    except (TypeError, ValueError) as error:
        raise ValueError("clock reader must return ClockReading or (UTC, reference-before, reference-after)") from error
    return ClockReading(utc, before, after)


def _positive_integer(value, label: str) -> int:
    value = _integer(value, label)
    if value <= 0:
        raise ValueError(f"{label} must be positive")
    return value


def _nonnegative_integer(value, label: str) -> int:
    value = _integer(value, label)
    if value < 0:
        raise ValueError(f"{label} cannot be negative")
    return value

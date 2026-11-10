# System-clock synchronization and audio timing

PC UTC, audio sample rate and RF oscillator phase are independent quantities.
Selecting a GPS profile neither synchronizes the PC nor certifies an ADC/DAC.
The shared timing contracts apply to Fast, Legacy and the native Robust GUI
session. Robust uses an explicitly configured public calendar and declared
arrival bounds, together with actual audio-provider timing evidence. This does
not acquire unknown waveform epochs or certify device clocks. The old Robust
arrival-search/playback-repair campaigns and manual-candidate recipes remain
retired; see [current Robust scope](robust-current.md).

## Independent controls

Clock accuracy is a worst-case allowance at each station. Region is a total
propagation-uncertainty width centered on the selected offset. A 1 ms per-station
accuracy, 400 ms region and 2564 ms offset give an arrival interval centered on
2564 ms with radius 202 ms before audio/provider uncertainty. These are declared
assumptions, not a measured GPS bound or calculated lunar ephemeris.

Audio error describes causal lag `[0,A]` per station, not `+/-A`. The default
is 70 ms; supported durations range from zero through 60 seconds. It does not
change underrun-prevention buffer size. Entering a smaller allowance cannot
improve device evidence. Zero is a strict requested bound that ordinary device
timestamps generally cannot certify. See `include/datapump/clock_sync.hpp` for
shared duration validation, interval arithmetic and arrival mapping.

Known device queues are compensated in physical-event timestamps. Preserve a
capture timestamp's entire uncertainty radius U. A transmission aimed at the
midpoint A/2 retains radius A/2; do not halve provider uncertainty or add a
second midpoint shift to capture. A wider modeled arrival interval is not proof
that a device meets a narrower requested local Audio allowance. Default UI
settings and saved values do not establish any receiver's actual search coverage.

## Provider evidence

`include/datapump/audio_provider_timing.hpp` separates estimated timestamps from
bounded hardware dates, continuous sample counters, clock-quality reports,
future-rate certification and explicit discontinuity evidence. A transport name,
USB interface, selected oscillator or successful past fit grants none of those
capabilities. Evidence must refer to the actual sample clock and latency path.

A future-rate certificate bounds UTC seconds per nominal native second for its
stated validity interval and counter generation. A fit to past observations is
not such a certificate. Holdover needs its own valid evidence. A later update
cannot retroactively repair an expired interval or excuse a clock step outside
the previous bound. Report clock lock loss, changed counter generation or
unbounded dates; never silently promote estimated timestamps to bounded ones.

Discontinuity evidence distinguishes dropped, inserted, repeated and displaced
samples, including the affected counter interval. Callback lateness or ring
capacity does not establish actual sample loss. A bounded event record validates
what happened; it does not prove the waveform remained decodable or authorize
arbitrary repair. Preserve cumulative error and energy accounting across events.

## Continuous waveform coordinates

Logical source coordinates advance monotonically. UTC correction must not
rewind a keyed address, reseed a private stream, replay a pulse tail or discard
filter history. A continuous rate converter can preserve source continuity
while adjusting presentation, but its correction rate and accumulated phase
error still matter over a coherent observation. It cannot recover missing ADC
samples or certify an unknown hardware clock.

A narrow timing search requires a conservative map from source phase to actual
capture coordinates over the whole observation. Include both endpoints, the
initial timing interval, rate uncertainty and sample rounding. An affine past
fit can fail under evolving rate; reducing the search width without supporting
evidence can omit the correct candidate. Scheduling silence does not reset an
uncertainty budget or make a device clock stable.

## Historical findings and current validation

Old cable experiments distinguished clock following from matched-waveform
reception: a fitted timestamp path or successful short decode did not establish
sustained drift tolerance. Model-only interruption bounds did not qualify real
xruns, inserted/deleted PCM or arbitrary phase steps. Native cable evidence also
did not establish physical Windows/browser behavior. Preserve these limits when
using historical records in [validation](validation.md), [clock-follow research](triad-clock-follow.md)
and [live-duplex research](triad-live-duplex.md).

Use cheap shared timing correctness checks for contract edits. Intensive Robust
receiver questions must follow [hierarchical abbreviated testing](triad-performance.md),
with fixed inputs, uncertainty and a stopping rule. Do not revive long playback,
tracking or calibration campaigns as a fallback for uncertain timing evidence.

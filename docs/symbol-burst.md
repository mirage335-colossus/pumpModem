# Burst scheduling controls and research

The GUI minimum-wait choices Off, 1, 2, 3, 5 and 10 symbols map to
`HelperBurstConfig::wait_bits`; Random extra 0 through 3 maps to
`random_extra_bits`. These are complete logical DATA-bit waits in the native
`HelperBurstPlan`, not individual chips or FFT tiles. The native
`Format::burst_wait_tiles` remains a separate within-bit calendar.

Both peers share the same public anchor, initial ordinal, key and wait settings.
Each support starts on the UTC 30-second grid. Helper-enabled slots preserve a
fresh 38-second lead, complete DATA pulse support and protected Helper tail.
Off removes the requested additional wait; it does not remove these mandatory
supports, guards and grid alignment.

Random extra selects 0 through the configured limit from a keyed permutation
with equal counts in each 252-slot cycle. Anchor, initial ordinal and cycle
separate the permutation domain; a fixed cycle total allows bounded direct
lookup at distant ordinals. Without encryption the public Amateur key supplies
a reproducible calendar, not secrecy. Message completion does not restart the
calendar. Waits do not become missing DATA slots or quiet-completion evidence.

The GUI, exporter and receiver use the same public slot geometry. Source and
channel state remain continuous across selected waits, including pulse and
delayed channel tails. The implementation and bounded checks are distinct from
long-duration or physical-device qualification.

## Retained scheduling lessons

Separating transmissions can give a receiver time to process queued work, but
a wait does not lower the peak workspace of one acquisition. Preparation cost,
first-result latency, average tracking cost and backlog must be counted
separately. Do not assume random extra wait rescues the shortest allowed gap.

Quiet source intervals are not automatically safe to discard from a capture.
Pulse tails, channel delay, timing uncertainty, rate drift and outstanding
observations can overlap them. A receiver may exclude samples only when the
retained observation requirements establish that they are unnecessary. Missing
timestamps or unexplained discontinuities cannot be replaced by invented silence.

Scheduled waiting is not failed-symbol evidence or a message terminator.
The current protocol requires two fully scored quiet public DATA slots for
physical completion. A long coordinate or simulated gap is not a long-running device test.
Study scheduling with [minimal abbreviated checks](triad-performance.md) and
preserve the [audio timing contract](clock-sync.md).

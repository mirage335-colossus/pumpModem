# Burst scheduling controls and research

The GUI keeps minimum-wait choices Off, 1, 2, 3, 5 and 10 symbols, and random
extra-wait choices 0 through 3. They remain saved settings for future backend
connection. [Robust GUI operation is unavailable](robust-current.md); the old
single-carrier UTC calendar, keyed random waits, capture exclusion and
silence-based completion implementation have been removed.

The current native `triad::v3::Format` has `burst_wait_tiles`. Native geometry
owns its tile lattice, pulse margins and source extent. This is not evidence
that the old GUI's whole-symbol settings or random-wait semantics already map
to that field. Reconnection must make the mapping explicit and validate it in
the single canonical implementation.

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
The replacement protocol must define completion independently of the retired
receiver. A long coordinate or simulated gap is not a long-running device test.
Study scheduling with [minimal abbreviated checks](triad-performance.md) and
preserve the [audio timing contract](clock-sync.md).

# Long-coordinate tracking: retained findings

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The current `PracticalTrackingReceiver` uses persistent nominal and adaptive
timing lanes as an internal fixed mixture. Its input is a declared scheduled
post-acquisition DATA bank. The earlier fixed/reset complete receiver alternatives
are retired. Blind acquisition, Helper continuity and multi-day hardware uptime
are not established by scheduled DATA results.

A bounded sparse forward filter carries delay/rate hypotheses while preserving
complex channel likelihood and accumulated bit evidence. Known local ADC
coordinates and unknown remote transmitter movement are different quantities.
A source-clock correction trace must not be passed to the receiver as truth.
Integer coordinates can span long times without materializing a waveform.

The [gentle-tracking record](validation-data/triad-v3/2026-11-06-gentle-tracking/README.md)
and [long tracking record](validation-data/triad-v3/2026-11-06-multiday-tracking/README.md)
preserve the measured operating envelopes and failures. In particular, resetting
state across segments can lose coherent information; stable timing-controller
behavior alone does not establish unchanged likelihood or sensitivity. Timing
priors, phase coherence, sample rate and nominal audio carrier remain relevant.

Long logical coordinates in a tiny deterministic test are cheap geometry checks,
not multi-day reception. Do not repeat complete long observations, block Monte
Carlo campaigns or physical uptime runs as regression. Use bounded continuity
and work/memory checks, then the [abbreviated statistical method](triad-performance.md)
for any intensive inference, retaining unresolved scope explicitly.

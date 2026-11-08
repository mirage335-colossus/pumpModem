# Helper phase and relative-clock tracking lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

Pilot frequency estimates can guide later phase correction, while simultaneous
pilot phase difference can constrain relative timing/clock evolution. Each hop
has a fresh unknown common RF phase. A frequency estimate accumulated over the
lead-in does not authorize extrapolating that phase across arbitrary hops.

The historical studies separated the frozen 36-second frequency result from
fresh phase observations and the fixed 38-second lead. Delayed complete-dwell
conditioning also had real buffering latency. Corrected probe error, pilot
phase error, clock error and reported uncertainty are different observables.
Differential channel phase or transients can imitate clock motion, so a good
flat-channel fit does not remove those ambiguities.

Measure receiver work separately from capture generation and evaluator metrology.
A short held-out conditioning window cannot establish the cost of a complete
stream. Cold acquisition, steady tracking, fallback, preparation and sample
conditioning must remain distinct in any [abbreviated cost model](triad-performance.md).

The [retained Helper evidence](validation-data/helper/README.md) and
[refinement](helper-frequency-refinement.md)/[continuation](helper-continuation.md)
notes preserve approaches and failures. Their standalone implementations and
campaign commands no longer exist.

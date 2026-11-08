# Helper clock, phase and boundary research

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The [retained Helper evidence](validation-data/helper/README.md), including the
[October 31 results](validation-data/helper/2026-10-31/README.md), records
matched experiments and negative controls. Their standalone estimators and
campaign drivers are removed. The canonical integration is described in
[Helper integration](helper-integration.md).

## Techniques and what they address

| Technique | Useful idea | Important limit |
| --- | --- | --- |
| Dated offset/drift fitting | Combine pilot frequency observations across randomized hops | Covariance depends on geometry, pilot correlation and clock model |
| Recent-history comparison | Detect model mismatch while retaining a longer fit | Small point error does not justify removing a mismatch allowance |
| Joint pilot frequency/phase difference | Cancel each hop's unknown common RF phase | Differential channel phase can imitate audio-clock change |
| Bounded change detection and recovery | Reacquire after an inconsistent prediction | A fallback that was not exercised is not validated |
| Piecewise/evolving-clock fit | Represent continuing clock motion | Extra state or a favorable fixture does not establish identifiability |
| Delayed smoothing and complete-dwell correction | Use bounded future evidence to correct a retained stream | Delay and unavailable endpoints are real costs |
| Strict auxiliary inversion timing | Locate a transmitted marker without payload decoding | Individual markers may be unavailable below noise |
| Pooled inversion timing | Accumulate weak timing evidence over multiple dwells | Latency, competing offsets and conditional uncertainty remain explicit |

## Observation ownership and uncertainty

Historical cases froze the frequency result available after 36 seconds and
kept the 38-second lead. Later observations could update tracking, but could not
improve a result labeled available at the earlier time. Every hop needed its
own fresh RF phase; independent hop phases were not borrowed across dwells.
A predictor guided local fitting without being counted as independent evidence.

Reported conditional intervals, observed point errors and calibrated coverage
are different quantities. Thermal noise, common oscillator process and bounded
model allowances were reported separately. Whole-cycle timing ambiguity,
curvature, transients, frequency steps and differential channel phase exposed
failures that a good average frequency error did not resolve.

## Boundary timing does not identify hardware retuning

An auxiliary marker identifies the transmitted calendar, not an arbitrary
receiver/radio retune delay. In a recorded 2-ms early-retune fixture, using the
marker boundary worsened wrong-hop correction. Weak or absent marker evidence
also did not erase useful pilot corrections or captured input. These findings
argue for explicit state and uncertainty, not universal rejection of marker,
ramp, transient or joint-estimation methods.

The evidence includes simulation power, noise, delay and oscillator assumptions.
A Helper and a probe each assigned 40 W represent 80 W combined during overlap;
that is not a 40-W total transmitter case. No result establishes current triad
sensitivity, complete physical-noise admission, or ordinary-receiver integration.
New investigations must use [minimal abbreviated tests](triad-performance.md),
not recreate the old full-stream comparisons or standalone receivers.

# Bounded physical followup

This is an isolated triad experiment definition, not a routine regression or a
claim of successful reception. It closes selected gaps in the
[broad study](../../2026-11-03-broad/README.md): full timing acquisition for
middle/random missed dwells, real gaps inside a bit, rate/spreading contrasts,
zero translation, a shared endpoint clock, and explicitly independent added
modulation with zero ray separation. Ordinary Robust integration stays outside
scope.

The default paired screen contains ten cells, each with two signal bits and
one control-only paired null. The six scheduling cells are middle dwells11/12,
the frozen random10% and random25% masks, internal waits of three tiles,
rate1200/Added1 and rate36/Added100. Four non-FHSS cells compare None with
explicit added-modulation rate0 or0.1Hz, zero shift with8270Hz oscillator
reference, and5.8GHz shift driven by the baseband reference. The depth is Each65.
The timing bank retains both endpoints and zero over the tight±0.0905s prior;
its3dB spacing is the native random-chip/infinite-RRC approximation, **not** a
finite waveform loss certificate.

The actual paired seed roots and baseline arguments are read from the retained
`acquisition/plan-coarse.json`, not reconstructed from a prose command. The
independent suite predeclares16 balanced signal paths plus one null and one
wrong-key path per cell. These small cells do not qualify99% reception or a
rare daily false-admission rate. Conditional reuse and the three receiver
methods must not be counted as independent receptions.

## Historical scope and limits

This plan and its associated runners describe a completed historical physical
follow-up, not an active experiment recipe. The initial opt-in
`max-spreading-rate0p1` geometry estimate was subsequently tested in the
separate [additional physical evidence](evidence/additional-README.md): its
−16 dB-Hz validation scored 16/16 correctly with the public CFO bank, while
the zero-CFO comparison scored 1/16. That configuration requested 8 seconds
but produced 3,842 seconds of source calendar and 3,843.398 seconds of sampled
calendar. The 16-path result is not general reliability qualification. The
design's restricted acquisition matrix omitted clock-rate and keyed-epoch
search and did not qualify full multidimensional acquisition, 99% reception,
rare daily false admissions, physical RF retuning, or sustained performance.
The recorded results and failed jobs remain historical evidence; do not
regenerate the cohort from this plan. Any present-day compute-heavy
Robust test must follow the [hierarchical abbreviated method](../../../../triad-performance.md),
and generated evidence follows the [material lifecycle](../../../../development.md#material-lifecycle).

The default several-second timing range at600fine symbols/s was estimated to
require about35,200timings on this3dB grid, exceeding the native10,000-group
cap and the scorer's5,000-base-group cap (10,000bit candidates). Automatic
1MHz/30MHz rate geometry was also outside the sampled matrix. These were
historical scope exclusions, not measured outcomes.

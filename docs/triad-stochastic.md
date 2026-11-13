# Historical stochastic-channel research

This page retains compact findings from the isolated three-carrier stochastic
channel study. It is research context, not a current receiver guide or execution
recipe. The current Robust GUI uses the canonical native C++
transmitter/channel/Helper and complete receiver path described in
[current Robust status](robust-current.md). This historical study's receiver
and measurements do not qualify that integrated path.

## Technique and scope

The study modeled continuous time-varying dispersive paths with delayed complex
ray gains, declared Doppler structure, selective amplitude modulation, and
independent noise. DATA and Helper traversed the same channel state, including
guards and delayed tails. An experimental receiver compared complete hypotheses
using shared ray states and optional pilot evidence.

This matters because fixed-ray or sinusoidal modulation tests do not establish
reception under renewed random fading, and band-labeled carrier filters do not
represent overlapping carrier spectra. The study also found that Helper
frequency narrowing can remain informative when coherent pilot-to-DATA phase
transfer is unresolved; that is a scoped research result, not a universal
receiver architecture requirement.

## Retained evidence

The source-bound [2026-11-05 stochastic record](validation-data/triad-v3/2026-11-05-stochastic/README.md)
compares static and dynamic likelihoods on declared sampled conditions, including
8 and 0 dB-Hz HF cohorts and selected moving-delay/fast-selective cases. It
records both successes and misses, wrong-bit outcomes, independent null
controls, calibration identities, timing-bank failures, and a later DATA-only
fallback. Known synchronization in some cohorts and finite timing/epoch banks in
others limit interpretation. Replays and pilot ablations share captures and are
not independent samples.

The results do not qualify the configuration cross-product, daily false
admissions, a general −60 dB-Hz receiver, blind multi-day acquisition, a physical
radio link, or an integrated GUI path. Apply the exact source, channel law,
acquisition assumptions, and uncertainty limits in the dated record before
using a number as a historical comparison.

## Nonstationary-channel research reconciliation

The two independent reviews in
[`_ref/resilience-triple_Robust_Modem`](../_ref/resilience-triple_Robust_Modem/)
were reconciled against native source `a0801df` on 2026-11-13. Their central
coverage finding agrees: a configured maximum echo delay/Doppler used as one
fixed template is not an envelope of every intermediate channel. The fine
receiver already evolves complex gains; “fixed-affine” refers to timing and
frequency coordinates, not constant gains. Conversely, source mismatch does
not prove failure: short native point trials tolerated intermediate echoes and
a common frequency chirp. Qualification needs contrasting signal/noise trials,
not just a matched simulator or an inspection of parameter names.

The detailed reference reports remain useful for the unresolved weak-level,
long HF/EME manual configurations: their source-bound analytical margins and
planner provenance are not replaced by the short native checks below. Retain
them for that specific comparison, without treating their older receiver
description as current. Distill the remaining applicable numerical assumptions
and retire redundant reports when that comparison is resolved.

[ITU-R F.1487](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)
describes stationary *random* tap gains and separates spectral centers from
widths. Stationarity of a law does not mean constant samples. An OU law, a
Gaussian Doppler spectrum, bounded smooth frequency wander and sinusoidal
selective coefficients are different models. Their temporal correlation and
spectral allowances must remain explicit. Gaussian spectral tails and the
nonlinear Gaussian-erf coefficient law do not have hard spectral support.

Recent field work adds useful contrary evidence to a universal Gaussian model.
[Fernandes et al. (2026)](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1713968/full)
find different fitted Doppler-residual distributions across diurnal regimes on
one 10 MHz link. Those five-minute residual statistics are not a law for
individual complex tap gains.
[Aricoche, Hysell and Morton (2026)](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full)
distinguish apparent range/Doppler spreading from changes within the averaging
window; their stationarity inference remains qualified. Neither study supplies
this modem's sensitivity, fade-duration guarantee or computational capacity.

Retain both long-coherence and changing-channel evidence. The historical study
found substantial gains from dynamic likelihoods in some known-synchronization
cohorts, but flexible models also admitted wrong-key signals and short-memory
models lost useful weak-signal evidence. Helper frequency assistance is distinct
from phase transfer across widely separated carriers. A new proposal statistic
must not become an unnormalized admission statistic. Compare competing bits,
noise, incorrect addresses, fades/recovery and physical quiet completion, and
charge preparation and unsuccessful acquisition as well as successful tracking.

Instantaneous bounds alone cannot guarantee reception through arbitrary rapid
variation or simultaneous sustained fades. Model delay support, Doppler center,
spectral width, variation rate and path powers separately. Changes in powers of
fixed-delay paths can vary effective delay spread without implausibly rapid
physical path motion; moving a delay also changes RF phase and must charge its
induced Doppler. Keep these distinctions when extending the native channel and
receiver, and reconsider temporal bases or bounded causal state inference when
fixed-block model mismatch remains material.

The native weak-sweep discriminator also separates structured motion from an
information limit: a weak static signal passed, a bounded linear sweep missed,
and a labelled oracle dechirp restored admission. Exact timing alone did not.
This motivates structured drift proposals alongside stochastic models, rather
than forcing every case into independent short blocks.
[Ransom et al. (2002), §4.2.2](https://arxiv.org/pdf/astro-ph/0204349)
demonstrate local Fourier-domain constant-drift matching in their pulsar search;
application after keyed modem matching still needs native evidence.
[Melatos et al. (2021)](https://arxiv.org/pdf/2107.12822)
show the benefit of phase continuity in their frequency/phase HMM, but their
unnormalized amplitude integral and empirically calibrated statistic cannot be
used as this receiver's conditional likelihood bound. A proper fixed mixture or
retained sub-mixture must account for every added model and original public
coordinate. A data-dependent proposal is not a free extra admission attempt.

A valid DATA-versus-white-noise likelihood is also not a guarantee of correct
bit discrimination or rejection of an unrelated signal. The first short-block
HF trial produced large, nearly equal scores for both bits. A separate pure-tone
control produced a false DATA decision despite a large bit-score gap. These
are different failure mechanisms. A minimum bit-score separation can turn
near-ties into erasures, but does not solve unmodeled interference or establish
a posterior error bound for retained, data-selected hypothesis sums.

Interference rejection must preserve a real signal in the same observation.
Dividing a DATA-only likelihood by a line likelihood can reject genuine DATA
as the interferer grows. A joint DATA-plus-line model instead projects both the
observation and DATA templates off the same nuisance direction, with the line
factor retained in both numerator and denominator. The projection changes the
template Gram as well as the correlations.
[Scharf and Friedlander (1994)](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf)
give the matched-subspace foundation. Explicit line alternatives in
[Keitel et al. (2014)](https://arxiv.org/abs/1311.5738) and their
[sensitivity follow-up](https://arxiv.org/pdf/1409.2696) illustrate both the value
of such alternatives and losses from poorly matched line rejection. Their
detector geometries do not qualify this modem. Retain pure-line, off-grid-line,
DATA-only and DATA-plus-line controls; score physical quiet on the original
unnotched observation.

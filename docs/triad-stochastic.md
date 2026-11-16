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

The native receiver now reserves a small slow complex-gain mixture for public
variable-channel configurations whose fine-cell cadence is itself slow. It
keeps the original 0.5 s, 2 s and infinite-coherence family at 75% prior mass,
and gives 12.5% each to two additional first-order complex-Gaussian gain laws.
Their correlation is exponential, `R(dt)=exp(-|dt|/tau)`, with coherence
scales derived from a declared rate, floored at two fine cells, and four times
that scale. The receiver
reuses its existing projections and channel sufficient statistics; this adds
two constant-size posteriors per fixed-affine coordinate, not a bank of path
histories. It is enabled for positive declared variation when two fine cells
span at least one second. A larger declared bound does not exclude slower
actual evolution, so it must not disable these slow alternatives. The cadence
gate and scale floor are resource/model-support choices, not evidence that the
physical channel is OU or that the receiver follows the maximum declared rate.

An excursion bound does not determine the stochastic spectrum or whether the
complex gain has constant magnitude. Exponential complex-gain covariance gives
a Lorentzian spectrum; ITU-R F.1487's Gaussian Doppler tap is a distinct law,
and a smooth bounded frequency wander or pure phase diffusion is distinct
again. Ghandour-Haidar, Ros and Brossier compare correlation-matched and
estimation-error-based AR(1) parameters for Jakes fading and show that the
preferred approximation depends on the estimation objective; that channel
estimation result does not validate this modem's gain model
([Signal Processing, 2012](https://doi.org/10.1016/j.sigpro.2011.08.014)).
Noncoherent low-SNR capacity also depends on the temporal fading covariance and
the peak/average power constraints, so an AWGN trend or a single spectral width
cannot establish the receiver's weak-signal floor
([Sethuraman et al.](https://doi.org/10.1109/TIT.2009.2012995)). Keep the slow
models as fixed-prior alternatives and assess matched OU, deterministic-wander,
phase-only, negative-control and quiet-completion cases separately. Their
presence does not guarantee reception outside the supported laws or bounds.

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

## Wrong Key Admission

The native receiver has a final, receiver-local **Wrong Key Admission**
heuristic. `current` is the default and preserves the previous DATA/erasure
decision exactly. Stronger policies apply only after ordinary model
fallback and scored-quiet checks have finished: a baseline DATA result becomes
an ambiguous erasure when its two operational bit log scores are too close.
Let `g` be their absolute gap, `m` their minimum, `T` the ordinary DATA
threshold, and `rho` and `delta` the selected policy constants. The boundary is

`g < max(existing_min_gap, delta) + rho * max(0, m - T)`.

`guarded` uses `delta=ln(10), rho=0.10`; `strict` uses
`delta=2*ln(10), rho=0.25`. The stronger GUI reference targets scale the
**entire Strict required margin** by 1.5, 2.75 and 4.0. Thus they also increase
the absolute gap floor when the competing score is below `T`; increasing only
`rho` left a residual class unaffected. Current, Guarded and Strict retain
their original equations. This is scalar arithmetic after the normal score
has been produced: it adds no search, projection, DSP workspace or worker,
restarts no fallback, and changes no public geometry. It does not alter the
transmitted waveform or its duration. An erasure cannot count as physical
quiet; the usual two scored quiet slots remain required for completion.

In the GUI this is now a reversible **browser** choice: the native session
always receives/decodes the unfiltered baseline and retains per-message DATA-bit
pass counts.
Completed rated records remain visible when at least half their admitted bits
pass the selected tier, with ties visible. This avoids a single-bit veto on a
long message, but it is still a chosen presentation rule, not a calibrated
message probability. Original decoded content and explicit saved files do not
change, and no second DSP or codec pass is performed. Unfiltered restores hidden
records within the bounded RAM history; pending/unrated records remain visible.
The single-bit studies below measure per-bit admission/retention, not the
multi-bit browser rule's message-level error rate. See
[reversible history validation](validation.md#reversible-wrong-key-admission-history--15-november-2026).

The dropdown uses `Unfiltered`, `2/3 reduction ~1dB`,
`1/3 reduction ~2dB`, `1/10 reduction ~3dB`,
`1/100 reduction ~6dB*` and `1/1000 reduction ~6dB*`.
The asterisk identifies an extrapolated model target in the control's help.
The dB values are approximate HF reference visibility-loss estimates. The
1/10 pilot crossing shares the baseline's `(7,10]` interval; both deeper
crossings occupy `(10,13]`. Those grids motivate coarse 0–3 and 0–6 dB
ranges, summarized by the compact ~3/~6 dB labels. These are not confidence
bounds or distinct exact penalties for the deep tiers.
The earlier finer-grid Guarded/Strict shifts motivate ~1/~2 dB. Clean HF showed
no extra losses, and long Sub-9 counterexamples still prevent a universal
added-dB interpretation. Original reception remains retained at every choice.
Fractions mean the target proportion
of wrong-key DATA bits **remaining relative to Unfiltered** in the static-HF
reference, not the fraction removed. The corresponding saved tokens remain
`current`, `guarded`, `strict`, `very-strict`, `extreme`, `maximum`.
These names do not set universal admission rates or the fraction of multi-bit
message rows hidden by the browser. The earlier changing-HF long-burst results
already retained 41/57 for Guarded and 23/57 for Strict, above the nominal
2/3 and 1/3 fractions. Preserve this contrary evidence.

For deeper reference targets, the pilot ranks baseline false DATA by
`R = g / [max(g0,2*ln(10)) + .25*max(0,m-T)]`. The 1/10 cutoff uses empirical
conditional order statistics separately in each foreign-burst stratum. The
1/100 and 1/1000 cutoffs use a predeclared exponential mean-excess tail fitted
at the upper third and upper fifth of each stratum, taking the largest fitted
cutoff and rounding upward to a quarter unit. Pareto fits and largest-observation
influence are reported separately, not presented as confidence intervals.
The two deepest choices are extrapolated model targets, not probabilities
verified by zero survivors in a small holdout.

This method follows the need to examine threshold choice and tail stability
in [Davison and Smith](https://doi.org/10.1111/j.2517-6161.1990.tb01796.x),
[Northrop et al.](https://arxiv.org/abs/1504.06653) and
[Murphy et al.](https://arxiv.org/abs/2310.17999). Bounds for fixed positive
quadratic forms of subgaussian vectors motivate comparing light tails
([Hsu, Kakade and Zhang](https://www.cs.columbia.edu/~djhsu/papers/quadratic-ecp.pdf));
they do not identify the tail onset or slope of this adaptive competing-score
ratio. Finite held-out risk calibration requires representative independent
draws, as in [Tong, Feng and Li](https://pmc.ncbi.nlm.nih.gov/articles/PMC5804623/)
and [Angelopoulos et al.](https://doi.org/10.1214/24-AOAS1998). Neither these
methods nor a display threshold justify transferring the reference probabilities
to an unspecified channel, key, foreign power, repeated-burst mixture or field
population.

The shared operational threshold is `T=ln((ceil(86400/0.4)+1)/10^-4)`
`=ln(216001/10^-4)=21.4933786883`, giving `exp(-T)=4.6296082e-10` for
one opportunity. Under the receiver's conditional noise-null argument, the
union bound across 216,001 nominal 0.4-second opportunities is
`216001*exp(-T)=10^-4/day`. `current`, `guarded` and `strict` retain this same
bound because the extra rule only removes baseline DATA decisions. These are
design constants and a conditional bound, not measured radio noise, wrong-key
traffic, a physical false-admission guarantee or a rate estimated from trials;
the bound does not cover a structured interferer. Margin nats are not universal
dB equivalents or tenfold changes in a false-admission rate.

The extra threshold is dormant when the competing score is at or below `T`
and the pre-existing minimum gap `g0` already meets the selected `delta`:
then the added score-dependent term is zero and the baseline already enforced
`g0`. In the changing-channel default, `g0=ln(199)=5.2933`, above the strict
floor `2*ln(10)=4.6052`; for `m<=T` neither stronger policy changes that
gap rule. For `m>T`, the `rho*(m-T)` term can still make the filter active.

### Interference-time exposure is not window exposure

A 10% interferer duty cycle describes active airtime; it does not mean 10% of
candidate receive windows overlap interference. Let `L` be burst duration and
`W` a candidate window. For periodic bursts of length `L` every `10L` with
random phase, the fraction of windows touched is
`min(1,(W+L)/(10L)) = min(1,0.1*(1+W/L))`: this holds directly when
`W+L<=10L`, and it saturates at 1 when `W>=9L`.
Thus every 24-hour candidate window can overlap a burst even when burst
airtime is only 10%.

For Poisson burst starts with fixed duration `L`, 10% stationary time
occupancy requires `lambda*L=-ln(0.9)`. The number of bursts overlapping one
window is Poisson with mean `mu=lambda*(W+L)=-ln(0.9)*(1+W/L)`, so the
probability of at least one overlap is `1-exp(-mu)`. This includes multiple
possible bursts during the window; applying a one-burst overlap probability
once would undercount long windows. If candidate windows recur every `C`
seconds and `p` is the conditional false-DATA probability given an exposed
window, expected false-DATA windows per day are `(86400/C)*f_window*p`, where
`f_window` is the periodic fraction above or the Poisson `1-exp(-mu)`. This is
an expected count, not the probability of any daily event; windows can be
dependent. The conditional probability `p` here includes every burst count;
it is not the one-burst admission probability `p1` measured by the native
single-interferer probe. Without repeated-burst qualification, a conservative
Poisson per-window bound is
`exp(-mu)*p0 + mu*exp(-mu)*U1 + 1-exp(-mu)*(1+mu)`, where `p0` is the
conditional noise-only bound and `U1` an upper confidence bound for one burst.
The last term treats every window with at least two bursts as potentially
admitted. This is an uncertainty bound, not a prediction of multiburst errors.
A 10% time-stress result alone supplies neither `p` nor that rate.
Sub-9/HF scenarios with bit windows up to 24 hours need their actual `W`, burst
duration `L` and cadence; airtime alone does not define window exposure.

Treat this as a relative-competition filter. It asks whether the winning DATA
bit is sufficiently separated from its competing bit relative to excess score
above the existing threshold. It is not a likelihood against a wrong-key
population, calibrated error probability, daily false-admission rate, sender
authentication or a guarantee that every unrelated keyed signal is rejected.
One accidentally dominant bit can still pass. A receiver-side threshold can
also erase wanted weak, distorted or strong long-window bits; there is no
universal sensitivity guarantee or assurance that more power restores admission.
The default preserves prior decisions for compatibility, while
stronger modes trade some wanted-signal admission for extra rejection of
near-balanced competing scores.

The motivation is bounded native evidence, not broad calibration. An early
strong foreign-key native fixture had relative-gap ratio 0.0298, where the
ratio is the bit-score gap divided by `max(1, min(bit scores) - T)`. In a
second diagnostic, four receiver roots scored on the same IQ all rejected
under `strict`, with ratios 0.0249, 0.1934, 0.0605 and 0.1234. The repeated
roots are not independent channel episodes, so they do not estimate natural
wrong-key frequency or a daily rate.

Fresh native holdouts sharpen the tradeoff. In two separate configured
carrier/bit/noise strata (seed 74, bit 0, carrier 1743; seed 113, bit 1,
carrier 1719), wrong-key-only inputs that baseline falsely admitted as DATA
were rejected by both stronger modes. Wanted near-threshold DATA in the same
strata, with operational scores 23.845 and 26.786 against `T=21.493`, passed
both modes. In matched cochannel mixes using wanted amplitudes 0.07 or 0.16
and a wrong-context same-frequency interferer at 0.38, all four baseline and
`guarded` decisions were correct; `strict` erased one, seed 113 at wanted
amplitude 0.07 (gap 6.6862 versus required separation 14.5614). This is direct
evidence of a receiver-side wanted-signal cost, not a general sensitivity or
wrong-key rate. See the source-bound [native validation record](validation.md)
for matched inputs and outcomes.

Earlier normalized-shadow work found that fixed-shape directional scores
saturate at high SNR and can lose wanted signals outside their admitted
subspace; a fixed 9/8 variance penalty also lost weak wanted DATA. These
contrary findings rule out treating a universal directional or variance veto
as a safe replacement. See the retained
[normalized-null analysis](../_appraisal/highFidelity-triple_Robust_Modem/hf-normalized-null.md)
and [predictive-completion analysis](../_appraisal/highFidelity-triple_Robust_Modem/hf-predictive-completion.md).

Formal robust tests illustrate the gap between a useful heuristic and a
calibrated test: safe/e-value and robust likelihood-ratio methods state their
null and sampling assumptions, while fixed-ratio GNSS literature warns that a
fixed critical ratio does not itself set a fixed failure rate. These are
methodological analogies, not derivations for this bit-score rule
([Grünwald, de Heide and Koolen, 2024](https://academic.oup.com/jrsssb/article/86/5/1091/7623686),
[Saha and Ramdas, 2025 version](https://arxiv.org/abs/2408.14015),
[Teunissen and Verhagen, §§2.6 and 3.3](https://gnss.curtin.edu.au/wp-content/uploads/sites/21/2016/04/Teunissen2007GNSS.pdf)).
The related Saha and Ramdas paper submitted on 1 November 2026 is a preprint
about instance-optimal sequential testing under its own formal setup, not
validation of this receiver heuristic ([preprint](https://arxiv.org/abs/2611.02130)).
Recent theory also cautions against declaring calibrated low-cost rejection
fundamentally impossible. [Grünwald et al., v2](https://arxiv.org/abs/2404.19465v2)
identify covariance conditions under which a simple likelihood ratio is valid
for an entire exponential-family null. [He and Cheng, 2026 preprint,
Theorem 2.2](https://arxiv.org/html/2609.27923v1) establish asymptotically
lossless exact/pivotal evidence growth for a finite composite null with iid
observations and joint atomlessness. Neither result supplies a finite-sample
threshold or processor-cost guarantee for this receiver's adaptively fitted,
correlated fragments and unknown interfering waveform. Reconsider a cheap
calibrated statistic if a suitable null law for already-computed sufficient
statistics can be established; do not infer it from the current score ratio.
Probability claims require predeclared independent draws matching their
endpoint. A complete public-slot score with a fresh key/noise/channel draw
supports a conditional false-DATA-window estimate; message-completion or
whole-receiver success requires complete receiver episodes. Fragments,
timing variants, paired policies/levels and roots sharing an IQ capture
remain dependent and do not multiply the independent sample count. See the
[episode-level statistical protocol](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/86-statistics-and-calculation-checks.md).
The completed 1,080-case static-HF study and separate 384-case sensitivity
refinement are recorded in [validation](validation.md#wrong-key-admission-rate-and-sensitivity-study--15-november-2026).
Clean static-HF wanted decisions were identical across the three policies. Under one
strong short-interferer case, the empirical 90%-correct reception point moved
by approximately +1 dB for Guarded and +2 dB for Strict on a 1 dB grid.
Wrong-key admissions remained far more frequent than the conditional thermal
bound. Larger relative or absolute thresholds lost wanted signals and did
not provide a calibrated generic low-rate option. These scenario-specific
findings are not universal dB penalties or thermal-rate multipliers.
The separate 96-minute Sub-9 wanted diagnostic is contrary evidence: a
same-key clean signal passed all policies at −18/−12/−6 dB-Hz, but Strict
erased it at 0 and both stronger policies erased it at +6/+12. Two fresh
channel/noise seeds also showed strong-signal erasures. The relative penalty
can grow faster than the discriminating bit-score gap. Consequently, neither
the HF +1/+2 dB figures nor a monotone benefit from raising modeled C/N0 applies
generically; retain Current when this tested long-window sensitivity matters.
The separate 576-call changing-HF GPU confirmation also showed no extra
clean-signal losses on its tested grid, but Strict still admitted 23/128 long
and 18/128 short foreign sources. Under the declared periodic 10% traffic
model these correspond to 15.07 and 29.68 false windows/day, far above the
conditional thermal target. The 96-minute Sub-9 adjacent/cochannel cohorts
observed respectively 19/1/0 out of 32 and 8/1/0 out of 16 for
Current/Guarded/Strict. Strict's zero observations give simultaneous 95%
upper bounds of 0.308 and 0.580 false windows/day in their separate
three-policy families, not zero risk. All cochannel frequency envelopes were
contained; the adjacent cohort includes common-wander excursions.
The measured families condition one interferer on nominal-envelope overlap
and do not represent natural traffic, 24-hour windows or complete-message rates.
The native `admission` probe scores once and applies all three policies to the
same observation, a reusable mechanism comparison rather than a daily-rate
estimator.

# Bounded HF phase refinement

This continuation starts from the retained off-grid final-bit failure in
[HF validation](hf-validation.md), using the same native receiver and approved
internal evidence hook. New work remains contained in this directory. Its first
question is whether missing phase coherence, search/prior cost or the particular
noisy observation limits that case. Neither recovery of that selected case nor
a fitted diagnostic establishes a general HF sensitivity floor.

## First causal diagnostic, frozen 14 November 2026

Run exactly two wanted, active-slot `hf-search` observations with the new
evaluator-only physical diagnostic, full public timing and existing phase bank:

| Scenario | Seed | Requested bit duration | C/N0 | Public slot |
| --- | ---: | ---: | ---: | ---: |
| `hf-vertical-eikonal-holdout` | 11001 | 128 s | 0 dB-Hz | 2 |
| `hf-vertical-eikonal-motion` | 10201 | 128 s | 0 dB-Hz | 1 |

Budget: 120 seconds aggregate, 60 seconds per observation, four million samples.
Use the local CPU and its automatic `W=2*H` allocation, with one intensive job
at a time. These are causal comparisons on existing observations, not additional
independent noise or environmental trials. The selected IQ/validity hashes must
match the retained observations exactly.

Run the ordinary public scorer first. Only afterward reconstruct two noise-free
native full-prefix emissions through the same channel and seed, changing only
the selected source bit. Advance both channels from sample zero and crop the
same public aperture; do not substitute the older isolated-bit oracle geometry.
Stream these references to the existing native history evaluator. Its single
full-aperture partition gives the known-physical-template complex-Gaussian-gain
likelihood and deterministic retained energy. Separately rerender the public
search's winning timing/phase/CFO cell and compare its clean-signal coherence
and raw single-cell likelihood. Retain its actual block-center approximation.
The physical templates, source bit and channel state never enter the public
scorer or receiver. A known-template likelihood on one noise realization is a
diagnostic, not a universal upper bound or an operational admission statistic.

High physical-template evidence with poor public-template coherence motivates
bounded phase refinement. Low matched evidence instead motivates an explicit
time/energy comparison; extra search alone would not explain a recovery.
Report raw likelihood, all applicable prior penalties and complete inference
separately. Keep every completed failure and resource-censored observation.

## Statistical and computational requirements for a candidate

The existing 97-model family currently occupies the whole supplemental prior.
A broader family must share a normalized fixed mixture with it; retaining all
old mass and adding positive new mass is invalid. An initial half/half split
would cost the old branch `log(2)` when new work contributes nothing. The outer
native supplement weight remains 0.25. Measure that cost on the previously
successful longer weak case.

Any complete parameter bank and its weights are fixed from public geometry
before IQ is inspected. Data-dependent proposals may choose cells, but retained
cells keep their original weights, duplicates are removed, and omitted mass
stays absent. A fitted trajectory is never itself a calibrated likelihood.
The entire public CFO prior remains charged. Constant/linear phase projection
may help grid placement or numerical conditioning, but cannot remove bounded
frequency uncertainty. Changes to maximum phase derivative must update the
effective-template block approximation and its error bound.

Reuse native block correlations and waveform rendering. Bound retained arrays,
proposal lanes, starts, sweeps, local iterations, exact shape/CFO work and all
scratch before observing samples. Smaller dispatched work cannot reduce the
receiver's global `W=2*H` budget. Resource pressure cannot restore omitted prior
mass or convert incomplete work into successful reception. Native complete
bit/null decisions, integrity checks and two scored quiet slots remain the
communication endpoint. The detailed candidate and its follow-up cases will be
frozen here before their outcomes are observed.

## Research cautions

The [existing HF research](hf-research.md) and the
[Astra skywave review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md)
separate slowly varying phase path, group delay, multiple magnetoionic modes,
small-scale scattering and mode disappearance. A denser periodic bank addresses
interpolation within one smooth family; it does not cover all those mechanisms.
The existing measured Ding excerpt still shows no nonlinear-search advantage
beyond ordinary CFO compensation.

HiCIRF explicitly discusses the danger of its coherent Rician component omitting
large-scale TEC phase change, thereby suggesting excessive coherent-integration
gain; it motivates separating large-scale evolution from small-scale
scintillation. [Nickisch et al. (2012)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2011RS004928).
Recent Alaska HF observations distinguish carrier-phase evolution from group
delay and report rapid changes beyond the inversion's resolution. Their
10-second coherent intervals and minute-scale reconstructions cannot calibrate
subsecond sample paths. [Aricoche et al. (2026)](https://www.frontiersin.org/articles/10.3389/fspas.2026.1848834/full).

Synthetic gravitational-wave wandering studies offer inference analogies, not
HF calibration. Carlin and Melatos compare deterministic and wandering signal
models, but their localization statistic and supplied true sky/orbital
parameters do not establish operational false-alarm thresholds.
[Carlin and Melatos (2025), §V](https://arxiv.org/html/2504.08163v1).
This favors testing model mismatch explicitly while preserving the current
proper white-noise likelihood argument.

The initialization failure also agrees with the staged-search distinction in
[Pletsch and Clark (2014)](https://doi.org/10.1088/0004-637X/795/1/75): refine
semicoherent candidate coordinates before coherent follow-up, since their
uncertainty can exceed the coherent peak's width. Its gamma-ray statistics and
search volume are different; the present scalar breakpoint step is an
implementation inference, not a reproduced algorithm or borrowed threshold.
[Lu et al. (September 2026 preprint), §III](https://arxiv.org/html/2609.12476v1)
similarly separate short coherent model accuracy from long frequency-track
combination, and show workload-dependent NUFFT savings against stroboscopic
resampling. That is a useful alternative when time-warp resampling dominates.
Here the retained block grid is already uniform and a native FFT covers CFO;
adding a NUFFT would not by itself repair candidate selection or establish a
speedup. Neither astrophysical result calibrates an HF propagation law.

The later long weak failure motivates a second distinction. Increasing
coherence in stages can broaden the initial optimization landscape before a
more selective coherent fit; [Bandopadhyay and Moore, §III](https://arxiv.org/html/2408.13170v2#S3)
track several peaks along such a ladder. Their empirical search thresholds are
not substitutes for this appraisal's exact, fully charged final mixture.
[Kejriwal, SPLIT v4 (12 November 2026), §IV.2](https://arxiv.org/html/2606.13728v4#S4.SS2)
also distinguishes robustness to modeling error from loss of optimal SNR and
warns that adding per-block inference parameters can make convergence much
slower. Here any staged objective would remain a fixed four-parameter proposal
with marginalized block gains, not a new per-block operating-state inference
pipeline or a claim of universal sensitivity gain.

Keep compact diagnostic receipts in ignored `local-evidence/` while they support
the unresolved phase-mismatch question and exact input/provenance checks. Remove
superseded successful raw receipts and setup outputs after their conclusion and
source binding are retained; preserve necessary failed controls. No duplicate
receiver implementation or full clean-IQ archive is required.

## First diagnostic outcome

Both frozen cases completed uncensored in 18.849 s, with exact equality to the
historical selected-IQ hashes. On the failed off-grid slot, the known physical
template has raw log likelihood 150.650, while the existing winning sinusoid
has 35.170 and retains only 0.2054 of the clean signal energy. On the original
recovered slot the corresponding values are 146.210, 125.365 and 0.8651.
The public-cell likelihood replay differs from its original calculation by at
most `1.26e-11`. These comparisons implicate template mismatch and establish
room for a bounded experiment; they do not supply an operational oracle.
Receipts: [physical diagnostics](local-evidence/hf-phase-physical-diagnostic/report.md).

## Frozen candidate specification

The candidate retains half the old 97-model prior and gives half to an implicit
Cartesian phase bank `phi(tau)=a*cos(2*pi*nu*tau)+b*sin(2*pi*nu*tau)`, centered on
the public observation. Its nonzero `(a,b)` pairs lie on a 0.25-radian lattice
inside radius 16 radians; `nu` spans 1/180 through 1/90 Hz with spacing at most
`0.5/(pi*16*T)`. Complete lattice cardinality determines uniform shape weights.
This interpolates the existing amplitude/period range rather than increasing
the maximum derivative; the old effective-template block geometry is retained.
It does not claim uniform global mismatch coverage near the disk boundary.

Accumulate deterministic block energies alongside existing complex correlations.
Use disjoint approximately eight-second segments, including the true partial
tail, to propose candidates. For segment energy `E_s`, variance `v` and complex
correlation `Q_s(f)`, the proposal sums
`-log(1+E_s/v)+|Q_s(f)|^2/[v*(v+E_s)]`. This independent-gain statistic is only a
proposal objective. Spectral tables cover the actual full CFO bound plus
16/90 Hz and interpolation guards, without wrapping or clipping at the CFO
center boundary. Exactly zero-energy segments contribute no information.

Select the common union of the best three old-family timing candidates and
the best three energy-normalized phase-agnostic segment candidates, at most
six public timing atoms. Both bits use that same timing subset. Rank every
retained timing first and report both rankings; exclusion is a proposal failure,
not evidence that the phase family cannot work. The coarse track bank uses
Cartesian step 4 radians, CFO spacing at most 0.05 Hz and frequency spacing
bounded by `0.05/[16*(1+pi*nu_max*T)]`. Retain at most three diverse basins,
including the exact public CFO endpoints in coarse proposals. Before coherent
ascent, refine each retained seed's CFO over one coarse step on either side,
clipped to public support. The interpolated segment objective is piecewise
linear in CFO: evaluate its segment-bin breakpoints and interval endpoints to
find that local maximum exactly. This is a bounded proposal operation, with
all extra table reads charged; it introduces no new evidence or prior mass.
Jointly refine shape and CFO with at most 16 iterations and four line-search
trials per iteration, then snap to the fixed bank. Score at most 24 distinct
shape IDs per timing, using the common union of bit proposals for both bits
and the full CFO FFT for each shape. Omitted shape/timing mass stays absent.

Report continuous proposal scores, snapped-cell loss, charged exact mixtures,
retained original mass, and actual lookup/FFT/direct-correlation counts. Bound
all work before IQ: at most 40 billion total FFT row-stages, two billion proposal
table lookups and the declared fixed direct-sweep quota, with all retained
correlations, energies, spectra and per-worker scratch charged. A resource stop
cannot reclaim prior weight. Use the existing native FFT and executor.

The first candidate stage has only two shadow observations: the failed off-grid
128-request/0 dB-Hz slot 2, seed 11001 (with physical diagnostic), and the earlier
moving 512-request/−6 dB-Hz slot 1, seed 10201. Both use full public timing and
refinement enabled. Exhaustive affine comparison is disabled in this stage so
the block geometry matches the operational supplement. Budget: 600 seconds
aggregate, 300 seconds each, eight million samples. Require historical selected
input hashes to match. Continue to the following complete-receiver cohort only
after these distinguish a useful refinement without losing the earlier weak
recovery; preserve any failed selection or optimization attempt.

## Frozen complete-receiver and mismatch cohort

The initial component run passed statistics, scenarios and receiver integration,
but the new refinement failed its noiseless off-grid acceptance after 89 checks.
It retained only 444.247 of an ideal 1029.06 raw log likelihood. Substituting the
coarse segment CFO for the whole-aperture FFT peak alone did not repair it.
Both failures are preserved as an initialization finding: the coarse CFO error
of 0.006836 Hz accumulates approximately 2.78 radians at the aperture edge.
An analytic reproduction matched the failing native optimizer and showed that
the public endpoint seed reaches 1028.377 with the same bounded optimizer,
despite starting with *lower* coherent evidence. This motivates the general
sub-bin segment refinement above, including an interior off-bin regression.
The analytic result is not a native acceptance result or a noisy-channel trial.
The generalized repair subsequently passed 107 native component assertions,
including the unchanged edge case and a distinct interior frequency, and all
17 appraisal CTest groups passed in 72.43 seconds. These are implementation
checks; the following campaigns remain separately scored evidence.

The conditional cohort contains 15 episodes, each capped at 300 seconds and
eight million samples, with a 3,600-second aggregate ceiling. All use the actual
native short payload `e`, configured calendar, raw white noise and complete
physical quiet-slot/codec endpoints. `baseline` here means the preceding
97-model experimental receiver; `refined` means the fixed new mixture.

- Original moving 128-request/0 dB-Hz refined, seed 10201.
- Original moving 512-request/−6 refined, seed 10201.
- Existing off-grid 128-request/0 and 128-request/−6 refined, seed 11001.
- Existing off-grid 512-request/−6 baseline/refined pair, seed 11001.
- Two fresh layer trajectories, each baseline/refined at 128-request/0:
  `(critical amplitude, period, phase)=(7000 Hz,149 s,1.13 rad)`, seed 12001,
  and `(8000 Hz,107 s,−0.61 rad)`, seed 12002.
- Compound baseline/refined pair at 128-request/0, seed 12001: two unresolved
  equal-power rays, the first using the 149-second layer above, the second
  using the same initial bulk delay with geometric delay motion of 75 ns,
  period 43 s and phase 0.6 rad, carrier-phase coupled. These are separate
  continuous per-ray laws; no composite energy renormalization or slot reset.
- CFO-edge refined at 128-request/0, seed 12004: the existing off-grid layer
  plus 0.98 Hz center offset, retaining the same public ±1 Hz center prior.
- Refined noise-only and wrong-key controls at 128-request/0, seed 12003.

The fresh and compound trajectories are constructed tests, not fitted events.
The compound's actual two-ray alternative is deliberately outside the public
single-tap signal model. The unchanged raw Gaussian null still permits the
same proper-template likelihoods; mismatch affects power. Record actual and
assumed path counts, received energy, cancellation and failures explicitly.
The exclusive delay/dispersion law within each analytical ray remains intact.

The weak short/long comparisons are necessary: a fixed eight-second proposal
window can become less effective at fixed total energy as duration increases.
If `M` independent equal-energy segments carry total coherent ratio `R`, the
known-track normalized-power mean shift is `R` against null standard deviation
roughly `sqrt(M)`, before searching tracks. A lost proposal therefore cannot be
called a fundamental channel limit. A bounded intermediate stage using longer
coherent segments after coarse phase compensation is the first remedy to test
if this failure occurs. The [staged-search literature](https://doi.org/10.1103/PhysRevD.110.103026)
motivates this tradeoff but supplies no HF calibration.

## Frozen shadow outcomes

The two shadow observations completed uncensored in 19.585 seconds with exact
historical selected-input hashes. Using operational block geometry, the failed
off-grid true-bit log evidence rises from 21.0376 for the old family to 130.0809
for the new family, or 129.3878 after their half/half mixture. The winning
snapped shape retains 0.96832 of clean physical energy; its independently
rerendered single-cell likelihood agrees within `4.63e-11`. Its raw value,
154.7713, exceeds the particular known-template noisy value 150.6496 because
the public template was selected on the noisy data; neither is an upper bound.
All selection penalties remain in the admitted mixture.

The original long weak observation retains true-bit evidence 93.3467 through
the old branch (94.0398 before its half weight). The new branch reaches only
15.2060: correct timing was retained, but its phase proposals converge to poor
coherent fits. This is a proposal/model-fitting limitation, not lack of received
energy or excessive final prior penalty. The shared old/new timing union also
protects the correct epoch when segment ranking alone favors a wrong epoch.
The new stage adds 1.94% and 3.14% to old FFT row work, respectively, plus
8.86/129.85 million table reads and 0.646/2.665 million direct rows. These are
logical work measurements, not complete-receiver throughput or GPU results.
The frozen complete-message cohort tests whether these conditional findings
survive bit/null decisions and physical completion.

Receipts: [two shadows](local-evidence/hf-phase-refinement-shadow/report.md).

## Long weak failure discriminator

The frozen receiver cohort's off-grid 512-request/−6 dB-Hz comparison failed
under both variants. Refinement recovered slot 0, but slot 1 remained quiet at
21.3206 against threshold 21.4934 and slot 2 was quiet at 1.3993; this caused
premature completion and an integrity rejection. This exposes the predeclared
long weak proposal concern rather than establishing a fundamental floor.

Before modifying the optimizer, freeze exactly two full-public-timing shadow
observations with physical diagnostics: slots 1 and 2 of that same scenario,
seed 11001, requested duration 512 seconds and −6 dB-Hz. Budget is 240 seconds
aggregate, 120 per episode and eight million samples. Slot 1 must match its
saved native selected-input hash; slot 2 must preserve the identical full-prefix
generation and channel configuration. Keep both noisy captures, priors and
failures fixed. Test whether physical evidence remains substantial and whether
the correct timing survives selection before authorizing a staged-coherence
proposal remedy. No new seed, power sweep or receiver truth input is involved.

Both diagnostics completed uncensored in 51.283 seconds. Physical-template
log likelihoods are 133.257 and 115.251, while the selected refined cells reach
50.725 and 30.387, retaining 0.3412 and 0.2253 of clean energy. Both retain the
correct epoch and the 1/2/3-ms arrival candidates. Slot 1 exactly matches native
selected hash `6866d5b2bd2f0a995808ce9311acf5b4bbcf7c718e490912c7659c53bf7344e9`.
This supplies the prerequisite for a staged proposal experiment.

## Frozen staged-coherence remedy

For public apertures at least 180 seconds (two shortest supported periods),
insert one approximately 32-second grouped objective using four of the existing
eight-second segments. Apply the full nonlinear trial phase at each block
center, then sum independent-gain likelihoods over groups using their actual
energies and partial final group. This changes proposals only. Use eight bounded
grouped iterations followed by eight of the existing whole-aperture iterations,
each with at most four backtracks. Retain three starts and 24 final shapes,
all original prior weights and the global work/memory caps. Short-aperture
behavior remains unchanged.

The grouped metric removes a separate constant phase within each group and
combines energy-weighted derivative covariances. Use diagonal scaling with
relative ridge 0.0625, variance floor `1e-8`, proposed RMS phase radius 0.25 rad
and maximum absolute step 4 in the existing normalized coordinates. Reusing
the whole-aperture absolute ridge would swamp the weak group-local frequency
curvature. Report grouped initial/final likelihood and steps separately; final
admission still uses one global gain and full CFO search. Bound 82 direct sweeps
per start, including both initializations, rather than the former 81.

First pass independent grouped sums/gradients and long synthetic component
checks, then rerun exactly the same two failed-slot physical shadows under the
new source identity. If these improve the charged evidence, run the frozen
six-episode complete-receiver cohort, with 1,800 seconds aggregate, 300 seconds
per episode and eight million samples: the original moving and off-grid weak
long cases; a baseline/refined weak long `hf-eikonal-holdout-b` pair with fresh
seed 13001; and long noise-only/wrong-key controls with seed 13002. All use
512-request seconds and −6 dB-Hz. A failed stage is retained and diagnosed;
neither a selected-slot improvement nor a favorable new seed replaces the
original complete-message failure.

The staged shadow manifest additionally repeats the earlier short off-grid
slot 2 as an exact inference-preservation check, for three episodes and a
300-second aggregate cap. On staged proposals `initial_log_lr` means the full
coherent score after the intermediate stage; grouped initial/final scores are
separate and must not be compared as the same objective.

The staged implementation passed 121 component assertions and all 17 appraisal
CTest groups (74.00 seconds). Three matched shadows then completed uncensored
in 45.473 seconds. The two long true-bit scores rise from 24.0931/4.1637 to
105.7873/88.6554, with clean coherence 0.98821/0.98045 and independent replay
agreement within `3e-14`. They retain the full
`log(39 * 3765636 * 4371) = 27.187736` cell penalty and half-family weights.
No old-family evidence changes. The short observation's input, both family
scores, mixture, selected cells and logical work are exactly preserved.
All actual work fits the public quotes; the staged long cases use about
3.80/3.79 billion total FFT row-stages, 97.38 million table reads and
2.69/2.45 million direct rows. [Staged shadows](local-evidence/hf-phase-staged-shadow/report.md).
The source is `9853f9d9248b023e27e5a5feb00bbaf823bfc428b9249cef13e3b612770d2f9b`;
its probe is `be5d276231cfd7e5966fecea9ba68e2f90367ab74a52c886243b5a98519a121c`.
These checks authorize the frozen complete-message cohort, not a communication
success or real-time-capacity claim by themselves.

All six receiver episodes subsequently completed uncensored in 561.865 seconds.
The original moving and previously failed off-grid weak long messages both
decoded correctly, with three correct DATA slots followed by two scored quiet
slots. The fresh 107-second trajectory at seed 13001 also completed correctly;
its matched old97 baseline classified all three transmitted slots as quiet.
Long noise-only and wrong-key controls produced no false DATA or accepted
messages. The three staged wanted episodes pass; these are descriptive pilots,
not a population success probability. [Complete receiver results](local-evidence/hf-phase-staged-receiver/report.md).

For the constructed off-grid case, reducing C/N0 by 6 dB and increasing actual
bit duration from 139.2 to 522 seconds (3.75 times) now preserves successful
communication. Source energy divided by noise variance is 0.94151 of the short
0-dB-Hz reference, a difference of about −0.26 dB. This demonstrates a repaired
near-equal-energy trend for this finite trajectory; it does not qualify a
universal sensitivity floor, absolute radiated power or superiority to another
mode.

## Resource-limit continuity

Completion review found a separate public-planning cliff: with 522.569-second
aperture, 1920 samples/s, ±10-Hz CFO and 13 timing atoms, the old97 branch fits
its 40-billion FFT row-stage cap (25,122,832,384). The combined FFT bound also
fits (28,115,009,536), but new direct work is 105,780,492 rows, exceeding its
100-million cap. Native predicate-only reproduction confirmed that enabling
refinement previously removed the otherwise supported old branch.

The corrected pre-IQ planner retains old97 at its fixed half weight and gives
the unexecuted new family zero evidence. It reports an explicit unavailable
reason and attempted public work quote; it never reallocates the missing prior
mass. Full-public timing geometry determines this choice before data-dependent
proposals. Workspace follows the admitted old path, and the ±1-Hz admitted path
is unchanged. The focused native suite passes 296 assertions, including this
boundary, worker counts 1/2, preserved admitted control and the actual shared
mixture calculation on old97 scores. Broad ±10-Hz DSP was not run.

This fallback applies to deterministic new-family planning/work bounds only.
Allocation failure, cancellation and runtime errors remain errors. The native
hook does not receive changing memory headroom, so no automatic memory-pressure
fallback is claimed. Explicit receiver limits and its ordinary admission remain
binding. The temporary predicate wrapper is superseded by native regression
checks and this concise finding; no duplicate planner or obsolete implementation
is retained.

Final validation passed all 17 appraisal CTest groups in 73.54 seconds, including
296 public-receiver and 121 refinement assertions plus 49 runner tests. The
same three shadows then completed in 57.949 seconds. Full/selected input hashes,
old-family outputs, old/new/combined bit scores, chosen timing and actual/quoted
logical work exactly match the pre-fallback staged receipts.
[Final parity receipts](local-evidence/hf-phase-final-shadow/report.md).
Final appraisal source:
`27d178dfb23d680b3245d73869b9cbc3288f50ba307f9ef083da53b087fbafa9`;
final probe:
`b414c696ab4ecafa6b0bfdff0a2bd05be2dab5be0e094d3ab7d798b13181f5f7`.
The six complete-message episodes retain their earlier `9853f9d9…` source and
`be5d2762…` probe identities; the exact admitted-path parity and current native
integration checks support that unchanged path without claiming a rerun of
those six episodes. Canonical native source is unchanged throughout:
`1749313fcabbf16527ac687c5cf4947acde3d00dca6bcd364b87d453b6d79a16`.

## Which theoretical trend applies

For a known deterministic sampled signal `s` in proper-complex white Gaussian
noise of variance `v`, let `R=||s||^2/v`. Its known-amplitude log likelihood is
`2*Re(s^H*y)/v-R`, with means `-R`/`+R` and variance `2R` under absence/presence.
At fixed false-positive probability `alpha` and detection probability `Pd`,
the conditional required ratio is
`R=0.5*(Phi_inverse(1-alpha)+Phi_inverse(Pd))^2`. This is a known-channel,
known-waveform comparison, not the receiver's composite-search error rate.

A different trend arises for genuinely unobserved Gaussian fading. With
noise-whitened signal covariance `K`, the divergence from noise is
`tr(K)-log(det(I+K))`, approximately `0.5*tr(K^2)` at small eigenvalues. For
`M` independent equal-energy blocks it becomes approximately `R^2/(2*M)`.
Thus constant information may require energy proportional to `sqrt(M)`;
with fixed coherence time, required duration can grow quadratically with the
inverse signal/noise-density ratio. This simple model is distinct from the
deterministic eikonal and from a universal capacity theorem. General
noncoherent-channel results depend on fading correlation and peak/average
power constraints, and effective coherence can itself depend on SNR.
[Sethuraman et al.](https://arxiv.org/abs/0712.2872),
[Bergel and Benedetto](https://arxiv.org/abs/1107.0639).

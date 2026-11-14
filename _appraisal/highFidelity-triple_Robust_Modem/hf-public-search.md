# Public phase-history search

This investigation follows the unresolved long-eikonal miss and measured-HF
phase mismatch in [HF validation](hf-validation.md). The former phase bank knew
the reference timing and had no frequency-search penalty. Its favorable scores
were motivation, not evidence of operational recovery. This continuation tests
what survives public acquisition uncertainty and matched affine controls.

## Fixed experiment and stopping rule

Frozen before the new receiver-scale outcomes, 13 November 2026:

- Use the same native source, channel, keyed `MappedBasis`, full canonical
  coarse search and snapped public timing grid. No transmitted bit, realized
  path, measured phase trace or oracle timing enters the search API.
- Local CPU first, `W=2*H`, one intensive test at a time. Maximum 600 seconds
  per episode, 3,600 seconds aggregate, 40 billion FFT row-stages per slot;
  ordinary process-memory admission includes input and all possible worker
  stacks. Exceeding a bound is censorship, not a reception failure or pass.
- Eleven base episodes: seed 10201 static/moving eikonal at requested 128 s,
  0 dB-Hz, slot 1; moving 64 s at 0 and moving 128 s at −6, also slot 1;
  seed 10501 measured Ding complex/amplitude/phase at +4, slot 0;
  seed 11001 moving 128 s at 0 noise-only, wrong-key and wanted, slot 1;
  seed 11001 off-grid eikonal holdout 128 s at 0, slot 1.
- At most two declared follow-ups: full timing at the original moving 128 s
  miss to distinguish candidate selection from model mismatch, and measured
  complex +8 if +4 remains below the public evidence reference. Retain every
  completed, failed or censored outcome. Do not enlarge this campaign to seek
  a favorable rate. These paired, selected cases are not independent random
  draws from the population of ionospheric days.
- The holdout is a constructed physical trajectory: critical-frequency
  variation ±8.5 kHz, period 137 s, initial phase .37 rad, with the original
  public ±1 Hz and 1–3 ms bounds. These parameters are absent from the detector.
  It does not establish calibration to a measured disturbed day.
- Consider complete-receiver promotion only if the original miss has correct
  evidence after all public and fixed branch penalties at bounded cost. The
  user separately approved the proposed generic two-file internal native hook.
  A shadow likelihood is never a DATA/quiet decision or successful message.

## Conditional complete-receiver stage

Predeclared before the public-search outcomes: if the original moving miss
survives the full search and projected branch penalty, use the approved native
hook at fixed weight 1/4. A separate campaign has ten episodes, 600 seconds per
episode and 3,000 seconds aggregate, bounded to eight million rendered samples.
The same compiled receiver handles public acquisition, scheduling, erasures,
native codec and two scored physical quiet slots.

The ten episodes are the original moving 128 s/0 dB-Hz pair (ordinary and
supplemented), supplemented static control, fresh moving and off-grid holdout,
supplemented moving 128 s/−6 dB-Hz, the moving 512 s/−6 dB-Hz pair (ordinary
and supplemented), and supplemented noise-only and wrong-key controls.
Use seed 10201 for original/static/weak comparisons and seed 11001 for fresh,
holdout and negative controls. The longer weak pair tests whether additional
physical energy can recover reception after the charged public search penalty;
no private reference or phase-bank retuning is allowed. Keep actual airtime,
transmit energy and full observation cost. A decoded message and a slot evidence
improvement are different endpoints. Selected slot-1 IQ/validity hashes must
match within each ordinary/supplemented pair.

Run this stage only after component and hook checks. Failure/censorship is an
outcome; it does not authorize unbounded search or extra seeds. Native candidate
telemetry excludes the exhaustive affine diagnostic comparator. Complete episode
wall/CPU cost is appraisal cost, not a qualified real-time capacity result or
an independent estimate of a rare false-output probability.

## Matched likelihoods and computation

### Native evidence-hook contract

The approved hook is confined to `src/triad_receiver_driver.hpp` and
`src/triad_receiver.cpp`. `SlotEvidenceSupplement` supplies a fixed prior weight,
a support predicate depending only on public configuration/slot geometry, a
positive workspace quote and a scorer. Worker admission, backlog and samples
cannot decide support. Zero weight disables it; an unsupported public shape
uses the ordinary path. An admitted supported shape must reserve its scratch or
fail admission, rather than restoring prior mass when resources are scarce.

The scorer borrows the existing public preparation, immutable IQ, receive key
and bounded shared executor. It returns two log e-values including its complete
alternative prior, excluding the equal bit prior and outer supplement weight.
At weight `w`, each bit uses `logadd(log(1-w)+old, log(w)+supplement)`.
The weight is reserved before any classifier; supplemental evidence starts at
minus infinity and is evaluated at most once, after the existing fallbacks
have not admitted DATA. Skipped work never restores its mass. Invalid values,
exceptions and cancellation cannot publish a successful result.

Any provisional supplemental admission goes through the existing coherent-line
gate. If a line is retained, the supplemental evidence is permanently removed;
ordinary and residual branches retain `1-w` without renormalization. Raw
supplemental evidence cannot enter a line-conditioned residual. Existing bit-gap,
line and quiet vetoes remain. Both workspace entry points charge input, receipts
and the maximum stage scratch including the supplement; its work is included
in native fine time. `StreamReceiverFixture::make_with_executor` connects it to
native admission, ordered commits, codecs and physical quiet completion.
This contract does not qualify the inherited interference policy or nonwhite
noise. Ordinary receiver entry points and application defaults are unchanged.

### Fixed hypothesis families

The three separately normalized families are constant frequency, affine
frequency drift and the unchanged 97-model sinusoidal phase bank. The latter
contains zero once and amplitudes 4/8/12/16 radians, periods 90/120/180 seconds,
and eight phase origins. Affine slopes span ±.013 Hz/s on a deterministic grid
whose nearest-cell phase error is at most .25 rad. This affine range is a
comparison assumption, not a calibrated ionospheric bound. The ±.013 range
was fixed during pre-outcome review to cover the sinusoidal dictionary’s
maximum local curvature (about .01241 Hz/s); this prevents a short-arc
comparison from excluding an otherwise equivalent affine trajectory. Its exhaustive cost
is a diagnostic expense; it is not the proposed operational supplement.

Frequency centers cover the full legacy plus atmospheric prior. The symmetric
FFT lattice rounds outward and reports its actual support and cardinality;
spacing is at most `min(.01 Hz,1/(4T))`. Timing mass includes all 13 public
epochs, all arrival cells and all configured clock cells. The selected timing
union is deduplicated and keeps its original full-grid weights. Unexamined
timing atoms contribute zero evidence, without renormalization.

For each timing atom, native templates for both bits are rendered once. Store
`q[b,j] = sum(conj(x[b,n])*y[n])` in each block and the full template energy
`E[b]`. Rotate these shared correlations by a fixed phase history and FFT over
block centers. Each effective template has a constant phase within a block;
the incomplete final block retains its nominal center but has no fabricated
samples or energy. The norm and noise variance are therefore exact for this
effective alternative. A derivative bound limits its phase deviation from the
samplewise model; deterministic tests separately check direct effective sums
and the ideal-to-effective coherence bound.

For known proper-complex white Gaussian variance `v` and one global
`CN(0,1)` gain, each effective-template log likelihood ratio is
`-log1p(E/v) + abs(r)^2/(v*(v+E))`. Equal model/CFO priors and the full timing
prior are charged before the two bits are averaged for presence. Maxima and
best cells are diagnostics only. No maximum is substituted for a mixture.

Under that fully specified null, a proper likelihood ratio has expectation
one. A fixed proper mixture retains this property; a data-selected subset
with unchanged nonnegative weights is pointwise no larger than the complete
mixture. Thus Markov's bound applies without requiring infeasible Monte Carlo
at the nominal rare-event threshold. This does **not** cover estimated noise,
unmodeled colored/impulsive noise, frontend transformations or the complete
canonical interference/quiet policy. Prior mismatch under the correct null
can reduce detection power without invalidating this bound.
See [Shafer et al.](https://arxiv.org/abs/0912.4269) and
[Vovk and Wang](https://arxiv.org/abs/1912.06116).
[Baluev (2022)](https://arxiv.org/abs/2203.08476) finds nearly identical
frequentist/Bayesian detection efficiency in its period-search example and
power loss from misspecified priors. Its calibration warning concerns practical
threshold efficiency, not a contradiction of the conservative Markov bound.

## Research reconciliation and limits

The [existing evidence](hf-research.md) and
[Fable skywave review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md)
distinguish phase path from group path, selected quiet measurements from
storm-wide distributions, and scalar narrowband replay from a full propagation
model. [Recent Alaska observations](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full)
support that distinction, but their averaging cannot calibrate subsecond
sample paths. The analytic parabolic layer isolates one mechanism and omits
magnetoionic splitting, collisional absorption and strong-scattering caustics.

The 41-second Ding record is used once on its declared sampling grid, without
stretching or looping. CFO and affine controls are necessary before attributing
a short excerpt's improvement specifically to nonlinear history. A winning
90-second sinusoid on a 19-second excerpt is a local approximation, not an
inferred ionospheric period. Actual [FT8/FT4 processing](https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf)
already exploits coherent information and substantial decoding computation;
this experiment makes no orders-of-magnitude superiority claim.

[Coherent track-before-detect research, revised in 2025](https://arxiv.org/abs/1709.00310v4)
supports joint trajectory/gain inference across coherent intervals in simulated
radar examples. Its section V-A computes its comparison CFAR threshold using
true reflectivities and trajectory; that is not evidence that a practical
data-fitted trajectory preserves this experiment’s fixed-mixture calibration.
It does not establish an HF communication advantage or remove
the need for an adequate evolution model and reference-clock uncertainty.

If repeated phase FFTs dominate, [Jacobi–Anger expansions](https://dlmf.nist.gov/10.12)
suggest sharing shifted frequency correlations across sinusoidal hypotheses.
Truncation, grid interpolation and likelihood normalization require separate
qualification. This alternative must preserve all public mass and cannot
justify unmeasured throughput claims.

The off-grid failure motivates a better-conditioned phase bank, not a claim
that a huge uniform grid is unavoidable. A concrete next candidate is
`phi(tau)=a*cos(2*pi*nu*tau)+b*sin(2*pi*nu*tau)`, with `tau` centered on the
public aperture. Cartesian coefficients avoid redundant angle coordinates near
zero amplitude. For local grid placement, the near-match loss can project a
phase difference off constant and linear terms, weighted by deterministic
template energies. The global complex gain already absorbs constant phase;
linear phase overlaps CFO. This is a local approximation, not a global covering
proof. The exact score must still retain the bounded public CFO support and all
its mass; projection cannot silently remove frequency uncertainty.

[Owen's template-spacing analysis](https://arxiv.org/abs/gr-qc/9511032) supplies
a mathematical analogue for mismatch-aware placement under finite compute.
[Prix's metric analysis](https://arxiv.org/abs/gr-qc/0606088) also tests where
local approximations fail. These are gravitational-wave search methods, not
HF field validation. Here they suggest bounded proposals from the existing
complex block correlations, followed by exact evaluation of nearby cells in a
predeclared bank, retaining the original 97-model mass. Free fitted centers or
selected-cell prior renormalization would not preserve the current likelihood
argument; noisy phase unwrapping is also unnecessary for complex-correlation
proposals. A conservative uniform grid estimate therefore
does not establish an unavoidable computational or sensitivity floor. This
candidate remains unimplemented and unqualified.

Keep the compact public-search and receiver receipts in ignored
`local-evidence/hf-public-*/` for the current off-grid sensitivity question,
the contradictory measured-record control, exact inference/input parity and
the claimed rendering-cost improvement. No full IQ captures are retained for
these campaigns. The successful operational transition is recorded in
[HF validation](hf-validation.md); obsolete proposal/setup outputs were removed.
Once a superseding source-bound check replaces a successful control or timing
comparison, retain its concise finding/provenance and remove the redundant raw
receipt. Failed controls remain until the failure is resolved or their necessary
evidence has been distilled without losing the applicable conditions.

## Adaptive timing diagnostic, declared after the eleven shadow cases

Both original and off-grid moving cases retained 13 of 39 timing atoms, all
at the 1 ms lower boundary. The public grid also contains 2 and 3 ms. Before
attributing the off-grid miss to phase-family coverage, run the unchanged
97-model detector with the entire timing grid on those two exact captures.
The original full-grid control and measured +8 control were already declared;
the off-grid full-grid replay is one additional, explicitly adaptive causal
investigation. It is not an extra confirmation seed and does not revise the
frozen eleven-episode campaign. Freeze these three episodes to 120 seconds
aggregate / 60 seconds each, with no further waveform or dictionary changes.
Compare exact slot hashes, timing mass, per-family evidence and execution cost.

The full-grid causal results raised the original nonlinear score from 93.233 to
109.225 and the off-grid score from 17.872 to 22.528. Before starting the ten
complete-receiver episodes, the candidate adopted the small-grid closure rule:
when the public arrival grid has at most three cells, score every cell at each
already retained epoch/clock key. The worst-case quote covers that expansion
before IQ is observed. This is a public geometry rule, not the evaluator's
2.099 ms path delay; the 97 phase models and all priors remain fixed. Shadow
selected mode remains unchanged. The small raw off-grid margin is not assumed
to survive the operational branch penalties.

## Shared-preparation cost check

After the ten complete-receiver episodes, reuse immutable phase rotations and
FFT twiddles once per public slot. This changes neither the 97-model bank,
arrival rule, prior weights nor stage order. Before execution, freeze three
matched repeats: original moving 128-request/0 dB-Hz, moving 512-request/−6,
and noise-only 128-request/0, with the original seeds and the enabled candidate.
Budget: 900 seconds aggregate, 600 seconds per episode, eight million samples.
Require identical complete input/validity hashes, decisions, bits, completion
and logical hypothesis/work counts; inspect evidence differences against the
unchanged scalar reference. Report memory and elapsed cost without treating a
single timing pair as a population speedup or real-time-capacity result.

Actual waveform rounding is 139.2 and 522.0 seconds per bit, a 3.75-fold physical
increase despite fourfold requested durations. At −6 dB-Hz the longer message's
transmit energy/noise is 394.601, versus 419.117 for the shorter 0 dB-Hz case;
they are close but are not an exactly equal-energy experiment.

## Failed final-slot discriminator

Completion review identified that the operational off-grid failure is slot 2,
whereas the earlier shadow comparison covered slot 1. Before ending this
continuation, run exactly one existing full-timing `hf-search` observation on
slot 2, seed 11001, requested 128 s, 0 dB-Hz, unchanged off-grid trajectory.
Budget 30 seconds and four million samples. Compare CFO, affine and 97-model
phase evidence and their best cells. The original operational receipt hashes
slot 1, so it cannot supply a direct slot-2 hash comparison: record this as a
reconstruction with the same frozen source, channel, seed and public geometry,
and retain its own slot-2 digest. No new waveform or fitted model is introduced.
This adaptive causal check is not a confirmation trial.

## Native paired-rendering discriminator

The shared-preparation repeats identified exact RRC template rendering as the
dominant callback cost. `MappedBasis` deliberately falls back to direct rendering
for RRC; its epoch source lock serializes different arrival candidates. Before
another complete episode, compare a contained adapter of the existing native
`PatternSource::render_bank` against those canonical samples. Two private sources
per timing lane share pulse/NCO work for the two bits. Gate the adapter to the
existing RRC, single zero-delay/zero-Doppler tap, zero clock/CFO and equal sample
rates. Require exact sample-coordinate equality throughout each tile, preserve
canonical IF/gain arithmetic, and otherwise use the existing `MappedBasis`.
Charge private sources and bank scratch in addition to the retained fallback.

First check fractional arrivals, positive/negative epoch offsets, waveform
boundaries, nonunit gain, partial tiles and forced coordinate fallback, then a
small deterministic rendering timing comparison. If parity passes and timing
supports a useful benefit, run only the original moving 128-request/0 dB-Hz and
long weak 512-request/−6 dB-Hz candidate episodes, with the original seed 10201.
Budget 600 seconds aggregate, 300 seconds per episode, eight million samples.
Require exact input hashes, decisions, bit evidence and logical likelihood work
against the shared-preparation receipts. Report actual paired/fallback fractions,
extra admitted memory and whole-episode cost. Retain the adapter only for a
useful measured gain on a challenging matched observation; otherwise remove it
and retain the negative conclusion. No new phase family, prior or stage order
is introduced, and these paired timings do not qualify sustained capacity.

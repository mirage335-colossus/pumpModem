# Correlated-gain presence inference on retained HF failures

This bounded appraisal tests whether public correlated-gain inference can
recover evidence lost by the existing receiver on the numerically qualified
plasma-lens and separated-screen failures. The native receiver's varying,
coherent-gain and drift fallbacks already ran on those misses. Repeating those
same branches does not test a new remedy. All new work remains in this
appraisal; production defaults and physical completion are unchanged.

## Research and the relevant theoretical comparisons

The reference collection distinguishes stationary fits, measured records and
uncertain environmental realizations in
[Fable 33](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md),
[Astra 60](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md)
and [Fable 70](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/70-implementation-options.md).
The receiver family here is a deliberately finite alternative, not a claim
that an unusual ionosphere follows a stationary Gaussian law.

[Guimarães et al.](https://www-public.imtbs-tsp.eu/~desbouvr/papiers-web/TW-Dec-08-1964.pdf)
demonstrate conditional Gaussian marginalization in blind doubly selective
channel inference, while retaining the cost of uncertain discrete histories.
[Arikan et al.](https://doi.org/10.1029/2003RS002879) give an HF Kalman example
with training and simulated channel conditions; that setup does not validate
this modem's unknown long history. In measured GNSS scintillation,
[Susi et al.](https://link.springer.com/article/10.1007/s10291-016-0597-y)
show why incorrect fixed dynamics can undermine tracking. GNSS tracking is
not an HF complete-message qualification. Direct HF observations by
[Aricoche et al. (2026)](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full)
also report fading faster than their inversion resolves. Their processing
intervals cannot calibrate the subsecond laws selected below.

Three comparisons must remain separate:

1. With a deterministic complex mean known exactly and white noise variance
   `v = E|n|²`, expected presence log likelihood grows as `||mean||²/v`.
   The retained private known-mean diagnostics establish useful information
   in particular samples under that favorable knowledge. They are not a
   demonstrated blind receiver sensitivity.
2. With a fixed waveform and an unknown zero-mean proper-complex Gaussian
   gain, the signal changes the covariance from `Cn` to `Cn + Q`. For a
   known covariance law the exact expected log likelihood under presence is
   `sum(gamma - log1p(gamma))`, where `gamma` are eigenvalues of the whitened
   signal covariance. At small per-mode SNR this is approximately
   `sum(gamma²)/2`. Correlation affects those eigenvalues and the evidence
   retained over time. This is a finite-record check for a stipulated law,
   not a universal ionospheric sensitivity floor.
3. Asymptotic coding capacity depends on input constraints as well as
   channel knowledge. [Sethuraman et al. (2009)](https://doi.org/10.1109/TIT.2009.2012995)
   analyze stationary correlated noncoherent Rayleigh channels with explicit
   peak and average power limits. Their quadratic low-SNR result under a
   fixed peak-to-average ratio does not apply universally: different peak
   constraints can permit linear scaling. Neither that result nor the
   conditional known-mean reference predicts this fixed modem's per-frame
   success curve without additional assumptions.

Search costs have two distinct parts: actual compute/workspace and the
statistical cost of uncertain public hypotheses. A fixed normalized mixture
of exact likelihood ratios has unit null expectation under its stated null.
Replacing that mixture with the largest fitted score removes its selection
penalty. Unknown, estimated or nonwhite noise requires separate qualification;
the white-noise calculation is not an arbitrary-noise guarantee.

## Frozen first discriminator

The new native shadow uses the same canonical paired keyed basis as the
receiver. It covers every one of the 559 public timing atoms and a closed
CFO interval of ±5 Hz at 1/32 Hz spacing, with trapezoidal endpoint weights.
It sums both bit hypotheses and all twelve equally weighted gain laws:
e-fold times 0.5, 2 and 8 seconds, each at powers 1/256, 1/32, 1/4 and 2.
Initialization is stationary and circular (`mean=0`, `variance=power`).
There is no fitted law, best-cell substitution, truth-selected timing or
physical trajectory supplied to the detector.

Gain is constant within each 960-sample (0.5-second) block. CFO phase is
constant within each 30-sample atom, referenced to its center, with a global
phase reference across blocks. At the ±5 Hz endpoints the atom phase
approximation is bounded by approximately 0.2454 radians; its likelihood
is exact for this finite template model, not for continuously rotating CFO.
The declared effective model must remain distinct from the physical renderer.
Zero-energy blocks still advance physical time. All deterministic covariance
and logarithm coefficients may be reused across CFOs only within the same
timing, bit and law.

Required numerical checks precede execution: independent dense complex
covariance likelihood; partial and zero-energy blocks; independent-gain and
coherent limits; canonical single-block agreement; global phase invariance;
FFT versus direct sums at −5, 0 and +5 Hz including partial final atoms and
inter-block phase. The FFT input tail is zero and its requested prefix counts
output bins. The shared executor retains the full available worker budget,
checked workspace admission and cancellation.

The first input is exactly the retained lens-holdout `B`, 0 dB-Hz, slot 2,
requested 64-second bits, seeds 69002/79002. Its complete observation has
134,921 samples. The quoted workload is 150,841,678 paired basis/sample
products, 3,551,268,864 conservative full FFT row-stages and 607,221,576
scalar innovations. The initial admitted caps are 160 million, 3.8 billion
and 650 million respectively, 3 million capture samples and 90 seconds wall
time. These are ceilings, not a runtime forecast. A deadline is censored
cost evidence, not a completed communications failure.

The first shadow reports normalized total evidence and the contributions of
native-selected and complementary timing cells without renormalizing either
subset. Existing presence and bit-separation references remain 21.4933786883
and `log(199) = 5.2933048247` nats. Native varying slots reserve half of their
model mass for line nuisance even when no line is retained. Relative to the
archived **final** native evidence, the candidate composition is therefore
`logadd(log(.75) + native_final, log(.125) + new_raw)`. A standalone candidate
must pay `log(8)`, not `log(4)`. Retained-line behavior must discard the
supplement without restoring its mass.

The standalone candidate's bit gap is only a screening statistic. Even when
its charged presence clears the threshold, the retained native branch can
favor the opposite bit and reduce the combined gap. An integrated decision
must recompute both combined per-bit evidences with the fixed weights before
testing separation; a new-only gap cannot establish that result.

A useful fully charged crossing warrants the frozen MPS slot discriminator
and then complete wanted, fresh holdout, noise and wrong-key observations.
A selected-cell crossing alone does not. Numerical agreement, informative
shadow evidence, successful complete messages and acceptable service cost
are separate gates. No receiver adoption is claimed by this protocol.

## Exact-input shadow results

The numerical covariance, canonical single-block, phase, CFO and work-admission
witnesses passed. The existing public-receiver check also passed; the compiled
integration group passed in 65.38 seconds. These tests preceded the shadows.

Both shadows reproduce the retained selected observation, including validity,
and the qualified physical table. All 559 timing atoms, 321 CFO cells and
twelve laws were evaluated with 24 workers. The native selected set contained
30 timings for the lens and 29 for the separated-screen case.

| Exact failed observation | New raw bit evidence (0, 1) | Presence after actual no-line mixture | Required presence | Mixed bit gap | Result |
| --- | --- | ---: | ---: | ---: | --- |
| Lens holdout `B`, 0 dB-Hz, slot 2 | −0.235890, 40.550693 | 37.778104 | 21.493379 | 40.772046 | Correct bit 1 clears the component comparison |
| Primary separated screens `A`, +10 dB-Hz, slot 10 | 21.573018, −1.008710 | 18.800429 | 21.493379 | 22.55347 | Correct bit 0 remains below presence threshold |

The original native lens evidence was approximately −6.25/−6.18. Combining
those exact final scores with the new branch at the specified weights gives
−2.300795/38.471251. The actual integrated receiver must still execute its
line handling and final classifier; this arithmetic does not establish a
delivered message.

In the lens, the native-selected timing subset contributes only −3.30/−3.22
to the new model; the complementary subset supplies essentially all the
correct-bit evidence. In the separated-screen case the selected subset's
correct-bit contribution is −1.78, while the complement contributes 21.57.
Thus coverage beyond retained timing proposals is a demonstrated contributor
within this model. These comparisons do not separately identify how much
of the improvement comes from timing coverage versus the new gain law.

For the separated-screen case, the best of the twelve individually evaluated
laws is the 8-second, 1/32-power law. Even assigning it all law-prior mass,
while retaining the full timing/CFO/equal-bit and one-eighth family charges,
gives at most 21.28451 nats of presence, below 21.493379. Any normalized
reweighting of these same laws is bounded by that maximum. This rules out a
same-law reweighting repair of this observation; it does not rule out different
dynamics, delay support, evidence or additional received energy. The original
miss is retained without tuning the family after observing it.

Model scoring took 5.102 seconds for the lens and 6.444 seconds for the
separated-screen case. Both executed 150,841,678 paired sample products,
157,638 FFT-prefix calls and 607,221,576 innovations, with no mapped-basis
fallback. The declared additional diagnostic workspace was 188,428,648 bytes
(179.70 MiB); application footprint, worker stacks and the earlier coarse
stage are separately charged. Summed worker elapsed times show basis rendering
dominates (106.72/137.93 seconds), followed by correlation/projection
(10.27/10.34) and innovations (3.98/4.18). Those overlapping intervals are
neither CPU time nor complete-service wall time. Total native invocation times
were 5.902 and 17.083 seconds; the latter includes 8.603 seconds of physical
table qualification. These are local CPU observations, not capacity or GPU
qualification.

The first lens runner check initially rejected a valid prefix receipt because
it incorrectly required the complete planned source count for a single-slot
shadow. The causal renderer had correctly consumed only the rendered prefix.
The narrow correction requires exactly `min(horizon, source_stop)` for these
shadows and preserves full-source equality for complete receives. Regression
mutations check falsely claimed full-source counts and invalid prefix/hash
extents. The original failed report remains intact; the
[offline validation record](local-evidence/hf-lens-correlated-gain-shadow/offline-validation.json)
binds the unchanged raw receipt to the corrected validator. No DSP was rerun
to replace that report. The subsequent
[separated-screen shadow](local-evidence/hf-phase-screens-correlated-gain-shadow/report.md)
completed strict validation on its first execution.

The shadow binaries bind to appraisal source digests
`f16856335105698f91de6cd39b992b9d27d01740fff061510c63f67db8fa25bd`
and `b242408d6da2e3f08ac80e21d4ccc474b4ca41a56364b3d7d2318cbce3bad4ab`
respectively, with unchanged canonical native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
Retain both raw shadows and the failed/original validation record through
the integrated comparison and investigation of the unclosed MPS boundary.
When superseded, preserve the exact-input, prior, negative and cost findings
here and remove redundant raw outputs rather than accumulating campaigns.

## Bounded complete-receiver comparison

The successful lens shadow proceeds to an opt-in appraisal supplement using
the previously approved generic native hook. A single typed scorer serves
both shadow and receiver paths. The shadow wrapper performs its own coarse
search; the receiver path borrows the existing immutable observation,
prepared coordinates, executor and admitted scratch. It performs no repeated
coarse acquisition and creates no second worker pool. Support is determined
by public geometry, not IQ or available memory. Resource failure cannot
restore reserved model mass. Production source and default policy remain
unchanged.

The native hook retains fixed quarter weight before its existing line
handling, and evaluates the entire public timing bank on every executed
fallback. Native DATA admissions may legitimately skip the fallback. Explicit
retained-line handling also remains intact. Complete, all-valid non-DATA
slots with fine candidates and no retained-line exception must have an
execution marker and one corresponding diagnostic record; a missing failed
fallback is not acceptable receipt coverage. Typed diagnostic retention and
the larger final JSON are bounded independently of scoring, with the existing
64 KiB per-public-slot reporting tier reserved for these runs.

The [nine-row campaign](campaigns/hf-correlated-gain-receiver.json) was frozen
after observing the two shadows, before any integrated receiver result:

- Exact lens holdout `B`, 0 dB-Hz, with chunk size 257 instead of the shadow's
  default. Require matching full/selected input hashes and raw branch evidence,
  correct complete `B` and two scored quiet slots.
- Fresh primary-lens `C` baseline/candidate pair, seeds 71003/81003. This
  changes payload/input and noise on an existing field, not the physical field.
- Ambient lens-off `A` regression, plus fresh noise and bright wrong-key
  controls with the same complete public calendar.
- Original separated-screen holdout `B`, retaining the incorrect-space
  counterexample rather than deleting it from the candidate's support domain.
- One prospectively fixed +11 dB-Hz primary separated-screen baseline/candidate
  pair with the original `A` and seeds. The +10 shadow remains a failed gate.

Each row has 300 seconds, the aggregate cap is 2,700 seconds and the sample
cap is 3,072,000. Helper and FEC remain off and the zero-clock ±5 Hz scope is
unchanged. The +11 pair tests whether additional energy and the fixed model
produce a real delivery advantage; the 2.69-nat deficit does not predict that
outcome. If both receivers recover, no delivery advantage at that point is
established. If both fail, retain that result without further SNR steps or
law fitting. Any wrong DATA, incorrectly accepted payload, negative-control
failure or resource censor remains a separate failed gate.

The first new lane-invariance test failed its fixture-size assertion before
running its lane or lease comparisons: it incorrectly assumed that a short
requested duration would produce fewer than 6,000 samples at 1,920 Hz. The
canonical waveform retains a minimum tile duration. The repaired component
fixture explicitly takes a 1,949-row aperture—two full gain blocks plus a
29-sample final atom—and retains nonzero-evidence, one-versus-three-lane and
undersized-lease checks. It is not a shortened complete-message test. The
[initial failed test log](local-evidence/hf-integrity-preflight/integrated-focused-tests.txt)
is preserved; the earlier covariance and exact-input shadow results are
unaffected by this fixture setup error.

### Complete-message outcomes

All nine rows completed and passed strict receipt validation in 556.35 seconds.
There were no execution censors. The communication outcomes remain separate:
five of seven wanted rows delivered correctly, one accepted incorrect content
and one delivered nothing. Both negative controls stayed quiet.

| Fixed observation | Ordinary receiver | Correlated candidate | Candidate native wall time |
| --- | --- | --- | ---: |
| Exact lens holdout `B`, 0 dB-Hz | Misses slot 2; no message (archived) | Complete correct `B` | 37.27 s |
| Fresh primary lens `C`, 0 dB-Hz | Complete correct `C`, 22.40 s | Complete correct `C` | 34.09 s |
| Ambient lens-off `A`, 0 dB-Hz | Complete correct `A` (archived) | Complete correct `A` | 33.79 s |
| Lens noise control | — | 15 quiet slots, no DATA or message | 123.49 s |
| Bright lens wrong key, +10 dB-Hz | — | 15 quiet slots, no DATA or message | 123.40 s |
| Exact separated-screen holdout `B`, 0 dB-Hz | Accepts wrong space (archived) | Still accepts wrong space | 97.16 s |
| Primary separated screens `A`, +11 dB-Hz | Misses slot 10; no message, 33.85 s | Complete correct `A` | 49.65 s |

The exact lens comparison reproduces the entire input/validity hash
`80ff806e6e6dab7d3ef1311c725c4261435108af56a2d801959d5e3d7f5b2b42`
and the selected slot-2 hash
`c087a7e77465c07b674cd5cb97f8340645241dbc5af83912f554f29717fe03a8`
despite changing source chunk size to 257. Its raw branch evidence is
bit-for-bit identical to the standalone shadow. The actual native decision
matches the independently reconstructed fixed-mass mixture, restores bit 2
and delivers `[66]` with no erasures, wrong bits or premature completion.
Trailing quiet slots are 13 and 14. There is no change in transmitted energy,
bit duration, channel table or receiver noise in this repair.

The +11 pair also matches full input, calendar, channel table and clean
energies. The ordinary receiver's missed slot 10 scores −3.98672 nats of
presence. The new model supplies bit evidence 34.167036/−1.105297; the actual
native mixture has presence 31.394447 and bit separation 35.242926. It
restores that slot and delivers exact `A`, again ending on quiet 13–14.
This is a demonstrated delivery advantage at +11 dB-Hz. The +10 shadow's
failed gate is not reclassified, and the older ordinary-receiver +20 success
does not establish a 9 dB sensitivity gain: the intervening ordinary-receiver
boundary has not been measured.

The consequential failure survives on exact input: the separated-screen `B`
candidate still calls active slots 0, 1, 2, 3, 5, 6, 10 and 11 quiet, emits
two premature completions and accepts space `[32]` over slots `[7,12)`.
Every admitted DATA bit is correct. All eight failed active slots and both
trailing quiet slots execute the new fallback. Thus this failure is not
hidden skipped work, and a stronger presence model does not establish
unprotected message integrity. The cohort-wide accepted-content gate fails;
there is no general promotion or change to production defaults.

All 15 fallbacks execute in each negative control. Their substantially larger
cost than wanted reception is reported rather than inferred from the easy
path. Every row keeps 24 receiver workers on 12 logical CPUs; observed peak
worker use is 24. The largest reported receiver reservation is 3,185,813,297
bytes, with a 188,504,296-byte candidate slot quote. These bounds and whole
invocation times do not constitute a sustained incoming-stream capacity
measurement.

The [campaign report](local-evidence/hf-correlated-gain-receiver/report.md)
and [independent receipt audit](local-evidence/hf-correlated-gain-receiver/integration-audit.txt)
retain all raw identities, matching predicates, actual final score mixtures,
work, memory, decoded bytes and physical tails. This cohort binds to appraisal
source `463b2b0f687a4e5c97dfb9af4588fd325c0acf97de1a8ed24f4996c2b54af6a7`
and the unchanged canonical digest above. All 34 appraisal CTest groups passed
in 89.13 seconds before the campaign; the corrected component lane test took
0.02 seconds. The checked local host remains the Ryzen 5 PRO 5650U with 12
logical CPUs and full affinity recorded in the
[host receipt](local-evidence/hf-wave-host.json). No RunPod instance was used.
Keep these as the current
repair and unresolved-fragment evidence; remove superseded replicas only
after their exact comparisons are no longer needed.

## Duration admission follow-up

The initial 160-million-product pilot cap rejects an existing 139.2-second
lens aperture, even though the measured 69.6-second model scoring takes only
5–6 seconds. That rejection is a pilot resource limit, not an established
processor ceiling. The bounded qualification doubles the limits to
320 million paired products, 7.6 billion conservative FFT row-stages and
1.3 billion innovations, while preserving every law, public hypothesis,
normalization weight, threshold and the full worker budget.

The archived long holdout has 4,300,477 capture samples, 15 public slots and a
largest aperture of 268,553 samples. Its predeclared model quote is
300,242,254 products, 7,052,165,120 FFT row-stages and 1,205,830,080
innovations. A cheap full-calendar shape/workspace quote precedes rendering;
actual native leases still enforce the joint memory budget. This is a bound
on the configured test capture, not use of a received packet length to choose
DSP or physical completion. Boundary rejection must pass before reception.

The frozen comparison is the original `B`, requested 128 seconds and
−3 dB-Hz, seeds 69002/79002, plus a full-calendar noise control with noise
seed 81006. Each receives 600 seconds, with 1,200 seconds aggregate and
5 million samples. The ordinary archived receiver already delivers this
longer case; the new trial tests preservation and duration scaling, not a
new gain claim or a refitted trajectory. The wanted full input hash,
calendar, table and clean energies must match the archive. A selected slot-2
digest is newly recorded; no such digest was requested in the original long
trial, so only its full capture identity supplies that historical comparison.

### Duration results and resource admission

Both rows in the [frozen duration campaign](campaigns/hf-correlated-gain-duration.json)
completed and passed strict validation in 304.02 seconds. The wanted capture
delivers exact `B`, with all thirteen active bits correct, no erasures or
premature completion, and scored quiet slots 13–14. The full input, calendar,
physical table and clean energies match the archived ordinary-receiver
capture. The supplement runs only on the two trailing quiet slots: this
preserves an already successful long-duration point rather than demonstrating
an additional bit repair. The noise capture evaluates the same fifteen-slot
calendar and accepts neither DATA nor a message.

The wanted native invocation takes 66.56 seconds, compared with 46.83 seconds
for the archived ordinary receiver. The noise invocation takes 237.21 seconds
and executes all fifteen complete fallbacks: 4,503,548,842 paired products
and 18,087,451,200 scalar innovations. Its peak reported reservation is
3,289,728,897 bytes. This unsuccessful-search cost is retained separately from
the wanted path's two fallbacks. The long/short incident energy-to-noise
ratio is 1.00035270 and the corresponding received ratio is 0.98580453.
These describe this particular duration pair; fixed total energy need not
produce monotone performance when a longer transmission exposes additional
unknown fading modes. The normalized candidate does not yet establish that
its sensitivity follows an information-theoretic bound.

The pre-rendering quote examines all fifteen configured public apertures at
the full 24-worker budget. Its largest additional workspace is 189,218,200
bytes. The wanted receiver's peak reported reservation is 3,269,830,529 bytes,
and observed peak worker use is 24. Unit checks admit the last 286,225-sample
aperture within the fixed work envelope and reject 286,226 samples and
`UINT64_MAX` without overflow. A requested 192-second complete configuration
is rejected before constructing a physical channel. A separate one-sample
capture-budget preflight records the admissible long geometry, then stops
before rendering. These are explicit resource limits, not quiet decisions or
communications failures, and neither the quotes nor invocation times qualify
sustained receiver capacity.

The original planning-only command with the receive-only candidate switch
was rejected by its existing mode guard; it did not exercise the geometry
preflight. The corrected receive-mode budget probe above supplies that check.
The [preflight and affected-test records](local-evidence/hf-duration-preflight/)
retain this distinction. Geometry checks pass in 0.03 seconds; the three
affected integration, runner and waveform-receipt groups pass in 60.19 seconds,
including 119 runner and 13 waveform-receipt checks. The prior full 34-group result remains applicable
to the unchanged parts of the suite.

The [duration report](local-evidence/hf-correlated-gain-duration/report.md)
and [independent audit](local-evidence/hf-correlated-gain-duration/duration-audit.txt)
bind these observations to appraisal source
`d6260db20cec9a95a4c2507e6d5619ba6cdd3b8378575bc0a03deaa0aca289f0`
and the unchanged canonical digest above. Retain the exact long capture and
negative-control receipts while the duration envelope or candidate admission
policy is under investigation; preserve their comparisons here and remove
redundant raw outputs when superseded. This closes the artificial admission
cliff identified in the pilot. It does not close the separated-screen
incorrect-space failure or qualify a general unusual-day HF receiver.

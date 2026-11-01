# Three-carrier v3: state-likelihood and path-diversity study

This report records focused development and holdout evidence for the isolated
v3 three-carrier experiment as of 1 November 2026. The format, channel contract,
receiver assumptions and conditional benchmarks are described in the
[v3 design guide](../../../triad-v3.md). This work does not integrate with the
ordinary Robust receiver or qualify a radio, antenna, oscillator, FHSS
retuning, or microwave link.

## Findings

The one-hour baseline path-diversity result is from **300 independent, balanced
synthetic HF colored-channel paths**, each carrying a one-bit pattern with
actual elapsed observation 3,714.015625 seconds (about 61.9 minutes). For the
Gaussian state likelihood at −16 dB-Hz, 296/300 paths were correctly accepted;
at −14 dB-Hz, 300/300 were accepted. A finite phase/frequency state likelihood
with 512 phase states, 43 frequency states, 24 gain quadrature settings and
FP32 bulk arithmetic accepted all 300 paths at both C/N₀ values. At −14 dB-Hz,
the Gaussian method also accepted all 300 independently sampled actual noisy
projections. There were zero wrong-bit acceptances and zero null admissions in
the reported receiver/path cases. The block-energy comparison accepted 0/300
projected-noise trials at −16 dB-Hz and 4/300 at −14 dB-Hz in both
projected-noise and actual-noisy comparisons.

For the one-hour 300-path results, the pointwise independent-Bernoulli
Chernoff lower bound on mean correct reception is 99.006% for 300/300 and
96.3545% for 296/300. The paths are balanced across bit values; each bit's
one-sided 95% lower bound for 150/150 is 98.0227%. These bounds are for the
fixed profile, C/N₀ and decision setup, not a simultaneous confidence envelope
over methods, operating points and model choices. Conditional or single-path
noise replications are not additional independent physical paths. Zero false
admissions in 300 evaluated null cases also does not empirically establish a
10⁻⁴-per-day rate; the corresponding pointwise one-sided 95% upper bound is
about 0.994% per evaluated null case. The operational repeated-attempt claim
below is an analytic allocation under its stated model, not a direct 24-hour
null campaign.

The two-day path-diversity campaign at −24 dB-Hz contains **300 independent,
balanced HF colored-channel paths**. Gaussian reception accepted 300/300
projected-noise trials and 300/300 actual sampled noisy projections; there
were zero block-energy acceptances, wrong-bit acceptances and null admissions
in either comparison. Its sampled capture elapsed 172,876.015625 seconds
(11,064,065 samples at 64 Hz); the active source pattern span was
172,871.2 seconds. These are pointwise path-distribution results, not a
universal 99% reliability claim. The pointwise balanced-bit mean lower bound
is 99.006% overall and 98.0227% for each bit.

An earlier development diagnostic used a single two-day sampled physical path
at −24 dB-Hz. Gaussian reception accepted 32/32 conditional noise trials and
the separately retained actual noisy projection accepted 1/1. The finite
phase/frequency likelihood accepted 32/32 conditional trials; block-energy
accepted 0/32. Keep these single-path diagnostic counts separate from the
independent 300-path result.

At the two-day edge, 64 independent Gaussian paths produced 15/64 accepted at
−28 dB-Hz and 64/64 at −26 dB-Hz; block-energy produced 0 acceptances, with no
wrong-bit acceptances or null admissions. Expanding the −26-dB-Hz Gaussian
cohort to 300 paths produced **298/300** (two misses), with a pointwise 95%
lower bound of 97.4612%, so this point does not demonstrate the 99% target.
The phase/frequency receiver accepted **32/32 at −26 dB-Hz** and **31/32 at
−28 dB-Hz**. The latter comprises two disjoint, preselected 16-path shards;
its pointwise independent-Bernoulli lower bound is 83.3271%, insufficient to
establish either 90% or 99% reception. All these cases had zero wrong-bit
acceptances and null admissions. In the one-hour phase/frequency edge campaign,
300 paths yielded 79/300 at −20 dB-Hz, 276/300 at −18 dB-Hz and 300/300 at
both −16 and −14 dB-Hz. These are fixed grid/model results, not a broad
all-operating-point sensitivity guarantee.

The strongest low-C/N₀ matched reference remains an optimistic known-channel
genie detection reference. Its target probabilities are detection
probabilities `P_D`, not correct-bit probability guarantees. The 8,640 tests
per day comprise 1,440 start times/day × 3 receiver methods × 2 bit hypotheses;
the relaxed 4,320 tests per 12 hours comprise 720 start times × 3 methods × 2
bits. The required nominal normalized energy `η₁ = (C/N₀)T` is 31.3078 for
`P_D = 0.99`, false probability at most 10⁻⁴/day, and 8,640 tests; it is
17.1842 for `P_D = 0.90`, false probability at most 1%/12 hours, and 4,320
tests. At the one-hour test's −16 dB-Hz and actual capture elapsed time,
`η₁ = 93.292`, about 4.742 dB above the strict reference; at −14 dB-Hz it is
about 6.742 dB above. This is nominal total-triplet energy accounting against
a genie with known waveform/channel assumptions, not a correct-bit guarantee,
implementation loss against an equally uncertain channel benchmark, or a
universal limit for detectors.

The phase/frequency experiments exposed an acquisition-model hazard: a coarse
finite frequency grid can create long-lag phase-correlation revivals absent
from the corresponding continuous oscillator model. The P512/F43 configuration
was used for the reported phase/frequency path campaign after this diagnostic.
The completed refinement and precision checks below preserve the tested
admission decisions, but finite-state exactness does not establish accuracy
for every continuous oscillator model.

## Test signal and acquisition scope

The one-hour path campaign used total-triplet C/N₀, a sample rate of 64 Hz,
GUI-style rate 3.6, Added Symbols ×10, 30 MHz RF metadata, and the two-ray
shared HF sampled channel. Its nominal shaped carrier spacing was 7.5 Hz and
the combined triplet width was 22.5 Hz. Each detector accumulated disjoint
4-second blocks. The main result uses the known-alignment candidate bank; it
does not include an exhaustive arrival-time/frequency/epoch search.

The selected `hf-colored` profile in `tools/triad-state-campaign.py` uses the
shared two-ray channel (1.5-ms excess delay and 0.3-Hz Doppler separation),
plus the selected colored oscillator and frequency-selective effects:

| Effect | Configured value |
|---|---:|
| Relative Wiener phase diffusion | 0.00872665 rad/√s |
| Relative white FM | 0.002 Hz·√s |
| Relative random-walk FM | 0.000001 Hz/√s |
| Flicker FM RMS | 0.002 Hz |
| Flicker FM time constants and band count | 0.1–86,400 s; 12 bands |
| Relative white PM RMS | 0.01 rad |
| Slow and fast frequency-selective amplitude perturbations | 5% each |

The receiver prior is distinct from the physical generator: the phase/frequency
campaign uses phase diffusion 0.0153 rad/√s, frequency standard deviation
0.003 Hz, OU correlation time 1,000 s, and a ±0.02-Hz frequency envelope.
The phase diffusion approximately includes the generator's white-FM contribution.
The Gaussian receiver mixes four fixed coherence times: 16, 64, 256 and 1,024 s.
The phase model's static amplitude nodes are 0.5, 1 and 2, with weights
0.25, 0.5 and 0.25; eight relative-ray phases are marginalized along with the
common phase/frequency trajectory. The Gaussian tap powers are 0.5 each.
These are different physical priors, so the comparison is not an ablation of
shared phase alone.

The main physical profile has zero static common offset, deterministic drift
and relative sample-clock error. Its colored oscillator states evolve
continuously through the full observation. The small acquisition probe adds
uniform off-grid arrival and frequency truth within its declared bank, but
does not add sample-clock uncertainty. A 1-ppm clock error would accumulate
0.173 s over the two-day capture—about one fine cell at rate 3.6, or ten at
rate 36. Such clock-uncertain multi-day acquisition is not qualified here.

The campaign leaves additional burst dropouts and source-envelope FHSS off.

The profile is a reproducible synthetic impairment selection, not a fitted
distribution for HF propagation or a particular transmitter. Results are
conditional on this profile, the selected keyed patterns, the public candidate
bank and block calendar. The waveform is still a sampled, shaped complex
baseband triplet; the experiment does not establish emissions compliance,
interference perception or physical LPI behavior.

The Gaussian and phase/frequency receivers consume full-covariance complex
block statistics. They are compared against a block-energy method whose
per-block public Gram rank can vary.
For each independently generated path, signal-present and noise-only
projection trials use paired deterministic thermal-noise seeds. The actual
noisy projection is a separate sampled channel/noise pass and is evaluated
only at the C/N₀ matching its export. “300 paths” refers to independent
key/channel path exports, balanced across transmitted bits, not 300 noise
draws of one path.

## Path results

| One-hour method | −20 dB-Hz | −18 dB-Hz | −16 dB-Hz | −14 dB-Hz |
|---|---:|---:|---:|---:|
| Gaussian state likelihood, projected noise | 20/300 | 171/300 | 296/300 | 300/300 |
| Phase/frequency likelihood, projected noise, P512/F43 | 79/300 | 276/300 | 300/300 | 300/300 |
| Block-energy, projected noise | 0/300 | 0/300 | 0/300 | 4/300 |
| Gaussian state likelihood, actual noisy projection | — | — | — | 300/300 |
| Block-energy, actual noisy projection | — | — | — | 4/300 |

All rows had zero wrong acceptances and zero admissions in their paired null
trials. Methods and SNR points reuse the same physical paths and thermal
realizations; this pairing is not additional path diversity. Actual-noise
rows use a separately generated noisy waveform pass.

The phase/frequency row used FP32 bulk arithmetic, 24 gain quadrature
settings, and the declared finite state prior. It is a particularly strong
result for that finite model and matched setup. It does not prove the phase
prior is the correct physical distribution, nor that the score is robust to
continuous-model mismatch, longer unmodeled outages or an acquisition bank.
The actual-noise rows retain sampled waveform variation, but do not replace
the much broader distribution of possible real HF channels.

On the **same 32 two-day paths and thermal-noise realizations at −28 dB-Hz**,
Gaussian reception accepted 7/32 and phase/frequency reception accepted
31/32 at the same gate. There were 24 phase/frequency-only successes, zero
Gaussian-only successes, seven shared successes and one shared miss. This
paired comparison supports further work on constrained phase trajectories;
it does not isolate one algorithmic change because the two likelihoods use
different gain/phase priors.

Additional path-diversity cases used the same core waveform/channel settings:

| Case | Paths and C/N₀ | Accepted | Elapsed capture |
|---|---|---:|---:|
| Two-day Gaussian diversity | 300 at −24 dB-Hz; projected and actual sampled noise | 300/300 in both | 172,876.015625 s |
| Two-day Gaussian edge | 300 at −26 dB-Hz; projected noise | 298/300 | 172,876.015625 s |
| Two-day phase/frequency edge | 32 at −26 / −28 dB-Hz; projected noise | 32/32 / 31/32 | 172,876.015625 s |
| Two-day erasure diversity | 32 at −24 dB-Hz; 10-ms erasure in every 400-ms dwell; projected and actual sampled noise | 32/32 in both | two-day case |
| HF burst | 16 at −14 dB-Hz; 3,600 s requested active; projected and actual sampled noise | 16/16 in both | 7,310.828125 s |
| Small acquisition bank | 16 at −14 dB-Hz; projected / actual sampled noise | 10/16 / 12/16 | 3,714.09375 s |
| FHSS envelope, 72 Hz / 1,024 Hz sample rate | 16 at −14 dB-Hz; projected and actual sampled noise | 16/16 in both | 3,602.1904296875 s |

The phase/frequency receiver accepted 15/16 projected-noise and 16/16 actual-noise
acquisition cases; it accepted 16/16 in both noise modes for the burst and
72-Hz hopping-envelope cases. These small cohorts diagnose behavior; 16/16
alone has only an 82.93% pointwise one-sided 95% lower success bound.

For the additional cases, block-energy accepted zero, and there were zero
wrong-bit acceptances and zero null admissions. The small acquisition bank is
not the exhaustive blind search. These counts verify the source bit; the
bit-marginal likelihood does not report a separately verified epoch or timing
estimate, so they are not a complete synchronization qualification.
At rate 3.6/64-Hz and 36/640-Hz settings,
the FHSS waveform was rejected by the protected-dwell guard; rate 72/1,024-Hz
passed that waveform-format guard. The FHSS case remains envelope-only and
does not simulate real RF retuning. The sampled channel retains echoes across
the envelope guards; echoes through frequency-changing RF hops were not tested.

The final two-path Rate 36 / 640-Hz two-day export exceeded its 720-second
generation allowance before either manifest completed. Its child exporters
were terminated and the failure logs retained. There is no receiver result
for that wider-band two-day case. This failure is a computation limit of the
attempted sampled exporter, not evidence that the waveform is undecodable.
The ad hoc outer Python timeout killed its campaign driver but left the two
exporter children running; both were explicitly terminated before Pod deletion.
Future bounded launches must own a dedicated process group and terminate/reap
the entire group (TERM grace, then KILL), rather than rely on
`subprocess.run(timeout=...)` to clean up descendants. The campaign driver
itself does not currently implement a wall-time limit or cancellation protocol.

The per-bit and pooled independent-Bernoulli mean bounds above are pointwise,
not familywise or simultaneous bounds. Conditional single-path results remain
separate from path-diversity confidence intervals.

## Conditional false-admission and energy references

The duration and search objectives remain the strict target of 99% correct
reception and at most 10⁻⁴ false admission per 24 hours, alongside the
explicitly relaxed 90% and 1% per 12 hours target. The following genie energy
references instead target `P_D` at each fixed, known-waveform test. Their
complete family counts allocate the false-alarm budget across start times,
three methods and two bit hypotheses:

| Target | Start times × methods × bits | Genie `η₁` |
|---|---:|---:|
| `P_D = 0.99`, 10⁻⁴ per 24 hours | 1,440 × 3 × 2 = 8,640 | 31.3078 |
| `P_D = 0.90`, 1% per 12 hours | 720 × 3 × 2 = 4,320 | 17.1842 |

For the same saved receiver scores, using the relaxed schedule lowers the
strict log gate by `log(200) = 5.29832`. One-hour phase/frequency reception
then changes from 276/300 to 293/300 at −18 dB-Hz and from 79/300 to 181/300
at −20 dB-Hz. Two-day Gaussian reception at −26 dB-Hz changes from 298/300
to 300/300; two-day phase/frequency reception at −28 dB-Hz changes from
31/32 to 32/32. These are explicitly labeled alternate-gate point estimates
on the same samples, with zero null admissions; no fresh confidence claim or
proof that relaxing the target is fundamentally necessary follows. If both
duration alternatives are searched instead, the strict family gate increases
by `log(2)`; the −26-dB-Hz Gaussian count becomes 296/300.

Here `η₁ = (C/N₀)T` is nominal total-triplet energy relative to white-noise
density. The genie assumes the known candidate waveform and its stated
alignment/channel knowledge. This is a detection-energy benchmark, not a
correct-bit probability, receiver implementation, unknown-channel guarantee
or duration recommendation. At the actual one-hour capture elapsed duration,
the nominal energy ratios at −16 and −14 dB-Hz are 93.292 and approximately
147.9, respectively. Their 4.742 dB and 6.742 dB excess over the strict genie
reference are not pure decoder loss: the operational model includes nuisance
uncertainty and finite search, while the genie grants more information.

For the two-day observation, nominal total-triplet energies at −24, −26 and
−28 dB-Hz are 688.232, 434.245 and 273.990. Their gaps above the strict genie
energy are 13.421, 11.421 and 9.421 dB. The −28-dB-Hz phase/frequency result
has not established the genie reference's 99% target; these numbers describe
energy expenditure, not a demonstrated 99%-sensitivity gap. The increasing
gap at longer duration is consistent with the cost of unknown evolving phase
and gain, but these experiments do not separate all causes of that gap.

Separately, `tools/triad_gaussian_bounds.py` evaluates one specified proper
complex OU/static tap prior from public Gram matrices, without reading hidden
simulator means or signal energies. For the two-day exported design and its
fixed individual Gaussian model, the Markov null gate was 18.2745. The optional
Gaussian fractional-moment null calibration changed the gate to −5.52746, a
reduction of 23.80196. Its conditional H1 correct-bit admission lower bound
rose from 0.999999941916 to 0.99999999995398 for the fixed Gaussian coherence
time `T_c = 256 s` case. This bound is conditioned on that one fixed design,
prior and complete capture; it does not apply to the multi-coherence Gaussian
mixture, phase/frequency mixture, adaptive model selection or an empirically
calibrated receiver. The calculation uses FP64 inequalities without
directed-rounding certification and assumes known independent circular white
noise, fixed public designs and erasure calendar, and the declared stationary
proper-complex Gaussian gain prior. A bound below target is inconclusive; it
does not show that reception is impossible.

At −26 dB-Hz, the same fixed-`T_c=256 s` Gaussian calculation, with four
selected methods budgeted, gives a calibrated gate of 11.74144 and a model-
conditional correct-admission lower bound of 0.998909. At −28 dB-Hz, the
calibrated gate improves only from 18.56218 to 18.34292 and the lower bound
is 0.303064. On 128 conditional noise draws of one sampled two-day path,
that small gate change raises correct acceptances from 62 to 67, with zero
wrong or null admissions. This is a fixed Gaussian model diagnostic, separate
from the four-coherence mixture and independent-path results. A fourth
operational method also requires reallocating the other methods' budgets;
these results cannot simply be added to the three-method family unchanged.

### Unknown noise variance diagnostic

A one-hour, 1,024-trial H0 diagnostic used only the public Gram matrix and a
fixed admission gate of 18.2745 at the nominal −14 dB-Hz variance. With the
actual noise variance scaled by 1, 1.1, 1.25, 1.5 and 2 relative to the assumed
variance, admissions were 0, 0, 319, 1,024 and 1,024. In particular, a 1.25
scale means the actual variance is 25% higher than assumed; it does not mean
the receiver underestimated variance by 25% when measured relative to the
actual value. This sensitivity rules out carrying the known-variance false
admission allocation over to unknown noise variance without additional work.
The 1,024 tests cover this finite public-Gram diagnostic only; they are not an
unknown-variance operational false-admission campaign.

An initial wrapper used incorrect likelihood normalization; those results are
retracted and retained only as invalid diagnostic artifacts. With the corrected
normalization, the first-block empirical covariance-trace residual was −0.823
standard errors; the relative covariance Frobenius error was 0.01993. An independent upper
confidence bound on residual noise variance is the next design direction, but
it is not implemented. There is no operational false-admission guarantee for
unknown noise variance in this study.

## GPU and computation evidence

### Benchmarks and scaling assumptions

For a known received complex waveform in white circular Gaussian noise, with
energy ratio `ρ = ||s||²/σ²`, the coherent detection reference is
`P_D = Q(Q⁻¹(α_test) − √(2ρ))`. It grants the entire channel trajectory and
alignment. Its energy collection scales as `T ∝ (C/N₀)⁻¹` for fixed unit gain.
The measured −16-dB-Hz hour-long point is within 4.742 dB of this nominal
energy reference; this is a quantified gap, not proof of optimal reception
with unknown channel and synchronization.

For a specified zero-mean proper complex Gaussian channel, whiten the noise
and let the signal covariance eigenvalues be `λ_i`. Its exact model likelihood
has distributions

```
L | H0 = −Σ log(1+λ_i) + Σ [λ_i/(1+λ_i)] Exp_i(1)
L | H1 = −Σ log(1+λ_i) + Σ λ_i Exp_i(1).
```

Thus `E1 L = tr C − log det(I+C)` and `Var1 L = tr C²`. For low per-mode SNR,
the mean information is approximately `tr C²/2`; for a stationary channel
with fixed coherence time and a sufficiently long observation this can give
`T ∝ (C/N₀)⁻²`, rather than coherent inverse-power scaling. Constants depend
on the tap spectrum, waveform, masks and noise covariance. The public-Gram
bound tool computes these quantities for the actual complete exported design;
it uses unconditional stationary covariance in `tr C`, not filtering covariance.
Its wrong-candidate bounds use full cross-Grams, including bit/epoch dependence.

The Gaussian recursion is checked against dense likelihoods, and the finite
phase/frequency filter against an enumerated small-state reference. These are
exactness checks for declared models. They do not make either model the optimal
physical HF receiver. No fitted power law or duration extrapolation is claimed
from the two measured durations.

The design guide's 1-dB implementation targets against equally informed
known-channel or fixed-rank references are not qualified for the new evolving-
state receivers by this campaign. Dense/finite-path numerical agreement
validates likelihood arithmetic, while the measured sensitivity still includes
prior mismatch, model uncertainty, conservative admission gates and a limited
acquisition bank. The measured energy gaps above, the noise-variance failure,
and the explicit large-bank memory estimates below quantify separate obstacles;
they are not a proof that any remaining gap is unavoidable.

### Noise-scale alternatives identified by the failure test

An independently obtained upper bound `v_U` on noise variance can support a
conservative likelihood gate. For fixed assumed `v ≥ σ²`, a proper fixed-mean
likelihood has `E0 LR = exp[−(1−σ²/v)||μ||²/v] ≤ 1`; proper channel/path mixtures
inherit the inequality. If calibration is independent of all scored features
and fails with probability at most `δ`, an attempt's false-admission bound
becomes `α + δ`. A union allocation covers overlapping attempts without
requiring their independence.

One possible calibration statistic is actual noisy energy in the orthogonal
complement of the **whole scored candidate bank**, with complex dimension
`M = valid samples − rank(bank)`. Then
`v_U = E_residual / GammaQuantile(δ, M, scale=1)` under constant white-noise
variance. This is the complex zero-mean counterpart of the
[chi-square variance confidence construction](https://www.itl.nist.gov/div898/handbook/prc/section2/prc231.htm).
The existing noiseless `signal_energy.f64` cannot supply that statistic.
Stable QR residuals and rank consistency with all retained scoring directions
are required; a sufficiently broad bank may leave no residual coordinates.

With half the daily budget allocated to calibration failure, 1,440 attempts
and one duration, `δ_attempt = 3.47222e−8`. For this D=4 design, constant-variance
pooling over an hour gives a typical upper-variance factor 1.01123; two-day
pooling gives 1.00163. Allowing variance to change independently every four
seconds instead gives approximately 1.55547 and 1.61973 after the blockwise
failure allocation. These are analytical variance allowances, not demonstrated
receiver losses. Global averaging cannot upper-bound changing local variance.

Another Gaussian-only alternative is a directional likelihood. For fixed
covariance shape `R`, `u=y/||y||` and `n` complex samples,
`LR = det(R)⁻¹ (uᴴR⁻¹u)⁻ⁿ` removes a common unknown scale. This follows from
integrating the Gaussian radial density, rather than inserting a noise estimate
in the existing LR; see the
[angular central Gaussian framework](https://academic.oup.com/biomet/article-abstract/74/3/579/238632).
Actual noisy total energy must be added to the exported observations before
this can be evaluated. Neither proposed noise-scale method is implemented or
qualified in this campaign; neither resolves arbitrary colored interference.

### Measured acceleration

The same-host P512/F43 FP64 reference took 31.940978 seconds on two AMD EPYC
9575F CPU threads and 1.650833 seconds on one RTX PRO 6000 Blackwell Workstation
GPU: **19.35× measured acceleration**. Both signal and null passes are included;
process startup and the shared waveform export are excluded. Another workload
ran on the other GPU. The two GPUs executed independent batches, rather than
splitting one capture across devices.

A one-hour, one-trial P512/F43 FP64 comparison processed the same exported
path on the local Ryzen 5 PRO 5650U laptop CPU and the remote GPU. The paired CPU pass times were 204.711616876 and
185.135143483 seconds; the corresponding GPU times were 0.935983027 and
0.714850053 seconds. The compared score was 13.4306988088917 on CPU and
13.4306988088643 on GPU. Summing these timed passes gives approximately 236×
observed pipeline acceleration with the tested devices and workload. Timing
excludes process startup and export; the CPU used two threads, and another
workload occupied a different GPU card during the comparison. This is a
measured acceleration for this finite-model likelihood workload, not general
receiver throughput or a promise for other grids and batch sizes.

A candidate frequency-layout rewrite from `einsum` to matrix multiplication
made the full prediction step only 1.01919× faster in its measured case, so it
was not adopted. A local full-state layout change with a ~2% gain did not
justify the additional implementation complexity. The large GPU speedup above
comes from CPU-to-GPU execution of the selected tensor workload, not from this
layout tweak.

### Precision and finite-grid checks

The CUDA finite-model diagnostic passed all seven checks, including direct
small-state path enumeration, raw-sample likelihood parity, persistence,
precision and a moderate-SNR null-mean check. The latter used 12,000 draws
and measured mean likelihood ratio 0.99724 with standard error 0.00208;
it does not validate rare operational tails.

For 16 paired one-hour conditional noise trials at −20 dB-Hz, P512 FP32 and
FP64 scores differed by at most 9.0e−6 log units across both candidates and
H0/H1. For a two-day −28-dB-Hz conditional trial, the maximum difference
was 0.000514. Neither comparison changed the chosen candidate or admission.
The two-day diagnostic itself missed in both precisions despite choosing
the correct raw bit; it is not one of the 32 independent-path trials.
Doubling the phase grid and refining the frequency grid to P1024/F83 left
the hour diagnostic's admissions unchanged. Two preselected independent
two-day paths at −28 dB-Hz also remained correctly accepted on P1024/F83.
The largest signal-score change on those two paths was 0.13514 log units;
the largest hour-long coarse-to-fine frequency-grid change was 0.14351.
These bounded refinement checks are not a continuous-grid convergence proof.

## Validation executed and limits

| Check | Result |
|---|---:|
| Release-mode focused triad tests | 10/10 passed |
| ASan/UBSan focused triad tests | 10/10 passed; leak detection disabled |
| Python state-study tests | 19 passed |
| Gaussian conditional-bounds tests | 9 passed |
| Independent-path study tests | 4 passed |
| Phase/frequency focused diagnostic | 7 passed on CPU and 7 on CUDA |

These focused checks cover the isolated exporter, workspace, finite likelihoods
and study drivers. They are not the ordinary Robust receiver regression
campaign. No external radio validation is claimed.

The Gaussian two-day diversity, 32-path phase/frequency edge, bounded
finite-grid refinement and long-duration numerical parity campaigns are
complete. A 300-path two-day phase/frequency campaign was not run. The
Rate 36 two-day exporter timed out, as recorded above. The two-GPU Pod was
stopped and deleted after verified artifact retrieval, within the authorized
two-hour allocation; its disposable container storage was removed. No cloud
resource remains reserved for this study.

The current acquisition results do not validate a comprehensive blind
timing/frequency/epoch search: the small-bank case is only a limited acquisition
probe. The statistics exporter caps the unique design dimension at D=64 and
raw samples at 128 million per export. That sample cap corresponds to about
8.68 hours at 4,096 Hz or 23.15 days at 64 Hz, but the CLI also limits the
observation calendar to seven days. These are interface/resource ceilings,
not evidence of operation at every supported sample rate or duration. The
full cross-Gram export is a small-bank simulation convenience, not a suitable
representation for a large blind bank. For illustration, 3,379 timings ×
13 epochs × two bits gives 87,854 hypotheses. With two distinct columns per
hypothesis, a dense complex-FP64 cross-Gram would require about 494 GB **per
block**. The within-hypothesis 2×2 Grams require only 5.62 MB per block.
Likewise, one FP32 P512/F43/24-quadrature posterior for that whole bank would
already occupy about 186 GB before scratch. These are arithmetic storage
estimates, not measured search timings. A practical full search needs tiled
likelihood states and shared FFT/GEMM projections, with cross-candidate noise
generated from common raw samples rather than a dense simulated Gram bank.
Weak-candidate pruning has not been validated and is not silently used here.

The
FHSS result only validates an envelope-format case at 72/1,024; it does not
simulate RF retuning or receiver capture during frequency-changing hops.
The sampled channel does retain delayed waveform history through envelope
guards; this is not a physical hopping validation. No
5.8-GHz GPSDO-free or microwave qualification was performed. Multi-day
checkpoint/restart, real noise/interference distributions, adaptive
oscillator/channel changes, actual operator coordination, unknown-variance
false-admission control, and operational false-admission monitoring remain
unqualified.

## Reproduction and provenance

Campaign generation uses `tools/triad-state-campaign.py`; the independent-path
receiver is `tools/triad-path-study.py`. The stable campaign split is `holdout`
for the 300 physical HF-colored paths and must remain a no-tuning holdout.
Local development fixtures use the separate `development` seed namespace.
Do not regenerate the holdout and then adjust priors, thresholds or grids
against its outcomes while continuing to call it holdout evidence.

A minimal command shape for the one-hour path campaign is shown below. Exact
seeds, shard ranges, input and executable hashes, full invocations, result
arrays and hardware metadata are retained in the evidence archive below:

```sh
python3 tools/triad-state-campaign.py \
  --binary build/dev-cli/triad_long \
  --output /tmp/triad-hf-colored-holdout \
  --paths 300 --seconds 3600 --profile hf-colored --split holdout \
  --block-seconds 4 --sample-rate 64 --rate 3.6 --cn0 -14

python3 tools/triad-path-study.py /tmp/triad-hf-colored-holdout/path-*/ \
  --output /tmp/triad-hf-colored-gaussian.json \
  --cn0 -16 -14 --split development --device cpu --precision fp64 \
  --methods gaussian block-energy --selection-methods 3

python3 tools/triad-path-study.py /tmp/triad-hf-colored-holdout/path-*/ \
  --output /tmp/triad-hf-colored-phase-frequency.json \
  --cn0 -16 -14 --split development --device cuda:0 --precision fp32 \
  --methods phase-frequency --selection-methods 3 --phase-states 512 \
  --frequency-bound .02 --phase-diffusion .0153

python3 tools/triad-path-study.py /tmp/triad-hf-colored-holdout/path-*/ \
  --output /tmp/triad-hf-colored-actual.json \
  --cn0 -14 --split development --device cpu --precision fp64 \
  --methods gaussian block-energy --selection-methods 3 --actual-observation
```

The generator's `holdout` label controls the independent physical path/key
seed namespace. In the receiver command, `--split development` labels the
thermal-noise resampling namespace only; it does not relabel the physical
exports or make this receiver analysis a clean holdout after development use.
These commands therefore describe the recorded development-noise analysis on
the fixed physical path set, not an untouched end-to-end holdout protocol.

The [evidence archive](evidence.tar.gz) retains source and binary hashes,
campaign and receiver configurations, independent key/channel/noise seed
manifests, per-path outcomes and score arrays, test logs, numerical and GPU
comparisons, and executed source snapshots. The [artifact manifest](artifact-manifest.json)
records SHA-256 hashes and the retained-data boundary; [final source hashes](final-source.sha256)
identify the local implementation. [Machine-readable results](summary.json)
and the [reception figure](reception.png) are generated by
[`analyze_results.py`](analyze_results.py). Alternate-gate counts in that
summary are fixed-score reclassifications, not new trials or newly calibrated
confidence claims.

All 680 completed remote workspace manifests and their file hashes are
retained. The approximately 6.68 GB of completed workspace files are not
embedded in this repository; representative balanced pairs remain in ignored
local evidence. Reproducing the full sampled-channel campaign requires
regeneration from the recorded source, commands and seeds. Cloud access,
billing and cleanup screenshots are kept outside the repository. No secret
key material is needed for these deterministic simulation fixtures.
The final hash inventory also records 14 incomplete temporary files from
the two timed-out Rate 36 exports (about 0.255 GB); they are not completed
workspaces or receiver evidence.

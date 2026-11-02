# Selective amplitude modulation in the isolated three-carrier modem

> **Model and capture correction.** Every receiver result in this record used
> the historical `legacy-allpass-v1` selective operator. That operator remains
> a valid reproducibility target for these runs, but the measured 0.00075-Hz /
> 0.05-Hz response collapses its added modulation to nearly common mode at
> displaced offsets. This study therefore does **not** establish useful strong
> differential AM reception for that case. The isolated v3 default is now
> `dispersive-dft-v1`; ordinary Robust simulation and hardware channels remain
> unchanged on the legacy operator. See the [model correction record](../2026-11-02-selective-correction/README.md)
> and its accepted [corrected-tone response probe](../2026-11-02-selective-correction/response-v2/README.md).
>
> There is a separate observation-calendar issue in the retained 850-day
> Each65/Off exports. Their manifest reports `source_samples = 18,366,718` and
> `sample_count = 18,366,719`; the natural full-tail calendar is 18,368,197
> rows: 1,478 rows or 5,912
> seconds at 0.25 samples/s were omitted. The report's larger channel-output
> count does not show those rows were captured or scored. The lost-tail energy
> and its sensitivity impact were not measured. The 16/16 admission results
> remain results for the legacy sampled support only; reproduce them with the
> legacy operator and original calendar. A corrected comparison must extend
> both models to a common calendar covering the longer tail.

**The sampled campaigns completed, including a consequential slower-rate
failure.** Seven of nine primary strict-gate signal cells achieved 300/300
correct accepted observations; the other two achieved 297/300 and 299/300.
The slower-rate follow-up improved signal counts but admitted wrong-key
signals: 6/32 and 7/32 at the strict gate, rising to 10/32 and 13/32 at the
relaxed gate. This prevents claiming a generally validated rate improvement.
An accepted diagnostic identifies substantial wrong-key subspace overlap; a
qualified repair remains open. No wrong accepted bits occurred in the
correct-key signal cells. These are fixed simulated operating points, not
universal sensitivity or operational false-admission qualifications. The
archive and final verifier have passed their declared integrity checks.

The starting source revision is `f13e3bc937f5b45949d7fd0adaedbe689f1b71b7`.
The study adds selective-stage simulation diagnostics and expands the isolated
campaign configuration controls. The accepted primary audit binds the completed
jobs to their planned source, binary, defaults, seeds, manifests and saved score
NPZs. The final inventory distinguishes remotely hashed raw arrays from the
subset retained and rehashed locally; the archive section records its limits.
The ordinary Robust receiver and its unrelated regression campaign are outside
this study.

## Questions and targets

The main questions are how the implemented selective modulation changes
receivable energy and candidate separation; whether different rates, block
lengths or channel priors recover that energy; and how deterministic modulation
differs from continuously renewing Gaussian ray fading. The comparison follows
the [preceding Doppler/delay study](../2026-11-02-spread/README.md), retaining
complex sampled projections and explicit noise calibration.

The strict operating target is **99% correct accepted reception and at most
10⁻⁴ probability of any noise-only admission per 24 hours**. The relaxed target
is **90% correct accepted reception and at most 1% per 12 hours**. Both remain
configurable. Correct acceptance, wrong acceptance, missed reception, null
admission and wrong-key admission must be reported separately. A successful
presence test is not by itself a correct-bit reception result.

All C/N₀ values are in dB-Hz: −60 dB-Hz means linear C/N₀ of 10⁻⁶ Hz. They are
not in-band SNR values. The injected C/N₀ is a nominal pre-fading link setting;
selective modulation and fading change realized signal energy. No antenna
efficiency or radiated-power inference is made from these supplied C/N₀ values.

## Sampled physical model and receiver scope

Unless a table explicitly says otherwise, the frozen configuration uses one
encrypted source bit, clock-derived keyed patterns, three adjacent shaped
carriers, 8270 Hz RF shift, GPSDO OCXO presets at both endpoints, margin 1,
Added Symbols ×1, FHSS Off and Symbol Burst Off. The public timing configuration
is 10 ms clock sync / 1 ms region / 0 ms offset with 70 ms audio error. Its
initial-arrival prior extends approximately ±90.5 ms, but these jobs retain
only the nominal zero timing and zero epoch branch. That is a declared
low-rate acquisition approximation, not exhaustive blind acquisition over the
prior. The high-spread controls deliberately remove initial-arrival error.

The sampled transmitter/channel preserves continuous oscillator, ray and filter
state. Delay, deterministic ray-center separation D and Gaussian width B are
separate parameters. The independent ray gains use the finite stationary
Gaussian FIR teacher, with B defined as twice the RMS Doppler width. B=0 in
the stable controls means no Gaussian or OU gain renewal; deterministic
ray-center rotations can still be nonzero. The physical parameters are exposed
as public receiver configuration, while realized gains, selective phases,
source-bit truth and simulator noise power remain hidden from scoring.

The reciprocal low-rate path uses constant oscillator offsets and Wiener phase.
It does not enable the full white/flicker/random-walk FM or white PM model,
sampled frontend, AGC, dropouts or FHSS. A long sampled span under this model
does not qualify a physical oscillator or propagation path for that span.
The maintained [channel contract](../../../channel-models.md) and
[v3 implementation guide](../../../triad-v3.md) define the broader implementation.

### What additional selective modulation means

The shared channel stage follows ray propagation and precedes receiver noise.
Three complementary filtered branches have time-varying gains

\[
g_j(t)=1+d_s\sin(2\pi\,0.1D t+\phi_{js})
          +d_f\sin(2\pi D t+\phi_{jf}).
\]

The six seeded phases evolve continuously. These are deterministic sinusoids
after initialization, not fresh amplitude innovations each cycle. The AM rates
depend on D, not on the independently specified Gaussian B. Margin scales the
resolved spread parameters once and does not multiply the component depths.
Zero resolved D bypasses this stage even if a depth is selected.

Each65 sets both depths to .65, allowing an individual branch gain between
−0.30 and 2.30, including polarity reversal. Slow65 and Fast65 activate only
one .65 component. **The stage does not normalize average power or individual
realizations.** It can add received energy as well as redistribute energy into
sidebands. It is therefore incorrect to describe every effect as a pure SNR
penalty. Off and active modes also have different filter tails.

New fixed-size `path_truth_simulation_only.selective_perturbation` telemetry
records component depths/rates, filter geometry, initial phases, seed, tail
allowance, processed rows and pre/post-stage signal energies. The energy sums
include captured tails and are measured before receiver oscillator processing,
thermal noise, frontend and observed erasures. Divide each sample-energy sum
by its recorded physical sample rate to obtain energy in seconds. The telemetry
is diagnostic truth, never a receiver channel estimate or acquisition input.

### Likelihoods and retained information

The main receiver is `gaussian-window-ucb`, using joint complex block/tap
correlations, public Gram matrices and a full-bank residual upper confidence
bound on noise variance. Its assumed covariance is
`exp[−(π B Δt)²/2]` at physical block centers. It treats fixed nonoverlapping
windows as independent and coefficients as constant within exported blocks.
Neither approximation is an exact model of the physical FIR channel. Larger
windows can retain more cross-block evidence but cannot recover information
already discarded during block projection.

Gaussian jobs use strict FP32 CUDA bulk work with existing FP64 preparation
and evidence reductions. The stable FFT likelihood uses CPU FP64 and its
public small residual-frequency bank. The Gaussian window and OU comparisons
use zero residual CFO beyond the exported candidates. This is a comparison of
declared receivers, not an equal-hardware throughput comparison or a claim of
universal optimality.

[`retention.py`](reproduction/retention.py) separately computes noiseless
projection energy from exported means, Grams and signal energy, using an FP64
PSD pseudoinverse with a declared rank cutoff. A candidate remains fixed over
the observation while its fitted coefficients may vary per block. The report
separates full-bank retention, correct- and wrong-label retention, physical
pre/post-AM energy and residual noise degrees of freedom. These are diagnostic
subspace quantities, not achievable detection probabilities. Receiver scoring
does not consume them.

## Frozen experiment matrix

The plans retain exact commands, seeds, deadlines, bounds and interpretation
limits. Requested durations below are accompanied by actual rounded active
durations and sampled capture spans in the completed-results section.
For Added Symbols ×1, the existing mapping gives fine-symbol rate per carrier
`R = GUI rate / 6`, carrier spacing `1.25 R` and ideal triplet bandwidth
`3.75 R`. This bandwidth excludes Doppler/AM capture allowance.

### Main paired development screen

[Export plan](reproduction/screen-export-plan.json),
[score plan](reproduction/screen-score-plan.json),
[generator](reproduction/make_screen.py).

The 48 cases contain four balanced sampled physical paths each, giving 192
exports. They reuse 20 distinct root vectors across the configurations; these
are not 192 independent physical-path realizations. Each conditional score
point uses 16 signal and 16 null noise draws per path. Depth variants share
roots at fixed rate, sample rate, duration and export C/N₀, but changing tails
means neither whole-array identity nor identical full noise samples follows.
Rate/sample-rate changes can change roots and must not be treated as paired.

| Profile | Requested span | GUI rate, Hz | Fs, Hz | Block, s | Delay, ms | D, Hz | B, Hz | Modes | Score C/N₀ grid, dB-Hz |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| g001 | 1 day | .036 | .25 | 64 | .005 | .001 | .001 | A | −30, −28, −27, −26, −25, −24 |
| g01 | 1 day | .36 | 2 | 8 | .05 | .005 | .01 | A | −28, −26, −24, −23, −22, −20 |
| g1 | 8640 s | 3.6 | 16 | 1 | .5 | .1 | .1 | A | −16, −14, −12, −10, −8 |
| stable | 1 day | .036 | .25 | 256 | .05 | .005 | 0 | A | −38, −36, −34, −32, −30, −28 |
| g01-slowrate | 1 day | .036 | 2 | 8 | .05 | .005 | .01 | R | −28, −26, −24, −23, −22, −20 |
| g1-slowrate | 8640 s | .36 | 16 | 1 | .5 | .1 | .1 | R | −16, −14, −12, −10, −8 |
| g001-fastam | 1 day | .036 | .25 | 64 | .005 | .01 | .001 | F | −30, −28, −27, −26, −25, −24 |
| g1-fastam | 8640 s | 3.6 | 16 | 1 | .5 | 1 | .1 | F | −16, −14, −12, −10, −8 |

A = Off, Each1, Each5, Each10, Each25, Each65, Slow65, Fast65;
R = Off, Each25, Each65;
F = Off, Slow65, Fast65, Each25, Each65.
The fast-AM variants change ray-center separation as well as AM rates while
holding B fixed. They do not isolate AM rate alone. The nominal exports use
−25 dB-Hz; the conditional simulator rescales linear signal evidence and
regenerates the joint Gaussian noisy projections and total energy for the
listed C/N₀ grid. Its replicas remain conditional on the sampled physical path.

### High-spread, known-initial-arrival controls

[Export plan](reproduction/highspread-export-plan.json),
[score plan](reproduction/highspread-score-plan.json),
[generator](reproduction/make_highspread.py).

Ten cases use two balanced paths each, totaling 20 exports: B=D=10 Hz with
6 ms delay, and B=D=30 Hz with 7 ms delay, each at Off, Each25, Each65, Slow65
and Fast65. Both profiles request 120 seconds at GUI rate 360 Hz, Fs=2048 Hz,
1/64-second blocks and 64-block windows. The ideal triplet bandwidth is 225 Hz;
delay×bandwidth is respectively 1.35 and 1.575. The conditional C/N₀ grid is
−2, 0, 2, 4, 6, 8, 10 and 12 dB-Hz, with 16 draws per control/path/point.

Clock sync, region, offset and audio error are explicitly zero here. Endpoint
oscillator/clock-rate processes remain active. This removes an initial-arrival
confound at high rates without claiming a blind acquisition result. B and D
both change between profiles; their effects are not independently identified.

### Actual sampled-noise holdouts

[Export plan](reproduction/holdout-export-plan.json),
[score plan](reproduction/holdout-score-plan.json),
[generator](reproduction/make_holdout.py).

Nine fixed signal cells contain 300 balanced observations each: **2700 signal
exports**, plus **192 controls** in six 32-path null/wrong-key cells. These use
the g001, g01 and g1 geometries above. Each profile tests Each65 at two C/N₀
values and Off at the stronger value:

| Profile | Each65 C/N₀, dB-Hz | Off and control C/N₀, dB-Hz | Signal paths per cell | Null / wrong-key paths |
|---|---|---:|---:|---:|
| g001 | −26, −27 | −26 | 300 | 32 / 32 |
| g01 | −22, −23 | −22 | 300 | 32 / 32 |
| g1 | −10, −11 | −10 | 300 | 32 / 32 |

These are `actual-noisy` observations: scoring reads `observation.c128`,
`received_energy.f64`, public Grams and validity calendars, without resampling
noise, sweeping injected C/N₀ or reading noiseless means for the decision.
There are 1800 distinct six-domain root vectors across the matrix. Off/Each65
at fixed geometry and C/N₀ are paired; controls reuse the first 32 roots.
Different C/N₀ values have separate seeded sets. Cross-mode/control pooling
must not manufacture additional independent physical paths.

Each signal cell has 150 observations per source-bit stratum. Report pointwise
bitwise Clopper–Pearson intervals under the independent, identically distributed
within-stratum assumption, and the analyzer's explicitly justified balanced
mean bound when used. A pooled iid-binomial claim needs an additional
identical-distribution assumption. Pointwise bounds do not establish simultaneous
coverage across all methods, gates or operating points. Missing/failed paths
must remain in the intended-attempt accounting rather than disappearing from
the denominator.

### Genuine 850-day sampled controls at −60 dB-Hz

[Export plan](reproduction/long-export-plan.json),
[score/retention plan](reproduction/long-score-plan.json),
[generator](reproduction/make_long.py).

Off and Each65 each have 16 balanced paths, 32 exports total, with paired
roots across modes. They request 850 days of active time at GUI rate .0036 Hz,
Fs=.25 Hz and 8192-second blocks. Delay is 5 ms, D=.05 Hz and B=0. The
static-FFT-UCB receiver scores the actual sampled observations in FP64.
No waveform repetition or time scaling is authorized by this plan.

Fs is raised from the preceding study's 1/8 Hz to 1/4 Hz to accommodate active
selective sidebands. The two new modes are mutually paired; they are not an
identical-noise replay of the preceding study. An 8192-second block spans
40.96 slow and 409.6 fast AM cycles, so the two-ray projection can discard
sideband energy before the likelihood. Four paths per mode are scheduled for
the retention diagnostic. The 16 paths per mode cannot establish 99%
reliability, and stable rays cannot establish long-span stochastic-spread
performance.

### Receiver alternatives and computation

[Alternative plan](reproduction/alternatives-score-plan.json),
[generator](reproduction/make_alternatives.py),
[threading benchmark plan](reproduction/threading-benchmark-plan.json).

Twenty-five new jobs compare seven receiver configurations over five existing
Each65 cases: g001, g01, g1, g1-slowrate and g1-fastam. The seven choices are
the existing exact-B/J64 baseline; J16 and J128 at exact B; B/2 and 2B at J64;
Gaussian OU; and static FFT. Inputs, draw addresses, trial batch and calibration
allocation remain paired. These are four-path development comparisons, not
new physical holdouts. The OU coherence time matches the Gaussian e-fold time
`sqrt(2)/(π B)`; its Lorentzian spectrum is not the physical Gaussian spectrum.

At fixed block count, dense full-rank preparation scales approximately as
O(blocks×J²), and factors as O(blocks×J). Thus J128 can require about four
times the J64 preparation work and twice the factor storage; these are scaling
estimates, not measured timings. Long OU sequential updates may hit the frozen
600-second deadline. A timeout is a computational failure to complete that
comparison, not a measured sensitivity failure. Per-job 8 GiB is a configured
array allowance, not observed process RSS or aggregate concurrent memory.

All 25 alternative jobs completed and their saved-score recounts were accepted.
The selected outcome comparisons appear below. The plans alone demonstrate no
hardware acceleration; isolated throughput, memory and final comparison
provenance remain separate qualifications.

## Admission calibration and evidence strength

The declared family reserves K=16 selectable receiver/model alternatives,
D=8 duration alternatives, two calibration policies and one completed attempt
per 60 seconds. Half the false-admission budget goes to scores and half to
calibration failure. A maximum over two bit likelihoods is counted. The strict
score gate is log-LR 22.7210633896; the relaxed gate is 17.4227460230, with
the conservative strict residual calibration retained for relaxed recounts.
The strict per-policy calibration allowance is approximately 2.17014×10⁻⁹
per completed attempt/duration. Seven configurations fit K16 for a fixed
declared physical case; unrestricted post-hoc choices across all physical
configurations are not automatically covered.

The analytical guarantee assumes one stationary white proper-complex Gaussian
noise variance per observation and a fixed full scored bank. Residual energy
orthogonal to that bank supplies an independent Gamma variance bound; the
normalized additive-signal mixture then supplies the score bound. Model
mismatch can reduce reception without by itself invalidating the null
expectation argument. Colored/changing noise, unsupported frontend nonlinearities,
adaptive bank selection or unresolved numerical projection error require
separate treatment. See the [calibration contract](../../../triad-noise-calibration.md).

An analytical family allocation is not an empirical observation of a rare
daily tail. Conditional noise replicas are not independent channel holdouts;
the small null and wrong-key sets do not qualify a 10⁻⁴/day operational rate.
Wrong-key rejection is also distinct from noise-only calibration. Recounting
the same scores at the relaxed gate is not a new trial. The fixed-window
allocation does not implement a continuous acquisition scheduler or unrestricted
sequential stopping. A reception lasting years does not acquire a fresh
independent false-admission allowance for every preceding day.

## Analytical results already completed

The [accepted information-bound study](theory/README.md) and its
[228-case result](theory/selective_information.json) analyze the exact shared
selective operator and sampled Gaussian FIR covariance under explicit assumptions.
The standalone helper comparison agrees with the C++ selective stage to
6.83×10⁻¹⁵ maximum absolute difference on its 256-sample diagnostic. This is
a software/model check, not a long-span reception result.

A Gaussian with the covariance of a random-covariance mixture gives a **lower**
KL divergence, so that proxy cannot establish impossibility. The study instead
reveals the selective trajectory and upper-bounds the resulting more informative
experiment. For zero-mean proper Gaussian input covariance K, it obtains

\[
D(P_Y\Vert Q)\le
\min\{(1+3q)\operatorname{tr}K,\;(F/2)\operatorname{tr}(K^2)\},
\quad q=(d_s^2+d_f^2)/2,
\]

with the defined conservative operator factor F in the theory report. In the
densely sampled renewing-Gaussian regime, the low-C allowance scales as C²T/B:
10 dB less C requires approximately 100 times the duration to retain the same
upper information allowance. That is a necessary-condition scaling, not an
achievable decoder-duration law or a universal propagation floor. Lowering
waveform rate alone does not remove its premise.

At −60 dB-Hz and a full ten-Julian-year pre-selective source span, the checked
bound excludes both operating targets for all tested depths at 5 µs delay
when B≥.1 Hz, and at both tested delays (5 µs and 7 ms) when B≥1 Hz. At
B=.001 Hz and 5 µs, Off through Each10 exclude the strict profile while the
relaxed profile remains inconclusive. Larger bounds at high AM depth or delay
reflect conservative energy/operator allowances, not measured benefits.
These are optimistic one-completion presence-test exclusions under the stated
law; they neither calibrate full acquisition nor prove correct-bit reception.

The finite sinusoidal AM law retains a coherent ensemble component. With
stable rays and known frequencies/timing it suggests a constrained finite
sideband basis rather than independent gains every AM cycle. The current
receiver does not implement that full continuous-state basis. With evolving
rays, filtering `h(t)s(t)` does not generally commute with multiplying filtered
`s(t)` by `h(t)`; any block-center simplification needs a measured error. This
is a concrete remaining algorithmic direction, not a demonstrated improvement.

## Completed receiver observations

These results come from `local-evidence/triad-selective/primary-audit-preview.json`,
`screen-analysis.json`, `highspread-analysis.json`, the development retention
reports and the two completed `stable850-*-m60.json` receiver reports. They are
observed outcomes, not counts inferred from plans. The accepted primary audit records
`integrity_complete: true` for all 60 export and 60 score batches, all 15 cells
and all 2892 intended paths. The source/binary/default/seed and saved-NPZ binding
is present, and independent review of the repaired auditor has completed.
The final inventory and archive receipt are recorded below. Its
`scientific_qualification` remains false.

The accepted [holdout summary](reproduction/holdout-summary.json) and
[reproducer](reproduction/summarize_holdouts.py) collect all 24 cells from the
two final audits. The [six-panel development figure](screen-curves.png) and
[plot script](reproduction/plot_results.py) show conditional development
curves after verifying 18 score NPZs and 60 manifests. Shading shows the
minimum and maximum conditional reception fractions across sampled physical
paths; it is **not a confidence interval** or a new holdout result.

### Primary actual-noisy holdouts

There are no missing or unscored primary observations. Counts below were
recomputed from saved scores. All wrong-accepted counts were zero at both gates.

| Profile | Mode | C/N₀, dB-Hz | Strict correct /300 | Relaxed correct /300 | Strict / relaxed misses | Strict balanced-mean 95% lower bound |
|---|---|---:|---:|---:|---:|---:|
| g001 | Each65 | −26 | 300 | 300 | 0 / 0 | .990064 |
| g001 | Each65 | −27 | 297 | 298 | 3 / 2 | .958042 |
| g001 | Off | −26 | 300 | 300 | 0 / 0 | .990064 |
| g01 | Each65 | −22 | 300 | 300 | 0 / 0 | .990064 |
| g01 | Each65 | −23 | 299 | 300 | 1 / 0 | .969562 |
| g01 | Off | −22 | 300 | 300 | 0 / 0 | .990064 |
| g1 | Each65 | −10 | 300 | 300 | 0 / 0 | .990064 |
| g1 | Each65 | −11 | 300 | 300 | 0 / 0 | .990064 |
| g1 | Off | −10 | 300 | 300 | 0 / 0 | .990064 |

Each of the three null cells and each of the three wrong-key cells admitted
**0/32** at either gate. For a single cell, 0/32 still permits approximately
8.94% admission probability at the one-sided 95% binomial upper bound; it does
not demonstrate the daily operational target. Controls must not be pooled as
independent observations across their shared roots.

For 300/300, the balanced-mean .990064 bound uses the all-success equal-stratum
AM–GM argument; each individual bit has only 150/150 and a .980227 pointwise
lower bound. This is pointwise evidence for one fixed cell, not simultaneous
99% qualification across all nine cells or both bit strata. With misses, the
reported balanced bounds average per-bit one-sided 97.5% bounds with a
Bonferroni pointwise guarantee. The relaxed bounds at g001 −27 and g01 −23
are .963417 and .990064. Three misses in 300 at g001 −27 do **not** reject
an underlying 99% correct-reception probability; they also do not establish it.

### Development transitions and retained energy

All 48 main score jobs and all ten high-spread score jobs completed with no
missing paths in the plan-bound recounts. Selected main-screen boundary points
show the depth trend. Every entry is **(strict count, relaxed count)**, each
out of 64 conditional draws clustered on four physical paths. The two numbers
are not a numerator/denominator; this is not a 64-path reliability estimate.

| Profile and C/N₀ | Off | Each1 | Each5 | Each10 | Each25 | Each65 | Slow65 | Fast65 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| g001, −27 | (60, 64) | (60, 64) | (60, 64) | (60, 64) | (63, 64) | (64, 64) | (64, 64) | (64, 64) |
| g01, −24 | (33, 50) | (33, 50) | (33, 50) | (33, 51) | (35, 55) | (57, 62) | (46, 59) | (46, 58) |
| g1, −14 | (7, 24) | (7, 24) | (7, 24) | (7, 25) | (9, 29) | (27, 48) | (21, 36) | (14, 36) |
| stable, −34 | (50, 59) | (50, 59) | (49, 59) | (49, 59) | (46, 59) | (43, 54) | (49, 59) | (44, 54) |

Active AM is not energy normalized, so higher counts in some fading cases
cannot be attributed solely to a more resilient waveform. In the stable case,
Each65 instead lowered acceptance at this chosen boundary. Across the main
screen there were zero wrong acceptances in 17,408 conditional signal decisions
and zero admissions in 17,408 conditional null decisions at either gate.
Those reused/clustered decisions are not independent operational tail trials.

The four-path Each65 diagnostics quantify why nominal C/N₀ alone does not
describe an equal-energy comparison:

| Profile | Measured post/pre selective energy | Correct-label retained fraction |
|---|---:|---:|
| g001 | 1.3202–1.4425 | 92.21–95.64% |
| g01 | 1.3622–1.4358 | 90.34–94.22% |
| g1 | 1.3735–1.3916 | 87.87–94.13% |
| stable | 1.0689–1.6398 | 79.00–89.28% |

The lower-rate Each65 cases reached 64 correct acceptances at both gates at
−24 dB-Hz (g01-slowrate) and −14 dB-Hz (g1-slowrate), versus counts
(57, 62) and (27, 48) at the original rates. Their
correct-label projection retention was 99.79–99.86% and 99.72–99.83%, versus
90.34–94.22% and 87.87–94.13% at the original rates. This development result
motivated the holdout test below, which exposed wrong-key admissions. It is
not an isolated rate gain: seed roots changed and rounded active durations
increased by about 11%. In the fast-AM Each65 cases, counts were (61, 64) at
g001 −27 and (8, 20) at g1 −14; D changed with AM rate.

The known-initial-arrival high-spread entries below are **(strict count,
relaxed count)**, each out of 32 conditional draws on two physical paths:

| Profile and C/N₀ | Off | Each25 | Each65 | Slow65 | Fast65 |
|---|---:|---:|---:|---:|---:|
| g10, 6 dB-Hz | (16, 23) | (16, 24) | (24, 29) | (23, 26) | (19, 24) |
| g10, 8 dB-Hz | (32, 32) | (32, 32) | (32, 32) | (32, 32) | (32, 32) |
| g30, 8 dB-Hz | (5, 11) | (6, 12) | (15, 25) | (10, 19) | (9, 16) |
| g30, 10 dB-Hz | (30, 30) | (30, 32) | (32, 32) | (32, 32) | (32, 32) |
| g30, 12 dB-Hz | (32, 32) | (32, 32) | (32, 32) | (32, 32) | (32, 32) |

No strict-gate signal was admitted at or below 4 dB-Hz in either profile.
There were zero wrong acceptances in 2560 conditional signal decisions and
zero admissions in 2560 null decisions at both gates. Correct-label retention
for Each65 was 87.60–90.38% at g10 and 78.86–79.60% at g30, compared with
98.61–98.63% and 89.85–90.28% for Off. These are model-specific sampled
transitions and compression diagnostics, not higher-spread physical floors.

Completed main-screen manifests give these actual durations. The Off capture
span is shown as a concrete sampled support measurement; active-AM filter
tails must be read from their own manifests rather than assumed identical.

| Profile | Requested active, s | Actual rounded active, s | Off sampled capture, s |
|---|---:|---:|---:|
| g001; stable; g001-fastam | 86400 | 96002.4 | 98696 |
| g01 | 86400 | 86421.6 | 86693.5 |
| g1; g1-fastam | 8640 | 8661.6 | 8690.875 |
| g01-slowrate | 86400 | 96002.4 | 98674 |
| g1-slowrate | 8640 | 9602.4 | 9871.6875 |
| g10 aligned | 120 | 123.2 | 125.525879 |
| g30 aligned | 120 | 123.2 | 125.526855 |

### Long-span result and compression refinement

Both 850-day modes completed **16/16 correct accepted observations at the
strict gate**, hence also at the relaxed gate, with zero wrong acceptances.
Each capture contained 18,366,719 complex sample rows spanning 73,466,876
seconds (850.311065 days); the actual rounded active span was 73,440,183.6
seconds (850.002125 days). This is genuinely sampled continuous model time,
not a repeated waveform. It remains a 16-path stable-ray control, not 99%
reliability qualification or evidence for renewing Gaussian fading at −60 dB-Hz.

The four diagnostic paths per mode show the information loss directly:

| Quantity | Off | Each65 |
|---|---:|---:|
| Correct-label retained fraction of sampled signal energy | 99.9966–99.9980% | 66.2684–75.9031% |
| Wrong-label retained fraction | 5.6219–5.6756% | 3.7931–4.4485% |
| Measured post/pre selective-stage energy ratio | Exact bypass | .903737–1.497264 |

Successful long reception therefore coexists with substantial discarded
sideband energy. These fractions are diagnostics, not a measured dB sensitivity
penalty or proof that all missing energy can improve bit discrimination.
The static scoring reports took 5.89 and 5.91 seconds for all 16 retained
workspaces in Off and Each65 respectively; this excludes sampled export and
is not end-to-end simulation or acquisition latency.

A follow-up re-export halves block duration to 4096 seconds on the first four
existing paths per mode. All eight re-exports passed the complete-capture
sufficient-statistics aggregation audit, and all eight strict receiver decisions
were accepted (4/4 per mode). They reuse these physical paths rather than
adding independent holdouts. Correct-label retained energy increased by only
.002796–.006289 dB for Each65 and .000045–.000088 dB for Off. This refinement
does not materially recover the discarded AM sidebands. The audit binds
metadata and aggregated statistics, not raw-I/Q identity or field performance;
no sensitivity improvement is demonstrated.

A full selective sideband basis would require 52 columns for the two bit
alternatives and two rays under the proposed 13-component construction. A
direct per-sample CPU Gram accumulation would increase the current four-column
quadratic term count by 169×, to roughly 50 billion terms per long capture.
This is a cost estimate, not a benchmark or proof against a structured
implementation. Correct treatment of unknown TX/RX phase and the order of
time-varying multiplication/filtering remains unresolved. No such new full
basis receiver is implemented in this iteration.

### Lower-edge and slower-rate actual-noisy follow-up

The [follow-up export plan](reproduction/followup-export-plan.json) and
[score plan](reproduction/followup-score-plan.json) completed all nine cells:
1500 signal observations and 128 controls. The plan-bound
`local-evidence/triad-selective/followup-audit-preview.json` records complete
coverage, no missing observations and these saved-score counts. All modes
are Each65. Bounds below retain the pointwise equal-bit-population assumptions
and method already described for the primary audit.

| Profile | GUI rate, Hz | C/N₀, dB-Hz | Strict correct /300 | Relaxed correct /300 | Strict / relaxed misses | Strict / relaxed balanced-mean 95% lower bound |
|---|---:|---:|---:|---:|---:|---:|
| g001 edge | .036 | −28 | 262 | 287 | 38 / 13 | .809370 / .911145 |
| g01 edge | .36 | −24 | 265 | 289 | 35 / 11 | .820871 / .920142 |
| g1 edge | 3.6 | −13 | 283 | 295 | 17 / 5 | .893515 / .948274 |
| g01 slower rate | .036 | −24 | 300 | 300 | 0 / 0 | .990064 / .990064 |
| g1 slower rate | .36 | −14 | 297 | 299 | 3 / 1 | .959187 / .969562 |

The accepted independent [confidence recount](reproduction/followup-confidence.json)
and [reproducer](reproduction/followup_confidence.py) bind all 1628 manifests
and score rows, including admitted-control identities. At the three original-rate
edge points, the strict-gate balanced-mean one-sided 95% **upper** bounds are
.921889, .929941 and .974302, respectively. All are below .99: those fixed
simulated cells fail the strict reception target at that confidence level.
Their relaxed-gate **lower** bounds are .911145, .920142 and .948274, each
above .90. This supports the relaxed correct-reception target at those points
under the stated stratum assumptions, not an independently measured daily
false-admission rate or a universal channel boundary. Each endpoint is
separately pointwise one-sided 95%; lower and upper endpoints together are
not a joint 95% interval, and no simultaneous claim across cells is made.

Wrong-accepted bits within the correct-key signal observations were zero at
both gates. However, the separate wrong-key controls failed materially:

| Slower-rate configuration | Null strict /32 | Null relaxed /32 | Wrong-key strict /32 | Wrong-key relaxed /32 |
|---|---:|---:|---:|---:|
| g01, rate .036, −24 dB-Hz | 0 | 0 | **6** | **10** |
| g1, rate .36, −14 dB-Hz | 0 | 0 | **7** | **13** |

The wrong-key counts correspond to 18.75%/31.25% and 21.875%/40.625%
observed strict/relaxed admission rates, respectively, in these 32-path
controls. They are not corrected by the successful noise-only controls or
by the higher correct-key reception rates. The analytical noise-only gate
does not promise wrong-key rejection. These failures
prevent treating the slower configurations as a validated general
improvement or changing a default planner/receiver to prefer them. In
particular, the 300/300 slower g01 signal result does not establish usable
encrypted-message discrimination by itself.

The independently accepted [wrong-key diagnostic](reproduction/wrong-key-diagnostic.json)
and [reproducer](reproduction/wrong_key_diagnostic.py) bind six selected raw
workspaces and 14 saved score-report/NPZ pairs. Its three focused self-checks
passed, with an additional dense Gaussian log-LR variance check agreeing
to 8.88×10⁻¹⁶. For the selected slower-rate path 90001, the best fixed
wrong-key two-ray subspace retains about 99.22% (g01) and 98.76% (g1) of
the actual signal energy when complex gains may vary freely per block.
Selected original-rate wrong-key examples retain about 65.76% and 57.23%.
The slower examples also have positive known-variance expected log-LR under
their fixed noiseless wrong-key waveform, whereas the original-rate examples'
expectations are negative. This explains why a noise-only presence statistic
can admit these structured wrong-key signals. These are optimistic subspace
and model diagnostics, not an isolated causal rate experiment: the original
and slower examples differ in seeds, C/N₀ and rounded duration.

A post-hoc requirement that the winning bit log-LR exceed the other by 20,
in addition to the original strict gate, gives 296/300 and 282/300 correct
acceptances for the slower g01/g1 samples, with 0/32 wrong-key admissions
in each. It also loses correct receptions. This reused-data tradeoff neither
qualifies a new gate nor proves a wrong-key admission bound; the primary
gates and results remain unchanged. The open design work is to distinguish
the keyed pattern from plausible structured signals, constrain channel
flexibility appropriately, and validate a minimum useful pattern rate or
complexity under the actual fading law.

### Matched receiver alternatives

All 25 jobs completed, covering 30 alternative-method curves plus the five
existing baselines. The accepted recount verifies the frozen options and
matched workspace receipts. These entries are **(strict count, relaxed count)**,
each out of 64 conditional draws on four reused physical paths. B refers to
the receiver's assumed width; the physical channel is unchanged.

| Each65 case / C/N₀ | B, J64 baseline | B, J16 | B, J128 | B/2, J64 | 2B, J64 | OU | Static FFT |
|---|---:|---:|---:|---:|---:|---:|---:|
| g001 / −28 | (57, 64) | (54, 61) | (59, 63) | (53, 56) | (54, 62) | (56, 61) | (0, 0) |
| g01 / −24 | (57, 62) | (52, 60) | (55, 62) | (50, 59) | (46, 58) | (55, 63) | (0, 0) |
| g1 / −14 | (27, 48) | (24, 44) | (32, 48) | (17, 32) | (18, 37) | (25, 45) | (0, 0) |
| g1 slower rate / −14 | (64, 64) | (64, 64) | (64, 64) | (61, 64) | (64, 64) | (64, 64) | (0, 0) |
| g1 fast AM / −12 | (58, 62) | (56, 61) | (59, 63) | (38, 46) | (49, 60) | (59, 63) | (0, 0) |

Longer covariance windows sometimes changed a few decisions, but these
samples do not identify one uniformly superior choice. The mismatched narrow
width lost detections in several selected cells. Static whole-observation
coefficients were ineffective at these selected evolving-channel boundaries;
this does not contradict their success on the separate stable-ray long-span
control. The slower-rate row still has the wrong-key limitation above, which
this conditional signal/null comparison does not test. Selection after viewing
these rows cannot be treated as fresh independent validation.

The accepted [comparison summary](reproduction/comparisons-summary.json) and
[reproducer](reproduction/summarize_comparisons.py) retain all matched outcomes,
stage timings, workspace receipts and the long-refinement comparison. In the
CPU FP32 / CUDA FP32 / CPU FP64 check, 768 paired conditional trials had
identical winning bits and strict-gate decisions for every device/precision
pair. The largest absolute log-score difference was 4.2219×10⁻⁵. Reported
wall times were 26.56–28.06 seconds under shared contention; draw/calibration
work dominated. These timings do **not** demonstrate GPU acceleration or an
isolated throughput ranking. They establish numerical agreement for the
tested arrays, not all possible near-threshold cases or peak-memory behavior.

Five selected existing actual-noisy captures were also replayed on CPU FP64
from the retained raw statistics. The [replay result](reproduction/replay-summary.json)
and [script](reproduction/replay_selected.py) report maximum absolute score
difference 4.5729×10⁻⁶ relative to the original GPU FP32 scores, with all
winners and strict/relaxed decisions unchanged. This reproduces the selected
wrong-key admissions as well as signal decisions; it is not a new performance
sample or a regeneration of the physical channel.

## Final binding and validation status

The study owner confirms **412 bounded cloud jobs exited successfully and
4772 sampled workspaces were produced**. Successful execution does not mean
every scientific target passed: the wrong-key failures and remaining scope
limits above remain explicit.

The retained archive is `local-evidence/triad-selective/selective-final.tar.gz`,
185,675,334 bytes, SHA-256
`6bcdc7ee6d210d4b57c5aad7abdbb15fd987e12680d3bd7055053e5aff56b3b4`.
The [verification receipt](reproduction/archive-verification.json) and
[verifier](reproduction/verify_archive.py) record the source bundle, before/after
source inventory, binary, manifests, job receipts and scorer input binding.
The repaired verifier passed independent review, and the maintained receipt
records the accepted result.

The recorded scope is precise: 15,893 retained files were rehashed locally;
33,404 raw array files totaling 15,461,611,136 bytes were hashed remotely.
Of those arrays, 189 files totaling 96,764,008 bytes from 27 selected workspaces
are retained. The other 33,215 raw files cannot be locally rehashed after
remote deletion. All 29,304 scorer-input hash receipts match the full remote
inventory. Retained scores, manifests and source fingerprints support a
broader recount than the selected raw replays; hashes of omitted raw bytes
do not make those bytes locally available. The built binary SHA-256 is
`7d6cc5cd6353f2d09f3aabf6203c8c51d90a12e6a6ab4ddc2daef652ccc712f3`.

| Evidence set | Planned scope | Current report status |
|---|---|---|
| Main development screen | 48 cases / 192 exports, conditional signal/null draws | 48/48 score jobs complete; selected transitions and retention reported above |
| High-spread controls | 10 cases / 20 exports | 10/10 score jobs complete; known-initial-arrival results above |
| Actual holdouts | 9 signal cells / 2700 signal observations; 192 controls | All 15 cells complete; repaired auditor independently accepted |
| Long controls | 32 paths / two 850-day modes at −60 dB-Hz | 16/16 per mode at both gates; four-path retention and sampled durations reported |
| Long block refinement | Eight re-exports of existing paths at 4096-second blocks | 8/8 aggregation audits and receiver decisions passed; negligible correct-label retention change |
| Receiver configurations | 25 jobs / seven configurations over five cases | 25/25 complete, accepted recounts; selected paired outcomes above |
| CPU/GPU comparison | 768 paired conditional trials | All rank/strict-gate decisions agree; contended timing does not demonstrate acceleration |
| Lower-edge follow-up | Five 300-path signal cells and 128 controls | All nine cells complete; slower-rate wrong-key failures remain unresolved |
| Local Added Symbols probe | 32 local exports on selected reused roots | Independently accepted diagnostic; fewer wrong-key admissions and fewer strict signal acceptances, not a qualified fix |
| Provenance/archive | Baseline plus study patch | Archive retained; inventories bound; repaired final verifier independently accepted |

The following focused checks have completed, as recorded by the study owner:

- Local `./build.sh test triad`: 14/14 passed in 18.68 seconds.
- Remote isolated triad group: 14/14 passed in 7.19 seconds.
- Conditional-sweep focused suite: 14/14 passed in .427 seconds.
- Low-rate campaign suite: 15/15 passed.
- Theory suite: seven tests passed; independently reviewed and accepted.

Earlier failures remain part of the evidence history. An incomplete initial
upload caused a bootstrap failure before the experiments; the transfer was
repaired before the reported runs. The outer SSH wrapper later exited 143
after all 25 alternative jobs had completed with exit zero; their individual
completion receipts and saved scores establish the scientific-job outcome.
The first holdout auditor crashed on a dict/Namespace mismatch in the real
workflow and read the wrong FHSS metadata field. Review also found that
generation-binding errors did not prevent completion/confidence claims and
that empty input inventories could pass. Those fail-open paths were repaired,
with 15 focused tests including actual end-to-end and negative-CLI checks,
before the final audits were accepted. Preliminary audit status is not
substituted for the repaired source/default/seed/raw-receipt checks.

The theory report records its initial strict-JSON serialization failure and
the native-scalar/full-report regression repair. This failure was not a change
to the physical law or reception threshold. Successful software checks do not
resolve the pending reception and operational calibration qualifications.

The plan-bound [screen analyzer](reproduction/analyze_screen.py) checks report
options, planned inputs, score NPZ hashes, manifest receipts, saved-count
recounts and missing/failed observations. Its default receipt validation does
not imply every large input array was locally rehashed; the final archive audit
must state which checks were actually performed. Raw sampled evidence belongs
in ignored `local-evidence/`, with compact summaries and source/hash provenance
retained here after completion.

## Local Added Symbols ×10 diagnostic

The [Added Symbols probe](reproduction/added-probe-summary.json) and
[reproducer](reproduction/added_probe.py) completed 32 local sampled exports
and 32 actual-noisy CPU FP64 comparisons. All exports exited successfully.
This post-hoc diagnostic uses the first eight roots from each slower-rate
profile/control cell, comparing the existing ×1 results with ×10 and
interleaving enabled. Signal winning-bit decisions were unchanged. Original
admission gates remain unchanged. Independent review accepted the probe after
checking all 32 manifests and their raw/score hash bindings, four score NPZs
and the current gates, with zero discrepancies.

| Profile / control | Baseline strict /8 | Added10 strict /8 | Baseline relaxed /8 | Added10 relaxed /8 |
|---|---:|---:|---:|---:|
| g01 slower rate / correct-key signal | 8 | 7 | 8 | 8 |
| g01 slower rate / wrong-key admission | 2 | 0 | 2 | 0 |
| g1 slower rate / correct-key signal | 8 | 6 | 8 | 7 |
| g1 slower rate / wrong-key admission | 3 | 0 | 3 | 0 |

Added Symbols increases the fine-symbol-rate/Gaussian-width ratio R/B from
.6 to 6 in these configurations. It also changes source geometry and energy
allocation, despite fixed requested duration and sample rate:

| Quantity | g01 baseline → Added10 | g1 baseline → Added10 |
|---|---:|---:|
| Actual rounded active seconds | 96002.4 → 89603.2 | 9602.4 → 8963.2 |
| Nominal payload-energy lower allocation, seconds | 64000 → 85333.3333 | 6400 → 8533.3333 |
| Tiles | 6 → 8 | 6 → 8 |
| Ideal triplet bandwidth, Hz | .0225 → .225 | .225 → 2.25 |

This is a promising identification tradeoff, not an equal-energy sensitivity
gain or qualified fix. Correct-key acceptance fell at the strict gate, while
the observed wrong-key admissions disappeared in these selected eight-path
samples. For 0/8, the simple one-sided 95% binomial upper bound is still
approximately 31.23%, under its iid assumption; that is far from a rare
admission qualification. No new null controls were run. The source/channel/noise
roots are paired where verified, but waveform, support and useful-energy
allocation change; the old GPU FP32 versus new CPU FP64 arithmetic is another
declared difference. These selected roots are not an independent holdout.

Two preflight harness bugs were repaired before execution. The probe uses the
existing transmitter's Added Symbols/interleave implementation; no new runtime
waveform or receiver code was introduced. The sum of per-export wall times was
200.78 seconds with two workers; the combined reported scoring wall times
totaled 59.47 seconds. Neither summed quantity is end-to-end elapsed latency.
All local processes completed. All raw probe evidence remains in
ignored `local-evidence/triad-selective/added-probe`. This local diagnostic is
outside the 412 cloud jobs, 4772-workspace cloud inventory and archive above.

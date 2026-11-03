# Broad configuration coverage of the isolated three-carrier modem

This study separates **configuration validity, capture feasibility, acquisition
coverage and correct admission**. The factorized census classifies all
2,500,485,120 requested configuration coordinates under stated assumptions.
It does not simulate or decode that many waveforms. Selected sampled-waveform
experiments, paired numerical repairs and full timing-bank comparisons are
recorded below. Independent long-span validation is reported separately.

Start future work with the [development workflow](../../../triad-development-workflow.md)
and [v3 guide](../../../triad-v3.md). Reuse retained complex workspaces for
receiver comparisons. A historical GPU or long-duration campaign is evidence,
not a routine regression prerequisite. Ordinary Robust integration, native audio
and release qualification remain outside this study.

## Complete configuration census

The [factorized census](evidence/factor-census.json) records the full axis values,
source and executable hashes, native receipts used, and the assumptions needed
to reduce the Cartesian product. It includes the 17 rates, three nominal
baseband references, nine shifts, two arrival policies, four base oscillators,
seven shift oscillators, ten spread presets, 16 selective-modulation settings
and 19 C/N₀ levels requested by the user. The additional axes are margins 1/3,
Added Symbols ×1/10/100/1000, FHSS off/on and whole-bit Symbol Burst off/×3.
The primary grid has 78,140,160 coordinates; these added axes expand it by 32.

| Exclusive result, in evaluation order | Configuration coordinates |
| --- | ---: |
| Unsupported zero-shift or shared-baseband-clock topology | 595,353,600 |
| Represented topology, source geometry rejected | 812,482,560 |
| Geometry accepted, deterministic capture margin ≤ 0 | 212,171,328 |
| Geometry accepted, deterministic capture margin > 0 | 880,477,632 |
| **Total** | **2,500,485,120** |

The unsupported-topology count comprises 277,831,680 zero-shift coordinates
(including their shared-clock overlap) and 317,521,920 positive-shift,
shared-baseband-clock coordinates. These are CLI/model omissions, not physical
impossibility results.

The source-geometry factor is measured with 136 native configurations:
17 rates × four Added Symbols factors × two FHSS settings. Of these, 78 pass,
24 have no protected FHSS dwell cells, 32 exceed the bounded tile workspace,
and two exceed the automatic sample-rate integer range. Under these census
settings, every 1-MHz and 30-MHz rate coordinate fails source geometry before
reception. Lower-rate failures depend on spreading and FHSS: a fine symbol that
cannot fit the protected portion of a 400-ms dwell cannot be repaired by a
receiver threshold change.

For accepted geometry the census evaluates the native deterministic capture
predicate using the actual native sample rate and nominal triplet bandwidth.
The independent validation set contains 96 native calls: six directed near-zero
cases and 90 seeded cases. All 62 accepted configurations match the calculated
margin exactly; the remaining 34 reject source geometry. An additional 450
near-zero factor rows are rechecked. The predicate uses strict positivity;
rounding a tiny negative margin to zero must not turn it into a pass.

These counts assume an eight-second requested active duration, automatic sample
rate, interleaved Added Symbols, no internal burst waits, zero explicit residual
CFO/drift and diurnal phase terms, and Gaussian Doppler width `B=0`. The selected
endpoint oscillator bounds still contribute to the capture calculation. Timing,
C/N₀ and whole-bit burst replicate this predicate without changing its value.
Changing these assumptions requires a new census. A positive margin does not
certify pulse-shaping alias loss, filter allocation, analog passband compatibility,
search resolution, receiver sensitivity or sufficient duration.

Additional model distinctions matter across the whole grid:

- `--nominal-if-hz` is an oscillator-error reference. The emitted simulation
  remains zero-centered complex baseband; it does not render a real-audio
  carrier at 1.725 or 8.270 kHz or model a shared RF/baseband clock.
- The FHSS cases here exercise transmitter envelope shaping and declared
  receiver-I/Q losses. They do not qualify physical RF retuning.
- Whole-bit Symbol Burst changes the transmission calendar. It does not by
  itself demonstrate receiver backlog clearance or within-bit processing pauses.
- The `none` spread preset bypasses the additional modulation stage. Selecting
  an active modulation label alongside `none` does not test that modulation;
  this affects 234,420,480 requested coordinates, overlapping the table above.
- Stable ray-center Doppler separation is distinct from stochastic Doppler
  width. This census does not turn a preset separation into diffuse fading.

## New controls for meaningful arrival and missed-hop tests

`triad_long --arrival-prior-seconds MIN MAX` selects an explicit uniform
physical initial-arrival prior independently of the public `--start` bank.
It overrides initial clock/audio timing only, preserves oscillator draws and
keeps the capture support large enough for both the physical prior and public
bank. Equal endpoints provide a declared fixed-arrival diagnostic. This makes
the several-second prior explicit even in paths where a clock-policy label alone
previously did not inject that uncertainty. Public coverage metadata is not a
claim that the timing grid is fine enough to avoid correlation loss.

`--missed-dwells LIST` accepts sorted, unique nonnegative receiver dwell indices
(at most 4,096) with FHSS enabled. The receiver erasure is applied after channel,
noise and front-end state advance. It zeros the delivered observation and marks
it invalid without resetting propagation or suppressing later echoes. Empty
lists preserve the previous path exactly. Focused tests cover adjacent and
separated losses, calendar origin, clock/retune offsets, chunk boundaries and
invalid inputs.

`--minimum-observed-fraction F` changes the C++ run-mode coverage gate in
`(0,1]`, default 0.9. It does not change likelihood or admission thresholds.
Report post-erasure signal energy when comparing loss schedules: the channel's
pre-erasure energy report alone cannot measure the receiver's retained energy.

The isolated `triad` group passed all 14 registered tests locally and in the
remote build. This is implementation validation for the affected scope, not a
reliability or ordinary-receiver qualification.

## Benchmarks and consequential algorithm choices

The [source-bound analytical report](evidence/coverage-bounds.json) covers all
19 requested C/N₀ levels. Its [derivations and primary sources](evidence/theory.md)
distinguish known-mean bit classification, signal presence and correct admission.
They must remain separate when comparing a practical receiver with a bound.

For proper-complex white noise and a receiver favorably given the exact waveform,
realized channel, phase, alignment and noise variance, the signal-presence energy
requirement is

\[
\rho \ge \tfrac12[\Phi^{-1}(P_D)-\Phi^{-1}(\alpha)]^2.
\]

Giving one completed test the entire horizon false-admission allowance yields:

| Target | Required useful energy / N₀ | At −60 dB-Hz and unit useful-energy efficiency |
| --- | ---: | ---: |
| 99% detection, α = 10⁻⁴ | 18.2732 | 211.50 days |
| 90% detection, α = 0.01 | 6.5085 | 75.33 days |

These favorable presence floors are not achieved or sufficient triad durations.
Repeated attempts, multiple durations, model selection and unknown parameters
require their own complete family accounting. The report also computes a chosen
Bonferroni allocation; that allocation is a design choice, not a universal
necessary search penalty. At unit useful-energy efficiency, 38 days at −60 dB-Hz
does not reach either presence target even under the favorable known-waveform
assumptions.

With active-time C/N₀ `c`, retained energy fraction `η` and duty cycle `d`, the
coherent reference is `ρ = c η d T_wall`. A 20-dB reduction in C/N₀ therefore
requires 100 times the time in that reference. Actual channel gain, shaping,
outages and capture losses must be measured rather than normalized away.
Rate or spreading changes cannot create additional energy at fixed power and
duration, though they can change distinguishability and model loss.

The optional continuous zero-mean Gaussian stress law has a different
low-C/N₀ necessary scaling, approximately `T ∝ B/c²` when `BT` is large.
Its information exclusions do not apply to stable `B=0` rays, arbitrary
selective modulation, or every physical channel. Coincident numeric values of
`B` and a preset ray separation do not make their laws equivalent.

A retained-template harmonic diagnostic also evaluated whether adding every
selective-modulation sideband as a freely fitted channel coefficient is useful.
For its Each65 case, the simple two-ray model retained median 71.0% of signal
energy; a 26-column expansion recovered the constructed signal to numerical
precision. However, a known-noise rank-26 projector needed energy 46.68 for
99%/10⁻⁴ detection, versus 23.92 at rank two. Recovering about 1.5 dB of typical
energy did not justify that nuisance-dimension cost in this example. The more
consequential comparison is physically constrained long coherent accumulation
versus unnecessary independent short-window gains. This is an algebraic
diagnostic, not a measured receiver improvement or a universal rejection of
sideband modeling.

Very high C/N₀ also tests representation. Approximate FP32 scale warnings are
144.5 dB sample SNR for adding noise to signal and 72.2 dB for covariance or
subtractive-residual operations. These are numerical-risk indicators, not
physical sensitivity boundaries. Direct residuals and structured likelihoods
may preserve evidence without forcing all bulk processing to FP64. Actual
CPU/GPU ranking, admission and timing comparisons are still required.

## Sampled-waveform campaign and numerical repair

The frozen core plan contained 83 selected intent cases and 1,543 jobs. Its
81 native configure jobs completed 78 cases and rejected three expected geometry
cases; 172/178 exports completed, with six dependency failures. This is selected
diagnostic coverage of the axes, not an exhaustive cross-product reception test.
The [baseline summary](evidence/baseline-summary.json) preserves the original
results; the [paired summary](evidence/paired-summary.json) includes completed
conditions retained inside failed jobs and supersedes baseline summaries that
omitted those partial conditions.

The core receiver work comprised 356 actual-noisy jobs, 912 conditional jobs
and 16 arithmetic comparisons. The corrected rescore reused exactly the same
observations, seeds, gates and declared searches:

| Paired job transition | Jobs |
| --- | ---: |
| Completed → completed | 1,232 |
| Numerical processing failure → completed | 15 |
| Unavailable export dependency → unchanged | 12 |
| Unsupported receiver frequency grid → unchanged | 25 |

The two-ray basis uses the preset's declared effective delay and ray-center
Doppler values. It fits unknown complex gains; it does not blindly estimate an
arbitrary delay/Doppler scattering function. Three separate renewed-Gaussian
stress cases exercise stochastic widths. Do not generalize the stable-ray
results to every channel having the same numeric Doppler/delay spread label.

The old static receiver attempted Cholesky on `I + G` where extreme signal scale
rounded away the identity in nominal null directions. The old Gaussian-window
path also retained inconsistent numerical null modes. The repaired paths first
whiten the observable Gram subspace, use small rectangular SVDs, and evaluate
`λ/(1+λ)` and `log1p(λ)` without subtracting nearly equal large matrices.
`B=0` uses the exact rank-two prior root. Rank truncation, discarded information,
whitening error and a consistent variance inflation of approximately
`1.0000355271` (0.000154 dB) are exposed. This is a finite-precision engineering
guard, not a formal IEEE rare-tail proof.

FP32 FFT/GEMM bulk operations remain available. The small rank/whitening
preparation and sensitive evidence reductions are explicitly FP64. The fix did
not lower admission thresholds, add arbitrary noise floors, force all work to
FP64 or change the original signal decisions in this sweep.

At the strict gate, the old actual-noisy controls admitted 1/16 method/capture
evaluations, and conditional null controls admitted 96/3,512 completed trials.
The corrected results admitted 0/16 and 0/3,572 respectively; the additional 60
null trials are the 15 recovered four-trial conditions. These are **reused and
correlated diagnostic evaluations**, not thousands of independent physical
captures or empirical qualification of 10⁻⁴ per day. All 97 previously observed
null admissions disappeared in the paired repair.

Consequential failures remain. Both methods admitted the same strong Aurora
wrong-key control at 80 dB-Hz. Gaussian-window reception admitted one wrong bit
in the actual renewing HF-worst case. The conditional nominal-bank sweep still
contains 67 wrong-bit admissions across repeated high-C/N₀ HF-worst and
free-running-base observations. These are unresolved acquisition/model failures,
not evidence that noise-only calibration provides authentication. Never replace
wrong-key or wrong-bit controls with null captures.

The [raw wrong-key diagnosis](evidence/wrong-key-diagnosis.md) found no same-key
substitution or statistics-extraction discrepancy. Independent raw SVD
projections agree with retained statistics to about 3×10⁻¹⁴. The 3.906-ms
four-column calibration span absorbs 99.8968% of this smooth narrowband
wrong-key waveform; its residual UCB is far below the structured signal's
power. This explains the very large scores under a white-noise null that does
not describe wrong-key interference. It is not a claim that wrong-key rejection
is inherently impossible. The existing optional consistency guard rejects this
capture, with its prior measured legitimate-reception cost retained in the note.

## Complete timing banks and missed hops

The [acquisition comparison](evidence/acquisition-summary.json) uses one sampled
capture per condition, all public timing groups, and unchanged gates. It covers
rate 360/None/Off, rate 360/HF-worst/Each65, rate 36/Each65 with the default wide
arrival interval, and FHSS with no losses or adjacent losses across the selected
cycle boundary. Each of the five cells has two signal bits and one null.

Both complete-bank short-window and global two-ray fits accepted **10/10 correct
signal bits, zero wrong bits and zero of five nulls** at 20 dB-Hz. The paired
nominal-timing controls missed the selected signals; those earlier misses must
not be called sensitivity boundaries. The global fit sums complex observations
and Grams before fitting its constant channel, retaining all timing groups. Its
channel assumption is suitable for the declared stable-ray case, not every
renewed or phase-unstable path. The two methods are compared on the same data,
so their successes are not independent trials.

The subsequently frozen [lower-C/N₀ paired sweep](evidence/sensitivity-summary.json)
changed only noise density at −6/0/6/12 dB-Hz, preserving physical paths, public
timing banks and seeds. It completed 24 configure jobs, 24 actual sampled
exports and both sets of 24 scores. At the strict gate:

| Cell | C/N₀ | Short windows: correct/2 | Global fit: correct/2 |
| --- | ---: | ---: | ---: |
| HF-worst / Each65 / rate 360 | −6, 0 | 0, 0 | 0, 0 |
| HF-worst / Each65 / rate 360 | 6 | 0 | 1 |
| HF-worst / Each65 / rate 360 | 12 | 1 | 2 |
| Default wide timing / Each65 / rate 36 | −6, 0 | 0, 0 | 0, 0 |
| Default wide timing / Each65 / rate 36 | 6, 12 | 2, 2 | 2, 2 |

The global HF-worst result at 6 dB-Hz was 2/2 under the relaxed gate. There were
no wrong-bit admissions or admissions of the eight null controls with either
method. These two-path cells show a useful algorithm choice, not a measured
population sensitivity gain in dB. The physical windows differ with sample
rate; do not infer that one fixed number of blocks has constant coherence time.
At 20 dB-Hz, global receiver time was 0.20–0.30 seconds per selected capture,
versus 0.38–12.84 seconds for short windows. This excludes waveform export and
process startup and reflects a changed channel model, not an exact algebraic
speedup of the same likelihood.

The original six 1,077-group FHSS exports exceeded their 180-second bounds.
A separate predeclared 455-group plan preserved the full arrival interval and
allowed approximately 3 dB nearest-grid mismatch in the native random-chip/RRC
triplet approximation. All six completed in approximately 143–146 seconds.
The predicted group-work reduction is not a measured speedup over a completed
fine-grid export. A proposed 1-ms grid would cross a triplet autocorrelation
zero; it cannot be labeled a 3-dB timing grid. See the
[resource followup](acquisition/coarse_fhss.py).

The original nominal-bank erasure experiments also include middle, random-ten
and random-25 dwell losses. Their post-erasure energy ratios were approximately
96.5–97.4%, 88.0–89.3% and 74.9–76.8% respectively; cycle-edge losses retained
96.7–97.7%. Nominal timing missed even the no-loss controls, so those experiments
alone establish energy accounting and masking, not reception resilience. Only
the separately completed full-bank cycle-edge comparison establishes the small
diagnostic reception result above. Physical RF retuning remains outside scope.

## Long observations, coherence and computational cost

The initial eight long captures use rates .001/.01 Hz, Sub9-best and Sub9-worst,
Each65, paired OCXO presets, 500-kHz RF shift, half-rate IF reference and margin
one at −60 dB-Hz. The 850-day request rounds to approximately 853.334 days at
rate .001 and 850.006 days at rate .01 before complete capture tails. These are
genuinely sampled continuous simulation calendars, not short-capture energy
scaling or 850 days of physical device measurement.

Static-frequency reception accepted all eight initial bits. The independent
short-window model accepted none, including after its revised conservative
memory bound required an explicit 0.5→2-GiB cap increase. That cap increase
resolved resource preflight; it did not improve sensitivity. Actual available
energy/N₀ in the initial paths ranged approximately 86–132. Added modulation
changes energy and was not renormalized away.

The [long followup recipe](long_followup/README.md) separates 128 fresh signal
paths (32 per configuration), four fresh nulls and four fresh wrong-key controls
from a conditional throughput benchmark. All 136 native exports and receiver
jobs completed. Each configuration accepted **32/32 correct bits**, with zero
wrong bits or misses; all four nulls and four wrong-key controls were rejected.
The [independent recount](evidence/long-summary.json) verifies raw scores,
input hashes, source bindings, complete planned denominators and both gates.

| Fresh configuration | Available post-channel energy/N₀ range | Minimum winning score, strict gate 22.721 |
| --- | ---: | ---: |
| Rate .001 / Sub9-best / Each65 | 70.35–150.87 | 30.127 |
| Rate .001 / Sub9-worst / Each65 | 69.27–135.05 | 25.886 |
| Rate .01 / Sub9-best / Each65 | 74.18–149.04 | 22.852 |
| Rate .01 / Sub9-worst / Each65 | 72.51–137.24 | 24.995 |

The near-threshold successful path is retained. Do not round these results into
a duration guarantee or attribute the energy range solely to decoder gain.
The native selected phase/channel laws remain assumptions over the long
calendar; these tests do not validate years of real GPSDO stability or propagation.

For each complete equal-bit 32-path design, the one-sided 95% lower bound on
mean success is 91.06%. This supports the relaxed 90% reception component
**pointwise under the simulated path law**. It does not establish 99%, 90% for
each bit separately (16/16 gives an 82.93% lower bound), or simultaneous
qualification across all configurations. Four nulls cannot empirically qualify
the operational false-admission target. Even with no failures, a predeclared
single-model 99%/95%-confidence all-success demonstration needs at least 299
independent paths; a simultaneous multi-model claim needs further allocation.

The 136 captures processed 3,755,584,070 complex samples and retained 4.25 GB of
statistics. The four-case pilot took 266 seconds with four native workers; the
remaining 132 exports took 1,431 seconds with 20 workers. All 136 actual scores
took 110 seconds with eight workers. The receiver consumes retained projections,
not 850 days of raw samples resident in memory. These measured costs are why
full long-span campaigns are optional research validation, not default tests.

The larger conditional benchmark used one rate-.001/Sub9-worst path, 32 draws
per signal/null condition, four C/N₀ levels and 52,431 residual-frequency bins.
All four CPU/GPU FP32/FP64 implementations selected the same candidates and
admissions. Relative to CPU FP64, maximum score deviations were 3.35×10⁻⁵ for
CPU FP32 and 7.67×10⁻⁵ for GPU FP32. TF32 was disabled.

| Arithmetic/backend | Whole bounded job | Sum of receiver-scoring intervals |
| --- | ---: | ---: |
| CPU FP32 | 203.0 s | 8.30 s |
| CPU FP64 | 204.2 s | 10.91 s |
| GPU FP32 | 197.2 s | 2.89 s |
| GPU FP64 | 294.3 s | 11.13 s |

GPU FP32 accelerated the scoring portion 2.87× versus CPU FP32, but complete-job
speedup was only 1.03× because conditional simulation/calibration dominated.
These single-run measurements do not establish a stable 3% throughput benefit.
The four jobs ran sequentially on one RTX PRO 6000 Blackwell Server GPU; CPU
jobs used one declared numerical thread. Small earlier captures were slower on
CUDA overall. Choose CPU for those cases rather than requiring GPU availability.

On that one fixed physical path, strict accepted-correct counts at −66/−63/−60/
−57 dB-Hz were 0/32, 2/32, 32/32 and 32/32, with no wrong bits or null admissions.
These are conditional noise draws. They locate a boundary for that path and
duration, not a general sensitivity boundary or a qualified duration planner.

For scale, the known-waveform presence reference given the same chosen
per-component family allowance needs energy/N₀ approximately 37.331 for 99%
detection, equivalent to 432.07 days at −60 dB-Hz and unit energy efficiency.
The 850-day nominal active energy is 73.44, 2.94 dB above that reference; actual
post-channel energy, phase loss, unknown frequency/channel and finite-pattern
effects differ. Relative to the more favorable one-completion floor of 18.273,
the nominal gap is 6.04 dB. Neither comparison demonstrates near-optimality:
the former includes a chosen union allocation, the latter gives the receiver
unavailable information, and the measured finite-seed reception rate remains
uncertain. Increasing duration by two orders of magnitude is not justified by
these stable-ray results; assuming the same result for an unstable oscillator
is also unjustified.

The [oscillator note](evidence/oscillator-coherence.md) explains why slow rate
alone cannot provide long coherent accumulation with every oscillator. Under
the native Brownian phase presets, mean untracked 850-day correlation loss is
0.77 dB for paired OCXOs, 14.54 dB for TCXOs and 34.47 dB for hobbyist XOs or
crystals. These are expectations, not universal detection limits. A separate
reviewed constant-envelope Brownian information bound excludes the stated XO
example under its explicit flat-channel assumptions; it does not exclude every
shaped three-carrier/Each65 signal. At .001-Hz rate and 5.8 GHz, native capture
requires over 1.1606 Hz with GPSDO or 1.1601 MHz with free crystals. Huge simple
coherent search grids are computational warnings, not lower bounds on every
possible acquisition algorithm.

## What to try next, and what this study does not establish

| Situation | Evidence-backed first step | Remaining limitation |
| --- | --- | --- |
| Stable few-ray path, small residual phase evolution | Retain complex statistics; compare a global two-ray/static-frequency likelihood before independent-window gains | Exact phase/channel law and finite public search still matter; not a universal coherent receiver |
| Large arrival uncertainty or high rate | Complete the public timing bank and measure mismatch loss | Nominal-bank misses are not a sensitivity boundary; grouped export can dominate cost |
| Fast renewed fading or large oscillator diffusion | Physically constrained phase/channel inference or an explicitly justified covariance model | The coherent-reference scaling no longer proves achievable sensitivity; do not replace ray separation with stochastic width |
| Strong Each25/Each65 | Use corrected dispersive filtering and measured post-channel energy | More energy is not receiver gain; free sideband coefficients can cost more evidence than they recover |
| Very high C/N₀ | Observable-subspace whitening/SVD and explicit arithmetic diagnostics | FP32 sample quantization and ill-conditioned model identification remain different from physical SNR |
| Low-rate microwave/free-crystal capture | Size the coarse capture/acquisition stage from oscillator bounds before fine decimation | This study does not supply a practical million-hertz capture pipeline for an 850-day millihertz waveform |
| Missed FHSS dwells | Preserve channel state/echoes; erase receiver observations and report retained energy | Only the selected full-bank cycle-edge case has successful reception evidence here; other schedules need valid acquisition first |
| Wrong-key signals | Preserve separate controls; evaluate the existing pattern-consistency proxy with its reception cost | A Gaussian noise bound is not authentication; no universal wrong-key rejection is claimed |

The parameter census, short physical diagnostics and retained long observations
are complementary. No single global pass percentage is meaningful across all
2.50 billion intent coordinates. In particular, the 200-dB-Hz target is not a
promise that every unsupported geometry or capture/search mismatch can decode.
No new ordinary receiver integration, analog RF measurement, real FHSS retuning,
or general 99%-qualified duration planner follows from this study.

Report actual-noisy observations separately from conditional noise redraws on
one retained physical path. Keep correct admissions, wrong admissions, misses,
null admissions, wrong-key admissions, processing failures and unsupported
searches separate. Count processing failures as missed opportunities when making
a reliability claim. Do not pool heterogeneous cells into an apparent success
rate; retain pointwise uncertainty and independent validation seeds.

The strict and relaxed targets use 99%/10⁻⁴ per day and 90%/0.01 per
12 hours respectively. Small diagnostic control sets cannot empirically
qualify those operational false-admission probabilities. Long-span results must
preserve the genuine time coordinates, continuous selected channel state and
complete tails; rescaling a short observation is not equivalent.

## Cheap reproduction and retained evidence

Use [the reproduction guide](reproduction/README.md) for frozen campaign plans,
bounded stages, provenance requirements and explicit optional long-span work.
For isolated implementation changes, start with the
[scope-based test guidance](../../../building.md#isolated-three-carrier-research).
The new lightweight tool checks are:

```sh
python3 -m unittest discover -s tests -p test_triad_coverage.py
python3 -m unittest discover -s tests -p test_triad_factor_census.py
python3 -m unittest discover -s tests -p test_triad_coverage_bounds.py
```

To regenerate the complete factorized validity census cheaply with the current
native executable, use a fresh scratch directory:

```sh
triad_census="$(mktemp -d "${TMPDIR:-/tmp}/triad-census.XXXXXX")"
python3 tools/triad_factor_native_inputs.py --root "$PWD" \
  --binary build/dev-cli/triad_long --output "$triad_census/native" --max-seconds 120
python3 tools/triad-factor-census.py --evidence-dir "$triad_census/native" \
  --binary build/dev-cli/triad_long --output "$triad_census/report.json"
```

This uses 232 bounded native configuration calls, not sampled waveforms. The
portable generator and receipt validation checks passed with both the default
binary and a copied executable at an alternate path. Do not require historical
scratch paths to reproduce the census.

The census and theory reports bind the source and executable identities used.
The factorized tool validates retained native receipts rather than launching
billions of processes. A new native source or binary requires regenerated,
source-matched receipts; old evidence must not be silently rebound to a new
build. Preserve the compact reports here and keep large raw workspaces and
exploratory receipts in their declared evidence archive.

The original theory and harmonic files in `evidence/` are retained copies. The original
theory note calls its accepted report `coverage-bounds-source-bound.json` and
uses underscores in the harmonic filenames; this directory stores those as
`coverage-bounds.json` and `harmonic-probe.{json,py}`. The harmonic script retains
its original local input paths and cannot be replayed without that shaped-basis
input. It is retained as a reproducibility record, not a self-contained fixture.

| Artifact | SHA-256 |
| --- | --- |
| [Factor census](evidence/factor-census.json) | `ca249bc201377df8fa1a03047fb89c51c6b9524f0fb759893b3e6b3fb03ed0ca` |
| [Analytical bounds](evidence/coverage-bounds.json) | `236221bf37cbbdf79922a3da327ca07cb3a331ed55091b37e1425fd99324f2dd` |
| [Theory note](evidence/theory.md) | `28aac87a8ce2ed6234f1f0e9294be9b4274364f02e0879adf0ce213c29ff59e0` |
| [Harmonic diagnostic result](evidence/harmonic-probe.json) | `8756f1b368e22054e28592a56576f1c725bf8c4f96675b54faba98933d3215db` |
| [Harmonic diagnostic source](evidence/harmonic-probe.py) | `19e05f8025708877d431f890e5550271b5d0db5205345e31c8f8980bfb88fc1f` |

## Evidence preservation and completed cloud cleanup

The [preservation record](evidence/preservation.json) binds all 15,835 retained
scientific files, totaling 10,557,157,072 bytes. Every remote file's size and
SHA-256 matched its local copy before the disposable Pod and container storage
were removed. The exact tested native executable, original source archive and
119,684-byte accepted Python overlay are retained in
`local-evidence/triad-broad-coverage/sources/`. Large workspaces and full stage
receipts are under `local-evidence/triad-broad-coverage/remote/{original,fixed}/`.
These ignored files remain in this workspace; a fresh Git checkout contains
the compact maintained evidence and recipes, not the large local archive.

The single GPU allocation was stopped and terminated on 2026-11-03 UTC.
The console then reported zero current spend. Estimated experiment cost was
about $7; that is elapsed-time accounting, not a finalized provider invoice.
No persistent volume or ongoing experiment process was left running.

Recount the preserved results without regenerating any waveform:

```sh
triad_py=/tmp/triad-weak-env/bin/python
triad_evidence=docs/validation-data/triad-v3/2026-11-03-broad/evidence
triad_remote=local-evidence/triad-broad-coverage/remote
"$triad_py" "$triad_evidence/summarize_long.py" --remote "$triad_remote" \
  --require-actual --output /tmp/triad-long-recount.json
"$triad_py" "$triad_evidence/summarize_acquisition.py" --remote "$triad_remote" \
  --name sensitivity-global --output /tmp/triad-sensitivity-recount.json
```

The long recount checks source/receipt identities, raw input and score hashes,
planned coverage and independent counts. It refuses incomplete reliability
bounds. The paired numerical summary compacts the separately source-bound
`rescore/analyze.py` result; it does not replace that analyzer's raw-array checks.
Use selected retained workspaces for further experiments. Uploading the full
10.56-GB archive is unnecessary for a focused receiver change.

# Random-channel reception and Helper phase limits

This study implements and tests time-varying stochastic propagation for the
isolated three-carrier modem. It materially improves reception over the static
two-ray comparator in the tested conditions. It does **not** qualify the entire
configuration cross-product, operational daily false admissions, or −60 dB-Hz
operation under renewing random fading. Ordinary Robust integration and live
radio measurements are outside this change.

Start with the [model, receiver and cheap-test guide](../../../triad-stochastic.md).
The source base is `a6678218dc88827fe5b58e6016cd85a393928d43` plus the retained
source overlay. Evidence distinguishes the original exporter, its byte-identical
rebuild, the later diagnostic-component exporter, and subsequent receiver fixes.
Historical deterministic/sinusoidal results remain controls for their own laws;
they do not qualify this stochastic channel.

## What changed

- Seeded physical-time Gaussian-spectrum complex ray gains, bounded random
  selective coefficients, and optional smooth random delay motion. Sample-rate
  changes do not redefine the underlying random process. The complete summed
  waveform traverses continuous dispersive state, including tails and guards.
- A block-first likelihood using all three complex DATA carriers and physically
  shared ray states. Optional low/high Helper pilot templates enter both the
  DATA-present and DATA-absent hypotheses. No noisily multiplied fine-symbol
  products or perfect-channel estimates are supplied to this receiver.
- An equivalent parallel affine state scan and tiled complete-bank acquisition.
  FP32 bulk arithmetic remains strict; small covariance preparation and evidence
  reductions use FP64. Calibration identities bind the effective receiver,
  source, projection, arithmetic and public geometry.
- A fixed, explicitly lossy core projection for strong selective sidebands.
  It changes retained receiver bins, not the physical propagation or waveform.
- Actual whole-bit Symbol Burst emission with a fresh 38-second Helper lead
  before every DATA pulse support. A proposed 76-second minimum rounds to 90
  seconds on the maintained 30-second UTC grid; long bits/backlog waits extend it.

Random ray width `B=2σ` is separate from the preset's ray-center separation.
The bounded Gaussian-transformed AM has different mean-square gain from a sine
at the same peak depth. Each 65 has about 0.453 dB less nominal branch power;
actual received energy, including interference, is measured separately.

## Sampled sensitivity results

Each original cohort contains 39 DATA-absent calibration captures, 32 signal
captures (16 of each bit), and 32 independent DATA-absent validation captures.
The full native Helper waveform, including auxiliary modulation, is present
where enabled. All use encryption, Rate 36, Added×10, HF-worst margin 3
(90-Hz ray-center separation and 21-ms excess delay), Each 65 random selective
modulation, nominal conditioned arrival/epoch, and explicitly disabled oscillator
errors. This is unknown-channel reception at known synchronization. Table C/N₀ values
are explicit noise overrides: the nominal 4 W / 180 dB link with the default noise
density instead has physical C/N₀≈20.02 dB-Hz. The 8/0/−6 dB-Hz captures deliberately
weaken that link; they are not the unmodified physical power/path-loss example.

Counts below are **correct / wrong / missed**, then null admissions out of 32.
The bit duration excludes Helper lead and channel tails; it is not received
DATA energy. No gate was lowered after viewing held-out outcomes.

| Condition | C/N₀ dB-Hz | Actual bit seconds | B Hz | Static | Dynamic |
| --- | ---: | ---: | ---: | --- | --- |
| No Helper | 8 | 23.2 | 0.5 | 20/0/12; 0 | 32/0/0; 4 |
| Both pilots | 8 | 23.2 | 0.5 | 23/1/8; 6 | 32/0/0; 4 |
| No Helper | 0 | 23.2 | 0.5 | 3/0/29; 0 | 26/1/5; 3 |
| Both pilots | 0 | 23.2 | 0.5 | 5/1/26; 0 | 32/0/0; 0 |
| Moving delay, no Helper | −6 | 69.6 | 0.5 | 4/1/27; 5 | 10/2/20; 4 |
| Moving delay, both pilots | −6 | 69.6 | 0.5 | 3/0/29; 0 | 28/1/3; 1 |
| Faster fading, no Helper | 8 | 23.2 | 5 | 2/3/27; 1 | 30/0/2; 1 |
| Faster fading, high pilot | 8 | 23.2 | 5 | 2/0/30; 1 | 32/0/0; 1 |

Selective fast width is 0.05 Hz in this table; slow width is one tenth.
Moving-delay cases use 0.001 ms excursion and 100-second knots. The B=5 cases use
25-ms blocks and a shorter fixed coherence bank; other cases use 250-ms blocks.
Only the high pilot was used at B=5 because the full two-pilot envelope conflicted
with the auxiliary exclusion. That is an adapter constraint, not a physical
impossibility. Paired Helper on/off cases share channel/noise seeds and identical
emitted DATA energy. The different durations/keys/levels are not one fitted
sensitivity or duration curve.

Same-I/Q exploratory pilot ablations at 0 dB-Hz gave dynamic 27/0/5 without pilot
use,31/0/1 with either single pilot, and 32/0/0 with both. Their null counts were
3,3,0,0 respectively. Physical Helper emissions remained present in every arm.
These are dependent replays, not additional independent trials.

### Strong, fast selective modulation

A separate independent cohort uses Each 65 with **90-Hz random selective width**,
the same 0.5-Hz ray width, Fs 6000 complex samples/s, and the fixed core projection.
The native captures retain actual broad sidebands; the receiver deliberately
discards some through its fixed core projection and is separately calibrated.

| Condition | Static correct/wrong/missed; null | Dynamic correct/wrong/missed; null |
| --- | --- | --- |
| Both pilots,8 dB-Hz | 17/0/15; 0/32 | 32/0/0; 3/32 |
| Both pilots,0 dB-Hz | 8/0/24; 3/32 | 32/0/0; 0/32 |
| Weak Helper,8 dB-Hz | 23/0/9; 0/32 | 32/0/0; 4/32 |

Weak Helper means 0.004 W versus 4 W DATA, rather than the 40 W Helper used in the
other HF cases. It demonstrates that this declared weak-pilot model can still
use DATA successfully; it is not a universal automatic Helper-lock test.

Before these cohorts,24 component-separated native captures (two seeds,
6/12-kHz sample rates, six source arms) measured 99.36–99.38% retained DATA energy,
93.46–93.72% low-pilot energy and 94.60–94.63% high-pilot energy. Only 0.032–0.035%
of isolated auxiliary energy entered the retained core bins. Its projection
into the DATA template subspace had noncentrality 0.00357–0.00478 at 8 dB-Hz.
This is actual leakage from the production auxiliary waveform through the
complete random channel, not band-center metadata or an ideal stopband claim.

The 6→12-kHz check had zero timestamp mismatch, approximately 3×10⁻⁶ DATA-only
normalized waveform RMS difference and about 0.41% composite power difference.
These two-seed refinement measurements are engineering diagnostics, not a
worst-case alias bound. A discovered units error divided the original reported
noncentrality by Fs twice; the retained original and separately corrected
derivative explicitly record it. Energy measurements and receiver scores were
unaffected. The weak-Helper run also retains an initial rejected CLI spelling;
it produced no capture before the corrected command.

## Blind acquisition failure and correction

An independent full-bank experiment searched 41 timings over[−55.5,125.5]ms and
13 epochs:533 hypotheses,78 tiles,103 complete captures. Native arrivals were
random throughout that continuous prior; true epoch stayed nominal, with
oscillators still off. The original 4.525-ms timing grid gave dynamic 3 correct,
2 wrong and 27 missed signals, with 2/32 null admissions. Static gave 1/0/31 and
3/32 null admissions. Those failed outcomes remain in the evidence.

On four selected captures, exact-arrival joint-pilot scores chose all bits
correctly. Quantizing the same arrivals to the coarse grid changed presence
scores from approximately 193,33,106 to−147,−85,−69 in three cases and produced
two raw bit errors. DATA-only reception at those same coarse coordinates chose
all four correctly. The widely separated pilots make relative phase much more
timing-sensitive than the DATA symbol rate suggests.

Replaying **every original hypothesis** with the complete DATA-only receiver,
and its own null calibration, gave dynamic 31 correct/0 wrong/1 missed,
with 1/32 null admissions; static gave 18/0/14. This is a same-capture diagnosis
and recovery, not independent validation. The public planning guard and fresh
validation have their own source identities. The guard's fresh key and 103 new
native captures used the public DATA-only fallback and produced **30 correct/0 wrong/2 missed**, with 1/32 null
admissions; static produced 10/0/22 with 0/32 null admissions. One of the two
misses had a raw bit error. The complete run took 110.38 s, including 82.95 s
preparation and 11.73 s scoring. All arrivals remained within the public prior.
This establishes recovery on independent sampled realizations at this finite
bank, not 99% reception or full oscillator acquisition. The 95% balanced-stratum
correct-reception interval for 30/32 is [66.21%,99.92%].

The guard uses timing-bank coverage over the whole public prior, including
endpoint gaps, unresolved sample-clock drift and declared delay excursion.
When relative pilot/DATA phase is unresolved it chooses DATA-only before
examining samples; the frozen gate therefore belongs to that actual procedure.
An explicit discrete prior remains available for truly discrete or historical
reference experiments. A singleton bank does not imply known continuous timing.
The equal-tone 0.5-dB planning reference permits about 116 µs spacing for 1850 Hz
separation. This is an engineering resolution check, not a guaranteed two-ray
sensitivity loss. Efficient fine joint timing and frequency-only pilot assistance
remain follow-up work; coarse pilot scores must not veto the complete DATA path.

## Computation, precision and uncertainty

The parallel state calculation preserved all raw bit decisions, calibration
orderings, held-out p-values and admissions in 206 actual-capture serial/scan
replays. Maximum absolute hypothesis-evidence changes were 1.17×10⁻⁴ and 8.95×10⁻⁶.
Small dense-covariance, rank-deficient, erased-block and FP64 references are
separate focused tests. Numerical envelopes measured on these fixtures are not
universal error bounds for arbitrary Helper power or ill-conditioning.

On the same eight captures and both-pilot policy, strict-FP32 complete scoring
(transfer, correlation, state; excluding preparation/loading/generation) gave:

| Backend | Serial→scan, single capture | Serial→scan, batch 8 |
| --- | ---: | ---: |
| CPU | 2.99× | 2.34× |
| RTX PRO 6000 Server | 7.36× | 4.52× |

The matched CPU measurements used two PyTorch threads on the Pod’s Xeon 6952P;
they are not the local Ryzen timing. Optimized GPU versus optimized CPU was only 1.19× single/1.09× batch on this
small workload. The synthetic 10,000-block GPU state-only speedup was 231×;
it is **not** a whole-receiver or CPU/GPU speedup. Three warm repeats provide
measured timings, not sustained qualification or confidence-qualified forecasts.
The failed blind campaign took 158.53 s including 13.56 s native generation,
120.90 s workspace preparation,21.47 s correlation and 0.824 s state inference.
Preparation is the next meaningful computational target. All trials share
prepared workspaces; replays reuse captures rather than regenerating them.

The diagnostic rank gate has 39 calibration samples,0.025 p-value resolution
and nominal marginal alpha 0.05 under exchangeability. It does not establish
conditional 5% for every frozen gate, simultaneous error across arms, wrong-bit
control, or 10⁻⁴/day. The 95% balanced-stratum interval for 32/32 correct is
[76.04%,100%];0/32 nulls has a 95% one-sided upper bound of 8.94%.
Do not pool paired channels, reused captures or selected oracles as independent.

Two selected no-Helper failures were also regenerated with oracle known means.
Their conditional optimal bit-error references were 1.10×10⁻⁵ and 8.76×10⁻⁴;
the available DATA energy was unchanged by Helper. This locates a substantial
unknown-channel/model gap in those realizations. It does not prove that the
entire gap is achievable without channel knowledge or provide a population bound.

## Scope still unqualified

- Continuous fine joint timing, independent RX clock scale, broad/free-running
  oscillator acquisition, and model-calibrated Helper narrowing in this new
  stochastic adapter. Earlier fixed-ray Helper narrowing results do not transfer.
- Partial-block ADC erasures with the Helper Fourier projection; fully erased
  blocks and no-Helper raw-row erasures have explicit checks. Whole-bit train
  generation is implemented, but the train receiver adapter remains unsupported.
- The multi-day−60 dB-Hz random-channel target, maximal spreading/high-rate/full
  cross-product, and operational repeated-attempt admission. The guide derives
  why stable-channel and renewing-unknown-channel duration laws can differ by
  orders of magnitude; these Gaussian assumptions do not bound every physical
  or transformed-Gaussian channel.
- Physical environmental parameter calibration, RF/ADC/DAC interference and
  measured field channels. The added law is an explicit stochastic stress model,
  not proof of universal realism or universal optimality.
- New-path branch instrumentation, sustained full-receiver timing and runtime
  prediction envelopes. Historical performance guard success does not cover
  this new model/adapter. Use the new rows in the maintained coverage matrix.

No release, publication or ordinary-receiver qualification is claimed.


## Reproduction and acceptance evidence

[compact-evidence.tar.gz](compact-evidence.tar.gz) retains protocols, schedules,
per-capture decisions/energy reports, hashes, gates, numerical measurements,
failed attempts, audits, command logs and one-off reproduction scripts.
[source-overlay.tar.gz](source-overlay.tar.gz) binds the changed source to the
base revision; [frozen-v1-overlay.tar.gz](frozen-v1-overlay.tar.gz) preserves the
original campaign implementation. The original exporter was rebuilt byte for
byte before component diagnostics changed it. [artifact-manifest.json](artifact-manifest.json)
and [SHA256SUMS](SHA256SUMS) identify these packages.

Raw I/Q and template arrays are omitted from the maintained compact archive;
their hashes and native commands are retained. Regenerating a selected capture
is required for a waveform replay after temporary cloud cleanup. Recounting
admissions, gates and timing needs only compact receipts. The local ignored
working directory retains additional selected I/Q. Do not confuse receipt audits
with independent reprocessing of every raw sample.

Accepted checks:

- Native isolated triad group: 16/16, 33.45 s test wall; separate whole-bit calendar
  CTest passed. Channel moments, covariance, cross-rate differential response,
  continuous state and moving-delay phase checks are included.
- Dynamic Python inventory: 39 tests, 6.71 s locally with CUDA and optional native
  replay explicitly skipped. On the GPU host, all 39 including actual CUDA and
  native replay passed in 5.54 s; no hardware skips were counted as passes.
- Exporter smoke: 55 commands including 26 expected rejections, 36.78 s.
  Complete two-bit burst qualification: 5.84 s for 161.87 s simulated elapsed time.
  Component/default-wire bridge: 18.92 s, with byte-identical default samples and
  approximately 3.2×10⁻¹⁶ decomposition residual. Production pilot synthesis
  matched 80,096 reference evaluations exactly.
- The old static Helper orchestration's 34 study/12 campaign tests passed;
  Helper-disabled paths bypass nonexistent pilot work and stochastic laws are
  rejected before using the old finite-line estimator.
- Independent audits rederived mixture/gate arithmetic, decisions and source
  relationships. Focused checks, sampled sensitivity, performance measurements
  and unqualified regimes remain separate outcomes.

Use the guide's short affected tests routinely. Re-run selected native exports
only when the source/channel changes; reuse captures for receiver comparisons.
No full historical research campaign is a routine regression prerequisite.

One RTX PRO 6000 Blackwell Server Edition Pod supplied the CUDA runs. The small
source upload completed in seconds; no persistent volume was needed. All compact
result receipts were downloaded before the temporary container and Pod were deleted.
The dashboard then reported $0.000/hour ongoing spend and no network volumes.
The elapsed-time cost ceiling was about $3.65, below this run’s $12 reservation;
that is an accounting bound, not a final provider invoice.

# TCXO phase-continuity discriminator

The existing finite-phase receiver has a useful advantage over independent
phase resets near the selected TCXO coherence boundary. The controlled
sufficient-statistic diagnostic was followed by 16 independent, source-bound
sampled flat-channel signal captures and four nulls. The receiver admitted
9/16 signals at the strict gate and 16/16 at the relaxed gate, compared with
3/16 and 7/16 for fixed coherence-length segmentation. This establishes a
bounded improvement under known acquisition, unit gain and injected noise
variance; it does not qualify a general propagation model or optimal receiver.
At a separately sampled −57-dB-Hz point, coherence-length segmentation and the
phase receiver both admitted 16/16 signals at both gates. That point favors
keeping the inexpensive segmented baseline available; the more elaborate
phase inference showed its measured reception benefit at the weaker point.

For the stipulated relative Wiener coefficient
`sigma = hypot(0.05, 0.05) * pi/180 rad/sqrt(s)`, define
`tau = 2/sigma^2 = 1,313,122.54 s` (about 15.2 days). At −60 dB-Hz,
`(C/N0)*tau = 1.3131`; 850 days provide nominal energy SNR 73.44. The test
preserves `c*tau`, `T/tau`, and `dt/tau`, not an arbitrary resemblance to a
short real RF capture. Its teacher is a continuous-Brownian skeleton with
constant phase inside each simulated block, unit gain, and two exactly
orthogonal candidate patterns. It has no shaping, selective modulation,
delay/Doppler, acquisition uncertainty, unknown amplitude, or noise uncertainty.

The simulator independently draws phase and noise for 128 signal paths, balances
the transmitted bit labels, and supplies 128 separate noise-only paths. C/N0
alternatives share these indexed draws. The detector receives only noisy
complex matched statistics and public Gram energy. The oracle additionally
receives the true phase trajectory and is a favorable benchmark only.

Each practical score is a normalized likelihood ratio under its fixed prior
against known circular Gaussian noise. The global and segmented comparators
integrate uniform unknown phase using `log(I0(2*abs(r))) - energy`; the existing
`PhaseFrequencyLikelihood` integrates its finite-state phase prior. The two
operating gates remain 22.7210633896 and 17.4227460230. They reserve half the
strict/relaxed false-admission budget for scores and cover two bits, 16 receiver
alternatives, eight durations, and 1,440 attempts/day or 720 attempts/12 hours.
This is an exact-arithmetic Gaussian-null bound, not empirical qualification of
rare admissions or a guarantee under structured interference.

## Development result

Correct admissions out of 128 signal paths, strict / relaxed:

| C/N0 | Global phase | Independent phase every tau | Existing finite phase, 32 states | Oracle phase |
| --- | ---: | ---: | ---: | ---: |
| −63 dB-Hz | 0 / 0 | 0 / 1 | 0 / 2 | 125 / 128 |
| −60 dB-Hz | 0 / 0 | 21 / 53 | 73 / 107 | 128 / 128 |
| −57 dB-Hz | 0 / 0 | 122 / 127 | 128 / 128 | 128 / 128 |

The other fixed segment lengths were `0.25*tau` and `4*tau`; they were worse
than `tau` on the central case. There were no wrong-bit admissions or noise-only
admissions for any of these listed cases. For the −60-dB-Hz phase receiver,
the strict correct fraction's two one-sided 95% Clopper–Pearson bounds are
[0.4938, 0.6444], and the relaxed bounds are [0.7724, 0.8874]. The zero-out-of-128
noise result still has a one-sided 95% upper bound of 2.313%; it does not
empirically verify the operational false-admission target. The report fields
ending in `interval95` contain **two one-sided 95% bounds**, equivalently a
central 90% interval, not a conventional two-sided 95% interval.

The base teacher step was `0.0125*tau` or smaller after integer rounding.
Comparing 32, 64 and 128 phase states changed no candidate or admission at the
finest block size. The 32/128 maximum log-likelihood difference at −60 dB-Hz
was 0.03358. Combining four consecutive observation blocks changed one strict
decision (73 to 72) at that C/N0. This is a receiver-block convergence check on
the same teacher data, not a continuous-time teacher convergence result.

The 18 phase configurations across three C/N0 values took 90.79 seconds total
on one local CPU thread; the central 32-state result took 3.34 seconds for
256 signal/null paths. FP32 versus FP64 on the same central inputs had maximum
log-likelihood difference `5.262e-5`, with no candidate or admission changes.
The implementation retains explicit FP64 log-evidence accumulation. These
small runs do not justify cloud startup or imply a GPU speedup.

At −60 dB-Hz the finite phase method improves reception substantially, but it
still falls below either reception target. The oracle gap remains large. The
result establishes neither a fundamental lower limit nor a sufficient duration
for a real TCXO, and does not transfer directly to the corrected Each 65%
two-ray channel. Actual oscillator phase-noise spectra and shared-clock topology
need their own model checks.

An independently seeded validation then fixed the 32-state, finest-block
receiver before drawing 256 new synthetic paths (`seed=703011`). At −60 dB-Hz
it admitted 145/256 correctly at the strict gate and 202/256 at the relaxed
gate, versus 45/256 and 105/256 for the fixed `tau` segment and zero for global
phase. All methods retained zero wrong-bit and null admissions. At −57 dB-Hz
the phase receiver admitted 256/256 correctly at both gates, whose one-sided
95% lower bound is 98.8366%: enough for 90% in this favorable teacher, not 99%.
Zero null admissions out of 256 have upper bound 1.1634%, far above the
operational target. The two C/N0 values reused the same validation paths;
they are not 512 independent signal paths. The two phase batches took 10.23
CPU seconds combined. See [development results](development.json),
[independent validation](independent-validation.json), and
[FP64 comparison](precision-fp64.json).

## Reproduce cheaply

Use the numerical environment described in the main development workflow:

```sh
TRIAD_PYTHON=/tmp/triad-weak-env/bin/python
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/diagnostic.py \
  --output NEW/development.json --trials 128 --phase-states 32 64 128 \
  --reblocks 1 4 --dt-over-tau .0125 --cn0 -63 -60 -57
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/diagnostic.py \
  --output NEW/precision-fp64.json --trials 128 --phase-states 32 \
  --dt-over-tau .0125 --cn0 -60 --precision fp64
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/diagnostic.py \
  --output NEW/independent-validation.json --trials 256 --seed 703011 \
  --phase-states 32 --dt-over-tau .0125 --cn0 -60 -57
```

The retained development JSON includes exact source hashes, environment,
declared teacher, raw counts, convergence comparisons and null-scope limits.
Compressed paired score arrays remain in ignored
`local-evidence/triad-extension/phase/`. No ordinary receiver regression or
integration is required to rerun this isolated diagnostic.
Use fresh output paths: the retained `diagnostic.py` version overwrites an
existing JSON/score-pair path. It is preserved unchanged to keep the executed
source hashes reproducible; a future campaign should add explicit collision
rejection as a separately bound source version. The actual-workspace wrapper
already rejects an existing result path.

## Sampled follow-up and original setup failure

`score_actual.py` wraps the existing phase receiver for a two-candidate,
one-tap, known-alignment, known-unit-gain actual-noisy workspace. It rejects
non-Gaussian frontends and controls other than signal/null. It does not read
noiseless simulator means. The one pilot export invoked a build path while
another agent rebuilt it; its exact executable provenance was not bound before
launch. That capture is retained as diagnostic-only evidence and must not be
counted as a source-bound validation success. A fresh source-bound cohort was
subsequently completed; its results below exclude this pilot entirely.

The first scorer invocation rejected the pilot before scoring because it
incorrectly required capture-relative arrival zero. The native workspace
correctly includes a public 0.05-second capture lead. The repaired wrapper
requires both candidate arrivals and the zero-width physical/public timing
bounds to equal that declared lead, while CFO, clock drift, keyed-epoch offset,
tap delay and tap Doppler remain zero. The failure and correction do not alter
the above sufficient-statistic results.

For a source-bound actual follow-up, bind the executable hash before and after
this command and use a fresh output directory. This is a deliberately flat,
known-acquisition isolation of phase inference, **not a full oscillator-profile
or strong-selective-channel experiment**:

```sh
TRIAD_BINARY=/path/to/verified/triad_long
"$TRIAD_BINARY" --mode export-stats --seconds 73440000 --rate .001 --added 1 \
  --sample-rate 1 --sample-rate-denominator 4 --rf-hz 500000 \
  --nominal-if-hz .0005 --channel none --phase-diffusion .001234134149488435 \
  --basis flat --cn0 -60 --threads 1 --memory-mib 256 \
  --export-max-samples 25000000 --export-max-mib 512 --block 250 \
  --signal-bit 0 --control signal --output NEW/actual-flat-tcxo \
  --stats-noisy --seed 603021
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/score_actual.py \
  NEW/actual-flat-tcxo --output NEW/actual-score.json --phase-states 32 64
```

The pilot has 18,456,032 complex samples at 0.25 sample/s, 73,825 blocks and a
complete 73,824,128-second capture calendar. Its source duration rounds upward
from the 850-day request; all duration statements must distinguish that actual
calendar. Existing `triad-state-study.py` remains the general conditional/actual
workspace driver; this small wrapper fixes a simpler unit-gain prior to isolate
the discriminator and should not become a second general receiver API.

## Equal energy: longer duration versus more power

The next comparison was fixed before its new draws: −60 dB-Hz for 1,700 days
versus −56.9897000434 dB-Hz for 850 days. Both provide nominal energy SNR
146.88, but their `c*tau` values are 1.3131 and 2.6262. With seed 803011,
128 fresh paths share the exact Brownian/noise prefix between arms. The long
arm continues the trajectory; it does not repeat a short observation.

| Arm | Strict correct / 128 | Relaxed correct / 128 | Strict two one-sided 95% bounds |
| --- | ---: | ---: | --- |
| Twice duration | 127 | 128 | [0.96348, 0.99960] |
| Twice power | 128 | 128 | [0.97687, 1] |

There were zero wrong admissions and zero admissions in 128 paired noise-only
controls per arm. The one-path difference does not resolve a power-versus-time
sensitivity boundary. The two phase runs took 9.64 CPU seconds combined. These
are still the favorable synthetic sufficient-statistic teacher and the existing
32-state receiver, not a 1,700-day sampled triad validation.

The known-waveform **presence** benchmarks use the same family as the gates.
The strict per-candidate allocation is `1.35633680556e-10`, and the relaxed
allocation is `2.71267361111e-8`. For desired detection probability `p`, the
Neyman–Pearson known-waveform energy benchmark is
`0.5 * (Phi^-1(1-alpha) + Phi^-1(p))^2`. It is 37.33099 strict and 22.56796
relaxed. Applying the more conservative fixed log-LR gates to that same known
waveform requires 44.72261 and 26.80636 instead. Neither number establishes
unknown-phase achievability. In particular, using the one-completion alpha
`1e-4` here would compare different operating requirements. See
[equal-energy results](duration-power.json).

```sh
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/duration_power.py \
  --output NEW/duration-power.json
```

## Source-bound actual batch and CPU/GPU comparison

`prepare_actual.py --output NEW/actual-plan.json` freezes portable argument
definitions and the source-file closure. It plans 16 independent signal paths
(eight per bit) and four independent nulls, then scores all 20 together using
the existing phase likelihood. The executor must bind and verify the actual
native executable, source archive and real build proof before execution and
check those identities again afterward. The plan does not invent a build
attestation. Each child has a 900-second limit; exports permit at most 12
concurrent one-thread jobs. Source/receiver outputs must be fresh, hash-bound,
and include every failed, skipped or timed-out job.

The resident wrapper `score_actual_batch.py` accepts one or more complete
actual-noisy workspaces. Every path retains its own public Gram and observation;
only identical public block calendars share the phase transition. No simulated
signal mean or phase truth enters inference. A 20-path batch has a conservative
resident host/device estimate around 283 MB and is bounded to 512 MiB by the
plan. Resident inputs and receiver state/graph storage share that single bound;
the state receives only the remaining budget. Framework runtime and allocator
caches are outside this owned-array estimate. Its narrow flat, known-gain,
known-acquisition scope is checked explicitly. The injected Gaussian noise
variance is known from the manifest and normalizes the observations and Grams;
this is not an unknown-noise calibrated receiver.
Its Clopper–Pearson summaries assume independent identically distributed paths
under that stipulated law; they do not validate the physical law or establish
operational reliability from 16 signals and four nulls.

Four single-capture benchmark jobs use the identical first capture: CPU reference and
CUDA graph implementation, each in FP32 and FP64. GPU jobs must run serially.
An additional predeclared CUDA FP32 batch scores the identical 20 observations
used by the combined CPU run. This is a throughput comparison, not 20 new
independent validation paths. All five benchmark jobs run sequentially.
They preserve the finite prior and use the existing graph implementation;
unavailable CUDA/Triton fails explicitly. Strict FP32 disables TF32 and retains
the existing explicit FP64 evidence reductions. The report binds a hash of the
prepared noisy statistics and separates input loading/validation, transfer and
state preparation, synchronized resident updates, and script time including
imports. Torch, CUDA and Triton versions are reported. The executor's external child time supplies full-process wall time,
including interpreter startup and output writing. Compare both score/ranking/
admission agreement and complete-job cost; GPU availability alone is no gain.

Example single-capture backend calls after a verified export:

```sh
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/score_actual_batch.py \
  NEW/actual-flat-tcxo --output NEW/cpu-fp32.json --device cpu \
  --precision fp32 --implementation reference
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/score_actual_batch.py \
  NEW/actual-flat-tcxo --output NEW/cuda-fp32.json --device cuda:0 \
  --precision fp32 --implementation graph
```

Repeat with explicit `--precision fp64` and fresh result names for the paired
reference comparison. `test_phase_extension.py` checks the phase integral
against independent quadrature, validates the public capture origin and scope
rejections, compares batch versus individual and FP32 versus FP64 inference,
checks the combined input/state memory limit, and checks path balance/seed
uniqueness in the planned batch. All six focused checks passed. These are focused
checks; they do not launch the long exports or GPU campaign.

After the original batch completed, the plan generator gained explicit
`--cn0` (default −60) and `--seed-offset` (default zero) parameters. The old
default job argument lists and definitions are unchanged. C/N0 must be finite
in [−80, 20] dB-Hz; the offset must be an exact integer in [0, 1,000,000,000].
Every variant needs a fresh plan, source binding and output root. To test the
synthetic −57-dB-Hz transition with independently seeded sampled captures:

```sh
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-extension/phase/prepare_actual.py \
  --output NEW/actual-plan-cn57.json --cn0 -57 --seed-offset 10000000
```

The offset moves all seven seed domains to new roots. Two additional focused
tests check unchanged defaults, exact seed shifts and invalid/inexact/nonfinite
inputs; all eight current checks pass. The original executed plan and archive
retain their earlier generator hash. A newly generated variant is distinct
evidence and must not overwrite or relabel that completed cohort.

## Accepted sampled results and measured acceleration

The separately bound remote cohort completed all 46 planned jobs: 20 configure,
20 actual-noisy exports, one 20-capture CPU score and five backend benchmarks.
The exact source/binary/archive identities, bounded-completion receipts, input
hashes, per-capture scores and outcomes are recorded in
[the actual summary](actual-summary.json). Saved score reports are in
[evidence](evidence/actual-all20-cpu-fp32.json). The earlier local pilot with
ambiguous executable identity contributes no accepted trial or timing result.

These are encrypted, rate-0.001-Hz, Added ×1, flat-channel captures with no
Doppler/delay spread or added selective modulation. The only oscillator
impairment is the explicitly selected relative Wiener coefficient matching
the TCXO stress parameter; full TCXO frequency/sample-clock profile acquisition
is absent. Arrival, CFO, clock and epoch are known; the receiver knows unit
gain and the injected Gaussian noise variance. Every capture contains
18,456,032 complex samples at 0.25 sample/s. The requested 850 active days round
to 73,728,051.2 active seconds, while pulse support and the complete tail extend
the capture to 73,824,128 seconds. Measured signal energy SNR across the 16
signals is 73.6672–73.6712 at nominal −60 dB-Hz.

| Receiver | Strict correct / misses | Relaxed correct / misses |
| --- | ---: | ---: |
| Global unknown phase | 0 / 16 | 0 / 16 |
| Independent phase every 0.25 tau | 0 / 16 | 0 / 16 |
| Independent phase every tau | 3 / 13 | 7 / 9 |
| Independent phase every 4 tau | 0 / 16 | 1 / 15 |
| Existing 32-state phase likelihood | 9 / 7 | 16 / 0 |

All methods had zero wrong-bit admissions and rejected all four null captures.
The phase receiver's strict two one-sided 95% bounds are [0.3334, 0.7733]; its
relaxed lower bound is 0.82925. Thus even 16/16 does **not** establish 90% or
99% correct reception. Zero out of four nulls has upper bound 0.52713, so the
small controls do not empirically qualify rare admissions. No wrong-key controls
were included; the earlier structured-interference limitation remains open.

The sampled strict fraction 9/16 agrees with the favorable synthetic holdout's
145/256 within their uncertainty. The sampled relaxed result 16/16 is higher
than the synthetic 202/256 point estimate, but these small-sample bounds do not
resolve a discrepancy. Actual triad Grams vary with the keyed, shaped waveform;
the synthetic teacher has exactly orthogonal patterns and constant per-block
energy. These are related diagnostics, not identical distributions. Measured
energy is about 2.95 dB above the strict known-waveform Neyman–Pearson reference
for the declared family, yet phase-unknown strict reliability remains unproven.
This is an uncertainty/model gap to investigate, not evidence of universal
near-bound performance or a justified duration planner for stronger channels.

On one RTX PRO 6000 Blackwell Workstation GPU, the identical-input measurements
were:

| Scope | CPU FP32 | CUDA FP32 graph | Observed ratio |
| --- | ---: | ---: | ---: |
| Single capture: synchronized updates | 8.570 s | 4.306 s | 1.99× |
| Single capture: full child process | 10.460 s | 7.026 s | 1.49× |
| Same 20 captures: synchronized updates | 10.374 s | 4.098 s | 2.53× |
| Same 20 captures: full child process | 22.514 s | 17.178 s | 1.31× |

The 20-capture input load/validation consumed 10.66–11.22 seconds. Exporting
the 20 actual captures took 191.01 seconds with at most 12 concurrent CPU jobs;
including that common generation stage reduces the observed generation-plus-
scoring ratio to 1.026×. Reusing the retained workspace is consequently much
more valuable than regenerating it to compare receiver arithmetic.

These synchronized update measurements include Python dispatch and first graph
warmup/capture; they are **not pure kernel times or measured warm steady-state
throughput**. The 20-capture GPU update included 0.292 seconds of recorded graph
warmup and 0.171 seconds of graph capture. There was one timed run per backend,
so cache and order effects remain unquantified. The campaign coordinator records
12 other CPU export jobs running during the original −60-dB-Hz timing work.
These are observations under concurrent load, not exclusive-host latency
measurements or latency distributions. No universal GPU
speedup or FP32-versus-FP64 performance rule follows from this small state size.

All four single-capture backends had identical prepared-input hashes and
unchanged winners/admissions. Relative to CPU FP64, maximum phase log-LR
differences were `7.221e-4` for CPU FP32, `4.958e-4` for CUDA FP32, and
`2.167e-13` for CUDA FP64. Across all 20 captures, CPU/CUDA FP32 differed by at
most `3.157e-4`, with no winner or gate decision changed. The backend repeats
are timing/precision evidence on the same paths, not extra independent trials.

## Independent −57-dB-Hz operating point

A second, separately frozen cohort used the same physical duration and receiver
assumptions, but nominal C/N0 −57 dB-Hz and all seven seed domains offset by
10,000,000. The 16 signal paths and four nulls are independent of the −60-dB-Hz
cohort. They must **not** be pooled as 32 same-SNR signal trials. All 46 planned
jobs completed. Their source bindings, independently recounted decisions,
measured energy and backend comparisons are in
[actual-cn57-summary.json](actual-cn57-summary.json), with
[compact score evidence](evidence/cn57/actual-all20-cpu-fp32.json). The original
−60-dB-Hz summary remains separate under `actual-summary.json`.

| Receiver | Strict correct / misses | Relaxed correct / misses |
| --- | ---: | ---: |
| Global unknown phase | 0 / 16 | 0 / 16 |
| Independent phase every 0.25 tau | 8 / 8 | 14 / 2 |
| Independent phase every tau | 16 / 0 | 16 / 0 |
| Independent phase every 4 tau | 1 / 15 | 2 / 14 |
| Existing 32-state phase likelihood | 16 / 0 | 16 / 0 |

Every method again had zero wrong-bit admissions and rejected all four nulls.
For either 16/16 result, the one-sided 95% lower bound is only 82.925%; this is
known-alignment, known-unit-gain, known-noise screening, not 90% or 99%
qualification. No wrong-key, Doppler/delay, selective-modulation, FHSS or burst
claim follows. Actual active/capture durations are unchanged, and measured
signal energy SNR is 146.9834–146.9933.

The coherence-length segment and phase receiver matched all observed admissions
at this point. All four analytic baseline scores together took 0.0205 seconds
after the common workspace load, compared with 9.748 seconds for CPU phase
updates; those are algorithm-stage times, not a 476-fold complete-receiver gain.
The input load alone took 10.687 seconds. Thus the simple segmented likelihood
is a useful inexpensive choice to try when this operating condition suffices.
The weaker −60-dB-Hz result justifies retaining phase continuity for conditions
where segmentation loses receptions; these two points do not establish the
exact sensitivity boundary or prove that either receiver is optimal.

CPU/CUDA FP32 agreed on all 20 winners and admissions; maximum phase log-LR
difference was `2.737e-4`. On the same first capture, differences from CPU FP64
were `6.717e-4` for CPU FP32, `5.696e-4` for CUDA FP32 and `1.279e-13` for CUDA
FP64, again without decision changes. The 20-capture phase update times were
9.748 seconds CPU and 4.185 seconds CUDA; full child-process times were 21.912
and 17.959 seconds. These correspond to observed ratios of 2.33× for updates
and 1.22× for complete scoring. Including the common 190.10-second export stage
reduces the generation-plus-scoring ratio to 1.019×.

Both CPU and GPU timing runs in this second cohort overlapped a heavy workload
using 18 other CPU workers, as recorded by the campaign coordinator. Together
with the original cohort's concurrent exports, single runs and graph setup,
this prevents interpreting the ratios as isolated hardware latency distributions.
The unchanged decisions and reproducible complete calendars are the stronger
evidence here; the timing observations identify useful batching without
requiring more receiver complexity for the −57-dB-Hz successes.

# Fractional timing diagnostic protocol (frozen before Monte Carlo)

## Question and scope

Compare the current five coherent timing points `{-64,-32,0,32,64}` with a direct
quarter-sample grid `[-64,64]` (513 hypotheses), using the same three coarse FFT
projections, 1024 public words, RF offsets `{-0.5,0,+0.5}` cycles per 64-tone
block, six rungs `{2,4,8,16,32,64}`, per-tone scores and priority. The nearest
coarse anchor is `-64` below `-32`, `+64` above `+32`, and `0` on the inclusive
central interval; central wins ties at `±32`. The receiver evaluates every candidate: it is not given the true word or arrival time.

All 64 tones and 32768 samples remain fixed. Transmitters use signed physical
frequency and rectangular support at their actual boundary, with phase measured
from that boundary. The fractional single-path rows use common phase and carrier
offsets `±0.25` cycles per block. The two-ray row adds an 8-sample delayed path
and +2-bin Doppler with stable direct/echo phase; only the phase-reset row resets
phase independently per tone. Guard storage contains the full delayed tail;
receiver scoring uses the same padded `32768+128` sample view. Integer starts
must match the existing transmitter numerically. Finite-window sample movement
is distinct from phase-only correction and is not a receiver qualification.

## Frozen cases and decision

- Quiet calibration: 399 paired records, fresh seed root `0x74696d01`; strict
  `>` cutoff uses one-based rank 398 separately for sparse and dense coherent
  scores, plus a common per-tone rank-398 cutoff. Each joint rule gives an
  admitted per-tone word priority, otherwise admitting its coherent winner. Each
  family has a 0.5% predictive rank budget; their union has a nominal 1% budget,
  averaged over exchangeable calibration/test draws. This is not a confidence
  guarantee conditional on one frozen calibration.
- Fresh quiet holdout: 400 records, independent seed root `0x74696d02`, not used
  to set either cutoff. Report the upper exact binomial interval and every
  false quiet word count, separating each family and the joint decisions.
- Six paired signal rows, 120 records each, independent seed root
  `0x74696d03`: on-grid `±32`, single path at 15 dB; hard mid-cell `±16`, single
  path at 15 dB; uniform fractional `[-64,64]`, single path at 15 dB; same
  fractional row at 18 dB; fractional two-ray at 18 dB; fractional phase-reset
  at 18 dB.
- Share payload, noise, schedule and trial-level phase draws across paired rows.
  Preserve all failures and report correct/wrong/absent, paired gains and
  losses, both cutoffs, and paired exact sign-test result for the primary
  fractional-single-path 15 dB outcome.
- The primary comparison is paired correct admission under each joint rule
  for the fractional-single-path 15 dB row. No waterfall gate applies. The fixed
  1% quiet diagnostic is
  not a rare false-alarm certification. No early stopping or sample expansion
  based on observed direction or significance.

- Arrival/frequency draws use seed `0x74696d06`; phase resets use `0x74696d07`.
  Noise, payload, ordinal and initial phases come from `Trial(0x74696d03, i)`;
  the same padded samples are reused across rows and both receivers. Exact
  power outside the receiver view, including a possible eight-sample echo tail
  beyond its end, is not silently folded back into the input.
- One continuous five-slot control uses seed `0x74696d05`, arrival 16.125
  samples, carrier +0.25 cycles/block, 25 dB active blocks and ordinals 0–4.
  Send words 217, 218, quiet, quiet, 221 in one buffer, retaining carrier phase
  across slots. Use the frozen dense joint rule and existing Calendar; check
  second-quiet closure and reopening. Empty, zero-scale and wholly nonfinite
  input must remain unscored and break the consecutive-quiet run. This single
  example is functional evidence, not a rare-error or field qualification.
- Independent trial workers shorten the campaign only; they do not establish
  parallel reception of one stream. Default to the C++ reported hardware count
  with an explicit worker argument available. The local run uses 12 workers.

The fixed experiment covers this finite 10-bit bank and stated channel rows;
it does not establish field reception, physical receiver completion, general
false-alarm rates, or complete service. Temporary output belongs in
`local-evidence/` and is removed after findings and provenance are retained in
the maintained appraisal record.

## Reproduce and validation

From the repository root, use the existing two-source C++20 build:

```sh
g++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -pthread -Isrc -Iinclude \
  _appraisal/algorithms-matrixModem/block-reception/block_reception.cpp \
  src/search_fft.cpp -o /tmp/matrix-block-timing
/tmp/matrix-block-timing --timing-checks
/tmp/matrix-block-timing --timing-pilot 3
/tmp/matrix-block-timing --timing-coverage 12
/tmp/matrix-block-timing --timing-unrelated
/tmp/matrix-block-timing --pointing-loss
```

The checks evaluate direct double-precision tone sums for all 1,024 words,
three RF candidates and six rungs at eleven timing points, including fractional
values and aperture handoffs, and compare each timing's winning score and word
with the optimized tree. They check integer-TX parity
with nonzero carrier and both path fixtures, native-FFT known-window unit power,
phase-only reuse at edges/handoffs, and the equal/absent/opposite-neighbour
boundary controls. Known word/time enter only these diagnostic oracles. Scalar
validation is disabled in the Monte Carlo and pilot. The pilot times warmed,
serial complete paired block scoring on both quiet and fractional active input;
it includes frontend, quantization and both searches but excludes source
creation and schedule construction. It is not complete application service.

The new comparator evaluates 9,455,616 coherent word scores and 99,283,968
complex tree additions per block, gathering 33,619,968 tone projections. Its
192 first-stage FFTs and 6,144 per-tone scores are shared. The paired harness also
runs the five-point comparator and its exact-parity copy. The four-bit input/bin
representation uses floating-point FFTs, rotations and accumulation here; it is
not four-bit transform arithmetic.

## Results — 28 November 2026

The three frozen cutoffs were **5.5478988046551079** for shared per-tone,
**13.65871519005635** for sparse coherent, and **16.317710015395722** for dense
coherent. The larger searched maximum raises the coherent cutoff. All table
entries are **correct / wrong / missed**; a wrong admission in quiet is false.
Each active row contains 120 draws; the quiet row contains 400. Rows share draws
where declared and must not be pooled as independent trials.

| Fixture | Per-tone | Sparse coherent | Dense coherent | Sparse joint | Dense joint | Joint gains / losses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Quiet, 400 | 0 / 1 / 399 | 0 / 2 / 398 | 0 / 2 / 398 | 0 / 3 / 397 | 0 / 3 / 397 | — |
| On-grid single ray, 15 dB | 5 / 0 / 115 | 105 / 0 / 15 | 92 / 0 / 28 | 105 / 0 / 15 | 92 / 0 / 28 | 0 / 13 |
| Mid-cell single ray, 15 dB | 8 / 0 / 112 | 0 / 1 / 119 | 99 / 0 / 21 | 8 / 1 / 111 | 99 / 0 / 21 | 91 / 0 |
| Fractional single ray, 15 dB | 11 / 0 / 109 | 4 / 1 / 115 | 106 / 0 / 14 | 15 / 1 / 104 | 106 / 0 / 14 | 91 / 0 |
| Fractional single ray, 18 dB | 102 / 0 / 18 | 4 / 1 / 115 | 120 / 0 / 0 | 102 / 0 / 18 | 120 / 0 / 0 | 18 / 0 |
| Fractional two-ray, 18 dB | 9 / 0 / 111 | 4 / 1 / 115 | 106 / 0 / 14 | 13 / 1 / 106 | 106 / 0 / 14 | 93 / 0 |
| Fractional phase reset, 18 dB | 100 / 0 / 20 | 0 / 1 / 119 | 2 / 0 / 118 | 100 / 0 / 20 | 100 / 0 / 20 | 0 / 0 |

For the primary uniform-fractional 15 dB row, correct joint admission increased
from 12.5% (pointwise 95% Wilson interval 7.72–19.60%) to 88.33%
(81.37–92.92%). The 91 gains and zero losses give an exact two-sided paired
test probability of **8.07794 × 10⁻²⁸**. This is a comparison conditional on the
two frozen cutoffs and the stated eight-schedule mixture, not a universal
sensitivity curve. The cost on the on-grid control was thirteen lost admissions.
The expanded coherent search alone recovered only two phase-reset words;
the unchanged per-tone route protected all 100 joint recoveries there.

Both joint rules admitted 3/400 quiet blocks, an observed 0.75% with exact
one-sided 95% upper bound **1.92692%**. This small holdout therefore does not
certify a frozen rate of at most 1%, much less the attended 10⁻⁴ target.
Equal false counts do not establish identical admitted blocks or identical
false-admission laws. The historical rare-error failure remains unchanged.

The dense joint continuous example returned **217, 218, absence, absence, 221**,
closed at slot 3 (the second scored quiet slot), and reopened at slot 4. Empty,
zero-scale and wholly unusable input remained unscored and interrupted quiet
completion. One fixed stream supplies functional evidence only. The historical
`--pointing-loss` run reproduced every prior count, including all 64/64 complete
five-slot episodes, after integrating the optional comparator.

### Arithmetic and finite-window checks

The direct-double winner audit passed at all eleven selected timing hypotheses;
the original sparse score and word matched bit for bit. Integer fractional-TX
parity, including both carrier signs and the delayed ray, had maximum sample
difference **8.33 × 10⁻⁹**. Every known-arrival native-FFT oracle retained unit
power within 2 × 10⁻⁶. These checks separate the sign convention and waveform
from the searched receiver's decision.

For the one prespecified noiseless word, the nearest reused aperture with the
exact arrival's phase correction retained about 0.879 power near ±32 samples,
versus unity with the actual window. Including quarter-sample grid error gave
a minimum of **0.855198** across the twelve tested arrivals (about 0.679 dB
loss). This is a constructed-word result, not a worst-case bound. The direct
`U/16` boundary controls retained **1, 0.87890625 and 0.765625** power with equal,
absent and opposite-phase neighbouring samples respectively. The last is the
earlier 1.160 dB counterexample. A denser phase grid does not remove that loss.

### Runtime and provenance

The run used an **AMD Ryzen 5 PRO 5650U**, affinity CPUs 0–11, 15,309 MiB system
RAM, Debian GCC **14.2.0**, and the CPU native FFT. No GPU arithmetic ran.
The complete fixed campaign plus stream/control checks took **108.14 s** wall,
1,128.58 s user CPU and 10.63 s system CPU; maximum resident set was
**50,356 KiB**. Twelve independent trial workers accelerate experimentation,
not one received stream. A Python `subprocess`/`resource` wrapper measured this
native binary; `/usr/bin/time` was unavailable and its first invocation ran
zero trials. No numerical modem work ran in Python.

Before the batch, three warmed serial comparisons averaged **0.1919 s** per
quiet block and **0.2091 s** per fractional active block. This includes the
paired receiver work on a configured 10.24 s observation, not application
stream service or a 16/24-bit capacity estimate. The capture occupies 540,676
bytes; immutable shared rotations 1,446,144 bytes; local dense score arrays
1,232,896 bytes. These are component sizes, not total workspace admission:
input, frontend scratch, plans, allocator and thread stacks also live.

The local pilot made a roughly two-minute batch practical without remote setup.
**Cloud spend was $0; this task provisioned no resources.** A longer CPU batch
or accelerated implementation should reconsider RunPod before Linode under
the user's eight-hour, $30/hour allowance. This experiment has no CUDA path
from which a rented GPU alone would obtain a speedup.

Base revision: `502227cdbdfbe204781c84da538df4a43884bd33`. Unchanged repository FFT,
headers and research RNG are identified by that base. SHA-256 for the measured
experimental sources and binary:

```text
block_reception.cpp   eb342e5e9ee39b755f5bec51798ce77d1d43704bf73e45b20425f75d0bb742bf
timing_trial.hpp      e96d3f640aa9b03781ab7a41c0fe908a41f69a5d0d89129ec2afadc62b24ef89
timing_experiment.hpp d6a3581a6b991b92d079bd530425c1392f1e07ad2c9295caea4536968c1c45da
matrix-block-timing   d6bcba0c5fc90c7595d288bfc836ef3b1fe642f35477396f95d180f25f4490fc
```

The figures and seeds above retain the complete small trial result. Raw stdout,
resource receipts and temporary binaries were removed after independent source
review and documentation checks; they were not an additional dataset.
The native experiment remains to reproduce and extend this unresolved
fractional-boundary/complete-service investigation. [Research interpretation and literature](../../../_ref/algorithms-matrixModem/fsk-timing-coverage.md)

## Follow-on unrelated-schedule control

After observing the fixed timing comparison, freeze a separate control before
taking more samples: 160 fresh single-ray wrong-schedule blocks at each of
24 and 36 dB, using the existing `other` key fixtures, `Trial(0x74696d08, i)`
and arrival/carrier root `0x74696d09`. Pair noise, word, schedule and fractional
arrival across both levels and receivers. Use the exact three cutoffs printed
above without retraining. Every admission is false for the configured receiver,
even if its word label coincides with the other transmitter's label. Report
each family and joint count separately from quiet and intended-signal trials.
Stop after these 320 blocks regardless of outcome. The control asks whether
quiet calibration transfers to strong unrelated traffic; it cannot certify a
rare false-admission rate.

The completed control produced the following false admissions; all other blocks
were rejected. The two levels share observations and are not independent evidence
that can be pooled.

| Wrong-schedule level | Per-tone | Sparse coherent | Dense coherent | Sparse joint | Dense joint |
| --- | ---: | ---: | ---: | ---: | ---: |
| 24 dB, 160 blocks | 1 | 0 | 0 | 1 | 1 |
| 36 dB, 160 blocks | 1 | 0 | 0 | 1 | 1 |

For either joint rule at either level, the exact one-sided 95% upper bound is
**2.93045%**. The observed coherent counts do not show an added failure in this
small control; they do not establish rejection of arbitrary unrelated traffic.
No threshold or intended-signal result was changed after this control.

Adding the separate command changed the experiment source and binary after the
main campaign. Its final SHA-256 identities, distinct from the main-run identities
above, are:

```text
block_reception.cpp   bd475bb4f728e34514080cda041e99628bf790ed819f0de9cac9e46a3899098d
timing_trial.hpp      e96d3f640aa9b03781ab7a41c0fe908a41f69a5d0d89129ec2afadc62b24ef89
timing_experiment.hpp 639baf511b75c6d0b60cce38386077dc97eeb5d4fa914dd0b524491c2beba8b0
matrix-block-timing   5348388d4ccf7d0da68608b31761c5042c5720dc686e4315c4c5c0ae32bf0281
```

The unchanged scorer's deterministic `--timing-checks` passed again with this
final binary. The main campaign was not resampled. The control's fixed seeds,
cutoffs, counts and source identities above retain its evidence without keeping
duplicate raw output.

# Configuration and acquisition extension

This continues the [broad study](../2026-11-03-broad/README.md) on source
`23181c2d` plus the changes recorded here. It remains an isolated three-carrier
experiment. The ordinary receiver and its regression campaign are outside scope.

## Software changes and focused evidence

The streaming simulator now accepts zero RF shift and a shared baseband/shift
clock. Sharing correlates static sampling/RF ppm within each endpoint; TX and RX
remain independent. Zero shift removes the shift profile, and both topologies
count base phase diffusion once. Stochastic sampling jitter is not implemented.
The explicit additional modulation-rate control permits a modulation-only stress
channel with spread None while preserving the historical default bypass.
See the [channel contract](../../../channel-models.md#versioned-three-carrier-selective-modulation)
and [v3 model description](../../../triad-v3.md).

The isolated 14-test group passed locally, including measured positive/negative
shared-clock phase and sampling-ratio tests. Four prior configurations retained
28/28 byte-identical sample/statistic arrays against the preserved original
binary. [Default comparison](evidence/default-compatibility.json) and
[measured shared-clock proof](evidence/shared-clock-sampled-proof.json) retain
identities and numerical results. These checks do not qualify actual radios.

The current [factorized census](evidence/configuration-census.json) represents all
9 shift choices and 7 shift-oscillator choices. Its 2,500,485,120 intent
coordinates partition into 1,066,383,360 source-geometry rejects, 244,545,732
nonpositive capture margins and 1,189,556,028 positive capture margins. This is
algebraic configuration coverage, not that many transmissions or receptions.
The old unsupported topology class has been evaluated through the remaining
geometry/capture gates; its removal does not imply every such case works.
Fresh native inputs contain 136 geometry receipts and 100 validation receipts;
61 accepted formula comparisons had zero numerical margin discrepancy, while
39 source-geometry rejections remain recorded. The checked input design requires
six directed boundary probes, 90 seeded probes and four topology probes.

## Reusing complex statistics for frequency acquisition

`tools/triad_group_acquire.py --frequency-offset-grid LOW HIGH MAX_STEP` adds a
fixed public CFO bank without regenerating sampled projections for each CFO.
It rotates original block correlations at their physical midpoint before
reblocking. Each timing × CFO alternative enters the normalized likelihood
mixture and residual-noise calibration allocation. Omitted or zero-only selection
preserves the previous scores. The explicit list option is
`--frequency-offsets-hz F0 F1 ...`.

This transformation is exact for a piecewise constant phase template and
approximates continuous CFO within each original block. The default maximum
block phase loss is 0.01 dB, bounded by a phase arc for arbitrary shaping and
masks, rather than a rectangular-pulse sinc assumption. The bound does not
certify the nonlinear likelihood error. Actual delayed paths, timestamps,
erasures and noise remain those of the exported observation; changing the
underlying projection basis still requires new projections.

All 18 focused group-receiver tests passed locally and on the Pod's Python 3.11,
including the negative-scientific-frequency CLI case that originally failed
there. The compatibility repair changes argument parsing only. A separate native sampled
HF-worst/Each65 comparison at rate36, Fs1024 and 16-sample blocks measured
maximum normalized correlation error 7.09e-5 against a 1.47e-4 operator bound,
and maximum group log-score difference 1.28e-4. Both receivers missed that
capture. This is approximation evidence, not a sensitivity or speedup claim.
The [record](evidence/frequency-reuse-native.json) binds the inputs and scorers.

## Sampled schedule and configuration campaign

The [physical campaign recipe](physical/README.md) defines complete tight-arrival
banks for three missed-dwell schedules, actual internal burst waits, rate/Added
Symbols contrasts, explicit None+modulation, direct VLF zero shift and shared
microwave clock. A paired screen precedes independent paths. The
[first nine-plan recount](physical/evidence/README.md) covers 222 sampled
condition captures and 666 receiver scores; the
[additional four-plan record](physical/evidence/additional-README.md) adds 60
captures and 180 scores. A selected
[two-plan sensitivity followup](physical/evidence/frontier-README.md) adds 36
captures and 108 scores, bringing this physical campaign to 318 captures and
954 method scores. Methods and C/N₀ sweeps reuse physical paths, so these are
not counts of independent reliability trials. The separate phase study below
adds 40 sampled captures under narrower assumptions.

| Independent screen | Whole observation + public CFO, correct/16 at strict gate | Short-window correct/16 |
| --- | ---: | ---: |
| None + explicit Each65, 6dB-Hz | 9 | 1 |
| Shared-clock 5.8GHz + Each65, 6dB-Hz | 11 | 0 |
| Random25% missed dwells, 6dB-Hz | 15 | 0 |
| Three internal wait tiles, 6dB-Hz | 16 | 0 |
| Rate0.1/Added1000, HF-best/Each65, −16dB-Hz | 16 | 0 |

The last case sends for 3,842s, rather than the eight-second request: minimum
pattern complexity controls that duration. Its 15 public timings ×95 CFOs
require 1,425 effective groups. Measured signal energy lies 4.37–6.86dB above
the favorable known-waveform strict presence benchmark. This is a useful
maximal-spreading success with energy margin, not a near-bound or −60dB-Hz
qualification. Full capture generation took a median17.25s on one CPU worker.

Reusing those same 16 paths at −20 and −18 dB-Hz brackets the transition:

| C/N₀ | Correct / wrong / missed, strict | Correct / wrong / missed, relaxed |
| --- | --- | --- |
| −20 dB-Hz | 5 / 0 / 11 | 8 / 0 / 8 |
| −18 dB-Hz | 12 / 0 / 4 | 15 / 0 / 1 |
| −16 dB-Hz, earlier result | 16 / 0 / 0 | 16 / 0 / 0 |

The public-CFO receiver selected the correct raw bit on all 16 paths at both
new levels; misses fell below the unchanged admission gate. No null or
wrong-key control was admitted. The extra levels were selected after the
−16 result, so this is a paired followup, not independent qualification.
At −20, available energy is 0.37–2.86 dB above the favorable known-waveform
99% presence benchmark, yet strict reception succeeds only 5/16 times.
Unknown channel and synchronization, finite-bank mismatch and calibrated
likelihood costs therefore remain material. The observed 4 dB improvement
to 16/16 does not establish an equivalent duration increase or a qualified
99% boundary; even 16/16 has a 95% one-sided lower bound of 82.925%.

The short-window model also admitted one wrong bit and two wrong-key controls
at 12dB-Hz in the light-cell screen. Whole-observation methods resolved those
examples. No sampled null was admitted, but the control counts are too small
for operational false-admission qualification. Noise-only likelihood guarantees
do not establish correct bit selection or rejection of structured interference.
The measured remaining random25 miss has energy sufficient for an almost-unity
known-waveform detection probability, so total energy alone does not explain it.
A [clean-signal projection audit](evidence/projection-README.md) finds that the
best available whole-observation two-tap representation retains only 42.0% of
that energy, a 3.77 dB loss. The noisy receiver already selected that same
timing/CFO group. Allowing independent short-window gains raises oracle
retention to 62.2%, but also fits more wrong-pattern energy; it is not a measured
sensitivity improvement. Timing, phase and selective-channel mismatch remain
unseparated. The diagnostic took 2.23 seconds including launch and 730 MiB peak
RSS, exceeding its estimated 512-MiB memory target.

The inherited whole-bit Symbol Burst setting changes slot calendar metadata;
`--burst-wait-tiles 3` is the separate control that inserts silence inside the
observed bit. FHSS here remains a shaped envelope plus receiver erasures, not
physical RF retuning. Common-frequency reuse does not supply missing clock-rate
or keyed-epoch searches. The receiver supports at most 5,000 base timing groups,
while the native grouped exporter permits 10,000. The default wide interval at
rate360/Added10 requires about 35,200 positions under the chosen 3-dB timing
approximation and remains unsupported by this route. No silently coarser bank
is counted as complete acquisition.

The complete census and selected waveform experiments answer different parts
of the requested cross-product. In particular:

| Configuration or condition | Current failure or limit | Evidence and useful next action |
| --- | --- | --- |
| 1MHz/30MHz rates with the census's automatic geometry | Bounded tile workspace or automatic sample-rate range rejects before reception | All such coordinates are source rejects; design a separate high-throughput sample path before a sensitivity campaign |
| Fine symbols longer than a protected FHSS dwell | No whole pattern cell fits the dwell | Change rate/spreading/dwell geometry; increasing C/N₀ cannot repair it |
| Free-running microwave oscillators with very narrow capture | Signal can lie outside the captured band | Evaluate the deterministic capture bound first; simultaneous-carrier cancellation does not recover discarded energy |
| Several-second arrival uncertainty at high fine-symbol rate | Complete timing bank exceeds retained-group limits | Explicitly unsupported by this grouped path; retain the lower-rate wide-bank results from the baseline study |
| Long observation with a single constant complex channel gain | Phase evolution can cancel accumulated evidence | Compare segmented and finite-state phase models on the same retained complex data |
| Each65 with spread None | Historical default bypasses added modulation | Use an explicit nonzero modulation rate for this stress case; preserve the old default when reproducing old seeds |
| Strong wrong-key signal | A Gaussian-noise false-admission guarantee does not reject every structured interferer | Prior short-window Aurora failures remain unresolved; noise-only controls are insufficient |
| Physical FHSS retuning, changing sample-clock jitter, native radio/audio limits | Not implemented or not measured here | No operational qualification is claimed from envelope-only or static-clock simulation |

All 17 rates, all oscillator and shift choices, both timing policies, all spread
and modulation labels, both margins and the declared spreading/burst/FHSS axes
are represented in the factorized configuration census. Their positive capture
margins are not reception results. The sampled campaigns select physically
valid cases and retain the remaining exclusions; they do not turn billions of
configuration coordinates into billions of independent trials.

## Phase continuity at the TCXO boundary

The [phase experiment](phase/README.md) reuses the existing finite circular-state
likelihood. Its first experiment has orthogonal patterns, known unit amplitude,
known acquisition and a common Brownian phase law. It is a controlled
sufficient-statistic experiment, not sampled triad or real oscillator evidence.
At the TCXO -60 dB-Hz/850-day equivalent, independent 256-path validation gave
145 strict and 202 relaxed correct admissions, versus 45 and 105 for the
preselected segmented baseline, and zero for whole-observation constant phase.
No wrong/null admissions occurred in that small sample. At -57 dB-Hz the phase
receiver accepted 256/256; the pointwise one-sided 95% lower bound is 98.84%,
which does not establish 99% reliability. Phase-grid and arithmetic checks,
assumptions and original failures are retained with that experiment.

The subsequent source-bound sampled replay generated 16 independent signals and
four nulls at each of −60 and −57 dB-Hz. Each capture actually advances roughly
854.45 days of physical simulation time and 18,456,032 samples; no short trace
was repeated or amplitude-scaled into a long observation. At −60, finite-phase
inference admitted 9/16 strict and 16/16 relaxed, versus 3/16 and 7/16 for
coherence-length segmentation. At −57, both methods admitted 16/16 at both
gates, whereas constant-phase whole-observation integration admitted 0/16.
No wrong admissions or null admissions occurred in either small cohort.

These are known-alignment, known-amplitude, flat-channel Brownian-phase
experiments. They do not qualify the complete GPSDO profile, unknown clock/epoch
search, two-ray fading or added selective modulation. Even 16/16 has a one-sided
95% reliability lower bound of only 82.925%. The
[actual −60 evidence](phase/actual-summary.json) and
[independent −57 evidence](phase/actual-cn57-summary.json) retain the complete
source/seed bindings and precision comparisons. The earlier pilot whose loaded
executable was uncertain remains excluded.

On identical 20-capture inputs at −60, GPU phase updates took 4.10s versus
10.37s on one CPU thread (2.53×); whole scoring took 17.18s versus 22.51s
(1.31×). Including measured waveform generation reduces that observed ratio to
1.026×. FP32 CPU/GPU results changed no winners or admissions. These are
single-run measurements with documented concurrent CPU export work, not timing
confidence intervals. At −57 the simple segmented scores matched reception at
much lower scoring cost, so the richer model is not prescribed universally.

## Efficient validation and remaining scope

Grouped export previously synthesized every receiver template twice for its
clean and sampled-noisy observations. The paired streaming implementation now
shares that deterministic work while retaining independent source, channel,
RNG, filter and nonlinear frontend state. It charges the additional memory
before construction and falls back to the old sequential path when needed.
Four focused old/new cases retained 26 byte-identical numerical arrays and all
semantic manifest fields. Both local and cloud isolated 14-test groups passed.

Two separate one-core, 455-group/189,500-sample benchmarks retained all seven
arrays byte-for-byte. [Local wall time](evidence/exporter-fusion-local.json)
fell 348.69→176.66 s (1.97×); [cloud wall time](evidence/exporter-fusion-cloud.json)
fell 231.30→115.70 s (2.00×). Cloud peak RSS rose 8,224→10,264 KiB. These are single
sequential before/after pairs, not timing confidence intervals or receiver
sensitivity gains. The exporter remains serial within a capture; `--threads`
does not parallelize its group loop. Independent captures were batched across
CPU workers. FFT timing reuse remains a larger unimplemented opportunity,
especially where sample count and timing-bank size grow together with rate.

Run the affected Python tests and `./build.sh test triad --cli` for implementation
changes. The long exports and full physical campaign are optional research, not
routine regression prerequisites. Reuse matching observations for receiver-only
comparisons; keep physical source hashes distinct from scorer hashes and retain
all failed and incomplete jobs in denominators. Standard strict 99%/1e-4 per day
and relaxed 90%/.01 per 12 hours targets remain separate and configurable.

The [preservation and reuse record](evidence/preservation.md) identifies the
7.17-GB local archive, its 8,635 independently verified payload files, the
original/repaired source versions and cheap score-recount routes. Full waveform
regeneration is unnecessary for those recounts or compatible receiver-only
comparisons.

Configuration acceptance, conditional synthetic success, actual sampled success
and reliability qualification are different evidence levels. High-rate resource
limits, wide blind acquisition, stochastic sample-clock evolution, general
wrong-key interference, physical FHSS retuning and arbitrary propagation remain
unqualified. Marginal numerical or rank optimizations are not a substitute for
these missing physical and acquisition hypotheses.

# Bounded timing acquisition and DSSS V2 — local manual checkpoint

Status: implementation and bounded local validation; **not overall qualification**.
No push, publication, hosted CI or platform/SDK qualification was performed.
The preceding runnable receiver and original raw diagnostic are preserved.

Source inspection confirms the 4096-samples-per-chip boundary is no longer a
universal private-receiver limit: explicit paired oscillator banks can use the
preceding pulse-moment/affine paths above it, subject to geometry and workspace.
The low-level unpaired legacy bank still keeps its 4096 limit to preserve its
small idle-memory contract. This stage retains both paths and the raw diagnostic.

## What changed

Qualified coherent acquisition selects the full admitted union of original timing
cells for every canonical stream phase and frequency/clock lane. It includes the
fractional refinement margin, full clock envelope and admitted timestamp error.
Zero selected cells skip private construction. Up to 32 starts use paired direct
matching; larger supported sets use bounded tiled convolution. The direct cap
has measured kernel evidence; tile choice uses a complete numerical cost model.
An allocation-free chooser compares complete numerical work against the original
FFT; unsupported or more expensive sets retain the original backend. Both I/Q,
existing energy/covariance corrections, original acquisition-family trial charges
and per-evaluation continuation penalties remain.

Separated timing components execute when their own complete symbol observations
are ready. They obey original peak exclusion and candidate capacity; they cannot
silently eliminate distant epoch, frequency or clock alternatives. Continuation
stops before the next unscored competitor. Six-second physical absence is separate
from arrival uncertainty, and neither EOF nor cancellation supplies silence.
A cold partial EOF can replay its bounded unfinished hop under the original
finite-final trial charge, even after its last selected component executed. The
checkpoint expires at the original full-hop observation frontier. Final replay
charges only physically observed starts; published output is never rolled back.

The DSSS V2 layer uses separate, newly domain-separated secret permutation and
unit-magnitude rotations over complete fine chips in each bit. Inner independent
0/1 patterns and framing stay unchanged. Final pulse shaping follows the chip
transformation. A 0.5 pre-limiter amplitude supplies headroom. V2 has an explicit
wire-version selector; DSSS-Off and legacy diagnostics remain available. Both
ends must select the same version. V2 retains two canonically addressed symbol
maps across phase changes; absolute-chip caches are invalidated each time.

## Final-candidate execution, unchanged original PCM

The final5 binary repeats the DSSS1000 endpoints against the frozen preceding
optimized receiver, using the same saved 60-second PCM and all five lanes.
Three alternating pairs per endpoint, isolated CPU 11, one worker; construction,
input/search, polling, finite finish and destruction are included.

| Admitted width | Baseline CPU median | Final5 CPU median | Final5 wall median | Paired speedup median [observed range] |
| ---: | ---: | ---: | ---: | ---: |
| 6 s | 13.001 s | 5.988 s | 5.989 s | 2.171× [1.665, 2.221] |
| 1 s | — | 2.986 s | — | one final-source control |
| 100 ms | — | 2.365 s | — | one final-source control |
| 10 ms | — | 2.168 s | — | one final-source control |
| 1 ms | 9.866 s | 1.829 s | 1.829 s | 5.395× [5.380, 5.630] |

The three interior widths are single final-source controls, with no new baseline
pair or uncertainty interval; every control completed the same bit and physical
absence assertions. Endpoint ranges retain every run and are descriptive, not
precise 95% intervals.
Final5 total CPU falls 3.274× as the admitted width falls 6 s → 1 ms. Its scored
start counter falls 2,275,570 → 1,500; the six-second count includes one cold EOF
replay, so it is executed work rather than a count of unique alternatives.
Final5 retained peak is 35,867,785 / 35,867,826 bytes at the two endpoints, versus
baseline 35,863,482: at most 4,344 extra bytes, within the same 64 MiB workspace.
Observed final5 process RSS is 55.27–55.45 MB, including harness PCM and runtime.
At one millisecond it uses 25 direct and 15 partitioned jobs, no broad input FFT
or full template FFT jobs. First-bit media time is 13.7728 s versus 26.2144 s in
the preceding receiver, with physical-bit-end to next-poll delay 0.8415 s versus
13.2831 s. Six-second-window readiness retains the original 26.2144 s frontier.
These are complete one-bank receptions; the rolling Live result below has a
different scope and no completed baseline ratio.

The six-second finite-EOF replay removes the preceding integration's charge for
2,260,160 unobserved acquisition alternatives. Final acquisition trials agree;
total trial counts still differ by 14 continuation evaluations (16,520,014 versus
16,520,028), and established/published-tail equivalence remains unqualified.
Accepted bits and physical completion agree. The final-source replay adds CPU,
so the historical table below is not substituted for this final measurement.

Additional final-source arithmetic controls keep the same admitted cells,
component scheduler, PCM and thresholds, but force the original full FFT scorer:

| Width | Forced full-FFT CPU | Automatic CPU | Single-pair ratio |
| ---: | ---: | ---: | ---: |
| 1 ms | 3.929 s | 1.875 s | 2.10× |
| 6 s | 7.236 s | 5.956 s | 1.21× |

Both controls retain exactly 16,520,014 trial charges and matching output/event
identity and physical completion. This isolates a real arithmetic reduction from
scheduling and exclusion effects. These are single controls, not repeated speedup
intervals; they validate the chosen overall mix. They do not measure whether the
retained broad full-FFT jobs would beat forced partitioning.

## Component profile and remaining bottleneck

A separate frozen-source build adds coarse timers, preserving output, workload
counts and source arithmetic. One worker avoids overlapping process-CPU timers.
Its total CPU is 5.987 s at 6 s width and 2.105 s at 1 ms; versus the adjacent
plain control this is +0.52% and +12.25%. Timing overhead and single-run variation
make these diagnostic component costs, not replacements for primary totals.
The following nonoverlapping accounting uses exclusive times; fused operations
remain explicitly fused. Full covariance/template-energy work inside those
scorers is not independently timed.

| Component CPU seconds | 6 s width | 1 ms width |
| --- | ---: | ---: |
| Input validation, projection, ring/scheduler residual | 0.0192 | 0.0197 |
| Observation copy and energy prefix | 0.0061 | 0.0058 |
| Input transforms (full + partition) | 0.0604 | 0.0084 |
| Separate private template generation | 3.0708 | 0.4597 |
| Paired direct generation + contraction (fused) | 0.0000 | 0.9058 |
| Template transforms | 0.9415 | 0.1072 |
| Partition products | 0.0967 | 0.0723 |
| Correlation inverse transforms (full + partition) | 0.6485 | 0.0003 |
| Outer presence guard | 0.7003 | 0.0849 |
| Continuation/tracking inclusive of its scorer | 0.3077 | 0.3074 |
| Execution scratch allocation/initialization | 0.0104 | 0.0098 |
| Progress publication | 0.0008 | 0.0008 |
| Remaining loops, evidence and unattributed overhead | 0.1243 | 0.1229 |

The remaining bottleneck is private-template generation/contraction and continued
scoring, not input mixing/filtering in this case. Input handling follows original
sample count; template work follows complete waveform length and retained
hypotheses; direct products follow retained starts; partition transform sizes and
counts follow the chosen tiles. Rolling admissions add constructors and fresh
canonical templates. No additional downconversion/decimation/filter stage was
introduced, so there is no new filter transition, delay or spectral-tail loss to
hide. This profile does not decompose the compact very-long-symbol backend or the
entire rolling Live frontend; those remain unmeasured component scopes.

## Historical width sweep, unchanged original PCM

Frozen optimized baseline: `tight-envelope-baseline-9ee268e`, exact HEAD
`9ee268e23449fbdc43c66a792fd1321b359fcbe3` plus recorded startup dirty documentation.
Historical measured receiver snapshot: `tight-candidate-paired-3`. These rows
precede the final EOF/cache/model changes. They are retained as investigation
evidence; final-candidate timing results are reported separately below.

27 complete pairs / 54 measured fresh-process executions. Three alternating pairs
per row, isolated CPU 11, Release/GCC14.2, one numerical worker, synthetic keys,
identical saved PCM per pair. All are full 60-second captures with a complete bit
and observed physical absence, one epoch bank with all five oscillator/clock pairs.
The provider actually qualifies the specified arrival map; these are not silent
fallback measurements. PCM generation/loading and initial process/OpenSSL setup
are excluded from receiver timing; construction, push, polling, finish/drain and
destruction are included. The harness launch barrier excludes metadata work from
receiver timing. All declared trials, including the slow DSSS10 run, are retained.

| DSSS | Admitted width | Baseline CPU s | New CPU s | Paired speedup median [observed range] | New wall s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | 6 s | 0.754 | 0.757 | 0.996× [0.875, 0.998] | 0.761 |
| 10 | 0.001 s | 0.273 | 0.267 | 1.025× [1.000, 1.025] | 0.269 |
| 100 | 6 s | 2.055 | 1.538 | 1.332× [1.324, 1.523] | 1.546 |
| 100 | 0.001 s | 1.128 | 0.588 | 1.917× [1.899, 2.082] | 0.592 |
| 1000 | 6 s | 12.582 | 5.361 | 2.356× [2.344, 2.398] | 5.390 |
| 1000 | 1 s | 9.730 | 2.828 | 3.430× [3.340, 3.576] | 2.842 |
| 1000 | 0.1 s | 9.500 | 2.244 | 4.202× [4.187, 4.295] | 2.256 |
| 1000 | 0.01 s | 9.452 | 2.159 | 4.347× [4.247, 4.472] | 2.171 |
| 1000 | 0.001 s | 9.783 | 1.867 | 5.261× [5.063, 5.344] | 1.877 |

Ranges describe the three observed pairs; they are **not precise 95% intervals**.
These are one-bank algorithm measurements, not whole rolling Live speedups.
At DSSS1000, width 6 s → 1 ms reduces scored original positions 2,086,730 → 1,500
and median total new CPU 5.361 → 1.867 seconds (~2.87×). Position count falls much
faster than total CPU because full symbol/template processing, input ingestion
and retained epochs remain. No orders-of-magnitude total speedup was achieved.

At 1 ms, the new receiver runs 25 paired-direct and 15 partitioned jobs, no broad
input/template FFT jobs, 10,346 partition input transforms and 8.51 million total
partition transform points. Six seconds retains 40 original FFT jobs plus 25
partitioned jobs; this broader geometry reaches the modeled crossover/fallback.
Its retained full-FFT jobs have not been compared with a forced-partition control,
so that part of the optimization remains unfinished under the requested criterion.
The fixed-bank accounted peak is 35,867,778 bytes versus baseline 35,863,482
(+4,296 bytes). Tiled work borrows existing transform scratch, not an additional
large input buffer. D1000 first-bit media time improves 26.2144 → 13.7728 seconds;
physical-bit-end to poll delay improves 13.2831 → 0.8415 seconds. This includes
canonical-phase alternatives and scheduler margins; one millisecond uncertainty
cannot remove the 12.8-second physical symbol. It is not sound-card wall latency.

The saved D1000 configuration is 40,000 real input samples/s, carrier 7,500 Hz,
8 samples/fine chip (200 µs), 8,000 samples/inner chip (200 ms), 512,000 samples/bit
(12.8 s), 10,000 projected complex bins/s, 128,000 bins/bit. Original transform
262,144 bins; original hop 13.4144 seconds. Workspace is 64 MiB for fixed-bank
comparisons. Timing trials retain five frequency/clock pairs, including the UTC
correction union. Widths use independently bounded synthetic provider metadata;
actual mapped width includes small outward numerical/rate inflation.

| Case | Input Fs | Stream carrier | Inner Rate | Fine chip | Inner chip | Symbol | Frequency/clock pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DSSS10 | 48,000/s | 9,000 Hz | 1,200 Hz | 166.667 µs | 1.66667 ms | 106.667 ms | 5 |
| DSSS100 | 40,000/s | 7,500 Hz | 100 Hz | 200 µs | 20 ms | 1.28 s | 5 |
| DSSS1000 | 40,000/s | 7,500 Hz | 10 Hz | 200 µs | 200 ms | 12.8 s | 5 |

Each baseline/current pair retains that same input rate and PCM. These are also
the application-selected rates for those configurations, not a separate sampling
speedup. All use eight input samples per fine chip, coherent acquisition and
64 MiB workspace. RF shift is 1 MHz with independent GPSDO XO/OCXO models, margin
3×; maximum clock magnitude is about 0.00060000000009 ppm, frequency half-width
0.0003054 Hz (D10) / 0.0003045 Hz (D100/1000). The table varies waveform geometry
between cases, never within a receiver comparison.
Fake FHSS keeps actual audio at the configured Shift. Its planner separately
reserves the highest illustrated RF shift (20.9 MHz here); that prospective
oscillator bound must not be mistaken for a hardware frequency hop or the fixed
1 MHz runtime benchmark bound. Default timing still means 13 initial epochs and
an independent ±7-second per-bank start scan; the admitted-width sweep is a
qualified arrival policy, not removal of those epoch alternatives.

## Final-source regressions and long originals

The prospectively capped matrix completed 26 pairs / 52 receiver executions in
235.63 seconds, plus ten separate PCM generations. Every bit/completion assertion
passed; no declared matrix case or cap was omitted. Three alternating pairs per
ordinary/original case, one pair at each of two joint boundaries. All slower runs
are retained. Ordinary cases are fast/wider/weaker **Robust** geometries, not
qualification of the separate Fast Modem protocol.

| Case | Baseline CPU median s | Final5 CPU median s | Paired speedup median [range] | Observation scope |
| --- | ---: | ---: | ---: | --- |
| fast | 0.529487 | 0.537975 | 0.969× [0.956, 0.989] | bit + observed absence |
| wide | 0.076052 | 0.078319 | 0.935× [0.933, 1.093] | bit + observed absence |
| weak | 0.070332 | 0.071850 | 0.979× [0.968, 0.991] | bit + observed absence |
| original-p005-auto | 0.199604 | 0.199160 | 0.990× [0.801, 1.002] | bit + observed absence |
| original-p5-auto | 0.201227 | 0.198702 | 1.009× [0.996, 1.013] | bit + observed absence |
| original-p005-6000 | 0.260179 | 0.267447 | 0.994× [0.972, 1.016] | partial |
| original-p5-6000 | 0.262395 | 0.276726 | 0.934× [0.930, 0.968] | partial |
| original-1500-auto | 0.263514 | 0.265571 | 1.007× [0.913, 1.042] | partial |

The ordinary F1 controls use carrier 1500 Hz / Fs 6000, Rate 1200/100/100,
chip lengths 10/120/120 samples, symbols 640/7680/61440 samples
(0.106667/1.28/10.24 seconds), three oscillator/clock pairs, 64 MiB workspace and
60-second captures. Targets are 60/20/10 dB-Hz; input C/N0 72/32/22. They are
functional regression cases, not near-threshold sensitivity curves. Their
publication delays remain unchanged. The few-percent slower controls and the
25%-slower first low-carrier run preclude a universal nonregression claim.

The original narrowband cases keep target 4.2185134083910505 dB-Hz, actual input
C/N0 −3 dB-Hz, Rate 0.01, GPSDO-XO / independent / margin 3, synthetic key/seed,
three oscillator/clock pairs, and a fixed 2,216,646,656-byte allowance matching the
recorded 50% baseline. This is not a fresh dynamic percentage-memory resolution.
Clock half-width is 0.0003 ppm; frequency half-widths are 1.5e−12 / 1.5e−10 /
4.5e−7 Hz for carriers 0.005 / 0.5 / 1500 Hz respectively.

| Original carrier / rate choice | Input Fs | Chip samples / duration | Symbol samples / duration | Captured media | Actual top backend |
| --- | ---: | ---: | ---: | ---: | --- |
| 0.005 Hz / application | 64 | 12,800 / 200 s | 409,600 / 6400 s | 30,000 s | clock_correlator |
| 0.5 Hz / application | 64 | 12,800 / 200 s | 409,600 / 6400 s | 30,000 s | clock_correlator |
| 0.005 Hz / fixed | 6000 | 1,200,000 / 200 s | 38,400,000 / 6400 s | 2000 s, partial | clock_correlator |
| 0.5 Hz / fixed | 6000 | 1,200,000 / 200 s | 38,400,000 / 6400 s | 2000 s, partial | clock_correlator |
| 1500 Hz / application = fixed | 6000 | 1,200,000 / 200 s | 38,400,000 / 6400 s | 2000 s, partial | clock_correlator |

The actual original symbol contains 32 inner chips, correcting the preliminary
unverified 12,800-second/64-chip assumption. Application-rate low-carrier captures
complete a bit plus fully observed absence, publishing at media 8000 seconds.
The 6000/s captures include the full 1600-second pulse prefix and chip boundaries,
but end before the first complete symbol; they establish neither acquisition
latency nor physical completion. Full acquisition plus absence would exceed the
probe's 256 MiB retained-PCM cap. The high-carrier application rate already equals
the fixed rate, so its exact result is reused, not measured twice.

Compact inner raw/pulse/moments selection is unobserved by this harness; only the
public top backend above is recorded. Peak retained storage is 382,382 bytes for
the complete low-rate cases and 381,152 bytes for the partial fixed-rate cases.
Corresponding process RSS is about 19.7–20.3 MB and 58.5–58.8 MB, including stored
PCM. The ordinary controls retain 471,282/471,282/887,026 bytes. Per-run throughput,
constructor/push/poll costs and all exact geometry remain in the JSON evidence.

Two co-signed D1000 boundary controls use actual clock ±0.00060000000009 ppm,
additive offset ±0.0003 Hz (combined bound ±0.0003045 Hz) and timing bias ±0.5 ms.
The true origin is ±20 samples from the mapped center inside the 20.0142-sample
half-width. Exact initial start/end and admission threshold agree; accepted bits
and completion counts agree. Earlier publication deliberately changes event timing
and its hash. Single-pair speedups are 5.56× and 5.37×; every joint corner was not
measured. Final5 D10/D100 repeated width curves and broader RF/Sub-9kHz long cases
remain outstanding; the source3 D10/D100 table above is historical evidence.

## Whole rolling Live observation

A full 60-second hardware-audio-stub capture with the fixed 2,216,646,656-byte
workspace matching the recorded 50% baseline admits 73 receivers, retires 47, and peaks at 33 resident instances.
This uses the requested GPS 1 ms, region 1 ms, Audio error 50 ms settings,
with synthetic capture error 0.1 ms and rate uncertainty 0.1 ppm. The policy's
calculated total arrival allowance is about 205 ms before outward rate/numerical
inflation (both stations' GPS and audio allowances are included). The Live result
records qualified timing active and fallback zero; the exact per-bank inflated
arrival map is not exported in that snapshot. The five-phase/12.8-second symbol
geometry and full oscillator lanes remain in effect.
Historical source3 legacy CPU 21.711 s / wall 24.854 s; peak accounted storage 733,930,216 bytes.
The historical snapshot V2 CPU was 20.661 s / wall 23.877 s. The final5 V2 replay
uses 21.365 s CPU / 24.483 s wall, with 752,267,248 bytes peak accounted storage
and 759,214,080 bytes peak RSS. This is one functional run, not a repeated timing
interval; pacing/snapshot overhead is included. One short archive-identity read
ran concurrently, so these times are functional diagnostics, not an isolated ratio.
Both recover the exact bit, publish at media 13.9 s and finish only after absence
at 26.3 s; no health failure, qualified timing active, no timing fallback. V2 uses
different waveform PCM, so these two times are **not a receiver speedup ratio**.
The preceding legacy receiver reaches its workspace admission limit at media
25.16 s, before its first FFT, with 1,366,456,352 bytes already retained. This is
a configured admission failure, not evidence of physical host OOM. There is no
valid completed baseline Live CPU or latency ratio. V2 peak retained storage is
about 18.3 MB above the historical legacy Live fixture; this includes the outer
map/state change across resident banks and is not a same-PCM receiver-only RAM
comparison. These are stub captures, not a live hardware timing guarantee.

## Envelope, clipping and power

Finite shaped waveform experiments, three symbols / 38.4 seconds at 40 ksample/s:

| DSSS | Guarded OOB, legacy | V2 | Legacy clipped samples | V2 | Mean-power change |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | −29.385 dBc | −48.222 dBc | 5.112% | 0 observed | −5.813 dB |
| 100 | −29.065 dBc | −48.225 dBc | 5.270% | 0 observed | −5.798 dB |
| 1000 | −30.145 dBc | −48.218 dBc | 3.826% | 0 observed | −5.844 dB |

D1000 equal-coefficient-power runs fall from 200 ms to 0.8 ms; the longest 40-ms
interval above twice mean power falls from 0.60 s to none in this capture. Whole-bit
power variation and coefficient histograms remain: this is no claim of statistical
indistinguishability. Guarded OOB excludes 200 Hz adjacent power; V2 unguarded OOB
is about −44.4 dBc. These are captured-waveform measurements, not a regulatory mask
or worst-case clipping guarantee. Permutation alone still clipped 2.394% of samples;
backoff alone removed observed clipping while leaving prolonged plateaus.

Digital backoff costs actual transmit power at unchanged downstream gain. Equal-RMS
restoration requires 5.844 dB gain in D1000 and reaches peak 1.418 > unity; hardware
may not supply it without renewed clipping. At equal real peak the measured
average-power deficit is 3.103 dB. These power costs are not detector sensitivity
loss. TX V2 retains an extra 555,720 bytes in this fixture. No added audio buffer
or filter delay is introduced by permutation, but map preparation costs CPU.
Worst-case next-symbol map preparation and hardware underrun margins remain
unmeasured; the absence of extra filtering delay does not qualify hardware timing.

## Fallback and remaining work

Selection is automatic. The direct/partitioned path currently covers generated
coherent templates without sample-fit, drift-section or differential-window
requirements. Timing must qualify; interval/range capacity, peak/candidate
separation and scratch bounds must be provable. Failure preserves the original
coverage and backend. If any phase group cannot use the bounded path, the cohort
retains its shared full FFT. Unsplittable components retain the full admitted-union
schedule. These unsupported geometries remain unfinished optimization, not claims
that broad work is always faster.

The direct crossover probe measured complete cold kernel work at this D1000
geometry: 32 retained starts took median 0.07055 s direct versus 0.08719 s full FFT;
128 took 0.11082 versus 0.08640 s. The production cap of 32 is conservative for
that probe. Its control included an input FFT in both variants, whereas final
all-direct acquisition omits it. Partition tile choice uses a numerical operation
model with a 10% margin; it is not a measured universal crossover. Final geometry
controls and whole-receiver timings have a separate scope.

Inner private-template generation is still full-symbol work. The new paired path
shares pulse evaluation and outer rotation across the independent 0/1 candidates;
it does not replace every fine-chip contraction with an inner-chip statistic.
Across rolling banks, equal canonical templates could be shared more widely, but
PCM geometry, thresholds, track state and fine-chip phase remain distinct. The
existing two V2 symbol maps are retained across phase changes; an additional
multi-megabyte paired cache has not been added without measured benefit. Coherent
outer guarding still reconstructs local-phase statistics from raw PCM in each
bank, because the shared-input interface lacks the needed phase/Gram contract.
These are remaining total-CPU costs, not reductions hidden in planner coefficients.

## Sensitivity and limits

Four fresh holdouts contain 20,000 paired V1/V2 physical white-noise draws each
(80,000 total), one synthetic key/waveform seed and five retained starts. This is
DSSS1000, 40 ksample/s, one matched epoch/frequency/rate/phase branch, outer guard
disabled and first acquisition only. Each finite transmitter waveform is scaled
to equal actual payload/tail energy before the paired channel/noise comparison;
V1 and V2 PCM differ. This measures waveform-design contrast, not same-PCM
receiver optimization loss, unchanged-output-level link budget or hardware gain.

The fixed grid is −5…25 dB-Hz in 0.05 dB increments. All draws are retained;
no sampled-grid nonmonotone or unbracketed crossing was observed. Four-case/90%/99%
family-corrected 95% analysis combines 10,000 paired quantile bootstraps, exact
order-statistic enclosures, finite-operator geometry/grid allowances and a
Clopper–Pearson allowance for unseen exceptional crossings. The wider conservative
enclosures below govern the conclusion, not the narrower approximate bootstrap.

| Conditional case | V1/V2 C/N0 at 90% | Additional V2 dB, conservative enclosure | V1/V2 C/N0 at 99% | Additional V2 dB, conservative enclosure |
| --- | ---: | ---: | ---: | ---: |
| Nominal bit 0 | 9.25 / 9.25 | 0.00 [−0.15, +0.10] | 9.90 / 9.85 | −0.05 [−0.20, +0.15] |
| Nominal bit 1 | 9.25 / 9.25 | 0.00 [−0.15, +0.05] | 9.90 / 9.85 | −0.05 [−0.20, +0.15] |
| Bit 0, +100 ppm / +0.05 Hz / diffusion 0.1 deg/√s, partial chip | 9.25 / 9.25 | 0.00 [−0.15, +0.05] | 9.90 / 9.85 | −0.05 [−0.20, +0.10] |
| Bit 1, −100 ppm / −0.05 Hz / diffusion 0.1 deg/√s, partial chip | 9.25 / 9.25 | 0.00 [−0.10, +0.10] | 9.90 / 9.85 | −0.05 [−0.20, +0.15] |

The clock/frequency stresses are matched-branch tests, not proof that those offsets
belong to the GPSDO acquisition bank. The partial symbol adds four samples to the
512,000-sample bit; actual starts are fractional. The threshold is fixed at
47.332366 under the nominal per-bank 1e−10 parameter. No actual rare false-accept
rate is inferred from that parameter or these trials. Conditional wrong accepted
bits are zero across the sampled grid; all correct/wrong counts remain in the
saved curve CSVs. At 9.25 dB-Hz, observed correct detection is 90.125–90.710% V1
and 90.715–91.210% V2; at 9.90 it is 99.135–99.235% and 99.190–99.340%.

**The 0.1 dB limit is not established.** Conservative 99% enclosures reach +0.15 dB;
values rounding to +0.10 are at the limit, not evidence of strictly lower loss.
The finite-operator bound does not include arbitrary FFT floating arithmetic or
float PCM casting. A separate 32-row actual float-PCM replay checks selected
holdout draws at 90%/99% grid points: automatic decisions and first/end positions
match the conditional model, maximum score discrepancy is 3.60e−7, alternative
score discrepancy 7.59e−8, and stored thresholds agree exactly. An unsigned
negation error in the first diagnostic score conversion was corrected; those
superseded replay files remain, and the Monte Carlo probability algebra was
unaffected. These selected replays do not turn 80,000 conditional draws into
80,000 full production acquisitions. Continuous
C/N0 monotonicity, active guard, full acquisition/rolling bank, multiple keys,
wrong outer keys, interference, hardware and cumulative raw-reference sensitivity
remain unqualified. Same-PCM retained-score regression is necessary but does not
alone establish that missing full-receiver sensitivity scope.
Published-tail finite-EOF threshold equivalence remains a separate limitation.
A larger trial threshold makes the matched gate stricter but also changes the
activation of the DSSS colored-excess guard, so it is not a proof of set inclusion
for the complete detector. Guard activation boundaries need their own validation.
No extremely rare false-accept rate is inferred from small noise-only trial counts.
The original likelihood/finite-noise statistics and trial penalties remain in use.

The fixed-template reference statistic `−(N−1) log(1−f)` has an exponential
null distribution for N independent isotropic complex Gaussian observations and
the stated fitted-energy fraction. A union bound does not require independent
search windows, but does require valid individual statistics and charging every
selectable alternative. Retaining those formulas and acquisition charges is useful
statistic evidence, not certification of the actual adaptive receiver. Projected
quadrature covariance/effective counts, chain selection and colored/wrong-outer
interference require separate validation. Each rolling key/epoch bank has its own
local false-alarm parameter; it is not an application-lifetime allocation across
all banks or resets. No application-global rare-event rate is claimed here.

## Manual launch and validation

The rebuilt local GUI is copied to `bounded-acquisition-manual-gui`; the script
below supplies the latest Rate10/DSSS1000 reproduction. It accepts further flags,
including the user's selected keyfile without inspecting its contents:

```sh
./.agent-work/artifacts/receiver-opt-20261008/manual-bounded-acquisition.sh --keyfile=/tmp/demoKey --key-name=Default --tx-key named
```

For the faster SSB-width starting point:

```sh
./.agent-work/artifacts/receiver-opt-20261008/manual-bounded-acquisition.sh --rate 360 --carrier 1001500 --dsss-factor 10 --keyfile=/tmp/demoKey --key-name=Default --tx-key named
```

Use `--dsss-version legacy` for wire compatibility with the preceding GUI.
V2 and legacy peers are not interchangeable. In the Rate10/DSSS1000 configuration,
one raw `0` bit takes 12.8 seconds plus separately observed absence; `quick brown`
is 70 transmitted bits, so its physical payload takes almost 15 minutes.
Numerical RX probability is intentionally withheld for V2; this is distinct from
its CPU and timing geometry diagnostics. Final candidate `tight-candidate-final-5/manifest.json` SHA256:
`56d2e5107f510346e597d8422118e3b151211fdba601d656d9319a602b215ed8`.
GUI SHA256: `343c94b155ffcaa83d6208285ecd232352caad9ebf6c6bd3620c6d504bde8b3b`.
Library SHA256: `83a2a828bf9b74d0db8635e83babf7742ba9663586dfe7b201286df07c382291`.
Exact source archive: `df5894db77fd0edb00bf001e898f0755fd998881f22d1bbd720af1bf56947aba`.
Release / GCC 14.2 / FLTK / sanitizers off, stable agent build tree, two build/test
workers. The manifest inventories each source file and exact CMake cache.
The preceding optimized baseline remains runnable and unchanged; the raw
correlator diagnostic remains available for subsequent cumulative comparisons.

Completed affected checks: crypto, PatternCode, FFT batch, correlator batch,
PatternReceiver, streaming modem, tuning, simulation estimates and Live profiles.
The original failures and their reruns remain in the logs: the new encrypted
profile fixture needed a synthetic loaded key; the cache-pressure fixture moved
from 320 to 328 KiB to accommodate bounded control metadata while retaining every
cache/bit assertion; constructor numerical setup is now priced consistently with
identical search operations. Full PatternReceiver and Live profiles reruns passed
in 61.86 and 143.11 seconds. The full GUI group passes 38/38 in 150.91 seconds after the numeric timing-advice
fixture explicitly selected legacy DSSS; V2 probability withdrawal has separate
planner assertions. The application rebuild with `./build.sh` and the complete CLI test pass (CLI 32.62 seconds); the manual launcher `--help` also passes.
Broad contract/calibration, sanitizer, native platform/SDK/packaging, hardware
loopback/timing/spectrum and full acquisition threshold curves remain required
before overall qualification. No pending, skipped or omitted check is a pass.

## Evidence

- [Final measurement report](../receiver-tight-envelope-measure-20261009/FINAL5-MEASUREMENT-REPORT.md),
  SHA256 `bf39f366bcb041c522f7cfef2ec9933ae60f52bd5c067c03a80afcf9a9a1107e`.
- [Final regression results and geometry](../receiver-tight-envelope-measure-20261009/final5-regression-summary.json),
  SHA256 `95fde104a182e478fbc5c6affe996d7edabfcc045d635a8e2d2a1f448fd4e613`.

- `../receiver-tight-envelope-measure-20261009/final5-primary-summary.json`
  and `final5-primary-pairs.json` for final-candidate endpoint executions.
- `../receiver-tight-envelope-measure-20261009/paired3-width-matrix-summary.json`
  and its exact raw PCM/result/harness manifests.
- `../outer-envelope-spectrum-20261009/REPORT.md` and `results.json`.
- `tight-envelope-baseline-9ee268e/manifest.json`; `tight-candidate-paired-3/manifest.json`.
- Maintained `docs/search-compute.md`, `docs/clock-sync.md`,
  `docs/simulation-estimates.md`, `docs/spread-spectrum-controls.md`.

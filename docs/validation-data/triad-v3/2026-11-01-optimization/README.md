# Three-carrier GPU optimization — 1 November 2026

The isolated study now has optional cached preparation, fused phase-state updates,
CUDA graph replay, persistent receivers and an FP64 register-reduction path.
The original strict FP32 and FP64 receivers remain available and remain the
reference defaults. The changes preserve the waveform and do not integrate into
the ordinary Robust receiver.

The final equal-work comparison improves the processing pipeline **4.62×** and
the complete subprocess job **3.82×**, including startup and output. The
campaign demonstrates substantial **conditional inference acceleration**.
It does **not** establish 5× more operationally qualified receptions per second.
At the sampled HF-hour operating point of −18 dB-Hz, the default gate recovered
277/300 captures; the relaxed gate recovered 295/300. Neither experiment
qualifies the full operational false-admission requirement.

See [hardware and complete-stage measurements](hardware.md),
[reproduction commands](reproduce.md), [precision allocation](precision-stages.md),
and the [pre-optimization protocol](../../../triad-gpu-optimization.md).
The maintained [raw evidence archive](evidence.tar.gz), [source snapshot](source-overlay.tar.gz)
and [fixed inputs](fixed-inputs.tar.gz) include [per-file evidence hashes](evidence-files.sha256.json).

The [v3 contract](../../../triad-v3.md) continues to govern encryption,
clock-derived keystream, interleaving, channels, oscillators and continuous state.

## Targets and evidence boundary

| Target | Result and gap |
| --- | --- |
| 99% correct; any noise admission ≤10^-4 per 24 hours | Actual sampled reception was 277/300 at −18 dB-Hz. Complete operational admission is unqualified. |
| Relaxed 90% correct; any noise admission ≤1% per 12 hours | Actual sampled reception was 295/300 at the same point. Its reception component is supported for this profile; admission remains unqualified. |
| 5× useful complete-pipeline throughput | Same-B32 conditional pipeline improved 5.62×. Best-configuration and equal-work results are separately reported in hardware.md. No operationally qualified throughput is established. |
| 0.1, 0.25, 0.5 and 1 dB allowable loss | Curves and paired numerical screens are available; every budget remains statistically unresolved. These are comparison budgets, not default-policy changes. |
| Cost per result meeting both gates | Undefined: there are no operationally qualified results in this campaign. Conditional costs are labeled separately. |

Count each scored H0 or H1 observation once; a paired noise trial produces two
observations. A capture is not a candidate-block update, and a hypothesis is not
an independently validated reception. H0/H1 pairs and reused noise across SNR
points are not independent trials. Timing repetitions do not enlarge reception
sample sizes.

The study distinguishes sampled known-channel discrimination, matched
unknown-phase/channel reception, exhaustive finite-bank acquisition and full
operational admission. The raw precision probe uses FP64 arithmetic on exported
complex64 waveform samples; it is not an independently generated FP64 waveform.
The C++ sampled physical path and GPU projected-statistic path are separate
implementations, with their different assumptions recorded.

The finite-state likelihood integrates normalized nuisance priors. Under its
known white circular-Gaussian null, the ideal likelihood-ratio identity supports
a family bound. Integrated nuisance states are not separate maximum tests.
Searched/selected timing, CFO, epoch, channel, duration and method alternatives
and repeated attempts still require their full declared allocation. Signal-model
mismatch can lose sensitivity without violating a correctly specified null;
quantization, interference and estimated covariance can change the null itself.

## Conditional throughput and bottlenecks

The initial workload is the complete 929-block, 3,714.015625-second exported
hour design, two two-tap bit candidates, P512/F43/Q24 and strict FP32 bulk math.
Both H0 and H1 are scored; the finite search/state support is preserved.
The benchmark phase diffusion is 0.00872665 rad/√s, whereas the physical holdout
uses 0.0153. Their sensitivity results are not interchangeable.

Three repeated, paired timing-protocol-v2 runs gave:

| Batch | Reference H0+H1 pipeline | Graph pipeline | Reference / graph inference only |
| ---: | ---: | ---: | ---: |
| 1 | 2.6755 s | 0.9506 s | 2.1634 / 0.2018 s |
| 8 | 4.6680 s | 1.5477 s | 4.1753 / 0.7908 s |
| 32 | 32.3057 s | 5.7481 s | 31.7706 / 4.8753 s |

Thus the same-B32 result is 5.62×, but the best initial reference batch already
processed about 3.43 observations/s. The graph B32 result is 11.13/s, about
3.25× that best baseline. Cached preparation raises the B32 result to 11.29/s.
Do not select an unnecessarily slow baseline to claim the broader objective.

Persistent campaigns reuse one receiver/design across independent noise batches.
The original eight-batch screen measured 4.122 observations/s for reference B4
and 15.961/s for graph B12. This 3.87× rate ratio compares jobs containing 64
and 192 observations respectively. Its measured variable-stage ratio is 4.13×;
it excludes initial setup. Subsequent equal-work and combined-reduction results
are in [hardware.md](hardware.md), rather than multiplying separate gains.

Cached Gaussian observation preparation produced bit-identical H0/H1 blocks in
4,645 paired block checks. Median preparation fell from 0.32935 to 0.07931 s;
including one 0.0899-second setup over five uses gives 3.21×. Its 572,264-byte
cache holds the fixed design and simulator truth, not receiver-visible true
channel state or cached noise. Nonlinear sampled frontends reject this shortcut.

A separate Gaussian-state covariance cache prepares eigendecompositions,
Cholesky factors and log determinants in CPU FP64. On its synthetic 929-block
B32 workload, resident gains were 3.78×/4.06× in FP32/FP64, but full preparation,
transfer and verification gains were 1.64×/1.73×. The warm CPU single-use version
was about 0.95×: caching is not automatically beneficial without reuse.

### Measured traces versus inferred bottlenecks

| Trace | Updates | CUDA kernels | Summed kernel time | First-to-last kernel span | Gaps |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 16 | 1,749 | 30.199 ms | 40.945 ms | 10.746 ms |
| Fused | 16 | 699 | 5.886 ms | 32.166 ms | 26.281 ms |
| Graph | 32 | 1,467 | 12.852 ms | 14.280 ms | 1.428 ms |

The original trace spends 12.467 ms in roll, 7.969 ms in add and 3.184 ms in
gather. Fusion removes array rearrangement; graph replay removes much of the
remaining launch gap. These traces sum CUDA kernel events only, not nested CPU
operator annotations. Normalize by update count before comparison.

The graph trace identified an FP64 conversion/copy costing 1.348 ms over 32
updates. The optional two-level Triton reducer keeps **both accumulation levels
FP64** while avoiding that full intermediate. Its paired full-hour update gain
was approximately 1.15× in both execution orders, with maximum score difference
4.3e-14 and no observed decision changes. Logical intermediate write/read
avoidance at B8 is estimated as 135,266,304 bytes per update; this is not a DRAM
counter measurement.

Nsight Compute was installed, but permission/virtualization restrictions
prevented useful counters; detailed device-specific probe results are archived.
Nsight Systems was unavailable. SM/tensor utilization, actual DRAM/cache
traffic, occupancy, register pressure and achieved FLOP/s remain unavailable.
The measured array work and launch gaps support the selected optimizations;
cache/bandwidth saturation remains an inference, not an established resource
ceiling. Low VRAM allocation is not a bandwidth measurement.

## Independent sampled HF-hour holdout

There are 300 fresh physical paths, balanced 150/150 bits, indices 2000–2299,
with independently seeded waveform/key, channel, oscillator and noise state.
Each contains 237,697 samples at 64 Hz, including complete tails. Seed and input
hash checks found no overlap with the recorded earlier campaign seeds.

| Gate | Actual sampled correct | Wrong | Missed |
| --- | ---: | ---: | ---: |
| Default, 17.1758859451 nats | 277/300 (92.33%) | 0 | 23 |
| Relaxed, 11.8775685786 nats | 295/300 (98.33%) | 0 | 5 |

For independent bounded trials without assuming identical success probabilities,
the one-sided 95% Hoeffding lower bounds are 0.8527 and 0.9127. Conditional
binomial Clopper–Pearson lower bounds are 0.8931 and 0.9653; their stronger
interpretation requires the binomial sampling assumption. Neither bound makes
this profile representative of every physical realization.

The reported **0/300 null admissions are projected-Gaussian nulls**, not actual
sampled signal-absent captures or operational day trials. Their binomial
one-sided 95% upper bound is approximately 0.994% per attempt, far above the
required operational tail. The first 20 paths were also scored on all three
GPUs and the original receiver, with no decision differences; those duplicates
are numerical checks and are not extra independent trials.

Actual source generation took 464.872 s with four CPU workers. Actual-observation
scoring took 28.508 s. If generation stays unchanged, eliminating scoring would
improve this serial source-inclusive validation workflow by only 1.061×. This
is not a ceiling on live reception or a speedup against the original receiver.
Generation combines waveform/crypto, channel, oscillator, RNG, projection and
I/O; those native sub-stages were not separately instrumented. It includes
separate reference/noisy passes, so removing repeated work is a concrete next
CPU optimization, not a claimed measured improvement.

## Precision and sensitivity curves

Strict FP32 versus FP64 on the same full-hour observations differed by at most
about 1.52e-5 nats in the numerical screen. Graph versus original FP64 differed
by about 7.11e-15. Bulk graph FP64 took approximately 0.704 s for paired B1
inference versus 0.202 s for strict FP32. Small score differences are evidence
for retaining these same-model implementations, not proof of tail equivalence.

The fixed-window sampled-waveform precision holdout used 1,024 independent noise
realizations per SNR, reused across modes and SNR points. For the default gate,
strict FP64 correct counts at −1, 0, .25, .5, .75, 1, 1.25, 1.5 and 2 dB-Hz were
621, 856, 902, 936, 964, 987, 1003, 1009 and 1018. The descriptive interpolated
99% crossing is approximately 1.76 dB-Hz. The relaxed 90% crossing is about
−0.895 dB-Hz. These are point estimates, not paired crossing confidence bounds.

The [default reception curve](curves/5090-precision-holdout-default-44ceb607f0fc-precision.svg),
[throughput–sensitivity comparison](curves/5090-precision-holdout-default-44ceb607f0fc-throughput-sensitivity.svg),
[cost–sensitivity comparison](curves/5090-precision-holdout-default-44ceb607f0fc-cost-sensitivity.svg)
and [machine-readable summary](curves/summary.json) show
pointwise confidence intervals, GPU inference throughput and conditional cost
against sensitivity loss. CPU FP64 remains the numerical reference and is not
priced at the GPU rate. The 0.1/0.25/0.5/1 dB budgets remain unresolved; zero
estimated crossing difference does not certify any of them.

FP16/BF16 storage, lower-precision product attempts, real decomposition and TF32
requests are explicit experiments. Padded E4M3 FP8 and INT8 library operations
executed on all three devices. E5M2×E5M2 was rejected by the library; FP4 and
block-scaled modes were not exercised. No SASS/tensor-instruction verification
was obtained. FP8/INT8 observation representation changed rankings and some
hypothetical gates even when arithmetic on represented inputs agreed closely.
No low-precision mode is promoted to the default receiver.

P128/F13 grid screening recovered 16/32 at −18 dB-Hz, compared with 30/32 for
P256/F23 and P512/F43. Around this boundary P128 loses roughly 2 dB and provides
little complete-pipeline gain because CPU preparation dominates. The changing
frequency grid means this is not a pure phase-grid experiment. The 32 reused
paths became development data; any selected model requires fresh validation.

## Sampled converters, interference and long state

The optional frontend quantizes DAC samples before propagation. Receiver
interference, analog filtering, noise, gain, ADC quantization and overload
recovery are processed on actual sampled observations in their documented
order. Nominal bits, ENOB noise, full scale, gain, thermal/added dither and
clipping are separate variables. Default-off is an exact bypass.

| Hour diagnostic | Projection error energy / expected thermal energy | Consequence in this realization |
| --- | ---: | --- |
| ADC16, full scale 512 | 1.016e-8 | No clipping; close baseline scores |
| ADC16, full scale 65536, ENOB12 | 0.0430 | Measurable additional impairment |
| Same, ENOB8 | 10.99 | Inflated likelihood scores; not a sensitivity gain |
| ADC16, full scale 32 | 0.281 | 226,354 clipped components; reception lost |
| Occupied tone + filtering/recovery | 3.054 | 31,471 clips; Gaussian null assumptions fail |
| DAC16 ENOB12 + ADC16 | 1.45e-8 | 109 DAC clips; quantization plus rare overload |

A sampled **signal-absent interference** case produced hypothetical bit scores
50.380 and 23.472 nats, above the 17.176 Gaussian gate. Admission is correctly
disabled for active frontends; this is a decisive null-model failure, not a
numerical precision failure.

Two genuine two-day captures preserve continuous channel/oscillator/frontend
state: 11,064,065 samples and 172,876.015625 seconds each, including tails.
ADC16 full scale 1024 had zero observed clipping and paired projection error
4.03e-8 of expected thermal energy. This is one realization, not a sensitivity
qualification. The baseline timing also included simulated H0/H1 whereas the ADC
run scored one actual observation; their wall times are not a speedup ratio.

FHSS shaping and channel/filter tails remain continuous. The current study does
not implement physical RF retuning: receiver-coordinate occupied tones do not
establish hop collision protection, retuning accuracy, adjacent-channel blocker
rejection or immunity to front-end overload.

### Limited ADC admission theorem

For fixed rank-one unit templates and known white Gaussian analog noise,
unclipped component rounding with step Δ obeys `||e|| ≤ Δ sqrt(N/2)` for N
complex samples. Adding that amplitude margin to the analog gate gives an
ideal-arithmetic finite-family bound. Admission rejects any observable rail
code, rather than consulting privileged simulator clipping flags.

The independent-noise holdout found no rounding-bound or gate-implication
violations. At full scale 512 the default deterministic gate inflation was
0.116–0.146 dB; **gate inflation is not measured sensitivity loss**. At full
scale 65536 the conservative bound rejected every signal; full scale 32 also
rejected every capture for rail codes. The theorem excludes interference,
estimated covariance, AGC, dither and general phase-frequency reception, and
has no floating-point roundoff certificate. Its NumPy ties-to-even quantizer is
an independent model, not a bit-exact replica of the C++ ties-away converter.

## Acquisition, failures and remaining limits

The acquisition comparison scores all 288 hypotheses: 9 CFOs × 2 templates ×
16 lags. Cached direct GEMM beats FFT for this small bank, while source/RNG,
preparation and independent CPU FP64 verification dominate the complete job.
No weak coarse detector or candidate pruning was introduced. This experiment
measures correct bit selection, not correct synchronization. It does not cover
broad epoch/clock ambiguity, all channel alternatives or an hour/day attempt
schedule. Some totals conservatively charge shared FFT preparation even to GEMM.

Preserved negative evidence includes incomplete early warmup/timing attribution,
small-limit B32 preflight failures, the original eight-noise-batch guard,
unprofitable single-use covariance caching, P128 sensitivity loss, aggressive
quantization ranking/gate changes, unavailable counters and the nonlinear-null
failure. Only corrected timing-protocol-v2 records enter throughput aggregates.

The isolated native triad group passed 10/10 release checks and 10/10 ASAN/UBSAN
checks with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer cannot run under the
available ptrace environment; leak coverage is not claimed. Focused Python
checks and native GPU reducer checks are archived. No unrelated ordinary
Robust regression campaign or release certification was performed.

The baseline revision is `75f3af291100d513a8bc20bff9f26d259b51ed71`.
Results record scorer-source and input hashes, including retained intermediate
snapshots. Historical exported inputs lack a verified exporter-source/binary
binding; hashing the current exporter does not establish that provenance. Fresh
physical campaigns separately bind their exporter source, binary and inputs. The old archived final-source list was stale for
`triad-long.cpp`; the contemporary current-source list matched the starting
checkout. Historical files were not rewritten to conceal that discrepancy.

## Ranked next experiments

1. Compare oracle sampled channel/oscillator discrimination with the receiver on
   identical boundary observations, including the 23 default misses. Separate
   energy limits from discarded information/model mismatch; validate any policy
   chosen from these diagnostics on fresh physical paths.
2. Attribute native source-generation time to crypto/waveform, channel,
   oscillator, RNG, projection and I/O; remove duplicated work before spending
   another campaign on the smaller scoring fraction of physical validation.
3. Establish a physical sampled null model for interference and independently
   estimated covariance, with complete operational family accounting. Neither
   higher precision nor FHSS alone fixes front-end overload.
4. Freeze a promising precision/model policy and run paired boundary campaigns
   with crossing intervals capable of resolving the four sensitivity budgets.
5. Extend only the important unsupported boundaries: ill-conditioned channel
   fits and rank decisions, broad timing/CFO/clock/epoch search, RF retuning and
   hop collisions, gain recovery, erasures and rate/bandwidth changes. Revisit
   overlap-save, search partitioning and device placement on those measured
   shapes rather than on advertised low-precision peaks.

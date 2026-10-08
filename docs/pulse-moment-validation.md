# High-chip pulse moments: implementation and qualification

This change extends the iterative receiver beyond its former 4,096 nominal
samples per chip limit. It was developed against `d4de80cf2a0247c03b41efc797a4250319cfb10b`.
The [implementation reference](search-compute.md#long-symbol-pulse-projection)
describes eligibility, workspace fallbacks and unchanged admission semantics.
Extended qualification is paused for manual GUI testing at the user's request.
All changes remain local. The measurements below do not establish completion
of the full regression or sensitivity requirements.

## Reproduction geometry

The supplied GUI command omits its selected key. Experiments use deterministic
synthetic 32-byte keys and epoch 1,800,000,000; no user's key is read. The
application policy is GPSDO-XO (`0.0001 ppm`, `0.5 degrees/sqrt(second)`), Shift
zero, independent references, margin three, Rate `0.01 Hz` and target C/N0
`4.2185134083910505 dB-Hz`. The configured transmit/path/noise values imply
input C/N0 `-3 dB-Hz`. The benchmark explicitly supplies the key and policy.

| Carrier | Selected sample rate | Samples/chip | Samples/symbol | Symbol time | Search bank |
| --- | ---: | ---: | ---: | ---: | --- |
| 0.005 Hz | 64 Hz | 12,800 | 409,600 | 6,400 s | 3 paired frequency/clock cells, 2 timing origins per cell |
| 0.5 Hz | 64 Hz | 12,800 | 409,600 | 6,400 s | same counts |
| 1,500 Hz | 6,000 Hz | 1,200,000 | 38,400,000 | 6,400 s | same counts |

Thirteen epochs give 78 timing/frequency/clock hypotheses per key. The final
implementation allocates 78 pulse lattices for this clipped timing region;
the redundant unused half-chip parity lattice is omitted without removing a
hypothesis. There is one private phase group, one coherent section and no local
differential detector for these 32-chip symbols. The original receiver chooses
raw correlation; the optimized receiver chooses pulse moments.

The 50% workspace resolver depends on available host memory. Initial geometry
captures resolved respectively to **1,968,955,392**, **1,968,023,552** and
**1,967,357,952 bytes**. Paired timing uses the last value explicitly for both
variants, so changing available memory cannot change detector coverage. It is
an allocation ceiling, not actual retained memory.

## Measurement method

`benchmark_pulse_receiver` constructs equal-budget receivers with identical
search settings, seeds, synthetic keys and immutable float PCM. Its diagnostic
`raw_reference` constructor option changes only projection selection. It is not
a persisted setting and adds no GUI/CLI parameter. Runs alternate order and
are pinned to one CPU with no concurrent heavy workload. PCM generation is
outside receiver timing; construction, pushes and polling are inside. Optional
component timers run separately from primary timings. Kernel preparation is a
subset of frontend cost, not a third additive total.

The tool records all resolved geometry, active detector branches, exact
workspace, CPU/wall time, frontend/search time, throughput, first-bit latency,
initial-search coverage, per-push/progress delay and memory. The DSP figure is
the sum of receiver peak workspace plus the shared cache. Process RSS includes
the immutable capture and is a process-lifetime high-water mark. Initial-search
coverage can require a second symbol for a negative origin whose first symbol
was truncated; it is distinct from receiving the first correct bit.

Application-selected sample rates and fixed-6,000-Hz comparisons are separate.
Carriers are never substituted within a sensitivity pair. Ordinary FFT-path
regression timings use the same baseline-public-API application harness against
the original and final static libraries, with a single saved PCM file and
identical resolved backend/search settings.

Build the maintained measurement tools after the application:

```sh
./build.sh --cli --build-dir build/measurement
cmake --build build/measurement --target benchmark_pulse_receiver benchmark_pulse_statistics
taskset -c 2 build/measurement/benchmark_pulse_receiver --carrier 1500 \
  --epochs 13 --seconds 20 --repeats 5 --workspace-bytes 1967357952 --csv paired.csv
python3 tools/analyze_pulse_receiver.py paired.csv --output paired.json
```

## Sensitivity and numerical equivalence

The optimized representation retains every input sample's two quadratures,
energy and observation count, plus the full template covariance. Each finite
pulse-table atom is affine inside a segment, so its dot product uses a zeroth
and first complex sample moment. The Gram is an exact-arithmetic polynomial
and geometric-moment sum. There is no filter transition, truncated spectral
tail, image rejection assumption, resampling, signal delay or replacement
noise dimension. Finite support endpoints remain literal singleton samples.
Amplitude limiting is present in actual transmitted fixture waveforms.

`benchmark_pulse_statistics` measures the complete single-coherent-branch
admission decision for both independent private bits. Raw and optimized
effective basis vectors, their small differences, the actual limited impaired
signal and optional interference occupy one joint orthonormal span. Shared
Gaussian coordinates give correlated template dots; orthogonal chi-square
energy supplies the rest of the same white-noise PCM observation. Energy is
not drawn independently of the projections. Selected draws are reconstructed
as float PCM and replayed through both complete receiver implementations.

For each noise draw the two coherent admission inequalities are quadratics
in positive signal amplitude. Every root-separated interval and internal root
is checked before estimating a monotone detection crossing. Unbracketed,
nonmonotone or numerically inconclusive draws are retained and fail the
experiment. Threshold and competing-bit decisions, alternative scores and
sampled endpoints are checked in the actual PCM replays.

The analysis reports the difference between the raw and optimized 90th/99th
percentiles of required C/N0. It resamples paired noise IDs together and reports
absolute-threshold uncertainty separately from paired-loss uncertainty. It
adds both root-bracketing errors and conservative floating-point allowances.
Those allowances use IEEE rounding estimates and observed replay agreement;
they are not a formal interval proof of every platform's `libm`. Conditional
fixed-waveform statistics are explicitly separate from full acquisition-bank
PCM tests. A zero observed disagreement count alone is not sensitivity proof.

For noise-only false accepts, a two-real-basis projection has a beta-distributed
explained-energy fraction under white Gaussian noise. The tool bounds the
ideal real-valued AWGN fit using the largest eigenvalue of the actual basis covariance
whitened by its implemented Gram, then applies the unchanged threshold and a
union bound over both private bits. Quantized PCM and finite accumulation are
checked separately by paired score and numerical-error tests; the ideal bound
is not a formal float-implementation tail bound. This single-branch bound does
not claim a rare-event rate from a small noise trial count or guarantee rejection
of arbitrary structured interference. Multi-hypothesis trial spending remains
the original receiver's responsibility.

## Coverage and remaining qualification

At the manual-testing checkpoint, local candidate `5a37b78` passed the complete
focused correlator, pulse Gram, cache and planner set (4/4), followed by the
added high-chip section/differential fixture and updated planner/cache checks.
The FLTK GUI application built successfully. Full general/native, sanitizer,
platform, SDK and packaging qualification has not run for this candidate.

Five conditional cases each completed 20,000 paired Gaussian draws and sixteen
paired actual-PCM replays. They covered both bits at 0.005 Hz/64 samples/s with
12,800-sample chips, bit zero at 0.5 Hz with the same geometry, bit one at 0.5 Hz
with a 0.503-Hz interferer, and bit zero at 1,500 Hz/6,000 samples/s with
4,097-sample chips and a 200-ppm clock offset. All included 0.5-degree/sqrt(second)
phase diffusion. Estimated additional C/N0 at both 90% and 99% detection was
zero at the 1e-7-dB root resolution. Paired bootstrap intervals were zero at
that resolution; adding both conservative numerical allowances gave upper
limits from **0.000350 to 0.000746 dB**. These are conditional single-branch
results, not complete acquisition-bank or full-reproduction qualification.
The longer 1,500-Hz statistic experiment was interrupted at the user's request
and is not counted as a pass.

Five isolated 20-second, 1,500-Hz reproduction pairs (13 epochs, 78 hypotheses)
measured median CPU/wall times of approximately **2.010/2.010 seconds raw** and
**0.04566/0.04567 seconds optimized**, with paired speedups **42.4–45.6x**.
This capture is a fraction of the 6,400-second symbol and establishes no
acquisition latency. Three paired 6,408-second captures at each low carrier
measured median wall times **11.814/4.371 seconds** at 0.005 Hz and
**11.774/4.538 seconds** at 0.5 Hz (raw/optimized). They accepted the first bit;
the complete initial bank coverage remained unfinished for negative origins.
Full performance, near-threshold acquisition-bank PCM curves, ordinary FFT-path
comparisons and final latency/memory reporting remain outstanding.

Focused checks compare all 153 pulse Gram pairs, including long chips, partial
cells, DC/carrier-square aliases, fractional clocks, nonunit carrier norms and
closed endpoints. Receiver checks compare complete scores, thresholds, private
identities and next-poll bit publication. Cache checks cover exact original
oscillator prefixes, multiple keys/epochs, bounded fallback, cancellation and
fresh PCM lifetimes. Broader contract, native GUI, sanitizer, platform and SDK
results, sensitivity intervals and final execution tables will be recorded here
and in the [validation record](validation.md) after they complete.

Remaining architectural limits are explicit: the raw PCM frontend still scales
with sample rate; distinct residual carrier banks still require mixing; clock
and timing lattices require their own bounded contractions. Live cache sharing
is opportunistic and whole-file transfer receivers currently retain individual
frontends. Unsupported geometry and tight budgets preserve the validated raw
fallback. No framing, transmitted bits or physical-absence deadline changes.


## Local follow-up: partial symbols and the displayed CPU jump

The later manual command uses Rate 0.1 Hz, carrier 0.05 Hz and target −38 dB-Hz.
Its powers imply input C/N0 **+0.0205999133 dB-Hz**, a 38.0206 dB design margin.
Signal power alone does not change the receiver's fixed search geometry. At
64 samples/s it resolves to 1,280 samples/chip, 25,478,859 samples/symbol
(**398,107.171875 seconds**), four drift sections and 1,244 complete local
20,480-sample windows plus a 1,739-sample tail. The final chip has 459 samples.
There are three frequency/clock pairs and two possible private phase groups.
The timestamp-zero GUI simulation includes 160 seconds of pulse-padding epoch
coverage: 173 epochs and 1,557 timing/frequency/clock hypotheses for one key.
Measurements fix the workspace ceiling at **1,999,661,056 bytes**. A thirteen-epoch
fixture retains 117 hypotheses; it must not be confused with the full GUI bank.

The old receiver fell back to full-rate shaped fitting because the symbol was
not a multiple of four complete chips. The new affine-span backend handles
this geometry with shared I/Q moments and a small per-frequency carrier-moment
table. Private coefficients remain fresh for every bit position. It adds no
filter, decimation, waveform change or physical delay. The whole-chip numerical
kernel cache also now reuses the valid polynomial interval for large chips.

At target −37, the symbol rounds to 16,384 complete chips. The previous planner
charged every fractional-clock cell as a full kernel rebuild, creating an
18.5-fold estimated jump despite the actual receiver being faster. A complete
16,384-cell kernel replay at C=1,280, origin zero and ±0.0003 ppm required only
one or two preparations. This is a geometry replay, not full-symbol receiver
execution. The central planner now estimates reuse and exposes the all-rebuild
allowance separately. Its coefficients and cache-density formula remain
engineering assumptions; these AMD Ryzen 5 PRO 5650U measurements do not
calibrate the named Intel reference processor.

Three isolated 640-second, thirteen-epoch pairs measured median raw/optimized
wall times **3.18786 / 1.98725 seconds**, CPU **3.18744 / 1.98706 seconds**,
with paired wall speedups **1.603–1.643×**. Retained DSP workspace was
**392,376 / 705,872 bytes** including the optional shared cache. Relative to the
previous automatic path's 653,040-byte footprint, the increase is **52,832
bytes** across thirteen epochs. Process peak RSS was 11,546,624 bytes, including
capture and process allocations. Earlier before/after absolute times were
substantially affected by host speed variation, so they are not used to claim a
speedup; the figures above compare paired algorithms within the same run.
These captures are a fraction of the 4.6-day symbol: no acquisition or accepted
bit latency is inferred from them. Exact endpoint/poll/absence behavior is
checked separately with complete-symbol paired fixtures.

### Conditional partial-span sensitivity

Five new cases each completed 20,000 paired Gaussian draws and 32 actual-PCM
raw/optimized replays, using the same waveform within each pair. At 0.05 Hz,
64 samples/s and C=1,280, they cover tails of 1, 459 (both bits) and 1,279
samples; the last includes a 200-ppm clock offset and 0.052-Hz interference.
A fifth case uses 1,500 Hz, 6,000 samples/s, C=4,097, a 27-sample tail,
−200 ppm and a 0.03125-Hz frequency offset. Every case includes 0.5-degree/
sqrt(second) phase diffusion and the actual finite, limited transmitted pulse.

| Case | 90% required C/N0 | 99% required C/N0 | Conservative upper additional C/N0 |
| --- | ---: | ---: | ---: |
| 0.05 Hz, tail 1, bit 0 | −12.49437 dB-Hz | −11.45945 dB-Hz | 0.000237 dB |
| 0.05 Hz, tail 459, bit 0 | −12.64933 | −11.61118 | 0.000211 dB |
| 0.05 Hz, tail 459, bit 1 | −12.01840 | −11.00730 | 0.000205 dB |
| 0.05 Hz, tail 1279, interference | −13.42712 | −12.18102 | 0.001536 dB |
| 1,500 Hz, tail 27 | 2.61182 | 3.63482 | 0.000444 dB |

Estimated raw-to-optimized loss and paired bootstrap intervals are zero at the
1e-7-dB root resolution. The upper column adds conservative numerical allowances
to simultaneous 95% family intervals from 1,000 paired bootstrap draws. The
worst **0.001536 dB** allowance is well below 0.1 dB. This is conditional
single-coherent-branch evidence, not calibration of the 4.6-day multi-branch
acquisition bank. Bounds for ideal AWGN false acceptance remain about
5.0e-11 for the two-private-bit single-branch trial; they are analytical
covariance bounds, not a rare-event estimate from a small noise sample.
Arbitrary interference and platform `libm` behavior are outside that bound.

The local probability planner now handles partial/quarter-crossing windows with
joint real covariance, and retries aligned noncircular fits from pristine
parameters. It retains the full noise energy and all eligible detector branches.
For the supplied strong-signal first bit, 4,096 trials returned 100% with a
95% Wilson interval of **99.9063–100%**. This interval covers sampling only.
The final keyed probe took 6.50 seconds initially and 0.38 seconds on a cached
repeat, with process RSS about 12.55 MB. The aligned −37 geometry also returned
a supported first-bit estimate after the real-covariance fallback; initial
calculation took 6.17 seconds, and cached repeats about 1.20 seconds. Template
quadrature is still regenerated even when Monte Carlo draws are cached. The
central receiver-work ratios are 0.06425 and 0.01620 seconds per audio-second
at −38 and −37 respectively; the latter's all-rebuild fallback allowance would
raise its total to about 0.72469. Those reference-model totals are estimates,
not measurements of this machine or the partial captures above. The quadrature, midpoint carrier,
and compact multi-bit timing limitations in
[the estimate reference](simulation-estimates.md) still apply. A first-bit
estimate does not manufacture a supported whole-message probability.


### GUI bank and fixed-sample-rate execution

The exact GUI epoch range was replayed with `--epochs 173 --epoch-before 166`:
166 earlier epochs, the source epoch and six later ones, retaining all 1,557
hypotheses. Three isolated 160-second pairs measured median CPU/wall times of
**10.04663 / 10.04780 seconds raw** and **6.29938 / 6.29985 seconds affine**;
paired wall speedups were **1.516–1.595×** (median 1.578×). Retained DSP storage
was **5,220,536 / 6,167,632 bytes**, and process peak RSS was 15,953,920 bytes.
This is about 15.9 versus 25.4 audio-seconds per wall-second. The largest push
in the median run fell from about 2.095 to 1.291 seconds with the benchmark's
2,048-sample polling blocks. No bit completed in this partial capture, so that
push time is not an acquisition or accepted-bit latency.

Separate instrumented runs recorded raw frontend/search wall cost
**0.0639 / 10.1920 seconds**, versus affine **0.0561 / 6.3653 seconds**.
Construction and bookkeeping account for the rest; instrumentation adds cost
and is not substituted for the primary paired timing.

Holding sample rate at **6,000 Hz**, Rate at 0.1 Hz, target at −38 and thirteen
epochs gives C=120,000, N=2,388,643,024 and T=398,107.1706667 seconds.
Three identical-PCM 20-second pairs at each carrier measured:

| Carrier | Raw median CPU / wall | Affine median CPU / wall | Paired median speedup (range) |
| --- | ---: | ---: | ---: |
| 0.05 Hz | 6.62030 / 6.62097 s | 0.73015 / 0.73026 s | 8.929× (8.535–9.121×) |
| 1,500 Hz | 6.69966 / 6.70041 s | 0.72907 / 0.72917 s | 8.828× (7.203–9.472×) |

DSP workspace was **392,376 / 799,472 bytes** and process peak RSS 11,751,424
bytes. Six thousand samples/s is also the selected rate for the 1,500-Hz case;
it is an explicit oversampling control for 0.05 Hz, whose selected rate is 64.
The optimized costs agree across carriers at fixed sample rate. The factor
relative to raw reception differs from the selected-rate low-carrier result,
so sampling changes must not be credited as algorithmic speedups. These remain
partial captures, not measured multi-day acquisition throughput or latency.


### Follow-up validation checkpoint

The application was rebuilt through `./build.sh`. All seven complete affected
CTest suites passed: `pattern_correlator`, `pattern_correlator_batch`,
`pattern_projection_cache`, `simulation_estimate`, `receiver_probability`,
`gui_link_planner` and `link_planner_curves` (91.03 seconds total wall time).
The new conditional production-PCM probability checks retain the full bank's
false-acceptance threshold while replaying the selected fractional timing lane;
they cover both private bits and low/transition/high signal levels. They are
model-error checks with finite-sample tolerances, not calibrated probability
or rare-event claims. Full acquisition-bank probability calibration and
quadrature refinement remain outside this local checkpoint.

A very low-noise finite-tail fixture initially admitted an extra bit in both
raw and affine receivers. Moderate noise retained the real pulse tail while
making the strict next-poll/full-absence fixture deterministic. The original
raw tail admission behavior is unchanged; no completion assertion was weakened.
The initial full-bank sampled model prototype was interrupted for excessive
focused-test cost and is not counted as a pass; selected-lane replay retains
the full bank trial penalty explicitly.

The current changes remain local and are ready for manual testing. Full
native/general, sanitizer, SDK, platform and packaging qualification remains
paused at the user's request; these focused results do not declare the original
end-to-end qualification objective complete. No settings or transmitted bits
were added or changed.


## RF and Sub-9kHz manual-feedback follow-up

This local follow-up starts from `f3cf818575fd34b6a047f445e265fbd288411854`.
It keeps full qualification paused for manual testing. It adds no persisted
setting. The rate dropdown now includes 0.001 Hz and 10 Hz; the shared lower
bound, CLI, launch-command and parameter-list round trips accept 0.001 Hz.
Rate remains nominal waveform bandwidth, not payload bits per second.

### Resolved configurations and the missing estimate

Both supplied commands were resolved with deterministic synthetic keys. The
30 MHz command has an on-air carrier of 30,001,500 Hz and a 30,000,000 Hz Shift,
so its real sampled carrier is 1,500 Hz. It is not a 5.8 GHz simulation. Both
oscillator profiles are GPSDO-OCXO, independent references, search margin three.
The Sub-9kHz case has no RF shift and therefore no RF-oscillator contribution.

| Quantity | 30 MHz command | 8.2 kHz command |
| --- | ---: | ---: |
| Input C/N0 | −19.9794000867 dB-Hz | −36 dB-Hz |
| Target C/N0; margin | −24; 4.0205999133 dB | −49; 13 dB |
| Rate; selected sample rate | 1 Hz; 6,000 samples/s | 0.01 Hz; 32,800 samples/s |
| Samples/chip | 12,000 | 6,560,000 |
| Samples/symbol | 98,304,000 | 164,389,412,630 |
| Seconds/symbol | 16,384 (4.55 hours) | 5,011,872.33628 (58.01 days) |
| Frequency/clock pairs | 1,181 | 101 |
| Carrier half-width | 0.00900045 Hz | 0.00000246 Hz |
| Clock half-width | 0.0003 ppm | 0.0003 ppm |
| GUI epoch range | 22 earlier + source + 6 later | 1,606 earlier + source + 6 later |
| Complete timed-bank hypothesis count | 530,845 | 325,826 |
| Private phase groups; coherent sections | 1; 4 | 2; 4 |
| Local differential windows | none | 1,566 complete 3,200-second windows plus tail |
| Selected optimized backend | `pulse_segments` | `pulse_segments` |

The earlier 50% workspace snapshot resolved to **1,999,661,056 bytes**. Every
raw/optimized timing pair below uses that exact common ceiling. Percentage
resolution depends on available host memory; it is not a permanent 2 GB setting.
Later 50% compact-constructor snapshots resolved to **2,235,377,664 bytes**
(RF) and **2,234,329,088 bytes** (Sub-9kHz), selecting the same backends and
hypothesis counts. Their aggregate constructor reservations were 376,289,384
and 965,425,664 bytes, before the optional 262,104-byte allocated cache arena.
The geometry-only tool was corrected to use the same compact flags as the
timed path; earlier default-constructor snapshots are not those bank estimates.

Sub-9kHz probability remains unavailable because the real-covariance probability
model supports at most **17 carrier candidates**, whereas this search has 101.
The UI now names that limit and explicitly says receiver coverage is unchanged.
It does not substitute a selected-lane probability for full-bank correct-bit
probability: a wrong-bit carrier can win the competition. The 13 dB arithmetic
margin does not resolve that modeling gap. Whole-symbol phase loss in this
configured stochastic model is only 0.0275827 dB; phase loss is not the failing
eligibility gate here. These modeled assumptions are not hardware measurements.

### Implementation and CPU cliffs

Aligned shaped symbols that cannot afford whole-chip kernel state now use the
same exact affine-span path as partial symbols, subject to its existing
1,024-sample/chip and 16-second eligibility gates. Affordable whole-chip pulse
projection still wins. No filtering, decimation, I/Q removal, narrower search,
private-pattern reuse or changed threshold is involved. This preserves finite
pulse tails, limited transmitted PCM and sample-domain completion endpoints.

A four-entry immutable carrier-moment palette occupies **416 bytes/frequency**
including tags, instead of 3,168 bytes for the previous compact 33-entry table.
Nonresident lengths use the same bounded exact helper on the stack. Live cache
admission uses finite-push/drain growth bounds and linear aggregate accounting,
replacing a blanket 2 MiB reservation per receiver. The shared PCM cache remains
256 KiB, and unsupported budgets retain the raw fallback.

The planner now accounts for these actual paths and palette misses. At the
fixed workspace above, the keyed RF central CPU/audio estimates change from
413.17 to **267.15** at target −24, and 1,233.52 to **562.67** at −25.
Sub-9kHz target −50 now fits affine storage, changing its model from 5,339.45 to
**1,368.36**; targets −49 and −48 give **1,088.22** and **872.73**. These are
reference-model estimates, not measurements or an encryption-only multiplier.
No coefficient was fitted to the following timings. Remaining steps include
real changes in chip count, carrier count and eligible detector branches;
plots have not been cosmetically smoothed across them.

### Actual paired execution

Release/GCC on the same Ryzen 5 PRO 5650U host, pinned to CPU 2, no concurrent
agent builds/tests. Each pair uses identical float PCM, key, seed and impairments;
order alternates and all hypotheses remain present. Three pairs per case use
application-selected sample rates, held fixed within each pair. Construction,
receiver pushes and polling are timed; waveform generation is excluded.
These very short captures exercise the complete bank without pretending to
receive hours or days of waveform.

| Case and observed PCM | Raw median CPU / wall | Affine median CPU / wall | Paired wall speedup median (range) |
| --- | ---: | ---: | ---: |
| RF, 96 samples / 0.016 s | 7.85082 / 7.85257 s | 4.15325 / 4.15391 s | **1.901×** (1.874–1.959×) |
| Sub-9kHz, 66 samples / 0.00201220 s | 14.94131 / 14.94296 s | 2.87882 / 2.87916 s | **5.200×** (4.854–5.276×) |

Median construction wall times are 0.06361/0.14120 seconds (RF raw/affine) and
0.22797/0.54671 seconds (Sub-9kHz). Total throughput is 12.23/23.11 samples/s
and 4.42/22.92 samples/s respectively. Accounted peak DSP workspace is
**340,807,624 / 376,551,488 bytes** and **814,242,488 / 965,687,768 bytes**.
The increases relative to raw are 35.7 MB and 151.4 MB; they are not free RAM.
The reduced palette saves storage compared with the previous affine table,
which is a different comparison from raw. Process lifetime peak RSS reached
303,415,296 bytes and 879,857,664 bytes, including fixture/process allocations.

Separate instrumented pairs give frontend/search wall times:
RF **0.02159 / 8.46686 s raw**, **0.04263 / 3.87720 s affine**;
Sub-9kHz **0.07231 / 15.03110 s raw**, **0.07855 / 2.25289 s affine**.
Private fitting remains the dominant work in these captures. Construction and
bookkeeping explain the remainder; instrumented totals do not replace the
primary measurements. Median maximum push times fell from 7.78891 to 3.99005 s
and 14.71497 to 2.33242 s. **No bit completed**, so acquisition and accepted-bit
progress latency are unmeasured here, not zero. These banks remain far from
real time on this host. Extrapolating these prefixes to full-symbol CPU or
acquisition time is not a measured result.

The cache still exhausts on long receiver-major pushes, and affine templates
are still contracted at oscillator-block boundaries. This follow-up does not
make all search cost independent of sample rate. Earlier fixed-6,000-Hz
comparisons above remain separate from sampling-policy gains.

### Ordinary-path regression timings

A standalone application harness was compiled against the original `d4de80c`
static library and the final library. It lets `StreamingReceiver` select its
normal backend. Three alternating pairs per case replay the exact same saved
120,000-sample PCM capture (20 seconds at 6,000 samples/s), with one warmup per
process, one worker, a 64 MiB workspace and the original oscillator policy.
All three cases selected FFT and retained three frequency/clock pairs.

| Case (bandwidth; target C/N0) | C; N | Original / final median CPU | Original / final median wall | Retained workspace, both |
| --- | ---: | ---: | ---: | ---: |
| Ordinary fast (1,200 Hz; 60 dB-Hz) | 10; 640 | 0.184868 / 0.184835 s | 0.184890 / 0.184835 s | 434,536 bytes |
| Wider band (100 Hz; 20 dB-Hz) | 120; 7,680 | 0.026028 / 0.026247 s | 0.026029 / 0.026269 s | 434,530 bytes |
| Weaker signal (100 Hz; 10 dB-Hz) | 120; 61,440 | 0.014298 / 0.014420 s | 0.014362 / 0.014420 s | 850,248 bytes |

Median wall changes are below 1%; the paired original/final ratios span
0.995–1.413, 0.988–1.047 and 0.996–1.001 respectively. The first fast baseline
pair was slower; all pairs remain included and this is not a speedup claim for
FFT. Input hashes, complete event hashes, bit counts, hypothesis counts,
coverage flags and retained memory match within every pair.

The fast fixture accepts all 16 bits exactly once and physically completes;
its final median acquisition is 1.99 ms of computation / 0.3413 s of sampled
media. Maximum push is 4.57 ms, and measured next-poll progress delay is bounded
between about 2 microseconds and 4.19 ms for the 2,048-sample polling blocks.
The wider case accepts 15 bits without completion (the capture is short), and
the weaker case accepts one bit without completion. They do not supply whole
message latency results. These are bounded regression measurements, not full
platform or performance qualification.

### Aligned-affine sensitivity and uncertainty

Two new cases each use **20,000 paired noise draws**, one for each private bit,
at 6,000 samples/s, 1,500 Hz, C=12,000 and exactly 32 chips (64 seconds).
They include +0.0003 ppm clock error, +0.003 Hz frequency offset, fractional
sample start, 0.0070710678 degrees/sqrt(second) phase diffusion and the finite
limited transmitted waveform. A 65,536-byte workspace automatically selects
the actual `pulse_segments` backend; the tool asserts its backend, spans,
sample counts, complete scores, thresholds, next-poll events and EOF behavior.
Twelve PCM seeds per bit give **48 paired actual-PCM replays across 24 seeds**.
Whole-chip moment and partial-tail control runs also pass.

| Private bit | 90% required C/N0 (absolute interval) | 99% required C/N0 (absolute interval) | Upper additional C/N0 including numerical allowance |
| --- | ---: | ---: | ---: |
| 0 | −2.49642 [−2.53384, −2.46583] dB-Hz | −1.47185 [−1.51479, −1.41371] dB-Hz | **0.001072 dB** |
| 1 | −2.05546 [−2.09079, −2.02399] dB-Hz | −1.03691 [−1.08417, −0.97673] dB-Hz | **0.001011 dB** |

Both bits have zero estimated additional required C/N0 and [0,0] paired
bootstrap intervals at the 1e-7-dB root resolution. Joint analysis uses 1,000
paired bootstrap draws and 98.75% individual intervals for **95% family
coverage over all four 90%/99% comparisons**. The upper column adds both root
errors and conservative IEEE numerical allowances; it is not a claim of exact
zero error or an arbitrary-libm interval proof. Maximum actual-PCM relative
score discrepancy is 6.44e-8. There are no invalid/nonmonotone omitted draws.
The analytical single-branch two-bit AWGN union bound is about 5.0e-11, based
on implemented Gram/covariance eigenvalues, not a rate estimated from 24 seeds.

This is conditional single-coherent-candidate sensitivity evidence. It does
not qualify the 8,192-chip RF symbol, 58-day Sub-9kHz symbol, their competing
carrier/timing/key/epoch banks, or hardware oscillator reliability. Existing
paired interference, wider clocks, partial/tail, multi-key/cache and physical
completion regressions remain intact. Full acquisition-bank sensitivity and
rare-event qualification remain outstanding.

### Navigation, validation and manual handoff

Stronger/Weaker use two checked current-input support previews while the precise
probability worker is pending; the worker retains its full calculation and gap
search. Previous plots remain labeled previous, and Apply remains disabled.
Twenty-four alternating shared-Application clicks with document generation in
the default LPI fixture took a median **0.0726 ms**, maximum **0.1496 ms**;
no worker was started in that timing probe. A separate keyed support probe
across both expensive geometries measured at most **4.04 ms for two targets**.
Neither is a measurement of rendering a previous completed plot on every native
platform. The full GUI planner regression also exercises rapid steps while a
probability job can be in flight and checks current target identity and layout.

The application/GUI build succeeded through `./build.sh`. Complete affected
suites pass: `pattern_correlator`, `pattern_correlator_batch`,
`pattern_projection_cache`, `simulation_estimate`, `receiver_probability`,
`tuning`, `gui_controller`, `gui_application`, `gui_link_planner`,
`link_planner_curves`, `gui_launch_command`, `gui_launch_settings`,
`gui_self_check` and `cli`. The keyed Live acquisition/epoch fixture passes.
The initial CLI run failed outdated below-minimum and 10-Hz duration assumptions;
focused repairs and the full CLI rerun passed. No test assertion or receiver
coverage was dropped.

An aligned test initially used an ascending carrier list, which let both raw
and affine receivers publish a weaker unmatched hypothesis before the central
candidate. All candidate scores agreed; immutable output cannot then change
owners. Retaining every lane but using the normal central-first policy passed
exact `01` and full absence completion. The existing order dependence is
recorded as an acquisition-policy limitation, not hidden as evidence of
full-bank sensitivity equivalence. The test also retains the slowest clock's
complete absent-symbol endpoint.

Local evidence is retained under
`.agent-work/artifacts/receiver-opt-20261008/`: the `rf*-performance.csv` and
`sub9-performance.csv` captures, component captures, joint aligned statistics,
analysis JSON, resolved geometry/probes, test logs and source-release manifest.
The `manual-rf30.sh` and `manual-sub9.sh` launchers use the rebuilt GUI and the
supplied arguments; select a synthetic key in the GUI because the exported
command does not contain it. All changes remain local. Full contract/general,
sanitizer/native-platform, SDK, packaging and full-bank sensitivity work remains
paused for the user's manual test; this is not a completion or release claim.

A GPSDO label alone cannot promise useful coherence at 0.001 Hz or over days.
The configured OCXO diffusion predicts only 0.224 degrees RMS over a 2,000-second
chip; that is a model assumption, not a specification for a physical unit.
[NIST's GPS-disciplined-clock study](https://www.nist.gov/publications/measurement-transient-environmental-effects-gps-disciplined-clocks)
explains the role of the local oscillator, control loop and environmental
transients. EME also requires a Doppler/libration channel, illustrated in this
[ARRL microwave EME report](https://www.arrl.org/files/file/Technology/microwave/Small%20Dishes%20and%20Digital%20EME.pdf).
Its 10 GHz example is not a measured 5.8 GHz channel for this application.
The present AWGN/static-offset/Brownian-phase fixtures establish neither EME
nor practical Sub-9kHz link feasibility.

## Further local candidate: private affine interval reuse

This stage starts from **`6330e94aadce8a5c4588f10218c8984be1097341`**,
including its preceding pulse-moment and affine optimizations. That candidate
was not fully qualified. Before editing, its source, Release configuration,
static library, GUI and measurement executables were frozen under
`.agent-work/artifacts/receiver-opt-20261008/optimized-baseline-6330e94/`.
The copied GUI passed its self-check. New measurements compare with that actual
frozen library, not just a switch in the new executable. The raw diagnostic
path remains available for cumulative comparisons.

The build uses GCC 14.2, Release `-O3 -DNDEBUG -std=c++20 -ffp-contract=off`,
FLTK GUI and CLI, without sanitizers, in
`build/agents/receiver-opt-20261008/native`. The new runtime library SHA-256 is
`fab0d8927d6f60f82844b9ec21b05a5309fb45225db1c1b502e6eacef2a856f1`.
This identifies the measured runtime; the final evidence manifest also records
source, tools and GUI hashes. All edits and artifacts remain local.

### Implementation and measured bottleneck

The original reproduction uses whole-chip `pulse_moments`, which already
accumulates shared public moments across oscillator blocks. In contrast, the
RF and Sub-9kHz banks select `pulse_segments`. Their preceding implementation
reconstructed both private candidates at each 32-sample block boundary, even
inside one linear pulse-table interval. Fine component profiles attributed
57.3% and 59.6% of measured preparation-plus-fitting CPU to preparation.
Those fine timers substantially perturb execution; their totals are not used
as speedup evidence. The planner's conservative 990/810 preparation/fitting
split is an engineering attribution, not a throughput calibration.

The new optional cache stores two complex value/slope pairs and absolute
first/end sample bounds: **80 bytes per lane and private phase group**. Each
entry belongs to exactly one lane/group and its current private bit address.
It is valid only inside the intersection of regular and shifted final-pulse
knots, chip, active quarter/window and symbol boundaries. Translation always
uses the retained anchor, avoiding chained rounding. An interval fast path skips
repeated timing/clock/pulse geometry only when every active group's cached
range encloses the entire original oscillator block. It retains the covariance
fit, sample boundaries and event order.

Completion invalidates and cleanses entries before changing the private bit
address. Deferred quarter-state materialization invalidates all intervals.
Required payload growth or a workspace reduction evicts the optional cache
before displacing required storage. Logical bit retention and all hypotheses
remain unchanged. Eligibility requires affine projection, at least 8,192
samples/chip, clock-scaled knot width greater than the oscillator block, and
room for the complete optional cache. Unaffordable or unsupported cases retain
the preceding arithmetic. No new user setting is introduced.

There is **no new mixer, filter, decimator or internal sample-rate floor**.
ADC validation and public I/Q pulse statistics still operate at the original
input rate; private preparation follows natural waveform boundaries, and fits
still follow the existing 32/128-sample oscillator blocks. Covariance, template
energies, thresholds, finite transmitted tails and amplitude limiting are
retained. There is no filter group delay, flush-generated observation or added
accepted-bit batching. Floating-point coefficient translation is separately
measured below.

### Measurement procedure

Each pair uses one saved original float PCM file and an exact canonical
manifest containing synthetic key scheme, seeds, impairments, all ordered
frequency/clock hypotheses, epochs, timing search, workspace and push size.
A digest/configuration mismatch is rejected before receiver execution. New and
preceding executables run in separate fresh processes, in alternating order,
pinned to CPU 2, with no overlapping build, test or receiver timing workload.
Three paired runs per receiver case report median CPU/wall times and the range
of paired CPU speedups. PCM generation/loading is outside receiver timing;
construction, frontend, private search and progress draining are inside.
Evidence export, EOF diagnostic calls and destruction are outside that timed
scope; this is not whole-process lifetime timing. EOF is independently checked
for newly manufactured bits or completions.
Process RSS includes the loaded capture and transient allocation. Timers for
components are separate runs and are not subtracted from uninstrumented totals.

Fixed-budget comparisons use **1,999,661,056 bytes** for both receivers.
Application-selected input rates are distinguished from the additional fixed
6,000-Hz controls. Live 50% workspace resolutions are recorded separately;
available host memory can change them. Every pair checks identical PCM,
ordered hypothesis bank, detector branches, event identities, evidence
identities and accepted/error/completion counts. A short prefix does not
establish full-reception CPU, acquisition latency or sensitivity.

### Resolved geometries and additional execution performance

On the AMD Ryzen 5 PRO 5650U host, the six 50% workspace resolutions were
1,584,324,608; 1,573,486,592; 1,556,996,096; 1,555,376,128;
1,554,989,056; and 1,540,098,048 bytes, in table order. The Sub-9kHz value
was inspected directly with the same correlator/search constructor: the
measurement tool's conservative 1 MiB-per-bank guard rejected that percentage
budget, while the direct constructor retained all 325,826 hypotheses, two phase
groups, four sections and the same differential window. That guard remains
intact for performance comparisons, which use the fixed budget above.

| Configuration | Input samples/s | Samples/chip (seconds) | Samples/symbol (seconds) | Keys × epochs; hypotheses | Detector; backend |
| --- | ---: | ---: | ---: | --- | --- |
| Primary 0.005 Hz | 64 | 12,800 (200) | 409,600 (6,400) | 1 × 13; 78 | 1 group/1 section; moments |
| Primary 0.5 Hz | 64 | 12,800 (200) | 409,600 (6,400) | 1 × 13; 78 | same |
| Primary 1,500 Hz | 6,000 | 1,200,000 (200) | 38,400,000 (6,400) | 1 × 13; 78 | same |
| Strong 0.05 Hz, BW 0.1 | 64 | 1,280 (20) | 25,478,859 (398,107.171875) | 1 × 173; 1,557 | 2 groups/4 sections/20,480-sample windows; affine, cache ineligible |
| RF 30,001,500 Hz / stream 1,500 Hz, BW 1 | 6,000 | 12,000 (2) | 98,304,000 (16,384) | 1 × 29; 530,845 | 1 group/4 sections/no local window; affine + cache |
| Sub-9kHz 8,200 Hz, BW 0.01 | 32,800 | 6,560,000 (200) | 164,389,412,630 (5,011,872.33628) | 1 × 1,613; 325,826 | 2 groups/4 sections/104,960,000-sample windows; affine + cache |

The primary and strong banks have three paired frequency/clock hypotheses;
RF has 1,181 and Sub-9kHz 101. Epochs before the source are respectively
6, 6, 6, 166, 22 and 1,606. Independent GPSDO-XO models use 0.0001 ppm and
0.5 degrees/sqrt(second); GPSDO-OCXO uses 0.0001 ppm and 0.005 degrees/sqrt(second).
RF combines LF/RF phase diffusion to 0.0070710678 degrees/sqrt(second).
Search margin is three, start uncertainty seven seconds. Input C/N0 is −3,
−3, −3, +0.0205999133, −19.9794000867 and −36 dB-Hz. Targets remain the supplied
4.2185134084, 4.2185134084, 4.2185134084, −38, −24 and −49 dB-Hz.

All sample-domain stages retain the listed input rate. The cache introduces
no lower-rate PCM stream. Nominal natural knot spacing is chip duration/256,
with extra shifted-tail, clock, quarter/window and symbol boundaries. This
reduces private preparation frequency; covariance fitting still follows the
existing oscillator blocks and therefore still has a sample-rate-dependent cost.
The original explicit paired pulse path no longer has a 4,096-chip-sample
ceiling; unsupported legacy unpaired geometries retain their existing fallback.

| Case / observed input | Preceding CPU / wall seconds | New CPU / wall seconds | Median paired CPU speedup [range] |
| --- | ---: | ---: | ---: |
| Primary 0.005 Hz, 200 s | 0.099085 / 0.099095 | 0.092585 / 0.092609 | 1.069× [1.068, 1.095] |
| Primary 0.5 Hz, 200 s | 0.097975 / 0.098004 | 0.091972 / 0.091988 | 1.065× [1.064, 1.065] |
| Primary 1,500 Hz, 200 s | 0.298823 / 0.298843 | 0.293907 / 0.293998 | 1.014× [1.010, 1.026] |
| Strong 0.05 Hz, 160 s | 6.581432 / 6.582000 | 6.568934 / 6.569938 | 1.001× [0.995, 1.018] |
| RF, 601 samples / 0.100167 s | 22.426568 / 22.429186 | 16.790488 / 16.793209 | **1.345× [1.315, 1.398]** |
| Sub-9kHz, 1,024 samples / 0.0312195 s | 22.296681 / 22.298703 | 4.800525 / 4.801151 | **4.624× [4.603, 4.645]** |
| Fixed 6,000 Hz, primary 0.005 Hz, 200 s | 0.294065 / 0.294132 | 0.289715 / 0.290014 | 1.015× [1.001, 1.026] |
| Fixed 6,000 Hz, primary 0.5 Hz, 200 s | 0.297154 / 0.297242 | 0.289827 / 0.289908 | 1.023× [1.022, 1.025] |
| Complete affine `001`, 277.282167 s | 34.345655 / 35.826410 | 25.136401 / 25.150958 | **1.430× [1.269, 1.484]** |

These are measured medians, not extrapolated full-reception costs or confidence
intervals for performance. The unchanged whole-chip and cache-ineligible controls
show small binary/host timing variation, not a new algorithmic speedup. The
complete affine control uses 6,000 samples/s, C=12,000, N=384,001, two keys,
three epochs, 36 hypotheses, two phase groups, one section, 0.001-second timing
uncertainty and input C/N0 +2 dB-Hz. Every run accepts exactly `001`, no other-bank
bits, zero errors and one physical completion; all assertions are retained.
It crosses chips, partial tails, many oscillator/pulse boundaries and symbols.
The dedicated receiver regression additionally crosses quarters and local windows.

The long-case prefixes have no accepted bits, acquisition or physical end.
Their zero event latency fields mean **unobserved**, not instant acquisition.
New throughput is 35.79 input samples/CPU second for RF and 213.31 for Sub-9kHz,
only 0.00597 and 0.00650 media seconds/CPU second in these measurements. This
stage does not establish practical real-time operation for either configuration.
Startup matters: the much shorter 97-/66-sample pilots improved only 1.18×/1.47×.
The 1,024-sample Sub-9kHz run still covers a small part of its long natural pulse
interval; its result must not be extrapolated over the 58-day symbol.

### Components, workspace and unchanged controls

Separate coarse instrumentation measured RF frontend/search CPU of
0.28572/22.24254 seconds preceding and 0.30134/17.58101 seconds new. Sub-9kHz
was 0.95171/21.35026 preceding and 0.92382/2.96010 new. Mixing and public affine
prefix moments are fused within frontend cost; there is no filter work.
Covariance/template-energy fitting is in the private fit component. Whole-chip
lattice statistics have their own counter; its zero on the affine path does
not mean that affine public statistics cost nothing.

Input validation was at most 0.00126 CPU seconds in these coarse profiles;
shared-cache setup at most 0.00025, progress polling at most 0.01052. Construction
was 0.26201/0.29823 seconds for RF and 0.88577/0.89297 for Sub-9kHz. Remaining
bookkeeping, allocation and timer overhead are not silently charged to mixing.
Public shared-cache hits/misses are unchanged: RF 3,220/647,511 and Sub-9kHz
185,380/5,027,836. Receiver-major cache exhaustion is still a limitation; this
change shares no private coefficient across keys or epochs.

RF peak accounted DSP storage increases from **376,551,488 to 419,022,336 bytes**
(+42,470,848; 11.28%). Sub-9kHz increases from **965,687,768 to 1,018,000,584 bytes**
(+52,312,816; 5.42%). This includes 80 bytes per cached lane/group plus 112 bytes
of added counters/state per receiver bank. Median process RSS was
303,624,192→345,948,160 and 880,726,016→932,982,784 bytes. Both retain the fixed
workspace ceiling; optional cache eviction is tested. The complete affine
control uses only 393,057→399,489 accounted bytes. Future payload growth can
remove the speedup by evicting the cache without reducing payload capacity.

Five alternating fresh-process application-selected-backend pairs at 6,000 Hz,
using identical existing 120,000-sample PCM, retain FFT selection and complete
clock coverage. Median CPU is 0.195809→0.187057 s for the ordinary fast link,
0.026449→0.025701 s for the wider-band case, and 0.013999→0.014039 s for the weaker
case. Accounted workspace remains 434,536; 434,530; and 850,248 bytes. Event hashes,
errors, counts and completion agree. Fast receives all 16 bits and physical end;
wide/weak receive 15/1 bits in the 20-second capture and do not complete.
The weaker +0.29% median change is within the small-run variation; these are
bounded regression checks, not platform-wide performance guarantees.

### Inclusive totals and bit publication latency

A final instrument-free series also times EOF handling and receiver destruction,
including cleansing the optional private cache. Diagnostic evidence export stays
outside receiver execution. Three additional RF/Sub-9kHz pairs use the identical
saved PCM and input rates above; the full-message cleanup/latency probe is one
additional pair. Timing varied between series, so both series and their scopes
are retained rather than replacing earlier measurements.

| Case | Inclusive CPU / wall seconds, preceding | Inclusive CPU / wall seconds, new | Paired CPU speedup |
| --- | ---: | ---: | ---: |
| RF prefix, three pairs | 24.96736 / 24.97045 | 19.84139 / 19.84416 | median **1.370×**, range 1.201–1.449× |
| Sub-9kHz prefix, three pairs | 19.71888 / 19.72172 | 5.06831 / 5.07031 | median **3.981×**, range 3.891–4.313× |
| Complete affine control, one pair | 34.60574 / 34.61138 | 24.73944 / 24.74181 | **1.399×** |

Medians of paired ratios need not equal ratios of separate medians. Median
cleanup CPU was 0.07087→0.08558 s for RF and 0.10504→0.11783 s for Sub-9kHz;
the small complete-control bank took 15→12 microseconds. The stated inclusive
totals include those costs. No future/full-symbol RF/Sub-9kHz total is inferred.

The full control's first bit appears at media time **64.170667 seconds** in both
versions, with measured host wall time 1.47008→1.12398 seconds. Complete initial
bank coverage occurs at media time **128 seconds**, host wall 11.87320→8.07195 s.
The maximum accepted-bit publication lag after its sampled endpoint is exactly
**1,029 samples / 0.1715 seconds in both**, within the existing 2,048-sample input
polling block. Measured publishing-push wall cost is 0.01671→0.01204 s. Added
media delay is **zero samples**, and every newly accepted bit still appears at
the next poll. EOF produces zero additional bits or completion events.

The legacy aggregate progress field also includes completion-only events, whose
endpoint is the last bit while absence is being observed. Its 64-second value
in this fixture is not bit publication latency. New dedicated
`accepted_bit_progress_latency_*` columns preserve that distinction without
altering or discarding the older measurements.

### Additional and cumulative sensitivity

Four new **20,000-draw triplet** cases share one union noise span and one
original PCM noise realization among raw, preceding and new receivers. Old
paired CSVs were not joined by seed: adding a basis can change the noise-span
orientation. The tool hashes that span and each joint draw. Sixteen selected
noise seeds, at two amplitudes each, also pass actual PCM replay through all
three implementations: **32 triplets / 96 receiver replays**. Maximum relative
PCM score discrepancy is 4.75e-8. All 80,000 triplets are retained; there are
zero unbracketed, nonmonotone or numerically unenclosed cases.

The criterion is correct-private-bit admission by the complete single coherent
branch at its unchanged false-alarm threshold. Principal cases use Fs=6,000,
C=12,000, N=384,000, carrier 1,500 Hz, clock +0.0003 ppm, offset +0.003 Hz and
phase diffusion 0.0070710678 degrees/sqrt(second), for each bit. A third uses
one extra tail sample, −0.0003 ppm, −0.009 Hz offset, 0.5-degree diffusion and
a 1,500.4-Hz interferer of amplitude 0.01 relative to unit Gaussian noise.
Its total fitted offset, including carrier clock scaling, is −0.00900045 Hz.
The fourth uses Fs=64, carrier 0.005 Hz, C=12,800, tail=12,799, N=422,399
(6,599.984375 seconds), clock +0.0003 ppm, 0.5-degree diffusion and bit 1.
Each retains the sampled finite pulse and transmitter amplitude limiting.
There is no added spectral-tail/filter loss: the new implementation removes
no samples or bandwidth. Remaining measured differences are arithmetic.

| Conditional case | 90% C/N0 [absolute diagnostic interval], dB-Hz | 99% C/N0 [absolute diagnostic interval], dB-Hz | Additional loss upper allowance | Cumulative raw loss upper allowance |
| --- | --- | --- | ---: | ---: |
| Aligned RF bit 0 | −2.49919 [−2.53743, −2.45908] | −1.47435 [−1.52527, −1.40733] | 0.001070 dB | 0.000682 dB |
| Aligned RF bit 1 | −2.05172 [−2.08870, −2.00875] | −1.02207 [−1.10862, −0.95970] | 0.000979 dB | 0.000626 dB |
| Tail/interference bit 0 | −2.48594 [−2.51981, −2.45165] | −1.43808 [−1.51309, −1.37961] | 0.001163 dB | 0.000736 dB |
| Long low-carrier bit 1 | −21.89682 [−21.93313, −21.85695] | −20.85306 [−20.92133, −20.77930] | **0.001237 dB** | **0.000771 dB** |

The estimated additional and cumulative differences at both quantiles are
**0.000000 dB**, with [0,0] paired bootstrap intervals at 1e-7-dB root resolution.
This is not a claim of zero physical error. Ten thousand common bootstrap
resamples use Bonferroni allocation across all **16 contrast/quantile
comparisons**, targeting 95% family coverage under the bootstrap approximation.
Each adjusted interval is 99.6875%; about 15.6 resamples populate each endpoint
tail. The table's loss columns include both variants' root errors and conservative
IEEE numerical allowances. Their corresponding inflated difference intervals
are symmetric about zero. Worst additional interval is **±0.001237 dB** and
worst cumulative interval **±0.000771 dB**, well below 0.1 dB **in this conditional
scope**. Absolute intervals are diagnostics, not extra members of the stated
16-contrast family. Numerical allowances are checked estimates, not an
arbitrary-libm interval proof.

The analysis also retains the threshold-region correct-bit detection curves
and Wilson intervals at half-dB points. The AWGN two-private-candidate analytic
union bound is approximately 5.0e-11 per single branch, computed with transformed
covariance/eigenvalue allowances. It is not estimated from the replay count,
and it does not bound interference or a full acquisition bank.

Each case knows its exact frequency/clock hypothesis and one fractional start;
its many noise draws condition on one fixed waveform/phase-diffusion trajectory.
The edge coordinates therefore test arithmetic at those coordinates, not
competition among mismatched grid hypotheses. This scope excludes the original
RF four-section/differential acquisition banks, their key/epoch arbitration,
unconditional phase/start distributions and threshold-region full-bank BER.
Those remain required qualification. Raw/preceding/new full-symbol regressions
separately cover clock extremes, fractional starts, pulse/quarter/window edges,
finite tails, cancellation, workspace eviction, EOF and physical absence.

### Strong-signal selectivity limitation retained

A full 0.005-Hz, 13-epoch control at input C/N0 +10 dB-Hz (stronger than the
supplied −3 dB-Hz reproduction) fails the strict standalone no-other-bank
assertion in the **frozen preceding receiver**. Both implementations receive the
source `01`, zero source-bit errors and one physical completion, but also
produce the same 24 non-source-epoch admissions. Event and evidence identities
match. The assertion was not deleted or relaxed, and this control is not
counted as a full-bank qualification pass.

Source review confirms distinct private epoch addresses; these are not aliases.
The benchmark drains all banks independently, whereas Live arbitrates overlapping
epochs belonging to the same key. This demonstrates an unchanged unarbitrated
bank-selectivity limitation under a strong structured signal. It establishes
neither GUI misdelivery nor a noise-only false-accept rate. Resolving or
qualifying that policy is separate from this arithmetic optimization.

### Manual checkpoint and remaining qualification

The rebuilt local GUI is
`build/agents/receiver-opt-20261008/native/datapump-gui`.
Launchers under `.agent-work/artifacts/receiver-opt-20261008/` are
`manual-interval-reproduction.sh [1500|0.5|0.005]`, `manual-interval-strong.sh`,
`manual-interval-rf30.sh` and `manual-interval-sub9.sh`. Select a key in the GUI;
the exported command does not include it. The preceding GUI remains runnable
under `optimized-baseline-6330e94/bin/datapump-gui` for comparison.

The application build through `./build.sh` and GUI self-check pass. After the
focused receiver, planner and analysis checks, all 14 complete selected affected
CTest cases pass: `pattern_correlator`, `pattern_correlator_batch`,
`pattern_projection_cache`, `simulation_estimate`, `receiver_probability`,
`tuning`, `gui_controller`, `gui_application`, `gui_link_planner`,
`link_planner_curves`, `gui_launch_command`, `gui_launch_settings`,
`gui_self_check` and `cli`. The keyed Live acquisition/epoch fixture passes.
No required assertion or hypothesis was removed. Analysis self-tests, legacy
and triplet PCM pilots, saved-PCM byte/configuration rejection checks, repeated
paired execution, complete-message controls and the conditional statistics above
are separate additional evidence. The unchanged strong-signal no-other-bank
assertion failure is explicitly retained as a limitation.

**Stop here for the requested manual test. This is not full qualification.**
Still required before declaring the overall optimization complete:

- Full contract/general coverage and complete calibration, including the fixed
  per-case seeds and combined statistical gates.
- Sanitizer and native-platform GUI checks, required Release real-time cases,
  SDK/platform builds and packaging checks; exact-source release certification
  if publication is later requested.
- Full acquisition-bank detection/BER curves, phase/start distributions,
  frequency/clock boundary competition, key/epoch arbitration, differential
  branches and applicable false-accept/interference qualification.
- Broader steady-state and long-symbol performance, complete original RF and
  Sub-9kHz reception/latency evidence where feasible, and explicit analytical
  treatment of configurations too long to measure. The provided prefixes do
  not cover every boundary of those geometries.
- Manual GUI progress/navigation checks and any resulting focused repairs.

The Sub-9kHz probability estimate remains outside the existing validated
frequency-bank model; the optimization does not invent a probability from link
margin. Hardware oscillator coherence, EME feasibility and Sub-9kHz propagation
are not established by these software experiments. No push, publication or
hosted CI dispatch was performed.

Final evidence lives in `interval-measurements/`, with source/build identities,
commands, PCM manifests, summaries and logs in the parent artifact directory.
`interval-local-manual-release.json` records the final source, GUI and evidence
hashes. The historical measurement-tool source/profile before the dedicated
latency counters is retained as `interval-benchmark-before-latency-metrics.cpp`
and `interval-performance-profile-before-latency.json`; the modem library is
identical across both measurement series.

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

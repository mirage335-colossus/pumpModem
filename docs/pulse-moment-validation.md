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

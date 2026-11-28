# Computation and simulation speed of the FSK lattice receiver

**Later correction.** For the corrected physical search and the distinction between measured batches and projected full-block costs, see the [search audit](../fsk-search-validity.md). The [receiver review](../fsk-reconciliation.md#what-reducing-the-work-can-buy) gives separate losses for coherent and noncoherent tone shedding and accounts for fixed work. The measurements below retain their original fixture and model.

26 November 2026 · Fable-5.2 pass · GPU measurement on a full RTX PRO 6000 Blackwell, with reconciliation

This note answers three questions for the [FSK specification draft](../../../_appraisal/matrixModem-FSK.md).
Can a direct DFT outperform an FFT? Where do Tensor Cores earn their place in this receiver? How
fast can the waveform be simulated? It reports one bounded GPU run and reconciles it with the
[DFT/FFT literature review](reviews/review-dft-fft-computation.md) and the earlier
[Sol-6.2 measurements](../Sol-6.2/tensor-reception-results.md). The source is
[`lattice_gpu.cu`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/lattice_gpu.cu) and the
raw outputs are in [`fsk-lattice/results/gpu/`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md#gpu-run).

Everything here is a stage measurement on synthetic data. It is not a receiver, a sensitivity
result or a capacity qualification.

## Answer in brief

- **A matrix-product DFT can beat cuFFT, but which is faster depends on the bin count and on the
  batch size.** For a 1,024-sample dwell on the Blackwell at 32,768 dwells per call, the FP16
  matrix-product DFT against batched cuFFT plus the same 4-bit evidence step is 1.27× slower for
  all 704 in-band fine bins, 1.2× faster for the 352 tone positions and 3.3× faster for 128 bins.
  At 4,096 dwells per call the FFT's data fits the GPU's cache and cuFFT is 2.1× faster than the
  matrix product for the 352 tone positions (200 against 97 million dwells per second).
- **No fixed routing table survives these results; the receiver needs a scheduler that works from
  measured costs.** With a search running on the same GPU, every GPU transform route fell 7–13× to
  the same 7–14 million dwells per second: the two streams take turns, and the GPU's time is
  divided almost without loss (the shares sum to 0.95–1.03). A rate measured under that sharing
  says nothing about which route is cheaper. A race that scored the transform's rate picked a
  route that takes twice the GPU time of the cheapest one, and a fixed "CPU FFT, then GPU search"
  rule is 12–100 times slower than a cost-based assignment on weak-signal blocks.
  [Adaptive routing](#adaptive-routing-chunk-size-contention-and-a-race).
- **A running prototype of the cost-based rule kept throughput within 12% of the best fixed
  assignment in five scenarios, and every fixed rule lost by 2 to 73 times in at least one.**
  From a cold start, exploration included, it took 1.04–1.12 times the best fixed rule's time on
  decode-heavy, dwell-heavy and mixed queues and on a GPU slowed fifty-fold, and it beat every
  fixed rule by 6% when a second load shared the GPU.
  [Running test](#a-running-test-of-the-cost-based-rule).
- **Both are far beyond any single-stream need.** Every route processes 24 to 110 million times
  real time: 3.6 hours of signal in 0.1 to 1 ms. The transform does not set the computational
  ceiling. The count of timing, frequency, drift, spread and message hypotheses does.
- **The matrix-product DFT is the best Tensor Core utilization measured in this project so far:**
  69–71% Tensor activity at 290–330 nominal TFLOP/s with FP16 operands. cuFFT runs with no Tensor
  activity at 92% of memory bandwidth, which confirms that the FFT is bandwidth-bound.
- **INT8 operands were slower than FP16 through cuBLAS on this device** (172–224 against 260–330
  nominal TFLOP/s; 36–40% against 55–71% Tensor activity). The review's roofline prediction that
  INT8 would tie cuFFT on the full grid and FP16 would lose by 2–4× did not hold in that form: FP16
  was the faster matrix route, and the full grid went to cuFFT by 1.3× (FP16) to 2× (INT8).
- **Four-bit operands are numerically sufficient where the input is noise-dominated.** The 15-level
  sample code adds −16.3 dB of quantization noise (0.10 dB of sensitivity). A DFT matrix with
  15-level coefficients has a −25.2 dB error floor and one with 8-bit coefficients −50.0 dB, both
  with exact 32-bit accumulation. An FFT cannot be run on 4-bit operands at all; a one-stage matrix
  product can. That is the sense in which the DFT is the natural transform of an INT4-operand
  receiver.
- **The exhaustive message search is not a matrix-product workload.** A complete maximum-likelihood
  decode of a 24-bit block (16.7 million codewords, 24 symbols of 64 tones) ran at 8,400 decodes
  per second as a direct CUDA-core kernel, 5,000/s as an FP16 one-hot matrix product with 55%
  Tensor activity, 3,400/s as INT8, and 2,000/s as a fast Walsh–Hadamard transform. High Tensor
  utilization did not buy more useful work here.
- **Sample-level simulation runs at 7 to 12 million times real time on one GPU**, noise generation
  included. A 24-hour transmission is simulated in about 10 ms and a month in a third of a second.
  Where the front end is linear a cell-domain simulation needs no samples at all.

## The run

| Item | Value |
| --- | --- |
| Device | NVIDIA RTX PRO 6000 Blackwell Server Edition, 97,887 MiB, 188 SMs, MIG disabled (full device) |
| Software | CUDA 12.8 (`nvcc` build 12.8.r12.8), driver 595.112.07, cuBLAS 12.8.4.1, cuFFT, cuRAND |
| Host | AMD EPYC 9355, 128 logical CPUs visible |
| Source SHA-256 | `1816eaba925730950fc58fb514a88cc834d49f9c38e767b894f09599a4872454` (`lattice_gpu.cu`) |
| Binary SHA-256 | `5cad546cda7930749cc0db663d4bc347fcbdb19ec88972b169757a64b3f5b475` |
| Pod | one on-demand pod, 0.135 h, about 0.37 USD; verified absent afterwards; no persistent volume |
| Upload | one source file, under 40 KiB |

Timing is host wall time over at least two seconds of repeated calls with a device synchronization
every fourth call and at the end. Telemetry is `nvidia-smi dmon` GPM metrics over ten-second
sustained runs; the figures quoted are the interior one-second samples.

## Transform stage

One 0.4 s dwell has 1,024 active complex samples at 3,200 Hz. The wanted output is 4-bit evidence,
`min(15, floor(energy / (0.5 × noise mean)))`, for a set of 3.125 Hz bins. A batch is 32,768 dwells
(3.6 hours of signal).

- **fft:** batched 1,024-point `cufftExecC2C` in FP32, then a kernel that takes the energy of the
  wanted bins.
- **dft16 / dft16h:** one `cublasGemmEx` product `C[2·bins × B] = D[2·bins × 2048] · X[2048 × B]`
  on real and imaginary parts, FP16 operands, FP32 or FP16 output, then the same energy kernel.
- **dft8:** the same product with INT8 operands holding the 15-level sample code and 8-bit or
  15-level coefficients, exact INT32 result (`CUBLAS_COMPUTE_32I`, accepted by `cublasGemmEx` on
  this device).

| Bins wanted | fft + evidence | dft16h + evidence | dft8 + evidence | Fastest | dft16h nominal rate |
| ---: | ---: | ---: | ---: | --- | ---: |
| 704 (all in-band fine bins) | 0.496 ms | 0.631 ms | 0.991 ms | fft by 1.27× | 300 TFLOP/s |
| 352 (tone positions) | 0.421 ms | 0.351 ms | 0.549 ms | dft16h by 1.20× | 269 TFLOP/s |
| 128 | 0.384 ms | 0.118 ms | 0.182 ms | dft16h by 3.3× | 290 TFLOP/s |

Per call of 32,768 dwells. In dwells per second and multiples of real time (0.4 s per dwell):

| Route | 704 bins | 352 bins | 128 bins |
| --- | ---: | ---: | ---: |
| fft + evidence | 66.0 M/s (26 M×) | 77.8 M/s (31 M×) | 85.3 M/s (34 M×) |
| dft16h + evidence | 51.9 M/s (21 M×) | 93.2 M/s (37 M×) | 277 M/s (111 M×) |
| dft8 + evidence | 33.1 M/s (13 M×) | 59.7 M/s (24 M×) | 180 M/s (72 M×) |

The transform alone took 0.369–0.370 ms for cuFFT whatever the bin count, as expected. The dft8
route timed the same with 8-bit and 15-level coefficients.

**Accuracy** (error power over signal power, first eight dwells against a host double-precision
DFT):

| Quantity | Result |
| --- | ---: |
| cuFFT FP32 | −134.7 dB |
| FP16 matrix product, FP32 accumulation | −71.7 dB |
| 15-level sample code alone (input quantization, noise-dominated input) | −16.3 dB |
| INT8 product with 8-bit coefficients, against the exact transform of the same coded samples | −50.0 dB |
| INT8 product with 15-level (4-bit) coefficients, same reference | −25.2 dB |

The coefficient floors match the earlier derived values (−49.8 and −25.5 dB,
[practice review](../Sol-6.2/mfsk-alternatives-practice-and-computation.md#quantization)). The
review adds that this error is not white: it concentrates in a few harmonically related bins, with
a worst spur of −25 to −29 dBc for 4-bit coefficients and −52 dBc for 8-bit
([§2.5](reviews/review-dft-fft-computation.md)). In noise-dominated bins a −25 dB floor raises the
noise by 0.014 dB. Next to a strong unexcised component it does not hold, which is why the
[interference note](othr-and-interference-tolerance.md) puts excision before the 4-bit code.

**Telemetry** (sustained, 704 bins):

| Route | Tensor activity | SM utilization | Memory-bandwidth utilization |
| --- | ---: | ---: | ---: |
| fft | 0% | 98% | 92% |
| dft16h | 69–71% (FP16 tensor) | 93% | 44% |
| dft8 | 40% (integer tensor) | 98% | 30% |

## What this settles about DFT versus FFT

1. **For the lattice's own shapes the two are within a factor of two of each other on a GPU,** and
   the crossover is near 40% of the bins. The earlier 4.1–5.1× advantage of cuFFT
   ([projection probe](../../../_appraisal/algorithms-matrixModem/projection-probe/README.md)) came
   from a matrix route that multiplied rows which were 75% zeros and paid NVFP4 packing; the review
   identified that handicap, and this run removes it.
2. **Use the matrix-product DFT when the bin set is shared across a batch and is a minority of the
   bins,** or when the operands are to stay at four bits. Use the FFT when every fine bin is wanted,
   for example a 2× or 4× oversampled frequency search.
3. **On a CPU the FFT wins for the tone grid.** The review measured a 512-point FFTW transform at
   0.81 µs and a direct DFT at 50–120 ns per bin on the 5650U (loaded host, one run), a crossover of
   7–16 bins. One stream with eight timing offsets and fourfold oversampling uses about 2×10⁻⁴ of
   one core. A CPU FFT front end is therefore correct for live reception on every representative
   receiver, and the GPU matrix route is for batch work: simulation, reprocessing and broad
   searches.
4. **Hypothesis-specific bases belong after the transform.** Fractional-bin offsets, spread weights
   and tapers are compact in frequency. A short matrix on complex bins, or on the energies of a
   2×-zero-padded periodogram for any stationary quadratic detector, does the same work with 30–100
   times less arithmetic than a time-domain generalised DFT
   ([review §2.3](reviews/review-dft-fft-computation.md)). No drift basis is needed inside a 160 ms
   cell below about 20 Hz/s.
5. **The INT8 result contradicts the vendor-rate expectation and is worth a follow-up.** cuBLAS
   delivered less from INT8 than from FP16 on this device. Whether a hand-written `mma` kernel, FP8
   or FP4 operands, or a fused energy epilogue changes that is unmeasured. Until it is measured,
   FP16 is the faster matrix route, and no hardware ceiling should be declared for the integer
   path.

## Exhaustive block decode

The specification's short block is decoded by scoring every message. The test case: 24 information
bits, 24 symbols of 64 tones, the tone index of each symbol a keyed linear map of the message, and
a 4-bit table `T_j` of accumulated evidence per symbol. `Score[c] = Σ_j T_j[G_j·c]` for all
16,777,216 messages `c`, then the maximum. Sixteen or sixty-four independent hypotheses (timing,
frequency, drift or spread) are decoded per call.

- **direct:** a CUDA-core kernel, one block per (hypothesis, high 12 bits), that sums the 24
  lookups and keeps the maximum without storing the scores.
- **gemm16 / gemm8:** `Score = P · W` with `P[4096 × 1536]` one-hot in (symbol, tone of the high
  half) and `W[1536 × 4096]` holding `T_j[t xor G_j·c_low]`; FP16 with FP32 result (exact, sums at
  most 360) or INT8 with INT32 result; then column maxima.
- **fwht:** scatter the Walsh spectra of the 24 tables into a 2²⁴ array and run a radix-2 fast
  Walsh–Hadamard transform on CUDA cores.

All four returned the same best score and message for every hypothesis (16 of 16 and 64 of 64).

| Route | Decodes per second | Tensor activity | Memory-bandwidth utilization |
| --- | ---: | ---: | ---: |
| direct (CUDA cores) | 8,400–8,480 | 0% | 0% |
| gemm16 + maxima | 5,030–5,080 (product alone 6,550) | 54–56% | 54–59% |
| gemm8 + maxima | 3,410–3,470 (product alone 4,050–4,090) | 36–38% | 35% |
| fwht (CUDA cores) | 2,000–2,010 | — | — |

- The direct kernel does 4×10⁸ lookups per decode entirely in cache. The one-hot product does 64
  times more arithmetic, 63/64 of it on zeros, and then moves a gigabyte of scores through memory
  to find the maximum.
- The one-hot product is exact with operands of at most four bits (0/1 and 0–15), so it is a
  legitimate INT4-operand formulation. It is the slower one.
- A fused maximum in the product's epilogue, or FP8/FP4 operands, might close the gap; neither is
  available through cuBLAS and neither was tested.
- The fast transform loses to both. In the Walsh–Hadamard domain the "fast transform against dense
  product" question has the opposite answer to the folklore: the dense product is 2.5 times faster
  than the fast transform, and the structured gather is faster still.

**Consequence.** One GPU scores 1.4×10¹¹ codewords per second. A broad search of, say, 369 timing
and frequency hypotheses with a full 24-bit decode each takes 44 ms, against a block that lasts
minutes to days on air. Decode cost does not limit the block size before memory does; the block
size is limited by `2^k` growth (a 28-bit block is sixteen times the work) and by the
[sectioning loss](coding-and-message-structure.md) of splitting a long message into blocks.

## Simulation speed

`lattice_gpu simulate` times a sample-level simulation of 32,768 dwells per call: complex Gaussian
noise for every sample (cuRAND Philox), one keyed tone per dwell with a Rayleigh gain, the
transform, and 4-bit evidence.

| Stage | Time per call | Dwells per second |
| --- | ---: | ---: |
| noise | 0.179 ms | 183 M |
| tone with Rayleigh gain | 0.369 ms | 89 M |
| samples to FP16 / to the 4-bit code | 0.272 / 0.227 ms | 120 M / 145 M |
| dft16h + evidence / fft + evidence | 0.629 / 0.496 ms | 52 M / 66 M |
| **total: noise, tone, fft, evidence** | **1.049 ms** | **31.2 M (12.5 M× real time)** |
| total: noise, tone, FP16, dft16h, evidence | 1.417 ms | 23.1 M (9.3 M×) |
| total: noise, tone, 4-bit code, dft8, evidence | 1.764 ms | 18.6 M (7.4 M×) |

The run's mean evidence level was 1.541, against 1.54 expected for noise alone, as it should be
with one weak tone in 704 bins.

- This is a lower bound on what a tuned simulator would do: the tone kernel is the slowest stage
  and is unoptimized, and no channel variation inside the dwell was simulated.
- It has no decoder, interleaver or channel memory between dwells. Those add little: accumulation
  is 256 additions per dwell.
- Only the sample-level path can qualify the 4-bit sample code, blanking and excision, because
  those are nonlinear before the transform.
- With a linear front end the bin statistics can be drawn directly. The conditions under which that
  is exact are listed in [review §4.3](reviews/review-dft-fft-computation.md): white Gaussian
  noise, an integer number of samples per cell, tones on exact bins, rectangular keying, and a
  channel that is constant over the dwell or whose within-dwell covariance is sampled. The lattice
  is specified to meet them. The local calculation
  [`lattice_receiver.cpp`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md) is a
  cell-domain simulation of this kind, with the within-dwell fading covariance computed exactly.

## Adaptive routing: chunk size, contention and a race

Second run, same device type and software (source SHA-256
`c149b9aedd900dcbd71d08b6d111e1ac12bf9e6cd29371fce94f0e3f58a27fac`, binary
`7608c223fb5c4270879c563f5509388a9ad44ec45b751685e5936345c6b88b87`; one pod, 0.061 h, about
0.16 USD, verified removed; host EPYC 9355 limited to 13.6 CPUs by quota). It asks whether an
automatic scheme that tries each route briefly and then uses the winner would work, and what it
has to measure. Outputs: `chunks.out`, `contend.out` and `identity_run2.out` in
[`fsk-lattice/results/gpu/`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md#gpu-run).

A fourth transform route was added: a **CPU route**, a plain radix-2 FP32 FFT and 4-bit evidence
on 13 host threads with the evidence uploaded to the device. It is not a tuned library, and it
spawns its threads per call, which penalises small batches.

### The fastest route changes with the portion size

352 tone positions, dwells per second:

| Dwells per call | cuFFT | FP16 matrix DFT | INT8 matrix DFT | CPU route |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 12.8 M | 7.2 M | 1.0 M | 0.11 M |
| 512 | 74.0 M | 30.1 M | 7.3 M | 0.46 M |
| 4,096 | **200.6 M** | 96.6 M | 31.6 M | 0.91 M |
| 32,768 | 77.7 M | **94.2 M** | 59.8 M | 1.82 M |

- cuFFT at 4,096 dwells per call works on 32 MB in and 32 MB out, inside the device's 128 MB
  cache; at 32,768 it works on 268 MB each way and is bandwidth-bound. The matrix product is flat
  between the two sizes.
- So the earlier statement "the matrix product is faster for the tone grid" is a statement about
  one batch size. With the best portion for each, cuFFT is 2.1× faster.
- Small portions are costly for every route: at 64 dwells per call each is 6–60 times slower than
  at its best size.
- By proportion, the matrix product would need fewer than roughly 170 bins to beat cuFFT at the
  4,096 size [derived, not measured].

### A search on the same GPU equalises the transform routes

A second host thread ran the block decode continuously on its own stream; the transform ran at
32,768 dwells per call on the first.

| Search load | Transform route | Transform, dwells/s | Decodes/s meanwhile |
| --- | --- | ---: | ---: |
| none | cuFFT / FP16 DFT / INT8 DFT / CPU | 77.7 M / 94.0 M / 59.8 M / 1.82 M | — |
| direct kernel (CUDA cores) | none | — | 8,234 |
| direct kernel | cuFFT | 7.2 M | 7,069 |
| direct kernel | FP16 DFT | 7.4 M | 7,250 |
| direct kernel | INT8 DFT | 7.1 M | 6,930 |
| direct kernel | CPU | 1.82 M | 8,391 |
| FP16 matrix search (Tensor Cores) | none | — | 5,024 |
| matrix search | cuFFT | 13.1 M | 4,249 |
| matrix search | FP16 DFT | 13.7 M | 4,463 |
| matrix search | INT8 DFT | 12.6 M | 4,082 |
| matrix search | CPU | 1.83 M | 5,020 |

- Kernels from the two streams take turns. The transform's rate is then set by how often it gets a
  turn, not by its own speed: three routes that differ by 1.6× alone are within 5% of each other
  under load.
- A transform running flat out on the GPU takes 11–14% of the GPU from the direct search and
  11–19% from the matrix search. That time is divided, not lost: each stage's rate as a fraction
  of its rate alone sums to 0.95–0.96 beside the direct search and 1.01–1.03 beside the matrix
  search ([model output, section 1](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/scheduling_model.md)).
  The CPU route takes nothing from the GPU.
- The test ran the transform with unlimited work. In a receiver the transform has only as many
  dwells as have arrived, so it cannot run ahead of need for long; what is lost by a bad choice is
  the GPU time of a dearer route, and the CPU capacity left unused.
- It does not matter whether the search uses Tensor Cores or CUDA cores. A Tensor Core transform
  does not find idle capacity beside a CUDA-core search; both need the same processors' time.
- The CPU route's 1.8 million dwells per second is 730,000 times real time for one stream. When
  the search is the bottleneck, "CPU FFT, then GPU search" is the right configuration, as
  conjectured. How much it matters depends on the block [derived from the rates above]: a
  400-hypothesis decode costs 49 ms of GPU time per block, and the transform about 11 ns per dwell
  and timing hypothesis. For blocks of a few thousand dwells the transform is under 1% of the GPU
  time; for weak-signal blocks of 10⁵ dwells and more it is comparable or larger, and the per-dwell
  work is then what a backlog consists of.

### A race that scores one stage picks the wrong configuration

The prototype race gave each transform route 0.5 s, then ran the fastest for 4 s, twice per load.

| Search load | Probe, dwells/s (cuFFT / FP16 DFT / INT8 DFT / CPU) | Winner | Decodes/s during the 4 s |
| --- | --- | --- | ---: |
| none | 77.7 M / 93.9 M / 59.8 M / 1.9 M | FP16 DFT | — |
| direct kernel | 7.2 M / 7.5 M / 7.1 M / 1.8 M | FP16 DFT | 7,245 (CPU route: 8,391) |
| matrix search | 13.1 M / 13.7 M / 12.6 M / 1.8 M | FP16 DFT | 4,463 (CPU route: 5,020) |

- The probes are stable: both cycles of each load agree to within 1%, so 0.5 s is long enough on
  this device.
- The race did what it was told and was wrong under load, for three reasons. The rates it compared
  were set by turn-taking, so routes whose GPU cost differs by 1.6× looked equal within 5%. It
  held the portion size at 32,768, where cuFFT is at its worst; cuFFT at 4,096 per call costs half
  the GPU time of the route it chose. And it never offered work to the idle CPU.
- What has to be measured is cost: the device time a portion takes when it has the device to
  itself. Costs add, so they can be compared across routes, sizes and devices; rates under sharing
  cannot.

### Model check of three rules on the measured costs

[`scheduling_model.py`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md) is
arithmetic on the measured rates, with the GPU taken as work-conserving. A workload is a number of
dwell transforms (dwells times nine timing hypotheses) and a number of full decodes. It is not a
running scheduler. Time to clear the workload:

| Workload | Stage race (FP16 DFT at 32,768, CPU idle) | Fixed "CPU FFT, GPU search" | Cost-based (cuFFT at 4,096, CPU takes transforms when free) |
| --- | ---: | ---: | ---: |
| one block at 0 dB-Hz (13,500 transforms, 400 decodes) | 48.7 ms | 48.6 ms | 48.6 ms |
| one block at −10 dB-Hz (1.1 million transforms) | 60.7 ms | 628 ms | 53.8 ms |
| one block at −20 dB-Hz (130 million transforms) | 1.43 s | 71.7 s | 0.69 s |
| 1,000 simulated blocks at −10 dB-Hz | 60.7 s | 10.5 min | 53.8 s |
| Hypothetical GPU 100 times slower, one block at −20 dB-Hz | 2.4 min | 71.7 s | 36.7 s |

- For a short block every rule is the same: the decode is all of the work.
- For weak-signal blocks the per-dwell work dominates. The stage race is 1.1–2.1× slower than the
  cost-based rule, and the fixed "CPU FFT, then GPU search" rule is 12–104× slower, because this
  CPU does 1.8 million dwells per second against the GPU's 200 million.
- On a slow GPU the fixed rule is nearly right and the stage race is the one that loses. Which
  fixed rule is right depends on the machine, which is the argument for measuring.
- On the measured machine the CPU adds under 1% to the transform capacity. Offloading to it frees
  the GPU for the search only when the dwell rate needed is below the CPU's own.

### A running test of the cost-based rule

Third run (source SHA-256 `af215b60df387d7d60506725254c4658d161f441eda6d8db75fa97db92c72a53`,
binary `7443f7f53658430a052f6e23462215cbb05735942b78c40e75865233de8e03d4`; one pod, 0.090 h, about
0.24 USD, verified removed; host EPYC 9335 limited to 13.6 CPUs). Output: `schedule.out`.

`lattice_gpu schedule` clears a queue of blocks. Each block needs transform portions (32,768 dwells)
and then decode portions (16 hypotheses), with at most two blocks open at once. A GPU executor and
a CPU executor (13 threads) each run one portion at a time. The CPU has an FFT route and a decode
route (the direct decode on host threads, verified against the GPU: 16 of 16). The GPU has five
transform routes and three decode routes.

Five fixed rules are compared with the **cost-based rule**: time every portion; keep a running
cost per executor, stage and route; run each route once, then the cheapest, with another route on
every twentieth portion; give a free executor the stage in which its cost relative to the other
executor's is lowest; and leave a portion alone when the other executor would clear all queued
work sooner than this one would finish it. It starts each scenario knowing nothing.

Seconds to clear the queue:

| Rule | decode-heavy | dwell-heavy | mixed | mixed, second GPU load | GPU slowed 50× |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stage-race outcome: GPU FP16 DFT at 32,768 + direct, CPU idle | 3.24 | 0.91 | 2.06 | 5.49 | 8.46 |
| GPU cheapest alone (cuFFT at 4,096 + direct), CPU idle | 3.22 | **0.49** | 1.79 | 11.67 | 7.17 |
| Fixed "CPU FFT, GPU search" | 3.23 | 35.89 | 19.35 | 20.90 | 6.50 |
| GPU cheapest + CPU pulls transforms | 3.12 | 0.53 | 1.87 | 9.01 | 6.83 |
| GPU cheapest + CPU pulls decodes | **2.92** | 0.52 | **1.73** | 11.18 | **1.95** |
| **Cost-based, cold start** | 3.07 | 0.55 | 1.81 | **5.18** | 2.15 |
| Cost-based / best fixed rule | 1.05 | 1.12 | 1.04 | 0.94 | 1.10 |
| Worst fixed rule / best fixed rule | 1.11 | 73 | 11 | 3.8 | 4.3 |

Workloads: 32 blocks of 1 transform and 50 decode portions; 4 blocks of 500 and 25; 16 of the
first kind then 2 of the second; the same with an FP16 matrix search running continuously on
another stream; and 6 blocks of 20 and 10 with every GPU portion followed by a wait of 49 times its
own duration, to stand in for a slow GPU.

- **No fixed rule is safe.** Each loses by a factor of 2.2 to 73 somewhere, measured against the
  fastest rule in that scenario. The best single fixed
  rule ("CPU pulls decodes") is 2.2 times slower than the cost-based rule when the GPU is shared.
- **The cost-based rule is never far off:** at most 12% behind the best fixed rule, and ahead of
  all of them in the shared-GPU case.
- **Its deficit is exploration.** In the decode-heavy case it ran the two slower decode routes 77
  times, which accounts for the whole 5%. A rule that explores a route less often the worse it has
  measured would recover most of it; that was not tried.
- **On this host the CPU's comparative advantage is the decode, not the FFT.** The CPU decodes at
  435 per second against the GPU's 8,380 (19 times slower) and transforms 87 times slower, so the
  rule sent the CPU 5% of the decodes and almost no transforms. "CPU FFT, then GPU search" has it
  backwards here. The CPU FFT is untuned; a tuned one would narrow the gap.
- **Under a second GPU load the cheapest route changed, and the rule followed.** cuFFT in eight
  calls of 4,096 went from 0.18 ms to 8.8 ms per portion, because each call waits its turn behind
  the other load; the FP16 matrix DFT in one call of 32,768 went from 0.36 to 2.2 ms. The rule
  moved to the single large call (754 of 873 GPU transform portions) and gave the CPU 143 transform
  and 63 decode portions. The fixed rule built on the route that is cheapest alone took 2.3 times
  longer.
- So the cost to keep is **the time a portion actually took as the scheduler experienced it.** When
  the scheduler has the device to itself, that is the device time, and costs add. When something
  outside it shares the device, the time includes waiting, favours fewer and larger calls, and is
  still the right thing to minimise. This corrects the stricter wording above ("costs measured with
  the device to itself").
- **With the GPU slowed fifty-fold** the rule gave the CPU 51 of 60 decodes and 20 of 120
  transforms and finished 3 to 4 times sooner than four of the five fixed rules.

Limits: synthetic portions with no data dependence between stages; two executors; two portion
sizes for the GPU transform and one elsewhere; a cold start each time; the slow GPU is an added
wait, not a device; one run per cell, and the smaller differences (a few percent) are within what a
rerun would move. The check of predicted against actual backlog and the overload valve were not
exercised.

### What an adaptive scheduler should do

Design inference from these measurements, the model check and the brief. Items 1 to 5 were
exercised by the running test above in a simplified form; items 6 and 7 were not. An earlier wording of this list raced whole configurations
on "backlog cleared" in half-second windows. That score cannot be read in half a second for a
block that takes longer to process, and the number of configurations multiplies; the cost form
below replaces it.

1. **Portions.** Cut the work into batches of dwells and batches of hypotheses whose results do
   not depend on which route ran them. Then a probe is real work, and its output is kept.
2. **Portion size is a tuned quantity,** per route and device, starting near the device's cache
   size for memory-bound routes. Too small wastes launches; too large leaves the cache.
3. **Measure cost, not rate.** One of the scheduler's portions at a time has each device, and the
   time each takes is kept as a running cost per unit of work for every stage, route, portion size
   and device. If something outside the scheduler shares the device, that time includes the wait;
   it is still the cost to use.
4. **Keep the costs fresh by probing.** About half a second in every ten goes to portions run by
   routes and sizes other than the current best. Their output is kept, so the probe costs only the
   difference in speed.
5. **Assign by cost.** Each stage's work on a device goes by its cheapest route and size. A free
   executor takes the oldest portion of the stage in which it is least disadvantaged against the
   other devices, so a slow CPU takes transforms and leaves decodes to a fast GPU, and a fast CPU
   beside a slow GPU does the opposite. An executor leaves a portion alone when the others would
   clear all queued work sooner than it would finish that portion. No stage runs ahead of what the
   next can consume. The CPU worker budget is not reduced.
6. **Check the whole receiver.** Predicted and actual backlog cleared are compared every cycle. A
   persistent gap means the costs do not add on that device (the measured sharing error is about
   5%), and the two best assignments are then compared end to end over several seconds.
7. **Reduced-fidelity routes are an overload valve with a declared price** (next subsection), used
   only when the full configuration cannot keep up, charged to the margin, limited, and reported.

### What can be traded for speed, and what it costs

From `lattice_receiver.cpp` section 8
([output](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section8.md)),
0.4 s dwell, loss as received power at −10 and 0 dB-Hz:

| Reduction | Throughput effect | 0.1–1 Hz spread | 3 Hz | 10–60 Hz |
| --- | --- | ---: | ---: | ---: |
| Tone-grid bins only (352 instead of 704) | the case where a matrix DFT can win | 0.00 dB | 0.27 dB | 1.50 dB |
| One bin per tone | no weighting stage | 0.00 dB | 0.28 dB | 2.4–6.2 dB |
| Shedding dwells evenly by key, keeping 63% / 50% / 40% | 1.6× / 2.0× / 2.5× | 1.0 / 1.5 / 2.0 dB | same | same |
| 4-bit or 8-bit DFT coefficients | none through cuBLAS on this device | 0.01 dB | same | same |

- Computing only the tone grid is within the 1–2 dB that may be given up, at any spread. Using one
  bin is not, above a few hertz.
- Shedding dwells is the general valve: it works for any overload factor, on any stage, and its
  cost is known in advance. It never removes a hypothesis, so it cannot remove the true one.
- Narrower bins do not suit a one-stage matrix product: its cost grows with bins times samples, so
  a 4 s dwell is about 144 times the work per dwell of a 0.4 s dwell for the same band [derived,
  unmeasured]. Long dwells need the two-stage form.
- At +10 dB-Hz the tone-grid loss is 3 dB of rate at wide spreads, not 1.5.

## Reconciliation with earlier findings

| Earlier statement | Status after this run |
| --- | --- |
| "FP32 cuFFT plus gather was 4.1–5.1× faster than reused NVFP4 projection" ([Sol-6.2 README](../Sol-6.2/README.md)) | True as measured; the matrix route was handicapped by zero-padded rows and packing. With 1,024 active samples the FP16 product is 1.27× slower for all bins and faster for the tone grid. |
| "Use factored small-radix GEMM stages, never one large DFT matrix" ([format note §2](../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md)) | Not supported at this size. One unfused 2,048-column product ran at 70% Tensor activity and within a factor of 1.3 of cuFFT. |
| "A Tensor Core DFT pays only when fused with what follows" (same) | Too strong. Unfused, it already wins when a minority of bins is wanted. Fusion remains untested. |
| "The sliding Fourier recurrence was 3.4× faster than cuFFT at the projection stage" ([reception results](../Sol-6.2/tensor-reception-results.md)) | Unchanged for that fixture (34 bins, 4-sample hops). The review's break-even formula puts the lattice's timing grid well outside the recurrence's range. |
| This note's own first-run reading, "the matrix product is faster for the tone grid" | True at 32,768 dwells per call. At 4,096 per call cuFFT is 2.1× faster. Route and portion size have to be chosen together. |
| Best Tensor activity 56–57% for dense codeword scoring (same) | Superseded as the best figure: 69–71% for the matrix-product DFT. For the codeword search itself a direct kernel is faster than any matrix product. |
| "Keyed sync patterns, codewords and keyed message banks … the most Tensor-friendly part" (format note §1) | Contradicted for one-hot tone codes: the gather structure runs faster on CUDA cores. It may still hold for dense ±1 feature codes such as the two-tone experiment. |
| INT8 predicted to tie cuFFT, FP16 to lose 2–4× ([review §3.2](reviews/review-dft-fft-computation.md)) | Reversed in order: FP16 was the faster matrix route through cuBLAS. The review's rates were vendor peaks. |
| INT4 tensor operands: Ada only, no library path, none on Blackwell (review §3.2; [Sol-6.2 accelerators](../Sol-6.2/int4-and-accelerators.md)) | Not retested. The run used INT8 containers for 4-bit values, which is the "emulate lower precision" route of the [brief](../../../_appraisal/matrixModem.md). |

## Limits and open measurements

- One device, one library version, synthetic data, stock cuBLAS and cuFFT. No Ada, RTX 5090,
  integrated-GPU or CPU matrix route was measured; the CPU figures quoted are the review's single
  loaded-host run.
- No fused kernels, no FP8/FP4 `mma` operands, no CUTLASS. The integer path's low Tensor activity
  is a library result, not a hardware ceiling.
- The decode test used 64 tones and 24 symbols. The specification's 256 tones leave the direct
  kernel's cost unchanged and make the one-hot product four times larger.
- No complete receiver exists. Conditioning, acquisition over a real hypothesis grid, accumulation
  over long blocks, null statistics and ordered commit are not in these timings.
- Coherent attempts without FHSS (added 27 November) were not timed. They are the same decode with
  two table columns per coherent window, 128 columns for a ladder down to 1/64 of the block, so
  the measured 8,400 full 24-bit decodes a second extrapolate to about 65. The reasoning is in the
  [lattice design note](fsk-lattice-design.md#without-fhss-contiguous-dwells-coherent-windows-and-few-positions).
- The scheduler exists only as the test prototype: synthetic portions, two executors, a cold start,
  one device type and an emulated slow one. It is not in a receiver, and ordered commit, memory
  admission, the predicted-against-actual check and the overload valve were not exercised. The CPU
  routes are untuned and ran on a quota of 13.6 CPUs.
- Next measurements, in order of value: (0) the scheduler of the adaptive-routing section on a
  queued mixed workload, scored by backlog cleared; (1) a fused transform, energy, weight and
  accumulation kernel against the separate stages; (2) the integer path with a hand-written `mma` kernel and
  with FP8/FP4 operands; (3) the same stages on an RTX 4090 and an RTX 5090; (4) the CPU front end
  on an idle 5650U and 8840U with the project's own FFT.

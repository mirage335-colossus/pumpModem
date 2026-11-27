# FSK format and receiver split for a cheap FFT front end and Tensor Core follow-up

Design guidance from the 25–26 November 2026 Sol-6.2 discussion. **Status: design inference, not
measured.** No Matrix Modem receiver built this way exists yet. The operation counts below are
order-of-magnitude estimates, and the GEMM-DFT break-even values are reviewer arithmetic from the
[practice and computation review](mfsk-alternatives-practice-and-computation.md). Measured anchors
are cited where they exist.

The goal is the automatic-mode objective in AGENTS.md: the highest complete-receiver computational
ceiling on challenging broad searches. The CPU FFT stays a small, regular, fully parallel front end.
Hypothesis-heavy follow-up runs as large fixed-shape matrix products at high Tensor Core utilization.
Sensitivity must not be traded away for this; see [the fading-limit study](fading-limit-and-mfsk-alternatives.md)
and the robustness preference (no drastic failure, even theoretical).

## 1. Where the work lies

- **The FFT work is bounded by band × observation time, not by hypothesis count.** A 2.2 kHz band
  over a 41 s dwell is about 90,000 complex samples. One long FFT costs about 1.5×10⁷ operations;
  a set of overlapping short-time FFTs is similar, about 10⁷–10⁸ operations per dwell per timing
  offset, and a 4–8-offset timing grid multiplies that by 4–8. This is about the cost of a fine
  waterfall and is cheap on a CPU.
- **The hypothesis work is about bins × segments × hypotheses,** potentially 10¹⁰–10¹² operations
  per dwell, and nearly all of it is GEMM-shaped:
  - **Drift / frequency rate (specular tracking):** stacked segment spectra × per-segment
    phase-ramp matrices; a batched complex GEMM of N_seg × N_drift × nearby bins. A longer
    coherent span only lengthens the GEMM K dimension.
  - **Specular coherent detection:** the same contraction followed by |·|².
  - **Partly specular, partly diffuse mixtures:** a bank of weight vectors (K-factor, spread width)
    applied to bin energies.
  - **Timing offsets:** either recompute the short segment FFTs per sub-symbol offset (a few times
    the base FFT cost), or apply frequency-domain phase ramps (GEMM).
  - **Keyed sync patterns, codewords and keyed message banks:** large batched correlations; the
    most Tensor-friendly part.
- **GEMM after the transform has no FFT alternative,** so the FFT/GEMM crossover does not apply to it.

## 2. A DFT as GEMM: when it pays

**26 November 2026 correction (Fable-5.2 pass).** This section's break-even table is the FP16 row
only, and its conclusions were too strong. Measured on a full RTX PRO 6000 Blackwell, one unfused
FP16 matrix-product DFT of a 1,024-sample dwell is 1.27× slower than cuFFT for all 704 in-band bins,
1.2× faster for the 352 tone positions and 3.3× faster for 128 bins, at 70% Tensor Core activity and
32,768 dwells per call. At 4,096 dwells per call cuFFT is 2.1× faster for the tone positions, so the
route and the batch size have to be chosen together.
The earlier 4.1–5.1× cuFFT advantage charged the matrix route with rows that were 75% zeros. See the
[computation note](../Fable-5.2/computation-and-simulation-speed.md) and the
[DFT/FFT review](../Fable-5.2/reviews/review-dft-fft-computation.md). The text below is kept as
written.

A direct DFT as a matrix product costs about 8N operations per output point versus about 5·log₂N
for an FFT, so arithmetic alone favours the FFT beyond N ≈ 8–16. On Tensor Cores the relevant
question is whether the arithmetic fits inside the memory time already spent; a stand-alone FFT is
bandwidth-bound. Derived break-even for an RTX 5090 at 8 bytes of traffic per point:

| Form | Approx. operations per point | Stays within memory time up to |
| --- | --- | --- |
| Direct DFT matrix | 8N | N ≈ 120 |
| Two-factor (Monarch / Cooley–Tukey, two small GEMM stages) | ≈ 16√N | N ≈ 3,000 |
| Three-factor | — | N ≈ 60,000 |

Consequences:

- Use factored small-radix GEMM stages, never one large DFT matrix. tcFFT (radix-16 Tensor stages)
  measured only 1.2–1.9× over cuFFT FP16. FlashFFTConv reached 56–62% of peak only by fusing the
  whole chain.
- A Tensor Core DFT pays only when **fused** with what follows (window, transform, |·|², spread
  weights, drift accumulation) so data stays on chip. Unfused, cuFFT already runs at the bandwidth
  ceiling.
- Repository evidence agrees: on the regular-grid projection stage, FP32 cuFFT plus gather was
  4.1–5.1× faster than reused NVFP4 projection on a full RTX PRO 6000 Blackwell
  ([README](README.md), [receiver architecture](receiver-architecture.md#compare-complete-factorizations)).
  Tensor value lies in the follow-up, not the transform.

## 3. The front end: a complex short-time FFT, not a magnitude waterfall

A waterfall is nearly sufficient, with one correction: it must be **complex and keep phase**.
A magnitude waterfall discards exactly what specular coherent follow-up needs and supports only the
diffuse, energy-based hypotheses.

- **Segments short enough** that drift and Doppler change phase by much less than a cycle within a
  segment, so segments can be phase-ramped and summed exactly. Segments much shorter than
  1/(spread) keep a diffuse signal's energy in a few bins.
- **Overlap and oversampling.** Without them, timing-grid loss reaches about 2.5 dB and frequency
  scalloping about 3.92 dB. Twofold overlap and twofold zero-padding reduce these to about 1.16 and
  0.91 dB (see the acquisition-grid section of the [practice review](mfsk-alternatives-practice-and-computation.md)).
- **Possibly two or three segment lengths,** because a slow specular path and a 30 Hz spread prefer
  different resolutions; still at most a few times the waterfall cost.
- **CPU parallelism.** Segments, timing offsets and segment lengths are independent fixed-size
  transforms: batch them across the full `W = 2·H` worker budget with one plan per length and
  shared immutable input buffers. The FFT output is the shared input of all GPU work; overlap its
  production with GPU consumption where dependencies permit.

## 4. Transmission-format characteristics that keep the split clean

Choose the transmission so the receiver's search becomes a few fixed, regular grids. Then the CPU FFT stays a simple front end, and everything else is large batched matrix work. These features help:

**Timing and framing**
- **A fixed public symbol clock.** Symbols start on a known grid (`T`, slot calendar), so timing hypotheses are a few sub-symbol offsets, not a continuous search.
- **One symbol duration per configuration,** ideally a power-of-two or otherwise FFT-friendly number of samples, so a single segment plan serves the whole message.
- **Integer segments per symbol.** The dwell `T` is an integer number of coherent segments `Tc`, so symbol energy is an exact sum of segment results with no partial windows.
- **No guard intervals or variable-length fields** that move symbol boundaries. Physical completion through scored quiet slots already fits this.

**Frequency plan**
- **Tones on an exact multiple of the segment-FFT bin spacing** (spacing = k/Tc, integer `k`). Each tone hypothesis then indexes fixed bins, which avoids interpolation and scalloping, and tone hypotheses become a fixed gather.
- **Spacing several times the widest Doppler occupancy to be survived,** so each tone owns a fixed, non-overlapping bin set. The spread weighting is then the same small weight matrix for every tone, which batches well.
- **Keyed hopping drawn from a public, deterministic sequence.** The receiver knows each symbol's tone set in advance, which turns detection into gathers followed by a GEMM with known codewords.
- **One shared band with a fixed sample rate,** so a single FFT plan covers everything.

**Phase and envelope**
- **Constant envelope at full peak power** (sensitivity optimum).
- **Phase-continuous tone transitions,** or a known phase rule. This lets coherent processing span symbol boundaries on specular paths: a drift or phase hypothesis applies a known phase ramp across symbols, which is again a GEMM.
- **No unknown per-symbol phase jumps.** Random phase resets would force noncoherent combining at every boundary and waste a stable path.

**Coding**
- **A nonbinary code over the tone alphabet** (GF(M) or orthogonal symbol codes). Symbol likelihoods come straight from tone energies, with no binary demapping, and the decoder works on batched symbol metrics.
- **Fixed message geometry.** Codeword length, interleaver and slot count are fixed per configuration, so batch shapes stay fixed (this matches AGENTS.md, which forbids letting received lengths select DSP geometry).
- **A long interleaver over a fixed block,** which spreads fades without changing the processing shape.

**Acquisition aids**
- **Known, keyed sync patterns spread through the message,** not a single preamble. Timing, frequency and drift acquisition become correlations of many symbols against known patterns, done as GEMM over stored segment spectra.
- **Pilot or sync tones on the same grid as data,** so the same FFT output serves both.
- **Narrow uncertainty where possible:** Helper or GPSDO timing and frequency references shrink the hypothesis count, which saves sensitivity (fewer false-alarm candidates; roughly log₂A bits of energy per message for A hypotheses) and compute together.

**What to avoid**
- **Chirps or continuously swept tones:** they need dechirping per hypothesis, or many FFT plans.
- **Variable symbol lengths, adaptive rates or headers that change the geometry.**
- **Tones between bins, or spacing tied to the Doppler estimate at runtime.**
- **Short dwells chosen for strong signals:** they cost sensitivity at weak C/N₀ (up to 14 dB of time against the bound on slow paths).
- **Very narrow spacing with long dwells,** which lets adjacent tones blur together in fast fading.

The upshot: the CPU runs one fixed, overlapped complex FFT of the band, and the GPU does fixed-shape gathers and GEMMs for tones × spreads × drift × timing offsets × keyed patterns × codewords. All of this is a design inference from the studies; none of it has been measured in a Matrix Modem receiver.

## 5. Reaching high Tensor Core utilization

"Near multi-petaflop" is a target, not a demonstrated result. The best measured repository figure is
258 TFLOP/s nominal dense FP16-input scoring with 56–57% Tensor activity on a full RTX PRO 6000
Blackwell, falling to 28–29% activity across the full IQ-to-decision replay
([Tensor reception results](tensor-reception-results.md)). Multi-petaflop rates exist only at FP8/FP4
operand peaks, so reaching them requires:

- **Enough useful work per dwell.** A single regular 2.2 kHz stream with narrow uncertainty may not
  fill a modern GPU. Utilization rises with broad searches (drift, timing, spread, keyed banks) and
  with unsuccessful Helper acquisition, which is exactly the challenging case automatic mode targets.
  Count padded or redundant work separately from useful hypotheses.
- **Fixed large shapes.** The format rules above give fixed M, N, K dimensions, so plans and kernels
  are chosen once.
- **Fusion.** Contraction, |·|², spread weighting and peak/top-k selection in one kernel. Drift planes
  (hypotheses × bins × segments) reached 4–12 GB in published searches; never write them out.
- **Precision staged by sensitivity.** FP16/BF16 inputs with FP32 accumulation are safe for
  phase-ramp contraction. FP4 DFT or phase matrices have an error floor around −20 to −25 dB, which
  limits coherent gain on strong specular lines and is unacceptable before Helper/DATA channel
  isolation or with a strong interferer present. Qualify FP8/NVFP4 separately on post-isolation
  energy and codeword stages, where most of the operation count lies. An INT8 matrix contraction is
  not an INT8 FFT.
- **Exact search where approximation fails drastically.** A brute-force phase-ramp drift grid is
  exact. Taylor-tree shortcuts lose 1.4–2.5 dB within their design range and collapse to about 4%
  beyond it; under the robustness preference prefer the GEMM grid. Drift curvature multiplies the
  grid, but each hypothesis is still a phase-matrix GEMM.
- **Overlap.** CPU FFT of the next dwell or offset proceeds while the GPU scores the previous one.

## 5a. Very short messages: time per message, not bits

**26 November 2026 correction (Fable-5.2 pass).** The union-bound figures below are loose. Exact
values for noncoherent orthogonal signalling on AWGN at 10⁻³ are `E/N₀` = 12.43, 24.93 and 110.94
for 1, 10 and 100 bits, a relative airtime of 1 : 2.0 : 8.9. The optimum branch SNR in Rayleigh
fading is 3.7 at 10 bits and 6.5 at 100 bits, not "about 3". On the measured GPU the exhaustive
search ran faster as a direct kernel than as a matrix product. See the
[coding note](../Fable-5.2/coding-and-message-structure.md) and the
[exact tables](../../../_appraisal/algorithms-matrixModem/short-message-energy/README.md). The text
below is kept as written.

At minutes to hours per bit, the figure of merit is airtime per received message. (Chat discussion,
26 November 2026; design inference, not measured.)

- **A convolutional code cannot speed up a one-bit message.** One bit is a choice between two
  transmissions; the best design is two maximally distinguishable signals spread over enough
  independent fades. A convolutional code applied to one bit is repetition, and a terminated tail
  adds overhead (tail-biting removes the tail but gains nothing).
- **Airtime grows much more slowly than message length.** Illustration with the union bound for
  noncoherent M-ary orthogonal signaling, AWGN, no fading, message error 10⁻³:
  E/N₀ ≈ 2·(k·ln2 + ln(1/ε)) [derived, loose for large k]:

  | Bits per message k | E/N₀ | Relative airtime |
  | --- | --- | --- |
  | 1 | ≈ 15.2 | 1× |
  | 10 | ≈ 27.7 | ≈ 1.8× |
  | 100 | ≈ 152 | ≈ 10× |

  Short messages spend most energy on reliability (the ln(1/ε) term), not information: one bit
  costs about 20× (13 dB) more energy per bit than the asymptotic ln 2 limit. Carry as many bits
  per message as the application can use; ten bits cost little more airtime than one. The
  finite-message calculation in
  [`coherence-limits/finite_message.cpp`](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md)
  gives the corresponding fading-channel durations.
- **Up to about 20–30 bits:** send the whole message as one choice from a keyed codebook of 2ᵏ
  waveforms and decode by exhaustive maximum likelihood over the bank. This is optimal and beats any
  trellis code, and it is the Tensor Core-shaped workload (the Blackwell experiment decoded 15-bit
  messages against 67,108,864 hypotheses; [results](tensor-reception-results.md)).
- **Fading:** spread message energy over many independent fades (time/frequency diversity built
  into the codewords, or interleaving). Kennedy's optimum per-branch SNR of about 3 suggests about
  E/(3N₀) branches. Without diversity a Rayleigh-faded short message needs far more energy.
- **Longer messages,** where an exhaustive bank is infeasible: tail-biting convolutional codes with
  large memory and list/ML decoding are among the best published codes below about 100 bits
  [Coşkun et al., *Efficient error-correcting codes in the short blocklength regime*, Physical
  Communication 34, 2019](https://doi.org/10.1016/j.phycom.2019.03.004).
- **Bound message length with fixed geometry:** one configured short-message format and one long
  format, each with fixed batch shapes; received lengths never select DSP geometry.
- Open: compute the table exactly (not by union bound) with Rayleigh and diversity cases, and
  measure bank decoding against tail-biting codes at matched airtime.

## 6. Open measurements

1. Segment-GEMM drift search on a full RTX PRO 6000 Blackwell: Tensor activity, useful hypotheses/s
   and complete-receiver service against a CUDA-core baseline.
2. CPU complex STFT throughput across `2·H` workers on the representative receivers, to confirm the
   front end is not the bottleneck at the chosen overlap and segment lengths.
3. FP16 versus FP8/NVFP4 coherent gain loss on strong specular lines, before and after isolation.
4. Fused versus unfused contraction memory traffic and peak/top-k selection cost.
5. Whether the format rules above cost sensitivity relative to the bound (for example the
   integer-segment and exact-bin constraints), using the existing
   [coherence-limit calculations](../../../_appraisal/algorithms-matrixModem/coherence-limits/README.md).

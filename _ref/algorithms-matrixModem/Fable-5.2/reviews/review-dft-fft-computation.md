# Direct DFT versus FFT for the Matrix Modem receiver: literature and documentation review

26 November 2026 · Fable-5.2 review · research material (retained under the
[research-material rule](../../../../docs/development.md#research-material))

**Question.** The project owner asked: "If DFT can outperform FFT, that should be investigated."
This review collects the cases where a direct or structured DFT (Goertzel, sliding/hopping DFT,
a one-stage matrix-product "GEMM-DFT", hypothesis-specific bases) beats an FFT, with crossover
conditions, measured throughput where it exists, and derived operation counts for the Matrix Modem
shapes (about 6.25 Hz tone spacing, 160 ms symbols, 3200 Hz complex baseband, `N` = 512 samples per
symbol, 352–448 usable bins).

**Status.** Literature and documentation review with reviewer arithmetic, plus one small CPU
microbenchmark run on a loaded host. No GPU was run. Nothing here is a Matrix Modem receiver
measurement, a sensitivity result or a capacity claim.

**Labels.** Every number carries one of:
**[measured: who, on what]**, **[vendor]** (vendor documentation or claim), **[derived]** (arithmetic or
numerical calculation done for this review; theoretical, with the stated assumptions), **[heuristic]**
(reasoned estimate, not derived from a model that was checked). Citations carry **[verified]**
(DOI resolved through `api.crossref.org`, arXiv identifier through the arXiv API, or the URL fetched,
all on 26 November 2026) or **[unverified]**, and say how much of the source was read. Flop counts use
the FFTW convention `5·N·log₂N` for a complex FFT and 8 real flops per complex multiply-accumulate
("cMAC"); integer "ops" count a multiply and an add separately, as the vendor tables do.

---

## 0. Answer in brief

1. **For the transform itself, on every target, the FFT wins unless very few bins are needed.**
   Measured here on the Ryzen 5 PRO 5650U (AVX2, one core, loaded host): a 512-point single-precision
   FFT took 0.81 µs; a direct DFT took 50–76 ns per bin with int8 operands and 73–120 ns per bin in
   FP32. The crossover is **`K*` ≈ 11–16 bins of 512 for int8 and 7–11 for FP32**
   [measured: this review, 5650U]. The textbook rule `K* ≈ (5/6)·log₂N ≈ 7.5` is right to within 2×.
   For the 64 keyed tones of a symbol, FFT plus gather is 4–5× faster than an int8 direct DFT and
   6–8× faster than an FP32 one.
2. **The transform is not where the computational ceiling is.** One stream with 8 timing offsets and
   4× frequency oversampling costs about 32 µs of one 5650U core per 160 ms symbol period
   [measured basis], a real-time factor near 5000 on one core. Post-transform work is about 4× the
   transform arithmetic for a drift × spread bank alone (§4.1) [derived] and 10²–10⁴× once codeword
   or keyed-pattern banks are searched
   ([Sol-6.2 §1](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md)). No DFT variant can raise
   the complete-receiver ceiling by more than the transform's share: about 20% in the first case,
   under 1% in the second.
3. **A direct DFT can win in four specific places:**
   - few bins on a CPU (`K` ≲ 8–16 of 512 on AVX2; predicted 20–60 on AVX-512 VNNI [heuristic]);
   - small hops with few bins (sliding/hopping update wins when hop `L < 5·N_fft·log₂N_fft/(8K)`
     [derived]; the Matrix timing grid of `N/8` with all bins does not qualify);
   - **integer operands with exact accumulation**: an INT8-operand one-stage DFT with s32
     accumulation has a −50 dBc coefficient error floor and no rounding growth, where an 8-bit
     fixed-point FFT of 512 points has an output noise-to-signal ratio of about −9 dB [derived];
   - **GPU batch work that shares one bin set**, with INT8 samples and few outputs: predicted 3–6×
     less memory traffic than cuFFT plus gather [derived, roofline, untested].
4. **"An unfused Tensor Core DFT cannot beat cuFFT because the FFT is bandwidth-bound" is correct
   for FP16/TF32 operands and is supported by 2023–2026 literature, but it is precision-specific.**
   At vendor INT8 and FP4 rates on Blackwell the arithmetic of a one-stage 512-point DFT fits inside
   the memory time (`N*` ≈ 470–1125) [derived]. The roofline then predicts parity, not a loss, and
   the repository's measured 4.1–5.1× cuFFT advantage included a fourfold zero-column handicap on the
   matrix route. Parity with a −20 to −50 dBc coefficient floor is still not a reason to replace an
   exact FFT.
5. **Hypothesis-specific bases do not need a time-domain matrix.** Fractional-bin offsets, Doppler
   spread weights and DPSS tapers are compact in frequency, so "FFT, then a short matrix on complex
   bins" does the same work with 30–100× fewer cMACs. Intra-symbol drift compensation is unnecessary
   below about 20 Hz/s at 160 ms symbols (loss ≤ 0.06 dB) [derived]. Any stationary quadratic
   detector is an exact linear function of the 2×-zero-padded periodogram [derived].
6. **Batch simulation should not form time samples where the waveform rules make the bin statistics
   exact.** Noise generation, not the transform, dominates a time-domain simulator.

---

## 1. What was already established, and what is new here

| Topic | Already established, and where | New, sharpened or contradicted here |
| --- | --- | --- |
| FFT/GEMM split | CPU FFT front end, GPU matrix follow-up; FFT work bounded by band × time, hypothesis work 10¹⁰–10¹² ops ([Sol-6.2 format note §1](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md)) | Measured CPU FFT and direct-DFT costs on the 5650U; per-workload operation counts and predicted winners (§4) |
| GEMM-DFT break-even | Direct DFT fits memory time to `N` ≈ 120, two-factor to 3,000, three-factor to 60,000 on RTX 5090 at 8 B/point ([same note §2](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md)) | That table is the FP16-with-FP32-accumulate row only. INT8 gives `N*` ≈ 470 and FP4 ≈ 935 on RTX 5090; 562 and 1125 on RTX PRO 6000 (§3.2). "Never one large DFT matrix" does not follow at `N` = 512 for INT8/FP4 |
| cuFFT versus NVFP4 projection | cuFFT plus gather 4.1–5.1× faster on a full RTX PRO 6000 Blackwell ([projection probe](../../../../_appraisal/algorithms-matrixModem/projection-probe/README.md)) | The matrix route multiplied 2,048-column rows that were 75% zeros. With 512 active columns the same data predict near parity (§3.2). The probe also fixes the measured cuFFT rate: 9.7×10¹⁰ points/s |
| Sliding projection | Sliding Fourier recurrence beat cuFFT 3.4× at the projection stage for 34 bins and 4-sample hops ([reception results](../../Sol-6.2/tensor-reception-results.md)) | Break-even hop formula; that fixture sits 47× inside it, the Matrix `N/8` timing grid with all bins sits 8–10× outside it (§2.2). An integer modulated sliding DFT is drift-free (§2.2) |
| Pruned/sparse transforms | Sparse FFT gives about 2× and degrades near threshold ([practice review](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md)) | Selected-output cases: Goertzel, pruned FFT (`O(N log K)`), transform decomposition, with measured crossover (§2.1) |
| Grid losses | 3.92 dB scalloping, 0.91 dB at 2× zero-padding, 2.50/1.16/0.56 dB timing loss at hops `T/2`, `T/4`, `T/8` ([practice review, acquisition grids](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md)) | 4× and 8× padding, truncated frequency interpolation (8 taps = 0.22 dB), NUFFT-type kernels, window on-bin losses, chirp-mismatch loss versus `rate·T²` (§2.3, §2.4) |
| Quantization | Input efficiency 0.637/0.881/0.963/0.988 for 1–4 bits; DFT-matrix error −76.6/−49.8/−25.5/−20.1 dB for FP16/INT8/INT4/NVFP4; interferer table ([practice review, quantization](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md)) | Coefficient error is not white: worst single spur and correlation loss per alphabet, including sign, ternary and E2M1; 8/π² check; AGC-error sensitivity per quantizer; fixed-point FFT growth versus one-stage DFT (§2.5) |
| Tensor-core transforms | tcFFT 1.2–1.9×, FlashFFTConv, HadaCore 1.0–1.4×, TurboFFT 90% bandwidth, Matsuoka epilogue wall ([Opus-5.5 tensor review §2–3](../../Opus-5.5/reviews/review-tensor-core-dsp.md)) | M³XU (SC24), TurboFNO (2025), WarpDrive/TensorFHE INT8 tensor-core NTT, Merry (2023) FP16-FFT rejection, an NVIDIA MMA-FFT patent (text not accessed) (§3.2, §5) |
| INT4 availability | INT4 native on Ada only; none documented on SM120 ([Opus-5.5 hardware review](../../Opus-5.5/reviews/review-gpu-hardware.md)) | Re-verified today against CUDA guide v13.5 Table 36, PTX ISA 9.5, cuBLAS 13.5, CUTLASS and TensorRT documentation; cuBLASLt has no 4-bit integer type (§3.2) |
| CPU int8 rates | uops.info instruction throughputs ([Opus-5.5 hardware review §1c](../../Opus-5.5/reviews/review-gpu-hardware.md)) | AMX measured instruction rate and vendor claim; an actual AVX2 int8 DFT kernel timed against FFTW (§3.1) |
| Simulation | Evaluation plan and fixtures ([tensor-reception README](../../../../_appraisal/algorithms-matrixModem/tensor-reception/README.md)) | Conditions under which a symbol-domain simulator is exact (§4.3) |

---

## 2. Where a direct or structured DFT beats an FFT

### 2.1 Few output bins

**Counts for `K` bins of an `N`-point transform, complex input.**

| Method | Flops | `N` = 512, `K` = 64 | Break-even `K*` at `N` = 512 | Source |
| --- | --- | ---: | ---: | --- |
| FFT, then gather | `5·N·log₂N` | 23,040 | — | FFTW convention |
| Goertzel | `(2N+4)` multiplies + `(4N+4)` adds per bin ≈ `6N·K` | 196,608 | 7.5 | [Wiki-Goertzel], count unsourced there; break-even [derived] |
| Direct correlation | `8N·K` | 262,144 | 5.6 | [derived] |
| Pruned FFT | `O(N·log K)` | ≤ 33% saving | — | [FFTW-pruned]; saving [derived] |
| Transform decomposition | FFTs of length ≈ `K` plus Horner recombination; fewer operations than pruning | — | — | [SorensenBurrus93], abstract |
| DFT via Walsh transform | ≈ `N·L/6` multiplications for `L` coefficients | — | `L*` ≈ 27 in multiplications only, against `(N/2)·log₂N`; units of the count not checked | [TadokoroHiguchi78], abstract; break-even [derived] |

- FFTW's own guidance: "the performance gains in general from pruned FFTs are quite modest"; it
  "would not recommend bothering" with a pruned 1-D FFT "unless you want 1% of the outputs or fewer"
  [FFTW-pruned, verified]. A selected-output FFT replaces `log N` by `log K`, a small additive saving.
- Rounding matters for the floating-point versions. FFTW states that the naive formula and Goertzel
  have error bounds growing as `O(N^{3/2})` and `O(N^{5/2})`, against `O(log N)` worst case and
  `O(√log N)` on average for an FFT [FFTW-pruned, FFTW-accuracy, verified]; Schatzman's abstract says
  FFT-computed transforms "are far more accurate than slow transforms" [Schatzman96]. Gentleman shows
  Goertzel "should be avoided, particularly for low frequencies" [Gentleman69, abstract]. The
  generalized Goertzel evaluates non-integer bins at negligible extra cost [SyselRajmic12, abstract].
- **Measured crossover on the 5650U** (single core, loaded host, method in Appendix C):

  | `K` bins of 512 | FFT 512 | FP32 FMA direct | int8 direct | int8, shared bin set, cache-blocked |
  | ---: | ---: | ---: | ---: | ---: |
  | 8 | 0.81 µs | 0.89 µs | 0.58 µs | 0.42 µs |
  | 16 | 0.81 µs | 1.76 µs | 1.15 µs | 0.81 µs |
  | 64 | 0.81 µs | 6.76 µs | 4.43 µs | 3.22 µs |
  | 448 | 0.81 µs | 48.2 µs | 32.2 µs | 22.7 µs |

  [measured: this review, Ryzen 5 PRO 5650U, FFTW 3.3.10 with SSE2/AVX codelets, g++ 14.2
  `-O3 -march=native`, thread CPU time, unrelated load average 19–27 on 12 logical CPUs].
  `K*` is 7–11 bins in FP32 and 11–16 with int8 operands. The int8 kernel is exact (zero difference
  from an integer reference).
- Two effects limit the direct form. First, the coefficient rows stream from cache: with a different
  bin set per symbol each cMAC reads 4 bytes of int8 coefficients (8 bytes in FP32), and the rate
  was 6.8–7.4 cMAC/ns; with a bin set shared by 64 symbols and cache blocking it rose to
  9.5–10.5 cMAC/ns [measured: this review]. Second, AVX2 lacks a widening dot product, so one int8
  cMAC needs `VPMADDUBSW` plus `VPMADDWD`.
- **Other CPUs** [heuristic, from instruction throughput, not measured]: AVX-512 VNNI executes
  64 int8 MAC/cycle on Zen 4 and client Intel cores and 128 on Zen 5 and server Intel cores
  [measured: uops.info, `VPDPBUSD zmm` at 1.0 and 0.5 cycles]. As an instruction-level upper bound
  that is 4–9× the per-cycle rate the AVX2 kernel achieved, while the FFT gains at most about 2× from
  AVX-512. A real kernel will not reach the bound; `K*` would move to a few tens of bins, perhaps
  20–60, so the 64-tone keyed case approaches parity on Zen 5 class cores. Intel AMX executes
  one `TDPBSSD` (16,384 int8 MAC) every 16 cycles, 1,024 MAC/cycle [measured: uops.info, Emerald
  Rapids; vendor claim "2048 int8 operations per cycle (vs. 256 without AMX)", Intel-AMX]. At that
  rate all 448 bins of 512 samples take about 900 cycles, the same order as an FFT, but only when at
  least 16 rows share the bin set, because the tile is 16 rows deep.

**Conclusion.** On the AVX2 receiver targets, take keyed tones by gather from an FFT. A direct int8
DFT is worth a test only at `K` ≤ 16, or on AVX-512 VNNI/AMX hosts with shared bin sets.

### 2.2 Sliding and hopping analysis

**Recurrences.** The sliding DFT updates bin `k` when the window advances one sample:
`S_k[n] = (S_k[n−1] − x[n−N] + x[n])·e^{j2πk/N}`, one complex multiply and two complex adds per bin
per sample; it is more efficient than Goertzel for sample-by-sample outputs, "especially when only a
subset of the DFT output bins are required" [JacobsenLyons03, abstract]. The pole sits on the unit
circle, so a finite-precision twiddle can move it outside; Jacobsen and Lyons discuss output
stabilization, and Kim and Chang give a closed-form noise-to-signal ratio for finite-bit twiddles
[KimChang00, abstract]. Duda's modulated SDFT moves the bin to DC so that the feedback coefficient is
exactly 1: "the finite precision of its representation is no longer a problem" [Duda10, abstract].
The hopping DFT [ParkKo14] and the modulated hopping DFT [Wang15, abstract: "arbitrary time hop L",
pole "precisely" on the unit circle] extend this to hops of `L` samples; Park's later algorithm adds an
updating vector to the previous bins [Park17, abstract]. Non-recursive tree algorithms are the
numerically stable alternative [RichardsonEddy19]. A 2022 review compares nine recursive variants
[ChauhanSingh22, metadata only].

**Cost per hop** [derived; `N` = 512; FFT recomputation = 23,040 flops]:

| Hop `L` | Block update, all 512 bins (`8KL + 6K`) | Block update, `K` = 64 | `N/L` pre-twisted `L`-point FFTs (`5N·log₂L + 12N`) | Break-even `K` for the block update |
| ---: | ---: | ---: | ---: | ---: |
| 256 (`N/2`) | 1,051,648 | 131,456 | 26,624 | 11 |
| 128 (`N/4`) | 527,360 | 65,920 | 24,064 | 22 |
| 64 (`N/8`) | 265,216 | 33,152 | 21,504 | 44 |
| 16 | 68,608 | 8,576 | 16,384 | 172 |
| 4 | 19,456 | 2,432 | 11,264 | 606 |
| 1 | 7,168 | 896 | 4,096 | all |

- The block update projects the `L`-sample difference `x[n+i] − x[n+i−N]` on each bin and rotates.
  It beats a recomputed FFT only when `L < 5·N_fft·log₂N_fft / (8K)`.
- A hop of `L` over all `N` bins can also be computed as `N/L` pre-twisted `L`-point FFTs. Its saving
  over a full FFT is `log₂N / (log₂L + 2.4)`: 7% at `N/8`, 29% at `N/32` and 2× at 4 samples; the
  one-sample SDFT (`8N`) is 5.6× cheaper. Like pruning, it only removes part of a logarithm.
- **The repository's sliding result is consistent with this and does not transfer.** That fixture
  used a 1,024-point FFT, 34 bins and 4-sample hops: break-even hop 188 samples, a 40× flop advantage,
  measured 3.4× at the stage on GPU ([reception results](../../Sol-6.2/tensor-reception-results.md)).
  The Matrix timing grid of `N/8` = 64 samples with 448 bins (or 1,792 fine bins against a 2,048-point
  FFT) has break-even hops of 6.4 and 7.9 samples, so sliding loses by 8–10×. With 64 keyed tones the
  break-even hop is 45 samples: near parity at `N/8`, a win at `N/16` and below.
- **Fixed point** [derived]. Write the modulated form with an integer, `N`-periodic coefficient table
  `c_k[n] = round(A·e^{−j2πkn/N})`: `S_k[n] = S_k[n−1] + (x[n] − x[n−N])·c_k[n]`. Because
  `c_k[n−N] = c_k[n]`, the sum telescopes exactly. With integer samples and exact (s32) accumulation
  the state equals the direct integer DFT of the current window for all time: no drift, no reseeding,
  no damping factor. It is a comb followed by an exact integrator, as in a CIC filter. A hop of `L` is
  a matrix product of the `L`-sample difference block with an `L`-column slice of the same integer DFT
  matrix, accumulated into the running state. A floating-point SDFT or mSDFT is only marginally
  stable; rounding accumulates as a random walk and needs periodic reseeding, which is what the
  repository fixture does with its direct FP32 seed. A fixed-point SDFT study reports slow error
  drift without correction [vdBylInggs16, search snippet only].

### 2.3 Bases an FFT does not produce directly

The candidates are fractional-bin frequency offsets, drift-compensated (chirp) bases, time-offset
windows and Slepian (DPSS) multitaper bases. A single matrix product
`Y[H × B] = Φ[H × 2N] · X[2N × B]` can hold any of them as rows of `Φ` at a fixed shape, for
`8N` = 4,096 flops per complex output at `N` = 512. The question is what the FFT-based routes cost
and lose.

**Fractional-bin frequency evaluation** [derived unless noted; outputs = 448 tones × `H` offsets]:

| Route | Flops per symbol | Worst-case loss or error |
| --- | ---: | --- |
| `N`-point FFT, nearest bin | 23,040 | 3.92 dB at half-bin offset |
| 2× zero-padded FFT | 51,200 | 0.91 dB |
| 4× zero-padded FFT, or four mixed 512-point FFTs | 112,640, or ≈ 104,000 | 0.22 dB |
| 8× zero-padded FFT | 245,760 | 0.056 dB |
| `N`-point FFT + truncated Dirichlet interpolation, 8 / 16 / 32 taps | 23,040 + 64 / 128 / 256 per output | 0.23 / 0.11 / 0.055 dB |
| 2× FFT of pre-compensated samples + `w`-tap kernel (type-2 NUFFT) | 51,200 + `8w` per output | error ≈ 10^−(w−1): `w` = 4 gives about 10⁻³ [Barnett19, kernel-width rule from memory] |
| Chirp-Z / Bluestein, `M` arbitrary-spaced outputs | 2 FFTs of length ≥ `N+M−1` + 3 pointwise products: ≈ 121,000 for `M` = 448; ≈ 565,000 for `M` = 1,792 | exact to rounding [Rabiner69, Bluestein70] |
| Direct matrix on samples | 1,835,008 per offset set | exact to coefficient precision |

- The NUFFT costs `O(N log N + M)` against `O(NM)` for direct evaluation; FINUFFT reports 10⁶–10⁸
  points/s on a CPU and up to 10⁹ on a GPU [FINUFFT-docs, verified; Barnett19, abstract; Shih21].
- A zoom FFT (mix, decimate, short FFT [HoyerStork77]) or chirp-Z transform pays when the band of
  interest is a small fraction of the sample rate. The Matrix passband is 69–88% of the 3,200 Hz
  sample rate, so neither helps; zero-padding or interpolation is cheaper.
- With tones on exact bins and a frequency error `δ` bins, the loss is `20·log₁₀ sinc(δ)`:
  0.036 dB at 0.05 bin (0.31 Hz) and 0.14 dB at 0.1 bin [derived]. With a Helper or GPSDO reference
  the oversampled grid is only needed for acquisition and drift tracking.
- Interbinning and Fourier interpolation are standard in pulsar searches [Ransom02]; three-bin
  estimators serve fine frequency estimation [JacobsenKootsookos07].

**Drift (chirp-rate) bases** [derived]. The loss from an uncompensated linear drift across a coherent
segment depends only on `c = rate·T²`, the number of bins swept:

| `c` | 0.25 | 0.5 | 1 | 2 | 3 | 4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss | 0.015 dB | 0.06 dB | 0.24 dB | 0.97 dB | 2.2 dB | 4.0 dB |
| Rate at `T` = 0.16 s | 9.8 Hz/s | 19.5 Hz/s | 39 Hz/s | 78 Hz/s | 117 Hz/s | 156 Hz/s |
| Rate at `T` = 1.28 s | 0.15 Hz/s | 0.31 Hz/s | 0.61 Hz/s | 1.2 Hz/s | 1.8 Hz/s | 2.4 Hz/s |

  At 160 ms symbols no HF drift rate needs an intra-symbol chirp basis. Drift is a per-symbol
  fractional-bin shift, which is a gather on the oversampled grid. A chirp basis matters only for
  coherent segments of a second or more on stable paths, and there it is the per-segment phase-ramp
  matrix on stored spectra already described in
  [Sol-6.2 §1](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md), not a time-domain matrix.
  The fractional Fourier transform is an `O(N log N)` alternative for one chirp rate [Ozaktas96].

**Doppler-spread and Slepian bases.**

- **Exact identity** [derived]. For any Hermitian Toeplitz `Q` (a stationary kernel `q[m−n]`) and any
  `M ≥ 2N−1`, `xᴴQx = Σ_k w_k·|X_M[k]|²`, where `X_M` is the `M`-point zero-padded DFT and
  `w_k = (1/M)·Σ_l q[l]·e^{j2πkl/M}`. So every stationary quadratic detector on `N` samples, including
  the locally optimal (low-SNR) estimator-correlator `xᴴR_s x` for any Doppler spectrum and the
  eigenvalue-weighted multitaper sum `Σ_j λ_j|v_jᴴx|² = ∫_{−W}^{W}|X(f)|²df`, is an exact linear
  function of the 2×-zero-padded periodogram. The weights can be negative and span all `2N` samples;
  truncating them to the tone's neighbourhood is the usual weighted periodogram.
- **What is not Toeplitz.** The unweighted DPSS projector `Σ_{j<J}|v_jᴴx|²`, and the finite-`N`
  estimator-correlator at high per-branch SNR (weights `λ_j/(λ_j+N₀)` on the DPSS eigenvectors for a
  flat spectrum [Kailath60, Slepian78, Thomson82]), need complex projections. Because the FFT is
  unitary, `v_jᴴx = (Fv_j)ᴴ(Fx)`, and a DPSS taper is by construction concentrated in about `2NW`
  bins. The projection is therefore a short matrix on complex FFT bins. The fast Slepian transform
  makes this precise: approximate projections onto the leading Slepian vectors at a complexity
  "comparable to the FFT" [Karnik19, abstract]; the correction rank grows as
  `O(log N·log 1/ε)` [from memory of the paper]. For a Doppler spread `B_D` over `T`, the number of
  significant tapers is about `B_D·T + 1`: 1–2 at 1–5 Hz, about 6 at 30 Hz, for `T` = 0.16 s
  [derived]. Slepian bases remove the leakage bias of a Fourier basis for time-variant channels
  [ZemenMecklenbrauker05, abstract].
- **Cost** [derived]. A time-domain matrix needs 512 cMAC per (tone, taper, offset) row. A matrix on
  complex bins needs about `2NW + 8` ≈ 10–16 cMAC per row, 30–50× fewer, with an error set by the
  truncated taper energy.

**Time-offset windows.** A window shifted by `τ` samples uses samples outside the current window,
so it is not a function of one FFT's output. It needs another FFT, or the hopping update of §2.2.
This is the one hypothesis dimension where the matrix form has a structural shortcut: eight offsets
at hop `N/8` cost the same block updates as one direct DFT (7.3 Mflop for 1,792 fine bins), though
that is still 8× the arithmetic of eight zero-padded FFTs (0.90 Mflop) [derived].

**Conclusion.** A single "generalised DFT" matrix on time samples is the simplest fixed shape and is
exact to its coefficient precision, but each of these bases is compact in frequency. FFT plus a
short matrix on complex bins is 30–100× cheaper in arithmetic. The time-domain matrix earns its cost
only where a Tensor Core rate advantage of the same order is actually realised and the outputs are
reduced on-chip (§3.2, §4).

### 2.4 Polyphase channelizers, windowed FFTs and a matrix tone bank

- **Cost** [derived]. An `M`-channel polyphase filter bank is a `P`-tap-per-branch FIR followed by an
  `M`-point FFT [harris03, Price16]. For `M` = 512 and `P` = 4 that is about 8,200 flops of FIR plus
  23,040 of FFT per output frame, 36% over a plain FFT; a 2×-oversampled bank doubles the frame rate.
  A matrix tone bank costs `8N` per channel regardless of the window, so any window, taper or
  per-tone weight is free once the `O(NK)` cost is paid.
- **A filter bank is the wrong matched filter for hopped tones.** Its prototype spans `P` symbol
  lengths, so with a tone that changes every symbol it integrates across symbol boundaries. For a
  rectangular-keyed tone on an exact bin, the rectangular `N`-sample window is the matched filter.
- **Loss relative to that matched filter** [derived, `N` = 512; consistent with Harris78]:

  | Window | On-bin | Worst case, no padding | 2× padding | 4× padding |
  | --- | ---: | ---: | ---: | ---: |
  | Rectangular | 0.00 dB | 3.92 dB | 0.91 dB | 0.22 dB |
  | Hamming | 1.34 dB | 3.10 dB | 1.78 dB | 1.45 dB |
  | Hann | 1.76 dB | 3.19 dB | 2.11 dB | 1.85 dB |
  | Blackman | 2.37 dB | 3.47 dB | 2.65 dB | 2.44 dB |

  A window or filter bank buys sidelobe rejection against strong off-bin signals and costs
  1.3–2.4 dB of white-noise sensitivity unless the transmitter adopts the same pulse shape. Oversampled
  banks exist to suppress aliasing between sub-bands in FDM channelizers [Tuthill15, abstract], which
  is not this receiver's problem.
- **Practice.** The MeerKAT GPU F-engine uses a 16-tap filter bank, FP32 cuFFT and a fused
  post-processing kernel; all three kernels are memory-bound, the post-processing at 6–7 flops per
  byte [measured: Merry23, RTX 3070 Ti]. Its narrowband mode fuses mixing, low-pass filtering and
  subsampling in one kernel that sits "at the boundary between being memory-bound and
  compute-bound" [katgpucbf-F, verified]. TINA's FP32 matrix filter bank beat its FP16 Tensor
  variant ([Sol-6.2 bottlenecks](../../Sol-6.2/tensor-receiver-bottlenecks.md)).

### 2.5 Integer and low-precision arithmetic

**Rounding growth: FFT versus one-stage DFT.**

- Fixed-point radix-2 FFT, white input, `B` fractional bits. With the input scaled by `1/N` to
  prevent overflow, the output noise-to-signal ratio is `N²·2^{−2B}`: it "increases as N², or 1 bit
  per stage" [Settimi10, appendix reproducing Oppenheim, Schafer and Buck 1999, read in full;
  primary analyses Welch69, OppenheimWeinstein72, TranThongLiu76, abstracts only]. With a factor-½
  scaling at every stage it is about `4N·2^{−2B}`, half a bit per stage [from memory of the same
  textbook; not re-read]. Floating-point FFT noise grows only as `log₂N` with rounding and faster
  with truncation [Weinstein69, abstract].
- At `N` = 512 [derived from those formulas]:

  | Register | Input-scaled FFT | Per-stage-scaled FFT | One-stage integer DFT, exact accumulation |
  | --- | ---: | ---: | ---: |
  | 8-bit (`B` = 7) | +12 dB | −9 dB | −50 dBc total coefficient error (INT8 operands) |
  | 12-bit (`B` = 11) | −12 dB | −33 dB | −74 dBc (11-bit coefficients) |
  | 16-bit (`B` = 15) | −36 dB | −57 dB | below −90 dBc |

  An INT8-operand matrix DFT with s32 accumulation is as accurate as a 15-bit fixed-point FFT. This is
  the quantitative form of the AGENTS.md rule that an INT8 matrix contraction is not an INT8 FFT. The
  s32 accumulator cannot overflow below about 1.3×10⁵ INT8 products
  ([practice review](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md)); 512 samples use
  0.8% of that.
- The claim "a one-stage DFT has only coefficient-quantization error" holds for exact integer
  accumulation (s32, or FP32 accumulation of integer-valued E2M1 products below 2²⁴, see the
  [hardware review](../../Opus-5.5/reviews/review-gpu-hardware.md)). It does not hold for a
  low-precision floating accumulator: there a direct sum is less accurate than an FFT (§2.1). Merry
  simulated an FP16 FFT and found harmonics near −75 dB, above his filter-bank floor, and kept FP32
  [measured (simulation): Merry23].

**Coefficient (reference) quantization** [derived; `N` = 512, 448 bins, references
`Q(A·cos) + j·Q(A·sin)`; Appendix B]:

| Reference alphabet | White-noise correlation loss, mean (worst) | Total spurious response | Worst single spur |
| --- | ---: | ---: | ---: |
| Sign, `{±1 ± j}` | 0.92 dB (2.04 dB at `k = ±N/4`) | −6.3 dBc | −9.5 dBc (−7.0 at `±N/4`) |
| Ternary `{0, ±1}`, threshold 0.383 | 0.35 dB (0.42) | −10.8 dBc | −13.3 dBc |
| Ternary, threshold 0.5 | 0.39 dB (0.42) | −10.2 dBc | −11.2 dBc; 3rd and 9th harmonics nulled |
| `{±1, ±3}` | 0.21 dB | −13.0 dBc | −13.2 dBc |
| 3-bit, `round(3cos)` | 0.064 dB | −18.3 dBc | −21.0 dBc |
| E2M1 codebook, scale 6 | 0.036 dB | −20.8 dBc | −24.8 dBc |
| E2M1 codebook, scale 4 | 0.026 dB | −22.2 dBc | −26.4 dBc |
| INT4, `round(7cos)` | 0.013 dB | −25.3 dBc | −28.6 dBc |
| 5-bit / 6-bit / 7-bit | 0.003 / 0.001 / 0.000 dB | −31.7 / −38.1 / −44.1 dBc | −34.8 / −44.0 / −45.9 dBc |
| INT8, `round(127cos)` | 0.000 dB | −50.2 dBc | −52.0 dBc |
| 11-bit mantissa (FP16-like) | 0.000 dB | −73.8 dBc | −79.3 dBc |

- The totals reproduce the prior −49.8, −25.5 and −20.1 dB figures for INT8, INT4 and NVFP4. The
  total follows `−(7.8 + 20·log₁₀A)` dB for a peak integer amplitude `A`, about 6 dB per bit.
- **The error is not noise.** A quantized reference for bin `k` is `G[(k·n) mod N]` for one fixed
  quantized period `G`, so its error is the harmonic series of `G` placed at bins `h·k mod N` with
  `h = −3, 5, −7, 9, …`. These alias back into the passband. Averaged over 511 other bins the INT4
  leak is about −52 dBc per bin, but the worst (interferer, tone) pair sees −28.6 dBc.
- **Coefficient quantization does not limit coherent gain.** The correlation loss of a 4-bit
  reference is 0.013–0.04 dB. Its cost is leakage from strong components at other frequencies. This
  corrects the wording in [Sol-6.2 §5](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md),
  where the −20 to −25 dB floor is said to limit coherent gain on strong specular lines; with one
  tone per symbol there is no self-leakage, and the exposure is to interferers.
- **The 0.9 dB figure is confirmed, with its caveat.** A hard-limited (square-wave) reference loses
  `8/π²` = 0.912 dB against a sinusoidal reference in white noise [derived analytically]. For
  `N` = 512 the complex sign reference gives 0.91–0.98 dB on 441 of 447 non-DC bins [derived]. Bins
  whose phase grid lands on zero crossings depend on the `sign(0)` convention (2.04 dB at `±N/4`,
  1.19 dB at `±N/8, ±3N/8`); a quarter-sample phase offset removes this. The harmonic caveat is real: a
  square-wave reference responds to the 3rd, 5th and 7th harmonic frequencies at −9.5, −14.0 and
  −16.9 dBc [derived], and in a sampled complex baseband these fall inside the band. A 1-bit
  reference is a screening tool at most. Combined with 1-bit samples (1.96 dB) the total is about
  2.9 dB.
- **Multiplierless approximations.** Approximate DFTs with entries from small integer sets exist for
  beamforming hardware: an 8-point version in 26 additions [Suarez14, abstract], a 16-point version
  [Ariyarathna19, abstract], and 1,024-point radix-32 versions that lose at most 0.9 dB of SNR gain
  [Madanayake20, abstract]. The integer FFT uses lifting steps with shifts and adds
  [Oraintara02, abstract]; CORDIC rotations replace multipliers in FFT hardware [Despain74,
  abstract]. These remove multipliers, which matter in ASIC and FPGA area. On Tensor Cores, VNNI and
  AMX the multiplier is free; the levers are operand width, memory traffic and accumulator exactness.
  The 0.9 dB class of loss equals that of the sign reference above.

**Input quantization** [derived; Gaussian input, Nyquist sampling, low SNR; loss in dB versus a gain
error relative to the optimum loading; Appendix B]. The optimum values reproduce the published
efficiencies 0.6366, 0.8098, 0.8825 and 0.9626 for 2, 3, 4 and 8 levels with thresholds 0.612σ
(3-level) and 0.996σ (4-level) [EVLA88, verified; TES07; Cooper70; VanVleckMiddleton66].

| Quantizer | Optimum loading | At optimum | −12 dB | −6 dB | −3 dB | +3 dB | +6 dB | +12 dB |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1-bit sign | any | 1.96 | 1.96 | 1.96 | 1.96 | 1.96 | 1.96 | 1.96 |
| 3-level `{0, ±1}` | threshold 0.61σ | 0.92 | 9.38 | 1.89 | 1.08 | 1.01 | 1.17 | 1.50 |
| 2-bit `{±1, ±3}` | threshold 1.0σ | 0.55 | 1.96 | 1.26 | 0.73 | 0.64 | 0.85 | 1.29 |
| 3-bit, 8 levels | step 0.57σ | 0.17 | 1.48 | 0.45 | 0.23 | 0.26 | 0.49 | 1.03 |
| 4-bit, 16 levels | step 0.34σ | 0.050 | 0.61 | 0.16 | 0.08 | 0.10 | 0.27 | 0.83 |
| INT4 round and clip, −8…7 | 2.8 LSB per σ | 0.056 | 0.68 | 0.18 | 0.09 | 0.11 | 0.28 | 0.84 |
| E2M1 nearest value | 2.1 units per σ | 0.055 | 0.32 | 0.10 | 0.07 | 0.08 | 0.21 | 0.73 |
| INT8 round and clip | 32 LSB per σ | 0.000 | 0.006 | 0.001 | 0.001 | 0.004 | 0.05 | 0.45 |

- Four bits tolerate ±6 dB of AGC error within 0.3 dB. The E2M1 codebook is as good as INT4 at the
  optimum and better when under-driven, because of its half-unit steps near zero. Three levels
  collapse when under-driven.
- **With strong interference the Gaussian table does not apply.** Classical limiter theory gives a
  weak signal about 6 dB of extra suppression beside a much stronger constant-envelope signal
  [Jones63, abstract; the 6 dB figure from memory; Davenport53]. A 2-bit converter with adaptive
  thresholds and weighting turns CW interference into a gain: its error rate "generally surpasses
  that of an ideal analog" correlator in CW interference and stays within 0.6 dB of it in Gaussian
  noise [Amoroso83, abstract; AmorosoBricker86, abstract]. Hegarty's model covers uniform quantizers
  with any number of levels in Gaussian noise plus Gaussian non-white interference [Hegarty11,
  abstract]; joint filtering and quantization optimisation for 1–3 bits is in [Curran10, abstract].
  Pulsar practice treats non-stationary inputs and level setting [JenetAnderson98]; Van Vleck
  corrections for multi-level correlators are in [Kogan98]. The repository's own interferer table
  (NVFP4 3.4 dB and INT4 1.3 dB at 20 dB interference-to-noise, INT8 0.4 dB at 40 dB) stands and is
  not recomputed here ([practice review](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md)).

---

## 3. Measured throughput on current hardware

### 3.1 CPU

**FFT, single precision, complex, batched, one core** [measured: this review, Ryzen 5 PRO 5650U
(Zen 3, AVX2), FFTW 3.3.10 Debian build with SSE2/AVX codelets and no AVX2/FMA codelets,
`FFTW_PATIENT`, `plan_many`, about 1 MiB of input per batch, thread CPU time, best of nine 80 ms
windows, host under unrelated load average 19–27 with the sibling hardware thread busy and a
reported 2.82 GHz clock]:

| `N` | 64 | 128 | 256 | 512 | 1,024 | 2,048 | 4,096 | 8,192 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Time per transform | 78 ns | 158 ns | 342 ns | 814 ns | 1.83 µs | 3.95 µs | 9.0 µs | 21.7 µs |
| `5N·log₂N` GFLOP/s | 24.7 | 28.4 | 30.0 | 28.3 | 28.0 | 28.5 | 27.3 | 24.5 |
| Points/s (millions) | 822 | 811 | 749 | 629 | 560 | 518 | 455 | 377 |

- These are pessimistic absolute values: the host was oversubscribed and FFTW lacked AVX2/FMA
  codelets. An idle core with an AVX2 build should be faster by a factor that was not measured;
  ratios between kernels in the same run are more reliable than the absolute times.
- **Published per-size numbers could not be extracted.** The current comparative suites publish
  charts, not tables: the pffft benchmark page (November 2026, nine machines; pffft's geometric-mean
  speed against FFTW in MEASURE mode ranges from 0.71 on a Xeon 6975P-C to 1.30 on a Neoverse N2)
  [pffft-bench, verified, charts only]; the KFR suite (single-threaded, MFLOPS, no results in the
  repository) [kfr-bench, verified]; an MKL/FFTW/pocketfft/DUCC harness on Rome, Skylake and Icelake
  [fft-bench, verified, charts only]; FFTW's own pages (metric definition, no recent machines)
  [fftw-speed, verified]. Intel's FFT guidance gives no figures [Intel-FFT-guide, verified]. A forum
  report of 32.6 GFLOP/s for MKL at `N` = 1,024 on a 3.1 GHz Sandy Bridge is a search snippet only
  [unverified]. The idle-host AVX2 and AVX-512 per-size table therefore remains an open measurement
  (experiment E1).

**Integer dot products for a CPU matrix DFT.**

| Instruction path | int8 MAC per cycle per core | cMAC per cycle | Status |
| --- | ---: | ---: | --- |
| AVX2 `VPMADDUBSW` + `VPMADDWD` (Zen 3, kernel above) | ≈ 14 achieved at 2.82 GHz, shared bin set | 3.6 | [measured: this review, loaded host] |
| AVX-512 VNNI `VPDPBUSD zmm`, Zen 4, Ice/Tiger/Alder Lake | 64 | 16 | [measured: uops.info instruction throughput 1.0 cycle] |
| AVX-512 VNNI, Zen 5, Cascade Lake, Emerald Rapids | 128 | 32 | [measured: uops.info, 0.5 cycle] |
| AMX `TDPBSSD` (Emerald Rapids) | 1,024 | 256 | [measured: uops.info, 16 cycles per 16,384-MAC tile; vendor: 2,048 "operations" per cycle versus 256, and a 7.8× microbenchmark at 32×512 by 512×32, Intel-AMX] |

- Instruction throughput is an upper bound; no measured oneDNN, OpenBLAS or BLIS int8 GEMM rate per
  core was found. OpenBLAS and BLIS have no int8 GEMM; oneDNN's `gemm_s8u8s32` is the library route
  [from memory].
- The measured AVX2 kernel reached 6.8–10.5 cMAC/ns against 28 GFLOP/s-equivalent for the FFT. In
  flops per second the int8 direct DFT ran at 54–84 GFLOP/s-equivalent, 2–3× the FFT's arithmetic
  rate, which cannot offset an 80× operation-count gap at 448 bins.

### 3.2 GPU

**Batched small FFTs are at the memory-bandwidth ceiling.**

- TurboFFT uses one kernel launch for `N` ≤ 2¹³ and "achieves 90% peak memory bandwidth" on an A100,
  with bandwidth defined as twice the problem size over the execution time. It is faster than cuFFT
  for problems within 2 MB and comparable or up to 3× faster elsewhere, so cuFFT itself is below the
  ceiling on small problems [measured: TurboFFT, A100, read in HTML].
- cuFFT complex transforms up to 16,384 points are memory-bound and use at least 87% of memory
  bandwidth [measured: Merry23, RTX 3070 Ti].
- VkFFT throughput "is similar to cuFFT up to N=1024" for batched single-precision transforms
  [measured: pyvkfft-perf, V100, plots only]; VkFFT's design and precision are in [VkFFT, abstract].
- **Repository anchor**: 512 windows of 2,048 points plus a gather in 10.8 µs, 9.7×10¹⁰ points/s, or
  1.55 TB/s of one-read-one-write traffic [measured: projection probe, full RTX PRO 6000 Blackwell
  Server]. Small batches hit a launch floor of 5.6–5.9 µs per call [same source].
- No published cuFFT figures for RTX 4090 or 5090 were found. From the 87–90% figures and vendor
  bandwidths of 1,008 and 1,792 GB/s: about 5.5×10¹⁰ points/s on an RTX 4090 and 1.0×10¹¹ on an
  RTX 5090 or PRO 6000, or 9 ns and 5 ns per 512-point transform in large batches [derived]. The
  repository measurement agrees with the second figure.

**Tensor-core and matrix-form transforms, 2021–2026.**

| Work | What it shows | Limit it reports |
| --- | --- | --- |
| tcFFT (2021), FlashFFTConv (2023), HadaCore (2024), Matsuoka (2026) | Already summarised in the [Opus-5.5 review §2–3](../../Opus-5.5/reviews/review-tensor-core-dsp.md) [identifiers verified] | 1.0–3× stand-alone; gains need fusion; integer epilogue wall |
| M³XU, SC24 | A proposed matrix unit with native FP32-complex mode, in GPGPU-Sim, reaches at most 1.99× and on average 1.52× over cuFFT. tcFFT extended to TF32 Tensor Cores "does not improve performance over" cuFFT [simulation: M3XU, full text read] | Existing Tensor Cores need 4× the operations per complex GEMM; even an ideal complex unit gains under 2× |
| TurboFNO, 2025 | Fused FFT–CGEMM–iFFT kernel with built-in truncation and zero-padding, on CUDA cores only: average 44% and up to 250% over PyTorch in 1-D; pruning removes 25–67.5% of butterflies [measured: TurboFNO, A100, HTML read] | The authors attribute the gain to fewer memory transactions; partial fusion gives 1–2% |
| TensorFHE, HPCA 2023 | Number-theoretic transform as UINT8 GEMM on Tensor Cores: 913 KOPS on A100, 2.61× over prior GPU work [measured: TensorFHE, abstract] | Wide integers split into 8-bit segments |
| WarpDrive, HPCA 2025 | In that design the GEMM is "only about 41% of the overall NTT"; memory-related pipeline stalls dominate. Fusing and using Tensor and CUDA cores concurrently gives 1,218 KOPS, 13.4× [measured: WarpDrive, A100, full text read] | Split/merge on CUDA cores; off-chip traffic |
| FHECore, 2026 | Reports decomposition and reassembly at "nearly 40%" of NTT latency in the tensor approach and proposes a modular matrix unit [simulation/proposal: FHECore, HTML read in part] | Integer epilogue |
| NVIDIA patent US 12,475,189 | Search snippet: DFT implementations on MMA circuitry "surpass" legacy ones for "difficult" (non-power-of-two) sizes, and a TF32 version beats legacy cuFFT by at least 10% on 28–42% of supported sizes | **[unverified]**: the published PDF is image-only and the text was not read |

  No work using INT8, FP8 or FP4 Tensor operands for a DFT or tone correlator with an accuracy study
  was found. The integer-transform precedents (TensorFHE, WarpDrive) are the closest, and their
  lesson is that the matrix product is under half the time.

**GEMM rates relevant to a matrix DFT** (dense, 2 ops per MAC). Vendor peaks and the measured
instruction-level results are tabulated in the
[Opus-5.5 hardware review §1a and §2](../../Opus-5.5/reviews/review-gpu-hardware.md) and are not
repeated. Additions and re-checks:

- Sustained library-level NVFP4 GEMM on an RTX PRO 6000 Blackwell Server Edition: 1,476 TFLOP/s at
  8k (73% of the 2,015 vendor peak) and 1,666 TFLOP/s at 16k (83%); the SM clock fell to 2.15 GHz at
  large shapes; the 2k shape lost throughput to wave quantization (256 tiles on 188 SMs)
  [measured: Colfax26, verified]. The repository's own 8192×8192×4096 run reached 1,375 TFLOP/s and a
  288×96×3072 shape only 11.7 TFLOP/s ([execution evidence](../../Sol-6.2/tensor-execution-evidence.md)).
- cuBLASLt integer support is `CUDA_R_8I` inputs with `CUDA_R_32I` output under
  `CUBLAS_COMPUTE_32I`, in the "TN" format with dimensions in multiples of 4; block-scaled
  `CUDA_R_4F_E2M1` requires `CUBLASLT_MATMUL_MATRIX_SCALE_VEC16_UE4M3`. Complex int8 inputs
  (`CUDA_C_8I`) are accepted only with FP32 complex compute and output [vendor: cuBLAS-doc 13.5,
  verified]. A Tensor-path integer complex DFT therefore uses the real-expanded form
  `[B × 2N]·[2N × 2K]`.

**Does a consumer or workstation NVIDIA GPU expose INT4 Tensor operands in a public API today?**

- **Ada (compute capability 8.9: RTX 4090, 4060 Ti): yes, at PTX and CUTLASS level.** The CUDA
  Programming Guide v13.5 (updated 31 October 2026), Table 36, marks INT4 for capabilities 7.5, 8.0,
  8.6, 8.7 and 8.9 [CUDA-T36, verified by parsing the table cells]. PTX ISA 9.5 (updated 2 November
  2026) defines `.u4/.s4` `mma` operations with shapes `.m8n8k32` ("sm_75 or higher") and
  `.m16n8k32`, `.m16n8k64` ("requires sm_80 or higher") [PTX-ISA, verified]. CUTLASS lists
  `s4 * s4 + s32` TensorOp kernels for SM75 and SM80 [CUTLASS-func, verified]; it has no separate
  SM89 entry, and native execution of these on sm_89 rests on the SASS evidence quoted in the
  [hardware review §2](../../Opus-5.5/reviews/review-gpu-hardware.md). No measured INT4 throughput
  on Ada was found there or here.
- **Blackwell (12.x: RTX 5090, RTX PRO 6000): no.** Table 36 leaves INT4 blank for 9.0, 10.0, 10.3,
  10.7, 11.0 and 12.x, and lists FP4, FP6, FP8 and INT8 for 12.x [CUDA-T36]. The PTX target note is
  language acceptance, not hardware support ([hardware review §2](../../Opus-5.5/reviews/review-gpu-hardware.md)).
- **No library exposes INT4 operands on any generation.** cuBLAS/cuBLASLt define no 4-bit integer
  data type [cuBLAS-doc]. TensorRT lists INT4 as "signed 4-bit integer, weight-only quantization"
  [TensorRT-quant, verified]. CUTLASS lists no SM90, SM100 or SM120 integer-4-bit kernel.

**Roofline for a one-stage matrix DFT** [derived; vendor dense peaks; a complex output point costs
`8N` ops; memory time per point at 8 or 16 bytes of traffic]:

| GPU | Operands | `N*` at 8 B/point | `N*` at 16 B/point |
| --- | --- | ---: | ---: |
| RTX 4090 | FP16, FP32 accumulate / INT8 / INT4 | 164 / 655 / 1,311 | 328 / 1,311 / 2,621 |
| RTX 5090 | FP16, FP32 accumulate / INT8 / FP4 | 117 / 468 / 935 | 234 / 935 / 1,871 |
| RTX PRO 6000 Blackwell | FP16 / INT8 / FP4 | 281 / 562 / 1,125 | 562 / 1,125 / 2,249 |

- `N*` is the input length at which the arithmetic of a direct DFT takes as long as moving the data
  once. Below it the matrix form is bandwidth-bound like the FFT; above it the matrix form is slower.
  The [Sol-6.2 table](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md) is the RTX 5090 FP16
  row at 8 B/point.
- At `N` = 512, FP16 with FP32 accumulation is 2–4× outside on GeForce parts. INT8 is at the
  boundary, and FP4 on Blackwell and INT4 on Ada are inside by about 2×. These are vendor peaks;
  sustained library rates are 50–85% of peak at large shapes and far lower at small ones.
- **Re-reading the projection probe** [derived from its measurement]. The NVFP4 route multiplied
  2,048-sample rows of which 1,536 samples were zero, 8·2,048·1,409 = 23.1 Mflop per window, 205×
  the nominal FFT arithmetic, and was 4.1–5.1× slower including FP32-to-NVFP4 packing. Its effective
  rate was about 270 TFLOP/s. With the 512 active samples only, the work is 51× the FFT's, and if
  time scaled with it the route would take about 11 µs against 10.8 µs for cuFFT. The probe README
  already says a 512-sample matrix "can change the crossover". Both routes sit near the 5–6 µs
  launch floor at these batch sizes, so the test needs larger batches to discriminate.
- **Few outputs, INT8 samples, shared bin set** [derived]. For one symbol index of a batch of `B`
  messages that share the key schedule: `X[B × 1024 INT8]·F_s[1024 × 128 INT8]` moves 1,024 bytes in
  and 512 bytes out per message-symbol, 0.86 ns at 1,792 GB/s, and needs 262,144 ops, 0.31 ns at the
  838 TOPS INT8 peak. cuFFT plus gather moves 8,192 bytes of complex64 per transform, 4.6–5.1 ns. The
  matrix route is predicted 3–6× faster. A custom FFT kernel that read INT8 and wrote only the
  gathered bins would have the same traffic, so the advantage is over the library route, not over a
  fused FFT. If each message has its own bin set there is no shared operand: the product degenerates
  to about 4 ops per byte and loses.

### 3.3 Closest large-scale practice

| System | Dominant kernels | Precision | Tensor Cores used effectively? |
| --- | --- | --- | --- |
| MeerKAT `katgpucbf` F-engine | Filter-bank FIR, cuFFT, fused post-processing; all memory-bound; throughput set by PCIe (four antennas at 2 GS/s) or one CPU core (6.2 GS/s) [measured: Merry23, RTX 3070 Ti] | 10-bit input, FP32 FIR and FFT, 8-bit output; FP16 FFT rejected | No (F-engine). The X-engine uses the Tensor-Core Correlator with INT8 ([Opus-5.5 review §1.1](../../Opus-5.5/reviews/review-tensor-core-dsp.md)) |
| xGPU [xGPU], CHIME [Klages15, CHIME22] | Cross-multiply and accumulate | FP32 on CUDA cores; CHIME packs 4+4-bit samples into 32-bit operations | Pre-date Tensor Cores |
| Tensor-Core Correlator and Beamformer [TCC, TCBF] | Complex GEMM (correlation, beamforming) | FP16, INT8, 1-bit | Yes: 5–10× over xGPU; 50–85% of peak at shapes of a few thousand per side; small shapes memory-bound (prior reviews) |
| MWAX [MWAX, abstract] | FX correlator on 24 GPU servers, 1.28 MHz coarse channels | from memory: cuFFT plus xGPU, FP32 | Not reported |
| Bifrost [Bifrost, metadata only] | Stream-processing framework with FFT and correlation blocks | from memory: 8-bit inputs, FP32 cuFFT | Not reported |
| GNSS software receivers | Tracking correlators gain most (about 50×); acquisition about 3.3×, limited by host–device transfer ([practice review](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md); [Hobiger10], [Xu17], metadata) | Low-bit samples, FP32 arithmetic | None found |
| Breakthrough Listen drift searches | turboSETI Taylor tree; hyperseti brute-force dedoppler in FP32 with `N_drift × N_chan × 4` bytes; "disk I/O and peak finding" are the bottlenecks [hyperseti, verified; Cruz22; Margot21] | FP32 | None found |

  In every system the transform or channelizer runs in FP32 on CUDA cores and is memory-bound.
  Tensor Cores are used effectively only for the dense cross-product stage, and only at large shapes.

---

## 4. Matrix Modem shapes: operation counts and predicted winners

Assumptions: 3,200 Hz complex baseband, `N` = 512, 448 usable bins, 64 keyed tones per symbol, a
256-symbol (40.96 s) dwell; one tone per symbol; tones on exact bins. CPU times scale the 5650U
measurements of §3.1 (loaded, one core). GPU figures are roofline predictions from §3.2 unless
marked measured. "Live" means one stream arriving at 6.25 symbols/s; "batch" means queued or
simulated work in large fixed shapes.

### 4.1 Counts

| Workload | FFT route | Direct or matrix route | Ratio |
| --- | --- | --- | ---: |
| (i) one stream, known timing, 448 bins | 23,040 flops; 0.81 µs [measured basis] | 1.84 Mflop; 22.7–32.2 µs int8, 33–48 µs FP32 [measured basis] | 80× flops; 28–59× time |
| (ii) 8 timing offsets × 4× oversampling (1,792 fine bins) | 8 × 2,048-point FFT = 0.90 Mflop; 32 µs [measured basis] | Direct: 58.7 Mflop. Hopping block update at `N/8`: 7.3 Mflop | 65×; 8.1× |
| (iii) 64 keyed bins, different per symbol | 23,040 + gather; 0.85 µs | Goertzel 0.20 Mflop; direct 0.26 Mflop; 4.4 µs int8, 6.8 µs FP32 [measured basis] | 8.5–11× flops; 5.4–8.3× time |
| (iv) 64 drift rates × 8 spread widths, 256 symbols, per timing offset | 256 FFTs = 28.8 Mflop; 8 spread FIRs on fine-bin energies ≈ 59 Mflop; drift gather-accumulate ≈ 59 M adds; total ≈ 1.5×10⁸ | Time-domain bank: 448 tones × 8 fractional offsets × about 12 taper rows × 512 samples × 8 = 1.76×10⁸ per symbol, 4.5×10¹⁰ per dwell | 300× |
| (v) batch simulation, per message | 131,072 samples; 262,144 normal variates; 256 FFTs = 5.9 Mflop (0.21 ms of one core) | Symbol domain: 229,376 variates for all 448 bins, or 32,768 for the 64 keyed bins | noise generation dominates |

Notes on the counts [derived]:
- (ii) Frequency oversampling is needed only when the frequency error exceeds about 0.1 bin; with a
  disciplined reference the count drops fourfold. Eight-tap interpolation from a 1,024-point FFT
  costs more than 4× zero-padding when all 1,792 fine bins are wanted (1.33 Mflop against 0.90).
- (iv) Intra-symbol drift is ignored because `c` < 0.03 below 1 Hz/s (§2.3); drift is a fractional
  bin shift per symbol, quantized to the 4× grid (0.22 dB) or the 8× grid (0.056 dB). The spread FIR
  is the truncated weighted periodogram; the exact Toeplitz form of §2.3 uses all `2N` samples.
- (v) A time-domain simulator spends more on Gaussian noise than on FFTs: at an assumed 5 ns per
  normal variate on one core [heuristic] the noise is 1.3 ms per message against 0.21 ms for the
  transforms.

### 4.2 Predicted winner

| Workload | Live, one stream | Batch | Basis |
| --- | --- | --- | --- |
| (i) | **CPU FFT.** Real-time factor about 2×10⁵ on one 5650U core | GPU cuFFT (about 5 ns per symbol on Blackwell); matrix DFT at parity only for INT8/FP4 | [measured basis] live; [derived] batch |
| (ii) | **CPU FFT**, zero-padded or four mixed short FFTs; 32 µs per symbol period, real-time factor about 5,000 | **cuFFT ≈ INT8 matrix DFT** (predicted 120–180 ns against 160 ns per symbol period); keep cuFFT because it is exact | [measured basis] live; [derived] batch |
| (iii) | **CPU FFT + gather** on AVX2 (5× over int8 direct). CPU direct int8 DFT worth testing on AVX-512 VNNI and AMX | **GPU INT8 matrix DFT** when a bin set is shared across the batch (predicted 3–6× over cuFFT + gather); otherwise cuFFT + gather | [measured: this review] live; [derived] batch |
| (iv) | **CPU FFT, then post-FFT weights and gathers.** About 1.2×10⁹ simple operations per dwell for 8 offsets, a fraction of a second on one core [heuristic] | **GPU fused matrix follow-up on FFT output** (weights, drift accumulation, reduction in one kernel). A time-domain generalised DFT is 300× the arithmetic and competes only if fused at Tensor rates | [derived] |
| (v) | — | **Symbol-domain statistics with no transform** where exact (§4.3); otherwise cuFFT or CPU FFT, since noise generation dominates; INT8 matrix DFT only when time-domain quantization must be simulated for keyed bins | [derived] |

The complete front end for (ii) uses 2×10⁻⁴ of one 5650U core. In (iv) the transforms are about 20%
of the arithmetic (28.8 of 147 Mflop per timing offset); with a codeword bank of 10¹⁰–10¹² operations
per dwell they are under 1%. The receiver's computational ceiling is set by what follows the
transform.

### 4.3 When a symbol-domain simulation is exact

A simulator can draw the receiver's bin statistics directly instead of forming time samples. The
`N` on-grid bins of an `N`-sample window are an invertible representation of the samples, so drawing
all `N` bins loses nothing; drawing only the bins a detector reads is exact for that detector.

**Exact** under these conditions [derived]:
- The receive chain is linear up to the statistic. White Gaussian noise then gives independent
  `CN(0, N·σ²)` values in the bins of a rectangular `N`-sample window.
- An integer number of samples per symbol and tones on exact bins of that window, with rectangular
  keying. Each tone then completes an integer number of cycles per symbol, so phase continuity at
  symbol boundaries is automatic and the signal occupies exactly one bin.
- A channel that is constant over each symbol (one complex gain per symbol and path). The signal bin
  is the gain times the tone amplitude; no inter-bin leakage.
- Timing offset, multipath delay and frequency offset remain exact with a block-constant channel:
  a window that straddles two symbols, or a tone at a fractional bin, contributes through closed-form
  Dirichlet kernels to every bin read.
- Zero-padded or interpolated fine bins are exact linear functions of the `N` on-grid bins, so their
  noise correlation is reproduced by interpolating drawn on-grid values.

**Approximate, with controllable error**:
- Fading within a symbol (Doppler spread comparable to `1/T`). The bin outputs for a tone are jointly
  Gaussian with a covariance fixed by the Doppler spectrum and window, and correlated across symbols.
  Sampling the fading process at a few times its bandwidth and projecting with a small matrix
  reproduces this to the interpolation error.
- Smoothed frequency transitions between symbols, coloured noise and narrowband interferers: closed
  forms or tables exist, but each adds leakage terms.

**Not representable** without time samples:
- Any nonlinearity before the transform: ADC or operand quantization of raw samples, clipping, AGC
  dynamics, impulse blanking. These must be simulated in the time domain or modelled as added noise
  and spurs. This is the case that matters for qualifying 4-bit arithmetic before Helper/DATA
  isolation.

For energy detectors a noise-only bin's power is exponentially distributed, so one uniform variate
replaces two normal variates [derived].

---

## 5. Does 2022–2026 literature contradict "an unfused Tensor Core DFT cannot beat cuFFT"?

**No direct contradiction was found; the claim should be narrowed.**

- **Support.** M³XU (2024): a TF32 Tensor Core tcFFT does not beat cuFFT, and a hypothetical native
  complex-FP32 matrix unit gains 1.5× on average. TurboFFT (2024–2025) and Merry (2023): CUDA-core
  FFTs already run at 87–90% of memory bandwidth for `N` ≤ 8,192–16,384. WarpDrive (2025): in a
  Tensor Core integer transform the GEMM is a minority of the time and fusion gives the gain.
  TurboFNO (2025): fusion gains come from fewer memory transactions, on CUDA cores. HadaCore (2024)
  and Matsuoka (2026) are in the prior review.
- **Partial counter-evidence.** tcFFT (2021) beat cuFFT's FP16 kernels by 1.24× on A100, because the
  FP16 baseline was not at the bandwidth ceiling. The NVIDIA patent snippet claims wins on
  non-power-of-two sizes, where cuFFT falls back to slower algorithms [unverified]. Neither applies
  to a 512- or 2,048-point FP32 transform.
- **Where the claim is weaker than stated** [derived]. The argument assumes FP16-class Tensor rates.
  At INT8 and FP4 peaks on Blackwell the one-stage arithmetic for `N` = 512 fits within the memory
  time (§3.2), so the roofline predicts parity for the full grid and an advantage when inputs are
  INT8 and outputs are few. No measurement exists for either case; the one repository measurement
  carried a 4× zero-column handicap.
- **Wording to use.** "An unfused FP16 or TF32 Tensor Core FFT or DFT does not beat cuFFT, which is
  bandwidth-bound. An unfused INT8 or FP4 one-stage DFT of 512 points is predicted to tie it on
  Blackwell and is untested. A matrix DFT is predicted to win only with INT8 operands, few outputs
  and a shared bin set, or when fused with the follow-up."

---

## 6. Contradictions and corrections to prior documents

1. **Break-even table in the Sol-6.2 format note §2.** It is the RTX 5090 FP16/FP32-accumulate case.
   For INT8 and FP4 the direct form fits to `N` ≈ 470–1,125, so "use factored small-radix GEMM
   stages, never one large DFT matrix" is not supported at `N` = 512; factoring adds a second memory
   pass. The advice remains right for FP16 operands and for `N` ≥ 2,048.
2. **"Repository evidence agrees: cuFFT 4.1–5.1× faster" (same note).** True as measured, but the
   comparison charged the matrix route with 4× zero columns and packing. It does not show that a
   512-sample matrix projection is slower than cuFFT.
3. **"FP4 DFT or phase matrices have an error floor around −20 to −25 dB, which limits coherent
   gain on strong specular lines" (same note §5).** The correlation loss of a 4-bit reference is
   0.013–0.04 dB. The floor is deterministic leakage from other frequencies, concentrated in a few
   harmonically related bins (worst −25 to −29 dBc). The precaution it motivates, 4-bit arithmetic
   only after isolation or without strong interferers, is unchanged.
4. **Sliding projection as a general frontend (reception results, bottlenecks note).** The 3.4×
   stage win belongs to 34 bins at 4-sample hops. On the Matrix `N/8` timing grid with all bins a
   sliding update is predicted 8–10× slower than recomputed FFTs.
5. **"Without overlap and oversampling … twofold zero-padding" (format note §3).** Complete with:
   4× padding 0.22 dB; 8-tap interpolation 0.23 dB without padding; and under 0.04 dB for on-bin
   tones once the frequency error is below 0.05 bin.
6. **Opus-5.5 tensor review, "INT8/INT4 FFT: no accuracy study found."** Still true in the
   literature. §2.5 supplies derived values and shows why the distinction between an INT8 FFT
   (−9 dB) and an INT8 matrix DFT (−50 dBc) is two orders of magnitude.

No contradiction was found with the INT4 availability findings of the Opus-5.5 hardware review; the
vendor tables read the same today.

---

## 7. Design consequences (testable statements)

1. On every representative receiver CPU the FFT front end for one stream with 8 timing offsets and
   4× oversampling uses under 0.1% of one core. A section timer on a challenging broad search should
   show the transform at about 20% of complete-receiver arithmetic with a drift × spread bank alone
   and below a few percent once a codeword or keyed-pattern bank is searched. If so, no transform
   substitution can raise the automatic-mode ceiling materially.
2. On AVX2 targets a direct DFT is faster than a 512-point FFT only for `K` ≤ 16 bins (int8) or
   `K` ≤ 11 (FP32). Keyed 64-tone extraction should gather from the FFT.
3. On AVX-512 VNNI hosts the int8 crossover is predicted at 20–60 bins; on AMX hosts with at least
   16 rows sharing a bin set, at all 448. Both are unmeasured.
4. A sliding or hopping update beats recomputed FFTs only when `L < 5·N_fft·log₂N_fft/(8K)`. For the
   Matrix timing grid this means at most about 44 on-grid bins, or 220 fine bins against a
   2,048-point FFT, at hop `N/8`.
5. An integer modulated sliding DFT with s32 accumulation is bit-identical to a direct integer DFT
   after any number of hops.
6. On a full RTX PRO 6000 Blackwell or RTX 5090, an unfused INT8 or NVFP4 matrix DFT of 512 active
   samples onto 1,409–1,792 fine bins runs within 0.5–2× of batched 2,048-point cuFFT at batch sizes
   of 4,096 and above. With FP16 operands it is 2–4× slower on GeForce parts.
7. For a bin set shared across a batch, an INT8 matrix DFT of 64 keyed tones is 3–6× faster than
   cuFFT plus gather.
8. INT8 references (−50 dBc total, −52 dBc worst spur) add less than 0.1 dB of loss with a single
   on-grid interferer up to about 35 dB above the per-bin noise even in the worst bin pair. INT4 and
   E2M1 references (−29 and −25 dBc worst spur) reach that limit at about 12 and 9 dB, so they belong
   after Helper/DATA isolation. Sign and ternary references lose 0.91 and 0.35–0.39 dB and admit
   −9.5 to −13 dBc harmonic responses; they are usable for screening only.
9. Four-bit samples (INT4 or E2M1) lose 0.05–0.06 dB at the optimum loading and under 0.3 dB over
   ±6 dB of gain error in Gaussian noise. E2M1 is the more tolerant of the two when under-driven.
10. At 160 ms symbols no drift basis is needed below 20 Hz/s. Drift, fractional offsets and spread
    weights are post-FFT operations on a 2×–4× grid; DPSS projections are matrices on about
    `B_D·T + 8` complex bins.
11. A rectangular window on exact bins is the matched filter. Any window or filter bank costs
    1.3–2.4 dB on-bin and is justified only by measured interference.
12. A symbol-domain simulator that follows §4.3 reproduces the time-domain simulator's decision
    statistics exactly for linear front ends, and cannot qualify pre-isolation quantization.

---

## 8. Experiments that would settle it

Each is small and bounded, uses the compiled C++ stack or a standalone C++/CUDA probe, and has a
predeclared decision rule. None is a capacity test.

| # | Benchmark | Shapes | Metric | Decision rule |
| --- | --- | --- | --- | --- |
| E1 | CPU FFT versus int8 and FP32 direct DFT, idle host, FFTW built with AVX2/FMA and the project's own FFT | `N` = 512; `K` ∈ {4, 8, 16, 32, 64, 128, 448}; distinct and shared bin sets; one core and `W = 2·H` workers; 5650U and 8840U | ns per symbol; cMAC/ns | Use a direct path for keyed extraction on a target only if it is ≥ 1.2× faster at `K` = 64 with identical integer outputs |
| E2 | Same kernel with `VPDPBUSD` (AVX-512 VNNI) and, on a rented Xeon, AMX tiles | As E1; 16-row shared sets for AMX | ns per symbol | Same rule; record `K*` per instruction set |
| E3 | GPU full grid: cuBLASLt INT8 `X[B×1024]·F[1024×3584]` → s32, and NVFP4 with 512 active samples, versus `cufftExecC2C` at 2,048 points | `B` ∈ {512, 4096, 32768}; full RTX PRO 6000 Blackwell; RTX 5090 or 4090 if available | CUDA-event µs per batch including packing; GPM Tensor activity and DRAM utilisation; relative RMS against FP64 | Keep cuFFT unless the matrix route is ≥ 1.5× faster at `B` ≥ 4,096 and passes E6 |
| E4 | GPU keyed bins: `X[65536×1024 INT8]·F_s[1024×128 INT8]` versus cuFFT 512 + gather | One symbol index; shared bin set; then per-message bin sets as a negative control | ns per message-symbol | Adopt for batch work with time-domain quantization if ≥ 2× faster; otherwise use the symbol domain |
| E5 | Fused versus unfused: transform or matrix DFT + squared magnitude + spread weights + drift accumulation + top-k in one kernel, against cuFFT and separate kernels | Workload (iv): 256 symbols × 1,792 fine bins × 64 drifts × 8 widths | Useful hypotheses/s; bytes of DRAM traffic per hypothesis | Fusion is worth its engineering cost at ≥ 2× on the complete stage |
| E6 | Precision sensitivity on the complete receiver: FP32 FFT, INT8-coefficient DFT, INT4- and E2M1-coefficient DFT, each with FP32, INT8 and 4-bit samples | 50% decode point; interferer 0, 20 and 40 dB above noise, on the worst harmonic bin and on a random bin; before and after isolation; AGC error ±6 dB | dB shift of the 50% point; false accepts | Proportional rule of the [precision guide](../../../../docs/gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling); flag any path whose worst-bin loss exceeds its random-bin loss by more than 1 dB |
| E7 | Toeplitz identity and estimator-correlator loss (CPU calculation) | `N` = 64–512; `B_D·T` ∈ {0.3, 1, 3, 5}; per-branch SNR −5 to +10 dB | Numerical identity to rounding; deflection and error-probability loss of truncated weighted periodogram against the exact estimator-correlator | Use DPSS bin matrices only where the truncated periodogram loses more than 0.2 dB |
| E8 | Symbol-domain versus time-domain simulator | Flat, block-fading, timing- and frequency-offset cases; 10⁴ messages | Two-sample test on decision metrics; decode rates with binomial intervals | Equivalence within sampling error qualifies the symbol-domain path for those cases |
| E9 | Integer hopping DFT | Hops 64, 16, 4; `K` ∈ {64, 192, 448}; 10⁷ hops | Bit equality with direct DFT; ns per hop against FFT | Confirms statement 5 and the break-even formula |

E1 was run once for this review under load (Appendix C); it needs an idle-host repeat before any
dispatch decision.

---

## 9. Weaknesses and unknowns

- **The CPU measurement is indicative.** One host, one run, load average 19–27 from unrelated jobs,
  sibling hardware thread busy, FFTW without AVX2/FMA codelets, an untuned int8 kernel. Ratios are
  more trustworthy than absolute times. The crossover range of 7–16 bins could move by tens of
  percent on an idle host.
- **No published per-size CPU FFT tables were retrieved** for AVX2 or AVX-512; the benchmark sites
  publish charts. No measured int8 GEMM rate per core was found. The AVX-512 VNNI and AMX
  crossovers are heuristic.
- **All GPU matrix-DFT statements are roofline predictions** from vendor peaks, one repository
  measurement and literature bandwidth fractions. Small-shape GEMM efficiency, packing cost and
  launch overhead can each change the answer by more than 2×. No cuFFT data for RTX 4090 or 5090
  were found.
- **Sources read only as abstracts or metadata**: Jacobsen and Lyons, Duda, Park and Ko, Wang et al.,
  Park, Kim and Chang, Sorensen and Burrus, Welch, Oppenheim and Weinstein, Tran-Thong and Liu,
  Amoroso, Hegarty, Curran, Tadokoro and Higuchi, Karnik et al., and the approximate-DFT papers.
  Operation counts for the sliding and hopping algorithms were derived here, not copied from the
  articles. The per-stage-scaled fixed-point FFT formula and the 6 dB limiter suppression are from
  memory.
- **Coefficient-quantization results assume on-grid interferers** and one rounding rule. Off-grid
  interferers spread over the same harmonic families. The E2M1 result depends on the scale chosen.
- **Input-quantization results assume Gaussian, Nyquist-sampled, noise-dominated input.** HF noise
  is often impulsive; a limiter can then help or hurt, and this was not analysed.
- **The high-SNR, finite-`N` estimator-correlator** is not Toeplitz. The loss of a truncated weighted
  periodogram for `B_D·T` near 1 was not computed (E7).
- **Simulator noise-generation cost** is a heuristic 5 ns per variate; GPU generator rates were not
  looked up.
- **Fusion** is asserted from the literature pattern and roofline arithmetic. No fused Matrix kernel
  exists.

---

## 10. Sources

All DOIs below resolved through Crossref on 26 November 2026 unless marked. "Abstract" means only
the abstract (through OpenAlex or the publisher) was read.

**Selected outputs, accuracy**
- [Goertzel58] G. Goertzel, "An algorithm for the evaluation of finite trigonometric series," *Amer. Math. Monthly* 65(1):34–35, 1958. https://doi.org/10.2307/2310304 [verified; not read]
- [Gentleman69] W. M. Gentleman, "An error analysis of Goertzel's (Watt's) method for computing Fourier coefficients," *Comput. J.* 12(2):160–164, 1969. https://doi.org/10.1093/comjnl/12.2.160 [verified; abstract]
- [Markel71] J. D. Markel, "FFT pruning," *IEEE Trans. Audio Electroacoust.* 19(4):305–311, 1971. https://doi.org/10.1109/TAU.1971.1162205 [verified; abstract]
- [SorensenBurrus93] H. V. Sorensen and C. S. Burrus, "Efficient computation of the DFT with only a subset of input or output points," *IEEE Trans. Signal Process.* 41(3):1184–1200, 1993. https://doi.org/10.1109/78.205723 [verified; abstract]
- [TadokoroHiguchi78] Y. Tadokoro and T. Higuchi, "Discrete Fourier transform computation via the Walsh transform," *IEEE Trans. Acoust., Speech, Signal Process.* 26(3):236–240, 1978. https://doi.org/10.1109/TASSP.1978.1163087 [verified; abstract]
- [SyselRajmic12] P. Sysel and P. Rajmic, "Goertzel algorithm generalized to non-integer multiples of fundamental frequency," *EURASIP J. Adv. Signal Process.* 2012:56. https://doi.org/10.1186/1687-6180-2012-56 [verified; abstract]
- [Schatzman96] J. C. Schatzman, "Accuracy of the discrete Fourier transform and the fast Fourier transform," *SIAM J. Sci. Comput.* 17(5):1150–1166, 1996. https://doi.org/10.1137/S1064827593247023 [verified; abstract]
- [GentlemanSande66] W. M. Gentleman and G. Sande, "Fast Fourier transforms—for fun and profit," *Proc. AFIPS Fall Joint Computer Conf.*, 563–578, 1966. https://doi.org/10.1145/1464291.1464352 [verified; cited through FFTW]
- [Gentleman68] W. M. Gentleman, "Matrix multiplication and fast Fourier transforms," *Bell Syst. Tech. J.* 47(6):1099–1103, 1968. https://doi.org/10.1002/j.1538-7305.1968.tb00074.x [verified; abstract] (the FFT as a matrix factorization)
- [FrigoJohnson05] M. Frigo and S. G. Johnson, "The design and implementation of FFTW3," *Proc. IEEE* 93(2):216–231, 2005. https://doi.org/10.1109/JPROC.2004.840301 [verified; not re-read]
- [FFTW-pruned] FFTW, "Pruned FFTs." https://www.fftw.org/pruned.html [verified; read]
- [FFTW-accuracy] FFTW, accuracy benchmark comments. https://www.fftw.org/accuracy/comments.html [verified; read]
- [Wiki-Goertzel] Wikipedia, "Goertzel algorithm." https://en.wikipedia.org/wiki/Goertzel_algorithm [verified; complexity section flagged there as unsourced]

**Sliding and hopping DFT**
- [JacobsenLyons03] E. Jacobsen and R. Lyons, "The sliding DFT," *IEEE Signal Process. Mag.* 20(2):74–80, 2003. https://doi.org/10.1109/MSP.2003.1184347 [verified; abstract; Crossref lists the column editor]
- [JacobsenLyons04] E. Jacobsen and R. Lyons, "An update to the sliding DFT," *IEEE Signal Process. Mag.* 21(1):110–111, 2004. https://doi.org/10.1109/MSP.2004.1516381 [verified; not read]
- [Duda10] K. Duda, "Accurate, guaranteed stable, sliding discrete Fourier transform," *IEEE Signal Process. Mag.* 27(6):124–127, 2010. https://doi.org/10.1109/MSP.2010.938088 [verified; abstract]
- [ParkKo14] C.-S. Park and S.-J. Ko, "The hopping discrete Fourier transform," *IEEE Signal Process. Mag.* 31(2):135–139, 2014. https://doi.org/10.1109/MSP.2013.2292891 [verified; abstract uninformative]
- [Wang15] Q. Wang, X. Yan and K. Qin, "High-precision, permanently stable, modulated hopping discrete Fourier transform," *IEEE Signal Process. Lett.* 22(6):748–751, 2015. https://doi.org/10.1109/LSP.2014.2369518 [verified; abstract]
- [Park17] C.-S. Park, "Guaranteed-stable sliding DFT algorithm with minimal computational requirements," *IEEE Trans. Signal Process.* 65(20):5281–5288, 2017. https://doi.org/10.1109/TSP.2017.2726988 [verified; abstract]
- [KimChang00] J.-H. Kim and T.-G. Chang, "Analytic derivation of the finite wordlength effect of the twiddle factors in recursive implementation of the sliding-DFT," *IEEE Trans. Signal Process.* 48(5):1485–1488, 2000. https://doi.org/10.1109/78.839998 [verified; abstract]
- [vdBylInggs16] A. van der Byl and M. R. Inggs, "Constraining error—a sliding discrete Fourier transform investigation," *Digit. Signal Process.* 51:54–61, 2016. https://doi.org/10.1016/j.dsp.2016.01.008 [verified; search snippet only]
- [ChauhanSingh22] A. Chauhan and K. M. Singh, "Recursive sliding DFT algorithms: a review," *Digit. Signal Process.* 127:103560, 2022. https://doi.org/10.1016/j.dsp.2022.103560 [verified; metadata and search snippet only]
- [RichardsonEddy19] L. F. Richardson and W. F. Eddy, "Algorithm 991: The 2D tree sliding window discrete Fourier transform," *ACM Trans. Math. Softw.* 45(1):12, 2019. https://doi.org/10.1145/3264426 ; https://arxiv.org/abs/1707.08213 [verified; snippet]

**Non-uniform and hypothesis-specific bases**
- [Rabiner69] L. Rabiner, R. Schafer and C. Rader, "The chirp z-transform algorithm," *IEEE Trans. Audio Electroacoust.* 17(2):86–92, 1969. https://doi.org/10.1109/TAU.1969.1162034 [verified; not re-read]
- [Bluestein70] L. Bluestein, "A linear filtering approach to the computation of discrete Fourier transform," *IEEE Trans. Audio Electroacoust.* 18(4):451–455, 1970. https://doi.org/10.1109/TAU.1970.1162132 [verified; not re-read]
- [HoyerStork77] E. Hoyer and R. Stork, "The zoom FFT using complex modulation," *Proc. ICASSP* 2:78–81, 1977. https://doi.org/10.1109/ICASSP.1977.1170177 [verified; not read]
- [DuttRokhlin93] A. Dutt and V. Rokhlin, "Fast Fourier transforms for nonequispaced data," *SIAM J. Sci. Comput.* 14(6):1368–1393, 1993. https://doi.org/10.1137/0914081 [verified; not read]
- [GreengardLee04] L. Greengard and J.-Y. Lee, "Accelerating the nonuniform fast Fourier transform," *SIAM Rev.* 46(3):443–454, 2004. https://doi.org/10.1137/S003614450343200X [verified; not read]
- [Barnett19] A. H. Barnett, J. Magland and L. af Klinteberg, "A parallel nonuniform fast Fourier transform library based on an 'exponential of semicircle' kernel," *SIAM J. Sci. Comput.* 41(5):C479–C504, 2019. https://doi.org/10.1137/18M120885X [verified; abstract]
- [Shih21] Y.-H. Shih, G. Wright, J. Andén, J. Blaschke and A. H. Barnett, "cuFINUFFT: a load-balanced GPU library for general-purpose nonuniform FFTs," *IPDPSW 2021*, 688–697. https://doi.org/10.1109/IPDPSW52791.2021.00105 [verified; not read]
- [FINUFFT-docs] Flatiron Institute, FINUFFT documentation. https://finufft.readthedocs.io/en/latest/ [verified; read]
- [Ransom02] S. M. Ransom, S. S. Eikenberry and J. Middleditch, "Fourier techniques for very long astrophysical time-series analysis," *Astron. J.* 124(3):1788–1809, 2002. https://doi.org/10.1086/342285 [verified; not re-read]
- [JacobsenKootsookos07] E. Jacobsen and P. Kootsookos, "Fast, accurate frequency estimators," *IEEE Signal Process. Mag.* 24(3):123–125, 2007. https://doi.org/10.1109/MSP.2007.361611 [verified; not read]
- [Ozaktas96] H. M. Ozaktas, O. Arikan, M. A. Kutay and G. Bozdagi, "Digital computation of the fractional Fourier transform," *IEEE Trans. Signal Process.* 44(9):2141–2150, 1996. https://doi.org/10.1109/78.536672 [verified; not read]
- [Slepian78] D. Slepian, "Prolate spheroidal wave functions, Fourier analysis, and uncertainty—V: the discrete case," *Bell Syst. Tech. J.* 57(5):1371–1430, 1978. https://doi.org/10.1002/j.1538-7305.1978.tb02104.x [verified; not re-read]
- [Thomson82] D. J. Thomson, "Spectrum estimation and harmonic analysis," *Proc. IEEE* 70(9):1055–1096, 1982. https://doi.org/10.1109/PROC.1982.12433 [verified; not re-read]
- [ZemenMecklenbrauker05] T. Zemen and C. F. Mecklenbräuker, "Time-variant channel estimation using discrete prolate spheroidal sequences," *IEEE Trans. Signal Process.* 53(9):3597–3607, 2005. https://doi.org/10.1109/TSP.2005.853104 [verified; abstract]
- [Karnik19] S. Karnik, Z. Zhu, M. B. Wakin, J. Romberg and M. A. Davenport, "The fast Slepian transform," *Appl. Comput. Harmon. Anal.* 46(3):624–652, 2019. https://doi.org/10.1016/j.acha.2017.07.005 ; https://arxiv.org/abs/1611.04950 [verified; abstract]
- [Kailath60] T. Kailath, "Correlation detection of signals perturbed by a random channel," *IRE Trans. Inf. Theory* 6(3):361–366, 1960. https://doi.org/10.1109/TIT.1960.1057563 [verified; cited through the practice review]

**Windows and channelizers**
- [Harris78] F. J. Harris, "On the use of windows for harmonic analysis with the discrete Fourier transform," *Proc. IEEE* 66(1):51–83, 1978. https://doi.org/10.1109/PROC.1978.10837 [verified; values recomputed here]
- [harris03] f. j. harris, C. Dick and M. Rice, "Digital receivers and transmitters using polyphase filter banks for wireless communications," *IEEE Trans. Microw. Theory Techn.* 51(4):1395–1412, 2003. https://doi.org/10.1109/TMTT.2003.809176 [verified; not re-read]
- [Price16] D. C. Price, "Spectrometers and polyphase filterbanks in radio astronomy," arXiv:1607.03579. https://arxiv.org/abs/1607.03579 [verified identifier; content per the practice review]
- [Tuthill15] J. Tuthill, G. Hampson, J. Bunton, f. harris, A. Brown, R. Ferris and others, "Compensating for oversampling effects in polyphase channelizers: a radio astronomy application," *IEEE SP/SPE Workshop 2015*, 255–260. https://doi.org/10.1109/DSP-SPE.2015.7369562 [verified; abstract]
- [Merry23] B. Merry, "Efficient channelization on a graphics processing unit," *J. Astron. Telesc. Instrum. Syst.* 9(3):038001, 2023. https://doi.org/10.1117/1.JATIS.9.3.038001 ; https://arxiv.org/abs/2303.09886 [verified; read through the ar5iv HTML]
- [katgpucbf-F] SARAO, katgpucbf "F-Engine Design." https://katgpucbf.readthedocs.io/en/latest/fgpu.design.html [verified; read]

**Finite precision and quantization**
- [Welch69] P. Welch, "A fixed-point fast Fourier transform error analysis," *IEEE Trans. Audio Electroacoust.* 17(2):151–157, 1969. https://doi.org/10.1109/TAU.1969.1162035 [verified; abstract]
- [Weinstein69] C. Weinstein, "Roundoff noise in floating point fast Fourier transform computation," *IEEE Trans. Audio Electroacoust.* 17(3):209–215, 1969. https://doi.org/10.1109/TAU.1969.1162049 [verified; abstract]
- [OppenheimWeinstein72] A. V. Oppenheim and C. J. Weinstein, "Effects of finite register length in digital filtering and the fast Fourier transform," *Proc. IEEE* 60(8):957–976, 1972. https://doi.org/10.1109/PROC.1972.8820 [verified; abstract]
- [TranThongLiu76] Tran-Thong and B. Liu, "Fixed-point fast Fourier transform error analysis," *IEEE Trans. Acoust., Speech, Signal Process.* 24(6):563–573, 1976. https://doi.org/10.1109/TASSP.1976.1162875 [verified; abstract]
- [Settimi10] A. Settimi, "Fourier domain analysis performances of a RESPER probe—amplitude and phase inaccuracies due to the round-off noise of FFT processors," arXiv:1009.1832, 2010. https://arxiv.org/abs/1009.1832 [verified; appendix read; used only as an accessible reproduction of the Oppenheim–Schafer–Buck fixed-point FFT result]
- [Oraintara02] S. Oraintara, Y.-J. Chen and T. Q. Nguyen, "Integer fast Fourier transform," *IEEE Trans. Signal Process.* 50(3):607–618, 2002. https://doi.org/10.1109/78.984749 [verified; abstract]
- [Despain74] A. M. Despain, "Fourier transform computers using CORDIC iterations," *IEEE Trans. Comput.* C-23(10):993–1001, 1974. https://doi.org/10.1109/T-C.1974.223800 [verified; abstract]
- [Suarez14] D. Suarez, R. J. Cintra, F. M. Bayer, A. Sengupta, S. Kulasekera and A. Madanayake, "Multi-beam RF aperture using multiplierless FFT approximation," *Electron. Lett.* 50(24):1788–1790, 2014. https://doi.org/10.1049/el.2014.3561 [verified; abstract]
- [Ariyarathna19] V. Ariyarathna, D. F. G. Coelho, S. Pulipati, R. J. Cintra, F. M. Bayer, V. S. Dimitrov and others, "Multibeam digital array receiver using a 16-point multiplierless DFT approximation," *IEEE Trans. Antennas Propag.* 67(2):925–933, 2019. https://doi.org/10.1109/TAP.2018.2882629 [verified; abstract]
- [Madanayake20] A. Madanayake, R. J. Cintra, N. Akram, V. Ariyarathna, S. Mandal, V. A. Coutinho and others, "Fast radix-32 approximate DFTs for 1024-beam digital RF beamforming," *IEEE Access* 8:96613–96627, 2020. https://doi.org/10.1109/ACCESS.2020.2994550 [verified; abstract]
- [VanVleckMiddleton66] J. H. Van Vleck and D. Middleton, "The spectrum of clipped noise," *Proc. IEEE* 54(1):2–19, 1966. https://doi.org/10.1109/PROC.1966.4567 [verified; not re-read]
- [Cooper70] B. F. C. Cooper, "Correlators with two-bit quantization," *Aust. J. Phys.* 23:521–527, 1970. https://ui.adsabs.harvard.edu/abs/1970AuJPh..23..521C [unverified: no Crossref record found; the ADS page returned a placeholder]
- [TES07] A. R. Thompson, D. T. Emerson and F. R. Schwab, "Convenient formulas for quantization efficiency," *Radio Sci.* 42:RS3022, 2007. https://doi.org/10.1029/2006RS003585 [verified; not read]
- [EVLA88] A. R. Thompson and D. T. Emerson, "Quantization noise," EVLA Memo 88, 21 January 2005. https://library.nrao.edu/public/memos/evla/EVLAM_88.pdf [verified; text extracted and read; Table 1 values]
- [TMS17] A. R. Thompson, J. M. Moran and G. W. Swenson, *Interferometry and Synthesis in Radio Astronomy*, 3rd ed., Springer, 2017. https://doi.org/10.1007/978-3-319-44431-4 [verified DOI; chapter 8 not accessible by fetch]
- [JenetAnderson98] F. A. Jenet and S. B. Anderson, "The effects of digitization on nonstationary stochastic signals with applications to pulsar signal baseband recording," *PASP* 110(754):1467–1478, 1998. https://doi.org/10.1086/316273 [verified; not re-read]
- [Kogan98] L. Kogan, "Correction functions for digital correlators with two and four quantization levels," *Radio Sci.* 33(5):1289–1296, 1998. https://doi.org/10.1029/98RS02202 [verified; not read]
- [Amoroso83] F. Amoroso, "Adaptive A/D converter to suppress CW interference in DSPN spread-spectrum communications," *IEEE Trans. Commun.* 31(10):1117–1123, 1983. https://doi.org/10.1109/TCOM.1983.1095753 [verified; abstract]
- [AmorosoBricker86] F. Amoroso and J. L. Bricker, "Performance of the adaptive A/D converter in combined CW and Gaussian interference," *IEEE Trans. Commun.* 34(3):209–213, 1986. https://doi.org/10.1109/TCOM.1986.1096517 [verified; abstract]
- [Hegarty11] C. J. Hegarty, "Analytical model for GNSS receiver implementation losses," *Navigation* 58(1):29–44, 2011. https://doi.org/10.1002/j.2161-4296.2011.tb01790.x [verified; abstract]
- [Curran10] J. T. Curran, D. Borio, G. Lachapelle and C. C. Murphy, "Reducing front-end bandwidth may improve digital GNSS receiver performance," *IEEE Trans. Signal Process.* 58(4):2399–2404, 2010. https://doi.org/10.1109/TSP.2009.2037860 [verified; abstract]
- [Jones63] J. J. Jones, "Hard-limiting of two signals in random noise," *IEEE Trans. Inf. Theory* 9(1):34–42, 1963. https://doi.org/10.1109/TIT.1963.1057797 [verified; abstract]
- [Davenport53] W. B. Davenport, "Signal-to-noise ratios in band-pass limiters," *J. Appl. Phys.* 24(6):720–727, 1953. https://doi.org/10.1063/1.1721365 [verified; not read]

**CPU and GPU throughput, vendor documentation**
- [CUDA-T36] NVIDIA, CUDA Programming Guide v13.5, "Compute Capabilities," Table 36. https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html [verified; table cells parsed]
- [PTX-ISA] NVIDIA, Parallel Thread Execution ISA 9.5. https://docs.nvidia.com/cuda/parallel-thread-execution/index.html [verified; `mma` notes read]
- [cuBLAS-doc] NVIDIA, cuBLAS 13.5 documentation, `cublasLtMatmul` tables. https://docs.nvidia.com/cuda/cublas/index.html [verified; tables parsed]
- [CUTLASS-func] NVIDIA, CUTLASS functionality. https://docs.nvidia.com/cutlass/latest/media/docs/cpp/functionality.html [verified; read]
- [TensorRT-quant] NVIDIA, TensorRT, "Working with Quantized Types." https://docs.nvidia.com/deeplearning/tensorrt/latest/inference-library/work-quantized-types.html [verified; read]
- [Intel-AMX] Intel Performance Index, Architecture Day 2021 claims. https://edc.intel.com/content/www/us/en/products/performance/benchmarks/architecture-day-2021/ [verified; read]
- [uops.info] A. Abel and J. Reineke, instruction measurements: `TDPBSSD` https://uops.info/html-instr/TDPBSSD_TMM_TMM_TMM.html ; `VPDPBUSD zmm` https://uops.info/html-instr/VPDPBUSD_ZMM_ZMM_ZMM.html [verified; read]
- [Intel-FFT-guide] Intel, "Choose a high-performance FFT in oneMKL or Intel IPP." https://www.intel.com/content/www/us/en/developer/articles/technical/onemkl-ipp-choosing-an-fft.html [verified; no figures]
- [pffft-bench] pffft benchmark notes, November 2026. https://raw.githubusercontent.com/marton78/pffft/master/docs/benchmarks.md [verified; charts not readable]
- [kfr-bench] KFR FFT benchmark suite. https://github.com/kfrlib/fft-benchmark [verified; no results in the repository]
- [fft-bench] R. Blackwell, `fft_bench`. https://www.github.com/blackwer/fft_bench [verified; charts not readable]
- [fftw-speed] FFTW benchFFT. https://www.fftw.org/speed/ [verified; metric definition only]
- [Colfax26] Colfax Research, "Optimizing an NVFP4 blockscaled GEMM on RTX PRO 6000 Blackwell GPU (SM120)," 9 August 2026. https://research.colfax-intl.com/optimizing-an-nvfp4-blockscaled-gemm-on-rtx-pro-6000-blackwell-gpu-sm120/ [verified; read]
- [TurboFFT] S. Wu and others, "TurboFFT: co-designed high-performance and fault-tolerant fast Fourier transform on GPUs," PPoPP 2025; arXiv:2412.05824. https://arxiv.org/abs/2412.05824 [verified; HTML read]. Earlier version arXiv:2405.02520 [verified identifier]
- [TurboFNO] S. Wu and others, "TurboFNO: high-performance Fourier neural operator with fused FFT-GEMM-iFFT on GPU," arXiv:2504.11681, 2025. https://arxiv.org/abs/2504.11681 [verified; HTML read]
- [M3XU] D. Ha, Y. Zhang, C.-C. Kao, C. J. Hughes, W. W. Ro and H.-W. Tseng, "M³XU: achieving high-precision and complex matrix multiplication with low-precision MXUs," *SC24*, 2024. https://doi.org/10.1109/SC41406.2024.00016 ; text read from https://par.nsf.gov/servlets/purl/10626416 [verified; full text]
- [TensorFHE] S. Fan, Z. Wang, W. Xu, R. Hou, D. Meng and M. Zhang, "TensorFHE: achieving practical computation on encrypted data using GPGPU," *HPCA 2023*, 922–934. https://doi.org/10.1109/HPCA56546.2023.10071017 [verified; abstract]
- [WarpDrive] G. Fan, M. Zhang, F. Zheng, S. Fan, T. Zhou, X. Deng and others, "WarpDrive: GPU-based fully homomorphic encryption acceleration leveraging tensor and CUDA cores," *HPCA 2025*, 1187–1200. https://doi.org/10.1109/HPCA61900.2025.00091 ; author copy https://zhengfangyu.github.io/assets/papers/HPCA2025.pdf [verified; full text]
- [FHECore] L. Daksha and others, "FHECore: rethinking GPU microarchitecture for fully homomorphic encryption," arXiv:2602.22229, 2026. https://arxiv.org/abs/2602.22229 [verified; first 100,000 characters of the HTML read]
- [VkFFT] D. Tolmachev, "VkFFT—a performant, cross-platform and open-source GPU FFT library," *IEEE Access* 11:12039–12058, 2023. https://doi.org/10.1109/ACCESS.2023.3242240 [verified; abstract]
- [pyvkfft-perf] pyvkfft performance notes. https://pyvkfft.readthedocs.io/en/latest/performance.html [verified; text read, plots not readable]
- [US12475189] "Techniques to perform operations with matrix multiply accumulate (MMA) circuitry," US patent 12,475,189. https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12475189 [**unverified content**: image-only PDF; assignee, dates and claims not read; statements above are from a search-engine snippet]
- Identifiers verified through the arXiv API, content as reported in the prior reviews: [tcFFT] arXiv:2104.11471; [FlashFFTConv] arXiv:2311.05908; [HadaCore] arXiv:2412.08832; [Matsuoka26] arXiv:2606.23698; [TCBF] Tensor-Core Beamformer, arXiv:2505.03269; [TINA] arXiv:2408.16551; Luo et al. arXiv:2402.13499 and arXiv:2501.12084; Jarmusch et al. arXiv:2507.10789; FlashGPU-sim arXiv:2609.15311; MMA-Sim arXiv:2511.10909; Nitz et al. arXiv:2611.17229; [Klages15] Klages et al., "GPU kernels for high-speed 4-bit astrophysical data processing," arXiv:1503.06203.
- Vendor peak rates and memory bandwidths (Ada and RTX Blackwell whitepapers) are taken from the [Opus-5.5 hardware review](../../Opus-5.5/reviews/review-gpu-hardware.md) and were not re-fetched.

**Practice**
- [TCC] J. W. Romein, "The Tensor-Core Correlator," *A&A* 656:A52, 2021. https://doi.org/10.1051/0004-6361/202141896 [verified; content per prior reviews]
- [xGPU] M. A. Clark, P. C. La Plante and L. J. Greenhill, "Accelerating radio astronomy cross-correlation with graphics processing units," *Int. J. High Perform. Comput. Appl.* 27(2):178–192, 2013. https://doi.org/10.1177/1094342012444794 [verified; content per prior reviews]
- [CHIME22] CHIME Collaboration, "An overview of CHIME, the Canadian Hydrogen Intensity Mapping Experiment," *ApJS* 261(2):29, 2022. https://doi.org/10.3847/1538-4365/ac6fd9 [verified; metadata only]
- [MWAX] I. S. Morrison and others, "MWAX: a new correlator for the Murchison Widefield Array," *PASA* 40:e019, 2023. https://doi.org/10.1017/pasa.2023.15 [verified; abstract]
- [Bifrost] M. D. Cranmer and others, "Bifrost: a Python/C++ framework for high-throughput stream processing in astronomy," *J. Astron. Instrum.* 6(4):1750007, 2017. https://doi.org/10.1142/S2251171717500076 [verified; metadata only]
- [Hobiger10] T. Hobiger, T. Gotoh, J. Amagai, Y. Koyama and T. Kondo, "A GPU based real-time GPS software receiver," *GPS Solut.* 14(2):207–216, 2010. https://doi.org/10.1007/s10291-009-0135-2 [verified; metadata only]
- [Xu17] L. Xu, N. I. Ziedan, X. Niu and W. Guo, "Correlation acceleration in GNSS software receivers using a CUDA-enabled GPU," *GPS Solut.* 21(1):225–236, 2017. https://doi.org/10.1007/s10291-016-0516-2 [verified; search snippet only]
- [hyperseti] hyperseti documentation, "Dedoppler." https://hyperseti.readthedocs.io/en/latest/pipeline/dedoppler.html [verified; read]
- [Cruz22] L. Cruz, W. Farah and R. Elkins, "Search for extraterrestrial intelligence: GPU accelerated TurboSETI," *Proc. SciPy 2022*. https://doi.org/10.25080/majora-212e5952-004 [verified; content per the practice review]
- [Margot21] J.-L. Margot and others, *Astron. J.* 161(2):55, 2021. https://doi.org/10.3847/1538-3881/abcc77 [verified; content per the practice review]

**Could not access**
- Text of US patent 12,475,189 (image-only PDF at USPTO; Google Patents returned 404; FreePatentsOnline 403).
- Hegarty's MITRE preprint `09_3995.pdf` (403); the HAL-ENAC interference-and-quantization paper (bot wall); a UNSW paper on ADC thresholds under CW interference (403). A search snippet from the last states an optimum threshold-to-noise ratio near 1.35 with CW interference against 0.8 without [unverified, not used].
- OpenBenchmarking FFTW result pages (403).
- Thompson, Moran and Swenson chapter 8 (login redirect); EVLA Memo 88 was used instead.
- Weinstein's Lincoln Laboratory TR-468 (file too large for the fetch tool).
- Full texts of the IEEE articles listed as "abstract" above.

---

## Appendix A. Derivations used above

- **Square-wave reference.** For `s = A·cos` and reference `r`, matched-filter SNR is
  `(∫sr)²/(N₀/2·∫r²)`. With `r = cos`: `A²T/N₀`. With `r = sign(cos)`: `∫sr = 2AT/π`, `∫r² = T`,
  giving `(8/π²)·A²T/N₀`. The complex case `r = sign(cos) + j·sign(sin)` gives the same ratio.
- **Harmonics of a quantized reference.** `q_k[n] = G[(kn) mod N]` with
  `G[p] = Q(A·cos 2πp/N) + j·Q(A·sin 2πp/N)`. With `Ĝ[h]` the DFT of `G`, the response of reference
  `k` to an on-grid tone at bin `m` is `Σ_{h: hk ≡ m} Ĝ[h]`. Quadrant symmetry leaves
  `h ∈ {1, −3, 5, −7, …}`.
- **Break-even hop.** Recomputed FFT `5·N_fft·log₂N_fft`; block update `8KL` plus a rotation.
- **Roofline `N*`.** Memory time per output point `b/BW`; ops available `R·b/BW`; a direct complex
  output needs `8N` ops; `N* = R·b/(8·BW)`.
- **Toeplitz identity.** `Σ_k w_k|X_M[k]|² = Σ_{m,n} x_m^*·x_n·Σ_k w_k e^{−j2πk(n−m)/M}`; lags
  `|n−m| ≤ N−1` are distinct modulo `M ≥ 2N−1`, so `w` is the inverse DFT of the lag sequence.
- **Chirp loss.** `|∫_{−1/2}^{1/2} exp(jπc·u²)du|²` with `c = rate·T²`.
- **Integer sliding DFT.** `S[n] − S[n−1] = x[n]c[n] − x[n−N]c[n−N]` and `c[n−N] = c[n]`.

## Appendix B. Calculation script (theoretical values of §2.2–§2.5 and §3.2)

Standard-library Python; run with `python3 -I`. It reproduces the coefficient-quantization table,
the input-quantization table, the window and interpolation losses, the chirp losses, the hop
counts, the roofline lengths and the fixed-point FFT figures. Assumptions: `N` = 512; bins −224…223;
references rounded to the nearest level; Gaussian unit-variance input, Nyquist-sampled, low SNR;
vendor dense peak rates.

```python
import cmath, math
N = 512; BINS = list(range(-224, 224))
E2M1 = sorted({s * m for s in (-1, 1) for m in (0, .5, 1, 1.5, 2, 3, 4, 6)})
def harm(q):                                   # harmonic spectrum of one quantised period
    G = [complex(q(math.cos(2*math.pi*p/N)), q(math.sin(2*math.pi*p/N))) for p in range(N)]
    W = [cmath.exp(-2j*math.pi*i/N) for i in range(N)]
    return [sum(G[p]*W[(h*p) % N] for p in range(N))/N for h in range(N)]
def coeff(name, q):                            # loss, total spur and worst spur over the bins
    Gh = harm(q); ml = wl = mf = 0.0; ws = -1e9
    for k in BINS:
        R = [0j]*N
        for h in range(N): R[(h*k) % N] += Gh[h]
        main = abs(R[k % N])**2; tot = sum(abs(r)**2 for r in R)
        loss = -10*math.log10(main/tot); ml += loss/len(BINS); wl = max(wl, loss)
        mf += (tot-main)/main/len(BINS)
        spur = max(abs(R[m])**2 for m in range(N) if m != k % N)/main
        ws = max(ws, 10*math.log10(max(spur, 1e-30)))
    print(name, round(ml, 3), round(wl, 3), round(10*math.log10(mf), 2), round(ws, 2))
book = lambda a, b: (lambda v: min(b, key=lambda c: abs(c - a*v)))
coeff("sign", lambda v: 1.0 if v >= 0 else -1.0)
coeff("ternary 0.383", lambda v: 0.0 if abs(v) < math.sin(math.pi/8) else math.copysign(1, v))
coeff("ternary 0.5", lambda v: 0.0 if abs(v) < 0.5 else math.copysign(1, v))
coeff("{1,3}", book(3.0, [-3, -1, 1, 3]))
for a in (3, 7, 15, 31, 63, 127, 2048): coeff("round(%dcos)" % a, lambda v, a=a: float(round(a*v)))
coeff("E2M1 x6", book(6.0, E2M1)); coeff("E2M1 x4", book(4.0, E2M1))

def gexp(f, lim=9.0, n=36000):                 # E[f(|x|)] doubled, Simpson, x ~ N(0,1)
    h = lim/n
    return 2*h/3/math.sqrt(2*math.pi)*sum((1 if i in (0, n) else 4 if i % 2 else 2)
                                          * f(i*h)*math.exp(-0.5*(i*h)**2) for i in range(n+1))
def eff(Q, g):
    a = gexp(lambda x: x*Q(g*x)); b = gexp(lambda x: Q(g*x)**2)
    return a*a/b if b > 0 else 1e-30
midrise = lambda L: (lambda v: math.copysign(min(L/2-.5, math.floor(abs(v))+.5), v))
QUANT = {"3-level": lambda v: 0.0 if abs(v) < .5 else math.copysign(1, v),
         "2-bit": lambda v: math.copysign(3.0 if abs(v) >= 2 else 1.0, v),
         "8-level": midrise(8), "16-level": midrise(16),
         "INT4": lambda v: float(max(-8, min(7, round(v)))),
         "E2M1": lambda v: min(E2M1, key=lambda c: abs(c - v)),
         "INT8": lambda v: float(max(-128, min(127, round(v))))}
for name, Q in QUANT.items():                  # optimum loading, then loss versus gain error
    e, g = max((eff(Q, .05*1.06**i), .05*1.06**i) for i in range(150))
    print(name, round(g, 2), [round(-10*math.log10(eff(Q, g*10**(d/20))), 3)
                              for d in (0, -12, -6, -3, 3, 6, 12)])

def wloss(w, d):                               # window loss versus matched rectangular filter
    s = sum(w[n]*cmath.exp(2j*math.pi*d*(n-(N-1)/2)/N) for n in range(N))
    return -10*math.log10(abs(s)**2/(N*sum(x*x for x in w)))
c = lambda n, m: math.cos(2*math.pi*m*(n+.5)/N)
for name, w in {"rect": [1.0]*N, "hann": [.5-.5*c(n, 1) for n in range(N)],
                "hamming": [.54-.46*c(n, 1) for n in range(N)],
                "blackman": [.42-.5*c(n, 1)+.08*c(n, 2) for n in range(N)]}.items():
    print(name, [round(wloss(w, d), 3) for d in (0, .5, .25, .125, .0625)])
D = lambda u: 1.0 if abs(u) < 1e-12 else math.sin(math.pi*u)/(N*math.sin(math.pi*u/N))
for M in (1, 2, 4, 8, 16):                     # truncated Dirichlet interpolation, 2M taps
    print(2*M, round(-10*math.log10(sum(D(.5-j)**2 for j in range(-M+1, M+1))), 3))
for cc in (.25, .5, 1, 2, 3, 4):               # chirp mismatch, c = rate*T^2
    s = sum(cmath.exp(1j*math.pi*cc*((i+.5)/4000-.5)**2) for i in range(4000))/4000
    print(cc, round(-20*math.log10(abs(s)), 3))
fft = 5*N*math.log2(N)
for L in (256, 128, 64, 16, 4, 1):             # hop cost and break-even K
    print(L, 8*512*L+6*512, 8*64*L+6*64, 5*N*math.log2(L)+12*N if L > 1 else 8*N, fft/(8*L+6))
for bw, rates in ((1008e9, (165.2e12, 660.6e12, 1321.2e12)), (1792e9, (209.5e12, 838e12, 1676e12)),
                  (1792e9, (503.8e12, 1007.6e12, 2015.2e12))):   # roofline N*
    print([(round(r*8/(8*bw)), round(r*16/(8*bw))) for r in rates])
for B in (7, 11, 15):                          # fixed-point FFT noise-to-signal, dB
    print(B, 10*math.log10(N*N*2.0**(-2*B)), 10*math.log10(4*N*2.0**(-2*B)))
```

## Appendix C. CPU microbenchmark method and raw output

- **Host.** AMD Ryzen 5 PRO 5650U (Zen 3, 6 cores/12 threads, AVX2 and FMA, no VNNI), Debian,
  g++ 14.2 `-O3 -march=native -std=c++17`. FFTW 3.3.10 from the system package, whose library reports
  `fftw-3.3.10-sse2-avx` (no AVX2 or FMA codelets), linked through hand-written prototypes.
- **Conditions.** One thread pinned to one logical CPU. Timing by `CLOCK_THREAD_CPUTIME_ID`, best of
  nine windows of at least 80 ms. Unrelated Monte Carlo jobs held the load average at 19–27, so the
  sibling hardware thread was busy; `/proc/cpuinfo` reported 2.82 GHz. This is not a qualified
  benchmark and not a capacity measurement.
- **FFT.** `fftwf_plan_many_dft`, `FFTW_PATIENT`, out of place, batch sized for about 1 MiB of input.
- **Direct FP32 kernel.** Split-complex input; four bins per pass; four FMAs per bin per eight
  samples; coefficient table 448 × 512 × 2 floats.
- **Direct int8 kernel.** Samples interleaved `[re, im]` as s8. Each bin has two unsigned rows,
  `[cr, ci] + 64` and `[−ci, cr] + 64`, with 7-bit coefficients `round(63·cos)`. One
  `VPMADDUBSW` gives a complete real or imaginary product pair in s16; `VPMADDWD` with ones widens to
  s32; the `+64` offset is removed with `64·Σ(re + im)` per symbol. A variant for samples limited to
  ±8 accumulates sixteen s16 results before widening. Outputs equal an exact integer reference.
- **Bin sets.** "Distinct": a fresh random set of `K` bins per symbol, 64 symbols. "Shared":
  one set for 64 symbols, processed four bins at a time over all symbols.

```text
FFT c2c single, ns per transform (GFLOP/s by 5 N log2 N):
  64: 77.9 (24.7)   128: 157.8 (28.4)   256: 341.7 (30.0)   512: 814.4 (28.3)
  1024: 1827.5 (28.0)   2048: 3953.0 (28.5)   4096: 9005.3 (27.3)   8192: 21723.4 (24.5)
Direct DFT, N = 512, ns per symbol, distinct bin set per symbol:
  K      FP32      int8   int8 (4-bit-range samples)
  4     480.3     303.4     275.1
  8     892.0     582.0     528.6
  16   1757.8    1148.7    1038.8
  32   3393.9    2234.3    2017.9
  64   6759.6    4425.8    3999.5
  128 13474.7    8947.8    8103.9
  256 27535.5   18106.6   16210.1
  448 48212.9   32152.9   29231.7
Direct DFT, shared bin set, cache-blocked:
  4     314.7     215.9     222.6
  8     590.0     417.5     439.2
  16   1161.3     812.7     832.9
  32   2334.0    1624.3    1726.6
  64   4782.7    3224.1    3079.8
  128  9554.1    6240.8    6871.0
  256 19474.0   12902.3   12652.3
  448 32782.3   22733.1   23314.9
int8 kernels: maximum absolute difference from the exact integer reference = 0
```

The benchmark source was scratch code and is not retained; the description above is sufficient to
rewrite it for experiment E1.

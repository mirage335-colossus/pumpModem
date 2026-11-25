# Tensor-core / matrix-unit utilization for signal processing and detection: literature review

Prepared 2026-11-24 for the "Matrix Modem" question: can a weak-signal receiver put nearly all of its work into batched low-precision GEMMs (hypothesis templates × received data) and actually reach a large fraction of GPU tensor capability?

**How to read this report.**
- Every number is attributed to a source, and each reference says what was actually read. Where I read only an abstract, a secondary summary or a non-peer-reviewed repository, that is flagged.
- Numbers marked **[derived]** are my own arithmetic from vendor peak figures. They are roofline estimates, not measurements.
- I found no published work matching the receiver's exact profile: one narrowband stream of about 2.4 k complex samples/s searched across an enormous hypothesis space on integer or FP4 tensor cores. The closest precedents are radio-astronomy correlators and beamformers, matched-filter bank searches, low-bit LLM kernels and binarized-network kernels.

---

## 0. Executive summary

1. **High tensor-core utilization on signal processing has been achieved, but only when the problem is a large, dense GEMM.**
   - The ASTRON Tensor-Core Correlator correlates 5–10× faster and more energy-efficiently than earlier GPU correlators (Romein 2021).
   - Its successor library (ccglib / Tensor-Core Beamformer) reaches about 50–85% of peak on large compute-bound shapes. In absolute terms that is 3,080 useful TOPS for 1-bit GEMM on an A100 and 1,400 TOPS on an RTX 4000 Ada.
   - Small or batched-small shapes are memory-bound, and utilization only approaches its optimum once matrices are a few thousand elements per side (Oostrum et al. 2025).
2. **Transforms such as the FFT and Hadamard gain little from tensor cores.**
   - Reported gains are 1.1–3.2× (tcFFT), 1.0–1.4× typical (HadaCore) and 1.54–1.77× in a radar pipeline.
   - The big wins come from changing the algorithm so the transform becomes GEMM-dominated with fused I/O. FlashFFTConv reaches up to 62% model-FLOP utilization end-to-end on H100.
3. **Kernel-level lessons from low-bit LLM and BNN work transfer directly:**
   - keep the main loop in tensor-core-native formats, with no dequantization there;
   - fuse reductions and quantization into the epilogue;
   - use bit-plane decomposition for odd precisions;
   - use persistent, grouped or Stream-K scheduling for irregular shapes;
   - remember that end-to-end gains are always below kernel gains.
4. **Matched-filter bank searches show the most transferable system pattern.** A 2026 paper fuses correlation with peak reduction so only candidate peaks leave the processor. It adds statistically controlled hierarchical screening and gets 19–26× over library FFT baselines (Nitz et al. 2026). GstLAL-style SVD compression cuts the filter count by about 10× (Cannon et al. 2010).
5. **Hardware-format availability is fragmented and must be designed around:**
   - INT4 tensor MMA is native on Turing, Ampere and Ada, and is reported emulated on sm_90 and sm_100.
   - INT4 is absent from the RTX 5090 specification table, where FP4 is the 4-bit path at 1,676 dense TFLOPS with FP32 accumulate.
   - 1-bit XOR is emulated on Hopper; the AND+popc form is required there.
   - GeForce parts run FP16 and FP8 with FP32 accumulate at half rate.
6. **Learned receivers are not a shortcut to sensitivity.**
   - The best learned 5G receivers come within about 1 dB of strong classical baselines, or gain under specific conditions such as sparse pilots.
   - Neural ML-decoding of long codes faces the curse of dimensionality.
   - In a gravitational-wave mock-data challenge, ML searches reached about 95% of matched-filter sensitive distance on Gaussian noise but only about 70% on real noise.
   - Model-based matched filtering remains the sensitivity reference. GEMM-shaped learned components are plausible as screens, not as replacements.

---

## Hardware reference figures (vendor whitepapers)

| GPU (arch) | Dense INT8 | Dense INT4 | Dense FP4 | FP8 (FP32 acc) | FP16 (FP32 acc) | DRAM BW | L2 |
|---|---|---|---|---|---|---|---|
| RTX 4090 (Ada, sm_89) | 660.6 TOPS | 1,321.2 TOPS | — | 330.3 TF (660.6 with FP16 acc) | 165.2 TF (330.3 with FP16 acc) | 1,008 GB/s | 72 MB |
| RTX 5090 (GB202, sm_120) | 838 TOPS | not listed | 1,676 TF (FP32 acc) | 419 TF (838 with FP16 acc) | 209.5 TF (419 with FP16 acc) | 1,792 GB/s | 96 MB |
| RTX PRO 6000 Blackwell WS | — | — | "4000 AI TOPS" FP4 *with sparsity* | — | — | 1,792 GB/s | — |
| A100 | 624 TOPS | 1,248 TOPS | — | — | 312 TF (FP16) | — | — |

Sources: Ada and RTX Blackwell whitepaper appendices (text extracted from the PDFs); NVIDIA RTX PRO 6000 product page; A100 values as quoted in Atom (Zhao et al. 2024) and the TCBF table (Oostrum et al. 2025). The A100 binary (1-bit) peak is 4,992 TOPS (TCBF Table).

**Measured MMA throughput (Luo et al. 2025, arXiv 2501.12084).**
- RTX 4090 with mma.sync:
  - FP16 with FP16 accumulate: 357.6 TFLOPS.
  - FP16 with FP32 accumulate: 178.9 TFLOPS, so the half rate is confirmed.
  - INT8: 711.7 TOPS. This is above nominal because the card ran at 2,710 MHz.
- A100 reaches at least 95% of peak, for example 607.6 INT8 TOPS.
- On H800, mma.sync averages only 62.9% of peak, while wgmma reaches about 95%.
- Random rather than zero inputs reduce sustained H800 dense FP16 throughput from 729 to 595 TFLOPS (about 18%), because the card hits its power cap.

**Ridge points [derived]** (peak divided by DRAM bandwidth, in ops per byte):
- RTX 4090: INT4 ≈ 1,311, INT8 ≈ 655, FP16 with FP32 accumulate ≈ 164.
- RTX 5090: FP4 ≈ 935, INT8 ≈ 468, FP8 with FP32 accumulate ≈ 234.
- RTX PRO 6000: assuming dense FP4 is half the sparse 4,000 figure, ≈ 1,116.

Any stage below these intensities is bandwidth-bound no matter what the tensor peak is.

---

## 1. Radio astronomy: correlators, beamformers and quantization practice

### 1.1 Tensor-Core Correlator (TCC) — Romein, A&A 656, A52 (2021)

**What was achieved.**
- TCC is an open-source GPU library that correlates signals "five to ten times faster and more energy efficiently than traditional state-of-the-art GPU correlators" (abstract). The comparison is against xGPU on an A100, per the full-text figure captions.
- Current repository README precisions and rates per block:

| Input | Since | Rate per block | Accumulator | Constraint |
|---|---|---|---|---|
| fp16 | sm_70 | 8 times per block | complex float | — |
| i8 | sm_72 | 16 times per block | complex int32 | input must not contain −128 |
| i4 | native only on Ampere and Ada | 32 times per block | complex int32 | input must not contain −8 |
| fp8 (e4m3, e5m2) | sm_90 | 16 times per block | — | — |
| fp4 (e2m1) | Blackwell | 32 times per block | — | only sm_120 tested |

- 1-bit is not in TCC; it is in ccglib (§1.2).
- I could not read the A&A full text (HTTP 403), so I have no per-GPU percent-of-peak figures.

**Design pattern.**
- The data layout is unusual but required: channel → time-block → receiver → polarization → time-within-block. The README says it is "the only format that yields good performance".
- Fragments are filled with receivers, and output tiles cover the triangular baseline matrix.
- The library computes only the correlations (the X step). The FFT/PFB "F" step runs on CUDA cores.

**Production derivative (katgpucbf, MeerKAT Extension).** Its design notes show how this works in practice:
- Complex multiplication is mapped to real tensor cores by interleaving real and imaginary parts in A, with a sign-swapped, permuted copy of B built with `__byte_perm` bit tricks.
- The fragment shape is 16×8 rather than 16×16, because the complex expansion of B needs the extra width.
- Accumulation is in 64-bit integers, reduced to 32-bit with saturation and counted overflows.
- An extra grid dimension over time blocks adds parallelism when each engine owns only a few channels. This is a direct small-problem workaround.

**Transferability.** High.
- The receiver's "hypotheses × samples" score is the same contraction, with hypotheses in place of receivers.
- The key difference is where the arithmetic intensity comes from. A correlator gets it from N² baselines on massive data. The receiver has tiny data and huge "weights" (templates), so its intensity must come from reusing each template across many timing columns, or from generating templates on-chip (§8).

### 1.2 Tensor-Core Beamformer / ccglib — Oostrum, Veenboer, Rook, Brown, Kruizinga, Romein (arXiv:2505.03269, 2025)

**What was achieved** (full text read; arXiv HTML).

Microbenchmarks, as fraction of peak:
- A100 float16 about 99%; A100 int1 AND (16×8×256) about 99% (4,942 of 4,992 TOPS).
- GH200 about 65%, because the library uses WMMA rather than WGMMA.
- GH200 int1 XOR only about 6%, because XOR is software-emulated on Hopper.

Tuned complex GEMM:

| GPU | Precision | TOPS | TOPS/J |
|---|---|---|---|
| RTX 4000 Ada | float16 | 93 | 0.7 |
| A100 | float16 | 173 | 0.8 |
| GH200 | float16 | 335 | 0.8 |
| MI300X | float16 | 603 | 0.9 |
| RTX 4000 Ada | int1 | 1,400 | 10.7 |
| A100 | int1 | 3,080 | 12.3 |
| GH200 | int1 | 3,780 useful TOPS (AND form issues about twice the instructions) | 6.0 |

- The authors summarize this as 50–85% of peak for large, compute-bound shapes.
- Performance is low for small matrices and approaches optimal only "from a few thousand elements per side". Sawtooth dips appear when sizes are not multiples of the per-block work.
- A batched-small shape (batch 256 × 1024 × 1024 × K) is memory-bound for both float16 and int1.

Use cases:
- **Ultrasound (1-bit, sign-only data):** M = 38,880, N = 8,041, K = 524,288. Processed in 1.2 s, versus about 15 minutes for float32 Octave on an A100.
- **LOFAR beamforming (16-bit):** up to 20× faster and 10× more energy-efficient than the float32 reference.

**Design pattern.**
- Complex GEMM is done as four real GEMMs on a planar (split real/imag) layout, which requires transposes.
- 1-bit complex values are encoded as ±1±i. Zero is not representable, so padding uses −1 and adds a correction term.
- Two dot-product forms:
  - XOR: dot = K − 2·popc(a⊕b).
  - AND, used on Hopper and newer: 2(popc(a∧b) + popc(¬a∧¬b)) − K, which costs twice the MMA instructions.
- Separate pack and transpose kernels are memory-bound.
- Kernels are auto-tuned (Kernel Tuner).
- In the ultrasound case, Doppler filtering is done in higher precision *before* taking the sign. This is high-precision preconditioning followed by a 1-bit bulk GEMM.

**Transferability.** Very high.
- It shows 1-bit complex GEMM at multi-PetaOps on datacenter parts, and 1.4 POPS on a 130 W-class Ada workstation card.
- It shows that 1-bit is viable when the data are noise-dominated and preconditioned.
- It shows that the small-shape memory wall is real.

### 1.3 xGPU and pre-tensor-core correlators

- **xGPU** (Clark, La Plante & Greenhill, IJHPCA 2013; arXiv:1107.4264) sustained up to 79% of peak single precision (over 1 TFLOPS on a GTX 480).
  - It did this with multi-level tiling (registers / shared memory / texture) and pipelined host-to-device transfers overlapped with compute.
  - Earlier GPU correlators were reported at only 10–30% of peak and bandwidth-bound.
- **Van Nieuwpoort & Romein** (arXiv:1702.00844) report 96% of peak on Blue Gene/P, against only 16% on ATI and 32% on NVIDIA GPUs of that era.
- **CHIME** (Klages et al. 2015, arXiv:1503.06203) packed 4+4-bit offset-encoded complex samples so that one 32-bit fused multiply-add does several multiplies. That gave 5.6 effective TOPS on a nominally 4.3 TOPS AMD GPU. Packing within a register was the pre-tensor-core route to low-bit throughput; I saw this figure in an index listing only.

### 1.4 PFB / F-engine, and quantization in practice

- **GPU polyphase filter bank** (Adámek, Novotný & Armour, Astron. Comput. 2016; arXiv:1511.03599): with low-precision input, performance is limited by *type conversions* rather than device bandwidth. Conversion cost is a recurring hidden bottleneck at low precision.
- **ngEHT/SMA design study** (Yu et al., Galaxies 11, 13, 2023):
  - F-engines (PFB, FFT, delay, requantization) run in floating point on CUDA cores; only the X-engine uses tensor cores (via TCC / WMMA).
  - VLBI recordings are 2-bit and limited to about 88% "digital efficiency".
- **Quantization losses at low SNR (classical results):**
  - 1-bit: efficiency → 2/π ≈ 0.64 (van Vleck; EVLA Memo 88). That is about −1.96 dB **[derived: 10·log10(π/2)]**.
  - 2-bit, four-level: about 0.88 (Cooper 1970; Thompson, Moran & Swenson, *Interferometry and Synthesis in Radio Astronomy*, Ch. 8). That is about −0.55 dB **[derived]**.
  - 4-bit is commonly quoted near 0.98. I saw this only in a search snippet; verify in TMS Ch. 8.
- These efficiencies hold for **noise-dominated** samples, which is exactly the weak-signal regime. They degrade when strong signals or interferers are present. That is the reason to isolate channels and remove interference *before* bulk low-bit quantization.
- An ASTRON 2024 slide deck rates the FFT as unsuitable for tensor cores ("FFT ✘ (radix 8 ✔)"), beamforming as a partial fit and correlation as a good fit. It also notes that 4-bit mode needed PTX-level work.

---

## 2. FFT/DFT via matrix multiplication on tensor cores

**tcFFT** (Li, Cheng & Lin, arXiv:2104.11471, 2021).
- Half-precision batched 1D/2D FFT built on radix-16 DFT matrices that fit tensor-core fragments.
- Speedups over cuFFT half-precision kernels running on CUDA cores:

| GPU | 1D (average) | 2D (range) |
|---|---|---|
| V100 | 1.90× | 1.29–3.24× |
| A100 | 1.24× | 1.10–3.03× |

- Twiddle multiplies and complex access patterns are not native to tensor cores and cost extra work.
- **Transfer:** modest. The FFT stays memory-bound. An FFT of size N has an intensity of about 0.3·log2 N flop/byte in complex fp32 **[derived: 5N log2 N flops / 16N bytes]**, which is about 3 flop/byte at N = 1024. That is two orders of magnitude below the ridge points above.

**Sorna et al. 2018** (HiPCW 2018, DOI 10.1109/HiPCW.2018.8634417).
- Splits fp32 into two fp16 parts, multiplies them on tensor cores and recombines, to keep accuracy.
- tcFFT reports that this cuBLAS-based approach ran far below cuFFT.
- It is an early example of multi-slice precision recovery; the Ozaki-scheme INT8 emulation of DGEMM (Ootomo et al., arXiv:2306.11975) is a later one.

**Durrani et al.** (PACT 2021, DOI 10.1109/PACT52795.2021.00032). Tensor cores plus warp shuffles for FFT and NTT: up to 1.5× for FFT and up to 4× for NTT over prior GPU implementations (abstract only).

**FlashFFTConv** (Fu, Kumbong, Nguyen & Ré; arXiv:2311.05908; ICLR 2024).
- Monarch (Bailey four-step) decomposition turns the FFT into chains of matmuls with pointwise twiddles.
- Decomposition order p trades FLOPs (p = 2 costs O(N^{3/2})) against I/O.
- Steps are fused into one kernel to stay in SRAM; order 4 is needed beyond about 32K fp16 complex values of SRAM.
- Results:
  - up to 7.93× over PyTorch FFT convolution;
  - up to 4.4× end-to-end on H100;
  - up to about 62% model-FLOP utilization.
  - Its MFU is *lower* than FlashAttention-2, but it wins on wall clock because it needs fewer FLOPs.
- **Transfer:** high as a pattern. Small DFTs (16–256 points) become dense GEMMs, and FLOPs are traded for tensor-core-shaped operations plus fusion.

**TurboFFT** (Wu et al., PPoPP 2025; arXiv:2412.05824). A CUDA-core FFT with algorithm-based fault tolerance that matches or beats cuFFT by up to 300%. It does *not* use tensor cores; it is listed here to show that state-of-the-art FFT speed still comes from memory-bound CUDA-core design.

**FP64 FFT on FP8 tensor cores** (Matsuoka, arXiv:2606.23698, 2026; projections, not measurements).
- A Bailey six-step 1024³ FFT emulated on B300 FP8 tensor cores is bound by a per-output *integer epilogue* of about 203–281 instructions per output.
- Projected time is 63–87 ms against a 12.9 ms roof, so it is 4.9–6.7× short of the roof.
- **Lesson:** after the MMA, per-output reconstruction or epilogue work can dominate.

**INT8/INT4 FFT.** I found no published INT8 or INT4 FFT with an accuracy study.
- An integer DFT-as-GEMM is the same object as a beamformer with quantized phase weights. TCBF's LOFAR beam weights are complex exponentials in fp16, so "frequency hypotheses as beams" has a direct precedent at 16 bits.
- A literature search also suggests that irrational DFT coefficients cause significant rounding error at low precision (SFC, OpenReview; abstract-level only).

---

## 3. Hadamard/Walsh transforms on tensor cores

**HadaCore** (Agarwal et al., arXiv:2412.08832; PyTorch blog Oct 2024).
- Applies a 16×16 Hadamard with FP16 `mma.m16n8k16` on register-resident data.
- Larger sizes are a Kronecker composition (H_n = H_16 ⊗ …), implemented with shared-memory transposes between 256-point rounds, up to 2^15.
- Speedups over Dao AI Lab's CUDA-core fast-hadamard-transform:
  - A100: 1.1–1.4× typical, peak 3.5×.
  - H100: 1.0–1.3× typical, peak 3.6×.
- Weak sizes: 512 needs cross-fragment synchronization, and a full 16×16 MMA is paid for a 2-point stage. Sizes ≥ 8K suffer uncoalesced transposed loads.
- FP16 and BF16 are reported numerically adequate; MMLU was unchanged in an FP8-attention Llama run.

**Dao AI Lab fast-hadamard-transform.** CUDA-core, fp32/fp16/bf16 up to 32,768 points, using few threadblock synchronizations.

**QuaRot / SpinQuant / FlatQuant.**
- Online Hadamard rotations are kept because they make 4-bit quantization accurate, not because they are free.
- FlatQuant reports that QuaRot's pre-quantization transform costs 0.26× (relative slowdown) versus 0.07× for its fused approach (Llama-2-7B, RTX 3090).
- QuaRot reports up to 3.33× prefill speedup with W4A4 on Llama-2-70B.

**Transfer.** Medium-high.
- ML decoding of first-order Reed–Muller or Walsh codewords is exactly a fast Hadamard transform. A structured codeword dimension should use FWHT/HadaCore-style Kronecker GEMMs, not a dense codebook GEMM.
- Rotating before quantizing (spreading outliers) is a precision tool for low-bit data paths.
- Expect at most about 1.4× from tensor cores on a stand-alone transform. The win is fusing it into a GEMM pipeline.

---

## 4. Matched-filter / template-bank searches

### 4.1 Gravitational waves

**Nitz, Kacanja & Capano, "Peak Performance in Matched Filtering" (arXiv:2611.17229, Nov 2026; abstract read).**
- Fuses the conjugate product into the first inverse-transform stage and the peak reduction into the last, so "only candidate peaks leave the processor".
- Tiles batches in 2D so K² correlations reuse 2K cached spectra, moving filtering from DRAM-bound to cache-bound.
- Adds hierarchical screening: short low-frequency transforms first, with stage thresholds set from the spectral power profile and noise autocovariance to meet a *declared false-dismissal probability*.
- Results at N = 4096:
  - Fused filtering: 1.4× (CPU, AOCL-FFTW), 6.2× (Radeon 8060S, Vulkan) and 3.1× (Apple M2) over library FFTs.
  - With screening, in a fiducial GW search (ρ_th = 5.5, 10⁻² false-dismissal target): 26×, 21× and 19×.
- Tensor-core use is not mentioned in the abstract.

**Ratio-Filter Dechirping** (Nitz, Kacanja & Soni, PRD 113, 082003, 2026; arXiv:2601.18835).
- Argues FFT matched filtering is memory-bound when templates exceed cache.
- Divides out a reference chirp so the residual ratios are slowly varying short FIR filters, which are compute-bound.
- Measured 8× on the offline filtering loop.

**GstLAL / SVD template compression** (Cannon et al., PRD 82, 044025, 2010). Truncated SVD reduces the filter count "by an order of magnitude" with an analytic SNR-loss bound. SNRs are reconstructed by linear combination, which is a GEMM.

**PyTorch GstLAL** (Huang et al., arXiv:2410.16416) and **SGNL** (arXiv:2511.04730).
- A100 in float16 reached up to 169× a single CPU core running GstLAL, with offline sensitivity comparable to float32.
- SGNL median latency is 4.7 s versus 9.0 s.
- This shows fp16 filtering of SVD bases is acceptable for this application.

**SPIIR GPU** (arXiv:1702.02256). Time-domain IIR banks reach more than 100× a single CPU core on Maxwell. Not tensor-core based.

### 4.2 Pulsar, FRB and SETI searches

- **AstroAccelerate FDAS** (Adámek et al., arXiv:1711.10855; Dimoudi et al., arXiv:1804.05335). Fourier-domain acceleration search is a bank of short FFT-based correlations, one per trial acceleration. A custom convolution was 2.5–3.9× faster than cuFFT-based code on P100, and FDAS overall about 8× faster than PRESTO's GPU FDAS. No tensor cores.
- **Dedispersion** (arXiv:2311.05341). Brute-force GPU dedispersion is about 4× faster than alternatives on 8-bit, 8,192-channel input. I found no published tensor-core dedispersion. A SPIE 2026 abstract mentions FFT plus batched-GEMM dedispersion and beamforming to use tensor cores, but I could not retrieve the paper.
- **SETI drift search:**
  - turboSETI uses a Taylor-tree dedoppler. Its CuPy port ran 6.8–9.3× faster in double and 7.5–10.6× in single precision on a GTX 1070 Ti (SciPy 2022).
  - hyperseti uses *brute-force* dedoppler on GPU and argues that I/O and peak-finding, not the kernel, are the bottlenecks.

### 4.3 Seismology, GNSS and radar

- **Seismology:** cuNCC template matching reached 2,912 GFLOP/s on a P100. No tensor cores.
- **GNSS acquisition:** 2-D code-phase × Doppler search, typically FFT-based parallel code-phase search. GPU studies (circa 2013) found GPUs adequate, with the largest advantage for long codes. I found no tensor-core GNSS acquisition.
- **Radar:** Wang et al. (IMCEC 2022, DOI 10.1109/IMCEC55388.2022.10019890), V100 coherent accumulation:
  - fp16 was 1.18–1.39× faster than fp32;
  - tcFFT tensor-core 2D FFT was 2.23–3.17× faster than fp16 cuFFT;
  - the whole pipeline was 1.54–1.77× faster.

**Transferability.** These are the receiver's closest algorithmic relatives: bank search, drift search and peak finding under false-alarm control.
- Most production systems still run FFT-based or tree algorithms on CUDA cores.
- The transferable wins are fusion of peak reduction, cache tiling, SVD/low-rank bank compression and *statistically controlled* hierarchical screening.
- Explicit tensor-core matched filtering is essentially unpublished, which is an opportunity and a risk.

---

## 5. Low-bit GEMM kernel engineering

| Work | Precision / GPU | Reported result | Key mechanism |
|---|---|---|---|
| BTC-BNN (Li & Su, IEEE TPDS 32(7) 2021; arXiv:2006.16578) | 1-bit, Turing | Up to 4.4× over CUTLASS bit-GEMM; ResNet-18 at 5.6K img/s, 77% faster than prior SOTA. BNNs on word-based architectures show about **1% utilization**. | Data-format co-design; stride-sensitive bit-tensor loads |
| APNN-TC (Feng et al., SC'21; arXiv:2106.12169) | Arbitrary wXaY via int1 bit-planes, RTX 3090 / A100 | Up to 3.78× over CUTLASS and 3.08× over cuBLAS; w1a2 up to 2.35× over CUTLASS int4; w5a1 up to 3× over cuBLAS int8 | Batch the p·q bit-plane products into one larger BMMA (pM×K by qN×K); recombine Y = Σ 2^{s+t} Y(s,t) in the epilogue |
| Ladder / BitBLAS (Wang et al., OSDI'24) | Mixed (e.g., W_INT2·A_INT8), A100 | Slides: up to 2.4× end-to-end, 3.7× kernel. BitBLAS README: W2A8 8×/2× over cuBLAS W16A16 (GEMV/GEMM) | Type-aware layout transformation matched to memory-hierarchy granularity |
| Marlin (Frantar et al., PPoPP'25; arXiv:2408.11743) | W4A16, A10 | About 3.9× over FP16 up to batch 16–32 (ideal 3.87×); up to 2.8× end-to-end in vLLM | Asynchronous global loads, offline-reshuffled weights, pipelined dequantization hidden behind memory |
| Atom (Zhao et al., MLSys'24; arXiv:2310.19102) | W4A4 INT4 TC, RTX 4090 | Up to 7.7× throughput vs FP16, 2.5× vs W8A8; kernel 3.5× vs FP16 at batch 128 | Mixed precision with outlier channels; fused dequantization |
| QServe (Lin et al., MLSys'25; arXiv:2405.04532) | W4A8KV4, A100 / L40S | 1.2–2.4× (A100) and up to 3.5× (L40S) over TensorRT-LLM; 2.5–2.9× over Atom and QuaRot | Keeps the *main loop on INT8 tensor cores*. W4A4 per-group dequantization on CUDA cores costs 20–90%, because FP32 CUDA-core throughput is only about 2% of INT4 tensor throughput |
| LUT-GEMM (Park et al., ICLR'24; arXiv:2206.09557) | BCQ 3-bit weights | 2.1× token latency vs OPTQ, OPT-175B on one GPU | Table lookup of precomputed partial products; no dequantization |
| T-MAC (Wei et al., EuroSys'25; arXiv:2407.00088) | 1–4-bit on CPU | Kernels up to 6.6× (average 3.6×) over llama.cpp; 30 tok/s single-core BitNet-3B on M2 Ultra | Bit-serial lookup tables replacing multiplies |
| Stream-K (Osama et al., PPoPP'23; arXiv:2301.03598) | GEMM scheduling, A100 | Up to 14× over data-parallel CUTLASS, up to 6.7× over cuBLAS, across 32,824 shapes; largest gains on memory-bound / awkward shapes | Split the aggregate K-loop iterations evenly over SMs instead of output tiles |
| CUTLASS grouped GEMM (docs) | Any | — | Persistent kernel looping over a tile scheduler across many problems in one launch |
| EVT (Chen et al., ASPLOS'24) | Epilogue fusion | 1.26–3.1× training speedups | Compiler-generated epilogue visitor trees plugged into CUTLASS main loops |
| FlashAttention-2 / -3 | FP16/BF16/FP8 | FA-2 up to 73% of A100 peak; FA-3 FP16 up to 740 TFLOPS (75% of H100), FP8 close to 1.2 PFLOPS | GEMM with an online reduction fused so the score matrix is never written to memory |

**Blackwell consumer and workstation (sm_120) evidence** (non-peer-reviewed; flagged).
- A public repository (waynehacking8/blackwell-tensorcore-kernels) measured an RTX PRO 6000 Blackwell **Max-Q** at M = N = K = 8192:
  - FP16 mma: about 243 TFLOPS.
  - FP8: 502 TFLOPS (91% of cuBLASLt FP8).
  - Packed block-scaled MXFP4 (`kind::mxf4`): 988 TFLOPS.
  - FP4 in 8-bit containers (`kind::f8f6f4`): 518 TFLOPS, i.e. FP8 rate.
- Those are about 55–57% of rate probes measured under a 300 W cap (FP16 ≈ 440, FP8 ≈ 880, MXFP4 ≈ 1,761).
- **Implication:** the 2× FP4 rate requires the packed block-scaled instruction. Scales may be fixed at 1.0.
- NVFP4 uses 16-element blocks with E4M3 scales plus a per-tensor FP32 scale, about 4.5 bits per value. MXFP4 uses 32-element blocks with E8M0 scales (NVIDIA blog).

**Format availability.**
- NVIDIA forum (community, unverified): s4 MMA compiles to INT8-IMMA emulation sequences on sm_90 and sm_100, but to native `IMMA.8832.S4.S4` on sm_75–89. Claims of hidden INT4-via-FP4 on Blackwell are unverified.
- Chen (arXiv:2608.11693, 2026) audits B300 (sm_103a): the `tcgen05` INT8 kind is not exposed, CUTLASS skips INT8 UMMA for 103a, and vLLM ships no Blackwell INT8 GEMM. INT8 may be architecturally present but not practically available.
- **Design consequence:** build the receiver's GEMM layer over an abstract "small-integer operand" type. Lower it to:
  - INT4 or INT8 on Ada;
  - FP4 E2M1 (or MXF4/NVF4 with unit scales) or INT8 on sm_120;
  - b1 AND+popc where binary data suffice.
- E2M1 represents {0, ±0.5, ±1, ±1.5, ±2, ±3, ±4, ±6}, so ternary or {±1, ±2, ±3, ±4} templates are *exact* in FP4. With FP32 accumulation, integer sums stay exact up to 2^24 **[derived from the format definitions]**.

---

## 6. Neural / learned receivers (including contradictory evidence)

**DeepRx** (Honkala, Korpi & Huttunen, IEEE TWC 20(6) 2021; arXiv:2005.01494).
- A fully convolutional receiver from frequency-domain samples to soft bits that outperforms traditional receivers on 3GPP channels.
- The authors attribute the gain mainly to exploiting the constellation structure of data symbols, i.e. implicit data-aided channel estimation. Gains are largest with sparse pilots (DeepRx MIMO).

**NVIDIA neural receiver for 5G NR MU-MIMO** (Cammerer et al., arXiv:2312.02601) and the real-time follow-up (Wiesmayr et al., arXiv:2409.02912).
- Less than 1 dB from an LMMSE channel-estimation plus K-best detection baseline, at much lower compute.
- Under 1 ms inference with TensorRT on an A100; the latency constraint costs under 0.7 dB.
- Open source: NVlabs/neural_rx.

**Neural decoding limits.**
- Gruber et al. (arXiv:1701.07738) state the curse of dimensionality directly: an N = 100, rate-1/2 code has 2^50 codewords. Only structured codes generalize from partial training, and experiments were limited to N ≤ 64.
- Neural BP with learned Tanner-edge weights (Nachmani et al., arXiv:1706.07043) improves short BCH codes by up to about 0.9 dB at high SNR. That is a refinement of BP, not a replacement for ML search.
- He, Wu & Wang (arXiv:2206.06025) find neural demodulators and decoders only *approach* ML on small problems.

**Weak-signal amateur modes.** Kozlenko et al. (arXiv:2502.19097) built a CNN JT65A demodulator. It sits about 1.5 dB *short* of the theoretical non-coherent MFSK limit (abstract). I found no FT8 or WSPR neural decoder study.

**Gravitational waves** (MLGWSC-1: Schäfer et al., PRD 107, 023021, 2023; arXiv:2209.11146). The best ML search reached up to 95% of matched-filter sensitive distance on Gaussian noise but about 70% on real O3a noise, at 1/month false-alarm rate.

**Assessment.**
- Learned receivers are GEMM-heavy and run at high tensor utilization (TensorRT).
- The evidence says they are competitive with classical receivers when the classical model is mismatched or expensive (sparse pilots, MU-MIMO). They trail optimal matched filtering when the waveform model is known and noise is Gaussian-like.
- For a known-waveform weak-signal receiver they are best treated as candidate *pre-screens* or interference classifiers. Their miss probabilities must be validated, which is the same requirement as Nitz et al.'s screening.
- This is not contradictory evidence against GEMM-izing the model-based receiver. Rather, it shows that tensor hardware productivity comes from making the *model-based* search GEMM-shaped.

---

## 7. Integer tensor cores for correlation, detection and decoding in communications

- **Massive MIMO detection:** Dabah, Ltaief, Rezki, Alouini & Keyes (SC22 poster) used half precision for a GPU detector: 1.7× on A100 without accuracy loss, and 4× on four A100s against single-precision single-GPU. Tensor-core impact on massive MIMO was described as "not studied yet". I found no peer-reviewed tensor-core sphere decoder or brute-force ML detector with utilization figures.
- **ML decoding as a matrix product:** Ly & Soljanin (arXiv:2510.21414, 2025) express universal ML (list) decoding of any block code as one vector–matrix product over a precomputed codebook matrix, followed by an argmax. Batching received words turns this into a GEMM. It is particularly effective for short, high-rate codes. No GPU implementation.
- **LDPC/polar decoding on GPU** (CUDA cores): LDPC about 4 Gbps with 87 µs latency on Titan RTX (Tarver et al. 2021); claims of 10 Gbps with early termination (MDPI 2022); polar BP above 1 Gbps. I found no tensor-core LDPC or polar decoder.
- **Quantum-code BP+OSD decoder** (Markidis, Pennati & Peng, arXiv:2610.28867, 2026):
  - Uses CUDA Graphs, retires converged samples, queues the remainder, and uses mixed FP16/FP32 plus INT8.
  - INT8 message storage halves amortized decode time on RTX 5080 with negligible logical-error increase.
  - RTX 5080 reaches 91% of GH200 throughput with 8.9× shorter median latency.
  - A search snippet attributes to this paper a finding of only modest gains from 1-bit MMA (AND+popc) for binary algebra on A100. I did not see that finding in its abstract.
- **5G PHY:** NVIDIA Aerial/cuPHY uses GPUs for LDPC and MIMO, but I found no published tensor-core utilization numbers.

**Assessment.** No published communications system demonstrates high integer-tensor-core utilization on hypothesis search. The ingredients exist: ML decoding as a GEMM, 1-bit and INT4 correlation from radio astronomy, and fused-peak matched filtering. The Matrix Modem would be early.

---

## 8. Practical utilization lessons

1. **Small FFTs and small GEMMs are bandwidth- or latency-bound.**
   - The FFT's arithmetic intensity grows only as log N (§2).
   - TCBF shows memory-bound behavior for batch 256 × 1024 × 1024 × K, and near-optimal only at thousands per side.
   - Below the ridge (about 655 ops/byte INT8 and 1,311 INT4 on the 4090) the tensor units idle.
2. **Template streaming needs enough reuse [derived].**
   - A template matrix T of M×K at b bits per element, applied to N data columns held on-chip, has intensity ≈ 2MNK / (MK·b/8) = 16N/b ops/byte.
   - For INT4 on the 4090: 4N ≥ 1,311, so N ≳ 330 timing or window columns must reuse each streamed template.
   - INT8 on the 4090 needs the same N ≳ 330 (2N ≥ 655).
   - FP4 on the 5090 needs N ≳ 235.
   - Alternatives are templates generated on-chip (phase ramps, codeword structure) or templates resident in L2 (72–96 MB on these parts).
3. **Outputs must be reduced in the epilogue [derived].**
   - Writing int32 scores costs 4MN bytes, which caps intensity at about K/2 (for K = 1024, ≈ 512 ops/byte, below the INT4 ridge).
   - Both FlashAttention and Nitz et al. avoid ever materializing the score matrix.
4. **Instruction choice matters.**
   - mma.sync reaches ≥ 95% on A100 and 4090 but averages 62.9% on Hopper; wgmma or tcgen05 are needed there.
   - WMMA held GH200 to about 65% in ccglib.
   - Markidis et al. (arXiv:1803.04014) report up to 83 TFLOPS (66%) on V100 in mixed precision, while a WMMA-based batched GEMM reached about 4 TFLOPS (abstract; the implementation details are not given there).
5. **Dequantization and conversion inside the main loop is fatal.** QServe measured 20–90% overhead, and the PFB was conversion-bound. Pack and convert once, upstream or in the epilogue.
6. **Launch overhead.**
   - About 10 µs per launch is "not uncommon" (Madgraph CUDACPP, arXiv:2510.05392).
   - NVIDIA documentation suggests 1.5–3× from CUDA Graphs for many small kernels, and 1.1–1.4× for large ones.
   - An NVIDIA blog measured 3.8 µs per kernel including overhead for 2.9 µs kernels.
   - PyGraph found 82 of 416 graphs gave no speedup, because parameter-copy overhead dominated.
   - Persistent and grouped kernels avoid launch overhead entirely.
7. **Host–device transfer.**
   - xGPU overlapped transfer with compute.
   - For a 2.4 k complex samples/s stream, raw data is about 19.2 kB/s in complex fp32 **[derived]**, so bandwidth is irrelevant. Per-call latency and synchronization dominate, which favors persistent kernels with device-side queues.
8. **Load balance across irregular shapes:** Stream-K or grouped persistent scheduling (§5).
9. **Benchmark honestly.**
   - Zero inputs overstate sustained throughput by up to 18% (H800, power cap).
   - Kernel speedups shrink end-to-end: Marlin 3.9× in the kernel versus 2.8× in vLLM. Atom's 7.7× was not reproduced in QServe's comparison, which found Atom 20–25% slower than TensorRT-LLM's W4A16 and W8A8 baselines.

---

## Design patterns that transfer

1. **Put hypotheses on M, time on N and integration length on K.**
   - Express scoring as C[h, t] = Σ_k T[h, k]·X[k, t].
   - Build X as an implicit Hankel/Toeplitz (sliding-window) operand in shared memory, the "implicit GEMM" convolution trick, so the tiny stream gives N timing columns without memory expansion.
   - Precedents: TCC, TCBF, Ly & Soljanin ML-as-GEMM.
2. **Fuse the detector into the epilogue.** Compute |·|² (and noncoherent combining), then max, top-k or threshold into a candidate list, in registers. Only candidates leave the SM (Nitz et al. 2026; FlashAttention; EVT).
3. **Hierarchical, statistically controlled screening.** Use a cheap low-precision or short-integration pass over *all* hypotheses with a declared miss probability, then refine survivors in higher precision (Nitz et al.: 19–26×). This preserves the full hypothesis space while spending little precision on it.
4. **Factor the hypothesis space instead of flattening it.**
   - Frequency × timing via Monarch/Kronecker DFT-GEMMs (FlashFFTConv).
   - Structured codebooks via FWHT (HadaCore).
   - Channel subspaces and drift via SVD or low-rank bases (GstLAL, about 10× fewer filters).
   - Dechirp or derotate common structure before the bank (ratio-filter dechirping).
5. **Precondition in high precision, then quantize to 1–4 bits.**
   - Examples: Doppler before sign in TCBF; F-engine float then integer X-engine in radio astronomy.
   - In the noise-dominated regime the losses are about 0.55 dB at 2-bit and about 1.96 dB at 1-bit (van Vleck/Cooper).
   - Keep templates exact in INT4 or E2M1. Accumulate in int32 or fp32.
6. **Arbitrary precision via bit-planes.** Express 1×2-bit or 2×3-bit products as batched binary GEMMs with shift-add recombination in the epilogue (APNN-TC). Choose the plane count by measured sensitivity per unit of cost.
7. **Complex arithmetic on real tensor cores.**
   - Interleaved real/imag with a sign-swapped B copy (katgpucbf), or planar layout with four real GEMMs (TCBF).
   - 1-bit complex as ±1±i, using AND+popc on Hopper and newer.
8. **Keep the main loop tensor-native.** No CUDA-core dequantization per MAC (QServe, LUT-GEMM). Do packing and transposes once per data block, upstream (TCBF pack/transpose kernels).
9. **Persistent, grouped or Stream-K scheduling plus CUDA Graphs** for many medium-sized problems, double-buffered shared memory, and device-side work queues. Retire converged or rejected hypotheses and compact the survivors (Markidis et al. 2026).
10. **Auto-tune tile shapes per device and format** (TCBF with Kernel Tuner), and size problems to tile multiples to avoid sawtooth losses.
11. **Use the cache tier as the reuse lever.** Tile batches so K² correlations reuse 2K cached spectra (Nitz). Size template working sets to fit L2 (72–96 MB on target GPUs).

## Pitfalls and negative results

- **About 1% utilization is the documented default** for bit-level work mapped onto word-based units (Li & Su). The receiver's earlier "<1%" is the expected failure mode of scalar per-hypothesis loops and small FFTs, not a hardware ceiling.
- **FFT and Hadamard on tensor cores give 1.0–3× at best.** The ASTRON slide rates the FFT a poor fit, and FP64-FFT emulation is projected to hit an integer-epilogue wall. Do not expect transforms to carry utilization.
- **Small or batched GEMMs are memory-bound** (TCBF small shapes), with sawtooth losses at non-multiple sizes.
- **Hopper 1-bit XOR runs at about 6% of peak** (emulated). Use the AND form, which costs 2× instructions.
- **mma.sync on Hopper is about 63% of peak, and WMMA held GH200 to about 65%.** The architecture-specific instruction family matters.
- **INT4 is not portable.** It is native only on sm_75–89; emulated on sm_90/100 (forum report); absent from the RTX 5090 specification; and INT8 `tcgen05` is unavailable on B300. FP4 in 8-bit containers runs at FP8 rate on sm_120.
- **GeForce half-rate FP32 accumulate** for FP16 and FP8 (4090 and 5090 tables; measured 178.9 vs 357.6 TFLOPS on 4090). FP4 on 5090 is listed at full rate with FP32 accumulate.
- **Dequantization and type conversion in the main loop** costs 20–90% (QServe), and the PFB was conversion-bound.
- **Output write-back** caps intensity at about K/2 ops/byte unless reduced on-chip **[derived]**.
- **CUDA Graphs are not a universal fix.** Parameter-copy overhead left about 20% of graphs (82 of 416) without gains (PyGraph).
- **Peak claims vs sustained work:** zero-data microbenchmarks show up to 18% optimism; kernel-only speedups are not end-to-end (Marlin, Atom vs QServe).
- **Brute force can lose to structured algorithms.** Tree dedispersion, Taylor dedoppler, FFA and FWHT do asymptotically less work. GEMM wins only when the ratio of effective throughputs exceeds the ratio of op counts. hyperseti reports the brute-force kernel is not the bottleneck there; I/O and peak finding are.
- **Learned receivers trail optimal matched filtering** when the model is known: about 1 dB from LMMSE+K-best; 70% sensitive distance on real GW noise; JT65A CNN 1.5 dB short. Neural ML decoding does not scale to long codes.
- **Low-bit losses assume noise-dominated samples.** Strong interferers break the van Vleck efficiencies, so isolate and whiten first.

---

## Implications for a ~2.4 k sample/s stream with a huge hypothesis space

- **Different shape from astronomy.** Radio astronomy has huge data and modest weights; this receiver is the reverse, so intensity must come from template reuse and on-chip template generation, not from data volume.
- **Concrete targets [derived]:**
  - At least about 256–512 timing or window columns per template fetch when templates stream from DRAM at 4 bits.
  - Integration K in the hundreds to thousands (cheap, because it does not change intensity).
  - Epilogue reduction to candidates.
  - M (hypotheses) of at least several thousand per launch, so each GEMM sits in the "few thousand per side" regime where TCBF reached 50–85%.
- **The latency vs N trade-off.** Accumulating more windows before scoring raises N and therefore intensity, but adds latency. This trade-off should be predeclared, consistent with the project's bounded-evidence policy.
- **Per-device format selection:**
  - Ada (4090, 4060 Ti): INT4 or INT8 IMMA via mma.sync.
  - Blackwell sm_120 (5090, RTX PRO 6000): block-scaled FP4 with unit scales, or INT8; INT4 is not listed.
  - Binary AND+popc where 1-bit data and templates suffice.
  - Fall back to INT8 or FP16 with explicit reporting.
- **No prior art validates the sensitivity of a fully tensor-core receiver.** Each low-precision stage needs complete-receiver sensitivity checks on challenging cases, as the project's own contracts require.

## Gaps (searched, not found)

- Tensor-core dedispersion, GNSS acquisition, LoRa dechirp, preamble or sync detection, or LDPC/polar decoding with utilization numbers.
- INT8 or INT4 FFT accuracy studies.
- Per-GPU percent-of-peak figures for the TCC paper (A&A full text was blocked).
- The SPIE 2026 FFT + batched-GEMM dedispersion paper.
- FT8 or WSPR neural decoders.
- Measured INT4 throughput on sm_120.

---

## References (with access notes)

1. Romein, J. W. "The Tensor-Core Correlator." A&A 656, A52 (2021). doi:10.1051/0004-6361/202141896. *Abstract via Crossref and Semantic Scholar; aanda.org full text returned 403.* README: https://git.astron.nl/RD/tensor-core-correlator (raw README read).
2. Oostrum, L., Veenboer, B., Rook, R., Brown, M., Kruizinga, P., Romein, J. W. "The Tensor-Core Beamformer: A High-Speed Signal-Processing Library for Multidisciplinary Use." arXiv:2505.03269 (2025). https://arxiv.org/abs/2505.03269. *Full HTML read.* Code: https://git.astron.nl/RD/recruit/ccglib
3. katgpucbf XB-engine design notes. https://katgpucbf.readthedocs.io/en/latest/_sources/xbgpu.design.x.rst.txt (read); https://github.com/ska-sa/katgpucbf
4. Yu, W., Romein, J. W., et al. "Prospects of GPU Tensor Core Correlation for the SMA and the ngEHT." Galaxies 11, 13 (2023). doi:10.3390/galaxies11010013. *Full text read.*
5. Clark, M. A., La Plante, P. C., Greenhill, L. J. "Accelerating Radio Astronomy Cross-Correlation with Graphics Processing Units." IJHPCA (2013); arXiv:1107.4264. *Abstract and snippets.*
6. van Nieuwpoort, R. V., Romein, J. W. "Correlating Radio Astronomy Signals with Many-Core Hardware." Int. J. Parallel Prog. (2011); arXiv:1702.00844. *Snippet.*
7. Klages, P., et al. "GPU Kernels for High-Speed 4-Bit Astrophysical Data Processing." arXiv:1503.06203 (2015). *Listing and snippet.*
8. Adámek, K., Novotný, J., Armour, W. "A polyphase filter for many-core architectures." Astron. Comput. (2016); arXiv:1511.03599. *Abstract.*
9. ASTRON slides, "GPU and network innovations" (2024). https://indico.astron.nl/event/378/contributions/3387/attachments/2401/3252/Madrid-GPUandNetworkInnovations.pdf *Snippet.*
10. EVLA Memo 88 (quantization efficiency). https://library.nrao.edu/public/memos/evla/EVLAM_88.pdf ; Thompson, Moran & Swenson, *Interferometry and Synthesis in Radio Astronomy*, Ch. 8. *Secondary snippets; standard results.*
11. Li, B., Cheng, S., Lin, J. "tcFFT: Accelerating Half-Precision FFT through Tensor Cores." arXiv:2104.11471 (2021). *Abstract.*
12. Sorna, A., et al. "Optimizing the Fast Fourier Transform Using Mixed Precision on Tensor Core Hardware." HiPCW 2018. doi:10.1109/HiPCW.2018.8634417. *Abstract.*
13. Durrani, S., et al. "Accelerating Fourier and Number Theoretic Transforms using Tensor Cores and Warp Shuffles." PACT 2021. doi:10.1109/PACT52795.2021.00032. *Abstract.*
14. Fu, D. Y., Kumbong, H., Nguyen, E., Ré, C. "FlashFFTConv: Efficient Convolutions for Long Sequences with Tensor Cores." arXiv:2311.05908; ICLR 2024. Blog: https://hazyresearch.stanford.edu/blog/2023-11-13-flashfftconv *Blog read.*
15. Wu, S., et al. "TurboFFT." PPoPP 2025; arXiv:2412.05824. *Abstract.*
16. Matsuoka, S. "FP8 is All You Need (Part 2): Full-FP64 3-D FFT on FP8-Generation Tensor Cores." arXiv:2606.23698 (2026). *Abstract; projections only.*
17. Agarwal, K., et al. "HadaCore: Tensor Core Accelerated Hadamard Transform Kernel." arXiv:2412.08832; https://pytorch.org/blog/hadacore/ *HTML read.*
18. Ashkboos, S., et al. "QuaRot." arXiv:2404.00456 (NeurIPS 2024); FlatQuant arXiv:2410.09426. *Snippets.*
19. Nitz, A. H., Kacanja, K., Capano, C. D. "Peak Performance in Matched Filtering: Fused Correlation and Statistically Controlled Screening on CPUs and GPUs." arXiv:2611.17229 (Nov 2026). *Abstract.*
20. Nitz, A. H., Kacanja, K., Soni, K. "Beyond FINDCHIRP … Ratio-Filter Dechirping." PRD 113, 082003 (2026); arXiv:2601.18835. *Abstract.*
21. Cannon, K., et al. "Singular value decomposition applied to compact binary coalescence gravitational-wave signals." PRD 82, 044025 (2010); arXiv:1005.0012. *Abstract snippet.*
22. Huang, Y.-J., Hanna, C., et al. "Scalable matched-filtering pipeline for gravitational-wave searches of compact binary mergers." arXiv:2410.16416; SGNL arXiv:2511.04730. *Abstracts.*
23. SPIIR on Maxwell GPUs. arXiv:1702.02256. *Snippet.*
24. Adámek, K., et al. "Improved Acceleration of the GPU Fourier Domain Acceleration Search Algorithm." arXiv:1711.10855; Dimoudi, S., et al. arXiv:1804.05335. *Snippets.*
25. "Accelerating Dedispersion using Many-Core Architectures." arXiv:2311.05341. *Snippet.*
26. turboSETI GPU (SciPy 2022 proceedings). https://proceedings.scipy.org/articles/majora-212e5952-004 ; hyperseti docs https://hyperseti.readthedocs.io/en/latest/pipeline/dedoppler.html *Snippets.*
27. cuNCC seismic template matching (SCEC). https://central.scec.org/publication/7711 *Snippet.*
28. GNSS acquisition background: "Comparison Framework of FPGA-based GNSS Signals Acquisition Architectures" (EPFL, https://infoscience.epfl.ch/record/181412) and a 2013 GPU acquisition study in *Artificial Satellites*, doi:10.2478/arsa-2013-0005 (https://reference-global.com/article/10.2478/arsa-2013-0005). *Snippets only; authorship not verified here.*
29. Wang, L., Chen, D., Wang, D., Wang, C. "Acceleration of radar echo coherent accumulation system based on half-precision format and tensor core." IMCEC 2022. doi:10.1109/IMCEC55388.2022.10019890. *Abstract page.*
30. Li, A., Su, S. "Accelerating Binarized Neural Networks via Bit-Tensor-Cores in Turing GPUs." IEEE TPDS 32(7):1878–1891 (2021); arXiv:2006.16578. *Abstract.*
31. Feng, B., Wang, Y., et al. "APNN-TC: Accelerating Arbitrary Precision Neural Networks on Ampere GPU Tensor Cores." SC'21. doi:10.1145/3458817.3476157; arXiv:2106.12169. *Full PDF text extracted.*
32. Wang, L., et al. "Ladder: Enabling Efficient Low-Precision Deep Learning Computing through Hardware-aware Tensor Transformation." OSDI'24. https://www.usenix.org/conference/osdi24/presentation/wang-lei ; BitBLAS https://github.com/microsoft/BitBLAS *Slides and README snippets.*
33. Frantar, E., et al. "MARLIN: Mixed-Precision Auto-Regressive Parallel Inference on Large Language Models." PPoPP'25; arXiv:2408.11743. *Abstract and README.*
34. Zhao, Y., et al. "Atom: Low-bit Quantization for Efficient and Accurate LLM Serving." MLSys'24; arXiv:2310.19102. *Abstract and snippets.*
35. Lin, Y., et al. "QServe: W4A8KV4 Quantization and System Co-design." MLSys'25; arXiv:2405.04532. *Snippets.*
36. Park, G., et al. "LUT-GEMM." ICLR 2024; arXiv:2206.09557. *Abstract.*
37. Wei, J., et al. "T-MAC: CPU Renaissance via Table Lookup for Low-Bit LLM Deployment on Edge." EuroSys'25; arXiv:2407.00088. *Abstract.*
38. Osama, M., Merrill, D., Cecka, C., Garland, M., Owens, J. D. "Stream-K." PPoPP'23; arXiv:2301.03598. *Abstract.*
39. CUTLASS grouped scheduler docs. https://docs.nvidia.com/cutlass/latest/media/docs/cpp/grouped_scheduler.html ; EVT (ASPLOS'24) via https://research.colfax-intl.com/epilogue_visitor_tree/ *Snippets.*
40. Dao, T. "FlashAttention-2." arXiv:2307.08691; Shah, J., et al. "FlashAttention-3." arXiv:2407.08608. *Abstracts.*
41. Markidis, S., et al. "NVIDIA Tensor Core Programmability, Performance & Precision." IPDPSW 2018; arXiv:1803.04014. *Abstract.*
42. Luo, W., et al. "Dissecting the NVIDIA Hopper Architecture through Microbenchmarking and Multiple Level Analysis." arXiv:2501.12084 (2025). *HTML read (first 100k characters).*
43. NVIDIA Ada GPU Architecture whitepaper; NVIDIA RTX Blackwell GPU Architecture whitepaper (images.nvidia.com). *PDF text extracted.* RTX PRO 6000 page: https://www.nvidia.com/en-us/products/workstations/professional-desktop-gpus/rtx-pro-6000/
44. waynehacking8/blackwell-tensorcore-kernels (GitHub; not peer-reviewed). https://github.com/waynehacking8/blackwell-tensorcore-kernels *README read.*
45. NVIDIA Developer Forum, "Does Blackwell support INT4 native?" https://forums.developer.nvidia.com/t/does-blackwell-support-int4-native/326513 *Community claims, unverified.*
46. Chen, T.-R. "Spec Sheets Are Not Kernels: An ISA- and Source-Level Audit of INT8 Availability on NVIDIA Blackwell Ultra." arXiv:2608.11693 (2026). *Abstract.*
47. NVIDIA blog, "Introducing NVFP4 for Efficient and Accurate Low-Precision Inference." https://developer.nvidia.com/blog/introducing-nvfp4-for-efficient-and-accurate-low-precision-inference *Snippets.*
48. NVIDIA DL CUDA Graph docs. https://docs.nvidia.com/dl-cuda-graph/cuda-graph-basics/quantitative-benefits.html ; PyGraph arXiv:2503.19779; Madgraph CUDACPP arXiv:2510.05392. *Snippets.*
49. Honkala, M., Korpi, D., Huttunen, J. "DeepRx: Fully Convolutional Deep Learning Receiver." IEEE TWC 20(6) (2021); arXiv:2005.01494. *Abstract.*
50. Cammerer, S., et al. "A Neural Receiver for 5G NR Multi-user MIMO." arXiv:2312.02601; Wiesmayr, R., et al. arXiv:2409.02912; https://github.com/NVlabs/neural_rx *Snippets.*
51. Gruber, T., Cammerer, S., Hoydis, J., ten Brink, S. "On Deep Learning-Based Channel Decoding." CISS 2017; arXiv:1701.07738. *Snippet.*
52. Nachmani, E., et al. "Deep Learning Methods for Improved Decoding of Linear Codes." IEEE JSTSP (2018); arXiv:1706.07043. *Snippet.*
53. He, B., Wu, Z., Wang, F. "Rethinking: Deep-learning-based Demodulation and Decoding." arXiv:2206.06025. *Abstract.*
54. Kozlenko, M., et al. "Software demodulation of weak radio signals using convolutional neural network." arXiv:2502.19097. *Abstract.*
55. Schäfer, M. B., et al. "MLGWSC-1: The first Machine Learning Gravitational-Wave Search Mock Data Challenge." PRD 107, 023021 (2023); arXiv:2209.11146. *Snippets.*
56. Dabah, Ltaief, Rezki, Alouini, Keyes. SC22 research poster (low-precision multi-GPU massive-MIMO detection). https://sc22.supercomputing.org/proceedings/tech_poster/tech_poster_pages/rpost160.html *Snippet.*
57. Ly and Soljanin. "Universal Maximum Likelihood (List) Decoding via Fast Vector-Matrix Multiplication." arXiv:2510.21414 (2025). *Snippets.*
58. Markidis, S., Pennati, L., Peng, I. "Engineering Low-Latency, High-Throughput Quantum Decoders on NVIDIA GPUs." arXiv:2610.28867 (2026). *Abstract.*
59. Ootomo, H., Ozaki, K., Yokota, R. "DGEMM on Integer Matrix Multiplication Unit." arXiv:2306.11975. *Title and snippet only.*

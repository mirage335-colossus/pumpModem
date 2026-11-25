# Matrix-form receiver algorithms and accelerator utilization

Research date: 24 November 2026. This chapter asks how a Matrix receiver can
put its work on matrix hardware, how much useful work a weak link actually
contains, and where extra computation converts into sensitivity. It draws on
two literature reviews made for this pass (tensor-core signal processing; GEMM-
shaped decoding and detection) and on `calc/workload.py`. Nothing here has been
implemented or timed.

## Findings

1. **A well-designed single-stream receiver has little useful work.** Even with
   every FFT replaced by a dense matrix product, long-symbol tone banks with
   Doppler-subspace and drift hypotheses need about 0.1 tera-operations per
   second of signal; keyed-tile reception with blind time/frequency search needs
   about 0.03. That is 0.002–0.01% of an RTX 4090's INT4 peak. A CPU can do the
   regular-grid version with FFTs. The live receiver's computational ceiling is
   therefore not the binding constraint for the recommended waveforms; the
   channel is.
2. **Work large enough to fill a petaop GPU exists in two places.**
   - *Codeword-level joint correlation* over coherent blocks: k = 24 bits per
     7.7 s block with 50 residual offsets is about 16 TOPS (1.2% of a 4090);
     k = 28 about 260 TOPS (20%); k = 32 about 4 POPS (more than any target).
     Expected gain is roughly 0.5–2 dB on coherent paths and near zero on
     spread paths.
   - *Batched Monte Carlo simulation*: 10⁴ independent trials of a keyed-tile
     receiver with blind search is about 330 TOPS per simulated second (25% of a
     4090), before channel synthesis. This directly attacks the user's slow
     simulation problem.
   Wide blind acquisition, many simultaneous keys or streams, and multi-category
   robustness banks add further work.
3. **Earlier ~1% utilization was the expected failure mode, not a hardware
   ceiling.** Bit-level and per-hypothesis work on word-based units commonly sits
   near 1% (binarized-network studies); small FFTs and small batched GEMMs are
   memory- or launch-bound; tensor-core FFTs gain only 1.1–3×. High utilization
   has been demonstrated only for large dense GEMMs: the Tensor-Core Beamformer
   reaches 50–85% of peak (3,080 useful 1-bit TOPS on an A100; 1,400 on an RTX
   4000 Ada) once matrices are a few thousand elements per side.
4. **The receiver's matrix shape is unusual: tiny data, huge templates.**
   Arithmetic intensity must come from reusing each streamed template across
   about 256–512 timing or window columns (INT4 on a 4090 needs N ≳ 330;
   FP4 on a 5090 N ≳ 235), or from generating templates on chip, and from
   reducing scores to candidates inside the kernel (writing int32 scores caps
   intensity at about K/2 operations per byte).
5. **Brute force buys dB in specific places only:** coherent codeword-level
   correlation on coherent paths, joint synchronization-and-decoding instead of
   sync symbols (0.7–3 dB of overhead recoverable while the search stays below
   about 2^20–2^30 cells), near-ML decoding of short codes (about 1 dB), and
   diversity combining. It does not beat ML, the channel model, or coherence
   limits, and each 2^10 more hypotheses costs about 0.3 dB of threshold.

## The receiver as a quantized matrix pipeline

A Matrix receiver can be written as a fixed sequence of matrix stages with
elementwise epilogues, the same structure as quantized neural-network
inference but with every weight known from the waveform and key:

| Stage | Operation | Shape | Precision | Notes |
| --- | --- | --- | --- | --- |
| 0. Front end | Filter, downconvert, decimate to ~2.4 k complex samples/s; blank or clip impulses; scale; quantize | streaming | float on CPU, then 4-bit codebook | Interference removal *before* 4-bit quantization keeps the van Vleck efficiencies valid |
| 1. Projection | Windows × templates: tones, Doppler-subspace bases, keyed tiles, decoys | [hypotheses × K] · [K × windows] | 4-bit operands, s32/FP32 accumulate | Windows as an implicit sliding (Hankel) operand in shared memory; template generated on chip where possible (phase ramps, keystream expansion) |
| 1e. Epilogue | Complex magnitude, Karhunen–Loève weights, subspace energy, requantize to 4 bits, per-window reductions, top-k or threshold | in registers | 4-bit out | Only candidates and soft metrics leave the SM |
| 2. Combination | Sequences/codewords × per-symbol metrics; or coherent block correlation | [codewords × N·M] · [N·M × blocks] | 4-bit | One-hot codeword matrices are binary; coherent block correlation uses complex projections |
| 3. Decoding | Batched OSD re-encoding and metric GEMMs; or LDPC message passing | [test patterns × k] · [k × (n−k)] | binary / 4-bit | OSD at order 4 is within ~0.1 dB of the normal approximation at (128,64) |
| 4. Commit | Ordered public slot commit, quiet-slot scoring, integrity | CPU | — | Unchanged repository contracts |

Complex products map onto real tensor cores by interleaving real and imaginary
parts with a sign-swapped, permuted copy of one operand (as in the MeerKAT
XB-engine) or as four planar real GEMMs (Tensor-Core Beamformer).

### Factorize before flattening

Dense matrices are only worth their extra arithmetic when no cheaper exact
structure exists:

- **Regular frequency grids:** an FFT is M/log₂M cheaper than a dense DFT-GEMM.
  Tensor-core FFTs (tcFFT, Monarch/four-step as in FlashFFTConv) gain 1.1–3×
  over CUDA-core FFTs; FlashFFTConv's end-to-end wins come from fusing the
  transform into GEMM-shaped stages. Use FFTs for tone banks unless the grid is
  irregular or keyed.
- **Walsh/Hadamard codebooks:** fast Hadamard transforms or Kronecker-factored
  16×16 GEMMs (HadaCore pattern, 1.0–1.4× over CUDA-core FHT).
- **Template banks with low-rank structure:** SVD compression (as in
  gravitational-wave searches, about 10× fewer filters) and dechirping before
  the bank.
- **Keyed pseudo-random templates** (family B in the [design chapter](05-waveform-and-parameter-design.md)):
  no fast transform exists, so dense matrix work is the efficient algorithm,
  not a wasteful one.

### Hierarchical, statistically controlled screening

The most transferable system pattern is from 2026 matched-filtering work
(Nitz, Kacanja & Capano): fuse correlation with peak reduction so only
candidates leave the processor, and screen all hypotheses with a cheap pass
whose thresholds meet a declared false-dismissal probability, then refine
survivors. It reported 19–26× over library-FFT baselines on CPUs and GPUs. For
Matrix: a coarse 4-bit pass over the whole hypothesis space with predeclared
miss probability, then finer passes (longer coherence, more drift hypotheses,
joint codeword correlation) on survivors. Every stage's miss probability
enters the complete-receiver sensitivity.

## Sizing the work

Dense-equivalent operation counts per second of received signal
(`calc/workload.py`; 8 real operations per complex multiply-accumulate):

| Workload | TOPS per signal-second | % of RTX 4090 INT4 | % of RTX PRO 6000 FP4 | Assumptions |
| --- | ---: | ---: | ---: | --- |
| W1 tone bank, T = 0.16 s, as dense DFT | 2.7×10⁻⁵ | 2×10⁻⁶ | 1×10⁻⁶ | 384 samples, 352 bins, 4 starts/symbol |
| W2 long symbols, T = 10.24 s, dense DFT, rank-4 subspace, 9 drifts | 0.125 | 0.009 | 0.006 | 45,056 half-bin frequencies |
| W3 keyed tiles, guided search | 5×10⁻⁴ | 4×10⁻⁵ | 3×10⁻⁵ | 25 Hz tiles, 64 dims, 64 data + 64 decoy templates, 10,500 offsets |
| W3b keyed tiles, blind search | 0.033 | 0.0025 | 0.0016 | 640,000 time/frequency/drift offsets |
| W4 codeword correlation, k = 24 | 16 | 1.2 | 0.8 | 7.68 s coherent block, 50 residual offsets |
| W4 codeword correlation, k = 28 | 258 | 20 | 13 | same |
| W4 codeword correlation, k = 32 | 4,120 | 310 | 200 | same (beyond any target) |
| W5 simulation, 10⁴ × W3b | 328 | 25 | 16 | channel synthesis not counted |

On the Radeon 780M (33 TOPS IU4 via HIP) W2 is 0.4% and W4 at k = 20 about 3%;
the Vega 5650U has no matrix or integer-dot hardware, so these iGPU workloads
fall to the CPU there.

The consequence for the user's "near 100% busy" goal: for live reception it is
reachable only by deliberately adding hypotheses that buy sensitivity
(codeword-level correlation, deeper drift/coherence banks, blind acquisition)
or by serving many streams, keys or bands. For development it is reachable
immediately through batched simulation. Utilization should be reported next to
useful complete-receiver progress, never instead of it.

## Where extra computation buys sensitivity

Ranked by expected value on this problem (evidence from the GEMM-algorithm
review unless noted):

1. **Longer coherent windows with frequency/drift search on quiet paths.**
   The limits chapter shows 5–20 dB between 160 ms symbols and optimal
   durations at low C/N0. Hypothesis counts grow ∝ T (frequency), T² (drift)
   and T³ (jerk); with FFTs over frequency the cost is about T³·log T. This is
   the largest available gain and the clearest use of computation, but it
   saturates once residual errors are below about 1/(2T) in frequency and 1/T²
   in drift, and it is conditional on the channel (drift-limited windows of
   1.5–40 s, see the channel chapter).
2. **Joint sync-and-decode instead of sync symbols.** WSJT modes spend 0.7–3 dB
   on sync energy; JT65 deep search failed by sync below about −28 dB, not by
   decoding. An ideal joint detector pays +0.33/+0.63/+1.17 dB for 2^10/2^20/2^40
   extra cells (k = 100, 10⁻³).
3. **Coherent multi-symbol / codeword correlation.** Recovers part of the
   3.6–4.5 dB per-symbol noncoherent loss of short-symbol MFSK on coherent
   paths. Published: FT8 3-symbol blocks +0.7 dB and FT4 +1.6 dB on AWGN, about
   0.1 dB at 10 Hz spread. With long symbols (recommended), much of this gain is
   already captured within each symbol.
4. **Near-ML decoding of short blocks.** OSD order 3 → 4 at (128,64) is worth
   about 0.4 dB and is batched-GEMM-shaped; CRC-aided polar list and
   tail-biting convolutional list decoders reach similar points but are not
   GEMM-shaped.
5. **Multi-hypothesis channel banks.** Several Doppler ranks, drift ranges and
   mode-line models evaluated in parallel and mixed, protecting against
   category mismatch in a link that cannot be reconfigured.

## Where it does not

- Beyond ML for a given detector output (best short-code decoders are already
  0.07–0.1 dB from the normal approximation).
- Against the channel: the no-CSI fading gap (≈ 3.8 dB vs 0.3 dB at k = 100),
  quasi-static fades and outages, Doppler-spread capacity limits.
- Coherent hypotheses longer than the coherence time (FT4's block detection at
  10 Hz: ≈ 0.1 dB).
- Ever-larger search breadth (≈ 0.3 dB per 2^10).
- Dense GEMMs where a fast transform exists.
- Learned receivers: GEMM-native and fast, but in the literature found they
  trail model-based matched filtering when the waveform is known (≈ 1 dB from
  strong 5G baselines; ≈ 70% of matched-filter sensitive distance on real
  gravitational-wave noise; a JT65A CNN ≈ 1.5 dB short of the noncoherent MFSK
  limit). Useful as interference classifiers or pre-screens with validated
  miss rates.
- Sparse regression codes and coded compressed sensing for a single short
  low-SNR message: no finite-length evidence of an advantage over OSD/list
  codes; at low rate they reduce to structured orthogonal signaling.

## Kernel engineering lessons that transfer

1. Hypotheses on the M dimension, time/windows on N, integration length on K;
   sliding windows as an implicit Hankel operand.
2. Keep the main loop in the tensor-native operand type; no per-multiply
   dequantization (QServe measured 20–90% overhead for CUDA-core dequantization;
   a GPU polyphase filter bank was conversion-bound).
3. Fuse magnitude, weighting, requantization and candidate reduction into the
   epilogue; never materialize full score matrices.
4. Persistent or grouped kernels with device-side queues for many medium
   problems; Stream-K scheduling for awkward shapes; CUDA Graphs help small
   kernels but not universally.
5. Auto-tune tile shapes per device and format; size problems to tile
   multiples (sawtooth losses otherwise).
6. Use L2 (72–96 MB on the target GPUs) to hold template working sets.
7. Benchmark with random data, sustained clocks and end-to-end receiver
   progress; kernel speedups shrink end-to-end (Marlin 3.9× kernel, 2.8×
   in vLLM).

## Simulation throughput

Development speed was named as a major problem. The batch dimension of
independent Monte Carlo trials is the easiest way to fill matrix hardware:

- Batch many independent channel realizations, keys and noise seeds through
  the same projection kernels; the template operand is shared and reused
  across the batch, which is exactly the reuse the roofline needs.
- Keep each realization's channel state continuous across the whole message
  (fade correlation, drift, delayed tails). Restarting channel state every
  block inflates the number of independent fades and hides long fades.
- Use a cheap *projection-level* simulator for screening (generate per-symbol
  sufficient statistics directly from the channel law, as the capacity
  calculations here do), and the full sample-level native path for
  qualification. The two must be checked against each other.
- Channel synthesis (Gaussian Doppler processes, multi-mode delays) can itself
  be written as FIR filtering in matrix form, but random-number generation and
  noise are bandwidth-bound; measure them separately from the receiver.

## Repository constraints that carry over

The [native processing contract](../../../docs/development.md#native-processing-and-integration)
keeps modem processing in C++, with Python limited to orchestration and
analysis; the [CPU allocation contract](../../../docs/development.md#receiver-cpu-allocation)
sets the default worker budget to twice the logical hardware threads; and the
[GPU precision guide](../../../docs/gpu-precision-controls.md) requires explicit
fallback reporting. A Matrix implementation would live under
`_appraisal/algorithms-matrixModem/` as a standalone native library and must
not modify Fast, Robust or Legacy code.

## Sources

- Romein, J. W. "The Tensor-Core Correlator." A&A 656, A52 (2021), doi:10.1051/0004-6361/202141896 (abstract; repository README read).
- Oostrum, L. et al. "The Tensor-Core Beamformer." arXiv:2505.03269 (2025) (full).
- katgpucbf XB-engine design notes (MeerKAT Extension) (read).
- Li, B., Cheng, S., Lin, J. "tcFFT." arXiv:2104.11471 (abstract); Fu, D. Y. et al. "FlashFFTConv." arXiv:2311.05908, ICLR 2024 (blog read); Agarwal, K. et al. "HadaCore." arXiv:2412.08832 (read).
- Nitz, A. H., Kacanja, K., Capano, C. D. "Peak Performance in Matched Filtering." arXiv:2611.17229 (2026) (abstract); Cannon, K. et al. SVD template compression, PRD 82, 044025 (2010).
- Li, A., Su, S. "Accelerating Binarized Neural Networks via Bit-Tensor-Cores in Turing GPUs." IEEE TPDS 32(7), 2021; Feng, B. et al. "APNN-TC." SC'21; Lin, Y. et al. "QServe." MLSys 2025; Frantar, E. et al. "MARLIN." PPoPP 2025; Osama, M. et al. "Stream-K." PPoPP 2023.
- Luo, W. et al. "Dissecting the NVIDIA Hopper Architecture through Microbenchmarking." arXiv:2501.12084 (RTX 4090 mma.sync measurements).
- Franke, S., Somerville, B., Taylor, J. "The FT4 and FT8 Communication Protocols." QEX 2020 (block detection, OSD).
- Fossorier, M., Lin, S. "Soft-decision decoding of linear block codes based on ordered statistics." IEEE TIT 41(5), 1995; Coşkun, M. C. et al. "Efficient error-correcting codes in the short blocklength regime." Physical Communication 2019 (as summarized in the GEMM review).
- Joseph, A., Barron, A. R. (2012); Rush, C., Greig, A., Venkataramanan, R. (2017); Fengler, A., Jung, P., Caire, G. (2021) — SPARC/AMP and unsourced random access (GEMM review).
- Honkala, M. et al. "DeepRx." IEEE TWC 2021; Cammerer, S. et al. arXiv:2312.02601; Schäfer, M. B. et al. "MLGWSC-1." PRD 107, 023021 (2023); Kozlenko, M. et al. arXiv:2502.19097.

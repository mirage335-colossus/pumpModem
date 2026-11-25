# Tensor Core use and FFT alternatives

24 November 2026 · Research note

The subsequent [repeated-FSK receiver experiment](tensor-reception-results.md)
measured Tensor Core activity in cuBLAS codeword scoring and across the full
replay, following separate cuFFT projection. The discussion of unmeasured
activity below refers to the earlier projection comparison.

## Answer in brief

**A cuFFT call is not evidence that a workload is using Tensor Cores, and the
available Matrix probe did not measure Tensor Core activity.** The CUDA 13.5
cuFFT reference documents FP16, FP32, FP64, and (in its extended API) BF16
transform types; it does not document a Tensor Core execution guarantee. The
current cuFFT page has no Tensor Core match. FP16/BF16 storage or API type,
lower time, high GPU busy percentage, and a vendor peak-TFLOP figure each fail
to establish Tensor Core instructions or their utilization. The probe used
`cufftExecC2C`, the single-precision complex transform API. The internal
instruction path was not inspected. NVIDIA's general CUDA guide names cuFFT
among example CUDA-X libraries for Tensor Core programming. It gives no
execution or utilization guarantee for this plan. [cuFFT API and precision
limits](https://docs.nvidia.com/cuda/cufft/), [CUDA compute capability
guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html).

This answer is deliberately narrower than “cuFFT never uses Tensor Cores.”
NVIDIA's public API promises transforms, not a fixed internal kernel, and
cuFFT can change its algorithms by shape, version and GPU. Current public
documentation neither proves Tensor Core use for a specific plan nor proves
that no internal kernel uses those units. Settle that question for the
actual plan with kernel-level instruction evidence and counters on an
authorized, representative device. On the existing matched regular-grid
case, the measured answer is instead operational: FP32 cuFFT plus a gather
was 4.1–5.1 times faster than the tested block-scaled NVFP4 projection.
The counter attempt failed with `ERR_NVGPUCTRPERM`, so its Tensor Core
activity is unknown.

## What the APIs and papers establish

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Current cuFFT documentation | Public transform types, API, size/stride limits, planning and batching behavior. FP16 is restricted to powers of two; BF16 uses a mix of single- and BF16-precision operations. | That any given transform dispatches to Tensor Cores, or achieves a specified fraction of Tensor peak. |
| cuFFTDx | A device-side FFT library can inline block/thread FFTs into a caller kernel and fuse adjacent work, potentially avoiding global-memory traffic. It supports FP16, FP32 and FP64 descriptions. | Tensor Core use; the API describes FFT precision as the underlying value/compute type and makes no Tensor Core guarantee. Fusion and less I/O can improve useful latency without tensor arithmetic. |
| tcFFT (Li, Cheng & Lin, 2021) | Tensor Core FFT is feasible: a custom half-precision batched 1D/2D design reported 1.24× average for 1D and 1.10–3.03× for tested 2D cases against cuFFT on A100; V100 results differed. It used fragment-level element manipulation plus a data-layout redesign. | A stock cuFFT feature, Blackwell performance, arbitrary streaming correlation, robust phase precision, or near-peak Tensor utilization for this modem. |
| FlashFFTConv (ICLR 2024) | A factorization can map long FFT-convolution workloads to matrix multiply units; authors report large gains over their software baselines for long sequence convolution. This is recent evidence against assuming all FFT-like work must remain scalar/butterfly work. | A radio receiver result. It addresses long filters/sequences and fused convolution; it does not show that a small-latency 160 ms modem hypothesis bank should be converted into a dense DFT or that this beats optimized cuFFT for the same full receiver. |

Sources: [cuFFTDx documentation](https://docs.nvidia.com/cuda/cufftdx/),
[cuFFTDx precision operator](https://docs.nvidia.com/cuda/cufftdx/api/operators.html),
[tcFFT author manuscript](https://arxiv.org/abs/2104.11471), and
[FlashFFTConv, ICLR 2024](https://proceedings.iclr.cc/paper_files/paper/2024/hash/281190d87732639e63bc19dedd7d711b-Abstract-Conference.html).

cuFFT's standard `cufftExecC2C` is explicitly single-precision complex to
complex. Its FP16 extended interface still has power-of-two-only transforms,
no multigpu execution, and other constraints. BF16 is not simply “BF16
Tensor Core”: NVIDIA explicitly says its FFT combines single- and
BF16-precision arithmetic. The value types say what the API accepts, not
which internal instruction runs. cuFFTDx is useful to test when fusion and
shared-memory/register ownership matter, but its documented precision list
and per-thread/block FFT mapping are not evidence of Tensor Core arithmetic.

The tcFFT paper is a meaningful counterexample to an overly categorical
claim that Tensor Cores cannot accelerate FFT. It is also evidence of the
engineering cost: FFT butterflies, twiddles, shuffles and complex
permutations do not directly fill matrix multiply tiles; their method
explicitly manipulates Tensor Core fragments and reorganizes memory. Its
reported comparison is half-precision, shape-dependent, on V100/A100-era
hardware. A follow-on implementation must revalidate error and receiver
decisions at low SNR, under carrier offset and fading, before using that
route for coherent modem evidence.

## Relevant experiment and Robust measurements

### Matrix regular-grid projection probe

The narrow probe ran on a full RTX PRO 6000 Blackwell Server Edition, SM120,
with CUDA 12.8. It compared batched 2,048-point complex FP32 cuFFT plus a bin
gather with the repository's unchanged block-scaled E2M1 NVFP4 projection
and FP32 accumulation. The 2,048 point input contained 512 acquired samples
and 1,536 zeros: zero extension refined bin spacing to 1.5625 Hz; it did not
extend observation time or add information. All requested bins were gathered
from the FFT output. Across batch sizes 16, 128, and 512 and 47 or 1,409 bins,
cuFFT took 5.6–10.8 µs and the NVFP4 pack-and-project path took 26.4–44.4 µs.
Thus cuFFT won by 4.11–5.07× for this regular grid. The result includes
resident-input projection work; it excludes plan creation, upload and
readback. Those exclusions mean it is not a complete receiver comparison.

The displayed cuFFT spot checks agreed with the double-precision scalar
oracle within `5×10⁻⁵` absolute complex magnitude at reported precision.
The NVFP4 route had 7.8–13.3% relative RMS difference from cuFFT on these
synthetic inputs. Nsight Compute attached, but counter collection failed with
`ERR_NVGPUCTRPERM`; no dynamic Tensor Core instruction/utilization result
exists. The NVFP4 implementation's data type and FP32 accumulator establish
its declared path, not measured activity. CUDA event times establish
elapsed kernel sequence time, not instruction utilization. Full run geometry,
commands and remaining limits are in the [projection probe record](../../../_appraisal/algorithms-matrixModem/projection-probe/README.md).

### Existing Robust evidence is a shape caution, not a Matrix forecast

The repository's full Blackwell results make the throughput gap concrete.
A dense resident 8192 × 8192 × 4096 NVFP4 cuBLASLt operation achieved
1.375 PFLOP/s and 72–75% Tensor activity, while a small 288 × 96 × 3072
operation on the same library achieved 11.69 TFLOP/s. These use dense
2MNK operation counts, E2M1 with unit UE4M3 scale, FP32 accumulation, and
resident data. The first is a large throughput-saturating synthetic GEMM;
the second illustrates the effect of small shapes. Neither is a cuFFT
measurement or a receiver service rate. The underlying comparison is recorded
in [validation](../../../docs/validation.md#blackwell-arithmetic-and-receiver-residency--22-november-2026).

On a separate matched Robust correlation component, batched FP32 cuFFT took
0.07885 ms for 1,350,784 scored cells, against 3.321 ms for one thread per
hypothesis and 4.083 ms for one warp per hypothesis. Those direct mappings
counted 208 times the nominal FFT arithmetic for the same output; both
matched every output to the FFT path and checked sampled FP64 references.
The comparison supports factoring shared regular frequency structure rather
than expanding it into one direct dot product per hypothesis. It does not
show that cuFFT uses Tensor Cores, compare a custom Tensor Core FFT, or
resolve nonuniform Doppler, delay rank processing, acquisition, decoding or
complete receiver costs. Same source record as above.

## Direct matrix products versus FFT projections

An all-frequency complex projection of a length-`N` window at `F` bins can
be written as a dense matrix product. In real arithmetic expansion, one
observation against `F` frequency vectors costs roughly `8NF` scalar
floating operations under the two-flop convention for real multiply-add.
For `N=512`, `F=1409`, that is about 5.77 million operations per window,
versus O(`N_fft log N_fft`) for the FFT plus gather, with `N_fft=2048` at the
same 1.5625 Hz grid. A Tensor Core GEMM may deliver
very high throughput for sufficiently large, well-shaped batches, but the
dense method spends O(`NF`) operations to compute a regular transform whose
FFT shares factorization work. Padding `F` or batching simulated cases can
improve tile fill/utilization while increasing work and memory; this is not
an automatic gain in live receiver throughput or energy efficiency.

A direct GEMM becomes more persuasive when the dictionary is genuinely
nonuniform or each output is a distinct, useful matched hypothesis, when
candidate banks amortize upload/packing, and when the full search is large
enough to occupy the device. Candidate banks might enumerate bounded
frequency error, drift, timing, and a small delay/Doppler channel subspace.
The algorithm should first exploit common structure (FFT/PFB, recurrence,
or low-rank factorization) and use matrix products for residual templates
that cannot share the transform. Count only semantically necessary candidate
work in “useful TFLOP/s.”

The user's current-received-versus-previous-received correlation is a
different matrix opportunity. For `B` new windows, `M` retained reference
windows, and `N` complex samples each, a complex cross-product matrix is
approximately a `[B, 2N] × [2N, 2M]` real GEMM and generates `B×M` scores.
That can expose Tensor Core work if `B`, `M`, and `N` are large. But the
data-dependent matrix alone does not identify a symbol: repeated clean
transmit structure, timing/phase alignment and a public or secret schedule
are needed to interpret a peak. In independent noise, multiplying two
noisy observations adds noise-product terms; under fading, oscillator drift,
frequency hopping or a changing multipath channel, an old raw waveform may
become a mismatched template. A matrix full of historical comparisons can
therefore have impressive activity while spending compute on ambiguous or
invalid hypotheses. Compare against a keyed pilot/template and a noncoherent
or channel-marginalized detector, with fractional frequency/timing and
delayed-tail coverage.

## How to establish Tensor Core execution on a follow-on route

Record these separately for each named device, plan, dtype, shape and library
version:

1. **API and source contract:** logical input/output and internal declared
   precision; normalization; every conversion, quantizer, scale and
   accumulator; plan parameters; padded work; input residency; batch geometry.
2. **Compiled route:** exact kernel names and binary/library hashes. For
   custom kernels, inspect SASS/PTX for the expected MMA instruction family.
   A source API name (`cublas`, `cufft`, `wmma`) is not final instruction
   evidence.
3. **Dynamic activity:** use an authorized hardware counter/tool to sample
   datatype-specific Tensor instructions or Tensor pipeline activity, with
   profiler version, collection scope, replay mode and permissions. Also
   report SM busy and memory bandwidth separately. If the access is denied,
   say activity is unknown; do not replace it with peak rate divided into
   event time.
4. **Practical score:** matched output/error and decision results at
   challenging SNR, offsets, delay spreads and changing paths; device-event
   timing separated into projection, gather/epilogue, packing, transfer and
   preparation; then complete receiver deadline/backlog, CPU fallback and
   memory. Compare against FFT and a reference detector with identical
   candidate coverage.

Peak Tensor TFLOP/s uses a specific format and counting convention. FFT
nominal FLOP/s commonly uses a different operation model; a high or low
ratio between them is not a Tensor Core utilization meter. Likewise,
“GPU-utilization 100%” can mean the device has pending work while matrix
units are idle. Report device, architecture/SKU, clocks/power state, tool and
counter permissions, instruction path, tensor-active fraction and SM/memory
occupancy as distinct fields. Do not infer work rate from GPU model names or
marketing TOPS.

## Design decision implication

For the proposed regular tone grid, retain batched cuFFT/gather as the
baseline because it is simple, exact to the tested precision, and currently
won the matched Blackwell timing. This is not a conclusion that cuFFT
maximizes Tensor Core throughput. First settle the modem's useful hypothesis
set and channel model; then compare (a) cuFFT plus shared epilogue, (b)
cuFFTDx/fused kernels for small windows and local reductions, (c) a custom
Tensor Core FFT/factorization such as the tcFFT family, and (d) Tensor Core
GEMM for residual nonuniform templates or coded-bank decoding. A recent
long-sequence convolution design makes (c) scientifically credible, but
does not remove the need for modem-specific matched tests.

The live one-stream throughput target and large offline/search batches should
be reported independently. If a live receiver supplies too little useful
work to saturate Tensor Cores, deliberately repeating/padding valid
hypotheses may cost airtime, energy, latency or sensitivity without improving
message decisions. A good architecture should use the device when real
uncertainty creates work, and preserve a bounded CPU or simpler-GPU fallback
when it does not. None of the cited studies or receipts proves a final modem
implementation, sensitivity, successful decode probability, or near-peak
Tensor utilization for Matrix Modem.

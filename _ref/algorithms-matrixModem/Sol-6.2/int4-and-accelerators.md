# Four-bit arithmetic and accelerator feasibility

This chapter answers a narrow question: what do the stated GPU targets actually
offer for four-bit matrix work, and what does an “INT4-only” receiver imply for
the products, accumulators, and code around a matrix multiply? It is a literature
and architecture assessment, not a hardware measurement or a selected Matrix
Modem implementation. NVIDIA instruction support, vendor product claims, API
availability, and measured complete-receiver performance are separate kinds of
evidence.

## Main finding

NVIDIA lists the Ada and Blackwell targets as supporting signed or unsigned
four-bit integer Tensor Core input operands. Its documented warp-level
`mma.sync` operation takes packed s4/u4 operands and accumulates into s32. This
is a promising native matrix multiply route on the RTX 4090, RTX 4090 Laptop,
RTX 4060 Ti, RTX 5090, and RTX PRO 6000 Blackwell. It does **not** implement an
all-INT4 multiply-accumulate. A dot product with a useful dynamic range needs a
wider accumulator, and the documented integer accumulator is 32-bit. Blackwell
SM100-class targets add the newer `tcgen05.mma` programming model, with `i8`
as its documented integer kind and a separate FP4 path. The RTX 5090 and RTX PRO
6000 Workstation are SM120-class targets. They use register-based MMA families
and lack the `tcgen05`/TMEM path. Their documented INT4 input route therefore
requires a separate real compiler and profiler qualification. [NVIDIA's
Blackwell pipeline target list](https://docs.nvidia.com/cuda/cublasdx/performance_guide_api_blackwell.html).

Consequently, two requirements have to be made precise before implementation.
“INT4” can mean that post-downconversion samples and template coefficients are
quantized and stored as signed four-bit integers, or it can forbid any wider
intermediate and accumulator. The former matches the normal definition of
low-bit Tensor Core GEMM and is supported by vendor hardware. The latter is not
what the documented INT4 matrix instruction does. Replacing its exact s32
accumulator with a wrapping or saturating four-bit sum changes the likelihood
score and can destroy long coherent evidence. Exact accumulation using four-bit
limbs is theoretically possible, but needs explicit carry handling and more
operations. It should be treated as a separate algorithm, with no presumed
Tensor Core speed advantage.

## What the accelerator documentation establishes

NVIDIA's CUDA Programming Guide lists Tensor Core input formats by compute
capability. INT4 is listed for 8.9, 10.0, 10.3, and 12.x. These correspond to
the requested Ada GeForce GPUs and the requested Blackwell GeForce and RTX PRO
GPUs. The table is a supported-input-format table, not a promise that a
particular library call, shape, accumulator, or kernel uses the Tensor Core
path. CUDA's guide recommends CUDA-X libraries or CUTLASS for Tensor Core
programming. It also allows inline PTX, which is the documented low-level path
if a needed operation is missing from the higher-level libraries.

The PTX ISA specifies integer `mma.sync` shapes including `m8n8k32`,
`m16n8k32`, and `m16n8k64` with s4/u4 operands and s32 accumulator/output. Packed
four-bit values therefore enter a matrix instruction in wide packed registers.
The product is not a scalar four-bit multiply; the instruction applies the
hardware's packed-lane decode and dot-product operation. The instruction also
has layout, alignment, warp-collective, and architecture rules. Correctly
writing a GEMM and getting a fast, high-utilization kernel are separate tasks.

Blackwell `tcgen05.mma` moves matrix data through shared memory or tensor memory
and issues an asynchronous matrix operation. PTX 9.5 documents integer
`tcgen05` as `kind::i8`. The Blackwell CUTLASS guide lists legacy integer types
for `tcgen05` as int8/uint8 and lists separate low-precision floating formats,
including FP4. PTX's `mma.sync` section continues to define four-bit integer
operands. Thus the portable documented proposition is “Blackwell supports
Tensor Core INT4 input arithmetic,” not “Blackwell INT4 uses `tcgen05`.” The
RTX 5090 and workstation PRO 6000 share compute capability 12.0, while
workstation/server Blackwell compute-capability variants and the rest of the
Blackwell product family must not be conflated.

| GPU target | Vendor architecture evidence | Four-bit matrix implication | Limits to retain |
| --- | --- | --- | --- |
| RTX 4090 desktop | Ada, CC 8.9, fourth-generation Tensor Cores | Documented packed INT4 Tensor Core inputs; legacy warp-level MMA is an obvious supported route | 24 GB GDDR6X and desktop clocks/power are specific to this SKU |
| RTX 4090 Laptop | Ada family, generally CC 8.9 | Same instruction family where the exact SKU reports CC 8.9 | “RTX 4090 Laptop” is a family across OEM power configurations; do not transfer desktop timing or throughput |
| RTX 4060 Ti 16 GB | Ada, CC 8.9, fourth-generation Tensor Cores | Same supported INT4 instruction class as the 4090 | Preserve the 16 GB variant; its smaller compute and memory subsystem matter for batching |
| RTX 5090 | Blackwell, CC 12.0, fifth-generation Tensor Cores | Guide lists INT4 inputs on CC 12.x. Qualify register-based INT4 `mma.sync` on SM120 | Published 3352 AI TOPS does not identify the INT4 rate |
| Full RTX PRO 6000 Blackwell Workstation Edition | Blackwell, CC 12.0, fifth-generation Tensor Cores | Same SM120 format support. Qualify the compiled INT4 path on this device separately | Verify workstation edition and full 96 GB device allocation. Its 4000 AI TOPS claim is effective FP4 with sparsity |
| Ryzen 5 PRO 5650U Radeon Graphics | AMD lists the 5650U CPU, while the integrated GPU is a Vega-generation device commonly identified as Vega 7 | No NVIDIA Tensor Core or RDNA3 WMMA path. Use a separately qualified CPU/vector route or a native shader route that does not claim matrix-core execution | Product name “Radeon Graphics” alone does not identify the exact accessible GPU, memory share, or driver path |
| Core i9-13900H Iris Xe / UHD-class graphics | Intel Xe-LP generation architecture for the Iris Xe Graphics iGPU class | Intel's Xe-LP architecture table reports no FP16 DPAS matrix throughput. It does not provide XMX matrix engines, which arrive in later discrete Arc architectures | Determine the actual integrated device and driver. 13900H includes Iris Xe graphics, but “similar era UHD” can denote multiple configurations |
| Newer integrated GPUs | Families vary substantially | Some later iGPUs have dedicated matrix paths; others rely on vector ALUs or expose cooperative-matrix APIs over different hardware | Gate by queried device features and compiled instruction route, not by a generic “new iGPU” label |

These are capability expectations, not device qualifications. NVIDIA's official
compute-capability page lists the RTX 4090 and 4060 Ti as 8.9 and the RTX 5090
and RTX PRO 6000 Blackwell as 12.0. NVIDIA's consumer 4090/4060 Ti pages identify
Ada and fourth-generation Tensor Cores. Its RTX 5090 product page advertises
3352 AI TOPS and says Blackwell/fifth-generation Tensor Cores, but does not
label that number as INT4. The RTX PRO 6000 workstation datasheet says 4000 AI
TOPS and its footnote specifies effective FP4 TOPS with sparsity. Neither
number substantiates an INT4 peak. A “4-bit AI” marketing number is not enough
to assign an integer format or arithmetic path.

### Accelerator and API distinction

`int4_t` in CUTLASS is a packed signed four-bit storage and operand type. It
does not require a four-bit accumulator. The CUTLASS Blackwell GEMM guide
describes the `tcgen05` integer route as 8-bit; the general CUDA compute guide
continues to list INT4 as a supported Tensor Core input type on CC 12.0.
These statements are consistent if a Blackwell library uses a different
instruction family for INT4. The final generated machine code, the exact CUTLASS
kernel family, its architecture guard, and an instruction/activity counter are
the evidence needed to say which path ran. A type name in C++ is not enough.

For Ada, the documented warp-level INT4 MMA instruction is directly relevant.
The same general instruction is available in the PTX ISA targeting later
architectures, but the newest architecture can favor different instruction
families and scheduling. No academic or vendor material reviewed here
demonstrates that a particular Matrix Modem shape on these exact GPUs will
reach near-peak operation throughput. PTX establishes semantics and target
availability. CUTLASS establishes a reusable C++ kernel framework. Neither
guarantees efficient tiling for the eventual receiver's small and irregular
matrices.

The four-bit floating-point formats do not satisfy an integer-modem contract.
Blackwell NVFP4 uses an E2M1 FP4 codebook and scaling, while signed integer
four-bit values represent the integers −8 through 7. Floating exponent/mantissa
interpretation, quantization scales, subnormals, zeros, and products differ.
Mapping integer values into FP4 to obtain advertised FP4 performance is not
integer arithmetic and could alter template correlations and null statistics.
An unofficial or undocumented hardware encoding trick is not a supported
portable design basis.

## Accumulator range and the meaning of “only INT4”

For signed input values `a,b ∈ [−8,7]`, a product ranges from −56 through +64.
The positive bound is asymmetric because `−8 × −8 = 64`. For one real-valued
dot product of K such terms, a conservative no-overflow bound for signed
32-bit accumulation is:

```text
64 K ≤ 2,147,483,647
K ≤ 33,554,431
```

This is a deliberately worst-case bound. The signal quantizer, zero centering,
observed value distribution, and correlation structure could produce much
smaller actual partial sums, but they cannot be assumed to do so for a
correctness guarantee. A complex product usually has two real products in each
real or imaginary accumulator, giving a conservative +128 per complex sample
and a bound of 16,777,215 samples for signed s32. Energy, bias removal, window
accumulation, and the subsequent score can add their own ranges and require
separate analysis. Quantization scale and clipping must also be explicit.

At sample counts typical of symbol correlation, s32 accumulation may be
plentifully safe, and s32 is an efficient native output of integer Tensor Core
MMA. The user's stated rule that software does not use wider precision is
therefore a genuine design constraint, not a detail to wave away. If that rule
is literal for every arithmetic step, alternatives include short independently
scored blocks followed by a carefully specified four-bit score combination,
or representing exact totals in base-16 limbs with carry propagation. Neither
has been tested here. Four-bit limb arithmetic needs sign representation,
carry bounds, matrix-output decomposition, and a way to aggregate the score
without silently using wider arithmetic. It can multiply instruction count and
packing overhead. Saturating or wrapping a four-bit sum is not an equivalent
substitute.

An explicit product contract can instead say: “The post-front-end samples and
templates are quantized to s4; hardware matrix products accumulate exactly in
s32, then downstream evidence uses the selected declared integer representation
and range.” This respects low precision in the high-volume multiply, but it
relaxes the literal prohibition on wider precision. That semantic decision
should be recorded before experiments compare kernels.

## Candidate workload and roofline implications

A matrix formulation has to come from a receiver likelihood or a justified
approximation. One plausible search tile is a bank of H quantized templates,
each with K samples, scored against S candidate sample windows:

```text
T[H,K] × X[K,S] → R[H,S]
nominal dot-product work = 2 H K S integer operations
```

The template matrix can hold frequency, timing, Doppler, or symbol hypotheses.
Rows/windows and candidate hypotheses offer parallel dimensions. This is only a
matrix-shape example. It does not establish that these hypotheses are
independent, that all templates are simultaneously affordable, or that the
resulting statistic is the correct detector under a time-varying channel.

For an illustrative tile with H=4096, K=512, S=128, the work is about 537
million multiply/add operations. Packed s4 storage for T and X is about 1.03
MiB before alignment/padding. Writing every output as s32 adds 2 MiB. Counting
those three global transfers gives about 169 nominal operations per byte,
before cache reuse, tiling, rereads, epilogue, and synchronization. At a
hypothetical dense 1.32 POPS INT4 peak and 1.0 TB/s device-memory bandwidth, a
simple peak roofline break-even would be around 1,320 operations per byte. This
example is therefore memory/traffic sensitive under that deliberately simple
model, even though the matrix math looks large. The 1.32 POPS figure is the
RTX 4090 Ada whitepaper's published INT4 Tensor peak (dense side of its
peak/sparse pair), not a measured modem rate. It should not be compared directly
with the 5090 or PRO's FP4 AI TOPS.

Keeping score tiles resident, accumulating multiple K blocks before writing,
reusing an observation tile across many templates, and reducing the result
inside the kernel can change arithmetic intensity. Conversely, materializing a
large H×S score matrix, generating/packing templates every batch, copying data
between host and GPU, or synchronizing after each tiny symbol can erase the
matrix advantage. INT4 halves operand traffic against INT8, but output traffic
and poor tile utilization can dominate. For this modem the meaningful number is
complete receiver work per second at the required detection/error behavior,
along with the sustained Tensor Core active fraction and all preparation,
launch, transfer, reduction, and memory costs.

Tiny segments are a particularly important risk. A nominal 6.25-symbol/s
configuration corresponds to roughly 160 ms per symbol. If the post-filter
sample rate and useful integration length make K small, one symbol's matrix is
not enough to occupy a large GPU. Batching independent timing/frequency/channel
hypotheses and multiple ready windows is a plausible way to build useful M, N,
and K dimensions, subject to latency and memory limits. The matrix should be
formed early enough to feed parallel work, while the transmitter should expose
enough structured, reusable template work to make receiver simulation cheap.
These are hypotheses to test, not evidence that GPU batching will be faster.

### INT4 alternatives and the portability floor

1. **Native packed s4 MMA with s32 accumulation.** This is the simplest
   documented high-throughput NVIDIA matrix path when the accumulator contract
   permits s32. Evaluate CUTLASS or a small CUDA C++ kernel and inspect generated
   code. Its exact support in higher-level APIs depends on library/version and
   operation shape.
2. **INT4 decomposition onto binary matrix operations.** The two's-complement
   value can be represented by four binary planes with signed positional
   weights. An integer dot can then be recovered from up to 16 pairwise
   bit-plane popcounts and weighted combinations. PTX defines binary
   `mma.sync` operations with AND/XOR and population count. This is an exact
   arithmetic decomposition only if all cross-plane counts, weights, signs,
   and accumulation are preserved. Up to sixteen MMA calls plus reductions
   make it an alternative for devices or APIs lacking packed INT4, not the
   first choice on Ada/Blackwell.
3. **Packed vector arithmetic.** CPU SIMD or shader integer operations can
   extract nibbles, sign-extend, multiply, and accumulate many values per
   machine word. This may be a useful cross-vendor baseline. It does not create
   matrix-core hardware on Vega, Xe-LP, or another GPU without such units.
4. **INT8 or FP4 substitution.** These are distinct data/arithmetic contracts.
   Do not use them under an INT4-only rule, or count their performance as INT4.
   If no supported INT4 route reaches the necessary ceiling, expose that
   constraint instead of changing precision invisibly.

CPU comparisons should use the same quantized samples, template scaling,
likelihood, thresholding, and documented accumulator semantics as the GPU.
The 5650U CPU's AVX2-class path is a natural local baseline, while the 13900H
has a different core mix and instruction support. Report CPU features,
compiler, vectorized kernel, thread count, and affinity. A CPU's high-rate
packed-integer kernel and a GPU's INT4 MMA can be compared in hypotheses
per second, but theoretical FLOPS/TOPS across unlike operations do not decide
the result.

## Requested receiver hardware and realistic interpretation

| Target class | Workload role in qualification | Current evidence limit |
| --- | --- | --- |
| RTX 4090 desktop and laptop | Ada INT4 compatibility reference and representative throughput tests | Same architecture class does not normalize laptop power/clock, memory speed, cooling, or sustained throughput |
| RTX 4060 Ti 16 GB | Lower memory-capacity Ada test; reveal whether batches/workspaces fit and whether the kernel degrades gracefully | The 8 GB model is not a substitute for the requested 16 GB card; GPU product AI TOPS is not INT4 qualification |
| RTX 5090 | Blackwell GeForce functional/throughput test, with a compile-time/runtime check for the actual INT4 path | Product reports 3352 AI TOPS without identifying it as INT4; do not borrow the PRO 6000 figure or instruction use |
| Full RTX PRO 6000 Blackwell Workstation | Preferred large matrix/workspace development device | Must verify full Workstation Edition rather than Max-Q/MIG; published AI TOPS are FP4+sparsity figures |
| Ryzen 5 PRO 5650U Vega iGPU | Native Vulkan/vector fallback and shared-RAM admission check | No documented Vega matrix unit analogous to Tensor Cores or RDNA3 WMMA; physical route still requires Mesa/driver/device evidence |
| Intel i9-13900H Iris Xe | Vulkan/compute fallback and CPU/GPU contention measurement | Xe-LP lacks the DPAS/XMX resources in Intel's architecture comparison; other integrated Intel generations differ |
| Later iGPUs | Capability probe and backend qualification by architecture | No generic matrix-precision claim applies across future vendors/generations |

AMD's RDNA3 ISA documents WMMA operations, and AMD describes WMMA as an RDNA3
Radeon RX 7000 feature. This does not apply backward to Vega in the 5650U.
Intel's Xe-LP architecture table identifies 96 vector engines on the listed
TGL device, and shows both FP16 DPAS throughput and XMX matrix engines as
absent. The same guide lists those resources for Xe-HPG Arc, a different
architecture. The 13900H Iris Xe iGPU is not the later Arc GPU architecture.
Therefore, today's requested iGPUs should be treated as portable compute
targets, not presumed four-bit matrix accelerators. A Vulkan or OpenCL API can
run a shader on them without proving the shader used matrix hardware.

Two newer integrated examples offer a documented route to investigate:

- Radeon 890M (`gfx1150`) and 8060S (`gfx1151`) are RDNA3.5. AMD documents
  `V_WMMA_I32_16X16X16_IU4` for signed or unsigned four-bit operands, selected
  per input, and signed 32-bit accumulator/output. The vendor's HIP builtins
  cover the corresponding architectures. Availability through the chosen OS,
  driver or native shader backend and complete performance still need device
  evidence. [AMD GPU specifications](https://rocm.docs.amd.com/en/docs-7.14.0/reference/gpu-specs.html),
  [RDNA3.5 ISA, Table 33](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna35_instruction_set_architecture.pdf),
  [AMD WMMA builtins](https://rocm-handbook.amd.com/projects/amd-rocm-optimization-guide/en/latest/compiler-builtins/rdna/rdna3-dense-wmma-builtins.html).
- Intel documents XMX INT4/INT2 on Xe2, including the Lunar Lake generation
  that supplies integrated Arc 140V. Its DPAS programming model includes
  sub-byte integer operand encodings. Intel's SPIR-V subgroup extension has
  packed four-bit flags, with multiplication/addition performed at the result
  precision. Check an actual native API, packing and shape for both operands,
  signed accumulation, generated code and shared-memory pressure. A product
  described as Arc or an INT4-weight model alone does not establish this route.
  [Intel Xe2 and Lunar Lake GPU architecture](https://cdrdv2-public.intel.com/824434/2024_Intel_Tech%20Tour%20TW_Xe2%20and%20Lunar%20Lakes%20GPU.pdf),
  [Intel DPAS compiler specification](https://github.com/intel/intel-graphics-compiler/blob/master/documentation/visa/instructions/DPAS.md),
  [Intel SPIR-V extension](https://github.khronos.org/SPIRV-Registry/extensions/INTEL/SPV_INTEL_subgroup_matrix_multiply_accumulate.html).

Runtime feature discovery should identify the device and driver, supported
subgroup/collective-matrix types and shapes, memory type and capacity, and
actual compiled instructions. Both examples keep the accumulator question open
under the strict four-bit requirement.

## Minimum decisive qualification plan

No hardware runs were performed for this report. When a contained appraisal is
authorized, a small test can settle the principal uncertainties before a larger
receiver experiment:

1. Record exact GPU SKU/edition, compute capability, usable memory and whether
   the allocation is full device or partitioned. Record driver, compiler,
   CUDA/CUTLASS version, OS, host CPU, memory, and power/clock environment.
2. Compile and run one deterministic small s4 GEMM and a matched CPU integer
   reference. Include positive and negative extremes, cancellation, all-zero
   rows, padded K, non-multiple tile dimensions, and a K large enough to cross
   block boundaries. Check exact integer result under the chosen accumulator
   contract, and deliberately verify overflow assumptions.
3. Inspect PTX/SASS/disassembly and runtime profiler counters. On Blackwell
   explicitly distinguish the warp-level integer MMA route from `tcgen05` i8
   and FP4. Query counters available on the actual chip first. Nsight Compute
   notes that pipeline metric names vary by compute capability. Nsight Systems
   and DCGM distinguish overall GPU engine/SM activity from Tensor activity.
4. Time the full tile including generation/packing, host-to-device transfer,
   kernel launch, GEMM, score/null reduction, synchronization, and result
   handling. Report H/K/S, padding and bytes, executed instruction/path, Tensor
   active cycles (where supported), SM activity, memory throughput, achieved
   matrix operations, useful complete hypotheses/s, and end-to-end receiver
   service. An overall GPU-busy percentage does not establish Tensor Core
   utilization. Tensor-active percentage does not establish complete-receiver
   speedup.
5. Repeat only the small paired test over shapes that expose launch-limited,
   matrix-compute, and memory/packing-limited regimes, then carry a promising
   shape into the native complete receiver. Preserve the same information
   content, hypothesis set, channel/noise trial and error criteria between CPU
   and GPU. Keep the full receiver decision and quiet-slot/service rules in
   scope.

The right optimization target is increased completed useful receiver work at
the difficult cases. Near-100% Tensor activity is an explicit design objective
for the high-end discrete cards, but it may require redundant candidate work or
batching. Record that utilization separately from speed and receiver service.
The system should not declare success solely because the GPU is busy.

## Sources and scope

Official architectural and programming sources take precedence for capability
claims. The following documents were consulted for this synthesis:

- NVIDIA, [CUDA C++ Programming Guide, Tensor Core supported formats](https://docs.nvidia.com/cuda/cuda-programming-guide/), especially its table of Tensor Core input types by compute capability and recommendation to use CUDA-X libraries/CUTLASS.
- NVIDIA, [Parallel Thread Execution ISA 9.5](https://docs.nvidia.com/cuda/parallel-thread-execution/), sections “Matrix multiply-accumulate operation using `mma` instruction” and “TensorCore 5th Generation Instructions: `tcgen05.mma`”. PTX defines operations and targets; it does not report application performance.
- NVIDIA CUTLASS, [Blackwell SM100 GEMMs](https://docs.nvidia.com/cutlass/latest/media/docs/cpp/blackwell_functionality.html) and [fundamental types](https://docs.nvidia.com/cutlass/latest/media/docs/cpp/fundamental_types.html). CUTLASS describes available template/kernel families and element types; final dispatch still depends on the build and shape.
- NVIDIA, [CUDA GPU compute capability list](https://developer.nvidia.com/cuda/gpus), [RTX 4090 Ada whitepaper](https://images.nvidia.com/aem-dam/Solutions/geforce/ada/nvidia-ada-gpu-architecture.pdf), [RTX 4060 family specifications](https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/rtx-4060-4060ti/), [RTX 5090 specifications](https://www.nvidia.com/en-us/geforce/graphics-cards/50-series/rtx-5090.html), and [RTX PRO 6000 Blackwell Workstation datasheet](https://www.nvidia.com/content/dam/en-zz/Solutions/data-center/rtx-pro-6000-blackwell-workstation-edition/workstation-blackwell-rtx-pro-6000-workstation-edition-nvidia-us-3519208-web.pdf). Marketing AI TOPS are qualified by their footnotes and data types.
- NVIDIA, [RTX 4090 Laptop comparison](https://www.nvidia.com/en-us/geforce/laptops/compare/) and [FP4 inference on GeForce RTX 50 series](https://developer.nvidia.com/blog/nvidia-tensorrt-unlocks-fp4-image-generation-for-geforce-rtx-50-series-gpus/). The product family's laptop power range and lower-level NVFP4 workflow are separate from the desktop 4090 and integer GEMM contract.
- Intel, [Iris Xe GPU architecture optimization guide](https://www.intel.com/content/www/us/en/docs/oneapi/optimization-guide-gpu/2023-0/intel-iris-xe-gpu-architecture.html) and [i9-13900H specifications](https://www.intel.com/content/www/us/en/products/sku/232135/intel-core-i913900h-processor-24m-cache-up-to-5-40-ghz/specifications.html). The architecture table's Xe-LP/HPG/HPC comparison is especially useful for DPAS/XMX differences.
- AMD, [Ryzen 5 PRO 5650U product support/specification page](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-5000-series/amd-ryzen-5-pro-5650u.html), [Vega ISA reference](https://gpuopen.com/wp-content/uploads/2019/11/Vega_7nm_Shader_ISA_26November2019.pdf), [RDNA3 ISA reference](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna3-shader-instruction-set-architecture-feb-2023_0.pdf), and [GPUOpen overview of RDNA3 WMMA](https://gpuopen.com/learn/rgp_1_15/). RDNA3 WMMA is not evidence for Vega support.
- NVIDIA, [Nsight Compute Compute Triage Guide](https://docs.nvidia.com/nsight-compute/ComputeTriage/), [Nsight Compute Profiling Guide](https://docs.nvidia.com/nsight-compute/ProfilingGuide/), and [DCGM profiling metrics](https://docs.nvidia.com/datacenter/dcgm/latest/learn/modules/profiling.html). The guides explain why busy, SM-active, Tensor-active, and throughput metrics are distinct.
- Markidis et al., [“NVIDIA Tensor Core Programmability, Performance & Precision”](https://arxiv.org/abs/1803.04014), 2018. This academic paper provides a useful historical explanation of Tensor Core programming and precision tradeoffs; it covers Volta-era hardware, so it is not evidence of Ada/Blackwell instruction behavior or Matrix Modem performance.

The repository's existing [native implementation options](../../highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md),
[native implementation options and research reconciliation](../../highFidelity-triple_Robust_Modem/Fable-5.2/70-implementation-options.md),
and [GPU selection/measurement contract](../../../docs/triad-performance.md#gpu-selection-for-accelerated-development)
are contextual leads. Their historical Robust Modem measurements and routes
must not be treated as Matrix Modem results. In particular, no RTX 5090, RTX
4090, Vega, or Iris Xe Matrix Modem INT4 kernel was compiled or timed in this
research task.

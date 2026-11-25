# Matrix hardware and four-bit precision

Research date: 24 November 2026. This chapter records what the requested
devices actually provide for 4-bit matrix arithmetic, how much sensitivity
4-bit quantization costs, and how a receiver can keep one numerical contract
across devices. It corrects several points in
[Sol-6.2's accelerator chapter](../Sol-6.2/int4-and-accelerators.md). No
hardware was measured in this pass; peak values come from vendor tables, and
derived values are marked.

## Findings

1. **Consumer and workstation Blackwell has no documented INT4 Tensor Core
   path.** The CUDA Programming Guide's Tensor Core input-type table (checked in
   CUDA 13.1.0 through the current guide) lists INT4 only for compute
   capability 7.5–8.9; 9.0, 10.x, 11.0 and 12.x have none. Both RTX Blackwell
   whitepapers omit the INT4 row entirely. PTX still accepts 4-bit `mma.sync`,
   but on sm_90/sm_100 the compiler emulates it with INT8 IMMA (user-posted
   SASS); no sm_120 SASS was found and must be checked. Sol-6.2's statement
   that INT4 is listed for 10.x and 12.x is a misreading.
2. **On RTX 5090 and RTX PRO 6000 the 4-bit fast path is block-scaled FP4
   (E2M1)** via `mma.sync.m16n8k64 … kind::mxf4` or `kind::mxf4nvf4`, with FP32
   accumulation, at twice the INT8 rate (5090: 1,676 vs 838 TOPS dense; PRO
   6000 Workstation: 2,015 vs 1,008). Unscaled FP4 (`kind::f8f6f4`) runs at the
   FP8 rate. Block scales may be fixed at exactly 1.0.
3. **E2M1 is an exact small-integer codebook.** It represents {0, ±0.5, ±1,
   ±1.5, ±2, ±3, ±4, ±6}; products are exact and, per a bit-accurate model
   (MMA-Sim) and exact-arithmetic work on the RTX PRO 6000, FP32 accumulation of
   integer-valued products is exact while partial sums stay below about 2^24.
   It cannot represent ±5, ±7 or −8.
4. **One 11-level codebook is exact on every 4-bit route.** The integer set
   {0, ±1, ±2, ±3, ±4, ±6} is exact in Ada INT4, Blackwell E2M1, AMD RDNA IU4,
   Intel XMX INT4 and INT8 everywhere. As a Gaussian input quantizer it loses
   0.087 dB (versus 0.052 dB for full INT4 and 0.055 dB for E2M1 with halves).
   This allows one numerical contract with per-device lowering.
5. **Four-bit quantization costs about a tenth of a dB.** Data samples: 0.05–0.09
   dB; tone templates: 0.01–0.04 dB; requantizing per-tone energies to 16
   levels: ≤ 0.1 dB in mutual-information terms. One-bit samples cost 1.96 dB
   and binary templates 0.9 dB. A Monte Carlo check with 4-bit data and 4-bit
   templates measured a 0.058 dB deflection loss.
6. **Proposed interpretation, pending user decision:** pair 4-bit operands with
   wide exact accumulation. The original strict INT4-only constraint remains
   unresolved; this research does not establish that this interpretation
   satisfies it. A single four-bit accumulator cannot preserve long exact
   correlation sums. The listed matrix hardware accumulates in s32 (integer
   routes) or FP32 (FP4 route, exact below 2^24 for integer-valued products).
7. **The integrated GPUs differ sharply.** The Ryzen 5 PRO 5650U's Vega iGPU
   (gfx90c) has no INT8 or INT4 dot instruction at all (the cited Vega ISA
   document describes gfx906); Iris Xe (Xe-LP) has only DP4A INT8; UHD 630 has
   neither. RDNA3/3.5 (Radeon 780M, 890M, 8060S) has WMMA IU4 reachable only
   through HIP; Vulkan cooperative matrix has no 4-bit integer type. Intel Xe2
   has XMX INT4 via SPIR-V Intel extensions.

## Peak dense throughput of the 4-bit-capable route

| Device | 4-bit route | 4-bit dense | INT8 dense | Accumulator | Source |
| --- | --- | ---: | ---: | --- | --- |
| RTX 4090 (128 SM, 2.52 GHz, 450 W) | INT4 `mma.sync` (native IMMA S4) | 1,321 TOPS | 661 TOPS | s32 | Ada whitepaper App. A |
| RTX 4090 Laptop (76 SM, 80–150 W) | INT4 | 453–635 TOPS | 226–318 TOPS | s32 | Derived from per-SM rates and clocks |
| RTX 4060 Ti 16 GB (34 SM) | INT4 | 353 TOPS | 177 TOPS | s32 | Derived; matches NVIDIA's "353 AI TOPS" |
| RTX 5090 (170 SM, 575 W) | FP4 E2M1 block-scaled | 1,676 TFLOPS | 838 TOPS | FP32 | RTX Blackwell whitepaper App. A |
| RTX PRO 6000 Blackwell Workstation (188 SM, 600 W) | FP4 E2M1 block-scaled | 2,015 TFLOPS | 1,008 TOPS | FP32 | RTX PRO Blackwell whitepaper Table 4 |
| RTX PRO 6000 Blackwell Max-Q (300 W) | FP4 | 1,756 TFLOPS | 878 TOPS | FP32 | same |
| Radeon 780M (Ryzen 7 8840U, gfx1103) | WMMA IU4 (HIP) | 33 TOPS | 17 TOPS | s32 | Derived from GPUOpen per-CU rates |
| Radeon 890M / 8060S (gfx1150/1151) | WMMA IU4 (HIP) | ~48 / ~119 TOPS | ~24 / ~59 TOPS | s32 | Derived; clocks unverified |
| Iris Xe (i9-13900H, 96 EU) | none (DP4A INT8) | — | 4.6–9.2 TOPS | s32 | Intel optimization guide; unit convention unresolved |
| Arc 140V (Xe2, Lunar Lake) | XMX INT4 | ~134 TOPS (derived) | 67 TOPS (official) | s32 | Intel Xe2 materials |
| Radeon Vega 7 (Ryzen 5 PRO 5650U, gfx90c) | none | — | none | — | LLVM AMDGPU feature tables |
| UHD 630 (i9-9900K) | none | — | none (no DP4A) | — | Intel ARK and Gen9 documentation |

GeForce cards halve FP16 and FP8 throughput with FP32 accumulation; INT8/INT4
with s32 accumulation and FP4 with FP32 accumulation run at full rate. The PRO
6000 removes the halving. Sustained throughput is lower than these peaks
(measured examples: an optimized NVFP4 GEMM reached 1,666 TFLOPS, 83% of the
PRO 6000 peak, with clocks dropping to 2.15 GHz; community FP4 kernels reached
about 61% on an RTX 5090; random rather than zero inputs reduce sustained
throughput by up to ~18% on power-limited parts).

CPUs: Zen 3 (5650U) and Coffee Lake (9900K) have AVX2 `VPMADDUBSW` only;
Raptor Lake (13900H) P-cores have AVX-VNNI; Zen 4 (8840U) has AVX-512 VNNI.
These provide int8 dot products at roughly 32–64 MAC per core per clock; 4-bit
values must be widened to 8 bits. NPUs (XDNA, Intel NPU) are separate
graph-compiled devices, not drop-in GEMM engines for an in-process receiver.

## Quantization losses

From `calc/quantization.cpp` (`results/quantization.md`).

**Data samples** (weak signal in Gaussian noise; correlation efficiency with
optimal scale):

| Quantizer | Levels | Efficiency | Loss |
| --- | ---: | ---: | ---: |
| 1-bit sign | 2 | 0.637 | 1.96 dB |
| 2-bit | 4 | 0.881 | 0.55 dB |
| 3-bit | 8 | 0.963 | 0.17 dB |
| INT4 two's complement −8…7 | 16 | 0.988 | 0.052 dB |
| E2M1 full (with halves) | 15 | 0.987 | 0.055 dB |
| E2M1 integers {0, ±1, ±2, ±3, ±4, ±6} | 11 | 0.980 | 0.087 dB |
| Integers −4…4 | 9 | 0.969 | 0.135 dB |

These are the classical van Vleck/Cooper efficiencies (1-bit 2/π, 2-bit 0.88),
extended to the codebooks in question. They assume noise-dominated samples:
strong interferers or impulses must be removed or clipped before quantization.

**Tone templates** (I and Q quantized separately): INT4 −7…7 at amplitude 7
loses 0.013 dB; E2M1 0.03–0.05 dB; integers −4…4 0.04 dB; ternary 0.36–0.40
dB; binary 0.92 dB.

**Combined check:** with 4-bit data and 4-bit templates, a Monte Carlo of a
weak tone (−20 dB per sample, 512 samples) gave a deflection ratio of 0.9867
(0.058 dB), matching the product of the separate efficiencies (0.9851).

**Energy requantization** before a decision or decoding GEMM (noncoherent
MFSK, Rayleigh, exact bin likelihoods): a uniform 16-level quantizer keeps the
mutual information within 0.01–0.08 dB (ratio of MI) for M = 16–256 and
Es/N0 = 0–14 dB; equal-probability thresholds are much worse because the signal
information lives in the tail. The theory review found 2-bit energy outputs
cost about 0.95 dB.

## A device-independent numerical contract

**25 November clarification — proposal only.** The earlier statement that
“INT4 only” *must mean* four-bit operands with wide exact accumulation was a
proposed interpretation, not an approved change to the
[original research brief](../../../_appraisal/matrixModem.md#preserved-brief).
Its strict post-isolation precision requirement remains unresolved. The
proposal below requires an explicit decision on wider accumulation and on any
FP4 or INT8 substitution; the [implementation alternatives](../Sol-6.2/int4-and-accelerators.md#int4-alternatives-and-the-portability-floor)
remain relevant. This is not an operative contract.

Proposed contract to keep the code lean while using each device's fastest exact
route:

1. **Operands:** signed small integers from the codebook
   {0, ±1, ±2, ±3, ±4, ±6} (or full INT4 −7…7 where all targets of a build
   support it). Samples, templates, intermediate energies and soft metrics are
   all requantized into this codebook with explicit per-block scale factors held
   outside the matrix operation.
2. **Products and accumulation:** exact. Integer routes accumulate in s32
   (no overflow while K·36 < 2^31, i.e. K < 6×10⁷). The FP4 route accumulates in
   FP32 and is exact while every partial sum, including chained C across
   K-tiles, stays below 2^24: worst case about 466,000 products of magnitude 36.
   A 60 s window at 2,400 complex samples/s is 288,000 real products per
   output, inside the worst-case bound; typical sums are far smaller.
3. **Lowering per device:** Ada → INT4 `mma.sync`; Blackwell SM120 →
   `kind::mxf4nvf4` with unit scales and integer-valued E2M1 operands; RDNA3 →
   WMMA IU4 via HIP; Xe2 → XMX INT4 via SPIR-V Intel; everything else → INT8
   (s32) or an exact CPU path. The fallback must be reported, never silent.
4. **Verification:** a deterministic adversarial test per device and driver
   (maximum magnitudes, cancellation, long-K chaining, every block-scale
   setting) must show bit-exact equality with a CPU reference before a route is
   enabled.

**Decisions for the user.** The original brief says FP4 will not be used, and
the appraisal preserves strict INT4 as an unresolved constraint. On the
Blackwell targets there is no documented INT4 Tensor Core route; this research
proposes integer-valued E2M1 operands on the FP4 instruction (2× the INT8 rate,
exact integer semantics within the bound above) and also records INT8 as an
alternative at half that rate. Whether either route is allowed under the
strict INT4-only rule is unresolved. Separately, whether wide exact accumulation
is compatible with that rule is unresolved too. The repository's automatic-mode
contract asks for NVFP4 to be tested on Blackwell. This research recommends the
integer-valued E2M1 lowering, gated by the exactness test, but neither that
recommendation nor the proposed accumulation semantics is an approved decision.

## Measuring utilization honestly

- Confirm the executed instruction in SASS (`IMMA…S4` vs INT8 emulation;
  `OMMA` vs `QMMA`) with `cuobjdump -sass` and Nsight Compute's source view.
- Nsight Compute pipeline metric names vary by architecture; query them on each
  device. DCGM tensor-activity metrics are documented for datacenter products
  only.
- Pipe-active fraction is not useful work: emulated INT4, unscaled FP4 on the FP8
  pipe or half-rate FP32-accumulate FP16 can all look busy at a fraction of peak.
  Report useful operations per second against the peak of the instruction
  actually executed, together with complete-receiver progress
  ([algorithm chapter](08-gemm-receiver-algorithms-and-utilization.md)).

## Verification items before committing to a design

1. Compile 4-bit `mma.sync` for sm_120a and inspect SASS (expect INT8
   emulation); likewise binary MMA.
2. Microbenchmark `kind::mxf4nvf4` m16n8k64 against `kind::f8f6f4` and INT8 on a
   5090 and a PRO 6000.
3. Exactness tests for integer-valued E2M1 dot products with FP32 accumulation.
4. INT4 `mma.sync` throughput on an actual Ada device (no published measurement
   found).
5. RDNA3 WMMA IU4 versus `v_dot8_i32_iu4` under HIP on the 8840U, and RADV
   cooperative-matrix SINT8 throughput.
6. Iris Xe DP4A rate (4.6 vs 9.2 TOPS convention).

## Sources

- NVIDIA CUDA C++ Programming Guide, Tensor Core input-type table (CUDA 13.1.0 → current; table parsed cell by cell in the hardware review).
- NVIDIA Ada GPU Architecture whitepaper (App. A, v2.02); NVIDIA RTX Blackwell GPU Architecture whitepaper (App. A); RTX PRO Blackwell whitepaper v1.1 (Table 4).
- NVIDIA PTX ISA 9.x, `mma.sync` block-scaled forms and element-type semantics; CUTLASS Blackwell/SM120 documentation (CUTLASS 3.9.0 release notes).
- NVIDIA Developer Forum thread "Does Blackwell support INT4 native?" (user-posted SASS; community, unverified).
- MMA-Sim bit-accurate tensor-core model, arXiv:2511.10909; Hayashi et al. base-13 E2M1 exact DGEMM emulation on RTX PRO 6000, arXiv:2608.06812 (as cited in the hardware review).
- Colfax Research NVFP4 GEMM on RTX PRO 6000 (83% of peak); community `nvfp4-train-sm120` measurements.
- LLVM AMDGPU processor features (`GCNFeatures.td`, AMDGPUUsage); AMD GPUOpen RDNA3/RDNA4 WMMA articles; AMD RDNA3.5 ISA.
- Intel oneAPI GPU optimization guide (Xe-LP); Intel Xe2 architecture materials; `SPV_INTEL_subgroup_matrix_multiply_accumulate`.
- Khronos `VK_KHR_cooperative_matrix`, `VK_EXT_shader_ocp_microscaling_types`; Mesa `features.txt`.
- uops.info measurements of `VPDPBUSD`/`VPMADDUBSW`.
- Thompson, Moran & Swenson, *Interferometry and Synthesis in Radio Astronomy*, Ch. 8 (quantization efficiency, via secondary sources); computed values in `calc/quantization.cpp`.

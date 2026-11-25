# Low-precision matrix hardware facts for a 4-bit receiver (literature check, 2026-11-24)

Scope: source-backed facts about INT4/INT8/FP4/FP8/FP16/binary matrix arithmetic on the
project's receiver targets and development GPUs, written to verify and extend
`_ref/algorithms-matrixModem/Sol-6.2/int4-and-accelerators.md`. No hardware was run and no
kernel was compiled in this session (no CUDA/ROCm toolchain on this host). Every number is
marked as **official** (vendor document), **measured** (paper/community microbenchmark),
**derived** (official per-unit rate × units × clock, computed here) or **unverified**.
Web-search quota ran out near the end; items marked *unverified* could not be re-checked.

---

## 0. Headline findings

1. **Consumer/workstation Blackwell (SM120, CC 12.0: RTX 5090, RTX PRO 6000) has no
   documented INT4 Tensor Core path.** The CUDA Programming Guide's Tensor Core input-type
   table (Table 36, identical in CUDA 13.1.0, 13.2.1, 13.4.1 and current docs) marks INT4
   only for CC 7.5, 8.0, 8.6, 8.7 and **8.9**; CC 9.0, 10.0, 10.3, 10.7, 11.0 and **12.x
   have no INT4**. Both RTX Blackwell whitepapers list Tensor Core types as "FP16, BF16,
   TF32, INT8, and Hopper's FP8 ... adds new support for FP4 and FP6"; their spec tables have
   **no INT4 row at all** (it is not "N/A"; the row was removed). **Sol-6.2 is wrong on this
   point** (see §11).
2. PTX still *accepts* `mma.sync ... .s4` for "sm_80 or higher", but on sm_90 and sm_100
   the compiler emits an emulation helper (`__cuda_sm_9x_mma_sub_byte_internal_*`) that
   unpacks nibbles and issues **INT8 IMMA**; on sm_89 it is a single native
   `IMMA.8832.S4.S4`. No sm_120 SASS was found in the literature; by the CUDA table it is
   almost certainly the same INT8-emulation route (≤ INT8 rate). **Verify with
   `cuobjdump -sass` on an sm_120 build before relying on it.**
3. On SM120 the 4-bit Tensor Core route is **FP4 E2M1 with block scaling**
   (`mma.sync.m16n8k64 ... kind::mxf4` / `kind::mxf4nvf4`, SASS `OMMA`), FP32 accumulate,
   at **2× the INT8 rate** and — uniquely among floating formats on GeForce — **not halved
   by FP32 accumulation** (5090: FP4 1676 dense vs INT8 838, FP8/FP32-acc 419). Unscaled FP4
   through `kind::f8f6f4` runs on the FP8 pipe (`QMMA`) at FP8 rate.
4. **E2M1 is an exact small-integer codebook** for {0, ±1, ±2, ±3, ±4, ±6} (plus ±0.5, ±1.5);
   products are exact and FP32 accumulation of integer-valued products is exact while
   partial sums stay below ~2^24 (hardware models: MMA-Sim; exploited by a base-13 E2M1
   Ozaki DGEMM on RTX PRO 6000). It **cannot** represent ±5, ±7, −8, so full-range INT4
   needs operand splitting (2–4 FP4 MMAs), which erases the 2× advantage.
5. **INT8 is the only integer precision with an accelerated path on every target that has
   one** (NVIDIA all generations, AMD RDNA3/3.5/4, Intel Xe-LP DP4A and Xe2 XMX, Zen 4 /
   Raptor Lake VNNI) — but **not** on the 5650U's Vega iGPU (gfx90c), which lacks
   `v_dot4_i32_i8`/`v_dot8_i32_i4` entirely, nor on UHD 630 (no DP4A). Vulkan cooperative
   matrix exposes SINT8/UINT8 but **no 4-bit integer type**.
6. Per SM per clock, GeForce dense Tensor rates have not changed since Turing for FP16
   (1024 FLOP) and INT8 (2048 OP); the 4096-op slot moved from INT4 (Turing–Ada) to FP4
   (Blackwell). PRO Blackwell removes the FP32-accumulate halving for FP16/FP8/TF32.

---

## 1. Master tables (dense peak; structured 2:4 sparsity doubles every Tensor row)

### 1a. NVIDIA (Tensor Core, TOPS / TFLOPS)

| Device (SMs @ boost) | INT4 (s32 acc) | INT8 (s32 acc) | FP4 E2M1 (FP32 acc) | FP8 FP16-acc / FP32-acc | FP16 FP16-acc / FP32-acc | BF16 (FP32 acc) | TF32 | Source |
|---|---|---|---|---|---|---|---|---|
| RTX 4090 (128 @ 2520 MHz, 450 W) | **1321.2** | 660.6 | — (not supported) | 660.6 / **330.3** | 330.3 / 165.2 | 165.2 | 82.6 | Official: Ada WP App. A (FP8/FP32 row "changed to proper number in v2.02"); same values in RTX Blackwell WP App. A |
| RTX 4090 Laptop (76 SMs @ 1455–2040 MHz, 80–150 W) | 453–635 | 226–318 | — | 226–318 / 113–159 | 113–159 / 57–79 | 57–79 | 28–40 | **Derived** (NVIDIA laptop specs × Ada per-SM rates). NVIDIA lists "686 AI TOPS", which implies ~2.20 GHz on an FP8-sparse basis and does **not** match its own 2040 MHz max boost — unexplained |
| RTX 4060 Ti 16 GB (34 @ 2535 MHz) | 353.0 | 176.5 | — | 176.5 / 88.3 | 88.3 / 44.1 | 44.1 | 22.1 | **Derived**; matches NVIDIA "353 AI TOPS" (= FP8 sparse = INT4 dense) |
| RTX 5090 (170 @ 2407 MHz, 575 W) | **no row** (emulation expected ≤ INT8) | 838 | **1676** | 838 / **419** | 419 / 209.5 | 209.5 | 104.8 | Official: RTX Blackwell WP App. A ("Peak FP4 Tensor TFLOPS with FP32 Accumulate (FP4 AI TOPS) 1676/3352") |
| RTX PRO 6000 Blackwell Workstation (188 @ 2617 MHz, 600 W) | **no row** | 1007.6 | **2015.2** | 1007.6 / 1007.6 | 503.8 / 503.8 | 503.8 | 251.9 | Official: RTX Blackwell PRO WP v1.1, Table 4 |
| RTX PRO 6000 Blackwell Max-Q (188 @ 2280 MHz, 300 W) | no row | 877.9 | 1755.7 | 877.9 / 877.9 | 438.9 / 438.9 | 438.9 | 219.5 | Official: same table; datasheet "3511 AI TOPS … Effective FP4 TOPS with sparsity" |
| RTX PRO 6000 Blackwell Server (24,064 cores; 600 W, cappable to 450 W; MIG up to 4×24 GB) | not listed | not listed | "4 PFLOPS" | "2 PFLOPS" | "1 PFLOPS" | "1 PFLOPS" | "234 TFLOPS" | Lenovo Press LP2263 reproduction; dense/sparse not stated (FP4/FP8/FP16 look like sparse figures); FP32 120 TFLOPS implies a lower clock than Workstation (derived ≈2.49 GHz) |

Per-SM-per-clock dense rates implied by the official tables (ops = 2 × MAC):

| Precision | Turing/Ampere/Ada GeForce | Blackwell GeForce (5090) | Blackwell PRO (6000) |
|---|---|---|---|
| FP16 → FP16 acc | 1024 | 1024 | 1024 |
| FP16/BF16 → FP32 acc | **512** (half) | **512** (half) | 1024 |
| TF32 | 256 | 256 | 512 |
| FP8 → FP16 / FP32 acc | 2048 / **1024** (Ada) | 2048 / **1024** | 2048 / 2048 |
| INT8 → s32 | 2048 | 2048 | 2048 |
| INT4 → s32 | 4096 | — | — |
| FP4 (block-scaled) → FP32 | — | 4096 | 4096 |

(Turing check: the Ada WP's RTX 2080 Ti column gives FP16 113.8, INT8 227.7, INT4 455.4 at
68 SMs × 1635 MHz = exactly 1024/2048/4096 per SM-clock.)

**Binary (b1):** Ada has native `BMMA` (Luo et al. Table VI: `BMMA.168256.AND.POPC`; forum
SASS: `BMMA.88128.XOR.POPC`/`.AND.POPC` on sm_89). No Ada b1 peak is published in the Ada
WP. Hopper: AND native, XOR emulated by two AND BMMAs. sm_100: b1 AND emulated via
`IMMA.16832.U8.U8`. The CUDA 13.5 "Blackwell and Rubin Instruction Set" SASS list contains
`HMMA, DMMA, OMMA, QMMA, IMMA` and `UTC*MMA` but **no `BMMA`** (Hopper's list has it). SM120
b1 SASS: not found — treat as emulated until checked.

### 1b. AMD, Intel and CPU targets

| Device | Matrix / dot hardware | INT4 | INT8 | FP16 | FP32 vector | Source / status |
|---|---|---|---|---|---|---|
| Ryzen 5 PRO 5650U — Radeon (Vega 7, **gfx90c**), 7 CU @ 1800 MHz | **None.** No WMMA/MFMA, **no `v_dot4_i32_i8`, `v_dot8_i32_i4`, `v_dot4_u32_u8`, `v_dot2_f32_f16`** | no instruction | no dot instruction; packed 16-bit `v_pk_mad_i16` ≤ 3.2 T-ops if full rate (*unverified rate*) | 3.23 TFLOPS packed (derived) | 1.61 TFLOPS (derived) | LLVM `GCNFeatures.td`: `FeatureISAVersion9_0_C` = consumer GFX9 + MadMix only; gfx906 adds DL/Dot1/Dot2/Dot7/Dot10. Clock/CU: WikiChip |
| Ryzen 7 8840U — Radeon 780M (**gfx1103**, RDNA3), 12 CU @ 2700 MHz | WMMA (256b, executes as 32-clock wave op) + `v_dot8_i32_iu4`, `v_dot4_i32_iu8` | 33.2 TOPS | 16.6 TOPS | 16.6 TFLOPS (WMMA) | 8.3 TFLOPS (dual-issue) | **Derived** from GPUOpen RDNA3 per-CU rates (FP16 512, BF16 512, IU8 512, IU4 1024 ops/clk/CU); LLVM gfx11 features incl. `WMMA256bInsts`, Dot7/Dot8 |
| Radeon 890M (gfx1150, RDNA3.5), 16 CU | same as RDNA3 | 47.5 | 23.8 | 23.8 | 11.9 | Derived at 2.9 GHz (*clock unverified*); per-CU RDNA3.5 rate assumed = RDNA3 (*unverified*) |
| Radeon 8060S (gfx1151, RDNA3.5), 40 CU | same | 118.8 | 59.4 | 59.4 | 29.7 | Derived at 2.9 GHz (*clock unverified*) |
| RDNA4 RX 9070 XT (gfx1201), 64 CU | WMMA 128b + SWMMAC (sparse) | **779** (1557 sparse) | 389 (779 sparse) | 195 (389 sparse) | — | Official AMD spec page (via search snippet; page fetch timed out). GPUOpen RDNA4: FP16 1024, I8 2048 /clk/CU → INT4 4096 /clk/CU (derived) |
| Core i9-13900H — Iris Xe (Xe-LP, 96 EU) @ 1.5 GHz | **No XMX/DPAS**; DP4A | — | 9.2 TOPS (derived, see note) | 4.6 TFLOPS | 2.30 TFLOPS | Intel opt. guide: Xe-LP 1536 FP32 FLOP/clk; per-EU INT8 "32 (DP4A)"; ARK: 96 EU, 1.50 GHz, "Iris Xe Graphics *eligible*" (dual-channel memory required for Iris branding) |
| Core i9-9900K — UHD 630 (Gen9.5 GT2, 24 EU *unverified*) @ 1.2 GHz | **None; no DP4A** | — | — | 0.92 TFLOPS | 0.46 TFLOPS | ARK 1.20 GHz; per-EU Gen9 rates from Intel Gen9 docs (*not re-read*) |
| Lunar Lake Arc 140V (Xe2, 8 Xe-cores, ≈2.05 GHz) | XMX 2048-bit ×8 per Xe-core | ~134 (derived) | **67** (official) | 33.5 (derived) | 4.2 (derived) | Intel Xe2 deck: per Xe2-core/clk FP32 256, FP16 512, DP4a INT8 1024, XMX FP16/BF16 2048, XMX INT8 4096, **XMX INT4/INT2 8192** |
| Panther Lake (Xe3, up to 12 Xe-cores, ≤2.5 GHz) | XMX | *unverified* | ~123 if Xe3 XMX = Xe2 per core (assumption) | — | — | Wikipedia (citing Intel): 12 Xe3 cores, 2.5 GHz, NPU 5 up to 50 TOPS. Per-core XMX rates not verified |

Note on Iris Xe: Intel's per-EU table lists FP32 "8" and INT8 "32 (DP4A)" ops/clk/EU, while
its configuration table gives 1536 FP32 FLOP/clk for 96 EUs (16/EU), so the per-EU table
counts multiply-adds; 32 INT8 MACs/clk/EU × 2 × 96 × 1.5 GHz = 9.2 TOPS. If "32" meant
ops, the figure would be 4.6 TOPS. Treat as 4.6–9.2 TOPS until measured.

### 1c. CPU integer dot throughput (per core per cycle; uops.info measurements)

| CPU (core) | Best int8 instruction (256/512-bit) | Measured throughput | Int8 MACs/clk/core | Notes |
|---|---|---|---|---|
| Ryzen 5 PRO 5650U (Zen 3, 6C/12T) | AVX2 `VPMADDUBSW ymm` (u8×s8 → pairwise s16, saturating) | 0.50 c/instr (FP03) | 64 multiplies into s16 pairs; ~32 MAC/clk with widening to s32 (needs `VPMADDWD`, same pipes) | No VNNI, no AVX-512 |
| Ryzen 7 8840U (Zen 4, 8C/16T) | AVX-512 VNNI `VPDPBUSD zmm` (u8×s8, 4-way, s32 acc) | **1.00 c/instr measured** (0.50 computed) | 64 | Zen 5 measured 0.50 (128/clk) |
| Core i9-13900H P-core (Raptor Cove ≈ Alder Lake-P) | AVX-VNNI `{vex} VPDPBUSD ymm` | 0.50 c/instr (p01) | 64 | ARK lists only AVX2 but "DL Boost: Yes" |
| Core i9-13900H E-core (Gracemont ≈ Alder Lake-E) | AVX-VNNI ymm | 2.00 c/instr | 16 | 6P + 8E, 20 threads |
| Core i9-9900K (Coffee Lake, 8C/16T) | AVX2 `VPMADDUBSW ymm` | 0.50 c/instr (p01) | as Zen 3 | No VNNI |

For INT4-ranged operands (|a|,|b| ≤ 8) a `VPMADDUBSW` pair-sum is ≤ 128, so up to ~255
pair-sums can be accumulated in s16 before widening; that lets AVX2 approach 64 MAC/clk/core
for 4-bit data (derived reasoning; signed×signed needs the usual +8 offset and a correction
term because `VPMADDUBSW`/`VPDPBUSD` take one unsigned operand).

---

## 2. Q1 — NVIDIA INT4 / b1 status per architecture, with evidence

- **CUDA Programming Guide, Table 36 "Input Data Types Supported by Tensor Core
  Acceleration per Compute Capability"** (parsed cell-by-cell including colspans):
  7.5: FP16, INT8, INT4 · 8.0: FP64, TF32, BF16, FP16, INT8, INT4 · 8.6/8.7: TF32, BF16,
  FP16, INT8, INT4 · **8.9: TF32, BF16, FP16, FP8, INT8, INT4** · 9.0: FP64, TF32, BF16,
  FP16, FP8, INT8 · 10.0: FP64…FP4, INT8 · 10.3: TF32…FP4, INT8 · 10.7: FP64…FP4 (no INT8,
  no INT4) · 11.0: TF32…FP4, INT8 · **12.x: TF32, BF16, FP16, FP8, FP6, FP4, INT8 (no INT4,
  no FP64)**. Hopper and all Blackwell variants therefore **dropped INT4** as a Tensor Core
  input; CC 10.7 also lacks INT8.
- **PTX ISA 9.5, `mma` Target ISA Notes:** ".u4/.s4 integer type mma operation with .m8n8k32
  shape sm_75 or higher"; ".m16n8k32 and .m16n8k64 shapes requires sm_80 or higher"; ".b1
  … .m16n8k128 and .m16n8k256 … requires sm_80 or higher". This is *language availability*
  (forward-compatible PTX), not hardware support. Integer `mma` accumulates in `.s32`, with
  `.satfinite` clamping or wrap-around otherwise.
- **SASS evidence (NVIDIA forum thread "Does Blackwell support INT4 native?", 2025-03-10,
  user striker159, not NVIDIA staff):** `IMMA.8832.S4.S4.SAT` on sm_75/80/86/89; on sm_90
  the helper `__cuda_sm_9x_mma_sub_byte_internal_m8n8k32_s4_s4_satfinite` uses
  `LOP3/SHF/IMAD` unpacking + `IMMA.8816.S8.S8`; sm_100 the same helper with
  `IMMA.16816.S8.S8`. No sm_120 dump was posted.
- **Luo et al., "Benchmarking and Dissecting the Nvidia Hopper GPU Architecture"
  (arXiv 2402.13499):** "on Ampere and Ada … INT4 mma … compiled into IMMA.16832.S4.S4"; "on
  Hopper … INT4 mma instructions are compiled into a series of IMAD instructions … run on the
  CUDA cores"; `wgmma` has no INT4. (Older toolkit than the forum post; both agree INT4 is not
  native on Hopper.) Their Table VII measured RTX 4090 INT8 m16n8k32 at 711.7 TOPS dense /
  1423 sparse (above the 660.6 nominal because the card ran above rated boost) and FP16→FP32
  at 178.9 TFLOPS (half of the FP16→FP16 357.6) — direct confirmation of the GeForce
  FP32-accumulate halving. **No INT4 throughput row exists in their tables**; I found no
  published measured INT4 `mma.sync` throughput for SM89 in this session.
- **SM120 measured data:** FlashGPU-sim (arXiv 2609.15311) on RTX 5090, normalized to
  2580 MHz: INT8 m16n8k32 **886.2 TOPS (98.7 % of 898.2)**, FP16→FP32 m16n8k16 221.1 TFLOPS
  (98.5 % of the *half-rate* 224.6 peak). The k-halved shapes (m16n8k16 INT8, m16n8k8 FP16)
  reach only ~49 % — use the largest K shape per instruction. Jarmusch et al. "Dissecting the
  NVIDIA Blackwell Architecture with Microbenchmarks" (arXiv 2507.10789, RTX 5080/GB203 vs
  H100 PCIe): supported types "FP4, FP6, FP8, INT8, FP16, BF16, TF32, FP64" (no INT4);
  `tcgen05` "yet to be supported for sm_120a"; unscaled FP4 via `kind::f8f6f4` compiled to
  `QMMA`, while block-scaled FP4 produced `OMMA`. No INT4 measurement anywhere.
- **Undocumented claim:** a forum post (2026-07-16, im0qianqian, unverified) says the FP4
  `OMMA.SF` encoding has an alternate "E0M3" element format selectable by SASS bits that
  delivers INT4 at FP4 speed after **cubin post-processing**. This is undocumented,
  unsupported by PTX/CUTLASS, and must not be a design basis; at most it is a curiosity to
  test on a sacrificial build.
- **Datacenter Blackwell (`tcgen05`):** PTX: `tcgen05.mma` targets sm_100a/sm_110a and the
  sm_100f/sm_110f families "except .kind::i8", which is only on sm_100a/sm_110a — i.e. even
  INT8 is not family-portable within datacenter Blackwell. No `.kind` for 4-bit integers
  exists. `tcgen05` is not available on sm_120.

**Answer:** INT4 Tensor Core MMA is native on Ada (SM89), not listed and (on sm_90/sm_100)
demonstrably emulated through INT8 IMMA on Hopper/datacenter Blackwell, and not listed for
SM120; on SM120 expect emulation at ≤ INT8 throughput (unverified by SASS). b1 is native on
SM89, emulated on sm_100, and absent from the Blackwell SASS list.

## 3. Q2 — GeForce FP32-accumulate halving

From the official tables (§1a): on GeForce Ada and GeForce Blackwell, **FP16→FP32, BF16,
TF32 and FP8→FP32 run at half** the rate of their FP16-accumulate/PRO counterparts;
**INT8→s32 and (Ada) INT4→s32 run at full rate**; on the RTX 5090 **FP4→FP32 is full rate**
(1676 = 2 × FP8/FP16-acc 838 = 4 × FP8/FP32-acc 419). The PRO 6000 (and RTX 6000 Ada:
FP16 364, FP8 728.5 with either accumulator) shows no halving. Measured confirmation: Luo et
al. (4090 FP16→FP32 at half) and FlashGPU-sim (5090 FP16→FP32 at half; INT8 at ~99 % of
full). Consequence for an exact-integer receiver on a 5090: INT8 IMMA (838) beats FP8 with a
safe FP32 accumulator (419) by 2×, and only E2M1 FP4 (1676) is faster.

FP16-accumulate is not a safe exact-integer accumulator: FP16 represents integers exactly
only up to 2048, and PTX allows "at least half precision" accumulation with unspecified
order and rounding.

## 4. Q3 — FP4 formats, accumulator, SM120 instructions, CUTLASS

**E2M1 codebook** (1 sign, 2 exponent, 1 mantissa; no Inf/NaN per PTX §5.2.3): magnitudes
{0, 0.5, 1, 1.5, 2, 3, 4, 6} → 15 distinct values (±0 coincide). Integer subset:
{0, ±1, ±2, ±3, ±4, ±6}. Doubled values are integers {0,1,2,3,4,6,8,12}, the basis of the
"base-13" limb representation of Hayashi et al. (arXiv 2608.06812, run on RTX PRO 6000).
For comparison: FP6 E2M3 represents every integer in [−7, 7] exactly (max 7.5); FP8 E4M3
represents every integer with |v| ≤ 16 exactly — but both run at FP8 rate on SM120.

**Block scaling (PTX §9.7.16.3, CUTLASS Blackwell guide):**

| Format | Elements | Scale type | Block (dense) | PTX `mma.sync` form (SM120) |
|---|---|---|---|---|
| MXFP4 (OCP) | E2M1 | UE8M0 (power of two) | 32 | `m16n8k64 … kind::mxf4.block_scale.scale_vec::2X … .ue8m0` |
| NVFP4 (not OCP) | E2M1 | UE4M3 (unsigned E4M3) | 16 | `m16n8k64 … kind::mxf4nvf4.block_scale.scale_vec::4X … .ue4m3` |
| MXFP4 with 16-blocks | E2M1 | UE8M0 | 16 | `kind::mxf4nvf4 … scale_vec::4X … .ue8m0` (PTX 9.1+) |
| MXFP8/MXFP6/MXFP4 mixed | E4M3/E5M2/E3M2/E2M3/E2M1 | UE8M0 | 32 | `m16n8k32 … kind::mxf8f6f4.block_scale.scale_vec::1X` (FP8-rate) |
| Unscaled FP8/FP6/FP4 | same | — | — | `m16n8k32 … kind::f8f6f4` (FP4/FP6 padded in 8-bit containers; FP8-rate) |

The block-scaled operation is D = (A·scale_A)(B·scale_B) + C with scales applied per K-chunk.
NVFP4 additionally uses a per-tensor FP32 scale in software (applied outside the MMA;
*not re-verified this session*). Targets: `.e2m1/.e3m2/.e2m3` and `.kind/.block_scale`
"require sm_120a and are supported on sm_120f … from PTX ISA 8.8" — not available on sm_89.
Packed FP4 conversion/arith helpers (e.g. `add.rn.e4m3x4.e2m1x4`) are sm_100a/sm_103a-only.

**Rates:** CUTLASS's SM100 table: `kind::f8f6f4` = "2x Hopper Fp8" (i.e. FP8 rate),
`kind::mxf8f6f4.block_scale` = FP8 rate, `kind::mxf4` and `kind::mxf4nvf4` = "4x Hopper
Fp8" (2× FP8). Jarmusch et al. observed the same split on GB203 (unscaled FP4 → `QMMA`,
block-scaled → `OMMA`). **Use `mxf4`/`mxf4nvf4` (m16n8k64, packed nibbles, no padding) to get
the FP4 rate; scales may be set to exactly 1.0 (UE8M0 exponent 127; UE4M3 1.0).**

**Accumulator:** PTX: for e2m1/e3m2/e2m3/e4m3/e5m2, "element-wise multiplication … performed
with specified precision. Accumulation … at least single precision. The accumulation order,
rounding, and handling of subnormal inputs are unspecified." Block-scaled forms only exist
with `.f32` D/C. Bit-accurate modelling (MMA-Sim, arXiv 2511.10909, measured on RTX 4090,
H100, B200, RTX PRO 6000 among others): products are exact; aligned significands keep
F = 25 fractional bits on RTX Blackwell `HMMA`/`QMMA` (incl. FP8/FP6/FP4 inputs) with
truncation; block-scaled `OMMA.SF` sums groups of 16 products exactly ("GDFS", 35 fractional
bits, round-toward-zero); FP32 output is rounded toward zero. **Ada `QMMA` (FP8) keeps only
F = 13**, and Hopper `QGMMA` also 13. Implications: integer-valued E2M1 dot products on SM120
are exact while every partial sum, including the chained C, stays below about 2^24; on Ada,
FP8 MMA with FP32 output stops being integer-exact once the running sum exceeds ~2^13–2^14
— another reason not to use FP8 as an integer carrier on Ada. These are model conclusions
from one study; confirm with adversarial cancellation tests on each device/driver.

**CUTLASS SM120:** CUTLASS 3.9.0 (2025-04-24) added "Blackwell SM120 kernels for GeForce
GPUs": block-scaled dense and sparse GEMM, NVFP4→BF16/NVFP4 with scale-factor generation,
MXFP8×MXFP6, grouped NVFP4 GEMM; later releases added SM120 blockwise GEMM (example 87),
FP8 grouped/ptr-array kernels, SM121 sharing, fixes for SM120 block-scaled MMAs and
128×32/128×64 tiles. No SM120 integer-4-bit kernels are listed. SM120 has 100 KB shared
memory per SM (99 KB per block, same as SM89) versus 228 KB on SM100 (CUDA guide), so SM100
tile configurations do not transfer.

**Achieved FP4 throughput (measured, not vendor):** Colfax Research: optimized NVFP4 GEMM on
RTX PRO 6000 reached 1666 TFLOPS at 16k (83 % of 2015.2), with clocks dropping to 2.15 GHz
at large shapes. Community `nvfp4-train-sm120`: up to 1027 TFLOPS on a desktop RTX 5090
(≈61 % of 1676) and 273–441 TFLOPS on an RTX 5090 Laptop.

## 5. Q4 — AMD

- **gfx90c (5650U Vega iGPU):** LLVM defines gfx90c with `FeatureISAVersion9_0_C` =
  consumer GFX9 common features + `FeatureMadMixInsts`; it has VOP3P packed 16-bit math
  (`FeatureGFX9` includes `FeatureVOP3PInsts`) but **none** of DLInsts, Dot1
  (`v_dot4_i32_i8`, `v_dot8_i32_i4`), Dot2 (`v_dot2_i32_i16`), Dot7 (`v_dot4_u32_u8`,
  `v_dot8_u32_u4`) or Dot10 (`v_dot2_f32_f16`) — those belong to gfx906 (MI50/60, Radeon
  VII). LLVM's AMDGPUUsage lists gfx90c as an APU with runtime `pal-amdpal` only (no ROCm
  HSA). So the "Vega 7nm ISA" document cited by Sol-6.2 describes gfx906 instructions that
  the 5650U **does not have**. Derived peak at 7 CU × 1.8 GHz: FP32 1.61, packed FP16 3.23
  TFLOPS. Integer 4/8-bit products must be built from 16/24/32-bit ALU ops (packed int16,
  `v_mad_u32_u24`, SWAR packing).
- **RDNA3 (780M gfx1103), RDNA3.5 (890M gfx1150, 8060S gfx1151):** LLVM gfx11/gfx11.5 have
  `WMMA256bInsts` (A/B fragments duplicated across half-waves), Dot7, Dot8
  (`v_dot4_i32_iu8`, `v_dot8_i32_iu4`), Dot9/10/12. WMMA instructions:
  `v_wmma_{f32,f16}_16x16x16_f16`, `…_bf16`, `v_wmma_i32_16x16x16_iu8`,
  **`v_wmma_i32_16x16x16_iu4`** (signedness per operand via `neg_lo`, s32 accumulator, clamp
  option). GPUOpen per-CU/clk: FP16 512, BF16 512, **IU8 512, IU4 1024** — identical to
  RDNA2's IU8/IU4 rates via dot instructions, so RDNA3 WMMA doubled only FP16, not integer
  throughput. GPUOpen: "A single WMMA instruction coordinates 32 clocks of work". The 780M is
  absent from AMD's WMMA article; its WMMA support follows from LLVM's gfx1103 feature set.
  ROCm's GPU table lists gfx1103 (e.g. Ryzen 7 7840U), gfx1150 and gfx1151 APUs; the 8840U
  itself is not listed there.
- **RDNA4 (gfx1200/1201):** `WMMA128bInsts` (no duplication) + `SWMMACGfx1200Insts` (2:4
  sparse). INT4: `v_wmma_i32_16x16x16_iu4`, `v_wmma_i32_16x16x32_iu4`, sparse
  `v_swmmac_i32_16x16x32_iu4`, `v_swmmac_i32_16x16x64_iu4`; plus FP8/BF8. RX 9070 XT
  official: INT4 779 dense / 1557 sparse, INT8 389 / 779, FP16 matrix 195 / 389.
- **8840U specifics:** 8 Zen 4 cores, Radeon 780M 12 CU @ up to 2700 MHz, XDNA NPU 16 TOPS,
  ~38–39 total platform TOPS (AMD pages differ). Derived 780M peaks: IU4 33.2, IU8 16.6,
  FP16 16.6 TOPS/TFLOPS — shared-memory-bandwidth-bound in practice (LPDDR5/5X).

## 6. Q5 — Intel

- **Iris Xe (Xe-LP, i9-13900H):** 96 EUs, 1.5 GHz; Intel's optimization guide: Xe-LP
  FP32 8, FP16 16, INT32 8, INT16 16, **INT8 32 (DP4A)** per EU per clock (Gen11: INT8 NA);
  no DPAS/XMX ("FP16 DPAS: NA" for Xe-LP; XMX only on Xe-HPG/HPC). Peak INT8 4.6–9.2 TOPS
  depending on the unit convention (§1b). Mesa's ANV cooperative-matrix design uses DP4A for
  8-bit matrices on Gfx12.
- **UHD 630 (Gen9.5, i9-9900K):** 350 MHz–1.20 GHz (ARK). No DP4A (introduced in Xe-LP), no
  matrix engine. FP16 at 2× FP32 per EU (Gen9 architecture; not re-read). Treat as a vector
  FP16/FP32 device; INT8 offers no instruction-level speedup.
- **Xe2 (Lunar Lake Arc 140V; Battlemage uses the same Xe2 core):** Intel Xe2 deck "Key Peak
  Metrics for 2nd Gen Xe-core": XMX INT8 4096 ops/clk, **XMX INT4/INT2 8192 ops/clk**, XMX
  FP16/BF16 2048, DP4a INT8 1024, FP32 256; "Support for INT2, INT4, INT8, FP16, BF16";
  Lunar Lake: 8 Xe-cores, "67 peak INT8 TOPs". INT2 is **not faster** than INT4.
- **Xe3 (Panther Lake):** up to 12 Xe3 cores, ≤2.5 GHz, NPU 5 up to 50 TOPS (Wikipedia citing
  Intel). Per-core XMX rates and Intel's GPU TOPS claim were *not verified* (search budget
  exhausted); an Xe2-equal assumption gives ≈123 INT8 TOPS.

## 7. Q6 — Portable matrix APIs

- **`VK_KHR_cooperative_matrix`** (`VkComponentTypeKHR`): FLOAT16/32/64, SINT8/16/32/64,
  UINT8/16/32/64; BFLOAT16 (with `VK_KHR_shader_bfloat16`); FLOAT8_E4M3/E5M2 (with
  `VK_EXT_shader_float8`); and, from `VK_EXT_shader_ocp_microscaling_types` (rev. 1,
  2026-04-29; Arm, Imagination, NVIDIA, Samsung), FLOAT4_E2M1, FLOAT6_E2M3/E3M2,
  FLOAT8_UNSIGNED_E8M0 and MXINT8. That extension adds **no block-scaled MMA** ("no support
  for microscaled tensors or operations on microscaled data") and its own Issues text says the
  new types cannot be used directly in matrix multiplication. **There is no 4-bit integer
  component type.** SINT8/UINT8_PACKED_NV belong to `VK_NV_cooperative_vector`.
  `VK_NV_cooperative_matrix2` (flexible sizes, reductions, tensor addressing) adds no new
  element types (*not re-read in detail*).
- **Mesa (features.txt, current main):** `VK_KHR_cooperative_matrix` DONE for anv, lvp,
  nvk/Turing+, panvk/v11+, **radv/gfx11+**, vn; `VK_EXT_cooperative_matrix_maintenance1`
  radv/gfx11+; `VK_EXT_shader_float8` radv/gfx12+; `VK_NV_cooperative_matrix2` lvp only;
  `VK_KHR_shader_integer_dot_product` DONE for radv, anv and others. So RADV gives the 780M
  /890M/8060S SINT8 WMMA through Vulkan, but **nothing on the 5650U's gfx90c**. ANV: Mesa MR
  25994 "Enable VK_KHR_cooperative_matrix on all Gfx9+ GPUs" — DPAS on Gfx12.5+, **DP4A for
  8-bit on Gfx12 (Tiger Lake/Xe-LP)**, subgroup-op emulation on older parts such as UHD 630
  (functional, not accelerated). DPAS was later disabled on Meteor Lake (no XMX).
  `VK_KHR_shader_integer_dot_product` exposes only packed 4×8-bit dot, not 8×4-bit.
- **`SPV_INTEL_subgroup_matrix_multiply_accumulate`:** operand flags include
  `MatrixAPackedInt4INTEL`/`MatrixBPackedInt4INTEL` (and Int8, TF32, FP16, BF16 packed;
  no FP8 flag); integer accumulation "performed at the precision of the Result Type"; M,N,K
  support is device-queried and unsupported shapes are undefined. The OpenCL extension
  `cl_intel_subgroup_matrix_multiply_accumulate` is the OpenCL counterpart (*not re-read*).
  This is the documented portable-ish route to Xe2 XMX INT4.
- **HIP (AMD):** `__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32/_w64` (gfx11),
  `…_iu8_…`, gfx12 `_gfx12` variants and `wmma_i32_16x16x32_iu4`, `swmmac_*_iu4`; dot
  builtins `__builtin_amdgcn_sudot8` (`v_dot8_i32_iu4`). ROCm availability on the iGPUs is
  separate from instruction availability.
- **NVIDIA:** INT4 only via PTX `mma.sync` (or WMMA "experimental sub-byte" fragments) on
  SM75–SM89; FP4 via PTX `mma.sync` block-scaled forms or CUTLASS SM120 collectives; Vulkan
  cooperative matrix on NVIDIA exposes no 4-bit integer type.

## 8. Q7 — CPU methods for INT4

See §1c. Additional points:
- llama.cpp-style Q4 kernels unpack nibbles (`VPAND`/`VPSRLW`/`VPSHUFB`) and feed
  `VPMADDUBSW` (AVX2) or `VPDPBUSD` (VNNI); the int8 instruction rate is the ceiling and the
  unpack adds ALU work. On Zen 4 512-bit VNNI is measured at 1 zmm/clk (same MAC rate as two
  ymm ops). (Kernel structure from general knowledge; not re-read this session.)
- **T-MAC** (Wei et al., EuroSys 2025, arXiv 2407.00088): table-lookup mixed-precision GEMV
  using "tbl/pshuf instructions"; claims up to 4× llama.cpp throughput and 70 % less
  energy; BitNet-b1.58-3B at 30 tok/s (1 core) / 71 tok/s (8 cores) on M2 Ultra; the README
  warns it "cannot guarantee significant speedup (especially for 4-bit token generation) on
  all x86 platforms". LUT cost scales with weight bits, so it wins mainly at ≤2 bits and for
  weight-stationary GEMV; for 4-bit × 4-bit products the int8 dot path is the baseline.
- No achieved TOPS figures for these specific CPUs were found in this session.

## 9. Q8 — NPUs (brief)

| Platform | NPU | Peak (vendor) | Custom GEMM programmability |
|---|---|---|---|
| 5650U (Cezanne) | none | — | — |
| 8840U (Hawk Point) | XDNA (AIE-ML) | 16 TOPS INT8 (AMD/TPU pages) | AMD IRON / MLIR-AIE open toolchain, Peano compiler, kernels in C++ (Wikipedia citing AMD); Ryzen AI SW; AIE-ML natively INT8/BF16 |
| Strix Point / Strix Halo | XDNA 2 | 50 TOPS (*not re-verified*) | same toolchain family |
| 13900H | none (GNA 3.0 per ARK) | — | — |
| Lunar Lake | NPU 4 | 48 TOPS (*not re-verified*) | OpenVINO / Level Zero graph compilation; no public kernel ISA (*not re-verified*) |
| Panther Lake | NPU 5 | up to 50 TOPS (Wikipedia citing Intel) | as above |

INT4 support on these NPUs was not verified. They are separate address spaces with
graph-compiler toolchains and are not drop-in GEMM engines for an in-process receiver.

## 10. Q9 — Measuring Tensor Core utilization

- **Nsight Compute:** "Pipeline metric names and available pipeline terms vary across GPU
  architectures … Query the metrics available on the target GPU before hard-coding." The
  `tensor` pipeline "executes various MMA instructions" (mma.sync family: HMMA/IMMA/QMMA/
  OMMA); the separate `tc` pipeline executes `UTC*MMA` (tcgen05) — irrelevant on SM120. Use
  `ncu --query-metrics` and the source/SASS view to confirm the executed opcode
  (`IMMA…S4` vs `IMMA…S8` emulation; `OMMA` vs `QMMA`). By default ncu locks clocks to
  their boost value (`--clock-control`); thermal throttling still overrides it.
- **DCGM:** `DCGM_FI_PROF_PIPE_TENSOR_ACTIVE` (1004) = "fraction of cycles the tensor
  (HMMA / IMMA) pipe was active … 100 % is equivalent to issuing a tensor instruction every
  other cycle"; ambiguous between few SMs fully busy and all SMs partly busy (pair with
  `SM_ACTIVE` 1002). **"Profiling metrics are currently supported on datacenter products
  starting with the Volta architecture"** — so DCGM tensor activity is not a supported
  measurement on GeForce 4090/5090 (PRO/Server applicability should be checked on the card).
- **Caveats:** pipe-active fraction is not work: a half-rate FP32-accumulate op, an
  emulated INT4 MMA (INT8 IMMA plus unpack ALU work), or unscaled FP4 on `QMMA` can all show
  high tensor activity at a fraction of the advertised peak. Report achieved useful ops/s =
  (executed MMA instructions × ops per instruction) / time, against the dense peak of the
  *instruction actually executed*, alongside SASS opcode identity, SM activity and DRAM
  throughput.

---

## 11. Corrections to Sol-6.2 (`int4-and-accelerators.md`)

1. **Wrong:** "NVIDIA lists the Ada and Blackwell targets as supporting … four-bit integer
   Tensor Core input operands" and "INT4 is listed for 8.9, 10.0, 10.3, and 12.x." The CUDA
   Programming Guide table (13.1.0 → current) lists INT4 **only up to 8.9**; 9.0, 10.0, 10.3,
   10.7, 11.0 and 12.x have no INT4. The RTX Blackwell and RTX PRO Blackwell whitepapers list
   Tensor types without INT4 and drop the INT4 spec row.
2. **Wrong conclusion:** "a promising native matrix multiply route on … RTX 5090, and RTX PRO
   6000" and "the portable documented proposition is 'Blackwell supports Tensor Core INT4
   input arithmetic'". PTX target notes ("sm_80 or higher") describe language acceptance; on
   sm_90/sm_100 s4 `mma` compiles to INT8-IMMA emulation, and SM120 is expected to behave
   the same (verify). Replace with: "INT4 is native only on the Ada targets; on SM120 the
   4-bit Tensor route is block-scaled FP4."
3. **Incomplete:** "Published 3352 AI TOPS does not identify the INT4 rate." The RTX
   Blackwell WP identifies it: FP4 Tensor with FP32 accumulate, 1676 dense / 3352 sparse; the
   PRO 6000 is 2015.2 / 4030.4 (datasheet "4000"), Max-Q 1755.7 / 3511.4.
4. **Overstated:** "Mapping integer values into FP4 … is not integer arithmetic." For the
   alphabet {0, ±1, ±2, ±3, ±4, ±6} (and halves), E2M1 MMA with FP32 accumulation is exact
   integer arithmetic within ~2^24, per PTX semantics (exact element products for e2m1) and
   MMA-Sim's bit-level model; Hayashi et al. build exact FP64 emulation on it. It is a
   different (non-uniform, 15-level) codebook, not INT4, and needs per-device verification.
5. **Binary-plane alternative** (INT4 via b1 popcount MMA) does not help on Blackwell: b1 is
   emulated on sm_100 and `BMMA` is absent from the Blackwell SASS list. It remains valid only
   on SM75–SM89 (where native INT4 exists anyway).
6. **Missing target:** the Ryzen 7 8840U (Radeon 780M, gfx1103, RDNA3 WMMA incl. IU4, Zen 4
   AVX-512 VNNI, 16-TOPS NPU) is a representative receiver target in AGENTS.md but is absent
   from Sol-6.2's tables. The i9-9900K/UHD 630 GUI target is also absent.
7. **Misattributed ISA:** the cited "Vega 7nm Shader ISA" documents gfx906 dot instructions;
   the 5650U's gfx90c lacks them (LLVM). Add that Vega gfx90c has **no** INT8/INT4 dot
   instruction, not just no matrix unit.
8. **Iris Xe:** add DP4A INT8 (32 per EU per clock) as the only low-precision integer
   accelerator; Mesa ANV uses it for cooperative-matrix INT8 on Gfx12.
9. **RDNA3/3.5:** add that WMMA IU8/IU4 per-CU rates (512/1024) equal RDNA2 dot-instruction
   rates; INT4 via WMMA is 2× INT8, not a new integer ceiling. RDNA4 doubles/quadruples it.
10. **Utilization:** DCGM profiling fields are documented for datacenter products only;
    for GeForce development use Nsight Compute/Systems.
11. Correct in Sol-6.2 and confirmed: RTX 4090 INT4 1321.2 dense; integer `mma` s32
    accumulation with wrap/`satfinite`; `tcgen05` integer kind is i8 only; Xe2 XMX
    INT4/INT2; RDNA3.5 `V_WMMA_I32_16X16X16_IU4`; SPIR-V Intel packed-int4 flags.

---

## 12. Implications for a 4-bit receiver

**Where 4-bit Tensor/matrix arithmetic exists (and at what multiple of INT8):**

| Target | 4-bit matrix route | Rate vs INT8 | Accumulator | Practical API |
|---|---|---|---|---|
| RTX 4090 / 4090 Laptop / 4060 Ti (SM89) | INT4 `mma.sync.m16n8k64.s4` (native `IMMA…S4`) | 2× | s32 exact | PTX inline / CUTLASS SM80-class kernels |
| RTX 5090 / RTX PRO 6000 (SM120) | **FP4 E2M1** block-scaled `mma.sync.m16n8k64 kind::mxf4[nvf4]` (`OMMA`) | 2× | FP32 (exact for integer sums < ~2^24, verify) | PTX inline (sm_120a/f) / CUTLASS ≥3.9 SM120 |
| RTX 5090 / PRO 6000, full-range INT4 | INT8 IMMA with s4 values sign-extended | 1× | s32 exact | PTX / CUTLASS / cuBLASLt |
| Radeon 780M / 890M / 8060S | WMMA IU4 or `v_dot8_i32_iu4` | 2× | s32 | HIP builtins only (Vulkan has no int4 type) |
| RDNA4 dGPU | WMMA/SWMMAC IU4 | 2× (+2× sparse) | s32 | HIP |
| Arc 140V / Xe2, Xe3 | XMX INT4 (INT2 same rate) | 2× | s32 | SPIR-V Intel ext / OpenCL Intel ext / oneAPI |
| Vega 7 (5650U), UHD 630 | none | — | — | vector shaders only; CPU likely competitive |
| Iris Xe (13900H) | none (DP4A INT8 only) | — | s32 | Vulkan cooperative matrix (ANV, DP4A-lowered) / integer dot product |
| Zen 3/4, Raptor Lake, Coffee Lake CPUs | none native; int8 dot with nibble unpack | ≤1× | s32 (VNNI) / s16→s32 (AVX2) | C++ intrinsics |

**Can FP4 E2M1 act as an exact small-integer codebook?** Yes, with conditions:
- Choose receiver quantizer levels from the E2M1 set, e.g. uniform {−4,…,4} (9 levels,
  ≈3.17 bits) or the full non-uniform sign+3-bit-magnitude set {0, ±0.5, ±1, ±1.5, ±2, ±3,
  ±4, ±6} (a companded 15-level quantizer, arguably a better fit for Gaussian-like soft
  samples than uniform INT4). Per-16 (NVFP4, UE4M3) or per-32 (MXFP4, UE8M0 power-of-two)
  block scales give free per-block gain normalization; UE8M0 keeps scaled products exact.
- Bounds: with |a|,|b| ≤ 6, |product| ≤ 36; exactness requires the accumulated magnitude
  (including C across K-tiles) to stay < 2^24 (~466k worst-case products per output).
- Full INT4 (−8…7) is not representable; v = a + b with a, b ∈ [−4, 4] is exact, so INT4 × E2M1
  costs 2 FP4 MMAs (= INT8 rate) and INT4 × INT4 costs 4 (= ½ INT8 rate). On SM120 the 2×
  advantage therefore exists **only if the receiver's samples and templates are natively on
  the E2M1 grid**; otherwise INT8 IMMA is the better exact route.
- Exactness rests on PTX's "multiplication … with specified precision", FP32 accumulation
  "at least single precision", and one bit-accurate model; accumulation order and rounding
  are formally unspecified. A cheap deterministic adversarial test per device and driver
  (cancellation, max-magnitude, long-K chaining through C, mixed block scales) must gate any
  claim of exactness.

**Is INT8 the portable integer route?** Largely yes. INT8 (s32 accumulate) is supported
and full-rate on every NVIDIA target (660.6 / 838 / 1007.6 TOPS on 4090 / 5090 / PRO 6000),
on RDNA3+ via WMMA/dot4 and Vulkan SINT8 cooperative matrix, on Xe-LP (DP4A), Xe2/Xe3 (XMX),
and on Zen 4 / Raptor Lake (VNNI) and AVX2 CPUs. Exceptions are the Vega gfx90c iGPU (no
int8 dot instruction at all) and UHD 630 (no DP4A), where FP16 vector math or the CPU is the
practical path. INT4 values stored packed and widened to INT8 in-register halve memory
traffic while keeping the portable INT8 compute path; the native-4-bit speedups (Ada INT4,
RDNA IU4, Xe2 XMX INT4, Blackwell FP4) are vendor-specific fast paths with different
operand types and APIs.

**Throughput ordering on the development and receiver GPUs (dense, exact-integer-capable):**
PRO 6000 FP4 2015 > 5090 FP4 1676 > 4090 INT4 1321 > PRO 6000 INT8 1008 > 5090 INT8 838 >
4090 INT8 661 > 4090 Laptop INT4 ≤635 > 4060 Ti INT4 353 — all peak, before memory, packing
and launch effects; sustained values depend on power limits (laptop 80–150 W; PRO Max-Q
300 W; Colfax saw PRO 6000 drop to 2.15 GHz).

**Open verification items (cheap, deterministic, before design commitment):**
1. Compile `mma.sync.m16n8k64.s32.s4.s4.s32` for sm_120a and inspect SASS (expect INT8
   emulation); also b1.
2. Microbenchmark `kind::mxf4nvf4` m16n8k64 vs `kind::f8f6f4` m16n8k32 vs INT8 m16n8k32 on a
   5090/PRO 6000 (expect ~2:1:1 on PRO, 4:1:2 for FP4:FP8(FP32):INT8 on 5090).
3. Exactness tests for E2M1 integer dot products with FP32 accumulate on SM120.
4. INT4 `mma` throughput on an actual SM89 device (no published figure found).
5. gfx1103 WMMA IU4 vs `v_dot8_i32_iu4` achieved rate under HIP on the 8840U, and RADV
   cooperative-matrix SINT8 rate.
6. Iris Xe DP4A unit convention (4.6 vs 9.2 TOPS) by microbenchmark.

---

## Sources

NVIDIA official
- Ada GPU Architecture whitepaper (App. A, v2.02 FP8/FP32 correction): https://images.nvidia.com/aem-dam/Solutions/geforce/ada/nvidia-ada-gpu-architecture.pdf
- RTX Blackwell GPU Architecture whitepaper (App. A, RTX 5090): https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf
- RTX Blackwell PRO GPU Architecture v1.1 (Table 4): https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/pdf/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1_1.pdf
- RTX PRO 6000 Max-Q datasheet: https://www.nvidia.com/content/dam/en-zz/Solutions/products/workstations/professional-desktop-gpus/rtx-pro-6000-max-q/workstation-datasheet-blackwell-rtx-pro-6000-max-q-nvidia-us-5349650-web.pdf
- RTX PRO 6000 Server Edition (Lenovo Press reproduction of NVIDIA specs): https://lenovopress.lenovo.com/lp2263
- GeForce laptop comparison (4090 Laptop): https://www.nvidia.com/en-us/geforce/laptops/compare/ ; RTX 4060 Ti: https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/rtx-4060-4060ti/
- CUDA Programming Guide, Compute Capabilities, Table 36 (and archives 13.1.0/13.2.1/13.4.1): https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html
- PTX ISA 9.5 (§5.2.3, §9.7.16.3, §9.7.16.5.14 `mma`, §9.7.18.10.10 `tcgen05.mma`): https://docs.nvidia.com/cuda/parallel-thread-execution/index.html
- CUDA Binary Utilities v13.5, Instruction Set Reference: https://docs.nvidia.com/cuda/cuda-binary-utilities/index.html
- CUTLASS Blackwell SM100 GEMMs: https://docs.nvidia.com/cutlass/latest/media/docs/cpp/blackwell_functionality.html ; Changelog: https://docs.nvidia.com/cutlass/latest/CHANGELOG.html ; Functionality: https://docs.nvidia.com/cutlass/latest/media/docs/cpp/functionality.html
- Nsight Compute Profiling Guide: https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html
- DCGM feature overview: https://docs.nvidia.com/datacenter/dcgm/latest/user-guide/feature-overview.html

Papers and community measurements
- Luo et al., Benchmarking and Dissecting the Nvidia Hopper GPU Architecture: https://arxiv.org/abs/2402.13499 (and 2501.12084)
- Jarmusch, Graddon, Chandrasekaran, Dissecting the NVIDIA Blackwell Architecture with Microbenchmarks: https://arxiv.org/abs/2507.10789
- FlashGPU-sim (RTX 5090 MMA tables): https://arxiv.org/abs/2609.15311
- MMA-Sim: Bit-Accurate Reference Model of Tensor Cores and Matrix Cores: https://arxiv.org/abs/2511.10909
- Hayashi et al., DGEMM with Ozaki Scheme I/II on FP4 Tensor Cores (base-13 E2M1): https://arxiv.org/abs/2608.06812
- APEX4 (secondary statement that Blackwell removed INT4): https://arxiv.org/abs/2606.08761
- NVIDIA forum, "Does Blackwell support INT4 native?": https://forums.developer.nvidia.com/t/does-blackwell-support-int4-native/326513
- Colfax Research, NVFP4 GEMM on RTX PRO 6000: https://research.colfax-intl.com/optimizing-an-nvfp4-blockscaled-gemm-on-rtx-pro-6000-blackwell-gpu-sm120/
- nvfp4-train-sm120 (community): https://github.com/Shengboj0324/nvfp4-train-sm120
- T-MAC: https://arxiv.org/abs/2407.00088 , https://github.com/microsoft/T-MAC
- uops.info: VPMADDUBSW ymm https://uops.info/html-instr/VPMADDUBSW_YMM_YMM_YMM.html ; VPDPBUSD zmm https://uops.info/html-instr/VPDPBUSD_ZMM_ZMM_ZMM.html ; {vex} VPDPBUSD ymm https://uops.info/html-instr/VPDPBUSD_VEX_YMM_YMM_YMM.html

AMD
- LLVM AMDGPU TableGen: https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AMDGPU/GCNFeatures.td , GCNProcessors.td , VOP3PInstructions.td ; AMDGPUUsage: https://llvm.org/docs/AMDGPUUsage.html
- GPUOpen, WMMA on RDNA 3: https://gpuopen.com/learn/wmma_on_rdna3/ ; Matrix cores on RDNA 4: https://gpuopen.com/learn/using_matrix_core_amd_rdna4/
- ROCm GPU specifications: https://rocm.docs.amd.com/en/latest/reference/gpu-specs.html
- RX 9070 XT specifications: https://www.amd.com/en/products/graphics/desktops/radeon/9000-series/amd-radeon-rx-9070XT.html
- Ryzen 7 8840U: https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen/ryzen-8000-series/amd-ryzen-7-8840u.html ; Ryzen 5 PRO 5650U: https://en.wikichip.org/wiki/amd/ryzen_5/pro_5650u

Intel
- oneAPI GPU optimization guide, Iris Xe architecture: https://www.intel.com/content/www/us/en/docs/oneapi/optimization-guide-gpu/2023-0/intel-iris-xe-gpu-architecture.html
- Xe2 and Lunar Lake GPU (Tech Tour deck): https://cdrdv2-public.intel.com/824434/2024_Intel_Tech%20Tour%20TW_Xe2%20and%20Lunar%20Lakes%20GPU.pdf
- ARK i9-13900H: https://www.intel.com/content/www/us/en/products/sku/232135/intel-core-i913900h-processor-24m-cache-up-to-5-40-ghz/specifications.html ; i9-9900K: https://www.intel.com/content/www/us/en/products/sku/186605/intel-core-i99900k-processor-16m-cache-up-to-5-00-ghz/specifications.html
- Panther Lake: https://en.wikipedia.org/wiki/Panther_Lake_(microprocessor)

APIs and drivers
- VkComponentTypeKHR: https://docs.vulkan.org/refpages/latest/refpages/source/VkComponentTypeKHR.html ; VK_EXT_shader_ocp_microscaling_types: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_shader_ocp_microscaling_types.html
- SPV_INTEL_subgroup_matrix_multiply_accumulate: https://github.khronos.org/SPIRV-Registry/extensions/INTEL/SPV_INTEL_subgroup_matrix_multiply_accumulate.html
- Mesa features.txt: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/features.txt ; ANV cooperative matrix MR: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/25994
- AMD XDNA/IRON (Wikipedia summary of AMD sources): https://en.wikipedia.org/wiki/AI_engine

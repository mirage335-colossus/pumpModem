# Native receiver precision and CPU forecasting

## Automatic mode prioritizes computational ceiling

**Automatic algorithm and arithmetic selection must prioritize the highest
practical complete-receiver computational ceiling and DSP workspace capacity on
challenging cases, so reception does not fail through accumulating input
backlog.** This is the governing CPU/GPU default-selection objective. Optimizing
an easy case already well below that ceiling, conserving energy, or preserving
unneeded numerical precision must not take priority over challenging reception.
This is a design requirement, not a claim that current defaults sustain every
configuration; current measured limits remain in [validation](validation.md).

The [Default GUI/launch option](search-arithmetic.md) is the user's means of
obtaining the best supported computational throughput without having to select
the right arithmetic and computational structure for the available processor.
FP32-minimum and FP64-force overrides serve exceptional precision needs; their
availability makes conservative precision an inappropriate automatic-mode goal.
The intended quiet-spectrum use should rarely need those overrides, but that
expectation is not a proof about FFT intermediate range, accumulation or
conditioning. Do not impose FP64 on bulk work because one sensitive stage needs it.

Apply these rules to automatic selection and new optimization work:

- Prefer algorithms and structures with the strongest supported challenging-case
  capacity, including broad searches, long transforms, unsuccessful candidates
  and noise-only/failed-Helper work. Consider scaling, workspace admission,
  parallel batches, preparation overlap and shared-buffer ownership together.
  Keep every public hypothesis; doing less required reception work is not a
  throughput improvement.
- Prefer suitable throughput-oriented arithmetic, including FP32, packed FP16,
  Q15 or INT8, for that algorithm and hardware. Higher precision needs a
  worthwhile demonstrated reception, range, conditioning or complete-service
  benefit, or a documented absence of plausible computational benefit from
  reducing it. An FP64 algorithm doing much less work can correctly beat a
  lower-precision algorithm: current scoped chirp-Z selection is such a case.
  Fewer bits alone do not determine the winner.
- A neutral or slower easy-case timing must not exclude a lower-precision default
  whose supported scaling or workspace benefit protects more demanding cases.
  Do not restrict eligibility to the exact small fixtures measured without a
  technical reason. Use public geometry, hardware capability and applicable
  bounds to generalize, stating extrapolation and uncertainty. A plausible
  unmeasured benefit calls for bounded investigation, not a fabricated speedup
  or an indefinite higher-precision default justified only by missing tests.
  Once the evidence supports a better challenging-case tradeoff, select that
  route automatically throughout its supported range; do not require users to
  discover an obscure manual kernel or arithmetic choice to obtain it.
- If documented hardware throughput or credible measurements suggest an advantage
  that the implementation does not realize, investigate native libraries/driver
  features, actual dispatch and generated/executed instructions, packing,
  conversions, layout, batching, occupancy, transfers and synchronization.
  Correct implementation bottlenecks before declaring the arithmetic or hardware
  ceiling reached. Half-width storage with FP32 computation is a memory
  optimization, not evidence of packed FP16 arithmetic; an INT8 contraction is
  not an INT8 FFT.
- Compare complete affected-stage and receiver costs, including recurring
  preparation and downstream work; report workspace savings separately.
  Proportional sensitivity and false-detection tradeoffs, with uncertainty,
  govern suitability. A universal FP64 score tolerance or arbitrary sensitivity
  loss cutoff must not substitute for that assessment. Higher precision retained
  because a stage is unqualified is a reported investigation gap, not evidence
  that it is numerically necessary.

Preserve configured-calendar behavior, scored physical quiet-slot completion,
the full `W=2*H` software-worker budget and exact explicit resource limits.
Use bounded hierarchical experiments and the
[CPU ceiling workflow](robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates);
do not expand testing into exhaustive campaigns. Distinguish justified defaults,
conditional forecasts and measured sustained capacity. Report requested and actual
arithmetic and every fallback; theoretical peaks and isolated kernels do not
establish whole-receiver throughput or CPU capacity.

**Hardware selection follows the [GPU selection contract](triad-performance.md#gpu-selection-for-accelerated-development):
prefer a full RTX PRO 6000 Blackwell or a demonstrably more suitable modern
accelerator.** Do not let a familiar A100/A5000 or one slow small-matrix experiment
silently determine the development target. Qualify the actual precision and
complete-work speedup on the selected hardware.
Prefer INT8 when an available native implementation meets the applicable
receiver contract and offers the best supported complete cost/capacity tradeoff
for challenging geometry. A type selector or theoretical instruction rate is insufficient.
Judge sensitivity loss proportionally against the complete computational
benefit, including whether it enables reception several dB further into the
noise floor. The user's examples are that under 1 dB may be worthwhile, around
1.5 dB may be worthwhile for a multiple-times speedup, 2 dB deserves consideration
for a drastic improvement, and around 3 dB is less acceptable. These are not
automatic cutoffs or approval of an unqualified point estimate. A strict
score-difference threshold by itself is a numerical diagnostic, not a demonstrated
sensitivity-loss threshold. Compare with high-precision reception on predeclared
signal, noise and conditioning strata, report uncertainty and false-alarm effects,
and preserve full public work and physical completion. The sensitivity method
and applicable bounds remain under investigation.

Historical Tensor Core work includes successful NVFP4 DSP correlation and
split-BF16/TF32x3 phase contractions. See the [verified Git inventory](triad-gpu-optimization.md#verified-git-history-inventory)
for measured wins, instruction evidence and integration limits. A failed small
FP16 Gram or lag-search replacement is not evidence that Tensor Cores never
helped this project; qualification of the current backend is a separate question.

Use efficient search, batched correlations, reusable device-resident templates
and buffers, and suitable matrix shapes where measurements support them.
Reduced input precision with higher-precision accumulation or small sensitive
reductions is a candidate tradeoff. Keep computation on the GPU where useful;
include packing, transfers, synchronization and remaining CPU preparation when
judging the speedup. A faster isolated GEMM does not establish a faster test.
Preserve the case's selected effects, public search coverage and receiver
decisions rather than reducing required work to obtain a favorable timing.

## Precision opportunities by stage

Use the [representative hardware contract](triad-performance.md#representative-receiver-hardware)
for Ada desktop/laptop GPUs and Ryzen CPU/integrated-graphics targets. A device's
low-precision capability is useful only through an implemented, measured native
path. Qualify the numerical stage and the processing hardware separately.

| Stage | Precision and performance-test boundary |
| --- | --- |
| Composite audio/I/Q and channel isolation | Preserve the current FIR/projection arithmetic and explicit strong-Helper checks. Quiet spectrum after filtering does not bound the dynamic range of this composite input. Include filtering and transfer costs. |
| Helper-specific filtered baseband | Lower-precision tone/correlation processing is a candidate because strong isolated Helper signals can have modest in-band dynamic range. It is not implemented as a low-precision GPU backend. Qualify frequency error and uncertainty before using them to narrow DATA work; phase/timing transfer needs its own evidence. |
| DATA-specific filtered baseband | Qualified native CPU FP32 FFTs, Vulkan FP32 FFTs and optional CUDA FP16/INT8 lag matrices operate here. Test scaling, clipping/nonfinite counts, accumulator bounds, full scores and candidate/decision stability, including noise-only and near-decision observations. |
| Broad frequency/arrival search | Measure full public coverage and complete search/selection cost. Current FFT and optional dense lag matrices are different algorithms; an arithmetic selector does not change an FFT into an INT8 transform. |
| Fine statistics, coordinates and persistent evidence | Retain precision where long accumulation, small frequency/phase/timing changes or conditioning affects state and decisions. A bulk-matrix pass does not qualify reduced-precision state updates. |
| Receiver scheduling and completion | Exercise native worker ownership, preparation overlap, bounded workspace and scored quiet-slot finish independently of arithmetic. Component utilization or a small transform win is not complete receiver capacity. |

Tensor Core FFT/filter-bank factorizations, batched small transforms and other
structured contractions are useful candidates for broad low-SNR searches after
channel isolation. They require their own native implementation, work mapping,
accuracy and complete-cost comparison. A dense DFT's quadratic work cannot be
made equivalent to an FFT's `N log N` scaling merely by tiling or selecting INT8.
The current `int8` option selects a bounded matrix contraction; unsupported or
CUDA frequency-tiled work explicitly stays on FFT. cuFFT's documented transform
precisions are distinct from low-precision matrix multiplication. See
[cuFFT's transform and precision documentation](https://docs.nvidia.com/cuda/cufft/).

Prefer the algorithm with the qualified ceiling on the most challenging
applicable geometry. A replacement default must cover unsuccessful Helper
acquisition and broad search, include recurring preparation and downstream work,
and preserve all hypotheses under the target memory bound. An easy-case speedup
well below real-time capacity is not a reason to choose a lower worst-case
ceiling. CUDA retains its full FFT default; the qualified Radeon direct route
has a separate explicitly bounded envelope and must retain full FFT fallback.

## Current implementation

The default compiled receiver uses automatic native CPU arithmetic and has no
Torch runtime dependency. On qualified native exact single-ray AVX2 frequency
searches not routed to chirp-Z, post-filter spectra use Q15 products and FP32
FFTs, subject to checked native transform and workspace bounds;
other qualified AVX2 frequency routes use FP32 products and FFTs after every
double-precision template Gram passes the `1/16` minimum eigenvalue-ratio guard.
Lag groups, compact tight-clock geometry, ill-conditioned banks and unqualified
CPU kernels retain explicit FP64 routes.
Coordinates, phase, Gram/quadratic scoring and fine state retain double precision.
Rare extreme post-filter exponents use exact power-of-two block scaling and
double-precision correlation restoration. See the [operating arithmetic policy](search-arithmetic.md)
for shared GUI/launch controls and the CPU reference override. This policy keeps
the full worker allocation and conservative memory admission.

For a native single-ray AVX2 exact-projection frequency search, fused preparation
can convert checked templates directly to FP32 spectra and quote a smaller
phase-aware memory bound. The bound still charges FP64 projection/FP32-plan
overlap and conversion work; other routes keep their existing bounds. Whether
that reduces the complete slot lease depends on the separate Helper and fine
bounds. See the [bounded comparison](validation.md#lower-precision-and-workspace-investigation).

The optional `DATAPUMP_TRIAD_TENSOR` build links a C++ ATen
bulk backend into that same receiver. Public coarse search, fine state, work
accounting, decisions and completion remain in the production C++ control path;
Python does not duplicate those algorithms.

`TensorReceiverOptions` selects CPU or an exact CUDA device, a bounded shared
workspace, `automatic` or `fp64` precision, and a separate coarse arithmetic
policy. The native workload CLI exposes `--tensor-precision automatic|fp64` and
`--tensor-coarse-math fft|fp16|int8`; `fft` remains the default. Automatic bulk
work normally uses FP32 with
FP64 coordinates and reductions. The compact tight-clock class (sample rate at
most 10 Hz and positive clock span at most 0.001 ppm) selects FP64. Receipts
report the actual coarse and fine arithmetic, device/runtime and memory use.
Fine Gaussian state retains the native higher-precision policy. No arithmetic
is silently selected from the observed score or transmitted truth.

### Vulkan on Vega integrated graphics

The optional `DATAPUMP_TRIAD_VULKAN` backend uses in-process Vulkan/VkFFT for
filtered DATA frequency-search FFTs, scoring and exact arrival frontiers.
The FFT route uses FP32 arithmetic; coordinates and quadratic scoring
retain higher precision. On the locally qualified device and geometry below,
automatic mode uses packed FP16 direct products with FP32 accumulation and FP64
scoring. Other shapes can use qualified FP16 FFT storage with FP32 FFT arithmetic
or FP32 storage. Native FIR keeps strong Helper energy out of this
reduced-precision search. Helper acquisition, template generation, lag groups,
the compact tight-clock FP64 class and fine processing stay on the canonical CPU
path with explicit stage routing. A numerical or resource failure must not
silently fall back. See the [native Vulkan scope](triad-performance.md#local-radeon-vulkan-processing)
and [current qualification](validation.md#local-radeon-vulkan-backend).

The GUI's FP64-force preference is incompatible with this FP32 Vulkan backend
and is rejected explicitly. CPU automatic versus forced-FP64 diagnostics remain
separate from GPU device/work evidence.

On the 5650U, exposed shader FP16/INT8 types and the integer-dot extension do not
by themselves prove packed throughput or accelerated dot products. The observed
RADV device reports FP16 support but no accelerated 8-bit integer-dot variants.
Vega can execute packed FP16 vector arithmetic; it has no qualified matrix/Tensor
Core path here. The current FFT implementation does not use that packed
arithmetic. VkFFT's FP16-storage mode still computes in FP32, so neither its
storage size nor a half-typed buffer establishes a 2× arithmetic gain.
Keep the [hardware peak model](triad-performance.md#radeon-arithmetic-ceilings)
separate from measured affected-phase and complete-receiver speedups.

Half storage and packed arithmetic are different operations: VkFFT half storage
retains FP32 compute, while the direct correlation executes packed FP16 products
and FP32 reductions. Initial diagnostic score differences exceeding `3e-6` do
not measure sensitivity loss. Bounded physical-reception checks and complete
message measurements support automatic packed direct work only for Mesa RADV
25.0.7 on PCI `1002:1638`, with compatible little-endian IEEE host, 16-bit buffer,
shader FP16 and compute-subgroup support. The public geometry must have one ray,
one arrival and at most 131,072 unpadded projected template rows; frequency and
workspace totals are independently processed in bounded tiles subject to native
plan and device limits. Exact power-of-two energy normalization is required.
Longer eligible direct correlations or unsupported scaling use FP32 direct
arithmetic; other unsupported geometry uses qualified half-storage FFT, FP32 GPU
work or an explicit native fallback as applicable. The packed-length boundary is
a conservative numerical envelope, not a universal reception-sensitivity proof;
direct near-threshold checks currently extend to 65,670 template rows. The tested cases do not establish
population sensitivity, every admitted combination, other drivers or whole-receiver
capacity. Packed direct and packed FFT are different algorithms, with different
cost scaling. See [the investigation](validation.md#packed-direct-correlation-follow-through).

The local RADV path uses [Mesa's normal ACO compiler](https://docs.mesa3d.org/drivers/radv.html);
switching its compiler backend is not an established optimization for this workload. AMD's current
experimental [TheRock device status](https://github.com/ROCm/TheRock/blob/main/SUPPORTED_GPUS.md)
lists `gfx90c` as build-passing, not sanity-tested; its nightlies include initial
rocFFT/hipFFT support. The matching packages would total approximately 829 MiB
compressed. Here a non-destructive host check found `/dev/kfd` inaccessible to
the current user, although the Vulkan render node was usable; AMD documents
`/dev/kfd` as a [ROCm compute requirement](https://rocm.docs.amd.com/projects/install-on-linux/en/docs-7.2.3/how-to/docker.html).
No driver, permissions or packages were changed. This is an unexecuted ROCm
alternative, not proof that the GPU lacks a possible ROCm implementation.

Current Helper FFT work occurs before channel isolation and retains double
precision; lowering it based only on the later quiet passband is not justified.
Fine Gaussian cancellation/conditioning likewise remains a separate precision
requirement. A small quadratic-scoring fraction alone does not justify changing
its precision while FFT/product work dominates the accelerated phase.

### Portable FP32 mapping arithmetic

FP32 mapped-basis rotation now declares `fp32-rn4-v1`: compute all four real
products with separate round-to-nearest rounding, then round the real subtraction
and imaginary addition separately. The fused CUDA mapper and native ATen
reference implement the same contract; their exact comparison and independent
arithmetic goldens remain required. The component identity reports
`mapping_fp32_arithmetic_contract`. FP64 mapping is unchanged.

This is an explicit arithmetic revision, not a device-specific tolerance or an
assumption that installed C10 complex multiplication chooses the same FMA
association on every CUDA architecture. The first Ada check reproduced an
association difference after exact interpolation/phase/trigonometric agreement.
The new contract passes the bounded actual RTX 4090 CUDA suite. An initial composite
unit-rotation golden incorrectly assumed identical rounded sine/cosine values;
its correction uses exact zero-angle and negative-integer phase wrap while
preserving the independent multiplication goldens and exact mapper comparison.
The corrected suite also retains the native receiver, near-decision, quiet and
strong-Helper checks. This is scoped Ada evidence, not a cross-device guarantee.
See [current outcomes](validation.md#representative-receiver-hardware-checks)
and keep both original failures. Earlier automatic-FP32 fine results bind their old source
and arithmetic; they cannot qualify this revision. Preserve the complete
receiver's native comparison, near-decision, quiet and strong-Helper gates.

### Stage boundaries and optional matrix processing

The optional FP16/INT8 matrix path is implemented in the canonical C++/CUDA
backend using cuBLAS. Bounded actual-GPU fixtures and two large post-channelization
shapes show numerical passes and lower bulk cost on the RTX PRO 6000. These are
scoped component results, not complete-receiver or backlog qualification. See
the [current results and failures](validation.md#native-matrix-and-preparation-follow-through)
before selecting an arithmetic mode.

Quantization belongs after channel isolation. The current coarse matrix entry
receives filtered DATA observations and templates. Raw composite I/Q, which may
contain much stronger Helper tones, keeps the existing FP32 FIR path or its
FP64 precision class; do not replace that input with INT8 or FP4. Fine DATA
observations are separately projected in native FP64 before the bulk interface.
Helper filtering remains native and supplies frequency intervals to DATA search.
Helper phase/timing transfer and a low-precision Helper backend are separate,
unqualified extensions. Quiet spectrum makes lower-precision bulk arithmetic
worth testing; it does not establish accumulation accuracy or conditioning.

| Coarse matrix request | Implemented arithmetic |
| --- | --- |
| `fp16` | Per-chunk row/column normalization, FP16 products with FP32 accumulation, and FP64 complex reconstruction and outer accumulation. |
| `int8` | Symmetric `[-127,127]` operands, independently initialized INT32 partials, and FP64 rescaling/reconstruction. At most 512 complex samples become 1,024 real products per partial: `1024*127² = 16,516,096 < 2²⁴`, so the integer sum cannot overflow. This bound does not remove input quantization error. |

The new FP4 candidate was removed after failing its frozen numerical gates.
Single-product E2M1/block-16 arithmetic produced `0.050318` normalized score
error against a 1% limit. Four-product high/residual reconstruction and a later
16× residual-scale repair still changed the unique noise peak. Favorable active
bulk timings did not resolve that failure or demonstrate an advantage over
qualified INT8. There is no current FP4 selector. This scoped result does not
invalidate the successful historical NVFP4 implementation or rule out a future
native design. Preserve that experiment's failed gates and lack of demonstrated
complete-cost benefit; a new candidate must be evaluated under the
[proportional sensitivity/capacity policy](#automatic-mode-prioritizes-computational-ceiling),
not rejected solely by that historical score or peak-agreement cutoff.

The implementation reuses per-stage native library handles and prepared
lag-group coefficient matrices. It combines up to 32 independent workspaces,
64 arrivals and two taps into a matrix with up to 4,096 complex rows, reducing
the risk that tiny products hide useful Tensor Core parallelism. Admission
reduces those batch dimensions when needed; it does not reduce the receiver's
global worker budget or search coverage. Coefficients, packing/library scratch,
results and host staging remain charged through their full lifetimes. Results
are published as complete ordered workspace grids in slices of at most eight.

Current matrix eligibility is 16–8,192 input samples, at least 16 arrivals,
and 8–256 frequencies per lag group, with total coefficient storage bounded by
`min(32 MiB, shared device workspace/8)`. Frequency-tiled searches, precise FP64
stages, unsupported devices/shapes and insufficient workspace retain the full
FFT calculation. Receipts distinguish `cpu-device`, `fp64-stage`,
`frequency-tile-route`, `unfavorable-shape`, `workspace-limit` and
`device-capability` fallbacks. A requested CUDA backend still must execute on
CUDA; this arithmetic fallback does not turn CPU work into GPU evidence.

The scalable CUDA frequency-tile and chirp/zoom FFT searches remain the default.
For a uniform grid, direct matrix correlation grows with samples times searched
frequencies; tiling bounds its storage but does not remove that arithmetic growth.
FFT search scales differently and must remain available for broad geometry.
Select a matrix default only from qualified, complete-cost measurements covering
the hardest applicable shapes. A narrow GEMM win or an unfavorable tiny Gram
measurement cannot decide the broad-search default. Tensor Core FFT factorization
would be a distinct algorithm to implement and qualify, not an INT8 cuFFT switch.

The CUDA frequency-tile route now implements exact arrival-peak selection on
the device, returning a fixed 40-byte frontier per workspace/frequency instead
of the complete FP64 arrival row. It still computes and validates every score;
logical score/work counts remain unchanged. Its contract matches native
plateau midpoints, ties, endpoints, separation and the two-peak ordering.
Contiguous score copies, packed device output and host publication buffers
remain charged until readback completes. Native and actual-CUDA fixtures pass,
and a bounded 32-frequency comparison improves fresh-owner bulk cost while
preserving every score and the exact frontier. The full 8,596-frequency
active/noise public-slot checks also pass with lower measured elapsed time
than the same-host CPU checks. Sustained backlog and CPU-forecast qualification
remain separate; see the current validation record.

Fine sessions also retain exact-value projection-bin and mapping-offset
descriptors, alongside the existing ownership-aware source-tile cache. Changed
bins, rows or offsets invalidate the relevant descriptor; fresh observations,
mapping coefficients and keyed source identities remain independent. Optional
caching can fall back to charged transient uploads under memory pressure.
Common Gaussian transition coefficients are now prepared once per chronological
block pair in native dispatch, including missing blocks. These are transfers of
the historical preparation/reuse lessons, not measured speedup claims.

The singleton DC fine Gram uses a direct C128 product even in precise stages;
the existing small direct Grams remain. Other CUDA fine Grams use an owned C128
ZGEMM with a charged 4 MiB caller workspace, avoiding ATen's process-lifetime
cuBLAS workspace cache. CPU BMM is
unchanged. The repaired actual-CUDA suite passes the unchanged 128 MiB fixture
and repeated fresh fine-owner allocation stress. These checks qualify the
tested ownership paths, not every device/configuration or all library memory.

Matrix receipts record requested arithmetic, loaded cuBLAS/SM identity,
successful product/chunk counts, actual peak dimensions, padded arithmetic,
packing counts, nonfinite/saturation diagnostics and scale maxima. Descriptor
hit/upload counters expose reuse. These counters do not establish hardware
occupancy or instruction execution; retain actual-device profiling and complete
elapsed costs as separate evidence.

The maintained [native component mode](triad-performance.md#bounded-native-component-comparisons)
checks the requested FP16/INT8 contractions and exact GPU peak reduction as
separate production operations. It rejects matrix fallbacks as matrix
qualification, binds fixture/input/device identities, and includes packing,
preparation and search in its fresh-owner costs. Its three-pair ratios remain
component evidence. The CUDA full FFT default and complete receiver qualification
gates are unchanged.
Its first actual RTX 4090 run passes both matrix modes and exact peak reduction
within 6.439 s aggregate. The [source-bound results](validation.md#representative-receiver-hardware-checks)
record the preceding failures and arithmetic revision; they do not qualify the
laptop/4060 Ti/APU targets or instruction-level Tensor Core utilization.

### Existing qualification and remaining limits

A requested CUDA backend must execute on CUDA or report unavailable; CPU fallback
is not GPU evidence. The CMake `DATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA` option requires
a CUDA-capable LibTorch build and registers the actual CUDA receiver check.
The CUDA backend now requires NVCC and the CUDA SDK. The measured SM 120
Blackwell build uses `CMAKE_CUDA_ARCHITECTURES=120` and
`TORCH_CUDA_ARCH_LIST=12.0`; choose an explicit applicable architecture for
another device rather than treating these as portable defaults. Optional CPU
ATen and default builds without Torch retain their existing dependencies.

The exact native baseline now has bounded CUDA parity and active/noise
functional evidence on a full RTX PRO 6000 Blackwell Server GPU. That evidence
does not qualify later transfer/arithmetic changes, all precision classes,
tensor-core execution, throughput or local CPU forecasts. CPU ATen checks,
source/build closure checks, CUDA-branch host syntax and workflow tests retain
their narrower scope. See
[current validation](validation.md#native-throughput-and-precision-investigation).

The workload pairs exact native CPU and CUDA observations for active, noise,
cold-maximum, terminal and erasure work. The last three are non-admitting
fine-only diagnostics; active/noise complete public acquisition. Separate full
operational near-decision, high-C/N0 and scored-quiet noise challenges check
decisions. The gate checks winner/order, fixed work, score tolerance
`atol=2e-3, rtol=2e-4` and at most 5% adaptive work-demand drift. A paired report
must cover the actual precision classes used by its target rows. Matching bits
alone does not establish matched observations or useful arithmetic fidelity.
Predeclare a `--near-cn0` override before the one-row challenge when needed;
do not choose it after seeing a favorable outcome.

The historical active fused-mapper comparison required its declared exact work
gate. The first real row-5 pair changed adaptive prefix candidate work despite matching
retained-state/transition counts; an installed C10 versus fused final-multiply
FMA difference was isolated. The explicit final-FMA repair passed exact
pointwise, full-slot and failed-row checkpoint work/state checks on the actual
RTX PRO 6000. That resolves the measured parity failure for the installed
runtime; it was not a bitwise guarantee for other LibTorch/compiler/device
combinations. The original failure and repaired run remain source-bound evidence;
the [new explicit FP32 contract](#portable-fp32-mapping-arithmetic) replaces that
installed-library association and needs its own qualification. Six exact paired looks show
fine and receiver-phase improvements for one row/seed and bounded continuation;
they do not establish whole-stream throughput or complete CPU forecasts. See
[the current evidence](validation.md#native-throughput-and-precision-investigation).

The current FP16 Gram-shape experiment met its arithmetic check but took
184 µs versus 119 µs for FP64 batched multiplication and 33 µs for a direct
FP64 experiment. That low-precision candidate is stopped. Its WMMA name does
not establish instruction-level tensor-core execution. The accepted CUDA
direct-FP64 Gram path applies to at most 128 modes in its non-DC,
automatic-precision class; other shapes retain batched multiplication.
Six paired DATA/QUIET observations preserved exact work/state, with maximum
score difference `1.86e-9`, and reduced fine and measured receiver-phase time
relative to the repaired fused mapper. This bounded row/seed result does not
qualify other geometry or whole-stream capacity.

The completed explicit-FP64 row-5 route passed five paired strata and three
operational challenges with exact work/acquisition and zero adaptive drift.
It supports partial cold, terminal and erasure CPU component forecasts. Active
and quiet work falls outside the unchanged local 5% cost-regime gate, so nominal,
conservative and complete-receiver forecasts remain unresolved. The automatic
class's high-SNR acquisition-coordinate failure remains separate; passing
explicit FP64 does not qualify automatic precision. Seven other manual rows
have geometry-only coverage. See [final limits and intervals](validation.md#native-throughput-and-precision-investigation).

A low-precision dtype, ATen operation or CUDA label does not prove tensor-core
execution or a speedup. Use actual execution and elapsed measurements, including
preparation, packing, transfers and synchronization. The present backend provides
no claim of tensor-core acceleration merely because ATen is enabled.

## Qualifying a faster path

Declare the arithmetic/error budget and representative work classes before
testing. Pair each used coarse/fine precision class with the CPU reference and
check candidate order, acquisition coordinates, scores, fixed-affine decisions,
state/work counts and native-cache behavior. Include noise-only, near-decision
and relevant high-SNR or ill-conditioned states; quiet spectrum alone does not
guarantee numerical conditioning. Separate numerical regression diagnostics from
reception, false-detection and service-cost outcomes. Existing exact arithmetic,
protocol and work-accounting checks keep their specific contracts; a new
precision candidate need not reproduce every FP64 score or candidate ranking
to demonstrate a worthwhile receiver tradeoff. Keep higher precision where the
evidence justifies it under the automatic-mode contract, and label conservative
implementation choices separately from demonstrated precision requirements.
Record requested and effective arithmetic,
actual backend execution and any fallback; do not silently label CPU or FP64
fallback work as low-precision GPU acceleration.

A GPU-only optimization needs new applicable GPU precision/work evidence while
unchanged CPU calibration remains reusable. A CPU algorithm or arithmetic
change needs fresh affected local CPU costs. GPU speed determines how quickly
work demand can be observed; source-matched CPU phase costs and independent
local holdouts determine the forecast. Do not multiply GPU elapsed time by a
generic FLOPS ratio. Follow the
[GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).

The workload gate reports numerical parity separately from CPU cost-work
compatibility. Executed FP16 or INT8 matrix work cannot qualify the existing
FFT-based CPU forecast model until an applicable CPU work mapping is measured.
Exact CUDA frontier reduction preserves the CPU logical-work projection through
`workspace_frequency_pairs`, `score_rows` and retained peaks/proposals. Its
`cuda_arrival_peak_rows` telemetry is not a replacement CPU work feature or a
GPU-time-to-CPU conversion. Full-grid/frontier parity and all existing source,
runtime, local calibration and holdout gates still apply.
Requested arithmetic is also part of relative-regression runtime identity.
Changed common transition preparation needs fresh affected CPU timings; neither
the old CPU costs nor a GPU product count alone supplies that calibration.

The [validation record](validation.md#gpu-assisted-receiver-measurement)
distinguishes the earlier accepted eight-row timing forecasts from unresolved
conservative CPU capacity and the restructuring's new source identity. Earlier
precision and timing failures remain source-bound evidence, not blanket
prohibitions on reduced precision.

## Retained lessons

These are source-bound findings from retired Python-orchestrated receivers, not
qualification of the current native ATen implementation:

- A high-SNR FP32 Gaussian innovation failure motivated FP64 state updates. This
  is a sensitive-stage finding, not a blanket requirement for FP64 bulk work or
  extreme in-band dynamic range.
- Compact tight-clock acquisition needed FP64 before pruning because small score
  changes altered candidate rankings and downstream work. Bulk and sensitive
  arithmetic classes must be paired separately.
- TF32x3, rounded TF32 and scaled FP16 direct-DFT lag-search experiments passed
  bounded row-3 checks on RTX PRO 6000 but did not improve the integrated receiver.
  Scaled FP16 sped up isolated packing-inclusive transforms by 1.33–1.88× while
  acquisition stages took longer and common stages were effectively unchanged.
  Those implementations were removed. The current native candidate adds shared
  workspace/arrival batching and bounded resident coefficients; measure those
  changed surrounding costs before treating it as a successful replacement.
- A prior GPU filter-staging/memory-lifetime edit lacked fresh CUDA execution
  after replacement capacity became unavailable. Earlier device qualification
  did not cover the changed source.

The [tensor experiment conclusion](validation.md#tensor-core-lag-search-conclusion),
[historical GPU measurement limits](validation.md#gpu-assisted-receiver-measurement)
and [optimization summary](triad-gpu-optimization.md) retain provenance and
failures. Old precision switches, standalone receivers and calibration reports
are not current APIs. Fresh native costs and independent holdouts are required;
do not infer them from the old host/GPU split. Device-resident batched or fused
formulations remain candidates until implemented and measured with complete work.
Preserve these concise lessons under the
[material lifecycle](development.md#material-lifecycle), not obsolete executable
campaigns or copied source trees.

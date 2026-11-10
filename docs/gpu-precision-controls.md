# Precision for GPU-assisted CPU forecasting

**Prefer practical lower-precision and tensor-compatible bulk arithmetic when
it speeds up qualified manual-suite forecasting.** The intended receiver
normally operates on quiet or mostly quiet spectrum, including FHSS; extreme
in-band dynamic range is not a general usage requirement. Do not impose FP64
on all work because one numerical stage needs it. Check the relevant error and
decision sensitivity with the bounded method in the
[CPU ceiling workflow](robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates).

Use efficient search, batched correlations, reusable device-resident templates
and buffers, and suitable matrix shapes where measurements support them.
Reduced input precision with higher-precision accumulation or small sensitive
reductions is a candidate tradeoff. Keep computation on the GPU where useful;
include packing, transfers, synchronization and remaining CPU preparation when
judging the speedup. A faster isolated GEMM does not establish a faster test.
Preserve the case's selected effects, public search coverage and receiver
decisions rather than reducing required work to obtain a favorable timing.

## Current implementation

The default compiled receiver uses its native CPU arithmetic and has no Torch
runtime dependency. The optional `DATAPUMP_TRIAD_TENSOR` build links a C++ ATen
bulk backend into that same receiver. Public coarse search, fine state, work
accounting, decisions and completion remain in the production C++ control path;
Python does not duplicate those algorithms.

`TensorReceiverOptions` selects CPU or an exact CUDA device, a bounded shared
workspace and `automatic` or `fp64` precision. The native workload CLI exposes
`--tensor-precision automatic|fp64`. Automatic bulk work normally uses FP32 with
FP64 coordinates and reductions. The compact tight-clock class (sample rate at
most 10 Hz and positive clock span at most 0.001 ppm) selects FP64. Receipts
report the actual coarse and fine arithmetic, device/runtime and memory use.
Fine Gaussian state retains the native higher-precision policy. No arithmetic
is silently selected from the observed score or transmitted truth.

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

The active fused-mapper comparison requires its declared exact work gate. The
first real row-5 pair changed adaptive prefix candidate work despite matching
retained-state/transition counts; an installed C10 versus fused final-multiply
FMA difference was isolated. The explicit final-FMA repair passed exact
pointwise, full-slot and failed-row checkpoint work/state checks on the actual
RTX PRO 6000. That resolves the measured parity failure for the installed
runtime; it is not a bitwise guarantee for other LibTorch/compiler/device
combinations. The oracle must reject changed installed arithmetic. Preserve the
original failure separately from the repaired run. Six exact paired looks show
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
guarantee numerical conditioning. Keep higher precision in the small stages
where the paired checks need it. Record requested and effective arithmetic,
actual backend execution and any fallback; do not silently label CPU or FP64
fallback work as low-precision GPU acceleration.

A GPU-only optimization needs new applicable GPU precision/work evidence while
unchanged CPU calibration remains reusable. A CPU algorithm or arithmetic
change needs fresh affected local CPU costs. GPU speed determines how quickly
work demand can be observed; source-matched CPU phase costs and independent
local holdouts determine the forecast. Do not multiply GPU elapsed time by a
generic FLOPS ratio. Follow the
[GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).

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
  Those implementations were removed. Do not restore that replacement without a
  changed workload or a concrete removal of surrounding costs.
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

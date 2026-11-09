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

The bounded RunPod workload uses CUDA for coarse search and fine bulk work.
Bulk arithmetic normally uses FP32/complex64. The compact tight-clock class
uses FP64/complex128 for the entire coarse grid before pruning and for fine
rendering, because small changes affected candidate rankings and downstream
work. Fine coordinates, Hermitian Gram calculations and Gaussian state retain
their explicit higher-precision paths. The headless CPU receiver has its own
explicit arithmetic; its fine Gaussian policy defaults to FP64 after a
reproduced high-SNR FP32 innovation failure. That is a stage-specific finding,
not a requirement for high dynamic range throughout the receiver.

The current GPU-only filter upload uses bounded host staging and retains
CUDA polyphase filtering. Its revised memory admission and exceptional-upload
lifetime have unit coverage, but the new source still needs execution on CUDA
because replacement RunPod capacity was unavailable. The earlier device
qualification does not qualify these edits; see the
[current GPU limits](validation.md#gpu-assisted-receiver-measurement).

The maintained backend has **no enabled tensor-core DFT path**. TF32x3,
rounded TF32 and power-of-two-scaled FP16 lag-search variants were implemented
and passed the bounded row-3 receiver checks on RTX PRO 6000. None demonstrated
a useful integrated speedup, so all three were removed. Scaled FP16 improved
packing-inclusive isolated transforms by 1.33–1.88×, but the receiver's changed
acquisition stages took longer and its common measured stages were effectively
unchanged. See the [tensor experiment conclusion](validation.md#tensor-core-lag-search-conclusion).
Do not repeat that direct-DFT replacement without a changed workload or a
concrete way to remove its measured surrounding costs. Different batched or
device-resident formulations remain development candidates, not available
switches or measured speedups. A low-precision dtype or matrix operation alone does not prove tensor
execution. The existing Gaussian FP32 guard requires TF32 disabled; a new
tensor path must have its own applicable checks.

The restructured CPU path also removes repeated bank synthesis and combines
fine render/projection work into candidate tiles. Reprofile the resulting
complete workflow before choosing the next tensor operation: the old host/GPU
cost split is not a current speedup forecast. A possible FHSS formulation is
local pulse-stencil matrices multiplied by batched keyed coefficients, followed
by accurate phase/gate handling and resident projection. Its coefficient/plan
adapter is not implemented or qualified. Compare a fused CUDA stencil with a
scaled FP16/FP32-accumulation implementation, including preparation, transfers
and actual receiver decisions; do not restore the rejected direct-DFT path or
advertise hypothetical tensor throughput as local CPU capacity.

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

The [GPU optimization summary](triad-gpu-optimization.md) preserves transferable
arithmetic and negative-result lessons from retired receivers. Their Search
arithmetic dropdown, standalone switches and receiver APIs are no longer current
interfaces. Historical speedups or failures do not qualify the current receiver.
Follow the [material lifecycle](development.md#material-lifecycle) rather than
restoring old implementations or exhaustive precision campaigns.

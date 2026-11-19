# Retained GPU optimization lessons

Follow [precision controls](gpu-precision-controls.md) and the
[current abbreviated policy](triad-performance.md). The former frozen full-run
optimization ladder, sensitivity sweeps and calibration campaigns are retired.
There is one canonical receiver implementation; alternative arithmetic belongs
inside the canonical implementation. The historical precision controls and
standalone GPU receiver APIs discussed in these reports have been removed.
This page records their evidence and [native disposition](#native-disposition-of-the-historical-techniques);
the current options and qualification rules are in the
[native precision guide](gpu-precision-controls.md).

**Successful Tensor Core DSP implementations exist in Git history.** NVFP4
accelerated an actual large correlation batch on RTX PRO 6000; split-BF16 and
TF32x3 accelerated a phase-state workload on RTX 5090. The recent small FP16
Gram failure does not negate those results. Their arithmetic, layout and reuse
techniques deserve consideration inside the current native backend, with new
qualification for its waveform, shapes and complete work.

**Preserve the hardest-search computational ceiling when selecting defaults.**
The canonical native backend now contains optional FP16 and INT8
post-channelization lag-DFT paths with reusable coefficients and bounded
workspace/arrival batching. Bounded GPU fixtures and two large bulk shapes pass;
complete-receiver qualification remains separate. The historical FP4 coarse
lag-matrix candidate was removed after failing the then-frozen noise-peak gate.
The resident fine projection described below remains in the native receiver.
That historical diagnostic failure alone is not a universal precision veto:
current qualification compares complete cost, physical reception, false alarms
and measured sensitivity loss with the user's proportional tradeoff in the
[precision guide](gpu-precision-controls.md). CUDA full frequency-tile and
chirp/zoom FFT coverage remains the default: a dense matrix's sample-by-frequency
arithmetic growth does not disappear when its storage is tiled. The qualified
Radeon direct route has a separately bounded envelope. Promote a different
default only with applicable complete-work and backlog evidence; an easy-case
efficiency result is insufficient.

## Continue Blackwell FP4 work

Start with the [latest same-device baseline](validation.md#blackwell-shared-bit-mapping-and-coarse-phase-reuse--19-november-2026),
the [five-slot timing breakdown](validation.md#five-slot-timing-and-measurement-scopes),
and the [exact reproduction](#reproduce-the-matched-blackwell-runs). The
tracked record carries the fixture identities, setup and work counts needed
to continue from a fresh checkout. The timings distinguish native stage
spans, complete service, and historical device-only matrix measurements.

Improve complete receiver service on challenging work. Preserve every public
hypothesis, the configured calendar and keyed ordinals, bounded workspace,
physical quiet-slot completion, and the full automatic `W = 2*H` software
worker budget. Treat the four-worker and 24-worker values below as explicit
benchmark allocations. An eligible fine owner can temporarily have fewer
lanes than the full receiver. The current automatic Vulkan/OpenCL route
selects resident FP4 for the qualified geometry with at most four fine-owner
lanes. Native fine won the fresh 24-worker severe and quiet comparisons.
The crossover between those allocations remains open. Compare at full
admitted budget when qualifying wider automatic dispatch.
[Precision policy](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
· [Worker allocation](development.md#receiver-cpu-allocation).

### Receiver path and timing landmarks

| Work | Where it runs and where to inspect |
| --- | --- |
| Source, channel and receiver service | The [native probe](../tools/triad-receiver-probe.cpp) generates the input and replays it through the [stream receiver](../src/triad_stream_receiver.cpp). Application scheduling, ordered commitments, codec and physical completion remain native. Inspect `score_slot_coarse` and `score_slot_fine` in [the receiver](../src/triad_receiver.cpp) and `stream_run` in the probe for timing boundaries. |
| Coarse search | [Coarse preparation](../src/triad_receiver_coarse.cpp) has native observation channelization, keyed template bank, exact cubic warp and reused Doppler phase. `Projection::apply` performs native overlap-add FFT convolution on the filtered Vulkan route. [Vulkan](../src/triad_vulkan_backend.cpp) transforms the bank and rotated observations, forms correlation products, does inverse FFT and quadratic scoring, then compacts the exact peak frontier before readback. The matched broad geometry has 78 workspaces, 15 frequencies and 3,377 arrivals, with two rays per template. |
| Fine source and ownership | `FineSession` in [fine processing](../src/triad_receiver_fine.cpp) prepares receiver observations and advances tracking and likelihood state. [MappedBasis](../src/triad_receiver_basis.cpp) produces and pins immutable keyed source tiles and exports the native mappings. It groups compatible ready blocks without advancing later state early. Candidate sets larger than one GPU submission retain every candidate. |
| OpenCL resident work | [Host projection and staging](../src/triad_opencl_nvfp4_projection.cpp) owns the queue, tile cache, descriptor flush, uploads and readback. [Device kernels](../src/triad_opencl_nvfp4_kernels.cu) render the paired maps and determine their scale. The FP4 path packs templates and coefficients and runs block-scaled E2M1 Tensor Core projection with FP32 accumulation. The [resident FFT](../src/triad_opencl_fine_fft.cuh) uses FP32 arithmetic and gathers the same bins. Both compute FP64 sufficient statistics on the device. The receiver reads those statistics and applies native state updates. |
| Fallback and receipts | [Vulkan routing](../src/triad_vulkan_backend.cpp) defines public geometry, shared device admission and native terminal, Helper-taper or workspace fallback. [Bulk work](../src/triad_receiver_bulk.hpp) defines what the counters cover. The distinct native CUDA path lives in [the CUDA backend](../src/triad_tensor_backend.cpp) and supports direct cuFFT and NVFP4 controls. Its results qualify that backend and runtime. |

The latest resident gain shortened the work around the matrix. Use the
[prior 576-lane matrix-only and large cuBLASLt controls](#same-card-resident-and-staged-controls)
for scale. The latter reached 1.309 PFLOP/s in a large dense synthetic
multiplication, compared with a roughly 2 PFLOP/s dense hardware reference.
The severe slot's 404 paired submissions and one single submission issue
137.424 GFLOP at `N=96` and `K=3072` padded. Applying the old already-packed
9.22 µs paired and 8.93 µs single event medians to that schedule gives about
3.7 ms of matrix time as an illustrative fixed-shape estimate. The latest
severe fine span is about one second. Issued-equivalent throughput over
fine-stage wall time, active matrix throughput, and Tensor Core duty are
separate measures. The new mapper removed 107,928 descriptors per severe or
quiet slot, saving 18,995,328 bytes while the blocking-write counts and tile
miss counts stayed the same. The matched improvement combines mapping,
host-side changes and coarse phase reuse.

### First profiling experiment

1. Start with a single W24 severe `admission` fixture and its exact
   resident-FP4 work receipt. Capture an uninstrumented timing before
   enabling profiling. Build and verify the instrumented probe before a
   paid timing window where practical. The previous scratch profile
   compiled and passed simulated event handling, then ran out of its
   pod work window before obtaining GPU events. The current production
   queue uses flags zero and its enqueues omit event handles.
2. In an experiment-only build of `OpenClNvfp4Projection`, enable
   `CL_QUEUE_PROFILING_ENABLE` on the existing in-order queue. Request tagged
   events for buffer fill, tile uploads, descriptor uploads, direct columns,
   observation upload, each map flush, peak scaling, input pack, the FP4
   product or FFT, sufficient statistics and statistics readback. Record
   bin upload and basis packing as cold setup. Read and release bounded
   event records after the existing blocking checkpoints. Preserve
   cancellation and host-buffer lifetimes. Count expected, timed and
   missing events and retain per-stage counts, bytes and completed mode
   counts. Treat `START` to `END` as a command interval. Track gaps between
   commands on the same device clock and collect `QUEUED` and `SUBMIT`
   where available. Use actual documented hardware counters for Tensor
   pipe activity where accessible under the same runtime restrictions.
   Earlier CUDA hardware-counter requests returned `ERR_NVGPUCTRPERM`.
   The latest OpenCL attempt ended at the work deadline before collection.
   [OpenCL event profiling](https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/clGetEventProfilingInfo.html).
3. Collect monotonic host spans for source callback work, tile fill/cache
   lookup and pinning, descriptor assembly, memory copies, enqueue, blocking
   upload/read calls, result unpack and remaining native tracking. A source
   callback can contain staging and a blocking cache flush. Record exclusive
   spans or an explicit nesting and overlap relation. Keep host and device
   clocks separate unless calibrated and preserve an uninstrumented result
   as the performance control.
4. Split the larger coarse span before proposing a replacement. Measure
   `filter_setup`, `mapped_fidelity`, `template_bank`, `observation`,
   `lag_setup`, `bulk_prepare`, `search` and `selection`. The probe's
   `acquire` and `coarse_json` expose these in diagnostic commands. The
   diagnostic Helper schedule and public coarse geometry can differ from
   `admission` or `message`. Verify coverage before transferring timings,
   or expose a bounded `CoarseProgress` receipt on the matched production
   probe. Keep overlapping bank, observation and Helper spans separate,
   and treat repeated search counters as cumulative. Compare host command
   recording, submit/fence wall, available Vulkan query pairs and FFT or
   shader setup. Investigate the route-dependent timestamp difference in
   the [timing record](validation.md#five-slot-timing-and-measurement-scopes)
   before assigning more of coarse wall time to a particular stage.
   [Vulkan timestamp semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html).

### Choose the next optimization from attribution

1. If source work or blocking submissions dominate fine time, overlap
   bounded preparation for an already-ready extent with current device
   work. Compare grouped staging or reusable asynchronous upload storage.
   Retain queued storage through driver completion, charge its full
   ownership lifetime, and keep direct/eviction paths and cancellation
   covered. Use source-cache misses and host wait measurements to decide
   which overhead to remove.
2. If the FP4 product consumes a material share, compare a more useful
   batch shape or matrix layout against the same resident FFT. Preserve
   tracking dependencies and the complete-owner budget when grouping
   ready work. Report issued padded arithmetic, useful work, packing,
   workspace and complete elapsed time. A larger resident-only control
   establishes what the card can sustain for its own geometry.
3. For coarse-dominated service, test reduced native warp or channelizer
   work and a device-resident FIR/crop on the same coarse prior before
   replacing the FFT. An exploratory Tensor Core coarse formulation can
   share one observation lag matrix by placing the trial-frequency
   rotation on the template. The resulting common arrival phase cancels
   in the two-ray quadratic score. Its additional dense work, preparation
   and noise-ranking effects need a bounded comparison. Earlier FP4 coarse
   work changed a unique noise peak, so include an acquisition and quiet
   holdout before adopting a different arithmetic route.

Use the [abbreviated test policy](robust-current.md#testing-computationally-intensive-robust-changes).
After an attribution look, predeclare the smallest useful matched comparison.
Repeat in reverse order when a timing difference is uncertain. For a routing
or arithmetic change, include the severe and quiet cases, native and
applicable GPU controls, and the complete stable message through two quiet
slots. Preserve the severe wrong-bit receipt as an unresolved reception
case. For broader performance or precision claims, use independent holdouts
and backlog, sensitivity and workspace checks within the same contracts.

## What to measure separately

Bulk template/correlation work, preparation, transfers, RNG/channel generation,
small factorizations and reductions can have very different costs. A fast kernel
is not end-to-end useful receiver throughput. Preserve input, source, shape,
precision, cache and device identities; same-device and cross-device comparisons
answer different questions. Advertising tensor peaks is not measuring this work.

Lower/tensor arithmetic is worth exploring where bulk work dominates. Sensitive
Gram/rank, covariance and likelihood reductions may benefit from higher precision
at little total cost. Do not hide expensive FP64 preparation behind a low-precision
selection or downcast every stage indiscriminately. Ill-conditioned, rank-deficient
and near-admission-boundary cases need explicit fallback and uncertainty.

Caching templates, FFTs, Grams and bases; shared observations; persistent buffers;
FFT proposals with sparse refinement; fused reductions; and suitable matrix
shapes are reusable ideas. Their value depends on preparation reuse and the real
workload. Search pruning must retain honest coverage and cannot count omitted
hypotheses as completed work.

One retired receiver experiment on an RTX 4090 found FP16 replay slower than
strict FP32, while joint FP8 quantization added misses in a 96-capture sample.
These are useful counterexamples, not predictions for the current receiver,
different hardware or different inputs.

## Interpreting future performance and utilization

The retained experiments establish outcomes for their recorded implementations,
inputs, devices and runtimes. They leave the attainable performance and Tensor
Core utilization of future implementations unresolved. A slower prototype can
justify keeping the current route for the tested workload. Any claim that
further improvement is impossible needs a bound whose assumptions cover the
proposed algorithm and implementation changes. The comparisons here supply
no such general bound.

Fusion, alternative factorizations, batching, layouts, precision decompositions
and preparation overlap are research candidates. For a proposed variant, state
which measured cost or technical argument motivates it and what remains to be
tested. Its achievable gain and practical feasibility depend on the resulting
implementation. Historical successes support those specific results and reasons
to investigate. Forecasts of new gains need their own assumptions and evidence.

Report the named utilization metric and its measurement interval alongside
useful work and elapsed time. Tensor Core activity, resident-warp occupancy,
dispatch counts and complete-receiver throughput measure different properties.
Higher occupancy can accompany unchanged or worse performance. Reducing work
or data movement can improve elapsed time while reducing activity. Low
utilization alone leaves both the cause and achievable improvement unresolved.
The useful receiver outcome depends on required work, memory, sensitivity and
completion.
[NVIDIA profiling guidance](https://docs.nvidia.com/nsight-compute/ProfilingGuide/).

A measured bottleneck describes the observed execution. Changes to algorithms,
data movement, precision, reuse or overlap can move it. Arithmetic peaks and
fixed-work speedup bounds remain conditional on their operation counts, bytes,
dependencies and unaffected costs. Reassess those quantities when a proposed
implementation changes them. Bounded experiments can support a practical
choice and a stopping decision while the potential for further improvement
remains open.

## FP4 utilization headroom on RTX PRO 6000 Blackwell

The staged socket-filtered SM120 receiver projects 288 complex DATA templates,
1,500 samples each, onto 47 retained complex modes. It issues a dense real
contraction of `288 × 96 × 3072` after splitting complex values and padding.
Counting a multiply-add as two operations gives **169.87 million issued** and
**162.43 million useful** floating-point operations per full submission. Only
4.4% of issued arithmetic is padding. The same arithmetic executes in 216
128-thread blocks. On 188 SMs, those 864 warps could occupy at most 9.6% of
the 9,024 available resident warp slots at once. Offline SM120a assembly
reported 40 registers per thread, 2 KiB of shared memory per block and zero
spills. These bounds describe launch geometry and resources. Measuring Tensor
pipe activity, instruction issue and cache traffic requires counters.
[Kernel](../src/triad_opencl_nvfp4_kernels.cu) ·
[Blackwell tuning guide](https://docs.nvidia.com/cuda/blackwell-tuning-guide/#occupancy).

Use approximately **1.9–2.0 PFLOP/s dense** as the theoretical reference, with
clock and edition stated. The Server Edition publishes 4 PFLOP/s of FP4 AI
performance. NVIDIA's GB202 Workstation Edition architecture table separates
about 2.02 PFLOP/s dense with FP32 accumulation from 4.03 PFLOP/s with
structured sparsity. The receiver uses arbitrary dense templates. Report
the large resident control separately from the rated peak.
[Server specifications](https://www.nvidia.com/en-us/data-center/rtx-pro-6000-blackwell-server-edition/) ·
[Blackwell architecture table](https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/pdf/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1_1.pdf#page=46).

The [production stable-slot investigation](validation.md#blackwell-nvfp4-fine-projection--19-november-2026)
recorded 809 pinned submissions at four admitted fine workers, 1.502 s of
fine processing and 6.715 s of slot service. Its final component comparison
gave a matrix-event median of 8.752 µs and a pinned-call median of 119.62 µs.
Those are separate measurements. Combining them with the actual work counts
yields the following scope estimates.

| Interval | Issued-equivalent FP4 throughput | Fraction of 2 PFLOP/s |
| --- | ---: | ---: |
| Matrix kernel event on a full 288-lane batch | 19.4 TFLOP/s | 0.97% |
| Pinned upload, packing, projection and readback | 1.42 TFLOP/s | 0.071% |
| Fine service for the measured slot | 91.5 GFLOP/s | 0.0046% |
| All receiver service for the measured slot | 20.5 GFLOP/s | 0.0010% |

Useful throughput is 95.6% of the issued rates for that slot. These are
arithmetic throughput estimates from operation counts and elapsed time. The
higher-allocation default uses native fine processing on the tested geometry,
with Vulkan processing millions of coarse rows. The four-worker wall difference
includes 0.595 s of fine improvement and 0.812 s of variation in coarse processing.
Only the phase measurements distinguish those effects.

### Same-card resident and staged controls

On 19 November 2026, source `ea0368c3d1ae31a0e4535373baf33233677e6f41`
was measured on a full RTX PRO 6000 Blackwell Server Edition with 188 SMs,
driver 595.112.07 and a reported maximum SM clock of 2,430 MHz. The OpenCL
probe compiled the production SM120a source and ran with the worker socket
filter. It used 7 warmups and 24 measured projections at each width, fixed
synthetic complex inputs and retained DFT bins. The resident measurement
reused the packed input, packed basis and output buffers. It timed the same
projection kernel including its reconstruction epilogue. The staged call
includes the blocking pinned upload, pack, projection and readback. Host
template generation ran before timing. Worst relative RMS across the
sampled DFT lanes was 0.1481. Results repeated bitwise and each larger batch
reproduced its 288-lane prefix bitwise.

| Lanes | Workgroups | Resident OpenCL event | Resident OpenCL | Staged OpenCL | Resident cuBLASLt graph |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 288 | 216 | 8.93 µs | 19.0 TFLOP/s | 1.16 TFLOP/s | 11.4 TFLOP/s |
| 576 | 432 | 9.22 µs | 36.9 TFLOP/s | 1.28 TFLOP/s | 22.7 TFLOP/s |
| 1,152 | 864 | 13.30 µs | 51.1 TFLOP/s | 1.52 TFLOP/s | 45.5 TFLOP/s |
| 4,608 | 3,456 | 41.01 µs | 66.3 TFLOP/s | 2.03 TFLOP/s | 177.2 TFLOP/s |

The staged medians at these widths were 146.88, 264.82, 446.93 and
1,337.74 µs. The 288-lane staged matrix event was 8.98 µs. Across a 24-launch
resident queue span, including gaps between launches, throughput ranged from
15.4 TFLOP/s at 288 to 64.7 TFLOP/s at 4,608. The upload events yielded
about 38.0 GB/s at 288 and 57.0 GB/s at 4,608. Widening by 16× gave 3.5×
resident matrix throughput and 1.75× staged throughput. The 4,608-lane
case exercises device capacity. Simultaneously ready receiver work at that
width requires a separate scheduling measurement.

The cuBLASLt control ran in a separate CUDA profiling process on the same
card with resident signed NVFP4 operands, unit block scales, FP32 accumulation
and FP32 output. It used library 12.8.4, selected among supported heuristics,
reported medians of 11 event-timed graph replays, and checked five output
elements against exact integer sums for each shape. Its output shape and
operation count match the padded dimensions in the table. Control timings
cover the matrix product and include the graph launch span. At 288 lanes,
cuBLASLt took 14.93 µs and OpenCL took 8.93 µs including reconstruction.
Widening the other output dimension to `1024 × 1152 × 3072` reached
**458 TFLOP/s**. A large
`8192 × 8192 × 4096` resident product reached **1.309 PFLOP/s**, about 65%
of the rounded dense reference. That is about 69× the production-width
OpenCL matrix rate and establishes an on-card scale reference. Current
CUTLASS SM120 NVFP4 tile sizes also favor wider
work. A 128×128 tile yields just three output tiles at 288×96 before split-K
or grouping. [CUTLASS SM120 tile table](https://docs.nvidia.com/cutlass/latest/media/docs/cpp/blackwell_functionality.html#blackwell-sm120-gemms).

The first benchmark launch stopped before measurement because SSH omitted
the toolkit compiler from its search path. The second used the deployed
build's compiler path and passed both controls. Both temporary pods were
removed and verified absent. Keep the compact shape and timing records under
`local-evidence/fp4-utilization-20261119/` until a newer matched measurement
supersedes them.

The staged path sends 3,458,304 bytes of mapped C64 input and scaling per
full batch, totaling 2.798 GB for the measured slot. The pinned device upload
event of 63.2 µs corresponds to 54.7 GB/s. Nominal PCIe 5.0 ×16 offers about
63 GB/s before protocol overhead, which bounds this input format to roughly
3.1 TFLOP/s of dense FP4 work even with perfect overlap and no other costs.
At the observed input rate the same bound is about 2.69 TFLOP/s. A packed
FP4 input plus scales and peaks would shrink the upload about 6.9×. Host
quantization and a still-limited link would remain part of that design. To
feed 2 PFLOP/s at this batch's arithmetic count through a 63 GB/s link,
each direction could move only about 5.4 kB per batch. The staged 108 kB
projection readback would cap fully overlapped throughput near 99 TFLOP/s
by itself. The [later resident route](#resident-mapping-and-paired-fine-statistics)
reduces the returned data to sufficient statistics.
[PCI-SIG encoded link rate](https://pcisig.com/sites/default/files/files/PCIe_Specification_Webinar_Rev%206_FINAL_0.pdf).

The 809 matrix-event medians add to only 7.08 ms. Even eliminating all of
that estimated time from the staged schedule would save about 0.47% of fine
time and 0.11% of slot time. Coarse processing accounts for 5.210 s or 77.6%
of the slot. Eliminating the entire fine stage would give at most 1.29× at
that measured coarse cost. Different receiver pipelines need their own
complete-service measurements.

The native CUDA implementation already demonstrates the more consequential
dataflow alternative. It leases immutable source tiles, sends approximately
312 bytes of affine and phase descriptors per column in the current layout,
and renders the mapped columns on the device. For 288 columns, the descriptors
alone are under 90 kB per batch, about 38× below the expanded upload before
source-tile misses. That bounded mapping contract became the basis for the
socket-filtered resident backend. On the native CUDA route, direct cuFFT
with retained-bin gather beat the pack-inclusive cuBLASLt FP4 path at the
matched small widths.
[Mapping interface](../src/triad_receiver_bulk.hpp) ·
[Fused device mapping](../src/triad_tensor_backend.cpp) ·
[Matched FFT comparison](validation.md#blackwell-nvfp4-fine-projection--19-november-2026).

Two equal-geometry blocks already available in a fine `Pair` can share a
576-lane launch while preserving chronological state updates. The staged
sizing exercise projected 405 instead of 809 batches. Its hypothetical
paired staged design would have increased projection workspace from
6.70/7.34 MiB to 13.40/14.51 MiB host/device per owner, with 6.92 MB of
expanded templates uploaded per full pair. The isolated projection-call
saving estimated from the component medians was roughly 12 ms for
404 pairs. The later measurement instead uses paired resident work.
Broader aggregation must quote all
immutable inputs, descriptors, owned outputs and retained states. Reuse the
existing receiver lanes for independent per-state correlations and Gram
work before considering lower precision in those statistics. Preserve full
`W=2*H`, active and failed Helper acquisition, every public coordinate and
scored quiet-slot completion.

The [resilience study](../_ref/resilience-triple_Robust_Modem/Astra-6/report.md#native-integration-and-computational-constraints)
supports these validation boundaries. It identifies source, projection,
statistics and state as distinct native stages and emphasizes reuse with
complete-service accounting. The
[Fable review](../_ref/resilience-triple_Robust_Modem/Fable-5.2/robust-nonstationary-channel-research.md#66-not-evaluated-here-and-possibly-better)
identifies GPU whole-grid innovation likelihoods as an open experiment.
Use those channel studies to scope receiver fidelity and public work. The
filtered timing experiment above supplies the FP4 throughput evidence.

## Resident mapping and paired fine statistics

The socket-filtered RunPod worker now has two explicit developer-probe routes
for the same isolated DATA geometry. Both use the canonical native mapping
source and a bounded per-owner device cache of 64 immutable waveform tiles.
Normal columns send a 176-byte affine and phase descriptor. A tile miss sends
128 KiB of source samples and a direct native mapping sends one C64 column.
Mapped samples and projected modes stay on the device. A blocking read returns
176 bytes per block and candidate, with sufficient statistics for both bits
and the nonzero-basis marker. Observation modes and initial bin indices are
included in the transfer receipt. Device buffers, reusable host staging and
the source allocations retained by cache identities are included in admission.

The receiver submits two compatible ready blocks at a time and still applies
each block's updates in order. The 1,500-sample, 47-mode, two-tap geometry
uses up to 576 template lanes in one paired submission. Larger candidate
banks use consecutive submissions and publish completed work after the last
one succeeds. Irregular terminal geometry and projections with the Helper
taper use the native route. The two controls are
`--vulkan-fine-projection resident-nvfp4` for E2M1 Tensor Core projection
with FP32 accumulation, and `resident-fft` for an FP32 mixed-radix FFT and
retained-bin gather. Both form correlation and Gram statistics in FP64.
The resident receipts identify the method, paired and single submissions,
mapped and direct columns, tile reuse, upload and readback bytes, and how
often the host blocked for writes. The counts describe work and transfers,
with no hardware Tensor Core activity fraction inferred from them.

On the same full RTX PRO 6000 Blackwell Server, four software workers
processed the stable 3.6 Hz input in 0.526 s of fine service with resident
FP4, versus 1.483 s with staged FP4. The resident FFT took 0.542 s. Per slot,
the resident methods sent 74.76 MB of fine-stage uploads and read back
10.25 MB, versus 2.798 GB and 87.60 MB for staged FP4. Severe and quiet
inputs sent 110.95 MB. The resident work reduced transfer by retaining
intermediate results and shortened fine service even on the severe and quiet
inputs. All three methods matched the six admission-policy dispositions.
[Matched receiver checks](validation.md#blackwell-resident-fine-processing--19-november-2026).

The 809 block-equivalents contain roughly 137.4 GFLOP of issued FP4
contraction work. Dividing by 0.526 s gives 0.26 TFLOP/s across the entire
resident fine stage, about 0.013% of the rounded 2 PFLOP/s dense reference.
That elapsed-work ratio includes the mixer, template mapping, FFT-independent
statistics, host bookkeeping and waits. The earlier matrix-only event
control is about 36.9 TFLOP/s at 576 lanes. Actual Tensor Core duty for this
complete run requires hardware counters. With 63 GB/s of ideal PCIe 5.0
×16 payload throughput, the stable slot's observed upload bytes alone
imply an optimistic bound near 116 TFLOP/s for this dataflow. The preceding
staged estimate was about 3.1 TFLOP/s. The remaining rate gap includes small
launch geometry, intermediate device work and host synchronization.
Coarse search still took 4.84 s of this 5.37 s slot, so speeding up the fine
matrix alone has little leverage on complete service.

## Shared bit mapping and coarse phase reuse

The native source emits both bit alternatives with the same affine map and
phase for a tap. The resident OpenCL mapper now sends one 176-byte descriptor
for that pair when both tile lists fit and belong to the same cache flush.
One device thread computes the coordinate, cubic weights and phase for both
bits. Each bit keeps its own signal tiles, output and underflow indicator.
The single-column route covers unmatched columns and cache boundaries.
The host reuses its immutable consecutive row offsets and pins each tile
group under a shared cache lookup. Arbitrary caller-provided offsets retain
their validation.

The Vulkan coarse path also reuses exact Doppler rotations across keyed and
clock template alternatives. Each cached phase uses the original extended
precision formula at the original row index. The temporary immutable table
fits within the admitted padded-bank allowance and is released before device
bank upload. Constant-phase columns compute their shared phase once. Both
changes reduce work around the existing GPU projections while preserving
the configured calendar and public search grid.

On a same-device Blackwell comparison, the 24-worker resident severe slot
spent 0.972–1.003 s in fine service, down from 1.439–1.450 s. Its roughly
137.4 GFLOP of issued FP4 products correspond to about 0.14 TFLOP/s across
the fine stage, up from about 0.095 TFLOP/s. This elapsed-work estimate
includes preparation and the rest of the fine pipeline. The matrix shape
and kernel are the same as the earlier 36.9 TFLOP/s matrix-only event
control for a 576-lane projection. The follow-up event-profile build
reached the pod deadline before collecting device events, so actual active
matrix throughput and Tensor Core duty on the updated receiver remain
unmeasured.

[Measured receiver service](validation.md#blackwell-shared-bit-mapping-and-coarse-phase-reuse--19-november-2026)
· [Next profiling experiment](#first-profiling-experiment).

### Reproduce the matched Blackwell runs

Start with the [Blackwell continuation guide](#continue-blackwell-fp4-work)
and the [five-slot timing definitions](validation.md#five-slot-timing-and-measurement-scopes).
The reference is `e76fc90e45f79779c676f0ed3b1643eb590b95dc`. The candidate
is `95b661e9c866d42b1445abdc3f49f3d87acaf827`. Their probe source manifest
SHA-256 values are
`46d3b972166013737aaf9f70f0134a3f5677378b291df148b47010a36dfb824d` and
`d8dd714cf29cd65753f82771cf3e3516425b4b9b89c1159062d35d32bfb52c7a`,
respectively. Both used build-options SHA-256
`6e9e5bcbce13bbc3bae3379606abeccff5d07814c832d214bde544f5aedd8c10`.
The build digest can change with the toolchain. Record it, the source manifest
and the probe binary hash for each new build. The source manifest binds the
native inputs even when the exported tree has no Git metadata.
[Probe identity generation](../cmake/TriadBench.cmake).

Use one budgeted full RTX PRO 6000 Blackwell Server Edition with a verified SSH
host key. The measured device reported 97,887 MiB, compute capability 12.0,
driver 595.112.07 and MIG disabled. Verify the runtime and effective CPU
allocation before timing. Keep the API key and private SSH identity local.
The native probe supplies its own deterministic synthetic fixture key.
Deploy the clean reference with the lab's `--mode gpu --profile simulation`
procedure and confirm `PUMP_LAB_MODE=vulkan` for this comparison. Stock
deployment prepares the signed Ubuntu OpenCL loader, a private NVIDIA EGL
Vulkan manifest and a private NVIDIA OpenCL vendor directory. Treat a
CUDA-selected stock worker as a separate route. Run `nvidia-smi -L` and
`nvidia-smi --query-gpu=name,memory.total,compute_cap,driver_version,mig.mode.current --format=csv,noheader,nounits`
outside the receiver boundary. Retain the technical device and toolkit
information without provider identifiers. The latest matched campaign
retained the build digest and worker counts. Detailed CPU model, effective
affinity and quota, and expanded compiler/toolkit versions are absent from
its compact receipts. Record those fields on the next run alongside the
probe's `allocation` and `identity` objects.
[GPU selection and authorization](triad-performance.md#gpu-selection-for-accelerated-development),
[lab deployment and SSH](../_zLab/README.md),
[stock driver preparation](../_zLab/prepare_vulkan.sh).

From the local checkout, set `ssh_host`, `ssh_port`, `ssh_user`,
`known_hosts` and `ssh_identity` to the independently verified direct SSH
settings. Export both committed sources into disposable directories and
deploy the reference. Use the same verified SSH procedure to transfer
`candidate.tar.gz` and `candidate.sha256` into a fresh private Pod directory.
Verify with `sha256sum -c candidate.sha256` there before extracting. Keep
both probe binaries in separate build trees for the alternating comparisons.

```bash
set -euo pipefail
umask 077
stage=$(mktemp -d)
mkdir "$stage/reference" "$stage/candidate"
snapshot_paths=(CMakeLists.txt CMakePresets.json LICENSE build.sh cmake src include tools tests third_party _zLab)
git archive e76fc90e45f79779c676f0ed3b1643eb590b95dc "${snapshot_paths[@]}" | tar -x -C "$stage/reference"
git archive 95b661e9c866d42b1445abdc3f49f3d87acaf827 "${snapshot_paths[@]}" | tar -x -C "$stage/candidate"
python3 -B "$stage/reference/_zLab/deploy.py" --host "$ssh_host" --port "$ssh_port" \
  --user "$ssh_user" --known-hosts "$known_hosts" --identity "$ssh_identity" \
  --mode gpu --profile simulation
python3 -B "$stage/candidate/_zLab/bundle.py" --output "$stage/candidate.tar.gz"
(cd "$stage" && sha256sum candidate.tar.gz > candidate.sha256)
```

On the Pod, build both native targets in the reference's existing deployment
and recreate the stock Release configuration in the candidate tree, using
the same compiler, toolkit and cache. For example, with the stock
Ninja/Vulkan profile and Ubuntu amd64 loader, set `candidate` to the
absolute candidate source directory and run:

```bash
set -euo pipefail
umask 077
lab=/workspace/pump-lab
nvcc=$(sed -n 's/^DATAPUMP_TRIAD_OPENCL_NVCC:FILEPATH=//p' "$lab/build/lab/CMakeCache.txt")
test -x "$nvcc"
"$nvcc" --version
cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$lab/build/lab/CMakeCache.txt")
test -x "$cxx"
jobs=$(python3 "$lab/tools/build_capacity.py" --runpod vulkan)
cmake --build "$lab/build/lab" --parallel "$jobs" \
  --target triad_receiver_probe test_triad_opencl_nvfp4
cmake -S "$candidate" -B "$candidate/build/lab" -G Ninja \
  "-DCMAKE_CXX_COMPILER=$cxx" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DDATAPUMP_BUILD_GUI=OFF \
  -DDATAPUMP_BUILD_CLI=ON -DDATAPUMP_BUILD_WEB_WORKER=ON -DDATAPUMP_PORTABLE=OFF \
  -DDATAPUMP_COMPILER_CACHE=ON -DDATAPUMP_TRIAD_TENSOR=OFF \
  -DDATAPUMP_TRIAD_VULKAN=ON -DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON \
  -DDATAPUMP_TRIAD_OPENCL_NVFP4=ON "-DDATAPUMP_TRIAD_OPENCL_NVCC:FILEPATH=$nvcc" \
  -DDATAPUMP_TRIAD_OPENCL_LOADER_PATH=/usr/lib/x86_64-linux-gnu/libOpenCL.so.1
cmake --build "$candidate/build/lab" --parallel "$jobs" \
  --target triad_receiver_probe test_triad_opencl_nvfp4
sha256sum "$lab/build/lab/triad_receiver_probe" "$candidate/build/lab/triad_receiver_probe"
```

Use the stock driver's validated environment and inherited no-socket filter
for both binaries. The probe is a developer executable. This temporary
launcher applies the same descriptor and filter functions as the production
worker before executing the probe, with anonymous pipes supplied by Python.
Python only launches the compiled test. The `AF_UNIX` attempt must fail with
`EPERM`, and the marker must reach the captured stderr. Check the OpenCL
component and initial Vulkan probe separately. A plain SSH shell can select
the default GLX-based ICD and fail Vulkan initialization. Keep the private
manifests and socket filter active in diagnosis.
[Worker boundary](../src/host/worker.cpp),
[stock relay environment](../_zLab/worker_relay.py).

```bash
scratch=$(mktemp -d)
cat > "$scratch/filter.cpp" <<'CPP'
#include "datapump/host/sandbox.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) return 125;
    datapump::host::restrict_descriptors(std::array{0, 1, 2});
    datapump::host::verify_anonymous_pipe(0, true);
    datapump::host::verify_anonymous_pipe(1, false);
    datapump::host::install_no_socket_boundary();
    if (!datapump::host::no_socket_boundary_active() ||
        socket(AF_UNIX, SOCK_STREAM, 0) != -1 || errno != EPERM) return 125;
    std::fputs("DATAPUMP_NO_SOCKET_VERIFIED\n", stderr);
    execv(argv[1], argv + 1);
    return 125;
}
CPP
c++ -O2 -std=c++20 -I"$lab/include" "$scratch/filter.cpp" \
  "$lab/src/host/sandbox.cpp" -o "$scratch/filter"

filtered() {
  python3 - "$lab" "$scratch/filter" "$@" <<'PY'
import subprocess
import sys

lab, launcher, *command = sys.argv[1:]
sys.path.insert(0, lab + "/_zLab")
from worker_relay import DEPLOYMENTS, gpu_environment

environment = gpu_environment(DEPLOYMENTS[False], False)
if not environment or not environment.get("OPENCL_VENDOR_PATH"):
    raise SystemExit("The private Vulkan and OpenCL manifests are required")
result = subprocess.run([launcher, *command], env=environment, input=b"",
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
sys.stdout.buffer.write(result.stdout)
sys.stderr.buffer.write(result.stderr)
if b"DATAPUMP_NO_SOCKET_VERIFIED" not in result.stderr:
    raise SystemExit("Missing no-socket boundary receipt")
raise SystemExit(result.returncode)
PY
}

ref="$lab/build/lab/triad_receiver_probe"
new="$candidate/build/lab/triad_receiver_probe"
filtered "$lab/build/lab/test_triad_opencl_nvfp4"
filtered "$candidate/build/lab/test_triad_opencl_nvfp4"
filtered "$new" describe --backend vulkan --require-accelerator on
```

Both component executions must report verified paired and single fine
statistics for NVFP4 and FFT. The separate probe must initialize Vulkan
under the filter. Record the route and executed work again from each
measured fixture.

Run the same seed and public search on both source versions. Severe uses
the full selected channel effects. Quiet uses noise alone with the same
declared search. Stable sets realized severity to zero and preserves the
declared search. `message` generates the native alarm fixture with raw
observations `001` followed by two quiet slots. The explicit `--bit 0` is
the wanted bit for `admission`. Four software workers qualify the existing
automatic fine route and 24 reproduce the wider production-path comparison.
Keep the normally available `W=2*H` receiver allocation for capacity
evaluation.

```bash
fixture=(
  --case hf --encrypt on --data-power-w 0.001995262314968879
  --path-loss-db 90 --noise-dbm-hz -164 --timing-policy broad
  --base-oscillator gpsdo-xo --shift-oscillator gpsdo-xo
  --spread-preset hf-worst --selective-perturbation each-65 --margin 3
  --fhss off --burst-wait-tiles 0 --rate 3.6 --rf-hz 30000000
  --added-symbols 1 --added-interleave off --target-cn0 32 --seed 950101
  --helper off --bit 0 --require-accelerator on
)
slot() {
  local binary=$1 workers=$2 fine=$3 deadline=$4
  shift 4
  filtered "$binary" admission "${fixture[@]}" --backend vulkan \
    --vulkan-fine-projection "$fine" --workers "$workers" \
    --max-samples 1215321 --deadline-ms "$deadline" "$@"
}
stream() {
  filtered "$1" message "${fixture[@]}" --backend gpu --workers 4 \
    --channel-severity 0 --slots 5 --max-samples 6255321 \
    --max-spool-bytes 106340457 --deadline-ms 85000
}

slot "$ref" 24 resident-nvfp4 45000 > "$scratch/ref-severe.json" 2> "$scratch/ref-severe.stderr"
slot "$new" 24 resident-nvfp4 45000 > "$scratch/new-severe.json" 2> "$scratch/new-severe.stderr"
slot "$new" 24 resident-nvfp4 45000 --stratum noise > "$scratch/new-quiet.json" 2> "$scratch/new-quiet.stderr"
slot "$ref" 24 resident-nvfp4 45000 --stratum noise > "$scratch/ref-quiet.json" 2> "$scratch/ref-quiet.stderr"
slot "$ref" 4 resident-nvfp4 45000 --channel-severity 0 > "$scratch/ref-stable.json" 2> "$scratch/ref-stable.stderr"
slot "$new" 4 resident-nvfp4 45000 --channel-severity 0 > "$scratch/new-stable.json" 2> "$scratch/new-stable.stderr"
slot "$new" 24 resident-nvfp4 45000 > "$scratch/new-severe-2.json" 2> "$scratch/new-severe-2.stderr"
slot "$ref" 24 resident-nvfp4 45000 > "$scratch/ref-severe-2.json" 2> "$scratch/ref-severe-2.stderr"
slot "$ref" 24 resident-nvfp4 45000 --stratum noise > "$scratch/ref-quiet-2.json" 2> "$scratch/ref-quiet-2.stderr"
slot "$new" 24 resident-nvfp4 45000 --stratum noise > "$scratch/new-quiet-2.json" 2> "$scratch/new-quiet-2.stderr"
stream "$ref" > "$scratch/ref-message.json" 2> "$scratch/ref-message.stderr"
stream "$new" > "$scratch/new-message.json" 2> "$scratch/new-message.stderr"
slot "$new" 24 native 32000 > "$scratch/new-native-severe.json" 2> "$scratch/new-native-severe.stderr"
slot "$new" 24 native 32000 --stratum noise > "$scratch/new-native-quiet.json" 2> "$scratch/new-native-quiet.stderr"
```

For an additional arithmetic contrast use
`slot "$new" 24 resident-fft 45000 --stratum noise`, compared against the
candidate resident FP4 quiet case. Keep the simulator input and each worker
allocation fixed through the comparison. Verify the executed backend for the
automatic stream.

| Fixture | Generated samples | Input SHA-256 |
| --- | ---: | --- |
| Severe | 1,215,321 | `b4d8fa5a6f4e8639871b11632abd6b80e9d101f3e73f3c21ffec050d44b08bf8` |
| Quiet | 1,215,321 | `2b9ca9df77bfc0bf6e42548790fd9aeffebd464609ac5db6e78ef79adc7a28c8` |
| Stable | 1,215,321 | `eaa4ae2af712def71715c59da4cbca75e881229de1e9b7f6b28be703063b7625` |
| Five-slot message | 6,255,321 | `4a3603c8698c8a1810b9e47c3e53269bebced25259668816bc61430501cc0b69` |

Require exit zero, `status=complete`, `observation.input_sha256`,
`observation.samples` and matching public work. Treat a changed input hash on
another platform as a separate stratum until reconciled. Require
`result.complete_public_slot` for `admission`. In `identity.backend_receipt`,
the matched explicit resident FP4 slots credited 3,951,090 coarse rows,
58,320 fine statistics, 10,967,040 projected values and 10,950,624 resident
FP4 modes. They used 405 GPU batches, comprising 404 paired batches and
one single. One additional terminal block ran natively.
The complete stream with the separate EOF control credited 23,706,540 coarse
rows, 365,526 fine statistics, 68,736,528 projected values and 68,635,980
resident FP4 modes. The 2,905 GPU batches comprised 2,898 pairs and seven
single batches. Six additional terminal blocks ran natively. Require
`complete_receiver_service`, `message_and_physical_quiet_verified` and
`eof_control_verified` on the message, with an expected-source match and
two trailing scored quiet slots.
The EOF-only control remains incomplete. Single-slot decisions must match
across all six admission policies. Current and Guarded recover stable bit 0.
Current admits the known severe wrong bit 1 and the five stronger policies
erase it. Every policy reports quiet for the quiet input.
Compare numerical evidence and quiet energy within the matched FP4 revision
pairs. The native controls retain the admitted coarse rows and decisions
with zero resident modes and a native fine session, while the resident FFT
control must credit FFT modes. The two different arithmetic routes can
produce different numerical evidence. Use the [native receipt
validator](../tools/triad_native.py) and the [recorded timing
scopes](validation.md#five-slot-timing-and-measurement-scopes).

Preserve the bounded workload and receipt gates in future comparisons.
Collect compact technical results, then remove temporary captures, launchers
and source snapshots when their purpose ends. Terminate only the owned
benchmark pod and verify removal through the provider inventory.

## Which arithmetic ran in a remote simulation?

The RunPod build uses the same compiled receiver as the desktop GUI.
`--backend gpu` asks the [shared GPU selector](../src/triad_gpu_backend.cpp)
for automatic dispatch. Its choice of CUDA or Vulkan identifies the device.
The following fields in a completed native probe's
`identity.backend_receipt` identify the instrumented work that device performed.
The table describes current routes and counter coverage. Tensor Core
compatibility of further formulations remains a separate engineering question.
An existing CPU or cuFFT route establishes where this implementation ran.
An unimplemented route or a slower tested candidate leaves other formulations
open. Scope any incompatibility claim to a demonstrated constraint on the
device, data type, library, formulation or required numerical behavior.
Keep the untested alternatives explicit.

The HMMA/IMMA counters below cover the explicit coarse lag-matrix calls.
Other library operations, including fine-stage Gram products, have separate
instrumentation. Zero counts establish zero credited calls through those
lag-matrix paths. Tensor Core use inside other library kernels requires
operation-specific dispatch or instruction evidence. The current receipts
leave that internal use unmeasured.

| Receiver stage | Implementation and evidence to retain |
| --- | --- |
| Filtered DATA lag/frequency matrix correlation | [TensorCoarse](../src/triad_tensor_backend.cpp) groups the observations and templates. The [CUDA matrix kernel](../src/triad_tensor_matrix.cu) packs them and checks the cuBLASLt algorithm's numerical implementation flags. FP16 products accumulate into FP32 (HMMA), or INT8 into INT32 (IMMA), before FP64 reconstruction. Inspect `cuda_matrix_lt_hmma_calls`, `cuda_matrix_lt_imma_calls`, `cuda_matrix_unwitnessed_calls` and `matrix_fft_fallback_counts`. Only the first two count calls through a matching Tensor Core algorithm. |
| Affine frequency search | The guarded CUDA route uses [half cuFFT](../src/triad_tensor_fft.cu) for eligible transforms or the applicable ordinary FFT route. Read `cuda_half_fft_inverse_ffts` and `half_fft_fallback_counts`. A half-FFT count measures half transforms. |
| Broad changing-channel timing search | `TensorCoarse::plan_segmented` chooses shapes for FP32 cuFFT. The [CUDA scoring kernels](../src/triad_tensor_peaks.cu) track candidate motion and rank lags on device. Check `cuda_segmented_sessions`, `cuda_segmented_inverse_ffts`, `cuda_segmented_score_rows` and `cuda_segmented_native_fallbacks`. |
| Fine tracking and decisions | The CUDA `TensorFine` class accelerates eligible bulk basis mapping, Gram products and statistics. Check `mapped_values`, `statistics_blocks`, `cuda_direct_gram_values`, `cuda_bmm_gram_values` and `native_fine_sessions`. The native receiver owns chronological state, bit and quiet decisions, codec and completion. For simulator generation time, read the separate `simulator` phase. [Receiver interfaces](robust-current.md#native-interfaces-and-ownership). |
| Filtered Blackwell fine projection | The [OpenCL route](../src/triad_opencl_nvfp4_projection.cpp) renders mapped tiles, projects DATA templates and returns sufficient statistics. `opencl_resident_nvfp4_modes` witnesses the Tensor Core projection and `opencl_resident_fft_modes` witnesses the alternate FFT. `opencl_resident_fine_pairs`, `opencl_resident_upload_bytes` and `opencl_resident_download_bytes` describe paired submissions and resident transfers. [Receiver timings and limits](validation.md#blackwell-resident-fine-processing--19-november-2026). |
| Vulkan on integrated Radeon | The [Vulkan backend](../src/triad_vulkan_backend.cpp) runs supported frequency search via VkFFT or direct products. `vulkan_dispatches` and the per-route score-row counters identify actual work. The qualified Vega FP16 direct route uses packed vector arithmetic. VkFFT half storage still computes in FP32. Native CPU retains Helper, lag groups and fine processing. [Vulkan stage scope](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics). |

For example, the 2 mW, 72 Hz/5× severe Blackwell control (manifest
`4ec7a895…`) recorded 8,075,106 segmented FP32 inverse FFTs and 1,760
affine half inverse FFTs. It erased the bit in 34.40 s of receiver work.
The 100 mW, 36 Hz/10× severe GUI run at Target −21, seed 940601 (manifest
`82bdd9f6…`), decoded bit 1 after 1,092,999,648 segmented inverse FFTs.
Its affine half route declined the shape. Simulation took 288.22 s and
receiver scoring took 2,052.09 s. Both receipts had zero credited HMMA and IMMA
calls through the instrumented lag-matrix paths. A utilization graph alone
cannot identify any of these paths.
[2 mW automatic comparison](validation.md#blackwell-receiver-trials-at-2-mw)
and [100 mW results and holdouts](validation.md#bounded-2-mw-hf-fhss-investigation--18-november-2026).

A separate Blackwell matrix case with 2,711 samples, 213 frequencies, 71
arrivals and 78 workspaces ran the FP16 HMMA and INT8 IMMA kernel families.
Tracing the INT8 call identified the loaded kernel, which contained 32 static
IMMA instructions. That count identifies the implementation. Dynamic issue
rate and Tensor Core occupancy were unmeasured.
[Matrix geometry, phase costs and instruction evidence](validation.md#current-cuda-precision-and-tensor-dispatch).

On the provisioned host, run `build/triad-cuda/triad_receiver_probe describe
--case hf --backend gpu` from the repository root to check the build, model
and selected device. This command only initializes the backend. To exercise
production matrix operations, use the [three-pair component command](triad-performance.md#bounded-native-component-comparisons).
For a simulated bit, use the [HF admission recipe](validation.md#reproducing-the-stable-alarm-and-severe-bit-tests)
and retain the Current-policy decision, input fingerprint, device receipt,
workspace limits and phase times together. This command reads the device
counters from a downloaded receipt.

```sh
jq '.identity.backend_receipt | {device_name, executed, cuda_segmented_inverse_ffts,
  cuda_half_fft_inverse_ffts, cuda_matrix_lt_hmma_calls, cuda_matrix_lt_imma_calls,
  cuda_matrix_unwitnessed_calls, cuda_segmented_native_fallbacks, native_fine_sessions}' result.json
```

For a new Tensor Core candidate, first verify the executed arithmetic on a
representative full batch and its tail, including packing and output
reordering. Then compare matched complete receiver slots on the same host.
The [measured transform comparison](#hardware-and-library-dispatch) includes
packing and rearrangement costs. Fusion, alternative factorizations, batching,
precision decompositions and device-specific kernels remain candidates.
Fine tracking also contains bulk basis, Gram and statistics work that can be
investigated while preserving ordered state dependencies. Assess each proposed
formulation's numerical behavior, memory and complete receiver cost. Bit
sensitivity and single-stream message backlog each require their own measurements.

## Hardware and library dispatch

Choose paths from queried hardware capabilities, library support, admitted
geometry and measured end-to-end work. Product names alone do not establish the
available instructions: record the physical GPU, compute capability, runtime
and library versions, actual selected implementation, executed workload, and
fallbacks. For matrix operations, cuBLASLt exposes numerical-implementation
flags that can constrain heuristic results; a returned HMMA/IMMA-compatible
algorithm is dispatch evidence. A kernel trace can show that the named kernel
ran, while an authorized instruction/counter profile is needed to support
stronger claims about generated Tensor Core instructions or utilization. Keep
library selection, kernel names, SASS inspection, hardware counters and
complete-receiver timing as distinct evidence.

| Device family | Relevant implementation boundary |
| --- | --- |
| GeForce RTX 4090 and RTX 4060 Ti 16 GB | Ada, compute capability 8.9. The existing native FP16/INT8 lag-matrix path is eligible for Tensor Core library dispatch when geometry, workspace and implementation flags permit. Eligibility is not a claim that every call used Tensor Cores or that matrix work beats FFT search. Laptop RTX 4090 allocation and power configuration require separate measurement. |
| GeForce RTX 5090 and full RTX PRO 6000 Blackwell | Blackwell, compute capability 12.0 for these devices. Confirm the exact full device and compiled architecture at runtime; do not transfer an SM100-specific path or an earlier GPU's shape results to SM120 by product name. Recheck library heuristic choices and actual receiver costs on both consumer and workstation devices. |
| Ryzen 5 PRO 5650U Radeon Vega iGPU | The locally observed Vulkan device is gfx90c. Its supported packed FP16 vector route is not matrix Tensor Core hardware. Half-storage in an FP32 VkFFT path is a workspace reduction, not packed-FP16 computation. Qualify actual arithmetic and memory use through the native Vulkan route. |
| Ryzen 7 8840U Radeon 780M | Identify the actual GPU architecture/runtime. A published rocWMMA support list that omits gfx1103 does not prove the hardware cannot run matrix operations, nor does a compatible build prove that it did. Require a working native library/kernel route and device evidence before reporting or dispatching one. |
| Intel i9-9900K UHD 630 | This Coffee Lake iGPU predates Intel XMX hardware. Do not assign an XMX path based on a generic Intel GPU label. The project has no actual UHD 630 test host, so its native GPU path remains unmeasured. |

cuFFT's half mode is a real half-precision FFT implementation, with documented
power-of-two sizing and other transform restrictions; the API does not promise
Tensor Core execution. Scaling and conversion costs, FFT intermediate range,
batch occupancy, exact plan workspace, and complete search cost still require
measurement. This differs from a Tensor Core FFT algorithm that factorizes a
transform into smaller matrix products and permutation/twiddle stages. The
published tcFFT study reports such an approach on V100/A100, but its reference
implementation targets those older GPUs and is GPL-3.0; it is evidence that a
factorized design can be useful (reported speedups were 1.29–3.24× on V100 and
1.10–3.03× on A100), not a qualified Ada/Blackwell implementation or code to
copy without resolving project licensing. Report half-cuFFT execution from
its recorded arithmetic. A claim about Tensor Core execution within that
library needs evidence for the particular kernel and runtime.

For segmented timing on the full RTX PRO 6000 Blackwell Server Edition, a
three-stage factored complex inverse used cuBLASLt FP16 HMMA with FP32
accumulation. The 16,384-point batches of 128 and 33 took 73.70 and 30.69 µs.
The 32,768-point batches of 128 and 95 took 138.21 and 102.37 µs. FP32 cuFFT
took 18.40, 16.38, 38.88 and 30.69 µs respectively, with production
normalization folded into lag scoring. The factored timing included packing,
twiddles and reordering. All 384 strongest arrival lags matched, including
the noise-only rows. This demonstrates a working Tensor Core formulation of
these inverses. FP32 cuFFT was faster for the tested implementations and
batches on this SM120 device. The measurements leave the performance of other
factorizations, fused layouts and batch shapes open. A follow-up should include
the actual full and tail batches and all preparation costs. Moving the complete
segmented search onto the same GPU gave a 2.38×
and 3.74× full-slot improvement in two paired severe-channel receiver trials.
[Operator measurements](../local-evidence/hf-2mw-gpu-20261118/canonical/tcfft.jsonl)
and [matched receiver trials](validation.md#blackwell-receiver-trials-at-2-mw).

The authors' RTX 5090 batched-SGEMM study is another useful dispatch warning:
in its tested matrix shapes and software stack, cuBLAS selected a small SIMT
kernel that left substantial FMA capacity unused, while a tailored schedule
improved those batches. That finding motivates checking actual shapes and
library dispatch before declaring a device limit; it does not establish that a
custom kernel improves this modem, whose packing, arrival coverage, FFT
fallbacks and receiver work must be measured as a whole.

Current C++ qualification has separate evidence for these paths: same-run
CUPTI capture tied the executed half-cuFFT calls to loaded `__half` symbols, and
SM120 cubin inspection found packed half arithmetic opcodes in those functions.
This proves the selected implementation contains genuine half arithmetic; it
does not quantify dynamic instruction issue or Tensor Core utilization. The
matrix path separately traces cuBLASLt HMMA/IMMA-compatible kernel families.
The current automatic half-cuFFT scope extends through NFFT=2^20 under a
work-volume, arrival, conditioning, memory and fail-closed dispatch gate; it is
not limited to the earlier NFFT=131072 fixture. Current costs, automatic
guards, fallback rules and measurement limits are in
[the validation record](validation.md#current-cuda-precision-and-tensor-dispatch).

Sources: [NVIDIA compute capabilities](https://developer.nvidia.com/cuda-gpus),
[CUDA programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html),
[cuBLASLt implementation flags](https://docs.nvidia.com/cuda/archive/12.8.1/cublas/index.html#cublasltnumericalimplflags-t),
[cuFFT half-precision limitations](https://docs.nvidia.com/cuda/archive/12.8.1/cufft/index.html#half-precision-cufft-transforms),
[tcFFT paper](https://arxiv.org/abs/2104.11471) and
[implementation](https://github.com/rox906/tcFFT),
[RTX 5090 SGEMM dispatch study](https://labs.cloudrift.ai/research/beating-cublas-on-rtx-5090),
[AMD ROCm GPU specifications](https://rocm.docs.amd.com/en/latest/reference/gpu-specs.html),
[rocWMMA support](https://rocm.docs.amd.com/projects/rocWMMA/en/latest/install/installation.html),
[Intel Core i9-9900K specifications](https://www.intel.com/content/www/us/en/products/sku/186605/intel-core-i99900k-processor-16m-cache-up-to-5-00-ghz/specifications.html),
and [Intel legacy GPU support](https://dgpu-docs.intel.com/overview/supported-hardware/legacy-gpus.html).

## Verified Git history inventory

The 10 November 2026 audit searched the 757 locally reachable commits
(10 September–10 November), including deleted paths and available branches/tags,
then inspected the relevant implementation lineages and selected original
receipts. Audit head: `89945e838bbbc7fbc259000cee9e401e7c804c63`. This is a
focused inventory of measured or promising wins, not a claim to have tested
every historical revision. No old receiver was executed or restored.

The October 31–November 1, 2026 studies used retired isolated v3 receivers,
including the earlier cyclic-amplitude/QPSK source. The optimization baseline
was `75f3af291100d513a8bc20bff9f26d259b51ed71`; the tensor and FP4 follow-ups
started from `ebfe649c1f9c0a5bace48594c982111f64c28f41` with recorded local
changes. These are dated algorithm findings, not current-source qualification.

| Historical implementation and evidence | Useful result and boundary |
| --- | --- |
| **NVFP4 correlation, resident geometry and fused reconstruction**, stored in [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-fp4/README.md#L21) | Full PRO 6000 actual correlation batch: strict FP32 **24.039896 → 17.852368 ms, 1.347×**. Same input hashes and 68,640 hypotheses/capture; includes packing, reconstruction, Grams and assembly, excludes phase inference/admission. The same large batch on RTX 5090 favored FP32, **29.16 versus 38.95 ms**. A separate RTX 5090 synthetic 8192×8192×4096 GEMM improved **9.475× including both packs**; that is not modem throughput. Small complete RTX 5090 receiver also favored FP32, 13.21 versus 14.01 ms. |
| **Fused split-BF16 / TF32x3 phase-state contractions**, [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-tensor/README.md#L34) | RTX 5090 resident inference **8.41737 → 8.12849 / 8.35953 s**. With setup amortized over 32 batches, seven-SNR-point complete subprocess medians **39.25711 → 37.74718 / 38.76491 s**, **1.040× / 1.013×**. Numerical screens and sampled rankings/gates passed. Fixed bank; physical generation, blind acquisition and operational sensitivity tails were not qualified. These sequential workload medians are not timing confidence intervals. |
| **Fused FP16 packing and tiled products**, [`933cdfe9`](https://github.com/mirage335-colossus/pumpModem/blob/933cdfe9/docs/validation-data/triad-v3/2026-11-01-consumer-gpu/README.md#L68) | RTX 4090 synthetic 2048×1024×4096: FP32 **0.423776 → 0.284672 ms**, **1.49× including both packs**; cached FP16 product 0.175104 ms. Complete receiver replay favored FP32, **63.149 versus 61.708 captures/s**. Packing/layout is a useful technique; a universal precision win is not established. |
| **K32 tensor partials with explicit FP32 outer additions**, [`933cdfe9`](https://github.com/mirage335-colossus/pumpModem/blob/933cdfe9/docs/validation-data/triad-v3/2026-11-01-consumer-gpu/README.md#L52) | Represented FP8 arithmetic error improved **21.85× / 26.62×** for E4M3/E5M2 at approximately unchanged 0.15 ms kernel time. FP64 outer accumulation gave the same sampled answers about **6.3× slower**. This controls product accumulation, not input quantization or weak-signal sensitivity. |
| **Scaled FP16 lag-domain DFT**, recorded in [`247b7202`](https://github.com/mirage335-colossus/pumpModem/blob/247b7202/docs/validation.md#L352) | PRO 6000 isolated transforms improved **1.88× / 1.33×**, including scaling/packing/setup/rescaling; paired receiver precision/work gates passed. Acquisition increased **3.543 → 4.103 s** and common stages were effectively unchanged. A transform win, not an integrated win. Experimental source was removed before this documentation commit; retained source hashes are not recoverable code. |
| **Fused phase stencil, persistent CUDA graphs/buffers and register reducer**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L8) | Matched RTX 5090 B4, 192 observations: processing **47.4876 → 11.7625 s, 4.04×**; whole subprocess **50.4042 → 14.9011 s, 3.38×**. Rankings/bits/admission matched; maximum log-LR difference 5.66e-6. B12 produced **4.62× / 3.82×**, but changed batch-shaped noise ordering: equal counts, not matched-input parity. This stencil used ordinary FP32/FP64, not Tensor Cores; acquisition and physical generation were excluded. |
| **Persistent whole-scan graph workspace**, [`25b320a2`](https://github.com/mirage335-colossus/pumpModem/blob/25b320a2/docs/validation-data/triad-v3/2026-11-01-fp4/README.md#L55), `tools/triad_scan_workspace.py` | Graph-captured correlation plus inference over all 85 blocks: RTX 5090 FP32 **37.00→13.21 ms, 2.80×**; NVFP4 **56.08→14.01 ms, 4.00×**; PRO 6000 NVFP4 **20.50→16.12 ms, 1.27×**, each versus its eager counterpart. Includes fresh-input copy, reset, scoring, D2H scores and CPU gates; excludes cold preparation. A/zero-B/A and random A/B/A reset checks passed. Fixed clock/key/geometry; no moving-clock or arbitrary workspace-reuse qualification. |
| **Mixed precision and FP64 reduction organization**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/README.md#L110) | RTX 5090 full-hour graph inference **0.704 → 0.202 s, 3.49×** with FP32 bulk/FP64 evidence rather than all-FP64 bulk. Separately, two-level FP64 reduction avoiding a full converted temporary improved **0.787498 → 0.683743 s, 1.152×** with score error ≤4.263e-14. Neither covers complete acquisition or sensitivity equivalence. |
| **Prepared Gaussian design and simulator caches**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/README.md#L85) | Synthetic B32 receiver cache: **3.78× / 4.06× resident**, **1.64× / 1.73× including design/transfer/verification**, FP32/FP64 respectively. Separate simulator preparation **0.32935 → 0.07931 s, 4.15×**, with 4,645 bit-identical blocks; five-use amortization 3.21×. Simulator truth never becomes receiver input. Warm CPU single-use receiver cache was slower (0.954×). |
| **Cached direct-GEMM acquisition bank**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L158) | Complete small 288-hypothesis bank ×128 observations: PRO 6000 kernel **4.246 → 0.868 ms, 4.89×** versus FFT; RTX 5090 **5.30×**. Full-family winners/gates agreed; preparation/verification dominated the complete pipeline. Shape-specific alternative, not broad-search or synchronization qualification. |
| **Independent trial sharding across two GPUs**, [`ebfe649c`](https://github.com/mirage335-colossus/pumpModem/blob/ebfe649c/docs/validation-data/triad-v3/2026-11-01-optimization/hardware.md#L137) | Two PRO 6000s achieved **1.960–1.981× complete-job speedup** with bit-identical saved scores. Useful for accelerating independent qualification trials, not evidence of parallelizing one received stream. |

The FP4 and phase-contraction results have more than dtype labels as mechanism
evidence. The inspected FP4 source calls `tl.dot_scaled`, checks the compiled
dense FP4 MMA path and rejects fallback; archived SASS contains
`OMMA.SF.16864.F32.E2M1.E2M1.UE4M3.4X`. Fused phase-contraction SASS contains
`HMMA.16816.F32.BF16` and `HMMA.1688.F32.TF32`. Source hashes match the profiled
files and numerical output receipts. Here “native FP4” means compiled GPU
instructions launched through Python/Triton, not the current C++ backend.
The [Triton operation](https://triton-lang.org/main/python-api/generated/triton.language.dot_scaled.html)
can emulate unsupported formats, so actual instruction/fallback evidence matters;
see the [NVIDIA MMA reference](https://docs.nvidia.com/cuda/parallel-thread-execution/#warp-level-matrix-instructions-mma).

Stored commits and measured source identities are distinct. FP4 reports name
`ebfe649c + source-hashed-study`; the three measured implementation hashes match
the files stored in `25b320a2`. The final phase-contraction source hash also
matches `25b320a2`. The graph source matches `ebfe649c`; its measured FP64 reducer
uses the archived `08c3446f…` source snapshot, with later dtype/accounting guards
in the committed file. Source, raw receipts and selected assembly were inspected
directly from Git objects without restoring campaigns.

## Native disposition of the historical techniques

All eleven GPU groups above have a current disposition below. Together with
the [ten CPU groups](triad-performance.md#earlier-cpu-implementations-worth-reconsidering),
the audit accounts for 21 historical technique groups. “Implemented”
describes code in the canonical native stack, not qualification by the old
measurement. Current scoped results and unresolved gates are in
[validation](validation.md#native-matrix-and-preparation-follow-through). The old finite phase-state model
and cyclic-amplitude/QPSK waveform differ from the current receiver, so retain
the transferable technique rather than restoring another receiver.

The table retains original numerical thresholds, score errors and changed peak
findings as historical observations. A score or peak difference alone does not
quantify operational sensitivity loss or establish that a different low-precision
method is inadmissible. Current evaluation must retain every public hypothesis
and measure physical outcomes, false alarms, uncertainty, complete cost and the
user's sensitivity-versus-computation tradeoff.

| Historical group | Current native disposition |
| --- | --- |
| NVFP4 correlation and fused reconstruction | The historical Triton result remains a proven scoped win. A new native cuBLASLt analogue was removed: single-product FP4 failed the 1% score gate (5.03% error), and four-product/residual-scale repairs changed a unique noise peak. Active bulk costs improved against FFT but did not establish an advantage over qualified INT8. This does not reject other FP4 designs or transfer the historical result to current C++. |
| Split-BF16 / TF32x3 phase contractions | These arithmetic modes remain absent. The old phase-state contraction does not map directly onto current Gaussian tracking. The high/residual principle was tested in the removed FP4 analogue, using actual decoded residuals, HH/HL/LH/LL products and FP64 reconstruction. Its noise-peak failure leaves the historical split-BF16/TF32x3 findings intact. |
| Fused FP16 packing and tiled products | Native scaled packing and FP32-accumulating products implemented; include all packing, setup and reconstruction when qualifying the path. The historical complete-receiver loss remains relevant. |
| K32 partials and explicit outer accumulation | Bounded-partial principle implemented with complex chunks of at most 512 samples, INT32 sums for INT8 and FP64 outer reconstruction. This is different arithmetic from the historical FP8 K32 result and needs its own numerical gate. |
| Scaled FP16 lag-domain DFT | Shape-qualified native coarse lag-group analogue implemented after DATA channelization, with shared coefficients and full FFT fallback. Historical transform wins did not establish an integrated win. |
| Fused phase stencil, graphs and register reducer | Current fused mapping, direct small Gram reduction and coalesced readback preserve fusion/reduction principles. Exact CUDA arrival-peak frontiers pass device fixtures and the full 8,596-frequency active/noise public-slot checks; a bounded bulk test measures the transfer/selection improvement. Sustained backlog and CPU forecasting remain separate. The old stencil belongs to a different model; CUDA graph replay is still absent. |
| Persistent whole-scan graph workspace | Exact descriptor and source-tile caches preserve part of the residency lesson; they are not graph replay. Bounded post-render projection/statistics capture remains an unimplemented candidate. |
| Mixed precision and FP64 reduction organization | FP32 bulk with FP64 coordinates, statistics and posterior evidence is retained. The historical two-level reducer is not established as the same current implementation, so its ratio does not transfer. |
| Prepared Gaussian design and simulator caches | Persistent native mapped bases, projection plans and device tiles are retained. Exact projection/offset descriptor reuse and shared block-pair transition preparation extend this lesson. Simulator truth remains outside receiver input. |
| Cached direct-GEMM acquisition bank | Prepared native lag-group matrices and batching across workspaces/arrivals provide a shape-specific analogue. They evaluate every public hypothesis and do not replace broad FFT acquisition with an unbounded dense bank. |
| Independent two-GPU trial sharding | Still applicable to independent compiled qualification jobs when needed. It is test orchestration, not one-stream receiver parallelism; it does not justify a second modem implementation. |

Current matrix batches can combine up to 32 workspaces, 64 arrivals and two taps
into 4,096 complex rows, subject to ready work and full memory admission. This
addresses a material historical distinction: useful large contraction shapes
must not be judged solely from the current fine stage's 1×1 or 2×2 Gram matrices.
Receipts expose actual dimensions and execution/fallback counts; potential
maximum shape alone is not evidence that a test dispatched it.

An optional split-BF16/TF32x3 variant would fit the prepared coarse matrix
boundary, subject to supported library/device shapes, explicit complex packing,
all split-product costs and scratch admission, complete public work and current
physical/sensitivity qualification. Historical score gates remain diagnostics,
not universal sensitivity criteria. It cannot replace the current Gaussian posterior or FP64 state by analogy
with the old phase lattice. The removed split-FP4 experiment informs that
residual technique without establishing those absent arithmetic modes.

The next graph boundary would be fixed-shape projection/statistics after native
rendering. It needs stable buffers, exact geometry/precision/bin signatures,
fresh observation and witness reset, warmed FFT plans, coordinated capture
across owners, and full graph-pool lifetime accounting. Callback-driven source
generation, changing keyed tiles and blocking readback cannot simply be captured
as though their host control flow replayed. Descriptor caching is a prerequisite
for some of this work, not proof that the historical graph gains are retained.

See the separate [CPU history inventory](triad-performance.md#earlier-cpu-implementations-worth-reconsidering)
for preserved native kernels, old waveform-specific methods and remaining reuse
opportunities. The current native precision guide keeps
[arithmetic and CPU forecasting limits](gpu-precision-controls.md#qualifying-a-faster-path)
separate from this historical inventory.

## Historical numerical limits and lifecycle

Plain BF16, TF32 and scaled FP16 phase-state contractions failed the original
likelihood-error/bias screen despite unchanged decisions in a small sample.
Joint FP8 observation quantization added misses in the separate 96-capture
sample. These failures do not invalidate the qualified split-product or FP4
arithmetic results above. A small FP64 restoration epilogue corrected subnormal
handling while bulk products remained lower precision; doing FP64 arithmetic
on stored complex64 templates never created an independent FP64 waveform.

Quantized-observation likelihoods, unknown interference and operational tails
were not qualified. The 0.1/0.25/0.5/1 dB sensitivity-loss budgets remained
unresolved, including hour/day misses shared by both reference and quantized
paths. Hardware utilization counters were unavailable; kernel timings and
instruction inspection did not establish SM occupancy or memory bandwidth.
These limits concern those historical models, not a general need for high
dynamic range in the current receiver.

Most implementations were removed in `419e6bae` during canonical-stack
consolidation, not because every measured win was disproved. The prepared
Gaussian Python implementation survived until `6bc9b0e3`'s Python retirement.
The superseded raw archives, plots, copied logs and precision evidence catalog
were removed after distilling these findings (including `345e8d70`) and the
[noise-model lesson](triad-noise-calibration.md). No current validation or
release claim depends on those campaigns. Follow the
[material lifecycle](development.md#material-lifecycle) rather than restoring
their drivers, source copies or exhaustive run instructions.

New intensive experiments must predeclare small distinct-path strata, confidence
and stopping rules, and applicable extrapolation. Unsupported prediction remains
unresolved. RunPod may shorten this same abbreviated method; it does not restore
the old full-workload objective or authorize larger campaigns.

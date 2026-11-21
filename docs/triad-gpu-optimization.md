# Robust GPU performance and retained lessons

This page holds the current GPU limits and ranked next steps, the method to
find and qualify a GPU improvement, the measured history of each GPU route,
and the retained historical Tensor Core lessons with their
[native disposition](#native-disposition-of-the-historical-techniques). The
current options and qualification rules are in the
[native precision guide](gpu-precision-controls.md), and the bounded test
method in the [abbreviated policy](triad-performance.md). There is one
canonical receiver implementation; alternative arithmetic belongs inside it.
The former frozen full-run optimization ladder, sensitivity sweeps,
calibration campaigns, historical precision controls and standalone GPU
receiver APIs are retired.

**Successful Tensor Core DSP implementations exist in Git history.** NVFP4
accelerated an actual large correlation batch on RTX PRO 6000; split-BF16 and
TF32x3 accelerated a phase-state workload on RTX 5090. The recent small FP16
Gram failure does not negate those results. Their arithmetic, layout and reuse
techniques deserve consideration inside the current native backend, with new
qualification for its waveform, shapes and complete work.

**Preserve the hardest-search computational ceiling when selecting defaults.**
Under the [automatic-mode contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
challenging-case complete service decides. An easy-case efficiency result is
insufficient, and an easy-case loss does not veto a challenging-case gain.
Promote a default only with applicable complete-work and backlog evidence.
The historical FP4 coarse lag-matrix candidate was removed after failing the
then-frozen noise-peak gate. That diagnostic failure alone is not a universal
precision veto: current qualification compares complete cost, physical
reception, false alarms and measured sensitivity loss under the user's
proportional tradeoff. A dense matrix's sample-by-frequency arithmetic growth
does not disappear when its storage is tiled.

## Current GPU limits and next steps

Improve complete receiver service on challenging work. Preserve every public
hypothesis, the configured calendar and keyed ordinals, bounded workspace,
physical quiet-slot completion and the full automatic `W = 2*H` software
worker budget ([worker allocation](development.md#receiver-cpu-allocation)).
Four- and 24-worker values in the records are explicit benchmark
allocations. Slot totals and the Vulkan stage splits below come from the
dated validation entries each row links. The stage splits and CPU-seconds of
the two CUDA rows were read from their stored receipts and are tabled with
the [cost model of the CPU families](validation.md#cost-model-of-the-cpu-families),
which records the inputs of its receipt splits, scenarios, arithmetic and
rates. The levers' other modeled figures (the drift and varying redesign
chains, the template upload, the gain-rank traffic and the FP32 AVX2
estimate) are rough estimates whose inputs were not kept: they order the
work and qualify nothing. Hosts differ between entries, so compare only
within one entry.
[Find and qualify a GPU improvement](#find-and-qualify-a-gpu-improvement)
gives the method.

### What automatic selection runs

`--backend gpu` and the GUI's **Default (fastest)** use one factory. What it
runs depends on the build and the device:

| Build and device | Device work | Native, and how to check |
| --- | --- | --- |
| Vulkan build with the OpenCL NVFP4 module (`-DDATAPUMP_TRIAD_TENSOR=OFF -DDATAPUMP_TRIAD_VULKAN=ON`, `DATAPUMP_TRIAD_OPENCL_NVFP4` compiled; the lab's `--mode gpu` deployment when it reports `PUMP_LAB_MODE=vulkan`) on the **SM 12.0 class**: an NVIDIA GPU that OpenCL reports as compute capability 12.0, with the Vulkan device's UUID. The RTX PRO 6000 Blackwell is qualified. An RTX 5090 is in the same class and would receive these routes unmeasured. | The [dense FP4 search](#dense-fp4-coarse-search-and-device-template-bank) for eligible frequency-tile sessions, two-ray and one-ray within the `dense-cost` bound, with the device template bank and FP32 partial rescoring; the Vulkan FFT search for sessions it declines. Vulkan segmented timing inside the [CUDA-qualified envelope](gpu-precision-controls.md#current-implementation). The [signed-lag gain direct rank](#signed-lag-ranks), which publishes only certified nominations equal to native. Resident FP4 fine statistics for the isolated 1,500-row, 47-mode, two-path DATA geometry, with [device tiles, windows of 16 and FP32 mapping and rendering](#device-rendered-fine-tiles-and-resident-windows). | Helper; lag-group and compact tight-clock coarse sessions (`native_coarse_lag_sessions`, `native_coarse_precision_sessions`); segmented shapes outside the envelope (`vulkan-segmented-unqualified-shape`, for example every call of the 72 Hz/1× slot); drift proposals and varying statistics (`vulkan-drift-unqualified-device`, `vulkan-varying-unqualified-device`); fine shapes outside the resident geometry; Gaussian states, recursions, decisions and codec. Check that `vulkan_segmented_qualified_device` and `vulkan_gain_rank_qualified_device` are true, and read `opencl_coarse_score_rows`, `opencl_resident_nvfp4_modes`, `vulkan_segmented_sessions`, `vulkan_gain_rank_sessions` and `signed_lag_gain.proposal_route` (`fp32_fft_direct_rank_fp64_accumulation_fp64_rescored_frontier`). This route holds the larger plan and shader caches, 59,088,896 B more per owner, and the device rank adds 6.8–72 MB of host quote per owner (6.8 MB on the 72 Hz/1× slot, 68.7 MB on the 72 Hz/5× control, 71.6 MB on the holdout). |
| The same build and device with FP32 minimum (the GUI's FP32 minimum, `--gpu-fp32-minimum on`) | The Vulkan FP32 FFT search. The factory sets FP32 coarse precision and drops the chirp-Z preference. The backend opens OpenCL automatically only with automatic coarse precision, so the SM 12.0 class is not identified (from the code; unmeasured). | No dense FP4 search, resident fine route, automatic segmented timing or gain rank. |
| The same build on any other Vulkan GPU: other NVIDIA generations, Radeon, Intel | Vulkan FFT frequency search; on the Radeon its qualified packed-FP16 direct route within its envelope; the [host-scoring compatibility route](gpu-precision-controls.md#gui-automatic-gpu-selection) where shader FP64 or INT64 is missing. | Segmented timing and every fine-stage family decline as `unqualified-device` (`vulkan-segmented-shader-fp64-unavailable` and `vulkan-<family>-shader-fp64-unavailable` on the host-scoring route). The explicit `--vulkan-gain-rank`, `--vulkan-drift` and `--vulkan-varying-fine` `device` routes run on RADV and are exact within their classes. Single loaded Radeon runs of drift and varying looked slower than native; the gain rank has no gated Radeon timing ([rejected approaches](#rejected-and-conditional-approaches)). |
| CUDA build (`-DDATAPUMP_TRIAD_TENSOR=ON`; the [RunPod simulation build](triad-performance.md#run-a-native-simulation-on-runpod) also sets `-DDATAPUMP_TRIAD_VULKAN=OFF`). A build with both backends tries CUDA first. | FP32 cuFFT segmented timing on SM120; guarded half cuFFT for SM89 and SM120 broad searches; the bounded FP16/INT8 lag matrix; CUDA fine processing (`TensorFine`: cuFFT projection, NVFP4 on SM120 where cuFFT cannot plan). | Never the dense FP4 search, the OpenCL resident fine route, the FP32 OpenCL kernels, Vulkan segmented timing or any fine-stage family: the CUDA backend keeps the null stage factories of [the bulk interface](../src/triad_receiver_bulk.hpp). With automatic precision other than FP64, single-tap fine shapes of at most 1024 rows and 40 projection modes run on the CPU with at least 26 workers on an AVX2 host. The stored HF receipts and the 36 Hz/10× −18 and −21 receipts ran this automatic route; the other stored 36 Hz receipts, including the holdout input's −12.9897, and the 60 Hz receipts ran explicit `--backend cuda`, most with CUDA segmented timing requested. Check the [CUDA counters](#which-arithmetic-ran-in-a-remote-simulation). |
| Developer `--backend vulkan` (not automatic) | Without an OpenCL request the class is not identified, so automatic segmented timing and the gain rank decline as `unqualified-device`. `--vulkan-precision nvfp4`, or `--vulkan-fine-projection` `nvfp4`, `resident-nvfp4` or `resident-fft`, identifies it. With `--vulkan-precision nvfp4` the dense search is required for every session and always scores one-ray sessions. With only a `--vulkan-fine-projection` request the coarse search stays on the Vulkan FFT route. Either way the route differs from automatic selection: each coarse session has its own workspace (256 MiB by default) and the chirp-Z preference is off. | Use it to attribute time and to compare a stage against native; decide defaults on unprofiled automatic runs of the flipped and unflipped builds ([Compare routes](#compare-routes)). |
| CPU (`--backend cpu`) | None. | Every stage. |

**No automatic route is fastest on every geometry.** The CUDA route lacks the
dense FP4 search, the resident fine route and the fine-stage families. Both
routes share one segmented envelope, which keeps the 72 Hz/1×-class shapes
native; the Vulkan/OpenCL route's segmented rate at HF shapes is
unmeasured. No same-pod, same-source comparison of the two automatic routes
exists. Unify their kernels, or select per stage once both are qualified
([next levers](#next-levers-in-order)).
Selection and precision rules are in the
[precision guide](gpu-precision-controls.md#fine-stage-device-routes).

### Where the time goes

All rows ran on one full RTX PRO 6000 Blackwell Server Edition unless named.
Slot and stage times are wall seconds of one slot:

| Slot and input | Route, host, workers | Slot | Largest parts | What bounds it |
| --- | --- | --- | --- | --- |
| Matched 3.6 Hz severe (`b4d8fa5a…`) | Vulkan/OpenCL automatic, EPYC 9335, 13.6-CPU quota, 24 workers ([20 Nov](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)) | 0.060–0.068 s | Coarse 0.020–0.024 s and fine 0.039–0.043 s on the final tree. Explicit profiled route: warp and FIR 7.2 ms, FP4 products 2.6 ms, FP32 mapping 5.1 ms, tile rendering 2.5 ms, lane rendering 1.9 ms, rescoring 0.89 ms; fine device span 34.7 ms. The simulator takes 0.19–0.35 s. | Fine owner work and the window wait. Before the FP32 kernels, on an EPYC 9554, the wait was 14–15 ms, owner addresses 14.8 ms, staging 9.3 ms and submission 9.8 ms ([remaining fine costs](#remaining-fine-costs)). No current per-slot device total is recorded. |
| 72 Hz/1× HF Worst, 0.2 W (`7e8ef8fa…`) | Vulkan/OpenCL automatic with the gain rank, Xeon 6952P, 40.8-CPU quota, W = 80 ([21 Nov](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026)) | 6.56 s; 5.58–6.61 s before the gain-rank flip | Coarse 5.52 s, of which signed-lag 0.43 s; fine 1.03 s, of which drift 0.41 s and varying fine 0.36 s | Native segmented timing: all 156 calls lie outside the envelope. With explicit device segmented timing on the same host the slot took 2.61–2.97 s and coarse 1.71–2.21 s. |
| 72 Hz/5× control, 2 mW (`af364b67…`) | Same | 17.75–19.76 s; 38.4–55.5 s in three runs before the flip (43.7 and 55.5 s in the two flip pairs) | Coarse 9.36–11.2 s: signed-lag 1.34–1.47 s and 8.0–9.7 s of other coarse work, including the device segmented stage; the retained summaries omit its `varying_coarse_seconds`. Fine 8.38–8.55 s: drift 2.18–2.22 s and varying fine 5.32–5.47 s. | The unsplit coarse remainder, then varying fine and drift. |
| 36 Hz/10× 100 mW holdout, Target −12.9897, 1,797.6 s bit (`8a6c8ab2…`) | Same | 281 s and 4,919 CPU-s; 688 s and 21,689 CPU-s before the flip | Coarse 113.3 s, of which signed-lag 3.05 s; fine 167.6 s, of which drift 60.9 s and varying fine 89.9 s | Native drift and varying fine, 151 s or 54% of the slot, then about 110 s of coarse work outside the rank, not split. About 17.5 of 40.8 CPUs were busy on average. |
| Same input | Explicit `--backend cuda` with CUDA segmented timing, 13-CPU allocation, W = 26, earlier source ([18 Nov](validation.md#blackwell-receiver-trials-at-2-mw)) | 401 s and 2,257 CPU-s | Coarse 218 s: CUDA segmented 133 s and signed-lag 81 s. Fine 183 s: drift 128 s and varying fine 38 s. | Native signed-lag and drift on that source; the CUDA route has no fine-stage device route. |
| HF 100 mW 36 Hz/10× Target −22.5, 17,205.6 s bit: the hardest stored receipt | CUDA automatic, 13-CPU affinity on an EPYC 9555 pod with a 27.2-CPU quota, W = 26 ([18 Nov](validation.md#blackwell-receiver-trials-at-2-mw)) | 3,707 s and 14,050 CPU-s, 3.8 CPUs busy on average | Coarse 2,220 s: CUDA segmented 1,626 s, signed-lag 386 s, other 208 s. Fine 1,487 s: drift 899 s, varying fine 401 s, other 186 s. The simulator took 404 s. | CUDA segmented timing, 44% of the slot, and the three CPU families, 46%. Device memory was nearly full (`peak_reserved_bytes` 99,243,065,344 of 101,975,851,008). The other 208 s of coarse work include the affine frequency search, whose half cuFFT declined the shape (`cuda-fp16-fft-unqualified-shape`), and the slot uploaded 249 GB. |
| 72 Hz/1× slot and 72 Hz/5× control | Radeon Vega Vulkan build, Ryzen 5 PRO 5650U, 24 workers, native segmented timing ([21 Nov](validation.md#fine-stage-device-routes-and-exact-cpu-stages--21-november-2026)) | 22.9–23.9 s and 250.9–251.6 s | Coarse 20.7–21.7 s and 240 s; fine 2.20–2.24 s and 10.9–11.0 s | Native segmented timing. The explicit device segmented route made the 72 Hz/1× slot about 10% slower, 25.7–26.1 against 23.5–23.6 s (process wall 25.9–26.3 against 23.7–23.8 s), with 29 against 258 process CPU-s ([20 Nov](validation.md#gpu-offload-survey-and-gated-device-routes--20-november-2026)). |

The 21 November stage split of the Blackwell rows is tabled in its
[validation entry](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026).
Reading the table:

- Stage timers nest. A slot is about Helper (when not overlapped) plus coarse
  plus fine. The segmented span and the signed-lag stage lie inside coarse,
  drift and varying fine inside fine
  ([Attribute the time](#attribute-the-time)).
- **Native stages scaled inversely on the large-quota pod; the cause is
  unmeasured.** On the 72 Hz/5× control the native signed-lag stage took
  20.3–38.2 s across nine runs of one binary on the Xeon at W = 80, against
  8.26–8.36 s on the 12-thread 5650U at W = 24, and native varying fine
  4.8–5.6 s across eleven runs, against 2.82–2.92 s. Drift scaled normally:
  2.13–2.30 s against 6.51–6.61 s. On the holdout input, native varying
  fine took 89.9 s at W = 80 against 37.6 s on a 13-CPU allocation at
  W = 26 (explicit `--backend cuda`, earlier source), and the native
  signed-lag stage 415 s against 81 s. Stored CUDA receipts show a similar
  pattern: 16,384-point signed-lag ranks ran 2.5–5.3e8 FFT row-stages per
  thread-second on 13-CPU allocations (the highest on a 27.2-CPU pod
  restricted to 13 CPUs by affinity) against 0.9–1.2e8 on 27-CPU quotas,
  some with an RTX PRO 4500; the two 8,192-point ranks ran 1.2e9 on a
  13-CPU allocation and 6.2e8 on a 27-CPU quota. Varying fine took 0.6–0.8 ms per 200-row block against 1.1–1.9 ms.
  Device-versus-native margins are therefore host-specific, and pod
  per-thread rates must not stand in for local costs. Candidate causes are
  static round-robin signed-lag lanes (`min(W, blocks)`), the per-batch
  phase barriers of varying fine (projection, statistics and recursion
  waves per batch of up to 256 blocks) under a CPU quota with twofold
  oversubscription, and contention when the workers fill a whole quota
  rather than an affinity-restricted allocation inside it. Investigate it
  as serialization; do not lower `W = 2*H`.

### What bounds the challenging slots

Ranked by their share of the challenging slots above. Each item says whether
it is measured, modeled from stored receipts or inferred from the code.

1. **Native drift and varying fine (measured share, inferred causes).** They
   took 151 s of the 281 s holdout and 7.5–7.7 s of the 72 Hz/5× slot on the
   Blackwell automatic route. Explicit device routes exist but were slower on
   the 40.8-CPU host: drift 0.81–1.65 against 0.36 s on the 72 Hz/1× slot and
   2.96–3.28 against 2.23–2.26 s on the 72 Hz/5× control; varying statistics
   3.16–3.60 against 0.18–0.30 s and 13.2 against 4.97–5.23 s. Likely causes:
   the drift session serializes every lane's device calls, the host still
   renders and uploads the drift products, and both families make many small
   calls whose fixed cost exceeds their transform work at these shapes
   ([drift](#drift-proposals), [varying](#varying-statistics)). Neither route
   was timed at the holdout or HF shape; no explicit configuration has fit
   the holdout yet ([open measurements](#open-measurements)).
2. **The coarse search outside the signed-lag rank (measured share,
   unmeasured split).** The 72 Hz/1× slot keeps native segmented timing,
   because its shapes lie outside the CUDA-qualified envelope. The explicit
   device route took 0.59 s against 4.58 s for that stage on the 20 November
   EPYC 9335 host and about halved the slot on the Xeon, 2.61–2.97 s against
   5.58–6.61 s. On the automatic route 8.0–9.7 s of coarse work besides the
   rank is unsplit on the 72 Hz/5× control, and about 110 s, 39% of the
   slot, on the flipped holdout. Both slots' segmented shapes lie inside the
   envelope, so both remainders presumably include a device segmented stage:
   it took 5.7–6.0 s on the 72 Hz/5× control on the 20 November host, and
   the CUDA route spent 133 s in segmented timing on the holdout input.
   `admission` reports that span as `varying_coarse_seconds`, but the
   21 November summaries did not keep it, so the next automatic run splits
   these remainders without new instrumentation. Then the segmented
   efficiency and per-stage selection [levers](#next-levers-in-order) apply.
3. **The segmented search at HF scale (measured rates, modeled share).** It
   took 1,626 s of the 3,707 s HF slot on the CUDA route. With the three CPU
   families ported whole it would be 71–92% of the slot across the 18
   CUDA-segmented receipts of the 20 heaviest stored receipts. CUDA
   sustains 3.9–5.2e11 FFT row-stages/s on the RTX PRO 6000 at N = 32,768,
   about 2% of the FP32 peak, and 0.7–1.4e11 on an RTX PRO 4500. If each of
   about three passes reads and writes 8-byte complex values (modeled), the
   RTX PRO 6000 rate moves 1.2–1.7 TB/s, near memory bandwidth. The in-slot
   Vulkan rate was 3.2e11 at N = 16,384 on the 72 Hz/5× control: 8,075,106
   inverse FFTs × 16,384 points × 14 stages in 5.715 s, not a matched
   shape. The native segmented search ran 4.0e10 on an
   EPYC pod and the Radeon's Vulkan route 5.6e9.
4. **The three CPU families are bound by structure and latency, not
   arithmetic (modeled).** On the HF slot they do about 2.8e13 floating-point
   operations in 1,687 s, about 1.7e10 per second; the CUDA segmented stage
   does about 3.2e15 in 1,626 s
   ([model inputs](validation.md#cost-model-of-the-cpu-families)). FFTs are
   only about 15–30% of drift CPU time. On the receipts' source varying fine
   ran a barrier per block at 0.6–1.9 ms per 200-row block; the current tree
   runs three barrier phases per batch of up to 256 blocks. Moving only the
   FFTs would give 1.19–1.30× on the HF slot. Fused whole-family device pipelines that
   keep the FP64 exact parts would give 1.70–1.75×, about 2,120–2,180 s, and
   1.33–2.6× across the 18 CUDA-segmented receipts of the 20 heaviest; the
   two native-segmented receipts reach 7.4–8.0× once segmented timing also
   moves. Even FP64 at the 1/64 rate, about 2 TFLOP/s, would finish the
   three families' arithmetic in about 14 s at that peak and 28 s at half
   of it, so device FP64 is acceptable where it removes round trips,
   barriers and host passes. The FFT shares are the weakest part of this
   model.
5. **The CPU waits during device stages (measured).** On the HF slot 3.8 of
   13 CPUs were busy on average, on the flipped holdout about 17.5 of 40.8.
   Signed-lag ranks start after the segmented stage drains, and drift and
   varying fine run in the fine stage after coarse ends. Overlapping ready
   work is a lever, not a reason to lower `W`.
6. **The automatic signed-lag stage is slower than the explicit one
   (measured, unexplained).** On the 72 Hz/5× control it took 1.34–1.47 s on
   the automatic route against 0.57–0.68 s on explicit `--backend vulkan`
   with a 4 GiB explicit workspace and device segmented timing. Candidates
   are a narrower batch under the automatic optional lease, at most a quarter
   of the shared workspace, and queue contention with the OpenCL and segmented
   sessions. The gain-rank receipt records no batch width; add it with
   [the first lever](#next-levers-in-order) and compare it with
   `vulkan_gain_rank_upload_bytes` and `vulkan_peak_leased_bytes` on the next
   device run.
7. **Admission charges that cap owners (code; effect unmeasured).** The
   backend-wide VkFFT and SPIR-V cache bound is charged in the stream
   reservation and again in each owner's lease. On the SM 12.0 automatic
   route the larger bound adds 59,088,896 B per owner and in the reservation,
   about 4.8 GB at 80 owners, while all three families together used 18
   entries and 2,241,636 B on the Radeon's 72 Hz/1× slot (no Blackwell cache
   use was recorded). Every family's host quote
   includes a 128 MiB compilation bound (`compiler_host_bound`), which can
   push a tight discrete-GPU envelope into a `-workspace` fallback. The
   explicit-workspace rules also differ: gain-rank and drift sessions are
   each bounded by the ceiling, varying sessions share it
   ([Fine-stage device ports](#fine-stage-device-ports)).
8. **One device, one queue (inferred).** The [Vulkan core](../src/triad_vulkan_core.hpp)
   opens one compute queue, submits under one queue mutex and orders
   dispatches with global compute barriers, so concurrent owners' device work
   runs in submission order. Building a twiddle-table plan holds the queue
   lock through VkFFT's initialization. Segmented sessions build their plans
   per coarse session, the gain rank per rank call, and drift and varying
   per stage. The cost under several owners is unmeasured.
9. **The matched fixture (easy).** It is not a priority under the contract;
   see [Remaining costs after the dense search](#remaining-costs-after-the-dense-search)
   and [Remaining fine costs](#remaining-fine-costs). Separate processes
   sharing the device lose most of the gain, 3.0 s against 0.45–0.81 s per
   slot, so one pod runs one receiver.

### Next levers, in order

Ordered by expected challenging-case effect per cost. Expected effects marked
modeled come from fixed-geometry cost models of the stored receipts, not
measurements.

1. **Attribute the fine-stage device routes (instrumentation, first).** Add
   per-family device intervals (stage contexts create no timestamp query pool
   today), device-call counts, download bytes, the gain-rank batch width and
   the session-mutex wait to the family receipts, and record plan and
   compilation seconds per family. Until then the causes in
   [item 1 above](#what-bounds-the-challenging-slots) are inferred. Results do
   not change, so the identity gate and the receipt checks of
   `tools/triad_native.py` qualify it.
2. **Redesign the drift device route.** Remove the session-wide
   serialization: per-lane command buffers and staging slots with shared
   plans, or an asynchronous submission that batches independent rank-pass
   atoms across lanes, 40–48 per sign on HF receipts. Per-lane contexts would
   multiply twiddle tables, about 270.5 MB per 2^25-point plan. Render the
   rank-pass products on the device with FP32 per-row arithmetic and FP64 or
   integer coordinates, as `fp32-rn4-v1` does (proposal-only; planned, not
   built), and upload the observation once per slot, 827 MB in FP32 at HF.
   Exact CPU changes that also shorten the device route: cache the survivor
   slope lists (19 of 109 HF evaluations re-render and rerun both ridges),
   share the direct-path render and products between the two signs, which are
   equal up to a power of two, and run both signs' survivor passes
   concurrently. Expected at HF (modeled): 899 s → about 100–150 s with the
   slope-blocked ridge, if its 63 ns per load reflects the HF host (an
   EPYC 9555; time the [ridge benchmark](#drift-proposals) on that CPU
   class to test it) → 60–90 s with device proposals → 20–40 s with device
   rendering, after which the exact FP64 survivors bound it. Qualify at
   proposal level on the holdout and the 72 Hz/5× control. Reconsider the
   present verdict once the route is timed at the holdout or HF shape.
3. **Redesign the varying device route.** In order: split one host batch
   into several device calls instead of shrinking the host batch; FP32
   template arithmetic (`fp32-template`, no kernels yet), which halves the
   template upload (modeled: about 562 GB in FP64 against 281 GB in FP32 per
   HF slot); move the raw-composite tracker to the device; render templates
   on the device, which needs a varying shape because the OpenCL renderer is
   fixed at 1,500 rows × 47 modes; widen the gather kernels; and try a dense
   DFT-to-modes contraction for 200- and 239-row blocks (27–33 modes), which
   reads each template once, against chirp-Z on bandwidth-bound Vega. Qualify
   `fp32-template` at the evidence level with per-block FP64 fallbacks for
   range, nonfinite values, fragile `DriftGram` pivots and raw-input dynamic
   range, plus a native rerun near the decision margin. A worst-case FFT
   bound summed over 516,614 HF blocks, 10–20 nats, is too loose to veto it;
   HF −22.5 sits 1.22 nats from its separation limit. On the CPU, a
   linear-domain scaled HMM recursion, one exp per state instead of about
   four log-adds, would cut the Markov part about 4× and needs its own
   qualification. Expected at HF (modeled): 401 s → roughly 30–60 s with
   device statistics, about 15 s with scaled Markov updates and device
   rendering. Reconsider at the holdout, where native varying fine takes
   90–97 s.
4. **Native-stage scaling at large `W` (CPU-side, no device
   qualification).** Find the serialization behind the inverse scaling in
   [Where the time goes](#where-the-time-goes); keep the `W = 2*H` default.
   Expected (modeled; the cause is unknown): matching the holdout input's
   37.6 s varying fine at W = 26 (explicit `--backend cuda`, earlier source)
   would save about 52 s of the 281 s flipped holdout, 19%; matching only
   the 5650U's 72 Hz/5× time would cut the stage 1.6–2.0×, about 35–45 s
   there. The native signed-lag stage, 2.4–4.6×
   slower on the 72 Hz/5× control than on the 5650U, still runs on CUDA
   builds and every device outside the SM 12.0 class. Start with section
   timers or thread-state samples
   ([method](triad-performance.md#paired-timing-and-load-gating)) on the
   72 Hz/5× control on a large-quota pod, beside a run restricted to 13 CPUs
   by affinity.
5. **Widen the qualified Vulkan segmented envelope** to the 72 Hz/1×-class
   shapes, which have fewer arrivals or blocks than the envelope admits. HF
   and holdout segmented shapes already lie inside it (the CUDA route
   scored them on the device under the same predicate), so the SM 12.0
   automatic route admits them, subject to the optional quarter-workspace
   lease. Measured on the 72 Hz/1× slot: stage 0.59 against 4.58 s, slot
   about halved. The route changes proposals at near ties (two of eight
   72 Hz/1× proposals on the Radeon, clock siblings 0.0003 ppm apart), so
   qualify decisions and evidence on the strata
   ([segmented route](gpu-precision-controls.md#current-implementation)).
6. **Segmented efficiency at HF scale, on both routes.** First measure the
   Vulkan rate at N = 32,768: `test_triad_vulkan --segmented-benchmark` on
   the device covers a 32,768-row shape (4,500 arrivals) and a 16,384-row
   shape with 10,553 arrivals; then read `varying_coarse_seconds` and the
   `vulkan_segmented_*` declines and fallbacks of an automatic holdout run
   for its share of the slot. The rate is bandwidth-bound, so cut traffic:
   fuse the shifted spectral product into the FFT load; prune the inverse
   output, since only 10,553 of 32,768 lags are read at HF; and share column
   spectra across the 15,733 frequency shifts. Segmented timing has no
   half-storage form. The factored HMMA inverse was 1.87–4.01× slower than
   FP32 cuFFT ([dispatch](#hardware-and-library-dispatch)).
7. **CUDA parity or per-stage selection.** Add cuFFT forms of the three
   families for CUDA builds, or a build that selects CUDA or Vulkan per stage.
   First compare the two automatic routes on one pod, from one source, on the
   [qualification slots](#qualification-slots). For the HF slot's other
   208 s of CUDA coarse work, qualify the affine frequency search's half
   cuFFT for that shape, which declined it
   (`cuda-fp16-fft-unqualified-shape`), and cut its 249 GB of upload.
8. **Overlap.** Run ready fine-family work beside the device segmented stage
   within the bounded executor and ordered commits, and measure it with
   backlog intervals ([Compare routes](#compare-routes)).
9. **Admission.** Charge the backend-wide cache once, in the stream
   reservation; size the stage bound from measured family use; measure a
   smaller compilation bound; reconcile the explicit-workspace rules; and
   size the VkFFT temporary buffer from VkFFT's requirement, which would
   roughly double the admitted gain-rank batch. Each raises admitted owners or
   batches under a binding memory limit, and no result changes.
10. **Gain rank.** Explain item 6 of the bounds above. Then a fused
    local-memory rank kernel: by a traffic model the unfused four-kernel
    form moves about 10 TB on the HF slot, about 6 s at 1.6 TB/s on the
    RTX PRO 6000, against an estimated 0.6 s fused. Scaling the holdout's
    measured 3.05 s by its block count (42,141 against 403,266 at HF) gives
    about 29 s, about five times the traffic model, so item 6 and the fused
    kernel are Blackwell HF levers too, though small beside the HF slot's
    other stages. The fused kernel is also the prerequisite for a Radeon
    class, about 45 s at HF on Vega (modeled). Further: a predeclared native
    audit set, so that ranks without sampled blocks
    (`signed_gain_full_direct_without_fixed`, now `audit-unavailable`) can
    certify; the fixed rank on the device for short apertures, 25.6% of the
    72 Hz/5× rank's FFT stages and about 1.5 s on the 5650U; and, for
    CPU-only targets, an FP32 AVX2 FFT with a vectorized two-pass
    log-mean-exp certified by the same FP64 frontier (estimated 3–5×; the
    epilogue is about 7 of 12 CPU-ms per HF block).
11. **Matched-fixture levers.** Fuse the fine map, peak and pack; synthesize
    whitened two-ray, varying-passband, midpoint-gain and FHSS-exact sessions
    on the device; move Gaussian updates to the device once its FP64 load
    falls; adopt a counter-based simulator when another change already
    alters every capture hash (`--capture-cache` keeps the simulator out of
    repeated comparisons meanwhile); widen the arrival frontier; and cut the
    product kernel's loads
    ([Remaining costs after the dense search](#remaining-costs-after-the-dense-search)).
12. **Other generations.** Measure an RTX 5090 first: it inherits the SM 12.0
    qualifications unmeasured, and a desktop host with fewer CPUs is the case
    most likely to differ. The RTX 4090, 4090 Laptop and 4060 Ti 16 GB have
    no FP4 matrix instructions, so the FP32 per-row kernels matter there;
    FP64 runs at about 1/64 rate on these NVIDIA parts and about 1/16 on
    Vega. Every Vulkan buffer requires host-visible, coherent memory and
    prefers device-local memory, so a discrete GPU without resizable BAR may
    expose only a small host-visible device-local heap and get a small
    automatic workspace (unmeasured).

### Rejected and conditional approaches

| Approach | Outcome and scope | Reconsider when | Detail |
| --- | --- | --- | --- |
| Drift device route, FP32 VkFFT, proposal-level | Slower than native at both 72 Hz shapes on the 40.8-CPU Blackwell host (two alternated pairs). On the Radeon single loaded 72 Hz/1× runs looked slower (indicative); no single-family 72 Hz/5× run was made | After lever 2, or once timed at the holdout or HF shape | [Drift proposals](#drift-proposals) |
| Varying device route, FP64 | Slower than native at both 72 Hz shapes on the 40.8-CPU Blackwell host (two alternated pairs). On the Radeon single loaded 72 Hz/1× runs looked slower (indicative) | After lever 3 | [Varying statistics](#varying-statistics) |
| Gain rank on the Radeon | Declined; no gated Radeon timing. The unfused form is memory-bound on Vega, about 2 ms per HF oracle block (indicative) | After a fused local-memory kernel: a complete-slot win on the challenging 72 Hz/5× control at full `W` and no slower complete service; the 72 Hz/1× slot is a regression check, not a veto | [Signed-lag ranks](#signed-lag-ranks) |
| Similar-lag (proposal-qualified) gain ranks instead of exact publication | Rejected: on the HF −22.5 and 36 Hz/10× −21 receipts the bit separation equals one direct-rank map's evidence difference to about 1e-13, so similar lags do not preserve decisions | Not at those shapes | [Signed-lag ranks](#signed-lag-ranks) |
| Dense FP4 forms of FFT-structured stages | Dense work is about 0.4·n/log2(n) times the FFT's, about 140× at n = 4,096, which cancels FP4's advantage | Only for small n and proposal-only parts | [Where FP4 fits](#where-fp4-fits-and-the-larger-levers) |
| Tensor Core (FP16, BF16, TF32) DFT form of the direct rank | About 4× the FFT's operations, bounded by the per-cell exp epilogue, 2.8e11 cells at HF; on Vega, which has no matrix cores, the FP32 FFT wins | Measure against a fused FP32 FFT first | Modeled, not built |
| FP4 for rendering, warp, mapping, rescoring, Gaussian states and the simulator | No contraction whose output only chooses candidates | — | [Where FP4 fits](#where-fp4-fits-and-the-larger-levers) |
| Factored HMMA inverse FFT on SM120 | 1.87–4.01× slower than FP32 cuFFT | A new factorization or fusion | [Dispatch](#hardware-and-library-dispatch) |
| Separate processes sharing one device | 3.0 s against 0.45–0.81 s per slot | Never for one receiver | [Remaining costs](#remaining-costs-after-the-dense-search) |
| Fine windows above 16 blocks | No gain on the matched slot | Another geometry | [Remaining fine costs](#remaining-fine-costs) |
| Global warp table instead of the register stash | Byte-identical, 18 MB per map | If the stash spills | [Remaining costs](#remaining-costs-after-the-dense-search) |
| Resident copies of signed-lag inputs | Host packing not measurable; 0.3 GB more at every width | If packing shows up on a fast device | [Signed-lag ranks](#signed-lag-ranks) |
| FP32 fine tile storage | Would lose the C128 presence witness of samples below the FP32 range | — | [Precision guide](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling) |
| Native fine processing on CPU-rich hosts | Resident fine was faster on the 20 November host; the 24-worker single-slot order reversed between hosts | On a CPU-rich host with a matched comparison | [Precision guide](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling) |
| GPU simulator | Changes every capture hash | With another capture-changing change | [Lever 11](#next-levers-in-order); [survey](validation.md#gpu-offload-survey-and-gated-device-routes--20-november-2026) |
| Historical RTX 4090 FP16 replay; FP8 observation quantization | Slower than FP32; FP8 added misses. Counterexamples only | — | [What to measure separately](#what-to-measure-separately) |

### Open measurements

- Per-family device time, call counts and mutex wait ([lever 1](#next-levers-in-order)).
- The varying phase split, `varying_batches`, `varying_batch_blocks`, the
  drift section split, the segmented span (`varying_coarse_seconds`) and the
  plan and compilation seconds on the Blackwell challenging slots. The probe
  emitted them, but the retained 21 November summaries omit them and the
  raw receipts were not kept; [step 10](#run-a-bounded-device-qualification)
  now lists them.
- The split of the holdout's coarse remainder, about 110 s:
  `varying_coarse_seconds`, `vulkan_segmented_sessions` and the segmented
  declines and fallbacks on the next automatic holdout run.
- Explicit family routes at the holdout: no configuration has fit. A 4 GiB
  device workspace was too small for its coarse session, and 48 GiB
  explicit plus per-owner leases exceeded a 64 GB host limit at 80 owners
  ("Native full-slot probe exceeds its local memory lease"). Try a host
  limit near the cgroup (188 GB on that host), an explicit `--slot-owners`
  count and a device workspace sized for its coarse session.
- An API and GPU-workload trace (for example Nsight Systems) of an
  unprofiled `--backend gpu` run, which could give fine-stage device
  intervals on the automatic route without code changes. Untested on the
  pod, which may not grant the permissions it needs.
- Hardware counters: Tensor pipe duty and `pipe_fp64`. Earlier CUDA counter
  requests returned `ERR_NVGPUCTRPERM`. The FP32 mapper's dynamic FP64-pipe
  use is open; its static SASS has no `DFMA`, `DADD` or `DMUL`, 8
  `F2F.F32.F64` and 3 `I2F`.
- CUDA device time: the CUDA backend records host `wall_seconds` only, so the
  HF receipts cannot split device from host time.
- A current per-slot device total on the matched slot.
- Multi-owner device behaviour: the queue lock during plan builds, the
  non-blocking leases and the single queue. Record `vulkan_fft_plan_seconds`
  and per-family fallbacks in a multi-owner run.
- Longer and challenging-case backlog. A 12-slot matched backlog
  (`6295fc3d…`) took 0.706–0.708 s of receiver service on the candidate
  against 0.831–0.835 s on the reference of the 20 November offload
  qualification at 24 workers, and 1.094 against 1.286 s at four.
- Whether the Radeon's explicit segmented route, which used a ninth of the
  process CPU at a 10% slower single slot, serves more slots in a
  multi-owner backlog.
- The slope-blocked ridge on a complete HF slot, and drift lane counts at
  W = 80 on an HF-shape (extended-plan) slot; the 72 Hz slots ran 43–48 of
  58 planned lanes.
- A same-pod comparison of the CUDA and Vulkan/OpenCL automatic routes.
- The RTX 5090 and other generations, for every family.
- The first run of each family pair was slower: drift 1.65 then 0.81 s on the
  72 Hz/1× slot, varying 3.60 then 3.16 s, gain 0.46 then 0.24 s. Check
  `vulkan_fft_plan_seconds` and `vulkan_shader_compile_seconds` for a
  cold-cache effect.
- The route-dependent Vulkan timestamp difference in the
  [timing record](validation.md#five-slot-timing-and-measurement-scopes), and
  the 5–21% two-binary difference of the Vulkan FFT coarse route, a
  [code-placement candidate](triad-performance.md#code-placement-and-x87-rounding-mode-switches)
  untested on a device.

### Receiver path and timing landmarks

| Work | Where it runs and where to inspect |
| --- | --- |
| Source, channel and receiver service | The [native probe](../tools/triad-receiver-probe.cpp) generates the input and replays it through the [stream receiver](../src/triad_stream_receiver.cpp). Application scheduling, ordered commitments, codec and physical completion remain native. Inspect `score_stages` in [the receiver](../src/triad_receiver.cpp), which sets `coarse_seconds`, `varying_coarse_seconds` and `fine_seconds`, and `stream_run` in the probe for timing boundaries. |
| Coarse search | On Blackwell, an eligible session uploads keyed coefficients and the channelized observation, and the [dense FP4 search](../src/triad_opencl_nvfp4_coarse.cpp) renders the bank, scores every row and returns rescored arrival frontiers through [its adapter](../src/triad_opencl_coarse_bulk.cpp). Other sessions use the following route. [Coarse preparation](../src/triad_receiver_coarse.cpp) has native observation channelization, keyed template bank, exact cubic warp and reused Doppler phase. `Projection::apply` performs native overlap-add FFT convolution on the filtered Vulkan route. [Vulkan](../src/triad_vulkan_backend.cpp) transforms the bank and rotated observations, forms correlation products, does inverse FFT and quadratic scoring, then compacts the exact peak frontier before readback. The matched broad geometry has 78 workspaces, 15 frequencies and 3,377 arrivals, with two rays per template. |
| Segmented timing | `TensorCoarse::plan_segmented` and the [CUDA scoring kernels](../src/triad_tensor_peaks.cu) on CUDA; the segmented session in [the Vulkan backend](../src/triad_vulkan_backend.cpp) on Vulkan. |
| Signed-lag ranks | `rank_gain_timing` in [coarse preparation](../src/triad_receiver_coarse.cpp); the device session in [`triad_vulkan_gain.cpp`](../src/triad_vulkan_gain.cpp). |
| Drift and varying fine | `score_drift_fine` and `score_varying_fine` in [fine processing](../src/triad_receiver_fine.cpp); the device sessions in [`triad_vulkan_drift.cpp`](../src/triad_vulkan_drift.cpp) and [`triad_vulkan_varying.cpp`](../src/triad_vulkan_varying.cpp). |
| Fine source and ownership | `FineSession` in [fine processing](../src/triad_receiver_fine.cpp) prepares receiver observations and advances tracking and likelihood state. [MappedBasis](../src/triad_receiver_basis.cpp) exports the native mappings. For a backend that renders tiles it supplies each column's public tile address and the keyed coefficients of a lane; for every other consumer it produces and pins immutable keyed source tiles. The session submits a window of ready fixed-affine dispatches and advances each state through them in public order. Candidate sets larger than one GPU submission retain every candidate. |
| OpenCL resident work | The [fine adapter](../src/triad_opencl_fine_bulk.cpp) leases the session and credits its receipt. [Host projection and staging](../src/triad_opencl_nvfp4_projection.cpp) owns the queue, tile cache, tile requests, descriptor flush, uploads and readback. [Device kernels](../src/triad_opencl_nvfp4_kernels.cu) render dense tiles and map them in FP32 arithmetic with an FP64 time base and fixed-point coordinates by default; tiles stay FP64 in the cache, and `--opencl-fine-map fp64` runs the FP64 control. The FP4 path packs templates and coefficients and runs block-scaled E2M1 Tensor Core projection with FP32 accumulation. The [resident FFT](../src/triad_opencl_fine_fft.cuh) uses FP32 arithmetic and gathers the same bins. Both compute FP64 sufficient statistics on the device. The receiver reads those statistics and applies native state updates. |
| Device core | [The Vulkan core](../src/triad_vulkan_core.hpp) and [its implementation](../src/triad_vulkan_core.cpp) hold the shared device leases and receipt, the plan and shader caches and the one compute queue. Only coarse contexts record timestamps. |
| Fallback and receipts | [Vulkan routing](../src/triad_vulkan_backend.cpp) defines public geometry and native terminal, Helper-taper or workspace fallback. [Bulk work](../src/triad_receiver_bulk.hpp) defines what the counters cover. The distinct native CUDA path lives in [the CUDA backend](../src/triad_tensor_backend.cpp) and supports direct cuFFT and NVFP4 controls. Its results qualify that backend and runtime. |

**Writing a Vulkan kernel.** GLSL FP64 has no `sin`, `cos`, `exp` or
`log`: reduce the argument in FP64 or fixed point and evaluate in FP32, as
`drift_dechirp` does. Vulkan bounds FP32 `exp` only to 3 + 2·|x| ULP, so keep
its arguments at or below zero, as the gain rank's two-pass log-mean-exp
does. FP64 runs at about 1/64 rate on the NVIDIA parts and about 1/16 on
Vega, so FP64 kernels pay off only where they remove round trips, barriers
or host passes. A new shader takes an index in its family's kind range of
the core's registry ([device core](#fine-stage-device-ports)).

## Find and qualify a GPU improvement

Use the [abbreviated test policy](robust-current.md#testing-computationally-intensive-robust-changes).
Start from [Where the time goes](#where-the-time-goes), challenging slots
first. After an attribution look, predeclare the smallest useful matched
comparison, and repeat in reverse order when a timing difference is
uncertain. For a routing or arithmetic change, include the severe and quiet
cases, native and applicable GPU controls, and the complete stable message
through two quiet slots. Preserve the severe wrong-bit receipt as an
unresolved reception case. For broader performance or precision claims, use
independent holdouts and backlog, sensitivity and workspace checks within
the same contracts. A predeclared rule must not require a win on an easy
slot: the 21 November gain-rank flip set such a clause aside before its flip
runs. Record any deviation before the runs it affects. When hardware should
help and does not, work through the libraries, dispatch, instructions,
packing, conversions, layout, batching, occupancy, transfers and
synchronization, as the
[precision rules](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
require, before declaring a hardware ceiling. Generic timing rules are in
[Paired timing and load gating](triad-performance.md#paired-timing-and-load-gating);
this section adds the GPU-specific ones.

### Attribute the time

| Question | Instrument | Limits |
| --- | --- | --- |
| Whole slot on the production route | `admission`: one slot through `score_receiver_slot`, the GPU A/B command | Slot fields only, including the stage shares and sub-timers below: no coarse sub-phases and no fine `times`. |
| Coarse sub-phases and fine owner spans | `slot` and `continuation`: `coarse_json` per sub-phase (`filter_setup`, `mapped_fidelity`, `template_bank`, `observation`, `lag_setup`, `bulk_prepare`, `search`, `selection`, `signed_gain_proposal`) and `fine_json.times` | Sub-phases are cumulative over the `coarse_tiles` steps. The coarse stage scores host-prepared columns, about 41 s at 24 workers on the pod and past a 45 s deadline at four, and states no template source; use it for attribution only ([fine stage on the device](#measure-the-fine-stage-on-the-device)). |
| Stream service | `message` and `backlog`: `receiver_service`, `controller_service`, `eof_control` (`message` only), `result.stream` (per-slot score and commit timing, `mean_busy_hardware_fraction`, `controller_thread_cpu_seconds`, `peak_uncommitted_slots`, `backpressure_retries`) and `result.backlog_interval` (`backlog` only) over the middle third of the slots (`service_seconds_per_baseband_second`, `busy_hardware_fraction`) | The backend receipt is cumulative over the process, so a `message` receipt includes the EOF control's work ([timing scopes](validation.md#five-slot-timing-and-measurement-scopes)). A multi-slot run in one process also separates cold plan builds from steady state. |
| Process CPU | Each `phases` entry's `process_cpu_seconds` (process-wide, all threads; busy CPUs are CPU-s over wall); stream runs add `controller_thread_cpu_seconds`, `mean_busy_hardware_fraction` and the backlog interval's `busy_hardware_fraction` | No per-stage CPU time. Bound a stage's CPUs by its lane caps, or difference two slots of equal geometry with and without the stage, as the [cost model](validation.md#cost-model-of-the-cpu-families) did for drift. |
| Stage shares | Slot fields: `helper_seconds`; `coarse_seconds`, which contains Helper when `helper_overlapped`; `varying_coarse_seconds`, the segmented span from first to last call, which can overlap bank and observation preparation; `signed_lag_gain.proposal_seconds`, after the segmented stage drains and disjoint from it; `fine_seconds` with `drift_seconds`, `varying_fine_seconds`, `coherent_gain_seconds` and `varying_quiet_seconds` | The drift and varying sub-timers (`drift_{render,ridge,ridge_search,slope,exact}_seconds`, `varying_{projection,statistics,recursion}_seconds`, `varying_wait_seconds`) are lane-summed, inflated when lanes share hardware threads, and include mutex waits. Busy over busy-plus-wait is lane utilization. Fine `times.*` owner spans do not add. Use shares; decide on slot totals. |
| Work done | Backend counters (`vulkan_<family>_*`, `vulkan_segmented_*`, `opencl_coarse_*`, `opencl_resident_*`, `cuda_*`) and slot counters | `drift_coherent_timing_fft_rows` counts only rank-pass zero-slope dechirps, 12.08 G of 17.18 G large-FFT rows on the HF receipt. Varying fine has no FFT counter. Rebuild totals from plan geometry. |
| OpenCL device intervals | `--opencl-profile on` with explicit `--backend vulkan`: `opencl_coarse_{product,packing,selection,synthesis,render,filter,gram,rescore}_seconds`, on by default for an explicit `nvfp4` coarse search; `opencl_resident_host_{source,staging,transfer,wait}_seconds`; `opencl_resident_device_{transfer,render,map,pack,product,statistics,span}_seconds` | Render, filter and Gram make up synthesis; rescoring is part of selection. Profiling cost about 9 ms of an 82 ms fine session. Not available on `--backend gpu`. |
| Vulkan device intervals | An always-on timestamp pair per coarse command buffer (`vulkan_timed_submissions`, `vulkan_device_command_seconds`); host `wall_seconds` from the submit lock to the fence, summed over owners; `vulkan_fft_plan_seconds`, `vulkan_shader_compile_seconds`, `vulkan_fft_cache_{hits,misses}`, `vulkan_shader_spirv_{compiles,cache_hits}` | Fine-stage contexts record no device time. The only family time is the host `vulkan_gain_rank_rescore_seconds`. |
| CUDA | Host `wall_seconds`, the HMMA and IMMA witnesses and the half-FFT counters | No device intervals. |
| Component rates | `test_triad_vulkan --segmented-benchmark` (`datapump.triad.vulkan-segmented-throughput.v1`, three fixed shapes, indicative), `--gain-rank` (per fixture `device_call_seconds`, band and frontier statistics), `--benchmark` and `--benchmark-cpu-precision`; `test_triad_opencl_nvfp4` | Fixed shapes, not complete service ([GPU builds](building.md#vulkan-and-opencl-gpu-builds)). |
| Kernel resources | The build writes `<build>/generated/triad_opencl_nvfp4/triad_opencl_nvfp4.ptx` for `sm_120a`. `ptxas -v -arch=sm_120a` on it gives registers and spills; record them per qualification (`coarse_warp_fir` 235 and `coarse_fp4_scores` 106 registers, no spills). For FP64-pipe questions, assemble a cubin with `ptxas -arch=sm_120a -o kernels.cubin triad_opencl_nvfp4.ptx` and count `DFMA`, `DADD`, `DMUL`, `F2F.F32.F64` and `I2F` in `cuobjdump -sass kernels.cubin`. | Static counts, not dynamic issue. A new kernel entry needs its row in the [PTX embedding table](../tools/triad_opencl_embed_ptx.py). The build has no query for GLSL kernel resources. |
| Memory | `workspace_peak_bytes`; `configuration.receiver_slot_workspace_bytes` from `plan`; `peak_reserved_bytes`; the host cgroup | `workspace_peak_bytes` is the admitted lease quote, not a measured peak. The `plan` quote is the CPU route's and omits device-route host quotes: gain-rank certification, the larger caches and OpenCL leases. The probe reports no RSS. |
| Shapes no local slot reaches | A microbenchmark at production geometry ([method](triad-performance.md#paired-timing-and-load-gating)) | Like the HF ridge benchmark of the [21 November record](validation.md#fine-stage-device-routes-and-exact-cpu-stages--21-november-2026). |

**The automatic route cannot be profiled directly.** `--backend gpu` rejects
`--device-index`, `--device-memory-limit-bytes` and backend arithmetic
options, and `--opencl-profile` needs `--backend vulkan`. Attribute on an
explicit reproduction (`--backend vulkan --vulkan-precision nvfp4
--opencl-profile on`, with `--vulkan-fine-projection resident-nvfp4` for the
fine stage), which differs from the automatic route
([route table](#what-automatic-selection-runs)), and decide on unprofiled
automatic runs. `tools/triad_native.py` cannot launch `admission` through
`NativeProbe`, but `validate_receipt(command="admission", ...)` checks a
saved receipt.

### Compare routes

- Follow [Paired timing and load gating](triad-performance.md#paired-timing-and-load-gating):
  one binary, any reference built on the same pod with the same toolchain,
  alternated pairs on identical inputs, a predeclared rule and decisions on
  slot totals.
- Use `--capture-cache DIR` for repeated captures. Its key binds the
  executable, libraries and math dispatch, and a repeated run replaces the
  simulator by a verified read: 0.02 s on the 72 Hz/5× control and about
  0.2 s on the holdout
  ([capture identity](channel-models.md#stream-evaluation-workers-and-capture-identity)).
- Alternate the order of native, the three families one at a time and all
  together, for example native, gain, drift, varying, all, all, varying,
  drift, gain, native. Use two pairs per cell on a pod and three locally.
- Explicit device routes against native answer whether the device is faster
  for a stage. The automatic route of a flipped build against the unflipped
  build answers whether the default gets faster. Do both before a flip.
- Record per run the `/result`, evidence, route strings, each family's
  declines and fallbacks, the band ratio, `peak_reserved_bytes` and the
  input hash, with the fields of [step 10](#run-a-bounded-device-qualification).
- Route strings differ by family when native: `varying_fine_route` is
  `native` on every route including CPU; `signed_lag_gain.proposal_route` is
  `native` with a backend and empty on CPU; `drift_proposal_route` is empty
  unless a device session ran. Analysis scripts must handle all three;
  harmonize them before freezing the receipt schema.

### Qualification slots

These arguments make the challenging strata reproducible. Every slot below
uses:

```text
--case hf --encrypt on --helper off --path-loss-db 180 --noise-dbm-hz -164
--timing-policy broad --base-oscillator gpsdo-xo --shift-oscillator gpsdo-xo
--spread-preset hf-worst --selective-perturbation each-65 --channel-model gui --margin 3
--burst-wait-tiles 0 --rf-hz 30000000 --seed 940600 --bit 0 --stratum active
```

| Slot | Role | Further arguments | Input SHA-256 | Memory |
| --- | --- | --- | --- | --- |
| Matched 3.6 Hz severe, quiet, stable and five-slot message | Easy: identity, regression and the defaults-unchanged gate | The fixtures of [Reproduce the matched Blackwell runs](#reproduce-the-matched-blackwell-runs) | `b4d8fa5a…`, `2b9ca9df…`, `eaa4ae2a…`, `4a3603c8…` | Defaults |
| 72 Hz/1× HF Worst | Easy for the families; segmented shapes outside the envelope; near-tie proposal check | `--data-power-w .2 --fhss off --rate 72 --added-symbols 1 --max-samples 40000000 --deadline-ms 900000` | `7e8ef8fa…` | Size it with `plan` at the pod's `W` |
| 72 Hz/5× control | Challenging; device segmented timing; rank-dominated before the gain-rank flip | `--data-power-w .002 --fhss on --rate 72 --added-symbols 5 --added-interleave on --target-cn0 4 --max-samples 40000000 --deadline-ms 1800000` | `af364b67…` | A 9 GB `--memory-limit-bytes` refused the slot at W = 80 and 64 GB worked. Explicit device runs used `--device-memory-limit-bytes 4294967296`. |
| 36 Hz/10× 100 mW holdout, Target −12.9897 | Challenging holdout; drift and varying dominate after the flip | `--data-power-w .1 --fhss on --rate 36 --added-symbols 10 --added-interleave on --target-cn0 -12.9897 --max-samples 26000000 --deadline-ms 1800000` | `8a6c8ab2…` | About 21.5 GB of receiver workspace per owner (`receiver_slot_workspace_bytes` 21,475,814,336 at W = 26). Its coarse session exceeds a 4 GiB explicit device workspace, and 48 GiB explicit plus per-owner leases exceeded a 64 GB host limit at 80 owners, so only `--backend gpu` has completed it ([open measurements](#open-measurements)). |
| HF 100 mW 36 Hz/10× Target −22.5 | Extrapolation shape only: a 3,707 s slot | The [HF recipe](validation.md#reproducing-the-stable-alarm-and-severe-bit-tests) | — | A 144.37 GB host quote at W = 26; 204.57 GB at W = 54 exceeded a 188 GB cgroup |

An easy row cannot veto a challenging-row gain. Do not rerun the HF row
routinely: extrapolate by fixed geometry from the holdout's measured stage
times scaled by block, atom or batch counts, and record the assumptions.
Component timings such as the gain-rank oracle's `device_call_seconds` and
the segmented benchmark depend on their own batch width and include host
staging, so they are component rates, not production rates: the NVIDIA
oracle's HF call took 0.516 s for 2,340 padded blocks in a 512 MiB
workspace, about 0.22 ms per block, against about 0.07 ms per block in the
holdout's 3.05 s stage over 42,141 blocks
([21 November](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026)).
Strip RAM-sized limits from saved argument files and size them with `plan`
for the pod's `W`.

### Run a bounded device qualification

1. **Predeclare before renting:** the question; the routes or families; the
   strata (easy, challenging, holdout); the order and pairs; a pass rule per
   family; the automatic-mode clause; a time budget and stop rule. A pass
   rule names the component tests; the exactness class (identical lags and
   evidence, identical decisions, or evidence within a stated bound); zero
   declines and fallbacks on explicit device runs; a faster stage and no
   slower slot on the challenging slots; and complete flipped automatic runs
   with identical decisions. Stay within the current
   [spending authorization](triad-performance.md#gpu-selection-for-accelerated-development).
2. **Locally:** build, run the emulated OpenCL tests and
   `./build.sh test triad`, add any new device test and its headers to
   [`triad_gpu_bundle.py`](../tools/triad_gpu_bundle.py), check that the
   bundle builds, and run `plan` at the target `W` to size memory.
3. **Identify the device and host:** run
   `nvidia-smi --query-gpu=name,memory.total,compute_cap,driver_version,mig.mode.current --format=csv`,
   and record the CPU model, effective quota and affinity, cgroup memory,
   compiler and NVCC versions. `W` is twice the quota's whole CPUs (the
   floor of quota over period): 80 at a 40.8-CPU quota, 26 at 13.6.
4. **Deploy and build:** use the lab's `--mode gpu --profile simulation`
   and confirm `PUMP_LAB_MODE=vulkan`; a CUDA-selected worker is a different
   route. Build the reference and every candidate on the pod with the same
   toolchain, and record source manifests and probe hashes. Extract an
   updated tree into the deployment with `tar --no-same-owner
   --no-same-permissions` and keep the deployment directory at mode 700, or
   the relay refuses with "Unsafe deployment Vulkan directory". Follow [Reproduce the matched Blackwell runs](#reproduce-the-matched-blackwell-runs)
   for the launcher and filter.
5. **Identity gate:** `test_triad_opencl_nvfp4`,
   `test_triad_opencl_fine_session`, and `test_triad_vulkan` in its default,
   `--segmented`, `--workspace-admission`, `--host-scoring`, `--core`,
   `--gain-rank`, `--drift` and `--varying` modes, plus `ptxas` registers and
   spills. Run filtered tests with standard input from `/dev/null`.
6. **Defaults unchanged:** reference against candidate on `--backend gpu`,
   the matched fixtures in both orders and the 72 Hz slots, with identical
   results, evidence and capture hashes.
7. **Explicit pairs** on one binary with `--capture-cache`.
8. **The holdout** on the automatic route.
9. **Flip:** rebuild with the qualified constant and compare the automatic
   route against the unflipped build.
10. **Close:** keep one summary line per run: exit, status, slot and stage
    seconds (with `helper_seconds` and `varying_coarse_seconds`), the
    phase's `process_cpu_seconds`, route strings, per-family planned,
    declined and fallback counts and reasons,
    `varying_{projection,statistics,recursion,wait}_seconds`,
    `varying_batches`, `varying_batch_blocks`,
    `drift_{render,ridge,ridge_search,slope,exact}_seconds`,
    `vulkan_fft_plan_seconds`, `vulkan_shader_compile_seconds`, band ratio,
    `peak_reserved_bytes` and input hash. Keep the device test outputs with
    UUIDs removed. Before terminating, copy the raw receipts, with UUIDs
    removed, into the ignored `local-evidence/` directory beside the
    summaries, and state their purpose and cleanup trigger in its README.
    Terminate promptly and verify the pod's absence.

Pitfalls:
- Memory limits and the options `--backend gpu` rejects: see the Memory
  column of [Qualification slots](#qualification-slots) and
  [Run and collect](triad-performance.md#run-a-native-simulation-on-runpod).
- Explicit workspaces may not fit the holdout.
- Never overwrite a phase script that a running shell is executing: bash
  resumes at its byte offset.
- A strict `-euo pipefail` script stops at the first failing test.
- Pod CPUs differ between rentals; compare only within one run
  ([on a pod](triad-performance.md#paired-timing-and-load-gating)).
- The first run of a family can be slower than the later ones.

### Decide, flip and record

- The flip constants are `automatic_segmented_qualified` in
  [the Vulkan backend](../src/triad_vulkan_backend.cpp),
  `automatic_gain_rank_qualified`, `automatic_drift_qualified` and
  `automatic_varying_qualified` in `src/triad_vulkan_{gain,drift,varying}.cpp`,
  and the `VulkanReceiverOptions` defaults `opencl_fine_fp32`,
  `opencl_coarse_fp32_rescore` and `opencl_coarse_one_ray` in
  [`triad_acceleration.hpp`](../include/datapump/triad_acceleration.hpp).
  Each applies only to the OpenCL-identified SM 12.0 class; a new class needs
  its own identification and qualification.
- Record in [validation](validation.md): the device, host CPU, quota, memory,
  `W`, toolchain, hashes, predeclared rules, deviations, tables and limits,
  and the evidence directory with its purpose and cleanup trigger.
- Update in the same change: the [route table](#what-automatic-selection-runs)
  and [Where the time goes](#where-the-time-goes); the precision guide's
  route sections; the probe's help text; the receipt rules of
  `tools/triad_native.py` and their mirror in
  `tests/test_triad_abbreviated.py` if receipts change; and the
  [Robust index](indexes/robust.md) row.
- Keep provider identifiers, prices, pod names, UUIDs and private paths out
  of the repository.

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

For scale, use the [prior 576-lane matrix-only and large cuBLASLt controls](#same-card-resident-and-staged-controls).
The latter reached 1.309 PFLOP/s in a large dense synthetic
multiplication, compared with a roughly 2 PFLOP/s dense hardware reference.
The severe slot's 404 paired submissions and one single submission issue
137.424 GFLOP at `N=96` and `K=3072` padded. Applying the old already-packed
9.22 µs paired and 8.93 µs single event medians to that schedule gives about
3.7 ms of matrix time as an illustrative fixed-shape estimate. The severe
fine span was about one second in that campaign and 0.54–0.69 s on the
20 November host before device tiles. With
[device tiles and windows](#fine-seconds-on-the-device) (20 November) the
same products took 2.0 ms of profiled device time in 51 submissions; the
fine stage is now about 0.04 s of the matched slot. Issued-equivalent
throughput over fine-stage wall time, active matrix throughput, and Tensor
Core duty are separate measures. The shared-bit mapper of 19 November
removed 107,928 descriptors per severe or quiet slot, saving 18,995,328
bytes while the blocking-write counts and tile miss counts stayed the same;
that matched improvement combined mapping, host-side changes and coarse
phase reuse.

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
arithmetic throughput estimates from operation counts and elapsed time. At
that time the higher-allocation default used native fine processing on the
tested geometry, with Vulkan processing millions of coarse rows. The four-worker wall difference
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
| ---: | ---: | ---: | ---: | ---: | ---: |
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

This section records the 19 November route: 176-byte descriptors, 128 KiB
tile uploads on a miss and two-block submissions. The current route uses
64-byte descriptors, 16-byte tile requests and windows of 16
([device tiles](#device-rendered-fine-tiles-and-resident-windows)).

The socket-filtered RunPod worker then had two explicit developer-probe routes
for the same isolated DATA geometry. Both use the canonical native mapping
source and a bounded per-owner device cache of 64 immutable waveform tiles.
Normal columns sent a 176-byte affine and phase descriptor. A tile miss sends
128 KiB of source samples and a direct native mapping sends one C64 column.
Mapped samples and projected modes stay on the device. A blocking read returns
176 bytes per block and candidate, with sufficient statistics for both bits
and the nonzero-basis marker. Observation modes and initial bin indices are
included in the transfer receipt. Device buffers, reusable host staging and
the source allocations retained by cache identities are included in admission.

The receiver submitted two compatible ready blocks at a time and still applied
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
phase for a tap. The resident OpenCL mapper of 19 November sent one 176-byte descriptor
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
control for a 576-lane projection. Device intervals were measured later:
active matrix time was 2.0 ms of profiled device time in 51 submissions.
Only Tensor Core duty, which needs hardware counters, remains unmeasured.

[Measured receiver service](validation.md#blackwell-shared-bit-mapping-and-coarse-phase-reuse--19-november-2026)
· [Open measurements](#open-measurements).

### Reproduce the matched Blackwell runs

Start with [Current GPU limits and next steps](#current-gpu-limits-and-next-steps)
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
information without provider identifiers. Record the CPU model, effective
affinity and quota, and expanded compiler and toolkit versions alongside the
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
the wanted bit for `admission`. Four and 24 software workers are the two
measured allocations of the automatic route.
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
single batches. Six additional terminal blocks ran natively. With the dense
coarse search the automatic stream credited the same coarse rows, 357,984
fine statistics, 67,318,272 projected values and 67,219,776 resident FP4
modes in 2,486 batches with 2,480 pairs. Require
`complete_receiver_service`, `message_and_physical_quiet_verified` and
`eof_control_verified` on the message, with an expected-source match and
two trailing scored quiet slots.
The EOF-only control remains incomplete. Single-slot decisions must match
across all six admission policies on these matched inputs. The dense search
met that. On twelve other severe seeds two inputs differed in one policy
outcome each, so compare further seeds as paired evidence, not as identity. Current and Guarded recover stable bit 0.
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

## Dense FP4 coarse search and device template bank

The matched broad search has 78 workspaces, 15 trial frequencies, 3,377
arrivals and two rays over 44,971 projected rows: 3,951,090 score rows and
1.42e12 real products per slot. The earlier route prepared 156 template
columns on the host, uploaded 82 MB, and transformed them on the device.
Host preparation was 59–90% of a slot. The dense search moves the whole
stage to the device and makes the products a Tensor Core contraction.
[Session](../src/triad_opencl_nvfp4_coarse.cpp) ·
[kernels](../src/triad_opencl_nvfp4_kernels.cu) ·
[receiver adapter](../src/triad_opencl_coarse_bulk.cpp) ·
[measured service](validation.md#blackwell-dense-fp4-coarse-search--20-november-2026).

**Formulation.** For one trial frequency the observation is rotated once and
packed as E2M1 with its real and imaginary parts interleaved, so an arrival
is one byte and the rows of one `m16n8k64` product are sixteen consecutive
arrivals of the same packed stream. Block scales cover eight samples, so
each frequency keeps eight packed variants and an arrival reads the one that
matches it modulo eight. A template column is packed twice, as `[Re, Im]`
and `[-Im, Re]`. The eight result columns are then the real and imaginary
correlations of four template columns: a bank tile of two workspaces of two
rays, or of four workspaces of one ray. One block scores one frequency, one
bank tile and 512 arrivals. The FP32 quadratic score of every arrival is
formed in the epilogue, from each ray's partner lane for two rays, and for
one ray as the scalar inverse Gram times the correlation's energy, with no
partner lane. The exact
arrival frontier of those scores is selected on the device, with the native
separation rule. Each retained arrival is then rescored in FP32 with FP64
accumulation from the unquantized resident bank. `--opencl-coarse-rescore fp32`
instead adds FP32 products in FP32 partial sums of 64 rows, each added in
FP64 as the FIR adds its taps, with an FP64 tree over the block; its
precision labels and reasons say `fp32_partial`, and receipts count the
peaks of each accumulation. Both rescore the same FP4 frontier by
construction. In emulation the partial sums' scores were within 1.8e-7 of
each row's best score of the FP64 accumulation on 14 synthesis geometries.
On the device the partial sums cut rescoring from 2.64 to 0.89 ms per
matched slot with equal decisions and bits, so they are the default
([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)); `--opencl-coarse-rescore fp64` keeps the control.
Rescoring reads one FP32
rotation of the observation per frequency of the tile, which the device
writes once with the statements the rescoring loop formerly evaluated for
every arrival and row. Only the 40-byte frontier of each workspace and
frequency is read back. FP4 therefore decides
which arrivals are kept. It never supplies a score the receiver uses.

**Device template bank.** The exact route's bank is a function of the keyed
chip coefficients, the pulse table and public geometry. The session uploads
those, renders each lane's integer source raster, applies each column's cubic
clock warp and echo rotation, runs the canonical decimating FIR and returns
FP64 Gram entries. The columns match the native exact columns within 6.1e-7
of each column's peak. Rendering the raster before the warp is necessary:
evaluating the waveform directly at warped positions differed by 8.5e-4,
because the native route interpolates a raster whose untapered pulse has
small steps at its support edges. Synthesis covers the exact frequency-tile
route with one or two rays; a one-ray bank renders, warps, filters and forms
scalar Grams with the same kernels, and in emulation its columns are bit for
bit the first-ray columns of the two-ray bank. Automatic selection admits
one-ray sessions with `opencl_coarse_one_ray`, on since its device
qualification; see [one-ray admission](#one-ray-admission-and-dense-cost).
Mapped, varying-passband and midpoint-gain sessions, fewer than 16 arrivals
and leases that do not fit keep the Vulkan FFT session through the
adapter's fallback; lag-group and compact tight-clock sessions run the
native CPU search (below). The receiver states which of its conditions
withheld the synthesis offer, the adapter adds its own reasons, and
receipts count each fallback once in `opencl_coarse_fallback_counts`:

| Reason | Withheld or declined by |
| --- | --- |
| `lag-group-route` | The lag-group route, or a route still open. Not reached through the Vulkan backend, which sends lag-group sessions to native search before a session exists and never opens a deferred route; kept for other callers. |
| `mapped-profile` | A mapped source profile (source step above one). |
| `midpoint-gain` | Midpoint gain timing, which implies a varying passband. |
| `varying-passband` | The varying-passband differential search. |
| `fine-cell-limit` | More than 100,000 fine cells. |
| `description-storage` | Keyed coefficients that exceed the native bank storage they borrow. |
| `device-templates-off` | `opencl_device_templates` off. |
| `few-arrivals` | Fewer than 16 arrivals on an automatic route. |
| `one-ray-unqualified` | A one-ray session on an automatic route without `opencl_coarse_one_ray`. |
| `dense-cost` | A one-ray automatic session above the measured dense work per workspace and frequency. |
| `keyed-chip-limit` | More than 64 MiB of keyed coefficients. |
| `kernel-extent` | A shape outside the kernels' 32-bit extents, or not one or two rays. |
| `workspace-lease` | A session lease above the device workspace limit. |
| `host-allocation` | A host allocation failure while the session considered synthesis. |
| `device-allocation` | A device that could not allocate the leased buffers. |
| `ill-conditioned-template` | A two-ray Gram below the 1/16 guard. |
| `unoffered` | Native columns with no stated reason. The receiver always states one, so only other callers reach it. |

Compact tight-clock and lag-group sessions of the automatic Vulkan backend,
and sessions whose Vulkan FFT plan does not fit, never reach the dense
session; `native_coarse_precision_sessions`, `native_coarse_lag_sessions` and
`native_coarse_workspace_sessions` count them.

**Conditioning.** FP4 scores choose the arrivals that are rescored, and the
quadratic form amplifies their error as a workspace's two rays approach
collinearity. A one-ray Gram is a scalar with ratio one, so the guard never
declines it. A session therefore leaves the dense route when any workspace's
smaller Gram eigenvalue is below one sixteenth of the larger, the canonical
reduced-precision guard. The device returns the Grams with the synthesized
bank, so the check costs one synthesis before the session prepares native
columns for the FFT route. An explicit request reports the bank instead. In
emulation, with a planted arrival at an exact score near 60 in noise, the
frontier kept it in 24 of 24 trials at a ratio of 0.27 and of 3.5e-3, in 22
of 24 at 5.5e-4 and in 1 of 24 at 1.4e-4, and a complete session with a
5 µs, 0.001 Hz spread lost a signal the FFT session found. The matched
fixture's ratio is 0.999996. The guard and these trials were not run on a
device, and a measured FP4 bound could be lower than 1/16.

**Transfers.** With 24 workers a slot uploads 473,952 bytes and reads back
49,920 in 13 blocking transfers, against 82,178,320 uploaded before. The
warp sets add one transfer of 672 bytes, 42 sets of four column indices. With
four workers the plan scores the 15 frequencies as four tiles of at most
four: 474,048 bytes and 22 transfers. Scoring host-prepared
columns with the same FP4 products left the coarse stage at 1.6–1.8 s with 24
workers and 4.3–4.6 s with four: bank preparation on the host was the cost,
not the FFT.

### One-ray admission and dense cost

The one-ray epilogue and session pass the kernel emulation and the device:
columns within 3.9e-7 to 4.2e-7 of their peak against the native exact
columns on the RTX PRO 6000, FP4 scores correlating at 0.9957 with FP64,
every strong planted arrival kept, and complete one-ray sessions reaching
the native search's winner. Against the FFT session it replaces, a 36 Hz
Spread None fixture (423 arrivals, 641 rows, 3 frequencies) kept every
decision and bit in seven seed pairs while its coarse stage fell from
0.19–0.42 s to 0.012–0.013 s at 24 workers and from 0.34 s to 0.013 s at
four. The 3,377-arrival GPSDO/IC-7100 3.6 Hz plan took 27 s on the dense
route against 94 s on the FFT route, and 19 s with FP32 partial rescoring
([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)).
Automatic mode therefore scores one-ray sessions (`opencl_coarse_one_ray`,
on by default; `--opencl-coarse-one-ray off` with `--backend gpu` keeps the
FFT route and counts `one-ray-unqualified`). An explicit `nvfp4` request
always scores them.

FP4 selection keeps the two best arrivals of each workspace and frequency,
so its risk is a weak true arrival near the noise maximum. The component
test's predeclared sweep plants one arrival per workspace in disjoint
stretches of unit noise, 150 rows and 512 arrivals, at noiseless scores of
24, 16, 12, 10 and 8, twenty trials per level and ray count. Its rule: FP4
keeps every arrival that the exact FP64 frontier keeps at 24 and 16, and at
least 90% over the sweep. In emulation FP4 kept 91 of the exact frontier's
93 for one ray, losing one at 10 and one at 8, and 83 of 83 for two rays,
and for each ray count kept one arrival that the exact frontier did not.
A 512-arrival row's noise maximum is about 6.8 for one ray and 8.9 for two,
and the matched search's about 16 and 18.5, so the sweep reaches below
arrivals that could win a complete search. It is component evidence, not a
receiver sensitivity or device result.

Dense products grow with arrivals × template rows for each workspace and
frequency, while the FFT route grows with (rows + arrivals) log(rows +
arrivals) plus host bank preparation; both scale with workspaces and
frequencies. With one-ray admission on, an automatic session also declines
as `dense-cost` when 4 × arrivals × rows exceeds the dense products per
workspace and frequency of the device-measured matched search, 8 × 3,377 ×
44,971 = 1.21e9, where the dense route took a twentieth or less of the FFT
session's coarse seconds. The bound is not a cost model: replacing it with a comparison
against an FFT estimate from `correlation_fft_rows` needs one profiled slot of
each route on a one-ray plan near it. The two-ray admission predates this
bound and keeps its qualified envelope, which includes unmeasured larger
plans such as HF Worst at 3.6 Hz, 7,036 arrivals and 93,614 rows. The dense
session also inherits the frequency tile width of the Vulkan FFT plan, so a
wide prior with a narrow tile is scored in many blocking rounds; a dense
tile width chosen independently would change the receiver's host quote and
needs the same measurement.

One-ray plans of the GUI default spread show the range (CPU probe `plan`, HF
case, 2 mW, broad timing, 78 workspaces):

| Clocks, rate, RF | Projected rate | Arrivals | Rows | Frequencies | Automatic route with one-ray admission |
| --- | ---: | ---: | ---: | ---: | --- |
| GPSDO, 3.6 Hz, 30 MHz (the matched fixture with Spread None) | 3 Hz | 43 | 631 | 15 | Native compact tight-clock FP64; never reaches the dense session |
| GPSDO, 36 Hz, 30 MHz | 30 Hz | 423 | 641 | 3 | Dense |
| GPSDO, 72 Hz, 30 MHz | 60 Hz | 845 | 822 | 2 | Dense |
| Crystal base, GPSDO shift, 3.6 Hz, 30 MHz | 5 Hz | 71 | 1,005 | 789 | Dense |
| GPSDO base, IC-7100 shift, 3.6 Hz, 30 MHz | 240 Hz | 3,377 | 44,966 | 134,690 | Dense, about 6.4e15 useful multiply-adds per slot |
| Crystal, 3.6 Hz, 1 MHz | 1 kHz | 14,071 | 187,194 | 449,761 | FFT (`dense-cost`); dense would issue about 3.7e17 |

The matched fixture with Spread None stays on the native route, so the gain
estimated for one-ray sessions does not occur there. The small plans have
little host preparation to remove. The IC-7100 plan took 27.1 s dense
against 93.7 s on the reference FFT route, with the same decision: FP4
products took 11.3 s, close to the estimate of roughly ten seconds, and FP64
rescoring 12.7 s. FP32 partial rescoring brought the coarse stage to 19.2 s,
so rescoring is the lever for large one-ray plans
([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)).

### FP4 product throughput

Device event intervals on a full RTX PRO 6000 Blackwell Server Edition, with
a multiply-add counted as two operations and the rounded 1.9–2.0 PFLOP/s
dense reference:

| Product kernel and shape | Issued throughput | Interval |
| --- | ---: | ---: |
| First layout, matched search | 426 TFLOP/s | 7.08 ms |
| First layout, operands repeated to stay in L1 | 478 TFLOP/s | 6.30 ms |
| First layout, scale loads and shifts hoisted | 699 TFLOP/s | 4.32 ms |
| First layout, every load hoisted: products only | 1,728–1,759 TFLOP/s | 1.75 ms |
| Ring layout, matched search | 1,215–1,223 TFLOP/s | 2.47–2.48 ms |
| Ring layout, matched search inside the receiver, 24 workers | 1,138–1,155 TFLOP/s | 2.62–2.66 ms |
| Ring layout, the same search as four frequency tiles, 4 workers | 936–953 TFLOP/s | 3.18–3.23 ms |
| Ring layout, 16,384 arrivals | 1,253–1,256 TFLOP/s | 11.0 ms |
| Ring layout, 390 workspaces | 1,287–1,290 TFLOP/s | 11.7 ms |
| Ring layout, one frequency or 4,096 rows | 917–928 TFLOP/s | 0.22–0.30 ms |

The standalone rows used 44,935 template rows. The receiver's bank has
44,971, padded to 45,120. The three hoisted rows are diagnostics that compute
wrong products. They
show that the first layout was limited by its instruction mix and not by
memory or by the product layout: ten 4-byte observation loads, three scale
loads with shifts, and three bank loads fed four products per K=64 slice.
A load cost about half a product's issue time. The ring layout removes most
of them. A thread's fragments are alternate words of its stream, group `g`
of a slice takes four of them from the `2g`-th on, and the next slice starts
four later. Each thread slot has its own stream of those words. A ring of
five quads holds a slice while one 16-byte load brings the quad the next
slice adds. Ready scale vectors advance the same way in one 8-byte load,
and one 16-byte bank record carries a slice's two operand words and its
scales. Three loads now feed eight products. The arithmetic and its results
are unchanged: the receiver's operational evidence was bit-identical on 18
repeated inputs before and after. Useful work is 94% of issued for the matched search. The rest is
the padded inner extent and the arrivals of the last block.

The earlier fine projection's matrix-only control was 36.9 TFLOP/s and the
large cuBLASLt multiplication 1.309 PFLOP/s. The dense search runs real
receiver work at about 60% of the dense reference during its product
interval, and about two thirds of what the same block issues with no loads.
That interval is 2.6 ms of a slot. About 3.0e12 issued operations per
matched slot, 2.6 ms of products at 1.14–1.16 PFLOP/s, over the
0.060–0.068 s automatic slot average about 44–50 TFLOP/s. Raising that figure
is a matter of the remaining host and round-trip costs below, not of the
product kernel.

### Remaining costs after the dense search

The matched fixtures are the easy case; the challenging slots come first
([what bounds them](#what-bounds-the-challenging-slots)). In measured order
for the matched fixtures:

1. **Fine processing: about 0.04 s of a 0.060–0.068 s automatic slot at 24
   workers** on the EPYC 9335 host of the
   [offload qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026).
   It was 0.37–0.94 s before [device-rendered tiles and resident windows](#device-rendered-fine-tiles-and-resident-windows)
   removed the host tile rendering and seven of every eight round trips. The
   FP32 mapper is now the default (24.3 → 5.1 ms) and the fine device span
   fell from 46.0 to 34.7 ms. Its [remaining costs](#remaining-fine-costs)
   are the owner's wait for each window and its own address and staging
   work.
2. **The simulator: 0.50–0.73 s per simulated slot on the EPYC 9554 host
   and 0.19–0.35 s on the EPYC 9335 host, at 12–24 workers.** The first is
   the host of the
   [exact speedups record](validation.md#exact-receiver-and-simulator-speedups--20-november-2026),
   against 0.89–1.94 s for `7ed007dc` and 2.5–4.4 s for the single-threaded
   simulator of `c02b726e`, with every capture hash kept. Source readers,
   a separate transmit oscillator job and a sequential pass overlapped with
   the next block's preparation replaced the one-thread source chain. The
   x87 rounding-mode switches that `a4dfb0cc` removed from the sample loops
   are the likely explanation of a 40% difference between two binaries with
   equal simulator objects.
   [Mechanism](triad-performance.md#code-placement-and-x87-rounding-mode-switches).
   On the Ryzen 5 PRO 5650U the severe slot takes 0.56 s with twelve
   workers, against 1.06 s before and 5.0 s for `c02b726e`. The simulator
   still takes about four times the automatic receiver slot. A section-timed
   copy found the receiver oscillator chain bounding the stable and message
   captures and the pass followed by a rows job bounding the severe one; the
   staged oscillator and noise chains and the shared row pool remove those
   dependencies without changing a sample. On the quiet 5650U they cut the
   simulator phase about 10% on the severe capture at twelve workers and 7%
   on the quiet capture, with no established gain on the stable capture or at
   four workers. On the pod's EPYC 9335 the simulator was within run-to-run
   noise between the reference and the candidate with these chains,
   0.19–0.33 s at 24 workers
   ([offload qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)).
   For repeated receiver runs of one probe binary, `--capture-cache`
   replaces the simulator by a verified read of the same capture. The
   proposals that change sample values are measured beside the channel
   contracts.
   [Channel contracts, changes and proposals](channel-models.md#stream-evaluation-workers-and-capture-identity) ·
   [earlier measurements](validation.md#simulator-throughput-and-capture-identity--20-november-2026).
3. **The coarse stage: 0.020–0.024 s at 24 workers on the final tree**
   (0.021–0.031 s on the candidate before the FP32, one-ray and segmented
   defaults flipped). Template synthesis took 62 ms on
   the device, 52 ms of it in a scalar FP32 FIR over stored raw columns. The
   fused FIR and the lane-independent restructure below brought lane
   rendering to 1.9 ms and the warp and FIR to 7.2 ms, now the largest
   synthesis part. The FP4 products take 2.6 ms and rescoring 0.89 ms with
   FP32 partial sums. The remainder of the stage is host work that was not
   split.
   [Device comparison](validation.md#exact-receiver-and-simulator-speedups--20-november-2026).

   A 128-thread block filters one 128-row tile of a column. It stages a
   window of taps and the raw span that its rows read in a static 47 KiB
   shared stage of 6,016 complex pairs. Each row keeps the scalar FIR's
   operations and order: FP32 partial sums of 64 taps from its own first
   tap, each added in FP64. The geometry selects the route:
   - *Warp in the FIR* when every tap fits one window beside the tile's
     span, at half a pair per tap: 127 D + 1.5 × taps ≤ 6,016 at
     decimation D. The canonical plan's 68.14 D + 1 taps meet that through
     D = 26, including the matched D = 25 with 1,705 taps. A block warps
     the raw rows that it stages with the stored warp's FP64 expressions.
     No raw column is stored, and a raster group takes two dispatches
     instead of three.
     Neighboring tiles overlap by taps − D rows, so the matched shape warps
     each raw row about 1.53 times. `opencl_coarse_filter_seconds` includes
     that repetition, while the warped-sample receipt counts each raw row
     once.
   - *Tiled FIR over stored raw columns* from D = 27 to 47, in two or more
     windows of taps: two at D = 32. Windows narrow to 31 taps at D = 47,
     where each window stages about 1.5 raw pairs per multiply-add.
   - *The scalar FIR in place* from D = 48, where 127 D fills the stage. It
     uses no shared memory and keeps its occupancy.

   The stage leaves room for at most two resident blocks, eight warps, per
   SM of a compute capability 12.0 device; at the matched shape the fused
   route still took 8.7 ms against 55.9 ms for the stored warp and scalar
   FIR. The matched slot's dispatches fell from 46 to 34 with 24 workers
   and from 58 to 49 with four: 13 raster groups lose their separate warp,
   and each frequency tile adds one rotation. Rescoring reads that rotation
   instead of evaluating a sine and cosine per arrival and row, and
   selection fell from 4.7 to 2.7 ms. Evidence and decisions on the device
   were identical to `7ed007dc`. In emulation the banks, Grams, FP4 scores
   and rescored frontiers of fourteen synthesis geometries were
   byte-identical too, covering all three routes and their limits at
   D = 26, 27, 47 and 48. That identity relies on NVCC's default
   `--fmad=true` contracting each `partial += weight * value` and the
   rotation to the same FFMA in the old and new kernels, so a change of
   those flags needs the device comparison again. Near D = 47, compare the
   tiled and in-place FIRs before relying on the tiled route's narrow
   windows; a smaller stage with more resident blocks was not tried.

   Both kernels then stopped repeating lane-independent FP64 work. A raster
   group holds four lanes, two epochs, and one thread renders a sample of
   all four: its time, envelope, chip support, pulse weights and carrier
   rotation once, then each lane's FP32 chip sums in their own order. The
   columns of a group that share a (clock, ray) map form sets of up to four.
   A fused FIR block filters one tile of every column of a set: each thread
   evaluates the FP64 position and echo terms of its staged rows once,
   keeps them in registers and stages each column's span from them, and the
   stored-raw route warps a set's columns from one evaluation the same way.
   In emulation the banks, Grams, FP4 scores and rescored frontiers of 14
   geometries were byte-identical to `b3c7c48f`: the fused, tiled and in-place
   routes at D = 4, 25, 26, 27, 32, 47, 48 and 50, continuous, resampled,
   gated and burst sources, three epochs, and shuffled columns with repeated
   lanes and sets wider than four. The device test also requires a column
   that shares its map with other lanes to filter to the same bits as when
   it is alone with that map, fused and stored. The same FFMA assumption
   applies. At the matched geometry the FP64 work of lane rendering and of
   the warp falls to 7 of 26 lane renders and 42 of 156 column warps.
   On the RTX PRO 6000 lane rendering fell from 6.9 to 1.9 ms and the warp
   and FIR from 8.6 to 7.2 ms, with identical banks, evidence and decisions
   against `b3c7c48f`; dispatches fell from 34 to 22 at 24 workers
   ([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)). The wider raster adds
   18 MB to the session.

   The stash holds 47 rows of three 32-bit terms per thread, about 141
   registers beside the FIR's, and stays in registers only if the compiler
   unrolls the staging loop and inlines it and the FIR; both are marked
   `__forceinline__` with constant stash indices. A Clang 19 NVPTX build
   kept it in registers that way, but put it in a local frame of about 600
   bytes whenever the staging was not inlined, as a lambda or an unmarked
   functor. Lane rendering's 17 pulse weights likewise left local memory
   only once their loop had a constant trip count. NVCC 12.8's `ptxas` for
   `sm_120a` gives `coarse_warp_fir` 235 registers and no spills, and
   `coarse_fp4_scores` 106 registers and no spills. A global warp
   table instead of the stash was tried in emulation and was also
   byte-identical, but at the matched 1,122,562 raw rows it needs 18 MB per
   (clock, ray) map, so a group's six maps need six table and FIR dispatch
   pairs within a bounded table; reconsider it only if the stash spills.
4. **Separate processes sharing the device.** Twelve simultaneous trials
   averaged 3.0 s per receiver slot against 0.45–0.81 s alone. Owners inside
   one process overlap on the device without that cost: the five-slot
   message took 1.05–1.17 s.
5. **The arrival frontier.** Two arrivals per row are retained by FP4 score
   before rescoring. On twelve severe seeds that changed one policy outcome
   on each of two inputs, one of them to the wrong bit under the current
   policy. Retaining more arrivals before the FP32 pass would cost little,
   would bring the retained set close to that of exact scores, and may let
   the conditioning guard relax. It needs device requalification.
6. **The product kernel.** Three loads per slice remain. Two workspace pairs
   per block would share the observation loads.

A 12-slot matched backlog was measured on 20 November: receiver service
fell from 0.831–0.835 to 0.706–0.708 s at 24 workers. Sessions the dense search declines,
the automatic plan's use of the FFT route's memory quote, RTX 5090, wider
priors, longer and challenging-case backlog and multi-owner device behaviour
remain unmeasured.

### Where FP4 fits, and the larger levers

A survey of receiver and simulator work on 20 November 2026 tested whether
most numerical work could use the FP4 Tensor Cores
([record](validation.md#gpu-offload-survey-and-gated-device-routes--20-november-2026)).
It cannot.

**Where block-scaled E2M1 fits.** Only large post-isolation contractions
whose output just chooses candidates that are then rescored or tracked
exactly. Both such contractions already run in NVFP4:
- the coarse frontier products;
- the fine 47-mode projection.

Together they took 4.6 ms of the roughly 60 ms of device time in the
matched slot when surveyed, before the FP32 kernels and the
lane-independent restructure.

**What does not fit.** A low-precision matrix form would change correctness
or receipts, or the work has no matrix to contract:
- the simulator: draws, phases and samples in recurrences, with no
  contraction;
- raw-composite FIRs and projections before isolation;
- template rendering, warp and mapping (elementwise, or a GEMV with N = 1);
- rescoring;
- Gaussian states and likelihoods;
- the codec and decisions.

**Device lever: FP32 per-row arithmetic.** In that survey about 44 ms per
matched slot was FP64 SIMT, which runs at 1/64 rate on Blackwell and on the
Ada targets:
- fine mapping, 24.5 ms;
- lane render, 7.0 ms;
- fine tile render, 6.1 ms;
- the warp's FP64 share, about 3.5 ms;
- rescoring, 2.7 ms.

The fix is FP32 per-row arithmetic with FP64 or integer coordinates, as the
qualified CUDA `fp32-rn4-v1` mapper does. The FP32 mapper, renderer and
partial rescoring now do this by default beside their FP64 controls: on the
RTX PRO 6000 the mapping fell from 24.3 to 5.1 ms, tile rendering from 6.0
to 2.5 ms and rescoring from 2.64 to 0.89 ms per matched slot
([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)),
and the lane-independent restructure brought lane rendering to 1.9 ms and
the warp and FIR to 7.2 ms. No current per-slot device total is recorded.
The FP64 that remains is mostly the coarse warp and lane render's time base
and the fine renderer's support work. This also matters for the RTX 4090,
4090 Laptop and 4060 Ti, which have no FP4 MMA.

**The larger costs are on challenging geometries**, where the survey found
FFT-structured native CPU work on every route
([survey figures](validation.md#gpu-offload-survey-and-gated-device-routes--20-november-2026)).
Dense forms of that work do about 0.4·n/log2(n) times the FFT's
operations, about 140× at n = 4,096, which cancels FP4's advantage except
at small n, and then only for parts that just propose candidates. Their
current state and the ranked levers are in
[What bounds the challenging slots](#what-bounds-the-challenging-slots),
[Next levers](#next-levers-in-order) and
[Rejected and conditional approaches](#rejected-and-conditional-approaches).

### Varying fine block batches

`score_varying_fine` no longer runs one executor wave per short block. It
scores batches of consecutive public blocks in three phases on the same lanes:
- **P**: project each block's observation, remove the line, and form its
  transitions and gain correlation;
- **S**: render, project and transform every family item's block statistics
  into the batch storage. Leaves run chunk by chunk (about 2,048 rows each),
  gain atoms first (16 FFTs per block), then fixed-affine states (8), then the
  tracker (4). Each chunk's first leaf fills the next chunk's basis tiles, as
  `FineSession` does. Filling the first and last blocks is enough: a chunk is
  at most 2,048 rows, no longer than one 8,192-sample tile at the 4x source
  rate. Filling every block of the next chunk on one leaf cost about 3 ms per
  chunk on the 48-row fixtures; reconsider only for chunks longer than a tile;
- **R**: one leaf per recursive chain (Gaussian updates, tracker scores,
  coherent posteriors, emissions, Markov recursions), in block order.

The stage is native FP64 on every automatic route, so the change reaches CPU, Vulkan
and CUDA builds alike. Every output of `score_varying_fine` is bit-identical:
each statistic and recursion keeps its per-element operations and order, and
a batch of one block on one lane is the old schedule. Tests compare batches
of 1, 3, 64 and all blocks on 1, 2 and `W` lanes against it. They cover span
and clock-shadow hooks, midpoint and signed-lag atoms, coherent gain, line and
Helper projections, the central independent-gain fallback (an empty projected
lattice near Nyquist), mid-batch failure and a stop inside a running batch.
Equality with the pre-batch code was checked once, bit for bit, with a scratch
digest of those scenarios built against both versions. The 72 Hz/1× slot's
probes (Vulkan build with native segmented timing, and CPU) match the pre-change receipts
except for the new fields; CUDA builds were not run.

The batch is charged in `varying_fine_plan` at the public family maxima. K is
the most blocks whose non-midpoint storage fits 16 MiB, at least one and at
most 256 or the slot's block count. The midpoint family then adds six
coordinates to each block. The scorer refuses a shape above the quoted
per-block bound. On the 72 Hz/1× slot a batch holds 52 of the 163 blocks, and
`workspace_peak_bytes` does not change: a larger stage bounds that lease.

The charge is real memory for the whole scorer call, so admission can change
although every score is unchanged. Where a varying quote is the largest
stage, the owner lease rises by up to K times the per-block bound, less the
removed observation buffers.
Drift's preserved stage would rise with it on the expanded-proposal path. A
quote-only sweep compared 764 public shapes at 1, 2, 4, 8 and 24 workers with
the pre-batch build. The lease rose on about a tenth of the quotes, all light
shapes where varying is the largest stage: by up to 18.3 MB, and at most 26%.
Every drift plan was unchanged by the batch alone. The bounded drift
widening ([Drift proposals](#drift-proposals)) can widen an ordinary drift plan
into this larger varying stage without raising the lease. No expanded-path
quote changed its lease; a larger stage bounds those, as the pinned long-calm
leases show. Under any binding memory limit, an explicit
`--memory-limit-bytes` or the default derived from available RAM
([CPU allocation](development.md#receiver-cpu-allocation)), a light shape can
therefore admit fewer owners or narrower balanced shapes. Lower
`maximum_varying_batch_bytes` if that matters more than fewer barriers.

Receipts:
- `varying_workspace_bytes`, `varying_batch_blocks` and `varying_batches`;
- the lane-seconds of each phase, `varying_{projection,statistics,recursion}_seconds`;
- `varying_wait_seconds`, idle time of the lanes that ran a leaf, from each
  lane's first leaf to the end of its phase. Lanes held by other owners are
  not counted.

A slot's receipts cover its ordinary, coherent-gain and line-conditioned
varying stages. Their seconds and batches add; the quote and blocks per
batch are the largest stage's. `varying_fine_route` and
`varying_fine_arithmetic` describe the ordinary stage, the only one a
device can serve.

Busy over busy-plus-wait is the stage's lane utilization. Lanes are software
workers, not hardware threads.

`ReceiverSlotScore` grew from 1,912 to 2,232 B (GCC 14, x86-64), by 320 B:
- these batch receipts, 56 B;
- the drift section timers, 40 B;
- the fine-stage route fields, 224 B: the varying route and arithmetic
  (64 B), the signed-lag route, device and rescoring row-stages, rescored
  lags and top-two gap (64 B), and the drift route, device atoms and retained
  digest for the ordinary and line-conditioned stages (96 B).

With spread Doppler the drift quote charges a score copy
(`sizeof(ReceiverSlotScore)`), so a lease that drift bounds rises by that
320 B.

The explicit Vulkan varying route moves phase S's template projections to
the device ([Varying statistics](#varying-statistics)); rendering stays on
the host. The R recursions, chiefly log-domain Markov updates, are the next
CPU lever; scaling them changes arithmetic and needs its own qualification.

### Reproduce and extend the dense search

Use the [matched procedure](#reproduce-the-matched-blackwell-runs) with the
same fixture, launcher and stock worker environment.

- `--backend gpu` runs automatic selection. Its receipt reports
  `opencl_coarse_score_rows`, sessions, frequency fallbacks with their
  reasons in `opencl_coarse_fallback_counts`, rescored peaks and transfer
  bytes, and a precision label naming the dense search. `--spread-preset none`
  gives one-ray sessions, which the dense search scores unless
  `--opencl-coarse-one-ray off` keeps them on the FFT session as
  `one-ray-unqualified`; a GPSDO clock at 3.6 Hz
  projects to 3 Hz and stays on the compact tight-clock FP64 route instead.
- `--backend vulkan --vulkan-precision nvfp4` requires the dense search,
  reports device event intervals in `opencl_coarse_*_seconds` and fails with
  a named reason when a session cannot be admitted. The synthesis and
  selection intervals are stage totals. `opencl_coarse_render_seconds`,
  `opencl_coarse_filter_seconds` and `opencl_coarse_gram_seconds` divide
  synthesis, with the warp counted in the filter on every route.
  `opencl_coarse_rescore_seconds` is the rotation and rescoring part of
  selection.
  It scores host-prepared columns where a session offers no synthesis, for
  example with a varying passband. `--opencl-coarse-templates device|native`
  states a template source, and the probe then requires it to be the one
  executed. The matched runs gave every explicit route a 1 GiB per-session
  limit with `--device-memory-limit-bytes`. The measured source leased
  258,943,744 bytes (247 MiB) for this geometry at 24 workers and
  236,576,512 (226 MiB) at four. `7ed007dc` quotes the same. Every dense
  session now also leases the rotated observation, tile frequencies ×
  (kernel rows + arrivals − 1) × 8 bytes, and warping in the FIR drops this
  geometry's 14-column raw batch of 125,726,976 bytes. `b3c7c48f` quotes
  139,018,496 bytes (133 MiB) and 112,396,800 (107 MiB). The four-lane
  raster and the warp sets raise that to 156,979,456 (150 MiB) and
  130,357,760 (124 MiB), and the device receipts report the leases.
- `--opencl-coarse-rescore fp32|fp64` with the explicit `nvfp4` search
  states the rescoring accumulation, FP32 partial sums by default since
  their Blackwell qualification and `fp64` as the control, and the probe
  requires
  `opencl_coarse_rescore_fp32_partial_peaks` or
  `opencl_coarse_rescore_fp64_peaks` to hold every rescored peak. The
  precision label is `nvfp4_dense_coarse_fp32_partial_rescored_peaks_native_fir_and_fine`
  for FP32 partial sums and keeps `nvfp4_dense_coarse_fp32_rescored_peaks_native_fir_and_fine`
  for FP64 accumulation.
- The link of the matched fixture is far above threshold. For paired
  decisions near it, raise `--noise-dbm-hz` towards −82 on the stable
  channel. `--signal-gain-db` is accepted on the CPU backend only, and the
  probe rejects a repeated option.
- A deployed worker refuses a second deployment of different source. Copy
  changed files to the deployed tree, rebuild the two targets and record
  source and binary hashes with the results.
- Check kernel changes first with `triad_opencl_nvfp4_emulated`,
  `triad_opencl_coarse_session_emulated` and its one-ray variant,
  `triad_opencl_fine_session_emulated` and its deferred-execution variant.
  They caught every layout error before a device run, and the deferred one
  catches a host buffer changed under a pending write. They cannot reach the Vulkan backend's device host:
  the pinned backend's missing lease budget appeared only on the device.
  They also do not compile the kernels with NVCC. A new kernel entry needs
  its row in the [PTX embedding table](../tools/triad_opencl_embed_ptx.py).

## Device-rendered fine tiles and resident windows

After the dense search the resident fine stage was most of a slot. Each of
its 405 submissions held two blocks. The owning host thread rendered every
dense source tile, uploaded it, and waited for each result. Two changes
remove that host work. On the RTX PRO 6000 Blackwell they cut the fine stage
of the matched slot from 0.55–0.60 s on the route they replace to
0.08–0.11 s at 4 and 24 software workers, with identical evidence, and the
automatic slot from 0.89–1.04 s on the reference build to 0.16–0.20 s;
streamed mappings, below, then brought it to 0.14–0.18 s.
[Fine adapter](../src/triad_opencl_fine_bulk.cpp) ·
[session](../src/triad_opencl_nvfp4_projection.cpp) ·
[kernels](../src/triad_opencl_nvfp4_kernels.cu) ·
[window scheduling](../src/triad_receiver_fine.cpp) ·
[validation record](validation.md#resident-fine-tiles-and-windows--20-november-2026).

**Tiles.** A dense fine tile is the keyed waveform of one epoch and bit on
the basis's dense grid. A backend that renders tiles now receives each mapped
column's public tile address in place of host samples, and `MappedBasis`
neither renders, pins nor caches that tile. The session uploads the pulse
table, the public waveform parameters and each addressed lane's keyed
coefficients once. A tile that its cache lacks costs one 16-byte request.
The FP64 kernel renders in the operation order of the native renderer, and
the FP64 mapper interpolates the result. Those tiles match native tiles
within 5.5e-14 to 2.5e-12 of the tile peak for continuous, resampled,
hop-gated and burst waveforms. The FP32 kernels, described in the
[precision guide](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling),
are the default since their
[Blackwell qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026);
their tiles are held to 4e-6 of the tile peak, and `--opencl-fine-map fp64`
keeps the FP64 kernels as the control. Columns that take the exact native
fallback are still uploaded as samples, and every other backend keeps native
tiles. A source with more than 32,768 cells keeps native tiles.

**Windows.** Ordinary fine scoring runs the fixed-affine lane, whose maps do
not depend on earlier statistics. The session therefore submits a window of
dispatches in one call: 16 blocks by default, 2 to 64 by option. Lanes
project the next window's observations meanwhile, one leaf per dispatch on
the OpenCL route, whose lanes own transform scratch. The owner places the
window's blocks first and adds their mode and quiet-energy receipts in
public order once all are projected. Each state then advances
through the window's dispatches in public order as one leaf, while the owner
already stages and submits the next window, and the owner closes the
dispatches in order afterwards. Statistics, state updates and receipts equal
those of single dispatches: a native test with a deterministic backend gives
bit-identical evidence for windows of 4, 6, 16 and 64 blocks, with
checkpoints inside a window and with an erased block. The adaptive
diagnostic lane, whose maps follow its pruning, keeps one dispatch per call.
A block outside the resident geometry, such as the enlarged terminal block,
takes the native route in its place in the window. A session leases only
the window it submits: the adaptive lane two blocks, an odd option one
block fewer. A lease too small for the window falls back to two-block
batches, and the next window asks for its lease again while still holding
the smaller one. Receipts name the largest batch in
`opencl_resident_window_blocks` and the windows that ran reduced in
`opencl_resident_reduced_windows`. No lease at all leaves native statistics
with a named reason.

**Smaller transfers.** The mapping descriptor fell from 176 to 64 bytes: a
block's rows span at most a few tiles, so one byte names each cache slot.
Clearing the batch buffer became a kernel that resets each lane's trailer,
because mapped and uploaded lanes are written in full. Lanes that received
no column are zeroed by a second kernel only when one exists.

**Streamed mappings.** With device tiles the owner no longer stages a whole
window before the device starts. Every four blocks it writes the new
descriptors and render requests without waiting and submits their render
and map kernels with `clFlush`, so the device maps while the owner stages
the following blocks. `--opencl-fine-stream 0` stages a whole window first,
as a control in the same binary. The writes come from per-batch arenas that
stay unchanged until the batch's statistics read, and a descriptor already
queued is never merged into. When a batch's render requests outgrow their
arena, four times the 64 cache slots, the owner waits for the queue before
reusing it. What still blocks is the session's setup (the DFT bins and the
source parameters), a keyed lane's first use, and the rare exact native
column; host tiles of the control route stay blocking too. Each buffer is
touched when the session is created, so a driver that allocates at first
use fails there and the adapter falls back.

A deferred mode of the kernel emulation, `DATAPUMP_TRIAD_EMULATION_DEFER=1`,
runs every non-blocking command as late as an asynchronous device may. In
that mode the fine-session test and a pressure check fail when a queued
write's source changes, which immediate execution cannot show. The pressure
check renders 292 tiles through a 16-block window and, with a 16-slot cache,
flushes under cache pressure and waits for its arenas; it catches a
loosened merge guard and a missing arena wait. On the device the severe
fine stage at 24 workers fell from 0.092–0.097 s staged whole to
0.066–0.070 s streamed, with identical evidence, and the owner's wait for
statistics from 34 to 14 ms. Streaming every two blocks was no better and
every eight was worse.
[Streamed device comparison](validation.md#streamed-resident-fine-mappings--20-november-2026).

### Counts for the matched slot

A slot of the [matched 3.6 Hz geometry](#reproduce-the-matched-blackwell-runs)
has 810 blocks and 72 fixed-affine states: 809 resident blocks and one
native terminal block. Device receipts of the severe slot, which keys two
epochs:

| Per slot | Reference automatic route | Control route | Windows of 16, device tiles |
| --- | ---: | ---: | ---: |
| Resident batches and blocking reads | 405 | 405 | 51 |
| Blocking writes | 1,041 | 1,073 | 159 |
| Kernel dispatches | not recorded | 2,434 | 357 |
| Upload bytes | 91,953,704 | 79,885,256 | 7,580,384 |
| Dense tiles uploaded / rendered on the device | 552 / 0 | 552 / 0 | 0 / 552 |
| Readback bytes | 10,251,648 | 10,251,648 | 10,251,648 |
| Device lease per fine owner (bytes) | 16,811,776 | 16,748,032 | 74,236,416 |

The control route is the reference arithmetic with the 64-byte descriptor:
`--opencl-fine-tiles native --opencl-fine-window 2`. These receipts predate
streamed mappings, which on the stand-in device leave 7 blocking writes per
slot, for the setup, and raise dispatches from 357 to 588. The stable slot keys
one epoch: 920 writes and 55,771,496 bytes before, 157 and 7,564,352 with
windows. Windows of 32 and 64 blocks gave 26 and 13 batches and 86 and 48
blocking writes under leases of 139,778,816 and 270,863,616 bytes. A
stand-in device whose kernels do nothing had predicted these counts before
the device run.

### Fine seconds on the device

Before streamed mappings, explicit routes inside the candidate binary, with
a 1 GiB device limit, every OpenCL timer off and both orders, gave fine and
slot seconds on one RTX PRO 6000 Blackwell:

| Input, software workers | Native fine | Control route | Device tiles, two-block batches | Windows of 16 |
| --- | ---: | ---: | ---: | ---: |
| Severe, 24 | 0.772–0.851 | 0.568–0.596 | 0.152–0.201 | 0.084–0.090 |
| Quiet, 24 | 0.776–0.794 | 0.578–0.580 | 0.176–0.187 | 0.106–0.107 |
| Stable, 24 | 0.784–0.827 | 0.356 | 0.148 | 0.081–0.086 |
| Severe, 4 | 2.182–2.274 | 0.548–0.554 | 0.154–0.157 | 0.085–0.088 |
| Quiet, 4 | 2.117–2.201 | 0.579 | 0.182–0.184 | 0.110–0.114 |
| Stable, 4 | 2.158–2.291 | 0.366–0.369 | 0.149–0.159 | 0.084–0.087 |

The severe slot fell from 0.648–0.681 s on the control route to
0.165–0.170 s with windows. Device tiles remove most of the time and
windows most of the rest. Windows of 32 and 64 blocks measured 0.083 and
0.080 s severe and 0.106 s quiet, within the spread of windows of 16. Every
resident route gave the same evidence, to the last digit, on all three
inputs at both widths. Native fine evidence differs by FP4 rounding.

A profiled `slot` run splits the severe fine stage at 24 workers:

| Per slot (ms) | Control route | Windows of 16 |
| --- | ---: | ---: |
| Fine session, wall | 583 | 91 |
| Owner: mapping sources, of which native tile rendering | 409, 377 | 22, 0 |
| Owner: staging descriptors | 11 | 14 |
| Owner: final transfers and dispatch | 40 | 5 |
| Owner: waiting for statistics | 35 | 34 |
| Device: mapping | 27.4 | 23.9 |
| Device: tile rendering | 0 | 4.3 |
| Device: packing, products and statistics | 19.6 | 7.1 |
| Device: transfers | 2.5 | 0.5 |
| Device span, first command to last | 493 | 75 |

The control route kept the device busy for about 50 ms of a 493 ms span.
With windows the device works about 36 ms and the owner about 42 ms, and
each waits for the other. Larger batches also cut packing and statistics
from 15.7 to 5.0 ms. Windows of 64 blocks gave 87 ms with the same split.
Unprofiled, the windowed session took 82 ms, so the timers cost about 9 ms.
The states' Gaussian updates, 0.21–0.28 s of processor time, and the 17 ms
observation projection run on the lanes beside this work.

### Host time without a device

The stand-in bounds what the host contributes. It says nothing about device
time, and its transfers are memory copies. On the Ryzen 5 PRO 5650U
development host a slot took, as the best of five in one alternating run:

| Keyed epochs, software workers | Single dispatches, native tiles (s) | Windows of 16, device tiles (s) |
| --- | ---: | ---: |
| Two, 4 | 0.680 | 0.068 |
| Two, 12 | 0.662 | 0.055 |
| Two, 24 | 0.675 | 0.059 |
| One, 4 | 0.379 | 0.066 |
| One, 12 | 0.381 | 0.056 |
| One, 24 | 0.402 | 0.058 |

Of the two-epoch control, native tile rendering was 0.56–0.57 s: 276
renders of two 8,192-sample tiles at about 2 ms each. Two earlier builds
with the same renderer source measured 0.78 and 1.12 s for this control at
12 workers, with 0.66 and 0.99 s of rendering. The native renderer then
switched the x87 rounding mode eight times per point, and
[that cost follows the link layout](triad-performance.md#code-placement-and-x87-rounding-mode-switches).
It [no longer switches](validation.md#receiver-host-rounding-mode-switches--20-november-2026).
Mapping and pinning took about 0.03 s, descriptor staging 0.02 s and the
copies 0.01 s. The one-epoch control renders half as many tiles.

With windows and device tiles the owner computes tile addresses in
0.022–0.025 s and stages descriptors in 0.013–0.015 s. The states take
0.11–0.19 s of processor time on the lanes beside that work. With 12 or 24
software workers the owner then waits 0.002–0.004 s for them. With four,
three lanes carry them and the owner waits about 0.02 s, which sets the
slot time there. The observation projection took 0.022–0.031 s on one
lane before each dispatch became its own leaf. Windows of 32 blocks measured
within 0.003 s of windows of 16.
Before the state updates ran beside the next window, the same slots took
0.08–0.09 s.

The address map floors and converts long-double coordinates once per
candidate and tap. With the compiler's inline forms, which switch the
rounding mode, it took 0.023–0.024 s in one of those two builds and
0.036–0.044 s in the other, and the windowed slot took 0.068–0.078 s at
12 workers. It now uses the forms of
[`triad_coordinate.hpp`](../src/triad_coordinate.hpp), which keep the
control word. Every byte uploaded by every route above stayed identical.
Only the build of the table has timed the new forms on a quiet host. A
different host scales all of these values. On the pod's EPYC 9355 the
[profiled split above](#fine-seconds-on-the-device) gives 22 ms for the
addresses and 377 ms for the control's native tile rendering, which still
switched there.

### Remaining fine costs

The profiled split of the severe slot at 24 workers with streamed mappings
and the [exact owner addressing](validation.md#exact-receiver-and-simulator-speedups--20-november-2026)
gave 79 ms of profiled fine work and 82–88 ms of unprofiled fine stage on
the EPYC 9554 pod, with the FP64 kernels. Shortening the owner's addresses
from 22.8 to 14.8 ms and its staging from 14.8 to 9.3 ms did not shorten the
unprofiled stage, so the device mapping and the window wait bounded it then.
The FP32 kernels have since become the default: on the EPYC 9335 host
mapping took 5.1 ms against 24.3 ms and tile rendering 2.5 ms against 6.0 ms,
and the fine device span fell from 46.0 to 34.7 ms
([qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026)).
The owner-side items below were measured before the FP32 kernels and now
exceed the device mapping; a profiled split with the FP32 kernels is not
recorded. Ranked:

1. **The owner waits for each window: 14–15 ms**, down from 34 ms staged
   whole. What remains is each window's last four blocks and its packing,
   products and statistics. Overlapping across windows would need a second
   set of batch buffers and twice the lease.
2. **Owner address computation: 14.8 ms.** Per candidate it holds no lock,
   allocation or `fprem` loop; two x87 divisions, five exact remainders and
   about ten long-double multiplies remain. Addresses of the next window
   could be computed on idle lanes, but that moves rather than removes CPU
   work and needs per-window records charged to the workspace quote.
3. **Owner staging and submission: 9.3 and 9.8 ms.** Descriptors are 6.9 MB
   of the 7.6 MB uploaded, each with its affine map and phase in 40 bytes,
   and every four blocks add a flush.
4. **Device rendering, packing, products and statistics.** Rendering ran in
   188 small dispatches and took 6.0 ms with the FP64 renderer and 2.5 ms
   with the FP32 one on the qualification host. The FP32 renderer keeps 142 static FP64 operations against
   the FP64 renderer's 258 (SASS counts), all outside the chip loop, which
   reads FP32 chips and pulse entries without conversions, so the
   dispatches' fixed costs and low occupancy may now bound it.
5. **Device mapping: 5.1 ms** with the FP32 mapper, close to the predicted
   3 to 4 ms. The FP64 mapper took 24.3–24.7 ms for 175 million samples with
   one FP64 sine and cosine each. The FP32 mapper's static SASS has no
   `DFMA`, `DADD` or `DMUL` against 110 FP64 operations in the FP64 mapper,
   but 8 `F2F.F32.F64` narrowings and 3 `I2F` conversions, which
   consumer-class SMs may run at the FP64 rate. Nsight `pipe_fp64` would
   answer the dynamic-issue question.
6. **The native terminal block.** One enlarged block per slot keeps the
   native route, with two tile renders. The device kernels have one fixed
   block shape.

Windows above 16 blocks did not help this slot, so the default stays at 16
and a 74 MB lease per fine owner. Streaming every four blocks stays the
default.

Three exact host changes followed this split. The common null of each
one-tap and persistent two-tap Gaussian update is skipped when its zero
statistics, zero mean, bounded prior and decaying transitions prove that it
keeps zero evidence; the additive slow pair still runs it. A quiet slot's
varying re-projection of about 800 FP64 blocks runs on the slot's lanes and
sums its mode terms in the original order. Memoizing the quiet threshold
was not worth process-wide state: its root-find takes 0.08–0.10 ms per call
on the 5650U. Receipts keep logical and executed work apart:
`gaussian_updates` still counts all three hypotheses, and
`gaussian_null_skips` counts the skipped nulls, so forecasts charge the
difference. A slot's `varying_quiet_seconds` times the quiet re-projection.

### Measure the fine stage on the device

Use the [matched procedure](#reproduce-the-matched-blackwell-runs). Build the
reference from the source before the change under test and the candidate
from it, and run both device tests first. The lab bundle ships only the
test sources it lists in
[`triad_gpu_bundle.py`](../tools/triad_gpu_bundle.py), so a new device test
and its headers need a line there, or the pod's configure fails:

```bash
cmake --build "$candidate/build/lab" --parallel "$jobs" \
  --target triad_receiver_probe test_triad_opencl_nvfp4 test_triad_opencl_fine_session
filtered "$candidate/build/lab/test_triad_opencl_nvfp4"
filtered "$candidate/build/lab/test_triad_opencl_fine_session"
```

The first runs both arithmetics. The FP64 kernels must report rendered
tiles within 1e-10 of native tiles and statistics within 2e-3 of the
uploaded-tile session for FP4 and 1e-6 for the FFT. The FP32 kernels must
report tiles within 4e-6, FFT statistics within 1e-5 of the FP64 kernels
and, in the pressure check, of the uploaded-tile session, and FP4
statistics within 2e-3.
Its coarse check holds FP32 partial rescoring within 1e-5 of the row's best
score of the FP64 accumulation. Its synthesis checks include columns that
share maps across lanes, which must filter to the same bits as alone. The
second runs a complete fine session through every route, with FP32 and FP64
evidence within 2e-3 of each other. Record each device test's duration
against its 30 s limit. The coarse session test, which meets the native
search with each rescoring and runs the one-ray sessions, has no device
build: it runs only in the kernel emulation
(`triad_opencl_coarse_session_emulated` and its `_one_ray` variant, 180 s),
so its result is not device coverage. Then compare routes inside the candidate binary:

```bash
route() {
  local label=$1 command=$2 tiles=$3 window=$4 profile=$5
  shift 5
  filtered "$new" "$command" "${fixture[@]}" --backend vulkan --vulkan-precision nvfp4 \
    --vulkan-fine-projection resident-nvfp4 --opencl-fine-tiles "$tiles" \
    --opencl-fine-window "$window" --opencl-profile "$profile" --workers 24 \
    --device-memory-limit-bytes 1073741824 --max-samples 1215321 --deadline-ms 45000 "$@" \
    > "$scratch/$label.json" 2> "$scratch/$label.stderr"
}
route control admission native 2 off
route windows admission device 16 off
route windows-profiled slot device 16 on
route control-profiled slot native 2 on
```

`--opencl-fine-tiles native --opencl-fine-window 2` is the control: uploaded
tiles in single-dispatch batches. `--opencl-fine-map fp32` runs the FP32
tile renderer and mapper, and the probe then requires
`opencl_resident_fp32_mapped_columns` and
`opencl_resident_fp32_rendered_tiles` to hold every mapped column and
rendered tile. `--opencl-fine-map fp64` runs the FP64 control and requires
the FP64 counters; unstated, the probe follows the library default, FP32
where the kernels are compiled. Expect
evidence equal within one arithmetic and FP4-rounding differences across
the two. `--opencl-fine-stream 0` with device tiles
stages each window before the device starts, the control for streamed
mappings. Windows are even numbers of blocks. A
stated tile source or window that did not execute fails the run, for
example when a lease refusal reduced a window. Time the routes with
unprofiled `admission` runs, in both orders. An explicit `nvfp4` coarse
search records its device intervals unless `--opencl-profile off`; the
resident fine session is timed only with `--opencl-profile on`, which the
identity records as `requested_opencl_profile`.

The probe's `slot` command adds the fine session's own receipt in its
`fine_work` phase, with per-state work timers. With windows,
`times.observation_seconds` is only the owner's block placement and mode
receipts. `times.worker_observation_seconds` sums the lanes' projection
leaves, which overlap one another, and the owner's wait for them is in
`dispatch_seconds`. The `slot` command's coarse stage scores
host-prepared columns: about 41 s at 24 workers on the pod, and past a
45 s deadline at four, so it states no template source. A profiled run
adds `opencl_resident_host_*_seconds` for the mapping sources, staging,
final transfers and the wait for statistics, and
`opencl_resident_device_*_seconds` for transfer, tile rendering, mapping,
packing, products and statistics, with the span from the first command to
the last. Profiling reads the clock around every staged column. Require
equal policy outcomes and equal `bit_evidence` between resident routes.
Repeat the automatic runs of the matched procedure against the reference
binary, both orders for severe and quiet, the five-slot message and the
paired threshold decisions.

## Fine-stage device ports

Three CPU stages can move transform work to the device: the signed-lag gain
direct rank of the coarse search, drift proposals, and the varying
statistics of fine scoring. **Each has a Vulkan implementation on an
explicit `device` route. Automatic selection runs only the signed-lag rank,
and only on the qualified RTX PRO 6000 Blackwell class** (SM 12.0,
identified through OpenCL). On the 40.8-CPU Xeon host at W = 80, where
the native rank ran 2.4–4.6× slower than on the 5650U, it cut the
72 Hz/5× automatic slot from 38.4–55.5 s to 17.8–19.8 s and a 36 Hz/10×
holdout from 688 s to 281 s; device margins are host-specific
([where the time goes](#where-the-time-goes)). Drift and varying device
routes were slower than native on that host
([validation](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026)).
For a supported shape on a device with FP64/INT64 shaders, automatic
selection declines every other family and device as
`vulkan-<family>-unqualified-device`; the host-scoring route declines
earlier as `vulkan-<family>-shader-fp64-unavailable` or
`vulkan-<family>-host-scoring-forced`, and a `native` route keeps the CPU
stage. A declined stage runs its unchanged native stage, so there decisions,
evidence and native counters are those of the CPU route. The device routes
differ in exactness: signed-lag ranks publish only certified nominations,
equal to native (E1); varying statistics differ from
native by FP64 roundoff (E2); drift proposals can select differently at
near ties (E3, proposal-qualified). The family subsections below give
their checks.

The exact CPU changes that came with the ports run on every route: the
slope-blocked drift ridge search with a wider drift stage, varying block
batches and signed-lag shift runs. On the 5650U they cut the drift stage by
16–20% on the 72 Hz/1× slot and 33% on the 72 Hz/5× control, and varying
fine by 13–18% and 37%, with every receipt value unchanged. Signed-lag
ranks and the complete slots, which the coarse stage dominates, moved by
less than 3%. See the
[validation entry](validation.md#fine-stage-device-routes-and-exact-cpu-stages--21-november-2026).
On the RTX PRO 6000 pod (Xeon 6952P, W = 80) the automatic fine stage fell
from 1.58 to 0.89–0.94 s on the 72 Hz/1× slot and from 17.1 to 8.0–8.7 s on
the 72 Hz/5× control, against single reference runs, and the drift stage
widened from 8 to 58 planned lanes, 43–48 of them running.

The backend-neutral interface is in
[`triad_receiver_bulk.hpp`](../src/triad_receiver_bulk.hpp). Its stage block
defines `GainRankBulk`, `DriftBulk` and `VaryingBulk` with their shapes,
and `ReceiverBulkBackend` adds `make_gain_rank`, `make_drift` and
`make_varying`.

- **A factory is the session's admission.** The slot owner calls it on its
  own thread when the stage starts. It returns an admitted session, or null
  with the decline or fallback recorded.
- **`stage_decline`** is the pure selection policy, shared by preflight and
  execution. Called with a family alone, it gives the shape-independent part.
- **`stage_host_bytes`** quotes a session's host memory to the DSP
  preflight: its plan and shader compilation (`compiler_host_bound`, one
  plan or shader at a time) on every device, plus the part of its lease in
  host RAM (`stage_host_memory`). That is the whole lease on unified memory.
  Elsewhere it is the buffers the host reads and one twiddle-table staging
  copy, where those use a system-memory heap, as on a discrete GPU with an
  explicit workspace. A declined stage, or one whose factory would admit
  nothing, quotes zero.
- **`record_stage_native`** records a stage the receiver ran natively
  without a factory's own record (`StageNative`):
  - a decline before any factory call;
  - a fallback before any factory call, such as drift or varying host
    bytes that the stage envelope cannot hold even at the narrowest batch
    (`unified-memory-envelope` on unified memory, `workspace` otherwise);
  - a fallback of an admitted session's call.

  The first two count their planned call, so the probe's
  `--require-accelerator` rule applies to a route whose every stage stayed
  native before its factory.
- A call returns false, publishes everything, or throws. False can come
  before device work (a nonfinite input) or after it (nonfinite device
  results). It leaves outputs and counters untouched and records the
  fallback, and the receiver runs that call's work natively.
- `SelectedGpu` forwards every member. `triad_vulkan_stage_policy` checks
  this with a fake backend in every build, with or without Vulkan.
- **Leases never wait.** In an automatic workspace every fine-stage session
  takes an optional lease of the shared device workspace. In an explicit
  workspace a gain-rank or drift session takes no lease and is bounded by
  the explicit ceiling by itself, as a coarse session is, while varying
  sessions and the OpenCL fine lease share one explicit ceiling
  (`Shared::try_acquire`). An explicit device footprint can therefore reach
  the ceiling times the live coarse, gain-rank and drift sessions, plus
  one. Reconcile the two explicit rules before a qualification compares
  families under one explicit workspace.

The Vulkan implementation has three layers:

- **Device core.** [`triad_vulkan_core.hpp`](../src/triad_vulkan_core.hpp)
  and its `.cpp` hold `Shared`, `Context`, buffers, pipelines and VkFFT
  plans, moved from the backend without numerical edits. They add:
  - FP64 VkFFT plans with quoted FP64 twiddle tables;
  - pipelines of up to six bindings;
  - a descriptor-pool size per context;
  - a shader registry with a kind range per family, where each family's
    shader header names a source for every index below its count;
  - a context that names its family. Its transforms count into the
    session's `StageDeviceWork`, never into the coarse search's
    `fft_calls`, `fft_rows`, `vulkan_dispatches`, `synchronizations`,
    `executed`, wall or device command time, which receipts use as proof
    of coarse work and as coarse throughput.
- **Policy and entry points.**
  [`triad_vulkan_stages.hpp`](../src/triad_vulkan_stages.hpp) holds the
  policy, the reason names and the family entry points.
- **Family files.** `src/triad_vulkan_{gain,drift,varying}.cpp` hold each
  family's policy, factory and session, with its kernels in the matching
  `*_shaders.hpp`. `src/triad_vulkan_drift.hpp` exposes the drift kernel
  oracle to `test_triad_vulkan`.

The policy tests its conditions in this order: native requested, explicit
FP64, forced host scoring, missing FP64/INT64 shaders, unsupported shape.
An explicit device route then accepts. Automatic selection also requires the
family's `automatic_<family>_qualified` constant (true only for the gain
rank) and the OpenCL-identified SM 12.0 device class. An accepted stage
without a device implementation declines as `unimplemented`; today only the
varying `fp32-template` arithmetic does. Automatic selection therefore
reports `vulkan-<family>-unqualified-device` for a supported shape, except
for the gain rank on the SM 12.0 class. An explicit device route runs the family on the device,
unless the receiver keeps the stage native for one of its own signed-lag
gain or varying reasons. A Vulkan
build's options and probe flags are in
[the precision guide](gpu-precision-controls.md#fine-stage-device-routes).

`triad_vulkan_core` (`test_triad_vulkan --core`) checks these on the device:

- the registry's family ranges;
- nine six-binding pipelines in a pool beyond the default eight sets and
  32 descriptors. One runs the products kernel against host FP32 arithmetic.
- 4,096-row FP64 twiddle-table plans against a host extended-precision DFT,
  with their quoted table charge released with the plans;
- coarse counters left unchanged by a fine-stage context.

On the Radeon (RADV RENOIR) the FP64 plans matched within 3.9e-15 of the
spectrum RMS forward and 1.2e-15 round trip. The products were exact. The
varying statistics' FP64 plans use the same quote.

The plan and shader caches are sized once per backend. A backend whose
policy can select some fine-stage family for the device holds 48 serialized
VkFFT programs in 64 MiB and 64 SPIR-V shaders in 512 KiB with 16 KiB of
metadata. That is an explicit device route, or a family qualified for
automatic selection on that device class. Every other backend keeps the
coarse search's 16 programs in 8 MiB and 20 shaders in 160 KiB with 8 KiB
of metadata. The larger bound adds 59,088,896 bytes to
`persistent_host_workspace()`. That bound is charged in the stream's fixed
reservation and again in each live owner's DSP lease, so under any binding
memory limit, an explicit `--memory-limit-bytes` or the default derived from
available RAM ([CPU allocation](development.md#receiver-cpu-allocation)), an
explicit device route can admit fewer owners. The cache size changes no
result. Native routes, and automatic routes outside the OpenCL-identified
SM 12.0 class, keep the smaller charge and an 8 MiB
`host_cache_limit_bytes`. Automatic selection on that class serves the gain
rank and holds the larger bound, so it, like an explicit device route, can
admit fewer owners. Charging the backend-wide cache once, in the
reservation, would remove the per-owner charge
([next levers](#next-levers-in-order)). With all
three families on the device, the 72 Hz/1× slot below used 18 cache entries
and 2,241,636 cache bytes. Explicit-route runs may report different
`vulkan_fft_cache_hits`, `vulkan_fft_cache_misses` and `host_cache_entries`
than coarse-only runs.

The 72 Hz/1× admission slot ([arguments](#qualification-slots), `--backend vulkan` with
automatic fine-stage routes) was checked on the Radeon against the run
before the fine-stage changes (21 Nov 2026):

- every decision, status, evidence value and native counter was equal, as
  were `workspace_peak_bytes` (1,166,014,368) and `host_cache_limit_bytes`
  (8 MiB);
- each family recorded one `vulkan-<family>-unqualified-device` decline
  (planned 1, declined 1), and the slot named its native routes
  (`signed_lag_gain.proposal_route` and `varying_fine_route` `native`,
  `varying_fine_arithmetic` `native_fp64`);
- the other new fields were zero, empty, the requested `automatic` routes,
  or receipts of native work: 18 `varying_components/*` copies of evidence,
  `drift_retained_digest` `a626c2e80c62feef`, `varying_batches` 4,
  `varying_batch_blocks` 52 and `varying_workspace_bytes`;
- the drift width receipts (`drift_stage_workers` and both planned widths)
  went from 8 to 24 lanes.

All three device routes were run together on the Radeon (21 Nov 2026, one
run each). On the 72 Hz/1× slot every difference from the native run equals the value of
exactly one single-family device run, apart from the shared cache counters,
which add. On the 72 Hz/5× control
([arguments](#qualification-slots)) the slot completes with zero declines and fallbacks:
lags 5438, 346 and 1215 equal native, the drift digest and drift evidence
are bit-identical to native, varying evidence stays within 1.8e-15 nats, and
the gain band ratio is 0.48. These single loaded runs give no timing result,
and the Radeon stays declined.

### Signed-lag ranks

Scope: the direct statistic of every unsampled block in `rank_gain_timing`.
The fixed rank of the sampled blocks stays on the host.

Qualification is decision-identical publication (E1), because similar lags
would not preserve decisions: on the hardest receipts, HF −22.5 and
36 Hz/10× −21, the bit separation equals the evidence difference of one
direct-rank map to about 1e-13 (lags 3102 and 3101). FP32 device totals
select a frontier. The host rescores it in FP64 under a band estimated from
audit blocks, and falls back natively on a band violation, frontier
overflow or near tie. Published lags therefore equal native at equal
workers whenever the audit band holds (see Limitations), and every fine
evidence term is unchanged.

**Status.** An unfused Vulkan session (`src/triad_vulkan_gain.cpp`, kernels
in `src/triad_vulkan_gain_shaders.hpp`) runs on the explicit
`--vulkan-gain-rank device` route. Automatic selection declines it as
`vulkan-gain-rank-unqualified-device` on every device except the qualified
RTX PRO 6000 Blackwell class, where `automatic_gain_rank_qualified` makes
it the automatic route (21 November 2026; [Blackwell results](#blackwell-qualification-of-the-gain-rank)). Each batch of
blocks runs four kernels:

1. `gain_columns` builds `[block][2 + chunks][T]` rows from the FP32 inputs
   staged for its submission: both bits' zero-padded template columns, then
   the native overlap-save observation windows. Chunk c holds outputs `[cC, cC + C)`,
   with `C = T - B + 1`. One batched forward VkFFT follows.
2. `gain_product` forms `Y[(k + bin) & (T - 1)]·conj(C[k])` for every chunk,
   bit and frequency, with separately rounded products. One batched inverse
   VkFFT follows.
3. `gain_rank` takes each block, bit and lag. It computes the FP32 emission
   `(g·re)² + (g·im)² - logdet`, with `g = cs/scale` rounded once, then a
   two-pass FP32 log-mean-exp over frequency. A nonfinite value stores NaN.
4. `gain_finish` adds each unsampled block's value to FP64 totals in block
   order, so the totals do not depend on the batch width.

The device always tiles: T is 256 on HF and 1,024 at 60 Hz/1×. The
observation and columns are packed with power-of-two scales, taken over the
whole request and folded into g. Audit blocks write their own rows.

**Staged inputs (instead of resident whole-request copies).** Each
submission packs only its own inputs: its blocks' observation spans, merged
while consecutive spans overlap, each block's two template columns in a
fixed slot, and their table entries. Only the run shifts stay resident.
The staging holds any one batch's spans, and merged consecutive spans leave
room for the submission's other batches. The device values are unchanged:
the oracle's statistics are identical to every printed digit, and a 3-block
batch gives the same totals and audit values bit for bit as a 39-block one.
The reason is workspace capacity. On the HF production shape (403,266
blocks, 256 audit blocks, 64 runs; quotes without buffer alignment):

| Batch | Resident copies | Staged |
|---|---|---|
| 1 block | 367 MB | 38 MB |
| 18 blocks | 586 MB | 258 MB |
| 64 blocks | 1,179 MB | 855 MB |

The probe's default explicit device workspace is 256 MiB. With resident
copies no HF batch fitted, so every explicit HF slot fell back; staged, the
session admits 18 blocks (the oracle checks that the production shape is
admissible there). The resident form would cost about 0.3 GB more at every
width. Reconsider resident copies only if host packing becomes measurable
on a fast device. Per unsampled HF block it packs about 0.5 KB of
observation and 0.5 KB of template columns (resident: 0.25 KB and 0.5 KB
once), and each audit block its own 85 KB span.

The session's lease covers every buffer and both plans' twiddle tables. It
takes the widest batch within its bound (at most 64 blocks and one
submission row cap; the explicit ceiling, or a quarter of the automatic
shared workspace), halves while the shared workspace refuses it, never
waits, and otherwise falls back as `workspace`. Nonfinite inputs fall back
as `nonfinite-input` before any device work. The session returns its
buffers, plans and lease as soon as its call ends (`GainRankBulk::release`),
before the fixed lanes finish and the FP64 certification runs; only its
receipt stays. VkFFT's temporary buffer is quoted at the product buffer's
size, although these single-upload power-of-two plans do not use it; a core
change that sizes it from VkFFT's requirement would roughly double the
admitted batch.

**Receiver.** The rank consults the backend's pure policy first, so a
declined route reports its policy reason; the factory records the planned
call. Then come the receiver's own gates, the shift runs and the tables.
The fixed lanes and one device job then run together. A factory that
returns no session has recorded either a decline or its own planned
fallback (a refused lease, an unplannable transform); the rank tells them
apart by the receipt counter that grew and reruns the native rank. The
reason it reports with that outcome is the receipt's reason of that kind.
The backend keeps the first reason of each kind, so after an earlier call
recorded one, a later call of the same kind reports the earlier reason.
Only test trials (`coarse_gain_rank_trial`) read this per-call reason.

Where the policy accepts the rank's shape, the DSP preflight
(`gain_device_charge`) charges two things:
- the receiver's certification host buffers
  (`gain_direct_device_host_bytes`);
- the session's host bytes (its compilation, `compiler_host_bound`, on
  every device, plus the part of its lease in host RAM), joined to the
  maximum of the coarse bulk terms, which hold the coarse sessions' own
  compilation.

The shape is public geometry: the run and audit tables at their
capacities, and outputs bounded from the aperture's two end blocks.

The receiver's own reasons are `verify-mode`, `no-direct-rank`,
`no-unsampled-blocks`, `audit-unavailable`, `shift-runs`, `shift-range`,
`band-violated`, `frontier-overflow` and `near-tie`.

Receipts: `vulkan_gain_rank_*`, with `frontier_lags`, `rescored_lags`,
`rescore_seconds`, `band_source` and `band_ratio`, recorded for every
certification, published or not. The first three add over certifications.
`band_ratio` is the largest rescored |T − R| over its lag's band, the
maximum over certifications; above 1 the band failed. `band_source`
(`audit` or `fp32-floor`) is the band term of the bit with the larger band
in the first recorded certification. It does not identify the lag band
behind `band_ratio`, which can come from the other term, the other bit or
a later certification. Slot fields:
- `signed_lag_gain.proposal_route`: `native` with a backend present, or
  `fp32_fft_direct_rank_fp64_accumulation_fp64_rescored_frontier`;
- `device_fft_row_stages` for any validated device call;
- `rescore_fft_row_stages` whenever the FP64 frontier rescoring ran,
  published or not: one forward 2B-point host transform per rescored lag
  and unsampled block (64 points on HF);
- `rescored_lags` and `top2_gap` of a certified rank only.

`fft_row_stages` counts the native rank's own transforms only. Device
transforms are in `device_fft_row_stages` and the host rescoring's in
`rescore_fft_row_stages`, so a CPU cost estimate of a device route adds
`fft_row_stages` and `rescore_fft_row_stages`. `score_rows` credits the
device-scored blocks after a certified readback.

**Exact shift runs (bit-identical).** A lag's rounded signed shift is
monotone in the block centre time. Each rank lane therefore evaluates its
full shift vector only when it leaves the current run of identical vectors.
It finds the run end by a binary search with the identical long-double
expression, fastest-moving lag first, and stops once the end precedes its
next visit. A disciplined (GPSDO-class) clock forms one run: about 4·W·L
evaluations per slot instead of blocks·L (HF: about 1e6 instead of 4.3e9,
analytic counts). On ppm-class clocks over long apertures nearly every block
starts a run and the cost stays near native; the 150 ppm signed-gain
fixtures have 221 runs. Workspace and receipts are unchanged. Device sessions
decline above 64 runs (`shift-runs`) or beyond the int32 relative-shift
table (`shift-range`).

**Certification (host).** Device totals T = S + D publish only when all of
the following hold:
- they are finite;
- each FP64-rescored frontier lag lies within its own band. A lag's band is
  N·(|mean| + 8·SE) of its audit blocks' device-minus-FP64 errors, with an
  FP32 floor of 8√N·2⁻²⁴·(1 + RMS v). Its standard deviation is at least the
  bit's pooled one.
- The bit's band is its largest lag band. The frontier is the lags within
  2·band of the best plus the top 8, at most 64. It is rescored through
  `|r| = |DFT_2B[conj(t)·y](k_f)|` in fixed 1024-block chunks;
- the top-two FP64 gap is at least max(1e-9, 8·n·u)·(1 + |R| + Σ|v|);
- the largest excluded total lies below the frontier boundary by more than
  the best lag's roundoff.

Otherwise the native unsampled loop reruns on the same lanes, bit for bit.
Every native outcome with a backend present records its reason. The device
host buffers are admitted and held-checked against their pure-geometry
quote.

Why the band is per lag: on the Radeon oracle the FP32 error is a systematic
relative error of the correlation power, about -3e-7, with a per-block mean
of about -6e-7. A lag's total error is therefore about N times its mean
block error, and it is largest at the best lag.
- The pooled band first tried, 8√N·σ + N·|μ|, failed there: the planted lag's
  error of 2.7e-3 exceeded its 1.5e-3 band. The self-check caught it and fell
  back as `band-violated`.
- A slope and offset fitted over all lags held every lag, but at 0.91 of its
  band.
- The per-lag audit estimate needs no model across lags. Reconsider a
  pooled or fitted band only with a qualification that bounds per-lag
  deviations.
- Its margin is modest: 0.84 (HF) and 0.80 (60 Hz) of a lag's band on the
  oracle, and the worst lag deviated about 5 per-lag standard errors from its
  audit mean, which an iid audit model makes unlikely. The audit and
  unsampled blocks therefore differ systematically, relative to the power.
  The frontier self-check keeps publication safe, but fading or heavy-tailed
  inputs would fall back more often. Decision: no extra term before
  qualification, with `band_ratio` as a gate on the Blackwell strata and a
  relative term (such as N·c·|mean|, c ≥ 0.25) or a per-lag
  maximum-deviation bound if they approached 1. Outcome: the NVIDIA oracle's
  worst lag error was 0.81 (HF) and 0.73 (60 Hz) of its band, and the
  production band ratios were 0.56 (72 Hz/1× slot), 0.26 (72 Hz/5× control)
  and 0.28 (holdout), so no relative term was added. The reconsider rule is
  at the end of the [Blackwell results](#blackwell-qualification-of-the-gain-rank).

The C++ mirror of these kernels on the aged 150 ppm fixtures
(`triad_native_receiver_signed-gain-certification`) still publishes the
native lags under this band, with rescored totals within 1e-10 of native.
It also exercises every fallback reason.

**Device oracle** (`test_triad_vulkan --gain-rank`, RADV RENOIR). The input
is synthetic: a planted bit-0 lag a quarter bin off the lattice, blocks with
silent templates, a silent observation span and two shift runs. The FP64
reference is the frontier rescoring arithmetic. Gates, fixed from the first
RADV run:
- each audit block's RMS error is at most 1e-6·(1 + RMS v);
- each cell's error is at most 1e-5·(1 + |v|);
- in qualification mode, every lag's total lies within its band.

| Shape | Unsampled (+ audit) blocks | Largest block RMS / cell error | Mean error | Band, bit 0 / 1 | Worst error / lag band | Frontier | Top-two gap |
|---|---|---|---|---|---|---|---|
| HF: B 32, T 256, L 10,553, Fd 33 | 2,048 (+ 256) | 5.6e-7 / 3.5e-6 | -6.0e-7 | 3.7e-3 / 2.8e-3 | 0.84 | 8 + 8 | 7,036 / 3.5 |
| 60 Hz: B 128, T 1,024, L 7,036, Fd 95, stride 2 | 192 (+ 64) | 6.9e-7 / 5.8e-6 | -1.0e-6 | 1.6e-3 / 5.9e-4 | 0.80 | 8 + 8 | 4,868 / 1.2 |

Both shapes take their band from the audit. The certified nominations equal
the FP64 rank's argmax, with the planted lag on bit 0. The worst lag
deviated about 5 standard errors from its audit estimate, inside the 8
predeclared. The checks also cover:
- exact transform, row-stage, score-row and download deltas; uploads lie
  between the blocks' span union and their sum;
- a 3-block batch (64 MiB workspace) reproducing the wide session's totals
  and audit values bit for bit;
- the HF production shape admissible in a 256 MiB explicit workspace
  (258,342,768 B, 18 blocks);
- a released session holding no buffers or lease while it lives;
- a pre-cancelled call, a call cancelled mid-call (it throws and publishes
  nothing, or completes) and a nonfinite input;
- a workspace one byte below a session's quote;
- the lease returning to zero;
- a second session's plan cache hits;
- the host quote (compilation on every device, plus the lease on unified
  memory and at most the host-read buffers elsewhere);
- llvmpipe rejection and the policy order;
- the receiver's rank on the aged 150 ppm signed-gain fixture (2,223 lags,
  428 blocks, 123 sampled) with a real session: certified nominations equal
  native (band ratio 0.65), and a 1 MiB workspace falls back in the factory
  as `workspace` and reruns the native rank bit for bit.

**Complete slot.** The 72 Hz/1× slot ([arguments](#qualification-slots)) ran on the Radeon
with `--backend vulkan --vulkan-segmented-timing native --require-accelerator
on` and 24 workers, comparing `--vulkan-gain-rank device` with `native` on
one binary:
- the lags were identical, in the same order: 1213, 1708, 2572 and 3084;
- every evidence, decision and counter field was identical except the
  declared ones. Those are the route fields; native-only `fft_row_stages`
  (1,776,025,600 to 1,123,942,400); the gain receipts; the session's 265 MB
  peak allocation within the 256 MiB explicit ceiling, with two cached plans
  and four shaders; and `workspace_peak_bytes`, +6,839,800 B for the
  certification buffers;
- the device ran one session: 720 forward and 94,800 inverse transforms (60
  blocks without padding), a 16-lag frontier, band ratio 0.54, about 1 ms of
  rescoring, and no declines or fallbacks. The resident form had admitted 7
  blocks per batch in the same ceiling (756 and 99,540 transforms), with
  identical lags and evidence.

That comparison predates the `rescore_fft_row_stages` slot field; a device
run now also differs from native in it.

On the Radeon's automatic route, as on every device outside the
OpenCL-identified SM 12.0 class, against the source before the fine-stage
changes the signed-lag rank adds only its recorded `unqualified-device` decline,
`proposal_route` `native` and receipts that stay zero or empty: the
`vulkan_gain_rank_*` work and extras, and the slot fields
`device_fft_row_stages`, `rescore_fft_row_stages`, `rescored_lags` and
`top2_gap`. On the CPU route only those slot fields are new, with an empty
`proposal_route`. Lags, evidence and native counters are unchanged on
both (the 72 Hz/1× slot on the automatic route and the 3.6 Hz severe CPU
slot, 21 Nov 2026).

A `slot` command's coarse `retained_bytes` rise on every route, CPU
included. The coarse session holds two copies of its midpoint timing result
and two of its progress receipt. The five signed-lag device fields add 32 B
to the timing result (528 to 560 B, GCC 14, x86-64). The bulk receipt's
fine-stage family fields add 528 B to each progress receipt (2,160 to
2,688 B). On the ctest `broad` slot command (`--workers 1 --coarse-tiles 1`)
`coarse_prepare` and `coarse_tiles` `retained_bytes` went from 10,759,616 to
10,760,736 B, which is 2 × 32 + 2 × 528 B. The admitted coarse workspace
covers it, and no decision changes. Moving the timing fields out of the
retained result would not restore the old receipt, because the bulk
receipt's part remains, so they stay.

With a 16 MiB explicit ceiling the gain factory falls back as `workspace`
and the rank runs natively; the slot then ends `incomplete` because the
coarse frequency search's own session exceeds that ceiling. There is no
timing claim. The unfused form is memory-bound on Vega: an indication of about 2 ms per HF
oracle block on the shared host. The 72 Hz/5× and HF shapes need a fused LDS rank kernel
before a Radeon class could qualify.

**Limitations.** Ranks without sampled blocks
(`signed_gain_full_direct_without_fixed`, the wide-span late-calendar
apertures) have no audit and decline `audit-unavailable`, so the device
cannot help where the direct rank is most expensive. A predeclared native
audit set scored beside the device would let them certify. It needs a full
native rerun on fallback and an audit term in the FFT stage bound. The
per-lag band assumes the stratified audit blocks represent the unsampled
ones for each lag. The frontier self-check tests it exactly, and the oracle
tests it in qualification mode on every lag. The Blackwell qualification
covered the 72 Hz/1× HF Worst slot, the 72 Hz/5× control and the 36 Hz/10×
holdout, all with HF Worst fading. Impulsive or heavy-tailed noise remains
uncovered; add it to the next qualification, on another generation or for a
CUDA equivalent.

#### Blackwell qualification of the gain rank

One full RTX PRO 6000 Blackwell (Xeon 6952P host, 40.8-CPU quota,
80 workers), 21 November 2026.
- **Oracle.** The device oracle's worst lag error was 0.81 of its band at
  the HF shape and 0.73 at 60 Hz. The production admission fit the 256 MiB
  explicit workspace.
- **Explicit device against native, one binary, two alternated pairs:**
  - 72 Hz/1× slot: stage 0.24–0.46 s against 0.19–0.20 s;
  - 72 Hz/5× control: stage 0.57–0.68 s against 26.7–32.6 s, slot
    16.3–16.9 s against 43.4–48.7 s.

  Lags and every evidence value were identical. Band ratios were 0.56 and
  0.26, with no fallbacks.
- **Automatic route with the family flipped, against the same build
  without the flip:**
  - 72 Hz/5× control: 17.8–19.8 s against 43.7–55.5 s in the two flip
    pairs; a third unflipped run took 38.4 s;
  - 36 Hz/10× 100 mW holdout: 281 s against 688 s, process CPU 4,919
    against 21,689 CPU-s, and the signed-lag stage 3.05 against 415 s;
  - 72 Hz/1× slot: 6.56 against 5.58 s, inside the unflipped runs' 5.58–6.61 s.

  `/result` and every evidence value were identical everywhere, and the
  holdout's band ratio was 0.28. On the 72 Hz/5× control the automatic
  route's stage took 1.34–1.47 s against 0.57–0.68 s explicit; see
  [What bounds the challenging slots](#what-bounds-the-challenging-slots).

The predeclared rule asked for a stage gain on the 72 Hz/1× slot too. That
clause was not applied, because the automatic-mode contract does not let
an easy-case loss veto a challenging-case advantage. The slot cost there is
within noise.

Reconsider the flip if a qualified shape shows fallbacks (`band-violated`,
`near-tie`, `frontier-overflow`) at a rate that costs more than the device
saves, or if the band ratio approaches 1.

### Drift proposals

Scope: FP32 ridge and dechirp transforms with their peak frontiers. Every
evidence term stays host FP64.

The planned qualification is proposal-level. Decisions must be identical.
Selection differences against the FP64 control are reported on predeclared
strata.

Status: implemented on Vulkan
([`triad_vulkan_drift.cpp`](../src/triad_vulkan_drift.cpp)), explicit
`--vulkan-drift device` only. Automatic selection declines
`vulkan-drift-unqualified-device` (`automatic_drift_qualified = false`); on
the RTX PRO 6000 Blackwell the device stage was slower than native (0.81–1.65
against 0.36 s on the 72 Hz/1× slot, 2.96–3.28 against 2.23–2.26 s on the
72 Hz/5× control, 80 workers), so it was not qualified. Explicit FP64
declines `fp64-requested`.

**Why the device route is slower.** No device-phase timing separated the
causes, so they are inferred:
- *Serialized lanes (code).* `VulkanDrift::ridge()` and `slopes()` hold the
  session mutex for the whole call, including submission, the fence wait and
  readback, and one session serves both signs and every lane. At W = 80,
  43–48 drift lanes ran, so their device calls ran one at a time. Lane
  timers include the mutex waits, so the lane-summed
  `drift_ridge_seconds` and `drift_slope_seconds` of a device run against
  native show the serialization. On the Radeon's 72 Hz/1× slot the native
  ridge took 1.5–1.75 lane-seconds; no retained device run kept the timer.
- *Host render and upload.* The host keeps the template render and products,
  51–67% of native drift lane-seconds on the 5650U at both 72 Hz shapes, and
  uploads the FP32 products: 117.7 MB on the 72 Hz/1× slot, and modeled 413,
  290 and 175 MB per atom at the HF, 36 Hz and 60 Hz shapes.
- *Little transform work per call.* The Radeon's 72 Hz/1× device run had
  115 evaluations, 65,696 forward and 669 slope FFTs and 3.65e9 FFT
  row-stages, about 12 ms at the in-slot segmented rate of the RTX PRO 6000
  (about 3e11 row-stages/s), against a 0.45–1.3 s loss there. Fixed and
  per-call cost dominate. The route also compiled 8 more VkFFT plans and 6
  more shaders per process.

The route was never timed at the holdout or HF shape. The holdout's native
drift took 50–61 s, and HF evaluations have 2^25-point slope transforms and 7,048
ridge FFTs. The redesign is [lever 2](#next-levers-in-order).
Receipts: `vulkan_drift_*`, with `ridge_cells` and `slope_ffts`. Slot fields:
`drift_proposal_route` (`vulkan_fp32_fft_fp64_ridge`, with
`_with_native_atoms` when an evaluation fell back), `drift_device_atoms`
(evaluations for which a device call published ridge records or slope
frontiers, including one whose later call fell back) and
`drift_retained_digest`, and the same three as `drift_line_*` for the
line-conditioned stage of a retained coherent line, which opens its own
session. The digest is FNV-1a over each sign's set of survivor timing atoms
(epoch, arrival, clock), each with its exactly scored (bit, slope, bin) set
in canonical order, on every route, so selection differences are visible
whatever the order of the timing atoms. The existing
`drift_coherent_timing_fft_rows` keeps its meaning.

**Device route (proposal-qualified).** One session per drift stage serves
both relative-Doppler signs (`score_drift_fine`). The host keeps the render
and products, normalization, every exact correlation, `DriftGram`, the
log-add and survivor selection, and replays the shared frontier, slow
refinement, slope list and peak merge.
- Each lane writes round32(P·2^-e), the native ridge transform input, into
  its own host-visible staging slot outside the session mutex. A product
  outside FP32 range leaves that evaluation native
  (`vulkan-drift-nonfinite-input`).
- Ridge: `drift_segment_column`, batched FP32 VkFFT with FP64-computed
  tables (chunks of 1,024 transforms and a tail plan), `drift_power` (FP64
  power in native order) and `drift_ridge` (a workgroup per slope; the FP64
  sum in segment order with the native skip test; host-rounded offsets as
  int32, clamped only where every center skips; first maximum by ascending
  center, NaN never wins). A plan with slow segments reads each power table
  back for the unchanged refinement, fast then slow. The stage's offset and
  bound tables are immutable for the session and upload once.
- Quotients are correctly rounded by an FMA-residual correction of the
  device division. RADV's raw FP64 division differed from the host by one
  ulp in about one quotient in ten and returned 0 for x/NaN.
- Slopes: a call's slopes go in chunks of the session's widest batch, and a
  shorter remainder in at most one transform of each smaller power of two
  (plans of the widest batch and every power of two below it). Each
  transform is one `drift_dechirp` dispatch over its slots (FP64 turns at
  t = first_t + jΔ with the host-rounded slope × step rate, a quarter-turn
  reduction, FP32 minimax sine and cosine on at most π/4, an FP32 complex
  product), one batched VkFFT and one `drift_peaks` dispatch over its slots
  (FP64 ranks, a stable top eight per thread and workgroup, nominations),
  then `drift_peaks_merge`. Before, every slope list shorter than the batch
  (the rank pass and nearly every survivor list once the zero slope is
  cached) ran one slope per transform and peak dispatch.
- `drift_power` and `drift_peaks` flag any NaN or infinite power or public
  rank, which finite native arithmetic cannot give and the frontier rules
  would otherwise drop silently. A flag, or a record, frontier or
  nomination that is not finite (or the empty -inf), leaves that
  evaluation native (`vulkan-drift-nonfinite-device`) with its outputs and
  work untouched.
- Workspace: per-lane staging, ridge chunk and temporary, per-kind bounds,
  offsets, power and records, the slope batch and temporary, partials,
  frontier, rates and flag, and every plan's twiddle tables. VkFFT builds
  each plan under the full `lut_quote` (80 bytes per row); the session then
  settles the plan's charge to its tables' buffer extents, which must lie
  within a derived bound of 2N + 8 complex entries plus per-upload alignment,
  or the session falls back with `-workspace`. Each axis upload of length
  L_k keeps a stage table of fewer than L_k entries (a radix-2^r stage
  stores r ≤ 2^r − 1 entries per prior length), and each later upload a
  four-step table of L_0⋯L_k ≤ N entries; with uploads of at least two
  rows those products sum below 2N. Plans build in ascending rows
  after every buffer, so the lease is the buffers, every plan's bound and
  the largest plan's quote above its bound. The widest slope batch up to
  the budget, 2^28 rows and 32-bit indices halves to fit `shared/4`
  (automatic, an optional `try_admit`) or the explicit session ceiling. The
  drift plan's host quote is unchanged, so per-atom fallbacks stay
  admissible. Each device lane reserves its own buffers once, at their
  quoted sizes: the ridge records, the slope budget's frontiers and, with
  a slow refinement, the larger of the two power tables it reads back.
  The route's host tables (built only once a session exists;
  a declined or native stage allocates none), its per-lane records and the
  session's host bytes (its compilation, `compiler_host_bound`, on every
  device, plus the part of its lease in host RAM) must fit
  `DriftPlan::device_envelope_bytes` minus the native quote: an ordinary
  plan's other-stage headroom or an extended plan's preserved stage.
  Otherwise the stage stays native before the factory with `-workspace`
  (tables), or after halving the batch to one with
  `-unified-memory-envelope` (unified memory) or `-workspace` (discrete).

**Device oracle (`triad_vulkan_drift`, RADV RENOIR, 21 Nov 2026).**
- Bit-identical to the native C++ definitions: 147,456 power values (both
  tap counts, zero, negative and NaN energies, wide scales), 633 ridge
  records (ties, NaN, whole-row skips), 192 frontier rows and 24
  nominations (ties, NaN, one and several workgroups, every slope in one
  dispatch). The nonfinite flag is set exactly for the cases with a NaN
  power or rank.
- VkFFT FP32 error at most 1.06e-6 of ‖x‖₂ over 2,048 to 2^22 rows
  (FloatPlan 8.0e-7). Batch-count invariance, which the test requires:
  0 differing bits in 1.14 M values, and no differing frontier row between
  one-slope sessions and batch-3, -4 and -8 sessions that split seven
  slopes into sub-batches.
- Dechirp angle error at most 8.4e-8 rad at ±slope radius on the 72 Hz/5×
  and HF apertures, both edges in one dispatch.
- Sessions: synthetic two-tap chirps keep their argmax, top-eight overlap
  16/16 on chirps and on noise, nominations within 3.1e-7, ridge records
  equal the native records of the returned table, which is within 2.8e-7
  of the host FloatPlan table. An infinite staged product makes two ridge
  and two slope calls return false with outputs and work untouched, four
  `nonfinite-device` fallbacks and no session credited.
- Twiddle tables settled at 2,048 to 2^25 rows within the bound: about
  8.1 bytes per row from 2^18 (2^25: 270.5 MB settled, bound 536.9 MB,
  `lut_quote` 2.68 GB). At HF (2^25, slope budget 3) the three slope plans
  (3, 2 and 1) lease about 3.8 GB of tables, against 5.4 GB for the former
  two plans at the full quote (*est.*).

**Complete slot (72 Hz/1×, RADV, W24, 21 Nov 2026).** Device against native
on one binary: the same decision and slot result, 19 timing atoms, 592
slopes, 115 device evaluations and no decline or fallback; the sub-batch
transforms left the digest and evidence unchanged. The retained digest
differs by one survivor atom: the positive sign's bit-1 zero-slope
nomination among three clock-lattice neighbours of one epoch and arrival
(0, +0.00015 and +0.0003 ppm), within 1e-7 of each other. Every other
survivor and every shared atom's retained (slope, bin) set is identical;
timing ranks differ by at most 4.9e-8 relative and zero-slope
log-nominations by 5.3e-7. |Δ drift_bit_evidence| is 8.0e-10 and 4.4e-9.
A drift-only FP64 control (a scratch build that ran the native FP64 drift
instantiation on the same timing atoms; not a receiver option) has the same
decision and keeps +0.0003 ppm at that tie. Its order of the three
neighbours is (+0.0003, +0.00015, 0); the device's (+0.00015, +0.0003, 0)
has one pair reversed, native FP32's (0, +0.00015, +0.0003) all three.
Against FP64, |Δ drift_bit_evidence| is 1.6e-9 and 8.8e-9 for native and
8.0e-10 and 4.4e-9 for the device, and every shared survivor's retained set
is identical on all three routes. On the 72 Hz/1× slot the device is therefore no worse
than native FP32 against FP64. `--backend cpu --native-coarse-precision
fp64` is not that control: it changes the upstream timing atoms (32 of 48
per sign in common). The device drift stage took longer than native on the
Vega (single runs on a loaded shared host, not a timing result), so the
Radeon stays declined. No single-family Radeon drift run of the 72 Hz/5×
control was made; all three routes together were, above. An FP64 tie guard
at the eighth rank and at the nominations would make selection follow FP64
at exactly these near ties. A drift qualification that reports selection
against FP64 needs that scratch build's FP64 drift instantiation exposed as
a qualification hook; the Blackwell run had no FP64 drift control.

**CPU baseline (exact, every route).** The native ridge search reads each
segment row once per block of consecutive native-order slopes (segments
outer, then bit, then each slope's contiguous center window). Every (slope,
center, bit) sum keeps its addends, segment order, open-half-band skip and
first-maximum tie, so records, slope lists and atom ranks are bit-identical
at every block size, including one slope, and equal a direct center-major
reference (`drift_ridge_interchange_oracle`: blocks of 1, 2, 3 and 16, the
cache rule's and one past the slope count, up to 64; receiver drift strata;
5650U 72 Hz/1× native and CPU slots). A block's FP64 sums are at most about
256 KiB: 16 slopes at the largest 1,025-center grid and up to 64 on smaller
grids. They occupy the lane's render tile, idle during the search and
already in the drift quote, so quotes, widths and leases are unchanged. On
the 5650U at the HF ridge shape (1,762 segments × 16,384 bins, 984 centers
at stride 4; a microbenchmark on random FP64 tables with 64 slopes in native
order), interchanging segments and centers alone was 4.0–4.6× faster than
the former center-major loop on one thread but only 1.7–1.9× with six
concurrent lanes: re-reading about 111 MB of row windows (of the ~462 MB
per-lane power table) for every slope is DRAM bound. Blocks of 16 slopes
were a further 3.7× on one thread and about 14× with six lanes (indicative
only: single runs on a loaded shared host), with bit-identical records.
Blocks of 32 were about 10 % faster again with six lanes, and blocks of 8
only about 6× over one slope at a time (a run that repeated four distinct
slopes), so the gain depends on a block's sums staying in the core's cache;
the 256 KiB budget is about half of a 5650U core's 512 KiB L2. These runs
had at most six concurrent lanes, one per physical core, whereas the default
24-worker 72 Hz/1× slot runs 24 drift lanes on 12 hardware threads, so SMT
siblings share an L2. The grid, the native slope order, the four-best ridge
frontier, the slow refinement and the slope list are shared
(`DriftRidgeGrid`, `drift_ridge_slope`, `DriftRidgeFrontier`). A device
ridge supplies only per-slope `DriftRidgeRecord`s in native order. A device
slope route merges each slope's stable top eight (equal ranks by ascending
signed bin, NaN and -inf excluded) through `merge_drift_peak` in scoring
order, which equals the native streaming frontier (tested). The oracle is
the only coverage of the exhaustive `2R+1 <= budget` bank, which no receiver
stratum or the 72 Hz/1× slot reaches.

The stage width follows the worker budget up to the public timing-atom bound
without raising any owner lease. An extended plan narrows to its preserved
lease (long calm W24: 15/14 lanes). An ordinary plan keeps its former eight
lanes and widens only into the largest other stage of the same lease (72 Hz/1×
W24: 24/24 lanes with unchanged `workspace_peak_bytes`; a drift-led W10 slot
stays at eight). Where varying leads, its block batches add headroom (the
480 Hz varying fixture widens from 8 to 18 lanes at W24).
`drift_{render,ridge,ridge_search,slope,exact}_seconds` are lane-summed wall
times, inflated when lanes share hardware threads, and include a
line-conditioned drift stage. On the 5650U 72 Hz/1× control template render
and products was over half the lane-seconds, the ridge search about 15 % and
the segment tables about 8 % (noisy shares). On the Blackwell pod's Xeon
6952P the HF-shape microbenchmark ran blocks of 16 a median 13–14× faster
per thread than the former loop at 8 and 16 lanes (12–23× across threads),
and blocks of 32 stayed within the run-to-run spread of 16, so 16 stays
([validation](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026)).
The 63 ns per load derived from the HF receipt, which ran on an EPYC 9555
pod restricted to 13 CPUs by affinity, was not reproduced on the Xeon,
where the former loop ran at 5–12 ns. Time the benchmark on an EPYC
9555-class CPU to test it. A complete HF slot remains unmeasured, and so do
drift lane counts at W = 80 on an HF-shape (extended-plan) slot; the 72 Hz
slots ran 43–48 of 58 planned lanes. At HF a drift lane quotes about
2.20 GiB (products 0.83 GB, columns 1.07 GB, power table 0.46 GB), so an
extended plan's preserved lease bounds the width. Per sign the 45 rank-pass
atoms take six rounds on eight lanes and the 10 survivors two, with two
lanes busy in the second, and the stored CUDA receipts kept about 4.9 of 13
CPUs busy during drift (from differential pairs of equal-geometry receipts,
[cost model](validation.md#cost-model-of-the-cpu-families)).

### Varying statistics

Scope: the template-side projections of `score_varying_fine`, for the
fixed-affine states (with any central independent atoms) and the tracked
independent-gain atoms. The host keeps template rendering, the observation
projection, the raw-composite tracker, the likelihoods and every
recursion, all native FP64.

**Status: an explicit FP64 Vulkan route, declined in automatic mode.**
`--vulkan-varying-fine device --vulkan-varying-arithmetic fp64` scores on
the device. Automatic selection reports `vulkan-varying-unqualified-device`:
on the RTX PRO 6000 Blackwell the device stage was slower than native
(3.2–3.6 against 0.18–0.30 s on the 72 Hz/1× slot; 13.2 s, and 12.8–13.5 s
with all three families on the device, against 5.0–5.2 s on the 72 Hz/5×
control), so `automatic_varying_qualified` stays false. FP32 templates
(`fp32-template`) have no kernels and decline as
`vulkan-varying-unimplemented`. They are the only lower-precision candidate
for these statistics; qualify them at the evidence level on challenging
slots, where an easy-slot result cannot veto them.

**Why the device route is slower.** The losses are latency, occupancy and
fixed cost, not arithmetic (modeled and inferred):
- The 72 Hz/5× stage models about 9.3e9 FP64 FFT row-stages per slot, tens
  of milliseconds even at the device's 1/64 FP64 rate, against an 8 s loss
  there and a 3 s loss on the 163-block 72 Hz/1× slot. An FP64 device route
  has no arithmetic-rate advantage over a 40-CPU FP64 host.
- On the Radeon, with 6-block batches and 28 calls on a loaded host
  (indicative), `varying_wait_seconds` rose from 0.67 s to 13.6–18.6
  lane-seconds while the statistics phase fell from 14.9–17.2 to 7.5–9.7:
  lanes idle during each batch's device call. Plan seconds were 0.29–0.57 s.
- The retained Blackwell summaries omit `varying_batches`,
  `varying_batch_blocks`, the varying phase timers and the plan and
  compilation seconds, which the probe emitted; the raw receipts were not
  kept, so the loss is unattributed there. The native 72 Hz/5× host batch
  is 172 blocks × 26 batches; at about 23.1 MB per block a 4 GiB explicit
  workspace may have halved the device batch.
- The affine gather runs one 64-lane workgroup per block and item over 27–33
  retained modes, then a six-step FP64 shared-memory reduction of 20 sums,
  so low occupancy per call is plausible.
- Each varying fine stage opens one session, which builds up to four FP64
  twiddle-table plans under the shared queue lock; its per-batch device
  calls reuse them.

The redesign is [lever 3](#next-levers-in-order).

How it runs ([`triad_vulkan_varying.cpp`](../src/triad_vulkan_varying.cpp),
kernels in [`triad_vulkan_varying_shaders.hpp`](../src/triad_vulkan_varying_shaders.hpp)):

- **One session per stage**, admitted by the factory. The widest batch of
  blocks comes first, halving until a lease that never waits holds. The
  host batch follows the admitted batch; the batch schedule never changes a
  varying result. A narrow lease multiplies the stage's executor phases
  (the 72 Hz/1× slot below: 55 batches of 3 blocks against 4 of 52 natively). Reconsider
  splitting one host batch into several device calls if the
  `varying_{projection,statistics,recursion}_seconds` phase timers show
  that overhead.
- **Per batch**, one device call scores the batch beside the tracker's
  leaves. Meanwhile the other leaves render the next batch into the other of
  two host-visible staging slots. The projection phase writes each block's
  projected observation into the current slot.
- **Device arithmetic** mirrors `DftPlan` and `Projection`. Templates are
  rotated by the projected tracker's half bin (the same repeated product),
  multiplied by the chirp and zero-padded. Then come an FP64 VkFFT forward
  transform with FP64 twiddle tables, the product with the host's kernel
  spectrum and the normalized inverse. VkFFT's inverse divides by the
  transform length as `search_fft` does; both forward transforms are
  unnormalized. FP64 kernels then gather the retained modes (times chirp
  and unitary scale), or the shifted bins of each tracked state. They form
  the native correlations, Gram entries, diagonals and cross terms.
  Products are `precise`, so only the device transforms and the order of the
  mode sums differ from native. A final block of another length uses its own
  transforms in the same submission. A power-of-two length skips the
  Bluestein steps.
- **Tables** come from the host: chirp, kernel spectrum, indices, scale and
  each state's half-bin (`projected_half_bin`, shared with the native
  visitor and exported for the checks).

Exactness is class E2. `triad_vulkan_varying` compares the device statistics
with native FP64 (`DftPlan` and the native sums) under a predeclared
norm-relative bound of 1e-12 per statistic group. The states' half-bins are
`projected_half_bin` of a projected-tracker lattice (whole half-bins of the
block length, rounding to either parity on a longer final block). Atom 0 is
a zero pattern, atom 1 a rank-one pair and later atoms random; the check
requires nonzero reference cells for every atom after the first. On the
Radeon (RADV RENOIR) the largest errors were:

| Shape | Affine groups | Tracked cells |
|---|---|---|
| 200/239 rows, 512-row transforms, 54 states, 4 atoms × 27 states | 8.6e-15 | 1.3e-14 |
| 1200/1505 rows, 4,096-row transforms, 48 states (one-tap models), 5 atoms × 75 states | 7.3e-14 | 9.9e-14 |
| 256 rows (power of two) beside 300, with central atoms | 2.1e-14 | – |
| 256 rows without a final segment (no Bluestein steps), 3 atoms × 13 states | 2.5e-15 | 5.5e-15 |
| 256 rows beside a Bluestein 300-row final block, 3 atoms × 11 states | 1.6e-14 | 2.8e-14 |
| 240 rows without a remainder, 3 atoms × 9 states | 7.3e-15 | 1.3e-14 |

Zero templates stay exactly zero. A rank-one Gram's raw Schur complement is
at 1e-15 of its echo energy. A fragile one, at 3e-14, differs by 4–5%
between routes: that is the entries' FP64 resolution. The likelihoods add
the identity and prior terms, so the complete slot below bounds the
evidence effect.

The same check drives the receiver's route through `score_varying_fine`,
with a test backend that wraps the Vulkan backend and can make chosen calls
fall back (a NaN projected mode, rejected before device work). It covers
tracked atoms (480 Hz, 190 blocks of 48 rows, 11 states) and central atoms
(2400 Hz near Nyquist, 190 blocks of 240 rows, an empty projected lattice),
each in 5 batches:

- every call falling back reproduces the native route bit for bit, which
  also matches the receiver without a backend;
- the device route keeps every counter, status and decision, with largest
  absolute evidence |Δ| 1.5e-11 and 5.5e-10 nats (the central evidence is
  about 1.8e5 nats, so 3e-15 relative); the bound is 1e-8;
- alternating fallbacks report `vulkan_partly_native` within the same bound;
- host memory outside the envelope falls back before the factory.

The complete 72 Hz/1× slot ([arguments](#qualification-slots), `--backend vulkan
--vulkan-segmented-timing native`, device fp64 against native, same build)
gave:

- the same quiet decision under all six policies;
- identical work counters, quiet and drift evidence, and a bit-identical
  raw-composite tracker;
- largest |Δ| 3.0e-14 nats over `varying_bit_evidence`, the signed-lag
  terms and the new `native_slot.work.varying_components` (fixed-affine
  and nominal independent-gain terms). The acceptance bound is 1e-8;
- `varying_fine_route` `vulkan` and `varying_fine_arithmetic`
  `vulkan_fp64_fft_fp64_state`, with zero declines and fallbacks;
- 3 blocks a call: the Radeon's unified-memory envelope holds the
  compilation and the 3-block lease (71,457,568 bytes) but not the 6-block
  one (140,774,400 bytes), so it lies between 205.7 and 275.0 MB. That gives
  55 calls, 39,120 forward and inverse transforms, 248,412 score rows, and
  four FP64 plans and four shaders compiled on a cold cache.

The 1e-8-nat bound is absolute. It is the acceptance bound for these
fixtures and named slots only; the 72 Hz/1× slot's varying evidence is about 10 nats. The
shape-general criterion is the norm-relative bound of 1e-12 per statistic
group above. Rescale the absolute bound before a qualification slot whose
evidence exceeds about 1e6 nats, where roundoff of 3e-15 relative reaches
3e-9 nats.

`varying_fine_seconds` was 2.39 s on the device at 3 blocks a call (1.16 s
at 6 before the compilation was charged) and 0.63 s natively, one run each
on a shared host. That is an indication, not a timing result. Keep the
Radeon declined; reconsider it only on a measured 72 Hz/5× complete-slot
win.

Workspace:

- **The native quote is unchanged.** The device route only lowers the host
  batch.
- **The device lease** holds both staging slots (FP64 templates of every
  device item, projected observations, rendered nonzero-basis flags and the
  stage constants), the transform data and VkFFT temporary, the tables and
  the results. It also holds the FP64 twiddle-table quote of each plan.
- **Leases never wait.** The limit per owner is a quarter of the shared
  workspace when automatic, or the explicit workspace. An explicit
  workspace's optional leases share its ceiling (`Shared::try_acquire`).
  The lease grows by about 23.1 MB per block on the 72 Hz/1× slot's shape, so its 52-block
  host batch needs about 1.2 GB. Qualification and timing runs on a
  discrete device must size `--device-memory-limit-bytes` for the target
  batch; the probe's 256 MiB default admits 6 blocks there.
- **Host memory.** A session compiles up to four FP64 plans and four
  shaders, one at a time, on any device. `stage_host_bytes` quotes the
  coarse sessions' `compiler_host_bound` (128 MiB) for that on every device,
  plus the full lease on unified memory, or elsewhere the host-read result
  buffer and one table staging copy where those use system memory. The
  receiver halves the batch until
  the quote fits `VaryingFinePlan::device_envelope_bytes`, which is the
  supplemental stage minus the varying quote, set by the receiver's quote.
  Otherwise the stage falls back as `vulkan-varying-unified-memory-envelope`
  (unified memory) or `vulkan-varying-workspace` (discrete). Where the
  varying quote is itself the largest supplemental term, the envelope is
  zero and every device falls back. Reconsider quoting the compilation in
  the supplemental stage, or a measured smaller bound, if a qualification
  meets such shapes.

Declines and fallbacks:

- The receiver's own declines come before the factory: `diagnostic` (span
  or clock shadow), `line`, `coherent-gain` and `projected-tracker` (Helper).
  They follow the shape-independent policy (native or FP64 requested,
  forced host scoring, missing FP64/INT64 shaders, automatic
  qualification) and precede the shape check. A shape-policy decline is
  recorded by its factory call, which must admit nothing.
- A coherent-gain plan has no fixed-affine states or tracked atoms, so its
  shape has no device items. Automatic selection reports it as
  `vulkan-varying-unsupported-shape`. An explicit device route records
  `vulkan-varying-coherent-gain`, or `vulkan-varying-line` for the
  line-conditioned coherent-gain stage. `triad_fine_session` checks these
  reasons on the automatic, device and native routes with a device-free
  backend that applies the Vulkan policy.
- A refused lease falls back as `workspace`. An untransformable plan throws
  on an explicit device route.
- Per call, a nonfinite projected observation (`nonfinite-input`, before
  device work) or a nonfinite device statistic (`nonfinite-device`, after
  it) makes the call return false with outputs and counters untouched.
  That batch's affine and atom statistics then run natively. The slot
  reports `varying_fine_route` `vulkan_partly_native` when some other batch
  ran on the device. When every call fell back it reports `native` and
  `native_fp64`, and the fallbacks appear only in
  `vulkan_varying_native_fallbacks` and `vulkan_varying_fallback_reason`.

Receipts (`vulkan_varying_*`):

- `sessions` is credited at the first validated call.
- Forward and inverse transforms count each plan's whole batch, including
  the stale columns of a shorter last call.
- `score_rows` counts published groups: one per block and affine item, and
  one per block, atom, bit and state.
- `upload_bytes` counts the bytes the host wrote for the device: each
  block's template columns at its own rows and its projected modes, plus
  the tables once.
- `blocks` and `batches` count device-scored blocks and calls.

The native route reports `native` and `native_fp64`.

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
| Broad changing-channel timing search | `TensorCoarse::plan_segmented` chooses shapes for FP32 cuFFT. The [CUDA scoring kernels](../src/triad_tensor_peaks.cu) track candidate motion and rank lags on device. Check `cuda_segmented_sessions`, `cuda_segmented_inverse_ffts`, `cuda_segmented_score_rows` and `cuda_segmented_native_fallbacks`. The [Vulkan session](../src/triad_vulkan_backend.cpp) runs the same kernel arithmetic with VkFFT FP32 transforms and GLSL kernels, also for the Blackwell Vulkan/OpenCL route. Automatic selection admits it on the OpenCL-identified SM 12.0 GPU since its [Blackwell qualification](validation.md#blackwell-qualification-of-the-offload-routes--20-november-2026). Check `vulkan_segmented_sessions`, `vulkan_segmented_declined_calls` with its reason, and `vulkan_segmented_native_fallbacks` with its reason. [Admission, proposal changes and Radeon results](gpu-precision-controls.md#current-implementation). |
| Fine tracking and decisions | The CUDA `TensorFine` class accelerates eligible bulk basis mapping, Gram products and statistics. Check `mapped_values`, `statistics_blocks`, `cuda_direct_gram_values`, `cuda_bmm_gram_values` and `native_fine_sessions`. The native receiver owns chronological state, bit and quiet decisions, codec and completion. For simulator generation time, read the separate `simulator` phase. [Receiver interfaces](robust-current.md#native-interfaces-and-ownership). |
| Filtered Blackwell fine projection | The [OpenCL route](../src/triad_opencl_nvfp4_projection.cpp) renders mapped tiles, projects DATA templates and returns sufficient statistics. `opencl_resident_nvfp4_modes` witnesses the Tensor Core projection and `opencl_resident_fft_modes` witnesses the alternate FFT. `opencl_resident_fp32_mapped_columns`, `opencl_resident_fp64_mapped_columns`, `opencl_resident_fp32_rendered_tiles` and `opencl_resident_fp64_rendered_tiles` name the tile and mapping arithmetic. `opencl_resident_fine_pairs`, `opencl_resident_upload_bytes` and `opencl_resident_download_bytes` describe paired submissions and resident transfers. [Receiver timings and limits](validation.md#blackwell-resident-fine-processing--19-november-2026). |
| Blackwell dense coarse search | The [OpenCL session](../src/triad_opencl_nvfp4_coarse.cpp) renders the template bank, scores every row with E2M1 Tensor Core products and rescores retained arrivals. `opencl_coarse_score_rows` witnesses the rows it scored, `opencl_coarse_rescored_peaks` the FP32 pass, with `opencl_coarse_rescore_fp32_partial_peaks` and `opencl_coarse_rescore_fp64_peaks` naming its accumulation, `opencl_coarse_synthesized_columns` and `opencl_coarse_uploaded_columns` the template source, and `opencl_coarse_frequency_fallbacks` the sessions that kept the FFT route, with `opencl_coarse_fallback_counts` naming each one's reason. `opencl_coarse_issued_products` and `opencl_coarse_useful_products` count real multiply-adds. Device intervals appear only on the explicit profiled route, with synthesis divided into render, filter and Gram parts and selection's rescoring part reported separately. [Receiver timings and limits](validation.md#blackwell-dense-fp4-coarse-search--20-november-2026). |
| Signed-lag ranks | On the OpenCL-identified SM 12.0 class, or with explicit `--vulkan-gain-rank device`, the [device session](../src/triad_vulkan_gain.cpp) ranks unsampled blocks in FP32 and the host certifies the frontier in FP64. Check `vulkan_gain_rank_sessions`, `vulkan_gain_rank_score_rows`, `vulkan_gain_rank_fft_row_stages`, `vulkan_gain_rank_qualified_device`, `vulkan_gain_rank_band_ratio`, `signed_lag_gain.proposal_route` and `signed_lag_gain.device_fft_row_stages`. [Port and limits](#signed-lag-ranks). |
| Drift and varying fine, explicit only | `--vulkan-drift device` and `--vulkan-varying-fine device` run the [drift](../src/triad_vulkan_drift.cpp) and [varying](../src/triad_vulkan_varying.cpp) sessions; automatic selection keeps both native. Check `vulkan_drift_*`, `drift_proposal_route`, `vulkan_varying_*` and `varying_fine_route`. [Ports](#fine-stage-device-ports). |
| Vulkan on integrated Radeon | The [Vulkan backend](../src/triad_vulkan_backend.cpp) runs supported frequency search via VkFFT or direct products. `vulkan_dispatches` and the per-route score-row counters identify actual work. The qualified Vega FP16 direct route uses packed vector arithmetic. VkFFT half storage still computes in FP32. Native CPU retains Helper, lag groups and fine processing. [Vulkan stage scope](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics). |

CUDA-selected builds, including the RunPod simulation build, run no
fine-stage family, dense FP4 search or resident fine route
([route table](#what-automatic-selection-runs)).

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

On the lab's Vulkan and OpenCL build, `build/lab/triad_receiver_probe describe
--backend gpu` checks the selected device, and this reads its work:

```sh
jq '.identity.backend_receipt | {device_name, vulkan_segmented_sessions,
  vulkan_segmented_decline_reason, vulkan_gain_rank_sessions, vulkan_gain_rank_decline_reason,
  vulkan_drift_decline_reason, vulkan_varying_decline_reason, opencl_coarse_score_rows,
  opencl_coarse_fallback_counts, opencl_resident_nvfp4_modes, opencl_resident_fp32_mapped_columns}' result.json
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
| GeForce RTX 5090 and full RTX PRO 6000 Blackwell | Blackwell, compute capability 12.0 for these devices. Confirm the exact full device and compiled architecture at runtime; do not transfer an SM100-specific path or an earlier GPU's shape results to SM120 by product name. Recheck library heuristic choices and actual receiver costs on both consumer and workstation devices. Automatic selection treats every NVIDIA device that OpenCL reports as compute capability 12.0 with a matching UUID as the qualified class, so an RTX 5090 inherits the RTX PRO 6000's routes unmeasured; an RTX 5090 run is the check. |
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
[Matched receiver trials](validation.md#blackwell-receiver-trials-at-2-mw).

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
| NVFP4 correlation and fused reconstruction | The historical Triton result remains a proven scoped win. A new native cuBLASLt analogue was removed: single-product FP4 failed the 1% score gate (5.03% error), and four-product/residual-scale repairs changed a unique noise peak. Active bulk costs improved against FFT but did not establish an advantage over qualified INT8. This does not reject other FP4 designs or transfer the historical result to current C++. E2M1 products now run in the [dense coarse search](#dense-fp4-coarse-search-and-device-template-bank) and the fine 47-mode projection ([where FP4 fits](#where-fp4-fits-and-the-larger-levers)). |
| Split-BF16 / TF32x3 phase contractions | These arithmetic modes remain absent. The old phase-state contraction does not map directly onto current Gaussian tracking. The high/residual principle was tested in the removed FP4 analogue, using actual decoded residuals, HH/HL/LH/LL products and FP64 reconstruction. Its noise-peak failure leaves the historical split-BF16/TF32x3 findings intact. |
| Fused FP16 packing and tiled products | Native scaled packing and FP32-accumulating products implemented; include all packing, setup and reconstruction when qualifying the path. The historical complete-receiver loss remains relevant. |
| K32 partials and explicit outer accumulation | Bounded-partial principle implemented with complex chunks of at most 512 samples, INT32 sums for INT8 and FP64 outer reconstruction. This is different arithmetic from the historical FP8 K32 result and needs its own numerical gate. |
| Scaled FP16 lag-domain DFT | Shape-qualified native coarse lag-group analogue implemented after DATA channelization, with shared coefficients and full FFT fallback. Historical transform wins did not establish an integrated win. |
| Fused phase stencil, graphs and register reducer | Current fused mapping, direct small Gram reduction and coalesced readback preserve fusion/reduction principles. Exact CUDA arrival-peak frontiers pass device fixtures and the full 8,596-frequency active/noise public-slot checks; a bounded bulk test measures the transfer/selection improvement. Sustained backlog and CPU forecasting remain separate. The old stencil belongs to a different model; CUDA graph replay is still absent. |
| Persistent whole-scan graph workspace | Exact descriptor and source-tile caches preserve part of the residency lesson; they are not graph replay. [Resident windows](#device-rendered-fine-tiles-and-resident-windows) now keep post-render projection and statistics on the device without graph capture; graph replay is still absent. |
| Mixed precision and FP64 reduction organization | FP32 bulk with FP64 coordinates, statistics and posterior evidence is retained. The historical two-level reducer is not established as the same current implementation, so its ratio does not transfer. |
| Prepared Gaussian design and simulator caches | Persistent native mapped bases, projection plans and device tiles are retained. Exact projection/offset descriptor reuse and shared block-pair transition preparation extend this lesson. Simulator truth remains outside receiver input. |
| Cached direct-GEMM acquisition bank | Prepared native lag-group matrices and batching across workspaces/arrivals provide a shape-specific analogue. They evaluate every public hypothesis and do not replace broad FFT acquisition with an unbounded dense bank. The [dense FP4 search](#dense-fp4-coarse-search-and-device-template-bank) with a device bank replaces the FFT route within admission for qualified geometries. |
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

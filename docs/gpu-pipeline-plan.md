# GPU-resident pipeline: implementation plan

This plan turns the [concepts to try later](gpu-strategy-playbook.md#concepts-to-try-later-move-the-pipeline-onto-the-gpu)
into ordered, bounded work. It is written for an agent that starts without
earlier context. Each phase lands independently and leaves the tree
releasable.

**Lifecycle.** When a phase lands, record its outcome in
[validation](validation.md) and the [GPU guide](triad-gpu-optimization.md),
then delete the phase from this page. Record a failed phase as a negative
finding with its reconsideration condition, then delete it too. Delete this
page when no phase is left.

## Why this work

- The RTX PRO 6000 and RTX 5090 offer about 100–126 TFLOP/s of FP32 and
  packed FP16 vector arithmetic beside their Tensor Cores
  ([hardware facts](gpu-strategy-playbook.md#hardware-facts-for-decisions)).
  On the challenging slots Tensor Core work is about zero and most of that
  vector capacity is idle.
- On the 36 Hz/10× holdout's automatic route, native drift and varying fine
  take 151 s of 281 s (RTX PRO 6000, 40.8-CPU host). Removing both entirely
  would give at most 2.2×. On the hardest HF receipt the three CPU families
  take 1,687 s of 3,707 s
  ([where the time goes](triad-gpu-optimization.md#where-the-time-goes)).
  A later automatic Vulkan run on a 13.6-CPU Blackwell host measured
  88.89 s in segmented timing and 64.96 s in native drift and varying out
  of 169.48 s. [Bounded attribution](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026).
- A Blackwell command sample puts segmented rank at 42–59% on the 72 Hz/5×
  geometry and inverse VkFFT at 25–31%. Ranking also takes 69% of the
  sampled 32,768-row holdout commands. A contiguous tracking layout
  helped the control by 2–3%, with no measured wide gain in one pair.
  Separate rank arithmetic and traffic at the target geometry before a new
  layout or FFT change. [Command profile](validation.md#inside-vulkan-segmented-timing),
  [paired layout check](validation.md#segmented-tracking-rank-reuse--22-november-2026).
- Guarded reciprocal normalization of the rank shader reduced segmented
  wall time by about 22% on a matched 72 Hz/5× active and noise pair. Its
  generated code retains division outside the normal reciprocal range, and
  its changed rounding still needs wider qualification. This is a separate
  device arithmetic opportunity from moving host preparation.
  [Normalization diagnostic](validation.md#segmented-rank-normalization-diagnostic).
- The input is a low-rate baseband stream, tens of MB per slot. The data
  derived from it and moved between host and device is hundreds of GB on the
  HF slot. Generating derived data on the device removes that traffic and the
  per-call round trips.

### GPU work distribution and host CPU demand

Assign workgroups batches of public frequency, timing and drift hypotheses.
Share channelized observations, keyed source tiles and spectra across the
batch. Keep the intermediates and bounded tracking state on the device, and
advance each recurrence in block order. Independent hypotheses and ready
slots supply parallel work. Thin host control still handles audio, ordered
commits and codec work. A persistent device work queue may help if dependency
or dispatch gaps remain after ordinary batching. Workgroups running different
whole receivers can duplicate transforms and carry large private state. GPU
thread, register and shared-memory limits affect the number of ready groups.
[Blackwell tuning guide](https://docs.nvidia.com/cuda/blackwell-tuning-guide/),
[device-side task-graph study](https://hpcrl.github.io/ICS2025-webpage/program/Proceedings_ICS25/ics25-66.pdf).

In the earlier matched wide pair, moving varying statistics to Vulkan reduced
receiver process CPU from 1,863 to 1,367 CPU-seconds, about 27%, while total
slot service stayed near 193 s. The device route still staged 36.91 GB of
logical templates for an input of 10.87 million samples (174 MB as complex
FP64). It still rendered those templates on the host. Measure smaller CPU
allocations with the same public work, memory caps and GPU. Keep the default
`W = 2*H` at each allocation. Those isolated slots can identify host demand;
an ordered backlog test on the same stream determines sustainable service.
[Matched wide pair](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026).

On one fixed Blackwell host the wide slot finished in 217.35 s with eight
physical cores available and 196.31 s under the 27-CPU quota. Explicit drift
and varying work on the eight-core run reduced process CPU by another 6.9%
and took 224.57 s. Both fine families still depend on host-built products or
templates. Transition the producer and its inputs to device ownership along
with the kernels, and measure the effect at the smaller host allocation.
[Same-host CPU allocation measurements](validation.md#host-cpu-allocation-with-a-fixed-blackwell-gpu--22-november-2026).

## Read first

1. `AGENTS.md`, all of it.
2. Playbook: [Start here](gpu-strategy-playbook.md#start-here),
   [Common mistakes](gpu-strategy-playbook.md#common-mistakes) and
   [Fact lookup](gpu-strategy-playbook.md#fact-lookup).
3. GPU guide: [Current GPU limits and next steps](triad-gpu-optimization.md#current-gpu-limits-and-next-steps)
   and [Find and qualify a GPU improvement](triad-gpu-optimization.md#find-and-qualify-a-gpu-improvement).
4. The family section you are about to change:
   [signed-lag ranks](triad-gpu-optimization.md#signed-lag-ranks),
   [drift proposals](triad-gpu-optimization.md#drift-proposals) or
   [varying statistics](triad-gpu-optimization.md#varying-statistics).

## Ground rules

- **Constraints that never change:** every public hypothesis, the configured
  calendar and keyed ordinals, physical quiet-slot completion, the `W = 2*H`
  default, a charged lease for every byte of host and device memory, and
  explicit decline and fallback reporting.
- **Exactness classes**
  ([definitions](gpu-strategy-playbook.md#exactness-classes)). The gain
  rank publishes only certified lags equal to native. Drift may change
  proposals at near ties but not decisions; its evidence stays FP64. Varying
  FP64 statistics differ by roundoff only. Lower precision for evidence needs
  an evidence-level qualification.
- **New device work starts on an explicit route.** Automatic selection keeps
  declining it (`automatic_<family>_qualified = false`) until a full
  RTX PRO 6000 qualification passes. Never flip a default from local results.
- **Keep the native stage.** It is the reference and the fallback. Do not
  delete or weaken it.
- **Spending.** Confirm that existing user authorization covers the planned
  rental and cost cap. Ask before exceeding its scope. Keep prices, provider
  identifiers, pod names, UUIDs and private paths out of the repository.
- **Git.** Work on `main`. No branches or worktrees. Commit and push only
  when the user asks.
- **Timing.** Never time beside a build, a test run or another agent. Use
  [paired timing and load gating](triad-performance.md#paired-timing-and-load-gating):
  alternated pairs, a predeclared rule, one binary for route comparisons.
- **One step at a time.** Make the smallest change, run its checks, then
  continue. If a step fails its checks three times, stop, write down what
  failed, and ask the user.
- **Decide on challenging slots.** A loss on the easy 72 Hz/1× slot does not
  veto a gain on the 72 Hz/5× control, the holdout or HF. It must stay a
  small loss.

## Where things are

| Piece | Location |
| --- | --- |
| Stage interface: `GainRankBulk::direct_ranks`, `DriftBulk::{staging, ridge, slopes}`, `VaryingBulk::{template_staging, statistics, batch_blocks}`, the factories `make_gain_rank`, `make_drift`, `make_varying`, `stage_decline`, `stage_host_bytes`, `record_stage_native`, and the receipt structs | [`src/triad_receiver_bulk.hpp`](../src/triad_receiver_bulk.hpp) |
| Vulkan device, leases, plan and shader caches, the single compute queue, receipt accumulation (`add_stage_work`) | [`src/triad_vulkan_core.hpp`](../src/triad_vulkan_core.hpp), [`src/triad_vulkan_core.cpp`](../src/triad_vulkan_core.cpp) |
| Family device sessions and their `automatic_<family>_qualified` constants | [`src/triad_vulkan_gain.cpp`](../src/triad_vulkan_gain.cpp), [`src/triad_vulkan_drift.cpp`](../src/triad_vulkan_drift.cpp), [`src/triad_vulkan_varying.cpp`](../src/triad_vulkan_varying.cpp); shaders in the `*_shaders.hpp` beside them |
| Selection policy shared by the families | [`src/triad_vulkan_stages.hpp`](../src/triad_vulkan_stages.hpp) |
| Native stages and where they call a device session | `rank_gain_timing` in [`src/triad_receiver_coarse.cpp`](../src/triad_receiver_coarse.cpp); `open_drift_device`, the drift stage and `score_varying_fine` in [`src/triad_receiver_fine.cpp`](../src/triad_receiver_fine.cpp); stage order and leases in `score_stages` in [`src/triad_receiver.cpp`](../src/triad_receiver.cpp) |
| OpenCL NVFP4 route (dense FP4 coarse search, resident fine) | `src/triad_opencl_*`; kernels in [`src/triad_opencl_nvfp4_kernels.cu`](../src/triad_opencl_nvfp4_kernels.cu). Needs NVCC and an SM 12.0 device to run |
| CUDA route | `src/triad_tensor_*` |
| Probe: flags, receipts, JSON keys | [`tools/triad-receiver-probe.cpp`](../tools/triad-receiver-probe.cpp); receipt validation in [`tools/triad_native.py`](../tools/triad_native.py) |
| Device tests | `test_triad_vulkan` modes `--core`, `--gain-rank`, `--drift`, `--varying`, `--segmented`, `--segmented-benchmark`; sources `tests/triad_vulkan_*_checks.cpp` |
| Receipt comparison | [`tools/triad_receipt_compare.py`](../tools/triad_receipt_compare.py) |
| Ridge benchmark | [`tools/triad_drift_ridge_bench.cpp`](../tools/triad_drift_ridge_bench.cpp) |

**Adding a receipt field** touches five places: the struct in
`triad_receiver_bulk.hpp`; `add_stage_work` in `triad_vulkan_core.hpp` and
`copy_stage_work` in `triad_receiver_bulk.hpp`; the probe's JSON; the lists in
`tools/triad_native.py`; and the Python tests. New fields are zero or empty
on routes that do not use them.

## What runs where

- **Any Vulkan GPU, including the development Radeon:** every explicit
  `--vulkan-gain-rank`, `--vulkan-drift` and `--vulkan-varying-fine`
  `device` route runs. Develop and check correctness here. Its speed decides
  nothing.
- **Not locally:** the OpenCL NVFP4 kernels. Locally they have emulated
  tests and a syntax-only build. They run only on an SM 12.0 device.
- **Rented RTX PRO 6000:** qualification only, at the checkpoints below.

## Commands

Build and test, default tree:

```sh
./build.sh test triad
./build.sh test fast
```

Device tree ([details](building.md#vulkan-and-opencl-gpu-builds)). Reuse a
compatible existing tree under `build/` if there is one, and substitute its
name below:

```sh
./build.sh configure --cli --build-dir build/dev-vulkan --jobs 2 -- -DDATAPUMP_TRIAD_VULKAN=ON -DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON
cmake --build build/dev-vulkan --target all datapump-tests-triad datapump-tests-gui --parallel 4
ctest --test-dir build/dev-vulkan --output-on-failure --parallel 2 -L '^triad$' -LE native_gui < /dev/null
ctest --test-dir build/dev-vulkan --output-on-failure --parallel 2 -R '^(triad_vulkan|gui_robust_gpu)' < /dev/null
python3 -m unittest tests.test_triad_abbreviated tests.test_triad_gpu_bundle
```

The 72 Hz/1× slot, the local workhorse (input `7e8ef8fa…`, about 24 s on the
5650U; write the JSON to a file under ignored `local-evidence/`):

```sh
build/dev-vulkan/triad_receiver_probe admission --case hf --memory-limit-bytes 9000000000 --encrypt on --helper off --data-power-w .2 --path-loss-db 180 --noise-dbm-hz -164 --timing-policy broad --base-oscillator gpsdo-xo --shift-oscillator gpsdo-xo --spread-preset hf-worst --selective-perturbation each-65 --channel-model gui --margin 3 --fhss off --burst-wait-tiles 0 --rf-hz 30000000 --rate 72 --added-symbols 1 --seed 940600 --bit 0 --stratum active --max-samples 40000000 --deadline-ms 900000 --backend vulkan --vulkan-segmented-timing native --require-accelerator on < /dev/null > local-evidence/NAME.json
```

Add `--vulkan-drift device`, `--vulkan-gain-rank device`, or
`--vulkan-varying-fine device --vulkan-varying-arithmetic fp64` for a device
route. The other slots are in
[Qualification slots](triad-gpu-optimization.md#qualification-slots). The
72 Hz/5× control takes about 4 minutes and 5 GB locally: run it once per
phase, not per edit.

Compare two receipts:

```sh
python3 tools/triad_receipt_compare.py BEFORE.json AFTER.json            # exact
python3 tools/triad_receipt_compare.py NATIVE.json DEVICE.json --scope slot
```

**Known answers on the 72 Hz/1× slot** (Radeon, Vulkan build):
- signed-lag lags 1213, 1708, 2572, 3084 on every route;
- drift digest `a626c2e80c62feef` native and `eb829892ab5ca438` on the
  device route, with the same decision;
- varying device evidence within 3.0e-14 nats of native.

## The gate every code step must pass

1. Both trees build with no new warnings.
2. The test groups above pass.
3. **Default routes are unchanged:** the 72 Hz/1× slot on `--backend vulkan`
   (native routes) and on `--backend cpu` compares identical to a receipt
   taken before the step. New fields may appear; no existing value may
   change. Run the 72 Hz/5× control once per phase.
4. **The device route keeps its class:** same `/result` as native on the
   72 Hz/1× slot, and the family's own limit (lags equal; decisions equal;
   evidence within 1e-8 nats).
5. The device route reports zero declines and zero fallbacks where it is
   meant to run, and a named reason everywhere else.

A step that fails gate 3 is wrong, however fast it is.

## Phases

Order is by value on the challenging slots, then by how much can be checked
without a rented GPU. [Decision points](#decision-points) say when to
reorder.

### Phase 0: baseline, no code change

1. Build both trees and run all test groups.
2. Take baseline receipts of the 72 Hz/1× slot: native Vulkan, CPU, and each
   device route. Check the known answers above.
3. Take one 72 Hz/5× native receipt.

**Done when** the known answers reproduce. If they do not, stop and report:
the tree or the device differs from the recorded state.

### Phase 2: native stages at a large worker count (CPU only)

**Why.** On the 40.8-CPU pod at W = 80 the native signed-lag stage of the
72 Hz/5× control took 20–38 s against 8.3 s on the 12-thread 5650U at
W = 24, and native varying fine was slower too. Drift scaled normally
([where the time goes](triad-gpu-optimization.md#where-the-time-goes)). This
slows every route that keeps those stages native, including CUDA builds.

**Steps.**
1. Try to reproduce locally: the 72 Hz/1× slot with `--workers 48` and
   `--workers 96` against the default. Compare the signed-lag and varying
   stage seconds. The 72 Hz/5× control needs more memory than the 5650U has
   at those counts. A null result is likely: on the pod the 72 Hz/1× slot's
   signed-lag stage was not slower (0.19–0.26 s at W = 80). The check is
   cheap, so run it anyway.
2. If it reproduces, find the serialization with thread-state samples or
   section timers. Candidates: static round-robin lanes, per-batch phase
   barriers under oversubscription, and the mapped-basis mutexes.
3. If it does not reproduce, add it to the next checkpoint: the 72 Hz/5×
   control at full quota, beside a run restricted to 13 CPUs by affinity.

**Checks.** The gate; results must stay bit-identical.

**Stop rule.** Do not lower `W = 2*H`. Fix the serialization or record it.

### Phase 3: drift on the device, end to end

**Why.** Drift is 60.9 s of the 281 s holdout and 899 s of the HF slot. The
present device route lost at both 72 Hz shapes: 0.81–1.65 s against 0.36 s,
and 2.96–3.28 s against 2.23–2.26 s (RTX PRO 6000, W = 80).

Take the sub-steps in this order. Run the gate after each.

- **3a. Remove the serialization.** One session mutex orders every lane's
  calls (`triad_vulkan_drift.cpp`). Give each lane its own command buffer and
  staging slot with shared plans, or collect independent rank-pass atoms from
  all lanes into one batched submission. Per-lane contexts multiply twiddle
  tables, about 270.5 MB per 2^25-point plan, so share plans.
- **3b. Upload the observation once per slot.** Keep it resident in the
  session. Stop staging per-atom products that the device could form.
- **3c. Render on the device.** Render the rank-pass templates and form the
  products in FP32 per-row arithmetic with FP64 or integer phase and
  coordinates, as the resident fine mapper does. GLSL FP64 has no `sin`,
  `cos`, `exp` or `log`: reduce the argument in FP64 or fixed point and
  evaluate in FP32, as `drift_dechirp` does. Proposal-level only; the exact
  FP64 survivor pass stays on the host.
- **3d. Exact CPU changes:** compare sharing the direct-path render between
  the two Doppler signs and running both signs' survivor passes concurrently.
  Each must preserve the exact evidence. The native route already reuses the
  rank-pass slope lists for surviving timing atoms. An opened device
  proposal session still evaluates its existing per-call fallback policy.
- **3e. Optional: the exact FP64 pass on the device.** Only if Phase 1 shows
  the host pass bounds the stage after 3c.

**Checks.** The gate. Extend `test_triad_vulkan --drift` for each new kernel:
compare with the native record bit for bit where the arithmetic is FP64, and
within a stated bound where it is FP32. Keep the batch-invariance check.

**Stop rule.** If after 3a and 3b the explicit device stage is not faster
than native in both pairs on the 72 Hz/5× control at a checkpoint, stop
before 3c and record the finding.

### Phase 4: varying statistics on the device

**Why.** Varying fine is 89.9 s of the holdout and 401 s of the HF slot. The
device route lost: 3.2–3.6 s against 0.18–0.30 s, and 13.2 s against 5.0–5.2 s.

- **4a. Split one host batch into several device calls when workspace is
  binding.** A narrow device lease shrinks the host batch, which multiplies
  executor phases. Keep the host batch and loop device calls inside it.
  The measured 72 Hz/5× control used the same 172-block width on both paths.
  Reserve this change for shapes where a narrower device lease is measured.
- **4b. Render templates on the device** instead of uploading FP64 templates
  (about 562 GB per HF slot, modeled).
- **4c. `fp32-template` arithmetic.** It has no kernels and declines as
  `unimplemented`. This lowers evidence precision, so it needs the
  evidence-level screen of the playbook's
  [eligibility procedure](gpu-strategy-playbook.md#eligibility-procedure),
  per-block FP64 fallbacks, and a native rerun near the decision margin. Do
  it last.
- **4d. CPU side: a linear-domain scaled recursion** for the Markov part.
  It changes arithmetic and needs its own qualification.

**Stop rule.** As Phase 3, measured on the 72 Hz/5× control and the holdout.

### Phase 5: one device-resident slot context

**Why.** After Phases 3 and 4 each family still uploads its own copy of the
slot's input.

**Steps.**
1. Add a per-slot device object in the Vulkan core. It holds the observation
   and, later, the channelized series and shared spectra.
2. Give it one lease for its lifetime, charged in the slot's quote, with a
   named fallback when the lease is refused.
3. Let the gain, drift and varying sessions bind it instead of staging their
   own input. Change one family at a time.
4. State the multi-owner rule: one context per live slot owner, bounded by
   the shared workspace. No context may wait on another.

**Checks.** The gate; upload bytes per slot fall; `workspace_peak_bytes`
changes only by the declared quote.

### Phase 6: let FP4 reach the challenging slots

**Why.** The dense FP4 coarse search never runs on the challenging slots,
because their sessions have a varying passband or midpoint gain and the
device bank is withheld
([opportunity map](gpu-strategy-playbook.md#tensor-core-opportunity-map-for-the-challenging-slots)).

**Steps.** Synthesize varying-passband and midpoint-gain banks on the
device in the OpenCL route. Estimate the gain first with step 5 of the
eligibility procedure. The kernels cannot run locally: write emulated tests
first, then use a checkpoint.

**Stop rule.** If the estimate is under 5% of the holdout slot, record it
and skip the phase.

### Phase 7: overlap and queues

- Run ready fine-family CPU work beside the device segmented stage, inside
  the bounded executor and ordered commits.
- Measure whether one compute queue with global barriers limits concurrent
  owners. Only then consider a second queue.

Measure with backlog intervals, not a single slot.

### Phase 8: admission

Each item raises admitted owners or batches; no result changes.
- Charge the backend-wide plan and shader cache once, in the stream
  reservation, not per owner.
- Replace the 128 MiB compilation bound with a measured one.
- Size the VkFFT temporary buffer from VkFFT's requirement.
- Reconcile the explicit-workspace rules of the three families.

### Phase 9: analysis data on the device

Add optional device-side summaries: evidence components, per-block
statistics, ridge maps and spectra. Transfer summaries, not raw
intermediates. Full dumps go behind an explicit option with charged
workspace. Default routes must be unchanged with the option off.

### Phase 10: CUDA builds

CUDA builds get none of the fine-stage device routes. Either add cuFFT forms
of the families, or select a backend per stage. First compare the two
automatic routes on one pod from one source.

### Phase 11: GPU simulator

Ask the user before starting. A counter-based generator on the device
changes every capture hash and every stored comparison. Keep the native
simulator as the reference, add a statistical cross-check, and land it only
together with another hash-changing change. Until then use
`--capture-cache`.

## Checkpoints on a rented RTX PRO 6000

Verify the applicable user authorization and cap before provisioning. Follow
[Run a bounded device qualification](triad-gpu-optimization.md#run-a-bounded-device-qualification)
and the playbook's [test recipe](gpu-strategy-playbook.md#test-recipe),
including [memory sizing](gpu-strategy-playbook.md#size-memory-on-a-pod).
Write the plan and pass rules before renting.

| Checkpoint | After | Measure |
| --- | --- | --- |
| A | Phase 2 | The large-`W` scaling experiment and remaining holdout measurements. The [first attribution run](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026) resolved the control's major cost and measured the automatic holdout's segmented span. Compare device against native segmented timing on the 72 Hz/1×-class shapes where the additional envelope is useful. |
| B | Phase 3 | Drift device against native, two alternated pairs, on the 72 Hz/5× control and the holdout. Flip only by the predeclared rule. |
| C | Phase 4 | The same for varying. |
| D | Phases 5 and 6 | All routes together; multi-owner backlog; FP4 reach. |

At every checkpoint also run the device tests, confirm defaults are
identical to the reference build, and keep the sanitized summaries under
`local-evidence/` with a cleanup trigger.

## Decision points

- **Use checkpoint A and subsequent traces** to reorder Phases 3 to 6 by the
  measured shares. The [family receipts](triad-gpu-optimization.md#fine-family-device-attribution)
  contain overlapping intervals and logical payload counts. An exclusive wall
  split or bus bandwidth claim needs a trace that observes it.
  - mutex wait dominates → 3a first;
  - upload dominates → 3b, 3c and 4b first;
  - host render dominates → 3c and 4b first;
  - the coarse remainder dominates the holdout → segmented work first
    ([levers 5 and 6](triad-gpu-optimization.md#next-levers-in-order)).
- **If Phase 2 finds a serialization fix,** rerun the device-against-native
  comparison: a faster native stage raises the bar for every device route.
- **No approval for a pod:** continue the phases that check locally (2,
  3a, 3b, 3d, 4a, 5, 8), keep automatic selection declined, and list what is
  waiting for a checkpoint.
- **A phase fails its stop rule:** record the negative finding with numbers,
  hardware and a reconsideration condition, then go to the next phase.

## Recording

- Results: a dated entry in [validation](validation.md) with hardware,
  inputs, pass rules, margins, failures and limits.
- Current behaviour, levers and reconsideration conditions: the
  [GPU guide](triad-gpu-optimization.md) and the
  [playbook](gpu-strategy-playbook.md) catalog.
- Temporary evidence: ignored `local-evidence/<topic-date>/` with a README
  that states its purpose and cleanup trigger. Keep only summaries.
- Then delete the finished phase from this page.

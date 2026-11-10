# Robust computational checks

The operating modem and measured receiver are the same compiled C++ stack.
Python's standard library orchestrates native probes and analyzes receipts; it
contains no second receiver DSP implementation. The default application requires
neither Python nor Torch. The optional compiled ATen backend uses the same native
receiver control, persistent state, work accounting and completion rules.

The [native processing contract](development.md#native-processing-and-integration)
also governs new experiments and cloud testing: extend the compiled probe and
production CPU/GPU backends, preserving shared buffers and worker ownership.
Do not implement replacement receiver DSP in Python to make a benchmark easier
to launch; Python remains the orchestration and analysis layer.

The tools answer different questions:

| Entry point | Decision |
| --- | --- |
| `triad_abbreviated.py` | Relative computational regression against a matched native baseline. |
| `triad_manual_cases.py` | Absolute local CPU capacity for the current ordered manual cases. |
| `triad_gpu_workload.py` and `triad_gpu_cpu_forecast.py` | GPU-observed native work combined with local native phase costs and independent CPU holdouts. |

None inherits a pass from the historical Python runtime. Its timings, process
transport applicability exceptions and old GPU forecasts are inadmissible as
native calibration. Changed CPU work, runtime or concurrency needs fresh affected
local timings and bounded holdouts. See [current validation](validation.md#native-codec-and-measurement-migration).
All receiver claims remain scoped to configured public timing and keyed ordinals.
Unknown waveform epoch, ordinal and timing acquisition is unqualified.

## GPU selection for accelerated development

**Prefer a full NVIDIA RTX PRO 6000 Blackwell for native GPU development,
accelerated regression tests and GPU-assisted CPU forecasting, or a demonstrably
more suitable modern accelerator.** The project prioritizes fast qualified test
turnaround and compatibility with highly parallel, lower-precision and Tensor
Core hardware at high productive utilization. Give that hardware a serious
development target: batch and arrange native bulk work to use its capabilities.
Energy efficiency is not the objective, and the lowest hourly price does not
necessarily produce the fastest or cheapest completed test.

Do not default to A100/A5000-class devices because they are familiar, have Tensor
Cores, or appeared in a historical rental proposal. Before selecting another
device, record a concrete workload, compatibility, availability or budget reason
in the existing experiment proposal or validation entry. An FP64-heavy stage or
one unsuccessful small FP16 kernel does not by itself overturn the preferred
hardware policy; evaluate the complete intended workload and its bulk arithmetic.
Use the bounded method, applicable evidence and a cheap preflight, not a new
benchmark campaign merely to justify a rental.

Before choosing hardware:

- Verify exact generation and edition, full GPU versus MIG partition, usable
  memory, power envelope and the host resources needed to feed it. NVIDIA lists
  Server, Workstation and Max-Q Workstation editions of the
  [RTX PRO 6000 Blackwell family](https://www.nvidia.com/en-us/products/workstations/professional-desktop-gpus/rtx-pro-6000-family/).
  Verify provider shorthand such as `WX` or `WK` against the actual SKU; do not
  silently substitute an older RTX 6000/A6000, Max-Q edition or partial GPU.
- Match the CUDA/LibTorch/compiler stack and native kernels to the device.
  Compare supported **dense** precision/accumulation throughput and actual matrix
  shapes; sparsity-qualified peak figures require a qualifying workload. The
  [RTX Blackwell architecture](https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/pdf/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1_1.pdf)
  provides newer Tensor Core hardware and lower-precision formats. Both
  [A100](https://www.nvidia.com/en-us/data-center/a100/) and
  [A5000](https://www.nvidia.com/content/dam/en-zz/Solutions/products/workstations/nvidia-rtx-a5000-datasheet.pdf)
  already have Tensor Cores; their presence alone is not the selection criterion.
  FP64-heavy or memory-bandwidth-bound work can favor a different device.
- Judge total upload, build, execution and retrieval time, including packing,
  transfers, synchronization and host preparation. Verify useful native work and
  actual accelerator execution; GPU utilization, device names and peak FLOPS do
  not establish a receiver speedup or a CPU capacity forecast.

The RTX PRO 6000 preference remains applicable despite the slower small FP16
Gram experiment in the [current validation record](validation.md#native-throughput-and-precision-investigation).
Keep its faster qualified native path while investigating useful batch shapes
or other bulk work when warranted. Useful Tensor Core acceleration remains a
measured outcome, not a claim conferred by this hardware preference. Follow the
[precision gates](gpu-precision-controls.md); GPU-observed work still needs
source-matched local CPU costs and independent holdouts for CPU forecasting.
Stay within the current spending authorization and cleanup contract; historical
prices and the completed rental's budget are not new spending authority.

## Relative regression harness

Run cheap native correctness checks before a declared timing comparison:

```sh
./build.sh test triad
python3 tools/triad_abbreviated.py \
  --probe build/dev/triad_receiver_probe --device cpu \
  --output local-evidence/triad-abbreviated.json
```

For a comparison, add `--baseline previous-native.json`. The
`datapump.triad.native-regression.v2` schema binds native input observations,
geometry, allocation, runtime, source and binary identity. Source may differ for
a declared candidate/reference comparison; the work and environment must remain
applicable. Saved v1 and old Python reports are rejected. The current driver
measures both reference and candidate binaries using their existing native
commands; this schema change does not grant a CI baseline-transition exception.
A report without a comparison may establish a baseline; that is not a
relative-regression pass.

Active and noise strata measure actual native slot, coarse, fine and audio
frontend work, giving eight phase/stratum combinations. A ninth `native_stream`
measurement uses the native `message` command: one fresh five-window `DDDQQ`
replay must decode bits `001` as `e` and complete after two scored quiet slots.
A one-unit preflight must pass before statistical looks. Stream batches sum
the fresh calls' `receiver_service` time, including receiver construction,
ingress/backpressure, spool reads/hash, drain and destruction. Process startup,
profile preparation, simulation, audio projection and the separate incomplete-EOF
control are outside that service phase but count toward the overall run budget.
This short mixed stream does not establish sustained backlog throughput.

Every active/noise batch unit is also a fresh native process executing
`regression --units 1`; the driver sums its native phase times. This measures
the first scored slot in a fresh process, not a fully cold GPU device or
steady-state service. Earlier same-process v2 reports have a different policy
and are rejected. Mixing the first and subsequent slot costs failed the GPU
fixed-geometry model gate; extra observations are not a repair for that mixture.

Each measurement uses bounded 1/2/4-unit batches and sequential looks at 3, 5
and 9 observations. The four-unit holdout checks the fixed-geometry repeated-unit
model. `--target-units` changes only the conditional projection, not the work
budget. The measured model-error allowance does not disappear with repetition.
The current policy uses a 30% material-slowdown threshold and separate model and
uncertainty gates. An inconclusive interval is not a pass.

`--threads`, `--slot-owners` and `--workspace-mib` preserve explicit native limits;
omitted workers use `2*H`. `--deadline-ms` and `--max-samples` bound each native
call. `--benchmark-deadline-ms` defaults to 300,000 ms and includes all subprocess
and excluded service work; exhaustion retains the partial report as incomplete.
The explicit sample cap must admit the complete five-window message (below
600,000 samples); the driver never raises a user cap to make a GPU run pass.
Unsupported or incomplete native work remains explicit in the report.
These measurements do not establish communication sensitivity, whole-case
real-time capacity or physical-device reception.

## RunPod GPU-assisted manual capacity forecast

Apply the [GPU selection contract](#gpu-selection-for-accelerated-development)
before selecting a provider device or runner.

GPU observations can accelerate collection of native work demand. Source-matched
local CPU timings supply elapsed cost. Never multiply GPU time by a FLOPS ratio,
use the cloud host's CPU as the local CPU, or credit a hypothetical optimization.
Prefer qualified lower-precision bulk work where complete experiments benefit;
follow the [precision guide](gpu-precision-controls.md).

First run the native correctness group and collect only needed current CPU costs
through the [manual capacity entry point](robust-manual-capacity.md#local-calibration-entry-point).
Predeclare cases, paired arithmetic classes, independent holdout seeds, worker
and owner counts, host/device memory, sample/search bounds and deadlines. Use the
same exact worker/owner/host-memory allocation on the GPU observation and local
CPU calibration/holdout. Automatic `2*H` on a different host is not necessarily
the same allocation. A cap recorded in bytes cannot be silently rounded to a
MiB-only command and relabeled as matched.

The source bundle contains a deterministic native build closure, not copied
Python DSP or a prebuilt exporter/kernel pair:

```sh
python3 tools/triad_gpu_bundle.py --output /tmp/robust-gpu-source.tar.gz
```

Build the probe from that closure with a CUDA-enabled C++11-ABI LibTorch install:

```sh
cmake -S . -B build/probe \
  -DBUILD_TESTING=OFF -DDATAPUMP_BUILD_GUI=OFF -DDATAPUMP_BUILD_CLI=ON \
  -DDATAPUMP_PORTABLE=OFF -DCMAKE_BUILD_TYPE=Release \
  -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON \
  -DCMAKE_PREFIX_PATH=/path/to/libtorch
cmake --build build/probe --target triad_receiver_probe
```

The CLI configuration remains enabled to satisfy the native install-target
contract; this command builds only the probe target. Use the full source and
CI closure for general correctness groups. The allowlisted bundle also includes
the tensor parity test for a separately declared bounded check. `DATAPUMP_TRIAD_TENSOR=OFF` is the default
native build; no Torch dependency is introduced into that profile. A requested
CUDA backend must execute on the selected CUDA device or report unavailable;
CPU fallback cannot count as GPU evidence.

The following placeholders are explicit declared limits, not portable defaults:

```sh
python3 tools/triad_gpu_workload.py \
  --probe build/probe/triad_receiver_probe --device cuda \
  --case-index N --reference-case N --seed 74021 \
  --workers W --slot-owners O --workspace-mib M --device-memory-mib D \
  --deadline-ms 30000 --row-deadline-ms 180000 \
  --max-samples 4194304 --coarse-tiles 4096 \
  --output local-evidence/robust-gpu-paired.json
```

Repeat `--case-index` and `--reference-case` for the declared coverage. Each
paired reference uses the exact native observation across CPU and CUDA, covering
active, noise, cold-maximum, terminal and erasure work. Active/noise complete
public acquisition; cold-maximum/terminal/erasure use declared non-admitting
fine-only diagnostics and do not prove acquisition. Separate full operational
near-decision, high-C/N0 and scored-quiet noise observations check decisions. `--near-cn0` can predeclare
the target for one paired row; otherwise the planned target is used. Reports
record requested and effective coarse/fine arithmetic. `--tensor-precision fp64`
is an explicit diagnostic override, not a default qualification shortcut.
Pairing requires winner/order and fixed-work agreement, score tolerance
`atol=2e-3, rtol=2e-4`, and at most 5% adaptive work-demand drift. Equal final bits
alone do not establish matched inputs or adequate precision.

`--row-deadline-ms` bounds the whole row. An explicit
`--fixed-active-seconds` must match a separately declared native CPU duration
override; it cannot silently replace the case's modeled duration.

Collect a fresh local CPU holdout using an independent seed and the matching
allocation. Its seed and native observation digest must differ from both the
CUDA trace and every local fit observation. For complete service, select the
smallest horizon whose exact native preflight covers startup carryover within
the declared sample/storage bounds, or declare an explicit `--backlog-slots B`.
The workload command opts into that CPU backlog with a positive count; its
default omits it and cannot qualify complete service:

```sh
python3 tools/triad_gpu_workload.py \
  --probe build/dev/triad_receiver_probe --device cpu \
  --case-index N --seed 97021 --backlog-slots B \
  --workers W --slot-owners O --workspace-mib M \
  --deadline-ms 30000 --row-deadline-ms 180000 \
  --max-samples 4194304 --coarse-tiles 4096 \
  --output local-evidence/robust-cpu-holdout.json
python3 tools/triad_gpu_cpu_forecast.py \
  --probe build/dev/triad_receiver_probe --case-index N \
  --cpu-report local-evidence/robust-cases.json \
  --gpu-report local-evidence/robust-gpu-paired.json \
  --local-holdout-report local-evidence/robust-cpu-holdout.json \
  --holdout-case-index N --reference-case-index N \
  --output local-evidence/robust-native-forecast.json
```

`--gpu-reference-report` supplies a separately retained applicable paired report
when target and paired observations are separate. Every invocation validates the
whole current case list; selected rows remain partial. Historical transport
bridges, cross-case transfers and phase-refresh exceptions are removed. A missing
native cost class requires a bounded current measurement, not an old report.

Forecasts retain signed and absolute stage errors so cancellation cannot conceal
a bad fit. Nominal holdouts allow a sum of absolute stage errors within 15% of
observed service and no individual contribution above 10%; conservative limits
are 20% and 15%. Guarded intervals include the declared model allowance as well
as timing uncertainty. A phase-forecast status of `conditional_phase_estimates`
and exit 0 establishes supported conditional phase estimates, not automatically
complete real-time capacity.

Configured logical-rate complete service additionally requires full native
strata; the current message/two-quiet/EOF gate; three actual ready-backlog CPU
calibration intervals; a fresh matched holdout at the declared preflight horizon;
measured controller work excluding condition-variable wait and separately
quantified spool reads/hash; and local fixed 180-byte multiblock codec
work with a matching holdout. Controller work is charged in full as a serialized
upper bound even when it may overlap DSP. Do not add whole overlapped stream wall
time to phase costs and count the DSP twice. Both the complete bound and measured
backlog must support the real-time criterion. Missing work leaves complete
capacity unresolved; a numeric point at or above 1 s/s fails. GUI presentation,
hardware-rate resampling and unknown-calendar acquisition remain outside that
logical-rate forecast. Preserve these limits in any application-level claim.

Unavailable accelerators, insufficient samples, incomplete public grids or a
deadline stop produce explicit unresolved workload results (exit 2). The
completed remote checkpoint has scoped CUDA and component-forecast evidence;
changed work/precision classes still require applicable device checks. Configured GPU
code, workflow tests or a functional pass do not establish throughput or CPU
forecast qualification. Use direct applicable native CPU measurements while a
required GPU work or precision class remains unqualified.

Use authenticated transfer for remote artifacts. Provider lifecycle and spending
require their own authorization; the opt-in CI workflow assumes a preprovisioned
runner and does not create cloud capacity. Retain small receipts needed for the
current claim or unresolved failure, then retire superseded artifacts under the
[material lifecycle](development.md#material-lifecycle).


### Native throughput and precision investigation — 10 November 2026

The completed bounded investigation started from clean source
`6bc9b0e3dd0ca91b3b8e3f4851301e6cf1b1371d` and compiled baseline manifest
`c9a797431bd9d35a0bd2c17149dbcf6211a2296c02ff8c67499c90e236099226`.
It measured native same-stream throughput, precision choices, useful GPU
acceleration and applicable local CPU forecasting. Changed implementations need
matched observations, full work/decision checks, affected local CPU costs and
independent bounded holdouts before a performance or capacity claim. The earlier
native regression baseline was established without a comparison; it is not a
pass for later changes.

The user authorized RunPod use for up to eight hours at an aggregate ceiling of
$20/hour. This superseded the earlier $2/60-minute A5000 proposal. The selected
task allocation was one full RTX PRO 6000, with a conservative $2.50/hour price
ceiling and $25 total task budget, for at most eight hours and without a
persistent volume. The roughly 100-minute rental was deleted by 20:47:10 UTC;
no pod or cloud volume remains. The observed balance decrease was about $4.08,
not a finalized invoice. Spending is separate from receiver performance. Pod identifiers,
account balances, billing records and operational logs remain outside the
checkout; do not copy them into documentation.

Preserve the automatic `2*H` receiver-worker budget and exact explicit limits.
Measure productive work in one stream and distinguish ready-input backlog from
startup, fill and drain. A running CUDA backend or a lower-precision dtype does
not prove tensor-core execution, numerical qualification or useful integrated
speedup. The exact baseline has passed bounded CUDA parity and active/noise
functional checks on the full RTX PRO 6000. Paired fused-mapper and direct-FP64
Gram checks show scoped fine/receiver-phase reductions for one row/seed and
bounded continuation. Three partial explicit-FP64 component forecasts are
supported. Tensor-core execution, nominal/conservative GPU-assisted service and
whole-stream capacity remain unqualified. The
[current validation entry](validation.md#native-throughput-and-precision-investigation)
records source, limits, original setup failures and measured device checks.
Initial matched O=2 and default-allocation shared-executor points
were slower, with a large first-slot latency regression and unqualified
carryover intervals. Later pilot timeouts remain part of the evidence. There is
no CPU throughput/utilization gain claim. Full-W scratch and oldest-slot leaf
priority are now implemented: the current point improves first-message latency
while remaining 1.73% slower overall than the baseline. The first fused GPU
mapper pair failed exact adaptive-work parity; the narrow final-FMA repair and
subsequent paired phase improvements retain their installed-runtime scope,
not a portable bitwise guarantee. The FP16 Gram-shape experiment was slower and
is not selected for integration. The accepted direct-FP64 Gram path covers its
bounded CUDA shape, rather than using that slower low-precision experiment.

The matched comparison is complete. Its obsolete copied executables, source
archives and superseded v1 baseline no longer have a retention purpose. Their
hashes and original coverage remain in
[baseline validation](validation.md#native-codec-and-measurement-migration).
Retain current raw observations and original failed receipts only for the final
scoped claims and unresolved gates. Retire them when applicable replacement
evidence resolves those gates and preserves the findings, margins and
source/configuration provenance. This does not require preserving obsolete
implementations or duplicate campaigns.

The fresh-call regression policy has passed 77 focused checks. CPU reference and
candidate both supported all nine models. Their mixed stream passed the 1.30
slowdown gate, but the eight standalone phase intervals remained inconclusive
at the bounded limit, so the overall comparison is inconclusive. The GPU
fresh-call run established a baseline with all nine models supported; it is not
a GPU relative-regression comparison. Forecast accounting now charges continuous
PCM time and checks independent adaptive-work counters. The current row-5
automatic-precision challenge still fails exact acquisition-coordinate agreement,
and the first predeclared near-decision bracket failed. A separately bounded
CPU-only construction subsequently fixed an 8.0 dB-Hz challenge. Explicit FP64
then passed all five strata and three operational challenges, with exact
work/acquisition and zero adaptive drift. The official report supports cold,
terminal and erasure component estimates; active/quiet work differs from local
calibration by 9–11%, outside the unchanged 5% cost-regime gate. Nominal and
conservative GPU-assisted service remain unresolved. CPU-only row-5 points and
replay margins do not establish a full interval capacity pass; the other seven
rows have geometry-only coverage. See the final intervals and retained failures
in [validation](validation.md#native-throughput-and-precision-investigation).

The current CUDA build requires NVCC and the CUDA SDK. For the actual SM 120
Blackwell device, the build uses `-DCMAKE_CUDA_ARCHITECTURES=120` with
`TORCH_CUDA_ARCH_LIST=12.0`. Verify the installed LibTorch/compiler/device
arithmetic with the exact oracle; the final-FMA match is source/runtime-specific.
Default builds without Torch and the optional CPU ATen path keep their separate
dependency contracts.

Each measurement remains predeclared and bounded within the existing
[hierarchical method](robust-manual-capacity.md#efficient-measurement); the larger
authorized cloud envelope does not authorize exhaustive or heavy campaigns.
Receiver completion, configured-calendar scope and unmeasured acquisition work
remain unchanged. Needed receipts were retrieved and cloud deletion was
confirmed. Command timeouts alone do not enforce deletion or a billing cap.

## Absolute manual-case capacity

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --probe build/dev/triad_receiver_probe \
  --output local-evidence/robust-cases.json
```

The native capacity schema retains raw work/phase receipts, nominal and
conservative service, actual same-stream/backlog results, memory, intervals and
margins. Default inference preserves `2*H` workers; explicit limits remain exact.
The [manual capacity guide](robust-manual-capacity.md) defines required work,
full-list coverage, bounds, residual runs and separate sensitivity trials.
One processing second per received baseband second is the real-time threshold;
do not divide by core count. A relative regression or accurate forecast does not
establish a complete-capacity pass.

## Historical findings that remain applicable

The [validation record](validation.md#complete-headless-robust-and-manual-capacity)
retains constituent reports and failures rather than relabeling them as native
calibration. The old all-eight one-pool estimates used 24 software workers over
12 hardware threads and a 9072 MiB cap. Nominal passes covered rows 2–8;
conservative passes covered rows 3 and 5. Row 4's conservative point was
4.11055 s/s, with unresolved state/model gates elsewhere. The interrupted row-7
report lacked its canonical final-runtime flag; an external audit did not create
that flag. The old input-transport applicability bridge is not valid here.

The historical process scheduler missed its original 15% throughput-gain gate;
the user subsequently accepted its smaller useful gain and increased productive
utilization. Pre-transport backlog utilization was 81.09%; the later 49-window
sealed-input run measured 8.325 busy threads of 12 (69.37%) over 51.898 seconds,
with 21.238 seconds of startup reported separately. Different RAM caps prevent a
causal transport-speed comparison. Its original exact gate failed on dispatch
counts in 17 slots; the separate post-hoc comparison and missing post-run input
hash remain explicit. Those results are not native scheduler capacity.

The [verified GPU history inventory](triad-gpu-optimization.md#verified-git-history-inventory)
records successful NVFP4 correlation, split-BF16/TF32x3 phase contractions,
CUDA graph/fusion, caching and other earlier implementations. Some lag-transform
experiments improved an isolated operation without improving their complete
trace; that result must not be generalized to all Tensor Core work. Earlier
accepted forecast timing gates still had unresolved
conservative capacity, including numeric point failures. Preserve those lessons
and [precision failures](gpu-precision-controls.md#retained-lessons), not obsolete
executable campaigns. No full, sustained, multi-day, exhaustive or heavy run is
a routine fallback when bounded current evidence is inconclusive.

### Earlier CPU implementations worth reconsidering

The 10 November history audit also found the following CPU lineages. Ratios
compare each historical implementation with its own contemporaneous baseline;
they are not comparisons with the current complete `IndependentApskV2` receiver.
Several wins were native kernels inside the Python-controlled receiver and
survived its migration. Deleting a Python caller does not imply deleting its
algorithm. These concise findings and Git references replace any need to restore
obsolete source trees or benchmark campaigns.

| Implementation and historical reference | Measured advantage | Current disposition and limits |
| --- | --- | --- |
| **Singleton-DC sum, direct output and shared pointer tables**, [`345e8d70`](https://github.com/mirage335-colossus/pumpModem/blob/345e8d70/docs/validation.md#L100) | DC sum instead of full FFT: nominal fine **1.32–1.35×**, capped-state **1.31× / 1.47×**; score delta ≤3.98e-13. Separate output/pointer-table change reduced fine time **9.2% / 4.7%** at six/twelve workers. | Native normalized DC sum, caller-owned scratch and shared pinned tiles remain. Historical percentages do not automatically apply to current orchestration. |
| **Fused mapped rendering/projection/statistics and paired chronological blocks**, stored in `3b423f93`; [findings](validation.md#receiver-restructuring) | Rows 7/4 fine throughput **1.750× / 1.664×**; CPU time down **37.0% / 31.4%**. Gaussian jobs 144→72 and worker submissions 1776→288 with logical references preserved. | Paired native mapped kernel and block updates substantially survive. Measured on dirty `247b7202`; three critical source hashes match `3b423f93`, not an assertion that its whole tree was the measured build. |
| **Shared observation spectra, native rank-two scoring and bounded peak selection**, `c54e2469`; [findings](https://github.com/mirage335-colossus/pumpModem/blob/89945e83/docs/validation.md#L744) | Row-4 noise **451.572→295.567 ms, 1.528×**; independent-frequency holdout **1.463×**. All 1,248 peak identities, order and published scores per look matched bitwise. | Old bridge removed; shared spectra, quadratic scores and bounded selection survive in native coarse processing. Critical measured source hashes match `c54e2469`. |
| **Candidate-parallel native Gaussian updates and shared Gram validation**, `c54e2469`; [findings](https://github.com/mirage335-colossus/pumpModem/blob/89945e83/docs/validation.md#L760) | Row-7 twelve-worker fine **403.174→334.730 ms, 1.204×**; model phase **97.034→19.366 ms, 5.010×**. Six-worker fine **1.331×**; row 4 only **1.095×**. | Native kernel retained and called directly by current fine receiver. The model-phase gain is not a fivefold complete-receiver gain. |
| **Cross-block validated mapping geometry reuse**, recorded in `58d7d8ea`; [findings](https://github.com/mirage335-colossus/pumpModem/blob/89945e83/docs/validation.md#L789) | Row-7 continuation **329.192→301.838 ms, 8.31%** lower; row-4 cold branch **4.14%** lower. Exact scores, state order, fills and logical references. | Exact cross-block descriptor-plan reuse is not established in the current per-render/tap preparation. Worth checking in native code; removal of Python overhead can change its benefit. |
| **Overlapping ready receiver windows**, recorded in `c54e2469`; [findings](https://github.com/mirage335-colossus/pumpModem/blob/89945e83/docs/validation.md#L983) | Two-controller discriminator **1.375×**; production five-window ABBA **21.6792→18.9520 s, 1.144×**, busy-core equivalents **2.909→3.566**. | Ready-window overlap survives in-process. Measured scheduler was dirty `0751d4dd`; the recording commit includes later accounting fixes. Do not restore processes or claim this establishes current capacity. Later three-controller gains still missed the original 15% gate. |
| **Earlier native per-slot workers versus shared pool**, `6bc9b0e3` versus `8706d228`; [matched point](validation.md#native-throughput-and-precision-investigation) | Earlier version: total **37.0251 versus 37.6654 s**, first slot **2.6940 versus 7.5399 s**, reported RAM **1.655 versus 3.160 GB**. Current pool completes first message earlier, **21.7266 versus 36.9523 s**. | A real older-native advantage on selected metrics, balanced by current message latency/ownership. One point is not a portable capacity ranking or reason to reduce the global worker default. |
| **Native FP32 FFT acquisition and close-clock template reuse**, [`c56d0e1a`](https://github.com/mirage335-colossus/pumpModem/blob/c56d0e1a/docs/validation.md#L107) | Complete one-bit-plus-absence captures: **1.429× / 2.074× / 1.984×** under three arrival priors. Five-lane template construction **142.3→53.2 ms, 2.676×**. Conditional 6,544-pair numerical/sensitivity screen had no decision/completion disagreement. | Retired single-carrier receiver, not current triple-waveform qualification; full-bank sensitivity remained unresolved. Generic SIMD FFT code remains, but the old receiver/cache contract was removed in `419e6bae`. |
| **Shared finite-pulse moments and affine interval coefficient cache**, `5a37b781` / [`12010d87`](https://github.com/mirage335-colossus/pumpModem/blob/12010d87/docs/pulse-moment-validation.md#L142) | Moment prefix **42.4–45.6×**; complete low-carrier captures about **2.6–2.7×**. Later coefficient-cache complete `001` fixture **1.430×**, inclusive pair **34.60574→24.73944 CPU s, 1.399×** ([cache results](https://github.com/mirage335-colossus/pumpModem/blob/12010d87/docs/pulse-moment-validation.md#L680)). | Largest ratio is only 20 s of a 6,400 s symbol; initial-bank coverage was unfinished. Old pulse mathematics must match the new waveform before reuse. Transfer factorization/cache principles, not the retired receiver. |
| **Scheduled physical moments, shared source curves and candidate-family SIMD**, [`fe3e2c28`](https://github.com/mirage335-colossus/pumpModem/blob/fe3e2c28/docs/scheduled-hops.md#L638), [`7af295c1`](https://github.com/mirage335-colossus/pumpModem/blob/7af295c1/docs/scheduled-hops.md#L889), [`42221b0a`](https://github.com/mirage335-colossus/pumpModem/blob/42221b0a/docs/scheduled-hops.md#L1595) | Identical complete 348 s PCM: **923.545→457.735 CPU s (2.018×)**, **118.884→65.709 wall s (1.809×)**. Subsequent hot/cold source reuse **1.259× CPU / 1.215× wall**. P3 frequency moments/SIMD reduced prefix CPU/wall by **27.6% / 22.6%**. | Retired scheduled/single-carrier architecture; full complex evidence preserved in the complete comparison. P3 prefix had no accepted bit/END and demanding full captures still failed. Duty reduction is not an equal-detector speedup. |

The earlier single-carrier implementations were removed in `419e6bae` for
canonical-stack consolidation; their deletion did not disprove the recorded
gains. Algorithmic factorization, reuse over natural signal intervals and grouping
exactly shared preparation remain useful research directions. Current native
CPU comparisons need matched waveform, public work, resource ownership and
complete service measurements before selecting an older technique.

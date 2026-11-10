# Robust computational checks

The operating modem and measured receiver are the same compiled C++ stack.
Python's standard library orchestrates native probes and analyzes receipts; it
contains no second receiver DSP implementation. The default application requires
neither Python nor Torch. The optional compiled ATen backend uses the same native
receiver control, persistent state, work accounting and completion rules.

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

## Relative regression harness

Run cheap native correctness checks before a declared timing comparison:

```sh
./build.sh test triad
python3 tools/triad_abbreviated.py \
  --probe build/dev/triad_receiver_probe --device cpu \
  --output local-evidence/triad-abbreviated.json
```

For a comparison, add `--baseline previous-native.json`. The
`datapump.triad.native-regression.v1` schema binds native input observations,
geometry, allocation, runtime, source and binary identity. Source may differ for
a declared candidate/reference comparison; the work and environment must remain
applicable. Old Python reports are rejected. A report without a comparison may
establish a baseline; that is not a relative-regression pass.

Active and noise strata measure actual native slot, coarse, fine and audio
frontend work. Each uses bounded 1/2/4-unit batches and sequential looks at 3, 5
and 9 observations. The four-unit holdout checks the fixed-geometry repeated-unit
model. `--target-units` changes only the conditional projection, not the work
budget. The measured model-error allowance does not disappear with repetition.
The current policy uses a 30% material-slowdown threshold and separate model and
uncertainty gates. An inconclusive interval is not a pass.

`--threads`, `--slot-owners` and `--workspace-mib` preserve explicit native limits;
omitted workers use `2*H`. `--deadline-ms` and `--max-samples` bound each native
call. Unsupported or incomplete native work remains explicit in the report.
These measurements do not establish communication sensitivity, whole-case
real-time capacity or physical-device reception.

## RunPod GPU-assisted manual capacity forecast

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
CI closure for general correctness groups. The bounded proposal below composes
only the bundled tensor parity test through an external CMake hook. `DATAPUMP_TRIAD_TENSOR=OFF` is the default
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
deadline stop produce explicit unresolved workload results (exit 2). No CUDA
execution is available in the current local environment; configured GPU code or
workflow tests are not a hardware qualification. Use direct applicable native
CPU measurements while CUDA evidence is missing.

Use authenticated transfer for remote artifacts. Provider lifecycle and spending
require their own authorization; the opt-in CI workflow assumes a preprovisioned
runner and does not create cloud capacity. Retain small receipts needed for the
current claim or unresolved failure, then retire superseded artifacts under the
[material lifecycle](development.md#material-lifecycle).


### Bounded RunPod proposal — 10 November 2026

This proposal tests actual CUDA linking, native CPU/CUDA parity and bounded
receiver execution. It does not qualify full manual-case capacity, forecast local
CPU service, or repeat historical campaigns. No pod has been provisioned. Standing
`CLOUD.md` authorization covers eligible compute, but account access, available
budget and existing resources have not been reconciled for this proposal. Keep
account-specific operational records outside the checkout.

| Limit | Proposed bound |
| --- | --- |
| Instance | One Secure Cloud on-demand RTX A5000: 24 GB VRAM, published 25 GB host RAM and 9 vCPUs. |
| Price | Published GPU price $0.27/hour, checked 10 November 2026; reject a launch quote above $0.50/hour. |
| Total budget | At most $2 all-in for this task; no retry, larger instance, extra funding or AutoPay changes. |
| Lifetime | At most 60 minutes from creation to confirmed deletion, including image pull and build. |
| Storage | 50 GB ephemeral container disk; no volume disk or network volume. |

At the published GPU price and $0.10/GB/month container-disk rate, one hour is
approximately $0.28 before tax; the deployment quote and actual accounting
remain authoritative. Compute/storage are billed by the second. Sources:
[RunPod GPU pricing](https://www.runpod.io/pricing) and
[Pod/storage pricing](https://docs.runpod.io/pods/pricing).

Pin the image to
`runpod/pytorch:1.2.0-cu1281-torch291-ubuntu2404@sha256:9b2667aebeb9e3005cd6b2ebcb0cfcef0a2e4562544669304d03d620e07e7037`
(11.85 GB compressed). Its PyTorch/CUDA/Ubuntu family is documented in the
[official image guide](https://github.com/runpod/containers/blob/main/official-templates/pytorch/README.md).
Use the packaged C++ LibTorch libraries; Python may locate that package and
orchestrate checks, but receiver DSP remains native. Fail readiness if the
pinned image, toolchain, CUDA device or required ABI is unavailable.

The creation-to-deletion schedule reserves 10 minutes for cold-pull/readiness,
25 for the bounded build, 15 for checks, five for retrieval/deletion and five
as margin. An early failure ends the run; unused time does not authorize larger
work. Before any launch, verify a working external control-plane delete route
and supervision through the deadline. Use an independent watchdog if readily
available. There is no provider `--terminate-after` flag in this plan: command
timeouts do not delete a pod or enforce a billing cap. Record confirmed deletion
and reconcile actual spend; a failed delete needs immediate control-plane
follow-up within the same envelope, not an unattended stopped resource.

Build only the allowlisted [native bundle](../tools/triad_gpu_bundle.py), probe and
[CUDA parity test](../tests/test_triad_tensor.cpp). The bundle includes that exact test and omits unrelated tests. Verify its SHA-256
`4e7b734f09cf4b9270ff9b4df53ed8285f196683b5de8d82aeacdb512f13f2ee`.
Place this locally checked compose hook outside the source as `contract.cmake`:

```cmake
add_executable(runpod_triad_tensor EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_triad_tensor.cpp")
target_include_directories(runpod_triad_tensor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(runpod_triad_tensor PRIVATE cxx_std_20)
target_link_libraries(runpod_triad_tensor PRIVATE datapump_triad_receiver)
```

Readiness must confirm one A5000/SM 8.6, driver/device memory, CUDA toolkit 12.8,
CMake ≥3.21, C++20, Ninja and OpenSSL 3. The packaged Torch must report 2.9.1,
CUDA 12.8 and C++11 ABI; obtain `LIBTORCH_PREFIX` from its
`torch.utils.cmake_prefix_path`. Set `TORCH_CUDA_ARCH_LIST=8.6`,
`PYTORCH_CUDA_ALLOC_CONF=backend:native`, and `OMP_NUM_THREADS`,
`MKL_NUM_THREADS`, `OPENBLAS_NUM_THREADS` to 1. Preserve preflight output:

```sh
nvidia-smi --query-gpu=name,memory.total,compute_cap,driver_version --format=csv,noheader
nvcc --version
python3 -c 'import torch; assert torch.__version__.split("+")[0]=="2.9.1" and torch.version.cuda=="12.8" and torch.compiled_with_cxx11_abi(); print(torch.utils.cmake_prefix_path)'
timeout -k 10s 5m cmake -S source -B build -G Ninja \
  -DCMAKE_PROJECT_DataPump_INCLUDE="$PWD/contract.cmake" \
  -DBUILD_TESTING=OFF -DDATAPUMP_BUILD_CLI=ON -DDATAPUMP_BUILD_GUI=OFF \
  -DDATAPUMP_PORTABLE=OFF -DCMAKE_BUILD_TYPE=Release \
  -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON \
  -DCMAKE_PREFIX_PATH="$LIBTORCH_PREFIX"
timeout -k 10s 20m cmake --build build --parallel 2 \
  --target triad_receiver_probe runpod_triad_tensor
ldd build/triad_receiver_probe
ldd build/runpod_triad_tensor
timeout -k 5s 45s build/runpod_triad_tensor --cuda
```

Verify resolved CUDA/ATen libraries and absence of `libpython`/`libtorch_python`.
An initial bundle omitted the spool and fixture-key headers. The allowlist now
includes both, with a recursive local-header closure check; all nine focused
bundle tests passed. The external hook's configuration and target/link graph
were checked locally with CPU LibTorch. A fresh extraction then compiled the
probe, bundled parity test and Helper exporter with two build jobs; CPU tensor
parity passed, source identity matched the local build, and no Python linkage
was present. The proposed CUDA build and checks have not been run. Build limits
of two jobs are separate from receiver allocation. The following device check
uses an explicit diagnostic W=2/O=1, 8 GiB host and 4 GiB device limit; it does
not reduce the application's automatic `2*H` receiver policy.

Run the following block under `timeout -k 5s 90s python3`, with
`EXPECTED_SOURCE_SHA256` set to the matched local CPU probe's current source
manifest before transfer. Its [native wrapper](../tools/triad_native.py) requires
actual accelerator execution; [runtime checks](../tools/triad_gpu_workload.py)
validate the declared device and resource accounting. Save command failures and
incomplete receipts as well as successful results.

```python
import os, sys
sys.path.insert(0, "source/tools")
from triad_native import NativeProbe, write_result
from triad_gpu_workload import cuda_runtime
p = NativeProbe("build/triad_receiver_probe", backend="cuda")
flags = dict(workers=2, slot_owners=1, memory_limit_bytes=8 << 30,
             device_index=0, device_memory_limit_bytes=4 << 30,
             tensor_precision="automatic", deadline_ms=12000,
             max_samples=100000, coarse_tiles=128, seed=74021)
for stratum in (None, "active", "noise"):
    r = p.call("describe", **flags) if stratum is None else p.call(
        "regression", stratum=stratum, units=1, **flags)
    write_result(f"receipts/{stratum or 'describe'}.json", r)
    assert r["status"] == "complete"
    identity = r["identity"]
    assert identity["configured_tensor_backend"] is True
    assert identity["source_manifest_sha256"] == os.environ["EXPECTED_SOURCE_SHA256"]
    bulk = identity["backend_receipt"]
    assert cuda_runtime(bulk)["device"] == "cuda:0"
    if stratum is not None:
        assert r["result"]["work_complete"] is True
        assert r["result"]["units"] == 1 and r["result"]["stratum"] == stratum
        assert r["result"]["evaluator_expected_decision_observed"] is True
        assert all(bulk[k] > 0 for k in ("fft_calls", "score_rows", "mapped_values",
            "statistics_blocks", "synchronizations", "upload_bytes", "download_bytes"))
        assert bulk["peak_reserved_bytes"] <= 4 << 30
        assert bulk["allocator_peak_reserved_bytes"] <= 4 << 30
p.verify_unchanged()
```

The required check sequence is one actual-CUDA parity check followed by one
native active and one noise regression unit, each bounded to 100,000 generated
samples and 12 seconds. Preserve source/binary hashes, device/runtime/arithmetic,
requested/effective allocation, memory, work/decisions and all failed/incomplete
receipts. No CPU fallback can satisfy a requested CUDA check.

Only if the core checks pass, at least ten minutes remain, and the remaining
envelope permits it, try current
manual row 6 and then row 2: full public selection and persistent 2/3/4/6-second
fine checkpoints, at most 30 seconds each and at most 8,388,608 generated samples
in total. Stop optional work at the first incomplete/failed result. These two
rows are bounded device/work probes, not a full manual suite, sensitivity claim
or CPU forecast. Do not change geometry, extend the cap or replace an incomplete
public search with evaluator coordinates.

Use freshly parsed `load_cases("source/_tests-RobustModem.md")` and each selected
row's exact `native_args`. With the same `NativeProbe`, first call
`plan` with a 5,000 ms deadline and 4,194,304 sample cap; require the modeled
profile, eligible continuation, admitted workspace ≤8 GiB and complete public
window within that cap. Then call `continuation` with those arguments,
`stratum="active"`, `slot_index=0`, 30,000 ms and the same sample cap. Save the
receipt before `validate_work(receipt, "cuda", "active")`; this checks full public
selection, effective arithmetic and persistent 2/3/4/6-second work without
fallback. Bound the whole optional wrapper by `timeout -k 5s 2m` and never proceed
to row 2 after an incomplete row 6. Local geometry preflights reported 1,630,039
rows/T=256.8 s for row 6 and 4,141,064/T=576.4 s for row 2; these are plans, not
successful CUDA execution or measured runtime.

Retrieve only the small source/build/runtime and check receipts required for the
claim, verify local hashes, then delete the pod and its container disk. Retain
receipts under the existing [validation lifecycle](development.md#material-lifecycle)
until the scoped claim is summarized or superseded; remove copied source/build
artifacts when that purpose ends. Never place account identifiers, credentials or
billing records in this guide.

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

Earlier GPU work found that preparation and simulation dominated a receiver
trace, and that faster isolated tensor transforms did not improve the complete
receiver. Earlier accepted forecast timing gates still had unresolved
conservative capacity, including numeric point failures. Preserve those lessons
and [precision failures](gpu-precision-controls.md#retained-lessons), not obsolete
executable campaigns. No full, sustained, multi-day, exhaustive or heavy run is
a routine fallback when bounded current evidence is inconclusive.

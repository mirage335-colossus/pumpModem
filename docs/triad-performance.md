# Robust computational checks

The Robust tools answer two different questions. The ordinary
[`tools/triad_abbreviated.py`](../tools/triad_abbreviated.py) harness compares
bounded computational work against a matched baseline to detect relative
degradation. The separate [`tools/triad_manual_cases.py`](../tools/triad_manual_cases.py)
suite evaluates absolute receiver capacity for the freshly read manual case
list. A pass in one mode does not imply a pass in the other. Agents working on
the manual list must start with the
[CPU ceiling workflow](robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates)
and its practical algorithm, precision and productive-parallelism requirements.

**Current complete-service estimates are scoped to the configured public
calendar. Acquisition of unconfigured waveform epochs, ordinals and timing is
unresolved, regardless of favorable numerical capacity points or forecast
accuracy.** Keep that coverage limit in reports. The continuous bit/null model
retains two-slot quiet confirmation; a semantic new-transmitter classifier is
not a receiver requirement. See
[the receiver scope](validation.md#continuous-receiver-restructuring).

## Relative regression harness

Run the native `triad` checks first, then the abbreviated regression harness for
a declared performance comparison:

```sh
./build.sh test triad
python3 tools/triad_abbreviated.py \
  --output local-evidence/triad-abbreviated.json \
  --device cpu --target-units 1000
```

Use a matched `--baseline previous.json` only while that comparison is active;
keep source, configuration, input, device and execution conditions matched.
The v2 harness includes FFTScreen preparation and scoring, Gaussian state
updates, sparse timing, and canonical complete-fine receiver processing. Its
receiver fixture uses a synthetic same-stream input and fully visible worker
allocation. This complete-fine timing is relative computational regression
evidence. It does not measure the native channel, quiet message completion,
nominal SNR sensitivity, full manual-case capacity, or physical reception.

Predeclare the fixed geometry, path strata, target unit count, uncertainty
decision, source/configuration provenance and output location. The hierarchy
uses 3, 5, then 9 outer looks, with 1- and 2-unit training measurements and a
4-unit holdout. The holdout checks the stated fixed-geometry repeated-unit
assumption. `--target-units` changes only the extrapolation target, not the
measurement budget. These conditional estimates do not qualify changed
algorithms, memory or concurrency regimes, sample rates, devices, or receiver
stages outside the measured workload.

Keep raw JSON only while it supports the current comparison or validation
claim. Distill useful conclusions, negative findings and provenance into the
maintained [validation record](validation.md), then remove superseded outputs.
RunPod may accelerate this same bounded method but does not justify a larger
campaign. Do not use full-observation, sustained, multi-day or exhaustive runs
as routine checks or fallbacks for an inconclusive abbreviated result.

## RunPod GPU-assisted manual capacity forecast

Prefer this path for repeat manual checks with applicable local CPU phase
calibration. RunPod supplies public work demand. Source-matched local timings
supply CPU costs for acquisition, fine work, recurring banks and fixed
orchestration. Refresh only missing local cost classes, or the affected local
calibration when the CPU work or runtime changes. A GPU-only change requires
new applicable GPU evidence and preserves an unchanged CPU calibration.
This procedure forecasts the implemented configured-calendar receiver phases;
fast GPU observations cannot fill missing waveform-address acquisition work.

The target is complete service on the **local CPU**, with its full available
logical-thread budget. GPU experiments accelerate workload observation; they
do not substitute the pod's CPU or GPU throughput for the local receiver.
Combine work demand with per-phase local elapsed costs at matched geometry,
arithmetic, cache/state and productive concurrency. Narrow sustained local
receiver tests and fresh holdouts establish those costs. Synthetic stress,
advertised FLOPS, thread counts and aggregate GPU/CPU wall-time ratios cannot
replace that calibration.

Use the [mixed-precision policy](gpu-precision-controls.md) to accelerate the
dominant work through efficient algorithms, batching and qualified lower or
tensor-compatible arithmetic. Measure complete experiment time, including
simulation, preparation and transfers. The pre-restructuring RTX PRO 6000 trace
spent about 80% of its time on native CPU simulation and bank preparation;
optimizing only small GPU kernels misses that measured bottleneck. See the
[current evidence](validation.md#gpu-assisted-receiver-measurement).

The continuous-listener and native coarse/Gaussian restructuring changed CPU
work. Refresh its affected local phase costs and require applicable matching GPU
work/precision evidence before publishing a new GPU-assisted forecast. The
pre-restructuring eight-row CPU/GPU receipts remain tied to their old
implementation.
The new native CPU kernels do not themselves qualify a faster CUDA path or
tensor execution. See [current restructuring validation](validation.md#continuous-receiver-restructuring).

Every automatic Robust receiver mode uses `S = 2*H` software permits, including
serial, finite and custom paths; explicit worker count `W` stays exact.
The canonical continuous controller cap is `min(S, W)`, admitting up to `2*H`
independent controllers sharing `2*H` permits by default. Owners grow lazily only
for ready complete windows whose full public DSP bounds, at least one worker per
job and exact memory leases fit; do not spawn owners for empty work. Completion
frees an owner before ordered commit, bounded by twice the resolved owner cap in
uncommitted records (`4*H` by default); job-free owners can retire for larger later
geometries. Prospective owner overhead uses the fully loaded child's maximum
pre-DSP bootstrap RSS/HWM after its first handshake, not the parent's captured-IQ
heap. Actual parent, live-owner and native workspace memory remain charged and
rechecked at admission. Memory admission may reduce owner count but retains `W`,
including fallback. Hardware availability remains `H`. Supported Linux pipelines
use sealed, read-only `memfd` input, charging one parent assembly transient before
dropping its array and retaining the descriptor/metadata for retry. Child mappings
close before acknowledgement. Remove only duplicate input-copy reserves; retain
full DSP/native workspace, raw-ring and result leases. Unsupported copied
transport retains its full charges. Capability probing is lazy, before pipeline
admission, and excludes finite/custom/serial paths. Full-size read-only transport
checks admitted three row-1 inputs at eight workers each where copied input
admitted one, with full DSP reservations retained. This did not execute row-1 DSP
or measure throughput. The completed native copied/shared functional comparison
used three owners at eight workers each, with exact checked outcomes and correct
quiet/EOF/lifetime behavior. Neither result establishes an elapsed gain or an
all-row shared-transport admission envelope.

A final-source 49-window same-stream observation with sealed input and automatic
allocation averaged **8.325 busy threads of 12 (69.37%)** over 51.898 seconds
of predeclared ready-backlog intervals. The longest uninterrupted interval was
32.899 seconds at 7.921 busy threads; owner startup totaled 21.238 seconds
separately. All 49 windows completed without fallback or retry. The original
exact comparison gate **failed** because dispatched tracking-task counts
changed in 17 slots. A separate post-hoc comparison excluded that
allocation-dependent count; the other compared work, scores and decisions
matched. This observation is not an all-row pipeline capacity result. Its
automatic RAM cap was 8.748 GB versus the earlier fixed 9.513 GB; the observations
do not isolate a transport speedup. [Final-source receipt](../local-evidence/final-backlog-cpu/result.json).

Before the `memfd` change, the enlarged automatic owner cap passed an independently
audited 49-window continuous-capture check; its predeclared sustained-backlog
interval averaged 81.09% of twelve hardware threads. That is pre-transport evidence.
It does not establish full saturation,
a paired speedup or all-eight-row pipeline capacity. The pre-`memfd` admission bootstrap's
owner widths are conditional on its transport, declared ring ranges, geometry
and RAM. They are not current shared-transport or universal concurrency factors.
Earlier hardware-count-capped and two-owner row-7 measurements retain their
original scope. The
[current all-eight complete-work estimates](validation.md#complete-headless-robust-and-manual-capacity)
use one twenty-four-worker pool on twelve hardware threads at a fixed 9072 MiB
cap. Conservative capacity passes cover rows 3 and 5; nominal passes cover rows
2–8. Row 4 has a conservative numeric point failure and unresolved model gates
remain. This audited union of matching per-row receipts is not a replacement
canonical calibration report or an automatic multiowner pipeline ceiling. These
receipts predate the input-transport change. The reviewed applicability receipt
and strict forecast consumer preserve unchanged one-pool DSP/finite costs without
changing raw timings or measured-source identities. Only the exact completed
canonical reports for rows 1–6 and 8 are eligible, as described below. Row 7's
interrupted raw report and the audited union are rejected before forecasting;
its manual estimate does not supply the missing canonical completion flag.
Preserve each constituent report and its limits, including row 7's external
posttermination identity audit and unavailable canonical final-runtime flag.
Record the external checks separately; do not fabricate that flag or carry
these one-pool estimates into a concurrent mode without applicable costs.
Automatic serial fallback retains auxiliary-process memory and cannot inherit
explicit `slot_processes=1` qualification. Forecast overlapping work only with
applicable costs for its shared worker allocations and a fresh outer-service
holdout including native preparation, IPC and ordered commit. Neither summing
overlapping slot wall times nor timing asynchronous input submission measures
that service. Keep the configured-calendar scope and prefer qualified
lower-precision GPU work for the implemented phases.

Package the allowlisted source with the matching native exporter and mapped
kernel. Transfer both artifacts separately with authenticated SSH/SCP and place
the kernel beside the exporter. The provider console handles pod lifecycle.
RunPod temporary disks are disposable. Stopping task instances is authorized;
do not keep them running merely to preserve temporary setup, builds or captures.
Download and verify the small receipts needed for the current validation claim,
then stop and remove the instance. Repository changes and retained local
evidence have their separate preservation requirements.

```sh
python3 tools/triad_gpu_bundle.py --output /tmp/robust-gpu-source.tar.gz
```

On the pod, run the bounded CUDA workload and predeclare representative paired
reference rows spanning every arithmetic class. A full-list example uses rows
1 and 2 for the tight-clock class and row 6 for the bulk FP32 class.
Use the CPU calibration's worker count and exact admitted workspace cap for the
GPU observation and local holdout; the pod's hardware count may differ. For a
calibration intended for this GPU CLI, resolve operating RAM once, round down
by less than 1 MiB before measurement, retain that preflight, and pass the
derived `--workspace-mib` to the manual suite with worker flags omitted. Do not
round an existing report's cap and pretend its identity still matches.

When fresh local calibration is required, obtain that cap after loading the
measurement runtime in the numerical Python environment:

```sh
mkdir -p local-evidence
ROBUST_WORKSPACE_MIB=$(python3 - <<'PY'
import json, sys
from pathlib import Path
sys.path.insert(0, 'tools')
import triad_case_capacity
from triad_cpu import detect_receiver_memory
snapshot = detect_receiver_memory()
budget = snapshot['memory_budget_bytes']
if type(budget) is not int or budget < (1 << 20):
    raise SystemExit('No admissible operating-RAM budget')
mib = budget // (1 << 20)
snapshot['rounded_workspace_limit_bytes'] = mib << 20
Path('local-evidence/robust-ram-preflight.json').write_text(
    json.dumps(snapshot, indent=2) + '\n')
print(mib)
PY
)
```

Pass `--workspace-mib "$ROBUST_WORKSPACE_MIB"` to the required calibration
command. The snapshot records resource assumptions; it does not freeze other
applications' RAM use, and normal admission still applies. Keep it with the
calibration it supports, then remove it when that evidence is retired.

Extract the shared values from an applicable current CPU report. This rejects
mixed allocations or a byte cap that the MiB-only GPU CLI cannot represent.
Use the same values on the pod and local host. Historical 3840/4608 MiB and
twelve-worker receipts keep their original limits; they are not defaults.

```sh
ROBUST_CPU_REPORT=local-evidence/robust-cases.json
read -r ROBUST_WORKERS ROBUST_WORKSPACE_MIB < <(
python3 - "$ROBUST_CPU_REPORT" <<'PY'
import json, sys
with open(sys.argv[1]) as source:
    report = json.load(source)
workers = {row['measurement']['inference_workers'] for row in report['cases']}
cap = report['workspace_limit_bytes']
assert len(workers) == 1
worker = next(iter(workers))
assert type(worker) is int and worker > 0
assert type(cap) is int and cap > 0 and cap % (1 << 20) == 0
print(worker, cap // (1 << 20))
PY
)
```

```sh
python3 tools/triad_gpu_workload.py \
  --device cuda --workers "$ROBUST_WORKERS" --workspace-mib "$ROBUST_WORKSPACE_MIB" \
  --reference-case 1 --reference-case 2 --reference-case 6 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-gpu-paired.json
```

CUDA coarse acquisition is the default with `--device cuda`. Record the resolved
software-worker allocation: omitted `--workers` uses `S = 2*H` from the probe
host's visible hardware count, while explicit `--workers W` remains exact.
Keep one numerical thread per receiver worker.
These commands pin the calibration's worker count so the paired and target
runs use the same allocation; this is not a portable thread cap. Preserve the
[automatic allocation contract](development.md#receiver-cpu-allocation)
on other processors. To repeat only
a target row, use `--case-index N` and retain the qualified paired GPU report.
The two reports must match the actual GPU backend, runtime, worker allocation,
workspace cap and source/native artifacts. Reference rows must still match their
current normalized configurations.

```sh
python3 tools/triad_gpu_workload.py --case-index 7 \
  --workers "$ROBUST_WORKERS" --workspace-mib "$ROBUST_WORKSPACE_MIB" \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-gpu-target-7.json
```

A bounded RTX PRO 6000 experiment ran two unpaired traces for rows 5 and 7
in 31.01 seconds of SSH wall time. Each job used 12 workers and a 4608 MiB
receiver workspace. Admission declared aggregate limits of 24 host workers,
10 GiB of host memory and 19 GiB of device memory. Both traces reproduced the
full trace's capture and useful work exactly. This was a two-job measurement
with explicit resource limits. [Dual-job result](../local-evidence/robust-v2/gpu-dual-5-7.json)

Predeclare independent local holdout rows for the applicable cost classes and
any explicit cross-case transfer. Create fresh bounded CPU measurements using
the same workspace cap. The following command checks only row 7.

```sh
python3 tools/triad_gpu_workload.py \
  --device cpu --case-index 7 \
  --workers "$ROBUST_WORKERS" --workspace-mib "$ROBUST_WORKSPACE_MIB" \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-cpu-holdout-7.json
```

An unavailable requested CPU workspace fails before measurement. Each acquired
branch uses one fresh timing or three same-work timings when the first takes at
most 0.5 seconds. Broad acquisition uses actual 1/2/4 bounded frequency tiles
and separately measured full-count candidate selection on three public score
orders. This yields a full-grid cost estimate while keeping the full scan out
of the holdout. Timing repetitions use the same signal and work geometry.

Example targeted transfer from encrypted row 5 to otherwise matching clear row
7, using a separate target GPU trace and cached qualified GPU references.

```sh
python3 tools/triad_gpu_cpu_forecast.py \
  --case-index 7 \
  --cpu-report local-evidence/robust-cases.json \
  --gpu-report local-evidence/robust-gpu-target-7.json \
  --gpu-reference-report local-evidence/robust-gpu-paired.json \
  --local-holdout-report local-evidence/robust-cpu-holdout-7.json \
  --holdout-case-index 7 \
  --reference-case-index 1 --reference-case-index 2 --reference-case-index 6 \
  --transfer-case 7=5 \
  --output local-evidence/robust-gpu-cpu-target-7.json
```

Use the paired report directly as `--gpu-report` for a full current-list
forecast. Omit the row selection, separate reference report and transfer flags
for ordinary same-row forecasting. Every invocation validates the entire
manual list. A target-only run covers its selected rows.

The optional applicability receipt bridges only the reviewed input-transport
change for configured-calendar, single-pool CPU coefficients. For example, after
producing current-source paired GPU evidence and an independent row-2 CPU holdout
at the matching allocation and cap:

```sh
python3 tools/triad_gpu_cpu_forecast.py --case-index 2 \
  --cpu-report local-evidence/parallel-routes/all8-cpu-capacity.json \
  --cpu-calibration-applicability local-evidence/parallel-routes/shared-input-cpu-applicability.json \
  --gpu-report local-evidence/robust-gpu-paired.json \
  --local-holdout-report local-evidence/robust-cpu-holdout-2.json \
  --holdout-case-index 2 \
  --reference-case-index 1 --reference-case-index 2 --reference-case-index 6 \
  --output local-evidence/robust-gpu-cpu-target-2.json
```

This receipt accepts exact bytes of `all8-cpu-capacity.json` for rows 1–6 or
`all8-cpu-residual-8.json` for row 8. It binds the reviewed source pair, permits
only the two specified pipeline files to differ, and checks native artifacts,
runtime, H=12/S=24 allocation, 9072 MiB cap and manual inputs. It does not alter
`same_measured_source`, relax any quality gate or renew the historical message
gate. CUDA target/reference reports and new independent CPU holdouts still need
current exact source. Do not combine it with `--refresh-cpu-phase`. The interrupted
row-7 report lacks `unchanged_at_completion` and is rejected, as is
`all8-current-summary.json`; row 7 needs a completed canonical calibration before
it can supply automatic forecast calibration. This bridge is not all-eight GPU
qualification. The [validation record](validation.md#complete-headless-robust-and-manual-capacity)
retains the structural proof, receipt and actual consumer check.

When the target has an uncalibrated H0 fine cost class, run its bounded local
stage calibration and keep the independent holdout separate. This stage command
uses local automatic `S=2*H`; the calibration's worker count must match it.

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --calibration-stage noise-only-fine --case-index 7 \
  --workspace-mib "$ROBUST_WORKSPACE_MIB" \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-noise-fine-7.json
```

Add `--calibrate-target-noise-case-index 7` and
`--target-noise-calibration-report local-evidence/robust-noise-fine-7.json` to
the transfer forecast. This measures only the H0 fine class on the seeded noise
observation after public acquisition. The nominal fit still uses the donor's
CPU timings. The H0 stage report carries its own source, configuration, CPU,
workspace and elapsed cost.

The measured row-7 target trace took 22.713 seconds inside the workload
driver. Its separate H0 fine calibration took 20.574 seconds on the local CPU.
Count the independent local holdout, process startup and any cloud transfer
or setup in end-to-end elapsed time as well. The component times remain
separate in the reports.

Forecast v2 keeps signed and relative residuals for every held-out stage. For
nominal service, the sum of absolute stage errors must fit within 15% of
observed whole service, with each stage contributing at most 10%. For the
conservative estimate, sum the absolute recurring-bank error, the largest
absolute acquisition-branch error and the largest absolute fine-branch error.
Their sum must fit within 20% of observed conservative service, with any
individual stage contributing at most 15%. Measured fixed CPU costs are charged
once in each service estimate. The budgets prevent cancellation between stages.
Exact inputs, decisions, workload geometry and source/runtime gates still apply.
The held-out erasure cost must fit under the observed state-ceiling estimate.

The engineering error budget `b` guards the combined local calibration interval
`[L, U]` as `[L/(1+b), U/(1-b)]` before the independent holdout. This allowance
is separate from the statistical timing interval and its coverage limits. Report
nominal and conservative estimates and any numerical point failure separately.
Missing work or a failed holdout keeps the affected verdict unresolved. The
previous relative-only rule rejected the row-7 hybrid H0 acquisition timing.
That failure remains in the [validation record](validation.md) under its own
policy and source identity.

The forecast tool's exit code 0 and `conditional_estimates` status indicate
supported conditional estimates, **not an all-case real-time capacity pass**.
Inspect each row's separate `nominal_service` and `complete_capacity` verdicts,
intervals and point failures. The full report must cover the entire current
ordered list, including duplicates; selected-row evidence needs an explicitly
matched coverage assembly before any full-list conclusion.
These verdicts remain within their configured-calendar scope. Report additional
waveform-address acquisition capacity as unresolved until its algorithm, work
and detection/error behavior are qualified.

When a same-row timing check drifts while its input and work gates still match,
refresh just that CPU phase. `--refresh-cpu-phase` requires the same CPU source,
native artifacts and runtime as the existing calibration. Changed CPU work or
runtime needs a new applicable calibration report; this flag cannot bypass the
identity gates, and old receipts must retain their original provenance.
For a targeted forecast, supply the existing
current local calibration, GPU work and paired references, and the earlier
independent holdout for the selected row. Use one `--case-index N`, the matching
`--holdout-case-index N` and a fresh output path. Add
`--refresh-cpu-phase nominal-fine` for continued fine work or
`--refresh-cpu-phase recurring-acquisition` for the largest recurring H0 search.

The following example shows the artifact layout of the earlier 12-worker RTX
run. Replace these paths with applicable reports for the current source; old
artifacts cannot bypass the identity checks.

```sh
python3 tools/triad_gpu_cpu_forecast.py \
  --refresh-cpu-phase nominal-fine --case-index 2 \
  --cpu-report local-evidence/robust-v2/calibration-complete.json \
  --gpu-report local-evidence/robust-v2/gpu-full.json \
  --local-holdout-report local-evidence/robust-v2/holdout-row-2.json \
  --holdout-case-index 2 \
  --reference-case-index 1 --reference-case-index 2 --reference-case-index 6 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-v2/forecast-refresh-row-2-next.json
```

The refresh preserves the original suite's statistical family. It prepares one
fixture, seals the new point and interval before timing a fresh receiver, and
keeps the other independently measured phases from their earlier receipts. The
nominal refresh regenerates DATA once. The recurring refresh uses seeded noise
and a canonical public bank, with bounded tiles and measured full-count
selection where required. The output records the observed operating profile,
artifact hashes and separate elapsed costs. Its mixed-time evidence applies to
the measured conditions. Read `phase_refresh.status` for the new timing check
and the main verdict for the complete service estimate.

Terminate disposable instances after the measurements. Keep only reports that
support the current validation, cached reference or an unresolved claim.

## Absolute manual-case capacity

When local timing calibration needs refreshing, run `./build.sh test triad`
followed by the manual-case tool.

```sh
python3 tools/triad_manual_cases.py \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export
```

**This command estimates per-row service with sequential windows and automatic
`S = 2*H` software workers** (twenty-four over twelve hardware threads on
this host). Its native continuous functional gate does not qualify
the automatic pipeline's capacity across the manual rows. Keep that
execution-mode distinction in every report; all-row parallel capacity is
unqualified.

Default operating RAM is cgroup-aware available memory minus
`min(2 GiB, max(512 MiB, 10% of available memory))`, with no 50% or 3840 MiB
default ceiling. Explicit workspace caps remain exact. Current free memory can
cause a late per-row fixed-cap admission rejection. Retain that rejection and
any timeout; continue only unfinished rows in fresh processes with unchanged
source, method, inputs, error-budget family, workers and cap. Do not remeasure
completed favorable rows or silently reduce the allocation. Label assembled
coverage and retain each constituent receipt and its completion limits; follow
the [residual-run rules](robust-manual-capacity.md#fast-local-cpu-capacity-measurement).

The default compares serial, known physical cores and the full visible logical
budget on one stream, then measures fresh complete inference with `S` when
allocation flags are omitted. On this 6-core/12-thread CPU the pilot uses
1, 6 and 12 workers and default inference uses twenty-four. Explicit
`--full-logical-inference` selects `H`; `--threads W` preserves exactly `W`,
including intentional overcommit. `--no-full-logical-inference` is an explicit
diagnostic/user opt-out that selects the pilot winner.
Neither a pilot winner nor power/thermal limits or temporary/minor timing
differences justify reducing the default. Follow the
[CPU allocation contract](development.md#receiver-cpu-allocation). `--sustained-allocation-probe`
adds a bounded matched six-second comparison when investigating scaling. Read
elapsed service, busy cores, work counters, windows and workspace separately.
Automatic canonical continuous reception grows admitted owners with ready work;
it is not capped at three. The hardware-utilization denominator remains twelve
on this host while default software permits are twenty-four. Explicit worker
caps remain exact. Memory admission can select fewer owners, preserving the
worker budget; finite and explicit-serial modes also default to `S`. The earlier
three-controller 15% gain gate failed, but the user accepted its modest
throughput gain and higher productive utilization under a changed criterion.
That is historical evidence, not the new scheduler's qualification.
Keep that history separate, keep numerical libraries single-threaded, and
qualify the actual regime's costs. See the
[allocation contract](development.md#receiver-cpu-allocation).

The tool rereads and strictly validates `_tests-RobustModem.md` by default on
every invocation. It supports `--case-list PATH` for another complete manual
list and repeatable `--case-index N` for an explicitly partial debugging rerun;
it still validates the whole list, rejects stale indices, and marks partial
reports. A partial result cannot support a full-list conclusion. The
compatibility invocation `triad_abbreviated.py --cases` forwards to this same
absolute-capacity implementation.

For debugging without the current-source native message gate, add
`--skip-message-gate` (and normally a selected current `--case-index N`):

```sh
python3 tools/triad_manual_cases.py --case-index N --skip-message-gate \
  --output local-evidence/robust-cases-debug.json \
  --exporter build/dev/triad_helper_export
```

This mode cannot produce an integrated suite pass. Do not use it for a
capacity qualification. The former `--paired-followup` option is removed.

The absolute CPU metric is elapsed, prepared receive service per second of
received baseband for a single input stream: a ratio at or above 1 fails the
real-time criterion, while 0.1 represents a 90% service-time margin. Do not
divide elapsed service by core count or infer the whole-machine ceiling from
an unmeasured stage. The complete receiver's fixed affine-only lane supplies
operational admission and the final bit; adaptive and combined lanes remain
diagnostic, though their work is still fully executed and timed. Fine
likelihoods default explicitly to FP64 after high-SNR Gaussian FP32 failed;
there is no adaptive precision fallback. Coarse sensitivity alone does not
establish operational admission, full Robust reception, physical-channel
population performance, or message success. Exact caps and uncertainty rules
are in the [manual capacity contract](robust-manual-capacity.md).

Physical quiet evidence uses complete orthogonal likelihood blocks wholly
inside the native public aperture: DATA public support intersected with the
search window and bounded by the neighboring latest/earliest possible DATA
slots. All received samples remain inputs to fine likelihoods. Neither packet
length nor transport EOF controls physical completion. The bounded cost gate
charges a fresh one-second, two-segment 256-initial-state transition probe once
per 0.5-second window segment to cover switching and merge work conservatively.
The frozen native functional gate passed, including its captured-prefix EOF
negative control. This is one bounded functional check, not capacity or
all-case qualification. The earlier integrated v3 reevaluation measured its
eight manual cases with unresolved worst-state margins and no full capacity
pass. Its named `triad` group passed 20/20 before that run. Those receipts and
the historical v2 phases retain their own hashes and policy; they do not qualify
the latest native CPU changes. Sensitivity
confirmation remains incomplete, so there is no >90% qualification. See the
[validation record](validation.md) for per-case ratios and provenance. Use
`--skip-message-gate` only for debugging: it bypasses the
current source message gate and prevents an integrated suite pass.

The headless `CompleteStreamReceiver` defaults to continuous captured-stream
listening on its configured public calendar, with the full visible logical CPU
budget, bounded output/backpressure and scored-quiet recovery. Two scored quiet
slots establish physical completion; transport EOF and received length do not
select the endpoint. Finite gates explicitly use `continuous=False`. This does
not yet qualify unconfigured waveform epoch/ordinal/timing acquisition, and
the headless path is not connected to the Robust GUI.
See [current architecture](robust-current.md) and
[protocol status](protocol.md).

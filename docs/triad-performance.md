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
simulation, preparation and transfers. The current RTX PRO 6000 trace still
spent about 80% of its time on native CPU simulation and bank preparation;
optimizing only small GPU kernels misses that measured bottleneck. See the
[current evidence](validation.md#gpu-assisted-receiver-measurement).

Package the allowlisted source with the matching native exporter and mapped
kernel. Transfer both artifacts separately with authenticated SSH/SCP and place
the kernel beside the exporter. The provider console handles pod lifecycle.

```sh
python3 tools/triad_gpu_bundle.py --output /tmp/robust-gpu-source.tar.gz
```

On the pod, run the bounded CUDA workload and predeclare representative paired
reference rows spanning every arithmetic class. A full-list example uses rows
1 and 2 for the tight-clock class and row 6 for the bulk FP32 class.

```sh
python3 tools/triad_gpu_workload.py \
  --device cuda --workers 12 --workspace-mib 4608 \
  --reference-case 1 --reference-case 2 --reference-case 6 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-gpu-paired.json
```

CUDA coarse acquisition is the default with `--device cuda`. With no
`--workers` override, the workload uses the full visible logical host CPU
budget and one numerical thread per receiver worker. The example pins 12
workers so the paired and target runs use the same allocation; it is not a
portable thread cap. Preserve the [full-budget default](development.md#receiver-cpu-allocation)
on other processors. To repeat only
a target row, use `--case-index N` and retain the qualified paired GPU report.
The two reports must match the actual GPU backend, runtime, worker allocation,
workspace cap and source/native artifacts. Reference rows must still match their
current normalized configurations.

```sh
python3 tools/triad_gpu_workload.py --case-index 7 --workers 12 --workspace-mib 4608 \
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
  --device cpu --case-index 7 --workspace-mib 4608 \
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

When the target has an uncalibrated H0 fine cost class, run its bounded local
stage calibration and keep the independent holdout separate.

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --calibration-stage noise-only-fine --case-index 7 --workspace-mib 4608 \
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

The following example uses the 12-worker RTX trace and paired references from
the current eight-row run.

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
  --exporter build/dev/triad_helper_export \
  --workspace-mib 4608
```

The 4608 MiB cap remains subject to the half-visible-memory guard. Retain any
workspace-admission failure and use a fresh matched selected-row continuation
before assembling complete coverage.

The default compares serial, known physical cores and the full visible logical
budget on one stream, then measures fresh complete inference at the full
logical allocation. On this 6-core/12-thread CPU the pilot uses 1, 6 and 12
workers. `--no-full-logical-inference` is an explicit diagnostic/user opt-out
that selects the pilot winner; `--threads N` sets an explicit diagnostic count.
Neither a pilot winner nor power/thermal limits or temporary/minor timing
differences justify reducing the default. Follow the
[CPU allocation contract](development.md#receiver-cpu-allocation). `--sustained-allocation-probe`
adds a bounded matched six-second comparison when investigating scaling. Read
elapsed service, busy cores, work counters, windows and workspace separately.

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
all-case qualification. The full integrated v3 reevaluation measured all
eight current manual cases with unresolved worst-state margins and no full
capacity pass. The named `triad` group passed 20/20 before that run. Historical
v2 base and continuation phases retain their own hashes and policy. Sensitivity
confirmation remains incomplete, so there is no >90% qualification. See the
[validation record](validation.md) for per-case ratios and provenance. Use
`--skip-message-gate` only for debugging: it bypasses the
current source message gate and prevents an integrated suite pass.

The current headless `CompleteStreamReceiver` adds bounded captured-stream
processing with public slot plans, complete fine tracking, and two scored quiet
slots to establish physical completion; transport EOF and received length do
not select the endpoint. The headless path is not connected to the Robust GUI.
See [current architecture](robust-current.md) and
[protocol status](protocol.md).

# Robust computational checks

The Robust tools answer two different questions. The ordinary
[`tools/triad_abbreviated.py`](../tools/triad_abbreviated.py) harness compares
bounded computational work against a matched baseline to detect relative
degradation. The separate [`tools/triad_manual_cases.py`](../tools/triad_manual_cases.py)
suite evaluates absolute receiver capacity for the freshly read manual case
list. A pass in one mode does not imply a pass in the other.

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

## RunPod GPU-assisted full-workload diagnostic

The current CUDA driver defaults to the complete eight-row list. Build a
matching native exporter and `triad_mapped_kernel.so`; transfer both artifacts
separately with the allowlisted source over authenticated SSH/SCP. The
[`triad_gpu_bundle.py`](../tools/triad_gpu_bundle.py) utility creates the small
source archive; transfer the matching exporter and kernel as separate files:

```sh
python3 tools/triad_gpu_bundle.py --output /tmp/robust-gpu-source.tar.gz
```

Place the kernel beside the exporter so the driver resolves and hashes it.
Run remotely through SSH:

```sh
python3 tools/triad_gpu_workload.py \
  --device cuda --workers 12 --workspace-mib 4608 --coarse-cuda \
  --reference-case 3 --reference-case 4 --reference-case 6 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/gpu-rtx-4608-final.json
```

The 4608 MiB workspace cap remains subject to the half-visible-memory guard.

This schema-2 `bounded_mixed_precision_cost_regime_observed` run measures the
full workload with CUDA coarse search and selected fine work. The three
`--reference-case` rows compare matched CPU reference stages. Measured results
were 2.11–14.71x for CUDA coarse-data work and 1.73–2.33x for continued fine
work versus the pod's 12-worker CPU, while native bank processing was only
1.01–1.32x. Bulk CUDA used FP32; small Gram/Gaussian decision work used FP64.
Three paired reference gates passed. This is a fast diagnostic workflow, not
full receiver qualification or a local CPU throughput conversion.

Forecast only against source-matched full CPU/GPU reports and an independent
local holdout at the same workspace cap. For pure row-5 to row-7 transfer, use:

Create the independent local holdout with the same cap and fresh row-7 work:

```sh
python3 tools/triad_gpu_workload.py \
  --device cpu --workers 12 --case-index 7 --workspace-mib 4608 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-cpu-holdout-7-4608.json
```

This selected-row run also obeys the half-visible-memory guard. Preserve any
prior workspace-admission failure in the full CPU report; the holdout is a
separate fresh measurement.

```sh
python3 tools/triad_gpu_cpu_forecast.py \
  --cpu-report local-evidence/robust-cases-12-complete.json \
  --gpu-report local-evidence/gpu-rtx-4608-final.json \
  --local-holdout-report local-evidence/robust-cpu-holdout-7-4608.json \
  --holdout-case-index 7 \
  --reference-case-index 3 --reference-case-index 4 --reference-case-index 6 \
  --transfer-case 7=5 \
  --output local-evidence/robust-gpu-cpu-transfer-7-final.json
```

For the hybrid row-7 experiment, also add
`--calibrate-target-noise-case-index 7` and write
`local-evidence/robust-gpu-cpu-transfer-7-noise-final.json`. For the same-row
report, omit `--transfer-case 7=5` and write
`local-evidence/robust-gpu-cpu-same-row-final.json`. The same-row
reports cover rows 3–8; rows 1–2 were rejected. Row-7 transfer matched
nominally (+9.318% versus its
independent holdout and -0.321% against the withheld full direct row), but the
11.63% H0 cache-hit discrepancy exceeded the 5% tolerance. Hybrid transfer's
nominal estimate passed while H0 acquisition cost drifted +46.92%, leaving
overall validation unresolved. Preserve stage gates and complete-capacity
verdicts; do not use a global GPU/CPU factor. These reports do not replace the
complete eight-row local CPU suite.

Use SSH/SCP and remote commands for software transfer, building and running;
the provider console handles pod lifecycle. Terminate disposable instances
after measurement and retain reports only while they support current claims.

## Absolute manual-case capacity

After `./build.sh test triad`, run the distinct manual-case tool:

```sh
python3 tools/triad_manual_cases.py \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export \
  --workspace-mib 4608 \
  --full-logical-inference --sustained-allocation-probe
```

The 4608 MiB cap remains subject to the half-visible-memory guard. Retain any
workspace-admission failure and use a fresh matched selected-row continuation
before assembling complete coverage.

The full-logical option retains the same-stream 1/6/12-worker comparison and
then performs a fresh complete inference with 12 workers for every row. Read
the pilot selection and fresh 12-worker service, effective busy cores, work
counters, windows and workspace separately. High utilization alone does not
establish throughput; both measurements must complete the declared receiver
work on one stream.

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

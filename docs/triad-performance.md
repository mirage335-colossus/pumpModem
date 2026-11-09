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

## GPU fine-work parity probe

[`tools/triad_gpu_workload.py`](../tools/triad_gpu_workload.py) compares a
small, predeclared selection of one to three current manual rows on CPU and
CUDA using the same native capture and completed public acquisition. For
example, after building the native exporter and installing the numerical Python
dependencies on a CUDA host:

```sh
python3 tools/triad_gpu_workload.py --case-index 7 --device cuda --workers 2 \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-gpu-workload.json
```

Indices are zero-based and are validated against the entire freshly parsed
manual list. This is a targeted diagnostic, not full-list coverage. One
capture per selected row exercises the nominal two-second prefix and 1/2/4
second continuations, a capped maximum-state transition and an interior
erasure. FP64 lane scores and work counters are compared, with retained-state
identities checked at the nominal checkpoints. The short endpoints can be
ambiguous or incomplete. CUDA linear algebra is initialized serially before
workers start; its startup and memory snapshots are reported separately from
the timed probe. Synchronization is included in GPU timings. Setup failures
may prevent a report entirely; recorded errors, changed source/list/binary
hashes, or parity failures produce an unresolved result.

`short_fine_work_parity_observed` is neither complete receiver qualification
nor a local CPU throughput estimate. Cloud CPU/GPU elapsed time cannot be
substituted for prepared service on this computer, and one paired short capture
does not establish speed, full-message success, workspace headroom, or a
statistical operating point. The requirements for local calibration and
complete work are in the [manual capacity contract](robust-manual-capacity.md).

For a remote diagnostic, [`tools/triad_gpu_bundle.py`](../tools/triad_gpu_bundle.py)
builds a compact, deterministic source archive with a per-file hash manifest:

```sh
python3 tools/triad_gpu_bundle.py --output /tmp/robust-gpu-source.tar.gz
```

Supply and verify a matching native exporter separately, record the source,
list and exporter hashes, and keep credentials and machine-specific local
calibration identities out of the bundle. The archive and temporary remote
environment are investigation artifacts; remove them when their purpose ends.
See the [validation record](validation.md) for the limited GPU observations.

## Absolute manual-case capacity

After `./build.sh test triad`, run the distinct manual-case tool:

```sh
python3 tools/triad_manual_cases.py \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export
```

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

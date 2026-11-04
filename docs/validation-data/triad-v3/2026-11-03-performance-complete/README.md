# Complete-scope performance regression work

**The entire requested suite is not yet qualified.** This record preserves the
expanded coverage, full-workload experiments, successful prediction chains and
failed qualifications. Passing component checks must not be promoted to a
complete-scope or release-assurance claim.

Use the [maintained matrix](../../../triad-performance-matrix.md) to select
checks and the [performance guide](../../../triad-performance.md) for routine
and focused qualification commands. Ordinary Robust integration and its
unrelated regressions were not run or changed. The earlier
[native/FFT trace evidence](../2026-11-03-performance-trace/README.md), including
its sustained checks and failed free-crystal result, remains valid within its
recorded scope.

## Evidence retained here

- [Sampled families](families/README.md): complete native export through receiver
  result IPC for Gaussian, phase/frequency, grouped, RBPF, CFO, static and ray
  receivers; frozen stage forecasts, independent full jobs, source snapshots,
  historical anchors, batch measurements, and all unsuccessful attempts.
- [Native coverage](coverage/native/README.md): focused compiler line/CFG
  instrumentation, actual branch assertions, corrected atomic counters and the
  earlier invalid counters. Header snapshots and object timestamps have an
  explicitly limited provenance meaning.
- [Decision controls](controls/README.md): near-threshold detection power,
  null-case false-failure uncertainty, full-only and outside-timer slowdowns,
  changed regimes, environmental controls and an actual sampled delay injection.
- [Python and GPU functional coverage](coverage/README.md): retained control-flow
  reports, the original failed covariance assertion, its corrected independent
  oracle comparison, and separate CPU/CUDA contexts.
- [Exact-identity FFT](identical-fft/README.md): independent full-bank checks on
  CPU and H200, actual loaded dependency identities and preserved rejected
  attempts. Equal implementation identities do not remove the absolute
  historical speed check.
- [Complete CPU routine](routine/README.md): the 2,999.858-second campaign,
  twenty-three workload decisions, actual serial costs, precision misses,
  hardware warnings and exact measured source snapshot.

These archives retain exact compressed reports and matching source where
available. Their verification commands check hashes and receipts without
repeating expensive simulations. Private cloud resource and account records
are deliberately outside the repository.

## What the experiments establish

The H200 direct campaign completed all twelve declared sampled-family cases in
645.067 seconds. Six staged cases independently qualified across the recorded
campaigns: Gaussian state and five phase implementations. Later reuse still
has to pass its current checks. The phase-graph 19-job fresh simulation batch
took 62.269 seconds against a frozen 62.100-second prediction. Its separate
cached batch retained 57.528 seconds of receiver computation, below its
requested 60-second minimum; it is not silently counted as a passed sustained
receiver check.

The first three-case GPU reuse run took 121.493 seconds. Gaussian and optimized
phase passed conditional reuse; graph remained unqualified. Its total-time
estimate reached the precision target while its smaller receiver-stage
interval was too wide. This exposed a stopping-rule defect: stage precision
must be checked before freezing a forecast used by stage-specific guards.
Old records retain the original rule and result.

The repaired split-stage rule qualified reference, reduction and tensor phase
in a 168.828-second campaign. A separate graph reuse took 40.721 seconds and
passed. The subsequent six-case reuse campaign took **225.037 seconds**:
Gaussian, reference, optimized and reduction passed; graph and tensor remained
unqualified. Graph's new full receiver endpoint disagreed with its conditional
interval. Tensor's receiver interval remained too wide at its final sample
count. These later failures are retained as current counterevidence, even
though each case has an earlier independently validated forecast.

A declared direct follow-up covered graph, tensor and the other six sampled
families in **370.251 seconds**. All eight direct decisions passed and reached
their timing-precision targets. This does not retroactively qualify the failed
models. The 225-second and 370-second campaigns were separate measurements;
their sum is not a measured combined routine or an assurance-matched speedup.

The tensor report also exposed avoidable work: 26.142 seconds of native export
continued after a capped receiver interval made conditional acceptance
mathematically impossible. The final routine-only guard stops those additional
stage looks while retaining one full endpoint. Nine focused tests and exact
report replay passed; the change has not received a new timing campaign and
26.142 seconds is not a measured counterfactual acceleration.

CPU stage qualification took 896.688 seconds for three cases and qualified
none. The Gaussian receiver forecast was above its independent full-job mean,
while a later phase case also disagreed with the shared generation estimate.
The six-block crossed input/order diagnostic did not establish a fixed input,
cache or environmental cause. These are unresolved prediction relationships,
not demonstrated implementation regressions or proven CPU-noise limits.

The previous phase runtime forecast from short prefixes underpredicted receiver time by
15.8% even though pipeline agreement looked much better. Separate stage guards
caught it. It is not reused as a successful forecast.

## Costs and acceptance limits

The complete CPU campaign finished in **49 min 59.86 s**. All 23 named warm
target decisions and all sixteen functional commands passed. Eleven timing
targets missed the 5% goal; prediction/direct-measurement and regression gates
still passed. Three raw hardware screens warned, and the original report was
incomplete because cold, coverage and inventory requirements remained unresolved.
The [routine record](routine/README.md) separates those outcomes and the later
reporting correction. Five-to-ten-minute complete CPU testing is not
demonstrated: reaching ten minutes from this run requires an 80% reduction.

The old 836.952-second routine and the new campaigns measure different scopes.
The 232.663-second unqualified staged Gaussian run cannot be advertised as a
saving over the 343.133-second qualified direct run: their assurance differs.
Likewise a 5% timing interval plus a model envelope is not a 5% full-runtime
accuracy interval. Component means, individual-run variation, model error and
whole-campaign cost remain separate in the reports.

The new sampled-family campaigns selected FP32 bulk arithmetic explicitly,
with the implementations' documented higher-precision preparation/reductions.
The native receiver remains its implemented FP64 path. Arithmetic stress tests
and numerical comparisons do not establish that these selections are globally
optimal on every processor or workload. Changing precision invalidates the
affected timing relationship and requires its numerical and timing checks.

The complete wrapper reports functional correctness, code-path coverage,
timing precision, prediction accuracy, hardware health and performance
regression separately. Normal CI uses reuse rather than first-use qualification,
preserves artifacts after failure and does not convert missing coverage or
models into a pass. A source-matched historical counter report does not prove
every numerical-library path, cold startup or memory regime.

After the timed campaign, the focused coverage gaps were addressed separately.
The new [native follow-up](coverage/native/followup-001/README.md) passed four
instrumented tests, five tiny CLI modes and two regular-build checks. It covers
the all-invalid observation and valid high-rank Gamma fallback; all nine
instrumented translation units were rebuilt after the input guard, with no
negative counters. The [fresh Python capture](coverage/python/attempt-04-dependency-closed/README.md)
passed all sixteen files in 53.43 seconds. Both collectors now check their
declared source/test/build dependencies before and after collection; both
receipts pass the wrapper's dependency check. This is declared-input binding,
not proof of every compiler include, dynamic import or semantic path. The
original timing report remains unchanged.

The post-campaign reporting fixes retain the existing prediction and regression
gates while separating the 5% timing goal and finite required gaps from scope
limitations. A new test path must have a matching coverage dependency receipt.
Narrow CI cache fallback keys allow strict validators to discover compatible
older artifacts; actual hosted cache reuse has not been exercised. The final
wrapper, workflow and staged-control checks passed 34, 11 and 9 tests
respectively. These checks are implementation evidence, not new prediction
qualification or a complete-suite false-failure guarantee. Existing per-harness
multiple-case/look adjustments do not establish a new pooled confidence level
for the complete combined campaign.

The authorized H200 rental was stopped and deleted after all results were
downloaded. It used temporary container storage and no persistent volume.
The observed allocation lasted about 2.1 hours at the displayed $5.30/hour,
approximately $11.2 before billing reconciliation. Uploads were small source
and binary bundles; no large sampled-capture upload was needed.

Still unqualified are cold startup/first-use predictions, unrepresented
memory/concurrency transitions and portable confidence-qualified predictions
for unmeasured architectures. Exact full-work counts and geometry guards
prevent extrapolating through these changes. Five-to-ten-minute complete CPU
operation and comparable-assurance computational savings remain to be
demonstrated; the retained failures are not permission to widen tolerances.

The remaining finite inventory gaps are standalone planner/statistics timing,
a validated forecast across a representative implemented resource boundary,
and an independently validated mixed-family campaign-throughput forecast.
These are distinct from the supported named direct measurements. The largest
practical blocker is the CPU abbreviation: independent checks rejected the
cheaper models, and the complete defensible measured route took fifty minutes.
No claim is made that fifty minutes is a physical minimum. Future work should
start with those specific failed relationships and stage receipts, not repeat
all historical campaigns. GPU graph endpoint disagreement and tensor timing
uncertainty also remain recorded rather than being retried into a pass.

The [classification replay](routine/classification-replay.json) separates the
original precision goal from supported warm decisions without changing the
measurements. Its coverage field intentionally retains the original receipts;
the [subsequent applicability check](routine/current-coverage-applicability.json)
records acceptance of the fresh coverage inputs separately. Full qualification
and the configured release-assurance gate remain incomplete. No hosted CI run,
public binary release or ordinary Robust receiver qualification was performed.

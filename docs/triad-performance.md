# Isolated three-carrier performance regression

Start with the [maintained coverage and prediction matrix](triad-performance-matrix.md).
It separates functional coverage, complete workload timings, validated forecasts
and unresolved regimes. Ordinary Robust integration is excluded.

**For agents adding or changing regression tests: follow the
[required efficient workflow](#required-efficient-regression-workflow) before
adding computation.** Development-cycle time is a design constraint. Preserve
coverage and predictive validity while using the least computation needed for
the declared performance decision; a longer test is not inherently stronger.

Use the appropriate entry point; a green guard is not full qualification:

| Purpose | Entry point | Decision and cost policy |
| --- | --- | --- |
| Routine CPU CI | [`triad_perf_guard.py --profile ci`](../tools/triad_perf_guard.py) | Broad screening; fresh confirmation of suspected slowdowns beyond 1.70×; hosted CI allows 600 seconds; the standalone CLI defaults to 300. |
| Development on available GPUs | `triad_perf_guard.py --profile gpu-dev --device cuda` | The applicable GPU families, a 1.30× threshold, and a default 600-second budget. |
| Prediction and complete-scope qualification | [`triad_perf_complete.py`](../tools/triad_perf_complete.py) and focused adapters below | Retains historical forecast validation, mean-runtime decisions, cold/sustained checks and explicit unresolved obligations. The engineering target remains 25 minutes; focused qualification can take longer. |

The fast guard can finish with **no confirmed performance regression and limited
assurance**. This accepts a greater chance of missing a regression to avoid
frequent false CI blocks. It never asserts that all runtimes are acceptable,
that an abbreviated forecast qualified, or that release certification completed.
Missing models or hardware, engineering warnings, insufficient timing evidence
and a spent work budget remain visible limitations. Actual correctness,
source/input integrity, work-count or execution failures remain blocking.

The separate [CPU CI null-blocking experiment](triad-performance-ci-null.md)
measures all-cause and statistical blocking across same-build A/A runs on CPU
allocations. Its baseline is excluded, its schedule is fixed without retries,
and allocation clusters do not establish independent physical hosts. The
[completed 64-allocation record](validation-data/triad-v3/2026-11-04-ci-null/README.md)
observed zero all-cause blocks. Its exact one-sided 95% upper bound is 4.57%
conditional on independent representative allocation clusters; unknown
physical-host grouping leaves a conservative bound of 95%. This is an
occasional qualification, not a CI prerequisite or evidence of regression
detection power. Keep the routine unit-plus-guard workflow unchanged and do not
repeat the cloud campaign for unaffected changes.

## Required efficient regression workflow

This is the maintained policy for future isolated three-carrier performance
work. Apply it to changes in test scripts, workload manifests and CI as well as
receiver algorithms. Keep mappings in the existing
[coverage and prediction matrix](triad-performance-matrix.md); do not introduce
a second qualification registry or make historical research a routine dependency.

1. **Cover distinct computation, not every parameter combination.** Map the
   affected transmitter, channel, acquisition, receiver, planner, streaming,
   arithmetic and CPU/GPU paths to cheap functional checks and representative
   full workloads. Include boundaries that change algorithms, fallbacks, work
   counts, memory use or concurrency. Use native/Python branch instrumentation
   where practical, plus explicit numerical-library and GPU regime checks.
   Function-entry witnesses, test names and parameter variety alone do not
   establish comprehensive code-path coverage. Mark exclusions and unavailable
   hardware explicitly; an untested path does not become covered to save time.
2. **Define the processing time being estimated.** Record full observation and
   search geometry, measured work counters and included stages. Generation,
   channel simulation, loading, preparation, transfers, receiver work and
   finalization belong in the target when the represented simulation includes
   them. Name cold startup, resident latency and multi-test campaign throughput
   separately, accounting for shared setup once. Simulated days are not processor
   wall time. An inner-kernel timer cannot stand in for an entire simulation.
3. **When forecasting, establish a predictive-validity chain for each regime.**
   Use source/build/input/arithmetic/environment-bound full and sustained runs
   to validate the relationship between abbreviated measured work and the full
   target. Freeze a forecast before independent full-target validation; retain
   raw errors, uncertainty and applicability limits. Check sustained behavior
   where warm-up, allocation, cache, transfer, thermal or scheduling effects can
   change costs. Do not scale a prefix through an algorithm or memory-regime
   boundary without evidence. A passing unit test or execution witness is not a
   runtime anchor. Hardware microbenchmarks alone do not establish receiver or
   cross-architecture scaling.
4. **Make routine forecasts reuse that chain.** Refresh only the inexpensive
   measurements needed to forecast current full-target time, preserving the
   validated work geometry and model-error allowance. Confidence and material
   slowdown decisions concern the represented **full workload**, not the small
   abbreviated call. Reuse compatible binaries, fixtures, sampled I/Q, DSP
   workspaces and unaffected-stage proofs. Invalidate affected relationships
   when source, work, arithmetic or environment changes; allow narrower reuse
   only with a checked dependency/transfer argument. Neither blind reuse nor
   automatic complete requalification is acceptable. Use file-only assessment
   when sufficient. Avoid repeated generation, setup and hashing inside timed
   cases unless those operations belong to the target.
5. **Choose the cheaper defensible measurement route.** Abbreviation is useful
   when it saves total development time at the required assurance. Prefer direct
   full-target measurement when modeling, validation or repeated sentinels cost
   more. The current guard deliberately uses direct named targets for that
   reason; its historical native/FFT and GPU campaign forecasts are not blanket
   predictors for every simulation. Extend inexpensive forecasting to missing
   costly families when independently validated savings justify it. GPU
   development checks are preferred when they improve turnaround and cover the
   relevant paths; they do not replace CPU-specific ISA, fallback or CI checks.
6. **Stop when the declared decision is supported.** Predeclare thresholds,
   looks and budget. Do not continue a resolved test to obtain a prettier 1–5%
   interval, rerun until green, widen a tolerance after seeing a failure, or turn
   a five-second diagnostic into a sustained campaign without a concrete gap.
   Escalate only the affected case when validity, regime or material-slowdown
   evidence requires it. Keep ordinary receiver and unrelated regression
   campaigns outside isolated work.

For a new or changed timing case, the maintained matrix/report must identify
its cheap coverage check, full target, measurement method, independent anchor
or direct-timing evidence, reuse/invalidation conditions and remaining gaps.
Report functional correctness, code-path coverage, timing uncertainty, prediction
accuracy, hardware health and performance decisions separately. Measure routine
command wall time including setup and confirmation overhead, and compare saved
computation with direct testing at comparable assurance. Do not claim savings
from shorter simulated observations or excluded setup costs alone.

The **25-minute engineering target is a ceiling to work toward, not a minimum
or an allowance to consume**. The normal guard budgets and measured roughly
5.5/7.7-minute CPU/GPU routines are described below. Occasional qualification
may take longer for a justified missing relationship; it must not be repeated
after every unrelated edit. Documentation-only edits require proportionate
link, command and consistency checks, not receiver benchmarking.

## Confidence and material slowdown

Use the selected tool's explicit decision contract. Keep these quantities
separate: the material slowdown ratio, directional error/confidence budget,
interval width, prediction-model error and individual-run variation. A very
narrow interval around a negligible slowdown is not a reason to block; an
imprecise observation is not proof of a regression. Do not demand a universal
5% runtime half-width before making a 30% or 70% slowdown decision.

The fast CI guard tests a **1.70×** threshold with a **1% workflow-wide**
statistical rejection budget; GPU development uses **1.30×** and **5%**.
It confirms suspects using the randomized balanced-block procedure below,
accounting for hypotheses, jobs and repeated looks. Its estimand is a median
block ratio, not a mean-runtime confidence interval. CI intentionally accepts
reduced detection power to keep false performance blocks rare across the whole
suite. The allowance is conditional on its stated assumptions; per-test
intervals and same-host repeats cannot establish an unconditional suite-wide
false-block rate. Preserve the null-control evidence and its clustering limits.

The separate qualification-oriented runner implements the earlier relaxed
criteria as **15% directional pass-error at 30% slowdown**, or **30% at 70%** in
its legacy CI profile, with a separate 5% per-test false-regression allowance.
These mean 85%/70% one-sided pass procedures under that contract, **not ±15%/±30%
interval widths or the current fast CI policy**. See the precise
[qualification definitions](#qualification-timing-precision-model-accuracy-and-regression-decisions)
before changing sampling. Historical ±5% model-validation requirements remain
part of their anchor identities; relaxing a regression decision does not
retroactively validate a failed predictor.

For a qualified mean-runtime comparison, an upper bound below the material
threshold supports a pass; a lower bound above it supports a regression;
overlap is inconclusive. The reject-only fast guard instead reports **no
confirmed regression / limited assurance** when confirmation is insufficient.
Do not relabel that outcome as a bound proving no slowdown. Correctness,
integrity and execution failures remain separate blocking outcomes.

When investigating a suspected regression, compare matched candidate/reference
work and inspect work counters, stage timings and hardware diagnostics. Shared
environmental slowdown can affect both builds; source-dependent slowdown can
also interact with cache or scheduling. Attribute a poor algorithm or
implementation only after evidence distinguishes these explanations. Additional
samples cannot repair an invalid model or systematic bias. Improve that weakest
link rather than repeatedly tightening already sufficient timing intervals.

## Fast guard commands and decision contract

Reuse compatible candidate/reference builds, sampled fixtures and coverage
receipts. Replace uppercase paths with existing artifacts; use a new output
folder for each run:

```sh
python3 tools/triad_perf_guard.py --profile ci --device cpu \
  --candidate-root . --reference-root REFERENCE_SOURCE \
  --binary build/dev-cli/triad_long --reference-binary REFERENCE_BINARY \
  --planner-binary build/dev-cli/triad_planner_perf \
  --planner-reference-binary REFERENCE_PLANNER_BINARY \
  --planner-bounds-workspace RETAINED_GAUSSIAN_WORKSPACE \
  --fixture-cache FIXTURE_CACHE --functional-defaults \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --hardware-baseline HARDWARE_PROFILE_JSON \
  --threads 2 --native-threads 1 --budget-seconds 600 \
  --output local-evidence/triad-performance-guard/cpu-001
```

For GPU development, use the same command with `--profile gpu-dev --device cuda
--precision fp32 --budget-seconds 600` and a distinct output. CUDA FP32 is the
full GPU-family profile; the explicit tensor family does not support FP64.
`--case HYPOTHESIS_ID` selects a diagnostic subset, which cannot claim complete
screening. Omitted optional planner, bounds, hardware or coverage inputs are
reported as unavailable rather than silently counted as tested. Measure the
external command wall time when assessing end-to-end latency; the internal
budget does not include dependency installation/build or the final report write.

Both guards screen complete named workload geometries. FFT captures and
persistent worker setup are reused where valid; sampled-family screens measure
each complete generation/receiver pipeline directly. Native simulations
include generation, channel, receiver and CLI completion. FFT screening includes
resident preparation and the full search bank; fixture generation and worker
startup are separate recorded costs. Sampled-family screens and confirmations regenerate their captures and include
generation, loading, preparation, transfer, receiver work and IPC in the complete
pipeline timer. Worker startup is a separately recorded campaign cost. Planner/resource targets
use full process timing. These direct comparisons introduce no new extrapolation
and do not require requalifying unaffected historical models. Cold compilation,
minute-long sustained checks and prediction qualification are separate tasks.

The earlier shared-generation family screen composed separately timed stages.
That estimate was not an independently validated complete-pipeline measurement.
The default now uses direct pipelines: retained accounting estimated roughly
28 seconds of additional CPU work, much less than a new multi-minute model
qualification. The shared composition remains a diagnostic-only constructor
option; it cannot silently substitute for direct measurement. Historical
profitable duration/frequency-bank forecasts remain available under their exact
qualification identities. Failed staged forecasts remain failed.

A screen nominates a suspect; it cannot block performance. Version 3 confirms
with **randomized balanced blocks**, each containing four complete executions:
C,R,R,C or R,C,C,R. The two candidate times and two reference times are summed;
the block is one statistical observation. Its two inner pairs are never counted
as independent observations. The estimand is the median randomized block ratio,
not the arithmetic mean runtime or a ratio of marginal medians. Existing
mean-runtime forecasts retain their separate qualification rules.

Each confirmation period creates fresh source workers in the assigned order,
warms the full selected case outside its target timer, then closes and reaps the
workers. Native, planner and resource targets already include fresh process
startup and exit. Raw context/PID or subprocess-launch receipts, target timings,
warmup, setup and cleanup costs are retained. These costs count toward the
campaign budget even when excluded from a resident target's timing boundary.
Immutable captures and frozen arithmetic choices can still be reused.

Assignment coins use fresh operating-system randomness and are frozen in the
recorded schedule before screening; simulation seeds remain reproducible.
`--order-seed` permits diagnostic replay but disables statistical performance
blocking. Repeating fixed source-order coins across CI runs does not establish
the randomized-inference assumptions.

The [exact sign/binomial test](https://www.itl.nist.gov/div898/software/dataplot/refman1/auxillar/signtest.htm)
treats ties as nonpositive. A
[Bonferroni allocation](https://www.itl.nist.gov/div898/handbook/prc/section4/prc473.htm)
spends one workflow budget across frozen hypotheses, jobs and confirmation looks:
`alpha_suite / (hypotheses × jobs × looks)`. CI defaults to 1%; GPU development
defaults to 5%. This is a per-workflow statistical allowance, not a lifetime
or unconditional hardware guarantee. Correctness, infrastructure and integrity
failures are separate from that allowance.

The conditional null requires the probability of a positive block contrast,
given earlier blocks and source-independent slot conditions, to be at most one
half. One sufficient condition is that candidate potential runtime is at most
the threshold times reference potential runtime in every slot, with no
source-dependent carry-over. The contrasts for complementary assignments then
sum to a nonpositive value, so at most one fair-coin outcome is positive.
This permits serial slot variation; it does not follow merely from equal
marginal runtime distributions. Source-dependent thermal or scheduling
carry-over can still violate the model. Fresh contexts and full-case warmup
address persistent worker effects without proving universal independence.
See [hierarchical benchmarking](https://kar.kent.ac.uk/33611/45/p63-kaliber.pdf)
for why many iterations within one worker do not replace fresh outer contexts.

CI normally has one look at 12 blocks; GPU development has looks at 12 and 20.
Larger job/hypothesis families or smaller error budgets can raise the frozen
counts. Confirmations run round-robin among suspects, starting with cheaper
cases. They stop without blocking when no remaining permitted look could reject,
or when the next complete block cannot fit the budget. An incomplete block is
retained but supplies no sign; it is not selectively replaced. No alpha is
reclaimed and no retry seeks a pass.

The standalone CLI’s 300/600-second defaults are effort budgets, not detection-power guarantees. Hosted CI supplies 600 seconds so modest scheduling variation around the measured five-minute run does not truncate its inventory.
Use `--budget-seconds 1500` when additional confirmation is justified. An expensive
suspect may remain unconfirmed. Neither a timeout nor a poor model alone makes
CI red. The broader qualification target remains 25 minutes; qualification is
occasional, rather than part of every correction cycle.

The [decision controls](../tools/triad_perf_guard_controls.py) retain the earlier
persistent-source failure and test the same latent slot effects with source
reassignment. They distinguish full-target, outside-target preparation and changed
work-regime controls. The
[actual context controls](../tools/triad_perf_guard_context_controls.py) use real
FFT/family A/A executions with disclosed logical slot-delay replay. Those small
experiments validate plumbing and context replacement, not a physical whole-suite
false-block frequency. Preserve the historical failures and report these scopes
separately from the synthetic Monte Carlo intervals.

Five-second arithmetic and tensor probes remain separate diagnostics, with
first-use costs and unavailable formats reported. A one-block hardware warning
cannot independently make the guard red. It can reveal a shared environmental
slowdown that cancels in candidate/reference ratios. Do not infer CUDA kernel
coverage, FP32 correctness or instruction utilization from a nominal dtype alone;
read the [GPU precision guide](gpu-precision-controls.md) before changing arithmetic.

The [current completion record](validation-data/triad-v3/2026-11-04-performance-completion/README.md)
records CPU/GPU command walls of **328.074/461.339 seconds**, all 33/36
workload screens and 18 functional receipts per run. Separate method assessments
qualified those finite measurement routes using actual full-target, cold,
sustained and source-bound coverage evidence. Neither routine found a confirmed
regression or functional/integrity failure. These walls exclude separate harness
tests and dependency/build setup; hosted GitHub Actions has not run. The GPU
profile includes extra cases and confirmations, so this is not an acceleration
comparison. The CPU sustained trend and unavailable hardware baselines remain
explicit limitations. The [earlier guard record](validation-data/triad-v3/2026-11-04-performance-guard/README.md)
preserves the persistent-context statistical failure; the new balanced-block
controls and their conditional limits are in the current record.

### File-only assessment and focused qualification

After obtaining a complete routine report, reuse compatible existing evidence:

```sh
python3 tools/triad_perf_complete.py assess-methods \
  --guard-report GUARD_OUTPUT/summary.json \
  --candidate-root . --reference-root REFERENCE_SOURCE \
  --binary build/dev-cli/triad_long --reference-binary REFERENCE_BINARY \
  --planner-binary build/dev-cli/triad_planner_perf \
  --planner-reference-binary REFERENCE_PLANNER_BINARY \
  --planner-bounds-workspace RETAINED_GAUSSIAN_WORKSPACE \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --lifecycle-report LIFECYCLE_SUMMARY \
  --lifecycle-anchor-directory LIFECYCLE_ANCHORS \
  --output local-evidence/triad-performance-guard/methods-001.json
```

This performs no receiver timing. It verifies raw full work, numeric/source/input
identities, timing boundaries, required inventory gaps, functional receipts and
instrumentation. It does not make performance acceptance or narrow timing
precision a prerequisite for qualifying a direct measurement method. If cold
proofs reside in a separate preserved report, supply `--lifecycle-cold-report`.
The explicit `--assessment-source-transfer` and `--guard-policy-source-transfer`
options support only the exact reviewed report-only transitions in the current
record; they are not permission to reuse arbitrary changed source.

When a relevant lifecycle regime actually changes, qualify that case separately:

```sh
python3 tools/triad_perf_lifecycle.py --mode qualify --device cuda \
  --precision fp32 --case mixed-cuda \
  --candidate-root . --reference-root REFERENCE_SOURCE \
  --binary build/dev-cli/triad_long --reference-binary REFERENCE_BINARY \
  --anchor-directory NEW_ANCHORS --output NEW_QUALIFICATION \
  --threads 2 --native-threads 1 --minimum-campaign-seconds 60 \
  --maximum-holds 3 --budget-seconds 1500
```

Use `--device cpu --case mixed-cpu --mixed-method direct --direct-cycles 6`
for the measured CPU direct route. Cold targets are separate named cases. A
new hardware/software/work regime may require a different sustained length;
the example is not a universal sufficient duration. Do not repeat qualification
merely because routine timing is noisy. Failed forecasts remain unselected;
direct measurement is preferable when forecast qualification costs more than
the work saved.

## Preserved qualification evidence

The [earlier complete-scope record](validation-data/triad-v3/2026-11-03-performance-complete/README.md)
preserves the routine run, prediction outcomes and coverage limits. The
[broader qualification record](validation-data/triad-v3/2026-11-03-performance-broader/README.md)
adds Blackwell GPU lifecycle qualification, planner/statistics direct timing and
direct resource-boundary timing. Those CPU/GPU reports remain incomplete; the
new guard does not retrospectively change their exits or resolve their holds.

The qualification-oriented runner's normal profile uses a 15% one-sided
per-test pass-error budget to rule out a slowdown greater than 30%; its legacy
`--regression-profile ci` uses 30% at 70% slowdown. Both retain a separate 5%
per-test false-regression budget. These are **not** the fast guard's workflow
policy and are no longer the normal hosted-CI entry point. The
[decision-policy record](validation-data/triad-v3/2026-11-03-performance-decision/README.md)
retains 23/23 supported CPU runtime gates in 12 minutes 21 seconds, versus about
50 minutes with width-driven stopping, without claiming whole-scope certification.

The native/FFT component is [`triad-performance.py`](../tools/triad-performance.py).
Its evidence includes complete native CLI simulations, full-geometry resident
FFT acquisition, named sampled-family workloads and a Gaussian-window helper.
Each has its own timing boundary and qualification identity. A passing execution
witness is neither source-line coverage nor a general duration predictor.
The [computational-family manifest](../tools/triad-performance-paths.json),
[native scenario manifest](../tools/triad-performance-native-cases.json) and
[triad development workflow](triad-development-workflow.md) define their finite
scope. Historical evidence is reusable only under its matching source, build,
workload, arithmetic, hardware and timing-boundary contract.

## Current broader qualification state

The qualification-oriented normal policy uses a one-sided 85% per-test pass
decision at a 1.30 slowdown ratio. Its legacy CI profile uses 70% at 1.70. Both keep a separate 5%
false-regression allowance and fixed-look spending. These decisions are separate
from the 5% timing-width diagnostic and from historical prediction qualification.
The engineering target for a complete qualification-oriented routine is 25 minutes.

On the RTX PRO 6000 Blackwell Workstation (96 GB), lifecycle002 conditionally
qualified its frozen mixed-campaign forecast and scoped process-cold / private
Triton-cache full-shape lifecycle checks. Its exact stack, scope, cache states,
and hold results are in the [broader qualification record](validation-data/triad-v3/2026-11-03-performance-broader/README.md).
Seven historical H200 staged proofs replayed on their matching stack; that replay
does not transfer H200 timings to Blackwell. The earlier CPU lifecycle forecast
remains inconclusive: its paired F+6W interval was [0.890392, 0.938725], outside
the [0.90, 1.10] equivalence band. Do not enable it as a passed CPU prediction.
The CPU direct all-nine-family lifecycle campaign completed with `auto` selecting
five cycles after its pilot (the fixed default remains six). Three paired
reference/candidate campaigns had reference times 104.398, 105.470 and 101.309 s;
candidate times were 102.029, 101.014 and 102.101 s. Each retained 72.5–75.7 s
of actual warm work. The normal-profile upper ratio bound was 1.032565, but the
last/first-quarter ratio was 1.144, above the 1.10 stability limit. Independent review accepted the raw-only lifecycle reclassification after verifying
21 protected timed/model/proof function bodies unchanged from 5dfa0492 and 12 raw-file hashes. The original CPU report and its exit
status remain unchanged; the 1.144 stability/environment hold means this is not a
routine pass.

Planner/statistics direct timing has passed for eight workloads in 42.635
seconds, and the direct paired/sequential resource-boundary comparison passed
for two regimes in 14.467 seconds. The prior cross-duration resource forecast
remains inconclusive and is not promoted by direct timing. These results close
named direct timing obligations, not general duration, memory-tier, or scaling
models. Current independent archive indexes are in the [broader record](validation-data/triad-v3/2026-11-03-performance-broader/README.md).

The latest archived CPU qualification-oriented routine
completed in 1684.791 s (28:05), 184.791 s above the 25-minute target. In
that original run, CPU warm-target decisions passed except broad FFT historical-
anchor applicability. Later focused CPU/GPU checks passed one endpoint each, but
the routine was not rerun after repair.
The VLF native case used `--native-direct-case native-vlf-low-cn0`. A separate
stopped savings experiment measured 57.819 s for the abbreviated arm and
46.949 s for the direct arm in its first matched block; these are arm costs, not
single-target runtimes. Its second abbreviated guard was inconclusive, so the
experiment is incomparable and supports no savings claim. The CPU routine also had
three raw one-shot hardware screen warnings: ratios 1.356 (stream), 1.314
(phase complex64) and 1.467 (phase complex128). These five-second screens are
not statistically confirmed regressions.

The latest archived RTX PRO 6000 Blackwell qualification-oriented GPU routine completed in 1170.758 s (19:31).
All twelve sampled-family cases and configured extensions passed. The normal
profile FHSS comparison interval [1.285225, 1.306855] crosses the 1.30 limit,
so its normal decision is inconclusive; the derived CI-profile decision at 1.70
is supported. Neither actual hosted CI workflow has run, and the run is not a
whole-scope pass.
The final accepted lifecycle-proof replay took 0.479253 s and passed its source
transfer checks without cold or mixed flags. It ran no receiver kernels and did
not rerun or revise the original incomplete routine; details are in the
[GPU routine archive](validation-data/triad-v3/2026-11-03-performance-broader/scientific-gpu-complete-002/README.md).

For complete-scope CPU qualification checks, use paired/direct named family measurement, include
planner and resource extensions, and do not automatically spend on the failed
CPU forecast. GPU lifecycle reuse is enabled only for the matching Blackwell
identity. The wrapper invocation
below shows the current direct CPU lifecycle options. Direct mode bypasses the
inconclusive forecast; an optional lifecycle anchor directory supplies an output-local cache only:

```sh
python3 tools/triad_perf_complete.py \
  --reference-root REFERENCE_SOURCE --binary build/dev-cli/triad_long \
  --reference-binary REFERENCE_BINARY --anchor-directory LEGACY_ANCHORS \
  --hardware-baseline HARDWARE_PROFILE_JSON \
  --family-route paired --family-mode direct \
  --planner-binary build/dev-cli/triad_planner_perf \
  --planner-reference-binary REFERENCE_PLANNER_BINARY \
  --planner-bounds-workspace RETAINED_GAUSSIAN_WORKSPACE \
  --resource-direct --lifecycle-mixed-method direct \
  --lifecycle-direct-cycles auto --lifecycle-anchor-directory CPU_LIFECYCLE_CACHE \
  --native-direct-case native-vlf-low-cn0 \
  --identical-fft-case free-crystal \
  --identical-anchor-directory EXACT_FFT_ANCHORS --fixture-cache FIXTURE_CACHE \
  --functional-defaults \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --device cpu --threads 2 --native-threads 1 --budget-seconds 2100 \
  --regression-profile normal --output local-evidence/triad-performance-complete/cpu-routine
```

The CPU direct lifecycle run is measured, with five cycles selected by
`auto`; a raw-only derived replay confirms the cold and nine-family target/work
contract; independent review accepted that derived classification after checking
21 protected timed/model/proof function bodies unchanged from 5dfa0492 and 12 raw-file hashes. The original exit and report remain
incomplete, with the 1.14435 stability/environment hold still unresolved. The historical F+6W CPU
forecast remains inconclusive and is not reused in direct mode. The ordinary
routine target is 1500 seconds. The local command uses a 2100-second hard cap
for practical headroom after the separately measured repair; it is not a promise
that a full routine fits or a new complete-run measurement. For GPU lifecycle reuse, use
`--device cuda --lifecycle-anchor-directory RTX_PRO_6000_LIFECYCLE_ANCHORS`;
keep it in a distinct output and matching device run. In CPU direct mode, the
optional lifecycle directory is only an output-local anchor cache, not a
qualified CPU forecast source. Do not substitute the H200
staged anchor directory for a Blackwell lifecycle identity.

## Focused prediction and qualification commands

For the native/FFT core component, reuse source-matched historical anchors and
compatible builds. This example does not include the planner, resource or
lifecycle extensions shown above. Qualification is a separate operation; the
aggregator never silently starts it. Replace uppercase paths with existing
artifacts:

```sh
python3 tools/triad_perf_complete.py \
  --reference-root REFERENCE_SOURCE --binary build/dev-cli/triad_long \
  --reference-binary REFERENCE_BINARY --anchor-directory LEGACY_ANCHORS \
  --family-route paired --family-mode direct \
  --fixture-cache FIXTURE_CACHE --hardware-baseline HARDWARE_PROFILE_JSON \
  --native-direct-case native-vlf-low-cn0 --functional-defaults \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --device cpu --threads 2 --native-threads 1 --budget-seconds 2100 \
  --regression-profile normal \
  --output local-evidence/triad-performance-complete/routine-001
```

Missing anchors, unsupported hardware and unresolved inventory rows remain
explicit. This command is not a claim that all current CPU models qualify or
that all qualification gaps are closed. The routine engineering target is
25 minutes; use the matrix's actual measurements.
Do not repeatedly launch qualification to turn an inconclusive result green.

The sampled-family route measures native waveform/channel generation and file
completion, then receiver loading, preparation, transfers, processing and result
IPC. The staged route measures generation separately and complete receivers on
cached sampled inputs. It shares generation measurements only for identical
generation profiles and retains their uncertainty once. Arithmetic stage means
are additive; geometric means are not. Both whole-pipeline and individual-stage
predictions must survive independent full-run validation. The observed CPU
phase-prefix failure and CPU cache-state discrepancy prohibit treating these
models as interchangeable. GPU cold lifecycle and mixed-campaign qualification
now exists for the RTX PRO 6000 Blackwell Workstation under its recorded source,
stack, and cache contract. The CPU lifecycle direct campaign is measured, and
independent review accepted its raw-only classification. Its stability/environment
hold remains unresolved and its original report remains incomplete. Neither
result transfers between backends.

The staged routine checks the pipeline slowdown decision and the timing
uncertainty needed by its independent endpoint guard. Once
a stage reaches its final permitted sample count, an interval too wide to
satisfy the existing conditional guard cannot be repaired by sampling another
stage. When the decision becomes decisive, the routine retains the required full
endpoint check. A terminal unresolved or infeasible result is frozen without an
endpoint and remains unqualified. The routine does not widen the guard, discard
an outlier, or qualify a model through early stopping. Qualification and historical proof replay retain
their original schedules. The narrow compatibility mappings for the two
recorded earlier drivers require unchanged measured code and policy constants;
they do not transfer arbitrary implementation changes.

For a named affected case, explicitly qualify on the intended backend:

```sh
python3 tools/triad_perf_staged.py \
  --reference-root REFERENCE_SOURCE --candidate-root . \
  --reference-binary REFERENCE_BINARY --binary build/dev-cli/triad_long \
  --case phase-graph-hour --device cuda --precision fp32 --mode qualify \
  --anchor-directory STAGED_ANCHORS --maximum-holds 9 \
  --budget-seconds 600 --output local-evidence/triad-performance-complete/qual-001
```

`--mode direct` measures the full target without a duration forecast (the
aggregator exposes this as `--family-mode direct`). Prefer it
when a model fails or costs more than direct measurement. The
[`sampled-family manifest`](../tools/triad-performance-families.json) declares
the concrete full workload, work dimensions and dependencies for each case.
In paired-family decision mode, missing applicable history selects bounded
direct full measurement and records that choice; it does not qualify the
missing model. Staged reuse still reports missing history as unqualified.

The broad/free-crystal cache issue was a manifest identity mismatch, not a
numerical-data mismatch: sampled arrays were identical while timer and temporary
output metadata differed. The strict identical FFT route requires the exact
retained `--fixture-cache FIXTURE_CACHE`; other routine routes can use that option
when needed. The legacy FFT reuse route prefers an eligible
exact-qualified format in FP32-then-FP64 order; qualification makes a fresh
selection. This was a manifest/cache identity issue, not a numerical model
failure. The focused GPU check passed in 33.771707 s ([archived details](validation-data/triad-v3/2026-11-03-performance-broader/scientific-focused-broad-001/README.md)): full reference and candidate runs
took 6.261369 s and 6.251684 s, and the single prediction/full-endpoint checks
passed. Its conditional normal interval [0.898242, 1.106221] includes the
historical engineering allowance. The focused CPU check also passed its single
prediction/full endpoint in 206.889412 s, with 33 training samples, full
reference/candidate times of 12.944703/13.118263 s, and interval
[0.882159, 1.148666]. The checks used qualified FP32 and preserved their source, fixtures and anchors. These individual checks do not establish future
coverage or rewrite the incomplete CPU routine. The 206.889 s focused CPU repair plus the original 1684.791 s routine sum to
1891.681 s (31m32s); this sum includes the original failed broad-HF pilot, which
was not rerun, and is not a newly measured complete-run wall time. No repaired
complete routine has been measured against the 25-minute target.

The strict identical free-crystal qualification took 47.034 s. One randomized
comparison measured 61.620 s for direct full work and 24.258 s for the abbreviated
route, a nominal 37.362 s (60.63%) per-use difference and roughly two-use
break-even after qualification cost. This is one randomized pair, not a repeated
savings estimate. The first absolute-baseline gate was inconclusive as expected;
the qualified anchor itself remains valid for its exact identity.

The optional [`triad_perf_identical_fft.py`](../tools/triad_perf_identical_fft.py)
route uses one timing stream only when both workers independently report equal
loaded DSP helpers, numerical/native libraries, execution controls, inputs and
geometry. It still checks absolute full-runtime history and a full-bank
sentinel; it never invents a paired implementation ratio. It times resident
preparation and scoring, excluding generation, startup, audit and report writes.
Select only qualified cases in the aggregator with repeated
`--identical-fft-case ID`, `--identical-anchor-directory PATH` and
`--fixture-cache PATH`. Ineligible deduplication is unresolved, not a silent
fallback or a newly qualified model.

Anchor reuse is identity-specific: affected source, workload, build, arithmetic,
numerical runtime, hardware, environment, thread count, adapter, timing policy
or measured geometry changes invalidate the corresponding relationship. An
unrelated documentation edit does not invalidate DSP source hashes. Identity
deduplication is an explicitly checked narrow reuse policy, not permission to
transfer forecasts across implementations or processor architectures.

Reuse compatible builds or configure both the candidate and its intended
reference. The reference binary is needed to make a native performance
comparison; if omitted, native cases still measure a prediction but explicitly
warn that implementation comparison is unavailable.

```sh
./build.sh configure --cli --build-dir build/dev-cli -- \
  -DBUILD_TESTING=ON -DDATAPUMP_COMPILER_CACHE=OFF
cmake --build build/dev-cli --config Release --target \
  triad_long test_triad_statistics test_triad_v3_source \
  test_triad_v3_channel test_triad_v3_receiver --parallel 2
python3 tools/triad-performance.py \
  --binary build/dev-cli/triad_long \
  --reference-binary build/previous/triad_long \
  --output local-evidence/triad-performance/run-001.json \
  --device cpu --threads 2 --native-threads 1 \
  --precision-target .05 --budget-seconds 1800
```

Use Python with NumPy, SciPy and PyTorch. The
[dedicated CI workflow](../.github/workflows/triad-performance.yml) pins its
numerical stack and builds reference and candidate binaries from immutable
source revisions. The complete aggregator defaults to 1,500 seconds per backend;
individual research drivers retain their explicit qualification allowances.
The preferred complete routine budget is 25 minutes. Occasional qualification
may take longer when needed to validate a relationship. Native build and Python
dependency setup are outside the per-backend budget; fixture creation, hardware
checks, native models, receiver measurements, witnesses and optional sustained
checks are inside it. A process-group deadline covers blocked descendants.
Incomplete or timed-out evidence never counts as a qualification pass.
The following options belong to the legacy native/FFT component and complete
qualification route, not to `triad_perf_guard.py`:

| Option | Meaning |
| --- | --- |
| `--profile ci` | Default required native, FFT, Gaussian-helper, and applicable path-witness cases. |
| `--profile quick` | Small smoke profile; `complete_required_coverage` is false. It cannot pass the full regression suite. |
| `--profile sustain` | Extended native/FFT scenario set; defaults to one 60-second sustained check when eligible. |
| `--stopping-rule decision` | Routine default: stop when the selected per-test slowdown decision resolves, with fixed-look error spending. Model applicability is still required. |
| `--regression-profile normal` | Default: 15% erroneous-pass budget, 30% slowdown threshold; 5% false-regression budget. |
| `--regression-profile ci` | Legacy qualification-oriented choice: 30% erroneous-pass budget, 70% slowdown threshold; 5% false-regression budget. |
| `--regression-profile strict` | Prior policy: 5% erroneous-pass budget, 30% slowdown threshold; 5% false-regression budget. |
| `--stopping-rule precision` | Explicit legacy measurement/qualification mode; continue toward the diagnostic width target. |
| `--precision-target .05` | Diagnostic maximum relative half-width. Drives sampling only in precision mode; it is not the routine acceptance criterion. |
| `--precision-target .01` | Optional tighter diagnostic width; does not force extra routine decision samples. |
| `--looks 5,9,17,33` | Predeclared cumulative timing sample counts. All looks are included in multiplicity adjustment. |
| `--validation-mode auto` | Reuse an applicable historical proof with a fresh full paired endpoint sentinel; otherwise qualify this case. |
| `--validation-mode qualify` | Force a new frozen forecast and independent full-workload qualification where that adapter has a duration model. |
| `--validation-mode reuse` | Reuse an applicable historical proof if available; do not silently spend a new qualification campaign. A missing/incompatible anchor leaves the model unqualified. |
| `--case ID` | Focus on named cases; repeat to select several. Historical qualification retains its original multiplicity; routine decisions remain per-test. Skips unrelated work and cannot claim complete CI coverage. |
| `--anchor-directory PATH` | Persist and find source/case/environment-bound forecast records. Default is the reusable `triad-performance-anchors` directory beside each output. |
| `--sustained-seconds N` | Request one predeclared sustained consistency check per eligible native/FFT family; accepts 0–3,600 seconds and defaults to zero, or 60 for `--profile sustain`. It is not a new CI or model qualification. |
| `--reference-binary FILE` | Binary for the paired comparison. Omitting it reuses the candidate binary and means native results cannot detect a change against a prior native implementation. |
| `--device cpu`, `cuda`, or `auto` | Measure CPU, CUDA, or available backends. Each backend receives its own budget. |
| `--hardware-baseline FILE` | Compare raw hardware probes with a compatible saved hardware profile. Missing/incompatible baselines remain unavailable, not passed. |
| `--target-profile FILE` | Retain a target profile for investigation. It does not authorize an architecture-transfer prediction. |

Select a new output path for each run; reports are not overwritten. The report
retains harness/application/binary identities, inputs, timings, selected
precision, work and geometry counters, predictions, anchors, intervals, and
unsupported/unresolved cases. Large raw reports belong in ignored
`local-evidence/`; maintained benchmark contracts and compact results belong in
`docs/`.

## What each timing prediction means

### Whole native CLI simulation

The native adapter launches the actual `triad_long` CLI for each timed sibling.
Each timed invocation includes process startup, the complete selected transmit
and sampled-channel path, natural output/filter tails, the full declared
candidate/basis work, final scoring, and output. It retains internal receiver
time, generation/setup/other pipeline time, and external startup/output time as
separate additive stages. The prediction target is external complete-test wall
time for the frozen `target_seconds`, trial count, seed protocol, complete input
rows, and fixed bank. This adapter uses the native receiver's current FP64
implementation regardless of the Python FFT precision policy.

For duration-model cases, the adapter runs two full-CLI duration siblings,
checks that they stay in the same measured work regime, fits stage rates against
actual row/basis counters, and limits extrapolation to at most eight times the
larger sibling's rows. Each independent timing observation is a block of one or
more complete CLI invocations; a discarded pilot chooses block size only. The
forecast is frozen before independent full-target validation. An invalid fit,
changed geometry, failed required witness, or unresolved prediction blocks a
supported pass. Cases without duration siblings run the full target directly.

The current profiles include the following representative durations. These
durations are simulation calendars requested of the modem, not CPU wall time.
The second duration used for training is a complete
simulation sibling, not a cropped sample array.

| Case | Training durations | Intermediate holdout | Full target | Main execution witness |
| --- | ---: | ---: | ---: | --- |
| `native-flat` | Direct | — | 262.4 s | Encrypted flat-basis simulation, complete stream, sampled white-noise path |
| `native-fhss-selective-burst` | Direct | — | 140.8 s | Two-ray HF, Each-65% perturbation, actual FHSS erasures, internal silent burst tiles |
| `native-vlf-low-cn0` | 2,304,001.6 s, 18,432,012.8 s | 36,864,025.6 s | 73,728,051.2 s | −60 dB-Hz, 0.001 Hz, 1/128 Hz point sampling; about 853 source days at target |
| `native-null` | Direct | — | 32.8 s | Complete no-transmitter/absent-candidate path |
| `native-wrong-key` | Direct | — | 32.8 s | Wrong-key candidate path |
| `native-frontend` | Direct | — | 32.8 s | Sampled quantizers, interference, continuous filter, overload recovery |
| `native-ou-path` | Direct | — | 32.8 s | Evolving OU rays, diurnal phase, outage |
| `native-gaussian-path` | Direct | — | 32.8 s | Finite Gaussian Doppler FIR path |
| `native-added-max` | Direct | — | 8 s | Maximal Added Symbols case; complete minimum private tile |

Durations come from the checked-in native manifest, with common CLI settings
(including epoch, chunk/block, and memory limits) retained in that manifest.
Small direct cases have no abbreviated prediction; they time their full target
repetitions directly. The very long VLF rows are low-rate point-sampled
simulations. Their source calendar does not imply equivalently long CPU
execution, and does not establish arbitrary multi-day path physics. These named
cases are broad witnesses, not a Cartesian sweep of all rate, shift, oscillator,
channel, modulation, burst, and spreading combinations.

Initial native model qualification uses independent full-target CLI executions on new
seeds. A frozen fit can be accepted only when the corrected equivalence
interval for mean log prediction error lies within the declared ±5% band for
reference, candidate, and their paired ratio, with matching full geometry.
Confirmation accumulates at predeclared counts of 3, 5, 9 and 17 full-target
executions per source, stopping at a supported decision or resource limit.
Every look uses a 97.5% two-sided interval and the same frozen forecast.
The per-execution relative-error check and stage comparisons remain diagnostic;
qualification concerns the mean log error, not every future execution.
An inconclusive result does not qualify the forecast. This separate confirmation
ledger is explained below; it is not the timing-family confidence ledger.

### Resident Python FFT acquisition

The FFT adapter times complete preparation plus the wrapped full scoring call
on a resident sampled fixture. Every call retains the fixture's full samples,
FFT length, lag/timing lattice, masks, preparation, templates/Gram data, rank
handling, and final score construction. It abbreviates only the number of CFO
alternatives. It predicts that same full-bank resident receiver workload; it
does **not** predict waveform/channel generation, Python interpreter startup,
a cold fixture load, the complete native simulation, or radio latency. Its
persisted geometry includes sample count/rate, FFT size, lag interval and step,
group tile, precision, and device.

A static work-count rule chooses direct full-bank measurement when training
plus validation work would consume at least 80% of the full bank. A discarded
pilot also selects direct measurement when preparation-inclusive abbreviated
work would cost at least 80% of its estimated full-call cost, or the estimated
full call is below one second. Otherwise it
fits a preparation/fixed/per-CFO cost model from short bank counts, with an
at-most-8× extrapolation bound. A fresh qualification freezes that forecast
before cumulative confirmation looks at 3, 5, 9 and 17 independent
full-bank measurements on the same complete fixture. Qualification uses the
same corrected mean log-error equivalence test as the native duration
model: the reference mean, candidate mean, and paired-ratio mean must each fit
the ±5% relative band. Individual holds are recorded, but every hold is not
required to fall within 5%. Routine reuse instead uses the conditional historical-proof procedure below,
including one fresh full endpoint pair. A direct full-bank case has no bank-size extrapolation and is tested at the full
receiver workload.

The retained Gaussian-window helper adapter measures its declared
preparation/scoring path and precision controls on its algebraic multi-block
fixture. Its family contributes a timing observation; it is not a whole
simulation or duration predictor.

### Executed path witnesses

`triad_perf_paths.py` runs separately from timing and emits function/branch
witnesses for the named computational families in the path manifest: Gaussian
state, reference/optimized phase-frequency likelihoods, CUDA graph and FP64
reduction paths, supported tensor phase/frequency paths, RBPF, grouped
acquisition, static-frequency and ray-frequency studies, and CFO batching.
The witness exercises declared branches such as empty/partial blocks, masks and
rank deficiency, tile tails, UCB, advection, reset/resampling and device paths.
Only branches available on the selected backend are applicable. A successful
witness means that the required named call ran; it is neither line/branch
coverage for every numerical state nor a duration/throughput forecast for those
algorithms. The native CLI adapter separately witnesses end-to-end channel,
waveform, encryption, erasure, burst and finalization paths in its manifest.

## Qualification timing precision, model accuracy, and regression decisions

Keep four uncertainties distinct: (1) uncertainty in the mean runtime; (2)
variation in a future runtime; (3) error in the abbreviated-to-full prediction;
and (4) drift or transfer to another source/build/host. More timing repetitions
can shrink the first term; they do not erase the other three. Under independent
normal log timings, mean uncertainty scales as `s / sqrt(n)`, whereas a single
future observation retains the `s * sqrt(1 + 1/n)` term. The
[NIST prediction-limit formula](https://www.itl.nist.gov/div898/software/dataplot/refman1/auxillar/predlimi.htm)
makes this distinction explicit. This motivates separating mean-model error
from the individual-run scatter used by the sentinel; it does not prove
stationarity or normality for a particular machine.

The qualification-oriented runner uses a **directional error probability per test**, not an interval-width
target. Normal uses an 85% one-sided pass procedure at a 1.30 ratio; CI uses
a 70% procedure at 1.70; strict retains 95% at 1.30. At the predeclared cumulative
looks 5, 9, 17 and 33, the paired upper slowdown bound spends its pass budget
divided by four per look (0.0375 normal, 0.075 CI, 0.0125 strict). Component-based
adapters additionally divide that budget across the unique components used in
their bound. The union bound controls erroneous early acceptance under the
stated statistical assumptions. It stops at the
first supported pass or regression, or the sample/resource ceiling. It does
not keep sampling a resolved test to refine its displayed runtime estimate.
The lower regression bound retains a separate 5% directional ledger in all
three profiles. The displayed pair of bounds is therefore not a simultaneous
two-sided interval, nor is there a simultaneous confidence claim for the
entire suite. There is no additional
case-count or three-diagnostic-metric penalty on this per-test decision.

Native, FFT and paired-family timings retain their established log analysis:
the estimand is the ratio of **geometric mean full-workload runtimes**, not an
individual future execution or an arithmetic mean in the presence of unequal
runtime variances. Staged predictions retain arithmetic stage means because
stage times add. Do not mix these estimands or transfer the historical
log-bias model to an arithmetic-ratio claim. The
[NIST one-sided mean interval](https://www.itl.nist.gov/div898/handbook/prc/section2/prc221.htm)
and [paired-observation analysis](https://www.itl.nist.gov/div898/handbook/prc/section3/prc311.htm)
describe the underlying Student-t inference; the fixed-look allocation here
is an explicit additional safeguard against repeated testing.

The old family-adjusted runtime-width diagnostics remain visible. Their 5%
target drives sampling only under explicit `--stopping-rule precision` and
historical qualification. A diagnostic width miss does not veto an otherwise
supported routine slowdown decision. Historical proof replay retains its
original stopping rule and is never retroactively promoted under this change.

Under the current implementation, outer samples are paired timed observations
made within persistent Python worker processes for FFT workloads. They estimate
resident-call timing in that regime; they do not estimate cold-process startup
variation. Native outer samples launch complete CLI runs. Both rely on
approximately independent stationary outer log runtimes for their Student-t
intervals. The current implementation records order, work geometry, and native
stage/quarter timing; it does not robustly remove arbitrary thermal drift or
scheduler contamination. If telemetry or the run sequence shows a changing
power/thermal regime, report the issue and withhold a precision-qualified
performance result rather than treating many correlated calls as independent
samples.

A precision goal is not a validity certificate for the extrapolation. The native
and FFT adapters also retain independent validation residuals and invalidate or
withhold unvalidated models. Native and FFT model residual qualification use a
finite-look equivalence test: the confidence intervals for the reference
mean log error, candidate mean log error, and paired mean log error must each
lie wholly inside the per-case ±5% relative-equivalence band. At each of the
four predeclared confirmation looks, the two-sided interval has 97.5%
confidence: α=0.10/4=0.025. For a truly out-of-band mean, false equivalence
requires crossing its relevant one-sided boundary, with probability at most
0.0125 per look under the stated assumptions. The union across four looks
bounds false qualification at 5% per case. Requiring all three metrics to pass
is an intersection–union test; this does not claim simultaneous interval
coverage across all metrics or all cases. Every forecast remains frozen,
every look is retained, and a resource stop stays unresolved. Individual holds
are not each required to be within 5%. This is not a future-run interval or proof every
residual lies within ±5%. Observed maximum residuals are useful scenarios, not
future coverage bounds.

The normal and strict profiles use a 30% regression threshold (ratio 1.30); the
explicit CI profile uses 70% (ratio 1.70). A **supported pass** requires valid
model qualification, direct full timing, or an explicitly conditional historical
model with its widened error allowance, and a per-test upper slowdown bound no
larger than the selected profile threshold. A **regression** is supported when
the lower bound exceeds that threshold. If the interval
crosses the threshold, the result is **inconclusive**. A conditional historical
model adds its frozen engineering allowance to the timing bound; this is not
an unconditional 95% bound on future prediction error. Missing coverage, numerical
mismatch, changed geometry, invalid model, or timeout is a failure/unqualified
result, never a successful performance pass. The suite stops after its maximum
look instead of spending arbitrarily for a preferred interval. A separate
**`severe_endpoint_prediction_mismatch`** blocks that qualification route when a failed full endpoint
underpredicts runtime by more than 30% even after subtracting its frozen
historical observation allowance and current timing uncertainty in log space.
This is an engineering **calibration failure**, not proof of a regression in
mean runtime. Its false-failure rate is unqualified: historical scatter is not
a proven future tail bound. Smaller violations, overlaps and absent history
remain visible unresolved warnings. The guard never enlarges an allowance or
retries the endpoint to obtain a pass.

## Historical proof, current speed, and the routine shortcut

Initial qualification and routine regression answer different questions.
Qualification validates the abbreviation's cost relationship with independent
full runs. Routine measurements estimate **current** stage coefficients; they
never reuse yesterday's processing speed as today's prediction.

Both adapters retain complete historical source/binary, harness and environment
hashes, frozen forecasts and raw independent full-run observations. Validators
reconstruct their predictions, errors, look order and decisions. Applicability
uses a separate explicit measurement contract. It still requires exact native
binaries or Python receiver module hashes, case/fixture/geometry, arithmetic,
timing boundary and recorded environment. Reporting-only harness changes can
reuse evidence under that reviewed contract. The specifically audited CPU002
v3 hash pairs have an explicit legacy mapping; unknown old protocols are rejected.
Original records are never rewritten or silently upgraded. Hashes provide
integrity/provenance, not cryptographic authentication of a trustworthy experiment.

A routine modeled case freezes the following before new full work:

1. Current abbreviated timing samples and the full-target forecast.
2. Verified historical full-run evidence and its applicability key.
3. Separate reference, candidate and paired **mean-model** log-error envelopes:
   the larger of a 10% engineering floor and historical mean-error interval
   endpoints. Mean-model plus current mean-timing uncertainty above 20% disables
   conditional support. Individual-run scatter does not become mean-model bias.
4. A separate **single-observation sentinel** envelope also retains the historical
   maximum absolute residual. Exactly one independent full-target paired endpoint
   runs with work and numerical checks before a decisive modeled pass or
   regression. If uncertainty is terminal or a case is infeasible, decision mode
   freezes the unresolved result without running an endpoint. Its discrepancy
   and training uncertainty must fit this frozen envelope; the envelope cannot
   expand after seeing it.

The resulting status is **`historically_supported_conditional`**, distinct from
fresh mean-equivalence qualification. The conditional regression gate widens the
current paired timing interval once by the historical paired envelope. Absolute
full-time estimates have corresponding separate engineering intervals. One
endpoint pair does not establish a new 5% model-bias confidence bound or coverage
of future runs. These intervals rely on the recorded cost relationship remaining
applicable; the sentinel is a practical contradiction check. An inconsistent or
uncertain sentinel withholds a supported pass and requests focused qualification.
There is no automatic retry until the check passes. A single sentinel cannot
guarantee detection of every 30% full-size-only slowdown; timing noise can mask
one observation. Negative controls measure its demonstrated behavior, not
universal detection power.

This permits cheap repeated checks after reporting/instrumentation changes
outside the timed receiver. Actual timed-receiver, binary, workload, precision,
or environment changes still require qualification of affected modeled cases.
It does **not** establish arbitrary cross-revision or cross-host transfer. Full
endpoint work is intentionally retained to catch cost changes that appear only
at the full workload. Cheap cases use direct timing and need no model anchor.

`auto` reuses an applicable proof or performs first-use qualification where
budget allows. `reuse` abstains when no applicable proof exists. `qualify`
explicitly runs fresh full holds. Keep the anchor directory across runs; changing
the output filename does not require new calibration. Use `--validation-mode qualify --case free-crystal` (or another
reported case ID) for affected-case qualification; a focused run is never a
complete qualification pass. Explicit qualification restores anchor and
sampled-fixture caches before measurement, and only trusted default-branch runs
save reusable caches. All restored records still pass normal identity/raw-proof
validation. A hosted runner with different hardware/runtime can legitimately
miss the cache's applicability checks; a cache hit alone never authorizes reuse.

The `--sustained-seconds` consistency run uses one qualified or conditionally supported forecast
per eligible family, selected by a predeclared rule: the eligible native case
with the longest full source duration and the eligible FFT case with the
largest full CFO bank. It
commits to the expected work, identity, seed, and complete geometry before the
single run. For native, it repeats complete full-target CLI trials until the
predicted native-pipeline work reaches the requested minimum; for FFT, it
repeats complete resident prepare/full-bank-score calls. It measures the actual
receiver/pipeline span and start/end block behavior. This check has no new
confidence interval and creates no qualification anchor; it is a sustained
consistency observation. It is disabled in normal CI (`0`), defaults to 60
seconds in the `sustain` profile, and runs only for an eligible exact forecast.
If its observed span misses the planned minimum or is much slower than predicted,
the retained outcome flags the mismatch for review. It does not trigger an
automatic retry or update the anchor.

## Hardware and arithmetic qualification

The hardware layer gives each warmed kernel a five-second target and records
first-use costs separately. CPU families include FP32/FP64 GEMM,
complex64/complex128 FFT, copying, phase products/reductions, small batched SVD,
and separate FP16/BF16 dispatch probes. CUDA arithmetic and explicit tensor
formats are measured separately; dtype alone does not prove specialized
instructions ran. The receiver chooses only between implemented FP32 and FP64
after comparison with a fixed FP64 reference. Strict FP32 does not silently
enable TF32, and no tensor FFT reception claim follows from hardware tensor
probes or off-line path witnesses.

Record completed CUDA work, not just asynchronous host dispatch time. Retain
CPU process time, wall time, device event time, shape, array bytes and
throughput. CPU process time per wall second is a utilization clue rather than
an instruction-set efficiency measure. A kernel result describes its measured
shape/cache regime; it is not a peak tensor/DRAM utilization guarantee. Missing
hardware counters stay missing. The optional `nvidia-smi` sampler provides
system-wide device/memory busy, clock, power and temperature observations; it is
not SM occupancy or per-process utilization.

CI additionally restores an immutable first-observed trusted hardware profile.
Its cache key identifies physical resource allocation and requested dispatch,
but deliberately excludes kernel, numerical-library and receiver-source versions.
This lets the raw hardware screen reveal environmental slowdowns shared by both
receiver implementations. A current comparison cannot automatically replace that
reference. Different CPU allocations abstain. A five-second block is a useful
throughput/utilization screen, not an independently replicated confidence bound
or certification that the original host represented ideal performance. The
actual-helper controls demonstrate that a synthetic hardware profile can raise
a warning; they do not establish universal physical slowdown detection. Paired
ratios can cancel common environmental degradation, and selected backend probes
do not observe every startup/import/disk activity or every CPU stage in a
GPU-hosted pipeline. Keep functional correctness, model validity and hardware
health as separate results.

In the historical RTX PRO 6000 Blackwell Server probe with Torch 2.8/Triton,
requested strict FP8 probes emitted FP16 tensor instructions and are reported as
unsupported, without fallback-format throughput. This is a tested
compiler/runtime result, not a claim that the GPU lacks FP8 capability.

The dedicated hosted CPU workflow requests AVX2 fallback dispatch in ATen,
OpenBLAS, MKL, oneDNN, and NumPy; it verifies physical AVX2/FMA capability and
observed ATen/NumPy dispatch. This is fallback-dispatch coverage, not complete
ISA isolation, cache/clock emulation, or binary portability certification.
The local AVX2 preflight was checked; hosted Actions results remain a separate
actual run. Library controls have build/version-specific limits: [ATen dispatch](https://github.com/pytorch/pytorch/blob/v2.8.0/aten/src/ATen/native/DispatchStub.cpp),
[OpenBLAS runtime selection](https://www.openmathlib.org/OpenBLAS/docs/runtime_variables/),
[oneDNN ISA control](https://uxlfoundation.github.io/oneDNN/v3.7/dev_guide_cpu_dispatcher_control.html),
and [NumPy dispatch inspection](https://numpy.org/doc/2.3/reference/generated/numpy.lib.introspect.opt_func_info.html).

## CI, qualification, and transfer

Normal CI runs `triad_perf_guard.py --profile ci` in the dedicated job unless
`devfast` is explicitly selected; it is separate from sanitizer/matrix
repetitions. New direct release builds depend on this exact-source guard. An
inconclusive performance result or engineering warning does not block it;
confirmed median slowdowns and actual correctness/integrity/execution failures
do. The workflow declares one performance job; a future matrix must update the
job count/index and retain a shared workflow error budget. A green guard leaves
`complete_scope_qualification` false.

Explicit trusted qualification retains bootstrap plus `triad_perf_complete.py`
under the normal qualification profile. It still exits nonzero for unresolved
finite complete-scope obligations. Direct timing can satisfy a named workload
without qualifying an unused forecast. Existing ordinary-release certification
remains separate; source-release and APT repackaging of older verified binaries
do not newly measure the current triad study.

Run focused qualification when changing a timing model, its computational regime
or a relevant source/arithmetic/environment identity. Freeze forecasts before
independent full workloads and retain residuals, counters, complete timing
boundaries and raw measurements. Reuse an unchanged relation only when its
applicability checks pass. Historical captures without timing provenance remain
contextual evidence; a successful guard cannot supply missing predictive validity.

No unmeasured architecture has a qualified runtime forecast. The current suite
explicitly marks cross-architecture transfer unqualified; a GPU GEMM rate or
hardware profile alone cannot predict the whole pipeline. Qualification on a
new host requires matched stage/work shapes, independent full-workload
validation, and a declared extrapolation envelope while retaining transfer,
serial, setup, memory/cache, and startup costs.

## Evidence and reproducible claims

The previous [2026-11-03 study archive](validation-data/triad-v3/2026-11-03-performance/README.md)
records the pre-native-duration suite, its measurements, rejected scaling model,
source snapshots and cleanup. Its 10-minute/20% policy, FFT/Gaussian adapters,
and measured host wall times are historical v1 evidence; they do not qualify the
current duration-model, adaptive-look or sustained-test implementation. Current
profile results must be retained separately with fresh source/binary hashes,
case identities, frozen prediction and full holdout checks. The separate
[traceability study](validation-data/triad-v3/2026-11-03-performance-trace/README.md)
records complete-simulation forecasts, independent confirmations, sustained
observations, failed precision targets and subsequent protocol changes.
Implementation tests alone do not establish predictive accuracy.

The suite uses paired randomized/interleaved executions because environmental
variation can dominate small timing differences. The statistical treatment
follows the benchmark hierarchy discussed by [Kalibera and Jones, *Quantifying
Performance Changes with Effect Size Confidence Intervals*](https://www.cs.kent.ac.uk/pubs/2012/3233/content.pdf)
and their [*Rigorous Benchmarking in Reasonable Time*](https://kar.kent.ac.uk/33611/): distinguish repeated measurements within a process from independent execution/process effects and allocate replication where variance and cost justify it. [Lan and DeMets' alpha-spending method](https://doi.org/10.1093/biomet/70.3.659) is a principled alternative when a variable schedule of repeated looks is needed; this implementation instead uses a fixed look schedule with conservative multiplicity adjustment. [Howard et al.'s confidence sequences](https://doi.org/10.1214/20-AOS1991) provide anytime-valid alternatives under their stated sub-Gaussian, Bernstein, self-normalized or other conditions; the suite does not claim those assumptions for arbitrary machine timing.

[Google Benchmark interleaving guidance](https://github.com/google/benchmark/blob/main/docs/random_interleaving.md)
supports spreading paired measurements across changing conditions. The guard freezes randomized pair order; historical adapters retain their
recorded interleaving protocols. Neither proves statistical independence
or remove thermal drift. [PyTorch's CUDA timing guidance](https://docs.pytorch.org/docs/stable/notes/cuda.html#asynchronous-execution)
requires synchronization or device events for asynchronous work. Keep timing
uncertainty, prediction residuals, future-run variation, hardware health and
architecture-transfer limits visible in every maintained conclusion.

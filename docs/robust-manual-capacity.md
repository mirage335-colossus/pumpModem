# Robust manual-case receiver capacity

The manually edited `_tests-RobustModem.md` asks **what this computer's CPU can
sustain for the complete receiver in real time**, using practical efficient
algorithms and all available hardware threads. These are absolute capacity
checks. When applicable local calibration exists, start with the
[GPU-assisted forecast](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
Use `tools/triad_manual_cases.py` for fresh CPU calibration. The former
`triad_abbreviated.py --cases` invocation forwards to the same implementation.
The ordinary `triad_abbreviated.py` performance regression asks whether measured
work degraded against a comparable baseline. A regression pass cannot establish
real-time capacity. Both tools use the same efficient, predeclared statistical
approach, with different decisions and explicitly recorded work coverage.

The current receiver implementation and validation are recorded in
[current architecture](robust-current.md) and [validation](validation.md).
Earlier coarse-only and isolated tracking measurements remain historical stage
evidence; they do not satisfy the complete-receiver contract below.

## Agent workflow for CPU ceiling estimates

This is the starting procedure for development agents without prior chat
context. The objective is a quick, defensible demonstration of local CPU
feasibility where supported, and an actionable bottleneck or uncertainty where
it is not. A passing relative regression, fast GPU kernel or hypothetical
optimized receiver cannot establish that outcome.

1. **Declare the question and freeze the measurement inputs.** Read the entire
   current case list, preserve all selected channel effects, and predeclare
   complete-work strata, measurement/error budgets and stopping rules. Record
   source, native binaries, runtime, CPU allocation, workspace and arithmetic.
   Run the applicable cheap correctness checks before timing; do not keep
   changing the harness during the final measurements.
2. **Use practical receiver algorithms and arithmetic.** Address dominant costs
   with reusable preparation, efficient search, batching, vectorization and
   parallel independent hypotheses within the same stream. Prefer qualified
   lower precision and tensor-compatible bulk GPU operations where they shorten
   the complete experiment. Expected quiet or mostly quiet spectrum, including
   FHSS, does not require extreme in-band dynamic range. Use the
   [precision policy](gpu-precision-controls.md) to check the actual sensitive
   stages. Incidental serialization or unnecessarily expensive arithmetic is an
   implementation constraint to improve, not proof of a hardware ceiling.
   Implement and qualify an optimization before counting its CPU savings.
3. **Reuse qualified evidence and measure only what is missing.** Follow the
   [GPU-assisted commands](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast):
   fast GPU traces supply receiver decisions, geometry and work demand;
   source-matched local CPU phase costs supply elapsed service. Calibrate with
   narrow sustained receiver work under the actual concurrency and cache/state
   regime, then use independent bounded local holdouts. Use the brief continued
   session below and record available operating conditions and duty cycle;
   startup-only timings or transient boost do not establish sustained service.
   Refresh drifting cost classes under unchanged identities; changed CPU work
   or runtime needs a new applicable calibration report accepted by the identity
   gates. Preserve other applicable calibration without relabeling old receipts.
   Synthetic CPU stress characterizes hardware scaling;
   it cannot replace receiver phase costs or supply a universal GPU/CPU factor.
4. **Demonstrate productive use of the available CPU.** Keep the full logical
   worker budget, one numerical-library thread per outer worker, and measured
   same-stream useful work. Check elapsed throughput, busy cores and work counts
   together. Investigate low utilization in the dominant stage; retain the full
   budget despite thermal/power limits or minor local timing differences. Fewer
   useful tasks in one stage do not justify a global cap. Follow the
   [allocation contract](development.md#receiver-cpu-allocation).
5. **Admit the forecast only after its checks.** Require applicable paired
   arithmetic/decision/work gates and independent local timing holdouts. Charge
   all [complete receiver work](#complete-receiver-work), including recurring
   preparation, unsuccessful candidates and quiet completion. Use the continued
   nominal measurement and the separate conservative state bound; do not replace
   either with GPU elapsed time or isolated favorable kernels. A failed gate
   calls for a bounded diagnosis or affected refresh, not looser thresholds or
   a full-duration campaign.
6. **Report capacity, not merely successful tool execution.** Cover every
   current ordered row, or label partial coverage. Report nominal and
   conservative seconds of CPU service per baseband second, their intervals,
   margin to 1 s/s, workspace, actual allocation/utilization and measured versus
   extrapolated work. Separate forecast accuracy, real-time capacity and
   communication sensitivity. Missing work prevents a complete-capacity pass;
   a point at or above 1 s/s is a point failure. Identify the dominant remaining
   cost and next bounded discriminator if feasibility is unresolved.

## Local calibration entry point

To refresh the complete local calibration, use the numerical Python
environment used by the build.

```sh
./build.sh test triad
python tools/triad_manual_cases.py --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-manual-capacity-current.json
```

The default compares serial, physical-core and full visible logical allocations,
then uses the full logical allocation for fresh inference. It performs the
current-source native message/quiet/EOF gate before measuring the current rows.
`--no-full-logical-inference` is an explicit diagnostic/user opt-out that selects
the pilot winner. Do not use it, or change the default, to compensate for power
or thermal limiting or temporary/minor timing differences; follow the
[CPU allocation contract](development.md#receiver-cpu-allocation).
`--case-index`, `--threads` and `--skip-message-gate` are diagnostic controls;
partial coverage or a skipped gate cannot produce an integrated suite pass.
For targeted comparison of sustained fine work, add
`--sustained-allocation-probe` with the worker pilot enabled. It reuses the
inference allocation's first three valid continued observations and takes at most three
fresh six-second observations at each distinct physical-core and full logical
allocation, within the visible quota. The two alternate allocations add at
most 36 seconds of repeated probe exposure to the same baseband slice and two
actual acquisitions per row. These are not unique stream seconds or independent
communication trials. All use
the real resident bank and require matching public proposals, retained state,
native cache work and DSP counts. Only declared worker dispatch counts may
differ. The report separates measured fine service from any supported
slot-normalized fine forecast. This exploratory comparison does not select
the inference allocation or qualify whole-receiver service or CPU ceiling.
Exit codes are 0 for conditional capacity pass, 1 for measured capacity failure,
and 2 for unresolved or invalidated evidence. JSON and its neighboring Markdown
report record the limits; a capacity pass is not a success-probability certificate.

The SNR candidate uses the complete receiver's actual fixed grid and the normal
affine residual/model priors, including the duration-dependent frequency step.
It remains a modeled true-component target until actual complete-path trials
qualify acquisition, wrong-bit decisions and physical completion. A separate
cheap diagnostic replays the simulator's known noise seed to recover the clean
sampled waveform, applies the canonical quiet projection, and estimates
DATA-to-QUIET probability under known proper-complex Gaussian noise. This is
conditional on that one channel realization; it is outside receiver CPU timing
and cannot supply receiver initialization or a population/message success claim.
A bound near one is inconclusive, not an observed false-end probability: an
admitted DATA decision takes precedence over the quiet statistic.

## Complete receiver work

A complete capacity estimate must account for received-baseband filtering,
Helper processing, full public coarse search, fine synchronization/refinement,
continuous tracking, bit/null decisions, quiet-slot completion, bounded source
decoding and result presentation. Include unsuccessful candidates, noise-only
listening, interior loss and reacquisition. Candidate-dependent setup, template
cache fills, key/ordinal changes, dispatch, synchronization and merging are
receiver work. Only reusable preparation before reception and simulator
generation may be excluded from sustained service; report readiness separately.

Receiver inputs are the samples and public local configuration. Evaluator truth
such as transmitted bits, actual offset, selected epoch, channel state or message
length must not choose the receiver's hypotheses, endpoint or decision. Coarse
proposals may select fine candidates, but coarse and refined scores from the
same observations must not be added as independent evidence.

Operational DATA admission and final bit selection use the fixed initial-affine
lane with the complete public-prior correction. The adaptive lane and combined
scores remain diagnostic but are fully executed and charged. Fine Gaussian
processing defaults explicitly to FP64 after a reproduced high-SNR FP32
innovation failure; numerical guards remain enforced and precision does not
change in response to an observation. This localized numerical requirement does
not prohibit qualified lower-precision bulk work; see the
[current stage distinctions](gpu-precision-controls.md#current-implementation).

## Local parallel capacity

Report prepared elapsed processing seconds per second of received baseband for
one complete receiver workload. A ratio at least 1 fails real-time capacity;
0.1 represents a 90% processing-time margin. Never divide elapsed service by
core count, sum thread CPU counters as elapsed time, or substitute throughput
from parallel independent simulation trials. CPU counters diagnose utilization.

Detect visible affinity, quotas and physical-core topology. Compare a small
declared set of same-stream worker allocations that includes the complete
available logical-CPU budget and physical cores when known. The pilot measures
scaling; its winner does not replace the full-logical default. Keep one aggregate
concurrency budget across numerical libraries and outer workers. Measure the
actual dispatch, merge and native-template work; do not assume that requesting
more library threads uses the CPU productively. Report remaining serial,
memory or scheduling limits. Workspace must include simultaneous worker state,
shared buffers, native services and transient scratch, with explicit uncertainty.

## Efficient measurement

Predeclare path strata, candidate/state limits, measurement budget, uncertainty
decision and stopping rule. Begin with cheap complete-path correctness checks.
Use brief actual receiver work and fixed-geometry extrapolation with a holdout;
account for every stage and its full multiplicity. Test cold, steady and terminal
work where their costs differ. Unmeasured stages, changing state regimes or a
failed holdout leave the complete estimate unresolved. A short timing probe is
not an independent success-probability trial or proof of full-message reception.

The manual harness shares the regression's 1/2/4-unit holdouts and 3/5/9
sequential timing looks. Its default nominal measurement requires actual complete
public acquisition, then keeps one fine session alive through a fixed two-second
prefix and cumulative one-, two- and four-second continuation. Charge prefix
setup and finish once per public slot, and the measured continuation rate for
the remaining window. Each outer observation resets the native cache once;
continuation must demonstrate new native cache fills, nonzero template work and
stable state geometry. Material state/cache changes or a failed cost holdout
prevent a supported extrapolation. The maximum is nine six-second observations
per row, not a full long transmission. Partial acquisition cannot supply
work-only coordinates to this nominal measurement.

The continuation sampler stops for supported relative precision, the bounded
look limit, or a failed work or holdout check. Apply the real-time decision
only after normalizing the prefix, continuation and finish by slot cadence and
adding the other required service. A review found that the historical v3 run
stopped four rows using the raw continuation rate. Those intervals do not
demonstrate correct stopping for final service. See the
[validation record](validation.md#complete-headless-robust-and-manual-capacity).

The separate conservative fine-state probe starts 256 states in
each lane, executes two half-second segments and the real intervening posterior
transition/phase transport/merge, and charges that entire unit once per actual
half-second segment. Both native and Python template caches are cold for each
unit. This deliberately repeated startup and emission work may withhold a pass;
it does not by itself prove ordinary reception is slower than real time. The
ordinary active, terminal and noise-listening strata are measured separately.
Broad public searches use brief exact frequency tiles plus measured full-count
candidate selection; partial tiles never produce reception decisions.

Recurring public ordinal banks include their fidelity checks and native source
preparation. Workspace accounting bounds simultaneous banks, projector/control
temporaries, worker scratch, input copies and native services before allocation,
and supplements those estimates with sampled parent/child RSS. Memory estimates
and fixed geometry allowances are not allocator or universal statistical guarantees.

Reread and validate the entire manual file on every invocation. Record exact
source, native binary, current list, configuration, machine and allocation
provenance. Partial row selection must remain explicit. Retain only evidence
supporting a current validation claim in ignored `local-evidence/`, and remove
superseded probes after their useful findings are recorded.

### Fast local CPU capacity measurement

The canonical CPU calibration measures service on this computer. The default
pilot compares serial, physical-core and full logical-thread allocations and
then measures fresh complete inference at all 12 visible logical CPUs on this
host.

```sh
./build.sh test triad
mkdir -p local-evidence
python3 tools/triad_manual_cases.py \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-cases.json \
  --workspace-mib 4608
```

The 4608 MiB cap remains subject to the half-visible-memory guard. If a row
fails workspace admission, retain that failure and run a fresh matching
`--case-index N` continuation with the same source, cap and inputs; only an
identity-matched replacement may fill that row in a clearly labeled coverage
assembly.

On the Ryzen 5 PRO 5650U used for this measurement, the declared pilot points
are 1, 6 physical cores and 12 logical threads. The report must show which
allocation the pilot selected and separately show the fresh 12-worker
inference; merely requesting 12 workers is not proof of productive parallel
execution. For identical single-stream work, compare elapsed service, summed
process/thread CPU time (effective busy cores), output/work counters and
workspace. The continuation and separate worst-state bounds remain required.

Estimate complete receiver service only after charging native acquisition,
coarse search, recurring public banks, fine likelihood and tracking, failed
candidates, terminal/noise and erasure work, quiet-slot completion, preparation,
merging and other orchestration at their measured multiplicities. Use the
largest public recurring receive window for stage work where it exceeds the
initial window, and include simultaneous receiver banks and worker scratch in
the workspace ceiling. A longer unmeasured window, unmatched state regime,
failed holdout or missing stage leaves the complete estimate unresolved.
Report each row's estimate, uncertainty, margin to 1 s/s and any point failure.
An estimate at or above 1 s/s is a point failure even if uncertainty is wide.
Do not divide by the worker count.

Before the final suite, finish implementation and diagnostics, freeze the
receiver, native support library, measurement code, source list and numerical
runtime. Partial reruns cannot stand alone as full-list coverage. A derived
coverage assembly is valid only when it binds identical source, case-list and
native-binary identities, records every original admission failure, and retains
the matched constituent reports; label it assembled, not one uninterrupted
run. Record the exact hashes and resolved CPU affinity, quota, topology,
Python/numerical runtime and worker allocation. Reuse the applicable local
phase costs for the GPU forecast. A change to the CPU work or runtime requires
fresh affected local calibration.

### GPU-assisted forecast and local fallback

Use the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
when the current CPU calibration covers the required work classes. The forecast
combines GPU-observed work with local phase timings, then checks fresh bounded
local measurements and reports nominal and conservative uncertainty separately.
Keep the paired GPU reference, CPU calibration and holdout tied to their exact
applicable artifacts. A missing cost class gets a bounded local calibration.
In the same-geometry encrypted-to-clear transfer, the noise-only fine class can
be measured independently with `--calibration-stage noise-only-fine`.

The [current validation record](validation.md#gpu-assisted-receiver-measurement)
separates accepted full-list timing forecasts from capacity decisions. The
current eight-row forecast passed its nominal and conservative timing gates;
all nominal intervals are below 1 s/s, but conservative capacity remains
unresolved, with conservative point failures in rows 4, 6 and 8. Earlier
precision/work failures and the rejected encrypted-to-clear transfer retain
their own source and policy identities. A successful forecasting gate does
not erase these distinctions or establish an unconditional capacity pass.

Use authenticated SSH/SCP for source and native artifacts. Reuse persistent
storage when cache savings justify its cost, then terminate disposable
instances after measurements. Keep only current validation evidence and
applicable reference or calibration reports.

## Separate sensitivity checks

The bounded [pilot](../tests/triad_manual_sensitivity.py) consumes a freshly
completed, unchanged manual-capacity report and chooses an affordable current
configuration. It tries an independent bit-0/bit-1 pair at the matched model
target, and at most one pair 6 dB higher after ordinary reception failure.
Correct reception includes both scored quiet slots and complete work receipts.
A single-bit dictionary rejection does not invalidate the physical bit endpoint;
these are not whole-text-message trials.

```sh
python tests/triad_manual_sensitivity.py \
  --capacity-report local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-sensitivity-pilot.json
```

If the original model-point pair succeeds, the separate
[confirmation](../tests/triad_manual_confirmation.py) predeclares one point
3 dB higher and 29 fresh independent AND pairs. It admits bounded waves under
local CPU, memory and elapsed-cost limits, and stops admitting work after a
failed pair or resource/identity gate. Every launched outcome is retained.
All 29 pairs must succeed before the exact one-sided 95% lower bound exceeds
90%. An incomplete prefix has no such qualification, and no result qualifies
literal `fox`, other cases or a minimum SNR threshold. This is a conditional
synthetic-channel operating point. See [exact binomial interval construction](https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm).

```sh
python tests/triad_manual_confirmation.py \
  --capacity-report local-evidence/robust-cases.json \
  --pilot-report local-evidence/robust-sensitivity-pilot.json \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-sensitivity-confirmation.json
```

Parallel independent receives are permitted here solely for sensitivity.
Their timing must never supply same-stream CPU-capacity claims. Use sampled
post-exec native RSS with workspace/runtime allowances: a reaped child's
high-water counter is not simultaneous native-process residency. Linux reports
the largest child's peak and preserves usage across exec; see
[`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html). The current
incomplete confirmation and its repaired monitor are recorded in
[validation](validation.md); do not silently restart its budget or relabel its
five successful pairs as a completed confirmation.

## Physical completion and security

Robust and Fast use observed quiet intervals for physical message completion.
Neither may use a received packet-length field or arbitrary-length header to
choose processing geometry, allocation sizes or the physical endpoint. The
receiver's slot geometry, buffers, candidate budget and source quota are local
bounded configuration. Input EOF, cancellation, quota exhaustion and application
end flags do not establish physical completion.

A quiet decision must come from fully executed received-sample processing.
Incomplete, erased, ambiguous or work-limited searches cannot be counted as
quiet. Hold a possible trailing quiet interval pending the declared end rule;
if DATA resumes, retain the missing interior interval as an erasure and perform
the required reacquisition. Do not fabricate its bit or silently shorten the
message. Decode and present source content only after physical completion and
the applicable source-validity checks. Keep false-end assumptions and end
latency explicit: a sufficiently deep fade can resemble physical absence.

Quiet energy uses only complete orthogonal projection blocks inside the native
public aperture: the current DATA support intersected with the search window
and the latest predecessor/earliest successor signal bounds. These include
public clock, arrival, delay and filter-tail envelopes, never realized channel
truth or a received length. All original input remains in the fine likelihood;
an absent or empty quiet aperture cannot establish silence.

Fast already enforces this boundary in its
[fixed-interval and physical-completion contract](fast-mode.md#fixed-intervals-without-received-lengths).
Its application metadata is interpreted after physical completion and is not a
modem packet-length mechanism. Preserve the
[shared received-content restrictions](security.md#received-content-presentation).

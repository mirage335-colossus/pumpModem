# Robust manual-case receiver capacity

The manually edited `_tests-RobustModem.md` asks **what this computer's complete
receiver can sustain in real time**, using efficient algorithms and all available
hardware threads. The current entry point invokes the same compiled C++ stack as
the GUI through `triad_receiver_probe`. Python performs orchestration and
statistical analysis only. Old Python/process-runtime calibration and transport
applicability exceptions are inadmissible as native calibration.

Absolute capacity, relative computational regression, communication sensitivity
and GPU phase-forecast accuracy are separate decisions. A pass in one does not
establish another. All receiver evidence is scoped to configured public timing
and keyed ordinals; unknown waveform epoch, ordinal and timing acquisition remains
unqualified. Reception continuously decides bits or nulls, and two scored quiet
slots confirm physical completion. A semantic transmitter-identity classifier
is not required. See [current architecture](robust-current.md) and
[current validation](validation.md#native-codec-and-measurement-migration).

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
   State that current calibration assumes a configured public calendar; keep
   unconfigured epoch/ordinal/timing acquisition separately unresolved.
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
4. **Demonstrate productive use of the available CPU.** Preserve access to all
   available hardware threads under the declared software-worker allocation,
   one numerical-library thread per outer worker, and measured
   same-stream useful work. Check elapsed throughput, busy cores and work counts
   together. Investigate low utilization in the dominant stage; retain the full
   budget despite thermal/power limits or minor local timing differences. Fewer
   useful tasks in one stage do not justify a global cap. Follow the
   [allocation contract](development.md#receiver-cpu-allocation).
   For continuous listening, test useful work while a single captured stream
   has ready input or backlog, including mostly-noise operation. Idle time after
   catching up is expected. Buffering can trade latency for larger ready batches;
   it cannot repair a sustained service rate slower than incoming samples.
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
   Preserve the configured-calendar qualification and unresolved additional
   acquisition coverage independently of numerical points and forecasting gates.

## Local calibration entry point

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --probe build/dev/triad_receiver_probe \
  --output local-evidence/robust-cases.json
```

`triad_abbreviated.py --cases` forwards to this absolute-capacity implementation.
The legacy `--exporter build/dev/triad_helper_export` spelling resolves its sibling
native probe; it does not select a Python receiver. Use `--probe` for new commands.
The `datapump.triad.native-capacity.v1` JSON retains native plans, all raw phase
receipts, source/binary/runtime identity, allocation, memory, work, uncertainty
and margins. A neighboring Markdown report summarizes nominal, conservative and
actual same-stream service with unresolved work.

Every invocation rereads the entire manual list, validates all sixteen required
fields and preserves order and duplicates. `--case-list PATH` selects another
complete list. Repeatable `--case-index N` selects explicitly partial work and
still validates the whole list. Identical normalized rows are measured separately
unless `--reuse-identical` is requested; reused rows are marked. Partial output
cannot qualify omitted rows.

Default inference uses `W=2*H` after a bounded serial, known physical-core and full
logical allocation pilot. `--threads W`, `--slot-owners O` and `--workspace-mib M`
are exact limits. `--full-logical-inference` explicitly selects `H` for a diagnostic;
`--no-full-logical-inference` explicitly selects the pilot winner. A pilot winner,
thermal limit or minor timing difference cannot reduce the automatic default.
`--sustained-allocation-probe` adds bounded continued fine comparisons at physical
and logical allocations; it does not select the default or prove whole-receiver
capacity. See [CPU allocation](development.md#receiver-cpu-allocation).

Declare `--deadline-ms` (per native call), `--row-deadline-ms`, `--max-samples` and
`--coarse-tiles` before a run. Defaults are 30,000 ms per call, 180,000 ms per row,
4,194,304 samples and 4,096 coarse tiles. A bound that cannot cover the public
work leaves that row unresolved. Do not reduce geometry or treat a partial grid
as reception. `--skip-message-gate` is diagnostic only and prevents an integrated
suite pass. `--target-cn0` declares the public duration-model target (default
32 dB-Hz); `--seconds` explicitly selects a separately declared DATA geometry.
Neither an override nor an abbreviated diagnostic qualifies an unmeasured
modeled-duration case. `--backlog-slots` selects an exact horizon; otherwise the
runner chooses the smallest exact native preflight that covers startup carryover
within declared bounds when possible. `--max-spool-bytes` caps temporary capture
storage. `--stream-looks` selects 1, 3, 5 or 9 actual replays (default 1). One
replay is a bounded point and does not invent a confidence interval.
`--calibration-stage noise-only-fine` is a selected-stage measurement,
not a complete-capacity result.

Capacity mode exits 0 only for its conditional pass, 1 for measured capacity
failure or input/error failure, and 2 for unresolved or invalidated evidence.
Stage and sensitivity modes have their own success statuses; successful tool
execution is not a complete-capacity claim. Reports must retain configured-calendar
scope, every missing stage, numerical point failures and uncertainty.

## Complete receiver work

A complete capacity estimate must account for received-baseband filtering,
Helper processing, full public coarse search, fine synchronization/refinement,
continuous tracking, bit/null decisions, quiet-slot completion, bounded source
decoding and result presentation. Include unsuccessful candidates, noise-only
listening, interior loss and reacquisition. Candidate-dependent setup, template
cache fills, key/ordinal changes, dispatch, synchronization and merging are
receiver work. Only reusable preparation before reception and simulator
generation may be excluded from sustained service; report readiness separately.

The continuous listener keeps public slot/sample/keyed ordinals monotonic across
messages; a codec reset does not reset native waveform identity. Its bounded
output queue, backpressure and scored-quiet recovery are part of the receive
lifecycle. Finite callers explicitly select finite native reception; a continuous gate
that continues through later quiet windows still proves only the configured
calendar. A broader acquisition claim must include additional waveform
epoch/ordinal/timing hypotheses, unsuccessful candidates and incorrect-address
behavior. It does not require a separate semantic new-transmitter classifier or
imply that native ordinals must restart at a message boundary. Do not extrapolate
per-slot cost into that claim without measuring the additional work. Keep these
coverage limits explicit in the [current architecture](robust-current.md).

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

The native `CompleteStreamReceiver` shares immutable input buffers and admits
independent in-process scorer owners for ready public windows. Resolve the total
`2*H` worker budget once; slot roots and their Helper, coarse and fine tasks
share a bounded executor. Per-owner lane/scratch caps bound DSP storage, not exclusive worker
permits. Automatic owner count can grow to that budget, subject to useful work
and full memory admission. Admission quotes the largest ready prefix at
full-`W` scratch per owner, preferring balanced scratch only when its complete
memory bound admits a strictly wider ready prefix. Memory beyond a sequential
lease for every owner the cap still admits then gives slots, oldest first,
the wider lease that overlaps Helper acquisition with coarse preparation. Each
shape remains fixed through its slot; full DSP and started idle pool stacks
remain charged. Ready leaves prioritize the oldest eligible public slot while
queued roots use unneeded lanes. Within a slot, serial steps
[overlap independent parallel work](development.md#overlap-within-a-slot) on
that owner's lanes. This does not change the aggregate `W` ceiling
or supply a throughput guarantee. Explicit limits
remain exact. There is no receiver subprocess,
child bootstrap or `memfd` operating transport. Full accounting, ordered commit,
bounded outputs and cancellation ownership follow the
[allocation contract](development.md#receiver-cpu-allocation).

Operating RAM is cgroup-aware available memory less
`min(2 GiB, max(512 MiB, 10% of available memory))` headroom. Windows automatic
availability is bounded by physical, commit and virtual availability. Unknown or
insufficient memory fails admission; swap does not enlarge the budget. Initialized
I/Q is charged once through the greater of process footprint and an explicit
ownership floor; outstanding DSP, stacks and bounded outputs remain reserved
separately. Whole-process RSS cannot prove a scorer's future workspace already
exists. Old three-owner, 3840 MiB or half-RAM settings are not portable defaults.

Measure productive work within one stream, including ready-input backlog and
mostly-noise operation. Compare identical observations at serial, physical and
logical allocations; distinguish software permits from the hardware-utilization
denominator. A higher logical busy fraction with unchanged elapsed service is
not a gain: on the measured six-core host the coarse search ran as fast on six
pinned cores as on twelve logical CPUs. Report elapsed service, busy cores, useful work, actual owners and
memory together. `peak_running_owners` distinguishes roots in scorer scope from
admitted owners; `peak_executor_workers` reports started pool threads, not busy
hardware cores. Roots executing their own leaves are not extra workers. Idle time after catching up is expected. Fewer useful tasks in
one stage must not become a global worker cap.

For overlapping windows, measure the outer receive-service interval through
ordered commit. Summed per-slot wall times double-count overlap; asynchronous
input submission omits background DSP. Within a slot, a Helper interval
reported as overlapped lies inside that slot's coarse interval. CPU counters diagnose utilization and do
not replace elapsed service. Never divide service by core count or use parallel
independent simulations as one receiver's throughput. Buffering can improve
batching and trade latency, but cannot repair service slower than incoming data.

Historical process-scheduler results remain in
[validation](validation.md#continuous-receiver-restructuring): the original 15%
throughput-gain gate failed, followed by the user's acceptance of a smaller useful
gain. The 81.09% and 69.37% backlog observations used different RAM limits; the
later exact gate failed on allocation-dependent dispatch counts and omitted its
post-run input hash. These are useful limitations, not new native capacity.

## Efficient measurement

Predeclare path strata, candidate/state limits, sample budget, uncertainty and
stopping rule. Start with cheap complete-path correctness checks. Fixed-geometry
extrapolation needs an independent cost holdout; unknown work, changing state
regimes and failed holdouts leave the complete estimate unresolved. No full,
sustained, multi-day, exhaustive or heavy campaign is a routine fallback.

The native sampler covers active, noise, cold-maximum, terminal and erasure
strata. Active/noise use public acquisition; cold-maximum, terminal and erasure
may use explicitly declared non-admitting fine-only diagnostics. Those diagnostics
do not qualify acquisition. Bounded coarse steps retain persistent state, but
partial search cannot provide an operational decision or a qualified full-grid
cost; a matched tile diagnostic remains separately labeled. A native step
searches its tiles as one span, so a one-tile step gives each lane a single
job and costs more per tile than a long step. Actual source
preparation, Helper, selection and real-audio projection are retained in the
receipts. Operational coordinates are not supplied by simulator truth.
The `slot` and `continuation` phases run Helper acquisition and then coarse
preparation, each on the full lane set, so their walls add. That is the
sequential lease, which every admitted slot can use. A slot with the wider
lease overlaps the two and is no slower. `regression` grants that lease when
local memory allows; `--helper-overlap off` scores the same slot in sequence.

For eligible long windows, nominal fine measurement keeps one `FineSession`
alive through a fixed two-second prefix and cumulative one-, two- and four-second
continuation. Its checkpoints are at 2/3/4/6 seconds. Native cache fills, positive
template work, observation identity and state geometry must remain applicable.
Charge prefix and finish once per slot, and continuation for the remaining
window. Three, five and nine outer looks bound the sample budget; material
model/work failure stops the affected inference. A short ineligible window is
measured directly rather than pretending it is a six-second continuation.

The separate cold-maximum probe starts 256 states per lane and executes two
half-second segments plus their actual transition. Its full cold work is charged
once per segment for the conservative bound. It is not a nominal warmed-session
estimate. Noise, terminal and explicit-erasure paths remain separate. The
recurring plan covers 24 public slots and uses the largest required window and
shortest cadence in that declared horizon; a different horizon or larger geometry
requires its own applicability check.

The suite replays the same bounded native capture spool for serial, physical
and logical allocation pilots and fresh default `2*H` inference. It measures
actual same-stream scheduler service, ready-input backlog separately from
startup/drain, full logical PCM projection through gaps, message/two-quiet/EOF
behavior and codec work. DSP timing is separate from evaluator message success.
Retain these receipts
separately from sums of phase models. Complete capacity requires all required
work, eligible intervals, workspace, current completion gates and the complete
ordered case list. Unknown-start acquisition remains separately unqualified.
A numeric point at or above 1 s/s is a failure even when uncertainty prevents a
stronger conclusion; 0.1 s/s represents 90% service-time margin.

### Fast local CPU capacity measurement

Freeze source, native binary, runtime, case list, arithmetic and resource limits
before final bounded measurements. Use the [entry point](#local-calibration-entry-point)
and inspect actual work coverage before interpreting a service ratio. Preserve
all current selected channel effects and the original received observations.
Record OS affinity/quota, physical topology, worker/owner allocation and available
memory. A fixed cap may become unavailable later; retain that admission failure.

Continue only unfinished rows in fresh matching `--case-index N` runs with the
same source, method, inputs, error-budget family, allocation and cap. Do not
remeasure favorable completed rows or lower limits to obtain a pass. A clearly
labeled coverage assembly must bind all constituent identities and retain every
original failure. A missing final identity flag remains missing; an external
posttermination check is a separate receipt.

The old all-eight one-pool estimates at H=12, W=24 and 9072 MiB are historical.
Nominal passes covered rows 2–8; conservative passes rows 3 and 5. Row 4's
conservative point was 4.11055 s/s. Active-fine, noise, terminal and maximum-state
model gates remained unresolved in other rows, and row 7 lacked its canonical
final-runtime flag. These limits and constituent provenance remain in
[validation](validation.md#complete-headless-robust-and-manual-capacity); none can
be substituted for current native calibration.

### GPU-assisted forecast and local fallback

Use the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
when applicable native CPU cost classes exist. Pair exact native CPU/CUDA
observations and arithmetic/work gates, then measure independent local holdouts.
The GPU supplies work demand, not local CPU elapsed service. Changed CPU work or
runtime requires fresh affected costs; unchanged evidence is reusable only with
explicit source/work applicability. Historical Python forecasts and transport
exceptions are rejected by the native schema.

Keep supported phase estimates distinct from the stronger configured logical-rate
complete-service decision. GUI presentation, hardware-rate resampling and unknown
calendar acquisition are outside that forecast and need their own evidence.
A missing GPU device is not a CPU fallback qualification; use direct applicable
native CPU measurements while GPU evidence is unavailable.

## Separate sensitivity checks

The native sensitivity mode runs independent complete-receiver `e` messages,
including both scored quiet slots, under explicit selected case/resource bounds:

```sh
python3 tools/triad_manual_cases.py \
  --probe build/dev/triad_receiver_probe --sensitivity --case-index N \
  --sensitivity-trials 3 \
  --output local-evidence/robust-sensitivity.json
```

`--sensitivity-cn0` declares an actual trial C/N0; omit it to use the model target.
Predeclare between 1 and 29 trials. Every launched outcome, seed and native work
receipt must remain in the report. These independent communication trials are
separate from repeated timing observations. A successful bounded run does not
establish a minimum threshold or population performance for other cases/messages.
The exact binomial interval reports its stated confidence; a >90% one-sided 95%
lower bound requires all 29 independent successes. A smaller or interrupted
prefix has no such qualification. The former Python sensitivity and confirmation
receivers are retired; their incomplete five-pair result remains historical in
[validation](validation.md), with its original failed monitor and scope.

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

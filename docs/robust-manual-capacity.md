# Robust manual-case receiver capacity

**Runtime applicability:** the operating receiver is now the native C++ stack
linked into the GUI, with shared buffers and in-process worker ownership.
The retained Python calibration commands and measured values below describe
their original reference/runtime revisions. Their process-owner, bootstrap and
`memfd` details are historical, not the native application's operating contract.
They do not qualify native GUI CPU capacity. New capacity claims require a
measurement route through the current complete native receiver, matched affected
local phase costs and the same full-work/uncertainty gates below. Functional
GUI validation is separate; see [current results](validation.md#native-gui-integration)
and [native ownership](development.md#receiver-cpu-allocation).

The manually edited `_tests-RobustModem.md` asks **what this computer's CPU can
sustain for the complete receiver in real time**, using practical efficient
algorithms and all available hardware threads. These are absolute capacity
checks. When applicable local calibration exists, start with the
[GPU-assisted forecast](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
The retained reference calibration tool is `tools/triad_manual_cases.py`. The former
`triad_abbreviated.py --cases` invocation forwards to the same implementation.
The ordinary `triad_abbreviated.py` performance regression asks whether measured
work degraded against a comparable baseline. A regression pass cannot establish
real-time capacity. Both tools use the same efficient, predeclared statistical
approach, with different decisions and explicitly recorded work coverage.

**The retained calibration covers complete configured-public-calendar
receiver work. Acquisition of unconfigured waveform epochs, ordinals and timing
is unresolved.** Every capacity report must retain this scope, even when its
configured-calendar nominal and conservative points are below 1 s/s. Those
numbers cannot qualify the additional acquisition work. The receiver's semantic
contract is continuous bit/null decisions with two scored quiet slots confirming
completion, not classification of a transmitter as new or existing.
See the [current scope and findings](validation.md#continuous-receiver-restructuring).

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

The following entry point refreshes the retained configured-calendar reference
calibration in the numerical Python environment used by the build. It does not
measure the new native GUI runtime; bind a native measurement route before
claiming application capacity.

```sh
./build.sh test triad
python tools/triad_manual_cases.py --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-manual-capacity-current.json
```

**These per-row service estimates use sequential windows at the automatic
`S = 2*H` software-worker allocation**, where `H` is hardware-thread
availability. This host uses twenty-four software workers over twelve hardware
threads. The native continuous functional gate is not a capacity
qualification of the automatic pipeline across the manual rows.
All-row parallel capacity remains unqualified; keep execution mode explicit.
The [current all-eight complete-work estimates](validation.md#complete-headless-robust-and-manual-capacity)
use one twenty-four-worker pool, twelve hardware threads and a fixed 9072 MiB cap.
They form an audited union of matching per-row receipts, not a replacement
canonical calibration report or automatic multiowner pipeline ceiling. They
predate the input-transport change. A reviewed applicability receipt and strict
forecast consumer preserve the unchanged one-pool DSP/finite costs without
changing raw timings or source identities. Only the exact completed canonical
reports for rows 1–6 and 8 are accepted; the audited union is not a calibration
input. Follow the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
Conservative capacity passes cover rows 3 and 5; nominal passes cover rows 2–8.
Row 4's conservative point estimate is 4.11055 processing seconds per baseband
second, a numeric failure. Row 1's active-fine uncertainty, rows 2 and 6's terminal
models, row 4's noise-only acquisition, row 7's noise-only fine model and row 8's
maximum-state/terminal models keep their respective conclusions unresolved.
Row 7 was fully serialized before timeout during row 8, but its raw report lacks
a canonical final-runtime flag. External posttermination source/native/input
identity and matching initial-runtime checks are recorded separately; they do
not manufacture the missing completion flag. Row 7 remains an audited manual
estimate but cannot serve as automatic forecast calibration without a completed
canonical calibration report. This is not all-eight GPU qualification.

The default compares serial, physical-core and full visible logical allocations,
then uses `S` for fresh inference when allocation flags are omitted. Explicit
`--full-logical-inference` selects `H` for a diagnostic; `--threads W` preserves
exactly `W`, including intentional software overcommit. It performs the
current-source native message/quiet/EOF gate before measuring the current rows.
The configured-calendar continuous gate also processes a sixth quiet slot after
the completed message; it does not test unconfigured waveform-address acquisition.
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

The continuous listener keeps public slot/sample/keyed ordinals monotonic across
messages; a codec reset does not reset native waveform identity. Its bounded
output queue, backpressure and scored-quiet recovery are part of the receive
lifecycle. Finite callers explicitly use `continuous=False`; a continuous gate
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

The measured algorithms and scheduler observations in this subsection belong
to the historical Python-orchestrated runtime. Their numerical results and
failed gates remain unchanged. Current native scheduling follows the shared
in-process [allocation contract](development.md#receiver-cpu-allocation) and
requires its own bounded same-stream measurements.

The CPU implementation prepares immutable candidate plans and fuses native
rendering, projection and sufficient statistics in the shared worker pool.
When both valid blocks of a segment are available, candidate tiles process
them in one wave. Supported one-/two-tap CPU FP64 state updates use native
candidate partitions in that same pool, reuse Gram validation across models,
and advance both observations in order before the existing timing/pruning
boundary. Exact bit-pair geometry is shared without merging their independent
signal hypotheses. Generic callbacks, partial erasures, device paths and
unsupported staging retain their ordinary bounded processing.
Mapped preparation reuses a bounded descriptor bank's extended-precision affine
terms across the two blocks, preserving numerical order and pin lifetimes.
Charge its additional shared staging reservation in the workspace budget.
Recurring integer-source banks reuse each native epoch/bit lane across the
clock/ray projections and apply a shared native cubic plan in parallel.
Coarse workspace partitions share immutable observation spectra; supported
two-ray batches combine quadratic scoring and bounded peak selection in native
code, retaining canonical selected scores and fallback for ambiguous comparisons.
These are changes to one received stream, not simultaneous simulation trials.

Every automatic Robust receiver mode uses `S = 2*H` software permits, where `H`
is hardware-thread availability. Serial, finite and custom modes use the same
rule; explicit worker count `W` remains exact. Canonical continuous reception
has an automatic controller cap of `min(S, W)`; explicit owner counts may be
one through `W`. Defaults admit up to `2*H` independent controllers sharing `2*H`
permits. Owners grow lazily for ready complete windows with admitted full public
DSP bounds, at least one worker per job and exact per-job memory leases; do not
spawn owners for empty work. Leases must leave room for later ingress. Completed
out-of-order results free owners while at most twice the resolved owner cap in
uncommitted records (`4*H` by default) await physical-order commit. Job-free idle owners can
retire before admitting a larger geometry. Fewer owners and late fallback retain
`W`; reject inadmissible work rather than clamping it to hardware count.
Resolve automatic permits once and pass explicit shares downstream. Record
hardware count, software permits and actual owners separately. After the first
fully loaded child handshake, prospective owner overhead uses its maximum
pre-DSP bootstrap RSS/HWM, not the parent's captured-IQ heap. Parent, live-owner
and native workspace memory remain charged and are rechecked at admission.
Supported Linux pipelines transport sealed, read-only `memfd` input. Charge the
single parent assembly transient before releasing its private array, retain the
descriptor/metadata for retry, and close child mappings before acknowledgement.
Only duplicate input-copy reserves are removed: full DSP/native workspace,
raw-ring and result leases remain. Unsupported copied transport keeps its full
charges; capability probing is lazy and excludes finite/custom/serial paths.
Full-size input/lifetime checks admitted three row-1 inputs at eight workers each
instead of the copied path's one, retaining full DSP reservations. This did not
execute row-1 DSP or measure its throughput. The completed native copied/shared
comparison used three owners at eight workers each and preserved checked outputs,
two-quiet completion, continued listening, EOF rejection and cleanup. The original
private-harness field error remains recorded; only the missing shared-input leg
was continued, without production changes or repeating the completed control.
These checks do not establish an elapsed gain or an all-row shared-transport
admission envelope.

The three-owner comparison missed its original 15% elapsed-gain threshold. The
user then chose the modest gain and increased productive utilization as useful
for this and less power-limited CPUs. Preserve the failed old gate and the new
adoption rationale separately. Three-by-six is historical evidence, not a fixed
controller endpoint. The final-source 49-window same-stream run with automatic
allocation and sealed input averaged **8.325 busy threads of 12 (69.37%)** over
51.898 seconds of predeclared backlog intervals. Its longest uninterrupted
interval was 32.899 seconds at 7.921 busy threads; owner startup totaled 21.238
seconds separately. The capture contained one DATA bit and 48 quiet windows;
its one-bit codec frame was invalid. The original exact comparison gate **failed**
on dispatched tracking-task counts in 17 slots; a separate post-hoc comparison excluding that
allocation-dependent count matched the remaining compared work, scores and
decisions. All 49 windows completed without fallback or retry. The full input
digest matched before ingress, but the predeclared post-run input hash was not
performed. This is a scoped utilization observation, not an all-row pipeline
capacity pass. The automatic
RAM cap was 8.748 GB versus the earlier fixed 9.513 GB; the observations do
not isolate a transport speedup. [Final-source receipt](../local-evidence/final-backlog-cpu/result.json). Before the `memfd`
transport change, the enlarged automatic
owner cap passed an independently audited 49-window continuous-capture check,
averaging 81.09% of twelve hardware threads over its predeclared sustained-backlog
interval. This is historical pre-transport evidence. Earlier
hardware-count-capped measurements retain their scope. This result is neither
full saturation nor a paired speedup or all-row pipeline capacity result. The
pre-`memfd` admission bootstrap supplies conditional owner widths for its declared
ring ranges; do not apply those widths to changed transport or arbitrary backlog,
geometry or RAM.
Higher busy-core counts do not themselves establish capacity or omitted work.
Runtime workspace rejection releases the owners before a
full-budget serial fallback; failed work remains part of service. This is
different from timing independent simulations. Earlier production evidence
covers the tested two-owner row-7 capture and
geometry; it does not qualify all eight rows or demonstrate full thread
saturation. The eight-row sequential calibration is a separate mode, not
qualification of the pipeline. Automatic serial fallback is also distinct from
explicit `slot_processes=1`: it retains the shared multiprocessing resource
tracker reservation of at least 32 MiB after closing slot owners. Do not inherit
explicit-serial workspace or capacity qualification for that fallback; consult
[current validation](validation.md#continuous-receiver-restructuring).

Robust's latency allowance permits bounded lookahead and batching when work is
independent. Processing buffered IQ later does not add oscillator drift to
already captured samples: receiver phase/clock hypotheses use physical sample
coordinates. Preserve those coordinates and each state's chronology; larger
computational batches do not authorize longer coherent integration. Reuse
invariant preparation, and overlap next-batch preparation only when its inputs
are ready, ownership is safe and the complete measured service improves.
Count prepared buffers and speculative work within the shared resource budget.

Record actual dispatch and cache accesses separately from logical search work.
`row_tile_references` counts template dependencies per rendered row; physical
`cache_hits` can fall when plans share a lookup. Forecasts compare the former
alongside states, decisions, cache fills and geometry. Never manufacture old
cache-hit counts to make a more efficient implementation look unchanged.
Less CPU time for equivalent work is a benefit even when average busy cores
falls. Retain the full worker budget and investigate the remaining critical
path; neither a pool size nor an isolated parallel kernel proves whole-receiver
saturation. See the [current restructuring evidence](validation.md#continuous-receiver-restructuring).

Report prepared elapsed processing seconds per second of received baseband for
one complete receiver workload. A ratio at least 1 fails real-time capacity;
0.1 represents a 90% processing-time margin. Never divide elapsed service by
core count, sum thread CPU counters as elapsed time, or substitute throughput
from parallel independent simulation trials. CPU counters diagnose utilization.
For overlapping windows, measure the outer receive-service interval, including
recurring bank work, IPC and ordered result commit. Summed slot wall times
double-count overlap, while timing only asynchronous `push()` returns omits
background DSP. Include input/backlog, owner/cache and IPC residency together.

Detect visible affinity, quotas and physical-core topology. Compare a small
declared set of same-stream worker allocations that includes the complete
available logical-CPU budget and physical cores when known. The pilot measures
scaling; its winner does not replace the automatic `S` default. Keep one aggregate
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
then measures fresh complete inference with `S = 2*H` software workers:
twenty-four on this twelve-hardware-thread host.

```sh
./build.sh test triad
mkdir -p local-evidence
python3 tools/triad_manual_cases.py \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-cases.json
```

With no workspace override, operating RAM is cgroup-aware available memory minus
`min(2 GiB, max(512 MiB, 10% of available memory))`. There is no 50% or 3840 MiB
default ceiling. Explicit workspace caps remain exact. A late row can fail a
fixed-cap admission check when current free memory has fallen, even though the
source and cap are unchanged. Preserve that rejection and any timeout. Continue
only unfinished rows in a fresh matching `--case-index N` process, keeping the
source, method, inputs, error-budget family, worker allocation and cap unchanged.
Do not remeasure completed favorable rows or silently lower workers or workspace
to make a run pass. Only identity-matched residual receipts may fill missing rows
in a clearly labeled coverage assembly; retain all original failures and the
constituent reports. A missing canonical final-runtime flag must remain missing;
record any external posttermination identity check separately.

On the Ryzen 5 PRO 5650U used for this measurement, the declared pilot points
are 1, 6 physical cores and 12 logical threads. The report must show which
allocation the pilot selected and separately show the fresh twenty-four-worker
inference. Explicit `--full-logical-inference` selects twelve for a diagnostic;
`--threads W` preserves exactly `W`. Merely requesting workers is not proof of
productive parallel execution. For identical single-stream work, compare elapsed service, summed
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
This speeds up forecasting of the implemented configured-calendar phases; it
does not supply unimplemented waveform-address acquisition or its CPU costs.
Keep the paired GPU reference, CPU calibration and holdout tied to their exact
applicable artifacts. A missing cost class gets a bounded local calibration.
In the same-geometry encrypted-to-clear transfer, the noise-only fine class can
be measured independently with `--calibration-stage noise-only-fine`.

The [validation record](validation.md#gpu-assisted-receiver-measurement)
separates accepted full-list timing forecasts from capacity decisions. The
pre-restructuring eight-row forecast passed its nominal and conservative timing gates;
all nominal intervals are below 1 s/s, but conservative capacity remains
unresolved, with conservative point failures in rows 4, 6 and 8. Earlier
precision/work failures and the rejected encrypted-to-clear transfer retain
their own source and policy identities. A successful forecasting gate does
not erase these distinctions or establish an unconditional capacity pass.
That earlier trace is not an applicable forecast for the restructured source.
Use applicable direct CPU costs while fresh matching GPU evidence is unavailable.
The [current all-eight CPU estimates](validation.md#complete-headless-robust-and-manual-capacity)
include fresh affected costs after the mapped-preparation change, for one
24-worker pool at 9072 MiB. Their audited summary is not a replacement canonical
calibration report: retain matching constituent reports and their provenance
limits when reusing phase costs. Earlier CPU/GPU receipts retain their own
identities and do not qualify this source or automatic parallel-mode service.
Fresh matching GPU evidence remains required. Never silently substitute old
evidence or a generic GPU/CPU speed ratio.

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

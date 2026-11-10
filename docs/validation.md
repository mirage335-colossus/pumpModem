# Validation status

Updated 9 November 2026. The historical cleanup checkpoint below was then
uncommitted and based on `357a50970a0c6214eeaade7c5704bbc39abd4b79`, now
committed as `419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. It is not release
certification. The [source and binary inventory](../local-evidence/robust-cleanup/source-binaries.json)
records 2,904 surviving implementation, test and build files and selected binary
hashes. This compact manifest substantiates this cleanup checkpoint; retire it
when the checkpoint is superseded. Its source-inventory SHA-256 is
`2405f259c050b68d7bbabad9628eb97ff750e53c6d9f51012f47530719f7c74b`.
This is a post-validation inventory, excluding documentation/evidence, not a
before/after timing comparison. Raw run logs are temporary; remove them after
this checkpoint is accepted unless a specific active validation or release
requirement still needs them. The concise outcomes and limitations below are
the maintained record.

## Sustained CPU hardware scaling

The Ryzen 5 PRO 5650U in the AC-powered ThinkPad L15 Gen 2a ran checked,
pinned AVX2 FP64 FMA work at 1, 2, 6 and 12 workers. Up to six workers occupied
distinct physical cores. Twelve used both siblings on all six cores. Each
primary run lasted 180 seconds and the table uses its final 120 seconds.

| Workers | Sustained GFLOP/s | Approximate active clock (GHz) |
| ---: | ---: | ---: |
| 1 | 51.641 | 4.04 |
| 2 | 95.691 | 3.74 |
| 6 | 214.277 | 2.81 |
| 12 | 237.934 | 2.55 |

Twelve workers delivered 11.04% more checked compute throughput than six and
2.49× the two-worker result. In the 90-second reverse-order revisit, the last
60 seconds measured 235.612 versus 216.224 GFLOP/s, an 8.97% gain. Twelve
workers averaged 11.45 busy logical CPUs in the primary run. These gains
describe register-resident FMA work. Same-stream receiver timings establish
how much of that parallel capacity the DSP can use.

**Allocation policy:** these measurements must not be used to reduce the
receiver's default worker budget. Keep all available logical hardware threads
available despite power/thermal limits or temporary/minor timing effects on
this host. Other processors and changing workloads can benefit from the
additional capacity. Low utilization calls for bottleneck analysis, not a
smaller global default; see the [CPU allocation contract](development.md#receiver-cpu-allocation).

The clock estimates use pinned-worker APERF/MPERF deltas. Boost was enabled
under `amd-pstate-epp` with the balanced profile and `balance_performance`
preference. AMD specifies 6 cores, 12 threads, 2.3 GHz base and boost up to
4.2 GHz. Linux documents that active frequency responds to workload and
operating conditions. [AMD specifications](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-5000-series/amd-ryzen-5-pro-5650u.html) · [Linux amd-pstate documentation](https://docs.kernel.org/admin-guide/pm/amd-pstate.html)

Although package-energy counters were unreadable, a separate 180-second run
with the exact same executable exposed readable AMD firmware power telemetry.
It sustained 240.037 GFLOP/s, 2.560 GHz and 11.51 busy CPUs. All 24 steady
five-second samples reported the sustained power limiter (SPL), with no
thermal limiter flags; median socket consumption was 11 W and Tctl was
58.75 °C. Across the eight receiver holdouts, 76 of 77 samples also reported
SPL, sometimes with current or fast package-power limiting. A low-load
control reported 1 W and no limiter flags. This directly establishes active
power limiting on the measured balanced profile. The configured ceiling is
unknown: sampled 11 W consumption is neither that ceiling nor AMD's specified
15 W default TDP. These observations do not supply a clock or GFLOP conversion
factor for receiver costs. The receipt preserves raw v2.2 metrics, the driver
mapping, hwmon cross-checks, timing windows and the unchanged original scaling
records. [Linux AMD telemetry](https://docs.kernel.org/gpu/amdgpu/thermal.html#gpu-metrics) · [Renoir/Cezanne firmware mapping](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/pm/swsmu/smu12/renoir_ppt.c)

Separate 20-second streaming-memory observations gave 9.362, 10.882, 9.312
and 9.012 useful GB/s at 1, 2, 6 and 12 workers. The 12-worker run averaged
8.80 busy logical CPUs while other desktop activity was present. These brief
observations do not establish an intrinsic SMT bandwidth penalty. Keep the
[scaling JSON](../local-evidence/cpu-scaling-current.json) while it supports
the worker-allocation decision and its telemetry limits. Remove the raw
report when a matched scaling check supersedes that evidence.

## Continuous receiver restructuring

**Scope: complete processing on a configured public calendar.** The retained
continuous listener preserves the configured keyed ordinal sequence. It does
not discover independently restarted transmitters. Its CPU estimates therefore
cannot establish ordinary blind continuous-listening capacity, even when the
configured-path numerical estimate passes. The full hardware-thread default
and the following qualified optimizations remain applicable.

The current development changes preserve the full logical CPU budget and
remove repeated work from the dominant noise search and fine state updates.
The comparisons below use the same captured input, receiver work and allocation
with the optimized callback enabled or disabled. They are scoped development
comparisons; their exact source/native identities are in each receipt. They
must not be multiplied together or treated as complete receiver capacity.

The coarse CPU path shares immutable observation spectra between workspace
partitions. A strict-FP64 native rank-two kernel combines quadratic scores and
bounded peak selection inside the existing worker pool. Conservative arithmetic
ambiguity guards retain canonical peak selection for near ties, and retained
scores are recomputed with the canonical arithmetic before global selection.
Unsupported ranks, guarded Grams and larger retained-peak counts keep the
general path. The final twelve-worker row-4 noise comparison reduced median
elapsed time from **0.451572 to 0.295567 seconds (1.528× throughput)**. Busy-core
equivalents increased from **6.66 to 8.12**. A disjoint frequency holdout reduced
0.478227 to 0.326784 seconds (1.463×). All 1,248 raw peak identities, ordering
and published scores per look matched bitwise. The entire bounded qualifier
took 5.124 seconds; it did not execute the full broad frequency grid.
[Guarded coarse comparison](../local-evidence/continuous-coarse/guarded-native-discriminator.json)

Complete mapped CPU FP64 one-/two-tap pairs now use native candidate-parallel
Gaussian updates on that same pool. Gram validation is shared across coherence
models; each candidate advances the two blocks in chronological order using
PSD square-root factors and Cholesky solves. The controller publishes results
only after every tile joins. Generic, erasure and device paths retain the
general implementation. At twelve workers, row 7's continued fine time fell
from **403.174 to 334.730 ms (1.204×)** and its model phase from 97.034 to
19.366 ms. Fine busy cores increased from 3.174 to 3.262, with 14.65% less CPU
time. Row 7 at six workers improved 1.331×. Row 4 at twelve improved 1.095×,
below the declared 10% secondary gain target; that outcome is retained.
Logical work, state order and threshold outcomes matched under the declared
posterior tolerance. These four-second continued sessions are not successful
message trials or whole-receiver saturation measurements.
[Row 7 / twelve](../local-evidence/structural-fine/row-7-native-gaussian-integrated-abba.json) ·
[Row 7 / six](../local-evidence/structural-fine/row-7-6-native-gaussian-integrated-abba.json) ·
[Row 4 / twelve](../local-evidence/structural-fine/row-4-12-native-gaussian-integrated-abba.json)

The post-native row-7 profile averaged 340.674 ms without instrumentation and
346.774 ms with it (1.79% higher). Candidate waves occupied 160.470 ms and
averaged 5.50 busy-core equivalents within those waves. Controller preparation
occupied 55.141 ms, including 14.592 ms of native-cache requests/waits; adaptive
transitions occupied 37.478 ms. Native model updates now occupy only 20.470 ms.
These spans are nested where stated; callable overlap is not an OS scheduling
trace. Native rendering and basis FFT dominate worker CPU time. The profile
retained exact work, ordered states, posterior bytes, scores and endpoints.
[Post-native profile](../local-evidence/structural-fine/row-7-post-native-residual-attribution.json)

A subsequent bounded scheduling discriminator processed two adjacent quiet
windows from **one continuous native capture** with separate persistent Python
controllers and native source owners. Both arrangements shared the same
twelve-thread budget. Two concurrent six-worker jobs reduced whole-slot bank,
DSP, IPC and ordered quiet-state service from **8.913410 to 6.481362 seconds
(27.29% less elapsed; 1.375× throughput)** compared with sequential twelve-worker
jobs. Busy-core equivalents increased from **2.94 to 4.45**. All logical work,
coverage and ordered decisions matched; the largest numerical difference was
4.44e-16. This experiment excludes production ingress and pruning, so it does
not yet qualify the integrated listener or other manual rows.
[Two-controller discriminator](../local-evidence/continuous-process/process-discriminator.json)

One predeclared width extension compared four three-worker jobs with two waves
of two six-worker jobs on four adjacent windows. Four owners reduced elapsed
time by **14.39%**, below the fixed **15% incremental adoption gate**, while
raising busy cores from 4.43 to 5.99. It also increased aggregate CPU work and
peak sampled RSS to 2.354 GB. All sixteen evaluated slots preserved logical
work and ordered decisions; maximum numerical difference was 1.33e-15. No
threshold was changed or favorable retry taken. Two owners remain the qualified
design choice; this does not reduce the aggregate hardware-thread budget.
The combined scheduling investigation took 107.53 seconds, including an initial
memory-admission rejection before DSP and the explicitly revised two-owner
partition. The global 3840 MiB budget was unchanged.
[Width extension](../local-evidence/continuous-process-width/process-discriminator.json)

The production continuous receiver now has the bounded two-owner scheduler.
Ordinary `push` can accept input while DSP runs; `resume(wait=True)` drains
accepted work. A lone job keeps the full visible logical-thread allocation;
two ready jobs share it. Private native owners isolate the mutable keyed basis
address, and the parent commits results in public-slot order. Runtime, input,
IPC and result ownership share the local memory reservation. Memory admission
can select a latched full-budget serial fallback. Transport failures and
interruption fail closed, and child teardown is bounded. Service accounting
unions actual background execution with controller API intervals, excludes
application polling delays and records one-time owner startup separately.

Independent source review accepted the integration; 80 focused parent/lifecycle
checks and 14 backend checks passed. The final required triad group passed
**22/22 in 32.84 seconds**, including the refill and auxiliary-memory corrections
below. The native functional gate passed in 24.15 seconds:
`001` decoded to `e`, quiet slots 3 and 4 completed the message, and quiet slot
5 was scored while listening continued. Four of six slots used the split
worker budget; there was no memory fallback. A truncated-input control remained
physically incomplete. An initial stdin-based launcher failed before DSP because
spawn could not reopen `<stdin>`; the identical call then used a guarded script.
No production code changed to repair that launcher. A subsequent narrow
ingress correction makes buffer backpressure wait only until space is available,
so the producer can refill while paired jobs continue; explicit drain/EOF still
wait for all accepted work. Its discriminating regression is included in the
80 checks. The native functional receipt predates that refill correction and
does not measure the integrated scheduler's throughput or qualify all cases.
[Final triad log](../local-evidence/continuous-pipeline/triad-final.log) ·
[Native pipeline gate](../local-evidence/continuous-pipeline/functional-gate.json)

The production row-7 backlog comparison then passed its predeclared integration
gates in **83.43 seconds**, including setup and cleanup. ABBA used ordinary
16,384-sample `push` calls and `resume(wait=True)` on the same five captured
windows: DATA bit 0 followed by four quiet windows. Both automatic looks used
allocations **12, 6, 6, 6, 6**, with no fallback or retries. All 1,649,627 supplied
samples were covered once (274.9378 seconds of physical input). Independent
audit checked 1,856 exact fields and 392 numerical fields across twenty slot
results; every exposed numerical value matched exactly. Deeper candidate/cache
parity remains scoped to the earlier two-owner discriminator.

| Production metric | Serial 12 | Automatic two owners |
| --- | ---: | ---: |
| Median complete receive service | 21.6792 s | 18.9520 s |
| Controller/worker service union | 21.6779 s | 18.9514 s |
| Average busy-core equivalents | 2.909 | 3.566 |
| Listed-process sampled CPU time | 63.055 s | 67.585 s |

This is **12.58% less elapsed service (1.144× throughput)** with **22.62% more
productive CPU occupancy**, at 7.18% more listed-process CPU time. These CPU and
busy-core figures cover the sampled evaluator/controller/native processes;
they exclude the shared resource tracker identified below. Elapsed service
includes its startup. The comparison includes
one-time spawned-owner startup (1.255 and 1.292 seconds). Subtracting only that
measured startup gives an 18.45% reduction over the five-window service; this
descriptive calculation is not a separate sustained-throughput measurement.
Full twelve-thread saturation and other rows' parallel capacity remain unproven.
All 66 measured sources and both capture hashes stayed unchanged.
[Production comparison](../local-evidence/continuous-pipeline/production-qualifier.json)

The sampler observed 1.318 GB across its listed evaluator/controller/native
processes. A subsequent audit found that it omitted Python's shared spawn
resource tracker (12.76 MB in an isolated matching-runtime check). That omission
does not exhaust the observed margin to the 3840 MiB limit, but it exceeds the
old conservative reservation's 7.64 MB headroom. The corrected implementation
reserves at least 32 MiB for that shared helper before spawning and retains the
reservation after closing owners or falling back to serial execution. It does
not create or kill the process-wide tracker itself. Explicit serial execution
from the outset creates no tracker and has a different memory allowance from
automatic fallback.

A **14.71-second current-source continuation check passed** on the exact
929,627-sample prefix of the production capture. The three DATA/quiet/quiet
results, work and numerical fields matched the earlier comparison exactly;
allocation was 12, 6, 6, without fallback. Sampled aggregate RSS including the
tracker was 1.277 GB. The 12.70 MB tracker was covered by the 33.55 MB reservation,
and modeled peak ownership was 4,015,521,077 bytes within the 4,026,531,840-byte
limit. Source/binary identities and post-exit cleanup checks passed. This
qualifies the corrected admission and work paths; the original ABBA remains
the timing evidence for its recorded source, not a rerun of the corrected
budget path.
[Tracker-budget continuation](../local-evidence/continuous-pipeline/tracker-budget-gate.json)

The [current execution timing chart](receiver-parallelism.html#cycle) records
**206 actual events** from a fresh five-window run on `c54e2469`, using the same
1,649,627-sample capture (274.9378 seconds of physical input). It shows parent
ingress/startup/assembly/commit, both owners' nested preparation/search/fine
stages and a preselected early fine cycle in quiet window 3. This replaces the
historical trace as the report's primary view; the old `3b423f9` trace remains
inside an explicitly historical comparison.

The plain control took **18.8923 seconds** and the traced run **18.8647 seconds**.
Their observed difference is −0.146%; this single ordered pair is neither a
speedup estimate nor a causal estimate of tracing overhead. Work, scores,
decisions and message state matched exactly. Both used allocations
**12, 6, 6, 6, 6**, with stable source, binary, capture and adapter hashes. The
whole bounded capture/check took **40.64 seconds**. No receiver code changed.

The timestamps show actual refill overlap: window 3 starts at 11.645 seconds
while window 2 runs until 13.377; window 4 starts at 13.386 while window 3 runs
until 17.455. The selected affine/adaptive cycles took 27.490/39.859 ms, each
with six basis jobs and six native Gaussian candidate jobs. They are fixed
early cycles, not a warmed average. Recorded call intervals include waits and
nested work; their overlap does not establish simultaneous OS execution or
full CPU saturation. Wall time uses the shared monotonic clock; thread CPU
values are independently sampled same-thread deltas, with small clock-sampling
differences possible. The chart does not qualify additional configurations or
blind reception.

Independent audit checked all event identities, nesting, dispatch/collection
order, physical coverage, source hashes and the twelve-worker permit ceiling.
Retain the compact trace while it supports this audited chart. The temporary
adapter, regenerated captures and redundant owner sidecars were removed after
acceptance (28.09 MB). Browser checks covered window navigation, interval
inspection, both fine-cycle views, historical disclosure and a 390-pixel layout;
no horizontal overflow or console warnings/errors were observed. Unique element IDs,
local links and JavaScript syntax also passed checks.
[Current timing receipt](../local-evidence/current-timing/timing-trace.json) ·
[Clock semantics](https://docs.python.org/3.13/library/time.html#time.perf_counter)

Private pipeline drivers and raw captures were removed after independent
audit (about 44 MB). Keep the compact comparison, functional/admission receipts
and original setup failures while they support the current scheduler claims;
remove them once superseded by an equally scoped validation. An unqualified
general parallel-forecast prototype was also deleted rather than maintained
alongside the canonical sampler. It produced no qualified capacity result.

One guarded FP64 phasor-recurrence experiment reused each direct phase anchor
for 32 samples without changing interpolation coordinates. The frozen paired
comparison preserved numerical/work gates but saved only 7.53% and 6.91% of
continued fine time on rows 7 and 4, below its declared 10% adoption gate.
The variant was removed. An earlier attempt was invalidated by a concurrent
source edit; that failure remains recorded separately. No source or binary
changed during the single authorized replacement comparison.
[Rejected recurrence](../local-evidence/structural-fine/phase-recurrence-discriminator.json)

Two-tile preparation overlap failed the declared 10% useful-gain gate before
and after native scoring. Wider workspace FFT batches and Python-only bounded
peak selection were slower. Those variants and their private drivers were
removed. The installed SciPy 1.18.1 FFT backend uses DUCC, releases the GIL for
the transform and already caches plans. The repository's power-of-two-only
native FFT would increase a representative 19,600-point transform to 32,768;
there is no established benefit from replacing this backend.
[SciPy wrapper](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/scipy/fft/_duccfft/pyduccfft.cxx) ·
[DUCC plan cache](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/subprojects/duccfft/ducc0/fft/fftnd_impl.h)

A bounded fresh-start supervisor prototype was rejected and removed. It tried
ordinal-zero starts on the public 30-second grid while continuing one acquired
message. In a native test with independent receiver configuration, leading
noise and a legitimate restart behaved correctly, but the eighth attempt
mistook an active message's ordinal-one DATA for a new ordinal-zero message.
Its DATA-versus-noise score was about 59.5 million against a 21.49 threshold.
The test stopped at that first mismatch after 36.175 seconds, with unchanged
source, input and binary identities. A noise-only admission bound does not
decide competition between two signal explanations. Subtracting existing
different-window scores or selected likelihood lower bounds would not repair
that model error.
[Failed native gate](../local-evidence/continuous-fresh-native-gate.json)

The same prototype has an unsuitable search-cost structure for the long-bit
cases: complete windows every 30 seconds would give 225 starts per 6,750-second
row-1 slot, before any competing-message work. This is a hypothetical work
density, not a measured CPU forecast. Native DATA keys incorporate start epoch,
symbol ordinal and local tile even without payload encryption, so one
UTC-shifted correlation prefix cannot represent all those starts. Retain this
failed receipt and its predeclaration/log while they support the unresolved
blind-acquisition design question; the unused supervisor, emitter fixture and
associated tests have been deleted. A replacement needs efficient staged or
Helper-assisted acquisition with declared missed-detection/false-start rules
and an occupied-signal model. The configured-calendar estimates must keep this
missing work explicit rather than labeling ordinary blind listening feasible.

Keep these compact receipts while they support the current implementation,
its numerical qualification and rejected alternatives. Private prototype
implementations have no continuing purpose. Replace raw receipts when a
matched current qualification supersedes their claim, preserving the concise
findings here. The earlier interactive report below remains a labeled baseline.

## Receiver restructuring

This section records the preceding source-pinned restructuring and the baseline
used by the interactive report. Later changes are recorded above.

That receiver stages immutable candidate plans, fuses mapped rendering,
DFT projection and sufficient statistics, and advances the two ready blocks
of a segment in one model job. Exact bit-pair geometry is evaluated once for
both independent DATA hypotheses. Global timing transitions and pruning retain
their original boundaries. Partial erasures and generic nonzero-null callbacks
keep the ordinary path. Worker failure drains submitted work, retains native
pointer owners and prevents publication of an incomplete incremental result.

At twelve workers, matched four-second continuation comparisons measured
**1.750× throughput on row 7** and **1.664× on row 4**, with 37.0% and 31.4%
less CPU time. The control disables the fused callback inside the same source
and uses the same capture, coordinates, allocation and native artifact. These
are fine-stage comparisons, not whole-service speedups or ratios against an
older calibration. Useful work, retained states and endpoints matched; maximum
lane-score differences were 5.68e-13 and 2.27e-13. Busy cores rose from 2.81 to
3.09 and from 3.02 to 3.44. **Whole-receiver saturation is still unproven.**
[Row 7](../local-evidence/structural-fine/row-7-12-final-overall.json) ·
[Row 4](../local-evidence/structural-fine/row-4-12-final-overall.json)

For the six-second row-7 session, Gaussian jobs fell from 144 to 72, mapped
worker submissions from 1,776 to 288, and physical cache hits from 18,083 to
236 while all 18,088 logical row/tile references remained. Exact bit-pair reuse
alone saved 18.9% of CPU time and 6.3% of elapsed time in its twelve-worker
comparison. The six-worker wall-time improvement was smaller and noisy; the
DC control established CPU-time savings but no reliable separate elapsed gain.
The full logical-thread default remains in force.

The preceding source's row-7 residual profile preserved work, states, scores and
endpoints in all five observations. Unmarked four-second continuations averaged
0.423 s and 3.16 busy cores. Instrumentation added about 2% on average. In the
marked path, mapping/statistics consumed 50% of elapsed time, model barriers
24%, and adaptive transitions 9%. These inclusive spans and worker CPU counters
must not be summed as independent elapsed costs. Fixed-affine setup, collection
and prediction together accounted for only 5.6%, an optimistic bound on savings
from persistence alone. A compiled batched state-update interface or safely
overlapped candidate lanes is a more substantial future restructuring, requiring
new numerical and transition-boundary qualification.
[Final residual profile](../local-evidence/structural-fine/row-7-final-residual-attribution.json)

A separate bounded chronological trace at source `3b423f9` supports the baseline
[interactive cycle chart](receiver-parallelism.html#cycle). One warm look and
four plain/traced ABBA looks took 4.34 s including fixture setup; no production
instrumentation was retained. The traced four-second continuation contains 16
candidate waves of twelve jobs, 16 model waves of three jobs and three native
cache fills. It reaches twelve overlapping worker job intervals. Within its
415.29 ms of instrumented push spans, 170.84 ms has no tracked worker job in
progress. That is neither an idle-CPU measurement nor a whole-receiver serial
fraction: controller work, native child work, gaps between wrappers and OS
scheduling are distinct. The traced mean push time was 6.84% higher than the
plain mean in these ABBA looks;
untraced controls averaged 379.32 ms. Work, ordered states, scores, endpoints
and channel posterior bytes at push boundaries matched exactly. These short
controls establish trace fidelity, not a new sustained capacity estimate.
The checked endpoint remained incomplete/ambiguous; this is not a successful
message trial. The displayed 417.03 ms axis additionally includes 1.74 ms of
probe checkpoint/digest gaps outside the push spans.
The receiver advances its phase/clock hypotheses using captured sample
timestamps, so CPU waiting does not age the captured waveform. Independent
observation or fixed-affine basis preparation can potentially overlap current
work, while adaptive state
prediction and posterior publication retain chronological dependencies. The
report's double-buffer schedule is an explicitly hypothetical model, with no
measured speedup claimed. Keep the [timeline receipt](../local-evidence/structural-fine/row-7-execution-timeline.json)
while it supports this current scheduling investigation; replace the snapshot
and remove the receipt when a changed scheduler supersedes it.

A bounded ready-lane overlap discriminator replayed a real row-7 segment's
prepared candidate work alongside the other lane's ordered model updates on
the same twelve-worker pool. Fresh model copies, pinned native owners and
49.22 MiB of scratch preserved statistics, posterior state and scores; raw
energy reduction differences stayed within the declared tolerance. Five warm
and twenty interleaved replays took 2.55 s internally. Six candidate jobs saved
a paired median 0.818 ms, below the predeclared 3 ms useful-gain gate; nine jobs
lost 0.924 ms. Even the positive result would save only about 1.5% of the final
fine slice before live planning/commit overhead. This does not justify a live
cross-lane scheduler change. The private driver was removed without changing
production sources or repeating the calibration.
[Overlap discriminator](../local-evidence/structural-fine/row-7-ready-lane-overlap.json)

Recurring integer-source bank preparation now leases each epoch/bit source
once, shares clock/ray plans and applies a native four-tap stencil in the worker
pool. The representative row-7 preparation fell from 6.628 s to 1.504 s with
source reuse, then to 0.490 s with the stencil. The final fresh stencil pair
was 1.434 s versus 0.490 s. Source requests fell from 1,248 to five. The final
twelve-worker observation averaged 3.51 busy cores; its six- and one-worker
observations were 0.396 s and 0.613 s. These single observations do not select
a smaller portable worker budget. All 78 workspace arrays agreed within
9.44e-16 peak-relative error, and DATA/H0 candidate identities, order,
coordinates and search work matched. A byte-equality gate failed and remains
recorded; the qualified result uses the declared numerical tolerance.
[Source reuse](../local-evidence/current-bank-lease/row7-pilot.json) ·
[Native stencil](../local-evidence/current-bank-lease/row7-native-stencil-paired.json)

A grouped NumPy projection variant increased CPU work without reducing
elapsed time and was removed. The post-reuse row-7 native source takes about
0.1 CPU-seconds, making further pulse-synthesis GPU acceleration a small target
there. A different FHSS coefficient/stencil interface could also remove
transport, but is not implemented or qualified. The earlier tensor-DFT
conclusion below remains applicable; no tensor speedup is inferred from this
CPU restructuring.

Large CPU observations also reuse the existing bounded FIR overlap-add path.
The branch depends on public sample count and backend, preserves finite tails,
global decimation phase and validity checks, and shares its actual buffer
estimate between ordinary and tiled acquisition. CUDA keeps its polyphase
filter. In a bounded row-1 public-geometry check with synthetic noise and a
tone, filtering fell from 6.715 s to 2.461 s and its modeled workspace from
2.940 GB to 0.342 GB. Projection relative error was 8.28e-16; full and tiled
search retained the same candidates and work. This is an operator/admission
check, not a native-channel communication trial or a complete capacity result.
[Filter qualification](../local-evidence/current-restructure/row1-streamed-observation-paired.json)

The fine and bank comparison receipts predate this isolated large-input FIR
routing change; their measured row-4/7 paths do not enter that branch. Keep
the scoped receipts, original failures and current integration results while
they support this qualification. Scratch implementations and copied sources
have been removed. Retire raw receipts when matched replacements supersede
the claims, retaining the concise algorithm and negative-result findings.

## Complete headless Robust and manual capacity

The current conditional forecasts cover all eight manual rows on the Ryzen 5 PRO
5650U in **explicit serial mode with all 12 logical workers and a 3840 MiB
workspace**. They include complete work on the configured public calendar.
**Default parallel capacity across all rows and blind fresh-transmitter capacity
remain unqualified.** These estimates do not inherit the row-7 pipeline speedup
or qualify automatic fallback with a retained shared tracker.

The bounded refresh took **3 minutes 42.93 seconds**, including two retained
setup/fixture corrections. It reuses reviewed unchanged DSP costs and measures
changed controller, input assembly, planning, codec and continuous-message
bookkeeping. All 66 current source hashes, both native binaries and runtime
identity were stable. Independent audit confirmed work/cache/memory checks,
unchanged DSP receipts and exact recomposition of costs and intervals. This is
conditional cost-class reuse with fresh wrapper costs, not new DSP timing
qualification or release certification.
[Current all-eight serial refresh](../local-evidence/continuous-receiver/current-serial12-refresh-h0.json) ·
[Applicability audit](../local-evidence/continuous-receiver/serial12-refresh-audit.json)

The underlying frozen sequential baseline took **14 minutes 24.21 seconds**,
including the configured continuous native gate, fixture preparation and bounded
measurements. Its 65 measured source files, native artifacts and case list stayed
unchanged. Exact source hashes on uncommitted `0751d4dd` are retained in the
reports. It predates the two-owner scheduler and supplies unchanged local phase
costs, rather than evidence of that scheduler's concurrency or IPC costs.
[Sequential baseline report](../local-evidence/continuous-receiver/calibration-all8.md) ·
[Sequential baseline JSON](../local-evidence/continuous-receiver/calibration-all8.json)

Every row includes the required configured-calendar receiver work. All eight
nominal numerical intervals are below 1 s/s, with point margins of
**82.92%–94.12%**. Seven nominal model decisions pass; row 4 remains unsupported
because its recurring-bank timing comparison narrowly exceeded its declared
30% gate (ratio 1.3043). No threshold was relaxed and no favorable retry was run.
The conservative work/model/interval gates support conditional capacity on
**rows 2, 5 and 7**. Rows 1, 3, 6 and 8 retain unsupported fine extrapolations.
Row 4 also fails the conservative numerical point criterion. These are scoped
CPU-capacity results, not communication-success probabilities or proof that
the hardware ceiling has been reached.

| Manual row | Nominal (s/s) | Nominal interval | Conservative point (s/s) | Conservative interval | Conservative decision | Continued fine busy cores |
| --- | ---: | --- | ---: | --- | --- | ---: |
| 1 Sub-9kHz | 0.074380 | 0.061820–0.089259 | 0.345250 | 0.204783–0.525967 | Unresolved | 2.656 |
| 2 Groundwave GPSDO | 0.076962 | 0.062438–0.095403 | 0.346030 | 0.286664–0.418095 | Conditional pass | 2.761 |
| 3 Groundwave Helper | 0.112558 | 0.084375–0.146508 | 0.329590 | 0.255563–0.450456 | Unresolved | 3.613 |
| 4 HF Helper | 0.165353 | 0.110595–0.235060 | 4.129237 | 0.372204–2101.40 | Unresolved; point fails | 3.833 |
| 5 HF encrypted GPSDO | 0.061270 | 0.045831–0.083959 | 0.278895 | 0.130658–0.505194 | Conditional pass | 3.268 |
| 6 FHSS | 0.170817 | 0.117349–0.240995 | 0.533668 | 0.436313–0.692413 | Unresolved | 2.774 |
| 7 HF unencrypted GPSDO | 0.058782 | 0.037936–0.089245 | 0.266651 | 0.222546–0.323282 | Conditional pass | 3.399 |
| 8 EME | 0.127802 | 0.075502–0.209365 | 0.521022 | 0.282059–0.879363 | Unresolved | 2.866 |

Row 4 still requires the broad 8,596-frequency fallback. Its conservative point
is **4.129 s/s**, while the declared uncertainty procedure gives 0.372–2101.40
s/s and leaves the acquisition model unsupported. That upper endpoint is an
uncertainty warning, not a credible precise maximum. Its bank observations were
0.683 and 0.524 seconds; the failed comparison is preserved. The matched kernel
speedups above are the evidence for the optimization, rather than a ratio
between unrelated complete-suite point estimates.

The reused fine measurements range from **2.66 to 3.83 busy-core equivalents**.
Maximum-state fine reaches 5.18, and FHSS nominal acquisition reaches 8.81.
**Whole-receiver saturation of all twelve threads remains unproven.** The
portable full-thread default is unchanged; pilot winners and this laptop's
power limits do not authorize reducing it. That baseline compares 1/6/12
workers within one stream and uses fresh twelve-worker inference. It does not
repeat the optional separate sustained allocation campaign.
[CPU allocation contract](development.md#receiver-cpu-allocation)

The baseline's named `./build.sh test triad` passed **22/22** in 34.91 seconds.
Its native gate decoded canonical `e` (`001`), completed it after two scored quiet
slots, then processed a sixth quiet slot while remaining in continuous receive
state. Draining/resuming preserves the public ordinal; the captured-prefix EOF
negative control remains incomplete. This is one configured-calendar
functional case, not blind transmitter discovery or an all-case success bound.
[Named test log](../local-evidence/continuous-receiver/triad.log)

Keep this frozen calibration while it supplies applicable unchanged local phase
costs and unresolved bounds under an explicit source review. The earlier
calibration remains only as the
embedded architecture report's explicitly historical baseline. Remove raw
receipts when their documented purpose is superseded.

The first invocation stopped all eight rows at geometry provenance because a
relative exporter path hashed differently from the canonical absolute path.
That 64.40-second setup failure was retained. Correcting the executable spelling
accepted row 1 but left rows 2–8 at the work/cache/memory guard; their discarded
subreceipts prevent assigning an exact per-row cause. Source review found the
private fixture compared H0 input with DATA maximum-state references, although
adaptive work depends on input. It also omitted the input reservation normally
initialized by acquisition. Row 1's corrected workspace arithmetic remains
within its limit: 1,425,142,394 bytes against 3,327,035,141 bytes.

The single final continuation preserved accepted row 1 and reproduced each
remaining row's own H0 reference: seed 96341, variance, extent, coordinates,
physical origin, ordinary state mode and canonical input reservation. All
individual work/cache/memory checks passed; full-input workspace equals the
baseline in every new row. This recovers conditional applicability without
changing DSP, fitting new DSP timings, relaxing thresholds or upgrading old
unsupported models. The 74.49-second continuation brought total external time
to 222.93 seconds within the original 300-second budget. Earlier failed attempts
remain distinct from this accepted check.

Keep the compact reports, original failures and applicability audit while they
support these current estimates. The private proof driver, bytecode and duplicate
per-row outputs were removed after audit (16 files, 5.13 MB); they have no
continuing implementation role.
[First corrected partial refresh](../local-evidence/continuous-receiver/current-serial12-refresh-corrected.json) ·
[Original setup failure](../local-evidence/continuous-receiver/current-serial12-refresh.json) ·
[Applicability audit](../local-evidence/continuous-receiver/serial12-refresh-audit.json)

Development checks exposed and corrected three reception defects before the
current measurement: FP32 innovation arithmetic failed at very high SNR,
legacy 0.01 Hz final frequency snapping discarded long-duration coarse
resolution, and fixed-affine residual clock hypotheses introduced an unintended
IF-dependent frequency shift. Fine processing now declares FP64, uses the
public duration-dependent frequency grid with its full prior penalty, and
preserves acquired total CFO in the fixed-affine basis. The adaptive model
remains diagnostic and its full work is measured. Focused numerical, wide-time
phase, prior-decomposition and lifetime tests passed. Memory reviews also removed
retained session/cache cycles and included simultaneous profile-validation banks
and transient projection/control buffers.

The frozen-source native message gate passed: DATA 0, DATA 0, DATA 1 decoded
canonical `e`, followed by two fully scored quiet slots and physical completion
before transport EOF. Its captured-prefix EOF negative control stayed
incomplete. This one high-SNR case establishes a functional path, not all-case
sensitivity. The default invocation repeats this gate on current sources. The
earlier source-changing functional exercise is superseded by this frozen result.

The earlier receiver's
[sensitivity pilot](../local-evidence/robust-manual-sensitivity-current.json)
received both independent bits correctly at 8.113563 dB-Hz in case 7,
each followed by two quiet slots and a complete work receipt. A fresh,
predeclared [confirmation](../local-evidence/robust-manual-confirmation-current.json)
at 11.113563 dB-Hz received its first **5/29 pairs** correctly, then stopped at
its memory-accounting gate. These were ten successful complete single-bit
endpoints, not five full `fox` messages. The incomplete confirmation establishes
no >90% probability bound. Do not pool different SNR points or count timing
repeats as additional successes.

That confirmation's resource monitor incorrectly treated the largest reaped
child's lifetime RSS as native post-exec residency. Its five child counters
exactly matched the Python workers' peaks. A separate zero-sample discriminator
reproduced this: a 125.7 MB launcher/child high-water counter accompanied only
20.5 MB combined sampled RSS in two verified native executables after exec.
The future confirmation helper now samples owned post-exec native processes,
retains conservative workspace/runtime/decoder allowances and counts persistent
worker processes across waves. Polling does not certify every transient peak.
Four focused regression fixtures pass. The original resource stop and all five
successful pairs remain unchanged. The original cost allowance did not admit
completion after diagnosis; the experiment was not restarted or extended.
After registering those fixtures, the affected `triad_cases` check passed again.

The v3 default manual invocation used the continued-session measurement. The
promotion evidence below records that earlier phase and is historical; the full
v3 capacity tables were removed because current-source results supersede them.
The [promotion receipt](../local-evidence/robust-manual-promotion-current.json)
verifies that the promoted probe is identical and that DSP sources and the
native binary did not change. It preserves the original measurement phases:
the separate continuation phase used family size 24; the v3 default uses 64
for its then-current list. The v3 wiring and accounting were tested separately,
including acquisition gating, missing-stage subtotals, slot normalization,
workspace/RSS/utilization and propagation of supported nominal failures. The
old observations are not relabeled as a full run of v3. Its full run was
source-bound but is no longer the maintained CPU estimate.

Keep prior base, continuation, allocation, pilot, incomplete-confirmation and
promotion reports only for the stated unresolved sensitivity/resource
comparisons; retire each when equivalent current evidence supersedes its
purpose. The duplicate temporary continuation/allocation runners and earlier
exploratory message/allocation outputs have been removed.

### Retained historical coarse evidence

Earlier tests used separate coarse and abbreviated tracking stages. They do
not measure complete receiver work or qualify the current endpoint. The old
nine-entry baseline is retained in [its JSON](../local-evidence/robust-cases-current.json)
only to preserve then-current configuration/provenance and unresolved sensitivity
outcomes: nominal Sub9 had insufficient successes, and the HF/FHSS fixed-point
paired check failed on pair six. Those observations must not be relabeled as
current eight-entry coverage or complete-message outcomes.

The earlier [Groundwave JSON](../local-evidence/robust-groundwave-current.json)
records resolution of the Helper handoff/search failure. A five-point bookkeeping
bank had rejected a useful continuous interval through neighbor padding; the
corrected caller uses continuous union width, preserving weak/broad-pilot
fallbacks. Exact lag-domain ZoomFFT completed the public CFO bank. At nominal
20.0206 dB-Hz DATA / 30.0206 dB-Hz Helper, both bits produced correct coarse
proposals. Its coarse-only service was 0.006148 s/s on this Ryzen 5 PRO 5650U;
that value excludes complete fine tracking and message work.

At -8.72375 dB-Hz, all 22 independent noise pairs recovered both coarse bits
using the complete 3,619-point prior after Helper fallback. The 90% one-sided
paired-event lower bound was 0.900628, conditional on one sampled channel and
the old coarse endpoint. This is neither a 95% bound nor literal-`fox` success.
The old `--paired-followup` campaign entry was removed with the superseded staged
suite. Keep these compact historical JSON records while those sensitivity
questions remain unresolved; retire them when current complete-receiver evidence
replaces the relevant claims. Their obsolete implementations are not retained.

## GPU-assisted receiver measurement

The completed campaign below **predates the receiver restructuring**. It
demonstrates the forecasting procedure, not current-source GPU qualification.
The new local CPU calibration must not be combined with these old GPU traces.
During the restructuring, a disposable RTX PRO 6000 host never reached SSH
readiness and was stopped and deleted. Replacement RTX PRO 6000 WK, RTX 5090
and B200 deployments were refused for unavailable capacity. No new full GPU
trace or GPU-derived CPU forecast is claimed from those attempts.

The current GPU filter uploads at most 2^20 input samples per staging block,
keeps each host block alive through stream synchronization, and drains an
exceptional submission before releasing its buffers. It preserves CUDA
polyphase filtering and explicitly budgets device input, full filter output,
cropping and standalone caller-owned host input. The revised row-1 filter
estimate is about 1.020 GB with its bank; this is an allocation model, not an
observed CUDA peak. The named GPU diagnostics passed **40/40** tests,
including fake-device copy/lifetime/interruption and exact-budget boundary
checks. Actual new-source CUDA execution remains unqualified until a usable
pod is available. CPU calibration does not include this GPU-only leaf.
[GPU regression log](../local-evidence/current-restructure/gpu-tests.log)

The earlier [eight-row RTX PRO 6000 trace](../local-evidence/current-parallel/gpu-current-full.json)
took 426.221 seconds internally. Allowlisted source and matching native
artifacts were only a few megabytes and transferred over authenticated SSH/SCP;
the source archive was 1.58 MB. The three paired reference rows (1, 2 and 6)
passed their precision, decision and work gates. Tight-clock
rows 1–2 now evaluate the whole coarse grid in FP64 before pruning and use
precise fine rendering; other rows retain bulk FP32. Continued fine work was
1.76–2.44× faster than the pod's matched 12-worker CPU. Native waveform
simulation and bank preparation still consumed about 80% of end-to-end time.
Do not attribute those CPU stages to GPU acceleration or use one global
GPU-to-CPU multiplier.

The earlier [full-list forecast](../local-evidence/current-parallel/forecast-current-all8.json)
covers all eight rows with source-matched local CPU costs and
[independent CPU traces](../local-evidence/current-parallel/holdout-current-all8.json).
The eight isolated holdouts took 539.759 seconds inside the drivers. Row 5 was
once refused admission before any receiver observation; its original log is
retained, and a fresh invocation admitted the same 4608 MiB cap and 12 workers.
No memory guard or timing threshold changed.

In that earlier campaign, **all eight nominal and conservative timing checks
passed**. Its forecast reports
`complete_point_estimates_with_unresolved_support` after two bounded local
phase refreshes described below.
All nominal guarded intervals
are below 1 s/s. Conservative points for rows 4, 6 and 8 fail real time, and
conservative capacity support remains unresolved for every row after the
forecast's engineering allowance. This is complete work accounting with explicit
uncertainty, not a saturated CPU ceiling or
all-case communication qualification.

Maximum accepted no-cancellation errors are 14.57% of nominal service and
17.02% of conservative service. The
[initial forecast](../local-evidence/current-parallel/forecast-initial-all8.json)
retains the original row-2 nominal and row-7 conservative timing failures.
One [canonical row-2 nominal refresh](../local-evidence/current-parallel/forecast-refresh-row-2-nominal.json)
took 24.770 seconds, with family size 72 and at most nine looks. It sealed its
new point and interval before a fresh receiver observation; nominal error was
−2.29%. Row 7's initial maximum-state cost contributed 15.253% error against
the 15% stage limit, so it required a separate cost-class refresh. Its
[accepted maximum-state refresh](../local-evidence/current-parallel/forecast-refresh-row-7-cap-repaired.json)
took 33.057 seconds and nine canonical looks. It rechecked the retained erasure
observation against the new bound and sealed the prediction before a new capped
receiver observation. Conservative error was +4.98%. The two accepted refreshes
took 57.827 seconds in summed measured time, excluding the failed setup attempt
below. The final report recomputes the complete list with only those two fits
and observations replaced. Other component checks remain explicitly mixed-time
verification. No measured source or precision/work/timing threshold changed.

The first row-7 maximum-state attempt used the canonical sampler, but its
temporary wrapper rejected live tuple coordinates against archived JSON lists
before sealing or obtaining an independent observation. That transient fit
had not been persisted and supplies no replacement numerical result. The
[failed-attempt disposition](../local-evidence/current-parallel/forecast-refresh-row-7-cap-disposition.json)
records that limitation and the original log. An
[offline diagnosis](../local-evidence/current-parallel/cap-refresh-format-diagnosis.json)
reproduced the representation rejection on archived data and restored the exact
original forecast through ordinary JSON normalization. One explicit format-only
harness repair then normalized and persisted the fit before verification; that
run produced the accepted receipt above. This repaired an entry-format failure
before any independent accuracy test. It did not retry a failed timing result
or relax a near-threshold limit. Both temporary wrappers were removed.

The disposable RTX PRO 6000 pod was stopped and terminated after results were
verified locally. No persistent volume was created. Temporary tensor sources,
CPU experiment drivers and the upload archive were removed. Retain that
campaign’s calibration, GPU references, holdouts, original failures and refresh receipts
while they support these claims and unresolved comparisons; retire them when
matched replacements supersede their purpose.

Earlier source-bound workflow results remain useful procedural evidence, not
current CPU calibration. A [cached same-case row-7 CLI forecast](../local-evidence/robust-v2/forecast-target-7-same-row.json)
passed both timing gates in about 47.82 seconds including target GPU work,
local verification and estimator startup, after applicable calibration and
paired references already existed. A [two-job cloud comparison](../local-evidence/robust-v2/gpu-dual-5-7.json)
ran rows 5/7 in 31.01 seconds over SSH with admitted aggregate CPU and memory
budgets and identical useful work. Independent simulations accelerate cloud
preparation; they do not demonstrate single-stream CPU capacity.

The [prior source's original forecast](../local-evidence/robust-v2/forecast-all8.json)
failed six nominal timing checks and one conservative check. Separate bounded
phase refreshes then passed its full list, but that result does not apply to
the changed CPU implementation. A [matched driver crossover](../local-evidence/robust-v2/driver-crossover.json)
found only 1.58% difference between calibration and holdout drivers and did not
reproduce the historical drift or a material garbage-collection cost. No unique
cause or retrospective clock correction is justified. The earlier tight-clock
FP32 trace rejected rows 1–2 on candidate ordering, and encrypted-5→clear-7 cost
transfer remained rejected despite supplying its missing H0 class. Keep those
failures source-bound; they do not prohibit qualified lower precision elsewhere.
See the [preferred workflow](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
for commands and applicability gates.

### Tensor-core lag-search conclusion

Two direct lag-domain Fourier transforms were implemented and tested on the
RTX PRO 6000 Blackwell using Triton 3.4: TF32x3 and explicitly rounded TF32,
both with FP32 accumulation and unchanged FP64 phase setup, Gram/rank and
quadratic scoring. Each executed 135 tensor launches across the actual row-3
DATA, noise and recurring search workload. Executed-kernel PTX and disassembled
SASS included `mma.sync.aligned.m16n8k8.row.col.f32.tf32.tf32.f32` and
`HMMA.1688.F32.TF32`. The [Triton arithmetic contract](https://github.com/triton-lang/triton/blob/v3.4.0/python/triton/language/core.py#L1858-L1890)
and [NVIDIA instruction reference](https://docs.nvidia.com/cuda/parallel-thread-execution/#warp-level-matrix-instructions-mma)
describe those operations.

Both variants passed the existing paired CPU receiver gates without relaxing
thresholds: public cells, candidate order, decisions, all five fine-work strata
and their scores matched. The worst coarse-score difference used 0.40% of the
allowed tolerance for TF32x3 and 56.76% for rounded TF32. This establishes
bounded computational agreement, not independent communication sensitivity.

| Variant | Isolated transform including coefficients, relative to ZoomFFT | Whole-work finding |
| --- | ---: | --- |
| TF32x3 | 0.78× | Already slower at the isolated transform; actual H0 searches were also slower. |
| Rounded TF32 | 1.08× | Warm ABBA process medians were 32.580 s versus 31.975 s for ZoomFFT: 1.89% slower, with both paired comparisons slower. DATA acquisition also increased from 0.935–0.939 s to 1.23–1.26 s. |

The TF32 experiment met its numerical gates but failed its predeclared useful
whole-trace improvement criterion. A separate power-of-two-scaled FP16 variant
then tested the more efficient FP16 tensor instructions, with FP32 accumulation,
explicit zero/nonfinite/range guards and scaling, packing, coefficient setup
and rescaling included in its preflight timings. At the two public row-3 matrix
geometries, the isolated transforms ran 1.88× and 1.33× faster than ZoomFFT.
Preflight PTX and SASS directly showed
`mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32` and `HMMA.16816.F32`.
Relative L2 errors were at most 2.67e-4; the tested nonzero inputs did not
underflow during packing.

The paired row-3 FP16 receiver passed the unchanged CPU precision, decision
and work gates, with exact candidate coordinates/order and all five fine-work
records. Coarse-score error consumed at most 58.39% of the existing tolerance.
However, its measured acquisition stages took 4.103 s versus 3.543 s for
ZoomFFT; the sum of common measured stages was effectively unchanged
(28.248 s versus 28.314 s, −0.24%). These sums exclude CPU-reference validation
overhead and unassigned process overhead. One bounded pair establishes no
material gain here, not a stable slowdown across workloads. The receiver
wrapper's counter was attached to temporary screen copies and did not reach
the original-screen receipt: changed scores and eligible geometry corroborate
the alternate arithmetic, but direct instruction proof is confined to the
preflight. No per-receiver FP16 launch count is claimed.

All three implementations and their switches were removed; the canonical GPU
FFT path remains. Lower precision was adequate for this tested coarse workload,
but the higher arithmetic throughput did not make this algorithm replacement a
faster forecast. This concludes the tested variants; it is not a prohibition on
different tensor algorithms or a reason to force all bulk work into FP64.

The [TF32 disposition receipt](../local-evidence/current-tensor/disposition.json)
binds actual source/native hashes, runtime, unchanged inputs, paired checks,
warm timing order and instruction evidence. Its roughly 2.3 MB of constituent
receipts and the [FP16 disposition](../local-evidence/current-tensor/fp16/disposition.json)
with its three constituent receipts support these specific numerical and
performance conclusions; remove them when the conclusions are superseded or
the summary alone suffices. No experimental driver or copied source tree is
retained.

## Test timing investigation

On 8 November 2026, the dirty working tree based on `64f7feb6` was timed with
GCC 14.2, the native Release FLTK configuration and two test jobs. `test all`
stopped before testing: `test_terminal_ui` and `test_framebuffer` failed to link
native audio symbols. Separate web and build-tool runs also had preview socket
permission failures and temporary GPG-key setup failures. Retain the three logs
in `local-evidence/test-timing-2026-11-08/` to diagnose these failed checks;
remove them when the link and environment failures are resolved or superseded
by equivalent current evidence.

## Transmit key policy checks

The uncommitted policy change based on `64f7feb6f298ba844f45ecbf1c57f02219158d16`
passed `./build.sh test contract` (26/26), `./build.sh test triad` (19/19), and
`./build.sh test gui` (27/27) on 7 November 2026. These runs used the existing
Release `build/dev` profile, two test jobs and four build jobs, with
`CCACHE_DISABLE=1` because the sandbox could not write the compiler cache.
The initial compile also exposed a missing test include, corrected before
these passing runs. Documentation link checks and `git diff --check` passed.

Coverage includes ordinary private/Amateur key parity, temporary noise DATA
keys and forced encryption, public-Amateur Helper decoding, changing callsign
ciphertext across absolute dwells, unchanged Helper vectors, independent
background noise, and the unavailable Robust boundary. Noise-mode coverage is
at the native policy/generator API; no GUI reconnection, operational noise
endpoint, physical transmission, or release certification is claimed.

## Cleanup validation

The CLI, FLTK GUI and pipe-worker applications rebuilt successfully. The FLTK
adapter and document test executables also compile and link; their native-window
runtime tests were not rerun. GUI controls remain, with Robust operation
explicitly unavailable pending connection to the canonical triad backend.

| Final affected check | Result |
| --- | --- |
| `./build.sh test contract` | 25/25 passed, 25.13 seconds |
| `./build.sh test triad` | 18/18 passed, 13.09 seconds |
| `./build.sh sanitize contract` | 25/25 passed, 37.49 seconds; ASan, UBSan and leak detection enabled |
| Final crypto/keyring fixture refresh under sanitizers | 2/2 passed, 8.69 seconds |
| Final focused crypto, keyring, Fast codec/CLI, GUI Fast, web worker/bridge | 7/7 passed, 40.40 seconds |

The triad Python aggregate ran 201 checks, with one native-dependent case
skipped there and covered by its separately registered native test. These are
small correctness checks, not full-observation or performance campaigns.
Independent source comparisons found the retained GF(256)/Reed–Solomon bodies,
Payload keystream path and prepared CBC implementation unchanged by the final
removal of unused recovery, framing and cryptographic APIs. Independent frozen
crypto vectors now exercise the retained prepared CBC API.

Earlier in the cleanup, the shared `regular` group passed 29/29 in 44.08 seconds
after removal of the two obsolete recovery/framing suites. The later crypto
change received the final affected checks above. Legacy passed 8/8 in 1.79
seconds. Web passed 13/13 in 146.47 seconds, including the retained Fast audio
case; the worker and bridge were rechecked after the final crypto change.
The initial Fast and GUI runs passed 35/37 and 26/27 respectively. Corrected
focused reruns completed every previously failing case; all 37 Fast and 27 GUI
cases have passed across those runs, but neither full group was rerun after
all cleanup changes.

The build-tool suite passed 27/32 in the sandbox; the five local socket/GPG
fixtures then passed outside it. Workflow tests passed 3/3, abbreviated-inference
tests 8/8, and certification-tool unit tests 86/86. The maintained-document
path/anchor audit and `git diff --check` pass.

Initial failures remain failed invocations: an invalid tiny channel fixture,
a CLI error exit code, obsolete GUI simulation/readiness expectations, and
missing native audio linkage for adapter test targets were corrected. The
initial sanitizer invocation could not run LeakSanitizer under the sandbox's
ptrace restrictions; the passing reruns retained leak detection outside that
restriction. No omitted check is counted as passed.

## Limits

RunPod instances supported the replay transport and the separately recorded
GPU parity investigation. The current manual capacity checks used simulated
baseband and local headless receiver work. The GUI remains disconnected.
Physical-device and release qualification are separate. The
[abbreviated regression harness](triad-performance.md) measures relative changes
in declared work and cannot establish absolute receiver capacity. Its historical
smoke measurement withheld an unresolved Gaussian projection and made no
comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

Useful [pattern](validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](validation-data/triad-v3/2026-11-05-stochastic/README.md)
measurements remain curated, source-bound research because their conclusions
inform the current triad. Preserve concise results, assumptions, limitations and
needed provenance; do not accumulate duplicate raw captures or parallel
implementations. The
[archive-retirement manifest](validation-data/triad-v3/archive-content-retirement.json)
records removed implementation copies and changed archive hashes; retained
triad data leaves were preserved byte-for-byte. Original source/internal hashes
remain historical provenance, not qualification of repacked archives or current
code. Consult the [evidence index](indexes/evidence.md) and Git history for
superseded results and their limits.

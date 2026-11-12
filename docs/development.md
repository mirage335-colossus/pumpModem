# Development contract

This page covers maintained cross-component boundaries. The current Robust
architecture is documented in [robust-current.md](robust-current.md); the
[protocol](protocol.md) and [GUI contract](gui-contract.md) remain the source of
truth for shared stream and presentation behavior. Do not treat retired
single-carrier or standalone triad/Helper implementations as compatibility
requirements.

## Automatic receiver capacity

**Prioritize complete-receiver computational ceiling and DSP workspace capacity
on challenging cases; defaults must minimize the risk of falling behind incoming
samples.** Automatic mode is the user's throughput-oriented choice across CPU
and GPU hardware. Higher-precision overrides do not justify a conservative
automatic default, and easy-case efficiency must not lower the challenging-case
ceiling. Apply the authoritative
[automatic-mode contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
to algorithm structure, arithmetic, batching, overlap and memory ownership. It
requires proportional reception/cost evidence rather than universal FP64 parity
or dB cutoffs, with explicit fallback and qualification limits. Preserve complete
public work, physical completion and the [full worker budget](#receiver-cpu-allocation).

## Native processing and integration

**For modem, DSP and other computationally intensive numerical work inside a
native application, prefer its integrated native implementation. In this project,
extend the canonical C++ stack and compatible in-process compiled libraries or
accelerator backends.** A convenient scripting library is not sufficient reason
to introduce another modem implementation, runtime or process boundary. Keep
signal generation, filtering, acquisition, tracking, decisions, codec processing
and audio processing in the native operating path. This rule does not require
rewriting ordinary orchestration/reporting scripts or optional frontend code.

The operating modem is one compiled C++ stack linked into the GUI application.
Helper generation/acquisition, DATA generation/scoring, ordered receiver state,
and audio have explicit internal interfaces. They share native buffers and
coordinate worker ownership in one process. Python may invoke, test or benchmark
that implementation; it is not a modem runtime, receiver subprocess, or IPC
transport. Python tools orchestrate compiled probes and analyze their returned
statistics; receiver DSP and persistent state have one C++ implementation. See
[current architecture](robust-current.md).

Use that integration deliberately:

- Share immutable input and prepared data directly. Give mutable receiver state,
  scratch buffers and device workspace explicit owners and lifetimes. Avoid
  per-stage serialization, copied transport and process-lifecycle machinery in
  the operating chain.
- Coordinate stage work through the shared [receiver CPU budget](#receiver-cpu-allocation).
  Design batch interfaces so parallel numerical work can overlap less parallel
  preparation of subsequent work where dependencies permit. Preserve chronological
  state/commit dependencies, cancellation and full memory reservations. Native
  threads and a compiled language alone do not establish productive overlap.
- Pass Helper measurements and uncertainty directly to DATA processing so they
  can narrow and maintain the relevant search bounds. Current Helper assistance
  supplies frequency intervals; phase/timing transfer needs separate correctness
  and performance qualification. Do not introduce IPC as the interface between
  these cooperating signal-processing stages.
- Keep CPU and GPU arithmetic behind native interfaces accepting useful batches,
  with suitable contiguous layouts, explicit shapes/strides, reusable workspace
  and qualified precision. Prefer extending the existing backend over building a
  standalone Python DSP or GPU reference that later needs another port. The
  [precision contract](gpu-precision-controls.md) still governs sensitive stages
  and fallback reporting.
- Test and benchmark the same compiled implementation used by the GUI. Extend
  native probe entry points rather than duplicating DSP in measurement scripts.
  On RunPod, build the minimal native source closure and run its compiled tests;
  Python may handle launch, case parsing and statistics. Record compiler, native
  library and device compatibility. A shared native stack does not imply one
  dependency-free binary that runs on every GPU or host. Fresh native probe
  processes for test isolation remain valid; they do not split the operating
  DSP chain.

The architectural lesson from the Python/native detour is to avoid making
receiver development depend on extra process ownership, transport, deployment
and cross-runtime integration concerns. The current native stack removes those
operating-path boundaries and exposes shared-buffer and worker coordination
directly. Historical measurements are not a controlled language comparison:
the current native scheduler improved first-message latency while its measured
total replay remained slower than the prior native baseline. Preserve the
[actual results and limits](validation.md#native-throughput-and-precision-investigation).
This preference does not claim that Python cannot call threaded native code,
CUDA or Tensor Cores, or that a C++ rewrite alone proves higher utilization.
See [NumPy's native threading guidance](https://numpy.org/doc/stable/reference/thread_safety.html)
and [PyTorch's CUDA and Tensor Core controls](https://docs.pytorch.org/docs/2.15/notes/cuda.html#tensorfloat-32-tf32-on-ampere-and-later-devices).
Measure complete useful work, backlog service and total test turnaround.

## Receiver CPU allocation

**The Robust receiver must preserve all available logical hardware threads.**
Detect hardware availability `H` from actual OS affinity and quota limits. The
automatic software-worker budget is **`W = 2*H`**: `H=1 → W=2`, `H=3 → W=6`,
and `H=12 → W=24`. Explicit worker counts remain exact. Resolve this budget once
in the receiver and share one bounded executor across slot and stage work;
nested layers must not multiply it. Report the software ceiling separately
from hardware utilization.

`CompleteStreamReceiver` admits independent in-process slot owners lazily when
complete public windows are ready. One receiver-specific executor lazily starts
at most `W` workers for all scoring roots and Helper, coarse and fine tasks. A slot scorer
runs as a root on one worker and is lane zero of its own stage; other workers
may execute that stage's leaves or another admitted root. Ready leaves are
prioritized by the oldest eligible public slot index; queued roots can use
lanes that this work does not need. This is scheduling priority, not an
additional worker allocation or a throughput guarantee. Do not add roots and
stage task counts as separate busy workers when a root is itself executing a
leaf.

### Overlap within a slot

**A slot's serial work must overlap independent parallel work on the same lane
set.** A stage has two leaf classes. Urgent leaves gate the owner's next
chronological step, and the owner runs them itself before waiting for helpers
that are already running. Released background leaves are claimed in order by
any lane with no urgent leaf to run, so they fill the tail of each urgent wave
and the owner's serial steps. The owner waits only for the background range
its next step needs; it never starts a leaf past that range.

Current overlap, all on one admitted lane/scratch shape per owner:

- Helper acquisition runs its independent steps as waves: both pilot fits,
  each target's frequency refinement and the branch filters. Every job records
  its own outcome, and the owner replays them in index order.
- Coarse preparation runs the observation filter and the mapped-profile gate's
  serial controls beside the template bank's per-workspace jobs.
- A slot whose lease has room for both starts coarse preparation before the
  frequency grid is known and acquires beside it. The owner acquires and posts
  its waves on the preparation stage. Where the mapped-profile gate needs the
  owner, acquisition runs whole on one lane instead.
- A native coarse advance searches its tiles as one span of dynamically claimed
  jobs. A fixed per-lane share idles every lane that finishes early.
- The fine fixed-affine lane has no chronological barrier, so its states are
  background leaves, at most one dispatch ahead. They overlap the adaptive
  lane's prediction, pruning and wave tails. The dispatch ahead is observed,
  and later dispatches' source tiles are filled, while the current one scores.

Keep these properties when changing a stage:

- Claim jobs one at a time. Give a lane reusable scratch instead of a fixed
  share of the work, and size jobs so a wave has several per lane.
- Overlap changes order and ownership, never arithmetic. Each job keeps its
  operands, sums are combined in a fixed order, and work receipts match the
  serial order. Preparation ahead of need may touch only what will be requested.
- Bound lookahead by memory and charge it. Observation runs one dispatch ahead.
  Tile fills run about two tiles ahead, and stay on demand for a fine bank of
  more than 16 coordinates, whose fills ahead could displace live tiles.
- Do not spread one memory-bound serial job across lanes without a measurement.
  Splitting the observation filter's large transforms into parallel chunks
  raised CPU 70–95% with no elapsed gain under the earlier transform, so it
  remains one overlapped job.
- Keep a lock that a leaf takes per lookup shared. With `W=2*H` a holder is
  regularly preempted, and every lane then queues behind an exclusive one;
  this stalled up to 18 lanes on the mapped-basis tile cache.
- Logical-CPU busy time is not throughput. Check elapsed service and CPU
  seconds for identical work together; see the
  [measured limits](validation.md#receiver-overlap-and-transform-order-follow-through).
- Replay parallel steps in serial order. A job that can end a serial loop
  early stores its value or its failure, and the owner reads them by index:
  later jobs may have run, but nothing after the first failure is read.
- Start a stage before one of its inputs only where no choice in it needs that
  input. Coarse preparation precedes the frequency grid without a compiled
  backend. The exact one-arrival coarse profile disables Helper, so Helper
  acquisition and its overlap with coarse preparation do not apply there; its
  native bank and observation filter already overlap. A slow mapped profile,
  whose route the intervals decide, keeps its bank in the time domain until
  then. Work receipts must equal the session built from those intervals.
- A slot that fails for one reason reports it as the sequential order would.
  When acquisition and preparation both fail, either may be reported.

Under the full software budget an overlapped serial chain shares hardware
threads with the work beside it, so acquisition takes longer there than alone.
Its two periodograms are single transforms, and several onset apertures still
run one after another; see the
[measured results](validation.md#helper-acquisition-overlap-follow-through).

Each owner keeps private mutable state and an admitted maximum lane/scratch
shape. This is a DSP memory bound, not an exclusive reservation of worker
permits for the entire slot. Quote the largest ready prefix at full `W` scratch
per owner; prefer a balanced shape only when its full memory bound admits a
strictly wider ready prefix. The incremental quote for a newly ready window
uses that window's full-`W` shape and adds incoming payload only for positions
not already covered by the prefix. Keep the admitted shape fixed until the
slot ends, even when peers finish. This prevents uncharged expansion; the
aggregate worker ceiling remains `W` in either case.

For a newly ready window, admission quotes its full `W`-scratch shape and the
additional incoming payload needed only for positions not already covered by
the current ready prefix. It does not create a prospective owner for a vacant
position when `O=1` or the owner cap is full. Live RSS admission still applies;
the quote is not an absolute guarantee that a later window will be admitted
regardless of intervening process growth. The admitted shape remains fixed
through its slot. A slot may use an overlap lease when its resource bound
allows Helper acquisition beside coarse preparation; this scheduling choice
does not guarantee lower whole-slot elapsed time under contention.

Reject inadmissible work if no shape fits the full DSP,
stack, input/output and ingress bounds. The shared executor bounds actual
simultaneous work. The automatic owner cap is `W`; explicit owner limits remain
exact and cannot exceed
the worker budget. Do not create workers for empty work or reduce the global
allocation merely because one stage has fewer useful tasks. A standalone slot
owns one private instance of the same executor for its three stages, and a
standalone coarse or fine session owns its own, each with its caller as lane
zero and unchanged stage mathematics; none by itself measures the shared
stream scheduler.
The earlier scheduler-only comparison improved first-message latency but
remained slower overall than its baseline, and its root-first and leaf-first
failures remain in
[that record](validation.md#native-throughput-and-precision-investigation).
The later overlap and transform-order changes shortened the matched replay;
see [their evidence and limits](validation.md#receiver-overlap-and-transform-order-follow-through).

Window samples are immutable shared native storage. Charge their assembly,
retained input, live scorer workspaces, worker stacks, application baseline and
bounded completed results for their complete ownership lifetimes. Every owner
retains its full prospective DSP reservation despite temporarily narrower
dispatch. Stack accounting includes prospective workers and started idle pool
threads until the pool closes; an idle thread is not released stack memory.
Initialized I/Q storage is covered once by the greater of observed process footprint and
an explicit baseline-plus-owned-input floor. Outstanding DSP, stacks and bounded
outputs remain separately reserved: whole-process RSS cannot identify which
anonymous pages belong to a scorer or prove that its remaining bound has been
materialized. This can conservatively count resident DSP in addition to its
reservation; it must not credit unrelated resident growth against future DSP.
Completed out-of-order windows may release their owners before public-slot-order commit;
at most twice the resolved owner cap in uncommitted results may be retained.
Cancellation must retain ownership until the relevant work has stopped. No
subprocess bootstrap, resource-tracker, copied IPC or `memfd` transport is part
of this operating path.

Default operating RAM is cgroup-aware available memory minus
`min(2 GiB, max(512 MiB, 10% of available memory))` for host headroom. Unknown or
insufficient availability fails admission; swap does not enlarge the budget.
On Windows, automatic availability is bounded by physical, commit and virtual
address availability; commit capacity does not expand the physical RAM budget.
Explicit memory limits remain exact. The old three-owner, 3840 MiB and
half-available-RAM test settings are not portable limits. Admission must reserve
full public DSP bounds and room for further ingress; reject work that cannot
fit instead of silently reducing the declared worker budget.

Do not lower the portable default because of thermal or package-power limits,
low observed utilization, a local pilot winner, or minor timing differences.
Preserve capacity for sample backlog, different processors and changing
workloads. The measured host gained 9–11% checked compute throughput at twelve
versus six threads despite power limiting; those
[hardware observations](validation.md#sustained-cpu-hardware-scaling) are not
receiver service measurements. Optimize serialization and unnecessary work.
Energy efficiency is not this capacity goal; productive utilization and modest
throughput gains can justify additional memory and concurrency.

Historical Python-orchestrated receiver measurements remain in
[validation](validation.md#continuous-receiver-restructuring). They include the
three-owner comparison's failed 15% throughput gate and the user's subsequent
acceptance of its smaller useful gain. Its 49-window backlog observations were
81.09% before shared transport and 69.37% afterward under different RAM caps;
they are not a causal transport comparison. The latter's original exact parity
gate failed on allocation-dependent task counts, and its post-run input hash
was not performed. Preserve these limits. None qualifies the new in-process
scheduler or supplies its CPU calibration.

Judge new capacity with bounded same-stream work, throughput and memory
measurements, separating predeclared backlog intervals from startup/fill/drain
averages and software-permit usage. One numerical thread per outer worker
remains the rule for numerical-library test harnesses. An independent collection
of simultaneous simulations is not one receiver's throughput. Reuse old phase
costs only with explicit source/work applicability; changed CPU work or runtime
requires fresh affected timings. See the
[manual capacity contract](robust-manual-capacity.md).

Historical all-eight estimates cover a configured public calendar and keyed
ordinal through one twenty-four-worker pool on twelve hardware threads at a
9072 MiB cap. Nominal passes covered rows 2–8 and conservative passes rows 3
and 5; row 4's conservative estimate was a numeric failure and model gates
remain unresolved. Row 7's interrupted report lacks its canonical final-runtime
flag. The former strict forecast bridge accepted completed canonical reports
for rows 1–6 and 8, not row 7 or the audited union. These facts qualify neither
the native GUI runtime nor automatic multiowner capacity. See
[the original scope and margins](validation.md#complete-headless-robust-and-manual-capacity).

The receiver continuously decides DATA or null on a configured calendar and
keyed ordinal sequence. Two fully scored quiet slots confirm physical completion;
a codec reset preserves the global waveform ordinal. Unknown waveform epoch,
ordinal and start-time acquisition remains unqualified. Broader acquisition
requires appropriate address hypotheses, incorrect-address checks and measured
costs; it does not require a semantic transmitter-identity classifier. Neither
received lengths, transport EOF nor cancellation may select physical completion
or DSP geometry.

## Material lifecycle

Retain concise descriptions of alternative techniques and their useful test
conclusions when they can inform the current implementation. Record the question,
assumptions, relevant positive and negative results, limitations and enough
source/configuration provenance to interpret them. A future possibility alone
does not justify keeping a whole experiment or historical campaign.

Keep raw evidence, captures, logs and intermediate artifacts only for a concrete
current investigation, unresolved failure, validation claim or release duty.
State the purpose and cleanup trigger in the existing relevant guide or validation
entry; do not create a retention registry. When the purpose ends, replace
superseded narratives with the concise findings, remove redundant data and
generated outputs, and update references. Retain a minimal dataset beyond that
only when a specific current question requires the underlying data and the
summary cannot answer it; document why. Apply this rule to ignored local files,
remote instances and volumes as well as tracked material.

Maintain one canonical implementation for each supported path. Remove obsolete
implementations and their obsolete tests, standalone copies, build wiring and
executable code inside evidence bundles rather than moving them into archives.
Protocol breaks use Git tags and separate binary releases; do not commit
generated output just to preserve it in Git. Continue to protect required
published-release assets, certification evidence, unresolved failures, active
work and relevant third-party notices. The [build guide](building.md#local-output-retention)
explains reuse and retirement of local build and SDK caches.

## Shared security and presentation

Restrict received content before it reaches native GUI text, clipboard requests,
CLI text output, or transmit-draft processing. The allowed-byte policy and
Developer/Shellcode exception are defined in [security](security.md#received-content-presentation)
and enforced by the shared [GUI contract](gui-contract.md#received-content-and-drafts).
Keep original payloads only in bounded receive memory for explicit user-selected
saving. Raw-bit presentation may preserve exact bits; it must not expose rejected
source bytes as text, escapes, or an unbounded output path.

Preserve input validation, selected-index checks, and trusted allocation extents
in [receive processing hardening](receive-processing-hardening.md). Keep shared
GUI behavior consistent across FLTK, Rev, terminal, framebuffer, and browser
adapters where supported. A view or adapter change does not authorize weakening
source restrictions or creating a separate receive policy.

## Fast and Legacy

Fast and Legacy are independent modem implementations. Preserve their documented
formats, exact input/output handling, and distinct validation limits in
[Fast guidance](indexes/fast.md) and the [Legacy reference](legacy-modem.md).
A Robust architecture or protocol change does not change their compatibility
contracts. Keep shared UI and audio changes covered by the applicable frontend
and hardware-independent checks in [building.md](building.md#testing-stages).

## Simulation and precision

When adding or changing sampled channel behavior, follow
[channel-models.md](channel-models.md). Preserve selected Doppler, delay-spread,
selective-amplitude effects, continuous state and complete delayed tails. None/Off
is an explicit baseline. Keep simulated metadata and software-test evidence
distinct from physical-device measurements.

The headless Robust receiver keeps explicit CPU arithmetic. The RunPod
workload probe records its automatic CUDA precision class for forecast work.
Numerical changes need paired decision and work checks with explicit
arithmetic and fallback reporting. Follow the
[GPU precision guidance](gpu-precision-controls.md) and
[abbreviated testing policy](building.md#testing-stages).

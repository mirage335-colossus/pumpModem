# Development contract

This page covers maintained cross-component boundaries. The current Robust
architecture is documented in [robust-current.md](robust-current.md); the
[protocol](protocol.md) and [GUI contract](gui-contract.md) remain the source of
truth for shared stream and presentation behavior. Do not treat retired
single-carrier or standalone triad/Helper implementations as compatibility
requirements.

## Receiver CPU allocation

**The Robust receiver must preserve all available logical hardware threads.**
Detect hardware availability `H` from actual affinity and quota limits rather
than hard-coding this computer's twelve threads or six physical cores. Distinguish
that hardware capacity from the automatic software-worker budget
**`S = 2*H`**. This portable rule admits more software workers than hardware
threads: `H=1 → S=2`, `H=3 → S=6`, and `H=12 → S=24`.
One Robust-specific resolver applies it when no worker count is supplied.
Receiver orchestration owns that budget and passes explicit admitted shares
downstream; it must not multiply the budget again at nested receiver layers.
An explicit worker count remains exact. Preserve available capacity
to keep up with incoming analog samples and accumulated backlog on other CPUs
and under changing search, tracking and channel workloads.

Do not lower this portable default because of thermal throttling, package-power
or current limiting, reduced boost clocks, low average utilization, a local
allocation-pilot winner, or temporary/minor throughput differences. A limitation
on one laptop does not establish a better default for less constrained machines
or even for another workload on that laptop. Such findings guide bottleneck
optimization and interpretation or refresh of timing estimates; they do not
authorize an automatic reduction to fewer workers. The current host gained
9–11% from twelve versus six threads in sustained compute despite observed
power limiting; see the [hardware measurements](validation.md#sustained-cpu-hardware-scaling).

Distinguish the worker budget from stage dispatch. A stage may use fewer tasks
when there is less useful independent work, or when finer subdivision adds
measured overhead. Do not manufacture work to raise utilization, and do not
turn that stage's batching choice into a global thread cap. Keep one aggregate
concurrency budget and one numerical-library thread per outer receiver worker
to avoid accidental nested oversubscription. Low-level constructors and bounded
correctness fixtures may receive explicit allocations; they do not select the
normal receiver's portable default. `CompleteStreamReceiver` in every mode,
`CompleteBitReceiver` and a standalone `SlotProcessBackend` resolve `S` when
their worker argument is omitted. Serial, finite and custom paths use the same
rule. An upstream controller resolves once and passes explicit counts to its
children, with one numerical-library thread per worker.

The canonical continuous pipeline's automatic controller cap is `min(S, W)`,
where `W` is the resolved software-worker budget (`S` by default). An explicit
owner count may be any positive value up to `W`; there is no arbitrary six-owner
ceiling. Owners grow lazily only for ready complete windows whose full public
DSP bounds, worker permits and RAM fit. Every dispatched job receives at least
one worker and an exact memory lease that leaves room for later ingress; do not
spawn owners for empty work. This host has twelve hardware threads and
twenty-four automatic software permits, admitting up to twenty-four independent
controllers sharing those permits. Actual width follows ready work and resource
admission, not a fixed three-owner split.

Completed out-of-order results free their owners for more work while the parent
commits results in physical order. For resolved owner cap `C`, at most `2*C`
uncommitted records are retained (`4*H` with automatic defaults).
Idle owners may retire only when job-free if a later larger public geometry
requires a narrower pool. Private source/cache owners, input, IPC and completed
results share the memory budget. Fewer owners or a late serial fallback retain
`W`; reject work that cannot fit rather than quietly reducing the worker budget.
Explicit worker and owner caps remain exact.

Default operating RAM is cgroup-aware available memory minus
`min(2 GiB, max(512 MiB, 10% of available memory))` for host headroom. It is not
half of available RAM or a fixed 3840 MiB ceiling. Explicit memory caps remain
exact; the admission checks must still account for every live owner and buffer.
Unknown or insufficient available RAM fails admission; swap does not enlarge
the operating budget. After the first child handshake, prospective owner overhead
uses the maximum pre-DSP bootstrap RSS/HWM of the fully loaded canonical child,
not the parent's captured-IQ heap. Load the child modules once before declaring
it ready. Actual parent, live-owner and native workspace memory remain charged
and are rechecked when admitting work.

Canonical continuous input transport uses sealed, read-only Linux `memfd`
backing when supported. Charge one private parent assembly transient before
releasing that array; retain only the sealed descriptor and metadata needed for
retry. Children map input read-only and close each mapping before acknowledging
the job. This removes duplicate per-job input-copy reserves, not full DSP/native
workspace, raw-ring or result leases, and it is not a generic RSS discount.
Probe Linux capability lazily before pipeline admission. Unsupported platforms
use copied transport with its original full charges; finite, custom and serial
paths do not perform that OS probe. Full-size read-only input/lifetime checks
admitted three row-1 inputs at eight workers each where copied transport admitted
one, preserving full DSP reservations. This did not execute row-1 DSP or measure
its throughput. The completed native copied/shared comparison used three owners
at eight workers each and preserved checked outputs, two-quiet completion,
continued listening, EOF rejection and descriptor/process cleanup. It does not
establish an elapsed gain or an all-row shared-transport admission envelope.

Before the `memfd` transport change, the enlarged automatic owner cap passed an
independently audited 49-window continuous-capture check. Its predeclared
sustained-backlog interval averaged
9.730 busy-core equivalents against twelve hardware threads (81.09%); the run
reached thirteen active services and fourteen resident owners while sharing
twenty-four software permits.
This is bounded evidence for that capture, not full saturation, a paired speedup
comparison, current shared-transport utilization or all-row pipeline capacity.
Earlier hardware-count-capped and fixed-width measurements retain their original
scope; see
[current validation](validation.md#continuous-receiver-restructuring).
The shared multiprocessing resource tracker stays reserved at at least 32 MiB
after owner shutdown. Automatic fallback therefore differs from explicit
`slot_processes=1`; never inherit its workspace or capacity qualification merely
because both execute one full-budget window at a time.

The earlier measured production improvement applies to the two-owner row-7
capture and geometry.
Full-thread saturation and all-eight-row parallel capacity remain unproven.
Per-row sequential manual estimates and the native continuous functional gate
do not establish that broader pipeline qualification.

Explicit 18/24 software-worker overrides remain supported on this host; they do
not turn its 12 hardware threads into 18 or 24 hardware threads. Record hardware
availability and admitted software workers separately,
retain 12 as the hardware-utilization denominator, and keep numerical libraries
at one thread per worker. Charge added runnable work, memory and coordination.
The three-owner comparison missed its original 15% elapsed-gain gate; preserve
that result. The user subsequently chose its modest throughput gain and higher
productive CPU occupancy as worthwhile for this and less power-limited hosts.
That changed adoption criterion accepts incremental throughput gains with useful
utilization; the three-by-six arrangement is historical evidence, not a fixed
controller endpoint. It does not retroactively turn the old gate into a pass.
Giving two owners nine
or twelve workers each was slower in the bounded screen. Keep useful work and
elapsed throughput visible alongside utilization, and continue removing serial
controller preparation as the accepted mapped-descriptor reuse does. Energy
efficiency is not the optimization objective for this work. See the
[measured decisions](validation.md#continuous-receiver-restructuring).

Retain explicit allocation controls for user limits and scoped diagnostics.
The manual suite's `--no-full-logical-inference` deliberately opts into pilot
selection; agents must not enable it to react to power/thermal limits or small
timing differences. With allocation flags omitted, inference uses `S`;
`--full-logical-inference` explicitly selects `H` for a diagnostic, and
`--threads W` preserves exactly `W`, including intentional overcommit.
A cloud example pinned to `--workers 12` preserves a
particular paired comparison, not a universal cap. Report allocation, useful
work and actual busy-core measurements separately.

Preserve the receiver's declared capacity scope as well as its CPU budget.
The [current all-eight complete-work estimates](validation.md#complete-headless-robust-and-manual-capacity)
cover a configured public calendar and keyed ordinal through one twenty-four-worker
pool on twelve hardware threads at a fixed 9072 MiB cap. The audited assembly
has conservative capacity passes for rows 3 and 5; nominal passes cover rows 2–8.
Row 4 has a conservative numeric point failure, and unresolved model gates remain.
These estimates predate the input-transport change. The reviewed applicability
receipt preserves the unchanged one-pool DSP/finite costs and raw identities;
its strict forecast consumer accepts the exact completed canonical reports for
rows 1–6 and 8. Row 7's interrupted report and the audited union are ineligible.
Follow the [GPU-assisted procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast);
this does not qualify all-eight GPU forecasting.
They do not qualify automatic multiowner pipeline ceilings. Preserve
constituent failures and receipt limits, including row 7's unavailable canonical
final-runtime flag; its final identity was checked externally after termination.
Unconfigured waveform epoch/ordinal/timing
acquisition remains unresolved; favorable timing or full thread use cannot
qualify that missing work. The operational contract is continuous bit/null
reception with two scored quiet slots confirming completion. Broader acquisition
coverage needs the corresponding hypotheses, incorrect-address checks and CPU
costs, rather than a semantic new-transmitter classifier. A codec reset does not
imply a native waveform-ordinal reset.
Follow the [current scope and findings](validation.md#continuous-receiver-restructuring)
and [manual capacity contract](robust-manual-capacity.md).

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

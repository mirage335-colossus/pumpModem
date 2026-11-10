# Development contract

This page covers maintained cross-component boundaries. The current Robust
architecture is documented in [robust-current.md](robust-current.md); the
[protocol](protocol.md) and [GUI contract](gui-contract.md) remain the source of
truth for shared stream and presentation behavior. Do not treat retired
single-carrier or standalone triad/Helper implementations as compatibility
requirements.

## Receiver CPU allocation

**The Robust receiver must preserve all available logical hardware threads.**
Detect hardware availability `H` from actual OS affinity and quota limits. The
automatic software-worker budget is **`W = 2*H`**: `H=1 → W=2`, `H=3 → W=6`,
and `H=12 → W=24`. Explicit worker counts remain exact. Resolve this budget once
in the receiver and pass admitted shares to its stages; nested layers must not
multiply it. Report software permits separately from hardware utilization.

The operating modem is one compiled C++ stack linked into the GUI application.
Helper generation/acquisition, DATA generation/scoring, ordered receiver state,
and audio have explicit internal interfaces. They share native buffers and
coordinate worker ownership in one process. Python may invoke, test or benchmark
that implementation; it is not a modem runtime, receiver subprocess, or IPC
transport. Python tools orchestrate compiled probes and analyze their returned
statistics; receiver DSP and persistent state have one C++ implementation. See
[current architecture](robust-current.md).

`CompleteStreamReceiver` admits independent in-process slot owners lazily when
complete public windows are ready. Each owner holds a scorer context with its
own mutable state and an exact share of `W`; its controlling worker and any
stage workers together consume that share. The automatic owner cap is `W`;
explicit owner limits remain exact and cannot exceed the worker budget. Do not
create workers for empty work. A stage may dispatch fewer tasks when useful work
is limited, without reducing the global allocation.

Window samples are immutable shared native storage. Charge their assembly,
retained input, live scorer workspaces, worker stacks, application baseline and
bounded completed results for their complete ownership lifetimes. Initialized
I/Q storage is covered once by the greater of observed process footprint and
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

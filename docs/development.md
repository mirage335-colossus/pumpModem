# Development contract

This page covers maintained cross-component boundaries. The current Robust
architecture is documented in [robust-current.md](robust-current.md); the
[protocol](protocol.md) and [GUI contract](gui-contract.md) remain the source of
truth for shared stream and presentation behavior. Do not treat retired
single-carrier or standalone triad/Helper implementations as compatibility
requirements.

## Receiver CPU allocation

**The Robust receiver's default worker budget is all available logical hardware
threads.** Respect explicit caller/user allocations and actual OS affinity and
CPU quota limits; detect the available budget rather than hard-coding this
computer's 12 threads or its six physical cores. Receiver orchestration owns
that budget and passes it to lower-level receivers. Preserve available capacity
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
to avoid nested oversubscription. Low-level constructors and bounded correctness
fixtures may receive explicit allocations; they do not select the normal
receiver's portable default. Normal callers of `CompleteStreamReceiver` and
`CompleteBitReceiver` must pass the controller's budget rather than rely on
their one-worker low-level fallback.

Retain explicit allocation controls for user limits and scoped diagnostics.
The manual suite's `--no-full-logical-inference` deliberately opts into pilot
selection; agents must not enable it to react to power/thermal limits or small
timing differences. A cloud example pinned to `--workers 12` preserves a
particular paired comparison, not a universal cap. Report allocation, useful
work and actual busy-core measurements separately.

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

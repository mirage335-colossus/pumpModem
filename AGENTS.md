# Development requirements

Use the [documentation index](docs/README.md) for maintained guidance. Check
[the development contract](docs/development.md), [build and test stages](docs/building.md#testing-stages),
and [current Robust architecture](docs/robust-current.md) before changing those areas.
Search `src/`, `include/`, `tests/` and `cmake/` first when locating implementation.

## Preserve the full receiver CPU budget

Default Robust receiver orchestration and capacity testing to **all available
logical hardware threads**, respecting explicit allocations and actual OS
affinity/quota limits. Do not reduce this default because of thermal or package
power limiting, low observed utilization, a single-machine pilot winner, or
temporary/minor timing differences. Preserve capacity for incoming sample
backlog, other processors and changing workloads. Smaller stage dispatch is
allowed when useful work is limited; it must not become a reduced global
worker default. Follow the [CPU allocation contract](docs/development.md#receiver-cpu-allocation).

## Manual tests target practical local CPU capacity

The editable `_tests-RobustModem.md` asks what this computer's **complete
receiver can sustain in real time**. Before using it, follow the
[agent workflow](docs/robust-manual-capacity.md#agent-workflow-for-cpu-ceiling-estimates).
Use efficient receiver algorithms and productive same-stream parallelism;
investigate serialization instead of treating it as the CPU's hardware ceiling.
Prefer qualified lower-precision and tensor-compatible GPU work with narrow,
sustained local CPU phase calibration and fresh holdouts. Follow the
[precision guide](docs/gpu-precision-controls.md) and reuse applicable evidence
through the [GPU-assisted procedure](docs/triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast).
GPU speed or a hypothetical optimization is not a CPU capacity result. Report
complete nominal and conservative service, uncertainty, margin and unresolved
work for every current row; a forecasting gate is distinct from a capacity pass.

## Prevent accumulation

Remove obsolete implementations and their tests, copied source, old performance
code, redundant historical evidence, abandoned experiments and stale builds as
part of the work that supersedes them. Do not keep them as examples or for
speculative future use. Preserve potentially useful alternative algorithms and
test conclusions as concise findings, not as old implementations or campaigns.

Treat experiments, captures, logs and generated outputs as temporary, including
ignored files and cloud volumes. Keep only what serves a concrete current
investigation, validation claim or required release record, and identify its
purpose and cleanup trigger in the relevant existing guide or validation entry.
When that purpose ends, distill applicable algorithm choices, negative results,
assumptions and provenance into concise guidance, then remove redundant outputs.
Do not keep whole campaigns merely because they might someday be useful, or
archive obsolete implementations, copied source trees or standalone variants.
Keep an indispensable dataset only when a specific current question cannot be
answered by the summary; explain why. Preserve evidence needed for unresolved
work, failed checks and release assurance until its purpose is resolved.

Reuse compatible build profiles and caches that current work needs; retire
abandoned profiles and stale outputs for removed targets after checking for
active users. Do not routinely purge useful caches or other contributors' work.
Apply the same lifecycle to local and remote storage. See the
[development contract](docs/development.md#material-lifecycle) and
[local output guidance](docs/building.md#local-output-retention).

## Robust architecture and intensive checks

The canonical Robust transmitter/channel/Helper path is the native
`IndependentApskV2` stack. The headless `CompleteStreamReceiver` connects noisy
Helper acquisition, full public `FFTScreen` proposals, fine persistent tracking,
bit/null decisions and scored quiet-slot completion to the canonical short
dictionary decoder. Implementation and short functional evidence do not qualify
all configurations or success probabilities. This backend is not connected to the GUI. Robust GUI
controls remain visible but Robust operation is unavailable pending
reconnection. Keep this boundary accurate in code, tests, and documentation.

Use one in-tree implementation. Protocol changes evolve the canonical source;
maintain older protocol compatibility with Git tags and separate binary releases.
Backport a necessary security fix narrowly to strictly sanitized dynamic-input
surfaces.

Computationally intensive Robust tests and performance estimates must use a
hierarchical, computationally minimal statistical method. Predeclare the covered
path strata, sample budget, uncertainty decision and stopping rule; use cheap
deterministic checks first. Extrapolate only from abbreviated measurements when
fixed-geometry assumptions, uncertainty, and source/configuration provenance are
recorded and applicable. Do not use full, sustained, multi-day, exhaustive, or
whole heavy-calibration runs as routine tests or fallbacks. RunPod may accelerate
the same method but does not justify larger experiments. See
[the maintained policy](docs/robust-current.md#testing-computationally-intensive-robust-changes)
and [the abbreviated harness](docs/triad-performance.md).
Run `./build.sh test triad` before refreshing local calibration with
`tools/triad_manual_cases.py`. Changed CPU work or runtime needs fresh affected
local timings; unchanged applicable calibration remains reusable.
Manual cases are absolute complete-receiver capacity tests, distinct from the
usual abbreviated performance regressions that look for degradation. They share
the efficient sampling method, not the meaning of a passing result. Follow
[the complete-work contract](docs/robust-manual-capacity.md): include fine search,
tracking, unsuccessful candidates, quiet-slot completion, reacquisition, recurring
preparation and orchestration. Measure productive parallelism within one received
stream on the local CPU, including its physical-core and full logical-CPU
allocations. Headless measurement commands default to the full visible logical
budget, with one numerical thread per receiver worker and explicit overrides.
Independent simultaneous simulations are not receiver throughput.
Missing or unmeasured required work must prevent a complete-receiver capacity pass.
Nominal service uses a brief uninterrupted fine session with a fixed two-second
prefix and 1/2/4-second continuation, verified cache fills and state/holdout gates.
Charge recurring work and prefix/finish once per slot. Do not infer a warm-up
benefit from fresh short sessions or replace the separate worst-state bound with
a nominal estimate. A numerical estimate at least 1 s/s is a point failure even
when its extrapolation uncertainty prevents a stronger capacity conclusion.
It reads `_tests-RobustModem.md` by default, requires all 16 strict fields,
preserves ordered duplicate rows, and reuses identical normalized inputs only
when explicitly marked. Use repeatable `--case-index N` for explicitly scoped
work. The full list is still parsed and validated, and partial output covers
only selected cases. Predeclare separate complete-receiver sensitivity checks
and keep those communication trials separate from CPU-capacity measurements.
Historical coarse-only sensitivity and isolated tracking results do not qualify
this complete-receiver contract. A prior comparison of numerical-library thread
counts is not evidence about parallel hypothesis/candidate processing. Preserve
explicit allocation controls and selected native channel effects; timing
replicas are not independent success trials. Keep raw case JSON/report
temporary in ignored `local-evidence/` and remove them when their current
validation purpose ends.

## Shared contracts

Robust and Fast finish messages from scored physical absence in quiet slots.
They must never use received packet lengths or arbitrary-length headers to select
DSP geometry, allocations, or physical completion. Transport EOF, cancellation,
local quotas and application metadata are not successful receiver completion.
Preserve fixed public geometry and local resource bounds; interior missing slots
remain erasures when transmission resumes. See the
[physical-completion contract](docs/robust-manual-capacity.md#physical-completion-and-security).

Preserve received-content restrictions and input validation in
[security](docs/security.md), [receive processing hardening](docs/receive-processing-hardening.md),
and the [GUI contract](docs/gui-contract.md). Keep GUI behavior shared across
native and optional frontends. Preserve Fast and Legacy behavior under their
[topic guidance](docs/README.md#other-maintained-components); do not infer their
compatibility from Robust research.

Before changing simulated channel paths, read the
[Doppler/delay-spread guide](docs/channel-models.md) and preserve selected effects,
continuous state, delayed tails, and explicit Off baselines. Before changing GPU
precision controls, follow [the precision guide](docs/gpu-precision-controls.md)
and retain explicit fallback reporting.

## Validation and outputs

Use `./build.sh` and named groups in [the build guide](docs/building.md). Run the
smallest relevant correctness checks during diagnosis, then the applicable
affordable checks. Never weaken contracts or report omitted or incomplete
coverage as passed. Documentation-only changes need proportionate link and
consistency checks, not receiver benchmarking.

When release assurance is in scope, follow [release validation](docs/releases.md)
and record current outcomes and limits in [validation](docs/validation.md).
Certification binds to exact source and binary hashes; publication, certification,
and promotion are separate operations.

Preserve unrelated edits and other contributors' work. Coordinate shared Git
changes and timing-sensitive tests. Keep temporary evidence in ignored
`local-evidence/`; maintained contracts and current validation belong in `docs/`.
Do not create session or coordination registries.

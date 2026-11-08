# Development requirements

Use the [documentation index](docs/README.md) for maintained guidance. Check
[the development contract](docs/development.md), [build and test stages](docs/building.md#testing-stages),
and [current Robust architecture](docs/robust-current.md) before changing those areas.
Search `src/`, `include/`, `tests/` and `cmake/` first when locating implementation.

## Robust architecture and intensive checks

The canonical Robust transmitter/channel/Helper path is the native
`IndependentApskV2` stack. Python WeakReceiver acquisition and
PracticalTrackingReceiver tracking are sequential stages and are not connected
to the GUI. Robust GUI controls remain visible but Robust operation is unavailable
pending reconnection. Keep this boundary accurate in code, tests, and documentation.

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

## Shared contracts

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

## Prevent accumulation

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

Preserve unrelated edits and other contributors' work. Coordinate shared Git
changes and timing-sensitive tests. Keep temporary evidence in ignored
`local-evidence/`; maintained contracts and current validation belong in `docs/`.
Do not create session or coordination registries.

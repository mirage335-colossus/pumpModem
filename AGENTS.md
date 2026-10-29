# Development requirements

Use the [documentation index](docs/README.md) to find maintained guidance.
Current compatibility requirements are in [the development contract](docs/development.md);
build and test stages are in [the build guide](docs/building.md#testing-stages).
Search `src/`, `include/`, `tests/` and `cmake/` first when locating implementation.

## Helper Carrier priority

Prioritize Helper Carrier's frequency and phase search-space narrowing for the
ordinary Robust receiver, especially the HF GPSDO alternative and also microwave
GPSDO supplementation. Do not make successful Helper auxiliary APSK decoding or
improved APSK sensitivity a prerequisite for this work. See the
[Helper Carrier priority and identification rationale](docs/helper-carrier.md#development-priority).
Preserve applicable regression requirements and report actual failures honestly.

For Helper-assisted ordinary Robust reception, assume the receiver is present
for the full 36-second Helper observation needed for frequency narrowing before
ordinary acquisition. The transmit lead is fixed at 95 400-ms dwells (38 seconds),
whether or not FHSS is enabled: 90 dwells for observation and five for detection.
Do not prioritize implementation or testing of receivers
joining an ongoing transmission without that lead-in. See the
[lead-in operating requirement](docs/helper-carrier.md#lead-in-operating-requirement).

For high-pilot identification, follow the [quiet-spectrum assumption](docs/helper-carrier.md#quiet-spectrum-assumption).
The strongest tone in the oscillator-model-derived expected high-pilot range may
be assumed to be the pilot. Do not add elaborate competing-tone identification
or tests for rejecting such tones solely to address interference excluded by this
assumption.

## Changes and validation

- Preserve wire formats and bit progress: 1–16 source-byte text keeps its fixed
  dictionary and exact endpoint; explicit binary input sends exactly its bits;
  longer text and attachments keep the configured 192-bit marker plus 128-coded-byte
  intervals. Do not add short-path overhead or let received lengths choose framing,
  allocation or completion.
- Decode source data only after physical completion. Reception ends only after
  consecutive fully scored absent-symbol durations cover six seconds. Expose each
  accepted bit on the next progress poll and update the same pending GUI row;
  preserve exact prefixes and distinguish pending from completed data. Keep the
  independent wire, physical-end and shared GUI regressions intact.
- For runtime changes, use focused checks during diagnosis, then complete the
  applicable regression and qualification stages. Never weaken a contract or
  report skipped, incomplete or omitted coverage as passed. See the build guide's
  [resource margins](docs/building.md#resource-benchmark-margins) and
  [sanitizer coverage policy](docs/building.md#sanitizer-throughput-scope).
- Build with `./build.sh`; run a group with `./build.sh test GROUP`. Reuse stable
  compatible profiles under `build/`; do not rely on historical top-level
  `build-*` trees as current results.
- When release assurance is in scope, follow [release validation](docs/releases.md#diagnose-a-branch-before-full-validation)
  and record actual results and limits in [validation](docs/validation.md).
  Certification binds to the exact published source and binary hashes. Publication,
  certification and promotion are separate release operations; diagnosis alone
  authorizes none.

## Shared work and outputs

- Use the main chat and native subagents for delegated work. Keep write ownership
  disjoint, preserve other contributors' edits, and serialize shared Git changes
  and final integration. Coordinate timing-sensitive tests and device access.
- Track launched processes until completion. Reuse compatible build outputs;
  keep temporary evidence compact in ignored `local-evidence/`. Maintained
  contracts, plans and current validation belong in `docs/`; use Git history for
  superseded narratives. Do not create session or coordination registries.

Documentation-only changes leave runtime behavior unchanged and need proportionate
checks. Detailed compatibility, testing, release and platform requirements remain
in their linked authoritative documents.

# Development requirements

Use the [documentation index](docs/README.md) to find maintained guidance.
Current compatibility requirements are in [the development contract](docs/development.md);
build and test stages are in [the build guide](docs/building.md#testing-stages).
Search `src/`, `include/`, `tests/` and `cmake/` first when locating implementation.

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

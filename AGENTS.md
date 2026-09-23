# Development requirements

Build/navigation entry point: [docs/building.md](docs/building.md).
Use `./build.sh` for the application and `./build.sh test GROUP` to build and
run the appropriate tests. Stable profiles live under `build/`; older top-level
`build-*` trees are historical and may contain stale binaries. Search `src/`,
`include/`, `tests/` and `cmake/` first; vendored code and historical validation
captures have their own documented provenance. Keep all checks below intact.

## Testing sequence for agents

- During diagnosis and implementation, build and run the smallest meaningful
  reproducer or affected test group. Use `devfast=true` only when its focused
  cases cover the fault; it is not general coverage. Avoid repeatedly running
  calibration, every platform or SDK builds while the same fault is unresolved.
- Once the bug is fixed and requested features are complete, advance to the
  normal full regression/general coverage for the final candidate. For CI use
  `devfast=false` (the default), including the applicable contract, GUI/native,
  platform, SDK and packaging checks. Do not finish a runtime change with only
  focused passes because broader checks are slower. Pure documentation changes
  need proportionate checks unless earlier code changes still await validation.
- Reuse completed evidence for the same source/configuration and avoid duplicate
  push, PR and manual runs. A temporary `[skip ci]` during diagnosis must be
  followed by an explicit full manual run or ordinary CI before completion.
  Wait for results; investigate failures with focused tests, then rerun the
  affected full checks. Do not count skipped, cancelled or queued jobs as passes.
  Automatic CI is deliberately lightweight on PRs and pushes to main; full
  native and SDK qualification require explicit manual dispatch after fixes.
  Runner defaults use the organization's H pools; do not repeat checks on
  smaller runners unless specifically requested.
- Native CI excludes only `fast_session` and `gui_fast_live` from its instrumented
  Debug job unless `sanitizer_realtime=true` is explicitly requested. Their
  real-time audio budgets are sensitive to sanitizer overhead; this is omitted
  instrumented coverage, not proof of a hardware-only problem or a test pass.
  Keep both mandatory in Release and retain their existing assertions. Other
  sanitizer failures remain fatal; do not extend this exclusion to new failures.
  Local tests remain available unchanged. See [sanitizer throughput scope](docs/building.md#sanitizer-throughput-scope).
- Reuse the exact prepared Linux SDK and Windows dependency recipes from the
  durable `base` release where applicable in ordinary release, certification
  and full CI jobs.
  Missing recipes require explicit base maintenance, not an implicit cold
  dependency build. Preserve source inputs, provenance and checksums, and never
  overwrite existing recipe assets. Windows reuses the runner's MSVC/Windows
  SDK separately; do not redistribute them. See [base maintenance](docs/releases.md#windows-dependency-base).
- When release delivery or assurance is in scope, follow publication with full
  certification of that release's exact source and binary hashes. A branch fix
  does not fix an older release; publish new binaries when needed. Diagnostic
  success cannot certify or promote a release. Preserve earlier release assets
  and reports, and record actual results and remaining limits in
  [docs/validation.md](docs/validation.md).
- Rev replay/waterfall display cadence is an advisory warning, not a build or
  certification gate. Keep it visible in logs/release `warning.log`, and do not
  spend long diagnostic runs tuning this known presentation limitation unless
  requested. Data integrity, physical completion, pending identity and source/
  bitmap correctness remain mandatory; see [release warnings](docs/releases.md#rev-display-warnings).
- The exact Windows Rev probe error `Required WGL ARB extensions not available`
  is an explicitly accepted hosted-environment warning. Only the four OpenGL
  source cases and published GUI smoke may be omitted under that classifier;
  clipboard, compilation, headless GUI/CLI, modem, packaging and calibration
  remain required. Reports must say `passed_with_warnings`, enumerate omitted
  graphics coverage and retain a per-run warning log. Such a green workflow is
  not full Windows Rev graphics qualification and must not promote Latest.
  Other graphics errors, assertions, crashes and timeouts remain failures.

See [testing stages](docs/building.md#testing-stages) and
[release validation](docs/releases.md#diagnose-a-branch-before-full-validation).

## Compatibility requirements

Preserve the existing short-message, fixed-interval and pending-reception behavior.
Read [the development contract](docs/development.md) before changing transport,
compression, receiver progress, CLI messaging or GUI message presentation.

- Nonempty text of **1–16 source bytes inclusive** uses the existing fixed short
  dictionary with its exact codes and bit endpoint. Explicit binary input sends
  exactly its entered bits, including leading zeros and partial bytes.
- Neither short path gains a header, marker, padding, transmitted length, FEC or
  MAC. Each extra bit may cost hours or longer to transmit.
- Longer text and every attachment retain the locally configured, fixed
  **192-bit marker + 128 coded bytes** interval format. Received lengths must not
  choose modem framing, allocation or completion. Source interpretation stays
  behind physical completion.
- Only observed absence across fully scored symbols ends reception: consecutive
  failed durations must cover six seconds. An hours-long symbol must finish
  before being classified absent. EOF, cancellation, codec ends and successful
  correction/authentication do not substitute for this event.
- Expose every newly accepted bit on the next progress poll and update the same
  pending GUI row. Never batch behind a byte, interval, dictionary token or full
  message. Preserve exact prefixes within the existing diagnostic retention
  bound and distinguish pending from completed data.

These are compatibility requirements for ordinary development and refactoring.
Keep the independent wire vectors, physical-end tests and shared GUI regressions
listed in the contract; do not weaken them to accommodate an unintended change.
Documentation/test-only work must leave runtime behavior unchanged.

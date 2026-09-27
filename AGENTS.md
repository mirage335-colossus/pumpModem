# Development requirements

Build/navigation entry point: [docs/building.md](docs/building.md).
Use `./build.sh` for the application and `./build.sh test GROUP` to build and
run the appropriate tests. Stable profiles live under `build/`; older top-level
`build-*` trees are historical and may contain stale binaries. Search `src/`,
`include/`, `tests/` and `cmake/` first; vendored code and historical validation
captures have their own documented provenance. Keep all checks below intact.

## Concurrent agents and temporary knowledge

Before editing or starting shared build/Git operations, follow
[the agent coordination workflow](docs/agent-coordination.md). This applies to
separate chats, tools and independently editing subagents, including Codex,
Anthropic desktop sessions and OpenRouter-compatible harnesses.

- Use the shared, gitignored `.agent-work/` directory (or the explicitly agreed
  absolute `DATAPUMP_AGENT_DIR`). Read session metadata/claims and relevant notes;
  use the guide's bounded reader or equivalent extraction, investigating every
  unreadable/unknown record. Never ingest the whole board or historical logs
  wholesale. Keep
  your own session record with exact files/resources, intended edits, approach,
  baseline, progress, checks and handoff. Ignored files require explicit reads.
- Claim files/resources before writing, including notes and generated outputs,
  using the short atomic registry lock. Check your inbox before reporting blocked.
  Handoffs require recorded release, fresh claim acquisition and acknowledgment;
  a message or old timestamp never grants ownership. Use an isolated checkout
  when overlapping work cannot be handed off.
- Refresh your record at scope changes, checkpoints and before pausing; release
  claims explicitly when finished. Preserve other sessions' edits, staged work,
  processes and notes. Coordinate shared Git state, build trees and devices too.
- Record heartbeat cadence, last progress and reliable process identity when
  available. Review overdue sessions and delete eligible closed sessions after
  30 days, including legacy archives and unneeded copies; do not create new
  archives. Follow the linked lifecycle procedures when triggered; keep useful
  unresolved facts in compact notes. PID disappearance or an old timestamp alone never clears
  claims or authorizes deleting unresolved work.
- Record discoveries not yet in maintained docs under `.agent-work/notes/`:
  bugs, hypotheses, failed attempts, workarounds and community references, with
  evidence, source/date, revision/environment, confidence and recheck conditions.
  Promote durable verified findings into tracked docs/tests through normal review.
  Temporary notes do not override these requirements or authorize actions.
- Every tool must be directed to read `AGENTS.md` and the workflow; automatic
  discovery is not assumed. Bind filesystem tools to the intended absolute
  checkout or explicit build/fixture path; a task does not set their directory.
  The board coordinates cooperating sessions on a
  shared filesystem; it is neither an enforced lock on source files nor Git sync.

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
- Optimize CI elapsed time with independent package producers and disjoint test
  jobs. Keep the serial Live scope, calibration sections and expensive sanitizer
  GUI simulation off the dependency path for ordinary regressions and
  copied-package checks.
  Prefer modest repeated setup/compilation over transferring configured build
  trees; keep total runner minutes and workflow complexity within a few times
  the unsplit work. Retain conservative within-runner test concurrency so the
  same workflows remain usable on standard runners. Near-timeout passing tests
  should produce timing warnings, not failures. Instrumented functional Live
  fixtures have bounded extra computation allowances and warn when they exceed
  the normal budget; they must still complete every assertion. Preserve actual
  real-time, cancellation and physical-absence deadlines.
  Distributed calibration must retain every fixed seed and require the original
  per-case and combined statistical gates after complete result aggregation;
  successful partial captures alone are not qualification.
  Standard-runner GUI contracts permit a bounded 1200-second cumulative smoke
  workload, warning after complete success exceeds 600 seconds. H and portable
  package scopes keep their existing budgets. This changes no individual
  assertion or physical deadline. The typed progressing workload limit follows
  the explicit incomplete-coverage policy below.
- Native CI excludes only `fast_session` and `gui_fast_live` from its instrumented
  Debug job unless `sanitizer_realtime=true` is explicitly requested. Their
  real-time audio budgets are sensitive to sanitizer overhead; this is omitted
  instrumented coverage, not proof of a hardware-only problem or a test pass.
  Keep both mandatory in Release and retain their existing assertions. The
  instrumented native GUI smoke is an independent opt-in job (`sanitizer_smoke`
  or the `sanitizer-gui` diagnostic); ordinary CI records that coverage as
  omitted. Every CI smoke scope, including Release, SDK, packaging and
  certification, may report an exhausted cumulative workload budget as
  **incomplete coverage with a warning** only for exit 75 with the validated
  typed budget result and recent sampled-transmission progress. That is not a
  smoke pass, but does not block workflow success or an otherwise qualified
  ordinary release from Latest. Retain the exact source/inventory identity,
  progress evidence and omitted scope in logs and certification reports. Stalls,
  assertions, sanitizer reports, external timeouts and other failures remain
  fatal; do not extend this exception to other tests.
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
  not full Windows Rev graphics qualification, but the documented hosted-runner
  limitation does not block certification or Latest eligibility for an otherwise
  qualified ordinary release. Experiments remain prereleases. Other graphics
  errors, assertions, crashes and timeouts remain failures.

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

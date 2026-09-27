# Coordination guidance consolidation, 2026-09-27

This change addresses the recurring findings in the [sixth](agent-coordination-rerun6.md)
and [seventh](agent-coordination-rerun7.md) exercises, which used unchanged guidance,
plus still-relevant evidence from the [original exercise](agent-coordination-exercises.md)
and [reruns one](agent-coordination-rerun.md), [two](agent-coordination-rerun2.md),
[three](agent-coordination-rerun3.md), [four](agent-coordination-rerun4.md) and
[five](agent-coordination-rerun5.md). Historical reports remain unchanged.

This is an implementation and regression report, not a new blinded worker trial.
The entry point remains [AGENTS.md](../AGENTS.md) and the
[routine workflow](agent-coordination.md). Workers do not need to read this report.

## Evidence and changes

Both recent runs observed zero lost ledger edits and zero stale `Updated` values
on sampled claim changes (12 and 15 transitions respectively). They do not show
that collisions cannot occur, or establish equal research effectiveness with and
without coordination. The repeated shortcomings were practical friction and
publication/status mistakes, not evidence for adding another ownership phase.

| Observed problem | Evidence | Implemented response |
| --- | --- | --- |
| Exclusive creation used without atomic complete message publication; initial record directly written | Run 7, B/C/E messages and C registration; no partial-message consumption observed | Optional publisher stages complete bytes, creates missing inboxes, exclusively publishes messages/new IDs and atomically replaces checked records. No direct-write fallback. |
| Missing recipient directory caused a send retry | Run 6, C | Publisher initializes ordinary inbox directories without replacing existing contents. |
| Candidate records polluted scans; all four checker users retried closure formatting | Run 7 | Candidates live in claimed artifacts or initial stdin; publication staging lives inside the owned registry mutex. Filled active/terminal examples and actionable literal-jobs diagnostics. Unknown session entries remain errors. |
| Missing metadata triggered broad manual reads; malformed records were awkward to repair | Run 6, C; earlier bounded-reader feedback | Exact missing-field diagnostics, field discovery and targeted metadata/whole-claims or handoff extraction. Legacy fallback stays incomplete; explicit manual atomic repair preserves claims and history. |
| Truncated startup reads and substantial repeated scans | Runs 6/7, all run-7 workers reported startup truncation | Separate bounded document calls; read the routine guide once, recipes only when needed. Compact JSON preserves every decoded metadata field, complete claim and error. No terminal-owner filtering or wholesale log reads. |
| Shared ledger held through unrelated reporting/checker retries | Run 7, D: approximately 136 seconds; earlier contention | Prepare the entry first; acquire, acknowledge, edit, verify, release and notify. Whole-session closure and unrelated receipts follow release. No new handoff phase. |
| Closed with a delivery receipt still pending; contradictory current jobs/results/dirty state | Run 6, A; run 7 status observations | Release unnecessary file claims promptly while keeping the session nonterminal. Replace current status, checks, remaining scope and request disposition together. Finished-job details leave the Running jobs field. |
| Requested research silently became optional | Run 7: 3/5 researched; D/E used local evidence and the prompt said “may” | Evaluation prompts must require actual independent primary-source research when that ability is being measured. Score research separately; preserve omissions and distinguish self-report from traces. Ordinary tasks gain no ceremonial browsing requirement. |
| Wrong checkout exposed historical answer context | Run 5 | Bind every filesystem tool/subprocess to the explicit checkout; delegators provide board-ignore proof when parent inspection is forbidden. |
| Repeated requests, obsolete reply IDs or wrong releasing owner | Original/runs 1–4; successful handling in runs 5–7 | Retain inbox checks before blocking, one current request per scope, actual acquired-from provenance and acknowledgment. A wrong reply ID alone does not block a proven free scope. Unchanged bytes do not select the releasing owner. |
| Unique notes, temporary paths or build outputs mistaken for unshared/unclaimed space | Original/runs 1–5 and build-guide ambiguity | Claim every output, including unique paths, caches and external TMPDIR; preserve source stability. Build guide now states the same rule as the workflow. |
| Environment symptom mistaken for cause | Runs 2/3/5–7, GPG setup | Separate hypotheses from evidence, run a discriminating probe, and rerun the whole blocked coverage. As a preventive aid, search notes again on a new failure. Setup failure before assertions remains zero executed assertions. |
| Successful repair missed consumer fields or evaluator controls were nondiscriminating | Earlier runs, especially D provenance and E alias fixtures | Preserve explicit user/public contracts; freeze actual prompts, guidance/helper hashes and source inventories; keep raw/adapted results and discriminating negative controls separate. |
| Helper regressions absent from lightweight CI | Repository integration audit | Both helper suites now run in automatic tooling checks and the normal CMake `build` group. README/build navigation point to the same workflow and recipes. |

The audit retained existing safeguards: exact/component-aware claims,
covering-directory narrowing before child transfers, one writer per file, complete
terminal claims, positive recovery evidence, no age/PID reclamation, stable build
inputs, preservation of dirty/index work, immutable messages and 30-day eligible
cleanup without new archives. There is one shared format and no new daemon,
mandatory helper, vendor-specific instruction copy or application dependency on
the board. Runtime source, compatibility requirements and application assertions
are unchanged.

## Reducing distraction

The routine workflow fell from **4,721 to 3,100 words** (34% shorter, whitespace
count at baseline `1072b88ee1bf76d624db2ad6e5b9360c2073fdd1`). Detailed filled examples
and commands moved to the [on-demand recipes](agent-coordination-recipes.md).
Recovery/retention and evaluation remain separate references with explicit triggers.
The smaller routine guide retains complete ownership review; the total reference
set is not claimed to be smaller.

In an active/terminal/unresolved-entry sample, compact scan output fell from
2,704 to 2,197 bytes (18.8%) with identical decoded JSON. This measures formatting
only, not agent tokens, elapsed time or cognitive load. Targeted inspection avoids
irrelevant progress/handoff text while preserving the requested complete section
and explicit validation failures/incomplete status. The reader remains non-atomic: observations cannot
detect every transient change between reads. Fresh complete review under the
mutex still governs acquisition.

Required research, implementation, validation and delivery stay in the existing
task/progress fields. Neither the workflow nor the helper adds a mandatory activity
report, synthetic finding or research note when none is useful. Future evaluations
must measure read/publication/retry costs alongside actual repair/research outcomes,
not equate absence of lost ledger edits with an overall task pass.

## Validation

Validation ran on Linux using Python 3, the current working tree and isolated
claimed build/temporary directories. Source/test writers stopped and handed off
exact hashes before integration checks.

- **36 reader/checker tests passed:** complete nested/terminal claims, unknown and
  unreadable entries, privacy boundaries, targeted legacy fallback, compact semantic
  equivalence, observable concurrent changes, actionable closure diagnostics,
  no terminal-ID reopening and explicit extended-retention requirements.
- **26 publisher tests passed:** complete visibility, stale expected hashes,
  duplicate IDs/messages, symlinks/nonregular paths, changed locks/records,
  missing inboxes, stdin registration and injected write/link/replace failures.
  Six independent CLI processes race the same message destination with distinct
  256 KiB bodies and a concurrent reader: exactly one succeeds and observed bytes
  are complete. A separate controlled staging test checks absence before completion.
- **Nine documentation-example checks passed:** the filled active/terminal records
  were extracted and exercised through real CLI registration, transition checking,
  scans, both inspection modes, field discovery, missing-inbox message publication,
  duplicate preservation, stale-hash rejection and terminal publication without
  session-directory staging leakage. These are fixture checks, not real claims
  to the illustrative paths/timestamps in the examples.
- **25/25 configured build-group checks passed**, using
  `./build.sh test build --cli --build-dir build/agents/coord-guidance8-20260927-c72a/cli --jobs 2 -- -DDATAPUMP_COMPILER_CACHE=OFF`.
  A direct probe first established that the sandbox denied Unix and loopback
  sockets (`EPERM`). The full group then ran with approved socket access and a short
  claimed TMPDIR, rather than repeating speculative GPG workarounds. Five optional
  assertions remain skipped: two Arch integration cases and three source-SDK host
  ELF fixtures. Their prerequisites are absent; configured CTest passes do not
  turn those omissions into qualification.
- Independent read-only review found no blocking defect under the documented
  cooperative POSIX contract. Its two documentation findings were addressed:
  malformed-existing-record repair and explicit final claim review inside the mutex.

Local relative links/anchors, example shell syntax and whitespace were also checked.
Evidence is retained under the normal lifecycle in
`.agent-work/artifacts/coord-guidance8-20260927-c72a/` and the two helper sessions'
artifacts; this tracked report preserves the useful results after those expire.

The publisher is optional and requires safe POSIX filesystem primitives. Tests do
not qualify Windows, network/cloud-sync filesystems, independent vendor harnesses,
uncooperative writers, physical devices or live release publication. The changed
guidance has **not** undergone another blinded five-worker exercise in this change.
All actionable findings from the audited runs are addressed and regression checks
pass; improved agent behavior and reduced overhead require a future measured trial,
not a guarantee inferred from these implementation tests.

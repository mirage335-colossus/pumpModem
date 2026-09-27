# Coordination exercise rerun, 2026-09-27

This reruns the five blinded historical repairs from
[the first exercise](agent-coordination-exercises.md) after the coordination guide
revision. The primary checkout started clean at
`ee979fe99f49b2655a6f015b0662d3d1c96081b7`. Outcomes concern isolated exercise
snapshots, not current production defects or release qualification.

## Method fixed before dispatch

Five fresh workers received the same five problem descriptions, pre-fix parent
revisions, independent-web-research requirement, and mocked/local regression scope.
Each had a fresh single-commit snapshot with the revised `AGENTS.md`, coordination
guide and conditional lifecycle reference. Workers had no conversation history,
original repository history, previous solution notes, evaluator files or sibling
source access. Boundaries were instructions, not an enforced filesystem sandbox.
Three worker slots allowed overlapping waves, not five simultaneous workers.

The historical fixes were `0afc74b`, `ba80ff6`, `1719955`, `53c54c3` and `0a2a738`
(cases A–E). Original held-out historical test files were frozen before workers
finished. Matching historical positive/negative controls from the first run can
be reused where source/test/configuration identities match. Candidate tests and
any necessary interface adaptations are reported separately.

All workers shared a **fresh** exercise board at
`.agent-work/artifacts/coord-rerun-20260927/board/`. This prevents previous
solution-note contamination, but also reduces startup board size compared with
the first run. The original LICENSE was included upfront, correcting the first
run's fixture omission. These differences prevent a clean timing comparison.
No fixes or protocol reminders were to be supplied after dispatch; any intervention
would be counted explicitly. Normal file handoff messages and result acknowledgments
are part of the protocol, not reminders.

The evaluator initially held a covering directory claim on `shared/`, containing
the required result ledger. After the first three requests, it narrowed that claim
to a separate guard file and released the ledger. A controlled second-owner
fixture (`ledger-relay`, operated by the evaluator) acquired the ledger under the
same mutex before the old release notices were delivered. It added a sentinel
header. Thus recipients had a true release notice from the former owner but had
to respect the current owner's claim. This is a simulated ownership change,
not an additional independent repair agent. These two added challenges were not
matched tests in the original run.

Primary observations are preserved ledger content, claim coverage of observed
writes, note claims, release/acquisition acknowledgment, spontaneous inbox checks,
reminders, and explicit closure. Coding/research outcomes are secondary and must
not be mistaken for collision outcomes. The first run already had zero observed
lost edits; maintaining zero alone cannot demonstrate a lower collision rate.

A bounded Linux inotify observer records changed-file hashes and the claims visible
at observation time in `evaluation/write-audit.jsonl`. It observes source/test,
note, board-message and shared-ledger changes, excluding `.git`, Python caches,
external temporary files and evaluator outputs. It does not intercept writes or
measure all read/token costs. Event handling and current-record reads are not an
atomic audit of every write; very fast new-directory/file creation can also evade
a newly installed watch. Counts are observations, not proof of universal coverage.

## Evidence location

The ignored `.agent-work/artifacts/coord-rerun-20260927/` contains the snapshots,
fresh board, held-out manifest, observer identity and logs, and candidate checks.
The documented session-retention policy applies. This report retains the durable
comparison without making normal startup depend on reading exercise logs.

## Coordination results

The rerun shows better handoff discipline, with no observed overwritten ledger
entries. It does **not** establish a lower collision rate: the first run also had
zero observed lost edits, and these are small, nonrandomized exercises.

| Observation | First exercise | Revised-guide rerun |
| --- | --- | --- |
| Independent repair workers | 5 | 5, at most 3 concurrently |
| Observed lost edits | 0 | 0; seed, relay header and all five entries preserved |
| Direct inbox reminders | A needed one; B received a redundant reminder | 0 |
| Note ownership at creation | C initially omitted an explicit note claim | All five registered note scopes before creation |
| Covering-directory challenge | Not separately measured | All three first-wave workers requested handoff |
| Old owner's release notice while new owner holds file | Not separately measured | 3/3 checked current ownership and requested the new owner's handoff |
| Final worker claims | Released | All five released; handoffs acknowledged and sessions closed |

After the controlled relay released the ledger, workers also handled real peer
contention: B and C encountered A's ownership, then C encountered B's. Their
entries preserved previous contents. No repair hints or out-of-band protocol
reminders were sent. File release notices and result receipts were normal
coordination messages. All five independently researched authoritative sources:
GitHub/CLI documentation for A, C and D; CPython and Git for B; Python and
Microsoft directory-identity documentation for E. Each recorded sources,
revision/environment, findings and limitations in its own note.

Compliance was not perfect. A repeated a handoff inquiry and briefly recorded an
outdated inbox status before recovering without a reminder; it also conservatively
misread a disjoint guard claim once. A reported unclaimed `/tmp` test-fixture
outputs, which it cleaned up. These remain a generated-output compliance gap.
D and E noticed stale canonical next-check fields in the evaluator record, sent
messages and preserved its claims. The evaluator corrected those fields; appended
progress alone had not made the record internally consistent. One evaluator reply
to A carried an older request ID; A identified the mismatch and freshly acquired
ownership instead of relying on the notice.

The observer captured 79 events, including 26 output observations. None showed
multiple claimants. One observation of D's final note showed no current claimant;
D confirmed from its tool-call sequence that the note write preceded claim release
in the same call. This is consistent with a delayed observation, not evidence of
a post-release write. The raw event and worker account are retained; claim reads
were not atomic with writes.
An observer crash on a disappearing temporary test directory created a gap from
05:38:31 to 05:43:00 UTC; the second segment excluded transient artifact trees and
finished normally. Two initial note creations escaped event capture, but recorded
prior claims and filesystem birth timestamps support their registration ordering.
Thus the evidence does not prove every write was claimed, or exclude every possible
collision. Observer segments, identities and gap details remain in `evaluation/`.

## Repair and research effectiveness

Workers retained the ability to investigate, change code and run targeted and full
local tooling suites while coordinating. No production repair was integrated.
Worker-created tests and evaluator checks are distinguished below; passing workers'
own tests alone is not historical equivalence.

| Case | Worker validation | Independent evaluator result |
| --- | --- | --- |
| A: release race and SDK pairing | Release 39 passed; SDK 24 passed, 3 skipped | Four public pairing probes matched historical behavior; three publication-order/race probes passed. Raw historical release suite: 20 passed, 1 failure, 5 errors. |
| B: raw ZIP paths and recipe line endings | Four tooling suites: 143 passed | Unchanged historical Windows-base suite: 22 passed. |
| C: paginated REST assets | Final mocked suites: 92 passed | Interface-adapted historical suites: 87 passed; five public downloader probes passed. Raw historical tests had helper/interface coupling failures. |
| D: packaging versus application source | Release 67 and certification 43 passed | Raw historical release: 67 passed, 1 failure, 2 errors; certification: 42 passed, 1 error. |
| E: filesystem cwd identity | 13 passed, including alias and wrong-directory cases | Same 13 passed under evaluator-controlled symlinked temporary-directory spelling. |

A now rejects orphan and extra unmatched SDK source archives, which the first
exercise missed. It uses REST release creation and the returned release ID for
upload/publication; historical helper-name and command mocks do not directly fit.
One remaining historical release-note expectation was outside its assigned problem.
Its implementation permits multiple complete SDK pairs, whereas the historical
implementation required exactly one; the four pairing probes do not establish
full equivalence.

C's adaptations change mock import paths, optional low-level digest defaults,
HTTP header whitespace and equivalent error text, and exclude five helper-specific
cases. Separate public probes retain exact-byte, digest-preflight, corruption and
network-failure cleanup, and no-overwrite checks. Initial SDK tests accidentally
attempted seven unmocked `gh` API requests, all of which failed. The worker repaired
the mocks before its final run; this was not a wholly service-free execution.

D now preserves `packager_sha` and `repackaged_from` in certification evidence,
improving on the first exercise. It still omits the historical certificate's
`tag_sha` field. That output-schema difference remains a failed held-out expectation,
not an adapted-away pass. Other raw-test differences include a private helper name,
diagnostic wording and unrelated CLI error reporting. Its optional APT integration
attempt failed during GPG-agent setup: zero tests executed, not passing coverage.

These are mocked/local tooling exercises. They neither certify releases nor provide
native Windows/application regression coverage. Historical positive controls from
the first exercise were reused only for matching source/test/configuration scope.
Candidate patches, identities, raw failures, adaptations and probe logs are retained
separately so successes do not hide failed or omitted checks.

## Cost and conclusion

The revised routine guide is shorter, and all five workers completed research and
repairs while handling ownership. However, equivalent search quality, coding speed
or token cost has **not** been demonstrated. Total reads, token use and cumulative
coordination I/O were not instrumented. Worker-reported successful mutex counts
were A 7 (including 2 aborted registry transactions), B 6, D 3 and E 4; C reported
8 outbound messages. Retained coordination bytes are not total traffic. The fresh
board, upfront LICENSE correction and added contention scenarios also confound
elapsed-time comparisons.

The strongest improvement is autonomous handling of release/acquire handoffs and
explicit note registration. Remaining weaknesses are consistent checkpoint updates,
generated temporary-output claims and audit completeness. Keep the short routine
checkpoints and per-session notes; ordinary work should prefer separate result files
and a single integrator instead of deliberately contending on a ledger as this test
did. No additional routine documentation is added based on this small sample.

All five workers used Codex in one environment. The filesystem protocol is
vendor-neutral, but this rerun does not validate Anthropic/OpenRouter harness
behavior, five-way simultaneous execution, noncooperating writers, or shared
filesystems on separate machines. A stronger future comparison would randomize
old/new guides across matched tasks and measure read/token costs and write ownership
at the operation boundary.

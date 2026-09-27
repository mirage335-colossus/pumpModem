# Second coordination rerun, 2026-09-27

This repeats the five blinded historical repairs after the second revision of
[the coordination guide](agent-coordination.md). The clean primary baseline was
`c3963eac76e47f2fb705a0bd0e79c70d23a6d507`. Compare the
[first exercise](agent-coordination-exercises.md) and
[previous rerun](agent-coordination-rerun.md). Exercise repairs stay isolated;
these results do not identify current production defects or qualify releases.

## Method and comparison limits

Five fresh agents receive anonymous one-commit snapshots at the same pre-fix
revisions as before, with the current agent instructions and guide overlaid.
The evaluator knows the five historical fixes (`0afc74b`, `ba80ff6`, `1719955`,
`53c54c3`, `0a2a738`); workers receive only behavioral problem descriptions,
choose their own authoritative web research, implement repairs and run local
regressions. No historical solutions, previous notes, sibling source or evaluator
files are permitted. These are instructed boundaries, not filesystem isolation.
Three worker slots allow overlapping waves, not five simultaneous workers.

A fresh shared board avoids solution contamination and matches the previous
rerun's small starting board. LICENSE is present upfront. The behavioral targets
are unchanged; dispatch text is reconstructed rather than a byte-identical prompt.
This run explicitly asks workers to report temporary/cache/log destinations and
unclaimed writes in their final accounts. That reporting cue may itself improve
compliance, so effects cannot be attributed to the guide alone. No repair hints or
out-of-band inbox reminders are planned; any intervention must be reported.

As before, the evaluator initially claims a directory covering the shared ledger,
then narrows it to a sibling guard file. A controlled second owner acquires the
ledger before the evaluator's release notices arrive. Workers must check current
claims and request the new owner's handoff. One subsequent release notice will
carry a deliberately mismatched request ID while its scope is unambiguous and
ownership has been released. This explicitly tests the revised free-file acquisition
rule. The relay is an evaluator fixture, not a sixth independent repair worker.

Primary measures are preserved shared contents, note/output claims, handoff
ordering, current status fields, unnecessary waiting, reminders and explicit
closure. Repairs and research are evaluated separately. Frozen historical tests
are retained before worker results; interface-dependent failures will be separated
from behavior failures without calling adapted tests unchanged historical passes.
Matching prior historical control evidence is reused only for matching inputs.

A bounded periodic observer records changed persistent files and visible claims,
including complete session-record snapshots and ledger contents. It tolerates
vanished files and scans new note directories, addressing the previous observer's
crash and watch-installation gaps. It still does not intercept writes: short-lived
outputs, multiple writes between samples and write/release ordering may be missed.
External temporary files and artifact trees are outside its coverage. Worker tool
accounts and declared output locations supplement this limited observation.
Read volume, token cost and search quality are not instrumented end to end.

Ignored evidence is under `.agent-work/artifacts/coord-rerun2-20260927/`, with
snapshots, `board/`, frozen tests and private `evaluation/` logs. The documented
retention policy applies; ordinary agent startup need not read exercise reports.

## Findings

Ownership discipline remained effective in the exercised conflicts, and temporary
output handling was more explicit. This is **not evidence of a lower collision
rate**: all three exercises had zero observed lost edits. The newest wording also
did not eliminate unnecessary handoff exchanges or inconsistent current-state fields.

| Measure | Previous rerun | This rerun |
| --- | --- | --- |
| Observed lost ledger edits | 0 | 0; seed, relay header and all five entries retained |
| Direct inbox reminders | 0 | 0 |
| Covering-directory and stale-release challenges | All three first-wave workers respected ownership | All three requested the original owner, then the relay |
| Note registration | All five registered before creation; mixed event/birth-time evidence | All five first observed note creations had the corresponding owner claim |
| Temporary outputs | A reported unclaimed `/tmp` fixtures | All five report claimed output locations; no known unclaimed writes reported |
| Mismatched request ID | Incidental evaluator ID mistake; recipient acquired freshly | Deliberate mismatch led B to request correction before acquiring |

B received a notice naming its older request while the exact released scope was
clear. It requested clarification instead of using the new free-file acquisition
rule. Neighboring observations show A had released the file and C had not yet
appended; no sustained competing owner appears at the clarification time. Sampling
cannot establish every instant of ownership. The evaluator supplied a matching
confirmation, after which B acquired, acknowledged and appended safely. This is
one extra exchange, not a collision or a direct reminder. It does not demonstrate
the intended shortcut, and the earlier incidental success by a different worker
prevents claiming consistent improvement on this behavior.

There was also natural contention beyond the controlled fixture. E encountered
D's live ledger claim, sent `e-ledger-1`, completed its independent source handoff,
then matched D's release, acquired ownership and acknowledged before appending.
The retained guard claim was not treated as blocking its sibling ledger.

All five workers used scoped artifact directories for logs, caches and temporary
fixtures. D and E additionally registered separate short `/tmp` directories for
GPG diagnostics before using them, then released them after cleanup. These are
positive examples of the revised output guidance. Their existence and claims are
recorded; the observer did not monitor those external directories. The explicit
output-reporting cue in dispatch and worker self-reporting remain confounders.

Some waiting snapshots still retained older top-level progress/timing fields
alongside newer inbox or handoff entries, notably A and B. This did not result in
reclamation or lost edits, but the new single-current-state instruction was not
followed uniformly. No stale-owner recovery or 30-day deletion was exercised.

## Observation coverage

The observer finished normally after 3,444 scans, recording 139 events and 46
persistent-output observations. All six observed ledger versions retained the
previous version as a prefix; final seed, header and five case entries are intact.
No sample showed multiple claimants. Five ledger updates were first observed after
the writers had already released their claims. Acquisition acknowledgments and
worker accounts support write-before-release ordering, but this sampler cannot
prove it atomically. Those five observations are retained as an audit limitation,
not silently scored as fully observed claimed writes or as proven violations.

Every first observed note creation had its owner's claim. The observer had no
crash/restart gap; its longest scan took about 0.172 seconds, with a 0.25-second
sleep between scans. This improves coverage of persistent changes, not coverage of
all transient writes. All five workers closed with empty claims and acknowledged
handoffs; the evaluator stopped its observer and retained no source claims.

## Repair and research results

Each worker independently consulted authoritative web sources and recorded local
evidence, environment and limitations. A used GitHub reference/release semantics;
B used CPython ZIP behavior and Git attributes; C used GitHub/CLI pagination and
asset-download contracts; D used GitHub tags/releases; E used Python filesystem
identity and Windows support in Python filesystem documentation. No source answers or held-out
failures were fed back for repair. Notes demonstrate research alongside
coordination, not objectively equal search quality or speed.

| Case | Worker validation | Evaluator validation |
| --- | --- | --- |
| A: release ownership and SDK pairing | 106 local tooling tests passed; no test skips | Four public pairing probes matched the historical fix; three publication/race probes passed. Raw historical suite: 26 ran, 1 failure and 5 errors. |
| B: ZIP spelling and line endings | 221 passed, 3 prerequisite skips | All 22 unchanged historical Windows-base tests passed. |
| C: paginated assets and downloads | 157 passed, no failures/skips | 87 interface-adapted historical high-level tests and five public download probes passed. Raw historical suites failed on fixture/interface differences. |
| D: packaging/application provenance | 242 passed, 3 native-compilation skips; APT required a successful unsandboxed local retry | Raw release suite: 70 ran, 1 failure and 2 errors. Certification: 43 ran, 1 error for missing `tag_sha`. |
| E: directory identity | Focused 13 passed, 1 Windows-only skip. Broader suites: 407 passed, 12 skipped, 1 failure and 3 setup errors from GPG startup | Candidate fixture under evaluator-controlled symlinked temporary path: 13 passed, 1 Windows-only skip. |

A's remaining raw differences are helper/API mock shapes and a release-note
expectation outside its assigned problem. Its public probes cover matching-pair
acceptance, mismatched/orphan/extra-source rejection, raced-tag failure, upload
failure preventing publication and publication of the returned release ID last.
These probes do not establish equivalence for every possible inventory or service
response. Successful ref-creation fixtures use the actual response fields its
implementation validates.

C's adaptations preserve the raw files separately. They bridge mocked `gh` byte
streams, normalize header whitespace, provide `tag_name` in successful REST
fixtures, and accept equivalent corruption/invalid-ID diagnostics. Certification
fixtures invoke the real candidate downloader behind a mocked transport, retaining
size/hash checks. Five tests tied to a private helper signature are excluded as in
previous runs; separate public probes cover exact bytes, digest preflight, corrupt
and interrupted downloads, cleanup and refusal to overwrite. Initial adapter
failures are retained alongside final passes. These are explicitly adapted passes.

D correctly separates packaging and application identities and preserves
`packager_sha` and `repackaged_from`, but still omits the historical certificate's
`tag_sha` field. This repeated output-schema gap remains a failed expectation.
Other raw differences concern helper naming, diagnostic wording and unrelated CLI
error reporting. The evaluator does not waive the missing field as mere mock
coupling. E's broader GPG failures remain failures; its short-path retry did not
resolve them. D's separate successful retry does not qualify E's failed run.

Workers chose different broader test scopes, so test totals are not comparable
productivity scores. Skipped, omitted or failed checks are not passes. Native
Windows/application coverage, live publication and release certification were
outside these exercises.

## Interpretation

The best supported incremental improvement is explicit ownership of temporary
outputs, including out-of-tree diagnostics. The existing release/acquire protocol
continued to handle contested files without reminders or observed lost edits.
The mismatched-ID exchange and partially stale status fields show that prose alone
still does not produce uniform compliance. Keep those findings separate from the
remaining coding-test gap.

All five workers completed useful research and coding while coordinating. This
small observational run cannot isolate the guide's effect, prove an improved
collision rate, or establish equal coding/search efficiency. No randomized old/new
control or end-to-end token/read measurement was performed. Workers reported
registry acquisitions of A 5, B 5, C 4, D 5 and E 6; their scopes and retry reporting
differ. These counts are not total coordination cost. Controlled relay waiting and
three-worker scheduling also confound elapsed-time comparisons.

All workers were Codex agents on one filesystem. This is not a cross-vendor test,
a five-way simultaneous run, a source-merge stress test or enforcement against
noncooperating writers. Production source and the index remain unchanged. No new
guide changes are made in this evaluation; the outcome is the report and isolated
exercise evidence.

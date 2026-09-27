# Third coordination rerun, 2026-09-27

This evaluates the revised [coordination guide](agent-coordination.md), including
its ownership-first handoff decision and grouped checkpoint fields. The primary
checkout began clean at `7b0ec9965f1c4f4d475ca76dd0c10b4d7099df80`.
Compare the [previous rerun](agent-coordination-rerun2.md); exercise repairs remain
isolated and do not constitute production or release qualification.

## Design fixed before results

Five fresh agents solve the same behavioral problems from five historical commits:
A `0afc74b` (release race/SDK identity), B `ba80ff6` (ZIP paths/line endings),
C `1719955` (paginated assets/downloads), D `53c54c3` (packaging provenance), and
E `0a2a738` (directory identity). Each receives the pre-fix source in an anonymous
single-commit snapshot, current guidance, and no previous conversation, solutions,
held-out tests, sibling source or previous notes. Blinding is instructed rather
than enforced by filesystem permissions. Tool/test baseline bytes are verified
against the historical parents; original historical tests are frozen privately.

Dispatch retains the previous behavioral descriptions, local/mock test scope,
authoritative independent web-research requirement, and final output-location
reporting cue. No source solutions or evaluator-discovered failures are sent back
for repair. Normal board handoff messages and acknowledgments are permitted;
extra protocol reminders, if needed, must be counted. Three worker slots allow
five agents in overlapping waves, not five simultaneously.

The repeated challenges are a covering directory claim, a sibling guard remaining
after narrowing, transfer to a relay before old-owner notices arrive, and one
unambiguous release notice with an incorrect request ID. B receives that incorrect
ID, matching the previous case assignment. Two additional checks broaden coverage:
C receives the existing board through a `shared/../board` spelling; before the
second wave, a synthetic old terminal session retains the ledger claim. D/E must
respect that claim despite its terminal state and apparent age. The latter is a
controlled inconsistent record, with no actual abandoned process, not real stale
work whose ownership is being guessed. New checks are not matched comparisons
with earlier runs.

The same bounded periodic observer samples persistent changes, claims, session
records and ledger contents. It neither intercepts writes nor captures every
short-lived claim or temporary file. External temporary paths and artifact trees
are outside its coverage; worker records supplement that evidence. Matching prior
historical control evidence is reused where input identities match. Raw historical
failures, explicit interface adaptations and behavior probes are kept distinct.

Evidence is ignored under `.agent-work/artifacts/coord-rerun3-20260927/`, including
`board/`, snapshots and private `evaluation/` material, subject to the existing
retention policy. Ordinary startup need not read exercise reports. This is not a
randomized comparison, a cross-vendor test, or a measurement of total reads/tokens.

## Coordination findings

The first wave respected the evaluator's covering directory claim, then all three
agents rechecked ownership after old-owner release notices and requested the new
relay owner. When C acquired the ledger, A and B requested C rather than writing
under an obsolete notice. C, A and B appended in order without losing the seed,
relay header or earlier results.

B received an incorrect reply ID as in the preceding rerun. This time it checked
current ownership first and requested C, the intervening owner; it did not ask the
relay to correct the ID. This supports the revised decision order. It does **not**
fully exercise the free-file/wrong-ID shortcut: the ledger was already owned by C.
C also resolved the supplied board alias to the existing physical directory.

D and E independently detected that the old terminal fixture retained a claim,
preserved both record and ledger, requested recovery and continued isolated repair
work. B independently flagged the same fixture during its completion scan. The
evaluator recorded positive fixture-specific evidence that no worker or job ever
existed, preserved the original record, and explicitly released ownership. This
checks resistance to age-based reclamation, not recovery of a genuinely suspended
cross-host process. The fixture was staged after first-wave ledger claims were
released, shortly before B completed its final metadata scan.

One evaluator bookkeeping defect mattered: the relay's closure fields were below
its progress section, and an obsolete sentence still said acknowledgments were
pending. D flagged missing closure metadata. The evaluator moved the existing
fields into Current checkpoint and removed the obsolete sentence; it did not
change ownership or provide repair hints. The grouped template helps only when
its own users keep canonical state together and remove superseded status text.

## Comparison and audit limits

| Measure | Previous rerun | This rerun |
| --- | --- | --- |
| Lost ledger edits observed | 0 | 0; seed, relay header and all five entries preserved |
| Extra protocol reminders | 0 | 0 |
| Wrong-ID response | Asked for correction despite apparent availability | Checked current owner and requested that owner; no ID correction exchange |
| Temporary-note claims | All five | All five, visible at sampled note writes |
| Output ownership | All five reported claimed outputs | All five reported claimed outputs; D/E separately claimed short `/tmp` paths |
| Current-state fields | Some stale canonical timestamps | Grouped fields adopted, but some stale timestamps and obsolete blocker text remain |
| Added challenges | Not exercised | Board alias resolved; old terminal retained claim preserved by D/E and independently B |

The observer ran to normal explicit shutdown, capturing 148 change events,
including 46 persistent output events. Six observed ledger versions preserved
the entire prior prefix. Four ledger changes were sampled after their claims had
already been released; records and messages document acquisition/release, but
these samples cannot prove write-time ownership. No sampled output had multiple
claimants. There was no known observer outage; sampling is still not atomic
write tracing. External temporary files and generated artifact trees are outside
its coverage. All five captured candidates retained their hashes, clean indexes
and whitespace checks through closure. All worker claims were explicitly released.

This is stronger evidence for correct handoff decisions and resistance to unsafe
reclamation. It is **not evidence of a lower collision rate**: previous runs also
observed zero lost edits. No total coordination cost was measured. B reported
225.0209 seconds of explicit waits; those include deliberately staged ownership
and evaluator scheduling. Other reported millisecond registry spans cover only
selected operations and must not be presented as total overhead. Three worker
slots and two overlapping waves also limit concurrency conclusions.

## Repair checks and research

Agents were required to research independently; notes retain primary-source URLs,
access dates, local evidence, environment and limitations. A used GitHub reference,
release and asset APIs; B used Python ZIP implementation/docs and Git attributes;
C used GitHub CLI pagination and REST asset documentation. D researched GitHub
tag creation semantics; E used Python filesystem-identity documentation. Research and repair
quality are assessed separately from successful coordination.

| Case | Worker-reported local results | Independent evaluation |
| --- | --- | --- |
| A | 112 passed, 3 patchelf skips | Four public recipe scenarios match historical behavior; three publication probes pass. Raw historical tests have private API/mock incompatibilities and an unrelated experiment-note assertion. |
| B | 215 selected tests passed | All 22 unchanged historical tests pass. Actual Windows and compiler fixtures remain omitted. |
| C | 170 passed, 3 patchelf skips | Five public probes and 87 explicitly adapted high-level historical checks pass. Raw private-interface failures retained separately. |
| D | 242 passed, 3 patchelf skips; seven APT tests passed after a socket-permitted local retry. | 69 adapted release checks pass; adapted certification runs 43 tests with one error: missing JSON `tag_sha`. Other raw failures are helper/diagnostic/wording differences and an unrelated CLI test. |
| E | Focused: 13 passed, 1 Windows skip. Broader: 14 suites passed, 4 GPG-dependent suites failed startup, including a short-path retry. | Candidate fixture suite under a symlinked temporary root: 13 passed, 1 Windows-only skip; wrong-directory rejection retained. |

A's public checks cover matched, mismatched, orphan and extra SDK source recipes,
plus create-race refusal, failed-upload nonpublication and publication of the
captured release ID. C's adaptations bridge its output-path transport interface,
complete REST fixtures, accept equivalent diagnostics and omit five tests tied to
the earlier private downloader signature. Raw failures are not counted as passes.
The private evidence records exact changes and logs. Local mocks do not establish
live GitHub behavior, Windows qualification or production readiness.

D preserves application and packaging identities and improves human-readable
reports, but still fails the historical JSON certificate contract for `tag_sha`,
as in the preceding rerun. It is a partial repair despite its local passes. D's
initial release-test adapter also accidentally removed a following class header;
the evaluator corrected that harness-only slice, retained the initial error log
and reran unchanged candidate code. E's independent alias evaluation uses its
new fixture tests, not an unchanged historical suite. D diagnosed a socket
permission failure before a permitted local APT retry; E retained its unresolved
GPG failures after a short-path retry. Neither result is a collision.

D reported accidentally reading extra closed-session disposition text while
extracting metadata, although no sibling source or solution notes were inspected.
That is a bounded-reading weakness to retain in the assessment. No token count,
search-quality scoring or matched no-coordination control was collected, so this
run cannot establish equal research effectiveness, zero distraction or faster
completion. The agents did independently research, implement and run substantial
local checks while coordinating.

## Remaining guidance opportunities

1. **Make checkpoint replacement part of the registry transaction.** All workers
   used the grouped fields, but some claim/blocker updates retained an older
   `Updated` value. Observed examples include A acquiring/releasing the ledger
   with an old pending-C narrative, B replacing its request with a new owner
   without refreshing timestamps, and D adding a short temporary-path claim
   without refreshing the checkpoint. These were roughly 77–123 seconds stale,
   not evidence of abandoned workers. Existing instructions already require
   refreshes; a compact transaction example or optional validator would be more
   useful than another general reminder. Refresh `Updated`, inbox time only if
   read, progress only when it occurred, and next action; remove obsolete blockers.
2. **Resolve superseded requests explicitly.** A/B correctly moved to C and later
   B to A as ownership changed. The relay only needed the acknowledgment from C,
   its actual acquirer. Marking earlier request IDs superseded in the existing
   handoff field would make closure easier to verify, without another message
   round trip or a separate permanent log. The evaluator's stale acknowledgment
   sentence demonstrates the bookkeeping risk; no worker collision followed.
3. **Keep the safety rules short and test the missing cases.** Directory aliases,
   stale release notices and terminal retained claims were handled successfully.
   Do not add more prose for those already-followed rules based on this run.
   Next exercises should cover a free ledger with a wrong reply ID, malformed
   claims, real interrupted child jobs, shared Git index/ref operations and
   overlapping source edits across actual independent harnesses. These are
   coverage gaps, not failures observed here.

The guide was held fixed during this evaluation. These are targeted follow-up
recommendations, not changes claimed to have passed the exercise. The instructions
already separate coordination from repair correctness; repeated report-schema
issues should be checked against concrete requested output artifacts rather than
addressed by growing the coordination guide.

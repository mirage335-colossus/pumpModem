# Fifth coordination rerun, 2026-09-27

This evaluates the [coordination guide](agent-coordination.md) and optional record
checker at `ebacdae05eafb9164d640f243f0f5e4ed3470cf6`, following the
[fourth rerun](agent-coordination-rerun4.md). Guidance stayed fixed throughout.
Production source was not changed or integrated.

Observed record maintenance and handoff attribution improved: **0 of 16 sampled
claim transitions** retained an unchanged `Updated`, versus **2 of 15** previously;
all five completed workers acknowledged the actual releasing owner. Every ledger
entry survived. Both runs observed zero lost edits, so this does **not** establish
a reduced collision rate. None of the five workers used the optional checker;
its benefit remains unmeasured.

## Design and deviations

Fresh workers received anonymous snapshots preceding five different fixes:
A `0afc74b`, B `ba80ff6`, C `1719955`, D `53c54c3`, E `0a2a738`.
Historical identities, fixed code and held-out tests were withheld. Tools/tests
were byte-verified against the historical parents; current guidance and checker
were overlaid. Three worker slots required overlapping waves. Each worker had to
research primary sources independently, implement/test its repair, record temporary
knowledge and append to one shared ledger through the coordination protocol.

The first D worker disclosed an accidental search in the parent working directory,
which exposed relevant implementation. It stopped without source edits and was
excluded from blinded scoring. A fresh D2 worker used a pristine replacement
snapshot and an explicit per-command working-directory cue. Five completed repairs
therefore represent six dispatched workers. This intervention was for blinding;
no collision-protocol reminders or repair answers were sent. Blinding was instructed,
not enforced by filesystem permissions.

Repeat the covering-directory, retained-sibling, ownership-relay, board-alias and
synthetic old terminal retained-claim challenges. Add an intervening owner who
releases the ledger without changing bytes. Give B a separate shared file so its
wrong-ID notice can be tested while that exact scope is free. D's task now explicitly
names the certificate's `tag_sha` field, addressing the previous underspecified
acceptance criterion. These changes and different interleavings limit comparisons.

An evaluator fixture defect blocked normal build-wrapper configuration: reused
anonymous snapshots omitted vendored inputs, including
`third_party/xz/UPSTREAM.sha256`, although it exists in the original historical
commit. The tools/tests comparison did not check complete source/vendor inventories.
Workers reported the blockage and used direct Python tooling suites. Those results
do not establish a successful configured build group. Before reuse, reconstruct
complete historical snapshots and verify their tracked-file inventories, including
ignored vendor files, before creating anonymous commits.

Evidence is in ignored `.agent-work/artifacts/coord-rerun5-20260927/`, including the
preregistered protocol, original/adapted test logs, observer events, candidate hashes,
research notes and post-completion surveys. A stale prior copy of E's held-out test
was rejected by hash verification before dispatch and replaced from its original
historical commit. Finished candidates were never changed in response to evaluation.

## Collision and handoff observations

| Measure | Fourth rerun | Fifth rerun |
| --- | --- | --- |
| Observed lost ledger edits | 0 | 0; all five entries and original seed preserved |
| Extra collision-protocol reminders | 0 | 0 |
| Claim transitions retaining old `Updated` | 2 / 15 | 0 / 16 sampled transitions |
| Actual releasing owner acknowledged | One stale-owner acknowledgment | Correct for all five completed ledger handoffs |
| Wrong reply ID while exact scope free | Not exercised | B acquired and acknowledged without a correction round trip |
| Terminal record retaining a claim | Preserved | E and D2 preserved it and requested disposition |
| Optional checker use | Not available | 0 / 5, by post-run self-report |

The evaluator narrowed its covering directory claim to a retained guard file.
Relay acquired the released outputs; a second fixture then acquired and released
the ledger without editing it. A consumed that second release and acknowledged
quiet-owner, despite identical content hashes. Subsequent ledger acquisitions
followed A → B → C. C aborted an acquisition after its mutex-protected recheck found
A held the file, then followed the later transfer to B. B separately consumed the
free wrong-ID file's release, recorded the mismatch and preserved its original text.

After the first wave, the evaluator registered the old terminal retained-claim
fixture on the free ledger. E and D2 left it intact while doing independent work.
Recovery recorded positive evidence that the synthetic record had never had a real
worker or jobs, preserved its original record and explicitly released ownership.
This does not test recovery of a real resumable process. E acquired next; D2's stale
baseline assertion stopped its write after E intervened. D2 reconciled E's release
and acknowledged E rather than the earlier recovery notice. The guard was unchanged.
C's aliased board path resolved to the agreed physical board.

Three observable ledger holds lasted approximately **43, 45 and 33 seconds**;
the previous run's three observable holds were approximately **21, 93 and 190
seconds**. C and D2's short acquisition/release transactions were not captured as
separate held intervals. Entries were prepared after validation; no additional
repair/test run was observed during these summary holds. This supports the intended
short shared-write scope, but is not a controlled speed or overhead measurement.

The main sampler recorded 138 changes; an additional sampler recorded five D2
source changes, starting while its checkout was still clean. Combined: 143 events,
39 output changes, five ledger versions, all preserving their previous prefix.
No sampled output had multiple claimants. Three ledger versions, B's wrong-ID
append and the excluded D disclosure note were first sampled after their claims
were released. That timing is not proof of unclaimed writes. Sampled research notes
for the five completed workers had claims; generated artifact trees and external
temporary locations were not fully traced. Sampling cannot prove atomic ownership
at every write or count every short transition.

All five final patches/hashes matched their captured candidates; indexes were clean,
there were no untracked candidate files, whitespace checks passed, and overlaid
guidance was unchanged. Worker and fixture claims were explicitly released.

## Repair and research results

| Case | Worker-reported validation | Independent evaluation |
| --- | --- | --- |
| A | 112 passed, 3 missing-`patchelf` skips | Four SDK pairing scenarios match the historical fix; three publication probes pass. |
| B | 24 focused passed; broader 198 passed, 3 skips | All 22 unchanged historical tests pass. |
| C | 93 focused, 44 build-tool and 58 SDK tests passed; 3 skips | Five public download probes and 87 explicitly adapted historical checks pass. |
| D2 | 113 focused and 130 broader tests passed; 3 skips; APT setup failed before tests | 69 adapted release checks pass; 43 unchanged certification tests retain one missing `repackaged_from` error. |
| E | 14 focused passed; 15 of 19 broader scripts succeeded, four GPG startup failures; 5 prerequisite skips | All 14 fixture tests pass under an aliased temporary root. Restoring only the original assertion makes both alias regressions fail while different-directory rejection passes. |

Original A–D controls fail the held-out suites; their historical fixes pass. A's
raw historical run retains five private-interface/mock errors and one unrelated
experiment-note failure. Its public publication mock initially lacked complete REST
responses; completing those fixtures made all three probes pass. Initial failures
remain in separate logs. C's adapters bridge downloader stream/callback interfaces,
complete requested draft metadata and accept equivalent diagnostics; they retain
real candidate identity/hash/cleanup behavior. Five tests coupled to an older private
downloader signature remain excluded, as in the preceding run. D2 maps a private
selector name and equivalent rejection wording, excluding the same unrelated CLI
sanitization test. Raw failures are not counted as passes.

D2 now emits `source_sha`, `packager_sha` and explicitly requested `tag_sha`, and
checks the packaging tag identity. The historical certificate also expects a
`repackaged_from` field, which the candidate omits while retaining that provenance
in release metadata. The task did not name this additional certificate key. Record
the exact schema mismatch without treating it as demonstrated unsafe publication
or attributing the named-field improvement to coordination guidance. Future tasks
should provide the complete externally visible schema, not private helper names
or incremental hints discovered through failures.

E's unchanged historical Linux controls pass on both old and fixed tools; they do
not discriminate this fixture-only problem. The separate alias negative control
does. No case establishes actual Windows or live GitHub qualification.

All five completed workers recorded dated primary sources and applicable local
evidence: GitHub REST/CLI for A/C/D2, Python/CPython and Microsoft naming documentation
for B, and Python filesystem-identity documentation for E. Each independently reported
using web tools in a read-only post-run survey. None reported reading another repair
implementation. No complete tool-trace/token accounting or matched no-coordination
control establishes equal research effectiveness or lack of distraction.

D2 reproduced GPG startup failure with a separate probe but did not establish its
cause or complete a successful rerun. E did no further diagnosis or successful
rerun. Both clarified afterward that a long temporary path was a hypothesis.
These failures remain unresolved coverage, unlike the successful socket-enabled
reruns in the fourth exercise; they are not repair passes or proven path-length bugs.

## Improvements supported by this run

- **Make bounded reads easy to execute.** D2 and E reported extracting unrelated
  completion summaries while scanning claims; B/C also encountered extra summaries
  or hashes. Trial a small tested metadata/complete-claims extraction example, with
  explicit failure on unreadable/unknown structure and terminal claims preserved.
  More reminders to read less are unlikely to solve an extraction problem.
- **Bind each tool to its intended checkout.** The excluded D attempt demonstrates
  a real default-directory error. An explicit working-directory cue or harness
  binding can prevent accidental parent reads and reduce the risk of wrong-checkout
  writes. A folder named in a task does not change a tool's default directory.
- **Test helper adoption before expanding it.** All five chose manual updates;
  B preferred its compact format. Zero stale sampled timestamps cannot be credited
  to an unused checker. Trial a low-effort template/update integration and measure
  its cost before making any helper mandatory or adding more routine prose.
- **Separate environment evidence from hypotheses.** A compact shared failure note
  should name the exact error, what a probe established, what remains unknown and
  the blocked test scope. D2/E's GPG handling shows room for better diagnosis and
  qualification, even though collision prevention held. Existing guidance already
  requires this distinction; copying more policy text is not the obvious remedy.

Release references and current-owner checks worked in the exercised transfers and
need no additional ceremony on this evidence. Complete snapshot preparation and
explicit output contracts need evaluator improvements. Simultaneous edits to the
same source file, shared Git mutations, malformed registries, genuinely interrupted
jobs and different vendors/harnesses remain untested. The shared ledger exercises
ownership mechanics; isolated historical repairs do not cover all those risks.

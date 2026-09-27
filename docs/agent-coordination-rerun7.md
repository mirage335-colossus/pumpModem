# Seventh coordination rerun, 2026-09-27

Five fresh workers again preserved every shared ledger entry, with **no observed
lost edits** and **0 of 15 sampled claim changes retaining an unchanged Updated
field**. The [sixth run](agent-coordination-rerun6.md) also observed zero lost edits
and zero unchanged timestamps (12 transitions). This supports repeatability, not
a demonstrated reduction in collision rate.

The guidance was unchanged. All five repairs passed their configured build-tool
groups, with the skips and adaptations below. However, **only three workers
performed independent web research**. That part of the requested exercise was
not met across all five. Message publication, temporary record candidates and
stale status text also exposed practical weaknesses despite the preserved edits.

## Design and comparison

The main baseline was `945f139346fed107328e7563e555f442735c5d84`; its only change
since `e59697a` was the sixth-run report. AGENTS, workflow, lifecycle guide and
reader hashes were recorded separately and remained identical to the sixth run.
No guidance was edited during this study.

The same behavioral contracts were assigned to five fresh conversations: A
`0afc74b` (publication races and SDK pairing), B `ba80ff6` (Windows archive/recipe
portability), C `1719955` (paginated inventories and downloads), D `53c54c3`
(repackaging provenance), and E `0a2a738` (native fixture directory identity).
Workers received no solution commits, patches, private helper names or held-out
checks. Full parent trees were reconstructed, including ignored tracked vendor
inputs, with 3,830 / 3,856 / 3,840 / 3,879 / 3,914 verified entries, modes and
symlink targets. Guidance overlays were recorded separately; all five configuration
preflights passed. Baseline/fixed controls and E's aliased temporary-root control
were checked before dispatch.

There were five repair dispatches, no excluded replacements, and overlapping waves
of at most three repair workers. D started after B finished; E started after A.
A separate reviewer evaluated frozen C behavior and was excluded from worker
metrics. Prompts matched the sixth run apart from study paths and inconsequential
spacing. Tasks, guidance, criteria and initial controls were frozen before dispatch;
post-run surveys did not supply repair feedback. There were zero additional
collision-protocol reminders. Normal handoff, recovery and delivery receipts were
sent; A/E additionally requested and received proof that their external board was
ignored by its containing repository.

Blinding was instruction-based, not enforced filesystem isolation. Workers reported
no prohibited implementation/history reads or wrong-directory calls. Repairs used
separate anonymous checkouts; shared integration outputs provided contention.
This does not exercise competing production-source edits, shared Git mutations,
real interrupted writers, other vendors' harnesses or distributed filesystems.

## Coordination evidence

| Measure | Sixth run | Seventh run |
| --- | --- | --- |
| Observed lost ledger edits | 0 | 0; seed and all five entries preserved |
| Unchanged Updated on sampled claim changes | 0 / 12 | 0 / 15 |
| Actual releasing owner acknowledged | All five | All five |
| B's free-scope wrong reply ID | Acquired without correction loop | Same |
| Old terminal retained claim | Preserved; designated recovery | Same |
| Bounded reader used, self-report | 5 / 5 | 5 / 5 |
| Transition checker used, self-report | 0 / 5 | 4 / 5 |
| Independent web tools used, self-report | 5 / 5 | 3 / 5 |

The evaluator narrowed a covering directory claim while retaining a guard file.
A relay acquired/released the ledger and B's separate output. Another owner then
acquired/released the ledger without changing its bytes. Workers received the
obsolete relay notice. B traced and acknowledged the actual unchanged-content
owner. It also acquired its separately free output despite a mismatched reply ID,
recorded the mismatch and preserved the header.

The first ledger chain was **B → C → A**. A reported a locked acquisition attempt
correctly aborting when C had acquired after B; it acknowledged C's release rather
than using B's older notice. Only after A released did the evaluator register a
synthetic 61-day-old terminal record retaining the ledger. Both D/E preserved
that claim, requested designated recovery and continued independent work. Recovery
used preserved fixture-construction evidence that no worker or jobs ever existed;
age and terminal status were not permission to reclaim. E acquired the recorded
recovery release. D subsequently followed E's intervening release, including a
second reply-ID mismatch without a correction loop. The final chain was
**A → retained fixture → E → D**. C resolved the supplied board-path alias.

The guard was byte-identical; B's separate output retained its header and one
entry. All worker, fixture and reviewer records ended terminal with empty claims.
Candidate hashes, guidance overlays and indexes remained unchanged after capture;
C's new untracked helper was separately captured and hashed. Whitespace checks
passed. No historical repair was integrated into the main checkout.

The observer stopped normally after 3,609 scans, recording **149 events and 41
output changes**. All five observed ledger versions preserved their preceding
prefix; no output sample had multiple claimants. B's write was first observed
after its release and appeared ownerless in that sample. This is not proof of an
unclaimed write: registry and output observations are not an atomic interception.
Generated trees and external temporary outputs were not exhaustively traced.

Observed ledger holds were approximately B 42, C 36, A 55, E 39 and D 136 seconds,
compared with approximately 18–38 seconds in the sixth run. D attributed its long
hold to append verification, evidence/note finalization and a failed closure-check
retry; it reported no tests or source edits during that hold. It should have
released the ledger before finishing unrelated session closure. Different
interleavings and unmeasured agent/tool latency prevent a controlled efficiency
comparison; these results do not establish lower coordination overhead.

## Repair and research results

| Case | Worker validation | Independent evaluation |
| --- | --- | --- |
| A | 29 focused tests; build-tool 8/8 | Four original SDK scenarios match the historical fix; three publication probes pass. An additional explicitly requested multiple-binary acceptance case passes. |
| B | 24 focused tests; build-tool 12/12 | 22 unchanged historical tests pass. |
| C | 43 release + 29 SDK + 23 certification tests; build-tool 10/10 | Five public download/cleanup probes and 92 adapted historical tests pass; no exclusions. |
| D | 67 release + 45 certification tests; build-tool 13/13 | 43 unchanged certification tests and 69 adapted release tests pass. |
| E | 14 focused tests; build-tool 22/22 | 14 tests pass under an aliased temporary root. Reverting only the identity comparison in an evaluator copy makes both alias regressions fail while different-directory rejection passes. |

Configured CTest success does not imply every optional assertion ran. B/C/D each
retained three internal source-SDK skips. B/C identified missing `patchelf`; D's
nonverbose final log records the count without individual reasons. E retained
five tool/platform-dependent skips: two native pacman and three host C++ cases.
There was no actual Windows, live publication, device or full application-runtime
qualification.

D's initial APT setup executed zero assertions; E initially passed 18 of 22 CTest
entries with four GPG-dependent failures. Their local probes identified forbidden
socket binding, rather than assuming that pathname length explained the failure.
Permission-reviewed socket-enabled reruns completed their full configured groups;
initial failures and optional skips remain in the evidence.

Raw historical failures were retained. A's 26-test run has five private-interface
errors and an unrelated experiment-note assertion. Its public probes independently
exercise publication and pairing. The extra multiple-binary case was added to
check the already-declared contract; the original historical fix rejects it, so
it is not counted as another historical-fix match or a comparable prior-run score.
C's raw suites have mock/private-API errors. Its independent reviewer copied tests,
mapped the moved helper to the actual candidate implementation, passed binary
stdout through mocks, supplied the REST `tag_name`, and accepted equivalent header
spacing, overwrite exception type and rejection diagnostics. Pagination mocks
remained active and release pagination was strengthened. All 43 + 27 + 22 checks
passed without excluded tests or candidate edits. D mapped a private selector and
two equivalent rejection messages; one unrelated CLI diagnostic test requiring a
later `release.os` import remained excluded, as in the previous run.

A/B/C independently used web tools and recorded primary sources with local
applicability: GitHub REST/CLI for A/C and Microsoft naming, CPython and Git
attributes for B. **D/E explicitly reported no web research or external sources**;
their repairs used local producers, consumers and executable tests. The reused
prompt says workers “may independently research” and therefore failed to make the
user's requested research demonstration an unambiguous requirement. This is an
evaluation-prompt limitation, not evidence that coordination caused the omission.
No replacement or coached research follow-up was substituted into the score.

Workers said ownership waiting affected final reporting rather than coding/tests.
A/B/C estimated roughly 10–15 coordination-bearing calls and several minutes;
E estimated several startup minutes plus about four minutes in final reporting.
D estimated about 40 filesystem calls overall, which is not a coordination-only
count. These are uninstrumented self-reports. There was no matched no-coordination
control or complete read/token trace; equal research effectiveness and negligible
distraction are not established.

## Improvements supported by this run

1. **Provide a complete atomic-publication example.** B/C/E reported initial
   outbound requests written with exclusive creation but without atomic complete
   publication. That prevents replacement, yet lets a reader see incomplete
   content. C also initially wrote its record directly under the mutex. Show
   recipient-directory creation, complete temporary writes, immutable no-replace
   publication for messages, and atomic record replacement. No lost message was
   observed; the reported protocol shortcoming still matters.
2. **Keep unvalidated record candidates out of the scanned session directory.**
   All four checker users hit the requirement for literal `Running jobs: none`
   at closure. Failed or transient sibling candidates made scans incomplete:
   C encountered B's temporary record; D's leftover candidate affected E; E also
   moved its own failed candidate into claimed artifacts. Prefer preparation and
   validation in already-claimed artifacts, then short atomic publication. Give
   actionable field diagnostics and a filled terminal example. Do not solve this
   by silently ignoring unknown registry entries or treating incomplete scans as
   permission to acquire.
3. **Separate shared-file release from unrelated closure work.** D's 136-second
   hold joined ledger release to evidence finalization and record-check retries.
   The guide already says to append, verify and release promptly; make that an
   easy concrete operation, with unrelated closure afterward. More handoff phases
   would not address this failure to follow an existing short path.
4. **Reduce repeated reads and reconcile current status.** Every worker reported
   truncated initial combined reads and targeted rereads; several found repeated
   scan output substantial. Show bounded startup commands and consider a more
   compact routine display that still preserves complete claims and unresolved
   ownership. Observed records also retained pending/failed test descriptions
   beside successful current job results, or “no changes” after edits. A complete
   checkpoint/closure update should replace these fields together.
5. **Make the research criterion explicit in future exercises.** Require and
   score actual independent primary-source research when evaluating that ability;
   retain omissions rather than accepting plausible source links or a successful
   local repair as a substitute. Ordinary guidance should help agents preserve
   the user's full requested deliverables, without imposing ceremonial browsing
   on unrelated tasks.

These findings favor clearer executable examples and smaller reliable updates,
not another ownership phase. Further evaluation should measure these aids against
the same repair and complete-claim criteria, with research explicitly required
and read/publication overhead instrumented where possible.

Detailed inventories, frozen prompts, raw/adapted tests, candidate hashes, observer
records and surveys are retained in the ignored
`.agent-work/artifacts/coord-rerun7-20260927/` under the normal lifecycle rules.
This report is the only new main-tree deliverable; guidance and runtime behavior
remain unchanged.

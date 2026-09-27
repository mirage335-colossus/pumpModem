# Fourth coordination rerun, 2026-09-27

This evaluates the revised [coordination guide](agent-coordination.md) at
`c2ab00ce525f7a6179f8693b9fc693ee0ea0ff88`, following the
[third rerun](agent-coordination-rerun3.md). Guidance stays fixed during the trial.
Production source remains unchanged; all repair candidates are isolated.

Observed checkpoint consistency improved: **2 of 15 sampled claim transitions**
retained an unchanged `Updated` field, compared with **7 of 15** in the previous
run. All ledger content was preserved, with **zero extra protocol reminders**.
This supports a narrower improvement in record maintenance, not a demonstrated
reduction in collision rate: both runs already observed zero lost edits.

## Preregistered design

Five fresh agents receive anonymous single-commit snapshots preceding five
different historical fixes: A `0afc74b`, B `ba80ff6`, C `1719955`, D `53c54c3`,
and E `0a2a738`. These identities, fixed code and private historical tests are
withheld from workers. Historical tools/tests are byte-verified; current AGENTS
and coordination/lifecycle guidance are overlaid. Workers receive the same
behavioral tasks, independent authoritative web-research requirement, local/mock
validation scope and output-location reporting cue as the previous run. A/B/C task
wording is restated from that scope rather than byte-identical to the earlier
prompts; this is an additional limit on causal comparisons. Three
worker slots require overlapping waves. Blinding is instructed, not enforced
through filesystem permissions.

Repeat the covering-directory, retained sibling, ownership relay, incorrect
reply-ID, board-path alias and synthetic old terminal retained-claim challenges.
Try delivering B's wrong-ID release before other relay release notices so a free
ownership window can be observed; record the actual interleaving rather than
assuming it. A release notice alone never grants access. The terminal fixture
has no real worker or jobs and can only test preservation/recovery decisions,
not recovery of a genuinely resumable cross-host process.

Assess preserved ledger prefixes, claims at sampled writes, checkpoint and
blocker consistency across claim changes, explicit superseded requests,
acknowledgments, note/output ownership, independent research and repair correctness.
Count extra protocol reminders separately from ordinary handoff messages.
Do not send held-out failures or solutions back to workers. Use the existing
bounded periodic sampler, not a write interceptor; retain raw historical failures
separately from documented test-interface adaptations and public behavior probes.

Evidence lives in ignored `.agent-work/artifacts/coord-rerun4-20260927/`, with
workers' board records and private evaluator material. No total token/read cost,
matched no-coordination control or cross-vendor execution is measured. Existing
zero-loss runs mean another zero cannot establish a reduced collision rate.

## Observations

All first-wave agents requested the covering-directory handoff, then independently
rechecked claims after the evaluator's old-owner notices and requested the relay.
All three explicitly marked their initial request superseded by the relay request
in their current handoff field. No source hints or extra protocol reminders were
sent. The wrong-ID challenge follows this ownership transition; its actual result
is evaluated separately from the existence of a release notice.

A acquired from the relay's recorded release before its delayed release notice,
appended, and released. B then encountered A and later C as current owners and
sent superseding requests rather than writing through stale notices. Delivering
B's wrong-ID notice first did not establish a free-file observation: A intervened
before B's acquisition attempt. B did not request an ID correction. The free-file
wrong-ID branch remains a coverage gap, not a scored success.

C acquired after A's append/release, but its acknowledgment named the older relay
as the releasing owner. Its current claims were exclusive and it preserved A's
entry. This is an acknowledgment-provenance defect, not evidence of a lost edit.
It illustrates how several owners can use a file between an earlier release
notice and a later acquisition. The revised wording alone did not prevent it.

D and E preserved the synthetic old terminal claim, read lifecycle guidance and
requested recovery while working independently. C also flagged the record during
closure. The evaluator preserved its initial snapshot, recorded that no worker or
jobs ever existed, then explicitly released it with actual closure metadata.
There was no age-based reclamation, process killing or deletion of unknown work.

## Comparison and evidence limits

| Measure | Third rerun | Fourth rerun |
| --- | --- | --- |
| Observed lost ledger edits | 0 | 0; all five results plus seed/header preserved |
| Extra protocol reminders | 0 | 0 |
| Sampled claim transitions retaining old `Updated` | 7 / 15 | 2 / 15 |
| Superseded handoff records | Reroutes worked; some obsolete pending text remained | All first-wave agents explicitly recorded supersession; B and D also recorded later owners |
| Temporary-note claims | All five | All five, visible at sampled note writes |
| Temporary/output ownership | All five reported claimed outputs | All five reported claimed outputs; D/E short `/tmp` paths claimed |
| Retained terminal claim and board alias | Preserved / resolved | Preserved / C resolved the physical board path |
| Repair completeness | Certificate key mismatch; E broad GPG failures unresolved | Same certificate key mismatch; E GPG failures diagnosed and reruns passed |

The final timestamp comparison normalizes explicit held resource paths, excluding
one previous terminal-record wording change. Earlier progress commentary counted
16 raw claim-section changes for that run; 15 changed actual held resources.
Both remaining misses here concern new temporary paths; E also acquired the
ledger in the same stale-checkpoint update. Its saved `Updated` was about 100
seconds old; D's was about 46 seconds old. A later fresh checkpoint does not make
the earlier transaction atomic. C also retained a nearly due next-check time
while acquiring; refreshed `Updated` alone cannot prove full state consistency.

The observer ran normally from 07:14:41 to 07:34:11 UTC, recording 142 change events,
47 persistent output changes and six ledger versions. Every version preserved the
prior prefix. No sampled output had multiple claimants; three ledger versions were
sampled after the owner had already released its claim. Messages/records document
the handoffs, but sampling cannot prove atomic write-time ownership or catch every
short-lived claim. All sampled note writes had visible claims. External temporary paths
and generated artifact trees are outside the sampler; worker reports supplement
that evidence. No worker reported an unclaimed write.

All five candidates retained the exact captured patch and changed-file hashes,
clean indexes, no untracked files and clean whitespace checks through closure.
Workers and fixtures released all claims. Reported millisecond coordination spans
measure selected operations only; no total overhead, reading cost or elapsed-time
improvement is claimed. The differently timed handoffs, restated A/B/C prompts,
small sample and same-harness environment prevent causal or cross-vendor claims.

## Repair and research evaluation

| Case | Worker-reported local checks | Independent evaluation |
| --- | --- | --- |
| A | 112 passed, 3 host-runtime prerequisite skips | Four SDK recipe scenarios match historical behavior; three publication probes pass. |
| B | 192 passed, no skips | All 22 unchanged historical tests pass. |
| C | 164 passed, 3 prerequisite skips | Five public download probes and 87 explicitly adapted high-level historical checks pass. |
| D | 241 passed, 3 prerequisite skips in the final tooling run; 7 separate APT tests passed after permitted local socket access | 69 adapted release checks pass; 43 unchanged certification tests retain one missing `tag_sha` error. |
| E | 436 passed, 6 prerequisite/platform skips across 18 suites; initial GPG failures resolved | Candidate fixture suite under a symlinked temporary root: 13 passed, 1 Windows-only skip; different-directory rejection retained. |

Raw historical failures remain separate: A has private helper/REST fixture
mismatches and an unrelated experiment-note assertion; C uses a differently named
helper and an open-stream transport interface. C's adaptations preserve real
candidate identity/hash/cleanup behavior, accept equivalent header flags and
remove five tests bound to an earlier private signature. Complete REST fixtures
and equivalent diagnostics follow the prior run's documented adaptations. The
first adapted release/SDK runs still rejected the equivalent `--header` flag;
those harness-only failures and the corrected runs are retained. No candidate
was changed in response to evaluator checks, and no answers were sent back.

Local mocks and Linux fixture runs do not establish live GitHub behavior or real
Windows qualification. Repair checks remain separate from coordination scores.

D exposes application source, packaging source and repackage origin, and validates
the tag against the packaging commit. It still does not emit a separate `tag_sha`
certificate key. This is the same exact historical-output-contract mismatch as
before, not evidence of an unsafe release or collision. The behavioral prompt did
not specify that key; future exercises should supply externally observable schema
requirements without supplying implementation answers. Private helper names and
exact diagnostic wording likewise require adapters, not repair-failure scores.
D's adapter changes only its private selector name and equivalent diagnostic,
and excludes the same unrelated CLI sanitizer test as the earlier run.

All five agents recorded independently researched primary sources with local
evidence and limitations: GitHub REST/CLI for A/C/D, Python ZIP implementation and
Git attributes for B, and Python filesystem identity plus Microsoft path naming
for E. D and E investigated GPG startup failures and completed permitted local
socket-enabled reruns. E's corresponding failures remained unresolved in the
preceding run. This is observed diagnostic progress, not a controlled attribution
to one documentation sentence. No search-quality rubric or token/read measurement
establishes equal effectiveness or reduced distraction.

## Remaining improvement opportunities

- **Prefer a small record-update aid over more reminders.** The new transaction
  wording improved observed timestamp refreshes, but D/E still separately added
  temporary-path claims with stale checkpoints. An optional, tested updater or
  read-only consistency check could catch that mechanical mistake while leaving
  progress/inbox times tied to actual events. It must never reclaim ownership,
  hide incomplete claims, or make another harness mandatory.
- **Reconcile the releasing owner when notices cross several transfers.** C's
  acknowledgment went to an older owner after A had written and released the file.
  A compact release receipt identifying owner, scope and file baseline could help
  distinguish an earlier notice from the handoff actually consumed. Exercise this
  before adopting it; preserve pending content integrations separately and avoid
  turning superseded requests into needless acknowledgment loops.
- **Acquire shared summary files when ready to append.** C and E held the ledger
  during further checks while another completed agent waited. No edits were lost,
  but this serializes unrelated work. A short reminder limited to shared summary
  files, or the existing per-session-result/one-integrator approach, is preferable
  to weakening claims on source or build inputs.

The current alias, old-notice and retained-terminal-claim safety rules worked in
the tested cases and need no extra policy paragraphs on this evidence. Important
untested cases remain: a genuinely free file with a wrong reply ID, malformed
registries, actual interrupted child processes, simultaneous source edits and
shared Git operations across different harnesses. These are coverage gaps, not
observed failures. The guide was not edited during this exercise.

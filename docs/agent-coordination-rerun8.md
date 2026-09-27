# Eighth coordination rerun, 2026-09-27

Five fresh repair workers preserved the seed and all five shared ledger entries,
with **zero observed lost edits, zero sampled overlapping output claims, and
0 of 13 sampled claim changes retaining an unchanged Updated field**. All five
performed independent primary-source web lookups and passed their normal configured
build-tool groups, with the omissions and evaluation adaptations below.

The revised guidance showed useful practical improvement over the
[sixth](agent-coordination-rerun6.md) and [seventh](agent-coordination-rerun7.md)
runs: all workers used the atomic publisher, record candidates stayed outside the
registry, coordination startup reads were untruncated, and all released the ledger
before preparing separate final evidence. The earlier runs also had zero observed
lost edits. This demonstrates better protocol execution in this exercise, **not a
measured reduction in collision probability or proof of negligible distraction**.

## Design and comparability

The main baseline was `296eddfb8d2e3631c630d01b2f0df69fa8ccb5ce`, containing the
revised AGENTS/workflow/lifecycle guidance, filled recipes, compact reader/checker
and new atomic publisher. Exact guidance, helper and dispatched-prompt hashes were
frozen before work began. No guidance changed during this study.

Five fresh conversations received the same historical behavioral problems as runs
6/7: A `0afc74b` (publication races and SDK pairing), B `ba80ff6` (Windows archive
and recipe portability), C `1719955` (paginated asset inventories and downloads),
D `53c54c3` (repackaging provenance), and E `0a2a738` (fixture directory identity).
They received no solution commit, patch, private helper name or held-out test.
Complete parent trees, including tracked ignored vendor inputs, were verified
against Git paths, modes and blobs: 3,830 / 3,856 / 3,840 / 3,879 / 3,914 entries.
Guidance overlays were separately recorded. All configuration preflights passed.

Unlike the previous permissive prompt, each worker was explicitly required to
actually use web tools and record primary-source applicability. Remote reads were
allowed; remote mutations remained forbidden. Workers received containing-repository
ignore proof, access to a compact environment note, and an explicit requirement to
produce independent final evidence after the ledger edit. These changes, together
with new helper availability and different interleavings, prevent attributing every
improvement to documentation alone.

There were five repair dispatches, no excluded replacements, and overlapping waves
of at most three repair workers: A/B/C first, D after B, E after A. Read-only study
reviewers and D's read-only candidate reviewer were excluded from worker metrics.
There were no extra collision-protocol reminders or held-out repair hints. Normal
handoff/recovery notices and final delivery receipts were sent. Post-run surveys
were read-only; finished candidates remained frozen.

Blinding was instruction-based, not filesystem-enforced. No worker reported reading
another repair implementation, parent history or its historical answer. E disclosed
an unnecessarily broad startup note-metadata search that printed sibling titles,
affected paths, status and brief results; it reported no E repair answer exposure.
This is a remaining information-boundary limitation, not verified perfect blinding.
Repairs were in isolated anonymous checkouts; contention concerned shared integration
files. Real shared-source edits, Git mutations, interrupted real jobs, other vendors'
harnesses and distributed filesystems were not exercised.

## Coordination results

| Measure | Sixth run | Seventh run | Eighth run |
| --- | --- | --- | --- |
| Observed lost ledger edits | 0 | 0 | 0 |
| Unchanged Updated on sampled claim transitions | 0/12 | 0/15 | 0/13 |
| Actual releasing owner acknowledged | All five | All five | All five |
| Retained terminal claim respected | Yes | Yes | Yes |
| Bounded reader used, self-report | 5/5 | 5/5 | 5/5 |
| Transition checker used, self-report | 0/5 | 4/5 | 5/5 |
| Atomic publication helper used, self-report | Unavailable | Unavailable | 5/5 |
| Independent web tools used, self-report | 5/5 | 3/5 | 5/5; now explicitly required |

The evaluator first narrowed a covering directory claim while keeping the guard.
A relay acquired/released the ledger; another owner acquired/released it without
changing bytes. Workers received the obsolete relay notice. B followed the actual
`quiet-owner` release. For B's separate output, the recorded release was valid but
the reply ID was obsolete; B acknowledged the mismatch and acquired without an
unnecessary clarification loop.

The ledger chain was **quiet-owner → B → A → C → retained fixture → E → D**.
A and C each stopped a stale-baseline attempt before acquisition/writing and
reconciled the intervening owner. After C released, the evaluator created a
synthetic 61-day-old terminal record retaining the ledger. Both D/E preserved it,
requested designated recovery and continued repair work; C also flagged it after
completion. Recovery relied on positive fixture-construction evidence that no
worker/jobs had ever existed, not its age or terminal label. E acquired the recovery
release. D stopped on E's changed bytes, superseded its recovery request, and acquired
from E's actual release. C resolved the supplied board-path alias.

The observer stopped normally after **3,613 scans, 123 events and 42 output changes**.
Every observed ledger version preserved its preceding prefix. No output sample had
multiple claimants, unparsed claims or unknown attribution. Four ledger writes were
first observed against an ownerless registry snapshot; E's was seen with its owner.
These are non-atomic samples, not evidence of four unclaimed writes. Brief registry
transitions can fall between samples. Generated build and external temporary outputs
were not exhaustively traced.

Only B and A's ledger ownership spans were captured at both ends: approximately
**0.46 and 0.55 seconds**, compared with 18–38-second sampled spans in run 6 and
36–136 seconds in run 7. C/E/D claim spans were not captured at both ends;
their exact durations are unknown, not zero. Records, release notices and final-evidence
creation order corroborate that all five released before independent reporting.
In particular, D no longer joined its ledger release to unrelated closure work.
This supports shorter shared-file holds, not a claim about total coordination time.

No worker reported direct/incomplete message publication or record candidates in
`sessions/`; the observer saw no temporary record candidates under `sessions/` or rewritten message. All used
the publisher for records and messages. Independent primitive checks also passed:
**26 publisher tests and 36 record-reader/checker tests**, including concurrent
publication, complete-message visibility, stale replacement and injected failures.
Sampling alone cannot prove every publication was atomic; these tests qualify the
local implementation, not every filesystem or harness.

## Repair, research and validation

| Case | Worker validation | Independent frozen-candidate evaluation |
| --- | --- | --- |
| A | 40 focused tests; normal build-tool 8/8 | Matching/mismatching/orphan/extra SDK scenarios behave correctly; explicitly requested multiple-binary case passes; three adapted public publication probes pass. |
| B | 25 focused tests; normal build-tool 12/12 | 22 unchanged historical tests pass. |
| C | 44 release + 30 SDK + 22 certification tests; normal build-tool 10/10 | Five public download/cleanup probes and 87 adapted high-level historical tests pass; empty-leading-page variants also pass. |
| D | 67 release + 44 certification tests; normal build-tool 13/13 | 69 adapted release and 43 adapted certification tests pass, including schema-3 provenance assertions. |
| E | 14 focused tests; normal build-tool 22/22 | All 14 pass under an aliased temporary root; independent generated-fixture probes accept the same directory alias and reject a different directory. |

Every normal group retained three internal source-SDK ELF skips; E additionally
retained two native pacman skips. `patchelf` and native pacman were unavailable.
CTest's passing group counts do not turn these optional omissions into executed
assertions. D initially passed 12/13 entries, with APT setup executing zero
assertions; E initially passed 18/22, with four GPG-dependent setup failures.
Short AF_UNIX probes established sandbox `EPERM`. Approved local socket-enabled
reruns completed their entire configured groups. Initial failures remain recorded.
No native Windows, live GitHub, physical-device, full application-runtime or release
certification qualification is claimed.

Baseline/fixed controls for A–D failed/passed before dispatch. E's ordinary copied
suite passed both variants and is explicitly nondiscriminating. Its own-revision
aliased-root control failed before/passed after; direct alias/different-directory
controls were checked before E dispatch. Reverting only the generated fixture's
identity comparison in an evaluator copy makes the candidate alias probe fail
while retaining different-directory rejection. Candidate sources were never edited
in response to held-out results.

Raw historical failures were preserved and kept separate from behavior scoring:

- A's 26-test raw run retained five private-interface errors and one unrelated
  experiment-note failure. Three public publication checks initially had two mock
  failures because the candidate uses `gh api --input` plus `--raw-field name=`.
  The [official CLI manual](https://cli.github.com/manual/gh_api), consulted by the
  evaluator on 2026-09-27, confirms these fields become query parameters with
  `--input`. The adapter accepts that encoding, still checks exact uploaded bytes,
  captured draft identity, race rejection and publish ordering. The multiple-binary
  SDK requirement is inherited from the task prompt; the original historical fix
  rejects that extension, so its success is not another historical-fix match.
  Review found the initial independent publication probes narrower than the worker's
  tests. Five supplemental checks against the unchanged candidate now pass, covering
  release-create races, full upload inventory, default draft retention, experiment/Latest
  fields and failure after two successful uploads. These are post-freeze additions
  covering the unchanged declared contract, not preregistered comparative evidence.
- C's raw release/SDK/certification suites failed on mocked binary stdout routing,
  REST fixture fields and private downloader assumptions. Adapters route binary
  writes through the real candidate helper, supply REST `tag_name`, and accept
  equivalent Accept-header spacing and rejection wording. Five private-helper
  tests are excluded, explicitly, leaving 38 + 27 + 22 high-level checks; five
  separate public probes cover binary bytes, digest validation, interruption,
  corruption cleanup and overwrite preservation. Certification's real digest
  checks remain active. An initial adapter omitted a second mock's `tag_name` and
  was corrected; no candidate change occurred. Counts are not directly comparable
  to run 7's differently adapted 92-test suite.
- D's raw 70 release tests had two private-interface errors and two diagnostic
  mismatches; its 43 certification tests had two diagnostic mismatches. Adapters
  map the private tag selector and equivalent rejection text. One unrelated later
  CLI-diagnostic test requiring a missing `release.os` import remains excluded,
  as in prior runs. Public provenance values and validation gates stay asserted.

All five reported actual independent web-tool use and retained dated local-applicability
notes: GitHub REST/CLI for A/C, Microsoft/Python/CPython/Git attributes for B,
GitHub refs/releases and Git tag resolution for D, Python directory identity for E.
E opened primary pages and used find; it did not use a search-engine query. These
are worker-visible call reconstructions and notes, not an evaluator-captured complete
web trace. No coached after-the-fact lookup was substituted for a missing attempt.

## Remaining friction and proportionate improvements

Observed practice improved on the specific publication, registry-candidate
and long-held-summary failures from runs 6/7. Remaining weaknesses concern truthful
current status and bounded supporting work:

1. **Make event-based checkpoints easier to execute.** A/B/C/D reported `Running jobs:
   none` while short normal builds were actually running. E retained a completed
   build as running and temporarily kept an obsolete retained-claim blocker after
   reading recovery. B retained old “awaiting” text beside a completed handoff;
   D's release snapshot still named appending as its next action. D also initially reported no skips, then corrected the three internal
   SDK omissions before closure. Terminal records reconciled ownership, but intermediate status can misdirect another agent.
   A's acquisition record also carried an Updated time about 34 seconds before
   publication; 0/13 unchanged values does not establish exact timestamp freshness.
   The guide already requires consistent updates. A compact launch/completion and
   handoff example that replaces current fields together would be more useful than
   adding another mandatory reporting phase. Helpers cannot infer factual truth.
2. **Distinguish inbox enumeration from message processing.** B/C/E explicitly
   reported initial inbox timestamps without a preceding inbox read; B enumerated
   three new messages before closure but read their bodies afterward. A had a
   harmless missing-inbox error and approximate checkpoint-based inbox timestamps.
   A small missing-directory-safe inbox recipe should return a timestamp after
   reading relevant new messages. An empty/missing inbox is a valid observation;
   assigning “now” without the check is not. This does not justify waiting for
   future messages after a completed release.
3. **Keep source/research and note reads bounded too.** Coordination startup reads
   were untruncated for all five, but A/B/C/D reported later combined source/build/web
   truncation, and E truncated the build guide. Thus every worker still reported
   some non-coordination output truncation. E's broad note-metadata output was
   wider than its task required. Use path/topic-filtered note discovery and bounded
   source sections, reopening omitted material needed for a decision. Avoid adding
   wholesale board reads or mandatory repeated documentation passes.
4. **Preserve minimal failure evidence without increasing chatter.** A safely
   rejected an unchanged-Updated release proposal; source-hash checks stopped A/C/D
   before stale acquisitions. Those are successful guards, not collisions. C's
   initial failed SDK test log was overwritten by the passing rerun. Keep a compact
   failure summary or distinct relevant log when it explains a fixture correction;
   do not retain every transient output indefinitely.

There were no reported closure-format retries comparable to run 7's four checker
users failing on the literal `Running jobs: none` requirement. Overall coordination
call counts/time were not consistently instrumented. A reported 10 compact scans,
7 targeted inspections, 6 record and 4 message publications; B reported 11/5/7/6;
E reported 8/3/6/7. These are self-reports, not a normalized overhead benchmark.
A/B/C registration-to-closure elapsed times were about 10m36s / 8m53s / 11m52s and
include useful work. There is no matched no-coordination control or full read/token
trace, so equal research effectiveness and low total distraction remain unproven.

The evaluator also exceeded an output cap once during startup, then reread complete
claims before registration. That is an evaluator reading error, excluded from
worker metrics. Future studies should capture publication/event timings and web
calls directly, keep mocks aligned with public interfaces, and add a matched control
if measuring distraction. These study improvements belong in evaluator tooling,
not more routine worker policy.

Detailed inventories, prompts, controls, raw/adapted tests, candidate hashes,
observer records and surveys are retained under the ignored
`.agent-work/artifacts/coord-rerun8-20260927/` with the normal 30-day lifecycle.
All worker and synthetic-owner claims were released; the guard and B's separate
header were preserved. The main index and historical candidate hashes were unchanged
after capture. This report is the only main-tree change; no historical repair was
integrated and guidance/runtime behavior remained unchanged.

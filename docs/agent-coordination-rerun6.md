# Sixth coordination rerun, 2026-09-27

The revised [coordination workflow](agent-coordination.md) held up in another
five-worker exercise. **No lost ledger edits were observed**, and **0 of 12 sampled
worker claim transitions** retained a stale `Updated`. The [fifth rerun](agent-coordination-rerun5.md)
also observed zero lost edits and zero stale timestamps (16 sampled transitions),
so this does **not** demonstrate a further reduction in collision rate.

Practical outcomes improved: all five workers used the new bounded reader, no
replacement worker was needed for disclosed implementation exposure, and every
historical build-tool group eventually passed. D and E diagnosed socket restrictions
and completed successful reruns instead of leaving GPG failures unexplained.
Formatting and fallback-reading friction remains; additional ownership ceremony
is not supported by this run.

## Preparation and comparison limits

Guidance and the reader remained fixed at
`e59697a` throughout. Five fresh workers received anonymous pre-fix snapshots for
A `0afc74b`, B `ba80ff6`, C `1719955`, D `53c54c3`, and E `0a2a738`.
Workers received behavioral tasks, not solution commits, implementation patches
or held-out tests. They independently researched primary sources, repaired their
cases, ran tests and delivered results through the shared board.

Each entire historical parent tree was reconstructed and verified against Git:
3,830 / 3,856 / 3,840 / 3,879 / 3,914 tracked entries for A–E, including vendor files,
file modes and symlink targets. No submodules or unresolved LFS pointers were
encountered. Current AGENTS, workflow, lifecycle and reader overlays were recorded
separately. Anonymous commit inventories were checked, and all five CMake CLI/test
configuration preflights passed. This corrects the previous incomplete-snapshot
problem; it also changes the validation conditions, so improved build outcomes
cannot be credited solely to the guidance.

Tasks, criteria and source/test hashes were recorded before dispatch. Task wording
was refreshed, with D now explicitly requiring all certificate provenance fields,
including `repackaged_from` when present in metadata. A's requested publication
safety includes binding operations to the draft actually created. Those explicit
contracts and the changed interleaving limit comparisons with earlier runs.

There were five repair dispatches and no excluded replacements. Three available
worker slots produced overlapping waves, not five simultaneous workers. D began
while C was finishing; the retained-claim fixture was registered only after C's
ledger release. A separate evaluator later checked frozen C behavior and was
excluded from repair-worker metrics. No repair answers or evaluator-discovered
failures were sent back to workers. Normal handoff/recovery/delivery replies were
sent; there were **zero extra collision-protocol reminders**.

Blinding was instruction-based, not a filesystem security boundary. Workers
reported no parent/history/peer implementation reads or wrong-directory calls.
Their source repairs occupied separate historical checkouts; only the integration
outputs were shared. This does not test competing edits to one production source
file, shared Git mutations, real interrupted jobs or other vendors' harnesses.

## Coordination results

| Measure | Fifth rerun | Sixth rerun |
| --- | --- | --- |
| Observed lost ledger edits | 0 | 0; seed and all five entries preserved |
| Stale `Updated` on sampled claim changes | 0 / 16 | 0 / 12 |
| Actual releasing owner acknowledged | All five completed workers | All five |
| B's wrong reply ID on free exact scope | Acquired without correction loop | Acquired without correction loop |
| Old terminal retained claim | Preserved; designated recovery | D/E preserved it; designated recovery |
| New bounded reader used | Not available | 5 / 5 by post-run self-report |
| Optional transition checker used | 0 / 5 | 0 / 5 |
| Disclosed implementation exposure requiring replacement | 1 excluded attempt | None reported |

The evaluator narrowed its covering directory claim while retaining a guard file.
Relay then acquired/released the ledger and B's separate output; quiet-owner
acquired/released the ledger without changing bytes. Workers received an obsolete
relay notice. B correctly acknowledged quiet-owner, while accepting the mismatched
reply ID for its independently free output. The ledger then passed **B → A → C**.
A and C reported mutex-protected rechecks stopping acquisition when another worker
held it; they rerouted their requests instead of writing.

The evaluator subsequently registered a synthetic 61-day-old terminal record
retaining the ledger. D and E requested designated recovery and continued independent
work. Recovery used recorded positive evidence that this fixture never had a real
worker or jobs, preserved its initial record, and explicitly released ownership.
D acquired that release; E later followed D's intervening release. Neither age,
terminal status nor the old C/recovery notices granted access. The final chain was
**C → retained fixture → D → E**. C also resolved the board's `shared/../board` alias.

The guard remained byte-identical. B's separate output preserved its header and
contains exactly one result entry. All repair workers and the independent reviewer
closed with empty claims. Candidates, indexes and overlaid guidance were checked
after evaluation: patches/hashes unchanged from capture, no staged or untracked
candidate files, and clean whitespace checks. No production repair was integrated.

The sampler recorded **132 events and 42 output changes**, with no sampled multiple
claimants. Observable ledger claim holds were approximately 32 seconds (B), 29 (A),
30 (C), 18 (D) and 38 (E). Entries followed completed validation; no further repair
or test run was observed during these holds. These timings are not a controlled
overhead comparison. C's ledger change was observed before the corresponding
claim appeared in the sampler's registry snapshot; this is **not proof of an
unclaimed write**. Sampling cannot establish atomic ownership for every write,
and generated artifact trees/external temporary outputs were not fully traced.

## Repair and research outcomes

| Case | Worker validation | Independent evaluation |
| --- | --- | --- |
| A | 37 focused tests; build-tool group 8/8 | Four SDK inventory scenarios match the historical fix; three publication probes pass. |
| B | 24 focused tests; build-tool group 12/12 | All 22 unchanged historical tests pass. |
| C | 87 focused tests; build-tool group 10/10 | Five public binary/integrity/cleanup probes and 87 explicitly adapted historical checks pass. |
| D | 67 release + 44 certification tests; build-tool group 13/13 after socket-enabled rerun | 43 unchanged certification tests and 69 adapted release checks pass. |
| E | 14 focused tests; build-tool group 22/22 after socket-enabled rerun | All 14 fixture tests pass under an aliased temporary root; reverting only the identity comparison makes both alias regressions fail while different-directory rejection still passes. |

Configured CTest success is not proof that every optional assertion ran. E retained
five optional skips: two Arch and three distro cases. No actual Windows, live
GitHub publication, device or full application-runtime qualification is claimed.

Original A–D controls fail the held-out suites; their historical fixes pass. E's
original fixture fails under an aliased temporary root and its historical fix
passes. Running the fixed E fixture against both tool versions alone did not
discriminate the bug; the alias and identity-comparison negative controls do.

Raw failures remain separate from adapted results. A's historical 26-test run has
five private-interface/mock errors and an unrelated experiment-note assertion;
its public probes exercise the requested SDK and publication behavior. C's public
probe adapter only normalizes option ordering and header whitespace. Its high-level
suite adaptations bridge mocked binary transport, supply the REST response's
`tag_name`, exercise the actual certification downloader, and accept equivalent
integrity/invalid-ID diagnostics. Five tests coupled to a private downloader API
remain excluded. Multi-page mock responses remain active, including three adjacent
arrays with an empty first page. D maps a private selector name and equivalent
error wording, excluding the same unrelated CLI test requiring a later `release.os`
import. Its unchanged certification checks now pass all provenance assertions,
including the newly explicit field. No finished candidate was edited in response
to evaluator tests.

All five recorded dated primary references and local applicability: GitHub REST/CLI
for A/C/D, Microsoft naming rules and CPython ZIP implementation for B, and Python
filesystem identity documentation for E. Each reported independent web-tool use.
Useful research and repair work therefore occurred alongside coordination. There
was no matched no-coordination control, complete read/token trace or normalized
elapsed-time comparison; equal research effectiveness and negligible distraction
remain unestablished.

D's long/short GPG probes and E's short-path retry did not establish pathname length
as the cause. Direct GPG/socket probes demonstrated `Operation not permitted`/`EPERM`
for socket binding. Approved local socket-enabled reruns passed their complete
configured groups. Initial failures and remaining optional skips were retained;
shortening a path alone was not presented as a successful fix.

## Where guidance and tooling could improve

- **Make valid records easier to produce and errors easier to repair.** C omitted
  heartbeat/token/retention fields, making its initial scan incomplete. A/B manually
  inspected its complete claims; C corrected the record and later scans succeeded.
  A minimal filled template/update aid and diagnostics naming missing known fields
  could avoid this detour. Consider returning a safely extracted complete claim
  section alongside a metadata error while still marking the scan incomplete;
  never turn missing fields into permission to acquire.
- **Provide a bounded fallback and handoff reader.** A/B/C reported extra task,
  progress or hash details during manual reads; C also saw evaluator commentary,
  without repair answers. E read more handoff history than necessary. All five
  used `--scan`, but it did not remove this friction. A targeted section-reading
  example or helper is more promising than repeating “read less.” Claims must
  remain complete. D/E also needed to reread truncated combined startup output.
- **Include recipient-directory creation in message publication examples.** C's
  first send to a missing inbox directory failed, then succeeded after correction.
  This did not lose data, but a small executable example could prevent avoidable
  delivery retries while retaining immutable, atomic publication.
- **Keep current status consistent with completion.** Several workers reported
  briefly stale progress text. A closed while its delivery acknowledgment was still
  pending; receipt arrived afterward without lost work. A compact completion update
  should reconcile jobs, pending receipts and progress together. Do not attribute
  improvements to the unused transition checker or mandate it without measuring
  adoption cost. A/E also reported guessed nonexistent tool paths; discoverability
  should improve through usable entry points, not more required prose.

Current-owner checks, unchanged-byte provenance and explicit recovery worked in
these scenarios. No extra handoff phase is indicated. The most useful next trial
would measure whether the proposed reading/record aids reduce retries and unrelated
reads while preserving the same complete-claim checks and repair coverage.

Detailed protocol, full-tree inventories, prompt/test hashes, raw/adapted logs,
observer events, candidate hashes and post-closure surveys are retained under the
ignored `.agent-work/artifacts/coord-rerun6-20260927/` with normal lifecycle rules.
The evaluator's final report is the only new tracked deliverable.

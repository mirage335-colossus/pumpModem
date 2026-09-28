# Decentralized coordination hardening and validation

Baseline: `1b3c74c9d99c6e0b4400b7ea7576648684183ba4`, 2026-09-28 UTC.
This implements findings from the [shared-source study](agent-coordination-shared-source-study.md)
and the clarification that participants are unrelated chats, potentially using
different harnesses. Each participant independently calls the atomic reservation
protocol. No parent agent, editing scheduler or continuously running controller
is required. Reservations persist after the short registry transaction ends.

## Implemented changes

| Finding | Change and verification |
| --- | --- |
| A directory mislabeled as a file could admit a child-file claimant | Validate actual filesystem kind on each scan/publication. Revalidate new proposals **inside the mutex**, including paths materialized between preliminary validation and lock entry. |
| One explicit scope suppressed uncertainty about other opaque history | `Scope inventory: complete` asserts an exhaustive typed inventory. Unmarked non-neutral history remains uncertain even beside some explicit rows. Malformed, duplicate and contradictory declarations block filtering. Old records remain manually reviewable. |
| Clean contention and uncertain publication looked alike | Structured codes, phases, uncertainty and next actions distinguish busy/stale rejection from publication/cleanup/callback/output uncertainty. Descriptor-close errors and witnessed scan/input races are covered. No generic automatic replay. |
| Unrelated ownership changes invalidated reviews | Scoped fingerprints preserve relevant ownership, input ownership and relevant/uncertain handoffs. Every claim/error is still rescanned under the mutex; own-record hashes remain exact. Narrow/empty reviews retain the global fallback. |
| Stale buffers and in-place source writes | New `tools/agent-edit.py` checks current reservations, exact source/record hashes and source/staging identities, then atomically replaces a bounded file. Each chat can call it independently. Reservations remain held after success or failure. |
| Automatic CI omitted transaction tests | Automatic tooling now runs reader, publisher, transaction, editor and stress suites. CMake/build registration and navigation include the new suites. |
| Limited shared-source evidence | Durable `tests/test_agent_stress.py` runs independent shared/disjoint writers with no turn scheduler, atomic predecessor acknowledgments and exact preservation checks. A separate model-study recipe distinguishes seeded stress from historical/blinded comparisons. |

Independent review found and fixed further authorization gaps. Conservative
case/Unicode overlap must reject possible foreign conflicts, but cannot **grant**
authority over a distinct spelling on a case-sensitive filesystem. The editor now
requires exact canonical own-file/ancestor coverage. It rejects equal-byte staging
inode replacement without consuming the unfamiliar file, rejects board targets
that would bypass record/message publication, and reports malformed CLI content
as structured rejection.

The editor supports existing singly linked regular files up to 8 MiB, ordinary
permission bits, current effective uid/gid and no extended attributes/ACLs. Mode
is preserved; inode identity changes. Staging occurs outside the mutex; bounded
verification/publication occurs inside, without caller commands or test callbacks.
Unsupported metadata fails without a direct-write fallback. Interrupted staging
leaves the original source and reservation intact. Errors after replacement,
flush or cleanup require inspection of saved state.

This is cooperative edit support, not arbitrary process supervision or a security
boundary. Each caller must finish its queued edits and child/output writers before
release. Unrestricted tools can bypass an advisory board. Harnesses can enforce
their own tool permissions or private checkouts without one common controller.
Claims never expire merely because a timestamp or process looks old.

## Regressions and failures found during follow-up

The five coordination suites contain **175 passing tests**: 64 reader, 32 publisher,
53 transaction, 24 editor and two process scenarios. This is 60 additional tests
over the earlier 115-test reader/publisher/transaction total; subcases are not
counted separately.

Coverage includes kind/alias changes, incomplete handoffs, unseen closed owners,
unchanged-byte input ownership, malformed/unknown records, narrow release reviews,
own-record replacement, precondition rejection, uncertain publication/cleanup,
interrupted staging, stale saves, retained claims and metadata restrictions.
Critical interleavings use barriers or injected ordering, not timing guesses.

The initial 16-process run stopped on a legitimate changing-board scan. Added
`SnapshotChanged` classification permits a fresh observation only when **every**
unresolved error is a typed race; concurrent malformed/unreadable entries still
block acquisition. The corrected run passed.

The initial 64-process run exposed loss of a clean busy classification in the edit
adapter. Ownership remained held and the client stopped. Clean busy/stale codes
are now preserved only before attempted replacement and after successful staging
cleanup. Fixture retries verify unchanged saved ownership and source; uncertain
errors remain fatal. All initial failed logs are retained beside successful reruns.

## Independent-process qualification

After a one-time start barrier, every client independently reviewed/reserved its
source, acknowledged its predecessor, guarded its save, verified retained work,
released and closed. No process selected the next winning writer.

| Final backoff workload | Completed | Acquisition attempts | Edit attempts | Release attempts | Elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 disjoint files | 64/64 | 757 | 66 | 434 | 9.845 s |
| One file, 64 writers | 64/64 | 865 | 64 | 64 | 8.771 s |

Acquisition attempts include observations of existing owners, not only rejected
transactions. Every exact contributor/value pair survived. Shared saves formed
one consecutive preimage/postimage chain; all expected predecessor acknowledgments
were present. Every child joined. Complete final scans found exactly 64 closed
records with empty claims in each workload.

An earlier successful short-backoff run took 74.351 s disjoint and 28.446 s shared.
The disjoint run generated 5,458 acquisition, 2,472 edit and 3,950 release attempts.
Bounded exponential jitter reduced the observed polling load. These are single
local observations, not statistical performance or fairness guarantees. The
registry still scans the whole board. These processes are **not 64 AI agents**.
The fixture checks acknowledgment publication before editing; it does not make
already closed predecessors resume to process those receipts.

A later boundary probe used the driver's supported maximum of **128 processes**
on the repository filesystem. The original retry schedule failed the disjoint
scenario at the unchanged 180-second acquisition deadline. The shared-file
scenario passed in 86.962 seconds with 12,825 acquisition observations and 128
saves/releases. This is a real unsuccessful qualification at that load, retained
in `stress128-repository.log`; the earlier 64-process passes do not erase it.
The same-workload diagnostic changed only retry timing to equal-jitter exponential
backoff: 25–50 ms initially, saturated at 2–4 seconds. It passed disjoint in
46.147 seconds and shared in 42.995 seconds. The helper's advice and durable
stress client now use that policy without needing a participant count or
controller. Default/maximum retry counts, the 180-second worker deadline and
every correctness assertion remain unchanged. A subsequent run of the exact
final candidate also passed:

| Final repository-filesystem workload | Completed | Acquisition attempts | Edit attempts | Release attempts | Elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 disjoint files | 128/128 | 1,282 | 135 | 474 | 40.298 s |
| One file, 128 writers | 128/128 | 1,338 | 128 | 128 | 40.306 s |

All final contributions, hash-chain links, acknowledgment fields and closed/empty
claims passed; every child joined. This supports reducing retry traffic under
contention. The timing comparison is still a pair of local observations, not a
causal benchmark or fairness guarantee. The saturated-delay bounds are also
checked deterministically in the transaction suite. These are **128 operating
system processes, not 128 AI workers**.


## Independent model repair loops

Three available model workers each received four related assignments in the same
disposable `tools/release.py`. They used separate sessions and chose acquisition
timing themselves; the evaluator assigned no editing order. A decentralized
readiness barrier required all three initial registrations. Exact dispatch text,
prompts, contracts and hashes were frozen before launch.

The twelve assignments are seeded behavioral regressions, not twelve different
historical commits. Every isolated mutation failed its designated contract and
the clean control passed all twelve before dispatch. Reused conversations and
previously exercised problems make this an open-book coordination study, not a
blinded repair-quality comparison.

The complete baseline has 3,945 tracked entries, verified against Git blob
contents/modes, with no hard links or shared Git state. Candidate helpers/guidance
and public contracts are explicit overlays. Original-source write-open was denied
with `EROFS` through the required `bwrap` commands. This is per-command protection;
the harness did not revoke every alternate write capability. All source saves use
the guarded editor rather than the previous study's in-place save controller.

All twelve repairs completed across thirteen ownership cycles: four saves per
worker plus one failed cycle with no save. The actual successful save order was
`alpha01 → gamma03 → alpha06 → alpha09 → beta02 → beta04 → alpha11 → gamma05b →
beta08 → beta10 → gamma07 → gamma12`. Reconstructed receipt/snapshot hashes form
one uninterrupted source chain. Replaying all twelve contracts on the seeded
state and after every save produced **156 contract evaluations**, with passing
counts increasing exactly from zero to twelve and no regression of an earlier
repair. All **81 existing release-tool tests** also passed on the final disposable
source. Its only difference from the clean control is one condition's formatting.

Final exercise source SHA256:
`86038abcb75c3a0352f4c72e13a37d49ff695f9218add736dc39f2ddb3808f28`.
The seed record and all three worker records are closed with empty claims; every
frozen helper/guidance/contract overlay retained its exact dispatch hash. A stable
final source snapshot was captured after checking record identities, closed states
and source identity before and after the read.

Repair success did **not** mean perfect protocol compliance:

- Gamma's first task05 attempt failed a nonunique patch precondition before save.
  The worker incorrectly published a prewritten passing release summary, then
  corrected it about 25.6 seconds later. No edit receipt or after-test exists for
  that attempt; task05b is the successful repair. The intervening same-byte release
  also demonstrated why source hashes alone cannot identify the last owner.
- Alpha's four atomic acknowledgment messages had matching recipients/hashes but
  omitted exact release references, absolute scopes and request correlation.
  All thirteen acquisition messages exist and inspected client code orders
  acquisition, acknowledgment and edit. That supports ordering, not complete
  acknowledgment compliance; file timestamps alone are not an execution trace.
- Five clean stale rejections (two Alpha preparations, two Gamma source
  preconditions and one Gamma review) and two Beta contention observations stopped
  dependent edits. Alpha and Beta each reconciled an initial artifact-parent
  setup failure before continuing. Beta reconciled a rejected malformed closure.
- The evaluator's seed record retained generic fixture baseline/progress prose
  despite completed setup. Its exact typed source release and stopped-writer
  fields were correct. This descriptive metadata defect is retained as an
  evaluator failure; completed controls are established by their captured results,
  not that stale prose. The closed record was not rewritten after the study.

These observations led to further maintained improvements: complete lifecycle
validation before publication classification; a regression that a failed dependent
check cannot reach successful release and that reconciled failure can be released
honestly; explicit straight-line outcome gating in the recipe; and a typed
`acknowledgment()` builder with Python/CLI validation and full-field process tests.
The builder formats a receipt and required caller-supplied lineage/correlation;
it does not authenticate receipts, determine the latest owner or publish messages.
These changes do not retroactively make the study's original failures compliant.

The final candidate also adds three staging cleanup fault tests, covering initial
identity failure, descriptor close failure and failed cleanup inspection. Those
fixes, lifecycle/acknowledgment convenience and final retry timing were made after the live
bundle was frozen. They were qualified by focused regressions, the final process
workload and normal build-tool coverage; the model trial was not rerun on that
later bundle.

## Normal regression and limits

The immutable candidate passed the normal build-tool group:

```sh
./build.sh test build --cli --build-dir /tmp/dp-decentralized-20260928/eval/build --jobs 2
```

**28/28 CTest entries passed** on the final candidate in 26.02 seconds, using
approved local GPG socket access for offline
packaging fixtures. Five internal omissions remain: three host ELF cases lack SDK
fixture prerequisites including `patchelf`; two native pacman cases require a
root/disposable Arch environment. These are skips, not passes. Coordination tests
had no skips in this Linux run. The repository and `/tmp` are distinct filesystems
(device IDs 29 and 40), so all five coordination suites were also run with their
fixtures on the repository filesystem. All 175 tests passed there, including both
64-process scenarios. The final timing change additionally passed the affected
transaction suite and both 128-process workloads there; this qualifies that filesystem's exercised primitives as
well as the temporary filesystem. The default CI process count remains 16; larger
counts are explicit stress qualification, not additional reasoning agents.

The original HEAD and index hashes remain unchanged. Comparing all 3,945 baseline
tracked entries found changes only in the twelve intended coordination/CI/build/
documentation files; four new helper/test/report files are untracked additions.
No modem, application, release-tool or wire behavior was changed in the original
checkout. Exercise repairs remain disposable. Build-tool coverage does not imply
runtime/native GUI/platform/release certification. Windows, network filesystems,
power-loss durability, hostile bypass tools and actual mixed-provider/mixed-harness
operation were not qualified. The harness offers three AI workers beside the
parent; assignments and deterministic processes cannot substitute for more models.

All seven recommendations have implementation, regression or explicit operational
boundaries. This finite evidence cannot establish absence of every future defect.
Raw scripts, prompts, failures, hashes and logs remain under
`.agent-work/artifacts/coord-decentralized-20260928/` and
`/tmp/dp-decentralized-20260928/eval/` with normal lifecycle retention.

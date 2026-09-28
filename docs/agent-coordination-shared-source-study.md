# Shared-source coordination stress study, 2026-09-28 UTC

This report describes the original study and its then-unimplemented proposals.
The subsequent [implementation and decentralized validation](agent-coordination-decentralized-validation.md)
records the fixes, additional regressions and follow-up evidence. Historical
counts and limitations below have not been rewritten as later results.

Real shared-source handoffs and larger process workloads completed without losing
accepted repairs. The investigation also found warranted improvements: claim-kind
validation, incomplete handoff discovery, automatic transaction-test coverage, and
safer executable orchestration. Retry overhead was measurable even for unrelated
files. These results do not establish that all future defects have been eliminated.

**The requested larger simultaneous AI population remains untested.** This harness
provided three child-agent slots. Three genuine model workers completed twelve
related repair assignments, four assignments each. They were reused conversations,
not twelve independent or blinded agents. Separate experiments used 32 and 64
concurrent operating-system processes; those are not additional reasoning agents.
An attempted extra agent dispatch hit the harness limit. No alternate Codex or
Claude executable was available locally.

The original checkout started clean at
`6cccb96a9feece1759f706729a3844740f7d22a0`. Its existing tracked files, HEAD and
index were preserved. The only intended original-checkout addition is this report.
No application, release-tool, coordination-helper, test or CI implementation was
changed there. A small proposed helper patch was validated only in an isolated
candidate and remains a recommendation, not an integrated fix.

## Design and protection

The previous [run10](agent-coordination-rerun10.md) used isolated repairs and a
shared ledger. This study used one shared `tools/release.py` in a disposable,
complete repository copy. Workers investigated concurrently and contended for the
same source file. Correct ownership serialized saves; simultaneous uncontrolled
saves were exercised only in separate destructive negative-control fixtures.

The managed-worktree tool returned `Git is unavailable`. Local Git worked, so the
fallback materialized all **3,944 tracked entries** from `git archive`, checked
blob contents, modes and symlink targets, and created independent Git state. It
did not use hard links or share the original `.git`. The checkout was
`/tmp/dp-shared-source-20260928/checkout`, with its own fresh `.agent-work` board.
Setup ownership was released before participant ownership began. Evaluator output
directories remained separate from participant claims.

Every worker write-capable command used a `bwrap` boundary with the filesystem
read-only except the disposable source file and disposable board. A direct
write-open of the original `tools/release.py` was rejected with `EROFS`, before
changing bytes. GitHub token variables were removed from those commands; release
behavior used offline mocks. Network namespace setup was unavailable. The
read-only filesystem boundary worked without it.

This was **per-command protection**, not globally restricted agent capabilities.
The model sessions still had their ordinary tools; their adherence to using the
boundary was cooperative. The writable source binding also did not itself enforce
claims. Do not infer protection against arbitrary agents bypassing the controller.

The study controller used the unchanged checked session helper for acquisition,
publication and release. Source saves required a published whole-file claim, the
caller-supplied current SHA-256, uniquely matching replacement text and successful
Python compilation. It recorded write intent, preimage, postimage and release in
separate participant artifacts. It was a study adapter, not new production tooling.
Eight controller controls checked successful lifecycle operations, rejection of
unclaimed/stale writes, and rejection of original-checkout writes.

Twelve defects were independently seeded into the current release tool. These
were **seeded behavioral regressions, not twelve different historical commits**.
Contracts, mutations and control hashes were frozen before repair. The clean
control passed all twelve tests; each isolated mutation failed its corresponding
test. Workers could read the source and public tests, but were instructed not to
read evaluator repair/control sources. This was open-book coordination testing,
not a blinded repair-quality comparison. Exact worker prompts remain in the chat
trace; they were not independently archived and hashed before dispatch.

## Live shared-source workload

| Assignment | Worker | Required behavior |
| --- | --- | --- |
| 01 | Alpha | Distinguish HTTP 404 absence from an existing resource and permission/network/service failure |
| 02 | Beta | Consume every paginated array, including pages after an empty first page; reject nonarrays |
| 03 | Gamma | Preserve reserved draft identity, title, experiment state and positive nonboolean ID |
| 04 | Alpha | Require the exact source/packaging commit and tag object type |
| 05 | Beta | Reject duplicate, unexpected and missing-support assets |
| 06 | Gamma | Require positive nonboolean asset IDs and completed upload state |
| 07 | Alpha | Require valid SHA-256 by default; preserve the explicitly optional legacy digest path |
| 08 | Beta | Stream exact binary bytes by asset ID with the octet-stream media type |
| 09 | Gamma | Never overwrite an existing destination |
| 10 | Alpha | Check downloaded content/size and remove failed partial downloads |
| 11 | Beta | Check exact workflow/source metadata, pending certification and warnings |
| 12 | Gamma | Finalize the complete target inventory; do not publish after prerequisite failure |

Assignments 03–05 changed the same draft-validation function; 06–10 changed the
same download function. Later tasks depended on those guards. Each worker released
the source after each assignment, before unrelated evidence formatting.

All three workers registered and inspected the same initial source hash before
the start barrier. Gamma's initial stale-hash acquisition was rejected before an
edit or acknowledgment. The workers then voluntarily agreed on repeating
Alpha/Beta/Gamma turns, while still checking real release/acquisition records.
This reduced repeated live-agent acquisition races; it is part of the observed
workflow, not an independently imposed fairness guarantee.

Results:

- **12 successful source acquisitions, 12 source saves and 12 releases.** All
  three repair sessions and the setup/controller/final-snapshot sessions closed
  with empty claims. The final board scan was complete: six records.
- One stale acquisition and one ambiguous replacement were rejected. Beta
  reconciled unchanged source/ownership and narrowed the replacement before
  retrying. No dependent source write followed either rejection.
- All twelve source revisions were reconstructed from the initial bytes,
  recorded patch hashes and consecutive source preimage/postimage hashes.
  **144 contract evaluations** checked all twelve contracts against every saved
  revision. Each step added its assigned passing contract, and no previously
  passing contract regressed. The final candidate passed **12/12**.
- Final source SHA-256:
  `06ebc13c242e55f79ac2282eee2392d2f428cd40c7d66164a2ea36be23d9c5d1`.
  It differed from the original clean release tool only by Gamma's stricter
  boolean identity checks for draft/prerelease status. That isolated change was
  not integrated into the original checkout.
- Additional worker checks covered duplicate/unexpected/missing inventory,
  finalization failure gates and destination preservation. These are supplemental
  checks, not retroactively added frozen acceptance criteria.

The trace establishes preservation of the controlled saves. It is not a complete
trace of every model/tool operation and cannot exclude an unobserved bypass that
temporarily changes and restores a file. One Alpha tool batch also lacked an
explicit failure short-circuit between commands, although all commands in that
batch succeeded. That is an orchestration weakness, not an observed lost edit.

## Larger independent-process workloads

Each process prepared an acquisition review before a barrier. It read the source
and prepared a candidate, acquired a real source claim, revalidated the exact
preimage hash, then saved the modified Python function's mapping, executed the
result, checked retained contributions, released and closed. These workloads
exercise repeated successful writers, unlike a one-winner lock contest.

| Workload | Successful writers | Acquisition attempts / retries | Release attempts / retries | Elapsed |
| --- | ---: | ---: | ---: | ---: |
| One shared source, 32 processes | 32/32 | 618 / 586 | 32 / 0 | 6.15 s |
| One shared source, 64 processes | 64/64 | 2,844 / 2,780 | 67 / 3 | 37.96 s |
| Disjoint sources, 32 processes | 32/32 | 158 / 126 | 144 / 112 | 3.88 s |

An independent audit checked **exact final worker/value mappings**, not just a
sum, consecutive source hashes in the shared-file runs, complete scans, exact
session counts, empty claims, and joined children. Every contribution survived.
Shared-file acquisition-receipt-to-write-completion intervals did not overlap.
That interval is narrower than the whole claim lifetime; the disjoint-file
summary's overlap field was not evaluated and must not be interpreted as a
measurement of disjoint concurrency.

The 64-shared and 32-disjoint experiments ran at the same time. Their durations
are mixed-load observations, not a controlled standalone scaling comparison.
There was no starvation in these finite runs; no fairness guarantee follows.
Generic retries in this test driver were not subjected to uncertain-publication
injection and are not a safe general-purpose retry implementation.

## Deterministic fault and dependency controls

| Risk | Observed negative control | Positive control / interpretation |
| --- | --- | --- |
| Two stale source saves | The second unguarded save removed the first repair | Exact preimage guard rejected the stale action and retained the first repair |
| Incorrect file/directory kind | Existing directory claimed as `file`; descendant file claim also granted | Isolated prototype rejects both kind mismatches, including path-kind changes after review/grant |
| Partial scope declarations | Adding an unrelated explicit `Scope` suppressed the opaque-handoff uncertainty fallback | Robustness gap for malformed/incompletely migrated records; see recommendations |
| Disjoint edits sharing a schema | Both file claims and isolated unit checks succeeded, but producer/consumer composition failed | Shared invariant claim blocked the second edit until handoff; adapted combined behavior passed |
| Edit then revert during a consumer run | Endpoint hashes matched while the consumer observed inconsistent states | Confirms need for stable source/snapshot; endpoint hashing is not continuous stability evidence |
| Opposite resource acquisition order | Both partial owners were blocked from acquiring the other's scope | Mutex stayed free; quiescent release followed by all-or-nothing acquisition completed |
| Parent releases before child exits | A deliberately false no-jobs closure was accepted; the child overwrote the next owner | A small trusted release guard rejected the live-child request; joining before release preserved the next owner |
| Process killed during source save | In-place write left syntactically incomplete source | Staging kept exact original bytes before publication; retained claims blocked newcomers in both cases |
| Changing large board | Three progress updates each invalidated the next page of a 128-record scan | Immutable observation delivered all 128 records; later malformed entry blocked fresh review and remained visible |

The child-lifetime result does not mean the record parser can discover arbitrary
processes. The positive guard knew the actual child handle. The source-crash test
killed a process after a pipe-confirmed partial write, before staged publication;
it does not qualify atomic replacement across power loss or filesystem failure.
The initial versions of these two probes had weaker measurement labels. Reviewed
v2 controls use exact pre-restoration byte equality and an actual rejecting
release-guard call. Initial and corrected evidence are retained separately.

Synthetic fault boards are inert fixtures, not resumable agent sessions. No
age/PID-only reclamation of real work occurred. Recovery controls knew their sole
child by construction, joined it, and restored their known fixture preimage.

## Warranted improvements

### 1. Reject existing filesystem/claim-kind mismatches

**Priority: high; concrete helper hardening.**
[Claim parsing](../tools/check-agent-record.py) validates canonical spelling and
hard links but accepts an existing directory labeled `file`. Overlap detection
uses the declared kind, so that malformed parent claim does not cover its child.
The checked transaction granted both claims in the reproducer. This is a
malformed-input robustness defect, not a violation shown with correctly typed
claims.

Reject `file` claims that currently resolve to directories and `directory` claims
that resolve to non-directories. Continue permitting explicitly typed absent
paths, rechecking at acquisition and during subsequent complete-claims scans.
Do not blindly tighten `canonical_scope`: historical path discovery intentionally
treats some path mentions conservatively as covering scopes.

A four-line isolated `parse_claims` prototype passed **53 reader, 32 publisher and
30 session tests (115 total)**. Additional controls rejected existing mismatches,
a directory created after review, and a previously absent file claim becoming a
directory before a descendant grant. Correct file/directory claims still passed.
The proposed patch is retained in the study evidence; it has not been integrated.

### 2. Preserve uncertainty for incomplete handoff declarations

**Priority: medium; conservative-discovery robustness.**
`handoff_relevance` currently disables its opaque-history fallback as soon as it
sees any explicit `Scope` line, including `Scope: none`. Consequently:

```text
- Scope: file: /unrelated/file
- Release: R1; also released shared.py unchanged.
```

can be classified as irrelevant to the omitted shared source, whereas the opaque
release line alone requires manual review. These records violate the workflow's
requirement to enumerate every exact scope. This is **not** evidence that a
compliant complete handoff was missed, but it is important when records are
partially migrated or written by less reliable participants.

Define an unambiguous completeness contract for machine-filterable handoffs;
retain manual uncertainty for undeclared/contradictory history. Add mixed explicit
and opaque cases, including `Scope: none` with real transfer history. Preserve
ordinary release-reference/hash/stopped-writer prose; simply rejecting all prose
after a scope declaration would break valid records. No heuristic production
rewrite was made during this study.

### 3. Run the checked-session suite in automatic feedback

**Priority: high; concrete coverage omission.**
The automatic PR/main tooling step in [.github/workflows/ci.yml](../.github/workflows/ci.yml)
invokes `test_agent_record.py` and `test_agent_board.py` but omits
`test_agent_session.py`, where acquisition, crash-boundary and concurrency tests
live. Automatic Legacy diagnostics do not supply that coverage. Add the third
suite to that step and to the coordination row in [building.md](building.md).

[CMakeLists.txt](../CMakeLists.txt) and [TestGroups.cmake](../cmake/TestGroups.cmake)
already register the suite. Full/manual build scopes include it. The gap is
automatic transaction-suite feedback, not absence from all CI.

### 4. Supply a tested edit/job controller for uncertain participants

**Priority: high when models or subprocesses cannot reliably follow prose.**
The existing workflow already requires qualified executable operations and
enforced isolation. Implement that requirement at the source/job boundary, not
only at record publication: gate dependent edits, acknowledgments, generators and
test launches on successful acquisition; retain claims until known children and
output writers finish; recheck exact source preimages; stage complete source
replacements where supported. Keep raw shared-source/Git writes unavailable to
participants that need this protection.

The controlled negative cases establish why these obligations matter. The study
adapter itself was deliberately limited: in-place source writes, unconditional
`Running jobs: none` for its short synchronous operations, and mechanically
refreshed timestamps. Printing inbox bodies is not proof of semantic processing.
Worker self-reports and trace review were needed to check those facts. It should
not be adopted unchanged as production orchestration.

Acceptance should include failed-acquisition-to-dependent-action chains, late
child/log writers, queued saves, interrupted staging/replacement, explicit
reconciliation after uncertain publication, and denied bypass writes through the
participant's actual available tools. This extends executable support rather than
adding another long prose checklist.

### 5. Distinguish retryable rejection from uncertain publication

**Priority: medium; make safe callers easier to implement.**
Expose structured failure categories or phase information. Clean contention and
stale-review rejection can trigger bounded backoff followed by rereading and
replanning. Publication/cleanup/output uncertainty must instead stop dependent
actions and reconcile saved state. Do not make callers infer this distinction
from one generic `CoordinationError` string or retry every such exception.

Existing injected-failure suites correctly preserve uncertain committed state.
The new load driver does not extend that assurance to generic retry loops: no
uncertain-publication failure was injected during those successful load runs.

### 6. Reduce unrelated review invalidations without weakening exclusion

**Priority: medium; measured scalability concern, not demonstrated corruption.**
`agent-session.review` fingerprints all ownership and all `Blockers and handoff`
text. A disjoint registration/release invalidates pending reviews. A deterministic
two-file case reproduced this; the 32-disjoint workload required 126 acquisition
retries and 112 release retries despite independent source files.

Consider separating ordinary blocker/progress text from ownership provenance,
and a tested bounded retry API. Any narrower fingerprint must still rescan all
current claims and preserve relevant unknown-record, quiet-owner, unchanged-byte
handoff and input-identity checks. Do not skip owners, weaken ABA/provenance
protection, or claim a fairness improvement from these timings. Global
invalidation is currently an intentional conservative safeguard.

### 7. Retain reproducible shared-source and failure scenarios

**Priority: medium; close the previous exercise coverage gap.**
Promote compact deterministic regressions for the confirmed kind/discovery cases
and sustained multiwriter preservation. Add a separate shared-source study recipe
to the [evaluation guide](agent-coordination-evaluation.md), distinguishing seeded
stress tasks from historical/blinded repairs. Record actual model-worker count,
peak concurrency, reused contexts, overlapping functions, source/input hashes,
negative controls, retry phases, evaluator interventions and internal test skips.

The existing guidance already covers invariant resources, immutable observations,
child lifetimes, deadlocks and recovery. These probes support those rules; they do
not justify duplicating them with generic new policy. A future genuinely larger
AI trial requires a harness with more concurrent independently constrained model
workers. That requested scale cannot be substituted with more assignments or
deterministic processes.

## Combined validation and remaining limits

After the final source release, the evaluator acquired it, copied and verified
the complete 3,944-entry final snapshot, and released the shared source before
running separate general validation:

```sh
./build.sh test build --cli --build-dir /tmp/dp-shared-source-20260928/evaluation/build --jobs 2
```

The normal build-tool group initially passed **22/26** entries. Four APT/packaging
fixtures failed at GPG setup. A separate short-path AF_UNIX bind probe reproduced
`Operation not permitted`, matching the existing environment note; no source or
assertion was changed. With approved local socket access, only the four affected
full CTest entries were rerun and all passed. The same frozen candidate therefore
has completed passing evidence for **26/26 CTest entries**. Initial and rerun logs
are retained; the initial run itself was not green.

Five internal omissions remain: three host-runtime ELF integration cases require
the unavailable SDK fixture prerequisites, including `patchelf`; two native
pacman cases require the disposable root/Arch environment. They are skips, not
passes. This is build/release/coordination-tool regression evidence, not modem,
native GUI, every platform/SDK, physical-device or release certification.

The relevant code audit included all three coordination helpers and their test
suites, workflow/lifecycle/recipes/evaluation documentation, the release-tool
consumers and fixtures, CMake registration and automatic CI routing. The
repository's scheduler/FFT/recovery dependency examples were checked against
`src/search_parallel.hpp`, `src/pattern_fft_batch.cpp`, `src/pattern_receiver.cpp`
and `src/recovery.cpp`. Those application algorithms were not modified or put
through a new live multi-agent repair trial.

No matched no-coordination model trial, complete model-token/attention accounting,
Windows/network-filesystem validation, shared Git mutation trial or power-loss
durability qualification was performed. No clean-protocol, reduced collision-rate,
negligible-distraction or exhaustive future-defect claim follows. The finite risk
matrix above is the supported assurance boundary.

Compact raw evidence, scripts, exact patches, failure logs and SHA-256 manifest
are retained under
`.agent-work/artifacts/shared-source-stress-20260928/evidence/`; disposable complete
trees remain under `/tmp/dp-shared-source-20260928/`. They follow the normal
retention workflow and are not a new permanent archive. This report preserves
the essential results and unresolved recommendations after raw evidence expires.

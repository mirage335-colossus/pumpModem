# Coordination hardening and assurance, 2026-09-27

This change addresses the execution failures in [run10](agent-coordination-rerun10.md)
and the still-relevant lessons from runs1–9. It strengthens the optional helpers,
keeps one authoritative session record, and adds explicit dependency/integration
rules. It does not change modem behavior or the harness's research/tool policy.
Routine users read AGENTS and the workflow; this coverage record and the detailed
recipes are not additional startup requirements.

## Coverage and design

| Failure or scenario | Safeguard and remaining human responsibility |
| --- | --- |
| Closed intervening owner changed no bytes and sent no notice | Scope discovery includes closed handoffs and covering scopes; compare the release/acquired-from chain, not timestamps or equal hashes |
| Failure before publication followed by false acknowledgment | Checked operation gates all dependent actions; callbacks run only after publication verification and successful owned-lock cleanup |
| Publisher succeeds but cleanup/output fails | No success receipt/dependent callback; inspect saved state, preserve terminal release, do not blindly replay |
| Completed build hidden behind failed acquisition | Event-preserving checkpoint replaces current jobs/progress/handoff/next action together, independently of acquiring more scope |
| Mechanical inbox/progress timestamps | Builder preserves prior event times unless explicit observed times are supplied; factual observation remains the caller's responsibility |
| Truncated startup/large registry output | Bounded document and whole-record/error pages with snapshot checks; every page/error still requires review |
| Missing lock host/start metadata | Mutex wrapper supplies host, UTC, role, PID/start identity where available; never identifies its shell as the agent worker |
| Workspace mount returns stale directory-handle listing | Refresh enumeration through a same-identity descriptor; retain owner-byte and unfamiliar-entry checks; qualify on the actual board filesystem |
| Overlapping files, covering directory, rename, aliases | Canonical component overlap checks plus complete claim review; both rename endpoints and all generated outputs remain claimed |
| Partial acquisition and circular waits | Acquire coupled scopes together or arrange quiescent handoff/isolation; no waiting/callback/build under the registry mutex |
| Two writers race to create the same session or message | Existing no-replace publication retained; exact own-record CAS and saved-byte verification retained |
| Candidates or temporary message bytes become visible early | Stage records inside owned mutex and messages privately before atomic publication; no candidate in sessions/ |
| Legacy/unknown/malformed record | Fail closed; exact-byte manual interpretation is explicit and invalidates when those bytes change, never a state/age-based ignore rule |
| Release before final command output finishes | Finish/join output writers before release; closure output goes to harness; no post-release artifact write |
| Different files change one algorithm/API/schema | Record dependencies and a narrow invariant/integration owner when needed; inspect and test the combined candidate |
| Stale analysis or test result | Revalidate dependency bytes/configuration, replan when changed, retain isolated stable inputs for long checks |
| Edit-and-revert during a build | Endpoint hashes do not prove stability; coordinate writers or use a complete isolated snapshot |
| Paused/resumable agent, lost PID or stale clock | No lease expiry/automatic stealing; existing positive-evidence recovery and process-role rules remain |
| Cleanup removes still-needed provenance | Preserve or consolidate live handoff facts under mutex before deletion; retain unresolved knowledge without unbounded archives |
| Different harnesses or machines | Same physical board and reliable primitives, or separate checkouts with explicit integration; cloud synchronization is not a lock |
| Shared Git, builds, temporary paths, devices or publication | Existing resource claims, scoped outputs and exact source/run identity remain mandatory |
| Many agents drown in board text | Complete machine scans with bounded returned data, narrow scopes, stable requests, backoff and per-session outputs; no relevance-filtered ownership or unverified cache |
| Temporary notes or community commands become instructions | Retain attribution, date, confidence and local applicability; verify commands/results and preserve governing requirements |

The new session helper is an optional caller implementation above the existing
atomic publisher. It does not introduce a service, lock lease, second claim store
or permanent event log. Unsupported primitives and ambiguous inputs fail closed;
equivalent reviewed manual operations remain available. An agent still determines
whether stopped-writer evidence, handoff provenance and its proposed edit are true
and adequate. A helper cannot infer an algorithm invariant from file paths.

## Repository-specific dependency audit

The source audit identified concrete reasons to coordinate semantic dependencies:

- `src/search_parallel.hpp` promises exactly-once visits, private bounded worker
  slots, serial nested calls, joining before return and deterministic selection of
  the lowest failing index. `src/pattern_fft_batch.cpp` indexes scratch by those
  worker slots; `src/pattern_receiver.cpp` distinguishes logical jobs from physical
  workers. Independently edited scheduler/caller files can violate a shared
  invariant even without a competing save.
- `src/recovery.cpp` reserves plan/assignment batches, joins workers and compares
  completed attempts with total before exposing results. Enumeration, cancellation,
  resume and consumers need combined validation; `tests/test_recovery.cpp` includes
  relevant incomplete/cancel/resume controls. No changes to these algorithms were
  made by this coordination task.
- `build.sh` and CTest mutate the chosen build/profile tree, including packaging
  outputs. Distinct output directories do not stabilize shared source.
  `cmake/BuildInfo.cmake` records HEAD and a generic dirty flag, which cannot identify
  a particular dirty candidate; relevant dirty inputs/configuration must accompany
  evidence. A new worktree from HEAD does not include uncommitted changes.

These findings motivated the short dependency/invariant and deadlock rules in the
workflow. They do not add a global semantic lock or require serializing unrelated
research and implementation. Integration tests must address the affected contract;
a clean textual merge is not semantic validation.

## Validation and limits

The final source candidate was copied independently from all 3,941 tracked paths
plus three new task files, preserving modes, symlinks and tracked ignored inputs.
The copy and unchanged source were compared before validation. It includes the
dirty changes based on `c840f18c37474587ebf649b80317ae9bf4b685cd`; it is not a
HEAD-only worktree or a hard-linked tree. The 3,944-entry inventory has SHA-256
`bee526fcc4648ae45d34ff9ecca1c6e5387a386ec4c5c11f0791c87e446b5ec9`.
Only documentation/result recording changed after this capture; tested helper,
test and CMake inputs remained identical.

On Linux x86-64 / Python 3.13.5:

- `./build.sh test build --cli --build-dir /tmp/dp-coord11/build --jobs 2`
  on that snapshot passed **26/26 CTest entries** in 17.75 seconds. This normal
  build-tool group includes **53 reader, 30 publisher and 25 session-operation
  tests**. Local GPG fixture IPC used approved execution because prior runs had
  established sandbox socket failures; no real release was mutated.
- Five internal integration cases were skipped: three ELF SDK cases need
  `patchelf`; two native pacman cases need root and the disposable Arch/pacman
  environment. These are coverage omissions, not passes. Native Windows,
  network filesystems, application runtime and release certification were not
  qualified by this helper/documentation change.
- Focused coordination suites also ran with temporary fixtures on the actual
  workspace filesystem. The reader, publisher and final session helper passed
  **53/53, 30/30 and 25/25**, respectively.
- Session tests include a barrier-started **32-thread same-file contest** with
  exactly one acquisition/callback, and **eight disjoint workers** completing
  useful writes and closure with bounded retries. Failure controls cover rejected
  preconditions/publication, uncertain cleanup, terminal callbacks, stale records,
  changed inputs/aliases, exact-byte legacy interpretation and unnoticed closed
  owners. Reader tests include 75-record paging, changing snapshots, malformed
  entries, long documents, CRLF and undecodable filesystem names.

Review and environment checks found additional defects before completion: CRLF
normalization invalidated exact-byte record hashes; an undecodable filename could
break a scan fingerprint; and the workspace mount returned an empty listing from
a directory descriptor opened before `owner.md` was created. Fresh relative
descriptors saw the correct owner and the same inode. The original workspace
session run had 11 failures and seven errors in 24 cases; that failed attempt and
the discriminating reproduction are retained. The final fix refreshes enumeration
without ignoring unfamiliar entries or weakening identity checks, and adds a
deterministic regression. Passing `/tmp` tests alone would have missed this defect.

A contributing writer also waited for a harness response despite a delivered
filesystem receipt. The workflow now explicitly says to process the board inbox
without depending on a harness wakeup. This observation is implementation-time
evidence, not an extra blinded worker trial.

Detailed logs, source inventory and final input checks remain under
`.agent-work/artifacts/coord-guidance11-20260927/` and the contributing writers'
artifacts, subject to closure/retention rules. Documentation links, local anchors,
code fences and whitespace were checked. Source changes are limited to the
coordination helpers, their tests and test registration; modem behavior is unchanged.

This is a cooperative protocol. It cannot stop a process with filesystem access
from ignoring it, authenticate arbitrary board authors, or guarantee absence of
logic/security defects. Snapshot tokens have no persistent event counter: a
create/release/delete cycle that erases every intervening ownership fact could
escape comparison. The lifecycle therefore prohibits erasing still-needed
provenance and requires transfer/consolidation before earlier disposal; ordinary
retention applies once that dependency is resolved. Atomic visibility is not
power-loss durability. The Python
publisher requires supported POSIX descriptor-relative operations and hard links;
other harnesses/platforms must use equivalent safe primitives or isolate work.
Git worktrees still share some Git state, including most refs. These boundaries
follow the [Python filesystem API](https://docs.python.org/3/library/os.html#os.replace)
and [Git worktree documentation](https://git-scm.com/docs/git-worktree#_refs), checked
2026-09-27; they are not newly claimed operating-system guarantees.

Deterministic stress tests are not a new blinded five-agent useful-work study or a
measurement of research prevalence, human attention cost or collision probability.
The [evaluation guide](agent-coordination-evaluation.md#broader-collisions-and-many-agent-validation)
now requires broader source/dependency and large-board scenarios rather than
inferring those properties from a successful shared ledger. Preserve the observed
limits when deciding whether further empirical assurance is needed.

## Follow-up: participants that cannot reliably follow the protocol

The follow-up starts from committed `7415fd3`; the earlier validation above remains
historical evidence for its original candidate. A stronger model, clean merge or
passing test cannot replace exclusion before a shared write. Routine claim changes
now require the checked transaction or an equivalently qualified executable wrapper.
The ad hoc shell-lock example was removed. The guide distinguishes the short
registry mutex from claims held throughout editing and validation. Uncertain
participants require read-only tool access or permissions that actually deny shared
source/board/build/common-Git writes while allowing private implementation. These
repository tools do not install those boundaries in other harnesses.

The low-level publisher previously checked session identity alone. It now also
requires an acquisition-specific random 32-hex token, generated and passed by the
checked mutex context. The immutable `RegistryLock(fd, token)` handle changes on
every acquisition, even for the same session. Missing, malformed, duplicated or
stale tokens fail before staging; successful explicit delegation to a publisher
subprocess remains supported. Custom callers must supply `lock_token` or CLI
`--lock-token`; routine callers use `commit`, which handles it automatically.
Existing tokenless locks stay owned and are not silently migrated or removed.
This prevents accidental session-only borrowing and stale-token replay, but is not
authentication: a participant able to read/alter the current board can copy a token.
Enforced isolation remains necessary when that participant cannot honor the rules.

New failure controls cover independent processes racing one source claim, same-ID
reentry and stale acquisition, killed holders, owner replacement with identical
bytes, directory replacement, and failures of owner unlink or final `rmdir`.
Successful publication remains authoritative when cleanup fails; dependent work
is suppressed and the lock stays blocking, including an empty partially cleaned
lock. Recovery still needs positive evidence and exclusive coordination, not age
or PID death alone. The Linux [mkdir](https://man7.org/linux/man-pages/man2/mkdir.2.html)
and [rmdir](https://man7.org/linux/man-pages/man2/rmdir.2.html) references, checked
2026-09-27, document existing-name rejection, empty-directory removal and NFS
caveats; testing a local filesystem does not qualify network/cloud synchronization.

Final validation used an independent copy of all 3,944 tracked inputs, including
current dirty bytes, modes, symlinks and tracked ignored sources. Before/after
comparison verified the copy; inventory SHA-256:
`07ab250c6def28bbdc802e6f6f64d09b48de1706fe8b50654000295a643c57ea`.
Only documentation changed afterward. The normal
`./build.sh test build --cli --build-dir /tmp/dp-lock12/build --jobs 2` group passed
**26/26 CTest entries** in 17.99 seconds, including **53 reader, 32 publisher and
30 session tests**. Five prerequisite omissions remain the same as above: three
ELF SDK cases without `patchelf` and two native pacman cases. Local GPG fixture IPC
used approved execution; no real release was changed.

Both changed suites also passed on the workspace filesystem (32 publisher and
30 session cases); the two process cases were rerun after making their working
directory explicit. Eight independent processes raced from a pipe barrier with
one successful claim/write; the existing 32-thread contest and eight-disjoint-worker
fixtures remain. Independent code and guidance reviews found no remaining blocker.
Logs, frozen inventory and final source comparisons are in
`.agent-work/artifacts/coord-lock12-20260927/` under ordinary retention.
This is helper/process evidence, not a trial of less capable reasoning models or
proof of permissions in another harness. No modem runtime behavior changed.

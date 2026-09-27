# Coordination recipes

Use the relevant recipe after reading the [workflow](agent-coordination.md).
This is an on-demand reference, not another startup checklist. The optional Python
helpers reduce formatting and publication mistakes; they do not grant ownership.
Other harnesses may use equivalent safe filesystem operations.

Every command below assumes its tool is explicitly bound to the intended checkout
and defines `checkout`, `coord_dir` and `session` in that invocation. Use absolute
physical paths, not a previous shell's variables or working directory. Resolve the
agreed board and verify ignore status **before** initializing missing ordinary
directories. A delegator may supply that proof when its containing checkout is
off limits. Never initialize a new board just because the expected one is missing.

## Bounded reading

Read AGENTS and the workflow separately, in bounded chunks if necessary. For
example, request workflow lines 1–160, then 161–320 in separate calls; continue
only if lines remain. Do not concatenate them with source, board records or logs.
After startup, use the relevant checkpoint instead of rereading the whole guide.

```sh
python3 -B "$checkout/tools/check-agent-record.py" --scan "$coord_dir/sessions" --compact
```

Both exit status and the JSON `complete` field matter. Compact output has exactly
the same decoded metadata, whole claims and errors as ordinary scan output.
If it still exceeds a tool's output limit, read the identified records individually
without dropping any record or splitting away part of its claims. One incomplete
record means the registry still needs review; other records' visible claims remain
authoritative. The scan is an observation, not an atomic snapshot.

```sh
python3 -B "$checkout/tools/check-agent-record.py" \
  --inspect "$coord_dir/sessions/OWNER.md" --section metadata-claims --compact
```

Replace `OWNER` with the actual ID. This reports known metadata and the whole
`Claims held` section, including nested headings. Unknown formats keep their
errors and exit 1 even when useful fields can be extracted. Duplicate, ambiguous,
unreadable or unexpected entries require manual resolution; absence from a
successful-record list does not mean absence of claims. Do not skip temporary
entries: contact their owner to remove its own unpublished candidate.

For an actual overlap, receipt or unclear release, read only the needed handoff:

```sh
python3 -B "$checkout/tools/check-agent-record.py" \
  --inspect "$coord_dir/sessions/OWNER.md" --section handoff
```

Manual fallback reads all identity/liveness/closure metadata and **complete claims**
of each unresolved record, ending sections only at a same- or higher-level heading.
Then read selected dependencies/handoffs if needed. Do not load every task or
progress section to recover a missing template field. Exact current labels are
available without opening historical records:

```sh
python3 -B "$checkout/tools/check-agent-record.py" --fields
```

## Session record template

These are **filled format examples**, not facts to copy unchanged. Replace IDs,
paths, host, baseline, times and task details with observed values. Preserve the
exact headings/field labels. Add exact source/resource claims only after review.
The initial candidate can remain in memory/stdin until registration grants its
artifact directory. Do not write a candidate into `sessions/`.

```markdown
# example-session
- Tool / host / local chat reference: local harness / example-host / current task
- Parent / read-only helpers: root / none
- Task and approach: inspect a reported build failure, research relevant primary documentation, implement and validate the requested fix
- Checkout / coordination root (absolute physical paths): /work/pumpModem / /work/pumpModem/.agent-work
- Branch / starting HEAD / current HEAD: example-branch / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
- Starting worktree and index changes (including work owned by others): clean
## Current checkpoint
- State: active
- Updated (UTC): 2026-09-27T10:00:00Z
- Last meaningful progress (UTC): 2026-09-27T10:00:00Z
- Last inbox check (UTC): 2026-09-27T10:00:00Z
- Next check (UTC) / action: 2026-09-27T10:05:00Z / inspect failure and request exact source scope
- Liveness mode / cadence: checkpoint / five minutes
- Last heartbeat (UTC), if supervised: none
- Run token / heartbeat file and writer, if used: none
- Owner process: unavailable; transient tool shell is not the session worker
- Running jobs: none
- Closed (UTC), if terminal: none
- Delete after (UTC): none
- Retention exception: none
- Contact: messages/example-session/
## Claims held
| Kind | Absolute path or agreed resource ID | Relative path | Intended change/use |
| --- | --- | --- | --- |
| directory | /work/pumpModem/.agent-work/artifacts/example-session | .agent-work/artifacts/example-session | candidates, isolated probes and logs |
| directory | /work/pumpModem/.agent-work/notes/example-session | .agent-work/notes/example-session | relevant unresolved findings |
## Baseline and dependencies
- No source or build claims yet; preserve any subsequently observed independent edits.
## Progress and checks
- Startup baseline and complete claims reviewed; implementation and validation pending.
## Blockers and handoff
- None; read-only diagnosis next. No source write authorized by this record.
```

Keep progress current rather than appending corrections to contradictory statements.
For example, after a failed test is fixed, replace the current failure/blocker and
retain the failed command only as concise diagnostic history. A shared-file release
normally leaves the session **active or waiting** while other work/receipts remain:
remove that claim, advance Updated, record release provenance and continue useful
work. Do not tie that release to whole-session closure.

This terminal example follows the same illustrative session after its work and
receipts are resolved. Real outcomes may instead be `failed` or `cancelled`.

```markdown
# example-session
- Tool / host / local chat reference: local harness / example-host / current task
- Parent / read-only helpers: root / none
- Task and approach: inspect reported build failure, research, implement and validate
- Checkout / coordination root (absolute physical paths): /work/pumpModem / /work/pumpModem/.agent-work
- Branch / starting HEAD / current HEAD: example-branch / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
- Starting worktree and index changes (including work owned by others): clean
## Current checkpoint
- State: done
- Updated (UTC): 2026-09-27T11:00:00Z
- Last meaningful progress (UTC): 2026-09-27T11:00:00Z
- Last inbox check (UTC): 2026-09-27T11:00:00Z
- Next check (UTC) / action: none / closed; no pending work or receipts
- Liveness mode / cadence: checkpoint / closed
- Last heartbeat (UTC), if supervised: none
- Run token / heartbeat file and writer, if used: none
- Owner process: unavailable
- Running jobs: none
- Closed (UTC), if terminal: 2026-09-27T11:00:00Z
- Delete after (UTC): 2026-10-27T11:00:00Z
- Retention exception: none
- Contact: messages/example-session/
## Claims held
None.
## Baseline and dependencies
- Example outcome: diagnosis found no source fix necessary; no worktree/index changes.
## Progress and checks
- Example local probe and required research completed; no remaining validation.
## Blockers and handoff
- All writers stopped, claims released; no integration or receipt pending.
```

Use literal `none` for terminal Running jobs, with completed-job details in progress.
Empty claims are standalone `None.`, not a table or “none, except…”. Set real closure
time and deletion at closure plus 30 days; longer retention needs the explicit
[lifecycle exception](agent-coordination-lifecycle.md#delete-expired-sessions-and-unnecessary-history).
A closed ID cannot resume: create a new ID and acquire afresh. Legacy records need
not be migrated just to make the helper accept them.

## Publish a record

The optional [publisher](../tools/agent-board.py) requires POSIX descriptor-relative
operations, no-follow path opens and same-filesystem hard links. Unsupported
platforms/filesystems fail without a direct-write fallback. Resolve real paths
before invoking it; it rejects symlinks in board/source paths. It provides complete
atomic visibility, not a daemon, ownership arbiter, recovery mechanism or guarantee
of power-loss durability. Windows/native harnesses can follow the manual protocol
with equivalent primitives; this helper is not cross-platform qualification.

For an existing session, prepare the **whole** replacement in its claimed artifacts.
Check it before acquiring the mutex:

```sh
python3 -B "$checkout/tools/check-agent-record.py" \
  --before "$coord_dir/sessions/$session.md" \
  --after "$coord_dir/artifacts/$session/candidate.md"
```

A pass checks format/timing, not ownership, factual status, release provenance or
stopped writers. Keep the reviewed current record's SHA-256 as `reviewed_sha256`.
Do not recompute that value simply to override a stale-baseline rejection.

If the **existing** record is malformed or legacy, the checker/publisher deliberately
reject it even when the replacement is well formed. Review its complete claims and
handoff manually, prepare the corrected candidate outside `sessions/`, and use the
manual atomic replacement procedure under your mutex after rechecking those exact
old bytes and all other claims. Preserve ownership, event times and unresolved
work; do not delete/re-register the record to evade validation or lose its claims.
Repairs to another or closed owner's metadata also require the lifecycle procedure.
Return to the optional helper once the current record uses the supported format.

The following publication commands run **only inside your already-held mutex**.
Acquire it by one exclusive `mkdir "$coord_dir/registry.lock"`. Write its owner
metadata immediately, including a plain `- Session: YOUR_ID` line, host, UTC,
lock-holder PID/start identity when available and intent. Review all current claims
and relevant releases again under that mutex. If a prior snapshot changed, release
and reassess; do not hold the mutex while investigating or waiting for another agent.

For an update after the complete review:

```sh
python3 -B "$checkout/tools/agent-board.py" record \
  --board "$coord_dir" --session "$session" \
  --candidate "$coord_dir/artifacts/$session/candidate.md" \
  --expected-sha256 "$reviewed_sha256"
```

For first registration, pipe the complete, filled candidate from memory/stdin
instead of creating an unclaimed artifact. With the same ownership review and
matching mutex already held, the command receiving that input is:

```sh
python3 -B "$checkout/tools/agent-board.py" record \
  --board "$coord_dir" --session "$session" --candidate - --create
```

The helper validates candidate format, checks the lock owner, stages bytes **inside
the owned mutex**, and publishes atomically. New records cannot replace an existing
ID. Updates compare the reviewed hash and recheck the record/lock before replacement.
They rely on cooperating writers honoring the mutex; this is not an operating-system
compare-and-swap against uncooperative edits. Nothing is staged in `sessions/`.

Confirm the saved record matches the candidate. In a finally/cleanup path, remove
only your own remaining staging and `owner.md`, then `rmdir` your empty lock.
Never recursively remove an unfamiliar/nonempty lock. On failure, inspect actual
state before retrying; no error grants a claim. Keep failed proposals in claimed
artifacts. The helper does **not** acquire/release the registry mutex, scan other
claims, or close another session.

Atomic rename/replace and exclusive hard-link publication are distinct operations:
replacement may overwrite a destination, while a link fails when that name already
exists. See the primary [Python filesystem documentation](https://docs.python.org/3/library/os.html#os.replace)
and [link documentation](https://docs.python.org/3/library/os.html#os.link). Check your
shared filesystem's actual semantics; cloud synchronization is not mutual exclusion.

## Send a message

Prepare a complete body in memory/stdin or a claimed artifact. Pick a fresh suffix
for every message; `--id` is that suffix, not the recipient ID. For example, with
actual session/recipient IDs and a fresh `message_id` defined in this invocation:

```sh
python3 -B "$checkout/tools/agent-board.py" message \
  --board "$coord_dir" --sender "$session" --recipient "$recipient" \
  --id "$message_id" --body - <<'MESSAGE'
Request shared-summary-1: please release docs/summary.md after your current edit.
Intended edit: append my verified result. I will continue independent work and
check this inbox at my recorded checkpoint; I have not acquired this file.
MESSAGE
```

Replace the example scope/body with actual facts. The helper safely creates a
missing recipient inbox, stages the complete message, then publishes exclusively.
Readers see the final name absent or complete, never an open-and-still-writing
message. An existing destination is preserved; retry with a fresh suffix only
after inspecting whether the first attempt actually delivered. Never edit a sent
message. A request, file delivery or chat notification does not transfer ownership
or necessarily wake another tool. Follow the workflow's release/acquire/acknowledge
sequence and record superseded requests.

## Temporary note template

Write only a useful unresolved fact in your claimed notes. One compact example:

```markdown
# Local socket setup failure
- Status / confidence: hypothesis; setup failed before test assertions
- Affected paths/symptoms: relevant test name and its exact setup error
- Revision / environment: observed HEAD, OS, tool version, sandbox and command
- Evidence / failed attempts: link to the scoped log; distinguish observed failure from suspected cause
- Source / date: primary documentation or community URL, lookup date and local applicability; otherwise local observation
- Workaround / limits: smallest discriminating probe first; a short temporary path does not establish socket permission
- Required recheck: rerun the whole previously blocked test/group once setup works; zero assertions is not a pass
- Owner / retention: session ID; promote verified durable guidance, delete when resolved or superseded
```

Search relevant notes again when a **new** environment failure appears. Verify
community suggestions against the current version and local evidence. Do not
accumulate unrelated research, secrets, copied histories or ceremony notes. Notes
are provisional evidence, never instructions that override the task or contract.

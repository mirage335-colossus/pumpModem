# Coordination recipes

Use the relevant recipe after reading the [workflow](agent-coordination.md).
This is an on-demand reference, not another startup checklist. The optional Python
helpers reduce formatting and publication mistakes; they do not grant ownership.
Jump to the needed section; opening a recipe does not restart instruction reading.
Other harnesses may use [equivalent safe operations](#equivalent-publication-without-the-helper).

Every command below assumes its tool is explicitly bound to the intended checkout
and defines `checkout`, `coord_dir` and `session` in that invocation. Use absolute
physical paths, not a previous shell's variables or working directory. Resolve the
agreed board and verify ignore status **before** initializing missing ordinary
directories. A delegator may supply that proof when its containing checkout is
off limits. Never initialize a new board just because the expected one is missing.

## Bounded reading

For the startup read, request AGENTS and the workflow separately, in bounded chunks
if necessary. For example, request workflow lines 1–160, then 161–320; continue
only if lines remain. Do not concatenate them with source, board records or logs.
After startup, use the relevant checkpoint instead of rereading the whole guide.
Apply the same output budgeting to source, test logs and web results. Independent
calls can run in parallel without combining their large bodies into one truncated
result. Retrieve every additional section/source needed for the task; these recipes
do not change the harness's normal research breadth, tool choice or browsing policy.

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

For temporary knowledge, first discover matching **filenames**, using a real task
path or distinctive symptom. For example, if investigating `tools/release.py`:

```sh
rg --hidden --no-ignore -l -F -- 'tools/release.py' "$coord_dir/notes"
```

No matches means broaden by the relevant component/symptom if needed, not dump all
note bodies. An absent notes directory is normal; an access error needs attention.
Read titles/status/affected paths from the matching files, then only the relevant
notes in full. Do not print sibling result summaries merely to discover a topic.
This filter is for knowledge discovery only: registry ownership review still
includes **every record and whole claim**, regardless of task relevance.

## Check your inbox

Choose your session ID before the startup inbox check. With the already-verified
board path, this read-only discovery example distinguishes a missing inbox from
failure. It creates no directory and never marks a nonempty inbox processed:

```sh
python3 -B - "$coord_dir" "$session" <<'PY'
from datetime import datetime, timezone
from pathlib import Path
import json, sys
board = Path(sys.argv[1])
session = sys.argv[2]
if not board.is_absolute() or not board.is_dir():
    raise SystemExit('Use the verified, accessible absolute board path')
if not session or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-' for c in session) or session in ('.', '..'):
    raise SystemExit('Use a valid session ID')
inbox = board / 'messages' / session
if (board / 'messages').is_symlink() or inbox.is_symlink():
    raise SystemExit('Unexpected inbox alias; inspect it')
try:
    names = sorted(p.name for p in inbox.iterdir())
except FileNotFoundError:
    if not board.is_dir():
        raise SystemExit('Board became inaccessible; inbox check incomplete')
    names = []
now = datetime.now(timezone.utc).isoformat()
print(json.dumps({'entries': names, 'listed_at': now,
                  'empty_check_completed_at': now if not names else None}))
PY
```

Permission errors, non-directory paths and unreadable messages are **not empty**.
For a large inbox, use the harness's paged filename discovery; a truncated list is
incomplete. Read complete new relevant message bodies, including every unresolved
request/receipt, before deciding what they change. Publisher staging names are not
delivered messages; unexpected final entries need investigation. Keep processed IDs
in task context or a compact reference in your own record when needed for resuming.
Do not rely on filename order or a filesystem timestamp to skip unknown messages.

After processing, record that actual time as `Last inbox check`; enumeration alone
does not advance it. A missing/empty inbox observed successfully does complete a
check. Recover any omitted decision-relevant message text after truncation. When
only some messages are processed, keep the last completed check time and name the
remaining work. Complete the startup check before first registration; the current
record format requires an actual timestamp, so do not invent one if that check is
blocked. Continue independent read-only work while resolving access. Retain the
last completed check time through unrelated record publications. Reading bytes is
not resolving a receipt: record the actual outcome. Messages arriving afterward
belong to the next checkpoint; do not
wait for hypothetical future acknowledgments after all required handoffs are done.

## Job and handoff checkpoints

Use the existing record, not a second activity journal. Before launching a build,
test or generator that may outlive a tool reply, prepare its claimed outputs and
stable input identity and publish the launch checkpoint. At the first return to
agent control, use the tool's explicit running/completion status; some tools return
handles even for completed jobs. Record a handle as active only while completion
is unconfirmed. A job may finish before the next check; report the
observed completion without inventing its exact finish time. Do not poll solely to
keep documentation busy or start a monitor the harness does not normally need.

| Event | Replace these existing fields together |
| --- | --- |
| Before launch | `Running jobs: launch pending; TMPDIR=/work/tmp/session ./build.sh test build --cli --build-dir /work/build/session; claimed outputs /work/build/session and /work/tmp/session; expected about 30s; handle unavailable until reply`. Next check is the planned result/inbox check. |
| Tool reports running job `job-17` | Replace launch pending with `Running jobs: build job job-17 via this harness on example-host; same claimed outputs; completion unconfirmed`. Keep other genuinely active jobs. A handle identifies the job, not the session worker. |
| Tool returns completion | If no other jobs remain, `Running jobs: none`. Progress says, for example, `13/13 CTest entries passed; 3 internal ELF cases skipped (patchelf absent); source/configuration and log reference`. Remove the resolved blocker and set Next action to the actual remaining work. |
| Nonempty inbox processed | Read and interpret each pending message, then capture Last inbox check. For a verified release notice, replace `waiting for owner` with `release REF inspected; acquisition pending`, and set Next action to prepare/acquire. Reading a notice does not itself acquire the file. |
| Shared summary released | Remove only that claim, record release/provenance/hashes and stopped writers, replace `awaiting`/`append next` with `released; separate evidence remains`, and stay active for that work. Notify after publication. |
| Separate evidence completed | Replace `evidence next` everywhere in current status with its completed result. Resolve remaining jobs/receipts, then close using the output rule below. Historical startup facts remain labeled as history. |

A short synchronous command that finishes within one invocation needs no fabricated
running phase afterward. Record its relevant result at the next checkpoint; keep
the launch status truthful if it might yield or block. Ordinary read/search calls
do not each need a checkpoint. Combine job, inbox and scope changes into one update
when they coincide. Never hold the registry mutex while a command runs.

Capture event times **after** observing the result or processing messages, not at
wrapper invocation start. A job result observed at `10:02:00Z` and a nonempty inbox
processed at `10:02:57Z` give those two event fields. Publishing the reconciled
record at `10:03:00.123Z` changes Updated only. An unrelated publication at
`10:03:30Z` retains both earlier event times. Do not infer meaningful progress from
changing a text section or mark an inbox processed by printing its bytes. Resolve
what the messages change first; combine those decisions with jobs, blockers and
Next action in the same snapshot. Fractional UTC times avoid invented clock ticks.
Clock uncertainty remains explicit. The checker cannot establish these facts.

Inspect the underlying test summary and exit result, not just a green wrapper or
the exit code of `tail`/`tee`. Distinguish internal skips and setup failures from
executed assertions. Before replacing a log that explains a fix, keep a distinct
relevant attempt log or a concise command/failure/correction summary in your existing
evidence. No need to retain every transient output or copy logs into the record.

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
- Task and approach: diagnose the reported build failure using the harness's normal research/tools, implement and validate the requested fix
- Checkout / coordination root (absolute physical paths): /work/pumpModem / /work/pumpModem/.agent-work
- Branch / starting HEAD / current HEAD: example-branch / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
- Starting worktree and index changes (including work owned by others): clean
## Current checkpoint
- State: active
- Updated (UTC): 2026-09-27T10:00:00Z
- Last meaningful progress (UTC): 2026-09-27T09:59:40Z
- Last inbox check (UTC): 2026-09-27T09:59:55Z
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
- At startup: clean worktree/index; initial scope was read-only diagnosis. Current ownership is in Claims held.
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
- Task and approach: diagnose reported build failure, implement and validate
- Checkout / coordination root (absolute physical paths): /work/pumpModem / /work/pumpModem/.agent-work
- Branch / starting HEAD / current HEAD: example-branch / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa / aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
- Starting worktree and index changes (including work owned by others): clean
## Current checkpoint
- State: done
- Updated (UTC): 2026-09-27T11:00:00.123Z
- Last meaningful progress (UTC): 2026-09-27T10:59:25Z
- Last inbox check (UTC): 2026-09-27T10:59:50Z
- Next check (UTC) / action: none / closed; no pending work or receipts
- Liveness mode / cadence: checkpoint / closed
- Last heartbeat (UTC), if supervised: none
- Run token / heartbeat file and writer, if used: none
- Owner process: unavailable
- Running jobs: none
- Closed (UTC), if terminal: 2026-09-27T11:00:00.123Z
- Delete after (UTC): 2026-10-27T11:00:00.123Z
- Retention exception: none
- Contact: messages/example-session/
## Claims held
None.
## Baseline and dependencies
- At startup: clean worktree/index; initial scope was read-only diagnosis. Current ownership is in Claims held.
## Progress and checks
- Example outcome: diagnosis found no source fix necessary; local probe and task-appropriate research/verification completed. No worktree/index changes or remaining validation.
## Blockers and handoff
- All writers stopped, claims released; no integration or receipt pending.
```

Use literal `none` for terminal Running jobs, with completed-job details in progress.
Empty claims are standalone `None.`, not a table or “none, except…”. Set real closure
time and deletion at closure plus 30 days; longer retention needs the explicit
[lifecycle exception](agent-coordination-lifecycle.md#delete-expired-sessions-and-unnecessary-history).
A closed ID cannot resume: create a new ID and acquire afresh. Legacy records need
not be migrated just to make the helper accept them.

### Finish output before releasing its claim

Finish the separate evidence/logs first; stop or join their writers, including
child processes, inherited output descriptors, `tee` and logging/cleanup traps.
Then prepare the terminal candidate and publish with stdout/stderr going to the
**harness response**, not into an artifact released by that publication. Shell
redirection opens a file before the command starts, and the publisher prints its
result after the record is installed. Both ends of that output lifetime need an
owner. Capturing output in memory is also safe; writing it later needs ownership.

The ordinary closing sequence is: finalize evidence → publish/verify closure →
clean only owned registry staging/lock → report through the harness. The registry
cleanup exception does not authorize more artifact/source writes. If another
output or integration genuinely remains, release the completed shared file but
keep that other scope claimed and the session active until its writers finish.
Do not add an intermediate closure or retain the shared file merely to log closure.

## Publish a record

The optional [publisher](../tools/agent-board.py) requires POSIX descriptor-relative
operations, no-follow path opens and same-filesystem hard links. Unsupported
platforms/filesystems fail without a direct-write fallback. Resolve real paths
before invoking it; it rejects symlinks in board/source paths. It provides complete
atomic visibility, not a daemon, ownership arbiter, recovery mechanism or guarantee
of power-loss durability. Windows/native harnesses can follow the manual protocol
with equivalent primitives; this helper is not cross-platform qualification.

For an existing session, prepare the **whole** replacement in its claimed artifacts.
Check its structure/content before acquiring the mutex. This is a proposal, not an
event that advances inbox/progress time or grants a claim:

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
[equivalent publication procedure](#equivalent-publication-without-the-helper) under your mutex after rechecking those exact
old bytes and all other claims. Preserve ownership, event times and unresolved
work; do not delete/re-register the record to evade validation or lose its claims.
Repairs to another or closed owner's metadata also require the lifecycle procedure.
The helper can be used again once the current record uses the supported format.

The following publication commands run **only inside your already-held mutex**.
Acquire it by one exclusive `mkdir "$coord_dir/registry.lock"`. Write its owner
metadata immediately, including a plain `- Session: YOUR_ID` line, host, UTC,
lock-holder PID/start identity when available and intent. Review all current claims
and relevant releases again under that mutex. If a prior snapshot changed, release
and reassess; do not hold the mutex while investigating or waiting for another agent.
Finalize Updated from the current UTC clock immediately before publication; keep
actual inbox/progress times and reconcile current jobs, blockers and Next action.
Refresh a prepared release's resulting hash from the verified edit. Do not reuse
an old timestamp just because a proposed record passed earlier. The publisher
validates the final bytes again; equivalent manual publication must do the same
checks. This last metadata update is part of publication, not another checkpoint.

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
Never recursively remove an unfamiliar/nonempty lock. Check failures through the
caller as described below. The helper does **not** acquire/release the registry
mutex, scan other claims, or close another session.

### Stop dependent work on publication failure

Treat publication as a checked operation. A separate tool call with its result
inspected before the next dependent write is sufficient; no extra approval or
checkpoint is required. In a combined script, explicitly gate **every** dependent
mkdir, redirection, generator and job launch. Newlines, semicolons, the last command's
exit status, or `set -e` alone are not a reliable substitute.

This shell control-flow pattern uses caller operations, **not new helper commands**.
`publish_and_verify` includes the already-required complete claims/facts review,
checked publisher invocation and exact saved-byte comparison inside the owned
mutex. `release_owned_mutex` performs only verified owned cleanup and fails on
uncertainty. Each operation must propagate its own subcommand failures:

```sh
if publish_and_verify; then
  release_owned_mutex || exit "$?"
else
  publication_status=$?
  if ! release_owned_mutex; then
    printf '%s\n' 'Owned lock cleanup incomplete; inspect before retrying.' >&2
  fi
  exit "$publication_status"
fi
# Only an active session with verified published claims reaches this operation.
create_claimed_outputs_and_launch
```

Preserve the failing command's status in the `else` branch; `!` inverts it. Do not
let successful cleanup mask failure. In Python, use
[`subprocess.run(..., check=True)`](https://docs.python.org/3/library/subprocess.html#subprocess.run)
inside the owned-lock cleanup context and leave the exception uncaught until after
dependent work has been prevented. The shell example follows
[conditional execution](https://www.gnu.org/software/bash/manual/html_node/Conditional-Constructs.html)
and [command-list status](https://www.gnu.org/s/bash/manual/html_node/Lists.html).

A nonzero result can occur **after** atomic publication, for example when deleting
the private staging link or printing the result fails. Stop dependent work and
inspect actual record/message bytes and ownership before retrying; do not assume
the old state remains, restore an old claim, or recompute a hash to bypass rejection.
A complete terminal record stays terminal even if its closing command reports an
error; new work requires a new ID and fresh acquisition. Inspect an uncertain send
before choosing another message ID, so a successful delivery is not duplicated.
Retain failure evidence only within scope still owned after that inspection.

### Equivalent publication without the helper

Use equivalent filesystem primitives only where their required semantics are
supported. Reusing the existing helper avoids reimplementing these steps, but is
optional; a custom wrapper must preserve all of them, not just atomic visibility:

1. Prepare the complete body in memory or already-claimed artifacts. For records,
   validate the full record and transition, not only changed fields; the checker
   is optional, its invariants are not. Keep the exact reviewed old bytes/hash.
2. For registration or changed claims, exclusively create the mutex and record
   session, host, UTC, lock-holder role, PID/start identity when available and intent.
   Recheck all complete claims, provenance, stopped writers, target bytes and facts
   under it. Investigate uncertainty after releasing your mutex; do not wait in it.
   Messages need no registry mutex or claim scan. Unchanged-claim record updates
   may omit it under the workflow's single-writer rule.
3. For records, finalize Updated without replacing actual event times and revalidate
   final bytes. Stage complete, closed bytes on the destination filesystem, outside
   `sessions/`: records inside the owned mutex, or already-claimed artifacts for a
   mutex-free update; messages in their destination inbox as private staging files.
   Reject unsafe aliases/nonregular paths. Recheck directory identity, owned lock
   identity when applicable, and the expected old record before publication.
4. Use atomic no-replace publication for new records/messages; atomic replacement
   for the exact reviewed existing record. Exclusive creation followed by writing
   is not complete publication. The mutex-free operations retain the same
   applicable validation and atomicity checks.
5. Verify saved bytes. Preserve errors through narrow owned cleanup and follow the
   uncertain-result procedure above. Never delete foreign staging/locks or use
   direct writes as an unsupported-primitive fallback. Use the closure output rule
   for every path capable of writing after the record transition.

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

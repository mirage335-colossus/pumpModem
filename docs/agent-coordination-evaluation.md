# Preparing and evaluating coordination exercises

Read this when running a coordination study, not during ordinary development.
The [fifth](agent-coordination-rerun5.md) informs the preparation safeguards. The
[sixth](agent-coordination-rerun6.md) and [seventh](agent-coordination-rerun7.md)
reruns used unchanged guidance; the
[eighth](agent-coordination-rerun8.md) tested the subsequent revision. Use the
[coordination workflow](agent-coordination.md) for the evaluator's own files,
fixtures, observers, builds and handoffs too.

## Prepare before dispatch

- Give at least five fresh workers problems drawn from different historical
  commits, with behavioral requirements and complete externally visible output
  contracts. State required artifact fields and meanings; withhold solution
  commits, patches, private helper names and held-out test contents. Freeze tasks,
  guidance, acceptance criteria and scoring before work starts. Do not teach
  missing requirements through incremental failure hints.
- If the user asks to evaluate independent web research, **require each worker
  to actually use available web tools and primary sources relevant to its task**.
  Do not reuse permissive “may research” wording. Give no solution queries or
  source URLs that reveal the repair. Require a compact source/access-date/local-
  applicability note and distinguish a reported lookup from captured tool evidence.
  A citation copied from another note, successful local repair or a later coached
  lookup does not satisfy the original independent-research criterion. If access
  is unavailable, report it as unmet/blocked; do not silently replace the worker.
  This is a requested exercise criterion, not a change to ordinary harness policy.
  Matched controls must receive the same research/tool instructions, available
  capabilities and task scope. Do not infer normal browsing prevalence from a
  trial whose prompt newly mandates it; page opens/finds and search queries are
  distinct valid lookup evidence, and raw call count is not research quality.
- Materialize each entire historical parent tree from Git's tracked inventory,
  including vendored files that current ignore rules might exclude. Verify file
  paths, modes, symlink targets and content against that tree before creating the
  anonymous commit. Preserve tracked ignored inputs; a clean `git status` or
  tools/tests-only comparison is insufficient. Record guidance overlays separately.
  Check submodule/LFS materialization if applicable; record unavailable prerequisites.
- Run a small configuration/prerequisite preflight and baseline/fixed controls
  before dispatch. Verify held-out test hashes against their original revisions.
  A fixture defect is an evaluator failure to fix before reuse, not an agent
  repair failure. Keep any unavoidable coverage gap explicit.
- Give each worker absolute physical checkout and shared-board paths, require
  explicit tool working directories, and provide current AGENTS/workflow files.
  Supply board ignore verification when its containing checkout is outside the
  worker's permitted reading scope. Keep helpers available at documented paths.
  Use fresh conversation context. Restrict parent/history, peer code and evaluator
  artifacts when the harness permits; otherwise disclose instruction-only blinding.
  If exposure occurs, stop and exclude that attempt, preserve its disclosure and
  replace it with a fresh worker/snapshot. Count every dispatched attempt.

Save the exact dispatched prompt and hash, guidance/helper hashes, tracked tree
inventory, public contracts and scoring rubric. A changed HEAD alone does not
mean guidance changed. Compare those inputs before describing a run as an
improvement trial; identical guidance yields a replication. Keep evaluator-only
guidance, historical reports and held-out repairs outside routine worker reading.

## Observe useful work and coordination together

Keep concurrent windows and task difficulty comparable. Record actual concurrency
and waves when capacity is limited. Include independently researched repairs and
meaningful tests alongside covering-directory claims, ownership relays, unchanged
content across owners, free-scope mismatched reply IDs and retained terminal claims.
Synthetic recovery fixtures must never justify reclaiming a real owner's work.
Source repairs in isolated checkouts plus a shared ledger test a narrower risk
than simultaneous source edits, Git mutations or real interrupted jobs; label it.

Start observers before edits. Preserve baseline/index state, record and output
changes, release references, candidate hashes, errors and final claim disposition.
Sampling can miss brief ownership transitions; an output first seen after release
is not proof of an unclaimed write. Report uncertain attribution and unobserved
paths. Count guidance reminders and other evaluator interventions separately.

Measure helper adoption and overhead: whether the bounded reader/checker was used,
setup trouble, coordination read volume/tool calls or elapsed time where available,
and time blocked by ownership. Observe web research, primary sources, local
applicability and repair/test outcomes. A post-run survey is self-report, not a
complete tool trace. If comparing research effectiveness or distraction, use a
matched no-coordination control or clearly state that no such conclusion follows.

Count publication-helper use, incomplete scans, candidate files exposed in
`sessions/`, partial/direct message writes, missing-inbox retries, truncated reads,
unknown-path guesses, stale current status and closure retries. Preserve whether
each observation came from a tool trace, sampling or a post-run self-report. Do
not call exclusive creation an atomic complete publication. Use existing messages
and records to distinguish entry readiness, request, acquisition, append, release
and receipt times; separate ownership waiting from independent repair/test time.
Include a case where unrelated closure/evidence work remains after a shared edit;
the worker should release that file promptly while keeping its session active.
Include launch/yield/completion, a missing/empty/nonempty inbox, and a candidate
prepared before publication. Compare observed job results and processed message IDs
with the current record, not merely its parseability. A changed Updated is not
proof of publication-time freshness. Capture event times/tool traces where the
harness permits; do not impose extra worker messages or hold times to make sampling
easier. Separate coordination reads from ordinary research/source reads and record
truncation recovery and scope, not just whether a helper was used.

For publication primitives, supplement sampling with deterministic concurrent
writer/reader and injected-failure tests. Readers should see absent or complete
messages and complete old/new records; duplicate publication must not replace
bytes. Keep unknown/stale/retained-claim scenarios too. Primitive tests do not
replace blinded worker exercises or qualify other operating systems/filesystems.

## Score the finished candidate

Freeze candidate bytes before evaluation; do not modify them in response to
held-out failures. Compare public behavior and emitted artifact schemas with the
declared contract. Keep raw historical failures; label any private-interface/mock
adaptations, exclusions and test-fixture repairs separately. Verify that adapted
tests still exercise real candidate behavior. Baseline/fixed controls must
discriminate the bug; passing both is not evidence of a repair. Do not imply actual
Windows, device or live-service qualification from a simulation on another host.
Audit coverage against each declared public behavior, not test counts: a publication
success probe, for example, must check intended inventory and final state as well as
destination ID. Preserve original results and label any post-freeze supplemental
probe separately; do not call it preregistered evidence or send its findings back
to a finished repair worker. Retain a compact useful failure/correction record
instead of overwriting the only evidence of a fixture or environment error.

Report passed, failed, skipped, setup-blocked and untested scopes separately.
Score repair correctness, requested independent research, required validation and
coordination separately. A collision-free ledger is not an overall pass if any
requested dimension is missing. Retain failed attempts and unfulfilled criteria;
do not repair the score with undisclosed replacements or post-result coaching.
Investigate environment failures with a discriminating probe and record what is
established versus hypothesized; zero executed assertions are not a suite pass.
Record unresolved fixture defects as comparison limits. Compare like measurements
and disclose changes in prompts, interleavings, tools and environment. Zero observed
lost edits in two runs does not establish a reduced collision rate, and an unused
helper cannot explain an improvement. Prefer a small actionable change supported
by the results over expanding routine policy. Keep detailed raw evidence in the
ignored board with the usual disposition and retention rules.

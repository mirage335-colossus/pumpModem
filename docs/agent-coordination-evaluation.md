# Preparing and evaluating coordination exercises

Read this when running a coordination study, not during ordinary development.
The [fifth rerun](agent-coordination-rerun5.md) motivates these checks. Use the
[coordination workflow](agent-coordination.md) for the evaluator's own files,
fixtures, observers, builds and handoffs too.

## Prepare before dispatch

- Give at least five fresh workers problems drawn from different historical
  commits, with behavioral requirements and complete externally visible output
  contracts. State required artifact fields and meanings; withhold solution
  commits, patches, private helper names and held-out test contents. Freeze tasks,
  guidance, acceptance criteria and scoring before work starts. Do not teach
  missing requirements through incremental failure hints.
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
  Use fresh conversation context. Restrict parent/history, peer code and evaluator
  artifacts when the harness permits; otherwise disclose instruction-only blinding.
  If exposure occurs, stop and exclude that attempt, preserve its disclosure and
  replace it with a fresh worker/snapshot. Count every dispatched attempt.

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

## Score the finished candidate

Freeze candidate bytes before evaluation; do not modify them in response to
held-out failures. Compare public behavior and emitted artifact schemas with the
declared contract. Keep raw historical failures; label any private-interface/mock
adaptations, exclusions and test-fixture repairs separately. Verify that adapted
tests still exercise real candidate behavior. Baseline/fixed controls must
discriminate the bug; passing both is not evidence of a repair. Do not imply actual
Windows, device or live-service qualification from a simulation on another host.

Report passed, failed, skipped, setup-blocked and untested scopes separately.
Investigate environment failures with a discriminating probe and record what is
established versus hypothesized; zero executed assertions are not a suite pass.
Record unresolved fixture defects as comparison limits. Compare like measurements
and disclose changes in prompts, interleavings, tools and environment. Zero observed
lost edits in two runs does not establish a reduced collision rate, and an unused
helper cannot explain an improvement. Prefer a small actionable change supported
by the results over expanding routine policy. Keep detailed raw evidence in the
ignored board with the usual disposition and retention rules.

# Concurrent development: procedures and evidence

[Documentation index](../README.md) · [Build and test](development.md) ·
[Validation and research evidence](evidence.md)

## Current workflow and reference

| Document | Purpose |
| --- | --- |
| [Contributor requirements](../../AGENTS.md) | Entry requirements for shared development and validation. |
| [Coordination workflow](../agent-coordination.md) | Sessions, claims, dependencies, handoffs and checkpoints. |
| [Coordination recipes](../agent-coordination-recipes.md) | On-demand checked operations, bounded readers, record/message examples and guarded edits. |
| [Lifecycle](../agent-coordination-lifecycle.md) | Liveness, recovery, retention and cleanup procedures. |
| [Evaluation guide](../agent-coordination-evaluation.md) | Preparation and interpretation when conducting a coordination study. |

Task-relevant temporary knowledge may also exist in the gitignored
`.agent-work/notes/` directory, or the agreed alternate board. It requires explicit
local discovery as described in the workflow and is not distributed with these
documents. Its evidence and retention rules remain those of the workflow.

## Studies and the reasons for later changes

These are dated empirical reports and design/implementation history. They retain
failed attempts, isolated repair outcomes and limits; their results do not
qualify application releases or prove behavior for every harness. They are
available when relevant, not additional routine startup reading.

| Document | Context |
| --- | --- |
| [Original blinded exercises](../agent-coordination-exercises.md) | Initial five-worker exercise, methods, outcomes and limitations. |
| [Rerun 1](../agent-coordination-rerun.md), [2](../agent-coordination-rerun2.md), [3](../agent-coordination-rerun3.md), [4](../agent-coordination-rerun4.md), [5](../agent-coordination-rerun5.md) | Successive guidance/checkpoint/handoff evaluations; each preserves its own baseline and repair evidence. |
| [Rerun 6](../agent-coordination-rerun6.md), [7](../agent-coordination-rerun7.md), [8](../agent-coordination-rerun8.md), [9](../agent-coordination-rerun9.md), [10](../agent-coordination-rerun10.md) | Follow-up trials, repeated observations, failures and coverage limitations. |
| [Guidance improvements](../agent-coordination-improvements.md) | Rationale and changes arising from the trials, with historical reports retained. |
| [Hardening and assurance](../agent-coordination-assurance11.md) | Helper/dependency hardening after run 10, validation and remaining limits. |
| [Shared-source stress study](../agent-coordination-shared-source-study.md) | Shared-source/process study and then-unimplemented proposals. |
| [Decentralized hardening and validation](../agent-coordination-decentralized-validation.md) | Subsequent implementation, regressions and validation of findings from the shared-source study. |

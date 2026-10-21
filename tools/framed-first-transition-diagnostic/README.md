# Fixed-origin first-loss adapter for current 4e90487

Parent assignment remains `instrumented-gui-core-failures-20261018`. This is a
diagnostic artifact, not a production fix or qualification. The original native
run 39549988592/job 119056743159 failed `test_live --ci-functional-clock` after
115.04 seconds in framed mode with search-limited health, origin
1792561396.3857498, 269 owners, 305 admissions, 36 retirements and 154676 samples.
The earlier original Symbol_Burst GUI gate passed; no causal equivalence with
earlier failures is assumed.

Run the adapter against an exclusively owned, exact source archive:

```sh
python3 prepare.py --source-root /absolute/private/source --output-dir /absolute/private/evidence
```

It validates the complete 842-file `src/include/tests/cmake/CMakeLists.txt`
preimage inventory and every postimage before writing. Only six allowlisted
source files change. It writes an applied manifest, final source inventory,
preimages, forward patch and reverse patch to the fresh evidence directory.
There are no Git operations, build operations, downloads or test launches in
the adapter. Source replacements are atomic per file; a later write failure
rolls back its own changed preimages and records the rollback result.

The source input is exactly commit
`4e9048748e8d34ca4f90ef1dd01bd8d4588759b2`; this identity comes from the parent
source archive. SHA-256 pins independently gate its compile-relevant bytes.
The diagnostic deliberately replaces the default epoch helper with the failed
origin. It does not supply a custom EpochClock: the original default media-clock
mode remains enabled. Current production code is the preimage, including layout
receivers, retirement, template, replay and snapshot changes; old N production
postimages are never copied over it.

The test-only CLI is:

```sh
DATAPUMP_FIRST_TRANSITION_CENSUS=0 test_live --ci-functional-clock-trace /absolute/off.jsonl
DATAPUMP_FIRST_TRANSITION_CENSUS=1 test_live --ci-functional-clock-trace /absolute/on.jsonl
```

Use the same binary, configuration, fixed origin and environment for both runs.
The wrapper opens an outer trace Scope and enters the exact existing
`--ci-functional-clock` branch. All its assertions and normal/instrumented
40/120-second deadlines remain. Both modes use the same event filter. The only
observer selector is census 0 or 1; other values are invalid diagnostic setup.
Original application exit/stdout/stderr and health failures must be retained.
Warnings cannot waive health failure. No capacity, retirement, deadline,
reclaimer, optimization or envelope switches are introduced.

`checks6/result.json` records passing offline pin rejection, six-file scope,
postimage readback, independent reverse-patch roundtrip, all ten current
coverage-loss mutations, single-first-loss guard, capacities and entrypoint
checks. The generator also checks that removing the observation-only retirement
edits reproduces the current production predicate exactly. `checks/` and
`checks2/` retain two validation-setup failures (POSIX patch retained the removed
header as an empty file; an initial regex counted a health read as a bank store).
`checks3/` retains the passing pre-clock-counter candidate; `checks4/` retains
the passing clock-counter candidate before common OFF/ON PCM witnesses.
`frozen-v1/` preserves manifest `09aa255d...a7f374c`, patch
`5ee95eb0...c565718` and all six postimages before independent review;
`checks5/` preserves its passing source-only evidence. Review found that the
inherited extra native track-radius and owner initial-search queries can advance
a Burst cursor. The corrected observer skips those two extra queries for Burst,
marks their fields unavailable and increments `unavailable_queries`; such a
capture is incomplete. Original retirement queries and their journal remain.

C++ compilation, actual OFF/ON executions, trace closure, runtime ledger
reconciliation and causal interpretation are unexecuted here. The parent owns
review, hosted build and integration.

## Reading the capture

The producer is `payload/src/first_transition_probe.hpp`, adapted from the
validated N diagnostic. At most one full capture is attempted process-wide:
the first false-to-true bank coverage condition. Ordinary successful fallback
selection does not trigger an earlier census. The ten source stores have
distinct before-store causes, including both aggregate modem-limit stores.

Trace numeric booleans are integers 0/1. Use exact event names, capture ID,
bank allocation-account identity, revision, sample and case sequence to join;
pointer values have process-local meaning only. A bank object address is not a
stable join identity. Scope covers initial `make_bank` and every `feed_bank`.

The census emits begin, candidate, shared, owner, pattern-state, component,
optional, track and terminal summary rows. Public owner workspace terms sum
exactly to each owner workspace. `first_transition_shared` separately itemizes
shared control, allocator nodes, pools, source/cache owners, capture reservation,
deferred input and the original 65536-byte control allowance, once per bank.
Its `bank_ledger_matches` compares shared sum plus current owner sum with the
independently recomputed current bank charge. Reservations and active shared
bytes are qualified explicitly and must not be added to heap capacity totals.

Stored bank charge can temporarily differ from actual current owner charge
during a feed. `pending_accounted_*` identifies the original accounting term;
explain its difference rather than treating it as corruption or changing it.
Candidate state and an unadmitted candidate's storage remain separate from
resident accounting. Constructor requirements, grants, last reclaim and last
single-peer trim retain their original knownness and identity guards.

Retirement rows are the predicates actually evaluated by the original refresh,
including early synchronized/search-incomplete/frozen-Burst paths, expiry only
when reached, and the delayed frontier only when queried. A journal lookup is
not a reevaluation at capture time; its sample/identity/revision are emitted.
An owner absent from that journal remains explicitly unknown. Journal time and
stored owner admission/last-confidence/search/integration scalars are emitted
for reconciliation without reevaluating expiry. Native tracks
contain current pending/confirmed bits, source/sample/bin coordinates, complete
symbol absence, continuation caps and non-final readiness geometry. A ready
input endpoint alone does not prove that the original schedule could run it.

PCM SHA-256 covers original float32 object bytes at every successful committed
feed boundary, scoped to source quota identity and revision. A wrapper-only
trace-active flag enables exactly the same bounded hashing and cumulative
`first_transition_input_witness` rows in OFF and ON. Case stamps update in both
modes. `first_transition_input_summary` records cumulative commits, attempts,
8192-row witness limit, omitted/error/context-overflow counts at joined Session
destruction, including OFF. The last witness for each bank/revision is its final
reached prefix. The begin row also
hashes the triggering feed span. Require little-endian host, valid continuous
prefix, no prefix overflow and the exact bank/local coordinate fields before
joining waveforms. Compare matching reached sample boundaries in OFF and ON
before making an overhead attribution. Different hashes retain census evidence
but prevent a matched-input overhead claim. A hash is an input witness, not
proof of equal scheduling. The census, retention journal and private inspection
remain enabled only in ON.

Bounded correlator inspection exposes present public charge, stored hypothesis
state and selected independently owned vector capacities. It does not resolve
diagnostics, score samples, manufacture absence or change reception. Correlator
private breakdown/readiness is incomplete and explicitly marked by
`correlator_component_breakdown_incomplete>0`; a capture containing such an owner
cannot be called a complete native-internal census. The parent agreed to retain
this limitation unless the actual first-loss capture exercises this branch.

Require owners==owner_total, tracks==track_total, exactly one appropriate state
per owner/candidate, exact owner/shared sums, terminal summary, zero unavailable
queries, zero context and
callback errors, zero trace drops, complete sequence set, matching footer and
external trace seal. Caps are 512 owners, 128 tracks per owner, 131072 capture
rows, 16 PCM prefix contexts and 512 continuation contexts. Any cap, unknown
required state, queue timeout, missing terminal row or dropped row means
incomplete evidence, never a pass. Missing capture alone proves no absence.

Capture CPU/wall spans its queries, emission and bounded backpressure, excluding
its terminal summary, pretransition hooks/journal/hashes/filter and writer CPU.
Dedicated measurement clock reads, backpressure clock reads and per-row trace
clock reads are separately counted. Hook CPU remains explicitly unmeasured;
whole-child OFF/ON measurements and matching boundaries are still required.
The current trace implementation, queue limits, footer and seal stay intact.

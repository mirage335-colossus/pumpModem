# Current-source GUI stop critical-path capture

This is the first-stage, **unmatched** diagnostic for
`gui-stop-current-identity-wave`, under the original Sol-owned
`instrumented-gui-core-failures-20261018` diagnosis. It cannot establish ON/OFF
instrumentation overhead, load causation, or the failed core run's waveform.
The legacy `gui-stop-differential.py` source pins and preparation history are
unchanged. Its generic `trace_seal` reader is reused only for file closure.

## Integration and invocation

Use a fresh isolated current checkout/archive. `pins.json` binds actual a6 GUI
and runtime read dependencies; metadata-only newer commits are recorded. Do not
relax a stale pin. The preparation manifest records the whole original source
inventory, actual Git HEAD when available, original and changed files, tool and
support identities. Archives without Git metadata are suitable for mechanical
checks, but the runner should retain their separately authenticated commit.

```
python3 -B tools/gui-stop-current-wave.py prepare --root "$PWD" --report "$PWD/build/gui-stop-current"
```

Build only `test_gui_controller` and `test_scheduled_parallel_preparation`, using
the existing standard GNU11 Debug/O1 ASan+UBSan, portable OFF recipe. Export
`compile_commands.json`; the runner checks actual GUI instrumented/SAN flags and
cache values. No application build or runtime is performed by `prepare`.

```
python3 -B tools/gui-stop-current-wave.py run --root "$PWD" --report "$PWD/build/gui-stop-current" --binary "$PWD/build/test_gui_controller" --load-binary "$PWD/build/test_scheduled_parallel_preparation" --condition preparation --capture on --timeout 600
python3 -B tools/gui-stop-current-wave.py restore --root "$PWD" --report "$PWD/build/gui-stop-current"
```

Always retain artifacts and run restoration even when `run` exits nonzero.
Preserve the original GUI exit, timeout, stderr and all assertion failures. The
GUI executes its **entire original noarg main**. No predicate, 5/10-second stop
clock, 6-second absence, protected tail, key generation, channel RNG, epoch
provider, source scheduling or transmitter endpoint is changed. There is no
extended post-deadline observation and no warning exception. A failed assertion
can therefore destroy the Session before natural EOF/GUI acknowledgement; that
is a valid captured prefix, not proof those later events never occur.

For a future unloaded natural-input control use a fresh report and
`--condition none`; `--capture off` disables the C++ observer but retains the
external proc sampler. Distinct natural processes remain **unmatched**.

## Processes and bounds

One preparation child runs its original noarg program. The runner waits at most
120 seconds for actual `arithmetic.serial` entry, then starts the GUI. It retains
the load until the entire GUI process exits, sends TERM then KILL to the load's
process group and reaps its leader. Phase crossings/early completion are explicit;
they invalidate a uniform-first-phase load condition. Deliberately terminated
preparation is a partial diagnostic, never a preparation test pass. GUI external
timeout is 600 seconds by default (allowed range 30–900); it is independent of,
and never replaces, original test deadlines. Finally blocks close both children.

The C++ observer allocates a fixed 4MiB PCM prefix, at most 8192 fixed scalar rows,
4096 partitions and 64 metadata strings of at most 15000 bytes. It never writes
files from read/feed hot paths. It flushes only after EU Controller destruction
has joined all Session tasks. The trace queue is drained below half capacity
before each final emission, under one global ten-second emission-admission deadline;
missing/dropped/overflow/error flags invalidate complete measurement. This
teardown cost is outside the original stop timer, but inside whole-process wall.

The Python sampler polls at 0.2 seconds, caps 4501 observations and 64 TIDs per
process, and records actual timestamps. It retains raw per-TID CPU ticks,
identity/start ticks, affinity and schedstat, cgroup CPU/throttling/pressure and
memory fields. `sched_schedstats` is recorded: raw zero delay with accounting
disabled is not evidence of no queueing. Proc races/unavailable fields remain
explicit. Sampler wall/CPU covers proc reads, excluding final JSON serialization;
C++ observer sums overlap and exclude writer/serialization, snapshot throttle/locking, scope setup and
clock-call self cost. **Neither is a full overhead bound or matched estimate.**

## Producer and evidence contract

`run/trace.jsonl` has the ordinary strict begin/footer/external finalization seal.
The private allowlist is applied before sequence assignment and retains only
`trace_begin` plus `gui_stop_current_*`; existing hot trace events are suppressed.
All coarse scalar records are serialized after join. `observed_wall_ns`,
`observed_thread_cpu_ns` and `tid` describe observation time, not serialization.
`capture_index` is contiguous insertion order; cross-thread timestamps are not a
causal total order. Thread CPU may only be subtracted for the same TID.

Events are `case_begin`, `source_read_begin/end/result`, `rx_feed_begin/end`,
`publication_begin/end`, `request_before_lock`, `request_lock_acquired`,
`request_installed`, `endpoint_before/installed`, `natural_eof`,
`internal_completion`, `noise_factory_begin/end`, `snapshot_status`, `gui_ack`, `strict_stop_observation`, `case_joined`,
`detail`, `pcm_partition`, and terminal `summary`, all with that prefix.

* Request-before-lock stores a coherent **last completed source-read witness**
  and its read ID/time. `current_read_id`/`read_inflight` distinguish a newer read.
  It does not read mutable transmitter state from the GUI thread. Current
  in-flight generated/transmitted coordinates are unknown. The source snapshot
  has its own time and can be taken after API entry.
* Source-thread endpoint events contain exact physical generated
  (`samples_emitted`), channel-transmitted and endpoint (`total_samples`)
  coordinates at that source boundary. These are separate from the published
  floating source-seconds counter. They are never inferred from one another.
* Read results name channel address, read ID, serial, source first/end, output
  count, capacity and unwinding status. Read identity is process-unique. Channel
  pointers alone are not lifetime identities; use read/configuration generations
  and actual counters/partitions together.
* Feed intervals include bank source-quota identity, revision/generation and bank
  ingested/committed count (not a processed frontier). Only non-unwinding, unchanged-revision, exact count advancement is
  committed. All other attempted spans remain uncertain; no PCM or absence is
  invented. Hash concatenation is of committed spans only. Partition fields
  preserve each bank/channel coordinate; do not treat a reset as continuity.
* After a stop request, the existing GUI snapshot/ack path buffers `snapshot_status`
  at most every 200ms, plus first and final/ack observations. It records ingress,
  raw hardware processed/queued/decoding disposition, resident instances, health,
  backlog/oldest age, published source seconds and simulation tail/compute state.
  `hardware_frontier_applicable` and `hardware_queues_applicable` are false in
  Simulation: their raw zeros do **not** prove no backlog or a processed frontier.
  `bank_processed_frontier_available` is explicitly false; no per-owner scan or
  new receiver-frontier accessor was added. `status_hook_wall_ns` marks the hook
  reading the already-published Snapshot, not the receiver's publication time.
  First/final status can be closer than 200ms. No status is forced after original
  teardown, and failed setup before stop can have no post-request status.
* Publication scopes include early-return/no-op calls; an interval does not prove
  a plot was changed. Interval CPU is calling-thread CPU, and a straddling interval
  includes pre-request work. Pool-worker CPU is intentionally not attributed by
  the C++ scope; join it to proc TIDs or retain it as unaccounted.
* Details contain selected receiver geometry, source factory geometry and prepared-noise configuration witnesses,
  plan shifts/guards, key MACs, spreading seeds, channel parameters and serialized
  RNG/distribution state. Only prepared-noise details inside a same-TID source
  `noise_factory_begin/end` bracket identify that actual TX; unrelated GUI
  preparation details must not be conflated. No raw temporary root is exported. The copied keyfile
  is the synthetic test-created file only; this is not general user-key tracing.

`committed-input.f32` is a bounded native-endian float prefix, up to 4MiB. The
summary contains a full SHA256 over all committed spans, exact sample counts,
truncation and uncertainty flags. The runner independently checks partition
counts/offsets and, when untruncated, the actual file hash. This enables later PCM
receiver replay; it does **not** promise exact transmitter regeneration without
the unavailable temporary root or reconstruct the old failed waveform.

The runner records stdout/stderr, original exit, child CPU/RSS and process closure,
source/cache/compile-command/binary/tool identities before and after, phase logs
and proc samples. Source, trace and PCM validation do not convert a failed GUI
into a pass. Coarse capture only localizes work versus waiting within this run.
EOF/ack missing because the original assertion aborted is reported as unobserved.

## Remaining acceptance

This tool requires independent source review, actual C++ build and one standard
runner invocation. Python/mechanical preparation checks do not establish C++
syntax, runtime safety, observer cost or successful GUI completion.

## Native receiver CPU partition wave (schema 2)

The completed first-stage capture at commit b721005 remains schema 1 with its
own frozen tool identity. Schema 2 adds only private diagnostic scopes to the
native receiver and propagates the submission's profiler stage to joined search
workers. `native-substages.json` uses whole-file SHA preconditions and disjoint
original-line edits applied in descending order, followed by exact postimage SHA
checks. Failed sequential-context recipes remain retained in the assignment's
before-Sol evidence.

GUI child only enables the existing `DATAPUMP_RX_CPU_PROFILE=coarse`; the partial
preparation child retains its normal configuration. No arithmetic, search bounds,
thread count, scheduling, cache policy or test assertion changes. Alternate
native scoring function scopes count attempts, including their fast rejection.
The fallback dispatch stage counts wrapper calls; its nested PatternCorrelator
Context records callee work in the existing categories. Streamed/bounded private
value generation remains inside its enclosing native backend stage.

Buffered `receiver_selection` details identify actual family/backend choices.
Four process-cumulative CPU ledgers are taken at Case start, stop request, strict
observation and joined teardown; pre-stop failures may omit request/observation.
They never reset counters. Request/observation snapshots can cut active Contexts
and are not atomic intervals or exact stop CPU totals. Joined teardown must be
complete/quiescent without clock, nesting or overflow errors. The captured feed
spans bound carry-in and cancellation work. Keep source and worker CPU separate
from wall duration; compare completed-context deltas with the proc sample bounds
and retain any signed unexplained residual. No matched overhead or load-causation
claim follows from comparison with schema 1's natural input. Existing observer
self-cost and profile clock-read counts are retained; full instrumentation cost
remains uncalibrated. This wave localizes work and does not qualify performance.

## Executor subdivision and protected ledgers (schema 3)

The native CPU capture now subdivides `execute_fft_search_cpu` into validation,
per-job template setup, streamed/private template values, forward transform,
product sweep, inverse transform, score sweep, and the alternate drift path.
These are coarse scopes outside bin/start loops. The enclosing
`native_fft_search` is residual dispatch/control/wait work; the sum of its inner
stages is the executor workload, rather than FFT arithmetic alone. Calls count
phase entries, not samples or evidence evaluations. Existing floating-point
operations, cancellation, jobs, workers, and assertions are unchanged.

CPU ledgers have 16 reserved slots independent of 256 bounded general detail
slots. Both limits and first-overflow wall times are recorded; any overflow
still invalidates the capture. Ledger identity is `(boundary, request_ordinal)`:
the original successful GUI case issues a second request after its primary EU
stop observation. Every request must have a matching ordinal ledger, and joined
must match the final ordinal and remain quiescent. Critical ledger storage cannot
be consumed by later receiver selections. Request/strict snapshots can still
cut active Contexts; their differences are provisional completed-context deltas,
not an atomic CPU interval. Only the joined ledger establishes final totals.

The previous schema-2 run 39391135074 passed the entire original GUI main with
EU stop 9.177969929 seconds, but lost 13 general metadata records including its
joined ledger. That capture remains incomplete. Schema 3 does not reinterpret
it as qualification. No matched overhead or speed comparison is inferred from
natural-input runs; self-cost counters exclude clock/scope and scheduling cost.


The next leaf wave records each distinct executor inverse shape per physical
thread and request ordinal: transform size, legacy FP64 / borrowed FP64 / legacy
FP32 kind, and the borrowed kernel's actual vectorized selection. Shape records
are bounded/deduplicated; neither backend nor SIMD selection is inferred from
receiver-selection text. Timing samples one inverse per 257 calls per kind and
thread, dividing it into checks/setup, bit permutation, butterflies and inverse
normalization. The odd prime stride avoids selecting only one bit of a pair.
Scopes outside selected calls are disengaged in coarse mode. Their CPU counts
are sparse observations, not the complete inverse workload or extrapolated stop
latency. The existing enclosing inverse scope still covers the remaining work.

The completed prior wave 39395606466 on 5319139 retained all five CPU ledgers,
with zero overflow and a full original GUI pass (EU 9.986809925 seconds). Its
primary completed-Context delta was 20.683850150 CPU seconds across 9.988151231
wall seconds, with one active Context at each cut. Inverse FFT, scoring and
product used 8.603036679, 4.550112335 and 2.710410693 CPU seconds, respectively;
each had 328,450 calls. Private template generation and forward transforms had
zero measured calls. It did not distinguish the two inverse kernels or sizes.
Earlier normalization evidence on the exact current FFT source remains retained
and reusable; it does not prove which kernel this GUI executed.

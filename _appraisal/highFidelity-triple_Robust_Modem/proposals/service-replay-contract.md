# Opt-in native service replay

`--service-replay` is a flag for `receive` or `plan`. Its dependent options are
`--service-episodes 1|2` (default 1), `--service-spool-byte-limit N` (positive;
default checked `max_samples * 17`), and the optional paired
`--service-backlog-begin-slot N --service-backlog-end-slot N`. The interval must
have `begin < end < scored_slot_horizon`. Replay accepts chunks of 1–4096 rows.
The existing wall, sample, memory, worker and owner bounds continue to apply.
Two episodes accept wanted/noise/wrong-key/wrong-ordinal controls. Other controls
retain their existing single-episode semantics. No service options affect an
ordinary invocation.

`plan.service_replay` has `schema_version: 1`, `episodes`,
`wire_bits_per_episode`, `expected_message_count` (episodes for wanted,
interior-fade or capture-gap; zero for noise, wrong-key, wrong-ordinal or EOF),
`expected_message_intervals` (source schedule even for negative controls; objects with
`first_public_slot`, `stop_public_slot`), `slot_pattern` (D/Q source schedule,
all Q for noise), `maximum_spool_bytes`, `planned_spool_bytes`,
`fits_spool_budget`, `input_chunk_samples`, `backlog_begin_slot` and
`backlog_end_slot` (null when omitted), and `backlog_unique_baseband_seconds`
(the exact public-calendar pattern-start duration, null when omitted; do not
derive it from DATA bit duration). Each episode has the same prepared
payload, with its own globally addressed native bit source. Episode two starts
at `wire_bits + 2`; two-episode horizon is `2*(wire_bits+2)+1`. For the three-bit
`e` fixture this is DDDQQDDDQQQ, with message endpoints [0,5) and [5,10).
The channel continues once through both episodes and their actual quiet gap.
The fixed gap is rejected if the public tail bound cannot preserve its quiet
opportunities. A single episode preserves the original source and horizon.

`service_replay` is emitted after successful capture preparation and receiver
construction, including on partial replay. It repeats the plan's service fields and adds:

The channel's `input_sha256`, `hashed_samples` and `samples` retain the complete
sealed capture on interrupted replay: the digest equals `captured_input_sha256`
and both counts equal the planned horizon. Only `service_replay.verified_input_sha256`
and `replayed_samples` describe the verified replay prefix; accepted receiver
input remains `outcomes.accepted_samples` / `receiver.input_stop` and may be
smaller than that prefix after backpressure. Selected-observation verification
still reflects the replayed portion and can remain incomplete after interruption.

- `wall_seconds`, nullable `process_cpu_seconds`: full replay service from before
  receiver construction through cancellation/drain, digest finalization and
  receiver destruction. Rendering is excluded. `unique_baseband_seconds` is the
  declared horizon divided by sample rate, and
  `service_seconds_per_baseband_second` is wall divided by this duration.
- `spool_read_and_verification_seconds`, nullable
  `spool_read_and_verification_process_cpu_seconds`: the measured read/rewind/hash
  subset, included in service. Process CPU in that scope can include concurrent
  scorer work and is not subtracted to claim DSP-only CPU.
- `render_wall_seconds`, nullable `render_process_cpu_seconds`,
  `spool_write_seconds`, nullable `spool_write_process_cpu_seconds`: capture and
  its write subset, excluded from replay service.
- `captured_input_sha256`, `verified_input_sha256`, `digest_match`,
  `replayed_samples`, `work_complete`. Digest match requires the entire horizon;
  work completeness additionally requires completed status, ordered complete
  all-valid canonical receipts with expected epoch/ordinal and input-ended state.
- `communication_complete`: target controls require every expected message with
  exact payload and framing, zero false DATA/premature completions, and complete
  work. Noise/wrong-key/wrong-ordinal require no accepted messages or false DATA
  and complete work. This is distinct from service timing eligibility.
- `peak_active_owners`, `mean_busy_hardware_fraction` (nullable CPU/wall/H),
  `receiver_peak_reserved_bytes`, `outside_receiver_future_reserved_bytes`,
  `spool_residency_reserved_bytes`, `peak_reserved_bytes` (sum of those three),
  `spool_backing_kind`, `spool_bytes`, `spool_working_bytes`,
  `total_application_memory_limit_bytes`, `receiver_memory_limit_bytes`,
  `max_samples`, `max_wall_seconds`, `workers_explicit`, `owners_explicit`,
  `capacity_qualified: false`.
- `backlog_interval`: null if omitted; otherwise native probe interval fields
  `start_after_public_slot`, `stop_after_public_slot`, `complete`, `reason`,
  nullable `process_cpu_seconds`. Eligible intervals also have `wall_seconds`,
  `unique_baseband_seconds`, `service_seconds_per_baseband_second`, `commits`,
  `initial_uncommitted_slots`, and optionally `busy_hardware_fraction`.
  Incomplete whole replay never yields a complete interval.

Opt-in slot receipts add nullable `commit_input_stop`,
`commit_uncommitted_slots`, `commit_active_owners`, `commit_next_ready` beside
the existing commit wall/process-CPU fields. The backlog rule is the native
[`backlog_interval`](../../../tools/triad-receiver-probe.cpp): all interval
input ready at its first commit, interval commits strictly greater than initial
uncommitted slots, positive elapsed time. An invalid interval is incomplete,
never a capacity pass. Explicit O=2 retains the selected/default full W budget
and does not qualify automatic owner selection.

Existing `outcomes.correct_complete` and `correct_decoded_complete` are false
for every two-episode run; use `service_replay.communication_complete` and the
individual message receipts. One-episode outcomes retain their prior meaning.
Existing outer status/exit conventions remain: `completed` reports finished
work even when communication fails; resource/time/error/unavailable are partial
or unavailable evidence. Pre-render failure may return no `service_replay`.
Per-message opt-in `wrong_bits`, `ambiguous_active_slots` and `quiet_active_slots`
are raw counts for the matching expected episode, or null for an unexpected
start. Quiet and erasure slot indices remain local to the message.
No `capacity_qualified` field becomes true.

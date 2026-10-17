# Symbol_Burst implementation handoff

Implement an optional Robust Modem mode that transmits a complete existing symbol,
leaves a predictable silent interval for receive processing, and starts the next
symbol on a 30-second UTC boundary. Reuse the existing numerical receivers. The
success criterion is actual recovery of nearly feasible receiver workloads through
bounded backlog draining, with matching truthful Link planner estimates.

This is a planning handoff for GPT-6.1 Sol at Ultra effort. It specifies proposed
implementation work, not an implemented or qualified feature. Source baseline:
`c9b5ae9bacad657fa9a3cf65ad988edc1173883b`, branch `codex/pulse-moments`, inspected
17 October 2026. Recheck current source before implementation; symbol/function
names below are more durable than line numbers. This handoff changes documentation
only and does not authorize a release.

## User requirements and accepted decisions

The following requirements are authoritative for this work:

| Item | Required behavior |
| --- | --- |
| Manual enablement | A persistent dropdown immediately right of FHSS: Off, 1, 2, 3, 5, 10. Off is the initial/default setting. |
| Minimum wait | After each complete symbol, wait at least the selected number of symbol durations. |
| Random wait | The next dropdown offers 0, 1, 2, 3 additional symbol durations. Encryption uses an agreed cryptographic whole-integer draw from zero through that limit. |
| Plaintext | Symbol_Burst remains available; effective random wait is zero. A merely loaded receive key does not enable TX randomization. |
| Start timing | Subsequent symbols start on 30-second UTC boundaries, with at least 60 seconds between starts. The user explicitly accepted this minimum. |
| Waveform | No Symbol_Burst dwells, symbol splitting, or new ramps. Existing FHSS remains a separate layer. Preserve existing finite waveform/filter support. |
| Receiver | Reuse existing search, scoring and strategy selection. Retain the complete symbol and at least 20 seconds of guard on either side; incorporate clock, region, offset, audio and rate uncertainty. |
| Initial acquisition | The user explicitly accepted normal acquisition before tracking a transmission, followed by scheduled silence. Listen early enough to cover the configured initial timing uncertainty. |
| Processing relief | There must be actual intervals in which new capture is unnecessary and queued work can drain. A lower displayed CPU number alone is insufficient. |
| Completion | Preserve the development contract: only complete failed expected symbols, totaling at least six seconds of symbol duration, establish physical END. |
| Preservation | Keep earlier fixes and retain roughly comparable feasibility for Default 32/55 dB, the supplied −8 dB configuration, Spread, Weak, Sub-9 kHz and EME, including nearby CPU-curve points and plaintext/encrypted variants. |

Read [AGENTS.md](../AGENTS.md), [coordination](agent-coordination.md),
[building](building.md) and the [development contract](development.md) before
editing. This plan does not override their compatibility or validation rules.

## Scope and important consequences

The numerical decoder need not be replaced. FFT tracking already separates source
symbol index from physical placement, and the compact correlator centralizes symbol
boundaries. The work is a new calendar, its integration into those boundaries,
safe capture exclusion, and corresponding resource/cost accounting.

Three consequences must remain visible throughout implementation:

1. While seeking an unknown transmission, arbitrary eligible first starts still
   require broad listening. There is no globally seekable secret calendar implied
   by the user's per-transmission recurrence. Do not invent one or discard cold
   candidates to obtain an apparent speedup.
2. Once tracking begins, guaranteed silence requires a real barrier against fresh
   first-start acquisition outside the tracked operation. Continuing to create
   unacquired-key/epoch cohorts at every possible start would consume those gaps.
   The receiver therefore temporarily gives up discovery of unrelated new
   transmissions during the tracked reception. This is a mode-specific listening
   policy, not a change to symbol admission thresholds. Existing active contexts
   and already promised acquisition windows remain protected.
3. The six-second absence contract can give long completion latency for short
   symbols. For `T = 0.1 s`, 60 missing expected symbols are required: about an
   hour at one slot per minute. For `T >= 6 s`, one complete missing symbol is
   sufficient. Planned waiting and the 20-second guards contribute zero to this
   failure-duration counter. Do not change this rule or add a terminator.

The scope is Robust Modem. Preserve Fast and Legacy behavior. Keep experimental
Sched_Hop separate: the recommended first implementation rejects simultaneous
enabled Sched_Hop and Symbol_Burst with a clear configuration error. It preserves
each mode independently and supports ordinary production FHSS with Symbol_Burst.
Do not silently turn either mode off or route Symbol_Burst through Sched_Hop's
0.4-second dwell/split-source machinery. Diagnostic FHSS whole-symbol deferral also
needs an explicit unsupported-combination error unless its composition is proved;
it cannot move Burst starts away from the UTC grid.

## Exact timing and address specification

### Distinguish symbol core from waveform support

Use the existing useful symbol duration for the dropdown's unit:

```text
F = configured integer DSP sample rate
D = symbol_sample_count(config)
T = D / F seconds
P = pattern_pulse_padding_samples(config)
N = minimum wait in {1, 2, 3, 5, 10}
R = effective additional-wait limit in {0, 1, 2, 3}
```

The recommended precise interpretation of a symbol starting on a UTC boundary is
that its reserved finite support begins there. This refines the earlier informal
formula, which did not distinguish core timing from pulse tails:

```text
B_i = support start of symbol i, on the 30-second UTC grid
C_i = B_i + P                      actual payload/core origin
U_i = C_i + D                      core end
E_i = U_i + P                      reserved support end, exclusive
J_i = agreed integer draw in [0, R]

B_(i+1) = ceil_to_30_seconds(
    max(E_i + (N + J_i) * D, B_i + 60 * F))
```

All expressions above are sample coordinates. The support reservation is
conservative; the first nonzero sample may fall inside it. If the waveform helper
requires an extra endpoint cell, use that actual bound consistently in TX, RX,
estimates and independent vectors. Do not clip a pulse to make the formula fit.

`C_i`, not `B_i` or compressed source time, determines the existing private pattern,
DSSS and Data addresses. Preserve its epoch and sample phase. Never substitute a
noisy observed arrival time for the canonical sender address. Reception maps that
address into its existing arrival-time hypotheses.

Keep the existing one-time message training prefix before the first symbol and
existing final suppression after the last symbol. They remain separately accounted
waveforms; do not repeat either for every bit. The first support start must be a
grid point late enough for prefix preparation, audio delivery margin and the
existing per-key epoch reservation. Final suppression is discussed below.

For `P = 0`, independent arithmetic examples are:

| Core duration T | N | Realized J | Next start relative to B_i |
| ---: | ---: | ---: | ---: |
| 1 s | 1 | 0 | 60 s |
| 25 s | 1 | 0 | 60 s |
| 25 s | 1 | 3 | 150 s |
| 40 s | 1 | 0 | 90 s |
| 120 s | 2 | 1 | 480 s |

With `T=30 s`, `P/F=0.5 s`, `N=1`, `J=0`, full support plus wait requires
61 seconds, so the next support start is 90 seconds later. This is intentional
preservation of waveform support. Distinct random draws can round to the same
next start, especially for short symbols under the 60-second floor; do not force
observable jitter by altering the rule.

### Checked arithmetic and bounded traversal

Keep UTC whole seconds and sample phase separate where multiplying an absolute
epoch by `F` could overflow. Use checked integer addition/multiplication and the
existing coordinate conventions. For a representable tick and positive quantum:

```cpp
auto r = tick % quantum;
return r == 0 ? tick : checked_add(tick, quantum - r);
```

Do not use floating point rounding of a large absolute UTC timestamp, wrapping
arithmetic, or unchecked `tick + quantum - 1`. Define half-open intervals and
the exact-boundary behavior once. A boundary already satisfying the minimum is
eligible; an extra 30 seconds is not required.

Implement an immutable `SymbolBurstPlan`/calendar and a small forward cursor.
Suggested conceptual API, with names adjusted to existing conventions:

```cpp
struct SymbolBurstOptions { unsigned wait_symbols = 0, random_limit = 0; };
struct SymbolBurstSlot { Coordinate support_begin, payload_begin,
                                    payload_end, support_end; };

SymbolBurstSlot first_slot(Coordinate earliest_support) const;
SymbolBurstSlot successor(const SymbolBurstSlot& current) const;
SymbolStreamAddress payload_address(const SymbolBurstSlot&) const;
```

Keep the logical source index separate from physical coordinates. Missed symbols
advance the cursor through the same expected slots. The random draw depends on
canonical timing and key, never on the decoded bit or successful reception.

This recursive calendar does not have constant-time arbitrary `slot(rank)` or
`lower_bound()` from an arbitrary message origin. Do not copy those guarantees
from `FhssSymbolCalendar`. Maintain bounded current/neighbor cursors for pulse
support and continuation. Walk a finite message once to compute exact duration
if necessary, charge that work, and propagate the cursor through sequential bit
masking and interval handling. Walking from symbol zero for every PCM sample or
every masked bit is forbidden: the latter becomes quadratic in message length.
Any bounded recovery operation that needs earlier addresses must have an explicit
bounded traversal/cache strategy. Retain a cursor checkpoint at the beginning of
the bounded recovery window, including logical index and canonical payload
address, then advance only within that window. Never allocate a whole-message
schedule table merely because the message can last hours.

Separate geometry validation from bound-origin validation. Configurations and
planner objects may exist with an unset epoch/phase before scheduling. Normalize
estimates to an explicitly grid-aligned representative support start and derived
payload origin; do not compute a negative C-P from a placeholder or reject an
otherwise valid unscheduled configuration. Validate real TX/RX origins when bound.

### Cryptographic waits

Use the existing cryptographic implementation with a new versioned domain
`SymbolBurstWaitV1`, counter prefix `sb-wai01`, under the FHSS purpose key.
This purpose is available when
frequency hopping is Off; it does not enable FHSS. Append enum entries rather than
renumbering existing ones. Add the domain allowlist, purpose checks and a unique
counter prefix in `crypto.cpp`; retain every existing vector.

Use the canonical sender payload epoch `C_i / F` and a 32-byte block beginning at
checked byte offset `32 * (C_i % F)` in that domain. Validate `phase < F` before
computing the offset. Each epoch/phase owns its entire block, independent of call
order. Include complete calendar/waveform geometry, including F, D, P, N and
effective R, in cache identity. This design does not promise independent draws
between different geometries at the same canonical address; it does separate
wait randomness from every existing use. Do not add an optional child-key scheme
or overload Data, DSSS, channel-hop or Sched_Hop addresses.

Uniform integer selection uses successive bytes of that 32-byte block. For
`count = R + 1`, accept bytes below `256 - (256 % count)` and return `byte % count`.
For three choices this rejects 255. Fail explicitly if all 32 bytes are rejected.
No modulo-biased fallback, local
PRNG seed, wall-clock entropy or peer-dependent number of preceding RNG calls.
Effective `R=0` returns zero without crypto work. Plaintext also returns zero,
even when unrelated receive keys are loaded. Independent TX/RX vectors must pin
both the chosen draw and the resulting slot.

Context separation is consistent with [RFC 5869 section 3.2](https://www.rfc-editor.org/rfc/rfc5869.html#section-3.2).
No new cryptographic primitive or on-air negotiation is needed; peers configure
the same mode, geometry, wait settings and communication key locally.

## Receiver listening and processing policy

### Acquisition state machine

Use explicit internal states or their equivalent, with one integration owner:

| State | Capture and admission behavior | Exit condition |
| --- | --- | --- |
| Seeking | Existing acquisition and timing uncertainty coverage; begin listening before the possible first symbol. No claimed scheduled-silence saving. | A complete symbol is admitted and establishes a tracked Burst context. |
| Reconciling acquisition | Stop replenishing fresh first-start cohorts for this Burst listening session. Preserve all already admitted contexts, competing continuation hypotheses, and original unscored acquisition windows. Continue processing/capture wherever they still require it. | Existing work establishes complete exclusion for some future intervals. |
| Tracking | Capture the union of retained required windows; consume proven quiet intervals without adding observations. Continue queued DSP processing and publish progress. Fresh unrelated-message acquisition stays suspended. | Last active context physically ends, or explicit cancellation/reset ends tracking without END. |
| Seeking again | Restore ordinary discovery after all tracked contexts and their required tail work are reconciled. | Normal acquisition resumes. |

Do not enter Tracking from a speculative peak, source parse, FEC or MAC outcome.
Use existing complete-symbol admission. A false admission must still resolve
through ordinary evidence; do not add a wall-clock completion timeout. Record and
test the effect of this listening policy on a second unrelated transmission:
the mode intentionally does not promise discovery while its samples are outside
the active capture windows. Never merge that later transmission into the current
pending identity.

This barrier must cover new first-start admissions for other unacquired keys and
profiles in the same Burst listening session, not just the winning key's scorer.
Keep already active streams and still-valid same-reception alternatives. A
continuous active consumer or overlapping tracked schedules can prevent global
capture exclusion; report that condition and do not drop it to claim rescue.
Ending one context does not end or discard another.

Enumerate cold starts from every eligible support boundary `B = 30kF` intersecting
the complete configured sender/arrival search envelope, then derive
`C = B + P` and its exact epoch/phase. Retain the existing arrival hypotheses
around that canonical address. Do not reuse the ordinary contiguous-symbol
`canonical_phase_extent/step` grid blindly: for example, D=1600, F=8000, P=800
gives payload phase800, absent from a phase grid stepping by gcd(D,F)=1600.
Do not substitute observed arrival phase, omit eligible boundaries, or expand to
every sample phase unnecessarily. Each profile uses its own D/P and full timing
envelope. Include this rule in the shared search geometry and cost/preflight.

The original acquisition window of an unconfirmed hypothesis must finish being
scored before retirement. Freezing future admissions does not authorize deleting
its retained samples because wall time advanced. Preserve the recent sample-time
retirement correction and explicit retirement versus physical-END distinction.

### Capture windows and real processing gaps

For each eligible slot, compute the existing earliest/latest arrival envelope of
the complete support, including offset, region, clock-rate alternatives, system
clock error, causal audio lag and provider uncertainty. Expand this envelope by at
least 20 seconds before and after. Do not start listening only 20 seconds before
the symbol's end: that loses the start of a long symbol.

Retain Default's several-second initial search semantics. Integrate Symbol_Burst
into the effective UTC calendar policy deliberately: Default's assumed 10 ms per
station is distinct from message-epoch uncertainty and from audio timing evidence.
Preserve [clock-sync.md](clock-sync.md)'s causal 0..+70 ms per-station audio model,
provider uncertainty and offset/region handling; do not double-count midpoints or
treat unqualified timestamps as measured GPS accuracy. Expand/fall back safely
when dates or timing bounds are insufficient.

Merge overlapping windows across all required consumers. For a simple single
tracked source with guard extents `G_after_i` and `G_before_next`, the usable gap is:

```text
quiet_i = max(0, (B_next - G_before_next) - (E_i + G_after_i))
```

Only the complement of the complete window union is discardable. At `T=25 s`,
`P=0`, `N=1`, `J=0`, 60-second spacing and two 20-second guards give no quiet gap.
With `N=2`, spacing becomes 90 seconds and the nominal quiet gap is 25 seconds
before additional timing uncertainty. Therefore 60-second start spacing alone
does not establish the feature's purpose.

Keep the literal selected schedule. If guards overlap or service time is
insufficient, report **no demonstrated backlog rescue at this setting** and
compute the smallest larger existing N choice that would satisfy the modeled
constraints, if one exists. Let the operator select it. Do not silently extend
waits, change targets, shrink guards, or let random extra waits be essential for
stability. Qualify minimum wait with realized `J=0`; extra randomness can only add
opportunity. The existing CPU budget remains advisory and does not become a new
TX prohibition.

A useful gap is necessary but not sufficient. Measure queued work at each window
end and before the next window, peak retained bytes, processing progress and
long-run queue trend. A rescued case must drain accumulated work within its
available recurring service time with bounded memory. It may process during
capture too. Account separately for startup acquisition debt and the finite tail
left when fresh admissions stop.

That tail can include long-profile hypotheses with outstanding original windows.
It can delay the first usable quiet interval. Qualification must start with normal
acquisition, not only an artificially pre-acquired track. Symbol_Burst cannot
rescue a configuration that never completes initial admission within bounded
resources; diagnose that separately rather than hiding startup cost.

### Skipping input without inventing observations

Represent a proven skipped interval as elapsed physical coordinates, not as a
large zero-filled PCM buffer. It supplies no fit, energy statistic, known sample,
accepted bit, failed-symbol duration or physical completion. Keep carrier/time
coordinates monotone. Settle already captured prefixes before invalidating an
FFT window that crosses an unobserved gap. Preserve required filter history or
restart it only where retained guards and window-support rules prove that safe.

Extend every production strategy that Auto can select. At the baseline,
`PatternReceiver::try_skip_unobserved` only delegates to its fallback;
`PatternCorrelator::try_skip_unobserved` currently supports scheduled-map amplitude
mode only. Adding a calendar field does not make general FFT/complex skipping
work. Existing Live capture certificates, generation/revision fences, current
and future consumer unions must be extended or generalized without weakening
their fail-closed behavior. Unknown proof means capture, not discard.

`live.cpp` already drains pending DSP with an empty new-input block. Preserve this
path and its cancellation/responsiveness. A quiet device callback must not cause
the worker to sleep indefinitely while queued work remains. Conversely, do not
busy-spin or re-run completed searches on each empty poll. The audio device may
remain open to preserve timing; absence of receiver PCM work does not require
repeated device shutdown/reopening. Diagnostics must not manufacture samples or
silently defeat capture exclusion; label uncaptured visual intervals honestly.

### Progress and physical completion

Increment logical symbol position for each expected slot, including misses. Keep
unknown-slot markers and original Data/marker/FEC positions. Publish each newly
accepted bit on the next poll in the same pending row, including `0`, `00`, `001`.
Retain the diagnostic prefix bound and delayed source interpretation.

Use an explicit count/duration of consecutively fully scored absent expected
symbols. The compact correlator's ordinary elapsed-time expression
`end - committed_end` is unsuitable once it includes Burst gaps. Its existing
scheduled source-duration semantics illustrate the required distinction. Failed
guards and planned quiet do not count; an interrupted or partly captured expected
symbol is not a fully scored absence. EOF, cancellation, receive replacement,
codec completion and successful authentication never synthesize END.

For fixed unscaled fixtures, add D samples per fully scored missing expected slot
and require at least 6F; reset on an accepted symbol. Production must preserve
the existing rate-adjusted complete-symbol duration and conservative threshold
comparison where clock hypotheses change observed duration. Never derive it
from elapsed gap-inclusive coordinates. A lost/unobserved expected slot cannot
bridge separate failed runs into consecutive evidence. Test both sides of the
six-second boundary with clock scaling. A fully absent middle symbol whose scored
duration is at least six seconds ends that reception; a test of missing-slot
continuation must use shorter durations or genuine uncertainty, not override END.

## Transmitter and transport integration

Keep one streaming transmitter for the message. Generate the same complete
existing symbol waveform at its scheduled physical support, preserving carrier,
private pattern and DSSS coordinates. Fill true inter-symbol silence efficiently
without an allocation proportional to gap length. Avoid rebuilding the whole
decoder/transmitter, permutation state or waveform for every bit.

Two verified traps in `PatternTransmitter::Impl` need explicit tests:

* `wait_sample()` fills existing FHSS/packed-calendar gaps with independent
  `VirtualCover`. Symbol_Burst's inter-symbol gaps must bypass that behavior.
* The sparse rendering branch is currently selected by `padding || packed_calendar`.
  Symbol_Burst also needs correct sparse placement when `P == 0`; otherwise the
  contiguous path can emit symbols back to back.

Production FHSS channel selection and existing envelopes continue according to
physical UTC, including elapsed silence. Symbol_Burst neither supplies a dwell
grid nor resets the FHSS schedule at each symbol. Preserve pulse/filter support
instead of adding a hard clip or a new ramp at the edge of a burst. Validate public
and private waveforms as supported by existing configuration rules.

Keep one existing final suppression sequence. Private suppression uses
`protected_cover_span_samples + 2P + 1`; FHSS can lengthen its protected span.
Include its actual extent in airtime, stop forecasts and CPU/capture accounting.
It may overlap an expected capture guard. Test that cover alone is not admitted
as the next symbol; complete scoring, not digital zero samples, establishes
absence. Do not truncate security cover to manufacture a clean completion test.

Update `schedule_transmission()` so grid selection precedes final encryption and
source construction. Preserve retry-on-slow-preparation, delivery margin and
reserved-epoch behavior. Continue using the existing audio UTC-following and
bounded correction machinery during playback. A one-time initial alignment
followed by an unmodelled free-running DAC is insufficient for a long sequence.

Audit cancellation at prefix, support edges, inside a symbol, during a long gap,
and final suppression. Protect already exposed pulse support; never emit a future
payload merely to complete a stop. Key exposure/reservation and
`predicted_physical_valid_payload_end` must track actual exposed payload, not
elapsed silence. A backward/large clock step follows existing protected-stop
policy; it cannot rewind PCM, reuse a private address or replay a pulse tail.

Propagate the calendar through `mask_binary_bits`, interval MAC context,
`PatternCode::stream_address`, physical duration, repeat eligibility, inspection,
simulation, generated bits and receiver continuation. Neither a reduced
`bit_rate()` nor a modified airtime estimate alone implements the feature.

Integrate Tune/`NoiseTransmitter` explicitly. Its open-ended safety target is
currently `2^48` samples and existing calendars have direct inverses. Do not walk
millions of recursive Burst slots to construct it. Use a lazy physical horizon or
a checked conservative bound with lazy stopping; preserve finite requests,
temporary-key-before-scheduling order, generated Data addresses and bounded
startup. Do not silently remove Symbol_Burst from ordinary message transmission
because Tune needs additional work.

## Code ownership and integration map

These are locations to inspect and likely edit, not permission to claim every
file at once. Use the current source and claim each exact path before writing.

| Area | Current entry points and work |
| --- | --- |
| Config and validation | `include/datapump/modem.hpp`, `src/modem.cpp`; disabled optional plan, strict values and combination checks. Preserve default hot paths. |
| Shared calendar | New `include/datapump/symbol_burst.hpp` or equivalent; `symbol_schedule.hpp`; exact support/core placement and bounded cursor. |
| Crypto | `include/datapump/crypto.hpp`, `src/crypto.cpp`; appended domain, distinct address space, fixed draw budget and vectors. |
| Waveform source | `src/pattern_code.cpp`; `PatternCode::Impl::stream_address`, `PatternTransmitter::Impl::{payload_offset,payload_lower_bound,wait_sample,render_source,finish}`, generated bits and NoiseTransmitter. |
| Playback | `src/transmit_timing.hpp`, `src/transmit_epoch_guard.hpp`, audio timing consumers in `src/live.cpp`; initial schedule, actual origin, exposure and protected stop. |
| Transport | `include/datapump/transfer.hpp`, `src/transfer.cpp`; effective key, masking cursor, interval context, estimates, receiver mapping and repeat airtime. |
| FFT receiver | `src/pattern_receiver.cpp`; `source_start`, `stream_address`, track boundaries, `advance_bins`, full observations, skip/envelope interface. |
| Compact receiver | `src/pattern_correlator.cpp`; `clock_boundary`, contiguous address shortcuts, complete/absence, carry and skip behavior. |
| Search and strategy | `src/pattern_search.cpp`, `src/pattern_start_geometry.hpp`, `src/pattern_envelope.cpp`, `src/receiver_strategy.cpp`; complete search domains, real physical aperture and strategy eligibility. |
| Live capture | `src/live.cpp`, `src/sched_capture.hpp`, relevant shared capture/history helpers; acquisition barrier, union certificates, stable revisions, queued work without new capture. |
| Estimator | `src/simulation_estimate.cpp` and relevant receiver work/geometry helpers; finite, seeking and tracked recurring costs, full workspace and actual slot geometry. |
| Planner | `src/gui/link_planner_model.cpp`, `link_planner_model.hpp`, `link_planner_page.cpp`; shared curve/selected geometry, cache keys and useful-gap diagnostics. |
| UI fields and layout | `src/gui/ui_contract.hpp`, `desktop_layout.hpp`, `screen_console.cpp`, `controller.cpp`; persistent controls immediately right of FHSS. |
| CLI and persistence | `src/gui/launch_command.hpp/.cpp`, `src/gui/application.cpp`, `src/main.cpp`; exact parse/export/apply and help. |
| Frontend transport | Inspect `src/gui/web_bridge.cpp`, `src/host/runtime.cpp`, `src/host/worker.cpp`, `web/protocol.mjs`; generic controls already carry settings, so do not invent unnecessary IPC fields. |
| Tests and documentation | `CMakeLists.txt`, `cmake/TestGroups.cmake`, focused new fixtures and existing preservation tests; update relevant manuals and timing/planner docs after behavior is verified. |

Freeze the calendar, address and capture/absence invariants before parallel edits.
One coordinator integrates coupled TX/RX/transport/live changes. A separate worker
can prepare pure calendar tests or UI work after interfaces are fixed. Another can
own a baseline harness in isolated outputs. File disjointness alone does not make
search state or final validation independent. Give every agent AGENTS.md, the
coordination workflow, exact checkout and board path; do not assume discovery.

## UI and settings details

Suggested CLI spellings are `--symbol-burst off|1|2|3|5|10` and
`--symbol-burst-random 0|1|2|3`. These names are proposed; settle them once and use
them consistently in GUI export, parameter lists, help and tests. Reject other
values atomically. An old startup command without these flags uses Off/0 defaults.
Import into an already running controller is an optional-field patch: omitted
Burst flags preserve its current values. Do not invent an implicit full-import
reset mechanism or silently turn an active mode off because a partial command
does not mention it.

Keep the random preference when disabled if consistent with existing settings
behavior, while clearly displaying effective zero for plaintext or Burst Off.
Symbol_Burst itself must remain selectable without a key. All loaded receive
keys retain their correct separate role. Apply changes atomically to short/long
TX and corresponding receive profiles; invalidate active configuration through
existing revision rules, not mid-symbol mutation.

Append new UI field identifiers before `count` to preserve existing numeric web
identities. Use shared controls rather than backend-specific duplicate widgets.
The native layout currently has default/minimum dimensions 1180×1072 and
1130×992. FHSS occupies `x=741, width=W-999`, followed by the key control at
`x=W-248, width=232`. Two extra dropdowns require an intentional shared header
layout adjustment. Do not restore earlier smaller windows, make widths negative,
hide the controls in Developer mode, or cover key selection. Verify minimum,
default and large sizes, all normal menus and both native adapters. Extend web,
terminal and framebuffer contract coverage as applicable.

Keep implementation details out of the ordinary control flow. Useful operator
information is next expected symbol, effective wait, whether tracking has begun,
usable quiet time, and whether the selected setting is expected to drain work.
Explain temporary suspension of unrelated-message discovery in help. Do not put
internal hypothesis counts or crypto addresses in ordinary labels.

## Planner preservation and performance evidence

### Freeze comparable inputs

Build a fresh baseline from the actual starting candidate. Record source SHA and
dirty hashes, compiler/flags, binary identity, absolute DSP bytes, CPU/workers,
timing basis, selected TX encryption, all loaded RX keys/profiles, receiver policy
and arithmetic. Percentages alone are not comparable because available memory
changes. Keep the same absolute bytes for before/after pairs, and record the
user-facing 50% value separately.

The exact supplied command is:

```sh
./datapump-gui --pattern auto-keystream --tx-dbm 3 --path-loss-db 170 --noise-dbm-hz -164 --oscillator crystal --shift 0 --search-margin 3 --reference independent --target-snr -8 --short-target-snr 32 --long-target-snr 55 --rate 3600 --carrier 1500 --doppler 0.000000c --dsp-workspace 50% --clock-sync default --audio-error 70ms --dsss-mode off --dsss-factor 10 --dsss-version interleave --search-arithmetic default --receiver-strategy auto --receiver-cpu-budget 0.6 --live-duplex yes --sched-hop-dilution 0 --sched-hop 0 --sched-frequency-search 1 --fhss off --keyfile=/tmp/demoKey --key-name=Default --tx-key named
```

Use the freshly built executable in its stable/claimed build directory; the
literal command above records input semantics, not authority to use a stale root
binary. Automated fixtures should use existing synthetic in-memory key facilities
or properly scoped fixture keyfiles, without reading real user secrets. Assert
that import retains preview −8, short 32 and long 55 exactly.

The authoritative named presets are in `Controller::planner_example`, with
matching explanations in [link-planner.md](link-planner.md). Capture their actual
resolved settings, not just their displayed names. The first three named examples
select a TX key; EME ordinarily selects no TX key while retaining loaded RX keys.
Public FHSS follows existing inactive/unsupported behavior and must not quietly
acquire a key for a comparison.

The historical source harness at
`.agent-work/artifacts/sched-regression-review-20261016/planner_matrix.cpp` is a
useful local starting point, if present. It exports every CPU curve point plus
target/short/long ±1 dB neighbors. Copy/adapt its source under new claims and build
it against current code; never use its old executables or obsolete compatibility
macros as current evidence. The maintained tests are also sufficient starting
points when this ignored historical artifact is unavailable.

### Comparison matrix and required fields

| Axis | Required values |
| --- | --- |
| Configurations | Default 32/55, exact −8 command, Spread, Weak, Sub-9 kHz, EME. |
| Encryption | Explicit plaintext and encrypted variants, with selected TX key and loaded RX-bank state recorded separately. |
| Burst | Off before/after; every N in 1/2/3/5/10 and R in 0/1/2/3 for model/calendar checks. |
| Realized waits | Zero and maximum draw, selected mixed sequences, boundary/rounding cases. Use zero extra wait for minimum-service qualification. |
| Targets | Requested/short/long points, ±1 dB neighbors, all generated CPU-curve samples, and relevant FFT/rate/RAM/strategy transitions. |
| Timing | Default, explicit good GPS with offset/region/audio error, and larger uncertainty that overlaps capture windows. |
| Composition | FHSS Off/production On where valid; DSSS Off/interleave and representative supported factors; existing strategies explicitly and Auto. |
| Workloads | One-bit/raw `001`, short text, interval text/attachment, missing slot, final absence, repeated symbols long enough to expose queue growth. |

Exhaustively cover the cross-product in pure calendar/geometry/support checks.
Retain paired full CPU curves for Off and representative enabled configurations;
use selected/neighbor rows for the remaining N/R combinations and reuse shared
cache results. Full probability curves are not assumed cheap. Choose measured
DSP fixtures that cover distinct production paths and the claimed rescue cases;
do not run every expensive capture for every redundant dropdown combination.

Each row should retain actual target and geometry, sample/chip/symbol/support
sizes, backend, frequency/rate/timing bank counts, state (seeking or tracking),
capture union, min/mean/max spacing and useful quiet, finite/sustained/deadline
work ratios, peak shared/receiver reservation, availability and exact reason.
Measured rows also need process CPU time, paced wall time, accepted bits, physical
END status, maximum backlog and its trend, and source/configuration identity.

For a claimed rescue, pair the same source/configuration/workload with Burst Off
and On. Establish that Off exceeds the configured CPU service allowance, grows
backlog, or misses a recurring service deadline; then show that On with realized
J=0 meets the relevant allowance/deadlines and has a bounded non-growing queue
with actual drain during quiet intervals. Observe several consecutive tracked
windows after acquisition debt is reconciled. If Off only exceeds the advisory
0.6 allowance while remaining physically real-time, label the result a budget
rescue, not proof of unavoidable hardware overload. Already comfortable controls
demonstrate preservation and cannot substitute for this paired rescue evidence.

### Estimate the work actually performed

Keep graph and headline consistent through the same estimator. Existing selected
and curve values protect against misleading results with the maximum of finite
one-bit work and applicable sustained work. Preserve that principle and define
the changed denominators precisely. Do not suppress a costly seeking phase by
averaging it over fictional idle time, or continue charging perpetual broad
acquisition after the new admission barrier has actually stopped it.

Distinguish normal seeking, acquisition-tail debt, tracked recurring work, and
physical completion tail. Include complete symbol scoring, retained alternatives,
guards, final absent symbols, ordinary final cover, calendar/setup and buffer
management. Show a diagnostic work-to-next-required-window ratio where useful.
RF duty is not capture duty and neither is automatically CPU duty. Gaps do not
reduce whole-symbol FFT size or peak workspace in proportion to airtime.

Extend `curve_base_key`, `receiver_layout_key`, `curve_key` and `support_context`
for every new effective input that changes the result. Include timing/capture
policy and schedule identity where relevant; preserve draft-independent one-bit
cache reuse. Repainting unchanged inputs should not recompute a curve. Toggling
Burst, its wait, encryption or effective random limit must invalidate affected
results and immediately withdraw stale observer/model data.

### Preserve previous corrections and diagnose real limits

[sched-hop-regression-review.md](sched-hop-regression-review.md) records a
historical comparison at 5,683,755,008 DSP bytes. The keyed −8 request was already
workspace-limited. An older GUI had silently changed its preview to +0.218495 dB.
Spread's older lower cost omitted required UTC-steering work when its bank grew
from three to seven frequency/rate pairs. These are explanations of earlier
results, not a benchmark of the current candidate.

Current source includes subsequent amplitude arrival/selection/probability fixes.
Preserve full oscillator coverage, actual FFT observation geometry, shared Live
reservations, exact imported targets, sample-time retirement, phase-consistent
continuation, pending publication, finite public arrival windows, quarter-chip
dated geometry and matching cost/preflight, correct payload timing anchors, power
normalization, model-scope labels and restored native layout/presentation.

Off should retain the same wire samples, addresses, geometry, available curve
points and deterministic estimator values unless an independently demonstrated
existing defect is fixed. No new calendar allocation or crypto call belongs on
the Off hot path. For enabled mode, use paired measurements to detect repeatable
regressions beyond run-to-run variation; do not invent a universal percentage
tolerance without establishing baseline variance.

If a configuration loses feasibility, identify whether the cause is calendar
overhead, continuing acquisition, extra capture/FFT work, allocation/reservation,
clock coverage, strategy selection or an incorrect estimate. Repair the cause
and repeat the affected pair. For an inherited limitation, establish its actual
current reason before choosing an optimization. Reuse existing eligible bounded
algorithms and immutable work where justified; do not force an unsupported
decoder, cut hypotheses, shrink a symbol, change power/target, or relabel missing
coverage to obtain green output. Report a fundamental remaining workspace limit
separately from CPU relief. Such a case is not evidence that the requested rescue
has been achieved.

## Ordered implementation stages

Complete the exit check for each stage before broadening edits. Keep each stage's
source/configuration identity and useful failures in claimed evidence. Stop an
expensive attempt that has already answered its discriminating question; then
fix the identified fault before repeating it.

| Stage | Deliverable | Exit check |
| --- | --- | --- |
| 0 Baseline | Coordination record, stable source, isolated claimed outputs, rebuilt matrix and representative sampled baseline. | Exact user targets/configurations preserved; current feasible and unsupported cases distinguished; earlier fixes inventoried. |
| 1 Calendar | Pure options, support/core coordinates, immutable successor/cursor, domain-separated waits, independent oracle/vector tests. | All boundaries, N/R/J combinations, overflow, plaintext and address tests pass; Off unchanged. |
| 2 Streaming TX | Sparse symbols with literal inter-symbol silence, physical UTC/FHSS, correct masking and durations, bounded cancellation/Tune. | Independent PCM/support/address vectors, gap zeros, P=0, FHSS, cover and stop tests pass; construction/streaming avoid quadratic or duration-sized work. |
| 3 Existing RX | Shared calendar continuation in selected FFT/compact/amplitude paths, complete-symbol admission, explicit expected-slot absence. | Raw `001`, missing-slot identities and physical END work from independently constructed PCM; next-poll progress preserved. |
| 4 Production backlog | Live acquisition barrier/reconciliation, complete capture union, safe skipped coordinates, pending-only DSP drain. | Paired Off/On evidence for at least one near-limit plaintext and one encrypted case: the same workload fails its continuous service allowance or deadline, then J=0 Burst provides genuine recurring gaps and bounded non-growing queue/drain after normal startup. Concurrent retained consumers remain protected. This is the decisive feasibility checkpoint. |
| 5 User controls | Shared GUI layout, exact CLI/import/export/settings propagation, help and frontend events. | Controls in requested position; minimum-size layouts usable; old settings and atomic failure behavior preserved. |
| 6 Planner and rescue | Shared calendar/capture accounting, correct caches, full before/after matrix; underlying regression fixes. | Curve/headline agree; seeking/tracking distinction honest; useful-gap recommendation correct; claimed rescue backed by sampled work and memory evidence. |
| 7 Qualification | Final candidate through affected full regression, contract, native/platform, SDK and packaging checks; maintained documentation updated. | Required jobs completed successfully or their exact permitted warning/omission is recorded. No focused-only completion claim. |

Stages 3 and 4 should precede UI polish and large performance experiments. If the
production bank cannot obtain real silence after acquisition, solve the admission
and capture policy before adding estimator discounts. A pure calendar or offline
round trip is not the decisive checkpoint.

## Test assertions and execution

Add focused `test_symbol_burst*` executables or extend appropriate existing
fixtures. New target/group names below are proposals and must be registered in
`CMakeLists.txt` and `cmake/TestGroups.cmake` before using them. Keep independent
expected values, not only encoder/decoder round trips.

| Test family | Assertions |
| --- | --- |
| Calendar/crypto | All selectable N/R, every possible J, exact/immediately adjacent grid boundaries, minimum60, fractional duration, large UTC/overflow, rejection exhaustion, plaintext zero, deterministic key/domain addressing, missing-slot successors. |
| Waveform | Exact core and finite support at independent scheduled positions; literal gap zeros; P=0; no new ramps/dwells; no wait cover; existing FHSS and DSSS addresses/power; one prefix/final tail; chunk-size independence. |
| Timing/stop | Default and explicit offset/region/audio allowances, earliest/latest endpoints, rate bounds, long drift, slow preparation retry, clock discontinuity, cancellation at support edges/mid-gap, no future key exposure. |
| Acquisition | Pre-first listening catches early/late arrivals; fresh cohorts freeze only after admitted complete symbol; original unscored windows finish; seek resumes correctly; unrelated-message discovery policy tested explicitly. |
| Capture union | Every retained key/profile/clock/active context, overlapping windows, revision change/stale proof, missing dates, continuous consumer, exact next-window boundary; skip never discards a required sample. |
| Scoring/absence | Existing strategy and full-symbol evidence retained; isolated fragment rejected; gaps/guards add zero absence; ceil(6/T) failed slots; long absent symbol fully observed; partial capture/EOF/cancel/codec success never END. |
| Progress/transport | `0 -> 00 -> 001` in same pending row on next poll; leading zeros/partial bytes; exact fixed dictionary; 16/17-byte boundary; fixed192-bit marker+128-coded-byte intervals; missing positions and interval MAC context; source decode only after physical END. |
| Resource behavior | Bounded cursors/caches/scratch, no gap-sized PCM, no O(n²) masking, bounded open-ended Tune startup, no idle busy-loop/repeated scoring, fair pending-only processing and no starvation. |
| Backlog rescue | Independent producer clock; real processing gaps; queue/byte peaks and drain before later windows; worst minimum wait; plaintext/private, default/explicit timing; overload remains honestly reported. |
| UI/model | Strict atomic parser/export, effective plaintext zero, all profiles updated, old field IDs, control placement/menus at all sizes, frontend round trips, no stale curves, exact selected-point equality and transition gaps. |

Use an independent PCM producer or explicit fixture clock so decoder speed cannot
slow down the producer and make overload disappear. Accelerated deterministic
clock tests verify semantics; they do not establish real-time throughput. Retain
at least one actually paced production Live capture per claimed rescue family.
Existing controlled-media `CpuWorkBudget` allowances apply only within their
documented functional scope; do not relax real-time, cancellation or physical
absence deadlines. Keep long-symbol/real sample coverage where the contract
requires it, including the existing four-hour streamed test.

Existing useful fixtures include `test_pattern_receiver`,
`test_pattern_correlator`, `test_live_profiles`, `test_live_receptions`,
`test_live_planning_link`, `test_live_transmit_lock`, `test_receiver_strategy`,
`test_simulation_estimate`, `test_link_planner_curves`, `test_gui_link_planner`,
`test_gui_launch_command`, `test_gui_launch_settings`, `test_gui_layout` and
`test_web_bridge`. Preserve the scheduled/FHSS and amplitude-arrival suites too.

Use `./build.sh` to configure/build the application and
`./build.sh test GROUP` for appropriate groups. From the absolute checkout, with
all outputs/resources claimed and stable source inputs, a valid shared-GUI command
is:

```sh
./build.sh test gui --build-dir build/agents/SESSION/gui --jobs 2
```

For narrower diagnosis after that tree is configured, build the specific targets
before CTest. This existing selection is valid:

```sh
cmake --build build/agents/SESSION/gui --target \
  test_gui_launch_command test_gui_launch_settings \
  test_link_planner_curves test_gui_link_planner --parallel 2
ctest --test-dir build/agents/SESSION/gui --output-on-failure \
  -R '^(gui_launch_command|gui_launch_settings|link_planner_curves|gui_link_planner)$' -j 2
```

Use the new Burst targets similarly after registering them; do not run full
calibration during each calendar edit. Native display and timing work must not
compete with another heavy local run. Current relevant full group names include
`contract`, `regular`, `gui`, `frontends`, `web`, `native`, `packaging`,
`amplitude_arrival`, `amplitude_probability` and `all`. Groups overlap; reuse
matching completed evidence instead of duplicating expensive suites.

The completed runtime candidate needs ordinary full CI and SDK qualification,
including applicable native/platform/packaging scopes. Current documented manual
dispatches are:

```sh
gh workflow run ci.yml --ref REF -f devfast=false \
  -f linux_runner=ubuntu-latest-h -f windows_runner=windows-latest-h
gh workflow run sdk.yml --ref REF -f devfast=false \
  -f linux_runner=ubuntu-latest-h
```

Substitute the actual reviewed candidate ref and verify current workflow schemas
before dispatch. Use shared Git/CI claims and the repository's normal publication
authorization. These commands are instructions for the implementation stage, not
a dispatch requested by this planning turn. Wait for real results. Automatic
push/PR green checks are lightweight and do not replace this qualification.
Reuse durable base dependency recipes. Preserve mandatory Release real-time
tests; ordinary instrumented omissions are omitted coverage, not passes. Apply
only the exact typed workload/hosted-WGL exceptions already documented in AGENTS
and the build/release guides. A release, if separately requested, needs exact
source/binary certification after publication.

## Completion and next agent handoff

The implementation is complete only when all of the following are supported:

* Exact timing, random-wait agreement and actual payload addresses are shared by
  transmitter, receiver, transport and estimates; no additional wire bits.
* The requested controls work in shared frontends, including plaintext, and
  configuration/launch round trips preserve the chosen settings.
* Existing numerical receivers decode complete scheduled symbols, preserve
  next-poll pending progress and use only fully scored expected-slot absence.
* Production Live suspends unnecessary fresh acquisition after tracking, retains
  promised work, and demonstrates genuine capture-free processing intervals.
* Claimed near-limit rescues have measured bounded backlog and memory at minimum
  random wait, and the planner agrees with the actual operating state and work.
* Current feasible examples/nearby curves remain roughly feasible, previous fixes
  survive, and inherited or new limitations have concrete diagnosed causes.
* Off preserves existing behavior and cost; required final regression and
  qualification complete for the exact delivered source/configuration.

Update maintained behavior/man pages only after their statements match the
implementation. Preserve historical evidence and its limits. Keep compact
unresolved facts in claimed `.agent-work/notes/`, then release source/resource
claims promptly after their last writer and required validation. No blanket
cleanup, reset or staging of another session's work.

Suggested prompt for the implementing chat:

> Implement `docs/symbol-burst-implementation-plan.md` from the current checkout.
> Read AGENTS.md and the coordination workflow first; verify the current baseline
> and preserve existing changes. Follow the exact timing, acquisition, capture,
> absence and crypto invariants. Complete the production backlog-draining slice
> before broad UI/planner work. Keep the specified examples and nearby CPU curves
> honest and roughly feasible; fix demonstrated underlying regressions. Finish
> the required full validation rather than stopping at focused passes. Report
> actual results and remaining limits. Do not publish a release unless requested.

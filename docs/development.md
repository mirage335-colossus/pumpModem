# Message behavior that development must preserve

Short text, explicit binary messages, fixed-interval longer messages and the
pending view are supported product behavior. Preserve them during protocol work,
compression changes, performance improvements and GUI refactoring. A bit can take
hours or longer to transmit: adding a few framing bits or hiding a received prefix
until completion can make a useful message impractical.

This is a maintenance contract for the existing implementation. The
[protocol](protocol.md) specifies the wire format and
[GUI contract](gui-contract.md) specifies its shared presentation. The
[original specification](original-specification.md),
[migration plan](packetless-stream-plan.md) and older validation entries describe
historical designs; they do not override these current requirements.

For independent Fast Modem throughput work, consult the
[coding and capacity study](fast-coding-study.md), including its development
priorities and evidence limits, the [physical cable study](fast-cable-live-study.md),
and the [current Fast format](fast-mode.md).
The [implemented capacity format](fast-capacity-codec.md) and
[capacity cable experiments](fast-capacity-live-study.md) describe the independent
Fast v2 development; its compact source flags and sparse framing do not apply to
the regular transport paths below.
Its proposed coding and marker changes do not alter this compatibility contract.
The independent [acoustic OFDM format](fast-acoustic-ofdm.md) and
[speaker/microphone live study](fast-acoustic-live-study.md) cover frequency
equalization, training, physical completion and measured acoustic limits.

## Preserve all three transmit paths

| Input | Required existing behavior |
| --- | --- |
| Nonempty text, 1–16 source bytes inclusive | Always use the built-in fixed short dictionary, even when interval compression is off or literal escapes expand the input. Preserve the original codebook and exact endpoint: `e` is `001`; `quick brown` is 70 bits; `quick brown fox ` (16 bytes, including the trailing space) is 98 bits. |
| Explicit binary/status draft | Bypass dictionary encoding and send exactly the entered bits. `0` is one payload symbol; `001` is three. Leading zeros and non-byte lengths remain significant. |
| Text of 17 or more source bytes, empty byte-API sources, and every attachment | Keep a 192-bit marker before each fixed 128-coded-byte interval, including the last. FEC, source codec and keys come from matching local settings. |

The short-text limit counts source bytes, including UTF-8 bytes and visible
callsign/grid/Repeatable text. It does not count displayed escape characters or
compressed bits. The short paths carry no marker, header, transmitted length,
dictionary identifier, padding, terminator, FEC or MAC. Optional Data masking
adds no bits. Existing settling/filter/suppression waveforms are separate from
payload bits. The saved FEC choice still applies when a draft becomes longer.

Keep estimates, actual transmission, receive interpretation, GUI inspection and
copy/paste consistent. At physical completion, eligible raw bits may also spell
dictionary text (`001` can be shown as `e`); preserve the exact bits alongside
that interpretation. This is not authentication. Do not add a mode flag to
resolve the existing ambiguity or pad incomplete dictionary tokens into text.

## Fixed framing and physical completion

Received header lengths must not drive modem boundaries, memory allocation,
acquisition or message completion. Keep fixed local geometry and bounded
storage while receiving. Corrected source areas stay in bounded RAM (no temporary disk files), but all
source decoding, decompression and attachment-prefix interpretation wait until
physical completion. Even a complete codec stream or an underfilled interval
cannot finish reception early. After physical completion, the existing source
codec and bounded attachment prefix may be interpreted under local quotas;
this does not authorize introducing a packet-length parser.

The only physical end is observed absence of admitted symbols: score complete
symbols, and end after consecutive failed durations cover at least six seconds.
For a fixed duration `T`, this requires `ceil(6/T)` complete failed symbols.
At four hours per symbol, six seconds of silence within the next symbol is not
enough; one fully observed absent four-hour symbol is. EOF, cancellation, quotas,
receiver replacement, source validity, FEC and MAC outcomes never manufacture
physical completion. Simulation and recorded audio obey the same rule.

Unknown slots keep their positions through masking, marker alignment and FEC.
An unresolved interval must not disappear so its neighbors become apparently
contiguous source input. Keep scratch, source buffers and diagnostic retention bounded
without making total message length or symbol airtime a buffering prerequisite.

Receiver-local differential windows do not create smaller payload symbols.
They retain soft matched products in bounded state and may only contribute to
an admission decision after the entire physical symbol has been scored.
Their local duration is receiver configuration, independent of the complete
bit duration. Detector-choice penalties, the finite-noise bound and the
strongest-quarter guard must accompany their score. Never infer calibrated
sensitivity from the older coherent/four-quarter probability model for this
additional branch.

Preserve the bounded post-end recovery for a missing or damaged leading marker:
only a fully retained single interval with no timed unknown slots is eligible.
Search every coded start within the first 200 positions using sufficient RS
evidence; a matching marker suffix adds evidence but is never required. These
positions follow the one-marker geometry and existing seven-bit slip allowance,
not a computational budget. Check later retained positions for competing credible
alignments as well; they can veto recovery but cannot become accepted starts.
Use the configured FEC/key and actual symbol positions; keep the existing
aggregate marker false-match bound. Commit only the unique corrected candidate,
then interpret its source. Source syntax cannot select alignment, and public
correction cannot claim authentication. This fallback must neither run before
physical completion nor replace established or failed interval sources.

Additional exhaustive recovery runs only after that same physical completion
event. Keep its coordinator outside the symbol scorer and existing RS/crypto
algorithms: it consumes retained hard 0/1 decisions, explicit missing-slot masks
and their established symbol positions. Do not pass analog samples or symbol
confidence into this layer, shift canonical cipher addresses, or join separate
physical receptions. Its local work and storage settings do not alter any wire
path or marker cadence.

The default additional search budget is five wall-clock minutes per run, using
up to the available CPU cores, with 65,536 separately retained hard-bit slots.
Keep this retention distinct from the unchanged 4,096-bit diagnostic prefix.
An exhausted retention quota leaves recovery unavailable; it must not silently
omit intervals, manufacture source continuity or hide newly admitted pending
bits. All worker scratch and retained hypotheses must remain locally bounded.

Workers may enumerate missing-bit assignments and fixed-size alignment
hypotheses, then call the unchanged RS decoder and full HMAC verifier. Preserve
unknown status in resulting diagnostics: guessed bits are never observations.
Account for all admitted hypotheses in the independent alignment-evidence bound;
HMAC and source syntax cannot substitute for that calculation. Commit only a
unique reconstruction after its competing alignments have been resolved.
Cancellation or the end of a computation budget leaves the search unfinished
and resumable, with no speculative decoded source exposed. Physical completion
and recovery progress must be independently visible in CLI/GUI status. Recovery
must not block the live receive path or pending progress for a later reception.

## Pending reception is part of the feature

Every newly accepted symbol must reach the next receiver progress poll. The
shared GUI must update pending progress on that poll and show the available bit
prefix. For `001`, an operator sees `0`, then `00`, then `001` in the same pending
row, even if those updates are hours apart. Do not wait for
a byte, dictionary token, marker, coding interval, arbitrary batch size or the
whole message. No decision is promised before a symbol is actually accepted.

Keep leading zeros, partial-byte prefixes, stable reception identity, unknown-slot
zero placeholders and the missing-bit notice. A pending row stays visibly pending
and cannot acquire completed-message copy/save eligibility merely because a prefix is byte-aligned
or decodable. On physical completion, update the existing row; short messages
retain their exact raw bits alongside any dictionary interpretation. Both FLTK
and Rev consume this shared behavior.

Keep the existing diagnostic bound: reception retains the first 4,096 raw bits.
Beyond that prefix, pending counts can continue updating without growing the
displayed bit string. Completed interval sources may replace the pending prefix
with decoded content. This bound does not justify delaying any of the bits of a
tiny message, and this contract does not require unbounded raw-message retention.

## Regression coverage and checks

Keep independent expected-bit vectors as well as round trips: a matching encoder
and decoder could otherwise change the codebook together without a test failing.

| Contract | Existing regression suites |
| --- | --- |
| Original dictionary codes, every byte, canonical escapes, exact/truncated endpoints | `compression_short` |
| Inclusive 16/17-byte split, FEC/compression/key combinations, exact airtime and raw bits | `transfer`, `stream_receive` |
| Fixed interval geometry, zero/trailing-byte preservation, errors/erasures, bounded post-end RS alignment search and ambiguity rejection, source decode only after physical end | `stream_codec`, `stream_receive`, `attachment` |
| Exhaustive hard-bit assignments, deterministic worker coverage, timeout/cancellation resumption, unchanged evidence and original missing-bit accounting | `recovery` |
| Actual sampled four-hour symbols draining one bit at a time with bounded memory; absence vs EOF and partial silence | `pattern_correlator`; complementary FFT reception checks in `pattern_receiver` |
| Bounded carrier/clock search, sub-Hz configuration, exact weak sampled reception and noise-only rejection | `pattern_search`, `tuning`, `simulation_estimate`, `weak_signal`; coupled-clock and streamed-template checks in `pattern_receiver` |
| Whole-bit section fitting under phase and gain changes, conservative evidence, isolated-tail rejection, exact pending prefixes and bounded memory | `pattern_drift`, `pattern_fft_batch`, `pattern_correlator_batch` |
| Local differential products, geometry boundaries, noise-only and isolated-fragment controls, scalar/batch equivalence, sampled full-bit admission and unchanged pending/completion behavior | `pattern_differential`, `receiver_differential`, `pattern_fft_batch`, `pattern_correlator_batch` |
| Modeled reception versus independent sampled captures, phase/frequency/timing impairments, raw-bit draft success and physical completion | `receiver_probability`, `differential_probability`, `differential_receiver_probability`, `simulation_estimate` |
| Competing RX target orders and geometries, immediate revisions, obsolete-content withdrawal, independent later receptions and bounded arbitration | `live_profiles`, `live_receptions`, `live`, `live_resources`, `cli` |
| In-memory per-key TX lock, pulse lookahead, cancellation/profile changes, clock corrections and one-transmission override | `live_transmit_lock` |
| Pending prefixes and row identity, completed copy behavior, short/raw compose edits and transmission inspection | `gui_application`, `gui_controller`, `gui_inspection`, `gui_binary_editor` |

From the repository root, build and run the focused headless coverage:

```sh
./build.sh test contract --jobs 2
```

The `contract` CTest label and `datapump-tests-contract` build target select
these same 30 suites (the CLI suite is included when Python is available).
See [the build guide](building.md) for direct CMake commands and other groups.

The long-symbol regression generates sampled PCM with four-hour coordinates; it
does not wait four wall-clock hours. Shared GUI suites require no display and
exercise the presentation used by both adapters. They do not establish native
window rendering or physical-link performance. For adapter changes, also run the
[native GUI conformance checks](gui-architecture.md#verification-and-maintenance-guardrails)
for each affected backend on a display. Record the actual checks and any limits
in [validation](validation.md); do not replace physical progress assertions with
whole-message loopbacks or relax them merely to make a refactor pass.

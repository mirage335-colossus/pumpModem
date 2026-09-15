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
storage while receiving. Corrected source areas may spool internally, but all
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
contiguous source input. Keep scratch, spooling and diagnostic retention bounded
without making total message length or symbol airtime a buffering prerequisite.

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
| Fixed interval geometry, zero/trailing-byte preservation, errors/erasures, source decode only after physical end | `stream_codec`, `stream_receive`, `attachment` |
| Actual sampled four-hour symbols draining one bit at a time with bounded memory; absence vs EOF and partial silence | `pattern_correlator`; complementary FFT reception checks in `pattern_receiver` |
| Pending prefixes and row identity, completed copy behavior, short/raw compose edits and transmission inspection | `gui_application`, `gui_controller`, `gui_inspection`, `gui_binary_editor` |

From the repository root, build and run the focused headless coverage:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -j 2 \
  -R '^(compression_short|transfer|stream_codec|stream_receive|attachment|pattern_correlator|pattern_receiver|gui_application|gui_controller|gui_inspection|gui_binary_editor)$'
```

The long-symbol regression generates sampled PCM with four-hour coordinates; it
does not wait four wall-clock hours. Shared GUI suites require no display and
exercise the presentation used by both adapters. They do not establish native
window rendering or physical-link performance. For adapter changes, also run the
[native GUI conformance checks](gui-architecture.md#verification-and-maintenance-guardrails)
for each affected backend on a display. Record the actual checks and any limits
in [validation](validation.md); do not replace physical progress assertions with
whole-message loopbacks or relax them merely to make a refactor pass.

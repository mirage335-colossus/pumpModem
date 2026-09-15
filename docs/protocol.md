# Fixed-interval pattern stream

The modem transports opaque source bytes using one bit per pattern symbol.
There are no packets, received integer lengths, variable modem headers, packet
IDs, repeat flags, bootstrap CRCs, or whole-stream integrity footers. An optional
attachment prefix is ordinary source content interpreted by the application.
Both peers select the same waveform, FEC, source codec and key locally. There is no over-air negotiation or old-packet fallback.
Existing keyfiles remain usable. The fixed short dictionary retains its original
bit codes; the interval transport is incompatible with the previous packets.

## Fixed wire geometry

Nonempty text of up to 16 source bytes uses the [exact raw-bit path](#exact-raw-bit-path).
The following geometry applies to text longer than 16 bytes, empty sources sent
through the byte API, and attachments of every size. The default FEC preset is
RS60 (60% parity overhead).

```text
192-bit marker | 128 coded bytes | 192-bit marker | 128 coded bytes | ...
```

One marker precedes each coding interval. No extra marker announces completion.
Every interval therefore occupies 1,216 one-bit symbols, including its marker.
All intervals use the same width, including the last and a one-byte attachment.
A separate approximately two-second hardware-settling waveform may precede the
stream, rounded to the nearest whole symbol duration with ties upward. It
carries no data or acquisition condition; sufficiently long symbols have no
settling prefix. Pulse-filter tails also carry no additional source bits.
Exactly three seconds of independent noise follow the payload and filter tail,
including for symbols longer than six seconds. Its separate suppression domain
consumes no payload positions. It suppresses weaker delayed echoes and provides
no ending signal: physical completion still requires absence of admitted symbols.

| Local FEC | Public data area | Keyed data area | HMAC when keyed | RS parity | Unknown-location byte errors |
|---|---:|---:|---:|---:|---:|
| Off | 128 | 96 | 32 | 0 | 0 |
| RS20 | 106 | 74 | 32 | 22 | 11 |
| RS60 | 80 | 48 | 32 | 48 | 24 |

For example, a keyed RS20 interval is 74 fixed data bytes, then 32 HMAC bytes,
then 22 RS parity bytes. Public RS20 is 106 data bytes then 22 parity bytes.
There is no public SHA-256 digest or replacement checksum. The data-area
representation is selected locally as described below; transmitted bytes
never select a mode, allocation size or alternate parser.

RS uses shortened systematic GF(256) codewords with polynomial `0x11d` and
first root alpha^0. The two enabled profiles are RS(128,106) and RS(128,80).
Each interval is corrected independently; there is no whole-message interleaver
or final shortened-word rule. The generic implementation accepts unique known
erasure positions. Its mixed correction budget is `2*errors + erasures <= p`,
where `p` is 22 or 48 parity bytes. Errors can occur in data, HMAC or parity.
A successful public RS correction is not cryptographic authentication.

Diagnostics report received, corrected and missing bits separately for data,
keyed HMAC and parity. Known-bit accuracy excludes unknown slots. Repaired-byte
counts include declared erasures even when a zero placeholder matched the
reconstructed byte, so parity-only and missing-zero repairs remain visible.

## Source bytes and compression

For interval-coded sources, the application selects one source convention locally.
Short text uses the fixed dictionary described below and bypasses both interval
conventions. There is no compression flag on air.

### Compressed source (default)

Encode exactly one **raw LZMA2** stream, using the fixed 4 MiB dictionary and
preset 9 extreme search settings, including empty and incompressible input.
This is raw LZMA2, not an XZ container. Compression is always encoded in this
profile; it is not conditionally omitted when the result expands.

Split the compressed bytes across fixed data areas and zero-pad only the final
area. Padding must be shorter than one data area. Exact multiples add no extra
all-padding interval. The LZMA2 end control is part of the source codec, never a
modem stream-ending signal.

During reception, corrected source areas enter a capped temporary-file spool.
Only after the physical end event may the application invoke LZMA2. It produces
output incrementally under a local quota without an original-size field, requires
the codec to end, and checks that every remaining received byte is zero and that
there are fewer than one area's remaining bytes. This preserves arbitrary
original bytes, including real trailing zeros. Missing or unresolved intervals
must not be removed and their neighbors concatenated into decoder input.
No source decoder, compressed preview or speculative decompression runs before
the physical end event.

Decoder scratch is separately capped at 8 MiB; encoder scratch at 64 MiB. Both
use a budgeted allocator and a fixed dictionary. Input storage and decoded output
have independent local quotas. These numbers are not a total process RSS cap.

### Exact uncompressed source

Each fixed data area contains a compile-time number of nine-bit cells:

```text
present bit | eight source-byte bits
```

A present cell carries one byte, including `00` and `ff`. An absent cell carries
no byte and all its bits must be zero. Present cells form a prefix within each
interval; remaining cells and spare final bits are zero. This is occupancy
information, with a fixed bounded loop rather than an integer length parser.
An underfilled interval does not end a stream; subsequent intervals may carry
more bytes. Every interval, including occupancy and fill, is covered by RS and
by HMAC when keyed.

| FEC | Public source bytes/interval | Keyed source bytes/interval |
|---|---:|---:|
| Off | 113 | 85 |
| RS20 | 94 | 65 |
| RS60 | 71 | 42 |

If an interval cannot be repaired, its occupancy and original source-byte count
are unknown. Do not turn unknown validity bits into an apparently empty interval
or advance an exact source offset by a guessed count. Keep its coded-symbol
address and mark reconstruction incomplete.

### Explicit attachments

Before source encoding, an attachment adds the literal prefix
`#ATTACHMENT### fileName.ext ###ATTACHMENT# ` followed by the exact file bytes.
Attaching forces Repeatable off. Ordinary binary or text sources have no prefix
and do not create file entries.

Only after physical completion and source decoding does the application inspect
a bounded prefix at source byte zero. A valid UTF-8 basename is 1–255 bytes with
no path separators or control characters. The application removes that prefix,
retains the filename as a local Save suggestion, and exposes the exact remaining
bytes. Malformed markers remain ordinary message content. No received name is
opened or saved automatically. The source quota allows only the fixed maximum
prefix overhead beyond the configured file-content quota. There are no length
fields or repeated attachment records in the modem.

## Marker alignment and erasures

The 192-bit marker repeats one 96-bit word twice. The word is derived at runtime
from the first 12 SHA-256 bytes of the ASCII label `DataPump/byte-boundary/v1`.
This derives a public alignment constant; it does not add a digest of public
content. Storing the label rather than literal marker bytes reduces accidental
self-recognition when transferring the source or executable, without excluding
collisions in arbitrary content.

The marker collector has fixed interval cadence and bounded start/endpoint
searches. It tolerates up to seven bits of alignment displacement; marker
candidates can include up to eight substitutions or a contiguous deletion of
up to 80 bits subject to the evidence budget. An inferred deletion needs an
exact observed 32-bit trailing anchor. A known missing leading prefix may use
the acquired stream position to determine the endpoint. Competing plausible
endpoints reject alignment rather than selecting one arbitrarily.

Timed unknown slots remain unknown through decryption and marker recognition.
They supply no marker confidence. Packing a byte containing any unknown bit
produces a declared byte erasure; zero placeholders are never silently treated
as received evidence. The fixed 128-byte geometry permits missing final coded
slots to be supplied as erasures after physical completion. This allows RS to
repair a missing final parity bit without a packet length or byte-alignment gate.
An entirely unobserved interval is not manufactured from a marker alone.

### Marker evidence threshold

For `n` observed independent fair bits and at most `e` mismatches, the fixed
hypothesis bound is `sum(i=0..e, C(n,i))/2^n`. The collector charges 144,615 start
and deletion hypotheses per marker attempt. Attempt `j`, starting at one,
receives at most `2^-84/(j*(j+1))` false-match budget; these budgets sum to at most
`2^-84` across an indefinitely drained collector. Chunk boundaries add no trials.
Integer-rounded penalties conservatively enforce this bound, so later attempts
or fewer surviving observations can reduce the permitted mismatch count.
This is an analytic random-input model, not channel calibration, a posterior
probability, authentication, or protection against deliberately constructed data.

## Encryption and interval authentication

```text
TX: source areas -> keyed HMAC -> fixed RS -> markers -> Data mask -> patterns
RX: patterns -> Data unmasking -> markers/erasures -> RS -> keyed HMAC -> spool
```

Encryption masks every marker, source, tag and parity bit at its original symbol
position. Missing slots still consume positions. Nothing resets the cipher,
epoch or marker cadence merely because an interval is drained or a symbol lost.
The selected non-tone key also enables private pattern templates; tone modes
force private protections off. Purpose-separated keys retain the existing Data,
MAC, Scrambler and optional DSSS roles.

Only keyed intervals carry HMAC-SHA256. The MAC covers a fixed versioned domain,
the local FEC/source profile, the canonical `(epoch, ordinal)` address of the
first coded symbol after the marker, and the entire fixed data area. The address
comes from `symbol_stream_address`, shared with Data masking, and can be acquired
without the original stream's start counter. No noisy measured frequency or
received-block counter is used as the authentication address. A mismatched key,
profile or address fails authentication.

A schedule address is not a unique transmission nonce. Reuse of a key/address
retains replay and CTR-reuse limitations. Interval authentication does not prove
that the transmitter intended no later interval. For a single canonical LZMA2
source, loss of the final interval removes its codec endpoint and is detected by
strict post-end decompression; this is source-format validation, not a new
physical ending rule or durable whole-transmission replay protection.

## The sole physical end rule

Iterative search scores **complete symbols**. A completed failed symbol lasting
at least six seconds ends the stream immediately. For shorter symbols, consecutive
failed-symbol durations must cover at least six seconds. There is no independent
minimum-two-symbol exception and no partial-window preemption of a long symbol.
For fixed duration `T`, ending requires `ceil(6/T)` consecutive failed symbols,
each fully scored. Rate correction uses received-sample time.

Only this observed absence emits physical completion. EOF, cancellation, quota
exhaustion, receiver replacement, MAC/RS success or failure, source occupancy and
codec end do not complete a stream. Incomplete capture stays incomplete. Missing
interior slots can be retained compactly while timing continues; source processing
cannot admit waveform hypotheses or extend their lifetime. A long fade can end a
received segment even if the sender intended to continue.

Live audio, recordings and simulation follow the same rule. A recording must
contain enough actual sampled absence; EOF does not synthesize it. Independent
sends need separation sufficient for the complete-symbol absence rule and filter
and acquisition margins. A local processing interruption may release resources
without claiming observed silence or invoking decompression.

## Exact raw-bit path

Nonempty text of up to 16 source bytes automatically uses the fixed short
dictionary. This threshold counts bytes, including visible convenience text,
not characters or encoded bits. Attachments always use intervals. Explicit
binary/status input bypasses the dictionary and preserves leading zeros and
non-byte bit counts: `001` is exactly three payload symbols, with no byte padding.
The 16-byte threshold is a transmit choice only. It does not impose a received
bit count or end a reception at 128 bits.

The dictionary is a fixed prefix code over source bytes; it transmits no table,
identifier or original size. Both peers use these original codes:

| Source bytes | Codes | Bits per byte |
|---|---|---:|
| Space, `e`, `t`, `a`, `o` | `000`, `001`, `010`, `011`, `100` | 3 |
| `i`, `n` | `1010`, `1011` | 4 |
| `s h r d l u c m f w y p b g` | Consecutive codes `110000` through `111101` | 6 |
| Every other byte | `11111` followed by its eight MSB-first literal bits | 13 |

Text `e` therefore sends exactly `001`. No padding or terminator follows its
last token. All short text uses this dictionary, including when the local
interval compression option is off or literal escapes expand the input. There
is no ambiguous raw-byte fallback. At most 16 decoded bytes occupy 208 bits.

For example, `quick brown` is 11 bytes and sends exactly 70 dictionary bits.
`quick brown fox `, including its trailing space, is 16 bytes and sends exactly
98 bits. The first interval-coded text size is 17 bytes. The threshold is shared
by the transmitter, receiver and GUI; it counts decoded source bytes, including
spaces, rather than the number of visible escaped characters or packed bits.

Both unmarked paths have no byte intervals, marker, transmitted length, FEC or
MAC. Optional Data masking uses the normal symbol schedule and adds no bits.
The selected FEC preset is retained for later interval-coded drafts.

Every newly accepted symbol is available on the next receiver progress poll;
there is no byte, marker or 1,024-bit prerequisite. Pending rows retain leading
zeros and the exact partial-byte prefix. Raw receptions remain unvalidated and
use the same physical end rule: consecutive fully scored failed symbols must
cover six seconds. At symbol durations of six seconds or longer, one wholly
missed symbol ends reception; six seconds inside an unfinished long symbol does
not. Completion never waits for a byte boundary, marker, parity or source codec.
The existing settling/filter/suppression waveforms carry no additional bits.

Only after physical completion may the application interpret an unmarked short
stream with this dictionary. It requires complete canonical tokens, no unknown
symbol slots, at most 208 observed bits and at most 16 decoded bytes under the
local content quota. Recognized interval markers and failed interval sources do
not fall back to this decoder. Invalid/truncated tokens retain raw bits without
inventing missing bits or releasing a partial decoded prefix.

With no mode marker, an explicit raw sequence can also spell dictionary text.
The application retains the exact bits alongside any completed interpretation;
`001` can be displayed as `e` while still copying as exactly `001`. Dictionary
decoding is not validation or authentication and cannot create an attachment.

## API and memory boundary

`stream_codec.hpp` contains fixed interval/source APIs and generic `fec` routines.
`Message.local_id`, callsign, grid and repeat flags remain application-local.
Attachment names use the bounded source-text convention above.
`transfer::StreamReceiver` consumes drainable physical chunks and gates the source
codec on true completion. `encode_packet`, `decode_packet`, header probes,
`pack`/`unpack` and original-size decoders are removed. The short dictionary has
only exact-bit encode/decode APIs, with no packed padding or length field.

Physical candidates, marker overlap, one coded interval, erasure masks and RS
scratch are bounded. Corrected source bytes use a capped spool until completion;
raw diagnostics retain a bounded prefix. Local output and event limits still
matter for an indefinitely confident signal. Successfully decoded content is
opaque data, never a received path, command or permission to execute a file.

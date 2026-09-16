# Fixed-interval pattern stream

The modem transports opaque source bytes using one bit per pattern symbol.
There are no packets, received integer lengths, variable modem headers, packet
IDs, repeat flags, bootstrap CRCs, or whole-stream integrity footers. An optional
attachment prefix is ordinary source content interpreted by the application.
Both peers select the same waveform, FEC, source codec and key locally. There is no over-air negotiation or old-packet fallback.
Existing keyfiles remain usable. The fixed short dictionary retains its original
bit codes; the interval transport is incompatible with the previous packets.

These are compatibility requirements for ongoing development, including the
short paths and per-bit pending view. See the
[preservation contract and regression coverage](development.md).

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

### Post-end recovery of a missing or damaged leading marker

If no marker was established, physical completion permits one additional bounded
search for a single received interval. The complete raw input must still fit in
the retained diagnostic prefix and contain no timed unknown slots. This search
cannot replace an established or failed interval reconstruction, recover a
discarded prefix, or concatenate separated intervals.

The search tries every possible coded start at received offsets 0–199. These
200 positions cover one 192-bit marker plus the existing seven-bit slip allowance;
this is the supported leading-marker geometry, not a computational budget. All
remaining received bits must fit within one fixed 1,024-bit coded interval, with
at least one coded bit observed. The complete capture therefore contains at most
1,223 bits. Inputs within the short-message bit limit remain on their existing
path. Only the unobserved final coded slots become erasures.

At each start, the longest exact marker suffix immediately preceding it supplies
optional evidence, from zero to 192 bits. There is no minimum suffix requirement:
RS can establish alignment without any marker bits. The locally selected RS
profile must repair the interval and supply the evidence described below; FEC Off
is ineligible. Keyed intervals must also pass their normal HMAC check using the
actual received symbol address. Missing marker bits never advance the acquired
clock or reset Data masking.

The search also tests the remaining retained start positions using the same
evidence threshold solely to detect ambiguity. Positions beyond offset 199 can
veto recovery but cannot become accepted starts. This prevents the supported
geometry's edge from concealing competing alignments in structured data, such
as an all-zero codeword. Any different credible coded start rejects recovery,
even if its source bytes agree. Equivalent suffix hypotheses may share a start.
Only a unique candidate within the supported geometry is committed, reusing its
already corrected interval rather than running RS a second time. The normal
source-decoding and attachment-interpretation path then runs under its existing
quotas. No source codec, readable text, occupancy value, or transmitted length
supplies alignment evidence. This recovery neither ends reception nor adds
transmitted bits, and public RS recovery remains unauthenticated.

### Exhaustive hard-bit recovery

When ordinary decoding and the existing small leading-marker fallback cannot
complete an interval source, physical completion may start an additional,
parallel search. This is receiver-only work: neither the source encoder, RS
algorithm, HMAC, cipher, transmitted marker nor fixed interval cadence changes.
The recovery input contains only admitted hard decisions, explicit unknown
slots and their existing canonical symbol positions. Analog samples and symbol
confidence are not supplied to this layer.

The receiver retains at most 65,536 hard-bit slots for this work by default,
independently of the 4,096-bit diagnostic prefix. Exceeding the recovery quota
disables this additional recovery for that capture; it does not truncate source
input into an apparently complete message. Every candidate uses the locally
configured 128-byte codeword size, FEC profile and keys. Missing positions retain
their physical addresses through Data unmasking and packing. This search cannot
repair arbitrary unlocated insertions/deletions or choose another cipher epoch.
It never joins receptions separated by the physical absence completion event.

An established interval start fixes the 1,216-slot cadence (192 marker slots
plus 1,024 coded slots), including intervening failed intervals. Without an
established start, offsets 0–199 supply eligible first coded starts, as in the
existing fallback. Later retained starts are checked for competing alignments;
starts on the same fixed cadence belong to the same alignment. Source decoding
is deferred until a complete, unique reconstruction is selected. Neither
decompression success nor a plausible attachment prefix chooses the alignment.

For a fixed interval, ordinary RS treats every byte containing an unknown bit
as an erasure, even when its other seven bits were received. The additional
search can exhaustively assign missing bit values in selected erased bytes,
then call the ordinary RS decoder with the remaining byte erasures. Bytes with
the fewest unknown bits are selected first to minimize the assignment domain;
there is no confidence ranking. By default, the assignment plan leaves room for
two additional erroneous bytes in the normal `2*errors+erasures <= parity`
budget. Any candidate must preserve all observed bits inside originally partial
bytes, and keyed intervals must pass their complete original HMAC. Guessed bits
remain missing in diagnostics; they do not become received evidence.

Independent assignments run in batches on at most the available CPU cores.
The default wall-clock budget is 300 seconds per run. CLI receive commands use
`--recovery-seconds` (0 disables the additional search), `--recovery-threads`
(0 selects the available cores), `--recovery-bits` and `--recovery-errors`
(default 2, maximum 24). These are local computation/storage settings, not
transmitted lengths. A large error reserve can create an impractical assignment
domain; a domain beyond the bounded integer counter is unavailable.
The recovery engine has a separate 16 MiB workspace allowance by default;
worker count also fits the available scratch space and number of batches.

`RecoveryProgress` reports attempts, planned total and elapsed milliseconds.
Planning is lazy, so the total can grow during preparation. `incomplete` means
the time budget ended, `cancelled` means a stop was requested, `exhausted` means
the entire declared scope produced no complete candidate, and `ambiguous` means
competing credible reconstructions remain. An incomplete or cancelled search
retains its work and can resume through `recover_received` while its result is
alive. This state is an in-memory job, not a persisted checkpoint. A candidate
found before all required ambiguity checks finish remains private; elapsed time
cannot establish uniqueness. `recovered` describes interval reconstruction;
the ordinary bounded source decoder must still succeed before
`content_validated` becomes true. Physical `stream_complete` is independent of
all these states. Live recovery runs separately from audio capture and pending
bit updates for later receptions.
Two distinct credible reconstructions establish ambiguity immediately, so that
rejecting outcome may stop before every planned attempt has run.
The GUI Recovery menu can resume or cancel a job. Clearing received content,
stopping or reconfiguring the session discards retained jobs. The live queue
holds at most eight jobs within a separate 16 MiB reserved workspace.

### Marker evidence threshold

For `n` observed independent fair bits and at most `e` mismatches, the fixed
hypothesis bound is `sum(i=0..e, C(n,i))/2^n`. The collector charges 144,615 start
and deletion hypotheses per marker attempt. Attempt `j`, starting at one,
receives at most `2^-84/(j*(j+1))` false-match budget; these budgets sum to at most
`2^-84` across an indefinitely drained collector. Chunk boundaries add no trials.
Integer-rounded penalties conservatively enforce this bound, so later attempts
or fewer surviving observations can reduce the permitted mismatch count.

The post-end search requires at least 144 combined evidence bits per hypothesis.
For an exact suffix with `n` observed marker bits (including `n=0`), `p` RS parity
bytes, `v` erased coded bytes and `e` corrected fully observed bytes, its
conservative evidence count is:

```text
n + 8*(p-v) - 15*e - ceil(log2(e+1))
```

For each codeword, the radius-`e` byte-error neighborhood has at most
`sum(i=0..e, C(128-v,i)*255^i) <= (e+1)*2^(15*e)` members. Projecting away the
`v` erased bytes therefore bounds random RS acceptance by that volume times
`2^(-8*(p-v))`. The calculation discards even the observed bits inside an erased
byte. Exact marker observations are disjoint from coded observations, so their
evidence adds. Charging all 200 eligible starts and all 193 possible suffix
lengths gives at most `200*193 = 38,600` hypotheses. Each is bounded by `2^-144`,
giving a total post-end false-match bound below `2^-128`. The additional starts
tested only for ambiguity can reject candidates, so they cannot increase this
false-accept bound. Successful HMAC checking supplies no extra credit in this
calculation.

The additional search preserves the existing aggregate `2^-84` bound without
changing ordinary marker acceptance. The collector rounds its 144,615 trials
up to `2^18`, and its rounded attempt weights satisfy
`sum(j=1..infinity, 2^(-ceil(log2(j))-ceil(log2(j+1)))) = 5/6`. Thus ordinary
marker attempts together use at most
`(144615/2^18)*(5/6)*2^-84 < 2^-85`. Adding the single post-end search's
`2^-128` budget still leaves the combined bound below `2^-84` per collector.

The separate exhaustive hard-bit recovery uses the original observed bit mask,
including known bits inside partially missing bytes. Let `u` be the number of
originally unknown coded bits, `e` the corrected fully observed bytes, `p` the
parity bytes and `n` the exact observed marker-suffix bits. Its evidence is:

```text
n + 8*p - u - 15*e - ceil(log2(e+1))
```

The decoder requires recovered partially missing bytes to preserve their
original observed bits exactly. Projecting all RS codewords onto the originally
observed coordinates loses at most `u` constraints, giving `8*p-u` evidence
before the same conservative byte-error-neighborhood penalty. Enumerating
missing-bit assignments rediscovers members of that already counted projection;
guessed values supply no evidence and do not add a separate assignment factor.
For `N` retained slots, every accepted hypothesis must provide at least
`128 + ceil(log2(N*193))` evidence bits, charging all retained starts and all
193 marker suffix lengths. Thus the entire declared search is bounded by
`2^-128` under the same independent-fair-bit model. Resuming a job continues its
finite domain and does not create a new trial budget. Full HMAC verification is
still required for keyed candidates and contributes no evidence credit.

Including both the existing fallback and exhaustive recovery gives at most two
`2^-128` contributions in addition to ordinary marker acceptance below `2^-85`;
the aggregate remains below `2^-84` per collector. Increasing workers or the
time budget changes progress through the declared domain, not this bound.

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

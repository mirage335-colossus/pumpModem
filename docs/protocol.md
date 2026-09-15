# Data Pump pattern transport and compact packet format

Automatic modem profiles use one meaningful bit per binary pattern symbol.
Pattern evidence establishes signal presence, timing and burst boundaries
before application decoding. Both audio peers need matching modem settings.
The compact packet codec remains available for larger text, files and
screenshots; it does not define the presence of a raw-bit signal. Existing
keyfiles remain compatible. Receiving a file never executes it.

The automatic waveform is incompatible with the previous automatic APSK
waveform. Both peers must use the current pattern transport. Legacy APSK has been removed;
the receiver does not negotiate compatibility over air.
Pattern peers also require the initial and periodic recovery convention for all
compact packets. There is no automatic retry of their earlier representation
without the initial marker after a recovery candidate fails validation.

## Raw bits and short text

Raw binary input is transmitted exactly as entered, including leading zeros
and lengths not divisible by eight. Three input bits produce exactly three
payload pattern symbols. There is no packet header, checksum, MAC, FEC,
byte-padding bit, or transmitted length field on this path.

A separate hardware-settling prefix precedes a nonempty transmission when two
seconds rounds to at least one whole payload-symbol duration. Rounding is to
nearest, with ties upward: symbols longer than four seconds add no prefix. This
independent noise-like waveform helps external gain control and muting settle;
it carries no payload or training information and is not required for reception.
Payload stream positions start at zero after the prefix. See
[settling waveform and timing](pattern-constellation.md#hardware-settling-prefix).

Normal text below 16 original UTF-8 bytes uses the fixed short-byte dictionary
described below, emitted as exact prefix-code bits. It carries no compression
flag, original-size field or padding. The receiver decodes complete dictionary
tokens through the detected burst endpoint and only presents this automatic
short-text interpretation when the result is below 16 bytes. Truncated or
uninterpretable input remains available as raw bits. For example, the byte
`e` is its three-bit dictionary code, rather than a framed one-byte message.

Text of at least 16 source bytes, files and screenshots use the compact
packet codec below, with initial and periodic byte-boundary recovery on the
pattern transport. Files and screenshots retain that representation even
when their payload is below 16 bytes; the application does not invent an
untransmitted file type or filename from raw bits. Direct packet APIs can also
encode short text, but those framed bytes are separate from the normal
short-text transfer path.

The receiver preserves detected bits independently of packet validity. Its
`raw_bits` diagnostic retains Data-stream decryption of the observed bits,
including transport markers, with zero placeholders for missing interior
symbol slots; `missing_symbols` counts those unobserved bits. It is distinct from the
recovered logical packet candidate. A burst may subsequently validate as a
compact packet; short raw bursts may provide a dictionary interpretation. Neither interpretation
changes which waveform timing or pattern candidates were selected. Keyed raw
bits use the independent Data stream before transmission; no authentication
tag is silently added to them.

## Framed packet sequence

The packet contains a **variable compact bootstrap**, then the optionally
Reed–Solomon-coded and interleaved body. Automatic binary pattern modulation
sends each wire bit, including the initial and periodic recovery markers
described below, as a separate pattern symbol, without symbol padding. The same hardware-settling
prefix described above can precede it; it is separate from the packet and
supplies no modem training.

There is no APSK training path or final-symbol zero padding. Byte-oriented
`pack`/`unpack` APIs remain separate from modulation and do not insert or remove
transport byte-boundary recovery markers.

### Periodic byte-boundary recovery

For compact packets on the pattern transport, the sender inserts a fixed
24-byte marker before the encoded packet and after every complete 256 bytes of
that packet, counting the protected bootstrap, interleaved body and parity.
This includes a final block of exactly 256 bytes. A shorter encoded packet has
only the initial marker. The rule applies with or without encryption and FEC;
the 16-original-byte FEC threshold
does not change this encoded-byte cadence. Exact raw-bit and short dictionary
transmissions bypass it.

The marker consists of the same 96-bit word repeated twice. At runtime the word
is the first 12 digest bytes of SHA-256 over the ASCII label
`DataPump/byte-boundary/v1`, without a terminating zero; bytes are emitted most
significant bit first. Source and executable storage use the derivation label,
not the literal marker bytes. This reduces accidental self-recognition when
transferring the program or source. It cannot make a finite marker absent from
every possible file or memory dump.

`message_bits` returns logical packet bits before transport processing.
`message_wire_bits` inserts markers into those bits, then applies the optional
Data-stream mask to the entire result, including every marker bit. Markers
consume Data-stream positions, and all transmitted bits then pass through the
configured pattern mapping and Scrambler/DSSS layers. Keyed transmission never
places an unencrypted recovery marker on the wire. With `N` encoded bytes,
overhead is `24 * (1 + floor(N / 256))` bytes: 24 bytes initially plus 9.375% per
full block. Markers contain no lengths, types, addresses, commands or authentication.

After pattern acquisition and the existing whole-stream Data decryption,
recovery starts at the existing burst origin and examines only the expected
plaintext marker slots. Initial starts are searched at offsets 0 through 7 bits,
and subsequent starts from -7 through +7 bits around each expected boundary.
The rest of the payload is never searched. Exact matches take the fast path.
Otherwise a complete 192-bit marker may have up to eight changed bits. A partial
marker may have one contiguous run of 1 through 80 missing bits, including a
missing prefix, provided its final 32 bits survive and match exactly. The
remaining observed bits may contain changes within the confidence budget below.
Arbitrary distributed deletions and a missing marker suffix are outside this
model: a surviving prefix alone cannot identify where packet data begins.

Timed unknown symbol slots retain their positions through Data decryption and
marker matching. They contribute neither matches nor mismatches: the evidence
calculation uses only observed 0/1 bits. Distributed unknown slots therefore
need no deletion-path search. They cannot satisfy the exact trailing anchor
required when inferring a deleted marker run. Recovered data slots become
plaintext zero placeholders before byte packing and Reed–Solomon correction.

If pattern acquisition supplies a leading stream-symbol index from 1 through
80, recovery compares the surviving initial marker suffix after the existing
decryption at that index. This uses the same evidence threshold and does not
try alternative crypto positions. In an unkeyed burst, the bounded deletion
search can also infer a missing marker prefix without a known stream index.
The acquired index fixes the known suffix's endpoint, so that comparison may
spend its mismatch budget or retain unknown slots even in the final 32 bits.
An inferred deletion still requires the exact observed trailing anchor.

Passing matches must identify a unique marker endpoint. Equivalent deletion
paths ending at the same bit collapse to their strongest evidence; competing
endpoints reject recovery. For a matched periodic slot, the preceding plaintext
interval is normalized to 2,048 bits: retain its prefix, trim extra tail bits or
zero-fill a missing tail. Different starts sharing an endpoint can leave the
last few preceding data bits uncertain, so the affected region still depends
on FEC and whole-packet integrity. A damaged unrecognized marker consumes its
nominal slot when available, preserving an otherwise aligned packet; a later
recognized marker can restore alignment. Incomplete unrecognized slots remain
uninterpreted.

#### Marker evidence threshold

The acceptance bound assumes the known input bits are independent and fair
conditional on the unknown-slot positions. For one fixed hypothesis with `n`
observed marker bits (excluding unknown slots) and at most `e` mismatches, its
false-match probability is at most `V(n,e) / 2^n`, where
`V(n,e) = sum(i=0..e, binom(n,i))`. This is the cumulative
[binomial probability at p = 1/2](https://itl.nist.gov/div898/handbook/eda/section3/eda366i.htm).
The exact trailing anchor further restricts partial matches; the bound
conservatively counts all mismatch positions anyway.

All starts and deletion runs are charged to
`H = 15 * (1 + sum(d=1..80, 193-d-32)) = 144615` hypotheses per slot.
For `B` received slots including unknowns, `S = 1 + floor(B / (2048-7+112))` bounds the number
of slots. Each candidate, with `e <= 8`, must satisfy
`n - ceil(log2(S)) - ceil(log2(H)) - ceil(log2(V(n,e))) >= 84`.
These conservative rounded costs and the
[union bound](https://math.iisc.ac.in/~gadgil/MA261/notes/chapter-8.html)
limit the chance of any false marker acceptance in one recovery call to
`2^-84` under that input model, accounting for all tested starts, deletion
runs and mismatch patterns. Shorter surviving markers and larger inputs
therefore permit fewer mismatches, or fail the threshold entirely. The two
repeated words still constrain distinct observed bits under this model.
For a short input with one charged slot, 112 surviving bits (80 missing) and
one mismatch provide 87 evidence bits after trial costs; two mismatches
provide only 81 and fail the threshold. With 128 surviving bits, up to four
mismatches provide 86 evidence bits. These counts describe a fixed candidate;
equivalent deletion paths can explain the same endpoint with fewer mismatches.
With no mismatches and one charged slot, at least 102 known matching bits are
required. Filling an unknown slot with zero never increases that evidence.
This is an analytic random-input bound, not a measured channel error rate,
a posterior probability that a boundary is correct, or an authentication
claim. It supplies no statistical guarantee for deliberately constructed data.

Marker matching and removal operate after the existing Data-stream decryption,
before body deinterleaving, Reed–Solomon correction and final integrity checks.
A marker never starts a packet parser or a nested message. The recovered
candidate permits a single compact packet at the burst
origin whose declared extent must equal the complete recovered byte extent.
Failed validation does not trigger a second attempt on the unstripped stream
or a fallback to older packets without the initial marker.
Short dictionary interpretation is limited to 195 acquired bits (15 maximally
escaped bytes). A recognized complete or partial marker also prevents
dictionary fallback when packet validation fails, including a short damaged
packet. A marker supplies framing evidence; validated packet metadata still
determines whether the content is text, a file or a screenshot. Acquired raw
bits remain available as diagnostics independently of either interpretation.

This repairs byte alignment after a net shift of at most seven decoded plaintext
data bits per searched slot, together with the bounded marker damage above.
Marker normalization cannot reconstruct a damaged data tail without a later recognized marker,
whole missing blocks or an unknown absolute stream offset. Pattern constellation
decoding remains the sole authority for timing
and keystream alignment. Recovery never trials cryptographic offsets, resets
counters or reseeds streams; it cannot restore Data-stream, Scrambler or DSSS
alignment. Subsequent bits must still be acquired and decrypted correctly in
the same burst. There is no improvement to waveform acquisition or carrier/clock
tracking. A recognized marker does not authenticate content: the complete packet must still
pass SHA-256 or keyed HMAC-SHA256 before validated content is released.

### Compact bootstrap

The systematic bootstrap occupies four to eight bytes:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | Packed controls, below |
| 1 | 1–5 | Uncoded body length as canonical ULEB128, including metadata and integrity tag |
| following | 2 | Big-endian CRC-16/CCITT-FALSE of controls and encoded length |
| following | 0–6 | Shortened RS parity, using the effective data FEC policy |

Control bits 0–1 select body FEC: 0 off, 1 RS20, 2 RS60; 3 is invalid.
Bits 2–3 select content kind: 0 text, 1 file, 2 screenshot; 3 is invalid.
Bit 4 indicates compressed payload, bit 5 keyed authentication and bit 6 a
repeat request. Bit 7 is zero. There is no format-version byte, compression
algorithm selector, dictionary identifier, transmitted codebook, tag-length
field, packet-start magic or duplicate payload-length field. The initial and
periodic transport markers above are outside the packet codec. Original content below
16 bytes forces FEC Off for both header and body, regardless of the requested
mode. This threshold concerns original bytes, including UTF-8 byte counts,
and applies to files as well as text. It does not depend on compressed size or
the length of metadata strings.

The CRC uses polynomial `0x1021`, initial value `0xFFFF`, no reflection and no
final XOR. Header parity uses the same even-rounded 20% or 60% rule as body
blocks; FEC Off adds none. A four-byte header therefore uses zero, two or four
parity bytes. A five-byte header uses zero, two or four. This is the same nominal
coding policy; rounding small blocks to even parity makes their actual overhead
ratio higher. Header and body always use the same modulation and symbol rate.

Within this packet codec, ordinary text under 16 bytes has a **four-byte total
bootstrap**; normal short-text transfer bypasses it. Packet text under
256 bytes usually has four or five systematic bytes and at most four parity
bytes. Long metadata strings can increase the length field by one byte. The
maximum bootstrap is 14 bytes, a receive-probe bound, not transmitted padding.
The receiver tries bounded header-length and FEC hypotheses, repairs enabled
RS codewords, and validates controls, canonical length and CRC before using a
declared size. The CRC is a candidate filter, not a substitute for final integrity.

The body length determines codeword and interleaver dimensions. The receiver
checks that length and its working-buffer cost before allocation.
`packet_frame_size` accepts a complete variable bootstrap; `packet_header_extent`
returns its actual protected length. Incomplete prefixes return no size.
`decode_packet` reports the exact consumed length and ignores a demodulator
tail after that frame. Pattern transport additionally requires that consumed
length equal the complete candidate extent; it does not extract inner packets
or accept a valid packet followed by extra candidate bytes.

Automatic acquisition fits the legal pattern waveforms against noise, with
unknown common phase and amplitude. It does not use the APSK lattice, packet
bootstrap, CRC or MAC as a signal-lock gate. Packet sizes and integrity are
validated only after accepted pattern bits become available.
Finite frequency/timing coverage, noise and clock drift still limit reception.

The validated packet length terminates packet content. Pattern evidence
separately terminates a detected burst. A fade can split a burst because signal
loss alone cannot establish whether a transmitter intended to stop. No end
marker or zero-byte guard is sent beyond the periodic recovery cadence;
finishing a finite capture is local and does
not increase transmitted airtime.

## Logical body before coding

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 16 | Packet identifier |
| 16 | 4 | CRC-32 of the identifier followed by one repeat-flag byte |
| 20 | 1 | Filename length, 0–255 |
| 21 | 1 | Callsign length, 0–64 |
| 22 | 1 | Grid length, 0–32 |
| 23 | 1–5 | Original payload byte count, canonical unsigned LEB128 |
| following | variable | Filename, callsign, then grid, without terminators |
| following | variable | Compressed or original data |
| final | 32 | SHA-256 digest or keyed HMAC-SHA256 |

The original length fits uint32. ULEB128 uses low seven-bit groups first and
bit 7 for continuation. The decoder rejects overflow, more than five bytes,
truncation and redundant zero groups. Payload length follows from body length
minus metadata, strings and the fixed tag; it is not stored a second time.
An ordinary short text has 24 fixed metadata bytes before any strings.

An all-zero application identifier requests a random 16-byte identifier. A
repeat retains the received identifier and repeat flag. The independent ID
checksum uses reflected CRC-32, polynomial `0xEDB88320`, initial and final XOR
`0xFFFFFFFF`. It also limits accidental identity changes in pending previews.
Callsign and grid describe content; no routing or destination address exists.

These metadata fields and the repeat flag remain available to the CLI and
packet API. The desktop Callsign, Grid and Repeatable controls instead insert
editable in-band text into Message; desktop messages leave their corresponding
packet metadata empty and the packet repeat flag off. See the
[desktop compose behavior](../README.md#desktop-console).

The digest/MAC covers the canonical variable systematic bootstrap followed
by the logical metadata and encoded payload. It excludes the tag itself,
hardware settling, parity and transport recovery markers. There are no symbol pad bits. A keyed
receiver rejects unkeyed packets;
SHA-256 alone supplies integrity, not authentication. Transfer-layer keyed
MACs bind the local epoch, which is not transmitted as a packet field.

For automatic pattern transport, private Data-stream encryption wraps all wire
bits after recovery markers are inserted, independently of private pattern and
DSSS streams. Protected settling uses those same selected keys with separate
preamble counter positions, mixing the streams before noise amplitude/phase
mapping. Tone modes force all private protections off. No public whitening
layer is added after encryption.

## Automatic compression

For framed packets, compression is attempted automatically unless explicitly
disabled by the CLI.
It is selected only when it saves at least one whole payload byte. The single
compressed flag and the original length completely determine decoding:

* Below 256 original bytes: one fixed byte-prefix code.
* At least 256 original bytes: raw LZMA2 using preset 9 extreme search settings.
* Compressed flag clear: original bytes, with no expansion or codec overhead.

Neither codec sends a built-in dictionary, dictionary identifier or codebook.
There is no fallback to a legacy packet decoder or old phrase dictionary.

### Fixed short-byte code

Tokens are packed most-significant bit first. Common lowercase letters have
short codes, as in the frequency-weighted idea behind Morse code:

| Bytes in code order | Codes | Bits |
| --- | --- | ---: |
| space, `e`, `t`, `a`, `o` | `000` through `100` | 3 |
| `i`, `n` | `1010`, `1011` | 4 |
| `s h r d l u c m f w y p b g` | `110000` through `111101` | 6 |
| Every other byte | `11111` followed by its eight literal bits | 13 |

This codes bytes, including arbitrary binary and UTF-8. There are no phrase
tokens. Escaping a directly coded byte is noncanonical. The exact-bit short-text
path terminates at the detected bit endpoint and permits no padding. In the
framed packet representation, the original byte count terminates decoding and
only zero to seven zero pad bits may follow. Extra bytes, nonzero padding,
truncated tokens and expansion beyond the count are rejected.

### Long data and files

The raw LZMA2 profile uses preset 9 with the extreme option and the BT4 match
finder. Its history window is `min(64 MiB, max(4 KiB, original byte count))`,
derived identically by sender and receiver. Small inputs therefore do not
allocate a full 64 MiB window. The stream carries no XZ container, dictionary
identifier or history-size field. Raw LZMA2's own chunk controls remain part of
the compressed data. Packet integrity covers the entire compressed stream.

The implementation statically builds the pinned liblzma source included in the
repository. Copying the portable application needs no library download or
installed compression package. Encoder scratch is capped at 768 MiB and decoder
scratch at 80 MiB, with preflight estimates and an enforcing allocator. These
bounds are separate from the received-content cache and DSP workspace. Actual
scratch shrinks with the derived history window. Maximal search can cost CPU;
simulation and GUI estimation run this work outside the UI thread.

Decoded output is bounded before allocation, and exact original length, stream
end and absence of trailing data are checked. An incompressible candidate is
discarded without allowing its output buffer to grow beyond the input size.

## Reed–Solomon and interleaving

All RS codewords are systematic, shortened over GF(256), polynomial `0x11D`,
first generator root alpha^0. Bootstrap and body use the same selected coding
policy, automatically Off below 16 original bytes. Body coding uses these
maximum data-block sizes:

| Body mode | Full data bytes | Full parity bytes |
| --- | ---: | ---: |
| Off | No blocks | 0 |
| RS20 | 210 | 42 |
| RS60 | 150 | 90 |

For a shortened block with `n` data bytes, parity is the even-rounded ceiling
of `n/5` or `3n/5`. The percentages describe parity overhead relative to data,
not correctable-error percentages. A block corrects at most half its parity
count in erroneous bytes. The final block is shortened, not padded.

Body codewords are transmitted by columns across rows. Columns skip absent
cells in the shortened final row. Parity therefore is not one contiguous
footer after the logical data. The bootstrap is separate from this interleave.
Pre-FEC accuracy compares received and corrected systematic body bits, including
encoded data, metadata and tag; bootstrap and parity do not enter its denominator.
Only a completely verified packet supplies this statistic.

Timed missing bits currently enter packet decoding as plaintext zeroes at
their original positions. The RS decoder treats affected bytes as ordinary
errors; it does not yet accept known erasure positions. This restores the byte
grouping that a dropped bit would destroy, but does not increase the code's
error budget. A missing bit whose transmitted value was zero needs no change.
The reported missing-symbol count distinguishes those inferred slots from
observed decisions. A 100% pre-FEC body accuracy can also coexist with repairs
to header or parity bytes, which are excluded from that metric.

## Previews and resource limits

Pending previews read a bounded systematic prefix of incoming body columns and
check metadata before decoding complete short tokens or a bounded LZMA2 prefix.
Incomplete compressed input can provide provisional text. A preview is mutable,
unverified, not copyable and never eligible to save or execute. Full integrity
verification and bounded decompression are required before releasing content.

Packet buffers, declared original output, compression scratch, DSP workspace and
received-content storage are bounded separately. Corrupt lengths are validated
before related allocations. Attachment names must be safe basenames and valid
UTF-8; traversal, separators, control characters, reserved device names and
malformed metadata are rejected independently of packet integrity.

Repeat policy is unchanged: compare actual encoded content airtime against the
same message with an empty payload, retaining the original packet's effective
coding policy for this numeric baseline. Fixed training, header and metadata costs
are excluded; growth of the variable original-length field and its parity is
part of the incremental encoded cost. At most two seconds of incremental content, or at most one
original payload byte, is allowed by the default policy. Packet sizes and time
estimates reflect actual compression, FEC, initial and periodic transport
recovery overhead where applicable and the single packet symbol boundary.

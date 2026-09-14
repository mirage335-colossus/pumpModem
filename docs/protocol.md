# Data Pump pattern transport and compact packet format

Automatic modem profiles use one meaningful bit per binary pattern symbol.
Pattern evidence establishes signal presence, timing and burst boundaries
before application decoding. Both audio peers need matching modem settings.
The compact packet codec remains available for larger text, files and
screenshots; it does not define the presence of a raw-bit signal. Existing
keyfiles remain compatible. Receiving a file never executes it.

The automatic waveform is incompatible with the previous automatic APSK
waveform. Both peers must use the new pattern transport, or explicitly select
matching legacy APSK settings; the receiver does not negotiate this over air.

## Raw bits and short text

Raw binary input is transmitted exactly as entered, including leading zeros
and lengths not divisible by eight. Three input bits produce exactly three
payload pattern symbols. There is no packet header, checksum, MAC, FEC,
byte-padding bit, or transmitted length field on this path.

A separate hardware-settling prefix precedes a nonempty transmission when five
seconds rounds to at least one whole payload-symbol duration. Rounding is to
nearest, with ties upward: symbols longer than ten seconds add no prefix. This
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

Text of at least 16 source bytes, files and screenshots use the existing compact
packet codec below. Files and screenshots retain that representation even
when their payload is below 16 bytes; the application does not invent an
untransmitted file type or filename from raw bits. Direct packet APIs can also
encode short text, but those framed bytes are separate from the normal
short-text transfer path.

The receiver preserves detected raw bits independently of packet validity. A
byte-aligned burst may subsequently validate as a compact packet; otherwise
the short dictionary may provide a text interpretation. Neither interpretation
changes which waveform timing or pattern candidates were selected. Keyed raw
bits use the independent Data stream before transmission; no authentication
tag is silently added to them.

## Framed packet sequence

The packet contains a **variable compact bootstrap**, then the optionally
Reed–Solomon-coded and interleaved body. Automatic binary pattern modulation
sends each packed packet bit as a separate pattern symbol, without symbol
padding. The same hardware-settling prefix described above can precede it;
it is separate from the packet and supplies no modem training.

Manual legacy APSK configurations additionally send five seconds of training:
32 input bytes mapped onto 64 four-bit APSK segments, independent of the payload
symbol clock. Their bootstrap and body form one continuous APSK bitstream, with
unused bits possible only in its final symbol. Those legacy waveform details
do not add bits to the automatic raw pattern path.

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
field, magic marker or duplicate payload-length field. Original content below
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
tail after that frame.

Automatic acquisition fits the legal pattern waveforms against noise, with
unknown common phase and amplitude. It does not use the APSK lattice, packet
bootstrap, CRC or MAC as a signal-lock gate. Packet sizes and integrity are
validated only after accepted pattern bits become available. Legacy APSK
acquisition retains its earlier amplitude/phase and protected-bootstrap search.
Finite frequency/timing coverage, noise and clock drift still limit reception.

The validated packet length terminates packet content. Pattern evidence
separately terminates a detected burst. A fade can split a burst because signal
loss alone cannot establish whether a transmitter intended to stop. No end
marker or zero-byte guard is sent; finishing a finite capture is local and does
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
training, parity and symbol pad bits. A keyed receiver rejects unkeyed packets;
SHA-256 alone supplies integrity, not authentication. Transfer-layer keyed
MACs bind the local epoch, which is not transmitted as a packet field.

For automatic pattern transport, private Data-stream encryption wraps the exact
payload bits, independently of private pattern and DSSS streams. The manual
legacy audio path wraps training and the complete protected packet, then applies
its public whitening mask to the packet, excluding training. Its receiver
reverses whitening and private masking before FEC and integrity verification.
Public whitening is reversible scrambling, not encryption.

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
estimates reflect actual compression, FEC and the single packet symbol boundary.

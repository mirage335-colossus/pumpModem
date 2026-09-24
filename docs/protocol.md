# Data Pump compact packet format

Release 0.6.0 uses this format exclusively. The old packet formats and their
compression selectors are removed. Both audio peers must use the compact
format and matching modem settings. Existing keyfiles and the separate raw-bit
transmission path are unchanged. Receiving a file never executes it.

## On-air sequence

The modem sends its fixed five-second training, then a **16-byte protected
bootstrap**, then the optionally Reed–Solomon-coded and interleaved body.
Training contains 32 input bytes mapped onto 64 four-bit APSK segments; it is
independent of the payload symbol clock. Bootstrap and body end at separate
APSK symbol boundaries. Their final zero pad bits are not packet bytes.

The eight systematic bootstrap bytes have this network-byte-order layout:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | Fixed acquisition marker `D5` |
| 1 | 1 | Packed controls, below |
| 2 | 4 | Uncoded body length, including metadata and integrity tag |
| 6 | 2 | CRC-16/CCITT-FALSE of bytes 0–5 |
| 8 | 8 | Shortened RS parity over the eight systematic bytes |

Control bits 0–1 select body FEC: 0 off, 1 RS20, 2 RS60; 3 is invalid.
Bits 2–3 select content kind: 0 text, 1 file, 2 screenshot; 3 is invalid.
Bit 4 indicates compressed payload, bit 5 keyed authentication and bit 6 a
repeat request. Bit 7 is zero. There is no format-version byte, compression
algorithm selector, dictionary identifier, transmitted codebook, tag-length
field or duplicate payload-length field.

The CRC uses polynomial `0x1021`, initial value `0xFFFF`, no reflection and no
final XOR. RS(16,8) corrects up to four damaged bytes, including damage to the
CRC. A conservative prefilter can reject only independently contradictory
bytes; full RS, CRC and length validation decide acquisition. The bootstrap
is 56 bytes shorter than the former 72-byte prefix. Its smaller observation
window uses alternate radial-gain hypotheses during symbol acquisition.

The body length determines codeword and interleaver dimensions. The receiver
checks that length and its working-buffer cost before allocation.
`packet_frame_size` needs all 16 bootstrap bytes; `decode_packet` reports the
exact consumed length and ignores a demodulator tail after that frame.

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

The digest/MAC covers the canonical eight-byte systematic bootstrap followed
by the logical metadata and encoded payload. It excludes the tag itself,
training, parity and symbol pad bits. A keyed receiver rejects unkeyed packets;
SHA-256 alone supplies integrity, not authentication. Transfer-layer keyed
MACs bind the local epoch, which is not transmitted as a packet field.

Private stream encryption wraps training and the complete protected packet.
The existing public whitening mask is then applied to the packet, excluding
training. The receiver reverses whitening and private masking before FEC and
integrity verification. Whitening is reversible scrambling, not encryption.

## Automatic compression

Compression is attempted automatically unless explicitly disabled by the CLI.
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
tokens. Escaping a directly coded byte is noncanonical. The original byte count
terminates decoding; only zero to seven zero pad bits may follow. Extra bytes,
nonzero padding, truncated tokens and expansion beyond the count are rejected.

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
first generator root alpha^0. Bootstrap parity is always eight bytes, including
when body FEC is Off. Body coding uses these maximum data-block sizes:

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
same message with an empty payload. Fixed training, header and metadata costs
are excluded; growth of the variable original-length field and its parity is
part of the incremental encoded cost. At most two seconds of incremental content, or at most one
original payload byte, is allowed by the default policy. Packet sizes and time
estimates reflect actual compression, body FEC and separate symbol boundaries.

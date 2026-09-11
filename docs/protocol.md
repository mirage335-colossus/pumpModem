# Data Pump packet format, versions 1 and 2

This document specifies the implemented packet codec. It is a versioned project
format, not a claim of compatibility with an existing radio modem. Integers use
network byte order. A packet has no destination, source address, route, or hop
field. Optional callsign and grid strings describe content and are never used for
routing. Content is text, a file, or a screenshot; receiving does not execute it.
Version 2 adds a variable-length short-payload prefix code. The framing, metadata,
integrity, FEC, and interleaving layouts remain the same. The decoder accepts both
versions; an older decoder that accepts only version 1 cannot read version 2.

Application release 0.5.1 retains differential 4/8/16/32/64-APSK and symbol-boundary
padding around the protected bootstrap. It applies a public additive whitening
mask to the audio frame after encryption, excluding the training. Audio peers
require matching modem settings and the 0.5 whitening mask. Release 0.5.1 changes
narrow automatic audio defaults to a 1500 Hz carrier, so both endpoints must
agree on the carrier and internal clock as well as bandwidth and pattern.
This does not change either packet version or the existing keyfile formats;
modem configuration and packet codec versions are separate concepts.

Whitening is reversed before private decryption and packet FEC/validation. It
adds no bytes, entropy, authentication, secrecy or error propagation. The exact
mask is specified in [modem.md](modem.md); `pack`/`unpack` output is unchanged.

The audio synchronization preamble is outside the packet codec. It is neither
covered by packet Reed–Solomon coding nor by the packet digest/MAC. When
encryption is enabled, the transmitter encrypts the complete modem frame,
including its preamble and all packet error correction. The packet codec itself
does not encrypt and never adds an encryption nonce or public-key envelope.

## Wire layout

The wire packet consists of a 72-byte protected bootstrap followed by an
interleaved, optionally Reed–Solomon-protected body. Each bootstrap is a shortened
RS(72,40) codeword with 32 parity bytes. Its first 40 bytes are systematic data:

| Byte offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | ASCII `DP01` |
| 4 | 1 | Format version, `1` legacy dictionary or `2` variable-length prefix code |
| 5 | 1 | Body FEC: `0` off, `1` 20%, `2` 60% |
| 6 | 1 | Flags: bit 0 dictionary compression, bit 1 keyed MAC, bit 2 request repeat |
| 7 | 1 | Content kind: `0` text, `1` file, `2` screenshot |
| 8 | 8 | Body length before body FEC, including the 32-byte tag |
| 16 | 8 | Original payload byte count |
| 24 | 8 | Encoded payload byte count |
| 32 | 2 | Tag length, always `32` |
| 34 | 2 | Reserved, both zero |
| 36 | 4 | CRC-32 of bootstrap bytes 0–35 |

The receiver corrects this fixed-size codeword, checks its magic, version,
reserved values, flags, CRC, and internal lengths, and enforces the memory budget
before allocating any advertised body. The CRC uses the reflected polynomial
`0xedb88320`, with initial and final XOR `0xffffffff`. This CRC limits accidental
false prefixes; it is not authentication. All 40 bootstrap bytes are also covered
by the final digest or keyed MAC, so modifying FEC mode, lengths, flags, or kind
cannot produce an accepted keyed packet without a valid tag.

`packet_frame_size` returns the total required bytes from a complete valid
bootstrap. With fewer than 72 bytes it returns no size. `decode_packet` accepts
extra demodulator tail bytes and reports the exact consumed packet length.

The decoded body is:

| Byte offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 16 | Random packet identifier |
| 16 | 4 | CRC-32 of the 16 ID bytes followed by one repeat-flag byte (`0` or `1`) |
| 20 | 2 | Filename byte count, 0–255 |
| 22 | 2 | Callsign byte count, 0–64 |
| 24 | 2 | Grid byte count, 0–32 |
| 26 | variable | Filename, then callsign, then grid, with no terminators |
| following | variable | Encoded payload |
| final | 32 | Integrity digest or keyed message authentication code |

If the application supplies an all-zero identifier, the encoder generates 16
bytes using OpenSSL's cryptographic random generator. A repeated packet keeps
the identifier from the decoded message. The repeat flag requests repetition by
an external application; the library does not retransmit or implement a
repeater. The independent ID checksum is checked even when the full body digest
is valid.

Repeatability is a shared transfer-service policy, not an extra packet-size field
or a raw-byte limit. With the selected compression and FEC options, the service
encodes both the complete message and a baseline message with identical metadata,
kind, identifier, and repeat flag but an empty payload:

```text
content_bytes   = max(0, full_packet_bytes - empty_payload_packet_bytes)
content_symbols = max(0, payload_symbols(full_packet_bytes) - payload_symbols(empty_payload_packet_bytes))
content_seconds = content_symbols * symbol_sample_count / internal_sample_rate
repeat_allowed  = content_seconds <= 2 OR original_payload_bytes <= 1
```

The subtraction excludes the fixed bootstrap, ID, metadata, tag, and their fixed
FEC cost while including incremental payload coding/parity, compression and
symbol-boundary padding. `payload_symbols` counts the protected bootstrap and
remaining body with their separate symbol boundaries.
The preamble is outside both packets and never consumes this allowance. There is
no 64 KiB limit. The threshold and one-byte floor are configurable through
`transfer::RepeatPolicy`; the defaults above preserve a repeatable one-byte
distress message even at an extremely slow symbol rate. `pack` and `transmit`
reject a requested repeat flag when this policy fails. The codec can still decode
the flag independently of a local channel's speed.

`transfer::estimate` reports content airtime, complete packet airtime, and total
airtime including the preamble separately. Its repeatability result is separate
from streaming DSP and content feasibility. The default preamble occupies five
seconds independently of payload symbol duration. Streaming operations retain
bounded DSP state, while legacy batch PCM/WAV operations can still exceed their
separate full-waveform budget. `memory_supported` reports streaming feasibility;
`batch_memory_supported` reports the latter boundary. The one-byte floor does
not bypass content, DSP or numeric-range checks and does not add a repeater.

The canonical input to the digest/MAC is the 40-byte bootstrap concatenated with
the entire body except its final 32-byte tag. Unkeyed packets use SHA-256; this
detects corruption but does not authenticate a sender. Keyed packets use the
provided 32-byte MAC callback. A receiver configured with a verifier rejects
unkeyed packets; a receiver without one rejects keyed packets. Successful Reed–
Solomon correction alone never releases a validated message. The decoder also
requires the full digest or MAC and all metadata checks to pass.
The shared transfer service's keyed callback also prepends ASCII `DP-EPOCH`,
the byte `01`, and the eight-byte big-endian candidate timestamp to this canonical input. A frame
therefore cannot pass that service's MAC check under a different candidate epoch.

## Reed–Solomon and interleaving

RS uses GF(256), primitive polynomial `x^8 + x^4 + x^3 + x^2 + 1` (`0x11d`),
primitive element 2, and generator roots `alpha^0` through `alpha^(p-1)`. All
codewords are systematic, highest-degree coefficient first; shortened words
omit leading zero data symbols. For example, data `01` with two parity symbols
encodes as `01 03 02`.

The 20% setting divides the body into at most 210 data bytes per block; full
blocks have 42 parity bytes. The 60% setting uses at most 150 data bytes and 90
parity bytes. A final short block uses `ceil(k/5)` or `ceil(3k/5)` parity bytes,
rounded up to the next even number. Thus percentages refer to parity relative
to data, and rounding adds up to two bytes for short blocks. An RS block can
correct at most half its parity count in unknown byte errors. No erasure hints
are required. Decoding uses Berlekamp–Massey, a root search, and a small GF(256)
linear solve for error magnitudes, then checks all syndromes again.

The encoder lays full systematic codewords out as rows and transmits columns:
byte zero of each row, byte one of each row, and so on, skipping nonexistent
positions of the final short row. The receiver reverses this interleaver before
correction. This distributes a contiguous burst across blocks. With FEC off,
the body is transmitted directly without interleaving; the bootstrap still has
its fixed protection. Every body byte, including its digest/MAC, is covered by
body FEC when enabled. Beyond the correction radius, RS may fail or miscorrect;
the independent final digest/MAC is mandatory in either case.

## Deterministic short compression

Payloads below 256 bytes are eligible for the fixed dictionary, regardless of
content kind. The compressor operates on bytes, so UTF-8, embedded NULs, and
arbitrary binary data remain lossless. It uses compression only when the encoded
byte sequence is strictly shorter; all other payloads are stored verbatim. The
flag and both lengths are authenticated in the bootstrap.

The encoder tries both codecs and chooses version 2 only when its whole-byte
output is strictly shorter than version 1. Ties retain version 1. If neither
codec saves a byte, the payload is uncompressed and the encoder emits version 1.
The receiver selects the compressed representation from the authenticated
version byte. An uncompressed payload has the same meaning in either version.

### Version 1 dictionary tokens

Tokens in both versions are packed most-significant bit first:

| Prefix | Following bits | Meaning |
| --- | --- | --- |
| `00` | 5-bit index | One character from the 32-byte alphabet below |
| `01` | 6-bit index | One dictionary entry below |
| `10` | 8-bit value | One literal byte |
| `11` | none | Reserved, rejected |

The alphabet in index order is the following escaped ASCII string:
`" etaoinshrdlucmfwypvbgkqjxz0123\n"`.

The 64 dictionary entries, in index order, are:

```text
"the ", "The ", "and ", "message", "received", "station", "ready", "please",
"hello", "thank", "you", "this ", "that ", "with ", "from ", "have ",
"for ", "your ", "will ", "are ", "not ", "can ", "all ", "test",
"ing", "tion", " to ", " is ", " in ", " of ", " on ", " at ",
"CQ", "QRS", "73", "599", "copy", "send", "file", "next",
"time", "good", "signal", "power", "radio", "call", "grid", "data",
"pump", "status", "normal", "distress", "over", "out", "yes", "no",
"http", "://", ".com", "www.", "00", "11", "  ", "\r\n"
```

At each input offset, the encoder chooses the longest matching dictionary entry,
breaking ties by its lowest index. If none matches, it uses an alphabet token
when possible, otherwise a literal. There is no end marker: decoding stops at
the authenticated original byte length. Up to seven trailing zero padding bits
are allowed; extra bytes, nonzero padding, reserved tokens, truncation, and
expansion beyond the declared length are rejected. This is a deliberately
small, fixed dictionary, not a claim of a statistically optimal compressor.

### Version 2 variable-length prefix tokens

Version 2 gives short prefix codes to thirteen frequent ASCII bytes. It reuses
the exact 64-entry dictionary above and has a literal escape for every other
byte. There is no transmitted model or runtime-trained dictionary.

| Prefix | Following bits | Meaning |
| --- | --- | --- |
| `000`, `001`, `010`, `011`, `100` | None | Space, `e`, `t`, `a`, `o`, respectively |
| `1010`, `1011` | None | `i`, `n` |
| `11000`, `11001`, `11010`, `11011` | None | `s`, `h`, `r`, `d` |
| `11100`, `11101` | None | `l`, `u` |
| `111100` | 6-bit index | Dictionary entry, 12 bits in total |
| `111101` | None | Reserved, rejected |
| `11111` | 8-bit value | Literal byte, 13 bits in total |

The encoder uses dynamic programming over the at-most-255-byte input to minimize
bit count across byte and phrase tokens. Equal-cost choices retain the byte
token, or the earlier dictionary index when multiple phrases improve on it.
The verified original length ends decoding; there is no end marker. Both
versions reject expansion beyond that length, truncation, extra whole bytes,
and nonzero padding. For example,
the bytes space, `e`, `t` encode as `000 001 010` plus seven padding zeros, yielding
`05 00` in hexadecimal. Eighty `e` bytes occupy thirty encoded bytes. This example
demonstrates the three-bit common-byte code and does not promise a three-bit
average for general text, Unicode, or binary data.

## Provisional packet previews

`preview_packet_partial` offers a bounded best-effort view of incomplete hard
bytes after a usable bootstrap and metadata prefix are available. It can expose
complete tokens from an incomplete version 1 or 2 compressed stream. These bytes
have not passed final integrity/authentication and may change after further FEC
correction or complete decoding. A preview must remain visibly provisional and
must not become a verified cache entry, save result, clipboard result, or repeat
request. `decode_packet` remains the only validated-message boundary.

## Resource and attachment validation

The configured decode memory limit defaults to 256 MiB. Before body allocation,
the implementation budgets coded-body bytes, twice the decoded body, original
payload length, and 4096 bytes for fixed decoding scratch. The caller-owned input
buffer is excluded. Checked size arithmetic rejects integer overflow, and
metadata and dictionary expansion have separate small bounds. Consequently the
maximum payload is below the configured memory limit; applications should also
budget their received-message cache. The streaming transfer service separates
application `content_limit` (default 256 MiB) from `dsp_workspace_bytes` (default
64 MiB) and derives checked packet scratch bounds from the content limit. Long
payload symbol durations do not allocate correspondingly long audio buffers.
These are distinct bounds rather than a single cap on total process memory.

Encoding also checks its memory budget before allocating payload-sized copies.
Its conservative budget uses the uncompressed size and includes the caller's
message bytes, twice the maximum coded-body bytes, twice the maximum uncoded-body
bytes, and 4096 bytes of scratch. The encoder does not rely on compression to fit
an oversized input within the limit.

File and screenshot content requires a basename; text may omit it. Filenames
cannot contain directory separators, drive separators, control bytes, Windows
forbidden punctuation, trailing spaces or periods, or reserved device names.
Device checks include the superscript port digits listed in Microsoft's
[Windows filename rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
Filename, callsign, and grid strings must be valid UTF-8. Callsign and grid strings
also reject ASCII control bytes. Payloads, including text payloads, may contain
arbitrary bytes; this metadata validation does not change the transmitted content.
The codec performs no filesystem
or clipboard writes; a user interface must require an explicit save destination
and must render received text as data. Callsign and grid fields are descriptive
metadata, not authenticated personal identities in unkeyed mode.
The native GUI copies only verified text with strict UTF-8 validation. Its file
list contains verified file and screenshot kinds only; even an ASCII file is
saved explicitly rather than treated as ticker clipboard text.

# Protocol status

The former single-carrier Robust implementation is retired. The native profiles
below explicitly define the maintained wire format; historical codec or marker
conventions do not make an older receiver a supported fallback. Protocol changes
evolve the canonical source and use separately versioned binaries, not parallel
in-tree receivers. In particular, the native long-message keyed tag has its own
versioned domain and is not the former long-message keyed format.

The current [Robust stack](robust-current.md) is native C++ and linked into the
GUI. It uses the canonical `IndependentApskV2` waveform, shaping, continuous
sampled channel, Helper generator and complete slot receiver. Peers explicitly
share a public calendar, initial keyed ordinal, waveform profile, FEC mode and
compression setting.
Unknown waveform-address acquisition remains outside the current scope.

`CompleteStreamReceiver` processes public slot plans, complete proposals and
persistent fine tracking. Two fully scored quiet slots following DATA establish
physical completion. Quiet evidence uses complete orthogonal likelihood blocks
wholly inside the public aperture: DATA support intersected with the search
window and bounded by neighboring latest/earliest DATA support. All received
samples remain in fine likelihoods. Interior missing slots remain erasures if
DATA resumes; malformed or over-limit episodes require quiet rearming.

Received lengths, local quotas, transport EOF and cancellation never select DSP
geometry, allocations or successful physical completion. Codec state resets
without resetting the global waveform ordinal. A bounded functional or parity
result does not establish all-case sensitivity, real-time capacity or
physical-channel qualification. See [current validation](validation.md#native-codec-and-measurement-migration),
[manual capacity](robust-manual-capacity.md) and
[relative regression](triad-performance.md).

## Configured native short-message profile

Text of one through 16 bytes retains the native short codec and produces at most
208 short-code bits. FEC Off transmits those exact bits. Raw-bit transmission
also preserves exact bits and bypasses text FEC. RS20 and RS60 are new,
explicitly configured wire profiles; they do not autodetect a received header.
After physical completion, they decode this bounded codeword:

`u8 bit count | MSB-first packed short bits / zero padding | CRC32-BE | RS parity`

The bit count is 1 through 208. The pre-parity body has `K = 6..31` bytes;
RS20 adds `ceil(K/5)` parity bytes and RS60 adds `ceil(3K/5)`. Their maximum
transmitted lengths are 304 and 400 bits. The CRC covers the domain bytes
`D P R F 1 FecMode` followed by the count and packed bits. It detects errors;
it is not sender authentication. Padding, length, selected mode and CRC must
validate after Reed–Solomon correction. Erased bit slots identify erased bytes.

These codec lengths are fixed local limits checked only after the receiver
establishes physical end. They do not choose receive sample extents or silence
thresholds. Ambiguous leading prefixes cannot establish the beginning of a
message and are not repaired by guessing a shortened FEC frame. Received text
still passes the shared presentation restrictions.

## Native long-message intervals

Text longer than 16 bytes uses fixed intervals of **1,216 physical bit slots**:
a 192-bit marker followed by one 128-byte codeword. The marker repeats the first
96 bits of SHA-256 over ASCII `DataPump/byte-boundary/v1`. Initial recognition
requires at least 144 known marker bits and at most eight mismatches at the
configured start. It does not slide across samples, insert/delete bits or infer
an unknown waveform address. After recognition, every marker/codeword keeps the
same public cadence even when later markers are damaged; their evidence is
reported separately.

FEC and key selection are local peer settings, not received length headers:

| Long profile | Codeword | Public source bytes | Keyed source bytes | Keyed tag bytes |
| --- | --- | ---: | ---: | ---: |
| Off | 128 data bytes | 128 | 96 | 32 |
| RS20 | RS(128,106), 22 parity bytes | 106 | 74 | 32 |
| RS60 | RS(128,80), 48 parity bytes | 80 | 48 | 32 |

With compression enabled, raw LZMA2 source bytes fill successive source areas;
the final area has zero padding. Decompression and padding checks occur after
physical completion under the local decoded-byte quota. Without compression,
each source byte uses a nine-bit cell: one occupied bit followed by eight data
bits. Absent cells and unused bits must be zero. The fixed public interval size
and local quotas determine storage; received content never enlarges DSP windows
or allocations.

A keyed interval appends a 32-byte dual-lane `confidentiality_mac` tag before RS
parity. The authenticated context is ASCII `DP-TRIAD-INTERVAL`, byte version 1,
the selected FEC byte and compression byte, then big-endian 64-bit epoch and
ordinal of the interval's **first coded bit slot**, followed by its source area.
The actual native public calendar supplies that address. A private key binds
that word to its configured context; public AMATEUR material supplies no sender
authentication. The tag does not authenticate a whole-message endpoint. Short
profiles remain unchanged and do not acquire this interval tag.

Two fully scored quiet slots still establish physical completion. The global
waveform ordinal continues through all markers, coded bits and quiet slots.
Incremental reception checks complete codewords and retains only a bounded
4,096-bit diagnostic prefix; a partial prefix is not offered as exact complete
raw data. Transmit preparation keeps bounded source bytes and generates one
codeword at a time. Lazy long-message emission is not limited by the materialized
bit-array API's 65,536-bit convenience bound.

The GUI accepts up to 1 MiB of decoded text bytes; its text field can represent
up to 4 MiB of escaped editor text. The native profile defaults to a 4 MiB local
content quota, and the Session clamps to that value. Explicit native callers
may configure a larger bounded quota up to the existing 256 MiB local ceiling,
subject to full memory admission. Source-storage bounds derive from
`2*content_limit + 4096`, rounded down to complete source areas. These are local
resource limits, not on-air lengths or fixed message-duration limits.

## Completion and authentication limits

The short and long prefixes are not disjoint at every early stopping point.
For example, the six observed bits `111010` form the valid Off short message
`y` as well as the beginning of the long marker. Before enough marker evidence
arrives, the receiver cannot determine which transmission was intended.

Losing all remaining complete uncompressed intervals can leave a valid shorter
stream. This applies to any valid prefix ending at a complete codeword, including
underfilled source areas that can be followed by another interval. Per-word tags
authenticate the retained words; they do not prove that no later words were
transmitted. Physical absence and source validation remain the completion rules.
Do not describe the format as whole-message endpoint authentication, infer an
unobserved suffix from received length, or silently add a length header to
resolve this ambiguity. See [security boundaries](security.md).

Fast has its own maintained wire formats and codecs; see the [Fast format](fast-mode.md)
and [capacity codec](fast-capacity-codec.md). Legacy has its own modem behavior
in the [Legacy reference](legacy-modem.md). Shared cryptographic and
received-content boundaries remain in [crypto.md](crypto.md) and
[security.md](security.md).

## Retained shared components

The application continues to maintain shared crypto, key-selection, source
sanitization, and codec components required by the active services. Their
contracts are defined by their current headers and focused tests, not by the
retired Robust framing. Keyfiles may contain named entries; a receive
operation selects one entry, by name or the documented default.

A historical protocol or fixture in Git history is evidence about that exact
revision only. It is not a supported fallback, current test requirement, or
qualification of the canonical path.

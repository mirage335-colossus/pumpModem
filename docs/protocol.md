# Protocol status

The former single-carrier fixed-interval Robust protocol is retired and is not a
current interoperability or compatibility contract. Its short dictionary,
marker/interval, recovery, and GUI-progress specifications no longer define the
maintained Robust implementation. Protocol breaks are handled by evolving the
canonical source and publishing a separately versioned binary; the repository
does not retain parallel Robust receivers for older wire formats.

The current [Robust stack](robust-current.md) is native C++ and linked into the
GUI. It uses the canonical `IndependentApskV2` waveform, shaping, continuous
sampled channel, Helper generator and complete slot receiver. Peers explicitly
share a public calendar, initial keyed ordinal, waveform profile and FEC mode.
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
physical-channel qualification. See [current validation](validation.md#native-gui-integration),
[manual capacity](robust-manual-capacity.md) and
[relative regression](triad-performance.md).

## Configured native short-message profile

The native text codec accepts at most 16 decoded bytes and produces at most
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

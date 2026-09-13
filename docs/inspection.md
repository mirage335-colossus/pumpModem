# Modem and transmission inspection

The native desktop keeps shared key, metadata and modem controls visible above
and below three tabs. **Console** contains composition, the signal browser,
received files and live plots. Switching tabs leaves the receiver and current
three-second simulation replay running.

**Modem flow** shows the currently selected transmit and receive paths as ordered
stage cards. Active processing, disabled options and unavailable algorithms have
different labels. It includes the ideal phase/amplitude alphabets used for
training and data, the chosen pattern or tone mode, symbol integration, packet
coding, encryption, and the appropriate audio or simulation path. These are
configured ideal points; measured received symbols appear on Console.

Its static **pattern/scrambler constellation** shows each complete symbol as a
row of complex phase/amplitude chips. All configured symbols and every position
in the compact sign period are inspectable, with pages for long patterns.
Distances account for the full template and exact quantized chip durations,
even when only one page is visible. The view compares the permitted templates
with an unused sign-vector example and a one-chip cyclic timing shift, including
correlation and residual after the best complex amplitude/phase fit. It reports
modeled chip-to-symbol integration gain separately from phase/amplitude geometry.
Keyed modes use public illustrative signs and never expose the epoch-derived
private sequence. See [geometry and interpretation](pattern-constellation.md).

The view describes the implemented gain estimate accurately: framed acquisition
searches timing, gain and initial differential-phase candidates using pattern
constellation evidence and compact headers. Short frames remain provisional
until complete integrity verification, while acquisition keeps searching.
The receiver despreads and integrates the complex chip samples before its APSK
decisions; no hard chip decisions or independently clean chip constellation are
required. The chosen gain is held for body decoding. Timing hypotheses remain
finite, with no continuous clock/frequency tracker, audio AGC or convolutional
encoder/Viterbi decoder. Both simulation and real audio use this blind PCM
receiver. Simulation supplies independent startup timing and phase, sample-clock
error and noise; it supplies no matched chip observations. Raw binary has no
acquisition framing, so its sampled waveform is displayed without a fabricated
known-length receive result. Hardware audio additionally passes through the
hardware/internal-clock conversion.

**Transmission layout** shows the physical on-air sequence separately from the
logical packet fields before interleaving. Blocks are schematic, with byte,
symbol and duration counts giving their scale. The payload is a placeholder;
the diagrams never display input contents, metadata values, key bytes, encrypted
training, or private spreading sequences.

For packets, the fixed five-second training consists of 64 four-bit APSK
segments. The following header contains packed flags, a canonical ULEB128
body length and CRC16: four to eight systematic bytes, plus parity according
to the effective data coding policy. Below 16 original bytes there is no RS
in the header or body; an ordinary one-word message has a four-byte bootstrap.
The Console displays this automatic FEC override while retaining the choice
for longer messages. There is no magic, packet version, dictionary identifier,
or compression-selector field.

The logical body starts with 23 fixed metadata bytes: a 16-byte ID, four-byte
ID/repeat CRC32, and three one-byte string lengths. A canonical ULEB128 original
payload length takes another one to five bytes, followed by the metadata
strings, encoded or original payload, and 32-byte digest or keyed MAC. Encoded
payload length is inferred from the body length and metadata. The integrity
tag covers the canonical variable header and uncoded metadata/payload, excluding
the tag itself and parity. The view shows body Reed–Solomon block geometry and
column-wise interleaving, including shortened final blocks. Header and body
share a continuous bitstream; only the final packet symbol can need pad bits.
The view apportions airtime by bits across the shared header/body symbol.
No zero-byte tail is transmitted. No diagram represents interleaved parity as a
single physical footer.

Compression is enabled only when the candidate is smaller. Original payloads
below 256 bytes use one fixed prefix code over bytes: the common space/e/t/a/o
bytes use three bits, with no phrase dictionary or codebook header. Longer
payloads use raw LZMA2 at preset 9e. Its history window is inferred from the
original length, from 4 KiB up to 64 MiB, rather than identified on air. The
inspection reports the selected algorithm, actual encoded byte count and
applicable encoder/decoder scratch separately from retained content. Incoming
packets select these paths from their own compression flag and original length;
outgoing settings do not disable receiver capabilities.

Selecting Binary instead shows the exact bit count, full symbols and any final
partial alphabet. This path has no preamble, packet header, compression,
Reed–Solomon parity or integrity tag. Selected stream encryption adds no bits.

Inspection shares the asynchronous airtime calculation and current edit
revision. Its packet layout comes from the real encoder, including whether
the selected short or long compression actually saves bytes. Editing a message or settings
clears the previous diagram until the new result is ready; invalid inputs show
their error. Inspection does not synthesize a waveform, so a symbol lasting
hours or a high internal sample rate does not allocate an hours-long sample
array. Packet identifiers generated for a future transmission are represented
by their field size rather than by a speculative random value.

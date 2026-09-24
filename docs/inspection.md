# Modem and transmission inspection

The native desktop keeps shared key, metadata and modem controls visible above
and below three tabs. **Console** contains composition, the signal browser,
received files and live plots. Switching tabs leaves the receiver and current
three-second simulation replay running.

**Modem flow** shows the selected transmit and receive paths as ordered stage
cards. Automatic profiles carry one meaningful bit in each of two distinguishable
patterns. Their cards show content-bit encoding, optional private Data masking,
pattern selection, hardware settling, audio or sampled simulation, and reception
by pattern evidence. Short raw bits and text have no modem training or packet
stages. The separate settling waveform helps external hardware prepare for
payload reception and is never an acquisition condition. Larger text,
files and screenshots retain their packet codec after pattern acquisition.
Explicit manual APSK configurations retain separate training/data alphabets and
their legacy framed receiver. Active, disabled and unavailable stages have
different labels. Configured symbols here are distinct from Console measurements.

The static **pattern/scrambler constellation** shows the binary zero and one
codeword rows at chip centers. Their internal transitions distinguish them;
they are not merely opposite absolute phases of one codeword. Long symbols show
a paged prefix of at most 16,384 chips and state its coverage. Pattern distances
use actual sample-duration weights over that illustrated prefix, rather than
claiming the distance of an unseen multi-hour symbol. Tone rows use complex
chip-center samples and approximate continuous-tone distances. Timing-shift
comparisons fit common complex amplitude and phase. Keyed rows use public
illustration seeds and never expose the actual private epoch stream. The manual
APSK view instead shows its complete repeated-template geometry. See
[geometry and interpretation](pattern-constellation.md).

Automatic reception fits soft pattern evidence against noise while allowing
unknown common phase and amplitude. That evidence controls signal start, end,
timing and keystream hypotheses. APSK cloud residuals, preamble detection and
packet integrity do not establish its lock. Strong individual patterns can
qualify; compatible weak candidates can accumulate evidence, and additional
weak bits must justify their own extension of a confirmed prefix. Raw reception
therefore returns an exact detected bit string without a transmitter-supplied
boundary or length. Completed dictionary text and raw bits can be copied without
a checksum; the displayed pattern score is model evidence, not measured SNR or
authentication. Manual APSK retains its protected-header/integrity lock gates.

Receive searches use the selected bandwidth and pattern mode, plus the separate
comma-separated target C/N0 list. Invalid list text resets it to `40`. Carrier,
clock and key coverage is finite. The long-symbol fallback requires an explicit
clock-start window and budgets every frequency/rate/timing combination; memory
or CPU can still make that coverage impractical. See
[long-symbol bounds](pattern-constellation.md#streaming-long-symbol-correlation).
There is no unrestricted oscillator tracker, audio AGC or convolutional/Viterbi
decoder. Simulation and hardware use actual PCM; simulation supplies independent
startup phase/timing, clock error and noise, without matched chip observations.
Hardware audio additionally converts between hardware and internal sample clocks.

**Transmission layout** shows the physical on-air sequence and its meaningful
bit count, symbol count and duration. Blocks are schematic rather than scaled
to airtime. Automatic pattern transmission uses exactly one payload symbol per
bit, with zero modem training and symbol padding. A separate hardware-settling
block rounds five seconds to the nearest whole payload-symbol duration, with
ties upward; it is absent for symbols longer than ten seconds. Its duration
contributes to total airtime without adding meaningful bits. The diagrams show
counts and placeholders,
never input contents, metadata values, key bytes or private spreading sequences.

For raw input, the layout reports the exact supplied bits, including leading
zeros and partial bytes, with compression, header, checksum and FEC all off.
For text below 16 original UTF-8 bytes, it reports the exact fixed-dictionary
bits with the same absence of framing, integrity and padding. For example,
`e` occupies its three-bit code and therefore exactly three payload pattern
symbols, in addition to any settling prefix.
This short-text dictionary is used regardless of the packet compression/FEC
settings. An empty text draft adds no transmission.

Text of at least 16 source bytes and all files/screenshots retain the packet
codec. Their automatic layout reports the encoded packet size, effective
compression, FEC and Reed–Solomon codeword structure beneath the pattern-bit
sequence. The packet header contains flags, canonical ULEB128 body length and
CRC16: four to eight systematic bytes plus optional parity. Packet payloads
below 16 original bytes have no RS anywhere; this includes small attachments.
They still have packet metadata and integrity, unlike short text. No magic,
packet-version or dictionary-identifier field is transmitted.

The manual APSK packet view additionally separates its logical field blocks
from the physical sequence. Five-second training occupies 64 four-bit segments
before the packet. The body contains 23 fixed metadata bytes, a ULEB128 original
length, metadata strings, payload and a 32-byte digest or keyed MAC. Packet
integrity covers the canonical header and uncoded metadata/payload. Body RS
codewords are interleaved by columns, including a shortened final block; parity
is not a single physical footer. Header and body share a continuous bitstream.
Only manual APSK may have unused positions in the final packet symbol; binary
pattern modulation has none. Neither sends a zero-byte tail. The precise packet
format is documented in [protocol](protocol.md).

Within the packet codec, compression is used only when the candidate is smaller.
Payloads below 256 bytes use the fixed byte-prefix code; longer payloads use raw
LZMA2 preset 9e, with history inferred from original length. Packet flags and
original length select decompression; no dictionary/codebook is transmitted.
The layout reports actual encoded size and effective compression/FEC. This
byte-packed, optional packet compression is distinct from the mandatory exact
dictionary bits of ordinary short pattern text.

For a draft no longer than 16 bytes, editing the binary field selects an exact
raw draft of up to 128 bits. A partial final byte is valid and adds no padding;
complete bytes also refresh the separate text/byte view. Editing text returns
to ordinary text encoding. For longer messages, the binary field continues to
replace the first 16 bytes while preserving the suffix, and that byte-editing
mode requires complete bytes. The layout follows the active draft mode rather
than assuming every binary edit is a packet.

Inspection shares the asynchronous airtime calculation and edit revision. Counts
come from the real bit/packet encoder and bounded transmitter estimate. Editing
content or settings clears the old diagram until the new result is ready;
invalid input shows its error. Inspection does not synthesize a waveform or
allocate a multi-hour chip sequence. Successful TX estimation does not assert
that every corresponding RX clock/key bank fits memory or runs in real time.
Packet identifiers are represented by field size rather than speculative values.

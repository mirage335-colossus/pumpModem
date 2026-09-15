# Modem and transmission inspection

The native desktop keeps shared key, metadata and modem controls visible above
and below three tabs. **Console** contains composition, the signal browser,
received files and live plots. Switching tabs leaves the receiver and current
three-second simulation replay running.

**Modem flow** shows the selected transmit and receive paths as ordered stage
cards. Automatic profiles carry one meaningful bit in each of two distinguishable
patterns. Their cards show content-bit encoding, optional private Data masking,
pattern selection, hardware settling, audio or sampled simulation, and reception
by pattern evidence. Every ordinary text, file or screenshot source uses fixed
128-byte coded intervals, with one alignment marker before each interval.
Explicit raw bits bypass the source and interval codecs. The separate settling
waveform helps external hardware prepare and is never an acquisition condition.
Active, disabled and unavailable stages have
different labels. Configured symbols here are distinct from Console measurements.

The static **pattern/scrambler constellation** shows the binary zero and one
codeword input-chip rows before pulse shaping. Their internal transitions distinguish them;
they are not merely opposite absolute phases of one codeword. Long symbols show
a paged prefix of at most 16,384 chips and state its coverage. Pattern distances
use input-chip duration weights over that illustrated prefix. They exclude
filter overlap, burst tails and crest limiting, so they are not distances
between actual shaped waveforms or measurements of acquisition confidence.
The preview also does not claim the distance of an unseen multi-hour symbol. Tone rows use complex
chip-center samples and approximate continuous-tone distances. Timing-shift
comparisons fit common complex amplitude and phase. Keyed rows use public
illustration seeds and never expose the actual private epoch stream. There is no separate APSK view. See
[geometry and interpretation](pattern-constellation.md).

Constellation capture covers at least one nominal 60 Hz frame (16.7 ms) of
signal time, independent of GUI polling and without VSYNC or display-backend
timing. Its sample and point capacities grow with the selected signal rate;
the waveform and spectrum continue to use their latest 2,048 PCM samples.

During audio transmission, **Transmitted constellation** retains the actual
logical payload chip I/Q values before pulse shaping, with a minimum history capacity of 2,048 points and
enough room for every chip overlapping a nominal frame. Polling faster than the chip rate does
not erase earlier points or substitute measured samples between chips. Settling
audio uses measured outgoing I/Q until the first payload chip position is reached.
Waveform and spectrum views continue to show actual shaped outgoing PCM.
Simulation and reception continue to show measured receiver input I/Q.
Public pattern rows repeat each symbol, so short public templates can still
produce a sparse plot; private chips advance through the keystream. One bit per
complete pattern does not limit its chips to two amplitude/phase values.

Automatic reception fits soft pattern evidence against noise while allowing
unknown common phase and amplitude. That evidence controls signal start, end,
timing and keystream hypotheses. APSK cloud residuals, preamble detection and
interval integrity do not establish its lock. Strong individual patterns can
qualify; compatible weak candidates can accumulate evidence, and additional
weak bits must justify their own extension of a confirmed prefix. Raw reception
therefore returns an exact detected bit string without a transmitter-supplied
boundary or length. Completed public sources and raw bits can be copied without
a message checksum; the displayed pattern score is model evidence, not measured SNR or
authentication. No codec result controls pattern acquisition or stream end. Full symbols are
scored before absence is determined. Consecutive missing symbols end reception
when their durations cover six seconds; one missing symbol suffices when its
duration is at least six seconds. Shorter interference retains timed unknown
slots for Reed–Solomon erasure correction. EOF and codec success do not complete
a stream.

Receive searches use the selected rate, carrier and pattern mode, plus the separate
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
block rounds two seconds to the nearest whole payload-symbol duration, with
ties upward; it is absent for symbols longer than four seconds. Its duration
contributes to total airtime without adding meaningful bits. The diagrams show
counts and placeholders,
never input contents, metadata values, key bytes or private spreading sequences.

For raw input, the layout reports the exact supplied bits, including leading
zeros and partial bytes, with compression, integrity and FEC all off. An empty
draft adds no transmission.

All ordinary sources use the same interval size. RS20 reserves 22 parity bytes;
RS60 reserves 48. Encryption additionally reserves 32 bytes for HMAC-SHA256
inside the protected data area. Public mode has no message digest. One marker
precedes each interval, including the first; there is no terminal marker or
transmitted length. The layout reports actual source, coded and marker bit
counts. A one-byte source therefore still occupies one interval and one marker.
See [protocol](protocol.md) for exact geometry and correction limits.

Compression uses raw LZMA2 with a fixed dictionary, even when encoding makes
the source larger. Decompression runs only after physical symbol absence has
completed the stream. The local uncompressed setting uses validity-plus-byte
cells to preserve exact binary contents, including trailing zero bytes.

All payload symbols carry one meaningful bit. Final unused source space is
filled before integrity, parity and private Data masking. Every transmitted
marker and coded bit consumes its original timing and cipher position. Tone
selection clears and disables encryption, and its inspection reports that it
provides no LPI protection. Private pattern examples use public illustration
seeds; actual private streams never appear in configuration illustrations.

For a draft no longer than 16 bytes, editing the binary field selects an exact
raw draft of up to 128 bits. A partial final byte is valid and adds no padding;
complete bytes also refresh the separate text/byte view. Editing text returns
to ordinary text encoding. For longer messages, the binary field continues to
replace the first 16 bytes while preserving the suffix, and that byte-editing
mode requires complete bytes. The layout follows the active draft mode rather
than assuming every binary edit requests source encoding.

Inspection shares the asynchronous airtime calculation and edit revision. Counts
come from the real bit/stream encoder and bounded transmitter estimate. Editing
content or settings clears the old diagram until the new result is ready;
invalid input shows its error. Inspection does not synthesize a waveform or
allocate a multi-hour chip sequence. Successful TX estimation does not assert
that every corresponding RX clock/key bank fits memory or runs in real time.
Reception identifiers are local UI state and occupy no transmitted bytes.

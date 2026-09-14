# Audio modem reference

The default CLI and GUI transport carries one bit per independent pattern
codeword. The pattern receiver discovers signal start, bit sequence and end
from pattern evidence alone. It compares timing, carrier and keystream positions
without a preamble, packet header, checksum or APSK residual lock condition.
Short text (under 16 original bytes) uses the fixed bit-prefix dictionary with
no byte padding, framing or FEC. Explicit raw drafts send exactly their 0/1 bits.
Larger messages and attachments retain the packet codec after pattern recovery.

The manual `Config::pattern_symbols = false` path retains differential APSK,
training and protected-bootstrap packet acquisition. Its diagnostic algorithms
are described separately below; they do not govern default pattern acquisition.
Existing keyfiles need no migration, but the new default waveform is different.

This is a reference implementation. Modeled pattern log evidence is not a
calibrated probability, measured SNR, near-capacity throughput result, radio
sensitivity measurement or proof of low probability of intercept. The finite
frequency, clock and memory limits below remain material.

## Timing, training, and spreading

The default internal configuration is mono 6,000 Hz PCM, a 1500 Hz carrier and
1,200 Hz nominal bandwidth. For nominal bandwidth `B` from 1 Hz through 30 MHz,
the planner selects `Fs = max(6000, ceil(4B))` and carrier `max(1500, 0.75B)`.
Narrow audio remains around a usable carrier rather than falling below 300 Hz.
The 6 kHz floor is needed for this real-PCM carrier representation; it does not
raise the nominal chip or symbol rate. The 30 MHz plan still uses 120 million
internal samples/second. Manual CLI carrier/sample-rate overrides remain available.
The range is a DSP configuration range; an SDR device backend is not implemented.
The modem's nominal chip rate is bandwidth / 2.
`symbol_seconds` is the bandwidth-derived duration (or explicit integration),
independent of the hardware clock. `symbol_sample_count` rounds that duration
up once to an internal PCM sample; exact airtime estimates include this rounding.
The nominal gross bit rate is selected bits per symbol divided by nominal symbol
duration, before packet and training overhead. Audio conversion does not alter
these modem settings or rates.

### Automatic-pattern hardware settling

A nonempty automatic-pattern transmission starts with a hardware-settling
waveform lasting approximately five seconds, rounded to the nearest whole
sample-quantized payload-symbol duration, with half-symbol ties rounded up.
For symbol duration `T`, the prefix contains `floor(5 / T + 0.5)` intervals of
length `T`; it is absent when `T` exceeds ten seconds. The first payload symbol
then follows at its normal duration, starting at payload stream position zero.
An empty payload emits no prefix.

The settling waveform uses independent circular Gaussian-derived I/Q noise,
refreshed approximately every half chip, at the payload's mean transmit power.
Its radius is limited and normalized to keep peaks inside PCM headroom.
Rectangular noise updates have a wider first-null spectrum than the full-chip
payload pulses; the bandwidth setting is nominal, not an enforced spectral
mask. It helps external gain control and muting settle before
fast payload symbols arrive, and is not made from the legal payload patterns.
It supplies no training, header or acquisition condition: pattern evidence
alone accepts the following symbols even when the prefix is lost or distorted.
Any selected key XOR-encrypts the preamble noise bytes with its existing
Data-purpose key and transmission epoch before I/Q mapping, even with payload
spreading disabled. Enabled Scrambler and DSSS layers then both apply using
the same keys and epoch as their payload streams. The high eight CTR counter
bytes contain the fixed ASCII pad `preamble`, separating prefix positions
without generating or deriving extra keys. Tone mode uses the same noise
prefix, refreshing phase and amplitude at roughly twice the nominal chip rate;
it reserves no payload constellation points. The transmission epoch is fixed
before the prefix, and clock-start hypotheses include its elapsed time.
Airtime estimates include the prefix without adding to meaningful payload bits.

### Explicit legacy APSK waveform and training

The following training, whitening and APSK details apply only when
`pattern_symbols` is false. Default pattern transmission uses only the separate
hardware-settling waveform described above, without modem training.

Bytes are sent most-significant bits first. Two-bit and three-bit profiles use
two amplitude rings and two/four differential phases; four through six-bit
profiles retain eight phases and use two/four/eight amplitude rings. Phase and
amplitude indexes use Gray adjacency. For `R` rings, the radius increment is
`sqrt(0.30625 / ((R+1)*(2*R+1)/6))`. Every equiprobable constellation therefore
has mean complex power 0.30625 and real carrier power 0.153125; peak amplitude
stays below one. The original four-bit map uses radii 0.35 and 0.7 and phase-step
indexes `[0, 1, 3, 2, 7, 6, 4, 5]`. Phase continues across the training/payload
boundary. PCM is the real component of the complex symbol times the carrier
oscillator and configured chip sign.

The variable bootstrap and remaining packet body form a continuous bitstream.
Only the final symbol can contain unused pad bits, excluded from decoded bytes.
There is no magic marker, internal symbol padding, end marker or transmitted
zero-byte tail. `payload_symbol_count` is the ceiling of packet bits divided by
bits per symbol. Repeatable airtime counts incremental symbols relative to a
numeric empty-payload baseline with the same metadata and effective coding.

The 32-byte known preamble always uses 64 independently timed 16-APSK training symbols. Its duration is five seconds,
quantized to the sample grid, independently of payload symbol duration. A payload
symbol longer than five seconds does not lengthen the preamble to one or more
whole payload symbols. The transmitter and receiver use the same training
schedule and transition to payload timing at its end. Training is outside the
packet codec and is not Reed–Solomon protected or independently authenticated.
The unencrypted training bytes are:

```text
53 19 a7 e2 86 d4 3b 0f 65 92 ce 48 71 ad f0 26
b8 4d 03 e7 9a 61 35 cf 28 d0 7e 94 ab 16 f3 59
```

The full preamble, framed packet and parity are encrypted together when a key is
selected. The separate legacy few-bit waveform has no training prefix; default
pattern-mode status transmissions use the hardware-settling rule above.

After private encryption, the audio transfer layer XORs the frame after its
32-byte training prefix with a public whitening stream. This removes the strong
amplitude/phase bias of structured headers without adding bytes or airtime.
The protocol seed is the ASCII string `DataPump/audio/whitening/v0.5`, padded
with zero bytes to 32 bytes, passed to `Crypto`. Use its `Scrambler` purpose,
epoch zero and frame-relative byte offset; offset zero begins immediately after
the training. The first 16 mask bytes are `fbd27bba1decfa41e9265d3cc2740276`.
Both keyed and plain audio use this layer. Receiving removes whitening before
private decryption, FEC and packet validation; XOR does not spread bit errors.
Protected-bootstrap trials cache the combined mask once per candidate.
`pack`/`unpack`, the raw-byte modem API, training and few-bit status are unchanged.

Whitening is public and reversible. It does not add payload entropy, improve
the modulation's intrinsic capacity, conceal repeated frames, or guarantee low
probability of intercept. A finite short message need not visit every point,
and the planner can select a smaller alphabet when its faster symbol rate wins.
Point occupancy is also different from a white spectrum: this modem still uses
rectangular pulses, whose sidelobes remain. Pulse shaping and its receiver
filter must be designed together; see [Analog Devices AN-922](https://www.analog.com/en/resources/app-notes/an-922.html).

### Pattern codewords and named choices

`PatternCode` supplies two distinguishable codewords for each one-bit symbol.
The public pattern resets at a symbol boundary. In keyed pattern mode, the
Scrambler stream uses absolute chip positions, so successive symbols use fresh
fragments rather than repeating a small chip block. The second codeword mixes
the first with a nonconstant balanced mask, preserving distinguishability under
unknown carrier phase. A separate DSSS purpose remains independent. Pattern
and data streams share the candidate epoch and searched stream position.

| Mode | Behavior |
| --- | --- |
| `auto-keystream` | Automatic binary pattern integration with fresh keyed chips; falls back to public `auto-pattern` without a key. |
| `auto-pattern` | Automatic integration of two rare codewords: public without a key, fresh keyed fragments with encryption. |
| `auto-tone` | Automatic integration of two continuous tones. |
| `pattern-3`, `pattern-4`, `pattern-6`, `pattern-8`, `pattern-12`, `pattern-16` | Preserve the named short pattern duration, mixing with fresh keyed fragments when encrypted; do not meet the automatic rarity floor. |
| `tone-1`, `tone-2`, `tone-3`, `tone-4`, `tone-8`, `tone-32`, `tone-128`, `tone-1024`, `tone-4096`, `tone-16384` | Preserve the named tone duration in chips. |

Tone codewords use opposite quarter-turn progression per chip, producing
frequencies at carrier ± chip_rate/4. Distinguishing tone identity requires
carrier uncertainty smaller than chip_rate/4. A long tone can accumulate energy
without becoming a rapidly changing pattern; tone acquisition has narrower
frequency and clock conditions. Automatic tests need not force tone modes.
Data masking alone does not give tone templates a key identity. Selecting
among receive keys by pattern evidence requires keyed patterns or independent
keyed DSSS; plain tone fits cannot resolve that ambiguity.

Automatic integration can exceed the largest named factor through
`Config::integration_seconds`. Duration is quantized once to a sample boundary,
and a final partial chip is permitted. Pattern generation uses bounded storage;
receiver timing coverage and numeric limits still bound usable integration.

Pulse shapes are rectangular. Their sidelobes extend outside nominal bandwidth;
there is no certified occupied-bandwidth mask. Carrier and nominal bandwidth
must fit inside the internal DSP passband. The 24 kHz GUI preset uses a 96 kHz
internal clock and a fitting carrier. That clock is not a sound-card requirement.
A carrier need not complete an integer number of cycles in a chip.

Audio I/O first tries the internal rate when it lies in the supported audio
range of 8..384 kHz, then other audio rates, preferring higher clocks to preserve
passband. Narrower or wider internal clocks stay separate from this hardware
negotiation. Explicit device
identity and default-card discovery are preserved. ALSA software resampling and
WinMM ACM conversion are disabled during this negotiation. A bounded Blackman-
windowed sinc converter connects the selected hardware clock to the internal
clock, preserving phase, duration and callback continuity. Downsampling ratios
greater than four are decomposed into bounded stages with small intermediate
buffers instead of allocating a duration-sized filter. Each stage uses 256
interpolated fractional phases. For unequal rates, its declared flat passband is
0.42 times the lower rate and its transition rolls off before Nyquist. Equal-rate
conversion is bit-exact and reports the Nyquist bound of 0.5 times the rate.
Finite streams produce `ceil(input_count * output_rate / input_rate)`
samples, with zero extension at their boundaries. Converter memory depends on
rate ratio, never transmission duration, and is included in live DSP accounting.

Interpolation cannot restore frequencies beyond hardware Nyquist or compensate
unknown analog filtering. The GUI shows the negotiated clock. Both the GUI and
direct CLI audio commands reject a selected band that exceeds the negotiated
converter's usable passband; WAV output remains independent of hardware.
Oscillator drift and
physical microphone/speaker frequency response still need device-level validation.

## Automatic signal planning

The CLI and GUI default to bandwidth 1,200 Hz and TX target C/N0 of 40 dB-Hz.
The selected pattern/tone mode is fixed during receive search; encryption
normally selects `auto-keystream`, with `auto-pattern` otherwise. The separate
**RX targets (dB-Hz)** field and CLI `--receive-targets` accept a comma-separated
list, default `40`. Entries are trimmed and deduplicated. An empty, malformed,
nonfinite, out-of-range (outside -200..200 dB-Hz), over-16-entry or over-512-byte
list resets entirely to `40`. The GUI permits partial editing, then normalizes
after 750 ms of inactivity. RX targets never alter the scalar TX target.

The receiver resolves only this list, using the selected bandwidth and mode;
identical sample-quantized waveform profiles are searched once. Manual CLI
`--spreading`, `--scramble`, `--dsss`, `--sample-rate` or `--carrier` selects
legacy explicit configuration and cannot be combined with automatic planning
or `--receive-targets`. Simulator `--snr` describes sample-power noise and does
not choose the transmitted profile.

`tuning::resolve` uses binary pattern symbols (`constellation_bits = 1`). It
starts with a modeled integrated-energy target of 18 dB and an automatic rarity
floor of 64 chips, then selects the smallest supported integration meeting
both. Automatic factors are 64 through 16,384, doubling at each step; longer
integration uses an explicit duration. Forced modes preserve the requested
length and report an unsupported target when the rarity floor or energy model
is unmet. APSK geometry does not choose default pattern length or bit rate.

```text
required_symbol_seconds = 10^((18 - C/N0_dBHz) / 10)
estimated_Es/N0_dB       = C/N0_dBHz + 10 log10(symbol_seconds)
bit_rate                = 1 / symbol_seconds
```

C/N0 uses noise power in a 1 Hz reference bandwidth. For bandwidth B,
`SNR_B = C/N0 - 10 log10(B)`. The 18 dB target and 64-chip floor are initial
engineering model choices, not an empirically calibrated false-alarm or missed-
detection specification. Search multiplicity, correlated bins, channel model
and adaptive timing selection must be included in future calibration. Long
integration alone does not establish tolerance of clock drift or interference.

## Incremental pattern receiver and live audio

`StreamingTransmitter` emits bounded PCM chunks, including the rounded
hardware-settling prefix when nonzero. It does not allocate a whole prefix
waveform or require the receiver to recognize it.
`StreamingReceiver` dispatches pattern transport to `PatternReceiver`; explicit
legacy packet configurations retain their earlier APSK path. The pattern
receiver mixes input into a short complex baseband ring, uses FFT correlation
for candidate timing windows and retains bounded `PatternEvidence` records.
Each record includes sample interval, frequency, stream-symbol position, bit,
best score and alternative score. It keeps a bounded set of candidate chains
and recovered bits while evicting old waveform history after a few symbol
lengths. Weak evidence may be discarded at the retention threshold.

Evidence uses normalized complex projection onto each candidate pattern,
with unknown complex amplitude and local energy normalization. Its score is
in natural-log units derived from a white Gaussian model. It is not APSK
point error, measured dB, a calibrated confidence percentage or authentication.
Timing, carrier and keystream candidates are compared only using this pattern
evidence. High individual evidence can admit a single symbol; weaker retained
symbols can contribute to a chain. A low-evidence gap ends the recovered burst.
No supplied bit count, packet length, known text or checksum admits a burst.
Packet decoding and optional integrity checks occur after the burst is chosen.

The default frequency bank contains five offsets at 0, ±1/(4T), ±1/(2T),
where T is symbol duration. The API permits an explicit bounded offset bank.
Keyed search tries a finite set of epochs and initial stream positions; it
transmits no epoch, symbol index or chip-block index. Timing resolution and
coverage depend on the allocated transform. This is not a whole-band search,
an arbitrary Doppler tracker or a guarantee of long-duration clock recovery.

Ordinary FFT acquisition requires workspace proportional to a few longest
symbol windows. When that allocation exceeds the budget and a system-clock
start hint is available, `PatternCorrelator` instead maintains bounded running
fits over the entire requested timing/frequency/rate window. Half-chip timing
coverage is checked before allocation; unaffordable coverage is explicitly
rejected. No full-symbol waveform or keystream array is required by this fallback.

Tests cover a four-hour symbol prefix with bounded storage, noise rejection,
and actual short-signal PCM recovery through the fallback. These checks establish
bounded state and exercised decoding, not a completed four-hour weak-signal
reception experiment. The default six-second epoch search supplies ±7 seconds
of start uncertainty; at 1.2 kHz this costs roughly 26 MB per epoch before
retained bits. CPU cost also grows with timing and epoch hypotheses and is not
established as real time for a large bank. Multi-hour operation requires a finite
clock window, adequate DSP budget and sufficient oscillator stability.

An idle public fallback is rearmed after two symbol durations, so it does not
remain confined to its original phase window forever. Active admitted bursts
retain their state; ordinary FFT discovery is not periodically reset. This
rearming does not provide complete continuous coverage of arbitrary start times
between windows. The configured budget remains an upper limit, not a target
to fill.

The GUI DSP workspace dropdown offers 25%, 50% (default), or 75% of available
RAM, resolved at startup or when selected. Received messages/files retain a
separate default 256 MiB quota. Candidate records, transform storage and active
bit chains consume DSP budget; message byte limits do not silently truncate
signal history. Search work still grows with sample rate, retained hypotheses
and integrations. Increasing memory alone does not prove acquisition.

### Explicit legacy receiver

Manual APSK configurations retain finite timing/gain candidates, training
measurement, protected-bootstrap checking, recorded symbol replay and final
packet validation. Their completed differential-constellation fit selects
among packet candidates. That path exists for compatibility and diagnostic
coverage; its amplitude/phase lock criteria are not used by pattern transport.
The manual `BinaryReceiver` remains an aligned diagnostic requiring known bit
count and origin. Neither supplies evidence for automatic pattern discovery.

`live::Session` owns continuous capture, preparation and decoding workers. Normal
GUI startup opens the OS-default input; an optional device override is available.
Actual playback pauses capture and resumes it afterward. Incoming audio arrives
in small chunks; queues, acquisition state and current plots are bounded. The
obsolete `receive_buffer_seconds` setting no longer determines an audio window.
Cancellation is checked between processing chunks and driver buffer operations.
A driver blocking inside an operating-system call remains outside that guarantee.

The waveform, windowed FFT and measured complex constellation feed live plots.
Plot input is a continuous bounded sample window with an absolute sample origin;
FFT windows and carrier phase do not restart at an audio callback boundary.
Varying callback sizes or GUI scheduling therefore do not redefine the signal
being measured. Rendering is paced to about 20 updates per second as CPU time
allows; this is not a guarantee against an overloaded audio device or processor.
Idle simulated noise follows the same 20 Hz cadence with bounded preview chunks.
Queuing a transmission interrupts that wait; active transmissions run at CPU
speed independently of their virtual airtime.
For explicit legacy APSK, the GUI consumes fresh symbol observations from the provisional or locked receiver. Their
phase is measured relative to the preceding received symbol, and their amplitude
is normalized by the receiver's estimated gain. This is the decoder's coordinate
system; points remain measured values and are never snapped to symbol decisions.
A bounded queue retains each point with its measured phase reference until the
next GUI snapshot. Changing a timing or gain hypothesis does not republish older
symbol times; overflow is counted rather than growing this queue indefinitely.
Unlocked input supplies current noise/phase I/Q diagnostics. The source label
distinguishes those input measurements from received or transmitted symbols.
Axes and amplitude rings are display aids, not a calibration certificate. Decoder
diagnostics and acquisition scores are evidence of processing, never packet
authenticity.

Pattern-only signal rows show **Pattern score** in model log-evidence units and
**No checksum / FEC**. Decoded dictionary text shows a single text entry,
including when its compressed bit length is not a multiple of eight. When no
text is decoded, whole raw bytes use an escaped byte view and a partial final
byte uses an exact raw-bit entry. Completed
entries can be copied; **Paste as message** loads text and escaped bytes exactly
for inspection in Binary. These results do not enter the packet-validated inbox.

For the explicit legacy receiver, the signal browser's **Preamble** percentage is recognized training duration
divided by the expected five-second training duration. Training always has 64
segments (12.8 per second), even when the detected payload timing implies less
than one payload symbol in five seconds. The receiver independently compares
carrier projections with the expected training sequence, accounting for common
gain and differential phase offset. It requires matching runs of at least eight
segments; phase tolerance is 15 degrees and relative amplitude tolerance is 25%.
This conservative evidence measure is distinct from correlation, sample-buffer
coverage, or packet authentication. Blind bootstrap acquisition never creates
preamble evidence from the regenerated prefix.

Fixed projection history and bounded timestamped records preserve the measurement
without storing an entire slow transmission. Missing or noisy training can give
zero recognized coverage. Ambiguous timing, evicted history or observations too
coarse to resolve training produce `--`. This diagnostic does not gate decoding.

**Data … pre-FEC** measures exact bit agreement between the received systematic
packet body and the corrected, fully verified body. It includes metadata, encoded
content and the integrity tag, excluding bootstrap, parity and padding. Comparing
the received and corrected bytes after deinterleaving counts actual bit changes,
not eight assumed errors for every repaired byte. Compression changes the encoded
denominator; XOR encryption and whitening preserve these bit differences.
Partial or failed packets cannot establish this accuracy and remain `pending`.
Simulation also withholds this measurement until the verified result is
presented at the three-second deadline; advance computation does not expose it
through a pending browser row.

The waveform initially shows four carrier cycles from the latest buffer.
A bounded 64-tap Blackman-windowed sinc reconstructs the line between measured
samples, which remain visible as dots. It does not synthesize a presumed carrier.
Up to 32 captured guard samples on each side avoid edge extrapolation when
available. The mouse wheel changes the timebase and double-click restores it.
Zoomed-out views draw each pixel's
minimum/maximum samples, preserving peaks without skipping input. The waterfall
uses peak pooling over every FFT bin, and one labeled 100 dB color range for all
retained rows. Large simulated noise can raise that shared range; a settings
change or click clears it. Different frequency axes never share a history.
Unlocked receive I/Q diagnostics fit both carrier bases, including their cross
term, so a window containing fractional carrier cycles does not introduce an
artificial amplitude or phase ellipse.

Actual playback similarly drains newly emitted payload symbols, excluding fixed
training. Pending points survive plot publication until a GUI snapshot consumes
them; each delivered batch replaces the previously displayed cloud. A slow
consumer receives a bounded batch with a count of overflowed points through
`Snapshot::constellation_dropped`, also shown as omitted points in the plot caption.
Source changes do not mix raw I/Q and symbol
coordinates. The modem's legacy history APIs still expose up to 2,048 raw recent
symbols for other callers; the GUI uses the drain APIs instead. Labels show the
selected diagnostic alphabet and the current observation count. Pattern transport
shows measured differential chip observations separately from pattern evidence;
those noisy phase/amplitude points do not control acquisition. No ideal or missing
points are inserted. Symbol plots retain a nominal unit scale rather than
stretching a lone inner ring to the outer edge.
Positive rates too small for decimal display use scientific notation instead of
rounding to `0.0 bit/s`.

Pending ticker observations can change as additional bytes and parity arrive.
They stay visibly provisional and cannot trigger clipboard copy or file save.
Only complete validated text can be copied; validated files and screenshots
appear in the separate RAM file list for explicit exclusive save. Ordered event
serials preserve pending-before-final order. Simulation schedules these events
alongside its plots: normal GUI polling shows pending reception before the
verified result. If polling stalls, due events are delivered in chronological
order on the next poll without extending the presentation to force a visible
pause between them.

## Sampled simulation

The continuous simulator sends the transmitter's actual carrier and spreading
waveform through a channel into the ordinary PCM receiver. The receiver starts
unlocked, processes idle noise before transmission, and continues through burst
boundaries without a transmitter-triggered reset. It receives no symbol
boundaries, spreading decisions, payload size or transmitter epoch. Its candidate
keys and epochs come from its own settings and clock and follow the same finite
search and retention policy as audio reception.
For one-shot `transfer::simulate`, `ChannelConfig::receiver_timestamp` optionally
sets an independent receiver search center; the CLI exposes it as
`--receiver-time SECONDS`, separately from transmitter `--time SECONDS`. Omission
uses the explicitly configured transfer timestamp. An encrypted epoch outside
the receiver's `--search-seconds` window is not supplied by the transmitter.

The channel uses a seeded arbitrary initial carrier phase, a fractional sample
startup offset, and a random idle interval before each burst. Its default
**100 ppm relative transmitter/receiver crystal error** and separate Wiener
phase-diffusion assumption of **0.5 degrees per square root second** model the
relative oscillators of free-running computers. The crystal parameter represents
the combined relative error. Positive error raises the received carrier and makes
symbols arrive sooner:

```text
clock_ratio       = 1 + clock_error_ppm * 1e-6
carrier_error_Hz  = explicit_frequency_offset + carrier_Hz * (clock_ratio - 1)
RMS_phase_change  = phase_noise_degrees_per_sqrt_second * sqrt(elapsed_seconds)
```

Receiver samples include AWGN and the altered carrier, sample timing and phase
trajectory. Sample-clock error can reduce the actual spreading correlation; no
matched-despreading statistic is handed to the receiver. The default receiver
must discover pattern symbols and signal boundaries from these samples; the
legacy packet path must additionally acquire its bootstrap. Finite carrier and
timing coverage means an impaired signal can still fail acquisition or
validation. Longer integration alone cannot repair oscillator coherence loss.
Use `--clock-error-ppm 0 --phase-noise 0` with CLI `simulate` or `listen` when
intentionally testing ideal oscillator stability; startup remains unsynchronized.

Samples are processed in bounded chunks at CPU speed without sleeping for the
advertised on-air duration or retaining the whole waveform. Retained waveform
history spans a few symbols; CPU work scales with the number of samples. High sample rates
and hour-long symbols can therefore take substantial time. Cancellation is
checked between and within chunks. The model does not establish fading/multipath
performance, nonlinear hardware behavior or interference rejection.

During computation, snapshots are captured at evenly spaced media positions from
the start to the end of the transmitted signal, including any hardware-settling
prefix. Legacy packet snapshots include their fixed training and protected header. The normal timeline contains 60 frames. Each
stores a compact 256-sample waveform, 257 peak-pooled spectrum bins, the fresh
measured constellation for its interval and up to 4096 bytes of browser preview
text. Its source and lock state are captured at that position. Reception continues
through ordinary channel noise after the source stops; a later lock cannot be
applied to an earlier frame.
Waveform previews retain bounded recent receiver samples from the actual
channel, including its noise, carrier phase and spreading.

After sampled computation completes, the GUI plays the entire timeline over
three wall-clock seconds. Preparation does not wait for simulated airtime.
Every new frame updates waveform and constellation and adds one waterfall row.
Decoded text and metadata are withheld until their scheduled preview positions;
the browser shows provisional reception when the receiver produces it. A short
pattern burst may complete without a separate provisional event. Completed raw
bits and short dictionary text are released at the three-second deadline;
packet-validated text, file entries and pre-FEC accuracy use the same delivery
point. Failed decoding supplies no recovered result. Prepared content cannot
be copied or saved before its presentation completes.

If GUI polling skips frames, their fresh points are merged into the next
delivered batch within the same source coordinates, subject to a
bounded capacity and an explicit overflow count. Duplicate polls do not append
duplicate rows. Due browser events are delivered in chronological order.
Replay workspace uses at most one eighth of the configured DSP budget, capped
at roughly 1.4 MiB. This includes roughly 38 KiB for the prepared result's
diagnostics and caption, plus the frames' plots, points and preview text. Smaller
budgets reduce frame and point capacities. This bounded workspace is independent
of simulated airtime; the prepared packet's content is charged to the separate
receive-content quota.

Calls queued while simulation is still computing are presented consecutively,
with a separate three-second replay for each. An explicit new transmission
during active presentation or selecting Stop replay interrupts it and discards
its undelivered browser events and received result. Previously completed
receptions remain available.
At the three-second deadline, any points still undelivered because GUI polling
stalled are counted as omitted. Due browser events and the final result are
released in order; replay does not extend or place old points over live input.
On completion or cancellation, waveform, waterfall and constellation all resume
live input. Background simulated reception continues while replay is presented;
the previous packet does not remain as a persistent constellation cloud.
The packet is still decoded and validated; transmitted application bytes are not
inserted directly into the receive cache.

Named presets interpret transmit power in dBm plus negative attenuation in dB:
`no`, `3dBm -6dB`, `3dBm -60dB`, `3dBm -90dB`, `3dBm -120dB`, `3dBm -170dB`,
`3dBm -200dB`, `3dBm -230dB`, `50dBm -200dB`, `50dBm -270dB`, and
`70dBm -250dB`. The reference assumptions are -174 dBm/Hz thermal density and a
10 dB noise figure:

```text
received_dBm   = transmit_dBm + attenuation_dB
noise_dBm      = -174 + 10 + 10 log10(B)
channel_SNR_dB = received_dBm - noise_dBm
C/N0_dBHz      = received_dBm - (-174 + 10)
sample_SNR_dB  = C/N0_dBHz - 10 log10(sample_rate / 2)
```

The last value sets AWGN power over the real sampled Nyquist bandwidth. For
`3dBm -170dB`, C/N0 is -3 dB-Hz, regardless of the selected channel bandwidth.
These are model inputs, not measurements of a soundcard or radio. Extreme
presets may fail decoding.

## Raw binary transmission

The GUI's Binary editor is an alternative to its message/file source. It accepts
`0` and `1`, preserves leading zeros and ignores whitespace. Its estimate and
transmit paths use `transfer::estimate_binary` and `transfer::binary_transmitter`.
`Session::transmit_bits` queues the same bounded streaming transmitter for audio
or the sampled simulation channel. Binary mode does not send callsign/grid
metadata, an attachment, repeat requests, compression, modem training, a packet
header, integrity tag or error correction. The separate hardware-settling prefix
uses the same rounded duration as other pattern transmissions.

In default pattern mode, each bit occupies exactly one complete pattern symbol,
so a three-bit draft occupies three payload symbols with no byte padding. Any
hardware-settling prefix adds airtime but no payload symbols or bits. Selected keys
mask the actual data bits and seed independent pattern/DSSS streams. When a
prefix is present, those same keys also mask its noise through the separate
`preamble` CTR range; they add no transmitted data bits.

Raw signals have no packet bootstrap or authentication. Simulation and audio
feed their waveform to the same blind pattern receiver, without transmitting
or passing the bit count, start sample or carrier phase as decoder metadata.
Pattern evidence discovers the bits and burst end. Completed raw bits can be
copied even when the expected bit count is unknown. The low-level manual
`modem::BinaryReceiver` API remains an explicitly aligned diagnostic.

## Batch PCM, WAV and few-bit status

Batch `transfer::transmit`/`receive`, `modem::simulate` and WAV interfaces
operate on complete PCM vectors and retain their batch allocation checks.
The transfer APIs honor pattern or explicit legacy configuration; streaming support does not make an
arbitrarily large WAV fit in memory. The sample-domain channel adds leading delay,
AWGN, fixed frequency shift, relative sample-clock error and Wiener phase noise.
It interpolates the analytic PCM waveform at the altered clock and applies phase
noise sample by sample. The continuous sampled channel exercises the same
waveform timing and chip correlation without retaining a complete PCM vector.

The generic raw-byte `modem::demodulate` API uses known-training timing and
constant carrier-offset acquisition with the shared adaptive APSK quantizer. Default
`transfer::receive` and live reception use pattern-only streaming acquisition.
Explicit legacy configurations use protected-bootstrap streaming acquisition. Thus obscured-training packet recovery does not imply that
the generic raw-byte API can acquire arbitrary bytes without known training.
The legacy batch feasibility check includes its complex acquisition workspace;
streaming DSP feasibility is reported separately.

WAV is little-endian RIFF PCM16 mono. Readers validate chunk/container lengths,
sample and byte rates, format and allocation limits. Writes round and clip finite
samples to PCM16. Clipping a loud noise waveform changes its SNR.

Automatic `status-tx` sends one pattern symbol per supplied 0/1 bit through the
same raw-bit transmitter. `status-rx` discovers the complete bit string through
pattern evidence, then compares `--bits` afterward. JSON includes the exact raw
bits/count, model score and `packet_validated: false`; a wrong comparison value
does not alter acquisition. The explicit manual status API retains aligned
DBPSK known-status correlation. Neither result authenticates the sender.

## Resource boundaries and verification

`Options::content_limit` defaults to 256 MiB and limits application content.
The GUI receive cache is independently bounded to 256 MiB. Packet coding scratch,
framing/parity and copies have checked bounds derived from content size; this is
not a hard cap on process RSS. `dsp_workspace_bytes` defaults to 50% of available
RAM and budgets streaming sample queues, retained pattern-symbol recordings and
DSP state independently of the content quota. The GUI dropdown offers 25%, 50%
and 75%; it shows the resolved byte ceiling, which stays fixed until the choice
changes. Signal history is not subject to the 256 MiB received-content limit.
Transmitter chunk storage does not grow with tone duration. Default FFT receive
workspace grows with symbol length; its bounded clock-window fallback trades
coverage and computation for duration-independent waveform storage. Unsupported
clock-window coverage is rejected explicitly.

`transfer::estimate` allocates encoded packet data, never an audio waveform. It
reports content, full-packet and total durations, repeat eligibility, streaming
`memory_supported`, and separate `batch_memory_supported`. The one-byte repeat
exception does not waive content, workspace or numeric-range checks.

Legacy `tests/test_regressions.cpp` covers 2.4 kHz 128/1024-tone transfers whose full PCM
would exceed the DSP budget, a 24 kHz fractional-carrier raw PCM roundtrip,
automatic integration beyond the old 16,384-chip ceiling, and fixed five-second
training shorter than one payload symbol. Another raw PCM case replaces all
five seconds of training with noise and requires acquisition of the following
slow-symbol packet with body FEC off. Live tests cover ongoing noise,
ordered provisional/final events, cancellation, content/DSP limits and receive
resumption. New `test_pattern_code`, `test_pattern_receiver`, `test_pattern_transfer` and
CLI/GUI three-bit regressions exercise actual pattern waveforms and exact raw
recovery. These tests do not establish physical-device performance. Consult
[validation.md](validation.md) for which builds and tests have actually run.

Weak-channel regressions explicitly disable oscillator impairments when isolating
the planner's AWGN integration assumption. Separate channel tests cover relative
clock drift, phase diffusion, coherence loss, seeded replay and bounded previews.
An ideal-oscillator weak-channel result does not establish the same sensitivity
with the default 100 ppm crystal, on raw PCM, or on physical hardware.

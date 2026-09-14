# Audio modem reference

The default CLI and GUI transport carries one bit per independent pattern
codeword. The pattern receiver discovers signal start, bit sequence and end
from pattern evidence alone. It compares timing, carrier and keystream positions
without a preamble, packet header, checksum or APSK residual lock condition.
Short text (under 16 original bytes) uses the fixed bit-prefix dictionary with
no byte padding, framing or FEC. Explicit raw drafts send exactly their 0/1 bits.
Larger messages and attachments retain the packet codec after pattern recovery.

Pattern transport is the only supported waveform. Explicit
`Config::pattern_symbols = false` or multi-bit APSK profiles are rejected.
The old fixed training prefix, repeated spreading templates and final-symbol
zero padding have been removed. Existing keyfiles remain usable, but peers
must use the new private waveform.

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
refreshed once per chip, at the payload's mean transmit power.
Its radius is limited and normalized to keep peaks inside PCM headroom.
Private payload and prefix use the same update cadence and noise mapping.
Rectangular pulses have sidelobes; bandwidth is nominal, not an enforced spectral mask. It helps external gain control and muting settle before
fast payload symbols arrive, and is not made from the legal payload patterns.
It supplies no training, header or acquisition condition: pattern evidence
alone accepts the following symbols even when the prefix is lost or distorted.
Any selected key XOR-encrypts the preamble noise bytes with its existing
Data-purpose key and transmission epoch before I/Q mapping. Enabled Scrambler and DSSS bytes XOR in before that same
mapping, using
the same keys and epoch as their payload streams. The high eight CTR counter
bytes contain the fixed ASCII pad `preamble`, separating prefix positions
without generating or deriving extra keys. Tone mode uses the same noise
prefix, refreshing phase and amplitude once per chip;
it reserves no payload constellation points. The transmission epoch is fixed
before the prefix, and clock-start hypotheses include its elapsed time.
Airtime estimates include the prefix without adding to meaningful payload bits.

### Private waveform and remaining signal structure

Private templates map eight mixed keystream bytes per chip to circular noise,
with both amplitude and phase varying. The bounded Gaussian transform has unit
expected complex power before the transmitter's amplitude scale. Its two bit
alternatives retain a public internal-transition mask, so the same iterative
pattern comparison works under unknown common gain and phase. FFT scoring uses
actual template energy; the clock-window path fits its full Gram matrix.
See [exact stream mapping](crypto.md#binary-pattern-chip-addressing).

Previously private spreading only selected a real +/-1 multiplier of the carrier.
Squaring the transmitted PCM canceled every private sign. That was a mapping
defect, not a requirement of pattern-only acquisition. Circular private templates
remove that invariant and the public constant-envelope payload signature.
Band occupancy, regular chip timing, capped amplitudes and burst duration remain;
no claim of absolute indistinguishability from noise is made.

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
Tone modes always disable Data encryption, Scrambler and DSSS. The GUI clears
and disables key selection; CLI and transfer APIs ignore selected keys for
tone transfers. These modes are for unencrypted communications or experiments
and are not LPI modes.

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
explicit pattern configuration and cannot be combined with automatic planning
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
`StreamingReceiver` wraps `PatternReceiver` for all supported configurations. The pattern
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
must discover pattern symbols and signal boundaries from these samples; all packet interpretation follows acquired pattern bits. Finite carrier and
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
prefix. The normal timeline contains 60 frames. Each
stores a compact 256-sample waveform, 257 peak-pooled spectrum bins, the fresh
measured input I/Q and retained pattern evidence for its interval. Pattern
acquisition does not switch the I/Q source. Reception continues through ordinary
channel noise after the source stops; the final frame can include that tail and
its newly completed evidence. Later evidence is never applied to an earlier frame.
Waveform previews retain bounded recent receiver samples from the actual
channel, including its noise, carrier phase and spreading.

After sampled computation completes, the GUI plays the entire timeline over
three wall-clock seconds. Preparation does not wait for simulated airtime.
Every new frame updates waveform and constellation and adds one waterfall row.
Completed raw bits and short dictionary text are released at the three-second deadline;
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
copied even when the expected bit count is unknown. The aligned legacy
`modem::BinaryReceiver` API has been removed.

## Batch PCM, WAV and few-bit status

Batch `transfer::transmit`/`receive`, `modem::simulate` and WAV interfaces
operate on complete PCM vectors and retain their batch allocation checks.
The transfer APIs use pattern-only acquisition; streaming support does not make an
arbitrarily large WAV fit in memory. The sample-domain channel adds leading delay,
AWGN, fixed frequency shift, relative sample-clock error and Wiener phase noise.
It interpolates the analytic PCM waveform at the altered clock and applies phase
noise sample by sample. The continuous sampled channel exercises the same
waveform timing and chip correlation without retaining a complete PCM vector.

Use `PatternReceiver`, `transfer::receive` or live reception for blind sample
acquisition. The removed known-training demodulator cannot be used as a
fallback. Batch PCM and streaming DSP feasibility are reported separately.

WAV is little-endian RIFF PCM16 mono. Readers validate chunk/container lengths,
sample and byte rates, format and allocation limits. Writes round and clip finite
samples to PCM16. Clipping a loud noise waveform changes its SNR.

Automatic `status-tx` sends one pattern symbol per supplied 0/1 bit through the
same raw-bit transmitter. `status-rx` discovers the complete bit string through
pattern evidence, then compares `--bits` afterward. JSON includes the exact raw
bits/count, model score and `packet_validated: false`; a wrong comparison value
does not alter acquisition. No status result authenticates the sender.

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

The regression suite covers pattern-only acquisition through missing or obscured
settling, exact raw bits, encrypted packets, independent sample clocks, bounded
long-symbol searches, cancellation and tone policy. Private-template tests check
both quadratures, variable amplitude, key/epoch separation, absence of the old
squared-carrier invariant, and energy-normalized receive evidence. They do not
establish physical-device interception resistance. Consult [validation.md](validation.md)
for previously measured build and hardware boundaries.

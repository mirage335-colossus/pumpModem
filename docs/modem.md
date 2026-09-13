# Audio modem reference, application version 0.7.0

Version 0.7.0 uses shared differential 4/8/16/32/64-APSK waveforms for PCM and
continuous operation. The selected profile carries two through six bits per
payload symbol, with both amplitude and phase modulation. Audio peers require
matching carrier and modem settings and the compact 0.7 packet format. Ordinary
one-word messages have a four-byte bootstrap and no RS coding. Other short
messages typically use six to nine bootstrap bytes when coding is enabled.
Gain acquisition accounts for a short header that occupies only a subset of
amplitude rings. Fixed training, three-second
simulation presentation, the narrow-audio 1500 Hz carrier and public whitening
are unchanged. Existing keyfiles need no migration.

This is a reference modem. It does not establish near-capacity throughput,
calibrated radio sensitivity, a spectral mask, or low probability of intercept.
The regression suite is described below; current execution results belong in
[validation.md](validation.md).

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
selected. The separate few-bit status API has no preamble.

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

The named choices are:

| Mode | Behavior |
| --- | --- |
| `auto-keystream` | Automatic integration with keyed chip signs; falls back to `auto-pattern` without a key. |
| `auto-pattern` | Automatic integration using the fixed pattern. |
| `auto-tone` | Automatic integration with constant chip signs. |
| `pattern-3`, `pattern-4`, `pattern-6`, `pattern-8`, `pattern-12`, `pattern-16` | The named number of fixed-pattern chips. |
| `tone-1`, `tone-2`, `tone-3`, `tone-4`, `tone-8`, `tone-32`, `tone-128`, `tone-1024`, `tone-4096`, `tone-16384` | The named tone duration in chips, without pattern sign changes. |

Tone reception requires narrower operating conditions, such as GNSS timing
synchronization, low frequencies, high symbol rates and suitable hardware.
Automatic regression checks must not force these tone modes or expect their
simulations to decode. They exercise frequent, measurable phase/amplitude shifts
with fixed or seeded pseudorandom patterns instead; those differential
measurements also underpin tone shifts when the operating conditions permit.

Automatic integration can exceed the duration of the largest named tone through
`Config::integration_seconds`. Named factors remain useful explicit choices;
they no longer impose a 16,384-chip ceiling on automatic duration. Finite numeric
range and supported sample timing still limit representable configurations.

The fixed pattern and optional keyed signs multiply the data waveform. The
bounded chip template contains at most 16,384 signs and repeats within each
payload symbol; it is not an ever-growing per-frame chip stream. Scrambler
and DSSS seeds come from separate purpose-specific crypto streams at the same
candidate epoch. Tone omits the fixed-pattern/scrambler signs; an independently
selected DSSS layer remains separate. The constellation preserves both amplitude
and differential phase; plotting each observation on a unit circle would erase
part of the transmitted information.

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

The CLI and GUI use automatic planning by default: a target C/N0 of 40 dB-Hz,
with `auto-keystream` when a key is selected and `auto-pattern` otherwise.
Explicit CLI `--spreading`, `--scramble`, `--dsss`, `--sample-rate` or `--carrier`
controls select the manual configuration instead. Those controls cannot be
combined with an explicit `--target-snr` or `--pattern` automatic plan.
The simulator's `--snr` describes channel noise and does not select the transmit
configuration.

`tuning::resolve` accepts bandwidth, target **C/N0 in dB-Hz**, a pattern mode and
key availability. C/N0 is signal power divided by noise power in a 1 Hz reference
bandwidth. For channel bandwidth `B`, `SNR_B = C/N0 - 10 log10(B)`.

The planner chooses an internal sample rate/carrier and evaluates all supported
constellations. For each profile it estimates the integration needed for a
geometry-based noise margin, then chooses the highest gross bit rate among the
profiles meeting that margin. Larger amplitude alphabets increase density
without narrowing the phase spacing beyond eight positions. Six bits per symbol
still requires at least two symbols for an isolated byte; all profiles retain
at least four distinct signals for short content.

```text
angular_distance        = sqrt(2) * radius_increment * sin(pi / phase_count - pi / 64)
distance                = min(radius_increment, angular_distance)
target_Es/N0_dB          = 10 log10(2 * 3.2^2 * 0.30625 / distance^2)
required_symbol_seconds = 10^((target_Es/N0_dB - C/N0) / 10)
estimated_Es/N0_dB       = C/N0 + 10 log10(symbol_seconds)
bit_rate                = selected_bits / symbol_seconds
```

This conservative geometric model uses the worst inner-ring angular distance,
differential phase noise and a 3.2-sigma half-distance margin. It reserves
2.8125 degrees of drift per symbol; the narrowest phase decision half-width is
22.5 degrees. This is a design margin, not a carrier/clock tracking algorithm.
For sufficiently long symbols even small frequency errors exceed it.

Automatic modes choose finite pattern factors or longer integration as necessary.
Weak C/N0 favors smaller constellations and shorter integration than a fixed dense
profile would require. Forced modes preserve their selected duration and choose
the fastest supported constellation that meets the margin; if none can, the
planner retains the most robust profile and reports the unmet target. Numeric
limits are checked. This is optimization over the implemented profile set, not a
proof of capacity-optimal throughput or calibrated packet error rate.

These estimates concern payload integration, not guaranteed acquisition or a
measured error rate. In particular, making payload symbols arbitrarily long
does not increase the energy in a fixed five-second preamble. Reception therefore
uses pattern constellation evidence and the compact packet bootstrap for blind acquisition rather than requiring
a training correlation threshold. It can acquire a long-symbol header even when
training is obscured, subject to the finite timing bank and successful frame
validation. Oscillator drift, fading and finite receiver hypotheses impose further
limits. No thermal sensitivity or near-capacity claim follows from the planner.

## Incremental receiver and live audio

`StreamingTransmitter` emits bounded PCM chunks or complex symbol integrals.
`StreamingReceiver` accepts chunks and retains measured pattern-symbol recordings
for its admitted timing/gain candidates. After the last symbol, it replays each
complete recording, refines gain, and compares full-packet differential
constellation residual SNR. An adequate digest/MAC-valid match stays provisional:
live input allows one more symbol interval from the earliest complete fit so
neighboring timing windows can finish. Finite captures compare all available
final windows at `finish()`. Only the highest-SNR valid complete fit in the
admitted search is emitted, with its original pre-FEC decisions and diagnostics.
This also applies to frames larger than 2048 encoded bytes.

The history stores despread complex symbol measurements, not hard-decision
bytes alone; long symbols do not require a PCM allocation for their full duration.
A finite bank of at most eight full recordings shares the configured DSP budget.
The GUI DSP history dropdown offers 25%, 50% (default), or 75% of available RAM,
resolved at startup or when that selection changes. The budget is a ceiling,
not an upfront allocation; OS/container memory headroom is used where available.
Received messages/files retain their separate default 256 MiB content quota.
That content quota does not cap signal history. A frame still needs sufficient
DSP space for its recorded measurements and replay; transmit estimates include
that requirement. Idle keys in the live bank lend unused space to active
recordings, and failed recordings release their storage.

Acquisition searches a finite set of timing hypotheses at the configured carrier,
using the pattern constellation's radial and differential-phase fit, followed by
compact header checks. It does not require the five-second training bytes to
be observable. Initial phase labels are tried explicitly; FEC Off no longer
depends on RS repairing that ambiguity. Header acceptance starts a provisional decoder while other timing
hypotheses keep searching. Full-frame integrity and the completed SNR comparison
commit the receive fit. PCM mixing solves the I/Q Gram system so it does not assume
an integer number of carrier cycles per integration.
Dense profiles first screen amplitude-lattice residuals using three times the
radial noise standard deviation at the planner's geometric margin. Bounded gain
hypotheses consider every possible highest occupied ring; full protected-header
validation and complete frame checks resolve the ambiguity when outer rings are absent. This keeps idle
noise fitting inexpensive. The screen is designed for the stated AWGN margin;
it is not a guarantee that every impulsively corrupted, otherwise RS-correctable
waveform will be acquired.
An additional differential phase/amplitude residual check includes the planner's
allowed phase drift. Repeated decoded probes share bounded validation caches;
they do not repeat RS and CRC work for every equivalent timing/gain hypothesis.
Provisional frames can supply measured constellation points and mutable
text previews, but they cannot select the active key, enter the verified inbox,
or become copyable/savable content. Candidate rejection clears its tentative
text. The full packet's digest or MAC remains the delivery condition.
Keyed continuous reception builds a bounded bank of loaded-key and candidate-
epoch receivers. The default search is plus or minus six whole seconds; the live
API accepts at most 60 seconds in either direction and 128 loaded keys, with the
aggregate bank constrained by the DSP budget. A bank that exceeds that budget
is rejected. This is not a whole-band search, unlimited timing search, arbitrary
Doppler tracker, or proof of reliable multi-day acquisition.

Tone acquisition uses up to 64 symbol phases; ordinary pattern acquisition uses
up to 256. Keyed templates of 1,024 chips or more redistribute 224 origins between
112 coarse phases and 112 finer positions within about two chips of the fixed
training boundary; duplicate positions are merged. This recovers capture-aligned
and nearby long encrypted starts without storing their PCM duration. Arbitrary
unknown start times can still fall outside this finite chip-alignment search.
Long blind integrations retain their admitted epoch bank for a bootstrap span;
they do not accumulate an unlimited set of new wall-clock epochs.

Streaming reception assumes carrier error is within the chosen integration's
tolerance. The generic batch known-training decoder can estimate a small static
carrier offset, but that search is not part of the blind streaming packet path.
Simulation exercises this same PCM acquisition and chip correlation with
arbitrary startup phase and offset. It cannot establish arbitrary long
encrypted-pattern acquisition or demonstrate an unimplemented tracking loop.

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
The GUI consumes fresh symbol observations from the provisional or locked receiver. Their
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

The signal browser's **Preamble** percentage is recognized training duration
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
selected APSK alphabet and the current observation count. No ideal or missing
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
matched-despreading statistic is handed to the receiver. The receiver must find
a valid bootstrap and complete packet from these samples. It has no continuous
clock or frequency tracking loop, so an impaired signal can fail acquisition or
validation. Longer integration alone cannot repair oscillator coherence loss.
Use `--clock-error-ppm 0 --phase-noise 0` with CLI `simulate` or `listen` when
intentionally testing ideal oscillator stability; startup remains unsynchronized.

Samples are processed in bounded chunks at CPU speed without sleeping for the
advertised on-air duration or retaining the whole waveform. Memory stays bounded
with airtime, but CPU work scales with the number of samples. High sample rates
and hour-long symbols can therefore take substantial time. Cancellation is
checked between and within chunks. The model does not establish fading/multipath
performance, nonlinear hardware behavior or interference rejection.

During computation, snapshots are captured at evenly spaced media positions from
the start of fixed training to the end of the transmitted packet, including the
protected header and encoded body. The normal timeline contains 60 frames. Each
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
packet may validate without a separate provisional event. Complete verified text, received
file entries and pre-FEC accuracy are released together at the three-second
deadline. Failed decoding supplies no verified result. A prepared packet cannot
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
metadata, an attachment, repeat requests, compression, fixed training, a packet
header, integrity tag or error correction.

Each complete group uses the configured APSK constellation. A partial final group
selects a smaller set of points from that constellation; it carries only the
remaining bits. Airtime is the number of groups times the quantized symbol duration,
without byte padding or a five-second preamble. Selected keys mask only the actual
bits with the data stream and seed the normal scrambler/spreading configuration.

Raw signals have no packet bootstrap or authentication. Simulation feeds their
actual waveform to the same continuous blind receiver as packet and audio input.
It does not construct an aligned receiver from the transmitted bit count, key,
start time or initial carrier phase. Automatic raw discovery is not implemented,
so transmitting raw bits does not produce a received bit string or a verified
packet result. Transmitted waveform and constellation previews remain available.

The low-level `modem::BinaryReceiver` API remains an explicitly aligned diagnostic
that requires a known bit count, symbol origin, gain and carrier reference. It is
not used as evidence of unsynchronized simulation reception. The following CLI
status API retains its separate DBPSK format.

## Batch PCM, WAV and few-bit status

The legacy `transfer::transmit`/`receive`, `modem::simulate` and WAV interfaces
operate on complete PCM vectors and retain their batch allocation checks.
They use the shared 0.5 modem configuration; streaming support does not make an
arbitrarily large WAV fit in memory. The sample-domain channel adds leading delay,
AWGN, fixed frequency shift, relative sample-clock error and Wiener phase noise.
It interpolates the analytic PCM waveform at the altered clock and applies phase
noise sample by sample. The continuous sampled channel exercises the same
waveform timing and chip correlation without retaining a complete PCM vector.

The generic raw-byte `modem::demodulate` API uses known-training timing and
constant carrier-offset acquisition with the shared adaptive APSK quantizer. Normal
packet `transfer::receive` and live reception use protected-bootstrap streaming
acquisition instead. Thus obscured-training packet recovery does not imply that
the generic raw-byte API can acquire arbitrary bytes without known training.
The legacy batch feasibility check includes its complex acquisition workspace;
streaming DSP feasibility is reported separately.

WAV is little-endian RIFF PCM16 mono. Readers validate chunk/container lengths,
sample and byte rates, format and allocation limits. Writes round and clip finite
samples to PCM16. Clipping a loud noise waveform changes its SNR.

The separate status API emits one DBPSK symbol per supplied 0/1 bit, without
byte padding, preamble, header, authenticator or FEC. Known-status detection
requires aligned samples. A status correlation is neither authenticated identity
nor validated text/file reception. There is no automatic unknown-beacon monitor.

## Resource boundaries and verification

`Options::content_limit` defaults to 256 MiB and limits application content.
The GUI receive cache is independently bounded to 256 MiB. Packet coding scratch,
framing/parity and copies have checked bounds derived from content size; this is
not a hard cap on process RSS. `dsp_workspace_bytes` defaults to 50% of available
RAM and budgets streaming sample queues, retained pattern-symbol recordings and
DSP state independently of the content quota. The GUI dropdown offers 25%, 50%
and 75%; it shows the resolved byte ceiling, which stays fixed until the choice
changes. Signal history is not subject to the 256 MiB received-content limit.
Increasing tone duration alone does not require a larger sample workspace.

`transfer::estimate` allocates encoded packet data, never an audio waveform. It
reports content, full-packet and total durations, repeat eligibility, streaming
`memory_supported`, and separate `batch_memory_supported`. The one-byte repeat
exception does not waive content, workspace or numeric-range checks.

`tests/test_regressions.cpp` covers 2.4 kHz 128/1024-tone transfers whose full PCM
would exceed the DSP budget, a 24 kHz fractional-carrier raw PCM roundtrip,
automatic integration beyond the old 16,384-chip ceiling, and fixed five-second
training shorter than one payload symbol. Another raw PCM case replaces all
five seconds of training with noise and requires acquisition of the following
slow-symbol packet with body FEC off. Live tests cover ongoing noise,
ordered provisional/final events, cancellation, content/DSP limits and receive
resumption. These tests do not establish physical-device performance. Consult
[validation.md](validation.md) for which builds and tests have actually run.

Weak-channel regressions explicitly disable oscillator impairments when isolating
the planner's AWGN integration assumption. Separate channel tests cover relative
clock drift, phase diffusion, coherence loss, seeded replay and bounded previews.
An ideal-oscillator weak-channel result does not establish the same sensitivity
with the default 100 ppm crystal, on raw PCM, or on physical hardware.

# Audio modem reference, waveform version 0.3

Version 0.3 uses shared differential 4/8/16/32/64-APSK waveforms for PCM and
continuous operation. The selected profile carries two through six bits per
payload symbol, with both amplitude and phase modulation. Audio peers require
matching 0.3 modem settings. Packet versions 1 and 2, and the existing keyfile formats,
remain unchanged; changing the waveform does not migrate or regenerate keys.

This is a reference modem. It does not establish near-capacity throughput,
calibrated radio sensitivity, a spectral mask, or low probability of intercept.
The regression suite is described below; current execution results belong in
[validation.md](validation.md).

## Timing, training, and spreading

The default internal configuration is mono 48 kHz PCM, a 1,500 Hz carrier and
1,200 Hz nominal bandwidth. The modem's nominal chip rate is bandwidth / 2.
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

The fixed 72-byte protected bootstrap and the remaining packet body each finish
on a symbol boundary. Unused bits are zero padding, excluded from decoded bytes.
The five-bit profile needs padding at the bootstrap boundary. `payload_symbol_count`
includes both boundaries; repeatable airtime counts the incremental symbols
relative to an empty packet with the same metadata.

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

The named choices are:

| Mode | Behavior |
| --- | --- |
| `auto-keystream` | Automatic integration with keyed chip signs; falls back to `auto-pattern` without a key. |
| `auto-pattern` | Automatic integration using the fixed pattern. |
| `auto-tone` | Automatic integration with constant chip signs. |
| `pattern-3`, `pattern-4`, `pattern-6`, `pattern-8`, `pattern-12`, `pattern-16` | The named number of fixed-pattern chips. |
| `tone-1`, `tone-2`, `tone-3`, `tone-4`, `tone-8`, `tone-32`, `tone-128`, `tone-1024`, `tone-4096`, `tone-16384` | The named tone duration in chips, without pattern sign changes. |

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

Audio I/O first tries the internal rate on the selected endpoint, then other
supported rates, preferring higher clocks to preserve passband. Explicit device
identity and default-card discovery are preserved. ALSA software resampling and
WinMM ACM conversion are disabled during this negotiation. A bounded Blackman-
windowed sinc converter connects the selected hardware clock to the internal
clock, preserving phase, duration and callback continuity. It uses 256
interpolated fractional phases. For unequal rates, its declared flat passband is
0.42 times the lower rate and its transition rolls off before Nyquist. Equal-rate
conversion is bit-exact and reports the Nyquist bound of 0.5 times the rate.
Finite streams produce `ceil(input_count * output_rate / input_rate)`
samples, with zero extension at their boundaries. Converter memory depends on
rate ratio, never transmission duration, and is included in live DSP accounting.

Interpolation cannot restore frequencies beyond hardware Nyquist or compensate
unknown analog filtering. The GUI shows the negotiated clock and indicates when
the selected band exceeds the converter's usable passband. Oscillator drift and
physical microphone/speaker frequency response still need device-level validation.

## Automatic signal planning

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
uses the protected packet bootstrap for blind acquisition rather than requiring
a training correlation threshold. It can acquire a long-symbol header even when
training is obscured, subject to the finite timing bank and successful bootstrap
correction. Oscillator drift, fading and finite receiver hypotheses impose further
limits. No thermal sensitivity or near-capacity claim follows from the planner.

## Incremental receiver and live audio

`StreamingTransmitter` emits bounded PCM chunks or complex symbol integrals.
`StreamingReceiver` accepts chunks, retains bounded acquisition/integration state,
and emits newly decoded bytes. It does not retain the whole audio duration or
allocate one sample buffer per long symbol. Packet bytes are accumulated under a
separate content budget and pass the ordinary bootstrap, FEC, metadata and final
digest/MAC checks before becoming a received message.

Acquisition searches a finite set of timing hypotheses at the configured carrier,
using corrected protected-bootstrap structure as its synchronization validator.
It does not require the five-second training bytes to be observable. Full packet
digest/MAC verification is still mandatory; bootstrap acceptance is not final
content validation. PCM mixing solves the I/Q Gram system so it does not assume
an integer number of carrier cycles per integration.
Dense profiles first screen amplitude-lattice residuals using three times the
radial noise standard deviation at the planner's geometric margin. Bounded gain
hypotheses consider every possible highest occupied ring; full protected-header
validation resolves the ambiguity when outer rings are absent. This keeps idle
noise fitting inexpensive. The screen is designed for the stated AWGN margin;
it is not a guarantee that every impulsively corrupted, otherwise RS-correctable
waveform will be acquired.
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
Accelerated simulation assumes matched chip despreading and ideal carrier timing;
it cannot establish arbitrary long encrypted-pattern acquisition on real audio.

`live::Session` owns continuous capture, preparation and decoding workers. Normal
GUI startup opens the OS-default input; an optional device override is available.
Actual playback pauses capture and resumes it afterward. Incoming audio arrives
in small chunks; queues, acquisition state and current plots are bounded. The
obsolete `receive_buffer_seconds` setting no longer determines an audio window.
Cancellation is checked between processing chunks and driver buffer operations.
A driver blocking inside an operating-system call remains outside that guarantee.

The waveform, windowed FFT and measured complex constellation feed live plots.
Idle noise plots update about 20 times per second. The constellation uses one
shared display scale and retains relative amplitudes. Its axes and amplitude
rings are display aids, not a calibration certificate. Decoder diagnostics and
acquisition scores are evidence of signal processing, never packet authenticity.
Positive rates too small for decimal display use scientific notation instead of
rounding to `0.0 bit/s`.

Pending ticker observations can change as additional bytes and parity arrive.
They stay visibly provisional and cannot trigger clipboard copy or file save.
Only complete validated text can be copied; validated files and screenshots
appear in the separate RAM file list for explicit exclusive save. Ordered event
serials preserve pending-before-final order even if several events reach the GUI
in one poll.

## CPU-bounded simulation

The continuous simulator uses the same symbol mapping, training schedule,
receiver decisions, packet correction and validation as the PCM path. It advances
virtual media time by the processed symbol durations. It runs as fast as bounded
DSP work permits, yielding between chunks; it does not sleep for the advertised
on-air duration or allocate that duration as PCM. No fixed real-time multiplier
is required. TX progress distinguishes virtual elapsed seconds from wall-clock
CPU time, and completion/cancellation remain observable between GUI polls.

For the accelerated AWGN path, complex integrated symbol observations receive
noise with the variance implied by sample-domain white noise and their sample
counts. This is a coherent, ideal-carrier, ideal-symbol-timing channel model.
Uniform-time observation bins cross training/payload boundaries without carrying
framing labels. Chip despreading alignment is ideal in this statistical model;
the receiver still searches phase/header hypotheses. It avoids synthesizing
every sample of a very long tone. It is not a substitute
for raw PCM acquisition tests, and does not model arbitrary timing offsets,
resampling drift, phase noise, multipath, nonlinear hardware or interference.
Plots use bounded signal/noise previews rather than a retained whole transmission.
Twenty-four spectrum rows are captured by media position between one-quarter
and one-half of the payload, independent of GUI polling. On simulation completion,
the waveform and spectrum show the payload-midpoint sample for two wall-clock
seconds. A bounded transmitter segment history reconstructs the actual local
waveform, including carrier phase and spreading, with independent display noise.
The constellation uses accumulated receiver observations (up to 2,048 points),
not transmitted ideal points. After the hold, live waveform/waterfall updates
resume while the received constellation persists until the next transmission
or configuration. Background simulation reception continues throughout the hold.
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

## Batch PCM, WAV and few-bit status

The legacy `transfer::transmit`/`receive`, `modem::simulate` and WAV interfaces
operate on complete PCM vectors and retain their batch allocation checks.
They use the shared 0.3 waveform; streaming support does not make an arbitrarily
large WAV fit in memory. The sample-domain channel can add leading delay, AWGN
and a fixed frequency shift. This path tests effects excluded by the accelerated
ideal-timing model.

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
not a hard cap on process RSS. `dsp_workspace_bytes` defaults to 64 MiB and budgets
streaming sample queues and DSP state independently of content and duration.
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

The weak-channel regression also derives sampled AWGN from C/N0 targets of
-20 and -60 dB-Hz and checks several deterministic seeds with RS60. This tests
integration at the planner's actual noise assumption within the ideal-timing
statistical model. It does not establish those sensitivities on raw PCM or
physical hardware.

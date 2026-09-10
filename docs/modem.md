# Audio modem reference

The implemented physical layer is differential quadrature phase-shift keying
(DQPSK) with integrate-and-dump reception, optional chip spreading, known-training
acquisition, and bounded in-memory diagnostics. It is a working reference modem,
not an implementation of the requested near-capacity adaptive high-order modem.
It does not claim performance close to Shannon capacity, calibrated radio
sensitivity, a particular occupied-bandwidth mask, or low probability of intercept.

## Waveform and rates

The unspread physical-layer default is mono 48 kHz audio, a 1,500 Hz carrier, and
a nominal 1,200 Hz bandwidth setting. Nominal chip rate is bandwidth / 2. Samples per chip are
rounded to the nearest multiple of four; the actual chip rate is sample rate /
samples per chip. Each QPSK symbol spans `spreading_factor` chips and carries two
bits. The actual bit rate is therefore twice chip rate / spreading factor.
At spreading factor one this is 1,200 bit/s before training, framing, MAC, and
FEC overhead.
Automatic tuning may select a much slower rate for the requested noise level.

Bytes are sent most significant pair first. Pair-to-differential-phase mapping is
00 = 0, 01 = +pi/2, 10 = -pi/2, 11 = pi. Initial phase is zero; every radiated
symbol still represents caller-provided data. No reference symbol, synchronization
word, length header, or footer is inserted by `modulate`. It is the caller's job
to concatenate the preamble and framed payload, encrypt that whole sequence when
requested, and supply the matching plaintext or ciphertext preamble to reception.

Pulse shapes are rectangular. Their sidelobes extend outside the nominal
bandwidth; a transmit filter and verified spectral mask are future work. Carrier
and nominal bandwidth must fit between DC and Nyquist. For example, the 24 kHz
setting works with a 12 kHz carrier at 96 kHz sampling. The floating-point peak
transmit amplitude is 0.7.

`preamble` returns repetitions of ASCII `U3<Zif`. Each byte has four one bits and
four zero bits. Training lasts at least five seconds and has at least twelve
bytes, so very slow settings take longer than five seconds. This training is not
FEC-protected. Never require its received bytes to be error-free: strip its known
length and use packet FEC and integrity/authentication to validate the payload.
The no-training status mode is a separate API.

## Automatic tuning and signal estimates

`tuning::resolve` takes nominal bandwidth, target **C/N0 in dB-Hz**, a pattern
mode, and whether encryption is available. C/N0 is signal power divided by noise
power in a 1 Hz reference bandwidth. It differs from SNR measured over the whole
channel: for bandwidth `B` hertz, `SNR_B = C/N0 - 10 log10(B)`.

The planner selects a 48, 96, 192, or 384 kHz sample rate and a carrier that fit
the requested nominal bandwidth between DC and Nyquist. It uses the modem's
actual, quantized chip duration `T_chip` and a fixed engineering target of 10 dB
symbol energy to noise density, `Es/N0`:

```text
required_spreading = max(1, 10^((10 - C/N0) / 10) / T_chip)
estimated_Es/N0    = C/N0 + 10 log10(T_chip * spreading_factor)
processing_gain   = 10 log10(spreading_factor)
```

Automatic modes round up to the next supported factor from
`1, 2, 3, 4, 6, 8, 12, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384`.
If the required integration exceeds the maximum, the plan keeps the maximum
and reports `target_supported = false`. Explicit modes retain the selected
factor and also report whether it meets the estimate. These calculations do not
establish measured decoder sensitivity, successful acquisition, a bit-error
rate, or a near-capacity waveform.

For example, 1,200 Hz bandwidth and C/N0 of 6 dB-Hz require approximately 1,507
chips per symbol; auto selects 2,048, yielding 0.5859375 bit/s. At that rate the
minimum twelve training bytes occupy 163.84 seconds. Training is independent of
content size and excluded from the repeatability allowance, but still contributes
to the total duration and working memory. The ordinary 256 MiB buffered receiver
cannot process every such long packet. `transfer::estimate` reports the content,
packet, and total durations separately and checks the waveform/acquisition budget
without allocating a waveform. A supported signal estimate does not imply that
the transfer fits memory.

## Spreading and independent streams

The available named choices are:

| Mode | Waveform behavior |
| --- | --- |
| `auto-keystream` | Automatically selected factor with keyed chip signs; falls back to `auto-pattern` without a key. |
| `auto-pattern` | Automatically selected factor using the fixed pattern. |
| `auto-tone` | Automatically selected symbol duration with constant chip signs. |
| `pattern-3`, `pattern-4`, `pattern-6`, `pattern-8`, `pattern-12`, `pattern-16` | Exactly the named number of pattern chips per symbol. |
| `tone-1`, `tone-2`, `tone-3`, `tone-4`, `tone-8`, `tone-32`, `tone-128`, `tone-1024`, `tone-4096`, `tone-16384` | Exactly the named duration in chips; no fixed-pattern or keystream sign changes. |

In pattern mode with a spreading factor above one, the fixed sequence
`++-+--+-` repeats or truncates within each data symbol, restarting at each
symbol boundary. The eight-chip base sequence is balanced; arbitrary truncated
lengths need not be. This preserves the previous fixed-pattern waveform.
Tone mode uses constant positive chip signs while the caller's data still
controls the differential phase. It is a real waveform distinction, not a label
for the fixed pattern. Tone mode rejects `scramble`.

With `scramble` enabled, chip signs come from the project's AES-256-CTR crypto
abstraction, using its Scrambler
purpose. With `dsss` enabled, an independently seeded Dsss purpose stream supplies
another sign sequence. The two signs multiply; despreading occurs before symbol
integration and differential decisions.
The independent DSSS layer remains an explicit option; enabling it adds its
keyed signs even when the underlying mode is tone.

The caller must derive `spreading_seed` and `dsss_seed` independently from the
same candidate transmission timestamp and the corresponding purpose-specific
streams. The modem expands those seeds at its local stream position zero. The
DSP has no wall-clock logic and does not independently shift one layer. Default
seeds are intended only for deterministic tests; they provide no secrecy.

The reference does not implement a rotating high-order constellation lookup,
cryptographic RF hopping, convolutional codes, trellis decoding, or adaptive
modulation. The project packet layer supplies Reed–Solomon correction. Increased
spreading improves integration at a correspondingly lower throughput; there is
no claim that it creates additional channel capacity.

## Acquisition and diagnostics

Reception mixes audio to complex baseband and integrates over one chip, sampled
four times per chip. FFT correlation matches the differential known preamble
against the capture. The best coarse timing is refined hierarchically down to
single samples. Each refinement stage evaluates at most seventeen trial offsets;
this avoids a brute-force search over every sample of a multi-second symbol.
Known-symbol phase is unwrapped and fitted by linear regression to estimate a
small constant carrier offset. Reception does not track rapid Doppler changes,
resampling drift, multipath, or a changing carrier.

A preamble must pass both coarse differential correlation and coherent
known-symbol fitting. These are acquisition heuristics, not cryptographic or
packet-validity evidence. Diagnostics expose the acquired sample offset,
normalized training fit, estimated post-integration training SNR, actual bit
rate, up to 2,048 normalized differential constellation points, and up to 2,048
waveform samples. SNR is a measured training residual ratio, not dB/Hz and not a
calibrated receiver measurement.

Decoded bytes include the preamble and complete bytes recoverable from the
capture. Trailing silence can yield additional bytes. The packet reader must
respect its declared frame length. A short final fractional chip is tolerated
for the last byte when timing estimation falls slightly late. The receiver does
not expose soft bits. A higher layer can request a bounded provisional packet
preview from incomplete hard bytes; this does not change the modem's final
capture-based acquisition and decoding API.

## Continuous receiver

The native console and `pump listen` share `live::Session`. One worker maintains
the audio stream or continuously generated Gaussian noise, a second encodes queued
transmissions, and a third acquires and decodes snapshots of a rolling sample
buffer. Audio capture opens the OS-default input automatically and delivers short
chunks without reopening the device for each chunk. Real transmission pauses
capture and resumes it afterward. Simulation adds the prepared waveform to the
same noisy input stream, so it passes through the actual receiver while transmitting.

The raw waveform, windowed 2048-point FFT and unsynchronized measured differential
baseband constellation update about 20 times per second. The GUI turns spectrum
frames into a false-color waterfall and animates frequency-labeled ticker rows.
Provisional text is bounded and never promoted to a received file before full FEC
and integrity/authentication checks succeed. Consumed samples are discarded to
prevent repeated delivery. A protected incoming frame length can expand the
receive window within its memory budget.

The default rolling window is 30 seconds, capped by a conservative allocation
budget shared with decoding and prepared transmission. Valid but unbufferable
weak-signal settings still show live noise/plots and report the acquisition limit.
They do not silently become successful transfers. Very long integration still
requires a future incremental DSP implementation. The receiver decodes one
configured carrier; the frequency label is not a claim of whole-band scanning.

## Simulation and WAV

`simulate` adds zero-mean white Gaussian noise at a specified ratio to the input
waveform's measured sample power. The seed makes repeated runs deterministic
within the same C++ library implementation. Noise is present during the specified
leading delay as well. An optional fixed frequency shift uses an FFT analytic
signal; it is a channel test, not automatic radio Doppler tracking.

The physical presets interpret the first number as transmit power in dBm and
the second as negative channel attenuation in dB. Available presets are `no`,
`3dBm -6dB`, `3dBm -60dB`, `3dBm -90dB`, `3dBm -120dB`, `3dBm -170dB`,
`3dBm -200dB`, `3dBm -230dB`, `50dBm -200dB`, `50dBm -270dB`, and
`70dBm -250dB`. They assume thermal noise density of -174 dBm/Hz and a 10 dB
noise figure, with no additional gains or losses:

```text
received_dBm     = transmit_dBm + attenuation_dB
noise_dBm        = -174 + 10 + 10 log10(B)
channel_SNR_dB   = received_dBm - noise_dBm
C/N0_dBHz        = received_dBm - (-174 + 10)
sample_SNR_dB    = C/N0_dBHz - 10 log10(sample_rate / 2)
```

The last value feeds `modem::ChannelConfig::snr_db`, since generated white noise
occupies the real sampled Nyquist bandwidth. Using channel SNR directly would
understate the noise when the sample rate is high relative to the channel.
For `3dBm -170dB`, received power is -167 dBm and C/N0 is -3 dB-Hz; at 1,200 Hz
the in-band SNR is about -33.79 dB. These are input assumptions for an AWGN test,
not evidence that this receiver can recover the packet. The model does not
include antenna behavior, fading, interference, front-end saturation, oscillator
stability, or measured hardware sensitivity. Extreme presets can fail decoding.

WAV I/O is little-endian RIFF PCM16 mono. Reads validate container and chunk
bounds, format, sample and byte rates, and allocation limits; unknown bounded
chunks are skipped. Unsupported encodings and channels fail explicitly. Writes
round and clip finite samples to PCM16 range. Thus writing an excessively loud
noisy simulation to PCM16 can clip it and change its SNR.

## Few-bit status

`modulate_status` accepts elements that are exactly 0 or 1 and emits one
DBPSK symbol per element. There is no byte padding, preamble, header, checksum,
authenticator, or FEC; a three-bit input emits exactly three symbols. The status
bit rate is half the normal DQPSK bit rate for the same configuration.
`detect_status` currently returns signed normalized coherent correlation for a
known bit sequence and an exactly aligned, exactly sized capture. It requires
phase and timing alignment and does not implement a continuous unknown-callsign
search or day-long streaming integration. Higher layers must not present a status
correlation as authenticated identity or validated file/text data.

## Resource policy and verification

The default memory limit is 256 MiB. Audio, generated waveforms, FFT workspaces,
spreading sequences, and decoded symbol arrays are checked before large
allocations. Conservative workspace estimates can reject a capture whose raw
samples alone fit within the limit. Capture is finite and in-memory; streaming
multi-day very-slow operation and billions-of-symbol searches are not implemented.
Non-finite configuration values and audio samples are rejected. Spreading factors
are limited to 1..16,384; training time is limited to 5..32,768 seconds.
`transfer::estimate` accounts for the actual encoded packet and byte-rounded
training duration. The repeat policy separately counts incremental coded content
airtime only; see [protocol.md](protocol.md). Its one-byte exception does not
bypass the modem's allocation checks.

The tests exercise binary loopback, arbitrary sample delay, Gaussian noise,
static frequency offset, simultaneous independent seeded spreading streams,
ciphertext-like training, the 24 kHz bandwidth preset, pure-noise and wrong-preamble
rejection, WAV quantization and malformed inputs, finite-value and memory limits,
and exact-length three-bit status. They use explicit checks that remain active
in release builds. No physical soundcard or radio-channel performance is implied
by these deterministic software tests.
Tuning tests cover every named mode, distinct tone/pattern waveforms, C/N0
integration calculations, physically normalized noise presets, and unsupported
targets. Transfer tests compare estimates against actual sample counts and check
the inclusive repeatability boundary, metadata exclusion, and one-byte floor.

## Windows audio lifecycle

The Windows WinMM adapter uses an event callback and two prepared 4,096-sample
buffers. Playback is paused while its initial buffers are queued, then restarted;
each completed buffer is refilled while the other remains queued. Capture also
queues both buffers before starting. Missing completion events time out after
three seconds. Queue failures, invalid capture sizes, underruns, overruns, and
cleanup failures are reported. Exception cleanup resets the device before
releasing prepared buffers. These waits do not protect against an operating
system driver blocking inside a synchronous WinMM API call.

A test-only WinMM surface compiles the Windows adapter on Linux and verifies
sample ordering, prequeueing, failure paths, bounded waits, and resource cleanup.
It cannot establish Windows ABI compatibility or physical device behavior; a
native Windows build and device run remain necessary. The lifecycle follows
Microsoft's [playback pause/restart guidance](https://learn.microsoft.com/en-us/windows/win32/multimedia/stopping-pausing-and-restarting-playback)
and [prepared-buffer contract](https://learn.microsoft.com/en-us/previous-versions/dd743868%28v%3Dvs.85%29).

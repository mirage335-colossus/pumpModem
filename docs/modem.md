# Audio modem reference

The implemented physical layer is differential quadrature phase-shift keying
(DQPSK) with integrate-and-dump reception, optional chip spreading, known-training
acquisition, and bounded in-memory diagnostics. It is a working reference modem,
not an implementation of the requested near-capacity adaptive high-order modem.
It does not claim performance close to Shannon capacity, calibrated radio
sensitivity, a particular occupied-bandwidth mask, or low probability of intercept.

## Waveform and rates

The default is mono 48 kHz audio, a 1,500 Hz carrier, and a nominal 1,200 Hz
bandwidth setting. Nominal chip rate is bandwidth / 2. Samples per chip are
rounded to the nearest multiple of four; the actual chip rate is sample rate /
samples per chip. Each QPSK symbol spans `spreading_factor` chips and carries two
bits. The actual bit rate is therefore twice chip rate / spreading factor.
At defaults this is 1,200 bit/s before training, framing, MAC, and FEC overhead.

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

## Spreading and independent streams

With a spreading factor above one, an unencrypted fixed balanced chip pattern
`++-+--+-` repeats within each data symbol. With `scramble` enabled, chip signs
come from the project's AES-256-CTR crypto abstraction, using its Scrambler
purpose. With `dsss` enabled, an independently seeded Dsss purpose stream supplies
another sign sequence. The two signs multiply; despreading occurs before symbol
integration and differential decisions.

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
not expose soft bits or perform a live provisional-text update loop.

## Simulation and WAV

`simulate` adds zero-mean white Gaussian noise at a specified ratio to the input
waveform's measured sample power. The seed makes repeated runs deterministic
within the same C++ library implementation. Noise is present during the specified
leading delay as well. An optional fixed frequency shift uses an FFT analytic
signal; it is a channel test, not automatic radio Doppler tracking. There are no
thermal-noise, path-loss, transmit-power, or antenna claims in this model.

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

The tests exercise binary loopback, arbitrary sample delay, Gaussian noise,
static frequency offset, simultaneous independent seeded spreading streams,
ciphertext-like training, the 24 kHz bandwidth preset, pure-noise and wrong-preamble
rejection, WAV quantization and malformed inputs, finite-value and memory limits,
and exact-length three-bit status. They use explicit checks that remain active
in release builds. No physical soundcard or radio-channel performance is implied
by these deterministic software tests.

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

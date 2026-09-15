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

The default CLI/internal configuration is mono 6,000 Hz PCM, a 1500 Hz carrier and
1,200 Hz nominal rate parameter (`Config::bandwidth_hz`). For this parameter `B`
from 1 Hz through 30 MHz, the legacy carrier recommendation is
`max(1500, 0.75B)`. With a selected carrier `fc`, automatic planning chooses
`Fs = ceil(max(64, 4B, 4fc))`. The GUI defaults to `B=3600`, `fc=1500`, and
14,400 internal samples/s, and exposes both Rate and Carrier controls.
Narrow audio remains around a usable carrier rather than falling below 300 Hz.
The 6 kHz floor is needed for this real-PCM carrier representation; it does not
raise the nominal chip or symbol rate. The 30 MHz plan still uses 120 million
internal samples/second. Manual CLI carrier/sample-rate overrides remain available.
The range is a DSP configuration range; an SDR device backend is not implemented.
The modem's nominal chip rate is bandwidth / 2.
`symbol_seconds` is the bandwidth-derived duration (or explicit integration),
independent of the hardware clock. `symbol_sample_count` rounds that duration
up once to an internal PCM sample; exact airtime estimates include this rounding.
Shaped transmissions also include the two finite filter tails described below;
these add a fixed burst duration, without changing any payload-symbol duration.
The nominal gross bit rate is selected bits per symbol divided by nominal symbol
duration, before packet and training overhead. Audio conversion does not alter
these modem settings or rates.

### Automatic-pattern hardware settling

A nonempty automatic-pattern transmission starts with a hardware-settling
waveform lasting approximately two seconds, rounded to the nearest whole
sample-quantized payload-symbol duration, with half-symbol ties rounded up.
For symbol duration `T`, the prefix contains `floor(2 / T + 0.5)` intervals of
length `T`; it is absent when `T` exceeds four seconds. The first payload symbol
then follows at its normal duration, starting at payload stream position zero.
An empty payload emits no prefix.

The settling waveform uses independent circular Gaussian-derived I/Q noise,
refreshed once per chip, at the payload's mean transmit power.
Its radius is limited and normalized to keep peaks inside PCM headroom.
Private payload and prefix use the same update cadence and noise mapping.
Eligible pattern profiles apply the same pulse shaping continuously across the
prefix and payload. The prefix helps external gain control and muting settle before
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
it reserves no payload constellation points. Automatically timed hardware
output schedules the first payload symbol on a whole system-clock second;
playback starts earlier by the prefix and leading filter tail. The symbol's
timestamp denotes that payload boundary. Explicit-timestamp captures and
simulation retain deterministic timing, including their prefix offset.
The output device is opened before choosing the scheduled start. Unmeasured
device and output-buffer latency can still shift the physical audio boundary.
Airtime estimates include the prefix and filter tails without adding meaningful
payload bits. For shaped profiles the first payload position is
`pattern_pulse_padding_samples(config) + training_sample_count(config)`.

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
Both public and private patterns use circular I/Q chips with varying amplitude
and phase. The public pattern resets at a symbol boundary. Its short templates
can produce sparse constellation plots because repetition adds no new chip
values. In keyed pattern mode, each symbol selects the whole second at its
scheduled start and holds that epoch for its entire pattern. Symbols beginning
in one second consume successive counter positions; a symbol beginning in a
later second selects that newer epoch. Long symbols skip intervening seconds,
and crossing a second inside a pattern never changes that pattern's reference.
Stable purpose roots let the receiver regenerate later symbols without the
original message epoch. The second codeword mixes
the first with a nonconstant balanced mask, preserving distinguishability under
unknown carrier phase. A separate DSSS purpose remains independent. Pattern
and Data streams share the symbol-start epoch and subsecond position. Streams
are generated on demand with fixed-size caches. See
[the exact integer schedule](crypto.md#binary-pattern-chip-addressing).

| Mode | Behavior |
| --- | --- |
| `auto-keystream` | Automatic binary pattern integration with fresh keyed chips; falls back to public `auto-pattern` without a key. |
| `auto-pattern` | Automatic integration of two rare codewords: public without a key, fresh keyed fragments with encryption. |
| `auto-tone` | Automatic integration of two continuous tones. |
| `pattern-3`, `pattern-4`, `pattern-6`, `pattern-8`, `pattern-12`, `pattern-16` | Preserve the named duration and fresh keyed fragments. Only `pattern-16` can meet the automatic floor, with sufficient in-band SNR. |
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

### Pulse shaping

`Config::pulse_shaping` defaults to `true`. Pattern symbols lasting at least
16 complete nominal chip times use a root-raised-cosine (RRC) pulse with 25%
rolloff and a finite 16-chip span. Manual patterns shorter than this and all
tone modes retain rectangular pulses. Setting the flag to `false` restores the
previous waveform for comparison; both endpoints must match the flag and
profile. No public acquisition marker or waveform-negotiation field is added.

The chip rate and integration duration are unchanged. For a sample-quantized
chip rate `R`, the ideal support is `1.25 * R`; at nominal bandwidth 1,200 Hz,
`R = 600` chips/s and the target is about 750 Hz. Nominal bandwidth continues to
control chip-rate planning and the reported C/N0 conversion, rather than
claiming a measured occupied width. Continuous filtering joins the independent
settling chips and every payload symbol. Eight chip times of zero extension at
each burst edge emit the complete tails: 26.7 ms of fixed additional airtime
at 600 chips/s. No extra payload symbols, chip addresses or keystream bytes are
consumed. A partial final chip retains its original address and duration-weighted
energy. Constellation observers still report the logical input chips before
shaping; waveform and spectrum views show the actual shaped samples.

Overlapping pulses can exceed the original chip peak limit. A circular radial
limiter caps the analytic PCM radius at 0.992 after shaping, preserving rotational
symmetry without a fixed power backoff. In a reproducible private 65,536-chip
capture at 600 chips/s, limiting reduced power by 0.083 dB relative to its
unlimited linear waveform, with 0.109% mean-squared waveform error relative to
signal energy. Hann-windowed spectra measured approximately 742–746 Hz at
26 dB below their spectral peak; spectral density beyond carrier ±400 Hz remained more
than 33 dB below that peak. These are software measurements for that capture,
not universal bounds or RF compliance measurements. The test suite checks
actual limited PCM sidelobes, mean power, crest headroom and chip addressing.

Receive templates use each candidate symbol's linear shaped contribution,
evaluated against the original independent sample/bin observations. Actual
template energies and Gram terms enter the fit; filtering does not manufacture
additional independent noise observations or a new source of timing confidence.
Unknown adjacent-symbol tails and the limiter residual remain model mismatch.
Pattern evidence alone still controls acquisition, iterative timing refinement,
continuation and burst boundaries; no prefix, decoded content, CRC or MAC
supplies clock-lock confidence.

The fast receiver's existing sample bins discard a small amount of shaped
signal energy. In the equal-C/N0 private +26 dB-Hz comparison, its pattern scores
were about 5% lower on average than with rectangular pulses. A marginal final
bit fell below the unchanged acceptance threshold and remained unconfirmed;
the raw-sample correlator recovered it. Thus unchanged payload rate does not
imply identical noisy decisions or zero confidence cost. The receiver retains
its conservative acceptance rules. See the [validation record](validation.md)
for the tested cases and limits.

Regular chip timing can remain observable through periodic second-order
statistics even though the chips and radial limiter are circularly symmetric.
The smoother spectrum reduces distant sidelobes; it does not establish
indistinguishability from bandpass-filtered white noise or a measured adversary
observation time. Keyless energy and correlation detectors can still respond
to the signal. Encryption, HKDF purposes, CTR domains, byte mixing and fresh
absolute chip positions are unchanged by shaping.

There is no certified occupied-bandwidth mask. Eligible shaped patterns must
fit their ideal RRC support above DC and below the internal Nyquist frequency;
short rectangular patterns and tone modes retain the conservative nominal
`carrier ± B/2` guard. Selecting such an unshaped mode may require raising a
low carrier. Finite filter tails and limiter regrowth remain subject to the
spectral qualifications above. The 24 kHz GUI preset uses a 96 kHz
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

The CLI defaults to nominal rate 1,200 Hz and TX target C/N0 of 40 dB-Hz. The GUI
defaults to Rate 3,600 Hz, Carrier 1,500 Hz and TX target C/N0 of 80 dB-Hz, and
offers an 18 kHz rate preset. Its default shaped spectrum ideally spans
375–2,625 Hz, including rolloff, for ordinary audio transfer between computers.
This changes carrier placement and chip timing, not the keystream purposes,
encryption, pulse shape or pattern-evidence synchronization rules. The selected
rate, carrier and pattern/tone mode are fixed during receive
search; encryption normally selects `auto-keystream`, with `auto-pattern`
otherwise. The **RX targets (dB-Hz)** field and CLI `--receive-targets` accept
a comma-separated list. The GUI RX list starts at `80`; changing TX SNR to a
valid value replaces it with that single matching target. It can then be edited
independently. The CLI receive list defaults to `40`.

Entries are trimmed and deduplicated. An empty, malformed,
nonfinite, out-of-range (outside -200..200 dB-Hz), over-16-entry or over-512-byte
list resets entirely to `40`. The GUI permits partial editing, then normalizes
after 750 ms of inactivity. RX targets never alter the scalar TX target.

The receiver resolves only this list, using the selected rate, carrier, clock
and mode before selecting the pattern length and checking its confidence floor;
identical sample-quantized waveform profiles are searched once. Manual CLI
`--spreading`, `--scramble`, `--dsss`, `--sample-rate` or `--carrier` selects
explicit pattern configuration and cannot be combined with automatic planning
or `--receive-targets`. Simulator `--snr` describes sample-power noise and does
not choose the transmitted profile.

`tuning::resolve` uses binary pattern symbols (`constellation_bits = 1`). It
retains the modeled integrated-energy target of 18 dB and selects the smallest
supported integration meeting both that target and an SNR-dependent chip floor:

| Modeled SNR in the selected bandwidth | Minimum pattern chips | Gross rate at 12 kHz |
| --- | ---: | ---: |
| At least 30 dB | 16 | 375 bit/s |
| At least 24 dB, below 30 dB | 32 | 187.5 bit/s |
| Below 24 dB | 64 | At most 93.75 bit/s |

These gates require high SNR relative to bandwidth, not simply a high C/N0
number. A shortened profile must contain only whole sample-quantized chips;
otherwise selection tries the next length up to the previous 64-chip floor.
This prevents a new periodic short-chip hold at fractional bandwidths or custom
clocks. The receive-profile bank checks its preserved actual clock as well.
Private profiles with compact orthogonal receive bins reserve at least 32 chips
to preserve boundary-bit recovery; the recommended 12 kHz profile uses the exact
sample fit and remains eligible for 16 chips. Other profiles outside the bounded
256-sample exact-fit range retain 64 chips.
Automatic tone modes retain the 64-chip floor. Automatic factors are
16 through 16,384, doubling at each step; longer integration uses an explicit
duration. Forced modes preserve the requested length and report an unsupported
target when the applicable chip floor or energy model is unmet.

```text
required_symbol_seconds = 10^((18 - C/N0_dBHz) / 10)
estimated_Es/N0_dB       = C/N0_dBHz + 10 log10(symbol_seconds)
bit_rate                = 1 / symbol_seconds
```

C/N0 uses noise power in a 1 Hz reference bandwidth. For bandwidth B,
`SNR_B = C/N0 - 10 log10(B)`. The 18 dB target and adaptive chip floors are
engineering model choices, not an empirically calibrated false-alarm or missed-
detection specification. Search multiplicity, correlated bins, channel model
and adaptive timing selection must be included in future calibration. Long
integration alone does not establish tolerance of clock drift or interference.

At 12 kHz and 80 dB-Hz, the selected 16-chip pattern improves gross throughput
fourfold over the previous fixed floor. Symbol and chain admission thresholds,
keystream generation and the transmitted chip distribution are unchanged.
Short private symbols with nonorthogonal carrier bins now use the same exact
real-PCM fit as short public symbols, retaining the cap of four real evidence
dimensions per chip. Orthogonal private bins retain their compact search. This
avoids losing short-pattern evidence by integrating across chip boundaries.
Patterns below 16 chips remain manual: some pass clean PCM yet lose an endpoint
under the sampled clock/phase channel. See [throughput limits and validation](throughput.md).
This is still one bit per codeword; multi-kilobit bulk transfer needs additional
validated waveform work. Peers using automatic tuning must use matching plans;
an older fixed-64-chip automatic profile is not the new high-SNR profile.

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
symbols can contribute to a chain. The low-level default ends the contiguous
recovered span at a failed symbol. Established timing continues across
missing symbols until at least two successive symbols have failed and their
combined duration exceeds six seconds. Thus two missing hours-long symbols
end a message; short symbols retain synchronization through a gap of up to
six seconds. A sufficiently confident surviving symbol can start a new span
without earlier symbols having decoded. Later seconds remain independently
searchable after an old message ends.
Retained weak symbols can be admitted by their combined evidence. If that
combined evidence remains insufficient when a confident symbol arrives, the
receiver preserves the confirmed prefix and starts a new span at that symbol.
No supplied bit count, packet length, known text or checksum admits a burst.
Packet decoding and optional integrity checks use already admitted observations.

Live reception, capture decoding and transfer simulation enable
`PatternSearch::preserve_symbol_gaps`. Within the same admitted timing track,
unknown interior slots are retained until an independently confident symbol
confirms their extent, subject to the same gap timeout. They provide no bit
evidence, score or timing refinement, and cannot select an unresolved private
stream schedule. Unsupported weak tails become unknown slots rather than
borrowing a later symbol's confidence. Unconfirmed trailing slots are trimmed
at silence, timeout, capacity exhaustion or the end of a capture.

The internal `missing_pattern_bit` value keeps those positions distinct from
observed zeroes and ones without extra per-bit allocations. Transfer decoding
decrypts known spans at their original stream positions, excludes unknowns from
marker evidence, and fills the missing plaintext bits with zero for FEC. Only
a recognized leading marker and complete packet integrity permit a gap-filled
packet to validate. Such bits cannot become dictionary text. `Received::raw_bits`
contains diagnostic zero placeholders; `Received::missing_symbols` counts them.
This preserves interior byte alignment; it cannot infer missing final symbols
without a later confident endpoint, or repair an actual discontinuity in the
capture clock. The transmitted format and symbol acceptance thresholds are unchanged.

Before retaining a new gap, the transfer layer checks whether the confirmed
prefix is already a complete packet. A bounded marker/header probe rejects
obvious incomplete prefixes; full SHA-256 or MAC validation is required before
closing the packet buffer. This keeps successive complete packets separate
even on the same timing grid, without changing the acquired clock or giving
unobserved bits confidence. Raw streams have no packet integrity or explicit
endpoint, so their gap timeout determines whether same-grid spans are joined.
During a pending gap the provisional view marks the confirmed observed span
complete, allowing prompt raw presentation. A later recovered extension can
replace that view, and a validated packet upgrades any earlier short-text
interpretation on the same signal.

The default frequency bank contains five offsets at 0, ±1/(4T), ±1/(2T),
where T is symbol duration. The API permits an explicit bounded offset bank.
Keyed search tries a finite set of epochs and initial stream positions. Where
symbol duration does not divide one second, its subsecond phase hypotheses
follow the finite `gcd(symbol_samples, sample_rate)` lattice. Phases yielding
the same symbol stream address share a template; confident symbols narrow the
remaining interval. Weak noise does not select a different phase. The search
transmits no epoch, symbol index or chip-block index. Timing resolution and
coverage depend on the allocated transform. This is not a whole-band search,
an arbitrary Doppler tracker or a guarantee of long-duration clock recovery.

Ordinary FFT acquisition requires workspace proportional to a few longest
symbol windows. When that allocation exceeds the budget and a system-clock
start hint is available, `PatternCorrelator` instead maintains bounded running
fits over the entire requested timing/frequency/rate window. Half-chip timing
coverage is checked before allocation; unaffordable coverage is explicitly
rejected. No full-symbol waveform or keystream array is required by this fallback.

Live keyed profiles whose symbols last at least 60 seconds select
`compact_clock_search` directly, even when a large FFT would fit. This retains
the configured timing, carrier-frequency, clock-rate and subsecond-phase
coverage. It uses 32-sample projection blocks and retains at most 32 diagnostic
candidate records and 64 constellation points. Those limits trim diagnostic
history; they do not remove running correlation hypotheses or lower confidence
thresholds. Live admission also reserves 8 KiB per compact receiver for bounded
control and diagnostic growth, in addition to its measured allocation.
Public profiles retain continuous FFT discovery when it fits, since they share
one repeating code rather than needing a separate receiver for every epoch.

Tests cover a four-hour symbol prefix with bounded storage, noise rejection,
and actual short-signal PCM recovery through the fallback. These checks establish
bounded state and exercised decoding, not a completed four-hour weak-signal
reception experiment. The default six-second epoch search supplies ±7 seconds
of start uncertainty. This is uncertainty in alignment: each admitted fit can
accumulate over the entire hours-long symbol. Completed low-confidence results
are discarded; running fits contain only compact sums. Their count, including
distinct phase-address groups, determines search storage. CPU cost also grows
with timing and epoch hypotheses and is not
established as real time for a large bank. Multi-hour operation requires a finite
clock window, adequate DSP budget and sufficient oscillator stability.

An unconfirmed live epoch retires after one symbol duration plus prefix and
clock-window allowance. A confirmed receiver retires after the permitted
failed-symbol gap and timing allowance; old noise epochs are not retained
indefinitely. Idle public fallback windows are also rearmed, while ordinary
FFT discovery remains continuous. This rearming does not provide complete
coverage of arbitrary start times between windows.

Compact storage is a per-receiver bound. The aggregate bank can retain many
possible start seconds while a long symbol is still being observed, and it
also scales with selected keys and profiles. Admission still reports incomplete
coverage when the configured budget is exhausted. A four-hour duration alone
neither requires a four-hour waveform buffer nor guarantees that every bank
configuration fits or runs in real time.

On the tested 64-bit build, a compact private profile at 6,000 samples/s,
1,500 Hz carrier and 1 Hz nominal bandwidth uses two-second chips. Its
±7-second start window has 15 half-chip origins, five default frequency
offsets and one zero-ppm clock rate: 75 timing/frequency hypotheses per epoch.

| Symbol duration | Measured streaming receiver | Live admission per epoch, including control reserve |
| --- | ---: | ---: |
| 4 hours, one subsecond phase | 44,480 bytes | 53,360 bytes |
| 4 hours + 0.3 seconds, phase search enabled | 52,880 bytes | 61,760 bytes |

About 14,430 simultaneous unconfirmed start epochs would therefore account for
approximately 734 MiB or 850 MiB respectively per key/profile, before global
audio, plots and other DSP reservations. These are bank-size estimates from
measured per-receiver allocations, not measurements of a four-hour hardware
run. Wider bands, additional rates or keys, fractional symbol phases and
available RAM change admission; oscillator coherence and CPU throughput still
require validation on the intended hardware.

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
at the storage needed for 60 frames at the selected signal rate. Each captured
constellation covers at least 1/60 second of signal time; waveform and spectrum
previews retain their fixed sizes. The reservation includes the prepared result's
diagnostics and caption, plus the frames' plots, points and preview text. Smaller
budgets reduce the frame count while retaining each frame's complete measured
constellation. This bounded workspace is independent
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

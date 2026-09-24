# Pattern / scrambler constellation

Automatic tuning uses a binary pattern alphabet: each complete pattern carries
one meaningful bit. The two legal waveforms have different internal phase
transitions. They are not merely opposite absolute phases of the same waveform,
so their identity does not require a preceding payload symbol or preamble.
Raw `001` occupies exactly three payload pattern symbols, with no transmitted
byte padding, header, checksum, or correction bits. A separate hardware-settling
prefix can precede those symbols; reception does not depend on receiving it.

## Hardware-settling prefix

For a nonempty transmission, the transmitter first emits approximately five
seconds of settling waveform, rounded to the nearest whole payload-symbol
duration, with half-symbol ties rounded up. If `T` is the actual sample-quantized
symbol duration, its count is `floor(5 / T + 0.5)` and its duration is that count
multiplied by `T`. Thus 0.1-second symbols get 50 settling intervals, four-second
symbols get one, ten-second symbols get one, and symbols longer than ten seconds
get none. Empty payloads emit nothing.

This prefix brings external automatic gain control, audio muting and similar
hardware toward their transmit operating level before the first payload symbol.
It uses an independent noise-like sign stream at the normal amplitude and chip
rate, rather than sending legal payload codewords. It is neither a training
sequence nor a synchronization marker, and the receiver never requires or fits
it to establish lock. A missing or distorted prefix does not change the payload
format or the evidence needed to accept a symbol.

The prefix stream has its own derivation domain, with a private seed when
keyed pattern or DSSS spreading is active and a public seed otherwise. Its samples are
reproducible for previews without reusing payload stream positions. The epoch
is fixed at transmission start; payload Data and pattern positions still begin
at zero. System-clock hypotheses account for the prefix's elapsed duration
when predicting the first payload symbol. Estimates and transmission layouts
include its airtime separately from meaningful bits and payload symbols.

## Pattern waveform

Auto Pattern and the forced pattern lengths use keyed, advancing pattern
fragments whenever encryption is selected. This lets pattern evidence identify
the receive key and epoch without packet validation; a public template with
only an encrypted Data stream could not distinguish those hypotheses. The
Auto keystream choice has the same keyed behavior and falls back to public
patterns without a key. Explicit tone modes retain their separate waveform.

`PatternCode` generates a public pseudorandom sign row for ordinary patterns or
a private Scrambler stream for keyed patterns. Bit zero uses that row; bit one
multiplies it by the balanced mask `+--+++--`, repeated over the symbol. The mask
changes internal differential signs and is neither a global phase reversal nor
a simple alternating carrier shift. A one-chip profile remains ambiguous under
unknown carrier phase; permitting a manually selected short profile is not a
claim of adequate detection confidence.

The public row restarts at each symbol. Private Scrambler and independent DSSS
streams instead consume fresh absolute chip positions throughout and between
symbols. Integrations longer than 16,384 chips do not repeat a private template.
The generator caches only a fixed block of each stream and supports random
access for candidate clock positions. A partial final chip consumes its stream
position before the next symbol begins.

Both bit values have the same constant transmitted amplitude. Their information
is in the complete pattern, not a separately distinguishable APSK coefficient.
The sample clock determines quantized chip durations; the configured symbol
duration determines the exact number of samples sent. No extra symbol is added
to fill a byte.

Tone mode uses two tones at the nominal carrier plus or minus one quarter of
the chip rate. Their labels require an agreed carrier and receiver frequency
uncertainty smaller than one quarter of the chip rate. Allowing unrestricted
frequency search would make one bit's tone indistinguishable from the other's
frequency offset. Tone evidence can accumulate with duration, but does not
provide a pseudorandom timing signature.

## Acquisition evidence

`PatternReceiver` scores sampled waveforms against both legal patterns. A fit
allows unknown common complex phase and amplitude; no hard chip decision or
clean APSK cloud is required before pattern scoring. The live chip constellation
therefore remains a diagnostic view of measured differential observations.
Its radial/phase residual does not control pattern acquisition.

For independent circular Gaussian complex observations, let `rho²` be the
normalized energy in the fitted pattern direction and `N` the number of
observations. The detector's noise-tail score is:

```text
score = -(N - 1) log(1 - rho²)
```

Larger scores mean a less likely noise-only projection under that model. The
implementation compares both bit alternatives and accounts for repeated search
trials in its admission threshold. The winning pattern's margin over the other
pattern is separate from its evidence against noise. A high-scoring individual
symbol can start a burst; weaker retained symbols can accumulate evidence along
a compatible chain. Additional weak symbols remain pending until their own
continuation evidence is sufficient; earlier strong symbols do not justify an
arbitrary noise tail. Perfect patterns are unnecessary.

These scores are model-based detection evidence, not authentication, a posterior
probability that text is correct, or calibrated sensitivity for an audio/radio
front end. Correlated interference, oscillator drift, filtering and real PCM
quadrature effects require validation against their actual noise distributions.
A useful-looking constellation or a successful simulation cannot substitute
for those measurements.

Acquisition uses a bounded FFT timing search and an explicit finite carrier
bank, followed by local timing comparisons for later symbols. Pattern evidence
alone starts and ends burst acquisition. Packet parsing, integrity checks and
optional FEC operate after that detection decision. A fade can still split a
burst; silence-based delimiting does not establish that a transmitter intended
to stop.

The current default frequency bank has five offsets spaced by `1/(4T)`, where
`T` is a symbol's duration. This is finite frequency coverage, not an unlimited
carrier or continuous clock tracking loop. The receiver retains a few symbols
of baseband history, bounded candidate records and its correlation workspace.
The configured DSP memory is a ceiling. The FFT workspace grows with symbol
length. When that workspace would exceed the ceiling and a system-clock start
window is supplied, reception uses the streaming correlator described below.
Without affordable, explicit search coverage, construction reports a resource
error instead of silently reducing timing resolution.

## Streaming long-symbol correlation

`PatternSearch.start_offset_seconds` predicts the first payload symbol's
start relative to the first captured sample. `start_uncertainty_seconds`
defines its uncertainty window; `clock_errors_ppm` and
`frequency_offsets_hz` define finite clock-rate and frequency hypotheses.
Clock-rate hypotheses change the chip and symbol schedule; any accompanying
carrier-frequency shift must also lie in the supplied frequency bank.
The receiver searches the entire supplied start window at half-chip spacing.
It budgets the full time × frequency × clock-rate combination before allocation
and rejects a window it cannot cover. A negative start offset also identifies
which private stream symbols precede the surviving capture.

`PatternCorrelator` generates chip references on demand and retains sufficient
statistics for each hypothesis. Fixed blocks of 128 real PCM samples share
carrier projection work. Each fit retains the two carrier projections, their
exact Gram matrix and observed energy, not a symbol-sized waveform or FFT.
Its state therefore depends on the requested hypothesis count and retained
bits rather than a symbol's duration. No symbol boundary, bit count, phase,
gain or transmitted symbol value is handed to the detector by the transmitter.

For `N` independent real Gaussian samples and two nonsingular fitted carrier
bases, this path uses the real-sample noise-tail score
`-(N-2)/2 * log(1-rho²)`. The Gram matrix accounts for quadratures that are not
orthogonal over a finite observation. This differs from the complex-bin score
above because the observation degrees of freedom differ.

The fallback supports long integrations within an explicit clock window. It
does not infer unsearched frequency drift, clock rates or unbounded start
times, and it does not establish that a physical oscillator will remain
coherent for hours. Tests cover constant storage during a four-hour symbol,
sampled short bursts, a finite clock-rate bank and cropped keyed captures;
they do not claim a measured multi-hour radio link budget.

At 1,200 Hz bandwidth, 6,000 samples/s, ten samples/chip and the default five
frequencies, the four-hour-symbol constructor retains the following state on
the tested 64-bit build. These amounts are **per epoch/profile**; the live
receiver also budgets its wrappers, audio, plots and other admitted epochs.

| Start uncertainty | One clock rate | Three rates: -100, 0, +100 ppm |
| --- | ---: | ---: |
| ±1 second | 3.65 MiB | 13.22 MiB |
| ±2 seconds | 7.13 MiB | 26.27 MiB |
| ±7 seconds | 24.53 MiB | 91.50 MiB |

Transfer's default six-second epoch search supplies a ±7-second start window.
Thus an 8 MiB direct receiver can hold the ±2-second, one-rate example, but not
the default ±7-second coverage. The live receiver reserves other DSP storage
and caps each receiver's share, so its total configured ceiling must be larger
still. Adding memory does not make an unaffordable CPU search real time. A
brief full-active-bank benchmark on the development host needed about 34 ms
for a 21.3 ms PCM block at ±2 seconds with one rate, and 111 ms at ±7 seconds.
The scalar fallback provides bounded offline/streaming state; sustained live
operation requires measured computational capacity for its admitted bank.

## Modem flow inspection

The **Modem flow** tab shows the two configured binary codeword rows and their
distance, with representative public seeds for keyed illustrations. It does
not reveal the actual private epoch stream. Long symbols display a bounded
prefix of at most 16,384 chips and label truncation; the illustrated-prefix
distance must not be read as a measured full-symbol confidence value.

Pattern distances weight each displayed chip by its actual sample duration.
The one-chip-offset comparison illustrates whether a shifted row can be fit by
an arbitrary common complex scalar. Tone rows show sampled complex tone values;
their display is an illustration of continuous tone evolution, not extra
transmitted chip labels. The Console's live measured chip constellation is
separate from this static alphabet view.

## Manual legacy APSK

Explicit legacy configurations with `pattern_symbols == false` retain the
previous 2–6-bit APSK alphabet, repeated sign period, five-second training and
packet-oriented acquisition. Their static inspection still uses the shared-row
model `s[k] = a * p[k]`. In that model all legal waveforms are coefficients in
one complex direction, and the complete-template distance is `T * |a-b|²`.
Those properties and the legacy APSK residual gate do not describe the new
binary pattern receiver.

# Pattern / scrambler constellation

Automatic tuning uses a binary pattern alphabet: each complete pattern carries
one meaningful bit. The two legal waveforms have different internal phase
transitions. They are not merely opposite absolute phases of the same waveform,
so their identity does not require a preceding payload symbol or preamble.
Raw `001` occupies exactly three payload pattern symbols, with no transmitted
byte padding, header, checksum, or correction bits. A separate hardware-settling
prefix can precede those symbols; reception does not depend on receiving it.

## Hardware-settling prefix

A nonempty transmission starts with approximately five seconds of settling,
rounded to the nearest whole sample-quantized payload-symbol duration, with
half-symbol ties upward. If a symbol lasts `T` seconds, the prefix lasts
`floor(5/T + 0.5)*T`. It is absent when `T > 10`. Empty payloads emit nothing.

The prefix helps external gain control and muting settle. Its samples use the
same bounded circular Gaussian-derived I/Q mapping, expected mean power and
once-per-chip updates as private payload patterns. Public prefix bytes are
XORed with Data and every enabled Scrambler/DSSS byte stream before mapping.
All use the existing purpose keys and epoch with the separate `preamble` CTR
counter pad. The prefix does not consume payload stream positions. It is generated
independently of both payload codewords and carries no acquisition marker. The receiver must
acquire surviving payload patterns even when settling is missing or obscured.

Tone modes use an unencrypted settling prefix; selecting tone clears the key
and disables private spreading. They are not LPI modes. No separate legacy
APSK preamble or final-symbol padding remains.

## Pattern waveform

Every complete pattern carries one bit. Chip duration is
`ceil(2*sample_rate/bandwidth)` samples; the final chip of a symbol can be
partial. Symbol `j` uses absolute chip positions starting at `j*C`, where
`C = ceil(symbol_samples/chip_samples)`. Partial chips consume a full position.
Epoch and chip index are local hypotheses and are never transmitted fields.

Public unkeyed patterns use a deterministic circular I/Q noise row that restarts
each symbol. Its amplitude and phase both vary; a binary pattern alphabet
means two complete codewords, not two permitted chip values. A short public
codeword still has few distinct points: repeated transmissions reuse its chips,
so the scatter plot contains at most twice its chip count across both bit values.
Selected keys always enable private Scrambler waveforms on the
transfer path. A private row consumes eight bytes per absolute chip, mixing
Scrambler and enabled DSSS bytes before mapping to circular noise. Both its
amplitude and phase depend on the private streams. The radius is capped to
stay inside PCM headroom, with unit expected complex power before scaling.
[Cryptographic addressing](crypto.md#binary-pattern-chip-addressing) defines
the exact byte and amplitude/phase convention.

The two bit alternatives mix this row with distinct public internal-transition
masks. They are neither a global phase reversal nor an alternating carrier
shift, so unknown common phase does not erase their distinction. Only one
alternative is transmitted at each private position. Fresh secret positions
across symbols prevent the paired waveform cancellation that reuse would allow.

The previous real +/-1 mapping left a fixed carrier after squaring PCM. That
signature was a mapping defect, not a necessary property of pattern search.
Circular I/Q noise removes it and avoids a fixed payload envelope. Public rows
remain recognizable through repetition and provide no encryption or LPI. The
new prefix and private payload also share noise statistics and chip cadence.
Finite bandwidth, rectangular chip holds, capped amplitudes and burst edges
remain observable physical properties; there is no claim of absolute
indistinguishability from ambient noise.

Tone waveforms use opposite quarter-turn progression per chip, producing
carrier offsets of +/- chip_rate/4. Distinguishing labels requires a narrower
carrier-uncertainty range. Tone disables encryption, Scrambler and DSSS in the
GUI, CLI and transfer APIs. Public tone experiments make no LPI claim.

## Acquisition evidence

`PatternReceiver` scores sampled waveforms against both legal patterns. A fit
allows unknown common complex phase and amplitude; no hard chip decision or
clean APSK cloud is required before pattern scoring. The live constellation
shows measured receiver input I/Q or a bounded history of actual transmitted
chip I/Q, with the source labeled. Its geometry does not control pattern acquisition.

For independent circular Gaussian complex observations, let `rho²` be the
normalized energy in the fitted pattern direction (using the actual sum of
squared template magnitudes, including partial observations) and `N` the number of
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

Public symbols and private symbols with nonorthogonal carrier bins, of at most
256 samples, use individual PCM samples in the FFT search,
so coarse chip-bin boundaries cannot erase a short final bit. This path fits
the exact two-real-basis carrier Gram matrix. Its real-sample score
`-(N-2)/2 * log(1-rho²)` caps `N` at four real dimensions per chip to retain
the previous half-chip evidence scale; finer sampling does not create extra
independent chip evidence. Longer public symbols retain chip-bin integration;
when their sample/chip alignment requires one-sample bins, they also use this
fit. Longer private symbols retain their previous scoring and timing path.
Orthogonal private bins retain their compact search; automatic profiles reserve
at least 32 chips on that path to preserve boundary-bit recovery.
The two pattern candidates, search penalties and chain thresholds are unchanged.
The reported start can retain a nearby
earlier candidate admitted before the next FFT block arrives.

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
benchmark before the circular private-waveform change needed about 34 ms
for a 21.3 ms PCM block at ±2 seconds with one rate, and 111 ms at ±7 seconds.
Those historical timings do not measure the current private templates. The scalar
fallback provides bounded offline/streaming state; sustained live operation
requires current measurements for its admitted bank.

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

## Compatibility

High-SNR automatic plans now select 16 or 32 chips where the in-band SNR,
whole-chip alignment and receiver confidence gates permit, with 64 chips or
longer retained otherwise. Both peers must
derive matching plans. The private chip mapping, purpose keys and epoch/chip
address convention are unchanged by this tuning update; shortening a symbol
does not reuse its absolute private chip positions. See [throughput](throughput.md).

The previous APSK transmitter, fixed training, repeating sign-template path,
and aligned symbol receiver have been removed. The private circular waveform
also changes the previous keyed pattern format; public circular chips replace
the previous unkeyed real-sign format. Peers must use the same current
waveform; the receiver performs no compatibility negotiation or fallback.

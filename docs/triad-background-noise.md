# Cryptographic transmit background noise

The shared, default-off background-noise stage adds noise at the final audio
sample rate, after resampling, Helper composition and signal gain. It spans the
represented DAC Nyquist band. It is additional transmitted energy, separate from
receiver thermal noise and message encryption. The shared implementation is
`audio::PlaybackOutput` in `include/datapump/audio_output.hpp` and
`src/audio_output.cpp`, with `background_noise` as its cryptographic source.
Use this stage instead of implementing another generator in a backend.

The [current Robust status](robust-current.md) describes the native GUI
transmitter and receiver that use the shared audio output component. Historical
adapter and CLI recipes are not current entry points.

## Randomness and state

Each enabled generator obtains a fresh private 256-bit key from OpenSSL's
OS-seeded private RNG, independent of configured DATA and Helper keys.
AES-256-CTR encrypts zero blocks; counter exhaustion and RNG/EVP errors fail
the operation. Temporary key and stream buffers are cleansed and never written
to receipts. Off requests no entropy. A simulation seed does not reproduce new
background noise: preserve the sampled output and its hash when an observation
must be replayed. Do not add transmitter noise to a replay of an existing
receiver capture. This is separate from the transmit-noise DATA key policy in
[Robust architecture](robust-current.md#transmit-key-policy).

One output object owns one transmission, including its internal zero-valued
samples, gaps and drained resampler output. Preroll and device preparation are
not active source samples. Neither buffer boundaries nor a zero signal amplitude
restart the stream. Stereo routing duplicates the mono noise; it does not create
an independent I/Q transmitter.

## Units and headroom

The canonical level is real-DAC mean square `v`, normalized to full-scale peak 1:

| Control | Definition |
| --- | --- |
| Effective bits B | `v = 2^(-2B)/3`, ideal quantization variance for step `2/2^B` |
| Obscured bits K | Effective bits `16-K` |
| Suppression D dB | `v = 0.5 * 10^(-D/10)`, below a full-scale sine |
| Added watts Pn | `v = reference_mean_square * Pn/reference_watts` |

For relative power the fixed reference is the strongest complete active signal:
total DATA across three carriers or the complete effective Helper, whichever is
larger. It is not one pilot, a composite peak, or a noisy received measurement.
Reference RMS includes the selected transmit gain in final normalized DAC units.
For 1 mW added to a 40 W reference, relative power is -46.0206 dB and relative
RMS is 0.005. Analog filtering and radio gain still determine radiated power.
Ten ideal bits correspond to 61.97 dB below a full-scale sine; sixteen to 98.09
dB. These conventions are not measured hardware ENOB.

Float noise is bounded uniform noise. Integer output mixes adjacent symmetric
integer-uniform supports to achieve the requested variance, including below one
LSB: support `[-m,m]` has variance `m(m+1)/3`. Quantized variance below `2^-32`
code-squared is rejected. Whiteness concerns second-order statistics, not a
requirement for a Gaussian distribution. Headroom is checked; there is no silent
clipping, limiter or automatic normalization. Off preserves the existing output.

## Bandwidth and evidence limits

A narrow complex-I/Q injection is a different experiment. For ideal analytic
conversion, real white noise of variance `v` at 48 kHz has complex PSD
`2v/48000`; an ideal 6 kHz complex passband retains variance `v/4`. Injecting
complex variance `v` directly at 6 kHz is therefore 6.02 dB stronger in-band.
Actual filters and time-varying propagation require their own energy accounting.
Noise added after a simulated channel is not transmitter noise propagated
through that channel. Preserve processing order and channel tails.

The [full-rate record](validation-data/triad-v3/2026-11-06-background-noise-fullrate/README.md)
preserves bandwidth/DDC checks; the [HF study](validation-data/triad-v3/2026-11-06-hf-background-target/README.md)
preserves receiver misses and CPU/preparation costs; the [earlier cable record](validation-data/triad-v3/2026-11-06-background-noise/README.md)
preserves route-specific power/PSD evidence. These dated results do not qualify
changed code, other hardware, a browser binary, or a complete Robust receiver.
Prior false-admission bounds need re-examination for colored transmitted noise.
Use [bounded abbreviated checks](triad-performance.md) for intensive new work.

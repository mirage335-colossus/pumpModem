# Cryptographic transmit background noise

The isolated three-carrier tools provide an optional, default-off, full-baseband
transmit noise floor. This is additional transmitted energy, separate from
receiver thermal noise and from message encryption. The ordinary receiver and
GUI are not integrated by this change.

Use the shared implementation in `include/datapump/background_noise.hpp` and
`src/background_noise.cpp`; do not substitute a seeded simulation PRNG. Each
enabled generator/transmission obtains a new private 256-bit key from OpenSSL's
OS-seeded private RNG. AES-256-CTR encrypts zeros in buffered blocks. Its initial
counter contains system-clock time and starts its block counter at zero, with
an exhaustion guard. Key material and buffered stream state are temporary,
cleansed on destruction, and never written to receipts. RNG/EVP errors fail the
operation. Off does not request entropy. OpenSSL dispatches to accelerated AES
implementations where supported; there is no GPU dependency.

A simulation seed still reproduces the selected physical channel, but cannot
reproduce newly generated background noise. Retain an observation/output hash
and, when needed, the sampled output for analysis replay. Every **new**
transmission receives fresh noise, including when its clean waveform is cached.
Replaying a recorded receiver capture is not a new transmission and must not add
transmitter noise to those ADC samples.

## Units and reference power

One level control may be selected. The canonical quantity is added real-DAC
mean square (v), for samples normalized to full-scale peak 1:

| Control | Definition |
| --- | --- |
| Effective bits (B), 0–16 | (v=2^{-2B}/3), the ideal quantization variance \(\Delta^2/12\), with \(\Delta=2/2^B\). |
| Obscured bits (K), 0–16 | Effective bits (16-K). |
| Suppression (D) dB | Positive dB **below a full-scale sine**, (v=0.5\,10^{-D/10}). |
| Noise watts (P_n) | (v=P_{\rm reference,samples} P_n/P_{\rm reference,watts}\). Both reference quantities are fixed for the transmission. |

The power reference is the strongest complete active signal: total DATA across
all three carriers or the complete effective Helper signal, whichever is larger.
It is not the instantaneous composite peak, a single pilot, or the sum of
Helper and DATA powers. Generic PCM tools require an explicit reference RMS
and watts; the Helper exporter derives the reference from its existing active
source power calibration. Do not estimate this reference from a faded, noisy
received capture. Supplied PCM reference RMS is after any explicit signal gain.

For 1 mW added to a 40 W reference, added power is −46.0206 dB and RMS amplitude
is 0.005 of reference RMS. With a 4 W reference these are −36.0206 dB and
0.015811. These are transmitter-equivalent relative powers; analog filtering,
radio bandwidth and gain affect actual radiated noise. No RF wattmeter
calibration is implied by a cable experiment.

Ten ideal bits correspond to 61.97 dB below a full-scale sine; sixteen ideal
bits correspond to 98.09 dB. A 90 dB setting is about 14.66 ideal bits. Practical
16-bit hardware can have a higher analog noise floor. These ideal conventions
do not claim measured hardware ENOB.

## Placement, bandwidth and headroom

In native `StreamingChannel`, noise is generated once per chronological source
sample after DATA/Helper composition and before transmitter oscillators and
propagation. It continues through source guards and burst gaps, ends at the
source endpoint, and its echoes/filter tails drain normally. It is not gated
again at reception. Disabling the transmitted source disables this transmitter
noise as well. Receiver-only noise controls remain independent.

The complex simulation uses the analytic-equivalent convention
\(E|n|^2=v\), half in each independent component. Full baseband means that
simulation's configured sample bandwidth, **not** an implicit 48 kHz DAC.
Subsequent interpolation, oscillator shifting and channel filtering can alter
the spectrum. Existing sampled interpolation and Nyquist-edge wrapping are
not a newly validated analog reconstruction filter. Keep DATA/Helper signal
capture bounds separate from the noise bandwidth.

For raw ALSA live tests, add noise at the final hardware-rate S16 boundary,
after audio conversion and all prepared waveform processing. Stereo playback
duplicates the same mono samples/noise, preserving the existing radio route.
No independent stereo or I/Q hardware claim is made. A cable waveform which
already contains a simulated HF channel receives this physical-DAC noise
**after** that simulated channel; this differs from transmitter noise propagated
through native HF simulation. Receipts must preserve that ordering distinction.
The duplex suite currently supports this feature with `--backend alsa-tools`;
its native audio-capture backend rejects the option because final hardware-rate
injection through that backend's possible resampler has not been implemented.
The earlier receiver false-admission bounds do not automatically cover this
additional, potentially colored transmitted-noise source.

Float noise is independent bounded uniform noise. Quantized output uses a
symmetric integer distribution calibrated to the requested variance, including
below one LSB. With integer-uniform support \([-m,m]\), variance is
\(m(m+1)/3\); mixing adjacent support sizes supplies intermediate variances.
This avoids a quiet 16-bit noise setting disappearing under truncation. Sparse
nonzero DAC codes at very low levels are expected. Whiteness is a second-order
spectral property; this noise is not required to be Gaussian.
Quantized variances below \(2^{-32}\) code-squared are rejected: their mixture
probability is too small for the declared numerical error budget. This is far
below the requested 16-bit/90 dB levels; it avoids claiming exact arbitrarily
small powers from finite random arithmetic.

Headroom is checked; there is no automatic normalization, limiter or silent
clipping. Use an explicit fixed signal gain or lower noise level when their
bounded sum does not fit. A zero-bit setting can occupy essentially the entire
DAC range and leave no useful headroom for a nonzero signal. Off retains the
previous waveform/quantizer exactly.

## Minimal commands and testing

Activate the existing research Python environment for the optional NumPy/SciPy
checks (the stream adapter tests also use Torch). Core CTest checks use C++ and
Python's standard library only.

```sh
./build.sh test triad --cli --jobs 2
# Optional tools in the same configured profile:
cmake --build build/dev-cli --target background_noise triad_long triad_helper_export --parallel 2

# Input is clean mono float32le at the actual DAC rate.
build/dev-cli/background_noise --input clean.f32 --output noisy.s16 \
  --format f32 --output-format s16 --sample-rate 48000 \
  --bits 10 --metadata noise.json

# Fixed strongest-signal reference: 40W corresponds to RMS0.1 in these samples.
build/dev-cli/background_noise --input clean.f32 --output noisy-mw.s16 \
  --format f32 --output-format s16 --noise-watts .001 \
  --reference-watts 40 --reference-rms .1 --metadata noise-mw.json

# Digital qualification only; fresh output directory, no audio device access.
python3 tools/triad_noise_qualification.py \
  --output local-evidence/noise-digital

# In the existing research Python environment (NumPy/SciPy; stream tests use Torch):
python3 -m unittest tests/test_triad_background_noise.py \
  tests/test_triad_live_alsa.py tests/test_triad_stream_run.py tests/test_triad_live_duplex.py
```

Native exporters and supported live adapters use the prefixed selectors
`--background-noise-bits`, `--background-noise-obscured-bits`,
`--background-noise-db`, and `--background-noise-watts`. The raw-ALSA and
continuous-stream adapters accept `--background-noise-tool` to select the built
native executable. Generic PCM watt settings also require
`--background-noise-reference-watts` and `--background-noise-reference-rms`.
Unsupported backends must reject a selected setting rather than ignore it.

The focused core and adapter checks, plus the short digital qualification, are
the routine tests for this feature. Do not repeat the historical weak-signal,
multi-day or GPU receiver campaigns merely to test noise generation. Physical
qualification is opt-in with `triad_noise_qualification.py --live --live-only`
and an explicit `--device hw:C,D`; it briefly owns that capture/playback route.

The [measured digital and cable qualification](validation-data/triad-v3/2026-11-06-background-noise/README.md)
records the source identities, power/PSD errors, CPU cost, test results and limits.

## Basis

OpenSSL documents its [private random generator](https://docs.openssl.org/3.0/man3/RAND_bytes/)
and [EVP cipher interface](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/).
Counter uniqueness is specified by [NIST SP800-38A](https://nvlpubs.nist.gov/nistpubs/legacy/sp/nistspecialpublication800-38a.pdf).
The ideal converter convention follows [Analog Devices AN-282](https://www.analog.com/media/en/technical-documentation/application-notes/an-282.pdf).
Implementation tests and measured output spectra supply the software evidence;
these references alone do not prove this implementation's noise-floor accuracy.

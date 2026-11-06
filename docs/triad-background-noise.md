# Cryptographic transmit background noise

The optional, default-off transmit noise floor is added at the **shared final-rate
audio output stage**, after resampling and Helper composition and before delivery
to any supported audio backend. It spans the represented DAC Nyquist band; it is
not limited to the modem's narrow complex I/Q bandwidth. This is additional
transmitted energy, separate from receiver thermal noise and message encryption.

Use `audio::PlaybackOutput` in `include/datapump/audio_output.hpp` and
`src/audio_output.cpp` for playback and offline PCM rendering. Its cryptographic
source is shared with `include/datapump/background_noise.hpp` and
`src/background_noise.cpp`; do not duplicate it in a backend or substitute a
seeded simulation PRNG. Each enabled generator/transmission obtains a new private
256-bit key from OpenSSL's
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
mean square \(v\), for samples normalized to full-scale peak 1:

| Control | Definition |
| --- | --- |
| Effective bits (B), 0–16 | \(v=2^{-2B}/3\), the ideal quantization variance \(\Delta^2/12\), with \(\Delta=2/2^B\). |
| Obscured bits (K), 0–16 | Effective bits \(16-K\). |
| Suppression (D) dB | Positive dB **below a full-scale sine**, \(v=0.5\,10^{-D/10}\). |
| Noise watts \(P_n\) | \(v=P_{\rm reference,samples} P_n/P_{\rm reference,watts}\). Both reference quantities are fixed for the transmission. |

The power reference is the strongest complete active signal: total DATA across
all three carriers or the complete effective Helper signal, whichever is larger.
It is not the instantaneous composite peak, a single pilot, or the sum of
Helper and DATA powers. Full-rate PCM tools and the production CLI require an
explicit reference RMS and watts for relative-power mode. The reference RMS is
in **final normalized DAC units, including the selected transmit gain**. The
output stage does not silently rescale that reference when applying gain. Do not
estimate it from a faded, noisy received capture. The isolated narrow-I/Q
diagnostic exporter can derive its separate source reference from its active
DATA/Helper power calibration; that is not a full-DAC reference.

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

One `audio::PlaybackOutput` belongs to one playback/transmission, not one buffer
or one open device. It adds noise after the final rate conversion, Helper/output
transform and signal gain. Native S16 packing and host/browser Float32 delivery
use the same stage. Enabled Float32 output passes unity gain to the provider so
that the existing provider gain cannot scale the signal and noise a second time.
Stereo routing duplicates the same mono noise on the selected output channels;
this does not introduce an independent stereo/I/Q transmitter.

The generator starts lazily on the first active source sample, even when that
sample is zero. Device calibration and scheduled preroll silence remain quiet
and consume no stream samples. Source-internal FHSS guards, Symbol Burst gaps
and drained resampler output remain part of the active transmission and receive
noise. Noise is not inferred from nonzero signal amplitude or restarted at
buffer boundaries. A successful completion report records the actual final
sample rate, PCM format, active sample count and measured noise moments, but no
key or random stream material.

The shared integration covers native ALSA/PulseAudio/PipeWire and Windows output,
and the host/browser audio provider. `audio::Options::background_noise` and
`live::Settings::background_noise` expose the setting programmatically. The
production CLI accepts it for `tx`/`status-tx --device` and transmitting `listen`
commands. There is no new GUI control. Provider contract tests and physical
hardware measurements are distinct evidence: native ALSA cable measurements do
not establish physical Windows or browser behavior, and mocked host-provider
checks do not constitute a newly built/tested WebAssembly binary. The local
WebAssembly SDK was unavailable during this integration.

For an offline full-audio simulation, use the `background_noise` PCM renderer:
resample from `--input-rate` to `--sample-rate`, then add noise at that output
rate. Apply subsequent transmitter reconstruction/filter and propagation models
to this complete noisy waveform, retaining their state and tails. For a cable
fixture that already contains a simulated HF channel, final hardware-DAC noise
is added **after** that simulated channel. Receipts must preserve this ordering;
it is not equivalent to transmitter noise propagated through the HF model. Both
`triad_live_duplex` capture backends (`native` and `alsa-tools`) support final-rate
noise; the native backend adds it during playback rather than pre-noising a file.

### Narrow-I/Q diagnostics are not the DAC feature

The earlier `StreamingChannel` noise injection remains only as the explicitly
named `diagnostic_iq_noise` / `--diagnostic-iq-noise-*` experiment. It injects
complex noise with \(E|n|^2=v\) over that simulation's sampled I/Q Nyquist band,
after DATA/Helper composition and before transmitter oscillators and propagation.
Its channel state and tails remain continuous. `triad_long` and
`triad_helper_export` reject the old `--background-noise-*` spelling to prevent
accidentally presenting this restricted-bandwidth diagnostic as full-DAC noise.
The production narrowband `live::Settings::simulation` path also rejects the
full-rate setting; use the offline full-audio renderer for that layer instead.

Equal total noise variance in these two representations does **not** imply
equal receiver noise density. For example, with an energy-preserving analytic
conversion, real white DAC noise of variance \(v\) at 48 kHz has complex PSD
\(2v/48000\). An ideal 6 kHz complex passband retains variance \(v/4\), whereas
directly injecting complex variance \(v\) at 6 kHz is 6.02 dB stronger in-band.
That comparison assumes ideal flat filtering; time-varying channels and actual
filter responses need their own propagation/energy calculation. Neither path
compensates analog filtering to make radiated noise spectrally flat. Earlier
receiver false-admission bounds do not automatically cover this added,
potentially colored transmitted-noise source.

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
./build.sh configure --cli --web-worker --build-dir build/dev-cli
./build.sh test triad --cli --web-worker --build-dir build/dev-cli --jobs 2
# --web-worker also makes the host-provider contract target available.
# Optional tools in the same configured profile:
cmake --build build/dev-cli --target background_noise triad_long triad_helper_export --parallel 2

# Clean mono float32le at 6 kHz; noise spans the final 48 kHz audio output.
build/dev-cli/background_noise --input clean-6k.f32 --output noisy-48k.s16 \
  --format f32 --output-format s16 --input-rate 6000 --sample-rate 48000 \
  --bits 10 --metadata noise.json
# Omit --input-rate when the source already has the output sample rate.

# Fixed strongest-signal reference: 40W corresponds to RMS0.1 in these samples.
build/dev-cli/background_noise --input clean.f32 --output noisy-mw.s16 \
  --format f32 --output-format s16 --noise-watts .001 \
  --reference-watts 40 --reference-rms .1 --metadata noise-mw.json

# Shared full-rate digital qualification; fresh directory, no audio device access.
python3 tools/triad_fullrate_noise_qualification.py \
  --tool build/dev-cli/background_noise --output local-evidence/noise-fullrate-digital

# In the existing research Python environment (NumPy/SciPy; stream tests use Torch):
python3 -m unittest tests/test_triad_background_noise.py \
  tests/test_triad_live_alsa.py tests/test_triad_stream_run.py tests/test_triad_live_duplex.py
```

Production live transmission and live adapters use the prefixed selectors
`--background-noise-bits`, `--background-noise-obscured-bits`,
`--background-noise-db`, and `--background-noise-watts`. The raw-ALSA and
continuous-stream adapters accept `--background-noise-tool` to select the built
native executable. Generic PCM watt settings also require
`--background-noise-reference-watts` and `--background-noise-reference-rms`.
The native duplex backend uses its built-in shared stage, so it does not use
`--background-noise-tool`. Unsupported operations (such as receive-only playback
options or a narrow-I/Q simulation) reject the setting rather than ignore it.

The focused core and adapter checks, plus the short digital qualification, are
the routine tests for this feature. Do not repeat the historical weak-signal,
multi-day or GPU receiver campaigns merely to test noise generation. Physical
qualification is opt-in with `triad_noise_qualification.py --live --live-only`
and an explicit `--device hw:C,D`; it briefly owns that capture/playback route.

The [shared full-rate qualification](validation-data/triad-v3/2026-11-06-background-noise-fullrate/README.md)
records the lower-rate-source to full-audio bandwidth checks, DDC variance
prediction and backend evidence limits.

The [HF background-noise target study](validation-data/triad-v3/2026-11-06-hf-background-target/README.md)
records software and cable reception near 8 dB-Hz DATA using full-audio AES
noise, with measured CPU/preparation costs, preserved misses and explicit
physical-null and continuous-streaming limits.

The [earlier digital and cable qualification](validation-data/triad-v3/2026-11-06-background-noise/README.md)
records its source identities, power/PSD errors, CPU cost and limits. Its 48 kHz
raw-ALSA DAC measurements remain evidence for that route. Its old complex
simulation results describe the narrow-I/Q diagnostic above, not a 48 kHz DAC
noise floor; its former native-backend exclusion is historical. Do not rewrite
those measurements as evidence for the new shared-backend integration.

## Basis

OpenSSL documents its [private random generator](https://docs.openssl.org/3.0/man3/RAND_bytes/)
and [EVP cipher interface](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/).
Counter uniqueness is specified by [NIST SP800-38A](https://nvlpubs.nist.gov/nistpubs/legacy/sp/nistspecialpublication800-38a.pdf).
The ideal converter convention follows [Analog Devices AN-282](https://www.analog.com/media/en/technical-documentation/application-notes/an-282.pdf).
Implementation tests and measured output spectra supply the software evidence;
these references alone do not prove this implementation's noise-floor accuracy.

# Offline control for the cable probe

These are noiseless sampled-PCM controls using the production Fast transmitter
and receiver through `tools/fast_cable_probe.cpp --offline`. No audio device was
opened. They help separate the modem's residual waveform/DSP error from the
additional effect of the physical DAC, cable, ADC and audio converters. They
are not physical-link trials or estimates of file-transfer success probability.

`offline-probe.jsonl` preserves the original twelve-point exploratory sweep,
with 1,000 intervals (2,048,000 payload bits) per point. `matched-offline.jsonl`
contains five controls matching the live raw probes' waveform settings, seed
417, amplitude 0.5 and 512 intervals (1,048,576 payload bits) per point. Each
matched record includes its complete reproduction command. The exploratory
18,000-symbol/s point used carrier 10,300 Hz; the matched point correctly uses
the live trial's 10,200 Hz.

| Case | Symbol rate | Carrier | Rolloff | Logical sample rate | Offline wrong / erased bits | Live wrong / erased bits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline | 15,000 | 9,300 | 0.2 | 48,000 | 0 / 0 | 1 / 0 |
| tight16500 | 16,500 | 9,400 | 0.1 | 48,000 | 5 / 0 | 35 / 0 |
| tight18000 | 18,000 | 10,200 | 0.1 | 48,000 | 10 / 0 | 73 / 0 |
| tight19200 | 19,200 | 10,800 | 0.1 | 48,000 | 38 / 8 | 768 / 40 |
| bridge44100 | 15,000 | 9,300 | 0.2 | 44,100 | 0 / 0 | 1 / 0 |

The live counts above come from the adjacent `raw-CASE.json` files, not from
these offline runs. Every matched offline control received all 512 intervals
in their original positions and observed physical completion. Its independent
alignment diagnostic identified offset zero for every interval. At matched
settings, final offline EVM was respectively 0.01027, 0.02281, 0.02423, 0.02531
and 0.01151. EVM is residual constellation error, not a calibrated SNR estimate.
The low-rolloff candidates already make occasional errors without analog
noise; the physical channel increases those error counts, especially at the
highest tested symbol rate.

The raw fixture uses deterministic random payload bits at their original
interval ordinal. Each nonzero soft decision is compared against that exact
bit position; zero soft values are separately counted as erasures. Missing
intervals and displaced acquisition prevent strict success. An independent
256-bit diagnostic searches nearby expected interval ordinals to identify
alignment loss, but never changes the strict comparison or success criterion.
Raw mode bypasses source coding, convolutional coding and Reed–Solomon coding;
the code-rate, RS and depth fields retained in its configuration JSON do not
mean that raw bits were protected by those codes.

All runs supply one second of leading zero PCM, then the generated waveform,
then eight seconds of zero PCM. The receiver must observe six seconds of
absent symbols to mark completion; input EOF cannot complete it. PCM is
processed in bounded chunks. Offline sample-rate changes exercise modem
sampling geometry, but do not exercise hardware negotiation or the live
resampler; zero-valued negotiated-format fields identify this distinction.

Build from the repository root:

```sh
c++ -std=c++20 -O3 -Iinclude tools/fast_cable_probe.cpp \
  build/libdatapump_fast.a build/libdatapump.a build/third_party/xz/liblzma.a \
  -lcrypto -ldl -pthread -o build/fast_cable_probe
```

For example, reproduce the matched baseline:

```sh
build/fast_cable_probe --offline --mode raw --intervals 512 --apsk 256 \
  --symbol-rate 15000 --carrier 9300 --rolloff 0.2 --sample-rate 48000 \
  --seed 417 --quiet
```

No FEC threshold, untested LDPC implementation, or 50 MB reliability guarantee
can be inferred from these raw controls. Their purpose is to prevent a large
analog signal-to-noise level ratio from being mistaken for negligible residual
modem errors.

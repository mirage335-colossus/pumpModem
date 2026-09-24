# Connected cable SNR: live measurements, 2026-09-20

The connected headphone-to-microphone loopback measured approximately **79 dB
SNR on its better individual input channel**. Averaging both captured channels
gave **81.48 dB SNR and 80.22 dB SINAD over 20–20,000 Hz**, using a 997 Hz
sine, amplitude 0.99, playback at 100%, and capture gain at 0 dB. Over the
modem's 300–18,300 Hz band, the combined results were **82.50 dB SNR and
80.96 dB SINAD**. Signal and residual powers were pooled across three repeated
steady windows. These are the best conditions tested, not a global hardware limit.

At the original 95% playback setting, a 0.95-amplitude sine gave **79.92 dB
SNR and 79.06 dB SINAD** across five repetitions. The production modem's S16
audio API measured less than the desktop float path. A simultaneous wideband
multitone also gave a lower residual ratio than the single sine. These
distinctions matter when interpreting a high SNR as modem operating margin.

The [evidence directory](validation-data/fast/cable-snr-20260920/README.md)
contains analyses, device metadata, sample-window indices and retained PCM
windows. Playback was restored to **95%** and capture to **0 dB**, verified in
[environment-after.json](validation-data/fast/cable-snr-20260920/environment-after.json).
This investigation changed no modem defaults.

## Conditions and definitions

The live route used the desktop's default Realtek ALC257 analog output and
analog input through the user's existing cable, not the output-monitor source.
Both output channels carried the stimulus. Playback and recording used
48,000 samples/s. The desktop runner exchanged float32 PCM with PulseAudio on
PipeWire; the device nodes exposed S32 PCM. Container formats do not establish
ADC resolution or effective bits. This same-card test does not characterize
independent DAC/ADC clocks or other sample rates.

All reported bands are **unweighted**. Single-tone SNR removes DC, the fitted
fundamental, and harmonic orders 2–6 from the noise denominator. SINAD removes
only DC and the fundamental, retaining harmonic distortion and other residual
components. Higher harmonics and sidebands remain in the SNR residual. This
distinction follows the converter measurement definitions in
[Analog Devices AN-835](https://www.analog.com/en/resources/app-notes/an-835.html).

Each tone's actual frequency was refined near its nominal value, followed by
a joint sinusoidal fit with fixed harmonic multiples, folded at Nyquist when
necessary. Hann-windowed residual spectra used window-energy normalization;
noise power was integrated over the stated band, including fractional edge
bins. This avoids treating spectral leakage as noise or quoting an FFT-bin
floor as integrated SNR. Transitions and fades were excluded: steady windows
discarded 0.5 seconds at each end. Separate one-second windows check stability
alongside the longer fits. PCM was not coherently averaged across repetitions.

Fits remove few parameters from tens of thousands of samples; the uncorrected
noise-projection fraction is recorded. An [independent FFT-mask check](validation-data/fast/cable-snr-20260920/independent-check.json)
of 21 strong-tone windows agreed within 0.023 dB for SNR and 0.016 dB for SINAD.
Eleven synthetic analysis tests passed. Overlapping diagnostic windows were
not pooled as independent observations.

## Results

| Measurement | Conditions | 20–20,000 Hz | 300–18,300 Hz |
| --- | --- | ---: | ---: |
| Individual left-input SNR | Maximum-level setting, three repeats | 78.96 dB | 80.01 dB |
| Individual right-input SNR | Same recordings | 78.43 dB | 79.44 dB |
| Best repeated sine SNR | 997 Hz, amplitude 0.99, playback 100%, three repeats | 81.48 dB | 82.50 dB |
| Corresponding SINAD | Same recordings | 80.22 dB | 80.96 dB |
| Original-volume sine SNR | 997 Hz, amplitude 0.95, playback 95%, five repeats | 79.92 dB | 80.98 dB |
| Corresponding SINAD | Same recordings | 79.06 dB | 79.90 dB |
| Production S16-path SNR | 997 Hz, amplitude 0.90, playback 95% | 76.31 dB | 77.26 dB |
| Production S16-path SINAD | Same recording | 75.95 dB | 76.81 dB |
| Multitone signal/residual ratio | 19 simultaneous tones, peak 0.95, playback 95%, three repeats | 71.62 dB | 72.64 dB |

Desktop rows other than the two individual-channel rows use `(left + right)/2`
before analysis. Its approximately 2.5 dB benefit over the better channel is
part of the measurement configuration, not extra performance of either ADC.

The maximum-level sine's full-audio SNR ranged from **81.44 to 81.57 dB**;
the five original-volume repetitions ranged from **79.90 to 79.94 dB**.
The maximum-level capture peak was approximately 0.2344, with no near-full-scale
capture samples. Thus the best result was not obtained by clipping the ADC.
The analyses and exact settings are retained in
[maximum-g0](validation-data/fast/cable-snr-20260920/runs/maximum-g0/analysis-mono.json)
and [repeat-g0](validation-data/fast/cable-snr-20260920/runs/repeat-g0/analysis-mono.json).

Eight separate tones from 313 to 18,203 Hz, at amplitude 0.95 and 95% playback,
gave full-audio SNRs of approximately **79.95–80.18 dB**. Their measured signal
levels varied by only **0.022 dB**. This supports a flat response at the sampled
frequencies, rather than proving every frequency between them. Increasing
capture gain by 6 or 12 dB did not materially improve SNR: signal and noise
increased together. Two transient-contaminated records remain in the evidence
(6 dB gain/amplitude 0.03, and 12 dB/amplitude 0.25), with much worse measured
SNR. Their cause was not isolated; they were not substituted for clean records
or silently deleted.

The multitone spans **353–18,199 Hz**. Its residual retains noise, distortion
and intermodulation after subtracting fitted commanded tones. Products landing
on an excited tone cannot be separated from that tone by this measurement.
Consequently **71.62 dB is a linear multitone residual ratio, not an AWGN SNR**
or a calibrated prediction of modem decoding performance.

The [production-path comparison](validation-data/fast/cable-snr-20260920/runs/levels-g0/analysis-production-mono.json)
used the actual application's audio API, including S16 playback/capture
conversion. The desktop and production paths also differ in channel handling;
the observed difference cannot be attributed solely to nominal PCM bit depth.

## Silence controls and limits

Repeated high-level tones were followed by tones 60 dB lower and separate
silence. Digital devices can mute during silence and exaggerate conventional
signal-to-idle-noise ratios; [Audio Precision describes this limitation and
low-level-tone controls](https://www.audioprecision.com/category/news/signal-to-noise-ratio-snr-dynamic-range-and-noise).
Here, quiet noise was only about 0.3 dB below the high-tone noise. The headline
results use noise measured **during the tone**, rather than substituting the
quieter interval. These checks provide no evidence of a large silence-muting
benefit, but do not certify every driver processing setting.

This establishes repeatable best-case sine performance for this connected
system and selected settings. It does not establish 60–90 dB of usable modem
margin, independent-clock tolerance, or file-transfer success probabilities.
The earlier [modem transfer study](fast-cable-live-study.md) reports separate
modem and file-transfer observations; its constellation EVM must not be
relabelled as hardware SNR.

## Reproduction

From the repository root, use the bundled Python with NumPy and a fresh output
directory. `--live` opens the real default devices; the runner restores volume
settings afterward:

```sh
snr_python=/home/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3
"$snr_python" tools/cable_snr_live.py --live --output /tmp/cable-snr-repeat \
  --mode repeat --gain-db 0 --sink-percent 100 --amplitude .99 \
  --frequencies 997 --repeats 3
"$snr_python" tools/cable_snr_report.py /tmp/cable-snr-repeat --channel mono
```

For the production comparison, generate the level-sweep fixture without opening
audio, build the standalone capture tool, and replay through the restored setup:

```sh
c++ -std=c++20 -O3 -Iinclude tools/cable_audio_capture.cpp \
  build/libdatapump.a build/third_party/xz/liblzma.a \
  -lcrypto -ldl -pthread -o build/cable_audio_capture
"$snr_python" tools/cable_snr_live.py --output /tmp/cable-snr-levels --mode levels
build/cable_audio_capture --input /tmp/cable-snr-levels/transmit-mono.f32 \
  --output /tmp/cable-snr-levels/production.f32 --rate 48000 --device default
"$snr_python" tools/cable_snr_report.py /tmp/cable-snr-levels --channel mono \
  --capture-file /tmp/cable-snr-levels/production.f32 --capture-channels 1
```

The production comparison requires playback at 95% and capture gain at 0 dB;
the reported row uses its amplitude-0.90 segment. The evidence inventory
provides complete frequency and multitone configurations.

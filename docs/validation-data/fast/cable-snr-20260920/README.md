# Physical cable SNR evidence — 20 September 2026 UTC

The [measurement report](../../../cable-snr-live-study.md) interprets these actual
headphone-output-to-microphone-input captures. No monitor source, virtual cable,
synthetic noise capture, or modem simulation substitutes for the physical path.
Runtime modem defaults and wire formats were not changed.

## Results and inventory

All desktop runs use the default ALC257 analog endpoints, both playback
channels, float32 client PCM, and observed 48 kHz/S32 stereo hardware streams.
`mono` analysis means the arithmetic mean of the two captured channels; original
left/right samples remain available. It does not imply a mono hardware capture.
The production comparison separately uses the application's actual S16/mono
capture and S16/stereo playback API.

| Directory under `runs/` | Measurement |
| --- | --- |
| `levels-g0` | Eight 997 Hz levels, peaks .03–.9, original zero-dB capture gain and 95% playback. Also contains the separate production-S16 capture metadata and analysis. |
| `levels-g6` | Same eight levels with +6 dB capture gain, restored afterward. A transient in the .03 plateau is retained. |
| `levels-g12` | Same levels with +12 dB capture gain, restored afterward. A transient in the .25 plateau is retained. |
| `frequency-g0` | Eight frequencies, 313–18,203 Hz, peak .95, playback 95%, original capture gain. |
| `repeat-g0` | Five .95-peak 997 Hz plateaus at playback 95%, alternating silence and 60-dB-lower tones. |
| `maximum-g0` | Three .99-peak 997 Hz plateaus at playback 100%, with the same low-level controls; playback restored to 95%. |
| `multitone-g0` | Three repeated 19-tone signals spanning 353–18,199 Hz at peak .95/playback 95%, plus 60-dB-lower controls. |

Every run retains `metadata.json`, `analysis-mono.json`, playback/capture logs,
and selected losslessly archived raw windows in `capture-windows.npz`.
`capture-windows.json` maps each array to its original sample indices and the
corresponding analysis record, and records its original float32 SHA-256.
The NPZ includes the original two capture channels and matching transmit
samples. No coherent averaging across time or repetitions was applied.

All steady windows of the frequency, repeat, maximum, and multitone runs are
archived. Level-sweep raw windows retain the .35/.9 cases at zero gain, the
.03 transient/.9 cases at +6 dB, and the .25 transient/.9 cases at +12 dB.
All other numerical results and full-record hashes remain in the archive;
the complete raw records remain in `/tmp/pump-snr-20260920-*` for this session.
The two disturbed windows were not removed from results or the raw evidence.

Additional files:

- [manifest.json](manifest.json): tool hashes, complete-record hashes, and raw-window inventory.
- [environment-after.json](environment-after.json): device identity, codec capabilities, observed active hardware format, and restored mixer state.
- [independent-check.json](independent-check.json): independent FFT-mask cross-check, pooled repetition statistics, individual-channel comparison, and frequency response.
- [independent-check.py](independent-check.py): source of that cross-check against the original complete captures in `/tmp`; only its explicitly labelled channel-comparison section calls the main analyzer.
- [verify-windows.py](verify-windows.py): verify archived hashes and reproduce their SNR/SINAD/residual metrics without any audio access.
- [validation.log](validation.log): offline synthetic analysis tests and archived-window verification.

## Reproduction

The Python tools require NumPy. In this session the bundled interpreter was
`/home/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3`.
Commands below assume `python3` resolves to an interpreter with NumPy.

A plan generates known PCM and segment metadata without opening audio:

```sh
python3 tools/cable_snr_live.py --output /tmp/new-snr-plan --mode levels
```

For the actual microphone/headphone cable, retain the archived directories and
choose fresh output paths. These commands **play and record real audio**:

```sh
python3 tools/cable_snr_live.py --live --output /tmp/new-snr-levels \
  --mode levels --gain-db 0
python3 tools/cable_snr_live.py --live --output /tmp/new-snr-repeat \
  --mode repeat --gain-db 0 --amplitude .95 --frequencies 997 --repeats 5
python3 tools/cable_snr_live.py --live --output /tmp/new-snr-maximum \
  --mode repeat --gain-db 0 --amplitude .99 --sink-percent 100 \
  --frequencies 997 --repeats 3
```

The runner restores the original capture volume in `finally`; when testing
output 100%, it also restores the original output setting. It neither changes
modem defaults nor configures a persistent audio-processing pipeline.
Frequency and multitone lists and all commanded levels are retained in each
run's metadata. Capture gain is a relative change from the volume at invocation;
verify hardware dB controls rather than copying host-specific percentages.

Analyze a completed capture without touching hardware:

```sh
python3 tools/cable_snr_report.py /tmp/new-snr-repeat
python3 tests/test_cable_snr_analysis.py
python3 docs/validation-data/fast/cable-snr-20260920/verify-windows.py
```

The independent production-API comparison was built with:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude \
  tools/cable_audio_capture.cpp build/libdatapump.a \
  build/third_party/xz/liblzma.a -lcrypto -ldl -pthread \
  -o build/cable_audio_capture
```

Then, with the original mixer settings and no competing measurement stream:

```sh
build/cable_audio_capture --input /tmp/new-snr-levels/transmit-mono.f32 \
  --output /tmp/new-snr-levels/production-capture.f32 --rate 48000 --device default
python3 tools/cable_snr_report.py /tmp/new-snr-levels \
  --capture-file /tmp/new-snr-levels/production-capture.f32 --capture-channels 1
```

Initial ALSA default-device warnings in the production log precede successful
fallback; the recorded formats and successful exit describe the actual stream.
The measurement report specifies bandwidths, harmonic removal, repetition
pooling, shared-clock limitations, and the difference between single-tone SNR
and the multitone residual ratio.

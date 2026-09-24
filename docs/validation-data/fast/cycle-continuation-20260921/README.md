# Damaged-cycle continuation evidence — 2026-09-21

Supports the [continuation diagnosis and fix](../../../fast-cycle-continuation.md).
No new live audio was emitted. Replays use the unchanged real default-device
speaker/microphone captures from the prior routing study. All four PCM hashes
were checked against that study's manifest before archiving these results.

## Contents

- `replay-a20.json` / `.log`: 100 KB, 64-QAM, right only, amplitude 0.20.
  All 32 frames attempted, eight unconverged, one damaged source cycle and two
  later checksum-verified cycles. The file remains incomplete.
- `replay-q64.json` / `.log`: 100 KB, 64-QAM, amplitude 0.40. All 32 frames
  attempted, 24 unconverged, three damaged source cycles, no verified source.
- `replay-bootstrap.json` / `.log`: failed-bootstrap 64-QAM repeat. Eight
  frames attempted, `decoding_stopped=1`, no source positions allocated.
- `replay-q16-500k.json` / `.log`: clean 500 KB control. Exact source recovered,
  96/96 converged frames, no damaged cycles, matching original received hash.
- `checks/`: focused and broader test logs, Release and Rev build logs,
  diagnostic-tool build log, and Rev shared GUI self-check.
- `manifest.json`: base Git revision, current source/binary hashes, retained
  PCM paths/hashes, and replay-output hashes. Raw PCM remains outside Git.

`verified_bytes` measures checked opaque source-area space, including flags and
padding; `spool_bytes` additionally includes fixed holes. Neither is a recovered
file-byte count. Progress logs are sampled, so final JSON carries total counts.
Replay wall time measures offline processing, not a fresh transfer's airtime or
live scheduling headroom.

The earlier live measurements attempted only 16 frames for the first two
recordings because the old decoder stopped after the first bad source cycle.
The bootstrap failure attempted eight frames and the clean control 96. See
[`../acoustic-routing-20260921/measurements/`](../acoustic-routing-20260921/measurements/)
for original results. Seeds reproduce source bytes but not the encoder's fresh
random salt; exact waveform reproduction uses the retained PCM.

## Reproduction and checks

The diagnosis provides the decisive replay command. Substitute
`/tmp/acoustic-routing-95-q64-mono.f32` and amplitude `.40` for the second run;
use `q64-mono-repeat.f32` for the bootstrap control. The clean control uses
`/tmp/acoustic-routing-95-q16-mono-500k.f32`, `--qam 16`, `--bytes 500000`,
amplitude `.40`, and `--require-success`. All use seed 701, LDPC 3/4, depth 8,
FFT 32768, prefix 4096 and pilot stride 16.

Build the probe with `cmake --build build --target fast_cable_probe --parallel 2`.
Do not select `--abort-on-failure` for continuation testing. Incomplete-file
replays intentionally omit `--require-success`; their final JSON must still be
checked for the expected negative whole-file result and positive continuation.

The new codec-only cases passed with `build/test_fast_codec --continuation`.
The final Release build and this broader suite passed **20/20 in 143.08 seconds**:

```sh
ctest --test-dir build --output-on-failure -j2 -R \
  '^(fast_codec|fast_acoustic|fast_files|fast_session|fast_transfer|fast_telemetry|fast_boundary|fast_cli|gui_fast|gui_fast_live|gui_controller|gui_application|gui_self_check|compression_short|transfer|stream_codec|stream_receive|attachment|gui_inspection|gui_binary_editor)$'
```

Both GUI variants rebuilt. `build-rev/datapump-gui --self-check` passed without
a display. The incremental Rev build reports an existing bitmap-width warning;
the focused codec build reports an existing OFDM indentation warning. Neither
warning concerns the changed decoding logic. No native adapter changed.

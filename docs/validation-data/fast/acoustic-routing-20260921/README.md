# Acoustic routing diagnosis evidence — 2026-09-21

This archive supports [the routing diagnosis](../../../fast-acoustic-routing-diagnosis.md).
Measurements used the real default laptop speakers and microphone, playback
volume 95%, capture volume 27%, 48 kHz PCM, FFT 32768, cyclic prefix 4096,
pilot stride 16, amplitude 0.40, LDPC 3/4, and interleave depth 8.

The four 100,000-byte tests used source seed 701 and compared 16/64-QAM with
right-only/both-speaker playback. Both 16-QAM runs and 64-QAM stereo recovered
the exact source; 64-QAM right-only failed in its second coding cycle. The
subsequent 500,000-byte 16-QAM/right-only transfer succeeded exactly in
140.4967033 seconds with 96/96 LDPC frames converged.
Further 64-QAM/right-only runs at amplitude 0.40, amplitude 0.20, and
amplitude 0.20 with LDPC 2/3 all failed source recovery. The final default's
shared GUI transmit/listen/Save path passed an exact 60-byte transfer over
real audio in 41.456375741 seconds. The focused suite passed 20/20 tests.

These sequential runs are not a matched-received-power experiment: stereo was
approximately 3.5 dB stronger at the microphone. The results do not uniquely
identify loudspeaker nonlinearity, two-speaker interference, room motion, or
device processing. They do not establish a 50 MB success probability.

## Contents and retained inputs

- `measurements/`: live probe JSON and progress/error logs, including 500 KB.
- `analysis/`: compact block, cycle, LDPC-frame, bit-plane, and 250 Hz-band
  summaries. Frame/cycle/plane statistics exclude fixed cycle alignment bits.
- `controls/`: same-recording frozen-channel and full-refresh replays, plus
  alternating-data-block cross-validation of a per-frequency correction.
- `sources/`: baseline receiver, experimental receiver variants, probe,
  known-symbol helper, cross-validation helper, and summary script snapshots.
- `gui/`: final default's real-audio shared GUI result, logs, and saved text.
- `validation/`: regression and diagnostic-build logs.
- `checks/`: final build/self-check logs and runtime/binary hashes.
- `manifest.json`: source/result hashes and hashes/paths of retained raw inputs.
- `helper-consistency.json`: conservation and bounds checks on aggregate results.

Raw PCM is intentionally outside this repository. Exact transmitted bit files
also remain at their recorded `/tmp` paths. The manifest identifies both by
SHA-256 and length. A seed reproduces source bytes; the encoder still generates
a fresh salt, so the seed alone does **not** reproduce the exact transmitted
waveform. Known-symbol replay must use the bit file saved with its recording.

## Live reproduction

Run from the repository root after building `fast_cable_probe`. These commands
open the real default audio devices. For each pair below substitute `QAM` with
16 or 64 and `NAME` with `q16-mono`, `q16-stereo`, `q64-mono`, or `q64-stereo`.
Append `--stereo` only for the two-speaker cases. Omitting it means right-only
output on a stereo endpoint.

```sh
build/fast_cable_probe --profile acoustic --capacity --mode codec \
  --device default --bytes 100000 --seed 701 --qam QAM --code-rate 3/4 \
  --depth 8 --amplitude .4 --sample-rate 48000 \
  --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16 \
  --ofdm-low 500 --ofdm-high 18000 --pre 1 --tail 8 \
  --capture-save /tmp/acoustic-routing-95-NAME.f32 \
  --tx-bits-save /tmp/acoustic-routing-95-NAME-bits.u8 --require-success
```

The 500 KB confirmation uses the same explicit settings, `--qam 16`,
`--bytes 500000`, and prefix `/tmp/acoustic-routing-95-q16-mono-500k`, without
`--stereo`. The archived JSON records the actual selected settings. Fresh
live outcomes may differ; exact source equality and reported physical end are
the acceptance checks.

The later 64-QAM/right-only repeats use prefixes
`acoustic-routing-95-q64-mono-repeat` (amplitude 0.40),
`acoustic-routing-95-q64-mono-a20` (amplitude 0.20), and
`acoustic-routing-95-q64-r23-mono-a20` (amplitude 0.20 and code rate 2/3),
with the same 100000-byte source and seed 701. All three are recorded failures.

## Recorded-waveform and information replay

For baseline 64-QAM/right-only replay, use:

```sh
build/fast_cable_probe --profile acoustic --capacity --mode codec \
  --replay /tmp/acoustic-routing-95-q64-mono.f32 --bytes 100000 --seed 701 \
  --qam 64 --code-rate 3/4 --depth 8 --amplitude .4 \
  --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16 --require-success
```

That negative control exits nonzero because source recovery fails. All 1016
physical intervals are nevertheless observed. The source decoder stops after
the first uncorrectable coding cycle; the independent known-bit helper scores
all 32 transmitted LDPC frames.

```sh
build/acoustic_known_symbols \
  /tmp/acoustic-routing-95-q64-mono.f32 \
  /tmp/acoustic-routing-95-q64-mono-bits.u8 \
  /tmp/acoustic-routing-95-q64-mono-known 64 8 32768 4096 16
python3 docs/validation-data/fast/acoustic-routing-20260921/sources/summarize_acoustic_information.py \
  /tmp/acoustic-routing-95-q64-mono-known 64 32768
```

Use the matching names and QAM argument for the other three recordings. The
helper emits existing bin/block statistics plus cycle/frame/bit-plane files.
GMI uses the receiver's clipped max-log LLRs at scale 1. The reported
`oracle_scale` maximizes an explicit post-capture scale grid using known bits;
it is not an implemented decoder change or an independently measured capacity.
Frame indices are `cycle * depth + frame`. Every included frame in these four
recordings has exactly 64800 compared coordinates. Fixed alignment bits are
excluded from frame/cycle/plane statistics; original bin/block statistics
include them. The variance and EVM columns are receiver residual measures,
not noise-only SNR measurements.

The 2/3 recording has the same fixed coded-frame and OFDM geometry as 3/4;
the known-symbol helper scores its saved transmitted bits independently of
source decoding. For its summary, supply `64 32768 2/3` to the Python script
so the information-load comparison uses the actual code rate. GMI is an
empirical decoder-metric diagnostic, not a strict channel-capacity upper bound.

## Receiver update controls

`sources/acoustic_ofdm-baseline.cpp` is the exact receiver used for the initial
matrix. `acoustic_ofdm-frozen.cpp` retains the startup channel estimate while
continuing pilot-based common gain/timing tracking. `acoustic_ofdm-full-refresh.cpp`
uses the latest independently admitted refresh instead of the baseline 75/25
blend. Neither experimental receiver was retained in production.

To reproduce a variant without editing the working tree, compile its object
with the same member name as the production static library object:

```sh
mkdir -p /tmp/acoustic-control-build
c++ -std=c++20 -O3 -DNDEBUG -Iinclude -Isrc/fast -c \
  docs/validation-data/fast/acoustic-routing-20260921/sources/acoustic_ofdm-frozen.cpp \
  -o /tmp/acoustic-control-build/acoustic_ofdm.cpp.o
cp build/libdatapump_fast.a /tmp/acoustic-control-build/libdatapump_fast.a
ar r /tmp/acoustic-control-build/libdatapump_fast.a \
  /tmp/acoustic-control-build/acoustic_ofdm.cpp.o
c++ -std=c++20 -O3 -Iinclude -Itools \
  docs/validation-data/fast/acoustic-routing-20260921/sources/fast_cable_probe.cpp \
  /tmp/acoustic-control-build/libdatapump_fast.a build/libdatapump.a \
  -lcrypto -ldl build/third_party/xz/liblzma.a -pthread \
  -o /tmp/acoustic-control-build/probe
```

Run the recorded-waveform command above with this `probe`, and use the
full-refresh source for the other variant. Replacing the original archive
member, rather than adding an object under a different name, is essential.
The snapshots preserve the surrounding source revision; this recipe assumes
the matching repository headers and other library objects.

The known-data cross-validation helper is limited to the recorded 64-QAM,
depth-8 geometry. Build it against the **baseline** library:

```sh
c++ -std=c++20 -O2 -Iinclude -Itools \
  docs/validation-data/fast/acoustic-routing-20260921/sources/acoustic_channel_crossvalidate.cpp \
  build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl \
  build/third_party/xz/liblzma.a -pthread -o /tmp/acoustic-channel-crossvalidate
/tmp/acoustic-channel-crossvalidate \
  /tmp/acoustic-routing-95-q64-mono.f32 \
  /tmp/acoustic-routing-95-q64-mono-bits.u8 /tmp/acoustic-channel-crossvalidate.csv
```

It fits a per-bin complex prediction from even data blocks and scores the
unseen odd blocks. The model uses known source bits unavailable to ordinary
reception. Its error reduction identifies a repeatable prediction mismatch,
but neither identifies a unique physical cause nor demonstrates an online
receiver fix. In the CSV, band `-1` denotes the full occupied band; other rows
are 250 Hz ranges beginning at `band_low_hz`.

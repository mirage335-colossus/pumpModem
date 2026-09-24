# Acoustic receiver diagnosis evidence

See [the diagnosis](../../../fast-acoustic-reception-diagnosis.md) for findings,
limits and outcomes. Investigation started from commit
`91c41a6db001b5e85c1c29bdcd9e1fafc9ee10af`.

`acoustic-rx-debug-75` is the original receiver, normal 100 KB live trial.
`acoustic-rx-gain-ramp` is the first acquisition-fix receiver, 100 KB live trial
with diagnostic transmit gain rising from 0.2 to 1 during training. Its final
coding cycle failed; it must not be counted as a successful file transfer.
`acoustic-rx-gain-ramp-before-replay-matched` replays that identical physical
capture through the frozen old receiver with all geometry specified explicitly.
The old probe executable had older defaults, so omitting these options would
compare different waveforms and would not be a valid before/after control.

```sh
# Normal original live trial (explicit geometry)
build/fast_cable_probe --profile acoustic --capacity --mode codec \
  --bytes 100000 --qam 64 --code-rate 3/4 --depth 8 --amplitude .40 \
  --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16 --stereo --seed 601 \
  --capture-save /tmp/acoustic-rx-debug-75.f32 \
  --tx-bits-save /tmp/acoustic-rx-debug-75-bits.u8 --require-success

# Controlled real-device startup attenuation, with new diagnostic option
build/fast_cable_probe --profile acoustic --capacity --mode codec \
  --bytes 100000 --qam 64 --code-rate 3/4 --depth 8 --amplitude .40 \
  --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16 --stereo --seed 602 \
  --training-gain-start .2 --capture-save /tmp/acoustic-rx-gain-ramp.f32 \
  --tx-bits-save /tmp/acoustic-rx-gain-ramp-bits.u8 --require-success

# Exact physical-recording comparison against the retained old receiver
/tmp/acoustic-rx-before-fix-probe --profile acoustic --capacity --mode codec \
  --bytes 100000 --qam 64 --code-rate 3/4 --depth 8 --amplitude .40 \
  --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16 --stereo --seed 602 \
  --replay /tmp/acoustic-rx-gain-ramp.f32
```

The GUI harness uses the full shared `Application`, including normal Regular/Fast
audio handoff, real audio, presentation polling and explicit Save service. It
represents two separate instances and does not drive native widgets. Build the
archived `acoustic-gui-live-debug.cpp` with the current build's libraries:

```sh
c++ -std=c++20 -O2 -I src/gui -I include \
  docs/validation-data/fast/acoustic-reception-20260921/acoustic-gui-live-debug.cpp \
  -o /tmp/acoustic-gui-live-debug-fixed \
  build/libdatapump_gui_application.a build/libdatapump.a \
  build/libdatapump_fast.a build/libdatapump_legacy.a build/libdatapump.a \
  -lcrypto -ldl build/third_party/xz/liblzma.a
```

Run the archived Python orchestrator after choosing unused output paths. The
`acoustic-gui-live-current` directory retains the first harness's premature Save
check; its `FAIL` is a presentation-poll race in the harness, not a demonstrated
product failure. The corrected `acoustic-gui-live-fixed` trial passed both
processes and saved exact text. The original harness ran concurrently with part
of the paced, mock-audio GUI regression; the corrected live trial ran without it.

Large raw PCM, exact source-bit fixtures and the frozen old executable remain
under `/tmp`; `retained-large-files.json` records sizes and SHA-256 hashes. They
are not committed audio artifacts. The source fixture is deterministic by seed;
bootstrap randomness means exact bit comparisons must use saved transmitted bits.
Replay JSON has no regenerated source hash, so use exact source equality and the
independent received hash, not an empty source-hash field, to assess success.

The final weighted estimator and fresh live checks are recorded in:

- `acoustic-rx-gain-ramp-weighted.json`: exact re-decode of the previously failed
  real 100 KB capture.
- `acoustic-rx-debug-75-weighted.json` and
  `fast-acoustic-v3-bulk-5m-weighted.json`: successful 100 KB and 5 MB controls.
- `acoustic-rx-gain-ramp-known-*-blocks.csv`: independently known transmitted-bit
  BER/GMI diagnostics for the unweighted, discarded rebase, and weighted estimators.
- `acoustic-ofdm-unweighted-acquisition-fixed.cpp`: frozen intermediate receiver
  for reproducing the estimator comparison.
- `ofdm-weighted-regression-{before,after}.txt`: deterministic noisy 30 KB
  before/after source recovery, with the fixture harness alongside.
- `acoustic-rx-final-gain-500k.*`: fresh 500 KB physical transfer with final
  receiver, gain ramp 0.2 to 1, seed 603, 96/96 LDPC frames and exact bytes.
- `acoustic-gui-live-final/`: final weighted receiver's full shared Application
  real-device text and explicit Save check.

The 500 KB command uses the same explicit geometry as the 100 KB example above,
with `--bytes 500000 --seed 603` and unused capture/bit output paths. The final
GUI harness links the same way, with output executable
`/tmp/acoustic-gui-live-debug-final`; run `acoustic-gui-live-final-run.py` after
choosing unused output paths. `final-source-hashes.json` identifies runtime and
regression sources. These experiments do not establish a 50 MB success probability.

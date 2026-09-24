# Radio capacity validation — 2026-09-21

Offline production-waveform tests only; no audio or RF devices were operated.
See [the radio profile study](../../../fast-radio-capacity.md) for the SNR
definition, exact geometry, results and limitations.

The permanent regression is `tests/test_fast_radio.cpp`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_fast_radio --parallel 2
ctest --test-dir build --output-on-failure -R '^fast_radio$'
```

`radio-test.log` records the first successful default codec and 20 dB sampled
SSB/FM run. `radio-ctest.log` records the final regression after adding the
3 Hz carrier offset and 15% / 0.5 ms echo to its SSB waveform case.

The other sources freeze the bounded diagnostic fixtures as executed:

- `candidate.cpp` / `candidate.log`: 256-QAM, LDPC 2/3 at 20 dB failed. Its
  planned 18 dB second case did not run because the first assertion failed.
- `characterize.cpp` / `characterize.log`: 64-QAM 3/4 at 17 and 18 dB,
  64-QAM 8/9 at 20 dB, 256-QAM 2/3 at 23 dB. The printed `erased_intervals`
  includes absent intervals scored during the final silence; it is not a count
  of missing payload intervals. The codec received all 64 transmitted intervals
  at each point. The failed bootstrap at 17 dB appropriately prevents further
  source decoding because its integrity context was never established.
- `echo.cpp` / `echo.log`: 64-QAM 3/4 at 20 dB, 3 Hz carrier offset and a
  static delayed echo at 15% amplitude and 0.5 ms delay.

To rebuild an archived diagnostic from the repository root:

```sh
c++ -O3 -DNDEBUG -std=c++20 -Iinclude \
  docs/validation-data/fast/radio-capacity-20260921/characterize.cpp \
  build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl \
  build/third_party/xz/liblzma.a -o /tmp/fast-radio-characterize
/tmp/fast-radio-characterize
```

Substitute `candidate.cpp` or `echo.cpp` for the other fixtures. The snapshots
use the production codec and modem APIs, so future DSP changes may alter their
results. The files are reproducibility evidence, not new production programs.

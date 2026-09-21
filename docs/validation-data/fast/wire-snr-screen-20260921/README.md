# Wire expected-SNR preset screen — 2026-09-21

This bounded offline screen found and removed an invalid automatic preset.
The original Gaussian-capacity-plus-constant-gap model selected 1,048,576-QAM
with LDPC 1/2 at 36 dB expected cable SNR. All four tested LDPC frames failed.
The corrected automatic selector excludes LDPC 1/2 above 16-QAM until those
combinations receive their own calibrated selection thresholds. Manual
configuration remains available.

Only the 36 dB entry changes among the offered menu choices:

| 36 dB wire setting | Estimated 50 MB payload bit/s | AWGN result |
| --- | ---: | --- |
| Original: 1,048,576-QAM, LDPC 1/2 | 160,903 | 4/4 failed; 12,999 wrong source bits |
| Corrected: 4096-QAM, LDPC 7/9 | 155,335 | 4/4 exact; mean 2.25 iterations |

That correction costs approximately 3.46% estimated throughput. All other
original menu configurations passed four frames each. The suspicious
1,048,576-QAM 2/3 setting at 46 dB passed all four frames, as did additional
45.5 and 46.5 dB checks. These small samples detect obvious bad presets; they
do not establish low error rates, fade margin, or whole-file reliability.

## What was actually tested

`ldpc_wire_awgn.cpp` uses the production LDPC encoder, decoder, permutation,
capacity interleave rotation, spectral whitening, Gray QAM max-log demapper
and ±24 LLR clipping. Four random LDPC source words form one depth-four cycle.
Mapping resets at every 2048-bit physical interval and includes the production
zero bits at the end of each partial QAM label. Cycle rounding and whitening
also include the final fixed-interval fill. Thus dense-QAM bit-plane imbalance
is presented to the actual LDPC matrix after the same interleaving as production.

The analytic mapper computes the same Cartesian Gray lattice without allocating
millions of constellation points. Gaussian noise is injected into the complex
constellation observations, with variance `N0/2` in each coordinate. For the
wire menu points:

```
Es/N0_dB = expected_full_18000_Hz_SNR_dB + 10 log10(1.02)
         = expected_SNR_dB + 0.0860017 dB
```

This is a **decoder-only AWGN screen**. It does not generate PCM, acquire a
marker, estimate a channel, test pilots, run an audio device, or transmit a radio
signal. The outer RS and integrity checker are not invoked. A passed frame means
all decoded source bits matched its known random source. No convergence-only
success shortcut was used. Production decoder iteration limits were retained.

The GMI column is the confidence-based information estimate per transmitted
wire bit, including the small fixed-cycle fill. It is not a measured hardware
SNR. In particular, GMI above the nominal code rate does **not** guarantee this
finite LDPC decoder succeeds: the failing 36 dB point had GMI 0.5376 at rate 1/2.

## Evidence files

- `original-menu.csv`: all 14 original wire menu entries, four frames each.
- `half-rate-screen.csv`: nine powers-of-four QAM orders at the old model's
  rate-1/2 target plus its 3 dB allowance and the rolloff conversion. Orders
  64 through 16,384 passed; 65,536 through 4,194,304 failed all four frames
  per point. Passing lower-order cases are still excluded conservatively from
  automatic selection above 16-QAM until thresholds have been calibrated.
- `replacements.csv`: the corrected 36 dB setting and the ±0.5 dB neighborhood
  of the 46 dB / 1,048,576-QAM / 2/3 setting.
- `original-preset-table.csv` and `corrected-preset-table.csv`: every menu
  choice across all four profiles. Columns are channel, expected reference SNR,
  waveform, QAM order, code rate, stored SC baud, actual occupied bandwidth,
  interleave depth and estimated public 50 MB payload bit/s. For OFDM the
  stored SC baud field is inactive; its FFT and prefix determine physical timing.
- `preset_geometry_review.cpp`: independent table checker covering 401 SNR
  points per channel in 0.1 dB steps. It checks monotonic payload throughput,
  bandwidth, minimum baud, unchanged nominal profile IDs, all rate-option IDs,
  and rejection of NaN, infinity and out-of-range assumptions.
- `corrected-geometry-review.log`: all 1,604 points passed after the correction.

The screen contains 104 LDPC frames total. Twenty failed across the deliberately
investigated bad points; the corrected menu configurations had no failures in
these small samples.

## Reproduction

From the repository root after building `datapump_fast`:

```sh
c++ -O3 -DNDEBUG -std=c++20 -Iinclude \
  docs/validation-data/fast/wire-snr-screen-20260921/ldpc_wire_awgn.cpp \
  build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl \
  build/third_party/xz/liblzma.a -o /tmp/fast-wire-preset-awgn
/tmp/fast-wire-preset-awgn 1/2 1048576 36.08600171761918 4 73209
/tmp/fast-wire-preset-awgn 7/9 4096 36.08600171761918 4 75200
```

Arguments are code rate, constellation order, complex-symbol Es/N0 in dB,
interleave depth / number of frames, and random seed. Each CSV row records its
arguments and result. Compile `preset_geometry_review.cpp` the same way to
rerun the table checks against the current selector. Future changes to the
production mapper, interleaving, decoder or preset model may change results.

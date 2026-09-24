# Final 4M-QAM amplitude and coding margin captures

These five additional live 100,000-byte attempts used the frozen 2,048-symbol
preamble receiver, 17,647.0588235294 symbols/s, rolloff 0.02, 64-symbol markers
every 16 fixed intervals, and four pilots at the end of each interval. The
analysis below replays recorded PCM without audio access and compares every
admitted payload observation with its exact saved transmitted mapper bits.
Each row represents **one attempt**, not an estimated success probability.

| LDPC rate | Transmit amplitude | Live file result | True RMS EVM | Symbol signal/error | Raw coded BER | Unit-scale log-loss metric, bits/data symbol |
|---|---:|---|---:|---:|---:|---:|
| 9/10 | 0.30 | Exact | 0.05778% | 64.764 dB | 1.320% | 20.681 |
| 8/9 | 0.27 | Exact | 0.06405% | 63.869 dB | 1.713% | 20.370 |
| 9/10 | 0.27 | Exact | 0.06550% | 63.675 dB | 1.798% | 20.332 |
| 8/9 | 0.24 | Failed bootstrap: 1 of 4 LDPC frames failed | 0.07072% | 63.009 dB | 2.115% | 19.922 |
| 9/10 | 0.24 | First two cycles passed; all 4 frames in cycle 2 failed | 0.07120% | 62.951 dB | 2.137% | 20.086 |

The failed 8/9 attempt aborted playback after its bootstrap failure; only 296
complete received intervals are analyzed. Four additional queued transmit
intervals were not completely received, and the last analyzed cycle contains
42 intervals. The other captures have all 635 intervals. Later waveform data in
a failed capture does not imply that source decoding continued after failure.

Definitions and limitations of true-symbol EVM and the unit-scale log-loss
metric are in `qam-known-symbol-method.md`. The metric excludes the 20 mapper
fill bits per interval but includes known cycle fill, and is not a measured
Shannon capacity or a guarantee of finite-code decoding. Actual cycle LDPC
input loads are 19.2997 bits/data symbol for 8/9 and 19.5410 for 9/10; the
conservative nominal references are 19.3664 and 19.6085 respectively.

## What explains the lower-amplitude failures

The product of transmit amplitude and true EVM is 0.0001697–0.0001769 across all
five attempts, a 4.2% range. A fixed additive error floor predicts a 1.938 dB
signal/error reduction when amplitude falls from 0.30 to 0.24; the measured
9/10 reduction is 1.813 dB. This is strong evidence that reducing amplitude
reduces usable noise margin. It does not by itself distinguish analog noise,
quantization, or remaining receiver residual error.

The former acquisition and carrier failures are absent. Initial clock estimates
are within ±0.008 ppm, and all observed estimates remain within ±0.027 ppm.
The maximum interval common phase error is 0.000563 rad. Fitting and removing
one common complex gain per 94-symbol interval accounts for only 6–8% of total
error energy, including the statistical reduction from fitting two parameters.
Thus a large coherent phase excursion does not explain the amplitude cliff.

The failure is a practical decoder threshold, not a demonstrated fundamental
capacity limit. The failing 8/9 bootstrap has EVM 0.0007174, signal/error
62.884 dB, and unit-scale log-loss metric 19.644 bits/data symbol. The failing
9/10 cycle 2 has EVM 0.0007483, signal/error 62.519 dB, and metric 19.832.
Both metrics remain above their nominal coding references. Finite block length,
the particular iterative LDPC decoder, correlated/non-Gaussian residuals, and
mismatched soft metrics can therefore matter before an information-rate
estimate crosses the coding load.

There is direct evidence of imperfect soft-metric calibration: mean demapper
variance is 1.96 dB below actual data-symbol error variance in the failed 8/9
bootstrap, and 1.74 dB below it in failed 9/10 cycle 2. Some successful cycles
also underestimate variance, so this is a plausible contributing factor, not a
proven sole cause. Held-out marker calibration samples a different symbol set
and only twelve observations per marker. Future candidate improvements are
more reliable payload-relevant variance calibration, exact local PAM log-sum
likelihoods, and additional coding margin. These have not been implemented or
retested as part of this final amplitude comparison.

The separate ideal-AWGN benchmark uses exact PAM likelihoods. The live modem
uses max-log likelihoods and has non-Gaussian, time-varying residual error, so
live failures around 63 dB do not contradict a successful ideal-AWGN trial at
62 dB. A single successful 9/10 run at nominal amplitude is also weaker evidence
for long-file reliability than the completed 50,000,000-byte 8/9 trial.

## Reproduction and archive

The five original result records are under `live-runs/v3-4m-r*-a*-100k.json`.
Losslessly compressed PCM and exact bit files use the corresponding root-level
names `v3-4m-r*-a*-100k.{f32,bits}.gz`. Per-interval CSV files are
`4m-r*-a*-100k-known-symbols.csv`; `qam-margin-summary.json` includes aggregate
and per-cycle results, calibration ratios, and clock/phase bounds.

Build the diagnostic from the frozen receiver, then decompress and replay each
pair. For example, from the repository root:

```sh
c++ -std=c++20 -O2 -Iinclude \
  docs/validation-data/fast/capacity-20260920/qam-known-symbol-diagnostic.cpp \
  docs/validation-data/fast/capacity-20260920/modem-long-preamble.cpp \
  build/libdatapump_fast.a build/libdatapump.a build/third_party/xz/liblzma.a \
  -lcrypto -ldl -pthread -o /tmp/qam-final-margin-diagnostic
gzip -dc docs/validation-data/fast/capacity-20260920/v3-4m-r910-a24-100k.f32.gz > /tmp/margin.f32
gzip -dc docs/validation-data/fast/capacity-20260920/v3-4m-r910-a24-100k.bits.gz > /tmp/margin.bits
/tmp/qam-final-margin-diagnostic /tmp/margin.f32 /tmp/margin.bits > /tmp/margin.csv
cmp /tmp/margin.csv docs/validation-data/fast/capacity-20260920/4m-r910-a24-100k-known-symbols.csv
python3 docs/validation-data/fast/capacity-20260920/qam-margin-summary.py > /tmp/margin-summary.json
cmp /tmp/margin-summary.json docs/validation-data/fast/capacity-20260920/qam-margin-summary.json
```

The diagnostic's fixed amplitude and code-rate fields do not affect receiver
geometry, symbol observations, or likelihoods; it does no source/FEC decoding.
The saved original mapper bits supply the reference independently of either
code rate. No production DSP behavior was changed for these comparisons.

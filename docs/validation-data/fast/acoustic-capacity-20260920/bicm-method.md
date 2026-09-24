# Ideal BICM screen for the measured acoustic spectrum

This is an offline candidate comparison, not an additional live trial. It uses
the peak-0.20 and peak-0.40 stereo-playback sounders' arithmetic microphone mean,
over the production OFDM band of 500–18,000 Hz. The sounder method and its
limitations are described in [sounder-method.md](sounder-method.md).

[`tools/acoustic_bicm_analysis.py`](../../../../tools/acoustic_bicm_analysis.py)
computes exact Gray square-QAM bit likelihood information for 4, 16, 64, 256 and
1,024 points. It integrates over every equally probable PAM input level and
96-point Gauss–Hermite Gaussian-noise quadrature on each axis. Axis signal energy
is 1/2 and axis noise variance is N0/2, so the input SNR is complex-symbol Es/N0.
Information is the sum of `1 - E log2(1 + exp(-signed LLR))` across label bits.
This uses full likelihood sums, not the production max-log approximation.

The grid spans −20 to 45 dB in 0.25 dB increments. The NPZ retains every bit
plane's information, and the CSV retains their sums. Four numerical controls
check quadrature convergence against 160 points, independent 200,000-sample
Monte Carlo observations at QPSK/0 dB and 64-QAM/18 dB, Gaussian-input capacity
bounds and signal/noise limits, and a pooled loading result against an exhaustive
two-carrier optimum. All passed.

Measured per-frequency SNR is the sounder source PSD times the estimated response
power divided by the conservative held-out residual PSD. It is interpolated onto
the current production data subcarriers, excluding each eighth active carrier
used for tracking/verification. Treating these ratios as Es/N0 assumes a linear,
stationary channel, Gaussian-equivalent residual, adequately equalized carriers,
and sufficient cyclic prefix. Real residual depends on waveform, level and time.

## Uniform QAM and coding margin

For ideally interleaved BICM, average information per coded bit must at least
match the code rate. Meeting that condition does not establish this finite
64,800-bit LDPC decoder's waterfall or a file-success probability. The 1 and 2 dB
columns subtract that amount from every measured bin's SNR before calculating
information; they are sensitivity checks, not measured fading margins.

| Source peak | QAM | 0 dB penalty | 1 dB penalty | 2 dB penalty |
| ---: | ---: | ---: | ---: | ---: |
| 0.20 | 4 | 0.9990 | 0.9984 | 0.9974 |
| 0.20 | 16 | 0.9867 | 0.9815 | 0.9746 |
| 0.20 | 64 | 0.9319 | 0.9117 | 0.8872 |
| 0.20 | 256 | 0.7942 | 0.7595 | 0.7231 |
| 0.20 | 1,024 | 0.6371 | 0.6037 | 0.5707 |
| 0.40 | 4 | 0.9998 | 0.9996 | 0.9993 |
| 0.40 | 16 | 0.9942 | 0.9912 | 0.9871 |
| 0.40 | 64 | 0.9564 | 0.9411 | 0.9220 |
| 0.40 | 256 | 0.8402 | 0.8093 | 0.7764 |
| 0.40 | 1,024 | 0.6908 | 0.6583 | 0.6255 |

Entries are information bits per coded label bit. The tested rates are 0.5,
0.75, 0.7778 and 0.8889. At peak 0.20, 64-QAM with rate 3/4 retains substantial
ideal margin under a 2 dB penalty. Rate 8/9 clears a 1 dB penalty but narrowly
misses 2 dB. With 256-QAM, rate 3/4 barely clears 1 dB and fails 2 dB; rate 7/9
barely clears the unpenalized case. At peak 0.40, 256-QAM/3/4 clears 2 dB while
7/9 narrowly misses. Real decoder losses can consume these small margins.

At FFT 8,192 / prefix 4,096, the current pilot pattern leaves 2,613 data
subcarriers. Nominal source rates including this prefix and pilot allocation,
but before coding-cycle packing, outer RS, digests, startup/training or silence,
are 45.93 kbit/s for 64-QAM/3/4, 61.24 kbit/s for 256-QAM/3/4 and 63.51 kbit/s
for 256-QAM/7/9. Their corresponding 32,768 / 8,192 rates are about 55.12,
73.50 and 76.22 kbit/s. The larger FFT reduces prefix cost from 33.3% to 20%; its
longer symbol also increases latency and exposure to channel variation.

Simply adding constellation points does not guarantee more bit-metric
information at a fixed SNR. For example, at peak 0.20 with a 1 dB penalty,
256-QAM gives 6.076 bits per payload symbol while 1,024-QAM gives 6.037. The
latter has more weak bit planes and requires a lower code rate. A 1,024-QAM/1/2
entry that passes this information screen is a theoretical comparator, not a
recommendation supported by live acoustic decoding.

## Would bit loading help?

The report contains two explicitly different models. A conservative local model
chooses the largest constellation individually clearing the rate on each
carrier. It can underperform uniform modulation with coding across frequency,
because the uniform code can share information margin between strong and weak
carriers.

The pooled model maximizes total assigned label bits while requiring nonnegative
aggregate BICM margin across carriers, using a Lagrange search over discrete
orders 4/16/64/256/1,024 or off. It keeps per-carrier power fixed; it neither
water-fills power nor reallocates unused bins' power. Discrete choices leave a
little excess margin. A useful real implementation needs a compatible bit map,
LDPC interleaving, signaling or matching local settings, and finite-code margin;
none of those changes is implemented by this analysis.

| Peak | SNR penalty | Best uniform screen | Pooled loading screen |
| ---: | ---: | ---: | ---: |
| 0.20 | 0 dB | 63.51 kbit/s | 66.16 kbit/s |
| 0.20 | 1 dB | 61.24 kbit/s | 62.96 kbit/s |
| 0.20 | 2 dB | 51.04 kbit/s | 59.75 kbit/s |
| 0.40 | 0 dB | 63.51 kbit/s | 71.29 kbit/s |
| 0.40 | 1 dB | 63.51 kbit/s | 68.20 kbit/s |
| 0.40 | 2 dB | 61.24 kbit/s | 65.11 kbit/s |

These 8,192 / 4,096 figures include only prefix and pilot overhead. At peak 0.20
with 0–1 dB penalty, pooled loading adds only roughly 3–4% over the best uniform
screen. Gains become larger when a uniform mode crosses its information limit.
This comparison favors first validating uniform 64/256-QAM before introducing
loading complexity; it does not rule out greater gains in another room or with
a different code/modulation set.

## Evidence and reproduction

`bicm-8192-4096-analysis.json` and `bicm-32768-8192-analysis.json` retain every
uniform order/rate/margin combination, bit-plane information and both loading
allocations. `bicm-8192-4096-uniform.csv` is a compact uniform-mode table.
`bicm-manifest.json` identifies the source and test hashes. No audio is opened.

```sh
OPENBLAS_NUM_THREADS=1 python3 tools/acoustic_bicm_analysis.py \
  --spectrum /tmp/fast-acoustic-sounder-a20/spectral-analysis-arithmetic_mean.npz \
  --spectrum /tmp/fast-acoustic-sounder-a40/spectral-analysis-arithmetic_mean.npz \
  --output /tmp/new-bicm-analysis --fft 8192 --prefix 4096
OPENBLAS_NUM_THREADS=1 python3 tests/test_acoustic_bicm_analysis.py
```

Use the NumPy-enabled interpreter named in the sounder method on this host.
Reproducing the other geometry changes `--fft` to 32768 and `--prefix` to 8192.

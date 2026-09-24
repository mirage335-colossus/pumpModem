# Production LDPC: dense-QAM ideal-channel diagnostic

The CSV in this directory was generated on 2026-09-20 by
[`tools/fast_ldpc_benchmark.cpp`](../../../../tools/fast_ldpc_benchmark.cpp), using
the production `datapump::fast::ldpc` implementation and its frozen whole-frame
bit permutation. It does not use an external LDPC decoder or any audio device.
Every actual invocation and its stdout are archived in `ldpc-awgn-commands.json`.

## Reproduction

From the repository root:

```sh
c++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude \
  tools/fast_ldpc_benchmark.cpp src/fast/ldpc.cpp \
  -o /tmp/pump-fast-ldpc-benchmark
/tmp/pump-fast-ldpc-benchmark 1048576 9/10 57 64 9202090 50
```

Arguments are square-QAM order, LDPC rate, Es/N0 in dB, number of independent
random frames, seed, and maximum decoder iterations. The tool emits one CSV row
without a header; the first line of `ldpc-awgn.csv` supplies its schema. All runs
use a 50-iteration limit. The coarse sweep covers orders 65,536, 262,144 and
1,048,576; rates 7/9, 8/9 and 9/10; and Es/N0 45, 50, 55 and 60 dB, with eight
frames per point. Subsequent one-dB points locate the transition, then selected
points are repeated with 64 frames and a separate seed.

A later extension adds 4,194,304-QAM at every integer Es/N0 from 57 through
65 dB for the same three rates, again with eight frames per point and then
64-frame independent-seed extensions at the first clean pilot point. The only
benchmark code change for that extension raises its accepted QAM-order limit;
the LDPC engine, channel model, likelihood calculation and permutation are
unchanged. The source manifest preserves the earlier benchmark hashes.

## Measurement model

Frames use the normal-length DVB-S2X B10 (7/9) or DVB-S2 B10/B11 (8/9, 9/10)
64800-bit LDPC matrices. No BCH, outer RS, source-format padding, flags, integrity fields,
pilots, markers, pulse-shaping filter, channel estimator or clock tracker are
simulated. Source bytes and noise are newly generated for every frame.

Modulation is unit-average-energy square QAM with separate reflected Gray labels
on the two axes. Noise is independent complex Gaussian: total complex variance
`N0 = 10^(-EsN0_dB/10)`, with variance `N0/2` on each axis. Each axis's exact
log-sum bit likelihood is computed from all PAM levels, clipped to ±50 and
converted to float, then deinterleaved and passed to the production decoder.
The modem's live max-log demapper and its estimated noise are not substituted
for this known-noise diagnostic. Therefore its threshold is an ideal reference
for that implementation, not a measured live-link operating threshold.

The later 4,194,304-QAM extension uses 22 bits per modulation symbol. Each
64,800-bit codeword therefore needs 12 zero fill bits in its last QAM symbol.
Those bits are neither LDPC information nor observations in the reported BER or
GMI means. Rates quoted as information bits per symbol omit this very small
final-symbol packing loss, as well as all outer/physical framing overhead.

`wrong_frames` and `wrong_source_bits` compare every decoded source bit against
the original, including nonconvergent outputs. `nonconverged_frames` counts
syndrome failures. `undetected_wrong_frames` counts wrong source frames with a
valid final syndrome. No observed undetected error establishes a useful bound
on rare undetected events at this sample size. The result does not claim that
an outer code could repair the wrong frames.

GMI is the bit-metric generalized mutual-information estimate
`bps * (1 - mean(log2(1 + exp(-LLR * transmitted_sign))))`, with sign +1 for a
transmitted one. It includes the actual transmitted bits and pre-decoder LLRs.
Raw BER likewise precedes LDPC. Decoder timing includes production scratch
allocation and syndrome testing but excludes channel generation/demapping and
the explicit bit deinterleaver. `channel_seconds_per_frame` reports the latter
channel/demapper work separately. `elapsed_seconds` is the complete invocation
after initializing the fixed graph and constellation. Other development work
was running on this host, so timings are diagnostic, not isolated CPU benchmarks.

The frozen permutation is independent of the random generator. Common seeds
across rate/order points do not imply identical source/channel sequences because
the information-block sizes and symbol counts differ. The eight-frame and
64-frame runs use different seeds and are not duplicated observations.

## Interpretation limits

The archive contains 104 invocations / 1,560 random codewords. On the tested
one-dB grid, the lowest points with zero wrong source frames and zero syndrome
failures in a 64-frame independent-seed extension are:

| Square-QAM order | LDPC 7/9 | LDPC 8/9 | LDPC 9/10 |
| --- | ---: | ---: | ---: |
| 65,536 | 41 dB | 46 dB | 46 dB |
| 262,144 | 46 dB | 52 dB | 52 dB |
| 1,048,576 | 51 dB | 57 dB | 57 dB |
| 4,194,304 | 57 dB* | 62 dB | 63 dB |

*57 dB is the lowest 4M-QAM point tested; the 7/9 transition below that point
was not located. At 4M-QAM, 8/9 fails all eight pilot frames at 61 dB and
9/10 fails all eight at 62 dB. Their 64-frame extensions at 62 and 63 dB
respectively have no wrong source frames or syndrome failures. The 9/10
63 dB extension averages 7.59 iterations and 85 ms decoder time per frame;
the 8/9 62 dB extension averages 14.13 iterations and 170 ms under concurrent
host load. This excludes the diagnostic's deliberately exact, expensive
likelihood summation; the production analytic demapper has a different cost.

These are tested points, not precisely estimated statistical thresholds.
The 262,144-QAM 8/9 point at 51 dB initially passed all eight pilot frames, but
then failed one of 64 independent-seed frames, with 598 wrong source bits and
detected nonconvergence. Its 52 dB extension passed all 64. The failed result is
retained in the CSV. No undetected wrong source frame was observed anywhere in
the bounded sweep, including the many deliberate below-threshold failures.

The sweep demonstrates a sharp coding transition even at these high SNRs. For
example, at 45 dB, 65,536-QAM with 7/9 succeeds in all eight coarse trials while
8/9 and 9/10 fail all eight. More parity can therefore increase successfully
delivered throughput, despite reducing the nominal number of source bits.
At 60 dB all three rates work in all eight coarse trials through 1,048,576-QAM.
The 1,048,576-QAM 9/10 point carries 18 source bits per modulation symbol, giving
270 kbit/s at 15 ksymbol/s before any outer coding or physical framing overhead.
For 4,194,304-QAM, 8/9 carries 19.556 information bits/symbol and 9/10 carries
19.8 bits/symbol. At 17 ksymbol/s those are about 332.4 and 336.6 kbit/s before
the listed overheads. Dropping from 9/10 to 8/9 sacrifices only 1.23% nominal
throughput while the tested one-dB grid shows a one-dB improvement in required
Es/N0. These are coding candidates for live evaluation, not measured cable rates.

At 1,048,576-QAM, the 8/9 and 9/10 eight-frame pilots both fail at 56 dB and both
succeed at 57 dB. This is a useful comparison with approximately 62 dB of measured
live constellation signal-to-residual, but only if the live errors behave like
stationary, independent Gaussian noise. Tone SNR, broadband SNR and EVM do not
establish that condition. Clock slips, clipping, correlated equalizer errors,
outliers and interruptions can dominate a large file's failure probability even
when the average residual is small.

Zero failures in 64 frames gives a one-sided 95% binomial upper failure bound of
about 4.57%, not a demonstrated zero error floor. A 50 MB file at rate 9/10 spans
at least about 6,859 such information frames before outer/framing overhead. If
each frame failure were fatal and independent, an 80% file success target would
require a per-frame failure probability around 0.0033% or lower. These bounded
experiments locate useful candidates; they do not certify long-file delivery.

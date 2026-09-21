# LDPC rate margin and acoustic lock — 21 September 2026 UTC

For unchanged 16-QAM modulation and symbol timing, rate 3/4 bought approximately
**2.6 dB over 8/9 and 2.8 dB over 9/10** in an offline sweep of the production
LDPC decoder. These are approximate differences near the frame-error waterfall,
not demonstrated acoustic fade margins or rare-error operating thresholds.
No audio devices were opened and no runtime code or defaults were changed.

## Decoder experiment

The test used the production Gray QAM mapper and max-log soft demapper,
64,800-bit LDPC matrices, maximum 50 iterations, per-frame permutation,
depth-eight capacity rotation and whitening. LLRs were clipped to ±24 as in
the acoustic demapper. Each equally likely QAM symbol has average energy one;
independent complex Gaussian noise has total variance N0 and variance N0/2
on each real axis. Noise variance was known exactly to the demapper.

There was no OFDM waveform, synchronization, pilot estimation, frequency
selectivity, clock error, outer RS, source framing, or physical-end detection.
Exact recovered source bytes were compared with the original source; syndrome
convergence alone was not treated as success.

| Rate | Es/N0 | Failed / tested frames |
| --- | ---: | ---: |
| 3/4 | 9.80 dB | 10 / 24 |
| 3/4 | 9.85 dB | 3 / 24 |
| 3/4 | 10.00 dB | 0 / 24 |
| 8/9 | 12.35 dB | 14 / 24 |
| 8/9 | 12.40 dB | 8 / 24 |
| 8/9 | 12.50 dB | 1 / 24 |
| 9/10 | 12.55 dB | 18 / 24 |
| 9/10 | 12.60 dB | 7 / 24 |
| 9/10 | 12.80 dB | 0 / 24 |

The short sweep places the roughly half-failing transition near 9.8, 12.4,
and 12.6 dB, respectively. Sampling uncertainty precludes interpreting those
rounded positions as precisely measured thresholds. In total, 20 points and
336 frames took 96.37 seconds of benchmark runtime. Even 0/24 failures does
not qualify a low frame-error rate for a many-thousand-frame file.

Es/N0 is the useful relative link-budget coordinate when modulation, bandwidth,
and symbol timing stay unchanged. For information-bit energy instead,
`Eb/N0 = (Es/N0) / (4R)`. Thus the approximately 2.6/2.8 dB differences become
approximately 1.8/2.0 dB in Eb/N0. The definitions must not be mixed.

## Ideal information comparison

The existing `tools/acoustic_bicm_analysis.py` integrates exact Gray-QAM bit
likelihoods under AWGN. A 192-point Gauss–Hermite calculation and bisection
located where mean information per coded bit equals each rate. This uses
full likelihood sums, whereas production uses a max-log approximation.

| Rate | Gaussian-input bound | Ideal uniform 16-QAM BICM boundary |
| --- | ---: | ---: |
| 3/4 | 8.451 dB | 9.309 dB |
| 8/9 | 10.317 dB | 11.867 dB |
| 9/10 | 10.463 dB | 12.118 dB |

The BICM boundary differences are 2.559 and 2.809 dB. These information
boundaries are not thresholds for a finite code. The Gaussian-input column
uses `Es/N0 = 2^(4R) - 1`, allowing an input distribution that is not uniform
16-QAM. The two columns therefore describe different constraints. The AWGN,
SNR-normalization, and BICM definitions follow the
[MIT communication notes, chapters 3, 4 and 14](https://ocw.mit.edu/courses/6-451-principles-of-digital-communication-ii-spring-2005/bb895c1dee9ce0b39d6846e0aa984981_MIT6_451S05_FullLecNotes.pdf).

Neither theoretical column nor the decoder sweep measures the live acoustic
path's tolerance to changing echoes, nonlinear distortion, wrong channel
estimates, or sample discontinuities. A single hardware tone SNR or displayed
constellation EVM cannot substitute for that characterization.

## Lock versus coding failure

In the [reproduced acoustic failure](fast-acoustic-routing-diagnosis.md), all
1016 physical intervals arrived and all 32 data OFDM blocks were admitted.
The known-bit diagnostic had no erased LDPC coordinates. Bootstrap's eight
LDPC frames converged; the next eight failed and outer RS could not recover
the cycle. Maximum data-block timing correction was 0.034956 sample, compared
with a six-sample fit radius. This was a payload decoding failure while
physical synchronization remained usable, not an observed prolonged lock loss.

The QAM demapper calculates soft metrics independently for each equalized
symbol; it has no persistent lock state. Timing and common gain/phase tracking
use known pilots. Independent QPSK verification tones admit each OFDM block.
Those robust, selected tones can remain usable after dense payload data has
lost enough information to defeat LDPC. Data decisions and LDPC results do
not feed back into the current tracker.

There are separate failure mechanisms:

- A rejected physical block creates zero-confidence erasures at its fixed
  coordinates. A later valid block resumes delivery without shifting framing.
- Six seconds of consecutive fully scored failed blocks ends reception. At
  the current block duration that is about eight blocks, or 6.144 seconds.
  Extra LDPC cannot reverse that physical-end decision.
- The receiver does not search for a new preamble after initial acquisition
  within a transfer. A sufficiently large timing discontinuity can defeat it.
- The implementation used during this measurement latched a source-decoder
  failure after an uncorrectable coding or integrity cycle. Later intervals
  still counted but were no longer FEC-decoded. This was a separate software
  bug, not loss of physical synchronization. The subsequent
  [continuation fix](fast-cycle-continuation.md) retains a hole and decodes later
  cycles at their original positions. Missing bytes still prevent a complete
  file; there is no retransmission mechanism.

Relevant implementation: `src/fast/modem.cpp` (soft demapper),
`src/fast/acoustic_ofdm.cpp` (pilot verification, erasures, tracking and physical
completion), `src/fast/codec.cpp` (`StreamDecoder::push_interval`), and
`src/fast/session.cpp` (continued reception until physical end).

## Reproduction

The [evidence directory](validation-data/fast/ldpc-rate-margin-20260921/)
contains the helper, all sweep rows, exact build/source hashes, theory script,
and theory results. Run from the repository root with matching Release libraries:

```sh
c++ -std=c++20 -O3 -Iinclude \
  docs/validation-data/fast/ldpc-rate-margin-20260921/ldpc_qam16_margin.cpp \
  build/libdatapump_fast.a build/libdatapump.a \
  -lcrypto -ldl build/third_party/xz/liblzma.a -pthread \
  -o /tmp/ldpc_qam16_margin
/tmp/ldpc_qam16_margin 3/4 9.8 3 7102
python3 docs/validation-data/fast/ldpc-rate-margin-20260921/ldpc-rate-theory.py
```

The helper arguments are rate, Es/N0 in dB, cycles, and seed; each cycle tests
eight LDPC frames. Each CSV row records its actual rate, SNR, frame count and
seed. Python theory reproduction requires NumPy. The helper is a bounded
experiment fixture for the recorded arguments, not a shipped CLI.

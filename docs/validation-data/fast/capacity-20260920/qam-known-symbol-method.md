# Known-symbol diagnosis of a physical 4M-QAM recording

The files named `4m-r89-*` describe the **same physical recording**, made through
48 kHz default headphone/microphone devices, stereo output, amplitude 0.30,
4,194,304-QAM, LDPC 8/9, 17,647.0588235294 symbols/s, rolloff 0.02,
64-QPSK-symbol markers every 16 intervals, and four pilots following each payload
group. Each interval contains 2,048 coded bits. There are 635 complete intervals
in this recording. It predates the longer acquisition preamble and revised
LDPC frame/constellation-plane interleaver. The saved transmitted bits make this
analysis independent of that subsequently changed codec mapping.

`4m-r89-known-symbols.bits.gz` is the exact transmitter input to the mapper:
one byte valued 0 or 1 per bit. `4m-r89-known-symbols.f32.gz` is the real captured
mono PCM, IEEE binary32 little-endian, including physical silence. These are
lossless gzip archives, not generated channel simulations. Hashes and original
sizes are in `qam-known-symbol-manifest.json`.

The diagnostic maps the saved original bits to their exact Gray lattice points
and compares them with receiver I/Q observations. It does **not** equate
nearest-decision distance with physical signal-to-noise ratio. EVM is the square
root of observed squared error divided by original symbol energy per interval;
cycle summaries sum error energy and source energy where those columns are
available. The two older historical receiver-stage CSVs retain averages of
per-interval squared EVM. Mean phase and gain are
complex least-squares ratios against the exact transmitted symbols. The error
includes noise, timing error, phase error, residual channel response, and DSP
error. Consequently `-20 log10(EVM)` is a signal-to-error ratio, not a calibrated
AWGN measurement.

`variance` reconstructs the max-log demapper's variance from exact Cartesian
squared-distance differences divided by unsaturated output LLRs. GMI is the
observed binary-log-loss information rate at the receiver's **unit LLR scale**,
not a maximization over LLR scaling. Its units are actual information bits per
payload QAM symbol: `(2048 - bit_log_loss_nats / log(2)) / 94`. The 20 fixed mapper
fill bits per interval provide no source information. The nominal 8/9 reference
`(2048 / 94) * (8 / 9) = 19.3664` information bits per payload symbol is
conservative: a complete cycle contains four 64,800-bit LDPC frames and 896
additional fixed cycle-fill bits, spread over 127 intervals. Its actual LDPC
input load is `230400 / (127 * 94) = 19.2997` bits per payload symbol. The
diagnostic excludes mapper fill but retains the known cycle-fill bits in its
log-loss calculation, so it is a reproducible comparison of receiver stages,
not an exact measured source-information rate. Values below zero denote a
badly mismatched decoder metric; they are not negative Shannon capacity.

The three CSVs show successive receiver changes against the unchanged capture:

- `4m-r89-before-pilot-fix.csv`: held-out variance calibration, short-marker
  frequency resets and decision-directed carrier tracking. Cycle 1 had a GMI of
  16.83 bits/symbol, below both the 19.366 nominal reference and the 19.300-bit
  actual LDPC input load of rate 8/9.
  The worst interval's common phase error was 0.002758 rad despite gain 0.999962.
- `4m-r89-causal-pilot-pll.csv`: carrier frequency retained from acquisition and
  tracked by the four known pilots, without dense decision-directed phase
  updates. Every cycle's GMI exceeded 20.51 bits/symbol.
- `4m-r89-pilot-interpolation.csv`: additionally buffer one bounded pilot group
  and interpolate its phase between known endpoints before final demapping.
  Every cycle measured 20.53–20.69 bits/symbol; the maximum interval common phase
  error fell to 0.000389 rad. There are exactly 59,690 reported payload symbols,
  with no absent-tail points included.

`qam-known-symbol-summary.json` contains the per-cycle calculations. The
`1m-r910-held-out-variance.csv` provides a separate earlier 1M-QAM recording's
cross-check of known-symbol error versus held-out calibration. Its transmitted
source was reconstructed from a successfully corrected bootstrap salt and known
fixture bytes; it is supporting data, not the reproduction input below.

## Reproduce the final 4M-QAM comparison

From the repository root, build the library normally, then run:

```sh
gzip -dc docs/validation-data/fast/capacity-20260920/4m-r89-known-symbols.f32.gz > /tmp/capacity-known.f32
gzip -dc docs/validation-data/fast/capacity-20260920/4m-r89-known-symbols.bits.gz > /tmp/capacity-known.bits
c++ -std=c++20 -O2 -Iinclude \
  docs/validation-data/fast/capacity-20260920/qam-known-symbol-diagnostic.cpp \
  docs/validation-data/fast/capacity-20260920/modem-pilot-interpolation.cpp \
  build/libdatapump_fast.a build/libdatapump.a build/third_party/xz/liblzma.a \
  -lcrypto -ldl -pthread -o /tmp/qam-known-symbol-diagnostic
/tmp/qam-known-symbol-diagnostic /tmp/capacity-known.f32 /tmp/capacity-known.bits > /tmp/capacity-known.csv
cmp /tmp/capacity-known.csv docs/validation-data/fast/capacity-20260920/4m-r89-pilot-interpolation.csv
```

The archived DSP source intentionally retains the original 128-symbol capacity
preamble required by this older recording. The current modem uses longer
training. The `cmp` passed when the archive was created. Earlier-stage CSVs are
historical outputs; the frozen source reproduces the final interpolation stage.
This procedure performs no playback or capture and does not infer file success
from raw EVM alone.

## Short versus long acquisition training

`4m-v2-short-training-failure` is a separate, newer physical capture using the
balanced LDPC bit-plane mapping. Its 128-symbol preamble left an initial clock
estimate of **−0.6336 ppm**, which corrected only gradually over subsequent
markers. The first complete coding cycle had EVM around **0.00098** even though
its mean phase and gain stayed close to their targets. Error grew within each
16-interval marker block and dropped after a new timing observation. The live
probe aborted following the failed bootstrap; the analysis uses its 557 fully
received intervals, excluding four prefetched transmitter intervals that were
not completely played. This is not a successful source transfer.

`4m-v3-long-training-success` uses the final **2,048-symbol acquisition preamble**
and otherwise the same tested 4M-QAM 8/9 settings. The preamble lasts
about 0.116 seconds at this symbol rate, 0.109 seconds longer than the old one. Its 100,000-byte live transfer passed
all 20 LDPC frames and the final source digest. Direct comparison against the
saved transmitted bits gives:

- Aggregate EVM **0.000583836** (0.05838%), or **64.674 dB** source-symbol/error
  energy ratio. This includes residual DSP and channel impairments.
- Raw coded-bit error rate **1.35896%**, before LDPC and outer RS correction.
- Unit-scale empirical GMI **20.662 bits per payload symbol**, with all five
  coding cycles in **20.641–20.692**, versus the conservative 19.366-bit nominal
  reference (actual LDPC input load 19.300 bits/symbol).
- Initial clock estimate **+0.002889 ppm**; subsequent observed estimates stayed
  approximately within ±0.022 ppm. The largest interval mean phase error was
  0.000431 radians.

The receiver's final nearest-decision EVM was 0.000433. It is lower than the true
known-symbol EVM, demonstrating why it must not be used as a hardware SNR claim.

The compressed PCM and exact transmitted bits for both captures, source JSON
reports, and per-interval CSVs are retained. `modem-long-preamble.cpp` freezes the
long-preamble DSP for reproducibility. Use the same diagnostic compilation as
above with that source substituted, then the decompressed v3 inputs, to reproduce
`4m-v3-long-training-success.csv`. Use the short-preamble snapshot for the v2
failure. The historical code snapshots belong only to this validation archive;
normal application builds use `src/fast/modem.cpp`.

A stronger control uses the **identical v3 recording** twice. The frozen short
receiver uses only the last 128 training symbols (the training sequence is
periodic); the long receiver uses all 2,048. In this controlled comparison,
short fitting estimates −0.7173 ppm and produces first-cycle EVM 0.001074 and GMI
16.204 bits/symbol. Long fitting estimates +0.002889 ppm and produces first-cycle
EVM 0.0005865 and GMI 20.655 bits/symbol. Playback, capture, source bits, amplitude,
and actual channel noise are identical. `4m-v3-counterfactual-short-fit.csv`
records this control. It isolates the clock estimator's use of training rather
than attributing the improvement to a different noise realization.

## Untested next step: pack across fixed interval boundaries

At 4M-QAM, the present 16-interval marker group uses 1,504 data symbols, 64 pilot
symbols, and 64 marker symbols: **1,632 symbols total**. There are 320 mapper fill
bits across the 16 separate 2,048-bit intervals. A future wire format could pack
the same fixed 32,768 coded bits across those boundaries, using 1,490 data symbols
and 12 fill bits, with four pilots after each group of at most 256 data symbols:
24 pilot symbols plus the same 64 marker symbols, or **1,578 symbols total**.

That arithmetic predicts **3.42% more throughput**, or **3.31% less steady-state
airtime**, with unchanged LDPC, RS, and marker count. It would save approximately
42 seconds on a 50 MB transfer at the current tested settings. It would require
new fixed framing and receiver scheduling, and its longer pilot gaps would need
new clock/phase and live reliability tests. **It has not been implemented or
tested** and is not included in the measured results or selected default.

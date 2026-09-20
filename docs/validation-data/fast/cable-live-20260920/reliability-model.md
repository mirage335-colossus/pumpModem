# Cable throughput/reliability planning calculations

These artifacts extend the earlier coding study to **100,000, 5,000,000 and
50,000,000 decimal source bytes**. They are analytical calculations, not new
hardware measurements. No runtime files, defaults, wire formats or tests were
changed to produce them. The regular modem compatibility contract is unaffected.

## Exact production airtime

[airtime.csv](airtime.csv) contains 6,912 calls to the existing production
`estimate_transmission` function: all three sizes, public/encrypted, 16/64/256-APSK,
all three existing convolutional rates, both existing RS strengths and every
depth from 1 through 64. [airtime-estimator.cpp](airtime-estimator.cpp) reproduces
the sweep. These are estimator results including bootstrap, nine-bit source
cells, coding/interval padding, training, markers, pilots, pulse tail and
**6.25 seconds of transmitted end silence**. They do not measure device queue
latency, additional receiver waiting, decoding stalls or delivery probability.

Public/unencrypted airtime in seconds:

| Settings | 100,000 bytes | 5,000,000 bytes | 50,000,000 bytes |
|---|---:|---:|---:|
| Existing wire default: 16-APSK, 3/4, robust RS, depth 16 | 41.929 | 1,726.836 | 17,195.124 |
| 256-APSK, 3/4, robust RS, depth 16 | 25.878 | 952.576 | 9,460.135 |
| 256-APSK, 7/8, robust RS, depth 16 | 23.202 | 823.533 | 8,170.970 |
| 256-APSK, 7/8, high-rate RS, depth 16 | 21.865 | 760.666 | 7,543.190 |
| 256-APSK, 7/8, high-rate RS, depth 64 | 23.625 | 742.550 | 7,346.586 |
| 256-APSK, 7/8, high-rate RS, depth 62 | 22.921 | 736.026 | 7,275.599 |

For 50 MB, depth **62** gives the minimum airtime in this exhaustive existing
settings sweep. Depth 64 loses a little efficiency through cycle rounding.
For the same 256-APSK/7/8/high-rate settings, the minimum for 100 KB is depth 6
(21.372 s), and for 5 MB it is depth 55 (735.111 s). A single depth chosen for
50 MB may reasonably accept the extra 1.549 s on 100 KB. The depth-62 50 MB
ceiling is **54,978 bit/s of source bytes**, or **2 h 1 min 15.6 s** per attempt.
This is a candidate for physical qualification, not an established optimum
for this cable.

For independent identical attempts with negligible retry turnaround, minimize
`T / P`, where T is attempt duration and P is whole-file success. A candidate
is better when `P_new / P_old > T_new / T_old`. For 50 MB, stepping through the
first four rows and then depth 62 gives the following break-even ratios:

| Change, with other settings retained | Minimum P_new / P_old for better goodput |
|---|---:|
| 16-APSK to 256-APSK | 0.550164 |
| Convolutional rate 3/4 to 7/8 | 0.863727 |
| Robust RS to high-rate RS | 0.923169 |
| Depth 16 to depth 62 at the high-rate setting | 0.964526 |

These ratios do not waive the separate requirement `P_new >= 0.8`. Include
retry turnaround W using `(T + (1-P)*W)/P` when it is material. Persistent
bad conditions invalidate an independent-retry interpretation.

## Whole-file reliability and sparse outer parity

[reliability-model.py](reliability-model.py) and its output
[reliability-model.json](reliability-model.json) retain the full calculations.

For the existing 256-APSK/7/8/high-rate/depth-62 public format, any unresolved
coding cycle prevents complete delivery. The following model assumes equal,
independent cycle failure probability q, including the bootstrap:

| Source bytes | Coding cycles | Maximum q for `(1-q)^cycles >= 0.8` |
|---|---:|---:|
| 100,000 | 10 | 2.20672% |
| 5,000,000 | 438 | 0.0509330% |
| 50,000,000 | 4,363 | 0.00511432% |

RS operates within these cycles. Average corrected-byte counts and BER cannot
be substituted for residual cycle failures. A burst, clock-tracking failure,
capture overrun or false synchronization can destroy many cycles together.

The earlier proposed LDPC geometry carries 6,292 source bytes per word after
an illustrative eight-byte integrity allocation. It is **not implemented** and
is not the current 32-byte SHA-256/HMAC layout. Model whole-LDPC-word failures as
detected erasures, with independent probability q for data and repair words:

`P_success = P[Binomial(K + r, q) <= r]`.

| Source bytes | Data words K | Repairs r = ceil(0.003 K) | Actual parity/data | Largest q for 80% success |
|---|---:|---:|---:|---:|
| 100,000 | 16 | 1 | 6.25000% | 4.87632% |
| 5,000,000 | 795 | 3 | 0.37736% | 0.287945% |
| 50,000,000 | 7,947 | 24 | 0.30200% | 0.260054% |

At q=0.3%, the 50 MB/24-repair model succeeds only **56.107%** of the time;
at q=0.1%, it succeeds **99.99989%**. At q=0.3%, expected-goodput optimization
instead selects **43 repairs**, or **0.54108% parity/data**, and predicts
**99.9849%** success. Accepting an 80% minimum does not make 80% the fastest
operating point: a small parity increase can avoid many full-file retries.
The optimization searches zero through ceil(10% of K) repairs, includes failed
repair words and excludes bursts, interruptions and fixed turnaround.

Conventional per-word GF(256) RS cannot directly express 0.3% overhead: at the
existing 128-byte codeword length, even one parity byte is 1/127 = 0.7874%
parity/data; correcting one unknown byte needs two parity symbols. Across a
maximum 255-symbol GF(256) word, one parity symbol still costs 1/254 = 0.3937%.
The proposed sparse protection therefore requires a different long-span layout,
such as GF(2^16) RS stripes across shards. It is not a small adjustment to the
existing `robust` boolean. Long protection groups, bounded allocation,
shortening, integrity, bootstrap loss and physical completion need design and
independent tests before such a format can be selected as a default.

The earlier LDPC study's zero failures in 800 words imply a one-sided 95%
upper FER of **0.373766%**, assuming independent fixed-point trials. At that
upper bound, 24 repairs for 50 MB give only **16.5766% modeled success**. This
does not prove the real FER is that high; it shows why those 800 ideal-AWGN
words do not establish this cable's 50 MB success probability. The coded
PCM front end, residual EVM, error bursts, device queues and CPU deadlines
were absent from that LDPC experiment.

For independent complete-file trials at one previously selected point,
**14 successes in 14 attempts** give a one-sided 95% success lower bound of
**80.7364%**; 13/13 give only 79.4183%. Fourteen 50 MB attempts at the current
fastest existing settings already require over **28 hours of airtime**.
One successful file is useful engineering evidence but gives only a 5% lower
bound by this method. Selection sweeps should be followed by held-out validation;
pooling trials across changing levels/settings, or treating correlated cycles
as independent files, overstates the evidence.

## Marker false-match calculation and scope

The production Fast detector uses:

```text
quality = |sum(x[k] * conj(marker[k]))|^2 / (N * sum(|x[k]|^2))
accept when quality > 0.72
```

The threshold is **squared normalized coherence**, not an amplitude threshold
of 0.72. For a symbol-aligned window of independent uniform, unit-energy QPSK,
the marker products have four equiprobable phases. If the four counts are
a,b,c,d, acceptance is exactly:

```text
25 * ((a-c)^2 + (b-d)^2) > 18 * N^2
```

The script counts all accepted sequences exactly with integer multinomial
coefficients and divides by `4^N`. It includes arbitrary rotation through the
phase-invariant magnitude. Thus a 64-QPSK-symbol marker does **not** provide
128 bits of false-match evidence at this tolerant gate.

| N QPSK symbols | -log2(per-window false-match probability) | -log2(union upper bound over 2^30 such windows) |
|---|---:|---:|
| 48 | 60.330 | 30.330 |
| 56 | 70.675 | 40.675 |
| 64, existing length | 80.735 | 50.735 |
| 72 | 90.683 | 60.683 |
| 80 | 99.427 | 69.427 |
| 88 | 109.957 | 79.957 |
| 96 | 120.345 | 90.345 |

The last column is a conservative **conditional** union bound, not a measured
file-error probability. The explicit 2^30 budget is a planning example:
the fastest current 50 MB waveform contains about 349 million 48 kHz samples.
Actual acquisition scans samples, uses timing refinement, and observes shaped
APSK data with coding, pilot structure and nonconstant amplitude. Those windows
do not follow this QPSK distribution. A union bound needs both a bound for each
actual hypothesis and the actual total search budget; the table establishes
neither for the production detector. Overlap alone is harmless to the union
bound, but a wrong per-hypothesis distribution is not.

Consequently, **do not shorten the existing marker on the assumption that its
nominal bit count guarantees a file-wide false-match probability below 2^-80**.
First define whether the requirement is per tested boundary or per complete
transfer, bound timing/phase hypotheses, and implement independent acceptance
evidence with a proven aggregate budget. Arbitrary deterministic source data
can deliberately contain any fixed public bit pattern; a random-data bound
also needs an explicit distribution or a constrained encoding that excludes it.
Subsequent checksum success cannot stand in for physical alignment evidence.

Keeping the existing marker length while transmitting it less often is a
separate throughput candidate. At 256-APSK, one marker every four physical
payload intervals reduces the average from 352 to 304 symbols per interval,
an asymptotic **15.789%** speed gain. Carrier/timing stability, retained erasure
positions, reacquisition, completion and marker-evidence accounting still need
tests. This is not implemented by this analysis.

## Reproduction and provenance

The production-estimator sweep linked the existing Release build at repository
commit `2af1136befdd8004e75198ad2f9b38149e51246b` on 2026-09-20 UTC:

```sh
c++ -O3 -std=c++20 -Iinclude \
  docs/validation-data/fast/cable-live-20260920/airtime-estimator.cpp \
  -o /tmp/fast-cable-airtime \
  build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl \
  build/third_party/xz/liblzma.a
/tmp/fast-cable-airtime > /tmp/fast-cable-airtime.csv
python3 docs/validation-data/fast/cable-live-20260920/reliability-model.py \
  > /tmp/fast-cable-reliability-model.json
```

Reproduction should use fresh output paths to preserve the archived evidence.
The linked `build/libdatapump_fast.a` SHA-256 was
`1368ab46a3cb0f957dcc6cdd0be459cb9c1666f6e86dd49c021ebdccb37cd2a3`.
The source SHA-256 values at generation were:

| File | SHA-256 |
|---|---|
| `src/fast/profile.cpp` | `60735a7f5eb898b7f547897538a50b7dccc677e24d8a51b1e2270ef142d3e11f` |
| `src/fast/codec.cpp` | `c61af954fb051964fef8122dc947b6c231e08148d1a385edd307dc754d5beeac` |
| `src/fast/modem.cpp` | `5c6a09ac73edc9658e9ce6619ee347f83824162e9afbb3c0967c1ef5104db38f` |

The model script was executed successfully, including its exact short-marker
and confidence-bound sanity assertions. No runtime regression claims are made
for these documentation/diagnostic-only artifacts.

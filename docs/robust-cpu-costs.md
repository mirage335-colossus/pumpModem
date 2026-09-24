# Robust Modem CPU mitigation cost and planning estimates

The selected receive CPU mitigations add small costs to the measured Robust
reception and recovery workloads. They do not change the search space, error
correction capability, wire format or physical completion rule. The larger
percentage changes in very short dictionary decoding are fractions of a
microsecond. These measurements inform an additional payload-processing term
in the shared Console and Link Planner estimate; they do not justify multiplying
all modem DSP work by the decoder overhead.

## Paired measurements

The same benchmark translation unit was linked against pre-mitigation revision
`f715839` and hardened revision `a062386`, using GCC 14.2 Release archives on an
AMD Ryzen 5 PRO 5650U Linux x86-64 host. The baseline already includes restricted
received-text presentation, so this comparison measures the CPU-mitigation
change, not the earlier character policy. Seventeen fixtures exercise short
interpretation, authenticated RS correction, exhaustive recovery, alignment
planning, full source decoding and sampled modem reception.

There are two seven-sample runs per version, in baseline/current/current/baseline
order. Each sample runs for at least 250 ms, except that one operation may take
longer. No competing build, test or benchmark was scheduled during these runs.
Each reported time is the mean of the two run medians. Source totals divided by
iteration counts retain precision for sub-microsecond cases. Each operation
checks exact results, correction/assignment counts or rejection, and sampled
receptions must complete by observed absence before EOF. Constructor, planning,
checking and teardown costs are included; transmitter setup is outside timing.

| Workload | Before | Hardened | Wall-time change |
| --- | ---: | ---: | ---: |
| One-bit incomplete dictionary token | 1.46 µs | 1.77 µs | about +22% |
| Three-bit `e` | 0.020 µs | 0.030 µs | about +52% |
| 98-bit, 16-byte dictionary message | 0.073 µs | 0.276 µs | about +280% |
| Authenticated RS60, clean interval | 17.96 µs | 18.16 µs | +1.14% |
| Authenticated RS60, 10 errors + 6 erasures | 44.09 µs | 44.71 µs | +1.42% |
| Authenticated RS60, 24 errors | 58.57 µs | 60.04 µs | +2.51% |
| 8,192 assignments, one worker | 205.56 ms | 206.42 ms | +0.42% |
| 8,192 assignments, four workers | 68.28 ms | 69.14 ms | +1.26% |
| Missing-marker alignment, 243 candidates | 12.01 ms | 12.17 ms | +1.32% |
| Wrong-address rejection, 243 candidates | 12.07 ms | 12.28 ms | +1.70% |
| Planning 32,769 all-missing positions | 70.12 ms | 72.60 ms | +3.54% |
| Keyed 256-byte clean stream, seven intervals | 985.68 µs | 987.51 µs | +0.19% |
| Keyed 256-byte damaged stream | 1.170 ms | 1.167 ms | −0.24% |
| Keyed compressed 64 KiB source, three intervals | 448.26 µs | 446.20 µs | −0.46% |
| Sampled FFT reception, full one-interval source | 588.21 ms | 590.11 ms | +0.32% |
| Sampled clock-window reception, three-bit source | 19.44 ms | 19.26 ms | −0.93% |

The remaining anchored recovery fixture, with one candidate and no missing bits,
changes from 111.43 to 111.92 µs (+0.44%). The sampled FFT pairs individually
change by −0.56% and +1.21%; these small differences do not establish a DSP
regression or speedup. The planning increase is more consistent: +3.43% and
+3.65%. These are repeated local timings, not confidence intervals or a bound
for all data, thermal states, compilers or processors.

[Raw samples, summary, calculation script and reproduction details](validation-data/robust-mitigations-2026-09-22/README.md)
retain both wall time and total process CPU time. Four-worker recovery uses
about 274 ms of process CPU time in 69 ms of elapsed time; wall time is the
appropriate denominator for its wall-clock search budget. It would be incorrect
to treat that elapsed time as the sum of the workers' CPU work.

## Effect on iterative recovery

Every completed benchmark searches the same candidates before and after the
mitigations and preserves exact results, authentication and original missing-bit
counts. The five-minute default budget, retained-bit limit, worker policy and
resumption behavior are unchanged. A fixed wall-clock budget can nevertheless
finish slightly less work when each candidate takes longer.

For this fixture, equivalent candidate throughput is
`attempts / elapsed_seconds`. Extrapolating that rate for 300 seconds gives:

| Workload | Before, candidates/s | Hardened, candidates/s | Before / hardened equivalent work in 300 s |
| --- | ---: | ---: | ---: |
| 8,192 assignments, one worker | 39,852 | 39,686 | 11.956 / 11.906 million |
| 8,192 assignments, four workers | 119,975 | 118,479 | 35.992 / 35.544 million |
| Missing-marker alignment | 20,231 | 19,966 | 6.069 / 5.990 million |
| Wrong-address rejection | 20,129 | 19,793 | 6.039 / 5.938 million |

This is equivalent repeated-fixture work, not a claim that the finite fixture
contains millions of candidates or that other error patterns have this rate.
Planning/startup is included in each repetition. The all-missing planning case
has no evaluated candidates, so it has no candidate-throughput extrapolation.
The one- and four-worker assignment rates decrease by approximately 0.42% and
1.25%, respectively; time overhead and throughput loss have different
denominators.

## Shared estimate adjustment

The previous model counted channel generation, carrier projection, acquisition,
tracking and startup. It did not count ordinary payload interpretation. The new
model adds that missing baseline work and a separately identifiable mitigation
reserve. Consequently the entire increase in the displayed estimate must not
be described as mitigation overhead.

Let `P = ordinary_baseline + mitigation_reserve`. The calculation is:

- Console CPU time = previous simulation CPU time + `P`.
- Link Planner workload ratio = `(previous receiver CPU time + P) / audio_seconds`.
- Hypothetical GPU time = previous GPU time + `P`, because payload processing
  remains on the CPU.
- `payload_processing_seconds = P`; `mitigation_seconds = mitigation_reserve`
  is a subset already included in `P`.

The allowance is charged once for a matching received stream, not once per
unrelated profile or key search bank. Empty/unmatched input has no desired-source
allowance. Costs are not discounted by reception probability. The existing DSP
operation budgets, probability calculation and 30 ms CPU startup allowance are
unchanged. The additive CLI `analyze-link` fields expose this accounting.

The rounded coefficients in `src/simulation_estimate.cpp` are:

| Component | Ordinary baseline | Mitigation reserve |
| --- | ---: | ---: |
| Nonempty reception | 2 µs | — |
| Raw/short retained view | 2 ns × min(wire bits, 4,096) | 0.05 µs boundary reserve |
| Eligible short interpretation, at most 208 wire bits | Covered by raw baseline | 0.4 µs + 3 ns × wire bits |
| Each fixed interval | 25 µs | 0.05 µs |
| RS20 correction, each interval | 20 µs | 5% of correction baseline |
| RS60 correction, each interval | 60 µs | 5% of correction baseline |
| Keyed stream processing, each interval | 100 µs | 0.02 µs |
| Original source output | 5 ns × source bytes | Covered by boundary reserve |
| Enabled source decompression | 25 µs + 2 ns × source bytes | 5% of decompression baseline |

There is no correction term with FEC off. The interval count is
`ceil(coded_bytes / 128)` from the existing locally generated transmission
estimate. No received length controls framing or allocation. The short allowance
also covers failed interpretation of a single raw bit; literal binary and
short dictionary transmissions with the same bits get the same cost. Longer
raw drafts skip dictionary interpretation and do not acquire interval correction,
authentication or decompression merely because those settings are saved.
Optional raw Data masking remains part of the common coarse receive allowance.

The 0.4 µs short reserve rounds above the measured one-bit rejection increase;
the per-bit term covers the longer dictionary example. The 5% repair reserve
rounds above the measured RS changes, including the earlier hardening study.
The RS60 baseline budgets correction near its error limit rather than a clean
syndrome pass. RS20 is an explicit extrapolation, not separately calibrated.
The keyed 100 µs term covers full-stream Data unmasking/setup and bookkeeping as
well as authentication; it is not an isolated HMAC measurement. Source copying
and decompression reserves use the full-source fixtures and the earlier
[receive-processing measurements](receive-processing-hardening.md#performance-validation).
These terms deliberately overestimate some clean or highly compressible cases.
They are not a worst-case bound for malformed input or GUI redraws.

For example, seven keyed RS60 intervals containing an uncompressed 256-byte
source receive about 1.320 ms of payload allowance, including 21.49 µs of
mitigation reserve. The damaged fixture measures 1.167 ms overall. Three keyed
compressed intervals representing 64 KiB receive about 1.058 ms, including
17.01 µs of mitigation reserve, versus 0.446 ms measured. This margin is a
planning choice, not a fit that claims every component was measured separately.

The Link Planner always evaluates one raw bit, independently of the Console
draft. Its new allowance is **2.455 µs**, of which **0.453 µs** is the mitigation
subset. Saved FEC and compression therefore do not cause an artificial drop in
one-bit headroom. The same allowance feeds the headline and every CPU curve
point. Larger Console drafts use their own source size and interval count.

For a public 8,000-sample/s, 1,000 Hz bandwidth, 1,500 Hz carrier, 16-chip
profile with 32 ms symbols and zero epoch-search seconds, the actual updated
model gives the following Console totals. These deliberately retain more
digits than the GUI so the small change can be seen. The previous value is
the new total minus `payload_processing_seconds`, since the DSP formula is
unchanged; it is not a timing measurement.

| Draft | Console CPU before → after | Added payload allowance | Mitigation subset |
| --- | ---: | ---: | ---: |
| One raw bit | 0.099988517 → 0.099990972 s | 2.455 µs | 0.453 µs |
| `quick brown fox `, 98 bits | 0.124224905 → 0.124227845 s | 2.940 µs | 0.744 µs |
| 256 `e` bytes, compression off, four intervals | 1.314259121 → 1.314614601 s | 355.480 µs | 12.200 µs |
| 65,536 `e` bytes, compressed, two intervals | 0.707019862 → 0.707689518 s | 669.656 µs | 13.904 µs |

The corresponding one-bit Planner workload ratio increases from 0.005982535
to 0.005982735 processing seconds per audio second; its green headroom category
is unchanged. Most displayed rounded values will be unchanged because the
added cost is small compared with DSP and startup. The allowance is still
included before rounding and before CPU category/curve calculation.

The one-bit example can be reproduced with:

```sh
build/dev/pump analyze-link --bits 0 --sample-rate 8000 --bw 1000 \
  --carrier 1500 --spreading 16 --time 1800000000 --search-seconds 0 \
  --clock-error-ppm 0 --phase-noise 0 --trials 10 \
  --tx-dbm 3 --attenuation-db -120
```

For the other rows replace `--bits 0` with `--text 'quick brown fox '`, or
`--input -` supplied with exactly the indicated number of `e` bytes; add
`--no-compression` for the 256-byte row. The default RS60 setting applies to
the framed rows. Divide `current_receiver.receiver_cpu_seconds` by
`transmission.simulated_seconds` for the workload ratio; use the one-bit row
when comparing with the Planner, regardless of the Console draft.

Exceptional missing-marker and exhaustive hard-bit recovery stay outside nominal
headroom. Their measured overhead is documented above, but their work depends
on actual damage, alignment and cancellation/resumption state. Inventing a fixed
300-second charge for every received bit, or multiplying all unchanged DSP work
by the 3.54% planning result, would misrepresent ordinary reception. Simultaneous
recovery can consume live-reception headroom beyond the displayed estimate.

## Scope of the extrapolation

The UI retains its fixed Intel Core i9-13900H reference and existing engineering
throughput budgets. These new coefficients are conservative, rounded allowances
informed by measurements on the AMD host; they do not establish measured
absolute performance on the named Intel laptop. There is no automatic benchmark,
hardware detection or CPU utilization measurement in the application. In
particular, the MSVC fence fallback and other architectures require separate
measurements; these GNU x86-64 figures cannot calibrate them.

The change affects planning numbers and explanations. It changes no receive
algorithm, protection, search limit, transmitted bit or accepted character.
Functional checks and timings do not prove immunity to speculative execution or
other CPU weaknesses; the scope of the implemented defenses remains described
in [receive processing hardening](receive-processing-hardening.md).

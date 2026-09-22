# Robust receive mitigation measurements — 2026-09-22

These measurements compare the selected CPU speculation mitigations with the
immediately preceding received-character-policy implementation. They cover
Robust Modem receive processing and exhaustive recovery, and support the
[CPU estimate update](../../robust-cpu-costs.md). They are local throughput evidence,
not a security test or a universal processor-performance guarantee.

## Retained evidence and provenance

- [`samples.csv`](samples.csv): all 476 final samples, in acquisition order.
- [`summary.csv`](summary.csv): the mean of the two run medians for each version
  and workload, with per-pair changes and fixed-budget throughput extrapolations.
- [`analyze.py`](analyze.py): validates fixture metadata and sample coverage,
  then reproduces the summary from the retained sample totals.
- Harness: [`tools/benchmark_robust_mitigations.cpp`](../../../tools/benchmark_robust_mitigations.cpp).
- Baseline library revision: `f715839b5b921576c15d2179e7fb1d51dc7194d5`
  (`DRAFT - limit received character set.`).
- Mitigated library revision: `a062386ef563bbd563ed06435d7611eda76ea10a`
  (`DRAFT - CPU mitigations.`), before the CPU-estimate changes.
- Host: AMD Ryzen 5 PRO 5650U, six cores / twelve logical CPUs; Linux x86-64,
  Debian kernel `6.12.107+deb13-amd64`; GCC `14.2.0-19`; Release `-O3 -DNDEBUG`.
  No machine-specific `-march` option, CPU affinity or frequency lock was added.
  This host is **not** the nominal reference processor named by the planner.

The exact compared binaries and harness had these SHA-256 hashes:

```text
6037df714df454ab4451975a728512cfe6a7dc907574c398cfe40ea155823da1  tools/benchmark_robust_mitigations.cpp
971679304ebb1a2b6a5bb35a07527031bb28156238dded2f7eaf65c5c65cb29d  build/cpu-hardening/robust-baseline
29af86ba74d59018ebae52cfc59e48a1b796701320225ccf30bfd0d4713860a7  build/cpu-hardening/robust-current
a299089d68ec9468e941353f52bb98b7779a6a005a8be344d07073f074f34caf  baseline/lib/libdatapump.a
7abdeae5c62488f71ff3b8020b522531ba344b2385ac401e5f7cb388464c9ca9  baseline/lib/liblzma.a
```

The baseline archives were preserved before mitigation development and their
hashes match the original `build/cpu-hardening/baseline.sha256` record. The
mitigated benchmark binary was linked before the later estimator build; its
binary hash identifies the measured artifact. That subsequent build replaced
`build/dev/libdatapump.a`, so its present hash is deliberately not presented as
the measured archive's hash. Local binaries and build logs remain ignored build
artifacts; the source, sample data and analysis are retained here.

## Reproduction

Build each revision in a separate checkout/output directory with the same
compiler and Release settings, using `./build.sh`. Preserve each completed
`libdatapump.a` and private `liblzma.a`; never compare a stale executable after
rebuilding only a library. The harness calls stable out-of-line library APIs and
needs no old inline mitigation header. The commands used with the preserved
local archives were:

```sh
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude \
  tools/benchmark_robust_mitigations.cpp \
  build/cpu-hardening/baseline/lib/libdatapump.a \
  build/cpu-hardening/baseline/lib/liblzma.a \
  -lcrypto -lpthread -ldl -o build/cpu-hardening/robust-baseline

c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude \
  tools/benchmark_robust_mitigations.cpp \
  build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a \
  -lcrypto -lpthread -ldl -o build/cpu-hardening/robust-current
```

Stop competing build/test workloads and run sequentially in ABBA order:

```sh
mkdir -p build/robust-cpu-estimates
build/cpu-hardening/robust-baseline 7 250 > build/robust-cpu-estimates/baseline-1.csv 2> build/robust-cpu-estimates/baseline-1.stderr
build/cpu-hardening/robust-current 7 250 > build/robust-cpu-estimates/current-1.csv 2> build/robust-cpu-estimates/current-1.stderr
build/cpu-hardening/robust-current 7 250 > build/robust-cpu-estimates/current-2.csv 2> build/robust-cpu-estimates/current-2.stderr
build/cpu-hardening/robust-baseline 7 250 > build/robust-cpu-estimates/baseline-2.csv 2> build/robust-cpu-estimates/baseline-2.stderr
```

The first three columns of `samples.csv` were prepended while concatenating
these four outputs: `(order,version,run)` is respectively `(1,baseline,1)`,
`(2,current,1)`, `(3,current,2)`, `(4,baseline,2)`. All other columns are the
unaltered harness CSV output. Each stderr reports 17 completed workloads.
Checksums differ between timed runs because adaptive batching changes operation
counts; the per-operation `result_signature`, retained bits and attempts agree
across every sample and version.

Reproduce the summary from the retained samples:

```sh
python3 docs/validation-data/robust-mitigations-2026-09-22/analyze.py \
  > /tmp/robust-mitigations-summary.csv
cmp /tmp/robust-mitigations-summary.csv \
  docs/validation-data/robust-mitigations-2026-09-22/summary.csv
```

`--smoke [workload_filter]` performs a correctness pass; a third numeric-mode
argument selects workload names containing that substring, for example
`robust-current 7 250 recovery`. The discarded preliminary timing set used
per-operation clock polling and allocated successful-check error messages. It
is excluded from `samples.csv` and all results here.

## Measurement and calculation

Each workload warms once before timing. Each version/run supplies seven
samples, each lasting at least 250 ms; actual sample durations were
0.250000022–0.636566737 seconds. Adaptive batches amortize clock-query overhead.
Every operation still verifies its result. Timing includes object construction,
planning, receive processing, correctness checks and teardown. Transmit fixture
generation and encoding are outside timing. There are no deliberate sleeps.

`steady_clock` measures elapsed wall time. `std::clock` measures total process
CPU time, including recovery workers. Four-worker CPU time can therefore exceed
wall time substantially. Wall time is the appropriate quantity for extrapolating
coverage under the application's wall-clock recovery deadline; process CPU time
shows the computational work consumed.

For each sample, calculate `wall_seconds / iterations` and
`cpu_seconds / iterations`. Take each run's median of these seven quotients,
then the arithmetic mean of its two run medians for each version. The reported
change is `100 × (current_mean / baseline_mean − 1)`. Pair 1 compares the first
baseline/current runs; pair 2 compares the second current/baseline runs. Their
spread is descriptive, not a statistical confidence interval.

The analysis uses sample totals and iteration counts, **not** the separately
printed per-operation durations, which are rounded to integer nanoseconds.
Otherwise the 20 ns short-code case acquires appreciable rounding error. Thus
the three-bit change is 51.67%, and the 98-bit change is 279.79%; large ratios
here still represent only about 10 ns and 203 ns of added work respectively.

For a fixture with `N` attempted assignments and mean wall time `T`, the
throughput estimate is `N / T`, and the five-minute extrapolation is
`300 × N / T`. These are equivalent attempts at this fixture's average cost,
not additional observed trials or a promise about another missing-bit geometry.
The full fixture includes preparation and teardown on every repetition; a
single longer search pays its initial planning cost only once. No extrapolation
is meaningful for the planning-only case, which has zero evaluable assignments.

## Workloads and observations

| Workload | Verified operation | Baseline → mitigated mean of wall medians | Change |
| --- | --- | --- | --- |
| One-bit short input | Reject incomplete dictionary token through ordinary exception handling | 1.455 → 1.773 µs | +21.88% |
| Three-bit short input | Decode exact `001` to `e` | 0.0201 → 0.0304 µs | +51.67% |
| 98-bit short input | Decode exact 16-byte `quick brown fox ` | 0.0727 → 0.2761 µs | +279.79% |
| Clean RS60 interval | 128 coded bytes, exact 48-byte authenticated source | 17.956 → 18.161 µs | +1.14% |
| Damaged RS60 interval | Ten errors plus six erasures; exact HMAC-verified repair | 44.086 → 44.713 µs | +1.42% |
| RS60 correction limit | 24 unknown byte errors; exact authenticated repair | 58.572 → 60.039 µs | +2.51% |
| Exhaustive recovery, one worker | All 8,192 assignments; exact authenticated source and 57 original missing bits | 205.559 → 206.420 ms | +0.42% |
| Exhaustive recovery, four workers | Same 8,192 assignments and exact result | 68.281 → 69.143 ms | +1.26% |
| Anchored recovery, no missing bits | One candidate, exact authenticated source | 111.427 → 111.922 µs | +0.44% |
| Missing-marker alignment | All 243 candidates; exact source at its real canonical address | 12.011 → 12.171 ms | +1.32% |
| Wrong-address rejection | All 243 candidates; no provisional decoded output | 12.072 → 12.277 ms | +1.70% |
| All-missing planning | 32,769 missing retained bits; no evaluable hypotheses | 70.117 → 72.599 ms | +3.54% |
| Clean stream source | Seven fixed intervals; exact 256-byte authenticated source | 985.684 → 987.509 µs | +0.19% |
| Damaged stream source | Seven fixed intervals, each with ten errors and six erasures | 1169.992 → 1167.176 µs | −0.24% |
| Compressed stream source | Three authenticated fixed intervals, exact 64 KiB decompressed source | 448.259 → 446.200 µs | −0.46% |
| Sampled FFT receive | 1,216 exact wire bits, full interval/source decode, observed physical completion | 588.214 → 590.112 ms | +0.32% |
| Sampled clock-window receive | Exact `001` and `e`, observed physical completion | 19.440 → 19.259 ms | −0.93% |

The sampled FFT fixture uses 6,000 samples/s, 1,200 Hz bandwidth, a 1,500 Hz
carrier and 16-chip symbols. Its 44.4495 seconds of supplied media include
nonzero capture delay and the full absence/acquisition tail. The compact
clock-window fixture uses 256 samples/s, 64 Hz bandwidth/carrier, a 16-second
symbol and 64 chips; it supplies 96.06640625 media seconds. Both use one scoring
worker, drain accepted bits into the real `StreamReceiver`, assert exact source
content, and require physical completion **before** calling `finish()`. They
exercise generated clean PCM, not live hardware or noisy channel calibration.

Mean process CPU / supplied media time was 0.013233 → 0.013276 for FFT reception
and 0.00020236 → 0.00020047 for clock-window reception. These are fixture ratios,
not measurements of the user's machine or complete GUI/audio application load.

The equivalent five-minute exhaustive-search counts are about 11.956 → 11.906
million assignments with one worker and 35.992 → 35.544 million with four
workers: approximately 0.42% and 1.25% less coverage respectively at these
fixture costs. The all-missing planning increase is **3.54%**, or about 2.48 ms
once for the tested capture; it is not a per-assignment penalty.

## Limits on interpretation

The sub-percent sampled/stream changes are comparable to run variation. The
sampled FFT pair changes were −0.56% and +1.21%; the one-worker exhaustive pairs
were +1.00% and −0.16%. Negative values are not evidence that hardening makes a
path faster. No blanket increase to every DSP operation is justified by these
results: the mitigation changes selected source/repair boundaries, while most
sample scoring is unchanged.

This comparison covers one GCC/x86-64 host, specific payloads and fixed recovery
geometries. It does not calibrate ARM barriers, MSVC's different fencing helper,
all FEC modes, every search space, pathological compression streams, native GUI
rendering, audio drivers or explicit file-saving costs. It does not measure the
nominal reference CPU used by the console/planner model. Any rounded allowance
used there is a stated engineering extrapolation from these stage measurements,
not a measured performance claim for that processor.

Scheduling, CPU boost, thermal conditions, shared caches and power limits can
still affect sequential runs. ABBA ordering reduces simple time drift; it does
not eliminate these effects. Neither performance data nor exact recovery
results establish immunity to speculative execution attacks or other CPU flaws.

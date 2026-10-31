# Helper complete-stream and inversion-timing evidence — 2026-10-31

This package preserves the experimental data supporting the latest
[integration guide](../../../helper-integration.md) and
[continuation study](../../../helper-continuation.md#sensitive-pooled-inversion-timing-experimental).
It includes failures and controls as well as successes. These are standalone
simulation results, not ordinary Robust integration, successful decoding,
hardware qualification or a physical sensitivity bound.

## What is retained

| File | Content |
| --- | --- |
| [manifest.json](manifest.json) | Configuration, counts, data hashes, available binary/source provenance, aggregation rules and omissions |
| [cases.jsonl](cases.jsonl) | 222 experiment arms: 80 loss-screen, 64 held-out, 50 mismatch/control, six stronger-transient, 16 delay/RF/probe variants and six serialized CPU repeats |
| [reservations.json](reservations.json) | 48 distinct public fixture schedule keys, corresponding noise seeds, reserved partitions and impairment combinations |
| [strict-inversion.json](strict-inversion.json) | Earlier strict individual-marker screen: 80 captures at ten losses, including individual marker errors and intervals |

Each main case has a stable `id`, exact study-executable `argv`, binary hash,
the original summary fields, and selected complete-output metric rows. No
successful-case filter was applied. The metric selection retains all-sample
results for clean twins, noisy probes, physical-boundary regions and APSK where
applicable; it omits most chronological debug rows and likelihood curves.
CPU repeats retain their summaries without duplicating waveform metrology.
Raw PCM, executables and bulk logs are intentionally not required.

The recorded campaign binary and the later accounting build are distinct. A
complete historical source/compiler/environment manifest was **not** recorded;
the partial source hashes and export commit in the manifest do not establish a
complete build identity. Rebuilding current sources and replaying an argument
list is reproducible experimentation, not a promise of the historical binary
hash or identical CPU timing. Original local-file hashes are provenance only;
the checker does not need those files. This package does not certify a release.

## Operating model and interpretation

The standard capture is 40 seconds at 24 kHz, with exactly 36 seconds for the
frozen frequency result and a fixed 38-second Helper lead. Held-out tone or
360 Hz shaped APSK probes occupy capture seconds 38–40; delayed output drains
after capture. The Helper is 40 W and the independent probe is 40 W: **80 W
combined** when simultaneous. Thermal noise uses 290 K and 10 dB noise figure.
The standard HF schedule uses cryptographic-keystream random hops over 50
channels at 50 kHz spacing around 30 MHz, with independent audio/RF errors,
drift and phase noise. Mode-specific overrides are identified in each case's
arguments and summary and implemented in the tracked study/fixture sources.

The receiver uses Helper observations only. No probe or simulator truth supplies
its synchronization, boundary selection, correction or adaptive alignment.
Clean-twin/oracle diagnostics use unavailable truth and are not receiver results
or proven physical bounds. The pre-probe synthetic tone is evaluator-only.
One evaluator reference is retained; there is no per-hop/window realignment.

Compare `nominal` against `pooled-inversion` on the same capture. Only timing
selection changes in this comparison; the frozen frequency result is unchanged.
Pooling uses independent RF phase for each hop and shared clock information.
Rolling timing updates are correlated and must not be counted as independent
captures. Delay can improve an unreleased sample using later Helper observations;
it cannot improve a result labeled available at 36 seconds. Sample capture dates,
release dates and latest evidence dates remain distinct.

All captured data is passed through complete-dwell correction, including uncertain
samples outside the pilot aperture. `well_supported` is a confidence label, not
an output mask. Actual missing input remains an explicit gap. Zero clean energy
has undefined phase and is counted separately. Beginning/interior/end regions
are disjoint; the overlapping boundary region must not be added to them when
computing total RMS. Per-case percentile medians are not pooled percentiles.

## Principal recorded comparisons

| Quantity | Nominal schedule → pooled inversion, or conditional result |
| --- | --- |
| 32 fresh HF cases at 40 W / 180 dB, calendar timing at 36 s | 32/32 qualified and intervals covered; RMS 0.441 ms, worst 0.965 ms, median half-width ±2.050 ms. No individual marker qualified. |
| Frequency at exactly 36 s, same 32 cases | 31/32 available, all 31 intervals covered; RMS 0.01357 Hz, worst 0.02887 Hz, median half-width ±0.11695 Hz. Identical between methods. This does not prove universal ±0.1 Hz reliability. |
| All-output clean-twin phase RMS / wrong-hop outputs | 8.843° → 6.859° / 166,929 → 76,553. All 30,721,545 emitted samples received phase/clock correction in each arm; 6,069,369 retained the conservative support label. |
| Beginning / interior / end phase RMS | 20.074° → 14.045° / 4.5255° → 4.5256° / 4.805° → 4.575°. Boundary-region RMS 14.593° → 10.443°; worst errors still approached 180°. |
| Eight matched cases each at 180/175/170/165/160 dB | All calendar estimates qualified and covered at 36 s. Timing RMS was 276/179/100/55/47 µs; median half-width was 2169/1265/748/550/356 µs. These use different keys from the 32-case set. |
| Negative controls | 24 absent-marker, straight-auxiliary and noise-only captures: no individual/pooled timing qualification or selected marker boundary. |
| Independent RF retune 2 ms early | Wrong-hop outputs worsened 1,224 → 4,976 despite useful transmitted-calendar timing. A marker does not measure arbitrary independent hardware retune delay. |
| One 180 dB APSK comparison | Clean-twin EVM 0.057960 → 0.051444; noisy-probe EVM 0.55321 → 0.555159. A clean-twin improvement is not a noisy-probe improvement. |
| Exactly 36 s tracking, all threads | 0.336–0.374 process CPU seconds including inversion; complete 40 s conditioning plus drain had median total receiver CPU 0.703 s versus nominal 0.507 s. |

The record also retains delay variants (0.4/1/2/3 s), wide-hop microwave cases,
no-pilot-ramp operation, phase/gain/settling disturbances, steps, curvature,
gaps, system-clock adjustments, differential-phase violations and whole-cycle
timing jumps. Some remain severe failures. Arbitrary differential phase can
imitate audio-clock evolution. See the continuation study for these limits,
actual capture-to-release latency, memory bounds and detailed CPU partitions.

The earlier strict detector screen qualified individual markers only in some
cases at 120–128 dB and none at 130–180 dB. Its microsecond accuracy conditional
on detection is **not** the sensitivity of the later pooled detector. Neither
screen establishes a physical limit or exhaustion of inexpensive approaches.

## Check and reproduce without local historical artifacts

The Python utility uses only the standard library and this package. Verification
checks recorded data integrity and consistency; it does not rerun the receiver
or qualify its behavior. Summary recomputes aggregates from the retained cases.

```sh
python3 tools/helper-evidence.py verify
python3 tools/helper-evidence.py summary
python3 tests/test_helper_evidence.py

./build.sh configure
cmake --build build/dev --parallel 2 --target test_helper_complete_study
# Replay one exact recorded case with the current build into a NEW directory.
python3 tools/helper-evidence.py run --case screen/000 \
  --binary build/dev/test_helper_complete_study \
  --output local-evidence/helper-replay-screen000
```

The replay command records the new binary hash and output separately and refuses
to overwrite existing evidence. Use the `argv` arrays to reproduce other cases
or the [study commands](../../../helper-continuation.md#work-bounds-checks-and-reproduction)
for matched sweeps and applicable focused receiver checks. CPU comparisons must
be serialized without concurrent compilation/simulation and must distinguish
tracking, actual conditioned duration, generation and metrology; overlapping
component counters must not be added twice.

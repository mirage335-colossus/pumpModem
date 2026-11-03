# Selected −20/−18 dB-Hz followup for ×1000 spreading

This separate record contains **two additional plans, 36 sampled condition
captures and 108 receiver scores**. They reuse the same 16 signal paths and
control seeds as the [earlier −16 dB-Hz experiment](additional-README.md).
The new SNR points were selected after that result; they are a paired
sensitivity followup, **not a new independent validation cohort**. The first
nine-plan and additional four-plan records remain unchanged.

Independent recounting matched saved JSON/NPZ scores, normalized public-group
mixtures and all admission outcomes. All configure, export and score jobs
completed. There were no wrong-bit admissions, null admissions or wrong-key
admissions. The [summary](frontier-summary.json) preserves per-cell counts,
pointwise uncertainty, signal energies, paired identities, exact CFO scores,
timings and provenance. Ordinary-receiver integration remains outside scope.

## Observed sensitivity bracket

Entries are correct / wrong / missed for the 16 signals. The −16 dB-Hz row is
the previous result, included for comparison rather than added to this
package's denominator.

| C/N₀ dB-Hz | Global + public CFO: strict | Global + public CFO: relaxed | Global, zero CFO | Independent windows |
| --- | --- | --- | --- | --- |
| −20, new | 5 / 0 / 11 | 8 / 0 / 8 | 0 / 0 / 16 at both gates | 0 / 0 / 16 at both gates |
| −18, new | 12 / 0 / 4 | 15 / 0 / 1 | 0 / 0 / 16 at both gates | 0 / 0 / 16 at both gates |
| −16, prior | 16 / 0 / 0 | 16 / 0 / 0 | 1 / 0 / 15 at both gates | 0 / 0 / 16 at both gates |

The public-CFO method chooses the correct **raw candidate bit on all 16
paths at both new SNRs**. Its misses are below the fixed admission threshold;
they are not raw bit reversals. In contrast, before admission, the window
method chooses the wrong bit on 7/16 and 5/16 paths at −20/−18 dB-Hz, and
the zero-CFO global method on 4/16 and 3/16. Those incorrect choices are all
rejected. Raw correctness alone does not justify lowering an admission gate.

The paired CFO acceptance sets grow monotonically across the three sampled
SNRs. Strict reception gains seven paths from −20 to −18 and four more at
−16; relaxed reception gains seven and then one. This gives a useful
**empirical −20/−18/−16 dB-Hz transition bracket** at one fixed duration.
It does not locate a qualified 90%/99% sensitivity boundary. For example,
the pointwise one-sided 95% lower bounds are 51.56% for 12/16, 73.60% for
15/16 and 82.93% for 16/16 under the independent/common-probability assumption.
They are not simultaneous guarantees across the selected SNR grid. The same
one null and one wrong-key path are reused at each SNR; the controls remain
far too small for rare operational admission qualification.

## Energy benchmark and unresolved loss

The benchmark uses the actual post-channel, post-erasure waveform energy,
with all waveform, channel, bit, phase, synchronization and white Gaussian
noise parameters known. With
`rho = energy_seconds × 10^(C/N0/10)` and
`alpha = exp(−actual_log_gate)`, ideal presence detection has
`Pd = Phi(sqrt(2rho) − norm.isf(alpha))`.
The strict gate is 22.72106339, allocating alpha = 1.35633681×10⁻¹⁰ and
requiring rho = 37.3309873 for 99% detection. The relaxed gate is
17.42274602, allocating alpha = 2.71267361×10⁻⁸ and requiring rho =
22.5679582 for 90%. Each capture is charged its declared search-family
allocation, rather than the entire daily or 12-hour false-admission budget.

| C/N₀ | Actual rho range | dB above strict 99% known-waveform energy | Mean genie Pd: strict / relaxed |
| --- | ---: | ---: | ---: |
| −20 | 40.64765–72.17947 | 0.370–2.863 | 0.999651268 / 0.999985298 |
| −18 | 64.42219–114.39676 | 2.370–4.863 | 0.999999984 / 0.9999999999 |
| −16, prior | 102.10228–181.30664 | 4.370–6.863 | approximately 1 / 1 |

Even the weakest −20 dB-Hz path has genie strict Pd = 0.996554. Thus the
practical misses are not explained by total energy insufficiency under this
favorable benchmark. Unknown channel/synchronization, finite-bank mismatch,
likelihood modeling and admission calibration still cost useful information.
The experiment does **not** isolate those costs or establish an exact
implementation loss in decibels. In particular, the timing grid's stipulated
random-chip/infinite-RRC approximation permits 2.3837 dB nearest-grid loss;
that is an approximation, not a measured loss for these finite waveforms.
The selected two-ray likelihood also does not become the known complete
Each65-modulated waveform used by the genie.

The cohort reaches 16/16 strict admissions after a 4 dB increase from −20
to −16, a factor 2.512 in received energy at unchanged duration. It reaches
15/16 relaxed after 2 dB, a factor 1.585. These measured SNR changes must not
be presented as equivalent transmit-duration factors: no duration sweep,
longer coherence validation or planner qualification was performed here.

## Unchanged physical case and computational cost

All seven original seed arguments, actual signal energies and source/sample
calendars match the earlier −16 dB-Hz cohort exactly. Only the requested
C/N₀ and corresponding noise amplitude change. Per-path post-channel energy
is 4,064.77–7,217.95 energy-seconds; it was not renormalized to conceal channel
gain variation.

Configured rate is 0.1 Hz with Added Symbols ×1000, yielding 16.6667 fine
symbols/s/carrier. Requested eight seconds still rounds to **3,842 seconds
of source calendar and 3,843.3984375 seconds of sampled observation**:
491,955 complex samples at 128 samples/s. This is approximately 64 minutes,
not an eight-second transmission. The physical model remains 30 MHz,
1,725 Hz nominal IF, GPSDO OCXO endpoints, margin 1, HF-best 0.1 Hz/0.5 ms
two-ray spread and Each65 additional modulation, with FHSS and internal
waits off.

The unchanged public bank contains 15 arrival positions × 95 CFO positions
= 1,425 groups. All groups participate, without simulator-truth pruning.
Original blocks last one second; the independent-window alternative uses
two-second windows. The original-block phase-arc power-loss bound remains
0.00038582 dB, below the unchanged 0.01 dB allowance. Neither that bound nor
these reception results qualify clock-rate or keyed-epoch acquisition.

| C/N₀ | Median export time/capture | Retained workspace/capture | Median CFO receiver time | Export stage / score stage elapsed |
| --- | ---: | ---: | ---: | ---: |
| −20 | 17.545 s | 22.353 MB | 0.6933 s | 106.480 / 35.324 s |
| −18 | 17.604 s | 22.353 MB | 0.7062 s | 106.738 / 35.635 s |

Each stage used three concurrent jobs and one CPU thread per child. Stage
times are reported separately; they are not asserted to be a single summed
campaign wall time. These are measured CPU costs under host load, not GPU
acceleration or controlled hardware-scaling measurements. Bulk receiver
arithmetic remains FP32 with explicit FP64 small preparation/reductions.

The record demonstrates a practical sensitivity transition for this
hour-scale, ×1000-spreading configuration. It establishes no new −60 dB-Hz,
multi-day, general propagation, wrong-key rejection or ordinary-receiver
qualification. No further campaign is part of this followup.

## Reproduce the recount only

Restore the original compact reports before running this recipe. Available
JSON/NPZ scores, manifest data, group mixtures and outcome counts were checked.
Raw grouped statistics are absent from the compact package; their original
receipt hashes were retained but their bytes were not independently re-read.
Use a fresh local output directory; these commands execute no receiver or
waveform simulator and regenerate structured results rather than this prose.

```sh
STUDY=docs/validation-data/triad-v3/2026-11-03-extension/physical/evidence
REPORTS=local-evidence/triad-extension/remote/maxspread-frontier-reports
OUT=local-evidence/triad-extension/frontier-analysis-reproduction
PYTHON=/tmp/triad-weak-env/bin/python
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 "$PYTHON" "$STUDY/analyze_physical.py" --input "$REPORTS" --output "$OUT/frontier-full-analysis.json"
"$PYTHON" "$STUDY/frontier_describe.py" --input "$OUT/frontier-full-analysis.json" --reports "$REPORTS" --prior "$STUDY/additional-summary.json" --output "$OUT/frontier-summary.json"
```

NumPy and SciPy are required. The frontier summary supplies its own scope
instead of the shared analyzer's historical first-package limitations text:
zero execution failures here, with the 0.01 dB allowance throughout. The
first package's six failures and microwave 0.02 dB arm remain separate.

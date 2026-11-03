# Independent long-span cohort and selected duration/spread followups

All **80 additional sampled captures** completed and pass the independent
record/status/score/manifest audit. This snapshot is separate from the
[initial 152 captures](../README.md), whose files and denominators are preserved.
No receiver or simulation was rerun for this review.

| Cell, Each65 at −60 dB-Hz | Signal count | Strict correct / wrong / miss | Relaxed correct / wrong / miss | Null / wrong-key admissions |
| --- | ---: | ---: | ---: | ---: |
| A: 850-day request, Sub9 best | 32 | 30 / 0 / 2 | 31 / 0 / 1 | 0/4 / 0/4 |
| B: 425-day request, Sub9 best | 16 | 11 / 0 / 5 | 12 / 0 / 4 | 0/2 / 0/2 |
| C: 850-day request, Sub9 moderate | 16 | 16 / 0 / 0 | 16 / 0 / 0 | 0/2 / 0/2 |

A uses **32 new signal seed tuples**, disjoint from both initial cohorts.
B and C use the same 16-tuple subset of A. Thus A is an independent cohort
for the selected configuration; B/C are paired followups, not additional
independent validation of A. Changing sample rate changes stochastic sample
consumption, so equal roots do not preserve identical continuous paths.

The [summary](summary.json) includes each capture, both interval methods,
energy benchmarks, timing and exact bindings. [analysis.py](analysis.py)
recounts only retained files. The primary cohort improves evidence beyond
four trials, but **does not establish 99% or 90% reception** at 95% confidence:

| Cell/gate | Exact two-sided 95% interval | Wilson 95% interval |
| --- | --- | --- |
| A strict, 30/32 | [0.792, 0.992] | [0.799, 0.983] |
| A relaxed, 31/32 | [0.838, 0.999] | [0.843, 0.994] |
| B strict, 11/16 | [0.413, 0.890] | [0.444, 0.858] |
| B relaxed, 12/16 | [0.476, 0.927] | [0.505, 0.898] |
| C both gates, 16/16 | [0.794, 1] | [0.806, 1] |

These intervals assume the declared per-cell sampling, not pooled IID paths
across cells. Zero admissions in four controls gives a one-sided 95% upper
probability of 52.7%; zero in two gives 77.6%. The controls do not qualify
the operational rare-admission targets.

## Actual durations, sampling and the spread comparison

All cells use Rate0.001, Added Symbols×1, encrypted clock-derived patterns,
RF0/IF0.0005 Hz, GPSDO OCXO/baseband-clock, Each65, margin1, tight timing,
and the same whole-observation two-ray receiver. B=0 describes constant
unknown receiver gains, not zero physical Doppler. Exact preset delay/Doppler
taps are supplied as public basis hypotheses. No realized phase oracle is
used. All banks have two integer lags × three public CFO values; epoch,
clock and drift hypotheses are fixed.

| Quantity | A | B | C |
| --- | ---: | ---: | ---: |
| Ray separation / excess delay | 0.001 Hz / 0.005 ms | 0.001 Hz / 0.005 ms | 0.005 Hz / 0.05 ms |
| Source calendar | 853.334 d | 426.667 d | 853.334 d |
| Observation | 854.519 d | 427.852 d | 854.491 d |
| Sample interval | 128 s | 128 s | 32 s |
| Complex samples | 576,800 | 288,800 | 2,307,126 |
| Explicit arrays | 608.50 MiB | 305.57 MiB | 2433.02 MiB |
| Declared array budget | 1 GiB | 1 GiB | 4 GiB |

C raises sampling rate **fourfold**, as well as changing ray separation,
delay and the selective-modulation rates coupled to spread. Its 4-million-row,
4-GiB plan was frozen separately; the source calendar remains unchanged.
It is not a pure Doppler ablation. The 16/16 outcome does **not** show that
higher Doppler improves reception. Its weakest score is **22.81188**, only
**0.09082 nats** above the strict gate of 22.72106.

These are actual long simulation coordinates, not short-run extrapolation,
but retain the initial study's point-sampling limits. A/C are not a
physical-time-aligned sampling refinement. No high-rate integration/decimation
equivalence, multi-year field stability, arbitrary stochastic ray renewal,
wide blind channel search, millisecond erasures or real RF retuning is
qualified by these cells. Preset Wiener phase remains active; the additional
low-rate oscillator spectra remain zero as declared by that model.

## Seven strict misses and available energy

All seven strict misses select the **correct raw bit**. All frontends and
erasure masks are off in these cells, so reported sampled channel energy
divided by thermal variance is a valid conditional `rho` for the sampled
model. The known-complete-waveform presence benchmark uses the same allocated
score alpha, `exp(-gate)`, and known phase/channel/arrival. It is not the
achieved correct-bit probability or a universal minimum duration.

| Capture | Strict score | `rho` | Known-waveform strict Pd | Energy above strict99 genie |
| --- | ---: | ---: | ---: | ---: |
| A trial2 | 14.419 | 58.077 | 0.999996 | +1.92 dB |
| A trial27 | 21.377 | 84.268 | >0.9999999999 | +3.54 dB |
| B trial0 | 11.850 | 49.479 | 0.999860 | +1.22 dB |
| B trial1 | 22.224 | 56.448 | 0.999992 | +1.80 dB |
| B trial11 | 14.435 | 46.165 | 0.999507 | +0.92 dB |
| B trial2 | 3.084 | 28.994 | 0.903311 | −1.10 dB |
| B trial4 | 14.064 | 36.886 | 0.988537 | −0.05 dB |

A trial27 and B trial1 pass the relaxed gate; the other five remain missed.
The strict99 known-waveform energy requirement is 37.331; the relaxed90
requirement is 22.568 under their actual allocations. Per-capture target
benchmarks and separately named 99% diagnostics are retained in the summary.
Some practical misses retain substantial genie energy, establishing a remaining
model/acquisition gap; other half-duration paths have less energy than strict99
requires even with known waveform. This does not isolate the gap's cause or
justify an additional decoder algorithm without a matched diagnostic.

Actual `rho` ranges are 58.08–136.97 for A, 28.99–68.27 for B and
58.10–137.25 for C. B approximately halves available energy, and its
reception fraction declines on these paired roots. This supplies a measured
duration contrast; it does not establish an optimum duration, a 99% planner,
or a universal −60 dB-Hz boundary.

## Cost, binding and reproduction

The campaign took **215.755 seconds with four CPU workers**, one thread per
child. Export / complete score CLI ranges were:

- A: 2.74–3.22 s / 2.36–6.02 s.
- B: 1.36–1.63 s / 1.81–2.54 s.
- C: 10.71–12.31 s / 5.93–12.91 s.

Concurrent jobs and different array sizes prevent treating these ranges as
isolated throughput or hardware scaling qualifications. Array estimates
exclude interpreter and allocator overhead. The full raw input inventories
bind **10,873,625,400 bytes**; this compact review rehashed manifests and
reports, not those omitted raw arrays. Full group-score tables were omitted,
so the review independently recounts decisions/calibration allocations and
geometry, not the underlying mixtures or residual UCB computations.

The archive is 190,542 bytes, SHA-256
`a5063321694e4b1d3aa4459eaf5ebca358656fd55bd7a4bb0fd38675e1edad78`.
Plan SHA-256 is `3f011e6555e3ec8313b2dfaef0fdeb435123e69ca46d6dc4e72471da4319a307`.
Every run binds historical receiver `f91265b9...` and Gaussian helper
`b22fe002...`, with unchanged recorded source/binary identities.

After the campaigns completed, the coordinator added one arithmetic-report
field to the local FFT source, producing
`d4a3c2cc39e0068e6aa6aab27d7ee8eeff6cc7db72a790868fe166c3f1d65843`.
That reporting-only variant explicitly names device FP64 whitening/reductions;
it changes no arithmetic. The review records the current/hash difference,
retains historical experiment binding and does not rescore or label it an
artifact mismatch failure.

Reproduce this read-only audit from the repository root:

```sh
/tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-03-fft-timing/physical/followup/analysis.py \
  --campaign local-evidence/triad-fft-timing/campaign/remote-followup/followup \
    local-evidence/triad-fft-timing/campaign/long-followup-preflight-002/plan.json \
  --archive local-evidence/triad-fft-timing/campaign/compact-followup.tar.gz \
  --output local-evidence/triad-fft-timing/followup-review.json
```

The initial summary is read only to verify cohort separation. No original
evidence is overwritten. Ordinary Robust integration remains outside scope.

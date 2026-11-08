# Helper / three-carrier GPU, mixed HF clocks and lower-IF FHSS

This extends the [earlier sampled study](../2026-11-04-helper/README.md) and
[maintained guide](../../../triad-helper.md). It remains isolated from ordinary
Robust, GUI/live audio and physical RF retuning. Measurements used an RTX PRO
6000 Blackwell Workstation Edition (96 GiB), PyTorch 2.8.0/CUDA 12.8 and an EPYC
9355 host. CPU comparisons limit numeric libraries to four threads. Local
Helper/grid diagnostics used the Ryzen host with two numeric threads.

## Measured GPU benefit

The exact earlier EME observation and templates were replayed on CPU and CUDA.
Three paired runs alternate execution order; the first includes lazy cache and
kernel setup, the last two reuse all 13 epoch workspaces. CUDA is synchronized
at timing boundaries. Bulk correlations/products use complex FP32; sensitive
Gram preparation, whitening and reductions remain FP64. TF32 is disabled.

| Measured stage | CPU | CUDA | CPU / CUDA |
| --- | ---: | ---: | ---: |
| Original receiver, resident complete paired comparison | 8.403 s | about 7.1 s | 1.18× |
| Batched receiver, resident full-bank FFT correlations | 3.025 s | 0.0370 s | 81.7× |
| Batched receiver, resident full-bank DATA search | 3.111 s | 0.0819 s | 38.0× |
| Batched receiver, resident complete paired comparison | 3.569 s | 0.446 s | 8.0× |
| Batched receiver, first comparison plus constructor | 8.775 s | 5.791 s | 1.52× |

“Complete paired comparison” includes Helper estimation, whole-capture FIR,
both full/narrowed DATA searches, I/Q loading/hashing and finalization; source
generation and Python process import/startup are excluded. Cold is **in-process**,
not cold machine/process startup. These are descriptive replays, not independent
signal receptions or confidence intervals. Bits, winning epochs, timings and
CFOs agree between CPU/CUDA; the batched receiver's maximum per-bit likelihood
discrepancy is 2.29e-5 (the original receiver's is 2.55e-5).

The improvement comes from caching public residual/channel factors, resident
FFT templates, frequency batching and avoiding per-frequency CPU preparation
and transfers. An aligned CFO lattice additionally permits exact cyclic shifts
of one observation FFT; arbitrary CFOs use batched modulation/FFTs. No weaker
arithmetic is hidden under FP32. The new global residual-confidence allocation
also changes multi-epoch scores relative to legacy: it is **not** merely a
numerically identical optimization and needs its own admission calibration.

## Free-running baseband and IC-7100 HF

The requested 4 W DATA / 40 W Helper / 180 dB link, Rate 36, Added ×10,
interleave, 30 MHz RF, HF worst ×3, Each 65%, no FHSS and no Symbol Burst is
retained. DATA IF is 1725 Hz. Canonical endpoint oscillator draws are independent:
audio ±50 ppm per radio, RF ±0.5 ppm per radio. Margin widens the search, not
these draws. Selected spread is 90 Hz / 21 ms. Physical DATA C/N₀ is 20.0206
dB-Hz at −164 dBm/Hz; `--cn0 8` separately raises noise to test 8 dB-Hz.

Public margin-3 bounds are CFO ±90.522026 Hz and relative audio clock
±300.015001 ppm. The receiver also retains a separate `1/RX-clock` nuisance
grid; Doppler and modulation offsets must not be stretched by the TX/RX source
clock ratio. Helper jointly fits both pilot combs with this distinction.
Whole-capture 400/440-Hz FIR pass/stop edges retain the broader DATA search;
an explicit 250 ms capture extension preserves its causal tail.

The full GPU bank contains **17,035 CFOs × 3,168 timing positions × 13 epochs
× 7 audio-clock values × 3 RX-rate values = 14,732,958,240 groups**. Both source
bits are compared within each group. Every declared group is evaluated; no
score-based pruning or truth-based alignment is used. The DATA likelihood
still fits only two complex ray coefficients and omits selective-AM structure.

| Complete sampled case | Full DATA processing | Assisted DATA processing | Helper estimator | Raw bits |
| --- | ---: | ---: | ---: | --- |
| Physical link, seed 94101, transmitted 0 | 406.015 s | 89.375 s | 2.986 s | both correct |
| 8 dB-Hz, seed 94111, transmitted 1 | 405.769 s | 88.855 s | 3.130 s | both correct |

These DATA times include fresh lazy template/FIR/Gram preparation with cache
capacity one. Whole-capture filtering adds about 0.569 s to either arm; shared
constructor cost is about 0.228 s. Thus the measured complete cold receiver
benefit is approximately **4.4×**, not the much larger candidate-count ratio.

Both cases reduce CFOs **17,035→5** and clock values **7→3**, retaining all
epochs, lags and three RX-rate alternatives: **7,949.7× fewer groups**. In the
physical case, actual DATA search work excluding template preparation decreases
**198.864→1.246 s (159.6×)**; preparation is **206.856→88.035 s**. This identifies
the remaining CPU preparation cost rather than attributing it to FFT execution.

A separate full physical-link replay retained all 273 public workspaces.
Explicit prewarming cost **207.302 s**; subsequent baseline/assisted DATA scores
took **198.077 / 1.188 s**, with no new hypothesis preparation. Including the
0.571 s whole-capture filter and 2.965 s Helper estimate gives **198.648 versus
4.725 s (42.0×)** for repeated receiver processing. Both again selected the
correct bit and the same winning candidate. Peak CUDA allocation was 1.635 GiB
(1.789 GiB reserved); host factors are additional. This is one full resident
replay, not a confidence-qualified latency distribution. The 207 s prewarm is
amortizable only while the public templates and search geometry remain valid.

The physical case preserves the full-bank winning candidate. At 8 dB-Hz the
winning CFO is retained and both bits are correct, but the working maximum
changes clock/timing: the full bank prefers +200 ppm and the narrowed bank
+100 ppm despite true clock ≈−0.51 ppm. The mismatched two-ray model and finite
timing grid can favor nuisance values far from truth. This is **not** evidence
that narrowing preserves every full-bank maximum or improves bit sensitivity.
These two full receptions are raw bit decisions, not calibrated admissions.

The separate Helper diagnostic initially used an endpoint grid of 16,803 CFOs.
Eight frozen initial captures produced six narrowings and two optimizer-limit
fallbacks. Practical optimizer convergence tolerances resolved those two while
retaining the original six selected banks; maximum paired score change was
2.59e-6. Eight paired replays plus **two fresh captures** then all narrowed to
4–6 CFOs and covered true CFO/audio-clock values. Maximum errors were 0.000596
Hz / 0.592 ppm. Local estimator times were 9.28–10.87 s. Only the final two are
independent of the convergence correction. These engineering intervals have
no calibrated coverage probability or global continuous-search guarantee.

Independent review also caught an ineffective half-window guard: a 0.10 Hz
frequency step could be mistaken for a sidelobe. Each half now performs its
own full-prior search, and both 0.06/0.10 Hz step controls fall back. Failed
optimizers still force fallback; tolerances were not widened to hide ambiguity.

## Work forecast and clock-grid diagnostics

Before the first full HF run, a fixed public epoch/clock/rate work ladder tested
64, 256 and 1,024 CFOs at full timing geometry. The 64/256 fit predicted the
held-out 1,024-size GPU cost within 0.014%; CPU within 1.90%. Batch 32 performed
similarly to 128 and better than 8. The frozen full GPU forecast, including
per-hypothesis preparation, was **397.956 s**; the independent full run took
**406.015 s**, 2.02% above forecast. The corresponding CPU prediction is
8,196.9 s, but that full CPU workload was **not run**. It must not be presented
as a measured CPU/GPU full-HF speedup or a proven future prediction bound.

Public-template clock/rate probes cover 16 joint cell midpoints/outer corners,
all 13 epochs and both bits: 416 two-ray subspace comparisons, 347.79 s local
CPU. Worst sampled weaker-direction energy retention is 0.90093 (−0.453 dB).
This optimizes **continuous common retiming** and excludes the actual four-sample
arrival-lattice loss, CFO mismatch, selective modulation and noise. It is not
a complete-bank or continuous-parameter bound. An initial probe incorrectly
limited absolute retiming; its superseded result is retained and not used.
The exact launch-time source hash was not captured for this diagnostic; its
reproduction snapshot includes later metadata/limitation edits. The worker
reported no changes to the calculation. Treat this as a diagnostic receipt,
not an exact-source timing anchor. The separate GPU full-run anchors retain
their runtime source identities.

## Lower-IF microwave ramp

`--case eme --data-if 1725 --ramp-seconds .006` preserves Rate 360, Added off,
400 ms dwells, 55 ms quiet intervals, 5 ms safety margins, pulse support,
64 protected symbols and the 26.4 s DATA tile. Protected pulses remain 5.667 ms
clear of ramps. A 10 ms ramp correctly fails because no protected cells remain.
The production Helper planner selects a 2654.1667 Hz high pilot. Other presets
and the earlier explicit waveform remain unchanged by default.

Paired native controls isolate the propagated DATA and Helper with shared
noise/channel seeds, continuous state and complete tails. Source bit-difference
energy rises only 2.17%; total DATA source energy changes about 0.072% and actual
propagated DATA energy about 0.15%. The closer Helper placement increases FIR
leakage from **104.8 dB below noise to 64.3 dB below noise** in this fixture.
That remains negligible here, but is a 40.6 dB relative loss of rejection.
Actual Helper propagation energy changes −2.77 dB because its frequencies move
through the dispersive channel. Neither component is normalized after reception.

Three fixed campaigns use the same 35-seed schedule: 19 independent DATA-absent
calibration captures, eight balanced-bit signals and eight DATA-absent holdouts.
Helper remains present in the null captures. Each arm freezes its own conservative
rank gate at 0.05 before holdouts. The old/new weak cases use the same new batched
receiver and paired seeds; no gate is adjusted after observing a miss.

| Waveform and DATA C/N₀ | Arm | Correct admitted / 8 | Wrong admitted | Missed | Raw bit errors | False DATA admissions / 8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 6 ms, 1725 Hz, physical 10.02 dB-Hz | full | 8 | 0 | 0 | 0 | 1 |
| same | Helper | 8 | 0 | 0 | 0 | 0 |
| 6 ms, 1725 Hz, 1.5 dB-Hz | full | 5 | 0 | 3 | 1 | 1 |
| same | Helper | 5 | 0 | 3 | 2 | 0 |
| original ramp, 4000 Hz, 1.5 dB-Hz | full | 5 | 0 | 3 | 1 | 1 |
| same | Helper | 5 | 0 | 3 | 1 | 0 |

Every weak-case admission/miss decision agrees between the two waveforms on
paired seeds; one rejected Helper-arm bit changes from correct to wrong. All
eight true CFOs are inside Helper intervals in each campaign. At 1.5 dB-Hz,
only five of eight full-bank winning CFOs survive narrowing in either waveform.
Thus returning to the lower IF works at the physical link and preserves these
paired admission outcomes, but **does not solve the weak-case sensitivity
failures or establish noninferiority across the channel population**.

Uncertainty remains large. The fixed-bit-stratified 95% interval for balanced
correct admission is [0.334, 1] for 8/8 and [0.100, 0.975] for 5/8. One-sided
95% false-admission upper bounds are 31.2% for 0/8 and 47.1% for 1/8. These are
conditional on representative independent within-stratum draws and the frozen
gate, per category/arm; they are not simultaneous or daily-rate guarantees.
Paired waveform/C/N₀ conditions are not pooled as independent trials. Complete
campaign wall times were 120.98, 122.10 and 122.36 s, including native generation,
shared preparation, loading, scoring, hashing and result writes; about 100 s
per campaign was native export. Saved gates and decisions were independently
recomputed using `triad_helper_evidence.py` without rerunning DSP.

## Reproduction and cheap future checks

The [compact evidence archive](compact-evidence.tar.gz) is 2,304,988 bytes;
its [manifest](artifact-manifest.json), [raw-input inventory](raw-input-inventory.json)
and [checksums](SHA256SUMS) retain source/input identities, measurements,
frozen forecasts, gates, trial outcomes and failed diagnostics. Large sample
arrays are hash-inventoried rather than embedded. Only named visualization
arrays are removed from selected JSON reports, with every transformation
recorded. All three archived campaign gates/decisions were reverified after
fresh extraction; no DSP rerun is needed for that check.

Use the [current abbreviated testing policy](../../../triad-performance.md).
Current Python checks pass **82/82** in 10.47 s; native exporter checks pass five
captures and five rejected invocations. Default observation, validity and truth
bytes match the retained pre-change HF export. These are functional checks,
separate from sampled receiver and timing evidence.

Do not add the full HF banks, clock-grid probe or admission campaigns to routine
CI. Reuse exact source/input identities, public templates and DSP workspaces.
The frozen work ladder/full target pair is a named GPU prediction anchor, not
permission to extrapolate across new arithmetic, memory or algorithm regimes.
The existing ordinary receiver campaign is outside this study.

The recorded HF and EME GPU jobs compared previously prepared fixtures and
measured a bounded experimental receiver scope. Their timings and limitations
are in this report; the study did not qualify arbitrary stochastic spread,
actual RF retunes, live-radio distortion or operational reception/admission
targets. Its thread allocation and `NEW_*` command examples are historical
details, not current qualification instructions. Follow the maintained
[abbreviated triad harness](../../../triad-performance.md) and
[material lifecycle policy](../../../development.md#material-lifecycle).
The empirical engineering channel is not a field measurement, and neither this
study nor its GPU speedups qualify arbitrary stochastic spread, actual RF
retunes, live-radio distortion, daily false admission or 99% correct reception.
The temporary GPU Pod and container storage were removed after evidence
retrieval; the provider displayed zero ongoing spend. No persistent volume
was created.

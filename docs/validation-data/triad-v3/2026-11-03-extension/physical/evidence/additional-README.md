# Additional schedule and ×1000-spreading experiments

This separate record adds **four plans, 60 sampled condition captures and 180 receiver scores** to the unchanged [first nine-plan evidence](README.md). Independent recounting matched every saved JSON/NPZ score, normalized timing/frequency mixture and outcome. All configure, export and rescore jobs completed. No wrong-bit, noise-only or wrong-key admission occurred. This is isolated three-carrier evidence; ordinary-receiver integration remains outside scope.

The [summary](additional-summary.json) retains separate populations, pointwise intervals, energies, calendars, resource measurements and source/input bindings. It does not merge these captures into the first nine-plan denominator.

## Reception counts

Entries are correct/wrong/missed. Strict and relaxed results agree except where separately shown. Independent cells contain 16 balanced signal paths plus one null and one wrong-key control. Paired cells contain two signals and one null; paired −23 and −16 dB-Hz use the same paths and indexed noise seeds.

| Population | C/N0 dB-Hz | Independent windows | Global, zero CFO | Global + public CFO bank |
| --- | ---: | --- | --- | --- |
| validation-schedule-6: internal-wait3 | 6 | 0/0/16 | 16/0/0 | 16/0/0 |
| validation-schedule-6: loss-random25 | 6 | 0/0/16 | 15/0/1 | 15/0/1 |
| maxspread-validation-m16: max-spreading-rate0p1 | -16 | 0/0/16 | 1/0/15 | 16/0/0 |
| maxspread-paired-m16: max-spreading-rate0p1 | -16 | 0/0/2 | 0/0/2 | 2/0/0 |
| maxspread-paired-m23: max-spreading-rate0p1 | -23 | 0/0/2 | 0/0/2 | strict 0/0/2; relaxed 1/0/1 |

The independent 16/16 cells have only an 82.925% one-sided 95% lower confidence bound under the stated independent/common-probability assumption; 15/16 has 73.604%. A single rejected control has a 95% upper admission bound of 95%. These are selected finite cells, not 99% or 90% general reliability qualification. Across this package there are five noise-only captures and three wrong-key captures; methods and paired SNR reuse do not multiply independent controls.

## Actual durations and acquisition work

×1000 is Added Symbols at configured rate 0.1 Hz, giving 16.6667 fine symbols/s/carrier and 20.8333 Hz carrier spacing. With the existing private-pattern minimum/rounding, requested 8 seconds produces **3,842 seconds of source calendar and 3,843.3984375 seconds of capture** (491,955 complex samples at 128 samples/s). It is an approximately 64-minute transmission, not an 8-second success. The nominal payload allocation is 3,840 seconds.

This case uses 30 MHz shift, 1,725 Hz nominal IF, GPSDO OCXO endpoints, margin 1, HF-best 0.1 Hz/0.5 ms two-ray spread, Each65 additional modulation, and tight ±90.5 ms timing. FHSS and internal waits are off for this case. The receiver uses 15 timing positions and 95 CFO alternatives, or 1,425 joint groups, with original 1-second blocks. Its independent-window alternative therefore uses **2-second windows**, not the 62.5 ms windows in the scheduling cells. The reported original-block phase-arc power-loss bound is 0.00038582 dB, below the unchanged 0.01 dB allowance. This bound does not certify continuous likelihood accuracy or total timing mismatch.

| Independent cell | Source calendar s | Sampled calendar s | Samples / Fs | Timing groups | Timing × CFO groups |
| --- | ---: | ---: | --- | ---: | ---: |
| internal-wait3 | 156.4 | 156.664550781 | 641698 / 4096 | 455 | 2275 |
| loss-random25 | 46 | 46.2646484375 | 189500 / 4096 | 455 | 1365 |
| max-spreading-rate0p1 | 3842 | 3843.3984375 | 491955 / 128 | 15 | 1425 |

All complete declared timing/CFO groups participate; no simulator frequency or arrival selects the winning hypothesis. Success on these banks does not qualify clock-rate/keyed-epoch acquisition. FHSS scheduling still means source-envelope shaping plus receiver erasures, not measured RF retuning. Internal waits are real silent tiles inside the sampled bit.

## Energy benchmarks and remaining gap

The unchanged benchmark uses actual post-erasure signal energy and alpha=exp(−the actual log gate), not the full daily/12-hour budget for each capture. Under a known complete complex waveform, channel, bit, alignment and white Gaussian noise, Pd=Phi(sqrt(2rho)−norm.isf(alpha)). Strict 99% requires rho 37.33099 at alpha 1.35634e−10; relaxed 90% requires rho 22.56796 at alpha 2.71267e−8. These are conditional presence-detection benchmarks, not measured bit-reception probabilities or universal duration lower bounds.

| Independent cell | Actual rho range | dB above strict 99% genie required energy | Minimum genie strict Pd |
| --- | ---: | ---: | ---: |
| internal-wait3 | 165.55915–243.52352 | 6.469–8.145 | 1 |
| loss-random25 | 69.88944–210.49606 | 2.723–7.512 | 0.999999981899 |
| max-spreading-rate0p1 | 102.10228–181.30664 | 4.370–6.863 | 1 |

At −16 dB-Hz the ×1000 validation has 4.37–6.86 dB available-energy margin over the strict genie target. Its 16/16 CFO result therefore demonstrates useful hour-scale processing, not operation within a demonstrated fraction of a decibel of a fundamental bound. Zero-CFO global 1/16 and two-second-window 0/16 have ample total energy; they fail to collect it as effectively under the selected channel/offset model. The finite CFO bank recovers these cases using the same received observations.

The two paired −23 dB-Hz captures have rho 22.61283 and 30.82285: 0.83–2.18 dB below the energy required for strict 99% known-waveform detection. Their strict genie Pd values are 0.65933 and 0.93787; relaxed values are 0.90117 and 0.99213. Actual CFO scores are [20.45321,−0.57093] and [−1.34476,2.08955], so only the first passes the relaxed 17.42275 gate and neither passes the strict 22.72106 gate. Two paths and a 7 dB SNR spacing do not locate a sensitivity boundary or measure the unknown-channel loss.

The remaining random 25% miss is `loss-random25-validation-11-b1-signal`. Both global methods select the correct bit but score only 9.72293/9.06717, below both gates. It retains rho 69.88944 and genie Pd 0.999999982. This is a remaining reception/model/acquisition gap, not an energy impossibility. Its causes were not isolated in this read-only recount.

Post-channel energy for the ×1000 validation is 4,064.77–7,217.95 energy-seconds over 3,842 source seconds. Frequency-selective modulation and fading change that energy; it was preserved in the analysis, not normalized away. The paired −23/−16 inputs have exactly the same signal energy and seed arguments. There is no matched Added 1 waveform here, so these results do not establish a sensitivity gain caused solely by ×1000 spreading. No new −60 dB-Hz result or multi-day extrapolation is claimed.

## Measured computational cost

| Independent cell | Median export s | Median retained workspace MiB | Median receiver s: window / global / CFO |
| --- | ---: | ---: | --- |
| internal-wait3 | 380.788 | 836.647 | 39.2590 / 0.4363 / 1.3706 |
| loss-random25 | 228.810 | 247.822 | 13.0888 / 0.2878 / 0.4924 |
| max-spreading-rate0p1 | 17.250 | 21.318 | 1.2779 / 0.1893 / 0.6897 |

These are per-job elapsed observations on the cloud CPU host, with one CPU thread per child; scheduling exports ran 18 children concurrently and ×1000 validation exports ran six. They include host contention and are not controlled hardware-scaling curves. Receiver bulk arithmetic was FP32 with reported FP64 small preparation/reductions; no GPU acceleration is demonstrated by these figures.

The hour-scale, 15-timing low-rate workspace costs about 22.35 MB and 17.25 seconds to export; the shorter 455-timing schedules cost about 260/877 MB and 229/381 seconds. Computational cost depends on sampled rate, number of blocks, timing-bank size and retained basis dimensions, not elapsed physical time alone. Independent-window scoring spends 13.09/39.26 seconds on the two scheduling cells while global scoring takes 0.288/0.436 seconds; CFO takes 0.492/1.371 seconds. These measurements favor accumulated complex evidence in these stable cases. They do not make constant-channel collection appropriate for arbitrary renewed fading.

## Reproduce this analysis only

The original compact report archive must be restored first. Raw grouped statistics are not included there; their original receipt hashes are retained but were not independently re-read. Available manifests, JSON/NPZ scores, mixtures and outcome counts were checked. This recipe executes no modem simulation or receiver and does not edit either historical summary. Use a fresh local output directory. The commands regenerate the structured analysis and additional summary; this narrative is maintained separately:

```sh
STUDY=docs/validation-data/triad-v3/2026-11-03-extension/physical/evidence
REPORTS=local-evidence/triad-extension/remote/additional-physical-reports
OUT=local-evidence/triad-extension/additional-analysis-reproduction
PYTHON=/tmp/triad-weak-env/bin/python
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 "$PYTHON" "$STUDY/analyze_physical.py" --input "$REPORTS" --output "$OUT/additional-full-analysis.json"
"$PYTHON" "$STUDY/additional_describe.py" --input "$OUT/additional-full-analysis.json" --reports "$REPORTS" --output-dir "$OUT"
```

NumPy and SciPy are required. The shared analyzer has historical first-package wording in its generic limitations field; this additional summary supplies its own explicit scope: **zero failed executions here**, all methods use 0.01 dB phase-loss allowance, and the first package's six failures and microwave 0.02 dB arm remain separate. The maintained additional files retain full-analysis, reproducer, plan, receipt and manifest identities.

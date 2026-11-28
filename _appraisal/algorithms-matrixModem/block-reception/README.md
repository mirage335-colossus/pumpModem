# One-cell HF FSK block reception

This bounded sample-level experiment tests whether a 10-bit block survives the combination of quantization, correlated timing and frequency-width trials, exhaustive message scoring and a presence threshold. It exercises the proposed HF **lattice** with FHSS off. One block has 64 contiguous tones of 160 ms (32 symbols, two repeats), 352 positions on 6.25 Hz spacing and 1,024 candidate words. The known public calendar selects the block ordinal. [Current FSK format and receiver requirements](../../matrixModem-FSK.md) · [Historical design study](../../../_ref/algorithms-matrixModem/fsk-design-study.md)

The pointing-loss experiment measures whether the ordinary single-orientation receiver can read an intact waveform as its received power falls. The draft receiver uses per-tone and coherent evidence together. The no-argument experiment below is the earlier per-tone-only comparison. Separate, explicitly selected experiments evaluate reversed spectra, mis-set tone spacing and damaged tones. Their extra searches run only in those experiments. [Pointing loss and waterfall visibility](pointing-loss.md) · [Optional receiver fault tests](fault-tests.md)

## Implementation status

The native ten-bit HF reference takes complex samples, uses seeded surrogate schedules
with FHSS off, and receives the configured public calendar and block ordinal.
Its ordinary receiver searches residual timing and frequency using one- and five-bin
energy sums and coherent windows. Candidate scores are standardized within each
hypothesis before taking searched maxima. The experiment uses a floating-point FFT
and coherent sums, with four-bit-valued sample, evidence and complex-bin components
in wider containers. Four-bit transform arithmetic remains unqualified.

The full design spread bank, broader timing and drift envelopes, unknown-epoch or
ordinal acquisition, wire interoperability, field sensitivity and sustained receiver
service remain open. The protocols below state the geometry and coverage actually
executed. [FSK requirements](../../matrixModem-FSK.md) · [Timing coverage](timing-coverage.md)

## Receiver qualification requirements

These requirements govern qualification of the receiver design and its accelerated
routes. The bounded experiments below supply evidence for their recorded subsets.

- Check distinguishability on the surviving whole-word position sequences. Qualify
  public decisions, sensitivity and executed arithmetic before service comparisons.
  Freeze branch cutoffs before checking fresh quiet, active, interfered and
  wrong-schedule blocks. Include optional inverse-power weighting, sparse-line excision
  and truncated frequency apertures in calibration. [Code identifiability](../../../_ref/algorithms-matrixModem/fsk-code-identifiability.md)
  · [Presence calibration](../../../_ref/algorithms-matrixModem/fsk-presence-calibration.md)
- Measure complete single-stream service through ordered decisions and two-quiet closure,
  including preparation, transfers, unsuccessful search, recurring calibration and peak
  live workspace. Measure sustained backlog over a declared interval. Record each run's
  timings, chosen assignment and limiting stage per device.
- Use the same scheduling policy for HF and sub-9 kHz. Charge mandatory phase operations,
  timing searches and workspace. Additional concealment below 9 kHz must add no receiver
  processing cost. Test an independent challenging workload before extrapolating from a
  kernel or a single block.
- Compare direct lookup with a qualified Walsh–Hadamard transform for additive word
  scores. For oscillator searches compare direct scoring on small banks with dechirping
  and transforms across tone sums on broad banks. Time qualified CPU FFT, GPU FFT and
  matrix-product DFT routes at actual batch sizes. These are candidate routes whose
  decisions and complete service require qualification for the intended configuration.
  [Route evidence and assumptions](../../../_ref/algorithms-matrixModem/fsk-design-study.md#13-computation-and-simulation)

## Fixed protocol before sampling

- Use eight distinct reproducible public schedule fixtures. Select one uniformly for each isolated trial. For each fixture draw the 32 binary matrices and masks, a shuffled repeated symbol assignment, an independent 256-of-352 injection and a quarter-turn for each tone. Use the repository's research RNG to reproduce schedule structure. Payloads are uniformly selected post-encryption bits. A second independent set of schedules supplies the wrong-schedule check.
- At 3,200 complex samples/s, add independent circular Gaussian noise of mean sample energy one. The block energy in each table is transmitted energy divided by the complex noise variance. A one-path block has exactly this pre-noise energy. The adverse fixture uses two equal-power paths, one delayed 8 samples (2.5 ms) and shifted by 12.5 Hz, with a random relative phase. This is a two-ray stress case. Report it separately from a measured or Gaussian-spread HF channel.
- Estimate noise scale from the lower quartile of subsampled input powers. Quantize both components at 0.45 estimated component standard deviations, with levels −7 through +7. Erase the whole sample if either rounded component exceeds that range. For each timing alignment (−64, 0, +64 samples), apply the repository's 512-point FFT to each tone of the **same** observed buffer. Use median normalization per frequency over the block, then across positions per tone. Compare single-bin energy with the mean of five adjacent bins. Quantize each to 0–15 over six times unit noise energy. For each alignment and width, sum the chosen entries for each candidate. Standardize these 1,024 sums using their observed mean and standard deviation to compare widths. Pick the single largest standardized score over all six hypotheses and all messages. These are experimental scores with a data-dependent normalization.
- Freeze the threshold at the 396th smallest of 399 noise-only block maxima (rank 396/400 for a fresh exchangeable draw, with strict exceedance). Use 400 independent noise holdouts, 160 wrong-schedule blocks at 24 dB and 160 independent signal blocks per row. Signal rows are 18, 22 and 24 dB in the single-path model and the same three energies with two rays. True timing is randomly −32 or +32 samples, between grid points. Share underlying noise, data, phase and schedule between signal rows to isolate the comparison. Count correct acceptance, wrong acceptance and absence; report two-sided 95% Wilson intervals for individual proportions. Stop after these fixed draws even if inconclusive. The 1% diagnostic threshold does not measure a one-in-a-million false-presence rate.
- Exercise a continuous public calendar stream with different schedules at successive ordinals. Plant a strong block, one fully extinguished block, then a strong block followed by two quiet periods. Repeat with two consecutive extinguished transmitted blocks before the resumed strong block to test premature completion. Use the same threshold and scorer as for the trials. Require exact reported payloads, trailing quiet closure on the second scored absence, and retention of a single interior gap. Report any failure and the actual output. Silence has received Gaussian noise throughout. Transport end never supplies quiet.
- The original and separated-bin energy experiments assume a known block calendar, two spectral widths, three timing offsets, constant clocks and no hopping, drift, inter-tone coherent search, radar conditioning, or frontend filtering. Isolated trials have noise padding outside the block. The stream checks use the actual adjacent blocks. These tests supply neither device throughput nor probability of success outside their cases.

## Bounded comparison after the first result

The primary 18 dB holdout exposed a large loss with the delayed, +2-bin ray. Before taking any further samples, freeze a follow-on comparison. Add two equal-weight apertures, (0, +2 bins) and (0, −2 bins), to the existing one- and five-bin bank. This is informed by the constructed counterexample. It searches the sign and includes the original widths; it has no channel estimate, continuous Doppler search, channel inversion or new transmitter feature. Quantize and normalize all four scores by the original rules. Score both banks on the same IQ and candidates. Compare the six- and twelve-hypothesis maxima at separate 396th-of-399 noise thresholds, with 400 new common noise holdouts. Use newly seeded, matched 160-block sets at 18 dB for one ray, two rays separated by +2 or −2 bins, and a mismatch of +1.5 bins (9.375 Hz). The +2-bin case is primary. Retain counts of paired gains/losses in correct admission. For that one primary outcome, use the exact two-sided binomial test on discordant pairs. Report wrong payload separately. Stop after these prespecified cells. Original trials stay reported with their original threshold.

## Reproduce

The [fractional-timing comparison](timing-coverage.md) extends the coherent
experiment with a separately calibrated 513-point timing bank. It tests arrivals
between the five historical coherent timing candidates and keeps the per-tone
fallback. Its own protocol, deterministic controls, results and commands are
separate from the historical comparisons below.

From the repository root with a C++20 compiler, build the two-source experimental binary and run each fixed experiment.

```sh
g++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -pthread -Isrc -Iinclude \
  _appraisal/algorithms-matrixModem/block-reception/block_reception.cpp \
  src/search_fft.cpp -o /tmp/matrix-block-reception
/tmp/matrix-block-reception
/tmp/matrix-block-reception --apertures
/tmp/matrix-block-reception --coherent
/tmp/matrix-block-reception --held
/tmp/matrix-block-reception --thresholds
/tmp/matrix-block-reception --rare
```

Tested on an AMD Ryzen 5 PRO 5650U with Debian GCC 14.2.0. Repository FFT and research RNG from revision `396b47e1`; all signal generation, FFT, scoring and decisions run natively on the CPU. The FFT uses floating-point coefficients and results. The 4-bit restrictions tested here apply to the input and evidence codes. [Native experiment](block_reception.cpp)

### Reproduce the transmit waterfall

The [format guide](../../matrixModem-FSK.md#reading-the-transmit-format) shows the noiseless transmit fixture at seed `0xaa6218f2`, ordinal 0 and word 173. The fixture uses reproducible surrogate schedules. For each 160 ms tone, `--format-example` checks that the largest bin in the 512-sample FFT matches the scheduled frequency. It also checks the projection after removing the keyed phase against the expected complex amplitude. The experiment uses a centred complex band. Position `p` is signed FFT bin `p − 176` and is labelled at `450 + 6.25p` Hz on the audio grid.

```sh
/tmp/matrix-block-reception --format-example > /tmp/matrix-format-example.csv
python3 _appraisal/algorithms-matrixModem/block-reception/plot_format_waterfall.py \
  _appraisal/algorithms-matrixModem/block-reception/format-waterfall.svg \
  < /tmp/matrix-format-example.csv
```

The figure marks the verified bin for each of the 64 intervals. The enlargement labels each selected slot and its schedule, with breaks before and after slots 23–26. Its frequency scale matches the overview. Slots 0 and 23 carry copies of coded symbol 15 with independently drawn placements. The schematic uses equal peak power and no noise. The [orientation experiment](pointing-loss.md) shows received spectra on a noisy path.

## Original six-hypothesis bank

The 396th calibration score was 5.3561. A correct admission requires the threshold crossing and the correct word on the same block. For the two null rows every acceptance is false. The 95% intervals condition on the frozen calibration and the eight-schedule mixture. Rows at different energies or on different channels are paired, and margins are pointwise.

| Signal or null | Trials | Correct admissions (95% interval) | Wrong or false admissions | Absent |
|---|---:|---:|---:|---:|
| Noise | 400 | — | 5 (0.54–2.89%) | 395 |
| Wrong schedule, 24 dB | 160 | — | 0 (0–2.34%) | 160 |
| Single path, 18 dB | 160 | 125 (71.10–83.83%) | 0 | 35 |
| Single path, 22 dB | 160 | 160 (97.66–100%) | 0 | 0 |
| Single path, 24 dB | 160 | 160 (97.66–100%) | 0 | 0 |
| Two rays, 18 dB | 160 | 12 (4.34–12.65%) | 0 | 148 |
| Two rays, 22 dB | 160 | 159 (96.55–99.89%) | 0 | 1 |
| Two rays, 24 dB | 160 | 160 (97.66–100%) | 0 | 0 |

The three energy points correspond to approximately +7.90, +11.90 and +13.90 dB-Hz over 10.24 s, using nominal transmitted energy and unit ensemble channel power. No row with 160 draws resolves a 10⁻³ miss target. At the weakest tested level the two-ray loss shows up in presence decisions even though every accepted payload was correct.

Both continuous stream assertions passed. With one extinguished interior block the accepted words were 113, absence, 889, absence, absence. The first gap counted as an erasure. The receiver closed only after the second trailing absence, ordinal 4. With two extinguished interior words it closed prematurely at ordinal 2, reopened at ordinal 3 with the correct 889, and closed again at ordinal 5. Total extinction gives the receiver exactly the same sample law as silence. A second scored absence can establish a receiver boundary while the transmitter is still active. These deliberately constructed streams do not measure how often that happens in nature.

## Adding two separated frequency bins

The original six-hypothesis search and expanded twelve-hypothesis search used thresholds of 5.23393 and 5.53567 respectively on the **new** calibration. False presence was 3/400 (95% Wilson 0.26–2.18%) and 2/400 (0.14–1.80%) on the same noise holdouts. Those counts are compatible with substantial uncertainty in each realized false-alarm rate. The paired comparisons below are conditional on the two frozen thresholds.

| 18 dB channel, 160 fresh shared blocks per row | Original correct | Expanded correct | Expanded gains / losses on paired blocks | Wrong accepted, original / expanded |
|---|---:|---:|---:|---:|
| Single ray | 135 | 125 | 0 / 10 | 0 / 0 |
| Second ray +2 bins | 15 | 40 | 25 / 0 | 0 / 0 |
| Second ray −2 bins | 19 | 47 | 30 / 2 | 2 / 0 |
| Second ray +1.5 bins | 20 | 19 | 4 / 5 | 1 / 0 |

In the prespecified +2-bin case, correct admission rose from 9.4% (95% interval 5.8–14.9%) to 25.0% (18.9–32.2%). The exact two-sided test on the 25 discordant blocks gives *p* = 5.96 × 10⁻⁸. The extra aperture combines signal energy from both rays and excludes three of the noise bins in the five-bin average. Yet 120 of 160 blocks were still missed. Half a bin of mismatch removed the observed gain. The higher joint threshold also cost ten single-ray admissions. Keep the two-bin pair as a conditional diagnostic; a cross-channel gain for routine use remains unestablished. These probabilities depend on this score, energy, delay, off-grid timing, channel geometry and threshold calibration.

## Coherent branch and protected per-tone fallback

Freeze this comparison before sampling. Reuse the original six-hypothesis per-tone bank and its FFTs. Score all 1,024 words on all six coherent window lengths (2, 4, 8, 16, 32 and 64 tones), five specified integer time offsets (−64, −32, 0, 32, 64 samples), and three frequency offsets (−0.5, 0, +0.5 cycles per 10.24 s block). The two intermediate timing candidates use the central FFT windows and a per-position phase correction. They approximate boundary movement; the actual samples still cross tone boundaries. Each projected complex bin uses two four-bit component codes, followed by the scheduled phase correction, the hypothesized position-dependent timing correction and the hypothesized common frequency rotation at each tone midpoint. For each candidate sum complex values within each window and add window powers over the block. Score every message, standardizing against the other messages at that timing, frequency and rung before taking the maximum. This tests one finite grid.

Use independent fresh seeds for 399 noise calibration blocks and 400 common held-out null blocks. The baseline uses order statistic 396. The joint detector assigns each branch 0.5% of aggregate false-admission probability with order statistic 398 for each complete branch maximum. Prefer an admitted per-tone word, otherwise take the coherent word if admitted. The thresholds are marginal predictive rank bounds over calibration and test under the same exchangeable null, with no independence assumption between the branches. On 120 fresh shared trials per stratum, compare single-path and equal-power +2-bin two-ray at 15 and 18 dB, and independent uniform phase reset each tone at 18 and 22 dB. All RF offsets are independently signed ±0.25 cycles/block, halfway between frequency hypotheses; true starts remain ±32 samples between FFT alignments. Report baseline, each allocated branch and the joint result including wrong payload, miss and noise false admissions. Preserve fixed draws even if unresolved. Verify a continuous joint stream with one interior outage and trailing physical quiet, and check that a lost observation breaks a run of scored quiet. Record work dimensions and measured wall time for the full bounded experiment including generation. No hardware capacity claim.

The thresholds were 5.11875 for the 1%-budget per-tone baseline, 5.48257 for its reserved 0.5% branch, and 12.9713 for the 0.5% coherent branch. On the common 400 noise holdouts their false admissions were 8 (95% Wilson 1.02–3.90%), 2 (0.14–1.80%) and 1 (0.04–1.40%). The joint rule admitted 3 (0.26–2.18%). The baseline's realized 2.0% is a reminder that the rank bound averages over calibration. It does not certify a particular frozen threshold at 1%.

| Channel and block energy, 120 trials per row | Correct, baseline | Correct, reserved per-tone | Correct, reserved coherent | Correct, joint | Missed, joint |
|---|---:|---:|---:|---:|---:|
| Single ray, 15 dB | 16 | 9 | 114 | 114 | 6 |
| Single ray, 18 dB | 101 | 93 | 120 | 120 | 0 |
| Two rays, 15 dB | 3 | 1 | 39 | 39 | 81 |
| Two rays, 18 dB | 15 | 10 | 115 | 115 | 5 |
| Independent phase each tone, 18 dB | 105 | 91 | 1 | 91 | 29 |
| Independent phase each tone, 22 dB | 120 | 120 | 35 | 120 | 0 |

There were zero wrong admissions in every signal row for all four rules. Misses for each individual branch are therefore 120 minus the number correct. For the joint 15 dB single ray, the 95% correct-admission interval is 89.52–97.69%. It is 24.78–41.31% on the two-ray case at the same energy and 90.62–98.21% at 18 dB. The direct ray retains a stable phase in both two-ray cases. At 18 dB with random phase resets, the per-tone fallback protected 91 correct admissions while the coherent branch alone afforded just one. The higher reserved per-tone threshold lost 14 admissions relative to giving per-tone the full false-admission budget. A search over additional waveform hypotheses can add sensitivity and still hurt the original route after calibration.

The joint continuous stream gave 113, scored absence, 889, scored absence, scored absence, closing at ordinal 4. A separate negative control placed a missing buffer, zero-scale input, or wholly unusable interior IQ between two quiet observations. Each invalid slot broke the consecutive-quiet count, the receiver waited for two newly consecutive scored absences, and resumed data instead preserved the unresolved interior ordinals as erasures. Total **signal** extinction in valid received noise and unavailable receiver observations have different outcomes.

Per 10.24 s block the branches share 192 first FFTs of 512 points. The coherent branch gathers 327,680 tone projections (five timing hypotheses × 64 tones × 1,024 words). Three frequency hypotheses and six window lengths yield 92,160 coherent word scores and 967,680 pairwise complex tree additions. The original energy branch keeps its 6,144 word scores. This implementation uses the same three RF hypotheses even on short rungs to permit a simple exact search over its declared scores. Applying each RF rotation at a tone midpoint models phase across tones, with small residual intra-tone mismatch on this narrow grid. A missing shifted window boundary, a wider frequency search, independent sample-rate error, or a true start beyond the five tested offsets can change the result.

One local single-process run of the joint campaign took 16.2 seconds including signal generation, quantization, searches, decisions and stream checks (399 calibration + 400 held-out null + 720 present + 5 contiguous scored blocks). The original two diagnostic runs were executing concurrently, and this timing excludes initial schedule preparation. It describes experiment turnaround. All three commands reproduced their recorded results after integrating the branches.

## One-cell versus two-cell HF tones

Before sampling, fix a comparison at 10.24 s, 10 bits, 352 positions, 32 symbols and equal transmitted block energy. The one-cell format carries each symbol twice in 64 tones, the held format once in 32 tones of 320 ms. Keep the same matrices, masks and payload, drawing eight per-format permutations, interleavers and phases from reproducible schedule fixtures. Test a single frequency-flat, zero-mean complex Gaussian fading tap with a Gaussian Doppler spectrum approximated by 32 equal-power Doppler quantiles and independent Gaussian complex coefficients, constant over a trial. Compare slow 0.5 Hz and fast 20 Hz full two-standard-deviation widths. This single diffuse tap has neither a deterministic direct ray nor frequency diversity. The path varies continuously throughout each sampled block. Both formats see exactly the same gain and noise on the same sample indices, including in deep fades. Hold oscillator offset at zero, and use the existing ±32-sample half-grid timing error.

Use native 512-point and 1,024-point FFTs for per-tone evidence. Each compares the single matched bin and the mean noise-normalized energy around the same ±12.5 Hz range (five or nine bins respectively). Sum 4-bit per-tone evidence for all messages; calibrate the maximum over the three coarse timing alignments and two widths separately per format. Additionally run the same coherent grid and six physical-time rungs from the previous section for both formats, retaining their separate 0.5% per-tone and 0.5% coherent budgets. In the held format this reuses the quantized one-cell projections for the coherent branch; the per-tone branch evaluates the full two-cell FFT. Keep 399 shared fresh calibration nulls, 400 shared held-out nulls and 120 matched cases for each of two channel widths at 18 and 22 dB. Report correct/wrong/missed outcomes, paired gains/losses and the primary slow 18 dB paired per-tone comparison. Stop after these fixed draws regardless of sign. No threshold tuning or optimal-duration extrapolation.

The primary exact two-sided paired test gave *p* = 0.00333. At 0.5 Hz and 18 dB, the longer tone gained 24 correct per-tone admissions and lost 7 (81 to 98 correct out of 120). With the coherent branch added, 18 gained and 7 lost (90 to 101 correct). Thus in this slow diffuse fixture a receiver that tries coherent windows still benefited from holding the transmitted tone. On the fast path at 18 dB both designs missed almost every block. At 22 dB the four extra correct joint admissions for held tones there arose from 20 gains and 16 losses. This small difference supplies little basis for selecting a duration on that path.

| 2σ spread, block energy | One cell per-tone C/W/M | Two cells per-tone C/W/M | One cell joint C/W/M | Two cells joint C/W/M | Held joint gains / losses |
|---|---:|---:|---:|---:|---:|
| 0.5 Hz, 18 dB | 81 / 1 / 38 | 98 / 0 / 22 | 90 / 2 / 28 | 101 / 0 / 19 | 18 / 7 |
| 0.5 Hz, 22 dB | 120 / 0 / 0 | 120 / 0 / 0 | 120 / 0 / 0 | 120 / 0 / 0 | 0 / 0 |
| 20 Hz, 18 dB | 0 / 2 / 118 | 3 / 4 / 113 | 0 / 4 / 116 | 3 / 2 / 115 | 3 / 0 |
| 20 Hz, 22 dB | 82 / 2 / 36 | 89 / 0 / 31 | 81 / 1 / 38 | 85 / 0 / 35 | 20 / 16 |

C/W/M counts correct admissions, wrong admissions and misses. Primary slow-path per-tone 95% Wilson intervals are 58.7–75.2% for one cell and 73.8–87.6% for two. The full per-tone and joint branches used distinct calibrations for each format. The per-tone-only, reserved per-tone and reserved coherent thresholds were 5.34598, 5.38788 and 12.6951 for one cell, and 5.43116, 5.62931 and 12.6664 for two. On the common 400 null holdouts, false admissions for the two durations were 4 and 8 with per-tone alone, and 7 and 4 with the joint detector. The tests compare equal nominal predictive budgets; fixed-threshold false rates still have sampling uncertainty. All 120 received gain histories are shared across levels and formats and each has at most 32 independent complex fading coefficients. With block energy diversity defined as reciprocal variance of the unit-mean block-average received power, the finite fixture gives 9.30 at 0.5 Hz versus 9.40 for the continuous Gaussian spectrum. At 20 Hz it gives 31.96 versus 363.32. The implementation computes the finite variance from the average of sinc²((fᵢ−fⱼ)T) over the 32² frequency pairs and the continuous variance by integrating the squared Gaussian covariance over the block. Fast-path counts therefore describe a finite channel with much more block-to-block energy variation than the continuous reference. The wider apertures span the same bin-center interval but their rectangular spectral responses differ.

ITU-R F.1487 Annex 3 specifies a 2σ spread, with 0.5 Hz and two independently fading paths separated by 1 ms for its moderate mid-latitude case. The single-tap fixture here isolates Doppler and omits that delay diversity. Twenty hertz probes fast fading and is within the comparative test grid of Annex 2. Additional delay spread, finer spectral models, a weaker power or a different detector can change the ranking. [ITU-R F.1487, Annexes 2–3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I%21%21PDF-E.pdf)

The joint receiver for both formats forms 192 FFTs of 512 samples, gathers 327,680 complex tone-cell values and scores all six coherent physical window lengths. The held format adds 96 FFTs of 1,024 samples for its full-resolution per-tone pass. Per-tone lookup/additions across the 6,144 candidate scores fall from 393,216 to 196,608. On one local instrumented rerun, 1,279 scorer calls per format took 10.44 s for one cell and 10.26 s for two. The whole comparison took 22.37 s. Per-format timings include sample coding, FFTs and both search branches on one CPU thread, with templates and schedule prepared. The timing difference is too small to establish an advantage. The three earlier probes retained their counts. [Comparison implementation](held_tones.hpp)

## False-admission budgets for a human-read word

Prespecify aggregate predictive budgets of 0.5%, 1% and 2% per 10.24 s public block. Reserve half for each of the two searched families and retain per-tone priority when both admit a word. Use per-family order statistics 399, 398 and 396 of the same 399 calibration maxima, with strict exceedance. Re-evaluate the existing 400 quiet holdouts and all four matched 120-word signal strata from the held-tone comparison. Make each decision from the same two scores and winning words. This comparison reuses data that was already inspected; it supplies paired descriptive differences and pointwise intervals rather than new independent confirmation. Stop after the prespecified budgets and strata. Report quiet false admissions, correct signal words, wrong signal words and misses separately. Compute illustrative listening exposure and the two-consecutive-quiet wait under an independently scored, all-quiet calendar with a constant realized per-block false rate. Those models are conditional examples, separate from the empirical calibration.

The diagnostic gave the following counts, with 400 quiet blocks and 120 signal blocks in each channel/energy cell. Signal entries are **correct / wrong / missed**. The nested score thresholds allow more words through, but a lower per-tone cutoff can also change which word wins when the branches disagree.

| Tone cells | Aggregate budget | Quiet false | 0.5 Hz, 18 dB | 0.5 Hz, 22 dB | 20 Hz, 18 dB | 20 Hz, 22 dB |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.5% | 4 | 87 / 2 / 31 | 120 / 0 / 0 | 0 / 3 / 117 | 75 / 1 / 44 |
| 1 | 1% | 7 | 90 / 2 / 28 | 120 / 0 / 0 | 0 / 4 / 116 | 81 / 1 / 38 |
| 1 | 2% | 9 | 90 / 2 / 28 | 120 / 0 / 0 | 0 / 4 / 116 | 82 / 2 / 36 |
| 2 | 0.5% | 4 | 101 / 0 / 19 | 120 / 0 / 0 | 3 / 2 / 115 | 85 / 0 / 35 |
| 2 | 1% | 4 | 101 / 0 / 19 | 120 / 0 / 0 | 3 / 2 / 115 | 85 / 0 / 35 |
| 2 | 2% | 13 | 103 / 0 / 17 | 120 / 0 / 0 | 3 / 6 / 111 | 89 / 0 / 31 |

For example, at the 2% budget the one- and two-cell quiet false proportions were 2.25% (95% Wilson 1.19–4.22%) and 3.25% (1.91–5.48%). Tightening the held-tone cut from 2% to 1% on the difficult 20 Hz, 18 dB fixture removed four wrong words and retained its three correct words. At 22 dB, relaxing the cut admitted four more correct words. All durations and search counts remained fixed. This comparison measures admission and substitution at fixed energy and time. It cannot quantify an airtime reduction from an operator's willingness to inspect candidates.

For a human who listens continuously, the opportunity count matters. At realized false rate *p*, 351 fully observed silent blocks (just under one hour here) contribute 351*p* expected false block admissions regardless of dependence if each block has the same marginal rate. If independent, the probability of at least one is 1−(1−*p*)³⁵¹. From the start of trailing quiet, the two-quiet rule closes on schedule with probability (1−*p*)² and takes (2−*p*)/(1−*p*)² scored periods on average under this same independent model.

| Assumed realized quiet error *p* | Expected false blocks in 351 | At least one false | Closure delayed beyond two slots | Mean excess closure time |
|---:|---:|---:|---:|---:|
| 0.5% | 1.76 | 82.79% | 1.00% | 0.15 s |
| 1% | 3.51 | 97.06% | 1.99% | 0.31 s |
| 2% | 7.02 | 99.92% | 3.96% | 0.63 s |

A user expecting a single scheduled word faces one opportunity. A silent receiver left running all day faces thousands. False block counts and displayed message counts can differ when adjacent admissions are grouped. Interference that persists across slots changes the wait distribution, and two adjacent missed active blocks can close a message prematurely. The two-quiet rule and unavailable-input handling stay fixed for all thresholds.

## One false admission per ten thousand quiet blocks

Preregister a fresh train/holdout comparison for the full 10.24 s one-cell joint score. Freeze the earlier eight schedule fixtures, letter probabilities and branch priority. Compute 19,999 independent calibration block maxima per family under the earlier Gaussian quiet model; set each threshold to its own training maximum. Strict exceedance gives a marginal predictive bound of 1/20,000 per family averaged over training, or at most 10⁻⁴ in the union. Also compute the earlier diagnostic nominal 1% union from the 19,900th of 19,999 per family. For fixed thresholds, count per-family and joint false admissions on 30,000 fresh Gaussian quiet blocks. Use an exact one-sided 95% binomial upper confidence bound for each reported joint rate. For the single designated 10⁻⁴ target a fixed-threshold upper bound ≤10⁻⁴ requires zero false admissions. Stop after these 30,000 blocks regardless of result and preserve any failure. No inference at 10⁻⁶ from this run.

Prespecify 120 fresh, paired signal blocks for each of the four existing 0.5 Hz/20 Hz by 18 dB/22 dB strata, with shared noise and channel between threshold choices. Report correct, wrong and missed words for both cuts. These small present counts characterize the trade-off; the primary endpoint is the 30,000-block null holdout. Use six native trial workers with independent per-index seeds and the same searched receiver. Do not pool the previous calibration or noise holdouts.

Separately freeze 160 new wrong-schedule single-path blocks at each of 24 and 36 dB. Use the original wrong-schedule key fixture and trial-indexed new data/noise, common across both levels and both cuts, with zero common carrier offset and ±32-sample starts. Every accepted word is false for the receiver, even if its bits happen to equal the other transmitter's bits. Keep these rows apart from Gaussian quiet and from wrong substitutions on the intended signal. No recalibration and no pooling with the 30,000 quiet blocks.

**The 10⁻⁴ fixed-threshold confidence endpoint was not met.** One of 30,000 independent quiet blocks was admitted. The point estimate is 3.33 × 10⁻⁵, and the exact one-sided 95% binomial upper bound is 1.58119 × 10⁻⁴. The calibration and thresholds remained frozen. In the looser comparison, 287 of the same 30,000 blocks were admitted. The observations constrain false admissions in the Gaussian fixture with the specified public-schedule mixture and searched geometry.

| Nominal aggregate target | Per-tone / coherent cutoffs | Per-tone false | Coherent false | Joint false / 30,000 | Joint exact upper 95% |
|---:|---:|---:|---:|---:|---:|
| 10⁻⁴ | 6.63583 / 17.9827 | 1 | 0 | 1 (0.00333%) | 0.01581% |
| 1% | 5.41397 / 12.9452 | 152 | 136 | 287 (0.9567%) | 1.0544% |

| Intended signal, 120 blocks per row | 10⁻⁴ correct / wrong / missed | 1% correct / wrong / missed |
|---|---:|---:|
| 0.5 Hz, 18 dB | 50 / 0 / 70 | 93 / 0 / 27 |
| 0.5 Hz, 22 dB | 115 / 0 / 5 | 117 / 0 / 3 |
| 20 Hz, 18 dB | 0 / 0 / 120 | 2 / 2 / 116 |
| 20 Hz, 22 dB | 46 / 0 / 74 | 94 / 0 / 26 |

On the slow 18 dB fixture, relaxing from 10⁻⁴ to 1% yielded **43 additional correct admissions in 120 transmitted words**. On the fast 22 dB fixture the corresponding difference was 48. In the weak fast case it yielded two correct and two wrong admissions. The 10.24 s transmitted duration and the searched work stayed constant in these comparisons.

The strong wrong-schedule signal produced 0/160 and 1/160 false admissions at 24 dB for the 10⁻⁴ and 1% cuts respectively. At 36 dB the counts were 0/160 and 6/160. A zero count in 160 still gives a pointwise two-sided 95% Wilson upper limit of 2.34%; the 6/160 interval is 1.73–7.94%. Quiet-noise calibration alone therefore gives insufficient evidence about strong overlapping transmissions. Both levels used identical noise, word and other-schedule draws. The wrong-schedule extension was requested mid-run after calibration had started. Its decisions were checked against all four corners of a ±0.0001 interval around both displayed thresholds, containing every original full-precision cutoff; the winning words agreed throughout. A final integrated `--rare` run used the full-precision thresholds directly. It reproduced all calibration cutoffs, quiet counts, intended-signal counts and wrong-schedule counts on the identical seeds in 117.7 s, including both wrong-schedule levels. This repetition uses the same observations.

For scale, at a realized rate of 10⁻⁴ and 10.24 s per block, continuous silence gives about 0.84 false block admissions in a day on average. At 10⁻⁶ the corresponding model gives about 0.0084 per day. With zero observed false admissions, demonstrating a one-sided 95% upper bound of 10⁻⁶ needs 2,995,731 independent quiet holdouts. This historical reference is theoretical. The present trial did not measure a 10⁻⁶ threshold, its signal performance, or a duration reduction against it. The six-worker local run took 118.5 s for calibration, quiet holdout and the 480 intended-signal blocks. Independent trial workers shortened experimental turnaround. [Calibration and exposure models](../../../_ref/algorithms-matrixModem/fsk-presence-calibration.md)

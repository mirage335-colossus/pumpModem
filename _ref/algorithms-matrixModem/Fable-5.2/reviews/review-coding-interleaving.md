# Coding, look combining, interleaving and termination for a one-way noncoherent MFSK modem

Literature review with independent calculations, 26 November 2026 (Fable-5.2 pass). Scope: which code, which way of reaching very low rates, which interleaver and metric, and which termination rule suit Matrix Modem: a one-way, very-low-rate noncoherent MFSK link on HF skywave, messages of 10 to a few hundred bits (plus a long format), fixed geometry, physical end of message, a shared keystream, and a receiver built from large fixed-shape matrix products.

**Status.** Research findings, not a qualified design. None of the numbers is a Matrix Modem measurement. The calculations are theoretical, with the assumptions stated beside them, and are kept as research material in [`_appraisal/algorithms-matrixModem/short-message-energy/`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/) (see [calculation record](#9-calculation-record)).

**Evidence labels.**

- **[P]** published: a value or statement taken from the cited source. Where only an abstract was read, the source list says so.
- **[D]** derived here, by the named program. `sme` means [`short_message_energy.cpp`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/short_message_energy.cpp) with output [`results/short_message_energy.md`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/results/short_message_energy.md); `mc` means [`codebook_mc.cpp`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/codebook_mc.cpp) with output [`results/codebook_mc.md`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/results/codebook_mc.md). Unit conversions of published thresholds are also [D] and say so.
- **[H]** heuristic or inference of this review: an argument, an extrapolation, or a design idea that has not been computed or published.

Energies are per **message**, `E/N0`, as plain ratios unless marked dB; `N0` is the one-sided noise density. `ε` is the message error probability. Every source in the [source list](#10-sources) is marked [verified] or [unverified].

## 0. What is already established, and what is new here

| Topic | Already established, and where | New, sharpened or contradicted here |
| --- | --- | --- |
| Fading bound, energy per coherence interval, quadratic regime | [Fading-limit study](../../Sol-6.2/fading-limit-and-mfsk-alternatives.md) | Used, not repeated |
| Finite-message penalty | Information-density heuristic, 20–10,000 bits ([finite_message.md](../../../../_appraisal/algorithms-matrixModem/coherence-limits/results/finite_message.md)) | Exact orthogonal-code energies for 1–300 bits, three error targets, with a converse; the heuristic is 0.3–0.6 dB conservative against them ([section 8](#8-contradictions-and-corrections-to-earlier-documents)) |
| One-shot orthogonal signalling | `Eb/N0` for k = 1–128 on AWGN; Rayleigh look sweep for k = 16 and 100; Yang–Durisi–Polyanskiy Table I ([GEMM decoding review §1](../../Opus-5.5/reviews/review-gemm-decoding.md), [calc/finite_energy.md](../../../../_appraisal/algorithms-matrixModem/calc/results/finite_energy.md)) | Energy per message, the 10-bit premise, the knee at `k = log2(1/ε)`, the optimum branch count and branch SNR as functions of k, and the cost of too few branches |
| Short messages | Union-bound illustration 15.2 / 27.7 / 152; "bank up to 20–30 bits, tail-biting codes beyond" ([format guidance §5a](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md)) | Exact values 12.4 / 24.9 / 110.9; a keyed random 64-ary bank measured against the orthogonal reference; the cost of sectioning; a product-structured bank |
| Short-code decoders | OSD, tail-biting, CRC-polar, GRAND, Reed–Muller, Fano, sparse regression at (128,64) on binary-input AWGN; WSJT thresholds ([GEMM decoding review §2–3, §9](../../Opus-5.5/reviews/review-gemm-decoding.md)) | What each decoder needs on the noncoherent q-ary channel (shape, precision, erasures, noise knowledge); cutoff rate of that channel; published noncoherent results; the WSPR threshold corrected |
| Capacity of noncoherent MFSK | Symbol-wise limits, optimum symbol SNR, in-symbol diversity hurts ([theory review §2](../../Opus-5.5/reviews/review-noncoherent-theory.md)) | Which element should carry rate reduction; when repetition equals coding; combining loss against per-look SNR |
| Kennedy's optimum of about 3 per branch | [Fading-limit study, distinctions](../../Sol-6.2/fading-limit-and-mfsk-alternatives.md#distinctions-to-keep-in-view) | 3 is the low-rate limit; at ε = 10⁻³ the optimum is 3.0 (1 bit), 3.7 (10 bits), 6.5 (100 bits), 9.1 (300 bits), and it is broad |
| Interference | "Clip or blank, robust bin statistics, erasures"; classical partial-band results, clip-level table, OTHR waveforms and a periodic-interleaver demonstration in the sibling [OTHR review](review-othr-interference.md) | The decoder-level consequence: metrics compared inside a bank decoder with fading; margin-to-erasure tables with finite diversity; margin spent as SNR against margin spent as branches |
| Interleaving | "Interleave over the slowest fade; treat outages as erasures" | Number of branches, not time alone; keyed placement of many weak looks; fade-duration numbers; what a slot must span |
| Termination | Contract: scored quiet slots, no length headers | Error trade-off of the quiet-slot test from the decoder's own scores; literature on joint detection and decoding |

## 1. Findings in brief

1. **The 10-bit premise holds, for a reason that is not convolutional coding.** With `2^k` orthogonal codewords and maximum-likelihood detection, a 10-bit message needs 2.01 times the energy of a 1-bit message at ε = 10⁻³ on AWGN and 2.02 times on Rayleigh fading with optimum diversity. The factor is 2.5 at ε = 10⁻² and 1.8 at 10⁻⁴ [D, `sme`]. A message shorter than 10 bits that is padded to 10 therefore pays at most about 3 dB.
2. **The knee is at `k = log2(1/ε)` bits.** The energy reaches twice the 1-bit energy at 6.6, 9.9 and 13.3 bits for ε = 10⁻², 10⁻³, 10⁻⁴ on AWGN (6.1, 9.8, 13.5 on Rayleigh) [D]. Ten bits is the knee for ε = 10⁻³ and for no other target. Above about 50 bits the cost is close to a fixed charge plus a per-bit charge: `E/N0 ≈ 25 + 0.85·k` on AWGN and `≈ 90 + 1.45·k` on Rayleigh at ε = 10⁻³ [D, fit].
3. **Orthogonal signalling is nearly the best any code can do at these lengths.** On AWGN the noncoherent orthogonal code is 1.15 dB above the Polyanskiy–Poor–Verdú converse for all codes at 10 bits and 0.19 dB at 100 bits [D against P]. On Rayleigh fading without channel knowledge, the published converse lies about 0.7 dB below the orthogonal scheme at 10 bits and within about 0.3 dB from 100 bits [P, read from a figure].
4. **Fading costs 2.7–4.5 dB at the optimum and far more with too few branches.** At 10 bits and ε = 10⁻³ the requirement is 18.2 dB with 18 branches, 21.0 dB with 4, 26.4 dB with 2 and 38.8 dB with 1; with 128 branches it is 20.0 dB [D]. Too few independent branches is a drastic failure; too many is a mild loss.
5. **A keyed random 64-ary bank with exhaustive decoding is within 0.2 dB of the orthogonal reference at 10 bits** and 0.7 dB at 20 bits; at 10 bits it loses 0.7 dB with 16 tones and 1.4 dB with 8, and at 20 bits 256 tones bring the loss back to 0.3 dB [D, `mc`]. The tone alphabet matters more than the code.
6. **Sectioning is expensive.** Splitting 100 bits into four independently decoded 25-bit sections costs 2.1 dB on AWGN and 2.9 dB on Rayleigh; 300 bits in twelve sections costs 3.2 and 4.7 dB [D]. A bank calendar is right for one or two blocks and wrong for a few hundred bits.
7. **Sequential decoding is the wrong family here.** The cutoff rate of the noncoherent 64-ary channel needs 3.3 dB on AWGN and 6.8 dB on Rayleigh [D], against capacities near 1.2 and 2.3 dB, and its computation is unbounded. The fielded K = 32 codes are nevertheless better than the earlier review said: WSPR's published threshold is −31 dB, not −28 dB [P].
8. **Rate reduction should be carried first by the dwell, then by repeated looks, and not by the code rate.** Repetition with square-law combining collects at least 93% of what an ideal lower-rate code would collect while the combined symbol carries less than about 0.3 of `log2 q` bits [D, `mc`]. Below about 0 dB, each dB of per-look SNR costs 0.6–0.9 dB of energy [D].
9. **A summed energy metric fails completely under partial interference**: 1% of cells hit at +20 dB takes a design with 6 dB of margin from no observed errors to an 80% error rate [D, `mc`]. Normalising each slot by the median of its own tones and clipping restores most of the genie performance.
10. **A margin of X dB does not buy the textbook erased fraction `1 − 10^(−X/10)` when it is spent as SNR on few branches.** With the energy-optimal 18 branches, 10 dB of margin tolerates 62% random erasures, not 90%; with 89 weaker branches it tolerates 87% at a 1.3 dB sensitivity cost [D]. The table is in [section 5.5](#55-from-margin-to-erased-fraction).
11. **Two scored quiet slots is the right order of magnitude.** A block that decodes at ε = 10⁻³ is told apart from noise by its own best score with only about 10⁻² reliability; 3 dB of margin brings that to about 2·10⁻⁵ [D]. An outage looks exactly like quiet.
12. **Recommended:** accumulate robust cell metrics over keyed, pseudo-randomly placed looks; decode 10–24-bit blocks with an exhaustive keyed bank; use a nonbinary code over the tone alphabet (Q65-like) for blocks of about 80–300 bits; keep a CRC-aided tail-biting code as the alternative without iterative decoding ([section 7](#7-recommended-architecture-and-alternatives-question-6)).

## 2. Energy per message at finite length (Question 1)

### 2.1 Published bounds

- **AWGN, no feedback.** `log M*(E, ε) = (E/N0)·log e − sqrt(2E/N0)·Q⁻¹(ε)·log e + ½·log(E/N0) + O(1)` [P: Polyanskiy, Poor and Verdú 2011, as restated in Yang, Durisi and Polyanskiy 2016, eq. (2)]. The achievability part is the exact error probability of `M` orthogonal codewords under maximum-likelihood decoding; the converse is the meta-converse with the noise distribution as auxiliary [P: same restatement]. The general converse is `M ≤ sup 1/β_(1−ε)(P_(Y|X=x), P_(Y|X=0))` over codewords within the energy budget [P: Yang, Durisi and Polyanskiy 2016, Theorem 3]. For the AWGN channel the Neyman–Pearson test between two Gaussians evaluates it to `1/M ≥ Q(sqrt(2E/N0) + Q⁻¹(1−ε))`, that is `E/N0 ≥ ½·(Q⁻¹(1/M) + Q⁻¹(ε))²` [D, one-line evaluation; it is also the form recalled from the 2011 paper, whose full text could not be retrieved in this pass]. It reproduces the three-term expansion with an `O(1)` term of `Q⁻¹(ε)²/2 + ½·ln 4π` [D].
- **Rayleigh block fading, no channel knowledge.** The scheme is uncoded pulse-position (orthogonal) signalling concatenated with a repetition code over `N` coherence intervals, with a threshold decoder (Corollary 4) or maximum-likelihood decoding (Corollary 5); a non-asymptotic converse is Theorem 6. Table I, ε = 10⁻³: 10 bits need `E/N0` = 67 with `N*` = 18; 100 bits need 2.4·10², `N*` = 38; 1000 bits need 1.3·10³, `N*` = 96. The gap to −1.59 dB shrinks only as `((log k)/k)^(1/3)`, and the result does not depend on the coherence interval [P: Yang, Durisi and Polyanskiy 2016, full text]. In their Figure 1 the converse is about 0.7 dB below the Corollary 5 curve at 10 bits and the two are within about 0.3 dB from 100 bits upward [P, read from the figure, ±0.2 dB].
- **Rate-constrained short packets.** For 168 symbols at 0.48 bit per channel use on Rayleigh block fading, noncoherent transmission beats optimised pilot-assisted transmission by 1.2 dB at 10⁻³ [P: Östman et al. 2019, abstract]. This is a coherent-symbol result at moderate rate; it supports sending no pilots, and says nothing about the minimum energy of a tone code.
- **Block size and real codes.** The sphere-packing bound normalised to the capacity limit of each rate is almost independent of rate; 1000-bit blocks have an inherent advantage of about 1.4 dB over 100-bit blocks; turbo codes sit 0.7 dB from the bound for 1024 bits and more, 0.9 and 1.05 dB at 512 and 256 bits. The best short block codes with maximum-likelihood decoding ((8,4), (24,12), (48,24)) run parallel to the bound. For **terminated convolutional codes the required `Eb/N0` at a fixed word error rate "bottoms out and then begins to increase slowly" as the block grows** [P: Dolinar, Divsalar and Pollara 1998, full text]. Their footnote 8 gives the orthogonal-code bound used in 2.4.

### 2.2 AWGN

`2^k` orthogonal codewords, one unknown phase per message, exact error probability [D, `sme` §1]. `conv` is the converse of 2.1 for any code without feedback; it allows coherent detection, so it also bounds every noncoherent scheme.

| k | `E/N0` at ε = 10⁻² | 10⁻³ | 10⁻⁴ | `Eb/N0` (dB) at 10⁻² | 10⁻³ | 10⁻⁴ | `conv` at 10⁻² | 10⁻³ | 10⁻⁴ | above `conv` at 10⁻³ |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 7.82 | 12.43 | 17.03 | 8.93 | 10.94 | 12.31 | 2.71 | 4.77 | 6.92 | 4.15 dB |
| 5 | 13.78 | 18.83 | 23.66 | 4.40 | 5.76 | 6.75 | 8.77 | 12.27 | 15.58 | 1.86 dB |
| 10 | 19.38 | 24.93 | 30.09 | 2.87 | 3.97 | 4.78 | 14.71 | 19.14 | 23.23 | 1.15 dB |
| 16 | 25.60 | 31.72 | 37.30 | 2.04 | 2.97 | 3.68 | 21.10 | 26.35 | 31.11 | 0.81 dB |
| 20 | 29.57 | 36.05 | 41.90 | 1.70 | 2.56 | 3.21 | 25.13 | 30.84 | 35.97 | 0.68 dB |
| 30 | 39.12 | 46.39 | 52.87 | 1.15 | 1.89 | 2.46 | 34.74 | 41.40 | 47.32 | 0.49 dB |
| 50 | 57.21 | 65.83 | 73.39 | 0.58 | 1.19 | 1.67 | 52.86 | 61.01 | 68.15 | 0.33 dB |
| 77 | 80.50 | 90.60 | 99.37 | 0.19 | 0.71 | 1.11 | 76.14 | 85.86 | 94.30 | 0.23 dB |
| 100 | 99.76 | 110.94 | 120.59 | −0.01 | 0.45 | 0.81 | 95.37 | 106.21 | 115.57 | 0.19 dB |
| 200 | 180.33 | 195.19 | 207.87 | −0.45 | −0.11 | 0.17 | 175.83 | 190.44 | 202.91 | 0.11 dB |
| 300 | 258.31 | 276.02 | 291.03 | −0.65 | −0.36 | −0.13 | 253.71 | 271.21 | 286.06 | 0.08 dB |

The 1-bit entry is exact: `E/N0 = 2·ln(1/(2ε))`. The 4.15 dB gap at 1 bit is the coherent antipodal pair, which a noncoherent link cannot use. From 10 bits upward no code of any kind, coherent or not, can save more than 1.15 dB against noncoherent orthogonal signalling on a phase-stable channel, and from 50 bits no more than 0.33 dB.

### 2.3 Rayleigh fading without channel knowledge, optimised diversity

Each codeword spreads its energy equally over `L` independently fading cells (branches); the other codewords occupy disjoint cells; the receiver sums energies, which is exactly maximum likelihood for equal-mean Rayleigh branches. Exact error probability, `L` optimised [D, `sme` §2]. The program reproduces Table I above: 66.3 (L = 18), 238.6 (L = 37, against `N*` = 38), 1319 (L = 96).

| k | `E/N0` at ε = 10⁻² | 10⁻³ | 10⁻⁴ | `Eb/N0` (dB) at 10⁻² | 10⁻³ | 10⁻⁴ | `L*` at 10⁻² / 10⁻³ / 10⁻⁴ | Branch SNR `E/(L*·N0)` | `L` within 0.5 dB (10⁻³) | Above AWGN (10⁻³) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |
| 1 | 18.9 | 32.8 | 47.2 | 12.76 | 15.16 | 16.74 | 7 / 11 / 16 | 2.70 / 2.98 / 2.95 | 6–27 | 4.22 dB |
| 5 | 34.5 | 50.8 | 66.7 | 8.39 | 10.07 | 11.25 | 10 / 16 / 21 | 3.45 / 3.17 / 3.17 | 8–38 | 4.31 dB |
| 10 | 47.7 | 66.3 | 83.8 | 6.79 | 8.21 | 9.23 | 12 / 18 / 24 | 3.98 / 3.68 / 3.49 | 9–46 | 4.25 dB |
| 16 | 61.4 | 82.3 | 101.7 | 5.84 | 7.11 | 8.03 | 14 / 21 / 27 | 4.38 / 3.92 / 3.77 | 10–51 | 4.14 dB |
| 20 | 69.7 | 92.0 | 112.5 | 5.42 | 6.63 | 7.50 | 15 / 22 / 29 | 4.65 / 4.18 / 3.88 | 11–57 | 4.07 dB |
| 30 | 88.9 | 114.3 | 137.3 | 4.72 | 5.81 | 6.61 | 17 / 25 / 32 | 5.23 / 4.57 / 4.29 | 12–63 | 3.92 dB |
| 50 | 123.3 | 153.8 | 180.8 | 3.92 | 4.88 | 5.58 | 21 / 29 / 37 | 5.87 / 5.30 / 4.89 | 13–78 | 3.69 dB |
| 77 | 165.0 | 201.2 | 232.8 | 3.31 | 4.17 | 4.80 | 24 / 34 / 43 | 6.88 / 5.92 / 5.41 | 15–92 | 3.46 dB |
| 100 | 198.2 | 238.6 | 273.4 | 2.97 | 3.78 | 4.37 | 27 / 37 / 47 | 7.34 / 6.45 / 5.82 | 16–100 | 3.33 dB |
| 200 | 329.6 | 384.5 | 431.0 | 2.17 | 2.84 | 3.33 | 35 / 48 / 60 | 9.42 / 8.01 / 7.18 | 20–137 | 2.94 dB |
| 300 | 450.0 | 516.6 | 572.3 | 1.76 | 2.36 | 2.81 | 42 / 57 / 70 | 10.71 / 9.06 / 8.18 | 23–163 | 2.72 dB |

**Fixed branch count, ε = 10⁻³, `E/N0` in dB** [D, `sme` §2]:

| k | L = 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 1024 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 29.99 | 20.21 | 16.45 | 15.27 | 15.25 | 15.82 | 16.72 | 17.83 | 19.06 | 21.76 |
| 10 | 38.75 | 26.41 | 21.04 | 18.87 | 18.23 | 18.41 | 19.05 | 19.99 | 21.11 | 23.67 |
| 20 | 41.59 | 28.83 | 23.08 | 20.59 | 19.72 | 19.72 | 20.25 | 21.09 | 22.15 | 24.63 |
| 50 | 45.47 | 32.34 | 26.19 | 23.33 | 22.13 | 21.87 | 22.19 | 22.89 | 23.83 | 26.18 |
| 100 | 48.44 | 35.14 | 28.78 | 25.69 | 24.26 | 23.79 | 23.93 | 24.48 | 25.31 | 27.51 |

Three points follow.

- **The optimum branch SNR is not a constant 3.** It is 3.0 for one bit and rises to 3.7, 6.5 and 9.1 for 10, 100 and 300 bits at ε = 10⁻³. The value 3 (4.8 dB) is the low-rate limit: it maximises `−ln D(γ)/γ` for the Bhattacharyya parameter `D = 4(1+γ)/(2+γ)²` of two tones, giving `P ≈ exp(−0.1488·E/N0)` per competing codeword [D, `sme` §10; the textbook form of the Pierce–Kennedy result]. Longer messages work at a higher rate per branch and prefer fewer, stronger branches.
- **The optimum is broad on the high side and steep on the low side.** A factor of two too few branches costs about 0.7 dB at 10 bits; a factor of four too few costs 2.8 dB; one branch costs 20–25 dB. A factor of seven too many costs 1.8 dB. A format that cannot guarantee about 8 independent branches for a 10-bit message, or 16 for 100 bits, has a drastic failure mode on slow, frequency-flat channels.
- **Frequency-flat slow fading is the dangerous case for the shortest messages.** At +10 dB-Hz a 10-bit message needs only about 7 s of signal. On a 0.1 Hz path that is one or two coherence intervals in time, so the branches must come from keyed hopping across coherence bandwidths, or the message must be stretched and pay the single-branch price [H, from the table and the fade durations in 5.4].

### 2.4 The 10-bit premise

Premise: "messages shorter than somewhat more than 10 bits need at most about 2× the transmit time/energy of the shortest possible message".

| Channel | ε | `E(1)` | `E(5)` | `E(10)` | `E(20)` | `E(10)/E(1)` | `E(20)/E(10)` | `E(100)/E(10)` | k where `E(k) = 2·E(1)` | `log2(1/ε)` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| AWGN | 10⁻² | 7.82 | 13.78 | 19.38 | 29.57 | 2.48 | 1.53 | 5.15 | 6.6 | 6.6 |
| AWGN | 10⁻³ | 12.43 | 18.83 | 24.93 | 36.05 | 2.01 | 1.45 | 4.45 | 9.9 | 10.0 |
| AWGN | 10⁻⁴ | 17.03 | 23.66 | 30.09 | 41.90 | 1.77 | 1.39 | 4.01 | 13.3 | 13.3 |
| Rayleigh, `L*` | 10⁻² | 18.88 | 34.50 | 47.73 | 69.70 | 2.53 | 1.46 | 4.15 | 6.1 | 6.6 |
| Rayleigh, `L*` | 10⁻³ | 32.81 | 50.79 | 66.29 | 92.04 | 2.02 | 1.39 | 3.60 | 9.8 | 10.0 |
| Rayleigh, `L*` | 10⁻⁴ | 47.20 | 66.66 | 83.84 | 112.54 | 1.78 | 1.34 | 3.26 | 13.5 | 13.3 |

All [D, `sme` §3].

- **Quantitatively the premise is right at ε = 10⁻³**: ten bits cost 2.0 times one bit, on both channels. It is slightly optimistic at 10⁻² (2.5 times) and conservative at 10⁻⁴ (1.8 times). A 5-bit message costs 76% of a 10-bit one, so declining to support messages under 10 bits wastes at most 3 dB for a 1-bit user and 1.2 dB for a 5-bit user.
- **The reason is the reliability term, not the code family.** For orthogonal codewords the error bound is `exp(−(E/N0)/2 + k·ln 2)` while `k·ln 2 ≤ (E/N0)/4`, and `exp(−(sqrt(E/N0) − sqrt(k·ln 2))²)` beyond [P: Dolinar, Divsalar and Pollara 1998, footnote 8, in per-bit form]. Hence `E/N0 ≈ 2·(k·ln 2 + ln(1/ε))` up to `k = log2(1/ε)`, where the information term equals the reliability term and the energy is exactly twice its `k → 0` value. Beyond the knee `E/N0 ≈ (sqrt(k·ln 2) + sqrt(ln(1/ε)))²`. The computed knees (6.6, 9.9, 13.3 bits) match `log2(1/ε)` to 0.1 bit. The noncoherent exponent equals the coherent one [recalled; Jacobs 1963, not read], which is why the same knee appears; with optimum diversity the Rayleigh channel shows it too [D].
- **"With convolutional codes" should be dropped from the premise.** A convolutional code on a 10-bit message is a block code with at most 1024 codewords; decoded by maximum likelihood it is a bank, and its tail or its tail-biting structure only restricts the codebook. A zero tail of `K − 1` bits on a 10-bit message is a rate loss of 2 dB for `K = 7`. The published observation that terminated convolutional codes get worse relative to the bound as blocks grow [P: Dolinar et al.] points the same way: the trellis is a decoding convenience, and it is not needed below about 24 bits.
- **The earlier union-bound illustration is corrected in [section 8](#8-contradictions-and-corrections-to-earlier-documents).**

### 2.5 Where the cost grows linearly

Marginal energy per added bit, `(E(k2) − E(k1))/(k2 − k1)` in units of `N0` [D, `sme` §3]. The capacity slope is `ln 2` = 0.693; the AWGN low-rate slope is `2·ln 2` = 1.386; the Rayleigh low-rate slope is `ln 2/0.1488` = 4.66.

| Channel, ε = 10⁻³ | 1–5 | 5–10 | 10–16 | 16–20 | 20–30 | 30–50 | 50–77 | 77–100 | 100–200 | 200–300 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| AWGN | 1.60 | 1.22 | 1.13 | 1.08 | 1.03 | 0.97 | 0.92 | 0.88 | 0.84 | 0.81 |
| Rayleigh, `L*` | 4.50 | 3.10 | 2.67 | 2.44 | 2.23 | 1.97 | 1.76 | 1.62 | 1.46 | 1.32 |

The slope never becomes constant; it falls slowly toward 0.693. A straight line through the 50–300 bit values is nevertheless accurate to about 3% on AWGN and 6% on Rayleigh: `E/N0 ≈ 25 + 0.85·k` and `E/N0 ≈ 90 + 1.45·k` at ε = 10⁻³ [D, fit to the tables]. The fixed charge is worth about 30 bits of marginal energy on AWGN and about 60 on Rayleigh. Cost per message is therefore dominated by the fixed charge below about 30 bits (AWGN) or 60 bits (Rayleigh) and is roughly proportional to length only well beyond that. Doubling 10 bits to 20 costs 1.39–1.45 times; doubling 100 to 200 costs 1.61 (Rayleigh) to 1.76 times (AWGN).

### 2.6 Limits of these numbers

- They assume `2^k` fully orthogonal codewords, which needs `2^k·L` time-frequency cells. A real tone bank reuses tones; 3.4 measures that loss.
- Branches are independent and equal in mean (ideal interleaving, Rayleigh, block fading). Rician or line-like paths need less; two-ray paths with deep nulls and slow fades that leave fewer independent branches need more.
- Known timing and frequency, Gaussian noise of known level, no interference, no quantisation. Acquisition hypotheses add roughly `log2 A` bits of energy ([fading-limit study](../../Sol-6.2/fading-limit-and-mfsk-alternatives.md)).
- The peak constraint is not binding in this model: each branch is one tone dwell at full power, and the optimum branch SNR of 3–9 is reachable only when `(C/N0)·Tc` is at least that large. Otherwise [section 4](#4-very-low-rates-question-3) applies.

## 3. Practical codes for 10–300 bits (Question 2)

### 3.1 What the code sees

- **On an interleaved Rayleigh channel the per-symbol tone energies are sufficient statistics.** The likelihood of a codeword factorises over symbols and is linear in the energies. "Codeword-level" correlation has nothing more to collect; the 3.6–4.5 dB that the earlier review assigns to per-symbol noncoherent detection exists only on phase-stable paths [H, from the model; consistent with that review's own condition].
- **The alphabet sets a floor that no code crosses.** Symbol-wise limits from the [theory review](../../Opus-5.5/reviews/review-noncoherent-theory.md): 64 tones need at least 1.23 dB on AWGN and about 2.25 dB on Rayleigh; 1024 tones 0.16 and 1.43 dB. Four tones at rate 1/2 need 4.18 dB [P: Guillén i Fàbregas and Grant, via that review]. A 100-bit message with unconstrained orthogonal codewords needs 0.45 dB on AWGN (2.2). The choice between 4 and 64 tones is worth about 3 dB; the choice between good decoders is worth about 1 dB.
- **Cutoff rate** of the q-ary noncoherent channel with soft energies, `R0 = log2 q − log2(1 + (q−1)·D)` [D, `sme` §10]. It is the practical limit of sequential decoding and a fair guide for Viterbi decoding of moderate constraint lengths.

| q | AWGN: min `Eb/N0` (dB) | at `Es/N0` (dB) | code rate | Rayleigh: min `Eb/N0` (dB) | at `Es/N0` (dB) | code rate |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 8.29 | 5.6 | 0.53 | 10.14 | 3.6 | 0.22 |
| 4 | 5.95 | 6.5 | 0.57 | 8.18 | 4.1 | 0.20 |
| 16 | 4.12 | 8.2 | 0.63 | 7.03 | 4.6 | 0.14 |
| 64 | 3.34 | 9.5 | 0.69 | 6.77 | 4.8 | 0.11 |
| 256 | 2.92 | 10.6 | 0.72 | 6.70 | 4.8 | 0.08 |
| 1024 | 2.65 | 11.5 | 0.76 | 6.69 | 4.8 | 0.07 |

On Rayleigh fading the cutoff rate saturates at 6.7 dB however many tones are used, 4.5 dB above the 64-tone capacity. A decoder family bound to `R0` gives away that much on fading paths.

### 3.2 Family by family

**(a) Orthogonal, biorthogonal and first-order Reed–Muller codes with a fast Hadamard decoder.**
- Published: exact maximum-likelihood decoding by a fast Hadamard transform, with memory and computation savings from structured generator rows [P: Be'ery and Snyders 1986, abstract]; the Mariner (32,6) code and Green machine are in the [earlier review](../../Opus-5.5/reviews/review-gemm-decoding.md). Eight-tone MFSK with triple diversity has a 21.8 dB advantage over binary FSK for 6-bit characters at 10⁻³ on Rayleigh fading [P: Hahn 1962, abstract].
- Fit: a Hadamard codeword of ±1 chips needs phase coherence across chips, so on this channel the orthogonal code is the tone itself. With one tone among `q` repeated over `L` keyed looks, the tables of section 2 apply exactly for `k ≤ log2 q` (8.5 bits with 352 tones). As a binary outer code fed with bit metrics it inherits the bit-interleaving loss of about 1.9 dB at 64 tones ([fading-limit study](../../Sol-6.2/fading-limit-and-mfsk-alternatives.md)).
- Decoder: regular batch; the Hadamard matrix is an exact ±1 operand, so INT4/INT8 inputs with wide accumulators are exact [H]. Decisions do not depend on the noise level. Erasures are zeros.

**(b) Convolutional codes.**
- *Orthogonal and superorthogonal codes.* Error probability decreases exponentially with the energy over one constraint length and is markedly better than that of orthogonal block codes [P: Viterbi 1967, abstract]; very low rate orthogonal convolutional codes approach capacity for the aggregate of many coherent BPSK users [P: Viterbi 1990, abstract]. Both are coherent constructions.
- *Dual-k, Trumpis and other codes over GF(q) for q-ary orthogonal signals.* Two classes of nonbinary convolutional codes include dual-k and the Trumpis codes as special cases; codes optimal under a transfer-function bound were found by search, and their performance on AWGN, Rayleigh and partial-band channels is discussed [P: Ryan and Wilson 1991, abstract; no numbers obtained]. Against the worst-case partial-band jammer the best results come from M-ary signalling with optimum diversity and M-ary codes: Reed–Solomon, dual-k, orthogonal convolutional or concatenated [P: Ma and Poole 1984, abstract]. A dual-k code has only `q` states, so its Viterbi step is a `q × q` max-plus product.
- *Sequential decoding with K = 32.* A practical sequential decoder with M-orthogonal modulation reached within 7 dB of the Shannon limit with incoherent reception [P: Jordan 1966, abstract]. WSJT-X uses K = 32, rate 1/2 codes with a zero tail in JT4, JT9 and WSPR; the summary table gives 50% decode thresholds of −23 dB (JT4A), −26 dB (JT9A) and −31 dB (WSPR) in 2500 Hz [P: WSJT-X 2.7.0 User Guide, §18]. Converted with `C/N0 = SNR + 33.98 dB` and the listed durations and sync fractions [D, conversion]:

  | Mode (payload bits, code) | Threshold (dB) | `Eb/N0` total (dB) | data symbols only (dB) |
  | --- | ---: | ---: | ---: |
  | WSPR (50, K = 32 r = 1/2, 4-FSK with a sync bit per symbol) | −31 | 6.4 | 3.4 |
  | JT9A (72, K = 32 r = 1/2, 9-FSK) | −26 | 6.3 | 5.4 |
  | JT4A (72, K = 32 r = 1/2, 4-FSK) | −23 | 9.1 | 6.1 |
  | JT65A (72, Reed–Solomon (63,12), 65-FSK) | −25 | 7.1 | 4.1 |
  | Q65-60A (77, QRA (63,13) over GF(64) with 12-bit CRC) | −27.6 | 4.6 | 3.3 |
  | FT8 (77, LDPC (174,91), 8-GFSK) | −21 | 5.1 | 3.8 |
  | FST4-60 (77, LDPC (240,101), 4-GFSK) | −28.1 | 4.2 | 2.9 |
  | FST4-1800 (77, same) | −43.2 | 4.5 | 3.2 |
  | FST4W-120 (50, LDPC (240,74), 4-GFSK) | −32.8 | 4.6 | 3.3 |
  | FST4W-1800 (50, same) | −44.8 | 4.7 | 3.5 |

  The noncoherent orthogonal reference at 50% for 72–77 bits is about −1.6 dB ([earlier review](../../Opus-5.5/reviews/review-gemm-decoding.md)), so the best fielded modes are 4.5–5 dB above it in data energy and 6–8 dB in total. On data energy the K = 32 sequential code of WSPR (3.4 dB) equals the LDPC code of FST4W-120 (3.3 dB); WSPR loses by its 50% sync energy, not by its decoder. The drawback of sequential decoding is its computation: the distribution of decoder work has a Pareto tail [recalled; Jacobs and Berlekamp 1967, not read], which is an erasure mechanism tied to compute and is at odds with fixed-shape batches.
- *Tail-biting codes with list decoding.* On binary-input AWGN at (128,64), a memory-14 tail-biting code with wrap-around Viterbi decoding is within 0.07 dB of the normal approximation and memory 8 loses about 1 dB [P: Coşkun et al. 2019]. Expurgating outer functions bring the gap between the distance-spectrum bound and the random-coding union bound from over 1 dB to 0.23 dB at 10⁻⁶ for 64-bit messages [P: Wesel et al. 2023, abstract]. A 32-bit message with a rate-1/5 tail-biting code and an optimised CRC, **lowered to rate 32/864 by plain repetition**, outperforms the 5G broadcast-channel polar code in error rate and run time [P: King, Kwon, Yang, Ryan and Wesel 2022, abstract]. **On the noncoherent channel:** rate-1/2 codes over GF(4) with CRC-aided list Viterbi decoding, 64 four-ary information symbols, 4-FSK on AWGN, come within 0.59 dB of the random-coding union bound of that channel at 10⁻⁴ with memory 4, and about 0.9 dB with memory 2; the expected list size tends to 1 at high SNR [P: King, Ryan and Wesel 2022]. Larger alphabets and fading were left as future work there. The average list size is 100–1000 at a frame error rate of 10⁻³ in a related binary design, and a bound on the accepted metric controls the trade between error rate, complexity and undetected errors [P: Qu et al. 2025, abstract].
- Decoder: add-compare-select over a trellis, regular and batchable across hypotheses, but max-plus and therefore not a Tensor Core operation. `q^ν` states make memory 2 over GF(64) already 4096 states. INT8 metrics suffice; erasures are zero metrics; with linear energy metrics the survivor does not depend on the noise level.

**(c) Nonbinary LDPC and Q-ary repeat-accumulate codes.**
- Published: belief propagation over GF(q) has a Fourier-domain form whose check node is a transform of the message vector, and an extended min-sum form that keeps short sorted lists [P: Declercq and Fossorier 2007, abstract]. On binary-input AWGN at (128,64) a GF(256) code under belief propagation is 0.7 dB from the normal approximation, ordered-statistics reprocessing gains 0.5 dB more, no floor was seen down to 10⁻⁹, and the decoding complexity stays well above that of a binary turbo code [P: Coşkun et al. 2019]. Q65 uses a (65,15) code over GF(64) whose two CRC symbols (12 bits) are punctured, leaving 63 channel symbols for 13 information symbols, with thresholds in the table above [P: WSJT-X guide]. A nonbinary code with cyclic code-shift keying carries 360 bits at −11 dB SNR within 1.2 dB of the finite-length bound, its codeword doubling as the preamble [P: Saied, Ghouwayel and Boutillon 2022, abstract; coherent AWGN]. Decoder metrics that need no SNR knowledge exist for noncoherent orthogonal modulation [P: Guillén i Fàbregas and Grant 2007, abstract]; the numbers (0.6 dB, and 1.5 dB in Rayleigh) are in the [theory review](../../Opus-5.5/reviews/review-noncoherent-theory.md). Simple design rules exist for turbo codes over GF(q) with q-ary orthogonal modulation [P: Kim et al. 2011, abstract].
- Decoder: gathers (field multiplications permute a length-`q` vector), `q × q` Walsh–Hadamard products, elementwise products, a fixed number of iterations. That is a regular batch, and the transform is a ±1 matrix product of inner dimension `q` [H]. Probability-domain messages need FP16 with renormalisation or FP32; INT4/INT8 messages are not suitable for the transform-domain products [H]. The extended min-sum form is sort-based and branches. Erasures are uniform message vectors. The channel metric needs the noise level and mean SNR, or the SNR-free metrics above, or a small bank of hypotheses.
- Failure modes: a decoding threshold below which belief propagation fails abruptly, no maximum-likelihood guarantee at short length, and undetected errors unless a CRC is carried.

**(d) Polar and related codes.** A (128,71) polar code with a 7-bit CRC and list 32 is very close to the random-coding union bound; at (512,256) a list near 1024 is needed [P: Coşkun et al. 2019]. Nonbinary polar codes with cyclic code-shift keying are reported close to the finite-blocklength limits [P: Savin 2021, abstract; coherent]. No finite-length result for polar or nonbinary polar codes on noncoherent MFSK in Rayleigh fading was found. The decoder is sequential with sorting and pruning; it batches across hypotheses but is not a matrix product.

**(e) Reed–Solomon codes.** Maximum distance separable: any `n − k` erased symbols are recovered, and errors and erasures trade as `2e + s ≤ n − k`. Algebraic soft decoding clearly outperforms Guruswami–Sudan and generalised-minimum-distance decoding at polynomial complexity [P: Koetter and Vardy 2003, abstract]; the Franke–Taylor numbers for JT65 are in the earlier review. With diversity and parallel errors-and-erasures decoding a Reed–Solomon code neutralises narrowband interference whatever its power or statistics [P: Pursley and Stark 1985, abstract]. Decoding is finite-field algebra and trial erasure patterns: batchable, not a real-valued matrix product. It needs symbol decisions and reliabilities, not a noise level.

**(f) Ordered-statistics decoding and GRAND.** Order-4 reprocessing is within 0.1 dB of the normal approximation at (128,64) ([earlier review](../../Opus-5.5/reviews/review-gemm-decoding.md)). GRAND variants target short, high-rate codes: Fading-GRAND is reported for BCH (127,106) and (127,113) [P: Abbas, Jalaleddine and Gross 2022, abstract], and an erasure-aware syndrome check for a rate 105/128 random linear code under jamming [P: Ercan et al. 2023, abstract]. At rates of 1/4 they are the wrong tool. What is and is not Tensor-Core-shaped in ordered-statistics decoding [H]:
- *Is:* scoring the candidate codewords, a ±1 matrix times a real reliability matrix.
- *Is, with care:* re-encoding the test patterns. A product over GF(2) is not a real-valued product, but an integer product of 0/1 matrices followed by a parity reduction of every output is exact while the accumulator holds the row weight (at most `k`), which INT8 operands with INT32 accumulation, or FP16 accumulation up to 2048, satisfy. Over a prime field GF(p) with `p` tones the same holds with a reduction modulo `p`.
- *Is not:* the reliability sort, the Gaussian elimination per received word, and early stopping.

**(g) Exhaustive keyed bank, sectioning, and sparse regression.** See 3.4.

### 3.3 Comparison

Gap is the extra energy against the Rayleigh orthogonal reference of 2.3 at ε = 10⁻³, which is itself within about 0.7 dB (10 bits) to 0.3 dB (100 bits and more) of the published converse. Entries marked [H] are estimates assembled from the cited pieces and are the weakest numbers in this review.

| Family | Decoder shape | Operand precision | Erasures | Needs noise level? | Gap at 10 / 20 / 50 / 100 / 300 bits (dB) | Published noncoherent fading evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Keyed random bank, exhaustive, 64 tones | One-hot matrix × metric matrix; fixed shape | Binary × INT8/FP16, wide accumulate | Zero rows | No | 0.2 [D] / 0.7 [D, at ε = 10⁻²] / 2.4 as 2×25 / 3.7 as 4×25 / 5.5 as 12×25 ([D] sectioning + 0.8 [H] for a 25-bit bank) | This review's Monte Carlo only |
| Nonbinary LDPC or QRA over GF(64), transform-domain BP | Gathers, Hadamard products, elementwise; fixed iterations | FP16/FP32 | Uniform vectors | Yes, or SNR-free metric (0.6–1.5 dB) | n/a / n/a / 1.5–2.5 [H] / 1.5–2 [H] / 1–1.5 [H] | Q65 on spread channels (earlier reviews); capacity-approaching thresholds asymptotically |
| CRC-aided tail-biting code over GF(q), list Viterbi | Max-plus trellis; regular, not a matrix product | INT8 | Zero metrics | No | 0.3–1 [H] / 0.5–1 [H] / 1.5–2 [H] / 1.5–2 [H] / 2–2.5 [H] | 4-FSK on AWGN within 0.59 dB of its own bound [P]; Rayleigh discussed without accessible numbers |
| Reed–Solomon over GF(64), soft or errors-and-erasures | Finite-field algebra, trial patterns | Symbols + reliabilities | Native, `n − k` | No | n/a / n/a / 2–3 [H] / 2–3 [H] / needs GF(256) | JT65 (earlier review); FH/MFSK with partial-band interference [P] |
| K = 32 sequential | Data-dependent tree search | INT8 | Zero metrics | Bias needs SNR | bounded by `R0`: at least 4.5 dB above 64-tone capacity on Rayleigh [D] | WSPR, JT9, JT4 on AWGN [P] |
| Binary code with OSD on bit metrics | Mixed (3.2 f) | INT8 reliabilities | Zero | No | about 1.9 dB bit-interleaving loss at 64 tones plus 0.1–0.5 | FT8/FT4 on ITU channels (earlier review) |
| Polar, CRC-aided list | Sequential, sort and prune | INT8 | Zero | No | not found for this channel | None found |

### 3.4 The keyed bank, sectioning and sparse regression

**Bank against the orthogonal reference** [D, `mc` §1]. `2^k` random q-ary codewords over `n` symbol slots, one independent Rayleigh fade per slot, exhaustive maximum-likelihood decoding. The reference is the exact orthogonal requirement with the same number of branches.

| k | Tones `q` | Slots `n` | Codebook | Target ε | Error at the reference energy | Extra energy for the target |
| ---: | ---: | ---: | --- | ---: | ---: | ---: |
| 10 | 64 | 18 | random | 10⁻³ | 1.49·10⁻³ | 0.21 dB |
| 10 | 64 | 18 | product 8×8 | 10⁻³ | 2.17·10⁻³ | 0.45 dB |
| 10 | 16 | 18 | random | 10⁻³ | 3.48·10⁻³ | 0.74 dB |
| 10 | 8 | 18 | random | 10⁻³ | 9.33·10⁻³ | 1.42 dB |
| 10 | 4 | 18 | random | 10⁻³ | 4.41·10⁻² | more than 2 dB |
| 10 | 64 | 9 | random | 10⁻³ | 2.23·10⁻³ | 0.65 dB |
| 10 | 64 | 36 | random | 10⁻³ | 1.18·10⁻³ | 0.07 dB |
| 16 | 64 | 14 | random | 10⁻² | 2.00·10⁻² | 0.55 dB |
| 16 | 64 | 14 | product 8×8 | 10⁻² | 2.50·10⁻² | 0.73 dB |
| 16 | 64 | 28 | random | 10⁻² | 1.41·10⁻² | 0.18 dB |
| 20 | 64 | 15 | random | 10⁻² | 3.07·10⁻² | 0.71 dB |
| 20 | 256 | 15 | random | 10⁻² | 1.49·10⁻² | 0.28 dB |

The 20-bit rows have 10⁴ trials per point (about ±0.1 dB); the others 10⁵ to 2·10⁶.

- With 64 tones and as many slots as the optimum branch count, the random bank loses 0.21 dB at 10 bits, 0.55 dB at 16 bits and 0.71 dB at 20 bits (the last two at ε = 10⁻²). The loss is the price of reusing tones: two codewords share a tone in a slot with probability `1/q`, so each codeword has neighbours at reduced distance, and their number grows with `2^k`. With 256 tones the 20-bit loss falls to 0.28 dB.
- Doubling the slots (18 to 36, 14 to 28) lowers the collision loss to 0.07–0.18 dB but the reference itself is then 0.27–0.28 dB above the optimum, so the total barely changes. Halving them (9 slots) costs 0.65 dB on top of a 0.46 dB worse reference.
- The alphabet is the lever: 16 tones cost 0.74 dB and 8 tones 1.42 dB at 10 bits. A wide Doppler-tolerant spacing that leaves about 22 tones in the passband therefore costs the bank roughly half a dB at 10 bits and more at 20 [H, interpolated].
- The product-structured bank costs 0.24 dB more than the random one at 10 bits and 0.18 dB at 16 bits.

**Shape.** Scoring is `C·a`, with `C` the one-hot `[2^k × n·q]` codebook matrix and `a` the stacked metrics. Dense, it does `q` times more multiply-adds than the `n` gathers per codeword that carry information; that padding must be counted separately from useful work. A **product-structured bank** removes most of it [H, design idea of this review]: write the message as `(u1, u2)` and make the tone in slot `i` the pair `(a_i(u1), b_i(u2))` with `q = q1·q2`. The score matrix over `(u1, u2)` is then `U1ᵀ·W`, where `U1` is a one-hot `[n·q1 × 2^(k/2)]` matrix and `W[(i,a), u2] = A_i[a, b_i(u2)]` is a gathered `[n·q1 × 2^(k/2)]` metric matrix: a true matrix product of inner dimension `n·q1` producing all `2^k` scores, with padding `q1` instead of `q`. The Monte Carlo rows marked 8x8 measure its cost in sensitivity.

**Sectioning** [D, `sme` §4]. A k-bit message as `s` independently decoded sections, no outer code, ε = 10⁻³ for the whole message:

| k | sections × bits | AWGN loss (dB) | Rayleigh loss (dB) |
| ---: | --- | ---: | ---: |
| 20 | 2 × 10 | 1.68 | 1.92 |
| 50 | 2 × 25 | 1.18 | 1.56 |
| 50 | 5 × 10 | 3.36 | 4.08 |
| 100 | 4 × 25 | 2.11 | 2.92 |
| 100 | 5 × 20 | 2.58 | 3.49 |
| 100 | 10 × 10 | 4.33 | 5.46 |
| 200 | 8 × 25 | 2.84 | 4.09 |
| 300 | 12 × 25 | 3.19 | 4.70 |
| 300 | 30 × 10 | 5.48 | 7.28 |

The loss is mostly the fixed charge of 2.5 paid `s` times: `(s·a + b·k)/(a + b·k)` with the fitted constants reproduces the 25-bit rows within about 0.4 dB (it overstates the 10-bit rows, where the linear fit does not hold). A hard-decision outer Reed–Solomon code over 25-bit sections does not recover it: a (24,12) code over twelve sections tolerates a section error rate of 0.07 [D, binomial tail], and the doubled number of sections costs about as much as the relaxed target saves [H, estimate from the tables]. Soft or list information from the sections would be needed, and that is the nonbinary-code design of 3.2 (c) with larger symbols.

**Fixed geometry makes some sectioning unavoidable.** With an unknown message length and no length header, the format must be a calendar of fixed blocks. The energy for a k-bit message is then `ceil(k/b)` blocks of `b` bits. The comparison that matters is between block sizes, not between a bank and an ideal 300-bit code: 10 bits in a 24-bit block cost about 1.5–1.6 times the 10-bit energy; 100 bits in 24- or 25-bit blocks cost about 3–3.7 dB more than one 100-bit code on the Rayleigh model [D, from the tables, the 24-bit values interpolated].

**Sparse regression codes.** A sparse superposition codeword is `A·β` with one nonzero per section. With tones as the dictionary, `A` is orthogonal, the sections are the symbol slots, and independent sections are uncoded MFSK: the coding gain of sparse regression comes from a dense non-orthogonal dictionary, which is neither constant-envelope nor noncoherently detectable. Approximate message passing has nothing to do when the cell energies are already sufficient statistics. The relation to matrix products is only that any codebook is scored by `C·a` [H].

## 4. Very low rates (Question 3)

Rates of 1/100 to 1/10,000 mean accumulating energy over hours or days. Four elements can carry the reduction: a longer tone dwell, repeated looks combined noncoherently, a lower code rate, and a larger orthogonal alphabet. They are not equivalent.

**Noncoherent combining loss on a non-fading channel** [D, `sme` §5]. The message energy is split over `L` looks with independent unknown phases and the looks are summed as energies (the standard combiner; the exact rule sums `ln I0` terms, which is the same at low per-look SNR and slightly better at high). ε = 10⁻³.

| L | 10 bits: per-look SNR (dB) | loss (dB) | 100 bits: per-look SNR (dB) | loss (dB) | 1 bit: loss (dB) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 13.97 | 0 | 20.45 | 0 | 0 |
| 4 | 9.01 | 1.06 | 14.84 | 0.41 | 1.43 |
| 16 | 4.53 | 2.60 | 9.65 | 1.24 | 3.26 |
| 64 | 0.52 | 4.61 | 5.03 | 2.64 | 5.50 |
| 256 | −3.11 | 7.01 | 0.97 | 4.60 | 8.04 |
| 1024 | −6.47 | 9.66 | −2.67 | 6.99 | 10.80 |
| 4096 | −9.68 | 12.48 | −6.02 | 9.65 | 13.67 |

Below about 0 dB per look, each further factor of four in looks costs 2.4–2.9 dB, approaching the square-root law of 3 dB: the energy needed grows as `sqrt(L)`. In radar practice the same effect is tabulated as integration loss; for typical detection settings the gain of `N` looks behaves as `N^0.6` to `N^0.8`, not `N^0.5` and not `N` [P: MathWorks radar documentation, citing Barton and Richards]. The anti-jam literature calls it coherence loss or combining loss and identifies it as the price of time diversity [P: Viterbi 1982; Lee, French and Miller 1984; abstracts].

**Rayleigh fading with a capped branch SNR** [D, `sme` §6]. If one coherence interval holds only `x = (C/N0)·Tc`:

| Branch SNR `x` (dB) | 10 bits: branches | loss vs optimum (dB) | 100 bits: branches | loss vs optimum (dB) |
| ---: | ---: | ---: | ---: | ---: |
| +20 | 3 | 6.56 | 6 | 4.01 |
| +13 | 6 | 2.57 | 14 | 0.69 |
| +10 | 8 | 0.82 | 25 | 0.20 |
| +6 | 17 | 0.09 | 62 | 0.15 |
| +3 | 36 | 0.35 | 145 | 0.84 |
| 0 | 89 | 1.28 | 391 | 2.15 |
| −3 | 259 | 2.92 | 1202 | 4.02 |
| −6 | 854 | 5.10 | 4090 | 6.34 |
| −10 | 4744 | 8.55 | 23179 | 9.88 |

Below 0 dB each dB of branch SNR costs 0.6–0.9 dB of energy; this is the quadratic regime of the fading-limit study seen from the code's side. Branches stronger than about +10 dB cost as well, through lost diversity.

**Repetition against a lower-rate code** [D, `mc` §2]. Mutual information of one q-ary symbol sent on `L` Rayleigh looks at per-look SNR `g`, square-law combined; "efficiency" is `I_L/(L·I_1)`, the fraction of what an ideal code over `L` distinct symbols would collect; "one look" is the same energy in a single look.

| Per-look SNR | Looks `L` | `I_L` (bits, of 6) | Efficiency | One look with the same energy (bits) |
| ---: | ---: | ---: | ---: | ---: |
| −10 dB | 16 | 0.106 | 1.00 | 0.82 |
| −10 dB | 64 | 0.422 | 1.00 | 3.07 |
| −10 dB | 256 | 1.63 | 0.96 | 4.99 |
| −10 dB | 1024 | 4.74 | 0.70 | 5.72 |
| −5 dB | 8 | 0.466 | 0.99 | 1.43 |
| −5 dB | 32 | 1.77 | 0.94 | 3.84 |
| −5 dB | 64 | 3.18 | 0.85 | 4.76 |
| −5 dB | 128 | 4.88 | 0.65 | 5.33 |
| 0 dB | 2 | 0.818 | 0.98 | 1.09 |
| 0 dB | 4 | 1.56 | 0.93 | 2.20 |
| 0 dB | 8 | 2.82 | 0.84 | 3.46 |
| 0 dB | 16 | 4.45 | 0.67 | 4.49 |
| +5 dB | 1 | 1.79 | 1 | 1.79 |
| +5 dB | 2 | 3.09 | 0.86 | 3.04 |
| +5 dB | 4 | 4.66 | 0.65 | 4.18 |

64 tones; Monte Carlo standard errors are at most 0.003 bit. With 1024 tones the pattern is the same against the fraction of `log2 q` carried: efficiency 0.98 at 0.17, 0.95–0.98 at 0.33–0.37, 0.85–0.87 at 0.60–0.66. Above about +5 dB per look the single look no longer wins: four looks at +5 dB carry 4.66 bits against 4.18 for one look at +11 dB, which is the optimum branch SNR of 2.3 seen through mutual information.

Conclusions:

1. **Dwell first.** Lengthen the coherent look until the branch SNR at the design point is between about +3 and +10 dB, or until the coherence of the path stops it. Nothing else recovers what a weak look loses: in the table above one look at 64 times the SNR carries about seven times the information of 64 weak looks at −10 dB.
2. **Then repeat looks, with keyed placement.** When the path cannot support a stronger look, repeated looks combined as energies are the only tool, and they are as good as any code: efficiency stays at or above 0.93 while the combined symbol carries less than about 0.3 of `log2 q` bits, is about 0.85 at one half and 0.65–0.70 at 0.8 of `log2 q`. Published support in the coherent setting: incremental redundancy gains over repetition (Chase) combining are largest at high code rates and modulation orders and small for low ones [P: Frenger, Parkvall and Dahlman 2001, abstract]; they grow with the initial rate and shrink, or reverse, when the SNR varies between transmissions [P: Cheng 2006, abstract]; and the rate-32/864 example of 3.2 (b).
3. **Hold the code rate near 0.2–0.35 of `log2 q` on fading paths and do not lower it further.** The symbol-wise optimum for 64 tones in Rayleigh fading is about 1.5 bits per symbol at a symbol SNR near 4 dB (theory review); for fixed alphabet the required energy per bit diverges as the rate goes to zero [P: Stark 1985 and Guillén i Fàbregas and Grant, via that review]. A code of rate below about 0.2 buys nothing over repetition and costs decoder size. A code of rate above one half makes repetition wasteful (0.7 dB at one half, 1.6–1.9 dB at 0.8).
4. **A larger orthogonal alphabet per code symbol is free of combining loss but limited by the grid.** Lower rate through more tones per symbol does not split energy, and it lowers the alphabet's floor (3.1). The tone count is set by the passband and by the Doppler-tolerant spacing, so this element is fixed by the channel configuration and cannot be extended at will. A code symbol made of several tone slots is a short orthogonal-like inner code only if those slots are scored jointly, which is what the bank does; scored slot by slot it is repetition or a nonbinary code again [H].

So the element that must not be asked to carry the rate reduction is the code. The dwell carries it while the path allows; looks carry the rest; the code stays where it is efficient. Low-rate turbo-Hadamard codes reach a bit error rate of 10⁻⁵ at −1.2 dB with 65,534-bit blocks on coherent AWGN [P: Ping, Leung and Wu 2003, abstract]; that route needs phase and very long blocks and does not transfer.

## 5. Interference, robust metrics, erasures and interleaving (Question 4)

The sibling [OTHR review](review-othr-interference.md) covers the radar waveforms, what they do to a tone receiver before the code, pre-transform excision, a clip-level table for the weak-look regime and a demonstration of periodic-interleaver failure. This section covers the decoder side and does not repeat it.

### 5.1 Classical results that carry over

- **Worst case without coding.** A jammer that concentrates its power in part of the band or on tones makes the error rate of uncoded FH/MFSK fall only inversely with SNR [P: Houston 1975, as summarised by Levitt 1985 and by Viterbi 1982]. One tone per M-ary band is 4.3 dB more effective than worst-case partial-band noise for binary signalling and 10.5 dB for 16-ary [P: Levitt 1985, JPL Publication 84-90].
- **Diversity with side information removes the worst case.** With optimum diversity, linear energy combining and perfect knowledge of which hops are hit, binary FH/FSK against the worst one-tone-per-band jammer gains 35 dB over no diversity at 10⁻⁵ and ends 1.6 dB from broadband noise; 4-ary ends within 3.6 dB and 16-ary within 7.4 dB; the optimum is insensitive to small changes in the diversity order [P: Levitt 1985]. The exponential dependence is restored by time diversity, which is a repetition code [P: Viterbi and Jacobs 1975, as summarised by Levitt].
- **Without side information a summed energy metric gets worse with more diversity.** With square-law linear combining and no knowledge of the jamming state, more hops per bit gave higher error probabilities [P: Lee, French and Miller 1984, abstract]. Per-hop gain control or clipping before combining restores a diversity gain when the jamming reduction exceeds the combining loss [P: Lee, Miller and Kim 1984]. A self-normalising receiver does so with no knowledge of signal or jamming levels [P: Miller, Lee and Kadrichu 1986].
- **Coding.** Capacity and cutoff rate were computed for soft and hard decisions with and without side information; the companion paper on coded performance treats soft decisions only with side information and hard decisions with or without [P: Stark 1985, Parts I and II, abstracts]. Against the worst-case partial-band jammer the best performance is obtained with M-ary signalling, optimum diversity and M-ary codes [P: Ma and Poole 1984]. Nonlinear combining with M-ary modulation and coding is effective, and thermal noise cannot be neglected when choosing the diversity order [P: Lee, French and Miller 1988].
- **Shared randomness.** For an arbitrarily varying channel, a code randomised by a key the interferer does not hold achieves the random-code capacity [recalled from Lapidoth and Narayan 1998 and Ahlswede 1978; metadata verified, text not re-read]. The keystream is exactly that resource: it makes any interferer that does not know the key equivalent, on average over the key, to a memoryless one with the same hit fraction.

### 5.2 Metrics without side information

Published options, all for noncoherent FSK with diversity:

| Metric | What it needs | Published finding |
| --- | --- | --- |
| Clipped-linear combining | A measurement of the signal level | Performs well in rejection and in the SNR required; depends on measuring the signal level at the detector output [P: Keller and Pursley 1987] |
| Ratio-threshold quality bit (largest over second-largest output) | Nothing | Up to 6 dB better against a tone jammer in a coded system; tone and partial-band nearly equal and slightly worse than broadband noise [P: Viterbi 1982] |
| Ratio-statistic combining | Nothing | Judged an excellent combining scheme for partial-band interference and Rayleigh fading [P: Keller and Pursley 1989] |
| Self-normalisation | Nothing | Diversity gain without level knowledge [P: Miller, Lee and Kadrichu 1986] |
| Noise-normalisation | Per-hop noise estimate | Diversity greatly reduces the degradation in Rician fading with partial-band interference [P: Robertson and Ha 1992] |
| Product combining | Nothing | Comparable to the clipper without needing the SNR [P: Viswanathan and Taghizadeh 1988]; good multitone rejection [P: Teh, Kot and Li 1999] |
| Order statistics with normalised envelopes | Nothing | Significantly better than normalisation alone in multitone jamming, slightly worse in noise jamming [P: Gulliver 1991] |
| Iterative estimation of per-dwell noise level inside the decoder | A decoder with soft outputs | Turbo and convolutional codes with expectation-maximisation estimates of amplitude and noise density per block or dwell [P: Kang and Stark 1998; El Gamal and Geraniotis 2002; Cheng, Valenti and Torrieri 2007; Torrieri, Cheng and Valenti 2008] |

**Inside a bank decoder** [D, `mc` §3]. 10 bits, 64 tones, random bank, Rayleigh fading, two designs with the same 6 dB of extra energy: A keeps 18 slots and makes each 6 dB stronger; B uses 72 slots at the no-margin slot SNR. "Slot hits" raise the noise of all 64 cells of a slot (pulsed or broadband interference); "cell hits" raise single cells (narrowband interference after keyed tone permutation). The slot noise estimate is the median of the slot's own 64 energies; the floor is the lower quartile of the slot estimates in the same message.

Slot hits, interference 20 dB above the noise; message error probability, 0 meaning no error in 10⁵ (B) or 2·10⁵ (A) trials:

| Metric | A, ρ = 0.10 | A, 0.25 | A, 0.50 | A, 0.75 | B, 0.25 | B, 0.50 | B, 0.75 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Summed energy | 0.78 | 0.97 | 0.99 | 0.99 | 1.0 | 1.0 | 1.0 |
| Clip at 8 × known quiet noise | 0 | 3.5·10⁻⁵ | 1.1·10⁻² | 0.22 | 0 | 2.6·10⁻⁴ | 0.10 |
| Slot-median normalised | 0 | 1.0·10⁻⁵ | 5.8·10⁻³ | 0.18 | 0 | 1.1·10⁻⁴ | 0.077 |
| Normalised, clipped at 8 | 0 | 5.0·10⁻⁵ | 1.3·10⁻² | 0.28 | 0 | 2.0·10⁻⁴ | 0.12 |
| Normalised, clipped, weighted by floor/median | 0 | 1.5·10⁻⁵ | 5.1·10⁻³ | 0.22 | 0 | 0 | 0.066 |
| log(1 + y/median) | 0 | 1.2·10⁻⁴ | 2.0·10⁻² | 0.33 | 0 | 1.4·10⁻³ | 0.26 |
| Rank within slot | 3.3·10⁻³ | 4.9·10⁻² | 0.40 | 0.86 | 3.3·10⁻⁴ | 8.4·10⁻² | 0.73 |
| Genie maximum likelihood (knows hits, level, SNR) | 0 | 5·10⁻⁶ | 2.1·10⁻³ | 0.11 | 0 | 0 | 5.0·10⁻³ |
| Genie erasure (knows hits) | 0 | 5·10⁻⁶ | 2.3·10⁻³ | 0.12 | 0 | 0 | 5.0·10⁻³ |

Cell hits, 20 dB above the noise:

| Metric | A, ρ = 0.01 | A, 0.03 | A, 0.10 | A, 0.20 | B, 0.01 | B, 0.10 | B, 0.20 | B, 0.30 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Summed energy | 0.80 | 0.97 | 0.99 | 0.99 | 0.98 | 1.0 | 1.0 | 1.0 |
| Clip at 8 × known quiet noise | 0 | 0 | 1.6·10⁻⁴ | 7.2·10⁻³ | 0 | 2.0·10⁻⁵ | 2.6·10⁻³ | 3.8·10⁻² |
| Slot-median normalised | 0.79 | 0.97 | 0.99 | 0.99 | 0.98 | 1.0 | 1.0 | 1.0 |
| Normalised, clipped at 8 | 0 | 5·10⁻⁶ | 4.7·10⁻⁴ | 3.0·10⁻² | 0 | 7.0·10⁻⁵ | 5.9·10⁻² | 0.53 |
| Normalised, clipped, weighted by floor/median | 0 | 5·10⁻⁶ | 4.7·10⁻⁴ | 2.9·10⁻² | 0 | 8.0·10⁻⁵ | 4.9·10⁻² | 0.47 |
| log(1 + y/median) | 1.5·10⁻⁵ | 6.8·10⁻⁴ | 7.2·10⁻² | 0.46 | 0 | 6.0·10⁻² | 0.50 | 0.81 |
| Rank within slot | 1.1·10⁻⁴ | 3.5·10⁻⁴ | 2.1·10⁻³ | 3.4·10⁻² | 0 | 1.0·10⁻⁵ | 5.1·10⁻⁴ | 2.7·10⁻² |
| Genie maximum likelihood | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Genie erasure | 0 | 0 | 0 | 2.0·10⁻⁵ | 0 | 0 | 0 | 0 |

At 40 dB the bounded metrics behave the same within a factor of about two; the logarithmic metric fails already at 1% of cells (4.6·10⁻² for A), and the known-noise clip improves against slot hits because every hit cell saturates (B at ρ = 0.75: 6.2·10⁻³ against the genie's 5.3·10⁻³). The full tables are in the output file.

Reading of the tables:

- The **summed energy metric collapses** at the smallest hit fraction tried, which is the Lee–French–Miller result at codeword level. A design with 6 dB of margin goes from no observed errors to 80–98% errors when 1% of cells are hit.
- **M-ary signalling supplies its own side information.** Each slot has `q − 1` noise-only cells, so the median of the slot is a noise estimate that costs nothing and is hardly affected by the signal cell.
- **Normalisation alone handles slot hits and not cell hits; clipping alone handles both when the quiet noise level is known; normalisation plus clipping handles both without that knowledge.** With the floor weighting, the 72-slot design shows no error with half of its slots hit, as the genie does.
- **The shortfall against the genie is large when many single cells are hit** (10–30% of cells): from about 10⁻⁴ to 0.5, against no errors. Three things would narrow it and were not tested: a clip level matched to the signal cell (8 times the noise is too high for design B, whose signal cells average 4.7), erasure decisions of the ratio-threshold or Bayesian kind, and per-tone noise estimates learned over time when the interferer is persistent, which the sibling review recommends. The clip level's dependence on the signal level is the drawback Keller and Pursley name; in the weak-look regime of the sibling review the right level is 2–3 times the noise.
- **Rank metrics** are the best of the blind metrics against many cell hits at moderate branch SNR (design B, 20% of cells: 5·10⁻⁴ against 5·10⁻²) and the worst against slot hits, where they give a hit slot the same weight as a clean one. A logarithmic metric is poor against cell hits.
- **Design B is far more tolerant than design A** at equal energy, which leads to 5.5.
- All of these metrics are elementwise operations and one order statistic per fixed-size slot. They fit a fixed-shape batch ahead of the scoring product.

### 5.3 Errors and erasures

- A flagged erasure costs its share of energy and nothing more: zero rows for a bank or a trellis, uniform vectors for a nonbinary code, native in Reed–Solomon decoding.
- Deciding what to erase: the ratio-threshold test [P: Viterbi 1982]; Bayesian erasure insertion, which lowers error probabilities by several orders of magnitude against strong partial-band interference and by up to six orders with fading relative to hard decisions [P: Baum and Pursley 1992, 1997].
- With many looks per code symbol the distinction softens: a clipped, normalised look that was hit contributes almost nothing, which is an erasure of that look without a decision [H].
- A signal outage is different from interference. Looks without signal add noise to every hypothesis equally. In the weak-look regime, unflagged outage looks cost the linear law `−10·log10(1−ρ)` in SNR, whereas flagged ones cost half as many dB [H, deflection argument: `d² ∝ ((1−ρ)·L·g)²/L` against `(1−ρ)·L·g²`]. Flagging outages needs an estimate of signal presence over many looks, which is the quiet test of section 6.

### 5.4 Interleaver design

**What to avoid.** Periodic block interleavers and unkeyed convolutional interleavers. A periodic interferer whose period is commensurate with the interleaver's sends its hits to the same code symbols in every look. Interferers with periods exist at every scale relevant here: radar sweeps at 7 to about 100 per second, bursts of seconds, on-off cycles such as 50 s in 90 s [P: IARU Region 1 monitoring pages; details in the sibling review], hourly schedules and the diurnal cycle. The textbook caution about periodic interleavers is recalled, not re-read; the sibling review demonstrates it numerically.

**What to use.** A keyed pseudo-random assignment of every look to a time-frequency cell, redrawn from the keystream for every block, with a spread constraint.

- *Keyed and aperiodic*, so that an interferer without the key hits each code symbol's looks as an independent sample (5.1, last item).
- *Many looks per code symbol* where the regime allows it. Then a hit fraction `ρ` removes about the same fraction of every symbol's looks, and the code sees a uniform SNR loss in place of erased symbols [H; the tables in 5.5 quantify it].
- *A spread constraint* so that the looks of one symbol are not adjacent: the S-random rule, in which each new position differs by more than `S` from the previous `S` choices, succeeds in reasonable time for `S < sqrt(N/2)` [P: Divsalar and Pollara 1995]. Unconstrained random placement leaves clusters. Ramsey and Forney interleavers give a guaranteed separation with minimum delay and storage [P: Ramsey 1970; Forney 1971, abstracts], but they are periodic and need a flush that conflicts with quiet-slot termination.
- *Per block.* A block's looks stay inside its own calendar slot so that a quiet slot is really empty (section 6).

**Depth against fade duration** [D by formula, H in use]. For Rayleigh fading with a Gaussian Doppler spectrum of width `D = 2σ`, the level-crossing rate at a power level `ρ²` relative to the mean is `sqrt(π)·D·ρ·exp(−ρ²)` and the mean fade duration is `(exp(ρ²) − 1)/(sqrt(π)·D·ρ)`. Independent branches are about `1/B_eff = 0.56/D` apart.

| `D` | Fades below −3 dB: mean length, spacing | Below −10 dB: mean length, spacing | Branch spacing | Span of 18 / 37 branches in time alone |
| ---: | --- | --- | ---: | --- |
| 1 Hz | 0.52 s, 1.3 s | 0.19 s, 2.0 s | 0.56 s | 10 s / 21 s |
| 0.1 Hz | 5.2 s, 13 s | 1.9 s, 20 s | 5.6 s | 1.7 min / 3.5 min |
| 0.01 Hz | 52 s, 2.2 min | 19 s, 3.3 min | 56 s | 17 min / 35 min |

Rules that follow:

1. A block's span in time, multiplied by the number of independent coherence bands its hops cover, must supply at least the lower end of the "within 0.5 dB" range of 2.3: 9 branches for 10 bits, 16 for 100. For blocks lasting minutes to hours this is automatic at 0.1 Hz and more; for short blocks at high C/N0 on slow paths it is not, and frequency diversity from hopping is the only source.
2. A fade or outage of length `T_o` inside a block of span `T` erases `T_o/T` of the looks. With margin `X` the block survives while that fraction stays below the entry of 5.5. With many weak looks per symbol the block span must be about 1.3 times the outage at a 6 dB margin and 1.8 times at 3 dB; with the energy-optimal few-branch design the factors are 1.9 and 2.8. Focusing fades of ten minutes and flare fadeouts with means of 13–39 minutes (fading-limit study) therefore need block spans of roughly 13–50 minutes at 6 dB and 18–70 minutes at 3 dB in the first case. The sibling review's rule of ten times the longest burst is the same statement with under 0.5 dB of margin consumed.
3. Diurnal closures of hours cannot be bridged inside a block unless the block spans most of a day. They belong to a layer above: repeated blocks or an outer block-erasure code over days, with the outage flagged by the quiet test.

### 5.5 From margin to erased fraction

Ideal erasure decoding of a channel that is linear in energy tolerates `ρ = 1 − 10^(−X/10)`. Three things change that number: how many branches the design has, whether the looks are weak, and whether the erased set is fixed or random. All entries [D, `sme` §7], known erasure positions, ε = 10⁻³; pairs are 10 bits / 100 bits. The margin `X` makes every surviving branch `X` dB stronger than the no-margin design.

| Margin `X` | Ideal `1 − 10^(−X/10)` | Weak-look limit `1 − 10^(−2X/10)` | Energy-optimal design (18 / 37 branches): fixed erased set | same, random erasures | Branches at 0 dB (89 / 391): fixed set | same, random | Branches at −6 dB (854 / 4090): random |
| ---: | ---: | ---: | --- | --- | --- | --- | --- |
| 3 dB | 0.50 | 0.75 | 0.44 / 0.46 | 0.36 / 0.40 | 0.60 / 0.63 | 0.57 / 0.62 | 0.69 / 0.70 |
| 6 dB | 0.75 | 0.94 | 0.61 / 0.68 | 0.52 / 0.59 | 0.81 / 0.84 | 0.77 / 0.83 | 0.89 / 0.90 |
| 10 dB | 0.90 | 0.990 | 0.78 / 0.81 | 0.62 / 0.72 | 0.91 / 0.94 | 0.87 / 0.92 | 0.96 / 0.97 |
| 13 dB | 0.95 | 0.997 | 0.83 / 0.86 | 0.65 / 0.76 | 0.93 / 0.96 | 0.90 / 0.95 | 0.98 / 0.99 |
| 16 dB | 0.975 | 0.999 | 0.83 / 0.89 | 0.67 / 0.79 | 0.96 / 0.98 | 0.91 / 0.96 | 0.986 / 0.992 |
| 20 dB | 0.990 | 1.000 | 0.89 / 0.92 | 0.68 / 0.81 | 0.97 / 0.985 | 0.92 / 0.97 | 0.990 / 0.996 |
| Sensitivity cost of the design | | | 0 | 0 | 1.3 / 2.2 dB | 1.3 / 2.2 dB | 5.1 / 6.3 dB |

**If the margin is spent on extra branches instead**, `n = L*·10^(X/10)` branches at the optimum branch SNR, the ideal fraction is tolerated by construction, but the tolerance to extra path loss is only 2.7, 5.0, 7.7, 9.5, 11.2 and 13.4 dB for `X` = 3, 6, 10, 13, 16, 20 dB (10 bits; 2.8 to 13.9 dB for 100 bits) [D]. Path-loss margin and erasure margin are different resources: one is energy per branch, the other is the number of branches.

Caveats:

- **Positions known.** Unknown positions with a summed energy metric give no tolerance at all (5.2). With the normalised and clipped metric the Monte Carlo of 5.2 shows the remaining shortfall against the genie.
- **Finite diversity.** With few strong branches the survivors must still supply diversity; margin as SNR saturates (0.68 at 20 dB for 18 branches).
- **Random against fixed.** A keyed interleaver turns an interferer covering a fraction `p` into independent erasures; with 18 branches the count of survivors fluctuates and the tolerable `p` is well below the tolerable fixed fraction.
- **Signal outages** in the weak-look regime follow the ideal column when unflagged and the weak-look column only when flagged (5.3).
- **The margin is shared** with fading deciles, noise deciles and receiver losses. The sibling review gives the radar levels each option absorbs when nothing is excised; a strong unexcised radar exhausts any option.
- **A figure to publish** [H]: design the format with branches near 0 dB at the design point and quote the random-erasure column for the worse of 10 and 100 bits: 0.57, 0.77, 0.87, 0.90, 0.91, 0.92 for 3, 6, 10, 13, 16, 20 dB.

## 6. Physical end of message with fixed geometry (Question 5)

**Literature.**

- *Codeword or noise.* When each frame is either silent or carries a codeword, the optimum rule that trades decoding error, false alarm and misdetection is known, with exact exponents; earlier decoders were strictly suboptimal [P: Weinberger and Merhav 2014, abstract]. Finite-length bounds that include false alarm and misdetection, for detection jointly with decoding or on a separate preamble, show where a system is limited by detection and not by decoding [P: Lancho, Östman and Durisi 2021, abstract]. Forney's erasure option is the same test from the decoder's side [P: Forney 1968, abstract].
- *Asynchronism.* Sending `B` bits at an unknown time costs `(B + H̄)` times the synchronous cost per bit, `H̄` being the timing uncertainty in bits, when idling is free [P: Chandar, Tchamkerten and Tse 2013, abstract]. The end of a message is the same problem mirrored: silence is free, and telling it from signal costs evidence.
- *Preamble-less frames.* A nonbinary codeword can serve as its own preamble and be detected at −11 dB SNR [P: Saied, Ghouwayel and Boutillon 2022, abstract].
- *Blind rate or length detection.* Eight-rate variable-rate transmission with a 16-bit CRC as the only rate indicator loses about 0.1 dB at a frame error rate of 10⁻² in two-path Rayleigh fading against a known rate [P: Okumura and Adachi 1996, abstract]. Third-generation cellular blind transport-format detection checks a CRC and a path-metric threshold at each candidate end position, with false detection near `2^(−n)` for an n-bit CRC [search summary of 3GPP TS 25.212 Annex A; unverified]. These select among lengths by content, which the project's contract forbids as a way to choose DSP geometry; they are relevant only as integrity checks.
- *Rateless transmission.* Rateless and rate-compatible families let a receiver stop when a validity test passes [recalled for Luby 2002, Shokrollahi 2006 and Perry et al. 2012, not read; P for Erez, Trott and Wornell 2012, abstract, whose construction includes repetition with time-varying weights]. Without feedback the transmitter cannot stop early, so on a one-way link "rateless" means only that a receiver with spare margin may finish early inside a fixed calendar. Repeated keyed looks already have that property.
- *Change detection.* Detecting that a stream has stopped is a quickest-detection problem; delay grows as the log of the false-alarm interval over the divergence per sample [recalled from Lorden 1971 and Lai 1998; metadata verified only]. In a block calendar the decision is taken per slot, so the fixed-sample test below applies.

**The test the bank already contains** [D, `sme` §8]. One slot carries one k-bit block over the energy-optimal number of branches. The slot is declared occupied when the largest codeword score exceeds a threshold. False continuation: a noise-only slot is declared occupied. False termination: an occupied slot is declared quiet.

| Block | Margin | False termination at false continuation 10⁻² | 10⁻³ | 10⁻⁴ | 10⁻⁶ | Equal-error point |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 bits, 18 branches | 0 dB | 5.3·10⁻³ | 1.2·10⁻² | 2.2·10⁻² | 6.2·10⁻² | 6.3·10⁻³ |
| 10 bits | 3 dB | 6.1·10⁻⁶ | 1.8·10⁻⁵ | 4.7·10⁻⁵ | 2.2·10⁻⁴ | 5.8·10⁻⁵ |
| 10 bits | 6 dB | 6.6·10⁻¹⁰ | 2.4·10⁻⁹ | 7.4·10⁻⁹ | 4.9·10⁻⁸ | 1.1·10⁻⁷ |
| 25 bits, 24 branches | 0 dB | 3.1·10⁻³ | 5.6·10⁻³ | 9.4·10⁻³ | 2.3·10⁻² | 4.0·10⁻³ |
| 25 bits | 3 dB | 4.3·10⁻⁷ | 1.0·10⁻⁶ | 2.2·10⁻⁶ | 8.6·10⁻⁶ | 5.4·10⁻⁶ |
| Known keyed pattern, 18 branches, same energy as the 10-bit block | 0 dB | 9.6·10⁻⁵ | 5.5·10⁻⁴ | 1.9·10⁻³ | 1.2·10⁻² | 6.9·10⁻⁴ |

- **At the no-margin point, detection is weaker than decoding.** A 10-bit block that decodes with error 10⁻³ is distinguished from noise with about 6·10⁻³ at the equal-error point, because the threshold must clear the largest of 1024 noise scores. About 1–1.5 dB more energy equalises the two [D, interpolated].
- **Two quiet slots.** If fades are independent between slots, requiring two consecutive quiet slots squares the false-termination probability: `(1.2·10⁻²)² ≈ 1.4·10⁻⁴` per pair at a false-continuation setting of 10⁻³. A message of `N` blocks has `N − 1` chances, so 30 blocks need the 10⁻² setting (`29 × (5.3·10⁻³)² ≈ 8·10⁻⁴`) or some margin. With 3 dB of margin one slot is already at 10⁻⁵.
- **False continuation** appends a spurious block unless something rejects it. The threshold test is that rejection; an outer CRC over the message is the second line. The undetected-error and erasure trade of CRC size is studied for the tail-biting designs [P: King, Kwon et al. 2022, abstract].
- **An outage is indistinguishable from quiet.** A fadeout that covers two slots ends the message, however good the test. Remedies inside the contract [H]: let the number of quiet slots scale with the outage the configuration must survive; keep termination revocable, so that a schedule that resumes re-opens the message with the gap as erasures (the contract's "interior missing slots remain erasures"); and never let the quiet decision use fewer independent branches than the decode.

**Designs compatible with unknown length and fixed geometry.**

| Design | Length information | False-termination driver | False-continuation driver | Note |
| --- | --- | --- | --- | --- |
| Fixed block calendar, independent blocks (bank or tail-biting), end at scored quiet slots | None | Deep fade or outage of a whole slot | Largest noise score; rejected by threshold and CRC | Fits the contract directly; block granularity costs energy (3.4) |
| Same, with a streaming outer erasure code over blocks whose final parity blocks follow the data | None; the transmitter knows the end and sends the closing parity | Same | Same | Low-delay burst-erasure codes exist [Martinian and Sundberg 2004; title only, not read]; not analysed here |
| Repeated keyed looks of one block until a deadline ("rateless" in the receiver) | None | Not applicable inside a block | Receiver stops early only on a validity test | Margin becomes early completion |
| Convolutional code or interleaver running across blocks | None | Tail must be flushed; partly filled slots blur the quiet test | Same | Conflicts with clean quiet slots |
| Candidate-length CRC checks (blind format detection) | In content | n/a | `2^(−n)` per candidate | Not allowed to select geometry; usable as an integrity check only |

## 7. Recommended architecture and alternatives (Question 6)

What is common to every option, and is the main recommendation [H, synthesis]:

1. **A fixed block calendar.** One configured block size and shape per format. A message is a run of occupied slots ended by scored quiet slots.
2. **Keyed pseudo-random placement** of every look of every code symbol inside its slot, in time and across the hop set, with a spread constraint, redrawn per block.
3. **Robust cell metric before accumulation:** divide by the slot's own median, clip at a level tied to the expected signal cell, down-weight slots whose median exceeds the message's quiet floor.
4. **Square-law accumulation over looks** into a fixed `[n × q]` metric matrix per block. The dwell is set as long as the path allows; looks carry the rest of the rate reduction; the code rate stays near 0.2–0.35 of `log2 q`.
5. **Design point with branches near 0 to +3 dB**, accepting 0.4–2.2 dB of sensitivity (section 4) for erasure tolerance near the ideal law.
6. **Quiet test from the decoder's own best score**, two slots, revocable.

The decoder then differs by block size.

**A. Keyed bank, exhaustive decoding (recommended for blocks of 10–24 bits).** A keyed random or product-structured q-ary codebook; all `2^b` scores from one matrix product; the best score decides the block and, against a threshold, its presence. With 64 tones its loss against the reference grows from 0.2 dB at 10 bits to about 0.7 dB at 20; more tones reduce it.

**B. Nonbinary code over the tone alphabet (recommended for blocks of about 80–300 bits).** A Q65-like QRA or nonbinary LDPC code over GF(64) or larger at rate 0.2–0.35, transform-domain belief propagation with a fixed iteration count, a CRC inside the block, and a short list or ordered-statistics pass seeded by the bank machinery for the least reliable symbols.

**C. CRC-aided tail-biting convolutional code over GF(q) (alternative to B).** Dual-k or Ryan–Wilson class code, list Viterbi decoding with a fixed maximum list and a bound on the accepted metric.

| Criterion | A: bank blocks | B: nonbinary code | C: tail-biting with list |
| --- | --- | --- | --- |
| (i) Gap to the Rayleigh orthogonal reference, ε = 10⁻³, at 10 / 20 / 50 / 100 / 300 bits | 0.2 [D] / 0.7 [D, at 10⁻²; 0.3 with 256 tones] / 2.4 / 3.7 / 5.5 dB (two, four, twelve 25-bit blocks, [D] + [H]) | not suited below about 50 bits / 1.5–2.5 / 1.5–2 / 1–1.5 dB [H] | 0.3–1 / 0.5–1 / 1.5–2 / 1.5–2 / 2–2.5 dB [H] |
| (ii) Drastic failure modes | None in decoding: maximum likelihood, no floor, no convergence threshold, scale-free. Granularity loss is uniform | Abrupt threshold; needs metric scaling or an SNR-free metric; undetected errors need the CRC; no floor reported to 10⁻⁹ on binary-input AWGN [P] | None in decoding with a list cap; list depth varies with SNR and must be bounded |
| Sensitivity to a wrong noise estimate | None for the decision; the clip level and the quiet threshold use the slot median | 0.6–1.5 dB published for SNR-free metrics | None for the decision |
| Sensitivity to bursts and erasures | Zero rows; tolerance set by branches (5.5) | Uniform vectors; rate 0.2–0.35 tolerates large symbol erasure fractions | Zero metrics; long erasure runs exceed the constraint length unless interleaved |
| Sync dependence | Timing and frequency hypotheses multiply the product's other dimension | Same front end; each hypothesis needs a decode or a pre-screen | Same |
| (iii) Tensor Core shape | Best: one large fixed product per block and hypothesis batch; padding `q` (or `q1` structured) | Good: batched `q × q` Hadamard products and elementwise work in FP16/FP32 | Poor: max-plus recursion; CUDA cores or CPU SIMD |
| Fast batch simulation | Best | Good | Good on CPU |
| (iv) Implementation simplicity | Simplest: a codebook generator and a product | Moderate: code design, schedule, metric scaling, CRC | Simple to moderate: trellis, list, CRC design |

**Ranking.** For 10–24 bits: A, then C, with B not applicable. For 50 bits: A as two blocks and B or C as one block are within about 1 dB of each other on these estimates, so A wins on robustness and shape. For 100–300 bits: B, then C, with A 2–4 dB behind. If belief propagation's threshold behaviour is judged a drastic failure mode under the project's preference, C replaces B at a cost of perhaps 0.5–1 dB and a decoder that does not use Tensor Cores. Sequential decoding, binary codes on bit metrics, and sectioning with hard-decision outer codes are not recommended.

**Published against inferred.** Published: the bounds; the value of M-ary codes with diversity under interference; the behaviour of each decoder family on binary-input AWGN; one noncoherent result for tail-biting codes (4-FSK, AWGN); Q65 and the other fielded thresholds. Derived here: every energy, sectioning, combining, margin and quiet-test table, the bank and metric Monte Carlo. Inferred: all gap entries marked [H], the product-structured bank, the matrix-product reading of ordered-statistics re-encoding, the floor-weighted metric, and the ranking itself.

## 8. Contradictions and corrections to earlier documents

1. **Union-bound illustration in the [format guidance §5a](../../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md).** It gives `E/N0` of 15.2, 27.7 and 152 for 1, 10 and 100 bits (AWGN, noncoherent, ε = 10⁻³). Exact values are 12.43, 24.93 and 110.94 [D]. Relative airtime is 1 : 2.0 : 8.9, not 1 : 1.8 : 10. The 100-bit figure was 1.4 dB too high because the union-bound form holds only up to `k = log2(1/ε)`. "One bit costs about 20× (13 dB) more per bit than `ln 2`" becomes 17.9× (12.5 dB) on AWGN and 47× (16.7 dB) on Rayleigh.
2. **"Kennedy's optimum of about 3 suggests about `E/(3·N0)` branches"** (same section). At ε = 10⁻³ the optimum branch SNR is 3.7 at 10 bits and 6.5 at 100 bits, so `E/3` overstates the count by a factor of two at 100 bits. The optimum is broad, so following the old rule costs under 0.5 dB; the error that matters is on the other side, too few branches.
3. **WSPR threshold in the [GEMM decoding review §2.8](../../Opus-5.5/reviews/review-gemm-decoding.md).** It used −28 dB from a secondary source and derived 9.4 dB total `Eb/N0`. The WSJT-X 2.7.0 guide gives −31 dB, which is 6.4 dB total and 3.4 dB in data energy [P, D]. Sequential decoding with K = 32 is then about as good in data energy as the LDPC modes of the same guide; its real problems are the cutoff-rate ceiling on fading paths and unbounded computation.
4. **"Codeword-level correlation recovers the 3.6–4.5 dB per-symbol noncoherent penalty"** (GEMM decoding review §10). True on phase-stable paths, as that review says. On interleaved Rayleigh fading there is no such penalty to recover, because the symbol energies are sufficient statistics. The bank's value there is exact joint decoding of the code, not coherence.
5. **"Tail-biting convolutional codes are among the best below about 100 bits"** (format guidance §5a). Supported for binary-input coherent AWGN. For noncoherent q-ary signalling the only published point found is 4-FSK on AWGN, within 0.59 dB of that channel's own bound; the alphabet's floor (4.2 dB for 4-FSK at rate 1/2 against 0.45 dB for unconstrained orthogonal signalling at 100 bits) is the larger term.
6. **Finite-message heuristic** ([finite_message.md](../../../../_appraisal/algorithms-matrixModem/coherence-limits/results/finite_message.md)). For 100 bits at ε = 10⁻³ it implies about 4.5, 6.5 and 14.0 dB `Eb/N0` at `x` = +10, 0 and −10 dB (its penalty added to the fading-limit study's minimum for the Gaussian spectrum). The exact block-fading orthogonal values at the same branch SNR are 3.98, 5.92 and 13.65 dB [D]. The models differ, so this is a consistency check: the heuristic is conservative by 0.3–0.6 dB.
7. **"Interleave over at least the slowest fade"** (fading-limit study, implications). Necessary and not sufficient: the count of independent branches and the way the margin is spent decide the erasure tolerance (5.5), and a keyed aperiodic placement is required, not just depth.
8. **Agreement with the sibling [OTHR review](review-othr-interference.md).** Its conclusions that summed energy is the wrong metric, that bounded-influence metrics cost a few dB without side information, and that placement must be pseudo-random are confirmed here at decoder level. One difference of regime: its clip level of 2–3 times the noise mean is for weak looks; with branch SNR of +6 to +12 dB the clip must sit near the signal-cell level (8 times the noise in the Monte Carlo here).

## 9. Calculation record

Programs and outputs are in [`_appraisal/algorithms-matrixModem/short-message-energy/`](../../../../_appraisal/algorithms-matrixModem/short-message-energy/). They are theoretical calculations: standard library only, no repository sources, kept under the research-material rule. Both ran locally on the 12-thread Ryzen 5 PRO 5650U.

| Program | What it computes | Output | Runtime |
| --- | --- | --- | --- |
| `short_message_energy.cpp` | Exact message error of `2^k` orthogonal codewords: noncoherent and coherent AWGN, the converse, Rayleigh with `L` branches (optimised and fixed), ratios and marginal cost, sectioning, look-combining loss, capped branch SNR, margin against fixed and random erasures, the quiet-slot test, cutoff rates | `results/short_message_energy.md` | 7.5 min |
| `codebook_mc.cpp` | Monte Carlo: random and product-structured q-ary bank against the orthogonal reference; mutual information of repeated looks; bank decoding under slot and cell interference with seven blind metrics and two genie metrics | `results/codebook_mc.md` | 21.5 min |

```bash
cd _appraisal/algorithms-matrixModem/short-message-energy
g++ -O2 -std=c++17 -fopenmp short_message_energy.cpp -o /tmp/short_message_energy && /tmp/short_message_energy > results/short_message_energy.md
g++ -O3 -march=native -std=c++17 -fopenmp codebook_mc.cpp -o /tmp/codebook_mc && /tmp/codebook_mc > results/codebook_mc.md
```

Validation in the first output: the binary closed forms on AWGN and Rayleigh are reproduced to six digits; Table I of Yang, Durisi and Polyanskiy is reproduced (66.3, 238.6, 1319 against 67, 2.4·10², 1.3·10³); the 16-bit AWGN value matches the earlier [`calc/finite_energy`](../../../../_appraisal/algorithms-matrixModem/calc/results/finite_energy.md) (2.97 dB). The Monte Carlo uses static scheduling with one generator per thread, so its stored output repeats exactly on 12 threads and within the printed standard errors otherwise.

## 10. Sources

Verification: **[verified]** means the DOI resolved at Crossref to the stated authors and title, or the URL or arXiv identifier was fetched and its content matched. The words after it say how much was read. Abstracts of IEEE papers were read through OpenAlex.

**Bounds and theory**

- Y. Polyanskiy, H. V. Poor, S. Verdú, "Minimum energy to send k bits through the Gaussian channel with and without feedback," *IEEE Trans. Inf. Theory* 57(8):4880–4902, 2011. DOI [10.1109/TIT.2011.2159041](https://doi.org/10.1109/TIT.2011.2159041) [verified; abstract via [MIT DSpace](https://dspace.mit.edu/handle/1721.1/81369); full text not retrieved].
- W. Yang, G. Durisi, Y. Polyanskiy, "Minimum energy to send k bits over multiple-antenna fading channels," *IEEE Trans. Inf. Theory* 62(12):6831–6853, 2016. DOI [10.1109/TIT.2016.2615629](https://doi.org/10.1109/TIT.2016.2615629) [verified]; [arXiv:1507.03843](https://arxiv.org/abs/1507.03843) [verified; full text read].
- J. Östman, G. Durisi, E. G. Ström, M. C. Coşkun, G. Liva, "Short packets over block-memoryless fading channels: pilot-assisted or noncoherent transmission?" *IEEE Trans. Commun.* 67(2):1521–1536, 2019. DOI [10.1109/TCOMM.2018.2874993](https://doi.org/10.1109/TCOMM.2018.2874993) [verified]; [arXiv:1712.06387](https://arxiv.org/abs/1712.06387) [verified; abstract].
- S. Dolinar, D. Divsalar, F. Pollara, "Code performance as a function of block size," *TMO Progress Report* 42-133, JPL, 15 May 1998. [PDF](https://ipnpr.jpl.nasa.gov/progress_report/42-133/133K.pdf) [verified; full text read].
- A. Lancho, J. Östman, G. Durisi, T. Koch, G. Vazquez-Vilar, "Saddlepoint approximations for short-packet wireless communications," *IEEE Trans. Wireless Commun.* 19(7):4831–4846, 2020. DOI [10.1109/TWC.2020.2987573](https://doi.org/10.1109/TWC.2020.2987573) [verified; not read; listed as the tool for rate-constrained noncoherent block fading].
- J. Pierce, "Theoretical limitations on frequency and time diversity for fading binary transmissions," *IRE Trans. Commun. Syst.* 9(2):186–187, 1961. DOI [10.1109/TCOM.1961.1097668](https://doi.org/10.1109/TCOM.1961.1097668) [verified; abstract].
- J. Pierce, "Theoretical diversity improvement in frequency-shift keying," *Proc. IRE* 46(5):903–910, 1958. DOI [10.1109/JRPROC.1958.286944](https://doi.org/10.1109/JRPROC.1958.286944) [verified; not read].
- P. Hahn, "Theoretical diversity improvement in multiple frequency shift keying," *IRE Trans. Commun. Syst.* 10(2):177–184, 1962. DOI [10.1109/TCOM.1962.1088650](https://doi.org/10.1109/TCOM.1962.1088650) [verified; abstract].
- I. Jacobs, "The asymptotic behavior of incoherent M-ary communication systems," *Proc. IEEE* 51(1):251–252, 1963. DOI [10.1109/PROC.1963.1729](https://doi.org/10.1109/PROC.1963.1729) [verified; not read].
- R. S. Kennedy, *Fading Dispersive Communication Channels*, Wiley, 1969 [unverified; not accessed; existence corroborated by H. Rowe's review, *IEEE Trans. Inf. Theory* 18(1):219, 1972, DOI [10.1109/TIT.1972.1054726](https://doi.org/10.1109/TIT.1972.1054726), verified].
- A. Lapidoth, P. Narayan, "Reliable communication under channel uncertainty," *IEEE Trans. Inf. Theory* 44(6):2148–2177, 1998. DOI [10.1109/18.720535](https://doi.org/10.1109/18.720535) [verified; not re-read]. R. Ahlswede, "Elimination of correlation in random codes for arbitrarily varying channels," *Z. Wahrscheinlichkeitstheorie verw. Gebiete* 44(2):159–175, 1978. DOI [10.1007/BF00533053](https://doi.org/10.1007/BF00533053) [verified; not read].

**Codes and decoders**

- M. C. Coşkun, G. Durisi, T. Jerkovits, G. Liva, W. Ryan, B. Stein, F. Steiner, "Efficient error-correcting codes in the short blocklength regime," *Physical Communication* 34:66–79, 2019. DOI [10.1016/j.phycom.2019.03.004](https://doi.org/10.1016/j.phycom.2019.03.004) [verified]; [arXiv:1812.08562](https://arxiv.org/abs/1812.08562) [verified; text read through ar5iv; two "11 dB" values there are taken as typesetting errors for 1 dB].
- J. King, W. Ryan, R. D. Wesel, "CRC-aided short convolutional codes and RCU bounds for orthogonal signaling," *IEEE GLOBECOM* 2022, 4256–4261. DOI [10.1109/GLOBECOM48099.2022.10001459](https://doi.org/10.1109/GLOBECOM48099.2022.10001459) [verified]; [arXiv:2210.00026](https://arxiv.org/abs/2210.00026) [verified; text read through ar5iv].
- J. King, A. Kwon, H. Yang, W. Ryan, R. D. Wesel, "CRC-aided list decoding of convolutional and polar codes for short messages in 5G," *IEEE ICC* 2022, 92–97. DOI [10.1109/ICC45855.2022.9838726](https://doi.org/10.1109/ICC45855.2022.9838726) [verified; abstract].
- H. Yang, E. Liang, M. Pan, R. D. Wesel, "CRC-aided list decoding of convolutional codes in the short blocklength regime," *IEEE Trans. Inf. Theory* 68(6):3744–3766, 2022. DOI [10.1109/TIT.2022.3150717](https://doi.org/10.1109/TIT.2022.3150717) [verified; not re-read].
- R. D. Wesel, A. Antonini, L. Wang, W. Sui, B. Towell, H. Grissett, "ELF codes: concatenated codes with an expurgating linear function as the outer code," *ISTC* 2023. DOI [10.1109/ISTC57237.2023.10273535](https://doi.org/10.1109/ISTC57237.2023.10273535) [verified; abstract].
- Z. Qu, W. Sui, G. Liva, D. Divsalar, R. D. Wesel, "Bounded-metric list decoding of linearly expurgated tail-biting convolutional codes," *ISTC* 2025. DOI [10.1109/ISTC65386.2025.11154526](https://doi.org/10.1109/ISTC65386.2025.11154526) [verified; abstract].
- W. Sui, B. Towell, A. Asmani, H. Yang, H. Grissett, R. D. Wesel, "CRC-aided high-rate convolutional codes with short blocklengths for list decoding," *IEEE Trans. Commun.* 72(1):63–74, 2024. DOI [10.1109/TCOMM.2023.3324370](https://doi.org/10.1109/TCOMM.2023.3324370) [verified; abstract; background only].
- A. J. Viterbi, "Orthogonal tree codes for communication in the presence of white Gaussian noise," *IEEE Trans. Commun. Technol.* 15(2):238–242, 1967. DOI [10.1109/TCOM.1967.1089561](https://doi.org/10.1109/TCOM.1967.1089561) [verified; abstract].
- A. J. Viterbi, "Very low rate convolutional codes for maximum theoretical performance of spread-spectrum multiple-access channels," *IEEE J. Sel. Areas Commun.* 8(4):641–649, 1990. DOI [10.1109/49.54460](https://doi.org/10.1109/49.54460) [verified; abstract].
- W. E. Ryan, S. G. Wilson, "Two classes of convolutional codes over GF(q) for q-ary orthogonal signaling," *IEEE Trans. Commun.* 39(1):30–40, 1991. DOI [10.1109/26.68274](https://doi.org/10.1109/26.68274) [verified; abstract].
- J. P. Odenwalder, "Dual-k convolutional codes for noncoherently demodulated channels," *Proc. Int. Telemetering Conf.*, 1976 [unverified; not accessed; known here through Ryan and Wilson and through Ma and Poole]. B. D. Trumpis, *Convolutional coding for M-ary channels*, PhD thesis, UCLA, 1975 [unverified; not accessed; same route].
- K. Jordan, "The performance of sequential decoding in conjunction with efficient modulation," *IEEE Trans. Commun. Technol.* 14(3):283–297, 1966. DOI [10.1109/TCOM.1966.1089324](https://doi.org/10.1109/TCOM.1966.1089324) [verified; abstract].
- I. Jacobs, E. Berlekamp, "A lower bound to the distribution of computation for sequential decoding," *IEEE Trans. Inf. Theory* 13(2):167–174, 1967. DOI [10.1109/TIT.1967.1053977](https://doi.org/10.1109/TIT.1967.1053977) [verified; not read].
- J. Layland, W. Lushbaugh, "A flexible high-speed sequential decoder for deep space channels," *IEEE Trans. Commun. Technol.* 19(5):813–820, 1971. DOI [10.1109/TCOM.1971.1090732](https://doi.org/10.1109/TCOM.1971.1090732) [verified; not read; cited as background for the K = 32 codes].
- J. H. Taylor, *WSJT-X User Guide*, version 2.7.0. [PDF](https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.pdf) [verified; section 18 and Table 7 read].
- D. Declercq, M. Fossorier, "Decoding algorithms for nonbinary LDPC codes over GF(q)," *IEEE Trans. Commun.* 55(4):633–643, 2007. DOI [10.1109/TCOMM.2007.894088](https://doi.org/10.1109/TCOMM.2007.894088) [verified; abstract].
- M. C. Davey, D. MacKay, "Low-density parity check codes over GF(q)," *IEEE Commun. Lett.* 2(6):165–167, 1998. DOI [10.1109/4234.681360](https://doi.org/10.1109/4234.681360) [verified; not read].
- K. Saied, A. C. Al Ghouwayel, E. Boutillon, "Short frame transmission at very low SNR by associating CCSK modulation with NB-code," *IEEE Trans. Wireless Commun.* 21(9):7194–7206, 2022. DOI [10.1109/TWC.2022.3156628](https://doi.org/10.1109/TWC.2022.3156628) [verified; abstract].
- V. Savin, "Non-binary polar codes for spread-spectrum modulations," *ISTC* 2021. DOI [10.1109/ISTC49272.2021.9594166](https://doi.org/10.1109/ISTC49272.2021.9594166) [verified; abstract].
- A. Guillén i Fàbregas, A. J. Grant, "Capacity approaching codes for non-coherent orthogonal modulation," *IEEE Trans. Wireless Commun.* 6(11):4004–4013, 2007. DOI [10.1109/TWC.2007.06040154](https://doi.org/10.1109/TWC.2007.06040154) [verified; abstract].
- Y. Kim, K. Cheun, K. Yang, M. Sagong, "Design of turbo codes over GF(q) with q-ary orthogonal modulation," *IEEE Trans. Commun.* 59(3):625–631, 2011. DOI [10.1109/TCOMM.2011.012711.090402](https://doi.org/10.1109/TCOMM.2011.012711.090402) [verified; abstract].
- R. Koetter, A. Vardy, "Algebraic soft-decision decoding of Reed–Solomon codes," *IEEE Trans. Inf. Theory* 49(11):2809–2825, 2003. DOI [10.1109/TIT.2003.819332](https://doi.org/10.1109/TIT.2003.819332) [verified; abstract].
- Y. Be'ery, J. Snyders, "Optimal soft decision block decoders based on fast Hadamard transform," *IEEE Trans. Inf. Theory* 32(3):355–364, 1986. DOI [10.1109/TIT.1986.1057189](https://doi.org/10.1109/TIT.1986.1057189) [verified; abstract].
- S. M. Abbas, M. Jalaleddine, W. J. Gross, "GRAND for Rayleigh fading channels," *IEEE Globecom Workshops* 2022, 504–509. DOI [10.1109/GCWkshps56602.2022.10008698](https://doi.org/10.1109/GCWkshps56602.2022.10008698) [verified; abstract].
- F. Ercan, K. Galligan, D. Starobinski, M. Médard, K. R. Duffy, R. T. Yazicigil, "GRAND-EDGE: a universal, jamming-resilient algorithm with error-and-erasure decoding," *IEEE ICC* 2023, 4501–4507. DOI [10.1109/ICC45041.2023.10279273](https://doi.org/10.1109/ICC45041.2023.10279273) [verified; abstract].
- Li Ping, W. K. Leung, K. Y. Wu, "Low-rate turbo-Hadamard codes," *IEEE Trans. Inf. Theory* 49(12):3213–3224, 2003. DOI [10.1109/TIT.2003.820018](https://doi.org/10.1109/TIT.2003.820018) [verified; abstract].
- P. Frenger, S. Parkvall, E. Dahlman, "Performance comparison of HARQ with Chase combining and incremental redundancy for HSDPA," *IEEE VTC Fall* 2001, 1829–1833. DOI [10.1109/VTC.2001.956516](https://doi.org/10.1109/VTC.2001.956516) [verified; abstract].
- J.-F. Cheng, "Coding performance of hybrid ARQ schemes," *IEEE Trans. Commun.* 54(6):1017–1029, 2006. DOI [10.1109/TCOMM.2006.876846](https://doi.org/10.1109/TCOMM.2006.876846) [verified; abstract].
- MathWorks, "Introduction to pulse integration and fluctuation loss in radar," Radar Toolbox documentation. [Page](https://www.mathworks.com/help/radar/ug/introduction-to-integration-and-fluctuation-losses-in-radar.html) [verified; read]. It cites D. K. Barton, *Radar Equations for Modern Radar*, Artech House, 2013, and M. A. Richards, "Noncoherent integration gain, and its approximation," Georgia Tech memo, 2010 [both unverified; not accessed].

**Interference and anti-jam**

- A. J. Viterbi, I. M. Jacobs, "Advances in coding and modulation for noncoherent channels affected by fading, partial band, and multiple-access interference," in *Advances in Communication Systems*, vol. 4, Academic Press, 1975, 279–308. DOI [10.1016/B978-0-12-010904-3.50013-0](https://doi.org/10.1016/B978-0-12-010904-3.50013-0) [verified; not read; content through Levitt].
- B. K. Levitt, "FH/MFSK performance in multitone jamming," JPL Publication 84-90, 15 January 1985. [NTRS PDF](https://ntrs.nasa.gov/api/citations/19850015886/downloads/19850015886.pdf) [verified; read]. Journal version: *IEEE J. Sel. Areas Commun.* 3(5):627–643, 1985. DOI [10.1109/JSAC.1985.1146253](https://doi.org/10.1109/JSAC.1985.1146253) [verified; abstract].
- W. E. Stark, "Coding for frequency-hopped spread-spectrum communication with partial-band interference — Part I: capacity and cutoff rate; Part II: coded performance," *IEEE Trans. Commun.* 33(10):1036–1044 and 1045–1057, 1985. DOIs [10.1109/TCOM.1985.1096223](https://doi.org/10.1109/TCOM.1985.1096223), [10.1109/TCOM.1985.1096224](https://doi.org/10.1109/TCOM.1985.1096224) [verified; abstracts].
- H. Ma, M. Poole, "Error-correcting codes against the worst-case partial-band jammer," *IEEE Trans. Commun.* 32(2):124–133, 1984. DOI [10.1109/TCOM.1984.1096036](https://doi.org/10.1109/TCOM.1984.1096036) [verified; abstract].
- M. B. Pursley, W. E. Stark, "Performance of Reed–Solomon coded frequency-hop spread-spectrum communications in partial-band interference," *IEEE Trans. Commun.* 33(8):767–774, 1985. DOI [10.1109/TCOM.1985.1096386](https://doi.org/10.1109/TCOM.1985.1096386) [verified; abstract].
- J. S. Lee, R. French, L. Miller, "Probability of error analyses of a BFSK frequency-hopping system with diversity under partial-band jamming interference — Part I," *IEEE Trans. Commun.* 32(6):645–653, 1984. DOI [10.1109/TCOM.1984.1096128](https://doi.org/10.1109/TCOM.1984.1096128). Part II (J. S. Lee, L. Miller, Y. Kim), 32(12):1243–1250, 1984. DOI [10.1109/TCOM.1984.1096018](https://doi.org/10.1109/TCOM.1984.1096018). Part III (L. Miller, J. S. Lee, A. Kadrichu), 34(7):669–675, 1986. DOI [10.1109/TCOM.1986.1096609](https://doi.org/10.1109/TCOM.1986.1096609) [all verified; abstracts].
- J. S. Lee, R. French, L. Miller, "Error-correcting codes and nonlinear diversity combining against worst case partial-band noise jamming of frequency-hopping MFSK systems," *IEEE Trans. Commun.* 36(4):471–478, 1988. DOI [10.1109/26.2772](https://doi.org/10.1109/26.2772) [verified; abstract].
- A. J. Viterbi, "A robust ratio-threshold technique to mitigate tone and partial band jamming in coded MFSK systems," *IEEE MILCOM* 1982, 22.4-1–22.4-5. DOI [10.1109/MILCOM.1982.4805943](https://doi.org/10.1109/MILCOM.1982.4805943) [verified; abstract].
- C. Keller, M. B. Pursley, "Clipped diversity combining for channels with partial-band interference — Part I: clipped-linear combining," *IEEE Trans. Commun.* 35(12):1320–1328, 1987. DOI [10.1109/TCOM.1987.1096711](https://doi.org/10.1109/TCOM.1987.1096711). "Part II: ratio-statistic combining," 37(2):145–151, 1989. DOI [10.1109/26.20082](https://doi.org/10.1109/26.20082) [both verified; abstracts].
- R. Viswanathan, K. Taghizadeh, "Diversity combining in FH/BFSK systems to combat partial band jamming," *IEEE Trans. Commun.* 36(9):1062–1069, 1988. DOI [10.1109/26.7518](https://doi.org/10.1109/26.7518) [verified; abstract].
- K. C. Teh, A. C. Kot, K. H. Li, "Performance analysis of an FFH/BFSK product-combining receiver under multitone jamming," *IEEE Trans. Veh. Technol.* 48(6):1946–1953, 1999. DOI [10.1109/25.806787](https://doi.org/10.1109/25.806787) [verified; abstract].
- T. A. Gulliver, "Order statistics diversity combining in worst case noise and multitone jamming," *IEEE MILCOM* 1991, 385–389. DOI [10.1109/MILCOM.1991.258279](https://doi.org/10.1109/MILCOM.1991.258279) [verified; abstract].
- R. C. Robertson, T. T. Ha, "Error probabilities of fast frequency-hopped MFSK with noise-normalization combining in a fading channel with partial-band interference," *IEEE Trans. Commun.* 40(2):404–412, 1992. DOI [10.1109/26.129202](https://doi.org/10.1109/26.129202) [verified; abstract].
- C. W. Baum, M. B. Pursley, "Bayesian methods for erasure insertion in frequency-hop communication systems with partial-band interference," *IEEE Trans. Commun.* 40(7):1231–1238, 1992. DOI [10.1109/26.153368](https://doi.org/10.1109/26.153368). "Erasure insertion in frequency-hop communications with fading and partial-band interference," *IEEE Trans. Veh. Technol.* 46(4):949–956, 1997. DOI [10.1109/25.653069](https://doi.org/10.1109/25.653069) [both verified; abstracts].
- J. H. Kang, W. E. Stark, "Turbo codes for noncoherent FH-SS with partial band interference," *IEEE Trans. Commun.* 46(11):1451–1458, 1998. DOI [10.1109/26.729389](https://doi.org/10.1109/26.729389) [verified; abstract].
- H. El Gamal, E. Geraniotis, "Iterative channel estimation and decoding for convolutionally coded anti-jam FH signals," *IEEE Trans. Commun.* 50(2):321–331, 2002. DOI [10.1109/26.983327](https://doi.org/10.1109/26.983327) [verified; abstract].
- S. Cheng, M. C. Valenti, D. Torrieri, "Robust iterative noncoherent reception of coded FSK over block fading channels," *IEEE Trans. Wireless Commun.* 6(9):3142–3147, 2007. DOI [10.1109/TWC.2007.06020004](https://doi.org/10.1109/TWC.2007.06020004) [verified; abstract].
- D. Torrieri, S. Cheng, M. C. Valenti, "Robust frequency hopping for interference and fading channels," *IEEE Trans. Commun.* 56(8):1343–1351, 2008. DOI [10.1109/TCOMM.2008.060549](https://doi.org/10.1109/TCOMM.2008.060549) [verified; abstract].
- IARU Region 1 Monitoring System, "Radars" wiki page. [Page](https://www.iaru-r1.org/spectrum/monitoring-system/iarums-wiki/radars/) [verified; read; volunteer observations].
- M. K. Simon, J. K. Omura, R. A. Scholtz, B. K. Levitt, *Spread Spectrum Communications Handbook*; S. W. Houston, "Modulation techniques for communication, Part 1," NAECON 1975 [both unverified; not accessed; known through Levitt 1985].

**Interleavers**

- J. Ramsey, "Realization of optimum interleavers," *IEEE Trans. Inf. Theory* 16(3):338–345, 1970. DOI [10.1109/TIT.1970.1054443](https://doi.org/10.1109/TIT.1970.1054443) [verified; abstract].
- G. D. Forney, "Burst-correcting codes for the classic bursty channel," *IEEE Trans. Commun. Technol.* 19(5):772–781, 1971. DOI [10.1109/TCOM.1971.1090719](https://doi.org/10.1109/TCOM.1971.1090719) [verified; abstract].
- D. Divsalar, F. Pollara, "Multiple turbo codes for deep-space communications," *TDA Progress Report* 42-121, JPL, 15 May 1995. [NTRS PDF](https://ntrs.nasa.gov/api/citations/19950025806/downloads/19950025806.pdf) [verified; S-random passage read].

**Detection, termination and rateless transmission**

- N. Weinberger, N. Merhav, "Codeword or noise? Exact random coding exponents for joint detection and decoding," *IEEE Trans. Inf. Theory* 60(9):5077–5094, 2014. DOI [10.1109/TIT.2014.2335733](https://doi.org/10.1109/TIT.2014.2335733) [verified; abstract].
- A. Lancho, J. Östman, G. Durisi, "On joint detection and decoding in short-packet communications," *IEEE GLOBECOM* 2021. DOI [10.1109/GLOBECOM46510.2021.9685448](https://doi.org/10.1109/GLOBECOM46510.2021.9685448) [verified; abstract].
- G. D. Forney, "Exponential error bounds for erasure, list, and decision feedback schemes," *IEEE Trans. Inf. Theory* 14(2):206–220, 1968. DOI [10.1109/TIT.1968.1054129](https://doi.org/10.1109/TIT.1968.1054129) [verified; abstract].
- V. Chandar, A. Tchamkerten, D. Tse, "Asynchronous capacity per unit cost," *IEEE Trans. Inf. Theory* 59(3):1213–1226, 2013. DOI [10.1109/TIT.2012.2236914](https://doi.org/10.1109/TIT.2012.2236914) [verified; abstract].
- Y. Okumura, F. Adachi, "Variable rate data transmission with blind rate detection for coherent DS-CDMA mobile radio," *Electronics Letters* 32(20):1865–1866, 1996. DOI [10.1049/el:19961238](https://doi.org/10.1049/el:19961238) [verified; abstract].
- 3GPP TS 25.212, Annex A, blind transport format detection [unverified; search summary only].
- M. Luby, "LT codes," *IEEE FOCS* 2002, 271–280. DOI [10.1109/SFCS.2002.1181950](https://doi.org/10.1109/SFCS.2002.1181950) [verified; not read]. A. Shokrollahi, "Raptor codes," *IEEE Trans. Inf. Theory* 52(6):2551–2567, 2006. DOI [10.1109/TIT.2006.874390](https://doi.org/10.1109/TIT.2006.874390) [verified; not read]. U. Erez, M. D. Trott, G. W. Wornell, "Rateless coding for Gaussian channels," *IEEE Trans. Inf. Theory* 58(2):530–547, 2012. DOI [10.1109/TIT.2011.2173242](https://doi.org/10.1109/TIT.2011.2173242) [verified; abstract]. J. Perry, P. A. Iannucci, K. E. Fleming, H. Balakrishnan, D. Shah, "Spinal codes," *ACM SIGCOMM* 2012, 49–60. DOI [10.1145/2342356.2342363](https://doi.org/10.1145/2342356.2342363) [verified; not read].
- G. Lorden, "Procedures for reacting to a change in distribution," *Ann. Math. Statist.* 42(6):1897–1908, 1971. DOI [10.1214/aoms/1177693055](https://doi.org/10.1214/aoms/1177693055) [verified; not read]. T. L. Lai, "Information bounds and quick detection of parameter changes in stochastic systems," *IEEE Trans. Inf. Theory* 44(7):2917–2929, 1998. DOI [10.1109/18.737522](https://doi.org/10.1109/18.737522) [verified; not read].
- E. Martinian, C.-E. W. Sundberg, "Burst erasure correction codes with low decoding delay," *IEEE Trans. Inf. Theory* 50(10):2494–2502, 2004. DOI [10.1109/TIT.2004.834844](https://doi.org/10.1109/TIT.2004.834844) [verified; not read].

**Sources sought and not accessed**

- Polyanskiy, Poor and Verdú 2011, full text (repository returned HTTP 429).
- N. Palermo, "Q-ary repeat-accumulate codes for weak signals communications," 2016 (the address given in the Q65 guide returned HTTP 404; the web archive could not be fetched).
- Odenwalder 1976, Trumpis 1975, Houston 1975, the Simon–Omura–Scholtz–Levitt handbook, Kennedy 1969, Proakis' textbook, Barton 2013 and Richards 2010.
- Full texts of the IEEE papers listed with "abstract"; in particular Stark 1985 (no numbers for soft decisions without side information), Ryan and Wilson 1991 (no Rayleigh numbers) and Keller and Pursley (no curves).
- 3GPP TS 25.212 Annex A and an Odenwalder coding handbook on DTIC (HTTP 403 or 404).

## 11. Design consequences

Written as statements a simulator or a bench test can pass or fail. "Reference" means the exact values of section 2.

1. **Bank sensitivity.** A 10-bit block with 64 tones and 18 or more independently fading symbol slots, decoded exhaustively with ideal energies, meets ε = 10⁻³ within 0.3 dB of `E/N0` = 66.3 (18.2 dB). With 16 tones the allowance is 0.9 dB. A 20-bit block with 64 tones and 15 slots meets ε = 10⁻² within 0.9 dB of 69.7 (18.4 dB), and within 0.4 dB with 256 tones.
2. **Premise.** Measured energy for a 10-bit block is between 1.8 and 2.2 times that for a 1-bit decision between two keyed patterns at ε = 10⁻³ under the same channel model.
3. **Diversity floor.** With independent branches reduced to 4, the 10-bit requirement rises by 2.8 ± 0.5 dB; with 2, by 8 ± 1 dB. A configuration that can leave a block with fewer than 8 independent branches on a 0.1 Hz frequency-flat path is rejected or flagged.
4. **No decoding by received length.** Block shape, slot count and accumulator shapes are identical for every message length; only the number of occupied slots differs.
5. **Rate reduction.** Doubling the number of looks at a per-look SNR of 0 dB or less while halving the code rate below 0.25 of `log2 q` changes the required energy by less than 0.3 dB; doing the same at a combined-symbol information above 0.5 of `log2 q` shows a repetition loss of at least 0.5 dB.
6. **Combining loss.** At fixed total energy, replacing one look by 64 equal looks costs 4.6 ± 0.3 dB for a 10-bit block on a non-fading channel.
7. **Metric.** With 1% of cells raised by 20 dB at random, the summed-energy metric fails (error above 0.5 for the 6 dB design of 5.2) and the normalised, clipped metric shows no error in 10⁵ trials. Adding looks never increases the error rate with the robust metric.
8. **Slot hits.** With 50% of slots raised by 20 dB or 40 dB, the 72-slot design with 6 dB of margin and the normalised, clipped, floor-weighted metric stays below ε = 10⁻³.
9. **Margin table.** For a design with branches at 0 dB, random flagged erasures of 0.57, 0.77, 0.87, 0.90, 0.91 and 0.92 of the looks are survived at ε = 10⁻³ with margins of 3, 6, 10, 13, 16 and 20 dB. For the energy-optimal 18-branch design the corresponding values are 0.36, 0.52, 0.62, 0.65, 0.67, 0.68.
10. **Two resources.** A format that spends a 10 dB margin on ten times the branches tolerates 90% flagged erasures and only 7.7 ± 0.3 dB of extra path loss.
11. **Interleaver.** For interferer periods of 20 ms, 1 s, 20 s, 90 s, 240 s, 1 h and 24 h at every phase, the worst fraction of any one code symbol's looks that is hit stays within the binomial 99.9% point of the mean hit fraction. No two looks of one code symbol are closer in time than the configured spread.
12. **Quiet test.** At the no-margin point a 10-bit block gives false termination of (1.2 ± 0.3)·10⁻² per slot at false continuation 10⁻³, and below 10⁻⁴ with 3 dB of margin. With the two-slot rule and independent fades the false-termination rate per message of 30 blocks is below 10⁻³ at the 10⁻² setting.
13. **Outage.** A signal outage of three slots in the middle of a message, followed by resumption, yields a message with three erased blocks and not a terminated one.
14. **Sectioning.** A 100-bit message sent as four independent 25-bit blocks needs 2.9 ± 0.4 dB more energy than the reference for 100 bits; any 100-bit block code proposed for the format must beat that by a stated amount at ε = 10⁻³ on the Rayleigh model before its decoder complexity is accepted.
15. **Product-structured bank.** Its loss against the unstructured bank is as measured in 3.4 at 10 and 16 bits and is to be measured at 20–24 bits, where it is expected to be smaller.
16. **Sequential decoding excluded.** No operating mode depends on a decoder whose work per block is unbounded.

## 12. Weaknesses and unknowns

- **Channel model.** Independent, equal-mean Rayleigh branches and block fading. Real HF branches are correlated in time and frequency, are sometimes Rician or two-ray, and change statistics over a long message. The number of independent branches actually available per block is the largest unknown and decides whether the results of 2.3 or the single-branch column apply.
- **Converse on fading.** The Rayleigh converse is read from a published figure to ±0.2 dB; it was not recomputed. The AWGN converse is evaluated here from the general form restated in the fading paper, and not checked against the text of the 2011 paper.
- **Gap estimates.** Every [H] entry in 3.3 and section 7 is assembled from results on other channels (binary-input AWGN, 4-FSK AWGN) and from capacity plus a finite-length allowance. No published frame-error curve against a finite-length bound was found for nonbinary LDPC, polar, or tail-biting codes with 64-tone noncoherent signalling in Rayleigh fading. These numbers could be wrong by a dB in either direction.
- **Bank Monte Carlo.** Ideal exponential cell energies and independent slots. The 16- and 20-bit rows are at ε = 10⁻² only, and the 20-bit rows have 10⁴ trials; 24–26 bits were not simulated, and the 0.8 dB allowance used for 25-bit blocks is an extrapolation. The product-structured bank is an untested idea beyond the two rows reported.
- **Metric Monte Carlo.** One code size, two designs, Gaussian interference that raises noise in cells or slots, one clip level, known timing. Real interference has structure (combs, sweeps) and the sibling review shows that at the modem's native resolution a radar is not sparse; the erasure model applies after excision or to bursts.
- **Margin tables** assume flagged erasures and a margin that scales every surviving branch. The combination of fading deciles, noise deciles, interference and receiver losses in one margin is not modelled.
- **Quiet test.** Independent fades between slots are assumed for the two-slot rule; slow fades violate it. Outage statistics decide the false-termination rate in practice and are not in the model.
- **Acquisition.** Timing, frequency, drift and spread hypotheses are outside every number here. They multiply the decoder's batch dimension and raise both thresholds.
- **Peak-limited weak regime.** When `(C/N0)·Tc` is below about 0 dB the design is in the quadratic regime whatever the code; the code-level statements about optimum branch SNR do not apply and only the look-combining tables do.
- **Precision.** Statements about INT4/INT8/FP16 suitability are inferences; no quantised decoder was simulated.
- **Sources not read in full.** Most IEEE papers were available as abstracts only. Classical numbers for coded systems without side information (Stark 1985) were not obtained. The dual-k and Trumpis sources, the spread-spectrum handbook, Kennedy's book, the QRA code paper and the 3GPP annex were not accessed.

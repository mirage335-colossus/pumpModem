# GEMM-shaped coding, decoding and joint detection for Matrix Modem: what extra compute buys

Literature review, 2026-11-24. Scope: algorithms whose dominant cost can be written as large dense or batched matrix products, judged by the dB they deliver near finite-blocklength limits at very low SNR with unknown phase, frequency and timing, and with fading. This extends `Sol-6.2/waveform-options.md` and `Sol-6.2/receiver-architecture.md`. Those chapters cover MFSK, tone-bank GEMM layout, segment coherence loss and null calibration, which are not repeated here. None of the numbers below is a Matrix Modem measurement.

Evidence labels used throughout:

- **[P]** is a published value from the cited source.
- **[D]** is derived here from a published threshold by a stated unit conversion.
- **[C]** is computed here from a standard closed-form or Monte Carlo expression. The review's scratch scripts (`gemm_orth.py`, `misc.py`, `div2.py`, `search_cost.py`, `mfsk_mi.py`) were temporary and are not retained; the orthogonal-signaling and MFSK values are independently reproduced by `_appraisal/algorithms-matrixModem/calc/`. The checks against analytic M=2 cases and against Yang–Durisi–Polyanskiy's published Table I agree (see §1).

## 0. Findings in brief

1. **The bound depends on the channel model more than on the decoder.** For k = 100 bits at ε = 10⁻³ the ideal values are:
   - Coherent AWGN: ≈0.30 dB Eb/N0 [C].
   - One unknown phase per message (noncoherent orthogonal ML): ≈0.45 dB [C].
   - Independent Rayleigh fading with no CSI: ≈3.8 dB, and only with ~32–38 independent diversity looks [P, C].
   - A single fade per message: ≈28 dB [C].

   No amount of decoder compute crosses these gaps. Only coherence (channel knowledge) or diversity can.
2. **Near-ML short-code decoders already exist, and compute buys their last dB.** At (128,64):
   - eBCH with OSD order 4 is within ≈0.1 dB of the normal approximation (NA).
   - A memory-14 tail-biting convolutional code is within ≈0.07 dB of the NA.
   - CRC-aided polar codes with list 32 come close to the RCU bound.

   [P: Coşkun et al.] Beyond near-ML, extra compute saturates by definition. OSD's test-pattern re-encoding and its metric evaluation are naturally batched GEMMs. Polar SCL, Viterbi and Fano decoding are not GEMM-shaped.
3. **In MFSK-style weak-signal modes the decoder is not the main loss.** Q65-60A and FT8 decode within ≈1.0–1.3 dB of the capacity of their own per-symbol noncoherent MFSK channel [D, C]. That channel itself sits ≈3.6–4.5 dB above codeword-level ideal detection at the same 50% success point [C]. Sync symbols then cost another 1.3–3.0 dB [D]. The remaining dB lie in **coherent multi-symbol / codeword-level correlation and in sync-free joint detection**, the parts that grow hypothesis matrices. Whether they are recoverable depends on channel coherence. FT8's 3-symbol block detection gains 0.7 dB on AWGN. On the 10 Hz channel block detection gives FT4 only ≈0.1 dB, and FT8's baseline (N=1, BP) decoder never reaches 50% there [P].
4. **A-priori ("deep search", AP) decoding buys large apparent dB by shrinking the message set, not by decoding the same information better.** Reported gains: Q65 1.5–3.6 dB, FT8 ≈2 dB on mild channels and ≈10 dB on the 10 Hz high-latitude channel, JT65 DS ≈4–5 dB [P]. These are honest gains only when the candidate list is genuinely known in advance. JT65 DS was limited by **synchronization failure** below about −28 dB, not by the decoder [P].
5. **Search hypotheses cost energy even with an ideal detector.** Treating extra timing/frequency hypotheses as additional orthogonal alternatives gives a pessimistic penalty at k = 100, ε = 10⁻³ [C]:
   - 2¹⁰ cells: +0.33 dB.
   - 2²⁰ cells: +0.63 dB.
   - 2⁴⁰ cells: +1.17 dB.

   Hypothesis counts grow like T (frequency), T² (drift) and T³ (jerk) with coherent time T, so brute-force search saturates against both compute and false alarm.
6. **SPARC/AMP and coded compressed sensing are matrix–vector algorithms.** Their published finite-length wins are at moderate SNR and rate, or in multi-user sum-rate regimes. Single-user low-SNR evidence is thin, and dense instances are memory-bound unless batched. A structured (Hadamard/DFT) dictionary is preferred.
7. **Low precision is not the bottleneck for decoding.** Quantization losses are much smaller than the losses in findings 1–5:
   - 4-bit LDPC/BP messages lose ≈0.1 dB [P].
   - ORBGRAND needs only ⌈log₂ n⌉ bits of rank reliability [P].
   - Quantized neural BP reaches ≈0.5 dB of ML with 5 bits [P].

   Raw-sample quantization and accumulators are covered by the existing int4 chapter.
8. **Neural decoders do not beat ML.** They suffer the 2^k "curse of dimensionality" for one-shot decoding. Published gains are relative to BP, at blocklengths ≤ ~128, and are not a route to sensitivity beyond OSD/list decoding.

## 1. Reference bounds at k ≈ 50–200 and low rate

**Finite-blocklength (bandwidth-limited).** The normal approximation and the RCU/meta-converse bounds of Polyanskiy–Poor–Verdú (2010) are the standard yardsticks for rate-constrained codes. At (128,64) the meta-converse and RCU bracket the minimum Eb/N0 at CER 10⁻⁶ to 3.5–3.7 dB [P: Coşkun et al. 2019].

**Minimum energy (unconstrained bandwidth, the "low-rate" limit).** Polyanskiy–Poor–Verdú (2011) show that without feedback, reaching −1.57 dB needs at least 10⁶ information bits [P]. M = 2^k orthogonal signalling with ML detection is an achievability scheme for this regime. Its exact error probability gives [C]:

| k (bits) | ε | Coherent orthogonal Eb/N0 | Noncoherent orthogonal (one unknown phase per message) |
|---:|---:|---:|---:|
| 16 | 10⁻³ | 2.48 dB | 2.97 dB |
| 64 | 10⁻³ | 0.70 dB | 0.90 dB |
| 100 | 10⁻³ | 0.30 dB | 0.45 dB |
| 100 | 10⁻² | −0.17 dB | −0.01 dB |
| 200 | 10⁻³ | −0.19 dB | −0.11 dB |
| 1000 | 10⁻³ | −0.92 dB | −0.90 dB |
| 72 | 0.5 | −1.83 dB | −1.59 dB |
| 77 | 0.5 | −1.81 dB | −1.59 dB |

A single unknown phase per message costs only 0.1–0.2 dB at k ≈ 100. With ideal joint detection, phase is nearly free if it is constant over the message. At 50% success (the amateur-radio threshold convention) the finite-k requirement is about −1.6 to −1.8 dB, below −1.59 dB because a 50% error rate is allowed.

**Noncoherent fading.** Yang, Durisi and Polyanskiy (2016) prove that without receiver CSI the gap to −1.59 dB shrinks only as ((log k)/k)^{1/3}, versus 1/√k with CSI [P]. Reaching −1.5 dB needs ≥7×10⁷ bits without CSI versus 6×10⁴ with CSI. At k = 1000 the no-CSI case costs 2 dB more [P]. Their Table I (ε = 10⁻³, single antenna) gives the following achievable values:

| k | Achievable E/N0 | Eb/N0 [D] | Optimal repetitions N* |
|---:|---:|---:|---:|
| 10 | 67 | 8.3 dB | 18 |
| 100 | 2.4×10² | 3.8 dB | 38 |
| 1000 | 1.3×10³ | 1.1 dB | 96 |

My orthogonal-code calculation with L independent Rayleigh looks and square-law combining reproduces this [C]:

| k | ε | L=1 | L=2 | L=4 | L=8 | L=16 | L=32 | L=64 | L=128 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 10⁻² | 18.4 | 10.0 | 5.9 | 4.0 | 3.1 | **3.0** | 3.3 | 4.0 |
| 100 | 10⁻³ | 28.4 | 15.1 | 8.8 | 5.7 | 4.3 | **3.8** | 3.9 | 4.5 |
| 16 | 10⁻³ | 28.6 | 16.0 | 10.4 | 8.0 | **7.2** | 7.2 | 7.8 | 8.7 |

Values are Eb/N0 in dB, energy split equally over L independent looks.

There is an optimal diversity order. Splitting energy too finely creates a noncoherent combining loss, and the curve turns up. Matching published Table I at k = 100 and the "2 dB at k = 1000" statement validates the calculation.

**Implications for Matrix Modem.**

- On a channel that is phase-stable over the message, the target is ≈0.3–0.5 dB Eb/N0 for 100 bits at 10⁻³. Every dB above that is recoverable in principle by better detection.
- On an underspread fading channel without exploitable coherence, ≈3.8 dB is the floor, and only with ≈30–40 independent looks.
- In a quasi-static fade that spans the message there is no useful finite-energy guarantee: 28 dB at 10⁻³. This is the existing chapter's outage point, now quantified.

## 2. Brute-force and near-ML decoding of short codes

### 2.1 Exhaustive and a-priori codebook correlation (JT65 DS, Q65 and FT8 AP)

**What it computes.** For a candidate list of L messages, re-encode each and score it against the received soft data. With MFSK energy detection the score is Σⱼ E[j, cⱼ], a product of a one-hot codeword matrix **[L × nM]** with energies **[nM × H]**, where H is the number of time/frequency hypotheses. It is a binary-operand GEMM, so INT4/INT1-friendly, and arithmetic is ∝ L·n·M·H. For coherent signalling it is **[L × N]** waveforms times **[N × H]** samples. Full exhaustive search is impractical beyond k ≈ 30–40 bits (judgment). A-priori lists make it feasible.

**Evidence.**

- *JT65* uses a (63,12) RS code over GF(64) with 64-FSK and 63 data plus 63 sync symbols.
  - The Franke–Taylor (FT) stochastic-erasure list decoder is ≈2 dB better than hard-decision Berlekamp–Massey. It is 0.2 dB (high SNR) to nearly 0.5 dB (low SNR) better than Kötter–Vardy, using up to T = 10⁵ erasure trials [P].
  - Hinted "Deep Search" used a codeword list of L = 5850. The authors note DS performance was limited by synchronization failures below about −28 dB [P: Franke & Taylor 2016, Fig. 4].
  - A third-party simulation puts FT near −25 dB and DS near −29.5 dB, both SNR in 2500 Hz [P, secondary: Estévez].
- *Q65* uses a (65,15) QRA code over GF(64), punctured to (63,13) with a 12-bit CRC, plus 22 sync symbols. Thresholds without AP versus with maximum AP [P: Taylor et al. 2021]:

  | Submode | No AP | Max AP | AP gain |
  |---|---:|---:|---:|
  | 15 s | −22.2 dB | −23.7 dB | 1.5 dB |
  | 30 s | −24.8 dB | −26.6 dB | 1.8 dB |
  | 60 s | −27.6 dB | −30.2 dB | 2.6 dB |
  | 120 s | −30.8 dB | −32.5 dB | 1.7 dB |
  | 300 s | −33.8 dB | −37.4 dB | 3.6 dB |

  Decoding remains effective with Doppler spread up to about ten times the tone spacing.
- *FT8/FT4* use LDPC (174,91) with BP then OSD. Maximum AP gives ≈2 dB on mild channels, and ≈5 dB (FT4) and ≈10 dB (FT8) on the ITU high-latitude moderate channel (10 Hz spread, 3 ms) [P: Franke, Somerville & Taylor 2020].

**Interpretation.** AP and DS gains are real when the hypothesis set is genuinely small and known. They shrink the information content, for example to a few bits ("RRR/73" for FT8), so they are not decoder sensitivity for a fixed k-bit payload. For Matrix Modem's encrypted, unpredictable payload the analogue is header, key and schedule hypotheses, not message content. The JT65 result shows that **separate sync becomes the binding constraint once decoding is list-limited**.

### 2.2 Multi-symbol block detection (coherent across a few symbols)

FT8 correlates every 3-symbol block against 8³ = 512 three-symbol waveforms. FT4 does the same for 4-ary with N up to 4. Bit LLRs come from max-log over the 512 correlation magnitudes. The 512 values are built from per-symbol FFT correlations, avoiding redundant products [P]. As a matrix this is a one-hot **[M^N × N·M]** times complex per-symbol correlations **[N·M × blocks]**, followed by a magnitude. Gains [P]:

| Channel | FT8 block detection | FT8 BP+OSD on top | FT4 block detection | FT4 total |
|---|---:|---:|---:|---:|
| AWGN | 0.7 dB | +0.5 dB | 1.6 dB | 2.2 dB |
| Mid-latitude disturbed (1 Hz, 2 ms) | 0.5 dB | — | 1.3 dB | 2.5 dB |
| High-latitude moderate (10 Hz) | — | — | ≈0.1 dB | — |

FT8's total improvement over the N=1/BP baseline is 1.2 dB on AWGN and 2.1 dB on the mid-latitude disturbed channel. On the 10 Hz channel the authors attribute FT4's small gain to a coherence time comparable to the block. FT8 fails to reach 50% at any SNR with the baseline decoder because the spread exceeds its tone spacing [P].

This is the clearest published case of **hypothesis-matrix growth (M^N) converting to dB only while the channel is coherent over the block**.

### 2.3 Ordered statistics decoding (OSD)

**What it computes.** Sort positions by reliability and Gaussian-eliminate to the most reliable basis (MRB). Then enumerate test error patterns (TEPs) of weight ≤ t on the k MRB bits, re-encode each, and keep the best correlation [P: Fossorier & Lin 1995].

- Re-encoding is **[T × k]·[k × (n−k)]** over GF(2), where T = Σ_{i≤t} C(k,i).
- The metric is **[T × (n−k)]** discrepancies against |LLR|.

Both are GEMM-shaped per received word. Gaussian elimination is per word and serial. List sizes [C]:

| k | t | TEPs T |
|---:|---:|---:|
| 64 | 3 | 43,745 |
| 64 | 4 | 679,121 |
| 100 | 3 | 166,751 |
| 100 | 4 | 4,087,976 |

**Sensitivity.** On eBCH(128,64), OSD with t = 4 is within ≈0.1 dB of the NA at CER ≈ 10⁻⁴, and t = 3 leaves ≈0.5 dB [P: Coşkun et al.]. BP followed by OSD(t=4) on a (3,6) LDPC code gains ≈1 dB at 10⁻⁴ over BP alone [P]. In the field it underlies FT8/FT4, where hybrid BP+OSD adds 0.5–0.6 dB on AWGN [P]. Complexity-reduction rules (stopping and discarding) give a significant reduction at negligible loss [P: Yue et al. 2021].

**Scaling.** Fossorier & Lin relate the order needed for asymptotically ML performance to roughly d_min/4. At (128,64), t = 4 is already ≈0.1 dB from the NA. Extra order buys dB until ML is reached, then nothing: monotone, then saturating. Cost is exponential in t. It is batched GEMM-friendly, but the per-word permutation and elimination hurt GPU utilization. Recent work instead pairs neural min-sum with OSD as a post-processor to protect throughput [P: arXiv 2307.06575, 2404.14165, 2509.25580, 2412.20828]. **No GPU-specific OSD throughput study was located** (search budget exhausted, see notes).

### 2.4 CRC-aided polar SCL and PAC codes

The successive-cancellation list decoder costs O(L n log n), sequential over bits [P: Tal & Vardy 2015]. A (128,71) polar code with CRC-7 (dimension 64) and list size 32 performs very close to the RCU bound down to low error rates. With L = 8, the gap to the ML lower bound is already nearly invisible for (128,64) [P: Coşkun et al.]. PAC (128,64) codes (Arıkan 2019) under Fano/sequential decoding are reported to meet the dispersion/NA approximation [P, secondary: citing papers found by search, e.g. Rowshan, Burg & Viterbo]. List decoding of PAC reportedly needs very large lists to match [P, secondary].

GPU fit is poor. The structure is a butterfly with data-dependent sorting and pruning, not a GEMM. Batching across codewords gives throughput but not tensor-core utilization.

### 2.5 GRAND and ORBGRAND

GRAND guesses noise patterns in likelihood order until the syndrome is zero. It is capacity-achieving with random codes, and the abandonment variant GRANDAB is too [P: Duffy, Li & Médard 2019]. ORBGRAND needs only the reliability *rank*, at most ⌈log₂ n⌉ bits per bit. It is fully parallelizable and approximately ML [P: Duffy, An & Médard 2022; abstract targets "short, high-rate" codes].

Syndrome checking is **[Q × n]·[n × (n−k)]** over GF(2), a good bit-GEMM, but the query count Q is the issue. For a random-like code, each non-codeword guess hits *some* codeword with probability ≈2^{−(n−k)}, so useful search depth scales with 2^{n−k}. That is reasoning from the GRAND model, not a measured figure. **At the low rates relevant here (n−k ≫ k), GRAND is the wrong tool.** Its strength is high rate and low redundancy.

### 2.6 Reed–Muller / biorthogonal codes with fast Hadamard decoding

First-order RM(1,m) = biorthogonal (2^m, m+1) codes have exact ML decoding by a fast Hadamard transform: m·2^m additions versus 2^{2m} multiply-adds for a dense correlator. The ratio is [C]:

| m | Dense / FHT |
|---:|---:|
| 5 | 6.4× |
| 6 | 10.7× |
| 10 | 102× |
| 16 | 4096× |

Mariner Mars 1969 flew the (32,6) biorthogonal code. Green's Caltech thesis gives the factorized "Green machine" decoder [P: JPL TR 32-1354; Green 1969]. For higher-order RM codes, recursive projection–aggregation (RPA) decoding is close to ML at lengths ≤ 1024. RM codes under RPA outperform polar codes under SCL+CRC with the same parameters [P: Ye & Abbe 2020].

Biorthogonal codes are exactly the large-alphabet orthogonal signals of §5. Tensor cores win over FHT only for small m, or when the GEMM's other dimension (observations × hypotheses) is huge and the transform's memory passes dominate. A Hadamard-structured block GEMM (Kronecker-factored, say 2^m = 2^a·2^b with two GEMM passes of size 2^a and 2^b) is the natural middle ground. It is an inference from the factorization, not a published GPU result.

### 2.7 Tail-biting convolutional codes with list Viterbi (EbNaut-style)

At (128,64), a memory-14 TBCC with WAVA (about four iterations) is within ≈0.07 dB of the NA at CER ≈ 10⁻⁴. Memory 8 loses ≈1 dB at 10⁻⁵ [P: Coşkun et al.]. With optimized CRCs and serial list Viterbi, CRC-ZTCCs come within 0.4 dB of RCU at 10⁻⁴ (k = 64, rate 1/2), and several CRC-TBCCs beat RCU at moderate SNR. Expected list rank tends to 1 at high SNR [P: Yang, Liang, Pan & Wesel 2022].

EbNaut is a coherent BPSK amateur VLF/LF mode with strong convolutional coding and CRC. One published transmission used code label "8k19a" with a 16-bit CRC. Interpreting the label as rate 1/8, K = 19 is plausible but not verified, because abelian.org was unreachable. Two design points are confirmed:

- The receiver must know frequency and timing in advance. It deliberately spends no energy on sync, and needs substantial computing power [P: PA3FWM].
- A 30° phase-search step bounds the phase-mismatch loss at 0.3 dB [P: DL4YHF].

The trellis (2^ν states × n stages) is a (max,+) recursion, which tensor cores do not execute. List Viterbi is a poor GEMM fit.

### 2.8 Fano sequential decoding (WSPR)

WSPR sends 50 bits with a K = 32, r = 1/2 code over 162 4-FSK symbols, each carrying one sync bit and one data bit. Transmission lasts 110.6 s, and it is decoded with a Fano sequential decoder because K = 32 rules out Viterbi [P: Wikipedia, secondary]. The usual quoted threshold is ≈−28 dB in 2500 Hz, with a −34 dB claim on the same page unreconciled [P, secondary]. A third-party simulation shows ~34% decodes at −30 dB [P, secondary]. At −28 dB the total Eb/N0 is ≈9.4 dB [D], far from the ≈−1.6 dB ideal at 50%. Fano decoding is data-dependent and serial, so it does not fit GEMM.

### 2.9 Where the WSJT modes sit (AWGN, 50% decode)

Unit conversion: C/N0 = SNR₂₅₀₀ + 33.98 dB-Hz and Eb/N0 = C/N0 + 10 log₁₀(T/k). "Data-only" T counts only data symbols, which matches the Franke–Taylor convention Eb/N0 = SNR₂₅₀₀ + 29.1 dB for JT65.

| Mode | Threshold [P] | Eb/N0 data-only [D] | Eb/N0 incl. sync [D] | Sync overhead [D] | Per-symbol noncoherent MFSK capacity limit at its rate [C] | Codeword-level ideal at 50% [C] |
|---|---|---:|---:|---:|---:|---:|
| JT65 (FT decoder) | ≈−25 dB | 4.1 dB | 7.1 dB | 3.0 dB | 2.1 dB (64-FSK, 1.14 b/sym) | −1.59 dB |
| Q65-60A | −27.6 dB | 3.3 dB | 4.6 dB | 1.3 dB | 2.05 dB (64-FSK, 1.22 b/sym) | −1.59 dB |
| FT8 (BP+OSD) | −20.8 dB | 4.0 dB | 5.3 dB | 1.3 dB | 2.95 dB (8-FSK, 1.33 b/sym)* | −1.59 dB |

\* FT8's 3-symbol coherent block detection exceeds the per-symbol channel assumption, so its limit is only indicative. The Monte Carlo accuracy is ≈±0.1 dB.

The loss decomposes as follows (AWGN, 50%):

- **Code and decoder loss** against the symbol-channel capacity: ≈1.2 dB for Q65, ≈1.0 dB for FT8 and ≈2 dB for JT65. Better near-ML decoding is worth at most this much, and less once finite length is charged.
- **Per-symbol noncoherent detection loss** against codeword-level ideal detection: ≈3.6 dB for 64-FSK and ≈4.5 dB for 8-FSK at these rates. Only phase-coherent combining across symbols (block or codeword correlation) can recover it, and only on phase-stable paths.
- **Sync energy**: 1.3–3.0 dB. Joint sync and decoding can recover it only if the extra hypotheses do not cost more (§4, §6).

Q65's threshold also does not scale ideally with duration. From 15 s to 300 s the ideal gain is 13.6 dB, but 11.6 dB is observed, so total Eb/N0 worsens from ≈4.0 to ≈6.0 dB [D]. The cause is not established. Wider relative search grids and narrower tones are plausible contributors.

## 3. Sparse regression codes and coded compressed sensing

**SPARCs.** The codeword is Aβ, with A of size **n × LM** and β having one nonzero per section of size M. Least-squares (ML) decoding is reliable up to capacity [P: Joseph & Barron 2012]. AMP decoding is capacity-achieving asymptotically, with complexity proportional to the design-matrix size [P: Rush, Greig & Venkataramanan 2017]. Error exponents decay in n/(log n)^{2T} [P: Rush & Venkataramanan 2019].

Each AMP iteration is one A·v and one Aᵀ·z, about 2nLM MACs dense, or O(LM log LM) with Hadamard/DFT designs. These are **matrix–vector** products. Tensor-core efficiency requires batching across codewords, received blocks or sync hypotheses. A dense random A is otherwise memory-bandwidth bound.

**Finite length.** With Hadamard designs and optimized power allocation [P: Greig & Venkataramanan 2018]:

- At R = 1 and R = 1.5 bits per real dimension, AMP-SPARCs beat WiMAX LDPC coded modulation near the Shannon limit but have a weaker waterfall.
- One worked case sits 2 dB from the Shannon limit at Eb/N0 = 5.7 dB.
- For R ≤ 1 the best power allocation was flat, and the large-system theory then predicts no initial decoding progress. Finite-size fluctuations rescue it in practice.
- A partial outer LDPC code creates a steep waterfall.

All reported operating points are at moderate SNR (snr 7–31). **I found no finite-length evidence that AMP-SPARCs beat OSD/list-decoded short codes at k ≈ 100 and very low SNR.** At such low rate a SPARC with orthogonal sections becomes structured PPM/MFSK (§5).

**Unsourced random access (coded compressed sensing, CCS).** Polyanskiy's benchmark uses k = 100 bits, n = 30,000 real channel uses and per-user error 0.05. The tree-coded CCS of Amalladinne, Chamberland and Narayanan splits messages into sub-blocks recovered by compressed sensing and stitched by a tree code; the conference version reported ≈4.3 dB from Polyanskiy's achievability [P]. The SPARC/AMP version reaches **4.3 dB Eb/N0 at Ka = 300**, 0.7 dB better than the previous 5 dB [P: Fengler, Jung & Caire 2021]. That instance uses J = 15, L = 16 sections of 2^15 columns (B = 100, n = 30,000). Densely, A would hold 30,000 × 524,288 ≈ 1.6×10¹⁰ entries, about 7.9 GB at INT4 [C]. Structured matrices are used.

These are multi-user sum-rate results. Their transferable lesson is the **inner CS + outer tree code split**, which keeps the dictionary at 2^J ≪ 2^k. Fading and MIMO extensions exist, including covariance-based massive-MIMO activity detection, but they target base stations with many antennas.

## 4. Noncoherent, pilot-free and joint-sync decoding

- **MSDD.** Multiple-symbol differential detection does ML sequence estimation over N-symbol windows. One or two extra symbols give substantial gain over conventional DPSK, and the gain depends on M and N [P: Divsalar & Simon 1990]. Hypotheses per window are M^{N−1}, a **[M^{N−1} × N] × [N × windows]** GEMM, exponential in N.
- **Noncoherent sequence detection.** Viterbi-based detection with implicit phase memory approaches coherent performance as memory grows [P: Colavolpe & Raheli 1999]. A companion letter states that the convergence rate is independent of SNR. Its states multiply with memory, and reduced-state techniques are needed.
- **Iterative joint phase estimation and decoding.** Barbieri & Colavolpe (2007) handle phase noise and frequency offset. Peleg, Shamai & Galán (2000) report a turbo code with phase constant over 10 symbols at BER < 10⁻³ at 2.6 dB, 1.3 dB from that channel's capacity [P, abstract]. This is iterative message passing, not GEMM.
- **Pilot versus pilot-free at finite length.** For a 168-symbol packet at rate 0.48 over Rayleigh block fading with 8-symbol blocks, optimized pilot-assisted transmission needs **1.2 dB more** Eb than a well-designed noncoherent scheme at PER 10⁻³. Noncoherent transmission is more energy-efficient even with optimized pilot number and power [P: Östman et al. 2019]. A practical PAT scheme came within ≈1 dB of its bound.
- **Unitary/noncoherent block codes.** The GLRT ‖C_iᴴY‖² over unitary codewords is a pure **[codewords × T]** GEMM followed by a norm [P: Hochwald & Marzetta 2000].
- **Joint synchronization ("codeword as its own sync").** In asynchronous communication, training-based schemes that separate sync from information are suboptimal in general, especially at high rate and high asynchronism [P: Tchamkerten, Chandar & Wornell 2013].

  Practical evidence: JT65 DS was sync-limited (§2.1). EbNaut removes sync energy by assuming known timing and frequency. Separate sync costs 1.3–3.0 dB in WSJT modes [D].

  The cost side: adding log₂H sync hypotheses to an ideal orthogonal detector at k = 100, ε = 10⁻³ costs +0.33 / +0.63 / +0.91 / +1.17 dB for 10 / 20 / 30 / 40 search bits [C]. At ε = 10⁻² the figures are within ≈0.06 dB of these. This is pessimistic because neighbouring cells are correlated.

  So joint detection can recover most of a 1.3–3 dB sync overhead when H ≲ 2^{20–30}. Beyond that, the null hypotheses consume the gain. Calibrated whole-search false alarm (existing chapter) is mandatory.

## 5. Large-alphabet noncoherent orthogonal signalling

For M-ary orthogonal signalling with one unknown phase per symbol, the AWGN requirement for symbol error ε is [C]:

| ε | M=2 | M=4 | M=16 | M=64 | M=1024 | M=2^16 | M=2^100 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 10⁻³ | 10.94 | 8.61 | 6.41 | 5.25 | 3.97 | 2.97 | 0.45 |
| 10⁻² | — | — | — | 3.97 | — | — | −0.01 |

Values are Eb/N0 in dB.

The noncoherent penalty versus coherent shrinks with M: 1.1 dB at M = 2 and ≈0.15 dB at M = 2^100 [C].

The receiver is a transform:

- An FFT for MFSK and FPPM.
- An FHT for Walsh codes. IS-95's pilotless reverse link mapped six coded bits to one of 64 Walsh words and detected them noncoherently with an FHT [P, secondary].
- A one-hot GEMM for PPM.

Dense GEMM is M/log₂M times more arithmetic. It wins only when M is small (≲64) or the transform is non-uniform (keyed hop grids, non-grid frequencies, arbitrary template banks). Large M costs bandwidth (MFSK) or peak power (PPM), and raises the multiple-test null burden. HF Doppler spread caps the useful symbol length (§7).

## 6. Acquisition and search: hypothesis scaling with coherent time T

| Search | Algorithm | Cost |
|---|---|---|
| Code phase × Doppler | FFT circular correlation, ≈2000× faster acquisition than serial search [P: van Nee & Coenen 1991] | O(N log N) per Doppler bin. Bins ∝ T because resolution ≈ 1/T |
| Drift (linear frequency rate) | "Tree deDoppler" (Taylor tree) incoherent drift search [P: Siemion 2013; Enriquez 2017; turboSETI] | n² path sums reduced to ≈n log₂ n |
| Acceleration / jerk | Fourier-domain acceleration search [P: Ransom et al. 2002] and jerk search [P: Andersen & Ransom 2018] | Jerk search runtime is significantly longer. A GPU implementation gives a 90× speedup over PRESTO [P: Adámek et al.] |
| Polynomial phase | High-order ambiguity function [P: Peleg & Porat 1991] | Non-ML estimator with SNR thresholds |

The incoherent drift sum loses efficiency once drift exceeds one channel per time step, falling as 1/N with N channels crossed [P, secondary]. Breakthrough Listen commonly searches ±4 Hz/s.

**Scaling.** For a fixed physical uncertainty range [C, from resolution arguments]:

- Frequency cells grow ∝ T.
- Drift cells grow ∝ T².
- Jerk cells grow ∝ T³.
- Each direct correlation costs ∝ T samples.

So brute-force frequency×drift search costs ∝ T⁴ direct, or ≈T³ log T with FFTs over frequency. Coherent gain grows only ∝ T, and the null-hypothesis count grows with the cell count.

Costas arrays give MFSK sync patterns with near-ideal (thumbtack) time–frequency ambiguity [P: Costas 1984]. FT8 embeds three 7×7 Costas arrays (21 symbols) [P]. Their detection is itself a small tone-bank GEMM. Hierarchical search (coarse incoherent, then fine coherent; existing chapter) is the only way the T³–T⁴ growth stays affordable. Extra compute buys dB in search only until residual errors fall below a fraction of the cell size, roughly 1/(2T) in frequency and 1/T² in drift. After that it buys nothing.

## 7. Doppler-spread-tolerant detection

- **Estimator–correlator.** For a Gaussian random channel the optimal detector is a quadratic form, yᴴR(R+N₀I)⁻¹y [P: Kailath 1960].
- **Low-rank form.** A band-limited Doppler process of width B_D over time T is well spanned by ≈2B_DT+1 DPSS (Slepian) vectors [P: Zemen & Mecklenbräuker 2005; Thomson 1982 multitaper]. The detector becomes a **[F·D × N] × [N × windows]** GEMM, with D ≈ 2B_DT+1 per tone, followed by group energy sums. This is the matched-subspace form in the existing chapter (Scharf & Friedlander).

  It recovers energy smeared across bins, but with a D-dimensional noncoherent combining loss. Expect monotone gain from D = 1 up to the true spread dimension, then loss.
- **Symbol duration versus spread, measured.** FT8 has 0.16 s symbols and 6.25 Hz spacing; FT4 has 0.048 s symbols and 20.8 Hz spacing. On AWGN FT8 is ≈3 dB more sensitive. On the 10 Hz high-latitude channel FT8 degrades by 12.2 dB (to −8.6 dB) and FT4 only by 5.3 dB (to −12.2 dB) [P, D]. Q65 offers spacings 1–16× the base precisely to match spread and decodes at spreads up to ~10× the spacing [P]. Earlier JT65 simulations showed 0.2 Hz spread slightly *helping* FT decoding near threshold, consistent with low-rate codes exploiting fading peaks [P].
- **Diversity.** Section 1's table is the information-theoretic statement. At k = 100 the optimum is ≈30–40 independent looks. Hopping or time spread helps only if the looks are independent, which the existing coherence-bandwidth measurements must establish.

## 8. Learned, GPU-parallel and low-precision decoders (2018–2026)

**One-shot neural decoders.** They hit the 2^k-class "curse of dimensionality". A length-100 rate-1/2 code has 2^50 codewords, too many to train [P: Gruber et al. 2017].

**Unrolled and weighted BP and GNN decoders.** These improve on BP for HDPC codes [P: Nachmani et al. 2016/2018; Cammerer et al. 2022]. The transformer-based ECCT reports state-of-the-art results among neural decoders at n ≤ ~128 [P: Choukroun & Wolf 2022]. Pruned and quantized neural BP reaches ≈0.5 dB from ML for an RM code with 5-bit quantization [P: Buchberger et al. 2021]. The arXiv predecessor reported 0.27 dB, and the difference is unexplained.

None of these exceeds ML. OSD(t=4) and list decoding already sit at ≈0.1 dB from the NA at (128,64). These decoders are GEMM-native, transformer and GNN layers, so they run fast on tensor cores. Their value is throughput or latency per near-ML decode, not sensitivity.

**Low-precision metrics.**

- A 4-bit RCQ BP decoder is within 0.1 dB of full-precision BP on an 802.11 rate-1/2 LDPC code [P: arXiv 2104.09480].
- Uniform versus non-uniform coarse quantization differs by < 0.01 dB in one study [P: arXiv 2205.01503].
- ORBGRAND's reliability input is only a rank [P].
- JT65's FT soft decoder beats hard-decision BM by ≈2 dB [P], the classic soft-versus-hard gap.

For decoding, 4-bit LLRs are therefore a ≈0.1 dB question. The large 4-bit risks are upstream: raw-sample quantization, accumulator range and threshold-tail calibration, as covered in `int4-and-accelerators.md` and `information-limits.md`.

## 9. Comparison table

Shapes: H = time/freq/drift hypotheses, N = samples, L = candidate list, n, k = code parameters, M = alphabet.

| Algorithm | Matrix form | Ops scaling | dB vs bound (evidence) | Phase / frequency / fading robustness | GPU / INT4 fit | Sources |
|---|---|---|---|---|---|---|
| Exhaustive / AP list codeword correlation | one-hot [L×nM]·E[nM×H], or waveforms [L×N]·X[N×H] | L·n·M·H (or L·N·H) | ML for the list. JT65 DS ≈4–5 dB over FT, Q65 AP 1.5–3.6 dB, FT8 AP 2–10 dB, all from reduced message entropy | Inherits the symbol detector. Sync-limited (JT65) | Excellent: binary operand, huge GEMM | Franke & Taylor 2016; Taylor et al. 2021; Franke et al. 2020 |
| Multi-symbol block detection (N symbols) | one-hot [M^N×NM]·C[NM×blocks] then \|·\| | M^N per block | FT8 +0.7 dB, FT4 +1.6 dB (AWGN) | Gain collapses when coherence < block (≈0.1 dB at 10 Hz) | Good for M^N ≲ 10³–10⁴ | Franke et al. 2020 |
| OSD order t | GF(2) [T×k]·[k×(n−k)] plus metric [T×(n−k)]·\|LLR\| per word | T=Σ_{i≤t}C(k,i), e.g. 6.8×10⁵ at k=64, t=4 | ≈0.1 dB from NA at (128,64), t=4 | Needs soft LLRs from a front end | Batched GEMM good. Per-word elimination serial | Fossorier & Lin 1995; Coşkun et al.; Yue et al. |
| CA-SCL polar / PAC (Fano) | butterfly, sort/prune | O(L n log n) / variable | Near RCU (L=32, 128-bit); PAC meets NA | Same | Poor (control flow) | Tal & Vardy 2015; Coşkun et al.; Arıkan 2019 |
| GRAND / ORBGRAND | syndrome [Q×n]·[n×(n−k)] GF(2) | Q up to ~2^{n−k} | ≈ML for high-rate codes | Same; ≤⌈log₂n⌉-bit reliability | Bit-GEMM good, but Q explodes at low rate | Duffy et al. 2019, 2022 |
| RM(1,m) / biorthogonal ML | FHT, or dense [2^m×2^m]·[2^m×H] | m·2^m vs 4^m | Exact ML for that code | Coherent unless combined noncoherently | FHT or Kronecker-factored GEMM | Green 1969; JPL TR 32-1354; Ye & Abbe 2020 |
| TBCC + CRC list Viterbi | (max,+) trellis | 2^ν·n·(list) | Memory 14 ≈0.07 dB from NA; CRC-ZTCC ≤0.4 dB from RCU | Coherent BPSK (EbNaut) needs known phase/time | Poor (no (max,+) tensor op) | Coşkun et al.; Yang et al. 2022 |
| Fano sequential (WSPR) | tree search | variable, SNR-dependent | WSPR ≈9.4 dB Eb/N0 total at −28 dB [D] | 4-FSK noncoherent | Poor | WSPR refs |
| SPARC + AMP | A[n×LM]·v per iteration | 2nLM·T_iter (dense); LM log LM (Hadamard) | Asymptotically capacity-achieving; finite: ~2 dB at R=1.5, n~10³ | Shown at moderate SNR only | Matrix–vector, so batch needed | Joseph & Barron; Rush et al.; Greig & Venkataramanan |
| CCS / URA (tree + AMP) | A[n×L2^J] AMP + tree code | as above + tree combinatorics | Ka=300: 4.3 dB (Pe 0.05); ≈4.3 dB from FBL in conference version | Fading/MIMO variants exist | Structured transforms | Fengler et al. 2021; Amalladinne et al. 2020 |
| MSDD / noncoherent sequence detection | [M^{N−1}×N] per window / Viterbi | M^{N−1} | Approaches coherent as N grows | Robust to constant/slow phase; frequency offset needs extension | GEMM for MSDD; Viterbi poor | Divsalar & Simon 1990; Colavolpe & Raheli 1999 |
| Pilot-free vs pilot (block fading) | GLRT over codewords | as list correlation | Noncoherent beats PAT by 1.2 dB (n=168) | Block fading | GEMM | Östman et al. 2019; Hochwald & Marzetta 2000 |
| Large-M orthogonal (MFSK, PPM, Walsh) | FFT/FHT, or dense [M×M] | M log M vs M² | Noncoherent 0.45 dB at M=2^100 vs 5.25 dB at M=64 (10⁻³) [C] | Phase-free; Doppler caps T | Transform better unless M small or grid irregular | IS-95 refs; §5 [C] |
| FFT acquisition / dedoppler / FDAS | FFT over frequency; tree over drift | ∝T³ log T (freq×drift), more for jerk | Residual-offset loss 0.91 dB at a 0.5/T grid and 3.92 dB at half-bin (existing doc) | Drift/jerk explicit | FFT plus small GEMMs | van Nee & Coenen; Siemion; Ransom; Andersen & Ransom |
| DPSS subspace / estimator–correlator | [F·D×N]·[N×W] | D≈2B_DT+1 | Recovers spread energy minus D-dimensional combining loss | Designed for spread | Excellent | Kailath 1960; Zemen & Mecklenbräuker 2005 |
| Neural decoders | dense layers | per-layer GEMMs | ≥ ML gap (≈0.27–0.5 dB for quantized NBP on RM) | Learned; no evidence beyond training channel | Native | Gruber 2017; Nachmani 2018; Choukroun & Wolf 2022; Buchberger 2021 |

## 10. Where brute force buys dB

1. **Codeword-level (or long-block) coherent correlation on phase-stable channels.** This recovers the ≈3.6–4.5 dB per-symbol noncoherent penalty of MFSK at WSJT-like rates [C]. It is a huge hypothesis GEMM. Evidence: FT8 block detection (+0.7 dB with N=3 only) and the orthogonal bound tables. It is conditional on coherence over the correlated span.
2. **Joint sync + decode instead of sync symbols.** This recovers 1.3–3.0 dB of sync energy [D] while H ≲ 2^20–2^30 hypotheses (≤0.6–0.9 dB search penalty) [C]. JT65 DS's sync-limited failure is the positive evidence; asynchronous-capacity theory is the general support.
3. **Near-ML decoding of a short code.** OSD order 3→4 is worth ≈0.4 dB at (128,64), and BP→BP+OSD 0.5–1 dB [P]. It is monotone up to ML, then flat.
4. **A-priori list decoding when the candidate list is truly known.** Honest gains of 1.5–10 dB [P], but only by reducing message entropy.
5. **Diversity combining across independent looks** (DPSS subspaces, hopping, time spread). In Rayleigh fading it moves k = 100 from 28 dB (L=1) to ≈3.8 dB (L≈32) at 10⁻³ [C, P]. Compute is cheap here; independence is what matters.

## 11. Where it does not

1. **Beyond ML.** No decoder beats ML for the given detector output. At (128,64) the best decoders are already 0.07–0.1 dB from the NA, and neural decoders also live below that ceiling.
2. **Against the channel model.** No compute closes the ≈3.5 dB no-CSI fading gap at k = 100 (0.30 versus 3.8 dB) or the quasi-static outage (28 dB at 10⁻³). Only coherence, diversity or longer k can.
3. **Coherent hypotheses beyond the coherence time.** FT4's block detection gains only ≈0.1 dB at 10 Hz spread [P]. Longer segments lose sinc²/η energy (existing chapter), while hypotheses grow ∝ T…T⁴.
4. **Ever-larger search spaces.** Each factor of 2^10 in hypotheses costs ≈0.3 dB of threshold in the ideal case [C], plus false alarms per hour.
5. **GRAND at low rate.** Query depth scales with redundancy.
6. **SPARC/AMP for a single low-SNR short message.** There is no finite-length evidence of an advantage over OSD/list codes. The algorithm is matrix–vector, and at low rate it reduces to structured orthogonal signalling.
7. **Dense GEMM where a fast transform exists.** For regular MFSK or Walsh grids, FFT/FHT are M/log₂M cheaper. Tensor cores pay only for irregular, keyed or subspace banks, or for very wide batch dimensions.
8. **Lower precision beyond ~4-bit LLRs.** Losses are ≈0.1 dB. Compute saved there does not translate into dB, and saturation or tail calibration matter more.

## 12. Suggested Matrix Modem discriminating experiments (from the above)

1. Paired equal-energy comparison of per-symbol energy scoring versus codeword-level coherent correlation versus DPSS-subspace scoring on identical replayed channels (quiet, 1 Hz, 10 Hz). Report the dB at fixed whole-search false alarm, plus hypothesis counts.
2. Sync-symbol versus sync-free (joint) detection at equal total energy, sweeping H from 2^10 to 2^30.
3. Short code family at k ≈ 64–128: eBCH/TBCC with OSD or list decoding, with batched-GEMM OSD on the target GPU, against the [C] orthogonal and Yang–Durisi–Polyanskiy bounds.
4. Diversity order L sweep (hop, segment) against §1's optimum, using measured, not assumed, look independence.

## 13. Citations and access notes

**Bounds**

- Y. Polyanskiy, H. V. Poor, S. Verdú, "Channel coding rate in the finite blocklength regime," *IEEE TIT* 56(5), 2010. DOI [10.1109/TIT.2010.2043769](https://doi.org/10.1109/TIT.2010.2043769).
- Y. Polyanskiy, H. V. Poor, S. Verdú, "Minimum energy to send k bits through the Gaussian channel with and without feedback," *IEEE TIT* 57(8):4880–4902, 2011. DOI [10.1109/TIT.2011.2159041](https://doi.org/10.1109/TIT.2011.2159041). Abstract via [MIT DSpace](https://dspace.mit.edu/handle/1721.1/81369); the full-text fetch was rate-limited.
- W. Yang, G. Durisi, Y. Polyanskiy, "Minimum energy to send k bits over multiple-antenna fading channels," *IEEE TIT* 62(12):6831–6853, 2016. [arXiv:1507.03843](https://arxiv.org/abs/1507.03843); full text read; Table I and Fig. 1 used. ISIT 2015 version DOI [10.1109/ISIT.2015.7282999](https://doi.org/10.1109/ISIT.2015.7282999).
- M. C. Coşkun et al., "Efficient error-correcting codes in the short blocklength regime," *Physical Communication* 34:66–79, 2019. [arXiv:1812.08562](https://arxiv.org/abs/1812.08562); HTML read. Values are approximate where read from text describing figures.
- G. Liva et al., "Code design for short blocks: a survey," [arXiv:1610.00873](https://arxiv.org/abs/1610.00873); not read in full.

**Amateur weak-signal practice**

- S. J. Franke, J. H. Taylor, "Open source soft-decision decoder for the JT65 (63,12) Reed–Solomon code," *QEX* May/June 2016, 8–17. [PDF](https://wsjt.sourceforge.io/FrankeTaylor_QEX_2016.pdf); full text read.
- S. J. Franke, B. Somerville, J. H. Taylor, "The FT4 and FT8 communication protocols," *QEX* July/Aug 2020, 7–17. [Reprint PDF](https://downloads.hamradiodeluxe.com/FT4_FT8_QEX.pdf); full text read; Tables 5–6.
- J. Taylor, B. Somerville, S. Franke, N. Palermo, "Quick-Start Guide to Q65," 2021. [PDF](https://wsjt.sourceforge.io/Q65_Quick_Start.pdf); full text read.
- D. Estévez, "Simulating JT modes: how low can they get?" [blog](https://destevez.net/?p=1677). Secondary simulation; bandwidth convention not stated there.
- WSPR parameters: [Wikipedia](https://en.wikipedia.org/wiki/WSPR_(amateur_radio_software)); secondary, with internally inconsistent thresholds.
- EbNaut:
  - [PA3FWM technote](https://pa3fwm.nl/technotes/tn13d.html).
  - [DL4YHF EbNaut receiver notes](https://qsl.net/d/dl4yhf/speclab/EbNaut_Rcvr.htm).
  - [RAC TCA Jan/Feb 2018 p.25](https://wp.rac.ca/wp-content/plugins/fliphtml5/2018/01/files/basic-html/page25.html), the source of the "8k19a" label.
  - The primary site abelian.org/ebnaut failed DNS resolution, so the code parameters are unverified.

**Decoders**

- M. P. C. Fossorier, S. Lin, "Soft-decision decoding of linear block codes based on ordered statistics," *IEEE TIT* 41(5), 1995. DOI [10.1109/18.412683](https://doi.org/10.1109/18.412683).
- C. Yue et al., "A revisit to ordered statistics decoding: distance distribution and decoding rules," *IEEE TIT* 2021. [arXiv:2004.04913](https://arxiv.org/abs/2004.04913); abstract only.
- Hybrid NMS–OSD and throughput-oriented OSD: [arXiv:2307.06575](https://arxiv.org/abs/2307.06575), [arXiv:2404.14165](https://arxiv.org/abs/2404.14165), [arXiv:2509.25580](https://arxiv.org/abs/2509.25580), [arXiv:2412.20828](https://arxiv.org/abs/2412.20828). Known from search summaries only.
- I. Tal, A. Vardy, "List decoding of polar codes," *IEEE TIT* 61(5), 2015. DOI [10.1109/TIT.2015.2410251](https://doi.org/10.1109/TIT.2015.2410251).
- E. Arıkan, "From sequential decoding to channel polarization and back again," [arXiv:1908.09594](https://arxiv.org/abs/1908.09594).
- M. Rowshan, A. Burg, E. Viterbo, PAC decoding comparison, [arXiv:2002.06805](https://arxiv.org/abs/2002.06805); search summary only.
- K. R. Duffy, J. Li, M. Médard, "Capacity-achieving guessing random additive noise decoding," *IEEE TIT* 65(7), 2019. DOI [10.1109/TIT.2019.2896110](https://doi.org/10.1109/TIT.2019.2896110).
- K. R. Duffy, W. An, M. Médard, "Ordered reliability bits GRAND," *IEEE TSP* 2022. DOI [10.1109/TSP.2022.3203251](https://doi.org/10.1109/TSP.2022.3203251); [arXiv:2001.00546](https://arxiv.org/abs/2001.00546); abstract read.
- M. Ye, E. Abbe, "Recursive projection-aggregation decoding of Reed–Muller codes," *IEEE TIT* 66(8), 2020. DOI [10.1109/TIT.2020.2977917](https://doi.org/10.1109/TIT.2020.2977917).
- R. R. Green, *Decoding Cosets of First-Order Reed–Muller Codes*, Caltech PhD thesis, 1969. [Link](https://thesis.caltech.edu/9626/).
- JPL TR 32-1354, "A high-rate telemetry system for the Mariner Mars 1969 mission." [NTRS](https://ntrs.nasa.gov/api/citations/19690012788/downloads/19690012788.pdf); table of contents only.
- H. Yang, E. Liang, M. Pan, R. D. Wesel, "CRC-aided list decoding of convolutional codes in the short blocklength regime," *IEEE TIT* 68(6), 2022. DOI [10.1109/TIT.2022.3150717](https://doi.org/10.1109/TIT.2022.3150717); [arXiv:2104.13905](https://arxiv.org/abs/2104.13905).

**SPARCs and CCS**

- A. Joseph, A. R. Barron, *IEEE TIT* 58(5), 2012. DOI [10.1109/TIT.2012.2184847](https://doi.org/10.1109/TIT.2012.2184847).
- C. Rush, A. Greig, R. Venkataramanan, *IEEE TIT* 63(3), 2017. DOI [10.1109/TIT.2017.2649460](https://doi.org/10.1109/TIT.2017.2649460).
- C. Rush, R. Venkataramanan, *IEEE TIT* 2019. DOI [10.1109/TIT.2018.2882177](https://doi.org/10.1109/TIT.2018.2882177).
- A. Greig, R. Venkataramanan, *IEEE TCOM* 66(3), 2018. DOI [10.1109/TCOMM.2017.2776937](https://doi.org/10.1109/TCOMM.2017.2776937); [arXiv:1705.02091](https://arxiv.org/abs/1705.02091); full text read.
- A. Fengler, P. Jung, G. Caire, "SPARCs for unsourced random access," *IEEE TIT* 67(10), 2021. DOI [10.1109/TIT.2021.3081189](https://doi.org/10.1109/TIT.2021.3081189); [arXiv:1901.06234](https://arxiv.org/abs/1901.06234); full text read.
- V. K. Amalladinne, J.-F. Chamberland, K. R. Narayanan, *IEEE TIT* 66(10), 2020. DOI [10.1109/TIT.2020.3012948](https://doi.org/10.1109/TIT.2020.3012948); [arXiv:1809.04745](https://arxiv.org/abs/1809.04745). The 4.3 dB-from-achievability figure is from the conference version, via a search summary.
- Y. Polyanskiy, "A perspective on massive random-access," ISIT 2017; benchmark parameters confirmed via Fengler et al.

**Noncoherent and synchronization**

- D. Divsalar, M. K. Simon, "Multiple-symbol differential detection of MPSK," *IEEE TCOM* 38(3), 1990. DOI [10.1109/26.48887](https://doi.org/10.1109/26.48887).
- G. Colavolpe, R. Raheli, "Noncoherent sequence detection," *IEEE TCOM* 47(9), 1999. DOI [10.1109/26.789673](https://doi.org/10.1109/26.789673).
- A. Barbieri, G. Colavolpe, *IEEE TCOM* 2007. DOI [10.1109/TCOMM.2006.887501](https://doi.org/10.1109/TCOMM.2006.887501).
- M. Peleg, S. Shamai, S. Galán, *IEE Proc. Commun.* 2000; catalogue abstract only.
- J. Östman et al., *IEEE TCOM* 67(2), 2019. DOI [10.1109/TCOMM.2018.2874993](https://doi.org/10.1109/TCOMM.2018.2874993); [arXiv:1712.06387](https://arxiv.org/abs/1712.06387).
- B. M. Hochwald, T. L. Marzetta, *IEEE TIT* 46(2), 2000. DOI [10.1109/18.825818](https://doi.org/10.1109/18.825818).
- A. Tchamkerten, V. Chandar, G. W. Wornell, "Asynchronous communication: capacity bounds and suboptimality of training," *IEEE TIT* 59(3), 2013. DOI [10.1109/TIT.2012.2227238](https://doi.org/10.1109/TIT.2012.2227238).

**Search**

- R. van Nee, A. Coenen, *Electronics Letters* 27(2), 1991. DOI [10.1049/el:19910102](https://doi.org/10.1049/el:19910102).
- S. M. Ransom, S. S. Eikenberry, J. Middleditch, *AJ* 124, 2002. DOI [10.1086/342285](https://doi.org/10.1086/342285).
- B. C. Andersen, S. M. Ransom, *ApJL* 863:L13, 2018. DOI [10.3847/2041-8213/aad59f](https://doi.org/10.3847/2041-8213/aad59f).
- K. Adámek et al., [arXiv:1911.01353](https://arxiv.org/abs/1911.01353).
- Enriquez et al. 2017, [arXiv:1709.03491](https://arxiv.org/abs/1709.03491); [turbo_seti](https://github.com/UCBerkeleySETI/turbo_seti).
- S. Peleg, B. Porat, *IEEE TIT* 37(2), 1991. DOI [10.1109/18.75269](https://doi.org/10.1109/18.75269).
- J. P. Costas, *Proc. IEEE* 72(8), 1984. DOI [10.1109/PROC.1984.12967](https://doi.org/10.1109/PROC.1984.12967).

**Doppler spread**

- T. Kailath, *IRE Trans. IT* 6(3), 1960. DOI [10.1109/TIT.1960.1057563](https://doi.org/10.1109/TIT.1960.1057563).
- D. J. Thomson, *Proc. IEEE* 70(9), 1982. DOI [10.1109/PROC.1982.12433](https://doi.org/10.1109/PROC.1982.12433).
- T. Zemen, C. F. Mecklenbräuker, *IEEE TSP* 53(9), 2005. DOI [10.1109/TSP.2005.853104](https://doi.org/10.1109/TSP.2005.853104).

**Neural and quantized decoders**

- T. Gruber et al., [arXiv:1701.07738](https://arxiv.org/abs/1701.07738).
- E. Nachmani et al., Allerton 2016, DOI [10.1109/ALLERTON.2016.7852251](https://doi.org/10.1109/ALLERTON.2016.7852251); *IEEE JSTSP* 2018, DOI [10.1109/JSTSP.2017.2788405](https://doi.org/10.1109/JSTSP.2017.2788405).
- Y. Choukroun, L. Wolf, "Error correction code transformer," NeurIPS 2022. [arXiv:2203.14966](https://arxiv.org/abs/2203.14966).
- S. Cammerer et al., [arXiv:2207.14742](https://arxiv.org/abs/2207.14742).
- A. Buchberger et al., *IEEE JSAC* 39(7), 2021. DOI [10.1109/JSAC.2020.3041392](https://doi.org/10.1109/JSAC.2020.3041392).
- RCQ 4-bit decoders, [arXiv:2104.09480](https://arxiv.org/abs/2104.09480); coarse quantization study, [arXiv:2205.01503](https://arxiv.org/abs/2205.01503). Both from search summaries.
- IS-95 64-ary Walsh/FHT: secondary sources only (patent text and an SDR'05 paper, [PDF](https://www.wirelessinnovation.org/assets/Proceedings/2005/2005-sdr05-2-1-01-chan.pdf)). No required-Eb/N0 figure was found.

**Access limits.** The shared web-search budget was exhausted before these could be checked:

- GPU-specific OSD implementations.
- Exact PAC dB gaps.
- MSDD dB values.
- Low-SNR SPARC finite-length results.
- EbNaut primary documentation.

These are flagged above, not filled in. All [C] values come from exact integrals or Monte Carlo of standard models: AWGN, a single unknown uniform phase, i.i.d. Rayleigh looks and ideal energy bins. They are reference curves, not modem predictions.

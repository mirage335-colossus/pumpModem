# Coherent FSK tone rate, spacing and positions: literature review

27 November 2026 · Delegated literature review for the sub-9 kHz FSK rate and spacing note · web research, sources marked by access level

Original title: *Tone rate, tone spacing and number of positions for coherently received, slightly spread, few-Hz-wide M-FSK: literature report*. The question was how to choose the tone rate, the tone spacing and the number of positions of an M-ary FSK signal that has to be received coherently across many tones, on a nearly phase-stable path, in a band of a few hertz to 100 Hz. The report's own calculations ("my calc") were made with [`coherent_fsk_calc.py`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/coherent_fsk_calc.py) and [`search_grid_calc.py`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/search_grid_calc.py), kept with their outputs in [`literature-calcs/`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/README.md).

**Status.** This is the report of a delegated literature search, as it was returned to the
coordinating session on 27 November 2026. It was written into the repository on 28 November at the
user's request; a first attempt to store it was declined by the permission system, so the
[note it served](../sub9khz-fsk-rate-and-spacing.md) was written first and carries the synthesis.
Research findings, not a qualified design; nothing here is a Matrix Modem measurement.

**What was changed on storing.** This header was added and the report's own title line moved
into it. Paths to temporary files were replaced by a description, and paths to the scratch
programs that were kept now point at their copies. Nothing else was edited: "you" and
"your" address the coordinating session, and "your derivation", "your list", "the design context"
and the lettered conditions refer to the brief, which is reproduced at the end.

**How far it was checked.** All 51 distinct DOIs in this file resolve (checked on 28 November 2026: 50 at Crossref, with the record's authors named beside each citation, and one arXiv DOI at doi.org). The check shows that each DOI exists and belongs to the cited authors, not that the source says what is quoted from it. The search states its own access level for every source; numbers from sources it could not open are marked there.

**Where the note differs.** The note is later and was corrected after an independent check of its
numbers. Where a figure in this report was superseded there (hypothesis counts with the
1/(8 × span) timing rule, the measured decode rates, the block Monte Carlo), the note stands.

---

No repository files were touched. Fetched PDFs were auto-saved by the fetch tool in the session's tool-results directory, and text extracts sit in the session scratchpad.

**Access legend.**
- **[F]** full text read directly (text extraction or page images).
- **[S]** full page read through the fetch tool's summarizer, so numbers are as it reported them.
- **[A]** abstract only.
- **[B]** bibliographic record or search snippet only; no numbers are attributed to these except where explicitly marked as a snippet.
- **[X]** could not access.

**Number labels.** "Exact" means closed-form theory. "My calc" means I computed it here from a stated formula. "Graph" means read off a published figure, about ±0.2 dB or ±10%.

---

## 1. Coherent detection of FSK: spacing, deviation, multi-symbol observation, and relevance under whole-codeword correlation

### 1.1 Orthogonality spacings (exact)

For two equal-energy tones of length T, spacing Δf = h/T, with equal phase at the start of the tone, the complex correlation is ρ = sinc(h)·e^{jπh}.

- Coherent detection uses Re ρ = sin(2πh)/(2πh), which is zero at h = 1/2, 1, 3/2, …
- Envelope detection uses |ρ| = |sinc h|, which is zero only at h = 1, 2, …

So the textbook pair is confirmed: 1/(2T) for coherent orthogonality (MSK-type) and 1/T for non-coherent orthogonality.

The 1/(2T) orthogonality is conditional on phase:

| h | Re ρ | Im ρ | \|ρ\|² |
|---|---|---|---|
| 0.5 | 0 | 0.637 | 0.405 |
| 1.0 | 0 | 0 | 0 |

A 90° phase offset between two adjacent tones on a 1/(2T) lattice turns their correlation from 0 into ±0.64. Coherent orthogonality at half spacing therefore needs a known carrier phase and a fixed (0° or 180°) phase relation between tones.

### 1.2 Optimum deviation, single-symbol coherent binary FSK (exact)

Minimum Re ρ = −0.217 at h = 0.715, i.e. Δf = 0.358·R.

- Gain over orthogonal FSK: 10·log₁₀(1.217) = 0.85 dB.
- Still 2.16 dB worse than antipodal PSK.

Source: NASA, "Performance of Binary FSK Data Transmission Systems", N77-23289 (NTRS 19770016345) [F, OCR; author not legible]. It states "(2Δf)T = 0.715", "ρ = −0.22", "only 2.2 dB worse than coherent PSK" and "a 0.8-dB improvement over … coherent detection of orthogonal FSK". My recomputation agrees (h = 0.7151, ρ = −0.2172).

### 1.3 Multi-symbol observation of CPFSK

- **Osborne & Luntz 1974**, IEEE Trans. Commun. 22(8):1023–1036, DOI 10.1109/TCOM.1974.1092333 [A]. The abstract covers coherent and non-coherent detection with arbitrary index and observation interval, and says non-coherent multi-symbol CPFSK "can be better than the performance of coherent PSK". It gives no numbers.
- **Schonhoff 1976**, IEEE Trans. Commun. 24(6):644–652, DOI 10.1109/TCOM.1976.1093344 [A]. Quaternary coherent CPFSK with a five-symbol decision beats coherent QPSK by 3.5 dB. Octal with a three-symbol decision beats octal orthogonal by 2.6 dB. Both are high-SNR estimates from error-probability upper bounds, checked by Monte Carlo.
- **Aulin & Sundberg 1981** (Part I, DOI 10.1109/TCOM.1981.1095001; Part II with Rydbeck, DOI 10.1109/TCOM.1981.1094985) [B]. **Anderson, Aulin & Sundberg**, *Digital Phase Modulation*, 1986, DOI 10.1007/978-1-4899-2031-7 [X]. **Sundberg**, "Continuous phase modulation", IEEE Commun. Mag. 24(4):25–38, 1986, DOI 10.1109/MCOM.1986.1093063 [F, partial OCR].
- The standard binary full-response distance bound is d²(h) = 2(1 − sin 2πh / 2πh). It peaks at 2.43 for h = 0.715, which is +0.85 dB over MSK/BPSK (d² = 2) [my calc from the standard formula; I did not confirm the "three symbols needed" figure in an opened source].

The same h = 0.715 and the same 0.85 dB appear twice with different baselines. Single-symbol detection is 0.85 dB better than orthogonal FSK. Multi-symbol (phase-trellis) detection is 0.85 dB better than antipodal.

### 1.4 Does this matter with a random code and exhaustive codeword correlation? Mostly no, with two refinements

**(a) The CPM gains are high-SNR minimum-distance gains from phase memory.** On the lattice in the design context (Δf = 1/T, whole cycles per tone) every tone returns to the same phase. There is no phase memory and the trellis gain is zero by construction. The code supplies all the memory.

**(b) Your expectation is confirmed.** With coherent maximum-likelihood correlation of whole codewords, the pairwise distance is d² = 2E(1 − Re ρ_codewords). Only the real cross-correlation between tone waveforms enters, including the per-tone phases.

In the power-limited regime the first-order capacity depends on energy, not on dimensionality or orthogonality, provided the signalling has no wasted mean component. This is the framework of Verdú 2002, "Spectral efficiency in the wideband regime", IEEE Trans. IT 48(6):1319–1343, DOI 10.1109/TIT.2002.1003824 [A]: minimum Eb/N0 is the first-order quantity and the wideband slope is the second-order one. The "zero-mean inputs are first-order optimal" statement is my recollection of the paper, not text I opened.

Here the SNR per Hz·s is C/(N0·B) ≈ −20 dB or lower. The second-order (bandwidth) term is of order SNR/2, which is under 0.1 dB. Non-orthogonal spacing below 1/T therefore costs coherent reception essentially nothing at capacity. What it changes is:
- the spread of codeword cross-correlations in a short block;
- the per-tone energy-detection fallback, where adjacent-tone leakage is |ρ|² = 0.405 at h = 0.5 and 0.81 at h = 0.25 [my calc].

**(c) Coded CPFSK sources.** All are abstract-level only, so I attribute no numbers.
- Cheng, Valenti & Torrieri, "Coherent continuous-phase frequency-shift keying: parameter optimization and code design", IEEE Trans. Wireless Commun. 8(4):1792–1802, 2009, DOI 10.1109/TWC.2009.071311 [B]. It jointly optimizes modulation index and code rate under a 99%-bandwidth constraint.
- Seshadri, Cheng & Valenti, VTC-Fall 2007, DOI 10.1109/VETECF.2007.265 [A].
- Cheng, Seshadri, Valenti & Torrieri, "The capacity of noncoherent continuous-phase frequency shift keying", CISS 2007, DOI 10.1109/CISS.2007.4298336 [A]. Non-orthogonal modulation with multiple tones helps most when bandwidth is tight.
- Valenti, Cheng & Torrieri, "Iterative multisymbol noncoherent reception of coded CPFSK", MILCOM 2009, DOI 10.1109/MILCOM.2009.5380088 [B, snippet]. The index need not be rational, and the oscillator need only be stable over a small group of symbols.
- Torrieri, Cheng & Valenti, IEEE Trans. Commun. 56(8):1343–1351, 2008, DOI 10.1109/TCOMM.2008.060549 [B].
- Cheng's dissertation, DOI 10.33915/etd.8617 [X, 403].

These papers show that under a bandwidth constraint the information-theoretic optimum for coded FSK is generally not h = 1. I could not open the tables giving the optimal h.

---

## 2. Capacity and cutoff rate of M-ary orthogonal signalling: coherent versus non-coherent

### 2.1 Coherent M-ary orthogonal (exact brick-wall limits)

Source: Dolinar, Divsalar, Hamkins & Pollara, "Capacity of Pulse-Position Modulation (PPM) on Gaussian and Webb Channels", JPL TMO Progress Report 42-142, 15 Aug 2000 [F].

- Soft-decision coherent M-ary orthogonal signalling on AWGN needs Eb/N0 > (M ln 2)/(M − 1).
- The simplex set achieves the same capacity with a factor M/(M − 1) less energy.
- "Each M-ary orthogonal signal has an associated Eb/N0 penalty of 10 log₁₀ M/(M−1) dB."
- The report prints +3.01 dB (2-PPM), +1.25 dB (4), +0.28 dB (16) and +0.02 dB (256).
- It cites MacMullan & Collins, "The capacity of orthogonal and bi-orthogonal codes on the Gaussian channel", IEEE Trans. IT 44(3):1217–1232, 1998, DOI 10.1109/18.669291 [B].
- It states soft-output channels have about a 3 dB advantage over hard-output ones.

| M | Simplex-to-orthogonal penalty (exact) | Minimum Eb/N0, coherent orthogonal |
|---|---|---|
| 2 | 3.01 dB | +1.42 dB |
| 4 | 1.25 dB | −0.34 dB |
| 8 | 0.58 dB | −1.01 dB |
| 16 | 0.28 dB | −1.31 dB |
| 64 | 0.07 dB | −1.52 dB |
| 256 | 0.02 dB | −1.57 dB |
| antipodal / simplex / unconstrained | 0 | −1.59 dB |

The penalty is a centroid effect. A random M-ary tone sequence correlates with another by 1/M on average, so 1/M of the energy is a common component that carries no information. It disappears if coinciding tones are not always in phase.

**Cutoff rate.** The textbook coherent result is R0 = log₂[M / (1 + (M−1)e^{−Es/2N0})]. I did not re-open Wozencraft & Jacobs, Viterbi & Omura or Proakis [X]. Its low-rate limit is Eb/N0 → 2 ln2 · M/(M−1), which gives 4.43, 2.67, 2.00 and 1.70 dB for M = 2, 4, 8, 16, against 1.42 dB for antipodal [my calc]. The same M/(M−1) ladder applies.

### 2.2 Non-coherent M-ary orthogonal

Source: Butman & Levitt, "Capacity for Noncoherent, Soft-Decision MFSK Signaling", JPL TR 32-1526 Vol. XV, pp. 146–155, 15 June 1973 [F, pages 1–8 as images]. It plots C_M/C∞ against per-tone pre-detection SNR ST/N0, where C∞ = (S/N0)·log₂e is the infinite-bandwidth coherent capacity.

| M | Peak C_M/C∞ (graph) | At ST/N0 (graph) | Loss vs coherent limit |
|---|---|---|---|
| 2 | ≈ 0.145 | ≈ 2–3 | ≈ 8.4 dB |
| 4 | ≈ 0.26 | ≈ 3 | ≈ 5.8 dB |
| 8 | ≈ 0.36 | ≈ 3–4 | ≈ 4.4 dB |
| 16 | ≈ 0.41 | ≈ 4 | ≈ 3.9 dB |

- For M → ∞ the ratio rises monotonically to 1.
- At ST/N0 = 0.1 the curves read ≈ 0.022 (M = 2) and ≈ 0.045 (M → ∞).
- The text gives 0.109 for a binary single-observable receiver with 4 quantization levels at ST/N0 = 1.
- 8-level quantization is essentially as good as unquantized.

These readings match the small-signal law I derived: per tone, I ≈ ((M−1)/2M)·(Es/N0)² nats, so C_nc/C_coh ≈ ((M−1)/2M)·Es/N0 for Es/N0 ≪ 1 [my derivation].

Other sources:
- Bar-David & Butman, "Performance of Coded, Noncoherent, Hard-Decision MFSK Systems", JPL TR 32-1526 Vol. XIII, pp. 82–91, 1973 [A]. With hard decisions the optimum number of frequencies is never below 7, and binary FSK is at least 2.2 dB worse than 7 signals. Cutoff-rate results are similar.
- Butman, Bar-David, Levitt, Lyon & Klass, IEEE Trans. Commun. 24(10):1078–1088, 1976, DOI 10.1109/TCOM.1976.1093213 [A]. Capacity and cutoff rate against pre-detection SNR and M "determine the best signal duration and signal number" given power, bandwidth, N0 and coherence time. This is the classic statement of the problem posed here.
- Stark 1985, IEEE Trans. Commun. 33(11):1153–1159, DOI 10.1109/TCOM.1985.1096232 [A]. Optimal code rates exist that minimize the required SNR for non-coherent FSK. No numbers are in the abstract.
- Valenti & Cheng, IEEE JSAC 23(9):1739–1747, 2005, DOI 10.1109/JSAC.2005.853794 [X]. Guillén i Fàbregas & Grant, IEEE Trans. Wireless Commun. 6(11):4004–4013, 2007, DOI 10.1109/TWC.2007.06040154 [X, 403]. Massey 1981, DOI 10.1109/TCOM.1981.1094916 [B].

### 2.3 What this means for the number of tone positions

- **Coherent rungs.** Extra positions buy only the M/(M−1) centroid term (3.0, 1.25, 0.58, 0.28 dB) plus finite-block distance effects. The centroid term can be removed without any extra positions (see 7(ii)-1).
- **Energy-detection rungs.** M matters a lot: 8.4, 5.8, 4.4 and 3.9 dB below the coherent limit at best, and only when each detected piece has E/N0 ≈ 3–6 dB.

### 2.4 Short-block bounds, to calibrate 10–24-bit messages

Source: Dolinar, Divsalar & Pollara, "Code Performance as a Function of Block Size", TMO PR 42-133, 15 May 1998 [F, text plus Figs 7–9]. Shannon's sphere-packing bound for the unconstrained AWGN channel at word error rate 10⁻⁴, as the rate goes to zero (all graph readings):

| k (bits) | Sphere-packing lower bound | Continuous-input random-coding bound | Gallager bound, binary-input random codes |
|---|---|---|---|
| 16 | ≈ 2.9 dB | ≈ 3.3 dB | ≈ 4.0 dB |
| 32 | ≈ 1.9 dB | ≈ 2.1 dB | ≈ 2.7 dB |

- Rule of thumb from the text: k·(ΔSNR_dB)² ≈ 1000 at 10⁻⁴, accurate only for k above about 1000.
- For k = 10–24, a pseudo-random code with exhaustive ML decoding is expected to sit within a few tenths of a dB of the best possible code. Block length, not the modulation alphabet, is then the dominant gap to −1.59 dB.

---

## 3. Partially coherent reception: coherent window against coherence time

### 3.1 Block and multiple-symbol detection

- Divsalar, Simon & Shahshahani, "Multiple Symbol Differential Detection of Uncoded and Trellis Coded MPSK", JPL Publication 89-38, 15 Nov 1989 [F, partial]. Journal version: Divsalar & Simon, IEEE Trans. Commun. 38(3):300–308, 1990, DOI 10.1109/26.48887 [B]. Maximum-likelihood sequence detection over an N-symbol window "fills the gap" between 2-symbol differential and ideal coherent detection. The coherent-to-DPSK gaps quoted in the introduction are about 0.75 dB (binary), 2.2 dB (QPSK, Pb = 10⁻⁵) and over 2.5 dB (8PSK). These are high-SNR, uncoded results.
- Simon & Divsalar, "Maximum-likelihood block detection of noncoherent continuous phase modulation", IEEE Trans. Commun. 41(1):90–98, 1993, DOI 10.1109/26.212365 [B].
- Simon & Divsalar, "Multiple symbol partially coherent detection of MPSK", IEEE Trans. Commun. 42:430–439, 1994, DOI 10.1109/TCOMM.1994.577070 [B]. A search snippet of the TDA 42-110 version says a longer observation keeps improving performance but does not remove the partially-coherent error floor.
- Colavolpe & Raheli, "Noncoherent sequence detection", IEEE Trans. Commun. 47(9):1376–1385, 1999, DOI 10.1109/26.789673, and "…of continuous phase modulations", 47(9):1303–1307, DOI 10.1109/26.789664 [B].
- Peleg & Shamai, "On the capacity of the blockwise incoherent MPSK channel", IEEE Trans. Commun. 46(5):603–609, 1998, DOI 10.1109/26.668728 [B, abstract snippet]. With phase constant over L symbols, i.i.d. uniform MPSK is capacity-achieving and coherent capacity is approached as L grows.
- Nuriyev & Anastasopoulos, IEEE Trans. IT 51(3):866–883, 2005, DOI 10.1109/TIT.2004.842569 [B].
- Peleg, Shamai & Galán, IEE Proc. Commun. 147:87, 2000, DOI 10.1049/ip-com:20000175 [B].

**A working precedent for a ladder of coherent windows (FST4).** The WSJT-X FST4 decoder source (`get_fst4_bitmetrics.f90`, `fst4_decode.f90`, read from a GitHub mirror [S]) does this:
- It forms complex per-symbol tone correlations and combines them coherently over blocks of 1, 2, 4 and 8 symbols (4, 16, 256 and 65,536 sequences).
- It then tries the four metric sets in turn.
- Frequency search: 0.1 baud coarse, then 0.02 baud fine.
- Timing search: 4 downsampled samples coarse, then 1 sample fine, at 36–40 samples per symbol.

The FST4 Quick-Start Guide (Franke, Somerville & Taylor) [F] attributes the gain to this: "FST4-60 is about 1.7 dB more sensitive than JT9, largely because it uses multi-symbol block detection where appropriate" (AWGN simulation).

### 3.2 Loss against window length ÷ coherence time (my calc, standard second-moment formulas)

Mean coherent power gain of an integrate-and-dump of length T over a unit-power channel process with autocorrelation R(τ): G = (2/T²)∫₀ᵀ (T−τ)R(τ)dτ.

**Wiener phase (Lorentzian line, full width Δν):** G = 2(a − 1 + e^{−a})/a² with a = πΔνT.

| Δν·T | 0.05 | 0.1 | 0.2 | 0.3 | 0.5 | 1 | 2 |
|---|---|---|---|---|---|---|---|
| Loss (dB) | 0.22 | 0.44 | 0.86 | 1.26 | 2.0 | 3.5 | 5.7 |

**Gaussian Doppler spectrum (rms σ; the ITU/Watterson "Doppler spread" is 2σ):** G ≈ 1 − (πσT)²/3 for small σT.

| σ·T | 0.1 | 0.2 | 0.3 | 0.5 | 0.75 |
|---|---|---|---|---|---|
| Loss (dB) | 0.14 | 0.52 | 1.06 | 2.25 | 3.55 |

**Consistency with FST4 practice.** The Quick-Start says "decoding requires Doppler spreads less than the submode tone spacing. Sensitivity is best when Doppler spread is no more than 1/8 of the tone spacing" [F].
- Spread × T = 1/8 costs about 0.05 dB per symbol.
- Over the longest 8-symbol coherent block it costs about 2 dB.
- So the 1/8 rule is where the top rung of their ladder stops paying.

### 3.3 Optimum coherent window for a given linewidth and C/N0 (my calc)

Criterion: deflection of a sum of K = T_total/T squared window correlations, SNR_out ∝ K·(Gρ)²/(1 + 2Gρ), with ρ = (C/N0)·T. This is an illustrative proxy, not a published table.

| C/N0 (dB-Hz) | Linewidth | T_opt | Δν·T_opt | Window SNR | Efficiency vs fully coherent |
|---|---|---|---|---|---|
| −10 | 1 mHz | 68 s | 0.07 | +8.0 dB | −0.6 dB |
| −10 | 10 mHz | 20 s | 0.20 | +2.1 dB | −2.0 dB |
| −20 | 1 mHz | 202 s | 0.20 | +2.2 dB | −2.0 dB |
| −20 | 3 mHz | 105 s | 0.31 | −1.1 dB | −3.5 dB |
| −20 | 10 mHz | 47 s | 0.47 | −5.2 dB | −6.1 dB |
| −30 | 1 mHz | 473 s | 0.47 | −5.2 dB | −6.1 dB |
| −30 | 3 mHz | 194 s | 0.58 | −9.4 dB | −9.6 dB |
| −30 | 10 mHz | 65 s | 0.65 | −14.4 dB | −14.2 dB |

Two conclusions follow.

1. **The best window is not a fixed fraction of the coherence time.** It is about 0.1–0.2/Δν when the window SNR is well above 0 dB, and it stretches to about 0.6–0.7/Δν (accepting 2–2.5 dB of decorrelation) when the window SNR is below 0 dB. A ladder of window lengths is therefore the right structure.

2. **The governing number is (C/N0)·T_coherence.** At about 1 or more, non-coherent combining of coherent windows is within about 2 dB of coherent. Below about 0.1 the loss grows as 1/(window SNR), so each further dB of weakness costs 2 dB of integration time. This is the square-law small-signal suppression of section 2.2.

With millihertz spread, efficient reception stops near C/N0 ≈ −20 to −25 dB-Hz. At −40 dB-Hz the receiver needs coherence over about 10⁴ s or it pays tens of dB.

Published systems sit at the same working point (my arithmetic from their published thresholds):
- FST4W, all lengths: Es/N0 per symbol ≈ −0.4 dB, with Eb/N0 ≈ +4.6 dB at threshold.
- WSPR: ≈ +1.3 dB per symbol.
- MER EDL tones: ≈ −0.5 dB per 0.1 s coherent look.
- Q65 and JT9, with larger alphabets: ≈ +5.6 dB per symbol.

### 3.4 Phase-noise capacity results (mostly high-SNR; limited help here)

- Katz & Shamai, IEEE Trans. IT 50(10):2257–2270, 2004, DOI 10.1109/TIT.2004.834745 [B, abstract snippet]. For the memoryless non-coherent AWGN channel the capacity-achieving input is discrete with infinitely many mass points. Tight lower bounds are computed. I could not read the partially coherent (Tikhonov) part.
- Lapidoth, ITW 2002, DOI 10.1109/ITW.2002.1115399 [B]. Barbieri & Colavolpe, IEEE Trans. Commun. 59:3223–3228, 2011, DOI 10.1109/TCOMM.2011.082011.100217 [B]. Khanzadi, Durisi & Eriksson, DOI 10.1109/TCOMM.2015.2408605 [B].
- Khanzadi, Krishnan, Söder & Eriksson, "On the capacity of the Wiener phase-noise channel…", IEEE Trans. Commun. 63:4174–4184, 2015, DOI 10.1109/TCOMM.2015.2465389 [A]. The bounds "follow the well-known AWGN capacity curve" at low SNR. This holds for symbol-rate sampling with small per-symbol phase innovation.
- Ghozlan & Kramer, IEEE Trans. IT 63(4):2376–2393, 2017, DOI 10.1109/TIT.2017.2662698 [A]. Filtering of the phase noise within a symbol matters, and multi-sample (oversampling) receivers raise rates at both strong and weak phase noise.
- Barletta & Rini, "On the capacity of the oversampled Wiener phase noise channel", arXiv:2001.07485 [A]. It gives inner bounds from coherent and from non-coherent combining of the oversamples, and three regimes: AWGN-like, non-coherent-like, and one in which "partially-coherent combining is asymptotically optimal". This is the closest theoretical statement supporting sub-tone and multi-tone coherent windows followed by energy combining.
- Zheng, Tse & Médard, "Channel coherence in the low-SNR regime", IEEE Trans. IT 53(3):976–997, 2007, DOI 10.1109/TIT.2006.890777 [B, abstract snippet]. Capacity is linear in SNR with or without receiver channel knowledge, and the sub-linear term is set by a "coherence level" relating SNR to coherence time.
- Optical FSK with laser phase noise is the classical exact treatment of filter-then-envelope-detect-then-post-integrate. Foschini, Greenstein & Vannucci, IEEE Trans. Commun. 36(3):306–314, 1988, DOI 10.1109/26.1456 [B, snippet]: with the IF bandwidth optimized, large phase noise costs only a minor SNR increase. Greenstein, Vannucci & Foschini, 37(4):405–407, 1989, DOI 10.1109/26.20125 [B]. Azizoglu & Humblet, J. Lightwave Technol. 9(11):1398–1410, 1991, DOI 10.1109/50.90938 [X, 403; snippet says phase noise "may necessitate a wider filter bandwidth, or equivalently smaller integration times"].

---

## 4. Practice

### 4.1 Navy and NATO VLF/LF

**MIL-STD-188-140A** (US interoperability standard referencing STANAG 5030; STANAG itself [X, restricted]) [F, scanned pages 13–15, 23–31]:

- **Rates.** VLF modulation rates are 5 Bd, 50×2ⁿ Bd or 75×2ⁿ Bd.
- **Why not PSK.** PSK "is not presently used at VLF/LF because of the transients caused by phase discontinuities", which can damage the final amplifiers or cause a transmitter "kickdown".
- **FSK.** The same phase-discontinuity restriction applies. Phase-continuous FSK needs an integer modulation index and equal mark/space phase at the transition. "The requirement to make the best possible use of bandwidth in VLF and LF results in the minimum modulation index of 1.0 being used for FSK." At 50 Bd this is a 50 Hz total shift (±25 Hz), by the standard's own definition.
- **Index 0.5.** Conventional FSK at 0.5 has a serious discontinuity every other transition. Compatible Shift Keying (CSK) and MSK give index 0.5 "with acceptable phase continuity and with half the RF bandwidth". CSK does it by selecting among mark, space and their 180°-shifted versions at each transition.
- **Necessary bandwidth.** ≤ 2.4 × baud at index 1.0, and ≤ 1.2 × baud at index 0.5.
- **Tolerances.** Amplitude response ≤ 1 dB and envelope-delay distortion ≤ 50 µs over the necessary bandwidth. Phase jitter ≤ 3° for coherent systems. Frequency accuracy 1 part in 10⁹.

**Woznak, "PTTI relating to the VERDIN communications system", 1971** [F]:
- VERDIN covers 14–60 kHz with one, two or four 50-baud channels.
- Modes are CW, FSK, CSK ("which can be detected either for phase or frequency") and MSK as primary.
- Without a time standard the receiver would have to search about ±1 s, which was judged to make the system "economically unfeasible". Rubidium (later caesium) standards were therefore built in.

**FAS, Navy VLF page** [S]: 50-baud broadcast; VERDIN multiplexes up to four 50 bps channels.

**Reasoning.** The spacing-to-rate ratio is driven to the minimum (h = 1, then h = 0.5 with engineered phase continuity) by antenna bandwidth and by transmitter intolerance of phase steps. Coherent detection is bought with atomic time and frequency, not with a search.

### 4.2 ELF (Sanguine, Seafarer, Project ELF)

- **Bernstein, McNeill & Richer**, IEEE Trans. Commun. 22(4):508–528, 1974, DOI 10.1109/TCOM.1974.1092208 [A]. Convolutional encoding with binary antipodal modulation. The receiver used adaptive nonlinear noise processing, notch filtering, ocean-channel compensation and sequential decoding, and was tested in real time on a submerged submarine.
- **Bernstein et al.**, "Long-range communications at extremely low frequencies", Proc. IEEE 62(3):292–312, 1974, DOI 10.1109/PROC.1974.9426 [B].
- **MIT Lincoln Laboratory history sidebar** [F]. The 1972 Tinosa test used "a low data rate (0.03 bps)" and "a test transmitter that radiated less than 1 W", with "a binary minimum shift-keying bandspread technique on a 76 Hz carrier" for a twenty-character message. The signal "applied minimum FSK modulation to binary convolutional coding". Nonlinear noise processing and coding allowed about a 100-fold reduction in transmitter power.
- **Altgelt, "The world's largest radio station"** [F, secondary]. The operational signal shifts between 72 Hz and 80 Hz around 76 Hz. An 8 Hz shift at index 0.5 implies 16 chips/s; this is my inference, and the "16 Hz" appeared only in a DTIC search snippet [X, 403]. The same document quotes the "few characters every 15 minutes" figure from congressional testimony.

**Lesson.** The closest precedent for coherent reception through a tiny-bandwidth antenna used MSK-type (h = 1/2) signalling detected as antipodal. It did not use orthogonal h = 1 tones.

### 4.3 Amateur weak-signal modes

**Symbol length, spacing and threshold** (WSJT-X protocol table [S]; FST4 and Q65 Quick-Start guides [F]; thresholds are AWGN simulations in 2500 Hz):

| Mode | Modulation | Baud = spacing (Hz) | Sync energy | Threshold |
|---|---|---|---|---|
| FST4W-120 | 4-GFSK, LDPC (240,74) | 1.46 | 25% | −32.8 dB |
| FST4W-300 | same | 0.558 | 25% | −36.8 dB |
| FST4W-900 | same | 0.180 | 25% | −41.7 dB |
| FST4W-1800 | same | 0.089 | 25% | −44.8 dB |
| WSPR | 4-FSK, K=32 r=1/2 | 1.465 | 50% | −31 dB |
| JT9A | 9-FSK | 1.736 | 19% | −26 dB |
| JT65A | 65-FSK, RS(63,12) | 2.692 | 50% | −25 dB |
| Q65-60A | 65-FSK, QRA (63,13) | 1.667 | 26% | −27.6 dB |
| Q65-300A | same | 0.289 | 26% | −33.8 dB |

- FST4 symbol lengths run from 0.060 s (15 s period) to 11.2 s (1800 s period). FST4W covers 0.683–11.2 s.
- Every WSJT mode uses spacing equal to baud (h = 1) in its narrowest form. None uses h < 1.

**Spacing-to-rate ratios above 1 exist only for spread channels:**
- JT4 A–G: 4.375 to 315 Hz spacing at 4.375 baud, with the threshold falling from −23 to −17 dB.
- JT9 B–H: 3.47 to 222 Hz.
- Q65 A–E: 1, 2, 4, 8, 16 × baud.
- The user guide says the wide-submode thresholds apply "when spreading is comparable to the tone spacing" [S].

**Q65 Quick-Start** (Taylor, Somerville, Franke & Palermo, 2021) [F]:
- "On paths with Doppler spread more than a few Hz, the weak-signal performance of Q65 is the best among all WSJT-X modes."
- "Decoding is effective for signals with Doppler spread up to ten times the tone spacing, and even beyond."
- Recommended pairings include 30A for 6 m ionoscatter, 60D for 10 GHz rain and troposcatter, 120E for small-dish 10/24 GHz EME, and 60A to 60D for EME from 50 MHz to 10 GHz.
- Fig. 1 shows the threshold of 60D and 120E rising by several dB as spread goes from 0 to 100 Hz (vertical axis −21 to −30 dB; I did not extract curve values).

**FST4 Quick-Start** [F]:
- Requires that "oscillator drifts and path-induced Doppler shifts must be less than the tone spacing, over the full sequence length".
- Notes FST4 "do not require the strict, independent time synchronization and phase locking of modes like EbNaut".

**Griffiths, Elmore, Robinett, Rhymes & Watrous, "FST4W on the HF Bands", TAPR DCC 2022** [F, first pages]:
- Sensitivity rises with sequence length while tolerance to spectral spread falls.
- At 2 Hz spread FST4W-120 decodes at −24 dB where WSPR needs −17 dB.
- At 3 Hz WSPR does not decode at any SNR.
- The 1800 s mode is usable on upper HF only for ground wave with GPS-disciplined oscillators at both ends.
- Measured single-tone spectral widths are 5.3, 2.14, 0.74 and 0.35 mHz for −120 to −1800.

**EbNaut** (Nicholson). The primary site abelian.org was unreachable (DNS failure) [X]. Secondary sources:
- DL4YHF receiver notes [S]: coherent BPSK. Examples are 25 characters at 8 s per symbol over 24,320 s (6.75 h) on 8822 Hz, and 9 characters at 0.5 s per symbol over 352 s on 137.78 kHz. The reference-phase search uses 30° steps with at most 0.3 dB loss. A 15° phase-error budget implies 1.7 µHz at VLF and about 117 µHz at LF.
- IZ7SLZ notes [F, partial]: a typical announcement reads "137495 Hz, 18 Char., CRC16, 8K19A, Sym=0.5 s". LF frequency accuracy needs to be about 0.1 mHz (10⁻⁹).
- PA3FWM technote [S]: the mode needs "a propagation path of which the phase shift does not change too much during the entire transmission", which limits it to LF and VLF. Skipping the search is "a conscious design choice". A VLF coherent-BPSK experiment achieved Eb/N0 ≈ −1 dB at 0.0058 bit/s (−57 dB in 2500 Hz, about −23 dB-Hz), against +5 dB for WSPR and JT65.

In EbNaut each symbol sits at about −14 dB Es/N0. Symbol length is decoupled from SNR and is set by bandwidth, timing and stability.

**WOLF** (KK7KA; text mirrored by DL4YHF) [S]:
- BPSK at 10 bit/s; 80 data bits coded at rate 1/6 with a K = 15 tail-biting convolutional code, giving 480 bits.
- These are interleaved 1:1 with a known PN reference, giving 960 bits per 96 s frame. The reference takes half the energy.
- Frames are integrated coherently.
- Drift of 3 mHz/min costs about 3 dB, and 10 mHz/min is probably fatal.
- Rationale: amplifiers are peak-limited, so a single constant carrier is used. FSK is better when the path or equipment is unstable, and PSK is better on stable LF paths. The author suggests an 8- or 16-tone version if that proves wrong.

**Not found in primary form:** JT9-2/-5/-10/-30 parameters; WSPR-15 (0.183 Hz spacing, snippet only); Opera (closed source, no specification found); Jason; DFCW/QRSS; "Slow JT9".

### 4.4 Deep-space semaphore tones

**Satorius, Estabrook, Wilson & Fort, "Direct-to-Earth Communications and Signal Processing for Mars Exploration Rover Entry, Descent, and Landing", IPN Progress Report 42-153, 15 May 2003** [F]:

- **Signal.** One of 256 tones every 10 s (8 bits per 10 s), 20 s optional on the bridle, with 100 Hz separation between tones. Spacing × T = 1000.
- **Why MFSK.** "High dynamics and low signal-to-noise ratio … will not reliably support phase-coherent communications." A type-III loop would need about 13 Hz bandwidth and 22 dB-Hz of carrier.
- **Dynamics.** Doppler rate up to 700–1200 Hz/s; second derivative −25 to +40 Hz/s².
- **Detector.** The coherent FFT length is matched to the dynamics, with non-coherent averaging over the rest of the symbol. FFT resolution is 10 Hz (0.1 s coherent, 100 looks per 10 s tone) during entry, and 1 Hz with T = 10–30 s after landing.
- **Search grids.** Frequency-rate steps are 1.25–5 Hz/s when tracking and 10–80 Hz/s in acquisition, down to 0.02 Hz/s when landed.
- **Performance.** 99% tone detection at total P/N0 ≈ 13 dB-Hz (theory), with half the power in a residual carrier. Carrier acquisition needs about 19–22.5 dB-Hz.
- **Cost of the search.** The tracking threshold is "approximately 2 to 3 dB lower" than acquisition purely "due to the reduction in the size of the search space".
- **Tests.** Tone and carrier C/N0 estimates down to 5.4–7.8 dB-Hz (measurement).

Later missions:
- Soriano et al., IEEE Aerospace 2013, DOI 10.1109/AERO.2013.6496816 [A]. Same MFSK approach for MSL.
- Mauger et al., "Direct to Earth Communications Using MFSK Tones during M2020 Entry, Descent, and Landing", IEEE Aerospace 2022, DOI 10.1109/AERO53065.2022.9843672 [B].

---

## 5. FSK through a band-limited, high-Q antenna

No source I could open gives a rule for total FSK span ÷ −3 dB bandwidth. What exists:

**(a) MIL-STD-188-140A** [F]: ≤ 1 dB amplitude variation and ≤ 50 µs delay distortion over the necessary bandwidth (1.2 × baud for h = 0.5, 2.4 × baud for h = 1), plus the ban on phase discontinuities.

**(b) Synchronous antenna retuning.**
- Wolff, "High-speed frequency-shift keying of LF and VLF radio circuits", IRE Trans. Commun. Syst. 5(3):29–42, 1957, DOI 10.1109/TCOM.1957.1097513 [B, abstract snippet]. Varying antenna reactance in synchronism with the FSK raises speed from about 20 wpm to 100 wpm or more.
- Navy "Electron" article "High-speed keying of VLF radio circuits" [F]. Stored energy is "several hundred times" the radiated energy. On-off keying at 15 kc is limited to about 18 wpm. With synchronous reactance keying (28.5 kc, 100 c/s shift, 10 kW) 60-wpm teletype and up to 460-wpm Morse were achieved, with the mark-to-space transition "within one r-f cycle".
- Kemp et al., "A high Q piezoelectric resonator as a portable VLF transmitter", Nature Commun. 10:1715, 2019, DOI 10.1038/s41467-019-09680-2 [S]. Q ≈ 3×10⁵ gives a passive bandwidth of 84 mHz at 35.5 kHz. FSK with 7 Hz separation was demonstrated at keying rates from 0.1 to 50 Hz by direct antenna modulation, more than 83 times the passive Bode-Fano limit. The time-varying resonator is not bound by the linear time-invariant limit.

**(c) Single-tuned filter numbers (my calc).** For −3 dB bandwidth B, the time constant is τ = 1/(πB).

| Offset from centre | Amplitude | Phase | Group delay |
|---|---|---|---|
| 0 | 0 dB | 0° | τ |
| ±0.1B | −0.17 dB | ∓11° | 0.96τ |
| ±0.25B | −0.97 dB | ∓27° | 0.80τ |
| ±0.5B | −3.0 dB | ∓45° | 0.50τ |

- The static per-tone phases must be in the coherent template. A 45° error is a 3 dB coherent loss.
- The group delay acts as a tone-dependent timing offset of up to τ/2 across the band.
- A single-tuned antenna meets the MIL-STD 1 dB flatness only over the central 0.51B. Read literally, that implies necessary bandwidth ≤ about half the antenna's −3 dB bandwidth unless equalized (my inference).

**Transition transient**, as a fraction of the tone's correlation amplitude:

| Transition | Amplitude loss |
|---|---|
| Phase-continuous step between tones Δf apart | (τ/T)·x²/(1+x²), x = 2Δf/B |
| 90° phase step | τ/T = 1/(πBT) |
| 180° phase step | 2τ/T |

- For the continuous-phase step the fraction of τ/T lost is 4% at Δf = 0.1B, 20% at 0.25B and 50% at 0.5B.
- In power, a 90° step costs 0.57, 0.28, 0.14 and 0.03 dB at B·T = 5, 10, 20 and 100. A 180° step costs 1.18, 0.57, 0.28 and 0.06 dB.

Your "about 1/(π·B·T)" is the amplitude loss for a quarter-turn step. The power loss is twice that, a reversal doubles it again, and continuous-phase steps between closely spaced tones are second-order small. The energy is not all lost: it appears as a tail in the next tone that a template including the antenna response would recover.

---

## 6. Recent work (2019–2026)

**Short packets with unknown phase.**
- Östman, Durisi, Ström, Coşkun & Liva, IEEE Trans. Commun. 67(2):1521–1536, 2019, DOI 10.1109/TCOMM.2018.2874993 [F, arXiv 1712.06387, key passages]. "Noncoherent transmission is more energy efficient than PAT, even when the number of pilot symbols and their power is optimized." For 168 symbols at rate 0.48 with 8-symbol blocks, pilot-assisted transmission needs 1.2 dB more Eb/N0 at 10⁻³, and a further 0.5–0.6 dB if pilots and data have equal power.
- Ferro, Schiavone, Liva & Magarini, "A decoding algorithm for terminated convolutional codes over the blockwise noncoherent channel", ISTC 2025, DOI 10.1109/ISTC65386.2025.11154494 [F, abstract and results]. Pilot-free decoding with code-aided phase estimation gains about 0.5 dB over pilots and comes within 0.3 dB (BPSK) and 0.6 dB (QPSK) of the finite-blocklength achievability bound for an unknown block phase.
- Bibliographic only [B]: Polyanskiy, Poor & Verdú, DOI 10.1109/TIT.2010.2043769; Lancho et al., DOI 10.1109/TWC.2020.2987573; Durisi, Koch & Popovski, DOI 10.1109/JPROC.2016.2537298; Coşkun et al., DOI 10.1016/j.phycom.2019.03.004.
- Implication: sending the message with no pilot and letting the exhaustive search resolve phase per window is the approach the bounds favour.

**Oversampled Wiener phase-noise channel:** Barletta & Rini 2020 (section 3.4).

**High-Q transmitters:** Kemp et al. 2019 (section 5).

**LoRa-like chirp versus FSK** [B, snippets only]. arXiv 1906.04256 and 1905.11252 treat LoRa as an M-ary scheme whose waveforms are not exactly orthogonal, demodulated like non-coherent M-FSK. I found nothing giving chirps an advantage over tones at ultra-low SNR on a phase-stable narrowband channel.

**Sigfox-class ultra-narrowband** [B, snippets]. 100 bit/s D-BPSK uplink and 600 bit/s GFSK downlink. I found no primary paper quantifying drift tolerance.

---

## 7. Synthesis

### (i) Findings that bear most directly on tone rate, spacing and number of positions

1. **For the coherent rungs, tone rate is not set by SNR.** Per-tone Es/N0 can be −15 dB or lower (EbNaut works that way). What sets T is, in order:
   - the antenna transient: keep B·T ≥ 20 for ≤ 0.14 dB per quarter-turn step, or B·T ≥ 10 if steps are phase-continuous and tones are close;
   - the timing-search burden (the VERDIN lesson);
   - the per-tone decorrelation budget: keep spread × T ≤ 0.1–0.2 for ≤ 0.5 dB.

2. **For the energy-detection and short-window rungs, the unit that matters is the coherent window.** It should hold E/N0 ≈ 2–4 (3–6 dB): Butman–Levitt peaks at ST/N0 ≈ 2–4, and FST4W, WSPR and MER all run at about 0 dB per coherent piece. At −10 dB-Hz that means 20–40 s. At −20 dB-Hz it means 200–400 s. With millihertz spread this is achievable down to about −20 to −25 dB-Hz. Below that, energy combining loses 1 dB per dB and only whole-message coherence helps.

3. **Tone spacing.**
   - 1/T is right if per-tone energy detection must stay clean. It is the universal amateur choice and the MIL-STD minimum for simple phase-continuous FSK.
   - Spacing above 1/T is justified only when spread or drift approaches the spacing (JT4, JT9, Q65, MER).
   - Spacing below 1/T costs the coherent rungs essentially nothing at these SNRs but degrades the energy rung (leakage |ρ|² = 0.405 at half spacing). The energy rung must then become a multi-tone sequence correlator, as in FST4 or Simon–Divsalar block detection.
   - If the band admits only 2–4 positions at 1/T, half spacing doubles the positions for the coherent rungs.

4. **Number of positions.**
   - Coherent: only the centroid term matters (3.0, 1.25, 0.58, 0.28 dB for M = 2, 4, 8, 16), and it is removable.
   - Non-coherent: more positions matter strongly (best-case 8.4, 5.8, 4.4, 3.9 dB below the coherent limit; hard-decision optimum never below 7 tones).
   - So fit 8–16 positions if the energy rung is expected to carry real traffic. 2–4 suffice if the link is expected to be coherent and the phase carries information.

5. **Search grids are a real cost.** MER reports 2–3 dB of threshold difference from search-space size alone. EbNaut and VERDIN avoid the search with GPS or atomic references. Every extra ladder rung and grid point raises the noise-only maximum, which matters for a 10–24-bit ML decoder with no CRC margin.

### (ii) Where the literature contradicts or refines the design context

1. **Tone-count penalties (2.6 / 0.8 / 0.2 / 0 dB).** The capacity-limit values are 3.01 / 1.25 / 0.58 / 0.28 dB (Dolinar et al. 2000). Your finite-block numbers are 0.3–0.45 dB smaller at every M.
   - That offset is plausible. Random tone codewords have a lower cross-correlation variance than random phase-keyed ones, about (1/M)(1−1/M) against 1 per symbol.
   - "Sixteen costs nothing" is a finite-block effect, not an asymptotic one.
   - The penalty exists only if two candidate codewords that pick the same tone in a slot also carry the same phase, i.e. the quarter-turn is keyed per slot independently of the message. If the keyed sequence instead selects both tone and phase as a function of the message (a frequency × phase product alphabet), the mean correlation is zero and the whole centroid penalty disappears at no bandwidth cost. The energy rung is unaffected, because tone patterns alone still separate messages.
   - Precedents: biorthogonal sets (MacMullan & Collins 1998 [B]); Padovani & Wolf, "Coded phase/frequency modulation", IEEE Trans. Commun. 34(5):446–453, 1986, DOI 10.1109/TCOM.1986.1096564 [B].
   - I could not tell from the context whether the phase is already message-dependent. If it is, the observed 2.6 dB for two tones needs another explanation.

2. **"Phase-continuous" and "keyed quarter-turn phase per tone" conflict through a high-Q antenna.** Each quarter-turn step is a phase discontinuity costing about 1/(πBT) in amplitude, and a half-turn costs twice that. The VLF standard avoids such steps because of transmitter stress.
   - The professional answer was MSK/CSK: realise the quarter-turns as continuous-phase half-cycle frequency excursions on a 1/(2T) lattice.
   - The ELF precedent detected exactly that as antipodal.
   - This is a real alternative to an h = 1 lattice with phase jumps: antipodal-class distance from two frequencies in about 1.2/T of bandwidth, at the price of a non-orthogonal energy rung.

3. **Coherent orthogonality at 1/(2T) is phase-conditional.** Combining a half-spacing lattice with arbitrary keyed quarter-turns gives ±0.64 correlations between adjacent tones in quadrature. This is harmless on average for ML codeword decoding but should be in the distance spectrum.

4. **Frequency grid at half the reciprocal of the window.** Scalloping is 0.91 dB worst case and 0.29 dB on average [my calc, rectangular window]. At a quarter of the reciprocal it is 0.22 / 0.07 dB. FST4's fine step is 0.02 baud, about 0.16 of the reciprocal of its 8-symbol block. EbNaut budgets 0.3 dB for its phase grid.

5. **Timing grid of one eighth of a tone.** Loss is linear in offset: 0.56 dB worst case and 0.27 dB on average when the neighbouring tone differs. At one sixteenth it is 0.28 / 0.14 dB. FST4 uses about 1/36–1/40 of a symbol (0.11 dB worst).
   - Items 4 and 5 together cost up to about 1.5 dB worst case and about 0.56 dB on average. That is larger than the 0.2–0.8 dB differences between tone counts being optimized.

6. **"Try both and keep the better" is supported**, by FST4's 1/2/4/8-symbol ladder (+1.7 dB over JT9 in simulation) and by Barletta & Rini's three regimes. Two refinements:
   - the optimum window is 0.1–0.7 of 1/linewidth depending on window SNR, so the ladder should extend to about 2/(π·linewidth);
   - best-of selection inflates false decodes and must be included in thresholds.

7. **The h = 0.715 result is irrelevant** to an h = 1, whole-cycle, coded, exhaustively decoded design. So are the multi-symbol CPFSK gains of 0.85 to 3.5 dB. They are high-SNR minimum-distance results that depend on phase memory. They become relevant only if the lattice is deliberately moved off whole cycles to gain bandwidth.

8. **The antenna's static phase and delay per tone must be in the template** (up to ∓45° and τ/2 at the −3 dB edges). The context mentions group delay as a timing offset, but it is tone-dependent, not a single offset.

9. **Synchronous retuning of the antenna with the keying** (Wolff 1957; Kemp et al. 2019) removes the passive-bandwidth limit for one-tone-at-a-time FSK. If the transmit antenna can be switched, "only a few positions fit" stops being a hard constraint.

### (iii) Not accessed, or accessed only as an abstract or record

**Abstract only [A]:** Osborne & Luntz 1974; Schonhoff 1976; Stark 1985; Butman et al. 1976; Bar-David & Butman 1973; Bernstein, McNeill & Richer 1974; Verdú 2002; Khanzadi et al. 2015 (Wiener); Ghozlan & Kramer 2017; Barletta & Rini 2020; Seshadri, Cheng & Valenti 2007; Cheng et al. CISS 2007; Soriano et al. 2013.

**Record or snippet only [B]:** Aulin & Sundberg 1981 Parts I and II; Cheng, Valenti & Torrieri 2009; Valenti, Cheng & Torrieri 2009; Torrieri, Cheng & Valenti 2008; Massey 1981; MacMullan & Collins 1998; Padovani & Wolf 1986; Divsalar & Simon 1990 (journal); Simon & Divsalar 1993 and 1994; Colavolpe & Raheli 1999 (both); Peleg & Shamai 1998; Peleg, Shamai & Galán 2000; Nuriyev & Anastasopoulos 2005; Katz & Shamai 2004; Lapidoth 2002; Barbieri & Colavolpe 2011; Khanzadi, Durisi & Eriksson 2015; Zheng, Tse & Médard 2007; Foschini, Greenstein & Vannucci 1988; Greenstein, Vannucci & Foschini 1989; Bernstein et al. 1974 (Proc. IEEE); Wolff 1957; Polyanskiy, Poor & Verdú 2010; Lancho et al. 2020; Durisi, Koch & Popovski 2016; Coşkun et al. 2019; Mauger et al. 2022; the LoRa and Sigfox items.

**Could not access [X]:** Anderson, Aulin & Sundberg (book); Proakis; Wozencraft & Jacobs; Viterbi & Omura; Watt, *VLF Radio Engineering*; STANAG 5030; Valenti & Cheng 2005 (extraction failed); Guillén i Fàbregas & Grant 2007 (403); Cheng's dissertation (403); Azizoglu & Humblet 1991 (403); DTIC ELF and VLF reports (403); the EbNaut primary site (DNS failure, and the web archive is blocked for the fetch tool).

**Not found or not researched:** JT9-2…-30 tables; WSPR-15 primary document; Opera specification; Jason; DFCW/QRSS; Slow JT9; any Q65 or FST4 QEX paper beyond the Quick-Start guides; Hamkins-type "MFSK for low-rate deep space" reports beyond the EDL papers.

**DOI verification.** Every DOI printed above was fetched at api.crossref.org/works/<DOI> and matched on authors, title and year. No DOI was constructed from a pattern. The one identifier not verifiable in Crossref is arXiv:2001.07485 (its DataCite DOI 10.48550/arXiv.2001.07485 returns "Resource not found" there), so it is cited by arXiv number. The JPL progress reports, MIL-STD-188-140A, the WSJT-X guides, the TAPR paper and the amateur web pages have no DOI.

---

## The brief given to the search

Reproduced as sent, so that the report's references to it can be read.

> Literature research task (web research; do NOT edit or create files in any repository). Your final message is the deliverable and nobody else sees your intermediate work, so put everything there. It may be long (3,000–6,000 words is fine).
>
> Question: How should the tone (symbol) rate and the tone spacing of an M-ary FSK signal be chosen when (a) the receiver must detect coherently across many tones because each tone carries far too little energy to be detected by itself (carrier-to-noise density roughly −10 to −40 dB-Hz, so integration over minutes to hours), (b) the channel is nearly phase-stable but has a small Doppler spread / slow phase wander (millihertz), (c) the total bandwidth is small (a few Hz to perhaps 100 Hz, set by a high-Q transmit antenna) so only a few tone positions fit, (d) the transmitter is peak-power limited with a constant envelope, one tone at a time, and (e) the message is short (10–24 information bits per block, decoded by exhaustive maximum-likelihood search over all messages with a known keyed pseudo-random code, so the receiver can correlate each whole candidate codeword coherently)?
>
> Design context I already have (do not repeat; check and extend): a lattice where tone spacing equals the reciprocal of the tone length (orthogonal for non-coherent detection, whole cycles per tone, phase-continuous, each tone given a keyed quarter-turn phase); the receiver always tries both per-tone energy detection and coherent sums over windows (a ladder of window lengths), keeping the better; two tone positions cost 2.6 dB and four cost 0.8 dB against a coherent phase-keyed reference in a 10-bit block, eight 0.2 dB, sixteen nothing; the frequency search spacing is half the reciprocal of the coherent window; timing search spacing is one eighth of a tone.
>
> Please find and report, with full citations:
> 1. Theory of coherent detection of FSK: minimum spacing for coherent orthogonality (1/(2T), i.e. MSK-type) versus non-coherent orthogonality (1/T); the optimum deviation for coherent binary FSK (the well-known h ≈ 0.715 giving about 0.8 dB better than orthogonal with single-symbol coherent detection) and for multi-symbol observation of CPFSK (Osborne & Luntz 1974; Schonhoff 1976; Aulin & Sundberg 1981 CPM papers; Anderson, Aulin & Sundberg "Digital Phase Modulation"), what gain is available at low spectral occupancy, and whether any of this matters when a random code over the tones is decoded by exhaustive codeword correlation (my expectation: with whole-codeword correlation only the cross-correlation between tone waveforms matters, and non-orthogonal closer spacing trades bandwidth for a small energy loss — confirm or correct with sources on coded CPFSK / "coded non-orthogonal FSK" at low rate and on the capacity of FSK with spacing below 1/T under coherent detection).
> 2. Capacity and cutoff rate of M-ary orthogonal signalling with COHERENT detection at very low SNR versus non-coherent, as a function of M (2, 4, 8, 16, 64, 256): the loss of 2- and 4-ary orthogonal FSK against antipodal/BPSK and against the unconstrained AWGN bound at low rate (the 3 dB for binary orthogonal, biorthogonal sets, simplex sets), so I can judge how many tone positions are worth fitting into a narrow band. Sources: Proakis; Wozencraft & Jacobs; Viterbi & Omura; Stark 1985 "Capacity and cutoff rate of noncoherent FSK with nonselective Rician fading"; Butman et al. on non-coherent M-ary; Dolinar/Divsalar/Pollara JPL reports; Massey 1981 "Capacity, cutoff rate, and coding for a direct-detection optical channel"; any exact "coherent M-ary orthogonal capacity" tables.
> 3. Partially coherent reception: block/multiple-symbol detection of FSK and DPSK (Divsalar & Simon 1990; Simon & Divsalar on multiple-symbol non-coherent CPFSK; Valenti & Cheng on iterative non-coherent CPFSK; "trellis-based non-coherent sequence detection" Colavolpe & Raheli 1999), and the performance of coherent integration over windows shorter than the phase coherence time. In particular, results expressing the loss versus (window length ÷ coherence time), and the optimal coherent window for a given Doppler spread and symbol SNR; the Tikhonov-phase (partially coherent) capacity results (e.g. Katz & Shamai 2004 "On the capacity-achieving distribution of the discrete-time noncoherent and partially coherent AWGN channels"; Barbieri & Colavolpe; Lapidoth on phase noise channels; Khanzadi et al.) and what they say for very low SNR with Wiener phase noise: how the best strategy trades symbol length against phase-noise innovation.
> 4. Practice in weak-signal and VLF systems, with their actual parameters and the reasons given: (i) US Navy / NATO VLF: 50-baud FSK with ±25 Hz shift, 200-baud MSK, multi-channel MSK, "compatible shift keying", STANAG 5030, VERDIN, and the minimum-shift choice driven by antenna bandwidth; ELF Seafarer/Sanguine/ELF Communications System MSK at 76 Hz with ~8 Hz shift, 16 chips/s?, convolutional code and sequential decoding with 15-minute messages (Bernstein et al. 1974 "Long-range communications at extremely low frequencies", Proc. IEEE; IEEE Trans. Commun. April 1974 special issue on ELF communications) — these are the closest precedents for coherent FSK through a tiny-bandwidth antenna at negative SNR; (ii) amateur weak-signal modes: EbNaut (coherent BPSK, symbol lengths of 1 s to many minutes, rate-1/K convolutional codes with list decoding, used at 8.27 kHz, 137 kHz, 472 kHz), FST4/FST4W (4-GFSK, tone spacing equal to baud, symbol lengths 0.14–11.2 s for 15–1800 s periods, block detection over several symbols, and the measured sensitivity versus Doppler spread), WSPR/WSPR-15, JT9 (9-FSK with sub-Hz spacing variants JT9-2…JT9-30), Opera, DFCW/QRSS, WOLF (coherent BPSK at 10 baud on 137 kHz with long coherent integration), Jason, "Slow JT9", and Q65 (65-FSK with selectable tone-spacing multipliers A–E and T/R periods; the published guidance on choosing spacing versus Doppler spread). I want the published reasoning for spacing-to-rate ratios larger than 1 on spread channels, and for symbol lengths versus path stability.
> 5. FSK through a band-limited (high-Q) channel/antenna: effect of the transmit filter on coherent correlation (amplitude roll-off, group delay equals timing offset, transition transient loss about 1/(π·B·T)), and any guidance on the ratio of total FSK span to the −3 dB bandwidth.
> 6. Recent (2019–2026) academic work that adds nuance: e.g. LoRa-like chirp or "FSK-LoRa" comparisons at ultra-low SNR, short-packet finite-blocklength bounds for non-coherent/coherent FSK (Polyanskiy–Poor–Verdú normal approximation; Durisi, Koch, Östman, Lancho et al. on short packets with unknown phase / pilot-free transmission; "quasi-static phase noise"), satellite/IoT ultra-narrowband FSK (Sigfox-like 100 bit/s D-BPSK and 600 bit/s GFSK; UNB frequency drift literature), and deep-space coherent/non-coherent MFSK beacon tones (e.g. JPL "MFSK for low-rate deep space", the Mars rover EDL tones: 256-FSK "semaphore" tones at very low C/N0 with 10 s symbols — Satorius, Estabrook, Wilson, Fort 2003 "Direct-to-Earth communications and signal processing for Mars Exploration Rover entry, descent, and landing", which is a close analogue: M-FSK tones detected at about 20 dB-Hz with frequency drift searches).
>
> Method requirements:
> - Prefer primary sources. For each source say whether you read the full text, only the abstract, or could not access it; never attribute a number to a source you could not open.
> - Verify every DOI by fetching https://api.crossref.org/works/<DOI> and confirm authors/title/year match; list unverifiable DOIs separately; never construct a DOI from a pattern.
> - Give numbers with units and conditions, and say whether each is exact theory, simulation or measurement.
> - Do not include any personal or project-identifying information in web requests; use generic queries.
> - End with (i) the findings that bear most directly on choosing tone rate, tone spacing and number of positions for a coherent-reception, slightly spread, few-hertz-to-100-Hz-wide link; (ii) where the literature contradicts or refines the design context listed above; (iii) a not-accessed list.

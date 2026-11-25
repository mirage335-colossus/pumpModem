# Theory literature review: very weak, one-way, noncoherent, peak-limited HF links

Prepared 2026-11-24 for the Matrix Modem study. It extends, and does not repeat, `Sol-6.2/information-limits.md` and `Sol-6.2/observer-and-interference.md`. Those notes already cover Shannon/AWGN arithmetic, the PPV normal approximation, the toy quasi-static outage, the flash-signaling divergence argument, Koch–Lapidoth 1-bit results, the Durisi–Morgenshtern–Bölcskei sensitivity paper, Qi–Koch 2026, the union-bound threshold `ln(K/α)`, the AWGN square-root law, SNR walls (qualitatively), and the radiometer-versus-matched-filter time scaling.

**Access legend.** **[FT]** means I read the full text (arXiv, author copy, or OCR of a scanned report). **[FT-conf]** means I read the conference version, not the journal version. **[Sec]** means the statements come from a secondary source that quotes the paper, which is named. **[Abs]** means abstract or bibliographic record only. **[Mem]** means I did not retrieve the paper in this session; the item comes from prior knowledge and must be checked before anyone relies on it. **[Computed]** marks numbers I derived or simulated here. The review's scratch scripts were temporary and are not retained; its peak-limited capacity values are independently reproduced by `_appraisal/algorithms_matrixModem/Opus-5.5/calc/peak_limited_bound.py` and `mfsk_capacity.cpp`. The Monte Carlo (MC) figures carry roughly ±0.1 dB sampling uncertainty unless a tighter value is stated.

**Search limitation.** The session's shared web-search budget ran out partway through Topic 6. Topics 6–8 therefore rely on directly fetched known URLs, prior knowledge flagged [Mem], and my own computations. In particular, I could not survey 2018–2026 covert-communication papers specific to frequency hopping or FSK. That gap is listed under Topic 7.

---

## 0. The decision-relevant quantity: energy per coherence cell

Every peak-constrained noncoherent result below reduces to one dimensionless parameter:

\[
x \;=\; \frac{E_c}{N_0} \;=\; \frac{C}{N_0}\,T_c \;\approx\; \frac{C/N_0}{B_{\rm eff}},\qquad 
B_{\rm eff}=\frac{\big(\int S_H(f)\,df\big)^2}{\int S_H(f)^2\,df}.
\]

Here x is the received energy that a constant-envelope tone collects during one fading coherence interval, measured in units of N0. For a flat (rectangular) Doppler spectrum of width B_D, B_eff = B_D. For a Gaussian Doppler spectrum with rms σ, which is the shape the Watterson/ITU HF model specifies through its "2σ" spread, B_eff = 2√π σ ≈ 1.77·(2σ) [Computed]. With C/N0 between −15 and +25 dB-Hz and B_D between 0.1 and 30 Hz, x ranges from about −30 dB to +35 dB. That range spans every regime discussed below. In summary:

- At x ≳ 20 dB, noncoherent operation is within a few tenths of a dB of AWGN.
- At x ≈ 0 dB, the loss is 5–7 dB.
- At x ≪ 1, the rate collapses to a quadratic law, R ≈ (C/N0)²/(2B_eff) nats/s. In that regime every dB of C/N0 lost costs 2 dB of rate.

---

## 1. Wideband / low-SNR noncoherent fading capacity, coherence, and peak constraints

### 1.1 Exact capacity per unit energy under a peak constraint (Sethuraman & Hajek 2005) [FT]

**Model.** The discrete-time channel is Y(k) = H(k)X(k) + W(k). H is stationary, ergodic, proper complex Gaussian with unit variance. W is white with unit variance. Neither end has CSI. The constraint is |X|² ≤ P (peak), plus an average-energy budget.

**Proposition 3.1.** Let S(ω) be the absolutely continuous part of the fading spectral measure. Then

\[
C_p(P)=1-\frac{I(P)}{P},\qquad I(P)=\int_{-\pi}^{\pi}\log\!\big(1+P\,S(\omega)\big)\frac{d\omega}{2\pi}\quad\text{(nats/J, N_0=1)}.
\]

ON-OFF codewords whose ON value is √P achieve this asymptotically. ON symbols are rare and come in long bursts. I(P) is the information rate about the fading process that a constant input at √P reveals, so it is the "price of not knowing H". With receiver CSI, C_p = 1 at any peak (Prop. 3.2). Because log(1+x) ≥ x − x²/2, the bound C_p ≤ U_p = (P/2)∫S²dω/2π holds, and it is tight as P → 0.

**Proposition 3.3 (continuous time, no bandwidth constraint).** The same form holds with I(P) = ∫ log(1 + P S_H(ω)) dω/2π over ℝ. The paper gives closed forms:

- **Gauss–Markov fading:** C_p = 1 − [√((log ρ)² − 2P log ρ) + log ρ]/P.
- **Clarke/Jakes fading:** a closed form exists. U_p diverges for this spectrum.
- **Block fading with block length T (Corollary 4.3):** C_p = 1 − log(1+PT)/(PT).

Eq. (36) adds that, for capacity per unit time with both peak and average constraints, C ≤ (T/2)·P_avg·P. With a bounded peak-to-average ratio, capacity is therefore **quadratic** in average power at low power.

*Applicability.* This is the cleanest available model of a single constant-envelope HF tone, because a tone a few Hz wide is frequency-flat when the delay spread is 0.5–7 ms. With units restored (P → C/N0 in Hz, S_H in s):

\[
\eta(x)=\frac{C_p}{1\ \text{nat}/N_0}=1-\frac{\ln(1+x)}{x},\qquad x=\frac{C/N_0}{B_D}\ \text{(flat Doppler)}\ \text{or}\ (C/N_0)T\ \text{(block)}.
\]

### 1.2 Constant-envelope M-ary FSK achieves the same expression (Viterbi 1967; Durisi–Bölcskei–Shamai 2007)

Viterbi analyzed M orthogonal signals, each a sample function of a stationary complex Gaussian process with a frequency-shifted common spectrum (that is, FSK through Doppler-spread Rayleigh fading). He derived ML error exponents that are positive at all rates up to capacity [Abs; DOI 10.1109/TIT.1967.1054039]. Durisi, Bölcskei and Shamai, *Capacity of Underspread Noncoherent WSSUS Fading Channels under Peak Signal Constraints* (arXiv:0705.1682; ISIT 2007, venue not verified) [FT], prove a lower bound for a time-only peak constraint P_peak = βP and infinite bandwidth (Thm. 3):

\[
C_\infty \ge P\sigma_H^2-\frac{1}{\beta}\int_\nu \log\!\big(1+\beta P\,\tilde C_H(\nu)\big)\,d\nu .
\]

Here C̃_H is the power-Doppler profile. At β = 1 (no peakiness at all, which is a constant-envelope tone hopping among frequencies), this bound "coincides with Viterbi's lower bound". The paper itself does not give a matching upper bound.

**Inference, flagged.** For frequency-flat time-selective fading, Sethuraman–Hajek Prop. 3.3 gives C(P_avg ≤ P_peak = P) ≤ P·C_p(P) = P − I(P). That is the same expression. Combining the two papers, the β = 1 infinite-bandwidth capacity of constant-envelope signaling over frequency-flat Rayleigh fading appears to be exactly

\[
C = \frac{C}{N_0}-\int \ln\!\Big(1+\frac{C}{N_0}S_H(f)\Big)\,df\quad\text{nats/s},
\]

and large-M FSK (Matrix's candidate) achieves it. Two caveats remain. The model correspondence (independent versus shared fading across tones, and the bandwidth idealization) is mine and has not been checked by the authors. And, as Section 2 shows, convergence in M is slow.

Two further results from the same paper [FT]:

- With a peak constraint in both time **and** frequency (noise-like spreading), capacity rises to a peak at a critical bandwidth and then falls to zero ("overspreading"). Their IEEE 802.11a-like example (Δ_H = 10⁻³, 11 mW, −90 dB path loss) peaks near **11 GHz**.
- With peakiness allowed in frequency only (FSK-like), overspreading is avoided.

### 1.3 Quadratic regime and the overspreading literature

- **Médard & Gallager 2002**, IEEE TIT 48(4):840–852, DOI 10.1109/18.992769 [Sec: author talk slides "Bandwidth scaling for fading multipath channels"; Gómez-Cuba et al.]. When signals spread power uniformly with a fourth-moment (kurtosis) bound E|X|⁴ ≤ αS²/W², mutual information per unit time decays like **1/W**. The slide form is (1/n)I ≤ αS²T_cF_c/(W²N0²) per sample. Peaky-in-frequency strategies "akin to frequency hopping" escape the bound.
- **Telatar & Tse 2000**, IEEE TIT 46(4):1384–1400 [FT].
  - Wideband capacity equals that of AWGN, and **FSK with noncoherent detection at low duty cycle achieves it** (Thm. 1).
  - White-like signals deliver mutual information inversely proportional to the number of resolvable paths L, even when the paths are tracked perfectly.
  - A critical L_crit marks the overspreading threshold. In words, it is crossed "when the SNR per path over a coherence time interval is small", so L_crit scales like (P/N0)T_c. The exact symbolic formula did not survive text extraction.
  - Their example uses T_c = 18 ms and a 9.6 kb/s user, which gives L_crit of order 10².
- **Subramanian & Hajek 2002** [Abs]. Capacity per unit "fourthegy" (a cost built on the squared ambiguity function) is finite and nonzero for WSSUS channels. This is the capacity-per-cost counterpart of Médard–Gallager.
- **Rao–Hassibi-type results** [Mem] and Sethuraman–Hajek eq. (36) both give the same conclusion: with bounded peakiness, I ≈ (energy per coherence block)²/2 per block.

**Numerical consequence [Computed, flat Doppler]:**

\[
R \approx \frac{(C/N_0)^2}{2B_{\rm eff}}\ \text{nats/s}\quad(x\ll1).
\]

### 1.4 Flash signaling, wideband slope, and PAR limits (Verdú 2002; Gursoy–Poor–Verdú OOFSK) [Sec; FT]

- **Verdú (2002).** Without receiver CSI, approaching −1.59 dB requires flash signaling, and the wideband slope is S0 = 0. The −1.59 dB limit is approached with an infinite second derivative of capacity. [Sec: quoted by Yang–Durisi–Polyanskiy 2016 and Gómez-Cuba et al.; the theorem numbering is cited there as "Th. 7".]
- **Gursoy, Poor & Verdú, "On-Off Frequency-Shift-Keying for Wideband Fading Channels"** (arXiv cs/0510075; EURASIP JWCN 2006) [FT]. The receiver uses M-FSK overlaid on on/off keying with energy detection.
  - **Prop. 5:** with a *fixed duty factor* ν (fixed PAR), even in unfaded AWGN Ċ(0) = 0. The bit energy at zero rate is infinite, so **minimum E_b/N0 occurs at a nonzero rate**.
  - For 2-OOFSK in AWGN, the minimum E_b/N0 is **≈ 6.7 dB at ν = 1** (ordinary binary FSK, constant envelope) and **≈ −0.2 dB at ν = 10⁻⁴**, which costs a PAR of 10⁴.
  - **Prop. 6:** with a peak limit only, the minimum bit energy is at zero spectral efficiency and does not depend on M. For imperfect fading knowledge, E_b/N0,min = log_e 2 / [1 − log(1+η)/η] for Rayleigh, where η = AT/N0 is the normalized peak energy per symbol. This is the same η(x) as above. S0 = 0 whenever ηγ² ≥ 1.

*Applicability.* Matrix is in the "limited peak" case. Transmitting at peak continuously is the best possible use of a peak-limited transmitter, and the achievable efficiency is set by the per-coherence-cell energy x, not by the choice of data rate.

### 1.5 How large must the coherence be? (Ray, Médard & Zheng 2007; Zheng, Tse & Médard 2007)

Ray, Médard & Zheng, "On noncoherent MIMO channels in the wideband regime: capacity and reliability" (arXiv cs/0603018; IEEE TIT 2007) [FT, arXiv version], study Rayleigh block fading with coherence length l symbols, an **average-power constraint only**, and SNR per degree of freedom.

- **Theorems 1–3, SISO (t = r = 1).** C(SNR) = SNR − SNR^{1+α} + … holds if and only if l ≈ ¼·SNR^{−2(α+σ)}. "Peaky Gaussian" signaling achieves it by transmitting in a fraction SNR^{1−α} of the blocks.
- **Coherent-like capacity** (α = 1, penalty O(SNR²)) needs **l ≥ ¼·SNR⁻²**.
- Eliminating α gives a loss of **Δ ≈ SNR/(2√l)**. The minimum energy per bit penalty therefore falls as **1/√l**. For example, l = 100 gives a 5 % (0.22 dB) loss, and l = 10⁴ gives 0.5 %.
- At the i.i.d. extreme, Δ ≐ SNR/log(1/SNR).
- Error probability in this regime is dominated by outage and decays only inversely with coherence length. Cut-off and critical rates are far below capacity.

The optimal peaky-Gaussian block uses per-symbol SNR′ ≈ 1/(2√l), so each block carries energy ≈ √l/2 [Computed from the theorem]. Under a hard peak per symbol, that SNR′ may not be reachable, and Section 1.1 then governs.

Zheng, Tse & Médard, "Channel coherence in the low-SNR regime," IEEE TIT 53(3):976–997, 2007, DOI 10.1109/TIT.2006.890777 [Abs/Sec]. It defines a "coherence level" through the scaling of coherence time with SNR, and that level determines the sublinear term. Gómez-Cuba et al. restate the coherence dimension as B_cT_c ∝ SNR^{−μ}.

### 1.6 Critical bandwidth for non-peaky signals (Lozano & Porrat 2012; Gómez-Cuba, Du, Médard & Erkip)

Lozano & Porrat, IEEE TWC 11(1):246–257, 2012 [Abs; FT not obtainable (HTTP 403)], with formulas taken [Sec] from Gómez-Cuba et al. (arXiv:1501.04905v3, 2016; IEEE TWC version not verified) [FT]. Non-peaky signals reach a maximum rate at a finite critical bandwidth. For SISO, the restated formulas are:

\[
R(B_{\rm crit})\approx \frac{P}{N_0}\Big(1-\sqrt{\tfrac{1+\ln(B_cT_c)}{B_cT_c}\,\kappa\,\ln\pi}\Big),\qquad
(\delta B)^*\approx\frac{P}{N_0}\sqrt{\frac{\kappa\,B_cT_c}{\ln(B_cT_c)}} .
\]

κ is the channel kurtosis: 2 for Rayleigh, 2 − 4k²/(1+2k)² for Rician, and 1 + 1/m for Nakagami. The extent of the square root is my reconstruction from extracted text and matches the paper's numerical examples to within about 10 %.

Gómez-Cuba et al.'s unifying result is that every scheme with duty cycle δ and bandwidth B depends only on the **bandwidth occupancy δB**. Peaky and non-peaky signaling with the same optimal occupancy approach C∞ within the same gap. Their example uses 2×2 MIMO at P/N0 = 70 dB: (δB)* ≈ 120 MHz with a gap below 0.18 when B_cT_c = 10³, and (δB)* ≈ 930 MHz with a gap below 0.03 when B_cT_c = 10⁵.

**HF translation [Computed].** Take C/N0 = 10 dB-Hz (P/N0 = 10 Hz) and B_cT_c ≈ 10³ (for example, 1 ms delay spread and 1 s coherence). Then (δB)* ≈ 10·√(2000/6.9) ≈ **170 Hz**. At 0 dB-Hz it is about **17 Hz**. A noise-like waveform that fills 2.2 kHz continuously is therefore *overspread* for weak HF. A single hopping tone, whose instantaneous occupancy is about 1/T_s, is not. This reverses the wireless-broadband conclusion that "critical bandwidths are GHz, so non-peakiness is harmless", because HF power is roughly 50–70 dB lower per unit coherence.

**Porrat, Tse & Nacu 2007**, IEEE TIT (arXiv cs/0509068) [Abs/Sec]. Duty-cycled DSSS reaches wideband capacity if the number of resolvable paths grows sublinearly with bandwidth. Duty-cycled PPM needs sublogarithmic growth, because its lower spectral efficiency makes it more sensitive to channel uncertainty.

**Luo, Médard & Zheng 2005**, JSAC 23(9):1830–1838, DOI 10.1109/JSAC.2005.853803 [Abs]. Flash FSK achieves wideband capacity, but its codeword error probability falls slowly with bandwidth. Multitone FSK keeps the property, needs **lower peak power per tone** by an error-exponent argument, and supports a larger alphabet. The paper also computes hard-decision capacities for single- and two-tone FSK at finite bandwidth, which are close to the wideband limit for typical parameters.

### 1.7 Rate ceilings for HF parameters [Computed from §1.1–1.2]

**Table A.** Ceiling R = (C/N0)·η(x)·log₂e for a flat Doppler spectrum of width B_D, or a Gaussian spectrum with 2σ = B_D. Values are bit/s, with the loss relative to AWGN in dB in parentheses. These are ideal M → ∞ bounds.

| C/N0 (dB-Hz) | AWGN | B_D = 0.1 Hz flat / Gauss | 1 Hz | 3 Hz | 10 Hz | 30 Hz |
|---:|---:|---|---|---|---|---|
| −15 | 0.046 | 0.0060 (−8.8) / 0.0036 (−11.0) | 7.1e-4 (−18.1) / 4.0e-4 (−20.6) | 2.4e-4 (−22.8) | 7.2e-5 (−28.0) | 2.4e-5 (−32.8) |
| −5 | 0.46 | 0.25 (−2.6) / 0.19 (−3.9) | 0.060 (−8.8) / 0.036 (−11.0) | 0.022 (−13.1) | 0.0071 (−18.1) | 0.0024 (−22.8) |
| 0 | 1.44 | 1.10 (−1.2) / 0.91 (−2.0) | 0.44 (−5.1) / 0.29 (−7.0) | 0.20 (−8.6) | 0.068 (−13.3) | 0.024 (−17.9) |
| +10 | 14.4 | 13.8 (−0.2) | 11.0 (−1.2) / 9.1 (−2.0) | 8.1 (−2.5) | 4.4 (−5.1) / 2.9 (−7.0) | 2.0 (−8.6) |
| +20 | 144 | 143 | 138 (−0.2) | 129 (−0.5) | 110 (−1.2) / 91 (−2.0) | 81 (−2.5) / 60 (−3.8) |
| +25 | 456 | 455 | 448 | 436 | 406 (−0.5) | 350 (−1.1) / 293 (−1.9) |

**Table B.** Required C/N0 (dB-Hz) for a target ideal rate R, flat Doppler spectrum.

| R (bit/s) | AWGN | B_D = 0.1 | 0.3 | 1 | 3 | 10 | 30 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.01 | −21.6 | −13.8 | −11.6 | −9.1 | −6.8 | −4.2 | −1.9 |
| 0.1 | −11.6 | −7.8 | −6.0 | −3.8 | −1.6 | +0.9 | +3.2 |
| 1 | −1.6 | −0.3 | +0.7 | +2.2 | +4.0 | +6.2 | +8.4 |
| 10 | +8.4 | +8.7 | +9.0 | +9.7 | +10.7 | +12.2 | +14.0 |

Practical additions on top of these ceilings:

- Finite-M noncoherent detection: +1 to 1.5 dB (Section 2).
- Finite message length and reliability (Section 3).
- Search and false-alarm costs (Section 8).
- Outage. Slow fading with few independent fades per message is not captured by these ergodic numbers.

---

## 2. Coded noncoherent orthogonal (MFSK) performance and capacity

**Stark 1985**, IEEE TCOM 33(11), DOI 10.1109/TCOM.1985.1096232 [Abs; author copy is a scan and was not OCR'd]. It is the original capacity and cutoff-rate calculation for noncoherent FSK with soft and hard decisions in nonselective Rician fading. Later work cites it as the reference showing that the required E_b/N0 has a minimum at a nonzero code rate and diverges as R → 0 for any fixed M.

**Guillén i Fàbregas & Grant** (AusCTW 2006 [FT-conf]; journal version IEEE TWC 6(11):4004–4013, 2007 [Abs]).

- Increasing M lowers the required E_b; as R → 0 the required E_b/N0 → ∞.
- The paper cites prior turbo results: within about 0.7 dB of binary noncoherent FSK capacity in Rayleigh fading, and BICM-ID with the cdma2000 turbo code within about 0.9 dB (4-ary) to about 1.7 dB (64-ary) of capacity. The latter figures are attributed to Valenti & Cheng [Sec].
- They optimize irregular repeat-accumulate outer codes and reach decoding thresholds **0.005–0.17 dB from capacity** in AWGN.
- Their SNR-independent metrics lose about 0.6 dB. Not knowing E_s, N0 or the fading amplitude costs about 1.5 dB in Rayleigh.

| M | R | Capacity E_b/N0 (dB) | Decoding threshold (dB) |
|---:|---:|---:|---:|
| 4 | 1/4 | 4.62 | 4.76 |
| 4 | 1/2 | 4.18 | 4.35 |
| 8 | 1/4 | 3.55 | 3.60 |
| 8 | 1/2 | 2.99 | 3.04 |
| 16 | 1/4 | 2.59 | 2.60 |
| 16 | 1/2 | 2.07 | 2.14 |
| 64 | 1/4 | 1.68 | 1.69 |
| 64 | 1/2 | 1.12 | 1.15 |

**Valenti & Cheng 2005**, JSAC 23(9):1739–1747 [Abs/Sec]. Turbo-coded BICM-ID with a soft noncoherent demodulator, EXIT analysis, and a table of capacity, EXIT threshold and BER 10⁻⁵ points. I could not read the values.

**Extension to large M and fading [Computed, symbol-wise mutual information with soft energy metrics, optimized E_s/N0].**

| Channel | M = 16 | 64 | 256 | 1024 | 4096 |
|---|---:|---:|---:|---:|---:|
| AWGN, noncoherent (min E_b/N0, dB) | 1.97 | 1.23 | 0.59 | 0.16 | 0.04 |
| Optimal E_s/N0 (dB), AWGN | 4 | 6 | 6 | 8 | 8 |
| Rayleigh, independent fade per symbol (L = 1) | – | ≈2.25 | ≈1.85 | **1.43 ± 0.05** | ≈1.2 |
| Optimal E_s/N0 (dB), Rayleigh | – | 4 | 4–6 | 6 | 6 |

The MC agrees with the Guillén–Grant AWGN capacities to within about 0.1 dB at M = 16 and 64. For comparison, **coherent** orthogonal M = 1024 in AWGN reaches −1.35 dB at E_s/N0 = 6 dB [Computed]. The noncoherent penalty at M = 1024 is therefore about 1.5 dB in AWGN, and fading adds about another 1.3 dB.

**Large-M convergence in Rayleigh is slow.** The M → ∞ bound at E_s/N0 = 6 dB gives E_b/N0 ≥ 0.65 dB, but M = 1024 reaches only 1.43 dB. Noise-only bins produce maxima near ln M, and the exponential signal-bin energy needs M to be very large before the per-symbol information approaches γ − ln(1+γ).

**Implicit in-symbol diversity hurts the coded rate [Computed].** For Rayleigh with M = 1024 and L equal-power independent fades inside one symbol (L ≈ 1 + T_s·B_eff):

| L | 1 | 2 | 4 | 8 |
|---:|---:|---:|---:|---:|
| min E_b/N0 (dB) | 1.43 | ≈1.95 | ≈2.45 | ≈3.4 |

The large-M bound becomes 1 − (L/γ)ln(1+γ/L), which falls monotonically in L at fixed γ. Diversity should come from coding across symbols, not from symbols longer than T_c (see Section 5).

**Weak, coherence-limited operating points [Computed].** If the per-symbol energy is limited because x = (C/N0)·T_c is small, Rayleigh M = 1024 gives about 3.6 dB at E_s/N0 = 0 dB and about 4.5 dB at −1 dB. Below that, the quadratic regime of Section 1.3 applies.

*Nonbinary codes with MFSK.* Q-ary LDPC and QRA codes map naturally onto M-ary symbols and avoid BICM loss. I could not verify specific academic thresholds in this session [Mem]. Treat amateur weak-signal modes as engineering anchors only.

---

## 3. Finite-blocklength and minimum energy for short noncoherent messages

**Polyanskiy, Poor & Verdú 2011**, "Minimum energy to send k bits through the Gaussian channel with and without feedback," IEEE TIT 57(8):4880–4902, DOI 10.1109/TIT.2011.2159041 [Abs; expansion as restated in Yang–Durisi–Polyanskiy, FT]. Without feedback:

\[
\log M^*(E,\epsilon)=\frac{E}{N_0}\log e-\sqrt{\frac{2E}{N_0}}\,Q^{-1}(\epsilon)\log e+\tfrac12\log\frac{E}{N_0}+O(1),
\qquad \frac{E_b^*}{N_0}\approx\ln2+\sqrt{\frac{2\ln2}{k}}\,Q^{-1}(\epsilon).
\]

An M-ary orthogonal (PPM-like) codebook with ML decoding achieves this. The abstract states that reaching −1.57 dB without feedback needs k ≥ 10⁶ bits, while with feedback a single bit can be sent at −1.59 dB with zero error.

[Computed, 3-term expansion, ε = 10⁻³] AWGN E_b/N0 required:

| k (bits) | 32 | 128 | 1024 | 8192 |
|---:|---:|---:|---:|---:|
| E_b/N0 (dB) | +2.08 | +0.31 | −0.90 | −1.34 |

**Yang, Durisi & Polyanskiy 2016**, "Minimum energy to send k bits over multiple-antenna fading channels," IEEE TIT 62(12):6831–6853, DOI 10.1109/TIT.2016.2615629 (arXiv:1507.03843) [FT]. The model is Rayleigh block fading with unlimited blocklength and finite energy.

- **No CSI:** log M* = E log e − V0·(E·Q⁻¹(ε))^{2/3}(log E)^{1/3} + …, with V0 = (12^{−1/3} + (2/3)^{1/3})(log e)^{2/3}. The fraction placement in V0 is as text-extracted; verify it against the paper. The gap to −1.59 dB scales as **((ln k)/k)^{1/3}**, versus 1/√k with perfect CSIR.
- Both expansions are **independent of coherence interval and number of transmit antennas**.
- Numerically, a single receive antenna needs **7×10⁷ bits** to reach −1.5 dB without CSI and **6×10⁴ bits** with CSI.
- Achievability uses PPM plus repetition and therefore **requires flash signaling**.
- **Peak-limited heuristic (their eqs. 64–66).** With peak A per codeword entry,

\[
\frac{\log M}{E}\approx \log e-\frac{\log(1+A)}{A}-\sqrt{\frac{A}{E}}\,Q^{-1}(\epsilon)\log e .
\]

  The second term is the Sethuraman–Hajek peak penalty. The third is the penalty from too few independent fades: peakier codewords average less over the channel. The optimal value is A* ≈ ((3/2)Q⁻¹(ε) log e)^{−2/3}·E^{1/3}·log^{2/3}E.

**Peak-limited fading energy [Computed from eq. 65, with A interpreted as energy per independent fade = (C/N0)·T_c; heuristic].** Required E_b/N0 (dB) at ε = 10⁻³:

| k | A = 0 dB | 5 dB | 10 dB | 15 dB | 20 dB | 30 dB | AWGN |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 128 | 6.07 | **4.35** | 4.50 | 6.43 | 9.82 | 18.9 | 0.31 |
| 1024 | 4.45 | 2.22 | **1.41** | 1.86 | 3.51 | 10.3 | −0.90 |
| 8192 | 3.86 | 1.44 | 0.24 | **−0.03** | 0.42 | 3.75 | −1.34 |

For a 128-bit message, fading without CSI costs about **4 dB** relative to AWGN even at the best energy per fade (about 5–10 dB). That optimum needs roughly E/A ≈ 100 independent fades. HF slow fading with T_c of 1–5 s and perhaps 2–15 independent subbands across 2.2 kHz usually cannot supply that many within a short message. The link is then outage-limited, and the ergodic formulas are optimistic.

**Östman, Durisi, Ström, Coşkun & Liva 2019**, "Short packets over block-memoryless fading channels: pilot-assisted or noncoherent transmission?", IEEE TCOM 67(2) (arXiv:1712.06387) [FT]. The paper gives RCUs and min-max converse bounds for Rician block fading without CSI.

- For n = 168, R = 0.48 bit/channel use and ε = 10⁻³ in Rayleigh, the energy-optimal number of diversity branches is **ℓ* ≈ 21**. Fewer branches leave too little diversity; more leave too few symbols per coherence block.
- **Noncoherent transmission beats optimized pilot-assisted transmission by about 1.2 dB** (another 0.5–0.6 dB if pilot and data power must be equal).
- With optimized pilot power, one pilot per coherence block suffices.
- A practical pilot-assisted code comes within about 1 dB of its bound.

**Quasi-static fading (Yang, Durisi, Koch & Polyanskiy 2014, IEEE TIT 60(7))** [Mem]. The channel dispersion is zero, so R*(n, ε) = C_ε + O(log n / n). Outage capacity, not blocklength, governs. *Lancho, Koch & Durisi* (IEEE TIT 2020) and *Lancho, Östman, Durisi et al.* (saddlepoint approximations, IEEE TWC 2020) [Mem] provide high-SNR normal approximations and saddlepoint methods for noncoherent Rayleigh block fading. They are high-SNR or moderate-SNR tools, not weak-signal ones.

---

## 4. Multiple-symbol and sequence noncoherent detection

- **Divsalar & Simon 1990**, IEEE TCOM 38(3):300–308 [Abs]. **JPL Publication 89-38** (Divsalar, Simon & Shahshahani, 1989) [FT, OCR].
  - Conventional differential detection costs about 0.75 dB (DBPSK), about 2.2 dB (DQPSK) and more than 2.5 dB (8-DPSK) at P_b = 10⁻⁵ relative to coherent detection.
  - Extending observation from N = 2 to N = 3 symbols recovers more than half of the DBPSK loss and more than 1 dB of the DQPSK loss. The N = 3 trellis-coded example gains about 0.75 dB.
  - The detector assumes carrier phase constant over the window, which is typically reasonable for 3–4 symbols.
  - Brute-force complexity grows as M^{N−1} per block. **Mackenthun 1994** (IEEE TCOM 42(2/3/4):1471–1474) [Abs] reduces it to O(N log N) for MPSK.
- **Colavolpe & Raheli 1999**, "Noncoherent sequence detection," IEEE TCOM 47(9):1376–1385 [FT].
  - Optimal detection under a constant unknown phase needs complexity exponential in transmission length.
  - Their Viterbi-based approximation trades "implicit phase memory" N against trellis states, works with time-varying phase, and approaches coherent performance at a rate independent of SNR.
  - Examples include DQPSK at about 0.5 dB from coherent at moderate complexity; with code-trellis states, the loss falls from about 1.5 dB to negligible as the number of states grows.
- **Simon & Divsalar 1993**, "Maximum-likelihood block detection of noncoherent continuous phase modulation," IEEE TCOM 41(1):90–98, DOI 10.1109/26.212365 [Abs]. This is block ML detection of full-response CPM, which includes CPFSK.

**Size of the prize for MFSK [Computed].** In AWGN with M = 1024, ideal coherent orthogonal signaling reaches −1.35 dB versus +0.16 dB noncoherent, so phase-exploiting multi-symbol detection is worth at most about **1.5 dB** there, less at smaller M. In fading, that gain can only be realized if (a) the transmitter keeps phase continuity across tone changes, (b) the receiver keeps track of the deterministic phase relationship between tones, and (c) the channel phase is stable, N·T_s·B_eff ≪ 1. The usable coherent energy per cell is again x = (C/N0)·T_c. When x ≲ 0 dB, there is little phase information to recover.

Joint detection over N symbols requires M^N hypotheses (times phase hypotheses). Reduced-state or decision-feedback forms (Colavolpe–Raheli) or per-tone phase tracking are the practical routes. That hypothesis count matters for the look-elsewhere burden in Section 8.

---

## 5. Detecting Gaussian (Doppler-spread) signals and the optimal T·B_D

**Estimator–correlator** (Price 1956; Kailath 1960; Van Trees, *Detection, Estimation and Modulation Theory, Part III*, 1971/2001) [Mem]. With eigenvalues λ_k of the received-signal covariance on [0, T], the log-likelihood ratio is Σ_k [λ_k/(λ_k+N0)]·|r_k|²/N0 − Σ ln(1+λ_k/N0). At low per-eigenvalue SNR the deflection is d² ≈ Σ_k (λ_k/N0)². For energy E spread over L_eff equal eigenvalues, this gives (E/N0)²/L_eff.

The effective number of independent fading cells is L_eff = (Σλ)²/Σλ², which for T ≫ T_c is about T·B_eff [Computed]. Once T ≫ 1/B_eff, the detection time therefore follows T ≈ d²B_eff/(C/N0)². This is the radiometer law with the Doppler bandwidth replacing the receiver bandwidth.

**Optimum diversity (energy per eigenmode).** Richters, *Communication over Fading Dispersive Channels*, MIT RLE Tech. Rep. 464, 1967 [FT, OCR], reproduces the zero-rate result Kennedy obtained (published in his 1969 book). For orthogonal signals over Rayleigh fading, the low-rate (zero-rate) error exponent is maximized when the energy-to-noise ratio per diversity path (eigenvalue) is **z0 ≈ 3.07 (4.9 dB)**.

[Computed] The exponent per unit energy is max_z [2 ln(1+z/2) − ln(1+z)]/z = **0.1488** at z = 3.070. The comparable AWGN zero-rate exponent for orthogonal signals is 0.25, so the gap is about 2.3 dB. At z0 the capacity efficiency η(3.07) is only 0.543. That optimum belongs to uncoded or low-rate reliability, not to capacity, where larger z is always better.

**Matched subspace detectors** (Scharf & Friedlander 1994, IEEE TSP 42(8)) [Mem], with DPSS/Slepian bases (Slepian 1978, BSTJ 57(5)) [Mem]. A Doppler-spread tone over time T occupies about 2WT + 1 ≈ T·B_D + 1 significant Slepian dimensions, where 2W is the Doppler width. Projecting onto a rank-r subspace gives a χ²_{2r} null distribution: an energy detector that is matched to the spread instead of the full bin.

**Optimal per-symbol T·B_D (synthesis).**

1. **For coded capacity:** keep T_s ≲ T_c (L_eff ≈ 1), and make the per-symbol energy (C/N0)·T_s as large as coherence allows. Section 2's MC shows L = 1 is best at M = 1024.
2. **For uncoded or low-rate reliability:** choose T so that the energy per effective mode is about 3, that is, (C/N0)·T/(1 + T·B_eff) ≈ 3.
3. **For presence detection beyond T_c:** use semicoherent combining.

The continuous-gravitational-wave StackSlide literature (Prix & Shaltev 2012, PRD 85:084010 [Abs]) formalizes the optimal segment length at fixed computing cost. In the weak-signal limit, semicoherent sensitivity scales as N_seg^{−1/4}·T_seg^{−1/2}. That scaling is [Computed] from d² ∝ N_seg·(E_seg/N0)²; I could not confirm it in the paper text. It maps directly onto choosing per-segment FFT length versus the number of noncoherent sums in the Matrix template bank.

---

## 6. Quantization at low SNR, and how many bits are needed

**Input or correlator quantization [Computed; reproduces Van Vleck–Middleton and Cooper, and the Thompson, Moran & Swenson *Interferometry and Synthesis in Radio Astronomy* (3rd ed., 2017, ch. 8) values — TMS not retrieved].** The efficiency for weak signals after linear processing (FFT or correlation) is η_Q = (E[x·q(x)])²/E[q²] for a uniform quantizer of a unit-variance Gaussian with the step optimized:

| Levels | η_Q | Loss (dB) | Optimal step (σ) | Mean-shift Fisher | Variance Fisher (step optimized) |
|---:|---:|---:|---:|---:|---:|
| 2 | 0.637 | 1.96 | – | 0.637 | 0 |
| 3 | 0.810 | 0.92 | 1.224 | 0.810 | 0.652 |
| 4 | 0.881 | 0.55 | 0.996 | 0.883 | 0.652 |
| 8 | 0.963 | 0.17 | 0.586 | 0.964 | 0.887 |
| **16 (4-bit)** | **0.9885** | **0.050** | 0.336 | 0.989 | 0.967 |
| 15 (signed [−7, 7]) | 0.987 | 0.056 | 0.354 | 0.988 | 0.963 |
| 32 | 0.9965 | 0.015 | 0.188 | 0.997 | 0.990 |

The 16-level mean-shift Fisher value agrees with the covered note's 0.9887. Oversampling relative to the 2.2 kHz band spreads quantization noise outside the band and improves η_Q further [Mem: TMS]. A clipped or mis-scaled gain costs more than the bit count.

**4-bit templates [Computed].** Quantizing a complex exponential template to integers in [−7, 7] (scale 7) has correlation efficiency **0.997 (−0.013 dB)**. Exact accumulation must be wider than 4 bits (covered note).

**Quantizing energy or LLR outputs [Computed; MFSK capacity with exact bin-LLR decoding, Rayleigh, M = 1024, E_s/N0 = 6 dB, common random numbers against the unquantized 2.863 bit/symbol].**

| Levels on noise-normalized energy | Clip level | Information (bit/symbol) | E_b/N0 penalty |
|---|---:|---:|---:|
| 16 | 12 | 2.834 | **0.045 dB** |
| 16 | 16 | 2.818 | 0.07 dB |
| 16 | 22 | 2.792 | 0.11 dB |
| 8 | 10 | 2.760 | 0.16 dB |
| 8 | 14 | 2.736 | 0.20 dB |
| 4 | 6 | 2.300 | **0.95 dB** |

Four-bit tone-energy outputs are nearly free *if* gain and clip are set well. Two-bit outputs are not. Logarithmic or companded quantizers were not tested.

**Literature [Mem unless marked].**

- **Koch & Lapidoth 2013** (covered). 1-bit symmetric quantization loses 2/π at low SNR. Asymmetric quantizers with flash recover the slope. Noncoherent Rayleigh with 1-bit output has an unavoidable loss.
- **Singh, Dabeer & Madhow**, IEEE TCOM 57(12), 2009 [Mem]. In AWGN, 2–3-bit ADCs lose only a small fraction of capacity at low to moderate SNR, and 1-bit loses 2/π at low SNR. Their precise percentages were not re-read.
- **Mo & Heath 2015**, IEEE TSP [Mem]. 1-bit MIMO with transmitter CSI. Mostly not relevant at HF.
- **LLR and message quantization in decoders**: Rave 2009, IEEE SPL (MI-optimal LLR quantization); Winkelbauer & Matz, SPAWC 2015; Zhao, Zarkeshvari & Banihashemi 2005, IEEE TCOM (quantized min-sum); Lee & Thorpe, ISIT 2005 (4-bit nonuniform BP) [Mem]. The common conclusion is that 3–5-bit messages are within about 0.1–0.2 dB when scaling is good. This is consistent with the computed table above. Verify the specific dB figures before quoting them.

---

## 7. Low probability of intercept / detection (LPI/LPD) observers for FHSS/MFSK

**SNR wall (Tandra & Sahai, IEEE JSTSP 2(1), 2008) [FT].** The radiometer wall is SNR_wall = (ρ² − 1)/ρ, where ρ = 10^{x/10} for x dB of noise uncertainty (eq. 2). For any detector of a white unknown signal, the wall lies between ρ − 1 and (ρ² − 1)/ρ (eq. 8). A matched filter's wall improves in proportion to the coherence time N_c. Feature detectors improve only as √N_c in frequency-selective fading.

[Computed] Radiometer wall:

| Noise uncertainty | Wall | Wall over 2.2 kHz (C/N0) | Wall over a 6.25 Hz bin (C/N0) |
|---:|---:|---:|---:|
| 0.01 dB | −23.4 dB | 10.1 dB-Hz | −15.4 dB-Hz |
| 0.1 dB | −13.4 dB | 20.1 dB-Hz | −5.4 dB-Hz |
| 1 dB | −3.3 dB | 30.1 dB-Hz | +4.6 dB-Hz |

Hopping across the band forces a non-keyed observer back toward the full-band wall, *unless* it uses per-symbol nonlinear statistics (below).

**Square-root law with a shared key and a same-channel warden.** Wang, Wornell & Zheng, "Fundamental limits of communication with low probability of detection," IEEE TIT 62(6), 2016, DOI 10.1109/TIT.2016.2548471 (arXiv:1506.03236) [FT].

- The covertness constraint is D(Q_n‖Q_0^{×n}) ≤ δ, and the warden sees the same channel outputs.
- Transmitter and receiver share a secret key, which they also use for synchronization.
- The maximum is K_n ≈ L·√(nδ). For **real AWGN, L = 1 √nat**, independent of σ² (Thm. 5).
- For discrete memoryless channels, L = max_{P̃} Σ P̃(x) D(W(·|x)‖Q0) / √(½ χ²(Q̃‖Q0)) (Thm. 2, finite when the "off" symbol is not redundant).

Bash, Goeckel & Towsley (covered) and Bloch 2016 (covered) set the key requirement at order √n·log n and order √n bits respectively when the warden's channel is not worse [Mem for exact orders]. Bash, Goeckel & Towsley, "LPD communication when the warden does not know when," ISIT 2014 [Mem], show that uncertainty over T(n) possible slots raises the limit to O(min{√(n log T(n)), n}).

[Computed] For W = 2.2 kHz with n = 2WT real dimensions:

| T | δ | Covert bits | Implied warden-site C/N0 |
|---|---:|---:|---:|
| 1 h | 0.05 nat | ≈1,300 | ≈ −6 dB-Hz |
| 1 h | 0.01 nat | ≈570 | ≈ −10 dB-Hz |
| 1 day | 0.05 nat | ≈6,300 | ≈ −13 dB-Hz |

These numbers require Bob to reach AWGN capacity at that C/N0, which a noncoherent fading receiver cannot do (Section 1).

**Noncoherent fading changes the covert constant [Computed, heuristic extension of WWZ Thm. 2 beyond its proven DMC scope].** For i.i.d. Rayleigh with sparse, keyed on-off symbols of per-symbol energy A, χ²(CN(0, 1+A)‖CN(0, 1)) = A²/(1 − A²). It is **infinite for A ≥ 1**, so the per-fade energy must stay **below 0 dB**. The resulting L ≈ 0.25 √nat at A ≈ 0.65, compared with 1 for AWGN. This conflicts directly with the reception-efficiency requirement x ≳ 10 dB (Section 1).

**Unkeyed observer that knows symbol duration and tone grid but not the tone or hop schedule [derived here; MC values Computed].** The observer's optimal per-symbol likelihood ratio is Λ = (1/M) Σ_j ℓ(r_j), where ℓ is the single-bin likelihood ratio.

- Because data and key make the tone choice uniform, **the key adds nothing beyond the data randomness** unless it hides timing, the tone subset, or frequency offset.
- χ²_symbol = χ²_bin/M exactly, with χ²_bin = I0(2ρ) − 1 for AWGN noncoherent and ρ²/(1 − ρ²) for Rayleigh (infinite at ρ ≥ 1).
- **Low per-symbol SNR:** log Λ ≈ (ρ/M)Σ(r_j − 1) gives d² = Kρ²/M = T_obs·(C/N0)²/B_occ, with B_occ = M/T_s. So **T_obs ≈ d²·B_occ/(C/N0)²**. That is exactly a radiometer over the occupied band: knowing timing and the grid gains nothing.
- **Moderate or high ρ:** the nonlinearity matters a great deal. Monte Carlo D(P1‖P0) per symbol at M = 1024, with the radiometer approximation ρ²/2M in brackets:

| E_s/N0 per symbol | AWGN observer KL (nats/symbol) | Rayleigh observer KL (nats/symbol) |
|---:|---:|---:|
| 0 dB | 6.9e-4 [4.9e-4] | 3.8e-3 |
| 4 dB | 0.0091 [0.0031] | 0.080 |
| 6 dB | 0.078 [0.0077] | 0.42 |
| 8 dB | 0.48 [0.019] | 1.34 |
| 10 dB | 2.18 | – |

Compare the intended receiver's information per symbol (Rayleigh, M = 1024): 0.30 nats at 0 dB, 1.98 nats at 6 dB.

- At the **capacity-efficient operating point (E_s/N0 ≈ 6 dB)**, a timing- and grid-aware observer at the receive site accumulates about **0.21 nats of evidence per nat delivered**. By Stein's lemma, detection at P_FA ≈ 10⁻³ takes on the order of **ln(10³)/0.42 ≈ 16 symbols**.
- At **0 dB per symbol**, the ratio is about 0.013. A 128-bit message then gives the observer only about 1.2 nats, but Bob's E_b/N0 rises to about 3.6 dB.
- The LPD advantage of orthogonal M-ary signaling is a factor of about M **only in the quadratic regime**, which is exactly where noncoherent reception is inefficient. Long-coherence keyed coherent processing (processing gain) is the only way to have both. Its gain is capped by x = (C/N0)·T_c.

**FH-detection literature for observer design [Mem; not retrieved].**

- Dillard 1979, IEEE TAES 15(4) (channelized radiometers).
- Polydoros & Woo 1985, IEEE JSAC (autocorrelation detection of FH).
- Levitt, Cheng, Polydoros & Simon 1994, IEEE TCOM ("Optimum detection of slow frequency-hopped signals"). The average-likelihood detector equals the per-symbol Λ above, and filter-bank combiners approximate it.
- Miller, Lee & Torrieri 1993, IEEE TAES (partial-band coverage).
- Mills & Prescott 1996, IEEE TAES (radiometer models).
- Torrieri, *Principles of Spread-Spectrum Communication Systems*.

**Gap.** I did not survey 2018–2026 covert FH or FSK papers because the search budget was exhausted.

---

## 8. Very large template banks: look-elsewhere and extreme-value thresholds

**Gross & Vitells 2010**, Eur. Phys. J. C 70:525–530, DOI 10.1140/epjc/s10052-010-1470-8 (arXiv:1005.1891) [FT], building on Davies (1977, 1987, Biometrika) [Mem]. For a χ²_s statistic q(θ) scanned over θ ∈ [L, U]:

\[
P\big(\max_\theta q>c\big)\le P(\chi^2_s>c)+\langle N(c)\rangle,\quad 
\langle N(c)\rangle=\frac{c^{(s-1)/2}e^{-c/2}}{\sqrt{\pi}\,2^{s/2}\Gamma(\tfrac{s+1}{2})}\int_L^U C(\theta)\,d\theta .
\]

The practical recipe is to estimate ⟨N(c0)⟩ by counting upcrossings at a low level c0 in a few noise-only runs, then extrapolate:

\[
P\le P(\chi^2_s>c)+\langle N(c_0)\rangle\,(c/c_0)^{(s-1)/2}e^{-(c-c_0)/2}.
\]

The trial factor ≈ 1 + N·√(c/2)·Γ(s/2)/Γ((s+1)/2) grows linearly with the fixed-point significance, where N = (1/√2π)∫C dθ is the "effective number of independent regions". Their toy example: ⟨N(0.5)⟩ = 4.34 gives N = 5.58.

**Matrix-specific form [Computed, Rice envelope upcrossings].** A complex tone-bank output with unit-mean exponential |z(f)|², rectangular window T and a continuous (finely oversampled) frequency scan over band B gives

\[
P_{FA}\approx \langle N(u)\rangle=B\,T\sqrt{\pi u/3}\,e^{-u}\quad\Rightarrow\quad K_{\rm eff}(u)=BT\sqrt{\pi u/3}.
\]

That is 3.2× the Fourier-bin count at u = 10 and 5.1× at u = 25. Fine frequency, time-offset, drift and Doppler scans therefore cost more effective trials than the critically sampled count, and the excess grows with threshold.

Multi-dimensional scans (time × frequency × drift) need the Adler–Taylor/Worsley Euler-characteristic heuristic, P(max > u) ≈ Σ_d 𝓛_d·ρ_d(u), where the leading (volume) term grows like u^{d/2}e^{−u} for χ²₂ fields [Mem]. Gravitational-wave practice fits P(max ≤ u) = F(u)^{N_eff} to noise-only maxima (Wette 2012, PRD 85:042003 [Abs]). Pulsar FFT and acceleration searches set thresholds the same way (Ransom, Eikenberry & Middleditch 2002, AJ 124:1788 [Abs]).

**Recommendation.** Use the union bound ln(K/α) as a conservative design value. Calibrate K_eff by fitting the maximum over the *actual* bank on held-out noise. Also count multi-symbol joint hypotheses (M^N) and keys (Section 4), and apply Gross–Vitells extrapolation to reach α ≈ 10⁻⁶–10⁻⁹ per hour without exhaustive null runs.

---

## 9. Contradictions and nuances

1. **Overspreading pessimism versus critical-bandwidth optimism.** Médard–Gallager and Telatar–Tse say spread signals fail. Lozano–Porrat and Durisi et al. say critical bandwidths are GHz, so practical systems are fine. Both are right; they sit at different x. For weak HF the critical occupancy (δB)* is **tens to a few hundred Hz** (Section 1.6). The pessimistic regime applies to any 2.2 kHz noise-like waveform at C/N0 ≲ 15 dB-Hz. The optimism applies to concentrated-tone MFSK only while x = (C/N0)/B_eff ≳ 10 dB.
2. **Durisi underspread optimism.** "Close to AWGN at practical SNR" assumes broadband-wireless power levels. At C/N0 = 0 dB-Hz with B_D = 1–10 Hz, the exact peak-limited bound in Table A loses 5–16 dB. This does not contradict their theorems; it is outside their numerical regime.
3. **Lower rate does not mean better energy efficiency.** Under a PAR or peak limit, the minimum E_b/N0 occurs at a *nonzero* rate (Gursoy–Poor–Verdú; Stark; Guillén–Grant). For Matrix, slowing the data rate at constant transmit power cannot beat η(x); it only spends time. More energy per coherence cell (longer T_c, narrower Doppler, or more transmit power) is what helps.
4. **Diversity helps reliability, hurts capacity.** Kennedy's optimum of about 3 per mode is about low-rate exponents. The coded-capacity MC shows in-symbol diversity is a loss. Finite-blocklength work (Östman ℓ* ≈ 21; the YDP A* trade) shows codewords still need many *independent* fades. The resolution is to concentrate energy within each fade and spread across many fades. HF slow fading limits the number of fades, so outage dominates short messages.
5. **Coherence-independence in YDP 2016.** The ((ln k)/k)^{1/3} law does not depend on n_c, but only because flash is allowed. Under a peak limit (their eq. 65), energy per fade A, and hence coherence, enters directly.
6. **Noncoherent beats pilots for short packets (Östman 2019)**, whereas Lozano–Porrat use pilot-based non-peaky schemes near-optimally. These are different regimes: short packets with few symbols per coherence block versus long codewords with large B_cT_c.
7. **DSSS versus PPM under uncertainty (Porrat–Tse–Nacu).** Duty-cycled PPM tolerates *less* path growth than DSSS. Peakiness alone is not sufficient; spectral efficiency at the duty-cycled operating point matters too.
8. **LPD versus efficiency.** The square-root law and SNR walls favor spreading energy thinly. Noncoherent fading reception requires concentrating it (x ≳ 10 dB). Under Rayleigh, per-fade energy at or above 0 dB makes χ² infinite. A same-site, timing-aware observer is far less "blind" at E_s/N0 ≈ 6 dB than radiometer scaling suggests (Section 7).
9. **Quantization.** 1-bit is costly, and noncoherent 1-bit is unavoidably lossy (Koch–Lapidoth). 4-bit inputs and templates are nearly lossless (≤ 0.07 dB), but **2-bit energy or LLR outputs cost about 1 dB**, and poor clipping costs more than bit depth.

---

## 10. Decision-relevant synthesis for Matrix Modem

- **Plan on x = (C/N0)·T_c, not C/N0 alone.** Table A is the ceiling. At C/N0 = 0 dB-Hz, B_D = 1 Hz gives 0.29–0.44 bit/s (5–7 dB below AWGN). At +10 dB-Hz and B_D = 10 Hz, the ceiling is 2.9–4.4 bit/s. At −15 dB-Hz, any B_D ≥ 1 Hz leaves the link in the quadratic regime (≤ 10⁻³ bit/s).
- **Large-M constant-envelope FSK is the right structural family** for a peak-limited, CSI-free link: it achieves the β = 1 bound (Section 1.2). Use symbols no longer than about T_c, the largest M the search cost allows (1024–4096), soft energy metrics, and nonbinary or iterative codes. Expect about 1.2–1.5 dB above the ideal bound for Rayleigh at those M.
- **For k ≈ 128-bit messages at ε = 10⁻³, budget about 4 dB over AWGN** even at the best energy per fade, plus outage when fewer than about 50–100 independent fades fit in the message.
- **Multi-symbol phase exploitation** is worth at most about 1.5 dB at M = 1024 (AWGN) and only when N·T_s·B_eff ≪ 1. It multiplies the hypothesis count.
- **4-bit arithmetic** costs about 0.05 dB for inputs, 0.013 dB for templates and about 0.05 dB for energy outputs, provided gain, clip and wide accumulation are right.
- **LPD:** at efficient per-symbol energies, assume a timing-aware observer at the receive site detects within tens of symbols. Real LPD margins come from observer noise uncertainty (SNR walls), timing, frequency and drift search, and observer geometry, not from tone keying.
- **False alarms:** use K_eff = B·T·√(πu/3) for continuous frequency scans and calibrate the maximum over the real bank empirically.

---

## Bibliography (access status as marked in the text)

- Sethuraman, V. & Hajek, B., "Capacity per unit energy of fading channels with a peak constraint," IEEE TIT 51(9), 2005; arXiv:cs/0504085 [FT].
- Sethuraman, V., Wang, L., Hajek, B. & Lapidoth, A., "Low-SNR capacity of noncoherent fading channels," IEEE TIT 55(4), 2009, DOI 10.1109/TIT.2009.2012995; arXiv:0712.2872 [Abs].
- Viterbi, A. J., "Performance of an M-ary orthogonal communication system using stationary stochastic signals," IEEE TIT 13(3):414–422, 1967, DOI 10.1109/TIT.1967.1054039 [Abs].
- Durisi, G., Bölcskei, H. & Shamai, S., "Capacity of underspread noncoherent WSSUS fading channels under peak signal constraints," arXiv:0705.1682 [FT].
- Durisi, G., Schuster, U. G., Bölcskei, H. & Shamai, S., "Noncoherent capacity of underspread fading channels," IEEE TIT 56(1), 2010; arXiv:0804.1748 [Abs].
- Médard, M. & Gallager, R. G., "Bandwidth scaling for fading multipath channels," IEEE TIT 48(4):840–852, 2002, DOI 10.1109/18.992769 [Sec: slides].
- Telatar, I. E. & Tse, D. N. C., "Capacity and mutual information of wideband multipath fading channels," IEEE TIT 46(4):1384–1400, 2000 [FT].
- Subramanian, V. G. & Hajek, B., "Broad-band fading channels: signal burstiness and capacity," IEEE TIT 48(4), 2002 [Abs].
- Verdú, S., "Spectral efficiency in the wideband regime," IEEE TIT 48(6), 2002, DOI 10.1109/TIT.2002.1003824 [Sec].
- Gursoy, M. C., Poor, H. V. & Verdú, S., "On-off frequency-shift keying for wideband fading channels," EURASIP JWCN 2006; arXiv:cs/0510075 [FT].
- Ray, S., Médard, M. & Zheng, L., "On noncoherent MIMO channels in the wideband regime: capacity and reliability," IEEE TIT 2007; arXiv:cs/0603018 [FT].
- Zheng, L., Tse, D. N. C. & Médard, M., "Channel coherence in the low-SNR regime," IEEE TIT 53(3):976–997, 2007, DOI 10.1109/TIT.2006.890777 [Abs].
- Lozano, A. & Porrat, D., "Non-peaky signals in wideband fading channels: achievable bit rates and optimal bandwidth," IEEE TWC 11(1):246–257, 2012 [Abs/Sec].
- Gómez-Cuba, F., Du, J., Médard, M. & Erkip, E., "Unified capacity limit of non-coherent wideband fading channels," arXiv:1501.04905v3 (published version not verified) [FT].
- Porrat, D., Tse, D. N. C. & Nacu, S., "Channel uncertainty in ultra-wideband communication systems," IEEE TIT 2007; arXiv:cs/0509068 [Abs].
- Luo, C., Médard, M. & Zheng, L., "On approaching wideband capacity using multitone FSK," IEEE JSAC 23(9):1830–1838, 2005, DOI 10.1109/JSAC.2005.853803 [Abs].
- Stark, W. E., IEEE TCOM 33(11), 1985, DOI 10.1109/TCOM.1985.1096232 [Abs].
- Guillén i Fàbregas, A. & Grant, A., "Capacity approaching codes for non-coherent orthogonal modulation," IEEE TWC 6(11):4004–4013, 2007 [FT-conf: AusCTW 2006].
- Valenti, M. C. & Cheng, S., IEEE JSAC 23(9):1739–1747, 2005 [Abs/Sec].
- Polyanskiy, Y., Poor, H. V. & Verdú, S., "Minimum energy to send k bits through the Gaussian channel with and without feedback," IEEE TIT 57(8):4880–4902, 2011, DOI 10.1109/TIT.2011.2159041 [Abs/Sec].
- Yang, W., Durisi, G. & Polyanskiy, Y., "Minimum energy to send k bits over multiple-antenna fading channels," IEEE TIT 62(12):6831–6853, 2016, DOI 10.1109/TIT.2016.2615629; arXiv:1507.03843 [FT].
- Östman, J., Durisi, G., Ström, E. G., Coşkun, M. C. & Liva, G., "Short packets over block-memoryless fading channels: pilot-assisted or noncoherent transmission?", IEEE TCOM 67(2), 2019; arXiv:1712.06387 [FT].
- Yang, W., Durisi, G., Koch, T. & Polyanskiy, Y., "Quasi-static multiple-antenna fading channels at finite blocklength," IEEE TIT 60(7), 2014 [Mem].
- Divsalar, D. & Simon, M. K., "Multiple-symbol differential detection of MPSK," IEEE TCOM 38(3):300–308, 1990 [Abs]. Divsalar, Simon & Shahshahani, JPL Publication 89-38, 1989 [FT].
- Colavolpe, G. & Raheli, R., "Noncoherent sequence detection," IEEE TCOM 47(9):1376–1385, 1999 [FT].
- Simon, M. K. & Divsalar, D., IEEE TCOM 41(1):90–98, 1993, DOI 10.1109/26.212365 [Abs].
- Mackenthun, K., IEEE TCOM 42(2/3/4):1471–1474, 1994 [Abs].
- Richters, J. S., *Communication over Fading Dispersive Channels*, MIT RLE TR-464, 1967 [FT].
- Kennedy, R. S., *Fading Dispersive Communication Channels*, Wiley, 1969 [Sec, via Richters].
- Tandra, R. & Sahai, A., "SNR walls for signal detection," IEEE JSTSP 2(1), 2008 [FT].
- Wang, L., Wornell, G. W. & Zheng, L., "Fundamental limits of communication with low probability of detection," IEEE TIT 62(6), 2016, DOI 10.1109/TIT.2016.2548471; arXiv:1506.03236 [FT].
- Gross, E. & Vitells, O., "Trial factors for the look elsewhere effect in high energy physics," EPJC 70:525–530, 2010 [FT].
- Prix, R. & Shaltev, M., PRD 85:084010, 2012 [Abs].
- Wette, K., PRD 85:042003, 2012 [Abs].
- Ransom, S. M., Eikenberry, S. S. & Middleditch, J., AJ 124:1788, 2002 [Abs].
- Items marked [Mem] only: Price 1956; Kailath 1960; Van Trees Part III; Scharf & Friedlander 1994; Slepian 1978; Singh, Dabeer & Madhow 2009; Mo & Heath 2015; Rave 2009; Winkelbauer & Matz 2015; Zhao, Zarkeshvari & Banihashemi 2005; Lee & Thorpe 2005; Dillard 1979; Polydoros & Woo 1985; Levitt et al. 1994; Miller, Lee & Torrieri 1993; Mills & Prescott 1996; Bash, Goeckel & Towsley ISIT 2014; Davies 1977/1987; Adler–Taylor/Worsley EC heuristic; Lancho et al. 2020; Thompson, Moran & Swenson 2017.

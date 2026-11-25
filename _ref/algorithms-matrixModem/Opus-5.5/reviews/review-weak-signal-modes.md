# Empirical anchors from existing weak-signal systems (for Matrix Modem benchmarking)

Compiled 2026-11-24. Scope: documented decoding thresholds converted to C/N0 and Eb/N0 so they can be compared
against each other and against the Shannon limit. This report is for benchmarking a one-way, noncoherent,
tone/MFSK-like HF modem in a 2.2 kHz audio band that receives far below the noise.

## 0. Conventions, arithmetic and access legend

**Conversions used throughout**
- C/N0 [dB-Hz] = SNR_B + 10·log10(B). For B = 2500 Hz that is **SNR_2500 + 33.98**. For B = 3000 Hz it is
  SNR + 34.77, and for 125 kHz it is SNR + 50.97.
- Eb/N0 [dB] = C/N0 − 10·log10(Rb). Rb is the **information** rate: user payload bits (CRC excluded) divided by
  the *whole* transmission duration, sync time included. C is the **total** received power, sync energy included.
  This is the "honest" system Eb/N0. Some authors exclude sync energy; where that matters I say so.
- Gap = Eb/N0 − (−1.59 dB). −1.59 dB is ln 2, the infinite-length, infinite-bandwidth limit for P_e → 0.
- "Message energy" E/N0 = C/N0 + 10·log10(T): the total received energy per message relative to N0. It is the
  quantity that matters for messages lasting minutes to days.
- Unless stated otherwise, WSJT-X thresholds are the **SNR_2500 at 50 % probability of decode on AWGN**
  (definition in [A], [B], [C], [D]). Thresholds from other sources use other criteria; each is labelled.

**Access legend.** Each number carries a source tag. **[P-full]** means I read the full primary text.
**[P-part]** means I read the relevant sections or tables of the primary. **[S]** means secondary: a summary,
an aggregator, a search snippet or a page summarised by a fetch tool. **[C]** means my own calculation from
the cited inputs. The tool's web-search budget ran out late in the work, so some items stay at secondary level
or are marked "not found" (§9).

---

## 1. Master comparison table

The arithmetic columns show C/N0 = SNR + 10log10(B) and Eb/N0 = C/N0 − 10log10(Rb) explicitly. Rb is in bit/s.

| System | Payload / duration → Rb | 10log Rb | Threshold (definition) | C/N0 dB-Hz | Eb/N0 dB | Gap to −1.59 | Fading / Doppler tolerance (documented) | Sources |
|---|---|---|---|---|---|---|---|---|
| FST4W-120 | 50 b / 109.3 s → 0.4575 | −3.40 | −32.8 dB (50 %, AWGN) | −32.8+33.98 = **+1.18** | 1.18+3.40 = **+4.58** | 6.17 | At 2 Hz spectral spread: −24 dB (vs −17 for WSPR). Best when spread ≤ 1/8 of the tone spacing; needs spread < tone spacing | [A][C] P-full; [I] P-full |
| FST4W-300 | 50 / 286.7 → 0.1744 | −7.58 | −36.8 (50 %, AWGN) | −2.82 | **+4.76** | 6.35 | Tone spacing 0.56 Hz. HF 2-hop with standard KiwiSDR already below 90 % decode in quiet conditions | [A][C][I] |
| FST4W-900 | 50 / 887.5 → 0.0563 | −12.49 | −41.7 | −7.72 | **+4.77** | 6.36 | Tone spacing 0.18 Hz; marginal on 20 m 1-hop with standard receivers | [A][C][I] |
| FST4W-1800 | 50 / 1792 → 0.0279 | −15.54 | −44.8 | −10.82 | **+4.72** | 6.31 | Tone spacing 0.089 Hz; [I] expects it to be useful at upper HF only in rare cases (e.g., surface wave with GPSDO at both ends) | [A][C][I] |
| FST4-15 … FST4-1800 | 77 b / 9.6…1792 s → 8.02…0.043 | +9.04…−13.67 | −20.7 … −43.2 | +13.28 … −9.22 | **+4.16 … +4.45** (constant to ±0.15 dB across all T/R) | 5.75–6.04 | Same rule; FST4-15 used on 50 MHz ionoscatter | [A][C] |
| WSPR (WSJT-X decoder) | 50 / 110.6 → 0.4521 | −3.45 | −31 (50 %) [A]; −31.4 in K9AN's figure [I]; −28 for the original WSPR program [Z] | +2.98 | **+6.43** (−31) / +6.03 (−31.4) / +9.43 (−28) | 8.02 / 7.62 / 11.0 | Threshold rises faster than FST4W-120 above 2 Hz spread; **no decode at any SNR at 3 Hz spread** | [A] P-full; [I] P-full; [Z] S |
| WSPR-15 | 50 b at 0.056 bit/s [K] | −12.52 | −38 [K] | −4.02 | **+8.50** (+5 in [K] excluding sync) | 10.1 | not found | [K] P-full (secondary for WSPR-15) |
| FT8 | 77 / 12.6 → 6.111 | +7.86 | −20.8 (50 %, AWGN, block detection + BP+OSD, no AP) [B]; table value −21 [A] | +13.18 | **+5.32** | 6.91 | Mid-lat quiet 0.1 Hz/0.5 ms −20.0; moderate 0.5 Hz/1 ms −18.8; disturbed 1 Hz/2 ms −18.6; **high-lat moderate 10 Hz/3 ms −8.6**. Max AP: −22.7 / −22.4 / −22.1 / −22.1 / −18.9 | [B] P-full |
| FT4 | 77 / 5.04 → 15.28 | +11.84 | −17.5 (50 %, AWGN) | +16.48 | **+4.64** | 6.23 | −17.4 / −15.8 / −15.2 / −12.2 on the same four channels; max AP −19.5 … −17.4 | [B] |
| JT65A (Franke–Taylor soft RS) | 72 / 46.8 → 1.538 | +1.87 | −25 (50 %) | +8.98 | **+7.11** (+4.1 in the paper's convention excluding sync energy) | 8.70 | Spreads of 0.2 and 1.0 Hz carry much smaller penalties for the FT and DS decoders than for hard-decision BM; 0.2 Hz *slightly improved* FT near threshold | [A] P-full; [F] P-full (LyX manuscript) |
| JT65 deep search (hinted) | effectively log2(list) bits, not 72 | — | 58 % at −28; 92 % at −27 (2005 decoder) | ≈ +6 | not comparable (list of about 5850 codewords ≈ 12.5 bits) | — | DS limited by **sync failure** at SNR ≲ −28 dB | [E] P-full; [F] |
| JT9A | 72 / 49.0 → 1.469 | +1.67 | −26 | +7.98 | **+6.31** | 7.90 | JT9B…H (wider spacing): −26 … −20 "when spreading is comparable to tone spacing" | [A] |
| JT4A | 72 / 47.1 → 1.529 | +1.84 | −23 | +10.98 | **+9.14** | 10.73 | JT4B…G: −22 … −17 with spreading comparable to tone spacing | [A] |
| Q65-15A … Q65-300A | 77 / 12.8…293.8 s | +7.79 … −5.82 | −22.2, −24.8, −27.6, −30.8, −33.8 (50 %, AWGN); max AP −23.7, −26.6, −30.2, −32.5, −37.4 | +11.78 … +0.18 | **+3.99 (15A), +4.38, +4.59 (60A), +4.86, +6.00 (300A)** | 5.6–7.6 | "Decoding is effective for signals with Doppler spread up to ten times the tone spacing, and even beyond." Plotted curves for 60D/120E vs 0–100 Hz spread (values not extractable as text) | [A][D] P-full |
| QRA64 | 72 b, ~1 min | — | −26.5 no AP → −30.5 full AP (Palermo, via [L]) | — | — | — | Mentions EME at 4 Hz spread | [L] S |
| MSK144 (single 72 ms frame, coherent OQPSK) | 77 / 0.072 → 1069 | +30.29 | ≈40 % at −1.5 dB; "nearly always" above 0 dB | +32.48 | **+2.19** at the 40 % point | 3.78 | Coherent averaging of up to 7 frames gives up to 8.5 dB | [G] P-part |
| JS8 Slow / Normal / Fast / JS8-40 | 75 b frame; 25.28 / 12.64 / 7.9 / 3.95 s | +4.72 / +7.73 / +9.77 / +12.79 | −28 / −24 / −20 / −18 ("Sensitivity/dB", criterion not stated) | +5.98 / +9.98 / +13.98 / +15.98 | +1.26 / +2.25 / +4.21 / +3.19 | **implausible**, see note | not found | [M] P-full |
| EbNaut (coherent BPSK, K=25 r=1/16 conv. + list Viterbi), W4DEX→UK 8822 Hz | 142.5 b / 24320 s → 0.005859 | −22.32 | measured −56.2 dB in 2500 Hz at a successful decode (ranked 89th in a 50 000 list) | −22.22 | **+0.10** by arithmetic (page states −0.1) | 1.69 | VLF; needs phase-stable paths, GPS-locked TX/RX, known start time | [J] P-full |
| EbNaut, r=1/16 K=25, list 50 000 (lab) | 5–25 chars | — | 63–74 % success at Eb/N0 −1.0; 84–87 % at −0.5 | — | **−1.0** (≥63 %) | **0.59** | — | [J] |
| EbNaut DF6NM→Nicholson 2014, 8270 Hz | 46 b / 7920 s → 0.00581 | −22.36 | −57 [K] | −23.02 | **−0.66** ([K] rounds to −1) | 0.93 | VLF, < 10 µW ERP, 1028 km | [K] |
| Olivia 32/1000 | 5 chars × 7 b / 2 s = 17.5 b/s [O] [C] | +12.43 | −14 dB (Wikipedia; bandwidth not stated); −14 dB error-free pangram on PathSim [N] | +19.98 (if 2500 Hz) | **+7.5** (if 2500 Hz) | 9.1 | 100 % copy at −8 dB on 10 Hz/3 ms and at **−3 dB on 30 Hz/7 ms** (high-lat disturbed) [N] | [O] S; [N] P-full |
| Opera Op2 | 28 info b / ~120 s → 0.233 | −6.32 | ≈ −25.5 dB tone-on power (interpolated 50 % from [L]: 17 % at −26, 66 % at −25) → ≈ −28.5 average (50 % on-off duty) | +5.48 | **+11.8** | 13.4 | not found | [L] S; [K] |
| MIL-STD-188-110C serial (single-tone) 75 bit/s | 75 bit/s | +18.75 | **+2 dB in 3 kHz** for coded BER 1e-5 on a 2-path Rayleigh channel, 5 ms, 5 Hz | +36.77 | **+18.0** | 19.6 | That *is* the fading spec; AWGN figure not in table | [P] P-part |
| MIL-STD-188-110C App. D WBHF WID 0 (Walsh, 75 bit/s in 3 kHz) | 75 | +18.75 | **−6 dB AWGN; −1 dB "Poor"** (ITU-R F.1487 mid-lat disturbed: 2 paths, 2 ms, 1 Hz), BER ≤ 1e-5 | +28.77 / +33.77 | **+10.0 / +15.0** | 11.6 / 16.6 | 5 dB fading penalty | [P] |
| 2G ALE (MIL-STD-188-141A) | — | — | > 50 % linking at −1.5 dB AWGN; +2.5 / +3 dB good/poor | — | not computed | — | — | [Q] S |
| CCSDS turbo r=1/6, k=8920 | (block code) | — | BER 1e-6 ≈ FER 1e-4 at **Eb/N0 ≈ −0.1 dB** (rate 1/2: 1.1 dB) | — | **−0.1** | **1.49** (≈1.0 dB above the rate-1/6 binary-input limit of −1.1) | deep space: AWGN | [R] P-part |
| Voyager 160 bit/s (V2, 1996 DCT, DSS-43 70 m) | 160 bit/s (22.04 dB) | +22.04 | telemetry threshold Eb/N0 2.34 dB (RS(255,223) + (7,1/2) conv.); carrier-loop threshold noise BW 14.8 dB-Hz (2B_L0 ≈ 30 Hz) | received Pt/N0 39.9 (margin 13.3 dB) → threshold ≈ 26.6 [C] | **+2.34** (data channel) | 3.93 | — | [S] P-part |
| GPS L2CL acquisition (Psiaki 2004) | detection only | — | acquired at **C/N0 = 9 dB-Hz**: 2.5 s coherent × 8 noncoherent = 20 s, P_md 1.8e-5, P_fa 1e-4, with Doppler-rate aiding and an OCXO | 9 | E/N0 over 20 s = 9+13.0 = **22.0 dB** [C] | — | 20 ms coherent (CM, data-modulated) would need > 140 s for P_md < 10 % | [T] P-part |
| LoRa SF12 / 125 kHz, CR 4/5 | Rb = 12·(125000/4096)·4/5 = 293 [C] | +24.67 | demodulator SNR limit −20 dB (125 kHz) | −20+50.97 = +30.97 | **+6.30** | 7.89 | — | [V] S |
| Sigfox UL (DBPSK, 100 bit/s, 100 Hz) | 100 | +20.0 | sensitivity −142 dBm | = −142+174−NF = 32−NF (NF not published in sources seen) | **12 − NF** (e.g., 7 dB if NF = 5) [C] | — | 3× time/frequency repetition | [W] S |
| SETI narrowband (turboSETI, Enriquez 2017) | detection only | — | S/N ≥ 25 (search at 20) with δν = 3 Hz, τ = 300 s, drift ±2 Hz/s in 0.01 Hz/s steps | 10log10(25·√(3/(2·300))) = **+2.5** (n_pol = 2) or +4.0 (n_pol = 1) [C] | E/N0 over 300 s ≈ 27.3–28.8 dB [C] | — | Incoherent (tree de-Doppler) sum of 18 s spectra | [U] S |

**Note on JS8.** JS8 Normal uses FT8's 8-GFSK at 6.25 baud and FT8's LDPC family, yet claims −24 dB. That is
3.2 dB better than FT8's measured −20.8 dB [B] with a 75-bit rather than 77-bit payload. JS8 Slow at −28 dB
would imply Eb/N0 ≈ 1.3 dB, about 4 dB better than every measured WSJT 8-FSK mode. The guide gives no
probability criterion and itself calls FT8's −24 dB "theoretical" [M]. Treat the JS8 numbers as nominal, not
measured.

**Key takeaways from the table**
1. The best measured practical noncoherent MFSK systems (FST4/FST4W, Q65-15…120A, FT4, FT8) reach a 50 %
   decode on AWGN at **total Eb/N0 ≈ 4.0–5.3 dB**. That is **5.6–6.9 dB from −1.59 dB**.
2. In AWGN, the FST4 family's Eb/N0 is **constant to ±0.15 dB from 15 s to 1800 s** [C]. The protocol scales
   at exactly 3 dB per doubling of duration. What limits the long submodes is spread and drift, not the
   coding.
3. The coherent long-code EbNaut system on phase-stable VLF reaches **−1.0 to +0.1 dB**, 0.6–1.7 dB from the
   limit. That is about 5 dB better than the best noncoherent MFSK. The price is known timing, known frequency
   and long phase coherence.
4. Every mode with 50–77-bit messages needs a **message energy E/N0 of ≈ 21.6–24 dB** [C]: FST4W ≈ 21.6–21.8,
   FST4 ≈ 23.0–23.3, FT8 24.2, JT65 25.7. EbNaut puts 142.5 bits into about the same energy (21.6 dB). GPS
   acquisition at 9 dB-Hz uses about 22 dB just to *detect* one known signal.

---

## 2. Reference limits I computed (finite message length and noncoherent constraints)

These are my own computations [C], made with pure-Python numerical integration and Monte Carlo. The scripts are
in temporary scratch scripts (`orth.py`, `ncfsk.py`, `ncmin.py`) that are not retained; the orthogonal-signaling values are independently reproduced by `_appraisal/algorithms-matrixModem/calc/finite_energy.cpp`.

**(a) Finite-message benchmark: ML detection of M = 2^k orthogonal signals on AWGN.** This scheme is
achievable. At M = 2^50 it differs negligibly from a simplex code. I did not compute the PPV converse. The
table gives the required Eb/N0 for a given probability of correct message.

| k (bits) | Coherent, P_c = 0.5 / 0.9 / 0.99 | Noncoherent (one unknown phase for the whole message), 0.5 / 0.9 / 0.99 |
|---|---|---|
| 28 (Opera, about 5 EbNaut chars) | −2.11 / −0.36 / +0.85 dB | −1.58 / +0.08 / +1.24 |
| 50 (WSPR / FST4W) | **−1.91** / −0.61 / +0.33 | −1.58 / −0.32 / +0.58 |
| 72 / 77 (JT65 / FT8, FST4, Q65) | −1.83 / −0.74 / +0.05 ; −1.81 / −0.77 / 0.00 | −1.59 / −0.53 / +0.25 ; −1.59 / −0.56 / +0.19 |
| 142.5 (EbNaut 25 chars) | −1.72 / −0.95 / −0.37 | −1.59 / −0.83 / −0.25 |

The implications:
- At the **50 %** criterion used by every WSJT threshold, short messages can sit slightly *below* −1.59 dB.
  "Gap to −1.59" therefore *understates* the true gap at 50 % by about 0.2–0.3 dB.
- Going from 50 % to 99 % costs **≈ 2.2 dB** for k = 50. A design that needs reliable delivery should not
  compare its 99 % point with others' 50 % thresholds.
- This agrees with CCSDS [R] P-part: at WER 1e-4, 1000-bit blocks have "an inherent advantage of about 1.3 dB
  compared to 100-bit blocks". Going to 10 000 bits gives about 0.5 dB more and 100 000 bits about 0.2 dB more.
- EbNaut's sphere-packing discussion [J] is consistent too. Its rate 1/8, K=25 code on 5 characters reaches
  50 % at −1.5 dB with a 50 000 list, against +0.5 dB without list decoding, a 2 dB list gain.

**(b) Modulation-constrained limit for *symbol-by-symbol* noncoherent orthogonal M-FSK.** The model is
independent random phase per symbol with ideal soft (Bessel-likelihood) metrics. The quantity is the minimum
Eb/N0 over all code rates, with infinite code length and data-symbol energy only. These are Monte Carlo
values, accurate to about ±0.1 dB.

| M | min Eb/N0 | at Es/N0 per symbol | rate there |
|---|---|---|---|
| 2 | **6.65 dB** | 3.5 dB | 0.48 b/sym |
| 4 | **4.20 dB** | 4.0 dB | 0.95 b/sym |
| 8 | **2.89 dB** | 4.5 dB | 1.45 b/sym |
| 16 | **2.00 dB** | 5.0 dB | 2.0 b/sym |
| 64 | **1.12 dB** | 6.5 dB | 3.45 b/sym |
| 256 | ≈0.5 dB | 7.0 dB | 4.4 b/sym |

At a *low* rate, symbol-wise noncoherent FSK gets *worse*. At 0.1 b/sym it needs 8.24 dB (M=2), 7.20 dB (M=4)
and 5.90 dB (M=64). This is the classic noncoherent "squaring loss": each symbol must carry an Es/N0 of a few
dB.

At each mode's actual rate per data symbol, the symbol-wise limits are: FST4W 4-FSK at 0.417 b/sym 4.90 dB;
FST4 0.642 b/sym 4.43 dB; WSPR (BFSK after sync) 0.309 b/sym 6.86 dB; FT8 8-FSK 1.328 b/sym 2.91 dB; JT9
1.043 b/sym 3.05 dB; JT65 64-FSK 1.143 b/sym 1.92 dB; Q65 1.222 b/sym 1.84 dB.

**(c) Per-symbol SNR actually used at threshold, and a loss decomposition** [C]. The decomposition is
Es/N0 = C/N0 + 10log10(T_sym), then Eb_data = Es − 10log10(R_sym), and sync loss = −10log10(1 − sync energy
fraction).

| Mode | Es/N0 per symbol at threshold | Total Eb/N0 | Sync loss | Data-energy Eb/N0 | Symbol-wise noncoherent limit at that rate | Residual (finite length, 50 % criterion, decoder; negative = block-detection gain) |
|---|---|---|---|---|---|---|
| FST4W-120 | **−0.48 dB** | 4.58 | 1.25 | 3.33 | 4.90 | **−1.57** |
| FST4-120 | +1.02 | 4.20 | 1.25 | 2.95 | 4.43 | −1.48 |
| FT8 (AWGN) | +5.22 | 5.33 | 1.34 | 3.99 | 2.91 | +1.08 |
| JT9A | +5.58 | 6.31 | 0.92 | 5.40 | 3.05 | +2.35 |
| JT65A | +4.68 | 7.11 | 3.01 | 4.10 | 1.92 | +2.18 |
| Q65-60A | +4.16 | 4.60 | 1.31 | 3.29 | 1.84 | +1.45 |
| WSPR (−31) | +1.32 | 6.43 | n/a (sync shares symbols) | 6.43 | 6.86 | −0.43 |

**Interpretation.** WSJT modes operate at per-symbol Es/N0 between about −0.5 and +5.6 dB. "−45 dB" exists
only in the 2500 Hz reference bandwidth.

FST4/FST4W beat the symbol-wise noncoherent limit by about 1.5 dB because their multi-symbol block detection
exploits phase continuity across symbols on AWGN. [C] states that "FST4-60 is about 1.7 dB more sensitive than
JT9, largely because it uses multi-symbol block detection". [B] measured block-detection gains of 0.7 dB (FT8)
and 1.6 dB (FT4) on AWGN, but only 0.1 dB on the 10 Hz high-latitude channel.

The −1.5 dB residual is therefore a *coherence dividend* that disappears when coherence time is shorter than
the block.

---

## 3. Per-system notes

### 3.1 WSJT-X family

**Common structure [A] P-full.** All modes are fixed-length structured messages with continuous-phase,
constant-envelope FSK and a known sync vector. The payload is 72 bits (JT4/9/65), 77 bits (FST4, FT4, FT8,
Q65, MSK144) or 50 bits (WSPR, FST4W). The S/N threshold is defined as "the signal-to-noise ratio (in a 2500 Hz
reference bandwidth) above which the probability of decoding is 50% or higher".

**FST4 / FST4W** [A][C] P-full. Both use 4-GFSK. A frame is 160 symbols: 120 data symbols of 2 bits each and
five 8-symbol sync groups. Sync energy is 0.25.
- FST4: 77 bits + 24-bit CRC → LDPC (240,101).
- FST4W: 50 bits + 24-bit CRC → LDPC (240,74).
- T/R period, symbol length and tone spacing:

  | T/R | Symbol length | Tone spacing |
  |---|---|---|
  | 15 s | 0.060 s | 16.67 Hz |
  | 30 s | 0.140 s | 7.14 Hz |
  | 60 s | 0.324 s | 3.09 Hz |
  | 120 s | 0.683 s | 1.46 Hz |
  | 300 s | 1.792 s | 0.56 Hz |
  | 900 s | 5.547 s | 0.180 Hz |
  | 1800 s | 11.200 s | 0.089 Hz |

- Occupied bandwidth runs from 67.7 Hz down to 0.36 Hz. Transmission durations are 9.6 to 1792 s.
- Claims in [C]:
  - FST4-60 is 1.7 dB better than JT9, and with AP up to 4.7 dB better.
  - FST4W-120 is about 1.4 dB better than WSPR.
  - Achieving these sensitivities "requires that oscillator drifts and path-induced Doppler shifts must be less
    than the tone spacing, over the full sequence length".
  - "Sensitivity is best when Doppler spread is no more than 1/8 of the tone spacing."
- Other uses: noise blanking at 5–15 % is recommended on LF/MF. VK7MO used FST4W-1800 for 153 km
  non-line-of-sight optical scatter.
- Decoder: LDPC with multi-symbol block detection [C]. AP decoding is available. [C] does not describe the
  BP/OSD details; [B] gives them for FT8. A journal paper specific to FST4 was "in preparation" in [C], and I
  found no publication of it.

**HF evidence for FST4W (Griffiths et al., TAPR DCC 2022)** [I] P-full text:
- FST4W-120 is 1–1.4 dB better than WSPR for spreads < 0.2 Hz.
- At 2 Hz spread FST4W-120 decodes at −24 dB, against −17 dB for WSPR. At 3 Hz WSPR does not decode at any SNR.
  This comes from simulations by K9AN.
- Measured single-tone spectral widths, against the nominal values, were 5.3 / 2.14 / 0.74 / 0.35 mHz for
  -120 / -300 / -900 / -1800.
- A KiwiSDR receiving via browser added a median 0.057–0.67 Hz of spread, depending on buffer setting. Its decode
  rate fell to 23 % at the worst setting.
- A low-cost QDX TCXO receiver had 0.008 Hz σ.
- On 20 m, FST4W-300 over a 2-hop path with a standard KiwiSDR is below 90 % decode even when the ionosphere
  is quiet. FST4W-900 over 1-hop is marginal, and -1800 was not considered.
- The paper derives an explicit "spectral spread budget" (TX + RX + path) for each submode. This is directly
  relevant to a multi-minute Matrix Modem symbol design.

**WSPR** [A] P-full:
- 50 bits → K=32, r=1/2 convolutional code with zero tail → 162 symbols, interleaved. The decoder algorithm is
  not described in [A] and I did not verify it here.
- 4-FSK at 1.4648 baud, 5.9 Hz occupied bandwidth, 110.6 s. Each symbol carries 1 data bit and 1 sync bit, so the
  "sync energy 0.5" is notional: sync costs alphabet, not separate symbol time.
- Threshold: −31 dB for WSJT-X [A]; −31.4 in K9AN's figure [I]. The original WSPR program quoted −28 dB [Z] S.
  An independent simulation (2016) found about 34 % decodes at −30 dB and 6 % at −31 dB [L] S.

**WSPR-15.** PA3FWM gives −38 dB, 0.056 bit/s and +5 dB Eb/N0 excluding sync [K]. I could not find a primary
WSPR-15 specification (search budget exhausted), so the numbers are secondary.

**FT8 / FT4** [B] P-full:
- FT8: 8-GFSK at 6.25 baud, 50 Hz, 79 symbols (58 data + 3×7 Costas), 12.6 s.
- FT4: 4-GFSK at 20.83 baud, 83.3 Hz, 105 symbols, 5.04 s.
- Code: LDPC (174,91), carrying 77 bits + 14-bit CRC.
- Decoding thresholds on AWGN / mid-lat disturbed / high-lat moderate:

  | Decoding | FT4 | FT8 |
  |---|---|---|
  | N=1 BP | −15.3 / −12.7 / −10.4 | −19.6 / −16.5 / fail |
  | Block detection | −16.9 / −14.0 / −10.5 | −20.3 / −17.0 / fail |
  | Block detection + BP + OSD | −17.5 / −15.2 / −12.2 | −20.8 / −18.6 / −8.6 |

- "Doppler spread on the high-latitude disturbed channel exceeds the FT8 tone spacing, and FT8 fails to achieve a
  50% decode probability at any SNR using only the baseline decoding scheme."
- Max AP gives about 2 dB on mild channels, and about 5 dB (FT4) and 10 dB (FT8) on the most disturbed channel.
- Late starts: without AP, transmissions starting more than 5 s late fail. With "AP mycall" (−10 dB, mid-lat
  moderate), up to about 8 s late still decodes.
- Multi-pass decoding subtracts decoded signals and decodes again.

**SuperFox (2024)** [H] P-full. Q-ary polar code, 127 code symbols plus 24 sync symbols, about 11.7 Hz tone
spacing, noncoherent demodulation only.
- Standard FT8 decodes 3–4 dB weaker signals on AWGN and on mid-latitude moderate (0.5 Hz/1 ms).
- SuperFox "degrade[s] less on more difficult propagation paths", because of its wider tones and because "the
  FT8 decoder tries to make use of available inter-symbol coherence, which difficult propagation paths tend to
  destroy".
- On-air SNR reports can be in error by "several dB, especially on the low side".

**JT65** [E] P-full (2005); [F] P-full manuscript of the QEX May/June 2016 paper (LyX source in the WSJT-X
repository; figures not viewed):
- RS(63,12) over GF(64), 65-FSK at 2.692 baud, 63 data + 63 sync symbols (50 % sync energy), 46.8 s.
- 2005 KV decoder, JT65B: 96 % at −23, 41 % at −24, 3 % at −25. JT65A is about 1 dB better than JT65B and JT65C
  about 1 dB worse.
- Deep search, about 2·N_c hypothetical messages from a call list: 92 % at −27, 58 % at −28, 17 % at −29.
  "About 4 dB more sensitivity", which Taylor equates with "schedule gain".
- **Synchronisation:** 93 % at −28, 74 % at −29, 44 % at −30, 19 % at −31. "For the tests … with SNR less than
  about –29 dB, failure to synchronize is the cause of many failures to decode."
- Shorthand two-tone messages: 88 % at −31, 60 % at −32, 26 % at −33.
- Rayleigh fading moved the curves "several dB to the right".
- Franke–Taylor (2016) stochastic-erasure soft decoder:
  - About 2 dB better than BM and 0.2–0.5 dB better than KV.
  - Decodes words with up to 43 of 63 symbols in error.
  - The paper gives SNR_2500 = Eb/N0 − 29.1 dB, with sync energy excluded (Eb = 63Es/72). −24 dB therefore
    corresponds to Eb/N0 = 5.1 dB.
  - With 0.2 and 1.0 Hz Doppler spreads, "penalties are much smaller with both FT and Deep Search" than for BM.
  - DS (list L = 5850) "is limited by synchronization failures when SNR ≲ −28 dB".
- Independent 2016 simulation [L] S: FT decoder threshold about −25 dB; JT65B at 4 Hz spread about −24.5;
  JT9A about −27.

**JT9 / JT4** [A]:
- JT9: K=32, r=1/2 convolutional code, 9-FSK with 1 sync tone at 1.736 baud, 85 symbols, 16 of them sync (0.19).
- JT4: same code, 4-FSK at 4.375 baud with 1 data bit and 1 sync bit per symbol.
- Thresholds −26 and −23 dB, with wider submodes listed for Doppler. These are older, less efficient designs
  (Eb/N0 6.3 and 9.1 dB).

**Q65 / QRA64** [A][D] P-full:
- Code: (65,15) Q-ary repeat-accumulate code over GF(64). Two punctured symbols carry a 12-bit CRC, giving an
  effective (63,13).
- 85 symbols: 63 data and 22 sync ("tone 0"), sync energy 0.26. Submodes A–E use 1–16× tone spacing.
- Threshold table: see §1. Max AP gains 1.5–3.6 dB.
- Quoted claims:
  - "Decoding is effective for signals with Doppler spread up to ten times the tone spacing, and even beyond."
  - It is the most sensitive WSJT-X mode "when path Doppler spread is more than a few Hz".
  - "Highly effective message averaging."
  - A list-decoding technique using previously copied message fragments, with no callsign database.
- Secondary data:
  - Simulations by KA1GT [X] S, Q65-300E at 15 Hz spread with perfect Doppler tracking: −32 dB single-period
    (AP); −35 dB needs about 4 periods; −38 dB needs about 35 periods, roughly 6 h. That is "One dB of improved
    sensitivity when you double the number of signals averaged". Ideal noncoherent integration gives about
    1.5 dB per doubling at low SNR (standard result) and coherent 3 dB.
  - Same source: at 33 Hz spread the 60B/C/D submodes give 98 % at −25 and 0–10 % at −26 (with AP). Choose
    tone spacing ≈ 20–25 % of spread.
  - QRA64, per Palermo via [L] S: −26.5 dB without AP, −30.5 dB with the message fully known.

**MSK144** [G] P-part:
- OQPSK at 2000 baud, LDPC (128,90), 72 ms frames.
- About 40 % of single frames decode at −1.5 dB and nearly all above 0 dB.
- Coherent averaging of N frames gives 10logN, up to 8.5 dB for 7 frames.
- The value here is as a coherent short-frame contrast: Eb/N0 ≈ 2.2 dB at the 40 % point.

**Echo mode:** not a communication protocol, so not analysed (no threshold data collected).

**JS8Call** [M] P-full:
- Speeds and nominal sensitivities: Slow 25.28 s / 25 Hz / 3.125 baud / −28 dB; Normal 12.64 s / −24;
  Fast 7.9 s / −20; JS8-40 3.95 s / −18. Data frames are 75 bits.
- The guide says Slow is "3dB more sensitive than Normal" "based on the modulation bandwidth and error
  correction". These are design statements, not measurements, and are inconsistent with FT8 measurements (see
  the note in §1).

### 3.2 EbNaut (Paul Nicholson; DF6NM, W4DEX and others) [J] P-full (Wayback copy, because abelian.org did not resolve)

- **Signal:** coherent BPSK, UT-synchronous, with TX and RX locked to GPS or an atomic standard.
- **Coding:** source coding at 6 bits per character (52 valid codes, 5.7 bits of information), then a 16-bit CRC,
  then a terminated convolutional code with K up to 25 and rate 1/n (r = 1/16 in the examples), then a
  rectangular interleaver.
- **Decoder:**
  - Soft Viterbi forward pass with Soong–Huang serial list decoding.
  - Lists hold up to 50 000 entries typically, and several million are possible.
  - The CRC and source-code validity select the correct entry.
  - Phase trials run in 30° or 15° steps (≤ 0.3 / < 0.1 dB loss), with split-message phase permutations.
  - Timing must be within 1/10 symbol.
- **Memory and compute:** about 2 GB of trellis memory per character at K=25. The W4DEX decode used 49 GB of RAM
  and 2.5 min on a 32-core Xeon.
- **Measured success, r=1/16 K=25, list 50 000:**

  | Message | Eb/N0 −0.5 dB | Eb/N0 −1.0 dB |
  |---|---|---|
  | 25 characters | 85 % | 63 % |
  | 12 characters | 87 % | 70 % |
  | 5 characters | 84 % | 74 % |

- **List-decoding gain:** for r=1/8 K=25 with 5 characters, 50 % success comes at +0.5 dB without a list and at
  −1.5 dB with a 50 000 list, a 2 dB gain.
- **False-decode budget:** P_f = 1 − (1 − 2^−(c+s))^L, with c the CRC bits and s ≈ 0.3·N_chars. For example,
  50 000 list entries with 5 characters gives 23.7 %. This is the outer-code ceiling on list length.
- **W4DEX → UK on 8822 Hz:**
  - About 150 µW ERP, 25 characters (142.5 bits), 8 s symbols, 3040 coded symbols, 24 320 s (6 h 45 min).
  - Es/N0 −13.2 dB in 0.125 Hz, S/N −22.2 dB in 1 Hz and −56.2 dB in 2500 Hz ("approx −76 dB before sferic
    blanking").
  - Symbol error rate 0.38, ranked 89th of 50 000, 70 % of channel capacity.
  - The page reports Eb/N0 −0.1 dB. Its own carrier numbers give +0.1 dB: −22.22 + 22.32 [C]. This is a
    0.2 dB rounding inconsistency.
- **Sferic blanking:** blanking 25–35 % of samples "gain in signal/noise ratio is around 20dB at 8kHz". This is
  relevant to impulsive HF/LF noise.
- **LF trials (137 kHz):** symbols of 0.1–3 s, messages up to 60 min, more than 7000 km. The first 2-way EbNaut
  contact (2015) was decoded at Eb/N0 from 11 dB down to 1.6 dB.
- **Operational constraint:** the receiver must know the frequency, start time, code and symbol period in advance.
  There is no blind acquisition. "Transmissions must be announced in advance."

### 3.3 Other amateur modes

- **PA3FWM comparison (2015)** [K] P-full, Eb/N0 values excluding WSJT sync energy, 2500 Hz reference:

  | Mode | Threshold | Rate | Eb/N0 |
  |---|---|---|---|
  | SSB voice | +10 dB | — | +31 dB |
  | CW by ear (ZRO test) | −18 dB | 0.54 b/s | +16 dB |
  | QRSS3 by waterfall | −26 dB | — | +14 dB |
  | RSCW | −12 dB | — | +13 dB |
  | OPERA-2 | −23 dB | 0.23 b/s | +14 dB |
  | RTTY | −5 dB | — | +14 dB |
  | PSK31 | −10 dB | — | +9 dB |
  | WSPR | −29 dB | — | +5 dB |
  | WSPR-15 | −38 dB | — | +5 dB |
  | JT65 | −24 dB | — | +5 dB |
  | Coherent BPSK at VLF | −57 dB | — | −1 dB |

  The article points out that a mode working at the lowest SNR is not necessarily the most efficient in Eb/N0,
  and that "Eb/N0 says nothing about how well the mode copes with" fading or impulse noise.
- **Opera** [L] S, [K]:
  - On-off-keyed, about 28 bits (callsign), about 50 % duty cycle. Closed source with no public specification.
  - Measured by EA4GPZ, tone-on power, 100 trials per point:

    | Submode | Trials decoded at three SNRs | Threshold |
    |---|---|---|
    | Op05 | 8 / 22 / 67 % at −20 / −19 / −18 | ≈ −19 |
    | Op2 | 17 / 66 / 92 % at −26 / −25 / −24 | ≈ −25 |
    | Op4 | 3 / 24 / 65 % at −30 / −29 / −28 | ≈ −29 |

  - That is more than 6 dB worse than WSPR/JT9/JT65/QRA64 for Op05. The Opera group claimed −44 dB for Op64,
    which I did not verify.
  - My Op2 Eb/N0 of about 11.8 dB uses average power (tone-on −3 dB).
- **ROS** [AA] S: FHSS/MFSK at about 2.2 kHz, 1–16 baud variants. Only relative claims were found ("same s/n as
  wspr 2" for MF2, a "−25 dB" test video). **No documented threshold with definition found.**
- **Olivia / Contestia / MFSK16 / THOR** [O] S; [N] P-full (K2MO PathSim tests, compiled on the digitalradio
  list; undated; noise bandwidth not stated; criterion "all data (Pangram text) passed … without error"):
  - Olivia 32/1000: 32 tones at 31.25 Hz spacing and 31.25 baud. Each 7-bit character is coded as one of 64 Walsh
    functions, 5 characters per 2 s block, about 17.5 b/s [O]. Threshold −14 dB [O][N].
  - Error-free minimum SNR on a direct path: MFSK4 −16, Contestia 500/32 −15, DominoEX-4 −15, MFSK16 −14,
    THOR11 −14, Contestia 500/16 −13, THOR16 −12, Olivia 500/16 −12, PSK31 −10, MT63 −10/−8, RTTY45 −5 [N].
  - **Fading results [N]:** 100 % pangram copy at −8 dB on high-lat moderate (10 Hz/3 ms) for Olivia 1000/32,
    MFSK16, Contestia 500/16, Olivia 500/8 and 500/16. On high-lat disturbed (30 Hz/7 ms) **Olivia 1000/32 copied
    100 % at −3 dB**. On mid-lat disturbed (1 Hz/2 ms), Thor-11 and Thor-16 copied 100 % at −8 dB, while
    PSK31/DominoEX/FEC-31 gave "No Copy".
  - So wide-tone MFSK with Walsh/orthogonal coding trades about 6–11 dB of AWGN sensitivity for 10–30 Hz spread
    tolerance.

### 3.4 Military and professional HF

- **MIL-STD-188-110C w/Change 1 (2011)** [P] P-part:
  - Serial single-tone 75 bit/s: 8-PSK at 2400 baud with Walsh-like 32-chip repetition, r=1/2 conv. code (K=7)
    and interleaver.
  - Required performance, coded BER 1e-5, SNR measured in 3 kHz, two independent equal Rayleigh paths: 75 bit/s
    needs **+2 dB at 5 ms / 5 Hz**. For comparison, 150 bit/s needs 5 dB, 300 needs 7, 600 needs 7 (2 ms / 1 Hz),
    1200 needs 11 and 2400 needs 18.
  - Embedded-radio tests get +1 dB, and frequency hopping +2 dB.
  - The "robust serial tone mode … shall meet the performance requirements of STANAG 4415". STANAG 4415 is
    restricted; secondary sources say only that its receiver requirements are stricter, with more multipath and
    Doppler spread [AA] S. No numbers found.
- **Appendix D wideband (WBHF; used for 4G/WALE traffic):**
  - Waveform ID 0 uses Walsh orthogonal modulation. It is 75 bit/s in 3 kHz and up to 600 bit/s in wider
    bandwidths.
  - Table D-LII, BER ≤ 1e-5: **AWGN −6 dB, "Poor" −1 dB** (ITU-R F.1487 mid-lat disturbed: 2 Rayleigh paths,
    2 ms, 1 Hz). WID 1 (150 bit/s) needs −3 / +3 dB.
  - Test durations: 60 min (AWGN) and 5 h (Poor).
  - Doppler test (WID 10, long interleaver): acquire and hold for 5 min with 75 Hz offset, 2 ms delay spread and
    1 Hz fading.
- **ALE:** for 2G ALE (MIL-STD-188-141A), 50 % linking at −1.5 dB AWGN and +2.5/+3 dB good/poor [Q] S. For 3G ALE
  (STANAG 4538 BW0–BW5 burst waveforms) and 4G Deep/Fast WALE (MIL-STD-188-141D App. G), **no numeric thresholds
  found** in accessible text. Deep WALE uses about 1.28 s Walsh-coded PDUs (sigidwiki, S).
- **Lesson.** Professional HF waveforms are specified at BER 1e-5 on *fading* channels with a 3 kHz bandwidth
  and coherent PSK with channel estimation. Their Eb/N0 of 10–18 dB is far above the amateur weak-signal modes.
  Their design point is reliability under fading and multipath at 75 bit/s or more, not minimum energy.

### 3.5 Deep space

- **CCSDS 130.1-G-3 (2020)** [R] P-part:
  - Capacity asymptotes for binary-input codes: 0.2 dB (r=1/2), −0.5 (1/3), −0.8 (1/4), −1.1 (1/6), and −1.6 as
    r → 0.
  - Turbo codes with 10 iterations reach BER 1e-6 at −0.1 / 0.15 / 0.4 / 1.1 dB for r = 1/6, 1/4, 1/3, 1/2.
    About the same Eb/N0 gives FER 1e-4.
  - The rate-1/2 turbo code with k = 8920 comes "within 1 dB" of its rate-1/2 limit.
  - Concatenated RS + (15,1/6) conv. needs about 0.8 dB, RS + (15,1/4) about 1.0 dB, and RS + (7,1/2) about
    2.6 dB at BER 1e-6 with depth-5 interleaving.
  - AR4JA LDPC k values {1024, 4096, ∞} are spaced about 0.6 dB on the sphere-packing bound at WER 1e-8.
  - The simulations "assume that there are no synchronization losses". Deep-space links rely on separately
    tracked carriers.
- **Voyager** [S] P-part, DESCANSO 2002, V2 at DSS-43, 1996-01-01, 48.6 AU:
  - X-band Pt/N0 39.9 dB-Hz.
  - Carrier suppression −6.2 dB leaves the residual carrier at 19.0 dB SNR in a threshold loop noise bandwidth of
    14.8 dB-Hz.
  - 160 bit/s data: ST/N0 16.4 dB, less 0.8 dB system loss. The threshold is 2.34 dB, giving a 13.3 dB margin.
    The threshold Pt/N0 is therefore about 26.6 dB-Hz [C].
  - Lifetime table: Voyager 1 at 160 bit/s on 34 m HEF until about 2024, and Voyager 2 until about 2029. Later
    reception needs 70 m or arrays. That is an inference from the table, and I found no 2026 link budget.
  - The 2.34 dB threshold Eb/N0 is 3.9 dB from −1.59. The **residual carrier** costs another 1.25 dB of power
    (data power is −1.25 dB of total).

### 3.6 GNSS high-sensitivity acquisition

- **Psiaki 2004** [T] P-part:
  - L2 CL (dataless) signals at **C/N0 = 9 dB-Hz** were acquired with 2.5 s coherent × 8 noncoherent = 20 s of
    data.
  - P_md = 1.8e-5 at P_fa = 1e-4.
  - This required **Doppler-rate aiding** (known α_D = −6.43 rad/s²) and **OCXO-class clock stability**.
  - With the data-modulated CM code, coherent integration is limited to 20 ms and "over 140 sec of data would
    have to be processed" for P_md < 10 %, P_fa < 0.1 %.
  - So moving from 2.5 s to 20 ms coherence costs at least 7× the time, which is ≥ 8.5 dB of energy, and that
    with *worse* error rates [C].
- **General [S]:** coherent integration on GPS C/A is limited to the 20 ms data bit. Non-coherent post-detection
  integration extends it at lower efficiency.

### 3.7 IoT LPWAN

- **LoRa** [V] S. The Semtech SX1276 datasheet (via mirror and snippet) gives SF12, 4096 chips/symbol and a
  "LoRa demodulator SNR −20 dB". Bit rate SF·BW/2^SF·CR = 293 b/s for CR 4/5 [C]. That gives Eb/N0 ≈ 6.3 dB. The
  datasheet does not state the PER criterion.
- **Sigfox** [W] S. Base-station sensitivity is −142 dBm at 100 b/s (−134 dBm at 600 b/s) with DBPSK in 100 Hz.
  Eb/N0 = 12 − NF dB [C]. I found no published NF.

### 3.8 SETI and pulsar searches

- **Breakthrough Listen, Enriquez 2017** [U] S (ar5iv, via a summariser):
  - GBT L-band, 3 Hz channels, 18 s spectra, 300 s scans. turboSETI incoherent tree de-Doppler over ±2 Hz/s in
    0.01 Hz/s steps (= δν/τ_obs).
  - Detection at S/N 20, ON hits retained at S/N > 25.
  - S_min = SNR·SEFD/δν_t·√(δν/(n_pol·τ)) = 17 Jy (SEFD 10 Jy).
  - Uncorrected drift reduces detectability per bin by (ν̇·τ/δν)^½. The incoherent approach loses sensitivity at
    large drift rates.
  - My conversion is P/N0 = SNR·√(δν/(n_pol·τ)) ≈ 1.8–2.5 Hz, i.e. **C/N0 ≈ 2.5–4 dB-Hz**. Over 300 s that is a
    *detection* energy of about 27–29 dB [C].
  - This is about 12 dB more energy than an idealised single-bin coherent FFT detection would need. The coherent
    detector would need about 15 dB for P_d ≈ 0.5 at P_fa ≈ 1e-14 per trial, under my rough assumptions [C].
  - That illustrates the cost of incoherent processing forced by an unknown drift rate. A coherent 300 s FFT
    tolerates only ≲ 1/τ² ≈ 1e-5 Hz/s of drift.
- **Pulsar acceleration searches** [AB] S. FDAS (Ransom et al. 2002) matched-filters Fourier power over
  acceleration templates (z, w). Surveys such as GBNCC sum up to 16 harmonics incoherently and fold candidates
  above 6σ. I did not find quantitative sensitivity-loss figures (search budget exhausted).

---

## 4. Fading and Doppler evidence, consolidated

| Evidence | Channel | Penalty vs AWGN | Source |
|---|---|---|---|
| FT8 (6.25 Hz tones) | ITU mid-lat quiet 0.1 Hz / moderate 0.5 Hz / disturbed 1 Hz / high-lat moderate 10 Hz | 0.8 / 2.0 / 2.2 / **12.2 dB** (no AP); max AP: 0.3 / 0.6 / 0.6 / 3.8 dB relative to max-AP AWGN | [B] P-full |
| FT4 (20.8 Hz tones) | same | 0.1 / 1.7 / 2.3 / 5.3 dB | [B] |
| FST4W-120 vs WSPR (1.46 Hz tones) | spread 2 Hz | FST4W −24 (≈ 8.8 dB penalty); WSPR −17 (≈ 14 dB); WSPR fails at 3 Hz | [I] |
| FST4/FST4W general rule | — | decode needs spread < tone spacing; best ≤ 1/8 of tone spacing | [C] |
| Q65 | spread up to 10× tone spacing | still decodes; KA1GT finds about 1.5 dB difference between submode choices at 5 Hz | [D] P-full; [X] S |
| JT65 (2.69 Hz tones) | 0.2 / 1.0 Hz | FT and DS penalties "much smaller" than BM; 0.2 Hz slightly helpful; Rayleigh fading shifts curves "several dB" | [F][E] |
| JT65B, QRA64B | 4 Hz | ≈ −24.5 dB (JT65B), ≈ −27 dB (QRA64B, no AP), ≈ −30 dB (with AP) | [L] S |
| Olivia 1000/32 (31.25 Hz tones) | 10 Hz/3 ms; 30 Hz/7 ms | −14 → −8 and −3 dB, i.e. **≈ 6 and 11 dB** penalty | [N] |
| MIL-110C WBHF WID0 | 1 Hz / 2 ms (Poor) | **5 dB** at BER 1e-5 | [P] |
| SuperFox vs FT8 | AWGN, 0.5 Hz/1 ms, worse | FT8 wins by 3–4 dB on easy channels; SuperFox's (noncoherent) degradation grows more slowly | [H] |
| Block detection gain | AWGN vs 10 Hz | +0.7…1.6 dB on AWGN; +0.1 dB at 10 Hz | [B] |
| EME (libration) | — | covered only qualitatively by JT65/Q65 docs ("Rayleigh fading … several dB") | [E][D] |

---

## 5. Where practical weak-signal systems lose dB

Starting point: on AWGN at 50 % decode, the best practical noncoherent MFSK sits at total Eb/N0 ≈ 4–5 dB. The
finite-k 50 % floor for 50–77 bits is about −1.8 to −1.9 dB (coherent) or −1.6 dB (noncoherent). The total
deficit is therefore about 6–7 dB. The evidence attributes it as follows.

1. **Synchronisation overhead: 0.7–3 dB of energy** [A][C]. The sync energy fraction converts to dB as
   −10log10(1 − f):

   | Modes | Sync fraction | Loss |
   |---|---|---|
   | FT4 | 0.15 | 0.71 dB |
   | JT9 | 0.19 | 0.92 dB |
   | FST4 / FST4W | 0.25 | 1.25 dB |
   | Q65 | 0.26 | 1.31 dB |
   | FT8 | 0.27 | 1.37 dB |
   | JT65 | 0.5 | 3.01 dB |

   WSPR and JT4 pay instead in alphabet, using 1 of 2 bits per symbol for sync.

   **Sync also sets hard floors.** JT65 decodes below about −29 dB are mostly sync failures, and DS is limited by
   sync at ≲ −28 dB [E][F]. GNSS acquisition is a pure synchronisation problem and needs about 22 dB of E/N0 at
   9 dB-Hz [T]. PA3FWM notes that sync could be dropped "at the expense of very much searching" [K]. EbNaut
   avoids acquisition entirely by pre-arranged time and frequency [J].

2. **Noncoherent detection: 3–8 dB when symbols are treated independently; ≈ 1.5 dB of that is recoverable
   with inter-symbol coherence.**
   - With symbol-wise soft noncoherent detection, even an infinite code needs at least 4.2 dB (4-FSK), 2.9 dB
     (8-FSK) or 1.1 dB (64-FSK) [C]. At the low code rates of FST4W/WSPR the requirement is 4.9–6.9 dB.
   - WSJT designs work around this by keeping per-symbol Es/N0 near −0.5…+5.6 dB, using long symbols rather
     than spreading energy thinly. They also use block detection where phase is stable (FT8 +0.7, FT4 +1.6,
     FST4 about +1.7 dB vs JT9) [B][C].
   - Larger alphabets help: 64-FSK Q65 and JT65 have low symbol-wise limits, but pay in bandwidth and in a weaker
     or RS code.
   - Fully coherent EbNaut gets within about 0.6 dB (at ≥ 63 % success) [J]. MSK144 shows coherent averaging
     giving the full 10logN [G].
   - Noncoherent averaging of repeats gains only about 1 dB per doubling in Q65 practice [X], against 3 dB
     coherent. GNSS shows that limiting coherence from 2.5 s to 20 ms costs ≥ 8.5 dB [T][C].

3. **Finite length and decoder: about 1–2.5 dB at 50 %, more at high reliability.**
   - After removing sync, the residual above the symbol-wise noncoherent limit is +1.1 dB (FT8), +1.45 dB (Q65)
     and +2.2–2.4 dB (JT65 RS, JT9 conv.). It is negative for FST4/FST4W because block detection more than
     compensates [C].
   - Decoder improvements are worth single dB: OSD +0.5–0.6 dB [B]; FT vs BM about 2 dB and vs KV 0.2–0.5 dB
     [F]; list decoding +2 dB [J].
   - Raising reliability from 50 % to 99 % costs about 2.2 dB at k = 50 [C]. CCSDS puts the 100→1000-bit block
     advantage at about 1.3 dB [R].
   - **A-priori information** ("schedule gain") is the largest single lever for short messages. It is worth
     +1.5–3.6 dB on AWGN (Q65 [D], FT8 about 2 dB [B]), +5…10 dB on disturbed channels [B], and +4 dB for
     JT65 DS [E]. The cost is more false decodes; the EbNaut appendix quantifies that via P_f(L).

4. **Fading and Doppler: 0–12 dB, set by spread relative to tone spacing.** See §4. The single most predictive
   parameter is spread/tone-spacing [B][C][I].
   - Below about 1/8 the penalty is small (≲ 1 dB). Near 1 the penalty is large. Above 1 the mode fails unless
     it is designed for it (Q65; Olivia's wide tones).
   - **Long-period modes are spread-limited in practice.** FST4W-900/1800 gains are rarely usable on HF, and
     equipment spread (SDR buffering, TCXO drift) can consume the whole budget [I].
   - Multipath delay matters little when it is much shorter than the symbol [B].

5. **Measurement and reporting.** On-air SNR estimates near threshold can be off by "several dB" [H]. Nominal
   thresholds (JS8, some Opera claims) are not measurements. Threshold criteria also differ: 50 % (WSJT), 40 %
   (MSK144), "error-free pangram" (K2MO), BER 1e-5 (MIL), FER 1e-4 (CCSDS), and detection P_d/P_fa (GNSS, SETI).
   Compare like with like.

---

## 6. Recent (2018–2026) work on ultra-weak modes and decoders

| Work | Year | Claimed or measured gain | Access |
|---|---|---|---|
| FT8/FT4 protocol paper (Franke, Somerville, Taylor, QEX) | 2020 | Block detection + OSD: +1.2 dB (FT8), +2.2 dB (FT4) on AWGN; AP up to 10 dB on disturbed channels | [B] P-full |
| FST4 / FST4W (WSJT-X 2.3) | 2020 | 1.7 dB over JT9 (4.7 with AP); 1.4 dB over WSPR; FST4W-1800 at −44.8 dB | [C] P-full |
| Q65 (QRA codes, Palermo; WSJT-X 2.4) | 2021 | Best WSJT-X mode for spread > few Hz; AP +1.5…3.6 dB; averaging | [D] P-full |
| FST4W on HF (Griffiths, Elmore, Robinett, Rhymes, Watrous; TAPR DCC) | 2022 | Quantified spectral-spread budgets; FST4W-120 −24 vs WSPR −17 at 2 Hz; WsprDaemon multi-mode decode | [I] P-full |
| SuperFox (Q-ary polar code) | 2024 | Throughput mode; 3–4 dB *less* sensitive than single FT8 but more spread-tolerant | [H] P-full |
| Kozlenko, NN demodulation of JT65A-like MFSK (IEEE conf. 2020; arXiv 2025) | 2020/2025 | CNN about 1.5 dB *worse* than the theoretical noncoherent orthogonal-MFSK limit; no gain over classical demodulation reported | [Y] S |
| EbNaut (coherent BPSK + long-K list Viterbi) | 2014–2016+ | −1 dB Eb/N0 at 63–74 % success; transatlantic below 9 kHz | [J] P-full |

**Not found** (search budget exhausted, so this is not proof of absence):
- a peer-reviewed deep-learning FT8 or WSPR decoder with a demonstrated threshold gain;
- a published "coherent WSPR" or coherent-integration FST4W decoder;
- HamSCI beacon-mode threshold papers beyond [I];
- a 2024–2026 QEX paper on a new HF weak-signal decoder.

---

## 7. Quantitative anchors most relevant to Matrix Modem

1. **Targets.**
   - Matching today's best noncoherent MFSK on AWGN at 50 % decode means **total Eb/N0 ≈ 4.2–4.8 dB**, the
     FST4/FST4W level.
   - The best modulation-constrained (symbol-wise) designs with long codes could reach about 1–3 dB, which needs
     larger alphabets and/or multi-symbol coherence.
   - Approaching −1 dB has been demonstrated only with coherent signalling over phase-stable paths with known
     timing (EbNaut).
2. **Energy per message.** About 21.6 dB of E/N0 has been needed for 50 bits (FST4W) and about 23 dB for 77 bits
   (FST4) at 50 %. For example, a received C/N0 of −10 dB-Hz requires about 10^(3.16) ≈ 1450 s for a 50-bit
   message at FST4W efficiency [C].
3. **Per-symbol SNR.** Practical noncoherent designs keep per-symbol Es/N0 between about −0.5 and +6 dB at
   threshold. The noncoherent optimum is +3.5…7 dB per symbol for M = 2…256.
4. **Spread and drift budget.** Tone spacing must exceed the total TX + path + RX spread, ideally by 8×.
   FST4W-1800's 0.089 Hz spacing implies a ≲ 11 mHz spread for best sensitivity [C, from the 1/8 rule]. That is
   rarely available on HF skywave [I].
5. **Sync and acquisition.** Budget 0.7–1.4 dB of energy for embedded sync (WSJT practice). Expect
   sync/acquisition, not FEC, to set the floor once codes are strong (JT65 evidence). Blind search over
   time/frequency/drift adds trial-count-driven threshold costs (SETI, GNSS).
6. **Reliability criterion.** State thresholds at 50 % *and* at 90/99 %. For k = 50 the theoretical difference is
   about 1.3 dB (50→90 %) and 2.2 dB (50→99 %) [C].

---

## 8. Source list

- [A] WSJT-X User Guide, Protocol Specifications (protocols.adoc, master branch on SourceForge). P-full.
  https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/doc/user_guide/en/protocols.adoc. Also WSJT-X 2.7.0 guide
  intro (WSPR −31 dB, FST4W −45 dB): https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.html
- [B] S. Franke, B. Somerville, J. Taylor, "The FT4 and FT8 Communication Protocols," QEX July/Aug 2020.
  P-full. https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf
- [C] Franke, Somerville, Taylor, "Quick-Start Guide to FST4 and FST4W." P-full.
  https://wsjt.sourceforge.io/FST4_Quick_Start.pdf
- [D] Taylor, Somerville, Franke, Palermo, "Quick-Start Guide to Q65," 2021. P-full; figure values not extracted.
  https://wsjt.sourceforge.io/Q65_Quick_Start.pdf
- [E] J. Taylor, "The JT65 Communications Protocol," 2005. P-full. https://wsjt.sourceforge.io/JT65.pdf
- [F] S. Franke, J. Taylor, "Open Source Soft-Decision Decoder for the JT65 (63,12) Reed-Solomon Code," QEX
  May/June 2016. Read as the LyX manuscript in the WSJT-X repository, lib/ftrsd/ftrsd_paper/ftrsd.lyx
  (P-full text, figures not viewed).
- [G] Franke, Taylor, "The MSK144 Protocol for Meteor-Scatter Communication," QEX Sep/Oct 2017. P-part.
  https://ARRL.org/files/file/QEX_Next_Issue/SeptOct2017/FrankeTaylor.pdf
- [H] Taylor, Franke, "SuperFox and FT8: Weak-Signal Performance," 2024. P-full.
  https://wsjt.sourceforge.io/SuperFox_Performance.pdf
- [I] Griffiths, Elmore, Robinett, Rhymes, Watrous, "FST4W on the HF Bands," TAPR DCC 2022. P-full text.
  https://web.tapr.org/tech_docs/DCCpapers/2022%2002.pdf
- [J] P. Nicholson, "EbNaut Coherent BPSK," main page, technical notes and appendix (abelian.org/ebnaut).
  P-full via Wayback copy (2024).
- [K] P.-T. de Boer PA3FWM, "Signal/noise ratio of digital amateur modes," 2015. P-full.
  https://www.pa3fwm.nl/technotes/tn09b.html
- [L] D. Estévez EA4GPZ, destevez.net ?p=1677 (2016 JT-mode simulations) and ?p=1942 (Opera). S (fetched via
  summariser).
- [M] JS8Call User Guide (JS8Call-improved). P-full.
  https://js8call-improved.github.io/downloads/JS8Call_User_Guide.pdf
- [N] A. Bombardiere K2MO, path-simulator tests (AE4JY PathSim), digitalradio list attachment. P-full; undated.
  https://www.mail-archive.com/digitalradio@yahoogroups.com/msg18007/Path_Simulator_Tests.rtf
- [O] Wikipedia, "Olivia MFSK" (rev. Nov 2025). S.
- [P] MIL-STD-188-110C w/Change 1 (23 Sep 2011): Table XVI, Table D-II, Table D-LII. P-part.
  https://www.sigidwiki.com/images/c/c8/MIL-STD-188_110C_CHG_NOTICE-1.pdf
- [Q] RadioReference wiki, MIL-STD-188-141A. S.
- [R] CCSDS 130.1-G-3 (June 2020; EC1 2024), TM Synchronization and Channel Coding — Summary of Concept and
  Rationale, §3.3, §7.4, §8.1. P-part. https://public.ccsds.org/Pubs/130x1g3e1.pdf
- [S] R. Ludwig, J. Taylor, "Voyager Telecommunications," DESCANSO Design & Performance Summary Series, 2002,
  Tables 1-1, 5-2 to 5-4. P-part. https://descanso.jpl.nasa.gov/DPSummary/Descanso4--Voyager_new.pdf
- [T] M. Psiaki, "FFT-Based Acquisition of GPS L2 Civilian CM and CL Signals," ION GNSS 2004. P-part.
  https://gps.mae.cornell.edu/psiaki_iongnss2004.pdf
- [U] Enriquez et al., ApJ 849:104 (2017), arXiv:1709.03491. S (ar5iv via summariser).
- [V] Semtech SX1276 datasheet (mirror). S (snippet). https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf
- [W] Sigfox sensitivity figures from technical overviews and comparison tables. S (snippets).
- [X] B. Atkins KA1GT, Q65 mode selection and 300E averaging simulations (bobatkins.com). S.
- [Y] M. Kozlenko, arXiv:2502.19097 and arXiv:2502.16371. S (abstracts).
- [Z] C. Milazzo KP4MD, "Interpreting WSPR Data for Other Communication Modes," 2013 (WSPR −28 dB). S.
  http://www.qsl.net/kp4md/wsprmodes.htm
- [AA] sigidwiki (ROS, STANAG 4415, 4G ALE); cartoonman STANAG-4415 page; i56578-swl blog. S.
- [AB] Pulsar FDAS and harmonic-summing figures (GBNCC survey arXiv:1406.5214 and others), via search snippets. S.

## 9. Gaps (not found or not verified)

- **ROS:** no defined threshold found.
- **WSPR-15:** no primary specification found.
- **STANAG 4415:** the performance annex is restricted.
- **3G ALE (BW0–BW5) and 4G Deep/Fast WALE:** no numeric thresholds found.
- **Sigfox:** base-station noise figure not found.
- **Q65:** threshold-vs-spread curve values not extracted.
- **JT65:** fading-curve values not extracted (figure only).
- **Pulsar acceleration searches:** no sensitivity-loss numbers found.
- **Voyager:** no current (2026) link budget found.
- **Echo mode:** omitted.
- **Not found:** deep-learning decoders with a demonstrated gain, and coherent WSPR variants.
- The WebSearch budget was exhausted before these could be pursued.

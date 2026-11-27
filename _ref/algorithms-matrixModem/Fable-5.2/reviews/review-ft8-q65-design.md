# FT8 and Q65 design evidence for a keyed, hopped, one-way MFSK modem: primary-source and literature review

26 November 2026 · Fable-5.2 review · research material (retained under the
[research-material rule](../../../../docs/development.md#research-material))

**Status.** Literature, documentation and source-code review with reviewer arithmetic and three small
theoretical calculations (Appendix A). The WSJT primary sources and source code were read by the
reviewer; four bounded sub-questions (field evidence, coding literature, military and amateur MFSK
designs, low-power records) were searched by delegated agents and are marked. Nothing here is a Matrix
Modem measurement, a sensitivity result or a capacity claim. The web-search budget for this pass ran out
before all follow-ups were made; §10.6 lists what was not pursued.

## 0. Already established versus new here

| Topic | Already established, and where | New, sharper or contradicting here |
|---|---|---|
| FT8/FT4 frame and thresholds | 79 symbols, 3×7 Costas, LDPC(174,91)+CRC14, −20.8 dB AWGN and the five-channel table: [Opus review §1, §3.1](../../Opus-5.5/reviews/review-weak-signal-modes.md), [anchors](../../Opus-5.5/04-empirical-weak-signal-anchors.md) | Exact decoder pipeline from source (§1.3–1.6): search grids, metric normalisation, four LLR sets, BP→OSD coupling, false-decode gates, "a7" list decode, subtraction tracker bandwidth. FT8 at 10 Hz spread has a **decode-probability ceiling near 0.66**, not a 12 dB penalty (§1.7). |
| FT8 waveform | "Gaussian smoothed, not rectangular tones": [Sol waveform review](../../Sol-6.2/waveform-and-repetition-review.md) | BT = 2 costs **0.05 dB** and leaves a −32 dB/bin self-interference floor; the same BT costs **2.4 dB** when tones hop across 448 bins (§1.2, §6.2, Appendix A.2) [derived]. |
| Sync overhead | 0.7–3 dB by mode; JT65 sync-limited below −28 dB: Opus review §5 | Costas ambiguity counts; Q65 synchronises on **all 85 symbols** when a message list exists; Costas peak degraded at 100 Hz spread; small-signal cost of data-directed sync (§1.3, §3.5, §6.3). |
| Block detection | +0.7 (FT8), +1.6 dB (FT4) on AWGN, +0.1 dB at 10 Hz: anchors §Loss decomposition | Gain versus spread × block duration, and the capacity numbers for N = 1/2/4 CPFSK blocks (§1.4, §4.3). |
| Bit versus symbol metrics | CM over BICM by 0.5 dB (4-FSK) and 1.9 dB (64-FSK) at rate 1/3, from secondary citation: [Sol practice §Coded noncoherent MFSK](../../Sol-6.2/mfsk-alternatives-practice-and-computation.md) | Values at the actual FT8 and Q65 rates, for M = 8…256, AWGN and Rayleigh: **0.9 dB (8-FSK), 2.5 dB (64-FSK)**; with bit metrics the best alphabet is 8–16, with symbol metrics it keeps improving (§4.2) [derived]. |
| Q65 thresholds and metric | AWGN table, weights `g·Es/N0/(1+g·Es/N0)`, 9 % table steps, digitised Fig. 1: Sol practice §Q65 | **Two Fig. 1 curves were mislabelled in the Sol table** (§7). Fig. 2 values for Q65-60A; hypothesis grid, list-decode thresholds, sync search, averaging rule, drift search from source (§3). |
| Spread versus tone spacing | "Spread/tone spacing is the predictive parameter"; 1/8 rule: Opus review §4–5 | One normalised table across FT8, FT4, FST4W, WSPR, Q65, JT65, Olivia, STANAG 4415; cliffs versus graceful slopes; FST4W fails near spread ≈ spacing even at −15 dB (§2.1–2.3). |
| HF spread statistics | F.1487 classes; DAMSON ranges: [Sol channel reviews](../../Sol-6.2/hf-channels.md) | Measured 14 MHz FST4W spectral spreads by mode (8 mHz … 0.65 Hz) and Doppler shifts (§2.2). |
| Drift and timing | FST4 rule "drift < tone spacing"; SETI/GNSS search analogues: Sol practice | FT8 measured drift tolerance (0.08 Hz/s at threshold), ±2.5 s lag search, 36 % erasure tolerance at about 9 dB margin (§1.8). |
| STANAG 4415 | −7 dB, 40 Hz, ~12 ms (FFI); primer values: Sol practice §Military HF | Vendor-transcribed mandatory table: 10 ms, 0.5–50 Hz at −1…0 dB; **"16 ms" not found in any source**; the 75 bit/s waveform is 4-ary Walsh (§4.5). |
| Olivia family | PathSim pass/fail points: Opus review §3.3 | Primary structure (64-symbol blocks, Walsh per bit-plane), designer rules, extra PathSim points (§4.6). |
| Low-power records | none in these five documents | Documented 50 µW–100 mW WSPR/QRSS cases, implied system loss and the C/N0 floor of all of them (§5). |
| Citations | — | DOI `10.1109/TWC.2007.06040154` is Guillén i Fàbregas & Grant, not Cheng, Valenti & Torrieri (§7). |

**Labels.** Each number carries one of: **[PS]** published simulation; **[M]** measured (who and what is
stated); **[DOC]** documentation or developer statement; **[SRC]** read in WSJT-X source code (master
branch on SourceForge, fetched 26 November 2026; an implementation fact, not a performance result);
**[SPEC]** specification requirement; **[V]** vendor claim; **[A]** anecdotal; **[D]** derived here
(theoretical, assumptions stated); **[H]** heuristic inference by the reviewer. "(deleg.)" marks material
read by a delegated research agent and relayed, not seen verbatim by the reviewer.

**Conversions [D].** WSJT SNR is in 2500 Hz. `C/N0 = SNR + 33.98 dB-Hz`. `Es/N0 = C/N0 + 10·log10(T_sym)`.
`Eb/N0 = C/N0 − 10·log10(R_b)`, with `R_b` the payload bits over the whole transmission and `C` the total
power including sync. Military SNRs are in 3 kHz (`+34.77`). All WSJT thresholds are 50 % decode
probability unless stated.

---

## 1. FT8: exact engineering and the reason for each choice

Sources: the protocol paper [S1], the WSJT-X source [S2], the user guide and release notes [S5].

### 1.1 Parameters

| Element | Value | Stated or evident reason | Label |
|---|---|---|---|
| Slot | 15 s T/R, nominal start 0.5 s, 79 symbols × 0.160 s = 12.64 s | Four-message contacts in about a minute instead of four or more with 60 s modes; about 4 dB less sensitive than JT65 accepted for that. Target use was multi-hop sporadic-E with weak, fading signals and short openings [S14, deleg.] | [DOC] S1 §1 |
| Tones | 8-CPFSK, modulation index 1, 6.25 baud, 6.25 Hz spacing, 50 Hz occupied | Spacing = baud is the minimum orthogonal spacing for noncoherent detection | [DOC] S1 Table 4 |
| Shaping | Gaussian-smoothed frequency pulse, BT = 2 (FT4: BT = 1); amplitude ramp only at start and end, raised cosine over T/8 = 20 ms | Lower spectral sidelobes so dozens of signals share a few kHz; inter-symbol interference is deliberate and limited to adjacent pulses; constant envelope avoids intermodulation in nonlinear amplifiers | [DOC] S1 §5 |
| Mapping | Gray code, tones 0–7 ↔ 000, 001, 011, 010, 110, 100, 101, 111 | Adjacent tones differ in one bit, which helps when Doppler spread is comparable to the tone separation | [DOC] S1 §4 |
| Sync | 7×7 Costas array 3,1,4,0,6,5,2 at symbols 0–6, 36–42, 72–78 | Time and frequency synchronisation from one pattern; three copies also survive late starts | [DOC] S1 §4 |
| Sync energy | 21/79 = 26.6 %, i.e. 1.34 dB of total energy | — | [D] |
| Code | 77 bits + CRC-14 (polynomial 0x6757, computed over the message zero-extended to 82 bits) → LDPC (174,91), rate 0.523; every bit in 3 checks, checks of 6–7 bits | Code designed for this mode by the authors; CRC controls false decodes | [DOC] S1 §3; [SRC] `decode174_91.f90` |
| Data symbols | 174/3 = 58 | — | [DOC] |
| Rates | `R_b` = 77/12.64 = 6.09 bit/s; 1.328 payload bits and 1.569 code-input bits per data symbol | — | [D] |

The original 2017 FT8 used a different (flipped) Costas array; the source still carries the comment that
the array is flipped relative to the original [SRC] `ft8b.f90`. (Its 75-bit payload is recalled by the
reviewer and was not re-verified.)

### 1.2 Gaussian shaping: what BT = 2 does

Calculation A.2 generates the paper's GFSK waveform and measures a rectangular one-symbol DFT, which is
what the WSJT-X detector uses (§1.4). Results for random 8-tone data, T = 0.16 s [D]:

| Pulse | Energy in wanted bin | Mean leakage into each of the six nearest bins | 99 % occupied bandwidth | Power beyond band edge + 25 Hz / + 100 Hz / + 300 Hz |
|---|---:|---:|---:|---:|
| Rectangular (plain CPFSK) | 0.00 dB | none (orthogonal) | 51.6 Hz | −32.6 / −45.6 / −56.6 dB |
| BT = 2 (FT8) | −0.05 dB | −32 dB | 50.0 Hz | −56.8 / below −130 / below −130 dB |
| BT = 1 with 8 tones | −0.38 dB | −20 dB | 48.4 Hz | −82.3 / −130 / below −130 dB |
| BT = 1 with 4 tones (FT4, normalised) | −0.10 dB | −26 dB | 25.0 Hz (4-tone span 18.8 Hz) | −85.3 / below −130 / below −130 dB |

"Band edge" is the outermost tone centre. Assumptions: ideal transmitter, random independent symbols,
Hann-windowed periodogram over 8-symbol segments, double precision.

Consequences. (a) The shaping barely changes the 99 % bandwidth. Its value is the far-out skirt:
24 dB less power beyond 25 Hz and more than 80 dB less beyond 100 Hz, which matters when a neighbour is
50 dB stronger. (b) The receiver can ignore the shaping: the loss is 0.05 dB and the self-interference
floor of −32 dB per bin is far below noise at the operating point (`Es/N0` ≈ +5 dB). (c) The cost grows
with the frequency excursion per transition: BT = 1 costs 0.10 dB with 4 tones and 0.38 dB with 8. §6.2
carries this to wide hops.

### 1.3 Acquisition: what the Costas arrays buy

Coarse search [SRC] `sync8.f90`, `ft8_params.f90`:

- Power spectra from 2× zero-padded FFTs (3.125 Hz bins) stepped every quarter symbol (40 ms).
- For every frequency bin and every lag within ±62 steps (±2.48 s) of the nominal start, sum the power in
  the 21 Costas cells and divide by the mean power in the other six tone cells of the same symbols. The
  metric is therefore self-normalised to local noise and interference.
- A second metric uses only the middle and last arrays, and the larger of the two is kept. This is what
  lets late-starting transmissions synchronise.
- Per frequency, the best lag within ±0.4 s and the best within ±2.48 s are both kept. Values are
  normalised by the 40th percentile across frequency. Candidates need a normalised metric of at least 1.3
  (deepest setting), 1.6 (normal) or 2.0 (early decode); at most 600 candidates; near-duplicates within
  4 Hz and 40 ms are merged.

Fine search [SRC] `ft8b.f90`: mix to baseband and decimate to 200 Hz (32 samples per symbol); time ±10
samples in 5 ms steps; frequency ±2.5 Hz in 0.5 Hz steps; time again ±4 samples. Then a hard check: the
strongest of the eight bins must equal the Costas tone in **at least 7 of the 21** sync symbols, or the
candidate is dropped.

Ambiguity [D] (Appendix A.1). One 7×7 array has at most 1 coincidence of 7 for any non-zero time and
frequency shift (126 shifts give 0, 42 give 1). For the whole frame the largest false alignments are 14
of 21 at ±36 symbols (±5.76 s) and 7 of 21 at ±72 symbols; within the ±2.5 s search window the worst is
3 of 21. Repeating one array three times therefore creates large sidelobes, and the bounded lag search
is what excludes them. Grid losses after fine search are small: a 0.25 Hz residual costs 0.02 dB and a
2.5 ms residual 0.14 dB [D].

Costas arrays resolve time and frequency jointly, where a single tone or a linear sweep leaves a ridge
[S26]. Their weakness is frequency spread: on a 10 GHz EME recording with about 100 Hz spread, the Costas
correlation peak of QRA64 was reported as not clearly evident, with many secondary peaks, until a
spread-matched metric was used [DOC] S6 slides 24–25. Q65 dropped Costas arrays for a single sync tone
(§3.5).

### 1.4 Symbol detection and bit metrics

[SRC] `ft8b.f90`; [DOC] S1 §6.

- One 32-point FFT per symbol on the 200 Hz stream gives eight complex bin values. This is a rectangular
  matched filter; the Gaussian shaping is not compensated.
- Bit metric (max-log on magnitudes): for bit `j`, the largest correlation magnitude among hypotheses with
  the bit set minus the largest among those with it clear.
- Four sets of 174 metrics are built: N = 1 (8 hypotheses per symbol), N = 2 (64 two-symbol sequences),
  N = 3 (512 three-symbol sequences), and N = 1 again with each bit normalised by the larger of its two
  maxima. Multi-symbol correlations are formed by adding the single-symbol complex bins coherently; no
  further FFTs are needed. Blocks do not straddle the two 29-symbol halves. Phase continuity between
  blocks is not used.
- Each set is scaled to unit standard deviation and multiplied by 2.83 before decoding.

Published thresholds by decoder stage [PS] S1 Table 5 (SNR in 2500 Hz; `C/N0` adds 33.98):

| Decoder | FT8 AWGN | FT8 mid-lat. disturbed (1 Hz, 2 ms) | FT8 high-lat. moderate (10 Hz, 3 ms) | FT4 AWGN | FT4 1 Hz | FT4 10 Hz |
|---|---:|---:|---:|---:|---:|---:|
| N = 1, BP | −19.6 | −16.5 | no 50 % point | −15.3 | −12.7 | −10.4 |
| N = 1,2,3 (FT4: 1,2,4), BP | −20.3 | −17.0 | no 50 % point | −16.9 | −14.0 | −10.5 |
| the same + OSD | −20.8 | −18.6 | −8.6 | −17.5 | −15.2 | −12.2 |

Reading the table [D]:

- Block-detection gain against spread × block duration: FT4 (N = 4, 0.192 s): 1.6 dB at 0, 1.3 dB at
  0.19, 0.1 dB at 1.9. FT8 (N = 3, 0.48 s): 0.7 dB at 0, 0.5 dB at 0.48, none at 4.8. The gain is mostly
  kept while spread × block duration is below about 0.2 and is gone by about 2.
- OSD gains 0.5–0.6 dB on AWGN and 1.2–1.7 dB on the fading channels. Near-ML decoding matters more when
  fading erases symbols.
- AP decoding uses the single-symbol metrics only [SRC].

### 1.5 Decoding, false-decode control and a priori information

[SRC] `decode174_91.f90`, `osd174_91.f90`, `ft8b.f90`, `ft8_a7.f90`, `ft8_decode.f90`.

- **Belief propagation:** sum-product, at most 30 iterations, stop when all checks and the CRC pass.
- **OSD fallback:** fed not with channel metrics but with the accumulated BP posteriors saved after the
  first one or two iterations (up to three), so BP conditions the ordering. Order-1 reprocessing with the
  Wu–Hadjicostis preprocessing rules [S27, S28] (`nord=1`, `npre1=1`, `nt=40`, `ntheta=10`).
- **Acceptance gates:** CRC-14; at most 36 of 174 hard decisions may disagree with the codeword; the
  all-zero codeword and undefined message types are rejected; a decode with 10 or fewer correct hard sync
  symbols and an estimated SNR below −24 dB is discarded as a likely false decode.
- **Order per candidate:** N = 1, N = 2, N = 3, normalised N = 1, then up to four AP passes.
- **AP types** (bits clamped to ±1.01 × the largest metric and masked in BP): `CQ ? ?` and
  `MyCall ? ?` 32 bits; `MyCall DxCall ?` 61 bits; `MyCall DxCall RRR|73|RR73` all 77 bits. AP with both
  calls is tried only near the contact's frequency.
- **AP gains** [PS] S1 Table 6 and Fig. 8: maximum AP gives 1.9 dB on AWGN (−20.8 → −22.7), 2.4, 3.3 and
  3.5 dB on the three mid-latitude channels, and 10.3 dB at 10 Hz (−8.6 → −18.9). On the mid-latitude
  disturbed channel the three AP stages give about 0.8, 2.3 and 3.5 dB [PS, figure reading ±0.2 dB]. The
  paper states that AP raises the false-decode rate to some degree; no rate is given.
- **"a7" list decode** (WSJT-X 2.6): about 200 hypothesised messages are built from the previous
  sequence's decodes on that frequency (call pair with grid, report, RRR, RR73, 73). Each is encoded and
  compared with all four metric sets by soft distance. The best is accepted only if its distance is at
  most 100 and the runner-up is at least 1.3 times farther. Disabled in the first period after a band
  change [SRC; DOC release notes 2.6.0].

### 1.6 Subtraction and multi-pass decoding

[DOC] S1 §6; [SRC] `subtractft8.f90`, `ft8_decode.f90`. The decoded message is re-modulated with the same
GFSK generator. The complex gain is the received signal times the conjugate reference, low-pass filtered
with a cos² window 4000 samples long (0.333 s, about two symbols; half-power bandwidth about ±2.2 Hz
[D]), with end corrections. The product of gain and reference is subtracted. An optional step refines
the start time over ±7.5 ms. Decoding then repeats: three passes at the deeper settings. Since 2.2.0
decoding also starts before the transmission ends, in three stages [DOC release notes].

The tracker follows amplitude and phase variation up to about 2 Hz either side. It is the only place
where FT8 estimates the channel, and it does so after decoding, from the full known waveform.

### 1.7 Thresholds on standard channel simulations

Channel model: two independent equal-power Rayleigh paths with Gaussian Doppler spectra, after ITU-R
F.1487 [S19, S29]. [PS] S1 Table 6 and Fig. 7, with conversions [D]:

| Channel (spread, delay) | FT8 no AP | `C/N0` | `Eb/N0` | FT8 max AP | FT4 no AP | `C/N0` | `Eb/N0` | FT4 max AP |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AWGN | −20.8 | 13.2 | 5.3 | −22.7 | −17.5 | 16.5 | 4.6 | −19.5 |
| Mid-lat. quiet (0.1 Hz, 0.5 ms) | −20.0 | 14.0 | 6.1 | −22.4 | −17.4 | 16.6 | 4.7 | −19.4 |
| Mid-lat. moderate (0.5 Hz, 1 ms) | −18.8 | 15.2 | 7.3 | −22.1 | −15.8 | 18.2 | 6.3 | −18.6 |
| Mid-lat. disturbed (1 Hz, 2 ms) | −18.6 | 15.4 | 7.5 | −22.1 | −15.2 | 18.8 | 6.9 | −18.4 |
| High-lat. moderate (10 Hz, 3 ms) | −8.6 | 25.4 | 17.5 | −18.9 | −12.2 | 21.8 | 9.9 | −17.4 |

Three points the prior documents did not record:

1. **FT8 at 10 Hz has a ceiling, not a penalty.** In Fig. 7 the FT8 high-latitude curve passes 0.5 near
   −8.6 dB and flattens at about 0.65–0.67 by 0 dB, the right-hand edge of the plot [PS, figure reading].
   About a third of frames fail at any plotted SNR, 21 dB above the AWGN threshold. A 90 % criterion is
   never met. A developer statement in 2024 says the same [DOC, S13, deleg.]. FT4 on the same channel
   reaches about 0.97 by −5 dB.
2. **The 50 → 90 % step widens under fading.** About 1.0 dB on AWGN; about 2.1 dB (FT8) and 2.7 dB (FT4)
   on the 1 Hz channel [PS, figure reading ±0.3 dB].
3. **Untested classes.** The paper does not report low-latitude disturbed (10 Hz, 6 ms) or high-latitude
   disturbed (30 Hz, 7 ms). The delays tested (≤ 3 ms) are under 2 % of the symbol; the paper says delay
   has little effect because of that.

### 1.8 Timing, erasures and drift

- **Clock offset:** lag search ±2.5 s around the nominal start [SRC].
- **Late starts / missing symbols** [PS] S1 Fig. 9 (mid-latitude moderate, −10 dB, i.e. 8.8 dB above that
  channel's threshold): without AP, decoding holds to about 4 s into the slot and reaches 50 % near 5.1 s,
  i.e. with about 36 % of the frame missing (first Costas array and 22 of 58 data symbols) [figure
  reading; fraction D]. With `MyCall` AP the 50 % point is near 8.2 s, with about 61 % missing. The code
  alone could tolerate at most 47.7 % erasures at this rate [D].
- **Frequency drift:** the decoder has no drift hypothesis; frequency is one constant per candidate [SRC].
  Developer statement: little loss up to about 3 Hz over 12.6 s, significant beyond [DOC] S11; a second
  developer puts the limit near half a tone spacing per transmission [DOC] S12 (deleg.). Loopback
  measurement with WSJT-X 2.0.0, six trials per point [M] S15 (deleg., read directly by the agent):

  | FT8 level | 100 % decodes up to | Roll-off |
  |---|---|---|
  | threshold (about −19 dB) | 0.08 Hz/s (1 Hz per transmission, 1/6 bin) | 50 % at 0.3 Hz/s (3.8 Hz); 17 % at 0.5 Hz/s |
  | +5 dB | 0.5 Hz/s | 17 % at 0.8 Hz/s |
  | +10 dB | 0.7 Hz/s (8.8 Hz, 1.4 bins) | 33 % at 0.88 Hz/s |

  The same measurements give 50 % points at threshold of about 0.08 Hz/s for JT9A and 0.7 Hz/s for JT65C.
- **Doppler rate on satellites** [M/PS] S16 (deleg.): FT8 through a LEO transponder tolerated a
  Doppler-correction timing error of 0.17–0.26 s; 24 of 33 periods decoded with a residual drift of about
  0.11 Hz/s.

---

## 2. Why 6.25 Hz and 160 ms work on HF, and where they stop working

### 2.1 Threshold loss against spread, one table

Loss is the 50 % threshold minus the same mode's AWGN (or zero-spread) threshold. "Spread" follows each
source's own definition (F.1487 2σ Gaussian for WSJT simulations; Q65 "Doppler spread" in its simulator;
"spectral spread" in the FST4W paper), so cross-mode comparison is approximate.

| Mode, decoder | Δf (Hz) | T (s) | Spread (Hz) | Spread/Δf | Spread × T | Loss (dB) | Label |
|---|---:|---:|---|---|---|---|---|
| FT8, block + BP + OSD | 6.25 | 0.160 | 0.1 / 0.5 / 1 / 10 | 0.016 / 0.08 / 0.16 / 1.6 | same | 0.8 / 2.0 / 2.2 / 12.2 with ceiling 0.66 | [PS] S1 |
| FT8, BP only | 6.25 | 0.160 | 1 / 10 | 0.16 / 1.6 | same | 3.1 / no 50 % point | [PS] S1 |
| FT4, block + BP + OSD | 20.83 | 0.048 | 0.1 / 0.5 / 1 / 10 | 0.005 / 0.024 / 0.048 / 0.48 | same | 0.1 / 1.7 / 2.3 / 5.3 | [PS] S1 |
| FST4W-120 | 1.465 | 0.683 | 0.1 / 0.5 / 1 / 2 / 3 | 0.07 / 0.34 / 0.68 / 1.4 / 2.0 | same | ≈ 1.1 / 2.7 / 4.0 / 7.4 / 10.9 | [PS] S8 Fig. 1, reading ±0.5 dB |
| WSPR | 1.465 | 0.683 | 0.1 / 0.5 / 1 / 2 / 2.5 / 3 | 0.07 … 2.0 | same | ≈ 0.4 / 1.4 / 2.8 / 5.9 / 14 / no decode | [PS] S8 Fig. 1, reading |
| JT65A, soft RS | 2.69 | 0.372 | 4 | 1.5 | 1.5 | ≈ 0.5–1 | [PS] S16 (deleg., 100 files per point) |
| Q65-60A | 1.667 | 0.600 | 5 / 10 / 20 | 3 / 6 / 12 | 3 / 6 / 12 | 2.0 / 3.0 / 4.2 | [PS] S4 Fig. 2, reading ±0.2 dB |
| Q65-60D | 13.33 | 0.600 | 5 / 10 / 20 / 50 / 100 | 0.4 / 0.75 / 1.5 / 3.8 / 7.5 | 3 / 6 / 12 / 30 / 60 | 1.4 / 1.9 / 2.7 / 4.4 / 6.1 | [PS] S4 Fig. 1, reading ±0.2 dB |
| Q65-120E | 12.0 | 1.333 | 5 / 10 / 20 / 50 / 100 | 0.4 / 0.8 / 1.7 / 4.2 / 8.3 | 6.7 / 13 / 27 / 67 / 133 | 1.0 / 1.6 / 2.6 / 4.4 / 6.3 | [PS] S4 Fig. 1, reading |
| Olivia 32/1000 | 31.25 | 0.032 | 10 (3 ms) / 30 (7 ms) | 0.32 / 0.96 | same | copy at −8 and −3 dB against −14 dB quiet: at most 6 and 11 | [M] S36 simulator, one level per channel |
| STANAG 4415, 75 bit/s | chip 2400/s; Walsh symbol 13.3 ms | — | 0.5–50 (10 ms) | — | 0.007–0.67 per symbol | required 8–9 above the AWGN row (different BER); vendor-measured 6–10 | [SPEC]/[V] S30 (deleg.) |

What the table shows:

- **Single-bin metrics have a cliff near spread ≈ tone spacing.** FT8 without OSD has no 50 % point at
  1.6 spacings; with OSD it has a ceiling of two thirds. WSPR has no decode at 2.0 spacings. FST4W-120
  degrades more gracefully (7.4 dB at 1.4 spacings) but see §2.3.
- **A spread-weighted symbol metric with a low-rate 64-ary code has no cliff.** Q65-60A loses 4.2 dB at
  12 spacings and reaches 100 % decoding 1.4 dB above its 50 % point [PS, Fig. 2].
- **With that metric, tone spacing hardly matters.** At equal symbol length and equal spread, Q65-60A
  (1.67 Hz) and Q65-60D (13.3 Hz) differ by −0.5 dB at 0 Hz, 0.1 dB at 5 Hz, 0.6 dB at 10 Hz and 1.0 dB at
  20 Hz in favour of the wider spacing [PS, D]. Eight times the bandwidth buys at most 1 dB up to twelve
  spacings.
- **JT65's old design is spread-tolerant for the same reason.** A 64-ary alphabet, a rate-0.19 symbol
  code and soft symbol information lose under 1 dB at 1.5 spacings.

The paper text [S8] attaches −24 dB (FST4W-120) and −17 dB (WSPR) to "2 Hz"; in its Fig. 1 that pair of
points sits near 2.5 Hz, and at 2 Hz both curves read about −25.5 dB [figure reading]. The prior documents
quote the text values. In absolute terms the FST4W-120 readings are about −31.7, −28.8, −25.4 and
−21.9 dB at 0.1, 1, 2 and 3 Hz: `C/N0` 2.3, 5.2, 8.6 and 12.1 dB-Hz, `Eb/N0` 5.7, 8.6, 12.0 and 15.5 dB
[D from figure readings].

### 2.2 The ionosphere against 6.25 Hz

Measured 14 MHz spectral spreads from FST4W transmissions with GPS-disciplined oscillators [M] S20
(deleg.): one-hop F-region paths 8–21 mHz when purely one-hop and under 50–75 mHz typically; mixed one-
and two-hop 74–503 mHz as 20-minute means (typically 100–270 mHz) on an 1808 km path; two-hop ground
side-scatter 130–650 mHz; a 12,254 km trans-equatorial path with the spread distribution peaking near
0.4 Hz [M, figure reading, deleg.]. Equipment adds under 30 mHz when disciplined, but a KiwiSDR browser
connection added 0.06–0.67 Hz [M] S8.

Doppler shift (not spread) at mid-latitudes is of order ±1 Hz, with sunrise peaks of +0.6 Hz on 3.5 MHz,
+1.2 Hz on 7 MHz and up to +1.7 Hz on 10 MHz [M] S20, S21, S24 (deleg.). It changes over tens of
minutes.

High latitudes: Doppler spreads of 2–55 Hz and multipath spreads of 1–11 ms were exceeded 5 % of the time
across four measured paths [M] S22 (abstract).

Against these numbers [D]:

- One-hop spread is 0.1–1.2 % of 6.25 Hz and side-scatter 2–10 %. All F.1487 mid-latitude classes
  (0.1–1 Hz) are at most 16 %. FT8 pays 0.8–2.2 dB there, most of it Rayleigh fading of a two-path channel
  rather than tone smearing.
- Delay spreads of 0.5–3 ms are 0.3–2 % of the symbol.
- The symbol (0.16 s) is short against the coherence time (1–10 s at 0.1–1 Hz), so a symbol is nearly
  coherent and three-symbol blocks still gain something.
- A diurnal ±1 Hz shift is slow: orders of magnitude below the 0.08 Hz/s drift limit of §1.8.
- Narrower designs sit on the edge. With 1.46 Hz tones the "best at spread ≤ 1/8 spacing" rule [DOC, S9]
  gives 0.18 Hz, inside the measured side-scatter range.
- So 6.25 Hz is roughly the narrowest spacing that keeps every mid-latitude class below one sixth of a
  bin, and the 15 s slot sets the +13 dB-Hz operating point. It is a mid-latitude design. At 10 Hz
  (high-latitude moderate, low-latitude disturbed) and above it fails [H].

No published sensitivity-loss-per-hertz figure for FT8 was found beyond S1's table, and no independent
threshold-versus-spread curve for FT8, FT4, JS8, JT9 or WSPR outside the developers' own simulations
(deleg. search; see §10.6).

### 2.3 Where each mode fails

- **FT8.** High latitudes (ceiling of 0.66 at 10 Hz). Auroral and trans-equatorial flutter: only
  anecdotes were found, with spreads of tens of hertz and more quoted for TEP [A] S25; the developers'
  recommendation for such paths is a wider Q65 submode [DOC] S4. Drift above about 1 Hz per transmission
  at threshold. The FT8 decoder's use of inter-symbol coherence is destroyed by difficult paths, which the
  developers give as the reason a wider-tone noncoherent design degrades less [DOC] S10 (deleg.).
- **FST4/FST4W.** The developers' rule is that oscillator drift and path Doppler must stay below the tone
  spacing over the whole transmission, with best sensitivity at spread ≤ 1/8 spacing [DOC] S9. That gives
  best-sensitivity limits of 183, 70, 22 and 11 mHz for the 120, 300, 900 and 1800 s submodes [D]. A
  simulator run at −15 dB input SNR (18 dB above the −120 threshold) put the 90 %, 50 % and 10 % decode
  points at measured spectral widths of about 1.05, 1.3 and 1.75 Hz for FST4W-120, 0.45, 0.6 and 0.68 Hz
  for −300, and 0.14, 0.19 and 0.25 Hz for −900 [PS] S8 Fig. 8 (figure reading). That is 0.7–0.8, 0.9–1.1
  and 1.2–1.4 tone spacings: **a hard failure near spread ≈ spacing that signal strength does not cure.**
  The paper's conclusion for 14 MHz is that −300 on two-hop paths is already below 90 % in quiet
  conditions, −900 is marginal on one hop and −1800 is not worth attempting. On LF and MF the spreads are
  millihertz, which is why the long submodes are used there.
- **WSPR.** No decode at 3 Hz at any SNR [PS] S8. Its decoder does search linear drift of ±4 Hz over the
  transmission [SRC, deleg.].
- **FT4.** Wider and faster: 5.3 dB loss at 10 Hz and no ceiling. It is 3.3 dB less sensitive than FT8 on
  AWGN because its transmission is 2.5 times shorter; per bit it is 0.7 dB more efficient (lower sync
  fraction, larger block-detection gain) [D from S1].
- **JS8.** Same waveform family at 3.125, 6.25, 10 and 20 baud. No measured thresholds or spread data
  were found (the prior documents reached the same result). By the spread/spacing scaling of the FT8 row,
  the slow mode would meet FT8's 10 Hz behaviour near 5 Hz and the turbo mode near 32 Hz [H, untested].
- **JT9.** 1.74 Hz tones; drift tolerance 0.08 Hz/s at threshold [M] S15; wider submodes exist for spread.
- **Incremental-frequency modes (DominoEX, THOR).** The designer doubles the tone spacing relative to the
  baud rate at 8 baud and below because slower modes need more Doppler room [DOC] S36 (deleg.); they still
  gave no copy at 10 Hz/3 ms even at +10 dB in the simulator comparison [M] S36.

---

## 3. Q65 in detail

Sources: quick-start guide [S4], source [S3], Palermo's 2016 presentation [S6], Taylor's 2016 slides [S7],
user guide and release notes [S5]. No QEX article on Q65 was found; the quick-start guide and the source
are the primary description. No document called "MAPP65" was found (MAP65 is the wideband receiver
program; its quick-start was not read).

### 3.1 Signal

65-tone FSK without Gaussian shaping [DOC] S5. 85 symbols: 63 data symbols on tones 1–64 and 22 sync
symbols on tone 0 at positions 1, 9, 12, 13, 15, 22, 23, 26, 27, 33, 35, 38, 46, 50, 55, 60, 62, 66, 69,
74, 76, 85 [SRC]. Sync energy 22/85 = 25.9 % (1.30 dB) [D]. T/R 15, 30, 60, 120, 300 s with symbols of
0.15, 0.3, 0.6, 1.333, 3.456 s. Submodes A–E multiply the tone spacing by 1, 2, 4, 8, 16; occupied
bandwidth is 65 spacings, 19 Hz to 1733 Hz [DOC].

AWGN thresholds for the A submodes [PS] S4, with conversions [D] (`R_b` = 77 bits over the transmission):

| Submode | Δf (Hz) | Threshold | `C/N0` | `Eb/N0` | `Es/N0` | Max AP |
|---|---:|---:|---:|---:|---:|---:|
| 15A | 6.667 | −22.2 | 11.8 | 4.0 | 3.5 | −23.7 |
| 30A | 3.333 | −24.8 | 9.2 | 4.4 | 4.0 | −26.6 |
| 60A | 1.667 | −27.6 | 6.4 | 4.6 | 4.2 | −30.2 |
| 120A | 0.750 | −30.8 | 3.2 | 4.9 | 4.4 | −32.5 |
| 300A | 0.289 | −33.8 | 0.2 | 6.0 | 5.6 | −37.4 |

Q65-15A and FT8 carry the same 77 bits in the same 12.6–12.8 s. Q65-15A is 1.4 dB more sensitive on AWGN
and uses 433 Hz instead of 50 Hz.

### 3.2 Code and decoder

- **Code.** A (65,15) repeat-accumulate code over GF(64); two symbols carrying a 12-bit CRC are
  punctured, leaving (63,13): 78 bits in 63 symbols, 1.238 bits per symbol [DOC] S4. Information symbols
  are repeated, permuted, multiplied by field weights and accumulated; each check involves at most three
  symbols; the accumulator is terminated at zero [SRC `qracodes.c`; DOC S6].
- **Distance from capacity.** The decoder metric is tuned for `Eb/N0` = 2.8 dB, described as the point of
  about 50 % word error [SRC `q65.c`]. The symbol-metric limit of noncoherent 64-FSK at 1.238 bits per
  symbol is `Es/N0` = 2.85 dB, `Eb/N0` = 1.92 dB (Appendix A.3; the Sol study found 2.87 dB). The design
  point is therefore about 0.9 dB above the infinite-length limit of its modulation [D]. The measured
  Q65-60A threshold is 1.3 dB above it (Sol practice review). For QRA64, slides give a code gain of 1.0–1.5 dB
  over the JT65 Reed–Solomon decoders and 1.9 dB from better sync [DOC] S7; a plot of QRA(12,63) against
  RS(12,63) carries two 1.3 dB annotations whose reference curves could not be identified from the text
  layer [DOC] S6.
- **Why GF(64).** A binary code with bit metrics on the same 64 tones would need `Eb/N0` = 4.45 dB at this
  rate: a 2.5 dB loss (§4.2) [D].
- **Decoder.** Messages are 64-entry probability vectors. A check node convolves distributions over the
  field with Walsh–Hadamard transforms: transform, multiply, transform back, at cost M·log₂M instead of
  M² [SRC `qracodes.c`]. Field weights are permutations of the vector. Iteration limit 40, 60 or 100 by
  depth setting [SRC]. A decode must pass the CRC.
- **Symbol metric on AWGN/Rayleigh.** Bessel likelihood with `Es/N0` fixed at the design value rather than
  estimated. The source's reasoning: `Es/N0` cannot be estimated from a few dozen weak symbols, and a
  metric that is exact at the threshold is close to best where it matters [SRC comments].

### 3.3 The fast-fading metric

[SRC] `q65.c`, `q65.f90`, `q65_loops.f90`.

- Symbol spectra have resolution equal to the baud rate. Each tone owns `2^submode` bins; 64·(2 +
  `2^submode`) bins are kept per symbol.
- Tone log-likelihood = Σ over neighbouring bins of `w_k · E_k`, with `w_k = g_k·EsNo / (1 + g_k·EsNo) / N0`.
  The profiles `g_k` are tabulated for Gaussian and Lorentzian Doppler spectra; WSJT-X uses the Lorentzian
  tables. Tables are indexed by the 90 %-energy bandwidth times symbol length (B90·Ts) in 9 % steps over
  64 entries, about 0.9–210 Hz at the reference symbol length.
- The assumed operating `Es/N0` rises with spread by `8·log(B90)/log(240)` dB: 8 dB at 240 Hz.
- Noise density is the mean over all bins, corrected for the assumed signal share. The library notes this
  assumes no strong interference.
- Before decoding, spectra are divided by their 40th percentile, clipped at 20 and narrow carriers are
  removed.
- **Spread is found by trial decoding.** Hypotheses are B90 = 1.72^i Hz: i = 1…7 for A (1.7–45 Hz), 3…9
  for B (5–132 Hz), 8…14 for C–E (77 Hz upward, capped by the table), two more each way at the deepest
  setting. The first hypothesis that yields a valid decode wins. Small frequency (half-baud) and time
  (T/16) offsets are tried in the same loop, limited to a neighbourhood of the centre.

Published behaviour [PS] S4, figure readings ±0.2 dB, conversions [D]:

| Spread (Hz) | 60A no AP | 60D no AP | 60D max AP | 120E no AP | 120E max AP |
|---:|---:|---:|---:|---:|---:|
| 0 | −27.6 | −27.1 | −27.5 | −29.0 | −29.6 |
| 5 | −25.6 | −25.7 | −26.8 | −28.0 | −28.9 |
| 10 | −24.6 | −25.2 | −26.6 | −27.4 | −29.0 |
| 20 | −23.4 | −24.4 | −26.3 | −26.4 | −28.7 |
| 50 | — | −22.7 | −25.4 | −24.6 | −27.7 |
| 100 | — | −21.0 | −23.9 | −22.7 | −25.8 |

In `C/N0`: 60D needs 6.9 dB-Hz at 0 Hz and 13.0 dB-Hz at 100 Hz; 120E needs 5.0 and 11.3 dB-Hz. In
`Eb/N0`: 60D 5.1 → 11.2 dB; 120E 6.7 → 13.0 dB.

Observations [D]:

- Both wide submodes lose 6.1–6.3 dB from 0 to 100 Hz without AP. With maximum AP the loss is 3.6–3.8 dB.
- AP gain grows with spread: 0.4–0.6 dB at 0 Hz, 1.4–1.6 dB at 10 Hz, 2.9–3.1 dB at 100 Hz. FT8 shows the
  same trend (§1.5).
- 120E is better than 60D by 1.7–2.3 dB at every spread. The symbol is 2.22 times longer (3.5 dB more
  energy). A gain of half the energy ratio in dB (1.7 dB) is the square-law result for noncoherent
  combining. It appears even at 0 Hz, which fits the source: the D and E submodes never hypothesise less
  than 77 Hz of spread [H].
- 120E at 0 Hz needs 1.8 dB more `Eb/N0` than 120A. Wide submodes pay for spread tolerance on quiet paths.
- The 2 dB shortfall of 300A against ideal scaling (4.0 → 6.0 dB `Eb/N0`), left unexplained in the anchors
  chapter, has a candidate cause: the smallest spread hypothesis for A submodes is 1.72 Hz, which is 6
  bins for 300A but 1 bin for 60A, so the long submodes always combine several bins noncoherently [H,
  from source; not tested].

### 3.4 Q65 against FT8 on spread paths

No source tests both on one channel. Combining S1 and S4 [D]:

- At spread × T ≈ 1.6 (10 Hz for FT8), FT8 has a ceiling of 0.66 and a 50 % point 12.2 dB up. Q65 at
  spread × T = 3 loses 1.4–2.0 dB and has no ceiling.
- Scaling Q65-60A's curve to Q65-15A by spread × T puts 15A near −21 dB at 10 Hz, about 12 dB better than
  FT8 without AP and 2 dB better than FT8 with all 77 bits known [H; scaling assumption, not a published
  point].
- Costs: 8.7 times the bandwidth; a GF(64) decoder run once per spread, frequency and time hypothesis;
  0.5 dB on quiet paths for the D submode and 1.8 dB for 120E; one signal per frequency slot rather than
  FT8's dense multi-signal decoding.

Field and simulation results: nearly every Q65-30A transmission copied over an 1150 km 50 MHz ionoscatter
path at 300 W [M, developers' report] S4; three sequences that failed singly decoded when averaged, in
the guide's sample files [M, single example]; on 10 GHz EME with up to 100 Hz spread the fast-fading
metric recovered about 6 dB over a single-bin decoder and kept decoding with 9 dB of added noise [M,
replayed recordings] S6. EME and troposcatter comparisons are in the prior documents. No HF measurement
of Q65 was found.

### 3.5 Synchronisation, list decoding, averaging, drift

[SRC] `q65.f90`, `q65.c`, `q65_set_list.f90`, `q65_decode.f90`.

- **Two sync paths.** (1) With a list of expected messages, the decoder correlates symbol energies along
  the full 85-symbol tone sequence of each list entry over frequency and lag. The best entry must beat
  the runner-up by 10 % (submodes A–C). (2) Otherwise it sums energy at the 22 sync positions on tone 0,
  minus the frame-average energy in that bin, over lag and optional linear-drift hypotheses; candidates
  need a peak 6 or more baseline deviations high.
- **Lag range** ±1.0 s (to +5.5 s when EME delay is enabled), against FT8's ±2.5 s.
- **Sync pattern** [D]: the 22 positions have aperiodic autocorrelation sidelobes of 3–5 of 22 for shifts
  of 1–10 symbols and at most 8 of 22 (shifts 11 and 14). With the zero-mean template the source uses,
  the worst sidelobe is about 14 % of the peak. One tone gives no frequency structure to smear, which is
  why it survives spread that defeats Costas arrays; it is also why a carrier on the sync tone, or a
  frequency-selective null there, attacks sync alone [H].
- **List ("q3") decoding.** The list is 206 messages: the two calls with nothing, RRR, RR73, 73, a CQ, a
  grid, and 200 signal reports. Each codeword's log-likelihood is computed from the fast-fading
  intrinsics; the best is accepted if it exceeds `−260 + ln(list size / 3)`, and a second fixed floor of
  −242 applies. The source states the list-size term keeps the false-decode rate independent of list
  size. This is a hypothesis test, with sync taken from all 85 symbols: 5.9 dB more sync energy than the
  22 sync symbols [D].
- **AP without a list.** Known symbols are masked in belief propagation, as in FT8.
- **AP gains.** 1.5–3.6 dB on AWGN for the A submodes [PS] S4; see §3.3 for the dependence on spread. For
  QRA64 the stages were +0.5, +1.1, +2.3 and +4.2 dB [DOC] S7.
- **False decodes.** Controlled by the 12-bit CRC, the likelihood thresholds and the 10 % margin. No rate
  is published. A flaw producing false decodes in the D and E submodes was fixed in 2.7.0-rc2 [DOC] S5.
- **Averaging.** Symbol power spectra of successive sequences are averaged: linearly for the first four,
  then as an exponential average with weight 1/4 [SRC; DOC release notes 2.4.0-rc2]. It is power-domain
  (noncoherent) and its depth is capped at roughly seven sequences' worth of noise averaging [D].
- **Drift.** Linear drift hypotheses in the sync search since 2.5.0, extended to list decodes in 2.5.1
  [DOC]. The search shifts the sync positions by whole bins in proportion to time.

### 3.6 Documented weaknesses

1. Sync without a list rests on 26 % of the energy at one frequency and a detection threshold.
2. AP and list decoding trade sensitivity for false decodes; the control is a tuned threshold, and a flaw
   was shipped and fixed.
3. Timing tolerance is ±1 s in normal use.
4. Drift must be linear and within the configured range; no general trajectory search.
5. Long submodes lose efficiency (4.0 → 6.0 dB `Eb/N0` from 15 s to 300 s) [prior anchors].
6. Averaging depth is capped and noncoherent.
7. Bandwidth is 65 tone spacings and the mode is aimed at one signal per slot.
8. Noise estimation assumes no strong interference.

---

## 4. Other designs and recent academic work

### 4.1 Evaluations and decoders for the WSJT modes (2018–2026)

A delegated search of arXiv, Crossref and OpenAlex found no peer-reviewed evaluation of FT8, FT4, JS8 or
WSPR on Watterson or measured HF channels beyond the developers' documents, and no neural, sequence-
detection or cross-transmission-averaging FT8 decoder with a stated gain over WSJT-X, other than the
Kozlenko work already recorded [S49]. Independent decoders (ft8_lib, weakmon, PyFT8) publish no
sensitivity in dB; one author reports about 90 % of WSJT-X's fast-mode decode count on a short recording
[A, deleg.]. Papers that use FT8 or JS8 treat them as tools: disaster telemedicine with JS8 [S46],
ionospheric condition estimation from FT8 spots [S47], corroboration of propagation models [S48]. A 2026
preprint reusing 8-GFSK and LDPC(174,91) claims −21.0 dB without channel simulations [S50]. This agrees
with the prior documents' "not found".

### 4.2 Symbol metrics against bit metrics (alphabet matched to the code)

Published capacity limits, noncoherent orthogonal M-FSK, AWGN, rate 1/2, minimum `Eb/N0` in dB [theory,
PS] S40 (deleg., full text):

| M | Bit metrics (BICM) | Symbol metrics (CM) | Simulated BICM, turbo code, 6138 bits | Simulated with iterative demapping |
|---:|---:|---:|---:|---:|
| 2 | 6.71 | 6.71 | 7.27 | 7.27 |
| 4 | 4.64 | 4.18 | 5.23 | 4.89 |
| 16 | 3.27 | 2.07 | 3.82 | 3.18 |
| 64 | 2.81 | 1.11 | 3.32 | 2.64 |

Iterative demapping recovers 0.3–0.4 dB at M = 4, 0.7–0.9 dB at M = 16 and 0.75–1.0 dB at M = 64, roughly
half or less of the gap at M = 64 [PS] S41. Optimised repeat-accumulate codes with a recursive inner
modulator reach thresholds within 0.01–0.17 dB of the symbol-metric capacity at block length 100,000
[theory/PS] S42; their table gives 2.99 dB (rate 1/2) and 3.55 dB (rate 1/4) for M = 8 and 1.12 and
1.68 dB for M = 64. No short-block result of that kind was found.

Values at the rates FT8 and Q65 actually use, and across alphabets at equal bits per symbol (Appendix
A.3) [D]; ideal likelihoods, infinite block length, energy observed in one bin per tone:

| M | Bits/symbol | AWGN: symbol metric | AWGN: bit metric | Loss | Rayleigh per symbol, no CSI: symbol metric | bit metric | Loss |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 1.569 (FT8 code input) | 2.88 | 3.73 | 0.85 | 5.13 | 5.93 | 0.80 |
| 8 | 1.328 (FT8 payload) | 2.91 | 3.82 | 0.91 | 4.68 | 5.53 | 0.85 |
| 16 | 1.328 | 2.28 | 3.75 | 1.47 | 3.50 | 4.78 | 1.28 |
| 32 | 1.328 | 2.00 | 3.96 | 1.96 | 2.85 | 4.54 | 1.69 |
| 64 | 1.328 | 1.83 | 4.28 | 2.45 | 2.53 | 4.51 | 1.98 |
| 64 | 1.238 (Q65 payload) | 1.92 | 4.45 | 2.53 | 2.56 | 4.63 | 2.07 |
| 256 | 1.328 | 1.71 | 5.02 | 3.31 | — | — | — |

Values are required `Eb/N0` in dB (Monte Carlo, about ±0.05 dB; the 8-FSK value is 0.1 dB below S42's
table, unexplained).

Consequences [D]:

- FT8 gives up about 0.9 dB by pairing 8 tones with a binary code and non-iterated bit metrics.
- With bit metrics the best alphabet is 8–16; 64 tones are 0.5 dB worse than 8 on AWGN. With symbol
  metrics, larger is better at every step, with diminishing returns after 64.
- Going from 8 to 64 tones at the same bits per symbol gains 1.1 dB on AWGN and 2.2 dB on independent
  Rayleigh symbols. The published 1.4 dB AWGN advantage of Q65-15A over FT8 is consistent with the first
  figure.
- On independent Rayleigh symbols the 64-ary symbol-metric limit is only 0.6–0.75 dB above its AWGN value;
  for 8-ary it is 1.8–2.3 dB above.

Short-block evidence for alphabet-matched codes: non-binary LDPC with code-shift keying gains about
2.6 dB (Gaussian) and 3.5 dB (Rayleigh) over an unstated binary baseline [PS, abstract] S43; non-binary
LDPC designs for orders 8–32 gain up to 0.8 dB at 192 information bits [PS, abstract] S44; a CRC-aided
4-ary convolutional code with list decoding, 128 bits, noncoherent 4-FSK, is 0.59 dB from the random-
coding union bound at frame error 10⁻⁴, and the paper supplies that bound for noncoherent orthogonal
signalling [PS, full text, deleg.] S45. No comparison of a GF(64) code with a binary code on noncoherent
64-FSK at 60–300 symbols was found.

Non-binary decoder cost: FFT/Hadamard-domain belief propagation [S51], extended min-sum with truncated
messages [S52, S53], min-max [S54], and GPU implementations of each [S55–S57] (abstracts only).

### 4.3 Multi-symbol detection of CPFSK

Capacity limits for noncoherent CPFSK with N-symbol blocks, minimum `Eb/N0` at 2 Hz per bit/s [theory]
S58 (deleg., full text):

| M | AWGN, N = 1 / 2 / 4 | Block-fading Rayleigh, N = 1 / 2 / 4 |
|---:|---|---|
| 2 | 8.08 / 5.43 / 3.63 | 10.69 / 7.96 / 6.12 |
| 4 | 5.33 / 3.51 / 2.20 | 8.17 / 5.87 / 4.31 |

These assume the channel is constant over each block, so they bound the gain; they do not show its loss
under spread. The only quantitative loss data found are S1's (§1.4). Block detection follows Simon and
Divsalar [S59], cited by S1. Coded noncoherent FSK with unknown frequency offset is treated in [S60].

### 4.4 Preamble-less frames

The "quasi-cyclic short packet" work pairs a non-binary code with cyclic code-shift keying and detects
and synchronises on the coded frame itself: error probability 10⁻⁴ at −11 dB SNR per chip for a 360-bit
payload, 1.2 dB from the finite-length bound, with a frame 23 % shorter than a preamble-based one [PS,
abstract] S61; companion papers cover time sync, sliding-window detection and phase sync near −10 dB
[S62–S66]. Full texts were not accessible. This is the closest published analogue to using a keyed tone
schedule as the only synchronisation structure.

### 4.5 Military 75 bit/s Walsh waveforms

- **Waveform** [SPEC] S31 (deleg., primary text): each pair of coded bits selects one of four orthogonal
  32-chip sequences (a 4-chip Walsh pattern repeated eight times), scrambled, on 2400 chip/s 8-PSK. A
  symbol lasts 13.33 ms. Rate-1/2, constraint-length-7 code and an interleaver of up to 4.8 s. No known
  probe symbols at 75 bit/s. It is 4-ary, not 32- or 64-ary.
- **STANAG 4415 mandatory performance**, as transcribed by a modem vendor [SPEC via V] S30 (deleg.): two
  equal paths, long interleaver, BER below 10⁻⁴ at 10 ms delay for Doppler spreads of 0.5, 1, 2, 5, 10,
  20, 30, 40 and 50 Hz at 0, −1, −1, −1, −1, −1, −1, −0.5 and 0 dB; AWGN BER below 10⁻³ at −9 dB. The
  bandwidth is presumed 3 kHz. In `C/N0` these are 33.8–34.8 dB-Hz on the fading rows and 25.8 dB-Hz on
  AWGN; `Eb/N0` 15.0–16.0 dB and 7.0 dB [D]. The vendor's own modem is listed at −2.0 to −5.5 dB on the
  fading rows and −11.6 dB on AWGN [V]. Three further rows could not be read reliably from the page.
- **The claim "40 Hz Doppler spread and 16 ms delay spread".** 40 Hz is a mandatory test point (at
  −0.5 dB) and 50 Hz is the highest. The delay in every located source is 10 ms (test condition) or
  "approaching 12 ms" (the FFI overview cited in the Sol review, at −7 dB, which the vendor table does
  not support as a requirement). **No source containing 16 ms was found**; 16 ms would exceed the 13.3 ms
  symbol. The STANAG text itself is restricted and was not read.
- **How it survives 50 Hz** [D]: with a Gaussian Doppler spectrum the channel correlation across one
  13.3 ms symbol is about 0.25 at 40 Hz and 0.11 at 50 Hz, but about 0.98 across one 1.67 ms Walsh
  repeat. Per-symbol coherent estimation cannot work there; combining sub-symbol correlations can. No
  open source describes the receiver. A simulation paper on the same modulation family uses a RAKE with
  decision-directed updates [PS] S33.
- **Beyond the test points:** no curve beyond 50 Hz or 10 ms was found. A waveform-design paper notes
  that serial-tone failure beyond the designed delay span is usually catastrophic [DOC] S32.
- **MIL-STD-188-110C** [SPEC] S31: 75 bit/s needs +2 dB at 5 ms and 5 Hz for BER 10⁻⁵; the optional
  robust mode must meet STANAG 4415; "16 ms" does not occur in the text (deleg. text search).

These thresholds are 10–20 dB above the weak-signal modes in `Eb/N0`. Their relevance is the method:
short orthogonal symbols, low-rate convolutional code, long interleaver, no pilots, and a specification
that is flat from 0.5 to 50 Hz.

### 4.6 Olivia, Contestia, MFSK16, DominoEX, THOR

[DOC] S36 (deleg.; Olivia's Walsh sign detail is from the agent's memory of the reference source and is
unverified):

- **Olivia 32/1000:** 32 tones, 31.25 Hz spacing, 31.25 baud; each tone carries 5 bits (Gray). A block is
  64 symbols; each of the five bit-planes is one 64-bit Walsh function carrying a 7-bit character, so
  five characters per 2.048 s (17.1 bit/s). Bit interleaving and a fixed scrambler; tones are amplitude-
  shaped with a cosine-series window; decoding by fast Hadamard transform. It is a binary bi-orthogonal
  block code on a 32-ary alphabet, not a 64-ary symbol code.
- **Claimed sensitivity:** 10 dB below noise in 1 kHz [DOC, author]: `C/N0` = 20 dB-Hz, `Eb/N0` = 7.7 dB
  [D]. Program documentation lists −12 to −14 dB without a bandwidth.
- **MFSK16:** 16 tones, 15.625 Hz and 15.625 baud, continuous phase without shaping, rate-1/2
  constraint-7 code, short interleaver. Designed for long and polar paths: phase modulation was rejected
  because of polar phase disturbance, short symbols because arrival times vary by 5–10 ms and up to 30 ms
  between long and short path [DOC, QST 2001]. No Doppler-spread limit is stated.
- **Simulator points beyond the prior documents** [M, one compiler]: at 20 ms and 1 Hz (+10 dB) Olivia,
  MFSK16 and THOR copy fully; MFSK16 copies fully at 10 Hz/3 ms (−8 dB) and 75 % at 30 Hz/7 ms (−3 dB).
  If those SNRs are in 3 kHz, as a later post by the compiler indicates, Olivia 32/1000's full copy at
  −8 dB and −3 dB corresponds to `C/N0` 26.8 and 31.8 dB-Hz and `Eb/N0` 14.4 and 19.4 dB [D].

---

## 5. Lowest-power long-haul evidence

Cases, from a delegated search (primary pages fetched unless noted; SNR in 2500 Hz):

| Case | Power | Path | Result | Label |
|---|---|---|---|---|
| WSPR, 30 m, dipole → receive-only station, March–April 2009 [S67] | 100 mW nominal | Eastern USA → Western Australia; the article gives no distance, a news item gives about 18,600 km for a similar pair | Received on most days; three-week mean SNR about −22 to −26 dB in two daily openings; signals under 10 mW also decoded (no SNR given) | [M], figure reading by agent |
| Balloon WSPR, 30 m, 21–27 Aug 2015 [S68] | 16 mW measured by the builder | Balloon over N. Atlantic/Europe → New Zealand and Australia | Daily best distance 16,500–19,500 km on four days, hundreds to thousands of reports per day; no SNR listed | [M] |
| Balloon WSPR, 2021 [S69] | 10 mW nominal | to Australia, about 19,000 km | Longest reception; no SNR | [M], nominal power |
| Balloon WSPR, 2015 [S70] | 25 mW nominal | two circumnavigations | Tracked from four continents | [A] |
| WSPR, 14 MHz, Sept 2020 [S71] | 0.5 mW (5 mW through a 10 dB pad) | Wales → USA, about 5,300 km | −22 dB | [M], single night, calculated power |
| WSPR, 14 MHz, Sept 2020 [S71] | 50 µW (50 mW through 30 dB) | Wales → Canary Is. 2,860 km; → USA about 5,200 km | about −25 dB to the Canaries; USA an hour later | [M], single evening |
| WSPR, 28 MHz, Jan 2024 [S72] | at most 10 mW ERP | England → Brazil 9,593 km | spotted; 99 reporters that day | [M], no SNR |
| QRSS, 10 MHz, 2010 [S73]; 28 MHz, 2024 [S74] | 100 mW | USA → Belgium 5,800 km; USA → Ireland 6,000 km | visual copy | [A] |
| QRSS, 14 MHz, 2014 [S75] | 1 µW measured | 1,873 km | visible for six hours with 38 stacked ten-minute frames; disciplined oscillators | [M] |

Database sample [M, self-reported power] S76 (deleg.; 20 m, October 2026, callsigns that are not balloon
telemetry): beyond 10,000 km there were 3,531 spots at a reported 1 mW with SNR 5th/50th/95th percentiles
−28 / −22 / −13 dB, and 17,408 at 10 mW with −27 / −22 / −11 dB. The power field is unreliable in both
directions (attenuated transmitters report the undivided power; telemetry frames encode data in it), and
only successful decodes are recorded, so these are truncated distributions, not path statistics. A
peer-reviewed balloon study found most 20 m spots at 1,000–5,000 km and none beyond 7,500 km at high solar
elevation [M, abstract] S77.

Not found: any reception of 100 µW or less beyond about 5,200 km; any FST4W long-path result at
milliwatt power; QRSS record tables.

**Implied loss and `C/N0`** [D]. Assumptions: `C/N0 = SNR + 34.0`; external noise density at the receiver
from the prior [noise chapter](../../Opus-5.5/02-noise-and-link-budget.md): −144 dBm/Hz (quiet rural) to
−134 dBm/Hz (rural) at 10 MHz, and −147 to −139 dBm/Hz at 14 MHz from the same ITU-R P.372 median
man-made and galactic expressions (constants recalled, checked against the chapter's 10 MHz values); "system
loss" = transmitter power − received power, antenna gains included.

| Case | `C/N0` (dB-Hz) | System loss, quiet … rural receiver | Free-space loss for the distance |
|---|---:|---:|---:|
| 100 mW, 18,600 km, 10 MHz, −22 … −26 dB | 12 … 8 | 152–156 … 142–146 dB | 138.0 dB |
| 10–16 mW balloon, 19,000 km, 10 MHz, decode implies ≥ −31 dB | ≥ 3 | at most 151–153 … 141–143 dB | 138.4 dB |
| 0.5 mW, 5,300 km, 14 MHz, −22 dB | 12 | 132 … 124 dB | 129.9 dB |
| 50 µW, 2,860 km, 14 MHz, −25 dB | 9 | 125 … 117 dB | 124.6 dB |

Bounds on "2–100 mW through more than 150 dB" [D]:

- Every documented milliwatt-level decode sits at `C/N0` ≥ about +3 dB-Hz (the WSPR threshold), and the
  database 5th percentile is +4 to +6 dB-Hz. Nothing documented at HF is below that.
- The documented antipodal successes imply 141–156 dB of system loss. "More than 150 dB" is therefore the
  upper edge of what WSPR records at these powers, not a typical case.
- With a quiet 10 MHz receiver, `C/N0 = P_tx + 144 − L`: 2 mW gives −3, −13, −23 dB-Hz at 150, 160,
  170 dB; 100 mW gives +14, +4, −6. A rural receiver is 10 dB worse. The stated range of +10 to −25 dB-Hz
  corresponds to 154–172 dB (quiet) for 100 mW down to 2 mW.
- So the lower 28 dB of the target range has no HF precedent. FST4W's long submodes reach −3 to −11 dB-Hz
  on AWGN but fail on HF spread (§2.3). The −22 dB-Hz precedent in the prior documents is coherent
  signalling on a phase-stable VLF path.
- At −25 dB-Hz a 50-bit message at the best practical noncoherent efficiency on a stable channel (about
  22 dB of `E/N0`) takes about 14 hours; the prior limits chapters show how much worse spread makes it.

---

## 6. Synthesis: adopt, adapt or avoid

### 6.1 Tone spacing equal to baud, or a multiple

- **Evidence.** Spacing = baud with single-bin metrics fails near spread ≈ spacing (FT8, WSPR, FST4W).
  With a spread-weighted metric and a low-rate 64-ary code, an eight-fold wider spacing changes the
  threshold by −0.5 to +1.0 dB over 0–12 spacings of spread (§2.1). Incremental-frequency designs double
  the spacing at low baud rates by rule.
- **For a keyed hopped design** the question splits. The grid can stay at spacing = baud; that is what a
  one-symbol FFT gives. What must be wide is the separation between the candidate tones of one symbol,
  and the schedule controls that directly. Candidates scattered over 448 bins are rarely neighbours;
  enforcing a minimum separation of several bins removes the mechanism behind FT8's ceiling at no
  bandwidth cost, because the band is in use anyway.
- **Adopt:** 6.25 Hz grid = baud. **Adapt:** a schedule constraint that keeps same-symbol candidates at
  least twice the largest spread to be survived apart (10 bins for 30 Hz). **Avoid:** adjacent-tone
  alphabets with single-bin metrics.

### 6.2 Gaussian shaping, plain CPFSK or amplitude ramps when tones hop across the passband

Calculation A.2, tones uniformly random over 448 bins (2.8 kHz), T = 0.16 s, rectangular one-symbol
detector [D]:

| Transition | Wanted-bin energy | Mean leakage per adjacent bin | Power outside the hop band by more than 100 Hz / 300 Hz | Envelope |
|---|---:|---:|---:|---|
| Plain CPFSK (instant jump) | 0.00 dB | none | −37.0 / −40.9 dB | constant |
| Gaussian BT = 1 | −6.64 dB | −9 dB | below −130 dB | constant |
| Gaussian BT = 2 (FT8's value) | −2.36 dB | −13 dB | −117 dB / below −130 | constant |
| Gaussian BT = 4 | −0.92 dB | −20 dB | −82 dB / below −130 | constant |
| Gaussian BT = 8 | −0.35 dB | −27 dB | −63 / −110 dB | constant |
| Gaussian BT = 16 | −0.12 dB | −35 dB | −53 / −79 dB | constant |
| Raised-cosine amplitude ramp, 5 ms each edge | −0.28 dB (peak-limited) | −30 dB | −48 / −71 dB | mean power −0.17 dB |
| Ramp 10 ms | −0.56 dB | −24 dB | −62 / −83 dB | −0.35 dB |
| Ramp 20 ms (FT8's start/end ramp, applied to every symbol) | −1.16 dB | −18 dB | −75 / −95 dB | −0.74 dB |

For contiguous alphabets the same BT = 2 costs 0.05 dB at 8 tones, 0.20 dB at 16 and 1.09 dB at 64.

- A Gaussian transition has a fixed duration. During it the frequency sweeps through every bin between
  the two tones, and that time is lost to the wanted bin. The loss grows with the jump.
- **Avoid** FT8's BT = 2 for wide hops. **Adopt** frequency shaping in a weaker form: BT ≈ 8–16 costs
  0.12–0.35 dB, keeps a constant envelope and puts out-of-band power 16–26 dB (at 100 Hz) and 38–69 dB (at
  300 Hz) below plain CPFSK. At equal skirt level near 100 Hz, BT = 8 costs 0.35 dB against 0.56 dB for a
  10 ms amplitude ramp, and its far skirt is 26 dB lower.
- Plain CPFSK is the most efficient and is acceptable if the 1/f² skirt (−37 dB beyond 100 Hz) and
  per-hop transients are acceptable to the transmitter filter and to the observer analysis in the prior
  documents.
- **Phase continuity** across hops costs nothing to keep. FT8's multi-symbol gain needs the channel phase
  to be common to the symbols combined. Across hops wider than the coherence bandwidth (roughly 80–320 Hz
  for 0.5–2 ms of delay spread) it is not, so that gain (0.7–1.6 dB on quiet paths) is forfeited by full-
  band hopping unless consecutive symbols are kept close in frequency [H].

### 6.3 Distributed Costas-like sync or the keyed schedule as implicit sync

- FT8 and Q65 both spend about 26 % of the energy (1.3 dB) on known symbols. Q65 abandons that structure
  whenever it can: with a candidate list it synchronises on all 85 symbols and calls that its most
  sensitive indicator [DOC] S4.
- A keyed pseudo-random schedule is a Costas-like pattern with far lower ambiguity than three repeated
  7×7 arrays (expected false coincidences per shift are of order symbols × candidates / bins, with no
  repeated-block sidelobe).
- **But unknown data dilutes sync.** In the small-signal limit the best per-symbol sync statistic is the
  summed energy of the M candidate bins. Its deflection per symbol is 1/M of a known tone's [D,
  Appendix A.4]. Using every symbol as data-bearing sync instead of a fraction f of pilots changes the
  deflection by 1/(M·f): about −3 dB for M = 8 and f = 0.27, about −12 dB for M = 64. At per-symbol
  `Es/N0` of several dB the penalty fades because the right bin usually dominates. Long integration
  operates at low per-symbol SNR, where it does not fade.
- JT65's deep-search decoder was limited by sync failures below about −28 dB (prior Opus review); QRA64
  gained 1.9 dB over JT65 from its sync scheme alone [DOC] S7.
- **Adopt** full-sequence correlation for verification and for any small-codebook message. **Adapt** the
  pilot idea: pilots become schedule positions with one candidate, hopping like everything else, and
  their fraction becomes a design variable sized by acquisition at the lowest `C/N0`, not fixed at 26 %.
  **Avoid** a fixed-frequency sync tone and repeated identical sync blocks.

### 6.4 Alphabet size against code alphabet

- With symbol metrics, 64 candidates need 1.1 dB less than 8 on AWGN and 2.2 dB less on independent
  Rayleigh symbols, at the same bits per symbol (§4.2). With bit metrics, 64 is worse than 8.
- The bandwidth objection to 64 tones disappears when candidates are schedule positions in a passband
  already occupied; the constraint becomes candidate separation (§6.1): 448 bins hold 64 candidates about
  7 bins apart, or 32 about 14 apart.
- **Adopt** an alphabet-matched code (GF(M) repeat-accumulate, non-binary LDPC or polar) with M = 32–64.
  **Adapt** if a binary code is kept: stay at 8–16 candidates and add iterative demapping or multi-symbol
  metrics, accepting about 1 dB. **Avoid** large alphabets with bit metrics.
- Beyond 64 the capacity gain is 0.1 dB per doubling while sync dilution and the per-symbol search grow.

### 6.5 Fast-fading metric

- **Adopt.** It is what removes the cliff: 4.2 dB at twelve spacings instead of failure at 1.6.
- **Adapt** three things. (1) Q65 selects spread by trial decoding, which multiplies decoder work and
  false-decode opportunities; a long transmission can estimate spread from accumulated pilot or
  candidate-bin spectra instead. (2) Q65's noise estimate assumes a clean band; a 2.8 kHz hop band needs
  per-bin, per-time robust baselines of the kind FT8's sync uses (percentile normalisation, clipping).
  (3) Q65 fixes the assumed `Es/N0` at its threshold; a design with a set of fixed configurations has one
  such constant per configuration.
- The price is the square-law regime: once spread × T exceeds about 3, each doubling of spread costs
  about 1.3–1.5 dB, and a 2.2-fold longer symbol returns 1.7–2.3 dB rather than 3.5 dB (§3.3).

### 6.6 Averaging and long integration

- Q65 averages symbol power spectra with a capped time constant. That is repetition coding with a soft
  combiner and a depth limit, and the prior documents record about 1 dB per doubling in practice.
- FST4 shows the alternative: scale the symbol and keep one codeword, holding `Eb/N0` within 0.15 dB from
  15 s to 1800 s on AWGN. HF spread forbids that below about 1 Hz spacing (§2.3).
- With 160 ms symbols and `C/N0` of −10 dB-Hz the per-symbol `Es/N0` is −18 dB. The FT8, FT4 and Q65
  thresholds here sit at +3.3 to +5.6 dB per symbol (FST4W, with coherent block detection, at −0.5 dB).
  The FT8 and Q65 evidence therefore does not test the regime the design targets; only the Q65 points
  at 50–100 Hz (energy split over 30–130 cells) and the FST4W spread data touch it.
- **Adopt** accumulation of symbol-metric likelihoods across the whole message before one decode.
  **Avoid** capped averaging of spectra and independent decode attempts on repeats. **Adapt** drift
  handling: neither FT8 (constant frequency, 1 Hz per 12.6 s at threshold) nor Q65 (one linear rate)
  models a path that wanders ±1 Hz over tens of minutes plus oscillator drift; a long integration needs
  piecewise tracks.

### 6.7 AP-style known-bit decoding in a keyed system

- AP in FT8 and Q65 works by shrinking the message set, and its gain grows with spread: 1.9 → 10.3 dB
  (FT8), 0.5 → 3 dB (Q65). The prior anchors chapter is right that message priors do not exist for an
  unpredictable payload.
- Three things do transfer. (1) Bits that both ends already know (frame counters derived from the
  schedule, fixed type or address fields) can be clamped at no cost. (2) For 10–20-bit messages the whole
  codebook is a list: exhaustive likelihood ranking of every codeword is a matrix product and is the
  Q65 list decoder taken to its limit, including sync from the full sequence. (3) The false-accept rule
  transfers: threshold raised by the logarithm of the list size (Q65), plus a margin over the runner-up
  (1.10 in Q65 sync, 1.3 in FT8's a7).
- **Avoid** borrowing the CRC sizes. FT8's 14 bits and Q65's 12 bits are sized for a few thousand decoder
  trials per slot with extra plausibility gates. A receiver that tests millions of time, frequency,
  drift and spread hypotheses per message needs the check length set from that count.

### 6.8 Other elements

| Element | Verdict | Basis |
|---|---|---|
| Gray mapping | Irrelevant once candidates are scattered and metrics are per symbol | S1's reason is adjacent-tone confusion |
| OSD fallback after BP | Adopt the principle (near-ML for short codes; 1.2–1.7 dB on fading) | §1.4 |
| Subtraction of decoded signals | Not applicable to one signal; the gain tracker (±2 Hz) is a ready-made post-decode channel estimator | §1.6 |
| Late-start tolerance by a "last two arrays" metric | Adopt as erasure-tolerant acquisition; interior gaps are erasures | §1.3, §1.8 |
| Percentile baselines, clipping, carrier removal | Adopt | §1.3, §3.3 |
| Fixed design `Es/N0` in the metric | Adopt per configuration | §3.2 |

---

## 7. Contradictions and corrections to prior repository documents

1. **Q65 Fig. 1 table in the Sol practice review.** Two columns are swapped. Viewing the figure (solid
   lines no AP, dashed maximum AP; circles 60D, squares 120E) gives the table in §3.3. The values the Sol
   table lists as "60D, maximum AP" are 120E without AP, and its "120E, no AP" values are 60D with
   maximum AP. Consequences: 120E without AP loses 6.3 dB from 0 to 100 Hz, not 3.6 dB; the 3.6–3.8 dB
   figure belongs to the maximum-AP curves. The Sol text that 120E "lost less" than 60D, and that the
   data agree with the branch-count model "only partly", should be withdrawn: both no-AP curves lose
   6.1–6.3 dB. Its "how close under spread" paragraph used the correct 60D row and stands.
2. **DOI attribution in the Sol waveform review.** `10.1109/TWC.2007.06040154` resolves to Guillén i
   Fàbregas and Grant, "Capacity Approaching Codes for Non-Coherent Orthogonal Modulation". Cheng,
   Valenti and Torrieri, "Robust Iterative Noncoherent Reception of Coded FSK over Block Fading Channels",
   is `10.1109/TWC.2007.06020004`. Both verified through Crossref.
3. **"FT8 loses 12.2 dB at 10 Hz"** (anchors chapter, Opus review). The 50 % point moves by 12.2 dB, but
   the curve saturates near 0.66. Any criterion above about 65 % is unreachable; the entry should be read
   as a failure, not a penalty.
4. **Q65 averaging depth.** The Opus review relays a secondary simulation in which about 35 periods buy
   6 dB. The current source averages linearly over four sequences and then exponentially with weight
   1/4. The two are not reconciled; the secondary result may come from an earlier build or its own
   averaging.
5. **STANAG 4415.** The Sol review's "−7 dB, 40 Hz, approaching 12 ms" (FFI overview) and the vendor-
   transcribed requirement table (−1 to 0 dB, up to 50 Hz, 10 ms) are different statements; the second
   is the specification's test condition. Neither supports 16 ms.
6. **Sharpening, not contradiction.** The Sol waveform review warns that rectangular tones are not an FT8
   model. For detection the difference is 0.05 dB and a −32 dB floor (§1.2); for emitted spectrum it is
   tens of dB.
7. **FST4W-120 "−24 dB at 2 Hz"** in both Opus documents follows the paper's text; the paper's own figure
   puts that point near 2.5 Hz.

---

## 8. Design consequences (testable statements)

1. **No cliff.** With same-symbol candidates at least 10 bins apart and a spread-weighted energy metric,
   decode probability reaches 99 % at some finite SNR for every spread from 0 to 30 Hz. FT8's reference
   behaviour is a ceiling of 0.66 at 10 Hz.
2. **Slope.** With 160 ms symbols, 64 candidates and a symbol-metric code, the 50 % threshold rises by no
   more than about 1.5 dB at 10 Hz (spread × T = 1.6) and 2.5 dB at 30 Hz, by scaling Q65's curves.
   Failing this by more than 1 dB indicates a metric or noise-estimate fault, not a channel limit.
3. **Shaping.** On the real transmit chain, Gaussian transitions with BT ≥ 8 lose at most 0.35 dB of
   wanted-bin energy for full-band hops; BT = 2 loses about 2.4 dB. Measure wanted-bin energy and power
   more than 100 Hz outside the hop band for BT = 2, 4, 8, 16 and plain CPFSK.
4. **Receiver model.** A rectangular one-symbol DFT is within 0.1–0.35 dB of matched for the shaping
   chosen in item 3; no shaped matched filter is needed.
5. **Alphabet.** At equal payload, airtime and symbol length, 64 candidates with a GF(64)-class code beat
   8 candidates with a binary code and bit metrics by at least 1 dB on AWGN and 2 dB on independent
   Rayleigh symbols (capacity predicts 1.9–2.0 dB and 3.0 dB including the bit-metric loss). A smaller
   gain means the short code, not the alphabet, is limiting.
6. **Bit metrics.** If a binary code is used, 16 candidates are no worse than 64.
7. **Multi-symbol metrics.** Two- and three-symbol coherent metrics gain at most 0.7 dB at zero spread,
   under 0.5 dB at spread × block duration 0.5 and nothing at 2, and only for symbols within the
   coherence bandwidth. If hops are unconstrained, expect no gain.
8. **Pilot fraction.** Sweep the fraction of single-candidate positions (0, 5, 10, 15, 27 %) at the
   lowest target `C/N0`. Acquisition failure must stay below one tenth of decode failure at threshold.
   Prediction: with 64 candidates and per-symbol `Es/N0` below 0 dB, zero pilots is not the optimum.
9. **Sync ambiguity.** For the keyed pattern, the largest sync statistic at any wrong time or frequency
   alignment inside the search window stays below 15 % of the true peak at high SNR (FT8: 3 of 21; Q65:
   about 14 %).
10. **Erasures.** With 35 % of symbols removed in one contiguous gap, decoding still succeeds 10 dB above
    threshold (FT8 reference: 36 % at 8.8–10.8 dB margin).
11. **Drift.** State the tolerated frequency rate at threshold per coherent segment and verify it. FT8's
    reference is 1/6 bin per frame for no loss and 0.6 bin for 50 %.
12. **Spread estimation.** Estimating spread from accumulated spectra, then decoding once, is within
    0.3 dB of the best of seven trial decodes on a ×1.72 spread grid.
13. **Noise baseline.** With one carrier 30 dB above the noise inside the hop band, threshold moves by
    less than 0.5 dB when baselines are per-bin percentiles with clipping; a band-mean noise estimate
    fails this.
14. **Small codebooks.** For payloads up to about 16 bits, exhaustive codeword ranking is within 0.5 dB of
    the finite-length noncoherent bound, and the false-accept rate stays flat when the list size changes
    if the threshold moves with the logarithm of the list size.
15. **Check length.** The false-accept probability is stated for the total number of hypotheses tested
    per message, and the check field is sized from it.
16. **Averaging.** One decode on likelihoods accumulated over K repeats beats the best of K separate
    decodes and beats spectrum averaging with a capped time constant, for K ≥ 8.

---

## 9. Weaknesses and unknowns

- **50 % criterion.** Every WSJT threshold here is a 50 % point. The 50 → 90 % step is 1 dB on AWGN and
  2–3 dB on 1 Hz channels for FT8/FT4; it is not published for Q65 under AP.
- **Figure readings.** Q65 values are read from two plots (±0.2 dB), FST4W and WSPR values from a
  log-axis plot (±0.5 dB, ±0.3 Hz), FT8 ceiling and 90 % points from Fig. 7 (±0.02 in probability,
  ±0.3 dB).
- **Spread definitions differ** between F.1487, Q65's simulator and the FST4W paper's input and output
  quantities. The normalised table in §2.1 mixes them.
- **No independent replication** of any FT8, FT4 or Q65 threshold-versus-spread result was found; all
  come from the developers' simulators. JS8 has no measured thresholds at all.
- **Relayed material.** Items marked (deleg.) were read by a delegated agent, some through a summarising
  fetch. These include the drift measurements, the HF spread statistics, the STANAG table, the Olivia
  description, the low-power cases and the 2024 developer statement about the FT8 ceiling (which the
  reviewer did confirm independently from S1's Fig. 7).
- **STANAG 4415.** The requirement table is a vendor transcription with three unread rows and an
  unstated bandwidth. The standard was not read. Behaviour beyond 50 Hz and 10 ms is unknown. The origin
  of the "16 ms" figure is unknown.
- **Source-code inferences.** Explanations offered for Q65's long-submode shortfall and for the 120E/60D
  gap rest on reading hypothesis ranges in the source, not on tests. The interaction with the peaked
  Lorentzian weights was not quantified.
- **Derived capacities** assume infinite block length, ideal likelihoods with known `Es/N0`, one energy
  observation per tone and independent Rayleigh symbols. They say nothing about 60–300-symbol codes, sync
  loss or spread. The 0.1 dB disagreement with one published 8-FSK value is unexplained.
- **Shaping calculation** assumes an ideal transmitter, uniformly random hops and no SSB filter. A real
  filter will convert out-of-band skirt into envelope ripple for plain CPFSK; that was not modelled.
- **Sync dilution** (§6.3) is a small-signal deflection argument, not a detection-probability result.
- **The target regime is untested by these modes.** No FT8 or Q65 result exists at per-symbol `Es/N0`
  below about +3.5 dB, at message durations beyond 30 minutes on HF, or at `C/N0` below +3 dB-Hz on HF at
  milliwatt power.
- **Low-power cases** rest on self-reported or calculated powers, single nights, and a database that
  records only successes. Implied losses depend on an assumed receiver noise density that spans 10 dB.
- **False-decode rates** for FT8 AP, FT8 a7 and Q65 list decoding are not published.
- **Auroral and trans-equatorial behaviour** of FT8 is anecdotal only; no spread values with decode
  statistics were found.

---

## 10. Sources

Verification on 26 November 2026. **[verified]**: DOI resolved through `api.crossref.org` or arXiv
identifier through the arXiv API by the reviewer, or URL fetched by the reviewer. **[verified-D]**: URL
fetched by a delegated agent and not re-fetched. **[unverified]**: not fetched in this pass. Reading depth
is stated.

### 10.1 WSJT primary sources

- **S1** S. Franke, B. Somerville, J. Taylor, "The FT4 and FT8 Communication Protocols," QEX, July/August
  2020, pp. 7–17. https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf [verified; full text; Figs. 7–9 viewed].
- **S2** WSJT-X source, master branch: `lib/ft8/ft8_params.f90`, `sync8.f90`, `ft8b.f90`,
  `decode174_91.f90`, `osd174_91.f90`, `subtractft8.f90`, `gen_ft8wave.f90`, `ft8_a7.f90`,
  `lib/ft8_decode.f90`. https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/lib/ft8/ [verified;
  `sync8`, `ft8b`, `subtractft8`, `gen_ft8wave` read in full, others by targeted extraction].
- **S3** WSJT-X source, master branch: `lib/qra/q65/q65.c`, `q65.f90`, `q65_loops.f90`,
  `q65_set_list.f90`, `lib/q65_decode.f90`, `lib/qra/qracodes/qracodes.c`.
  https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/lib/qra/ [verified; targeted extraction of
  comments, constants and loops].
- **S4** J. Taylor, B. Somerville, S. Franke, N. Palermo, "Quick-Start Guide to Q65," 3 April 2021.
  https://wsjt.sourceforge.io/Q65_Quick_Start.pdf [verified; full text; Figs. 1–2 viewed].
- **S5** WSJT-X User Guide, protocol specifications,
  https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/doc/user_guide/en/protocols.adoc [verified;
  targeted]; WSJT-X release notes, https://wsjt.sourceforge.io/wsjtx-doc/Release_Notes.txt [verified;
  through a summarising fetch, first 100,000 of 104,663 characters].
- **S6** N. Palermo, "Q-ary Repeat-Accumulate Codes for Weak Signals Communications," XVII EME
  Conference, Venice, 2016 (slides).
  http://www.eme2016.org/wp-content/uploads/2016/08/EME-2016-IV3NWV-Presentation.pdf [verified; text
  layer read, figures not viewed].
- **S7** J. Taylor, "WSJT-X: New Codes, Modes and Tools for Weak-Signal Communication," EME Conference,
  Venice, 2016 (slides). https://wsjt.sourceforge.io/K1JT_EME_2016_Venice.pdf [verified; text layer,
  targeted].
- **S8** G. Griffiths, G. Elmore, R. Robinett, L. Rhymes, D. Watrous, "FST4W on the HF Bands," TAPR/ARRL
  Digital Communications Conference, 2022. https://web.tapr.org/tech_docs/DCCpapers/2022%2002.pdf
  [verified-D for the fetch; Figs. 1 and 8 and selected text read by the reviewer from the cached copy].
- **S9** S. Franke, B. Somerville, J. Taylor, "Quick-Start Guide to FST4 and FST4W," 2020.
  https://wsjt.sourceforge.io/FST4_Quick_Start.pdf [verified-D; also read in the prior documents].
- **S10** J. Taylor, S. Franke, "SuperFox and FT8: Weak-Signal Performance," 6 September 2024.
  https://wsjt.sourceforge.io/SuperFox_Performance.pdf [verified-D; read directly by the agent].
- **S11** J. Taylor, message to wsjt-devel, 6 April 2018.
  https://sourceforge.net/p/wsjt/mailman/message/36286180/ [verified; through a summarising fetch].
- **S12** B. Somerville, same thread. https://sourceforge.net/p/wsjt/mailman/message/36286016
  [verified-D; summarising fetch].
- **S13** J. Taylor, wsjt-devel, 7 September 2024, quoted in
  https://sourceforge.net/p/wsjt/mailman/message/58813960 [verified-D; summarising fetch].
- **S14** J. Taylor, wsjt-devel, 29 June 2017.
  https://mail-archive.com/wsjt-devel@lists.sourceforge.net/msg08704.html [verified-D; summarising
  fetch].

### 10.2 Measurements, independent simulations and propagation

- **S15** R. Rehr (W3SZ), "VHF/UHF/Microwave: Which JT mode should I choose?", VHF Super Conference,
  April 2019. https://w3sz.com/W3SZ-VHF-UHF-uWave-NewDataWhichJTModeNew4-3-19.pdf [verified-D; read
  directly by the agent].
- **S16** D. Estévez (EA4GPZ): JT-mode simulations, 8 October 2016, https://destevez.net/?p=1677;
  "WSJT-X and linear satellites, part I," 12 August 2017,
  https://destevez.net/2017/08/wsjt-x-and-linear-satellites-part-i/; FO-29 test, 23 October 2017,
  https://destevez.net/?p=3406 [verified-D; summarising fetch].
- **S17** B. Atkins (KA1GT), "Q65 – choice of mode for 1296 EME."
  https://bobatkins.com/radio/q65_mode_selection.html [verified-D].
- **S18** R. Moncur (VK7MO), C. Suckling (G3WDG), "Comparisons of QRA64 and JT4 for 3 cm EME," revised
  9 June 2018. https://www.ok2kkw.com/more/comparisons_of_qra64_and_jt4_for_3cm_eme.htm [verified-D].
- **S19** ITU-R Recommendation F.1487, "Testing of HF modems with bandwidths of up to about 12 kHz using
  ionospheric channel simulators," 2000. https://www.itu.int/rec/R-REC-F.1487/en [unverified in this
  pass; channel classes taken from S1 and the prior documents].
- **S20** G. Griffiths (G3ZIL): HamSCI Workshop 2023 abstract, https://www.hamsci.org/node/697; RadCom
  July 2024 pp. 40–42,
  https://hamsci.org/sites/default/files/publications/p40_42_Griffiths_RSGB_RadCom_July24.pdf; RadCom
  August 2024 pp. 42–44,
  https://hamsci.org/sites/default/files/publications/p42_44_Reflection_Height_RSGB_RadCom_Aug24.pdf
  [verified-D; PDFs read directly by the agent, abstract through a summarising fetch].
- **S21** K. Collins, J. Gibbons, N. Frissell, A. Montare, et al., "Crowdsourced Doppler measurements of
  time standard stations demonstrating ionospheric variability," Earth System Science Data 15(3),
  1403–1418, 2023. https://doi.org/10.5194/essd-15-1403-2023 [verified; preprint read by the agent].
- **S22** M. Angling, P. Cannon, N. Davies, T. Willink, et al., "Measurements of Doppler and multipath
  spread on oblique high-latitude HF paths and their use in characterizing data modem performance," Radio
  Science 33(1), 97–107, 1998. https://doi.org/10.1029/97RS02206 [verified; abstract only].
- **S23** N. Frissell, J. Vega, E. Markowitz, A. Gerrard, et al., "High-Frequency Communications Response
  to Solar Activity in September 2017 as Observed by Amateur Radio Networks," Space Weather 17(1),
  118–132, 2019. https://doi.org/10.1029/2018SW002008 [verified; abstract only; no spread data].
- **S24** S. Fernandes et al., HamSCI Workshop 2024 slides.
  https://hamsci.org/sites/default/files/pages/hamsci_workshop_2024/orals/O08_Sabastian_Fernandes.pdf
  [verified-D].
- **S25** Anecdotal reports: https://groups.google.com/g/pnwvhfs/c/x7-pBO19WGc (6 m aurora);
  https://medxr.blogspot.com/2026/03/strong-signal-no-decodes-auroral.html;
  https://groups.google.com/g/hamsci/c/MiAjHSjxiKk (144 MHz TEP); https://dxmaps.com/tep.html;
  https://www.qsl.net/ei7gl/ei5dd6m.htm (search snippet only) [verified-D except the last].
- **S26** J. Costas, "A study of a class of detection waveforms having nearly ideal range–Doppler
  ambiguity properties," Proceedings of the IEEE 72(8), 996–1009, 1984.
  https://doi.org/10.1109/PROC.1984.12967 [verified; not read, cited for the array property].
- **S27** M. Fossorier, S. Lin, "Soft-decision decoding of linear block codes based on ordered
  statistics," IEEE Transactions on Information Theory 41(5), 1379–1396, 1995.
  https://doi.org/10.1109/18.412683 [verified; not read].
- **S28** Y. Wu, C. Hadjicostis, "Soft-Decision Decoding of Linear Block Codes Using Preprocessing and
  Diversification," IEEE Transactions on Information Theory 53(1), 378–393, 2007.
  https://doi.org/10.1109/TIT.2006.887478 [verified; not read; S1's reference 17].
- **S29** C. Watterson, J. Juroshek, W. Bensema, "Experimental Confirmation of an HF Channel Model," IEEE
  Transactions on Communication Technology 18(6), 792–803, 1970.
  https://doi.org/10.1109/TCOM.1970.1090438 [verified; not read in this pass].

### 10.3 Military and amateur MFSK/Walsh designs

- **S30** RapidM, "STANAG 4415" product-standard page. https://www.rapidm.com/standard/stanag-4415/
  [verified-D; table aligned by row order after text conversion].
- **S31** MIL-STD-188-110C with Change 1, 23 September 2011.
  https://www.sigidwiki.com/images/c/c8/MIL-STD-188_110C_CHG_NOTICE-1.pdf [verified-D; text-searched].
- **S32** M. Jorgenson, K. Moreland, "HF Serial-Tone Waveform Design," NATO RTO MP-26, 1999 (copy at
  n5dux.com; exact URL not recorded by the agent) [unverified URL; read by the agent].
- **S33** Hunt et al., "Evaluation and Comparison …" (wideband HF Walsh waveform simulation), Idaho
  National Laboratory report INL/CON-22-67079, August 2022 (INL digital library; exact URL not recorded)
  [unverified URL; read by the agent].
- **S34** V. Jodalen, R. Otnes, B. Solberg, "NATO standards for HF communications: an overview and
  technical description," FFI/RAPPORT-2004/02015.
  https://www.ffi.no/en/publications-archive/nato-standards-for-hf-communications-an-overview-and-technical-description
  [unverified in this pass (connection error); cited in the Sol practice review].
- **S35** Verified DOIs, abstracts or snippets only: M. Jorgenson, R. Johnson, et al., "Improving the LPI
  and self-interference characteristics of the STANAG 4415 very robust HF waveform," MILCOM 2002,
  115–119, https://doi.org/10.1109/MILCOM.2002.1180424; V. Jodalen, T. Bergsvik, P. Cannon, P. Arthur,
  "Performance of HF modems on high-latitude paths using multiple frequencies," Radio Science 36(6),
  1687–1698, 2001, https://doi.org/10.1029/2000RS002547; T. Willink, N. Davies, M. Angling, V. Jodalen,
  et al., "Robust HF data communications at high latitudes," IEE Proceedings – Microwaves, Antennas and
  Propagation 146(4), 263, 1999, https://doi.org/10.1049/ip-map:19990575; P. Bergadà, R. Alsina-Pagès,
  J. Pijoan, M. Salvador, et al., "Digital transmission techniques for a long haul HF link: DSSS versus
  OFDM," Radio Science 49(7), 518–530, 2014, https://doi.org/10.1002/2013RS005203; A. Stocker,
  E. Warrington, D. Siddle, "Observations of Doppler and delay spreads on HF signals received over polar
  cap and trough paths at various stages of the solar cycle," Radio Science 48(5), 638–645, 2013,
  https://doi.org/10.1002/2013RS005264 [all verified].
- **S36** Olivia description (mirror of P. Jalocha's draft), https://www.arrl.org/olivia; fldigi mode
  documentation (w1hkj.org; exact pages not recorded); M. Greenman (ZL1BPU), MFSK16 specification
  rev. 1.2, 11 July 2000 (qsl.net; exact URL not recorded), "MFSK for the New Millennium," QST, January
  2001, pp. 33–36, and DominoEX technical pages; T. Bombardiere (K2MO), path-simulator tests,
  https://www.mail-archive.com/digitalradio@yahoogroups.com/msg18007/Path_Simulator_Tests.rtf
  [verified-D for the pages fetched; URLs without a full address are unverified].

### 10.4 Coding and detection literature

- **S40** S. Cheng, M. Valenti, "Bit-interleaved turbo-coded noncoherent orthogonal modulation with
  iterative demodulation and decoding: capacity limits and convergence analysis," 38th Asilomar
  Conference, 2004, 2020–2024. https://doi.org/10.1109/ACSSC.2004.1399519 [verified; full text read by
  the agent]. Companion: M. Valenti, E. Hueffmeier, B. Bogusch, J. Fryer, "Towards the capacity of
  noncoherent orthogonal modulation: BICM-ID for turbo coded NFSK," MILCOM 2004, 1549–1555.
  https://doi.org/10.1109/MILCOM.2004.1495169 [verified; full text read by the agent].
- **S41** M. Valenti, S. Cheng, "Iterative demodulation and decoding of turbo-coded M-ary noncoherent
  orthogonal modulation," IEEE Journal on Selected Areas in Communications 23(9), 1739–1747, 2005.
  https://doi.org/10.1109/JSAC.2005.853794 [verified; full text read by the agent].
- **S42** A. Guillén i Fàbregas, A. Grant, "Capacity approaching codes for the non-coherent FSK channel,"
  Australian Communications Theory Workshop, 2006, 152–158. https://doi.org/10.1109/AUSCTW.2006.1625273
  [verified; full text read by the agent]. Journal version: "Capacity Approaching Codes for Non-Coherent
  Orthogonal Modulation," IEEE Transactions on Wireless Communications 6(11), 4004–4013, 2007.
  https://doi.org/10.1109/TWC.2007.06040154 [verified; abstract only]. For the correction in §7: S. Cheng,
  M. Valenti, D. Torrieri, "Robust Iterative Noncoherent Reception of Coded FSK over Block Fading
  Channels," IEEE Transactions on Wireless Communications 6(9), 3142–3147, 2007.
  https://doi.org/10.1109/TWC.2007.06020004 [verified].
- **S43** O. Abassi, L. Conde-Canencia, M. Mansour, E. Boutillon, "Non-Binary Low-Density Parity-Check
  coded Cyclic Code-Shift Keying," IEEE WCNC 2013, 3890–3894. https://doi.org/10.1109/WCNC.2013.6555196
  [verified; abstract only].
- **S44** G. Liva, B. Matuz, E. Paolini, M. Flanagan, "Non-binary LDPC codes for orthogonal modulations:
  Analysis and code design," IEEE ICC 2017. https://doi.org/10.1109/ICC.2017.7996749 [verified; abstract
  only]. Related, abstracts only: B. Matuz, G. Liva, E. Paolini, M. Chiani, et al., "Low-Rate Non-Binary
  LDPC Codes for Coherent and Blockwise Non-Coherent AWGN Channels," IEEE Transactions on Communications
  61(10), 4096–4107, 2013, https://doi.org/10.1109/TCOMM.2013.090513.130320; J. Bourduge, C. Poulliat,
  B. Gadat, "Non Binary LDPC Coded Orthogonal Modulation Schemes based on Non Binary Multilevel Coding,"
  IEEE ISIT 2023, 2517–2522, https://doi.org/10.1109/ISIT54713.2023.10206851; H. Hu, Y. Pei, Zhang,
  N. Ge, "Non-binary LDPC Coded Orthogonal Modulation with Non-coherent Detection," ICT 2012,
  https://doi.org/10.1109/ICTEL.2012.6221216; V. Savin, "Non-Binary Polar Codes for Spread-Spectrum
  Modulations," ISTC 2021, https://doi.org/10.1109/ISTC49272.2021.9594166 [all verified].
- **S45** J. King, W. Ryan, R. Wesel, "CRC-Aided Short Convolutional Codes and RCU Bounds for Orthogonal
  Signaling," IEEE GLOBECOM 2022, 4256–4261. https://doi.org/10.1109/GLOBECOM48099.2022.10001459;
  arXiv:2210.00026, https://arxiv.org/abs/2210.00026 [both verified; full text read by the agent].
- **S46** Ohyama, Iwamoto, Kita, Kanatake, et al., "Study on application of communication in JS8 mode in
  the amateur radio high frequency band for telemedicine in the event …," IEEE/WIC/ACM WI-IAT 2022,
  696–701. https://doi.org/10.1109/WI-IAT55865.2022.00111 [verified; abstract only].
- **S47** Tanabe, Yoshii, Shimamoto, "FT8-Based Ionospheric Condition Estimation for Aerial and
  Spaceborne Communications," IEEE VTC2026-Spring. https://doi.org/10.1109/VTC2026-Spring68026.2026.11756712
  [verified; abstract only].
- **S48** Weber, Nowak, Berg, Behdad, "Wideband HF Skywave Propagation: A Review with Link Modeling
  Corroborated by Propagation Data," IEEE Open Journal of Antennas and Propagation, 2026.
  https://doi.org/10.1109/OJAP.2026.3730332 [verified; abstract only].
- **S49** M. Kozlenko, I. Lazarovych, V. Tkachuk, V. Vialkova, "Software demodulation of weak radio
  signals using convolutional neural network," arXiv:2502.19097. https://arxiv.org/abs/2502.19097
  [verified; recorded in the prior documents].
- **S50** L. Quesada, "The LQ Digital Mode Family: Protocol Architecture and Reference Specification for
  Weak-Signal Communications," arXiv:2609.06427. https://arxiv.org/abs/2609.06427 [verified; read by the
  agent].
- **S51** L. Barnault, D. Declercq, "Fast decoding algorithm for LDPC over GF(2^q)," IEEE ITW 2003, 70–73.
  https://doi.org/10.1109/ITW.2003.1216697 [verified; abstract only].
- **S52** D. Declercq, M. Fossorier, "Decoding Algorithms for Nonbinary LDPC Codes Over GF(q)," IEEE
  Transactions on Communications 55(4), 633–643, 2007. https://doi.org/10.1109/TCOMM.2007.894088
  [verified; abstract only].
- **S53** A. Voicila, D. Declercq, F. Verdier, M. Fossorier, et al., "Low-complexity decoding for
  non-binary LDPC codes in high order fields," IEEE Transactions on Communications 58(5), 1365–1375, 2010.
  https://doi.org/10.1109/TCOMM.2010.05.070096 [verified; abstract only].
- **S54** V. Savin, "Min-Max decoding for non binary LDPC codes," IEEE ISIT 2008, 960–964.
  https://doi.org/10.1109/ISIT.2008.4595129 [verified; abstract only].
- **S55–S57** GPU decoders, abstracts only: Andrade, Falcao, Silva, Kasai, "FFT-SPA non-binary LDPC
  decoding on GPU," ICASSP 2013, https://doi.org/10.1109/ICASSP.2013.6638633; Beermann, Monro, Schmalen,
  Vary, "High speed decoding of non-binary irregular LDPC codes using GPUs," SiPS 2013,
  https://doi.org/10.1109/SiPS.2013.6674477; Wang, Shen, Yin, Wu, "Parallel nonbinary LDPC decoding on
  GPU," Asilomar 2012, https://doi.org/10.1109/ACSSC.2012.6489229 [all verified]. Also J. Jabour,
  A. Chamas Al-Ghouwayel, E. Boutillon, "Fast polarisation-aware decoder for non-binary polar codes,"
  Annals of Telecommunications, 2026, https://doi.org/10.1007/s12243-026-01192-7; arXiv:2509.09554
  [verified; abstract only].
- **S58** M. Valenti, S. Cheng, D. Torrieri, "Iterative Multisymbol Noncoherent Reception of Coded CPFSK,"
  IEEE Transactions on Communications 58(7), 2046–2054, 2010.
  https://doi.org/10.1109/TCOMM.2010.07.090453 [verified; full text read by the agent]. Related:
  S. Cheng, M. Valenti, D. Torrieri, "Coherent and Multi-symbol Noncoherent CPFSK: Capacity and Code
  Design," MILCOM 2007, https://doi.org/10.1109/MILCOM.2007.4454912 [verified; abstract only].
- **S59** M. Simon, D. Divsalar, "Maximum-likelihood block detection of noncoherent continuous phase
  modulation," IEEE Transactions on Communications 41(1), 90–98, 1993. https://doi.org/10.1109/26.212365
  [verified; abstract only]. Also verified, abstracts only: S. Hinedi, M. Simon, D. Raphaeli, "The
  performance of noncoherent orthogonal M-FSK in the presence of timing and frequency errors," IEEE
  Transactions on Communications 43(2/3/4), 922–933, 1995, https://doi.org/10.1109/26.380125;
  G. Vitetta, U. Mengali, D. Taylor, "Optimal noncoherent detection of FSK signals transmitted over
  linearly time-selective Rayleigh fading channels," IEEE Transactions on Communications 45(11),
  1417–1425, 1997, https://doi.org/10.1109/26.649762; W. Osborne, M. Luntz, "Coherent and Noncoherent
  Detection CPFSK," IEEE Transactions on Communications 22(8), 1023–1036, 1974,
  https://doi.org/10.1109/TCOM.1974.1092333.
- **S60** J. Gómez-Vilardebó, X. Mestre, M. Navarro, Quintanilla, et al., "On Noncoherent FSK Reception
  With Doppler Frequency Uncertainty for Space Communications," IEEE Journal on Selected Areas in
  Communications 42(5), 1292–1303, 2024. https://doi.org/10.1109/JSAC.2024.3365881 [verified; recorded in
  the prior documents].
- **S61** K. Saied, A. Al Ghouwayel, E. Boutillon, "Short Frame Transmission at Very Low SNR by
  Associating CCSK Modulation With NB-Code," IEEE Transactions on Wireless Communications 21(9),
  7194–7206, 2022. https://doi.org/10.1109/TWC.2022.3156628 [verified; abstract only].
- **S62–S66** Companion papers, abstracts only: "Time-Synchronization of CCSK Short Frames," WiMob 2021,
  https://doi.org/10.1109/WiMob52687.2021.9606328; "Time sliding window for the detection of CCSK
  frames," SiPS 2021, https://doi.org/10.1109/SiPS52927.2021.00026; "Phase Synchronization for
  Non-Binary Coded CCSK Short Frames," VTC2022-Spring,
  https://doi.org/10.1109/VTC2022-Spring54318.2022.9860930; "Weighted Coherent Detection of QCSP
  frames," WCNC 2023, https://doi.org/10.1109/WCNC55385.2023.10118735; C. Monière, B. Le Gal,
  E. Boutillon, "Real-time energy-efficient software and hardware implementations of a QCSP communication
  system," Journal of Systems Architecture 141, 102933, 2023,
  https://doi.org/10.1016/j.sysarc.2023.102933; C. Marchand, E. Boutillon, "Rate-adaptive Inner Code for
  Non-Binary Decoders," ISTC 2021, https://doi.org/10.1109/ISTC49272.2021.9594157 [all verified].
- Also verified, not read: Y. Polyanskiy, H. V. Poor, S. Verdú, "Minimum Energy to Send k Bits Through
  the Gaussian Channel With and Without Feedback," IEEE Transactions on Information Theory 57(8),
  4880–4902, 2011, https://doi.org/10.1109/TIT.2011.2159041; T. Ferrett, M. Valenti, "Noncoherent
  LDPC-Coded Physical-Layer Network Coding using Multitone FSK," arXiv:1801.10370,
  https://arxiv.org/abs/1801.10370.

### 10.5 Low-power evidence

- **S67** J. Taylor, B. Walker, "WSPRing Around the World," QST, November 2010.
  https://wsjt.sourceforge.io/WSPR_QST_Nov_2010.pdf [verified-D; read directly by the agent].
- **S68** QRP Labs flight pages for VE3KCL S-4 and S-3: https://qrp-labs.com/flights/s4,
  https://qrp-labs.com/flights/s3.html; tracker page https://qrp-labs.com/u4b [verified-D].
- **S69** B. Klofas, "Picoballoon launch 10, weeks 1 and 2," 2021.
  https://klofas.com/blog/2021/picoballoon-launch-10-weeks-1-and-2/ [verified-D].
- **S70** https://picospace.net/ and an ARRL news item (URL not recorded) [verified-D for the first].
- **S71** MW1CFN blog, September 2020:
  https://mw1cfnradio.blogspot.com/2020/09/qrppppppppppppppppppppppppp.html and
  https://mw1cfnradio.blogspot.com/2020/09/and-noweven-lower-power.html [verified-D].
- **S72** G3XBM blog, December 2023 and January 2024: https://g3xbm-qrp.blogspot.com/2024/01,
  https://g3xbm-qrp.blogspot.com/2023/12 [verified-D].
- **S73** https://www.nycresistor.com/2010/06/26/crossing-the-pond-with-100-milliwatts/ [verified-D].
- **S74** https://ei7gl.blogspot.com/search?q=qrss [verified-D].
- **S75** https://www.zianet.com/dhassall/BILLIONMPW.html and
  https://pensacolasnapper.blogspot.com/2014/05 [verified-D].
- **S76** wspr.live public database, two bounded queries for October 2026, https://wspr.live [verified-D].
- **S77** T. McKinney, N. Perlaky, M. Newchurch, B. Brown, "Insights on Polar Day Antarctica Radio
  Propagation Using Amateur Radio Beacons on Circumnavigating Balloons," Atmosphere 14(7), 1118, 2023.
  https://doi.org/10.3390/atmos14071118 [verified; abstract only].

### 10.6 Wanted but not accessed, or not found

- A QEX article on Q65; any document named "MAPP65"; the `qracodes` repository's own documentation (the
  GitHub repository holds project folders without a readme at the root).
- STANAG 4415 and STANAG 4538 texts (restricted); FFI reports 2004/02015 and 2000/02687 in this pass;
  MIL-STD-188-141 performance tables; any STANAG 4415 result beyond 50 Hz or 10 ms; the origin of
  "16 ms".
- Full texts of the QCSP papers, Guillén i Fàbregas and Grant 2007, Liva 2017, Abassi 2013, Hinedi 1995,
  Vitetta 1997, Angling 1998, McKinney 2023.
- G. Griffiths, "Identifying 14 MHz propagation modes using FST4W SNR and spectral spread," QEX May–June
  2024; median or percentile spread tables by band and path.
- Measured JS8 thresholds; independent FT8 decoder sensitivities in dB; FT8 or WSPR decode statistics
  against geomagnetic indices; FT8 failure statistics with spread values on auroral or trans-equatorial
  paths; HF measurements of Q65.
- Published false-decode rates for FT8 AP, FT8 a7 and Q65 list decoding.
- Any reception of 100 µW or less beyond about 5,200 km; QRSS record tables; forum threads on lowest
  power.
- The web-search allowance (shared with the delegated agents) was exhausted before follow-up searches on
  the STANAG delay figure and on Q65 false decodes and drift limits could be made.

---

## Appendix A. Calculations (theoretical; reproducible)

All three programs are standalone, use only the C++17 or Python 3 standard library, and were run on
26 November 2026. Outputs are quoted in the sections named.

### A.1 Sync-pattern ambiguity (§1.3, §3.5)

```python
# Sync-pattern ambiguity counts. FT8 Costas array and Q65 sync positions.
cos7=[3,1,4,0,6,5,2]
mx=0; hist={}
for dt in range(-6,7):
    for df in range(-6,7):
        if dt==0 and df==0: continue
        c=sum(1 for n in range(7) if 0<=n+dt<7 and cos7[n+dt]==cos7[n]+df)
        hist[c]=hist.get(c,0)+1; mx=max(mx,c)
print("FT8 Costas 7x7: max off-peak coincidences",mx,"hist",hist)
pos={}
for blk in (0,36,72):
    for n in range(7): pos[blk+n]=cos7[n]
best={}
for dt in range(-78,79):
    for df in range(-7,8):
        if dt==0 and df==0: continue
        c=sum(1 for p,t in pos.items() if (p+dt) in pos and pos[p+dt]==t+df)
        if c>=3: best[(dt,df)]=c
print(sorted(best.items(),key=lambda kv:-kv[1])[:8])
m2=0
for dt in range(-16,17):
    for df in range(-7,8):
        if dt==0 and df==0: continue
        m2=max(m2,sum(1 for p,t in pos.items() if (p+dt) in pos and pos[p+dt]==t+df))
print("FT8: max sidelobe hits within +/-16 symbols:",m2,"of 21")
isync=[1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85]
s=set(isync)
ac={d:sum(1 for p in isync if p+d in s) for d in range(1,85)}
print("Q65 sync hits (of 22) for shifts 1..12:",[ac[d] for d in range(1,13)], "max",max(ac.values()))
```

Output: one array, at most 1 coincidence (126 shifts with 0, 42 with 1); frame sidelobes 14 at ±36
symbols, 7 at ±72, at most 3 within ±16 symbols; Q65 shifts 1–12 give 3, 4, 5, 5, 5, 3, 5, 5, 5, 5, 8, 7
hits, maximum 8 (shifts 11 and 14). With Q65's template (+1 at sync positions, −22/63 elsewhere) a shift
with `h` hits scores `h − (22 − h)·22/63`, i.e. 3.1 of 22 for `h` = 8.

### A.2 Shaped constant-envelope MFSK: wanted-bin energy and skirt (§1.2, §6.2)

Model: modulation index 1, tone spacing 1/T, frequency pulse as S1 eq. (3), three-symbol pulse support,
tones independent and uniform over `M` bins centred on zero; optional raised-cosine amplitude ramp of a
fraction of T at each symbol edge with an instantaneous frequency jump. Detector: rectangular DFT over
one symbol. Spectrum: Hann periodogram over 8-symbol segments with half overlap. Arguments: `M`, `BT`
(≥ 50 means rectangular), samples per symbol, number of symbols, ramp fraction. Runs used: 8/4 tones with
256 samples per symbol and 4000 symbols; 16/64 tones with 512; 448 tones with 2048 and 3000 symbols.

```cpp
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
using cd = std::complex<double>;
static void fft(std::vector<cd>& a) {
  size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) { size_t b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) std::swap(a[i], a[j]); }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len; cd wl(cos(ang), sin(ang));
    for (size_t i = 0; i < n; i += len) { cd w(1); for (size_t k = 0; k < len / 2; k++) { cd u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
  }
}
int main(int argc, char** argv) {
  int M = atoi(argv[1]); double BT = atof(argv[2]); int nsps = atoi(argv[3]); int nsym = atoi(argv[4]);
  double ramp = argc > 5 ? atof(argv[5]) : 0.0;
  const double k = M_PI * sqrt(2.0 / log(2.0));
  std::mt19937_64 rng(12345); std::uniform_int_distribution<int> U(0, M - 1);
  std::vector<int> b(nsym); for (auto& x : b) x = U(rng);
  std::vector<double> p(3 * nsps);
  for (int i = 0; i < 3 * nsps; i++) { double t = (i + 0.5 - 1.5 * nsps) / nsps;
    p[i] = BT >= 50 ? (fabs(t) < 0.5 ? 1.0 : 0.0) : 0.5 * (erf(k * BT * (t + 0.5)) - erf(k * BT * (t - 0.5))); }
  std::vector<double> dphi((nsym + 2) * (size_t)nsps, 0.0);
  double off = -(M - 1) / 2.0;
  for (int n = 0; n < nsym; n++) for (int i = 0; i < 3 * nsps; i++) dphi[(size_t)n * nsps + i] += 2 * M_PI * (b[n] + off) / nsps * p[i];
  std::vector<cd> x((size_t)nsym * nsps); double ph = 0, pavg = 0;
  for (size_t j = 0; j < x.size(); j++) { double a = 1; int i = j % nsps; int nr = (int)(ramp * nsps);
    if (nr > 0) { if (i < nr) a = 0.5 * (1 - cos(M_PI * (i + 0.5) / nr)); else if (i >= nsps - nr) a = 0.5 * (1 - cos(M_PI * (nsps - i - 0.5) / nr)); }
    x[j] = a * cd(cos(ph), sin(ph)); pavg += a * a; ph += dphi[j + nsps]; ph = fmod(ph, 2 * M_PI); }
  pavg /= x.size();
  double ewant = 0, eadj[4] = {0, 0, 0, 0}; long cnt = 0;
  for (int n = 1; n < nsym - 1; n++) {
    for (int d = -3; d <= 3; d++) {
      double f = (b[n] + off + d) / nsps; cd s = 0;
      for (int i = 0; i < nsps; i++) { double a = -2 * M_PI * f * i; s += x[(size_t)n * nsps + i] * cd(cos(a), sin(a)); }
      double e = std::norm(s) / ((double)nsps * nsps);
      if (d == 0) ewant += e; else eadj[abs(d)] += e / 2;
    }
    cnt++;
  }
  printf("ramp=%g avg/peak %.2f dB | M=%d BT=%g: wanted %.4f (%.2f dB); leak +-1 %.1f, +-2 %.1f, +-3 %.1f dB\n", ramp, 10 * log10(pavg), M, BT,
         ewant / cnt, 10 * log10(ewant / cnt), 10 * log10(eadj[1] / cnt), 10 * log10(eadj[2] / cnt), 10 * log10(eadj[3] / cnt));
  size_t L = 8 * (size_t)nsps; std::vector<double> psd(L, 0.0); std::vector<cd> seg(L);
  for (size_t s0 = 0; s0 + L <= x.size(); s0 += L / 2) { for (size_t i = 0; i < L; i++) seg[i] = x[s0 + i] * (0.5 - 0.5 * cos(2 * M_PI * i / L)); fft(seg); for (size_t i = 0; i < L; i++) psd[i] += std::norm(seg[i]); }
  double tot = 0; for (double v : psd) tot += v;
  double binhz = 6.25 / 8.0;
  auto outside = [&](double h) { double in = 0; for (size_t i = 0; i < L; i++) { double f = (i < L / 2 ? (double)i : (double)i - L) * binhz; if (fabs(f) <= h) in += psd[i]; } return 1 - in / tot; };
  double span = (M - 1) * 6.25, lo = 0, hi = L / 2 * binhz;
  for (int it = 0; it < 40; it++) { double mid = (lo + hi) / 2; if (outside(mid) > 0.01) lo = mid; else hi = mid; }
  printf("   span %.1f Hz; 99%% BW %.1f Hz; beyond span/2+{6.25,25,100,300} Hz: %.1f %.1f %.1f %.1f dB\n", span, 2 * hi,
         10 * log10(outside(span / 2 + 6.25) + 1e-30), 10 * log10(outside(span / 2 + 25) + 1e-30), 10 * log10(outside(span / 2 + 100) + 1e-30), 10 * log10(outside(span / 2 + 300) + 1e-30));
}
```

Check: with a ramp of fraction `r` the wanted-bin energy is `(1 − r)²` and the mean power `1 − 1.25·r`;
the program returns 0.7656 and −0.74 dB for `r` = 1/8, as expected. Values printed below about −115 dB
are at the numerical floor of the window and are reported as "below −130 dB" only where the program
printed −130 dB or less.

### A.3 Symbol-metric and bit-metric limits of noncoherent M-FSK (§3.2, §4.2)

Model: one energy observation per tone; AWGN with unknown phase (likelihood `I0(2·sqrt(Es·y)/N0)`), or
Rayleigh fading independent per symbol without channel state (log-likelihood `y·γ/(1+γ)`); known `Es/N0`;
symmetric labelling. Symbol-metric information is `log2 M − E[log2(Σ_j p_j / p_x)]`; bit-metric
information sums, over the label bits, `1 − E[log2(Σ_j p_j / Σ_{j: same bit} p_j)]`. Bisection finds the
`Es/N0` that gives the target bits per symbol. Trials per point: 400,000 (M = 8), 200,000 (16),
100,000–150,000 (32), 60,000–120,000 (64), 40,000 (256).

```cpp
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
static double logI0(double x) { return x < 600 ? log(std::cyl_bessel_i(0.0, x)) : x - 0.5 * log(2 * M_PI * x); }
static void mi(int M, int chan, double esn0, long trials, double& cm, double& bicm, std::mt19937_64& g) {
  std::normal_distribution<double> N(0, sqrt(0.5)); int m = (int)round(log2(M)); std::vector<double> L(M);
  double scm = 0, sb = 0;
  for (long t = 0; t < trials; t++) {
    for (int k = 0; k < M; k++) {
      double re = N(g), im = N(g);
      if (k == 0) { if (chan == 0) re += sqrt(esn0); else { double s = sqrt(1 + esn0); re *= s; im *= s; } }
      double y = re * re + im * im;
      L[k] = chan == 0 ? logI0(2 * sqrt(esn0 * y)) : y * esn0 / (1 + esn0);
    }
    double mx = L[0]; for (int k = 1; k < M; k++) if (L[k] > mx) mx = L[k];
    double tot = 0; std::vector<double> z(m, 0.0);
    for (int k = 0; k < M; k++) { double e = exp(L[k] - mx); tot += e; for (int i = 0; i < m; i++) if (!((k >> i) & 1)) z[i] += e; }
    scm += log2(tot / exp(L[0] - mx));
    for (int i = 0; i < m; i++) sb += log2(tot / z[i]);
  }
  cm = m - scm / trials; bicm = m - sb / trials;
}
int main(int argc, char** argv) {
  int M = atoi(argv[1]); int chan = atoi(argv[2]); double R = atof(argv[3]); long trials = atol(argv[4]);
  std::mt19937_64 g(777);
  for (int which = 0; which < 2; which++) {
    double lo = -5, hi = 25;
    for (int it = 0; it < 14; it++) { double mid = (lo + hi) / 2, cm, b; mi(M, chan, pow(10, mid / 10), trials, cm, b, g); if ((which ? b : cm) < R) lo = mid; else hi = mid; }
    double es = (lo + hi) / 2;
    printf("M=%d %s %s: R=%.3f b/sym needs Es/N0 %.2f dB, Eb/N0 %.2f dB\n", M, chan ? "Rayleigh" : "AWGN", which ? "BICM" : "CM", R, es, es - 10 * log10(R));
  }
}
```

Output (`Es/N0`, `Eb/N0` in dB):

| M | Channel | Bits/symbol | Symbol metric | Bit metric |
|---:|---|---:|---|---|
| 8 | AWGN | 1.569 | 4.83, 2.88 | 5.68, 3.73 |
| 8 | AWGN | 1.328 | 4.14, 2.91 | 5.05, 3.82 |
| 8 | Rayleigh | 1.569 | 7.09, 5.13 | 7.89, 5.93 |
| 8 | Rayleigh | 1.328 | 5.91, 4.68 | 6.76, 5.53 |
| 16 | AWGN | 1.328 | 3.52, 2.28 | 4.98, 3.75 |
| 16 | Rayleigh | 1.328 | 4.73, 3.50 | 6.01, 4.78 |
| 32 | AWGN | 1.328 | 3.23, 2.00 | 5.19, 3.96 |
| 32 | Rayleigh | 1.328 | 4.08, 2.85 | 5.77, 4.54 |
| 64 | AWGN | 1.429 | 3.29, 1.74 | 5.67, 4.12 |
| 64 | AWGN | 1.328 | 3.06, 1.83 | 5.51, 4.28 |
| 64 | AWGN | 1.238 | 2.85, 1.92 | 5.38, 4.45 |
| 64 | Rayleigh | 1.429 | 4.04, 2.49 | 6.02, 4.47 |
| 64 | Rayleigh | 1.328 | 3.76, 2.53 | 5.74, 4.51 |
| 64 | Rayleigh | 1.238 | 3.49, 2.56 | 5.56, 4.63 |
| 256 | AWGN | 1.328 | 2.94, 1.71 | 6.25, 5.02 |

### A.4 Small-signal sync deflection with unknown data (§6.3)

For one symbol with the tone in one of `M` equiprobable candidate bins, unit noise per bin and symbol
SNR `γ ≪ 1`, the likelihood ratio against noise only is `(1/M)·Σ_j e^{−γ}·I0(2·sqrt(γ·y_j)) ≈ 1 +
(γ/M)·Σ_j (y_j − 1)`. The locally optimum statistic is the summed energy of the `M` bins: mean shift `γ`,
noise variance `M`. Deflection² per symbol is `γ²/M`, against `γ²` for a known tone. Over `N` symbols, all
data-bearing, the deflection² is `N·γ²/M`; with a fraction `f` of known symbols and the rest ignored it
is `f·N·γ²`. The ratio is `1/(M·f)`: 0.47 (−3.3 dB) for `M` = 8, `f` = 0.266; 0.06 (−12.2 dB) for `M` = 64,
`f` = 0.26. In required per-symbol SNR these are half as many dB. The approximation fails once `γ`
approaches `ln M`, where the largest bin is usually the right one.

### A.5 Arithmetic used in §5

Free-space loss `20·log10(4πd/λ)`: 10.14 MHz (λ = 29.57 m) at 18,600 km 138.0 dB and at 19,500 km
138.4 dB; 14.097 MHz (λ = 21.27 m) at 5,294 km 129.9 dB and at 2,864 km 124.6 dB. Received power =
`C/N0 + N0`. System loss = transmitter power in dBm − received power. Example: 100 mW (20 dBm), `C/N0`
8–12 dB-Hz, `N0` −143.7 dBm/Hz gives −135.7 to −131.7 dBm and 151.7–155.7 dB. Message time at −25 dB-Hz
for 22 dB of `E/N0`: `10^(4.7)` = 50,100 s.

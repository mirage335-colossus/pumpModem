# VLF antenna bandwidth, keying and voltage limits for a 61 m antenna at 10 kW: literature review

27 November 2026 · Delegated literature review for the sub-9 kHz FSK rate and spacing note · web research, sources marked by access level

Original title: *Modulation bandwidth of a 61 m / 10 kW sub-9 kHz antenna and slow FSK through it: literature check*. The question was what limits the modulation bandwidth of a 61 m top-loaded vertical below 9 kHz fed with 10 kW, and how slow FSK passes through it. The search delegated three sub-searches; their reports are appendices A to C, as returned to it.

**Status.** This is the report of a delegated literature search, as it was returned to the
coordinating session on 27 November 2026. It was written into the repository on 28 November at the
user's request; a first attempt to store it was declined by the permission system, so the
[note it served](../sub9khz-fsk-rate-and-spacing.md) was written first and carries the synthesis.
Research findings, not a qualified design; nothing here is a Matrix Modem measurement.

**What was changed on storing.** This header was added and the report's own title line moved
into it. Paths to temporary files were replaced by a description, and paths to the scratch
programs that were kept now point at their copies. The three appendices had their headings moved down one level. Nothing else was edited: "you" and
"your" address the coordinating session, and "your derivation", "your list", "the design context"
and the lettered conditions refer to the brief, which is reproduced at the end.

**How far it was checked.** All 92 distinct DOIs in this file resolve at Crossref (checked on 28 November 2026). For 88 the record's authors are named beside the citation; the other four (three conference papers by Yao, Xu and Wang and one by Gołkowski) were compared by eye and match, the record only spelling the names differently. The check shows that each DOI exists and belongs to the cited authors, not that the source says what is quoted from it. The search states its own access level for every source; numbers from sources it could not open are marked there. In the appendices only the numbers the first-level report marks [D✓] were re-checked by it; the rest stand as the second-level searches returned them.

**Where the note differs.** The note is later and was corrected after an independent check of its
numbers. Where a figure in this report was superseded there (hypothesis counts with the
1/(8 × span) timing rule, the measured decode rates, the block Monte Carlo), the note stands.

---

## 0. Bottom line and limits of this pass

- **Your derivation is the standard result.** B = P/(2π·C·V²) is exactly the quotient of the two classical Navy/Watt formulas (voltage-limited radiated power and the bandwidth–efficiency product). I confirmed both formulas on the page images of NAVELEX 0101,113 (1972). I did not find your closed form printed in any source I could open; Watt's book itself was not accessible.
- **At 0.1–10 tones/s the keying rate is not the constraint; the total FSK span and tuning-drift phase are.** Every published keying rule passes 10 tones/s through any bandwidth above roughly 10–20 Hz.
- **Search limits.** The shared web-search budget (200 calls) ran out. Aerostat/kite/TACAMO antennas, Tavolara, Noviken, Rosnay, Criggion, SLFCS as built, Decca/NDB and transportable Cold-War systems were not researched; Rhauderfehn is Wikipedia-only. DTIC blocks the fetch tool, so DTIC reports were read from Internet Archive OCR mirrors, where equations are often garbled.
- **Access codes used below.**
  - **[R]** I read the pages or text directly.
  - **[R-ocr]** I read OCR text through the fetch tool's summariser.
  - **[D]** read by a delegated research agent; **[D✓]** means I re-checked the quoted numbers against the saved text.
  - **[A]** abstract or index record only.
  - **[N]** not accessed.
- Numbers are flagged M (measured), C (calculated or nominal in the source), or "my arithmetic".

## 1. Classical bandwidth theory and the check of your derivation

### 1.1 The two formulas and their quotient

- **Voltage-limited radiated power**, NAVELEX 0101,113 p. 3-32, "Maximum Radiated Power and Bandwidth Efficiency Product" [R]:
  P_Rss = (640π⁴/c²)·V_TM²·h_e²·C_s²·f⁴ (V_TM = top-hat voltage, rms; C_s = static capacitance).
- **Bandwidth–efficiency product**, same handbook p. 3-36 [R]:
  b_as·η_as = (320π³/c²)·f⁴·h_e²·C_s. The handbook calls it the "inherent or intrinsic bandwidth-efficiency product".
- **Quotient (my arithmetic):** P_in = P_Rss/η = 2π·C_s·V²·b. This is your formula. It is also Q = ω·W/P with the stored energy pinned at W = C·V_rms² by the voltage limit.
- **Two regimes**, NAVELEX p. 3-34 [R]:
  - Voltage-limited: P_Rss = η·(transmitter power needed to reach V_TM).
  - Transmitter-limited: P_R = η·P_TM. So above your B, radiated power falls as 1/B, as you state.
  - The handbook adds that most VLF communications antennas are voltage-limited at the lower VLF frequencies, and plots capability for 15 kHz and 250 kV, which it calls the "present state of the art".

### 1.2 Independent confirmations

- **A. N. Smith 1981** [R-ocr]: B = f/Q with Q = X₀/R; P_r ∝ h_e²V²C₀²f⁴; "100%-efficiency bandwidth" B₁₀₀ = B_as·η_as, fixed by geometry. Omega antennas are transmitter-limited above the bottom of their band; VLF communications antennas are voltage-limited below mid-band.
- **DECO study appended to NBS Report 9725** (Watt's firm) [D✓]: b_as(3 dB) with constant 1.11×10⁻¹³ in MKS units, matching 320π³/c² = 1.102×10⁻¹³. It uses "inherent bandwidth" for the 100%-efficiency value (12 Hz inherent against 40 Hz system at 20 kHz for a 1220 ft tower).
- **HNCD 1963 Northwest Cape design report** [R-ocr]: the requirement is stated as bandwidth "at 100% antenna efficiency" (37.5 c/s at 15.5 kc); bandwidth ∝ h_e²·C.
- **Wheeler 1958** [A]: the same physics as "radiation power factor" (1/Q_rad) proportional to effective volume; effective height sets the current and effective area sets the voltage.
- **Watt 1967** [N]: the publisher's contents list shows sections "Calculation of Antenna Bandwidth" and "Power Bandwidth Product". NAVELEX cites Watt for its keying table.

### 1.3 Names in use

- "Bandwidth–efficiency product" (NAVELEX; Watt).
- "Intrinsic", "inherent" or "100%-efficiency" bandwidth B₁₀₀ (Smith; DECO; HNCD).
- "Voltage-limited radiating capability" P_Rss (NAVELEX).
- "Radiation power factor" (Wheeler).
- Modern equivalents: radiation Q / Chu limit and "efficiency–bandwidth product".

### 1.4 Published refinements

1. **Use static-capacitance reactance, not feed reactance.** B = f·R/X₀ with X₀ = 1/(ωC_s). The feed reactance is X₀(1 − f²/f₀²) (Smith [R-ocr]). Cutler's 6-panel array measured 137.5 Hz against 180 Hz predicted from feed reactance over resistance [D, M]. For a 61 m antenna at 8 kHz, f ≪ f₀, so no correction applies.
2. **Matched-source broadening.** Belrose, AGARD CP-529 [D✓]: a conjugately matched transmitter doubles the 3 dB bandwidth; a practical figure is BW_op = 1.5 × BW_as. NIST quotes the same 1.5 for WWVB [D]. Inference: a class-D voltage-source output gives none of this unless the coupling network presents a matched source.
3. **Bandwidth resistors are real practice.** Anthorn at 19 kHz [D✓, M]: R_r = 0.08 Ω, 0.18 Ω including bandwidth resistors, X = −j85 Ω, 44% efficiency, 100 Hz bandwidth.
4. **Energy recovery beats resistive loading.** Hartley 1968 [A]: energy fed back to the supply as the pulse decays gives more than three times the broadbanding × efficiency product of "brute-force resistance loading". The worked example is a Loran-type pulse, 3 kW peak radiated, from a 300 ft umbrella.
5. **Synchronous tuning** removes the trade for FSK (section 2).
6. **Multiple tuning** (Alexanderson; Belrose 1991/1993) raises efficiency by lowering ground loss. It leaves B₁₀₀ unchanged, so bandwidth narrows.

### 1.5 Typical bandwidths of real stations

| Station | f (kHz) | Bandwidth | Efficiency | I, V | Access, basis |
|---|---|---|---|---|---|
| Cutler, 6-panel / 4-panel | 24.0 | 137.5 / 100 Hz | 75% / 64% | 1850 A, 65.5 kV base / 1987 A, 99.7 kV base | [D] M |
| Cutler c. 1963, dual / single | 14 | 51 / 39 Hz | 50% / 33% | 3500 / 2500 A | [D] C |
| Northwest Cape | 19.8 | not given | 82% (table internally inconsistent) | 2600 A; 86.6 kV base, 126.9 kV top at 1.63 MW | [R-ocr] C |
| Northwest Cape | 10 | 50 Hz, voltage-limited | 4% | 70 kW radiated | [R-ocr] |
| Jim Creek | 15.3 | 55 Hz | 21% | 2100 A | [D] C |
| Rugby GBR | 16 | about 45 Hz | 21% | 720 A | [D] C |
| Annapolis | 18 | about 18 Hz | 17% | – | [D] C |
| Lualualei (old) | 15 | 12 Hz | 22% | 988 A | [D] C |
| Anthorn | 19 | 100 Hz with resistors | 44% | – | [D✓] M |
| Omega Norway | 10.2 | about 30 Hz (derived) | 5.9% | 351 A, 141 kV for 150 kW in, 8.85 kW out | [D] |
| Omega Hawaii | 10.2 | about 22 Hz (derived) | about 7% | 437–446 A, about 150 kV | [D] M |
| WWVL Fort Collins (four 122 m masts) | 20 | about 35–40 Hz (derived) | 5.4% | 183 A, about 87 kV (derived) | [D✓] |
| WWVB (same antenna design) | 60 | 263 Hz | 57.5% | 150 A; 17.6 kV base, 28.4 kV top | [D] M |
| Grimeton SAQ | 17.2 | about 26–29 Hz (derived from 12 ms rise) | not found | voltage not found | [D] |

- Design classes from Smith 1981 [R-ocr]: Omega more than 10 Hz at 10 kHz with efficiency above 7% and under 150 kW input; legacy VLF communications more than 50 Hz at 15 kHz; newer at least 200 Hz above 18 kHz; LF more than 200 Hz.
- I checked the formula against Omega Norway (36.96 nF, 205 m, 10.2 kHz): B₁₀₀ = 1.85 Hz, so 31 Hz at 5.9% efficiency. It agrees.
- Siple: FSK pairs and frequency ramps were transmitted through a "narrowband antenna system" [D]; no Q or bandwidth figure was found. Raghuram et al. 1974 and Helliwell & Katsufrakis 1974 were not read.
- HAARP modulated heating has no antenna Q; HF-to-ELF conversion efficiency is 0.0004–0.0032% (4–32 W) [D, A].

## 2. Keying through a high-Q antenna faster or wider than its static bandwidth

### 2.1 Historical record

- **NEL, "High-speed keying of VLF radio circuits", BuShips *Electron*, pp. 20–21** [R, page images; the archive filename implies January 1951].
  - On-off keying at 15 kc on one shore station is limited to about 40 ms mark or space, about 18 wpm (C).
  - Method: antenna-circuit reactance is varied in synchronism with the frequency shift, at constant power.
  - 100 W experiment: 60 wpm teletype (M).
  - 10 kW transmitter at 28.5 kc with 100 c/s shift at Chollas Heights: 60 wpm printing telegraph received about 500 miles away; maximum 460 wpm (M).
  - The oscillogram caption says the mark-to-space transition occurs within one RF cycle.
  - Modulator cost about 10% of the transmitter.
- **Wolff 1957** [A]: existing VLF transmitters are limited to about 20 wpm by antenna Q; synchronous reactance variation gives 100 wpm or more; under 10% added cost.
- **Lee (Westinghouse), US 2,824,955, filed 1950** [R-ocr]: a saturable reactor is tapped onto the tuning coil so that it carries only a small part of the antenna current; 100 W, circuit Q 132.
- **Deise (Westinghouse), US 2,853,634** [R-ocr]: control winding driven 25 to 125 A in 2 ms; a purely resistive drive would waste about 320 kW, hence pulse-transformer boosting.
- **Jacob & Brauch 1954** [N; cited via US 4,689,803].
  - The Megapulse patent background describes ferrite reactors "in each of two helix houses": 23-ton oil tanks holding 3.75 t of ferrite, 72 Hz bandwidth around 17.8 kHz with 100 Hz shift, radiated power about doubled, performance held back by the drive circuits.
  - Two helix houses and 17.8 kHz match Cutler, and NAVELEX 1972 says saturable-reactor synchronous tuning "is presently at Cutler" [R]. Whether those numbers come from the 1954 article is uncertain.
- **Galejs 1963**, two papers [A].
  - As described by Santos (UCLA dissertation 2021) [D✓]: the AEÜ paper showed that switching must be synchronised with the FSK waveform, at the moments that retain the stored energy, to keep efficiency. The IEEE note showed that proper switching instants prevent decay transients in keyed operation.
  - I could not read either paper.
- **Navy operating practice.**
  - NAVELEX pp. 3-3/3-5 [D]: mark and space 50 Hz apart; a transition is allowed only when both have coincident same-sense zero crossings (every 20 ms). The stated reason is that antenna Q above 200 will not tolerate abrupt change without arcing.
  - Gebhard, NRL Report 8300 [D; I did not relocate this passage in the garbled OCR]: abrupt shifts caused horn-gap flashover, overload trips and burnt helix cable; the 1963 keyer used a linear frequency ramp.
  - Same report [D✓]: NRL's 1951 answer was a four-level FSK teleprinter code at half the keying rate, so the signal fitted the antenna bandwidth.
- **Hartley 1971** [A, title only]: "Electronic Broad Banding of VLF/LF Antennas for FSK Radio Communication".
- **Megapulse, US 4,689,803 (1987)** [R-ocr].
  - Saturable magnetic switches of tape-wound Permalloy: each holds off 10 kV rms, carries 100 A rms, rated 1.0 MVA over 15–30 kHz.
  - The transition is deliberately gradual, about 1 ms (about 20 RF cycles), to keep the spectrum narrow.
  - 200-baud tests are shown as waveforms only.
  - It states that 1600-baud MSK at 37.2 kHz with a 100 kW transmitter "has been achieved" with antenna bandwidth under a quarter of the shift.
  - No efficiency or loss figures are given.
- **US 5,034,697 (Navy)** [D]: Q "usually greater than 250"; 21.40 kHz ± 50 Hz; detuning loss up to 4 dB.
- **Omega is the closest operational precedent** for slow, wide hops near 10 kHz at 10 kW radiated [D].
  - Frequencies 10.2 to 13.6 kHz in 0.9–1.2 s segments with 0.2 s gaps.
  - Each frequency had its own helix tap, variometer and high-voltage antenna relay, with a per-frequency servo active only in its own segment.
  - Current rise times to 63% were 6–23 ms.
  - Mismatched servo gearing made the tuning "hunt back and forth" at La Réunion.
- **WWVL** alternated carriers 100 or 500 Hz apart on 10 s dwells (Fey & Looney 1966 [D]).
  - Morgan 1962 [D] gives the procedure for 20.0/20.5 kHz: reduce power, retune by saturable reactor under phase-detector control, restore power at a zero crossing.

### 2.2 Modern direct antenna modulation

All of these were read by the delegated agent; most IEEE papers are abstract-only.

- **Transient-free condition.** The lineage agrees: switch a capacitor at the zero crossing of its own voltage, or an inductor at zero current. All energy then sits in the other reactance and moves to the new resonance with no dump.
- **Salehi (Virginia Tech dissertation 2013; PIER 143, 2013)** [D, full text of the dissertation].
  - Measured: loop antenna switched between 57.75 and 42 MHz, Q 18.6 / 52.5, FSK at up to 12 MHz switching rate, PIN-diode switch.
  - Simulated: switching an inductor at non-zero current left 0.36 of the current.
  - Problems: switch loss lowers Q (22 down to about 7 with switch ICs); bias-filter time constants.
  - Salehi's later arXiv 1511.07473 was administratively withdrawn (authorship dispute; journal retraction).
- **Santos et al. (UCLA), IEEE TMTT 2020 / dissertation** [D].
  - BFSK on a loaded loop near 75 MHz, unloaded Q about 400 (simulated), GaN switch.
  - Measured efficiency–bandwidth improvement up to 5 times, far below theory.
  - At low rates exact timing is not needed provided switching occurs at the same point each cycle.
  - Power 10–16 W; drain swing 37 V (simulated).
- **Azad & Wang 2014** [D]: 270 kHz inductive link, on-off keying, switching at maximum inductor current; tones up to 18 dB stronger than conventional (M).
- **Schab, Huang & Adams 2019/2020; Huang et al. 2021** [A / R-ocr of the arXiv abstract].
  - Energy-synchronous OOK and PSK at 27 MHz, milliwatt class.
  - Parasitic internal ringing corrupts the stored energy.
  - A DC-assisted scheme gives 10–20 dB lower error-vector magnitude (M).
- **Strachen et al. 2024** [D].
  - Class-E stage driving a lumped high-Q antenna model at 3.045 MHz: 64 W, 78% efficiency (M).
  - About 10 kV p-p on the capacitor (estimated), with only 300 V on the switch.
  - BFSK with 30 kHz spacing.
  - Zero-voltage switching holds over only about 3.02–3.31 MHz; abrupt phase changes give drain voltage spikes.
- **Georgia Tech pulse-trapping antenna.**
  - Slevin, Cohen & Gołkowski, URSI GASS 2021 [R]: 3 m wire at 100 kHz, about 5 dB more received signal with the switch timed against switch closed (M, at 5 m); the f² against f⁴ scaling is simulated.
  - Whitmore et al. 2025 [A]: 275 m ground dipole with a high-voltage switch; no ratings in the abstract.
- **Kemp et al. 2019** [R-ocr of the PMC text].
  - Lithium niobate rod at 35.5 kHz, Q about 3×10⁵ (ring-down, M), passive bandwidth 84 mHz.
  - A relay-switched 60 pF plate capacitance shifts the resonance by 7 Hz. The headline "83×" is 7 Hz / 84 mHz.
  - FSK rate 0.05 Hz in the main data set; modulation shown up to 50 Hz.
  - Radiated power was not measured; efficiency is estimated at 10⁻⁷ to 10⁻⁸.
- **Hassanien et al. 2020** [D]: PZT disc, loaded Q about 850, BFSK at 33.218/33.248 kHz with both tones inside the 3 dB bandwidth and no retuning, up to 60 b/s (M). This is the nearest modern analogue to plain FSK inside the passband.

**What transfers to your case.**
- No modern paper that was opened demonstrates synchronous switching of an antenna reactance at tens of kV or kW.
- The only documented high-power implementations are magnetic: saturable reactors at 10 kV rms or less and about 1 MVA per module, placed on a tap or coupling network and not at the antenna node. Omega used mechanical relays between segments.
- My arithmetic: at 8.27 kHz a 1 µs timing error is 3°. A 100 Hz shift needs ΔC/C of about 2.4%, so a worst-phase switch dumps at most about 2.4% of the stored energy (about 0.4 J of 18.5 J at 100 kV and 1.85 nF). The swung reactive power is about 23 kVA of roughly 960 kVA.

## 3. Plain FSK through a fixed-tuned series-resonant antenna

### 3.1 Response and transient

- Static response (standard): H(δ) = 1/(1 + j·2δ/B); amplitude 1/√(1+(2δ/B)²); phase −atan(2δ/B); group delay at resonance τ = 1/(πB).
- Measured check [D]: WWVB bandwidth "slightly over 600 Hz", group delay about 500 µs; 1/(π·600) = 530 µs.
- After a phase-continuous step, the new forced response is joined by a free oscillation at the antenna's own resonance, of initial size H(f₁) − H(f₂), decaying as e^(−t/τ).
  - Residual is 4.3% of that vector at t = 1/B and 1% at 1.47/B (my arithmetic).
  - McCoy 1954 and Gumowski 1954 [A] treat this. Carson & Fry 1937 [D, partly read] give the quasi-stationary expansion suited to slow sweeps.

### 3.2 Published keying rules

- **Watt's rule as reproduced in NAVELEX pp. 1-9/1-10** [D]: b₃dB = 1.16·k·R_w·(m + τ_b). With R_w = 71.42 wpm, k = 0.25, m = 1 and τ_b = 1.45, 50-baud FSK needs about 50 Hz. The delegated reader notes that the 25 Hz dotting frequency would give about 71 Hz.
- **Vallese 1972** [D]: for a single-tuned antenna, element length × 6 dB bandwidth ≈ 3, so baud ≈ 0.58 × B₃dB for interrupted CW.
- **4-channel MSK** (200 baud, 100 Hz shift) needs 200 Hz. Per Belrose, operational bandwidth at every surveyed major station except Northwest Cape falls short of this [D✓].
- **STANAG 5030**, as quoted by Harrington, AGARD CP-529 [D✓]: phase deviation from linearity no more than 12°, for no more than 0.5 dB Eb/N0 loss. The STANAG itself is restricted and was not read.
- **Cory & Holzheimer**, AGARD CP-529 [D✓]: a 20–25 Hz loaded bandwidth was chosen to support 10 chips/s MSK.
- **Edge-tone penalty** for a span S centred on resonance with constant drive voltage (my arithmetic).

  | S / B | Edge loss | Edge phase |
  |---|---|---|
  | 1/4 | 0.26 dB | 14° |
  | 1/2 | 0.97 dB | 27° |
  | 1 | 3 dB | 45° |
  | 2 | 7 dB | 63° |

### 3.3 Transmitter tolerance of the detuned load

- Load at offset: Z = R(1 + jx) with x = 2δ/B. VSWR is 2.6 at x = 1 and 5.0 at x = 1.8 (my arithmetic).
- Nautel VLF120 data sheet [D]: class D; rated power up to 5:1 VSWR; "graceful power reduction" beyond; efficiency reduced at high VSWR.
- Rockwell US 5,404,114 [D]: solid-state VLF transmitters on high-Q antennas are damaged if drive is removed or the frequency is changed drastically, because stored energy returns into the output devices.
- GTE US 5,345,200 [D]: a 24 Ω source into 0.1 − j50 Ω at 15 kHz gives large current excursions that modern solid-state transmitters "can handle"; resistive loading is "not a practical solution".
- Trips [D]: WWVB drops drive on excess reflected power in gusts; DCF77 interrupts in storms and freezing rain.
- No published phase-angle or zero-voltage-switching loss limits were found for Continental, Telefunken, Thales or amateur class-D/E designs.

## 4. Voltage limits

### 4.1 Hansen & Watt 2003 (SSC San Diego TR 1904) and related data

Read as OCR text; equation typography was garbled, so check the PDF before quoting constants.

- **Smooth conductors:** E_c ≈ 17.25 + 12/√d kV/cm rms (d in cm). The 17.25 is 0.707 × 24.4 kV/cm.
- **Peek's constants** (30–31 kV peak/cm) overestimate for large wires.
- **Frequency.**
  - Onset is practically independent of frequency to tens of kHz (Kolechitskii).
  - Large smooth wires at 29–47 kHz sit about 7–8% below 60 Hz.
  - Smith 1963: −5% at 15–20 kHz, −9% at 25–30 kHz, −14% at 45–60 kHz.
  - A "critical frequency" separates regimes: about 10 kHz for a 1 cm wire, about 30 kHz for 0.39 cm.
  - Inference: thin wire at 8 kHz is in the power-frequency-like regime.
- **Humidity** lowers onset at VLF/LF, about −0.2% per %RH above the critical frequency. This is the opposite of 60 Hz.
- **Stranding factor:** about 0.5 for large cables, approaching 1 for small.
- **Wet (spray) factor:**
  - about 0.5 for 1-inch smooth pipe;
  - much nearer 1 for stranded cable;
  - above 1 below about 0.2 cm diameter;
  - independent of rain rate.
- **Measured examples (Forestport, M):** 2.34 cm smooth at 27.5 kHz, 16.07 kV/cm dry and 11.17 wet; 3.23 cm, 13.81 dry and 10.33 wet.
- **Other:** pipe ends see about twice the infinite-wire gradient; a 25% safety factor is suggested for outdoor spray-wet design.

### 4.2 Stranded-cable fits

Hansen & Rodriguez 2011, Fig. 7 at 40 kHz [R, figure re-rendered at 300 dpi]; E in kV/cm rms, d in cm; data span about 0.23–3.2 cm:
- E_smooth = 17.25 + 12/d^0.43
- E_stranded,dry = 8 + 13/d^0.5
- E_stranded,wet = 3 + 13/d^0.6

Wet reduction from these fits (my arithmetic):

| Diameter | Wet reduction |
|---|---|
| 2.3 mm | about 0–6% |
| 5 mm | about 14% |
| 10 mm | about 24% |
| 25–32 mm | about 36–38% |
| 25 mm smooth pipe | about 50% |

- The same paper says corona loss is proportional to frequency, so operating in corona is not practical at VLF.
- Ideal isolated-wire onset from these fits, V = E·a·ln(2h/a) at 61 m height (my arithmetic, no end effects):
  - 2 mm wire: about 43 kV.
  - 5 mm: about 71 kV dry, 61 kV wet.
  - 10 mm: about 106 kV dry, 81 kV wet.

### 4.3 Design practice

- **Cutler** [R, TR 1681]: design gradient under 0.8 kV/mm; measured extinction on 23–32 mm cables 1.03–1.3 kV/mm dry and 0.88–1.08 kV/mm wet at 28–41 kHz (M); wet design 0.8 kV/mm for 1.5-inch and 0.95 kV/mm for 1-inch cable.
- **Smith 1981** [R-ocr]: historical Navy limit about 200–250 kV; worst-case allowable gradient about 0.4 kV/mm; wet derating 0.65–0.8; LF stations 50–100 kV.
- **Insulators** [R-ocr]:
  - at VLF/LF corona onset is close to flashover;
  - wet withstand is 10–20% below wet flashover;
  - water films heat above about 2.5 kV/inch;
  - insulators accepted on 60 Hz criteria failed in service in the early 1970s.

### 4.4 Small stations and the nearest match to "60 m, 10 kW, below 20 kHz"

No exact match was found.

- **WWVL** [D✓, NBS Report 9725]: 33.4 kW in, 183 A, 1.8 kW radiated. The stated limit is bushing flashover at "approximately 100 kv", not transmitter power.
- **DECO scaling table, same report** [D✓, C]: a 206 ft (63 m) top-loaded tower has h_e = 40 m and C = 2.5 nF. This is the only documented figure found at your scale. For 100 kW at 60 kHz the study preferred taller towers to voltages approaching 200 kV.
- **Auburn/NASA 1965** [R for the abstract pages; rest D]: a 300 ft radiator with a 12-rib umbrella, 5080 pF from scale models, for 100–200 mW radiated. Tuning-inductor loss dominates.
- **Amateur sub-9 kHz stations** [D, from the sub9khz compilation and station reports]:
  - DK7FC kite: about 1 A and about 16 kV, 27–58 mW ERP, current held to 2 A "to prevent a HV breakdown on the coil".
  - DK7FC fixed inverted-L (473 pF): about 28 kV at 8.27 kHz (derived); trips in rain.
  - DF6NM: corona "sizzles around the coil" at 11 kV; 25 kV withstand limit.
  - DK1IS: 660 pF, 560 mH, 160 Ω, Q about 46, about 180 Hz bandwidth, about 17.5 kV.
  - G3XIZ and VO1NA: coil arcing.
  - ON7YD on 136 kHz: corona reports all involved more than 30 kV.
- **Pattern.** In small stations the first limit is the coil top or bushing (11–28 kV amateur, about 100 kV at WWVL), not wire corona.
- SAQ antenna voltage was not found.

## 5. Time variation of tuning and resulting phase

- **Governing relation.** Morgan, NBS Report 7286 (1962) [D, page image checked by the delegated agent]: δt/T ≈ 0.159·Q·ΔC/C, i.e. Δφ ≈ Q·ΔC/C = 2Q·Δf/f. Causes listed: wind, snow, ice.
- **Measured.**
  - WWVL/WWVB (Andrews 1965 [D]): the servo holds phase to about 0.1 µs, but shows "a few microseconds" with a period of "several seconds" in heavy wind. That is about 14–22° at 20 kHz.
  - WWVL under remote phase lock (Fey, Milton & Morgan 1962 [D]): 0.05 µs or better.
  - DCF77 (Piester et al. 2012 [D]): ±0.1 µs slow variation.
  - Omega Hawaii [D]: loss resistance rises at rain onset and with salt on insulators; the "wind" capacitance step used for servo tests was 400–1000 pF (about 1–2%).
  - Cutler [R]: ice lowers the panels, capacitance rises, and the tuning range can be exhausted.
  - Graf 1965 [D]: the antenna is "the major source of phase error" (coil heating, wind loading); no magnitudes.
  - Amateur [D]: IW4DXW current 625 mA dry against 570 mA under cloud; DK1IS at Q 46 needs no retuning for sway.
- **Automatic tuning practice** [D].
  - A V–I phase detector drives a motorised variometer (WWVB, Omega).
  - "Sudden gusts can move the antenna too rapidly for the autotuning system to respond" (NIST SP 250-67).
  - Omega used per-frequency servos gated by the keying waveform, and separately fed antenna-current phase back to the exciter. Its specification was under 1° radiated phase error averaged over a minute.
  - No loop bandwidth, slew or deadband figures were found.
- **Coherent receiver budget.** DL4YHF's EbNaut notes [D] allow about 15° over the coherent span.
- **My arithmetic.**
  - WWVL's gust phase with Q about 540 implies ΔC/C of roughly 450–700 ppm.
  - The same sway on your antenna gives about 14° at Q = 500, about 3° at Q = 96, about 0.4° at Q = 13.
  - One degree corresponds to 35 ppm of capacitance at Q = 500.

## 6. Recent literature (2018–2026) that adds nuance

- **No camp claims the Chu bound on a passive linear time-invariant antenna is false.** The claim is that information bandwidth can be decoupled from impedance bandwidth once time-invariance or passivity is dropped (Manteghi 2019 [A]; Shlivinski & Hadad 2018 [A]; Li, Mekawy & Alù 2019 [A]; Mekawy et al. 2021 [A]; Mostafa et al. 2022 [R-ocr], measured only 2–3 dB sideband gain at 880 MHz).
- **Critiques** [D, full text unless noted].
  - Schab 2020 (IEEE AWPL): practical "quasi-resonant" DAM gains only over a limited size range and "may have limited applicability at extremely low frequencies".
  - Schab, Singh & Bohannon 2020 [A]: non-LTI transmitters have no defined Q or bandwidth; judge them by time-domain distortion.
  - Hayran & Monticone 2023: switched matching suits short pulses, pump energy must be counted, parametric schemes can go unstable.
  - Latypov 2026 [A]: mechanical emitters remain subject to Chu–Harrington and already sit near the limit.
  - Yaghjian 2018 [A]: passive lossy dispersive tuning can double the bandwidth of 50%-efficient antennas.
- **Mechanical, piezoelectric and magnetoelectric transmitters** work at 0.25–300 b/s but at fT–nT field levels [D, mostly A]:
  - Thanalakshme 2022: 166 Hz, 5–10 b/s on-off keying.
  - Zhang 2023: BFSK at 5–10 b/s to 60–110 m.
  - Glickstein 2020: continuous-frequency FSK, 0.25 b/s.
  - Chu 2023: 21.2 kHz, Q about 40, 300 b/s.
  - None is a route to a 10 kW radiator.
- **Non-Foster transmit matching** is watt-class (Sussman-Fort & Rudish 2009 [A]: 1 W near 20 MHz).

## (i) Findings that bear most on tone rate and total span

All bandwidths below are my arithmetic from your figures.

1. **Radiated power is fixed by V, C and h_e.** I·R_r gives about 0.16–0.28 W at 100 kV, 1.85 nF and h_e of 38–50 m. Extra transmitter power buys only bandwidth: 86 Hz at 100 kV, 256 Hz at 58 kV (both 1.85 nF).
2. **Natural bandwidth is probably 12–30 Hz** for a well-built system (Q 300–700, as at Omega and WWVL) and 40–180 Hz for amateur-grade loss. At Q = 500 the antenna reaches 100 kV on about 1.9 kW; the remaining 8 kW is usable only through a bandwidth resistor, wider drive or synchronous tuning.
3. **Keying rate is not binding at 10 tones/s or less.** τ = 1/(πB) is 19 ms at 16.5 Hz and 3.7 ms at 86 Hz. Only 10 tones/s into roughly 6 Hz is marginal.
4. **Span is binding.** A span equal to B costs 3 dB and 45° at the edge tones with constant drive voltage; half of B costs 1 dB.
5. **Three documented ways to widen the span**, none of which costs radiated power in the voltage-limited regime:
   - a bandwidth resistor (Anthorn);
   - driving the edge tones harder into the reactive load, within the transmitter's VSWR rating (about 5:1, i.e. S up to about 1.8·B, by inference from the Nautel sheet);
   - retuning per tone. Omega-style relay or tap switching suits 1 tone/s or slower with 0.2 s gaps. Saturable-reactor tuning is proven at 10 kV and 1 MVA.
6. **Key phase-continuously and never jump or remove the drive.** This is the Navy rule and the class-D patent warning.
7. **Lower Q also buys phase stability.** A bandwidth resistor reduces wind-induced phase in the same proportion (item 5 arithmetic). Measuring antenna-current phase and correcting the exciter, as at WWVL and Omega, is the proven fix; variometer servos are slower than gusts.

## (ii) Where the literature disagrees with or refines your list

- **Derivation: confirmed**, with the refinements in section 1.4. The matched-source factor of 1.5–2 does not apply to a stiff class-D output.
- **Capacitance and effective height: consistent.** DECO gives 2.5 nF and 40 m for a heavily top-loaded 63 m tower; your 0.9–1.85 nF is plausible and slightly conservative.
- **Radiation Q.** At 8.27 kHz your stated ranges of C and h_e give 3.5–12 million by the standard formula. The 53 million figure needs a lower frequency (about 5 kHz) or a smaller h_e.
- **Radiation resistance:** 1.7–3.0 mΩ for h_e of 38–50 m; your "2–3 mΩ" is fine.
- **Loaded Q of 1,400** needs a helix Q above about 1,500 and only a few ohms of ground loss. Station helices reach about 2,000, and Litz up to 3,500 [R-ocr]. Amateur systems at this frequency show 160–320 Ω total.
- **B at Q ≈ 4** (the 2.1 kHz case): the narrowband model no longer holds. Inference: the antenna would also stop filtering class-D harmonics.
- **Wet derating of 28–46%** fits large or smooth conductors (25 mm stranded about 36%, smooth pipe about 50%). The Hansen data show little wet penalty for thin stranded wire (0–15% at 2–5 mm). Corona rings are smooth, so they take the larger penalty. Humidity alone lowers onset at VLF.
- **29 kV for thin wire** is conservative against about 43 kV ideal for 2 mm wire, which is appropriate given end effects of about 2×.
- **100 kV with rings** is credible for the wire, but real small stations were limited first by the bushing or coil (WWVL about 100 kV; amateurs 11–28 kV).

## (iii) Not accessed, and DOI status

**Not accessed (full text).**
- Watt, *VLF Radio Engineering* (1967); Wheeler 1958 (both papers); Wolff 1957; Galejs 1963 (both); Hartley 1968 and 1971; Jacob & Brauch 1954.
- RADC TR-74-161 (CARBE); Watt & Farstad 1963, "Increasing VLF antenna bandwidth" (DECO 34-A-1); Watt 1962, "Corona characteristics as related to VLF antennas" (DECO 34-R-4); Smith 1963 corona report.
- Belrose 1991; STANAG 5030; Jordan & Balmain; the 1967 NPS Northwest Cape thesis.
- Raghuram et al. 1974; Helliwell & Katsufrakis 1974; Creighton & Wase 1969 (Rugby); Hansen & Adams 2011.
- Hansen & Watt TR 1904 chapters 7–8 and its design-curve figures.
- Most IEEE DAM papers (abstract only); Whitmore 2025; Nicholson's abelian.org pages; DK7FC's own pages.
- Everything listed as unresearched in section 0.

**DOIs I verified directly at Crossref.**
- 10.1109/TCOM.1957.1097513 (Wolff)
- 10.1109/TCOM.1963.1088740 (Galejs, IEEE)
- 10.1109/TAP.1958.1144566 and 10.1109/TAP.1958.1144550 (Wheeler)
- 10.1016/B978-0-08-012313-4.50006-2 (Watt, chapter "Transmitting Antenna", pp. 11–168)
- 10.1109/TCOM.1968.1089913 and 10.1109/TCOM.1971.1090683 (Hartley)
- 10.1038/s41467-019-09680-2 (Kemp)
- 10.1109/APS.2011.5996389 (Hansen & Rodriguez)

**DOIs verified by the delegated agents at Crossref (not re-checked by me).**
- Schab 2020: 10.1109/LAWP.2020.3005412
- Schab, Huang & Adams: 10.1109/ACCESS.2019.2902365; 10.1109/OJAP.2020.2972842
- Schab, Singh & Bohannon: 10.1109/TAP.2020.2980355
- Santos et al.: 10.1109/TMTT.2020.3016381
- Azad & Wang: 10.1109/TCSI.2013.2283995
- Salehi et al.: 10.2528/PIER13102204
- Manteghi 2019: 10.1109/MAP.2019.2907892
- Strachen et al. 2024: 10.1063/5.0213308
- Slevin et al. 2022: 10.1109/TAP.2021.3096950
- Whitmore et al. 2025: 10.1109/TAP.2025.3540651
- Hassanien et al.: 10.1038/s41598-020-73973-6
- Thanalakshme et al.: 10.1109/TAP.2021.3137244
- Zhang et al. 2023: 10.1109/TAP.2023.3240068
- Glickstein et al.: 10.1109/ACCESS.2019.2961708
- Chu et al.: 10.34133/research.0208
- Hayran & Monticone: 10.1109/MAP.2023.3236275
- Shlivinski & Hadad: 10.1103/PhysRevLett.121.204301
- Li, Mekawy & Alù: 10.1103/PhysRevLett.123.164102
- Mekawy et al.: 10.1103/PhysRevApplied.15.054063
- Sussman-Fort & Rudish: 10.1109/TAP.2009.2024494
- Yaghjian 2018: 10.1049/iet-map.2017.0648
- Latypov 2026: 10.1063/5.0335626
- Fey, Milton & Morgan: 10.1038/1931063c0
- Andrews 1965: 10.1109/TIM.1965.4313482
- Fey & Looney: 10.1109/TIM.1966.4313539
- Carson & Fry: 10.1002/j.1538-7305.1937.tb00766.x
- McCoy / Gumowski 1954: 10.1109/JRPROC.1954.274498 and 10.1109/JRPROC.1954.274519 (the agent did not say which is which)
- Raghuram et al.: 10.1109/TAP.1974.1140777
- Helliwell & Katsufrakis: 10.1029/JA079i016p02511

**Not Crossref-verified.**
- Huang et al. 2021: 10.1109/TMTT.2021.3103209 (taken from the arXiv 2103.11906 landing page only).
- Piester et al. 2012 (DCF77): no DOI obtained.

**No DOI exists or was found.**
- Galejs, "Electronic broadbanding of high-Q tuned circuits or antennas", Arch. Elektr. Übertragung 17:375–380 (1963).
- Jacob & Brauch, Electronics 27(12):148–151 (1954).
- NAVELEX 0101,113 (Aug 1972).
- Hansen & Watt, SSC San Diego TR 1904 (2003, ADA526052).
- Hansen, NRaD TR 1681 (1994).
- Hansen & Chavez, TR 1617 (1993, ADA272018).
- Smith, CEL CR 81.020 (1981, ADA102473).
- Seeley & Wiborg, NOLC 721 (1967, AD0653851).
- HNCD 1963 (AD0408682).
- AGARD CP-529 (1993, ADA267991).
- NBS Reports 7286 and 9725.
- Omega reports (AD0630900, ADA056685/702, ADA082114, ADA082683, ADA137673).
- Slevin et al., URSI GASS 2021.
- The patents (US 2,824,955; 2,853,634; 4,689,803; 5,034,697; 5,404,114; 5,345,200).

**Housekeeping.** Nothing was written to the repository. The delegated agents left temporary text mirrors in the session scratchpad at the session scratchpad (temporary, not kept); I added one figure crop there, at the session scratchpad (temporary, not kept) More searching needs a follow-up turn, because the search budget for this one is spent.

---

## Appendix A (second-level search): Documented VLF/LF stations: bandwidth, Q, efficiency, current and voltage

**Scope caveat.** The shared web-search budget ran out part-way through, so the aerostat/TVLF, Decca/NDB, Criggion, Tavolara, Noviken, Rosnay and SLFCS-as-built items were not researched. Everything marked FT was read as OCR text through WebFetch's extraction model, not from page images; spot-check any number you intend to quote. No repository files were touched.

Access codes: **FT** full text, **AB** abstract only, **SN** search-engine excerpt only (page not opened), **WP** Wikipedia pointer. Basis: **M** measured, **C** calculated, **N** nominal/design, **D** derived by me from the source's numbers.

### 1. Professional stations

| Station / configuration | f (kHz) | Antenna | C | h_e | Bandwidth | Efficiency | I (A) | V (kV) | P_rad | Source, basis |
|---|---|---|---|---|---|---|---|---|---|---|
| Cutler NAA, south array, 6-panel | 24.0 | Trideco, 13 towers | 123.9 nF | 140.1 m | 137.5 Hz (low power, network analyzer) | 74.9% | 1850 | 65.5 base | 679 kW | [1] FT, M |
| Cutler, 4-panel | 24.0 | same | 90.1 nF | 130.4 m | 100 Hz | 64.3% | 1987 | 99.7 base | 679 kW | [1] FT, M |
| Cutler c. 1963, dual / single | 14 | same | – | – | 51 / 39 Hz | 50% / 33% | 3500 / 2500 | – | – | [2] FT, C |
| Cutler 1959 requirement | 14.3–30 | – | – | – | ≥30 Hz | >50% | – | 200 max, 0.65 kV/mm | 1 MW | [3] secondary, N |
| Jim Creek NLK | 15.3 | 10 valley spans, 5,640–8,800 ft | – | – | 55 Hz (0.36%) | 21% | 2100 | 200 "when both halves operate" | 320 kW | [2] FT, C; voltage [4] N |
| Annapolis NSS | 18 | nine 600 ft towers | – | – | ~0.1% (≈18 Hz D) | 17% | garbled | – | garbled | [2] FT, C |
| Lualualei NPM (old) | 15 | seven 600 ft towers | – | – | 12 Hz (0.08%) | 22% | 988 | – | 44 kW | [2] FT, C |
| Lualualei NPM (1971 tests) | 16.6–28.5 | two top-loaded arrays | – | ~192–194 m at 16.6 | – | – | 1055–1227 dual | – | 128–506 kW per array | [5] FT, M, table garbled |
| Rugby GBR | 16 | 820 ft masts | – | – | 0.28% (≈45 Hz) | 21% | 720 | 165 (1938 magazine caption) | 52 kW | [2] FT, C; voltage [6] WP |
| Omega Norway (Bratland), 1975 height | 10.2 | valley span | 36.96 nF apparent | 205 m | ≈30 Hz (D) | 5.9% | 351 | 141 at bushing top | 8.85 kW | [7] FT; R, C, h_e M; I, V C for 150 kW |
| same | 13.6 | | 40.67 nF | 219 m | – | 10% | 314 | 88 | 15.0 kW | [7] |
| Omega Hawaii (Haiku) | 10.2 | valley span | 46.1 nF apparent | 169 m | ≈22 Hz (D) | ≈7.3% (D) | 437–446 for 10 kW; 457 max | ≈150 (D) | 10.5 kW max | [8] FT, M |
| Omega La Réunion | 10.2 | grounded tower | – | 163 m | – | 8–13.8% over band | 454 for 10 kW; 491 max | – | 11.2 kW max | [9] FT, M |
| Omega 1966 specification | 10.2–13.6 | 1200–1400 ft top-loaded tower | ≈0.06 µF | 150–170 m | "small amount" | ≤20% chosen for bandwidth | – | 150 assumed; 120–140 for 10 kW | 10 kW | [10] FT, N |
| WWVL Fort Collins (1969) | 20 | four 122 m masts, diamond top hat | 14.6 nF | 85.5 m | ≈35–40 Hz (D) | 5.4% | 183 | ≈87 (D); bushing flashover ≈100 | 1.8 kW from 33.4 kW | [11] FT; C, h_e from [12] M |
| same antenna, 1994 | 20.6 | | | | – | 6.1% | – | X_b = −477 Ω | – | [12] FT, M |
| WWVB south, same design | 60 | | 14.7 nF | 85.5 m | 263 Hz | 57.5% | 150 | 17.6 base, 28.4 top | 10.35 kW | [12] FT, M |
| DECO proposal, 487L-type tower | 20 | 1220 ft, 16 × 1000 ft top-load cables | 14 nF | 220 m | 12 Hz inherent, 40 Hz system | 30% | 300 | 160 design limit | 30 kW | [11] appendix FT, N/C |
| Forestport test tower | 10.2 | 1205 ft, six top guys as top-load | – | – | – | – | garbled | – | 0.5 kW max | [2] FT, C |
| Grimeton SAQ | 17.2 | six 127 m towers, 46 m cross-arms, 8 wires, ~2 km, multiple-tuned | – | – | ≈26–29 Hz (D from 12 ms rise/decay) | – | "60 A" (SN, ambiguous) | not found | ~80 kW of 200 kW run | [13] FT, N |
| Siple (Zeus) | ~5 (5.15) | 21.2 km dipole on 2 km ice ([14] says 23 km) | – | – | "narrowband antenna system" | – | – | – | 100 kW transmitter | [14] FT; [15] AB |
| HAARP modulated heating | 2.125 | none | – | – | no antenna Q | 0.0004–0.0032% HF to ELF | – | – | 4–32 W | [16] AB, M |
| DHO38 Rhauderfehn | 23.4 | eight 352.8 m umbrella masts | – | – | 4 × 50 baud MSK | – | – | insulators rated 300 | up to 800 kW | WP |
| Anthorn GQD | 19.6 | 228 m centre mast + 12 | – | – | – | – | – | "several hundred kV" | 550 kW | WP |

Notes on the table:

- **Cutler bandwidth warning.** X_b/R predicts 180 Hz for the 6-panel case; the measured value is 137.5 Hz. Simple X/R estimates overstate bandwidth when the operating frequency is a large fraction of self-resonance (40.2 kHz here).
- **ITT Table III [2]** is a 1965 compilation computed from a 1963 Cutler trip report, Rusch (Caltech TR 4, 1959), an IIT survey (1961) and DECO 42-F (1962). Its fourth column is efficiency × relative bandwidth, i.e. the 100%-efficiency bandwidth: about 26 Hz Cutler, 11.5 Hz Jim Creek, 9.4 Hz Rugby, 3.1 Hz Annapolis, 2.6 Hz Lualualei (D). The Haiku row is internally inconsistent in the OCR and I did not use it.
- **Omega voltage limits.** Norway insulation was nominally 250 kV on the bushing and 220 kV on the spans. The report says these were never closely approached and "the system is clearly transmitter power limited" (150 kW).
- **Omega retuning between segments.**
  - The 1966 specification calls for a servo-controlled tuning unit with "four tapped variometers, each connected to the transmitter cable output transformer by high-voltage switches", driven by the multiplexing waveforms, with switching "within 0.2 seconds". Its coupler figure shows antenna-circuit Q = 2000.
  - As built, each frequency had its own helix tap, variometer room and antenna relay, with per-frequency servo gear ratios (Hawaii, La Réunion, Norway).
  - La Réunion reports that mismatched gear ratios made the tuning "hunt back and forth" as each frequency keyed, and could prevent retuning within one segment.
  - Segments were 0.9–1.2 s with 0.2 s gaps [10]. I found no stated current rise-time.
- **Siple.** [14] describes FSK pairs, phase reversals and frequency ramps. [15] puts the half-wave resonance near 2.5 kHz in 1983 and notes better efficiency at higher frequency and a later crossed dipole. Jupiter (1979) and the 42 km extension are SN/WP only. I found no antenna Q, bandwidth or efficiency figure.
- **TACAMO.** 200 kW dual trailing wire is WP and uncited there. Wade [17] gives theory for 10,000–25,000 ft wires: efficiency roughly 70–90% near λ/2, 5–15% when very short, and a corona limit of about 10 kV peak at altitude (OCR garbled). Breakall (ACES J. 9(1):18–31, 1994) is AB only, with no numbers.
- **Alpha/RSDN-20.** WP only: 11.905/12.649/14.881 kHz, 0.4 s pulses in a 3.6 s cycle, no antenna data.

### 2. Closest matches to "60 m mast, 10 kW, below 20 kHz"

No documented station matches all three. The nearest are:

1. **WWVL, Fort Collins [11][12].** This is the best analogue: 122 m masts, 20 kHz, 33.4 kW into the antenna, 183 A, 1.8 kW radiated. The stated limit was "the flashover voltage of the antenna bushing… approximately 100 kv", not transmitter power. In 1994 the tuning helix accounted for 0.607 Ω of the 0.89 Ω total at 20.6 kHz, with R_r 0.054 Ω.
2. **DECO top-loaded-tower table [11 appendix], calculated.** Actual height against effective height and capacitance: 206 ft → 40 m, 2.5 nF; 284 ft → 55 m, 3.4 nF; 387 ft → 75 m, 4.7 nF; 516 ft → 100 m, 6.2 nF; 1060 ft → 200 m, 12.3 nF. The first row is the only documented figure I found for a roughly 63 m top-loaded tower. The same study gives minimum tower heights for 100 kW at 60 kHz: 490 ft at 200 kV, 580 ft at 150 kV, 730 ft at 100 kV, 1110 ft at 50 kV. It preferred a taller tower to "operating voltages approaching 200 kv".
3. **Auburn/NASA Marshall design study [18].** A 300 ft radiator with a 12-rib 300 ft umbrella, 5080 pF from scale models, about 12 mH tuning at 20 kHz, for only 100–200 mW radiated. It concludes the tuning-inductor loss dominates efficiency. This is a design study, not a built station; the 1968 follow-up was too large to fetch.
4. **Forestport [2].** 0.5 kW radiated at 10.2 kHz, but from a 1205 ft tower.

My own arithmetic, not from a source: the 63 m DECO row at 9 kHz and 100 kV gives about 0.45 W radiated, using the standard short-monopole formula.

### 3. Amateur and experimental stations

| Station | f | Antenna | C / L | I | V | ERP | Best DX, mode | Source |
|---|---|---|---|---|---|---|---|---|
| DK7FC/P kite, Oct 2010 | 8970 / 6470 / 5170 Hz | 200 m kite vertical (300 m for 5170) | ≈1.1 nF (D) | 600 mA at 12 V; up to 900 mA at 550 W | "1 A… about 16 kV"; ~7 kV rms at 5170 Hz, 400 mA | 27 / 11 / 2 mW | 1310 km (8970), 904 km (5170); DFCW-600 | SQ5BPF pages SN; [19] FT |
| DK7FC/P kite, 4 Dec 2010 | 8970 Hz | kite | – | – | – | 58 mW (record) | 2873 km, carrier | [19] FT |
| DK7FC/P kite, June 2011 | 8970 / 6470 Hz | 300 m kite | – | self-limited to 2 A "to prevent a HV breakdown on the coil" | – | planned ≤250 mW | 2404 km | [20] FT |
| DK7FC fixed, 2011 | 8970 Hz | inverted-L, ~30 m high, ~70 m long | 473 pF; ~550 mH, 320 Ω DC | – | target 50 kV rms needing ~1.2 kW | 200–800 µW | UK | [20] FT |
| DK7FC fixed, 2017–18 | 8270 Hz | same | | 650–700 mA | ≈28 kV (D) | – | EbNaut 30–60 s symbols, 3–8 chars, 10–13 h; 300 chars to 881 km | [20][19] |
| same | 4470 / 3675 Hz | same | 3.9 H at 3675 | 150–300 / 170 mA | coil rated 15–15.5 kV | 3–25 µW | – | [20] |
| same | 2970 Hz | same | ~2.9–3.8 H | 100–110 mA | 12 kV | 1.2 µW (R_r 70 µΩ) | 65 km, EbNaut 80 s | [20] |
| same | 970 Hz | same | Z = 342 kΩ | 15–36 mA | 5–12 kV | 3–18 nW | 40.5 km | [20] |
| DF6NM kite, 2011 | 8970 Hz | 180 m wire, h_e ≈ 50 m | ≈900 pF; 280 Ω total | 0.6 A (~100 W) | ≈11–12 kV | 1.3 mW | 2547 km | [20] |
| DF6NM fixed | 8270 Hz | – | – | 0.3 A | limited by "25 kV voltage withstand" | 10–15 µW | >1000 km, 95% BPSK decode | [20] |
| DK1IS | 8270 Hz | 13 m up, 4 × 33 m top load | 660 pF; 560 mH; 160 Ω | 600 mA | ≈17.5 kV (D); coil tested 20 kV | 30–70 µW | 1071 km | [20] |
| W4DEX | 8971 Hz | 29 m vertical + ~170 m top hat | 1200 pF; 265 mH | – | – | ~150 µW | 6194 km; 12.5 dB in 46 µHz | [21] FT |
| VO1NA | 8270 Hz | 100 m wire at ~12 m | 0.45–0.5 H | ~20 mA (2015) | – | 10 µW | 4369 km, 50-char EbNaut (2021) | [20][22][19] |
| G3XIZ | 8.977 kHz | inverted-L, 40 m out, 10 m up | – | – | – | ~2 µW at 150 W | 218 km | [20] |
| OK2BVG | 8970 Hz | L, 24 m up + 220 m top; or 150 m kite | – | ≤0.38 A, 120 W | – | – | 424 km two-way; DFCW-3000 | [20] |
| OE3GHB | 8970 Hz | 18 m pole + 100 m top | ~600 pF | 400–600 mA | – | – | – | [20] |
| IW4DXW | 8270 Hz | – | – | 520–625 mA | – | 300 nW at 5 W (2020) | 1413 km | [20][19] |
| G3XBM | 8.76–8.98 kHz | earth mode, 20 m electrodes, 5 W | – | – | – | – | 6 km | [20] |
| W2ZM (WH2XBA) | 29.499 kHz | 90 ft vertical, 800 W | – | – | – | ~1 mW EIRP | transatlantic, QRSS-120 | ARRL 2014 FT |

Corona, arcing and drift reports:

- **DF6NM:** "corona sizzles around the coil at about 0.55 A (11 kV)"; an earlier wire had a "voltage limit of about 12 kV due to corona"; coil layer-to-layer limit about 3.7 kV.
- **G3XIZ:** a "necklace of spherical sparks on the top winding of the loading coil", a 10 mm spark drawn from the aerial, and enamel burned away.
- **DJ8WX:** "one of the anchorage ropes didn't stand the sparks" (2013).
- **VO1NA:** "the coil arced and had to be rewound" (2016).
- **DK7FC:** safety circuit trips in strong rain; "risk of a flash over when it starts to rain".
- **IW4DXW:** current 625 mA in dry sun against 570 mA under cloud.
- **DK1IS:** Q 46, about 180 Hz at −3 dB, so retuning for wind sway was "unnecessary"; inductance rises slightly with current.
- **Kite tuning drift:** I found no explicit report of drift with kite motion.

LF (136 kHz) guidance:

- **ON7YD §2.17 [23]:** "The corona effect is rather seldom reported by amateurs on LF… the antenna voltage was in excess of 30kV in all cases."
- **ON7YD worked examples:** 10 m vertical at 100 W gives 0.92 A, 16 kV, 7.5 mW; with a 20 m top load, 1.11 A, 7 kV, 27 mW; 70 pF at 400 W gives 36 kV and 2.2 A. A 300 pF, 60 Ω antenna has 1.5 kHz bandwidth at 2:1 SWR.
- **G3YXM [24]:** "there could be 20kV on the aerial"; tuners "just flash over".

### 4. Not accessed

- **Rugby 1966–69 rebuild:** Creighton & Wase, "The New Very-Low-Frequency Transmitter at Rugby Radio Station", POEEJ 61(4):232, Jan 1969. The PDF exceeds the fetch size limit. Hall, POEEJ July 1965, and "High power VLF radio station for NATO", Radio & Electronic Engineer 29:47 (1965, Anthorn) were also not opened.
- **Siple:** Raghuram, Smith & Bell, IEEE Trans. AP 22(2):334–338, 1974, doi:10.1109/TAP.1974.1140777; Helliwell & Katsufrakis, JGR 79(16):2511–2518, 1974, doi:10.1029/JA079i016p02511. Both DOIs verified in Crossref; neither text read.
- **Watt, VLF Radio Engineering (1967)** station tables; Hansen's 2010 "US Navy VLF/LF Transmitters" talk; NAVELEX 0101,113 chapter 3 (opened, but it covers equipment and has no antenna table).
- **Auburn 1968 follow-up** (NTRS 19680021824): over the size limit.
- **Amateur primary pages:** SQ5BPF's DK7FC pages (certificate errors), abelian.org (DNS failure; read through the i2phd.org mirror), DK7FC's qrz.com page, rsgb_lf_group archive, web.archive.org (blocked).
- **Not searched at all:** aerostat/kite/helicopter VLF (TVLF, TARS, Belrose/CRC, Trueman, NOSC), transportable 487L and Soviet/German mobile systems, Decca/Datatrak/NDB/LF time stations, Sunset Canyon WWVL, Criggion, Tavolara, Noviken, Rosnay, G3LDO, DL3ZID, IK1QFK, and the Decca Puckeridge special-event operation.

### Sources

1. Hansen, Chavez & Olsen, *VLF Cutler: September 1997 Four-Panel Tests; RADHAZ and Field Strength Measurement*, SSC San Diego TR 1761, Jan 1998 (ADA345699).
2. Vallese, Passalaqua & Rakovsky, *Feasibility of Design of Omega Antennas by Ferrite-Loading Techniques*, ITT Federal Laboratories, May 1965 (AD0360988).
3. E. M. Newman, "The Navy's VLF Antenna at Cutler Maine", LIMARC slides, Apr 2020.
4. navy-radio.com Jim Creek page, citing BuShips Journal, Mar 1954.
5. Barr & Raudenbush, NRL Memorandum Report 2475, Aug 1972 (AD0748177).
6. Wikimedia Commons caption citing *Electronics* 11(4), Apr 1938, cover.
7. Smith & Hanselman, NOSC TR 246, vols 1 and 3, 8 May 1978 (ADA056702, ADA056685).
8. Hanselman, NOSC TR 493, 19 Oct 1979 (ADA082114).
9. Hanselman, NOSC TR 484, 10 Jul 1979 (ADA082683).
10. Pierce, Palmer, Watt & Woodward, *Omega: A World-Wide Navigational System*, 2nd rev., Pickard & Burns 886B, 1 May 1966 (AD0630900).
11. J. B. Milton, *WWVB/WWVL Field Studies*, NBS Report 9725, 1 Mar 1969 (tf.nist.gov/general/pdf/458.pdf), with appended DECO Electronics OCD/NBS station study.
12. Hansen & Gish, *WWVB Baseline Measurements: Summary, Findings, and Recommendations*, NRaD TR 1692, Feb 1995 (ADA299078).
13. Alexander association (alexander.n.se), transmitter page and forum post by F. Wiklund.
14. Helliwell & Katsufrakis, *Antarctic Journal of the U.S.*, Sept/Oct 1975, p. 205.
15. Carpenter, Bell & Smith, J. Atmos. Terr. Phys. 50(2):105–115, 1988, doi:10.1016/0021-9169(88)90048-7 (verified).
16. Moore, Inan, Bell & Kennedy, JGR 112(A5), 2007, doi:10.1029/2006JA012063 (verified).
17. H. D. Wade, Univ. New Mexico EE-100, Oct 1963 (AD0423420).
18. E. R. Graf (project leader), Auburn Univ. VLF Transmission Laboratory Technical Report 9, 15 Dec 1965 (NTRS 19660021560).
19. P. Nicholson, "Some recent milestones with amateur radio experiments at VLF", mirror at i2phd.org/abelian.
20. sites.google.com/site/sub9khz news pages 2011–2019 (G3XBM's compilation of reflector posts).
21. ARRL News, "Radio Amateur's Sub-9 kHz VLF Signal Detected Across the Atlantic", 2014.
22. *The Canadian Amateur*, Jan/Feb 2018, p. 23; ARRL News 11 Jan 2021. ARRL's "10 mW" is corrected to 10 µW in [19].
23. R. Strobbe ON7YD, "Antennas for 136 kHz", strobbe.eu/on7yd/136ant.
24. G3YXM, "Getting on 136", wireless.org.uk/geton136.htm.

Saved PDFs with text layers, in the session's fetch cache (temporary, not kept):

- `webfetch-1795826316143-gw69a4.pdf` (NBS Report 9725)
- `webfetch-1795826216903-mmcpl1.pdf` (Auburn TR 9)
- `webfetch-1795826462025-nvqpn9.pdf` (Antarctic Journal 1975)

---

## Appendix B (second-level search): Slow FSK through a fixed-tuned high-Q VLF antenna: literature findings (Parts A and B)

**Limits of this pass.** The shared web-search budget ran out mid-task, so later sources were reached only by known URL. abelian.org (EbNaut) failed DNS on every attempt and web.archive.org is blocked, so Nicholson's own pages were not read. No repository files were touched; fetched text sits in the session scratchpad only.

Flags: **[M]** measured, **[S]** specification or requirement, **[C]** calculated (mine unless attributed), **[R]** rule of thumb.

### A1. Amplitude/phase versus offset, and the frequency-step transient

**Static response [C, standard].** For the single-tuned circuit with B = f0/Q, the complex-envelope response is H(δ) = 1/(1 + j·2δ/B). Amplitude is 1/√(1+(2δ/B)²), phase is −atan(2δ/B), and group delay at resonance is τ = 2Q/ω0 = 1/(πB).
- A measured check: NIST SP 250-67 gives WWVB's loaded bandwidth as "slightly over 600 Hz" (estimated) and the group delay through the dual antenna as about 500 µs [M]; 1/(π·600 Hz) = 530 µs.

**Step transient [C], consistent with the classical papers.** After a phase-continuous step f1→f2, the output is the new forced response plus a free oscillation at the antenna's own resonance, of initial size H(f1)−H(f2), decaying as e^(−t/τ). The residual is 4.3% of that vector after T = 1/B and 1% after 1.47/B.
- McCoy 1954 (abstract only): for small steps near midband the FM transient equals the AM transient of the equivalent low-pass circuit; large steps depend on step size, start frequency and direction.
- Gumowski 1954 (abstract only): step and impulse FM response of a single tuned circuit.
- Carson & Fry 1937 (OCR text, partly read): response = quasi-stationary term E·Y(iΩ)·exp(i∫Ω dt) plus a correction series in derivatives of Y and of Ω(t). This suits slow sweeps, not abrupt keying.

**Navy keying practice (NAVELEX 0101,113, Aug 1972, page images read).**
- pp. 1-9/1-10, citing Watt's Table 6.5.1: b3dB = 1.16·k·Rw·(m + τb), with a 3 dB/6 dB bandwidth ratio of 0.58 for a single-tuned antenna. With Rw = 71.42 wpm, k = 0.25, m = 1, τb = 1.45, the result is about 50 Hz for 50-baud FSK [R].
- My caveat [C]: k·Rw = 17.9 Hz, but the dotting frequency of 50 baud is 25 Hz, which would give about 71 Hz.
- pp. 3-3/3-5: mark and space are 50 Hz apart. A transition is allowed only when both have coincident same-sense zero crossings, every 20 ms. The stated reason is antenna Q "greater than 200": stored energy will not tolerate abrupt change without arcing.
- p. 3-36: bandwidth–efficiency product b·η = (320π³/c²)·f⁴·he²·Cs. Lower-VLF antennas were "limited to low data rate ICW"; saturable-reactor synchronous tuning was in use at Cutler.

**Vallese, ElectroPhysics 1972 (DTIC AD0746119, OCR read).**
- Same Watt formula, with k = 0.19 Cable Morse, 0.25 five-unit synchronous TTY, 0.42 seven-unit start-stop, 0.45 Continental Morse.
- For single-tuned antennas, element length × 6 dB bandwidth ≈ 3, so f_r = b6/6 [R]. That is baud ≈ b6/3 ≈ 0.58·b3 [C].
- Synchronous reactance switching had extended practical shift to about ±50 Hz at about 100 wpm, but causes large transients unless timed.
- Bessel-type feed networks are preferred over Chebyshev because phase distortion causes ringing.

**Gebhard, NRL Report 8300 (OCR read).**
- Shore-antenna bandwidth limited CW to roughly 30 wpm (the OCR is garbled; inferred from "about one-half" of the lowest teleprinter speed).
- NRL's 1951 answer was a four-level FSK teleprinter code at half the keying rate.
- The 1963 keyer reached 60 wpm with two frequencies. It shifted at coincident zero crossings with a linear frequency ramp, long enough to keep transition sidebands inside the antenna bandwidth.
- Earlier abrupt shifts had caused horn-gap flashover, overload trips and burnt helix cable.

**Other numbers.**
- Watt, Plush, Brown & Morgan 1961 (full text): NBA's 6 dB transmitting bandwidth was 44 c/s [M]. Fig. 5 shows a typical keying envelope on a 0–60 ms axis.
- Megapulse US 4,689,803 (fragments via the fetch tool): Q "often several hundred"; reactor tuning raised Cutler's bandwidth to 72 Hz at 17.8 kHz; 200-baud operation with 100 Hz shift against a 160 Hz system bandwidth; 1600-baud MSK at 37.2 kHz with antenna bandwidth under a quarter of the shift, using synchronous tuning.
- US 5,034,697 (Navy): Q "usually greater than 250"; example 21.40 kHz ±50 Hz; detuning loss "as much as 4 dB".
- Cory & Holzheimer, AGARD CP-529 paper 29 (OCR): a 3 dB loaded bandwidth of 20–25 Hz was chosen to support 10 chips/s MSK [R]. Omega sites could carry 5–25 baud, with the rate "taken equal to the available antenna bandwidth".

**4-channel MSK and STANAG 5030.**

| Item | Value | Source |
|---|---|---|
| Phase deviation from linearity | ≤12°, for ≤0.5 dB Eb/N0 loss at CER 1e-3 [S] | Harrington, AGARD CP-529 paper 20 (OCR read), citing STANAG 5030 Ed. 3 Amdt 2 (1989) |
| Bandwidth for 4-channel MSK | 200 Hz [S/R] | Belrose, AGARD paper 19 (OCR read) |
| Modes | A1A, 50 Bd FSK, 100 Bd MSK-2, 200 Bd MSK-4 [S] | Nautel VLF120 sheet (STANAG 5030/4724) |
| 200 Bd MSK shift | 100 Hz (Fc ± 50 Hz) | Ashdown, AGARD paper 21; Megapulse patent |
| 75 baud | no figure found | — |

- Harrington also reports that stepped-CW bandwidth checks (10 Hz steps over 15–30 minutes) mislead with non-linear amplifiers and are disturbed by "cloud cover and wind gust factors".
- Belrose: Anthorn at 19 kHz has Rr = 0.08 Ω [M], 0.18 Ω including bandwidth resistors, X = −j85 Ω, 44% efficiency and 100 Hz bandwidth. Operating bandwidth is 1.5–2 times the antenna-system bandwidth when conjugately matched [R]. NIST quotes the same 1.5 factor for WWVB.
- STANAG 5030 itself is restricted (per the i56578-swl blog) and was not read.

### A2. Transmitter tolerance of the detuned load

**Load seen at offset [C].** Z = R(1 + jx) with x = 2δ/B.

| x | Offset δ | Load angle | VSWR (ref R) |
|---|---|---|---|
| 0.5 | B/4 | 27° | 1.64 |
| 1 | B/2 | 45° | 2.62 |
| 1.8 | 0.9·B | 61° | 5.0 |
| 2 | B | 63° | 5.83 |

A stiff voltage-source (class-D) drive gets no matched-source broadening unless the coupling network supplies it (my inference).

**Vendor and patent statements.**
- Nautel VLF120 sheet (text read) [S]: full-bridge class D, 2 kW per amplifier, rated conservatively for "high reactive and reflected power typical of narrow band low frequency antennas". Rated power is delivered up to 5.0:1 VSWR, with "graceful power reduction" beyond and self-protection into any load. Efficiency above 82% is "reduced when operating with high VSWR".
- Rockwell US 5,404,114 (summary read): solid-state class-D VLF transmitters on high-Q antennas are damaged if drive is removed "or drastically changed in frequency". Stored energy returns and dissipates in the output devices above rating; sensing must act in under a quarter RF cycle.
- Nautel US 7,107,026 (summary read; 190–535 kHz): class-D transmitters are "much more susceptible than their predecessors" to mismatch, with reflected power dissipated in the final heat sinks.
- GTE US 5,345,200 (summary read): 24 Ω source into 0.1 − j50 Ω at 15 kHz (Q = 500). "Large current excursions, however modern solid state transmitters can handle these"; resistive loading "not a practical solution".

**Bandwidth-resistor practice.** Anthorn (Belrose); NAVELEX p. 3-51 mentions cooling "the bandwidth resistor if installed"; Harrington notes the resistor current affects tune state. Hartley 1968 (abstract only) reports an energy-recovery pulse technique giving more than 3 times the broadbanding × efficiency product of resistive loading.

**Trips.** WWVB disconnects drive on excess reflected power in gusts, with three retries (SP 250-67). DCF77 interrupts transmission when storm or freezing-rain mismatch grows too large (Piester et al. 2012).

**Not found.** Published phase-angle limits or zero-voltage-switching loss data for Continental, Telefunken/Hagenuk, Thales, Megapulse or amateur class-D/E designs.

### B3. Tuning drift and the phase it causes

**Governing relation (Morgan, NBS Report 7286, 1962, page image checked).**
- δt/T ≈ 0.159·Q0·(ΔC/C), i.e. Δφ ≈ Q·ΔC/C = 2Q·Δf/f0 rad. He attributes it to W. W. Brown (private communication) and names "wind, snow, ice, etc." as causes.
- His example: Q0 = 1000, 20 kHz, 1 µs target gives ΔC/C ≈ 1.4e-4, or "about 2 pF" of 0.008 µF. My arithmetic gives 1.26e-4 and 1.0 pF.

**Measured magnitudes.**

| Station | Finding | Source |
|---|---|---|
| WWVL/WWVB, Fort Collins | Servo holds phase within about 0.1 µs; "a few microseconds" with a period of "several seconds" in heavy wind as the antenna sways and detunes [M]. A few µs is roughly 14–22° at 20 kHz. Slow causes: temperature, ice, thermal expansion | Andrews 1965 (full text) |
| WWVL, Sunset | Residual no more than 0.05 µs under remote phase lock [M] | Fey, Milton & Morgan 1962 (full text) |
| DCF77 | Slow variations up to about ±0.1 µs (±2.8° at 77.5 kHz) from temperature and slight detuning [M]; keying-rhythm antenna transients average out | Piester et al. 2012 (full text) |
| Omega | Rise time to 63%, in ms at 10.2/11.05/11.33/13.6 kHz: Liberia 13/10.5/10/6, Hawaii 12/10/9.5/6, North Dakota 23/22/19.5/12.5 [S/M] | DOT-CG-N-2-82, 1984 (OCR read) |

- From the Omega rise times, B = 1/(πτ) is about 14–53 Hz and Q about 256–737 [C], squarely in your regime.
- Omega Hawaii (NOSC, OCR read): loss resistance rises at rain onset and with salt-contaminated insulators; the "wind" capacitance step used for servo tests was 400–1000 pF [M].
- Graf 1965 (NASA-CR, OCR read): the antenna is "the major source of phase error", with slow drift from tuning-coil heating and short-term variation from wind loading. No magnitudes given.

**Amateur reports (sub9khz news pages, read via the fetch tool).**
- DK1IS at 8.27 kHz: system bandwidth about 180 Hz, Q ≈ 46 [M]; no retuning needed for wind sway so far.
- IW4DXW: antenna current 625 mA dry, 570 mA cloudy, about 590 mA at 86% humidity [M].
- DK7FC: a rain trip circuit, and a SpecLab-based regulator that "holds the phase and target current".
- ON7YD: day-to-day weather capacitance changes are "rather small", corrected by a "few %" of loading-coil adjustment.
- No amateur source gave degrees of wind-induced phase.

### B4. Automatic tuning, and what a coherent receiver needs

**Automatic tuning.**
- Omega 1966 system report (DTIC AD0630900, OCR read), §6.6.8–6.6.9: a current transformer and voltage divider feed a phase detector. Its output is switched by the multiplexing waveforms to per-frequency variometer servos, so each loop acts only during its own segment. Separately, antenna-current feedback to the exciter stabilises radiated phase against "antenna element sag which is affected by temperature".
- NOSC Norway and Hawaii reports: variometers are ganged through sprocket ratios (4/3 at 10.2 kHz, 3/4 at 13.6 kHz) so one capacitance change corrects all frequencies. Otherwise the system "hunts back and forth as each frequency is keyed".
- Omega signal spec [S]: radiated phase error under 1° averaged over one minute.
- WWVB (SP 250-67): a V–I phase comparator drives a motorised variometer through a controller. "Sudden gusts can move the antenna too rapidly for the autotuning system to respond."
- Morgan 1962, for 20.0/20.5 kHz alternation: reduce power, retune by saturable reactor under phase-detector feedback, restore power at a zero crossing. Fey & Looney 1966 (full text) describe WWVL alternating two carriers 100 or 500 Hz apart on 10 s dwells; an improved AGC in the transmitter phase-control servos visibly improved stability.
- No source gave loop bandwidth, slew rate or deadband figures. The only hint is that seconds-scale gust motion beats the WWVB and WWVL loops.

**Coherent receiver requirement.** DL4YHF's EbNaut receiver notes (read) budget about 15° of phase error over the coherent span: 4.72 µs over a 6.75 h message of 8 s symbols at 8822 Hz. They say transmitter and ionosphere add further error, without quantifying antenna drift.

### Rules of thumb for the stated regime

1. **Span, not keying rate, binds.** With B of 6–600 Hz and 0.1–10 tones/s, T·B runs from 0.6 to 6000. Transients matter only at the corner (10 tones/s into 6 Hz: τ = 53 ms, 15% residual at symbol end) [C].
2. **Edge-tone penalty** for span S centred on resonance: 10·log(1+(S/B)²) and ∓atan(S/B) [C]. S = B/4 gives 0.26 dB and 14°; S = B/2 gives 0.97 dB and 27°; S = B gives 3 dB and 45°; S = 2B gives 7 dB and 63°.
3. **Navy rule [R]:** b3 ≈ 1.16·f_r·(m + 1.45). ICW baud ≈ 0.58·b3. Fifty baud with 50 Hz shift needs about 50 Hz; 200-baud MSK needs 200 Hz and ≤12° phase non-linearity. MSK chip rate ≈ B/2 to B/2.5.
4. **Phase versus detuning:** Δφ ≈ Q·ΔC/C [R]. For a 15° budget [C]: Q = 1400 allows about 190 ppm of capacitance (96 ppm of frequency); Q = 46 allows 0.58%; Q = 13 allows 2.1%. One degree at Q = 1400 is 12.5 ppm.
5. **Transmitter:** hold x = 2δ/B below about 1.8 for a 5:1 VSWR rating [C]. Key phase-continuously; never remove or jump the drive of a class-D stage on a high-Q load.

### Access and DOIs

**Not accessed:** Watt 1967 (only via NAVELEX and Vallese); STANAG 5030 and MIL-STD-188-140; Hartley 1968 and 1971, McCoy, Gumowski (abstracts only); Salinger 1942, van der Pol 1946, Baghdady 1958, Weiner & Leon 1965, Swanson 1983 (bibliographic only); Clavier; Belrose 1991 (the AGARD paper was read instead); Jacob & Brauch 1954; RADC TR-74-161; Nicholson's EbNaut pages; DK7FC's own site (down); Drumgrange datasheets (search snippet only); MSF, HBG, Loran-C and Decca tuning literature; NOSC TR 246 beyond the gear-ratio passage.

**DOIs confirmed by direct Crossref lookup:** 10.1109/TCOM.1968.1089913; 10.1109/TCOM.1971.1090683; 10.1109/JRPROC.1954.274498; 10.1109/JRPROC.1954.274519.

**DOIs returned by Crossref bibliographic search with matching metadata:**
- Fey, Milton & Morgan 1962: 10.1038/1931063c0
- Fey & Looney 1966: 10.1109/TIM.1966.4313539
- Andrews 1965: 10.1109/TIM.1965.4313482
- Swanson 1983: 10.1109/PROC.1983.12743
- Watt et al. 1961: 10.6028/jres.065D.070 (Crossref mis-lists the first author as "Wait")
- Blair & Morgan 1965: 10.6028/jres.069D.100
- Carson & Fry 1937: 10.1002/j.1538-7305.1937.tb00766.x
- Salinger 1942: 10.1109/JRPROC.1942.231701
- Baghdady 1958: 10.1109/TCT.1958.1086474
- Weiner & Leon 1965: 10.1109/PROC.1965.3924
- van der Pol 1946: 10.1049/ji-3-2.1946.0024

**No DOI:** NAVELEX 0101,113; NBS Report 7286; AGARD CP-529 (DTIC ADA267991); NRL 8300 (ADA084225); the Omega reports (AD0630900, ADA056702, ADA082114, ADA137673); NIST SP 250-67; the patents.

Fetched text mirrors are in the session scratchpad (temporary, not kept) (temporary).

---

## Appendix C (second-level search): Modern (2004-2026) switched/time-varying, DAM, mechanical-antenna and beyond-Chu literature: findings

**Headline:** nothing I could open demonstrates energy-synchronous switching of an antenna reactance at tens of kV or kW in the modern literature. The highest-power modern demonstrations are 64 W into a lumped antenna model (about 10 kV p-p on the capacitor, but only 300 V on the switch) and 10-16 W at VHF. The one modern high-voltage VLF switching experiment (Whitmore et al. 2025) I could read only as an abstract, which gives no ratings.

**Method limits.** The session's shared web-search budget ran out partway through, so Section 3 and several named targets were covered only by direct fetches of known arXiv IDs and DOIs, and by Crossref title-to-DOI resolution for papers already known to exist. No further open-ended discovery was possible. Every DOI below was confirmed against `api.crossref.org/works/<DOI>` (authors, title, year).

**Access codes.**
- [F] full text read by direct text extraction
- [F*] full text read through the fetch tool's summarizer (lower confidence on fine detail)
- [A] abstract only
- [N] not accessed

#### 1. DAM / switched small antennas

**Corrections to your target list**
- Manteghi 2009 is an IEEE AP-S symposium digest paper, not AWPL.
- "Santos" belongs to the UCLA (Y. E. Wang) group, not the Adams group. The Adams-group energy-synchronous papers are Schab/Huang/Adams.
- Salehi's arXiv 1511.07473 was withdrawn by arXiv admin with the comment "administratively withdrawn because of authorship dispute and retraction by the journal" (journal not named). The v1 abstract states the zero-crossing condition.

**Sources**
- **Yao & Wang, IEEE MTT-S IMS Digest 2004**, vol. 2, 1271-1274, 10.1109/MWSYM.2004.1339221; companion AP-S 2004, 791-794, 10.1109/APS.2004.1329789. [A] RF switches integrated in a 2.4 GHz patch with 30 MHz bandwidth; transmitted a digitally modulated signal of more than 400 MHz bandwidth (stated as experimental).
- **Xu, Jing & Wang, AP-S 2006**, 167-170, 10.1109/APS.2006.1710480. [A] λ/10 switched dipole at 100 MHz; efficiency-bandwidth product 0.105 vs 0.0013 conventional (SIMULATED). States that switch loss is the adverse factor and that low on-resistance, high off-resistance and fast switching are needed.
- **Xu & Wang**: RWS 2007, 483-486, 10.1109/RWS.2007.351873; iWAT 2007, 463-466, 10.1109/IWAT.2007.370174; AP-S 2007, 2261-2264, 10.1109/APS.2007.4395981. [A] Theory and transmission-line analysis; no numbers in abstracts.
- **Xu & Wang, iWAT 2010**, 10.1109/IWAT.2010.5464640. [A] Claims the first DAM transmitter with an electrically small antenna; received information tone "significantly higher" than conventional once signal bandwidth exceeds antenna bandwidth (MEASURED, no numbers in abstract).
- **Azad & Wang, IEEE TCAS-I 61(3) 902-910, 2014**, 10.1109/TCSI.2013.2283995. [A]; details from Azad's UCLA dissertation (2013, escholarship 6fp808gn) [F].
  - Setup (MEASURED): 270 kHz; 5 cm, 24-turn coils, 74 µH, intrinsic Q 35, loaded transmit Q 17.5; on-off keying.
  - Switch: SPDT analog switch ISL43L220 (0.22 Ω) diverts the loop to ground at maximum inductor current.
  - Result (MEASURED): data rates from f0/8 up to the carrier frequency; modulation tones up to 18 dB stronger than conventional.
  - This is OOK; I found no capacitor-switching FSK paper by Azad.
- **Manteghi, AP-S 2009**, 10.1109/APS.2009.5171747. [A] Concept only.
- **Salehi, Manteghi, Suh, Sajuyigbe & Skinner, PIER 143, 421-445, 2013**, 10.2528/PIER13102204. PDF blocked by a human-verification page; details are from Salehi's Virginia Tech dissertation (2013) [F], which covers the same work.
  - Condition: switch the capacitor at the zero crossing of its own voltage (or the inductor at zero current). All energy is then in the other reactance, charge/flux continuity holds without an impulse, and the stored energy moves to the new resonance with no switching loss.
  - Off-timing: a voltage step and impulsive capacitor current; stored electric energy drops, worse for larger switched C. Equation symbols were lost in my text extraction; the standard charge-sharing result, energy fraction lost = C2/(C1+C2) × (v/Vpk)², is my reconstruction.
  - SIMULATED (ADS): 500 to 300 MHz, Q 119/198; switched-inductor case at non-zero current gives an after/before current ratio of 0.36.
  - MEASURED: electrically coupled loop antenna, f1 = 57.75 MHz, f2 = 42 MHz, Q1 = 18.6, Q2 = 52.5, about 3 MHz bandwidth; FSK switched at up to 12 MHz (24 Mb/s).
  - Switch: Avago HSMP-482 PIN diode (0.6 Ω), on/off about 65 ns. CMOS/GaAs/pHEMT switch ICs "failed to maintain the required Q" (Q fell from 22 to about 7).
  - Stated problems: switch insertion loss, capacitor Q, bias-filter time constant, unequal on/off dwell.
- **Manteghi, IEEE TAP 64(4) 1201-1208, 2016**, 10.1109/TAP.2016.2525828. [A] Transient-state antenna radiating DSB-AM and circularly polarized QPSK; "simulation and measured results" claimed, no numbers in abstract.
- **Manteghi, IEEE APM 61(3) 14-26, 2019**, 10.1109/MAP.2019.2907892. [A] Argues that instantaneous bandwidth does not bound information rate if the frequency or type of stored energy is modulated while total stored energy is kept. Pendulum analogy plus SIMULATIONS only. Subtitle: "without violating the thermodynamic Q-factor".
- **Schab, Huang & Adams, IEEE Access 7, 30213-30219, 2019**, 10.1109/ACCESS.2019.2902365. [A + university news item] OOK at 27.12 MHz, antenna just under 1 m, far-field over-the-air (MEASURED). DAM "partially removes" the antenna's narrowband filtering; a time-domain model predicts transients "under varying synchronization conditions". No bit rates or power in what I could read.
- **Schab, Huang & Adams, IEEE OJAP 1, 41-46, 2020**, 10.1109/OJAP.2020.2972842. [A] Energy-synchronous PSK, MEASURED against an equal-size, equal-efficiency conventional antenna. Context from your already-held arXiv 2103.11906 [F*]:
  - The open-circuit scheme opens the switch at the antenna voltage peak and recloses when the source is also at peak.
  - Parasitics cause internal ringing that corrupts stored energy and radiates at a second frequency.
  - Hardware: ADG919 CMOS switch (about 5 Ω on, 4 pF off), total Q about 10, milliwatt class.
- **Schab, IEEE AWPL 19(11) 1911-1915, 2020**, 10.1109/LAWP.2020.3005412 (arXiv 2003.12623). [F] The key critique; analytical and numerical only.
  - Ideal non-resonant DAM gains over a resistively broadbanded conventional transmitter are unbounded as ka goes to 0.
  - "Quasi-resonant" DAM, which is nearly all practical schemes, gains only over a limited size range.
  - Conclusion, verbatim: such systems "may have limited applicability at extremely low frequencies"; loop-based methods likewise (efficiency scales as (ka)^4).
- **Schab, Singh & Bohannon, IEEE TAP 68(7) 5209-5217, 2020**, 10.1109/TAP.2020.2980355. [A] Non-LTI transmitters have no defined Q or bandwidth; assess them by time-domain distortion against LTI bounds.
- **Santos, Fereidoony, Hedayati & Wang, IEEE TMTT 68(12) 5029-5041, 2020**, 10.1109/TMTT.2020.3016381. [A]; Santos UCLA dissertation 2021 [F].
  - Setup: BFSK centred at 75 MHz (70/75 MHz) on a capacitively loaded loop, 0.0275λ × 0.0251λ; unloaded Q 403/413 and about 2% efficiency (SIMULATED); passive bandwidth about 0.25%.
  - Switch: GaN EPC2012C (0.1 Ω on, 225 pF off as stated).
  - Condition: switch at maximum loop current, i.e. zero voltage on the switched capacitor; bit timing 2/fb = n/f_on + m/f_off for phase continuity.
  - MEASURED: efficiency-bandwidth improvement β up to 5 over 700 kb/s-7 Mb/s, far below theory. Deliberately wrong (zero-current) timing was clearly worse at high rates. At low rates exact timing is not needed, "so long as the switching occurs at the same locations per cycle".
  - Losses listed: on-resistance, charge loss from non-zero-voltage switching, driver/switch transients, timing resolution.
  - Earlier MILCOM 2018 (10.1109/MILCOM.2018.8599778) [A] claimed a 45× improvement at 50 MHz; the abstract does not say whether that was measured.
- **Santos, Bhakta, Fereidoony & Wang**: IEEE Access 8, 199462-199469, 2020, 10.1109/ACCESS.2020.3035135 [A] (4FSK at 35 MHz, MEASURED far field); IET Circuits Devices Syst. 16(5) 373-381, 2022, 10.1049/cds2.12108 [N, 403; content from dissertation]. Power handling 40-42 dBm input (MEASURED compression about 41-42 dBm); drain swing 37 V (SIMULATED). RF coupling into the gate drive ("self turn-on") is the limiting problem.
- **Strachen, Booske & Behdad, J. Appl. Phys. 135, 224502, 2024**, 10.1063/5.0213308 (arXiv 2404.03468). [F]
  - Class-E switch-mode stage driving a lumped high-Q antenna model (100 pF, 28 µH, 1.43 Ω) at 3.045 MHz; no physical antenna, no field test.
  - MEASURED: 64 W into the model resistor, 78% total efficiency, 9.5 A peak, 300 V peak drain on a 1 kV Si VDMOS (DRF1201).
  - ESTIMATED from current: about 10 kV p-p across the capacitor; 700 W projected at 1 kV drain.
  - Modulation: BFSK with 30 kHz spacing, about 65% efficiency; BPSK 38 kb/s; BASK 24 kb/s.
  - Zero-voltage switching holds only over about 3.02-3.31 MHz; abrupt phase changes produce drain voltage spikes.
  - Related: Strachen, Mohammadi, Booske & Behdad, IEEE TAP 70(3) 1600-1611, 2022, 10.1109/TAP.2021.3119095 [A]: 2 m dipole with class-AB current buffer, whole HF band, more than 15 dB over passive matching (MEASURED, field tests).
- **Georgia Tech pulse-trapping, journal versions**
  - Slevin, Cohen, Opalinski, Thompson & Gołkowski, IEEE TAP 70(1) 97-110, 2022, 10.1109/TAP.2021.3096950. [A] Theory and numerical modelling only.
  - Whitmore, Cohen, Hansen, Singer, Julch, Lampley, Miller & Yeager, IEEE TAP 73(6) 3882-3891, 2025, 10.1109/TAP.2025.3540651. [A] Conference precursor AP-S 2024, 2479-2480, 10.1109/AP-S/INC-USNC-URSI52054.2024.10687254. [A]
    - 275 m horizontal dipole on the ground, interrupted by a high-voltage switch (co-authors from Eagle Harbor Technologies and Long Wave Inc.).
    - On-antenna charge trapping MEASURED; magnetic field "boosted"; far-field detection; broadband operation and frequency hopping.
    - Efficiency "greatly limited" by the horizontal geometry. The abstract gives no voltage, power or field numbers.

#### 2. Mechanical / piezoelectric / magnetoelectric transmitters

- **Bickford et al., IEEE TAP 67(4) 2209-2223, 2019**, 10.1109/TAP.2019.2893329. [A] Theory: metre-scale moving magnets/electrets could exceed coils and short dipoles by more than 8 orders of magnitude at 1 kHz and below (ESTIMATED).
- **Fawole & Tabib-Azar, IEEE TAP 65(12) 6927-6936, 2017**, 10.1109/TAP.2017.2761555. [A] 22 Hz; 0.6 µT at 1 m; 8 b/s; binary data to 70 cm through obstacles (MEASURED).
- **Burch, Garraud, Mitchell, Moore & Arnold, IEEE TAP 66(11) 6265-6272, 2018**, 10.1109/TAP.2018.2869205. [A] 3 cm³ rotating magnet, below about 500 Hz, detected beyond 100 m (MEASURED). Fields are quasi-static; radiation is "predicted to be weak".
- **Strachen, Booske & Behdad, PLOS ONE 13, e0199934, 2018**, 10.1371/journal.pone.0199934. [F*] 150 Hz spinning magnet with a saturable mu-metal shutter; ASK at 60 b/s, modulation depth about 33%, field measured at 34 cm (MEASURED); about 17 W coil power (ESTIMATED).
- **Gołkowski et al., AP-S 2018**, 65-66, 10.1109/APUSNCURSINRSM.2018.8608677. [A] Shutter antenna with fixed magnets; no numbers.
- **Prasad M N et al., Sci. Rep. 9, 13220, 2019**, 10.1038/s41598-019-49341-4. [F*, PMC] Pendulum array at 1031 Hz; mechanical Q about 62 (ESTIMATED from model); OOK at 2 b/s; range about 25-30 m at 0.6-1.9 W; about 7 dB better than a coil (MEASURED).
- **Rezaei et al., IEEE TAP 68(3) 2332-2339, 2020**, 10.1109/TAP.2019.2955069. [A] Rotating magnet, 23 dB more field than an equal-volume coil at 0.35 W; ASK via a modulation coil.
- **Glickstein et al., IEEE Access 8, 2455-2471, 2020**, 10.1109/ACCESS.2019.2961708. [A] Continuous-frequency FSK to minimise torque; 100 Hz, about 0.25 b/s, 5 m through concrete (MEASURED).
- **Barani, Kashanianfard & Sarabandi, IEEE TAP 69(7) 3726-3739, 2021**, 10.1109/TAP.2020.3044385. [A] Phase modulation and frequency multiplication with movable high-permeability plates; prototype measured, no rates in abstract.
- **Thanalakshme et al., IEEE TAP 70(5) 3710-3722, 2022**, 10.1109/TAP.2021.3137244 (arXiv 2012.14548). [F] Resonant rotor at 166 Hz; 21 W (MEASURED) for 1.09 fT rms referred to 1 km; OOK at 5 and 10 b/s (MEASURED), rate set by the mechanical time constant; multi-rotor version to about 1 kHz.
- **Liu, Gong, Liu & Hou, IEEE TAP 69(10) 6391-6400, 2021**, 10.1109/TAP.2021.3070657. [A] 30 Hz; uses 2.74% of the equivalent coil's power; 10 m surface-to-undersea link.
- **Zhang, Gong, Wang, Ji & Fang, IEEE TAP 71(3) 2300-2310, 2023**, 10.1109/TAP.2023.3240068. [A] 5×5 magnet array, 360 A·m²; BFSK by motor speed; detected at 500 m range and 5 m depth; 10 b/s at 60 m and 5 b/s at 110 m (MEASURED); 100 fT at 1 km (SIMULATED).
- **Sun et al., Electronics 11, 596, 2022**, 10.3390/electronics11040596. [A] Rotating-shutter 2FSK at 12.5 b/s indoors (MEASURED).
- **Southeast University group (Cao)**
  - Wang et al., Int. J. Antennas Propag. 2021, 5524732, 10.1155/2021/5524732. [A] 0-250 Hz, magnets under 10 cm³, 2FSK by speed control.
  - Zhang et al., IEEE TAP 71(3) 2321-2329, 2023, 10.1109/TAP.2023.3238643. [A] 75-125 Hz array, 2 b/s.
  - Wang et al., iScience 26, 106741, 2023, 10.1016/j.isci.2023.106741. [A] 2FSK array experiment.
- **Gong, Liu & Liu, PIER M 72, 125-133, 2018**, 10.2528/PIERM18070204. [A] No modulation data.
- **Hassanien, Breen, Li & Gong, Sci. Rep. 10, 17006, 2020**, 10.1038/s41598-020-73973-6. [F*, PMC] The closest analogue to your fractional-bandwidth regime.
  - PZT disc, loaded Q about 850; BFSK tones 33.218/33.248 kHz (30 Hz shift), both inside the 3 dB bandwidth, with no retuning.
  - Up to 60 b/s; ASK settling about 24 ms (MEASURED).
  - 0.23 fT rms at 1 km for 1.2 W, extrapolated from measurements within 6 m.
  - More than 6000× the efficiency of an equal-size small antenna (ESTIMATED).
- **Dong et al., IEEE AWPL 19(3) 398-402, 2020**, 10.1109/LAWP.2020.2968604. [A] Magnetoelectric pair, 120 m, 80 V drive, 400 mW (MEASURED); DAM "demonstrated", no rate in abstract.
- **Xu et al., Sensors 19, 853, 2019**, 10.3390/s19040853. [A] Magnetoelectric transmitter, 10⁴× a same-area loop.
- **Chu et al., Research 6, 0208, 2023**, 10.34133/research.0208. [F*, PMC] 21.2 kHz, mechanical Q about 40, 500 Hz bandwidth; BASK/BPSK at 300 b/s, error-free at 18 m; 112 nT at 1 m (MEASURED). Deliberately low Q to avoid needing DAM.
- **Cui et al. survey, iScience 26, 105832, 2023**, 10.1016/j.isci.2022.105832. [F*, first 100k characters] Motor inertia limits rotating-magnet FSK/MSK rate; resonance limits piezoelectric FSK tone spacing.
- **Latypov, J. Appl. Phys. 139, 184501, 2026**, 10.1063/5.0335626 (arXiv 2604.26980). [A] Critical: mechanical emitters remain subject to Chu-Harrington, and measured devices already sit near the derived limit, casting doubt on further orders-of-magnitude gains.

#### 3. Time-varying / non-Foster matching and caveats

- **Shlivinski & Hadad, PRL 121, 204301, 2018**, 10.1103/PhysRevLett.121.204301 (arXiv 1805.03704). [A + review] THEORY: switch transmission-line parameters while a short pulse is inside the line. Follow-up: Hadad & Shlivinski, IEEE TAP 68(3) 1643-1654, 2020, 10.1109/TAP.2020.2967302. [A]
- **Li, Mekawy & Alù, PRL 123, 164102, 2019**, 10.1103/PhysRevLett.123.164102. [A + review] Parametric matching network pumped at about 2f plus deliberate source detuning; bandwidth scales as 1/√Q; stability addressed; SIMULATED 300 MHz loop with varactors.
- **Mekawy, Li, Radi & Alù, Phys. Rev. Applied 15, 054063, 2021**, 10.1103/PhysRevApplied.15.054063. [A] Experimental small loop with a capacitor modulated near 2f; "stable non-Foster wideband" enhancement beyond passive (MEASURED per abstract, no numbers).
- **Loghmannia & Manteghi, IEEE APM 64(5) 55-68, 2022**, 10.1109/MAP.2021.3089997 (arXiv 1907.11683). [F] RECEIVE only: parametric up-converter at 100 to 2290 MHz; 32× bandwidth for about 2 dB noise figure (SIMULATED).
- **Hayran & Monticone, IEEE APM 65(4) 29-38, 2023**, 10.1109/MAP.2023.3236275 (arXiv 2205.07142). [F] Review. Caveats it states:
  - switched matching works only for short pulses and needs a fast detector to know the pulse is in the line;
  - spectral compression or broadening must be undone afterwards;
  - parametric gain can go unstable;
  - pump energy must be counted in the efficiency figure and may be impractical or damaging;
  - dispersion is usually ignored in the theory.
  - Related: Hayran & Monticone, Phys. Rev. Applied 21, 044007, 2024, 10.1103/PhysRevApplied.21.044007 (title verified only).
- **Sussman-Fort & Rudish, IEEE TAP 57(8) 2230-2241, 2009**, 10.1109/TAP.2009.2024494. [A] Receive: up to 20 dB signal-to-noise gain over 20-120 MHz. Transmit: efficiency improved by more than 2× over 5% bandwidth near 20 MHz at 1 W (MEASURED).
- **Stearns, AP-S 2011**, 1942-1945, 10.1109/APS.2011.5996883. [A] Stability theory; a measured monopole match exceeded the Fano limit.
- **Non-Foster transmit limits**, as summarised in Strachen 2024 [F]: device voltage and power limits (reported transmit levels 4 dBm and 1.3 W), bandwidth limited by device voltage rating, instability and distortion at power.
- **Yaghjian, IET Microw. Antennas Propag. 12(4) 459-466, 2018**, 10.1049/iet-map.2017.0648. [A] Passive, not time-varying: lossy dispersive tuning doubles the bandwidth of 50%-efficient antennas relative to the Chu-based prediction.
- **Other verified items (title/abstract only)**
  - Yang, Wen & Sievenpiper, Phys. Rev. Applied 17, 044003, 2022, 10.1103/PhysRevApplied.17.044003: switched energy-trap absorber, measured.
  - Firestein, Shlivinski & Hadad, 10.1103/PhysRevApplied.17.014017.
  - Mirmoosa et al., 10.1103/PhysRevApplied.11.014024.
  - Jayathurathnage et al., 10.1103/PhysRevApplied.16.014017.
  - Wang, Katehi & Peroulis, IEEE TMTT 58(8) 2231-2243, 2010, 10.1109/TMTT.2010.2052870: synchronisation error must stay under about 10% of the pulse width.
  - Shyianov et al., IEEE TCOM 70(3) 2010-2024, 2022, 10.1109/TCOMM.2021.3099842.
  - Akrout & Castellanos, arXiv 2607.23688 (2026): optimal modulation is bang-bang; required switching time grows as antennas get electrically smaller.

**What each camp actually claims.** None claims that Chu's radiation-Q bound on an LTI passive antenna is false. The claims are that information bandwidth is decoupled from impedance bandwidth once time-invariance (Wang, Manteghi, Adams, Cohen) or passivity (Alù, non-Foster) is dropped. Schab's bound, Hayran & Monticone's caveats and Latypov are the substantive critiques I could verify.

#### Synthesis for about 8 kHz, 10-100 kV, 10 kW, 0.1-10 tones/s

The numbers in this section are my own estimates, not from any source.

1. **Transient-free condition.** The lineage agrees: switch a capacitor at its own voltage zero (all energy in the inductor) and an inductor at current zero. The open-circuit OOK/PSK schemes are the dual (open at voltage peak, current zero, hold the charge).
2. **Timing is easy at 8 kHz.** A 1 µs error is 0.05 rad, about 5% of peak voltage and 0.25% of the worst-case dump. The switch technologies that limited the VHF work are not the constraint here.
3. **Efficiency is not the stake at small shifts.** A 10 Hz shift needs ΔC/C of about 0.25%, so even a worst-phase switch dumps only about 0.25% of stored energy. Stored energy is PQ/ω, about 200 J at Q = 1000 and 20 J at Q = 100, so about 0.5 J per transition at worst. The real issues are impulsive current in the switch, spectral splatter, and an open switch holding off the full node voltage.
4. **Tens of kV is not demonstrated in the modern DAM literature I could read.** Demonstrated levels: milliwatts (Adams, Salehi, Azad), 10-16 W with 37 V drain swing (Santos), 64 W with 300 V on the switch and about 10 kV p-p on the capacitor (Strachen). The Strachen architecture points to switching at a low-voltage node rather than the antenna node. Whitmore 2025 and your Kemp 2019 are the only modern high-voltage VLF experiments; I could not get Whitmore's ratings.
5. **DAM's headline benefit barely applies at your rates.** It targets symbol rates far above antenna bandwidth. Your time constant Q/(πf) is 40 ms at Q = 1000 and 4 ms at Q = 100, against symbols of 0.1-10 s. Only Q near 1000 at 10 tones/s is marginal. Your actual problem is resonance tracking when tone spacing exceeds f/Q (8 Hz at Q = 1000), which is the classical Wolff/Hartley problem; Kemp and Hassanien are the nearest modern analogues.
6. **Schab's bound bites hardest for you.** At ka of about 0.01, quasi-resonant DAM gives little or no efficiency advantage; the tuning inductor and ground loss still set efficiency.
7. **Parametric and non-Foster matching do not transfer.** They are watt-class at best, receive-oriented, or simulation-only.
8. **Mechanical antennas** work in your bit-rate regime (0.25-300 b/s) but at fT-nT field levels; they are not a route to a 10 kW radiator.

#### Not accessed, not located, unverified

- **Full text not accessed (abstract only):** Yao & Wang 2004; all Xu & Wang papers; Azad TCAS-I; Manteghi 2009, 2016, 2019; Schab/Huang/Adams Access 2019 and OJAP 2020 (IEEE Xplore returned empty pages or HTTP 418; NCSU repository blocked); Santos TMTT and IEEE Access; IET CDS 2022 (403); Slevin TAP 2022; Whitmore 2024 and 2025; Li/Mekawy/Alù PRL and Mekawy 2021 (APS 403); Shlivinski & Hadad; Sussman-Fort & Rudish; Stearns; Yaghjian; Bickford; Fawole; Burch; Barani; Rezaei; Glickstein; Dong; Liu 2021; Zhang 2023; the Southeast University papers (Hindawi and MDPI 403); the PIER 2013 PDF; the Georgia Tech Thompson thesis (403).
- **Not located (search budget exhausted):** Janaswamy "Time-varying antennas" (2014?); any time-varying-specific rebuttal by R. C. Hansen, Gustafsson or Capek (only Hansen's 1981 "Fundamental limitations in antennas", 10.1109/PROC.1981.11950, verified as a citation); "Cao & Yao"; Prasad's URSI 2018 spinning-magnet paper; Barani & Sarabandi AP-S 2018; Hayran & Monticone 2021; any SiC-based or kW-class VLF/LF DAM paper; switch ratings for the Whitmore/Eagle Harbor experiment.
- **Unverified numbers seen only in search snippets:** an IMS 2019 "47×" claim (Santos); "20 kb/s DAM at about 23.95 kHz" for a magnetoelectric antenna; a 2022 magnetoelectric ASK "12 kb/s at 27.68 kHz".
- **arXiv-only, no published DOI checked:** 2607.23688 (the abstract page says accepted to Asilomar 2026).
- **Context only:** an IEEE Spectrum article describes IARPA's EQuAL-P programme (HF/UHF, 10 dB goal, mostly time-varying approaches; NCSU and UW-Madison among performers). It gives no VLF or high-voltage data.

Extracted text of the full-text sources (temporary scratch, nothing written to the repository) is in the session scratchpad (temporary, not kept) (salehi_diss.txt, santos.txt, azad.txt, schab.txt, classE.txt, hayran.txt, loghmannia.txt, thana.txt).

---

## The brief given to the search

Reproduced as sent, so that the report's references to it can be read.

> Literature research task (web research; do NOT edit or create files in any repository). Your final message is the deliverable and nobody else sees your intermediate work, so put everything there. It may be long (3,000–6,000 words is fine).
>
> Question: For a radio transmitter below 9 kHz (VLF/ULF, e.g. 3–9 kHz, with 8.27 kHz as a reference frequency) using an electrically tiny, top-loaded vertical antenna about 200 ft (61 m) tall, fed by a transmitter with 10 kW of available input power, what limits the usable modulation bandwidth, and how can slow frequency-shift keying (FSK, tone rates from about 0.1 to 10 tones per second, total span from a few Hz to a few hundred Hz) be passed through it?
>
> What I already have (do not re-derive, but do check it against the literature and tell me if it is wrong):
> - Lumped model: capacitance C about 0.9 nF (T antenna with 122 m top wire) to 1.85 nF (umbrella); effective height 38–50 m; radiation resistance about 2–3 milliohm at 8.27 kHz; reactance 10–21 kilohm; radiation Q of 6–53 million; loaded Q of roughly 13–1,400 depending on ground and coil loss (−3 dB bandwidth 6–620 Hz); corona-limited top voltage assumed 29 kV (thin wire), 58 kV (umbrella) up to about 100 kV with corona rings; wet weather lowers corona onset by 28–46%.
> - My own derivation, which I want checked: when the antenna is voltage-limited, the antenna current is I = V·ωC whatever the loss, so radiated power is fixed; a transmitter with real power P to spend can afford a total series resistance R = P/I², which gives a −3 dB bandwidth B = P / (2π·C·V²) independent of frequency (V rms). For P = 10 kW that is about 86 Hz (1.85 nF, 100 kV) to about 2.1 kHz (0.9 nF, 29 kV). Beyond that bandwidth, radiated power falls as 1/B (the bandwidth–efficiency product f·R_r/X_C is a constant of the antenna). Is this the standard result (Watt's "VLF Radio Engineering", Wheeler, Hansen, Belrose)? What is it called, and what are the published refinements?
>
> Please find and report, with full citations:
> 1. Classical VLF antenna engineering on bandwidth: bandwidth–efficiency product, intrinsic (radiation) bandwidth, the voltage-limited versus power-limited regimes, and typical bandwidths of real VLF stations (e.g. Cutler, Northwest Cape, Jim Creek, Lualualei, Anthorn/Rugby, Grimeton SAQ at 17.2 kHz, Rhauderfehn, Tavolara, Noviken, Siple Station, HAARP-driven or other ELF/VLF research transmitters, TACAMO trailing wire, balloon/kite/aerostat-lifted antennas). Sources such as A. D. Watt "VLF Radio Engineering" (1967), Wheeler's VLF antenna papers (1958), Belrose, Hansen & co. (NOSC/NRaD/SSC reports), the 1967 NPS Northwest Cape thesis, Jordan & Balmain, etc.
> 2. Keying through a high-Q antenna: historical and modern methods to send FSK or MSK faster than the static bandwidth allows — synchronous/dynamic antenna tuning, saturable-reactor or switched-capacitor/inductor frequency-shift keying, "direct antenna modulation" (DAM) and "energy-synchronous" switching, time-varying matching beyond the Bode–Fano/Chu limits, non-Foster matching. I want the key papers (e.g. Jacob & Brauch 1954; Wolff 1957 "High-speed frequency-shift keying of LF and VLF radio circuits"; Galejs 1963 "Electronic broadbanding of high-Q tuned circuits or antennas"; Hartley 1969/1970 on LF/VLF radiation systems; Yao & Wang, Xu & Wang, Salehi/Manteghi, Schab/Huang/Adams on DAM, 2004–2025; any 2020–2026 work on VLF/LF direct antenna modulation, including piezoelectric/magnetoelectric/mechanical "mechtenna" transmitters and their FSK rates) and what they actually demonstrated: frequencies, Q, shift, keying rate, transient behaviour, the condition for transient-free switching (switch at zero stored energy in the switched element), losses and practical problems (switch voltage/current ratings at tens of kV). State clearly what is measured versus simulated.
> 3. What happens to plain FSK through a fixed-tuned series-resonant antenna: amplitude and phase versus offset, the envelope/phase transient after a frequency step (time constant 1/(π·B)), intersymbol effects, and reflected/reactive power seen by the transmitter (class-D/switch-mode VLF transmitters and their tolerance of mistuned loads). Any rule of thumb relating keying rate and shift to antenna bandwidth (e.g. Navy 50-baud FSK ±25 Hz and 200-baud MSK needing given bandwidths; STANAG 5030).
> 4. Voltage limits for small VLF antennas: corona onset on wires versus diameter (Peek's law at VLF, frequency dependence, the Watt–Hansen laboratory data), insulator and base/coil voltage limits, wet/iced conditions, and what voltages amateur and experimental sub-9 kHz or LF stations actually ran (e.g. DK7FC kite antenna at 8.97/8.27/5.17 kHz, other "dreamers band" stations, 137 kHz/73 kHz amateur stations with kV-level antennas, the SAQ Alexanderson alternator, Cold-War portable/transportable VLF systems). Any documented example near "about 60 m mast, about 10 kW" below 20 kHz.
> 5. Time variation of antenna tuning: measured resonance/capacitance drift from wind, temperature, rain, ice, and the phase modulation it causes on the radiated signal; automatic tuning (variometer servo) practice and its loop bandwidth. This matters because the receiver integrates coherently for minutes to hours.
> 6. Recent academic literature (2018–2026) that adds nuance or contradicts the classical picture: e.g. claims of beating the Chu limit with time-varying or nonlinear antennas, experimental VLF mechanical antennas with FSK/BFSK data rates, wideband VLF transmit techniques, and any critical rebuttals.
>
> Method requirements:
> - Prefer primary sources (journal papers, technical reports from DTIC/NTRS/OSTI, theses) over blogs. For each source, say whether you read the full text, only the abstract, or could not access it. Do not present a claim as coming from a source you could not open.
> - Verify every DOI you cite by fetching https://api.crossref.org/works/<DOI> (or the arXiv/DTIC landing page) and confirm authors/title/year match. List any DOI you could not verify separately. Never construct a DOI from a pattern.
> - Give concrete numbers with units, and note whether they are measured, simulated or estimated.
> - Do not include any personal or project-identifying information in web requests; use generic queries.
> - End with (i) a short list of the findings that bear most directly on choosing an FSK tone rate and total FSK span for the 61 m / 10 kW case, (ii) points where the literature disagrees with or refines the "what I already have" list above, and (iii) a not-accessed list.

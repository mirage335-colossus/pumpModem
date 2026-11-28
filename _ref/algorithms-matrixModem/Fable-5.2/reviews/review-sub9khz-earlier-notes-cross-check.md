# Earlier sub-9 kHz notes: inconsistencies and gaps found on re-reading

27 November 2026 · Three delegated read-only extractions of the repository's own sub-9 kHz research, made for the [sub-9 kHz FSK rate and spacing note](../sub9khz-fsk-rate-and-spacing.md) · no web access, no file edited

**Status.** Before the rate and spacing work, three delegated agents re-read the earlier sub-9 kHz
notes and returned literal extractions of their antenna, propagation and noise findings. The
extractions repeat what those notes already say and are not kept. What they added is kept here, as
returned: the places where the earlier notes disagree with themselves or with each other, the
premises that turned out wrong, and what was asked for and is not in the notes. It was written into
the repository on 28 November at the user's request. Nothing in the earlier notes was changed;
this file is the cross-reference that the
[research-material rule](../../../../docs/development.md#research-material) asks for.

**How to read it.** Citations are `key:line`, with line numbers as the files stood on 27 November
2026 before that day's edits to `LAT` and `MMR`. "(my note)" and first-person remarks are the
extracting agent's. Evidence tags such as [E], [P], [G] and [T] are the earlier notes' own.
None of the items was re-verified against its source for this file.

**What was acted on.** Two items changed the rate and spacing note: the railway-supply harmonic
near 8,266.7 Hz (noise item 5) is now a stated weakness of the 12.5 Hz slot there, and the
correction that the Sub9 presets live in `src/tuning_presets.cpp` and not in the appraisal
simulator (propagation, premise 1). The rest is recorded for whoever next revises those notes.

| Key | File |
| --- | --- |
| F00 | [`00-method-and-modem-context.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/00-method-and-modem-context.md) |
| F10 | [`10-sub9khz-propagation.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md) |
| F11 | [`11-sub9khz-antennas-links-and-practice.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md) |
| F12 | [`12-sub9khz-noise.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/12-sub9khz-noise.md) |
| F81 | [`81-scenarios-sub9.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/81-scenarios-sub9.md) |
| F85, F86, F90 | [`85-scenarios-noise-and-equipment.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md), [`86-statistics-and-calculation-checks.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/86-statistics-and-calculation-checks.md), [`90-cross-check-calculations.md`](../../../highFidelity-triple_Robust_Modem/Fable-5.2/90-cross-check-calculations.md) |
| A10, A50 | [`Astra-6/10-sub9khz-antenna.md`](../../../highFidelity-triple_Robust_Modem/Astra-6/10-sub9khz-antenna.md), [`Astra-6/50-noise-interference-and-frontends.md`](../../../highFidelity-triple_Robust_Modem/Astra-6/50-noise-interference-and-frontends.md) |
| LAT, MMR | [`fsk-lattice-design.md`](../fsk-lattice-design.md), [`README.md`](../README.md) of this directory |
| OWS, OGM | [`review-weak-signal-modes.md`](../../Opus-5.5/reviews/review-weak-signal-modes.md), [`review-gemm-decoding.md`](../../Opus-5.5/reviews/review-gemm-decoding.md) |
| AM, AS | [`models.md`](../../../../_appraisal/highFidelity-triple_Robust_Modem/models.md), [`src/scenarios.cpp`](../../../../_appraisal/highFidelity-triple_Robust_Modem/src/scenarios.cpp) of the earlier appraisal |
| CM, TP, TH | [`docs/channel-models.md`](../../../../docs/channel-models.md), [`src/tuning_presets.cpp`](../../../../src/tuning_presets.cpp), [`include/datapump/tuning.hpp`](../../../../include/datapump/tuning.hpp) |

## 1. Antenna notes (F11, A10)

### 1.1 What they do and do not contain for this question

- **Almost all numbers are in F11.** A10 gives no capacitance, R_r, loss, Q, bandwidth, voltage or link-budget figure for the 61 m antenna. It says its calculations are "scale checks and scenario guidance, not a link budget or antenna qualification" (A10:5).
- **Nothing is tabulated for 10 kW input into the 61 m antenna.** Table B stops at 10 W / 100 W / 1 kW. The only "10 kW" in either file is the 1,000–1,500 m balloon cable radiating 10 kW at 21 kHz (F11:301).
- **The antenna is voltage-limited, not power-limited, well below 10 kW in most modelled cases.** At 8.27 kHz the T is marked voltage-limited ("V") at 100 W in S1 and at 1 kW in S2/S3 (F11:239–241); only the lossiest case S4 is not V at 1 kW (F11:242). No explicit threshold power is stated anywhere.
- **Modelled −3 dB bandwidth is 6–620 Hz** (F11:55), from B = f/Q_L with Q_L = X_C/R_total. For the T at 8.27 kHz it is 18.5 / 57.1 / 173.9 / 458.3 Hz for S1–S4 (F11:266).
- **Every 61 m figure is [E]** (own estimate), from an assumed geometry and assumed loss cases. F11 calls it "a reproducible sensitivity case, not a source-identified or field-validated antenna" (F11:255).
- **No keying analysis exists for this antenna.** F11 evaluates bandwidth only against the modem's 0.0625 Hz signal and 60 s chips, and concludes it "never binds" (F11:54). It gives no tone-spacing or tone-rate rule; the nearest hook is the time constant Q_L/(πf) of 0.5–53 ms being "relevant to any fast keying" (F11:735).

### 1.2 Inconsistencies and things to treat with care

**Within F11**
1. **"About 160 mW at 100 kV"** (F11:631) and "5–160 mW" (F11:8) are labelled generically as "61 m top-loaded". Table C shows 164 mW at 100 kV is the umbrella; the T at 100 kV is 66 mW (F11:279–280).
2. **Adverse bandwidth row** "50; 150–175 Hz" (F11:633) does not match every S3 row of Table B: bare wire S3 at 8.27 kHz is Q 60, 138 Hz (F11:237), and T S3 at 2.97 kHz is Q 61, 49 Hz (F11:248).
3. **Auburn shortfall** is quoted as "about 10–22 dB" in efficiency (F11:8, F11:297) and as "7 and 17 dB" in field (F11:771; 17.2 and 7.2 dB at F11:297). These are different estimators, not a contradiction, but easy to conflate.
4. **"Table D" and "Table E"** are cited (F11:93, F11:129) but neither appears as a labelled table; only bullet values are given.
5. **[P20] and [P53]** are the same Sensors 2022 paper under two keys; [P19] and [P54] likewise (F11:907–908, F11:941–942).
6. **No [P56]** exists in the reference list (keys jump from P55 to P57).
7. **Static bandwidths:** 33–70 Hz for operational stations (F11:56) versus 160 Hz for ETAM (F11:116), both from [T6]; they refer to different antennas, and F11 does not explain the relation.

**Between F11 and A10**
1. **Chu (1948):** A10 cites MIT RLE Technical Report 64 as "Full text" (A10:134); F11 cites the J. Appl. Phys. paper as "Metadata" and says Chu was "verified as citations only" (F11:911, F11:807). The numerical floors agree in form (both use a ≈ 61 m) but are quoted at different frequencies: 6.6×10⁵ at 9 kHz (A10:109) versus 8.5×10⁵ at 8.27 kHz (F11:51).
2. **Northwest Cape at 15.5 kHz:** A10 gives Gobbel's design table (h_e 191.4 m, R_r 0.154 Ω) (A10:28); F11 gives NRL measurements (h_e 184 m, R_r 0.142 Ω) (F11:307). Different sources, design versus measurement.
3. **7.8 kHz loop:** "0.5 m diameter" and "approximately 1.2 kW" (A10:36) versus "51 cm-diameter" and "nominal 1,200 W" (F11:176).
4. **Gu et al. 2021:** "Full text" in A10 (A10:125) versus "Abstract (Crossref record)" in F11 (F11:935).
5. **P.372 edition:** A10's detailed statements refer to P.372-15 (2021) (A10:52–54); F11 uses P.372-18 (09/2026) (F11:885).
6. **Scope:** A10 says "No 2026 study was used as evidence" (A10:141); F11 cites a 2026 paper [P48].

## 2. Propagation notes (F10, F81, F00 and the presets)

### 2.1 Three corrections to the premise of the extraction

The brief assumed the Sub9 presets were described in the appraisal models and in the channel-model
guide.

1. **The "Sub9 best / moderate / worst" presets are not in AM or AS.** They are defined in TP:22–24 and described in F00:174–193. The appraisal simulator (AS) has different sub-9 scenarios: `vlf-resonant`, `vlf-detuned`, `vlf-phase-wander`, `vlf-phase-wander-wide-bound`, `vlf-varying-clean`, `vlf-phase-wander-hour` (AS:195–197, 311–387).
2. **CM has no sub-9 kHz preset section.** Grep for sub9, sub-9, VLF and 8.27 returns nothing there. It only describes the generic GUI two-path "changing-channel realization" that all spread presets use (CM:363–419).
3. **F86 and F90 contain no sub-9 kHz phase-stability, Doppler-spread or delay-spread measurements.** They hold only generic width/coherence conversions and scenario bookkeeping (see section e4 and the end of section d).

### 2.2 Internal inconsistencies and loose ends

1. **Time span of the 2 µHz figure.** F10:305 computes "23° in ≈9 h → 2.0 µHz", but F10:220 describes the same data as ranging "from midday to 03:30 UT", a longer span than ≈9 h; F10:177–178 mention 4 h windows between 11:30 and 18:30 UT by day and 16:30 to 03:30 UT at night. The ≈9 h basis is not explained.
2. **Rounding drift.** The summary says "≈70° in 2.5 h ... (≈20 µHz)" (F10:10); the tables say 71° and 22 µHz (F10:227, 306); the summary range is "2–22 µHz".
3. **Electric-field phase duration.** F10:307 uses "42° in 4.5 h"; F10:221 gives only "electric-field phase range 42°" with no duration.
4. **4.47 kHz daytime.** F10:222 quotes "no useful signal during daytime" plus a "20 degree nighttime phase lag", yet F10:285 reports a 48 h integration "after correcting a 20° day/night offset", which implies daytime data were combined. Not reconciled. F10:353 notes a similar tension for 2.97 kHz.
5. **"Measured differential delays (50 µs to 1.9 ms)"** (F10:10, 405). The 50 µs lower end is tagged [E; S1 Fig. 4, S] in F10:362, i.e. geometry and standard, not a sub-9 measurement. The nearest measured item is the sferic-image reading "≈+50–110 µs" (F10:180). The chapter's own geometry table has 46 µs at 2,000 km (F10:53).
6. **Preset multiples differ between chapters.** F10:405 says Sub9 best is "45–500 times the quiet-time rates"; F11:748 says "about 30–1,000 times the offsets and wander that the GPS-locked links met". Different baselines.
7. **Adverse amateur evidence missing from F10.** F11 has the 23–29 µHz nocturnal Doppler episode with S/N 5.2 → 1.2–2.0 in 46 µHz bins (F11:471) and W4DEX's 67–161° night-to-night shifts (F11:616, 660). Neither appears in F10's V4/V10/V11 tables. F10 says "night-to-night phase repeatable within 9°" (881 km), F11 says ≈±3° / within ≈6° (3,576 km): different paths.
8. **Precipitation-burst phase.** "5° in 1 s" (F10:317) is labelled "[T8; 5° assumed]". T8 as quoted gives only amplitudes (0.5–10 dB), so the 5° is an assumption.
9. **F81 profiles versus F10 measured profiles.** F81 night reference is h′ = 87, β = 0.30 (ITU planning pair) while F10 reports measured night 85.1 ± 0.4 / 0.63 ± 0.04 and says ITU's night β of 0.3 at 10 kHz is inconsistent with it (F10:197, 423). F81's day asymptote (74, 0.50) matches no single row of F10's V3 table. F81 labels both as declared diagnostic choices.
10. **LMP day/night phase at 1 Mm not compared.** F81:295 gives +18.105° (and −7.345° mid-transition); F10 gives 39–54°/Mm single-mode and 23–31° measured. The notes do not discuss this.
11. **TM2 excess attenuation** is quoted at two frequencies: ≈37/≈18 dB/Mm absolute at 8.27 kHz (F10:163) versus 34/17 dB/Mm more than TM1 at 8 kHz (F10:184). Not contradictory, just easy to conflate.
12. **Doc/code mismatch.** AM:34 lists only `vlf-resonant`, `vlf-detuned`, `vlf-phase-wander`; AS:195–197 also has `vlf-phase-wander-wide-bound`, `vlf-varying-clean`, `vlf-phase-wander-hour`.
13. **Two different "0.001 Hz" items.** The Sub9 best Doppler bound (TP:22) and the appraisal/F81 receiver frequency bound (`frequency_bound_hz = .001`, AS:333; F81:98–99) are separate things. The A1 physical phase law peaks at 0.436 mHz.

### 2.3 Not covered in the files read

- Allan deviation, phase structure function, or phase/Doppler power spectrum of a sub-9 kHz carrier.
- Rician K or specular/diffuse ratio.
- Doppler spread in the HF sense (fading bandwidth); only phase-rate "equivalent Doppler shifts" are given.
- Gravity waves and TIDs by name.
- Long-path, antipodal or round-the-world VLF multipath delay.
- An explicit count of propagating modes per frequency by day and night.
- Measured phase size of LEP or early/fast events.
- Quantitative storm statistics.
- Any terminator height-rate to Doppler calculation at 8.27 kHz.
- Any stated derivation or confidence level for the Sub9 best/moderate/worst preset values.
- Any sub-9 preset text in docs/channel-models.md.

## 3. Noise notes (F12, F11, and the lattice note)

### 3.1 Internal inconsistencies

1. P.372 at 8.27 kHz: F12 gives 147.8 / 174.7 dB (11 / 252 fT/√Hz; "148–175" in its summary). F11 gives "about 146–147" / "about 175" (9–10 / 260 fT/√Hz) and uses 146 dB for N2 and 174 dB for N5. Different reading methods (±1 dB trace vs ±2 dB by eye).
2. Harmonic wander at n=165: 3.3 Hz (F12:212, σ = 20 mHz estimate) vs 3.1 Hz (F12:306, 18.8 mHz measured). LAT:463 quotes "2.5–3.1 Hz". F11:620 uses a different statistic (±0.05 Hz → ±8 Hz).
3. F12:14 and F12:374 call the EbNaut 20 dB the "only figure" near 8 kHz. F11:460–465 records two more measured amateur results (≈23.5 dB at 6.47 kHz; ≈6–8 dB at 8.97 kHz with ≈60 % blanked) and summarises "6–23 dB" (F11:10, 621).
4. Blanked fraction 25–35 % (up to 60 %) in amateur practice vs ELF duty factors 0.02–0.07, ">95 % of samples kept", and a "<1 dB for duty factors below 0.1" blanking-loss formula. F12:373 calls this "probably pre-detection bandwidth and pulse duty factor, not a true conflict". Blanker thresholds also vary: ≈10× mean rectified (EbNaut), 12× (vlfrx-tools example), 1× or 1.2× mean (2010 tests).
5. Railway harmonics and 8270 Hz: F11:470 puts the 16.7 Hz families at ≈8266.6 Hz. By the notes' own numbers that is within about 3.4 Hz of 8270 Hz, while F11:620 says a 0.06 Hz signal "must sit at least tens of Hz from harmonic families". The notes do not reconcile this.
6. 60 Hz grids: by F12:214, 8270 Hz needs only −72.5 mHz to be crossed (vs +121 mHz on a 50 Hz grid). The "20 Hz above the 165th harmonic" rationale is 50 Hz-specific; the notes do not comment.
7. Lattice search volume: LAT:1134–1135 uses 0.28 mHz bins (7,200 for ±1 Hz; 36,000 for ±5 Hz). MMR:180–181 uses 0.14 mHz bins (12,960 for ±0.9 Hz; 72,000 for ±5 Hz), i.e. half-bin spacing. LAT:1179 revises the drift density to "about 2" per mHz. LAT:355 and 908 still say "1 ppm … 8 mHz" although LAT:1197 says "1 ppm at 9 kHz made 9 mHz throughout" (different carrier, so not numerically wrong).
8. C/N0 range of real links: LAT:426 "−15 to −37 dB-Hz" vs F11:10 "−15 to −47 dB-Hz"; the lower end is reached only by multi-night stacking (F11:652).
9. EbNaut W4DEX Eb/N0: page says −0.1 dB, arithmetic gives +0.1 dB (OWS:345–346). OGM:180 calls the rate-1/8 reading of "8k19a" "plausible but not verified"; F11:480 treats it as confirmed by the reported information rate.
10. Odd vs even harmonics: the only evidence is satellite abstracts and odd-harmonic notches at 950–4050 Hz; nothing on the ground near 8 kHz.

### 3.2 Asked for and not covered

- Mains harmonic amplitude relative to noise near 8 kHz; line width; sidebands; ground odd/even ratio near 8 kHz; cancellation depth near 8 kHz.
- An explicit statement of how wide a signal may be before it straddles a harmonic. Only "Four positions fit between two 50 Hz harmonics" (LAT:464) and the unvalidated "tens of Hz" inference (F11:620).
- Explicit reasons for choosing 5.17 kHz or 2.97 kHz.
- Sferic rate or cluster statistics inside 3–9 kHz; tweek occurrence or amplitude per harmonic; whistler occurrence or levels; chorus/hiss densities.
- Levels of switching-supply and similar interferers; any VLF station list near 9 kHz other than Omega 10.2 kHz.
- Sound-card sample rates and ADC details of amateur receivers; a quantified timing/frequency uncertainty for a typical GPS-PPS-disciplined sound-card receiver beyond the Todmorden figures (±100 ns, 30 ns RMS) and the DF6NM 0.25 mHz read-out error.
- The phrase "antenna roll-off".

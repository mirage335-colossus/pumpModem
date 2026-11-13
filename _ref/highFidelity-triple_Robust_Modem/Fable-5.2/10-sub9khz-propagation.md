# Propagation below 9 kHz

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** antennas, links and practice [11](11-sub9khz-antennas-links-and-practice.md); noise [12](12-sub9khz-noise.md); equipment [55](55-equipment-and-front-end-effects.md); independent arithmetic [90](90-cross-check-calculations.md).

This chapter covers the path only; antennas and link records are in [11](11-sub9khz-antennas-links-and-practice.md) and noise is in [12](12-sub9khz-noise.md). All web sources were accessed on 2026-11-13.

**Summary.** Below 9 kHz the path is an Earth–ionosphere waveguide only a few wavelengths high, so the channel differs in kind from HF. (1) Zones: quasi-static and induction terms dominate inside λ/2π (5.8 km at 8.27 kHz, 16 km at 2.97 kHz, 49 km at 970 Hz, 628 km at 76 Hz); ITU-R P.368 gives the 300·√P/d radiation formula 1 dB accuracy only for kr > 10 (58 km at 8.27 kHz); ITU-R P.684 recommends ray hops below 1,000 km and waveguide modes beyond. (2) Loss: about 0.5–2 dB/Mm at 76–100 Hz (0.5 dB/Mm derived from 82 Hz signals on January nights); 13–20 (day) and 5–10 (night) dB/Mm at 1 kHz; an absorption band at roughly 1.5–5 kHz; measured daytime 7–9 dB/Mm at 6 kHz and about 6 (east→west) or 3 (west→east) dB/Mm at 8 kHz; about 1.5–3 dB/Mm at night at 8–10 kHz. (3) Stability: on ~1 Mm paths at 8.3 kHz amateurs measured a midday-to-night carrier-phase excursion of only 23–31° in 4 h averages (about 2 µHz mean rate), but hourly phases that wandered by up to ≈70° in 2.5 h on a good winter night (≈20 µHz); night-to-night phase repeatable within 9°, constant-phase BPSK for 9.7 h over 6,194 km, and carrier lines resolved in 2.3–2.9 µHz after 4–5 days. Omega-band records (10.2–13.6 kHz, 8–10 Mm) show a day-to-day spread of about 7° (standard deviation) at a fixed hour, about 40°/Mm day–night shift and 30–120 µHz while the terminator crosses. Solar flares advance the phase by 13–16°/Mm per decade of X-ray flux at 21–25 kHz; at 10.2 kHz over 7.8 Mm typical events were 36–54° with a median onset rate of about 0.6 mHz; an extrapolation to 8.27 kHz, not tested by any measurement found, gives ≈24°/Mm per decade (0.2–7 mHz while M–X flares rise). (4) At night on short paths, ground wave and multi-hop sky waves give 10–40 dB fades and echoes 0.26–1.9 ms late. (5) The Sub9 presets bound Doppler at 1, 5 and 50 mHz: even the best exceeds the quiet-time phase rates (2–22 µHz) by about 45 to 500 times and sits at flare-onset rates, and the worst exceeds every rate found by about an order of magnitude or more. Their delays (5 µs, 50 µs, 5 ms) span the measured differential delays (50 µs to 1.9 ms) but are approximately frequency-flat across 0.06 Hz, while still allowing complex-gain changes and cancellation. Slow flat fading is within reach of the two-path family; diurnal level steps, terminator minima and flare steps are not represented.

Conventions used below. Evidence grades: [S] standard, [P] research paper or preprint (peer-review status checked separately) paper, [T] government/laboratory report or PhD thesis, [B] book, [G] grey literature, [E] this chapter's own estimate (formula shown). Access status: "read" (supporting passage or figure read in full text), "abstract", "secondary" (via the named source). "Read from graph" means a value read off a printed curve; treat as ±10–15 %. Source keys such as [S1], [P6], [G4] refer to the reference list at the end. Constants in the arithmetic: c = 299,792 km/s, Earth radius a = 6,371 km.

## Physical mechanisms

### M1. Field zones of an electrically tiny vertical radiator

A 61 m vertical is λ/594 at 8.27 kHz and λ/1,655 at 2.97 kHz, so its field is that of a vertical electric dipole on a conducting plane. The power-law terms are the quasi-static (1/r³), induction (1/r²) and radiation (1/r) terms; they are equal at kr = 1, i.e. r = λ/2π.

- ITU-R P.368-10 states the radiation reference used throughout LF/VLF engineering: a short vertical monopole on perfectly conducting ground radiating 1 kW gives 300 mV/m at 1 km (cymomotive force 300 V), i.e. E = 300·√P_kW/d_km mV/m [S2, Annex 1; also S1 eq. (1) and footnote 1; read].
- P.368-10 Annex 1 NOTE 3: the method "gives the total field strength at distance r with an error less than 1 dB when kr is greater than about 10"; nearer in, the induction and static fields are included by adding 10·log10[1 − 1/(kr)² + 1/(kr)⁴] dB [S2; read]. Evaluated [E]: +40.0 dB at kr = 0.1, +27.8 dB at 0.2, +11.1 dB at 0.5, 0 dB at 1, −0.9 dB at 2, −0.45 dB at 3, −0.04 dB at 10.
- Validity limits of the 300·√P/d formula: P.368-10 is defined only for 10 kHz–30 MHz, gives only "the radiation field, which would be measured in the far-field region", and is to be used "only when it is known that the amplitude of ionospheric reflections are negligible" [S2, recommends 2 and Annex 1; read]. Below 10 kHz there are no ITU ground-wave curves. The formula remains the inverse-distance reference to which the waveguide factor of M2 is applied.

Zone boundaries [E: λ = c/f; r = λ/2π; r(kr = 10) = 10λ/2π]:

| f | λ (km) | λ/2π (km) | kr = 10 at (km) |
|---|---|---|---|
| 30 Hz | 9,993 | 1,590 | 15,900 |
| 76 Hz | 3,945 | 628 | 6,280 |
| 300 Hz | 999 | 159 | 1,590 |
| 970 Hz | 309 | 49 | 492 |
| 1,970 Hz | 152 | 24 | 242 |
| 2,970 Hz | 101 | 16 | 161 |
| 5,170 Hz | 58 | 9.2 | 92 |
| 8,270 Hz | 36.3 | 5.8 | 58 |
| 8,970 Hz | 33.4 | 5.3 | 53 |

A third, waveguide-specific zone exists at ELF. Once the range exceeds roughly the guide height h, evanescent higher modes have died away and the zero-order mode carries the field even where kr < 1. Wait gives, for a vertical dipole between perfectly conducting planes and "a distance which is large compared to h", E_z = (μ0·ω·I·ds/4h)·H0⁽²⁾(kρ) [P1 eq. (2.1); read]. For kρ ≪ 1 the companion magnetic field tends to H_φ ≈ I·ds/(2π·h·ρ) [E: small-argument Hankel function], i.e. it falls as 1/ρ, not as the free-space induction 1/ρ². This zone exists only if λ/2π > h, i.e. f < c/(2πh) ≈ 680 Hz for h = 70 km and 550 Hz for h = 87 km [E]. Above about 1 kHz the wave zone begins before the guide height is reached.

### M2. From ground wave plus sky-wave hops to waveguide modes

- ITU-R P.684-8 §1: two descriptions exist. The wave-hop method (ground wave plus one or more ionospheric reflections) "should be used at LF and, for distances less than 1 000 km, at VLF"; the waveguide-mode method "should be used at VLF for distances greater than 1 000 km"; inside 1,000 km the mode series converges slowly and "propagation at ELF also may be described in terms of a single waveguide mode" [S1 §1.1–1.3; read].
- Reference heights in P.684-8: 70 km "typical daytime" and 90 km "typical night-time" [S1 §2.2.1; read].
- Hop-versus-ground-wave differential delay (delay spread at short range) follows from geometry. P.684-8 Fig. 4 gives it for h = 70 and 90 km; the worked example in Annex 1 reads 46–47 µs (h = 70) and 67 µs (h = 90) for one hop at 1,911 km [S1 Annex 1 stages 05 and 10; read]. The spherical-Earth geometry L_n = 2n·[a² + (a+h)² − 2a(a+h)·cos(d/2na)]^½, Δt = (L_n − d)/c [E], reproduces those two values (45.9 and 66.9 µs at 2,000 km) and gives:

| d (km) | h = 70 km: 1 / 2 / 3 / 4 hops (µs) | h = 87 km: 1 / 2 / 3 / 4 hops (µs) |
|---|---|---|
| 100 | 241 / 659 / 1,107 / 1,564 | 337 / 875 / 1,440 / 2,012 |
| 172 | 169 / 524 / 941 / 1,381 | 245 / 723 / 1,261 / 1,819 |
| 300 | 109 / 372 / 724 / 1,121 | 162 / 536 / 1,011 / 1,530 |
| 500 | 73 / 252 / 517 / 842 | 109 / 374 / 751 / 1,197 |
| 1,000 | 50 / 146 / 299 / 503 | 72 / 218 / 447 / 747 |
| 2,000 | 46* / 100 / 181 / 291 | 64 / 143 / 267 / 435 |

(* one hop is beyond its geometric-optics limit; the wave is diffracted.) Hop amplitudes depend on the reflection coefficient, which P.684-8 tabulates against f·cos i and which is much larger at night than by day [S1 §2.2.5, Fig. 10; read, values not transcribed].

- Mode sum. For a flat guide with a perfectly conducting ground and a "magnetic-wall" ionosphere Wait gives E_z ≈ [η·I·ds/(h·(λρ)^½)]·e^{iπ/4}·Σ_n S_n^{3/2}·e^{−ikS_nρ}, with 2hC_n = (n − ½)λ; for an "electric-wall" ionosphere the resonance is 2hC_n = nλ and a zero-order (TEM) mode exists [P1 eqs. (2.3)–(2.8); read]. Dividing by the free-space reference E0 = η·I·ds/(λρ) gives the engineering form for one mode [E]:
  E = (300·√P_kW/d)·(√(λd)/h)·|Λ_n|·[(d/a)/sin(d/a)]^½·10^(−α·d/20) mV/m,
  i.e. cylindrical spreading (field ∝ d^−½), a spherical focusing term, an excitation factor Λ and an attenuation rate α. Wait's introduction records the same (sin d/a)^−½ spreading with exponential decay for the low-attenuation modes [P1 §1; read].
- ITU definitions for each mode with eigenangle θ_n: phase velocity V = c/[K·Re(sin θ_n)], attenuation α = −8.6859·k·Im(sin θ_n) dB/Mm [S1 eqs. (39)–(40); read].

### M3. Which modes exist below 9 kHz

Ideal cut-offs f_n = n·c/2h [E] (the real, diffuse D region shifts them a little):

| h (km) | n = 1 | 2 | 3 | 4 | 5 (kHz) |
|---|---|---|---|---|---|
| 70 (day) | 2.14 | 4.28 | 6.42 | 8.57 | 10.71 |
| 72.8 (day, mid-latitude) | 2.06 | 4.12 | 6.18 | 8.24 | 10.30 |
| 74 (day, ITU) | 2.03 | 4.05 | 6.08 | 8.10 | 10.13 |
| 85.1 (night) | 1.76 | 3.52 | 5.28 | 7.05 | 8.81 |
| 87 (night, ITU) | 1.72 | 3.45 | 5.17 | 6.89 | 8.61 |
| 90 | 1.67 | 3.33 | 5.00 | 6.66 | 8.33 |

- Below the first cut-off only the quasi-TEM (QTEM) mode propagates. It has no cut-off, but its attenuation rises steadily with frequency, so that on a modelled 1,960 km night path it contributes nothing significant above ∼1.2 kHz [T4 §2.7.1; read]. Reising gives the same picture: cut-offs "at integer multiples of ∼1.8 kHz" for an 80–85 km night guide, and QTEM signals "strongly attenuated above ∼1 kHz" [T5 ch. 1; read].
- Each cut-off carries a quasi-TE and a quasi-TM mode. In the night-time model the first mode appearing above ∼1.6 kHz is QTE; near cut-off the QTE fields far exceed the QTM fields because the QTM modes are extremely attenuated there; QTM1 contributes only above ∼2.5 kHz; the two interfere to a near-null at ∼3 kHz; the second-order QTE mode enters at ∼3.3 kHz; above 10 kHz five or more modes have similar phase velocities [T4 §2.7.1; read]. Cohen quotes first- and second-order cut-offs of ∼1.8 and ∼3.6 kHz and concentric interference rings in modelled ground fields at 5 kHz [T6 ch. 4; read].
- A night-time spectrogram from Todmorden (UK) shows the cut-off ladder directly as horizontal bands at multiples of about 1.7 kHz up to beyond 12 kHz [G3, image 150822_sferic_modes_EW_25a.png; read; reading of the image].
- Coincidences worth knowing [E, from the table]: 8.27 kHz lies within 4 % of the fourth daytime cut-off; 8.97 kHz lies 2–4 % above the fifth night-time cut-off; 5.17 kHz sits on the third night-time cut-off; 6.47 and 4.47 kHz sit just above the third and second daytime cut-offs. Near-cut-off modes are steep-incidence, heavily attenuated and slow, so they matter only at short range (M5).
- Why there is an absorption band. At ELF the ionosphere behaves as an electric wall and the TEM mode has loss rising with frequency; at VLF it behaves "more like a magnetic wall", and attenuation "decreases with increasing frequency in the range from about 2 to 18 kc and thereafter increases", a Brewster-angle-like effect [P1 §1; read]. The "predicted absorption band at frequencies around 3 kc/s was confirmed" in atmospheric-noise spectra [T2 §3; read]. Johler and Berry add that "in the absorption region between VLF and ELF, the abnormal modes [generated by the geomagnetic field] may become dominant beyond 1000 km" [T3 abstract; read].
- Excitation. For mode 1 at 8 kHz the excitation factor is 0 to +1 dB over perfect ground and rises to +2 to +3 dB for ground of 0.2–1 mS/m [T1 Fig. 33; P2 Fig. 7; read from graph]. Height gain is irrelevant for ground-based terminals a tiny fraction of a wavelength high.

### M4. Non-reciprocity and ground conductivity

- The geomagnetic field makes attenuation direction-dependent: "attenuation is lower for propagation from west-to-east than from east-to-west" [T1 §7 item c; read]. Galejs computes the same sense at ELF near the first Schumann resonance: 0.022 dB/Mm west→east against 0.165 dB/Mm east→west at 8 Hz [P4 abstract and §6; read].
- Finite ground conductivity raises attenuation and lowers phase velocity; "ground conductivities less than 1 millimho/m lead to prohibitively high attenuation rates in the daytime", and "frequencies of the order of 10 kc/s may be more adversely affected by very-low conductivity than frequencies of the order of 30 kc/s" [P2 abstract and §3; read]. ITU reference grounds: sea 5 S/m, land 2 mS/m, polar ice 0.025 mS/m [S1 Table 1; read]. P.684-8 Annex 3 shows the Greenland ice cap as a field-strength anomaly at 24 kHz [S1 Annex 3; read].

### M5. Phase velocity, group velocity and dispersion

- Ideal guide: v_g,n = c·[1 − (f_cn/f)²]^½ and v_p,n = c/[1 − (f_cn/f)²]^½ [T4 §2.1; B-level textbook result]. Excess group delay over the speed-of-light delay is (1/c)·{[1 − (f_c/f)²]^−½ − 1} per unit distance [E]: 13.6 ms/Mm at f = 1.02 f_c, 7.6 at 1.05 f_c, 4.7 at 1.1 f_c, 1.14 at 1.5 f_c, 0.52 at 2 f_c, 0.20 at 3 f_c, 0.075 at 4.8 f_c. This is the tweek dispersion law: a sferic's late energy arrives as tones descending towards each cut-off, with spacing 2h/c between late pulses [T4 §2.1; read].
- In the real guide mode 1 is faster than light below a crossover: "the phase velocity ratio v/c of mode 1, in daytime, is greater than one for frequencies less than about 14 kc/s ... at nighttime, the cross-over point is more like 10 kc/s" [T1 §7 item g; read].
- The QTEM mode is slower than light: phase velocity "∼0.9 c" [T5; read], c/v ≈ 1.2–1.3 at 10–100 Hz falling to ≈1.1 near 1 kHz [P5 Fig. 8b; read from graph].
- Within a 0.06 Hz signal none of this dispersion is resolvable: for mode 1 at 8.27 kHz at night the group-delay slope is about −19 µs per kHz per Mm [E: dτ/df = −(d/c)·(f_c²/f³)·(1 − f_c²/f²)^−3/2 with f_c = 1.72 kHz]. Dispersion matters only through the relative phase of the modes or rays, i.e. as flat fading.

### M6. What makes the path vary

Everything is controlled by the height and sharpness of the lower edge of the D region, described by Wait's two parameters: N(z) = 1.43×10¹³·exp(−0.15·H′)·exp[(β − 0.15)(z − H′)] m⁻³ [P9 §1; S1 eq. (49); read]. By day solar Lyman-α (above ~70 km) and galactic cosmic rays (below) maintain it; at night scattered Lyman-α does [P10 §1; P13 §1; read]. Changes of H′ and β arise from solar zenith angle (diurnal, seasonal, latitude), the day–night terminator (where modes convert into one another), solar-flare X-rays (minutes to hours, sunlit paths only), energetic-electron precipitation (lightning-induced bursts lasting seconds to minutes; substorm and storm precipitation lasting hours to days, mainly at higher latitudes), solar proton events (polar cap, days), eclipses, and the mesospheric seasonal cycle. A phase-velocity change Δ(v/c) over distance d changes carrier phase by Δφ = 360°·(d/λ)·Δ(v/c) [E].

### M7. Through-the-earth and "earth-mode" links

- Skin depth δ = (π·f·μ0·σ)^−½ = 503/√(σ·f) m [B-level formula; E]; ITU-R P.527-6 §3 defines penetration depth as the 1/e field depth and plots it against frequency for sea water, wet and dry soil and ice [S3 §3 and Fig. 1; read]. Evaluated [E]:

| σ (S/m) | 30 Hz | 76 Hz | 300 Hz | 1 kHz | 2.97 kHz | 8.27 kHz |
|---|---|---|---|---|---|---|
| 5 (sea) | 41 m | 26 m | 13 m | 7.1 m | 4.1 m | 2.5 m |
| 10⁻² (wet soil) | 919 m | 577 m | 291 m | 159 m | 92 m | 55 m |
| 10⁻³ (average rock) | 2.9 km | 1.8 km | 919 m | 503 m | 292 m | 175 m |
| 10⁻⁴ (dry/crystalline) | 9.2 km | 5.8 km | 2.9 km | 1.6 km | 924 m | 553 m |

- At ranges of a few skin depths or less, an earth-electrode pair behaves as a grounded current dipole in the quasi-static regime; potentials and fields fall as the inverse cube of distance. The amateur description is the same: "signals thus propagated attenuate rapidly with distance (inverse cube attenuation rate)" [G9; read].
- At ranges of many skin depths the direct path through the ground is extinguished exponentially (8.7 dB per skin depth) and the surviving signal is the lateral, "up-over-and-down" wave: up to the surface, along it in air, and down to the receiver. No primary source for the lateral-wave formulas was obtained (Wait's and Bannister's quasi-static papers, and King, Owens and Wu's monograph, are [recollection, unverified]).
- A ground dipole (earth-electrode antenna) is also a legitimate radiator into the waveguide: it launches TM modes end-fire and TE modes broadside [S1 Table 2 and Fig. 19 define the V, E and B exciters; read]. This is how amateurs radiate at 0.2–8.3 kHz with 0.5–1.3 km earth dipoles [G1].

## Measured and reported values

### V1. Attenuation rate against frequency

| Frequency | Day (dB/Mm) | Night (dB/Mm) | Conditions and source |
|---|---|---|---|
| 8–20 Hz | 0.022 W→E and 0.165 E→W at 8 Hz; 0.16–0.32 at 20 Hz | 0.2–0.3 at 20 Hz | Theory, QTEM [P4 abstract; Fig. 11 read from graph] |
| 76–82 Hz | 1.3 (the value assumed in the prediction that matched the measurements) | 0.5 (January, derived from the 82 Hz field strengths at 3.2–18.3 Mm); ≈1 in March/April (Bannister 1993, 1997, as cited in [P21]) | Kola Peninsula transmitter at 82 Hz received at Søndrestrømfjord, Dunedin and Arrival Heights in January 1990; night c/v ≈ 1.09 assumed; 76 Hz WTF/MTF field strengths agree on the same curves [P21 "Analysis"; read] |
| 100 Hz | 1.2–1.3 (theory); ≈1.6–2 (sferics) | 0.9–1.2 (theory); ≈1.1 (sferics) | Theory [P4 Fig. 11]; sferic-derived values of Chapman & Macario (1956) and Jean et al. as replotted in [P5 Fig. 8a]; both read from graph; secondary for the sferic data |
| 200 Hz | 2.7–3.0 | 1.5–2.6 | Theory [P4 Fig. 11; graph] |
| 500 Hz | 6.5–7.5 | 3–6 | Theory [P4 Fig. 11; graph] |
| 1 kHz | 13–14 (theory); ≈17–20 (sferics) | 4.6–9 (theory); ≈7–10 (sferics) | [P4 Fig. 11]; Chapman & Macario via [P5 Fig. 8a]; graph |
| 2 kHz | 25 to >50 | 7–15 | Theory, QTEM [P4 Fig. 11; graph] |
| 2.4 kHz | night value + ≈6 | — | HAARP 2,375 Hz, Kodiak (661 km) and Midway (4,466 km), 1 March 2007; inferred from a 9 dB relative change while the terminator moved 1.5 Mm; the author calls it "not unreasonable", not a calibrated rate [T6 App. A; read] |
| ≈3.5 kHz | ≈5 (low reliability) | — | Sferics, see next row; the author warns that values below 5 kHz are affected by "limitations in the recording and analysis" [P3 Fig. 7 and §3.2; read] |
| 5–6 kHz | 9–10 E→W (peak of curve); the abstract gives about 7 to 9 at 6 kHz | — | Sferics recorded at Boulder, Salt Lake City, Palo Alto and Maui, summer 1958, 1,000–10,000 km, 74–82 % sea water [P3 abstract, §3.2 and Fig. 7; read] |
| 7 kHz | ≈7.8 E→W; ≈4.9 W→E | — | [P3 Figs. 7 and 12; graph] |
| 8 kHz | ≈6.0 E→W; ≈3.1 W→E (measured). Theory: 6.4 E→W, 4.6 no field, 3.4 W→E | Theory: ≈3.3 E→W, 2.3 no field, 1.75 W→E (h = 90, β = 0.3); 2.0 (β = 0.5, isotropic) | Measured [P3 Figs. 7, 12; graph]. Theory: mode 1, sea, h = 70 km, β = 0.3 [T1 Fig. 31a; graph]; night [T1 Figs. 31a, 11a; graph] |
| 8.8 kHz | — | ≈1.5 (W→E, North Atlantic) | Estimate from an amateur reception: 0.12 fT (36 nV/m) at 6,194 km for a stated ≈150 µW ERP, compared with the unattenuated single-mode field of 107 nV/m from the M2 formula with h = 87 km: 9.5 dB over 6.19 Mm [E on G5]. ERP uncertainty of ±3 dB changes this by ±0.5 dB/Mm |
| 10.2 kHz | 2.5 W→E; 3.4 N–S; 4.1 E→W | 1.6 N–S; non-reciprocal ±0.5 | Omega-era measurements over sea, summer, temperate latitudes (Swanson, NEL Report 1239) quoted in [T1 §8 "Added Note"; secondary] |
| >10 kHz | "about 1 to 3" | minimum near 12 kHz | [P3 abstract]; day minimum 18 kHz, night ≈12 kHz [T1 §7; read] |

Rules of thumb in the secondary literature are consistent with the table at 8–9 kHz but not below: "0.5 dB to 8 dB per 1000km, depending on frequency, time of day, land or sea path, and direction ..." [G2; read]; "typically ∼2–3 dB/1,000 km" for sferics [T5; read].

East–west asymmetry measured by day: E→W exceeds W→E by 2.7 dB/Mm at 3 kHz, 3.8 at 5 kHz, 2.5 at 6 kHz, 3.0 at 7.5 kHz, falling to ≈1 dB/Mm at 10 kHz and above [P3 Fig. 12; graph]; "about 3 decibels per 1,000 kilometers less [W→E] for frequencies lower than 8 kilocycles" [P3 abstract; read].

Ground conductivity, mode 1 at 8 kHz, day (h = 70 km, β = 0.3) [P2 Fig. 6; read from graph]: W→E 3.4 (perfect ground), ≈3.9 (10 mS/m), ≈4.5 (2 mS/m), ≈5.3 (1 mS/m), ≈6.6 (0.5), ≈7.6 (0.3), ≈8.6 (0.2 mS/m); E→W ≈6.4 (perfect), ≈8 (1 mS/m), ≈9.5 (0.5 mS/m), >10 (0.3 mS/m). The gap-filling model below gives the same order for 1 mS/m ground (relative permittivity 15): +1.8 dB/Mm by day and +1.5 dB/Mm at night at 8 kHz, +1.6 and +1.3 dB/Mm at 6 kHz, with phase velocity lower by 0.12–0.15 % [E model].

Gap-filling model [E]. No source obtained gives day and night rates between 2 and 8 kHz for both polarisations. The wave equations were therefore integrated through Wait's isotropic exponential conductivity profile, ω_r(z) = 2.5×10⁵·exp[β(z − H′)] s⁻¹, n² = 1 − iω_r/ω, with the Earth-flattening term 2z/a, starting from an upgoing wave at the top and solving the ground boundary condition for the eigenvalue S (TM: dH/dz = −ik·n²·E, dE/dz = −ik·[(n² + 2z/a − S²)/n²]·H; TE: d²E/dz² = −k²(n² + 2z/a − S²)E; α = −8.686·k·Im S). Check against published anchors: mode 1, h = 60 km, β = 0.5: 6.5, 3.0, 1.8 dB/Mm at 8, 12, 20 kHz against ≈6.3, 2.9, 1.85 read from [T1 Fig. 11a]; mode 2, h = 70 km at 24 kHz: 5.3 against ≈5.0; day at 200 and 500 Hz: 2.2–3.5 and 5.6–8.6 against 2.7–3.0 and 6.5–7.5 from [P4]; day at 1 kHz: 12–19 against 13–20. The model has no geomagnetic field (so no east–west asymmetry and no TE–TM coupling) and no ions, and at night the real ionosphere is strongly anisotropic, so the night column is indicative only. A separate implementation of the same equations, written for the review of this chapter (same profile, Earth-flattening term and perfect ground), reproduced every value spot-checked to within about 4 %: day TM for both profiles from 0.2 to 10 kHz, day TE1, and night TM1, TE1 and QTEM; it also gave 4.9 dB/Mm and v/c − 1 = 0.91 % for mode 1 at 8 kHz with h = 70 km, β = 0.3, against 4.6 dB/Mm and 0.9 % in [T1 Figs. 31a and 32] [E]. This checks the arithmetic, not the physics: both share the model's omissions.

| f (kHz) | Day TM (H′ 70, β 0.45 / H′ 74, β 0.30) | Day TE1 (same two profiles) | Night TM1 (H′ 85.1, β 0.63) | Night TE1 | Night QTEM |
|---|---|---|---|---|---|
| 0.2 | 2.2 / 3.5 | — | — | — | 1.2 |
| 0.5 | 5.6 / 8.6 | — | — | — | 3.1 |
| 1.0 | 12 / 19 | — | — | — | 7.0 |
| 1.5 | 21 / 31 | — | evanescent | evanescent | 12.5 |
| 2.0 | 32 / 45 | evanescent | 27 | 15 | 20 |
| 2.5 | 45 / 59 | 29 / 37 | 27 | 8.6 | 31 |
| 2.97 | 56–59 / 66 | 20 / 27 | 28 | 6.7 | 44 |
| 3.5 | 54 / 33 | 15 / 21 | 23 | 5.3 | — |
| 4.0 | 28 / 20 | 12 / 17 | 14 | 4.6 | — |
| 4.47 | 19 / 15 | 11 / 15 | 10 | 4.1 | — |
| 5.17 | 12 / 10 | 9.4 / 13 | 6.6 | 3.5 | — |
| 6.47 | 6.6 / 6.1 | 7.5 / 11 | 3.7 | 2.9 | — |
| 8.27 | 3.9 / 3.9 | 6.0 / 8.7 | 2.3 | 2.4 | — |
| 8.97 | 3.3 / 3.5 | 5.6 / 8.2 | 2.0 | 2.2 | — |
| 10.0 | 2.8 / 3.0 | 5.2 / 7.5 | 1.7 | 2.1 | — |

Reading: by day the lowest-loss vertically polarised mode exceeds 20 dB/Mm from about 1.3 to 4.3 kHz and peaks near 55–65 dB/Mm at 3 kHz; in the 74/0.30 profile the ELF QTEM branch and VLF "mode 1" are one continuous branch through that peak. Second-order TM modes at 8.27 kHz come out at ≈37 dB/Mm by day and ≈18 dB/Mm at night (compare ≥10–14 dB/Mm for h = 90 km in [T1 Fig. 31b]). The model exceeds Taylor's measured values at 5 kHz by 30–50 % and is far above his (unreliable) 3–4 kHz values; that disagreement is unresolved (see "Contradictions, open questions, and things not found").

### V2. Phase velocity, group delay and modal interference

| Quantity | Value | Conditions and source |
|---|---|---|
| (v/c − 1) of mode 1 at 8 kHz | +1.45 % (h = 60 km), +0.9 % (70), +0.55 % (80), +0.28 % (90) | Theory, β = 0.3, sea; nearly independent of propagation direction [T1 Fig. 32; graph] |
| (v/c − 1) at 10.2 kHz, measured | +0.34 % day, −0.06 % night | Sea paths, temperate latitudes, summer (Swanson via [T1 §8]; secondary) |
| Finite ground | sea-to-land difference ≈5×10⁻⁴ in v/c at 10.2 kHz | Pierce & Nath via [T1 §8]; secondary |
| Day–night phase change implied | ≈54°/Mm at 8.27 kHz for h 70→87 km (Δ(v/c) = 0.54 %); 39°/Mm for 74→87 km; 49°/Mm at 10.2 kHz from the measured 0.40 % | [E] Δφ = 360°·(d/λ)·Δ(v/c) with the two rows above |
| Sensitivity of mode-1 phase to height at 8.27 kHz | ≈5.5° per km per Mm by day; ≈2.7° per km per Mm at night | [E] slopes 0.055 %/km and 0.027 %/km read from [T1 Fig. 32] |
| QTEM phase velocity | ≈0.9 c; c/v ≈ 1.2 near 100 Hz, ≈1.1 near 1 kHz | [T5]; [P5 Fig. 8b; graph]; the model of V1 gives c/v = 1.15–1.20 at 200 Hz and 1.09–1.11 at 1 kHz for the two day profiles, and 1.09 and 1.06 for the night profile [E] |
| ELF group velocity of lightning impulses | ≈0.86 c by day; 0.90–0.91 c at the X-ray maximum of an M8.2 flare, rising 1–3 min after the X-ray flux, then ≈0.84 c for a while afterwards | Impulses from lightning 5,000–9,000 km away located by WWLLN, receiver band 0.03–1,000 Hz, Hylaty (Poland), 20 September 2023 [P22 §3–4; read] |
| ELF "slow tail" lag behind the VLF head | ≈3 ms at ≈12,000 km (≈0.25 ms/Mm) | Sferics from North America received at Palmer Station [T5 ch. 4; read] |
| Group path by day, two carriers 60 Hz apart near 8.27 kHz (the message does not state the two frequencies; "8.28/8.34 kHz" was not confirmed on re-check) | 1,000–1,068 km measured against 1,028 km great circle (stated error ±60 km) | Two carriers 60 Hz apart, alternated every 5 min, 10 µW ERP, Germany→Todmorden, 6 January 2015, 4 h windows between 11:30 and 18:30 UT [G4; read] |
| Same group path, at night | swings from 1,335 km (+1.0 ms) to 754 km (−0.9 ms) and back to 1,019 km | Same experiment, windows from 16:30 to 03:30 UT; the author attributes it to "multi-path interference ... the ground wave is still significant at 1 Mm and there will be several sky wave components" [G4; read]. His error estimate for the weaker signals is ±180 km (±0.6 ms), so the excursions are only 1.5–1.7 times the stated error |
| Echo delays at 172 km, 4.0–8.3 kHz, night | arrivals numbered 0–4 on the plot at path delays of about 170 (ground), 250, 400, 570 and 745 km, i.e. about 0.26, 0.76, 1.3 and 1.9 ms after the ground wave, with envelope maxima at or slightly beyond the numbered positions; first hop comparable to the ground wave, later hops progressively weaker | Stepped-frequency sounding, 4,025–8,275 Hz in 50 Hz steps, 31 August 2021, 19:30–22:29 UT, Germany; the delay axis already subtracts an estimated 0.1 ms transmitter group delay [G1 entry 2021-08-31; G3 image staircase_VLF3.png; reading of the plot]. The numbered positions coincide with flat-earth hop path lengths for a 90 km reflection height (249, 399, 567 and 740 km), so they are probably the operator's predicted positions; the envelope maxima fit ≈90–94 km in the hop formula. The 4.25 kHz sounding bandwidth resolves only about 0.24 ms, so ground wave and first hop are barely separated [E] |
| Hop delays in sferics, night | first hop ≈+50–110 µs, second ≈+200–400 µs at 500–1,000 km, following the predicted hop curves out to 2,000 km | Stacked sferic waveforms against range, Todmorden [G3 image 191207-n-all-h1.png; reading of the image] |
| Ideal-guide differential group delay between modes 1 and 2 | 0.26 ms/Mm at 8.27 kHz at night (h = 87 km); 0.40 ms/Mm by day (h = 72.8 km); 0.94 ms/Mm at 5.17 kHz at night | [E] from v_g,n = c·[1 − (f_cn/f)²]^½ |
| Same from the gap-filling model of V1 (group delay = d(f·Re S)/df ÷ c) | TM2 minus TM1 at 8 kHz: 0.40 ms/Mm by day (H′ 72.8, β 0.345), 0.29 ms/Mm at night (H′ 85.1, β 0.63); at 6 kHz: 1.7 and 0.84 ms/Mm; TE1 minus TM1 at 8 kHz: 0.08 and 0.06 ms/Mm | [E model]. Mode 1 itself is only 0.05 (day) and 0.04 (night) ms/Mm slower than light at 8 kHz, i.e. v_g ≈ 0.985–0.988 c |
| Mode-1 day–night phase-velocity difference, model | 0.41 % at 8 kHz (+0.75 % by day, +0.34 % at night), i.e. ≈39°/Mm; 0.30 % at 10 kHz, i.e. ≈36°/Mm | [E model]; compare the measured 0.40 % and 41°/Mm at 10.2 kHz (V4) |
| Mode-1/mode-2 interference period | ideal guide ≈525 km at 8.27 kHz (h = 87 km); model ≈430 km by day and ≈640 km at night at 8 kHz | [E] λ/(Re S1 − Re S2). In the model TM2 is 34 dB/Mm (day) or 17 dB/Mm (night) more attenuated than TM1, so the ripple it causes dies out within roughly 0.5 Mm by day and 1–1.5 Mm at night |

### V3. D-region parameters in use

| Condition | H′ (km) | β (km⁻¹) | Source |
|---|---|---|---|
| Day, "all latitudes, and all seasons" (planning value) | 74 | 0.3 | [S1 §4.4 and Annex 2; read] |
| Day, Sun near overhead | ≈70 | ≈0.45–0.5 | [P9 §1; read]. The classic pair 70 km / 0.45 km⁻¹ is attributed to Thomson (1993) in [T4 §3.2] |
| Day, tropical, midday, zenith angle ≈10° | 69.3 ± 0.3 | 0.49 ± 0.02 | NPM 21.4 kHz, 306 km path, Hawaii [P9 abstract; read] |
| Day, mid-latitude (dip latitude ≈52.5°), midsummer noon 2015 | 72.8 ± 0.2 | 0.345 ± 0.015 | DHO 23.4 kHz, North Sea paths [P10 abstract; read] |
| Day, Arctic, summer 2013 | 73.7 ± 0.7 | 0.32 ± 0.02 | Trans-polar paths; little zenith-angle dependence because particle precipitation dominates [P11 abstract; read] |
| Towards dawn/dusk (zenith angle 70–90°), low latitude | >80 | ≈0.30 or lower | [P9 abstract; read] |
| Dawn at mid-latitude | from ≈85 (night) "rapidly down to ∼73 km" at zenith angle 90°, back up to ≈78 at 75°, then down to the noon value | minimum ≈0.25 at zenith angle ≈75° (dawn) or ≈85° (dusk) | [P14 abstract; read]. No dawn minimum at low latitudes |
| Night, mid-latitude, solar minimum | 85.1 ± 0.4 | 0.63 ± 0.04 | Averages over several days; 10–41 kHz, long sea paths [P6 abstract; read]; confirmed on other non-equatorial paths [P7] |
| Night, planning value | 87 (polar 80) | "linearly with frequency from 0.3 at 10 kHz to 0.8 at 60 kHz" | [S1 §4.4; read] |
| Night, sferic-derived, central USA, July 1996 | 83.2 to 84.7 on adjacent paths | 0.49–0.51 | Spectral fits of sferics, with the fit criterion summed over 3–14 kHz (lower frequencies were excluded because they showed variations the model did not predict), stated precision 0.2 km [T4 §3.4; read] |
| Night, sferic-derived, eastern USA, July–August 2005, 00–05 LT, 59 nights | hourly means 82.0–87.2, mean 84.9, standard deviation 1.1 (260 h); largest change 1.3 km in 1 h and just over 4 km in 5 h; 19 nights changed by less than 0.5 km in 5 h; up to 2.0 km between regions ≈500 km apart | fixed at 0.65 | Fine spectral structure of sferics at 3–8 kHz from lightning 500–800 km away, fitted against an FDTD model; ±0.6 km for one sferic, ≈0.1 km for 5 min averages [P23; read] |
| Night, tweek-derived, low latitude (India), 2010, 21–02 LT | 83.5 (summer), 85.6–86.1 (equinox, winter); the tweek reflection height itself was 85–95 km, changing by up to 9 km from day to day and by 1–2 km within any hour | 0.60 (summer), 0.51–0.54 (equinox, winter) | First-mode cut-off frequencies of tweeks, near 1.6–1.8 kHz for these heights [E: c/2h], on five quiet days in each of three months [P24; read] |
| Night, Arctic | ≈79 (equinox) to 77 (winter) | 0.6 | [P12 abstract; read] |
| Trans-equatorial night paths | "often not well-modeled" by any realistic H′, β | — | Observed amplitudes several dB low, e.g. ≈7 dB on Omega Japan→Dunedin [P7 abstract; P6 §3; read] |
| Flare, X5 | ≈58 | rising towards ≈0.52 | McRae & Thomson (2004) as quoted in [P8 §1 and §5; secondary] |
| Flare, X45 (4 November 2003) | ≈53 | ≈0.57 | [P8 abstract and §5; read] |
| Moderate polar-cap absorption event | "reflecting height is lowered by about 10 km" | profile shape little changed | [T1 §2; read] |
| Total eclipse | +5 to +8 km (1999); +6 to +11 km (1965) | — | Clilverd et al. (2001); Crary & Schneible (1965), both via [T9 ch. 4; secondary] |
| Night-time semi-annual oscillation | ≤1.3–1.8 km equivalent | or 0.13–0.15 | Fitted to 3.3 and 4.2 dB peak-to-peak amplitude cycles at 45.9 and 19.8 kHz [P17 §3; read] |

The 2015–2025 measurements revise the classical single daytime pair in two ways: mid- and high-latitude daytime profiles are higher and less sharp than the tropical one, and the dawn transition is non-monotonic. The ITU planning values (day 74/0.3 for all conditions; night β tied to frequency) are not consistent with them, and ITU itself says the seasonal variation "cannot be predicted" by its model [S1 Annex 2; read].

### V4. Diurnal amplitude and phase, measured

| Path and frequency | Night minus day amplitude | Night minus day phase | Other features | Source |
|---|---|---|---|---|
| Omega Argentina→Dunedin, 8.7 Mm, sea, westward, 10.2 kHz, Feb–Mar 1996 | +13.8 dB | −358° (41°/Mm) | Transition occupies ≈9 h each way; day-to-day spread about ±2 dB and ±15° by day; a ≈40° step with a 5–8 dB notch at the end of the sunrise transition | [P6 Figs. 1–2; read; spreads and transition times are readings of Fig. 1] |
| Same path, 13.6 kHz | +6.6 dB | −338° (39°/Mm) | — | [P6 Fig. 3; read] |
| Omega La Réunion→Dunedin, 10.1 Mm, eastward, 13.6 kHz, 4–12 March 1996 | ≈+9 dB | ≈−395° (39°/Mm) | Night spread across nine nights ≈±30–40°; day spread ≈±15°; a ≈4–5 dB minimum lasting ≈1 h at sunrise | [P6 Fig. 4; reading of the figure] |
| Omega Hawaii→Forestport (New York), 7.8 Mm, 10.2 kHz, 17–24 May 1966 | — | diurnal change ≈100 cec (≈1 cycle, ≈100 µs) | Standard deviation about the eight-day mean curve ≈2 cec (≈7°, ≈2 µs) in hourly values; phase constant from about 06 to 09 UT while the whole path is dark; a slow "curvature" through the day at 10.2 kHz | [T11 Fig. 1 and text; read] |
| Germany→Todmorden, 1,028 km, two carriers 60 Hz apart near 8.27 kHz, 6–7 January 2015 | not reported (S/N 13–19 dB throughout) | carrier phases (4 h averages) ranged over 23° and 31° from midday to 03:30 UT | See V2 for the group-delay swings | [G4; read] |
| Same path, 8,270 Hz, night of 5–6 January 2015, 1 h averages every 30 min | S/N 5–14 dB in 278 µHz | magnetic-field phase −49° at 01:30 UT, −120° at 04:00 UT, back to −74° by 09:00 UT (window start times); electric-field phase range 42° | The operator judged the "phase is steady enough from 02:00 to 10:00" for an 8 h coherent average | [G12; read] |
| Germany→Todmorden, 881 km, 4,470 Hz, August 2017 | "no useful signal during daytime" | "20 degree nighttime phase lag" | 14.0 dB S/N in 23.1 µHz after combining 48 h | [G8, G1; read] |
| Germany→Todmorden, 881 km, 8,270.1 Hz, 11–14 November 2017 | nightly S/N −18.5, −16.1, −14.7 dB in 1 Hz | best-fit constant phase of each night 101.5°, 92.5°, 100.6° | A 12 h 14 min message each night, single-night Eb/N0 −4.4, −2.0 and −0.6 dB; decoded from two nights combined with "constant reference phase" (Eb/N0 +1.7 dB) | [G6; read] |
| Germany, 168 km, 5,170 Hz, 15–16 April 2022 | night level within about ±5 dB of the day level between fades | day: steady within ≈±10° for hours around noon apart from the flare step of V7, then drifting by 30–45° through the afternoon; night: wanders by tens of degrees | Three field components recorded, 600 s resolution; fades of 10–40 dB at night and at sunrise with phase reversals | [G1 entry 2022-04-15; G3 image dl3jmm_dl0ao_5k17_220415-16.png; reading of the plot] |
| VLF paths in general (context; the paper's own paths are at 21–25 kHz) | — | daytime phases "reproducible, in quiet conditions away from dawn and dusk, to a very few microseconds or better than about 10 degrees" | — | [P8 §1; read] |

The 1,028 km result (23–31°) is about half the single-mode estimate of V2 (≈40–55°); at 1 Mm the field is still a mixture of ground wave and several sky-wave modes, so the single-mode figure is not applicable there. The 9–10 Mm Omega results match it (39–41°/Mm). The 4 h averages also hide shorter wander: on the same path the night before, hourly magnetic-field phases moved by 71° in 2.5 h, while the electric-field phases moved by less, as expected when two components weight the arriving modes or hops differently [E, interpretation].

### V5. Terminator (sunrise and sunset) effects

- Long paths crossing the terminator show "clear successive minima in amplitude, known as modal minima", from modes generated where the D region changes rapidly with distance, "often accompanied by rapid phase changes known as cycle slips", particularly at dawn [P14 §1, citing Crombie 1964; read].
- Sizes seen in the records above: 4–8 dB deep, of the order of 1 h long, with a ≈40° step, at 10.2–13.6 kHz on 9–10 Mm paths [P6 Figs. 1 and 4; reading of the figures]; a deep (≥20 dB at 600 s resolution) minimum at local sunrise on the 168 km, 5.17 kHz path [G3 image; reading of the plot].
- Cycle slips at 10.2 kHz: the Liberia–Hawaii Omega phase difference received in Ohio lost one complete cycle each night over four days and did not return to the same value after 24 h, which the report attributes to modal interference on the Liberia signal (an east-to-west path from near the magnetic equator); the published correction tables did not predict it [T12; read]. Swanson gives the mechanism: if the dominant mode changes between day and night, the two modes must be equal somewhere in the transition, and a null and a cycle slip follow if they are then in opposition [T11; read]. A slip of one cycle per day is a mean offset of 11.6 µHz [E].
- Sferic spectra during sunset over a 1,960 km path: within about 40 minutes the 1.5–3 kHz band appears and modal-interference structure between 3 and 10 kHz develops, as H′ rises from 79 to 81.5 km; by day those spectral details "are absent" [T4 §3.4.2; read].
- HAARP 2,375 Hz at Midway strengthened relative to Alaskan sites as sunset advanced along the path (the ≈6 dB/Mm figure of V1) [T6 App. A; read].

### V6. Night-time variability

- "VLF propagation at night is often significantly more variable than by day", partly because the reflecting region is more variable and partly because more modes arrive with significant amplitude, so the resultant is more sensitive to small changes [P6 §1; read].
- Short night paths are proportionately more variable than long ones, "consistent with the view that the night D-region is significantly variable in time" with long paths averaging it out [P6 §3.7; read].
- Night-time amplitudes and phases on a 4.9 Mm North Atlantic path are "somewhat sensitive to magnetic activity" [P6 §3.3; read].
- Spatial structure: 1.5 km difference in H′ between two partly overlapping night paths over the central USA [T4 §3.4.1; read].
- Amateur transatlantic carrier at 8.82 kHz: "S/N variations of around +/-2 dB" between nights [G2 notes; read].
- Height statistics measured with sub-9 kHz signals: fits of the 3–8 kHz fine structure of night-time sferics over the eastern USA gave hourly mean heights with a standard deviation of 1.1 km over 260 h (July–August 2005), changes of up to 1.3 km in 1 h and just over 4 km in 5 h, oscillations with periods of tens of minutes to hours on some nights, and 19 of 59 nights with less than 0.5 km change in 5 h; on some nights the variability followed the local lightning rate, on others nothing identified [P23; read]. Low-latitude tweeks gave reflection heights changing by up to 9 km from day to day and 1–2 km within an hour [P24; read].
- What those height changes mean for a carrier [E]: with the night-time mode-1 sensitivity of V2 (≈2.7° per km per Mm at 8.27 kHz), 1.3 km in an hour is ≈3.5° per hour per Mm, i.e. ≈16 µHz on a 6 Mm path; with the ideal-guide sensitivity of the mode-1/mode-2 relative phase (≈17° per km at 1 Mm) it is ≈22° per hour at 1 Mm, the same order as the 28° per hour seen on the 1,028 km path in V4.
- No published statistics (probability distributions, spectra) of night-time fading or phase of a carrier below 9 kHz were found; the height statistics above are the closest.

### V7. Solar flares (sudden ionospheric disturbances)

- Phase advance and height lowering are "nearly proportional to the logarithm of the X-ray flux" with no saturation up to X45; sensitivity threshold about C1 [P8 §1; read].
- Phase sensitivity at 21–25 kHz: ≈130° per decade of flux on a fully sunlit 8 Mm path, ≈70° per decade on a partly sunlit 9 Mm path [P8 §3; read]; regression slopes equivalent to ≈150° per decade over 11,246 km at 21.4 kHz and ≈86° per decade over 5,667 km at 24.0 kHz [P15 Table 1, inverse of the phase coefficients 6.5–6.7×10⁻³ and 11.6×10⁻³ per degree; read; the inversion is this chapter's]. That is 13–16° per decade per Mm on the sunlit paths (13–14 over 11.2 Mm, 15 over 5.7 Mm, 16 over 8 Mm) and ≈8° on the partly sunlit 9 Mm path.
- Closest measured frequency, 10.2 kHz [T10; read]: 500 sudden phase anomalies on Omega signals received in New York in 1961–1970, mostly Hawaii→New York (7,814 km). Maximum phase offsets were most often 10–15 cec (36–54°, which the source equates to 1.2–1.8 km of phase-height change), with a mean of 23 cec (83°) for events above 5 cec; the median onset rate was 3.5 cec per minute (≈0.58 mHz, a fractional frequency shift of 6×10⁻⁸), the phase peaking in about 6 min; recovery was mostly at 0.5 cec per minute or slower, and durations clustered around 40–49 min. At a sunspot number near 85 about one SPA a day is observed "somewhere in the world", and near noon in active years SPAs dominate the phase perturbations beyond the 95th percentile.
- Scaling to 8.27 kHz [E]: H′ falls by ≈4.4 km per decade of flux (70 km to 58 km between C1 and X5, 2.7 decades [P8]); with the daytime slope 0.055 %/km of V2 this gives ≈24° per decade per Mm, about 1.5 times the 13–16° measured at 21–25 kHz. An M1 flare from a C1 background is then ≈24°/Mm, X1 ≈49°/Mm, X10 ≈73°/Mm, and the X45 lowering of 17 km [P8] ≈93°/Mm. No measurement testing this scaling was found. With the source's own conversion (0.12 km per cec on that path), the typical 10.2 kHz anomalies above correspond to height changes of 1.2–2.8 km, i.e. to less than one decade of flux on this scaling, so the two are not in conflict but neither confirms the other.
- Time scales: X-ray flux rises "often within a few minutes" and decays over "a few tens of minutes to several hours" [P8 §1]; flare onset 10–100 s, typical duration ≈30 min [P16 §1; read]; phase-detected onset lags the X-ray onset by 56 s and 177 s on average on two paths [P15 §2.3; read]; eclipse measurements give an ionospheric response time constant of 4.3 ± 1.5 min (Lynn 1981 via [T9]; secondary).
- Amplitude is a poor predictor (4–10 times the mean-square error of phase) and can go either way: amplitude changes "can involve increases, decreases, or both" [P16 abstract and §6; read].
- Direct observation below 9 kHz: M-class flares on 15 April 2022 are visible in both amplitude and phase of a 5,170 Hz amateur signal over 168 km [G1]; on the plot the 10:30 UT event appears at about 10:45 UT as a phase advance of roughly +20 to +40° depending on field component, with amplitude changes from about −6 dB (east–west magnetic component) to about +1 to +2 dB (north–south magnetic and vertical electric) [G3 image; reading of the plot].
- Opposite sign at long range: flares are reported to give a sudden decrease of atmospherics between 1 and 10 kHz, while enhancing them at 10–75 kHz and below 1 kHz [T9, background chapter; secondary, original not seen]. A 2024 paper likewise states, citing earlier work, that flares increase attenuation in the lower VLF band (<16 kHz) and decrease it above 16 kHz [P22 §1; read].
- ELF: during an M8.2 flare the attenuation of lightning impulses received from 5–9 Mm decreased and their group velocity rose from ≈0.86 c to 0.90–0.91 c, 1–3 min after the X-ray rise; a two-parameter D-region model reproduces the velocity change at 450 Hz [P22 §3–4; read].
- A 2026 method detects 82.7 % of M and X flares from VLF phase trend changes "within one fourth of their rise time" (15–45 kHz) [P19 abstract; read].

### V8. Lightning-induced perturbations

- Lightning-induced electron precipitation (measured on NAA 24.0 kHz and NAU 40.75 kHz, mid-latitude USA): amplitude change 0.5–10 dB, onset delay 0.2–2.5 s after the stroke, onset duration 0.5–5 s, recovery 10–100 s; on one night 81 events ≥0.5 dB in four hours, from 1.7 % of 4,810 located flashes [T8 Table 1.1 and ch. 2; read].
- Early VLF events (measured at 19.8 and 21.4 kHz, Fiji): most are "associated with amplitude changes between 0.2-0.8 dB with only a few cases > 0.8 dB"; early/fast events occur more often at night [P18 abstract; read].
- Below 10 kHz: lightning-induced (Trimpi) events have been observed at high latitude (L ≥ 4) on 3.79 kHz signals from Siple Station received at Halley and South Pole; amplitude events of up to 6 dB at Halley could not be reproduced by height depressions on the great-circle path and were attributed to echoes from precipitation patches off the path [P25; abstract]. Nearest above 10 kHz, on Omega Argentina 12.9 kHz received at Palmer (L ≈ 2.4), amplitude changes "tended to be small (~0.5 dB) and negative" and phase perturbations were about 10 dB more detectable than amplitude ones [P26; abstract]. No early/fast events below 10 kHz were found. Their duration (seconds to two minutes) is shorter than one 60 s chip or comparable to it.

### V9. Storms, polar-cap events, eclipses, seasons

- Geomagnetic activity: qualitative sensitivity of night-time mid-latitude paths [P6 §3.3]; in the Arctic the quiet D region itself is maintained by energetic-electron precipitation, by day and by night [P11, P12 abstracts; read]. No quantitative storm statistics obtained.
- Polar-cap absorption: effects "may last up to 14 days" at magnetic latitudes above 60°; reflection height lowered ≈10 km in a moderate event [T1 §2; read].
- Eclipse (11 August 1999, 17 paths, 90–14,510 km, 16–24 kHz; the frequency range was not confirmed on re-check): amplitude increases on paths <2,000 km, decreases on paths >10,000 km, phase retardation on most paths (Clilverd et al. 2001 via [T9 ch. 4]; secondary); 2–3 dB on short paths (Crary & Schneible 1965 via [T9]; secondary).
- Season: night-time amplitude semi-annual oscillation of 3.3–4.2 dB peak to peak [P17; read]. Ground-wave seasonal changes quoted by ITU apply to 150 kHz and above [S2 NOTE 2] and are not evidence for sub-9 kHz.

### V10. Demonstrated coherent integration below 9 kHz

All entries are amateur experiments with GPS-disciplined transmit and receive timing, reported by the receiving operator [G1 unless stated; read]. Bandwidth is the reciprocal of the coherent integration time.

| Path | f (Hz) | km | Coherent integration achieved |
|---|---|---|---|
| France→Todmorden (UK) | 8,270.035 | 578 | 97 h continuous (1–5 Feb 2022), day and night together: line 13.8 dB above noise in 2.86 µHz, 5.5 nV/m (spectrum image 220205a.png confirms a single-bin line) |
| Italy→Todmorden | 8,269.9975 | 1,413 | 121 h continuous (27 Jan–1 Feb 2020): 15.9 dB in 2.3 µHz, ≈300 nW ERP |
| Germany→Todmorden | 8,270.001 | 1,071 | daytime only: 13.9 dB in 15.4 µHz (≈18 h), ≈200 nW ERP |
| Newfoundland→Todmorden | 8,270.007 | 3,576 | 13.9 dB in 46.3 µHz (6 h), ≈10 µW ERP |
| Newfoundland→France | 8,270.000277 | 4,039 | carrier detected "in 4 hour integration" (2025) |
| Germany→Todmorden | 4,470.005 | 881 | 48 h, 14.0 dB in 23.1 µHz after correcting a 20° day/night offset, ≈5 µW ERP [G1, G8] |
| Germany→Todmorden | 2,970.000081 | 881 | 13:00–00:00 UT on nine days stacked: 14.0 dB in 25.25 µHz |
| Germany→Poland | 1,670.03 | 576 | ten 2.4 h integrations (115.7 µHz), 5.2 σ |
| Germany (ground dipole) | 970.03 | 344 | 18.4 dB in 12.8 µHz (≈22 h) |
| Germany (ground dipole) | 227.005 | 164 | 104 h: 18.5 dB in 2.67 µHz |
| North Carolina→Todmorden, coherent BPSK | 8,269 | 6,194 | 42 characters, 7.5 s symbols, 22:15–07:59 UT (9.7 h), decoded with "constant reference phase", Eb/N0 +0.1 dB against +0.15 dB predicted by the operator's signal calculator [G7] |
| North Carolina→Todmorden, coherent BPSK | 8,822 | 6,194 | 3,040 symbols of 8 s (6.8 h), Eb/N0 −0.1 dB, 0.12 fT, ≈150 µW ERP [G5] |
| Germany→Todmorden, coherent BPSK | 8,270.1 | 881 | 300 characters, 6 s symbols, 12 h 14 min per night; decoded from two nights combined coherently (Eb/N0 +1.7 dB; all three nights combined +2.6 dB) [G6] |
| Germany→India, coherent BPSK | 8,270.03 | 7,493 | 30 s symbols, Eb/N0 −0.6 dB (2022) |

Operational statements from the decoder documentation: "Usually at VLF the decode is successful with a uniform reference phase"; if not, the message is split into two and then four segments with separate phases (108 combinations in 30° or 15° steps); extra searching is needed "if propagation was disturbed or if one end of the link has oscillator problems"; signals "can be received with a steady phase sometimes for hours at a time" [G2; read]. Symbol periods in use range from 1.5 s to 180 s [G1].

Interpretation [E]. A line that stays in one 2.3–2.9 µHz bin for 4–5 days shows that the mean path phasor is stable from day to day. It does not show that phase is constant: a repeatable diurnal phase swing Δφ puts power into sidebands at multiples of 11.6 µHz (outside the analysed bin) and reduces the carrier by 20·log10|cos(Δφ/2)| for a two-state day/night pattern: 0.3 dB for 30°, 1.7 dB for 70°, 3.8 dB for 100°, 11.7 dB for 150°. With ≈40°/Mm (V4) this caps useful round-the-clock integration at roughly 2 Mm at 8.27 kHz; longer amateur paths are in fact integrated over night-time windows only and "stacked" across nights [G1]. A constant-phase BPSK decode over 9.7 h whose measured Eb/N0 (+0.1 dB) came within 0.05 dB of the operator's prediction (+0.15 dB) [G7] suggests that the coherence loss 20·log10⟨cos φ⟩ that night was small. Taking it as under ≈1 dB (an assumption: night-to-night S/N varies by ±2 dB on such paths [G2], so the prediction is itself uncertain) bounds the total excursion to about 54° for a two-state pattern or about 94° for a linear drift [E: 20·log10 cos(Δφ/2) and 20·log10 sinc(Δφ/2) = −1 dB], i.e. a mean rate under about 4–7.5 µHz. Hourly phases on a 1 Mm path wandered faster than that on a good night (V4, night of 5–6 January 2015).

### V11. Equivalent Doppler shifts and what they allow

Doppler f_D = (dφ/dt)/360°. For a linear phase ramp, coherent integration over T loses 20·log10|sin(π f_D T)/(π f_D T)| dB: 0.2 dB at f_D·T = 0.125, 0.9 dB at 0.25, 1.0 dB at 0.262, 3.9 dB at 0.5 [E]. The 1 dB coherence time is therefore T₁ ≈ 0.26/f_D, and a 96 min (5,760 s) bit integrated without phase tracking needs f_D < 45 µHz.

| Situation (source of the phase rate) | Phase rate | f_D | Drift per 60 s chip / per 96 min bit | T₁ |
|---|---|---|---|---|
| 8.3 kHz, 1,028 km, midday to night, 4 h averages [G4] | 23° in ≈9 h | 2.0 µHz | 0.04° / 4° | 37 h |
| 8.27 kHz, 1,028 km, good winter night, 1 h averages, magnetic field [G12] | 71° in 2.5 h | 22 µHz | 0.47° / 45° | 3.3 h |
| Same, electric field [G12] | 42° in 4.5 h | 7.2 µHz | 0.16° / 15° | 10 h |
| 8.27 kHz, 6,194 km, all-dark night [E on G7, bound for a linear drift] | ≤94° in 9.7 h | ≤7.5 µHz | ≤0.16° / ≤16° | ≥9.7 h |
| 10.2 kHz, 8.7 Mm, mean over the terminator crossing [P6 Fig. 1] | 358° in ≈9 h | 31 µHz | 0.7° / 64° | 2.4 h |
| Same, steepest hour [reading of P6 Fig. 1] | ≈100° in 1 h | 77 µHz | 1.7° / 160° | 57 min |
| Same, sunrise step [reading of P6 Fig. 1] | ≈40° in ≈15 min | 0.12 mHz | 2.7° / 256° | 35 min |
| 13.6 kHz, 10.1 Mm [reading of P6 Fig. 4] | ≈395° in ≈7 h | 44 µHz | 0.9° / 90° | 1.7 h |
| Flare onset, 10.2 kHz, 7.8 Mm, median of 500 events [T10] | 3.5 cec (12.6°) per minute for ≈6 min | 0.58 mHz | 12.6° / — | 7.5 min |
| Flare M1, 8.27 kHz, 1 Mm sunlit [E, V7] | 24° in 300 s | 0.22 mHz | 4.9° / — | 20 min |
| Flare X1, 8.27 kHz, 6 Mm sunlit [E, V7] | 290° in 300 s | 2.7 mHz | 58° / — | 1.6 min |
| Flare X10, 8.27 kHz, 6 Mm, 180 s rise [E, V7] | 440° in 180 s | 6.7 mHz | 146° / — | 39 s |
| Precipitation burst [T8; 5° assumed] | 5° in 1 s, recovering in 10–100 s | transient | — | — |
| Sub9 best preset, if its 1 mHz bound were a constant offset | 0.36°/s | 1 mHz | 21.6° / 2,074° (5.8 cycles) | 4.4 min |
| Sub9 moderate preset, likewise | 1.8°/s | 5 mHz | 108° / 10,368° (28.8 cycles) | 52 s |
| Sub9 worst preset, likewise | 18°/s | 50 mHz | 1,080° / 103,680° (288 cycles) | 5.2 s |

For the transients (the sunrise step and the flare rows) the rate lasts only minutes, and the total excursion bounds the loss over a longer integration: in the two-state sense of V10 a 40° step costs at most 0.5 dB, and the 24° of an M1 flare on a 1 Mm path about 0.2 dB [E].

### V12. Controlled sources in or near 1–9 kHz

| Source | What was measured about the path | Source |
|---|---|---|
| HAARP modulated heating, 2,125 and 575 Hz | Detected at Midway Atoll (≈4,400 km): "∼5 fT averaged over a 1-hour long integration" (the same thesis also quotes "∼ −55 dB-pT" for this detection, which is 1.8 fT; the two statements differ) | Moore et al. (2007) as described in [T6 ch. 1 and App. A; secondary] |
| HAARP, 2,375 Hz, 1 March 2007 | Up to −32 dB-pT (25 fT) at Midway (4,466 km), trackable on a seconds scale for 40 min; Kodiak (661 km) 10–20 dB stronger "and more stable" than Juneau (704 km, different azimuth); day/night difference ≈6 dB/Mm | [T6 App. A; read] |
| HAARP digital tests, 1,510, 2,125 and 2,735 Hz | QPSK at 100–800 bit/s received locally; error-rate outliers at 100 bit/s attributed to drift of the phase reference during a 40 s packet "if the ionosphere is highly variable"; the receiver was 37 km from the heater, so this is mainly variability of the source region rather than of a path | [T7 ch. 6; read] |
| Tromsø heater | Detections at 200 and 500 km (1985) and at ∼2,000 km at several frequencies (1991), with polarisation matching a propagation model; a claimed 2,073 Hz detection at ∼6,000 km is disputed | Barr et al. as described in [T6 ch. 1; secondary] |
| Lofted and long-wire sources | Radiated power only: 0.17–0.79 W at 1,280 Hz from a 10.6 km power line (Kafjord, Norway); 100 W at 6.6 kHz from a balloon-lofted 1.5 km vertical. For an aerostat-supported antenna the abstract states that at both VLF and ELF measured fields agreed closely with predictions | Dazey & Koons (1982) and Koons & Dazey (1983) via [T6; secondary]; Field et al. (1989) [abstract only] |
| Siple Station (≈1–10 kHz), US Navy ELF (76 Hz), ZEVS (82 Hz) | Antenna facts (Siple: 21–42 km dipole on ice, a few per cent efficient; Navy: ≈10 W radiated from 150 km of antenna; ZEVS: two parallel grounded antennas each about 60 km long, 200–300 A, 20–250 Hz). Path data: ZEVS 82 Hz received in January 1990 at 3.2, 16.5 and 18.3 Mm with S/N of 25, 13 and 9 dB in a 2 min integration, matching a prediction with 0.5 dB/Mm at night and 1.3 dB/Mm by day [P21; read]. Siple: lightning-induced events on its 3.79 kHz signal at Halley and South Pole [P25; abstract], and controlled transmissions from its dipole at varied (simulated) orientation used to test a high-latitude low-VLF mode model [P27; abstract] | Raghuram et al. (1974), Jones (1995), Velikhov et al. (1998) via [T6, T7; secondary]; Velikhov et al. (1996) via [P21] |
| Amateur transmitters, 227 Hz–8.97 kHz | See V2, V4, V10 | [G1–G8, G12] |

Two consistency checks of the single-mode formula of M2 against amateur data [E]: (i) 80 nW or 40 nW ERP (the milestone list gives 40 nW; the 80 nW figure was not confirmed on re-check) at 578 km, h = 87 km, α = 2 dB/Mm predicts 6.7 or 4.8 nV/m; 5.5 nV/m was measured as a four-day average [G1, G3]. (ii) The transatlantic case in V1 gives a plausible 1.5 dB/Mm. Agreement within the ERP uncertainty supports using the mode formula for level prediction at 8–9 kHz beyond a few hundred km.

Path loss in the conventional sense is small. From P.368-10 NOTE 1, L_b = 142.0 + 20·log10 f_MHz − E(dBµV/m per kW) [S2; read]; with the single-mode field at 8.27 kHz this is ≈46–49 dB at 1 Mm, ≈59–81 dB at 5 Mm and ≈70–114 dB at 10 Mm (night west→east to day east→west) [E]. A flat 180–200 dB "path loss" at these frequencies can therefore only be reached by adding quantities that are not path properties: the transmitting antenna's inefficiency and, because the GUI's noise density is fixed at −164 dBm/Hz, the external noise factor F_a (GUI path loss = L_b + F_a − 13.5 dB + antenna loss relative to an ideal short monopole, [00 §3.1](00-method-and-modem-context.md)).

### V13. Through-the-earth and earth-mode, reported

- Amateur earth-mode tests: 4–5 W between earth electrodes about 20 m apart reached about 5.6 km at 838 Hz and 5.1 km at 8.76 kHz, but only along buried pipes ("utilities assisted"); "well away from pipes there was no detectable signal"; at the range limit 8.76 kHz was about 6 dB weaker than 838 Hz [G9; read].
- Mine-communication and cave-radio measurement papers were not obtained.

## Moderate → adverse → worst-case parameter table

Applies to a far-field path (beyond ≈100 km) near 8.27 kHz unless a row says otherwise. "Worst case" entries combine conditions that have each been observed or computed but that no source found reports measured together below 9 kHz.

| Parameter | Moderate | Adverse | Worst case / outage |
|---|---|---|---|
| Attenuation rate at 8–9 kHz | 1.5–2.3 dB/Mm: night, sea, west→east [T1 Fig. 31a graph, T; E on G5] | 6–6.4 dB/Mm: day, sea, east→west [P3 graph, P; T1 graph, T] | 8 to >10 dB/Mm: day, east→west over 0.3–1 mS/m ground [P2 Fig. 6 graph, P]; ice-cap paths worse, not quantified here |
| Attenuation rate at 5.17 kHz | 3.5–7 dB/Mm at night (TE1–TM1) [E model] | 9–10 dB/Mm by day, east→west [P3 graph, P]; 10–13 [E model] | adverse plus ≈3 dB/Mm direction penalty and ground loss [P3 Fig. 12, P; P2, P] |
| Attenuation rate at 2.97 kHz | 7–28 dB/Mm at night depending on polarisation [E model] | 20–27 (TE) to 56–66 (TM) dB/Mm by day [E model] | daytime far field effectively closed in the model; amateurs report no usable daytime signal at 4.47 kHz over 881 km [G8], although the 2.97 kHz detection over the same distance stacked windows starting at 13:00 UT, in daylight [G1] |
| Attenuation rate below 1 kHz | 0.5 dB/Mm (January) to ≈1 dB/Mm (March/April) at 82 Hz, night [P21, P]; ≈1 dB/Mm at 100 Hz; ≈3–6 at 500 Hz, night [P4 graph, P] | ≈7 at 500 Hz, 13–20 at 1 kHz, day [P4, P5 graphs, P] | disturbed-ionosphere values not obtained |
| Day–night level change | ±5 dB at 168 km, 5.17 kHz [G3 reading, G] | +6.6 dB (13.6 kHz) to +13.8 dB (10.2 kHz) over 8.7 Mm [P6, P]; ≈6 dB/Mm at 2.4 kHz [T6, T] | "no useful signal during daytime" at 4.47 kHz over 881 km [G8] |
| Slow fading depth | ±2 dB night to night, 6.2 Mm, 8.8 kHz [G2, G]; ±2 dB day to day at 10.2 kHz [P6 reading, P] | 4–8 dB terminator minima, ≈1 h [P6 reading, P]; seasonal 3–4 dB [P17, P] | 10–40 dB nulls lasting tens of minutes on a 168 km night path at 5.17 kHz [G3 reading, G] |
| Carrier phase excursion, day to night | 23–31° at 1 Mm, 8.3 kHz [G4, G]; 20° at 0.9 Mm, 4.47 kHz [G8, G] | 39–41°/Mm on long paths at 10–14 kHz [P6, P]; ≈40–55°/Mm single-mode at 8 kHz [E on T1] | multiple cycles on 6–10 Mm paths (≈360° at 9 Mm) [P6, P] |
| Repeatability of phase | ≈9° spread of nightly constant phases over three nights, 881 km [G6, G]; ≈7° (2 cec) standard deviation at a fixed hour over eight days, 10.2 kHz, 7.8 Mm [T11, T]; <10° by day [P8, P] | ±15° day, ±30–40° night across nine days, 10 Mm, 13.6 kHz [P6 reading, P] | phase reversals through deep nulls [G3 reading, G]; cycle slips at dawn [P14, P]; one cycle lost every night at 10.2 kHz on an east-to-west path from near the magnetic equator [T12, T] |
| Doppler shift (phase rate) | 2–22 µHz: 4 h averages and hourly night-time wander at 1 Mm [G4, G12, G]; ≤7.5 µHz bound over a 9.7 h night at 6.2 Mm [E on G7] | 30–120 µHz during terminator passage on long paths [E on P6] | 0.58 mHz median flare onset at 10.2 kHz over 7.8 Mm, for ≈6 min [T10, T]; 0.2–7 mHz for minutes while M–X flares rise, extrapolated to 8.27 kHz [E on P8, P15] |
| 1 dB coherence time without tracking | ≈3–37 h [E, V11] | 0.5–2.4 h [E, V11] | ≈40 s to 20 min during a flare rise; 7.5 min at the median 10.2 kHz onset rate [E, V11] |
| Demonstrated coherent integration | 9.7 h BPSK over 6.2 Mm; 97–121 h carrier over 0.6–1.4 Mm [G1, G7, G] | night-only windows stacked over 9–18 nights [G1, G] | not applicable |
| Differential delay between components | 50–75 µs: one hop against ground wave at 1 Mm [E; S1 Fig. 4, S] | 0.25–0.9 ms: hops at 100–300 km, or modes 1–2 over 1–2 Mm [E] | 1.9 ms multi-hop echo measured at 172 km [G3 reading, G]; ±1 ms group-delay swing at 1 Mm at night [G4, G]; several ms/Mm for a mode within 10 % of cut-off [E] |
| Flare step | M-class: +20–40° with −6 to +2 dB depending on field component at 168 km, 5.17 kHz [G3 reading, G]; typically 36–54° (mean 83°) over 7.8 Mm at 10.2 kHz [T10, T] | X1: ≈49°/Mm on the sunlit part [E] | X10–X45: ≈73–93°/Mm [E on P8]; sign and size of the level change unpredictable [P16, P] |
| Short transients | 0.2–0.8 dB steps [P18, P; measured at 20 kHz] | 0.5–10 dB for 10–100 s, tens per night under active storms [T8, T; 24 and 40.75 kHz] | up to 6 dB on 3.79 kHz Siple signals at Halley, high latitude [P25, P, abstract]; no early/fast events measured below 10 kHz |
| Polar and auroral paths | night H′ 77–79 km instead of 85 km [P12, P] | storm-time variability [P6, P; qualitative] | polar-cap event: reflection height −10 km for up to 14 days [T1, T] |
| Near-field correction to the 1/d law | <1 dB beyond 58 km at 8.27 kHz [S2] | +11 dB at kr = 0.5 (2.9 km at 8.27 kHz; 8 km at 2.97 kHz) [E on S2] | inside λ/2π the link is an induction link, not a radio path |

## How the literature models or simulates this

- ITU-R P.684-8 (2022) [S1]. Wave-hop method (ground wave plus hops with empirical reflection coefficients) and a full-wave waveguide method built on MODESRCH. The mode finder was "developed primarily for VLF and lower LF (10 kHz to about 60 kHz)" and "also finds the single mode needed for ELF"; the programme "can be used to predict field strength at ELF (50 Hz to 3 000 Hz)" although "simpler methods have been developed for the low frequency part of this band" [S1 §3.3 and §5; read]. Horizontal changes are handled by a WKB mode sum when gradual and by mode conversion at the terminator or at land–sea boundaries [S1 §4.2–4.3]. Stated accuracy: none numerical; Annex 2 says seasonal variation cannot be predicted and gives no validation below 10 kHz. ELF profiles (electrons and ions to 260 km) are supplied separately [S1 Fig. 23, Tables 4–5].
- LWPC / MODEFNDR / MODESRCH (US Navy). Segmented waveguide (PRESEG), full-wave reflection coefficients and mode search (MODEFNDR), mode conversion (FASTMC) [T4 §2.5; read]. Reported performance: day and night amplitude and phase on long sea paths at 10–41 kHz reproduced with a single (H′, β) pair to within the quoted uncertainties (±0.4 km, ±0.04 km⁻¹) [P6]; trans-equatorial night paths are not reproduced by any realistic pair [P7]. Below 10 kHz: Cummer fitted night-time sferic spectra with MODEFNDR to a height precision of 0.2 km, with the fit criterion summed over 3–14 kHz; below 3 kHz the observed spectra "consistently show variations that are not predicted by the propagation model", which he attributes to the QTEM mode, and at 16–20 kHz observation and model disagreed consistently [T4 §3.4 and §5.2.5; read]. He modelled the QTEM band below 1.5 kHz separately, after modifying the code's output stage; daytime spectra lacked the structure needed for fitting [T4 abstract, §2.5, §4.1; read]. Ferguson's 1998 LWPC manual was not obtained (DTIC refused automated access), so its stated frequency limits are not verified here.
- Wait's mode theory with exponential profiles: the standard graphs for 8–30 kHz [T1], with ground-conductivity extension [P2]; Galejs's surface-impedance method for 20 Hz–3 kHz and VLF [P4]; Johler and Berry's complete mode sum [T3]. Wait and Spies themselves say the exponential form is not expected to be adequate at night [P2 §1; read].
- Stanford full-wave method (Lehtinen & Inan 2008, 2009): horizontally stratified anisotropic layers, flat Earth, fields in all components; used within 1,000 km of a source; its stratification error is quoted as no worse than 3 % for the main source term, and it has been compared with a finite-difference time-domain code [T6 ch. 2; T7 ch. 2; read].
- Finite-difference time domain and frequency domain codes for long paths (Cummer 2000; Chevalier & Inan 2006; Payne et al. 2007; later work by Marshall and colleagues). Cummer (2000) compares mode theory with time-domain simulation of lightning radiation and reports that fields from evanescent modes and post-discharge ionospheric currents matter at lower ELF (≲500 Hz) and short range (≲500 km) [abstract only]. Han & Cummer (2010) fitted night-time sferic spectra at 3–8 kHz against a database computed with an FDTD code, and report that a tenfold change in ion density and a change from a perfectly conducting ground to soil or water have negligible effect on the fitted interference pattern below 8 kHz, while changing the collision-frequency profile shifts it by the equivalent of 0.2 km of height [P23 §2; read]. Burns et al. (2021) ran a 3-D FDTD model over 2,000 km at 24 kHz and compared it with LWPC: amplitude and phase RMS differences of 0.39 dB and 2.1° for a homogeneous daytime ionosphere and 1.5 dB and 18° for a night-time one (h′ 85 km, β 0.7 km⁻¹) [P29 §V; read]. Marshall, Wallace & Turbe (2017) compared FDTD, FDFD and LWPC results; only its catalogue record was seen. These are model-to-model comparisons; no finite-difference validation against measured fields between 1 and 9 kHz was found.
- Longwave Mode Propagator (Gasdia & Marshall 2021): open-source Julia mode-theory code "similar to" LWPC "but aims to be more robust and adaptable", accepting arbitrary profiles [G10; read]. Its paper states that it "has been verified to produce reasonable results for transmitter frequencies from about 5 kHz to at least 100 kHz"; the comparisons it shows with LWPC and a finite-difference code use a 24 kHz transmitter [P28 §III–IV; read]. It is used with LWPC in current flare work at 15–45 kHz [P19]. No validation below 5 kHz is claimed.
- Empirical data sets usable for validation: the amateur milestone list and plots [G1, G3]; Omega 10.2–13.6 kHz day/night records [P6, T11] and flare statistics [T10]; sferic-derived attenuation [P3; Chapman & Macario via P5] and night-time heights from 3–8 kHz sferics [P23] and tweeks [P24]; 76–82 Hz field strengths at global ranges [P21]; AARDDVARK and similar 16–46 kHz phase/amplitude archives used in [P10–P17].

Known failures and limits: single-profile models at night near the equator [P7]; daytime mid/high-latitude profiles differing from the ITU pair [P10, P11]; dawn and dusk before 2025 [P14]; anisotropic mode coupling in the absorption band [T3]; sferic spectra below 3 kHz that the model did not reproduce [T4]; and, generally, no published validation of any code against controlled-source phase data between 1 and 9 kHz.

## What a high-fidelity simulation would need

Inputs: great-circle geometry and bearing (magnetic azimuth and dip); ground conductivity along the path (sea 5 S/m to ice 0.025 mS/m); date and time for solar zenith angle along the path; H′ and β as functions of zenith angle and latitude (V3) with night-to-night and within-night randomness; X-ray flux for flares; optional precipitation and polar-cap events; transmitter type (vertical monopole or ground dipole with its bearing) and receiver field component (vertical E or horizontal B), because TE and TM modes differ strongly below about 6 kHz.

Recent evidence reinforces the path-conditional reading: [Gu et al. (2021)](https://doi.org/10.1029/2020JA029101) use 2016–2019 ground observations in mid-latitude China and report that lightning within roughly 3,500 km dominates measured 3–30 kHz atmospheric noise at that station, with seasonal changes up to 10 dB. This constrains a plausible local-noise driver but supplies neither a universal sub-9 kHz noise law nor a coupled path-loss/noise distribution. [Reuveni et al. (2011)](https://www.ursi.org/proceedings/procGA11/ursi/EP1-8.pdf) report a 386-day Negev record sampling one minute every 15 minutes at 4 and 7.5 kHz in 75 Hz bandwidth; its Vd distributions vary with hour and season and are fit better by two Gaussians than by Poisson, Rayleigh or lognormal fits in their stated comparison. Vd is impulsiveness, not absolute PSD, and the record is one remote site. Do not transfer either study's marginal statistics to a 0.06-Hz modem input without validating the estimator and bandwidth conversion ([12](12-sub9khz-noise.md)).

Model-structure options, with limits:

1. Level and phase from a mode sum. Use an existing code (LWPC or the Longwave Mode Propagator) per path segment, with mode conversion at the terminator. Gives level, phase, modal minima and their time evolution. Limits: night-time and sub-5 kHz validity is thinly tested; a full run per time step is heavy but the signal is so slow that minute-scale steps suffice.
2. Table-driven single-mode model beyond roughly 1.5–2 Mm at 8–9 kHz: amplitude from the M2 formula with α from V1 and phase from Δφ = 360°·(d/λ)·Δ(v/c), each integrated over day and night segments. Cheap and transparent; cannot produce modal minima or short-path nulls.
3. Ray model inside about 1 Mm: ground wave plus N hops with the delays of M2 and night/day reflection coefficients, each hop phase driven by a slowly varying reflection height. This reproduces the measured echo delays and deep night nulls on short paths. Limits: reflection coefficients below 10 kHz at steep incidence are poorly documented; P.684-8 justifies the hop method by the guide being several wavelengths high [S1 §1.1], whereas it is only about two wavelengths high at 8 kHz and less than one at 3 kHz.
4. Stochastic layer on top of 1–3: a slow random process for H′ (and β) at night, with a standard deviation of the order of 1 km and correlation times of tens of minutes to hours, scaled by the sensitivities in V2 (2.7–5.5° per km per Mm for mode 1; about 17° per km at 1 Mm for the relative phase of modes 1 and 2 in the ideal guide). Night-time heights fitted to 3–8 kHz sferics support that order: hourly means with a standard deviation of 1.1 km, changes of up to 1.3 km in an hour and 4 km in 5 h, oscillations with periods of tens of minutes to hours on some nights, and quiet nights with less than 0.5 km in 5 h [P23]. Those are path-averaged heights from one mid-latitude summer; the carrier-phase statistics they imply have not been measured and would be tuned against the 5.17 kHz and 8.3 kHz amateur records.
5. Event layer: flare phase steps proportional to log flux with minutes-scale rise and tens-of-minutes decay; terminator steps and minima; optional 0.2–10 dB, 10–100 s precipitation transients; polar-cap events for high-latitude paths.

For the modem under study the essential outputs are a complex gain versus time with (a) microhertz-scale phase drift most of the time, (b) tens to hundreds of microhertz for about an hour at each terminator passage on long paths, (c) millihertz-scale ramps during large flares, (d) deep slow fades at night on short and medium paths, and (e) day/night level steps. Delay spread needs no tapped delay line at 0.06 Hz bandwidth; it enters only through the relative phases of rays or modes. A two-ray model is adequate only if its relative Doppler is made time-varying over five orders of magnitude and its amplitude ratio is tied to time of day and range.

Observables to validate against: the diurnal amplitude/phase curves of [P6] at 10.2 kHz; the 8.3 kHz two-tone phase and group-delay table [G4]; the 5.17 kHz 24 h three-component record [G3]; the 4–8.3 kHz echo profile at 172 km [G3]; multi-day carrier line widths [G1]; nightly phase repeatability [G6]; hourly night-time phase wander [G12]; flare phase per decade [P8, P15] and the 10.2 kHz flare statistics [T10]. Other approaches (full finite-difference modelling, assimilation of live VLF monitor data) may well be better; they were not assessed from primary sources here.

## Comparison with the modem's presets

The Sub9 presets of [00 §5](00-method-and-modem-context.md) bound the offset and wander of a two-path family whose echo delay is drawn once inside the preset delay and whose echo-to-direct ratio varies smoothly between zero and one; they are not stationary spreads. Against this chapter's evidence near 8.27 kHz [E, from V2, V4 and V11]:

| Preset (Doppler; delay) | Doppler bound against measured phase rates | Delay against measured differential delays |
|---|---|---|
| None (0; 0) | Nearest to quiet conditions: 2–22 µHz costs at most ≈0.2 dB over a 96 min bit, but the 23–31° (1 Mm) to ≈40°/Mm (long paths) diurnal swing, terminator passages and flares are absent | — |
| Sub9 best (1 mHz; 5 µs) | 45–500 times the quiet-time rates; above the terminator rates (30–120 µHz) and close to measured flare onsets (median 0.58 mHz at 10.2 kHz) | Below every measured differential delay (50 µs to 1.9 ms) |
| Sub9 moderate (5 mHz; 50 µs) | 230–2,500 times the quiet-time rates; inside the extrapolated range for M–X flare onsets (0.2–7 mHz), which lasts minutes | Comparable to one hop against the ground wave at 1 Mm (50–75 µs) |
| Sub9 worst (50 mHz; 5 ms) | About 7 times the largest extrapolated flare rate and 85 times the measured median onset; nothing found below 9 kHz reaches it | 2.6 times the longest measured echo (1.9 ms at 172 km); otherwise only a heavily attenuated mode within 10 % of cut-off (several ms/Mm) |

- Across the 0.06 Hz occupied band these delays produce little frequency selectivity, though the paths can change complex gain and cancel (|cos(πΔfτ)| > 0.9999 even for 5 ms), so in row 1 the preset's frequency bound and echo ratio are what matter. A two-path family with an echo ratio near one can produce the 10–40 dB slow nulls measured at night on a 168 km path only if the paths' relative frequency stays at the microhertz to tens-of-microhertz rates found here; if their actual separation were the Sub9 best bound, the two paths would beat every ≈17 min, faster than the measured nulls, which last tens of minutes [E].
- Level: the simulator's single constant gain, with at most one ramped attenuation interval, cannot follow the diurnal level steps (+7 to +14 dB night minus day at 10–14 kHz over 9 Mm; no useful daytime signal at 4.47 kHz over 0.9 Mm), the 4–8 dB terminator minima or the night nulls; one ramped interval can stand for one such event.
- Flat across frequency: the simulator applies one gain to DATA and the Helper tones alike. Below 9 kHz the attenuation rate changes by an order of magnitude between 550 Hz, 2.4 kHz and 8 kHz, and from day to night (V1), so tones a few kHz apart see different levels and phases [E].
- Path loss: the basic transmission loss at 8.27 kHz is only 46–114 dB (V12). Under the convention of [00 §3.1](00-method-and-modem-context.md), row 1's 200 dB against −164 dBm/Hz therefore stands for an external-noise factor F_a plus transmitting-antenna loss of about 100–168 dB (200 + 13.5 − L_b) [E]; whether that is realistic is for the noise and antenna chapters [12](12-sub9khz-noise.md) and [11](11-sub9khz-antennas-links-and-practice.md).
- Noise: this chapter adds only that the waveguide carrying the atmospherics changes with the same diurnal and flare effects (for example a reported flare-time decrease of 1–10 kHz atmospherics [T9]), so a fixed-variance white noise floor cannot follow them; the noise statistics themselves are in [12](12-sub9khz-noise.md).
- The HF, Aurora and EME presets describe other paths and do not apply below 9 kHz.

## Contradictions, open questions, and things not found

- Daytime attenuation at 3–5 kHz. Taylor's 1960 study measured vertical-E sferic waveforms at separated stations and estimated daytime attenuation from matched-stroke spectra over multi-megametre paths that were predominantly sea; it reported about 9–10 dB/Mm at 5–6 kHz and ≈5 dB/Mm at 3–4 kHz. The author cautioned that abrupt values below 5 kHz could have large errors where spectral amplitudes approached the recording/analysis floor, and the inferred land correction was only an approximate +0.3 dB/Mm [P3 §§3–4; read]. For the chapter's 5.17-kHz example, the measured 9–10 dB/Mm and the selected model's 10–13 dB/Mm overlap to the precision of the graph/profile inputs; this frequency is bounded for scenario sensitivity, not empirically validated. The stronger mismatch is the near-3-kHz maximum: the isotropic vertically polarized mode calculation gives tens of dB/Mm while Taylor's low-spectrum estimate is smaller and explicitly uncertain; receiver-floor/tracing error or a different modal mixture remain live alternatives. Cummer's fits likewise excluded below 3 kHz where observations and model differed [T4]. The mismatch is therefore unresolved below about 4 kHz and must not be collapsed into a single precise attenuation curve. Barr (1970), Barr, Jones & Rodger (2000), Watt (1967) and Galejs (1972), which could provide a more complete primary comparison, remain unavailable.
- Day–night phase change at 1 Mm. Measured 23–31° at 8.3 kHz over 1,028 km [G4] against 40–55° from single-mode theory; explained qualitatively by multi-component propagation at that range, not verified by modelling.
- Night-time group delay at 1 Mm. The measured swings of about ±1 ms [G4] are some twenty times what the isotropic model of V1 gives for interference between the first two vertically polarised modes (TM2 is ≈17 dB down at 1 Mm, giving about ±0.05 ms). Either measurement noise accounts for them (they are 1.5–1.7 times the author's stated error), or components not in the model (the ground wave, steep sky-wave hops, magnetically coupled quasi-TE modes) are stronger at night than it predicts. The 172 km sounding, where the first hop is as strong as the ground wave, favours the second reading but does not settle it.
- ITU planning profiles against measured profiles (V3): day 74/0.3 everywhere against 69–74 km and 0.32–0.49 km⁻¹ depending on latitude and zenith angle; night β at 10 kHz of 0.3 against a measured 0.63. Wait and Spies also found β ≈ 0.5 to 1 fitted night data better than 0.3 [T1 §8].
- Direction of the non-reciprocity. P.684-8 Annex 3 says of its 24 kHz coverage example that "Propagation from East-to-West is better than from West-to-East" [S1 Annex 3; read], the opposite of the sense measured at 3–10 kHz [P3] and computed by Wait and Spies and by Galejs [T1 §7, P2, P4]. Whether this is a wording slip or a property of that example (a transmitter in central Canada) was not resolved.
- Amplitude response to flares has different signs in different reports: component-dependent changes from about −6 to +2 dB on a 168 km path at 5.17 kHz [G3 reading], a reported decrease of 1–10 kHz atmospherics [T9, secondary] and, by a 2024 paper's account of earlier work, increased attenuation below 16 kHz on long paths, while ELF attenuation decreases [P22]. Lowering the reflection height raises cut-off frequencies and near-cut-off loss on long paths while strengthening the steep-incidence sky wave on short ones; this explanation is offered here and is untested.
- HAARP Midway level quoted two ways in the same thesis (5 fT and −55 dB-pT) [T6].
- Flare phase at 8.27 kHz. The extrapolation of V7 (≈24°/Mm per decade) is about 1.5 times the per-Mm sensitivity measured at 21–25 kHz and has not been tested; the only nearby measurements (10.2 kHz SPAs) are statistics of flares of unknown class. An independent check needs a sub-9 kHz carrier recorded through flares on a sunlit path of known length.
- Not found or not obtained: statistics of night-time fading or phase of a carrier below 9 kHz (only reflection-height statistics from 3–8 kHz sferics and from tweeks were found [P23, P24]); measurements of storm effects, polar-cap effects or eclipses below 10 kHz; early/fast lightning perturbations below 10 kHz, and lightning-induced events at 3.79 kHz beyond one abstract [P25]; Siple path data beyond two abstracts [P25, P27], and Carpenter's and Helliwell's Siple papers; Bannister's NUSC reports on 76 Hz propagation (search results list several on DTIC, for example ADA046411 and ADA167606, but DTIC refused automated access) and any measured phase stability at 76–82 Hz; controlled-source results from the Kola Peninsula (FENICS); Füllekrug's and Nickolaenko & Hayakawa's work; finite-difference validation against measured fields at 1–9 kHz from 2015–2026 (only model-to-model comparisons at 24 kHz were found [P29]); mine and cave through-the-earth measurements; the 2017 and 2024 eclipse VLF papers; Watt (1967) and Galejs (1972), not found on archive.org, and Budden (1961), listed there only as a restricted lending copy; conference abstracts that, according to search-result summaries, report daytime tweeks (1.5–10 kHz) disappearing for hours after M-class flares (the abstract pages refused automated access, so the claim is not used). DTIC, AGU/Wiley, ADS and the Wayback Machine refused automated access; the original amateur site is offline and a 2025 mirror was used.
- Recollections not verified and not used as evidence: 76 Hz attenuation of about 1.2–1.3 dB/Mm by day and 0.8 dB/Mm at night with c/v about 1.2 (Bannister 1984) [recollection, unverified; Fraser-Smith & Bannister (1998) use 1.3 dB/Mm by day, 0.5–1 dB/Mm at night and c/v ≈ 1.09 at night for 76–82 Hz [P21]]; LWPC's nominal range of roughly 10–60 kHz in Ferguson (1998) [recollection, unverified; consistent with the ITU wording]; "Porrat, Bannister & Fraser-Smith" as authors of the 2001 Radio Science paper on modal phenomena below 5 kHz [title, journal and year confirmed by search; authors from recollection]. Marshall, Wallace & Turbe (2017) is confirmed bibliographically (listed below), but its content was not read.
- Limits of the grey evidence: the amateur results come from one very careful receiving station and a handful of transmitters, mostly on 0.6–1.4 Mm European land/sea paths and North Atlantic night paths, in winter, at 8.27 kHz. Values read off posted plots are approximate. ERP figures are the operators' estimates.

## References

Standards
- [S1] ITU-R Recommendation P.684-8 (08/2022), "Prediction of field strength at frequencies below about 150 kHz". https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.684-8-202208-I!!PDF-E.pdf — read. [S]
- [S2] ITU-R Recommendation P.368-10 (08/2022), "Ground-wave propagation prediction method for frequencies between 10 kHz and 30 MHz". https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.368-10-202208-I!!PDF-E.pdf — read. [S]
- [S3] ITU-R Recommendation P.527-6 (09/2021), "Electrical characteristics of the surface of the Earth". https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.527-6-202109-I!!PDF-E.pdf — read (§3). [S]

Peer-reviewed papers (read in full text unless noted)
- [P1] Wait, J. R. (1960), "Terrestrial propagation of very-low-frequency radio waves: a theoretical investigation", J. Res. NBS 64D(2), 153–204. https://nvlpubs.nist.gov/nistpubs/jres/64D/jresv64Dn2p153_A1b.pdf [P]
- [P2] Wait, J. R., and K. P. Spies (1965), "Influence of finite ground conductivity on the propagation of VLF radio waves", Radio Sci. J. Res. NBS 69D(10), 1359–1373. https://nvlpubs.nist.gov/nistpubs/jres/69D/jresv69Dn10p1359_A1b.pdf [P]
- [P3] Taylor, W. L. (1960), "Daytime attenuation rates in the very low frequency band using atmospherics", J. Res. NBS 64D(4), 349–355. https://nvlpubs.nist.gov/nistpubs/jres/64D/jresv64Dn4p349_A1b.pdf [P]
- [P4] Galejs, J. (1964), "ELF and VLF waves below an inhomogeneous anisotropic ionosphere", Radio Sci. J. Res. NBS 68D(6), 693–707. https://nvlpubs.nist.gov/nistpubs/jres/68D/jresv68Dn6p693_A1b.pdf [P]
- [P5] Chapman, F. W., and D. Llanwyn Jones (1964), "Observations of Earth-ionosphere cavity resonances and their interpretation in terms of a two-layer ionosphere model", Radio Sci. J. Res. NBS 68D(11), 1177–1185. https://nvlpubs.nist.gov/nistpubs/jres/68D/jresv68Dn11p1177_A1b.pdf [P]
- [P6] Thomson, N. R., M. A. Clilverd, and W. M. McRae (2007), "Nighttime ionospheric D region parameters from VLF phase and amplitude", J. Geophys. Res. 112, A07304, doi:10.1029/2007JA012271. Author copy: https://www.physics.otago.ac.nz/space/Thomson_etal_2007_nighttime_model.pdf [P]
- [P7] Thomson, N. R., and W. M. McRae (2009), "Nighttime ionospheric D region: Equatorial and non-equatorial", J. Geophys. Res. 114, A08305, doi:10.1029/2008JA014001. https://www.physics.otago.ac.nz/space/2008JA014001-pip.pdf [P]
- [P8] Thomson, N. R., C. J. Rodger, and M. A. Clilverd (2005), "Large solar flares and their ionospheric D-region enhancements", J. Geophys. Res. 110, A06306, doi:10.1029/2005JA011008. https://www.physics.otago.ac.nz/space/Thomson_etal_2005_super_flare.pdf [P]
- [P9] Thomson, N. R., M. A. Clilverd, and C. J. Rodger (2014), "Low-latitude ionospheric D region dependence on solar zenith angle", J. Geophys. Res. 119, 6865–6875, doi:10.1002/2014JA020299. https://www.physics.otago.ac.nz/space/MS_Accepted_11Aug14.pdf [P]
- [P10] Thomson, N. R., M. A. Clilverd, and C. J. Rodger (2017), "Midlatitude ionospheric D region: height, sharpness and solar zenith angle", J. Geophys. Res. 122, 8933–8946, doi:10.1002/2017JA024455. https://www.physics.otago.ac.nz/space/NorthSea_Dregion_acepted_2Aug2017.pdf [P]
- [P11] Thomson, N. R., M. A. Clilverd, and C. J. Rodger (2018), "Quiet daytime Arctic ionospheric D region", J. Geophys. Res. 123, 9726–9742, doi:10.1029/2018JA025669. https://www.physics.otago.ac.nz/space/Daytime_Arctic_NRT_quiet_Dregion_2018.pdf — abstract and introduction read. [P]
- [P12] Thomson, N. R., M. A. Clilverd, J. B. Brundell, and C. J. Rodger (2021), "Quiet night Arctic ionospheric D region characteristics", J. Geophys. Res. 126, e2020JA029043. https://www.physics.otago.ac.nz/space/2020JA029043R_Article_File.pdf — abstract read. [P]
- [P13] Thomson, N. R., M. A. Clilverd, and C. J. Rodger (2022), "Ionospheric D region: VLF-measured electron densities compared with rocket-based FIRI-2018 model", J. Geophys. Res. 127, e2022JA030977. https://www.physics.otago.ac.nz/space/2022JA030977R_Merged_PDF.pdf — abstract and introduction read. [P]
- [P14] Thomson, N. R., M. A. Clilverd, and C. J. Rodger (2025), "Ionospheric D region: characteristics near dawn and dusk", J. Geophys. Res. 130, e2024JA033610. https://www.physics.otago.ac.nz/space/2024JA033610R_Merged_PDF.pdf — abstract and introduction read. [P]
- [P15] Belcher, S. R. G., M. A. Clilverd, C. J. Rodger, S. Cook, N. R. Thomson, J. B. Brundell, and T. Raita (2021), "Solar flare X-ray impacts on long subionospheric VLF paths", Space Weather 19, e2021SW002820. https://www.physics.otago.ac.nz/space/2021SW002820RR_Merged_PDF.pdf [P]
- [P16] George, H., C. J. Rodger, M. A. Clilverd, K. Cresswell-Moorcock, J. B. Brundell, and N. R. Thomson (2019), "Developing a nowcasting capability for X-class solar flares using VLF radiowave propagation changes", Space Weather 17, 1783–1799, doi:10.1029/2019SW002297. https://www.physics.otago.ac.nz/space/2019SW002297RR_Merged_PDF.pdf — abstract, introduction and discussion read. [P]
- [P17] Silber, I., C. Price, and C. J. Rodger (2016), "Semi-annual oscillation (SAO) of the nighttime ionospheric D region as detected through ground-based VLF receivers", Atmos. Chem. Phys. 16, 3279–3288, doi:10.5194/acp-16-3279-2016. https://acp.copernicus.org/articles/16/3279/2016/acp-16-3279-2016.pdf [P]
- [P18] Kumar, S., A. Kumar, and C. J. Rodger (2008), "Subionospheric early VLF perturbations observed at Suva: VLF detection of red sprites in the day?", J. Geophys. Res. 113, A03106, doi:10.1029/2007JA012750. https://www.physics.otago.ac.nz/space/USP_Trimpi_accepted_MS.pdf — abstract read. [P]
- [P19] Teysseyre, P., C. Briand, and M. Cohen (2026), "Real-time detection of solar flares from ground-based VLF data", preprint submitted to J. Space Weather Space Clim., arXiv:2605.11822. https://arxiv.org/abs/2605.11822 — abstract and introduction read. [P, preprint]
- [P20] Galejs, J. (1965), "Schumann resonances", Radio Sci. J. Res. NBS 69D(8), 1043–1055. https://nvlpubs.nist.gov/nistpubs/jres/69D/jresv69Dn8p1043_A1b.pdf — located, skimmed, not used for numbers. [P]
- [P21] Fraser-Smith, A. C., and P. R. Bannister (1998), "Reception of ELF signals at antipodal distances", Radio Sci. 33(1), 83–88, doi:10.1029/97RS01948 (bibliographic data from search records). Author copy: https://www.quakefinder.com/research/EQTdata/82Hz.pdf [P]
- [P22] Ostrowski, M., M. Gołkowski, J. Kubisz, Z. Nieckarz, A. Michalec, J. Mlynarczyk, J. Lichtenberger, and A. Maxworth (2024), "Effects of a solar flare on global propagation of extremely low frequency waves", J. Geophys. Res. Space Physics 129, e2024JA033083, doi:10.1029/2024JA033083. Repository copy: https://par.nsf.gov/servlets/purl/10610157 [P]
- [P23] Han, F., and S. A. Cummer (2010), "Midlatitude nighttime D region ionosphere variability on hourly to monthly time scales", J. Geophys. Res. 115, A09323, doi:10.1029/2010JA015437. Repository copy: https://dukespace.lib.duke.edu/server/api/core/bitstreams/92e24580-b7b7-44ba-8e2a-cb6824a71fcd/content [P]
- [P24] Maurya, A. K., B. Veenadhari, R. Singh, S. Kumar, M. B. Cohen, R. Selvakumaran, S. Gokani, P. Pant, A. K. Singh, and U. S. Inan (2012), "Nighttime D-region electron density measurements from ELF-VLF tweek radio atmospherics recorded at low latitudes", J. Geophys. Res., AGU manuscript 2012JA017876 (accepted copy). https://webflash.ess.washington.edu/publications/nightimeD-region2012JA017876-pip.pdf [P]
- [P25] Cotton, P. D., and A. J. Smith (1991), "Signature of burst particle precipitation on VLF signals propagating in the Antarctic earth-ionosphere waveguide", J. Geophys. Res. 96(A11), doi:10.1029/91JA01794. Record: https://www.osti.gov/biblio/5694833 — abstract read (its text is partly garbled in the record). [P]
- [P26] Inan, U. S., and D. L. Carpenter (1987), "Lightning-induced electron precipitation events observed at L ≈ 2.4 as phase and amplitude perturbations on subionospheric VLF signals", J. Geophys. Res. 92(A4), 3293, doi:10.1029/JA092iA04p03293. Record: https://www.osti.gov/biblio/6581317 — abstract read. [P]
- [P27] Smith, A. J., and P. D. Cotton (1990), "The Trimpi effect in Antarctica: observations and models", J. Atmos. Terr. Phys. 52(5). Record: https://nora.nerc.ac.uk/id/eprint/520874/ — abstract read. [P]
- [P28] Gasdia, F., and R. A. Marshall (2021), "A new longwave mode propagator for the Earth–ionosphere waveguide", IEEE Trans. Antennas Propag. (2021). Accepted version: https://par.nsf.gov/servlets/purl/10319646 — §III–IV read. [P]
- [P29] Burns, S., F. Gasdia, J. J. Simpson, and R. A. Marshall (2021), "3-D FDTD modeling of long-distance VLF propagation in the Earth-ionosphere waveguide", IEEE Trans. Antennas Propag. 69(11), from p. 7743. Copy: https://my.ece.utah.edu/~simpson/Papers/Paper44.pdf [P]

Reports and theses (read)
- [T1] Wait, J. R., and K. P. Spies (1964), "Characteristics of the Earth-ionosphere waveguide for VLF radio waves", NBS Technical Note 300. https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote300.pdf [T]
- [T2] Wait, J. R. (1960), "A survey and bibliography of recent research in the propagation of VLF radio waves", NBS Technical Note 58. https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote58.pdf [T]
- [T3] Johler, J. R., and L. A. Berry (1964), "A complete mode sum for LF, VLF, ELF terrestrial radio wave fields", NBS Monograph 78. https://nvlpubs.nist.gov/nistpubs/Legacy/MONO/nbsmonograph78.pdf — abstract and introduction read. [T]
- [T4] Cummer, S. A. (1997), "Lightning and ionospheric remote sensing using VLF/ELF radio atmospherics", PhD dissertation, Stanford University. Copy: https://pe2bz.philpem.me.uk/Comm/-%20ELF-VLF/-%20ELF-Theory/Papers-001/cummerthesis.pdf [T]
- [T5] Reising, S. C. (1998), "Remote sensing of the electrodynamic coupling between thunderstorm systems and the mesosphere/lower ionosphere", PhD dissertation, Stanford University. Copy: https://pe2bz.philpem.me.uk/Comm/-%20ELF-VLF/-%20ELF-Theory/Papers-001/reising_thesis.pdf [T]
- [T6] Cohen, M. B. (2009), "ELF/VLF phased array generation via frequency-matched steering of a continuous HF ionospheric heating beam", PhD dissertation, Stanford University. https://vlfstanford.ku.edu.tr/wp-content/uploads/2010/06/ThesisMoCo_OneSided.pdf [T]
- [T7] Jin, G. (2012), "Generation of extremely low frequency waves via modulated ionospheric heating for communications applications", PhD dissertation, Stanford University. https://vlfstanford.ku.edu.tr/wp-content/uploads/2013/01/JinThesisOnline_0.pdf [T]
- [T8] Peter, W. B. (2007), "Quantitative measurement of lightning-induced electron precipitation using VLF remote sensing", PhD dissertation, Stanford University. https://vlfstanford.ku.edu.tr/wp-content/uploads/2010/06/Thesis_Peter_Final.pdf [T]
- [T9] Pal, S. (2013), "Numerical modelling of VLF radio wave propagation through Earth-ionosphere waveguide and its application to sudden ionospheric disturbances", PhD thesis, University of Calcutta, arXiv:1503.05789. https://arxiv.org/abs/1503.05789 — used as a secondary source. [T]
- [T10] Swanson, E. R., and C. P. Kugel (1973), "A synoptic study of sudden phase anomalies (SPA's) effecting VLF navigation and timing", Naval Electronics Laboratory Center, in Proc. 5th Annual NASA and DoD Precise Time and Time Interval Planning Meeting, NASA Goddard Space Flight Center. https://ntrs.nasa.gov/api/citations/19750003227/downloads/19750003227.pdf [T]
- [T11] Swanson, E. R. (1972), "Use of propagation corrections for VLF timing", Naval Electronics Laboratory Center. Copy: https://www.masterclock.com/cmss_files/attachmentlibrary/Archived-papers/Use-of-Propagation-Corrections-for-VLF-Timing-1972.pdf [T]
- [T12] Burhans, R. W. (1976), "Diurnal measurements with prototype CMOS Omega receivers", Technical Memorandum 42, Avionics Engineering Center, Ohio University, for NASA Langley (NASA-CR-149160). https://ntrs.nasa.gov/api/citations/19770005663/downloads/19770005663.pdf [T]

The host of [T6]–[T8] refused automated access when this chapter was checked; those three were checked against copies retrieved from the same URLs earlier on 2026-11-13.

Grey literature (read)
- [G1] Nicholson, P., "Some recent milestones with amateur radio experiments at VLF" (list maintained to 2025). Original http://abelian.org/vlf/amateur-radio/ (offline); mirror of 8 August 2025: https://www.i2phd.org/abelian/abelian.org/vlf/amateur-radio/index.html [G]
- [G2] Nicholson, P., "EbNaut coherent BPSK" and "Technical details". Mirror: https://www.i2phd.org/abelian/abelian.org/ebnaut/index.html and https://www.i2phd.org/abelian/abelian.org/ebnaut/notes.html [G]
- [G3] Nicholson, P., "Todmorden VLF" station page and linked plots under https://www.i2phd.org/abelian/abelian.org/vlf/tmp/ (dl3jmm_dl0ao_5k17_220415-16.png; staircase_VLF3.png; 150822_sferic_modes_EW_25a.png; 191207-n-all-h1.png; 220205a.png; 200201a.png; 170326b.png). Page: https://www.i2phd.org/abelian/abelian.org/todmorden-vlf/index.html [G]
- [G4] Nicholson, P., "VLF: Path measurement by group delay", rsgb_lf_group, 7 January 2015. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2015-01/msg00164.html [G]
- [G5] Nicholson, P., "VLF: Transatlantic messages at 8822Hz", rsgb_lf_group, 4 January 2015. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2015-01/msg00095.html [G]
- [G6] Nicholson, P., "Re: VLF: A 300 char message?", rsgb_lf_group, 14 November 2017. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2017-11/msg00108.html [G]
- [G7] Nicholson, P., "Re: LF: 8269 kHz EbNaut tonight", rsgb_lf_group, 26 December 2017. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2017-12/msg00380.html [G]
- [G8] Nicholson, P., "Re: VLF: Carrier on 4470.005 Hz!", rsgb_lf_group, 24 August 2017. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2017-08/msg00126.html [G]
- [G9] Lapthorn, R. (G3XBM), "VLF using earth mode", Sub 9kHz Amateur Radio site. https://sites.google.com/site/sub9khz/vlf-using-earth-mode [G]
- [G10] Gasdia, F., LongwaveModePropagator.jl, README. https://github.com/fgasdia/LongwaveModePropagator.jl [G]
- [G11] de Boer, P. T. (PA3FWM), "An experimental new digimode: EbNaut". https://pa3fwm.nl/technotes/tn13d.html — summary only. [G]
- [G12] Nicholson, P., "Re: VLF: 8270 carrier", rsgb_lf_group, 6 January 2015. https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2015-01/msg00141.html [G]

Cited through other sources or by abstract only (bibliographic data confirmed from the citing document, an author-maintained publication list or a search record; text not read)
- Barr, R., D. Llanwyn Jones, and C. J. Rodger (2000), "ELF and VLF radio waves", J. Atmos. Sol.-Terr. Phys. 62(17–18), 1689–1718. Not obtained.
- Barr, R. (1970), "The ELF and VLF amplitude spectrum of atmospherics with particular reference to the attenuation band near 3 kHz", J. Atmos. Terr. Phys. Not obtained (title confirmed by search).
- Chapman, F. W., and R. C. V. Macario (1956), Nature 177, 930 — values via [P5].
- Clilverd, M. A., et al. (2001), "Total solar eclipse effects on VLF signals: observations and modeling", Radio Sci. 36(4), 773–788 — via [T9].
- Cummer, S. A. (2000), "Modeling electromagnetic propagation in the Earth-ionosphere waveguide", IEEE Trans. Antennas Propag. 48(9), 1420–1429, doi:10.1109/8.898776 — abstract.
- Field, E. C., L. R. Kies, P. R. Bannister, R. F. Ingram, W. D. Hopkins, and M. A. Roberts (1989), "An aerostat-supported ELF/VLF transmitter", Radio Sci. 24(2), 235–246, doi:10.1029/88RS04241 — abstract.
- Koons, H. C., and M. H. Dazey (1983), "High-power VLF transmitter facility utilizing a balloon lofted antenna", IEEE Trans. Antennas Propag.; Dazey & Koons (1982) — via [T6].
- Lehtinen, N. G., and U. S. Inan (2008), J. Geophys. Res. 113, A06301, doi:10.1029/2007JA012911; (2009), Geophys. Res. Lett. 36, L03104 — via [T6, T7].
- Chevalier, M. W., and U. S. Inan (2006), IEEE Antennas Wireless Propag. Lett. 5, 525–528 — via [T7].
- McRae, W. M., and N. R. Thomson (2000), J. Atmos. Sol.-Terr. Phys. 62, 609–618; (2004), 66, 77–87 — via [P6, P8].
- Moore, R. C., et al. (2007), "ELF waves generated by modulated HF heating of the auroral electrojet and observed at a ground distance of ∼4400 km", J. Geophys. Res. 112, A05309 — via [T6].
- Thomson, N. R. (1993), "Experimental daytime VLF ionospheric parameters", J. Atmos. Terr. Phys. 55, 173–184 — via [P6, P9].
- Ferguson, J. A. (1998), "Computer programs for assessment of long-wavelength radio communications, version 2.0", SSC San Diego Technical Document 3030 — cited by [P19, G10]; not obtained (DTIC refused automated access).
- Marshall, R. A., T. Wallace, and M. Turbe (2017), "Finite-difference modeling of very-low-frequency propagation in the Earth-ionosphere waveguide", IEEE Trans. Antennas Propag. 65(12), 7185–7197, doi:10.1109/TAP.2017.2758392 — catalogue record only: https://experts.colorado.edu/display/pubid_222830

Suggested sources not located in an accessible form: Watt, "VLF Radio Engineering" (1967); Galejs, "Terrestrial Propagation of Long Electromagnetic Waves" (1972); Budden, "The Wave-Guide Mode Theory of Wave Propagation" (1961; archive.org holds only a restricted lending copy); Bannister's ELF papers and NUSC reports (DTIC refused automated access); Füllekrug; Nickolaenko & Hayakawa; Carpenter's and Helliwell's Siple papers (only related abstracts, [P25]–[P27], were seen). Fraser-Smith & Bannister (1998) was obtained as [P21].

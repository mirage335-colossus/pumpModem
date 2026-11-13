# Skywave 1–30 MHz: signal level, absorption and outages

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** dispersion at mid-latitudes [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and at high and low latitudes [32](32-skywave-dispersion-highlatitude-equatorial.md); channel models [33](33-hf-channel-models-and-simulators.md); noise [50](50-noise-and-interference-mf-hf.md).

- **Predictions are monthly medians with large error.** ITU-R P.533-14 (08/2019) is the latest in-force revision. The ITU-R comparison example against the D1 data bank (P.1148-1, 1997, for the method as it then stood) shows a standard deviation of 10.5 dB over 16 268 monthly-median cases: 7–8 dB below 2 000 km, 13.6 and 14.5 dB in the 3 000–3 999 and 5 000–6 999 km bins, and 16.0 dB for path midpoints above 60° geomagnetic [S].
- **P.533 text and reference code differ.** Printed eq. 20 of P.533-13 and -14 gives twice the absorption that the ITU reference code computes when the per-point terms are equal, and the code's L_z and above-the-MUF laws differ from the text [S, G]. Neither says which is intended.
- **Normal in-band basic loss is far below 180 dB.** A simplified re-computation of P.533-14 in this record for mid-latitude paths gives basic transmission loss of about 103–137 dB at night on 1.8–7 MHz and 118–147 dB by day on the best band, for 100–3 000 km. A 10 000 km path gives about 141–150 dB in darkness and 172–183 dB in full daylight on the best band [E]. The GUI's '180 dB at −164 dBm/Hz' is not a basic loss: by section 3.1 of [00](00-method-and-modem-context.md) it stands for L_b + F_a ≈ 193.5 dB, that is L_b ≈ 134 dB at a rural site at 1.8 MHz up to about 175 dB where galactic noise sets the floor at 30 MHz [E].
- **A basic loss of 180–195 dB corresponds to abnormal propagation.** It matches P.533's above-the-MUF cap, daytime low-band paths, or full-daylight long-haul paths.
- **Day-to-day and within-hour spread.** Lower-decile signal deviations are 8–17 dB day-to-day plus 8 dB within the hour [S]. foF2 decile factors span 0.60–0.92 and 1.09–1.48 [S].
- **Outages.** Flares at the aviation "moderate" threshold (X1, 10⁻⁴ W m⁻² at 0.1–0.8 nm) occur about 123 times per 11-year cycle with a mean event duration of 68 min [P]. For an X1.3 flare, modelling consistent with observations gives 20–40 dB at 6.4 MHz, more than NOAA D-RAP predicts [P]. Polar-cap absorption can exceed 100 dB at HF for 1–4 days [B]. The May 2024 storm cut foF2 over Italy by about 70 % for two whole days [P].
- **MF.** Midday skywave is about 43 dB below the night median [S], and the hourly loss factor changes by about 19 dB over the two hours around sunset and 26 dB over the two hours around sunrise [E from S].

**Scope.** Signal level and availability of the 1–30 MHz skywave channel and their time behaviour. Delay/Doppler dispersion, channel simulators and noise are covered in chapters 31–33 and 50; noise figures appear here only to convert loss into C/N0.

**Search-history note.** The first research pass relied heavily on known-document retrieval and recorded limited 2020–2026 search coverage. On 13 November 2026 a focused follow-up searched the direct-report and recent-event gaps via ITU History, publisher full text, government/open-access repositories and DOI metadata. New discoveries and unresolved items are specified in the last section; this bounded follow-up is not an exhaustive systematic review.

**Evidence tags.** Grade: [S] ITU-R Recommendation or Report, [P] research paper or preprint (peer-review status checked separately), [T] government/laboratory report or documentation, [B] handbook or book, [G] grey literature, [E] this record's own estimate. Access: *read* (passage read in full text), *abstract* (abstract or record only), *secondary* (through the named source).

## Physical mechanisms

### Loss-budget anatomy (P.533-14 and its heritage)

P.533-14 predicts sky-wave propagation at 2–30 MHz (recommends 1) [S, read]. Up to 7 000 km it sums ray modes; beyond 9 000 km it uses an empirical composite formula; between the two it interpolates (§1) [S, read]. For each mode (§5.2.2, eqs 17–18):

- Field strength: E_w = 136.6 + P_t + G_t + 20 log f − L_b dB(1 µV/m), with P_t in dB(1 kW).
- Basic transmission loss: **L_b = 32.45 + 20 log f + 20 log p′ + L_i + L_m + L_g + L_h + L_z**.

| Term | What it is | Form in P.533-14 | Magnitude |
|---|---|---|---|
| 20 log p′ | spreading along the virtual slant path (eq. 19) | mirror reflection at h_r | free-space loss about 93–148 dB for the paths below [E] |
| L_i | D/E-region absorption (eq. 20) | see next subsection | 0–3 dB at night; 5–60 dB per path by day at 7 MHz; hundreds of dB by day at 1.8–3.5 MHz on long hops [E] |
| L_m | above-the-MUF loss (eqs 24–26) | F2: 36[(f/f_b) − 1]^½, at most 62 dB; E: 130[(f/f_b) − 1]², at most 81 dB | 0–62 dB (F2) |
| L_g | intermediate ground reflections (eq. 27) | 2(n − 1) dB | 0–12 dB |
| L_h | "auroral and other signal losses" (Table 2) | by geomagnetic latitude, local time, season, range | 0 below 42.5°; up to 21.4 dB (67.5–72.5°, equinox, 07–10 LT, at most 2 500 km) |
| L_z | effects "not otherwise included" | constant | 8.72 dB |

All rows [S, read]. Key points:

- **L_z is a calibration constant.** NOTE 1 to §5.2.2 defines it as the excess loss found from the difference between predicted field strength and the D1 data bank. The long-path analogue is L_y = −0.14 dB (§5.3.3).
- **Probability of ionospheric support** is handled separately (§3.6, eqs 9–10) from the foF2 decile factors of P.1239.
- **Absorption above the basic MUF** is computed on the ray paths that apply at the basic MUF (§5.2.2).

The 1980 CCIR "second interim method" lists more separate terms (§8.2, eq. 81): L_b = L_bf + L_a + L_e + L_h + L_m + L_q + L_r + L_z − G_f [S, read]. Here L_e is polarisation coupling plus ground reflection, L_q sporadic-E obscuration, L_r sporadic-E reflection and G_f focus gain. P.533 folds several of these into L_z and L_h.

**VOACAP/ICEPAC/IONCAP heritage.** George Lane's 2005 review [G, read] states:

- IONCAP, ICEPAC and ITSA-1 share the same "excess system loss" tables, which forced ITSA-1 predictions to match measurements.
- The tables rest on 43 circuit-years of receiver recordings on 33 circuits of 55–15 000 km at 2–20 MHz.
- The standard deviation of the excess system loss ranges from 6 to over 30 dB with path, season, time and geomagnetic latitude.
- The model had little data at high latitudes and in the Southern Hemisphere.
- Setting the sporadic-E multiplier to the recommended 0.7 raises predicted performance by about 3 dB.

IONCAP's own absorption expression could not be confirmed in a primary or secondary text, so no formula is given here. A 1985 US Air Force handbook describes its predecessor ITS-78 as using a semi-empirical expression for non-deviative absorption, with deviative losses folded into the loss uncertainty factors, and IONCAP as modifying the secant-law absorption equations for low frequencies reflected below 90 km [T, read: Dandekar 1985, §10.3].

### Non-deviative D/E-region absorption

- **Theory.** For quasi-longitudinal vertical propagation the ordinary-wave absorption is proportional to ∫ N ν / [µ (f + f_L)²] dh (eq. 37) [S, read: CCIR Suppl. 252-2 §7.3]. It is "non-deviative" where µ ≈ 1 and "deviative" near reflection where µ < 1.
- **George & Bradley oblique form** (eq. 43) [S, read; George & Bradley 1974 secondary]:
  L_a = φ_n(f_v/foE) · A_T(0,0) · F(χ) · (1 + 0.0067 R12) · sec i_100 / (f + f_L)², with F(χ) = cos^p(0.881 χ) and f_v = f cos i.
- **P.533-14 version** (eq. 20) uses the same structure at the two 90 km penetration points of each hop [S, read]:
  - f_L is the longitudinal gyrofrequency at 100 km, |f_H sin I| (eq. 23).
  - F(χ) has a floor of 0.02 and χ is capped at 102° (eq. 21). The night floor is therefore 2 % of the overhead-sun reference.
  - A_T,noon is 120–360 (Fig. 1). The exponent p is 0.7–1.7 (Fig. 3).
  - φ_n peaks near f_v/foE ≈ 1, where the wave is retarded in the E layer; this is where deviative loss enters.
- **Dependences.** Roughly (f + f_L)^−2 in frequency, sec i in obliquity, cos^p(0.881 χ) in solar zenith angle, linear in R12 (factor 1.07 at R12 = 10, 2.0 at R12 = 150), with f_L about 1.0–1.3 MHz at mid-latitudes [E, dipole field].
- **F-region absorption** from electron–ion and electron–neutral collisions is usually under 1 dB per single hop, except for the high-angle ray [S, read: CCIR Suppl. §7.3.1, citing Rush & Elkins 1975].
- **Night residual.** Japanese field-strength data up to 14 000 km give L_a = (7 + 0.019 D)(1 + 0.015 R12)/(f² + 10) dB, with D in km (eq. 44) [S, read; Wakai 1975 secondary]. At 1 000 km and R12 = 0 this gives 1.2 dB at 3.5 MHz and 2.0 dB at 1.8 MHz [E].
- **Rule of thumb.** 1 dB at 30 MHz may be 10 dB or more at lower HF, and low-elevation rays lose more [B, read: ITU Handbook §6.5.2].
- **Winter anomaly.** At temperate latitudes in winter, absorption is anomalously high and more variable from day to day [S, read: CCIR Suppl. §7.3]. A comparison with a 2.6 MHz, 300 km German path showed systematic differences of up to 6 dB, mainly at sunspot minimum (§7.3.1). P.533's long-path LUF uses a winter-anomaly factor of up to 0.30 at 60° latitude in December–January (Table 5). The Handbook attributes winter variability to meteorological effects and post-storm electron precipitation (§2.6.2.1) [B, read].

### Other loss and gain terms

- **Focusing and defocusing.**
  - Horizon focusing of low-angle rays reaches at most 9 dB at grazing incidence [S, read: CCIR Suppl. §7.2, after Bradley 1970].
  - Antipodal focusing is capped at 15 dB in P.533-14 (eq. 41) but at 30 dB, "arbitrarily", in the 1980 CCIR method (eq. 36).
  - MUF (skip-distance) focusing is transitory and not modelled; defocusing goes with high-angle rays near cut-off (§7.2).
  - Travelling ionospheric disturbances focus and defocus with an average period of about 20 min [B, read: Handbook §2.6.2.2].
- **Ground reflection.** P.533 uses a flat 2 dB per intermediate reflection. The CCIR method uses Fresnel coefficients for a circularly polarised wave with sea (5 S/m, ε_r 80), land (0.01 S/m, ε_r 10) and ice (0.0001 S/m, ε_r 1) (§7.4.2) [S, read]. The ice caps of Greenland and Antarctica are particularly poor reflectors (§6.5.4.2.3) [B, read].
- **Polarisation coupling at HF.** For linear antennas and quasi-longitudinal propagation the loss is 3–6 dB, depending on the extra absorption of the X wave. It can exceed 20 dB under some conditions (§7.4.1, citing Bradley 1968) [S, read].
- **Polarisation coupling at MF.** P.1147-4 gives the *excess* loss L_p = 180(36 + θ² + I²)^−½ − 2 dB per terminal for dip I ≤ 45°, and zero otherwise (eq. 8) [S, read]. This reaches 28 dB per terminal for east–west paths at the dip equator [E]. The power coupled into the extraordinary wave is absorbed, and only the vertical component of the emerging elliptical wave couples to the receiving antenna (§5.2.7) [B, read].
- **Sporadic E.**
  - Obscuration of F modes: L_q = −10 log(1 − R²) per layer traverse, with R falling steeply once f exceeds foEs·sec i (about 0.04 dB at f = foEs·sec i) (eqs 75–76) [S, read; Sinno et al. 1976 secondary].
  - Reflection from Es: P.534-6 gives the attenuation Γ relative to inverse distance (eq. 3) [S, read]. At 1 500 km, Γ is 4 dB at f/foEs = 2 and 23 dB at f/foEs = 5; at 500 km, 18 dB at f/foEs = 2 [E]. The formula holds for under 2 600 km and f/foEs of 1–8, with error under 5 dB.
- **Above-the-MUF propagation.** Report ITU-R P.2011-1 lists the mechanisms (§2) [S, read]:
  - ionospheric roughness (theory: only about 60 kHz above the basic MUF);
  - ground back- and side-scatter (suggested as dominant for single-hop paths up to 4 000 km);
  - higher-order backscatter, ducted and chordal modes, direct ionospheric scatter, sporadic E, auroral scatter and meteor scatter.
- **Auroral absorption.** Energetic electron precipitation in substorms, strongest in the 60–70° geomagnetic "outer precipitation zone" [B, read: Handbook §2.3.2.3–4]. Isolated substorms last about an hour and may recur in rapid succession. The CCIR method maps the probability that 30 MHz riometer absorption exceeds 1 dB and scales it as [(30 + f_L)/(f + f_L)]² (eqs 45–67) [S, read].
- **Polar-cap absorption (PCA).** Solar protons ionise 45–75 km altitude above about 60° magnetic latitude, day and night [B, read: Handbook §2.3.2.6]. Sunlit absorption is about four times the dark value at 30 MHz.
- **Short-wave fadeout (SWF).** Flare X-rays raise D-region density over the whole sunlit hemisphere. Lower frequencies are affected first and longest (§2.6.1.2) [B, read].
- **Post-storm effect.** Radiation-belt electron precipitation raises absorption from sub-auroral to middle latitudes through the storm recovery phase, up to ten days or more (§2.3.2.5) [B, read].

### MUF, LUF and what controls availability

- **Definitions** (P.373-9) [S, read]. *Basic MUF*: highest frequency propagating by refraction alone. *Operational MUF*: highest frequency giving acceptable performance. *OWF* and *HPF*: lower and upper deciles of the daily operational MUF. *LUF*: lowest frequency giving acceptable performance; it depends on the system.
- **Basic MUF** follows from foF2, M(3000)F2 and foE (P.533 eqs 3–6) [S, read].
- **Operational/basic MUF ratio** is 1.10–1.35 depending on season, day or night and radiated power (P.1240-2 Table 1) [S, read].
- **Sunspot index pitfall.** The P.1239 maps require version-1 sunspot numbers: R12(v1) = 0.6 × R12(v2) (P.371-9 Annex 1 §2, eq. 2) [S, read]. Feeding today's SILSO v2 numbers unscaled overstates solar activity by a factor of 1.67. foF2 is treated as saturating above R12 = 160 (P.1239-4 §3.1).
- **System dependence of the LUF.** For conventional services the LUF is set by absorption and noise. For a modem that tolerates 180–200 dB of loss, the classical LUF does not bind in the same way; the absolute absorption does [E].

### MF night skywave

P.1147-4 (150–1 700 kHz, 50–12 000 km) predicts the annual median field at six hours after sunset [S, read]:

- E = V + G_S − L_p + A − 20 log p − L_a − L_t − L_r, with A = 107 at MF (eq. 1).
- p = (d² + 40 000)^½ km (eq. 9).
- L_a = k (p/1000)^½ with k = 2π + 4.95 tan²Φ, Φ being the geomagnetic latitude of the midpoint, capped at 60° (eqs 10–11).
- L_t is the hourly loss factor (Fig. 3, Appendix 1).
- L_r = b (R/100)(p/1000) dB for |Φ| > 45°, with b = (|Φ| − 45)/3, or b = 1 in Europe (eqs 12–13).

The method assumes E-layer reflection. F-layer reflection takes over when f > foE·sec i, most likely at the top of the band, under 500 km, late at night and at sunspot minimum (Annex 1). P.533 starts at 2 MHz and P.1147 stops at 1.7 MHz, so 1.8–2.0 MHz sits in a gap between the two methods.

P.1147-4 (08/2007) is still the edition in force (checked 2026-11-13). It states no numerical error. Its Annex 1, "Accuracy of method", sets only qualitative limits: at LF the method is verified only to 7 500 km; it should be used with caution above 60° geomagnetic latitude; the hourly loss factor is accurate near 1 000 kHz at low solar activity, with appreciable errors away from that frequency, above all in transition hours; and the solar-activity effect two hours after sunset is considerably larger than at six hours [S, read]. The r.m.s. errors quoted under section 3 come from the Handbook, not from the Recommendation.

## Measured and reported values

### 1. Representative loss magnitudes

**Method [E].** The P.533-14 short-path and long-path equations were re-implemented in simplified form for this record. Inputs and simplifications:

- Monthly-median foF2 and M(3000)F2 from the P.1239 grid files distributed with the ITU reference software; foE from P.1239-4 eqs 12–18.
- Centred-dipole magnetic field; mid-path ionosphere; mirror height from eq. 2; p = 1.45; L_h omitted; isotropic antennas.
- Absorption averaged over penetration points and multiplied by hop count, as the ITU reference code does (see "Contradictions"). Applying printed eq. 20 literally would double every [L_i] value below. L_z = 8.72 dB and the above-the-MUF laws are those printed in P.533-14, not the code's.
- Paths lie along 10°E, centred at 40°N (geomagnetic latitude about 41°). "Noon", "midnight" and "pre-dawn" are 11, 23 and 04 UTC.
- Each cell is the range over January, April, July and October. R12 is in version-1 units.
- The cell gives L_b of the strongest unscreened mode and, in brackets, its absorption L_i. A star means the frequency is above that mode's monthly-median basic MUF in all four months; (*) means in some months.

These are this record's estimates, not output of the ITU software. The 1.8 MHz column is outside P.533's validity.

**Check against the reference code [E].** A hand recomputation of one cell (100 km, noon, R12 = 10, 1.8 MHz: 1E mode, L_i about 31 dB in January and 36 dB in July, L_b 125–130 dB) reproduces the table under the stated method. The only published output of the ITU software found, the sample report in its repository (Kranji–Beijing, 4 477 km, January, 02 UTC, R12 = 140, 6.2 MHz, isotropic antennas), gives 161.7 dB for the dominant 2F2 mode. The simplified method gives 172.9 dB there, about 11 dB more, nearly all of it in absorption. It also marks that mode as screened by the E layer, a test P.533 applies only up to 4 000 km (§4). The path lies outside Table 1's geometry (low latitude, beyond d_max, five control points) and the cause of the absorption difference was not isolated. Single cells should nevertheless be read as uncertain by about 10 dB, on top of the prediction error of section 3.

**Table 1. Basic transmission loss L_b (dB), isotropic antennas, P.533-14 short-path equations [E].**

| Path | Time | R12 | 1.8 MHz | 3.5 MHz | 7.0 MHz | 14.0 MHz | 21.0 MHz | 28.0 MHz | path basic MUF (MHz) | foF2 (MHz) |
|---|---|---|---|---|---|---|---|---|---|---|
| 100 km | noon | 10 | 125–130 [Li 31–36] | 125–129(*) [Li 23–30] | 118–135(*) [Li 5–7] | 155–163* [Li 1–2] | 172–181* [Li 1–1] | 186–188* [Li 0–0] | 6.0–7.3 | 5.5–6.7 |
| 100 km | noon | 150 | 144–154 [Li 50–60] | 139–143 [Li 39–43] | 124–128 [Li 9–12] | 132–149* [Li 3–3] | 154–165* [Li 1–2] | 166–178* [Li 1–1] | 9.8–13.3 | 9.2–12.8 |
| 100 km | midnight | 10 | 104–104 [Li 1–2] | 108–109 [Li 0–1] | 141–148* [Li 0–0] | 172–179* [Li 0–0] | 185–186* [Li 0–0] | 188–189* [Li 0–0] | 3.8–4.5 | 3.3–4.0 |
| 100 km | midnight | 150 | 106–107 [Li 2–3] | 111–111 [Li 1–1] | 116–139(*) [Li 0–0] | 149–170* [Li 0–0] | 167–187* [Li 0–0] | 181–190* [Li 0–0] | 5.0–9.1 | 4.4–8.5 |
| 100 km | pre-dawn | 10 | 102–103(*) [Li 1–7] | 107–119(*) [Li 0–2] | 141–153* [Li 0–1] | 172–181* [Li 0–0] | 184–185* [Li 0–0] | 187–187* [Li 0–0] | 3.2–4.5 | 2.6–4.0 |
| 100 km | pre-dawn | 150 | 105–107 [Li 2–12] | 110–112 [Li 1–3] | 115–148(*) [Li 0–1] | 154–180* [Li 0–0] | 172–187* [Li 0–0] | 186–189* [Li 0–0] | 3.8–7.7 | 3.3–7.1 |
| 1000 km | noon | 10 | 173–195 [Li 67–89] | 142–150 [Li 30–38] | 133–136 [Li 15–17] | 136–139* [Li 7–11] | 158–168* [Li 1–2] | 171–181* [Li 1–1] | 10.1–13.1 | 5.5–6.7 |
| 1000 km | noon | 150 | 231–272 [Li 124–165] | 165–180 [Li 52–68] | 142–147 [Li 24–29] | 131–132 [Li 5–6] | 132–149(*) [Li 2–7] | 154–167* [Li 1–2] | 15.1–21.1 | 9.2–12.8 |
| 1000 km | midnight | 10 | 110–111 [Li 2–3] | 114–115 [Li 1–1] | 120–135(*) [Li 0–0] | 161–168* [Li 0–0] | 179–187* [Li 0–0] | 193–194* [Li 0–0] | 5.9–7.2 | 3.3–4.0 |
| 1000 km | midnight | 150 | 112–114 [Li 3–5] | 116–116 [Li 1–2] | 121–121 [Li 0–1] | 134–160* [Li 0–0] | 157–178* [Li 0–0] | 170–192* [Li 0–0] | 7.5–13.4 | 4.4–8.5 |
| 1000 km | pre-dawn | 10 | 110–115 [Li 3–9] | 114–118 [Li 1–5] | 120–140(*) [Li 0–1] | 159–172* [Li 0–0] | 177–191* [Li 0–0] | 190–193* [Li 0–0] | 5.3–7.6 | 2.6–4.0 |
| 1000 km | pre-dawn | 150 | 112–122 [Li 5–16] | 116–121 [Li 2–9] | 120–138(*) [Li 1–2] | 140–169* [Li 0–1] | 160–188* [Li 0–0] | 173–194* [Li 0–0] | 5.7–12.1 | 3.3–7.1 |
| 3000 km | noon | 10 | 276–333 [Li 158–215] | 190–212 [Li 66–88] | 160–166 [Li 30–36] | 145–156 [Li 9–20] | 141–160(*) [Li 4–5] | 158–171* [Li 2–3] | 17.2–23.4 | 5.5–6.7 |
| 3000 km | noon | 150 | 411–518 [Li 293–400] | 244–286 [Li 120–163] | 179–191 [Li 49–62] | 149–154 [Li 13–17] | 144–147 [Li 6–10] | 143–158(*) [Li 3–4] | 24.5–34.8 | 9.2–12.8 |
| 3000 km | midnight | 10 | 125–128 [Li 6–10] | 124–125 [Li 2–3] | 129–129 [Li 1–1] | 149–159* [Li 0–0] | 169–177* [Li 0–0] | 182–190* [Li 0–0] | 9.6–12.0 | 3.3–4.0 |
| 3000 km | midnight | 150 | 130–136 [Li 10–17] | 126–129 [Li 4–6] | 129–130 [Li 1–2] | 134–150(*) [Li 0–1] | 138–170(*) [Li 0–0] | 160–183* [Li 0–0] | 11.7–21.3 | 4.4–8.5 |
| 3000 km | pre-dawn | 10 | 126–139 [Li 8–21] | 125–135 [Li 3–11] | 129–134 [Li 1–4] | 144–161* [Li 0–1] | 166–179* [Li 0–0] | 179–193* [Li 0–0] | 8.9–13.1 | 2.6–4.0 |
| 3000 km | pre-dawn | 150 | 132–157 [Li 14–39] | 127–143 [Li 5–19] | 129–137 [Li 1–6] | 134–161(*) [Li 0–2] | 146–179* [Li 0–1] | 163–192* [Li 0–0] | 9.1–19.9 | 3.3–7.1 |

What Table 1 shows [E]:

- **Night, low bands.** Absorption is 0–2 dB at 3.5 MHz and above for paths up to 1 000 km, and 1–17 dB at 1.8 MHz out to 3 000 km. Loss is close to free-space loss plus L_z.
- **Day, low bands.** At 1 000 km and noon, 3.5 MHz loses 30–68 dB to absorption and 7 MHz loses 15–29 dB. At 3 000 km, 3.5 MHz loses 66–163 dB and is effectively closed.
- **Solar cycle.** Solar maximum roughly doubles daytime absorption but lifts the path MUF, so the best band moves up and the best-band loss changes little.
- **Above the MUF.** P.533 adds up to 62 dB, so the predicted residual sits at 185–194 dB for these path lengths.
- **Pre-dawn MUF minimum.** The 3 000 km path MUF at noon is 17–35 MHz. Before dawn it is 9–20 MHz. The noon-to-pre-dawn ratio is 2.6–3.6 in January and about 1.2–1.3 in July. Monthly-median foF2 falls to 2.6 MHz (R12 = 10) or 3.3 MHz (R12 = 150) in January at about 04–05 local time.

**Long paths.** Table 2 applies the P.533-14 long-path formula (eqs 29–41). f_M and f_L are the method's upper and lower reference frequencies. Values above 250 dB mean the formula is far outside its window and the band is unusable. The last column is the range, over 24 hours, of the loss on the best of the six bands.

**Table 2. Long-path basic transmission loss L_b (dB), from 50°N 10°E, P.533-14 §5.3 [E].**

| Path | Month | R12 | UTC | fM (MHz) | fL (MHz) | 1.8 MHz | 3.5 MHz | 7.0 MHz | 14.0 MHz | 21.0 MHz | 28.0 MHz | best-band Lb over 24 h (min–max) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 000 km, N–S | 1 | 10 | 11 | 27.7 | 11.3 | >250 | >250 | 244 | 176 | 177 | 194 | 144–176 |
| 10 000 km, N–S | 1 | 10 | 23 | 14.8 | 1.8 | 171 | 144 | 144 | 182 | 244 | >250 | 144–176 |
| 10 000 km, N–S | 1 | 150 | 11 | 37.3 | 17.1 | >250 | >250 | >250 | 206 | 180 | 181 | 141–181 |
| 10 000 km, N–S | 1 | 150 | 23 | 16.4 | 1.8 | 171 | 143 | 142 | 173 | 225 | >250 | 141–181 |
| 10 000 km, N–S | 7 | 10 | 11 | 23.2 | 11.6 | >250 | >250 | 242 | 179 | 186 | 210 | 146–179 |
| 10 000 km, N–S | 7 | 10 | 23 | 10.5 | 1.8 | 171 | 147 | 156 | 225 | >250 | >250 | 146–179 |
| 10 000 km, N–S | 7 | 150 | 11 | 38.8 | 17.4 | >250 | >250 | >250 | 209 | 181 | 180 | 144–181 |
| 10 000 km, N–S | 7 | 150 | 23 | 10.7 | 1.8 | 171 | 147 | 155 | 222 | >250 | >250 | 144–181 |
| 19 500 km, az. 180° | 1 | 10 | 11 | 16.3 | 15.2 | >250 | >250 | 235 | 176 | 186 | 215 | 167–176 |
| 19 500 km, az. 180° | 1 | 10 | 23 | 15.7 | 7.5 | >250 | >250 | 172 | 169 | 204 | >250 | 167–176 |
| 19 500 km, az. 180° | 1 | 150 | 11 | 22.0 | 22.9 | >250 | >250 | >250 | 197 | 178 | 185 | 168–179 |
| 19 500 km, az. 180° | 1 | 150 | 23 | 18.1 | 11.5 | >250 | >250 | 211 | 170 | 186 | 219 | 168–179 |
| 19 500 km, az. 180° | 7 | 10 | 11 | 8.9 | 13.8 | >250 | >250 | 182 | 175 | 212 | >250 | 136–179 |
| 19 500 km, az. 180° | 7 | 10 | 23 | 17.4 | 2.5 | 184 | 145 | 136 | 158 | 199 | >250 | 136–179 |
| 19 500 km, az. 180° | 7 | 150 | 11 | 17.0 | 20.9 | >250 | >250 | >250 | 182 | 178 | 195 | 129–178 |
| 19 500 km, az. 180° | 7 | 150 | 23 | 32.1 | 3.6 | 238 | 165 | 136 | 137 | 150 | 169 | 129–178 |

What Table 2 shows [E]:

- **Dark 10 000 km path.** 142–147 dB on the better of 3.5 and 7 MHz. Free-space loss for the 10 494 km slant path is 123.8 dB at 3.5 MHz and 129.8 dB at 7 MHz, so the excess is 12–14 dB in January and about 23 dB in July, when f_M falls to about 10.5 MHz.
- **Sunlit 10 000 km path.** 176–180 dB on the best band (14–28 MHz). Free-space loss is 135.8 dB at 14 MHz and 141.8 dB at 28 MHz, so the excess is about 40 dB. The window between f_L (11–17 MHz) and f_M (23–39 MHz) is narrow.
- **Near-antipodal path.** The figures include 15 dB of antipodal focusing. Best-band loss ranges from 129 to 179 dB over the day. At some hours f_L exceeds f_M (for example July, R12 = 10, 11 UTC: f_M 8.9, f_L 13.8 MHz), and no band is inside the window; the formula then still returns about 175–178 dB.
- **Other geometries** (not tabulated): 10 000 km at azimuth 290°, 136–190 dB; 19 500 km at azimuth 60°, 130–180 dB; the 21 700 km long path, 137–185 dB (best band over 24 h).
- **Limits.** The long-path formula has no above-the-MUF cap and no auroral term. Its 1.8 MHz values are outside the method's range.

**MF night skywave.** Table 3 converts P.1147-4 annual-median field strength at six hours after sunset into basic transmission loss at 1.7 MHz. The conversion is L_b = 146.6 − E_0, taking 1 kW radiated from a short vertical as 34.8 dBW e.i.r.p. and the isotropic received power from P.533 eq. 43 [E].

**Table 3. MF night basic transmission loss (dB) at 1.7 MHz from P.1147-4, by midpoint geomagnetic latitude Φ [E from S].**

| Φ | 100 km | 1 000 km | 3 000 km | 10 000 km |
|---|---|---|---|---|
| 20° | 90 | 107 | 121 | 142 |
| 40° | 91 | 110 | 126 | 151 |
| 50° | 93 | 113 | 132 | (162) |
| 60° | 97 | 121 | 146 | (186) |

Caveats and adders for Table 3:

- The field is the one a ground-level loop measures, so it includes the ground-reflected wave. The transmit vertical-pattern factor G_V is not applied; it matters below about 500 km.
- Bracketed values apply one latitude to the whole 10 000 km path, which is unrealistic.
- Solar-activity loss L_r at R = 150: 2.5 dB (1 000 km) and 7.5 dB (3 000 km) at Φ = 50°; 7.7 and 22.5 dB at Φ = 60°; 1.5 and 4.5 dB with the European value b = 1 [E from S].
- Excess polarisation coupling near the dip equator: up to 28 dB per terminal [E from S].
- Ground conductivity an order of magnitude below 10 mS/m at a terminal lowers the field by up to 10 dB (Annex 1) [S, read].
- The Handbook reports that the method under-predicts paths of 5 000–10 000 km by typically 10 dB (§5.3.2.1) [B, read].

At night the two methods agree. The P.533 extrapolation at 1.8 MHz gives 110–114 dB at 1 000 km and 125–136 dB at 3 000 km (Table 1), against 110 and 126 dB here.

### 2. Variability statistics

**foF2 and MUF, day to day.**

- P.1239-4 Tables 2 and 3 give lower- and upper-decile factors for foF2 by local time, latitude, season and three R12 classes [S, read]. A parse of the tables in this record gives 0.60–0.92 and 1.09–1.48 globally; 0.70–0.90 and 1.09–1.30 at 40–50° latitude; 0.67–0.86 and 1.11–1.42 at 60–70°.
- For E modes the factors are 0.95 and 1.05 (P.533-14 §3.6) [S, read].
- The inter-decile range of the basic MUF at a given hour is 30–40 % of the median (Report P.2011-1 §4) [S, read].
- Rishbeth & Mendillo (2001): in years of medium solar activity (decimetric flux about 140 units) the daily fluctuations of NmF2, averaged over thirteen ionosonde stations, have a standard deviation of 20 % by day and 33 % by night. A large part follows geomagnetic activity and the rest is attributed to "meteorological" sources lower in the atmosphere [P, abstract read]. The 34-year span (1957–1990) and the split into about 3 % solar, 13 % geomagnetic and 15 % neutral-atmosphere contributions, adding in quadrature, come only through Luetzelschwab (2004) [G, secondary]. He describes the measure as the standard deviation of foF2 rather than of NmF2; the abstract is followed here. The full text was not obtained.
  - Since NmF2 ∝ foF2², this is about 10 % and 16 % in foF2. Deciles of 0.87 and 0.79 follow for a normal distribution, in line with P.1239 [E].
- Daily solar indices do not predict the daily ionosphere. At Millstone Hill in December 2003 on quiet days, R² was 0.013 between daily F10.7 and daily foF2 [G, read: Luetzelschwab 2004].

**Signal power, day to day (monthly distribution of hourly medians).**

P.842-5 Table 2 [S, read]; LD and UD are lower- and upper-decile deviations from the monthly median, in dB:

| f / basic MUF | below 60° geomagnetic, LD / UD | 60° or above, LD / UD |
|---|---|---|
| ≤ 0.8 | 8 / 6 | 11 / 9 |
| 1.0 | 12 / 8 | 16 / 11 |
| 1.2 | 13 / 12 | 17 / 12 |
| 1.4 | 10 / 13 | 13 / 13 |
| 2.0 | 8 / 9 | 11 / 9 |
| ≥ 5.0 | 5 / 7 | 7 / 7 |

- The older CCIR tables (after Barghausen et al. 1969) resolve season, local time and latitude [S, read: CCIR Suppl. Tables III–IV]. T_u is the loss exceeded 10 % of the time, as a deviation from the median.
  - Below 40° geomagnetic: T_u 8.2–11.5 dB, T_l 5.1 dB.
  - At 60–65°, paths up to 2 500 km: T_u up to 42.2 dB (equinox, 10–13 LT), 30.3 dB (winter, 07–10 LT), 33.3 dB (summer, 07–10 LT).
  - At 60–65°, paths over 2 500 km: T_u up to about 24 dB.
- The current P.842-5 table depends only on f/MUF and the 60° split. Resolution by season, local time and path length exists only in the older CCIR tables.
- F.339-8 Note 4 uses 10 dB for 90 % of days for the signal, and 11.5 dB combined with noise [S, read].

**Within the hour.**

- Upper decile 5 dB, lower decile 8 dB (P.842-5 Table 1; P.533-14 §8, from WARC HFBC-87) [S, read]. These equal the deciles of a Rayleigh-fading signal, +5.2 and −8.2 dB about the median [E].
- Over 3–7 min, distributions close to Rayleigh predominate. Over 30–60 min the distribution is log-normal with a decile range close to Rayleigh. The Rayleigh median is 1.6 dB below the r.m.s. value (CCIR Suppl. §8.6) [S, read].
- F.339-8 Note 5 assumes log-normal short-period fading with 7 dB between the median and the 10 % or 90 % level [S, read].
- A single 1F mode fades as Nakagami–Rice with a small steady component and a temporal correlation interval of 15–25 s (Handbook §6.5.1.2) [B, read].

**Combined lower decile.** P.533 eq. 46 root-sum-squares the within-hour and day-to-day terms. For the signal alone this gives 11.3 dB (f/MUF ≤ 0.8, mid-latitude), 15.3 dB (f/MUF = 1.2) and 18.8 dB (60° or above, f/MUF = 1.2) [E from S].

**Slower structure.**

- Absorption fading may last longer than an hour (Handbook §6.5.3.1.3) [B, read].
- Large-scale travelling ionospheric disturbances: a 3 November 2017 case had a period of about 2.5 h, wavelength about 1 680 km and speed about 1 200 km/h, seen as oscillating skip distance in amateur-network data [P, abstract: Frissell et al. 2022].

**MF night-to-night.**

- Field exceeded 10 % of the time is 0.2|Φ| − 2 dB above the annual median (limited to 6–10 dB); for 1 % it is 0.2|Φ| + 3 dB (limited to 11–15 dB) (P.1147-4 §3) [S, read].
- Night-to-night medians are log-normal; the fading rate at 1 MHz on long E-layer paths is about 10 per hour in Europe and about three times that in Japan (Handbook §5.2.1–2) [B, read].

### 3. Accuracy of prediction programs

**P.533 against the ITU-R D1 data bank.** P.1148-1 Table 2 gives an example of the standard comparison output (predicted minus measured monthly medians) [S, read]. It dates from 1997 and the table does not name the program; §5 of the same text refers to REC533, the P.533 implementation of the time. P.533-14 itself says only that D1 tests show accuracy comparable to more complex methods (considering a).

| Subset | Count | Mean (dB) | Standard deviation (dB) |
|---|---|---|---|
| All data | 16 268 | 0.0 | 10.5 |
| 2–5 MHz | 4 030 | −1.7 | 9.1 |
| 5–10 MHz | 4 004 | −0.3 | 10.6 |
| 15–30 MHz | 2 964 | +3.7 | 12.1 |
| 0–999 km | 6 439 | −0.3 | 7.7 |
| 1 000–1 999 km | 1 144 | +1.5 | 7.4 |
| 3 000–3 999 km | 975 | +3.8 | 13.6 |
| 5 000–6 999 km | 2 785 | −1.2 | 14.5 |
| 9 000–11 999 km | 2 223 | −1.0 | 11.1 |
| Geomagnetic latitude 40–60° | 6 226 | −0.3 | 9.0 |
| Geomagnetic latitude over 60° | 2 116 | +0.3 | 16.0 |

- The zero overall mean is expected, because the excess-loss constant is fitted to this data bank (an inference of this record from NOTE 1 of P.533 §5.2.2) [E].
- The Recommendation notes that the standard deviation says nothing about accuracy on individual days (Annex 1 §2).
- The data bank is dominated by measurements from Germany (10 499 cases) and Japan (4 119).

**Other evidence.**

- *MF.* Root-mean-square errors of the ITU-R method are 9.3 dB on intra-African paths, 5.8 dB on 87 Region 2 paths and 4.9 dB on 84 Asian paths (Handbook §5.3.2) [B, read].
- *High latitudes.* On a 285 km Norwegian path (Andøya–Alta, March 1988), ICEPAC was within ±20 dB of measured transmission loss (100–150 dB) in quiet conditions. Under disturbance it erred by up to about 200 dB in both directions: too much loss in the late morning, too little for most of the day, when the measured absorption at Qe = 8 was 150–280 dB. Measured circuit reliability was zero all day [G, read: Lane 2005, citing Jodalen & Thrane 1994].
- *NVIS.* On UK 5.29 MHz links in 2009–2011, VOACAP and ASAPS matched measurements well in September–November and March. They missed in April–August (attributed to sporadic E and possibly deviative absorption) and in December–February (winter anomaly) [P, abstract: Walden 2012].
- *Physics-based model against amateur links.* SAMI3 predicted 55.5 % of reported 2–30 MHz links of 500–2 000 km in March 2019; its NmF2 was biased 27 % high [P, abstract: Chartier et al. 2023].
- *High-latitude foF2.* E-CHAIM improves on IRI by up to 1.3 MHz r.m.s. in foF2 in the polar cap. A-CHAIM assimilation cut foF2 error by 40–50 % at mid-latitude and auroral stations in one disturbed week [P, abstract: Themens et al. 2017; Reid et al. 2023].
- *Assimilative foF2 nowcasts.* Two real-time models (IRI background, one digisonde and GPS TEC) gave foF2 r.m.s. errors of 0.2 MHz at night and 0.5 MHz by day at the best ground-truth site, about 685 km from the assimilated digisonde, in South Africa in September 2011. The errors were close to the floor set by travelling ionospheric disturbances [P, abstract: McNamara et al. 2013]. IRTAM, which assimilates a global ionosonde network into IRI, improved foF2 against 40 ionosondes in 2000–2019 but showed no gain against COSMIC radio occultation; it helps only near the assimilated stations [P, abstract: Pignalberi et al. 2021]. In May 2024 the IRI UP IONORING nowcast over Italy overestimated foF2 in the storm's main phase (evening of 10–11 May) and agreed well in the recovery phase [P, read: Spogli et al. 2024].
- *HF nowcast of supported frequencies.* A nowcast driven by a global assimilative model specified the supported frequency range on mid-latitude circuits, but tended to underestimate the MUF and did not follow day-to-day variability well. Without real-time absorption and noise observations it could not specify signal-to-noise ratio [P, abstract: McNamara et al. 2009].
- *Single cases.* A 27 308 km long-path contact on 14 MHz arrived about 42 dB stronger than VOACAP's 175 dB total loss implied. A 160 m path of 11 044 km was heard when multi-hop ray tracing put it 55 dB below the noise [G, read: Luetzelschwab].
- *Above-the-MUF loss models.* For 2 000 km and f/f_b = 1.4, the Gibson–Bradley model (theoretical, two-hop ground scatter) gives 80 dB and the Suessmann model (fitted to D1 for paths up to 4 000 km) 53 dB (Report P.2011-1 §6.2, §6.4) [S, read]. P.533's printed F2 formula gives 23 dB, and the reference code's law for paths up to 3 000 km 28 dB [E]. The Report also lists Hunt's (1991) D1 fit, printed as 36(f/f_b − 1)² dB with a 62 dB cap, which would give 6 dB. P.533 uses the same coefficient and cap with the exponent ½ (§6.3, §6.5) [S, read].

### 4. Outage mechanisms and magnitudes

**Short-wave fadeout (solar X-ray flares).**

*The operational model.* NOAA D-RAP documentation (updated 2010) [T, read]:

- Highest affected frequency (1 dB, vertical, two-way) at the sub-solar point: HAF = 10 log(flux in W m⁻²) + 65 MHz. It is fitted to M1 → 15, M5 → 20, X1 → 25 and X5 → 30 MHz from a 1997 forecaster manual.
- It tapers as (cos χ)^0.75.
- Absorption at other frequencies: A(f) = (f_0/f)^1.5 A(f_0).
- Oblique paths: scale by 1/sin(elevation). The documentation does not say at which height the elevation is taken.
- Typical flare durations used for the recovery clock: 25 min (M1), 40 min (M5), 60 min (X1), 120 min (X5).

Table 4 applies these rules [E from T].

**Table 4. D-RAP absorption at the sub-solar point (dB): vertical two-way / one oblique hop at 10° elevation.**

| Flare | 3.5 MHz | 7 MHz | 14 MHz | 28 MHz |
|---|---|---|---|---|
| M1 | 9 / 51 | 3 / 18 | 1 / 6 | 0.4 / 2 |
| X1 | 19 / 110 | 7 / 39 | 2 / 14 | 0.8 / 5 |
| X10 | 32 / 182 | 11 / 64 | 4 / 23 | 1.4 / 8 |

The oblique column applies 1/sin α to the 10° take-off angle, a factor of 5.8. At 80–110 km, where the absorption occurs, such a ray travels at about 13–14° (P.533 uses the incidence angle at 110 km), and the factor is about 4.0–4.3. The oblique values would then be about a quarter to a third lower in dB [E].

*Validation and criticism of D-RAP.*

- The 2010 validation report could not validate the X-ray component or the frequency-scaling law [T, read: Akmaev et al. 2010]. For proton events it found occasional overestimates of peak absorption by more than a factor of two at high-latitude stations in the American sector, including Thule, and systematic underestimates in the European sector. It called the output "a qualitative indicator" and the ±2 MHz error bars unrealistic. The model has no auroral absorption.
- Errors at 30 MHz scale up by about 5 at 10 MHz and about 15 at 5 MHz, and double for a two-way path (same report).
- For the X1.3 flare of 7 September 2017 on a 357 km path at 6.4 MHz, a physics-based model gives 20–40 dB (25–35 dB above pre-flare at 1500 UT). This agrees with the measurements of Levine et al. (2019), and both exceed D-RAP [P, read: Siskind et al. 2022]. A D-RAP estimate for that geometry in this record is about 7 dB [E].
- A physics-based framework using the Appleton–Hartree relation with an averaged collision frequency reproduced riometer data with a skill score of 0.4 relative to D-RAP [P, abstract: Chakraborty et al. 2021a].
- Noise attenuation at 34 SuperDARN radars during 80 flares (2013–2017) follows a frequency power law with exponent −1.6 over 8–20 MHz [P, abstract: Berngardt et al. 2019].

*Observed time behaviour.*

- SuperDARN ground scatter drops within about 100 s, reaches maximum suppression within a few tens of minutes and recovers over about half an hour [P, abstract: Chakraborty et al. 2018].
- For an X2.1 flare (11 March 2015), signal loss was complete for about 20 min and recovery took about 30 min; a Doppler "flash" of 1–2 min preceded the loss [P, abstract: Fiori et al. 2018].
- An X2.9 flare cut the number of Reverse Beacon Network paths to under 35 % [P, abstract: Frissell et al. 2014].
- For the X9.3 flare of 6 September 2017 (1153 UT), HF recovery over Europe, then in the noon sector, did not begin until about 1300 UT and took almost 2 h. Over the continental United States, then in the dawn sector, 7 and 14 MHz began to recover within 10–15 min and recovered fully within an hour. In the Caribbean the flares of 6, 7 and 10 September caused daytime blackouts with recovery times of tens of minutes to hours [P, read: Frissell et al. 2019].
- Ionosonde total fade-out lasted 15–150 min for flares above M6 in solar cycle 23, depending strongly on solar zenith angle; the minimum echo frequency rose to 2–9 MHz [P, read: Barta et al. 2019].
- Thirty-six years of Kokubunji ionograms give extreme fade-out durations of 1.8–3.6 h (once in 10 years), 4.0–6.8 h (once in 100) and 7.4–11.9 h (once in 1 000) [P, abstract: Tao et al. 2020].
- Mid-latitude radars saw 2–4 h of enhanced attenuation after M- and X-class flares in September 2017 [P, abstract: Bland et al. 2018].

**Polar-cap absorption (solar proton events).**

- Attenuation at HF can exceed 100 dB. Onset is 15 min to several hours after the flare. Events number 0–14 per year, last about two days on average and up to four. Hours of PCA per year are roughly −70 + 4.4 R; the worst surveyed year had 780 h, 9 % of the year. During a storm the absorption can spread to about 55° (Handbook §2.3.2.6, citing Larsen 1974) [B, read].
- D-RAP's algorithm, after Sauer & Wilkinson (2008): 30 MHz absorption is 0.115 [J(>5.2 MeV)]^½ dB by day and 0.020 [J(>2.2 MeV)]^½ dB by night, interpolated over ±10° solar elevation. Minimum event duration is 24.2 log(flux/15) hours, with the integral flux above 10 MeV [T, read]. The documentation does not say whether the 30 MHz values are one-way or two-way. The validation report compares them with riometer absorption, which crosses the D region once, and doubles them for a communication path [T, read: Akmaev et al. 2010]. D-RAP then adds them to its X-ray absorption, whose HAF is defined for a two-way vertical path [E, from the two documents].
  - For an integral flux of 1 000 cm⁻² s⁻¹ sr⁻¹ above those thresholds this gives 3.6 dB (day) and 0.6 dB (night) at 30 MHz, and 19 dB one-way vertical at 10 MHz by day. One oblique hop at a 10° take-off angle multiplies the one-way value by 2/sin 10° = 11.5, giving over 200 dB. With the angle evaluated at 80–110 km (see Table 4) the factor is about 8–8.6 and the result about 150–165 dB. Duration is 44 h or more [E from T].
- September 2017: near-total loss of SuperDARN backscatter for several days; atmospheric-noise attenuation up to 10 dB at 12 MHz in the northern polar cap and 14 dB in the southern [P, abstract: Bland et al. 2018].
- A whole-atmosphere chemistry model reproduces riometer absorption within 0.5 dB poleward of about 66° geomagnetic and about 1 dB equatorward, with the largest errors at twilight [P, abstract: Heino et al. 2019].
- A fixed 60° cutoff overestimates the affected area 90 % of the time, by 15–25 % on average [P, abstract: Heino & Partamies 2020]. Cutoff latitude lags Kp by about 3 h [P, abstract: Neal et al. 2013].

**Auroral absorption.**

- P.533's median allowance L_h peaks at 62.5–72.5° geomagnetic: 16.0 dB in winter (04–07 LT) and 21.4 dB at equinox (07–10 LT) for paths up to 2 500 km (Table 2) [S, read].
- Seven years of 38.2 MHz riometer data at Kilpisjärvi: most absorption is below 0.5 dB; values above 2 dB fall below the 0.1 % occurrence level; the peak is in the pre-noon sector [P, read: Kavanagh et al. 2004]. By inverse-square scaling, 0.5 dB at 38.2 MHz is about 7 dB at 10 MHz and 29 dB at 5 MHz, one way, vertical [E].
- Log absorption range is linear in Kp only for Kp < 4; the hourly magnetic range is a better predictor (R = 0.63) [P, abstract: Fiori et al. 2020].
- Recent Ottawa–Alert evidence is a 2013–2019 scheduled-link reception/outage record at six frequencies from 5 to 14 MHz, alongside 30-MHz riometer measurements. Gillies et al. (2026; Fiori coauthor) §3.1 constructs reception occurrence by assuming transmitter and receiver both operated continuously on days with data; July–August 2016 is excluded because almost no receptions suggested at least one endpoint was not continuously operating. For absorption comparisons (§3.2), 4–24 scheduled pulses per operating hour provide an expected-pulse denominator; across the retained dataset 62% of expected pulses were unreceived, whether from absorption or lack of a supported propagation path. The paper reports additional loss probability in bins with remote riometer absorption above 0.5 dB (weighted averages 26–32% for most frequencies, 19% at 14 MHz; §4), relative to baseline loss, not a universal absorption-caused outage probability. The riometers are all west of the link, about 900–1,700 km from modeled D-region pierce points (§§2, 3.4, 4); the paper links the weak transfer to spatial extent/location of absorption regions and finds no sharp absorption cutoff. This is useful absorption-conditioned service evidence, not coherent complex-channel data or a direct local attenuation measurement [P; full paper, Gillies et al. 2026, §§2–4, DOI 10.1029/2025SW004906].

**Geomagnetic storms: MUF depression.**

- The average storm has a positive phase followed by a negative phase; poleward of about 35° there is often little positive phase. Storms last days (Handbook §2.6.1.1) [B, read].
- September 2017 (Kp 8+, SYM-H −146 nT): 14 MHz and then 7 MHz were wiped out from about 1200 UT on 8 September; effective bands stayed suppressed for 12–15 days [P, read: Frissell et al. 2019].
- May 2024 (Dst −412 nT): foF2 and vertical TEC over Italy fell to about 2 MHz and 4 TECu, about 70 % below quiet values, for two whole days (11 and 13 May). Ionograms were also lost to flare absorption [P, read: Spogli et al. 2024].
- At high latitudes the F2 layer was completely absent on 11 May 2024 [P, abstract: Themens et al. 2024].
- The May 10–13 Asian-sector continuous-chirp study supplies direct *oblique-sounding* evidence for route changes and outages, not a modem fading distribution. On the Novosibirsk–Irkutsk, Magadan–Irkutsk and Magadan–Novosibirsk paths, storm main/recovery phases brought lower maximum observed frequencies and increased absorption; the authors report long blackout intervals, loss of regular 1F2/2F2 returns, intermittent F1/Es returns and diffuse/scattered returns near the shifted main ionospheric trough. On Novosibirsk–Irkutsk the initial 1F2/2F2 spread-F and diffuse returns were observed from 17:50 UT May 10; on Magadan–Novosibirsk the path crossed auroral and trough regions. Their source describes about 25,000 processed ionograms; vertical/NVIS sounding typically stepped at 1 min or less (with quick-mode examples down to 15 s), while the oblique-sounding path schedule is 5 min. It reports derived HFC/MOF histories and selected ionogram images, not raw voltage/IQ, modem C/N0/BER, a no-return denominator suitable for link probability, or joint delay–Doppler records. Published curves/ionograms may support digitized event-specific replay of mode/availability state, but cannot be converted to waveform replay. Preserve missing transmitter stations (Norilsk and Khabarovsk were unavailable for technical reasons) and the three actual routes when constructing any event scenario [P; read: Ponomarchuk et al. 2025, May 2024 storm study].
- In both the May 2024 and 10 October 2024 (Dst −335 nT) storms, ionisation stayed very low for more than a full day [P, abstract: Pierrard et al. 2025].
- The empirical STORM correction (75 stations, 43 storms) works at mid-latitudes in summer and equinox, but adds little in winter and near the equator [P, abstract: Araujo-Pradere et al. 2002].

**Structural features.**

- *Mid-latitude trough.* Near 57° magnetic latitude at midnight for moderate Kp, about 10° wide, moving equatorward 1.8° ± 0.4° per unit Kp. Density inside is from under 0.1 to almost 1 times that outside (Handbook §2.3.2.7) [B, read].
- *Pre-dawn minimum.* See Table 1.
- *LUF above MUF.* See the Table 2 cases.
- *Sporadic-E blanketing.* P.1239 maps fbEs deciles, but no HF outage statistics were found. The May 2024 oblique-sounding study reports Es and F1/F2 mode changes during the storm; Spogli et al. 2024 is a separate Italian ionosonde study and should not be used as the source for those Asian oblique-path observations [P; read: Ponomarchuk et al. 2025; Spogli et al. 2024].

**Occurrence over a solar cycle.**

| Event class | Rate per 11-year cycle | Typical duration | Source |
|---|---|---|---|
| Flare M1 or more (R1) | 2 000 (950 days) | occasional loss of contact | NOAA scales [T, read] |
| Flare X1 or more (R3) | 175 (140 days) | about an hour | NOAA scales |
| Flare X10 or more (R4) | 8 (8 days) | one to two hours | NOAA scales |
| Fadeout, moderate / severe | 123 / 5 | mean 68 / 132 min | Fiori et al. 2022 [P, abstract] |
| Proton event 10 pfu or more (S1) / 1 000 pfu (S3) / 10 000 pfu (S4) | 50 / 10 / 3 | S3: polar HF degraded; S4: polar HF blackout over several days | NOAA scales |
| PCA, moderate / severe | 24 / 13 | mean about 8 h / 1.6 days | Fiori et al. 2022 |
| Auroral absorption, moderate / severe | 40 / 3 | mean 5.1 h / 12 h | Fiori et al. 2022 |
| Post-storm MUF depression, moderate / severe | 200 / 56 | mean 5.5 h / 8.5 h | Fiori et al. 2022 |
| Storm Kp 7 (G3) / Kp 8 (G4) / Kp 9 (G5) | 200 / 100 / 4 | G5: HF may be impossible for one to two days | NOAA scales |

Fiori et al. (2022) add that the combined moderate-or-severe impact reached at most 163 and 78 days per year in solar cycles 22 and 23, with 70 % of events at solar maximum. When the thresholds were translated into expected HF impact, polar-cap absorption and fadeouts agreed with each other, but auroral absorption gave a 2–3 times higher expected impact [P, abstract].

Their moderate and severe levels are the thresholds of the ICAO space-weather advisory service. As restated by NOAA, they are an X-ray flux (0.1–0.8 nm) of 10⁻⁴ and 10⁻³ W m⁻² (X1 and X10); polar-cap absorption of 2 and 5 dB on a 30 MHz riometer; auroral absorption at Kp 8 and 9; and post-storm MUF depression of 30 % and 50 % against a 30-day running median of foF2 [T, read: NWS Instruction 10-1102, 2023]. That Fiori et al. used the ICAO thresholds is stated in their abstract; the values were read in the NOAA instruction, not in the paper.

**Solar cycle 25.** Active region 13664 produced 12 X-class flares on 8–15 May 2024, up to X8.7, plus ground-level enhancement #74; the storm, with Dst −412 nT, was the sixth largest since 1957 [P, read: Hayakawa et al. 2025]. NOAA region 3842 produced an X9.0 flare at 1218 UT on 3 October 2024, rated R3 [T, read: NOAA SWPC 2024]. Amateur receivers of the HamSCI network saw elevated HF Doppler shifts and more signal interruptions than usual during the May 2024 storm, and multi-hour blackouts in its recovery phase [G, abstract: Potter & Frissell 2026; Potter et al. 2026, not refereed]. No peer-reviewed HF-absorption analysis of the 2024–2025 X-class flares, and no solar-cycle-25 statistics of fadeouts or polar-cap absorption, were found.

### 5. MF and low-HF night skywave

- **Sunset and sunrise.** The hourly loss factor is 24.9, 12.4, 5.7, 2.8 and 0.3 dB at −1, 0, +1, +2 and +4 h from sunset. It is 0.4, 2.2, 9.6 and 28.3 dB at −3, −1, 0 and +1 h from sunrise [E from P.1147-4 Appendix 1]. That is about 19 dB over the two hours from 1 h before to 1 h after sunset, and 26 dB over the same span around sunrise. The curve is most accurate near 1 MHz at low solar activity (Annex 1) [S, read].
- **Daytime residual.** Annual-median midday field is about 43 dB below the value six hours after sunset. Winter months average about 10 dB above the annual median; the winter-to-summer ratio can exceed 30 dB; 10 % of days are about 13 dB above the median (P.1147-4 Annex 2) [S, read]. The Handbook gives 42.5–45 dB, and 20 dB above the median on 1 % of days (§5.4.5) [B, read].
- **Season at night.** Strongest in spring and autumn, weakest in summer and winter, summer more so. The swing is up to 15 dB at the low end of the MF band and about 3 dB at the top (Annex 1) [S, read].
- **Solar and geomagnetic dependence.** Solar activity lowers night field strength, more so at higher latitude and negligibly below about 40°. The effect two hours after sunset is about twice that at six hours (in dB). In Alaska the field peaks in the year after sunspot minimum (Handbook §5.2.5) [B, read].
- **Frequency.** United States data show 1 530 kHz typically 5–10 dB stronger than 700 kHz two hours after sunset, and about 3 dB at six hours, which the P.1147 frequency treatment does not reproduce (Handbook §5.1.2.2) [B, read].
- **160 m (1.8–2.0 MHz) practice** [G, read: Luetzelschwab, K9LA]:
  - Ray tracing gives 12.5 dB of absorption per F hop at night (9.8 dB for a low-angle E hop), hops of about 2 200 km and 3 dB per ground reflection, so about 20 dB per hop.
  - With 1 kW and 0 dBi antennas the received power is −60 dBm after one hop and −100 dBm after three (6 600 km). That is a basic loss of 120 and 160 dB [E]. Multi-hop is limited to about 7 000–10 000 km against quiet-rural noise.
  - Ducting in the night E–F valley is proposed for longer paths. An 11 000 km duct estimate is −71 dBm (loss about 131 dB [E]), some 60 dB better than multi-hop.
  - In one case (Sudan to Indiana, 11 044 km, March 2003) the signal was audible on two nights for about 20 minutes each.
  - Modelled night absorption is about 9 dB (winter) to 12 dB (summer) per hop and nearly independent of sunspot number.
  - High-latitude paths need a quiet field: all contacts from Italy to the North American west coast in one study occurred at K < 2.
  - The night E layer (foE about 0.3 MHz) returns 1.8 MHz rays below about 9.5° elevation; steeper rays take F hops.
  - The X wave is heavily absorbed, so the usable O wave is strongly elliptical, with its major axis along the field.
- **160 m polarisation, peer-reviewed.** WSPR receiving experiments show signal-to-noise ratio changing with the received polarisation, possibly linked to the gyrofrequency [P, abstract: Vanhamel et al. 2022].
- **Cross-modulation.** Measured modulation transfer at LF/MF in middle latitudes is below 7 %. A disturbing transmitter at 1.6 MHz would need about 1.4 MW e.i.r.p. at 150 km to become a nuisance, and above 5 MHz even HF broadcast powers are unlikely to matter (P.532-1) [S, read].
- **Eclipses.** On 21 August 2017, effects lasted ±0.3 h at 1.8 MHz, ±0.75 h at 3.5 and 7 MHz and ±1 h at 14 MHz [P, abstract: Frissell et al. 2018]. On 8 April 2024, a CHU signal at 7.85 MHz rose tenfold at one site [P, abstract: Usis et al. 2026], and the SuperDARN skip distance lagged maximum obscuration by about 45 min, which SAMI3 did not reproduce [P, abstract: Kunduri et al. 2024]. In the annular eclipse of 14 October 2023, 1- and 2-hop ground-scatter skip distances followed the bottomside density, which lagged peak obscuration by about 30 min [P, abstract: Thomas et al. 2024].

### 6. Marginal and exotic modes

| Mode | Excess loss relative to the normal mode | Character | Occurrence | Source |
|---|---|---|---|---|
| Ground side- and back-scatter above the MUF | 25–40 dB below the great-circle mode | fading faster than 1 per second; variable azimuth | suggested dominant above-MUF mechanism to 4 000 km | P.2011-1 §2.2 [S, read] |
| Reflection from F-region patches above the MUF | P.533: 0–62 dB; observed about 30 dB drop | night, mid-latitude | regular on Ottawa–Boston at 7.335 MHz when foF2 fell below the 5.7 MHz needed (signal −80 to −110 dBW) | McNamara et al. 2008 [P, abstract] |
| Ionospheric roughness | — | — | only about 60 kHz above the basic MUF in theory | P.2011-1 §2.1 |
| Direct E-region scatter | intensity ∝ f^−7.5 above 30 MHz | — | to about 2 000 km | P.2011-1 §2.6 |
| Sporadic E | Γ of 4–23 dB at 1 500 km for f/foEs of 2–5 | — | strongly seasonal; formula valid to 2 600 km per hop | P.534-6 [S, read] |
| Chordal and trans-equatorial | fewer absorption passes; one long-path case 36 dB better than multi-hop | coherent by day, scatter-like after sunset; azimuth deviations to ±50° | equatorial; up to 60 MHz by day | Nielson & Crochet 1974 [P, abstract]; P.2011-1; Luetzelschwab [G] |
| Round-the-world and ducted | low attenuation | preferred occurrence periods; twilight-zone path | — | Toman 1979 [P, abstract] |
| Grey-line at 7 MHz | — | reception clustered at sunrise or sunset; asymmetric by direction | New Zealand–UK, 2017 WSPR data | Lo et al. 2022 [P, abstract] |
| Long path over 9 000 km | Table 2 | delay spread assumed 3 ms at 7 000 km rising to 5 ms at 20 000 km | — | P.533-14 §10.2.3 [S, read] |
| 160 m E–F valley duct | about 60 dB better than multi-hop (model) | 20-minute openings | rare | Luetzelschwab [G, read] |

No source gave occurrence statistics for scatter modes on the 4 W scale relevant here.

### 7. What this means for the modem

C/N0 = P_t + G_t + G_r − L_b − F_a + 204 dB-Hz. With 4 W (6 dBW) and 0 dBi antennas, C/N0 = 210 − L_b − F_a [E]. Median man-made noise from P.372-18 (F_am = c − d log f) gives F_a for rural / quiet-rural sites of 60 / 46 dB at 1.8 MHz, 52 / 38 at 3.5 MHz, 44 / 29 at 7 MHz, 36 / 21 at 14 MHz and 27 / 12 at 28 MHz; galactic noise (26 dB at 14 MHz, 19 dB at 28 MHz) sets the floor above about 10 MHz [S, read]. Atmospheric noise can exceed these on the low bands at night; chapter 50 covers it.

**Table 5. C/N0 (dB-Hz) for 4 W and 0 dBi antennas, rural man-made noise [E].**

| Case | L_b (dB) at 3.5 / 7 / 14 MHz | C/N0 at 3.5 MHz | C/N0 at 7 MHz | C/N0 at 14 MHz |
|---|---|---|---|---|
| Night, 1 000 km, in band | 115 / 121 / — | 43 | 45 | — |
| Day, 1 000 km, solar minimum | 142–150 / 133–136 / 136–139 | 8 to 16 | 30 to 33 | 36 to 38 |
| Day, 3 000 km | — / — / 145–156 | — | — | 19 to 30 |
| Dark 10 000 km (January) | 144 / 144 / — | 14 | 22 | — |
| Sunlit 10 000 km | — / — / 176–180 | — | — | −6 to −2 |
| L_b = 180 dB (the GUI's path-loss figure read as a basic loss; see the comparison section) | 180 at any band | −22 | −14 | −6 |
| P.533 above-MUF floor | 185–194 at any band | −27 to −36 | −19 to −28 | −11 to −20 |

At a quiet-rural site a basic loss of 180 dB gives −8, +1 and +3 dB-Hz at 3.5, 7 and 14 MHz. Subtract 11–19 dB for the combined lower decile (section 2), and up to about 10 dB more for prediction error at one standard deviation.

Time scales to expect:

- **Seconds to minutes:** fading (chapters 31–33); fadeout onset in about 100 s.
- **Tens of minutes to hours:** fadeout (15–150 min; mean 68 min at the moderate threshold); MF terminator transitions (about 19–26 dB in two hours); travelling disturbances (20 min to 2.5 h); auroral absorption events (about an hour each, 5–12 h as events).
- **Hours to days:** post-storm MUF depression (mean 5.5–8.5 h; one to two days in superstorms); polar-cap absorption (8 h to four days).
- **Days to weeks:** post-storm absorption (up to ten days or more); suppressed bands (12–15 days in September 2017).

A 10-minute or 96-minute bit is as long as a fadeout or a terminator transition, so the level is not stationary within a symbol in those conditions.

## Moderate → adverse → worst-case parameter table

| Parameter | Moderate | Adverse | Worst case / outage |
|---|---|---|---|
| Basic loss, one hop or NVIS, in band (monthly median) | 103–137 dB (night, 1.8–7 MHz); 118–147 dB (day, best band) [E] | 142–180 dB (day, 3.5 MHz, 1 000 km) [E] | over 190 dB (day, 3.5 MHz, 3 000 km); 276 dB or more at 1.8 MHz [E, outside validity] |
| Basic loss, 10 000 km | 141–150 dB (dark path) [E] | 172–183 dB (sunlit, best band) [E] | over 200 dB off the usable window [E] |
| Basic loss, near-antipodal | 129–150 dB [E] | 165–179 dB [E] | f_L above f_M: no in-window band [E] |
| MF night loss at 1.7 MHz | 110 dB (1 000 km, Φ = 40°) [E from S] | 146 dB (3 000 km, Φ = 60°) [E from S] | plus 22 dB at solar maximum and up to 28 dB per terminal polarisation loss [E from S] |
| MF day minus night | about 33 dB (winter month) [E from S] | 43 dB (annual median) [S] | about 60 dB or more implied in summer [E from S] |
| D-region absorption, quiet Sun | 0–3 dB (night, 3.5 MHz and above) [E] | 15–29 dB (7 MHz, noon, 1 000 km) [E] | 66–163 dB (3.5 MHz, noon, 3 000 km) [E] |
| Flare absorption | 1–2 dB vertical, 6–11 dB per low-angle hop at 14 MHz (M-class) [E from T] | 20–40 dB at 6.4 MHz (X1.3) [P] | total fade-out 15–150 min; hours in extremes [P] |
| Polar-cap absorption (high-latitude paths) | none | tens of dB (integral flux 1 000, night, one low-angle hop at 10 MHz) [E from T] | over 100 dB for one to four days [B] |
| Auroral loss (median allowance) | 0 dB below 42.5° [S] | 0.3–7.5 dB at 52.5–62.5° [S] | 16–21 dB median at 62.5–72.5°; decile 30–42 dB [S] |
| Day-to-day signal, lower decile | 8 dB [S] | 12–13 dB near the MUF [S] | 16–17 dB at 60° or above [S] |
| Within-hour fading, lower decile | 8 dB [S] | 8 dB | 8 dB (plus mode-specific fading) |
| Monthly-median prediction error, 1σ | 7–8 dB (under 2 000 km) [S] | 10.5 dB overall [S] | 13.6–14.5 dB (3 000–3 999 and 5 000–6 999 km); 16.0 dB (over 60°) [S] |
| foF2/MUF daily factor | 0.70–0.90 lower decile at 40–50° latitude [S] | 0.60–0.70 [S] | −70 % for two days (May 2024); F2 absent at high latitude [P] |
| Noon to pre-dawn MUF ratio | 1.2–1.3 (summer) [E] | 2.6 (winter, solar minimum) [E] | 3.6 (winter, solar maximum) [E] |
| Above-MUF excess loss | 0–23 dB to f/f_b = 1.4 (P.533 formula) [E from S] | 53 dB (Suessmann) [S] | 62 dB cap (P.533); 80 dB (Gibson–Bradley) [S] |
| C/N0, 4 W, 0 dBi, rural noise | 30–45 dB-Hz [E] | −6 to +16 dB-Hz [E] | below −20 dB-Hz, or no signal [E] |
| Outage duration | none | 1–2 h (flare); 5–9 h (storm depression) [P] | days (PCA, superstorm); weeks of suppressed bands [P, B] |

No source observed all the worst-case entries together. Flares, proton events and storms did coincide in September 2017 and May 2024, but their effects fall on different regions: the sunlit hemisphere, the polar caps, and auroral and middle latitudes.

## How the literature models or simulates this

- **ITU-R P.533-14 and ITURHFProp.** Monthly-median field strength, MUF and reliability for 2–30 MHz. Calibrated to the D1 data bank, which is distributed with the reference code [G, read: repository listing]. No flare, proton or storm-day behaviour. It warns of distortions in the equatorial evening sector (§1). An equatorial-scatter attachment gives a probability of occurrence.
- **P.842-5.** Reliability from decile deviations of signal and noise, combined by root-sum-square.
- **VOACAP / IONCAP / ICEPAC.** Ray-hop modes to about 10 000 km and a forward-scatter long-path model, with "Method 30" smoothing between them [G, read: Lane 2005]. Signal distributions come from the excess-system-loss tables. Lane judges VOACAP accurate for undisturbed conditions and ICEPAC unreliable at high latitudes.
- **CCIR Supplement to Report 252-2 (1980).** The most explicit term-by-term loss model; superseded but still the clearest source for individual mechanisms.
- **P.1147-4, and the FCC/Wang method.** MF night skywave. The Wang method fits long paths and United States data better [B, read: Handbook §5.3.3].
- **NOAA D-RAP.** Real-time flare and proton absorption maps; archive at one-minute cadence since September 2009 [T, read: NCEI readme].
- **Physics-based absorption.**
  - SiMIAN integrates Sen–Wyller absorption along rays through NRLMSISE-00 [P, abstract: Pederick & Cervera 2014].
  - Naval Research Laboratory D/E-region modelling shows a primary low-latitude absorption peak in the lower E region and a secondary one below 75 km; nitric oxide near 80 km drives mid- to high-latitude changes [P, abstract: Siskind et al. 2017].
  - Zawdie et al. (2017) on absorption formulations [P, record only].
  - Flare models: Levine et al. (2019); Siskind et al. (2022); Chakraborty et al. (2021a).
- **Proton and auroral absorption.** Sauer & Wilkinson (2008); riometer assimilation (Rogers & Honary 2015) and twilight modelling (Rogers et al. 2016) [P, records only]; cutoff models (Dmitriev et al. 2010; Neal et al. 2013); Kp-quadratic auroral statistics (Kavanagh et al. 2004); hourly-range index (Fiori et al. 2020).
- **Ionospheric specification.** IRI-2020 (monthly averages, with storm option) [P, abstract: Bilitza et al. 2022]; E-CHAIM and A-CHAIM for high latitudes; the near-real-time polar HF model of Warrington et al. (2016), which assimilates ionosonde, TEC, proton and X-ray data.
- **Data sets for validation.** D1 data bank; riometers; SuperDARN ground scatter and noise; ionosonde minimum-frequency records; amateur networks. The amateur networks were designed to monitor path openings, not for finely calibrated measurements; timing is uncertain by about ±1 s, frequency by about ±1 Hz, and reports come every 1–2 min [P, read: Frissell et al. 2023]. That their reports carry no separate noise measurement is not stated there (not confirmed on re-check).

## What a high-fidelity simulation would need

These are options with their limits, not a design; better approaches may exist.

1. **A climatological median.** P.533 or VOACAP output for the chosen path, hour, month and R12 (version 1). *Limit:* monthly median only; errors of 7–16 dB at one standard deviation; unreliable at high latitudes and below 2 MHz.
2. **A scenario bias.** One random offset per simulated circuit-month drawn with about 10 dB standard deviation, to stop the simulator treating the prediction as truth. *Limit:* the D1 statistics mix paths and epochs; the true error is not Gaussian.
3. **Day-to-day level.** A split-normal draw from the P.842 deciles, or from the older CCIR tables where season and local time matter. *Limit:* no day-to-day correlation is given by either source; storm sequences persist for days.
4. **Daily MUF.** A daily foF2 factor from the P.1239 deciles, with an above-MUF loss law. *Limit:* the available laws differ by tens of dB; the choice should be a parameter, not a constant.
5. **Within-hour structure.** Hand over to the dispersion models, keeping the 5/8 dB deciles as a check.
6. **Event layer.**
   - Flares: arrival rate by class and cycle phase; time profile tied to the X-ray flux with a rise of minutes and decay of tens of minutes to hours; absorption scaled by frequency (exponent 1.5–2) and zenith angle. *Limit:* D-RAP magnitudes are uncertain by a factor of several in dB.
   - Proton events: day/night ratio of about 4–6 [B, T], latitude cutoff moving with Kp, durations of days.
   - Auroral absorption and storm MUF depression: Kp-driven, hours to days.
   - An alternative is to replay recorded drivers (X-ray and proton flux, Kp, ionosonde foF2) for documented periods such as September 2017, May 2024 and October 2024. *Limit:* replay covers few events.
7. **MF module.** P.1147 with its transition curve, latitude and solar terms, night-to-night log-normal spread and the daytime residual. *Limit:* 1.7–2 MHz is extrapolation; long paths are under-predicted.
8. **Rare enhanced modes.** Occasional short openings 30–60 dB above the multi-hop prediction on 160 m and on long paths. *Limit:* only anecdotal statistics exist.

**Observables to validate against:** D1-style monthly medians and their spread; ionosonde minimum-frequency fade-outs; riometer absorption; SuperDARN echo loss; amateur-network link counts and signal-to-noise distributions (with independent noise calibration); time-signal carrier amplitude records.

## Comparison with the modem's presets

The presets of [00](00-method-and-modem-context.md) section 5 are pairs of excess delay and Doppler; none carries a level. In every preset the received level is the one constant gain set by transmit power and path loss, with at most one ramped attenuation interval as a lower-level option. The comparison below is therefore with the level and noise conventions, and with the presets whose latitudes this chapter covers. The None preset is the static baseline, and the Sub9 and EME presets concern other chapters. Conversions use section 3.1 of 00: the GUI's path loss at −164 dBm/Hz equals L_b + F_a − 13.5 dB for 4 W e.m.r.p. from an ideal short monopole. In Table 5's convention (4 W e.i.r.p., isotropic antennas) the same 20 dB-Hz corresponds to L_b + F_a = 190 dB instead of 193.5 dB [E].

| Preset or convention | Setting | What this chapter establishes |
|---|---|---|
| HF best, moderate, worst (0.1 Hz/0.5 ms; 10 Hz/6 ms; 30 Hz/7 ms) | constant level | The level is not constant: within-hour lower decile 8 dB, day-to-day lower decile 8–17 dB, monthly-median prediction error 7–16 dB at 1σ (sections 2–3). Absorption at 7 MHz on 1 000 km rises from 0–2 dB at night to 15–29 dB at noon (Table 1). Fadeouts, storms and proton events come on top (section 4). |
| Aurora best and worst (10 Hz/3 ms; 55 Hz/11 ms) | constant level | At these latitudes the chapter's largest level penalties apply: a median auroral allowance peaking at 16–21 dB at 62.5–72.5° geomagnetic, upper-decile loss deviations up to 42 dB at 60–65°, a 16 dB prediction error above 60°, polar-cap absorption of tens to over 100 dB lasting hours to four days, and ICEPAC errors up to about 200 dB on a disturbed auroral path. |
| One attenuation interval with cosine ramps | one event | Can stand for one fadeout (onset in about 100 s, complete loss for about 20 min, recovery over about 30 min; total fade-out 15–150 min) or one MF terminator transition (19–26 dB in two hours). It cannot represent the diurnal absorption cycle, event recurrence (about 123 moderate fadeouts per cycle) or multi-day events (polar-cap absorption; two days of 70 % foF2 depression in May 2024). |
| Noise −164 dBm/Hz (F_a = 10 dB) | white, fixed | Below every HF noise level of section 7: F_a of 12–60 dB (quiet rural at 28 MHz to rural at 1.8 MHz) and galactic noise of 19–26 dB at 14–28 MHz. |
| 180 dB at −164 dBm/Hz, 4 W, 30 MHz (manual rows 4–7) | 20 dB-Hz | L_b + F_a ≈ 193.5 dB. With galactic noise near 30 MHz (F_a ≈ 18 dB) that is L_b ≈ 175 dB, about a 28 MHz, 1 000 km path at noon at solar minimum, slightly above its MUF (171–181 dB). A 3 000 km daytime path at solar maximum (143–158 dB) is 17–32 dB easier; a sunlit 10 000 km path at R12 = 150 (180–181 dB) about 5 dB harder; the P.533 above-the-MUF residual (185–194 dB) 10–19 dB harder. At a rural site (F_a ≈ 27 dB) each is about 8 dB harder [E]. |
| The same figure at lower HF, rural site | 20 dB-Hz | L_b ≈ 141 dB at 3.5 MHz, 150 dB at 7 MHz and 158 dB at 14 MHz. That is a daytime 1 000 km path on 3.5 MHz at solar minimum (142–150 dB, 1–9 dB harder), a daytime 1 000 km path on 7 MHz at solar maximum (142–147 dB, 3–8 dB easier), or a daytime 3 000 km path on 14 MHz (145–156 dB, 2–13 dB easier). Night in-band paths (103–137 dB) are much easier in basic loss, but night-time atmospheric noise raises F_a (chapter 50) [E]. |
| 180 dB at −164 dBm/Hz near 1 MHz (manual rows 2–3 are at 500 kHz) | 20 dB-Hz | With F_a of the order of 80 dB at night near 1 MHz (00 section 3.1), L_b ≈ 113 dB. Night skywave at 1.7 MHz has L_b of 90–113 dB out to 1 000 km at Φ ≤ 50° (Table 3), and about 11 dB less at 500 kHz, since the P.1147 field hardly depends on frequency within MF. A night skywave path can therefore reach that level; at midday it is about 43 dB weaker [E from S]. |

## Contradictions, open questions, and things not found

**Contradictions.**

1. *P.533-14 text against its reference code.* Printed eq. 20, unchanged since P.533-13 (07/2015), sums the absorption term over all m penetration points, two per hop, with no hop factor or normalisation [S, read]. P.533-12 (09/2013) instead multiplied by the hop count n and averaged over k control points [S, read]. The public reference code (P533 version string 14.2, repository state of May 2025) keeps that normalisation on penetration points: it averages the per-point terms over the 2n points, multiplies by n, and takes f_L from the control points rather than at each penetration point [G, read]. Where the per-point terms are equal, the printed form therefore gives twice the code's absorption. Neither text says which form is intended. Because L_z is fitted against D1 with an implementation of the method, the printed form should not be combined with the published L_z unless it was the form used in that fit, which neither source documents [E]. The code's constants were changed on 3 June 2020 from the printed L_z = 8.72 dB and L_y = −0.14 dB to 9.14 dB and −0.17 dB; P.533-12 had 10.3 and −0.9 dB [S, G, read: commit history]. Its above-the-MUF laws also differ from the text: for F2 modes, 36(f/f_b − 1)^½ + 5 dB capped at 60 dB up to 3 000 km and 70(f/f_b − 1) + 8 dB capped at 80 dB beyond; for E modes, 46(f/f_b − 1)^½ + 5 dB capped at 58 dB, against the printed 130(f/f_b − 1)² capped at 81 dB [G, read].
2. *Above-MUF loss.* 23 dB (P.533 text), 28 dB (reference code), 53 dB (Suessmann) and 80 dB (Gibson–Bradley) for the same case, and 6 dB if Hunt's fit is read as printed in Report P.2011-1.
3. *Frequency exponent of absorption.* 2 in magnetoionic theory and P.533; 1.5 in D-RAP; 1.6 observed by Berngardt et al. (2019).
4. *D-RAP bias.* Too little flare absorption (Levine, Siskind, Chakraborty). For proton events, occasional overestimates of peak absorption by more than a factor of two at high-latitude stations in the American sector, and systematic underestimates in the European sector (Akmaev). D-RAP also adds proton absorption that is compared with one-way riometer data to X-ray absorption defined for a two-way vertical path [E].
5. *Antipodal focusing cap.* 15 dB (P.533-14) against 30 dB (CCIR 1980).
6. *MF night absorption and the solar cycle.* Ray-tracing in the amateur literature finds it constant; ITU-R measurements show a reduction growing with latitude, and the CCIR night formula carries a factor (1 + 0.015 R12).
7. *160 m beyond 10 000 km.* About 131 dB (duct estimate), 142 dB (P.1147 at low latitude), 171 dB (P.533 long-path formula at its night floor) and 200–218 dB (multi-hop ray tracing): a spread of some 60–90 dB.
8. *Daytime below 2 MHz.* The P.533 absorption law extrapolated to 1.8 MHz gives 173–272 dB at 1 000 km. P.1147's 43 dB day–night difference implies about 153 dB. The secant-law extrapolation should not be used there.
9. *Variability treatments.* P.842 (by f/MUF), the CCIR tables (by season, time and latitude) and the IONCAP tables (1965 data) are three different descriptions of the same spread.
10. *Sunspot-number version.* P.1239 maps need version-1 numbers; modern lists are version 2.

**Not found or not obtained.**

- Full texts of Rishbeth & Mendillo (2001), Sauer & Wilkinson (2008), Fiori et al. (2022), Rogers & Honary (2015), Pederick & Cervera (2014), Zawdie et al. (2017), Levine et al. (2019), Witvliet & Alsina-Pagès (2017), Vanhamel et al. (2022) and Tao et al. (2020). Publisher sites refused automated access or no open copy was found; abstracts or records were used and are marked.
- The books by Davies (1990), McNamara (1991), Goodman (1992) and Hunsucker & Hargreaves (2003). No number here is attributed to them directly.
- The threshold table of Fiori et al. (2022) itself. The thresholds are the ICAO ones (their abstract), and the values were read in NWS Instruction 10-1102.
- Any peer-reviewed validation of VOACAP or P.533 signal levels against WSPR, Reverse Beacon Network or PSKReporter data. A further bounded search in 2026 found only informal comparisons (web logs, workshop slides), which were not read. Chartier et al. (2023) remains the nearest.
- Signal-level results from the October 2023 annular eclipse; only skip-distance results were found (Thomas et al. 2024).
- Peer-reviewed HF-absorption analyses of the October 2024 and 2025 X-class flares. The X9.0 flare of 3 October 2024 is confirmed by NOAA SWPC.
- Solar-cycle-25 statistics of fadeouts and polar-cap absorption.
- A primary source for IONCAP's absorption expression, which is therefore not given. George & Bradley (1974) and the IONCAP theory were reached only through the CCIR Supplement, Lane (2005) and Dandekar (1985). DTIC reports were not reachable.
- Measured statistics of 1.8–2.0 MHz loss beyond 10 000 km. The 131–218 dB bracket of contradiction 7 rests on model estimates and one anecdotal reception; nothing better was found.
- Outputs of the ITU reference software for Table 1's paths. The software was not run; the only reference output compared, the repository's sample report, lies outside Table 1's geometry.
- Occurrence statistics for sporadic-E blanketing of HF circuits and for scatter modes.

**Open questions.**

- How correlated are day-to-day level deviations from one day to the next, and across bands?
- What is the real distribution of loss above the MUF for a receiver that can use signals 60 dB down?
- How often do 160 m ducted openings occur, and for how long?

**Current follow-up disposition (13 November 2026).** The May 2024 Asian-sector paper is now inspected as a full paper, not an abstract-only report: it is event-specific ionogram/MOF evidence, with about 25,000 processed ionograms, 5-minute oblique sounding, and selected vertical/NVIS sequences at 1 minute or faster. It does not release IQ or a calibrated received-link outage denominator. The available article figures make digitization of event-specific route/mode presence possible; waveform replay and modem outage-probability estimation are not supported. A new 2026 Ottawa–Alert study reports narrowband Barker-burst receive parameters for decoded transmissions and shows why nearby vertical riometer absorption is an imperfect oblique-path attenuation proxy. Its measurement cadence, geometric displacement and decode conditioning must be retained; it is not a coherent HF channel sounding source.

The follow-up used the exact topic searches `Ponomarchuk May 10-13 2024 continuous chirp HF oblique sounding May storm Novosibirsk Irkutsk data availability`, `"continuous chirp" "May 2024" ionosphere Magadan Novosibirsk oblique sounding`, `"10.7910/DVN/RGDNR1"`, and `"The Reliability of HF Propagation Links Over Canada During Absorption Events"`. Primary routes included the full Ponomarchuk PDF, publisher article records, and the Harvard Dataverse public dataset API. API metadata for QAN–ALE reports a released 2024-01-09 version 1, one unrestricted ZIP (221,380,570 bytes), and CC0 1.0. The archive was downloaded to temporary storage, its MD5 matched the API checksum, ZIP CRC validation passed, and its inventory contained 5,588 MATLAB 5.0 files only. Six sampled MAT headers/scalar and vector fields contain derived reception observables (including SNR, signal power, TOF, Doppler and spreads, and arrival angles), not I/Q arrays. The metadata's filename template (`ddAle_Ott_...`) differs from the actual archive names (`ddAle_Qan_...`); actual file dates span 2012-07-06–2016-09-10. The per-detection sample does not contain pulse-by-pulse no-reception rows, and file presence/absence alone is not a calibrated attempt denominator. The paper-declared dataset also includes below-zero-SNR and 18.4-MHz records; sample calibration/units, whole-corpus field consistency, event holdouts, and complete attempt/operational masks have not been verified. The Dataverse HTML page's robot-verification response was an earlier access-route limitation; the public API and file download now succeeded. The open full text of Gillies et al. 2026 was also inspected (§§2–4); its assumed scheduled-pulse denominator and remote-riometer-conditioned additional-loss results are described above. This is a scoped retrieval and sample inspection, not exhaustive corpus validation or a systematic literature review.

## References

ITU-R texts were downloaded from itu.int and read unless stated.

**Standards and ITU-R reports [S]**
- Rec. ITU-R P.533-14 (08/2019), *Method for the prediction of the performance of HF circuits*. In force (checked 2026-11-13). Read. https://www.itu.int/rec/R-REC-P.533/en
- Rec. ITU-R P.533-12 (09/2013) and P.533-13 (07/2015), superseded. Read for eq. 20, L_z and L_y. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.533-12-201309-S!!PDF-E.pdf ; https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.533-13-201507-S!!PDF-E.pdf
- Rec. ITU-R P.842-5 (09/2013), *Computation of reliability and compatibility of HF radio systems*. Read.
- Rec. ITU-R P.1239-4 (08/2023), *ITU-R reference ionospheric characteristics*. Read.
- Rec. ITU-R P.1240-2 (07/2015), *ITU-R methods of basic MUF, operational MUF and ray-path prediction*. Read.
- Rec. ITU-R P.1147-4 (08/2007), *Prediction of sky-wave field strength at frequencies between about 150 and 1 700 kHz*. In force (checked 2026-11-13). Read.
- Rec. ITU-R P.534-6 (09/2021), *Method for calculating sporadic-E field strength*. Read in part.
- Rec. ITU-R F.339-8 (02/2013), *Bandwidths, signal-to-noise ratios and fading allowances in HF fixed and land mobile radiocommunication systems*. Read.
- Rec. ITU-R P.1148-1 (05/1997), *Standardized procedure for comparing predicted and observed HF sky-wave signal intensities*. Read.
- Rec. ITU-R P.373-9 (09/2013); P.371-9 (08/2023); P.372-18 (09/2026); P.532-1 (03/1992). Read in part.
- Report ITU-R P.2011-1 (1999), *Propagation at frequencies above the basic MUF*. Read. https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-P.2011-1-1999-PDF-E.pdf
- CCIR (1980), *Supplement to Report 252-2: Second CCIR computer-based interim method for estimating sky-wave field strength and transmission loss at frequencies between 2 and 30 MHz*, Geneva. Read. https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-P.252-2-1970-PDF-E.pdf

**Handbooks and books [B]**
- ITU (1998), *Handbook: The ionosphere and its effects on radiowave propagation*. Read (§2.3, 2.6, 5, 6.5). https://www.itu.int/dms_pub/itu-r/opb/hdb/R-HDB-32-1998-PDF-E.pdf
- Davies, K. (1990), *Ionospheric Radio*, Peter Peregrinus. doi:10.1049/PBEW031E. Not obtained.
- Hunsucker, R. D. & Hargreaves, J. K. (2003), *The High-Latitude Ionosphere and its Effects on Radio Propagation*, Cambridge. doi:10.1017/CBO9780511535758. Not obtained.
- McNamara (1991) and Goodman (1992): not obtained, record not verified.

**Government and laboratory documents [T]**
- NOAA SWPC, *Global D-Region Absorption Prediction Documentation* (updated 11 May 2010). Read. https://www.swpc.noaa.gov/content/global-d-region-absorption-prediction-documentation
- Akmaev, R. A. et al. (2010), *D-RAP Model Validation: I. Scientific Report*. Read. https://www.ngdc.noaa.gov/stp/drap/DRAP-V-Report1.pdf
- NOAA NCEI, D-RAP archive readme (2026). Read.
- NOAA SWPC, *NOAA Space Weather Scales*. Read (checked 2026-11-13). https://www.swpc.noaa.gov/noaa-scales-explanation
- NOAA SWPC (2024), *Another R3 (Strong) Flare Erupts from Region 3842 on 03 October 2024*, news item, 3 October 2024. Read. https://www.spaceweather.gov/news/another-r3-strong-flare-erupts-region-3842-03-october-2024
- NOAA National Weather Service (2023), *NWS Instruction 10-1102: Provision of Space Weather Services for Global Aviation*, 28 November 2023. Read (threshold table). https://www.weather.gov/media/directives/010_pdfs/pd01011002curr.pdf
- Dandekar, B. S. (1985), "Ionospheric modeling", §10.3 of chapter 10, *Ionospheric radio wave propagation*, in the US Air Force handbook of 1985 (NOAA NGDC online copy). Read. https://www.ngdc.noaa.gov/stp/space-weather/online-publications/miscellaneous/afrl_publications/handbook_1985/Chptr10.pdf
- Teters, L. R. et al. (1983), NTIA Report 83-127 (IONCAP user's manual). Downloaded; loss equations not located in the scanned text.

**Peer-reviewed [P]**
- Araujo-Pradere, E. A., Fuller-Rowell, T. J. & Codrescu, M. V. (2002), Radio Sci. 37(5). doi:10.1029/2001RS002467. Abstract.
- Barta, V. et al. (2019), Ann. Geophys. 37, 747–761. doi:10.5194/angeo-37-747-2019. Read.
- Berngardt, O. I. et al. (2019), Space Weather 17, 907–924. doi:10.1029/2018SW002130. Abstract.
- Bilitza, D. et al. (2022), Rev. Geophys. 60, e2022RG000792. doi:10.1029/2022RG000792. Abstract.
- Bland, E. C. et al. (2018), Space Weather 16, 1455–1469. doi:10.1029/2018SW001916. Abstract.
- Chakraborty, S. et al. (2018), Radio Sci. 53, 472–484. doi:10.1002/2017RS006488. Abstract.
- Chakraborty, S. et al. (2021a), Radio Sci. 56, e2021RS007285. doi:10.1029/2021RS007285. Abstract.
- Chakraborty, S. et al. (2019), Space Weather 17, 1351–1363, doi:10.1029/2019SW002179; (2021b), JGR Space Phys. 126, e2020JA028813, doi:10.1029/2020JA028813. Abstracts; no numbers used.
- Chartier, A. T. et al. (2023), Space Weather 21, e2023SW003590. doi:10.1029/2023SW003590. Abstract.
- Dmitriev, A. V. et al. (2010), JGR 115, A12244. doi:10.1029/2010JA015380. Abstract.
- Fiori, R. A. D. et al. (2018), Space Weather 16, 1348–1362. doi:10.1029/2018SW001905. Abstract.
- Fiori, R. A. D. et al. (2020), Space Weather 18, e2020SW002572. doi:10.1029/2020SW002572. Abstract.
- Fiori, R. A. D. et al. (2022), J. Space Weather Space Clim. 12, 21. doi:10.1051/swsc/2022017. Abstract (Crossref and Semantic Scholar records; the publisher refused automated access).
- Frissell, N. A. et al. (2014), Space Weather 12, 651–656. doi:10.1002/2014SW001132. Abstract.
- Frissell, N. A. et al. (2018), Geophys. Res. Lett. 45, 4665–4674. doi:10.1029/2018GL077324. Abstract.
- Frissell, N. A. et al. (2019), Space Weather 17, 118–132. doi:10.1029/2018SW002008. Read (author copy, hamsci.org).
- Frissell, N. A. et al. (2022), Geophys. Res. Lett. 49, e2022GL097879. doi:10.1029/2022GL097879. Abstract.
- Ponomarchuk, S. N. et al. (2025), “Effects of the May 10–13, 2024 magnetic storm in the Asian region of Russia from ionospheric sounding with a continuous chirp signal.” Full paper read: https://naukaru.ru/en/storage/download/242871. The observations cited above are mode/MOF and blackout evidence, not communications-channel fading statistics.
- Gillies, R. G., Spanswick, E., Fiori, R. A. D., Ghaly, F., Cameron, T., & Skone, S. (2026), “The Reliability of HF Propagation Links Over Canada During Absorption Events,” *Space Weather* 24(10), e2025SW004906. doi:10.1029/2025SW004906. https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2025SW004906 — full paper read (§§2–4). It derives reception/loss occurrence from scheduled pulses under an endpoint-operation assumption and relates excess losses to spatially offset riometer absorption; it is not a wideband complex-channel data set.
- Frissell, N. A. et al. (2023), Front. Astron. Space Sci. 10, 1184171. doi:10.3389/fspas.2023.1184171. Read in part (publisher full text).
- Hayakawa, H. et al. (2025), Astrophys. J. 979, 49. doi:10.3847/1538-4357/ad9335. Read in part (arXiv:2407.07665).
- Heino, E. et al. (2019), JGR Space Phys. 124, 1361–1376. doi:10.1029/2018JA026192. Abstract.
- Heino, E. & Partamies, N. (2020), JGR Space Phys. 125, e2020JA027935. doi:10.1029/2020JA027935. Abstract.
- Kavanagh, A. J. et al. (2004), Ann. Geophys. 22, 877–887. doi:10.5194/angeo-22-877-2004. Read in part.
- Kunduri, B. S. R. et al. (2024), Geophys. Res. Lett. 51, e2024GL112484. doi:10.1029/2024GL112484. Abstract.
- Levine, E. V., Sultan, P. J. & Teig, L. J. (2019), Radio Sci. 54, 168–180. doi:10.1029/2018RS006666. Abstract; results secondary via Siskind et al. (2022).
- Lo, S. et al. (2022), Atmosphere 13, 1340. doi:10.3390/atmos13081340. Abstract.
- McNamara, L. F. et al. (2008), Radio Sci. 43, RS2004. doi:10.1029/2007RS003742. Abstract.
- McNamara, L. F., Baker, C. R. & Borer, W. S. (2009), Radio Sci. 44. doi:10.1029/2008RS004004. Abstract.
- McNamara, L. F. et al. (2013), Radio Sci. 48, 143–157. doi:10.1002/rds.20020. Abstract.
- Neal, J. J., Rodger, C. J. & Green, J. C. (2013), Space Weather 11, 420–433. doi:10.1002/swe.20066. Abstract.
- Nielson, D. L. & Crochet, M. (1974), Rev. Geophys. 12, 688–702. doi:10.1029/RG012i004p00688. Abstract.
- Pederick, L. H. & Cervera, M. A. (2014), Radio Sci. 49, 81–93. doi:10.1002/2013RS005274. Abstract.
- Pierrard, V. et al. (2025), Atmosphere 16, 299. doi:10.3390/atmos16030299. Abstract.
- Pignalberi, A., Pietrella, M. & Pezzopane, M. (2021), Atmosphere 12, 1003. doi:10.3390/atmos12081003. Abstract.
- Reid, B. et al. (2023), Space Weather 21, e2022SW003185. doi:10.1029/2022SW003185. Abstract.
- Rishbeth, H. & Mendillo, M. (2001), J. Atmos. Sol.-Terr. Phys. 63, 1661–1680. doi:10.1016/S1364-6826(01)00036-0. Abstract read (University of Southampton repository record, https://eprints.soton.ac.uk/id/eprint/9167); full text not obtained. The 34-year span and the split of contributions are secondary via Luetzelschwab (2004).
- Rogers, N. C. & Honary, F. (2015), J. Space Weather Space Clim. 5, A8, doi:10.1051/swsc/2015009; Rogers, N. C. et al. (2016), Space Weather 14, 950–972, doi:10.1002/2016SW001527. Records only.
- Sauer, H. H. & Wilkinson, D. C. (2008), Space Weather 6, S12002. doi:10.1029/2008SW000399. Abstract; formulas secondary via the D-RAP documentation.
- Siskind, D. E. et al. (2017), Space Weather 15, 115–130. doi:10.1002/2016SW001546. Abstract.
- Siskind, D. E. et al. (2022), Space Weather 20, e2021SW003012. doi:10.1029/2021SW003012. Read.
- Spogli, L. et al. (2024), Ann. Geophys. (INGV) 67, PA218. doi:10.4401/ag-9117. Read.
- Tao, C. et al. (2020), Earth Planets Space 72, 173. doi:10.1186/s40623-020-01278-z. Abstract.
- Themens, D. R. et al. (2017), JGR Space Phys. 122, 9015–9031. doi:10.1002/2017JA024398. Abstract.
- Themens, D. R. et al. (2024), Geophys. Res. Lett. 51, e2024GL111677. doi:10.1029/2024GL111677. Abstract.
- Thomas, E. G. et al. (2024), Geophys. Res. Lett. 51, e2024GL112450. doi:10.1029/2024GL112450. Abstract.
- Toman, K. (1979), Radio Sci. 14, 447–453. doi:10.1029/RS014i003p00447. Abstract.
- Usis, M. et al. (2026), Front. Astron. Space Sci. 12, 1720301. doi:10.3389/fspas.2025.1720301. Abstract.
- Vanhamel, J., Machiels, W. & Lamy, H. (2022), Int. J. Antennas Propag. 2022, 4809313. doi:10.1155/2022/4809313. Abstract.
- Walden, M. C. (2012), Radio Sci. 47, RS0L09. doi:10.1029/2011RS004914. Abstract.
- Warrington, E. M. et al. (2016), Radio Sci. 51, 1048–1059. doi:10.1002/2015RS005880. Abstract.
- Zawdie, K. A. et al. (2017), Radio Sci. 52, 767–783. doi:10.1002/2017RS006256. Record only.
- Records verified but not used for numbers: Gonzalez-Esparza et al. (2024), doi:10.1029/2024SW004111; Kwak et al. (2024), doi:10.5140/JASS.2024.41.3.171; Wheeler (1966), doi:10.1002/rds19661111303; Mendillo (2006), doi:10.1029/2005RG000193; Buonsanto (1999), doi:10.1023/A:1005107532631.

**Grey literature [G]**
- ITU-R Study Group 3, *ITU-R-HF* source repository (ITURHFProp/P533, version string 14.2; includes the D1 data bank and P.1239 grid files). Source read at the state of 15 May 2025 (commit 82017594), with the commit history of `MedianSkywaveFieldStrengthShort.c` (constants changed on 3 June 2020, commit 9b93681a75) and the sample report `ITURHFProp/ReportsPDD190122-124921.txt`. https://github.com/ITU-R-Study-Group-3/ITU-R-HF
- Lane, G. (2005), *Review of the High Frequency Ionospheric Communications Enhanced Profile Analysis & Circuit (ICEPAC) Prediction Program*, preprint 2B-1, Ionospheric Effects Symposium. Read. https://voacap.com/2023/documents/GLane_ICEPAC.pdf
- Luetzelschwab, C. (K9LA), articles at https://k9la.us : *Day-to-Day Variability of the Ionosphere* (WorldRadio, Aug 2004); *Validating Propagation Predictions*; *Multi-Hop at Night on 160-Meters*, *Ducting on 160-Meters*, *Polarization Observations on 160-Meters*, *Low Angles on 160-Meters* (Topband Topics, Aug 2011); *Absorption at Night*; *Ducting and Spotlight Propagation on 160m* (CQ, Dec 2005); *160m at Solar Maximum* (WorldRadio, Mar 2004); *20M Ionosphere-Ionosphere Mode*. Read.
- HamSCI publications listing, https://hamsci.org/publications. Read (used to locate records).
- Potter, R. & Frissell, N. (2026), *HamSCI Personal Space Weather Station Observations of the Gannon Geomagnetic Storm*, thesis, University of Scranton; Potter, R. et al. (2026), *Personal Space Weather Station Observations of the Gannon Geomagnetic Storm*, HamSCI Workshop 2026. Abstracts read. https://hamsci.org/bibcite/reference/583 ; https://hamsci.org/bibcite/reference/565

**Own computations [E]**
- The scripts behind Tables 1 and 2 are a simplified re-implementation, not the ITU software. They were kept only in temporary working storage and are not part of this record; the method is described above Table 1.

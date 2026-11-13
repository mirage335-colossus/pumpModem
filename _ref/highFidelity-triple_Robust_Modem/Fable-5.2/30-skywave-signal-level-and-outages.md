# Skywave 1–30 MHz: signal level, absorption and outages

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature research and bounded native reference-model calculations. The official ITU model was compiled and exercised in temporary storage; no modem experiment or production-code change is implied.
- **Status.** Research remediation on 13 November 2026 replaces the unverified loss tables, resolves their principal discrepancy, and incorporates full primary-source methods/results. Source access, model reproduction, physical validation and remaining external evidence are distinguished below.
- **Related chapters.** dispersion at mid-latitudes [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and at high and low latitudes [32](32-skywave-dispersion-highlatitude-equatorial.md); channel models [33](33-hf-channel-models-and-simulators.md); noise [50](50-noise-and-interference-mf-hf.md).

- **Predictions are monthly medians with large error.** ITU-R P.533-14 (08/2019) is the latest in-force revision. The ITU-R comparison example against the D1 data bank (P.1148-1, 1997, for the method as it then stood) shows a standard deviation of 10.5 dB over 16 268 monthly-median cases: 7–8 dB below 2 000 km, 13.6 and 14.5 dB in the 3 000–3 999 and 5 000–6 999 km bins, and 16.0 dB for path midpoints above 60° geomagnetic [S].
- **P.533 text and reference code differ.** Printed eq. 20 of P.533-13 and -14 gives twice the absorption that the ITU reference code computes when the per-point terms are equal, and the code's L_z and above-the-MUF laws differ from the text [S, G]. Neither says which is intended.
- **Pinned reference predictions replace the simplified loss estimates.** Across the selected 100–3,000-km examples, best-band terminal-equivalent loss is 108.7–126.1 dB at 23 UTC and 119.0–156.8 dB at 11 UTC. The 10,000-km examples give 141.8–146.6 and 176.4–181.1 dB respectively. These conditional model results do not bound nature, and 180–195 dB is not a unique indicator of abnormal propagation. Tables 1–2 exclude the invalid 1.8-MHz extrapolation.
- **Keep the reference plane explicit.** At 4 W EIRP, 0 dBi receiving gain and matched isotropic-equivalent noise, `C/N0 = 210.0 − L_port − F_a` dB-Hz. Thus 20 dB-Hz means `L_port + F_a ≈ 190.0 dB`; the GUI's groundwave/short-monopole normalization cannot be transplanted directly into an HF skywave terminal budget.
- **Day-to-day and within-hour spread.** Lower-decile signal deviations are 8–17 dB for daily medians and 8 dB within the hour [S]; these are distinct conditional levels, not automatically additive independent margins. foF2 decile factors span 0.60–0.92 and 1.09–1.48 [S].
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
| L_i | D/E-region absorption (eq. 20) | see next subsection | Native examples: 0.257–0.645 dB at 7 MHz/1,000 km/23 UTC; 15.107–29.200 dB there at 11 UTC; 68.423–168.561 dB at 3.5 MHz/3,000 km/11 UTC [E] |
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

**IONCAP-family primary-source check.** The official [NTIA TR-83-127 user manual](https://its.ntia.gov/publications/download/83-127_ocr_reduced.pdf) was inspected rather than left as an unsearched download. Its §6.1/§6.2 concern distribution tapes and prediction files, not the physical equations implied by some symbol-list cross-references. Its introduction describes doubling ionospheric loss when adapting a point-to-point communication program to **radar**; that is not a rule to double communication loss or a reconciliation of P.533 equation 20.

The official [ICEPAC Technical Manual](https://its.ntia.gov/media/38678/icepac_tech_manual.pdf), §5.2, equations 5.1–5.4, is an accessible related primary derivation: Martyn's relation uses `L(f_ob) = L(f_v)/cos(phi_0)`, `f_ob = f_v sec(theta_t)`, with the ray/normal angle `phi_0` evaluated at 100 km, followed by a collision/density/gyrofrequency profile integral and empirical absorption fitting. It identifies ICEPAC as an IONCAP derivative with a changed ionospheric specification. This supports the ancestry and named model branch, **not exact identity with a particular original IONCAP implementation**. No unverified original IONCAP curve is used in the new tables. The remaining version-specific equation attribution is a provenance limit, not a missing numerical prerequisite for the executed P.533 branch.

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
- **Night residual.** Japanese field-strength data up to 14 000 km give L_a = (7 + 0.019 D)(1 + 0.015 R12)/(f² + 10) dB, with D in km (eq. 44) [S, read; Wakai 1975 secondary]. At 1 000 km and R12 = 0 this gives 1.2 dB at 3.5 MHz [E]. The algebraic 1.8-MHz value is 2.0 dB, but no valid-domain absolute-loss prediction is inferred there; this legacy residual alone does not fill the 1.7–2-MHz model gap.
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
- **Polar-cap absorption (PCA).** Solar protons ionise 45–75 km altitude above about 60° magnetic latitude, day and night [B, read: Handbook §2.3.2.6]. The handbook describes sunlit absorption as about four times the dark value at 30 MHz; this is not a fixed ratio in the spectrum-dependent Sauer model below.
- **Short-wave fadeout (SWF).** Flare X-rays raise D-region density over the whole sunlit hemisphere. Lower frequencies are affected first and longest (§2.6.1.2) [B, read].
- **Post-storm effect.** Radiation-belt electron precipitation raises absorption from sub-auroral to middle latitudes through the storm recovery phase, up to ten days or more (§2.3.2.5) [B, read].

### MUF, LUF and what controls availability

- **Definitions** (P.373-9) [S, read]. *Basic MUF*: highest frequency propagating by refraction alone. *Operational MUF*: highest frequency giving acceptable performance. *OWF* and *HPF*: lower and upper deciles of the daily operational MUF. *LUF*: lowest frequency giving acceptable performance; it depends on the system.
- **Basic MUF** follows from foF2, M(3000)F2 and foE (P.533 eqs 3–6) [S, read].
- **Operational/basic MUF ratio** is 1.10–1.35 depending on season, day or night and radiated power (P.1240-2 Table 1) [S, read].
- **Sunspot index pitfall.** The P.1239 maps require version-1 sunspot numbers: R12(v1) = 0.6 × R12(v2) (P.371-9 Annex 1 §2, eq. 2) [S, read]. Feeding today's SILSO v2 numbers unscaled overstates solar activity by a factor of 1.67. foF2 is treated as saturating above R12 = 160 (P.1239-4 §3.1).
- **System dependence of the LUF.** For conventional services the LUF is set by absorption and noise. A hypothetical service with a different required C/N0 can have a different LUF. Its available loss budget must first be calculated from EIRP, receive gain, noise and required C/N0; no modem-independent 180–200-dB tolerance is established here [E].

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

**Reference calculation [E, executed 13 November 2026].** Tables 1–2 replace the earlier bespoke, simplified calculation. The official [ITU-R-HF v14.3 release](https://github.com/ITU-R-Study-Group-3/ITU-R-HF/releases/tag/v14.3), commit `cd172be56dc04b154e5d2fa91cbaa6ecf5284305`, was clean-built and run locally with its distributed coefficient data. Its runtime identifies the HF model as **P533 14.2** and noise model as **P372 14.3**. Release 14.3 chiefly fixes noise time-block/interpolation and decile handling; it does not reconcile the printed P.533-14 absorption law. The calculations below therefore describe this pinned implementation, not a claim of exact equivalence to every equation in the Recommendation.

The output is **terminal-equivalent isotropic loss** `L_port = 30 − Pr`, where `Pr` is the program's median available received power in dBW for 1 kW and 0 dBi antennas. This is the quantity used in the subsequent power budget. P.533 equations 17 and 43 imply `L_port = L_b + 0.6 dB` for one mode; equation 44 power-sums modes. Consequently a strongest-mode `L_b`, a summed-mode loss, and `L_port` are different quantities. The official constants 136.6 and 107.2 were preserved. Independently, the field-to-isotropic-power conversion from SI is 107.216 dB, agreeing with the rounded second constant; no physical recalibration of the first is inferred. Long-path output uses the composite field rather than a per-mode loss. This convention also avoids importing the P.368 short-monopole groundwave correction into skywave.

Inputs: R12 version 1; isotropic transmit/receive patterns; `Path.txpower 0`; short great-circle direction; monthly coefficient maps; the program's representative fifteenth day; no added storm or observed-event correction. The executable year label is 1981, as in its example; these are climatological model states, not reconstructed 1981 weather. Table 1 uses meridional routes at 10°E, centered at 40°N, lengths 100/1,000/3,000 km, January/April/July/October, actual UTC 11/23/04, R12 10/150 and five frequencies. Each cell spans four monthly calculations: **360 cases**. Table 2 uses transmitter 50°N 10°E, initial azimuth 180°, distances 10,000/19,500 km, January/July, both R12 values, every UTC hour and five frequencies: **960 cases**. For the near-antipodal path the longitude changes by 180° after passing the South Pole. The 1.8-MHz column is withdrawn because P.533's stated domain begins at 2 MHz. No synthetic value fills the 1.7–2-MHz gap.

**Resolved Kranji–Beijing discrepancy.** The official sample input has transmitter 1.42°N/103.73°E, receiver output-grid coordinates **40.0°N/116.4°E**, R12 140, January, 6.2 MHz and 1 kW isotropic. The grid overrides the input's other receiver-coordinate fields. Clean compilation reproduces its dominant 2F2 `L_b = 161.670457 dB`, total field −9.122 dBµV/m and total `Pr = −132.169 dBW` to the report's printed precision. A diagnostic-only build prints the component terms without changing results. The earlier script, recovered from temporary research storage, reproduces **172.898372 dB** exactly; it also wrongly E-screens the mode beyond 4,000 km.

A consequential clock mismatch was hidden by the report label: configuration `Path.hour 2` is decremented to internal hour 1, which is passed to both the ionospheric grid and solar geometry. Thus this example is calculated at **01 UTC**, despite its output header saying hour 2. The original script used literal 02 UTC. At actual 02 UTC (configuration hour 3), the native 2F2 loss is **170.340365 dB**. The new tables deliberately supply `Path.hour = actual UTC + 1`.

| Controlled change, in this order | Resulting 2F2 L_b (dB) | Interpretation |
|---|---:|---|
| Original simplified calculation at literal 02 UTC | 172.898372 | Historical failure reproduced |
| Align to the sample's computed 01 UTC | 163.898975 | Solar geometry and hourly ionosphere both change |
| Use native Earth radius 6371.009 km | 163.898992 | Tiny geometric correction |
| Use selected-control-point reflection height 306.11293 km | 164.266600 | Original used midpoint height 350.84758 km at 01 UTC |
| Use native control-point longitudinal gyrofrequency 0.506721 MHz | 162.729407 | Original centered dipole/penetration average was 0.342280 MHz at this geometry |
| Use native foE at penetration points | 163.382200 | Changes deviative penetration factors |
| Use native solar zenith/noon angles, including equation of time | 161.091124 | Original omitted that correction |
| Use native spatial/month-dependent absorption exponent | 161.250454 | Replaces fixed p = 1.45 |
| Change L_z from printed 8.72 to code 9.14 | 161.670454 | Within 0.000004 dB of native result; diagnostic inputs were rounded |

This sequential attribution is order-dependent because absorption factors interact. The direct term comparison is unambiguous: original/native `L_fs` = 122.017228/121.810368, `L_i` = 40.161145/28.720089, `L_g` = 2/2, `L_m = L_h = 0`, `L_z` = 8.72/9.14 dB. Native absorption averages four penetration terms to 83.076078, uses incidence 75.566241° at 110 km and slant length 4738.345147 km. The original 11-dB discrepancy is therefore **resolved calculation disagreement**, not a measured model-error variance or justification for an extra 10-dB uncertainty allowance. The independent physical checks are the UTC/longitude solar-hour-angle relation, spherical hop geometry, units of the field-to-power conversion, and separation of per-mode versus summed power. Reproducing the code alone does not establish ionospheric accuracy.

**Table 1. Native terminal-equivalent isotropic loss L_port (dB), ranges over four months.** The final column is the program's path basic MUF; it does not imply that a residual above that MUF is a measured usable mode. Frequencies are MHz and distance is km.

| Distance | Actual UTC | R12 | 3.5 | 7 | 14 | 21 | 28 | Basic MUF |
|---|---|---|---|---|---|---|---|---|
| 100 | 11 | 10 | 126.6–131.6 | 119.0–141.1 | 160.0–167.2 | 172.6–174.1 | 175.7–176.1 | 6.0–7.3 |
| 100 | 11 | 150 | 141.5–145.2 | 125.3–129.4 | 138.1–154.6 | 158.9–169.7 | 170.3–176.1 | 9.7–13.3 |
| 100 | 23 | 10 | 108.7–109.5 | 146.3–152.6 | 169.3–169.6 | 173.0–173.1 | 175.5–175.6 | 3.8–4.5 |
| 100 | 23 | 150 | 111.2–111.7 | 116.4–144.2 | 154.3–168.9 | 170.0–173.2 | 175.5–175.7 | 5.0–9.1 |
| 100 | 04 | 10 | 107.6–124.5 | 145.7–157.0 | 169.2–169.5 | 172.9–173.0 | 175.4–175.5 | 3.2–4.5 |
| 100 | 04 | 150 | 110.1–112.3 | 115.5–153.5 | 158.8–169.7 | 172.0–173.2 | 175.6–175.7 | 3.8–7.7 |
| 1000 | 11 | 10 | 143.8–152.3 | 132.8–134.5 | 143.8–151.9 | 164.0–170.7 | 176.3–183.3 | 12.5–13.1 |
| 1000 | 11 | 150 | 167.5–183.9 | 142.6–148.2 | 130.6–132.8 | 132.9–160.1 | 160.0–172.3 | 15.8–21.1 |
| 1000 | 23 | 10 | 113.8–114.2 | 121.0–141.5 | 166.5–173.1 | 181.4–182.9 | 185.3–185.4 | 5.9–7.2 |
| 1000 | 23 | 150 | 115.4–116.0 | 120.2–121.9 | 140.0–165.9 | 162.8–181.2 | 175.5–185.6 | 7.4–13.4 |
| 1000 | 04 | 10 | 112.1–114.4 | 120.6–146.3 | 164.5–175.9 | 180.3–182.8 | 185.2–185.3 | 5.3–7.6 |
| 1000 | 04 | 150 | 113.4–119.0 | 117.3–143.6 | 146.2–174.4 | 166.2–183.1 | 178.4–185.6 | 5.7–12.1 |
| 3000 | 11 | 10 | 193.2–216.4 | 160.9–168.0 | 146.4–156.8 | 142.7–168.5 | 164.2–180.5 | 16.4–23.3 |
| 3000 | 11 | 150 | 249.2–293.6 | 181.1–193.8 | 148.1–153.6 | 142.8–148.2 | 144.2–164.0 | 24.5–34.8 |
| 3000 | 23 | 10 | 122.4–123.2 | 127.4–128.2 | 155.2–164.6 | 174.9–182.7 | 187.4–193.6 | 9.6–12.0 |
| 3000 | 23 | 150 | 124.5–126.1 | 127.6–128.3 | 133.9–156.3 | 139.1–175.6 | 166.4–188.1 | 11.7–21.3 |
| 3000 | 04 | 10 | 122.7–133.3 | 127.9–133.0 | 150.4–167.2 | 171.9–184.9 | 184.5–194.5 | 8.9–13.1 |
| 3000 | 04 | 150 | 125.3–141.8 | 127.5–133.8 | 133.9–167.0 | 151.8–184.7 | 168.9–194.6 | 9.1–20.0 |

The best of these five bands gives **119.0–156.8 dB at 11 UTC**, **108.7–126.1 dB at 23 UTC**, and **107.6–133.8 dB at 04 UTC** across the selected routes/months/solar states. These are bounded design examples, not global limits. Daytime 3.5 MHz on 3,000 km reaches 293.6 dB in the reference model without a storm; conversely a high-frequency 100-km above-MUF residual can be around 175 dB. The previous assertion that 180–195 dB uniquely signifies abnormal propagation, and the previous universal 185–194-dB above-MUF floor, are withdrawn. The code's cap, mode sum, frequency, distance and empirical calibration all affect that residual; no observation validates arbitrarily deep extrapolation.

**Table 2. Native long-route terminal-equivalent isotropic loss L_port (dB).** fM/fL are model reference frequencies, not measured instantaneous upper/lower communication limits. Entries above 250 dB are simply reported as `>250`; such predictions are not evidence of a measured weak tail. The final column minimizes over the five listed bands at each of 24 actual UTC hours, then reports the daily range.

| Distance | Month | R12 | UTC | fM | fL | 3.5 | 7 | 14 | 21 | 28 | Best band over 24 h |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 10000 | 1 | 10 | 11 | 27.7 | 11.4 | >250 | 247.2 | 176.4 | 177.4 | 194.9 | 143.5–177.3 |
| 10000 | 1 | 10 | 23 | 14.8 | 1.8 | 143.5 | 144.3 | 182.8 | 245.5 | >250 | 143.5–177.3 |
| 10000 | 1 | 150 | 11 | 37.3 | 17.1 | >250 | >250 | 207.1 | 181.1 | 181.4 | 139.1–181.8 |
| 10000 | 1 | 150 | 23 | 16.4 | 1.8 | 142.8 | 141.8 | 173.7 | 226.0 | >250 | 139.1–181.8 |
| 10000 | 7 | 10 | 11 | 23.1 | 11.6 | >250 | 244.6 | 179.6 | 186.2 | 210.8 | 145.7–179.8 |
| 10000 | 7 | 10 | 23 | 10.6 | 1.8 | 146.6 | 156.1 | 225.3 | >250 | >250 | 145.7–179.8 |
| 10000 | 7 | 150 | 11 | 39.2 | 17.5 | >250 | >250 | 210.1 | 181.4 | 180.0 | 143.8–181.6 |
| 10000 | 7 | 150 | 23 | 10.6 | 1.8 | 146.6 | 156.1 | 225.2 | >250 | >250 | 143.8–181.6 |
| 19500 | 1 | 10 | 11 | 17.8 | 15.1 | >250 | 241.7 | 176.6 | 183.6 | 209.5 | 164.3–177.3 |
| 19500 | 1 | 10 | 23 | 17.0 | 7.4 | >250 | 172.3 | 166.2 | 196.6 | 243.6 | 164.3–177.3 |
| 19500 | 1 | 150 | 11 | 22.8 | 22.8 | >250 | >250 | 198.7 | 179.1 | 185.1 | 165.8–179.6 |
| 19500 | 1 | 150 | 23 | 20.9 | 11.5 | >250 | 216.2 | 168.6 | 178.7 | 205.1 | 165.8–179.6 |
| 19500 | 7 | 10 | 11 | 9.7 | 13.7 | >250 | 186.9 | 175.6 | 210.5 | >250 | 136.8–179.6 |
| 19500 | 7 | 10 | 23 | 16.9 | 2.5 | 145.9 | 137.1 | 160.7 | 203.0 | >250 | 136.8–179.6 |
| 19500 | 7 | 150 | 11 | 19.5 | 20.8 | >250 | >250 | 187.4 | 178.6 | 191.7 | 129.7–179.9 |
| 19500 | 7 | 150 | 23 | 31.3 | 3.5 | 163.2 | 136.4 | 138.2 | 151.9 | 171.2 | 129.7–179.9 |

For 10,000 km, the selected dark 23-UTC best-band values are 141.8–146.6 dB and 11-UTC best-band values 176.4–181.1 dB. Full-day best-band extrema are 139.1–181.8 dB. The 19,500-km examples span 129.7–179.9 dB over a day and retain the prescribed maximum 15-dB antipodal focusing. Cases with fL > fM remain model extrapolations outside the favorable window. The earlier unexecuted off-table azimuth/21,700-km numerical claims are withdrawn. Neither these near-antipodal predictions nor the short-route comparison establish rare duct occurrence, observed storm loss, antenna performance or reception probability.

**MF night skywave.** Table 3 converts P.1147-4 annual-median field strength at six hours after sunset into a reference-normalized equivalent loss at 1.7 MHz. The conversion is `L_equiv = K − E_0`, where `K = 30 + 10 log10(3) + 107.2 + 20 log10(1.7) = 146.580… dB`: 1 kW with reference directivity 3 and the P.533 equation-43 field conversion [E]. This is a reference-normalized field quantity, not a terminal calibration.

**Table 3. MF night field-normalized equivalent loss (dB) at 1.7 MHz from P.1147-4, by midpoint geomagnetic latitude Φ [E from S].**

| Φ | 100 km | 1 000 km | 3 000 km | 10 000 km |
|---|---|---|---|---|
| 20° | 90 | 107 | 121 | 142 |
| 40° | 91 | 110 | 126 | 150 |
| 50° | 93 | 113 | 132 | (162) |
| 60° | 97 | 121 | 146 | (186) |

Reproduction: `p = sqrt(D² + 200²)` km; `k = 2π + 4.95 tan²(Φ)`; `L_equiv = K − 107 + 20 log10(p) + k sqrt(p/1000)`, with the listed latitude, zero solar-activity/hourly/polarization adders and unit reference pattern. Round only the final result to the nearest integer. All sixteen cells were independently recalculated. The inherited 40°/10,000-km cell was 151; the stated calculation gives 150.475… dB (150.495… with the previously displayed rounded K=146.6), hence 150 under this explicit convention. This rounding/reference discrepancy does not imply sub-decibel physical predictive accuracy.

Caveats and adders for Table 3:

- The field is the one a ground-level loop measures, so it includes the ground-reflected wave. The transmit vertical-pattern factor G_V is not applied; it matters below about 500 km.
- Bracketed values apply one latitude to the whole 10 000 km path, which is unrealistic.
- Solar-activity loss L_r at R = 150: 2.5 dB (1 000 km) and 7.5 dB (3 000 km) at Φ = 50°; 7.7 and 22.5 dB at Φ = 60°; 1.5 and 4.5 dB with the European value b = 1 [E from S].
- Excess polarisation coupling near the dip equator: up to 28 dB per terminal [E from S].
- Ground conductivity an order of magnitude below 10 mS/m at a terminal lowers the field by up to 10 dB (Annex 1) [S, read].
- The Handbook reports that the method under-predicts paths of 5 000–10 000 km by typically 10 dB (§5.3.2.1) [B, read].

Table 3 is a conversion of the P.1147 ground-level, path-aligned vertical-loop field convention to an equivalent isotropic loss; it is not a calibrated monopole terminal budget. The former apparent agreement with an out-of-domain 1.8-MHz P.533 calculation is withdrawn. For 1.7–2 MHz use measured, geometry-specific evidence or an explicitly theoretical model branch, not interpolation asserted to be validated.

### 2. Variability statistics

**foF2 and MUF, day to day.**

- P.1239-4 Tables 2 and 3 give lower- and upper-decile factors for foF2 by local time, latitude, season and three R12 classes [S, read]. A parse of the tables in this record gives 0.60–0.92 and 1.09–1.48 globally; 0.70–0.90 and 1.09–1.30 at 40–50° latitude; 0.67–0.86 and 1.11–1.42 at 60–70°.
- For E modes the factors are 0.95 and 1.05 (P.533-14 §3.6) [S, read].
- The inter-decile range of the basic MUF at a given hour is 30–40 % of the median (Report P.2011-1 §4) [S, read].
- Rishbeth & Mendillo (2001), full [primary author-institution copy](https://sirius.bu.edu/aeronomy/science2.pdf), abstract, data/method sections, Table 5 and final variance discussion: the 1957–1990, thirteen-station analysis concerns **daily NmF2**, with medium-activity standard deviations about 20% by day and 33% at night. Their approximate daytime decomposition assigns about 13% to geomagnetic and 15% to residual meteorological variability, assuming quadrature combination; solar contributions are small. The night decomposition is too uncertain for the same interpretation. These are density statistics, not dB path loss or received-power quantiles. The former conflicting secondary description as foF2 variability is superseded.
  - Since NmF2 ∝ foF2², first-order relative foF2 deviations are about half as large. A Gaussian transformation can be an illustrative sensitivity assumption, but does not establish daily lower-decile frequencies, cross-band dependence or extreme-storm tails [E].
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
- Large-scale travelling ionospheric disturbances can modulate skip distance [P, abstract lead: Frissell et al. 2022]. No numerical period/velocity is selected from this uninspected body; the inspected event/dispersion sources in 31 provide the quantitative cases.

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

**Recent comparison and public-data audit.** Zhu et al. (2026), [doi:10.1029/2025SW004907](https://doi.org/10.1029/2025SW004907), full methods/results, compare hourly monthly-median field predictions on historical D1 short-great-circle routes at 2.5–25.8 MHz. “Short” includes routes up to 16,448 km, not only P.533's ≤7,000-km branch. Reported predicted-minus-measured mean/SD are REC533 +0.28/9.81, ICEPAC −4.34/15.28 and VOACAP −4.23/15.39 dB. Their maps are model outputs, not measured coverage. The comparison is useful within its selected domain; D1 informed model calibration, and no independent contemporary weak-tail validation is established. The model ranking cannot be transferred to unobserved storms, below 2 MHz, complement long paths or a modem's decoding probability.

The linked [Zenodo deposit](https://doi.org/10.5281/zenodo.20413750) was actually downloaded and checked. Its `dbank_d1.txt` contains 1,613 uniquely keyed, valid fixed-width monthly rows: 1,492 short-route and 121 complementary-long-route rows, with 16,268 nonmissing hourly field values (15,602 on short routes), using `99` as missing. No malformed row was rejected. This differs by one short-route monthly row from the paper's 1,493+121 description. The archive contains eighteen map files, six locations times three models; headers identify ICEPAC 161207W and REC533/VOACAP 16.1207W. It supplies no paired D1 prediction/residual arrays, so the paper's aggregate errors have **not** been independently recomputed. Download sizes/MD5 are `147433 / 55240a896f8ba2b1e1c8b104aa6d34e5` for D1 and `556543 / 7e6883ab6fcae6ca8755d586630dc8e4` for the ZIP; all ZIP CRCs passed. This is a concrete reproducibility limit, not an access-budget failure.

A newer claimed field-strength validation also needs a different interpretation. Zhang et al. (2024), [full primary publisher PDF](https://mdpi-res.com/d_attachment/atmosphere/atmosphere-15-00767/article_deploy/atmosphere-15-00767.pdf), §§4–5, compare a horizontally stratified anisotropic transfer-matrix model using roughly 7,100-km, May–June 2015 observations. Automatic gain control prevents their received-level data from directly representing incident field, so the comparison target is **MOF-inverted field calculated using P.533**, not calibrated raw field. Thus improved agreement is not independent validation of absolute weak-signal levels [interpretation here]. The model includes semiempirical system loss, omits horizontal inhomogeneity and acknowledges numerical overflow in extensive matrix cascades. Its observation data are request-only. It is a possible modeling alternative requiring numerical and measurement validation, not a demonstrated universal improvement over P.533.

**Other evidence.**

- *MF.* Root-mean-square errors of the ITU-R method are 9.3 dB on intra-African paths, 5.8 dB on 87 Region 2 paths and 4.9 dB on 84 Asian paths (Handbook §5.3.2) [B, read].
- *High latitudes.* On a 285 km Norwegian path (Andøya–Alta, March 1988), ICEPAC was within ±20 dB of measured transmission loss (100–150 dB) in quiet conditions. Under disturbance it erred by up to about 200 dB in both directions: too much loss in the late morning, too little for most of the day, when the measured absorption at Qe = 8 was 150–280 dB. Measured circuit reliability was zero all day [G, read: Lane 2005, citing Jodalen & Thrane 1994].
- *NVIS.* Walden (2012), [primary indexed full text](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2011RS004914), §§2–5 and Tables 6–7: nine 5.29-MHz links over 23 months yield conditional RMS errors of 7–15 dB for VOACAP and 9–16 dB for ASAPS; measured-minus-predicted means are generally negative. The comparison selects SNR >15 dB, excludes Kp >4 and sporadic-E cases, and samples the peak of five-second carriers every fifteen minutes. Seasonal mismatch is real, but these selected peaks and censored weak signals do not calibrate an outage tail.
- *Physics-based model against amateur links.* Chartier et al. (2023), full [primary paper](https://doi.org/10.1029/2023SW003590), use reported WSPR detections on 500–2,000-km, 2–30-MHz routes in March 2019. Their 55.5% measure is one-sided model ray-path closure among received reports; it excludes nonreports and D-region absorption. It is neither communication success probability nor calibrated received-field accuracy. The reported 27% NmF2 bias concerns the modeled ionosphere.
- *High-latitude foF2.* E-CHAIM and A-CHAIM remain relevant model candidates [P, abstract: Themens et al. 2017; Reid et al. 2023]. Their uninspected numerical skill claims are not calibration inputs; the inspected assimilation comparisons above/below establish why geography, variable and validation selection must accompany a reported improvement.
- *Assimilative foF2 nowcasts.* McNamara et al. (2013), full [primary paper](https://doi.org/10.1002/rds.20020), §§2–7, 10–12, test GPSII/EDAM against South African ionosondes in September 2011: Grahamstown supplies assimilation data and other stations provide comparison profiles and manually scaled oblique MOFs. Best-site foF2 RMS errors of 0.2/0.5 MHz (night/day) apply after filtering autoscaling failures, unavailable assimilation data and nonphysical model outputs. Profile validation can favor the model sharing the inversion method; oblique MOF is a more independent observable. The proposed TID error floor also contains inseparable scaling errors, and missing-data operation was not qualified. These are conditional ionospheric-frequency errors, not a received-level or dropout distribution. Pignalberi et al. (2021), full [primary article](https://doi.org/10.3390/atmos12081003), find improved foF2 against assimilated ionosondes, no corresponding independent COSMIC improvement, and worse COSMIC hmF2 than IRI. Unresolved gradients and fast temporal variability remain; this is model-driver validation, not HF signal-level validation. In May 2024 the IRI UP IONORING nowcast over Italy overestimated foF2 in the storm's main phase (evening of 10–11 May) and agreed well in the recovery phase [P, read: Spogli et al. 2024].
- *HF nowcast of supported frequencies.* McNamara et al. (2009), full [primary paper](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2008rs004004), §§2–5, use fifteen-minute GAIM specifications with GPS-TEC assimilation, simplified one-/two-hop geometry and no sporadic E. Inskip–Rome and Irirangi–Sydney comparisons show daytime MUF underprediction and limited tracking of daily changes; sparse/poor ionograms and widely separated assimilation sites constrain attribution. Observed support and received-level examples are useful, but no real-time D-region absorption/noise inputs were available to calculate SNR. The paper does not validate an absolute weak-signal field model.
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

Table 4 applies these rules as a named operational-model sensitivity [E from T], not as independently measured oblique attenuation. Its crossing convention applies to this X-ray/HAF branch, not automatically to every riometer or PCA product.

**Table 4. Illustrative D-RAP product scaling (dB): vertical two-way / one hypothetical oblique hop at 10° elevation.**

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
- Chakraborty et al. (2021a), full [primary paper](https://doi.org/10.1029/2021RS007285), compare one-way vertical O-mode calculations with a selected dayside riometer corpus: 21 X and 37 M flares in 2011–2017, 410 station-event samples, with minimum peak/enhancement criteria. A relative skill score near 0.4 is not “40% absolute accuracy”; some formulations improve only marginally. A modeled subsolar frequency sweep becomes nonlinear below 5 MHz and saturates below about 4 MHz. That challenges a universal power-law extrapolation but does not validate oblique low-HF link loss. Temperature and background profiles remain important.

Levine et al. (2019), full [primary paper](https://doi.org/10.1029/2018RS006666), compare flare models with September 2017 ionosonde/NVIS observations, including 6.4-MHz O-mode data with an explicit vertical-to-oblique/two-traverse conversion. D-RAP underestimates the early X9.3 interval and agrees better later. Preserve event clock, background and geometry; this is not a generic attenuation assigned to all flares. Zawdie et al. (2017), full [primary paper](https://doi.org/10.1002/2017RS006256), show why collision-frequency choice must accompany the magnetoionic equation: for their 5-MHz vertical X-mode, 28°N/0°E example, matching appropriate collision profiles reduces Sen–Wyller/Appleton–Hartree total differences below 5%, despite much larger differences when an incompatible common collision frequency is imposed. Their night example is about an order of magnitude less absorbed than day and changes the D/E/F partition. Neither comparison is a measured universal oblique-path law.

*Observed time behaviour.*

- For the X9.3 flare of 6 September 2017 (1153 UT), HF recovery over Europe, then in the noon sector, did not begin until about 1300 UT and took almost 2 h. Over the continental United States, then in the dawn sector, 7 and 14 MHz began to recover within 10–15 min and recovered fully within an hour. In the Caribbean the flares of 6, 7 and 10 September caused daytime blackouts with recovery times of tens of minutes to hours [P, read: Frissell et al. 2019].
- Ionosonde total fade-out lasted 15–150 min for flares above M6 in solar cycle 23, depending strongly on solar zenith angle; the minimum echo frequency rose to 2–9 MHz [P, read: Barta et al. 2019].
- Tao et al. (2020), full [primary methods/results](https://doi.org/10.1186/s40623-020-01278-z), analyze Kokubunji 1981–2016 vertical ionograms at fifteen-minute cadence, selecting daytime events associated with ≥C1 flares and measuring minimum-frequency enhancement against a 27-day median. **Observed maxima** are 8 h 15 min for `dfmin ≥2.5 MHz`, 5 h 15 min for `≥3.5 MHz`, and 3 h 15 min for complete echo blackout; the longest enhanced-frequency event includes two flares. The quoted 10/100/1,000-year complete-blackout durations, 1.8/4.0/7.4 h, are **quadratic CCDF extrapolations fitted through three hours**, not observed return periods. Cadence, event selection, local daylight and the chosen tail function constrain their use. Their extreme absorption estimates additionally combine historical empirical laws with extrapolated flare intensity. Use these as explicitly theoretical duration/absorption branches, never as calibrated global communications-outage probabilities.

**Polar-cap absorption (solar proton events).**

- Attenuation at HF can exceed 100 dB. Onset is 15 min to several hours after the flare. Events number 0–14 per year, last about two days on average and up to four. Hours of PCA per year are roughly −70 + 4.4 R; the worst surveyed year had 780 h, 9 % of the year. During a storm the absorption can spread to about 55° (Handbook §2.3.2.6, citing Larsen 1974) [B, read].
- Sauer & Wilkinson (2008), full [primary paper](https://doi.org/10.1029/2008SW000399), §§3.2–5: the 30-MHz vertical single-transit parameterization is `0.115√J(>5.2 MeV)` by day and `0.020√J(>2.2 MeV)` at night. Their global product **doubles the vertical value** as a surrogate for a slanted reflected path with two lower-ionosphere crossings. The source therefore resolves the earlier crossing ambiguity. Applying another factor of two to that product would double-count this convention. Twilight interpolation between ±10° solar elevation is heuristic. The frequency exponent 1.5 has multifrequency-riometer/daytime support and is adopted at night without an equivalent night validation. Eleven major SEP events and Thule observations support the comparison; one event's initial discrepancy reaches roughly 50%. Neither the surrogate nor that calibration fixes arbitrary oblique geometry.
  - If a hypothetical spectrum has 1,000 flux units above **each distinct** energy threshold, the formulas give 3.64/0.632 dB vertical single transit, or 7.27/1.26 dB in the doubled product, at 30 MHz [E]. A single integral >10-MeV flux cannot supply both required lower-energy integrals. The previous >200-dB oblique example and duration inferred from this incompletely specified spectrum are withdrawn as physical evidence. A theoretical secant/frequency sensitivity branch may still be tested with its full input spectrum and traversal convention stated.

**Auroral absorption.**

- P.533's median allowance L_h peaks at 62.5–72.5° geomagnetic: 16.0 dB in winter (04–07 LT) and 21.4 dB at equinox (07–10 LT) for paths up to 2 500 km (Table 2) [S, read].
- Seven years of 38.2 MHz riometer data at Kilpisjärvi: most absorption is below 0.5 dB; values above 2 dB fall below the 0.1 % occurrence level; the peak is in the pre-noon sector [P, read: Kavanagh et al. 2004]. By inverse-square scaling, 0.5 dB at 38.2 MHz is about 7 dB at 10 MHz and 29 dB at 5 MHz, one way, vertical [E].
- Recent Ottawa–Alert evidence is a 2013–2019 scheduled-link reception/outage record at six frequencies from 5 to 14 MHz, alongside 30-MHz riometer measurements. Gillies et al. (2026; Fiori coauthor) §3.1 constructs reception occurrence by assuming transmitter and receiver both operated continuously on days with data; July–August 2016 is excluded because almost no receptions suggested at least one endpoint was not continuously operating. For absorption comparisons (§3.2), 4–24 scheduled pulses per operating hour provide an expected-pulse denominator; across the retained dataset 62% of expected pulses were unreceived, whether from absorption or lack of a supported propagation path. The paper reports additional loss probability in bins with remote riometer absorption above 0.5 dB (weighted averages 26–32% for most frequencies, 19% at 14 MHz; §4), relative to baseline loss, not a universal absorption-caused outage probability. The riometers are all west of the link, about 900–1,700 km from modeled D-region pierce points (§§2, 3.4, 4); the paper links the weak transfer to spatial extent/location of absorption regions and finds no sharp absorption cutoff. This is useful absorption-conditioned service evidence, not coherent complex-channel data or a direct local attenuation measurement [P; full paper, Gillies et al. 2026, §§2–4, DOI 10.1029/2025SW004906].

**Geomagnetic storms: MUF depression.**

- The average storm has a positive phase followed by a negative phase; poleward of about 35° there is often little positive phase. Storms last days (Handbook §2.6.1.1) [B, read].
- September 2017 (Kp 8+, SYM-H −146 nT): 14 MHz and then 7 MHz were wiped out from about 1200 UT on 8 September; effective bands stayed suppressed for 12–15 days [P, read: Frissell et al. 2019].
- May 2024 (Dst −412 nT): foF2 and vertical TEC over Italy fell to about 2 MHz and 4 TECu, about 70 % below quiet values, for two whole days (11 and 13 May). Ionograms were also lost to flare absorption [P, read: Spogli et al. 2024].
- The May 10–13 Asian-sector continuous-chirp study supplies direct *oblique-sounding* evidence for route changes and outages, not a modem fading distribution. On the Novosibirsk–Irkutsk, Magadan–Irkutsk and Magadan–Novosibirsk paths, storm main/recovery phases brought lower maximum observed frequencies and increased absorption; the authors report long blackout intervals, loss of regular 1F2/2F2 returns, intermittent F1/Es returns and diffuse/scattered returns near the shifted main ionospheric trough. On Novosibirsk–Irkutsk the initial 1F2/2F2 spread-F and diffuse returns were observed from 17:50 UT May 10; on Magadan–Novosibirsk the path crossed auroral and trough regions. Their source describes about 25,000 processed ionograms; vertical/NVIS sounding typically stepped at 1 min or less (with quick-mode examples down to 15 s), while the oblique-sounding path schedule is 5 min. It reports derived HFC/MOF histories and selected ionogram images, not raw voltage/IQ, modem C/N0/BER, a no-return denominator suitable for link probability, or joint delay–Doppler records. Published curves/ionograms may support digitized event-specific replay of mode/availability state, but cannot be converted to waveform replay. Preserve missing transmitter stations (Norilsk and Khabarovsk were unavailable for technical reasons) and the three actual routes when constructing any event scenario [P; read: Ponomarchuk et al. 2025, May 2024 storm study].

**Additional cycle-25 primary evidence, full papers read.** [Kitajima et al. (2026)](https://doi.org/10.1186/s40623-026-02525-5), §§2–4, use four Japanese ionosondes from Okinawa to Wakkanai, 1–30-MHz sweeps and five-minute fmin observations during six selected daytime X flares of May 8–15, 2024. The May 11 X5.8 event has echo-blackout lower bounds of 105/55/40/35 minutes by station. Adding modeled X-ray ionization improves fmin RMSE from 2.32 to 1.24 MHz in these windows, but station threshold calibration and evaluation on the same small event collection are not independent prospective validation. Sequential five-minute counts are not independent flares. The longer Wakkanai episode also requires the concurrent negative storm. These are vertical-echo/fmin constraints, not two-ended link or modem outcomes.

[Tao et al. (2026)](https://doi.org/10.1186/s40623-026-02462-3), methods/results, provide a Southern Hemisphere counterpoint at Syowa (69.0°S/39.6°E): 0.5–16-MHz, roughly 155-second FMCW sweeps every fifteen minutes. With normal transmitter power, absent echoes occur May 10 16:02–18:32, May 11 05:17–16:47 and May 16 05:32–10:32 UTC; October has further, sometimes intermittent, intervals. Proton forcing dominates the October interpretation, while May also involves auroral electrons and oval motion. Persistent auroral-zone phase scintillation during negative storms contrasts with northern polar-cap suppression; it is not a universal hemispheric rule. Echo absence, GNSS scintillation and communication availability remain different observables.

Cameron et al. (2026), full [primary methods and Table 1/§5](https://doi.org/10.1029/2025RS008395), provide a further MUF-proxy caution: their hourly maximum received frequency is the highest of six scheduled frequencies received on at least half the pulses. Frequency spacing is 0.7–3.3 MHz; undefined hours are not zero, and the highest available band is right-censored. A forty-storm QAN composite shows a 20.8% relative-MRF decrease four hours after average Dst minimum, while patches/Es can increase support. These are conditioned frequency-support observations, not a measured instantaneous MUF or a universal storm penalty. See [32](32-skywave-dispersion-highlatitude-equatorial.md) for the archive and estimator qualification.

Yasyukevich et al. (2026), [full primary paper](https://doi.org/10.3390/s26051490), §§2, 3.7–3.8, inspect 26 GIRO stations during November 12, 2025 03:00 UTC to November 13 11:00 UTC. Five high-latitude stations lack useful echoes in over 80% of soundings; **Alpena and Grahamstown are mid-latitude exceptions near 10%**, while other stations are near 3% or less. Preserve those exceptions rather than copying the simpler conclusion. These percentages concern soundings in a selected storm window, not independent link trials. Asian radar ground-scatter disappearance lasts one/two days while close-range ionospheric scatter appears; recovery includes enhanced noise as well as absorption. Such changes challenge an attenuation-only event model, but do not identify a complete terminal C/N0 or communications-outage denominator.

Verhulst et al. (2026), [full primary paper](https://doi.org/10.1029/2026RS008688), first published August 19, §§2–5 and Appendices A/B, demonstrate passive observations at six European DPS4D ionosondes, 38–54.6°N. Receive-only sweeps use 25-kHz steps, maximum gain, one antenna and O polarization, initially 10–30 MHz at 7.5-minute cadence. Relative band amplitudes are compared with a median of the preceding/following seven days. September/October 2024 and November 2025 flare examples show abrupt band-power reductions, while a solar radio burst enhances higher frequencies in one event. The May 2024 particle episode persists into the next day; its mixed auroral/proton attribution remains uncertain. Changing reflecting area, D/E/F support, absorption and local instrument noise all affect the observable. This is qualitative proof of principle, not calibrated incident field, an X-class-to-dB conversion or a transmission-attempt denominator. GIRO/TAV observations and a [code deposit](https://doi.org/10.5281/zenodo.20751690) are identified by the source; no raw complex-channel replay is inferred.

Liu et al. (2026), [full primary paper](https://doi.org/10.1029/2026SW005116), first published October 30, §§2–5, combine over 2,500 GNSS receivers at thirty-second cadence, three GIRO sites and GOES-18 for four X flares on November 9–14, 2025. A stronger X-class event can have a weaker TEC response because the EUV irradiance rate differs. All four show fmin changes/echo loss, but one event raises TEC while reducing foF2/NmF2 and hmF2; an interpretation involving redistribution is favored without a direct equatorial-electrojet determination. Background choices after storm recovery and ionosonde gaps limit timing/attribution. Thus TEC, flare X class and F-region peak density are not interchangeable absorption predictors. These four events provide neither calibrated communications loss nor a general outage probability.

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
| Fadeout, moderate / severe | 123 / 5 | mean 68 / 132 min | Fiori et al. 2022 [P, full §§3–5] |
| Proton event 10 pfu or more (S1) / 1 000 pfu (S3) / 10 000 pfu (S4) | 50 / 10 / 3 | S3: polar HF degraded; S4: polar HF blackout over several days | NOAA scales |
| PCA, moderate / severe | 24 / 13 | mean about 8 h / 1.6 days | Fiori et al. 2022 |
| Auroral absorption, moderate / severe | 40 / 3 | mean 5.1 h / 12 h | Fiori et al. 2022 |
| Post-storm MUF depression, moderate / severe | 200 / 56 | mean 5.5 h / 8.5 h | Fiori et al. 2022 |
| Storm Kp 7 (G3) / Kp 8 (G4) / Kp 9 (G5) | 200 / 100 / 4 | G5: HF may be impossible for one to two days | NOAA scales |

Fiori et al. (2022), full [primary article and threshold table](https://www.swsc-journal.org/articles/swsc/pdf/2022/01/swsc220003.pdf), distinguish SWF X-ray thresholds (X1/X10), auroral Kp proxies (8/9), PCA **30-MHz-equivalent** absorption (2/5 dB), and post-storm foF2 depression. These are different observables. Their historical rates and durations are conditional on definitions, geographic coverage and cycle; overlapping events and solar-maximum concentration prevent summing them into independent modem-outage probabilities. Kp is especially poor as a local absorption proxy: the high-Kp relationship is sparsely sampled/extrapolated and can disagree with individual riometers. Local observed absorption also differs from zenith-normalized absorption. Their discussion explicitly contrasts nondeviative exponent 2, empirical 1.5, 1.24 and frequency-dependent alternatives; none is universal. The table is now primary-checked rather than substituted from an operational notice.

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
- **160 m polarisation, peer-reviewed.** Vanhamel et al. (2022), full [primary paper](https://doi.org/10.1155/2022/4809313), measured 1.8–2-MHz WSPR reports with different receiving polarizations. Selected examples differ by roughly 3–13 dB and day/night variation can reach 22 dB; the O/X/gyrofrequency interpretation remains tentative. Detected-only reports and antenna/noise changes prevent treating this as an absolute propagation-loss distribution or success denominator.
- **Cross-modulation.** Measured modulation transfer at LF/MF in middle latitudes is below 7 %. A disturbing transmitter at 1.6 MHz would need about 1.4 MW e.i.r.p. at 150 km to become a nuisance, and above 5 MHz even HF broadcast powers are unlikely to matter (P.532-1) [S, read].
- **Eclipses.** Frissell et al. (2018), Usis et al. (2026), Kunduri et al. (2024) and Thomas et al. (2024) remain individually identified abstract-only leads for frequency-dependent level changes, moving skip distance and delayed recovery. Their former numerical amplitude/duration/lag claims are withdrawn from quantitative use. The moving shadow can motivate a theoretical changing-ionization branch; this chapter does not assign a calibrated eclipse channel or measured joint parameters without the primary methods and measurement convention.

### 6. Marginal and exotic modes

| Mode | Excess loss relative to the normal mode | Character | Occurrence | Source |
|---|---|---|---|---|
| Ground side- and back-scatter above the MUF | 25–40 dB below the great-circle mode | fading faster than 1 per second; variable azimuth | suggested dominant above-MUF mechanism to 4 000 km | P.2011-1 §2.2 [S, read] |
| Above-MUF patches and ground side scatter | distinct mechanisms; do not assign the printed P.533 cap as a measured bound | night, mid-latitude | Ottawa–Boston evidence is described below; mode identity is inferred | McNamara et al. 2008 [P, primary indexed body] |
| Ionospheric roughness | — | — | only about 60 kHz above the basic MUF in theory | P.2011-1 §2.1 |
| Direct E-region scatter | intensity ∝ f^−7.5 above 30 MHz | — | to about 2 000 km | P.2011-1 §2.6 |
| Sporadic E | Γ of 4–23 dB at 1 500 km for f/foEs of 2–5 | — | strongly seasonal; formula valid to 2 600 km per hop | P.534-6 [S, read] |
| Chordal and trans-equatorial | fewer ground/absorbing-region traversals are physically possible | event- and geometry-dependent | theoretical alternative; no occurrence or loss distribution assigned | P.2011-1 [S]; Nielson & Crochet 1974 [P, abstract lead]; Luetzelschwab [G] |
| Round-the-world and ducted | low attenuation | preferred occurrence periods; twilight-zone path | — | Toman 1979 [P, abstract] |

McNamara et al. (2008), [publisher-indexed primary body](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2007RS003742), §§1–4, 7–10, provide a useful above-MUF counterexample: 476-km Ottawa–Boston observations sampled every five minutes, with nearby hourly ionograms manually rescaled for quiet September/October 2003. Samples below 3-dB SNR were discarded, and cadence cannot resolve fast fades. At 7.335 MHz, the inferred sequence is regular refraction, possible overdense patches, then ground side scatter; observed frequency dependence does not uniquely distinguish ground from ionospheric scatter. Sporadic-E contamination shifts monthly medians, and no arrival-angle measurements establish each mode. The approximately −80 to −110 dBW change is received power for that equipment/path, not basic loss or a universal tail. This qualifies the former table's patch-only interpretation without establishing rare-mode occurrence rates. Direct publisher opening returned 403; the indexed primary methods/results were readable.
| Grey-line at 7 MHz | No calibrated loss distribution | Reception clustered at sunrise/sunset, asymmetric by direction | New Zealand/UK and eastern-Australian routes, 2017 detected WSPR reports; missing reports include equipment/noise/decoder effects | Lo & Rankov 2022, full [primary article](https://doi.org/10.3390/atmos13081340), methods/discussion |
| Long path over 9 000 km | Table 2 | delay spread assumed 3 ms at 7 000 km rising to 5 ms at 20 000 km | — | P.533-14 §10.2.3 [S, read] |
| 160 m E–F valley duct | about 60 dB better than multi-hop (model) | 20-minute openings | rare | Luetzelschwab [G, read] |

No source gave occurrence statistics for scatter modes on the 4 W scale relevant here.

### 7. What this means for the modem

For the stated isotropic port convention, `C/N0 = P_EIRP − L_port − N0` with all powers in dBW and `N0 ≈ −204 + F_a` dBW/Hz. At 4 W use the rounded expression `210.0 − L_port − F_a` dB-Hz. The receiver noise must refer to the same antenna/terminal convention. P.372's transfer from its reference antenna to a different antenna is an approximation; actual direction, polarization, ground loss, efficiency, receiver noise and local interference require the antenna/noise contract in chapters [50](50-noise-and-interference-mf-hf.md) and [55](55-equipment-and-front-end-effects.md).

**Table 5. Fixed rural-man-made-component anchor, 4 W EIRP, 0 dBi receive gain.** `F_am = 67.2 − 27.7 log10(f_MHz)` is the component median, deliberately held fixed. Atmospheric/galactic/receiver/interference terms are omitted here, so this is a lower-noise control, not a percentile of total noise or a predicted service probability. In particular, summing component medians does not in general give the median of their random sum. Galactic visibility itself depends on ionospheric screening.

| Specified state from Tables 1–2 | f (MHz) | L_port (dB) | Fixed F_am (dB) | C/N0 (dB-Hz) |
|---|---:|---:|---:|---:|
| 1,000 km, January, R12 10, 23 UTC | 3.5 | 114.153 | 52.129 | 43.7 |
| 1,000 km, January, R12 10, 11 UTC | 7 | 132.784 | 43.791 | 33.4 |
| 3,000 km, July, R12 150, 11 UTC | 3.5 | 293.569 | 52.129 | −135.7 |
| 10,000 km, January, R12 10, 23 UTC | 7 | 144.327 | 43.791 | 21.9 |
| 10,000 km, July, R12 150, 11 UTC | 28 | 180.004 | 27.114 | 2.9 |

The very weak third row is an extrapolated climatological attenuation stress, not a measured −135.7-dB-Hz communication channel. The last row fixes the former error of assigning a **best-band** loss at one frequency to the noise at a different frequency. A source-supported storm trajectory can change mode support, attenuation and noise separately, as described in [83](83-scenarios-skywave.md); a deterministic added loss is only a theoretical sensitivity. Do not add model-error SD, day-to-day deciles and within-hour fading as independent Gaussian margins: they describe different conditional levels and may overlap. Use shared latent state and explicit dependence sensitivity until joint data justify more.

Time scales to expect:

- **Seconds to minutes:** fading (chapters 31–33); fadeout onset in about 100 s.
- **Tens of minutes to hours:** fadeout (15–150 min; mean 68 min at the moderate threshold); MF terminator transitions (about 19–26 dB in two hours); travelling disturbances (20 min to 2.5 h); auroral absorption events (about an hour each, 5–12 h as events).
- **Hours to days:** post-storm MUF depression (mean 5.5–8.5 h; one to two days in superstorms); polar-cap absorption (8 h to four days).
- **Days to weeks:** post-storm absorption (up to ten days or more); suppressed bands (12–15 days in September 2017).

A 10-minute or 96-minute bit is as long as a fadeout or a terminator transition, so the level is not stationary within a symbol in those conditions.

## Moderate → adverse → extreme cases and coverage limits

“Worst case” must specify its conditioning and observable. No finite literature search establishes a global maximum loss or duration. Preserve measured events, climatological models and deliberate theoretical stresses as separate cases.

| Case family | Quantitative anchor | Authorized interpretation |
|---|---|---|
| Quiet, in-band mid-latitude reference | Table 5: 1,000 km, 3.5 MHz, January/R12 10/23 UTC, L_port 114.153 dB | Conditional climatology with an explicit fixed-noise control; not a real station's success rate |
| Moderate sunlit reference | Table 5: 1,000 km, 7 MHz, January/R12 10/11 UTC, L_port 132.784 dB | Same source-to-parameter chain; compare varying noise and antenna efficiency separately |
| Adverse long route | Table 5: 10,000 km, 28 MHz, July/R12 150/11 UTC, L_port 180.004 dB | Composite empirical model, with unvalidated weak-tail accuracy |
| Deliberate low-band attenuation stress | Table 5: 3,000 km, 3.5 MHz, July/R12 150/11 UTC, L_port 293.569 dB | Model-generated extreme, not an observed naturally bounded worst case |
| Native D/E absorption component | 7 MHz/1,000 km: 0.257–0.645 dB at 23 UTC, 15.107–29.200 dB at 11 UTC; 3.5 MHz/3,000 km/11 UTC: 68.423–168.561 dB | Strongest-mode components over the stated four months and two R12 values; mode loss differs from summed-port loss |
| Observed flare/negative-storm echo episode | Japanese May 2024 and Syowa May/October 2024 cases below; May 2024 oblique mode trajectory in 83 | Actual cadence and source-specific echo/mode support; not exact zero RF or communications outage |
| Day-to-day and within-hour spread | P.842/CCIR source-specific deciles in section 2 | Different statistical levels; select one consistent daily model and preserve dependence rather than stacking all tables |
| Monthly prediction discrepancy | Historical D1 and selected Walden residuals in section 3 | Circuit/model/input uncertainty, not a Gaussian event or fade generator |
| Polar-cap/auroral event stress | Calibrated 30-MHz riometer observables plus explicit frequency/geometry/chemistry alternatives | No universal Kp-to-local-loss conversion or independently sampled extreme factors |

| Frequency/path stratum | What the evidence supports | Explicit theoretical branch where observations are insufficient |
|---|---|---|
| 1–1.7 MHz | P.1147 night-field convention and qualified daytime/terminator corrections; ground/sky interference depends on antenna/site | Magnetoionic ray/ground coupling and alternative daytime attenuation, without transferring HF loss statistics |
| >1.7 to <2 MHz | Vanhamel's measured polarization-sensitive detected WSPR reports; some historical/anecdotal openings | Bracket E/F/duct support, collision/absorption and antenna response separately; no P.533-derived calibrated loss distribution |
| 2–5 MHz | P.533 stated domain; source-specific NVIS and ionosonde observations; absorption can depart from a simple frequency power law | Explicit collision-profile/ray-mode branches and threshold-censored non-detection; no universal low-HF secant extrapolation |
| 5–15 MHz | Better direct NVIS/oblique/high-latitude coverage, but individual archives retain selection, cadence and geography limits | Joint mode/level/dispersion sensitivity after the source-native estimator; no extrapolated universal outage rate |
| 15–30 MHz | P.533 within its stated band; above-MUF/Es/long-route modes and frequency-dependent noise visibility | Competing empirical tail laws and physical scatter mechanisms; capped residuals are not a measured signal floor |
| >9,000-km, antipodal/complementary long routes | Empirical composite models, detected-only grey-line reports and specific old observations | Focusing, chordal/duct and great-circle alternatives with explicit geometry; no inferred occurrence or 4-W success probability |

No source observes all extremes together. A combined synthetic maximum remains a deliberate robustness test. Flare, storm and proton forcing can coincide while acting on different parts of the path; jointly replayed event evidence is preferable when its observables are available.

## How the literature models or simulates this

- **ITU-R P.533-14 and ITURHFProp.** Monthly-median field strength, MUF and reliability for 2–30 MHz. Calibrated to the D1 data bank, which is distributed with the reference code [G, read: repository listing]. No flare, proton or storm-day behaviour. It warns of distortions in the equatorial evening sector (§1). An equatorial-scatter attachment gives a probability of occurrence.
- **P.842-5.** Reliability from decile deviations of signal and noise, combined by root-sum-square.
- **VOACAP / IONCAP / ICEPAC.** Ray-hop modes to about 10 000 km and a forward-scatter long-path model, with "Method 30" smoothing between them [G, read: Lane 2005]. Signal distributions come from the excess-system-loss tables. Lane judges VOACAP accurate for undisturbed conditions and ICEPAC unreliable at high latitudes.
- **CCIR Supplement to Report 252-2 (1980).** The most explicit term-by-term loss model; superseded but still the clearest source for individual mechanisms.
- **P.1147-4, and the FCC/Wang method.** MF night skywave. The Wang method fits long paths and United States data better [B, read: Handbook §5.3.3].
- **NOAA D-RAP.** Real-time flare and proton absorption maps; archive at one-minute cadence since September 2009 [T, read: NCEI readme].
- **Physics-based absorption.**
  - Pederick & Cervera (2014), full [SiMIAN paper](https://doi.org/10.1002/2013RS005274), §§2–6, integrate magnetoionic absorption along rays with atmospheric collision profiles, assuming absorption does not materially alter the traced ray. The showcased Kowandi vertical soundings use observed profiles inverted with POLAN, FIRI lower layers, and **O/X and first/second-hop power ratios**; they explicitly do not validate absolute absorption. Ratios are typically within about 3 dB in their limited comparison, with interference exclusions and assumed common equipment response. Ground-reflection loss is omitted from the hop-ratio comparison. The observed-data caption says 19 November 2012 while several modeled captions say 2013; the publication contains this inconsistency. Seasonal, solar-cycle, geographical and oblique-link validation remain limited. This supports a physically richer candidate, not a globally calibrated absolute-loss replacement.
  - Siskind et al. (2017), full [primary paper](https://doi.org/10.1002/2016SW001546), §§2–6, link WACCM/NOGAPS neutral composition, OASIS chemistry and MoJo absorption. Selected historical rocket electron-density profiles test the driver; the propagation calculation is upward, vertical, nondeviative X-mode at 2/5 MHz, with 2008–2009 solar-minimum inputs. It assumes steady photochemical equilibrium, excludes night and energetic-particle precipitation, and has a tropical low-altitude density deficit. Its tropical profile requires an extrapolation below 66 km. Low-latitude absorption peaks near the lower E region, whereas winter nitric oxide makes the middle D region important at higher latitudes. These conditional results reconcile apparently competing D-versus-E absorption descriptions; they do not establish an absolute oblique-link loss distribution.
  - Zawdie et al. (2017) on absorption formulations [P, full methods/results; matched collision-frequency/profile conventions required].
  - Flare models: Levine et al. (2019); Siskind et al. (2022); Chakraborty et al. (2021a).
- **Proton and auroral absorption.** Sauer & Wilkinson (2008); the full assimilation/twilight studies below; cutoff models (Dmitriev et al. 2010; Neal et al. 2013); Kp-quadratic auroral statistics (Kavanagh et al. 2004); hourly-range index (Fiori et al. 2020).
  - Rogers & Honary (2015), full [accepted primary manuscript](https://eprints.lancs.ac.uk/71645/4/Rogers_and_Honary_2015_swsc140045_accepted_version_.pdf), §§1–5, analyze fourteen riometers and 94 proton events. Wide beams are corrected to zenithal CNA; frequency normalization assumes exponent 1.5, including its less-supported night use. Sudden-commencement windows and corrupt/calibration samples are excluded. The reported 36% RMSE reduction comes from optimizing parameters against that corpus; it is not held-out global forecast skill. A separate thirteen-event experiment transfers hourly Taloyoak fits to other stations, improving some intervals while worsening others. Quiet-day-curve uncertainty at low absorption and weak cosmic signals at high absorption constrain the measured tails. Thus assimilation is promising with spatial validation and instrument error retained; it does not calibrate arbitrary oblique HF loss.
  - Rogers et al. (2016), full [primary paper](https://doi.org/10.1002/2016SW001527), §§3–5, fit thirteen multiday proton events at four polar-cap stations; four near-solstice events lack adequate zenith-angle coverage for twilight fitting. Sunrise/sunset responses differ, and a simple Earth-shadow transition can substantially mistime observed absorption. Their separate March 2012 demonstration assimilates twenty-five riometers with 24-hour age weighting and adjusts the cutoff boundary. Improvements exceeding 30% RMSE in a latitude band are errors at assimilated stations, not a withheld-site communication forecast. Chemistry and aperture averaging do not fully explain the observed morning delay. Use separate dawn/dusk response histories and spatially distributed observations; a single symmetric ±10° twilight law is a model sensitivity choice.
- **Ionospheric specification.** IRI-2020 (monthly averages, with storm option) [P, abstract: Bilitza et al. 2022]; E-CHAIM and A-CHAIM for high latitudes; the near-real-time polar HF model of Warrington et al. (2016), which assimilates ionosonde, TEC, proton and X-ray data.
- **Data sets for validation.** D1 data bank; riometers; SuperDARN ground scatter and noise; ionosonde minimum-frequency records; amateur networks. The amateur networks were designed to monitor path openings, not for finely calibrated measurements; timing is uncertain by about ±1 s, frequency by about ±1 Hz, and reports come every 1–2 min [P, read: Frissell et al. 2023]. That their reports carry no separate noise measurement is not stated there (not confirmed on re-check).

## What a high-fidelity simulation would need

These are options with their limits, not a design; better approaches may exist.

1. **A climatological median.** P.533 or VOACAP output for the chosen path, hour, month and R12 (version 1). *Limit:* monthly median only; errors of 7–16 dB at one standard deviation; unreliable at high latitudes and below 2 MHz.
2. **A scenario bias.** Apply a shared circuit/month uncertainty control across frequencies and days, with predeclared sensitivity offsets. Historical residuals motivate testing multiple offsets, but do not identify a universal probability law or maximum error. Do not reinterpret the corrected 11-dB implementation disagreement as physical uncertainty.
3. **Day-to-day level.** A split-normal draw from the P.842 deciles, or from the older CCIR tables where season and local time matter. *Limit:* no day-to-day correlation is given by either source; storm sequences persist for days.
4. **Daily MUF.** A daily foF2 factor from the P.1239 deciles, with an above-MUF loss law. *Limit:* the available laws differ by tens of dB; the choice should be a parameter, not a constant.
5. **Within-hour structure.** Hand over to the dispersion models, keeping the 5/8 dB deciles as a check.
6. **Event layer.**
   - Flares: use an observed event clock or an explicitly constructed arrival schedule. Compare named frequency-law sensitivities (for example 1.5/1.6/2) with collision/profile-based absorption and the nonlinear low-HF alternative; do not call the exponent interval a validated envelope. Account for illumination and the source measurement operator.
   - Proton events: preserve the energy spectrum, distinct day/night energy thresholds, twilight chemistry and spatial cutoff. A fixed day/night ratio cannot replace those inputs; observed durations can be days.
   - Auroral absorption and storm MUF depression: use local/event evidence where available; Kp may index a scenario but does not uniquely determine local attenuation or density change.
   - An alternative is to replay recorded drivers (X-ray and proton flux, Kp, ionosonde foF2) for documented periods such as September 2017, May 2024 and October 2024. *Limit:* replay covers few events.
7. **MF module.** P.1147 with its transition curve, latitude and solar terms, night-to-night log-normal spread and the daytime residual. *Limit:* 1.7–2 MHz is extrapolation; long paths are under-predicted.
8. **Rare enhanced modes.** Treat duct/chordal/Es/grey-line alternatives as separate physical hypotheses with continuous state. Selected reports motivate their inclusion, but do not identify an arrival rate, amplitude distribution or duration law.

**Observables to validate against:** D1-style monthly medians and their spread; ionosonde minimum-frequency fade-outs; riometer absorption; SuperDARN echo loss; amateur-network link counts and signal-to-noise distributions (with independent noise calibration); time-signal carrier amplitude records.

## Comparison with the modem's presets

The existing delay/Doppler presets specify dispersive controls, not calibrated received levels or event occurrence. Their static-level baseline remains useful. A physical HF case must additionally name frequency, route, actual UTC, R12 convention, source model/version, antenna planes, noise components and event state. Tables 1–2 supply reproducible climatological examples; Table 5 supplies a dimensional conditional path to C/N0.

| Existing control | Supported comparison and limit |
|---|---|
| HF best/moderate/worst delay–Doppler settings | Add independent level/noise controls; do not infer a measured joint worst case from separately selected maxima. P.842 deciles are statistical checks, not universal penalties to stack. |
| Static −164 dBm/Hz noise | A normalized experiment control. It is not a site's HF noise measurement or a complete P.372 total. |
| A normalized case yielding 20 dB-Hz | In the 4-W-EIRP, 0-dBi, isotropic-equivalent convention, require `L_port + F_a ≈ 190 dB`. This does not authorize applying the groundwave short-monopole GUI correction to skywave. |
| Illustrative fixed rural component at 3.5/7/14 MHz | The corresponding 20-dB-Hz loss budgets are 137.9/146.2/154.5 dB. Additional noise reduces them; changing antenna conventions requires recomputing both signal and noise. |
| 180-dB loss or an above-MUF model residual | Neither specifies a universal distance, storm category or outage. Frequency, mode support, model extrapolation and noise are essential. |

## Resolved discrepancies and remaining limits

**Printed-equation and implementation branches.** The [official Recommendation](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.533-14-201908-I!!PDF-E.pdf) and pinned source have the following concordance. A sensitivity substitution is not a newly validated “correct P.533” implementation.

| Quantity | Printed P.533-14 | Executed reference branch | Consequence |
|---|---|---|---|
| Absorption normalization | Sum over the 2n penetration points | Average over those points, multiplied by n | Holding all point terms/geometry equal gives a factor-two difference in dB absorption. Pointwise/control-point gyrofrequency choices must also be matched. |
| L_z / L_y | 8.72 / −0.14 dB | 9.14 / −0.17 dB | Calibration constants are implementation-dependent; do not mix them silently. |
| F2 above-MUF, D ≤3,000 km | `min(36 sqrt(r−1),62)` | `min(36 sqrt(r−1)+5,60)` | At r = 1.4, 22.77 versus 27.77 dB. |
| F2 above-MUF, D >3,000 km | Same printed F2 law | `min(70(r−1)+8,80)` | At r = 1.4, 22.77 versus 36 dB. |
| E above-MUF | `min(130(r−1)^2,81)` | `min(46 sqrt(r−1)+5,58)` | At r = 1.4, 20.8 versus 34.09 dB. |
| Clock and output grid | State the physical UTC and coordinates | Input hour is one-based; grid coordinates override receiver fields | Native replay must retain these conventions; the original Kranji comparison did not. |
| Per-mode versus terminal loss | Equations 17/43/44 | Official constants and power sum retained | A single mode adds 0.6 dB when translated to the terminal convention; mode summation can offset that difference. |

The code's 2020 calibration change and the v14.1 MUF-probability correction are separate from the v14.3 noise fixes. Neither release history nor the standard identifies an authoritative reconciliation of the absorption normalization. For the Kranji geometry, doubling the native absorption while changing only L_z to its printed value would change 161.670457 to **189.970546 dB** [E]; this is a controlled competing-law sensitivity, not the cause of the original 172.898372 result or a calibrated physical alternative. The original clock/geometry/absorption mismatch is already resolved above.

**Other consequential disagreements.** Above-MUF laws in P.2011 differ by tens of decibels; their mechanisms, fitted data and domains differ. Antipodal focusing is capped at 15 dB in P.533 and 30 dB in the older CCIR model. Nondeviative inverse-square absorption, D-RAP's empirical exponent and other fitted exponents are different approximations to frequency-, collision-, chemistry- and mode-dependent propagation. The Sauer primary text resolves the old PCA one/two-traverse ambiguity, while leaving arbitrary oblique geometry uncalibrated. A centered-dipole model, a modern magnetic field and an operationally fitted reference implementation are not interchangeable without sensitivity checks. The unqualified 1.8-MHz P.533 and mixed-reference-plane comparisons are withdrawn, not repaired by adding uncertainty.

**Research access and evidence status.** The native program and its grids have now been run. Previously unavailable primary methods/results of Rishbeth–Mendillo, Sauer–Wilkinson, Fiori 2022, Zawdie, Levine, Chakraborty 2021a, Vanhamel, Walden, Chartier, Pignalberi, Lo–Rankov, Tao 2020, Pederick–Cervera, Siskind 2017, Rogers 2015/2016 and McNamara 2008/2009/2013 were inspected through the stated routes; Walden used publisher-indexed body passages and tables. The full Fiori threshold table is included in that remediation. New Zhu, Kitajima, Tao, Cameron, Yasyukevich, Verhulst and Liu 2026 methods/results, Zhang 2024 and the Zhu public deposit were inspected. These are substantive source/reproduction remedies, not removal of budget notices. Each remaining abstract-only or secondary entry in the bibliography retains that grade and cannot supply a quantitative scenario calibration unless an independently identified full source supports the number.

**H07 event-source disposition.** The following specific leads retain their abstract/record grade. Their former numerical bullets are withdrawn from quantitative use; this does not claim their papers are inaccessible or fully inspected. The inspected substitutes address the consequential mechanism, with the stated observable limits. DOI routes identify the uninspected works for any later study requiring their particular events.

| Uninspected lead / primary route | Question retained | Inspected support and scoped consequence |
|---|---|---|
| [Berngardt 2019](https://doi.org/10.1029/2018SW002130) | Radar-noise attenuation and frequency dependence | Fiori 2022 and Chakraborty 2021a establish competing, nonuniversal scalings. No radar-derived exponent becomes a calibrated channel law. |
| [Chakraborty 2018](https://doi.org/10.1002/2017RS006488), [2019](https://doi.org/10.1029/2019SW002179), [2021b](https://doi.org/10.1029/2020JA028813); [Fiori 2018](https://doi.org/10.1029/2018SW001905) | Flare onset, Doppler flash, compound-event recovery | Full Levine 2019, Siskind 2022 and Tao 2020, plus the Chum/Doppler sources in 31, retain event evolution. No uninspected time constant is selected. Chakraborty 2021a itself is now fully inspected. |
| [Frissell 2014](https://doi.org/10.1002/2014SW001132) | Decoded amateur-network blackout | Frissell 2019 and Lo–Rankov 2022 retain measured-report changes and selection limits. Report-count reduction is not a transmission-attempt failure probability. |
| [Bland 2018](https://doi.org/10.1029/2018SW001916) | Multiday polar noise/backscatter suppression | Tao 2026 and Fiori 2022 support absorption episodes and distinct echo observables. The uninspected radar attenuation magnitude is not an input. |
| [Heino 2019](https://doi.org/10.1029/2018JA026192) | Whole-atmosphere chemistry and riometer skill | Rogers 2015/2016 and Siskind 2017 retain calibration, chemistry and twilight limitations; no sub-decibel global accuracy is promised. |
| [Heino–Partamies 2020](https://doi.org/10.1029/2020JA027935); [Neal 2013](https://doi.org/10.1002/swe.20066) | Energy-/time-dependent proton access boundary | Rogers 2015/2016 explicitly compare and adjust boundaries and examine lag. No fixed latitude, universal lag or affected-area percentage is selected. |
| [Fiori 2020](https://doi.org/10.1029/2020SW002572) | Local magnetic disturbance versus global Kp | Fiori 2022 documents high-Kp/local-absorption disagreement. An uninspected correlation coefficient is unnecessary to that conclusion. |
| [Araujo-Pradere 2002](https://doi.org/10.1029/2001RS002467), [Pierrard 2025](https://doi.org/10.3390/atmos16030299), [Themens 2024](https://doi.org/10.1029/2024GL111677) | Storm corrections and regional negative-phase behavior | Spogli 2024, Kitajima/Tao/Yasyukevich 2026 and Cameron 2026 retain regional differences, mode-support reductions and enhancements. No universal F2 disappearance, loss duration or STORM-model skill is inferred. |

Residual empirical limits are explicit: no eligible calibrated distribution of 1.7–2-MHz, >10,000-km received loss; no universal arrival/duration law for rare duct/Es/scatter openings; no jointly identified interday/cross-band level/noise distribution; no validated arbitrary deep above-MUF tail; and no complete contemporary global cycle-25 communications-outage denominator. Detected WSPR/RBN/PSK reports, vertical riometers, ionograms, radar echoes and packet reception are not interchangeable substitutes. These are unknown physical/statistical quantities, not licenses to claim a validated worst-case bound. The alternative branches and source-defined measurement operators above allow experiments without inventing those distributions. Unobtained book bodies and remaining primary-article leads are bibliographic limitations; no new quantitative conclusion is assigned to them.

The May 2024 oblique-mode figure extraction and processed QAN archive qualification belong to [83](83-scenarios-skywave.md), with source/estimator details in [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and [32](32-skywave-dispersion-highlatitude-equatorial.md). An earlier six-file schema inspection is not whole-corpus validation. Gillies' scheduled-pulse comparison remains explicitly conditional on endpoint operation and remote-riometer geometry. A source-derived mode-support trajectory still does not establish complex IQ, exact zero received RF, or modem completion behavior.

**Bounded search record (13 November 2026).** The recent-source pass first screened chapters 30–32 and used publisher/DOI routes, then the following exact web queries:

1. `2024 May G5 geomagnetic storm HF communication absorption polar cap HF radio link measurements DOI 2024 2025`
2. `2024 May geomagnetic storm equatorial HF propagation radio communication link availability absorption measured paper 2024 2025`
3. `site:agupubs.onlinelibrary.wiley.com 2024 HF radio propagation absorption storm polar cap communication links`
4. `2025 HF radio communication propagation outage absorption geomagnetic storm observational measured high latitude equatorial paper`
5. `2015 2023 HF skywave measured absorption received field strength WSPR model validation`
6. `2015 2023 HF ionospheric scintillation weak signal communications India Brazil Africa Indonesia`
7. `October 2024 X9 HF absorption communication measurements paper`
8. `2025 solar flare HF blackout measured ionosonde communications paper`

The first four recovered the Japanese and Syowa full papers; the extension checked older measurement/model comparisons and October-2024/2025 gaps, finding the Zhang and Yasyukevich papers and the recent absorption studies discussed here. DOI/title and cited-primary-source follow-ups were used for retained claims, including Verhulst (first published 19 August 2026) and Liu (30 October 2026). Exact DOI/title follow-ups also recovered McNamara 2008/2009/2013 and the H06 source bodies; the Rogers 2015 publisher route returned 403, after which its public Lancaster accepted manuscript was inspected. No paywall bypass or author contact was used. GNSS/TEC-only, UHF/satellite scintillation, solar-radio emission and official outage notices were not treated as measured HF link attenuation; news and informal sites did not supply quantitative anchors. The search adds East Asian, Southern auroral, Canadian and selected global sounder coverage, but recovered no new calibrated end-to-end availability denominator for equatorial Indonesia/India/South America or African/Southern-Ocean routes. This is an explicit bounded search result, not evidence that such measurements do not exist or a claim of exhaustive global coverage.

## H02–H09 research disposition

These dispositions resolve the selected research decisions by reproduction, primary-source analysis or an explicit evidence-limited branch. They do not claim every cited book/article was read or that the proposed modem experiments have been implemented.

| ID | Result | Boundary |
|---|---|---|
| H02 | Clean native reference build; Kranji fixture and 53 printed fields reproduced; 11.227915-dB discrepancy attributed term by term. All 1,320 grid cells rerun from the retained recipe with zero difference; affected tables, absorption examples, C/N0 and preset interpretations corrected. | Native monthly medians are model outputs. Invalid 1.8-MHz and unexecuted off-grid range claims were withdrawn; no code agreement is called field validation. |
| H03 | v14.3 commit/data/runtime pinned; printed/code absorption, constants, above-MUF laws, clock and power normalization compared. NTIA user-manual cross-reference mismatch checked; related ICEPAC primary equations inspected. | Printed versus native absorption calibration is not authoritatively reconciled. The branches remain separate. Exact original IONCAP-version equations are not asserted from its derivative. |
| H04 | Zhu 2026 full comparison and public D1/maps audited; Walden, Chartier, Pignalberi and McNamara 2009/2013 primary methods/results distinguish received-level, mode-support and ionosphere-driver errors. | D1 has calibration overlap and a one-row deposit/paper discrepancy; paired model residuals are not deposited. Conditional/censored RMS errors do not define independent Gaussian storm or weak-signal tails. |
| H05 | Valid-domain map covers 1–30 MHz; native long-route/above-MUF cases checked; Vanhamel, Lo–Rankov and McNamara 2008 primary evidence qualify low-HF/polarization, terminator and scatter interpretations. | 1.7–2-MHz extreme-distance loss and rare duct/Es/scatter arrival laws remain uncalibrated. Toman/Nielson–Crochet remain named leads; theoretical branches carry no invented occurrence rate or absolute-loss confidence. |
| H06 | All named H06 primary methods/results now inspected, including Fiori's actual threshold table, Sauer traversal normalization, Tao tail extrapolation, Rogers assimilation/twilight and collision/profile-sensitive models. | No universal frequency exponent, secant factor, dawn/dusk transition or outage rate follows. Relative ionogram-power validation, riometer CNA and oblique communications remain distinct. |
| H07 | Chakraborty 2021a inspected; the individual table above maps each remaining event lead to inspected support and withdraws its uninspected numerical authority. Existing full event studies retain onset/recovery, absorption, cutoff, storm and measurement-selection mechanisms. | Remaining named bodies retain their actual grades. Their particular numeric events/skill values cannot calibrate a scenario; no claim of exhaustive source reading is made. |
| H08 | Dated exact searches, primary citation follow-ups, exclusions and regional limits recorded; recent Japanese, Southern auroral, Canadian and global sounder studies inspected with distinct observables. | Global modern weak-link availability is not established. Uninspected eclipse bodies and Hayakawa (partial), Gonzalez-Esparza and Kwak remain contextual leads, not numerical inputs. The absence of an eligible result in a search cell is not proof of absent measurements. |
| H09 | Rishbeth–Mendillo primary density statistics corrected; version-1 sunspot input, model bias, daily variability, within-hour fading and noise-reference conventions separated. Shared latent-state/dependence sensitivities replace unsupported independent stacking. | No identified cross-band/day/noise joint law or extreme-event probability. Mendillo 2006, Buonsanto 1999 and Wheeler 1966 remain bibliographic/secondary leads without selected quantitative parameters. |

## Reproduction and calculation provenance

The following is an executable recipe for the native grid; it does not require the retired bespoke script. Use temporary storage. The checkout, data and compiled libraries are reference tools only, not a proposed operating modem dependency.

```sh
git clone --depth 1 --branch v14.3 https://github.com/ITU-R-Study-Group-3/ITU-R-HF.git /tmp/itu-hf-reference
cd /tmp/itu-hf-reference/Linux
make clean
make all
export HF_ROOT=/tmp/itu-hf-reference
export LD_LIBRARY_PATH="$HF_ROOT/P533/Linux:$HF_ROOT/P372/Linux"
```

`make clean` is necessary because the source repository includes prebuilt objects. The first local `make all` only relinked them; that attempt was superseded by the clean compilation above. The executed compiler was GCC 14.2.0 (Debian 14.2.0-19), stock C99/O2 makefiles; warnings were retained in the temporary build log, and the build exited successfully. No installation or modem test was performed.

```python
# Save/run in temporary storage with HF_ROOT and LD_LIBRARY_PATH as above.
from pathlib import Path
import os, re, math, subprocess, tempfile
src = Path(os.environ['HF_ROOT'])
out = Path(tempfile.mkdtemp(prefix='hf-grid-'))
base = (src/'ITURHFProp/Bin/itu_old.in').read_text(errors='replace').replace('\x1a', '')
months = ['January','February','March','April','May','June',
          'July','August','September','October','November','December']
rows = []
def run(name, tx, rx, ssn, mons, hours):
    edits = {'PathName': '"'+name+'"', 'Path.L_tx.lat': tx[0],
             'Path.L_tx.lng': tx[1], 'Path.SSN': ssn,
             'Path.month': ','.join(map(str, mons)),
             'Path.hour': ','.join(str(h+1) for h in hours),
             'Path.frequency': '3.5,7,14,21,28',
             'DataFilePath': '"'+str(src/'ITURHFProp/Data')+'/"',
             'RptFilePath': '"'+str(out)+'/"',
             'RptFileFormat': '"RPT_DUMPPATH"'}
    for corner in ['LL','LR','UL','UR']:
        edits[corner+'.lat'], edits[corner+'.lng'] = rx
    text = base
    for key, value in edits.items():
        text = re.sub(r'^'+re.escape(key)+r'\s+[^\n]*',
                      key+' '+str(value), text, flags=re.M)
    inp, report = out/(name+'.in'), out/(name+'.out')
    inp.write_text(text)
    subprocess.run([str(src/'ITURHFProp/Linux/ITURHFProp'), '-s',
                    str(inp), str(report)], check=True, stdout=subprocess.DEVNULL)
    for block in report.read_text(errors='replace').split(
            '*      DumpPathData - p533() Path data structure')[1:]:
        def num(pattern):
            match = re.search(pattern, block)
            return float(match[1]) if match else None
        month = months.index(re.search(r'Month = (\w+)', block)[1])+1
        utc = int(num(r'Hour\s*=\s*(\d+)'))-1
        frequency = num(r'Frequency =\s*([-\d.]+)')
        loss = 30-num(r'Median Rx power =\s*([-\d.]+)')
        rows.append((name, ssn, month, utc, frequency, loss,
                     num(r'basic MUF\s*=\s*([-\d.]+)'),
                     num(r'Upper Reference Frequency[^=]*=\s*([-\d.]+)'),
                     num(r'Lower Reference Frequency[^=]*=\s*([-\d.]+)')))
for distance in [100,1000,3000]:
    half = distance/6371.009*180/math.pi/2
    for ssn in [10,150]:
        run(f'short{distance}s{ssn}', (40-half,10), (40+half,10),
            ssn, [1,4,7,10], [11,23,4])
for distance in [10000,19500]:
    lat, lon = 50-distance/6371.009*180/math.pi, 10
    if lat < -90:
        lat, lon = -180-lat, -170
    for ssn in [10,150]:
        run(f'long{distance}s{ssn}', (50,10), (lat,lon),
            ssn, [1,7], list(range(24)))
assert len(rows) == len(set(r[:5] for r in rows)) == 1320
assert all(math.isfinite(r[5]) for r in rows)
print('name,ssn,month,actual_utc,MHz,L_port,basic_MUF,fM,fL')
for row in rows:
    print(','.join(map(str,row)))
```

Table 1 groups by name/UTC/frequency and takes the minimum/maximum over four months; its MUF column spans that row's months/frequencies. Table 2 selects UTC 11 and 23 for individual entries, and minimizes over the five frequencies separately at each of 24 hours for the final daily range. The model reports three decimals, so sub-millidecibel grid distinctions are not retained. There are 1,320 unique finite outputs; 154 extremely attenuated outputs are numerically below −307 dBW, but none equals the −307 initialization sentinel. They remain extrapolated values, not measurements; the table displays losses >250 dB without suggesting physical-tail accuracy.

The Kranji reproduction uses the unmodified sample geometry/hour/frequency and only replaces data/report paths. Changing its diagnostic `#define BARF FALSE` to `TRUE` in a **temporary copy** of `MedianSkywaveFieldStrengthShort.c`, compiling it with the stock other P533 objects, prints the term values in section 1; it changes no arithmetic. Fifty-three printed geometry/MUF/field/loss/gyro/solar values match both the committed sample and the clean native/diagnostic outputs exactly at printed precision. The causal calculation substitutes the native penetration-point parameters into the scalar absorption expression; its 0.000004-dB residual is the rounding bound stated above. A further 24 native diagnostic cases (1,000 km at 7 MHz at UTC 11/23; 3,000 km at 3.5 MHz at UTC 11; four months and both R12 values) give strongest-mode absorption ranges 15.107–29.200, 0.257–0.645 and 68.423–168.561 dB, respectively. These replace the corresponding simplified absorption summaries.

| Artifact | SHA-256 of the executed input/build |
|---|---|
| `ionos01.bin` | `bc85f60176ab6c3ba87786991144746d5136b1b0d8c2954dac6e4c1d9ed73f29` |
| `ionos04.bin` | `f5e00cdf933924737eb58440e75cf8aff6fad6c1c1ee8f1b46d7088f275a9293` |
| `ionos07.bin` | `ef44f7dbbf5fce17b115fbb8ae4252959c90a462b96334ce722910ee94b5b422` |
| `ionos10.bin` | `2a32b327d7fcc12bfa53e4668dcbf66a8b686bc6f0022de7617ca2802237b50a` |
| `P1239-3 Decile Factors.txt` | `1ca1a014e11b2f33a7f8c7c47b824e5fd53dd3a9ff89f07bb257e02d33b20aa9` |
| Native `libp533.so` | `11898bac2789eb30ef62684de3143efe0cec0edaad1ffb549d0164ac812cf036` |
| Native `ITURHFProp` | `1b31171fcadc8b66b864f677f2a8992a57f87d89e945a824153a87dca59d5745` |

The binary embeds build-date/version text and is not promised bit-identical under another compiler/date. The recovered historical `p533_budget.py` SHA-256 is `ca458ac6d924c23388a368169a27f3a1b4b16fd6ce3ee0645ad60a29401087a2`; its failed Kranji term vector and controlled substitutions are retained above rather than promoted into a second implementation. For example, native absorption is independently reconstructed as `2 × (1 + 0.0067×140) × 83.076078 / ((6.2 + 0.506721)^2 × cos(75.566241°)) = 28.720086 dB`, agreeing within diagnostic rounding. The original temporary aggregate JSON SHA-256 is `a3649e0a1185e7e422a117a292a134bb9b505290420f07072b888e25c50f84be`; the table values, full input recipe and source/data pins above are the durable evidence. Temporary raw reports/builds served this calculation review and the concurrent P.372 check and were removed after final review. Redundant Git objects, unused antenna files and duplicate coefficient trees were already removed to recover temporary space; used data and shared libraries were preserved through their active checks, then removed after review.

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
- Teters, L. R. et al. (1983), NTIA Report 83-127, IONCAP user manual. doi:10.70220/31pdtxnb. Primary contents/introduction inspected; its section-number references do not verify an original IONCAP absorption equation. [Official scan](https://its.ntia.gov/publications/download/83-127_ocr_reduced.pdf).
- NTIA/ITS, [ICEPAC Technical Manual](https://its.ntia.gov/media/38678/icepac_tech_manual.pdf), §5.2 equations 5.1–5.4. Related primary oblique/vertical absorption derivation inspected; not asserted to be identical to every IONCAP version.

**Peer-reviewed [P]**
- Araujo-Pradere, E. A., Fuller-Rowell, T. J. & Codrescu, M. V. (2002), Radio Sci. 37(5). doi:10.1029/2001RS002467. Abstract.
- Barta, V. et al. (2019), Ann. Geophys. 37, 747–761. doi:10.5194/angeo-37-747-2019. Read.
- Berngardt, O. I. et al. (2019), Space Weather 17, 907–924. doi:10.1029/2018SW002130. Abstract.
- Bilitza, D. et al. (2022), Rev. Geophys. 60, e2022RG000792. doi:10.1029/2022RG000792. Abstract.
- Bland, E. C. et al. (2018), Space Weather 16, 1455–1469. doi:10.1029/2018SW001916. Abstract.
- Chakraborty, S. et al. (2018), Radio Sci. 53, 472–484. doi:10.1002/2017RS006488. Abstract.
- Chakraborty, S. et al. (2021a), Radio Sci. 56, e2021RS007285. doi:10.1029/2021RS007285. Full primary methods/results read.
- Chakraborty, S. et al. (2019), Space Weather 17, 1351–1363, doi:10.1029/2019SW002179; (2021b), JGR Space Phys. 126, e2020JA028813, doi:10.1029/2020JA028813. Abstracts; no numbers used.
- Chartier, A. T. et al. (2023), Space Weather 21, e2023SW003590. doi:10.1029/2023SW003590. Full primary methods/results read; detected-report closure, not signal-level accuracy.
- Dmitriev, A. V. et al. (2010), JGR 115, A12244. doi:10.1029/2010JA015380. Abstract.
- Fiori, R. A. D. et al. (2018), Space Weather 16, 1348–1362. doi:10.1029/2018SW001905. Abstract.
- Fiori, R. A. D. et al. (2020), Space Weather 18, e2020SW002572. doi:10.1029/2020SW002572. Abstract.
- Fiori, R. A. D. et al. (2022), J. Space Weather Space Clim. 12, 21. doi:10.1051/swsc/2022017. Full primary paper and threshold table read via the publisher PDF.
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
- Levine, E. V., Sultan, P. J. & Teig, L. J. (2019), Radio Sci. 54, 168–180. doi:10.1029/2018RS006666. Full primary methods/results read; September 2017 geometry and traversal conventions retained.
- Lo, S. et al. (2022), Atmosphere 13, 1340. doi:10.3390/atmos13081340. Full primary methods/results read; decoded-only 2017 7-MHz reports.
- McNamara, L. F. et al. (2008), Radio Sci. 43, RS2004. doi:10.1029/2007RS003742. Primary indexed methods/results read; direct publisher opening returned 403.
- McNamara, L. F., Baker, C. R. & Borer, W. S. (2009), Radio Sci. 44. doi:10.1029/2008RS004004. Full primary methods/results read.
- McNamara, L. F. et al. (2013), Radio Sci. 48, 143–157. doi:10.1002/rds.20020. Full primary methods/results read.
- Neal, J. J., Rodger, C. J. & Green, J. C. (2013), Space Weather 11, 420–433. doi:10.1002/swe.20066. Abstract.
- Nielson, D. L. & Crochet, M. (1974), Rev. Geophys. 12, 688–702. doi:10.1029/RG012i004p00688. Abstract.
- Pederick, L. H. & Cervera, M. A. (2014), Radio Sci. 49, 81–93. doi:10.1002/2013RS005274. Full primary methods/results read; relative-power validation only.
- Pierrard, V. et al. (2025), Atmosphere 16, 299. doi:10.3390/atmos16030299. Abstract.
- Pignalberi, A., Pietrella, M. & Pezzopane, M. (2021), Atmosphere 12, 1003. doi:10.3390/atmos12081003. Full primary paper read; assimilated and independent validation distinguished.
- Reid, B. et al. (2023), Space Weather 21, e2022SW003185. doi:10.1029/2022SW003185. Abstract.
- Rishbeth, H. & Mendillo, M. (2001), J. Atmos. Sol.-Terr. Phys. 63, 1661–1680. doi:10.1016/S1364-6826(01)00036-0. Full primary author-institution copy read: https://sirius.bu.edu/aeronomy/science2.pdf; NmF2 variance, not received-loss statistics.
- Rogers, N. C. & Honary, F. (2015), J. Space Weather Space Clim. 5, A8, doi:10.1051/swsc/2015009; Rogers, N. C. et al. (2016), Space Weather 14, 950–972, doi:10.1002/2016SW001527. Full primary methods/results read: 2015 accepted manuscript at Lancaster; 2016 publisher body.
- Sauer, H. H. & Wilkinson, D. C. (2008), Space Weather 6, S12002. doi:10.1029/2008SW000399. Full primary paper read, §§3.2–5; includes explicit doubled-vertical product convention.
- Siskind, D. E. et al. (2017), Space Weather 15, 115–130. doi:10.1002/2016SW001546. Full primary methods/results read; density-profile comparison and conditional absorption modeling.
- Siskind, D. E. et al. (2022), Space Weather 20, e2021SW003012. doi:10.1029/2021SW003012. Read.
- Spogli, L. et al. (2024), Ann. Geophys. (INGV) 67, PA218. doi:10.4401/ag-9117. Read.
- Tao, C. et al. (2020), Earth Planets Space 72, 173. doi:10.1186/s40623-020-01278-z. Full primary methods/results read; observed duration and extrapolated return levels distinguished.
- Themens, D. R. et al. (2017), JGR Space Phys. 122, 9015–9031. doi:10.1002/2017JA024398. Abstract.
- Themens, D. R. et al. (2024), Geophys. Res. Lett. 51, e2024GL111677. doi:10.1029/2024GL111677. Abstract.
- Thomas, E. G. et al. (2024), Geophys. Res. Lett. 51, e2024GL112450. doi:10.1029/2024GL112450. Abstract.
- Toman, K. (1979), Radio Sci. 14, 447–453. doi:10.1029/RS014i003p00447. Abstract.
- Usis, M. et al. (2026), Front. Astron. Space Sci. 12, 1720301. doi:10.3389/fspas.2025.1720301. Abstract.
- Vanhamel, J., Machiels, W. & Lamy, H. (2022), Int. J. Antennas Propag. 2022, 4809313. doi:10.1155/2022/4809313. Full primary paper read; antenna/polarization-dependent detected reports.
- Walden, M. C. (2012), Radio Sci. 47, RS0L09. doi:10.1029/2011RS004914. Primary indexed full text read, §§2–5 and Tables 6–7; direct publisher body route inconsistently accessible.
- Warrington, E. M. et al. (2016), Radio Sci. 51, 1048–1059. doi:10.1002/2015RS005880. Abstract.
- Zawdie, K. A. et al. (2017), Radio Sci. 52, 767–783. doi:10.1002/2017RS006256. Full primary methods/results read; collision/profile matching matters.
- Records verified but not used for numbers: Gonzalez-Esparza et al. (2024), doi:10.1029/2024SW004111; Kwak et al. (2024), doi:10.5140/JASS.2024.41.3.171; Wheeler (1966), doi:10.1002/rds19661111303; Mendillo (2006), doi:10.1029/2005RG000193; Buonsanto (1999), doi:10.1023/A:1005107532631.

- Cameron et al. (2026), *Ionospheric Effects on Maximum Usable Frequency for a Cross-Auroral and a Polar Cap HF Radio Link*, Radio Science 61, e2025RS008395. doi:10.1029/2025RS008395. Full primary §2, Table 1 and §5 inspected; hourly MRF is a thresholded scheduled-frequency proxy.
- Zhu, Z. et al. (2026), *Exploring the Applicability of Three Popular Models for Predicting the Received Field Strength of HF Sky-Wave Propagation and Communication*, Space Weather 24. doi:10.1029/2025SW004907. Full primary methods/results; [public deposit](https://doi.org/10.5281/zenodo.20413750) independently downloaded and inspected.
- Kitajima et al. (2026), *Reproduction and evaluation of the SWF magnitude observed by ionosonde in Japan during May 2024 using the PHITS code and GAIA model*, Earth Planets Space 78, 212. doi:10.1186/s40623-026-02525-5. Full primary methods/results read.
- Tao, C. et al. (2026), *Ionospheric variations observed at Syowa Station, Antarctica, during large space weather events in May and October 2024*, Earth Planets Space 78, 117. doi:10.1186/s40623-026-02462-3. Full primary methods/results read.

- Zhang, Z., She, J., Fu, H., Zhao, L. & Ji, S. (2024), *Investigation and Validation of Short-Wave Scattering in the Anisotropic Ionosphere under a Geomagnetic Field*, Atmosphere 15, 767. doi:10.3390/atmos15070767. Full primary PDF read; MOF-inverted comparison target and AGC limitation.
- Yasyukevich, Y. et al. (2026), *The 12 November 2025 Ugly Duckling Geomagnetic Storm: From the Sun to the Earth*, Sensors 26(5), 1490. doi:10.3390/s26051490. Full primary methods and HF results §§3.7–3.8 read.

- Verhulst et al. (2026), *Radio-Quiet Ionosonde Observations for Monitoring Absorption in the HF Band*, Radio Science 61(8), e2026RS008688. doi:10.1029/2026RS008688. Full primary §§2–5 and Appendices A/B read; passive relative band power, not calibrated field. First published 19 August 2026.
- Liu, L. & Zou, S. (2026), *Global Ionospheric Responses to Four X-Class Solar Flares in November 2025*, Space Weather 24(11), e2026SW005116. doi:10.1029/2026SW005116. Full primary methods/results read; TEC and ionosonde observables. First published 30 October 2026.

**Grey literature [G]**
- ITU-R Study Group 3, *ITU-R-HF* source repository (ITURHFProp/P533, version string 14.2; includes the D1 data bank and P.1239 grid files). Executed v14.3 commit `cd172be56dc04b154e5d2fa91cbaa6ecf5284305`, with the sample report `ITURHFProp/ReportsPDD190122-124921.txt`. The earlier review used May 2025 commit `82017594` and the calibration-constant history (3 June 2020, `9b93681a75`); those inspections are distinct from this pinned executable result. https://github.com/ITU-R-Study-Group-3/ITU-R-HF
- Lane, G. (2005), *Review of the High Frequency Ionospheric Communications Enhanced Profile Analysis & Circuit (ICEPAC) Prediction Program*, preprint 2B-1, Ionospheric Effects Symposium. Read. https://voacap.com/2023/documents/GLane_ICEPAC.pdf
- Luetzelschwab, C. (K9LA), articles at https://k9la.us : *Day-to-Day Variability of the Ionosphere* (WorldRadio, Aug 2004); *Validating Propagation Predictions*; *Multi-Hop at Night on 160-Meters*, *Ducting on 160-Meters*, *Polarization Observations on 160-Meters*, *Low Angles on 160-Meters* (Topband Topics, Aug 2011); *Absorption at Night*; *Ducting and Spotlight Propagation on 160m* (CQ, Dec 2005); *160m at Solar Maximum* (WorldRadio, Mar 2004); *20M Ionosphere-Ionosphere Mode*. Read.
- HamSCI publications listing, https://hamsci.org/publications. Read (used to locate records).
- Potter, R. & Frissell, N. (2026), *HamSCI Personal Space Weather Station Observations of the Gannon Geomagnetic Storm*, thesis, University of Scranton; Potter, R. et al. (2026), *Personal Space Weather Station Observations of the Gannon Geomagnetic Storm*, HamSCI Workshop 2026. Abstracts read. https://hamsci.org/bibcite/reference/583 ; https://hamsci.org/bibcite/reference/565

**Own computations [E]**
- Tables 1–2: 1,320 pinned native ITU-reference cases, with executable recipe, coefficient/build hashes and term audit above. The embedded recipe was independently rerun and reproduced all 1,320 terminal-loss values exactly at report precision. All sixteen Table-3 scalar cells, twelve Table-4 vertical/oblique pairs and three stated loss budgets were also checked against their displayed formulas; the Table-3 rounding discrepancy is recorded above. Table 5 and the printed-law comparison are explicit dimensional/sensitivity calculations, not measured modem performance.

# Antennas, links and practice below 9 kHz with a 61 m antenna

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** propagation [10](10-sub9khz-propagation.md); noise [12](12-sub9khz-noise.md); equipment [55](55-equipment-and-front-end-effects.md); independent arithmetic [90](90-cross-check-calculations.md).

**Summary.** A 61 m vertical is electrically tiny below 9 kHz (h/λ ≈ 1.7×10⁻³ at 8.27 kHz). Efficiencies of −42 to −64 dB at 8.27 kHz and −54 to −77 dB at 2.97 kHz turn 100 W into 0.04–7 mW and 2–360 µW e.m.r.p. (two measured 91 m umbrellas at 21 kHz fell about 10–22 dB short of their 0.9 % design, depending on antenna and estimator, with coil Q about 85 [T]); corona onset (17–29 kV on thin wire, about 100 kV engineered) caps a top-loaded antenna near 5–160 mW at 8.27 kHz whatever the power [E, T]. Bandwidths of 6–620 Hz far exceed the modem's 0.06 Hz, so detuning matters: 1–4° for a 1 m sideways wind bow of a fixed T but roughly 4–65° for 1 m of extra sag, tens of degrees and 1–6 dB for kites and balloons, gross under ice [E, T, G]; one example day at a US Navy LF (60–80 kHz) station shows an hour-long mistuning costing over 1 dB and a 10 % swing in gross antenna resistance, and the Cutler VLF station de-ices one of its two arrays about 13 times a winter because ice would otherwise exhaust the retuning range [T]. Earth-electrode dipoles win only when long, over poor ground or below about 3 kHz; their modelled e.m.r.p. carries a 6 dB model uncertainty, Wheeler's effective height being half this chapter's [E, P]. Amateur "ERP" computed as 1.8·I²·R_r is 2.6 dB above e.m.r.p.; ERPs fitted to received fields may already be e.m.r.p. [G, E].

Amateurs radiated 40 nW–58 mW ERP in far-field carrier tests below 9 kHz (the 58 mW kite record of 2010 is a carrier detection that predates EbNaut) and decoded EbNaut messages to 7,493 km at ERPs from about 18 nW (970 Hz over 40.5 km, just inside λ/2π; lower notional ERPs only in the deep near field) up to the sub-milliwatt class. Over 0.8–7.5 Mm the decodes span about −15 to −47 dB-Hz (−22 to −42 dB-Hz in a smaller set that includes two carrier detections) and 0.5–70 information bits per hour (about 250 bits/h only at 254 km, near −10 dB-Hz), in single transmissions of about 1–28 h or coherent stacks of up to 18 nights [G, E]. Most records are single successes. VO1NA's 95-minute-per-bit decodes at about −25 to −38 dB-Hz parallel the modem's 96-minute bit, whose nominal row-1 C/N0 of −36 dB-Hz (68 dB below the 32 dB-Hz planning value) gives Eb/N0 ≈ +1.6 dB per bit [E]; VO1NA's decodes relied on K = 21, rate-1/16 coding with list decoding. Budgets give −36 dB-Hz for 100 W into a 61 m T at several thousand km (8.27 kHz); at 2.97 kHz, 5,000 km opens only at night in quiet noise; 32 dB-Hz at 0.6–1.4 Mm needs about 1–5 W ERP [E]. EbNaut decodes used GPS-locked frequency and timing at both ends, sferic blanking (6–23 dB), strong coding with list decoding and UT schedules; the earlier kite carriers were detected from free-running exciters only in bins of 0.7 mHz or wider, or after de-chirping. Reported losses: oscillator drift, summer sferics, mains and railway harmonics, detuning and wind, unrecorded transmission times and, as the source interprets it, nocturnal Doppler spreading [G]. Further hazards are night-to-night phase shifts common to all receivers (a transmitter origin is inferred) and false list decodes (a computed risk) [G, E]. The GPS-locked links that decoded met frequency offsets and wander far below even the Sub9 best preset's 1 mHz, and no preset models the antenna's high-Q, weather-driven phase response [E].

**Provenance and conventions.** This note merges two independently researched parts: Part A on antenna engineering and link budgets, and Part B on the communications achieved or proposed below 9 kHz, with their techniques, thresholds and failure causes. They are combined here by topic. Short-antenna theory and constants, bandwidth limits, corona, detuning, balloon variants, real antennas of related scale, earth-electrode practice, near-field corrections and the worked link budgets come from Part A. Amateur and experimental records, strategic ELF systems, scientific sources, techniques and the implications for hours-long integration come from Part B. Power conventions, earth electrodes, compact transmitters, C/N0 from amateur links, natural noise, the parameter table, models, simulation needs and contradictions combine both. Wherever one part's material appears in the other's sub-section, it is attributed in the text. Where both parts state the same thing it appears once, with both parts' details and sources. Where they disagree, both versions are kept and the disagreement is stated; these cases are collected under "Contradictions". "Part A" and "Part B" replace each researcher's first person, and [E] marks an estimate by the part concerned. Evidence grades: [S] standard, [P] research paper or preprint (peer-review status checked separately) ([P-conf] conference paper), [T] report or thesis, [B] book, [G] grey literature, [H] in-house chapter, [E] own estimate; combined grades such as [T/G] are the part's own. Access status: read, abstract, metadata, secondary or not obtained. Keys such as [P1] or [G10] refer to the merged reference list. Part B's passages also name sources by short titles (for example "msg00450", "abelian /vlf/ss100801/", "FS&B", "MIT LL" or "the list"), which the reference list identifies.

## Physical mechanisms

### Reference interpretation of “200 ft antenna”

The 200 ft (60.96 m) dimension is a working reference branch carried forward from the adjacent Astra-6 synthesis, not a uniquely specified real installation. For the reproducible calculation branch in this chapter, use a 61 m vertical with a 122 m horizontal top wire (the modeled T), electrostatic estimates C ≈ 0.90 nF and effective height h_e ≈ 50 m, plus an explicitly stated loading coil and loss network. The top wire, support, downlead, feed arrangement, ground system, soil, nearby trees/buildings and weather are assumptions until measured for a site; the effective height is not simply mast height. This branch is useful for sensitivity calculations, not an antenna certification. The model's capacitance/effective-height calculation was independently recomputed within about 1% of the original estimate [E; own-estimate provenance at end of chapter]. The separate 91 m umbrella measurements are evidence about design-versus-realized efficiency at 20–60 kHz, not a direct sub-9 kHz efficiency measurement. See [81](81-scenarios-sub9.md) for the fixed reference inputs and alternatives.

### Radiation from an electrically short vertical

- For an electrically short monopole on perfectly conducting ground, R_r = 160π²(h_e/λ)². The effective height h_e is the current-moment height. It equals the physical height for uniform current (heavy top loading) and h/2 for a linearly tapered current (bare wire). This is Watt's "standard" antenna, quoted by Dazey & Koons [T2, read, eqs. (9)–(11)]. Jasik's alternative for a bare cylinder, 415(h/λ)², corresponds to h_e = 0.5125 h; Dazey & Koons attribute the 2.5 % difference to the slight self-top-loading of a finite-diameter wire [T2, read]. Ray's constant-current 30.5 m element gives R_r = 0.01195 Ω at 27 kHz, consistent with the formula with h_e = h [T1, read, eqs. (2)–(4)].
- **Effective height as the charge centroid.** For an electrically short structure fed against perfect ground, I·h_e = jω∫z dq. So h_e is the charge-weighted mean height of all conductors, including top-hat charge. Part A used this with a thin-wire electrostatic method of moments: image ground, uniform-charge segments, potential matched on the wire surface [E; script description in "How the literature models"].
- **Capacitance and reactance.** The antenna is a capacitor C, with X_C = 1/(2πfC) and loading inductance L = 1/(ω²C). Base voltage per ampere is V/I ≈ X_C, and the voltage is nearly uniform along a short antenna. Dazey & Koons give the thin-cylinder transmission-line approximation Z0 = 60[ln(h/a) − 1] and X = Z0·cot(kh) [T2, read, eqs. (1)–(2)]. They state that a balloon cable approximates 5 pF/m [T2, read, eq. (6)].
- **Field relations** (NRL measurement practice) [T4, read, eqs. (2-2)–(2-3)]: R_r = d²E²/(90 I²) and h_e = 796·E·d/(I·f), with d in km, E in mV/m, I in A and f in kHz. Also 1 kW e.m.r.p. gives 186.3 mV/m at 1 mile, i.e. 300 mV/m at 1 km [T1, read, eq. (7)].

### Losses, efficiency and the power conventions

- **Efficiency** is η = R_r/(R_r + R_loss). R_loss is the sum of the loading-coil loss X/Q_coil, ground loss and dielectric/insulator/vegetation loss.
- **Dielectric loss.** Part A models it as an equivalent series resistance X_C·tanδ, which scales as 1/f [E]. The 1/f scaling is why vegetation and wet-insulator loss matters more at sub-9 kHz than at LF.
- **Ground loss** has two parts [T1, read, §V]:
  - H-field loss: radial base currents in the soil.
  - E-field loss: displacement current from the top load entering lossy soil between ½ and 1 tower height out.
  - Ray computed no more than 3 Ω per element for a minimal screen at 27 kHz, against a predicted 3.33 Ω, and measured 5.5 Ω on his model [T1, read].
- **Loading-coil loss.** Coil losses were well predicted below self-resonance. They rose sharply for coils of more than about 1,500 Ω reactance as self-resonance was approached, with the loss multiplied by 1/(1−y²)², where y is the ratio of working to self-resonant frequency (Terman). A copper-tube coil measured 1,480 Ω with 3.1 Ω resistance, Q ≈ 480. Calculated Q for Litz coils exceeded 1,000, beyond what the instruments could verify [T1, read, §VIII–IX].
  - At sub-9 kHz the required coil reactances are 10–160 kΩ (Table A), so distributed capacitance and self-resonance become design issues.
  - Typical values are 0.4 H with X ≈ 21 kΩ at 8.27 kHz, and 3.2 H at 2.97 kHz. A self-capacitance of 20–30 pF gives self-resonance at about 16–56 kHz, so the 1/(1−y²)² penalty is only 4–7 % [E].
- **E.m.r.p., e.r.p. and e.i.r.p.** The radiated power of a short monopole on perfect ground, I²R_r, is by definition its e.m.r.p. Its directivity of 3 (4.77 dBi) gives e.i.r.p. = 3 × e.m.r.p. and e.r.p. = 1.83 × e.m.r.p. (+2.62 dB) [E].
  - LF/MF amateurs (the 2200 m and 630 m bands) multiply I²R_r by about 1.82 to quote "ERP" and by 3 for EIRP [G7, read]. All amateur radiators are electrically tiny, so ERP is set by antenna current and effective height or area. The community's own worked calculations use dipole-referenced ERP: **ERP = 1.8·I²·Rrad**. This was stated for a 100 m² loop at 9 kHz with Rrad ≈ 250 pΩ (loop Rrad = 320π⁴A²/λ⁴, after M0BMU) — G3XBM "XBM VLF blog" [G15, read]. Part B's estimate [E]: the factor 1.8 is the gain of a short antenna over perfect ground (3) divided by the dipole gain (1.64), so the quoted "ERP" is about +2.6 dB above EMRP (= I²·Rrad) and about −2.2 dB below EIRP.
  - DK7FC's reported 11 mW e.r.p. at 6.47 kHz (kite, 0.92 A) agrees with Part A's modelled e.m.r.p. of 5.6 mW × 1.83 = 10.2 mW [E on G3]. Where amateur "ERP" is computed as 1.8·I²·R_r it is therefore dipole-referenced, 2.6 dB above e.m.r.p. ERPs inferred from received fields (an LWPC fit, or Nicholson's single-mode formula, which is algebraically in e.m.r.p.) may already be e.m.r.p., so the conversion is uncertain by up to 2.6 dB per record [E].
  - Individual records do not always state how their ERP was derived; W4DEX's 150 µW came from LWPC fitting of the measured field (w4dex.com [G22]).
  - **The documentation set's C/N0 formula uses e.m.r.p.** Part A converts every quoted amateur e.r.p. by −2.6 dB before using it; Part B quotes ERP as reported.
- **A worked amateur example**, a comment by ON7YD quoted on the same G3XBM page, is a 10 m vertical with 10 m top load at 9 kHz: Rrad ≈ 75 µΩ, C ≈ 110 pF, reactance 161 kΩ, a 2.84 H loading coil and about 1500 Ω total loss (several hundred ohms of it ground loss). This gives 0.25 A from 100 W, ≈ 8 µW ERP and ≈ 40 kV on the antenna [G15, read; calculated, not measured].

### Bandwidth and the Chu–Wheeler limits

- **Minimum radiation Q.** For a sphere of radius a enclosing the antenna, Q_A,min = 1/(ka) + 1/(ka)³ (McLean). For a monopole on a ground plane, the enclosing sphere radius is the full antenna length (image included) [P10 Kemp et al., read, Methods; P22 abstract].
  - Kemp et al. note that Thiele's far-field formulation gives a minimum about 10 times higher. For a passive antenna, the bandwidth–efficiency product Bη = 1/(2Q_A,min) [P10, read].
  - Chu (1948) and Wheeler (1947) are the original sources; only their bibliographic records were verified [P23, P24; not read].
- **Numbers for 61 m [E, Table A].**
  - At 8.27 kHz, ka = 0.0106 and Q_Chu = 8.5×10⁵. Even a lossless, sphere-filling 61 m antenna would have a bandwidth of only about 10 mHz, less than the modem's 62.5 mHz.
  - Real 61 m structures have Q_rad = X_C/R_r of 6–53×10⁶ at 8.27 kHz, which is 7–63 times Q_Chu. The umbrella comes nearest the bound and the bare wire is furthest from it.
  - Since η = Q_L/Q_rad, the passive bandwidth–efficiency product is f/Q_rad = 0.16–1.4 mHz at 8.27 kHz. This uses the −3 dB bandwidth f/Q_L; with [P10]'s VSWR-2 definition (Bη = 1/(2Q)) the products are about half as large, 0.08–0.7 mHz [E].
  - The modem's 0.0625 Hz bandwidth would therefore constrain efficiency only above about 0.3–2 % (0.13–1.1 % with the VSWR-2 definition). Practical efficiencies are 10⁻⁴–10⁻⁶, so bandwidth never binds.
- **Practical loaded Q** (Q_L = X_C/R_total) is 13–490 at 8.27 kHz and 20–490 at 2.97 kHz. That gives −3 dB bandwidths of 6–620 Hz [E, Table B].
  - These values match operational practice: high-power Navy and Air Force VLF/LF stations had static bandwidths of 33–70 Hz at 18–27 kHz [T6, read, Table I].
  - The antenna's group delay is Q_L/(πf), about 17 ms at Q_L = 450 and 8.27 kHz. It equals the envelope time constant, which is 0.5–53 ms over the Q_L range of Table B (6–50 ms for Q_L ≥ 150). Both are negligible against 60 s chips [E].

### Voltage, corona and insulation

- Radiated power at a voltage limit V is P = (VωC)²R_r. It scales as f⁴ for fixed geometry and is independent of loss and Q [E]. Dazey & Koons state directly that "the radiated power is usually limited by the voltage rating of the antenna system" [T2, read].
- **Onset gradients** (rms, at 28 kHz, from vertical-cell tests) [T3 Watt & Hansen 1992, read, Table 5-1a]:

  | Conductor (diameter) | Dry (kV/cm) | Wet (kV/cm) |
  | --- | ---: | ---: |
  | Smooth, 0.106 cm | 49.4 | 48.5 |
  | Smooth, 0.167 cm | 44.9 | 45.1 |
  | Smooth, 0.33 cm | 34.4 | 33.3 |
  | Smooth, 0.952 cm | 29.8 | 19.3 |
  | Smooth, 3.8 cm | 21.4 | 11.5 |
  | Stranded, 0.955 cm | 27.3 | 19.7 |

  - Rain lowers onset 28–46 % on conductors of 1 cm or more, and barely affects thin wire.
  - The frequency reduction from 60 Hz values is "a few percent" up to several kHz; the provisional law is K = 1 − 0.015·F^0.6·d^0.3 (F in kHz, d in cm; eq. 5-7, the OCR of the earlier eq. 5-3 being ambiguous between 0.015 and 0.018).
  - Corona power is P ≈ 2πfCV(V − V0) in rms quantities [T3, read, §5–6].
  - Corona-onset field also falls with altitude: 7.2 kV/cm at 18.3 km and 4.6 kV/cm at 30.5 km, relevant to balloon-lofted wires [P20, read].
- **Part A's model onset voltages** [E]: maximum surface gradient per kV from the method of moments, combined with the dry onset gradients above.
  - 1 mm wire: 2.87 kV/cm per kV, onset about 17 kV rms.
  - 2 mm wire: 1.6 kV/cm per kV, about 27 kV.
  - 10 mm tube: 0.43 kV/cm per kV, about 69 kV dry and 45 kV wet.
  - T with 2 mm wire: about 29 kV.
  - Umbrella with 4 mm wires: about 58 kV.
  - These are optimistic: wire ends, insulator hardware and the kite or balloon termination enhance the field and need corona balls or rings. A 2021 simulation study optimised a corona ring for the end of a tethered-balloon high-power VLF antenna, where dielectric loss in the strong field can raise the temperature abnormally and, in serious cases, fracture the antenna (Wang et al. 2021 [P50, abstract]).
- **Recorded practice** [T, G]:
  - Dazey & Koons ran the winch to 100 kV rms, with corona, sparking and local fires above that. Moisture caused arcing along the insulating struts, sometimes forcing operation "considerably lower than 100 kV" [T2, read].
  - Ray designed his 100 ft top hat for a surface gradient of about 21 kV/inch (8.3 kV/cm), half the 52 kV/inch (20.5 kV/cm) he took as dry-air breakdown, with 74.7 kV estimated on the top hat at 50 kW. The two-element model measured 90 kV at 50 kW, and the hexagonal reconfiguration reduced the voltage to 45 kV for 50 kW [T1, read, §IV and summary].
  - An ELF/VLF tether antenna lofted by an aerostat to 3,810 m (12,500 ft) showed a corona onset of 180 kV, and at ELF was operated up to 260 kV, 40 % above onset, while resisting the degrading effects of corona [P46, abstract]. This was a very long tether (its length and diameter are not given in the abstract), not a 61 m-class wire.
  - Amateurs report about 20 kV on a 200 m kite wire [G3] and "5 kV on antenna" in an 80 Hz test [G1].
- NEC4 charge-density analysis is the Navy's method for checking top-load gradients against design limits [T9, abstract].

### Detuning and its mechanisms

- At a fixed drive frequency, a capacitance change shifts resonance by Δf/f = −ΔC/(2C). Current falls by 1 dB when |2Q_LΔ| = 0.51, where Δ = Δf/f. Phase shifts by arctan(2Q_LΔ) [E, Table D].
  - At Q_L = 400, 1 dB corresponds to ΔC/C = 1.3×10⁻³ and 10° to 4.4×10⁻⁴. At 8.27 kHz that is 5.3 Hz of resonance shift for 1 dB.
- **Wind on fixed antennas.** In Part A's model, 1 m of sideways displacement (a bow at the centre) of a 122 m T top wire changes C by only about 1×10⁻⁴ (1.5×10⁻⁴ on re-computation), which gives 1–4° of phase change at Q_L = 150–480 [E]. Extra sag is not so benign. Re-computed with the same method (converged at 180–600 segments), 1 m of extra centre sag changes C by about +4×10⁻⁴ if the down-lead keeps its length and bows (4–11°), and by about −4×10⁻³ if the down-lead is shortened with the sag (33–65° and up to 7 dB) [E, re-check].
- **Kites and balloons.** Elevation angle changes both C and h_e [E]:

  | Kite angle (200 m wire) | ΔC | Δh_e | e.m.r.p. change |
  | --- | ---: | ---: | ---: |
  | 75° → 60° | +0.5 % | −10 % | −1 dB |
  | 75° → 45° | +1.5 % | −27 % | −2.7 dB |
  | 75° → 30° | +3.5 % | −49 % | −5.8 dB |

  - Reported: DK7FC found he could resonate the 200 m kite wire between about 6.4 and 6.6 kHz, tentatively attributing this to the elastic kite line stretching by up to 20 m (10 %) in strong wind. The resonance was at 5.7 kHz at a very low kite angle [G3, read].
  - G3 reports only this tuning range, with no time history of the resonance during a transmission. If the resonance swung across it (an assumed ±1.5 %), then with Q_L ≈ 37–44 the swing is 2Q_LΔ ≈ 1.1–1.3, i.e. about −3.5 to −4.4 dB and 48–53° [E on G3]. The Q_L is inferred from 500 W, 0.92 A and about 20 kV, or from Part A's modelled 940 pF.
  - Dazey & Koons measured h_e = 568 m in calm conditions and 465 m in 20–25 kt wind for a 1,220 m balloon cable (cable blown down beyond 45°). Radiated power fell from 1,400 W to 940 W (−1.7 dB) at the same current [T2, read].
- **Icing.** Ice's Debye relaxation has static permittivity 91.5 at 0 °C and τ = 5.3×10⁻¹⁶·exp(13.2 kcal mol⁻¹/RT) s [P21, abstract]. That puts the relaxation frequency at about 8 kHz at 0 °C, 3.3 kHz at −10 °C and 1.2 kHz at −20 °C [E]. Sub-9 kHz antennas therefore see ice near its loss peak, with tanδ of order 0.3–2.5 between 0 and −20 °C at 2.97–8.27 kHz [E; Debye form, ε∞ ≈ 3.2 assumed].
  - Treating 5 mm of radial ice as conductor raises the T's capacitance by 19 % [E]. That is an 8–9 % resonance shift (8.3 % exactly, 9.5 % linearised): the antenna is tens of dB off tune unless retuned, and it is also lossier.
  - Operational record (US Navy Cutler VLF station, a far larger antenna): ice loading lowers the counterweighted top-load panels, which raises the antenna capacitance until the retuning range is exhausted and transmission stops. One of two arrays is therefore de-iced with 3–6 MW of 60 Hz heating while the other transmits. There were 93 de-icing incidents in the seven winters 1986–1994, each lasting from a few minutes to just over an hour, and about 2 radial inches of ice was seen on guy wires in 1992–93 [T16, read].
- **Rain, humidity and temperature.**
  - Rain wets insulators, adding leakage and arcing [T2], and lowers corona onset on thick conductors by 28–46 % [T3].
  - Temperature changes wire sag and coil dimensions. A typical metal expansion coefficient (about 17 ppm/K) over a 30 K swing gives ΔC/C or ΔL/L of order 10⁻⁴–10⁻³ [E, assumption]. That needs slow retuning.
  - Measured at US Navy LF transmitting sites (60–80 kHz) by near-real-time monitoring of antenna voltage, current and phase: one example day's record showed a mistuning lasting about an hour that cut output power by more than 1 dB, and a variation of about 10 % in gross (total input) resistance over the day. The report gives changes in ground or air conductivity, moisture or other environmental conditions as possible causes, and states that weather-induced problems have been diagnosed with the system by correlating changes in that resistance with local humidity and rainfall [T18, read]. These are LF antennas, electrically far less short than a 61 m antenna below 9 kHz.
- **Retuning methods.**
  - Coarse taps plus a motor-driven variometer (1.8–0.8 mH), adjusted to maximise current [T2, read].
  - Antenna modulation (ETAM): inductance switched at current zeros with a phase-continuous drive. It carried 800 baud through a 160 Hz static bandwidth [T6, read].
  - Synchronous tuning (BEST): less than 0.5 dB loss with an instantaneous bandwidth far below the modulation bandwidth, and up to 7 dB more radiated power [T7, abstract].
  - Static dual-passband feed networks [T8, abstract].
  - Servo on the voltage–current phase: NIST's WWVB (60 kHz; two top-loaded monopoles, each on four 122 m towers) is retuned automatically during icy and/or windy conditions by a computer that detects a phase difference between voltage and current at the transmitter and drives the variometer motor [G23, read]. The aerostat-lofted antenna of [P46] was likewise retuned continuously through the changes in height and capacitance caused by the aerostat's flight [P46, abstract].
  - For a 0.06 Hz modem the requirement is not bandwidth but slow phase/amplitude stabilisation. A servo that holds the current–voltage phase at zero, as at WWVB, or monitoring of the antenna current phase, is the natural control [E]. Amateur kite stations already do this: the ebsynth transmit program closes a phase-locked loop on the antenna-current signal so that the radiated phase holds when wind changes the resonance (DL3JMM 2023 [G24, read]).

### Earth-electrode (ground) dipoles, buried wires, loops and earth-mode

- **Mechanism.** A horizontal wire grounded at both ends closes its circuit through the earth. The return current lies at a complex mean depth of δ/(1+j), where δ = √(2/(ωμ0σ)).
  - Wheeler (1961) shows that the radiation above ground is that of a vertical loop of effective height δ/√8 centred at the wire depth (with image). He adds that "it is remarkable that this is valid for any depth of the wire below the surface" [P1, read, eq. (6)]. With his reference (a loop just above a perfect ground plane, whose image doubles the far field) this corresponds to a monopole-equivalent height k0·l·δ/√8, half the value derived in the next bullet [E].
  - His 1 MHz example (100 m wire, 1 m depth, 0.01 S/m, δ = 5 m) gives an effective height of 1.8 m and an efficiency of 0.0014 (−28.5 dB), excluding electrode losses [P1, read].
- **Equivalent monopole height [E].** The equivalent vertical moment is k0·I·l·δ/√2, so the monopole-equivalent effective height in the end-fire direction is h_eq = l·√(ωε0/σ). Equivalently, an HED of moment Il on the surface radiates like a VED of moment Il·|Δ|·cos φ. The pattern is cos φ, maximum along the wire.
  - This is twice Wheeler's height, and for Wheeler's own 1 MHz example it gives four times his efficiency of 0.0014 (6 dB). A receiving-side derivation made on re-check supports this chapter's value: a grounded wire of length l on the surface picks up V_oc = E_x·l = Δ·E_z·l, so by reciprocity h_eq = |Δ|·l; with Carson's ground-return resistance this reproduces four times Wheeler's efficiency. The value also equals a loop of height δ/√2 just above the ground, δ/√2 being the effective depth W that FS&B use for the moment M = I·L·W of ELF lines [P32] [E]. The 6 dB difference is not resolved here and is carried as model uncertainty on every ground-dipole e.m.r.p. value below.
  - Values (Table E): at 8.27 kHz, h_eq/l = 0.0068, 0.021 and 0.068 for σ = 10⁻², 10⁻³ and 10⁻⁴ S/m. A 61 m dipole over 10⁻³ S/m is therefore equivalent to a 1.3 m monopole, and a 1,130 m dipole to 24 m (0.65 m and 12 m with Wheeler's height).
  - At 2.97 kHz the ratios are 0.0041, 0.013 and 0.041. At 76 Hz they are 6.5×10⁻⁴, 2.0×10⁻³ and 6.5×10⁻³.
- **Effective conductivity** is a deep, frequency-dependent, possibly anisotropic quantity. At the Wisconsin Test Facility the EW and NS antennas saw different effective conductivities and skewed patterns. The H/I method needs at least seven effective skin depths, and half-antenna predictions held to a factor of 1.22 [P28, abstract]. Burrows treated ground anisotropy for ELF transmitting antennas [P31, metadata only].
- **Loss.** The ground dipole is low-impedance and low-Q. Electrode resistance dominates, and there is no high voltage.
  - Reported totals are 82 Ω (DK7FC/P 1,130 m, 487 W at 2.44 A; 51–114 Ω in the same loop's other records at 176 Hz–8.27 kHz), about 133 Ω (EA4GHB 350 m, 60 V at 0.45 A, dry sand) and 200 Ω (F5VLB 75 m, 32 V at 0.16 A) [G1, read; E].
  - Efficiencies are therefore very low: about −77 dB (−83 dB with Wheeler's height) for 61 m at 8.27 kHz over 10⁻³ S/m with 100 Ω [E]. Current is set by transmitter voltage, not by corona.
- **Use in amateur practice.** Ground loops and ground (earth) dipoles of 75–1,320 m, driven at 0.16–4.5 A, were widely used from about 2019: DK7FC/P, DL0HOT, DL3JMM, EA4GHB, F5VLB, SQ2BXI/SQ5BPF and RN3AUS (milestone list [G1], below; Part B's characterisation).
- **Long lines.** A 10.6 km power line used as an antenna at 1,280 Hz had an estimated efficiency of −50 to −56 dB [P4, abstract; T2, read]; its current, dissipation and radiated power are under "Scientific controlled sources in or near 1–9 kHz" (Kafjord).
  - A 21 km horizontal antenna on the Antarctic ice sheet (Siple), modelled with a layered ice dielectric, was calculated to exceed 2 % efficiency over more than a 2:1 frequency range [P5, abstract].
  - Very poorly conducting substrate (ice, granite shield) is the regime where horizontal and earth antennas excel.
- **Large insulated loops** are no better than ground dipoles of the same footprint, and worse over poor ground. A vertical loop of area A on the ground has h_eq = k0A and R_r = 640π⁴(A/λ²)². For a 61 m × 61 m loop at 8.27 kHz, h_eq = 0.65 m and R_r = 5×10⁻⁷ Ω, against 1.3 m (0.65 m with Wheeler's height) for a 61 m ground dipole over 10⁻³ S/m [E]. A horizontal loop on the ground radiates horizontal polarisation, which the ground shorts.
- Wheeler adds that N wires in parallel, spaced more than a skin depth apart, multiply efficiency by about N [P1, read]. AD0653851 reports that n closely spaced horizontal conductors near the earth give n times the efficiency, with "super gain" [T10, abstract].
- **Earth-mode** (current injected between electrodes about 20 m apart) is a quasi-static near-field link with roughly inverse-cube decay. Buried metal utilities can assist it — G3XBM, RadCom Oct 2011 [G14, read].

### Strategic ELF grounded lines and ionospheric heaters

- **ELF strategic systems** use horizontal grounded wires over very low-conductivity rock. The return current flows deep, making a horizontal magnetic dipole with moment M = I·L·W, where W ≈ δ/√2 for uniform ground or ≈ h₁ for a resistive layer over a conductor. WTF/MTF: M ≈ 2×300 A × 22.5 km × 2.6 km = 3.5×10⁴ A·km² (σ ≈ 2.4×10⁻⁴ S/m). ZEVS: ≈ 1.1×10⁵ A·km² (σ₁ ≈ 10⁻⁵ S/m to about 10 km depth) — Fraser-Smith & Bannister 1998 [P32, read].
- **HAARP and other heaters** modulate the conductivity of the auroral electrojet, creating an "antenna in the sky". Conversion efficiency is about 0.0004–0.0032 % at 2125 Hz — Moore et al. 2007 [P35, abstract].

### Compact mechanical, piezoelectric and magnetoelectric transmitters

- **Rotating magnets.** A magnet of remanence B_r and volume V has moment m = B_rV/μ0, about 1.1×10⁶ A·m² per m³ of NdFeB [E]. Its near field falls as μ0m/(2πr³) on axis [P15, read, eq. (1)]. Range therefore scales only as m^(1/3), and radiation (∝ ω⁴m²) is negligible below 1 kHz.
- **Burch et al.** rotated a 3 cm³ magnet and found the fields "dominated by the static component, rather than by the radiation component". Magnetic signalling was "possible and practical" below about 500 Hz at more than 100 m [P12, abstract].
- **Bickford et al.** predict that metre-scale mechanical antennas can theoretically exceed conventional short dipoles and coils "by more than eight orders of magnitude" at 1 kHz and below. This is a theoretical result [P13, abstract].
- **Latypov (2026)** extends small-antenna theory to naturally resonant emitters, derives a fundamental efficiency limit for a unit-volume emitter at a given frequency and bandwidth, and applies the resulting figure of merit to public data from Navy ELF and VLF facilities and two mechanical transmitters. The abstract concludes that the mechanical antennas already "operate near the theoretical FOM limit, questioning claims of possible further orders-of-magnitude gains" [P48, abstract].
- **Other demonstrated ranges:**
  - 22 Hz, 0.6 µT at 1 m, 8 b/s, up to 70 cm through obstacles [P14, abstract].
  - 100 Hz, about 0.25 b/s, through concrete at up to about 5 m [P16, read].
  - A 4.77 g rotating magnet gave 23 dB more field than a coil of equal volume and 0.35 W dissipation [P15, read].
  - Resonant multirotor magneto-mechanical transmitters at tens of Hz to 1 kHz, with amplitude modulation up to 10 b/s [P17, abstract].
  - A magnetic pendulum array at 1.03 kHz with much higher Q than coils and "order of magnitude higher transmission efficiency" [P11, read].
  - Magnetoelectric VLF antennas reached a maximum of 120 m, with 80 V drive and 400 mW consumption [P18, abstract].
  - Part B: An ultracompact ELF magneto-mechanical antenna reached a longest underwater distance of about 210 m, and about 210 m across the water–air interface — Yang et al., IEEE TAP 71(3), 2082–2097, 2023 [P43, abstract]; the bibliographic data are confirmed by Crossref and by the reference list of Miś et al. 2024 [P38]. Related work reports amplitude modulation up to about 10 bit/s.
- **Kemp et al. piezoelectric LiNbO₃ rod** (35.5 kHz, about 10 cm): Q_m = 3–6×10⁵ and a calculated radiation efficiency of 10⁻⁸–2×10⁻⁷. That is more than 300 times better than earlier compact transmitters, and the bandwidth is 83 times the passive Bode–Fano limit [P10, read]. SLAC's release describes a 100 ft test link [G8, abstract].
  - Kemp et al. also note that matching a conventional 10 cm monopole with a 10 mA·m rms current moment at 35.5 kHz would need a 10.5 H, 125 kV inductor [P10, read].
- **Search for longer ranges.** Part A searched specifically for 2023–2024 kilometre-range field tests of mechanical antennas and found none. The closest was a 2023 rotating-magnet array study at 75–125 Hz with no kilometre-range figure in the abstract [search result, not read].
- **Implication [E].** For sub-9 kHz long-distance links these devices do not compete with a 61 m antenna: their e.m.r.p. is many orders lower. Their niche is near-field magneto-inductive signalling at metres to hundreds of metres. Part B likewise describes rotating-magnet and magneto-mechanical antennas as near-field, short-range devices that do not bear on far-field Earth–ionosphere links (it lists them among 2017–2025 compact ELF/ULF transmitters).

### Balloon and stratospheric variants (2015–2026)

- A light stratospheric balloon carried a 14.2 kHz transmitter with a 400 m vertical antenna (payload of at most 4 kg) to 29,164 m on 12 September 2020. The wire was folded to a physical length of 210 m, and the estimated radiated power was about 1 µW (−30 dBm). Of 61 listening reports, 14 were successful receptions and 32 null (noise or interference); signal strengths were recorded at 93–980 km and, at low quality, 1,750 km [P19, read].
- The companion study reports the charge-loss time constant against altitude, the fair-weather field (about 1 V/cm at the ground, 0.2 V/cm above 2 km) and the falling corona-onset field at altitude [P20, read].
- A tethered-balloon high-power VLF communication system is the subject of the 2021 corona-ring study noted under "Voltage, corona and insulation" (simulation only; the abstract gives no operating parameters) [P50, abstract].

### Why the links close: propagation, noise and phase coherence as practitioners explain them

- At VLF the signal is trapped between the ground and the D/E region. Power spreads cylindrically, so received power density falls roughly as 1/range, plus a waveguide attenuation of about 0.5–8 dB per 1000 km. The attenuation depends on frequency, day/night, land/sea and direction relative to the geomagnetic field. Paths are described as "generally stable and repeatable, often with little or only slow fading", sometimes with steady phase for hours — EbNaut index page [G10, read]. The abelian 9 kHz page uses a single-mode cavity model after Wait (1958); its form, parameters, self-declared defects and checks against reception are under "How the literature models or simulates this".
- **Noise.** Sferics dominate the background. Blanking the wideband (about 3–4 kHz) signal before narrowband processing removes the non-Gaussian part. In EbNaut's standard chain the threshold is about 10× the mean rectified noise amplitude; 25–35 % of samples are blanked and the S/N gain is about 20 dB at 8 kHz. The noise left is "approximately Gaussian", so the inner-product branch metrics are log-likelihoods — EbNaut technical notes [G10, read].
- **Phase coherence.** VLF paths, and audio-frequency transmitters locked to GPS, allow coherent BPSK and integration over hours. "Usually at VLF the decode is successful with a uniform reference phase." Phase search across 2 or 4 message segments handles disturbed propagation or oscillator problems — EbNaut notes [G10, read].

## Measured and reported values

### Capacitance and effective height: model against measurement [E, T, G]

| Structure | Model C | Model h_e | Measured or reported | Agreement |
| --- | ---: | ---: | --- | --- |
| 1,220 m balloon cable, 1.8 cm diameter | 6,300 pF | 597 m | 5,700 pF from Z0; 6,000 pF from 5 pF/m; h_e = 568 m calm, 465 m in 20–25 kt [T2, read] | C +5 to +11 %; h_e +5 % |
| 200 m kite wire at 75°, 1 mm | 940 pF | 95 m | about 1.1 nF inferred from about 20 kV at 0.9 A, 6.47 kHz; e.r.p. 11 mW [G3, read] | C −15 %; e.r.p. consistent (10.2 mW) |
| T, 4 × 33 m wires at 13 m (1.5 m spacing assumed) | 544 pF | 12.1 m | 660 pF (DK1IS) [G1, read] | −18 % (spacing and stray capacitance unknown) |
| 61 m bare wire, 2 mm / 1 mm / 10 mm | 340 / 318 / 407 pF | 29.8 m | — | — |
| 61 m lattice tower, a_eq 0.3 m | 814 pF | 28.5 m | — | — |
| 61 m T with 122 m top wire | 902 pF | 49.8 m (0.82 h) | — | — |
| 61 m wire with six 30 m flat radials | 999 pF | 51.2 m (0.84 h) | — | — |
| 61 m umbrella tower, 8 × 45 m wires at 35° | 1,850 pF | 38.4 m (0.63 h) | — | — |

The same umbrella geometry that maximises C costs h_e because its wires slope downward. W5EST's rule for LF/MF top hats, relayed in a reader's comment on VE7SL's page — subtract half the downward component of a sloping hat from the vertical section — expresses the same effect [G7, read].

### Radiation resistance and reactance at the requested frequencies [E, Table A]

R_r values: bare 2 mm wire (340 pF, h_e = 29.8 m) / T (902 pF, h_e = 49.8 m) / umbrella (1,850 pF, h_e = 38.4 m). The X_C and L_load columns are for the T. λ/2π marks the boundary of the near field.

| f | λ/2π | R_r, bare | R_r, T | R_r, umbrella | X_C (T) | L_load (T) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8,970 Hz | 5.3 km | 1.26 mΩ | 3.50 mΩ | 2.08 mΩ | 19.7 kΩ | 0.35 H |
| 8,270 Hz | 5.8 km | 1.07 mΩ | 2.98 mΩ | 1.77 mΩ | 21.3 kΩ | 0.41 H |
| 5,170 Hz | 9.2 km | 418 µΩ | 1.16 mΩ | 692 µΩ | 34.1 kΩ | 1.05 H |
| 2,970 Hz | 16.1 km | 138 µΩ | 384 µΩ | 228 µΩ | 59.4 kΩ | 3.18 H |
| 1,000 Hz | 47.7 km | 15.6 µΩ | 43.6 µΩ | 25.9 µΩ | 176 kΩ | 28.1 H |
| 300 Hz | 159 km | 1.41 µΩ | 3.92 µΩ | 2.33 µΩ | 588 kΩ | 312 H |
| 76 Hz | 628 km | 90 nΩ | 250 nΩ | 150 nΩ | 2.32 MΩ | 4,860 H |

- The bare 2 mm wire has X_C = 56.6 kΩ at 8.27 kHz and 157 kΩ at 2.97 kHz. The umbrella has 10.4 kΩ and 29 kΩ.
- Volts per ampere equal X_C: 1 A into the T needs about 21 kV at 8.27 kHz and 59 kV at 2.97 kHz. At 1 kHz the same current needs 176 kV, and at 76 Hz 2.3 MV, so below about 3 kHz a 61 m vertical is voltage-starved [E].
- Part A's summary gives R_r of 1.1–3.2 mΩ at 8.27 kHz (bare wire to six-radial wire; 3.5 mΩ is the T at 8.97 kHz), 0.14–0.41 mΩ at 2.97 kHz, 16–46 µΩ at 1 kHz and 0.1–0.3 µΩ at 76 Hz.

### Efficiency and radiated power for 10 W, 100 W and 1 kW [E, Table B]

**Loss cases (assumptions, each [E]):**

| Case | Q_coil | R_ground | tanδ | Anchored to |
| --- | ---: | ---: | ---: | --- |
| S1, engineered | 1,000 | 5 Ω | 0.001 | Ray's coil Q ≈ 480 measured and > 1,000 calculated; 3–5.5 Ω ground at 27 kHz [T1] |
| S2, good fixed amateur | 400 | 30 Ω | 0.003 | — |
| S3, typical or temporary | 250 | 150 Ω | 0.01 | DK7FC kite total ≈ 590 Ω at 6.47 kHz [G3] |
| S4, adverse (wet, trees, poor ground) | 150 | 400 Ω | 0.03 | — |

**Voltage limits:** 27 kV (bare 2 mm wire), 29 kV (T, 2 mm wires), 58 kV (umbrella). "V" marks a voltage-limited case; e.m.r.p. is then independent of transmitter power.

| Antenna, f | Loss case | η | Q_L; B | E.m.r.p. at 10 W / 100 W / 1 kW |
| --- | --- | ---: | ---: | --- |
| Bare wire, 8.27 kHz | S1 | −50.4 dB | 479; 17 Hz | 91 µW / 244 µW V / 244 µW V |
| Bare wire, 8.27 kHz | S3 | −59.4 dB | 60; 138 Hz | 11 µW / 114 µW / 244 µW V |
| Bare wire, 8.27 kHz | S4 | −63.6 dB | 23; 362 Hz | 4.3 µW / 43 µW / 244 µW V |
| T, 8.27 kHz | S1 | −42.0 dB | 448; 18 Hz | 0.63 mW / 5.5 mW V / 5.5 mW V |
| T, 8.27 kHz | S2 | −46.9 dB | 145; 57 Hz | 0.20 mW / 2.0 mW / 5.5 mW V |
| T, 8.27 kHz | S3 | −51.8 dB | 48; 174 Hz | 66 µW / 0.66 mW / 5.5 mW V |
| T, 8.27 kHz | S4 | −56.0 dB | 18; 458 Hz | 25 µW / 0.25 mW / 2.5 mW |
| Umbrella, 8.27 kHz | S1 | −41.6 dB | 403; 21 Hz | 0.69 mW / 6.9 mW / 55 mW V |
| Umbrella, 8.27 kHz | S2 | −46.9 dB | 119; 69 Hz | 0.20 mW / 2.0 mW / 20 mW |
| Umbrella, 8.27 kHz | S4 | −56.4 dB | 13; 621 Hz | 23 µW / 0.23 mW / 2.3 mW |
| Bare wire, 2.97 kHz | S1–S4 | −64 to −77 dB | 26–492; 6–116 Hz | 0.2–4 µW / 2.2–4 µW (mostly V) / 4.0 µW V |
| T, 2.97 kHz | S1 | −55.1 dB | 480; 6.2 Hz | 31 µW / 92 µW V / 92 µW V |
| T, 2.97 kHz | S3 | −64.1 dB | 61; 49 Hz | 3.9 µW / 39 µW / 92 µW V |
| Umbrella, 2.97 kHz | S1 | −54.4 dB | 460; 6.5 Hz | 36 µW / 0.36 mW / 0.92 mW V |
| Umbrella, 2.97 kHz | S2 | −59.2 dB | 153; 19 Hz | 12 µW / 0.12 mW / 0.92 mW V |
| Umbrella, 2.97 kHz | S4 | −68.1 dB | 20; 150 Hz | 1.6 µW / 16 µW / 0.16 mW |

E.r.p. = 1.83 × e.m.r.p. and e.i.r.p. = 3 × e.m.r.p.

Part A's summary gives the loss range as 25 Ω to 6 kΩ, for efficiencies of −42 to −64 dB at 8.27 kHz and −54 to −77 dB at 2.97 kHz.

**Voltage-limited ceilings** (P = (VωC)²R_r) [E, Table C]:

| Antenna, f | 20 kV | 50 kV | 100 kV | 200 kV |
| --- | ---: | ---: | ---: | ---: |
| T, 8.27 kHz | 2.6 mW | 16 mW | 66 mW | 0.26 W |
| Umbrella, 8.27 kHz | 6.6 mW | 41 mW | 164 mW | 0.66 W |
| T, 2.97 kHz | 44 µW | 0.27 mW | 1.1 mW | 4.4 mW |
| Umbrella, 2.97 kHz | 0.11 mW | 0.68 mW | 2.7 mW | 11 mW |
| Umbrella, 1 kHz | — | 8.8 µW | 35 µW | — |
| Umbrella, 300 Hz | — | — | 0.28 µW | — |
| Umbrella, 76 Hz | — | — | 1.2 nW | — |

### Real antennas of related scale (measured or reported)

- **Ray (1972), 100 ft (30.5 m), 27–60 kHz** [T1, read]:
  - Requirement: 0.2 % efficiency, goal 0.5 %, at 50 kW.
  - A 1:6.66 scale model achieved 0.198 % rising to 0.228 % with more radials. At 100 W input the losses split as 0.226 W radiated, 72 W in the coils and 27.8 W in the ground.
  - Measured bandwidth–efficiency product was 2.08 Hz. The two-element reconfiguration measured 0.215 % (radiation resistance 0.0204 Ω, ground 5.5 Ω, coil 4.85 Ω referred to the base; these resistances give 0.197 %, whereas Ray's eq. (55) prints 0.218 %, "approximately .215 as measured"), with 90 kV on the top hat at 50 kW.
  - The predicted full-size efficiency of the hexagonal version was 0.673 %.
  - Scaling Ray's result to 61 m at 8.27 kHz gives about −38 to −43 dB. The scaling uses R_r ∝ (h_e·f)², which gives ×0.25, and coil loss ∝ X_C, which gives about ×10. That is within a few dB of Part A's −42 to −47 dB [E].
- **Auburn University VLF stations (1967–68), two 300 ft (91 m) 12-rib umbrella antennas at 20.9 and 21.9 kHz, Alabama** [T17, read, OCR]:
  - Measured: static capacitance 5,160–5,200 pF at 1 kHz (5,812–5,830 pF operating, including strays); tuning coils of 9.9–10.8 mH in copper tubing on paraffined oak formers, with Q ≈ 85 (16–17 Ω); ground resistance 2.3–2.5 Ω; −3 dB bandwidth of the tuned antenna and transmitter about 545 Hz.
  - Radiation resistances of about 1 and 10 mΩ, inferred from received fields that were 17.2 and 7.2 dB below the design prediction, give efficiencies of about 0.005 % (Gold Hill) and 0.054 % (Fairhope) from the resistances (−43 and −33 dB; eqs. 95–96), or 0.01 % and 0.082 % (−40 and −31 dB) from base voltage, capacitance and input power (eqs. 93–94, the values listed in Table 6). The report calls both sets an order of magnitude below prediction and notes that the inferred losses may include radiation-pattern effects. The design had assumed 1.4 Ω of coil loss, 0.35 Ω of ground loss and 0.9 % efficiency, so the shortfall is about 10–22 dB, depending on antenna and estimator [E]. The report names the receiving whip's assumed effective height, and lower ground conductivity around one transmitter, as possible causes of the shortfall.
  - Received fields (averages 3.3 and 10.5 µV/m on paths of about 320 km) varied by a factor of three above and below average over winter and spring 1968.
  - [E] Coil loss, not ground loss, dominated, and the measured coil Q of 85 lies below Part A's adverse case S4 (Q_coil = 150).
- **Balloon cable, 1,000–1,500 m, 6.6–21 kHz** [T2, read; P3, abstract]:
  - 100 W radiated at 6.6 kHz and 10 kW at 21 kHz, both voltage-limited.
  - Typical operation: 40 A and 84 kV at 13.275 kHz, with 1,400 W or 940 W radiated depending on wind.
  - Far fields: 160 µV/m at El Segundo, and 10–30 µV/m at Dunedin, New Zealand.
  - Operations were limited to winds below 25 kt, and both balloons were destroyed on the ground in high winds [T2, read].
  - Part B, from a search summary only: the balloon-lofted TVLF facility was a conducting cable lofted to ≈1200–1500 m by a 1000 m³ helium balloon (design 3000 m), base-driven monopole. One cited measurement is 13.275 kHz at 36.6 A base current — Koons & Dazey 1983 IEEE TAP 31:243 [P3]; Aerospace SD-TR-82-31 [T2] [P/T, secondary via search summary only]. This is above 9 kHz; whether it operated below 9 kHz was not established by Part B. Part A's reading of [T2] (above) gives 100 W radiated at 6.6 kHz. The two parts' currents at 13.275 kHz are consistent: [T2]'s current monitor reads the antenna base current plus the winch capacitance current, so the typical 40 A at the monitor is 36.6 A at the antenna base, the value used for the 1,400 W and 940 W measurements [T2, read on re-check].
- **Navy stations (reference scale, much larger)** [T5, T4, read]:
  - NWC: R_r = 0.142 Ω and h_e = 184 m at 15.5 kHz; 0.323–0.329 Ω and h_e = 194 m at 22.3 kHz. Radiated 462 kW at 15.5 kHz and 1,065 kW at 22.3 kHz for 1,800 A (the report prints "23.3 kHz" here, but its operating frequency is 22.3 kHz and 1,800² × 0.329 Ω gives 1,066 kW).
  - NPM: h_e about 192 m at 16.6 kHz.
  - Insulator contamination (salt and red dust) and ground-system deterioration are listed as causes of efficiency loss [T5, read].
- **Amateur sub-9 kHz stations** [G1, G3–G6, read; e.r.p. as reported, e.m.r.p. = e.r.p./1.83]:

  | Station and antenna | f | Transmitter | Reported e.r.p. | Implied efficiency (e.m.r.p./P_tx) [E] |
  | --- | ---: | ---: | ---: | ---: |
  | DK7FC, 200 m kite wire | 6.47 kHz | about 500 W | 11 mW | −49 dB |
  | DK7FC, kite (record) | 8.97 kHz | not given | 58 mW | — |
  | DK1IS, Marconi-T 4 × 33 m at 13 m, 660 pF | 8.27 kHz | 5.4 W | 200 nW | −77 dB |
  | IW4DXW | 8.27 kHz | 5 W | 300 nW | −75 dB |
  | F5VLB, 75 m ground loop (32 V, 160 mA) | 8.27 kHz | 5 W | 40 nW (estimated) | −84 dB |
  | VO1NA | 8.27 kHz | not given | 10 µW | — |
  | DF6NM | 8.97 kHz | not given | 5 µW | — |
  | DK7FC, 970 Hz | 970 Hz | not given | 18 nW | — |
  | DK7FC, 270 Hz | 270 Hz | not given | about 6 pW | — |
  | DK7FC, 80 Hz | 80 Hz | not given | 2×10⁻¹⁴ W (5 kV on antenna) | — |

  - OE5ODL (Austria) used balloon-lifted wires of 80 m and 125 m at 8.97 kHz, received at 2,478–2,766 km: 4X1RF (Israel) at 2478 km and TF3HZ (Iceland) at 2766 km, in Feb 2011 [G1].
  - For DK1IS's 13 m T, Part A's model (544–660 pF, h_e = 12.1 m) with S3–S4 losses gives 0.56–1.7 µW e.m.r.p. at 5.4 W. The reported 200 nW e.r.p. is 0.11 µW e.m.r.p., 7–12 dB lower. The cause is unresolved: higher loss, a lower h_e from surroundings, or the estimation method [E on G1]. IW4DXW's antenna is not described.

### Earth-electrode antennas in practice [G1, read; E]

- DK7FC/P, 1,130 m ground loop:
  - 2.44 A (487 W) at 2,970 Hz: messages decoded at 840 and 975 km.
  - 1.75 A (350 W) at 8,270 Hz: carrier detected at 6,768 km.
  - 0.6 W (about 72 mA, if the 114 Ω implied by the 350 W, 1.75 A record at the same frequency applies) at 8,270 Hz: decoded at 840 km.
- DL3JMM:
  - 450 m earth dipole at 2,970 Hz: decoded at 1,081 km.
  - 4.5 A into a 500 m dipole at 1,670 Hz: detected at 576 km.
  - About 300 W into a large ground loop at 8,270 Hz: decoded at 5,200–7,493 km, and 194 nV/m measured at 5,200 km.
- DL0HOT: 1,320 m ground dipole at 470 Hz, decoded at 324 km.
- **Checks of Part A's model against these reports [E]:**
  - Part A's h_eq formula with σ = 10⁻³ S/m gives e.m.r.p. of 0.19 mW (2.97 kHz) and 2.2 mW (8.27 kHz) for DK7FC's two high-power cases.
  - F5VLB: 79 nW e.m.r.p. predicted, against 53 nW implied by 5.5 nV/m at 578 km. That is consistent.
  - EA4GHB: 350 m, 0.45 A, dry sand gives about 14 µW (σ = 10⁻³) to 140 µW (σ = 10⁻⁴) predicted. That predicts 46–147 nV/m at 1,432 km against 13 nV/m reported. The 11–21 dB discrepancy is unresolved.
  - With Wheeler's height (half this chapter's h_eq) every prediction above is 6 dB lower: DK7FC 0.05 and 0.54 mW; F5VLB 20 nW, 4.3 dB below the 53 nW implied; EA4GHB 3.4–34 µW and 23–74 nV/m, 5–15 dB above the 13 nV/m reported [E].

### Amateur and experimental sub-9 kHz records (all grey literature [G])

Unless stated otherwise, the source is the "Some recent milestones with amateur radio experiments at VLF" list compiled by Paul Nicholson (abelian.org/vlf/amateur-radio/list-date.html, CSV vlf-milestones.csv) [G1]. Access: read; re-checked entry by entry on 13 Nov 2026 against the Internet Archive copy of the CSV and date list of 17 Aug 2025, because abelian.org did not resolve. That copy's latest entry is 2025-03-14, so later entries could not be checked. The C/N0 values in these records are Part B's derivation [E]: C/N0 = Eb/N0 + 10·log10(NC·5.7/T_eff), with T_eff = N_stack·(6NC + CRC + K − 1)·n·T_s. The EbNaut notes confirm this definition: the reported Eb/N0 is computed from the carrier S/N with the NC × 5.7 information bits and the overall code rate of the cascaded codes (notes.shtml [G10]). The formula reproduces reported S/N in 1 Hz: W4DEX 25-char −22.4 vs −22.2 dB; W4DEX 42-char −21.6 vs −21.54 dB; OK2BVG −22.3 vs −22.28 dB; DK7FC/P→DL7NN −21.6 vs −21.6 dB. For stacked decodes the C/N0 is the average over all stacked transmissions. "Rep." marks evidence of repeatability; everything else is a single reported success. Part A converts some of the same decodes differently (see "Real-world C/N0 implied by amateur links" and "Contradictions").

**R1. Stefan Schäfer DK7FC (Germany, near Heidelberg/Frankfurt)**

| Date | f (Hz) | TX antenna / current / power | ERP (as reported) | RX, distance | Mode / integration | Result |
|---|---|---|---|---|---|---|
| 2010-03-15 | 8970.01 | kite-lofted vertical wire (2nd kite test) | up to 1.7 mW | Todmorden 858.7 km | CW; faint in 5 mHz/200 s spectrogram over 90 min | ≈2.0–3.0 fT, 6.6–7 σ (abelian /vlf/9k/ [G2] and milestone list) |
| 2010-03-21 | 8970 | kite (3rd) | ≈2 mW | 10 stations to 859 km; IK1QFK 544 km | DFCW600 (10-min dots) and DFCW120/QRSS120 | 2.7 fT ±50 %, 9.6 σ in one 10-min dot (abelian /vlf/9k/ [G2], /vlf/ss100321/ [G6]) |
| 2010-08-01 | 6469.97 | 200 m wire on an 8.1 m² kite at 45–80° elevation; ≈500 W, 0.75→0.92 A into loading coil, ≈20 kV; resonance 6.4–6.6 kHz, shifting to 5.7 kHz at low kite angle | ≈11 mW (est.) | F5WK 460 km; Todmorden 859 km | 12-min carrier, 1.8 mHz bin, blanking at threshold = mean amplitude (4–8 kHz pre-filter) | 2.5 fT (±50 %), >10 σ; blanking cut noise 15× (background ≈95 fT/√Hz, ≈3× winter) (abelian /vlf/ss100801/ [G3]) |
| 2010-10-02 | 8970 | kite (7th) | – | Todmorden 859 km; SQ5BPF 904 km | three carriers of 24–32 min, 700 µHz bins; blanking active ≈60 % of the time | 6.1–9.2 fT; S/N 15.6–25 (amplitude) = 24–28 dB in 700 µHz, the third carrier after a de-chirp of 43.2 mHz/h for TX drift (abelian /vlf/ss101002/ [G4]) |
| 2010-10-23 | 8970 / 5170 | kite (8th); first GPS-locked TX | – | EI8JK 1310 km; 5170 Hz first use, 6 stations | DFCW-600 | detection (list) |
| 2010-12-04 | 8970 | kite (9th) | 58 mW (record VLF ERP) | 4X1RF Israel 2873 km; 23 stations in 9 countries | carrier | detection distance record (list) |
| 2011-02-06 | 8970 | kite (10th) | – | TF3HZ Iceland 2404 km; 24 stations | carrier | first VLF into Iceland (list) |
| 2017-05-05/25 | 8270.1 | fixed station (antenna not described in sources read) | – | Todmorden 881 km | EbNaut 8K19A/8K21A, 10 s symbols; 100 and 160 chars; 13.7 h and 22 h single transmissions | Eb/N0 +0.2 / +0.6 dB → C/N0 ≈ −19 dB-Hz. **Rep.**: many 2017–2018 decodes on this path |
| 2017-11-14 | 8270.1 | fixed | – | Todmorden 881 km | EbNaut 4K19A, 6 s, 300 chars, 2 overnight transmissions stacked (2×12.2 h) | +1.7 dB, BER 21.4 %, ≈70 info bits/h averaged over both transmissions (1,710 bits in 24.4 h) → C/N0 ≈ −15 dB-Hz |
| 2017-10-23 | 5170.1 | fixed | – | RN3AUS 1985.6 km | EbNaut 8K19A, 1 char, CRC6, **180 s symbols** (12 h) | +2.6 dB (4 stacked: +4.5 dB) → C/N0 ≈ −36 dB-Hz; 0.5 info bits/h |
| 2017-11-06 | 8270.1 | fixed | – | RC4HAA 2877 km (land path) | 8K19A, 1 char, 180 s symbols | +5.4 dB → ≈ −33 dB-Hz |
| 2018-01-15 | 5170.1 | fixed | – | RN3AUS 1986 km | 16K21A, 12 chars, CRC20, 30 s; **17 overnight repeats stacked** (≈254 h) | −0.27 dB → average ≈ −41.5 dB-Hz |
| 2018-02-19 / 23 | 8270.1 | fixed | – | W1VD 6096 km (first E→W transatlantic decode below 9 kHz) | 16K21A, 1 char, 32 s, CRC24 (rank 31465); then 2 chars, 30 s, 3 nights stacked | +2.5 dB (−34 dB-Hz); +0.62 dB (−37.7 dB-Hz) |
| 2017-03-11 | 2970.000081 | fixed | – | Todmorden 881 km | carrier, 13:00–00:00 UT × 9 days stacked | 14.0 dB in 25.25 µHz (the width of one 11-h segment) after stacking nine daily segments → ≈ −41.5 dB-Hz per transmission, as for the list's other stacked records (−32.0 dB-Hz if the stack is ignored) [E] |
| 2019-04-06 | 2970.1 | DK7FC/P: 1130 m ground loop, 2.44 A, 487 W | – | SQ5BPF 975 km; Todmorden 840 km | 16K21A, 7 chars, CRC16, 6 s (2.1 h) | −0.4 dB (BER 42.5 %, list rank 14143) / +1.0 dB (BER 39.1 %) → −23 / −22 dB-Hz |
| 2019-12-27 | 8270.005 | 1130 m ground loop, 1.75 A, 350 W | – | N4VLF 6768 km (and W1VD) | carrier | E→W detection record (no S/N in list) |
| 2020-04-05 | 8270.005 | 1130 m ground loop, **0.6 W from a tablet computer** | – | Todmorden 840 km | 16K21A, 9 chars, 20 s (8 h) | +1.5 dB → −26 dB-Hz |
| 2021-05-12 | 1970.005 | 1130 m ground dipole, 2.75 A | – | DL7NN 379 km, 3-axis RX | 16K23A, 5 chars, 6 s | +2.0 dB; −21.6 dB in 1 Hz |
| 2018-02-07 | 970.0025 | not described in list | 18 nW | 40.5 km | 16K21A, 80 s symbols, CRC17; 2 repeats stacked (2×23.8 h) | +0.5 dB → −37 dB-Hz |
| 2018-11-26 | 80.005 | 5 kV on antenna | 2×10⁻¹⁴ W | 3.5 km (near field) | 16K21A, 10 s, 45 chars | +6.7 dB |
| 2019-02-17 | 22.97 | – | – | 3.5 km (near field) | 8K19A, **60 s**, 100 chars, CRC8 (≈83 h single transmission) | +1.2 dB |

At 970 Hz DK7FC reportedly needed 5 kV on the antenna for 15 mA, which gave about 3 nW ERP (search-result summary of sub9khz news; secondary, not read in full). Sub-9 kHz operation in Germany started from a Bundesnetzagentur indication that amateurs were "free" to transmit below 9 kHz (abelian /vlf/9k/ [G2, read]). A 2023 operating guide states that operation below 8.3 kHz is unregulated in most countries, Germany included, provided strong harmonics do not interfere with higher frequencies, that more than 1,000 km has been bridged at 8270 Hz with 5 W, and that the world records on 8270 Hz and 5170 Hz were achieved with 300 W (DL3JMM [G24, read]).

**R2. Paul Nicholson (Todmorden, UK, 53.703 N 2.072 W) — receiving system and analyses**
- Todmorden used orthogonal 20 m² loops: 1.5 fT in 1 Hz, with samples timestamped to ±100 ns of UT, in August 2010 (abelian /vlf/ss100801/ [G3, read]); system noise 2.8 fT/√Hz at 9 kHz and data timing error 30 ns RMS in October 2010. Flux densities are quoted to ±50 %, and a later current-injection calibration raised them by 6 % — abelian /vlf/mv101009/ [G5, read]. The EbNaut notes recommend orthogonal loops plus a vertical E-field probe, with output timestamped against GPS PPS and continuous correction of sample-rate drift (vlfrx-tools; Spectrum Lab on Windows) [G10, read].
- Combining H- and E-field outputs was needed to decode W4DEX at 6194 km (Nicholson post, rsgb_lf_group, 4 Jan 2015 [G11, read]). Antenna-response synthesis may steer away from the noise bearing rather than toward the transmitter (EbNaut notes [G10]). On 2010-10-02, however, pointing at the transmitter worked better than nulling the noise (abelian /vlf/ss101002/ [G4]).
- Seasonal noise at Todmorden is given with the natural-noise measurements below.

**R3. VO1NA (Joe Craig, Newfoundland)**
- Antenna: rotated-L about 100 m long at about 10 m average height (RAC repost of the ARRL item [G16, read]). 2014 TX chain: 10 W amplifier, step-up transformer, 500 Ω lossy tuning coil (L = 0.35 H), 100 m wire. His Canadian letter of authority allowed 10 mW at 8.0–8.3 kHz — sub9khz "VLF Amateur News 2014" [G15, read]. **ERP is about 10 µW**: the milestone list notes that ARRL's "10 mW" is incorrect, and the RAC repost repeats 10 mW.
- 2017-05-08 → Todmorden 3575.5 km: first transatlantic carrier CA→UK, 13.9 dB in 46.296 µHz (6 h) → C/N0 ≈ −29.4 dB-Hz [E].
- 2017-10-23 → Todmorden: first sub-9 kHz CA→UK message. EbNaut 8K25A, 3 chars, 55 s symbols (7.1 h), Eb/N0 +3.0 dB → ≈ −28.7 dB-Hz. 2017-11-06: 5 chars 8K19A, 26 s (3.7 h), +1.8 dB → −24.9 dB-Hz.
- 2017-12-22 to 28: 1-character decodes (16K21A, 60 s, CRC8, 9.1 h) at DL4YHF 4302 km (+4.7 dB), SQ5BPF 5097 km (+1.0 dB), IK1QFK 4533 km (+1.6 dB), RN3AUS 5811 km (+5.36 dB, 2 stacked) and N4VLF 2415 km (−0.1 dB). On 26–29 Dec the same transmissions reached Todmorden (3,576 km) at Eb/N0 +13.0, +12.3 and +11.4 dB, i.e. ≈ −24.6 to −26.2 dB-Hz (msg00450 [G13, read]; C/N0 [E]). 2018-01-04 Hawley TX 4240 km: −0.7 dB with 7 repeats stacked (≈63 h for 5.7 bits; average C/N0 ≈ −46.7 dB-Hz, the lowest in this compilation).
- 2021-01-06 → DK7FC 4369 km: 50 chars, 16K21A, 6 s, **18 transmissions stacked** (≈161 h), +0.31 dB.
- 2025-03-14 → F5WK (France) 4038.5 km: carrier detection in 4-hour integration (list; no S/N given).
- **Phase repeatability.** At Todmorden the VO1NA phase on three consecutive nights in December 2017 was −170.4°, −169.3° and −175.0°, at Eb/N0 of +13.0/+12.3/+11.4 dB (the post's dB values are Eb/N0: its Forest −0.1 dB and Hawley +5.7 dB match the list's Eb/N0 for the same decodes). At weak-signal sites the phase varied: Forest −121°/+5°/−33°, Bielefeld +11°/−8° — Nicholson, rsgb_lf_group msg00450, 29 Dec 2017 [G13, read]. **Rep.**

**R4. W4DEX (Dex McIntyre, North Carolina)**
- ERP estimated at about 150 µW and the received field about 0.12 fT at Todmorden — Nicholson, rsgb_lf_group 2015-01 msg00095 [G11, read].
- **Antenna (2014, 35.273 N 80.371 W)**: a vertical wire from an insulator 29 m up, plus a top hat of about 170 m of No. 18 copperweld running 7–20 m above oak and pine trees. Total capacitance is about 1200 pF. The loading coil is 5030 ft of No. 22 wire (≈265 mH; 540 mm × 840 mm) with a variometer, and the **antenna current is about 1 A** through a powerstat-core matching transformer from a bridged Hafler P-3000. The carrier is GPS-locked from an HP Z3801A 10 MHz reference via an HP 3586B. "LWPC requires ERP of 150uW to reproduce the observed flux density", the centre of the range estimated from the antenna dimensions and measured current — w4dex.com/vlf/8971Hz/ [G22, read]. [E] With ERP = 1.8·I²·160π²(h_e/λ)², 150 µW at 1 A and λ = 33.4 km implies an effective height h_e ≈ 7.7 m (about 10 m if the LWPC power is read as e.m.r.p.), far below the 29 m mast. Trees under the top hat probably act as the effective ground plane. This is a caution for any tree-surrounded 61 m-class antenna: effective height, not mast height, sets ERP.
- 2014-05-30/06-02: first strong sub-9 kHz transmission in North America; first transatlantic W→E sub-9 kHz detection on 8971 Hz at Todmorden 6194 km. Night of 2014-06-02 (00:00–06:00), combined H+E outputs: 12.5 dB in 46 µHz → C/N0 ≈ −30.9 dB-Hz [E]. Daytime average was about 0.02 fT in 23 µHz at similar S/N (w4dex.com page [G22]). 2014-06-03: Hawley TX 1816 km (list).
- 2014-12-15/16, 8822 Hz → DL4YHF 6918 km: 11.2 dB in 39.7 µHz (21:00–04:00) and 12.6 dB in 55.5 µHz; hourly S/N in 278 µHz was about 2–7 dB (sub9khz 2014 news [G15]). Derived C/N0 ≈ −32.8 dB-Hz (−30.0 dB-Hz from the 55.5 µHz figure, if that is a single window) [E].
- 2014-12-29 to 2015-01-01: first EbNaut in North America (N4VLF 254 km, +6.3 dB); first transatlantic W→E messages at Todmorden: 'EM95' 4 chars 9 s (−0.8 dB), 12 chars 14 s (+1.0 dB), 25 chars 8 s (−0.1 dB). In the 25-char test 1153 of 3040 symbols were wrong but the FEC corrected all of them. Information rate 24.6 bits/h at "80% of the channel capacity" (post [G11]) versus 21.1 bits/h at 70 % in the worked example (notes [G10]); [E] the post's figure corresponds to the 166 payload-plus-CRC bits over the 6.76 h message, the notes' to its 142.5 information bits. Too weak for squared-amplitude phase estimation, so a brute-force phase search was used (msg00095 [G11]).
- 2015-02-08 → N4VLF 254 km: 40 chars, 8K19A, 1.5 s symbols, Eb/N0 +1.5 dB, listed as the longest VLF message (list). [E] With CRC16 assumed, the message lasted 0.91 h: about 250 information bits/h at ≈ −10 dB-Hz.
- Worked example in the EbNaut notes: Es/N0 −13.2 dB in 0.125 Hz; S/N −22.2 dB in 1 Hz; −56.2 dB in 2500 Hz after blanking (≈ −76 dB before); symbol error rate 0.38; list rank 89/50 000; decoder memory 49 GB; decode 2.5 min on 32 cores. Carrier tests in the preceding days varied by about ±2 dB, and the message average fell near mid-range [G10, read].
- 2017-12-26 → Todmorden: 42 chars, 16K25A, CRC16, 7.5 s, **22:15–07:59 UT (9.7 h) with "constant reference phase"**. Eb/N0 +0.1 dB (calculator predicted +0.15 dB); BER 39 %; −21.54 dB in 1 Hz; rank 69 758; 75 GB; 598 s on an AWS r4.4xlarge. Nicholson called the signal "reliable repeatable good quality" — msg00380 [G12, read]. **Rep.**
- 2017-12-27 → IK1QFK 7173 km: 3 chars, 30 s, CRC20, +0.4 dB (world record for EbNaut at the time). 2018-01-05 → DL0AO 7258 km: six nightly repeats stacked, −1.9 dB (lowest Eb/N0 among the sub-9 kHz decodes listed).
- **Night-to-night phase shifts.** W4DEX phase shifted by about +147° to +161°, then −67° to −130°, at Forest (254 km), Hawley (1816 km) and Todmorden (6194 km) alike; the post reports the shifts as roughly the same at all three receivers (msg00450 [G13, read]). [E] Such a common-mode shift suggests a transmitter-side origin, but the cause was not established.

**R5. Italian and other receptions/transmissions**
- IK1QFK (Renato Romero, Cumiana, Italy) received the first Germany→Italy VLF (2010-03-21, 544 km) and many later decodes: W4DEX 7173 km; VO1NA 4533 km; DK7FC 5170 Hz 505 km, 3 chars 60 s, +5.0 dB; 12 chars −0.1 dB with 2 stacked. Germany→Italy at 4470 Hz (+5.0 dB) and 2970 Hz were also logged (list).
- IW4DXW (Italy, TX): 2017-12-14 first Italy→UK message, 16K21A 3 chars 30 s, +1.8 dB at 1412.6 km. 2020-02-01: QRP 5 W TX, ERP 300 nW, carrier 15.9 dB in 2.3 µHz (≈5 days) → C/N0 ≈ −40.5 dB-Hz.
- IZ7SLZ (Italy, RX): OK2BVG (Czech, 100 W, 550 mA)→IZ7SLZ +2.0 dB in 2021; the same transmission reached Todmorden 1417 km at +2.4 dB and −22.28 dB in 1 Hz.
- IK7FMO (SE Italy) transmitted on 8269.993 Hz with 70 W into a Marconi antenna to the OK2BVG grabber at 866 km in 2014. The signal was "not yet stable" (sub9khz 2014 [G15]).
- Other: OE5ODL's balloon-lofted antennas and their 2011 receptions are under "Real antennas of related scale". DF6NM: kite antenna, 120 mA, up to 25 µW ERP at 35 m effective height (search summary; secondary); 5 µW ERP fixed carriers 1027 km to Todmorden in 2010 (0.24–0.50 fT, SNR 1.2–5.7 in 20–60 µHz bins; 5–13 dB above the 0.11–0.13 fT that the page's single-mode model predicts [G5]); ≈1 µW ERP at 5170 Hz → Todmorden 11.2 dB in 55.5 µHz in 2017. DK1IS: Marconi-T 4×33 m at 13 m height, C = 660 pF, 5.4 W TX, 200 nW ERP, daytime detection at 1071 km (13.9 dB in 15.4 µHz). F5VLB: 32 V × 160 mA into a 75 m ground loop, est. 40 nW ERP, carrier at 578 km after 4 days (5.5 nV/m; 13.8 dB in 2.86 µHz → −41.6 dB-Hz). EA4GHB (Spain): 60 V, 450 mA into a 350 m ground loop over dry sandy soil; 13 nV/m and −24.2 dB in 1 Hz at 1432 km; EbNaut 8K19A 30 s +0.0 dB at 1524 km. SQ2BXI+SQ5BPF/P: 100 W into an earth dipole at Gdynia → Todmorden 1343 km, −1.5 dB. RN3AUS/A: ground dipole, Russia → Todmorden 2450 km, +1.3 dB. DL3JMM: about 300 W into a large ground loop → ILDN receivers in India (Gulmarg 5200 km +5.9 dB at 194 nV/m; Prayagraj 6379 km +5.1 dB; Tirunelveli 7493 km −0.6 dB, the sub-9 kHz world record of 2022-12-06), N4VLF 7033 km (+0.7 dB) and, at 5170 Hz, Prayagraj 6379 km (Eb/N0 −0.9 dB, "improved to +2.5dB during next three nights"; the list does not say whether by single-night decodes or by stacking, the linked posts were not obtained, and repeatability is therefore not confirmed) and Kolkata 7067 km (carrier).
- Other modes at VLF (all from the list): MFSK-37 with 300 s characters (DF6NM kite, 2011, 5 chars in 25 min to the UK). First international VLF two-way QSO, DF6NM–OK2BVG, 2012-03-25, MFSK-37 with 30-min characters. Opera OPDS-4H (2013–2014, 175 and 1028 km). WSPR-15 at 10 µW ERP (53 km) and 283 km (2020). FST4W-1800 at 164 km (2020, reported SNR −26 dB, conventionally in 2500 Hz ≈ +8 dB-Hz).

**R6. Earth-mode (G3XBM and others) [G14, G15, read]**
- G3XBM (RadCom Oct 2011 [G14]; blog [G15]): 5 W TDA2003 TX, 1 m rod about 20 m from a second earth on house radiator pipes, frequency from a divided HF crystal (drift "only around 0.1 Hz worst case"). Received at **6 km at 10 dB S/N in 11 mHz** (8.7605 kHz, QRSS30/continuous carrier, earth-electrode RX, Spectrum Lab locked mode), and 18 dB in 45 mHz on an 80 cm loop (14 Aug 2011). 1.095 kHz: about 10 dB in 11 mHz at 6 km. 838 Hz: 5.1–5.6 km. G6ALB (40–44 W) at 3 km gave ≥20 dB S/N in QRSS3 and WSPR −16/−17 dB.
- Failures: nothing at 7 km; 320 mW reached 1.5 km but failed at 5.1 km; 14 m spacing with short electrodes failed. Copy depended on buried metal near roads (nothing was copied away from roads or where pipes were absent), wet peat soil lowered earth-electrode pick-up in one test, and by-ear copy was limited to 0.3–0.5 km by 50 Hz hum and broadcast breakthrough. Water pipes turned out to be plastic, so "utilities assistance" was hard to quantify.
- DK7FC/P: about 50 W into electrodes 300 m apart received at 5.2 km (blog [G15]). A 600 m baseline and a few hundred watts reached "just less than 50 km", but "some of his signal was being radiated" (RadCom [G14]). The radiated share of energy from low-power earth-mode was estimated at "a few picowatts" (RadCom [G14]).

### Military and strategic ELF systems

Part B summarises the strategic systems as grounded lines 22–90 km long carrying 150–300 A that radiate ≈1–8 W at 76 Hz.

| System | Antenna / current | Radiated power | Signal / rate | Evidence |
|---|---|---|---|---|
| US Navy WTF (Clam Lake, WI) + MTF (Republic, MI), 76 Hz | WTF: two orthogonal 14-mile (22.5 km) overhead lines ("X"), 300 A each. MTF: 28-mile NS + 2×14-mile EW ("F"), 150 A. Ends grounded with 1–3 miles of buried wire and well electrodes 100–300 ft deep. Ground σ ≈ 2.4×10⁻⁴ S/m. Site power up to 3 MW from 1 MW generators (Clam Lake). | **8 W** "from the dual-site system" (Navy fact sheet, 2003 [T14; T/G, read]). The 1972 WTF test radiated "less than 1 W" (MIT LL [T13, read]). | MSK centred on 76 Hz, shifting between 72 and 80 Hz (IITRI reports via Altgelt [G19, read]). Synchronised dual-site broadcast 24/7 from 1 Oct 1989 (fact sheet [T14]). Data rate in 1972 test: **0.03 bit/s**, 20-character message (MIT LL [T13]). Seafarer-era estimates: 1 bit/s to 1 bit per 10 000 s; "a few characters every 15 minutes"; "almost two hours" for 12 letters (FEIS 1972 via Altgelt [G19, secondary]). | Altgelt (n.d., c.2005) [G19]; NCTAMS LANT fact sheet (2003) [T14]; MIT LL history [T13]; FS&B 1998 [P32] |
| 1972 Sanguine demonstration (USS Tinosa) | WTF test antenna | <1 W | Binary MSK band-spread on a 76 Hz carrier, binary convolutional coding with a sequential decoder, adaptive nonlinear noise processing, adaptive nulling of power-line interference, ocean-filter compensation, real-time minicomputer (Varian 620/L-100), trailing-wire antenna. Message decoded on board while submerged near 45 N 30 W. Decoding "consistently at all times"; transmitter and receiver time synchronisation maintained "over long periods". | MIT LL, "ELF Communications Development" [T13, read]; Bernstein et al. 1974 [P26, abstract via search] |
| Nonlinear processing gain | – | – | Clipper/whitening/notch/post-notch nonlinearity/phase-coherent matched filter. **7–20 dB** better than a linear receiver at times of high ELF noise. MIT LL states that the statistical properties of ELF atmospheric noise made a **100× (20 dB) reduction in transmitted power** over the previous Sanguine design possible, the savings resulting "primarily from nonlinear noise processing and efficient signal coding". | Evans & Griffiths 1974 [P33, abstract via search]; MIT LL [T13, read] |
| ZEVS (Kola, 69 N 33 E), 82 Hz | "Two parallel horizontal grounded antennas, each about 60 km long"; generators give "200 to 300 A … from 20 to 250 Hz" (Velikhov et al. 1996, quoted in FS&B [P32]). ≈EW, 13° N of E. First-layer σ ≈ 10⁻⁵ S/m to about 10 km. | Not stated. M ≈ 1.1×10⁵ A·km², **≈10 dB above WTF/MTF**. | CW reception, January 1990 (present in 80 % of synoptic minutes at Søndrestrømfjord); the SNRs compare the January 1990 field with the 1985–1994 January median 80 Hz noise, not with the noise during reception: **3.2 Mm: 0.208 pT, +4.1 dB in 1 Hz, +24.9 dB in a 2-min integration. Dunedin 16.5 Mm: 0.0943 pT, −7.8 dB/1 Hz, +13.0 dB/2 min. Arrival Heights 18.3 Mm: 0.0319 pT, −11.4 dB/1 Hz, +9.4 dB/2 min.** Not detected at Stanford (estimate ≤ +1.5 dB/2 min). Night attenuation 0.5 dB/Mm (derived from the 1990 data); typical day value 1.3 dB/Mm (Bannister, as quoted by FS&B); antipodal focusing +4.3 to +4.7 dB (calculated). | Fraser-Smith & Bannister 1998, Tables 1–2, eqs. (1)–(8), Fig. 1–2 [P32, read, author preprint] |
| ZEVS modulation | – | – | Grey claims: MSK with an 81–83.3 Hz shift and a "message waiting" pattern (81.6 Hz for 8 min, 82.7 Hz for 4 min) recorded in Italy on 8 Dec 2000; "a few bits per minute". | T. Jacobsen, ALFLAB [G20, read; unverified] |
| India (INS Kattabomman, Vijayanarayanam, Tamil Nadu) | Not published. Press says built by L&T with "the highest mast structures in the country", which suggests VLF masts. | – | Press (PTI/Business Standard, July 2014) and Wikipedia describe a new VLF facility, with ELF transmission said to have started in 2014. Sources disagree on whether an ELF transmitter exists. A separate VLF station at Vikarabad had its foundation stone laid in Oct 2024. No technical parameters are public. | Press and Wikipedia via search [G, secondary; not verified] |
| China (WEM, central China) | ≈80 km EW and ≈60 km NS grounded lines, each with a 500 kW transmitter. 0.1–300 Hz, 98 approved frequencies, frequency accuracy and stability <3×10⁻⁸. | – | Coverage radius 2000–3000 km at S/N 10–20 dB (0.5–300 Hz), and 1000–2000 km at 0.1–0.5 Hz. Received 300 m deep in the South China Sea. No data rates given; stated purpose is exploration and earthquake monitoring. The *Engineering* article is not marked retracted: the retraction banners in the publisher's page source are hidden templates with blank dates and links, the publisher's retraction-status service returns no notice, and Crossref records no update or retraction (checked 13 Nov 2026). Press (SCMP, 31 Dec 2018) reported a submarine use. | Lu et al. 2022 [P34, read]; SCMP via search [G21, secondary] |

[E] check: from Clam Lake (≈46.1 N, 90.9 W) to 45 N 30 W the great-circle distance is about 4600 km. MIT LL's "decoded more than 6000 km from the source" therefore refers to other receivers or is inexact. At 0.03 bit/s, a 20-character message of 5–6 bits per character takes about 55–67 min [E].

### Scientific controlled sources in or near 1–9 kHz

- **Siple Station, Antarctica (75°55′ S, 83°55′ W, L ≈ 4.1–4.3)**: built for VLF wave injection; conjugate to Roberval, Quebec. A 1969–70 test used an 8000 ft dipole above the snow before a planned 13-mile (≈21 km) elevated dipole. An elevated antenna's radiation efficiency "can be at least 50 times greater" than a buried one over 3–20 kHz — Katsufrakis, Antarctic J. U.S. 5(4) p.115, 1970 [T15, read (local copy); not re-confirmed on re-check]. Later descriptions give a 21.2 km dipole, then crossed 21 km dipoles on 2 km of ice, with transmitter ratings of 100 kW or 150 kW (sources conflict; secondary via search). In 1983 signals reached Palmer, Halley and South Pole (each ≈1500 km). The fundamental near 2.5 kHz was well defined broadside; at Halley the third harmonic (≈7.5 kHz) exceeded the fundamental by ≈40 dB at equal input power, attributed to antenna end-fire behaviour and "high waveguide attenuation in the 2–4 kHz range" — Carpenter, Bell & Smith 1988 [P40, abstract via NORA]. In the magnetosphere coherent signals showed ≈30 dB exponential growth, saturating near 5 pT — Helliwell 1988 [P41, abstract].
- **HAARP (Gakona, AK; described as a 3.6 MW HF facility in the 2013 study [P36])**: 2125 Hz received at Midway, 4400 km, consistent with ≈4–32 W radiated and an HF-to-ELF conversion efficiency of ≈0.0004–0.0032 % — Moore et al. 2007 [P35, abstract]. [E] That abstract gives no HF power; its two figures together imply about 1 MW, and relative to 3.6 MW the efficiency would be 0.0001–0.0009 %. About 100 days of 500–3500 Hz generation (≈5 million measurements) at a nearby site: day-to-day variation of up to **20–30 dB**, a midnight minimum, nights weaker on average but more variable, and a ≈5 dB strengthening over the first ≈30 min of transmission — Cohen & Gołkowski 2013 [P36, abstract]. The same abstract gives a median power injected into the waveguide of ≈0.05–0.1 W in its baseline configuration (vertical beam, 3.25 MHz, amplitude modulation), with possibly hundreds of watts for brief periods [P36, abstract]. [E] The median is well below the 4–32 W inferred from the 2007 Midway reception, although the two studies differ in frequency and modulation. Geometric modulation (beam steering at the ELF/VLF rate) gave 7–11 dB more than vertical AM, with ≈14 dB directionality. This was found by Cohen et al. 2008 and confirmed as the best of three methods by Cohen et al. 2010 [P37, abstract].
- **Compact and mechanical ELF/ULF transmitters (2017–2025)**: their demonstrated ranges, including the 210 m underwater result, are under "Compact mechanical, piezoelectric and magnetoelectric transmitters" (Physical mechanisms).
- **HAARP data transmission**: QPSK at 1510 Hz with harmonics, 100–800 bps tested, with BER rising as bandwidth widened (Jin et al. 2013, Radio Sci. 48:61–68 [P39], via Miś et al. [P38]; secondary). FSK at 2115/2135 Hz, Morse-coded, **10.93 bit/s**, a 60 s message on 7 Nov 2023 05:45–05:59 UT (2.8 MHz X-mode, ≈50 km beam), received at Chistochina 37 km away — Miś, Gołkowski, Moore & Modelski, URSI AT-RASC 2024 [P38; P-conf, read]. This is a short-range, high-SNR demonstration, not a long-haul link.
- **EISCAT heater communication (2022)**: QPSK at modulation frequencies of 2,017 and 3,517 Hz, at 20, 100 and 400 bit/s, generated by amplitude-modulated heating of the auroral electrojet; the received intensity relative to the natural current depended on the background ionosphere. Pooled with earlier HAARP results, BER was zero for Eb/N0 above 8 dB and followed the complementary error function below it — Yang et al. 2022 [P49, abstract]. The abstract gives no receiver distance; this too is a heater demonstration, not a long-haul link.
- **Kafjord, Norway power line**: 100 kW transportable transmitter on a 22 kV, 10.6 km line; 45 A at 1280 Hz; 72 kW dissipated; **0.17–0.79 W radiated** (transmissions to the SCATHA and GEOS spacecraft), an efficiency of −50 to −56 dB — Dazey & Koons 1982, Radio Sci. 17:589 [P4, abstract]; Part A also read [T2].
- **Balloon-lofted TVLF facility**: see the balloon-cable entry under "Real antennas of related scale", which combines both parts' information.
- **FENICS/power-line ELF sources (Kola)**: FENICS is driven by a 0.5–150 Hz generator [P45, abstract]. Line lengths of 100–120 km, up to 200–300 A and 2.5 MW come from search summaries only and are not in either abstract (not confirmed). Fedorov et al. model ZEVS at 200 A with a realistic source length of 60 km, giving up to 60 µV/m and 6 pT in the nightside upper ionosphere, and find that an unbalanced 50 Hz current of 10 A in lines longer than 100 km would be detectable from low orbit [P45, abstract]. Pilipenko et al. report the 82 Hz signal of "the ELF transmitter on the Kola Peninsula" detected several times by the DEMETER satellite at 660 km altitude, and also by a ground-based magnetometer [P44, abstract].

### Techniques and their thresholds

**D1. EbNaut (coherent BPSK; Nicholson with Vester DF6NM and McIntyre W4DEX) [G10, read]**
- Source coding: 52 valid 6-bit codes, 5.7 bits/char. Outer CRC16 (CCITT, zero initial value) by default; the CRC length is selectable and records use CRC0–32. Inner terminated feed-forward convolutional codes, rate 1/4–1/16, K = 15–25 (e.g. 8K19A d_min 92, 16K21A d_min 204, 16K25A d_min 238). Row/column interleaver. UT-synchronous start (tables.shtml, notes.shtml).
- Thresholds: 16K25A with a list of 50 000 succeeds **85 %/63 % at −0.5/−1.0 dB (25 chars)**, 87/70 % (12 chars) and 84/74 % (5 chars) (index). For 8K25A with 5 chars, 50 % success is at +0.5 dB without list decoding and at −1.5 dB with list 50 000 — about 2 dB extra on top of ≈3 dB of convolutional coding gain (notes). The Shannon limit is −1.59 dB, and the sphere-packing limit is computed for short blocks (appendix). Advice: "better to leave a bit in hand to allow for phase variations" (notes).
- False decodes: P_f(L) = 1 − (1 − 2^−(c+s))^L with s ≈ 0.3·NC. With L = 50 000, P_f = 33.6 % (3 chars), 23.7 % (5), 6.1 % (12), 0.42 % (25) (appendix). Short messages with long lists are therefore error-prone without operator plausibility checks.
- Receiver: 3 kHz band-limit → sferic blanker (≈10× mean rectified floor, 25–35 % blanked, ≈20 dB gain at 8 kHz) → complex baseband at ≈100 Hz → stored. Post-processing: antenna-response synthesis, coherent per-symbol integration, phase reference search (uniform phase in 30° or 15° steps; then 2 and 4 segments, 108 variations), soft Viterbi forward pass plus Soong–Huang list backward pass, CRC and source-code check, then carrier re-measurement after removing the modulation (notes).
- Timing and phase (appendix): phase offset δ costs 20·log10(cos δ). A 30° search grid loses at most 0.3 dB; 15° steps lose <0.1 dB. A timing offset t costs 20·log10(1 − 2|t|/T) for 0 ≤ |t| < T/2 (appendix, re-read from the Internet Archive copy). [E] This is −0.18 dB at 1 % of T, −0.92 dB at 5 % and −1.94 dB at 10 % (worst case, at symbol transitions). The notes ask for timing within 1/10 symbol; NTP suffices for symbols ≥ 1 s.
- Compute: K=25 needs about 2 GB per character (notes). 31 chars at K=19 take 9–16 min on a quad-core and 12 chars at K=21 take 50 min (notes). The 42-char K=25 decode used 75 GB (msg00380 [G12]).
- Carrier as a test signal: an unmodulated carrier is a valid message (all '*'). Long carrier recordings are replayed through the decoder to test code, length and symbol period (notes). This is a real-channel simulation method.

**D2. QRSS / DFCW / MFSK / WSPR-type modes [G]**
- QRSSn uses n-second dots: QRSS3 and QRSS30 (RadCom [G14]). DFCW600 uses 10-min dots and was read visually on spectrograms in 2010 (abelian /vlf/9k/ [G2]). EbNaut index [G10]: slow Morse with dot periods of 600 s or more "makes poor use of the channel capacity".
- WOLF (KK7KA, LF): BPSK at 10 bit/s with alternate reference bits from a known PN sequence. 15-char packets, 80 info bits, K=15 rate 1/6 tail-biting code, 960-bit 96 s frame, repeated and summed. By design and in offline simulation (WOLF is described as a proposed LF format, not as an on-air sub-9 kHz result): copy in 24 s at a level adequate for conventional CW, within two minutes at the level of 0.4 WPM QRSS, and in 20 min to several hours at 10 dB below that, depending on TX and RX stability. Occasional simulated copy at −45 dB and "quite robust" at −40 dB, from signals mixed with recorded or synthetic noise (reference noise bandwidth not defined). Drift of 3 mHz/min costs about 3 dB and about 10 mHz/min "will probably make communication impossible"; sample rates must match within a few ppm — DL4YHF-hosted WOLF description [G18, read].

**D3. Sferic blanking — measured effect [G, read]**

| Test | Blanker setting | Fraction blanked | Gain |
|---|---|---|---|
| EbNaut standard (notes [G10]) | ≈10× mean rectified amplitude, 3 kHz band | 25–35 % | ≈20 dB at 8 kHz (W4DEX: −76 → −56 dB in 2.5 kHz) |
| 2010-08-01, summer (ss100801 [G3]) | threshold = mean amplitude, 4–8 kHz pre-filter | – | noise ÷15 (≈23.5 dB) |
| 2010-10-02, quiet (ss101002 [G4]) | threshold = mean amplitude, 6–12 kHz, no dwell | ≈60 % | noise ÷2.1–2.6 (≈6–8 dB); S/N 7.5→15.6, 7.6→17, 9.6→25 |
| 2010-10 DF6NM tests (mv101009 [G5]) | 1.2× mean, 8–10 kHz band, 1 s moving mean | – | artefact: excess high-amplitude bins ("high tail") below ≈10 mHz resolution, reproduced with Gaussian noise; attributed to chance periodicities in the blanking waveform |

**D4. Frequency/timing requirements reported**
- AA5BW, oscillator stability ≈100 µHz over 12 h at 10 µW ERP: Nicholson estimated he would need 7–10 dB more ERP and **stability of order 10 µHz** (sub9khz 2014 [G15, read]).
- Kite TX drift of 43.2 mHz/h needed a de-chirp to collect energy into one 700 µHz bin (ss101002 [G4]); the undisciplined exciter of the 6.47 kHz kite test stayed mostly within ±500 µHz for 20 minutes (ss100801 [G3]). G3XBM's crystal-divider drift was ≈0.1 Hz (RadCom [G14]). DK7FC first used GPS-locked transmission in Oct 2010 (list), and by 2014 "GPS locked" his transmit frequency in Spectrum Lab; a receiving station (DF6NM) reported an uncorrected 0.25 mHz error in its frequency read-out (sub9khz 2014 [G15]). W4DEX locked to an HP Z3801A GPS standard (w4dex.com [G22]).
- Frequency choice: avoid n×50 Hz and n×16.6667 Hz ("frequency of the german trains grid", in DK7FC's words; the nominal frequency of the 15 kV railway network is 16.7 Hz [G25]); DK7FC chose 6470.00 Hz (ss100801 [G3]). [E] The two figures give harmonic families that nearly coincide near 8.27 kHz (495 × 16.7 ≈ 496 × 16.6667 ≈ 8266.6 Hz) but sit about 6 Hz apart near 2.97 and 5.17 kHz, so line positions should be measured rather than computed. Typical offsets are a few mHz, e.g. 8270.005 Hz.
- Nocturnal Doppler. On the 1027 km DF6NM→Todmorden path the 24 h carrier analysed in 6-h segments with 46 µHz bins fell from S/N 5.2 to 1.2–2.0 between about 21:00 and 09:00. The loss was attributed "mostly" to energy spreading out of the bin by Doppler from changing path length (mv101009 [G5, read; interpretive]). Over the same segments the mean noise rose from 0.08 to about 0.2 fT while the signal fell from about 0.5 to 0.24–0.32 fT, so part of the S/N fall was noise; the page attributes only the loss of signal to Doppler. [E] A sustained 46 µHz shift accumulates about 1 cycle in 6 h. Treated as a constant offset over a 6-h segment, the 4–6 dB fall in signal amplitude corresponds to about 23–29 µHz (|sinc(πΔfT)| = 0.48–0.64).

**D5. Coherent integration lengths demonstrated (sub-9 kHz) [E, from list]**
- Single EbNaut transmissions of 7–14 h overnight are routine. 22 h (160 chars, 8270 Hz, 881 km) and 27.9 h (100 chars, 6470 Hz, 881 km) were decoded, so on that path the integrations spanned day/night transitions. Whether segment phase splitting was needed is not stated.
- Multi-night coherent stacking: 2–18 repeats (up to ≈161–254 h of signal).
- Carrier detections in 2.3–2.9 µHz bins (≈4–5 days coherent) at 578 and 1413 km; 2.67 µHz (104 h) at 227 Hz.

### Real-world C/N0 implied by amateur links (comparison with the modem's −36 dB-Hz), and C/N0 per unit ERP

The EbNaut parameter "16K…" means rate 1/16 and "8K…" means rate 1/8; this reading is consistent with the 24.6 bit/h information rate reported for the 42-character message of 2017-12-26 (239.4 bits in 9.73 h) [E on G1]. The reported Eb/N0 is, however, referred to the NC × 5.7 information bits and the whole message duration [G10], so C/N0 = Eb/N0 + 10 log₁₀(5.7·NC/T) with T = N_stack·(6NC + CRC + K − 1)·n·T_s, as under "Amateur and experimental sub-9 kHz records". Part A had first converted with T_b = n·T_s, which omits the CRC, the K − 1 code-termination bits and the 6-versus-5.7 bits per character and overstated C/N0 by 0.7–4 dB; the rows below are recomputed with T, assuming CRC16 where the list gives no CRC [E] (see "Contradictions").

| Link | f, range | C/N0 (dB-Hz) | Derivation |
| --- | --- | ---: | --- |
| OK2BVG → Todmorden, 100 W, 550 mA | 8.27 kHz, 1,417 km | −22.3 | stated "−22.28 dB in 1 Hz" [G1] |
| EA4GHB → Todmorden, 13 nV/m | 8.27 kHz, 1,432 km | −24.2 | stated [G1]; implies E_n = 0.21 µV/m/√Hz = 0.7 fT/√Hz [E] |
| DK7FC/P → DL7NN | 1.97 kHz, 379 km | −21.6 | stated [G1] |
| DL3JMM → Gulmarg, 194 nV/m | 8.27 kHz, 5,200 km | −22.0 | 8K19A, 4 characters, 30 s symbols, Eb/N0 +5.9 dB (CRC16 assumed; ±0.6 dB for CRC 8–24); implies E_n ≈ 2.4 µV/m/√Hz (8 fT/√Hz) [E] |
| DL3JMM → Tirunelveli | 8.27 kHz, 7,493 km | −28.5 | Eb/N0 −0.6 dB, same message format (CRC16 assumed) [E] |
| VO1NA → DK7FC, 5.5 µW e.m.r.p. | 8.27 kHz, 4,369 km | −32.8 per transmission | 16K21A, 50 characters, 6 s symbols, 18 transmissions stacked to Eb/N0 +0.31 dB (CRC16 assumed) [E] |
| DK7FC/P → Todmorden | 2.97 kHz, 840 km | −21.7 | 16K21A, 7 characters, CRC16, 6 s symbols, Eb/N0 +1.0 dB [E] |
| F5VLB → Todmorden, carrier, 5.5 nV/m | 8.27 kHz, 578 km | −41.6 | 13.8 dB in 2.86 µHz [E] |

- For VO1NA → DK7FC Part A's propagation model predicts 8.9 nV/m, which implies E_n ≈ 1.3 fT/√Hz, consistent with the other blanked noise values [E].

**C/N0 per unit ERP (Part B) [E].** Normalising C/N0 to 1 W ERP (C/N0 + 10·log10(1 W/ERP)) gives the following. VO1NA 10 µW at 3576 km: −24.9 to −29.4 → **+20 to +25 dB-Hz per W**. W4DEX 150 µW at 6194 km: winter EbNaut −21.6 to −26.9 → **+11 to +17**; summer night carrier (June 2014) −30.9 → **+7**. DF6NM ≈1 µW at 1028 km (5170 Hz): −31.4 → **+28.6**. IW4DXW 300 nW at 1413 km: −40.5 → **+24.7**. DK1IS 200 nW at 1071 km by day: −34.2 → **+32.8**. F5VLB 40 nW at 578 km: −41.6 → **+32.4**. The DK7FC kite at 859 km (2010-10-02, 700 µHz after de-chirp) gives −3.6 to −7.7 dB-Hz, but ERP was not stated for that test; earlier kite tests were 1.7–2 mW, and with ~2 mW it would normalise to +19 to +23. A single-dot March 2010 kite reading (2.7 fT against 0.7 fT mean noise in a 10-min dot, probably unblanked) gives roughly −16 dB-Hz, or about +11 per W. Kite normalisation is therefore uncertain by ≥10 dB.
- The spread of about 25 dB reflects distance, season (W4DEX summer vs winter ≈4–9 dB), day/night and ERP uncertainty: the ERPs are estimates and one kite test was ≈3–6 dB below expectation. All the values are after sferic blanking at a quiet, well-equipped receiver.
- Consequence: 32 dB-Hz at 0.6–1.4 Mm needs about 1–5 W ERP (+33 to +25 dB-Hz per W), roughly 17–90× the 58 mW kite record and 10³–10⁷× typical amateur ERP. 1 mW ERP gives about +3 to −5 dB-Hz at 0.6–1.4 Mm and −5 to −23 dB-Hz at 3.6–6.2 Mm.

### Natural noise and receiving sensors (externally noise-limited?)

**Natural noise, ITU-R P.372-18 Fig. 1** [S1, read; graph read by eye, ±2 dB]. The values are minimum and maximum expected hourly medians of Fa over the whole Earth, all seasons and times of day, for a short vertical over perfect ground. P.372 notes very little seasonal, diurnal or geographic variation below 10 kHz, but "larger variability in the 100–10 000 Hz range" from waveguide cut-off. Measurements disagree with the first part of that note: Chrissan & Fraser-Smith report seasonal amplitude variations of up to a factor of 4 in some bands [P30, abstract], and a ground campaign at a mid-latitude site in China (February 2016 to February 2019) found seasonal variations of up to 10 dB across its 3–30 kHz bands, with the CCIR model overestimating the local level and lightning within about 3,500 km dominating the noise (Gu et al. 2021 [P47, abstract]). A site's level can therefore move by up to about 10 dB with season inside the P.372 bracket [E]. E_n = Fa + 20 log₁₀ f_MHz − 95.5 dB(µV/m) in 1 Hz [S1, read, eq. (7)].

| f | Fa minimum (E_n; B equivalent) | Fa maximum (E_n; B equivalent) |
| ---: | --- | --- |
| 76 Hz | about 209 dB (31 dBµV/m; 120 fT/√Hz) | about 218 dB (40 dBµV/m; 340 fT/√Hz) |
| 300 Hz | about 180 dB (14 dBµV/m; 17 fT/√Hz) | about 207 dB (41 dBµV/m; 375 fT/√Hz) |
| 1 kHz | about 153 dB (−2.5 dBµV/m; 2.5 fT/√Hz) | about 184 dB (28.5 dBµV/m; 89 fT/√Hz) |
| 2.97 kHz | about 126 dB (−20 dBµV/m; 0.33 fT/√Hz) | about 171 dB (25 dBµV/m; 59 fT/√Hz) |
| 5.17 kHz | about 126 dB (−15 dBµV/m; 0.6 fT/√Hz) | about 175 dB (34 dBµV/m; 164 fT/√Hz) |
| 8.27 kHz | about 146–147 dB (9–10 dBµV/m; 9–10 fT/√Hz) | about 175 dB (38 dBµV/m; 260 fT/√Hz) |

- The absolute minimum lies near 3–5 kHz, at 124–126 dB.
- In GUI terms N0 = −174 + Fa. At 8.27 kHz that is −28 to +1 dBm/Hz, versus the GUI's −164 dBm/Hz.

**Amateur measurements at Todmorden** [G3–G6, read] (blanker settings and measured gains are tabulated under "Techniques and their thresholds", D3):

- 8.97 kHz, October daytime: 36–53 fT "in 1 Hz" unblanked. Blanking roughly halves the amplitude, and was active about 60 % of the time [G4].
- 8.97 kHz, October 2010: mean noise of 0.067–0.22 fT in 20–60 µHz bins after a 1.2×-mean blanker, about 10–15 fT/√Hz by day and 24–32 fT/√Hz from the late-afternoon segment through the night; system noise 2.8 fT/√Hz at 9 kHz [G5]. [E] These levels are 14–46 times (23–33 dB) the 0.7 fT/√Hz implied by EA4GHB's December 2022 report at the same receiving site; the sources differ in year, receiver, blanker and estimator, and the difference is not reconciled.
- August, 6.47 kHz: 95 fT/√Hz on the bearing of the transmitter, "about 3 times the winter noise level". Blanking reduced the background by a factor of 15 [G3]. Part B adds that after blanking it fell to winter-like levels, suggesting "VLF reception of narrow band signals can be made equally well independently of season" (abelian /vlf/ss100801/) [G3, read; single-test inference].
- Typical natural ELF/VLF rms noise spectral density is 1–100 fT/√Hz, and sferics at more than 500 km are 1–100 pT [T12 Cohen thesis, read, App. C, citing Chrissan & Fraser-Smith]. Noise amplitudes vary seasonally by up to a factor of 4 in some bands [P30, abstract].

**Receiving sensors:**

| Sensor | Noise floor | Source |
| --- | --- | --- |
| Stanford 1 Ω/1 mH loop design | below 1 fT/√Hz above 250 Hz | P6, abstract |
| AWESOME, 25 m² loop | about −10 dB-fT/√Hz (0.32 fT/√Hz) at 18–30 kHz; about 1 fT detectable in 1 Hz at ELF; measured in the field, so environmental coupling included | T12, read |
| LF AWESOME (0.5–470 kHz) | 10–25 dB better, as low as 0.03 fT/√Hz depending on frequency | P8, abstract |
| AWESOME electric-field receiver | 0.677 nV/(m·√Hz), about 0.0023 fT/√Hz equivalent | P9, abstract |
| Todmorden loop (2010) | 2.8 fT/√Hz at 9 kHz | G5 |

**Judgement [E]:**

- At 8–9 kHz the natural floor (about 10 fT/√Hz rms, 0.7–30 fT/√Hz after blanking) is above good loop floors (below 1 fT/√Hz; LF AWESOME down to 0.03 fT/√Hz at its best frequency), so a receiver is externally noise-limited.
- At 2–5 kHz the quietest natural floor (0.3–0.6 fT/√Hz) is comparable to a small loop (about 1 fT/√Hz). Only large loops or LF-AWESOME-class sensors stay externally limited. The 2010 Todmorden system, at 2.8 fT/√Hz, would have been receiver-limited in quiet 3–5 kHz conditions.
- Electric-field probes are intrinsically sensitive enough: 0.7 nV/m/√Hz against about 100 nV/m/√Hz natural minimum. In practice they are limited by local mains-field pickup, precipitation static and loss of effective height near trees and buildings; quantitative sources for those were not found.
- Sferic blanking moves the effective floor towards the receiver floor. After blanking, the receiver can matter even at 8–9 kHz.

### Near-field corrections at 10 km [E]

Short monopole on perfect ground, ground level: E_z ∝ |1 − j/x − 1/x²| and H_φ ∝ |1 − j/x|, with x = kd.

| f | kd | E (relative to far-field formula) | B | E/(cB) |
| ---: | ---: | ---: | ---: | ---: |
| 8.27 kHz | 1.73 | −1.1 dB | +1.2 dB | −2.3 dB |
| 2.97 kHz | 0.62 | +7.1 dB | +5.5 dB | +1.5 dB |

E-field and loop receivers therefore read different C/N0 at short range unless the noise transforms the same way. The noise arrives as far-field waves, so it does not.

### Worked link budgets at 8.27 kHz and 2.97 kHz [E]

**Assumptions** (every number below depends on them):

1. The quantity carried is e.m.r.p. Field is the vertical E at ground level. Up to 100 km Part A uses the short-monopole field over perfect ground, including near-field terms. At sub-9 kHz, ground-wave attenuation over even 10⁻⁴ S/m is negligible within 100 km (numerical distance about 0.04 at 100 km).
2. At 1,000 and 5,000 km Part A uses the single-mode formula given under "How the literature models", with unit excitation and no mode interference. Night: h = 87 km. Day: h = 70 km.
3. Attenuation at 8.27 kHz: 2 dB/Mm by night [G2], 5 dB/Mm by day (interpolated from P2), 8 dB/Mm by day east to west over poor ground (from P2 and [H]). At 2.97 kHz: QTEM mode with 10 or 20 dB/Mm by night and 40 dB/Mm by day (assumption, bracketed by [H] and Part A's back-calculation from G1).
4. C/N0 = E_s − E_n. This is the documentation set's convention for an externally noise-limited receiver, so the receiving antenna drops out. It holds at 8.27 kHz; at 2.97 kHz under minimum noise it needs a large loop.
5. Noise cases:
   - N1: about 0.7 fT/√Hz, implied by the EA4GHB report [E on G1].
   - N2: P.372 minimum (F_a ≈ 146 dB at 8.27 kHz, 126 dB at 2.97 kHz) [S1].
   - N3: about 10 fT/√Hz after blanking by day [G5] at 8.27 kHz; 1 fT/√Hz (assumed) at 2.97 kHz.
   - N4: 95 fT/√Hz, unblanked summer noise at 6.47 kHz on the transmitter bearing [G3], applied at 8.27 kHz; 10 fT/√Hz (assumed) at 2.97 kHz.
   - N5: P.372 maximum (F_a ≈ 174 dB at 8.27 kHz, within the ±2 dB graph reading of the noise table's 175 dB; 171 dB at 2.97 kHz) [S1].
6. Basic transmission loss is L_b = 142.0 + 20 log₁₀ f_MHz − E(1 kW e.m.r.p.). At these frequencies the isotropic reference aperture λ²/4π is about 10⁸ m², so L_b is small and even negative at 10 km and 2.97 kHz. This shows why "path loss" is a poor description below 9 kHz.

**8.27 kHz, per 1 mW e.m.r.p.** (add 10 log₁₀(P/1 mW) dB). N1 and N5 bound the C/N0 range.

| Range | E | E (dBµV/m) | B | L_b | C/N0: N1 / N2 / N3 / N4 / N5 (dB-Hz) |
| --- | ---: | ---: | ---: | ---: | --- |
| 10 km | 26.5 µV/m | 28.5 | 116 fT | 11.9 dB | 42.0 / 19.6 / 18.9 / −0.6 / −8.4 |
| 100 km | 3.0 µV/m | 9.5 | 10 fT | 30.8 dB | 23.0 / 0.7 / 0.0 / −19.6 / −27.3 |
| 1,000 km night | 523 nV/m | −5.6 | 1.74 fT | 46.0 dB | 7.9 / −14.5 / −15.2 / −34.7 / −42.5 |
| 1,000 km day | 460 nV/m | −6.7 | 1.53 fT | 47.1 dB | 6.8 / −15.6 / −16.3 / −35.8 / −43.6 |
| 1,000 km day, east to west, poor ground | 326 nV/m | −9.7 | 1.09 fT | 50.1 dB | 3.8 / −18.6 / −19.3 / −38.8 / −46.6 |
| 5,000 km night | 98 nV/m | −20.2 | 0.33 fT | 60.5 dB | −6.7 / −29.0 / −29.7 / −49.3 / −57.0 |
| 5,000 km day | 22 nV/m | −33.3 | 0.072 fT | 73.7 dB | −19.8 / −42.2 / −42.8 / −62.4 / −70.2 |
| 5,000 km day, east to west, poor ground | 3.9 nV/m | −48.3 | 0.013 fT | 88.7 dB | −34.8 / −57.2 / −57.8 / −77.4 / −85.2 |

**2.97 kHz, per 1 mW e.m.r.p.**

| Range | E | E (dBµV/m) | B | L_b | C/N0: N2 / N3 / N4 / N5 (dB-Hz) |
| --- | ---: | ---: | ---: | ---: | --- |
| 10 km | 67.6 µV/m | 36.6 | 189 fT | −5.1 dB | 56.6 / 47.1 / 27.1 / 11.6 |
| 100 km (±5 dB, near the QTEM transition) | 3.0 µV/m | 9.4 | 10 fT | 22.0 dB | 29.5 / 19.9 / −0.1 / −15.5 |
| 1,000 km night, 10 dB/Mm | 174 nV/m | −15.2 | 0.58 fT | 46.7 dB | 4.8 / −4.7 / −24.7 / −40.2 |
| 1,000 km night, 20 dB/Mm | 55 nV/m | −25.2 | 0.18 fT | 56.7 dB | −5.2 / −14.7 / −34.7 / −50.2 |
| 1,000 km day, 40 dB/Mm | 6.8 nV/m | −43.3 | 0.023 fT | 74.8 dB | −23.3 / −32.9 / −52.9 / −68.3 |
| 5,000 km night, 10 dB/Mm | 0.8 nV/m | −61.8 | 0.0027 fT | 93.2 dB | −41.7 / −51.3 / −71.3 / −86.7 |
| 5,000 km night, 20 dB/Mm | 2.6 pV/m | −111.8 | — | 143.2 dB | −92 or worse |
| 5,000 km day | — | — | — | — | closed |

**Configurations, scaled from Table B** (e.m.r.p. relative to 1 mW):

- 8.27 kHz:
  - 100 W into the 61 m T (S2), 2.0 mW (+3.0 dB): C/N0 at 1,000 km night is −11.5 dB-Hz with N2 (+11.0 with N1, −39.5 with N5). At 5,000 km night it is −26.0 with N2 (−3.7 with N1, −54.0 with N5); by day with N2 it is −39.2.
  - 1 kW into the umbrella (S2), 20 mW (+13.1 dB): −15.9 dB-Hz at 5,000 km night with N2, −43.9 with N5.
  - 100 W into a bare wire (S3), 0.114 mW (−9.4 dB): −23.9 dB-Hz at 1,000 km night with N2, −38.4 at 5,000 km.
- 2.97 kHz:
  - 1 kW into the umbrella (S1 or S2, voltage-limited), 0.92 mW (−0.4 dB): +4.4 dB-Hz at 1,000 km night with N2, −23.7 by day. At 5,000 km night with N2 it is −42.1; otherwise the path is closed.
  - 100 W into the T, 0.092 mW (−10.4 dB): −5.6 dB-Hz at 1,000 km night with N2, −52 at 5,000 km.

**Comparison with the modem's nominal case** (1 mW, 200 dB, −164 dBm/Hz, giving −36 dB-Hz).

- In the documentation set's convention, PathLoss_GUI = P_tx[dBm] + 164 − C/N0 = L_b + F_a − 13.5 + (P_tx − P_emrp in dB).
  - Example: 100 W into the 61 m T at 8.27 kHz, 5,000 km night, P.372-minimum noise. Here L_b = 60.5 dB, F_a ≈ 146 dB and the antenna loss is 47.0 dB, so PathLoss_GUI ≈ 240 dB and C/N0 = −26 dB-Hz.
- The nominal −36 dB-Hz equals "100 W at a GUI path loss of 250 dB". With that antenna it corresponds to:
  - 5,000 km by night with noise about 10 dB above the P.372 minimum, or
  - 5,000 km by day at the P.372 minimum (−39 dB-Hz, within 3 dB), or
  - 1,000 km by night at about 25 dB above the P.372 minimum.
- It is also close to the per-transmission C/N0 of real transatlantic amateur detections and decodes at 8.27–8.82 kHz (about −33 dB-Hz: VO1NA → DK7FC per transmission, and W4DEX's 8822 Hz carrier at 6,918 km); Part B's transatlantic records span a wider range (see "Contradictions").
- Read as a transmitter power, "1 mW" into a 61 m antenna would give an e.m.r.p. of order 10⁻⁸–10⁻⁷ W with a top-loaded antenna and modest losses (S1–S2), and down to about 4×10⁻¹⁰ W with a lossy bare wire (S4). The C/N0 values would then be 42–64 dB below the per-1-mW tables. The nominal case is therefore physically meaningful only if "1 mW" is read as e.m.r.p., or if the 200 dB absorbs the antenna loss.

### Implications for hours-long coherent integration and a 0.06 Hz signal radiated at audio frequency [E unless marked]

1. **Direct analogue exists.** VO1NA's December 2017 EbNaut 1-character transmissions (16K21A, 60 s symbols, CRC8; 9.07 h) have **95.4 min per information bit**, almost exactly the modem's 60 s chip / 96-min bit. They decoded single-shot at the first-in-country receivers at 2.4–5.1 Mm with Eb/N0 −0.1 to +4.7 dB, and at Todmorden (3.6 Mm) on 26–29 Dec 2017 with +11.4 to +13.0 dB [G13], i.e. C/N0 ≈ −24.6 to −37.7 dB-Hz from 10 µW ERP (R3) [E from G]. A 96-min bit needs C/N0 ≈ −36 dB-Hz for Eb/N0 ≈ +1.6 dB (10·log10(1/5760 s) = −37.6 dB). This lies inside the demonstrated envelope, but only with coding comparable to K=21, rate 1/16 plus list decoding. An uncoded or weakly coded 96-min bit would need several dB more.
2. **Coherence over hours is demonstrated, not guaranteed.** Phase was "constant" over a 9.7 h night (W4DEX, 6194 km) and repeatable to ≈±3° night-to-night (VO1NA→Todmorden). 22–28 h single transmissions decoded at 881 km. Against this, coherence was lost through Doppler spreading out of 46 µHz bins overnight (1027 km), disturbed propagation sometimes needed 2–4 phase segments, and W4DEX's phase shifted by 67–161° between nights at three receivers alike (a transmitter origin is inferred, not established). Evidence comes from a handful of mid-latitude, mostly winter-night paths [G].
3. **Frequency budget.** Coherent loss over T for a residual offset Δf is 20·log10|sinc(πΔfT)|. For a 96-min bit: 10 µHz → 0.05 dB, 100 µHz → 5.4 dB, 1 mHz → incoherent. For 6 h: 10 µHz → 0.7 dB; for 12 h: 10 µHz → 2.9 dB. At 8.27 kHz, 10 µHz is 1.2×10⁻⁹ fractional. A free-running crystal at 10⁻⁶–10⁻⁸ gives 8.3 mHz–83 µHz, so integration of more than about an hour requires GPS-disciplined clocks at both ends or explicit frequency tracking. This matches Nicholson's requirement of stability "of order 10uHz" for 12-h carriers (sub9khz 2014) [G15]. Reported drift of 43.2 mHz/h (kite TX) would move a 0.06 Hz-wide signal by about 70 % of its width per hour, and the 0.1 Hz worst-case crystal drift (G3XBM) by more than its width.
4. **Radiating directly at audio frequency** is exactly the amateur practice: soundcard/DDS → audio PA (TDA2003 5 W; Hafler P-3000; 100–500 W) → step-up transformer → loading coil or ground loop. The DAC sample clock is therefore the carrier reference, and 1 ppm of clock error becomes 8.3 mHz of carrier error. The receiver must timestamp against GPS PPS and correct ADC sample-rate drift continuously (vlfrx-tools, Spectrum Lab) [G].
5. **Timing.** Chip timing to ≤T/10 (6 s for 60 s chips; EbNaut rule) is trivial with GPS or NTP. Phase coherence, not timing, is the binding constraint.
6. **Mains placement.** Amateurs place carriers off n×50 Hz and the railway harmonics (n×16.7 Hz nominal [G25]; DK7FC gives n×16.6667 Hz [G3]) (e.g. 8270 Hz, 20 Hz above the 165th 50 Hz harmonic) [G]. [E] Mains frequency wander of ±0.05 Hz moves the 165th harmonic by about ±8 Hz, so a 0.06 Hz signal must sit at least tens of Hz from harmonic families; this inference is not validated.
7. **Blanking needs the wideband audio.** EbNaut blanks a 3 kHz band before narrowband processing, which a ≤3 kHz audio passband supports. Blanking after narrow filtering would forfeit the 6–23 dB gain [G].
8. **Message duration.** At 6,750 s per slot (5,760 s bits; [00](00-method-and-modem-context.md) §4.1), a 16-bit message occupies 30 h, and about 34 h with its two scored quiet slots: longer than the longest single amateur decodes (22–28 h) and short of multi-night stacking (63–254 h). Success then relies on the day/night phase behaviour noted in item 2.

## Moderate → adverse → worst-case parameter table

| Parameter | Moderate | Adverse | Worst case / outage |
| --- | --- | --- | --- |
| **Transmitting antenna and radiated power** | | | |
| E.m.r.p., 8.27 kHz, 100 W into a 61 m top-loaded antenna | 2.0 mW (T or umbrella, S2) [E] | 0.23–0.66 mW (S3–S4) [E] | 43 µW (bare wire, S4); icing detunes the antenna by tens of dB until retuned [E, P21] |
| E.m.r.p., 2.97 kHz, 100 W | 0.12 mW (umbrella, S2) [E] | 39 µW (T, S3) [E] | 2.2 µW (bare wire, S4) [E] |
| Hard ceiling, 61 m top-loaded | 5.5 mW at 8.27 kHz (29 kV, thin-wire T) [E, T3] | 55 mW (umbrella, 58 kV) [E] | about 160 mW at 100 kV, needing corona rings and dry insulators [E, T2]; wet weather lowers onset 28–46 % [T3] |
| Antenna efficiency, 8.27 kHz | −42 to −47 dB [E; T1 scaling] | −52 to −56 dB [E] | −64 dB [E]; amateur suburban reports imply −75 to −77 dB (e.m.r.p./P_tx) [G1, E] |
| Loaded Q; −3 dB bandwidth | 150–480; 6–60 Hz [E; T6 practice 33–70 Hz] | 50; 150–175 Hz [E] | 13–25; 360–620 Hz (lossy) [E] |
| Detuning ΔC/C, fixed T or umbrella | about 1–1.5×10⁻⁴ (1 m sideways wind bow): 1–4° [E] | 10⁻⁴–10⁻³ thermal and rain drift: 2–25° and up to 1 dB [E, assumption]; 1 m of extra sag: +4×10⁻⁴ (down-lead bows) to −4×10⁻³ (down-lead shortened), 4–65° and up to 7 dB [E, re-check] | +19 % from 5 mm ice: off resonance by about 8–9 % [E, P21] |
| Detuning, kite or balloon | about 0.5 % and −1 dB in h_e (75° → 60°) [E] | about 1.5 % and −2.7 dB (45°) [E]; 6.4–6.6 kHz (±1.5 %) resonance range reported [G3] | about 3.5 % and −5.8 dB (30°) [E]; resonance at 5.7 kHz instead of 6.5 kHz at very low kite angle [G3]; operation impossible above about 25 kt [T2] |
| Measured antenna variation at operating stations (LF/VLF, larger antennas) | ≈10 % change in gross (total input) resistance over one example day at an LF (60–80 kHz) site, possibly environmental; weather problems at such sites were diagnosed by correlating it with humidity and rainfall [T18] | mistuning of about an hour costing more than 1 dB (same example day) [T18] | ice raises C until the retuning range is exhausted and a single array stops transmitting; Cutler avoids the outage by transmitting on one of two arrays while de-icing the other (≈13 de-icing incidents per winter, 1986–1994, each of minutes to just over an hour) [T16] |
| Transmit ERP (sub-9 kHz), as reported (dipole-referenced where computed as 1.8·I²R_r; e.m.r.p. is then 2.6 dB lower) | 1–150 µW fixed amateur (an upper value of 300 µW was not confirmed on re-check) [G] | 40–300 nW QRP [G] | 58 mW kite record [G]; earth-mode ≈pW radiated [G] |
| **Propagation and received field** | | | |
| Field, 8.27 kHz, 1,000 km, per 1 mW e.m.r.p. | 523 nV/m (night, 2 dB/Mm) [E, G2] | 460 nV/m (day, 5 dB/Mm) [E, P2] | 326 nV/m (day, east to west, poor ground, 8 dB/Mm) [E, P2] |
| Field, 8.27 kHz, 5,000 km, per 1 mW | 98 nV/m (night) [E] | 22 nV/m (day) [E] | 3.9 nV/m (day, east to west) [E] |
| Field, 2.97 kHz, 1,000 km, per 1 mW | 174 nV/m (night, 10 dB/Mm) [E] | 55 nV/m (night, 20 dB/Mm) [E] | 6.8 nV/m (day, 40 dB/Mm) [E]; daytime may be effectively closed [H, ch. 10] |
| Field, 2.97 kHz, 5,000 km, per 1 mW | 0.8 nV/m (night, 10 dB/Mm) [E] | 2.6 pV/m (20 dB/Mm) [E] | about 0 (day) [E] |
| Signal amplitude variability | ±2 dB carrier spread over days (W4DEX) [G] | received field ×3 above and below its average over winter–spring 1968 on ≈320 km paths at 21 kHz (propagation and antenna not separated) [T17]; Eb/N0 −0.9 dB "improved to +2.5 dB during next three nights" (DL3JMM→Prayagraj, 5.17 kHz; whether by single-night decodes or by stacking is not stated; not confirmed) [G] | HAARP-type source: up to 20–30 dB day-to-day at a nearby site [P] (mechanism-specific) |
| **Noise and reception** | | | |
| Noise E_n, 8.27 kHz | 0.7 fT/√Hz (EA4GHB → Todmorden, Dec 2022) to ≈8 fT/√Hz (DL3JMM → Gulmarg, India, Nov 2022) after blanking, from reported fields and S/N [E on G1]; ≈1.3 fT/√Hz with a modelled field (VO1NA → DK7FC, Jan 2021) [E] | about 10–15 fT/√Hz by day and 24–32 fT/√Hz in the evening and night (8.97 kHz, October 2010, 1.2×-mean blanker) [G5]; P.372 minimum rms about 10 [S1] | 95 fT/√Hz (6.47 kHz, 1 Aug 2010, unblanked, on the transmitter bearing; applied here to 8.27 kHz) [G3] to 260 fT/√Hz (P.372 maximum) [S1] |
| Noise E_n, 2.97 kHz | 0.33 fT/√Hz (P.372 minimum) [S1] | 1–10 fT/√Hz (assumed) [E] | 59 fT/√Hz (P.372 maximum) [S1] |
| Sferic blanking gain / fraction blanked | ≈20 dB at 8 kHz / 25–35 % (standard EbNaut setting, "normal VLF conditions") [G] | ≈6–8 dB / 60 % (aggressive, quiet day) [G] | summer background ≈95 fT/√Hz (≈3× winter) before blanking [G]; blanker periodicity artefacts below 10 mHz [G] |
| Receiving sensor against noise | loop below 1 fT/√Hz above 250 Hz [P6]; LF AWESOME 10–25 dB better, down to 0.03 fT/√Hz at its best frequency (value at 3–9 kHz not given in the abstract) [P8]; externally limited at 8–9 kHz [T12, E] | small loop marginal at 3–5 kHz in quiet conditions [E] | 2.8 fT/√Hz loop is receiver-limited at 3–5 kHz [G5, E] |
| **C/N0 and link performance** | | | |
| C/N0, 100 W into 61 m T (2 mW), 8.27 kHz, 5,000 km | −3.7 dB-Hz (night, quiet blanked noise) [E] | −26 dB-Hz (night, P.372 minimum) [E] | −54 dB-Hz (night, P.372 maximum); −39 dB-Hz by day with P.372 minimum [E] |
| C/N0, 1 kW into 61 m umbrella (0.92 mW), 2.97 kHz, 1,000 km | +4.4 dB-Hz (night, P.372 minimum) [E] | −5.1 to −15 dB-Hz (night, 20 dB/Mm or 1 fT/√Hz) [E] | −24 dB-Hz (day) to −41 dB-Hz (night, P.372 maximum) [E] |
| Received C/N0 at successful decode, amateur fixed stations (ERP nW to sub-mW), 0.8–7.5 Mm, after blanking | −15 to −25 dB-Hz (DK7FC→Todmorden 881 km; W4DEX 6194 km; DL3JMM→Gulmarg) [E from G] | −25 to −35 dB-Hz (VO1NA→Todmorden 3.6 Mm, −24.6 to −28.7; VO1NA→DL4YHF −32.9; DK7FC→RC4HAA −33.4; W1VD single transmission −34.0) [E from G] | −35 to −47 dB-Hz: single transmissions decoded down to about −38 dB-Hz (VO1NA→IK1QFK −36.0, →SQ5BPF −36.6, →N4VLF −37.7; DK7FC→RN3AUS −36.2), and below that only with 2–18 nights of stacking (VO1NA→Hawley −46.7; W4DEX→DL0AO −41.8; DK7FC→RN3AUS −41.5) [E from G]; no decode or detection reported below ≈ −47 dB-Hz |
| Strongest far-field sub-9 kHz | ≈ −4 to −8 dB-Hz, kite (mW class; ERP not stated for this test) at 859 km (2010) [E from G] | – | – |
| ELF strategic (76/82 Hz) | ZEVS +4.1 dB-Hz at 3.2 Mm (January 1990 field against the 1985–1994 January median 80 Hz noise) [P] | −7.8 dB-Hz at 16.5 Mm (same estimator) [P] | −11.4 dB-Hz at 18.3 Mm (same estimator); not detected above the background noise at Stanford (8.1 Mm; ≤ +1.5 dB in 2 min estimated) [P] |
| Eb/N0 at successful decode (coded, list 5×10⁴–10⁶) | +2 to +6 dB | −0.5 to +1 dB (success 84–87 % at −0.5 dB in the index table) [G] | −1.0 to −1.9 dB (63–74 % success at −1.0 dB in the index table; −1.9 with 6-night stack) [G]; Shannon −1.59 dB (a single short-block decode below that asymptotic limit can occur by chance, or lie within the dB-level uncertainty of the post-decode estimate [E]) |
| Symbol (code-bit) error rate in decoded messages | ≈21 % (Eb/N0 +1.7 dB) [G] | 33–39 % [G] | 42.5 % (rank 14 143; DK7FC/P → SQ5BPF, 2.97 kHz) [G]; 45.1 % (rank 118 919) was reached only at 17.47 kHz (DK7FC → Tasmania, 16,805 km) [G] |
| Achieved information rate (per transmission-hour) | 40–70 info bits/h (881 km, fixed DK7FC) [E from G]; ≈250 bits/h at 254 km (W4DEX → N4VLF, ≈ −10 dB-Hz) [E from G] | 0.8–25 bits/h (transatlantic) [E from G] | 0.5–0.6 bits/h (1 char in 9–12 h); 5.7 bits over 63 h stacked [E from G]; ELF 0.03 bit/s (108 bits/h, 1972 test) [T] |
| **Phase, frequency and timing** | | | |
| Phase stability within one transmission | constant reference phase for ≈10 h at night (W4DEX 42 chars) [G] | 2- or 4-segment phase search needed ("disturbed propagation", "oscillator problems") [G] | Doppler spreading out of 46 µHz bins from 21:00–09:00 (S/N 5.2→1.2, partly from rising noise) [G] |
| Night-to-night phase repeatability | within ≈6° over 3 nights (VO1NA→Todmorden 3576 km) [G] | tens of degrees at weak-signal sites [G] | 67–161° night-to-night shifts common to three receivers (W4DEX; a transmitter-side origin is inferred, not established) [G, E] |
| Required frequency accuracy (relative TX–RX) for 6–12 h coherence | ≲1 µHz (GPS-locked; <0.1 dB loss) [E] | 10 µHz (−0.7 dB over 6 h, −2.9 dB over 12 h) [E; Nicholson's "of order 10uHz" G] | ≥100 µHz (AA5BW-class) → −23 dB over 6 h; 43 mHz/h kite drift or 0.1 Hz crystal drift → incoherent [E/G] |
| Timing accuracy | 30 ns RMS (Todmorden) [G] | ≤T/10 (EbNaut requirement; ≤1.9 dB worst-case loss) [G/E] | unrecorded start times; soundcard overruns/DMA gaps padded [G] |

The worst-case column combines limits from different sources and links; Part A notes that nobody has reported observing them together. The rows on e.m.r.p., ceilings, efficiency, Q, detuning, field, noise level, sensors and modelled C/N0 are Part A's; the rows on reported ERP, amplitude variability, blanking, achieved C/N0, ELF, Eb/N0, error rates, information rate, phase, frequency and timing are Part B's. The row on measured antenna variation at operating stations was added on re-check.

## How the literature models or simulates this

### Antenna and transmitter models

- **Lumped short-antenna model.** C, h_e and R_r = 160π²(h_e/λ)² [T2]. It is valid when kh ≪ 1, i.e. at all sub-9 kHz frequencies for 61 m. It is the basis of the NRL proof-of-performance field-strength method (inverse-distance fitting at known distances, normalised to 100 km) [T4, T5].
  - The NRL authors note that the measured "antenna current" may include loss currents. Panel-meter reading errors of ±5 A changed h_e by 1–2 m [T4, read].
  - Known failure: the current at the measurement point is not the antenna current. Winch capacitance took 3.4 A of 40 A in Dazey & Koons [T2, read].
- **Thin-wire models.**
  - Z0 = 60[ln(h/a) − 1] with X = Z0 cot kh [T2]. Part A's electrostatic method of moments: about 120–300 uniform-charge segments, image ground, Gaussian elimination in pure Python, h_e from the charge centroid. It reproduced the 1,220 m balloon cable's capacitance within 5–11 % and its measured calm h_e within 5 % [E].
  - NEC4 is the Navy practice for top-load gradient checks, but "a large number of segments is needed to obtain the desired resolution in the charge data" [T9, abstract].
  - Thin-wire models give wire-end and tip gradients poorly; corona design needs finer models or measured gradients. A charge-simulation method for the near field of VLF transmitting antennas agrees with CST and FEKO to about 2.2 % on average and, by its abstract, with measured data; it finds the highest field at the top of the antenna (Ren et al. 2024 [P51, abstract]).
- **Ground-loss models.** Annular-ring integration of H-field and E-field losses using soil admittance Y_s and wire-screen admittance (Abbott) [T1, read]. Valid for homogeneous soil; real sites need measurement.
- **Corona models.** Peek's formula (valid 0.01–10 cm, uses 30 kV/cm) against Watt & Hansen's revised fit for smooth dry wire, E_onset = G0·(1 + a/(d·Dr)^b) kV/cm crest (eq. 5-6), with G0 = 24.3 kV/cm, a = 0.717, b = 0.423, d the wire diameter in cm and Dr the relative air density, plus frequency, stranding, wetness and cage corrections. Corona power is from Ryan & Henline [T3, read]. Validated at 18–57 kHz; extrapolation below 9 kHz is uncertain, although the frequency correction is only "a few percent" there [T3].
- **Earth-electrode antennas.** Wheeler's loop-height formula (δ/√8 with image) [P1]; it corresponds to h_eq = k0·l·δ/√8, half the HED surface-impedance value l·√(ωε0/σ) used in this chapter [E]. The chapter's value follows from the wave tilt (a receiving grounded wire on the surface picks up Δ·E_z·l; reciprocity) and is the HED-to-VED ratio Δ·cos φ stated under "Earth-electrode (ground) dipoles"; Wheeler's own 1 MHz example (0.0014) is 6 dB below it. The two are therefore not equivalent, and the 6 dB is carried as model uncertainty on all ground-dipole e.m.r.p. values. Layered or anisotropic effective conductivity is measured with H/I or wave-impedance methods [P28].
  - Known limits: real electrodes, near-electrode current crowding, layered earth, and non-uniform current along long wires. Wheeler lists end-connection loss and current variation as ignored [P1, read].
- **Electrically small antenna limits.** Chu / McLean Q_min [P10, P22]; Thiele's far-field formulation about 10 times higher [P10]; the bandwidth–efficiency product [P10]. Pfeiffer (2017, IEEE TAP 65, 1642) treats efficiency limits for small metallic antennas [metadata only, not read].

### Propagation and field strength

- **Propagation to the receiver (used in Part A's link budgets, [E]).**
  - Near field: exact short-dipole field over perfect ground.
  - 100 km: inverse-distance field with the near-field factor. Part A's ray estimate gives first-hop sky waves within about ±1–3 dB at 8.27 kHz. At 2.97 kHz the single QTEM mode alone is 4.8 dB below inverse distance, so the uncertainty is ±5 dB.
  - 1,000–5,000 km: single-mode waveguide formula E = 9.487·√(P_emrp·λ/(R_E sin(d/R_E)))·10^(−α·d_Mm/20)/h. Units: V/m, P in W, λ, R_E and h in m, α in dB/Mm, d_Mm in Mm. For QTEM (n = 0) the Neumann factor halves it.
  - Part A derived this formula from a parallel-plate guide (n ≥ 1 modes, perfect walls, unit excitation). It is identical to Nicholson's rendering of Wait (1958), with P his "ERP" and λ ≈ 33.4 km [G2, read; P29 not read]. His "ERP" must be e.m.r.p. for the identity to hold.
  - Attenuation rates: Taylor (1960) measured about 7–9 dB/Mm at 6 kHz and 1–3 dB/Mm above 10 kHz by day, with west-to-east 3 dB/Mm lower than east-to-west below 8 kHz [P2, read]. Wait & Spies find the daytime mode-1 minimum near 18 kHz and the nighttime minimum near 12 kHz, with west-to-east loss lower [T11, read, conclusions].
  - The 2.97 kHz rates (night 10–20, day 40 dB/Mm) are Part A's assumption, bracketed by chapter 10's model (night 7–28, day 20–66 dB/Mm) [H] and by Part A's back-calculation from DK7FC's 840 km reception (8–27 dB/Mm for post-blanking noise of about 5 to 0.8 fT/√Hz, and 36 dB/Mm at the P.372 minimum, re-computed on re-check with 0.19 mW e.m.r.p. and the night QTEM formula; about 7 dB/Mm lower with Wheeler's ground-dipole height) [E on G1].
  - These formulas ignore mode interference (±1–6 dB within about 1–1.5 Mm at night), land/sea, polarisation and geomagnetic direction, and day/night terminators.
- **Nicholson's single-mode cavity model as practitioners use it (Part B).** The abelian 9 kHz page uses a single-mode cavity model after Wait (1958): B = 1.0e−6·√(ERP·5.23e−6/sin(r/6371e3))·exp(−r/a)/h. Here a = 2.9e6 m (day) or 4.3e6 m (night), and h = 70 km (day) or 95 km (night); this gives about 3 dB/Mm by day and 2 dB/Mm by night, with a day/night crossover near 2700 km. The page lists its own defects: land/sea differences, magnetic-azimuth dependence, no vertical pattern, no multi-mode interference — "Tests at 9kHz", abelian.org/vlf/9k/ [G2, read, cached copy; P29 secondary]. The 2010 8.97 kHz kite reception at 859 km (2.0–3.0 fT) agreed with this model at about 3 dB/Mm (same page) [G2]. The 6.47 kHz reception in summer 2010 was 2–3× weaker than the ERP estimate predicted, which the authors attributed to marginal wind, unrecorded timing and calibration error — abelian.org/vlf/ss100801/ [G3, read].
- **Bannister ELF formula (Part B):** 20·log Hφ ≈ −131.8 + 20·log E − αρ − 10·log(a·sin(ρ/a)) for WTF/MTF, with +10 dB for ZEVS, night α ≈ 0.5–1.0 dB/Mm, day 1.3 dB/Mm and an antipodal focusing term. Validated against 1990 82 Hz data at 3.2–18.3 Mm ("excellent" agreement), FS&B 1998 [P32].

### Noise and noise processing

- **Noise.** ITU-R P.372 Fa curves and E_n conversion [S1]; measured spectra and blanking statistics [G3–G5, T12].
- **Noise-processor design from recorded noise**: the Sanguine processor was designed and evaluated by simulation on worldwide wideband 3–300 Hz recordings, giving 7–20 dB over linear processing (Evans & Griffiths 1974 [P33, abstract]). WOLF thresholds were obtained by mixing recorded or synthetic noise with attenuated signals (WOLF page [G18]).
- **Statistical significance of spectral peaks**: Rayleigh-bin significance with an empirically observed high tail after blanking (mv101009 [G5]). Gaussian/Rayleigh assumptions overstate significance at sub-10 mHz resolution if blanker periodicities exist.

### Link performance and decoding

- **EbNaut signal calculator and sphere-packing limit** (abelian.org/ebnaut/calc.php; appendix) [G10]. It predicts Eb/N0 from carrier S/N and code settings; for the W4DEX 42-char test it predicted +0.15 dB against +0.1 dB measured (msg00380 [G12]). It is valid only for AWGN after blanking with known phase, and it assumes the operator searches phase.
- **Real-channel replay.** Recorded unmodulated carriers from actual paths are fed to the decoder with different code, length and symbol settings. Because the cascaded code is linear, carrier performance represents any message (notes [G10]). This is the strongest method in the amateur literature: it carries real phase wander, amplitude variation and post-blanking noise. Its limit is that it is path- and night-specific.
- **Monte-Carlo code testing**: message error rates for 72-bit messages plus CRC16 against the sphere-packing limit, computed over hundreds of CPU-hours on rented AWS machines (results and notes pages [G10]). Gaussian noise only.

### Software

- Part A did not find a tool that simulates a sub-9 kHz transmitting-antenna transfer function with weather-driven detuning. NEC-class codes give static impedances only.

## What a high-fidelity simulation would need

Options from both parts, each with limits. Neither part presents a definitive design, and other approaches may be better (item 9).

1. **Transmitter–antenna block, a lumped high-Q resonator.**
   - Inputs: C, L, R_r and R_loss(f), plus a voltage clip at V_max (corona) that converts excess drive into a hard ceiling on e.m.r.p.
   - Time variation: C(t) and h_e(t) driven by wind (kites and balloons: percent-level changes over seconds to minutes; fixed T: about 10⁻⁴ for a sideways bow, 4×10⁻⁴–4×10⁻³ for 1 m of extra sag), temperature (slow, 10⁻⁴–10⁻³), rain (insulator leakage and higher tanδ, i.e. more R_loss and a lower corona limit) and icing (step-like ΔC/C of tens of percent with lossy ice near its Debye peak).
   - Observables to validate: antenna current amplitude and phase logs, and resonance tracking. DK7FC's kite logs and Dazey & Koons's h_e against wind are the only quantitative anchors found for lofted antennas. For fixed antennas the only anchors are operational records from larger LF/VLF stations: a 10 % swing in gross antenna resistance and an hour-long mistuning of more than 1 dB on one example day at an LF site [T18], and de-icing about 13 times a winter to keep ice from exhausting the retuning range [T16].
   - Limit: no measured spectra of wind-induced ΔC/C for fixed sub-9 kHz antennas were found, so the time scales are assumptions.
2. **Retuning-loop model.** A servo (variometer or switched inductance) with dead-band and time constant, or ETAM/BEST-style synchronous tuning [T6, T7]. WWVB's computer-driven variometer servo on the voltage–current phase is an operational example [G23]. The residual phase error becomes a slow phase-modulation term on the radiated field. Choose between ideal retuning, periodic retuning (staircase phase) and none (detuning drift). Validation: transmitter-side current-phase records.
3. **E.m.r.p. bookkeeping and convention.** Carry e.m.r.p. explicitly and convert amateur e.r.p. computed as 1.8·I²R_r by −2.6 dB (field-fitted ERPs may already be e.m.r.p.). For earth dipoles, include the cos φ azimuth pattern, an effective σ that can differ by tens of percent with azimuth [P28], and the 6 dB model uncertainty in h_eq between Wheeler's height and this chapter's.
4. **Signal level and field model by range.**
   - About 50 km and closer: exact near field. E and B scale differently, so the simulated sensor type matters.
   - 50–500 km: mode sum or ray model with ionospheric reflection coefficients.
   - Beyond 500 km: a mode model with day/night h and α, terminator crossings and mode interference.
   - Options: the single-mode formula, which is cheap; at 8.27 kHz it agreed within about 2 dB in the one case where both field and power were reported (F5VLB; 4.3 dB with Wheeler's ground-dipole height), gave a plausible implied post-blanking noise floor for VO1NA → DK7FC (1.3 fT/√Hz; no field was measured), and missed EA4GHB by 11–21 dB (5–15 dB with Wheeler's height). DL3JMM's 194 nV/m at Gulmarg has not been compared with a prediction. Full-wave or mode codes (LWPC is designed for higher VLF; validity at 2–9 kHz not verified here). Replay of measured amateur carrier-phase and amplitude records [G1].
   - Validation: the amateur C/N0 and field table above, and Nicholson's flux-density reports [G3–G5].
   - Signal-level statistics (Part B): draw C/N0 from the empirical envelope (−15 to −47 dB-Hz at decode for nW–sub-mW ERP at 0.8–7.5 Mm; per-W normalisation +7 to +33 dB-Hz), with night/day and seasonal (≈4–9 dB, W4DEX summer vs winter) differences and a ±2 dB carrier spread over days (W4DEX) [G10]. Limit: very few paths, and receivers biased to quiet, well-equipped sites.
5. **Phase process** (Part B): three components. (i) A slow propagation phase that is nearly constant over a night (≈10 h), repeatable to ≈±3° night-to-night on a strong path. (ii) Day/night transitions, where segment phase splitting may be needed. (iii) Nocturnal Doppler episodes with an equivalent constant frequency offset of about 20–30 µHz over a 6-h segment (inferred from the 4–6 dB signal loss in a 46 µHz bin) [E]. Add night-to-night phase shifts common to all receivers (W4DEX; a transmitter origin is inferred) and receiver timing gaps (padded over without reported loss in the one account found [G13]). Limit: the amateur sources give only snapshots; the sub-hour phase power spectrum was not found.
6. **Noise.** P.372 rms levels and measured post-blanking levels differ by up to 10–25 dB [S1, G3]. A simulator must state which it represents, or model impulsive noise plus a blanker. Receiver sensor noise is needed near 3–5 kHz, where the natural minimum approaches loop floors [P6, P8, T12].
   - Noise-input options (Part B): (a) replay wideband (≥3 kHz) real VLF recordings, seasonal and diurnal, through the modem's own blanker; or (b) a parametric impulsive model (e.g. Class-A/B or Hall) fitted to post-blanking Gaussianity and to blanked fractions of 25–60 %. Limit: (b) will not reproduce blanker-periodicity artefacts or bearing-dependent noise unless explicitly modelled. Validate against the gain table in D3 and EbNaut's "approximately Gaussian after blanking".
7. **Equipment details.**
   - Coil self-capacitance (tens of pF, 2–10 % of the tuning capacitance) and its temperature coefficient.
   - Transmitter keying transients through the high-Q load. ETAM requires phase-continuous waveforms; time constant Q_L/(πf) of 0.5–53 ms (6–50 ms for Q_L ≥ 150), irrelevant at 60 s chips but relevant to any fast keying [T6].
   - GPS-locked versus free-running transmit frequency: a 43.2 mHz/h drift had to be de-chirped in one amateur analysis [G4].
   - Part B's list: TX and RX frequency error and drift (GPSDO ≲10⁻¹¹ vs free-running TCXO 10⁻⁶–10⁻⁸ → 8 mHz–80 µHz at 8.27 kHz), DAC/ADC sample-clock ppm error mapping 1:1 onto the audio-frequency carrier, timestamp jitter, buffer overruns, antenna detuning (AM/PM on kite or wet antennas), outages (wind/rain/fuel), mains (n×50/60 Hz) and 16.7 Hz railway harmonic lines near the carriers.
8. **Observables to validate against** (Part B): reported Eb/N0 at decode versus code and length; symbol error rates of 21–42.5 % (45 % at 17.47 kHz); list ranks; carrier S/N versus bin width (1/T scaling up to the coherence limit); phase constancy over a night; night-to-night phase tables (msg00450 [G13]); blanker fraction and gain; success-rate curves (63–87 % at −1.0/−0.5 dB).
9. **Better approaches may exist.** For example, a co-simulation of a NEC-class geometry with weather time series, or empirical system identification from logged antenna current and received phase at a nearby monitor.

## Comparison with the modem's presets

This chapter bears on the presets through transmitter and receiver frequency stability, antenna detuning and the level convention; propagation Doppler and delay are treated in [10](10-sub9khz-propagation.md). The HF, Aurora and EME presets concern other bands and are not compared here. In the presets the Doppler value bounds the offset and wander of a two-path family whose direct path never fades ([00](00-method-and-modem-context.md) §5).

| Preset or convention | Value | What this chapter's evidence shows |
| --- | --- | --- |
| None | 0 Hz, 0 ms | Closest to the links that decoded. With GPS-locked frequency at both ends, carriers were integrated coherently in 2.3–2.9 µHz bins over 4–5 days, which needs TX–RX frequency agreement well below 1 µHz; the nocturnal path Doppler inferred on one 1,027 km path was about 20–30 µHz as an equivalent constant offset over 6 h [G, E]. |
| Sub9 best | 0.001 Hz, 0.005 ms | 1 mHz is about 30–1,000 times the offsets and wander that the GPS-locked links met. It is reached by an unsynchronised oscillator of about 10⁻⁷ (0.8 mHz at 8.27 kHz), and by kite detuning: a 50° swing spread over 10–600 s is an excursion of 0.2–14 mHz [E]. |
| Sub9 moderate | 0.005 Hz, 0.05 ms | Corresponds to a free-running crystal near 6×10⁻⁷ or to fast kite-angle changes. No EbNaut decode in the sources ran with wander of this size; on-off and frequency-shift modes such as QRSS3 were copied over a few kilometres with up to 0.1 Hz drift [G, E]. |
| Sub9 worst | 0.05 Hz, 5 ms | Comparable to G3XBM's 0.1 Hz worst-case crystal-divider drift and to the 43.2 mHz/h kite drift accumulated over one 96-min bit (≈69 mHz). Such signals were de-chirped, or were read only in 11–45 mHz bins over a few kilometres [G, E]. Two equal paths 5 ms apart decorrelate only across about 100 Hz, so at 0.06 Hz the delay acts as a phase term ([00](00-method-and-modem-context.md) §4.1). |
| Antenna response (in no preset) | — | A resonant 61 m antenna adds a constant group delay Q_L/(πf) of about 0.5–53 ms, comparable in size to the Sub9 worst delay but a single deterministic path. Through detuning it adds a slow phase and amplitude modulation: 1–4° for a 1 m sideways wind bow of a fixed T, about 4–65° and up to 7 dB for 1 m of extra sag, about 25° and up to 1 dB for a 10⁻³ thermal or rain drift, 48–53° and 3.5–4.4 dB if a kite's resonance swings across the reported ±1.5 % tuning range, and loss of resonance under ice. One example day at an LF station shows an hour-long mistuning of more than 1 dB [E, T]. |
| GUI level: 1 mW, 200 dB against −164 dBm/Hz (−36 dB-Hz) | F_a = 10 dB | Natural noise at 8.27 kHz has F_a ≈ 146–175 dB, so N0 = −28 to +1 dBm/Hz, 136–165 dB above the default [S]. By [00](00-method-and-modem-context.md) §3.1, −36 dB-Hz is 1 mW e.m.r.p. with L_b + F_a ≈ 213.5 dB: at 8.27 kHz and P.372-minimum noise, a 5,000 km path between the night (−29 dB-Hz) and day (−42 dB-Hz) cases, or 100 W into the 61 m T (S2) at 5,000 km by night with noise 10 dB above the minimum. Read as transmitter power, 1 mW would be 42–64 dB weaker. Decodes over 0.8–7.5 Mm were reported at −15 to −47 dB-Hz, and VO1NA's 95-minute-per-bit decodes at about −25 to −38 dB-Hz bracket the row-1 −36 dB-Hz; the 32 dB-Hz planning target would need about 1–5 W ERP even at 0.6–1.4 Mm [E, G]. |

Against the links that succeeded, the Sub9 Doppler values are generous stresses on oscillator and antenna wander. The presets omit the deterministic high-Q antenna response and its weather-driven phase and amplitude modulation, and their delay values produce little selectivity across the modem's 0.06 Hz DATA band but still allow complex gain changes and cancellation.

## Contradictions, open questions, and things not found

### Where the two parts differ

- **Eb/N0 to C/N0 conversion (resolved on re-check).** Part A took one information bit as n code symbols (T_b = n·T_s for rate 1/n) and used C/N0 = Eb/N0 − 10 log₁₀(T_b) [E]. Part B used EbNaut's T_eff = N_stack·(6NC + CRC + K − 1)·n·T_s with 5.7 bits per character, which reproduced EbNaut's reported "S/N in 1 Hz" within 0.2 dB in four cases [E]. The EbNaut notes state that the reported Eb/N0 is computed from the carrier S/N with the NC × 5.7 information bits and the overall code rate of the cascaded codes [G10], and the "S/N 7.5 dB in the 30.6 uHz bandwidth" that msg00450 gives for a VO1NA decode at Eb/N0 −0.1 dB is reproduced by T_eff (−37.7 dB-Hz both ways) [G13; E]. Part B's conversion is therefore the right one. Part A's overstated C/N0 by 0.7 dB (50 characters) to about 4 dB (4–7 characters); its four affected values (DK7FC/P → Todmorden, DL3JMM → Gulmarg and → Tirunelveli, VO1NA → DK7FC) have been recomputed, and the DK7FC/P → Todmorden decode is now −21.7 dB-Hz in both parts.
- **Envelope of amateur C/N0.** After that correction Part A's eight links span −21.6 to −41.6 dB-Hz over 0.4–7.5 Mm (its −24.2 and −41.6 dB-Hz values are carrier detections). Part B gives −15 to −47 dB-Hz at decode across its larger compilation, averaging stacked decodes over all stacked transmissions (VO1NA → Hawley, −46.7 dB-Hz). The ranges now differ only in link set. Part A also characterises real transatlantic detections and decodes at 8.27 kHz as about −33 dB-Hz per transmission; Part B's transatlantic decodes span about −21.6 dB-Hz (W4DEX → Todmorden winter EbNaut decodes, −21.6 to −26.9 dB-Hz) to −41.8 dB-Hz (W4DEX → DL0AO, 7,258 km, six nights stacked); the lowest east-to-west value is −37.7 dB-Hz (DK7FC → W1VD at 8270.1 Hz, three nights stacked).
- **The 24.6 bit/h information rate (resolved on re-check).** Both attributions are correct: the figure occurs twice. The milestone list gives 24.6 bits/h for W4DEX's 42-character message of 2017-12-26 (239.4 information bits in 9.73 h) [G1], which Part A used to confirm that "16K" and "8K" denote rates 1/16 and 1/8. Nicholson's post of 4 Jan 2015 gives 24.6 bits/h at "80% of the channel capacity" for W4DEX's 25-character test of 1 January 2015 [G11]. [E] That figure counts the 166 payload-plus-CRC bits over the 6.76 h message, whereas the EbNaut notes' worked example of the same message counts its 142.5 information bits and gives 21.1 bits/h at 70 % [G10].
- **Balloon-lofted facility below 9 kHz (resolved on re-check).** Part B, which had only a search summary of [P3] and [T2], cites 36.6 A base current at 13.275 kHz and could not establish whether the facility operated below 9 kHz. [T2] settles both points. Its lowest operating frequency was 6.6 kHz, where 100 W was radiated (voltage-limited). Its current monitor reads the antenna base current plus the winch capacitance current, so the typical 40 A at the monitor (84 kV, 13.275 kHz) is 36.6 A at the antenna base, the value used for the 1,400 W and 940 W measurements.
- **Access to [P3] and [P5].** Part A had the abstracts of Koons & Dazey (1983) and of Raghuram et al. (1974), and reports from the latter a calculated efficiency above 2 % over more than a 2:1 frequency range. Part B found both abstracts elided by the publisher and lists Raghuram et al. as not obtained.
- **Range of compact transmitters.** Part A found demonstrated ranges of at most about 120 m [P18] and, searching 2023–2024 specifically, no kilometre-range field tests. Part B cites about 210 m underwater for an ultracompact ELF magneto-mechanical antenna [P43, secondary; confirmed in the abstract on re-check]. Neither part found kilometre-scale tests.
- **The 6.47 kHz kite test.** Part A's antenna model reproduces the reported 11 mW e.r.p. (10.2 mW). Part B reports that the received field was 2–3× weaker than the ERP-based prediction, which the authors attributed to marginal wind, unrecorded timing and calibration error [G3]. Part A also notes that entering amateur e.r.p. into Nicholson's formula, which is algebraically in e.m.r.p., overstates the field by 2.6 dB. [E] That convention difference would account for 2.6 dB of the 6.0–9.5 dB (2–3× in amplitude) shortfall; the rest is not reconciled.

### Open questions and conflicts in the sources

- **E.r.p. against e.m.r.p.** (Part A) Amateur "ERP" figures computed from antenna current and R_r are dipole-referenced [G7; consistent with G3]; ERPs fitted to received fields, such as W4DEX's LWPC value, may not be [E]. Nicholson's field formula takes "ERP" but is algebraically the e.m.r.p. single-mode formula [E on G2]. Using amateur e.r.p. in it overstates field by 2.6 dB. This is within the ±50 % amplitude uncertainty quoted in G4 and G5, but systematic.
- **ERP and field-strength mismatches.** Part A: EA4GHB's field is 11–21 dB below Part A's ground-dipole prediction (5–15 dB with Wheeler's height), and DK1IS's reported e.r.p. is 7–12 dB below Part A's model of its 13 m T. Both are grey-literature single points, and the cause (site conductivity, current measurement, shielding by trees and buildings, or ERP estimation method) is unknown. Part B: the 6.47 kHz kite field was 2–3× below the ERP-based prediction. W4DEX's 150 µW (LWPC fit to measured field) implies h_e ≈ 7.7 m for a 29 m antenna [E]. In the other direction, DF6NM's 5 µW carriers of October 2010 were received 5–13 dB above the single-mode prediction for that ERP [G5]. At a larger scale, the two measured 91 m umbrellas at 21 kHz gave fields 7 and 17 dB below their design prediction [T17]. Quoted ERPs therefore carry several dB of uncertainty, and C/N0-per-W normalisations inherit it.
- **Ground-dipole effective height** (re-check). Wheeler (1961) gives a loop height δ/√8, which corresponds to half the monopole-equivalent height that this chapter derives from the surface impedance and the wave tilt, and that the effective depth W ≈ δ/√2 of FS&B's ELF moment implies [P1, P32; E]. A receiving-side derivation reproduces this chapter's value, so the factor 2 appears to lie in Wheeler's result, but it is not resolved here: the 6 dB in e.m.r.p. is carried as model uncertainty, and with Wheeler's height the F5VLB check worsens from 1.7 to 4.3 dB while the EA4GHB discrepancy shrinks to 5–15 dB.
- **Todmorden noise, 2010 against 2022** (re-check). G5's blanked October 2010 levels (10–15 fT/√Hz by day, 24–32 fT/√Hz in the evening and night) are 14–46 times the 0.7 fT/√Hz implied by EA4GHB's December 2022 report at the same site [G5, G1; E]. The sources differ in year, receiver, blanker and estimator, and the difference is not reconciled; a simulation that draws noise from both would span about 30 dB.
- **Seasonal noise variation** (re-check). P.372 notes very little seasonal variation below 10 kHz [S1], whereas [P30] reports seasonal amplitude changes of up to a factor of 4 in some bands, and a 2016–2019 campaign in China [P47, abstract] reports up to 10 dB in 3–30 kHz bands, with the CCIR model overestimating the local level.
- **Independent cross-check calculation ([90](90-cross-check-calculations.md) §2), compared here as another unvalidated estimate and not cited as a source:**
  - C (340 pF bare; 1 nF top-loaded), h_e (30.5 m; 50 m), R_r and the inverse-distance field at 100 km agree with Part A's values within 0.3 dB (R_r), 2 % (h_e) and 11 % (C). Part A's method-of-moments values: 340 pF and 29.8 m; 902–999 pF and 49.8–51.2 m.
  - Loss model: [90] uses coil Q = 300 plus 60 Ω, with no dielectric loss. That is comparable to Part A's S2, so its top-loaded efficiency and Q at 8.27 kHz (−46 dB, Q = 155) match Part A's T (−47 dB, Q = 145). Its bare-wire efficiencies (−53 dB at 8.27 kHz, −66 dB at 2.97 kHz) are about 2 dB better than Part A's S2 because Part A adds the dielectric term X_C·tanδ, which is largest for the high-reactance bare wire [E].
  - The material difference is voltage. [90]'s 1 kW cases imply 55–207 kV, e.g. 114 kV on a bare thin wire at 8.27 kHz and 207 kV at 1 kHz. All exceed corona onset on thin wire (17–29 kV), and four of the five exceed the about 100 kV practical maximum [T2, T3]. Its 100 W bare-wire cases (36 kV at 8.27 kHz, 65 kV at 2.97 kHz) also exceed thin-wire onset.
  - With Part A's voltage limits, the 1 kW e.m.r.p. falls to 0.24 mW (bare) or 5.5 mW (T) at 8.27 kHz, instead of 4.5 and 24.2 mW. At 2.97 kHz it is 4 µW against 250 µW for the bare wire, and 92 µW against 1.62 mW for the T. At 1 kHz the T gives 0.56–14 µW (20–100 kV) against 74 µW. These are 6–18 dB lower at 8.27 and 2.97 kHz and 7–21 dB lower at 1 kHz.
  - [90] itself now notes the cap: at 30 kV it gives 0.31 mW for the bare wire and 7.3 mW for the top-loaded case at 8.27 kHz. That is consistent with Part A's 0.24 mW (27 kV) and 5.5 mW (29 kV, 902 pF, h_e 49.8 m) once voltage, C and h_e are aligned [E].
  - [90]'s "radiated power" is numerically the e.m.r.p., as it states; its inverse-distance field at 100 km is acceptable within about ±3–5 dB.
- **Daytime attenuation at 2.97 kHz** (Part A) is unresolved. Taylor's measured values below about 4–5 kHz are flagged by him as affected by abrupt changes [P2, read]. Mode theory gives several tens of dB/Mm [H, ch. 10]. No amateur daytime 2.97 kHz reception beyond about 1 Mm was found in G1.
- **VO1NA ERP** (Part B): 10 µW (milestone list, Nicholson [G1]) versus "10 mW" (ARRL 2021 item and RAC repost [G16]). Part B follows 10 µW.
- **Regulatory boundary** (re-check): sub-9 kHz operation in Germany started from a Bundesnetzagentur indication of "free" operation below 9 kHz [G2], whereas the 2023 operating guide gives 8.3 kHz as the limit below which operation is unregulated in most countries [G24]. The difference is not explained in the sources read.
- **US ELF radiated power** (Part B): "eight-watt" dual-site (Navy fact sheet 2003 [T14]) versus "<1 W" for the 1972 WTF test (MIT LL [T13]). A search summary attributes "about 2 W" to secondary sources; Part B did not read them.
- **MIT LL distance** (Part B): "decoded more than 6000 km from the source", but the stated submarine position is ≈4600 km from Clam Lake [E].
- **Siple** (Part B): transmitter 100 kW versus 150 kW; antenna 21.2 km versus crossed 21 km versus later 39–42 km (secondary). Primary antenna-efficiency figures (Raghuram et al. 1974 [P5]) were not obtained; the abstract is elided by the publisher (Part A had the abstract; see above).
- **ZEVS** (Part B): power, modulation and coding are not documented in peer-reviewed sources Part B read. The ALFLAB [G20] MSK and "message waiting" description is grey. Location is near Murmansk per Velikhov 1996 (via FS&B [P32]), while a 2023 paper is said (search summary) to place current operation near the White Sea coast — unresolved. On re-check, [P44]'s abstract places the transmitter "on the Kola Peninsula", and neither [P44]'s nor [P45]'s abstract mentions the White Sea.
- **China WEM** (Part B): the only technical article found is the 2022 *Engineering* overview [P34]. Part B reported it as marked retracted; on re-check no retraction or correction notice was found (the page's banners are hidden templates). It gives no data rates, and its Table 1 of signal-strength tests was not read. **India**: no technical parameters; it is unclear whether the 2014 facility is ELF or VLF.
- **EbNaut appendix** (resolved on re-check): the Internet Archive copy of the appendix gives the timing-loss formula as 20 log10(1 − 2|t|/T) for 0 ≤ |t| < T/2; abelian.org itself did not resolve.
- **Stacking method** (Part B): whether multi-night "coherent stacking" re-aligns phase per transmission or relies on repeatable phase is not documented in the sources read. W4DEX's night-to-night phase shifts, common to all receivers, suggest re-alignment or tolerant decoding.
- **Season** (Part B): blanking was said to make narrowband reception "equally well independently of season" (one 2010 test), yet W4DEX's summer-night carrier normalises ≈4–9 dB below the winter EbNaut results on the same path. The two observations are not reconciled.
- **Single reception vs. repeatable link** (Part B): most milestone entries are single successes, and the list does not record attempts that failed. Success-rate data exist only from EbNaut simulations (index page) and a few repeated-path reports. Inferring link availability from the list would overstate reliability.
- **Mechanical antennas** (Part A). The more than eight orders of magnitude advantage is theoretical [P13]; a 2026 limit analysis places demonstrated mechanical emitters near the fundamental figure of merit for their volume and bandwidth, questioning further orders-of-magnitude gains [P48, abstract]. Demonstrated ranges (at most about 120 m [P18] in Part A; about 210 m underwater in Part B) are compared above. Kemp's efficiency is calculated, not measured in the far field [P10].

### Not obtained or not found

Part A:

- Watt, *VLF Radio Engineering* (1967): only metadata [B1].
- Belrose's chapter "VLF, LF and MF Antennas" in *Handbook of Antenna Design*, vol. 2 (1983): only metadata [B2].
- Burrows, *ELF Communications Antennas* (1978): not located in Crossref; it is cited in [P32]'s reference list. Burrows's 1974 IEEE Trans. Commun. papers were verified as metadata only [B3, P31].
- RSGB *LF Today*: not obtained.
- DK7FC's own web pages: server "not available" [G9]. Part B did not find DK7FC's fixed-station antenna and currents for the 2017–2018 records, or the DK7FC kite ERP for the Oct 2010 tests. The 2014 sub9khz news quotes DK7FC transmitting with 3 W on "the 70m long 30m high antenna" at 5.76 kHz [G15]; whether that is the antenna of the 2017–2018 records is not stated in the sources read.
- DTIC and MDPI direct downloads were refused for automated clients. DTIC reports were read through archive.org mirrors and MDPI papers through Europe PMC.
- navy-radio.virhistory.com, which hosts the NPS 1967 VLF antenna document and a copy of the Dazey & Koons report, did not resolve. Part B read two documents hosted at www.navy-radio.com ([T13], [T14]). On re-check the host still did not resolve, but an Internet Archive copy of its Cutler icing report was read [T16].
- Wheeler (1947), Chu (1948) and Hansen (1981) were verified as citations only.
- The suggested DOI 10.1109/TAP.2022.3142360 for Thanalakshme et al. could not be resolved. The paper is 10.1109/TAP.2021.3137244 (IEEE TAP 70(5), 3710–3722, 2022).
- QEX, RadCom and DUBUS articles, and NOSC VLF antenna handbooks: not searched or not found. Part B read one RadCom article (G3XBM, Oct 2011 [G14]) but found no QEX/RadCom/DUBUS EbNaut articles.
- No quantitative source on vegetation or tree loss at sub-9 kHz, on wind-induced capacitance spectra of fixed VLF antennas, or on E-field probe degradation near trees was found. Those parts are assumptions. A bounded re-check search found only operational records from larger LF/VLF stations (Navy LF antenna monitoring [T18], Cutler icing [T16], WWVB automatic retuning [G23]) and no measured ΔC/C for a fixed 50–100 m antenna below 9 kHz.
- Measured efficiencies of real 50–100 m-class top-loaded antennas were found only at 20–60 kHz (Ray's scale model [T1]; the Auburn 91 m umbrellas [T17]). Below 9 kHz there are only amateur estimates, and no 2015–2026 measurement of a tower or wire antenna of this size was found.

Part B:

- Not found: any peer-reviewed analysis of amateur sub-9 kHz results; operational US/Russian ELF receiver integration times or reliability statistics; long-range HAARP data-link results (a 2022 EISCAT heater experiment gives a BER–Eb/N0 relation but, in its abstract, no receiver distance [P49]); a sub-hour phase-noise or Doppler spectrum for sub-9 kHz paths; post-2025-03 milestone entries (the latest archived copy of the list, of August 2025, ends at 2025-03-14; a mirror at i2phd.org is identical to it). The list's entries of 5–6 June 2024 record "final" messages into Todmorden ("BYE", "TU PAUL"), so later receptions would rely on other receivers. The groups.io posts linked from recent entries were not obtained.
- Not obtained: Helliwell & Katsufrakis (1974) [P42]; Raghuram et al. (1974) [P5] (Part A had its abstract; see above).

## References

Grades: S standard, P peer-reviewed (P-conf: conference paper), T report or thesis, B book, G grey, H in-house, E own estimate; combined grades such as T/G are as assigned by Part B. Access: read, abstract, metadata, secondary or not obtained; where both parts used a source, each part's access is given. Keys [P1]–[P31], [T1]–[T12], [B1]–[B3] and [G1]–[G9] are Part A's; sources cited only by Part B have keys [P32]–[P45], [T13]–[T15] and [G10]–[G22]. Keys [P46], [T16]–[T18] and [G23] were added on re-check; [P47]–[P51], [G24] and [G25] on a further re-check.

**Standards**

- [S1] ITU-R Recommendation P.372-18 (09/2026), *Radio noise*. Geneva: ITU. https://www.itu.int/rec/R-REC-P.372. Read: §1.2.2 eq. (7); Part 2 text; Fig. 1 read from the graph. Re-read on re-check: the ITU page lists P.372-18 (09/2026) as in force, and the Fig. 1 values used here were confirmed within the stated ±2 dB. [S]

**Peer-reviewed papers**

- [P1] H. A. Wheeler, "Useful radiation from an underground antenna," *J. Res. NBS* 65D(1), 89–91, 1961. Read (OCR); eq. (6) and worked example; re-read on a further re-check (NIST PDF), including the reference loop "just above a perfect ground plane" whose image "doubles the far field". [P]
- [P2] W. L. Taylor, "Daytime attenuation rates in the very low frequency band using atmospherics," *J. Res. NBS* 64D(4), 349ff., 1960. Read (abstract and text, OCR). [P]
- [P3] H. C. Koons, M. H. Dazey, "High-power VLF transmitter facility utilizing a balloon lofted antenna," *IEEE Trans. Antennas Propag.* 31(2), 243–248, 1983, doi:10.1109/TAP.1983.1143044. Abstract (Part A); secondary, search summary only, abstract elided by the publisher (Part B). [P]
- [P4] M. H. Dazey, H. C. Koons, "Characteristics of a power line used as a VLF antenna," *Radio Sci.* 17(3), 589–597, 1982, doi:10.1029/RS017i003p00589. Abstract (both parts). [P]
- [P5] R. Raghuram, R. L. Smith, T. F. Bell, "VLF Antarctic antenna: impedance and efficiency," *IEEE Trans. Antennas Propag.* 22(2), 334–338, 1974, doi:10.1109/TAP.1974.1140777. Abstract (Part A); not obtained, bibliographic only, abstract elided by the publisher (Part B). [P]
- [P6] S. K. Harriman, E. W. Paschal, U. S. Inan, "Magnetic sensor design for femtotesla low-frequency signals," *IEEE Trans. Geosci. Remote Sens.* 48(1), 396–402, 2010, doi:10.1109/TGRS.2009.2027694. Abstract. [P]
- [P7] M. B. Cohen, U. S. Inan, E. W. Paschal, "Sensitive broadband ELF/VLF radio reception with the AWESOME instrument," *IEEE TGRS* 48(1), 3–17, 2010, doi:10.1109/TGRS.2009.2028334. Abstract (no numbers in the abstract). [P]
- [P8] M. B. Cohen et al., "Broadband longwave radio remote sensing instrumentation," *Rev. Sci. Instrum.* 89, 094501, 2018, doi:10.1063/1.5041419. Abstract. [P]
- [P9] Gurses, Whitmore, Cohen, "Ultra-sensitive broadband 'AWESOME' electric field receiver for nanovolt low-frequency signals," *Rev. Sci. Instrum.* 92(2), 024704, 2021, doi:10.1063/5.0031491. Abstract (Crossref record): 0.677 nV/(m·√Hz), near-DC to 470 kHz. [P]
- [P10] M. A. Kemp et al., "A high Q piezoelectric resonator as a portable VLF transmitter," *Nat. Commun.* 10, 1715, 2019, doi:10.1038/s41467-019-09680-2. Read (PMC6461683). [P]
- [P11] Srinivas Prasad M. N., R. U. Tok, F. Fereidoony, Y. E. Wang et al., "Magnetic pendulum arrays for efficient ULF transmission," *Sci. Rep.* 9, 13220, 2019, doi:10.1038/s41598-019-49341-4. Read (partial). [P]
- [P12] H. C. Burch, A. Garraud, M. F. Mitchell, R. C. Moore, D. P. Arnold, "Experimental generation of ELF radio signals using a rotating magnet," *IEEE TAP* 66(11), 6265–6272, 2018, doi:10.1109/TAP.2018.2869205. Abstract. [P]
- [P13] J. A. Bickford, A. Duwel, M. S. Weinberg, R. S. McNabb, D. K. Freeman, P. A. Ward, "Performance of electrically small conventional and mechanical antennas," *IEEE TAP* 67(4), 2209–2223, 2019, doi:10.1109/TAP.2019.2893329. Abstract. [P]
- [P14] O. C. Fawole, M. Tabib-Azar, "An electromechanically modulated permanent magnet antenna for wireless communication in harsh electromagnetic environments," *IEEE TAP* 65(12), 6927–6936, 2017, doi:10.1109/TAP.2017.2761555. Abstract. [P]
- [P15] H. Rezaei, V. Khilkevich, S. Yong, D. S. Stutts, D. Pommerenke, "Mechanical magnetic field generator for communication in the ULF range," *IEEE TAP* 68(3), 2332ff., 2020, doi:10.1109/TAP.2019.2955069. Read (NSF PAR 10213263, partial). [P]
- [P16] J. S. Glickstein et al., "Power-efficient ELF wireless communications using electro-mechanical transmitters," *IEEE Access* 8, 2455–2471, 2020, doi:10.1109/ACCESS.2019.2961708. Read (partial). [P]
- [P17] R. P. Thanalakshme et al., "Magneto-mechanical transmitters for ultralow frequency near-field data transfer," *IEEE TAP* 70(5), 3710–3722, 2022, doi:10.1109/TAP.2021.3137244. Abstract. [P]
- [P18] C. Dong et al., "A portable very low frequency (VLF) communication system based on acoustically actuated magnetoelectric antennas," *IEEE AWPL* 19(3), 398–402, 2020, doi:10.1109/LAWP.2020.2968604. Abstract. [P]
- [P19] T. A. Miś, J. Modelski, "Risk assessment and experimental light-balloon deployment of a stratospheric vertical VLF transmitter," *Sensors* 23(3), 1073, 2023, doi:10.3390/s23031073. Read (PMC9921905, partial). [P]
- [P20] T. A. Miś, J. Modelski, "In-flight electromagnetic compatibility of airborne vertical VLF antennas," *Sensors* 22(14), 5302, 2022, doi:10.3390/s22145302. Read (PMC9319893, partial). [P]
- [P21] R. P. Auty, R. H. Cole, "Dielectric properties of ice and solid D2O," *J. Chem. Phys.* 20, 1309–1314, 1952, doi:10.1063/1.1700726. Abstract. [P]
- [P22] J. S. McLean, "A re-examination of the fundamental limits on the radiation Q of electrically small antennas," *IEEE TAP* 44(5), 672ff., 1996, doi:10.1109/8.496253. Abstract; formula read via P10. [P]
- [P23] L. J. Chu, "Physical limitations of omni-directional antennas," *J. Appl. Phys.* 19, 1163–1175, 1948, doi:10.1063/1.1715038. Metadata. [P]
- [P24] H. A. Wheeler, "Fundamental limitations of small antennas," *Proc. IRE* 35, 1479–1484, 1947, doi:10.1109/JRPROC.1947.226199. Metadata. [P]
- [P25] R. C. Hansen, "Fundamental limitations in antennas," *Proc. IEEE* 69(2), 170–182, 1981, doi:10.1109/PROC.1981.11950. Metadata. [P]
- [P26] S. L. Bernstein et al., "Long-range communications at extremely low frequencies," *Proc. IEEE* 62(3), 292–312, 1974, doi:10.1109/PROC.1974.9426. Abstract (Part A); abstract via search only (Part B). [P]
- [P27] P. R. Bannister, "ELF propagation update," *IEEE J. Oceanic Eng.* 9(3), 179–188, 1984, doi:10.1109/JOE.1984.1145609. Abstract. This is part of the 1984 JOE ELF issue (see also doi:10.1109/JOE.1984.1145610, 1145625). [P]
- [P28] P. R. Bannister, "Summary of the Wisconsin Test Facility effective earth conductivity measurements," *Radio Sci.* 11(4), 405–411, 1976, doi:10.1029/RS011i004p00405. Abstract. [P]
- [P29] J. R. Wait, "A study of VLF field strength data: both old and new," *Geofis. Pura Appl.* 41, 73–85, 1958, doi:10.1007/BF01981861. Metadata; used through G2 (Part A). Secondary, via the abelian pages [G2] (Part B). [P]
- [P30] D. A. Chrissan, A. C. Fraser-Smith, "Seasonal variations of globally measured ELF/VLF radio noise," *Radio Sci.* 31(5), 1141–1152, 1996, doi:10.1029/96RS01930. Abstract. [P]
- [P31] M. L. Burrows, *IEEE Trans. Commun.* 22(4), 1974: "Ground anisotropy and ELF transmitting antennas" (402–404); "Bared-end ground for an insulated buried antenna cable" (404–408); "Optimizing the current distribution in a buried linear antenna" (409–411). Metadata. This is the April 1974 ELF special issue, introduced by J. R. Wait (353–354). [P]
- [P32] A. C. Fraser-Smith, P. R. Bannister, "Reception of ELF signals at antipodal distances," *Radio Sci.* 33(1), 83–88, 1998, doi:10.1029/97RS01948. Author copy https://www.quakefinder.com/research/EQTdata/82Hz.pdf. Cited in Part B's text as FS&B. Read (Part B); re-read on a further re-check (noise reference of Table 1, sources of the attenuation rates, reference list). [P]
- [P33] J. E. Evans, A. S. Griffiths, "Design of a Sanguine noise processor based upon world-wide ELF recordings," *IEEE Trans. Commun.* 22(4), 528–539, 1974, doi:10.1109/TCOM.1974.1092215. Abstract via search summary (Part B). [P]
- [P34] J. Lu, X. Zhuo, Y. Liu, G. Zhao, Q. Di, "The Extremely Low Frequency Engineering Project for Underground Exploration," *Engineering* 10, 13–20, 2022, doi:10.1016/j.eng.2021.12.003, https://www.engineering.org.cn/engi/EN/10.1016/j.eng.2021.12.003. Read via fetch summary (Part B); read on re-check from the publisher HTML (Table 1 not read). Part B recorded it as marked retracted; on re-check the page's retraction banners are hidden templates with blank dates and links, the publisher's retraction-status service returns no notice, and Crossref records no update or retraction (13 Nov 2026). [P]
- [P35] R. C. Moore, U. S. Inan, T. F. Bell, E. J. Kennedy, "ELF waves generated by modulated HF heating of the auroral electrojet and observed at a ground distance of ∼4400 km," *J. Geophys. Res.* 112, A05309, 2007, doi:10.1029/2006JA012063. Abstract (Part B). [P]
- [P36] M. B. Cohen, M. Gołkowski, "100 days of ELF/VLF generation via HF heating with HAARP," *J. Geophys. Res. Space Physics* 118, 6597–6607, 2013, doi:10.1002/jgra.50558. Abstract (Part B). [P]
- [P37] M. B. Cohen, U. S. Inan, M. Gołkowski, M. J. McCarrick, "ELF/VLF wave generation via ionospheric HF heating: Experimental comparison of amplitude modulation, beam painting, and geometric modulation," *J. Geophys. Res.* 115, A02302, 2010, doi:10.1029/2009JA014410. Abstract (Part B). [P]
- [P38] T. A. Miś, M. Gołkowski, R. C. Moore, J. Modelski, "FSK radio communication below 3 kHz using HAARP ionospheric excitation," 4th URSI AT-RASC, Gran Canaria, 2024, https://www.ursi.org/proceedings/procAT24/papers/0746.pdf. Read (Part B). [P-conf]
- [P39] G. Jin, M. Spasojevic, M. B. Cohen, U. S. Inan, "Utilizing nonlinear ELF generation in modulated ionospheric heating experiments for communication applications," *Radio Sci.* 48, 61–68, 2013, doi:10.1002/rds.20014. Secondary, via [P38] (Part B). [P]
- [P40] D. L. Carpenter, T. F. Bell, A. J. Smith, "The Siple VLF transmitter as a multi-frequency probe of the Earth-ionosphere waveguide," *J. Atmos. Terr. Phys.* 50(2), 105–115, 1988, doi:10.1016/0021-9169(88)90048-7. Abstract (NORA) (Part B). [P]
- [P41] R. A. Helliwell, "VLF wave stimulation experiments in the magnetosphere from Siple Station, Antarctica," *Rev. Geophys.* 26(3), 551–578, 1988, doi:10.1029/RG026i003p00551. Abstract (Part B). [P]
- [P42] R. A. Helliwell, J. P. Katsufrakis, "VLF wave injection into the magnetosphere from Siple Station, Antarctica," *J. Geophys. Res.* 79(16), 2511–2518, 1974, doi:10.1029/JA079i016p02511. Not obtained (Part B). [P]
- [P43] S. Yang, J. Geng, H. Zhou, … R. Jin, "Long-range EM communication underwater with ultracompact ELF magneto-mechanical antenna," *IEEE Trans. Antennas Propag.* 71(3), 2082–2097, 2023, doi:10.1109/TAP.2022.3227799. Secondary, via search summary and [P38] (Part B); on re-check, abstract (OpenAlex record of the DOI) and bibliographic data (Crossref). The arXiv number given earlier, 2210.06296, belongs to a different paper. [P]
- [P44] V. A. Pilipenko, M. Parrot, E. N. Fedorov, N. G. Mazur, "Electromagnetic field in the upper ionosphere from ELF ground-based transmitter," *J. Geophys. Res. Space Physics* 124(10), 8066–8080, 2019, doi:10.1029/2019JA026929. Secondary, search summaries only (Part B); abstract (Crossref record) on re-check. [P]
- [P45] E. N. Fedorov, N. G. Mazur, V. A. Pilipenko, V. V. Vakhnina, "Generation of artificial ULF/ELF electromagnetic emission in the ionosphere by horizontal ground-based current system," *J. Geophys. Res. Space Physics* 128(12), e2023JA031590, 2023, doi:10.1029/2023JA031590. Secondary, search summaries only (Part B); abstract (Crossref record) on re-check. [P]
- [P46] E. C. Field, L. R. Kies, P. R. Bannister, R. F. Ingram, W. D. Hopkins, M. A. Roberts, "An aerostat-supported ELF/VLF transmitter," *Radio Sci.* 24(2), 235–246, 1989, doi:10.1029/RS024i002p00235. Abstract (Crossref record). [P]
- [P47] X. Gu, G. Li, H. Pang, S. Wang, B. Ni, F. Luo, R. Peng, L. Chen, "Statistical analysis of very low frequency atmospheric noise caused by the global lightning using ground-based observations in China," *J. Geophys. Res. Space Physics* 126(6), e2020JA029101, 2021, doi:10.1029/2020JA029101. Abstract (Crossref record). [P]
- [P48] D. Latypov, "Naturally resonant emitters: Approaching fundamental antenna limits," *J. Appl. Phys.* 139(18), 184501, 2026, doi:10.1063/5.0335626; preprint arXiv:2604.26980. Abstract (Crossref record; arXiv listing). [P]
- [P49] J. Yang, Q. Li, H. Lu, S. Hao, J. Chen, Y. Yan, J. Wu, T. Xu, "Experimental ELF/VLF wave communication with excitation by ionosphere modulated heating," *Radio Sci.* 57(3), e2021RS007406, 2022, doi:10.1029/2021RS007406. Abstract (Crossref record). [P]
- [P50] A. Wang, X. Li, H. Zhang, J. Tian, W. Zhou, "Structural parameters optimization of corona ring of tethered balloon-type VLF antenna," *Int. J. Antennas Propag.* 2021, 5517978, doi:10.1155/2021/5517978. Abstract (Crossref record). [P]
- [P51] B.-Y. Ren, X.-Q. Li, Y.-C. Qin, H. Du, L.-C. Fei, R. Huang, "Novel numerical analysis of electric fields in the near field of very-low-frequency antennas based on the charge simulation method," *IET Microw. Antennas Propag.* 18(8), 618–628, 2024, doi:10.1049/mia2.12488. Abstract (Crossref record). [P]

**Reports and theses**

- [T1] H. A. Ray Jr. (Continental Electronics), *Dispersible Transmitting Antenna VLF/LF Investigation*, RADC, 1972, DTIC AD0740618. Read via archive.org DTIC_AD0740618 (OCR). [T]
- [T2] M. H. Dazey, H. C. Koons, *High-Power Transportable VLF Transmitter Facility*, Aerospace Corp. SD-TR-82-31, 25 May 1982, DTIC ADA117419. Read via archive.org DTIC_ADA117419 (Part A); secondary, via search summary only (Part B); re-read on re-check (lowest frequency and radiated powers, Table 1 currents, wind effect on h_e). [T]
- [T3] A. D. Watt, P. M. Hansen, *VLF/LF Corona Investigation*, NCCOSC RDT&E Division Technical Document 2266, May 1992, DTIC ADA258052. Read via archive.org. [T]
- [T4] Naval Research Laboratory, *Radiation Parameters of the VLF Transmitting Station NPM, Lualualei, Hawaii*, 1972, DTIC AD0748177. Read (partial, archive.org). [T]
- [T5] Naval Research Laboratory, *Antenna Parameters of the U.S. Navy VLF Transmitting Station NWC, Harold E. Holt, Exmouth, Australia*, 1972, DTIC AD0743884. Read (partial). [T]
- [T6] RADC, *Wideband Coherent Communication at VLF with the Experimental Transmitting Antenna Modulator (ETAM)*, 1973, DTIC AD0916687. Read (partial; Table I). [T]
- [T7] *Bandwidth Efficient Synchronous Tuning (BEST) for Improved VLF/LF Communications*, 1989, DTIC ADA213949. Abstract (archive.org metadata). [T]
- [T8] *VLF-LF Wide Shift FSK Antenna Feed Networks*, 1972, DTIC AD0746119. Abstract. [T]
- [T9] *NEC4 Analysis of a Navy VLF Antenna*, 1994, DTIC ADA279340. Abstract. [T]
- [T10] *Horizontal VLF Transmitting Antennas Near the Earth*, 1967, DTIC AD0653851. Abstract. [T]
- [T11] J. R. Wait, K. P. Spies, *Characteristics of the Earth–Ionosphere Waveguide for VLF Radio Waves*, NBS Technical Note 300, 1964. Read (partial, conclusions §; OCR). [T]
- [T12] M. B. Cohen, *ELF/VLF Phased Array Generation via Frequency-Matched Steering of a Continuous HF Ionospheric Heating Beam*, PhD dissertation, Stanford University, 2009, Appendix C. Read (local copy; no URL recorded); not re-confirmed on re-check. [T]
- [T13] MIT Lincoln Laboratory, "ELF Communications Development" (chapter extract, "Long-Range Terrestrial Communications", pp. 62–63, of a Laboratory history), https://www.navy-radio.com/commsta/elf/elf-lincoln-labs.pdf. Cited in Part B's text as MIT LL. Read (Part B). [T]
- [T14] NCTAMS LANT / U.S. Navy, "Extremely Low Frequency Transmitter Site, Clam Lake, Wisconsin" (ELF Clam Lake Fact Sheet, c.2003), https://www.navy-radio.com/commsta/elf/clam_lake_elf2003-1.pdf. Cited in Part B's text as the Navy fact sheet. Read (Part B). [T/G]
- [T15] J. Katsufrakis, "Magnetospheric and ionospheric studies at Siple Station during the austral summer of 1969–1970," *Antarctic Journal of the United States* 5(4), 115, 1970. Read (local copy; URL not recorded) (Part B); not re-confirmed on re-check. [T]
- [T16] P. Hansen, *VLF Cutler Hollow Core Cable Repair/Replacement*, NRaD (NCCOSC RDT&E Division) Technical Report 1681, September 1994. Read via the Internet Archive copy of https://www.navy-radio.virhistory.com/commsta/cutler/cutler-tr1681.pdf (sections on de-icing and de-icing records). [T]
- [T17] E. R. Graf (project leader), VLF Transmission Laboratory, Auburn University, *A Phase Stability Study System for VLF Propagation over Short Distances*, Technical Report No. 1, NASA contract NAS8-20154, 26 July 1968, NTRS 19680021824, https://ntrs.nasa.gov/citations/19680021824. Read (OCR; §IV and Table 6); re-read on a further re-check from the text layer of the NTRS PDF, in which Table 6 and eqs. (93)–(96) are legible. [T]
- [T18] S. C. Tietsworth, *A New System for Measurement of Low Frequency Radio Transmitting Antenna Parameters in Near Real Time*, Naval Ocean Systems Center, San Diego, June 1991, DTIC ADA238555. Read via archive.org DTIC_ADA238555 (OCR); the DTIC copy refused automated access. [T]

**Books**

- [B1] A. D. Watt, *VLF Radio Engineering*, Pergamon, 1967, doi prefix 10.1016/B978-0-08-012313-4. Metadata; not obtained. [B]
- [B2] J. S. Belrose, "VLF, LF and MF antennas," in *Handbook of Antenna Design*, vol. 2, Peter Peregrinus/IEE, 1983, pp. 553–662, doi:10.1049/PBEW015G_ch15. Metadata; not obtained. [B]
- [B3] M. L. Burrows, *ELF Communications Antennas*, Peter Peregrinus, Stevenage, 1978. First a lead from memory; on re-check found cited in [P32]'s reference list. Not obtained. [B]

**Grey literature**

- [G1] P. Nicholson (compiler), "Some recent milestones with amateur radio experiments at VLF," http://abelian.org/vlf/amateur-radio/; date list http://abelian.org/vlf/amateur-radio/list-date.html; CSV http://abelian.org/vlf/amateur-radio/vlf-milestones.csv. Amateur reports; cited in Part B's text as the milestone list or "the list". Read, latest entry 2025-03-14 (Part A); read from a cached copy (Part B); re-checked entry by entry against the Internet Archive copy of 17 Aug 2025 (CSV and date list), abelian.org not resolving. On a further re-check the CSV mirrored at https://www.i2phd.org/abelian/abelian.org/vlf/amateur-radio/vlf-milestones.csv was byte-identical to that copy. [G]
- [G2] P. Nicholson, "Tests at 9kHz," http://abelian.org/vlf/9k/. Read; propagation formula citing Wait (1958) (Part A). Read from a cached copy (Part B). Re-checked against the Internet Archive copy of Sep 2023. [G]
- [G3] P. Nicholson / S. Schäfer DK7FC, "6kHz Test, 2010-08-01," http://abelian.org/vlf/ss100801/. Read (Part A); read from a cached copy (Part B). Re-checked against the Internet Archive copy of Feb 2025. [G]
- [G4] P. Nicholson, "9kHz Test, 2010-10-02," http://abelian.org/vlf/ss101002/. Read (Part A); read from a cached copy (Part B). Re-checked against the Internet Archive copy of Jan 2018. [G]
- [G5] P. Nicholson, "9kHz Tests, 2010-10-09 to 2010-10-15" (DF6NM), http://abelian.org/vlf/mv101009/. Read (Part A); read from a cached copy (Part B). Re-checked against the Internet Archive copy of Jun 2021. [G]
- [G6] "9kHz Test, 2010-03-21" reception summary (listed by Part B under P. Nicholson), http://abelian.org/vlf/ss100321/. Read (Part A); read from a cached copy (Part B). Re-checked against the Internet Archive copy of Sep 2023. [G]
- [G7] VE7SL, "The Joys of ERP," https://www.amateurradio.com/the-joys-of-erp-2/. Abstract (search snippet); read on re-check: written for the 2200 m and 630 m bands, it gives TRP × 1.82 for ERP and × 3 for EIRP, and the sloping-hat rule is W5EST's, relayed in a reader's comment. [G]
- [G8] SLAC news release, 12 April 2019, "SLAC develops novel compact antenna for communicating where radios fail," https://www6.slac.stanford.edu/news/2019-04-12-slac-develops-novel-compact-antenna-communicating-where-radios-fail. Abstract (search snippet). [G]
- [G9] S. Schäfer DK7FC, VLF pages, http://www.iup.uni-heidelberg.de/schaefer_vlf/. Not obtained (site unavailable). [G]
- [G10] P. Nicholson, "EbNaut Coherent BPSK": index http://abelian.org/ebnaut/; technical notes http://abelian.org/ebnaut/notes.shtml; polynomial tables http://abelian.org/ebnaut/tables.shtml; appendix http://abelian.org/ebnaut/appendix.shtml; calculator http://abelian.org/ebnaut/calc.php. Cited in Part B's text as the EbNaut index, notes, tables, appendix or calculator. Read from cached copies; appendix truncated (Part B). Index, notes and appendix re-read from Internet Archive copies of 2024–2025, in which the appendix is complete. [G]
- [G11] P. Nicholson, "VLF: Transatlantic messages at 8822Hz," rsgb_lf_group, 4 Jan 2015, https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2015-01/msg00095.html. Read (Part B); re-read on re-check. [G]
- [G12] P. Nicholson, "Re: LF: 8269 kHz EbNaut tonight," rsgb_lf_group, 26 Dec 2017, https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2017-12/msg00380.html. Read (Part B); re-read on re-check. [G]
- [G13] P. Nicholson, "Re: LF: VLF NA Ebnaut," rsgb_lf_group, 29 Dec 2017, https://klubnl.pl/rsgb_lf_group-archives/html/rsgb_lf_group/2017-12/msg00450.html. Read (Part B); re-read on re-check. [G]
- [G14] R. Lapthorn (G3XBM), "VLF through the ground experiments," *RadCom*, Oct 2011, pp. 54–56, https://mikrocontroller.net/attachment/548225/VLF_p54_55_56_RadComOctober11__1_.pdf. Read (Part B). On re-check the host presented a bot check, so the claims were re-checked against the copy obtained by the research pass. [G]
- [G15] G3XBM, "G3XBM earth mode blog," https://sites.google.com/site/sub9khz/vlf-using-earth-mode/g3xbm-earth-mode-blog; "XBM VLF blog," https://sites.google.com/site/sub9khz/xbm-vlf-blog; "VLF Amateur News 2014," https://sites.google.com/site/sub9khz/vlf-amateur-news-2014. Cited in Part B's text as the blog, sub9khz 2014 or the sub9khz news. Read (Part B); re-read on re-check. [G]
- [G16] Radio Amateurs of Canada, "New amateur VLF transatlantic record," https://www.rac.ca/new-amateur-vlf-transatlantic-record/ (repost of an ARRL item, 2021). Read (Part B); re-read on re-check. [G]
- [G17] DD7PC, "VLF transmissions on 8.97, 6.47 and 5.17 kHz by Stefan, DK7FC," https://www.qsl.net/dd7pc/dk7fc_vlf_tx.htm. Read (sparse) (Part B). [G]
- [G18] S. Nelson (KK7KA), "WOLF" description, hosted by DL4YHF, https://qsl.net/dl4yhf/wolf/wolf_info.html. Read (Part B); re-read on re-check. [G]
- [G19] C. A. Altgelt, "The world's largest 'radio' station" (n.d., PDF dated 2005), https://pages.hep.wisc.edu/~prepost/ELF.pdf. Popular article citing IITRI reports D06205-3, D06214-6, and the Seafarer FEIS 1972. Read (Part B). [G]
- [G20] T. Jacobsen, "ZEVS, the Russian 82 Hz ELF transmitter," ALFLAB, Halden (URL not recorded). Read; unverified claims (Part B). [G]
- [G21] South China Morning Post, 31 Dec 2018, "China's new antenna is five times the size of New York City…," https://www.scmp.com/news/china/science/article/2180071/. Secondary, via search (Part B). [G]
- [G22] D. McIntyre (W4DEX), "8971 Hz" transmission page (2014), http://w4dex.com/vlf/8971Hz/. Read (Part B); re-checked against the Internet Archive copy of Aug 2025. [G]
- [G23] NIST, "Radio station WWVB," https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb. Read (antenna description, efficiency and automatic tuning). [G]
- [G24] B. Mehlis (DL3JMM), "(Amateur) radio operation on frequencies below 8300 Hz," V1.3 (English machine translation of "Funkbetrieb auf Frequenzen kleiner 8,3 kHz"), PDF dated 21 Oct 2023, https://dl3jmm.darc.de/Funkbetrieb_auf_Frequenzen_kleiner_8300_Hz_V1.3_en_DeepL.pdf. Read (regulation, power, kite phase-locked loop, ground-loop lengths). [G]
- [G25] Wikipedia, "15 kV AC railway electrification," https://en.wikipedia.org/wiki/15_kV_AC_railway_electrification. Page summary read 13 Nov 2026 (15 kV at 16.7 Hz in Germany, Austria, Switzerland, Sweden and Norway). [G]

**In-house**

- [H] Chapter 10 of this documentation set (10-sub9khz-propagation.md, Fable-5.2). Used only as a cross-reference for attenuation-rate ranges. [H]

**Own estimates** [E]: Part A's thin-wire electrostatic method of moments and its table, link-budget and detuning calculations (scripts kept only in the temporary working area, not in this directory). On re-check, Tables A–C, the Chu figures, both link-budget tables and the GUI-equivalence example were recomputed independently from the stated inputs and agree to rounding; the link budgets use F_a ≈ 146 and 174 dB (8.27 kHz) and 126 and 171 dB (2.97 kHz) for N2 and N5. On a further re-check an independent thin-wire electrostatic method of moments (exact uniform-charge segment potentials, image ground, 180–600 segments) gave 899 pF and h_e = 50.0 m for the 61 m T, against Part A's 902 pF and 49.8 m, and supplied the sag and sideways-bow detuning values; the Debye tanδ values, the Wheeler comparison and the G1 conversions added on that re-check are likewise [E].

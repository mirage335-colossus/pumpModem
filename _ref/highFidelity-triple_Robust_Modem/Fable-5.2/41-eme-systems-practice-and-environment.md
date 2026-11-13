# Earth–Moon–Earth: systems, practice and environment

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** lunar scattering and geometry [40](40-eme-lunar-scattering-and-geometry.md); equipment [55](55-equipment-and-front-end-effects.md); independent arithmetic [90](90-cross-check-calculations.md).

**Summary.** Moon relay has carried real traffic. The US Navy's Communication Moon Relay ran Washington–Hawaii operationally from 1959 to 1963 with 84 ft dishes, 100 kW and about 400 MHz, carrying up to sixteen 60-word-per-minute teleprinter channels [B][T]. Its limits were mutual Moon visibility, fading, the power-aperture product and, afloat, antenna stabilisation; delay spread proved smaller than early theory feared. Today's evidence is amateur and grey: at 5,760 MHz two-way contacts are completed between dishes as small as 1.2 m (22 W) and 1.5 m, with reports of −18 to −25 dB in 2,500 Hz (C/N0 ≈ 9–16 dB-Hz) [G]; the 22 W direction agrees within about 1 dB with the radar equation at 6.5 % lunar reflectivity if a 100 K receiver is assumed [E]. The isotropic two-way loss at 5.76 GHz is 284.1 dB at the mean centre-to-centre distance (282.8 at perigee, 285.1 at apogee) and 0.3 dB less with the Moon overhead [B][E]. The modem's "200 dB" case corresponds to roughly 2.8–3 m dishes at both ends; its −164 dBm/Hz noise density equals about 2,900 K, 14–16 dB worse than real receivers (70–110 K) [E]. At 5.8 GHz the non-lunar limits are mostly noise: lunar thermal emission (about 7 K in a 1.5 m dish, 27 K in 3 m, about 170 K in 10 m) [E], rain emission (which costs more than rain attenuation), the Sun near new Moon, ground pickup below about 5° elevation, and wireless-LAN interference reported on 5,760 MHz [G]. The ionosphere is negligible (Faraday rotation about 3° per pass in the ITU-R high-TEC case) except for low-latitude scintillation: 4 GHz satellite measurements scaled to 5.76 GHz give about 5–8 dB peak-to-peak per pass for 0.1–0.01 % of a solar-maximum year [S][E], while high-latitude amplitude scintillation scaled from L band is about 1 dB or less [P][E]; no measurement at 5–6 GHz on a lunar path was found. Doppler shift from Earth rotation reaches ±18 kHz (about ±20 kHz with the Moon's orbital motion) and 1.3 Hz/s [E]; predicted Doppler spread at 40° latitude is 86 Hz median and 164 Hz maximum limb-to-limb [G], so fading time scales are about 6–40 ms and incoherent multi-tone FSK with strong codes dominates. The only direct 5.7 GHz width measurements found are two visual estimates of 20 and 35 Hz near a libration minimum, 10–20 % below prediction [G]. WSJT-X's predicted "Width" and the "Doppler spread" of its Q65 simulator are full widths, so the published Q65 threshold curves at 100 Hz spread correspond roughly to a median 5.76 GHz channel [G][E].

Conventions: grades [S] standard, [P] research paper or preprint (peer-review status checked separately), [T] technical report, [B] book/handbook, [G] grey, [E] own estimate. Reference keys are resolved in the last section, where access status is given. "SNR2500" is signal-to-noise ratio in a 2,500 Hz reference bandwidth; C/N0 = SNR2500 + 33.98 dB-Hz [E]. One TECU is 10¹⁶ electrons/m². Each own estimate states its formula or method where it is used.

## Physical mechanisms

**Radar-equation link.** For isotropic antennas the two-way loss is L = −10 log[η r² λ² / (64 π² d⁴)], with lunar radius r = 1.738 × 10⁶ m, mean distance d = 3.844 × 10⁸ m and reflectivity η = 0.065 ([Hbk10] eq. 1, Table 30.7) [B]. Received power is Pr = Pt + Gt + Gr − L. Loss rises 6 dB per octave, but for fixed dish diameters gain rises 12 dB per octave for the pair, so the net loss falls 6 dB per octave. Trexler's survey of measured circuit loss found the same slope from 20 MHz to 3,000 MHz, passing through 258 dB at 300 MHz with ±4 dB scatter ([Trexler58] p. 291) [P]; extrapolated to 5,760 MHz this gives 283.7 dB, against 284.1 dB from the formula [E]. The fourth-power distance law gives −1.31 dB at an extreme perigee of 356,400 km and +0.98 dB at an apogee of 406,700 km [E] ("±1.1 dB" in [Hbk10] §30.9.1). These distances are centre to centre, which is close to the observer's range with the Moon on the horizon; with the Moon overhead the observer is 6,371 km closer and the loss is 0.29 dB lower (283.8 dB at mean distance, 282.5 dB at perigee, 284.8 dB at apogee) [E].

**Noise.** System temperature is Ts = Tr + Ta, where Ta sums the cosmic background (2.7 K), atmospheric emission, the Moon in the main beam, galactic emission (dominant below about 300 MHz, scaling about f^−2.6 to f^−2.75) and side-lobe pickup of warm ground ([Hbk10] §30.10.1; [P372] eq. 14) [B][S]. An absorbing atmosphere emits: T_sky = T_mr (1 − 10^(−A/10)) + 2.7 × 10^(−A/10), with T_mr ≈ 275 K ([P372] eq. 10; [P618] eq. 69) [S]. For a receiver of 50–100 K, each 0.1 dB of absorption therefore adds about 6 K, and "this increase of noise temperature may have a greater impact … than the attenuation itself" ([P618] §3) [S].

**The Moon as a noise source.** The Moon is a thermal emitter filling 0.49–0.56° of the beam it is tracked with. At centimetre wavelengths the emission comes from decimetres to metres of depth, where the monthly temperature wave is damped; the phase-dependent part falls from ±50 % at millimetre waves to a few per cent at 3–10 cm ([KT63] Table II) [P]. For a Gaussian beam of half-power width θ_b the antenna-temperature increase is ΔT ≈ T_B [1 − exp(−ln 2 (θ_moon/θ_b)²)] [E]. The same geometry means that when θ_b falls below the lunar diameter, extra dish gain no longer raises echo power in proportion ("fill" or "beam-width factor", [OE2IGL-LB]) [G].

**Troposphere.** Gaseous absorption (oxygen wing and water vapour), cloud liquid water (Rayleigh absorption ∝ roughly f²) and rain (γ = k R^α) attenuate each of the two slant passes and add noise on the receiving pass ([P676], [P840], [P838], [P618]) [S]. Refractive-index turbulence causes amplitude scintillation that grows as f^(7/12) and as (sin θ)^−1.2; below about 5° elevation it merges into multipath-like fading with a 10 dB/decade tail ([P618] §2.4) [S]. Refraction raises the apparent elevation and adds beam-spreading loss at low angles ([P834]) [S].

**Ionosphere.** Faraday rotation is θ = 2.36 × 10⁻¹⁴ B_av N_T / f² rad (f in GHz, N_T in el/m², B_av in T) ([P531] eq. 4) [S]. It has the same sense in both directions, so for a self-echo it doubles; combined with the geometric ("spatial") polarisation offset between distant stations it produces the one-way propagation familiar at 144–432 MHz ([Hbk10] §30.11.1; "MNR" in [UG27] §19) [B][G]. Scintillation from electron-density irregularities scales as about f^−1.5 in S4 and is strongest after sunset near the magnetic equator and in the auroral zones ([P531] §5) [S].

**Polarisation.** A smooth reflector preserves linear polarisation and reverses circular sense; the diffuse component is depolarised. At UHF the cross-polarised echo is "usually 15 dB or more" down; at 10 GHz and above it "may be only a few dB below" ([Hbk10] §30.9.4) [B].

**Doppler.** The line-of-sight rate is dominated by Earth rotation (about 460 m/s at the equator). For a self-echo f_D ≈ (2f/c) ω_E R_E cos φ cos δ sin H, and df_D/dt ≤ (2f/c) ω_E² R_E cos φ cos δ [E]. The Moon's own radial velocity, about ±56 m/s for the mean orbital eccentricity of 0.055, adds up to ±2.1 kHz on a 5.76 GHz self-echo over the month [E]. Libration spreads the echo over up to 4 f R ω_lib / c limb-to-limb ([K1JT10] p. 2) [G]; coherence time is about the inverse of the spread.

## Measured and reported values

### 1. Professional and historical use

| System / experiment | Parameters reported | Result, data rate, limits | Source, grade |
|---|---|---|---|
| Project Diana, Evans Signal Laboratory, January 1946 | 111.5 Mc receiver chain; about 15 kW peak for the echo series of Fig. 13 | Echoes "in excess of 20 db above receiver noise"; successive echoes 4 s apart measured 20, 2, 20, 6, 14 dB; frequently no echo at all | [DeWitt49] pp. 229, 239 [P] |
| Bay, Hungary, 1946 | "Novel electrolytic signal-output integrator" | Independent detection | [Yaplee58] p. 293 [P] (secondary for Bay) |
| DeWitt & Stodola, communication assessment (1949) | — | Listed limits: narrow beams needed, large power, tracking, mutual visibility, 2.4 s delay, "rapid and large variations" of attenuation, and Sun noise when Sun and Moon share the beam | [DeWitt49] p. 240 [P] |
| Grieg, Metzger & Waer, theory (1948) | Frequencies above about 50 Mc | Smooth Moon: all signal types including television; perfectly diffuse Moon: telegraph or teleprinter and possibly narrow-band telephone; Moon visible "up to approximately twelve hours a day"; for a rough Moon the modulation response falls as 41.1/f above about 100 c/s | [Grieg48] pp. 652, 663 [P] |
| First moon-relayed message, Cedar Rapids → Sterling, 28 Oct and 8 Nov 1951 | 418.0 Mc, 20 kW, transmit gain 21 dB (horn), receive gain 30 dB (31 ft dish), noise figure 4 dB, bandwidth 1 kc | Predicted S/N 8.6 dB in 1 kc (≈ 38.6 dB-Hz [E]); signal "varied from the noise level to occasional peaks as high as 10 db above the noise"; hand-keyed "What hath God wrought?" copied from tape; authors asked for 10–20 dB more gain for a satisfactory telegraph circuit; fixed horn usable about twice a month | [Sulzer52] p. 361 [P] |
| NRL Stump Neck radar, from October 1951 | 198 Mc, 220 × 263 ft fixed parabola, measured gain 10,000, beam 1.4°; 750 kW–1.2 MW peak, 10–12 µs pulses; receivers 2–3 dB noise figure | More than half the echo energy in the first 50 µs; CW circuit loss 252.5 dB at 198 Mc; "old theoretical model indicated a circuit bandwidth of 50 cps", measurements "possibly a 10,000-cps band"; above 198 Mc "no unaccounted-for long period fading" once polarisation and selective fading are removed | [Trexler58] pp. 286–292 [P] |
| NRL voice via Moon, 24 July 1954 | 100 W, amplitude modulation, 3 kc receiver bandwidth | Mean S/N "near 14 db" (≈ 48.8 dB-Hz [E]); voice "perfectly recognizable", no reverberation | [Trexler58] p. 292 [P]; date [vanKeuren97] p. 9 [B] |
| NRL transcontinental and Hawaii teletype, Nov 1955 – Jan 1956 | 301 and 300 Mc, 10 kW klystron, Stump Neck transmitter; simple SK-2 radar-antenna arrays at San Diego and Wahiawa | Teletype exchanged; fading observed; frequency diversity and circular polarisation recommended | [vanKeuren97] pp. 15–16 and n. 32 [B] |
| Communication Moon Relay, Washington–Hawaii | 84 ft steerable dishes and 100 kW transmitters at Annapolis and Opana; receivers at Cheltenham and Wahiawa; "around 400 megahertz" ([vanKeuren97]); a 1960 trade report gives "438–455 mc" and an effective radiated power of "more than 400,000,000 watts" | Up to 16 teleprinter channels at 60 words per minute (16 × 45.45 baud ≈ 727 baud aggregate [E]) plus facsimile; the 1960 demonstration used "4-channel teletypewriter and facsimile equipment", with operation "limited to that period when the moon is simultaneously visible at both terminals", from "a few hours up to a maximum of about 12 hours"; functional 1959, inaugurated January 1960; "operational from 1959 to 1963" | [vanKeuren97] pp. 16–17 [B]; [Jaffe66] [T]; [EW60] [G] |
| Shipboard (USS Oxford) and TRSSCOMM | 16 ft stabilised dish; receive only in 1961, 1 kW transmitter in 1962; later described as 10 kW; system AN/SRC-33 | First shore-to-ship message 15 Dec 1961; operational with Oxford 25 Feb 1964; five more ships 1966–69; suspended autumn 1969; Liberty installation "never was reliable"; Valdez problems "primarily involving the antenna and its controls" | [vanKeuren97] p. 17 [B]; [NavySpace] §2.3.1 [T]; [NSA-TRS] [T]; 10 kW and the 1961 date from [Wiki-TRSSCOMM] [G] |
| Jodrell Bank | Evans (1957): effective lunar depth ≤ 1 ms, so modulation up to about 1,000 c/s; Pye link May 1959 with 1 kW from the 250 ft telescope to the 84 ft AFCRC dish | "Clear voice circuits" reported by Lovell, but AFCRC judged that about 15 dB more system gain was needed for good voice or music; Pye planned a commercial relay with 30 ft dishes near 7,000 Mc, abandoned when satellites succeeded | [Agar97] pp. 23–26 [B] |
| Passive relay of terrestrial transmitters | Murchison Widefield Array, 80–300 MHz | Earth's FM broadcast band seen reflected from the Moon | [McKinley13] abstract [P] |
| Dwingeloo LoRa moonbounce, 5 Oct 2021 | 430–440 MHz, 350 W, 25 m dish, LR1110 chip; I/Q recorded at 433.8 MHz | Frame received after 2.44 s; range and Doppler "matched very well" with JPL Horizons; a delay–Doppler "preview" plot, with no published analysis found; open data set whose timestamps are "not very well calibrated" | [Lacuna21] [G]; [Zenodo22] [G] |
| Dwingeloo Earth–Venus–Earth and high-power Moon test, Oct 2026 | 1,299.650 MHz, EIRP 95.3 MW (about 1.5 kW into 48 dBi [E]), circular polarisation, receive bandwidth 1 Hz | "VENUS ECHO" decoded with a home-made FSK; Moon echo test announced as "roughly 10 dB stronger than usual" | [NL2610] pp. 19–20 [G] |

Consistency checks [E]: the Communication Moon Relay (50 dBW, two 38.0 dBi dishes at 400 MHz, L = 261.0 dB, assumed Ts = 400 K) gives C/N0 ≈ 68 dB-Hz, about 33 dB in 3 kHz before fading margin. The Stump Neck voice test (20 dBW + 40 + 40 − 252.5 dB) gives about 50 dB-Hz, consistent with 14 dB in 3 kc. The 81° longitude difference between Washington and Hawaii removes about 5.4 h from a roughly 12 h Moon pass, leaving a common window of order 6 h a day; this ignores latitude and lunar declination, which the contemporary range of "a few hours up to a maximum of about 12 hours" ([EW60]) includes.

### 2. Amateur practice by band

Milestones [B]: first amateur echoes 1953 (144 MHz); first two-way 1960 (1,296 MHz); first 5.7 GHz EME 1987; 10 GHz 1988; 24 GHz 2001; 47 GHz 2005 ([Hbk10] sidebar).

| Band (MHz) | L mean (dB) [Hbk10] T30.7 | Typical Ts (K) [Hbk10] T30.8 | CW self-echo baseline [Hbk10] T30.9 | Max self-echo Doppler | Limb-to-limb spread max / median; w10 / w50 median (Hz) [K1JT10] | Q65 submode advised [UG27] §9.7 |
|---|---|---|---|---|---|---|
| 50 | 242.9 | 3,500 | 4 × 12 m Yagis, 1,200 W | ±155 Hz [E] | — | 60A |
| 144 | 252.1 | 310 | 4 × 6 m Yagis, 500 W | ±440 Hz | 4 / 2.2; 1.1 / 0.4 | 60A |
| 432 | 261.6 | 85 | 4 × 6 m Yagis, 250 W | ±1.3 kHz [E] | 12 / 7; 3 / 1.2 | 60B |
| 1,296 | 271.2 | 70 | 3 m dish, 160 W | ±4 kHz | 37 / 19; 10 / 4 | 60C |
| 2,304 | 276.2 | 77 | 3 m dish, 60 W | ±7 kHz [E] | 65 / 35; 20 / 8 | 60C |
| 3,400–3,456 | 279.7 | 90 | 2 m dish, 120 W | ±10.5 kHz [E] | 97 / 51; 31 / 12 | 60D |
| 5,760 | 284.1 | 110 | 2 m dish (39.2 dBi, 1.8°), 60 W | ±17.9 kHz [E] | 164 / 86; 60 / 25 | 60D |
| 10,368 | 289.2 | 160 | 2 m dish (44.3 dBi, 1.0°), 25 W | ±30 kHz | 295 / 156; 128 / 58 | 60D or 60E |
| 24,048 | 296.5 [E] | 360 | — | ±70 kHz [Echo22] | up to 700 [Echo22] | 60E |

Notes. The baseline is S/N 3 dB in 50 Hz (20 dB-Hz) on the station's own echoes; digital modes work about 10 dB lower ([Hbk10] §30.10.2–3) [B]. The spread columns are for self-echoes at ±40° latitude. w50 and w10 are the full widths of the autocorrelation function of the echo spectrum at its 50 % (−3 dB) and 10 % (−10 dB) points; "loosely speaking", w50 spans the frequencies holding half the echo power and w10 those holding 90 % ([K1JT10] p. 3) [G]. The "Width" that WSJT-X predicts and displays is the "EME Doppler spread over full lunar disk", that is, the limb-to-limb width, and its Echo mode integrates signal power over that full width ([UG27] §9.6; [Echo22] pp. 2–3) [G]. The 5,760 MHz row is a prediction from measurements at 144–2,302 MHz and 10.3 GHz, not a 5.7 GHz measurement ([K1JT10] pp. 3–5, Figs 7–9) [G]. [Hbk10] prints 293.5 dB at 24 GHz; its own equation gives 296.5 dB [E]. The 5,760 MHz noise budget in [Hbk10] Table 30.8 is cosmic 3 K, atmosphere 3 K, Moon 13 K (2 m dish), side lobes 30 K, receiver 60 K.

**5,760 MHz station reports (all [G], self-reported, "432 and Above EME News").**

| Report | Stations | Result |
|---|---|---|
| IS0/HB9COG expedition, May–June 2025 [NL2506] | 1.5 m mesh dish; 8.5 h on 6 cm | 31 contacts (28 in Q65-60D, 3 in CW) with 16 countries; smallest station worked was G4BAO, "1.2 m solid dish 22 W, −24/R−18" |
| G4BAO [NL2506] | 1.2 m dish, 22 W | Signals "marginal both ways" with the 1.5 m dish; also worked Japan on Q65 |
| OK1KIR [NL2506] | large dish, Q65-60D | "25DB/11DB" reports "despite terrible QRM from WiFi channel sitting on 5760 MHz"; Q65 got through "the occasional time gaps of 20 MHz WiFi channel when CW signal is unreadably destroyed" |
| PA0PLY [NL2506] | — | −18/−25 dB with the expedition while its dish looked through trees |
| G3LTF [NL2506] | dish with 0.6° beam (about 6 m [E]) | CW 549; wind made it "very difficult to keep the dish … on the moon" |
| SP9VFD [NL2610] | 3 m solid dish, about 40 W | Newly ready for CW and Q65 on 6 cm |
| WA6PY [NL2610] | — | One 6 cm contact; "very strong QRM, pulses every 800 ms" |
| SV3AAF [NL2609] | — | "Significant interference from rogue hilltop Wi-Fi emissions was experienced on 6 cm with moon noise blanked out", limiting operating time on the band |

Link check [E]: 22 W (13.4 dBW) + 34.6 dBi (1.2 m) + 36.5 dBi (1.5 m) − 284.1 dB = −199.6 dBW; with Ts = 100 K, N0 = −208.6 dBW/Hz, so C/N0 = 9.0 dB-Hz and SNR2500 = −25.0 dB, against the reported −24 dB. A 1,296 MHz check is tighter: a 3 m dish, 200 W and 85 K predict own-echo SNR2500 of −14.6 dB (apogee) to −12.3 dB (perigee) and the station was "measuring values close to these ideals" ([Echo22] pp. 4–5) [G]; the same formula reproduces these figures within 0.1 dB [E].

**Small-dish 10 and 24 GHz results** [G]. A 64 cm dish with 7 W worked a 3 m, 75 W station at 10 GHz in a libration minimum; with 77 cm and 45 W, contacts were possible "on about half of all days", when spreading was below 150 Hz ([MT13] pp. 2–3). An 0.82 m dish measured Sun noise 7.4–7.5 dB (flux 314–322 units at 10,368 MHz), Moon noise 0.31–0.32 dB and noise figure 0.69 dB, and decoded the 7.2 m, about 15 W DL0SHF beacon at −13 to −12 dB against a −10.1 dB EMECalc prediction; the author attributes about 2 dB to the narrow transmit beam under-filling the Moon ([OE2IGL-10], 2023). A 1.2 m dish at 24 GHz measured a 0.7° beam, Moon noise 1.33 dB and noise figure 1.4 dB ([OE2IGL-24], 2026). At 10 GHz, 2.4 m dishes show 16.5–16.7 dB of Sun noise and 2.5–2.8 dB of Moon noise; a 4.5 m class station reports 3.1 dB ([NL2609], [NL2610]). Small-dish advice: 20 W minimum at 10 GHz and frequency known to better than 100 Hz ([OK2AQ22]).

### 3. Digital modes and thresholds

| Mode | Code, modulation | Tone spacing; occupied bandwidth | Tx time (s) | Threshold SNR2500, no spread | C/N0 (dB-Hz) [E] | Eb/N0, all energy / data tones only (dB) [E] | Source |
|---|---|---|---|---|---|---|---|
| CW by ear | — | — | — | about −18 | 16 | — | [K1JT08] T3 |
| JT65A/B/C | RS(63,12), 64-ary + sync tone (half the energy) | 2.69 / 5.38 / 10.77 Hz; 178 / 353 / 703 Hz | 46.8 | −25 for all three in [UG27]; −25 / −24 / −23 in the 2005 description | 9–11 | 7.1–9.1 / 4.1–6.1 | [UG27] T7–8; [W3SZ19]; [JT65-05] |
| JT4A…G | K = 32, r = 1/2 convolutional, 4-FSK, sync in half the energy | 4.375…315 Hz; 17.5…1,260 Hz in [MT13] (949 Hz for G in [UG27]); JT4F 157.5 Hz | 47.1 | −23 (A) … −17 (G), −18 for F, "when spreading is comparable to tone spacing" | 11–17 | 9.1–15.1 / 6.1–12.1 | [UG27] T7–8; [MT13] T1 |
| QRA64A…E | Q-ary repeat-accumulate (63,12), 64-FSK, Costas-array sync (25 %) | 1.74…27.8 Hz; 111…1,752 Hz | 48.4 | −26 … −22; slides give −28.1 dB for a full message | 8–12 | 6.3 / 5.0 (A) | [Venice16]; [W3SZ19] |
| Q65-15A / 30A / 60A / 120A / 300A | QRA (63,13) over GF(64) with 12-bit CRC, 65-FSK, 22 sync symbols of 85 | 6.67 / 3.33 / 1.67 / 0.75 / 0.29 Hz; 433 / 217 / 108 / 49 / 19 Hz; submodes B–E are 2–16 times wider | 12.8 / 25.5 / 51.0 / 113.3 / 293.8 | −22.2 / −24.8 / −27.6 / −30.8 / −33.8; with full a-priori information −23.7 … −37.4 | 11.8 / 9.2 / 6.4 / 3.2 / 0.2 | 4.0–6.0 / 2.7–4.7 | [Q65QS] p. 1; [UG27] T7, T9 |
| WSPR | K = 32, r = 1/2, 4-FSK, 1.46 baud | 1.46 Hz; 5.9 Hz | 110.6 | −31 (−29 in 2008) | 3 | 6.4 / 3.4 | [UG27]; [K1JT08] |

The Eb/N0 columns divide C/N0 by payload bits per transmission second (72, 77 or 50 bits); the second figure counts only data-tone energy, the convention under which [FT16] (Fig. 3 caption) states that SNR2500 = −24 dB is Eb/N0 = 5.1 dB for JT65.

**Threshold versus Doppler spread** (simulations; values read from the figures and rechecked against the plotted points to ±0.1 dB) [G]. The current WSJT-X Q65 simulator applies Rayleigh fading whose power spectrum is a truncated Lorentzian, P(f) ∝ 1.111/[1 + (6f/s)²] − 0.1 for |f| < s/2 and zero beyond, where s is the stated "Doppler spread" ([Q65sim]) [G]. The spread s is therefore the full support of the fading spectrum: its half-power width is 0.30 s, the central 50 % of the power lies within 0.20 s and the central 90 % within 0.56 s [E]. Whether the 2021 figures were made with this version of the simulator was not checked.

| Spread (Hz) | Q65-60A, no a-priori ([Q65QS] Fig. 2) | Q65-60D, none / full a-priori (Fig. 1) | Q65-120E, none / full a-priori (Fig. 1) |
|---|---|---|---|
| 0 | −27.6 | −27.1 / −27.5 | −29.0 / −29.6 |
| 5 | −25.6 | −25.7 / −26.8 | −28.0 / −28.9 |
| 10 | −24.6 | −25.2 / −26.6 | −27.4 / −29.0 |
| 20 | −23.4 | −24.4 / −26.3 | −26.4 / −28.7 |
| 50 | — | −22.7 / −25.4 | −24.6 / −27.7 |
| 100 | — | −21.0 / −23.9 | −22.7 / −25.8 |

Matched to [K1JT10]'s predicted 5,760 MHz medians (limb-to-limb 86 Hz, w10 60 Hz, w50 25 Hz), the simulator spread s that corresponds is roughly 85–125 Hz, depending on which width is matched; the 100 Hz row (Q65-60D −21.0 dB without a-priori information, −23.9 dB with full a-priori information) is therefore the nearest published estimate for a median 5.76 GHz channel, about 6 dB worse than the no-spread threshold, and the predicted 164 Hz maximum lies beyond the published curves [E].

Other statements. Q65 decodes with spread "up to ten times the tone spacing, and even beyond" ([Q65QS] p. 2) [G]. At 33 Hz spread Q65-60B/C/D all gave 98 % decodes at −25 dB and 0–10 % at −26 dB; at 5 Hz, 60A gave 76 % at −27 dB (simulation, [KA1GT-Q65]) [G]. Beyond 10 Hz of spreading the wide JT4 submodes lose about 1 dB per doubling and JT65C about 3 dB; the crossover is 50–80 Hz at 10 GHz and 20–40 Hz at 24 GHz ([MT13] p. 7) [G]. Rayleigh fading with 0.2–1 Hz spread wrecks hard-decision Reed–Solomon decoding but costs soft-decision decoding little, and 0.2 Hz of spread slightly helps near threshold ([FT16] §6.2, Fig. 4) [G]. Message averaging gains 1–3 dB and is "the norm" at 24 GHz ([MT13] pp. 6, 10) [G].

**Coding and modulation lessons.**
- Doppler spread sets the minimum usable tone spacing: JT65B (5.4 Hz) suits 144–432 MHz, JT65C (10.8 Hz) 1.3–2.3 GHz, and above 3.4 GHz JT65C works only near libration minima; 315 Hz spacing was judged effective to 10 GHz ([K1JT10] p. 4) [G].
- On the microwave bands "phase-coherent techniques are out of the question" because the echo phase varies arbitrarily and rapidly ([MT13] p. 6) [G]. Narrow, quasi-coherent modes work only where spread is small: WSPR (1.46 Hz tones) was used on 144 MHz EME but was not expected to work at 432 MHz and above, needing spread below about 1 Hz ([K1JT08] p. 8) [G]; FST4 needs spread and drift small compared with the keying rate, 0.089 Hz for its longest period ([UG27] §6.5) [G].
- What works is energy detection of one tone at a time, a low-rate code over a large alphabet (rate 0.19–0.21), robust synchronisation spread through the message, erasure-tolerant soft decoding, a-priori information and averaging ([K1JT10] p. 4; [FT16]; [Q65QS]) [G]. Reported gains: a better code 1.0–1.5 dB, a better synchronisation scheme 1.9 dB, a-priori information up to 4.2 dB ([Venice16]) [G]; GPS timing in place of synchronising energy about 2 dB ([MT13] p. 6) [G].
- For this modem [E]: a 60 Hz chip rate at 5.76 GHz meets spreads of 25–164 Hz; with fading time scales of 1/w10 to 1/w50 (17–40 ms) at the median and about 6 ms at the predicted maximum, the channel decorrelates within about one to two and a half 16.7 ms chips at the median and within half a chip at the maximum, except in rare minima.

### 4. Libration spreading, fading and minimum windows

- Echo spectra are sharply peaked with weak wings reaching the predicted limb-to-limb limits. Measured (w50, w10): 2.3 ± 0.3 and 6.9 ± 0.6 Hz at 432 MHz; 6.2 ± 0.3 and 18.9 ± 0.6 Hz at 1,296 MHz (one station, 6 June 2010) ([K1JT10] p. 3, Fig. 6) [G].
- Widths grow faster than frequency above 2 GHz; linear scaling "would suggest … around 15 Hz at 10 GHz, but measurements show it to be several times larger" ([Hbk10] §30.9.3; [K1JT10] Fig. 8) [B][G].
- Direct 5.7 GHz widths. On 16 May 2010, near a libration minimum, G3LTF received VK3NX and PA0EHG on 5.7 GHz with visually measured two-station echo widths of 35 Hz and 20 Hz, against predictions of 38 Hz and 25 Hz from the empirical rule width (Hz) ≈ 6000 × f (GHz) × apparent libration rate (deg/min); G3LTF's echo recordings at 432 MHz, 1.3, 2.3, 3.4 and 5.7 GHz fall on a straight line against the rule "within experimental error", though with a fitted slope not exactly 1, and the rule is stated to apply to beams wider than the Moon ([G3WDG10]) [G]. The theoretical limb-to-limb coefficient is 6,743 Hz per GHz per deg/min, so these visual widths are about 0.9 of the limb-to-limb width [E]. At 10 GHz the same work found two-station widths of 35–70 Hz against 44–73 Hz predicted, signal widths of about 150 Hz at high libration, and a width near zero with very slow fading at a predicted 2 Hz minimum ([G3WDG10]) [G]. No 5.7 GHz measurement of w50, w10 or the spectrum shape was found.
- At 10 GHz, slide captions give a single-pulse echo width of 136 Hz against 135 Hz predicted, widths of 56 and 57 Hz with two Doppler-steering methods, and 4 Hz close to a libration minimum, with Doppler changing at about 70 Hz/min ([Venice16], G3WDG data; terse captions, values as read from the slides) [G]. A 10 GHz spectrum at 90 Hz limb-to-limb had a half-power width of 19 Hz with nearly half the power inside ±5 Hz ([MT13] Fig. 1) [G]. At 24 GHz the power was "much more evenly distributed over the full range", with little central peak ([MT13] p. 4) [G]. Echo spectra at 144, 1,296 and 10,368 MHz are shown against predicted widths of 3.4, 18 and 228 Hz in [Echo22] Fig. 2 [G].
- Coherence times: several seconds at 144 MHz, a few tenths of a second at 1,296 MHz, 20 ms at 10 GHz ([Hbk10] §30.9.3) [B]. At 5.76 GHz the median w50 and w10 imply about 40 ms and 17 ms; the 164 Hz maximum implies about 6 ms [E]. These are inverse widths, the range within which [K1JT10] (p. 4) places most libration fading; the scattering-law estimate in chapter [40](40-eme-lunar-scattering-and-geometry.md) gives a shorter field-correlation time, 13 ms to a correlation of 0.5 at the median spread [E].
- Minimum windows are predictable. Deep minima "last only a few minutes" and occur "only two or three days a month"; spreads of "a few Hz at 10 GHz" were found on a regional path, but none on Australia–Europe or Australia–North America paths, where "many opportunities … well below 100 Hz" exist ([MT13] pp. 1–3) [G]. Equatorial stations see larger spreads, high-latitude stations smaller; for two stations the libration vectors add ([K1JT10] p. 3) [G].
- Delay: half the echo power arrives in the first 210 µs at 1.3 GHz and 650 µs at 8 GHz (from Hagfors 1970, [K1JT10] p. 2) [G]; interpolation gives about 530 µs at 5.76 GHz [E]. Chapter [40](40-eme-lunar-scattering-and-geometry.md) obtains about 0.75 ms (0.5–0.95 ms by weighting convention and interpolation) from Lincoln Laboratory's measured scattering law [E]. The full extent is 11.6 ms.

### 5. Noise at the receiver

**Sky.** Galactic brightness of 200 K at 408 MHz scales to 19.7 K at 1 GHz ([P372] §4.1) [S]; above 2 GHz only the 2.7 K background, the Sun, the Moon and a few discrete sources matter. At 144 MHz the sky behind the Moon rises to as much as ten times its average for about five days each month ([Hbk10] §30.10.1) [B].

**Atmospheric gases** (line-by-line method of [P676] Annex 1 integrated through a standard atmosphere with 7.5 g/m³ of water vapour; no refraction) [E from S]:

| Elevation | 5.76 GHz: A (dB), T (K) | 10.37 GHz | 24.05 GHz (standard) | 24.05 GHz (30 °C, 22 g/m³) |
|---|---|---|---|---|
| 90° | 0.040, 2.5 | 0.052, 3.3 | 0.40, 24 | 1.0, 57 |
| 30° | 0.080, 5.0 | 0.10, 6.5 | 0.79, 46 | 2.0, 102 |
| 10° | 0.23, 14 | 0.30, 18 | 2.3, 112 | 5.7, 201 |
| 5° | 0.43, 26 | 0.56, 33 | 4.4, 174 | 11, 254 |
| 2° | 0.86, 49 | 1.1, 63 | 9.3, 243 | 24, 274 |

At 5.76 GHz humidity changes these by under 0.03 dB.

**Lunar thermal emission.** Lunation-mean brightness temperature at disc centre: 239.5 ± 10.1 K at 1 cm, 238.5 ± 9.2 K at 3 cm, 240.5 ± 9.3 K at 10 cm, 244.8 ± 9.1 K at 30 cm ([Linsky73] Table 6) [P]. Earlier radiometry gives a constant component of 210–216 K with a first-harmonic amplitude of 13.5–16 K at 3.2 cm (precision ±2–2.5 %), and 220 K with 5.5 K at 9.4 cm ([KT63] Table II, rows 22–25) [P]; at 35 GHz the disc-centre value swings between 145 and 225 K ([Gibson58] summary) [P]. Interpolated to 5.2 cm: mean 210–240 K at disc centre, variation about ±8–11 K, with the maximum lagging full Moon by some tens of degrees (15–55° reported at 3.2 cm) [E]. [Hbk10] uses 210 K [B]. [P372] §4.1 states 140 K at new Moon to 280 K at full Moon, "almost independent of frequency above 1 GHz" [S]; that range matches millimetre-wave, not centimetre-wave, radiometry. A later compilation, Krotikov and Pelyushenko (1987) as tabulated in [Hafez13] Table 2, gives lunation means of 217 K at 9.4 GHz and 221 K at 3.1 GHz with first-harmonic amplitudes of 14 K and 4 K and harmonic phases of 40–42°; the amplitude is 38 K at 37 GHz, 72 K at 100 GHz and 101 K at 300 GHz [G; secondary for the 1987 source]. Interpolated to 5.76 GHz this gives about 219 ± 8 K (±4 %) [E]. The ±70 K swing of [P372] therefore corresponds to frequencies near 100 GHz, about nine times the 5.76 GHz amplitude [E]; the edition now in force, P.372-18 (09/2026), repeats the statement unchanged.

Antenna-temperature increase at 5.76 GHz for a disc-average 230 K, mean lunar diameter 0.518°, beam 70 λ/D [E]:

| Dish (m) | Gain at 55 % (dBi) | Beam (°) | Fill | ΔT_moon (K) | G/T loss for 70 K cold-sky Ts (dB) |
|---|---|---|---|---|---|
| 1.5 | 36.5 | 2.43 | 0.031 | 7 | 0.4 |
| 2.4 | 40.6 | 1.52 | 0.078 | 18 | 1.0 |
| 3.0 | 42.6 | 1.21 | 0.118 | 27 | 1.4 |
| 4.5 | 46.1 | 0.81 | 0.247 | 57 | 2.6 |
| 6.0 | 48.6 | 0.61 | 0.396 | 91 | 3.6 |
| 10.0 | 53.0 | 0.36 | 0.754 | 173 | 5.4 |

The uncertainty is about ±20 % (brightness temperature, beam efficiency, perigee to apogee). The formula puts all of the antenna's response in a Gaussian main beam; with 55 % aperture efficiency and a 70 λ/D beam that main beam holds about 73 % of the beam solid angle, so a main-beam efficiency of 0.7–0.8 would lower every entry by 20–30 % [E], although the 2.4 m measurements at 10 GHz cited next are easier to reconcile with the unreduced values, given plausible system temperatures [E]. Using the 1987 compilation's 219 K instead of 230 K lowers the entries by 5 % [E]. [Hbk10] gives 13 K for 2 m. Grey 10 GHz measurements (0.31 dB in 0.82 m; 2.5–2.8 dB in 2.4 m) are consistent with the same formula within that uncertainty [E].

**Sun.** The Sun is "at least 10⁴ K at 10 GHz" when quiet ([P372] §4.1) [S]. [Hbk10] Table 30.10 (9 dB of Sun noise for a 2 m dish with G/Ts = 18.8 dB/K) implies about 117 flux units at 5.76 GHz near solar minimum [E]. T_A = S G λ² / (8π k) then gives about 410 K (1.5 m), 1,650 K (3 m) and beam-filling of order 17,000 K (10 m) on boresight [E]. Measured fluxes are higher. Noon fluxes of the US Air Force Radio Solar Telescope Network at Palehua, interpolated log-linearly between 4,995 and 8,800 MHz, give 5,760 MHz medians of 138 sfu in 2009 and 134 sfu in 2019 (10th–90th percentiles 124–149 sfu in 2019), and 205 sfu in 2014 and 241 sfu in 2024 (10th–90th percentiles 175–303 sfu; highest noon value 381 sfu) [T][E]; the 4,995 MHz medians themselves were 113–118 sfu near solar minimum and 181–226 sfu in those maximum years ([RSTN]) [T]. The Handbook-derived 117 sfu is therefore about 15 % below a measured solar-minimum value (its rounded 9 dB Sun noise allows 103–134 sfu [E]); at the solar-maximum medians T_A is about 720–850 K (1.5 m) and 2,900–3,400 K (3 m) [E]. Near new Moon a station with a 5.2° beam at 1,296 MHz gained 5 dB of S/N (−18 to −13 dB) by offsetting the dish 2° away from the Sun on receive ([KA1GT-NM], 25 Sept 2022) [G]. At 5.76 GHz beams are four to five times narrower, so the exposure is limited to roughly ±0.5 day around new Moon for a 3 m dish, longer for 1 m dishes with poor side lobes [E]. The flux can change by 10–20 % between three-hourly readings, and by 100 % or more in a flare ([KA1GT-Sun]) [G].

**Ground and feed.** Typical side-lobe pickup is 30 K; every 0.1 dB of loss ahead of the preamplifier adds at least 7 K; best G/T needs about −15 dB edge taper against −10 dB for best gain ([Hbk10] §30.11) [B]. Reported preamplifier noise figures are 0.6–0.7 dB at 10 GHz and 1.3–1.4 dB at 24 GHz ([OE2IGL-10], [OE2IGL-24], [NL2506]) [G].

**Interference.** Besides the three 5,760 MHz reports above (a 20 MHz wireless-LAN channel, 800 ms pulses, and hilltop Wi-Fi that blanked Moon noise), a multi-band expedition "got jammed by heavy QRM" of unknown origin ([NL2506]) and stations report wireless-LAN noise at 2,304–2,400 MHz ([NL2609]) [G]. In the standard 5 GHz wireless-LAN channel plan, channel 153 spans 5,755–5,775 MHz and 5,800 MHz lies inside the 5,725–5,875 MHz industrial band [E, general knowledge].

### 6. Rain, cloud and tropospheric scintillation

Rain coefficients for circular polarisation from [P838] eqs 2–5 [S]: k = 4.60 × 10⁻⁴, α = 1.606 at 5.76 GHz (4.80 × 10⁻⁴, 1.602 at 5.8 GHz, about 3 % more attenuation [E]); 1.37 × 10⁻², 1.217 at 10.368 GHz; 0.142, 0.983 at 24.048 GHz. Specific attenuation (dB/km):

| Rain rate (mm/h) | 5 | 25 | 50 | 100 | 150 |
|---|---|---|---|---|---|
| 5.76 GHz | 0.006 | 0.081 | 0.25 | 0.75 | 1.43 |
| 10.368 GHz | 0.097 | 0.69 | 1.61 | 3.74 | 6.12 |
| 24.048 GHz | 0.69 | 3.36 | 6.64 | 13.1 | 19.6 |

One-pass slant attenuation exceeded for a given percentage of an average year, [P618] §2.2.1.1 with assumed rain heights [E from S]:

| Climate (0.01 % rain rate; latitude; 0 °C height) | Elevation | 5.76 GHz: 1 % / 0.1 % / 0.01 % (dB) | Sky noise at 0.01 % (K) | 10.37 GHz, 0.01 % (dB) | 24.05 GHz, 0.01 % (dB) |
|---|---|---|---|---|---|
| Temperate (30 mm/h; 50°; 2.6 km) | 30° | 0.02 / 0.13 / 0.53 | 32 | 4.1 | 21 |
| Temperate | 10° | 0.07 / 0.36 / 1.3 | 72 | 8.1 | 38 |
| Subtropical (60 mm/h; 35°; 4.0 km) | 30° | 0.08 / 0.40 / 1.5 | 79 | 8.2 | 41 |
| Tropical (100 mm/h; 10°; 4.8 km) | 30° | 0.19 / 1.05 / 3.1 | 141 | 15 | 66 |
| Tropical | 10° | 0.66 / 4.4 / 8.7 | 237 | 34 | >100 |

On a 70 K receiver, 0.5 dB of rain costs 2.0 dB in total, 1 dB costs 3.6 dB and 3 dB costs 7.7 dB; on the modem's assumed 2,900 K receiver the same rain costs 0.55, 1.1 and 3.2 dB [E]. Rain fade durations are log-normal with medians of "several minutes" ([P618] §2.2.5.1) [S]. One observer notes light rain "only affected the 3 cm band, causing a slight increase in noise" ([NL2609]) [G].

Cloud: 0.029 dB per kg/m² of liquid water at 5.76 GHz, 0.092 at 10.37 GHz and 0.47 at 24.05 GHz at zenith ([P840] eqs 2–12) [E from S]; heavy cloud of 1–2 kg/m² is under 0.06 dB at 5.76 GHz.

Tropospheric scintillation, one pass, [P618] eqs 42–49 [E from S]: at 5.76 GHz with a 1.5–3 m dish and N_wet = 50, σ = 0.05 dB at 30°, 0.19 dB at 10° and 0.44 dB at 5°; fade exceeded 0.01 % of the time is 0.4, 1.4 and 3.2 dB. For hot humid air (N_wet = 130) the figures double (σ = 0.86 dB and 6.2 dB at 5°). Below 5° the worst-month formula ([P618] eq. 56, land path) gives fades of 25 dB or more for 0.7–6 % of the worst month at 1° and 0.002–0.02 % at 3°, for a refractivity-gradient statistic of 5–20 % [E from S]. Below about 4° elevation on inland paths and 5° on overwater or coastal paths, multipath fading "can be particularly severe" ([P618] §2.3) [S].

Refraction: mean angular deviation 0.45–0.65° at 1° elevation, 0.21–0.27° at 4°, 0.10–0.14° at 10°, 0.03–0.04° at 30°; day-to-day variation 0.1° r.m.s. at 1° and 0.007° at 10° ([P834] Table 1) [S]. Beam-spreading loss is 0.53 dB at 1°, 0.25 dB at 3° and 0.14 dB at 5° per pass ([P834] eq. 16) [E from S]. At VHF, ground reflections add up to 6 dB of "ground gain" at favourable elevations ([Hbk10] §30.10.2) [B]. At 5.76 GHz a reflection point h metres below the feed produces elevation lobes spaced λ/(2h), that is 0.75° for 2 m and 0.3° for 5 m, crossed in roughly 1.5–5 min as the Moon rises [E]. No microwave EME measurement of this was found.

### 7. Ionosphere

Faraday rotation per pass:

| Frequency (MHz) | 5 TECU slant | 30 TECU | 100 TECU (all with B_av = 50 µT, [P531] eq. 4) [E from S] | [P618] Table 1 high case (TEC 10¹⁸ el/m², elevation about 30°), scaled as 1/f² [S][E] |
|---|---|---|---|---|
| 50 | 3.8 turns | 22.5 turns | 75 turns | 120 turns |
| 144 | 163° | 2.7 turns | 9.1 turns | 14.5 turns |
| 432 | 18° | 109° | 362° | 579° |
| 1,296 | 2.0° | 12° | 40° | 64° |
| 5,760 | 0.10° | 0.61° | 2.0° | 3.3° |
| 10,368 | 0.03° | 0.19° | 0.63° | 1.0° |

Published anchor values in [P618] Table 1 are 30 rotations at 0.1 GHz, 108° at 1 GHz, 12° at 3 GHz and 1.1° at 10 GHz. Measured at 144 MHz: the echo polarisation turned through three full turns in five hours after sunset, and 432 MHz tracked it at one ninth ([LE16] §3.2, Fig. 6) [G]. A two-pass total of 6.5° at 5.76 GHz costs 0.06 dB for linear polarisation and nothing for circular [E]. Ionospheric delay (0.25 µs), refraction (under 0.6′) and mid-latitude absorption (under 0.01 dB) at 1 GHz in the same high case scale as 1/f² and are 33 times smaller at 5.76 GHz ([P618] Table 1) [S][E].

Scintillation: classed weak for S4 < 0.3, moderate 0.3–0.6, strong above 0.6; peak-to-peak fluctuation ≈ 27.5 S4^1.26 dB; fading rate "about 0.1 to 1 Hz"; intensity is Nakagami-distributed with m = 1/S4² ([P531] §§5.1, 5.4) [S]. At equatorial stations in solar-maximum years it occurs "almost every evening after sunset, with the peak-to-peak fluctuations … at 4 GHz exceeding 10 dB" ([P531] §5.2.2) [S]; [P618] Table 1 gives more than 20 dB at 1 GHz, about 10 dB at 3 GHz and about 4 dB at 10 GHz for such conditions [S]. Events last "from 30 min to hours" ([P531] §5.2.2) [S].

*Measured.* The only gigahertz occurrence statistics in [P531] are 4 GHz geostationary-satellite records from the Hong Kong and Taipei earth stations, 1975– 1980, at elevations of about 20° and 30° (Fig. 14). In the solar-maximum year (sunspot number 153–165) the peak-to-peak fluctuation on the 20° path exceeded about 5 dB for 1 % of the year, 10 dB for 0.1 % and 14 dB for 0.01 % (read from the figure) [S]. For high latitudes [P531] gives only VHF and UHF examples (Fig. 5) and a qualitative world map of fading depth at 1.5 GHz (Fig. 6). At 1.575 GHz a station in northern Norway measured S4 above 0.5 and power fades of 12 dB-Hz during the 17 March 2015 storm, one of the largest of the last solar maximum, while noting that high-latitude events usually show phase scintillation with little or no amplitude scintillation ([JO18]) [P].

*Extrapolated.* [P531] §5.8 scales 4 GHz peak-to-peak values by (f/4)^−1.5, which at 5.76 GHz gives 2.9, 5.8 and 8.1 dB (S4 ≈ 0.17, 0.29, 0.38) for the 1 %, 0.1 % and 0.01 % levels [E]. Applying the same law to S4 instead, as §5.1 states it for weak and moderate scintillation, gives 2.5, 5.0 and 7.0 dB (S4 ≈ 0.15, 0.26, 0.34) [E]; log-interpolating [P618] Table 1 gives about 6 dB for the post-sunset equinox case [E]. For link budgets [P531] converts a peak-to-peak fluctuation to a loss of Pfluc/√2 (§5.8) [S]. The L-band storm value scales to S4 ≈ 0.07, about 1 dB peak-to-peak, at 5.76 GHz [E]. All of these are one-way, point-source figures. At mid-latitudes fades at 1 GHz are 0.1 dB for 1 % and 0.3 dB for 0.1 % of the time ([P618] Table 2) [S], so nothing measurable is expected at 5.76 GHz. "At some locations, ionospheric scintillation may be important at frequencies below about 6 GHz" ([P618] §2.3) [S]. For EME the handbook notes that scintillation "can increase the fading rates produced by Earth rotation and lunar librations", with frequency spread from a fraction of a hertz to several hertz, mainly at VHF and UHF ([Hbk10] §30.9.4) [B].

### 8. Polarisation, pointing and frequency control

- Transmit right-hand, receive left-hand circular is "the standard for EME at 1296 and 2304 MHz" ([Hbk10] §30.11.1) [B]. At 10 GHz linear polarisation remains the convention in 2026 (vertical in Europe; an editor's note says circular "is not used on 10 GHz") ([NL2609]; [KA1GT-DL0SHF]) [G]. At 5,760 MHz a 2006 feed paper states that circular polarisation had been agreed at EME conferences as the microwave standard, used by "a good number of regular stations" on 5.7 GHz while "just a few others still remain on linear", against a linear majority at 10 GHz ([CT1DMK06] p. 1) [G]; an earlier Finnish 6 cm station built a feed usable for either, "as the EME fraternity is somewhat divided in this matter" ([OH2AUE]) [G]. The handbook expected the 1,296/2,304 MHz convention (transmit right-hand) to "probably become the standard on higher bands as well" ([Hbk10] §30.11.1) [B]; the sense used at 5,760 MHz today was not found stated.
- Linear mismatch loss is cos²Δθ: about 1 dB at 30°, 2–3 dB at 45°, and 10–20 dB (not infinite) at 90° ([KA1GT-DL0SHF]; [KA1GT-101]) [G]. Linear is credited with 1–1.6 dB over circular on 10 GHz small-dish links ([OK2AQ22]) [G].
- On 1,296 MHz circular links, the opposite-sense port is typically about 10 dB below the wanted port (0.4 dB loss); 6 dB differences (1 dB loss) indicate a poor axial ratio somewhere in the link ([KA1GT-CP], 2025) [G]. The depolarised lunar component is about 18 dB down at disc centre and about 3 dB down near the limb ([KA1GT-101], unsourced figure) [G]. At 10 GHz, observations not made systematically gave depolarisation "down by 6dB" in most cases and 1–2 dB in some, and a circular-polarised echo showed "practically half the width" of the linear-polarised one in a back-to-back test on a 3 m dish ([CT1DMK06]) [G]; [K1JT10] (p. 3) expects larger widths in the depolarised component.
- Pointing: aim for one quarter of the half-power beamwidth ([Hbk10] §30.12.1) [B]; loss ≈ 12 (ε/θ_b)² dB per pass [E]. At 5.76 GHz a 0.1° error costs 0.02 dB (1.5 m), 0.08 dB (3 m) and 0.9 dB (10 m); 0.3° costs 0.18, 0.73 and 8.1 dB. The Moon's sky motion of up to 0.25°/min uses a quarter-beam in 146 s, 73 s and 22 s for those dishes [E]. An 0.85 m dish at 10 GHz lost 1 dB at 1° and 4 dB at 2° off the Sun ([KA1GT-10G]) [G]. When a narrow beam drifted from disc centre toward the limb, the received frequency shifted by about 100 Hz of a 200 Hz limb-to-limb spread at 24 GHz ([MT13] p. 7) [G]. For two narrow beams not aimed at the same lunar spot, loss ≈ 12 offset² / (θ_tx² + θ_rx²) dB ([OE2IGL-LB]) [G].
- Narrow-beam correction [E]: with a crude scattering-law fit (half the power inside 0.30 lunar radii, 74 % inside 0.5) and Gaussian beams, the echo falls short of the point-target equation by 0.2 dB for 3 m ↔ 3 m, 0.9 dB for 10 m ↔ 3 m and 1.5 dB for 10 m ↔ 10 m at 5.76 GHz. A top-hat Moon, as [OE2IGL-LB] assumes for frequencies above 10 GHz, gives larger values.
- Doppler shifts can be predicted to parts in 10¹⁰ or better; that needs station coordinates better than a six-character locator and a clock synchronised to UTC, and the errors from a 1 km position offset or a 1 s clock error are shown separately; two common amateur programs are within 1 Hz at 1 GHz through a pass ([LE16] §§1, 3.1, Fig. 5) [G]. GPS-locked stations stay "within a few Hz" at 1.3–2.3 GHz; at 24 GHz the median error after correction was under 20 Hz and variations under 40 Hz ([MT13] pp. 1, 7) [G]; a GPS-disciplined 10 GHz portable system is "within 10 Hz" ([OE2IGL-10]) [G]. At 5.76 GHz a fractional error of 10⁻⁹ is 5.8 Hz, 10⁻⁷ is 576 Hz and 10⁻⁶ is 5.8 kHz [E].
- Self-echo Doppler at 5.76 GHz: up to ±17.9 kHz at the equator (±13.7 kHz at 40° latitude) from Earth rotation, plus up to ±2.1 kHz from the Moon's orbital radial velocity, rate up to 1.30 Hz/s (78 Hz/min; 60 Hz/min at 40°) [E]; reported rates are 4–13 Hz/min at 1,296 MHz and 60–70 Hz/min at 10 GHz ([KA1GT-Q65]; [Venice16]) [G]. Radios step in 1, 10 or 20 Hz and some cannot retune while transmitting. Typical 1,296 MHz Q65-60C and 10 GHz Q65-60E signals decode without appreciable loss even with no Doppler tracking, and 10–20 Hz tracking steps do not affect Q65-60E at 10 GHz; at 1,296 MHz with Q65-60C a step in the middle of a receive period costs up to about 1 dB (10 Hz) or 3 dB (20 Hz). Q65 can remove linear drift of up to 83 Hz in a 60 s period ([KA1GT-Step]) [G]. In "constant frequency on Moon" operation each station pre-corrects half its own echo Doppler ([KA1GT-CFOM]) [G].

### 9. Link budgets at 5.76 GHz [E]

Formula: C/N0 = Pt + Gt + Gr − L − 10 log(k Ts), with G = 10 log[0.55 (πD/λ)²], λ = 52.05 mm, L = 284.14 dB at mean centre-to-centre distance (283.85 dB with the Moon overhead, which would raise every C/N0 below by 0.3 dB). Example: 40 W = 16.02 dBW; 3 m dish = 42.56 dBi; Pr = 16.02 + 85.12 − 284.14 = −183.0 dBW (−153.0 dBm); Ts = 100 K gives N0 = −208.6 dBW/Hz (−178.6 dBm/Hz); C/N0 = 25.6 dB-Hz.

| Case (mean distance, clear sky, on-axis) | Net loss L − Gt − Gr (dB) | Ts used (K) | C/N0 (dB-Hz) | SNR2500 (dB) |
|---|---|---|---|---|
| 1.2 m, 22 W → 1.5 m | 213.0 | 100 | 9.0 | −25.0 |
| 1.5 m, 40 W → 1.5 m | 211.1 | 100 | 13.6 | −20.4 |
| 2 m, 60 W, own echo | 206.1 | 110 | 19.9 | −14.1 |
| 3 m, 40 W → 3 m | 199.0 | 100 (70 + 27 Moon) | 25.6 | −8.4 |
| 3 m, 1 kW → 3 m | 199.0 | 100 | 39.6 | +5.6 |
| 3 m, 15 kW → 3 m | 199.0 | 100 | 51.3 | +17.4 |
| 10 m, 40 W → 3 m | 188.6 | 100 | 36.1 (35.1 with narrow-beam factor) | +2.1 |
| 3 m, 40 W → 10 m | 188.6 | 243 (70 + 173 Moon) | 32.2 (31.3) | −1.8 |
| 10 m, 40 W → 10 m | 178.1 | 243 | 42.7 (41.2) | +8.7 |
| 10 m, 1 kW → 10 m | 178.1 | 243 | 56.6 (55.1) | +22.7 |

The large–small pair is not reciprocal: Moon noise makes the 10 m dish 3.9 dB worse as the receiving end than the 3 m dish is in the opposite direction.

**The modem's nominal case.** 40 W (+46.02 dBm) − 200 dB = −153.98 dBm; against −164 dBm/Hz this is 10.0 dB-Hz, and a 15 kW carrier is 35.8 dB-Hz. A net loss of 200 dB needs 42.07 dBi at each end: 2.83 m at 55 % efficiency (2.6–3.0 m for 65–50 %; 2.79 m with the Moon overhead; within 0.01 m of these at 5.8 GHz). The noise density −164 dBm/Hz is 2,883 K, a 10 dB noise figure with the antenna at 290 K; real receivers at 50, 70 and 110 K give −181.6, −180.2 and −178.2 dBm/Hz. With the same 200 dB and Ts = 100 K the result would be 24.6 dB-Hz, and the 15 kW carrier 50.4 dB-Hz. Conversely, 10 dB-Hz with a real 100 K receiver and 40 W is reached with a net loss of about 214.6 dB, that is about 1.2 m dishes at both ends (9.7 dB-Hz), which is the class of the smallest stations completing contacts in 2025.

**Sensitivities.** Distance: −1.31 dB (perigee) to +0.98 dB (apogee). Moon noise: 0.4 dB (1.5 m) to 5.4 dB (10 m) on a 70 K receiver. Rain at the receiving end: 2.0 dB total at 0.5 dB attenuation, 7.7 dB at 3 dB; at the transmitting end only the attenuation counts. Pointing: 0.1–0.7 dB per end for 0.1–0.3° on a 3 m dish. Polarisation: 0.2–1 dB. Elevation 5°: 0.43 dB gas loss and about 26 K extra per end, plus scintillation. A combined adverse case for 3 m, 40 W ↔ 3 m (apogee, 0.5 dB rain at the receiver, 0.2° pointing error at both ends, 0.5 dB polarisation loss) is about 25.6 − 1.0 − 2.0 − 0.7 − 0.5 ≈ 21.4 dB-Hz.

## Moderate → adverse → worst-case parameter table

Values are for 5.76–5.8 GHz unless stated. "Worst" cells combine conditions that are individually documented; no source reports them occurring together.

| Parameter | Moderate | Adverse | Worst case / outage | Source, grade |
|---|---|---|---|---|
| Isotropic two-way loss | 284.1 dB (283.8 dB with the Moon overhead) | 285.1 dB (apogee) | 285.1 dB plus 1–2 dB narrow-beam shortfall for dishes ≥ 10 m | [Hbk10] [B]; [E] |
| Net loss, equal dishes | 199 dB (3 m) | 206 dB (2 m) | 211–213 dB (1.2–1.5 m) | [E] |
| C/N0, 3 m 40 W ↔ 3 m | 25–27 dB-Hz | 21–22 dB-Hz | below 15 dB-Hz with heavy rain or low elevation at both ends; none near the Sun | [E] |
| C/N0, smallest working stations | 13–16 dB-Hz | 9–10 dB-Hz (1.2 m, 22 W ↔ 1.5 m) | no decode | [NL2506] [G]; [E] |
| System temperature on cold sky | 60–110 K | 130–200 K (Moon in a 3–4.5 m beam, light rain) | 300 K to thousands of K (heavy rain; ground at < 3°; Sun in near side lobes) | [Hbk10] [B]; [E] |
| Noise density N0 | −180 to −178 dBm/Hz | −176 dBm/Hz | −170 dBm/Hz or worse | [E] |
| Moon noise ΔT | 7 K (1.5 m) – 27 K (3 m) | 57–91 K (4.5–6 m) | 173 K (10 m) | [E]; [Linsky73], [KT63] [P] |
| Moon brightness variation with phase | ±4–5 % | ±7 % (3 cm value) | — | [KT63] [P]; [Hafez13] [G]; [E] |
| Sun noise | none (elongation > 10°) | several dB within about one beamwidth | receiver swamped: about 1,650–1,900 K (3 m) on boresight at solar-minimum flux, 2,900–3,400 K at solar-maximum medians, more in flares | [KA1GT-NM] [G]; [RSTN] [T]; [E] |
| Gaseous loss per pass | 0.08 dB (30°) | 0.23 dB (10°) | 0.43–0.86 dB (5°–2°) | [P676] [S]; [E] |
| Rain loss per pass | ≤ 0.1 dB (99 % of time) | 0.5–1.5 dB (0.01 %, temperate to subtropical, 30°) | 3–9 dB (0.01 %, tropical, 30°–10°) | [P618], [P838] [S]; [E] |
| Rain noise at the receiver | ≤ 5 K | 30–80 K | 140–240 K | [P618] eq. 69 [S]; [E] |
| Cloud loss per pass | < 0.01 dB | 0.03 dB | 0.3 dB (2 kg/m² at 10°) | [P840] [S]; [E] |
| Tropospheric scintillation σ per pass | 0.05–0.1 dB (30°) | 0.2–0.4 dB (10°) | 0.4–0.9 dB at 5°; multipath fades ≥ 25 dB for up to several % of worst month at 1° | [P618] [S]; [E] |
| Refraction pointing offset | 0.03–0.04° (30°) | 0.10–0.14° (10°) | 0.45–0.65° (1°), varying 0.1° day to day | [P834] [S] |
| Faraday rotation per pass | 0.1–0.6° | 2° | 3.3° (loss ≤ 0.06 dB linear, two-pass) | [P531], [P618] [S]; [E] |
| Ionospheric scintillation per pass | none (mid-latitude) | 2.5–3 dB peak-to-peak (S4 ≈ 0.15–0.17) for 1 % of a solar-maximum year at a low-latitude station; about 1 dB at high latitude in a major storm | 5–8 dB peak-to-peak (S4 ≈ 0.26–0.38), 0.1–1 Hz, for 0.1–0.01 % of a solar-maximum year, in events of 0.5 h to hours (low latitude, post-sunset, equinox); all extrapolated from 4 GHz or L-band measurements | [P531], [P618] [S]; [JO18] [P]; [E] |
| Doppler shift, own echo | a few kHz near transit | ±14 kHz near the horizon at 40° latitude | ±18 kHz (equator), about ±20 kHz including the orbital term | [E]; [Hbk10] [B] |
| Doppler rate | 0.5 Hz/s (off transit) | 1.0 Hz/s (transit, 40° latitude) | 1.3 Hz/s (equator; 16–34 Hz across one 12–26 s modem bit if untracked) | [E] |
| Residual frequency error after tracking | 1–5 Hz (GPS-locked, 1 Hz steps) | 10–20 Hz (10–20 Hz steps) | 0.6–6 kHz (unlocked oscillators, 10⁻⁷–10⁻⁶) | [MT13], [OE2IGL-10], [KA1GT-Step] [G]; [E] |
| Doppler spread, limb-to-limb | 86 Hz median (w10 60, w50 25) | 164 Hz maximum at 40° latitude | larger near the equator (about 201 Hz estimated in chapter 40) | [K1JT10] [G] (predicted); chapter 40 [E] |
| Minimum-libration window (best case, not a degradation) | a few Hz for a few minutes, 2–3 days a month, on some regional paths; 20–35 Hz measured at 5.7 GHz near a minimum | "well below 100 Hz" at 10 GHz on intercontinental paths | no deep minimum available | [MT13] [G] (10 GHz); [G3WDG10] [G] |
| Coherence time (inverse widths) | 17–40 ms | 6 ms | — | [E] from [K1JT10]; shorter field-correlation times in chapter 40 |
| Delay spread | 0.5 ms to half power (0.75 ms by chapter 40's weighting) | diffuse tail of several ms (stated for ≥ 10 GHz) | 11.6 ms extent | [K1JT10] [G]; [Hbk10] [B]; [E] |
| Fading statistics | Rayleigh-like within a tone; peaked spectrum | flatter spectrum (as at 24 GHz) | plus 5–8 dB ionospheric or deep tropospheric fades | [Hbk10] [B]; [MT13] [G] |
| Polarisation loss | 0.2–0.4 dB (circular) | 1 dB | 10–15 dB (wrong sense or 90° linear offset) | [KA1GT-CP], [KA1GT-DL0SHF] [G] |
| Pointing loss per end, 3 m | < 0.1 dB | 0.3–0.7 dB (0.2–0.3°) | several dB to loss of signal in wind (6 m dish report) | [E]; [NL2506] [G] |
| In-band interference | none | intermittent wireless-LAN bursts; pulses every 0.8 s | continuous 20 MHz wireless-LAN carrier on 5,755–5,775 MHz, CW unreadable; hilltop Wi-Fi blanking Moon noise | [NL2506], [NL2609], [NL2610] [G] |
| Availability | Moon above 10° at both ends, a few to about 10 h/day | low-declination days, short common windows | below the horizon; ±0.5–1 day at new Moon; equipment faults | [Grieg48] [P]; [KA1GT-DL0SHF] [G]; [E] |

## How the literature models or simulates this

- **Radar equation with constant reflectivity** (η = 0.065, [Hbk10]). Validated by echo measurements at 1,296 MHz to a few tenths of a dB ([Echo22]) [G] and here within about 1 dB at 5.76 GHz from one contact report with an assumed receiver temperature [E]. It ignores the angular scattering law, so it overestimates when either beam is narrower than the Moon.
- **EMECalc** (VK3UM), the standard amateur link tool; includes a fill factor for Sun and Moon noise but not for the two-way echo, and no surface-accuracy term ([OE2IGL-LB]) [G]. Its documentation was not obtained. A newer tool for 1–100 GHz adds a two-way "beam-width factor", pointing-offset loss and [P676] gas loss ([OE2IGL-LB], citing DUBUS 4/2021 and 4/2022) [G].
- **Ephemeris-driven Doppler and libration**: WSJT-X uses the JPL DE405 ephemeris for Doppler, limb-to-limb width, spatial polarisation offset ("Dpol"), maximum non-reciprocity ("MNR"), sky temperature and a degradation figure ("Dgrd") relative to perigee and cold sky ([UG27] §19; [LE16]) [G]. Width predictions match measured echo widths at 144 MHz to 10 GHz ([K1JT10], [Venice16], [Echo22]) [G].
- **Empirical spread parameters** w50 and w10 with straight-line fits against limb-to-limb spread, valid 144 MHz–2.3 GHz, with a noted excess above 2 GHz ([K1JT10] Fig. 8) [G].
- **Mode simulators** in WSJT-X apply white Gaussian noise plus a single "Doppler spread" parameter and produce the threshold curves above ([Q65QS]; [FT16]) [G]; in the current Q65 simulator that parameter is the full support of a truncated-Lorentzian Rayleigh-fading spectrum, and the simulator also offers a linear drift and a tracking-step size ([Q65sim]) [G]. They do not model delay spread, the measured spectrum shape per band or non-Gaussian interference; [W3SZ19] argues that the result depends on the assumed spectrum shape (its own tests used one, centrally peaked shape), and the 24 GHz experience in [MT13] supports this.
- **Echo mode** in WSJT-X 2.6 integrates power over the predicted width and replaces an estimator that read about 12 dB low at 30 Hz spread ([Echo22]; [KA1GT-Echo]) [G]; it is the practical validation instrument.
- **ITU-R methods**: [P676] (gases; line-by-line valid to 1,000 GHz), [P838] and [P618] (rain to 55 GHz; r.m.s. error of the total-attenuation method about 33 %), [P840] (cloud), [P618] §2.4 (scintillation, 4–55 GHz, elevation ≥ 5°, and a low-angle method fitted to 6–38 GHz data at 1–4°), [P834] (refraction), [P531] (ionosphere, with the GISM scintillation model and NeQuick2), [P372] (noise). All are built for a point source at a satellite. None treats the Moon as an extended, already-fading target, and [P372]'s lunar temperature is too coarse at centimetre wavelengths.
- **Lunar thermal models**: [Linsky73] (mean brightness versus wavelength with error budget), [KT63] (Fourier components versus wavelength) and the Krotikov–Pelyushenko (1987) compilation of T0, T1 and phase versus frequency ([Hafez13]). Yang 2019’s regional orbital radiometry was subsequently inspected in full; its calibration-bias findings and limited transfer are recorded below.
- **Data sets**: [Zenodo22] holds I/Q recordings of 433.8 MHz moon echoes with transmit references; WSJT-X sample files include 10 GHz and 6 m EME recordings ([Q65QS] p. 4). No 5.76 GHz I/Q data set was found.

## What a high-fidelity simulation would need

**Inputs.** Station coordinates and time, giving (from an ephemeris) Moon range, elevation, Doppler, Doppler rate, libration-rate vector, lunar diameter and Sun–Moon elongation. Dish diameters, efficiencies, beam shapes and polarisation; transmitter power. Receiver noise temperature and spill-over versus elevation. Weather per end (rain, water vapour, cloud). Ionospheric state where relevant. Oscillator model and the Doppler-tracking scheme (who corrects, step size, whether retuning happens during transmission). An interference model for the chosen frequency.

**Model-structure options, with limits.**

1. *Budget plus single-parameter fading.* Radar equation, time-varying Ts, and a Rayleigh process with one spread parameter, as in the WSJT-X simulators. Cheap and comparable with published thresholds, but it misses the peaked spectrum, the delay tail and all slow effects. The present "6 Hz, 0.5 ms" preset is closest to this type, although its direct path never fades ([00](00-method-and-modem-context.md) §5); 6 Hz matches the median w50–w10 at 1.3 GHz or a rare minimum at 5.76 GHz, where 25–86 Hz is typical and 164 Hz the predicted maximum, while 0.5 ms is close to the half-power delay.
2. *Tapped delay line with measured spectra.* Several delay taps, each with its own Doppler spectrum derived from the scattering law and the libration vector (chapter [40](40-eme-lunar-scattering-and-geometry.md)), plus the slow multiplicative and additive processes below. Needs spectrum shapes that are measured only at a few bands.
3. *Delay–Doppler scattering-function synthesis.* Integrate a lunar scattering map over the disc with both antenna patterns; this yields the narrow-beam shortfall, pointing-induced frequency offset and polarisation components consistently. Heavier, and the scattering law at 5.76 GHz is itself interpolated.
4. *Replay of recorded echoes*, for example the 433.8 MHz set. Faithful but tied to one band, date and station.

**Slow processes to superimpose.** (a) Ts(t): Moon term by beam fill and phase; atmospheric term by elevation; rain cells with log-normal durations of minutes, acting as loss on both passes and as noise on the receive pass; Sun term by elongation and side-lobe level. (b) Pointing error as a random walk or wind-gust process mapped through the beam, with a frequency bias for narrow beams. (c) Elevation-dependent scintillation, switching to a multipath model below 5°. (d) Ionospheric scintillation as an optional Nakagami-m multiplicative process per pass. (e) Frequency: residual offset, staircase from tracking steps, and uncorrected Doppler rate. (f) Non-Gaussian interference: packet bursts of a 20 MHz wireless-LAN channel and pulsed emitters.

**Observables for validation.** Own-echo SNR2500 against prediction over a lunar month (WSJT-X Echo mode); Sun, Moon and ground Y-factors; echo spectrum widths against limb-to-limb prediction; decode probability of Q65-60D versus SNR and spread against [Q65QS] Fig. 1; reciprocity for unequal dishes; residual frequency error distributions.

**Caveats.** An extended target changes how ionospheric scintillation appears. Lunar scatterers within the dominant ±0.08° subtend about ±0.5 km at 350 km altitude, more than the 5.76 GHz Fresnel scale of about 135 m, so each ray may see independent phase perturbations and the effect may show as extra spreading, not as the point-source fades that [P531] predicts [E, conjecture; no measurement found]. Circular polarisation should suppress single-bounce ground multipath because the reflected sense is reversed at grazing incidence [E, conjecture]. Better approaches than those listed may exist.

## Comparison with the modem's presets

The modem's EME row (row 8 of the manual list: 40 W, 200 dB, Rate 360 Hz with hopping; 16.7 ms cells, 75 Hz carrier spacing, 225 Hz occupied, 26.4 s bits) is simulated with the preset "EME focused" and the default −164 dBm/Hz ([00](00-method-and-modem-context.md) §§4.1, 5). In the simulator the Doppler figure bounds the offset and wander of a direct path that never fades plus at most one echo, and the delay figure bounds that echo's excess delay.

| Quantity | 5.76–5.8 GHz literature (this chapter) | Simulator | Comparison [E] |
|---|---|---|---|
| Doppler spread | Limb-to-limb 86 Hz median and 164 Hz maximum at 40° latitude, more near the equator; median w50 25 Hz, w10 60 Hz; all predicted. Measured: 20–35 Hz near a libration minimum | "EME focused" 6 Hz; the widest preset, "Aurora worst", 55 Hz; "None" 0 | 6 Hz is a 1,296 MHz median (between w50 4 Hz and w10 10 Hz) or a deep 5.76 GHz minimum: a quarter of the median w50 and a fourteenth of the median limb-to-limb width. Every preset lies below the median limb-to-limb width |
| Fading | Rayleigh-like within a tone, no steady component; sharply peaked spectrum with wings to the limb-to-limb limits; fading time scales 6–40 ms | Direct path never fades; echo-to-direct amplitude ratio varies smoothly between 0 and 1 | The lunar channel has no non-fading path. A 26.4 s bit spans about 650–4,400 fading time scales and a 16.7 ms cell 0.4–2.8 |
| Delay | Half power after about 0.5 ms (0.75 ms in chapter 40); diffuse tail; 11.6 ms extent | "EME focused" 0.5 ms (one echo); "Aurora worst" 11 ms | 0.5 ms matches the half-power delay, not the tail or the continuum of scatterers |
| Doppler shift and rate | ±18–20 kHz; up to 1.3 Hz/s; residuals of 1–20 Hz after tracking, in steps | Static Doppler offset and a constant transmit slope | An untracked 1.3 Hz/s rate moves 34 Hz across a 26.4 s bit |
| Level | 40 W into a net loss of 200 dB corresponds to two dishes of about 2.8 m (2.6–3.0 m) | 40 W, 200 dB | Realistic for 3 m-class stations |
| Noise | 50–110 K on cold sky (−181.6 to −178.2 dBm/Hz); about −176 dBm/Hz adverse; Moon, rain and ground change it by tenths of a dB to several dB, and the Sun or heavy rain by more | −164 dBm/Hz (2,883 K), constant | 14–16 dB pessimistic for real receivers. The simulated 10 dB-Hz equals a pair of about 1.2 m dishes with a real 100 K receiver; 200 dB with 100 K would give 24.6 dB-Hz, and the 15 kW Helper carrier 50.4 dB-Hz instead of 35.8 dB-Hz |
| Interference | Intermittent and continuous wireless-LAN channels, pulsed emitters | One tone; single-sample impulses | No counterpart for a 20 MHz wireless-LAN channel |

The Sub9, HF and Aurora presets describe other channels; "Aurora worst" is the nearest in Doppler and delay magnitude but not in structure.

## Contradictions, open questions, and things not found

1. **Lunar brightness temperature.** [P372] (P.372-16 and the current P.372-18) gives 140–280 K with phase, independent of frequency above 1 GHz; [KT63], [Linsky73] and the Krotikov–Pelyushenko (1987) compilation ([Hafez13]) show 210–240 K with only ±2–7 % variation at 3–10 cm, about ±4 % when interpolated to 5.76 GHz. The centimetre-wave literature is preferred for 5.8 GHz; the P.372 swing corresponds to frequencies near 100 GHz in the 1987 compilation. Absolute means still differ by about 10 %: 210–221 K in [KT63] and the 1987 compilation against 238.5 ± 9.2 K at disc centre in [Linsky73].
2. **Spread at microwave frequencies.** Linear frequency scaling of VHF widths fails above 2 GHz ([Hbk10], [K1JT10]); at 24 GHz the central peak nearly vanishes ([MT13]). The 5,760 MHz w50 and w10 figures are predictions; the only direct 5.7 GHz measurements found are two visual widths near a libration minimum, 10–20 % below prediction ([G3WDG10]), and no measured 5.7 GHz spectrum shape was found. Nothing was found contradicting the limb-to-limb limit itself.
3. **Threshold figures disagree by source.** QRA64A: −26 dB ([W3SZ19], quoting the user guide) versus −28.1 dB for a full message ([Venice16]). JT65: −25 dB for all submodes ([UG27]) versus about −25/−24/−23 dB for A/B/C in the 2005 simulations ([JT65-05]). The "Doppler spread" of the current Q65 simulator is the full support of a truncated Lorentzian ([Q65sim]); whether the 2021 curves were made with this version was not checked.
4. **Link budget versus observation.** Agreement is good for beams wider than the Moon; a 2–4.5 dB shortfall is reported for a 0.3° transmit beam at 10 GHz ([OE2IGL-10]). The 5.76 GHz narrow-beam factors above are a crude estimate.
5. **Historical details.** First operation of the Communication Moon Relay is given as 1959 ([vanKeuren97], [Jaffe66]) with public inauguration in January 1960. [Jaffe66] dates Project Diana to 11 January 1946, other accounts to 10 January. [DeWitt49] cites about 15 kW peak for one series; the commonly quoted 3 kW was not checked. Early theory ([Grieg48]) expected teleprinter-only bandwidth; [Trexler58] measured far less delay spread at 198 Mc, but at C band the diffuse share is larger. The Communication Moon Relay frequency is "around 400 megahertz" in [vanKeuren97] but "438–455 mc" in [EW60], and the 1960 demonstration used four teletypewriter channels against the sixteen-channel capacity in [vanKeuren97].
6. **Ionospheric scintillation on EME paths.** No measurement at 5–6 GHz on a lunar path was found. The C-band figures here are 4 GHz geostationary-satellite statistics from Hong Kong and Taipei ([P531] Fig. 14) scaled in frequency, and the high-latitude figure is a GPS L-band storm measurement ([JO18]) scaled likewise. [P531] gives two scaling rules, S4 ∝ f^−1.5 (§5.1) and peak-to-peak dB ∝ f^−1.5 (§5.8), which differ by about 1 dB at 5.76 GHz for a 14 dB event at 4 GHz; [P618] Table 1 interpolates to about 6 dB. How the fades of the two passes combine, and how an extended, already-fading target changes them, is unknown.
7. **Historical missing-source record; current per-item dispositions below.** TRSSCOMM operating frequency, data rate and availability statistics (the NSA history is heavily redacted; frequency unverified). Availability or error-rate statistics for the Communication Moon Relay beyond the mutual-visibility statement of [EW60]; the NRL sources cited by [vanKeuren97], including L. A. Gebhard's NRL Report 8300 (1979), whose DTIC copy refused automated access. DUBUS articles (links dead). EMECalc documentation, RSGB and ARRL microwave handbooks. The C-band scintillation papers behind [P531] Fig. 14 (D. J. Fang and M. S. Pontes, COMSAT Tech. Rev. 11, 293–320, 1981; T. A. Mollen, C. H. Liu and D. J. Fang, Radio Sci. 23, 337–345, 1988; identified through a NASA Propagation Information Center review, N94-14671) and any C-band measurement at auroral latitudes. **Source-access update:** Yang et al.'s 2019 Chang'E-2 polar radiometer paper (DOI 10.1155/2019/3940837), described as snippet-only in the earlier research pass, was inspected in full via an open article copy on 2026-11-13 [Yang19]. It analyzes passive regional brightness temperatures at 3, 7.8, 19.35 and 37 GHz; the authors report a nearly constant negative bias, likely from cold-reference antenna heat contamination, while retaining variation trends. This is not a disc-integrated 5.76 GHz system temperature and does not measure a coherent EME scattering kernel; channel-2 data also depart from earlier results. A formal Q65 design paper beyond the quick-start guide and user guide (searched). Analysis of the 2021 LoRa delay–Doppler data beyond the press release and data-set description (searched; a promised conference presentation was not found). Project Echo's preliminary moon-bounce tests [recollection, unverified]. The sense of circular polarisation used at 5,760 MHz.
8. **Coverage and transfer of recent literature.** The earlier 2015– 2026 search was targeted rather than systematic. The 2026-11-13 source/data pass below supplies actual query routes and individual dispositions; it does not assert an exhaustive census. Gao et al. 2023 is local Mini-RF dielectric inversion (abstract/index record accessible; publisher full-text fetch blocked); Campbell 2025 is a 12.6/70-cm Stokes-polarimetry paper (publisher preview/abstract only) and cautions that physical labels assigned to polarimetric components are non-unique; Fassett et al. 2025 is an LPSC abstract on 98 local 4.2/12.6-cm bistatic SAR collects, where incidence angle dominates median response and phase angle has modest median variation; JPL IPN 42-216C is full text but models a lunar-surface asset downlink to a directional DSN receiver; and the 2024 Mini-RF paper is monostatic S-band image correction. None measures a disc-integrated 5.8-GHz EME scattering function. Keep local/orbital radar and one-way lunar downlink as transfer controls only, with their geometry and observable named.
9. **Different estimators across chapters.** The half-power delay at 5.76 GHz is 0.53 ms here (log-interpolation of [K1JT10]'s figures from Hagfors) and about 0.75 ms in chapter [40](40-eme-lunar-scattering-and-geometry.md) (Lincoln Laboratory law, surface-area weighting); coherence times here are inverse widths (17–40 ms at the median), while chapter 40's field-correlation estimate is 13 ms to a correlation of 0.5. These are different estimators applied to related data, not conflicting measurements.

## Completion pass: systems, data and calibration (2026-11-13)

The historical search notice records earlier limitations. Its source obligations
have now been individually assessed below. The mathematical EME scenario is
fixed in [84](84-scenarios-eme.md), while empirical target-band validation is a
separate unmet requirement. Source access, a model definition and an actual
channel/receiver measurement must not be conflated.

### Concrete public data routes and what they contain

**433.8 MHz SigMF.** [Zenodo5832579](https://zenodo.org/records/5832579),
version 1, is the Telkamp–Zeppenfeldt–van Muijlwijk–Dijkema dataset, published
2022-01-29 for the 2021-10-05 Dwingeloo experiments. This is single-station
self-EME, not the two-Earth-station 5.76 GHz scenario. The record warns that the
computer was not synchronized: file creation timestamps and guessed
first-sample timing are distinct; logs use local CEST. Preserve that warning
instead of treating a metadata UTC-like field as a surveyed precision epoch.

| Actual inspected artifact | Content / calibration consequence |
| --- | --- |
| [LoRa receive JSON](https://zenodo.org/records/5832579/files/camras-rx-2021_10_05_14_38_49.sigmf-meta?download=1) | `cf32_le`, SigMF1.0.0, sample rate2000000, frequency433800000 Hz, sample_start0, datetime 2021-10-05T13:38:53.200000; hardware description names LR1110 and Ettus B210; annotations empty |
| [CW transmit/receive JSON](https://zenodo.org/records/5832579/files/camras-txrx-2021_10_05_13_31_37.sigmf-meta?download=1) | Same complex float format/sample rate/frequency; datetime 2021-10-05T12:31:41.2; CW description; empty annotations |
| Matching IQ file listings | LoRa receive96.0 MB, displayed MD5`6e525853ef03204e4d42c2756c29a943`; CW80.0 MB, MD5`9372d0232c1b102c4156f1e956fcc7e3`. Checksums were read from the repository listing, **not verified against downloaded sample bytes** |
| Metadata license fields | Explicit `https://creativecommons.org/licenses/by/4.0/` in both inspected JSON files; preserve creator attribution on reuse |
| [Horizontal receiver log](https://zenodo.org/records/5832579/files/lora_log_horizontal.txt?download=1),13:05:22/25 and13:11:06/08 entries | Direct/echo-like pairs report decoded six-byte `PI9CAM`; the first pair’s time difference is about 2.435 s and the later echo-like entry reports SNR−16 dB. Earlier log entries have nonsensical lunar “Distance” values, so labels alone are not calibrated ranges or evidence that every entry is an echo |

The log is an **instrument report**, not an independent decode performed here.
`BW:1` is an unexplained device enum; it is not safe to infer bandwidth1 Hz or
an exact LoRa noise reference from that integer. RSSI/SNR fields do not provide
ADC-to-watt calibration, receiver noise temperature, front-end linearity,
polarization response or a denominator of all attempted packets. The dataset
can support its own-band relative echo delay, frequency shift and packet
reproduction after aligning actual transmit/receive windows. It cannot by
itself estimate natural 5.76 GHz success probability, calibrated C/N0 or
whole-disc short-wavelength scattering. No raw IQ, measured delay–Doppler
surface or complete packet-denominator analysis was produced in this pass.
The recorded 210 kHz carrier offset must be accounted for before decimation;
it is not an unexplained lunar Doppler shift.

**Orbital radar.** Official [PDS/ODE Mini-RF documentation](https://ode.rsl.wustl.edu/Moon/pagehelp/Content/Missions_Instruments/Lunar%20Reconnaissance%20Orbiter%20(LRO)/Mini-RF/Derived.htm)
links the PDS4 bundle `urn:nasa:pds:lro_minirf_bistatic`. The inspected
[archive root](https://pds-geosciences.wustl.edu/lro/urn-nasa-pds-lro_minirf_bistatic/)
and [readme](https://pds-geosciences.wustl.edu/lro/urn-nasa-pds-lro_minirf_bistatic/readme.txt)
are concrete public acquisition routes. The ODE product note says PDS4 adds
calibration/housekeeping and combines the real and imaginary calibrated
components previously stored separately. Derived Stokes/CPR products and
geometry backplanes remain **local Earth-to-LRO SAR**, not an Earth receiver’s
whole-disc voltage time series. Product labels/calibration version must be
checked before using them; public availability is not confirmation of a
particular processing correction or target-band suitability.

A primary [2011 Mini-RF quick-look memo](https://pds-geosciences.wustl.edu/lro/lro-l-mrflro-2_3_5-bistatic-v3/lromrf_3xxx/document/bistatic.htm),
“Raw Pulse Power” and “Spectrum of the Pulsed-CW Data,” describes asynchronous
Arecibo pulses, low-bit quantization and a horizontal-channel clipping/image
problem. This is direct counterevidence to treating archived IQ as automatically
ideal or calibrated. Its rough budget agreement is explicitly qualified by
the authors. New Peters 2026/Chan 2025 calibration material in chapter 40 further
requires polarimetric correction; no whole-disc law has been reconstructed here.

**Target-band route result.** Exact 5.7/5.76/5.8 GHz spectrum/recording searches,
Zenodo, official PDS/ODE and current ngRadar records did not identify a
calibrated public 5–6 cm whole-disc two-station record with the required station,
time, beam and noise metadata. This is a bounded search result, not proof that
no such measurement exists. The nearest inspected raw-data candidate above is
433.8 MHz; local 4.2 cm SAR and 13.9 GHz Green Bank images do not fill its role.

### Recovered scintillation source and fixed thermal interpretation

The previously missing [Fang–Pontes1981 original COMSAT paper](https://www.comsat-legacy.org/COMSAT%20Technical%20Review/CTR%20Fall%201981,%20V.11--2.PDF)
was inspected at printed pp. 293–320, especially measurement setup/Tables 1–2
(pp. 295–299), temporal behavior (pp. 304–307) and statistics/conclusions
(pp. 310–317). Hong Kong monitored satellite beacons and communication carriers
in 1977– 1980, with separate uplink/downlink frequencies and geographically
different paths. It reports strong events up to 14 dB peak-to-peak and breakdown
of weak-scattering frequency-power scaling. The worst-month statistic has a
specified threshold/denominator; it is not a bit-failure probability. This
remedies access to a decision-driving original source but **does not** measure
two 5.76 GHz lunar legs or their dependence. Strong-event frequency scaling
and an assumed independent product of two fades therefore cannot be promoted
to a calibrated EME scenario.84 uses fixed smooth attenuation controls instead.

For thermal calculations, distinguish brightness temperature Tb from
beam-weighted antenna temperature and full on-Moon Tsys. For a centred Gaussian
beam on a uniform disc, the small-angle fill fraction is
`F=1−exp[−4ln2(rMoon/β)²]`; a receiver term plus
`Tbackground+(Tb−Tbackground)F` gives a useful explicit comparator. Receiver
loss and spillover require their own reference planes. The 225 K brightness
in 84 is an assumed intermediate centimetre-wave value;80 K is its assumed
**total** on-Moon system temperature. They are not quantities to add. Narrower
beams increase both signal gain/coupling and lunar noise; compare both changes.
Yang 2019’s already inspected regional measurements/bias are retained rather
than misrepresented as an independently calibrated disc-integrated 5.76 GHz
antenna temperature. Exact topographic/thermal maps remain an optional,
calibration-dependent extension.

### Individual disposition of the historical list

| Original source/topic | Current decision and residual scope |
| --- | --- |
| TRSSCOMM frequency | Unverified in available redacted history; withdraw as a numerical input or evidence of operation at the scenario carrier |
| TRSSCOMM rate/availability | No quantified calibrated performance claim retained; historical existence does not set a modem threshold |
| CMR BER/availability | Not established beyond the cited visibility/history; no success probability inferred |
| Gebhard NRL 8300 / original NRL sources | Prior DTIC refusal retained. NASA history is sufficient for nonnumerical context; inaccessible report supplies no scenario parameter |
| DUBUS articles / dead links | Accessible author reprints MT13/Echo22/G3WDG retained at their exact scope; missing articles supply no unique threshold or spectrum parameter |
| EMECalc documentation | Not a selected authority;84 supplies explicit radar/beam/noise equations and assumptions instead of opaque tool defaults |
| RSGB microwave handbook | Not obtained; no selected parameter depends on unseen text |
| ARRL microwave handbook | Not obtained; existing Hbk10 is identified separately, not represented as this missing book |
| Fang–Pontes 1981 | Access remedied by full original issue; conditions and transfer limit above |
| Mollen–Liu–Fang 1988 | Exact primary title/DOI established: *A study of C band equatorial scintillation in the Asian sector*,10.1029/RS023i003p00337; publisher route failed. Fang 1981 provides direct source evidence, without claiming this distinct study was read |
| C-band auroral scintillation | No matching lunar-path measurement found; GPS L-band storm scaling remains a sensitivity analogy only |
| Yang 2019 | Earlier full-text remedy retained, with regional brightness/bias limits; no redundant claim of a new reading |
| Formal Q65 design paper | Accessible quick-start, user guide and simulator remain actual authorities. A formal paper is not invented; exact 2021 curve/simulator revision remains unverified, so thresholds are not transfer-calibrated to Robust |
|2021 LoRa analysis/presentation | Actual metadata and receiver log inspected above; promised presentation remains unlocated. Raw-IQ decoding/channel estimation is unexecuted and no success-denominator claim is made |
| Project Echo preliminary tests recollection | Withdrawn as evidential support; no implementation/design claim depends on it |
| Circular sense used by all 5,760 MHz stations | Not inferred as universal.84 defines the propagation-direction convention and separate receiver-port controls; cited practical feed reports remain station-specific |
|5.7 GHz measured spectrum / widths | Visual widths near libration minima remain grey evidence with unspecified detection threshold/window, not a measured full power spectrum |
| Whole-disc 5.76 GHz IQ | Open empirical qualification dependency; targeted routes/results above |
| Recent 2015– 2026 lunar work | Expanded with 2026 journal architecture/calibration,2025 abstracts and actual archive routes; geometry/estimator exclusions below. No exhaustive-literature claim |

### Search receipt and estimator reconciliation

All queries here were executed 2026-11-13. In addition to the exact-query table
in [40](40-eme-lunar-scattering-and-geometry.md), this systems pass used
`"Fang" "Pontes" "1981" scintillation` (original COMSAT issue),
`"Mollen" "Liu" "Fang" "1988" scintillation` (publisher contents/DOI),
`"lunar" "433.8" "delay Doppler" Telkamp` (Zenodo), and direct opens of the
actual JSON/log links above. PDS/ODE product documents were followed from
`site.pds-geosciences.wustl.edu lunar radar 4.2 cm Mini RF bistatic data`.
These are source/contents receipts, not a claim to have processed large binary
archives. A shell download attempt for the alternative historical ZPC scan
failed at DNS resolution; web metadata/text routes remained usable. No source
was upgraded solely because its URL existed.

The selected recent literature now includes 2017 long-wave and bistatic radar,
2019 radiometry/downlink modeling,2023 dielectric inversion,2024 topographic
image correction,2025 local bistatic/calibration studies and 2026 journal
instrument processing. Local SAR, passive regional thermal radiation and
one-way downlinks are excluded as direct whole-disc 5.76 GHz calibration.
Peters 2026’s journal paper specifically corrects the old missing-journal claim;
its existence does not remove the missing empirical EME record.

Delay t50 means integrated power below excess delay, RMS means deviation about
mean delay, and support means the entire geometric interval. The 0.53 ms
K1JT interpolation, chapter 40 full-table≈0.75 ms and 84 sparse-table 0.708 ms
are different reconstructions, not three measurements at 5.76 GHz. Surface-area
weighting must follow LL67; projected-area weighting is a different kernel.
Similarly limb-to-limb Doppler support, central power-containing band, PSD
FWHM, PSD-autocorrelation width and visually detected extent are distinct.
Inverse width is not interchangeable with field-correlation threshold time.
Any future data comparison must publish integration time, window, resolution,
noise subtraction and censoring before treating a discrepancy as model failure.

## References

Access status: "read" means the cited passage was read in full text; page pointers are in the body.

**Standards [S]** (all read)
- [P372] Rec. ITU-R P.372-16 (08/2022), *Radio noise*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.372-16-202208-S!!PDF-E.pdf. Equation numbers in the text refer to this edition. The edition in force, P.372-18 (09/2026), https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.372-18-202609-I!!PDF-E.pdf, was also read: its §4.1 lunar and solar statements are unchanged; its sky-brightness equation is now eq. (10), with a frequency-dependent T_mr (275 K retained for rain), and the galactic scaling is eq. (16).
- [P531] Rec. ITU-R P.531-16 (09/2025), *Ionospheric propagation data and prediction methods required for the design of satellite networks and systems*, the edition in force. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.531-16-202509-I!!PDF-E.pdf. Section and equation numbers in the text refer to it, and Fig. 14 was read from it. The superseded P.531-14 (08/2019), https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.531-14-201908-S!!PDF-E.pdf, gives the same statements (Faraday formula as eq. 2, peak-to-peak conversion as eq. 6, equatorial statement in §5.2).
- [P618] Rec. ITU-R P.618-14 (08/2023), *Propagation data and prediction methods required for the design of Earth-space telecommunication systems*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.618-14-202308-I!!PDF-E.pdf
- [P676] Rec. ITU-R P.676-13 (08/2022), *Attenuation by atmospheric gases and related effects*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.676-13-202208-I!!PDF-E.pdf
- [P834] Rec. ITU-R P.834-9 (12/2017), *Effects of tropospheric refraction on radiowave propagation*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.834-9-201712-I!!PDF-E.pdf
- [P838] Rec. ITU-R P.838-3 (03/2005), *Specific attenuation model for rain for use in prediction methods*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.838-3-200503-I!!PDF-E.pdf
- [P840] Rec. ITU-R P.840-9 (08/2023), *Attenuation due to clouds and fog*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.840-9-202308-I!!PDF-E.pdf

**Peer-reviewed [P]**
- [DeWitt49] J. H. DeWitt Jr and E. K. Stodola, "Detection of radio signals reflected from the Moon", *Proc. IRE* 37(3), 229–242, March 1949. Scan: https://www.worldradiohistory.com/Archive-IRE/40s/IRE-1949-03.pdf — read pp. 229, 239, 240.
- [Grieg48] D. D. Grieg, S. Metzger and R. Waer, "Considerations of moon-relay communication", *Proc. IRE* 36(5), 652–663, May 1948. Scan: https://www.worldradiohistory.com/Archive-IRE/40s/IRE-1948-05.pdf — read pp. 652, 662–663.
- [Sulzer52] P. G. Sulzer, G. F. Montgomery and I. H. Gerks, "An U-H-F moon relay", *Proc. IRE* 40(3), 361, March 1952. Scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1952-03.pdf — read.
- [Trexler58] J. H. Trexler, "Lunar radio echoes", *Proc. IRE* 46(1), 286–292, January 1958. Scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1958-01.pdf — read.
- [Yaplee58] B. S. Yaplee, R. H. Bruton, K. J. Craig and N. G. Roman, "Radar echoes from the Moon at a wavelength of 10 cm", *Proc. IRE* 46(1), 293 ff., January 1958 (same scan) — read first page.
- [Gibson58] J. E. Gibson, "Lunar thermal radiation at 35 kMc", *Proc. IRE* 46(1), 280 ff., January 1958 (same scan) — read summary and method.
- [KT63] V. D. Krotikov and V. S. Troitskii, "Radio emission and nature of the Moon", *Usp. Fiz. Nauk* 81(4), 589 ff. (52 pp.), 1963 (English: *Sov. Phys. Usp.* 6, 841, 1964; not opened). Russian original: https://ufn.ru/ufn63/ufn63_12/Russian/r6312a.pdf — read Table II.
- [Linsky73] J. L. Linsky, "The Moon as a proposed radiometric standard for microwave and infrared observations of extended sources", *Astrophys. J. Suppl.* 25, 163–204, 1973. https://articles.adsabs.harvard.edu/pdf/1973ApJS...25..163L — read abstract, Table 6, Fig. 15.
- [McKinley13] B. McKinley et al., "Low frequency observations of the Moon with the Murchison Widefield Array", arXiv:1211.1433 (to appear in *Astron. J.*). https://arxiv.org/abs/1211.1433 — abstract.
- [JO18] Y. Jin and K. Oksavik, "GPS scintillations and losses of signal lock at high latitudes during the 2015 St. Patrick's Day storm", *J. Geophys. Res. Space Phys.*, 2018, doi:10.1029/2018JA025933. Author copy: https://folk.uib.no/st07350/papers/jin2018JA025933.pdf — read key points, introduction and conclusions, observations in part.
- [Yang19] F. Yang, Y. Xu, K. L. Chan, X. Zhang, G. Hu and Y. Li, "Study of Chang’E-2 Microwave Radiometer Data in the Lunar Polar Region", *Advances in Astronomy* 2019, Article ID 3940837. https://doi.org/10.1155/2019/3940837 — full open-access article copy inspected 2026-11-13, §§2–3 and conclusion; channel frequencies, footprint/resolution, temperature accuracy and author-described cold-reference bias checked. This passive orbital radiometry is regional lunar brightness-temperature evidence, not a measured Earth–Moon–Earth echo or 5.76 GHz disc-integrated antenna-temperature record.

**Books, handbooks and technical reports [B]/[T]**
- [vanKeuren97] D. K. van Keuren, "Moon in their eyes: Moon communication relay at the Naval Research Laboratory, 1951–1962", ch. 2 in A. J. Butrica (ed.), *Beyond the Ionosphere: Fifty Years of Satellite Communication*, NASA SP-4217, 1997, pp. 9–18. https://history.nasa.gov/SP-4217/ch2.htm (served as the whole-book scan) — read [B].
- [Agar97] J. Agar, "Moon relay experiments at Jodrell Bank", ch. 3 in the same volume, pp. 19 ff. — read pp. 19–26 [B].
- [Jaffe66] NASA Office of Space Science and Applications (L. Jaffe, director), *Satellite Communications: Six Years of Achievement, 1958 through 1964*, 1966. https://ntrs.nasa.gov/api/citations/19660004987/downloads/19660004987.pdf — read Moon Relay passages [T].
- [NavySpace] US Navy, *From the Sea to the Stars*, ch. 2 §2.3.1. https://web.archive.org/web/20030827064907/http://www.history.navy.mil/books/space/Chapter2.htm — read [T].
- [NSA-TRS] National Security Agency, *A Review of the Technical Research Ship Program 1961–1969* (redacted release). https://www.governmentattic.org/5docs/ReviewTechResearchShipPgm_1961-1969u.pdf — read TRSSCOMM passages [T].
- [Hbk10] J. Taylor (K1JT), "Earth-Moon-Earth (EME) communication", §§30.8–30.13 of ch. 30, *The ARRL Handbook*, 2010. https://wsjt.sourceforge.io/Hbk_2010_Ch30_EME.pdf — read [B] (amateur handbook).
- [RSTN] US Air Force Radio Solar Telescope Network, Palehua noon solar radio flux, yearly files for 2009, 2014, 2019 and 2024, NOAA National Centers for Environmental Information archive. https://www.ngdc.noaa.gov/stp/space-weather/solar-data/solar-features/solar-radio/noontime-flux/palehua/ (format description in its `documentation/` folder) — read; 4,995 and 8,800 MHz columns used [T].

**Grey literature [G]** (all read unless noted)
- [K1JT10] J. Taylor, "Frequency-dependent characteristics of the EME path", 14th Int. EME Conf., Dallas, 2010. https://wsjt.sourceforge.io/EME2010_K1JT.pdf
- [K1JT08] J. Taylor, "Quest for optimum coding and modulation schemes for EME", 13th Int. EME Conf., Florence, 2008. https://wsjt.sourceforge.io/EME_Florence_2008.pdf
- [JT65-05] J. Taylor, "The JT65 communications protocol", *QEX*, Sept/Oct 2005. https://wsjt.sourceforge.io/JT65.pdf — read threshold passages.
- [FT16] S. Franke and J. Taylor, "Open source soft-decision decoder for the JT65 (63,12) Reed-Solomon code", *QEX*, May/June 2016. https://wsjt.sourceforge.io/FrankeTaylor_QEX_2016.pdf — read §§6–7.
- [LE16] J. Taylor, "High-accuracy prediction and measurement of lunar echoes", *QEX*, Nov/Dec 2016, 36–40. https://wsjt.sourceforge.io/LunarEchoes_QEX.pdf (the copy read carries the footer "QEX September/October 2016" and its own page numbers 1–5; issue and pages here follow the author's publication list)
- [MT13] R. Moncur (VK7MO) and J. Taylor, "Small station EME at 10 and 24 GHz: GPS locking, Doppler correction, and JT4", *DUBUS* 2/2013. https://wsjt.sourceforge.io/small_station_eme.pdf
- [Venice16] J. Taylor, "WSJT-X: new codes, modes, and tools for weak-signal communication", slides, 17th Int. EME Conf., Venice, 2016. https://wsjt.sourceforge.io/K1JT_EME_2016_Venice.pdf
- [Q65QS] J. Taylor, B. Somerville, S. Franke and N. Palermo, "Quick-start guide to Q65", 3 April 2021. https://wsjt.sourceforge.io/Q65_Quick_Start.pdf
- [UG27] *WSJT-X 2.7.0 User Guide*, §§6.5, 8–9, 18, 19. https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.pdf
- [Echo22] B. Atkins (KA1GT), C. Suckling (DL3WDG) and J. Taylor, "Echo mode in WSJT-X 2.6.0", *DUBUS* 4/2022. https://wsjt.sourceforge.io/Echo_Mode_in_WSJT-X_2.6.0.pdf
- [W3SZ19] R. Rehr (W3SZ), "VHF/UHF/Microwave: which JT mode should I choose?", VHF Super Conference, April 2019. https://w3sz.com/W3SZ-VHF-UHF-uWave-NewDataWhichJTModeNew4-3-19.pdf — read in part.
- [OK2AQ22] M. Kasal (OK2AQ), "MW EME with small offset dish", slides, 19th Int. EME Conf., Prague, 2022. https://www.radio.feec.vutbr.cz/esl/files/EME/Doc/EME2022_OK2AQ.pdf
- KA1GT web pages (B. Atkins, 2018–2025), https://bobatkins.com/radio/ : [KA1GT-Q65] q65_mode_selection.html; [KA1GT-Echo] EME-echoes.html; [KA1GT-NM] new_moon_EME.html; [KA1GT-CP] EME_circular_polarization_loss.html; [KA1GT-101] EME_101.html; [KA1GT-Sun] sun_noise.html; [KA1GT-DL0SHF] DL0SHF_10GHZ_beacon.html; [KA1GT-CFOM] CFOM.html; [KA1GT-Step] Q65_step_size_drift_compensation.html; [KA1GT-10G] 10Ghz_EME_rx_4.html.
- OE2IGL web pages (G. Ihninger, 2022–2026), https://www.wettersat.bplaced.net/ : [OE2IGL-10] eme10ghz.html; [OE2IGL-24] eme24ghz.html; [OE2IGL-LB] emelinkbudget.html.
- [NL2506], [NL2609], [NL2610] *432 and Above EME News* (founded by K2UYH): issue pages https://eme.radio/432-and-above-newsletter/432-and-above-2025-06 and https://eme.radio/432-and-above-newsletter/432-and-above-2026-09 ; October 2026 (vol. 55 no. 10) PDF https://eme.radio/images/newsletter/pdf/2026-10-vol-55-10.pdf
- [Lacuna21] Lacuna Space, "First LoRa message bounced off the moon", 24 Nov 2021. https://lacuna.space/lora-moon-bounce/
- [Zenodo22] T. Telkamp, F. Zeppenfeldt, J. van Muijlwijk and T. J. Dijkema, "SDR moonbounce signals at 433.8 MHz", data set, 29 Jan 2022, doi:10.5281/zenodo.5832579 — record, two actual SigMF metadata JSON files and horizontal receiver log inspected 2026-11-13; raw IQ not analyzed; exact receipts below.
- [Wiki-TRSSCOMM] Wikipedia, "Technical Research Ship Special COMMunications" (tertiary; used only for the 10 kW figure). https://en.wikipedia.org/wiki/Technical_Research_Ship_Special_COMMunications
- [EW60] "Revolutionary moon-relay communications system unveiled by Navy", Spot News, *Electronics World*, April 1960 (contemporary trade report). Transcription: https://www.rfcafe.com/references/electronics-world/electronic-industry-news-electronics-world-april-1960.htm — read.
- [G3WDG10] C. Suckling (G3WDG), "Predicting libration fading on the EME path", 14 June 2010 (the "G3WDG libration paper"). https://bobatkins.com/radio/g3wdg_libration.html — read.
- [Q65sim] WSJT-X source file `lib/qra/q65/q65sim.f90`, master branch as retrieved on 13 November 2026. https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/lib/qra/q65/q65sim.f90?format=raw — read (software source).
- [CT1DMK06] L. Cupido (CT1DMK) and W. Bauer (LX1DB), "Circular polarized antenna feed (for EME on 10 GHz and 5.7 GHz)", 2006 (date from the file). https://www.qsl.net/ct1dmk/cp_feed_dmk06.pdf — read p. 1 and the echo-test section.
- [OH2AUE] OH2AUE, "6 cm EME equipment", undated web page. https://www.qsl.net/oh2aue/eme6cm.htm — read.
- [Hafez13] Y. A. Hafez, L. Trojan, F. H. Albaqami, A. Z. Almutairi, R. D. Davies and C. Dickinson, "A radio determination of the time of the New Moon", arXiv:1312.5956, 2013 (preprint in MNRAS format; journal version not checked). https://arxiv.org/abs/1312.5956 — read Table 2 and §3; its lunar values are secondary for V. D. Krotikov and S. A. Pelyushenko (1987), not seen.
- [GAO23] Gao, Y. et al. (2023). Investigating the dielectric properties of lunar surface regolith fines using Mini-RF SAR data. *ISPRS Journal of Photogrammetry and Remote Sensing* 197, 56–70. https://doi.org/10.1016/j.isprsjprs.2023.01.008 — abstract/index record read; publisher full-text fetch blocked. [P]
- [CAMPBELL25] Campbell, B. A. (2025). Regolith properties and Stokes vector radar polarimetry models for the moon. *Icarus* 441, 116738. https://doi.org/10.1016/j.icarus.2025.116738 — publisher preview/abstract read. [P]
- [FASSETT25] Fassett, C. I. et al. (2025). Mini-RF X/C-band bistatic radar observations of the Moon as a function of incidence and phase angles. 56th Lunar and Planetary Science Conference, abstract 2129. https://www.hou.usra.edu/meetings/lpsc2025/pdf/2129.pdf — abstract read. [G]
- [JPL216C] Sanchez-Net, M. (2019). Analysis of the fading channel in downlinks from the lunar South Pole to the Deep Space Network. *IPN Progress Report* 42-216C. https://ipnpr.jpl.nasa.gov/progress_report/42-216/42-216C.pdf — full report read; one-way lunar asset-to-DSN geometry. [T]
- [KIRK24] Improved orthorectification and empirical reduction of topographic effects in monostatic Mini-RF S-band observations of the Moon. *The Planetary Science Journal* 5(1), 4 (2024). https://doi.org/10.3847/PSJ/ad0a61 — abstract checked; local image correction. [P]

- [FANG81] Fang, D. J., Pontes, M. S. (1981). *4/6-GHz ionospheric scintillation measurements during the peak of sunspot cycle 21*. COMSAT Technical Review 11(2),293–320. [Original issue](https://www.comsat-legacy.org/COMSAT%20Technical%20Review/CTR%20Fall%201981,%20V.11--2.PDF) — measurement/statistics/conclusion sections inspected 2026-11-13; satellite-link evidence. [P]
- [MOLLEN88] Mollen, T. A., Liu, C. H., Fang, D. J. (1988). *A study of C band equatorial scintillation in the Asian sector*. Radio Science 23,337–345. https://doi.org/10.1029/RS023i003p00337 — publisher contents record/title confirmed; full text not obtained. [P]
- [PDS-MRF] NASA PDS Geosciences/ODE. [Mini-RF derived-data documentation](https://ode.rsl.wustl.edu/Moon/pagehelp/Content/Missions_Instruments/Lunar%20Reconnaissance%20Orbiter%20(LRO)/Mini-RF/Derived.htm), [PDS4 archive](https://pds-geosciences.wustl.edu/lro/urn-nasa-pds-lro_minirf_bistatic/) — product/route text inspected 2026-11-13; binary products not analyzed. [T]

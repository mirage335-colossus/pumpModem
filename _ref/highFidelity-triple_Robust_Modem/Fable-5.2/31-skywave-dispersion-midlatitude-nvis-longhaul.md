# Skywave dispersion at mid-latitudes, on near-vertical paths and on long paths

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** signal level [30](30-skywave-signal-level-and-outages.md); high- and low-latitude extremes [32](32-skywave-dispersion-highlatitude-equatorial.md); channel models [33](33-hf-channel-models-and-simulators.md); independent arithmetic [90](90-cross-check-calculations.md).

**Summary.** The simulator preset "HF best: 0.1 Hz, 0.5 ms" is exactly ITU-R F.1487 "mid-latitude quiet" (the old CCIR "good" channel), "HF moderate: 10 Hz, 6 ms" is F.1487 *low-latitude* disturbed, and "HF worst: 30 Hz, 7 ms" is exactly F.1487 "high-latitude disturbed". No source read reports 30 Hz spread on a mid-latitude, NVIS or long-haul path. F.1487 gives no occurrence statistics for mid-latitudes; UK measurements on 1,440 km and 1,800 km paths along the mid-latitude trough give median composite (80 % power) Doppler spreads at the sounder's 0.5–0.6 Hz resolution limit and 95th percentiles of 1.5–2.5 Hz in most conditions, rising to 5–13 Hz at 10–14 MHz on winter and equinox nights at sunspot maximum, when reflections come from the trough wall or the auroral zone; their 95th-percentile composite multipath spreads reach 2.2–3.6 ms below 11 MHz at sunspot maximum and stay below 2.5 ms at minimum. The one detailed mid-latitude validation (Watterson, 1,294 km, 1967) found 3–4 unequal modes within 1.1 ms and per-mode 2σ spreads of 0.006–0.34 Hz, in three 10–13 minute samples chosen as the most nearly stationary parts of about 17 hours of recordings (NTIA 87-221 reads the selection as about 12 % of the record; the primary report gives no fraction). Multi-hop and NVIS geometry add discrete echoes: 0.7–1.1 ms (1F–2F at 1,350 km), 1.7 ms (317 km), 1.9 and 3.8 ms (vertical incidence); NVIS totals reach 2.7–2.9 ms (5.5 ms with ground wave), and one night-time case study on a 170 km UK path reports about 10 ms (secondary); a 12,700 km path shows 0.2–3.2 ms and 0.1–2.4 Hz (80 % power widths). At mid-latitudes the Doppler shift moves more than the spread: about ±0.1–0.2 Hz on quiet days, 0.24–0.31 Hz median peak-to-peak from travelling disturbances with 24–40 minute periods, +0.5 to ±1 Hz around sunrise, −4 Hz on a four-hop path, ±1.5 Hz in a moderate storm. Flare-induced deviations are mostly under 1 Hz and about four minutes long, but reach about 3.4–3.9 Hz at 3.6–7 MHz vertical incidence in an X-class flare (about 7.8 Hz in an M7.9 flare at low latitude), changing at roughly 0.1–0.25 Hz/s for tens of seconds; 50 Hz is reported as an extreme. Composite fading is near Rayleigh (deciles +5/−8 dB); single modes are often Rician and slow (10–20 s correlation times). For a 225 Hz signal the channel is flat up to about 1 ms of multipath, partly selective near 1.5 ms and selective at 2–7 ms; intra-mode dispersion is negligible. Night-time MF has a delay window under 3 ms, 10–30 fades per hour, lower-decile fades lasting about 3 minutes on average at 539 kHz, and about 0.1 Hz Doppler shift, near 1 Hz at the terminators.

**Limits of this file.** The research pass's web-search budget ran out after about 25 searches; later material came from direct downloads of primary documents, from following citations, and from publisher abstracts retrieved by DOI from the Crossref record. Several named sources were therefore read only as abstracts or not obtained; they are listed in the last section and nothing beyond the abstract is attributed to them. The checking pass re-opened most sources and reconciled the Watterson values with the primary ESSA report. On re-check the publisher copies of Hervás2015, Alsina2016, Maso2020, Gonzalez2021 and Ivanov2020, and the ETSI DRM specification, refused automated access, so the values taken from them rest on the first reading.

Citation keys (for example `[F1487]`) are resolved in the References. Each quantitative item carries: location in the source; evidence grade ([S] standard, [P] research paper or preprint (peer-review status checked separately), [T] technical report, [B] book, [G] grey, [E] this record's derivation); and access status (read / abstract / secondary).

---

## Physical mechanisms

**1. Discrete modes.** A skywave signal arrives by several geometric routes: one or more hops from the E, F1 or F2 layers, low and high (Pedersen) rays for each, and an ordinary (O) and extraordinary (X) magneto-ionic component for each ray. F.1487 describes the result as components "spread in time over an interval of up to several milliseconds", each with its own Doppler shift and spread [F1487 Annex 1 §1; S; read].

A mirror model reproduces the main delays. For n equal hops over ground distance D with virtual height h on a spherical Earth of radius r, the path length is P_n = 2n·√(r² + (r+h)² − 2r(r+h)·cos(D/(2nr))) and the delay is P_n/c [E; the same formula is used in Cerwin2026 eqs. 4–7]. With r = 6,371 km, h_E = 110 km, h_F = 300 km:

| D (km) | 1F−1E (ms) | 2F−1F (ms) | 3F−1F (ms) | 4F−1F (ms) | 2E−1E (ms) |
|---|---|---|---|---|---|
| 0 (NVIS) | 1.27 | 2.00 | 4.00 | 6.00 | 0.73 |
| 300 | 1.00 | 1.88 | 3.84 | 5.82 | 0.53 |
| 500 | 0.80 | 1.72 | 3.61 | 5.56 | 0.40 |
| 1,000 | 0.51 | 1.30 | 2.95 | 4.75 | 0.23 |
| 1,500 | 0.40 | 1.00 | 2.40 | 4.01 | 0.16 |
| 2,000 | 0.35 | 0.80 | 1.98 | 3.41 | 0.12 |
| 3,000 | (1E below horizon) | 0.58 | 1.46 | 2.59 | — |

[E]. Checks against measurements: the model gives 0.66 and 1.08 ms for 2F−1F at 1,350 km with h = 225 and 300 km (Cerwin2026 Fig. 1c gives 0.67 and 1.10 ms); 1.71 ms at 317 km with h = 277 km (measured 1.7 ms); and, for Watterson's 1,294 km path with h_F = 250 km, 0.29 ms for 1F−1E and 0.82 ms for 2F−1F (measured 0.25 and 0.85 ms). Two consequences follow. Differential delay falls with path length for a fixed mode pair, so short paths are the worst. Long delays need high-order modes, which need steep take-off angles and therefore a frequency well below the MUF.

**2. Dependence on frequency relative to the MUF.** As the operating frequency approaches the MUF, the higher-order modes drop out and the high and low rays merge, so multipath shrinks toward zero. Davies gives the Bailey "multipath reduction factor" result for a 2,500 km path: spread under 0.5 ms for f/MUF of 0.85–1.0, under 1 ms for 0.65–0.85, 1–2 ms for 0.43–0.65, maximum about 3 ms; for 1,000 km the maximum is about 5 ms, near f/MUF ≈ 0.56 [Mono80 §4.8, pp. 192–193, Figs 4.32–4.33; B; read]. The O and X junction frequencies differ by about 0.8 MHz at zero distance and under 0.2 MHz at 3,000 km (east–west, United States, statistical) [Mono80 §4.6.1, text to Fig. 4.21; B; read], so between the two MUFs one magneto-ionic component can vanish.

**3. Doppler shift.** The shift is Δf = −(1/λ₀)·dP/dt, where P is the phase path [Mono80 eq. 2.128, p. 98; B; read]. If the reflection height moves without a change of layer shape, Δf ∝ f; if ionisation is added below the reflection level, Δf ∝ 1/f [Mono80 eq. 6.1, p. 263; B; read]. At vertical incidence 1 Hz corresponds to a reflection-height speed of 15 m/s at 10 MHz, 50 m/s at 3 MHz and 150 m/s at 1 MHz (v = c·Δf/2f) [E]. Higher-order modes change path length faster for the same layer motion, so their Doppler shift scales roughly with hop number and the spectrum splits into separate tracks at dawn [Collins2022p §4.2; G preprint; read].

**4. Doppler spread and fading.** Each mode is a sum of wavelets from an extended, moving, irregular reflection region. Random relative phases give Rayleigh fading; a dominant specular wavelet gives Rice fading [Mono80 §5.4.2, pp. 242–243; B; read]. Classical theory has fading time proportional to wavelength × sec(incidence angle) / irregularity velocity, so fading is faster at higher frequency and steeper incidence [TN133 pp. 5–6, 10, citing Booker, Ratcliffe & Shinn 1950 and King 1958; T; secondary]. On top of this "diffuse" fading within a mode, unresolved modes beat against one another at the difference of their Doppler shifts (interference fading), and the O and X components of one mode beat at the difference of theirs (polarisation fading).

**5. Slow structure.** Atmospheric gravity waves produce travelling ionospheric disturbances (TIDs). Medium-scale TIDs have wavelengths of hundreds of kilometres and periods of 10–60 minutes; large-scale TIDs exceed 1,000 km and 30–180 minutes [Collins2023 §4.4, citing Hunsucker 1982; Trop2025 §1; P; read]. Mid-latitude SuperDARN ground scatter gives daytime medium-scale waves at 50–250 m/s (most at 100–150 m/s), 100–500 km (peak 250 km) and 23–60 minutes, mainly from autumn to spring [Frissell2014; P; abstract; Nishitani2019; P; read]. One large-scale event had a period of about 2.5 h, a wavelength of about 1,680 km and a speed of about 1,200 km/h [Frissell2022; P; abstract]. Czech continuous Doppler sounding gives 100–220 m/s, 80–300 km and a mean wind-frame period near 18 minutes, cut off at the Brunt–Väisälä period of about 10 minutes [Chum2021 abstract, conclusions; P; read]. The terminator, flares, eclipses and storms change the phase path faster and produce the largest shifts.

**6. Intra-mode dispersion.** Group delay varies with frequency within a mode, by tens to hundreds of microseconds per megahertz, rising steeply near the MUF. It limits wideband systems, not a 225 Hz one (numbers below).

---

## Measured and reported values

### A. Estimator definitions, and why numbers from different campaigns are not directly comparable

| Estimator | Definition | Used by | Relation to a Gaussian "2σ" |
|---|---|---|---|
| 2σ frequency spread | Twice the standard deviation of a Gaussian tap-gain spectrum | Watterson; F.520; F.1487 Annex 3 §1 [S; read] | 1 |
| 68 % two-sided width | Same thing: "Dsp = 2σd", containing 68 % of the power | DRM, ETSI ES 201 980 eq. B.4 [S; read] | 1 |
| rms width | Standard deviation σ of the whole measured spectrum | Shepherd & Lomax 1967 [abstract] | 0.5 for one Gaussian; larger if several shifted modes are included |
| 80 % power width ("composite" spread) | Interval between the 10 % and 90 % points of the integrated Doppler or delay power profile, all modes together | La Salle group, after Angling & Davies and Warrington & Stocker [Vilella2008 paras 22–25; Ads2013 eqs. 9–11; Hervás2015 §4.2; P; read]; for Doppler, the Leicester group uses the narrowest band holding 80 % of the power [WS2003 §3; P; read] | 1.28 |
| Composite multipath spread (Leicester, DAMSON) | From the rising edge of the first detected mode to the falling edge of the last, corrected for the pulse width; no weighting by mode power; from 2011, modes more than 12 dB below the strongest are excluded. The "effective" multipath spread weights mode pairs by their power ratio | WS2003 §3; SW2011 §2; P; read | — |
| Full width at half maximum | From a Gaussian fit | Aricoche2026 §4 [P; read] | 1.18 |
| −20 dB width | Full width 20 dB below the peak | — | 3.03 |
| rms delay spread; delay window W_q; delay interval I_th | Power-weighted standard deviation of excess delay; middle portion holding q % (typically 90 %) of the power; span between first and last crossing of a threshold (9, 12 or 15 dB below peak) | ITU-R P.1407-8 §2.2.2–2.2.5 [S; read] | — |
| Correlation bandwidth, coherence time | Lag at which the correlation falls to a stated fraction, typically 0.5 or 0.9 | P.1407-8 [S; read] | — |
| Correlation ("fading") time | Lag at which the amplitude autocorrelation is 0.5 | Balser & Smith 1962 §4 [P; read] | 2σ ≈ 0.265/T for Rayleigh fading with a Gaussian spectrum [E] |

Conversion factors in the last column are for a single Gaussian spectrum [E: 2 × 1.2816σ; 2.355σ; 2σ√(2 ln 100)]. Three cautions:

- For two equal paths separated by τ, the rms delay spread is τ/2, while the 80 % window and the delay interval are both τ [E].
- NRL and SRI wideband data are better fitted by an exponential autocorrelation, that is a Lorentzian Doppler spectrum [NTIA92 abstract; NTIA90 §2; T; read]. A Lorentzian has no finite variance, so "2σ" is undefined and width figures depend on the estimator and its truncation.
- "Composite" widths include the differences between the Doppler shifts and delays of the modes. They are not per-mode spreads.

### B. Mode structure and differential delay

**B1. One-hop oblique paths at mid-latitude.**

Watterson, Juroshek & Bensema: Long Branch, Illinois to Boulder, Colorado, 1,294 km, east–west, November 1967, 12 kHz probing, three analysed samples of 10–13 minutes. Values from the primary report [Watterson1969 Tables 4, 6 and 8, pp. 62, 70, 78; T; read], which NTIA87 reproduces (§5.2, Tables 5–8, pp. 40–46) with the two transcription differences noted below the table. "Spread" is the tap-gain frequency spread 2σ, twice the square root of the second central moment of the tap-gain spectrum [Watterson1969 §2.2.3, pp. 21–22]. The primary report labels the two magneto-ionic components of a mode "a" and "b" without saying which is ordinary; the O/X labels below follow NTIA87.

| Sample | Mode | Power (dB) | Delay (µs) | Doppler shift (Hz) | Spread (Hz) |
|---|---|---|---|---|---|
| I1, 9.259 MHz, day | 1E (O / X: −4.1 / −4.3 dB) | −1.2 | 40 | 0.0094 (O 0.0022, X 0.0170) | 0.027 (O 0.0073, X 0.032) |
| | 1F | −7.2 | 290 | 0.0089 | 0.144 |
| | 2F | −13.5 | 1,139 | −0.167 | 0.340 |
| I2, 9.259 MHz, day | 1E (O / X: −4.1 / −5.5 dB) | −1.7 | 40 | 0.0071 (O −0.0008, X 0.0127) | 0.015 (O 0.0064, X 0.0084) |
| | 1F | −5.9 | 290 | 0.0159 | 0.180 |
| | mixed | −17.6 | 590 | 0.108 | 0.334 |
| | 2F | −12.6 | 1,126 | 0.118 | 0.336 |
| I3, 5.864 MHz, night | unidentified 1 | −1.6 | 445 | 0.099 (0.076, 0.134) | 0.066 (0.036, 0.032) |
| | unidentified 2 | −7.7 | 750 | 0.131 (0.121, 0.141) | 0.023 (0.010, 0.013) |
| | unidentified 3 | −8.5 | 1,088 | 0.140 (0.121, 0.151) | 0.034 (0.015, 0.021) |

Transcription differences: NTIA87 prints the I2 O-component shift without its minus sign (0.0008) and the second I3 mode-3 component shift as 0.140; the primary tables give −0.0008 and 0.151 Hz, and the tabulated I3 mode-3 mean of 0.140 Hz follows from the primary component values [E]. In the primary I2 table the 1E mean shift (0.0071 Hz) matches its two components only if their power ratios are interchanged; the channel shift (0.0171 Hz) agrees with 0.0071 Hz [E]. The I3 component powers are −3.8 / −5.7, −10.8 / −10.6 and −12.9 / −10.4 dB for the three modes [Watterson1969 Table 8].

Power-weighted channel values (I1 / I2 / I3): time spread 478 / 520 / 464 µs; Doppler shift 0.0013 / 0.017 / 0.110 Hz; Doppler spread 0.123 / 0.140 / 0.067 Hz; within-mode "path time spread" 20 / 30 / 100 µs; model validity bandwidth 12.5 / 8.3 / 2.5 kHz [Watterson1969 Tables 4, 6, 8; NTIA87 Table 5, p. 42]. Watterson's channel time spread is twice the power-weighted rms delay spread [Watterson1969 eq. 58], so the rms values are 239 / 260 / 232 µs [E]. The validity bandwidths equal 1/(4 × path time spread), the report's eq. 110, and the report's abstract states the same rule of one quarter of the reciprocal of the time spread on the modes [Watterson1969 abstract and eq. 110; Watterson1970; P; abstract].

Balser & Smith: Atlanta to Ipswich, Massachusetts, 1,566 km, 35 µs pulses, 1959–1960. An example at 17 MHz shows two pulses about 200 µs apart (1E, 1F) and a third about 1 ms later (2F) [BS62 §3; P; read].

WWV to San Antonio, 1,350 km, 5 MHz, dawn of 29 January 2020: measured times of flight of 4.93 ms (one hop), 5.67 ms (two hops) and 6.38 ms (three hops), so 0.74 and 1.45 ms differentials, with a possible fourth cluster ("4 hop or Pedersen?") [Cerwin2020 slides 9–13; G; read]. The one- and two-hop values were measured at 1201 and 1329 UTC, so these differences are indicative rather than simultaneous delays [E]. The author's geometric expectation for this path is 0.3–2.6 ms between the one-hop and the two- and three-hop modes for heights of 150–300 km (a second text box on the same slide says 0.3–4 ms) [Cerwin2020 slide 6; G; read]. The same talk notes that the carrier often "disappears due to selective fading" [slide 15].

Texas regional paths, 5.3 and 7.2 MHz, 2023–2024: 1.7 ms between 1F2 and 2F2 at 317 km (26 February 2023, 2104 UTC, 5.3 MHz). The 1F2/2F2 pair was the multipath "encountered the majority of the time", and simultaneous one- and two-hop modes were "almost always possible" on the regional and NVIS paths. Lower values (1F2−1E) and higher values (2F2−1E, with 1F2 absent) were also common on a 407 km path [Cerwin2026 Figs 5, 14, §§2–5; P; read].

General statements: inter-modal delays are commonly 1–3 ms and up to 7–8 ms on long paths (citing Hoff & King 1981); intra-modal delays are tens of microseconds; with two or more modes the typical multipath spread is 1–10 ms [NTIA87 §2.3, pp. 11–12, and §5.1, p. 32; T; read].

Percentile statistics on UK paths along the mid-latitude trough (Uppsala–Leicester, 1,400–1,440 km, and Nurmijärvi–Leicester, 1,800 km; six frequencies between 4.64 and 18.38 MHz, each sounded every 2–3 minutes; observations with SNR of at least 4–6 dB): the composite multipath spread runs from the rising edge of the first detected mode to the falling edge of the last, corrected for the pulse width, and from the 2011 reprocessing excludes modes more than 12 dB below the strongest [WS2003 §3; SW2011 §2; P; read]. At sunspot maximum (2001) its 95th percentile was 2.2–2.8 ms by day in winter and 2.2–3.6 ms on winter nights at 4.6–11.1 MHz, falling to 0.2–1.5 ms at 14.4 MHz; at sunspot minimum (2006–2010) it did not exceed 2.5 ms and was 0.1–0.5 ms above 10 MHz; medians were 0.1–0.2 ms in almost all cases [SW2011 Tables 3–8, §§3–3.1; P; read]. The original 2003 processing, which kept the weak modes, gave larger values, for example 95th percentiles of 3.1–3.7 ms by day in winter and 5.8 ms on equinox nights at 6.95 MHz [WS2003 Tables 1–6; P; read]. The authors place the measured 95th percentiles in general between the F.1487 mid-latitude moderate (1 ms) and high-latitude moderate (3 ms) values [SW2011 §3.1].

**B2. NVIS (0 to about 250 km).**

| Source | Path, frequency | Result | Grade, access |
|---|---|---|---|
| Cerwin2026 Fig. 15, §5 | 8.77 km, Texas, 5.3 and 7.2 MHz, 2023–2024 campaigns | Two delay clusters averaging 1.9 ms and 3.8 ms (one and two extra vertical round trips to about 290 km) | P; read |
| Male et al. 2021 §4, Figs 9, 11–12, Table 3 | Barcelona–Cambrils NVIS, 5.4 MHz, 12 observation days in November–December; availability summarized over 07–16 UTC | Detected-frame availability: O 57.6% (hourly range 30–74%), X 72.2% (56.67–94%); combined-channel detection 86% over 07–16 UTC. Conditional delay-spread means 0.33 ms (O), 0.31 ms (X); maxima 2.71 and 2.89 ms. Delay statistics include only successfully demodulated frames and are conditioned on both channels active (07–16 UTC); Doppler could not be separated from SDR clock offset. | P; full text read; one path/frequency/season and apparatus |
| Gonzalez2021, frame-design section | 100 km, same link | Cyclic prefix set to 3 ms; ordinary-wave delay spread reached about 2.7 ms | P; read |
| Maso2020 §3, Figs 5, 9, 10, summary table | Short links on Livingston Island, Antarctica, 4.3 MHz | E echo 0.80 ms and F2 echo 2.07 ms after the ground wave; skywave-only total delay spread 0.05–2.75 ms; with ground wave up to 5.5 ms | P; read |
| Ivanov2020 §5, Fig. 6 | Vertical sounding, Yoshkar-Ola (56.6°N), 3.6–5.8 MHz, daytime, 24 kHz channels, 100 sessions | 70 % of delay/Doppler pairs in the authors' "moderately disturbed" F.1487 class, 30 % "moderate"; spreading increased with frequency | P (conference); read |
| Hervás2020 §3.4, reporting Davies et al. 2004 (WHISPER) | 170 km east–west, Malvern to Cobbett Hill, UK, spring 2001, 61.4 kchip/s PN sounding, 140 s records every 5 minutes | Three case studies: 3.9 MHz, 07:38 UTC, four modes (1E, 1F2, 2F2, 3F2), delay spread about 4 ms, Doppler spread ±0.5 Hz, nearly static for 140 s; 5.7 MHz, 20:31 UTC, five F-layer modes, about 10 ms and ±5 Hz, changing within the record; 6.7 MHz, 08:10 UTC, 6 ms and ±2 Hz | P (review); read; secondary for the WHISPER report, estimator not stated |
| F1487 Annex 3 §3.4 | "Disturbed near vertical incidence" | 7 ms, 1 Hz; ground wave "should be added if required" | S; read |
| BS2144 §4.9 | DRM in the tropical bands | NVIS delay and Doppler spread are "more severe especially at certain times of day, such as dawn and dusk" | S (report); read |

Geometry explains the 7 ms figure: at vertical incidence with h = 300 km successive F echoes are 2.0 ms apart, so 7 ms spans from the ground wave or first echo to the third or fourth F echo [E, table above]. At 170 km, 1E against a fifth F2 hop at 300 km is about 9 ms, so the WHISPER night case of about 10 ms is geometrically possible [E, flat-Earth mirror]. Apart from that single case study, known only through a review, no measured mid-latitude NVIS delay beyond 5.5 ms was found.

**B3. Very long multi-hop path (12,700 km, Livingston Island 62.6°S to the Ebro Observatory 40.8°N, or to Cambrils 41.0°N in 2014; at least four hops; crosses the equatorial zone).** Estimator: 80 % power width inside analysis windows of ±3.5 ms and ±2.5 Hz (2006/07 and 2014 analyses) or ±3 ms and ±4 Hz (2009/10).

| Survey | Composite multipath spread | Composite Doppler spread | Source |
|---|---|---|---|
| 2006/07 | 0.6–2.25 ms, falling with frequency; maxima 2–2.5 ms at 7–11 MHz around 22–04 UTC; minimum about 0.5 ms at 16.7 MHz | 1–1.5 Hz from sunset to 04–05 UTC; maximum about 1.5 Hz; minimum about 0.5 Hz at high frequencies at sunrise; "typical" night values 1.5 ms and 1 Hz | Vilella2008 paras 44–49, 61, Figs 10–13, Table 3; P; read |
| 2009/10 | 0.2–3.1 ms; maxima 2.7–3.2 ms at night and after sunrise below 14 MHz; minimum 0.25 ms by day at 12.5–23 MHz | Maxima 2–2.4 Hz at 3.5–14 MHz, 00–04 UTC; minima 0.1–0.24 Hz by day (10–17 UTC) at 15–22.5 MHz | Ads2013 paras 54–58, Tables 3–4, Figs 15–16; P; read |
| 2014 | Night-time means around 2–3 ms near 10 MHz | Maximum about 0.7 Hz | Hervás2015 §5.2, Fig. 17; P; read |
| All campaigns | Maximum 3.5 ms | Maximum about 2.5 Hz | Alsina2016 §3, p. 11; P; read |

The all-campaign maxima coincide with the analysis window limits, so they are probably truncated [E]. Absolute propagation time was 45–50 ms on average, against 42.3 ms for the great circle [Ads2013 paras 59–61, Fig. 18; P; read].

**B4. Night-time MF.** ITU-R P.1321-5 gives a simple mode model for 700 kHz and 1 MHz at 100, 200 and 500 km, four hours after sunset at sunspot minimum: ground wave, 1E, 2E, 3E, and above the E-layer critical frequency 1F, 2F, 3F. Read by eye from the rendered figures, the one-hop E mode lags the ground wave by about 0.4 ms at 100 km, 0.16 ms at 500 km and 0.12 ms at 1,000 km, and the one-hop F mode by more than 1.2 ms at 100 km, 0.8 ms at 200 km and about 0.45 ms at 500 km (Fig. 3); the mutual delays are about 1.5 ms (F, one to two hops) and 3 ms (F, one to three hops) at 100 km, falling to about 0.8 and 2.3 ms near 450 km, and 0.8–1.3 ms (E) at 100 km, falling to 0.2–0.3 ms at 1,000 km (Fig. 4). The delay window holding more than 98 % of the energy is "less than 3 ms" [P1321 Annex 1 §3, Figs 2–4, §5; S; read]. These are model values built on reflection coefficients, not sounding measurements. A flat-Earth mirror estimate gives the first skywave hop 0.4 ms (E, 100 km height) or 1.4 ms (F, 250 km) behind the ground wave at 100 km range, falling to 0.13 ms and 0.7 ms at 500 km, and 2E−1E of 0.73 ms at zero range and 0.23 ms at 1,000 km (h = 110 km) [E: delay = (2√((D/2)² + h²) − D)/c]. The P1321 F curve falls faster with range than this constant-height estimate (about 0.45 against 0.7 ms at 500 km). DRM channel 2 ("MF bands at night-time including a delayed skywave in addition to the ground wave") is a fixed path plus a path at 1 ms, −6 dB, with 0.1 Hz spread [DRM Table B.1; BS2144 annex text; S; read].

### C. Intra-mode dispersion and coherence bandwidth

| Quantity | Value | Conditions | Source |
|---|---|---|---|
| Dispersion, measured | 8–40 µs/MHz | 1F2, 126 km, southern California, autumn morning; X mode less dispersive than O | NTIA87 Table 1, p. 22, citing Wagner & Goldstein 1982; T; secondary |
| Dispersion, measured from an ionogram | 16, 32, 80, 96, 240, 416, 656 µs/MHz in successive 0.5 MHz steps from 4.88 MHz to the MUF | 1F2, 244 km, spring | NTIA87 Table 1, citing Salous 1985; T; secondary |
| Dispersion, predicted | 18–50 µs/MHz (2,799 km, summer) to 530–1,000 µs/MHz (926 km, winter, sunspot number 100), at 70–85 % of the junction frequency | 1F2 | NTIA87 Table 1, citing Milsom & Slator 1982; T; secondary |
| Dispersion, typical and disturbed | "a few tens" of µs/MHz undisturbed; 240 µs/MHz in mid-latitude spread-F | NRL wideband probe | NTIA90 §1, citing Wagner et al. 1989; T; secondary |
| Dispersion, ray-traced | 15–50 µs per 100 kHz | 2.9 and 3.4 MHz, sub-auroral Alaska | Aricoche2026 §2; P; read |
| Within-mode delay spread | O ≈ 50 µs, X ≈ 24 µs | NRL pulse data; the OCR text places this with the 126 km path but is garbled | NTIA88 Figs 5–6 text; T; read, low confidence in the path attribution |
| Within-mode "path time spread" | 20, 30, 100 µs | Watterson samples | Watterson1969 Tables 4, 6, 8; NTIA87 Table 5; T; read |
| Narrowest pulse passed without significant broadening | 3–5 µs for 50 % of daylight hours, at the minimum-dispersion frequency | 1,880 km east–west (Lubbock–Stanford) and 1,300 km north–south (Bozeman–Stanford) | Lynch, Fenwick & Villard 1972; P; abstract |
| Wideband coherent bandwidth | 100–200 kHz typical; 80 kHz; about 100 kHz at night | 126–244 km paths and others | NTIA87 Table 2, p. 23; T; secondary |
| Coherence bandwidth, vertical sounding | About 30 kHz | Yoshkar-Ola | Ivanov2020 §3, citing Ivanov 2006; P; secondary |
| Occurrence of mid-latitude spread-F (diffuse multipath) | About 35 % of the time | Over England, winter of a sunspot-minimum year | NTIA90 §1, citing Milsom & Slator 1982; T; secondary |

In the brief's units these are 0.008–1 µs/kHz. Across 225 Hz that is at most 0.2 µs, and across 3 kHz at most 3 µs [E: 1,000 µs/MHz × 0.225 kHz and × 3 kHz]. Intra-mode dispersion is irrelevant to this modem; multi-mode delay is what matters.

### D. Doppler shift

| Condition | Value | Path and frequency | Source |
|---|---|---|---|
| Quiet day | "usually do not exceed ±(0.1–0.2) Hz" | 11 paths of 910–1,875 km into Harbin, 5–9.8 MHz | Luo2021 conclusion 8, Table 2; P; read |
| Quiet day | "of the order of ±0.2 c/s", periods of the order of 10 minutes; E echoes much more stable than F2; oblique paths more stable in frequency than vertical but with a broader spectrum | Middle latitudes, vertical and oblique (WWV) | DaviesBaker1966; P; abstract |
| Disturbed periods | "as much as 2 to 3 c/s on 5 Mc/s"; at night and in some storms the line spectra "give way to diffuse spectra" | Vertical incidence, 5 MHz | DaviesBaker1966; P; abstract |
| Quiet, per mode | 0.001–0.17 Hz in magnitude | 1,294 km, 5.9 and 9.3 MHz | Watterson1969 Tables 4, 6, 8; T; read |
| TIDs, one year | Peak-to-peak range: median 0.31 Hz (winter, summer), 0.28 Hz (spring), 0.24 Hz (autumn); half-maximum span of the distribution 0.07–0.5 Hz. Period: median 24 min (winter), 28–29 min (equinoxes), 40 min (summer). Present 81–91 % of the time in winter and spring months, 52–80 % in summer and autumn (E and Es screening); strongest morning and evening | Moscow (RWM) to Kharkiv region, about 650 km, 9.996 MHz, 60 s spectra with 0.017 Hz resolution, detection threshold 0.05 Hz | Reznychenko2020 abstract, §3.2, Fig. 3; P; read |
| TIDs at MF | About 0.1 Hz; "up to a few tenths of a hertz" with periods near 40 minutes | AM broadcast band, several hundred km, north-eastern United States, 11 nights in March–April 2012 | Trop2025 §1 (read); Chilcote2015 (abstract); P |
| Before sunrise | Largest shifts, ±1 Hz, in a damped oscillation; one-hop mode dominant | WWV to Newark, 10 MHz, about 2,590 km [E from coordinates], data from 2021 onward | Fernandes2026 §3 item a, conclusions; P; read |
| Sunrise | Spike of about +0.5 Hz | Same | Fernandes2026 §3 item b; P; read |
| Daytime | 0 to +0.25 Hz (±0.25 Hz), Cauchy-distributed residuals; 2- and 3-hop modes | Same | Fernandes2026 §3.2, conclusions; P; read |
| "General" | "on the order of ±1 Hz" | Grape network, 2.5–10 MHz | Collins2023 Fig. 1 caption; P; read |
| Dawn | One to three extra higher-order tracks with larger positive shift; modes "manifest and extinguish abruptly" | WWV to Texas, 1,350 km, 5 MHz (seen at 2.5 and 5 MHz, not 10 MHz) | Collins2022p §4.2, Fig. 15; Cerwin2020 slide 2; G; read |
| Sunrise, four hops | Mean shift goes from −0.5 Hz at 06 UTC to −4 Hz at 07 UTC, then rises to +1.2 Hz by 20 UTC; night −0.5 to +1.2 Hz | 12,700 km, 2009/10 | Ads2013 paras 62–64, Fig. 19; P; read |
| Terminator at MF | "nearly 1 Hz" upward at dawn, downward at dusk | AM broadcast band | Chilcote2015; P; abstract |
| Eclipses | 0.25 Hz amplitude, about 2 h (annular, October 2023); 1 Hz, about 4 h (total, April 2024); about 1.3 Hz late-morning level and ±0.2–0.4 Hz envelope (August 2017) | 10 MHz WWV paths | Fernandes2026 §3.1; Collins2022p §3.2; P and G; read |
| Moderate storm (Kp to 5.7, Dst −55 nT) | Excursions to −1.5 and +1.5 Hz (−1.8 to +1.4 Hz at one sunset); quasi-periodic amplitudes 0.1–0.7 Hz with 20–110 minute periods; reflection level moving at 10–60 m/s | Harbin paths, 30 August–2 September 2019 | Luo2021 §6, conclusions 8–9, Table 2; P; read |
| Higher geomagnetic activity (K ≥ 2) | Larger amplitudes and longer periods; up to 0.5 Hz with periods over 60 minutes | 650 km, 9.996 MHz | Reznychenko2020 abstract, §3; P; read |
| Solar flare (sudden frequency deviation) | "a few cycles per second". In the example of Fig. 6.4 (WWV 10 MHz, 4 September 1961) the frequency rises over about 2 minutes, mostly in the last half-minute, to about +6 Hz, falls within about half a minute to about −2 Hz and recovers over about 1.5 minutes (read by eye; the trace is faint near the peak) | Classical | Mono80 §6.2.3, pp. 262–263, Fig. 6.4; B; read |
| Solar flare | X1.6 at 15° solar elevation (22 October 2014): peaks of about 3.4 Hz at 3.59 and 4.65 MHz and 3.9 Hz at 7.04 MHz; each rose by about 3 Hz within roughly 15–40 s, fell to about −1 Hz within about 20 s and recovered within about another minute (Fig. 4c, read by eye; 6 s resolution). Table 2's maximum virtual plasma velocities give the same peaks through fD = 2f·u/c (83–142 m/s → 3.4–3.9 Hz), about 1.5 Hz for X2.7 at 10° ("relatively small") and about 7.8 Hz for M7.9 at 32° (179 m/s at 6.57 MHz) [E]. The Doppler peak follows the maximum rate of rise of EUV flux within about 20 s; the 3.59 MHz signal was lost for about 20 minutes afterwards | Vertical sounding, 3.59–7.04 MHz, Czech Republic (50°N; X1.6) and Taiwan (24°N; X2.7, M7.9) | Chum2018 flare sections, Fig. 4c, Table 2, discussion; P; read |
| Solar flare | +0.25 Hz with about 10 dB attenuation, lasting through the blackout of about 34 minutes (M9.8 flare) | 10 MHz, 2,590 km, 28 November 2023 | Fernandes2026 §3.1, Fig. 6; P; read |
| Solar flare, statistics | "the majority of them have magnitudes less than 1 c/s"; most probable duration "about 4 min"; deviations "of up to 50 c/s" observed | WWV 10 MHz received at Boulder | DaviesBaker1966; P; abstract (re-checked in the Crossref record; full text not obtained). The 50 Hz figure is a stated extreme; no distribution was seen |
| Solar flare, atlas of 1973–74 events | Peak deviations of at least 4.5 Hz are classed as "large"; several occurred in April–May 1973 and none during the ATM-Skylab missions, a period of low solar activity; for solar zenith angle χ ≤ 85° the deviation scales as cos χ; detection threshold about 0.1 Hz; the end of an SFD is "usually very uncertain" | WWV to Sacramento Peak (881 km), near-vertical 2.4–6 MHz paths near Boulder, and paths in England, Alabama and Hawaii | Donnelly1974 §§2–4; T; read |
| Solar flare, SuperDARN ground scatter | A sudden phase change ("Doppler flash") precedes the echo dropout; echoes drop within about 100 s, reach maximum suppression within a few tens of minutes and recover over about half an hour | Mid-latitude radars, X-class flares | Chakraborty2018; P; abstract |
| Solar flare, SuperDARN Doppler flash | The flash "typically lasts for only a few minutes", whereas flare blackouts last 30–60 min; in one event model and observations both show an apparent Doppler velocity of up to about 100 m/s, about 7 Hz at 10 MHz on the radar's two-way path [E, f = 2vf₀/c] | SuperDARN radars, 8–18 MHz, six flares | Chakraborty2022 §§1, 4; P; read |
| Storm sudden commencement (theory) | About 1 Hz at 4 MHz for a 4-minute commencement of several tens of nanotesla | Middle latitudes, vertical incidence | Jacobs & Watanabe 1966; P; abstract; modelled |
| MF generally | Doppler shift "will be small" | LF/MF broadcasting | P1321 §4.3; S; read |

Rates of change of the shift [E, from 2πA/T for a sinusoid of amplitude A and period T, or excursion divided by duration]:

- TIDs: 0.2–5 mHz/s (0.1 Hz at 40 minutes gives 0.26 mHz/s; 1 Hz at 20 minutes gives 5.2 mHz/s).
- Sunrise on the four-hop path: 3.5 Hz in one hour, about 1 mHz/s on average; Ads2013 note that sunrise takes only 24 minutes along the four hops, so the rate within that time is probably at least 2.4 mHz/s.
- Storm excursions: 1.5 Hz in 15–35 minutes, 0.7–1.7 mHz/s.
- Flares: about 1 Hz reached in half of a 4-minute event gives roughly 8 mHz/s, with an assumed profile. Measured profiles are much steeper: in the 2014 X1.6 flare about 3 Hz was gained within roughly 15–40 s and 4–5 Hz lost within about 20 s, that is about 0.1–0.25 Hz/s for tens of seconds [Chum2018 Fig. 4c, read by eye]; the 1961 example of Mono80 Fig. 6.4 gives about 0.2 Hz/s over half a minute. The extreme 50 Hz event would imply about 0.4 Hz/s if it rose in two minutes, an assumed rise time.

### E. Doppler spread

| Condition | Value and estimator | Source |
|---|---|---|
| Quiet one-hop modes | 0.006–0.18 Hz per mode or magneto-ionic component (2σ); 2F and mixed modes 0.33–0.34 Hz; composite 0.07–0.14 Hz | Watterson1969 Tables 4, 6, 8; NTIA87 Tables 5–8; T; read |
| One-hop E and F, from fading | Correlation time (0.5 point) most often 10–20 s, tail to about 120 s; equivalent 2σ about 0.013–0.027 Hz | BS62 §4, Fig. 4; P; read; conversion [E] |
| Multi-hop F, from fading | "a very few seconds"; equivalent 2σ about 0.1 Hz for 3 s | BS62 §4; conversion [E] |
| NVIS, skywave only | 0.05–0.1 Hz (coherence time 10–20 s); ground wave 0.06–0.09 Hz | Maso2020 Figs 11–12; P; read (estimator not stated) |
| NVIS | Coherence time "roughly 10 s"; an earlier La Salle figure of 1.46 s is quoted as "most restrictive" | Ivanov2020 §3; Male2021 §2; P; read / secondary |
| Mid-latitude standard classes | 0.1 / 0.5 / 1 Hz (quiet / moderate / disturbed), 2σ; disturbed NVIS 1 Hz | F1487 Annex 3 §3; S; read |
| UK paths along the mid-latitude trough: 1,400–1,440 km and 1,800 km, 4.6–14.4 MHz, 2001 (sunspot maximum) and 2006–2010 (minimum) | Composite width (narrowest band holding 80 % of the power, all modes) at 0.5–0.625 Hz resolution. Medians at the resolution limit (0.5 Hz, sometimes 1.0 Hz) after 2006. 95th percentiles: 1.5 Hz in every season at sunspot minimum; 1.3–2.5 Hz by day and on summer nights at sunspot maximum; but 4.4–13.3 Hz on winter nights and 2.5–6.3 Hz on equinox nights at 6.95–14.4 MHz at sunspot maximum. In the original 2001 processing the winter-night medians at 10.39 and 11.12 MHz were 3.3 and 3.8 Hz, the 90th percentiles 8.7 and 11.2 Hz and the 95th percentiles 12.8 and 15.7 Hz. The large values come from signals arriving well north of the great circle, from the trough's poleward wall or the auroral zone | WS2003 §§3–5, Tables 1–6; SW2011 Tables 3–8, §3.1; P; read |
| 12,700 km path | 0.1–0.24 Hz (day, 15–22.5 MHz) to 2–2.4 Hz (night, 3.5–14 MHz); 80 % power width, composite | Ads2013; Vilella2008; P; read |
| Moderate storm, paths of 1,610–1,875 km | Spectra occupying about ±1.5 Hz for hours on a storm day (1,845 km, 31 August 2019). On the 1,875 km path the night-time spectrum broadened to −2.5 to +2 Hz on 29 August, a reference day before the storm commenced, and on the 1,610 km path the spectra filled ±1.5 Hz on 29–31 August alike | Luo2021 §§3, 6.1, 6.3, 6.7; P; read (occupied width read from spectrograms, not a statistical estimator) |
| Sub-auroral trough (boundary of this scope) | About 1 Hz rms predicted and "consistent with observations"; range spread one to a few km rms (about 7–20 µs) | 2.9 and 3.4 MHz, Alaska, 8 November 2023; Aricoche2026 §4; P; read |
| Mid-latitude and transauroral, one year near solar minimum | rms spread distributions published, 0.025 Hz resolution | Shepherd & Lomax 1967; P; abstract only, no values seen |
| Whole range seen by wideband probes | 0.1 Hz to over 40 Hz across equatorial, mid-latitude and polar paths | NTIA90 §3; T; read |
| Rule of thumb | "of the order of 1 Hz" for "typical moderate HF paths" (ten times Watterson's own composite values) | NTIA87 §5.1, p. 32; T; read |

Spread grows with hop number and toward night and low frequency on the long path, and with geomagnetic activity. Yeh & Villard (1960) found fading speed rising with magnetic activity and attributed fast fading on a temperate path to the aurora moving onto the path [TN133 p. 12; T; secondary].

### F. Fading statistics

**Amplitude distribution.** Of 146 records (each a little over 10 minutes, pulses resolving single modes, 1,566 km): 50 Rayleigh; 20 with a clear specular component (ratio of specular amplitude to rms scatter a ≥ 2, so K = a²/2 ≥ 2, or 3 dB); 9 "two sine waves"; 52 "probably nonstationary"; 15 unidentifiable. The strongest specular cases (a ≥ 3, K ≥ 6.5 dB) occurred only very close to the MUF, more on F than E and more in winter. The extreme was a = 7 (K ≈ 14 dB) on an F high ray with resolved magneto-ionic components, with "practically no fading at all" for 10 minutes. All multi-hop records were Rayleigh [BS62 §4 and summary table; P; read; K conversion E].

Davies: a continuous wave involving several paths is usually close to Rayleigh, while single modes resolved by short pulses "often have shallower (and slower) fading" with a specular-to-random ratio of 2 or more [Mono80 §5.4.2, p. 243; B; read]. The NRL wideband prober led to the same picture: "The mid-latitude channel is multimodal but is usually characterized by a single specular return per mode", whereas the transauroral channel at night is diffuse with Doppler spread larger "by an order of magnitude" [Wagner1988; P; abstract]. The historical record is mixed: Rayleigh (Grisdale et al. 1957; Yeh & Villard 1960, long paths), Rice (McNicol 1949), log-normal (Allcock 1950), and "Rayleigh, Gaussian, or log normal" on 640–1,140 km paths (Aggarwal 1959) [TN133 pp. 5–12; T; secondary].

Two qualifications apply to the "specular" component. First, measured across an antenna array it was itself found to vary in a way "suggesting that it is Rayleigh distributed", so that "averages over different periods may give quite dissimilar results"; the author inferred that there is strictly no specular component, only a group of rays from very similar directions [Boys1968; P; abstract]. A Rician K-factor is therefore a short-interval description, not a constant. Second, mid-latitude sporadic-E reflections are much cleaner: scatter-type signals were of the order of 30 dB below the reflected signal [Wagner1987; P; abstract].

**Slow fluctuation of mode power.** MITRE's 1 MHz-bandwidth measurements on near-vertical, 2,000 km and 3,000 km mid-latitude paths (mainly one-hop F) were summarised by modelling the path loss of a single mode over 15-minute intervals, in decibels, as a normally distributed first-order autoregressive process [RifkinBello1994; P; abstract]. The parameter values are in the paper and were not seen.

**Depth.** Within an hour the signal's upper decile is 5 dB above and its lower decile 8 dB below the median [P842 reliability-procedure tables, steps 4 and 7; S; read]; these are the Rayleigh deciles [Mono80 p. 243: lower decile 0.39 of the median]. F.339 instead uses a log-normal with 7 dB between the median and the 10 % or 90 % levels, except Rayleigh for fast telegraphy [F339 Annex 1 notes; S; read]. NVIS records show fades "down to −30 dB" within seconds, on top of slow fading over tens of minutes [Wit17 §4.1.1, Fig. 6; P; read]; NVIS conditions with almost no fading also occur (Burgess & Evans 1999, via Wit17). Amplitude swings were 5–20 dB on a quiet day and 30–60 dB during the 2019 storm [Luo2021 conclusion 10, Table 2; P; read]. In a 1 MHz bandwidth the received power had a median standard deviation of only 2–3 dB and median decorrelation times of 37–57 s on 2,000 and 3,000 km mid-latitude paths, and median fade depth always decreased with increasing bandwidth [RifkinPerry1994; P; abstract].

**Rate and duration.** No measured fade-duration distribution for HF mid-latitude skywave paths was found; the nearest is for night-time MF at 539 kHz, where lower-decile fades have gamma-distributed durations averaging about 3 minutes (section I). The following are derived for Rayleigh fading with a Gaussian Doppler spectrum: crossing rate N = 2√π·σ·ρ·e^(−ρ²), mean fade duration (e^(ρ²) − 1)/(2√π·σ·ρ), where σ is half the 2σ spread and ρ the threshold relative to rms [E].

| 2σ spread | Coherence time (complex correlation 0.5) | Fades below −10 dB: spacing, mean duration | Fades below −20 dB: spacing, mean duration |
|---|---|---|---|
| 0.01 Hz | 37 s | 197 s, 19 s | 570 s, 5.7 s |
| 0.03 Hz | 12.5 s | 66 s, 6.3 s | 190 s, 1.9 s |
| 0.1 Hz | 3.7 s | 20 s, 1.9 s | 57 s, 0.57 s |
| 0.5 Hz | 0.75 s | 3.9 s, 0.38 s | 11 s, 0.11 s |
| 1 Hz | 0.37 s | 2.0 s, 0.19 s | 5.7 s, 0.057 s |
| 30 Hz | 12 ms | 0.066 s, 6 ms | 0.19 s, 1.9 ms |

Time below −10 dB is 9.5 % and below −20 dB is 1.0 % for Rayleigh; with K = 6 dB these fall to about 1.7 % and 0.1 % [E, Monte Carlo]. At the slow end (0.01–0.03 Hz, which Watterson's E mode and Balser & Smith's one-hop records reach) a 10 dB fade lasts 6–19 s on average, comparable with the 11.6 s bit of rows 4, 5 and 7 of the manual test list.

**Interference and polarisation fading.** From Watterson's per-component shifts, the O–X beat periods are 68 s and 74 s (daytime E mode) and 17 s, 33 s and 50 s (night modes); beats between modes are 5.7 s and 9 s (E against 2F), 114 s and about 2,000 s (E against 1F), and 24–31 s at night [E from Watterson1969 Tables 4, 6, 8]. With the measured power ratios, the E–1F beat gives about 9.6 dB of ripple (6 dB ratio) and the E–2F beat about 4.3 dB (12.3 dB ratio) [E]. Davies instead gives interference and polarisation fading periods of "a fraction of a second to a few seconds", focusing fades of 15–30 minutes and absorption fading of an hour or more [Mono80 §5.4.1, p. 242; B; read]. Davies may be describing busier multi-mode channels; on Watterson's quiet path the beats are tens of seconds to minutes long. Sinusoidal fading in phase quadrature on crossed dipoles confirms the O–X origin (Hedlund & Edwards 1958, via TN133 p. 11).

Direct polarisation measurements on 1,300 km north–south and 1,900 km east–west temperate paths (one-hop F, 24 hours a day for 21½ days) gave a daytime polarisation rotation rate averaging 0.25 turn per minute at a fixed frequency, independent of azimuth and frequency, and near zero for much of the night, with "large fluctuations" [Epstein1969; P; abstract]. A linearly polarised antenna then sees a fade every half turn, that is about every 2 minutes by day [E], the same order as the 68–74 s derived from Watterson's E mode. Rotation with frequency averaged 1 turn/MHz (north–south) and 0.25 turn/MHz (east–west) by day, one to four times more at night, with a minimum near 0.8 of the maximum observed frequency [Epstein1969]. Across 3 kHz that is at most about 0.012 turn (4°), so polarisation fading is flat across the modem's passband [E]. On the 97 km NVIS link at 5.4 MHz, the O and X channel impulse responses had a correlation coefficient below 0.7 with a probability of nearly 40% in the identified "happy hour" intervals. Mean detected-frame availability over 07–16 UTC was 57.6% O and 72.2% X, with combined-channel detection 86%; these figures belong to this path, frequency, season and detection method, not a universal O/X prior. Delay spreads were calculated only for successfully demodulated frames, so the reported means/maxima exclude channel absences. The study's frequency-offset measurements were dominated by Red Pitaya clock instability and do not measure ionospheric Doppler [Male et al. 2021 abstract, §4, Figs 9–10, Table 3, §5; P; read]. At 7 MHz over 105 km in the Netherlands the isolation between the two circularly polarised characteristic waves exceeded 25 dB during the "Happy Hour", the interval when the path is just opening or closing and only the extraordinary wave propagates [Witvliet2015; P; abstract]. By inference, a receiver matched to one characteristic wave avoids polarisation fading, while a linearly polarised antenna receives both waves and does not [E].

**Amplitude scintillation.** No mid-latitude HF skywave scintillation statistics separate from the fading above were found.

### G. Frequency selectivity over 225 Hz and 3 kHz

For two equal, independently fading paths separated by τ, the complex frequency correlation is |cos(π·Δf·τ)| and spectral nulls are spaced 1/τ [E].

| τ | Null spacing | Correlation across 75 Hz | Correlation across 225 Hz |
|---|---|---|---|
| 0.5 ms | 2,000 Hz | 0.99 | 0.94 |
| 1 ms | 1,000 Hz | 0.97 | 0.76 |
| 2 ms | 500 Hz | 0.89 | 0.16 |
| 4 ms | 250 Hz | 0.59 | (recorrelates: 0.95) |
| 7 ms | 143 Hz | 0.08 | (0.23) |

So the 225 Hz signal is essentially flat-fading for τ up to about 1 ms, partly selective by 1.5 ms (correlation 0.5 at Δf·τ = 1/3), and fully selective at 2 ms and beyond; at 7 ms even adjacent carriers 75 Hz apart fade independently. The recorrelation at larger Δf·τ is an artefact of two discrete equal paths; three or more unequal modes do not recorrelate cleanly [E]. With real power ratios of 6–16 dB between modes the nulls are only 3–10 dB deep [E]. Measured NVIS coherence bandwidths quoted as 1/(delay spread) are 346–369 Hz at the worst (2.7–2.9 ms), about 3 kHz at the mean (0.33 ms), 364 Hz and 182 Hz in Antarctica [Male2021 §4; Maso2020 §3; P; read]. David et al. (1969) measured time–frequency correlation functions on a long-haul link; only the abstract was seen.

### H. Non-stationarity

- Watterson's primary report says only that, after inspection of about 17 hours of recordings (5 h 09 min, 7 h 18 min and 4 h 43 min on 7, 9–10 and 30 November 1967), "twelve intervals were chosen that seemed most nearly stationary in terms of fading rates, modal time delays, and average powers in the modes", and that portions of three of them were analysed [Watterson1969 §3.3, pp. 45–46, Table 1; T; read]. NTIA87 restates this as "about 2 hours or 12 percent were found to be approximately stationary" [NTIA87 §5.2, p. 40; T; read]; the primary report states neither the length of the twelve intervals nor a fraction, so the 12 % is NTIA87's reading of a selection, not the result of a stationarity test. The model was validated on the three samples, totalling 36 minutes [NTIA90 §1; Watterson1969 §5]. The "10 to 15 minute window of apparent stationarity" [NTIA87 §6.1, p. 47] is therefore the length of the selected samples, not a measured typical stationarity time.
- Balser & Smith concluded that the ionosphere can be treated as stationary for perhaps "only a few minutes"; 52 of 146 ten-minute records looked non-stationary; and with correlation times of 15 s a 10-minute record holds only about 40 independent samples, so the statistics themselves are poorly determined [BS62 §4, §6; P; read].
- Watterson-type simulators reproduce fading over a few seconds but not the variation of channel quality over "a few seconds to 10 min" [Wit17 §4; P; read], which prompted an "intermediate duration" variation model (Furman & Koski 2009, via Wit17; not obtained).
- Day structure: the long path shows four regimes (day, night, sunrise, sunset), with sunrise and sunset "unstable" in spread and number of paths [Hervás2015 §5.2; P; read]. On the 2,590 km path, residuals are Cauchy-distributed by day and log-normal or exponential-power before sunrise, so means and variances are not usable descriptors; medians and percentiles are [Fernandes2026 §3.2, Figs 9–10; P; read]. The Grape estimator is the frequency of the strongest spectral peak at 1 Hz cadence, so heavy tails probably include jumps between simultaneous modes [E].
- Mode appearance and loss: modes switch on and off abruptly at dawn as the supported take-off angle changes [Collins2022p §4.2; G; read]; signals fade in and out when the frequency is near a changing MUF, mainly early morning and late afternoon [Mono80 §5.4.1; B; read]; flares remove the lower frequencies for tens of minutes [Chum2018; P; read], with onset in about 100 s and recovery over about half an hour in SuperDARN ground scatter [Chakraborty2018; P; abstract].
- Hour scale: TID analysis needs the delay to be stable "on the order of 1 h" [Trop2025 §3; P; read]. In the Czech data, 1,572 ninety-minute intervals with clear Doppler signals were selected in the solar-maximum year and 1,554 in the solar-minimum year; only 186 and 121 of them passed the reliability criteria for a wave solution. Analysis was impossible in the remaining time for reasons that included spread-F, when the Doppler shift "was a random property" [Chum2021 results, conclusions; P; read].
- Within seconds: in the ELOISE campaign (central Australia, 5–17 September 2015; 69,898 channel-scattering-function dwells of 6.4 s on six of its eleven paths of 900–2,700 km), the strongest one-hop F2 low- and high-ray peaks varied within a dwell with median standard deviations of 0.5–1 dB in power and 0.02–0.03 Hz in Doppler on the Kalkarindji–Laverton path. The variability was larger at night, by 1.5–2 times in Doppler on the two most northerly paths, and largest before dawn (18–22 UT) [Heitmann2020 abstract, §§6.1–6.2; G, PhD thesis; read]. This tests stationarity within one dwell only.
- Neither Aricoche, Hysell & Morton (2026) nor Fernandes et al. (2026) analyses a stationarity interval; the former is a trough tomography paper with spread estimates, the latter a Doppler-shift statistics paper. No 2015–2026 measurement of the HF stationarity interval at the scale of minutes was found.

### I. Night-time MF (1–3 MHz): what was and was not found

- Delays: model values in B4 (window under 3 ms). No sounding measurements between 1 and 3 MHz were found. The nearest are 2.9 and 3.4 MHz in Alaska [Aricoche2026] and 3.6 MHz vertical sounding [Ivanov2020; Chum2018].
- Fading: night-to-night hourly medians log-normal with a semi-interdecile range of 3.5–9 dB; within the hour, individual modes log-normal with a standard deviation of "about 3 dB" ("few measurement data"); fading rate "between 10 and 30 fades/h" [P1321 §4.3; S; read]. On an 860 km link at 539 kHz over two years at sunspot number about 40: 1.8–3.5 excursions above the median per hour, 1.5–2.5 fades below the lower decile per hour, median duration of an excursion above the median about 4 minutes, and decile-threshold fade durations following a gamma distribution [P1321 Appendix 2, Tables 3–5, Fig. 7; S; read]. For lower-decile fades the parameters are α = 3.30 and λ = 1.13 with durations in minutes, a mean of about 2.9 minutes and a mode of about 2 minutes if λ is the rate parameter [E from P1321 Table 5]. This is below the 1–3 MHz range and should scale to somewhat faster fading at higher frequency [E].
- Where ground wave and skywave are comparable (the "night fading zone") fading is "particularly severe" [P1321 §3; Mono80 p. 245; read].
- Doppler: about 0.1 Hz from TIDs and nearly 1 Hz at the terminators (table D). These AM-band measurements use carriers of 0.81–1.56 MHz; skywave tracks are more distinct above 1 MHz, where nearly three times as many events were detected as on the lowest-frequency station [Trop2025 §§2, 6; Chilcote2015 abstract; P; read].
- Medium-wave NVIS channel models from DRM measurements exist (Guerra et al. 2013, a tap-delay-line model from six months of DRM trials at low solar activity, known only from an index-record abstract; Gil et al. 2013; Losada 2009; via Wit17), but their values were not obtained.

### J. Status of the named campaigns

| Campaign or source | What was obtained | Status |
|---|---|---|
| Watterson, Juroshek & Bensema 1970; ESSA ERL 112-ITS 80 (1969) | Primary 1969 report (archive.org copy of DTIC AD0699486): tables, definitions and the selection of samples; the NTIA 87-221 transcription; abstract of the 1970 paper | Read (primary report); the DTIC server refused automated access |
| CCIR Report 549; CCIR Report 266 | Only the F.520-2 values (good 0.5 ms/0.1 Hz; moderate 1 ms/0.5 Hz; poor 2 ms/1 Hz; flutter 0.5 ms/10 Hz) | Reports not obtained; a further search found only bibliographic mentions; F.520-2 read |
| Davies & Baker 1966 | Full publisher abstract (table D) | Abstract |
| Shepherd & Lomax 1967; David et al. 1969 | Abstracts only; no numerical results seen | Abstract |
| Balser & Smith 1962 | Full text | Read |
| MITRE (Perry, Rifkin, Bello) | Two Radio Science abstracts (section F). The conference paper with delay-spread, Doppler-shift and Doppler-spread statistics for three US mid-latitude paths (Clune, Fine, Freedman, Nissen & Perry 1991) is known only from an index record | Abstract; conference paper not obtained |
| UK campaigns (Leicester) | Warrington & Stocker 2003 and Stocker & Warrington 2011, full texts from the University of Leicester repository: percentile tables of Doppler and multipath spread (sections B1 and E) | Read |
| WHISPER (UK, with the Canadian CRC), 2001 | Three case studies on a 170 km path, through the 2020 review by Hervás, Bergadà & Alsina-Pagès (section B2) | Secondary; the 2004 WHISPER report not obtained |
| NRL wideband prober (Wagner, Goldstein) | Abstracts (126 km mid-latitude path; "single specular return per mode"); dispersion values through NTIA reports | Abstract + secondary |
| Antarctica–Spain (Vilella 2008; Ads 2013; Hervás 2015; Alsina-Pagès 2016) | Full texts | Read |
| La Salle NVIS (Male 2021; Maso 2020; Gonzalez 2021) | Full texts | Read |
| Witvliet & Alsina-Pagès 2017; Witvliet et al. 2015 | Full text of the 2017 review; abstract of the 2015 paper | Read; abstract (full text blocked) |
| DRM | ETSI channel set; ITU-R BS.2144 | ETSI annex read by the first pass (it refused automated access on re-check); BS.2144 read; individual trial reports not obtained |
| Chinese sounding | Luo et al. 2021 (Harbin network: 11 broadcast paths of 910–1,875 km at 5–10 MHz; 0.02 Hz and 7.5 s resolution per Guo et al. 2020, abstract). A conference paper on oblique-sounding channel statistics at mid and low latitudes during disturbances (Wang, Liu & Sun 2018) is known only from its bibliographic record | Read; Wang et al. not obtained |
| Indian sounding | Only classical fading studies via Salaman 1962 (for example flutter fading of 20–30 cycles/min before sunset and 120–140 at night on 41 and 60 m, low latitude) | Secondary; nothing modern found |
| Canadian wideband (Jorgenson) | Nothing located; the Canadian CRC took part in WHISPER (above) and DAMSON | Not obtained |
| ELOISE (Gardiner-Garden et al. 2019; Ayliffe et al. 2019; Heitmann et al. 2018; Heitmann 2020) | Abstracts: mid-latitude Australian campaign from September 2015 that recorded ionograms and channel scattering functions on angle-of-arrival arrays. Heitmann's 2020 PhD thesis gives within-dwell variability statistics (section H); no Doppler- or delay-spread distributions were extracted | Abstract; thesis read in part |
| SuperDARN ground scatter | Review statements [Nishitani2019], abstracts [Frissell2014; Frissell2022; Chakraborty2018] and the Doppler-flash study [Chakraborty2022] | Read in part + abstract; flare Doppler-flash duration and one velocity value extracted, no general Doppler-magnitude statistics |
| Amateur digital modes under measured spreads | Simulations only (below) | No measured-spread study found |
| Books (Goodman 1992; Maslin 1987; Davies 1990; Johnson et al. 2013; Fabrizio 2013) | Nothing | Not obtained; Davies 1965 (NBS Monograph 80) read instead |

---

## Moderate → adverse → worst-case parameter table

Scope: mid-latitude oblique, NVIS and long multi-hop paths, and MF at night. "Worst case" entries are the largest values reported in the sources read, or standard stress values; where the cell combines things never observed together, it says so.

| Parameter | Moderate | Adverse | Worst case / outage |
|---|---|---|---|
| Number of modes and relative power | 3–4 modes; strongest about −1.5 dB, others −6 to −18 dB [Watterson1969 Tables 4, 6, 8; T; read] | 2 comparable modes (1F2 and 2F2 "majority of the time" on 300–400 km paths) [Cerwin2026; P; read] | Modes appearing and vanishing at dawn and dusk; loss of all modes below the absorption-limited frequency for about 20 min in a flare [Collins2022p; Chum2018] |
| Multipath delay, one-hop oblique 1,000–2,500 km | 0.25–1.1 ms [Watterson1969; BS62]; composite medians 0.1–0.2 ms on 1,440–1,800 km UK paths [SW2011] | 1–2 ms (F.1487 mid-latitude moderate and disturbed) [S; read]; 95th percentiles mostly 1–3 ms, up to 3.6 ms on winter nights at sunspot maximum (composite, UK paths) [SW2011; P; read] | About 3 ms at f/MUF near 0.5 on 2,500 km; about 5 ms on 1,000 km [Mono80 §4.8; B; read]; 5.8 ms as a 95th percentile on equinox nights at 6.95 MHz in the 2003 processing that kept weak modes [WS2003] |
| Multipath delay, NVIS | 0.3 ms mean; often single-path [Male2021] | 1.9–2.9 ms (second F echo; E plus F2) [Cerwin2026; Male2021; Maso2020] | 3.8 ms measured (third echo); 5.5 ms with ground wave; 7 ms in F.1487 "disturbed NVIS"; about 10 ms in one night-time case study on a 170 km UK path [Cerwin2026; Maso2020; F1487; Hervás2020, secondary] |
| Multipath delay, 12,700 km | 0.2–1 ms by day above 12 MHz [Ads2013] | 1.5–2.5 ms at night (80 % width) [Vilella2008] | 3.2–3.5 ms (window-limited); 7–8 ms quoted for long paths [Ads2013; Alsina2016; NTIA87 citing Hoff & King] |
| Intra-mode spread | 20–50 µs [NTIA87; NTIA88] | 100 µs (night) [NTIA87] | 240 µs/MHz dispersion in mid-latitude spread-F; diffuse spread to about 2 ms on auroral paths (outside this scope) [NTIA90] |
| Doppler shift | ±0.1–0.3 Hz; TID 0.24–0.31 Hz peak-to-peak [Luo2021; Reznychenko2020] | ±0.5–1 Hz (sunrise, pre-dawn, eclipse) [Fernandes2026] | ±1.5 Hz (moderate storm); −4 Hz (sunrise over four hops); 2–3 Hz at 5 MHz vertical in disturbed periods; flares mostly under 1 Hz for about 4 min, but about 3.4–3.9 Hz for about a minute (X1.6, 3.6–7 MHz vertical incidence) and 7.8 Hz (M7.9, low latitude), extreme 50 Hz at 10 MHz [Luo2021; Ads2013; Chum2018; DaviesBaker1966 abstract] |
| Rate of change of shift | 0.2–1 mHz/s [E] | 1–5 mHz/s [E] | About 0.1–0.25 Hz/s for tens of seconds in flares [Chum2018 Fig. 4c; Mono80 Fig. 6.4; read by eye] |
| Doppler spread, per mode or composite | 0.01–0.15 Hz (2σ) [Watterson1969; BS62 via E]; composite (80 %) medians at or below the 0.5 Hz resolution on UK paths [SW2011] | 0.5–1 Hz (2σ; F.1487 moderate and disturbed) [S]; 95th percentiles 1.3–2.5 Hz (80 % width) on UK paths in most conditions [SW2011] | 95th percentiles of 10–13 Hz at 10–14 MHz on winter nights at sunspot maximum on a trough-oriented UK path, 15.7 Hz in the 2003 processing [SW2011; WS2003]; 2–2.5 Hz (80 % width, long path at night); spectra about ±1.5 Hz wide for hours in a storm [Ads2013; Luo2021]. No source read gives 30 Hz outside high latitudes |
| Coherence time (complex correlation 0.5) | 4–40 s [E from the row above] | 0.4–0.8 s [E] | 0.15–0.2 s [E; Alsina2016 quotes about 150 ms]; about 37–48 ms if a 10–13 Hz composite (80 %) width were one Gaussian component [E: 0.375 × 1.28/W] |
| Fade depth | Rician, K up to 14 dB near the MUF on one mode; deciles narrower than +5/−8 dB [BS62] | Rayleigh, deciles +5/−8 dB [P842; S] | Two near-equal slow modes: nulls over 20 dB lasting tens of seconds; 30–60 dB amplitude swings in a storm [E; Luo2021] |
| Fade duration at −10 dB | 2–19 s mean (2σ 0.1–0.01 Hz) [E] | 0.2–0.4 s (1–0.5 Hz) [E] | Fades comparable with the 11.6 s bit of rows 4, 5 and 7 at the slow, quiet end; millisecond fades only in the 30 Hz stress case [E] |
| Selectivity across 225 Hz | Flat (τ ≤ 1 ms: correlation ≥ 0.76) [E] | Partly to fully selective (1.5–2 ms) [E] | Carriers 75 Hz apart decorrelated (τ ≈ 7 ms) [E] |
| Stationarity interval | 10–13 min samples selected as the most nearly stationary [Watterson1969; NTIA87] | "a few minutes" [BS62] | Under a minute at mode onset or loss and in flares (inferred, not measured) [E] |
| MF night: delay | One skywave hop 0.4 ms (E) or more than 1.2 ms (F) behind the ground wave at 100 km, about 0.16 and 0.45 ms at 500 km [P1321 Fig. 3, read by eye; S] | Several hops within 3 ms [P1321 §5; S; read] | Not found |
| MF night: fading | 10–30 fades/h; single-mode σ about 3 dB [P1321] | Lower-decile fades 1.5–2.5 per hour, gamma-distributed durations averaging about 3 minutes at 539 kHz [P1321 App. 2; E] | Night fading zone with comparable ground and sky waves: deep nulls [P1321] |
| MF night: Doppler shift | About 0.1 Hz [Trop2025] | — | About 1 Hz at the terminators [Chilcote2015 abstract] |

The simulator's presets are compared with this table in [Comparison with the modem's presets](#comparison-with-the-modems-presets).

---

## How the literature models or simulates this

(Simulators and standards are covered in depth in [33](33-hf-channel-models-and-simulators.md); this section records only what bears on dispersion values and validity.)

- **Watterson / Gaussian-scatter tapped delay line** [F1487 Annex 1; F520]. Each tap is Rayleigh with a Gaussian spectrum, optionally two magneto-ionic components and a specular term. Validated domain: three 10–13 minute samples on one 1,294 km mid-latitude path in November 1967, selected for stationarity; valid over 2.5–12.5 kHz [NTIA87; NTIA90]. Known failures: no diffuse within-mode delay spread (spread-F), no non-stationarity, Gaussian rather than the Lorentzian shape seen in NRL and SRI data, no delay–Doppler coupling ("slant") [NTIA90 §1–2; NTIA92; T; read].
- **Representative two-path classes** [F1487 Annex 3; F520 Annex 1]. Mid-latitude 0.5 ms/0.1 Hz, 1 ms/0.5 Hz, 2 ms/1 Hz; NVIS disturbed 7 ms/1 Hz. Exceedance statistics (5 % of time) are given only for low latitudes (4 ms, 3 Hz) and high latitudes (5 ms, 25 Hz) [F1487 Annex 3 §5; S; read]. F.339 uses the 2 ms/1 Hz class as its "fading channel".
- **DRM channel set** [DRM Table B.1; BS2144]. Channel 3 ("US Consortium"): four paths at 0, 0.7, 1.5, 2.2 ms with gains 1, 0.7, 0.5, 0.25, shifts 0.1–1.0 Hz and spreads 0.1–2.0 Hz. Channel 5: two paths 4 ms apart with 2 Hz. Channel 6 ("tropical NVIS"): 0, 2, 4, 6 ms with shifts 0–3.6 Hz and spreads 0.1–7.2 Hz. Channel 3 is the only standard profile seen in this review with unequal modes and per-mode shifts, which is closer to Watterson's measurements than the two-path classes.
- **ITS wideband model** [NTIA88; NTIA90; NTIA92]. Delay shape, Doppler shape (Gaussian or Lorentzian), Doppler shift and its variation with delay are parameters per mode, fitted to NRL and SRI scattering functions; validated by visual comparison on a handful of paths (126 km mid-latitude, auroral, polar, equatorial).
- **Virtual-height geometry** [Cerwin2026]. Reproduces measured inter-mode delays to within the scatter of ionosonde heights on 9–1,350 km paths; flat-Earth and spherical versions agree within 2.5 % to 2,900 km.
- **Ray tracing through a climatological ionosphere** (PHaRLAP with IRI-2020) [Fernandes2026 §3.2.3–3.2.4]. Explains which hop modes exist by time of day; does not reproduce short-term Doppler variability, which the authors treat statistically.
- **Irregularity scattering theory** (Coleman 1996, via Aricoche2026 §4). Predicts rms Doppler and range spreading from irregularity scale, strength and drift; "plausible but highly sensitive to parameters which are not all well constrained". A related physically based wideband simulator sums E, low-angle F and high-angle F paths through a three-dimensional ionosphere with time-varying irregularities, illustrated on a 1,000 km path [Gherm2005; P; abstract].
- **Slow mode-power process.** Mode path loss in decibels as a first-order autoregressive Gaussian process over 15-minute intervals [RifkinBello1994; P; abstract]. This is the only published model found for the minutes-scale variation that the Watterson model omits; its parameters were not seen.
- **Doppler shift from ionograms.** Equations for a time-varying parabolic layer reproduce measured Doppler shifts from ionograms taken every 5 minutes [Lynn2009; P; abstract].
- **MF** [P1321]. Ground wave plus discrete E and F hops with log-normal amplitudes; gamma-distributed fade durations.
- **Fading margins** [P842; F339]. Rayleigh-like deciles within the hour.
- **Weak-signal amateur modes** [FST2020 Table 6; G; read]. Simulated FT8 thresholds (50 % decode, 2,500 Hz reference): −20.8 dB on AWGN; −20.0, −18.8 and −18.6 dB on the mid-latitude quiet, moderate and disturbed classes; −8.6 dB on high-latitude moderate (10 Hz, 3 ms), where spread exceeds the tone spacing. FST4's 1,800 s mode needs channel spread below its 0.089 Hz keying rate [WSJTX, FST4 section; G; read]. These are simulations on the F.1487 classes, not measurements.
- **Open data sets.** Grape Doppler records (Zenodo 10.5281/zenodo.6622111 and 10.5281/zenodo.13637199) [Collins2023; Fernandes2026]; AM-band Doppler spectra (Harvard Dataverse 10.7910/DVN/L3JXIH) [Trop2025]; WWV time-of-flight data (Zenodo 10.5281/zenodo.7191497) [Cerwin2020].

---

## What a high-fidelity simulation would need

These are options with their limits, not a design. Better approaches may exist, particularly ones driven directly by recorded data.

**Inputs.** Path length and geometry (NVIS, one-hop, multi-hop); frequency relative to the MUF of each mode; time relative to the terminator; season and solar activity; geomagnetic state; flare occurrence. For MF, the ground-wave to skywave ratio.

**Model-structure options.**

1. *Static multi-tap Watterson with measured-like mode sets.* Three or four taps with unequal powers, per-mode shifts and spreads (for example the Watterson I1–I3 sets, or DRM channel 3). Cheap and traceable. It still assumes stationarity and Gaussian spectra.
2. *Add per-mode Rician components and O/X sub-taps.* Two sub-taps per mode at the same delay with shifts differing by 0.01–0.06 Hz reproduce polarisation beats of 17–74 s (Epstein's daytime average corresponds to about 2 minutes). Evidence for K values is thin and partly contradictory ([BS62] and [Wagner1988] support a specular part; [Boys1968] says it fades slowly), so a specular term with slow log-normal modulation of each mode's power (first-order autoregressive in decibels over 15-minute blocks, after [RifkinBello1994]) may be closer to the data than a fixed K. The modulation parameters were not obtained.
3. *Geometry-driven taps.* Compute delays and relative Doppler from a time-varying virtual height (diurnal trend, plus TID sinusoids of 0.1–0.3 Hz peak-to-peak at 20–40 minutes, plus terminator ramps). This couples delay, shift and mode count physically and reproduces Doppler scaling with hop number. It needs a mode-existence rule (take-off angle against critical frequency) and says nothing about spread.
4. *Event overlays.* Flare transients (a positive pulse, mostly under 1 Hz and about 4 minutes long, occasionally several hertz or more, rising and falling at up to about 0.1–0.25 Hz/s and often followed by a brief negative swing, then absorption with onset in about 100 s and recovery over about half an hour), storm intervals (shift excursions of ±1.5 Hz, widened spectra, 30–60 dB amplitude swings), mode onset and loss at dawn and dusk. Magnitudes are documented; durations and rise times less so.
5. *Non-Gaussian spectra and diffuse delay* (ITS model): Lorentzian Doppler shape and within-mode delay spread for spread-F. Mid-latitude parameter statistics are missing.
6. *Replay.* Drive tap shifts directly from Grape or AM-band Doppler records, and delays from time-of-flight records. This gives real non-stationarity but only a single dominant-peak frequency per second, with no spread or per-mode amplitude.
7. *Ray tracing through a perturbed ionosphere.* Highest physical fidelity for mode structure; costly, and its Doppler statistics have not been validated against data in the sources read.

**Observables to validate against.**

- Inter-mode delay against path length and layer height (the 0.7–1.9 ms values in B1–B2).
- Daily Doppler-shift pattern and its percentile envelope (pre-dawn ±1 Hz, sunrise +0.5 Hz, day ±0.25 Hz at 10 MHz over 2,590 km); TID amplitude and period medians.
- Per-mode spread against hop number (0.01–0.03 Hz for E, 0.14–0.18 Hz for 1F, 0.34 Hz for 2F in the Watterson samples).
- Amplitude distribution per mode and composite (Rayleigh share, K values), deciles of +5/−8 dB.
- Envelope correlation time of 10–20 s for one-hop modes and a few seconds for multi-hop; polarisation rotation averaging 0.25 turn per minute by day and near zero for much of the night.
- The fraction of 10-minute intervals that look stationary (Watterson selected twelve near-stationary intervals from about 17 hours, read by NTIA87 as about 12 %; Balser & Smith classed 52 of 146 ten-minute records, 36 %, as probably non-stationary).
- Percentiles of composite Doppler and multipath spread by season, time of day and sunspot number (the UK trough-path tables in B1 and E).
- Frequency correlation across 75–225 Hz against delay.
- For MF: 10–30 fades per hour and minute-scale fade durations.

For this modem the simulation should be able to produce fades that last a whole bit, Doppler shifts that drift by tenths of a hertz within a message, and delays long enough (2 ms and more) to decorrelate the three carriers. In the present channel, as described in section 5 of the conventions document, the echo-to-direct ratio varies smoothly between zero and one, the echo and common frequencies wander inside the preset's Doppler value, both on a synthetic knot spacing, and the direct path never disappears; whether a preset produces the first two therefore depends on that synthetic time scale rather than on any measured one.

---

## Comparison with the modem's presets

Each preset is read in both of its meanings ([00](00-method-and-modem-context.md) section 5). In the simulator the Doppler figure bounds the offset and smooth wander of a two-path family whose direct path never disappears and whose echo delay is drawn inside the preset's delay; in F.1487 the same figure is the 2σ fading width of each of two equal Rayleigh paths with a fixed differential delay and no shift, a deliberately simplified test channel [F1487 Annex 3 §1; S; read]. The Sub9 and EME presets do not concern this chapter.

| Preset (Doppler, delay) | F.1487 namesake | Against this chapter's evidence |
|---|---|---|
| None (0, 0) | — | No skywave channel is static: even the quietest measured modes have 0.006–0.03 Hz of spread and quiet-day shifts of ±0.1–0.2 Hz, with three or four modes within about 1 ms (B1, D, E). |
| HF best (0.1 Hz, 0.5 ms) | Mid-latitude quiet (F.520 "good") | As a 2σ spread it is a fair single-number summary of a quiet daytime one-hop channel (Watterson composite 0.12–0.14 Hz by day, 0.07 Hz at night), but not a benign bound: quiet modes are often slower (0.006–0.07 Hz), which lengthens fades towards a bit; there are three or four unequal modes within 0.25–1.1 ms rather than two equal ones; some modes are Rician; and the modes carry shifts differing by up to about 0.2 Hz. As a bound on offset and wander it is smaller than routine quiet-day shifts (±0.1–0.2 Hz; travelling-disturbance swings of 0.24–0.31 Hz peak-to-peak) and far smaller than the ±1 Hz before sunrise. A 0.5 ms echo keeps 225 Hz nearly flat (correlation 0.94). |
| HF moderate (10 Hz, 6 ms) | *Low-latitude* disturbed; not a mid-latitude class | As a spread, 10 Hz is 10–100 times typical mid-latitude values. It is reached only as a 95th percentile on winter nights at sunspot maximum on the trough-oriented UK path (10–13 Hz of 80 % width at 10–14 MHz, about 8–10 Hz as a Gaussian 2σ [E], from reflections off the trough wall or the auroral zone) and approached as ±5 Hz in one 170 km night-time case study. As a bound on shift it covers every reported mid-latitude excursion (±1.5 Hz in a storm, −4 Hz over four hops at sunrise, 3.4–7.8 Hz in strong flares) except the 50 Hz flare extreme. A 6 ms delay is reached only by multi-hop NVIS (F.1487 disturbed NVIS 7 ms; about 10 ms in one case study), approached by one 95th-percentile composite spread in the 2003 UK processing (5.8 ms), and exceeded by the 7–8 ms quoted for long paths. No mid-latitude source read reports 10 Hz and 6 ms together. |
| HF worst (30 Hz, 7 ms) | High-latitude disturbed | No source in this chapter reports 30 Hz of spread at mid-latitudes, on NVIS or on long paths; the largest mid-latitude statistic is a 95th percentile of 13.3 Hz (15.7 Hz in the 2003 processing) on the trough-oriented path. A 7 ms delay occurs only as multi-hop NVIS or long-path multipath, with about 1 Hz of spread. The class does match high-latitude data: 5 %-exceedance spreads of 2–55 Hz and 1–11 ms on four high-latitude paths [Angling1998; P; abstract], and "tens of hertz" with hundreds of microseconds on disturbed polar, auroral and equatorial one-hop F paths [Basler1988; P; abstract]. As a bound on shift it exceeds everything in scope except the 50 Hz flare extreme. |
| Aurora best (10 Hz, 3 ms); Aurora worst (55 Hz, 11 ms) | High-latitude moderate; upper ends of the DAMSON 5 % ranges | Outside this chapter's scope ([32](32-skywave-dispersion-highlatitude-equatorial.md)). Winter nights at sunspot maximum on the trough-oriented path (95th percentiles of 10–13 Hz and 2.2–3.6 ms) resemble Aurora best, as the authors note for the F.1487 high-latitude moderate class [SW2011 §3.1]. Nothing in scope approaches Aurora worst. |

The mid-latitude adverse corner is therefore closer to 1–2.5 Hz of composite spread (the 95th percentile in most conditions) with 2–7 ms of multipath than to the HF moderate or HF worst preset. What the presets do not represent is structural: shift excursions of 1–4 Hz with drift, flare pulses that change at about 0.1–0.25 Hz/s, modes that switch on and off at dawn and dusk, three or four unequal modes with different shifts, and slow fades lasting a bit or longer. On level, this chapter's evidence is limited to flare fadeouts lasting 20–60 minutes and 30–60 dB swings in a storm, which the constant level of the present channel represents only through its single ramped attenuation interval (00 §5); noise and the −164 dBm/Hz default are treated in [30](30-skywave-signal-level-and-outages.md) and [50](50-noise-and-interference-mf-hf.md).

---

## Contradictions, open questions, and things not found

**Contradictions and tensions.**

- *Stationarity.* "About 10 minutes" is widely quoted from Watterson, but it is only the length of the samples he selected as the most nearly stationary from about 17 hours of recordings (NTIA 87-221 reads the selection as 12 % of the record), and Balser & Smith argue for a few minutes. No 2015–2026 measurement found resolves this; the ELOISE within-dwell statistics cover only 6.4 s.
- *Fading period.* Davies (1965) gives fractions of a second to a few seconds for interference and polarisation fading; Watterson's measured shifts imply 6 s to many minutes on a quiet path. Both may be right for different conditions.
- *Distribution.* Rayleigh, Rice, log-normal and "non-stationary" are all reported; F.339 and P.842 assume different short-term distributions (log-normal with 7 dB against Rayleigh-like 5/8 dB). Whether single modes have a true specular component is itself disputed: Balser & Smith, Davies and the NRL data say often yes; Boys (1968) says the apparent specular part is a slowly fading ray bundle. Witvliet & Alsina-Pagès (2017), citing Davies (1990, p. 237), offer one reconciliation of the distributions: short observations on long links show Rayleigh fading, longer ones a log-normal distribution [Wit17 §4.1.1; P; read; secondary for Davies 1990].
- *Spectrum shape.* Gaussian (Watterson, F.1487, DRM) against Lorentzian (ITS, from NRL and SRI data).
- *Flare shifts.* Not a contradiction but a wide distribution: most are under 1 Hz and last about 4 minutes, "a few" hertz is typical of large events (Davies 1965; Chum 2018, with about 3.4–3.9 Hz for an X1.6 flare and 7.8 Hz for an M7.9 flare), the 1974 atlas classes deviations of at least 4.5 Hz as large, and 50 Hz is the reported extreme at 10 MHz (Davies & Baker 1966, abstract). No modern distribution of flare-induced shifts was found.
- *Mid-latitude Doppler spread.* The F.1487 mid-latitude classes stop at 1 Hz and Watterson's quiet path gave 0.07–0.14 Hz, whereas the UK trough-oriented paths gave 95th percentiles of 1.5–2.5 Hz in most conditions and 10–13 Hz on winter nights at sunspot maximum. The UK medians sit at the sounder's 0.5–0.6 Hz resolution, so the two data sets cannot be compared at the low end, and the trough paths lie at the poleward edge of the mid-latitudes.
- *NVIS delay.* Measured mid-latitude NVIS totals stop at 2.7–3.8 ms (5.5 ms with ground wave on Antarctic links), while one WHISPER case study at 170 km reports about 10 ms; that value is known only through a review and its estimator is not stated.
- *NVIS coherence time.* 1.46 s (quoted by Male et al.) against 10–20 s (Maso et al.; Ivanov et al.).
- *Antarctica–Spain maxima.* The reported maxima (3.5 ms, 2.5 Hz) equal the analysis window limits and are likely truncated; the path is trans-equatorial, so its night-time spread is not a mid-latitude value.
- *Latitude labels.* The Livingston Island NVIS and Alaska trough results sit at the edge of this file's scope.

**Looked for and not found.**

- Percentile statistics of Doppler spread and multipath on mid-latitude paths other than the UK trough-oriented paths: the MITRE statistics for three US paths (Clune et al. 1991) and a Chinese conference paper on oblique-sounding channel statistics (Wang et al. 2018) are known only from index records, and Jorgenson's Canadian work was not located.
- Measured Doppler spread during mid-latitude spread-F and during storms as a statistical estimator (only spectrogram widths and the 240 µs/MHz dispersion figure).
- Measured fade-duration and fade-depth distributions for HF mid-latitude paths (those given are derived); the only measured duration distribution found is for night-time MF at 539 kHz.
- Any 2015–2026 measurement of the stationarity interval at the scale of minutes (only the 6.4 s ELOISE within-dwell statistics).
- Delay or Doppler-spread soundings between 1 and 3 MHz at night, and 160 m amateur-band studies.
- Measured studies of amateur weak-signal modes against measured spreads. FST4W "spectral spread" reports from the WSPR-daemon community, recalled by the research pass as showing tens to hundreds of millihertz on HF paths, could not be located [recollection, unverified].
- Distributions of Faraday-fading period (only Epstein's average rotation rates and the derived beat periods).
- Delay- and Doppler-spread distributions from ELOISE (the thesis read gives within-dwell variability only), and Doppler-magnitude statistics from SuperDARN ground scatter beyond the flare Doppler flash.

**Not obtained in full (search budget or access).** Table J gives the status of each named source. Not obtained at all: CCIR Reports 549 and 266, the Canadian wideband work, the MITRE and Chinese conference papers named above, the WHISPER report, DRM field-trial reports, modern Indian sounding, and the five books. The two 2026 Frontiers papers by Aricoche et al. and Fernandes et al. were read, but neither is a stationarity analysis; Cerwin et al. (2026) proved more relevant.

---

## References

Access: **read** = supporting passage read in full text; **abstract** = abstract or search snippet only; **secondary** = quoted through the named source.

**Standards and ITU reports**

- [F1487] Rec. ITU-R F.1487 (05/2000), *Testing of HF modems with bandwidths of up to about 12 kHz using ionospheric channel simulators*. https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf — read; [S].
- [F520] Rec. ITU-R (CCIR) F.520-2 (1992, withdrawn), *Use of high frequency ionospheric channel simulators*. https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.520-2-199203-W!!PDF-E.pdf — read; [S].
- [P1407] Rec. ITU-R P.1407-8 (09/2021), *Multipath propagation and parameterization of its characteristics*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.1407-8-202109-I!!PDF-E.pdf — read; [S].
- [P1321] Rec. ITU-R P.1321-5 (07/2015), *Propagation factors affecting systems using digital modulation techniques at LF and MF*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.1321-5-201507-I!!PDF-E.pdf — read; [S].
- [P842] Rec. ITU-R P.842-5 (09/2013), *Computation of reliability and compatibility of HF radio systems*. https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.842-5-201309-I!!PDF-E.pdf — read; [S].
- [F339] Rec. ITU-R F.339-8 (02/2013), *Bandwidths, signal-to-noise ratios and fading allowances in HF fixed and land mobile radiocommunication systems*. https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.339-8-201302-I!!PDF-E.pdf — read; [S].
- [BS2144] Report ITU-R BS.2144 (05/2009), *Planning parameters and coverage for Digital Radio Mondiale (DRM) broadcasting at frequencies below 30 MHz*. https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BS.2144-2009-PDF-E.pdf — read in part; [S].
- [DRM] ETSI ES 201 980 V4.2.1 (2021-01), *Digital Radio Mondiale (DRM); System Specification*, Annex B. https://www.etsi.org/deliver/etsi_es/201900_201999/201980/04.02.01_60/es_201980v040201p.pdf — read (Annex B) by the first pass; the server refused automated access on re-check; [S].

**Technical reports and monographs**

- [NTIA87] Hoffmeyer, J. A., & Nesenbergs, M. (1987). *Wideband HF modeling and simulation*. NTIA Report 87-221. https://its.ntia.gov/publications/download/87-221_ocr.pdf — read; [T].
- [NTIA88] Vogler, L. E., & Hoffmeyer, J. A. (1988). *A new approach to HF channel modeling and simulation, Part I: Deterministic model*. NTIA Report 88-240. https://its.ntia.gov/publications/download/TR-88-240.pdf — read in part; [T].
- [NTIA90] Vogler, L. E., & Hoffmeyer, J. A. (1990). *Part II: Stochastic model*. NTIA Report 90-255. https://its.ntia.gov/publications/download/90-255_ocr.pdf — read; [T].
- [NTIA92] Vogler, L. E., & Hoffmeyer, J. A. (1992). *Part III: Transfer function*. NTIA Report 92-284 (cover misprinted 93-284). https://www.its.ntia.gov/publications/download/92-284.pdf — read in part; [T].
- [TN133] Salaman, R. K. (1962). *Historical survey of fading at medium and high radio frequencies*. NBS Technical Note 133. https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote133.pdf — read; [T].
- [Mono80] Davies, K. (1965). *Ionospheric Radio Propagation*. NBS Monograph 80. https://nvlpubs.nist.gov/nistpubs/Legacy/MONO/nbsmonograph80.pdf — read (§3.2.3, §4.4, §4.8, §5.4, §6.2.3); [B].
- [Watterson1969] Watterson, C. C., Juroshek, J. R., & Bensema, W. D. (1969). *Experimental verification of an ionospheric channel model*. ESSA Tech. Rep. ERL 112-ITS 80; DTIC AD0699486. https://archive.org/details/DTIC_AD0699486 — read (archive.org scan and OCR text; the DTIC server, https://apps.dtic.mil/sti/pdfs/AD0699486.pdf, refused automated access); [T].
- [Donnelly1974] Donnelly, R. F., Berger, E. L., Busman, J. D., Henson, B., Jones, T. B., Lerfald, G. M., Najita, K., Retallack, W. M., & Wagner, W. J. (1974). *An atlas of extreme ultraviolet flashes of solar flares observed via sudden frequency deviations during the ATM-SKYLAB missions*. World Data Center A for Solar-Terrestrial Physics, Report UAG-36, NOAA. https://repository.library.noaa.gov/view/noaa/1029 — read (repository text version, §§1–4); [T].
- [Heitmann2020] Heitmann, A. J. (2020). *Characterising spatial and temporal ionospheric variability with a network of oblique angle-of-arrival and Doppler ionosondes*. PhD thesis, University of Adelaide. https://hdl.handle.net/2440/130401 — read in part (abstract, §§6.1–6.2); [G, thesis].
- Davies, N., Willink, T., Angling, M., & Cannon, P. (2004). *Initial results from WHISPER: a wideband HF ionospheric sounder for propagation environment research*. Bath, UK. — not obtained; secondary through [Hervás2020]; [G].

**Peer-reviewed papers**

- [Watterson1970] Watterson, C. C., Juroshek, J. R., & Bensema, W. D. (1970). Experimental confirmation of an HF channel model. *IEEE Trans. Commun. Technol.* 18(6), 792–803. doi:10.1109/TCOM.1970.1090438 — abstract; [P].
- [BS62] Balser, M., & Smith, W. B. (1962). Some statistical properties of pulsed oblique HF ionospheric transmissions. *J. Res. NBS* 66D(6), 721–730. https://nvlpubs.nist.gov/nistpubs/jres/66D/jresv66Dn6p721_A1b.pdf — read; [P].
- Shepherd, R., & Lomax, J. B. (1967). Frequency spread in ionospheric radio propagation. *IEEE Trans. Commun. Technol.* 15(2), 268–275. doi:10.1109/TCOM.1967.1089568 — abstract (search snippet); bibliographic data confirmed in Crossref; [P].
- [DaviesBaker1966] Davies, K., & Baker, D. M. (1966). On frequency variations of ionospherically propagated HF radio signals. *Radio Science* 1(5), 545–556. doi:10.1002/rds196615545 — abstract (Crossref record); [P].
- David, F., Franco, A. G., Sherman, H., & Shucavage, L. B. (1969). Correlation measurements on an HF transmission link. *IEEE Trans. Commun. Technol.* 17(2), 245–256. doi:10.1109/TCOM.1969.1090066 — abstract (search snippet); [P].
- [RifkinPerry1994] Rifkin, R., & Perry, B. D. (1994). Fade measurements on the wide bandwidth HF channel. *Radio Science* 29(5), 1339–1353. doi:10.1029/94RS00834 — abstract (Crossref record); [P].
- [RifkinBello1994] Rifkin, R., & Bello, P. A. (1994). Representation of propagation mode fading for the midlatitude wide bandwidth HF channel. *Radio Science* 29(4), 717–722. doi:10.1029/93RS03031 — abstract; [P].
- [Wagner1985] Wagner, L. S., & Goldstein, J. A. (1985). High-resolution probing of the HF ionospheric skywave channel: F2 layer results. *Radio Science* 20(3), 287–302. doi:10.1029/RS020i003p00287 — abstract; [P].
- [Wagner1987] Wagner, L. S. (1987). Characteristics of mid-latitude sporadic E observed with a wideband HF channel probe. *Radio Science* 22(5), 728–744. doi:10.1029/RS022i005p00728 — abstract; [P].
- [Wagner1988] Wagner, L. S., Goldstein, J. A., & Meyers, W. D. (1988). Wideband probing of the transauroral HF channel: Solar minimum. *Radio Science* 23(4), 555–568. doi:10.1029/RS023i004p00555 — abstract; [P].
- [Basler1988] Basler, R. P., Price, G. H., Tsunoda, R. T., & Wong, T. L. (1988). Ionospheric distortion of HF signals. *Radio Science* 23(4), 569–579. doi:10.1029/RS023i004p00569 — abstract; [P].
- [Angling1998] Angling, M. J., Cannon, P. S., Davies, N. C., Willink, T. J., Jodalen, V., & Lundborg, B. (1998). Measurements of Doppler and multipath spread on oblique high-latitude HF paths and their use in characterizing data modem performance. *Radio Science* 33(1), 97–107. doi:10.1029/97RS02206 — abstract; [P].
- [WS2003] Warrington, E. M., & Stocker, A. J. (2003). Measurements of the Doppler and multipath spread of HF signals received over a path oriented along the midlatitude trough. *Radio Science* 38(5), 1080. doi:10.1029/2002RS002815; https://figshare.le.ac.uk/articles/journal_contribution/Measurements_of_the_Doppler_and_multipath_spread_of_HF_signals_received_over_a_path_oriented_along_the_mid-latitude_trough/10079843 — read (University of Leicester repository copy); [P].
- [SW2011] Stocker, A. J., & Warrington, E. M. (2011). The effect of solar activity on the Doppler and multipath spread of HF signals received over paths oriented along the midlatitude trough. *Radio Science* 46, RS1014. doi:10.1029/2010RS004482; https://figshare.le.ac.uk/articles/journal_contribution/The_effect_of_solar_activity_on_the_Doppler_and_multipath_spread_of_HF_signals_received_over_paths_oriented_along_the_mid-latitude_trough/10100153 — read (University of Leicester repository copy); [P].
- Clune, M. I., Fine, P. B., Freedman, J. N., Nissen, C. A., & Perry, B. D. (1991). Delay and Doppler spreading characteristics of the wide-bandwidth HF channel. *Fifth International Conference on HF Radio Systems and Techniques*, Edinburgh, pp. 194–198. — index record only; [P, conference].
- Wang, F.-F., Liu, Y.-M., & Sun, S.-J. (2018). The statistic characteristics of HF oblique sounding channel at mid and low latitude during ionospheric disturbance time. *12th International Symposium on Antennas, Propagation and EM Theory (ISAPE)*, pp. 1–4. doi:10.1109/ISAPE.2018.8634091 — bibliographic record only (Crossref); [P, conference].
- Guerra Pereda, D., Coleto, M. A., Melgar, L., Gil, U., Pena, I., Prieto, G., & Angulo, I. (2013). Measurement based near vertical incidence skywave channel model in the medium wave band. *7th European Conference on Antennas and Propagation (EuCAP)*, Gothenburg, pp. 3403–3407. — index-record abstract only (cited by [Wit17] as Guerra et al.); [P, conference].
- Lynch, J. T., Fenwick, R. B., & Villard, O. G. (1972). Measurement of best time-delay resolution obtainable along east-west and north-south ionospheric paths. *Radio Science* 7(10), 925–929. doi:10.1029/RS007i010p00925 — abstract; [P].
- [Epstein1969] Epstein, M. R. (1969). Polarization of ionospherically propagated HF radio waves with applications to radio communication. *Radio Science* 4(1), 53–67. doi:10.1029/RS004i001p00053 — abstract; [P].
- [Boys1968] Boys, J. T. (1968). Statistical variations in the apparent specular component of ionospherically reflected radio waves. *Radio Science* 3(10), 984–990. doi:10.1002/rds1968310984 — abstract; [P].
- Jacobs, J. A., & Watanabe, T. (1966). Doppler frequency changes in radio waves propagating through a moving ionosphere. *Radio Science* 1(3), 257–264. doi:10.1002/rds196613257 — abstract; theory; [P].
- [Lynn2009] Lynn, K. J. W. (2009). A technique for calculating ionospheric Doppler shifts from standard ionograms suitable for scientific, HF communication, and OTH radar applications. *Radio Science* 44(6). doi:10.1029/2009RS004210 — abstract; [P].
- [Gherm2005] Gherm, V. E., Zernov, N. N., & Strangeways, H. J. (2005). HF propagation in a wideband ionospheric fluctuating reflection channel: Physically based software simulator of the channel. *Radio Science* 40(1). doi:10.1029/2004RS003093 — abstract; [P].
- [Frissell2014] Frissell, N. A., Baker, J. B. H., Ruohoniemi, J. M., Gerrard, A. J., Miller, E. S., Marini, J. P., West, M. L., & Bristow, W. A. (2014). Climatology of medium-scale traveling ionospheric disturbances observed by the midlatitude Blackstone SuperDARN radar. *J. Geophys. Res. Space Physics* 119(9), 7679–7697. doi:10.1002/2014JA019870 — abstract; [P].
- [Frissell2022] Frissell, N. A., Kaeppler, S. R., Sanchez, D. F., Perry, G. W., Engelke, W. D., Erickson, P. J., Coster, A. J., Ruohoniemi, J. M., et al. (2022). First observations of large scale traveling ionospheric disturbances using automated amateur radio receiving networks. *Geophys. Res. Lett.* 49(5), e2022GL097879. doi:10.1029/2022GL097879 — abstract; [P].
- [Chakraborty2018] Chakraborty, S., Ruohoniemi, J. M., Baker, J. B. H., & Nishitani, N. (2018). Characterization of short-wave fadeout seen in daytime SuperDARN ground scatter observations. *Radio Science* 53(4), 472–484. doi:10.1002/2017RS006488 — abstract; [P].
- [Chakraborty2022] Chakraborty, S., Qian, L., Baker, J. B. H., Ruohoniemi, J. M., Kuyeng, K., McInerney, J. M., et al. (2022). Driving influences of the Doppler flash observed by SuperDARN HF radars in response to solar flares. *J. Geophys. Res. Space Physics* 127(6). doi:10.1029/2022JA030342; https://www.ncbi.nlm.nih.gov/pmc/articles/PMC9286435/ — read (Europe PMC full text, §§1, 4); [P].
- Gardiner-Garden, R., Cervera, M., Debnam, R., Harris, T., Heitmann, A., Holdsworth, D., Netherway, D., Northey, B., et al. (2019). A description of the Elevation sensitive Oblique Incidence Sounder Experiment (ELOISE). *Adv. Space Res.* 64(10), 1887–1914. doi:10.1016/j.asr.2019.03.036 — bibliographic record only; [P].
- Ayliffe, J. K., Durbridge, L. J., Frazer, G. J., Gardiner-Garden, R. S., Heitmann, A. J., Praschifka, J., Quinn, A. D., Scarman, G. C., & Turley, M. D. E. (2019). The DST Group high-fidelity, multichannel oblique incidence ionosonde. *Radio Science* 54(1), 104–114. doi:10.1029/2018RS006681 — abstract; [P].
- Heitmann, A. J., Cervera, M. A., Gardiner-Garden, R. S., Holdsworth, D. A., MacKinnon, A. D., Reid, I. M., & Ward, B. D. (2018). Observations and modeling of traveling ionospheric disturbance signatures from an Australian network of oblique angle-of-arrival sounders. *Radio Science* 53(9), 1089–1107. doi:10.1029/2018RS006613 — abstract; [P].
- Guo, Q., Chernogor, L. F., Garmash, K. P., Rozumenko, V. T., & Zheng, Y. (2020). Radio monitoring of dynamic processes in the ionosphere over China during the partial solar eclipse of 11 August 2018. *Radio Science* 55(2). doi:10.1029/2019RS006866 — abstract; [P].
- [Vilella2008] Vilella, C., Miralles, D., & Pijoan, J. L. (2008). An Antarctica-to-Spain HF ionospheric radio link: Sounding results. *Radio Science* 43, RS4008. doi:10.1029/2007RS003812; https://dau.url.edu/handle/20.500.14342/3414 — read; [P].
- [Ads2013] Ads, A. G., Bergadà, P., Vilella, C., Regué, J. R., Pijoan, J. L., Bardají, R., & Mauricio, J. (2013). A comprehensive sounding of the ionospheric HF radio link from Antarctica to Spain. *Radio Science* 48, 1–12. doi:10.1029/2012RS005074; https://dau.url.edu/handle/20.500.14342/3365 — read; [P].
- [Hervás2015] Hervás, M., Alsina-Pagès, R. M., Orga, F., Altadill, D., Pijoan, J. L., & Badia, D. (2015). Narrowband and wideband channel sounding of an Antarctica to Spain ionospheric radio link. *Remote Sensing* 7(9), 11712–11730. doi:10.3390/rs70911712 — read; [P].
- [Alsina2016] Alsina-Pagès, R. M., Hervás, M., Orga, F., Pijoan, J. L., Badia, D., & Altadill, D. (2016). Physical layer definition for a long-haul HF Antarctica to Spain radio link. *Remote Sensing* 8(5), 380. doi:10.3390/rs8050380 — read; [P].
- [Male2021] Male, J., Porte, J., Gonzalez, T., Maso, J. M., Pijoan, J. L., & Badia, D. (2021). Analysis of the ordinary and extraordinary ionospheric modes for NVIS digital communications channels. *Sensors* 21(6), 2210. doi:10.3390/s21062210 — read; [P].
- [Maso2020] Maso, J. M., Porte, J., Pijoan, J. L., & Badia, D. (2020). Study of NVIS channel for USN protocol definition in Antarctica. *Electronics* 9(6), 1037. doi:10.3390/electronics9061037 — read; [P].
- [Gonzalez2021] Gonzalez, T., Porte, J., Male, J., Navarro, J., Maso, J. M., Zaballos, A., Pijoan, J. L., & Badia, D. (2021). SC-FDE layer for sensor networks in remote areas using NVIS communications. *Electronics* 10(14), 1636. doi:10.3390/electronics10141636 — read; [P].
- [Hervás2020] Hervás, M., Bergadà, P., & Alsina-Pagès, R. M. (2020). Ionospheric narrowband and wideband HF soundings for communications purposes: a review. *Sensors* 20(9), 2486. doi:10.3390/s20092486; https://www.ncbi.nlm.nih.gov/pmc/articles/PMC7273218/ — read (Europe PMC full text, §3.4); [P, review].
- [Wit17] Witvliet, B. A., & Alsina-Pagès, R. M. (2017). Radio communication via Near Vertical Incidence Skywave propagation: an overview. *Telecommunication Systems* 66, 295–309. doi:10.1007/s11235-017-0287-2 — read; [P].
- [Witvliet2015] Witvliet, B. A., van Maanen, E., Petersen, G. J., Westenberg, A. J., Bentum, M. J., Slump, C. H., & Schiphorst, R. (2015). Measuring the isolation of the circularly polarized characteristic waves in NVIS propagation. *IEEE Antennas Propag. Mag.* 57(3), 120–145. doi:10.1109/MAP.2015.2445633; https://research.utwente.nl/en/publications/measuring-the-isolation-of-the-circularly-polarized-characteristi/ — abstract; [P].
- [Ivanov2020] Ivanov, V. A., Elsukov, A. A., & Ryabova, M. I. (2020). Measuring scattering function of HF NVIS channel by sounding with the use of a noise-like BPSK signal. *J. Phys.: Conf. Ser.* 1632, 012001. doi:10.1088/1742-6596/1632/1/012001 — read; [P, conference].
- [Chum2018] Chum, J., Urbář, J., Laštovička, J., Cabrera, M. A., Liu, J.-Y., Bonomi, F. A. M., Fagre, M., Fišer, J., & Mošna, Z. (2018). Continuous Doppler sounding of the ionosphere during solar flares. *Earth, Planets and Space* 70, 198. doi:10.1186/s40623-018-0976-4 — read; [P].
- [Chum2021] Chum, J., Podolská, K., Rusz, J., Baše, J., & Tedoradze, N. (2021). Statistical investigation of gravity wave characteristics in the ionosphere. *Earth, Planets and Space* 73, 60. doi:10.1186/s40623-021-01379-3 — read; [P].
- [Collins2023] Collins, K., Gibbons, J., Frissell, N., Montare, A., Kazdan, D., et al. (2023). Crowdsourced Doppler measurements of time standard stations demonstrating ionospheric variability. *Earth Syst. Sci. Data* 15, 1403–1418. doi:10.5194/essd-15-1403-2023 — read; [P].
- [Fernandes2026] Fernandes, S., Perry, G. W., Trigo, T., Frissell, N. A., Gibbons, J., & Collins, K. (2026). Characterizing ionospheric variability through HF Doppler measurements: a statistical and numerical ray tracing analysis. *Front. Astron. Space Sci.* 13. doi:10.3389/fspas.2026.1713968 — read; [P].
- [Aricoche2026] Aricoche, J. A., Hysell, D. L., & Morton, Y. J. (2026). Probing the main ionospheric trough with an Alaskan HF beacon network. *Front. Astron. Space Sci.* 13. doi:10.3389/fspas.2026.1848834 — read; [P].
- [Cerwin2026] Cerwin, S. A., McMahan, J. T., Papadopoulos, A. S., Piccini, G. N., Frissell, N. A., Collins, K. V., Montare, A., Bilberry, P., Blackshear, S., & Themens, D. R. (2026). HamSCI HF multipath propagation mode analysis using amateur radios and audio waveforms sensitive to time difference of arrival. *Front. Astron. Space Sci.* 13, 1723511. doi:10.3389/fspas.2026.1723511 — read; [P].
- [Trop2025] Trop, C. C., LaBelle, J., Erickson, P. J., Zhang, S.-R., McGaw, D., & Kovacs, T. (2025). Tracking traveling ionospheric disturbances through Doppler-shifted AM radio transmissions. *Atmos. Meas. Tech.* 18, 1909–1925. doi:10.5194/amt-18-1909-2025 — read; [P].
- [Chilcote2015] Chilcote, M., LaBelle, J., Lind, F. D., Coster, A. J., Miller, E. S., Galkin, I. A., & Weatherwax, A. T. (2015). Detection of traveling ionospheric disturbances by medium-frequency Doppler sounding using AM radio transmissions. *Radio Science* 50(3), 249–263. doi:10.1002/2014RS005617 — abstract (Crossref record); also secondary through [Trop2025]; [P].
- [Reznychenko2020] Reznychenko, A. I., Koloskov, A. V., Sopin, A. O., & Yampolski, Y. M. (2020). Statistic of seasonal and diurnal variations of Doppler frequency shift of HF signals at mid-latitude radio path. *Radio Physics and Radio Astronomy* 25(2), 118–135 (in Russian). doi:10.15407/rpra25.02.118 — read; [P].
- [Luo2021] Luo, Y., Chernogor, L., Garmash, K., Guo, Q., Rozumenko, V., & Zheng, Y. (2021). Dynamic processes in the magnetic field and in the ionosphere during the 30 August–2 September 2019 geospace storm: influence on high frequency radio wave characteristics. *Ann. Geophys.* 39, 657–685. doi:10.5194/angeo-39-657-2021 — read; [P].
- [Nishitani2019] Nishitani, N., Ruohoniemi, J. M., Lester, M., et al. (2019). Review of the accomplishments of mid-latitude Super Dual Auroral Radar Network (SuperDARN) HF radars. *Prog. Earth Planet. Sci.* 6, 27. doi:10.1186/s40645-019-0270-5 — read in part; [P].

**Grey literature**

- [Collins2022p] Collins, K., Cerwin, S., Erickson, P. J., Joshi, D., Frissell, N., & Huba, J. (2022). Methods for estimation of ionospheric layer height characteristics from Doppler frequency and time of flight measurements on HF skywave signals. EGUsphere preprint. doi:10.5194/egusphere-2022-327 — read; [G, preprint].
- [Cerwin2020] Cerwin, S. (2020). WWV time tick arrival time study to investigate multiple modes during daily dawn and dusk transitions. HamSCI Workshop slides. doi:10.5281/zenodo.7191497 — read; [G].
- [FST2020] Franke, S., Somerville, B., & Taylor, J. (2020). The FT4 and FT8 communication protocols. *QEX*, July/August 2020, pp. 7 ff. https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf — read; [G].
- [WSJTX] *WSJT-X 2.6.1 User Guide*. https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.6.1.html — read (FST4 section); [G].
- Lombardi, M. (2022). Allan deviation analysis of WWV Doppler shift measurements recorded with the HamSCI Grape 1 receiver. HamSCI Workshop. https://hamsci.org/publications/2022/allan-deviation-analysis-of-wwv-doppler-shift-measurements-recorded-with-the — abstract; mainly ground-wave data, no skywave values extracted; [G].

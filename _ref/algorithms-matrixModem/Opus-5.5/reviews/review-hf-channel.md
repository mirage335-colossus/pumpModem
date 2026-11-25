# HF ionospheric channel, moderate through worst case: gap-focused literature review for the Matrix Modem

Prepared 2026-11-24. Scope: one-way, 2–100 mW, path loss over 150 dB, noncoherent MFSK-like tones (default 160 ms symbols, 6.25 Hz spacing) in a 450–2650 Hz SSB passband, with messages lasting minutes to days.

## 0. How to read this report

**Relation to existing project research.** These files already cover a great deal, and this report does not re-derive it:

- `_ref/algorithms-matrixModem/Sol-6.2/hf-channels.md`, cited here as **[Sol]**
- `_ref/highFidelity-triple_Robust_Modem/Fable-5.2/30–33`, cited as **[F30]–[F33]**
- `_ref/resilience-triple_Robust_Modem/Fable-5.2/robust-nonstationary-channel-research.md`, cited as **[RNS]**

Their coverage includes:

- F.1487 categories
- Watterson's validation
- the Balser–Smith fading census
- Leicester trough and polar-cap percentiles
- DAMSON and NRL auroral extremes
- equatorial spread-F (Kwajalein and Peru)
- Fernandes 2026 heavy-tailed Doppler residuals
- flare, PCA and storm outage tables (Fiori 2022, Tao 2020, Kitajima 2026)
- the May 2024 storm
- P.533/P.842 variability

Where one of those facts matters to a conclusion, the covered file is cited instead of being re-derived. New material gathered in this session is marked **NEW**.

**Access notes.**

- **[full]**: the full text was read, by WebFetch or by text extraction from the PDF.
- **[abstract]**: only the abstract or record page was read.
- **[snippet]**: only search-index excerpts were seen. Numbers carrying this mark are unverified.
- **[E]**: a value I derived, with the formula given.
- **Peer review:** "P" marks peer-reviewed sources; "G" marks grey literature (workshop posters, conference slides, amateur reports).

**Search limits.** The per-turn web-search budget ran out near the end of the work. The last unsearched lead was a cycle-25 UK/Canadian high-latitude Doppler-spread update, so recent high-latitude spread statistics may exist that this review did not find. Wiley/AGU, MDPI, DTIC and Springer pages often returned HTTP 403, which limited several items to abstract level.

**Estimator caution, repeated because it drives every number below.** The literature uses several different Doppler-spread measures:

- 2σ of a Gaussian (F.1487, Watterson)
- the narrowest band holding 80% of the power (Leicester, DAMSON); for a Gaussian this is 1.28 × 2σ
- FWHM or −10/−30 dB widths (equatorial and radar work)
- FST4W "spectral width", whose estimator the sources inspected here do not define

Coherence times below assume a Gaussian Doppler spectrum: the complex correlation falls to 0.5 at τ_c ≈ 0.375/(2σ) [E].

---

## 1. Coherence time, fade depth and fade duration (gap 1)

**Finding 1 — Measured HF fade-duration distributions remain scarce. The only modern measured numbers found are low-latitude fade rates and amateur SNR scatter.**

- **[F31] §F already records the gap.** It found no measured fade-duration distribution for mid-latitude HF skywave. The nearest measured distribution is for MF night skywave at 539 kHz: gamma-distributed lower-decile fades averaging about 3 min (P.1321). [F31] instead tabulates Rayleigh/Gaussian-derived crossing rates.
- **NEW:** Rao et al. (2002) monitored two broadcast links received at Tirupati, India:
  - Sri Lanka at 11.800 MHz, a geomagnetic-equator path
  - BBC Masirah at 15.310 MHz
  - The equatorial link showed "much higher fade rates" and generally larger fade ranges.
  - The authors conclude that fade allowances at these latitudes should exceed ITU-R recommendations.
  - Search excerpts (not verified in full text) give fade rates "rarely above ~26 per minute", and allowances of about 11–25 dB for 90% reliability at 10–18°N.
  - The fade range is defined as the 10–90% exceedance spread, and the fade rate as positive crossings per unit time.
  - This is the only post-2000 measured HF fade-rate result found. It is regional, and it is not a duration distribution.
  - Access: [abstract] (EarthRef), numbers [snippet].

**Finding 2 (NEW, important for long coherent integration) — On quiet one-hop mid-latitude paths, modern GPSDO-referenced FST4W measurements show ionospheric spreads of a few hundredths of a hertz. Equipment can contribute as much spread as the ionosphere.**

- **Source.** Griffiths, Elmore, Robinett, Rhymes and Watrous (2022, TAPR DCC) measured FST4W spectral widths.
  - The transmitters and receivers were GPSDO-locked, on 14 MHz in July 2022, under Kp < 3.
  - They separated equipment and path contributions with gamma-distribution deconvolution.
- **Equipment-only reference.** Over a 21 km line-of-sight path with GPSDOs at both ends, 543 spots on 19–23 July 2022 gave:
  - a fitted gamma distribution with shape 5.62 and scale 0.00132 Hz
  - a mode of about 0.006 Hz
- **One-hop ionospheric path.** The 1,558 km path from Fort Collins to Santa Rosa, using FST4W-300, gave:
  - a total-spread gamma with shape 1.878 and scale 0.0215 Hz
  - a path-only gamma with shape 1.151 and scale 0.0297 Hz
  - From the path-only fit: median 0.025 Hz, 90th percentile 0.076 Hz and 99th percentile 0.147 Hz [E, gamma quantiles].
- **Two-hop path.** The 5,291 km path to Maui had spectral widths about two orders of magnitude above the 21 km reference, which suggests tenths of a hertz [E]. Its distribution was right-skewed.
- **Trans-equatorial path.** The 12,254 km path to New Zealand had a more symmetric, near-Gaussian distribution, with its mode "well away from the lowest bin". No fit parameters are given.
- **Equipment can rival the ionosphere.** A standard GPS-aided KiwiSDR showed:
  - 1 s frequency errors with σ 0.046 Hz and mean −0.037 Hz at 10 MHz
  - a median FST4W-120 spectral width of 0.068 Hz over a 38 km path, comparable to the one-hop ionospheric median
  - a 0.667 Hz median spread with browser buffer abuf = 1.75 s, at which only 23% of transmissions decoded
- **Low-cost TCXO transceiver (QDX).** On receive it showed a −2.04 Hz mean error at 10 MHz with σ 0.008 Hz. After two FST4W-300 transmit cycles, its receive frequency had shifted by 0.75 Hz; it relaxed with a time constant of about 230 s and kept a 0.2 Hz residual.
- **Practical limits.** FST4W-900 (5.5 s symbols, 0.18 Hz tones) was judged usable only on one-hop paths, and FST4W-1800 only on non-ionospheric or lower-HF paths.
- **Caveats:**
  - The estimator is undefined, and the spectral width is integrated over the whole 110–300 s transmission, so it includes slow drift.
  - Only decoded spots were measured, so the high-spread tail is censored.
  - The data cover one season and quiet geomagnetic conditions, with a single transmitter.
- **If** the width were a Gaussian 2σ, the one-hop median and 90th percentile would imply τ_c ≈ 15 s and ≈ 5 s [E].
- Access: [full], G.

**Finding 3 (NEW) — The empirical decode rule for FST4/FST4W structurally matches the Matrix default.**

- **The rule.** Franke, Somerville and Taylor's FST4/FST4W guide says:
  - decoding requires the Doppler spread to be less than the tone spacing
  - sensitivity is best when the spread is at most 1/8 of the tone spacing
  - oscillator drift plus path Doppler shift must stay below the tone spacing over the whole sequence
- **Matching mode.** FST4-30 uses 0.140 s symbols with 7.14 Hz spacing and has a −24.2 dB threshold in 2500 Hz on AWGN, which is 9.8 dB-Hz [E]. Its structure is close to the Matrix default of 160 ms and 6.25 Hz.
- **Transfer to Matrix [E].** Applied by analogy, the rule suggests near-AWGN performance at spreads of 0.8 Hz or less, and steep loss as the spread approaches about 6 Hz.
- **Simulation (Watterson, fst4sim).**
  - WSPR (0.683 s symbols, 1.46 Hz tones) "will not decode whatever the SNR" at a 3 Hz spread.
  - At a 2 Hz spread, FST4W-120 decodes at −24 dB while WSPR needs −17 dB.
  - The reported SNR and spectral width become biased low as the spread grows, because only low-spread transmissions decode (Griffiths et al. 2022, §2.2.2).
- Access: [full] for both sources, G.

**Finding 4 — Amplitude statistics and K-factor: no new HF measurement found. Polarization structure is the most actionable new nuance.**

- **Covered.** Balser & Smith (1962) found, among 146 ten-minute single-mode records:
  - 50 Rayleigh
  - 20 with K ≥ 3 dB
  - an extreme of K ≈ 14 dB near the MUF
  - 52 "probably nonstationary"
  - all multi-hop records Rayleigh

  Boys (1968) argued that the apparent specular part is a slowly varying bundle of rays ([F31] §F). The CCIR summary says Rayleigh dominates over 3–7 min, log-normal over 30–60 min, with within-hour deciles of +5/−8 dB ([F30] §2).
- **NEW, modeling only.** A 2022 Russian paper (Izv. VUZov Radioelektronika) modelled single-beam shortwave links with IRI-2016. It found fading "Rician or generalized Rayleigh rather than Nakagami in 90% of cases", and that Nakagami assumptions can overstate reliability by up to 12% [snippet; page 403].
- **NEW, Witvliet (2022 invited Nordic HF paper; 2015 IEEE AP Magazine).**
  - O/X isolation at NVIS exceeds 25 dB (7 MHz, 105 km).
  - O–X recombination produces "polarization fading" even on a single hop.
  - During "happy hour", when only the X wave propagates around sunrise and after sunset, fading is reduced and the polarization is circular.
  - A dual-circular receive antenna is reported to reduce the fading margin by 8–11 dB compared with a single circular antenna.
  - Access: 2022 paper [full]; the 8–11 dB figure [snippet] (EuCAP paper not opened).
- **NEW, Modilim & Stubbs (2026, HamSCI).**
  - Receiving WWV (2.5–25 MHz) at two sites 32 km apart, they found fading events uncorrelated between sites. The ground-level fading pattern therefore has scales under 32 km.
  - Carrier-phase RMS at a 20 ms lag was 0.08–0.20 rad at 5 MHz (morning) and about 0.12 rad at 25 MHz (afternoon).
  - Access: [abstract], G.
- **NEW, SNR-reporting noise floor in amateur data (Richards 2021, HamSCI poster).**
  - Night-time ground-wave WSPR SNR is steady within ±1 dB, but there is a typical "~6 dB fuzz" by day from propagated noise.
  - Spots below about −25 dB are biased by censoring at the −31 dB decode floor.
  - About 10 spots (around 2 h) are needed for a stable mean.
  - Zander (2022, arXiv) found σ ≈ 3 dB in the SNR difference between two co-sited 200 mW WSPR beacons received simultaneously by the same reporters. That σ includes antenna-pattern and interference differences.
  - These figures bound how finely amateur data can resolve fading.
  - Access: [full], G.

**Finding 5 — Fade time scales a modem must interleave against.** These combine covered results with derived values.

| Time scale | Mechanism | Evidence |
|---|---|---|
| 0.006–0.4 s | Fast Rayleigh fades; −10 dB mean fade duration T = 0.094/σ_f s [E], where σ_f = half the 2σ spread | 0.19 s at 1 Hz (2σ); 6 ms at 30 Hz (2σ) ([F31] §F table) |
| 2–20 s | Fast fades at quiet one-hop spreads of 0.01–0.1 Hz (2σ) | 10 dB fades lasting 1.9–19 s and recurring every 20–200 s [E]; one-hop correlation time (0.5) mostly 10–20 s, tail to about 120 s (Balser & Smith, in [F31] §E); NVIS coherence 10–20 s (Maso 2020), versus a "most restrictive" 1.46 s (Male 2021) |
| 17 s to minutes | Mode beats and Faraday rotation | O–X beat periods of 17–74 s and E–F mode beats of 5.7 s to minutes (from Watterson's tables); daytime polarization rotation of 0.25 turn/min (Epstein 1969), giving a linear-antenna fade about every 2 min [E] ([F31] §F) |
| 10 s to minutes | Intermediate "channel-quality" variation not reproduced by Watterson simulators | Isode on-air logs show 10–20 dB SNR swings over about 10 s and blocky losses over minutes ([F33] §F; [RNS] §4.1) |
| 15–60 min | Slow log-normal or focusing fading; TIDs | Mode path loss modelled as an AR(1) process over 15-minute intervals (Rifkin & Bello 1994); focusing fades of 15–30 min (Davies); TID periods of 15 min to 3 h ([F31] §§F, D) |
| Hours | MUF closure, absorption fading, band openings | §5 below |

**Implication for interleaving [E].**

- Each −10 dB fade-and-recovery cycle in benign conditions spans about 20–200 s.
- Roughly ten independent fast-fading samples per codeword therefore need an interleaver span of at least about 3–30 min in quiet conditions. In moderate conditions (0.5–2 Hz) the span drops to tens of seconds.
- Averaging over slow fading needs spans of at least 1–3 h.
- Outages of hours to days need erasure coding across the whole message duration (§5, §9).

---

## 2. Doppler shift versus Doppler spread; drift rates (gap 2)

**Finding 6 — The magnitudes of common Doppler shift are now well bounded for mid-latitudes.** These are covered results with new anchors.

- **Covered ([F31] §D, [RNS] §4.1):**
  - quiet days within ±0.1–0.2 Hz (Harbin paths, Luo 2021)
  - TID peak-to-peak median 0.24–0.31 Hz, with periods of 24–40 min, present 52–91% of the time (Reznychenko 2020)
  - pre-sunrise ±1 Hz damped oscillation; sunrise spike about +0.5 Hz; daytime 0 to +0.25 Hz with Cauchy-distributed residuals (Fernandes 2026, 10 MHz, 2,590 km, 1 Hz cadence since March 2021)
  - eclipses 0.25–1 Hz over 2–4 h
  - moderate storm ±1.5 Hz (Luo 2021)
  - −4 Hz at sunrise on a four-hop path (Ads 2013)
- **NEW detail on Fernandes 2026.** The fit used 5-minute bins and the FITTER package over 10 candidate PDFs, chosen by least squared error. The paper reports no fitted parameters or numerical percentiles, so the Cauchy result cannot be used quantitatively as published. The M9.8 flare of 28 Nov 2023 produced about 10 dB attenuation for about 34 min with a +0.25 Hz shift. [full], P.
- **NEW, Chernogor et al. (2022).** During the 5–6 Jan 2019 partial eclipse, on 5.0–9.83 MHz oblique paths into Harbin, the Doppler spectrum broadened by up to ±1.5 Hz and the main-ray shift varied by up to ±0.5 Hz. [abstract], P.
- **Separate observables.** The broadening is a spread observable and the main-ray shift is a common-shift observable. Both are mid-latitude and event-driven.

**Finding 7 (NEW) — Sudden frequency deviations (SFDs) are the fastest routine drift source: up to 9 Hz, rising in about 1 minute.**

- **Reeve (2015, grey).** WWV at 15 and 20 MHz received at Anchorage over about 3,770 km [E], 11 Jun–31 Dec 2014 (near the cycle-24 maximum). In 2,621 h of daylight observation he found:
  - 64 SFDs, about one per 41 daylight hours [E], against 1,092 C/M/X flares reported in that period, i.e. about 6% of reported flares produced a detectable SFD [E]
  - 80% accompanied by X-ray flares
  - flare classes from B6.3 to X3.1
  - maximum deviations of 9 Hz at 15 MHz and 4 Hz at 20 MHz
  - start-to-maximum times of "generally 1 minute"
  - no clean relation between X-ray class and deviation
- **Derived rate.** This implies drift rates of the order of 0.15 Hz/s [E]. That matches the covered Chum et al. (2018) vertical-sounding profiles: rises of about 3 Hz in 15–40 s, falls of 4–5 Hz in about 20 s, i.e. 0.1–0.25 Hz/s, with peaks of 3.4–3.9 Hz (X1.6) and 7.8 Hz (M7.9) ([F31] §D).
- **Historical tail.** Davies & Baker (1966) report most SFDs under 1 Hz, with a most probable duration of about 4 min and an extreme of 50 Hz [F31].
- **Caveats for Reeve.** The receiver used a ±0.5 ppm reference (±7.5 Hz at 15 MHz, i.e. slow drift only) and recorded on a 13-minute chart display, with no statistical estimator.
- Access: [full], G.

**Finding 8 — Measured Doppler drift-rate distributions remain absent.** [RNS] and [F31] report no measured drift-rate statistic. This session found none either. The available rates are derived:

- TIDs: 0.2–7 mHz/s from 2πA/P
- sunrise: about 1–2.4 mHz/s
- storm excursions: 0.7–1.7 mHz/s
- flares and SFDs: 0.1–0.25 Hz/s for tens of seconds, measured from traces
- auroral case: about 0.1 Hz/s (8.7 Hz in 90 s; NRL single event, [F32])

**Implication for coherent integration [E].** A linear drift ḟ over a coherent window T produces a quadratic phase error of π·ḟ·T². Keeping that below about 1 rad requires T ≤ (π·ḟ)^−½:

| Drift rate | Maximum coherent window |
|---|---|
| 0.2 mHz/s | 40 s |
| 1 mHz/s | 18 s |
| 5 mHz/s | 8 s |
| 0.15 Hz/s (flare or SFD) | 1.5 s |

Coherent integration beyond about 10–20 s therefore needs explicit drift (chirp) hypotheses even in quiet TID conditions.

- OTH-radar practice gives a consistent order of magnitude: coherent dwell of "many seconds".
- A Florence PhD thesis notes signal "inconsistency" beyond about 23 s in particular European conditions. Access: [snippet], G.

**Finding 9 — Heavy tails mean percentiles, not σ.** The covered Fernandes (2026) result applies: daytime Cauchy (no finite variance); night-time log-normal or exponential-power. Peak-frequency estimates at a 1 Hz cadence probably include jumps between simultaneous modes ([F31] §H).

For acquisition, a frequency-search span set at "±kσ" of a Gaussian will under-cover the shift. Use empirical percentiles plus an explicit mode-jump hypothesis.

---

## 3. Non-WSSUS and nonstationary modeling and validation (gap 3)

**Finding 10 — The standards have not moved. No 2015–2026 measured-versus-Watterson modem comparison was found beyond the covered items.**

- **ITU-R F.1487-0 (2000)** is still the test standard [Sol][F33].
- **NEW: ITU-R F.1821-1 (02/2026)**, on characteristics of advanced digital HF systems, was text-searched. It references F.339 and P.533 and contains no simulator channel parameters. [full]
- **ITU-R F.1289.** I could not verify its title or content; neither the ITU record page nor searches returned it. Do not cite it as the wideband Vogler–Hoffmeyer standard without checking. The wideband model is documented in NTIA reports 88-240, 90-255 and 92-284 [F33].
- **Covered on-air evidence** ([F33] §F):
  - Isode found that simulators lack the blocky, minutes-scale losses seen on air.
  - Angling (2001) found that availability estimates from Watterson performance surfaces "do not perform well in the presence of Doppler spread" compared with replay.
- **NEW, kahf-modem (open source).** It explicitly labels its F.1487 Watterson benchmarks as "not an on-air HF claim" [snippet], which illustrates the norm.
- **WSJT thresholds are simulation-derived.** The FST4/FST4W published thresholds are AWGN simulations; fst4sim's fading uses an F.1487 Watterson channel (Griffiths et al. 2022). Amateur decoder "thresholds" are therefore simulation-calibrated too.

**Finding 11 — New physics- and geometry-driven models are appearing, but none is validated as a statistical communications channel at modem time scales.**

- **NEW, Lv et al. (2022, Remote Sensing 14:4133).**
  - Method: multi-source assimilation (background electron density), then 3-D ray tracing, then a Gaussian scattering function.
  - Comparison with two oblique-sounding paths: average deviations of about 17.3 km in group-path broadening and 0.21 Hz in Doppler shift.
  - Access: [abstract or snippet], P.
- **Covered** ([F33] completion): Liu et al. 2026 (Physical Communication) on ray-traced nonstationary cluster simulation; IEEE TAP 74(4) 2026 high-latitude ray-driven model. Both are abstract-level.
- **NEW, metadata only.** SSC Pacific TR 3075 (2017; Allen, Daly, Meloling et al.), "Mid-Latitude Mobile Wideband HF-NVIS Channel Analysis: Part 1" (DTIC AD1039223), builds an emulator-ready statistical NVIS channel model from wideband soundings and 3-D ray tracing. The PDF returned 403, so no parameters could be extracted.

**Finding 12 — Stationarity time scales.**

- **Covered:**
  - Watterson's "10 min" is the length of selected samples, not a measured stationarity interval.
  - Balser & Smith: "a few minutes".
  - Jones et al. (1986): spectral width decorrelates in about 3 min on a quiet day.
  - ELOISE: within-dwell standard deviation of 0.02–0.03 Hz in Doppler and 0.5–1 dB in power per 6.4 s.
  - No 2015–2026 measurement at minute scale ([F31] §H; [RNS] §4.1).
- **NEW, qualitative only.** Griffiths et al. (2022), Figs. 9–12, show FST4W spectral-width time series with:
  - apparently wave-like variations of low scatter
  - isolated events lasting about 30 min
  - spread rising steadily as the band closes
  - an unexplained high spread on a 124 km ground or near-ground path, exceeding that of a 1,535 km skywave path

These features fit spread being nonstationary on scales of tens of minutes.

---

## 4. Equatorial spread-F and auroral/polar spreads: occurrence (gap 4)

**Finding 13 (NEW) — Low-latitude spread-F occurrence is strongly local-time and season dependent, and its solar-cycle dependence is station and type dependent.**

- **Wang et al. (2010), Hainan DPS-4.** The station is at 19.5°N, 109.1°E, dip latitude 9.5°N; data run Mar 2002–Feb 2008. "Relative occurrence" means the fraction of ionograms in a 15-min slot showing that type.
  - **High solar activity (2002), equinox:** strong range spread-F (SSF) rises from about 20:00 LT to about 68% at 22:00 LT and disappears after 02:00 LT. Mixed spread-F (MSF) peaks at 24% at 03:15 LT.
  - **Low solar activity (2005–2007):** SSF and RSF stay under about 7% all night at equinox. MSF peaks at about 20% (00:45 LT, equinox) and about 45–48% (01:15 LT, summer).
  - **Winter:** below 5% except MSF at about 15% (03:45 LT, low-activity years).
  - **Trends:** SSF increases with sunspot number at equinox and in summer (fitted slope about 0.22% per SSN unit). Frequency spread-F is independent of SSN. All types correlate negatively with Kp above 1.
  - Access: [full], P.
- **Bhaneja et al. (2024).** Digisondes at Jicamarca (2001–2016), Ascension (2000–2014) and Kwajalein (2004–2012) give:
  - peak seasons Nov–Mar, Sep–Dec and Mar–Aug respectively
  - "significant variation is not observed over the solar cycle" at Jicamarca and Kwajalein, with spread-F persisting through the 2007–2010 deep minimum
  - Ascension higher at solar maximum
  - onset mainly after sunset, except at Ascension (throughout the night)
  - Numeric occurrence percentages appear only in figures.
  - Access: [full via fetch], P.
- **Australasian 1975–2013 ionograms** (Radio Science 2023, ADS bibcode 2023RaSc...5807495E): at mid-latitude stations, frequency- and range-spread rates were *negatively* correlated with solar activity. Access: [snippet]. The DOI is inferred as 10.1029/2022RS007495 and not opened.
- **Covered:**
  - mid-latitude spread-F on up to about 88% of night-time ionograms in the peak month at some East Asian stations, but under 15% at others (Wang et al. 2019, EPS 71:47, in [RNS])
  - Kwajalein at solar maximum: Doppler spread above 6 Hz at −10 dB and above 15 Hz at −30 dB on 50% of evenings; coherence bandwidth below 1.5 kHz on 75% of evenings (Fitzgerald et al. 1999, [F32] §G)
  - trans-equatorial plumes to about ±60 Hz, growing within minutes (Miller 2017, [F32] §H)
  - 1.5 GHz scintillation above 2 dB 70% of the time at high SSN versus 30% at low (Basu 1988)
- **Implication [E].** At HF the bottomside irregularities matter, and their occurrence does not necessarily fall with solar minimum the way L-band scintillation does. For low-latitude evening paths (about 20–02 LT, equinox), "severe" (more than 6 Hz) should be treated as roughly a coin-flip per evening at solar maximum, based on Kwajalein and Hainan, and as station dependent at solar minimum.

**Finding 14 — Auroral and polar cap: no new 2020–2026 percentile distribution of communications Doppler spread was found** (search budget exhausted). The covered numbers remain the best:

- polar-cap 95th percentiles to 13 Hz (80% width) (Stocker et al. 2013)
- DAMSON Harstad–Kiruna 95th percentiles of 49.5–73 Hz (conditional on SNR −5 to +5 dB in 3 kHz)
- 22.2 Hz versus 3.6 Hz at the 5% level on the same Svalbard–Kiruna path a decade apart (Warrington, Stocker & Siddle 2006)
- extremes clustered on a few days
- F.1487 5% exceedance of 25 Hz and 5 ms ([F32] §§A, J; [RNS] §4.7)

How often a high-latitude path is "severe" depends more on where the path sits relative to the trough and oval, and on season, local time and solar cycle, than on Kp ([F32] §F).

---

## 5. Space-weather outages and their durations (gap 5)

**Finding 15 (NEW) — Short-wave fadeout (SWF) phase durations from recent radar statistics.**

- **Liu et al. (2022, ApJ 933:45).** SuperDARN Zhongshan (Antarctica), about 10.4 MHz, 12 SWF events in 2010–2019 (3 M-class and 9 X-class flares), selected for Kp ≤ 3:
  - mean onset 6 min 54 s
  - mean blackout 20 min 24 s
  - mean recovery 39 min 36 s
  - long-onset events: onset 6–16 min, recovery 29–138 min
- **Same paper, citing Dieminger et al. for Boulder 1980–1987 SWFs:**
  - mean duration about 23 min
  - 58.9% under 14 min; 21.4% 15–29 min; 4.3% 30–44 min; 3% over 90 min
  - the remaining 12.4% presumably 45–90 min [E]
- **Chakraborty et al. (2018)**, northern ground scatter (covered abstract): onset about 100 s, blackout about 10 min, recovery about 42 min.
- Access: Liu 2022 [full via fetch], P.

**Finding 16 (NEW) — SWF depth and the noise caveat.**

- **Witvliet (2022).** An X1.6 flare attenuated all signals on a 7 MHz NVIS link (Netherlands) by more than 40 dB. The background noise also dropped 12 dB, because most rural noise arrives via the ionosphere. Onset was sudden and recovery gradual. [full]
- **Fernandes (2026).** An M9.8 flare gave about 10 dB attenuation for about 34 min on a 10 MHz, 2,590 km path [full].
- **Covered.** Modelled 20–40 dB at 6.4 MHz for X1.3, exceeding D-RAP (Siskind 2022; Levine 2019) ([F30] §4).
- **Modem implication.** At a noise-limited rural receiver, the SNR loss in an SWF can be markedly smaller than the signal loss. It can also be larger at a site dominated by local man-made noise.

**Finding 17 — Outage-rate and duration anchors (covered, with cycle-25 context).**

- **Fiori et al. (2022), per 11-year cycle:**

  | Event class | Events per cycle | Mean duration |
  |---|---|---|
  | Fadeouts, moderate / severe | 123 / 5 | 68 / 132 min |
  | PCA, moderate / severe | 24 / 13 | about 8 h / 1.6 days |
  | Auroral absorption, moderate / severe | 40 / 3 | 5.1 h / 12 h |
  | Post-storm MUF depression, moderate / severe | 200 / 56 | 5.5 h / 8.5 h |

- PCA can exceed 100 dB and last up to about 4 days.
- Tao et al. (2020) observed maxima at Kokubunji (1981–2016) of 3 h 15 min for complete echo blackout and 8 h 15 min for f_min elevated by 2.5 MHz or more.
- For the May 2024 storm, foF2 over Italy fell about 70% for two days (Spogli et al. 2024).
- During September 2017, effective bands stayed suppressed for 12–15 days (Frissell et al. 2019).
- Kitajima et al. (2026): Japanese echo-blackout lower bounds of 35–105 min for the X5.8 flare of 11 May 2024.
- All of the above are in [F30] §4.
- **NEW grey context.** SpaceWeatherLive counted 54 X-class flares in calendar 2024, the most since its 1996 record began. A secondary source states that 54 X flares in the first 4.5 years of cycle 25 exceed cycle 24's 49 over its whole 11 years. Access: [snippet], G.
- **Rate check [E].** Under cycle-25-maximum conditions, X-flare SWFs on the sunlit hemisphere occur at roughly weekly rates. That is far above Fiori's cycle-average 123 moderate fadeouts per 11 years, about one per month.
- **NEW, May 2024 storm, abstract level.** Potter & Frissell (2026) and Potter et al. (2026, HamSCI) report elevated Doppler shifts in the growth phase and "multi-hour blackouts throughout the gradual recovery phase", with no numbers in the abstracts. Grandin et al. (2024, Geosci. Commun. 7:297) collected citizen reports, including anomalous HF propagation. Access: [abstract].

**Finding 18 (NEW) — Sporadic-E openings from WSPR at upper HF.**

- **Liu & Dai (2026, Front. Astron. Space Sci. 13:1946062).** Starting from about 830 million 2021 WSPR spots, they kept about 1.49 million at 28 and 50 MHz, at 750–2,500 km, with SNR above −15 dB.
  - 27,253 Es detection windows (30 min each)
  - 69.7% of detections in May–Aug; summer-to-winter ratio 3.09 (about 5.6 in the Southern Hemisphere)
  - validated against Juliusruh at foEs ≥ 3 MHz: probability of detection 57%, false-alarm rate 51%
  - Opening durations were *assumed*, not measured.
- **Interpretation.** Es can both open and blanket paths. Durations of individual openings remain unquantified in any source found.
- Access: [full via fetch], P.

---

## 6. Low-power empirical evidence (WSPR, FST4W, RBN) (gap 6)

**Finding 19 — No peer-reviewed study was found that reports, for mW-level transmitters, the fraction of hours a path of given distance and band decodes at a given SNR.** That is a genuine gap. What exists:

- **Decoder floors.** WSPR is spec'd near −28 dB in 2500 Hz (about 6 dB-Hz [E]) and decodes to about −31 dB (3 dB-Hz). FST4W-1800 reaches about −44.8 dB (−10.8 dB-Hz) on AWGN, but only with spreads near or below 0.01 Hz (LF/MF, or GPSDO ground wave) (Franke et al. 2020; Griffiths et al. 2022).
- **Censoring and noise.**
  - SNR is linear from about −25 to +15 dB. A 19.5 dB power step at 0.1/1/10 W produced an 18.6 dB mean SNR change.
  - Means below about −23 dB are biased high because spots below the floor are missed.
  - Reporter equipment changes confound year-to-year comparisons.
  - Source: Richards 2021, [full], G.
- **Path-reliability proxy.** The share of active reporters in a grid square that heard the beacon in each hour (Griffiths & Robinett 2021, 5 W shipboard beacon, Dec 2020–Jan 2021) reproduced VOACAP's predicted MUF dip, but showed 14 MHz open for longer than predicted.
  - SNR heatmaps showed abrupt steps from no decodes to about 10 dB above the WSPR threshold as bands opened and closed.
  - Reporter counts were very uneven: 447 at 40 m and 473 at 20 m, against 11 at 160 m and 14 at 10 m.
  - Access: [full], G.
- **Diurnal gaps.** On the 1,558 km, 14 MHz FST4W path in July 2022, reception gaps clustered at 0600–1200 UTC, about 6 h/day of MUF-limited outage, with day-to-day variation. On the 5,291 km path the gaps were scattered through the day (Griffiths et al. 2022). [full], G.
- **Covered peer-reviewed WSPR work:**
  - Lo et al. (2022, Atmosphere 13:1340): 7 MHz grey-line clustering, with a noise explanation for missing sunset reports.
  - Chartier et al. (2023, Space Weather): WSPR ray-closure validation, 55.5%, which is not a success probability.
  - Vanhamel et al. (2022): 160 m polarization differences of 3–13 dB.
  - Mahmoudian et al. (2026): a November 2025 storm both suppressed and opened paths (10 MHz report counts going 29→1 at one time and 3→42 at another).
  - Sources: [F30], [Sol].
- **Grey anecdotes.** A 10 mW WSPR "lowest possible power" analysis (PA1B, a29.veron.nl) exists but is not statistical. Access: [snippet].
- **Conversion for a 2–100 mW Matrix transmitter [E].** Relative to WSPR's usual 0.2–5 W, the deficit is 3–34 dB, depending on the pairing (2 mW vs 5 W = −34 dB; 100 mW vs 200 mW = −3 dB). A WSPR spot at SNR S from a P-watt beacon implies about S + 10·log10(P_matrix/P) for the same path, antennas and noise. Each such spot is a single 2-minute, detection-conditioned sample.

---

## 7. Magneto-ionic splitting, frequency selectivity across 2.2 kHz, and delay extremes (gap 7)

**Finding 20 — O/X structure.**

- Witvliet's NVIS work: O/X isolation above 25 dB; X-only "happy hour" intervals near sunrise and after sunset with reduced fading; polarization fading from O–X recombination on a single hop (NEW, Finding 4).
- Tartón et al. (2021, Sensors 21:2210): the X mode had about 15% more availability and similar delay spread (about 0.3 ms), with up to 4 dB polarization-diversity gain [Sol].
- Epstein (1969): rotation of 0.25 turn/min by day, near zero at night, with large fluctuations; frequency rotation of 0.25–1 turn/MHz [F31].
- **Implication [E].** For a single linearly polarized antenna, slow polarization fading with periods of minutes is a structural feature of mid-latitude daytime links. It is not captured by an F.1487 two-path Rayleigh model unless O/X components are modelled.

**Finding 21 — Frequency selectivity across the 450–2650 Hz band [E, two-equal-ray heuristic |cos(π·Δf·τ)|].**

- **Adjacent tones.** Tones 6.25 Hz apart have correlation at least 0.99 for differential delays up to 7 ms. Adjacent Matrix tones therefore always fade together.
- **Correlation 0.5 spacing.** Correlation falls to 0.5 at Δf = 1/(3τ): 667 Hz at τ = 0.5 ms, 333 Hz at 1 ms, 167 Hz at 2 ms, 48 Hz at 7 ms.
- **Independent cells in the band.** The 2.2 kHz band holds roughly 2200·3τ "independent" frequency cells: about 0.7 at 0.1 ms, about 3 at 0.5 ms, about 7 at 1 ms, about 20 at 3 ms.
- **Measured coherence bandwidths:**

  | Path | Coherence bandwidth | Source |
  |---|---|---|
  | NVIS, mean | about 3 kHz | Male 2021 |
  | NVIS, worst | 346–369 Hz | Male 2021 |
  | Antarctic NVIS | 182–364 Hz | Maso 2020 |
  | 4,470 km transauroral | average 4.3 kHz; at least 1 kHz 90% of the time | Auterman 1962 |
  | Equatorial spread-F | below 1.5 kHz on 75% of evenings; below 1 kHz in developed spread-F | Fitzgerald 1999/1993 |

  Sources: [F31] §G, [RNS] §4.1, [F32] §G.
- **Consequence.** Single-mode quiet channels, with intra-mode spread of 20–100 µs, fade flat across the entire passband. Frequency hopping inside 2.2 kHz then provides no diversity against the dominant fades exactly when the channel is otherwise benign. Hopping diversity appears only with multi-mode delays of 0.5 ms or more. Time interleaving, not in-band hopping, must carry the diversity burden in benign conditions.

**Finding 22 — Delay extremes up to the round-the-world (RTW) scale.**

- **Covered maxima of differential delay** ([F31], [F32]):
  - mid-latitude 95th percentiles mostly 1–3.6 ms; 5.8 ms in one processing
  - NVIS 3.8 ms measured, 5.5 ms with ground wave, about 10 ms in one secondary-reported case
  - DAMSON Harstad–Kiruna 7.4–11.4 ms (95th percentile, instrument-limited)
  - off-great-circle trough-wall modes 2–7 ms later than the great-circle mode
- **NEW, RTW echoes.** These arrive about 135–138 ms after the direct signal (DTIC review of ionospheric ducting; Nature letters reporting about 138–140 ms); a uniform-path estimate gives 133.5 ms for 40,030 km [E].
  - That is about 0.86 of a 160 ms Matrix symbol.
  - Reported attenuation is "mainly determined by the low-latitude ionosphere in African and Asian–Pacific sectors" (Russian chirp-sounder network, 1997–1999).
  - **No statistics were found** on RTW relative level or occurrence. Access: [snippet] only.
  - **Design note [E].** An RTW echo lands almost a full symbol late. If it is strong enough to matter, it looks like inter-symbol interference from the previous symbol's tone, not intra-symbol multipath. A guard or overlap design tuned to 0–11 ms would not address it.
- **NEW, above-MUF scatter.** On a 7 MHz NVIS link at night above the MUF, the signal was typically 45–50 dB below the daytime NVIS level, yet still 20 dB SNR. It arrived diffusely at low elevation from all azimuths, attributed to ground scatter at 500–2,000 km (Witvliet 2022). A link that is "closed" in MUF terms may thus retain a weak, diffuse, probably high-spread scatter channel, which is relevant to a very-high-processing-gain receiver. Its Doppler spread and delay were not reported.

---

## 8. Contradictions and nuance versus older models (gap 8)

1. **Quiet mid-latitude one-hop channels are more benign than F.1487 "quiet" (0.1 Hz 2σ) in Doppler spread; disturbed high-latitude channels are harsher than F.1487 "disturbed" (30 Hz).**
   - Benign side: FST4W path-only median about 0.025 Hz (90th percentile 0.076 Hz) on 1,558 km at 14 MHz (Griffiths 2022). Watterson's quiet composites were 0.07–0.14 Hz and per-mode 0.006–0.18 Hz. Note the estimator mismatch.
   - Harsh side: DAMSON 95th percentiles of 49.5–73 Hz (80% width) [F32].
   - So a fixed preset ladder under-spans both ends.
2. **Equipment can rival the ionosphere.** On one-hop HF, oscillator and receiver processing spread (KiwiSDR median 0.068 Hz; browser buffering up to 0.67 Hz; TCXO thermal shift 0.75 Hz) can equal or exceed the ionospheric spread (Griffiths 2022). Older models attribute all spread to the path.
3. **Heavy-tailed common Doppler.** Daytime residuals are Cauchy-like (Fernandes 2026), so Gaussian ±kσ search spans are not conservative. Mode switching probably feeds the tails.
4. **Solar-cycle dependence is not universal.**
   - Bottomside spread-F at Jicamarca and Kwajalein showed no significant solar-cycle variation (Bhaneja 2024).
   - Mid-latitude Australasian spread-F is *anti*-correlated with solar activity (Radio Science 2023, snippet).
   - Hainan strong spread-F rises with SSN (Wang 2010).
   - L-band scintillation varies 70% versus 30% (Basu 1988).
   - Leicester high-latitude spread-versus-SSN relations were non-monotonic (Stocker 2013).
   - The same Svalbard–Kiruna path gave 22.2 Hz versus 3.6 Hz at 5% a decade apart (Warrington 2006).
5. **Absorption events do not translate one-for-one into SNR loss.** In Witvliet's X1.6 SWF the noise also dropped 12 dB. During the Gannon storm, WsprDaemon's FCC filing reports lower propagated-in noise at sites not limited by local noise [snippet]. Storms can also *open* paths (Mahmoudian 2026).
6. **Fade scales span seven orders of magnitude, and the standards disagree on the short-term law.** F.339 assumes log-normal; P.842 assumes Rayleigh deciles. Balser & Smith found about a third of records non-stationary. Low-latitude fade allowances exceed ITU values (Rao 2002). F.520-2's simulator fade durations of 0.05–1.5 s are far shorter than quiet-path fades of 2–20 s and MF fades of minutes ([RNS] §4.7).
7. **Simulator-calibrated thresholds.** Every amateur and military weak-signal threshold found (WSPR, FST4W, MIL-STD 75 bit/s) is calibrated on AWGN or Watterson channels. On-air evidence (Isode; Angling replay) shows minutes-scale blocky losses and Doppler-dependent availability errors that Watterson does not produce.
8. **Spatial decorrelation is small.** Fading was uncorrelated at 32 km separation (Modilim & Stubbs 2026), so per-site fade statistics are not shared regionally. Ground-level fading patterns are finer than many network-based "path availability" maps imply.
9. **"Closed" bands are not zero-signal.** Above-MUF NVIS scatter 45–50 dB down still gave 20 dB SNR at a quiet site (Witvliet 2022). Outage depth depends strongly on receiver processing gain and noise environment, so binary open/closed availability products (VOACAP, WSPR spot counts) understate what a very-high-gain receiver might see. The spread and delay of such scatter remain unmeasured in the sources found.

---

## 9. Implications for a weak-signal tone modem: moderate to worst-case categories

The values below combine measured statistics with derived quantities [E]. Each states its conditions. "Percentile" means the source's percentile of *detected* signals unless noted; low-SNR censoring biases these toward benign conditions ([RNS] §4.1). The worst-case column collects separately observed extremes, not one joint observation.

| Parameter | A. Benign (median quiet mid-latitude one-hop; Kp < 3) | B. Moderate (about 90th–95th percentile mid-latitude; low-latitude quiet or moderate; typical NVIS; sunrise, eclipse, moderate storm) | C. Severe (95th percentile polar cap or trough on winter nights at solar maximum; equatorial spread-F evenings; F.1487 high-latitude moderate) | D. Extreme stress (reported extremes, not percentiles) |
|---|---|---|---|---|
| Common Doppler shift (ionospheric only) | ±0.1–0.3 Hz (TID peak-to-peak median 0.24–0.31 Hz; daytime 0–0.25 Hz) | ±0.5–1.5 Hz (pre-sunrise ±1; eclipse 1; storm ±1.5) | ±2–5 Hz (MITRE high-latitude 90th percentile about 3 Hz; NRL −2.3 to −4.6; sunrise on 4 hops −4; X-flare 3.4–3.9) | 7.8–9 Hz SFD peaks (M7.9 near vertical; 15 MHz at Anchorage); 13–25 Hz auroral event; 20–30 Hz implied by equatorial plume phase rates [E]; 50 Hz historical SFD extreme |
| Equipment offset to add (not ionospheric) | GPSDO: about 0 | GPS-aided SDR: ±0.05–0.1 Hz | TCXO: about ±2 Hz at 10 MHz; thermal step 0.75 Hz | ±0.5 ppm reference: ±7.5 Hz at 15 MHz |
| Drift rate | 0.2–1 mHz/s [E] | 1–5 mHz/s [E] | about 0.1 Hz/s (auroral single event) | 0.1–0.25 Hz/s for tens of seconds (flares and SFDs) |
| Doppler spread | 0.01–0.1 Hz (FST4W one-hop median 0.025, 90th percentile 0.076, estimator unknown; Watterson 2σ 0.006–0.18) | 0.5–2.5 Hz (F.1487 0.5/1 Hz 2σ; UK trough 95th percentile 1.5–2.5 Hz, 80% width) | 5–15 Hz (polar-cap 95th percentile 13 Hz; trough winter night 10–13 Hz, 80% width; equatorial spread-F above 6 Hz at −10 dB on 50% of evenings; F.1487 10 Hz) | 25–73 Hz (F.1487 30 Hz 2σ; DAMSON 49.5–73 Hz 95th percentile, 80% width; spread-F plumes about ±60 Hz) |
| Coherence time, ρ = 0.5 [E] | 3.75–40 s | 0.15–0.75 s | 25–75 ms | 5–15 ms |
| Mean −10 dB fade duration [E] | 1.9–19 s, recurring every 20–200 s | 75–375 ms | 13–38 ms | 3–6 ms |
| Differential delay | ≤ 0.5–1 ms (often a single mode; UK composite medians 0.1–0.2 ms) | 1–3 ms (UK 95th percentiles mostly 1–3 ms; NVIS 1.9–2.9 ms) | 3–7 ms (trough 95th percentile to 5.8 ms; DAMSON 5% 3.5–5.7 ms; F.1487 NVIS-disturbed 7 ms) | 7–11.4 ms (DAMSON, instrument-limited); RTW echo at about 135–138 ms (level unknown) |
| Coherence bandwidth versus 2.2 kHz band | ≥ 2.2 kHz: whole band fades together | 0.3–1 kHz: about 2–7 independent cells | < 1.5 kHz (equatorial spread-F); 150–350 Hz | 90–150 Hz |
| Stationarity of statistics | Minutes to about 10 min (selected); spectral width decorrelates in about 3 min | A few minutes; steps at sunrise and sunset | Under 1 min at mode onset or loss and in plumes (spread doubling within one record) | — |
| Slow, outage-scale variation | Log-normal or focusing 15–60 min; daily MUF gap of hours (about 6 h/day on 1,558 km, 14 MHz, July 2022) | Band openings and closings appear as abrupt steps of about 10 dB | SWF: blackout about 10–20 min plus recovery about 40 min (mean); 3% of SWFs exceed 90 min | PCA mean 8 h to 1.6 days, up to about 4 days, over 100 dB; storm MUF depression 5.5–8.5 h mean; band suppression for days to 2 weeks (2017); foF2 −70% for 2 days (May 2024) |

**Concrete design consequences [E, inferences from the table].**

1. **Tone spacing and symbol length must be selectable by channel category.**
   - The FST4 rule (spread below the spacing; best at or below 1/8) puts the 160 ms / 6.25 Hz default comfortably inside categories A and lower B, i.e. spreads at or below about 0.8 Hz.
   - It becomes marginal in upper B and fails in C, where coherence time (25–75 ms) is shorter than the 160 ms symbol.
   - **Category C** would need spacing at least as large as the spread: about 15–30 Hz, i.e. symbols of about 33–67 ms or less.
   - **Category D** would need about 70–100 Hz or more, i.e. symbols of about 10–14 ms. That collides with 7–11 ms delay spread, so category D cannot be served by single-tone-per-symbol energy detection. It would need noncoherent combining across multiple tones or frequencies, or it must be declared out of scope.
   - **Category A** supports much longer symbols than 160 ms: FST4W-300's 1.8 s symbols at 0.56 Hz spacing decode routinely on one-hop 14 MHz paths with GPSDO. The energy-per-bit advantage of longer integration is available only there.
2. **Frequency search span** (ionospheric plus oscillator; use percentiles, not Gaussian σ):
   - **±1.5 Hz** covers category A–B ionospheric shifts.
   - **±5 Hz** covers most flares and high-latitude mean shifts.
   - **±10 Hz** covers the 15 MHz SFD maximum and the NRL auroral excursions.
   - **±30 Hz** is needed only for category-D shifts.
   - Add the oscillator budget separately. Unknown-equipment TCXO-class transmitters contribute ±2 Hz or more at 10 MHz, which already exceeds category-B ionospheric shifts.
   - Allow for mode jumps: two simultaneous components a few tenths of a hertz to ±8 Hz apart (NRL).
3. **Drift hypotheses for coherent or semi-coherent integration.** Coherent windows longer than about 8–18 s need chirp hypotheses at 1–5 mHz/s even in categories A–B. Flare and SFD episodes (0.1–0.25 Hz/s) limit coherent windows to about 1.5 s for minutes at a time, a few times per week near solar maximum on the sunlit side.
4. **Interleaving and erasure coding.**
   - Fast fades: an interleaving span of at least about 3–30 min in category A (10 or more independent fades), and tens of seconds in categories B–C.
   - Slow fading: at least 1–3 h.
   - Outages: for multi-hour messages, spread the code over at least one diurnal cycle. Expect contiguous erasures of about 6 h per day from MUF closure on fixed-frequency paths, and 10–140 min per SWF.
   - Multi-day PCA or storm events cannot be bridged by any within-day interleaver. A one-way design must either tolerate days of total erasure (messages spanning days) or accept those as failures.
   - In-band frequency hopping across 2.2 kHz adds little diversity in category A, where the band fades flat. It helps only in B–D, where delays reach 0.5 ms or more.
5. **Validation channels.**
   - Keep F.1487 as a screen, but add the following, since on-air evidence shows Watterson misses blocky, minutes-scale behaviour:
     - O/X slow polarization fading (minutes)
     - heavy-tailed common Doppler with mode jumps
     - SFD chirps of 0.15 Hz/s
     - a log-normal AR(1) slow-fading envelope on a 15-min scale
     - diurnal on/off availability with abrupt ±10 dB steps
     - an optional weak RTW tap at about 137 ms (level unknown: parameterize it and label it a stress test)
   - Include equipment-spread budgets (GPSDO versus GPS-aided versus TCXO) as a separate axis.

---

## 10. Searched for and not found (or not accessible)

- Measured HF fade-duration distributions (mid-latitude, NVIS or long-haul), and measured Rician K distributions after 1970. Only Rao 2002 low-latitude fade rates (snippet-level numbers) and older work exist.
- Any measured distribution of Doppler drift rate (Hz/s). Only single-event traces and derived values exist.
- Distributions of SFD magnitude across many flares. Only Reeve's 64-event amateur study, Donnelly's 1974 atlas and Davies & Baker exist.
- A peer-reviewed WSPR/FST4W study of hourly decode fraction versus distance, band and power at the mW level.
- High-latitude Doppler-spread percentile distributions from 2020–2026; the search budget ran out before this lead could be checked.
- RTW echo relative level and occurrence statistics.
- ITU-R F.1289 content, unverified.
- Full texts blocked (HTTP 403):
  - Rao 2002 (Wiley)
  - Vilella 2008 and Ads 2013 Antarctica–Spain fading statistics (recercat)
  - SSC Pacific TR 3075 (DTIC)
  - the Russian 2022 Rician/Nakagami paper (DOAJ)
  - the Hainan/Sanya 120°E chain paper (Springer, EPS 2018)

---

## 11. References

### New this session

| Source | Access |
|---|---|
| Griffiths, G., Elmore, G., Robinett, R., Rhymes, L., Watrous, J. (2022). FST4W on the HF Bands: Why – What to expect – Equipment – Results. *ARRL/TAPR Digital Communications Conference 2022*, pp. 12–22. https://web.tapr.org/tech_docs/DCCpapers/2022%2002.pdf | full text; G |
| Franke, S., Somerville, B., Taylor, J. (2020). Quick-Start Guide to FST4 and FST4W (WSJT-X 2.3.0). https://wsjt.sourceforge.io/FST4_Quick_Start.pdf | full text; G |
| Griffiths, G., Robinett, R. (2021). Visualizing propagation to mid-latitudes from a shipboard WSPR transmitter on a passage from 27°N to 70°S using the WsprDaemon database. *HamSCI Workshop 2021*, poster. https://hamsci.org/sites/default/files/publications/2021_HamSCI/20210320_1700z-Gwyn_Griffiths_G3ZIL_Rob_Robinett_AI6VN.pdf | full text; G |
| Richards, D. (2021). WSPR at Midlatitudes from KN4NBI: A Year of Data at Solar Minimum. *HamSCI Workshop 2021*, poster. https://hamsci.org/sites/default/files/publications/2021_HamSCI/20210320_1700z-Douglas_Richards_KN4NBI.pdf | full text; G |
| Zander, J. (2022). Simple HF antenna efficiency comparisons using the WSPR system. arXiv:2209.08989 | full text; G (preprint) |
| Liu, Y., Dai, L. (2026). Detection of the sporadic E layer using crowdsourced WSPR signal reports during the ascending phase of cycle 25. *Front. Astron. Space Sci.* 13:1946062. doi:10.3389/fspas.2026.1946062 | full text (via fetch); P |
| Wang, G. J., Shi, J. K., Wang, X., Shang, S. P., Zherebtsov, G., Pirog, O. M. (2010). The statistical properties of spread F observed at Hainan station during the declining period of the 23rd solar cycle. *Ann. Geophys.* 28, 1263–1271. doi:10.5194/angeo-28-1263-2010 | full text; P |
| Bhaneja, P., Klenzing, J., Pacheco, E. E., Earle, G. D., Bullett, T. W. (2024). Statistical analysis of low latitude spread F at the American, Atlantic, and Pacific sectors using digisonde observations. *Front. Astron. Space Sci.* 11:1421733. doi:10.3389/fspas.2024.1421733 | full text (via fetch; numbers only in figures); P |
| (Authors not retrieved) (2023). Occurrence Rates of Spread-F Echoes in Australasian VI Ionograms. *Radio Science* 58. ADS 2023RaSc...5807495E; DOI probably 10.1029/2022RS007495 (inferred) | snippet only |
| Liu, X. J., Wang, Z. W., Lu, J. Y., Liu, J. J., Hu, H. Q., Liu, J., Hu, Z. J., Wang, M., Zhao, K. (2022). Extreme Solar Flare-driven Short-wave Fadeout Observed by SuperDARN ZHO Radar. *Astrophys. J.* 933, 45. doi:10.3847/1538-4357/ac6f56 | full text (via fetch); P |
| Reeve, W. D. (2015). Sudden Frequency Deviations Caused by Solar Flares, Part II – Instrumentation and Observations. https://reeve.com/Documents/Articles%20Papers/Propagation%20Anomalies/Reeve_SuddenFreqDevMeas_P2.pdf | full text; G |
| Witvliet, B. A. (2022). Near Vertical Incidence Skywave: The propagation mechanism, the impact of antenna, backscatter and solar flares. Invited paper, Nordic HF conference (PITHIA-NRF funded). https://ris.utwente.nl/ws/portalfiles/portal/284844584/2022_Witvliet_NVIS_Nordic_HF.pdf | full text; G (invited conference) |
| Witvliet, B. A., et al. (2015). Measuring the isolation of the circularly polarized characteristic waves in NVIS propagation. *IEEE Antennas Propag. Mag.* https://research.utwente.nl/en/publications/measuring-the-isolation-of-the-circularly-polarized-characteristi/ | abstract; P |
| Witvliet, B. A., et al. The importance of circular polarization for diversity reception and MIMO in NVIS propagation. EuCAP. https://research.utwente.nl/en/publications/the-importance-of-circular-polarization-for-diversity-reception-a/ | snippet (8–11 dB figure); P (conference) |
| Modilim, O., Stubbs, C. (2026). Geographic Correlation Analysis of WWV Ionospheric Propagation: Phase Stability and Fading Statistics for Adaptive HF Radio. *HamSCI Workshop 2026*. https://hamsci.org/bibcite/export/ris/bibcite_reference/543 | abstract; G |
| Chernogor, L. F., Garmash, K. P., Guo, Q., Rozumenko, V. T., Zheng, Y. (2022). Ionospheric effects of the 5–6 January 2019 eclipse over the People's Republic of China: results from oblique sounding. *Ann. Geophys.* 40, 585–603. doi:10.5194/angeo-40-585-2022 | abstract; P |
| Lv, et al. (2022). High-Frequency Channel Modeling Based on the Multi-Source Ionospheric Assimilation Model. *Remote Sensing* 14(17), 4133. doi:10.3390/rs14174133 | abstract or snippet; P |
| Rao, S. V. B., Rao, T. R., Reddy, V. G., Lakshmi, D. R., Veenadhari, B., Dabas, R. S., Ahmed, I., Gupta, M. M., Reddy, B. M. (2002). HF radio signal fading and atmospheric radio noise measurements at low latitudes. *Radio Science* 37. doi:10.1029/2001RS002495 | abstract (EarthRef); numbers snippet only; P |
| Allen, J., Daly, M., Meloling, J., Mozaffar, N., Ontiveros, M., Verd, F., Truong, D. (2017). Mid-Latitude Mobile Wideband HF-NVIS Channel Analysis: Part 1. SSC Pacific TR 3075, DTIC AD1039223 | metadata only |
| Grandin, M., et al. (2024). The Gannon Storm: citizen science observations during the geomagnetic superstorm of 10 May 2024. *Geosci. Commun.* 7, 297–316. doi:10.5194/gc-7-297-2024 | abstract and front matter; P |
| Potter, R., Frissell, N. (2026). HamSCI PSWS Observations of the Gannon Geomagnetic Storm. BSc thesis, Univ. of Scranton; Potter, R., Frissell, N., Griffiths, G., Pandey, K. (2026). *HamSCI Workshop 2026*. https://hamsci.org/bibcite/reference/583 ; /565 | abstracts; G |
| ITU-R F.1821-1 (02/2026). Characteristics of advanced digital HF radiocommunication systems in the fixed and mobile service. https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1821-1-202602-I!!PDF-E.pdf | full text, text-searched |
| SpaceWeatherLive, solar-cycle-25 flare statistics (54 X-class flares in 2024). https://www.spaceweatherlive.com/en/solar-activity/top-50-solar-flares/solar-cycle/25.html | snippet; G |
| Fernandes, S., et al. (2026). Characterizing ionospheric variability through HF Doppler measurements: a statistical and numerical ray tracing analysis. *Front. Astron. Space Sci.* 13:1713968. doi:10.3389/fspas.2026.1713968 | full text, re-read for parameters (none published); P |

### Covered sources cited (details in [Sol], [F30]–[F33], [RNS])

- ITU-R F.1487-0 (2000); P.533-14; P.842-5; F.339-8; F.520-2; P.1321.
- Watterson, Juroshek & Bensema (1970); Watterson (1969) report; Balser & Smith (1962), *J. Res. NBS* 66D; Boys (1968); Epstein (1969), doi:10.1029/RS004i001p00053; Rifkin & Bello (1994), doi:10.1029/93RS03031.
- Stocker et al. (2013), doi:10.1002/2013RS005264; Stocker & Warrington (2011), doi:10.1029/2010RS004482; Warrington & Stocker (2003), doi:10.1029/2002RS002815; Warrington, Stocker & Siddle (2006), doi:10.1029/2005RS003294; Angling (2001) PhD thesis (Leicester); Nissen & Bello (2003), doi:10.1029/2002RS002746; Jodalen (2000) FFI report.
- Fitzgerald, Argo & Carlos (1999, OSTI 329722; 1993 LA-UR-93-2140); Miller (2017) IES slides; Basu, MacKenzie & Basu (1988), doi:10.1029/RS023i003p00363; Wang et al. (2019), doi:10.1186/s40623-019-1026-6.
- Chum et al. (2018), doi:10.1186/s40623-018-0976-4; Luo et al. (2021), *Ann. Geophys.* 39, 657–685; Reznychenko et al. (2020), doi:10.15407/rpra25.02.118; Collins et al. (2023), doi:10.5194/essd-15-1403-2023; Jones, Georges & Riley (1986), doi:10.1109/TAP.1986.1143842; Heitmann (2020) thesis, hdl:2440/130401.
- Chakraborty et al. (2018), doi:10.1002/2017RS006488; Chakraborty et al. (2022), doi:10.1029/2022JA030342; Fiori et al. (2022), *J. Space Weather Space Clim.*, https://www.swsc-journal.org/articles/swsc/pdf/2022/01/swsc220003.pdf; Tao et al. (2020), doi:10.1186/s40623-020-01278-z; Kitajima et al. (2026), doi:10.1186/s40623-026-02525-5; Spogli et al. (2024), doi:10.4401/ag-9117; Frissell et al. (2019), *Space Weather* 17, 118–132.
- Sensors 21:2210 (2021), doi:10.3390/s21062210. This one paper is cited as "Male et al." in [F31] and as "Tartón et al." in [Sol]; a search listing gives the authors as Male, Porté, Gonzalez, Maso, Pijoan, Badia. Maso et al. (2020), doi:10.3390/electronics9061037; Dear et al. (2024), doi:10.1029/2023RS007877; Auterman (1962) NBS TN 165.
- Lo et al. (2022), doi:10.3390/atmos13081340; Chartier et al. (2023), doi:10.1029/2023SW003590; Vanhamel et al. (2022), doi:10.1155/2022/4809313; Mahmoudian et al. (2026), doi:10.1029/2025SW004917; Cameron et al. (2024), doi:10.1029/2023RS007897; Aricoche et al. (2026), doi:10.3389/fspas.2026.1848834.
- Isode white paper on intermediate-term skywave variation.

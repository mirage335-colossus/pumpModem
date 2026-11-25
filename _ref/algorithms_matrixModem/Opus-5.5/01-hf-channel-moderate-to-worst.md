# The HF channel from benign to worst case

Research date: 24 November 2026. This chapter describes the natural phenomenon
Matrix Modem must survive. It concentrates on the quantities a one-way,
very-weak, tone-based receiver actually depends on, and on the time scales at
which they change. Earlier project chapters already review most classical
sources in depth; this chapter cites them rather than repeating them, adds
literature found in this pass, and translates the evidence into receiver
consequences. Numbers carry their source's definition and conditions. A value
marked **[derived]** is arithmetic from cited inputs, not a measurement.

Prior project evidence used here:
[F30](../../highFidelity-triple_Robust_Modem/Fable-5.2/30-skywave-signal-level-and-outages.md),
[F31](../../highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md),
[F32](../../highFidelity-triple_Robust_Modem/Fable-5.2/32-skywave-dispersion-highlatitude-equatorial.md),
[F33](../../highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md),
[RNS](../../resilience-triple_Robust_Modem/Fable-5.2/robust-nonstationary-channel-research.md)
and [Sol-6.2 HF channels](../Sol-6.2/hf-channels.md).

## Findings that change the design

1. **A narrowband tone does not see "delay spread" the way a wideband modem
   does.** Under the ITU-R F.1487/Watterson model, every propagation mode is a
   complex Gaussian process. A single tone of duration T passes through the
   sum of those processes evaluated at its own frequency. With equal path
   Doppler offsets, the sum is again one Rayleigh process with the combined
   Doppler spectrum. Multipath delay then affects a long tone only through
   symbol-edge smear (τ/T) and through the correlation between *different*
   tone frequencies. Doppler spread, Doppler shift and drift, and the
   slow-fading envelope are the parameters that decide tone-receiver
   performance. This is a model property; real multi-mode channels with
   different mode Doppler offsets produce several narrow lines rather than one
   broadened line, which a receiver can exploit (see [the design chapter](05-waveform-and-parameter-design.md)).
2. **Quiet paths are quieter, and disturbed paths harsher, than the F.1487
   ladder.** GPSDO-referenced FST4W measurements on a 1,558 km one-hop 14 MHz
   path (July 2022, Kp < 3) give a path-only spectral width with median about
   0.025 Hz and 90th percentile about 0.076 Hz (Griffiths et al. 2022; gamma
   fit after equipment deconvolution; estimator not defined in the source).
   At the other extreme, DAMSON's Harstad–Kiruna 95th-percentile 80%-power
   widths reach 49.5–73 Hz [F32]. F.1487's 0.1 Hz "quiet" and 30 Hz "disturbed"
   are test points, not bounds.
3. **The receiver's own equipment can dominate the quiet-channel budget.** In
   the same campaign a GPS-aided KiwiSDR added a median 0.068 Hz of apparent
   spread and TCXO transceivers showed offsets near 2 Hz at 10 MHz with 0.75 Hz
   thermal steps after transmitting (Griffiths et al. 2022). Long coherent
   integration is therefore conditional on disciplined oscillators *or* on a
   receiver that searches and tracks the equipment error. The Helper Carrier,
   used only as separate guidance, is one way to measure that common error.
4. **Common Doppler shift is heavy-tailed and drifts.** Daytime residual shift
   on a 2,590 km 10 MHz path is better described by a Cauchy law than by a
   Gaussian (Fernandes et al. 2026; no fitted parameters published). No measured
   distribution of drift *rate* exists in the literature found; derived rates
   are 0.2–5 mHz/s for TIDs, sunrise and storms, and about 0.1–0.25 Hz/s for
   minutes during flare-driven sudden frequency deviations (SFDs) (Reeve 2015,
   64 events; Chum et al. 2018 in [F31]). A linear drift ḟ limits a coherent
   window to about T ≤ (πḟ)^−½ for ≤ 1 rad of quadratic phase error **[derived]**:
   40 s at 0.2 mHz/s, 18 s at 1 mHz/s, 8 s at 5 mHz/s and 1.5 s during an SFD.
5. **In benign conditions the whole 2.2 kHz band fades together.** With
   single-mode intra-mode delay spreads of 20–100 µs, the frequency correlation
   across 2.2 kHz stays near one. Keyed frequency hopping inside the passband
   adds no diversity against the dominant fades exactly when the channel is
   otherwise easiest; diversity must come from time (interleaving over many
   coherence intervals). Hopping becomes a diversity source only with multi-mode
   delays of ~0.5 ms and more (coherence bandwidth ~150–700 Hz) [derived from
   [F31] §G and the two-ray heuristic].
6. **"Worst case" is a duration problem as much as a dispersion problem.**
   Shortwave fadeouts typically black out a sunlit path for 10–20 minutes with
   ~40 minutes recovery, and 3% exceed 90 minutes (Liu et al. 2022, citing
   Boulder statistics). Polar-cap absorption can exceed 100 dB for up to about
   four days, and storm-time MUF depressions last hours to days ([F30], Fiori
   et al. 2022). Cycle 25 has produced X-class flares at a much higher rate than
   cycle-average statistics imply. No receiver computation recovers a path that
   delivers no usable signal energy; only time, frequency and route choices do.
7. **A "closed" path is not always zero signal.** On a 7 MHz NVIS link at
   night above the MUF, Witvliet (2022) still observed diffuse ground-scatter
   reception 45–50 dB below the daytime level at 20 dB SNR. For a receiver with
   very large processing gain, availability products based on ordinary decode
   thresholds understate what is receivable. The Doppler and delay structure of
   such scatter was not reported, so its usefulness remains an open question.

## How the receiver experiences the channel

A Matrix-style receiver correlates the received baseband against tone or keyed
templates over windows of duration T and combines the results. Five separable
channel mechanisms act on that statistic:

| Mechanism | What the receiver sees | Time scale | Main design lever |
| --- | --- | --- | --- |
| Common Doppler shift and oscillator offset | Energy lands in the wrong frequency bin | Seconds to hours | Frequency search span and tracking; Helper guidance |
| Drift (Hz/s) | Energy spreads across bins within one long window | Tens of seconds | Drift (chirp) hypotheses for long windows |
| Doppler spread (fast fading) | Random gain variation within and between windows; energy smears over ~2σ in frequency | 5 ms – 40 s coherence | Tone spacing ≥ spread; subspace (multi-taper) detection; time interleaving |
| Slow fading (focusing, absorption, O/X polarization, mode beats) | Mean SNR varies 10–20 dB | 17 s – 60 min | Interleave across the slow-fade time scale or carry margin |
| Outage (MUF closure, SWF, PCA, storms) | No usable signal | 10 min – days | Message duration, erasure coding across the outage, frequency/route choice |

Delay spread enters this table only indirectly: through symbol-edge smear,
through frequency-selective fading *across* tones and hops, and through ISI if
a very late echo arrives (the round-the-world echo, below).

### Why the Watterson tap sum reduces to one fading process for a tone

Let the channel be h(t, τ) = Σ_k g_k(t) δ(τ − τ_k) with independent stationary
complex Gaussian processes g_k. A tone exp(j2πf₀t) emerges as
exp(j2πf₀t)·Σ_k g_k(t) exp(−j2πf₀τ_k). The phase factors are constants, so the
sum is a zero-mean complex Gaussian process whose spectrum is the power-weighted
sum of the path Doppler spectra. If all paths share one Doppler shift and
spectral width (the F.1487 default), a tone sees a single Rayleigh process with
that spectrum and the summed power. If the paths have different Doppler
offsets, the tone sees several lines. The delays τ_k matter only when the tone
starts or stops (smear of τ_k at each transition) and when comparing two tones
whose frequency separation is not small relative to 1/τ. This is why the
capacity calculations in [the limits chapter](03-information-limits-and-noncoherent-penalties.md)
can treat F.1487 channels as Rayleigh processes with Gaussian Doppler spectra.

## Parameter categories, benign to extreme

The table below merges measured statistics with derived quantities. It is
adapted from this pass's literature review (scratch report summarized here),
the prior chapters cited, and F.1487 Annex 3. Percentiles are of *detected*
signals; low-SNR censoring biases them benign [RNS]. The extreme column lists
separately observed extremes, not one joint observation.

| Parameter | A. Benign (quiet mid-latitude one-hop, Kp < 3) | B. Moderate (90–95th pct mid-latitude; low-latitude quiet/moderate; typical NVIS; sunrise, eclipse, moderate storm) | C. Severe (95th pct polar-cap/trough winter night at solar max; equatorial spread-F evenings; F.1487 high-latitude moderate) | D. Extreme (reported extremes) |
| --- | --- | --- | --- | --- |
| Common Doppler shift (ionosphere only) | ±0.1–0.3 Hz | ±0.5–1.5 Hz | ±2–5 Hz | 8–9 Hz SFD peaks; 13–25 Hz auroral event; ~50 Hz historical SFD |
| Equipment offset (add separately) | GPSDO ≈ 0 | GPS-aided SDR ±0.05–0.1 Hz | TCXO ≈ ±2 Hz at 10 MHz | ±0.5 ppm reference: ±7.5 Hz at 15 MHz |
| Drift rate | 0.2–1 mHz/s [derived] | 1–5 mHz/s [derived] | ~0.1 Hz/s (single auroral event) | 0.1–0.25 Hz/s for tens of seconds (flares/SFDs) |
| Doppler spread (estimator varies) | 0.01–0.1 Hz | 0.5–2.5 Hz | 5–15 Hz | 25–73 Hz; equatorial plumes ~±60 Hz |
| Coherence time, ρ = 0.5 (Gaussian) [derived] | 3.75–40 s | 0.15–0.75 s | 25–75 ms | 5–15 ms |
| Mean −10 dB fade duration [derived] | 1.9–19 s, recurring every 20–200 s | 75–375 ms | 13–38 ms | 3–6 ms |
| Differential delay | ≤ 0.5–1 ms | 1–3 ms | 3–7 ms | 7–11.4 ms (instrument-limited); RTW echo ~135–138 ms |
| Coherence bandwidth vs 2.2 kHz | ≥ 2.2 kHz (flat across band) | 0.3–1 kHz (2–7 independent cells) | 150–350 Hz; < 1.5 kHz in spread-F | 90–150 Hz |
| Stationarity of statistics | minutes to ~10 min; spectral width decorrelates in ~3 min | a few minutes; steps at sunrise/sunset | < 1 min at mode onset and in plumes | — |
| Slow fading / outage | log-normal 15–60 min; daily MUF gap of hours (~6 h/day observed on one 14 MHz path) | band openings/closings as ~10 dB steps | SWF blackout ~10–20 min + ~40 min recovery; 3% > 90 min | PCA ~8 h to ~4 days (>100 dB); storm MUF depression 5.5–8.5 h mean; band suppression for days |

Sources behind the rows: Griffiths et al. 2022 (FST4W spreads and equipment
spread); Fernandes et al. 2026 (heavy-tailed shift); Reeve 2015 and Chum et al.
2018 (SFDs); F.1487-0 Annex 3; Stocker et al. 2013, Warrington et al. 2006 and
DAMSON (high-latitude spread, via [F32]); Fitzgerald et al. 1999 (Kwajalein
spread-F, via [F32]); Liu et al. 2022 and Fiori et al. 2022 (outage durations);
Male et al. 2021 and Maso et al. 2020 (NVIS coherence bandwidth, via [F31]).

### Benign (A)

Quiet one-hop mid-latitude paths support coherent windows of seconds to tens
of seconds. FST4W-300 (1.8 s symbols at 0.56 Hz spacing) decodes routinely on
such paths with GPSDO references, while FST4W-900 and -1800 were judged usable
only on one-hop paths or on non-ionospheric/lower-HF paths (Griffiths et al.
2022). The binding constraints are the common-shift drift (needing chirp
hypotheses beyond ~10–20 s), equipment stability, and the 2–20 s fast fades
that recur every 20–200 s. Interleaving needs roughly 3–30 minutes to collect
ten independent fast fades, and 1–3 hours to average slow fading.

### Moderate (B)

Doppler spreads of 0.5–2.5 Hz give coherence times of 0.15–0.75 s. Most
weak-signal amateur modes with sub-second symbols remain usable here. FST4's
own guide states that decoding requires the spread to be below the tone spacing
and is best at ≤ 1/8 of it (Franke, Somerville & Taylor 2020). WSPR (1.46 Hz
tones) does not decode at a 3 Hz Watterson spread at any SNR, while FST4W-120
decodes at −24 dB at a 2 Hz spread (Griffiths et al. 2022, fst4sim). These are
simulation results on F.1487 channels; on-air channel behavior is blockier.

### Severe (C)

Spreads of 5–15 Hz occur at the 95th percentile on polar-cap and trough paths
in winter nights near solar maximum, and on roughly half of solar-maximum
evenings at Kwajalein during spread-F (Doppler spread above 6 Hz at the −10 dB
level). Equatorial spread-F occurrence is strongly local-time and season
dependent; at Hainan, strong range spread-F reached ~68% of ionograms at 22 LT
at equinox in 2002, but stayed below ~7% in low-activity years (Wang et al.
2010). Its solar-cycle dependence is not universal: Jicamarca and Kwajalein
showed no significant solar-cycle variation over 2001–2016 (Bhaneja et al.
2024). A modem cannot assume that solar minimum removes severe low-latitude
spread.

### Extreme (D) and outages

Category D combines 25–73 Hz spreads with 7–11 ms delays. Treat it as a stress
test, not a design point: the energy cost of noncoherent reception at these
spreads is extreme (see the next section and the limits chapter). Outages are
a separate axis. Shortwave fadeouts, polar-cap absorption, auroral absorption
and storm MUF depressions occur at the rates and durations in [F30] §4 and Fiori
et al. 2022; under cycle-25-maximum conditions X-flare fadeouts on the sunlit
hemisphere occur roughly weekly [derived]. In Witvliet's X1.6 fadeout the
signal fell by more than 40 dB but the background noise also fell 12 dB,
because most rural noise is itself propagated; SNR loss can be smaller than
signal loss at a noise-limited site and larger at a site dominated by local
man-made noise.

## What the categories mean for a tone receiver

The capacity calculations in [the limits chapter](03-information-limits-and-noncoherent-penalties.md)
quantify the consequences. The qualitative picture:

- **Doppler spread sets a minimum C/N0 for efficient noncoherent reception.**
  Efficient energy-detection reception needs the signal energy collected within
  one coherence time to be several times N0. Below that "knee" the information
  rate falls as the square of C/N0. With the 2σ convention, the knee moves up
  about 3 dB per doubling of the spread. The calculated knee is roughly
  −6 dB-Hz at 0.1 Hz, +2 dB-Hz at 0.5 Hz, +6 dB-Hz at 1 Hz, +10 dB-Hz at 2 Hz,
  +14 dB-Hz at 5 Hz, +18 dB-Hz at 10 Hz and above +20 dB-Hz at 30 Hz (defined
  as within about 4 dB of the Shannon limit; Gaussian Doppler, Rayleigh, ideal
  interleaving).
- **Tone spacing and symbol duration are separate controls.** A tone must be
  spaced so that Doppler-broadened neighbors do not overlap (spacing ≈ 1/T plus
  about twice the 2σ width), but the symbol may last many coherence times: the
  receiver then accumulates energy over a low-rank signal subspace. The FST4
  rule "spread below spacing" holds for single-bin detectors where spacing =
  1/T. It does not forbid long symbols with wide spacing, which the limits
  chapter finds near-optimal at low C/N0 in every Doppler category.
- **Frequency diversity inside 2.2 kHz is conditional.** It exists only in
  categories B–D. Time interleaving must carry the diversity in A.
- **The round-the-world echo is a symbol-scale ISI source for 160 ms symbols.**
  RTW echoes arrive about 135–138 ms after the direct signal (secondary
  sources; no level or occurrence statistics found). That is 0.86 of a 160 ms
  symbol: if present at a significant level, it overlaps the next symbol rather
  than adding intra-symbol multipath. Long symbols (seconds) make it a minor
  edge effect.
- **Simulators miss blocky, minutes-scale behavior.** On-air logs show 10–20 dB
  swings over ~10 s and blocky losses over minutes that Watterson simulators
  do not generate ([F33] §F; Isode). Matrix validation needs, in addition to
  F.1487 screens: slow O/X polarization fading, heavy-tailed common Doppler with
  mode jumps, SFD chirps near 0.15 Hz/s, a log-normal AR(1) slow-fade envelope
  on a ~15-minute scale, diurnal on/off availability with abrupt steps, an
  equipment-error axis (GPSDO, GPS-aided, TCXO), and an optional parameterized
  RTW tap labelled as a stress test.

## Contradictions and nuances to keep visible

1. **Estimator mismatch.** F.1487 and Watterson use 2σ of a Gaussian; Leicester
   and DAMSON use the narrowest band holding 80% of the power (1.28 × 2σ for a
   Gaussian); radar work uses FWHM or −10/−30 dB widths; FST4W "spectral width"
   is undefined in the inspected sources. Never compare raw numbers across
   these conventions.
2. **Solar-cycle dependence differs by mechanism and station**: Hainan strong
   spread-F rises with sunspot number, Jicamarca/Kwajalein bottomside spread-F
   shows no significant cycle dependence, mid-latitude Australasian spread-F is
   reported anti-correlated (snippet-level), and high-latitude relations at
   Leicester's paths were non-monotonic (Stocker et al. 2013).
3. **Absorption does not translate one-for-one into SNR loss** because external
   noise is itself propagated (Witvliet 2022). Storms can open paths as well as
   close them (Mahmoudian et al. 2026, in [Sol-6.2](../Sol-6.2/hf-channels.md)).
4. **Fade scales span about seven orders of magnitude** and standards disagree
   on the short-term law (F.339 log-normal vs P.842 Rayleigh deciles; Balser &
   Smith found about a third of records nonstationary). F.520-2's simulator fade
   durations of 0.05–1.5 s are far shorter than quiet-path fades of 2–20 s.
5. **Ground-level fading decorrelates within 32 km** (Modilim & Stubbs 2026,
   abstract), so per-site fade statistics are not regional, and two receivers a
   short distance apart could provide space diversity.
6. **Every published weak-signal threshold is simulator-calibrated** (AWGN or
   Watterson), including WSPR, FST4W and MIL-STD 75 bit/s. On-air availability
   errors under Doppler spread were documented by Angling (2001) using channel
   replay ([F33]).

## Gaps that remain

The literature found contains no measured mid-latitude HF fade-duration
distribution, no measured Rician K-factor distribution after about 1970, no
measured drift-rate distribution, no statistics on RTW echo level and
occurrence, no peer-reviewed mW-level WSPR/FST4W path-availability study, and
(within this pass's search budget) no 2020–2026 high-latitude Doppler-spread
percentile update. ITU-R F.1289 could not be verified and should not be cited
as the wideband channel standard without checking; the Vogler–Hoffmeyer model
is documented in NTIA reports 88-240, 90-255 and 92-284 ([F33]). These gaps are
why Matrix validation should use parameterized stress families and measured
captures, not one preset.

## Sources added in this pass

Access notes: **full** = full text read; **abstract** = abstract or record only;
**snippet** = search excerpt only (numbers unverified).

- Griffiths, G., Elmore, G., Robinett, R., Rhymes, L., Watrous, J. "FST4W on the HF Bands: Why – What to expect – Equipment – Results." ARRL/TAPR DCC 2022, pp. 12–22. https://web.tapr.org/tech_docs/DCCpapers/2022%2002.pdf (full; grey literature).
- Franke, S., Somerville, B., Taylor, J. "Quick-Start Guide to FST4 and FST4W." WSJT-X 2.3.0, 2020. https://wsjt.sourceforge.io/FST4_Quick_Start.pdf (full).
- Reeve, W. D. "Sudden Frequency Deviations Caused by Solar Flares, Part II." 2015. https://reeve.com/Documents/Articles%20Papers/Propagation%20Anomalies/Reeve_SuddenFreqDevMeas_P2.pdf (full; grey).
- Liu, X. J. et al. "Extreme Solar Flare-driven Short-wave Fadeout Observed by SuperDARN ZHO Radar." ApJ 933, 45 (2022). doi:10.3847/1538-4357/ac6f56 (full).
- Witvliet, B. A. "Near Vertical Incidence Skywave: The propagation mechanism, the impact of antenna, backscatter and solar flares." Invited paper, Nordic HF 2022. https://ris.utwente.nl/ws/portalfiles/portal/284844584/2022_Witvliet_NVIS_Nordic_HF.pdf (full).
- Wang, G. J. et al. "The statistical properties of spread F observed at Hainan station during the declining period of the 23rd solar cycle." Ann. Geophys. 28, 1263–1271 (2010). doi:10.5194/angeo-28-1263-2010 (full).
- Bhaneja, P. et al. "Statistical analysis of low latitude spread F at the American, Atlantic, and Pacific sectors using digisonde observations." Front. Astron. Space Sci. 11:1421733 (2024). doi:10.3389/fspas.2024.1421733 (full; numbers in figures only).
- Fernandes, S. et al. "Characterizing ionospheric variability through HF Doppler measurements." Front. Astron. Space Sci. 13:1713968 (2026). doi:10.3389/fspas.2026.1713968 (full; no fitted parameters published).
- Liu, Y., Dai, L. "Detection of the sporadic E layer using crowdsourced WSPR signal reports during the ascending phase of cycle 25." Front. Astron. Space Sci. 13:1946062 (2026). doi:10.3389/fspas.2026.1946062 (full).
- Chernogor, L. F. et al. "Ionospheric effects of the 5–6 January 2019 eclipse over the People's Republic of China: results from oblique sounding." Ann. Geophys. 40, 585–603 (2022). doi:10.5194/angeo-40-585-2022 (abstract).
- Modilim, O., Stubbs, C. "Geographic Correlation Analysis of WWV Ionospheric Propagation." HamSCI Workshop 2026 (abstract; grey).
- Rao, S. V. B. et al. "HF radio signal fading and atmospheric radio noise measurements at low latitudes." Radio Science 37 (2002). doi:10.1029/2001RS002495 (abstract; fade-rate numbers snippet-level).
- Richards, D. "WSPR at Midlatitudes from KN4NBI: A Year of Data at Solar Minimum." HamSCI 2021 poster (full; grey); Griffiths, G., Robinett, R. HamSCI 2021 poster on shipboard WSPR (full; grey).
- ITU-R F.1821-1 (02/2026), characteristics of advanced digital HF systems (full; contains no simulator channel parameters).

Classical sources (Watterson et al. 1970; Balser & Smith 1962; Epstein 1969;
Rifkin & Bello 1994; Stocker et al. 2013; Warrington et al. 2006; Nissen & Bello
2003; Fitzgerald et al. 1999; Fiori et al. 2022; Tao et al. 2020; Kitajima et
al. 2026; Spogli et al. 2024; Frissell et al. 2019) are reviewed with access
notes in [F30]–[F33] and [RNS].

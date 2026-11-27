# Over-the-horizon radar and other swept-FM interference against a weak-signal noncoherent MFSK receiver: literature review for the Matrix Modem

Prepared 2026-11-26. Scope: a one-way, very-low-rate noncoherent MFSK modem on HF skywave with about 6.25 Hz tone spacing, about 160 ms base symbols, tones placed by a keyed pseudo-random schedule inside a 2.2–2.8 kHz SSB audio passband, received at about +10 to −25 dB-Hz with long noncoherent integration, soft-decision coding and interleaving. The question is what over-the-horizon radar (OTHR) and related swept-FM emitters do to such a receiver, and what the configured "SNR path-loss margin" (3, 6, 10, 13, 16, 20 dB) and the interleaver must do so that communication continues without a drastic failure.

## 0. How to read this review

**Relation to earlier project research.** Three files were skimmed first and are not repeated here:

- [`Sol-6.2/channel-envelope-review.md`](../../Sol-6.2/channel-envelope-review.md) says interference and receiver overload must be separated from propagation, with no numbers for radar.
- [`Sol-6.2/hf-channel-structure-and-extremes.md`](../../Sol-6.2/hf-channel-structure-and-extremes.md) gives man-made noise levels, lists clipping or blanking, robust bin statistics and hopping as the mitigations for interfered noise, and records that occupancy statistics for amateur bands were not found.
- [`Opus-5.5/reviews/review-hf-channel.md`](../../Opus-5.5/reviews/review-hf-channel.md) mentions OTHR only for its coherent dwell of many seconds.

Section [What this adds or contradicts](#what-this-adds-to-or-contradicts-in-the-skimmed-research) lists the differences.

**Evidence labels on numbers.**

- **[M]** measured and published by an agency or in a peer-reviewed paper.
- **[P]** published model, specification or formula.
- **[V]** read from a volunteer monitor's spectrogram or log (waveform parameters are measured, but the receivers are uncalibrated and the attribution to a radar is the monitor's).
- **[A]** anecdotal: S-meter readings, verbal strength and persistence descriptions, hobbyist wiki entries without a stated source.
- **[D]** derived here. Formulas and the script outputs are in [`othr_interference_calc.py`](../../../../_appraisal/algorithms-matrixModem/othr-interference/othr_interference_calc.py) and [`othr_interference_calc.out.txt`](../../../../_appraisal/algorithms-matrixModem/othr-interference/othr_interference_calc.out.txt). These are theoretical values with the assumptions stated beside them.
- **[Mem]** recalled from memory and not re-read in this session. Treat as unverified.

**Access labels on sources.** [full] the text was read; [abstract] only the abstract or index record; [snippet] only a search-engine excerpt. Every reference in the [reference list](#references) is marked **[verified]** (the DOI resolved at Crossref to the stated title, or the URL was fetched and read) or **[unverified]**.

**Search limits.** Defence-agency hosts (DST Group, DTIC) and several publishers returned HTTP 403. The standard OTHR text (Fabrizio 2013) and the Radar Handbook chapter could not be read. No measured study of OTHR impact on FT8, WSPR or any other weak-signal digital mode was found; that absence is itself a finding (section 7).

## Summary of findings

1. **Most documented OTHR in the amateur and nearby bands is a periodic linear-FM sweep, 10–160 kHz wide, repeating 7–83 times per second** (the Iranian radar, at 150–870 pulses per second, is the exception). Volunteer monitoring gives 12 kHz at 40 sweeps/s for the Russian 29B6, 10 kHz at 42–83 sweeps/s in 2.5–10 s bursts for the Chinese "Foghorn", 10–160 kHz at 10–67 sweeps/s for other Chinese systems, and 20–80 kHz at 12.5–50 sweeps/s for the UK radar on Cyprus [V]. Published design ranges are 5–50 kHz, 2–60 Hz and 1–40 s coherent dwell ([P], read only as a search excerpt), and 5–40 kHz, 10–60 Hz and 0.7–20.5 s for the retired US OTH-B [P]. Oceanographic radars sweep 22–300 kHz once every 0.13–1 s at 30 W to 1 kW [P].
2. **All the modem's tones are inside the radar's sweep.** A 2.2–2.8 kHz passband is 2–28% of the swept band, so keyed hopping inside the passband gives no frequency diversity against the radar as a whole [D]. It only randomizes which tones coincide with the radar's spectral lines.
3. **Left alone, the radar is a noise-floor rise of `10·log₁₀(1 + ρ_W·W/B)` dB**, where `ρ_W` is how far the radar's in-band pulse stands above the noise in the 2.8 kHz passband and `B` is the swept bandwidth [D]. A 12 kHz radar that is only 10 dB above the band noise costs 5.2 dB. A radar logged at S9+20 dB on a quiet 14 MHz band costs 45–60 dB. No margin option survives a strong radar that is not removed; the largest option (20 dB) is used up by a 10 kHz radar 25.5 dB above the noise floor, about four S-units [D].
4. **The interference is sparse in three different ways, and which one is usable depends on the radar** [D]:
   - *In frequency*, when sweeps are coherent and the repetition rate is well above the bin width: a comb of lines every `f_r`. A comb notch of width `w` removes a fraction `w/f_r` of the spectrum: 8% (0.36 dB) at 50 Hz with a 4 Hz notch.
   - *In time*, when the sweep is much wider than the passband: a train of chirp pulses with duty `W/B`. Blanking costs `−10·log₁₀(1−d)`.
   - *Along the chirp*, by dechirping each crossing: an idealized `2·f_r/W` of the time-frequency plane.
5. **At the modem's own 6.25 Hz by 160 ms resolution nothing is sparse** for any OTHR with `f_r ≥ 6.25 Hz`: every cell is touched [D]. With a rectangular window and a 50 Hz comb, no bin is more than 10 dB below the mean interference; with a Hann window half the bins are within 20 dB of it (simulation, [D]). Excision therefore has to happen before the symbol transform, at finer time resolution or with a longer coherent analysis.
6. **Time blanking is weaker than `W/B` suggests for the common 10–12 kHz, 40–67 Hz radars.** Each crossing of the passband has a time-bandwidth product `W²/(B·f_r)` of only about 16, so the pulses have slowly decaying tails. Removing 99% of the in-band energy of a 10 kHz, 50 Hz radar needs 39% of the samples blanked, and 99.9% needs 59%, against a nominal duty of 28% (simulation with a steep filter, [D]). The comb is the better domain for those radars; blanking is the better one for 10 Hz or slower sweeps of 20 kHz and more.
7. **The comb survives ordinary mid-latitude propagation and fails in disturbed conditions.** A 4 Hz notch left −48 dB residual with a 1 Hz Doppler spread and −1 dB with a 10 Hz spread (simulation, [D]). Sweep-to-sweep random phase destroys the comb completely.
8. **Classic anti-jam theory says the same thing three ways.** Uncoded noncoherent FH/MFSK loses 32–36 dB at 10⁻⁵ to worst-case partial-band noise ([D], reproducing the textbook result). Diversity with side information recovers all but 1.6–7.4 dB [P]. Linear energy combining without side information makes more diversity *worse* [P]. A long noncoherent integrator that sums raw tone energy is exactly that last receiver.
9. **A robust metric without side information costs several dB more than erasure with it.** For a fraction `ρ` of strongly hit cells, erasure with perfect knowledge costs 0.46 dB (ρ = 0.10) or 1.25 dB (ρ = 0.25) of integration time; a clipped energy metric with no knowledge costs 2.5 dB or 4.6 dB at its best clip level; an unprotected energy sum costs 33 dB or 36 dB when the hit cells are 20 dB above the noise [D].
10. **Persistent radars cannot be outlasted by interleaving; burst radars can, but not with a periodic interleaver.** Radars are logged as persistent, and in one case as still present two hours later [A]. Bursts of 2.5–10 s, 50 s in every 90 s, and 120 s in every 240 s are logged [V]. A regularly spaced codeword has spacings at which every one of its symbols is hit, while a pseudo-random placement stays near the mean hit fraction [D].
11. **Radars choose channels that look empty.** OTHR frequency management includes a spectrum monitor (3 kHz steps in the ROTHR) and picks clear channels [P]. A modem below the noise floor looks like a clear channel, so the radar will not avoid it.
12. **Receiver practice.** AGC should be off or held; weak-signal software documentation says so [P]. FMCW radar is constant-envelope ahead of the SSB filter, so a wideband IF noise blanker sees nothing to blank [D], and analogue blankers splatter when strong signals are nearby [P].

## 1. What the emissions look like in a 2.2–2.8 kHz receiver

### 1.1 Common structure

A linear FMCW radar with swept bandwidth `B` and waveform repetition frequency `f_r` has sweep rate `k = B·f_r`. A receiver of bandwidth `W < B` sees one chirp crossing per sweep, lasting `W/k` seconds, so the duty in the passband is `W/B` [P: ITU-R M.2234 gives exactly this "time ratio" `B_r/B_c`]. The spectrum of a periodic LFM with a large time-bandwidth product is close to rectangular across `B` [P: Abramovich et al. 2022; Klauder et al. 1960]. The repetition frequency of skywave radars is bounded by range folding to roughly 100 Hz or less [P: Abramovich et al. 2022]. For oceanographic radars the sweep rate is the product of sweep repetition frequency and bandwidth, with 100 kHz/s and 2 Hz as the worked example [P: ITU-R M.2321].

Historical and overview sources (Headrick and Skolnik 1974; Headrick and Thomason 1998; Thomason 2003; Li 1998) give no repetition-rate numbers in the parts read. Early US radars used simple 100 µs pulses and then phase-coded pulses; later ones adopted FMCW, described as more compatible with other HF users than pulses [P: Thomason 2003].

Published design ranges for skywave OTHR:

| Source | Bandwidth | WRF | Coherent dwell | Power | Label |
| --- | --- | --- | --- | --- | --- |
| Francis, Cervera and Frazer 2017 (generic skywave OTHR) | 5–50 kHz | 2–60 Hz | 1–40 s | not given | [P], [snippet] |
| FAS data sheet for AN/FPS-118 OTH-B (retired) | 5–40 kHz | 10–60 Hz | 0.7–20.5 s | 1 MW CW, 80 dBW ERP, 5–28 MHz | [P], [full], 1999 page |
| Abramovich et al. 2022 (JORN and ROTHR class) | 20–25 kHz examples | ≤ about 100 Hz; examples 48.9 and 50 Hz | not given | not given | [P], [full] |

A consequence of the dwell figures: within one coherent dwell the waveform is phase-coherent from sweep to sweep (Doppler processing requires it), so the line comb is a designed property and not an accident. Between dwells the radar may change frequency, bandwidth or repetition rate.

### 1.2 By system class

Crossing time and duty are for `W = 2.8 kHz` [D]. "sps" is sweeps per second.

| System | Waveform | WRF | Swept bandwidth | Sweep rate | Crossing; duty | Burst and frequency behaviour | Bands | Label and source |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Russia 29B6 "Container" | FM on pulse (monitor's term); sweeps | 40 sps (Wikipedia lists 40–50 pulses/s [snippet]) | 12 kHz necessary; about 30 kHz occupied with sidebands | 480 kHz/s | 5.8 ms; 23% | Continuous for long periods; up to 5 transmissions at once in one band; 12 kHz, 40 pps bursts seen in 2025 may be the same radar | 40–12 m | [V] IARU wiki, newsletters 2021-12, 2024-07, 2025-09, 2026-10 |
| China "Foghorn" | FMCW bursts | 66.67 and 50 sps usual; 41.7, 48 and 83.3 also | 10 kHz | 417–833 kHz/s | 3.4–6.7 ms; 28% | Bursts of 2.5, 3.8, 5.1 s (2021) and 3–10 s (2026); jumps frequency; several bursts can collide | 40–12 m | [V] same; ARRL 2017 |
| China continuous | FMCW | 50 or 66.67 sps | 10 kHz | 500–667 kHz/s | 4.2–5.6 ms; 28% | Continuous | 20–15 m | [V] IARU wiki |
| China 40 kHz | FMCW | 10 or 20 sps | 40 kHz | 400–800 kHz/s | 3.5–7 ms; 7% | not described | 20, 15 m | [V] IARU wiki |
| China wideband | FMCW | 10 sps | 160 kHz (up to 360 kHz reported) | 1600 kHz/s | 1.75 ms; 1.8% | Continuous, or about 50 s on in every 90 s; centre frequency shifts | 40–12 m | [V] IARU wiki; ARRL 2021 |
| UK "PLUTO", Cyprus | FMCW | 50 or 25 sps; sometimes 12.5 | 20 kHz mostly; 40 kHz sometimes; 80 kHz rarely | 500–1000 kHz/s | 2.8–5.6 ms; 14% (7% at 40 kHz) | Up to 3 at once in a band | 17–10 m, rarely 30 m | [V] IARU wiki; newsletters |
| Iran (Ghadir/Sepehr class) | Amplitude-modulated pulses (monitor's term) in bursts | Alternating pairs: 150 and 313; 226 and 333; 307 and 870; 333 and 695 | about 45–60 kHz | 6.8–39 MHz/s if swept | under 0.5 ms; pulses not resolved in 2.8 kHz | Hops after every burst, or about every 31 s (5 bursts); daily on 28.860 MHz | 10 m | [V] IARU wiki; newsletters 2024-07, 2025-09; ARRL 2018 |
| Australia JORN | FMCW; generic design ranges in 1.1 | 7 sps in the bursts heard in Europe | 10 kHz (sometimes 12) | 70 kHz/s | 40 ms; 28% | Short bursts with an intro tone; frequency managed by clear-channel search | 30 m in Europe | [V] IARU wiki; [P] Earl and Ward 1987 |
| USA ROTHR AN/TPS-71 | FMCW bursts | 25, 32, 42 Hz in samples | 8 kHz in samples; 4, 16, 50, 100 kHz listed | 200–336 kHz/s | 8–14 ms; 35% | Groups of bursts, then a small frequency shift; rate often changes between bursts | 5–28 MHz | [A] sigidwiki (no source per value) |
| USA AN/FPS-118 OTH-B (retired) | FM/CW | 10–60 Hz | 5–40 kHz | 50–2400 kHz/s | — | — | 5–28 MHz | [P] FAS |
| USSR Duga "Woodpecker" (1976–1989) | Phase-coded pulses, 31 chips of 100 µs | 10 Hz usual; 16, 20 Hz | about 40 kHz typical | pulsed | 3.1 ms pulse; 3% | About 7 minutes per transmission | 7–19 MHz | [A] Wikipedia, citing Martinez 1982 and an FCC study |
| SuperDARN | Multi-pulse sequences (7 or 8 pulses of about 300 µs) | — | about 4.5–6 kHz | pulsed | — | Short bursts; hops | 8–20 MHz; seen on 20 m | [V] IARU; [snippet] for pulse length (Berngardt et al. 2015; Chisham et al. 2007 is the general reference) |
| Oceanographic FMICW (CODAR class), ITU systems 1–4 and Table 1 | Gated FMCW, 50% duty, gate period 486–1946 µs | 1–2 Hz (sweep time 0.5–1 s) | 25.6, 49.4, 101 kHz typical | 26–202 kHz/s | 14–109 ms; 2.8–11% | Continuous | 4.5, 13.5, 24.6 MHz | [P] ITU-R M.1874-1 |
| Oceanographic FMCW (WERA class), ITU systems 5–9 | Continuous FMCW | 2–7.7 Hz (sweep interval 130–500 ms) | 12.5–1000 kHz | 25 kHz/s to several MHz/s | — | Continuous | 8–42 MHz | [P] ITU-R M.1874-1 (Gurgel et al. 1999 is the WERA reference, not read) |
| Oceanographic FMICW, ITU systems 10–13 | Gated FMCW, 50% | 1.4–4 Hz (0.25–0.7 s) | 22, 100, 300 kHz | 31–1200 kHz/s | — | Continuous | 9.2, 24.5, 41.9 MHz | [P] ITU-R M.1874-1 |
| Chirp ionosondes | Linear FM, one pass | once per 5–30 min | about 3–30 MHz | 100 kHz/s | 28 ms; about 10⁻⁴ per sounder | — | all | [A] Greenman and Martinez; crossing time [D] |
| Unidentified | sweeps | 86–92 sps; 100 sps; 25 sps | 60 kHz; 10 kHz; 4 kHz | — | — | very rare | — | [V] IARU wiki |
| Unidentified "slow radar" | sweeps of 4 s | 0.25 sps | 4 kHz | 1 kHz/s | 2.8 s; 70% | 120 s on, 120 s off, daily for long hours on 14.132 MHz | 20 m | [V] newsletter 2024-07 |

Not obtained with any verified waveform numbers: France's Nostradamus (Bazin et al. 2006 was not readable), naval high-frequency surface-wave radars (only the abstract of Ponsford and Wang 2010 was read), and pulse ionosondes (the abstract of Reinisch et al. 2009 has no waveform numbers).

**A caution on waveform type.** The periodic-LFM picture is today's. Abramovich et al. (2022) design constant-modulus periodic FSK, Costas and noise-like waveforms for OTHR against fast targets. Such a waveform would keep the periodicity (a comb at `f_r`) and lose the chirp's time sparsity in a narrow receiver [D].

### 1.3 Received levels at long range

- Volunteer logs give S5 to S9+ for most radar entries, with one S9+20 dB entry for a 20 kHz, 50 sps signal on 21.315 MHz and repeated entries describing very strong, persistent signals [A: IARU newsletters 2021-12, 2024-07, 2025-09, 2026-10]. S-meters are uncalibrated.
- A crude link estimate agrees [D]. With 80 dBW ERP [P: FAS], 125 dB free-space loss for a 3000 km hop at 14 MHz and 10 dB of other loss, an isotropic antenna in the main beam receives about −25 dBm, which is S9+48 dB on the nominal −73 dBm scale. At 6000 km with 15–25 dB of extra loss it is S9+27 to S9+37 dB. A receiver 20–40 dB down in the sidelobes or back lobe sees S7 to S9+28 dB.
- Band noise for comparison [D, from the ITU-R P.372 coefficients already recorded in [`hf-channel-structure-and-extremes.md`](../../Sol-6.2/hf-channel-structure-and-extremes.md)]: in 2.8 kHz at 14 MHz, −104 dBm (rural) to −119 dBm (quiet rural); at 7 MHz, −96 to −110 dBm. An S9+20 dB radar is therefore 51–66 dB above the 14 MHz noise floor, and an S9 radar is 23–37 dB above the 7 MHz floor.
- Oceanographic radar by skywave: a radar near Brest was measured at 30–31 dBµV/m in 3 kHz at three German sites roughly 1000 km away, about 15 dB above the measuring system's 16 dBµV/m floor [M: ITU-R M.2234, Attachment 8]. A radar-side paper nevertheless judges the impact of low-power FMCW HF radars on other services to be small [P: Gurgel et al. 2007; abstract].

## 2. Occupancy statistics

**What exists.**

- The IARU Region 1 monitoring service reported close to 800 OTH radar observations in November 2021 and about 4500 for that year, more than every other interference source; the counts include repeated reports of the same radars [V: ARRL 2021]. Each of the four newsletters read (December 2021, July 2024, September 2025, October 2026) states that radars, mostly military OTHR, are the most numerous intruders in the amateur HF bands [V].
- Per-band counts appear occasionally: one national coordinator logged 64 reports of the Chinese burst radar in 20 m and 20 in 15 m in July 2024 [V].
- Persistence is described only in words: long-lasting or persistent for many entries; one radar across 7055–7075 kHz (which contains the FT8 frequency 7074 kHz) still present two hours after it was first logged; Foghorn bursts every morning (UTC) on several bands; the Iranian radar daily [A: newsletters 2025-09, 2026-10].
- Simultaneity: up to 5 Container transmissions and up to 3 PLUTO or Iranian transmissions in one band at once [V: IARU wiki].
- For oceanographic radars, a European skywave study found the −6 dB interference-to-noise criterion in a 3 kHz receiver exceeded for at most 18.4% of the time for the worst system, after accounting for duty and the `B_r/B_c` time ratio; a worldwide study gave at most 1.6% of time and area [P: ITU-R M.2234]. The −6 dB criterion is the one adopted in WRC-12 Resolution 612 [snippet].

**What does not exist in anything found.** No source gives the fraction of time, or of band, that OTHR occupies an amateur band at a stated threshold. Monitoring counts are reports by volunteers, not samples of a defined population, so they cannot be converted to a probability. Academic occupancy campaigns (the Cyprus series, 2012 onward; the UMIST series) report congestion per ITU allocation in 1 kHz channels and do not separate radar from other users [M: Haralambous and Mostafa; Mostafa et al. 2017]. The earlier project note that quantitative amateur-band occupancy was not found still stands. An HF interference model built from transmitter databases, ray tracing and a model ionosphere reproduces diurnal, seasonal and geographic variation and was checked against JORN spectrum-monitor data; it describes interference *to* the radar [P: Pederick and Cervera 2016; abstract].

**Radar behaviour that shapes occupancy.** OTHR frequency management systems include a spectrum monitor and pick channels that are clear [P: Earl and Ward 1987; Thayaparan et al. 2023]. The ROTHR monitor measures power in 3 kHz steps over 5–28 MHz, and the radar's frequency-agile signal is described as rarely interfering with conventional users [P: McNeal 1997]. Wideband HF radar waveforms can be designed with suppressed bands [P: Lindenfeld 2004]. All of these protect signals the radar can detect. A signal 20–40 dB below the noise in 2.8 kHz is not one of them [D].

## 3. Effect on a narrowband noncoherent tone receiver

Notation: radar received power `P`, swept bandwidth `B`, repetition `f_r`, sweep rate `k = B·f_r`; receiver passband `W`, bin spacing `Δf = 6.25 Hz`, symbol `T = 160 ms` (`Δf·T = 1`), noise density `N₀`. Everything in this section is derived here unless marked; the formulas are elementary and the small simulations are in the script.

### 3.1 Formulas

1. **Mean level, any coherence.** The radar's power spectral density is `P/B`, so the mean interference power in a bin is `P·Δf/B` and the mean in-bin interference-to-noise ratio is `P/(N₀·B)`. With `ρ_W = P/(N₀·W)` (the in-band pulse over the passband noise), the mean ratio is `ρ_W·W/B` for `B > W`. The simulation reproduces `P·Δf/B` to 0.00 dB for coherent and random-phase sweeps.
2. **Coherent sweeps: line comb.** Lines at every multiple of `f_r`, each of power `P·f_r/B`. A line centred on a bin is `f_r·T` times the mean: +9 dB at 50 Hz. The spacing in bins is `f_r/Δf`: 8 at 50 Hz, 6.4 at 40 Hz, 4 at 25 Hz, 1.6 at 10 Hz, 1.1 at 7 Hz. Because the radar's carrier is unrelated to the tone grid, the lines sit at an arbitrary sub-bin offset.
3. **Fraction of bins hit.** With an analysis window whose main lobe is `n_w` bins wide, `ρ_f ≈ min(1, n_w·Δf/f_r)`. With a notch of width `w` applied at finer resolution, `ρ_f = min(1, w/f_r)`.
4. **Fraction of time hit.** If `f_r·T ≥ 1`, every symbol contains at least one sweep, so every symbol is hit. If `f_r·T < 1` (oceanographic radars), the fraction of symbols a crossing touches is about `(W/k + T)·f_r`.
5. **Time-domain view.** A pulse train with duty `d = W/B`, pulse length `W/k`, period `1/f_r`. The time-bandwidth product of one crossing is `N_tb = W²/k`. The crossing is a clean pulse only when `N_tb ≫ 1`.
6. **Single sweep through one bin.** Each sweep deposits energy `P·Δf/k` in a bin; because `k ≫ Δf²` the bin filter rings for `1/Δf` and its peak response is `P·Δf²/k`, which is 41 dB below `P` at 500 kHz/s. In one bin the radar is therefore never a "pulse"; it is either a line or noise-like.
7. **Sweep-to-sweep random phase.** The comb disappears. Cell powers become noise-like (simulation: 39.5%, 93% and 98% of cells above 0, −10 and −20 dB of the mean, against 36.8%, 90.5% and 99.0% for exponential statistics).
8. **Ionospheric Doppler spread `ν` (Gaussian, 2σ).** Each line widens to `ν·√(2·L·ln10/10)` at `−L` dB: 3.0, 3.7 and 4.3 Hz at −20, −30 and −40 dB for `ν = 1 Hz`; 30, 37 and 43 Hz for `ν = 10 Hz`. The comb is usable while that width is well below `f_r`. Several propagation modes share the same line frequencies (each with its own Doppler shift), so multipath does not fill the comb; it lengthens the time-domain pulses by the delay spread.
9. **Dechirp bound.** Dechirping one crossing concentrates the radar into a resolution cell of `k/W` Hz. Notching two such cells per crossing removes `2·f_r/W` of the time-frequency plane, independent of `B`: 3.6% at 50 Hz, 0.7% at 10 Hz. This assumes one mode and known sweep timing and rate; each extra mode needs its own notch.

### 3.2 Worked numbers

From section A of the script output; all [D]. `d_t+g` adds a 2 ms guard per crossing; "native" is the fraction of 6.25 Hz by 160 ms cells the chirp passes through if nothing finer is done.

| Radar | `k` kHz/s | Crossing ms | `d_t = W/B` | `d_t+g` | Sweeps per symbol | Line spacing, bins | `N_tb` | Comb notch 4 Hz | Dechirp bound | Native cells |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 kHz at 50 Hz | 500 | 5.6 | 0.28 | 0.38 | 8 | 8 | 15.7 | 0.08 | 0.036 | 1.00 |
| 10 kHz at 66.67 Hz (Foghorn) | 667 | 4.2 | 0.28 | 0.41 | 10.7 | 10.7 | 11.8 | 0.06 | 0.048 | 1.00 |
| 12 kHz at 40 Hz (29B6) | 480 | 5.8 | 0.23 | 0.31 | 6.4 | 6.4 | 16.3 | 0.10 | 0.029 | 1.00 |
| 20 kHz at 50 Hz (PLUTO) | 1000 | 2.8 | 0.14 | 0.24 | 8 | 8 | 7.8 | 0.08 | 0.036 | 1.00 |
| 20 kHz at 10 Hz | 200 | 14 | 0.14 | 0.16 | 1.6 | 1.6 | 39 | 0.40 | 0.007 | 1.00 |
| 40 kHz at 25 Hz | 1000 | 2.8 | 0.07 | 0.12 | 4 | 4 | 7.8 | 0.16 | 0.018 | 1.00 |
| 40 kHz at 10 Hz | 400 | 7 | 0.07 | 0.09 | 1.6 | 1.6 | 19.6 | 0.40 | 0.007 | 1.00 |
| 160 kHz at 10 Hz | 1600 | 1.75 | 0.018 | 0.037 | 1.6 | 1.6 | 4.9 | 0.40 | 0.007 | 1.00 |
| 10 kHz at 7 Hz (JORN as logged) | 70 | 40 | 0.28 | 0.29 | 1.1 | 1.1 | 112 | 0.57 | 0.005 | 1.00 |
| 4 kHz at 25 Hz (unidentified) | 100 | 28 | 0.70 | 0.75 | 4 | 4 | 78 | 0.16 | 0.018 | 1.00 |
| 45 kHz at 313 Hz (Iran, if swept) | 14085 | 0.2 | 0.06 | 0.69 | 50 | 50 | 0.6 | 0.013 | 0.062 | 1.00 |
| CODAR 25.6 kHz at 1 Hz | 25.6 | 109 | 0.11 | 0.11 | 0.16 | 0.16 | 306 | none | 0.0007 | 0.27 |
| CODAR 49.4 kHz at 2 Hz | 98.8 | 28 | 0.057 | 0.061 | 0.32 | 0.32 | 79 | none | 0.0014 | 0.38 |
| CODAR 101 kHz at 2 Hz | 202 | 14 | 0.028 | 0.032 | 0.32 | 0.32 | 39 | none | 0.0014 | 0.35 |
| Slow 4 kHz at 0.25 Hz | 1 | 2800 | 0.70 | 0.70 | 0.04 | 0.04 | 7840 | none | 0.0002 | 0.046 |

Reading the table:

- For every OTHR row the "native" column is 1.00. An energy detector working only on its own cells sees every cell contaminated.
- The slow 4 kHz sweep is the one case that *is* sparse at native resolution: it moves 160 Hz per symbol, so it occupies about 28 of 448 bins in a symbol and behaves as moving partial-band interference.
- A radar whose sweep is not much wider than the passband (4–8 kHz) has a duty of 35–70% and cannot be blanked; only the comb helps.

### 3.3 Simulation results

Complex baseband, 2.56 s, no noise, passband 2.8 kHz inside the sweep, random sub-bin offset; all [D].

**Cells at native resolution** (fraction of bin-symbol cells above the stated level relative to the mean `P·Δf/B`):

| Case | Window | above 0 dB | above −10 dB | above −20 dB | above −30 dB |
| --- | --- | --- | --- | --- | --- |
| 10 kHz at 50 Hz, coherent | rectangular | 0.25 | 1.00 | 1.00 | 1.00 |
| same | Hann | 0.25 | 0.50 | 0.50 | 0.75 |
| same, random phase per sweep | Hann | 0.47 | 0.93 | 0.99 | 1.00 |
| same, coherent, 1 Hz Doppler spread | Hann | 0.20 | 0.35 | 0.44 | 0.57 |
| same, coherent, 10 Hz Doppler spread | Hann | 0.21 | 0.52 | 0.70 | 0.88 |
| 12 kHz at 40 Hz, coherent | Hann | 0.31 | 0.50 | 0.56 | 0.81 |
| 40 kHz at 25 Hz, coherent | Hann | 0.50 | 0.75 | 1.00 | 1.00 |
| 20 kHz at 10 Hz, coherent | Hann | 0.56 | 0.98 | 1.00 | 1.00 |
| CODAR 25.6 kHz at 1 Hz, ungated | Hann | 0.11 | 0.13 | 0.14 | 0.15 |
| CODAR 25.6 kHz at 1 Hz, gated 50% | Hann | 0.11 | 0.28 | 0.77 | 0.98 |

- A rectangular 160 ms window is useless against the comb: leakage keeps every bin within 10 dB of the mean.
- A Hann window gives clean bins only when lines are at least about 6 bins apart, and then only a quarter to a half of the bins; it also costs the wanted tone 1.76 dB of noise bandwidth [Mem, standard window property].
- Gating in FMICW radars spreads each sweep over many delayed images, so a gated CODAR contaminates far more cells than an ungated one at the −20 dB level.

**Blanking duty needed** (smallest fraction of time that holds the stated share of the in-band interference energy; the gate is applied after the passband filter):

| Case | `W/B` | 99% (20 dB) | 99.9% (30 dB) | 99.99% (40 dB) | Same with a fully tapered passband |
| --- | --- | --- | --- | --- | --- |
| 10 kHz at 50 Hz | 0.28 | 0.39 | 0.59 | 0.72 | 0.21 / 0.31 / 0.39 |
| 10 kHz at 50 Hz, two modes 1.5 ms apart | 0.28 | 0.39 | 0.59 | 0.74 | 0.26 / 0.38 / 0.48 |
| 12 kHz at 40 Hz | 0.23 | 0.31 | 0.47 | 0.63 | 0.17 / 0.25 / 0.31 |
| 20 kHz at 10 Hz | 0.14 | 0.15 | 0.19 | 0.23 | 0.10 / 0.12 / 0.14 |
| 40 kHz at 25 Hz | 0.07 | 0.12 | 0.21 | 0.31 | 0.06 / 0.08 / 0.11 |
| CODAR 25.6 kHz at 1 Hz, ungated | 0.11 | 0.096 | 0.10 | 0.11 | 0.07 / 0.08 / 0.09 |
| CODAR 25.6 kHz at 1 Hz, gated 50% | 0.11 | 0.37 | 0.71 | 0.89 | 0.43 / 0.82 / 0.95 |

- Blanking reaches the nominal `W/B` only when `N_tb` is in the hundreds.
- A steep SSB-like filter makes the tails worse. A smooth, tapered filter ahead of the gate roughly halves the duty needed, but the tapered column weights the band centre, so it flatters the result for edge tones.
- For gated oceanographic radars time blanking is poor at every level beyond 10 dB.

**Comb notch** (residual in-band interference after notching every line on one 2.56 s Hann-windowed record, line frequencies known):

| Case | 2 Hz notch | 4 Hz notch | 8 Hz notch |
| --- | --- | --- | --- |
| 10 kHz at 50 Hz, coherent | −34 dB | −51 dB | −66 dB |
| same, 1 Hz Doppler spread | −14.5 dB | −48 dB | −72 dB |
| same, 10 Hz Doppler spread | −0.6 dB | −1.0 dB | −2.6 dB |
| same, random phase per sweep | −0.1 dB | −0.2 dB | −0.5 dB |
| same, two modes 1.5 ms apart, 0.5 Hz spread each | −39 dB | −52 dB | −67 dB |
| 20 kHz at 10 Hz, 1 Hz spread | −6.9 dB | −48 dB | −73 dB |
| 40 kHz at 25 Hz, 1 Hz spread | −14.6 dB | −48 dB | −68 dB |

The fading model is a sum of 24 sinusoids and has no spectral tails, so the 4 Hz and 8 Hz figures under fading are optimistic.

**Cost of the notch to the wanted tone** (rectangular 160 ms tone, analytic):

| `f_r` | Notch | Tone on a line (worst) | Tone midway (best) | Mean over offsets |
| --- | --- | --- | --- | --- |
| 50 Hz | 4 Hz | 3.7 dB | 0.01 dB | 0.36 dB (8.0%) |
| 50 Hz | 8 Hz | 8.6 dB | 0.05 dB | 0.75 dB (15.9%) |
| 40 Hz | 4 Hz | 3.7 dB | 0.03 dB | 0.46 dB (10.0%) |
| 25 Hz | 4 Hz | 3.7 dB | 0.05 dB | 0.75 dB (15.9%) |
| 10 Hz | 4 Hz | 4.3 dB | 0.81 dB | 2.2 dB (39.7%) |

The mean equals `w/f_r`. The worst case matters: a tone that sits on a comb line loses 3.7 dB in that symbol and is distorted. With a keyed pseudo-random tone position the chance that a symbol's tone lies within 3 Hz of a line is `6 Hz/f_r` (12% at 50 Hz), independently from symbol to symbol; a fixed tone set would make it permanent for the unlucky tones.

**Cost of blanking to the wanted tone.** The gate scales the tone's amplitude in its own bin by `1−d` and the noise power by `1−d`, so the loss is `−10·log₁₀(1−d)`: 0.66 dB at 14%, 1.43 dB at 28%, 2.2 dB at 40%, 3.6 dB at 56%. The gate also copies the tone to `±n·f_r` (8 bins away at 50 Hz) at −9.4 dB relative to the blanked tone for `d = 0.28` and −16 dB for `d = 0.14`. Below the noise that image is harmless; for a strong wanted signal it is self-interference in other candidate tones.

## 4. Mitigation literature

### 4.1 Time-domain blanking

- **Published model.** With a blanker of net duty `PDC_B`, the effective noise density is `N₀,eff = N₀·(1 + I₀/N₀ + R_I)/(1 − PDC_B)`, where `R_I` is the average density of pulses that stay below the blanker threshold [P: ITU-R M.2030, eq. 1; the same structure is in Garcia-Pena et al. 2021, who state the `1/(1−bdc)` factor and refine `R_I`]. Loss from blanking alone is `−10·log₁₀(1−d)`.
- **Published model for a receiver that saturates instead of blanking.** `N₀,eff = N₀·(1 + I₀/N₀ + R_I)·(1 + N_LIM²·PDC/(1−PDC))/(1−PDC)`, with `N_LIM` the ratio of the saturation level to the 1σ noise voltage [P: ITU-R M.2030, eq. 5]. Applied here [D]: at 28% duty a hard limiter (`N_LIM = 1`) loses 2.85 dB, a limiter at 3σ loses 8.0 dB and one at 5σ loses 11.7 dB (σ is the per-component noise voltage). A high clip level is therefore harmful when the duty is large, the opposite of the impulsive-noise intuition.
- **Threshold choice.** Closed-form output SNR and an optimal blanking threshold exist for OFDM in impulsive noise, for blanking, clipping and a hybrid [P: Zhidkov 2006, 2008; abstracts only]. Blanking only the affected subcarriers, and choosing the threshold to maximize post-blanking SINR, improve on plain blanking [P: Epple and Schnell 2017; abstract].
- **Clean-noise cost** [D]: a blanker whose envelope threshold is 3 times the per-component noise σ loses 0.27 dB with no interference present, 0.87 dB at 2.5σ and 2.3 dB at 2σ. A clipper loses 0.08 dB at 2σ and 0.5 dB at 1σ. A hard limiter loses `π/4`, 1.05 dB.
- **Loss when the duty is large.** Section 3.3: for the 10–12 kHz radars the duty needed for 20–30 dB suppression is 31–59%, a loss of 1.6–3.9 dB, and suppression beyond 40 dB is not available by blanking at all.
- **Origin.** The aviation pulse-blanking analysis began with Hegarty et al. (2000) [not read].
- **Practice in weak-signal software.** WSJT-X offers a blanker that zeroes a chosen percentage of the largest samples for its slow modes [Mem; the passage was not reached in the fetched guide].

### 4.2 Chirp excision, dechirp-and-notch, adaptive cancellation

- Narrowband interference rejection in spread-spectrum receivers divides into least-mean-square prediction filters and transform-domain excision [P: Milstein 1988; abstract].
- For interferers with a time-varying instantaneous frequency, an open-loop notch that follows the instantaneous frequency estimated from a time-frequency distribution was analysed for swept and hopping jammers, with closed-form SNR improvement [P: Amin 1997; abstract].
- Notch filters steered by a time-frequency detector were developed for GNSS [P: Borio et al. 2008; abstract].
- No loss figure for a narrowband victim that sees only a small part of the sweep was found in the abstracts read. The bounds in section 3.1 item 9 and the notch results in section 3.3 are this review's own.
- A periodic interferer can also be estimated by synchronous averaging over `N` periods and subtracted; the residual noise increase is `1 + 1/N` (0.09 dB for 50 periods) [D]. That is the same operation as a comb notch of width about `f_r/N` and has the same limits from Doppler spread.

### 4.3 Robust metrics without side information

- **Zero-memory nonlinearities as robust estimators.** Treating the interferer as an outlier and passing samples through a nonlinearity derived from a heavy-tailed noise model (complex signum from a Laplacian model, myriad from a Cauchy model, Huber's function) rejects pulsed interference without detecting or estimating it, and the same idea can be applied in a transformed domain where the interferer is sparse [P: Borio 2017; Borio and Closas 2018; Borio, Li and Closas 2018; abstracts]. The clean-noise costs computed here are in 4.1.
- **Locally optimum detection in impulsive noise.** Middleton's Class A and B models and the locally optimum structures for coherent and incoherent reception give large savings in signal power over linear receivers in strongly non-Gaussian noise [P: Middleton 1977; Spaulding and Middleton 1977 Part II; abstracts; no numbers obtained]. Kassam (1988) is the standard text [not read].
- **Limiters.** A band-pass hard limiter in Gaussian noise costs 1.05 dB at low SNR ([D]; the classic result is Davenport 1953). With a strong second signal present, the weak one is suppressed further [P: Jones 1963, abstract; the usual figure of about 6 dB is [Mem]].
- **Per-cell clipping of the energy metric** [D]. Clip each bin's energy at `L` times that bin's mean noise energy. With no interference the loss in integration time is 0.06 dB (`L = 5`), 0.37 dB (`L = 3`), 0.96 dB (`L = 2`), 2.7 dB (`L = 1`). With a fraction `ρ` of cells hit hard at random:

  | `ρ` | Erasure with perfect knowledge | Clip `L = 2` | Clip `L = 3` | Clip `L = 5` | No protection, hits +20 dB |
  | --- | --- | --- | --- | --- | --- |
  | 0.02 | 0.09 dB | 1.3 dB | 0.95 dB | 1.4 dB | 26 dB |
  | 0.05 | 0.22 dB | 1.8 dB | 1.7 dB | 3.0 dB | 30 dB |
  | 0.10 | 0.46 dB | 2.5 dB | 2.9 dB | 4.9 dB | 33 dB |
  | 0.25 | 1.25 dB | 4.6 dB | 5.6 dB | 8.6 dB | 36 dB |
  | 0.40 | 2.2 dB | 6.6 dB | 7.9 dB | 11.2 dB | 38 dB |

  Values are extra integration time; the equivalent received power is half as many dB in the square-law regime. The clipped metric pays because a hit cell in a *wrong* candidate tone still adds a fixed amount, and that adds variance.
- **Per-bin noise normalization from order statistics.** Estimating a bin's noise level by the median of `n` energy samples has 2.08 times the variance of the mean but tolerates up to half the samples being contaminated [D]; at `n = 400` the rms error is 7%. Order-statistic thresholds are the standard radar answer to interfering targets [P: Rohling 1983; not re-read]. For a comb that stays in place for minutes, the per-bin level can be learned from hundreds of symbols, so the receiver can supply its own side information and approach the erasure column.

### 4.4 Erasure insertion and errors-and-erasures decoding

- Reed–Solomon coding with parallel errors-and-erasures decoding and diversity gives good performance in partial-band noise and neutralizes narrowband interference whatever its power or statistics [P: Pursley and Stark 1985; abstract].
- Viterbi's ratio-threshold test attaches a quality bit from the ratio of the largest to the second-largest filter output. It improves coded performance against a tone jammer by up to 6 dB, and makes tone and partial-band jamming almost equal and only slightly worse than full-band noise in a coded system [P: Viterbi 1982; abstract].
- Bayesian erasure insertion outperforms the ratio-threshold test; error probabilities fall by several orders of magnitude relative to errors-only decoding, up to six orders with fading [P: Baum and Pursley 1992, 1997; abstracts].
- Output-threshold, ratio-threshold and joint tests were compared for RS-coded slow-hopped MFSK under multitone jamming and Rayleigh fading [P: Ahmed, Yang and Hanzo 2007; abstract].
- On channels with block interference, capacity *increases* with the memory length while the cutoff rate decreases [P: McEliece and Stark 1984; abstract]. A long, steady interferer is therefore easier in principle than a memoryless one of the same duty, because its state can be learned.

### 4.5 Interleaving against periodic interference

- Periodic and convolutional interleavers are defined in Ramsey (1970) and Forney (1971) [DOIs verified; not re-read]. The standard caution that a periodic interleaver can be defeated by periodic bursts, and that a pseudo-random one should be used against them, is in the coding textbooks [Mem: Clark and Cain 1981].
- Demonstration here [D]: a 64-symbol codeword with regular spacing `D` symbols, examined only for spacings that span at least 10 interferer periods. For a 1 Hz oceanographic sweep, 80 of 2000 spacings lose at least 90% of the codeword at the worst phase (the first is `D = 25`, which is 4 s). For a 3.8 s burst every 20 s, 16 of 2000 do; for 50 s in 90 s, 39; for 120 s in 240 s, 28. The median spacing is harmless (worst-phase loss within 0.03 of the mean), which is why the hazard is easy to miss in testing. Pseudo-random placement gives a 99.9% point of 0.45, 0.36, 0.75 and 0.69 for the same four cases, against means of 0.27, 0.20, 0.56 and 0.50.
- Burst lengths from monitoring set the minimum span: 2.5–10 s bursts are 16–63 symbols; 50 s is 313 symbols; 120 s is 750 symbols.
- Resonance exists in frequency as well. The symbol length equals a whole number of sweeps at 50, 25, 12.5 and 100 Hz (8, 4, 2, 16), and the comb spacing is then a whole number of bins. A tone alphabet with regular spacing would have the same tones hit in every symbol; a keyed uniform position avoids that [D].

### 4.6 Keyed hopping against partial-band and comb interference

- **Uncoded worst case** [D, reproducing the classical result attributed to Houston 1975 and tabulated in the Simon–Omura–Scholtz–Levitt handbook, neither read]. For noncoherent M-ary FSK with one hop per symbol, worst-case partial-band noise gives `P_b = β/(E_b/N_J)` above a threshold `γ`:

  | M | β | γ | `E_b/N_J` for 10⁻⁵, worst case | Same in broadband noise | Loss |
  | --- | --- | --- | --- | --- | --- |
  | 2 | 0.3679 | 3.0 dB | 45.7 dB | 13.4 dB | 32.3 dB |
  | 4 | 0.2330 | 0.8 dB | 43.7 dB | 10.6 dB | 33.1 dB |
  | 8 | 0.1954 | −0.3 dB | 42.9 dB | 9.1 dB | 33.8 dB |
  | 16 | 0.1813 | −1.0 dB | 42.6 dB | 8.1 dB | 34.5 dB |
  | 32 | 0.1759 | −1.4 dB | 42.5 dB | 7.3 dB | 35.1 dB |
  | 64 | 0.1743 | −1.7 dB | 42.4 dB | 6.8 dB | 35.7 dB |

- **Multitone is worse for larger alphabets.** With one jamming tone per M-ary band, `BER = M/(2K·E_b/N_J)`; that is 4.3 dB more effective than partial-band noise at `K = 1` and 10.5 dB at `K = 4` [P: Levitt 1985]. An OTHR comb is a multitone interferer with one line every `f_r`.
- **Diversity with perfect jammer-state information** recovers nearly everything: at 10⁻⁵, optimum diversity gains 35 dB over no diversity for BFSK and ends 1.6 dB worse than broadband noise; under 3.6 dB worse for 4-FSK; at most 7.4 dB worse for 16-FSK; under 4.4 dB worse for 8-FSK against independent multitone [P: Levitt 1985, JPL Publication 84-90, full].
- **Without side information, linear combining is the wrong receiver.** For square-law linear combining with no knowledge of the jamming state, more hops per bit gave *higher* error probability under worst-case partial-band jamming [P: Lee, French and Miller 1984]. Conventional linear combining was found far from optimum in jamming [P: Gong 1983]. The order in which jammer and communicator optimize also matters; the usual solution is not a saddle point [P: Levitt 1985, "Strategies"].
- **Simple nonlinear weighting restores the diversity gain.** Per-hop adaptive gain control or soft limiting before combining gives a diversity gain when the reduction in jamming power outweighs the noncoherent combining loss [P: Lee, Miller and Kim 1984; Lee, Miller and French 1985]. Noise-normalization combining with diversity greatly reduces partial-band degradation in Rician fading, and coding adds to it [P: Robertson and Ha 1992]. A self-normalizing receiver that measures nothing was reported to perform nearly as well as one given the noise-plus-jamming powers [P: DTIC report ADA172929, snippet only].
- **Coded limits.** Capacity and cutoff rate under worst-case partial-band interference were computed for soft and hard decisions with and without side information [P: Stark 1985 Parts I and II; abstracts only, no numbers obtained]. Most of the jammer's advantage is recovered by coding and diversity [P: Viterbi and Jacobs 1975, as summarized by Levitt 1985].
- **What does not transfer.** Classical hopping spreads over a band much wider than the jammer can fill. Here the hop range is inside the interferer. The transferable parts are the receiver lessons: normalize or limit before combining, mark erasures, and code across the hits.

### 4.7 A measured data point for a conventional modem

A 9600 bit/s, 64-QAM, long-interleaver HF modem in 3 kHz was tested against simulated gated oceanographic radar sweeps on a non-fading channel. With the radar's *peak* at 6 dB below the noise, the bit error ratio moved from 10⁻⁵ to under 3×10⁻⁵. With the radar's *average* at 6 dB below the noise, it rose to 10⁻¹. The report concludes that degradation must not be scaled by the bandwidth ratio [M: ITU-R M.2234, Attachment 11]. The mean-density rule in section 3.1 item 1 is therefore a floor on the damage, valid only for a receiver whose metric cannot be captured by individual strong cells.

## 5. Receiver front end

- **AGC.** Weak-signal software guidance is to turn AGC off or reduce RF gain so that AGC does not act, and to set the no-signal audio level to about 30 dB on the program's level meter [P: WSJT-X 2.7 User Guide, full for this section]. Reasons specific to OTHR [D]: a fast AGC follows each 3–14 ms pulse and modulates the gain at `f_r`, which puts sidebands on the wanted tone at `±f_r` and corrupts energy normalization; a slow AGC holds the gain down between pulses, which is harmless to SNR only while the receiver's own noise and quantization stay below the reduced band noise.
- **ADC over-range.** A fast-rising signal can over-range the ADC before a DSP-derived AGC acts, and the receiver can stay driven at full scale until it ends [P: Farson, full]. With the level set as above, a 16-bit path has about 60 dB of headroom over the noise (the meter's full scale is about 90 dB for 16-bit audio [Mem]); a radar 51–66 dB above the noise (section 1.3) is at or beyond it [D].
- **Noise blankers.** An analogue pulse-gating blanker degrades the wanted signal when a strong signal is inside its wide bandwidth, because the gating edges splatter; a DSP blanker must be set so that it does not gate on strong signals outside the passband; placing the blanker ahead of the narrow filters avoids pulse stretching [P: Farson]. Special blankers were built into receivers for the 10 Hz pulsed Duga radar, whose pulses were longer than ordinary blankers handled [A: Wikipedia; Ham Radio Today 1983, snippet].
- **Why a transceiver's blanker does not help against FMCW OTHR** [D]. Ahead of the SSB filter the radar is a constant-envelope carrier sweeping through the blanker's passband; there are no pulses to detect. The pulses exist only after a filter narrower than the sweep, and after that filter they have already been stretched (section 3.3).
- **Recommendation supported by the above.** Fixed gain, the widest linear filter available, at least 16 bits with the noise 20–30 dB above the quantization floor (24 bits or a direct-sampling receiver with a wider IQ output is better), no transceiver noise blanker or noise reduction, and all gating done in the modem where its effect on the metric is known.

## 6. How much margin OTHR tolerance plausibly consumes

All values in this section are [D]. "Time dB" is extra integration time; the equivalent received power is half as many dB in the square-law regime and the same number of dB where the metric is linear in power.

### 6.1 Fraction of time-frequency cells lost

| Radar | Native cells, no pre-processing | Best practical excision | Fraction removed | Basis |
| --- | --- | --- | --- | --- |
| 10 kHz at 50 Hz (also 12 kHz at 40 Hz, Foghorn) | 100%; half the bins within 20 dB of the mean even with a Hann window | Comb notch of 4–8 Hz before the symbol transform | 8–20% | `w/f_r`; residual −48 to −66 dB at ν ≤ 1 Hz |
| same, comb unusable (ν ≥ 10 Hz or incoherent sweeps) | 100% | Blanking | 39% for 20 dB, 59% for 30 dB | simulation |
| 20 kHz at 10 Hz | 100% | Blanking | 15% for 20 dB, 19% for 30 dB, 23% for 40 dB | simulation |
| 40 kHz at 25 Hz | 100% | Comb notch 4 Hz, or blanking | 16%, or 12–31% | both |
| 160 kHz at 10 Hz | 100% | Blanking | 3.7% nominal | `d_t+g`; not simulated; `N_tb` = 4.9, so deep suppression will need more |
| 10 kHz at 7 Hz | 100% | Blanking | about 29–31% | `d_t+g`; `N_tb` = 112 |
| 4 kHz at 25 Hz | 100% | Comb notch 4 Hz | 16% | blanking would need 70% |
| CODAR 25.6 kHz at 1 Hz, ungated | 27% of symbols | Blanking | 10–11% | simulation |
| CODAR 25.6 kHz at 1 Hz, gated | about 30% of cells within 10 dB of the mean, 80% within 20 dB | Blanking of the main crossing only | 8% removes 90%; deeper suppression is impractical | simulation |
| Chirp ionosonde | under 0.1% of symbols | Erase the symbol | negligible | 28 ms per 5–30 min |

### 6.2 Extra energy needed

| Handling | Typical single OTHR, quiet path | Harder documented cases |
| --- | --- | --- |
| (a) Hit cells excised or erased with perfect side information | 0.3–1.0 dB (8–20% removed) | 1.2–1.6 dB (24–31% removed: 7–10 Hz sweeps of 10 kHz; three simultaneous 40–50 Hz combs); 1.6–3.9 dB when only blanking works on a 10–12 kHz radar (31–59% removed) |
| (b) Robust metric with no side information | 2.5–4.6 dB of time (1.3–2.3 dB of power) for 10–25% hit, clipped energy at `L = 2` | 6.6–9.3 dB of time for 40–60% hit; a hard limiter in the time domain at 28% duty costs 2.85 + 1.05 = 3.9 dB |
| (c) Not handled | `10·log₁₀(1 + ρ_W·W/B)`: 5.8 dB for a 10 kHz radar 10 dB above the passband noise; 14.6 dB at 20 dB; 24.5 dB at 30 dB; 44.5 dB at 50 dB | For a 160 kHz radar the same levels cost 0.7, 4.4, 12.7 and 32.4 dB |

**The radar level that each margin option absorbs if nothing is done** (`ρ_W`, dB above the noise in 2.8 kHz):

| Margin | 4 kHz radar | 10 kHz | 20 kHz | 40 kHz | 160 kHz |
| --- | --- | --- | --- | --- | --- |
| 3 dB | 1.5 | 5.5 | 8.5 | 11.5 | 17.5 |
| 6 dB | 6.3 | 10.3 | 13.3 | 16.3 | 22.3 |
| 10 dB | 11.1 | 15.1 | 18.1 | 21.1 | 27.1 |
| 13 dB | 14.3 | 18.3 | 21.3 | 24.3 | 30.3 |
| 16 dB | 17.4 | 21.4 | 24.4 | 27.4 | 33.5 |
| 20 dB | 21.5 | 25.5 | 28.5 | 31.5 | 37.5 |

Against the levels in section 1.3 (23–66 dB above the noise for S9 to S9+20 dB radars), the margin table cannot be the defence against a strong radar. Its role is narrower:

- With excision that works, reserve about 1 dB for a single radar and about 3–4 dB for the harder documented cases. Every margin option can afford the first; the 3 dB option cannot afford the second at all.
- The required suppression is `ρ_W·W/B` down to a residual well under the noise: 45–60 dB for an S9+20 dB, 12 kHz radar on a quiet 14 MHz band. The ideal 4 Hz notch reached 48–54 dB and the 8 Hz notch 66–73 dB in simulation, so the strongest logged radars need the wider notch (16–20% removed, 0.75–1.0 dB) or leave a residual rise.
- A weak radar, one that a listener would barely notice, is the case the margin does cover: 5 dB above the passband noise costs 2.75 dB for a 10 kHz sweep if ignored.

## 7. Recent work, 2018–2026

- **No measurement of OTHR impact on FT8, WSPR or other amateur weak-signal modes was found**, in academic literature, IARU material, or developer documentation. The nearest evidence is circumstantial: radars logged across 7050–7075 kHz, which includes the 40 m FT8 channel, for hours [A: IARU newsletter 2026-10].
- **OTHR activity has not declined.** The newsletters read from December 2021 to October 2026 all keep radars as the leading intruder; newer entries include 160–360 kHz Chinese sweeps, a 2025 burst variant resembling the 29B6, and faster Iranian hopping (every 31 s) with a new 695 pps rate in 2024 [V]. An Indian system was reported under construction in 2021 [A: ARRL].
- **Radar waveforms may stop being linear FM.** Constant-modulus periodic FSK, Costas and noise-like waveforms meeting the US spectral mask have been proposed for OTHR against hypersonic targets [P: Abramovich et al. 2022]. Time blanking and dechirping would not work against them; a comb at the repetition frequency would remain.
- **Frequency management is becoming more adaptive.** A Canadian OTHR frequency management system merges a spectrum monitor with an assimilative high-latitude ionosphere model and was used in trials [P: Thayaparan et al. 2023; abstract]. Network-level performance models for skywave radar exist [P: Francis et al. 2017; Cervera et al. 2018]. More agile radars mean shorter stays on one channel, which helps interleaving and hinders learned per-bin statistics.
- **Robust interference mitigation without estimating the interferer** is the main new idea since the older literature: zero-memory nonlinearities derived from robust statistics, in the time domain or a transform domain, validated on real jammed GNSS recordings [P: Borio 2017; Borio and Closas 2018; Borio, Li and Closas 2018]. It supports limiter-type and clipped metrics as a no-side-information fallback, at the costs computed in section 4.
- **Pulse-blanking models were tightened.** The widely used aviation formula assumes the non-blanked interference is spectrally flat after blanking; a 2021 analysis replaces that with the true post-blanker spectrum and agrees with simulation to 0.1–0.15 dB [P: Garcia-Pena et al. 2021].
- **Treating structured interference as Gaussian noise of equal power overestimates the error rate of a chirp-spread-spectrum receiver** [P: Huang and Lahoud 2025]. That is consistent with section 3: structure is exploitable. The ITU-R experiment in 4.7 shows the opposite for a receiver that does not exploit it. Both statements are true; they describe different receivers.
- **Not found:** any 2018–2026 paper on FMCW radar interference into LoRa or MFSK receivers specifically, or on radar–communication coexistence at HF beyond the oceanographic-radar ITU-R material of 2010–2014.

## Design consequences

Written as statements a simulator or bench test can pass or fail. "Reference radars" are: 10 kHz at 50 Hz, 12 kHz at 40 Hz, 10 kHz at 66.67 Hz in 3.8 s bursts every 20 s, 20 kHz at 10 Hz, 40 kHz at 25 Hz, 160 kHz at 10 Hz, 10 kHz at 7 Hz, 4 kHz at 25 Hz, and gated 25.6 kHz at 1 Hz; each coherent, with 0.1, 1 and 10 Hz Doppler spread, and with random phase per sweep.

**Waveform and schedule**

1. With any coherent reference radar present at 40 dB above the passband noise and Doppler spread at most 1 Hz, the fraction of symbols whose wanted tone lies within 3 Hz of a radar line must match `6 Hz/f_r` within sampling error, and must show no dependence on symbol index, message bit or codeword position. (Tests that the keyed tone position is uniform and has no structure commensurate with 6.25, 12.5, 25 or 50 Hz.)
2. No candidate-tone set used for one symbol may be an arithmetic progression with spacing equal to 2, 4, 8 or 16 bins.
3. Hopping inside the passband must not be credited with any gain against an interferer wider than the passband in any link budget or margin table.

**Receiver processing before the symbol transform**

4. A periodic-interference canceller (comb notch or synchronous subtraction) operating on at least 1 s of coherent data must reduce a coherent 10 kHz, 50 Hz radar with 1 Hz Doppler spread by at least 40 dB while removing no more than 10% of the wanted-signal energy averaged over tone positions.
5. A time gate operating after band-limiting must reduce a coherent 20 kHz, 10 Hz radar by at least 30 dB with at most 20% of samples gated, and must not be engaged for a radar whose required duty exceeds a configured limit (suggested 30%).
6. The receiver must choose between items 4 and 5 from measured periodicity and duty, and must report which it used. With both a 50 Hz comb radar and a 10 Hz pulse radar present, total removed energy must stay under 35%.
7. With excision switched on and no interference present, sensitivity must not fall by more than 0.1 dB. (Tests false triggering.)
8. With a radar 60 dB above the passband noise the sample path must not clip, or, if it does, the loss must not exceed the blanking loss for the clipped duty by more than 0.5 dB.

**Metric**

9. Tone energies must be normalized by a per-bin noise estimate that is robust to at least 30% contaminated samples (median or other order statistic) and by a per-time estimate that is robust to at least 30% contaminated bins.
10. The per-symbol soft metric must have bounded influence: a single cell at +60 dB must change a codeword's accumulated metric by no more than a cell at a configured clip level (suggested 2–3 times the bin's noise mean when no side information is available, higher when the cell is known clean).
11. With 25% of cells hit at +40 dB at random and only the bounded-influence metric of item 10 as protection, required integration time must rise by no more than 5 dB; with those cells flagged as erasures, by no more than 1.5 dB. (The derived values are 4.6 dB and 1.25 dB.)
12. Adding diversity or integration time under interference must never increase the error probability. (Tests for the linear-combining failure of Lee, French and Miller.)
13. With the random-phase 10 kHz, 50 Hz radar, for which neither comb nor native-resolution sparsity exists, the receiver must fall back to gating or to treating the radar as a noise rise, and must not produce false decodes.

**Interleaver and code**

14. Codeword symbol positions must be pseudo-random in time over the whole message, not regularly spaced. For interferer periods of 1 s, 20 s, 90 s, 240 s and 300 s at every phase, the worst hit fraction of any codeword must stay below the 99.9% binomial point for the mean hit fraction (0.45, 0.36, 0.75, 0.69, 0.02 for the cases in section 4.5).
15. The interleaver span must be at least 10 times the longest burst it is meant to bridge: 100 s for 10 s bursts, 15 minutes for 50 s in 90 s, 40 minutes for 120 s in 240 s.
16. A radar present continuously for 2 hours at 40 dB above the noise must cost no more than the excision loss; the code must not be relied on to bridge it.
17. The decoder must accept erasures, and the receiver must supply them for symbols whose excised fraction exceeds a threshold.

**Margin table**

18. The margin table must carry an explicit OTHR line: about 1 dB with excision for one radar, about 3–4 dB for the pessimistic multi-radar or blanking-only case, and "not covered" without excision.
19. For each margin option the documentation must state the radar level it absorbs with excision off, using the table in section 6.2 (for example 5.5 dB above the passband noise for the 3 dB option and 25.5 dB for the 20 dB option, for a 10 kHz sweep).
20. Acceptance tests must be run at radar levels of 10, 30 and 60 dB above the passband noise, because the failure modes differ: the first tests the margin, the second the metric, the third the excision depth and the sample path.

**Operating guidance**

21. Receiver setup guidance must say: AGC off or held, transceiver noise blanker and noise reduction off, widest filter, level set for 20–30 dB of noise above the quantization floor.

## Weaknesses and unknowns

- **No occupancy probability.** The fraction of time an OTHR occupies a given amateur-band channel is not known from any source found. Every "typical" in section 6 is conditional on a radar being present.
- **Volunteer data.** Sweep rates and bandwidths from IARU monitors are consistent across years and observers, but levels are S-meter readings, attribution to a named radar is by the monitor, and the service covers Region 1 only. Asia–Pacific and the Americas are under-represented; the Chinese wideband radar is said to be able to cover the whole 40 m band in the Far East [A: ARRL 2021].
- **Coherence of real received OTHR signals was not measured here.** The comb argument rests on the radar's need for coherent dwells of 1–40 s [P] and on project knowledge of mid-latitude Doppler spread. No recording of an OTHR signal was analysed. Line width, phase noise, dwell boundaries, sweep-rate changes between bursts and the "FM on pulse" duty of the 29B6 are unknown.
- **The comb-notch simulation is optimistic**: exact line frequencies, one 2.56 s record, a fading model without spectral tails, no noise, no wanted signal present, one radar.
- **The blanking simulation used two filter shapes only.** The duty needed for deep suppression depends on the filter; the numbers are indicative.
- **The clipped-metric formula** assumes hits are independent between candidate tones, saturate fully, and that the per-bin noise mean is known. It is a deflection calculation at very low SNR, not a coded error rate.
- **The dechirp bound** assumes a single mode and known sweep parameters; it was not simulated.
- **Parameters for JORN, ROTHR, Nostradamus, naval surface-wave radars and pulse ionosondes** are generic, hobbyist or missing. The primary texts were not accessible.
- **Iranian waveform type is uncertain** (the monitor's label is amplitude modulation on pulse). If it is unmodulated pulses, the comb spacing of 150–870 Hz still makes it benign after per-bin normalization, but that was not tested.
- **Future non-LFM radar waveforms** would invalidate the time-domain half of this review.
- **Simultaneous radars** (up to 5 logged) were handled only by adding fractions.
- **Symbol and dwell assumptions.** Calculations use 160 ms symbols and 6.25 Hz bins. A longer dwell narrows the bins and makes the comb sparser at native resolution; that case was not computed.
- **Classical anti-jam numbers** for coded systems without side information (Stark 1985) were not obtained; only abstracts were read.

## What this adds to or contradicts in the skimmed research

1. **Clipping or blanking impulses before the transform is not sufficient for OTHR, and a clipper can be harmful.** The earlier guidance came from impulsive noise with small duty. OTHR in a 2.8 kHz channel has 7–35% duty with long tails; a clipper at 3σ leaves an 8 dB loss at 28% duty [D from P]; blanking needs 39–59% of samples for the common radars [D].
2. **Hopping does not mitigate OTHR.** The earlier table lists hopping among the mitigations for interfered noise. Within a 2.2–2.8 kHz passband, hopping cannot leave a 10–160 kHz sweep. Its value is limited to randomizing comb hits.
3. **Treating bins with narrowband carriers as erasures does not work at native resolution.** Comb lines are 1.1–10.7 bins apart; with a rectangular 160 ms window no bin is clean, and with a Hann window at most half are. Bin erasure needs a comb filter ahead of the symbol transform, and fails altogether below about 12.5 Hz repetition.
4. **The expectation that a Gaussian-noise bound is conservative for a fixed noise power** remains reasonable as an information statement but is misleading as a margin statement: the unhandled loss is `1 + ρ_W·W/B` with `ρ_W` up to 50–65 dB.
5. **Interleaving guidance of at least 30 minutes for fading** does not bridge a persistent radar (hours), and regular interleavers have exact resonances with burst radars and 1–2 Hz oceanographic sweeps [D].
6. **Occupancy statistics**: the earlier negative result is confirmed. Counts of reports exist; probabilities do not.
7. **OTHR coherent dwell**: the earlier order of magnitude (many seconds) is now 0.7–20.5 s [P: FAS] and 1–40 s [P: Francis et al.], with WRF 2–60 Hz and bandwidth 5–50 kHz.
8. **New**: radars select channels that look clear, and a sub-noise modem looks clear.

## References

Verification: **[verified]** means the DOI resolved at Crossref (api.crossref.org) to the stated authors and title during this session, or the stated URL was fetched and read. Access level follows.

### OTHR and HF radar systems

- Abramovich, Y., Dickey, D., Abramovich, V. (2022). Spectrally limited periodic waveforms for HF OTHR applications. arXiv:2211.03640. <https://arxiv.org/abs/2211.03640> [verified] [full]
- Bazin, V., Molinie, J. P., Munoz, J., Dorey, P., Saillant, S., Auffray, G., et al. (2006). Nostradamus: an OTH radar. *IEEE Aerospace and Electronic Systems Magazine* 21(10), 3–11. <https://doi.org/10.1109/MAES.2006.275299> [verified] [title only]
- Bazin, V., et al. (2006). A general presentation about the OTH-radar NOSTRADAMUS. *2006 IEEE Conference on Radar*, 634–642. <https://doi.org/10.1109/RADAR.2006.1631867> [verified] [title only]
- Berngardt, O. I., Voronov, A. L., Grkovich, K. V. (2015). Optimal signals of Golomb ruler class for spectral measurements at EKB SuperDARN radar: theory and experiment. *Radio Science* 50(6), 486–500. <https://doi.org/10.1002/2014RS005589>; arXiv:1409.6053 <https://arxiv.org/abs/1409.6053> [verified] [abstract; pulse length from a snippet]
- Cervera, M. A., Francis, D. B., Frazer, G. J. (2018). Climatological model of over-the-horizon radar. *Radio Science* 53(9), 988–1001. <https://doi.org/10.1029/2018RS006607> [verified] [abstract]
- Chisham, G., Lester, M., Milan, S. E., Freeman, M. P., Bristow, W. A., Grocott, A., et al. (2007). A decade of the Super Dual Auroral Radar Network (SuperDARN). *Surveys in Geophysics* 28(1), 33–109. <https://doi.org/10.1007/s10712-007-9017-8> [verified] [title only]
- Earl, G. F., Ward, B. D. (1987). The frequency management system of the Jindalee over-the-horizon backscatter HF radar. *Radio Science* 22(2), 275–291. <https://doi.org/10.1029/RS022i002p00275> [verified] [abstract]
- Federation of American Scientists (page dated 1999). AN/FPS-118 Over-The-Horizon-Backscatter (OTH-B) radar. <https://nuke.fas.org/guide/usa/airdef/an-fps-118.htm> [verified] [full]
- Francis, D. B., Cervera, M. A., Frazer, G. J. (2017). Performance prediction for design of a network of skywave over-the-horizon radars. *IEEE Aerospace and Electronic Systems Magazine* 32(12), 18–28. <https://doi.org/10.1109/MAES.2017.170056> [verified DOI] [abstract; the 5–50 kHz, 2–60 Hz, 1–40 s figures are from a search excerpt of the full text, which returned HTTP 403]
- Gurgel, K.-W., Antonischki, G., Essen, H.-H., Schlick, T. (1999). Wellen Radar (WERA): a new ground-wave HF radar for ocean remote sensing. *Coastal Engineering* 37(3–4), 219–234. <https://doi.org/10.1016/S0378-3839(99)00027-7> [verified] [title only]
- Gurgel, K.-W., Barbin, Y., Schlick, T. (2007). Radio frequency interference suppression techniques in FMCW modulated HF radars. *OCEANS 2007 – Europe*. <https://doi.org/10.1109/OCEANSE.2007.4302289> [verified] [abstract]
- Headrick, J. M., Skolnik, M. I. (1974). Over-the-horizon radar in the HF band. *Proceedings of the IEEE* 62(6), 664–673. <https://doi.org/10.1109/PROC.1974.9506> [verified] [abstract]
- Headrick, J. M., Thomason, J. F. (1998). Applications of high-frequency radar. *Radio Science* 33(4), 1045–1054. <https://doi.org/10.1029/98RS01013> [verified] [abstract]
- Li, L.-W. (1998). High-frequency over-the-horizon radar and ionospheric backscatter studies in China. *Radio Science* 33(5), 1445–1458. <https://doi.org/10.1029/98RS01606> [verified] [abstract]
- Lindenfeld, M. J. (2004). Sparse frequency transmit-and-receive waveform design. *IEEE Transactions on Aerospace and Electronic Systems* 40(3), 851–861. <https://doi.org/10.1109/TAES.2004.1337459> [verified] [abstract]
- McNeal, G. D. (1997). Bandwidth occupancy profile of mid-Atlantic HF band users. *Radio Science* 32(5), 2037–2045. <https://doi.org/10.1029/97RS01030> [verified] [abstract]
- Pederick, L. H., Cervera, M. A. (2016). Modeling the interference environment in the HF band. *Radio Science* 51(2), 82–90. <https://doi.org/10.1002/2015RS005856> [verified] [abstract; full text returned HTTP 403]
- Ponsford, A. M., Wang, J. (2010). A review of high frequency surface wave radar for detection and tracking of ships. *Turkish Journal of Electrical Engineering and Computer Sciences*. <https://doi.org/10.3906/elk-0912-331> [verified] [abstract]
- Reinisch, B. W., Galkin, I. A., Khmyrov, G. M., Kozlov, A. V., Bibl, K., Lisysyan, I. A., et al. (2009). New Digisonde for research and monitoring applications. *Radio Science* 44, RS0A24. <https://doi.org/10.1029/2008RS004115> [verified] [abstract]
- Thayaparan, T., Villeneuve, H., Chiu, S. (2023). Over-the-horizon radar frequency management system using the Assimilation Canadian High Arctic Ionospheric Model (A-CHAIM). *2023 IEEE International Radar Conference*. <https://doi.org/10.1109/RADAR54928.2023.10371117> [verified] [abstract]
- Thomason, J. F. (2003). Development of over-the-horizon radar in the United States. *Proceedings of the International Conference on Radar*, 599–601. <https://doi.org/10.1109/RADAR.2003.1278809>; copy read at <https://www.sigidwiki.com/images/e/ea/Thomason_re_OTHR_0903.pdf> [verified] [full; no waveform repetition numbers in it]

### ITU-R documents

- ITU-R Recommendation M.1874-1 (02/2013). Technical and operational characteristics of oceanographic radars operating in sub-bands within the frequency range 3–50 MHz. <https://www.itu.int/rec/R-REC-M.1874/en> [verified] [full]
- ITU-R Report M.2234 (2011). The feasibility of sharing sub-bands between oceanographic radars operating in the radiolocation service and fixed and mobile services within the frequency band 3–50 MHz. <https://www.itu.int/pub/R-REP-M.2234> [verified] [full for the sections cited: main text, Attachments 8, 10, 11]
- ITU-R Report M.2321 (2014). Guidelines for the use of spectrum by oceanographic radars in the frequency range 3 to 50 MHz. <https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-M.2321-2014-PDF-E.pdf> [verified] [sweep-rate passages only]
- ITU-R Recommendation M.2030 (12/2012). Evaluation method for pulsed interference from relevant radio sources other than in the radionavigation-satellite service to the radionavigation-satellite service systems and networks operating in the 1 164–1 215 MHz, 1 215–1 300 MHz and 1 559–1 610 MHz frequency bands. <https://www.itu.int/dms_pubrec/itu-r/rec/m/R-REC-M.2030-0-201212-I!!PDF-E.pdf> [verified] [full for Annex 1 section 2]
- WRC-12 Resolution 612 (Rev. WRC-12). Use of the radiolocation service between 3 and 50 MHz to support oceanographic radar operations. <https://www.itu.int/en/ITU-R/terrestrial/fmd/Documents/Res%20612.pdf> [unverified; search excerpt only]

### Monitoring reports and other non-academic sources

- IARU Region 1 Monitoring System wiki, "Radars". <https://www.iaru-r1.org/spectrum/monitoring-system/iarums-wiki/radars/> [verified] [full]
- IARU Region 1 Monitoring System Newsletter, December 2021. <https://mrasz.hu/images/monitoring/IARUMS-Newsletter-2021-12.pdf> [verified] [full]
- IARU Region 1 Monitoring System Newsletter, July 2024. <https://mrasz.hu/images/monitoring/IARUMS-R1-Newsletter-2024-07.pdf> [verified] [full]
- IARU Intruder Watch Service Newsletter, September 2025. <https://www.iaru-r1.org/wp-content/uploads/2025/10/IARU-IWS-Newsletter-2025-09.pdf> [verified] [full; charts are images and were not read]
- IARU Intruder Watch Service Newsletter, October 2026. <https://www.iaru-r1.org/wp-content/uploads/2026/11/IARU-IWS-Newsletter-2026-10.pdf> [verified] [full; charts not read]
- ARRL News (2021-12-15). Observations of over-the-horizon radar interference in ham bands top all others. <https://www.arrl.org/news/observations-of-over-the-horizon-radar-interference-in-ham-bands-top-all-others> [verified] [full]
- ARRL News (2017-02-13). "Mysterious Foghorn" is Chinese over-the-horizon burst radar. <https://www.arrl.org/news/mysterious-foghorn-is-chinese-over-the-horizon-burst-radar> [verified] [full]
- ARRL News (2018-06-11). Iranian radars showing up on 10 meters. <https://www.arrl.org/news/iranian-radars-showing-up-on-10-meters> [verified] [full]
- Signal Identification Wiki. Relocatable Over-the-Horizon Radar (ROTHR). <https://sigidwiki.com/wiki/Relocatable_Over-the-Horizon_Radar_(ROTHR)> [verified] [full; hobbyist, values not individually sourced]
- Wikipedia. Duga radar. <https://en.wikipedia.org/wiki/Duga_radar> [verified] [full; tertiary]
- Greenman, M., Martinez, P. (1999–2009). Passive chirp sounding. <https://www.qsl.net/zl1bpu/IONO/chirps.htm> [verified] [full]
- Farson, A. M. (2010, updated 2018). AGC and noise blanker in an IF-DSP receiver. <https://www.qsl.net/ab4oj/icom/ic7700/agc_nb.html> [verified] [full]
- WSJT-X 2.7.0 User Guide, section 5 "Transceiver Setup". <https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.html> [verified] [section 5 only; the noise-blanker passage was not reached]

### Spectrum occupancy

- Haralambous, H., Mostafa, M. G. HF spectral occupancy over the eastern Mediterranean. Ionospheric Effects Symposium 2015 paper. <https://sites.bc.edu/wp-content/uploads/sites/196/2015/05/065-Haralambous-Paper.pdf> [verified] [full]
- Mostafa, M. G., Tsolaki, E., Haralambous, H. (2017). HF spectral occupancy time series models over the eastern Mediterranean region. *IEEE Transactions on Electromagnetic Compatibility* 59(1), 240–248. <https://doi.org/10.1109/TEMC.2016.2606130> [verified] [abstract]

### Interference mitigation, robust detection, blanking

- Amin, M. G. (1997). Interference mitigation in spread spectrum communication systems using time-frequency distributions. *IEEE Transactions on Signal Processing* 45(1), 90–101. <https://doi.org/10.1109/78.552208> [verified] [abstract]
- Borio, D. (2017). Myriad non-linearity for GNSS robust signal processing. *IET Radar, Sonar and Navigation* 11(10), 1467–1476. <https://doi.org/10.1049/iet-rsn.2016.0610> [verified] [title only]
- Borio, D., Closas, P. (2018). Complex signum non-linearity for robust GNSS interference mitigation. *IET Radar, Sonar and Navigation* 12(8), 900–909. <https://doi.org/10.1049/iet-rsn.2017.0552> [verified] [abstract]
- Borio, D., Li, H., Closas, P. (2018). Huber's non-linearity for GNSS interference mitigation. *Sensors* 18(7), 2217. <https://doi.org/10.3390/s18072217> [verified] [abstract]
- Borio, D., Camoriano, L., Savasta, S., Lo Presti, L. (2008). Time-frequency excision for GNSS applications. *IEEE Systems Journal* 2(1), 27–37. <https://doi.org/10.1109/JSYST.2007.914914> [verified] [abstract]
- Davenport, W. B. (1953). Signal-to-noise ratios in band-pass limiters. *Journal of Applied Physics* 24(6), 720–727. <https://doi.org/10.1063/1.1721365> [verified] [title only]
- Epple, U., Schnell, M. (2017). Advanced blanking nonlinearity for mitigating impulsive interference in OFDM systems. *IEEE Transactions on Vehicular Technology* 66(1). <https://doi.org/10.1109/TVT.2016.2535374> [verified] [abstract]
- Garcia-Pena, A., Julien, O., Macabiau, C., Mabilleau, M., Durel, P. (2021). GNSS C/N0 degradation model in presence of continuous wave and pulsed interference. *NAVIGATION* 68(1), 75–91. <https://doi.org/10.1002/navi.405> [verified] [full text page; equations were images]
- Hegarty, C., Van Dierendonck, A. J., Bobyn, D., Tran, M., Kim, T., Grabowski, J. (2000). Suppression of pulsed interference through blanking. Proceedings of the IAIN World Congress / ION Annual Meeting. [unverified; found only as a third-party copy; not read]
- Huang, J., Lahoud, S. (2025). Physical-layer analysis of LoRa robustness in the presence of narrowband interference. arXiv:2512.01088 <https://arxiv.org/abs/2512.01088>; MSWiM 2025, <https://doi.org/10.1109/MSWiM67937.2025.11308886> [verified] [abstract]
- Jones, J. J. (1963). Hard-limiting of two signals in random noise. *IEEE Transactions on Information Theory* 9(1), 34–42. <https://doi.org/10.1109/TIT.1963.1057797> [verified] [abstract]
- Kassam, S. A. (1988). *Signal Detection in Non-Gaussian Noise*. Springer. <https://doi.org/10.1007/978-1-4612-3834-8> [verified] [not read]
- Klauder, J. R., Price, A. C., Darlington, S., Albersheim, W. J. (1960). The theory and design of chirp radars. *Bell System Technical Journal* 39(4), 745–808. <https://doi.org/10.1002/j.1538-7305.1960.tb03942.x> [verified] [not re-read]
- Middleton, D. (1977). Statistical-physical models of electromagnetic interference. *IEEE Transactions on Electromagnetic Compatibility* EMC-19(3), 106–127. <https://doi.org/10.1109/TEMC.1977.303527> [verified] [title only]
- Milstein, L. B. (1988). Interference rejection techniques in spread spectrum communications. *Proceedings of the IEEE* 76(6), 657–671. <https://doi.org/10.1109/5.4455> [verified] [abstract]
- Rohling, H. (1983). Radar CFAR thresholding in clutter and multiple target situations. *IEEE Transactions on Aerospace and Electronic Systems* AES-19(4), 608–621. <https://doi.org/10.1109/TAES.1983.309350> [verified] [title only]
- Spaulding, A. D., Middleton, D. (1977). Optimum reception in an impulsive interference environment—Part II: incoherent reception. *IEEE Transactions on Communications* 25(9), 924–934. <https://doi.org/10.1109/TCOM.1977.1093942> [verified] [abstract]
- Zhidkov, S. V. (2006). Performance analysis and optimization of OFDM receiver with blanking nonlinearity in impulsive noise environment. *IEEE Transactions on Vehicular Technology* 55(1), 234–242. <https://doi.org/10.1109/TVT.2005.858191> [verified] [abstract]
- Zhidkov, S. V. (2008). Analysis and comparison of several simple impulsive noise mitigation schemes for OFDM receivers. *IEEE Transactions on Communications* 56(1), 5–9. <https://doi.org/10.1109/TCOMM.2008.050391> [verified] [abstract]

### Frequency-hopped MFSK under partial-band and multitone interference; erasures; interleaving

- Ahmed, S., Yang, L.-L., Hanzo, L. (2007). Erasure insertion in RS-coded SFH MFSK subjected to tone jamming and Rayleigh fading. *IEEE Transactions on Vehicular Technology* 56(6), 3563–3571. <https://doi.org/10.1109/TVT.2007.901861> [verified] [abstract]
- Baum, C. W., Pursley, M. B. (1992). Bayesian methods for erasure insertion in frequency-hop communication systems with partial-band interference. *IEEE Transactions on Communications* 40(7), 1231–1238. <https://doi.org/10.1109/26.153368> [verified] [abstract]
- Baum, C. W., Pursley, M. B. (1997). Erasure insertion in frequency-hop communications with fading and partial-band interference. *IEEE Transactions on Vehicular Technology* 46(4), 949–956. <https://doi.org/10.1109/25.653069> [verified] [abstract]
- Forney, G. D. (1971). Burst-correcting codes for the classic bursty channel. *IEEE Transactions on Communication Technology* 19(5), 772–781. <https://doi.org/10.1109/TCOM.1971.1090719> [verified] [not re-read]
- Gong, K. S. (1983). Performance of diversity combining techniques for FH/MFSK in worst case partial band noise and multi-tone jamming. *MILCOM 1983*, 17–21. <https://doi.org/10.1109/MILCOM.1983.4794654> [verified] [abstract]
- Lee, J. S., French, R. H., Miller, L. E. (1984). Probability of error analyses of a BFSK frequency-hopping system with diversity under partial-band jamming interference—Part I: performance of square-law linear combining soft decision receiver. *IEEE Transactions on Communications* 32(6), 645–653. <https://doi.org/10.1109/TCOM.1984.1096128> [verified] [abstract]
- Lee, J. S., Miller, L. E., Kim, Y. K. (1984). Same title—Part II: performance of square-law nonlinear combining soft decision receivers. *IEEE Transactions on Communications* 32(12), 1243–1250. <https://doi.org/10.1109/TCOM.1984.1096018> [verified] [abstract]
- Lee, J. S., Miller, L. E., French, R. H. (1985). The analyses of uncoded performances for certain ECCM receiver design strategies for multihops/symbol FH/MFSK waveforms. *IEEE Journal on Selected Areas in Communications* 3(5), 611–621. <https://doi.org/10.1109/JSAC.1985.1146248> [verified] [abstract]
- Levitt, B. K. (1985). FH/MFSK performance in multitone jamming. JPL Publication 84-90 (NASA CR-175667), <https://ntrs.nasa.gov/api/citations/19850015886/downloads/19850015886.pdf> [verified] [full]; journal version *IEEE Journal on Selected Areas in Communications* 3(5), 627–643, <https://doi.org/10.1109/JSAC.1985.1146253> [verified] [abstract]
- Levitt, B. K. (1985). Strategies for FH/MFSK signaling with diversity in worst-case partial-band noise. *IEEE Journal on Selected Areas in Communications* 3(5), 622–626. <https://doi.org/10.1109/JSAC.1985.1146252> [verified] [abstract]
- McEliece, R. J., Stark, W. E. (1984). Channels with block interference. *IEEE Transactions on Information Theory* 30(1), 44–53. <https://doi.org/10.1109/TIT.1984.1056848> [verified] [abstract]
- Pursley, M. B., Stark, W. E. (1985). Performance of Reed–Solomon coded frequency-hop spread-spectrum communications in partial-band interference. *IEEE Transactions on Communications* 33(8), 767–774. <https://doi.org/10.1109/TCOM.1985.1096386> [verified] [abstract]
- Ramsey, J. L. (1970). Realization of optimum interleavers. *IEEE Transactions on Information Theory* 16(3), 338–345. <https://doi.org/10.1109/TIT.1970.1054443> [verified] [not re-read]
- Robertson, R. C., Ha, T. T. (1992). Error probabilities of fast frequency-hopped MFSK with noise-normalization combining in a fading channel with partial-band interference. *IEEE Transactions on Communications* 40(2), 404–412. <https://doi.org/10.1109/26.129202> [verified] [abstract]
- Stark, W. E. (1985). Coding for frequency-hopped spread-spectrum communication with partial-band interference—Part I: capacity and cutoff rate. *IEEE Transactions on Communications* 33(10), 1036–1044. <https://doi.org/10.1109/TCOM.1985.1096223> [verified] [abstract]
- Stark, W. E. (1985). Same title—Part II: coded performance. *IEEE Transactions on Communications* 33(10), 1045–1057. <https://doi.org/10.1109/TCOM.1985.1096224> [verified] [abstract]
- Viterbi, A. J. (1982). A robust ratio-threshold technique to mitigate tone and partial band jamming in coded MFSK systems. *MILCOM 1982*, 22.4-1–22.4-5. <https://doi.org/10.1109/MILCOM.1982.4805943> [verified] [abstract]
- Viterbi, A. J., Jacobs, I. M. (1975). Advances in coding and modulation for noncoherent channels affected by fading, partial band, and multiple-access interference. *Advances in Communication Systems* 4, 279–308. <https://doi.org/10.1016/B978-0-12-010904-3.50013-0> [verified DOI and title; author names as cited by Levitt 1985] [not read]

### Cited from memory or through other papers, not verified

- DTIC report ADA172929 (1986), on frequency-hopped random-MFSK radios in worst-case partial-band noise jamming. <https://apps.dtic.mil/sti/pdfs/ADA172929.pdf> [unverified; HTTP 403; title and authors not confirmed; search excerpt only]
- Clark, G. C., Cain, J. B. (1981). *Error-Correction Coding for Digital Communications*. Plenum. [unverified] (pseudo-random interleaving against periodic bursts)
- Fabrizio, G. A. (2013). *High Frequency Over-the-Horizon Radar: Fundamental Principles, Signal Processing, and Practical Applications*. McGraw-Hill. ISBN 9780071621274. <https://www.oreilly.com/library/view/high-frequency-over-the-horizon/9780071621274/> [unverified; listing returned HTTP 403]
- Houston, S. W. (1975). Modulation techniques for communication, Part 1: tone and noise jamming performance of spread spectrum M-ary FSK and 2, 4-ary DPSK waveforms. *NAECON 1975*. [unverified; cited by Levitt 1985]
- Martinez, J. P. (1982). Analysis of the "Woodpecker" signal, *Wireless World*. [unverified; cited by the Wikipedia article]
- Simon, M. K., Omura, J. K., Scholtz, R. A., Levitt, B. K. *Spread Spectrum Communications Handbook*. McGraw-Hill. [unverified; the β values in section 4.6 were recomputed here and agree with recalled table values]

### Wanted but not accessed

- Fabrizio (2013) and the "HF over-the-horizon radar" chapter of Skolnik's *Radar Handbook* (primary sources for JORN and ROTHR waveform ranges and dwell scheduling).
- DST Group Technical Report TR-3477 on the Jindalee backscatter sounder (HTTP 403).
- Sales, G. (1992), OTH-B radar system: system summary, DTIC ADA261727; and DTIC ADA474069, ADA172929 (HTTP 403).
- Hadel, W. (DK2OM) (2013), Radar Systems, DARC intruder-monitoring PDF (larger than the fetch limit).
- Full texts of Francis et al. 2017, Pederick and Cervera 2016, Stark 1985, Viterbi 1982, Lee et al. 1984–1985, Zhidkov 2006 and 2008, Epple and Schnell 2017, Bazin et al. 2006, Li 1998, Reinisch et al. 2009, and the Borio papers (the open-access copy was behind a browser check).
- The 1988 FCC study of the Woodpecker signal.
- The statistics charts in the IARU newsletters (images), and any annual IARU summary with counts by band.
- ITU-R regular HF monitoring programme data (not examined).
- The WSJT-X guide's description of its sample blanker.

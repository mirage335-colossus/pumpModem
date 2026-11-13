# Independent cross-check calculations

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Conventions and evidence grades are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **What this is.** Model calculations made for this set, to check numbers in the chapters against an independent implementation. Every value here is graded [E]. None is a measurement, and none replaces a cited source: where a chapter quotes a source and this appendix differs, the source governs unless the difference is explained.
- **How it was made.** Pure-Python scripts using only the standard library, written for this work and run on 13 November 2026 in the session's scratch directory. They are not part of the repository. Each section states the formulas and assumptions needed to reproduce its numbers.
- **Related chapters.** groundwave [20](20-groundwave-1mhz.md) and [25](25-groundwave-30mhz.md); sub-9 kHz [10](10-sub9khz-propagation.md), [11](11-sub9khz-antennas-links-and-practice.md) and [12](12-sub9khz-noise.md); Earth–Moon–Earth [40](40-eme-lunar-scattering-and-geometry.md) and [41](41-eme-systems-practice-and-environment.md); skywave [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and [33](33-hf-channel-models-and-simulators.md); noise [12](12-sub9khz-noise.md) and [50](50-noise-and-interference-mf-hf.md).

**Current numerical evidence (13 November 2026).** [86](86-statistics-and-calculation-checks.md) independently reproduces the scalar antenna, field/noise, EME range/gain, mirror-path, width and integration calculations and selected lunar integrals. The original ground-wave residue-series implementation is absent: checking 144 field-to-loss conversions does **not** independently reproduce its fields, threshold distances, height effects or complex phase. Those historical solver outputs remain illustrative, not acceptance targets or calibrated scenario inputs. The Fable scenario chapters specify their own selected laws; in particular the selected eleven-node lunar law in [84](84-scenarios-eme.md) is different from this chapter's illustrative Hagfors/diffuse table. The numerical coverage table in 86 states exactly which quantities were checked.

## 1. Smooth-earth ground wave

### 1.1 Method and checks

The attenuation function W of a vertical dipole over a smooth, homogeneous spherical earth is computed by the residue series of Fock, Bremmer and Wait, with time dependence exp(+iωt):

    W = sqrt(π x) · exp(−iπ/4) · Σ_s exp(−i x t_s) / (t_s − q²) · G_s(y1) · G_s(y2)
    x = (ka/2)^(1/3) d/a,   q = −i (ka/2)^(1/3) Δ,   y = k h / (ka/2)^(1/3)
    Δ = sqrt(ε_c − 1)/ε_c,   ε_c = ε_r − i 60 σ λ   (vertical polarisation)

The roots t_s satisfy w1′(t) = q w1(t), where w1 is the Airy-type function 2√π exp(−iπ/6) Ai(t exp(−2πi/3)). They are found from the zeros of Ai′ (q = 0) by Newton iteration for small |q| and by Runge–Kutta continuation in q otherwise. Height-gain factors G_s are Airy ratios. Where x < 0.06 the flat-earth Sommerfeld–Norton function replaces the series. Results follow the ITU-R P.368 conventions: the reference radiator is a short vertical monopole radiating 1 kW e.m.r.p. over perfect ground (300 mV/m at 1 km), so

    E [dB(µV/m)] = 109.54 − 20 log d_km + 20 log |W|
    L_b [dB]     = 142.0 + 20 log f_MHz − E

The ground classes are the eight of P.368 (σ in S/m, ε_r): sea water 5, 70; land 3×10⁻², 40; wet ground 10⁻², 30; land 3×10⁻³, 22; medium dry ground 10⁻³, 15; dry ground 3×10⁻⁴, 7; very dry ground 10⁻⁴, 3; fresh-water ice 10⁻⁵, 3. The effective earth-radius factor is 4/3 unless stated.

Self-checks passed: Ai(0) and Ai′(0) to 15 digits; the first ten zeros of Ai and Ai′ to six digits; continuity of the series/asymptotic Airy evaluation to about 5×10⁻⁴ relative at the switch-over radius; |W| → 1 for a perfect conductor at short range (0.999 at 5 km, 0.989 at 20 km at 1 MHz); and flat-earth and spherical-earth results within 0.25 dB at 5–40 km over 3×10⁻³ S/m at 1 MHz.

**Agreement with the chapters.** The research passes for [20](20-groundwave-1mhz.md) and [25](25-groundwave-30mhz.md) wrote their own residue-series codes and checked them against the P.368-9 curves, NTIA test vectors and Barrick's curves. Their reported 1 MHz values (sea 68.5 dB(µV/m) and 73.5 dB at 100 km; 10 mS/m 50.5 and 91.5 dB; 3 mS/m 37.8 and 104.2 dB; 1 mS/m 29.3 and 112.7 dB; 118.7 dB at 300 km over 10 mS/m) are identical to the tables below to 0.1 dB. Their 30 MHz values (sea 132 dB at 100 km and 186 dB at 300 km; land 131 dB at 10 km and 182 dB at 100 km; 180 dB reached at about 280 km over sea and 57–113 km over land) agree to within 1 dB. Three implementations agreeing shows the arithmetic is consistent. It does not validate the model for any real path.

### 1.2 Field strength and basic transmission loss

Each cell gives E in dB(µV/m) for 1 kW e.m.r.p., then L_b in dB. Both antennas at ground level, 4/3 earth radius.

**500 kHz**

| Ground | 1 km | 10 km | 30 km | 100 km | 300 km | 1,000 km |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water, 5 S/m, ε 70 | 109.5 / 26.4 | 89.5 / 46.4 | 79.9 / 56.1 | 68.8 / 67.2 | 56.3 / 79.7 | 29.1 / 106.9 |
| Land, 3×10⁻² S/m, ε 40 | 109.5 / 26.5 | 89.3 / 46.7 | 79.2 / 56.7 | 66.9 / 69.0 | 51.6 / 84.3 | 17.8 / 118.1 |
| Wet ground, 10⁻² S/m, ε 30 | 109.4 / 26.6 | 88.8 / 47.2 | 77.9 / 58.1 | 63.1 / 72.8 | 42.6 / 93.4 | −2.3 / 138.3 |
| Land, 3×10⁻³ S/m, ε 22 | 109.0 / 27.0 | 86.8 / 49.2 | 73.3 / 62.7 | 52.7 / 83.3 | 27.4 / 108.6 | −23.7 / 159.6 |
| Medium dry ground, 10⁻³ S/m, ε 15 | 107.9 / 28.0 | 82.1 / 53.9 | 64.4 / 71.6 | 41.1 / 94.8 | 16.4 / 119.6 | −36.9 / 172.9 |
| Dry ground, 3×10⁻⁴ S/m, ε 7 | 105.5 / 30.5 | 73.4 / 62.6 | 53.3 / 82.6 | 30.8 / 105.2 | 6.1 / 129.9 | −48.7 / 184.6 |
| Very dry ground, 10⁻⁴ S/m, ε 3 | 101.4 / 34.6 | 64.4 / 71.6 | 44.7 / 91.3 | 22.5 / 113.5 | −2.3 / 138.3 | −57.9 / 193.9 |
| Fresh-water ice, 10⁻⁵ S/m, ε 3 | 98.5 / 37.5 | 62.1 / 73.9 | 42.9 / 93.1 | 20.9 / 115.1 | −3.9 / 139.9 | −59.5 / 195.5 |

**1 MHz**

| Ground | 1 km | 10 km | 30 km | 100 km | 300 km | 1,000 km |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water, 5 S/m, ε 70 | 109.5 / 32.5 | 89.5 / 52.5 | 79.8 / 62.2 | 68.5 / 73.5 | 54.8 / 87.2 | 22.2 / 119.8 |
| Land, 3×10⁻² S/m, ε 40 | 109.4 / 32.6 | 88.6 / 53.4 | 77.3 / 64.7 | 61.3 / 80.7 | 38.0 / 104.0 | −16.8 / 158.8 |
| Wet ground, 10⁻² S/m, ε 30 | 109.0 / 33.0 | 86.5 / 55.5 | 72.5 / 69.5 | 50.5 / 91.5 | 23.3 / 118.7 | −38.8 / 180.8 |
| Land, 3×10⁻³ S/m, ε 22 | 107.7 / 34.3 | 80.5 / 61.5 | 61.6 / 80.4 | 37.8 / 104.2 | 11.0 / 131.0 | −54.4 / 196.4 |
| Medium dry ground, 10⁻³ S/m, ε 15 | 104.9 / 37.1 | 72.1 / 69.9 | 52.2 / 89.8 | 29.3 / 112.7 | 2.4 / 139.6 | −64.4 / 206.4 |
| Dry ground, 3×10⁻⁴ S/m, ε 7 | 100.5 / 41.5 | 63.3 / 78.7 | 43.7 / 98.3 | 21.1 / 120.9 | −6.1 / 148.1 | −74.0 / 216.0 |
| Very dry ground, 10⁻⁴ S/m, ε 3 | 95.3 / 46.7 | 56.4 / 85.6 | 37.0 / 105.0 | 14.4 / 127.6 | −12.9 / 154.9 | −81.4 / 223.4 |
| Fresh-water ice, 10⁻⁵ S/m, ε 3 | 94.4 / 47.6 | 56.1 / 85.9 | 36.8 / 105.2 | 14.3 / 127.7 | −13.0 / 155.0 | −81.5 / 223.5 |

**30 MHz**

| Ground | 1 km | 3 km | 10 km | 30 km | 100 km | 300 km |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water, 5 S/m, ε 70 | 109.1 / 62.5 | 98.7 / 72.8 | 85.4 / 86.1 | 68.4 / 103.1 | 39.2 / 132.4 | −14.0 / 185.5 |
| Land, 3×10⁻² S/m, ε 40 | 86.9 / 84.6 | 67.7 / 103.8 | 46.4 / 125.2 | 25.9 / 145.7 | −4.0 / 175.6 | −65.0 / 236.5 |
| Wet ground, 10⁻² S/m, ε 30 | 83.6 / 88.0 | 64.5 / 107.0 | 43.3 / 128.3 | 22.8 / 148.8 | −7.2 / 178.7 | −68.3 / 239.8 |
| Land, 3×10⁻³ S/m, ε 22 | 80.8 / 90.7 | 61.8 / 109.8 | 40.5 / 131.0 | 20.0 / 151.5 | −10.0 / 181.5 | −71.3 / 242.8 |
| Medium dry ground, 10⁻³ S/m, ε 15 | 77.7 / 93.9 | 58.6 / 112.9 | 37.4 / 134.2 | 16.9 / 154.7 | −13.2 / 184.7 | −74.6 / 246.2 |
| Dry ground, 3×10⁻⁴ S/m, ε 7 | 71.8 / 99.7 | 52.7 / 118.8 | 31.5 / 140.1 | 10.9 / 160.6 | −19.2 / 190.7 | −80.9 / 252.4 |
| Very dry ground, 10⁻⁴ S/m, ε 3 | 66.6 / 104.9 | 47.5 / 124.0 | 26.3 / 145.2 | 5.7 / 165.8 | −24.5 / 196.0 | −86.3 / 257.8 |
| Fresh-water ice, 10⁻⁵ S/m, ε 3 | 66.6 / 104.9 | 47.5 / 124.0 | 26.3 / 145.2 | 5.7 / 165.8 | −24.5 / 196.0 | −86.3 / 257.8 |

At 30 MHz the land classes are not generally within a few decibels: at 100 km the listed land values span 175.6–196.0 dB, a 20.4 dB range, and at 300 km they span 236.5–257.8 dB. The very-dry-ground and ice rows coincide at the displayed precision, but that does not generalize to the other classes. These are the smooth-earth results for the stated electrical constants; real land paths at 30 MHz are further affected by terrain, vegetation and buildings, which this model does not contain ([25](25-groundwave-30mhz.md)).

### 1.3 Distance at which a given basic transmission loss is reached

Distances in km; ground-level antennas; 4/3 earth radius.

**500 kHz**

| Ground | 100 dB | 110 dB | 120 dB | 130 dB | 140 dB | 150 dB | 160 dB | 170 dB | 180 dB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water | 808 | 1,087 | 1,373 | 1,666 | 1,962 | 2,262 | 2,564 | 2,868 | 3,173 |
| Land, 3×10⁻² S/m | 602 | 818 | 1,042 | 1,271 | 1,503 | 1,738 | 1,974 | 2,212 | 2,451 |
| Wet ground, 10⁻² S/m | 387 | 535 | 694 | 860 | 1,029 | 1,200 | 1,373 | 1,547 | 1,722 |
| Land, 3×10⁻³ S/m | 214 | 316 | 440 | 575 | 716 | 860 | 1,005 | 1,152 | 1,299 |
| Medium dry ground, 10⁻³ S/m | 129 | 204 | 305 | 423 | 552 | 685 | 822 | 960 | 1,099 |
| Dry ground, 3×10⁻⁴ S/m | 76 | 128 | 203 | 301 | 417 | 542 | 671 | 804 | 937 |
| Very dry ground, 10⁻⁴ S/m | 49 | 83 | 139 | 218 | 318 | 434 | 558 | 686 | 817 |
| Fresh-water ice, 10⁻⁵ S/m | 44 | 77 | 128 | 204 | 302 | 415 | 538 | 666 | 796 |

**1 MHz**

| Ground | 100 dB | 110 dB | 120 dB | 130 dB | 140 dB | 150 dB | 160 dB | 170 dB | 180 dB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water | 558 | 777 | 1,005 | 1,238 | 1,475 | 1,714 | 1,956 | 2,199 | 2,443 |
| Land, 3×10⁻² S/m | 258 | 367 | 488 | 616 | 748 | 881 | 1,016 | 1,152 | 1,288 |
| Wet ground, 10⁻² S/m | 146 | 220 | 313 | 418 | 529 | 643 | 758 | 874 | 991 |
| Land, 3×10⁻³ S/m | 81 | 132 | 203 | 291 | 389 | 494 | 601 | 710 | 819 |
| Medium dry ground, 10⁻³ S/m | 52 | 88 | 142 | 215 | 304 | 401 | 504 | 609 | 715 |
| Dry ground, 3×10⁻⁴ S/m | 33 | 57 | 96 | 153 | 229 | 318 | 415 | 517 | 620 |
| Very dry ground, 10⁻⁴ S/m | 23 | 40 | 68 | 112 | 176 | 256 | 348 | 446 | 547 |
| Fresh-water ice, 10⁻⁵ S/m | 22 | 39 | 67 | 112 | 175 | 255 | 347 | 445 | 546 |

**30 MHz**

| Ground | 100 dB | 110 dB | 120 dB | 130 dB | 140 dB | 150 dB | 160 dB | 170 dB | 180 dB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water | 25 | 42 | 64 | 92 | 126 | 163 | 201 | 239 | 278 |
| Land, 3×10⁻² S/m | 2.4 | 4.2 | 7.5 | 13 | 22 | 37 | 58 | 84 | 113 |
| Wet ground, 10⁻² S/m | 2.0 | 3.6 | 6.3 | 11 | 19 | 32 | 51 | 75 | 104 |
| Land, 3×10⁻³ S/m | 1.7 | 3.0 | 5.4 | 9.5 | 16 | 28 | 45 | 68 | 96 |
| Medium dry ground, 10⁻³ S/m | 1.4 | 2.5 | 4.5 | 7.9 | 14 | 24 | 39 | 60 | 86 |
| Dry ground, 3×10⁻⁴ S/m | 1.0 | 1.8 | 3.2 | 5.7 | 10.0 | 17 | 29 | 47 | 70 |
| Very dry ground, 10⁻⁴ S/m | 0.8 | 1.3 | 2.4 | 4.2 | 7.4 | 13 | 22 | 37 | 57 |
| Fresh-water ice, 10⁻⁵ S/m | 0.8 | 1.3 | 2.4 | 4.2 | 7.4 | 13 | 22 | 37 | 57 |

### 1.4 Sensitivities

**Effective earth radius.** Changing the factor from 4/3 to 1 (no atmospheric refraction) lowers the field by the following amounts, in dB. It matters at 30 MHz and beyond a few hundred kilometres at MF; ITU-R P.368 uses an exponential atmosphere instead of either constant factor.

| Frequency, ground | 100 km | 300 km | 1,000 km |
| --- | ---: | ---: | ---: |
| 0.5 MHz, sea water | 0.2 | 1.2 | 5.8 |
| 0.5 MHz, land 3×10⁻³ S/m | 0.2 | 1.5 | 9.6 |
| 1 MHz, sea water | 0.3 | 1.7 | 7.5 |
| 1 MHz, land 3×10⁻³ S/m | 0.4 | 2.8 | 14.7 |
| 1 MHz, dry ground | 0.6 | 3.2 | 16.0 |
| 30 MHz, sea water | 1.7 | 10.0 | 39.8 |
| 30 MHz, land 3×10⁻³ S/m | 3.6 | 15.2 | 56.6 |

**Ground conductivity: amplitude and phase at 1 MHz.** Relative permittivity fixed at 15. Each cell gives 20 log |W| and the phase of W relative to σ = 10⁻² S/m, unwrapped.

| Distance | 10⁻² | 3×10⁻³ | 2×10⁻³ | 10⁻³ | 5×10⁻⁴ | 3×10⁻⁴ S/m |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 km | −7.0 dB, 0 rad | −18.5 dB, −0.57 rad | −22.6 dB, −0.52 rad | −27.8 dB, −0.25 rad | −30.6 dB, +0.07 rad | −31.5 dB, +0.26 rad |
| 100 km | −18.9 dB, 0 | −32.1 dB, +0.07 | −35.6 dB, +0.20 | −40.2 dB, +0.52 | −42.7 dB, +0.86 | −43.5 dB, +1.05 |
| 300 km | −36.8 dB, 0 | −49.4 dB, +0.26 | −52.9 dB, +0.42 | −57.6 dB, +0.77 | −60.1 dB, +1.13 | −60.9 dB, +1.33 |

A halving of conductivity between 10⁻³ and 5×10⁻⁴ S/m, of the kind freezing or drying can produce, changes the 100 km field by 2.5 dB and its phase by 0.34 rad. Spread over hours, such a phase change is a frequency offset of a few microhertz: far below the 1.7 mHz resolution of a 576 s bit. The amplitude change is the part that matters ([20](20-groundwave-1mhz.md) gives measured seasonal ranges).

**Antenna height at 30 MHz.** L_b in dB with both antennas at the stated height.

| Ground | Heights | 30 km | 50 km | 100 km | 200 km | 300 km |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea water | 0 m | 103.1 | 114.0 | 132.4 | 159.9 | 185.5 |
| Sea water | 10 m | 104.5 | 115.3 | 133.8 | 161.2 | 186.9 |
| Sea water | 30 m | 106.8 | 117.7 | 136.2 | 163.6 | 189.2 |
| Land, 3×10⁻³ S/m | 0 m | 151.5 | 162.4 | 181.5 | 212.9 | 242.8 |
| Land, 3×10⁻³ S/m | 10 m | 143.2 | 154.1 | 173.2 | 204.5 | 234.5 |
| Land, 3×10⁻³ S/m | 30 m | 127.4 | 138.3 | 157.4 | 188.7 | 218.7 |
| Dry ground, 3×10⁻⁴ S/m | 0 m | 160.6 | 171.6 | 190.7 | 222.3 | 252.4 |
| Dry ground, 3×10⁻⁴ S/m | 10 m | 145.3 | 156.3 | 175.5 | 207.0 | 237.2 |
| Dry ground, 3×10⁻⁴ S/m | 30 m | 127.7 | 138.6 | 157.8 | 189.3 | 219.4 |

Over land at 30 MHz, raising both antennas from the ground to 30 m reduces the loss by 24–33 dB. Over sea, height slightly increases the loss: the sea already behaves as a good conductor, and the surface-wave term dominates there.

**Frequency selectivity across a 3 kHz channel.** Over 3×10⁻³ S/m land, W changes by less than 0.07 dB in amplitude and less than 0.03 µs in group delay across 3 kHz at 1 MHz (100 and 300 km), and by less than 0.002 dB and 0.003 µs at 30 MHz (30 and 100 km). The smooth-earth ground wave is a flat, non-dispersive channel for this modem; any selectivity on a ground-wave path comes from other mechanisms.

### 1.5 What the GUI's "180 dB at −164 dBm/Hz" corresponds to on a ground-wave path

From section 3.1 of [00](00-method-and-modem-context.md), a GUI path loss of 180 dB against −164 dBm/Hz reproduces a real link's C/N0 when L_b + F_a + X = 193.5 dB, where X is the transmitting antenna's loss relative to an ideal short monopole. The table gives the corresponding L_b and the smooth-earth distances, for X = 0 and X = 20 dB. Man-made F_a are the ITU-R P.372 median formulas (c − d log f_MHz: quiet rural 53.6/28.6, rural 67.2/27.7, city 76.8/27.7); galactic 52 − 23 log f_MHz; the night atmospheric values are the 1 MHz range given in [50](50-noise-and-interference-mf-hf.md). This assumes the external noise stays above the receiver's own noise after receive-antenna losses.

| Frequency | Noise case | F_a | Equivalent L_b and distance, X = 0 | X = 20 dB |
| --- | --- | ---: | --- | --- |
| 0.5 MHz | quiet rural, man-made | 62.2 dB | 131 dB: sea 1,704 km, land 593 km | 111 dB: sea 1,123 km, land 331 km |
| 0.5 MHz | rural, man-made | 75.5 dB | 118 dB: sea 1,314 km, land 413 km | 98 dB: sea 753 km, land 196 km |
| 0.5 MHz | city, man-made | 85.1 dB | 108 dB: sea 1,040 km, land 298 km | 88 dB: sea 502 km, land 127 km |
| 1 MHz | quiet rural, man-made | 53.6 dB | 140 dB: sea 1,472 km, land 388 km | 120 dB: sea 1,003 km, land 202 km |
| 1 MHz | rural, man-made | 67.2 dB | 126 dB: sea 1,151 km, land 257 km | 106 dB: sea 695 km, land 111 km |
| 1 MHz | city, man-made | 76.8 dB | 117 dB: sea 929 km, land 177 km | 97 dB: sea 488 km, land 69 km |
| 1 MHz | night atmospheric, low | 60 dB | 134 dB: sea 1,320 km, land 324 km | 114 dB: sea 856 km, land 154 km |
| 1 MHz | night atmospheric, high | 90 dB | 104 dB: sea 633 km, land 97 km | 84 dB: sea 234 km, land 35 km |
| 30 MHz | galactic | 18.0 dB | 175 dB: sea 261 km, land 83 km | 155 dB: sea 183 km, land 36 km |
| 30 MHz | rural, man-made | 26.3 dB | 167 dB: sea 228 km, land 61 km | 147 dB: sea 152 km, land 24 km |
| 30 MHz | city, man-made | 35.9 dB | 158 dB: sea 191 km, land 40 km | 138 dB: sea 118 km, land 14 km |

"Land" is 3×10⁻³ S/m, ε 22, with ground-level antennas. The ground-wave distances at MF at night are not usable alone: beyond the fading zone the sky wave dominates ([20](20-groundwave-1mhz.md), [30](30-skywave-signal-level-and-outages.md)). At 30 MHz over land, elevated antennas, terrain and competing modes govern ([25](25-groundwave-30mhz.md)).

## 2. A 61 m antenna below 9 kHz

### 2.1 Assumptions

- **Capacitance.** A bare vertical wire, height h = 60.96 m and radius a: C = 2πε₀h / (ln(h/a) − 1), giving 339 pF for a = 1 mm, 403 pF for 5 mm and 786 pF for a 0.3 m tower-like radius. "Bare wire" below uses 340 pF; "top-loaded" assumes 1 nF, an illustrative value for a modest top hat.
- **Effective height.** h/2 = 30.5 m for the bare wire; 50 m assumed for the top-loaded case.
- **Radiation resistance.** R_r = 160π²(h_e/λ)², for a short monopole over perfect ground. Radiated power I²R_r is then numerically the e.m.r.p.
- **Losses.** Loading-coil Q of 300 (loss X_C/300), plus 60 Ω of ground and environment loss. Both are assumptions; real values vary widely ([11](11-sub9khz-antennas-links-and-practice.md), [55](55-equipment-and-front-end-effects.md)).
- **Field.** E = 120π I h_e/(λ d), the inverse-distance radiation field at 100 km, ignoring the Earth–ionosphere waveguide. At 100 km this is an order-of-magnitude reference only; [10](10-sub9khz-propagation.md) gives the waveguide treatment.

### 2.2 Electrical parameters

| Frequency | Wavelength | λ/2π | R_r, h_e 30.5 m | R_r, h_e 50 m | Reactance, 340 pF | Loading inductance | Volts per ampere, 340 pF / 1 nF |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8,970 Hz | 33.4 km | 5.3 km | 1.32 mΩ | 3.53 mΩ | 52 kΩ | 0.93 H | 52 kV / 18 kV |
| 8,270 Hz | 36.3 km | 5.8 km | 1.12 mΩ | 3.00 mΩ | 57 kΩ | 1.09 H | 57 kV / 19 kV |
| 5,170 Hz | 58.0 km | 9.2 km | 0.44 mΩ | 1.17 mΩ | 91 kΩ | 2.79 H | 91 kV / 31 kV |
| 2,970 Hz | 100.9 km | 16.1 km | 0.14 mΩ | 0.39 mΩ | 158 kΩ | 8.45 H | 158 kV / 54 kV |
| 1,000 Hz | 299.8 km | 47.7 km | 0.016 mΩ | 0.044 mΩ | 468 kΩ | 74.5 H | 468 kV / 159 kV |
| 300 Hz | 999 km | 159 km | 1.5 µΩ | 4.0 µΩ | 1,560 kΩ | 828 H | 1,560 kV / 531 kV |
| 76 Hz | 3,945 km | 628 km | 0.094 µΩ | 0.25 µΩ | 6,160 kΩ | 12,900 H | 6,160 kV / 2,090 kV |

The loading inductance resonates the antenna capacitance: 340 pF at 8.27 kHz needs 1.09 H.

### 2.3 Radiated power, field and bandwidth

| Case | Transmitter | Loss resistance | Current | Antenna voltage | Radiated power (e.m.r.p.) | Efficiency | Field at 100 km | Loaded Q; −3 dB bandwidth |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8.27 kHz, bare wire | 100 W | 249 Ω | 0.63 A | 36 kV | 0.45 mW | −53 dB | 2.0 µV/m (6.7 fT) | 228; 36 Hz |
| 8.27 kHz, bare wire | 1 kW | 249 Ω | 2.0 A | 114 kV | 4.5 mW | −53 dB | 6.4 µV/m (21 fT) | 228; 36 Hz |
| 8.27 kHz, top-loaded | 100 W | 124 Ω | 0.90 A | 17 kV | 2.4 mW | −46 dB | 4.7 µV/m (16 fT) | 155; 53 Hz |
| 8.27 kHz, top-loaded | 1 kW | 124 Ω | 2.8 A | 55 kV | 24 mW | −46 dB | 15 µV/m (49 fT) | 155; 53 Hz |
| 2.97 kHz, bare wire | 100 W | 585 Ω | 0.41 A | 65 kV | 25 µW | −66 dB | 0.47 µV/m (1.6 fT) | 269; 11 Hz |
| 2.97 kHz, bare wire | 1 kW | 585 Ω | 1.3 A | 206 kV | 0.25 mW | −66 dB | 1.5 µV/m (5.0 fT) | 269; 11 Hz |
| 2.97 kHz, top-loaded | 100 W | 239 Ω | 0.65 A | 35 kV | 0.16 mW | −58 dB | 1.2 µV/m (4.0 fT) | 225; 13 Hz |
| 2.97 kHz, top-loaded | 1 kW | 239 Ω | 2.0 A | 110 kV | 1.6 mW | −58 dB | 3.8 µV/m (12.8 fT) | 225; 13 Hz |
| 1 kHz, top-loaded | 100 W | 591 Ω | 0.41 A | 65 kV | 7.4 µW | −71 dB | 0.26 µV/m (0.9 fT) | 270; 3.7 Hz |
| 1 kHz, top-loaded | 1 kW | 591 Ω | 1.3 A | 207 kV | 74 µW | −71 dB | 0.82 µV/m (2.7 fT) | 270; 3.7 Hz |

- Several entries exceed practical insulation and corona limits. If the antenna voltage is capped at 30 kV, the bare wire at 8.27 kHz is limited to 0.53 A and 0.31 mW radiated, whatever the transmitter power, and the top-loaded case to 1.6 A and 7.3 mW. Voltage, not transmitter power, is then the binding limit; [11](11-sub9khz-antennas-links-and-practice.md) and [55](55-equipment-and-front-end-effects.md) give the limits reported in practice.
- The loaded bandwidth is a few hertz to tens of hertz. A 0.06 Hz-wide signal sees a flat response, but the antenna's resonance sets the phase: a fractional capacitance change ΔC/C detunes the resonance by Δf₀/f₀ ≈ −ΔC/(2C) at fixed inductance. At a fixed operating frequency the magnitude of the resulting phase change is about arctan(Q ΔC/C) (its sign depends on the transfer-function convention). For a 1 % change that is 35° at Q = 70, 57° at Q = 155 and 63° at Q = 200. A capacitance that wanders with wind or rain therefore acts as phase noise on the transmitted carrier, separate from any propagation effect.

### 2.4 C/N0 at 100 km against measured noise levels

The fields of section 2.3 are compared with four noise field densities at 8.27 kHz: the minimum and maximum of the ITU-R P.372 below-10 kHz figure as digitised in [12](12-sub9khz-noise.md) (F_a 147.8 and 174.7 dB, giving 3.4 and 75 µV/m/√Hz by E_n = F_a + 20 log f_MHz − 95.5), and the Stanford 8 kHz monthly mean envelope densities of 7.5 and 38 fT/√Hz reported there, converted by E = cB to 2.25 and 11.4 µV/m/√Hz. The Stanford values are mean envelopes, not rms, so these two columns are indicative only.

| Case | Field at 100 km | P.372 minimum | 7.5 fT/√Hz | 38 fT/√Hz | P.372 maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| 8.27 kHz, bare wire, 100 W | 2.0 µV/m | −4.6 dB-Hz | −1.0 | −15.1 | −31.5 |
| 8.27 kHz, bare wire, 1 kW | 6.4 µV/m | 5.4 | 9.0 | −5.1 | −21.5 |
| 8.27 kHz, top-loaded, 100 W | 4.7 µV/m | 2.7 | 6.3 | −7.8 | −24.2 |
| 8.27 kHz, top-loaded, 1 kW | 15 µV/m | 12.7 | 16.3 | 2.2 | −14.2 |

Row 1 of the manual test list, "1 mW, 200 dB" at −164 dBm/Hz, is −36 dB-Hz. One milliwatt of e.m.r.p. gives 3 µV/m at 100 km by inverse distance, so −36 dB-Hz is reached at about 250 km under the P.372 maximum noise, and only well beyond the range of the inverse-distance approximation (about 5,500 km) under the minimum. One physically illustrative interpretation of the synthetic row is roughly 1 mW of radiated power. The GUI power/path-loss pair itself does not fix that reference plane or uniquely determine antenna input power. Under the assumptions of section 2.1 that needs about 40 W into the top-loaded antenna or about 220 W into the bare wire at 8.27 kHz, and kilowatts at 3 kHz; with a 30 kV voltage cap the bare wire cannot radiate 1 mW at 8.27 kHz at all. The C/N0 the same radiated power gives varies by up to 27 dB with the noise conditions. These are estimates; measured links are in [11](11-sub9khz-antennas-links-and-practice.md).

### 2.5 Mains-harmonic wander near 8.27 kHz

The nearest power-line harmonic to 8.27 kHz is the 165th of 50 Hz (8,250 Hz) or the 138th of 60 Hz (8,280 Hz). A harmonic's frequency moves n times as far as the grid frequency.

| Grid deviation | 10 mHz | 20 mHz | 50 mHz | 100 mHz | 200 mHz |
| --- | ---: | ---: | ---: | ---: | ---: |
| 50 Hz grid, n = 165 | 1.7 Hz | 3.3 Hz | 8.2 Hz | 16.5 Hz | 33.0 Hz |
| 60 Hz grid, n = 138 | 1.4 Hz | 2.8 Hz | 6.9 Hz | 13.8 Hz | 27.6 Hz |

A grid deviation of a few tens of millihertz moves the harmonic by several hertz, more than fifty times the modem's 0.06 Hz occupied bandwidth. A harmonic line can therefore sweep through the signal band within minutes, and a fixed comb notch cannot follow it. [12](12-sub9khz-noise.md) gives the measured grid statistics.

## 3. Earth–Moon–Earth

### 3.1 Two-way path loss

The radar-equation loss between isotropic antennas is

    L = (4π)³ d⁴ / (λ² · ρ π a²),   a = 1,737.4 km,   ρ = 0.065 (assumed)

where ρπa² is the Moon's radar cross-section. Geocentric (centre-to-centre) distance, in dB:

| Lunar distance | 50 MHz | 144 MHz | 432 MHz | 1,296 MHz | 2,320 MHz | 5,760 MHz | 10,368 MHz | 24,048 MHz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Perigee, 356,400 km | 241.6 | 250.8 | 260.3 | 269.9 | 274.9 | 282.8 | 287.9 | 295.2 |
| Mean, 384,400 km | 242.9 | 252.1 | 261.6 | 271.2 | 276.2 | 284.1 | 289.2 | 296.6 |
| Apogee, 406,700 km | 243.9 | 253.1 | 262.6 | 272.2 | 277.2 | 285.1 | 290.2 | 297.5 |

- The 5,760 MHz mean value, 284.1 dB, equals the figure the research pass for [41](41-eme-systems-practice-and-environment.md) found in the ARRL Handbook and the value in [40](40-eme-lunar-scattering-and-geometry.md).
- *Topocentric distance.* With the Moon overhead the range is shorter by one Earth radius and the loss 0.29 dB lower (283.85 dB at mean distance); with the Moon on the horizon the range is almost exactly geocentric (284.14 dB). Across perigee-overhead to apogee-horizon the 5,760 MHz loss spans 282.5–285.1 dB, a range of 2.6 dB.
- *Cross-section.* ρ = 0.05 or 0.08 instead of 0.065 changes the loss by +1.1 or −0.9 dB. The chapters discuss how ρ varies with wavelength.
- *Narrow beams.* The formula assumes the whole disc is illuminated with the antennas' on-axis gain. Section 3.4 gives the correction when the beam is narrower than the Moon.

### 3.2 Delay and Doppler geometry

- **Echo delay.** The leading edge comes from the sub-radar point. Its delay is 2(D − R_E − a)/c with the Moon overhead: 2.32 s at perigee, 2.51 s at mean distance, 2.66 s at apogee. With the Moon on the horizon it is about 0.04 s longer (2.70 s at apogee).
- **Delay depth.** The limb lies 2a/c = 11.59 ms behind the leading edge. Delay increases with the fractional disc radius r as (2a/c)(1 − √(1 − r²)): 0.06 ms at r = 0.1, 0.23 ms at 0.2, 0.50 ms at 0.29, 1.55 ms at 0.5, 3.31 ms at 0.7, 11.6 ms at the limb.
- **Doppler shift.** The rotational term for an equatorial station's own echo reaches ±2v/λ = ±17.9 kHz at 5,760 MHz (v = 465 m/s). The Moon's orbital radial velocity, of order ±55 m/s, adds about ±2.1 kHz. The largest Doppler rate for an equatorial station's own echo with the Moon on the meridian is 2ω²R_E/λ = 1.30 Hz/s.
- **Limb-to-limb spread.** With apparent rotation rate ω_app, the echo spectrum extends over 4 ω_app a / λ. The diurnal contribution for an equatorial observer is ω R_E / D = 1.21 µrad/s at most; optical libration rates are about 0.36 µrad/s in longitude and 0.31 µrad/s in latitude. Limb-to-limb widths:

| Apparent rotation rate | 144 MHz | 1,296 MHz | 5,760 MHz | 10,368 MHz | 24,048 MHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0.2 µrad/s | 0.7 Hz | 6.0 Hz | 26.7 Hz | 48 Hz | 111 Hz |
| 0.5 µrad/s | 1.7 Hz | 15.0 Hz | 66.8 Hz | 120 Hz | 279 Hz |
| 1.0 µrad/s | 3.3 Hz | 30.0 Hz | 134 Hz | 240 Hz | 557 Hz |
| 1.2 µrad/s | 4.0 Hz | 36.1 Hz | 160 Hz | 288 Hz | 669 Hz |
| 1.6 µrad/s | 5.3 Hz | 48.1 Hz | 214 Hz | 385 Hz | 892 Hz |

The 160 Hz value at 1.2 µrad/s agrees with the 164–165 Hz maximum reported for 40° latitude in [40](40-eme-lunar-scattering-and-geometry.md) and [41](41-eme-systems-practice-and-environment.md). The actual rate at a given time combines the diurnal and libration terms vectorially and needs an ephemeris.

- **Speckle size on the ground.** For whole-disc illumination the echo's speckle pattern on Earth has a scale of about λD/(2a) = 5.8 m at 5,760 MHz and mean distance, or larger when the effective scattering area is concentrated near the disc centre. This dimensional scale alone does not establish independence between stations or aperture elements. Their covariance depends on the illuminated brightness/scattering distribution, separation direction, polarization and averaging; use its spatial Fourier transform or matched measurements. [40](40-eme-lunar-scattering-and-geometry.md) gives about 6.5 m by a different route.

### 3.3 Doppler spectrum and delay profile from assumed scattering laws

The echo is integrated over the visible hemisphere with σ⁰(φ) = (1 − f_d)·k_q · (C/2)(cos⁴φ + C sin²φ)^(−3/2) + f_d·k_d · cosⁿφ. This is a Hagfors quasi-specular term plus a diffuse term, normalised so that the diffuse share of whole-disc power is f_d. A surface point at fractional radius r contributes at excess delay (2a/c)(1 − cos φ) and at a Doppler offset proportional to its projected distance from the apparent rotation axis. Widths are fractions of the limb-to-limb width LL (multiply by LL from section 3.2 to get hertz). The parameter values are illustrative spans, not fits: [40](40-eme-lunar-scattering-and-geometry.md) gives the measured parameters and the reasons a value near C ≈ 40–45 with a diffuse share of about 30 % applies near 5 cm.

**Whole-disc illumination**

| Law: C, f_d, n | FWHM | −10 dB width | 2 × rms | Width with 50 % of power | 80 % | 90 % | Mean delay | rms delay | Power within 0.1 / 0.5 / 1 / 2 / 5 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 150, 0.10, 1.5 | 0.085 | 0.28 | 0.27 | 0.091 | 0.29 | 0.49 | 1.12 ms | 2.16 ms | 0.43 / 0.68 / 0.76 / 0.83 / 0.93 |
| 95, 0, 1.5 | 0.105 | 0.33 | 0.27 | 0.099 | 0.28 | 0.46 | 1.08 ms | 2.16 ms | 0.39 / 0.68 / 0.78 / 0.85 / 0.93 |
| 95, 0.20, 1.5 | 0.111 | 0.45 | 0.32 | 0.127 | 0.39 | 0.59 | 1.52 ms | 2.40 ms | 0.31 / 0.56 / 0.66 / 0.76 / 0.89 |
| 65, 0.25, 1.5 | 0.140 | 0.63 | 0.34 | 0.158 | 0.44 | 0.63 | 1.78 ms | 2.52 ms | 0.24 / 0.49 / 0.60 / 0.71 / 0.88 |
| 65, 0.25, 1.0 | 0.139 | 0.65 | 0.35 | 0.160 | 0.46 | 0.66 | 1.92 ms | 2.68 ms | 0.24 / 0.49 / 0.59 / 0.69 / 0.86 |
| 40, 0.35, 1.0 | 0.196 | 0.90 | 0.39 | 0.213 | 0.53 | 0.71 | 2.38 ms | 2.82 ms | 0.16 / 0.38 / 0.49 / 0.61 / 0.82 |
| 40, 0.50, 1.0 | 0.226 | 0.96 | 0.42 | 0.253 | 0.58 | 0.74 | 2.72 ms | 2.87 ms | 0.12 / 0.31 / 0.42 / 0.54 / 0.78 |
| 20, 0.50, 1.0 | 0.366 | 0.99 | 0.44 | 0.291 | 0.61 | 0.76 | 2.98 ms | 2.91 ms | 0.08 / 0.24 / 0.36 / 0.50 / 0.76 |

**Beam-limited illumination.** Law C = 40, f_d = 0.35, n = 1, the same Gaussian beam at both ends, with half-power beamwidth 70λ/D at 5,760 MHz; "Relative power" is the echo power relative to uniform whole-disc illumination at the same on-axis gain.

| Dish diameter (beamwidth) | FWHM | −10 dB width | 2 × rms | Width with 50 % / 90 % of power | Mean delay | rms delay | Power within 0.1 / 0.5 / 1 ms | Relative power |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| 3 m (1.21°) | 0.190 | 0.84 | 0.38 | 0.199 / 0.68 | 2.16 ms | 2.70 ms | 0.17 / 0.41 / 0.52 | −0.3 dB |
| 5 m (0.73°) | 0.182 | 0.72 | 0.35 | 0.178 / 0.63 | 1.83 ms | 2.47 ms | 0.19 / 0.45 / 0.57 | −0.8 dB |
| 7.3 m (0.50°) | 0.170 | 0.57 | 0.31 | 0.151 / 0.55 | 1.36 ms | 2.05 ms | 0.22 / 0.52 / 0.66 | −1.6 dB |
| 10 m (0.36°) | 0.158 | 0.45 | 0.25 | 0.126 / 0.43 | 0.88 ms | 1.46 ms | 0.27 / 0.62 / 0.76 | −2.5 dB |
| 25 m (0.15°) | 0.115 | 0.24 | 0.12 | 0.072 / 0.19 | 0.17 ms | 0.21 ms | 0.52 / 0.94 / 0.99 | −5.8 dB |
| 64 m (0.057°) | 0.066 | 0.12 | 0.057 | 0.038 / 0.094 | 0.04 ms | 0.04 ms | 0.92 / 1.00 / 1.00 | −10.9 dB |

What these show:

- The spectrum is peaked and heavy-tailed, not Gaussian. Its half-power width is only 8–37 % of the limb-to-limb width, while 90 % of the power needs 46–76 %. A single quoted "spread" is therefore ambiguous by a factor of three to five unless its definition is stated.
- The delay profile is continuous from 0 to 11.6 ms, with between a quarter and two thirds of the power within 0.5 ms and 7–24 % beyond 5 ms for whole-disc illumination. It is not a pair of discrete rays.
- A beam narrower than the Moon shortens the delay profile and narrows the spectrum, at the cost of echo power. Only dishes of about 25 m and more confine the echo to within about 0.5 ms at 5,760 MHz.
- **The "EME focused 6 Hz 0.5 ms" preset.** For 6 Hz to be the half-power width at the median limb-to-limb width of about 86 Hz reported in [41](41-eme-systems-practice-and-environment.md), the ratio would have to be 0.07. Under these laws that needs a dish of the order of 60 m, or a time near a libration minimum when the limb-to-limb width itself is small. The preset's 0.5 ms is close to the delay holding about half the whole-disc power under the more specular laws (C ≥ 65), not to a discrete echo delay. These statements depend on the assumed laws; [40](40-eme-lunar-scattering-and-geometry.md) reaches a similar conclusion from measured scattering tables.

### 3.4 Station figures at 5,760 MHz

| Dish diameter | Gain (55 % aperture efficiency) | Half-power beamwidth | Beamwidth ÷ mean lunar diameter |
| ---: | ---: | ---: | ---: |
| 1.5 m | 36.5 dBi | 2.43° | 4.69 |
| 2.4 m | 40.6 dBi | 1.52° | 2.93 |
| 3 m | 42.6 dBi | 1.21° | 2.34 |
| 5 m | 47.0 dBi | 0.73° | 1.41 |
| 7.3 m | 50.3 dBi | 0.50° | 0.96 |
| 10 m | 53.0 dBi | 0.36° | 0.70 |
| 25 m | 61.0 dBi | 0.15° | 0.28 |

**The manual EME row.** Two 3 m dishes give a net loss of 284.1 − 2 × 42.6 = 199.0 dB at mean geocentric distance; 200 dB corresponds to two 2.8 m dishes. The GUI's "40 W, 200 dB" at −164 dBm/Hz gives 10.0 dB-Hz. With two 3 m dishes and a 40 W transmitter, the scalar radar-equation result is `C/N₀ = 28.6, 26.6 and 23.9 dB-Hz` at system temperatures 50, 80 and 150 K respectively (N₀ = −181.6, −179.6 and −176.9 dBm/Hz), before pointing, polarisation and narrow-beam corrections. Here system temperature means the total on-Moon input-referred temperature: lunar antenna noise must already be included in the stated 50/80/150 K. If those numbers instead denote receiver-plus-off-Moon temperatures, add the beam-weighted lunar excess once and recompute; do not count it twice. The GUI default noise density corresponds to a system temperature of about 2,900 K. The 15 kW Helper pilot through the same path at 80 K would be about 52.3 dB-Hz. [41](41-eme-systems-practice-and-environment.md) gives the full station budgets and noise contributions.

### 3.5 Faraday rotation

One-way rotation Ω = 2.36×10⁴ · B∥ · TEC / f² rad, with B∥ = 40 µT, TEC in electrons/m² and f in Hz:

| Frequency | TEC 10 TECU | TEC 100 TECU |
| ---: | ---: | ---: |
| 50 MHz | 37.8 rad (6.0 turns) | 378 rad (60 turns) |
| 144 MHz | 4.55 rad (261°) | 45.5 rad (7.3 turns) |
| 432 MHz | 0.51 rad (29°) | 5.1 rad (290°) |
| 1,296 MHz | 0.056 rad (3.2°) | 0.56 rad (32°) |
| 5,760 MHz | 0.0029 rad (0.16°) | 0.029 rad (1.6°) |

At 5,760 MHz Faraday rotation is negligible for polarisation matching. Its frequency derivative is also negligible across 225 Hz.

## 4. Noise conversions

From section 3.1 of [00](00-method-and-modem-context.md): N0 = −174 + F_a dBm/Hz at the terminals of a lossless reference antenna, and E_n = F_a + 20 log f_MHz − 95.5 dB(µV/m) in 1 Hz for a short vertical monopole. The magnetic flux density uses the plane-wave conversion B = E/c. This is a formal comparison for a propagating plane wave; local reactive fields and Earth–ionosphere modes need their actual E/H impedance, so it does not calibrate a VLF magnetic receiver.

| Frequency | F_a | N0 | E_n | E_n | B_n |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8.27 kHz | 150 dB | −24 dBm/Hz | 12.9 dB(µV/m)/√Hz | 4.4 µV/m/√Hz | 14.6 fT/√Hz |
| 8.27 kHz | 130 dB | −44 dBm/Hz | −7.1 | 0.44 | 1.5 |
| 500 kHz | 90 dB | −84 dBm/Hz | −11.5 | 0.27 | 0.89 |
| 1 MHz | 60 dB | −114 dBm/Hz | −35.5 | 0.017 | 0.056 |
| 1 MHz | 80 dB | −94 dBm/Hz | −15.5 | 0.17 | 0.56 |
| 1 MHz | 100 dB | −74 dBm/Hz | 4.5 | 1.7 | 5.6 |
| 10 MHz | 30 dB | −144 dBm/Hz | −45.5 | 0.0053 | 0.018 |
| 10 MHz | 50 dB | −124 dBm/Hz | −25.5 | 0.053 | 0.18 |
| 30 MHz | 18 dB | −156 dBm/Hz | −48.0 | 0.0040 | 0.013 |
| 30 MHz | 36 dB | −138 dBm/Hz | −30.0 | 0.032 | 0.11 |

Below 9 kHz the "N0 at a lossless antenna" column is formal only: no practical receiving antenna there is lossless, and the field densities are the useful quantities.

## 5. Skywave geometry: mode-delay differences

Mirror model: reflection heights of 110 km for E and 300 km for F (and 250 and 400 km for the last column), spherical earth, no group retardation. Real group paths are longer near the reflection height and depend on the layer profile, so these are orders of magnitude only. Differences in ms:

| Path length | Ground wave to 1F | 1E to 1F | 1F to 2F | 1F to 3F | 1F to 4F | 1F to 5F | 1F at 250 km to 1F at 400 km |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 50 km | 1.84 | 1.26 | 2.00 | 4.00 | 6.00 | 8.00 | 1.00 |
| 100 km | 1.70 | 1.22 | 1.99 | 3.98 | 5.98 | 7.98 | 0.99 |
| 300 km | 1.25 | 1.00 | 1.88 | 3.84 | 5.82 | 7.81 | 0.91 |
| 1,000 km | 0.62 | 0.51 | 1.30 | 2.95 | 4.75 | 6.62 | 0.57 |
| 2,000 km | 0.44 | 0.35 | 0.80 | 1.98 | 3.41 | 5.01 | 0.37 |
| 3,000 km | 0.40 | 0.31 | 0.58 | 1.46 | 2.59 | 3.91 | 0.32 |

In this fixed-height mirror construction, the displayed 7 ms separation occurs only in the short-path higher-order cases; the displayed paths of a few thousand kilometres have separations below about 3–4 ms. This is a result of the selected geometry, not a bound on real skywave modes: variable ionospheric profiles, group retardation, tilted/irregular layers and mixed propagation can change mode delays. Nor does this calculation show that larger observed delay spreads must be scatter; that attribution requires measurement or a richer path model ([31](31-skywave-dispersion-midlatitude-nvis-longhaul.md), [32](32-skywave-dispersion-highlatitude-equatorial.md)).

## 6. Width, coherence and integration conversions

**Doppler-width conversions.** For a Gaussian spectrum with 2σ = B, and a Lorentzian with full width at half maximum Γ:

| Measure | Gaussian | Lorentzian |
| --- | ---: | ---: |
| Full width at half maximum | 1.177 B | Γ |
| Width holding 80 % of the power | 1.282 B | 3.08 Γ |
| Width holding 90 % of the power | 1.645 B | 6.31 Γ |
| Full width at −10 dB | 2.146 B | 3.00 Γ |
| Full width at −20 dB | 3.035 B | 9.95 Γ |
| Correlation time to 1/e | 0.450 / B | 0.318 / Γ |
| Correlation time to 0.5 | 0.375 / B | 0.221 / Γ |

**Two equal, independently fading paths separated by τ.** Frequency correlation of the complex gain has magnitude |cos(πΔfτ)|.

| τ | Correlation 0.5 at | Zero at | Fixed-ray notch spacing | Correlation at 75 / 150 / 225 Hz |
| ---: | ---: | ---: | ---: | --- |
| 0.5 ms | 667 Hz | 1,000 Hz | 2,000 Hz | 0.99 / 0.97 / 0.94 |
| 1 ms | 333 Hz | 500 Hz | 1,000 Hz | 0.97 / 0.89 / 0.76 |
| 2 ms | 167 Hz | 250 Hz | 500 Hz | 0.89 / 0.59 / 0.16 |
| 3 ms | 111 Hz | 167 Hz | 333 Hz | 0.76 / 0.16 / 0.52 |
| 6 ms | 56 Hz | 83 Hz | 167 Hz | 0.16 / 0.95 / 0.45 |
| 7 ms | 48 Hz | 71 Hz | 143 Hz | 0.08 / 0.99 / 0.23 |
| 11 ms | 30 Hz | 45 Hz | 91 Hz | 0.85 / 0.45 / 0.08 |

The correlation between carriers 75 Hz apart, the modem's spacing in the 225 Hz rows, oscillates with τ: it is high at 0.5–2 ms, nearly zero at 7 ms, and high again at about 13 ms. Two discrete paths are the worst case for this periodicity. A continuous delay profile smooths it.

**Coherent-integration loss on a diffusely fading path.** Mean energy collected by a fixed-phase coherent integration of length T on a Rayleigh path with a Gaussian Doppler spectrum of 2σ width B, relative to a static path:

| B·T | 0.1 | 0.3 | 0.5 | 0.8 | 1.0 | 1.2 | 1.5 | 2 | 3 | 5 | 10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss | 0.04 dB | 0.31 | 0.78 | 1.66 | 2.25 | 2.81 | 3.55 | 4.58 | 6.14 | 8.20 | 11.09 |

The 3 dB point is at B·T ≈ 1.3, consistent with the "about 3 dB at roughly 1.2/T" rule used in [00](00-method-and-modem-context.md) section 4.1. These are mean-energy values for the stated Rayleigh model. Its instantaneous energy is exponential: the 10th percentile is −9.77 dB relative to the mean, so a mean about 9.77 dB above a fixed energy threshold gives 90% exceedance in this energy-only model. This is a fade-quantile illustration, not a universal detection probability; actual detection also depends on noise, false-alarm criterion and receiver statistic.

**Time scales of the manual rows.**

| Row | Bit T | 1/T | 1 dB drift 2/T² | About 3 dB spread 1.2/T |
| --- | ---: | ---: | ---: | ---: |
| 1 | 5,760 s | 0.17 mHz | 60 nHz/s | 0.21 mHz |
| 2, 3 | 576 s | 1.7 mHz | 6.0 µHz/s | 2.1 mHz |
| 4, 5, 7 | 11.6 s | 86 mHz | 15 mHz/s | 0.10 Hz |
| 6 | 257 s | 3.9 mHz | 30 µHz/s | 4.7 mHz |
| 8 | 26.4 s | 38 mHz | 2.9 mHz/s | 45 mHz |

## 7. Limits of these calculations

- The ground-wave model is smooth and homogeneous. It contains no mixed paths, terrain, buildings, vegetation, ducting or sea roughness, and no sky wave.
- The antenna model uses assumed capacitance, effective height and losses. It does not include corona, insulator leakage, environmental losses varying with weather, or the waveguide field structure beyond 100 km.
- The lunar model uses assumed scattering laws, a geometric delay–Doppler mapping and Gaussian beams. It contains no regional albedo variation, no polarisation, no ephemeris-driven rotation vector and no measured 5–6 cm data, which [40](40-eme-lunar-scattering-and-geometry.md) reports do not exist in the sources found.
- The skywave delay table uses a mirror model with fixed heights and no group retardation. Its tabulated mode differences do not bound actual multi-hop delays or discriminate discrete modes from scatter. Equal-hop reflection points are imposed geometrically; visibility and ionospheric support must also be checked before treating any row as a physical mode.
- None of these calculations has been compared with a measurement made for this work. Their value is as an independent check on arithmetic and conventions, and as a statement of what simple physics alone predicts.

# Independent cross-check calculations

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Conventions and evidence grades are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **What this is.** Model calculations made for this set, to check numbers in the chapters against an independent implementation. Every value here is graded [E]. None is a measurement, and none replaces a cited source: where a chapter quotes a source and this appendix differs, the source governs unless the difference is explained.
- **How it was made.** The original calculations used standard-library Python scripts. On 13 November 2026 the surviving scripts were recovered and checked using pinned official NTIA C++ routines, a separate complex Airy-root calculation and independent C++ lunar quadrature. Python only orchestrated these small research calculations and compared results. No modem implementation or receiver performance was tested. Reproduction inputs, algorithms, versions and numerical residuals are recorded below; temporary source copies and outputs are not repository dependencies.
- **Related chapters.** groundwave [20](20-groundwave-1mhz.md) and [25](25-groundwave-30mhz.md); sub-9 kHz [10](10-sub9khz-propagation.md), [11](11-sub9khz-antennas-links-and-practice.md) and [12](12-sub9khz-noise.md); Earth–Moon–Earth [40](40-eme-lunar-scattering-and-geometry.md) and [41](41-eme-systems-practice-and-environment.md); skywave [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and [33](33-hf-channel-models-and-simulators.md); noise [12](12-sub9khz-noise.md) and [50](50-noise-and-interference-mf-hf.md).

**Current numerical evidence (13 November 2026).** The earlier statement that the ground-wave source was absent was incorrect: the original `xcheck/airy_gw.py`, `mk_tables.py`, `gw_more.py` and lunar scripts survived in the original research scratch directory. Section 1.1 now independently checks the actual fields, loss thresholds, height and radius effects, and complex conductivity response, rather than only converting fields to losses. Section 3.3 independently checks all eight whole-disc and six beam-limited lunar laws, including every Doppler-width column. [86](86-statistics-and-calculation-checks.md) records the complementary scalar checks. These are checks of the stated mathematical models, not measured path validation. The selected eleven-node lunar law in [84](84-scenarios-eme.md) remains a different law from this appendix's illustrative Hagfors/diffuse mixture.

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

The original calculation reported these internal self-checks: Ai(0) and Ai′(0) to 15 digits; the first ten zeros of Ai and Ai′ to six digits; continuity of the series/asymptotic Airy evaluation to about 5×10⁻⁴ relative at the switch-over radius; |W| → 1 for a perfect conductor at short range (0.999 at 5 km, 0.989 at 20 km at 1 MHz); and flat-earth and spherical-earth results within 0.25 dB at 5–40 km over 3×10⁻³ S/m at 1 MHz.

**Independent reproduction receipt.** Historical printed values were checked against repository revision `280b32e47af6f2066fcc44a8966d6af0b220d706`. The reference is [NTIA LFMF v1.1 at commit `57886e9d0a29fd8f04a6416dc3f782322f5a3589`](https://github.com/NTIA/LFMF/tree/57886e9d0a29fd8f04a6416dc3f782322f5a3589), with its [test-data submodule at `d3cc4d6efff973b1a4fac4dbf3074439aa3246ba`](https://github.com/NTIA/LFMF-test-data/tree/d3cc4d6efff973b1a4fac4dbf3074439aa3246ba). The public routine returns field magnitude, basic loss and a conventional received power; it does **not** return complex phase. The official code uses `a_e = 6370/[1 − 0.04665 exp(0.005577 N_s)]` km. Matching the original radius **6371 × 4/3 km** therefore requires **N_s = 301.1043147623609**, not 301 and not the **301.0199014826777** used for **6370 × 4/3 km** in [82](82-scenarios-groundwave.md). All ground tables below retain their original rounded normalization, `109.54` and `142.0`; the official code uses its own SI constants and rounded 4.77 dBi transmitter gain. This small normalization difference is retained in the comparison.

For complex checking, a separate wrapper evaluated the displayed residue sum using the official `WiRoot(s, …, q, …, AiryKind::WONE, AiryScaling::WAIT)` roots and `Airy(…, WONE, WAIT)` values in place of the original custom Airy/Newton/continuation code. It used `ε_c = ε_r − i σ/(2πf ε₀)`, `G_s(h) = w1(t_s − kh/ν)/w1(t_s)`, `ν = (ka/2)^(1/3)`, and the displayed `exp(+iωt)` convention. Summation at 400 and 800 roots checked truncation. Relative phase is unwrapped sequentially in the listed conductivity order, anchored to the first conductivity at each distance. This is an independent special-function/root implementation of the **same mathematical theory**, not an empirical phase measurement. Taking an absolute value from the public API alone would not have closed the phase check.

The following were actually executed. Residuals against printed tables include their display rounding; they are **numerical comparison results, not physical error bars**.

| Check and full inventory | Result |
| --- | --- |
| Official `LFMF_Examples.csv`: 95 vectors, of which 5 are valid and 90 intentionally invalid | All 95 return codes and all 5 valid solution-method enums matched; all 15 valid field/loss/power values within the published 0.1 dB tolerance; maximum residual 0.047503 dB |
| 144 field-strength and basic-loss pairs in §1.2 | Original rerun reproduces fields within 0.049727 dB of the printed values. Official field vs unrounded original: maximum 0.054745 dB, at 30 MHz/ice/3 km, where the original uses a flat-earth approximation and the reference includes curvature correction. Official vs printed: maximum 0.072896 dB in field and 0.064284 dB in loss |
| 96 cells in the original residue-series region | Independent complex reference vs original: maximum 0.000201 dB in magnitude and 0.000013292 rad in phase; 400→800-root change at most 0.000001913 dB and 0.000000121 rad |
| All 216 loss-threshold distances in §1.3 | Public LFMF, 48 geometric bisections on 0.001–10,000 km; largest distance difference from print 0.805010 km (500 kHz, sea, 140 dB: 1962.805010 vs 1962 km). Maximum final loss residual 3.64×10⁻¹² dB. At the shortest 30 MHz/ice/100 dB cell, 0.753810 km rounds to 0.8 km: its 6.13% displayed relative difference is predominantly coarse distance rounding |
| All 21 radius sensitivities in §1.4 | Independent complex reference vs print: maximum 0.049517 dB; 400/800-root results identical at double precision for these cells. The un-refracted 6371 km case is an explicit mathematical extension, outside the public routine's allowed refractivity interval |
| All 18 conductivity magnitude/relative-phase pairs | Maximum printed residual 0.049296 dB and 0.004939 rad; maximum 400→800-root phase change 2.81×10⁻⁹ rad |
| All 45 antenna-height losses | Public API vs print: maximum 0.061244 dB. Complex reference with original normalization vs print: 0.049253 dB; 400/800-root magnitudes identical at double precision for these cells |
| Four frequency-derivative cases in §1.4 | Independently reproduced; 400/800-root endpoint results identical at double precision. Group delay and its variation are now distinguished explicitly |
| All downstream noise/loss choices and ranges in §1.5 | **11 noise cases × 2 antenna-loss choices = 22 loss targets; two ground classes give 44 distances**, not 24. Exact noise formulas, rather than displayed rounded F_a, give maximum distance difference 0.753059 km |
| 12 paired 1 W/1 kW power-plane checks: 1/30 MHz × sea/3 mS/m land × 30/100/300 km, zero heights, N_s = 301 | Field rises by 30 dB (maximum residual 1.43×10⁻¹⁴ dB); basic loss is invariant (maximum residual 5.69×10⁻¹⁴ dB) |

For a short reproducible public-API check, use vertical polarization, 1 kW, zero heights, the matched refractivity above, and 100 km. Expected `(E, L_b)` in dB are `(68.489835437, 73.496154814)` at 1 MHz over sea, `(37.847171738, 104.138818513)` at 1 MHz over 3 mS/m land, `(39.150484393, 132.377930952)` at 30 MHz over sea and `(−9.972689615, 181.501104961)` at 30 MHz over that land. The full matrix inputs are the ground constants, frequencies, distances and heights printed in §§1.2–1.4. For a threshold, repeatedly replace the lower or upper geometric bracket endpoint according to whether the returned basic loss is below or above the target; retain valid return codes and verify the final loss, not only the distance.

Build used Debian `g++ 14.2.0-19`, `-O2 -std=c++17 -shared -fPIC`, the pinned `include/` directory, a small public-call/complex-sum wrapper, and the source files `LFMF.cpp`, `ValidateInputs.cpp`, `ResidueSeries.cpp`, `FlatEarthCurveCorrection.cpp`, `Airy.cpp`, `WiRoot.cpp`, `wofz.cpp` (seven library translation units), with no source modification. An initial all-source compile lacked the build-system definitions `LIBRARY_NAME`/`LIBRARY_VERSION` needed by `ReturnCodes.cpp` and failed **before any numerical assertions**; compiling the stated computational closure resolved that setup issue. No failing numerical vector was suppressed. For provenance, SHA-256 of the official downloaded archive was `37afe4dc6198fb926d93fb2749adffd4b8d6775a12545c8310851011b6d2ffe6`; the stable individual-source hash for `LFMF.cpp` is `a36f146dcf120d08d2432d12947c8ac89658af4a873724963c52828cd979ae99`, and `LFMF_Examples.csv` is `cb17a9334f3f51a226d1cd025047dd98d0ec0f77533362771cd9e2e24e16f2c2`. The recovered original `airy_gw.py` hash is `d1113c9d7ad6fc854ebefa6f42b09db75436c954a2886b510b6ec9bbf2e1313e`. These pins plus the equations and complete input grids identify the calculation independently of the temporary scratch path. Temporary source copies, wrappers and result files served review of this receipt and were removed after acceptance; this specification and its observed check results are retained.

**Applicability.** Public LFMF input bounds include 0.01–30 MHz, 0–50 m antenna heights, 250–400 N-units and 0.001–10,000 km distance; allowed inputs do not mean every real path in that rectangle is described adequately. The original switch at `x < 0.06` is different from LFMF's switch at `d < 80/f_MHz^(1/3)` km and its curvature-corrected flat solution. P.368-9's GRWAVE exponential atmosphere, P.368-10's linear-atmosphere formulation and this constant-effective-radius LFMF calculation must not be called identical physics; see [20](20-groundwave-1mhz.md). Agreement supports arithmetic and the selected smooth-earth model. It does not validate real terrain, mixed-media paths, surface roughness, temporal conductivity fluctuations or an operating receiver.

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

**Effective earth radius.** Changing the factor from 4/3 to 1 (no atmospheric refraction) lowers the field by the following amounts, in dB. It matters at 30 MHz and beyond a few hundred kilometres at MF. These are constant-radius sensitivity controls: P.368-9 GRWAVE uses an exponential atmosphere, while P.368-10 describes a linear-atmosphere formulation. Neither is reproduced merely by choosing one constant radius.

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

A halving of conductivity between 10⁻³ and 5×10⁻⁴ S/m changes this calculated 100 km field by 2.5 dB and its phase by 0.34 rad. The few-microhertz figure follows only if that phase difference evolves smoothly over hours; it is not an observed phase trajectory or a demonstrated bound on within-message frequency error. The calculation also does not establish that amplitude is the receiver-relevant effect: that depends on the temporal law, path, reference phase, and modem response. Chapter 20's seasonal amplitude observations have their own frequency, path, and estimator limits. The independent phase calculation above closes the numerical reproduction question. It does not supply a measured temporal law, so an amplitude-only time-varying scenario still needs its own physical justification.

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

In this smooth-earth calculation over land at 30 MHz, raising both antennas from ground level to 30 m reduces the computed loss by 24–33 dB. The calculation gives a small loss increase over seawater. The independent public API and complex-root calculations reproduce that sign, so it is a property of this selected smooth-surface solution rather than an isolated implementation discrepancy. It is not a general prediction for a raised real seawater antenna: direct/reflected space waves, terrain and actual antenna coupling require their own calculation. The table alone does not isolate a unique physical mechanism for a measured height dependence.

**Frequency selectivity across a 3 kHz channel.** For 3×10⁻³ S/m land, evaluate the independent complex W at `f₀ ± 1500 Hz`. The signed amplitude tilt is `20 log₁₀|W₊/W₋|`; the phase-secant group delay is `−arg(W₊/W₋)/(2π·3000)`. To measure **variation of group delay**, estimate the local derivative at each endpoint with an additional ±10 Hz central difference and subtract those two delays. The original prose confused a group-delay value with its variation.

| Centre frequency; distance | Amplitude tilt across 3 kHz | Secant group delay of W | Endpoint-to-endpoint change of local group delay |
| --- | ---: | ---: | ---: |
| 1 MHz; 100 km | −0.057046 dB | −0.028002 µs | −0.00011707 µs |
| 1 MHz; 300 km | −0.068316 dB | +0.023191 µs | −0.00011381 µs |
| 30 MHz; 30 km | −0.00096731 dB | +0.00016351 µs | +0.00000004947 µs |
| 30 MHz; 100 km | −0.00143302 dB | +0.00256629 µs | −0.00000008845 µs |

These delays describe the **attenuation function**, with the common propagation delay `d/c` removed; a negative entry is not a negative arrival time. The 30 MHz central derivative evaluates the analytic residue model just above 30 MHz as well as below; it is not an out-of-range public-LFMF acceptance claim. Within these four smooth, homogeneous-earth cases the calculated response changes little across 3 kHz. Mixed media, terrain, roughness, layering, skywave, antennas and receiver response are outside this comparison; the result does not establish that every real ground-wave path is flat.

### 1.5 What the GUI's "180 dB at −164 dBm/Hz" corresponds to on a ground-wave path

From section 3.1 of [00](00-method-and-modem-context.md), a GUI path loss of 180 dB against −164 dBm/Hz reproduces a real link's C/N0 when L_b + F_a + X = 193.5 dB, where X is the transmitting antenna's loss relative to an ideal short monopole. The table gives the corresponding L_b and the smooth-earth distances, for X = 0 and X = 20 dB. Man-made F_a are the ITU-R P.372 median formulas (c − d log f_MHz: quiet rural 53.6/28.6, rural 67.2/27.7, city 76.8/27.7); galactic 52 − 23 log f_MHz; the night atmospheric values are the 1 MHz range given in [50](50-noise-and-interference-mf-hf.md). This assumes the external noise stays above the receiver's own noise after receive-antenna losses. The field/available-power plane is the total field at a grounded, lossless short vertical receiving antenna: `P_av = E_dBµV/m − 20 log₁₀(f_MHz) − 78.5 dBm`, with effective aperture `0.75 λ²/(4π)`. It is not the public LFMF `P_rx` convention; see §4. Solve ranges with unrounded `F_a` and `L_b = 193.5 − F_a − X`; displayed rounded loss targets are labels, not the inversion inputs. These are C/N₀ equivalences under the stated assumptions, not communication-success ranges.

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

### 1.6 Mixed-path reference control

A bounded additional calculation addresses the 1.242 MHz, 19 km land plus 71 km sea profile discussed in [20](20-groundwave-1mhz.md), sourced there to Nishioka, Inasawa and Miyashita (2014). For this **control**, use the same pinned LFMF, 1 kW, zero heights, `N_s = 301.0199014826777`, land `(σ, ε_r) = (0.001, 15)` and sea `(5, 70)`. For total distance `d > l = 19 km`, let `E_L(d)` and `E_S(d)` be the homogeneous fields in dB(µV/m). The reciprocal Millington estimate is

    E_mix(d) = [E_L(l) + E_S(d) − E_S(l)
                + E_S(d−l) + E_L(d) − E_L(d−l)] / 2.

At `d = l`, use the homogeneous land value without calling a zero-distance solver.

| Distance | Homogeneous land | Homogeneous sea | 19 km land then sea, Millington |
| --- | ---: | ---: | ---: |
| 19 km | 57.326177 | 83.855695 | 57.326177 |
| 30 km | 49.028822 | 79.788803 | 61.888994 |
| 50 km | 39.626383 | 75.125498 | 59.642799 |
| 90 km | 28.318421 | 69.442402 | 55.027506 |

All entries are calculated field strengths in dB(µV/m). The initial recovery after the coastline is part of this reciprocal mixed-path construction. This is not a reproduction of the paper's exponential-atmosphere GRWAVE calculation, an absolute fit to its unknown receiving gain, or measured validation of this constant-radius implementation.

A same-input comparison also applies the **equivalent-distance algorithm** described in FCC 47 CFR **73.183(d)–(e)**, whose propagation curves are in 73.184. At the interface, find an effective sea distance `x` for which `E_S(x) = E_L(19 km)`, then continue along the sea curve for the physical sea-segment length. Reversing the path instead matches the first sea-segment field to a distance on the land curve, then adds 19 km. The algorithm keeps the interface field continuous and is not constructed to be reciprocal. [Official 2025 text, including 73.183(d)–(e) immediately before 73.184](https://www.govinfo.gov/content/pkg/CFR-2025-title47-vol4/pdf/CFR-2025-title47-vol4-sec73-184.pdf).

For the **same LFMF curves and constants as above**, the forward equivalent sea distance at the first boundary is **246.301119318 km**, with field **57.326176758 dB(µV/m)**. Field inversion used 48 geometric bisections over 0.001–10,000 km, with all final interface residuals below 10⁻⁹ dB.

| Total path | Reciprocal Millington | Equivalent distance, land→sea | Equivalent distance, reversed sea→land | Forward minus reverse |
| --- | ---: | ---: | ---: | ---: |
| 19 km, land only | 57.326177 | 57.326177 | 57.326177 | 0 dB |
| 30 km | 61.888994 | 56.661127 | 54.655940 | 2.005187 dB |
| 50 km | 59.642799 | 55.479029 | 52.792259 | 2.686770 dB |
| 90 km | 55.027506 | 53.199046 | 50.632386 | 2.566660 dB |

The first three numerical columns are fields in dB(µV/m). Reversing the segments leaves the Millington expression unchanged, whereas equivalent-distance stitching changes these fields by 2.0–2.7 dB. This controlled comparison isolates the stitching algorithm: it is **not an official FCC curve prediction**, because those curves specify different normalization and sea permittivity (80 rather than this calculation's 70), among other model choices. It also does not infer conductivity or calibrate an absolute link from the relative Japanese measurements.


## 2. A 61 m antenna below 9 kHz

### 2.1 Assumptions

- **Capacitance.** A bare vertical wire, height h = 60.96 m and radius a: C = 2πε₀h / (ln(h/a) − 1), giving 339 pF for a = 1 mm, 403 pF for 5 mm and 786 pF for a 0.3 m tower-like radius. "Bare wire" below uses 340 pF; "top-loaded" assumes 1 nF, an illustrative value for a modest top hat.
- **Effective height.** h/2 = 30.5 m for the bare wire; 50 m assumed for the top-loaded case.
- **Radiation resistance and power plane.** R_r = 160π²(h_e/λ)², for a short monopole over perfect ground, with **rms base current** I. Radiated power I²R_r is then numerically the e.m.r.p. This reference has directivity 3; if a source explicitly means e.r.p. relative to a half-wave dipole, the conversion is `(3/1.64) P_rad`, about +2.62 dB. An unexplained source label “ERP” is not enough to choose that conversion. Antenna input power includes the stated loading and ground losses, and is not radiated power.
- **Losses.** Loading-coil Q of 300 (loss X_C/300), plus 60 Ω of ground and environment loss. Both are assumptions; real values vary widely ([11](11-sub9khz-antennas-links-and-practice.md), [55](55-equipment-and-front-end-effects.md)).
- **Field.** E = 120π I h_e/(λ d) is the rms inverse-distance radiation field at 100 km. Together with R_r this gives `E² = 90 P_emrp/d²` in SI units, hence 300 mV/m at 1 km for 1 kW, consistent with §1. This is a radiation-field reference, not a VLF waveguide solution. The parenthesized magnetic fields in §2.3 use the additional plane-wave relation `B = E/c`; Earth–ionosphere modes and local/reactive fields need their actual vector field impedance. [10](10-sub9khz-propagation.md) supplies the propagation treatment. The capacitance/effective-height/loss assumptions here are not automatically the selected antenna law in [81](81-scenarios-sub9.md).

The selected circuit branch in [81](81-scenarios-sub9.md) instead specifies a 61 m vertical with a 122 m horizontal top wire, 2 mm wire diameter, **C₀ = 0.902 nF, h_e = 49.8 m, coil Q = 400, ground resistance 30 Ω and tanδ = 0.003**. This appendix's generic top-loaded example uses 1 nF, 50 m, Q = 300 and a combined 60 Ω ground/environment allowance. Those are different circuit inputs, so its current, voltage, efficiency and antenna-input power rows must not be copied into 81 without recalculation. A common 200 ft height does not establish the same current moment or radiated power. Modal field fits in 81 also retain their cited electric/magnetic component and power convention; the inverse-distance radiation reference here does not determine those transfers.

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

### 2.4 Formal field/noise comparisons at 100 km

The fields of section 2.3 are compared with four noise field densities at 8.27 kHz: the minimum and maximum of the ITU-R P.372 below-10 kHz figure as digitised in [12](12-sub9khz-noise.md) (F_a 147.8 and 174.7 dB, giving 3.4 and 75 µV/m/√Hz by E_n = F_a + 20 log f_MHz − 95.5), and the Stanford 8 kHz monthly mean envelope densities of 7.5 and 38 fT/√Hz reported there, converted by E = cB to 2.25 and 11.4 µV/m/√Hz. The Stanford values are mean envelopes, not rms, and the magnetic receiver observes a different field component from this ideal electric monopole. Thus the two Stanford columns are **formal plane-wave/envelope comparisons**, not calibrated available-power C/N₀. Even a statistical envelope-to-rms conversion would not establish the mode impedance, directional noise distribution or receiving-antenna calibration. The P.372 columns likewise retain the specified ideal antenna and digitized noise assumptions; no actual 100 km link was measured here.

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

A grid deviation of a few tens of millihertz can move a harmonic by several hertz, but the nearest nominal lines begin **20 Hz below** and **10 Hz above** 8270 Hz. An actual crossing requires the 50 Hz grid to reach `8270/165 = 50.1212121212 Hz`, or the 60 Hz grid to reach `8270/138 = 59.9275362319 Hz`. Wander smaller than those required signed offsets never reaches the carrier in this ideal harmonic model.

For a locally linear grid-frequency rate `α` in Hz/s, a present harmonic moves at `nα`; its residence time across occupied width B is `B/|nα|`, conditional on the trajectory actually crossing. For example, **assumed**, not measured, `|α| = 0.001 Hz/s` and `B = 0.0625 Hz` give 0.3788 s for n = 165 and 0.4529 s for n = 138. Moving a harmonic by 1 Hz over 60 s instead requires grid rates of 0.00010101 and 0.00012077 Hz/s respectively. Offsets alone establish neither occurrence probability nor a “within minutes” crossing. Those occurrence claims are withdrawn; a measured line-presence/trajectory record is needed to estimate them. A tracked comb and a fixed notch are distinct controls whose effect depends on their actual tracking bandwidth and the observed trajectory.

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

The echo is integrated over the visible hemisphere with an assumed **cross-section per unit physical surface area**,

    σ⁰(φ) = (1 − f_d) k_q (C/2)(cos⁴φ + C sin²φ)^(−3/2)
            + f_d k_d cosⁿφ.

The quasi-specular and diffuse components are separately normalized so that f_d is the diffuse fraction of whole-disc power. This fixes the area convention: a law quoted per projected area needs conversion before substitution. A surface point has excess delay `(2a/c)(1 − cos φ)` and a Doppler offset proportional to its projected displacement from the rotation axis. All Doppler widths below are fractions of the full limb-to-limb width LL. The parameters are illustrative spans, **not fitted lunar measurements**, and no OC/SC polarization ratio is inferred from this scalar law. [40](40-eme-lunar-scattering-and-geometry.md) distinguishes the measured angular, wavelength and polarization conventions.

**Independent integral and convergence check.** Set `u = cos φ`, `r = sqrt(1−u²)` and `T = 2a/c = 11.5906851799454 ms`. Define `Q(u) = (C/2)[u⁴ + C(1−u²)]^(−3/2)` and `D(u) = uⁿ`. Normalize each by its integral on `0≤u≤1`; their mixture is `p(u) = (1−f_d)Q/∫Q + f_d D/∫D`. This integration measure follows directly from spherical surface area `dA ∝ du dθ`.

For the same circular Gaussian power beam at both ends, let `h = HPBW / lunar angular diameter` and multiply the mixture by **`exp[−2 ln(2)(1−u²)/h²]`**, the product of the two one-way power gains. Denote the resulting law by `p_h` and its integral by Z. No beam means weight 1 and Z = 1. Reproduction equations are:

    S(x) ∝ 2 ∫₀^(π/2) p_h(sqrt(1−x²) cos θ) dθ,  −1≤x≤1
    FWHM/LL: solve S(x)/S(0) = 0.5, x≥0
    −10 dB width/LL: solve S(x)/S(0) = 0.1
    (2 rms Doppler width)/LL = sqrt[∫₀¹ (1−u²)p_h(u)du / (2Z)]
    F(x) = ∫₀¹ p_h(u) A(x,sqrt(1−u²))du / Z
    A(x,r) = 1 if r≤x, otherwise (2/π) asin(x/r)
    Central p-power width/LL: solve F(x) = p
    Delay moments: E[t^k] = ∫₀¹ [T(1−u)]^k p_h(u)du / Z
    P(t<t₀) = ∫_(1−t₀/T)^1 p_h(u)du / Z
    Relative echo power = 10 log₁₀ Z.

Here Doppler frequency is `ν = (LL/2)x`; a symmetric interval `−x…x` therefore has full width `x·LL`. The “rms delay” column is the standard deviation about the mean, not `sqrt(E[t²])`. To avoid an endpoint singularity in the percentile integral, split it at `u = sqrt(1−x²)` before quadrature.

A separate C++ Gauss–Legendre implementation evaluated **all 14 laws and all six Doppler-width definitions (84 quantities)**, plus 14 sets of delay moments, five delay fractions and relative power. It used 1024 then 2048 quadrature nodes, 55 bisection steps for spectral widths and 48 for central-power widths. Maximum refinement change among all 196 stored scalar outputs was **4.42×10⁻⁸** (a normalized central-power width for the narrowest beam); this is an observed quadrature refinement result, not a measurement uncertainty. The original angular histogram/trapezoid code was not reused. Two independent analytic limits also passed: diffuse-only `n=0` gives a flat Doppler spectrum, widths 1/1, `2 rms/LL = 1/√3`, central fractions equal to x, mean delay T/2 and rms delay T/√12; diffuse-only `n=1` gives `S(x) ∝ sqrt(1−x²)`, FWHM `√3/2`, −10 dB width `√0.99` and `2 rms/LL = 1/2`. At 512 nodes the largest listed analytic-percentile discrepancy was 1.04×10⁻⁹; analytic widths/moments agree to numerical precision. The quadrature implementation used `g++ 14.2.0-19`, `-O2 -std=c++17 -shared -fPIC`; SHA-256 of its temporary `moon.cpp` was `1448d7bd1f7e1f7980b706383161fcf787c4e0a8aad1d72200ede69262731341`. The equations and all inputs here are the durable reproduction specification.

**Whole-disc illumination**

| Law: C, f_d, n | FWHM | −10 dB width | 2 × rms | Width with 50% of power | 80% | 90% | Mean delay | rms delay | Power within 0.1 / 0.5 / 1 / 2 / 5 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 150, 0.10, 1.5 | 0.085 | 0.284 | 0.273 | 0.091 | 0.286 | 0.492 | 1.117 ms | 2.158 ms | 0.429 / 0.677 / 0.758 / 0.830 / 0.925 |
| 95, 0.00, 1.5 | 0.105 | 0.327 | 0.267 | 0.099 | 0.275 | 0.459 | 1.075 ms | 2.164 ms | 0.386 / 0.680 / 0.775 / 0.851 / 0.930 |
| 95, 0.20, 1.5 | 0.111 | 0.449 | 0.318 | 0.127 | 0.387 | 0.591 | 1.523 ms | 2.401 ms | 0.313 / 0.565 / 0.661 / 0.756 / 0.895 |
| 65, 0.25, 1.5 | 0.140 | 0.631 | 0.344 | 0.158 | 0.441 | 0.634 | 1.783 ms | 2.520 ms | 0.241 / 0.492 / 0.599 / 0.710 / 0.876 |
| 65, 0.25, 1.0 | 0.139 | 0.647 | 0.354 | 0.160 | 0.458 | 0.658 | 1.921 ms | 2.678 ms | 0.240 / 0.487 / 0.590 / 0.695 / 0.856 |
| 40, 0.35, 1.0 | 0.196 | 0.899 | 0.393 | 0.213 | 0.533 | 0.711 | 2.375 ms | 2.822 ms | 0.156 / 0.379 / 0.490 / 0.614 / 0.817 |
| 40, 0.50, 1.0 | 0.226 | 0.956 | 0.420 | 0.253 | 0.583 | 0.743 | 2.718 ms | 2.870 ms | 0.124 / 0.311 / 0.415 / 0.545 / 0.785 |
| 20, 0.50, 1.0 | 0.365 | 0.987 | 0.439 | 0.290 | 0.608 | 0.760 | 2.980 ms | 2.907 ms | 0.077 / 0.244 / 0.358 / 0.502 / 0.764 |

**Beam-limited illumination.** C = 40, f_d = 0.35, n = 1. The exact inputs here are the **rounded ratios h = 2.34, 1.41, 0.96, 0.70, 0.28, 0.11** inherited from the original calculation. The dish/beamwidth labels approximately follow `HPBW = 70λ/D` degrees at 5760 MHz; substituting the unrounded diameter formula gives a slightly different calculation. Relative power compares to whole-disc illumination at the **same on-axis gain**, not to a smaller dish's absolute received power.

| Approximate dish (beamwidth); exact h | FWHM | −10 dB width | 2 × rms | Width with 50% / 80% / 90% of power | Mean delay | rms delay | Power within 0.1 / 0.5 / 1 ms | Relative power |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| 3 m (1.21°); 2.34 | 0.190 | 0.837 | 0.377 | 0.199 / 0.503 / 0.684 | 2.162 ms | 2.695 ms | 0.168 / 0.405 / 0.522 | -0.326 dB |
| 5 m (0.73°); 1.41 | 0.182 | 0.721 | 0.351 | 0.178 / 0.452 / 0.635 | 1.831 ms | 2.465 ms | 0.188 / 0.450 / 0.574 | -0.836 dB |
| 7.3 m (0.50°); 0.96 | 0.170 | 0.570 | 0.308 | 0.151 / 0.375 / 0.546 | 1.357 ms | 2.048 ms | 0.222 / 0.524 / 0.658 | -1.593 dB |
| 10 m (0.36°); 0.70 | 0.158 | 0.449 | 0.253 | 0.126 / 0.294 / 0.430 | 0.868 ms | 1.462 ms | 0.271 / 0.620 / 0.761 | -2.491 dB |
| 25 m (0.15°); 0.28 | 0.115 | 0.241 | 0.118 | 0.072 / 0.145 / 0.193 | 0.164 ms | 0.210 ms | 0.523 / 0.935 / 0.990 | -5.821 dB |
| 64 m (0.057°); 0.11 | 0.066 | 0.122 | 0.057 | 0.038 / 0.073 / 0.094 | 0.038 ms | 0.040 ms | 0.923 / 1.000 / 1.000 | -10.850 dB |

The independent integrals correct several older histogram/rounding entries, for example the 10 m mean delay from 0.88 to **0.868 ms** and the 25 m mean from 0.17 to **0.164 ms**. More materially, the earlier prose saying the 25 m example placed half its power within **0.5 ms** was wrong: it places **52.294% within 0.1 ms** and **93.523% within 0.5 ms**.

The modeled spectrum is strongly peaked with substantial tails. Whole-disc FWHM spans 8.46–36.55% of LL, whereas its central 90%-power width spans 45.92–75.98%. These width definitions must accompany any number. The modeled delay profile extends continuously from 0 to 11.59 ms; whole-disc power within 0.5 ms spans 24.45–67.97%, and power beyond 5 ms spans 7.03–23.63%. A narrower beam concentrates the modeled delay and Doppler distributions and reduces their whole-disc-relative power. This is an integrated result for the assumed law, not evidence that a measured Moon follows that law.

**The “EME focused 6 Hz 0.5 ms” preset.** Condition on LL = 86 Hz, C = 40, f_d = 0.35, n = 1, identical Gaussian beams, a 0.518° lunar diameter and the **unrounded** `70λ/D` beam formula. Solving the integrals for a 6 Hz width gives **58.93 m for FWHM**, **50.09 m for twice rms**, or **91.45 m for central 90%-power width**. The unrounded solutions were rechecked at 2048 nodes (the last refined to 91.44561173 m), with maximum residual from 6 Hz below 3×10⁻¹⁰ Hz. Thus the old “about 60 m” estimate is specifically a FWHM comparison; there is no unique dish equivalent to an undefined 6 Hz spread. These three solutions have mean delays 0.04376, 0.05672 and 0.02090 ms respectively, and fractions within 0.5 ms of 0.999948, 0.999462 and more than 0.999999999. A 0.5 ms support, rms spread, mean or percentile is also a different quantity in each case. A Gaussian modem spread generally specified as twice rms must be compared with that same definition, while retaining the non-Gaussian lunar shape. Neither this calculation nor matching one width establishes an equivalent joint delay–Doppler channel, a calibrated K1JT estimator output, or an empirical preset. The ephemeris, scattering, polarization and estimator choices in [40](40-eme-lunar-scattering-and-geometry.md), [41](41-eme-systems-practice-and-environment.md) and [84](84-scenarios-eme.md) remain necessary.

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

For a two-leg 5760 MHz calculation, use the **slant** electron-column integral on each leg and the signed magnetic projection in a consistent propagation/polarization basis. The table above replaces that integral by a constant 40 µT times TEC. Without assuming cancellation, the magnitude of the combined rotation is bounded by the **sum of the two leg magnitudes**. The following conditional bounds assume each leg has the listed TEC and `|B∥|≤40 µT` throughout that column.

| TEC on each leg | Bound on combined rotation | Additional loss for initially aligned linear antennas | Bound on rotation change across 225 Hz |
| --- | ---: | ---: | ---: |
| 10 TECU | 0.00569059 rad (0.32605°) | 0.00014064 dB | 4.446×10⁻¹⁰ rad |
| 100 TECU | 0.05690586 rad (3.26047°) | 0.0140713 dB | 4.446×10⁻⁹ rad |
| 1000 TECU, stress assumption | 0.56905864 rad (32.6047°) | 1.48954 dB | 4.446×10⁻⁸ rad |

The linear-antenna entry is `−20 log₁₀ cos(2|Ω_leg|)` and presumes alignment before Faraday rotation, no other polarization mixing and a bound below π/2; a pre-existing near-orthogonal mismatch can be much more sensitive. For a band of full width Δf centered at f, the exact worst-signed two-leg change under the same frequency-independent column assumptions is bounded by `4|Ω_leg| (Δf/f) / [1 − (Δf/2f)²]²`. The 225 Hz entries use that expression; its leading derivative term `4|Ω_leg|·225/f` is indistinguishable at the displayed precision. The 1000 TECU row is a selected stress calculation, not an assigned occurrence rate.

For ideal circular polarization, a pure uniform Faraday rotation has Jones eigenvalues `exp(±iΩ)`: it changes phase but not power in a matching circular basis. This does not remove lunar cross-polar scattering, leakage, spatially varying rotation or basis/handedness changes at reflection. Thus **differential Faraday rotation across the narrow channel** is bounded and very small under these assumptions; **total linear-polarization mismatch** is small for the 10/100 TECU examples, potentially material for the 1000 TECU control, and not universally negligible. Exact two-leg geometry and the receiver polarization convention must be supplied to make a specific link claim.

## 4. Noise conversions

From section 3.1 of [00](00-method-and-modem-context.md): N0 = −174 + F_a dBm/Hz at the terminals of a lossless reference antenna, and E_n = F_a + 20 log f_MHz − 95.5 dB(µV/m) in 1 Hz for a short vertical monopole. The field relation is the **total field** at a grounded short vertical monopole, whose receiving effective aperture is `0.75λ²/(4π)` in this convention ([00 §3.1.1](00-method-and-modem-context.md#311-grounded-monopole-reference-plane-check), following P.341 Annex 3). With exact SI constants at 290 K, `P_av = E_dBµV/m − 20 log₁₀(f_MHz) − 78.46538 dBm` and the 1 Hz noise-field constant is −95.50981; the rounded −78.5/−95.5 forms above are consistent approximations. In contrast, the pinned LFMF implementation reports `P_rx = E_dBµV/m − 20 log₁₀(f_MHz) − 72.43`: this differs by **6.07 dB** from the rounded grounded-total-field formula, or **6.03538 dB** from its SI form. The ideal receiving-aperture ratio 3/0.75 alone is **6.02060 dB**; the residual reflects the code's rounded constants. Do not insert that API power directly into the P.372 grounded-antenna noise plane. [82](82-scenarios-groundwave.md) makes the same separation. The magnetic flux density uses the plane-wave conversion B = E/c. This is a formal comparison for a propagating plane wave; local reactive fields and Earth–ionosphere modes need their actual E/H impedance, so it does not calibrate a VLF magnetic receiver.

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

The correlation oscillates for **a fixed two-ray spacing**, separately at each carrier separation. It is not simultaneously small at all three separations: for example the 7 ms row gives 0.07846 at 75 Hz but 0.98769 at 150 Hz. The earlier general statement “nearly zero at 7 ms” was therefore too broad.

A precise extremum is available for a defined channel class. Let P be a nonnegative normalized delay-power measure supported in an interval of width D, and assume uncorrelated scattering so that `ρ(Δf) = ∫ exp(−i2πΔfτ) dP(τ)`. For `|Δf|D ≤ 1/2`, rotate the interval midpoint to phase zero. Every phasor then has real part at least `cos(π|Δf|D) ≥ 0`, so

    |ρ(Δf)| ≥ cos(π|Δf|D),       |Δf|D ≤ 1/2.

Two equal power masses at the interval endpoints attain the bound. For `|Δf|D ≥ 1/2`, the infimum is **zero**, attained by two equal masses separated by `1/(2|Δf|)` inside the interval. Hence the complete minimum over this class is the cosine bound up to the first zero and zero thereafter; it does **not** oscillate back upward. As a counterexample to calling endpoint rays universally worst: with D = 7 ms and Δf = 150 Hz the endpoints give 0.98769, while rays separated by 3.3333 ms give zero; at 75 Hz a 6.6667 ms separation likewise gives zero.

This proves a scoped mathematical bound and identifies its attaining profiles. It does not extend to arbitrary correlated taps, complex signed cross-power, fixed additional moments, simultaneous optimization at several Δf, or decoder error probability. A continuous delay profile can smooth a two-ray periodic pattern but is not automatically better for every receiver or carrier spacing.

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
- The lunar model uses assumed scattering laws, a geometric delay–Doppler mapping and Gaussian beams. The integrals are now independently reproduced, but contain no regional albedo variation, polarization or ephemeris-driven rotation vector. No measurement was fitted to these illustrative parameters. The available wavelength-specific measurements and public products, including their transfer limits, are assessed separately in [40](40-eme-lunar-scattering-and-geometry.md) and [84](84-scenarios-eme.md); absence of a fit here must not be represented as absence of all relevant lunar data.
- The skywave delay table uses a mirror model with fixed heights and no group retardation. Its tabulated mode differences do not bound actual multi-hop delays or discriminate discrete modes from scatter. Equal-hop reflection points are imposed geometrically; visibility and ionospheric support must also be checked before treating any row as a physical mode.
- Numerical reproduction of the displayed ground and lunar quantities, the antenna/power crosswalk, conditional harmonic/Faraday bounds and scoped correlation extremum are completed here. Empirical calibration of those laws, measured temporal statistics, native simulator implementation and communication-success testing are separate work. None of these calculations was compared with a measurement made for this work; their value is arithmetic/convention checking and explicit conditional physical predictions.

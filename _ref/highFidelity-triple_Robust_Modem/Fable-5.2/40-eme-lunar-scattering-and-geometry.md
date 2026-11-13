# Earth–Moon–Earth: lunar scattering and path geometry

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** systems, practice and environment [41](41-eme-systems-practice-and-environment.md); equipment [55](55-equipment-and-front-end-effects.md); simulation numerics [60](60-simulation-methods-and-validation.md); independent arithmetic [90](90-cross-check-calculations.md).

**Summary.** The Moon is a limb-darkened, apparently slowly rotating, incoherent scatterer. Its total radar cross-section is about 0.065–0.07 of its projected area from 1 cm to 1 m (best value 0.065 ± 0.008 at 23 cm; no directly applicable measurement at 5–6 cm was found, so 5.8 GHz is interpolated between 3.0–3.8 cm and 10–23 cm data). This gives a two-way isotropic path loss at 5760 MHz of 282.8 / 284.1 / 285.1 dB at perigee / mean / apogee, matching the amateur figure of 284.1 dB. The echo is the sum of a quasi-specular highlight (Hagfors law, C ≈ 40–45 near 4–5 cm) and a diffuse, depolarising component (about 30–35 % of the power at 3.6–3.8 cm). Integrating the Lincoln Laboratory 3.8-cm and 23-cm scattering laws and interpolating gives, for 5.2 cm and whole-disc illumination: half the power within about 0.75 ms of the leading edge, 10 % later than 5 ms, rms delay spread about 2.2 ms, full extent 11.6 ms (Evans' measured 68-cm pulse-length curve supports this integration; the amateur half-power delays derived from Hagfors (1970), about 30–45 % shorter, appear to carry an extra projected-area weighting). The limb-to-limb Doppler spread of self-echoes at 5760 MHz is typically 65–100 Hz (median, 40–52° latitude), up to 165 Hz at 40° and about 200 Hz at the equator, with brief near-zero minima; half the power lies within a band of about 16–19 Hz (the published amateur width w50 = 25 Hz uses a broader, autocorrelation-based definition). Rayleigh fading is a useful supported model, with estimated field coherence times of about 6–17 ms in normal conditions. The mean Doppler shift reaches ±16 kHz (40° latitude) to ±20 kHz (equator) and drifts at up to 0.95–1.25 Hz/s. The modem preset "EME focused: 6 Hz, 0.5 ms" pairs a whole-disc delay value with a Doppler value that occurs only near libration minima or with dishes of roughly 25 m and larger, and it omits the Rayleigh statistics and the long diffuse tail. For the stated brightness model, receivers about 20 m apart at 5.8 GHz have low zero-lag field correlation (field correlation 0.5 at about 6.5 m and about 0.1 at 20 m, consistent with the speckle scale used in planetary radar speckle tracking), and the amateur WSJT-X simulators represent the channel as one flat-fading tap with a truncated Lorentzian spectrum. Items not verified are listed at the end.

Evidence grades: [S] standard, [P] research paper or preprint (peer-review status checked separately), [T] technical report, [B] book, [G] grey literature, [E] own derivation. Access status is given per source in the References. Source keys such as [EV65] refer to the References. All [E] numbers were computed with small pure-Python scripts; formulas and inputs are stated where used.

## Physical mechanisms

### Radar equation for an extended target

For antennas whose beams are much wider than the Moon, the received power is P_r = P_t G_t G_r λ² σ / ((4π)³ d⁴), with σ = η π a², a = 1737.4 km the lunar radius and η the cross-section as a fraction of the projected disc. Equivalently, the two-way path loss between isotropic antennas is L = (4π)³ d⁴ / (η π a² λ²). Evans writes the same quantity in a wavelength-independent form, σ/(4πR²)² ≈ 1.95 × 10⁻²⁵ m⁻² (247.2 dB/m²) for η = 0.07 and R = 3.844 × 10⁸ m, i.e. the loss between an antenna of unit gain and one of unit aperture ([EV65] p. 1640, [P]). The cross-section so defined applies only when the whole disc is illuminated and all echo delays and Doppler components are accepted. Evans gives the general expression for a beam comparable with the disc, P_r = (P_t G_0² λ² σ / (64 π³ R⁴)) ∫ F²(θ) B(θ) 2π sin θ dθ, where F is the one-way antenna power pattern and B the normalised brightness distribution of the disc ([EV65] p. 1638, Eq. 2, [P]). Because the Moon is brightest at the centre, he states that little power is lost until the half-power beamwidth is smaller than the lunar diameter (½°), which for simple antennas means a diameter above about 100 λ ([EV65] p. 1638, [P]). At 5.8 GHz, 100 λ is 5.2 m; with HPBW ≈ 70 λ/D degrees the beam equals the 0.49–0.57° disc at D ≈ 6.4–7.4 m [E].

The echo fades because contributions from different parts of the surface interfere, so the cross-section must be obtained from the mean square echo amplitude over many independent fades; some early workers reported only the maximum echo ([EV65] p. 1638, [P]).

### Two scattering components

At all radio wavelengths the echo has (i) a quasi-specular component from surface facets tilted to face the radar, concentrated near the sub-radar point and preserving polarisation (reversing the sense of circular polarisation), and (ii) a diffuse component from wavelength-sized structure (rocks on and in the regolith), nearly uniformly distributed over the disc and strongly depolarising. The division shifts toward the diffuse component as wavelength decreases ([EV65] pp. 1655–1656; [EV69] pp. 222–226; [LL67] pp. 2–3, [P]/[T]).

The quasi-specular component follows the Hagfors law for a surface with an exponential height autocorrelation. Per unit surface area, σ₀(φ) = (C ρ₀ / 2) (cos⁴φ + C sin²φ)^(−3/2), where φ is the angle of incidence, ρ₀ the normal-incidence power reflectivity and C = (d₁ λ / 4π h₀²)² ([LL67] p. 18, Eq. 33, [T]; [EV69] Fig. 12 and Eq. 14, [P]). The effective rms slope is approximately 1/√C ([EV69] p. 222, [P]). The diffuse component follows cosⁿφ with n ≈ 3/2 for φ > 50° at 3.6–600 cm, and n ≈ 1 at 8.6 mm ([EV69] p. 222 and Fig. 11, [P]). The depolarised (same-sense circular) component follows cos φ, i.e. a uniformly bright disc ([EV65] Fig. 12; [LL67] p. 9; [ZPC74] Fig. 10b, [P]/[T]).

Depolarisation is explained by single scattering from rock fragments acting as randomly oriented dipoles; 65–70 % of the power returned at oblique angles at 23 cm must come from such scatterers ([EV69] pp. 224–226; [LL67] pp. 21–22, [P]/[T]).

### Delay structure

A point at angle of incidence φ has excess delay t = (2a/c)(1 − cos φ), with full depth 2a/c = 11.6 ms. Equal delay intervals correspond to equal surface areas (area per unit delay = π a c), so a short-pulse radar measures P(t) ∝ σ₀(φ(t)) directly ([EV65] p. 1642, Eqs. 11–12, [P]). A uniformly bright disc would give P(t) ∝ 1 − ct/2a ([EV65] p. 1642, [P]). Near the leading edge the Hagfors law reduces to P(t) ≈ (1 + t/t₀)^(−3/2) with t₀ = (2a/c)/(2C), i.e. a power-law (not exponential) decay; t₀ ≈ 145, 89 and 61 µs for C = 40, 65 and 95 [E]. The diffuse part is P(t) ∝ (1 − t/11.6 ms)^(3/2) [E from the cos^(3/2) law].

### Doppler structure

A terrestrial observer sees the Moon rotate slowly because of (i) optical libration in longitude (non-uniform orbital motion against uniform spin), (ii) optical libration in latitude (6.7° tilt of the spin axis to the orbit normal), (iii) diurnal libration (the observer is carried by Earth's rotation at up to 465 m/s across the line of sight), and (iv) the very small physical libration. Evans gives the usual range of the apparent angular rate as 10⁻⁷ to 10⁻⁶ rad/s ([EV65] p. 1641, [P]). If ω is the component of the apparent rotation vector perpendicular to the line of sight, a point on the apparent equator at the limb has Doppler offset f_max = ±2 ω a / λ for a monostatic radar ([EV69] p. 216, Eq. 29, [P]; [K1JT10] p. 2, [G]), so the limb-to-limb spread is LL = 4 a ω / λ = 4 f a ω / c. For ω = 10⁻⁶ rad/s this is 23.2 Hz per GHz [E]. Lines of constant Doppler are strips parallel to the apparent rotation axis; lines of constant delay are rings centred on the sub-radar point ([EV65] Fig. 4, [P]).

The apparent rotation vector can be written [E] as ω = ω_spin − (r × v)/r², where r and v are the station-to-Moon vector and its rate in an inertial frame and ω_spin is the lunar spin vector (2.66 × 10⁻⁶ rad/s about an axis 1.54° from the ecliptic pole). The station's velocity contributes |v_⊥|/d ≤ 465 cos(lat)/3.84 × 10⁸ = 1.21 × 10⁻⁶ cos(lat) rad/s at mean distance (1.30 × 10⁻⁶ at perigee); the transverse fraction of the station velocity is √(1 − cos²δ sin²H), so the diurnal term is largest at transit and smallest near moonrise and moonset for low declination. The geocentric (optical-libration) part alone has a median of 3.0 × 10⁻⁷ and a maximum of 5.5 × 10⁻⁷ rad/s (1.5 and 2.7°/day) [E, truncated lunar theory, years 2015 and 2025]. The terms add as vectors, so near-zero totals occur when the diurnal term cancels the optical-libration term.

For two stations A (transmit) and B (receive) the Doppler offset of a surface element is governed by the sum of the two one-way apparent rotation vectors, LL_AB = (2a/λ)|ω_A + ω_B|; the monostatic formula is the special case A = B ([K1JT10] p. 3, [G]: vector sum of the one-way contributions; [FR60] §4, [P]: total libration rate computed for separated transmitter and receiver; [G3WDG10], [G]: average of the two stations' sub-observer tracks).

### Delay–Doppler coupling

A ring at excess delay t has projected radius r/a = √(1 − (1 − t/T)²) (T = 11.6 ms) and spreads its power over Doppler offsets |f| ≤ f_max r/a with density 1/(π √((f_max r/a)² − f²)), because Doppler varies as the cosine of position angle round the ring [E]. Small delays therefore go with small Doppler offsets and the limb with the full spread: delay and Doppler are not independent, and a two-ray model with freely chosen delay and Doppler cannot represent the scattering function. The whole-disc Doppler spectrum of the quasi-specular component is, to a good approximation, a Lorentzian truncated at ±f_max: integrating the small-angle Hagfors law (1 + C ρ²)^(−3/2) along a strip gives S(f) ∝ 1/(1 + C f²/f_max²), full width at half maximum LL/√C [E]. A cos^n diffuse law gives S(f) ∝ (1 − f²/f_max²)^(n/2), i.e. a semicircle for the depolarised component (n = 1) [E]. The corresponding field autocorrelation is the Bessel (Hankel) transform of the angular power spectrum ([EV69] p. 216, Eq. 30, citing Hagfors, Nanni and Stone 1968, [P]); for the Lorentzian part it is approximately exponential, exp(−2π f_max τ/√C) [E].

For whole-disc illumination the product of full Doppler extent and full delay depth is LL × 11.6 ms, which reaches unity at LL = 86 Hz. Evans notes that pulse radars meet the resulting range–Doppler ambiguity only above about 5,000 Mc/s ([EV65] pp. 1653–1654, [P]). At 5.8 GHz the Moon is therefore about critically spread in full extent, although strongly underspread in terms of half-power widths (about 20 Hz × 0.75 ms ≈ 0.015) [E].

### Fading, speckle and reciprocity

The echo is the vector sum of very many independent contributions, so its complex amplitude is Gaussian and its envelope Rayleigh. The speckle pattern the Moon casts on Earth has a transverse scale of order λ d / (effective diameter of the bright region), and it moves across a station as the apparent geometry changes; temporal fading is this pattern sweeping past [E, van Cittert–Zernike reasoning; the same picture underlies planetary radar speckle tracking, [MARGOT21], [P]]. For the monostatic case a transverse station displacement Δ changes the two-way phase of a surface element at transverse position ξ by (4π/λ) ξ Δ/d, so temporal and spatial correlation are the same function with Δ_equivalent = 2 d ω τ [E]. Under reciprocal propagation, matched polarization and an effectively frozen geometry, A→Moon→B and the reverse have corresponding reciprocal transfer functions. Spatially separated receivers can be weakly correlated; separation alone does not prove exact independence [E].

## Measured and reported values

### Total radar cross-section versus wavelength

Table 1 reproduces Evans' compilation ([EV65] p. 1639, Table 1, [P]; values are fractions of π a² = 9.49 × 10¹² m²; errors are as reported, or ±3 dB where none was published).

| Author (as listed) | Year | λ (cm) | σ/πa² | Error (dB) |
|---|---|---|---|---|
| Lynn et al. | 1963 | 0.86 | 0.07 | ±1 |
| Kobrin | 1963 | 3.0 | 0.07 | ±1 |
| Morrow et al. | 1963 | 3.6 | 0.07 | ±1.5 |
| Evans & Pettengill | 1963 | 3.6 | 0.04 | ±3 |
| Kobrin | 1963 | 10.0 | 0.07 | ±1 |
| Hughes | 1963 | 10.0 | 0.05 | ±3 |
| Victor et al. | 1961 | 12.5 | 0.022 | ±3 |
| Aarons | 1959 | 33.5 | 0.09 | ±3 |
| Blevis & Chapman | 1960 | 61.0 | 0.05 | ±3 |
| Fricker et al. | 1960 | 73.0 | 0.074 | ±1 |
| Leadabrand | 1959 | 75.0 | 0.10 | ±3 |
| Trexler | 1958 | 100.0 | 0.07 | ±4 |
| Aarons | 1959 | 149.0 | 0.07 | ±3 |
| Trexler | 1958 | 150.0 | 0.08 | ±4 |
| Webb | 1959 | 199.0 | 0.05 | ±3 |
| Evans | 1957 | 250.0 | 0.10 | ±3 |
| Evans et al. | 1959 | 300.0 | 0.10 | ±3 |
| Evans & Ingalls | 1962 | 784.0 | 0.06 | ±5 |
| Davis & Rohlfs | 1964 | 1130 | 0.19 | +3/−2 |
| Davis & Rohlfs | 1964 | 1560 | 0.13 | +3/−2 |
| Davis & Rohlfs | 1964 | 1920 | 0.16 | +3/−2 |

- The most accurate value is at 23 cm: σ = (0.065 ± 0.008) π a², obtained by comparison with the Lincoln Calibration Sphere (1 m² at 23 cm), transmitting one circular sense and receiving the opposite sense; the quoted error is the rms of ±0.38 dB scatter in 40 sphere and 14 Moon observations ([LLQ2] pp. 10–11; [LL67] p. 1, [T]; published as Evans & Hagfors 1966, secondary). The Moon was observed with 2-ms pulses and the result scaled up to the whole disc with the measured fraction of cross-section seen at that pulse length ([LLQ2] p. 11, [T]), so the value also depends on the 23-cm delay law.
- Evans concludes the cross-section is about 7 % of the projected area from 1 cm to 1 m and perhaps 10 % or more from 1 to 10 m, but that ±3 dB uncertainties prevent any firm statement on wavelength dependence; the decametre values may be affected by the ionosphere ([EV65] abstract and p. 1639, [P]). Davis et al. later confirmed about 0.15–0.18 at 11.3 m from 1-min means at Faraday maxima, and showed ionospheric focusing events that briefly raised the apparent cross-section to nearly the full projected area ([DA65] pp. 1660–1663, [P]).
- **No measurement at 5–6 cm was found.** The nearest are 3.0 cm (0.07 ± 1 dB), 3.6 cm (0.07 ± 1.5 dB and 0.04 ± 3 dB), 10 cm (0.07 ± 1 dB and 0.05 ± 3 dB), 12.5 cm (0.022 ± 3 dB, an outlier) and 23 cm (0.065 ± 0.5 dB). Lincoln Laboratory treated 0.065 as "approximately true at 3.8 cm as well" ([LL67] p. 7, [T]). A value of 0.065–0.07 at 5.8 GHz is therefore an interpolation with a working ±1.5 dB sensitivity without a calibrated coverage probability [E].
- Monthly range variation changes the echo by about ±1 dB (2 dB peak-to-peak); Fricker et al. are cited as the only early workers to have observed it, and the 0.2 dB diurnal variation was not observable ([EV65] p. 1639, [P]).

### Two-way path loss, 50 MHz to 24 GHz [E]

L = 10 log₁₀[(4π)³ d⁴ / (η π a² λ²)], η = 0.065, a = 1737.4 km, centre-to-centre distances 356,400 / 384,400 / 406,700 km.

| f (MHz) | λ (cm) | Perigee (dB) | Mean (dB) | Apogee (dB) | ARRL Handbook 2010, mean ([HBK10] Table 30.7, [G]) |
|---|---|---|---|---|---|
| 50.1 | 598 | 241.6 | 242.9 | 243.9 | 242.9 |
| 144.1 | 208 | 250.8 | 252.1 | 253.1 | 252.1 |
| 432 | 69.4 | 260.3 | 261.6 | 262.6 | 261.6 |
| 1296 | 23.1 | 269.9 | 271.2 | 272.2 | 271.2 |
| 2320 | 12.9 | 274.9 | 276.2 | 277.2 | 276.2 (2304) |
| 3400 | 8.82 | 278.3 | 279.6 | 280.5 | 279.7 (3456) |
| 5760 | 5.205 | 282.8 | 284.1 | 285.1 | 284.1 |
| 5800 | 5.169 | 282.9 | 284.2 | 285.2 | — |
| 10368 | 2.89 | 287.9 | 289.2 | 290.2 | 289.2 |
| 24048 | 1.25 | 295.2 | 296.5 | 297.5 | 293.5 (see note) |

- Explicit 5760 MHz arithmetic: λ = 0.052047 m; σ = 0.065 × π × (1.7374 × 10⁶)² = 6.164 × 10¹¹ m²; (4π)³ = 1984.4; d⁴ = (3.844 × 10⁸)⁴ = 2.1834 × 10³⁴ m⁴; L = 1984.4 × 2.1834 × 10³⁴ / (6.164 × 10¹¹ × 2.7089 × 10⁻³) = 2.595 × 10²⁸ = 284.14 dB. Perigee to apogee spans 40 log₁₀(406.7/356.4) = 2.29 dB. A station directly under a perigee Moon is 6,378 km closer (282.5 dB).
- The Handbook uses the same formula with r = 1.738 × 10⁶ m, d = 3.844 × 10⁸ m, η = 0.065 and quotes ±1.1 dB for the ±6.8 % distance variation ([HBK10] §30.9.1, [G]). Its 24048 MHz entry (293.5 dB) is inconsistent with its own formula, which gives 296.5 dB; it appears to be a misprint [E].
- Sensitivity to η: 0.04 → +2.1 dB; 0.07 → −0.3 dB; 0.10 → −1.9 dB [E]. For the HF ideas below, the same formula gives 226.3 dB at 7.4 MHz and 235.6 dB at 21.5 MHz with η = 0.065, or about 4 dB less with the decametre cross-sections of 0.15–0.19 [E].

### Angular scattering law and delay power profile

**Measured law.** Lincoln Laboratory tabulated the opposite-sense circular ("polarised") cross-section per unit surface area versus delay at 3.8, 23 and 68 cm with about 10 µs resolution, normalised to a total of 0.065 π a² ([LL67] p. 6, Table II, [T]). Selected values (dB):

| Delay (µs) | φ (deg) | 3.8 cm | 23 cm | 68 cm |
|---|---|---|---|---|
| 10 | 2.4 | −0.83 | +2.77 | +4.29 |
| 100 | 7.5 | −4.03 | −1.53 | −0.91 |
| 500 | 16.9 | −8.78 | −8.58 | −9.31 |
| 1000 | 24.0 | −10.93 | −12.58 | −13.31 |
| 2000 | 34.2 | −13.73 | −16.33 | −18.01 |
| 3000 | 42.2 | −15.23 | −17.73 | −19.51 |
| 5000 | 55.3 | −17.43 | −19.98 | −21.31 |
| 7000 | 66.6 | −20.13 | −22.58 | −23.96 |
| 9000 | 77.1 | −23.98 | −26.53 | −27.81 |
| 10000 | 82.1 | −26.93 | −29.43 | −30.61 |
| 11000 | 87.0 | −32.03 | −34.93 | −35.81 |

- Near normal incidence the cross-section per unit area rises with wavelength as λ^0.46 (3.8–23 cm) and λ^0.32 (23–68 cm); near the limb it falls as λ^−0.32 and λ^−0.26 ([LL67] p. 9; [EV69] p. 223, Eqs. 31–32, [T]/[P]). Centre-to-limb brightness contrast is about 2 at 8.6 mm, about 25 at 3.6 cm and about 150 at 68 cm ([EV65] p. 1645, [P]).
- Hagfors-law fits near normal incidence: C = 95, 65 and 40 with ρ₀ = 0.0697, 0.0648 and 0.0500 at 68, 23 and 3.8 cm; the fit is excellent at 68 and 23 cm but "rather poor" at 3.8 cm ([LL67] p. 19, Table III, [T]). Effective rms slopes are 6°, 7° and 9° ([EV69] p. 222, [P]); geometric-optics treatments would give 10–12° ([LL67] p. 19, [T]). Log-wavelength interpolation gives C ≈ 44 at 5.2 cm [E].
- An analytic approximation to the 3.8-cm polarised law, good to 0.2 dB for 13° < θ < 71° and 1 dB elsewhere: P(θ) ∝ cos θ/(cos θ + sin θ/0.35)³ for θ ≤ 60°, and ∝ cos θ/(cos²θ + sin²θ/0.295²)^(3/2) beyond; the depolarised law was assumed ∝ cos θ ([LL68] p. 31, Eq. 44, [T]).
- Apollo bistatic (forward-scatter) radar at 13 and 116 cm also found slope distributions best matched by a Hagfors law, with rougher highlands (at least 8°) than maria ([SIMPSON78] abstract, [T]); that geometry is oblique-specular, unlike the near-backscatter geometry of EME.

**Diffuse fraction.** Two published decompositions differ slightly, and their authors call them crude:

| λ | Diffuse % ([LL67] Table I, [T]) | Quasi-specular / diffuse % ([EV69] Table II, [P]) | Surface fraction rough (X, [EV69]) |
|---|---|---|---|
| 68 cm | 20 | 80 / 20 | 8 % |
| 23 cm | 25 | 75 / 25 | 9 % |
| 3.6 cm | 35 | 70 / 30 | 14 % |
| 8.6 mm | 85 | 15 / 85 | 68 % |

Another extrapolation method gave 12 % at 70 cm (Pettengill and Thompson, quoted in [LL67] p. 2, [T]). At decametre wavelengths the quasi-specular component contains nearly all the energy ([DA65] p. 1663, [P]). For 5.2 cm an interpolated diffuse share of roughly 30 % (range 25–35 %) is reasonable [E].

**Power versus delay, cumulative [E].** Integrating Table II with P(t) ∝ σ₀(t) (the integral reproduces the stated 0.065 normalisation to within 3–5 %):

| Quantity | 3.8 cm | 23 cm | 68 cm | 5.2 cm (log-λ interpolation) |
|---|---|---|---|---|
| Fraction within first 100 µs | 0.15 | 0.31 | 0.41 | ≈0.17 |
| within 500 µs | 0.38 | 0.62 | 0.71 | ≈0.41 |
| within 1 ms | 0.51 | 0.73 | 0.81 | ≈0.55 |
| within 2 ms | 0.67 | 0.82 | 0.87 | ≈0.69 |
| within 5 ms | 0.88 | 0.94 | 0.95 | ≈0.89 |
| Half-power delay t50 | 0.94 ms | 0.27 ms | 0.15 ms | ≈0.75 ms |
| t90 | 5.4 ms | 3.8 ms | 2.8 ms | ≈5.1 ms |
| Mean excess delay | 1.90 ms | 1.13 ms | 0.84 ms | ≈1.7 ms |
| rms delay spread | 2.26 ms | 1.88 ms | 1.67 ms | ≈2.2 ms |

- Published figures for comparison: more than 50 % of the energy within the first 50 µs at 198 MHz, with little echo visible after 1 ms (sensitivity-limited) ([TR58] summary and p. 290, [P]); at 68 cm the fraction of cross-section seen with a pulse of given length is about 0.41 at 100 µs, one-half at about 155 µs, 0.71 at 500 µs, 0.80 at 1 ms, 0.87 at 2 ms and 0.95 at 5 ms ([EV65] p. 1638, Fig. 1, digitised from the page image, [P]); half of the power within 120 µs at 430 MHz, 210 µs at 1.3 GHz and 650 µs at 8 GHz, from the innermost 14 %, 19 % and 33 % of the radius ([K1JT10] p. 2 and Fig. 2, derived from Hagfors 1970, [G]).
- The [K1JT10] numbers are reproduced closely (130, 213 and 642 µs; radius fractions 0.15, 0.19, 0.33) if Table II is weighted by projected disc area instead of surface area [E]. Surface-area weighting is the one consistent with the table's normalisation and with P(t) ∝ σ₀(t): integrated with surface-area weights, the 68-, 23- and 3.8-cm columns give 0.068, 0.067 and 0.066 of πa² against the stated 0.065, whereas projected-area weights give 0.063, 0.061 and 0.055 [E]. Evans' measured 68-cm pulse-length curve (previous bullet) matches the surface-area integration at every delay read (0.41, 0.71, 0.81, 0.87 and 0.95 at 0.1, 0.5, 1, 2 and 5 ms; t50 153 µs, t90 2.8 ms), while projected-area weighting departs from it (0.44, 0.76, 0.85, 0.92, 0.98; t50 130 µs, t90 1.6 ms) [E]. The half-power delays are therefore about 30–45 % longer than [K1JT10] states (153/120, 270/210 and 936/650 µs) [E]. Hagfors (1970) itself was not obtained; a later paper compares its per-surface-area backscatter coefficients with the full-disc measurements documented by Hagfors (1970) ([CAMPBELL10] author manuscript p. 6, [P]), which is consistent with, but does not prove, a per-surface-area tabulation there.
- The Handbook states that the effective time spread is no more than 0.1 ms at VHF/UHF and that the median time spread "can be as much as several milliseconds" at 10 GHz and above ([HBK10] §30.9.2, [G]).

### Polarisation

- 68 cm, circular: the percentage polarisation falls linearly with delay to 60 % at 3 ms, after which the ratio of the two components is roughly constant ([EV65] p. 1646, Fig. 13, [P]). The depolarised component shows no leading-edge peak and follows cos φ ([EV69] Fig. 13, [P]).
- 23 cm: the expected-to-orthogonal power ratio is about 13 dB near normal incidence; toward the limb it levels at about 7 dB for linear and falls to about 3 dB for circular polarisation ([EV69] Fig. 14, after Hagfors 1967, read from the graph, [P]). For linearly polarised illumination the component polarised in the local plane of incidence is the stronger; the ratio of perpendicular to in-plane power approaches 0.5 at 23 cm and 0.75 at 3.8 cm near grazing ([LL67] p. 13, Figs. 15–16, [T]).
- 3.8 cm (7840 MHz), both circular senses: the polarised component falls from about −2 dB at 3–5° to −13 dB at 40° and −20 dB at 76°, while the depolarised component is nearly flat (cos θ) at about −16 to −17 dB out to 60° ([ZPC74] p. 45, Fig. 10, read from the graph, [P]). The opposite/same-sense ratio is therefore about 16 dB at 5°, 8 dB at 15°, 5 dB at 30° and 3–4 dB beyond 40° [E from the figure]. Both components show a narrow dip at 63°, tentatively attributed to a Brewster-angle effect (dielectric constant 3.9) ([ZPC74] pp. 45–46, [P]).
- Disc-integrated ratios [E]: at 3.8 cm the cos θ depolarised law integrates to a same-sense/opposite-sense ratio of roughly 0.25 (−6 ± 1.5 dB, limited by reading the figure and by a 1–2 dB scale ambiguity against Table II); at 23 cm, a 25 % diffuse share with a 3 dB ratio gives about 0.12 (−9 dB); interpolation to 5.2 cm gives about 0.2 (−7 ± 1.5 dB).
- Amateur measurements: at 10 GHz the circular cross-polar discrimination of the Moon is quoted as about 6.5 dB (from an OM6AA paper with OK1KIR comments, not seen) and linear depolarisation as about 10–11 dB; measured CP-to-CP self-echoes were 1.3 dB weaker than LP-to-LP (1.6 dB after a 0.3 dB system correction), and CP-to-LP showed 2 dB loss rather than 3 dB ([G3WDG16] slides "Results of own echo tests", "Discussion" and "Conclusions", [G]). With a 20-m dish illuminating only the centre of the disc at 10 GHz, the orthogonal linear polarisation was about 10 dB down ([SM5BSZ06], [G]). At 432 MHz with a 0.15° beam (under one-third of the disc), same-sense circular self-echoes were 15–20 dB below the expected sense ([ARECIBO10] p. 3, [G]). At 144 and 432 MHz with linear polarisation the cross-polarised echo was a few per cent or undetectable ([K1JT16] pp. 2–3, [G]). At 11–19 m, Faraday nulls 18–27 dB deep imply a very small depolarised share ([DA65] pp. 1661–1663, [P]).

### Regional variations and dependence on libration state

- Tycho is 7–10 times brighter than its surroundings at 68–70 cm in both polarisations, which rules out large flat facets ([EV65] p. 1654; [EV69] p. 227, [P]). At 3.8 cm, enhancements arise from moderate-size craters, very young small craters with blocky ejecta, and diffuse bright patches with no optical counterpart; relative backscatter was mapped at 2-km resolution to about 10 % ([ZPC74] abstract and p. 46, [P]).
- At 70 cm, highland terrain is about 50 % more reflective than mare when viewed obliquely (φ > 30°), whereas at 2.2 m mare is about 30 % more reflective in the specular direction ([EV69] p. 221, citing Thompson & Dyce 1966 and Tyler 1968, [P]). At 8.6 mm no mare–highland difference above ±2 dB was seen ([EV65] p. 1645, [P]).
- Modern Earth-based imaging attributes the higher 70-cm highland echoes to low microwave loss (low Fe and Ti) allowing deeper penetration to buried rocks, and the weak mare echoes to ilmenite-related loss; 3–4 cm waves penetrate less than a metre, 70-cm waves several metres or more ([CAMPBELL16], [P]).
- Dependence of the **total** echo on libration state: no measurement was found. Indirect evidence: at 412 MHz the total received level, corrected for distance, was "essentially constant" over three weeks ([FR60] p. 464, [P]); Evans notes that short-pulse results near the peak differ according to whether mare or highland occupies the sub-radar region ([EV69] p. 221, citing Burns 1968, [P]). Since the sub-radar point wanders by at most about ±8° while half the power comes from within 9–23° of it, a variation of a few tenths of a dB to about 1 dB is plausible but unverified [E, conjecture]. Calibrated amateur self-echo measurements at 1296 MHz with a 3-m dish are reported to come close to the predicted −14.6 dB (apogee) to −12.3 dB (perigee) signal-to-noise ratios, without any reported libration dependence ([WSJTX-ECHO] pp. 4–5, [G]). Mini-RF found opposition-like changes with bistatic angle only for young crater ejecta and polar crater floors ([PATT17]; [GSA18]; [EPSC22]; see the 4–8 GHz section below).

### Doppler spread: published values

- Typical limb-to-limb broadening is about 2 Hz at 100 MHz and "rarely larger" ([EV65] pp. 1641, 1653, [P]), i.e. about 20 Hz per GHz.
- [K1JT10] (p. 5 table, [G]) for self-echoes at ±40° latitude; w50 and w10 are widths taken from the autocorrelation of measured echo spectra, loosely the bands containing half and 90 % of the power:

| f (MHz) | LL maximum (Hz) | LL median (Hz) | w10 median (Hz) | w50 median (Hz) |
|---|---|---|---|---|
| 144 | 4 | 2.2 | 1.1 | 0.4 |
| 432 | 12 | 7 | 3 | 1.2 |
| 1296 | 37 | 19 | 10 | 4 |
| 2304 | 65 | 35 | 20 | 8 |
| 3400 | 97 | 51 | 31 | 12 |
| 5760 | 164 | 86 | 60 | 25 |
| 10368 | 295 | 156 | 128 | 58 |

  The 144–2304 MHz widths are fitted to amateur measurements (K1JT, K2UYH, VK7MO); the curves are calculated with an allowance for the larger reflecting region at higher frequencies, and two 10.3 GHz spectra from G3WDG are shown ([K1JT10] pp. 3–4, Figs. 6–9, [G]). The 5760 MHz row is therefore a model value, not a fit to 5.7 GHz data. Measured examples: w50 = 2.3 ± 0.3 Hz and w10 = 6.9 ± 0.6 Hz at 432 MHz; 6.2 ± 0.3 and 18.9 ± 0.6 Hz at 1296 MHz ([K1JT10] p. 3, [G]). Weak spectral wings extend to the full computed limb-to-limb range ([K1JT10] p. 3; [K1JT16] p. 3, [G]).
- [G3WDG10] ([G]): empirical echo width (Hz) = 6000 × f (GHz) × libration rate (deg/min), fitted to 10 GHz echoes and consistent with G3LTF's 432 MHz–5.7 GHz echoes. The theoretical limb-to-limb coefficient is 6743 [E], so visually estimated widths are about 0.9 LL. Two-station measurements at 5.7 GHz on 16 May 2010: G3LTF receiving VK3NX, 35 Hz measured against 38 Hz predicted; receiving PA0EHG, 20 Hz against 25 Hz.
- At 10 GHz, with LL about 90 Hz, a measured two-station spectrum had a half-power width of 19 Hz and nearly half its power within ±5 Hz; at 24 GHz the power was spread much more evenly with little central peak ([VK7MO] pp. 2, 4, [G]). The WSJT-X echo-mode note shows spectra with predicted full widths of 3.4, 18 and 228 Hz whose bases match the prediction ([WSJTX-ECHO], [G]).
- 412 MHz two-station link (South Dartmouth, Mass. to Alpha, Md., August 1957): fading rates from 3–4 per second down to about 0.005 per second, the fast rates prevailing and the very slow ones rare and brief; the rate of envelope maxima was 0.67 × the maximum limb Doppler offset, equivalent to a half-power-containing bandwidth of 0.18 of the limb-to-limb spread and to an effective reflecting disc of fractional radius about 0.2 ([FR60] pp. 456, 463–464, Eqs. 40–41, [P]).
- Decametre: libration fading periods of 2–20 s at 11–22 m ([DA65] p. 1660, [P]); at 100 MHz the leading-edge echo fades with a typical period of 10 s, increasing linearly with wavelength ([LLQ2] p. 46, [T]).

### Doppler spread at 5760 MHz: derived statistics [E]

Self-echo, Moon above the horizon, one year sampled every 3 min, truncated analytic lunar theory (checked against a textbook example to 0.003° and 35 km; spherical Earth; physical libration neglected). 2025 is near major lunar standstill (declination to ±28.6°), 2015 near minor standstill.

| Latitude | Year | LL 10 % | LL median | LL 90 % | LL max | Time with LL < 0.2 × median |
|---|---|---|---|---|---|---|
| 0° | 2025 | 69 | 127 | 178 | 201 | 1.0 % |
| 0° | 2015 | 47 | 107 | 164 | 180 | 1.8 % |
| 40° | 2025 | 54 | 101 | 144 | 165 | 1.3 % |
| 40° | 2015 | 37 | 80 | 130 | 144 | 1.9 % |
| 52° | 2025 | 45 | 85 | 123 | 142 | 1.6 % |
| 52° | 2015 | 30 | 66 | 110 | 123 | 1.8 % |
| 65° | 2025 | 31 | 66 | 100 | 131 | 1.9 % |
| 65° | 2015 | 22 | 50 | 85 | 97 | 1.6 % |

- These agree with [K1JT10] at 40° (maximum 164 Hz; median 86 Hz, computed for August 2010): maximum within 1–12 %, median within −7 to +17 % depending on the year. The corresponding apparent rotation rates run from below 10⁻⁸ to 1.5 × 10⁻⁶ rad/s (equator), consistent with Evans' 10⁻⁷–10⁻⁶.
- Scaling: LL is proportional to frequency. Dividing by 5.76 gives Hz per GHz (40°: maximum about 25–29, median 14–18). For 144 MHz multiply the table by 0.025; for 10368 MHz by 1.8; for 24048 MHz by 4.18.
- Variation through a pass and a month: spread is generally largest near transit and smallest near moonrise and moonset ([HBK10] §30.9.3; [G3WDG10], [G]). In predictions for June 2010, minima occurred with the Moon above the horizon on 11 days of the month, were deep on only one or two of them, fell on different days at different latitudes, and shifted by about 4 min per degree of longitude ([G3WDG10], [G]). In the model, each site sees 255–370 local minima per year while the Moon is up, of which 19–47 fall below 10 % of the median; LL is below 10 % of the median for 0.2–0.5 % of the time [E]. In absolute terms at 5760 MHz (equator to 52°, 2015 and 2025): LL < 30 Hz for 1.4–9.9 % of Moon-up time, < 10 Hz for 0.15–1.1 %, < 6 Hz for 0.05–0.4 % (12–42 episodes per year averaging 12–24 min each), and < 2 Hz for at most 0.05 % [E]. A minimum predicted to give 2 Hz width at 10 GHz was observed as nearly zero width with slow, deep fades that removed whole echoes ([G3WDG10], [G]).
- Two-station examples for 2025 (both elevations above 5°) [E]:

| Path | LL 10 % | LL median | LL 90 % | LL max | Max bistatic angle at the Moon |
|---|---|---|---|---|---|
| 40°N 75°W – 50°N 10°E | 41 | 79 | 117 | 128 | 1.0° |
| 50°N 10°E – 35°N 139°E | 20 | 52 | 85 | 98 | 1.4° |
| 50°N 10°E – 38°S 145°E | 24 | 59 | 82 | 91 | 2.0° |
| 40°N 75°W – 40°N 120°W | 60 | 106 | 141 | 156 | 0.6° |
| 40°N 75°W – 23°S 46°W | 77 | 119 | 155 | 173 | 1.2° |

  Mutual spread between stations at widely different longitudes is often smaller than either station's self-echo spread, as observed once on 10 GHz ([G3WDG10], [G]), because the two diurnal vectors partly cancel; closely spaced stations behave like the self-echo case.

### Spectrum shape, coherence time and coherence bandwidth

- Shape: sharply peaked with broad, low wings out to the limb values ([K1JT10] p. 3 and Fig. 6, [G]). From the measured scattering laws [E], whole-disc opposite-sense spectra have the following widths as fractions of LL:

| Law | Band holding 50 % of power | Band holding 90 % | FWHM | One-sided rms |
|---|---|---|---|---|
| 68 cm | 0.09 | 0.41 | 0.105 | 0.12 |
| 23 cm | 0.12 | 0.51 | 0.13 | 0.14 |
| 3.8 cm | 0.21 | 0.65 | 0.21 | 0.18 |
| 5.2 cm (interpolated) | 0.19 | 0.62 | 0.19 | 0.17 |

  The FWHM values match the Lorentzian prediction LL/√C (0.103, 0.124 for C = 95, 65) and the measured 19 Hz at LL ≈ 90 Hz at 10 GHz ([VK7MO]). The rms-based rate of envelope maxima for a Gaussian-equivalent spectrum, N = 2.52 σ, gives N/f_max = 0.61 at 68 cm against the measured 0.67 at 412 MHz ([FR60]). Published w50 values (0.17–0.37 LL) are larger than the 50 %-power bands computed here (0.09–0.21 LL), whereas published w10 values (0.43–0.82 LL) are close to the computed 90 % bands (0.41–0.65 LL). The definition explains much of this. [K1JT10] (p. 3) defines w50 and w10 as the full widths of the autocorrelation function of the echo spectrum at its 50 % and 10 % points, and only "loosely" as bands holding half and 90 % of the power. Applied to the computed spectra, that definition gives w50 = 0.20, 0.27 and 0.53 LL and w10 = 0.63, 0.83 and 1.08 LL for the 68-, 23- and 3.8-cm laws (0.48 and 1.03 LL for 5.2 cm), i.e. 2.2–2.6 times the 50 %-power band and 1.5–1.7 times the 90 % band; for an untruncated Lorentzian of FWHM Γ it gives exactly 2Γ and 6Γ, against a 50 % band of Γ and a 90 % band of 6.3Γ [E]. The measured ratio w10/w50 of about 3.0 at 432 and 1296 MHz ([K1JT10] p. 3) matches the autocorrelation definition applied to the 68- and 23-cm laws (3.1), not the ratio of the power-containing bands (4.2–4.4) [E]. The published w50 (0.17 and 0.21 LL at 432 and 1296 MHz) are, however, about 0.8 times the computed autocorrelation widths, and the 10 GHz model values (0.37 and 0.82 LL) lie between the two definitions; that residual factor is unexplained (it may lie in the scattering laws, the computed LL or the measurement). A published w50 should therefore not be read as a 50 %-power band: the two differ by a factor of about two.
- Measured whole-disc Doppler spectra at centimetre wavelengths: none was found in the professional literature. The classical 3.6-cm work measured echo power against delay with 30-µs pulses and 20-µs resolution (Evans 1962, in [EV65] p. 1643 and Fig. 8, [P]); the Haystack 3.8-cm work measured delay–Doppler maps through a narrow beam ([ZPC74]; [LL68], [P]/[T]); and the 3.8-cm depolarised law was assumed proportional to cos φ, not measured, in 1967– 1968 ([LL67] p. 9; [LL68] p. 31, [T]). Hagfors (1967) was not obtained; [EV69] cites it for the 23-cm polarisation ratios of its Fig. 14. Spectral shapes at 5–6 cm therefore rest on the measured delay laws and on statistical uniformity round each delay ring; the only measured centimetre-wave spectra found are amateur ones at 10 and 24 GHz ([VK7MO]; [K1JT10] Fig. 7; [WSJTX-ECHO], [G]).
- At 5760 MHz and median conditions (LL ≈ 86–101 Hz): FWHM and 50 % band about 16–19 Hz [E]; w50 = 25 Hz and w10 = 60 Hz ([K1JT10], [G]). At the 40° maximum (165 Hz): about 31 Hz [E]; equator maximum (201 Hz): about 38 Hz [E].
- Field coherence time [E]: for the interpolated 5.2-cm law the magnitude of the complex autocorrelation falls to 0.71, 0.5 and 1/e at τ × LL = 0.78, 1.14 and about 1.4. This gives 13 ms (to 0.5) at LL = 86 Hz, 11 ms at 101 Hz, 7 ms at 165 Hz, 5.7 ms at 201 Hz, 38 ms at 30 Hz and 114 ms at 10 Hz. The published rule that coherence time is the inverse of the frequency spread gives several seconds at 144 MHz, a few tenths of a second at 1296 MHz and 20 ms at 10 GHz ([HBK10] §30.9.3, [G]), i.e. about 40 ms at 5760 MHz using 1/w50; the two definitions differ by about a factor of three.
- Coherent-integration loss [E], fraction of echo power retained when integrating coherently for T after removing the mean Doppler (∫S(f) sinc²(πfT) df): −0.1 dB at T × LL = 0.5; −0.4 dB at 1.0; −0.8 dB at 1.44; −1.3 dB at 2; −2.1 dB at 3; −3.3 dB at 5; −5.5 dB at 10. For T = 16.7 ms, T × LL = 1.44 at LL = 86 Hz and about 3 at 180 Hz; the field correlation at a lag of 16.7 ms is then only about 0.35 and below 0.2 respectively, and gains averaged over adjacent 16.7-ms intervals correlate about 0.5 and 0.33.
- Expected rate of envelope maxima at 5760 MHz: about 0.44 × LL per second, i.e. roughly 40 per second at the median and 70–90 per second at the extremes [E, Gaussian-equivalent formula as used in [FR60]].
- Coherence bandwidth, measured: at 440 MHz over a Millstone Hill to Prince Albert link, the correlation coefficient of the detected amplitudes of two carriers fell rapidly with separation and reached 0.3 between 500 and 1000 Hz; 850-Hz-shift teletype tones would fade almost independently; SSB speech in 2 kHz was satisfactory with noticeable selective fading; narrow-band FM was badly distorted ([IBD61] p. 632 and Fig. 1, [P]). Predicted from the 68-cm delay profile [E]: squared magnitude of the frequency correlation 0.38, 0.30 and 0.25 at 500, 750 and 1000 Hz, in agreement.
- Coherence bandwidth, earlier estimates: Trexler inferred that a band of possibly 10 kHz exists (against about 50 Hz for a uniformly bright Moon) and passed AM voice in 3 kHz ([TR58] pp. 290–291, [P]); the Handbook quotes "a few kHz" ([HBK10] §30.9.2, [G]). These reflect the quasi-specular core only.
- Coherence bandwidth at 5.2 cm, predicted [E] (magnitude of the correlation of complex gain versus separation, whole disc, opposite-sense law): 0.94 at 25 Hz, 0.81 at 50 Hz, 0.67 at 75 Hz, 0.59 at 100 Hz, 0.53 at 150 Hz, 0.46 at 225 Hz, 0.32 at 500 Hz, 0.23 at 1 kHz. The fast initial drop is caused by the roughly 30 % of power in the diffuse tail (11.6 ms ↔ 86 Hz); the slow remainder by the sub-millisecond core. No measurement at 4–8 GHz was found.

### Amplitude statistics

- All evidence points to Rayleigh envelope statistics (exponentially distributed power) with no steady component: single delay–Doppler cells have rms intensity equal to the mean ([EV65] p. 1654, [P]); 85 averaged looks at 3.8 cm gave 11 % rms fluctuation, as expected for 1/√85 ([ZPC74] Table I, [P]); each coherent sample is treated as one draw from a Gaussian process built from many phase-incoherent elements ([LL68] p. 49, [T]); libration fading is described as ordinary Rayleigh fading ([HBK10] §30.9.3, [G]). At 10 and 24 GHz the phase is described as arbitrary and rapidly varying, ruling out phase-coherent techniques ([VK7MO] p. 6, [G]).
- A strong coherent (Rician) component would be favored by surface height deviations well under λ/8 across the first Fresnel zone, whose radius on the sphere is √(aλ/2): 212 m at 5.2 cm, 1.3 km at 2 m, 3.1 km at 11 m [E]. Lunar relief makes a strong coherent mean less likely under this heuristic; it does not prove a zero mean, so K≈0 is an approximation to test rather than a universal necessity [E]. The only hint of steadier behaviour is at 11.3 m, where nine of ten 1-min mean cross-sections taken at Faraday maxima fell within 10 % of their mean ([DA65] p. 1662, [P]); the data were smoothed over 20 s and selected, so this does not demonstrate a specular component.
- Wavelength dependence appears in the fading rate and spectrum shape, not in the first-order statistics. At 24 GHz the more diffuse echo gives a "more consistent signal level" after averaging ([VK7MO] p. 10, [G]), i.e. more independent fades per unit time and bandwidth.
- Rayleigh reference values: power is more than 10 dB below the mean for 9.5 % of the time, more than 20 dB below for 1.0 % and more than 30 dB below for 0.1 % [E].

### Beam narrower than the disc (the "focused" case)

- Published statements: Evans' Eq. 2 and the ½° rule ([EV65] p. 1638, [P]). Haystack (36.6 m, 7840 MHz) has a one-way beam of 4.4 arcmin and a two-way beam of 3.1 arcmin and therefore maps only a small patch; the Doppler spread across the illuminated region is then small compared with the reciprocal of its delay spread ([ZPC74] Table I; [LL68] p. 10, [P]/[T]). An amateur link-budget tool implements Evans' Eq. 2 as a "beam width factor" with a radially varying reflectivity taken from Evans' data and states that libration spreading depends on that factor ([WETTERSAT], [G]). G3WDG notes that observed spreading is lower when the antenna puts a spot on the Moon ([G3WDG10], [G]). With a 20-m dish at 10 GHz, depolarisation differs from the whole-disc case because only the centre is illuminated ([SM5BSZ06], [G]). When a narrow 24 GHz beam drifted toward the limb the echo shifted by about 100 Hz out of a 200 Hz spread ([VK7MO] p. 7, [G]). No published measurement of echo spread or cross-section versus dish size at 5.7 GHz was found.
- Derived [E]: Gaussian beam centred on the disc, 3.8-cm opposite-sense law as a stand-in for 5.8 GHz, mean lunar diameter 0.518°, dish diameter from HPBW = 70 λ/D. "Two-way" means the same narrow beam transmits and receives (self-echo, or two equal large dishes); "one-way" means only one end is narrow. Loss is relative to the full-disc cross-section with on-axis gains.

| HPBW / lunar diameter | Dish at 5.76 GHz | Two-way loss | One-way loss | t50, two-way | rms delay, two-way | 50 % Doppler band, two-way (× LL) | 90 % band (× LL) |
|---|---|---|---|---|---|---|---|
| 3 | 2.3 m | 0.2 dB | 0.1 dB | 0.87 ms | 2.2 ms | 0.20 | 0.64 |
| 2 | 3.5 m | 0.4 dB | 0.2 dB | 0.79 ms | 2.1 ms | 0.19 | 0.62 |
| 1 | 7.0 m | 1.3 dB | 0.7 dB | 0.51 ms | 1.7 ms | 0.16 | 0.53 |
| 0.75 | 9.4 m | 2.1 dB | 1.2 dB | 0.37 ms | 1.4 ms | 0.14 | 0.45 |
| 0.5 | 14 m | 3.4 dB | 2.2 dB | 0.22 ms | 0.72 ms | 0.11 | 0.33 |
| 0.35 | 20 m | 4.8 dB | 3.5 dB | 0.13 ms | 0.35 ms | 0.09 | 0.24 |
| 0.25 | 28 m | 6.3 dB | 4.8 dB | 0.08 ms | 0.18 ms | 0.07 | 0.19 |
| 0.10 | 70 m | 11.5 dB | 9.3 dB | 0.02 ms | 0.03 ms | 0.04 | 0.09 |

  With the 23-cm law the losses are smaller (0.8 dB two-way at HPBW = disc; 2.0 dB at half the disc). The result at HPBW = disc agrees with Evans' "little loss". Narrow beams remove the diffuse tail first: at HPBW = 0.25 disc the frequency correlation stays above 0.97 out to 225 Hz and 0.88 at 500 Hz [E]. The statistics remain Rayleigh because the illuminated spot still contains very many scatterers. Off-centre pointing shifts the Doppler centroid and increases the diffuse, depolarised share.

### Spatial decorrelation of the speckle pattern [E]

One-way transverse receiver displacement at which the field correlation falls to 0.5 (and to 1/e), from the Hankel transform of the projected brightness at 3.844 × 10⁸ m: 50 MHz, 1.6 km (2.2 km); 144 MHz, 560 m (770 m); 432 MHz, 186 m (256 m); 1296 MHz, 42 m (68 m); 2320 MHz, 24 m (38 m); 5760 MHz, 6.5 m (8.4 m); 10368 MHz, 3.4 m (4.1 m); 24048 MHz, 1.5 m (1.8 m). The 68-cm law was used at and below 432 MHz, so the VHF values are lower bounds; the 3.8-cm law was used above 10 GHz, so those are upper bounds. For self-echoes the displacement scale is halved. Consequences: (i) stations separated by tens of metres at 5.8 GHz have low modeled zero-lag correlation for this brightness law; exact independence and all time-lag correlations are not established; (ii) a self-echo test samples the same statistical family as a two-station link but with a different spread (LL_self versus LL_AB) and an independent realisation, so self-echo fading records cannot be replayed as the fading of a two-station link; (iii) the two directions are reciprocal under matched polarization and effectively frozen reciprocal propagation; (iv) an aperture larger than the speckle scale is the same statement as a beam narrower than the disc. No published treatment specific to EME was found. The same physics underlies radar speckle tracking of planetary spin states: the speckle pattern of the echo is tied to the target's rotation and sweeps over the Earth, with a characteristic scale rλ/d (r the range, d the diameter of the scattering area); echoes recorded at separate antennas are normally weakly correlated, and correlations above 0.6 appear only for about 30 s when the speckle trajectory is aligned with the baseline, at time lags of about 20 s (Venus at 3.5 cm: speckle scale about 0.7 km, Goldstone–Green Bank baseline about 3,000 km) ([MARGOT21] Methods, [P]; theory by Green (1962, 1968) and Holin (1988, 1992), secondary through [MARGOT21]). For the Moon, rλ/d with d = 2a gives 5.8 m at 5.76 GHz, close to the 6.5 m derived above from the peaked brightness law; the derived field correlation at 5.76 GHz is 0.27 at 10 m, 0.12 at 20 m and 0.06 at 30 m [E].

### Mean geometry: range, delay, Doppler shift and rate

- Distance: mean 384,400 km; mean perigee and apogee 363,300 and 405,500 km; overall range roughly 357,000–407,000 km ([MOONFACT], [G]). The radar-determined mean centre-to-centre distance is 384,400.2 ± 1.1 km (Yaplee et al., in [EV65] abstract, [P]). The truncated series gives geocentric extremes of 356,470–406,690 km over 1990– 2040 and topocentric distances to the Moon's centre of 350,500–406,600 km [E].
- Delay: 2d/c is 2.4 s at perigee, 2.7 s at apogee and 2.56 s on average ([HBK10] §30.9.2, [G]). Topocentric round trip to the Moon's centre is 2.338–2.713 s; the echo's leading edge, from the sub-radar point, arrives 11.6 ms earlier [E]. Measured at 7840 MHz from 42.6°N: 2367.5–2648.5 ms to the lunar surface, ±3 µs ([LL68] p. 47, Table IX, [T]).
- Delay rate: 2ṙ/c, up to ±3.5 µs/s at the equator and ±2.8 µs/s at 40° [E]; diurnal delay change up to about 42 ms between horizon and zenith (2 × 6378 km/c), monthly swing about 0.33 s [E].
- Doppler shift, self-echo, 5760 MHz [E]: maximum ±19.9 kHz at the equator (518 m/s range rate in the 2025 sample; up to about 539 m/s and ±20.7 kHz if the largest orbital radial velocity, 74 m/s, coincided with an equatorial moonrise), ±16.0 kHz at 40°, ±13.5 kHz at 52°, ±10.2 kHz at 65°; medians of the absolute shift 11.3, 8.5, 6.7 and 4.5 kHz. Multiply by 1.0069 for 5800 MHz. Published: two-way shifts as large as ±440 Hz at 144 MHz, ±4 kHz at 1296 MHz and ±30 kHz at 10 GHz, i.e. about 3.05 ppm or ±17.6 kHz at 5760 MHz ([K1JT16] p. 1; [HBK10] §30.9.3, [G]); these round figures correspond to Earth rotation alone. Measured at 7840 MHz from 42.6°N: −15,657 to +19,321 Hz (±0.15 Hz), i.e. −11.5 to +14.2 kHz scaled to 5760 MHz ([LL68] pp. 47–48, Tables IX–X, [T]).
- Doppler rate, self-echo, 5760 MHz [E]: maximum 1.25 Hz/s at the equator, 0.95 at 40°, 0.76 at 52°, 0.52 at 65°; medians 0.81, 0.63, 0.51 and 0.34 Hz/s. Analytic check: 2 ω_E² R_E cos(lat) cos δ / λ = 1.30 Hz/s at the equator. Measured at 7840 MHz on 25–26 July 1966: −1.14 to −1.17 Hz/s around transit, i.e. −0.84 to −0.86 Hz/s at 5760 MHz ([LL68] Table X, differenced, [T]). At 10 and 24 GHz the shift can change by more than 200 Hz per minute ([VK7MO] p. 1, [G]).
- Two-station links, 5760 MHz, 2025 [E]: 40°N 75°W to 50°N 10°E, median shift 3.6 kHz, maximum 10.2 kHz, rate up to 0.64 Hz/s; 50°N 10°E to 35°N 139°E, maximum 6.7 kHz; 40°N 75°W to 23°S 46°W, maximum 15.7 kHz and 1.0 Hz/s. The mutual shift is the sum of the two one-way shifts ([HBK10] §30.9.3, [G]).
- Higher-order terms [E]: the second derivative of the shift is at most about 1.3 Hz/s × 7.3 × 10⁻⁵ s⁻¹ ≈ 10⁻⁴ Hz/s², i.e. under 0.05 Hz departure from a linear ramp over 30 s. The range rate should be evaluated at the reflection epoch: using the reception epoch instead misplaces the shift by the Doppler rate times the one-way light time (up to 1.25 Hz/s × 1.28 s ≈ 1.6 Hz). Uncorrected drift is up to about 32 Hz in 26 s and 750 Hz in 10 min. Because the channel itself decorrelates in tens of milliseconds, deterministic higher-order terms matter only for keeping the mean Doppler centred to within a fraction of the spread.
- Prediction accuracy: with the 1960s lunar theory, observed-minus-predicted Doppler at 7840 MHz ranged from −1.04 to +0.64 Hz (measurement error ±0.15 Hz) and about 0.3 Hz (0.6 cm/s) remained unexplained after fitting station coordinates; delay residuals were −8.7 to +4.0 µs; ionospheric and tropospheric contributions were below the measurement error except under 10° elevation ([LL68] pp. 52–55, Table XIII, [T]; published as Smith et al. 1968, secondary). Modern ephemerides are far better: lunar laser ranging residuals are about 1.3 cm rms for recent data and DE440 and DE441 differ by under 2 m in the lunar orbit over the ranging era ([PARK21], [P]); Horizons serves the Moon from DE441 and provides light-time-corrected range and range rate for topocentric observers ([HORIZONS], [G]). Taylor states that EME Doppler can be calculated to parts in 10¹⁰ or better; his 0.1 Hz agreement at 144 and 432 MHz needed antenna coordinates to better than 1 km and time to better than 1 s; common programs were within 1 Hz at 1 GHz and a careful calculation within about 1 Hz at 10 GHz; ignoring UT1−UTC, nutation or using a series instead of the JPL ephemeris causes measurable errors ([K1JT16] pp. 1, 3–4 and Fig. 5, [G]). Scaled to 5.76 GHz [E]: a 1 s clock error gives up to 0.95–1.25 Hz, and a 1 km east–west position error is equivalent to about 2.8 s at 40° latitude, i.e. about 2.7 Hz; 0.1–0.3 Hz accuracy needs roughly 30–100 m and 0.1 s. Residual 24 GHz frequency error after Doppler correction with GPS-locked stations was under 20 Hz median ([VK7MO] p. 7, [G]).

### Direct lunar radar at 4–8 GHz, and 2015– 2026 literature

- Haystack, 7840 MHz (3.83 cm): observations from 1966, with the whole near side eventually mapped at 2 km in both circular senses; 200 kW peak, 66 dB gain, coherent integration periods 6–26 s, frequency standard better than 1 part in 10¹¹ ([ZPC74] Table I, [P]; [LL68] Table IV, [T]). These are the measurements closest to 5.8 GHz and the source of the 3.8-cm law used above.
- Mini-RF on the Lunar Reconnaissance Orbiter operates at 12.6 cm and 4.2 cm (called X-band by the team; within the IEEE C band). In bistatic mode it receives transmissions from Arecibo (12.6 cm, 2012– 2015, 28 observations) and from the Goldstone 34-m DSS-13 (4.2 cm; 35 observations in 2017– 2019), with about 100 m resolution ([EPSC19a], [G]). The 12.6-cm campaign sampled mare, highland, pyroclastic, crater-ejecta and crater-floor materials over a range of incidence and bistatic angles, and found responses consistent with an opposition effect in circular polarisation ratio only for the ejecta of several Copernican craters (varying with crater age) and for the part of the floor of Cabeus that is not in permanent shadow ([PATT17] abstract, [P]); the Cabeus peak is about 2° wide with a roughly 30 % rise in ratio, and was not seen in initial 4.2-cm data ([EPSC18], [G]). Crater-ejecta polarisation ratios have been characterised against bistatic angle at both wavelengths, with a clear opposition effect reported for several craters, and the 4.2-cm data for Anaxagoras and Kepler also suggest an opposition surge at low bistatic angles ([EPSC19b], [G]). A 2018 abstract places the increase in ratio near 2–4° bistatic angle and reports opposition-like behaviour at both wavelengths for Byrgius A, Aristarchus, La Condamine S and Kepler, and at 4.2 cm only for Anaxagoras ([GSA18], [G]). A 2022 abstract finds no indication of the effect in initial 4.2-cm data for the floor of Cabeus, but a response of different character on the floor of Amundsen ([EPSC22], [G]). Fassett et al.'s 2025 LPSC abstract analyzes 98 non-polar X/C-band bistatic collects: incidence angle is the first-order control on median m-χ polarimetric response, while phase/bistatic angle has modest median variation, with further terrain-specific analysis needed ([FASSETT25], [G, conference abstract]). These are local SAR pixels at 4.2/12.6 cm with varying incidence, emission and phase, not a disc-integrated cross-section or an Earth–Moon–Earth channel. No disc-averaged 4.2-cm scattering law or cross-section from Mini-RF was found.
- Relevance: the bistatic angle of a two-station EME link at the Moon is at most about 2° [E], the range over which these opposition effects occur. The classical assumption that two-station echoes scatter exactly like self-echoes is therefore not strictly guaranteed terrain by terrain, though any disc-integrated effect is unquantified and probably small, since the effect was reported for young crater ejecta and polar crater floors but not for the mare, highland and pyroclastic terrain also sampled ([PATT17] abstract).
- Jicamarca, 49.92 MHz (6 m): polarised and depolarised range–Doppler images with about 10 km range and 20 km Doppler resolution, the north–south ambiguity resolved by two-antenna interferometry; depolarised returns strongest from large young craters, weaker from maria (attributed to subsurface loss), with unexpectedly low backscatter from an old basin in the Schiller–Schickard region and from an area poleward of Mare Imbrium ([VIER17], abstract only, [P]).
- Earth-based imaging 2001– 2015 (Arecibo transmit, Green Bank receive, 12.6 and 70 cm; 20–600 m resolution) refined the regional picture — loss controlled by ilmenite/TiO₂, buried rocks, melt deposits — rather than the disc-averaged law ([CAMPBELL16], [P]). A 2011 model of scattering at 13 and 4 cm for ice detection exists ([TUH11], abstract only, [P]).
- Absolute calibration of the modern maps does not supersede the classical disc-integrated values. The 12.6-cm near-side maps (40 m single-look, 80 m four-look) gave well-calibrated circular polarisation ratios, but their backscatter coefficients, though consistent between runs, were a factor of 8–10 lower than expected from the full-disc measurements documented by Hagfors (1970), possibly because of aliased system noise; radiometric calibration was then ongoing ([CAMPBELL10] author manuscript pp. 2, 6, [P]). Six years later the 70-cm absolute calibration was judged successful, whereas the 12.6-cm estimates still differed significantly from the earlier disc-integrated values ([CAMPBELL16], [P]). The same manuscript cites Hagfors (1970, Fig. 23) for disc-integrated circular polarisation ratios rising toward the limb to maxima of about 0.4–0.5 at 68 cm and 0.5–0.6 at 23 cm ([CAMPBELL10] author manuscript pp. 7–8, secondary).
- Green Bank Telescope "next generation" radar pilot (Raytheon transmitter of under 700 W at Ku band, reception with the VLBA, observations 2020– 2021): images of the Apollo 15 landing-site region at about 1.25 m and of Tycho at about 5 m resolution; the planned system is 500 kW at 13.7 GHz ([NGRADAR23], [G]). These are regional images; no disc-integrated cross-section or scattering law was reported.
- The 2019 JPL analysis of a lunar south-pole asset downlink to a directive DSN dish explicitly says no local scattering-function or power-delay measurements were available, then derives an analytic WSSUS fading model whose Doppler profile and coherence depend on antenna directivity ([JPL216C], [T, full report read]). This is useful as a geometry/antenna alternative, but its near-grazing lunar transmitter-to-Earth downlink is not passive EME. Gao et al. (2023) estimate regolith dielectric properties from local Mini-RF hybrid-polarimetric SAR, including Apollo and Chang'e-5 sites, with reported RMSE 0.53 against sample measurements ([GAO23], [P, abstract and bibliographic record; publisher full-text fetch blocked]). Campbell (2025) finds that m-χ/Stokes components do not uniquely separate single- and double-bounce power because subsurface and random-polarisation contributions overlap; the evidence considered is principally 12.6-cm and 70-cm ([CAMPBELL25], [P, publisher preview/abstract read]). The 2024 Mini-RF S-band paper concerns monostatic image orthorectification and empirical topographic correction ([KIRK24], [P]). These improve local-material and interpretation choices; none supplies a whole-disc 5.8-GHz EME kernel.
- Not verified: Campbell et al. (2007), Campbell (2002), Chandrayaan-2 dual-frequency SAR results, and Mini-RF monostatic 4.2-cm polarisation statistics. None of the modern work obtained here revises the Evans–Hagfors disc-averaged law; it adds regional detail.

### Historical HF and VHF lunar radar

- Kerr and Shain (1951): about 20 MHz (21.54 MHz at 70 kW, 17.84 MHz at 50 kW, Radio Australia at Shepparton, received 600 km away at Hornsby); 30 experiments in about a year, echoes in 24. Echo intensities were below expectation, echoes first appeared at unexpectedly high elevation, vertical ray deviations of several degrees occurred, and the anomalies correlated with F2 critical frequency. Slow fading was ionospheric; fast fading (order of seconds) was attributed to libration; millisecond pulses were elongated. They concluded the Moon was a "rough" reflector ([KS51] pp. 230, 241–242, [P]), a conclusion later overturned ([EV65] p. 1642, [P]).
- Davis and Rohlfs (1964) and Davis et al. (1965): 11–22 m, monostatic, 250-µs pulses; cross-sections 0.13–0.19 (later 0.15–0.18 at 11.3 m); Faraday fading with periods from under a minute to several minutes and nulls 18–27 dB deep; libration fading of 2–20 s; travelling ionospheric disturbances with 20–40 min periods producing focusing; persistent discrete echoes up to 9 ms behind the leading edge; observations possible only well after ionospheric sunset ([DA65] pp. 1659–1664, [P]).
- Evans and Ingalls (1962): 7.84 m, cross-section 0.06 (±5 dB), spectrum indistinguishable from 68 cm but possibly affected by Faraday fading and scintillation ([EV65] Table 1 and p. 1644, [P]). Klemperer (1965) measured the 6-m scattering law at Jicamarca (cited in [LLQ2]; not read).
- HAARP–LWA: on 28–29 October 2007 the 3.6 MW HAARP array transmitted 2-s pulses every 5 s at 7.4075 and 9.4075 MHz, one hour per frequency per day; echoes were received on Long Wavelength Array antennas in New Mexico and described as the lowest-frequency lunar bistatic radar measurement ([HAARP08], press release, [G]). A repeat for amateur listeners on 6.7925 and 7.4075 MHz in January 2008 is known only from search-result summaries of a press report that could not be opened. A targeted search for a conference, memo or journal record found only the press release and its reposts; no technical analysis was found.
- Early VHF: 198 MHz short-pulse work showing the central highlight ([TR58], [P]); Browne et al. (1956) found deep Faraday nulls implying about 10 % depolarised power ([EV65] p. 1645, secondary).

## Moderate → adverse → worst-case parameter table

Values are for 5.76–5.8 GHz, whole-disc illumination and opposite-sense circular polarisation unless stated. "Worst case" combinations marked † have not been reported together.

| Parameter | Moderate | Adverse | Worst case / outage |
|---|---|---|---|
| Cross-section η = σ/πa² | 0.065–0.07, interpolated ([LL67], [EV65]; [T]/[P]) | 0.05 (low 10-cm value, ±3 dB, [EV65]; [P]) | 0.04 (low 3.6-cm value, ±3 dB, [EV65]; [P]) |
| Two-way isotropic path loss, 5760 MHz | 284.1 dB at mean distance [E] | 285.1 dB at apogee [E] | 287.2 dB: apogee with η = 0.04 † [E] |
| Monthly level variation | ±1 dB ([EV65]; [P]) | ±1.15 dB extremes [E] | 2.3 dB perigee to apogee [E] |
| Extra loss, beam narrower than disc | < 0.4 dB two-way for D ≤ 3.5 m [E] | 1.3 dB two-way, 0.7 dB one-way at D ≈ 7 m [E] | 3.4 dB / 2.2 dB at 14 m; 6.3 dB / 4.8 dB at 28 m [E] |
| Power in wrong circular sense (disc-integrated) | −7 dB, interpolated [E] | −6 dB at 3.8 cm [E from [ZPC74]] | −3 dB locally near the limb ([EV69] Fig. 14 at 23 cm; [P]) |
| Envelope statistics | Rayleigh, K ≈ 0 ([EV65], [ZPC74]; [P]) | Rayleigh | Rayleigh with second-scale fades at libration minima ([G3WDG10]; [G]) |
| Fade depth exceeded | 10 dB for 9.5 % of time [E] | 20 dB for 1 % [E] | 30 dB for 0.1 % [E] |
| Limb-to-limb Doppler spread, self-echo | 65–100 Hz (medians, 52°–40°) [E]; 86 Hz ([K1JT10]; [G]) | 110–145 Hz (90th percentile, 52°–40°) [E] | 165 Hz at 40°; 201 Hz at equator [E]; 164 Hz ([K1JT10]; [G]) |
| Half-power Doppler width | 12–19 Hz (FWHM, 0.19 LL) [E]; w50 = 25 Hz ([K1JT10]; [G]) | 21–28 Hz [E] | 38 Hz FWHM at LL = 200 Hz [E]; about 58 Hz if expressed as w50 (0.29 LL) |
| Band holding 90 % of power | 40–62 Hz (0.62 LL) [E]; w10 = 60 Hz ([K1JT10]; [G]) | 70–90 Hz [E] | about 125 Hz [E] |
| Field coherence time (correlation 0.5) | 11–17 ms [E] | 8–10 ms [E] | 5.7 ms [E]; opposite extreme: > 0.1 s for 0.15–1.1 % of time (LL < 10 Hz), about 1 s at deep minima [E] |
| Loss for 16.7 ms coherent integration | 0.5–1.0 dB [E] | 1.1–1.6 dB [E] | 2.3 dB at LL = 200 Hz [E] |
| Envelope maxima per second | 30–45 [E] | 50–65 [E] | 70–90 [E]; below 1 at deep minima |
| Half-power excess delay | 0.75 ms (0.5–0.95) [E] | 0.94 ms (3.8-cm law) [E] | — |
| Delay beyond which 10 % of power arrives | 5.1 ms [E] | 5.4 ms [E] | full extent 11.6 ms ([EV65]; [P]) |
| rms delay spread | 2.2 ms [E] | 2.3 ms [E] | 2.5 ms (Hagfors C = 40 without limb cut-off) [E] |
| Complex-gain correlation at 75 / 150 / 225 Hz separation | 0.67 / 0.53 / 0.46 [E] | 0.65 / 0.50 / 0.43 (3.8-cm law) [E] | no measurement at 4–8 GHz |
| Mean Doppler shift, self-echo | ±8.5 kHz median at 40° [E] | ±13–16 kHz (40°) [E] | ±20–21 kHz at equator [E] |
| Doppler rate, self-echo | 0.5–0.65 Hz/s (medians, 52°–40°) [E] | 0.95 Hz/s (40° maximum) [E] | 1.25–1.3 Hz/s (equator) [E] |
| Round-trip delay to Moon centre | 2.56 s ([EV65]; [P]) | 2.34–2.71 s [E] | drift ±3.5 µs/s; 42 ms diurnal swing [E] |
| Doppler prediction error (ephemeris and geometry only) | 0.1–0.3 Hz with JPL ephemeris, 30–100 m coordinates, 0.1 s time [E from [PARK21], [K1JT16]] | 1–3 Hz (1 s time error or 1 km position error) [E]; programs "better than 1 Hz at 1 GHz" ([K1JT16]; [G]) | several Hz if light time, UT1 or nutation are ignored [E] |
| Spatial decorrelation distance (one-way, correlation 0.5) | 6.5 m [E] | — | — |

## How the literature models or simulates this

- **Evans–Hagfors two-component model.** Quasi-specular Hagfors law plus cos^(3/2) diffuse law for the expected polarisation, cos law for the depolarised component, with wavelength-dependent C and diffuse fraction ([EV65], [EV69], [LL67]). Validated by short-pulse and delay–Doppler radar at 3.6–3.8, 23 and 68 cm, with sparser data at 8.6 mm, 10 cm, 6 m and 11 m. Known weaknesses: the Hagfors fit is poor at 3.8 cm; the law does not go to zero at the limb; decompositions into "specular" and "diffuse" are not unique (12–20 % at 70 cm; 30–35 % at 3.6 cm); regional structure and the 63° feature are outside the model; nothing is measured at 5–6 cm.
- **Delay–Doppler mapping.** The radar-astronomy processing model treats each resolution cell as an independent Gaussian scatterer whose mean power follows the scattering law times antenna pattern, with range–Doppler ambiguity north and south of the apparent equator ([LL68] §II, §VII; [ZPC74]). This is in effect a wide-sense-stationary uncorrelated-scattering description with a known scattering function.
- **Libration-rate prediction.** Fricker et al. computed total libration rate for separated stations and mapped it to fading rate with one empirical constant (0.67), validated at 412 MHz except for rates below about 0.05 Hz, where three-figure almanac data limited the prediction ([FR60]). Modern amateur software (MoonSked, EME Planner, WSJT-X) predicts limb-to-limb "width" for self-echo and two-station paths; WSJT-X Echo mode integrates signal power over the predicted full width ([G3WDG10], [K1JT10], [WSJTX-ECHO]). These predict the extent of the spectrum, not its shape.
- **Empirical width models.** [K1JT10] w50 and w10 curves versus frequency, and [G3WDG10] width = 6000 f × rate, are fits to amateur spectra, mostly at 144 MHz–2.3 GHz and 10 GHz; 5.7 GHz support consists of G3LTF's echoes and two two-station points.
- **Beam-limited link budgets.** Evans' Eq. 2 with a radial brightness law, as implemented in [WETTERSAT]; validated domain not documented.
- **Fading simulators in amateur weak-signal software.** The WSJT-X simulators for the EME modes (jt65sim, jt4sim and q65sim) multiply the whole signal by one random complex fading process, normalised to constant mean power, whose power spectrum is 1.111/(1 + x²) − 0.1 with x = 6 f / f_spread, cut off at |x| = 3; the user's "Doppler spread" f_spread is therefore the full base width of a truncated Lorentzian, in use since 28 March 2016, replacing a Gaussian ([WSJTX-SRC], [G]). Its FWHM is 0.30 f_spread, the bands holding 50 % and 90 % of the power are 0.20 and 0.56 f_spread, and the one-sided rms width is 0.16 f_spread [E]. With f_spread = LL this is close to the 5.2-cm whole-disc spectrum in its 50 % band and rms (0.19 and 0.17 LL) but has a broader core (FWHM 0.30 against 0.18 LL), no wings beyond ±LL/2 by construction, and no delay dispersion: it captures neither delay–Doppler coupling nor frequency selectivity. At UHF it would be much too broad for the same setting (23-cm law: 50 % band 0.12 LL). The HF simulator fst4sim offers a Watterson two-path model with delay and, alternatively, a Lorentzian fading routine ([WSJTX-SRC], [G]).
- **Speckle-pattern models.** Planetary radar astronomy treats the echo as a speckle pattern tied to the target's rotation that sweeps over the Earth (Green; Holin), and uses the cross-correlation of echoes received at two separated antennas to measure spin states of Mercury and Venus ([MARGOT21]). This is the explicit-scatterer view of option 3 below; it has not been applied to EME links.
- **Ephemerides.** JPL DE440/441 with Horizons for topocentric range, range rate and sub-observer coordinates ([PARK21], [HORIZONS]); [K1JT16] lists the steps (geodetic to geocentric conversion, UT1, nutation, ephemeris interpolation) needed for 10⁻¹⁰ Doppler accuracy.

## What a high-fidelity simulation would need

Inputs: date and time span; coordinates of both stations; frequency; antenna beamwidths, pointing offsets and polarisations at both ends; a lunar ephemeris with orientation (or an equivalent apparent-rotation model). Outputs to be generated as functions of time: mean delay and Doppler, limb-to-limb spread, and a random time-varying transfer function.

Model-structure options, with limits:

1. **Single-tap Rayleigh channel with a Lorentzian-plus-pedestal Doppler spectrum** whose width follows LL(t). Simplest, and close to what the WSJT-X simulators do with a fixed width and no pedestal ([WSJTX-SRC]); adequate when the signal bandwidth is well under about 30 Hz or beams are narrow. It cannot reproduce the partial decorrelation of carriers 75–225 Hz apart predicted for whole-disc illumination, and the spectrum shape at 5.8 GHz is interpolated, not measured.
2. **Tapped-delay-line WSSUS model built from the ring geometry.** Taps at delays out to 11.6 ms with powers σ₀(φ(t)) (opposite sense) and the depolarised law for the cross-polar channel; each tap given the ring Doppler density 1/(π√(f_r² − f²)) with f_r tied to delay. Reproduces delay–Doppler coupling, frequency correlation and polarisation behaviour. Limits: assumes statistical uniformity round each ring (no Tycho-like features), needs many taps to represent the power-law leading edge (10-µs structure is irrelevant for ≤ 3 kHz signals, so taps of 50–100 µs suffice), and inherits the uncertainty of the scattering law at 5.2 cm.
3. **Explicit scatterer (facet) simulation.** Several thousand random point scatterers on the visible hemisphere, weighted by the scattering law, antenna patterns and optionally a reflectivity map, with phases advanced from the true geometry (station positions, ephemeris, lunar orientation). Naturally gives non-stationary spread, two-station geometry, reciprocity, spatial decorrelation between sites, beam and pointing effects, and Doppler-centroid shifts. Limits: scatterer count and regeneration strategy affect statistics; depolarisation needs an explicit per-scatterer polarisation model (for example the dipole fraction of 65–70 % at oblique incidence); computational cost; still depends on the assumed law.
4. **Replay of recorded echoes.** Self-echo or beacon recordings (a 10 GHz EME beacon with a 7.2-m dish and 0.3° beam is listed in [WETTERSAT]; an archive of amateur echo recordings is mentioned in [G3WDG16]) give real statistics but for one geometry, band and beam, and self-echo records are not statistically identical to two-station links.

Whichever structure is used, the following should be explicit: the amplitude law (Rayleigh); the time variation of LL through each pass and month including minima; path-loss variation with distance; beam-narrowing loss and its reduction of delay and Doppler spread; polarisation coupling; and deterministic mean Doppler and delay with their rates. Better approaches may exist, for example using measured delay–Doppler images as the brightness map at the nearest wavelength.

Observables against which to validate:

- Total cross-section 0.065 ± 0.008 at 23 cm and the Table 1 values; path loss 284.1 dB at 5760 MHz at mean distance.
- Cumulative power versus delay (table above) and Evans' 68-cm pulse-length curve.
- Frequency correlation 0.3 at 500–1000 Hz separation at 440 MHz ([IBD61]).
- Fading rate equal to 0.67 × maximum limb offset at 412 MHz ([FR60]).
- Spectrum widths: [K1JT10] w50 and w10; 19 Hz half-power width at LL ≈ 90 Hz at 10 GHz ([VK7MO]); two-station widths of 20 and 35 Hz at 5.7 GHz on 16 May 2010 at 0741 UTC ([G3WDG10]); wings reaching the predicted limb values.
- Polarisation: opposite/same-sense ratio versus angle at 3.8 cm ([ZPC74] Fig. 10) and 23 cm ([EV69] Fig. 14); 6.5 dB circular and 10–11 dB linear discrimination at 10 GHz ([G3WDG16]).
- Mean Doppler and delay: Haystack Tables IX–X ([LL68]) and Horizons.
- Rayleigh first-order statistics (1/√N reduction of fluctuation with N looks, [ZPC74]).
- Spatial correlation between separated receivers: low except along the speckle trajectory, with a scale of order rλ/d ([MARGOT21] for Venus; about 6 m at 5.8 GHz for the Moon [E]).

## Comparison with the modem's presets

Each preset of the simulator ([00](00-method-and-modem-context.md), section 5) is an excess-delay value and a Doppler value that bound a two-path family whose direct path never fades. Row 8 of the manual list (5.8 GHz, 225 Hz occupied, 26.4-s bits of 66 hops, 10 dB-Hz) uses "EME focused: 6 Hz, 0.5 ms".

- **EME focused, whole-disc illumination** (dishes up to about 3–4 m at 5.8 GHz): the half-power delay is about 0.75 ms (0.5–0.95 ms across weighting conventions and wavelength interpolation), so 0.5 ms is a fair median-delay value; but 25 % of the power arrives later than about 2.5 ms and 10 % later than 5 ms, which a single 0.5-ms ray cannot represent. The Doppler value is low: limb-to-limb spread is typically 65–100 Hz and up to 165–200 Hz, with 50 %-power bands of 16–19 Hz typical and 31–38 Hz at worst (published w50 of 25 Hz and about 58 Hz). A 6 Hz half-power width needs LL ≈ 30 Hz, which occurs for about 1.4–10 % of Moon-up time depending on latitude and year; a 6 Hz total spread occurs in 12–42 episodes per year averaging 12–24 minutes (0.05–0.4 % of Moon-up time) [E].
- **EME focused, narrow-beam illumination:** to obtain a 6 Hz half-power band at the median spread requires a two-way beam of about a quarter of the disc (about 28 m at 5.76 GHz), and about 70 m at the maximum spread; such beams give half-power delays of 0.02–0.08 ms and rms delay spreads of 0.03–0.2 ms, not 0.5 ms [E]. A 7–14 m dish reduces delay to 0.2–0.5 ms but leaves the 50 % Doppler band at 0.11–0.16 LL (10–14 Hz at the median) [E].
- **Geometric consistency:** a ring at 0.5 ms excess delay has radius 0.29 a and Doppler offsets up to ±0.145 LL, i.e. ±12 Hz at the median spread. The pair (6 Hz, 0.5 ms) corresponds to LL ≈ 40 Hz, about half the median at 40° latitude [E].
- **What the preset omits:** Rayleigh statistics (two rays whose direct path never fades give quasi-periodic beats rather than random fades; the lower-level independent random ray gains of [00](00-method-and-modem-context.md) section 5 would fade each ray, but on only two delays and with an Ornstein–Uhlenbeck or Gaussian spectrum rather than a sharply peaked one with wings to the limb); the coupling of delay with Doppler; the diffuse tail to 11.6 ms that decorrelates carriers 75–225 Hz apart (predicted correlation 0.67–0.46); the hour-scale and monthly variation of spread from near zero to 200 Hz; the 2.3 dB peak-to-peak monthly path-loss variation about an isotropic value near 284 dB at 5.76 GHz; and mean Doppler of up to ±20 kHz drifting at up to 1.25 Hz/s. The simulator's flat 180–200 dB can only be read as loss net of antenna gains: two 3-m dishes at 5.76 GHz contribute about 85 dB (42.6 dBi each at 55 % efficiency), giving about 199 dB [E]; the gain bookkeeping belongs with the equipment notes.
- **The other presets.** None, the three Sub9 presets and HF best (Doppler at most 0.1 Hz) lie below every lunar spread at 5.8 GHz except the deepest libration minima (LL < 2 Hz for at most 0.05 % of Moon-up time) [E]. HF moderate (10 Hz, 6 ms), HF worst (30 Hz, 7 ms), Aurora best (10 Hz, 3 ms) and Aurora worst (55 Hz, 11 ms) bound delays and offsets of the same order as the whole-disc scattering function at 5.8 GHz (support 11.6 ms; offsets within ±LL/2, i.e. ±33–50 Hz at median spread and up to ±100 Hz), Aurora worst coming closest in extent. As two discrete paths with a direct path that never fades, none of them reproduces the Rayleigh statistics, the continuous power-law delay profile, the delay–Doppler coupling or the 0.46–0.67 correlation between carriers 75–225 Hz apart [E].
- **Whole-bit coherence:** for row 8, 1/T = 38 mHz, and a statistic coherent over a whole bit loses about 3 dB to a diffuse spread of about 1.2/T ≈ 45 mHz ([00](00-method-and-modem-context.md), section 4.1). The 50 %-power band of the lunar echo exceeds this by a factor of about 400 at median spread and still by about 8 at the deepest libration minima (LL ≈ 2 Hz, band ≈ 0.4 Hz), so whole-bit coherence is unavailable on this channel except perhaps for moments near an exact libration zero; within one 16.7-ms cell the coherent loss is 0.5–1.0 dB at median spread and 2.3 dB at the extreme (table above) [E]. If Helper pilots several hundred hertz or more from the DATA carriers are used, the predicted whole-disc correlation of their complex gain with that of DATA is only about 0.3 at 500 Hz and 0.2 at 1 kHz separation [E; no measurement at 4–8 GHz].
- **Mean Doppler and drift:** the simulator's static Doppler offset and deterministic transmit frequency slope can represent the mean shift (up to ±20 kHz) and its rate (up to 1.25 Hz/s) over one bit or one 90-s window, since the curvature moves the shift by only about 0.1–0.4 Hz from a straight line over 90 s, small against the spread; they cannot represent its evolution through a pass [E]. Uncorrected, the drift over one 26.4-s bit is up to about 33 Hz [E].
- **Level and noise:** row 8 sets 40 W and 200 dB against the default −164 dBm/Hz, giving 10 dB-Hz. That density corresponds to a system temperature of about 2,900 K, whereas an 80-K receiver at 5.8 GHz has −179.6 dBm/Hz ([00](00-method-and-modem-context.md), section 3.1). With 284.1 dB isotropic loss at mean distance, 10 dB-Hz at 80 K and 40 W implies antenna gains totalling about 68.5 dB (two dishes of about 1.15 m at 55 % efficiency), while two 3-m dishes would give about 26.6 dB-Hz [E]. For beams comparable with or narrower than the disc, the Moon's own emission raises the system temperature: Haystack's 3.8-cm receiver had 45 K on cold sky and 180 K (polarised channel) on the Moon ([ZPC74] Table I, [P]). A constant simulated level also omits the 2.3 dB perigee–apogee swing, beam-narrowing loss of several dB for large dishes and the Rayleigh fading of the echo [E].

## Contradictions, open questions, and things not found

- **Half-power delay.** [K1JT10] quotes 120, 210 and 650 µs at 0.43, 1.3 and 8 GHz. Integration of the Lincoln Laboratory table with surface-area weighting gives 153, 270 and 936 µs; Evans' measured 68-cm pulse-length curve gives about 155 µs and follows the surface-area integration out to 5 ms; Trexler's 50 µs at 198 MHz was sensitivity-limited. The [K1JT10] figures are reproduced by projected-area weighting, which overweights the disc centre by cos φ, fails the table's 0.065 normalisation at 3.8 cm (0.055) and departs from Evans' curve; surface-area weighting is required for LL67’s stated table convention. This does not establish an error in Hagfors (1970) or Taylor’s original estimator, whose full derivation has not been reproduced; preserve both estimators rather than silently choosing the shorter one.
- **Meaning of w50.** Published w50 (0.17–0.37 LL) exceeds the 50 %-power band computed from the scattering law (0.09–0.21 LL), while the 10 GHz spectrum in [VK7MO] has "nearly half" its power within 0.11 LL. The [K1JT10] definition (full width of the spectrum's autocorrelation at half maximum) gives 2.2–2.6 times the 50 %-power band for the measured laws, and the measured w10/w50 ratio of about 3 fits that definition; the published values are still about 0.8 times the computed autocorrelation widths, which is unexplained [E]. Definitions (autocorrelation width, FWHM, power-containing band, visually estimated "echo width") remain mixed in the literature.
- **Diffuse fraction** near 3.6–3.8 cm: 30 % ([EV69]) versus 35 % ([LL67]); at 70 cm 12 % versus 20 %. The 3.8-cm polarised laws of [LL67] and [ZPC74] differ by 1–3 dB at middle and large angles.
- **Cross-section wavelength dependence** is unresolved in the classical data (0.04 and 0.07 at 3.6 cm; 0.022 at 12.5 cm; 0.13–0.19 at decametre wavelengths with suspected ionospheric bias). Nothing newer on absolute disc-integrated cross-section was found, and an amateur tool states that no newer data exist ([WETTERSAT], [G]); the 12.6-cm backscatter coefficients of the modern Arecibo–Green Bank maps fell a factor of 8–10 below the classical disc-integrated values in 2010, possibly because of aliased system noise, and still differed significantly in 2016 ([CAMPBELL10]; [CAMPBELL16], [P]).
- **Circular versus linear at 10 GHz.** The measured CP-to-CP deficit of 1.3–1.6 dB ([G3WDG16]) exceeds the roughly 0.4–0.5 dB expected from the quoted discriminations (6.5 dB circular, 10–11 dB linear) and from the two-component budget [E]; antenna effects may contribute.
- **Handbook versus later amateur work.** The Handbook attributes frequency spread to orbital libration alone (0.2 m/s, about 0.2 Hz at 144 MHz) ([HBK10] §30.9.3), whereas [K1JT10] and [FR60] show that Earth rotation is usually the larger term; the Handbook figure is an effective width, not the limb-to-limb spread.
- **Kerr and Shain's "rough reflector"** conclusion at 20 MHz contradicts all later work; their fading and intensity anomalies were largely ionospheric.
- **Bistatic-angle (opposition) effects** seen by Mini-RF at 0.5–4° (12.6 cm, and for some young craters at 4.2 cm) are absent from the classical model; their disc-integrated consequence for two-station EME is unknown.
- **Overspread condition.** [LL68] (p. 10) states that the Doppler spread, even at high microwave frequencies, is small compared with the reciprocal of the total delay spread corresponding to the lunar radius. For the whole disc this conflicts with Evans' limit of about 5,000 Mc/s ([EV65] pp. 1653–1654) and with the computed spreads at 5.76 GHz (LL of 65–200 Hz against 1/11.6 ms = 86 Hz); it holds for the small region inside Haystack's beam, which is what that report needed [E].
- **Historical search record (before the 2026-11-13 completion pass; current dispositions below):** any measurement at 5–6 cm of total cross-section, scattering law, polarisation ratios or frequency correlation; any measured whole-disc Doppler spectrum at 3–6 cm (the professional 3.6- and 3.8-cm work measured delay profiles and narrow-beam delay–Doppler maps); any measurement of total echo versus libration state; any published analysis or measurement of speckle spatial decorrelation for EME specifically (the planetary speckle-tracking literature gives the theory and measurements for Mercury and Venus, [MARGOT21]); any published measurement of spread or cross-section versus dish size at 5.7 GHz; any conference, memo or journal record of the HAARP–LWA experiment; any journal paper on the Mini-RF 4.2-cm bistatic campaign.
- **Historical access record (current dispositions below):** Evans & Hagfors (eds.) *Radar Astronomy* (1968); Hagfors (1961, 1964, 1967, 1970) full texts; Daniels (1961, 1963); Evans & Pettengill (1963) and Evans & Hagfors (1966) full texts (used through the Lincoln Laboratory reports and Evans' reviews); Pettengill & Henry (1962); Browne et al. (1956); Thompson (1970– 1987) apart from the first page of the 1979 review; Campbell (2002, 2007); Vierinen et al. (2017), Patterson et al. (2017) and Thompson et al. (2011) beyond abstracts; Holin (1988, 1992) and Green (1962, 1968); journal papers on the Green Bank/VLBA radar, and orbital SAR papers of 2020– 2025 (Chandrayaan-2 dual-frequency SAR, Mini-RF monostatic 4.2-cm statistics). This records the earlier access state, not the later source-specific remedies or proof of absence.
- **Bibliographic notes:** the 1961 Proc. IRE band-pass letter is by Ingalls, Bird and Day; the QEX 2016 article is by Taylor alone, and its reprint is footed September/October 2016, whereas the WSJT-X 2.6.0 echo-mode note cites it as November–December 2016, pp. 36–40 ([WSJTX-ECHO]); the HAARP–LWA detections were in October 2007 at 7.4075 and 9.4075 MHz; the Davis–Rohlfs–Skaggs–Joss paper is titled "Decameter-wave radar studies of the lunar surface"; the 2010 Icarus paper by Campbell et al. is titled "Earth-based 12.6-cm wavelength radar mapping of the Moon" ([CAMPBELL10]).
- **Limits of the derivations here:** the ephemeris is a truncated series adequate for statistics only; the 5.2-cm scattering quantities are log-wavelength interpolations between 3.8 and 23 cm of the opposite-sense circular law, and linear co-polar reception would see a somewhat larger diffuse share; beam results assume Gaussian beams centred on the disc; coherence-time and correlation figures depend on the stated definitions.

## Completion pass: source decisions and transfer limits (2026-11-13)

The old missing-source lists above are retained as history. They are not live
instructions to search indefinitely, and removing their words would not repair
their underlying claims. The following dispositions distinguish source access,
a specified mathematical model and empirical qualification. The numerical
[84](84-scenarios-eme.md) contract is now fixed; whole-disc 5.76 GHz validation
remains unavailable. None of the new local SAR results closes that gap.

### Critical evidence chain

| Claim / selected input | Inspected primary locator and geometry | Use and remaining limit |
| --- | --- | --- |
| Per-surface-area OC delay law and mean cross-section | LL67 Table II, printed p.6/PDF index 8; §III.A.1 pp. 7–9; 3.8/23/68 cm Earth radar | Reopened source text confirms surface area, selected circular polarization and normalization. 84 supplies all selected numeric rows and interpolation; 5.2 cm is derived, not measured |
| Hagfors-shaped alternative | LL67 Eq.33 p.18 and Table III p.19, C=95/65/40 at 68/23/3.8 cm; Hagfors 1964 original scan identified, but page images not available through this tool | 84 uses C=44, explicitly normalized, as an alternative; LL67 is the actual numerical source, not an assertion of newly reading every inaccessible historical paper |
| Interpretation of C and model physics | Sultan-Salem and Tyler 2006, §§2–3 and abstract/conclusions, original publisher full text | The Kirchhoff-law model has energy-conservation and scale-dependent slope limitations. A good backscatter fit does not make C a unique physical RMS slope or justify the full bistatic field law |
| Later 3.8-cm maps | ZPC74 pp. 19–20,45–46 from the earlier page-image reading; alternative original scan located during this pass | Local maps and angular law, narrow Haystack beam; not a new measured whole-disc 5.76 GHz spectrum. Current alternative scan could not be visually rechecked; prior reading is retained, not upgraded |
| Depolarization / diffuse fraction | LL67 §III.A.2 p.9, EV69 review and ZPC74 loci above | Angular shape and band dependence justify uncertainty, not unique 5.76 GHz polarization statistics. 84’s ratios/correlation are fixed assumptions |
| 2026 X/C bistatic SAR | Peters et al. 2026, IEEE TGRS 64,5200913, §II.B and §III, full original article text via author-uploaded copy | Earth DSS-13 transmitter/LRO receiver; calibration and focused Stokes products. This **remedies the old claim that no journal paper was found**, without providing whole-disc EME data |
| Recent calibration counterevidence | Chan et al. 2025 LPSC 2294, pp. 1–2; Peters 2026 §II.B | Non-negligible H/V crosstalk requires correction; a raw orbital circular-polarization ratio is not an intrinsic lunar coefficient transferable without instrument calibration |
| Bistatic-angle dependence | Fassett et al. 2025 LPSC 2129, full two-page abstract; 98 local collects | Local incidence/phase response constrains transfer assumptions, not a disc-integrated opposition multiplier. 84’s OPP is explicitly synthetic |
| Public geometry | Park et al.2021 DE440/441 and Horizons manual | Public ephemeris may drive prediction; per-cell scattering and true fades remain evaluator-only. Full retarded two-leg solution is required; the old truncated annual series is statistical context only |

The later calibration paper reports an H/V imbalance of 11±0.3 dB and phase
91.5±1° during a 7140.2 MHz direct CW calibration; lunar backscatter produces a
different imbalance. These are **instrument** observations, not a lunar
polarization ratio. The 2025 abstract proposes cross-channel correction and
reprocessing of archival products. Consequently a local-map alternative must
bind the processing/calibration version and retain complex channels, not simply
paste uncorrected CPR pixels into the whole-disc kernel.

The original Hagfors law remains useful as a competing shape. The 2006 analysis
shows why fitting a normalized shape is a much narrower claim than solving
all-direction electromagnetic scattering. In particular, the quasispecular
highlight is not evidence for a deterministic direct path immune to Rayleigh
fading. The chosen discrete proper-Gaussian field has coherent realization
power and an incoherent **ensemble** sum; those are different quantities.

### Every original missing-source/topic obligation

“Not selected” below withdraws that source as a necessary numerical authority
for this 5.76 GHz experiment; it does not claim that the source is unimportant or
that its complete contents were read. “Open empirical” is a real limit on
qualification, not a missing parameter choice in 84.

| Original obligation | Current disposition and effect |
| --- | --- |
| 5–6 cm total cross-section | Open empirical. Selected η=.065 with .040 sensitivity is extrapolation; no newly acquired calibrated measurement |
| 5–6 cm angular scattering law | Open empirical; fixed T52/T38/T23/H25/H30/H35 alternatives replace unspecified interpolation choices |
| 5–6 cm polarization ratio | Open empirical; r=.10/.20/.30 controls are assumptions, never estimated availability |
| 5–6 cm frequency correlation | Derived for the fixed kernel in 84; measured joint carrier covariance remains open |
| Whole-disc 3–6 cm Doppler spectrum | No matching calibrated public record identified in the searches below; local radar images and visual amateur widths cannot substitute |
| Total echo versus libration state | Not established empirically; surface power/geometry coupling is a derived prediction and must not be fitted to a width-only observation |
| EME spatial-speckle decorrelation | Margot 2021 supplies planetary method context, not Moon-specific calibration. No distributed-station diversity gain is claimed |
|5.7 GHz spread/cross-section versus dish size | Open empirical; beam-integral sensitivity is explicitly calculated at matched kernel and Tsys |
| HAARP–LWA conference/memo/journal record | Retain official 2008 report as historical detection evidence only; it supplies no 5.76 GHz coefficient and is not a selected model input |
| Mini-RF 4.2 cm bistatic journal paper | Remedied by Peters 2026 original full-text sections; source geometry remains Earth-to-orbit/local SAR |
| Evans–Hagfors 1968 book | Not obtained; numerical dependency replaced by original LL67/LL68/EV65 material. No unseen book passage is used as direct evidence |
| Hagfors 1961 | Not selected; LL67 provides the applied angular law and measured fit coefficients |
| Hagfors 1964 | Original four-page scan route found; indexed abstract read, images unavailable here. LL67 Eq.33 plus 2006 primary derivation supports the selected functional form |
| Hagfors 1967 | Not selected as numerical authority; the fitted law and diffuse alternatives are explicitly constrained by inspected LL67 |
| Hagfors 1970 | Publisher abstract/reference record opened; full article not obtained. The half-power estimator disagreement remains bounded, not falsely attributed to an author error |
| Daniels 1961 | Not selected; no independent numeric 5.76 GHz parameter attributed to this uninspected source |
| Daniels 1963 | Not selected for the same reason; surface weighting follows LL67’s explicit convention |
| Evans–Pettengill 1963 | Primary measurements accessed through the same laboratory’s LL reports; journal article itself remains secondary/uninspected |
| Evans–Hagfors 1966 | Same primary-report replacement; article is not represented as newly read |
| Pettengill–Henry 1962 | Not selected; early ranging/history does not set this angular/field model |
| Browne et al.1956 | Not selected for microwave extrapolation; historical long-wave evidence retains its secondary status |
| Thompson 1970– 1987 series | Historical unspecified series is not a reproducible source request; retain 1979 first-page receipt, withdraw whole-series authority for this model. No uninspected year/article supplies a selected value |
| Campbell 2002 book | Not obtained; not needed for the explicit scalar/kernel contract |
| Campbell 2007 focused 70 cm mapping | Not selected for C-band coefficients; existing Campbell 2010/2016 calibration evidence carries the map-transfer caution |
| Vierinen et al.2017 | Publisher abstract/method-preview route reopened:49.92 MHz Jicamarca, circular TX/two linear RX, one observation pass. It is 6 **metres**, not 6 cm; no whole-disc C-band substitution |
| Patterson et al.2017 | Publisher preview and official archive routes identified; twelve-centimetre Earth-to-LRO products. Peters 2026 supplies the newer selected architecture/calibration detail |
| Thompson et al.2011 | Abstract-only physical icy-regolith modeling remains a lead; no claimed ice/depolarization decomposition is required by 84 |
| Holin 1988 | Not selected for empirical lunar speckle correlation; retain theory lead only |
| Holin 1992 | Same disposition; no independent-station decorrelation claim from its unseen contents |
| Green 1962 | Not selected; explicit retarded-path equations and LL reports replace a hidden measurement-method dependency |
| Green 1968 chapter | Book unavailable; no numerical result depends on its unseen text |
| Green Bank/VLBA journal work | Official ngRadar 2025/2026 results and 2025LPSC 2802 show 13.9 GHz local imaging with a 700 W demonstrator; no identified 5–6 cm whole-disc time series. Trade/proceedings evidence is not relabeled a full peer-reviewed channel study |
| Chandrayaan-2 dual-frequency SAR 2020– 2025 | L/S-band local geology/polarimetry, not selected as 5.76 GHz whole-disc calibration; no claim that this entire literature was read |
| Mini-RF monostatic 4.2 cm statistics 2020– 2025 | Chan 2025 supplies a concrete calibration/reprocessing caution; PDS4 products are public but not integrated here. Texture-based extension remains empirically unqualified |

### Search coverage and actual routes

Executed 2026-11-13; dates below are publication dates, not search-engine crawl
ages. Searches used exact English titles/author names plus mechanism/band
queries, followed by original articles, NASA/NIST/ADS, publisher pages,
author-institution records, official proceedings and PDS/Zenodo. This is a
reproducible targeted 2015– 2026 update with backward-reference checks, **not** a
claim to have read every paper in that interval. Older foundations are retained
because they contain the selected measurements; recency alone cannot replace
matching wavelength/geometry/calibration.

| Actual query or direct route | Returned material inspected / disposition |
| --- | --- |
| `"Hagfors" "1970" "189" "227" lunar surface radar pdf`; publisher DOI 10.1029/RS005i002p00189; ADS scan route | Publisher abstract/reference record; full-text absence remains explicit |
| `"Hagfors" "Backscattering from an undulating surface" pdf`; `"High-resolution radar maps" "Zisk" "1974" pdf` | Original scans at WA5VJB; readable indexed passages, but screenshot/text extraction insufficient for a new full-paper receipt; LL67 remains selected numerical authority |
| Direct NASA LL67; NASA LL68; ADS ZPC74 | LL67 numerical conventions reopened. LL68 returned size limit 24,410,839 bytes; ADS route failed. Prior passage-level receipts preserved, not fabricated as new successes |
| `"lunar" "5.7 GHz" "spectrum" measurement`; `"lunar radar" "5.76" measurement data`; `"lunar radar" "5.7" raw data` | No matching calibrated whole-disc record identified; G3WDG visual widths retain their limited estimator/status |
| `"moonbounce" "5.7" "recordings"`; `"lunar radar" "5.8 GHz" data` | No verified target-band IQ/calibration package; absence in this search is not proof none exists |
| `"lunar radar" "2025" "C-band" scattering`; `"Mini-RF" "X/C" "2025" calibration` | Fassett 2025/Chan 2025 primary abstracts, including calibration counterevidence |
| `"Mini-RF X/C-Band Bistatic Synthetic Aperture Radar" Peters DOI`; exact title plus `pdf`; exact DOI plus `pdf` | Peters 2026 identified in CU/Stanford author records; DOI tool route unavailable; original article body read through author-uploaded copy |
| `"lunar radar" "2024" "2025" "Green Bank" VLBA paper`; `"Green Bank Telescope" "planetary radar system" paper 2024 2025` | Official ngRadar pages and LPSC 2802; recent imaging evidence, not target-band whole-disc statistics |
| `"First Moon Bistatic SAR Imaging Experiment" FAST DOI`; `"High-Resolution Lunar Imaging Using Stepped-Frequency" DOI` | IEEE 2025 titles/DOIs identified; primary full-text access not established. No parameter extracted from search snippets |
| `"Radar images of the Moon at 6-meter wavelength" pdf` | Vierinen 2017 publisher abstract/method preview; explicit metre/centimetre distinction |
| `site.pds-geosciences.wustl.edu lunar radar 4.2 cm Mini RF bistatic data` | Official PDS/ODE holdings and processing notes; complex local products plus calibration metadata, detailed in 41 |

The original-source route for the 2006 Hagfors critique was the publisher
DOI 10.1029/2005JE002489, discovered while following the original 1964 law.
It is counterevidence to a literal physical interpretation of the fitted law,
not evidence that the empirical delay table should be discarded. Unavailable
sources above are not automatically new research tasks when their proposed
claim has been withdrawn or replaced; the open whole-disc measurement and
polarimetric-calibration dependencies remain substantive limits.

## References

Access status: **read** = supporting passage read in full text or page image; **abstract** = abstract or search snippet only; **secondary** = quoted through the named source; **not obtained**.

- [EV65] Evans, J. V. (1965). Radar studies of the Moon. *J. Res. Natl. Bur. Stand.* 69D (Radio Science) (12), 1637–1659. https://nvlpubs.nist.gov/nistpubs/jres/69D/jresv69Dn12p1637_A1b.pdf — read. [P]
- [EV69] Evans, J. V. (1969). Radar studies of planetary surfaces. *Annu. Rev. Astron. Astrophys.* 7, 201–248. Scan: https://articles.adsabs.harvard.edu/pdf/1969ARA%26A...7..201E — read (pp. 201, 204, 216, 219–227 as page images). [P]
- [LL67] MIT Lincoln Laboratory (1967). *Radar Studies of the Moon*, Final Report Vol. 1, 31 August 1967, contract NSR 22-009-106. NASA NTRS 19670030824. https://ntrs.nasa.gov/api/citations/19670030824/downloads/19670030824.pdf — read. [T]
- [LL68] MIT Lincoln Laboratory (1968). *Radar Studies of the Moon*, Final Report Vol. 2, 18 April 1968. NASA NTRS 19680018170. https://ntrs.nasa.gov/api/citations/19680018170/downloads/19680018170.pdf — read (§II–§VII, Tables IV, IX–XIII). [T]
- [LLQ2] MIT Lincoln Laboratory (1966). *Radar Studies of the Moon*, Quarterly Progress Report 2, 15 May 1966. NASA NTRS 19660020448. https://ntrs.nasa.gov/api/citations/19660020448/downloads/19660020448.pdf — read (cross-section and fading passages, reference list). [T]
- [FR60] Fricker, S. J., Ingalls, R. P., Mason, W. C., Stone, M. L., Swift, D. W. (1960). Computation and measurement of the fading rate of moon-reflected UHF signals. *J. Res. Natl. Bur. Stand.* 64D (5), 455–465. https://nvlpubs.nist.gov/nistpubs/jres/64D/jresv64Dn5p455_A1b.pdf — read. [P]
- [IBD61] Ingalls, R. P., Bird, L. E., Day, J. W. B. (1961). Band-pass measurements of a lunar reflection circuit. *Proc. IRE* 49 (3), 631–632. Issue scan: https://www.worldradiohistory.com/Archive-IRE/60s/IRE-Proceedings-1961-03.pdf — read. [P]
- [TR58] Trexler, J. H. (1958). Lunar radio echoes. *Proc. IRE* 46 (1), 286–292. Issue scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1958-01.pdf — read. [P]
- [KS51] Kerr, F. J., Shain, C. A. (1951). Moon echoes and transmission through the ionosphere. *Proc. IRE* 39 (3), 230–242. Issue scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1951-03.pdf — read (summary, pp. 230, 241–242). [P]
- [DA65] Davis, J. R., Rohlfs, D. C., Skaggs, G. A., Joss, J. W. (1965). Decameter-wave radar studies of the lunar surface. *J. Res. Natl. Bur. Stand.* 69D (12), 1659–1666. https://nvlpubs.nist.gov/nistpubs/jres/69D/jresv69Dn12p1659_A1b.pdf — read. [P]
- [ZPC74] Zisk, S. H., Pettengill, G. H., Catuna, G. W. (1974). High-resolution radar maps of the lunar surface at 3.8-cm wavelength. *The Moon* 10, 17–50. Scan: https://articles.adsabs.harvard.edu/pdf/1974Moon...10...17Z — read (pp. 17, 19–20, 45–46 as page images). [P]
- [SIMPSON78] Simpson, R. A. (1978). *Radar scattering laws and wavelength dependence of the lunar surface.* Stanford Center for Radar Astronomy, NASA CR-155714, NTRS 19780009307. https://ntrs.nasa.gov/api/citations/19780009307/downloads/19780009307.pdf — read (abstract, introduction). [T]
- [K1JT10] Taylor, J. (K1JT) (2010). Frequency-dependent characteristics of the EME path. Paper dated 15 June 2010 for the 14th International EME Conference, Dallas. https://wsjt.sourceforge.io/EME2010_K1JT.pdf — read. [G]
- [K1JT16] Taylor, J. H. (K1JT) (2016). High-accuracy prediction and measurement of lunar echoes. *QEX*, September/October 2016 (author reprint paginated 1–5). https://wsjt.sourceforge.io/LunarEchoes_QEX.pdf — read. [G]
- [HBK10] Taylor, J. (2010). Earth–Moon–Earth communication, §30.9–30.12 in *The ARRL Handbook 2010*, Chapter 30. https://wsjt.sourceforge.io/Hbk_2010_Ch30_EME.pdf — read. [G]
- [G3WDG10] Suckling, C. (G3WDG) (2010). Predicting libration fading on the EME path, 14 June 2010 (cited in [VK7MO] as also in DUBUS 3/2010). https://bobatkins.com/radio/g3wdg_libration.html — read. [G]
- [VK7MO] Moncur, R. (VK7MO), Taylor, J. (K1JT) (undated, c. 2013). Small station EME at 10 and 24 GHz: GPS locking, Doppler correction, and JT4. https://wsjt.sourceforge.io/small_station_eme.pdf — read. [G]
- [G3WDG16] Suckling, C. (G3WDG) (2016). Experiences with CP on 10 GHz. Slides, EME 2016 conference. https://mail.bobatkins.com/radio/data/EME-2016-G3WDG-Presentation.pdf — read. [G]
- [SM5BSZ06] Åsbrink, L. (SM5BSZ) (2006). Depolarization on 10 GHz EME, 5 October 2006. https://sm5bsz.com/linuxdsp/usage/pol10ghz.htm — read. [G]
- [ARECIBO10] Taylor, J., Vazquez, A., Breakall, J. (2010). Moonbounce from Arecibo Observatory (magazine article reprint). https://wsjt.sourceforge.io/Moonbounce_at_Arecibo.pdf — read. [G]
- [WSJTX-ECHO] WSJT development group (c. 2022). Echo mode in WSJT-X 2.6.0. https://wsjt.sourceforge.io/Echo_Mode_in_WSJT-X_2.6.0.pdf — read in part. [G]
- [WETTERSAT] EME Link Budget & Analysis Tool (1–300 GHz), web page dated October 2022. https://www.wettersat.bplaced.net/EME/EME.html — read. [G]
- [PARK21] Park, R. S., Folkner, W. M., Williams, J. G., Boggs, D. H. (2021). The JPL planetary and lunar ephemerides DE440 and DE441. *Astron. J.* 161, 105. https://iopscience.iop.org/article/10.3847/1538-3881/abd414 — read through an automated extract of the full text. [P]
- [HORIZONS] Jet Propulsion Laboratory. Horizons System manual. https://ssd.jpl.nasa.gov/horizons/manual.html — read. [G]
- [MOONFACT] NASA Space Science Data Coordinated Archive. Moon Fact Sheet. https://nssdc.gsfc.nasa.gov/planetary/factsheet/moonfact.html — read. [G]
- [CAMPBELL16] Campbell, B. A. (2016). Planetary geology with imaging radar: insights from Earth-based lunar studies, 2001–2015. *Publ. Astron. Soc. Pacific* 128 (964), 062001. https://iopscience.iop.org/article/10.1088/1538-3873/128/964/062001 — read through an automated extract of the full text. [P]
- [GAO23] Gao, Y. et al. (2023). Investigating the dielectric properties of lunar surface regolith fines using Mini-RF SAR data. *ISPRS Journal of Photogrammetry and Remote Sensing* 197, 56–70. https://doi.org/10.1016/j.isprsjprs.2023.01.008 — abstract and indexed record read; publisher full-text fetch returned 403 during this review. [P]
- [CAMPBELL25] Campbell, B. A. (2025). Regolith properties and Stokes vector radar polarimetry models for the moon. *Icarus* 441, 116738. https://doi.org/10.1016/j.icarus.2025.116738 — publisher preview and abstract read; full text unavailable in this review. [P]
- [FASSETT25] Fassett, C. I. et al. (2025). Mini-RF X/C-band bistatic radar observations of the Moon as a function of incidence and phase angles. 56th Lunar and Planetary Science Conference, abstract 2129. https://www.hou.usra.edu/meetings/lpsc2025/pdf/2129.pdf — abstract read. [G]
- [JPL216C] Sanchez-Net, M. (2019). Analysis of the fading channel in downlinks from the lunar South Pole to the Deep Space Network. *IPN Progress Report* 42-216C. https://ipnpr.jpl.nasa.gov/progress_report/42-216/42-216C.pdf — full report read. [T]
- [KIRK24] Improved orthorectification and empirical reduction of topographic effects in monostatic Mini-RF S-band observations of the Moon. *The Planetary Science Journal* 5(1), 4 (2024). https://doi.org/10.3847/PSJ/ad0a61 — bibliographic record and abstract checked; this is an image-correction, not EME link, source. [P]
- [PATT17] Patterson, G. W., Stickle, A. M., Turner, F. S., and 19 others (2017). Bistatic radar observations of the Moon using Mini-RF on LRO and the Arecibo Observatory. *Icarus* 283, 2–19. Record: https://ntrs.nasa.gov/citations/20170003185 — abstract. [P]
- [CAMPBELL10] Campbell, B. A., Carter, L. M., Campbell, D. B., Nolan, M., Chandler, J., Ghent, R. R., Hawke, B. R., Anderson, R. F., Wells, K. (2010). Earth-based 12.6-cm wavelength radar mapping of the Moon: new views of impact melt distribution and mare physical properties. *Icarus* 208 (2), 565–573. doi:10.1016/j.icarus.2010.03.011. Author manuscript ("Icarus, revised 2010") in the PDS archive: https://pds-geosciences.wustl.edu/Lunar/arcb_nrao-l-rtls_gbt-5-12.6cm-v1/slrm_90xx/document/sband_lunar_radar.pdf — read (manuscript pp. 2, 6–8). [P]
- [GSA18] Stickle, A. M., Patterson, G. W., Cahill, J. T. S. (2018). Mini-RF bistatic observations of Copernican crater ejecta. GSA Annual Meeting 2018, Indianapolis, abstract (paper 321057). https://gsa.confex.com/gsa/2018AM/webprogram/Paper321057.html (redirects to studio.m-anage.com) — read. [G]
- [EPSC22] Patterson, W., Stickle, A., Morgan, G. (2022). Mini-RF bistatic observations of south polar craters on the Moon. Europlanet Science Congress 2022, EPSC2022-1095. https://meetingorganizer.copernicus.org/EPSC2022/EPSC2022-1095.html — read. [G]
- [NGRADAR23] Taylor, P. A. (2023). The next generation planetary radar on the Green Bank Telescope. Slides, AAS 241 meeting, January 2023. https://aas.org/sites/default/files/2023-01/AAS241_Tue1_PatrickTaylor.pdf — read. [G]
- [MARGOT21] Margot, J.-L., Campbell, D. B., Giorgini, J. D., Jao, J. S., Snedeker, L. G., Ghigo, F. D., Bonsall, A. (2021). Spin state and moment of inertia of Venus. *Nature Astronomy*, doi:10.1038/s41550-021-01339-7. Preprint arXiv:2103.01504, https://arxiv.org/abs/2103.01504 — read (preprint, Methods: radar speckle tracking). [P]
- [WSJTX-SRC] WSJT-X source code, master branch, files lib/jt65sim.f90, lib/jt4sim.f90, lib/qra/q65/q65sim.f90 and lib/fst4/fst4sim.f90. https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/ — read 13 November 2026. [G]
- [EPSC18] Patterson, G. W., Prem, P., Stickle, A. M., Cahill, J. T. S., and the Mini-RF team (2018). Mini-RF S- and X-band bistatic observations of south polar craters on the Moon. EPSC Abstracts 12, EPSC2018-729. https://meetingorganizer.copernicus.org/EPSC2018/EPSC2018-729.pdf — read. [G]
- [EPSC19a] Patterson, G. W., Carter, L. M., Stickle, A. M., and others (2019). Mini-RF S- and X-band bistatic observations of the Moon. EPSC Abstracts 13, EPSC-DPS2019-566. https://meetingorganizer.copernicus.org/EPSC-DPS2019/EPSC-DPS2019-566-1.pdf — read. [G]
- [EPSC19b] Stickle, A., Patterson, W., Cahill, J., and the Mini-RF team (2019). Observations of the radar scattering (phase) function of Copernican crater ejecta on the Moon using Mini-RF. EPSC Abstracts 13, EPSC-DPS2019-622. https://meetingorganizer.copernicus.org/EPSC-DPS2019/EPSC-DPS2019-622-1.pdf — read. [G]
- [VIER17] Vierinen, J., Tveito, T., Gustavsson, B., Kesaraju, S., Milla, M. (2017). Radar images of the Moon at 6-meter wavelength. *Icarus* 297, 179–188. doi:10.1016/j.icarus.2017.06.035 — abstract (institutional repository record, https://repositorio.igp.gob.pe/handle/IGP/2273). [P]
- [TUH11] Thompson, T. W., Ustinov, E. A., Heggy, E. (2011). Modeling radar scattering from icy lunar regoliths at 13 cm and 4 cm wavelengths. *J. Geophys. Res.* 116. doi:10.1029/2009JE003368 — abstract (search snippet). [P]
- [HAARP08] Naval Research Laboratory (2008). Scientists detect lowest frequency radar echo from the moon. Press release, 8 January 2008. https://www.eurekalert.org/news-releases/724352 — read. [G]
- Thompson, T. W. (1979). A review of Earth-based radar mapping of the Moon. *The Moon and the Planets* 20, 179–198. Scan: https://articles.adsabs.harvard.edu/pdf/1979M%26P....20..179T — first page read. [P]
- Secondary only (bibliographic details as given in the reference lists of [LL67], [LLQ2], [TR58]): Evans, J. V., Pettengill, G. H. (1963) *J. Geophys. Res.* 68, 423–447; Evans, J. V., Hagfors, T. (1966) *J. Geophys. Res.* 71, 4871–4889; Hagfors, T. (1964) *J. Geophys. Res.* 69, 3779–3784; Hagfors, T. (1967) *Radio Science* 2, 445–465; Hagfors, T. et al. (1965) *Science* 150, 1153–1156; Davis, J. R., Rohlfs, D. C. (1964) *J. Geophys. Res.* 69, 3257–3262; Klemperer, W. K. (1965) *J. Geophys. Res.* 70, 3798–3800; Lynn, V. L., Sohigian, M. D., Crocker, E. A. (1964) *J. Geophys. Res.* 69, 781–783; Browne, I. C., Evans, J. V., Hargreaves, J. K., Murray, W. A. S. (1956) *Proc. Phys. Soc.* B 69, 901–920; Kerr, F. J., Shain, C. A., Higgins, C. S. (1949) *Nature* 163, 310–313; Smith, C. R., Pettengill, G. H., Shapiro, I. I., Weinstein, F. S. (1968) *Science* 160, 876–878. [P]
- Secondary only (bibliographic details as given in the reference lists of [EV65] and [MARGOT21]): Evans, J. V. (1962b) Radio echo observations of the moon at 3.6 cm wavelength, MIT Lincoln Laboratory Tech. Rep. 256; Green, P. E. (1962) Radar astronomy measurement techniques, MIT Lincoln Laboratory Tech. Rep. 282, and (1968) chapter "Radar measurements" in *Radar Astronomy*, McGraw-Hill; Kholin (Holin), I. V. (1988) *Radiophys. Quantum Electron.* 31, 371–374; Kholin (Holin), I. V. (1992) *Radiophys. Quantum Electron.* 35, 284–287. [T]/[B]/[P]
- Not obtained: Hagfors, T. (1970) *Radio Science* 5, 189–227, doi:10.1029/RS005i002p00189 (existence and pagination confirmed by index records; content used only through [K1JT10] and [CAMPBELL10]; no open copy was found); Evans, J. V., Hagfors, T. (eds.) (1968) *Radar Astronomy*, McGraw-Hill. [P]/[B]

- [PETERS26] Peters, S. et al. (2026). *Mini-RF X/C-Band Bistatic Synthetic Aperture Radar: Architecture, Polarimetric Observations, and Implications for Future Systems*. IEEE TGRS 64,5200913. https://doi.org/10.1109/TGRS.2026.3652796 — original full article text, especially §II.B/§III, inspected via [author-uploaded copy](https://www.researchgate.net/publication/399711673_Mini-RF_XC-Band_Bistatic_Synthetic_Aperture_Radar_Architecture_Polarimetric_Observations_and_Implications_for_Future_Systems); identity corroborated by [CU author record](https://experts.colorado.edu/display/fisid_174034). Publisher DOI retrieval failed; this does not imply the journal article is unavailable generally. [P]
- [CHAN25] Chan, K. et al. (2025). *Enhancements to Mini-RF X/C-band data quality through cross-channel calibration and reprocessing strategies*. LPSC 2294. https://www.hou.usra.edu/meetings/lpsc2025/pdf/2294.pdf — full two-page abstract read; conference evidence, not a journal article. [G]
- [ST06] Sultan-Salem, A. K., Tyler, G. L. (2006). *Hagfors’ law revisited*. JGR Planets 111,E06012. https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2005JE002489 — supporting original publisher abstract/§§2–3/conclusions inspected. [P]
- [GBT25] Taylor, P. A. et al. (2025). *The Next Generation Planetary Radar on the Green Bank Telescope*, LPSC 2802. https://www.hou.usra.edu/meetings/lpsc2025/pdf/2802.pdf ; [official ngRadar results](https://ngradar.nrao.edu/page/results) — instrument/imaging evidence; not a5.76 GHz channel record. [G]

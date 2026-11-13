# "Groundwave" near 30 MHz

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature, public-data inspection and reference calculations. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026; source attributions and access status for the selected recent groundwave studies were checked against primary full texts on this revision. Calculated budgets and unmeasured competing-mode levels remain estimates.
- **Related chapters.** groundwave near 1 MHz [20](20-groundwave-1mhz.md); illustrative groundwave scenario specifications [82](82-scenarios-groundwave.md); skywave level [30](30-skywave-signal-level-and-outages.md); noise [50](50-noise-and-interference-mf-hf.md); independent arithmetic [90](90-cross-check-calculations.md).

**Summary.** At 30 MHz a low, vertically polarized wanted path over sea can be surface-wave dominated; over ordinary land, the ground-admittance contribution is small and a space-wave/diffraction description is more useful. The chapter's smooth-earth reference calculation (cross-checked against P.368-9 curves, Barrick's 1970 curves, and independent recomputation, [90](90-cross-check-calculations.md)) gives, for ground-level vertical reference antennas, a basic transmission loss of 132 dB at 100 km and 186 dB at 300 km over sea, but 131 dB at only 10 km and 182 dB at 100 km over average land. These are reference-physics outputs, not proof that the received signal on a real route is that mode. The strongest new one-way observation reviewed here is Kirincich and Emery's 16.188 MHz, 8-month coastal record: at 7 km an antenna about 0.7λ above sea level was within transmitter line of sight and may have measured a non-coupled space wave; the 45 km receiver near sea level and below the transmitter horizon was ground-wave dominated. Their two receivers had different controlling variations, and complex EM models did not consistently outperform simpler environmental models. This argues for explicit height/horizon classification and against treating sea-state calculations as universal. A separate Dagefu et al. paper measured near-ground 40 MHz links only to 200 m in cluttered indoor/outdoor geometries; it is not a 30 MHz, long-range groundwave validation. At very large wanted losses, meteor, aircraft, ionospheric scatter and co-channel skywave may compete, but most levels here are calculated bounds rather than matched observations. The HF presets still describe neither the nominal wanted propagation nor a validated joint outage state. Under [00](00-method-and-modem-context.md) §3.1, the GUI's 180 dB at −164 dBm/Hz corresponds to about 175 dB basic transmission loss when the assumed external-noise factor is approximately 18 dB and transmitter/receiver reference assumptions match; 10 dB is the GUI noise-equivalent factor, not the assumed 30 MHz galactic external-noise factor.

*Source limitation: the evidence below rests mainly on documents obtained directly from primary locations (itu.int, its.ntia.gov, nvlpubs.nist.gov, the archive.org DTIC mirror, worldradiohistory.com scans) and primary journal full texts where available. Remaining abstract-only and inaccessible sources are marked in the tables/reference list and listed as gaps; their abstracts are not treated as full measurements.*

Conventions used throughout:

- Grades: [S] standard, [P] research paper or preprint (peer-review status checked separately), [T] technical report, [B] book, [G] grey, [E] this chapter's own estimate. Access status ("read", "abstract", "secondary") is given in the reference list and repeated where it matters.
- **The compared basic-loss conventions differ by about 6 dB in the ideal low-terminal, perfectly conducting-plane example; the difference is geometry-dependent more generally.** ITU-R P.368 defines Lb = 142.0 + 20 log f(MHz) − E(dBµV/m) (P.368-10 Annex 1 Note 1 [R1]); with it a perfectly conducting plane gives Lb equal to the free-space loss. Barrick, and the plane-earth/ITM/Egli literature, count the field doubling as propagation, so that the same plane gives free space minus 6 dB (Barrick 1970, text introducing Figs 7–8, about pp. 49–51 [R18]: losses over a perfect plane are "essentially 6 dB lower than the loss through free space"). NBS TN101, on which ITM rests, defines basic transmission loss with isotropic antennas "at the same locations" as the real ones, so the ground reflection is part of the propagation ([R28] §2). **Sea and smooth-earth values below are in the P.368 convention; ITM, Longley–Reasoner, Jansky & Bailey and Egli values, and this chapter's estimates for scatter modes, are in the free-space-antenna convention.** A −6.02 dB adjustment applies only to the corresponding low-terminal, same-field normalization illustrated by the perfectly conducting plane. It is not a universal conversion for arbitrary ITM terrain, antenna height, polarization or scatter paths; compare the actual source field and terminal reference instead. Per P.341-7 Annex 3 [R4], two loss-free short monopoles on a perfect plane have a terminal-to-terminal loss of Lbf − 3.5 dB. For the scatter modes of table E the comparison depends on the antenna's elevation pattern: for ideal ground-mounted verticals with **both** rays at 45°, the product of the two `3cos²θ` gains gives approximately +3.52 dB, matching the ideal grounded-short-monopole net factor. This conditional comparison is valid only if local ground enhancement is absent from the incident-field prediction and has not already been counted. Table E therefore remains a set of geometry/convention-dependent estimates, not a calibrated mode ranking [E].

## Physical mechanisms

### 1. What the word covers

The ITU handbook splits the ground wave into a space wave (direct plus ground-reflected ray) and a surface wave; when both terminals are close to the ground the reflection coefficient is −1, the two rays cancel and only the surface wave is left ([R3] §3.1, p. 3, [S] read). The attenuation of that surface wave relative to the inverse-distance field is the Sommerfeld–Norton function F = 1 − j√(πw) e^(−w) erfc(j√w), with numerical distance w = −jkr u²(1 − u²)/2 and u² = 1/(εr − j·18000σ/f_MHz) ([R3] eqs 5–8, 11; equivalently eqs 17–19 with surface impedance η). Around a spherical Earth the field is the van der Pol–Bremmer residue series, and the same series is what ITU-R P.526 calls smooth-earth diffraction: "LFMF-SmoothEarth" in P.368 is named there as the complete method, and P.526 §3.1 keeps only the first term ([R5] §3, p. 6, [S] read). So surface wave and smooth-earth diffraction are one calculation; the label depends on whether the ground constants matter.

They stop mattering over land above about 20 MHz. P.526-16 §3.1.1.2 states that for vertical polarisation above 20 MHz over land, or 300 MHz over sea, the ground-admittance factor β may be set to 1 ([R5] p. 9). Bullington's 1947 abstract makes the same point for frequencies above about 30 Mc ([R31], abstract fragment only). Consequently:

- **Sea, 30 MHz:** a true surface wave. |Δ| = 0.018, numerical distance |w| = 0.10 at 1 km and 1.0 at 9.5 km; loss within 0.5 dB of free space at 1 km and 4 dB above it at 10 km [E].
- **Land, 30 MHz:** |Δ| = 0.15–0.47, |w| = 7–70 at 1 km. The field is already in the 1/d² regime (|F| ≈ 1/(2|w|)) a few hundred metres from the antenna, 23–43 dB below the inverse-distance value at 1 km [E]. This, plus the curvature loss that starts near 25 km, is why land paths are short.

### 2. Surface impedance at 30 MHz

Own values [E] from Δ = √(εc − 1)/εc, εc = εr − jσ/(ωε0), with the ground constants of the P.368-9 curve set ([R2] Figs 1–11; listed in [R3] Table 2):

| Ground (σ S/m, εr) | \|Δ\| | arg Δ | \|w\| at 1 km | Minimum effective height h0 = λ/(2π\|Δ\|) |
|---|---|---|---|---|
| Sea, average salinity (5, 70) | 0.0183 | 44° | 0.10 | 87 m |
| Sea, low salinity (1, 80) | 0.0407 | 41° | 0.52 | 39 m |
| Land (3×10⁻², 40) | 0.149 | 12° | 7.0 | 10.6 m |
| Wet ground (1×10⁻², 30) | 0.178 | 5.5° | 9.9 | 8.9 m |
| Land (3×10⁻³, 22) | 0.208 | 2.2° | 13.6 | 7.6 m |
| Medium dry (1×10⁻³, 15) | 0.249 | 1.1° | 19.5 | 6.4 m |
| Dry (3×10⁻⁴, 7) | 0.350 | 0.6° | 38.5 | 4.5 m |
| Very dry (1×10⁻⁴, 3) | 0.471 | 0.3° | 69.8 | 3.4 m |

Over land at 30 MHz the impedance is almost purely resistive and set by εr, not σ; Adams et al. found exactly this at 28 MHz, where permittivity dominated their predictions while conductivity dominated at 5–11 MHz ([R24] §7, p. 59, [T] read). Note that [R3] Table 2 lists εr = 80 for average sea water whereas the P.368-9 and P.368-10 Figure 2 captions say εr = 70; the difference is immaterial at 30 MHz. The labels follow [R3] Table 2: 3×10⁻² S/m with εr 40 is "Land" (Fig. 4) and 10⁻² S/m with εr 30 is "Wet ground" (Fig. 5).

### 3. Distance regimes

- Flat-earth zone: the chapter's flat and spherical results differ by 0.1–0.3 dB at 10 km, 0.6 dB (sea) to 1.7 dB (land) at 30 km and 5.5–11 dB at 100 km [E; confirmed by independent re-computation]. The handbook gives d = 80/f_MHz^(1/3) km, i.e. about 26 km at 30 MHz ([R3] §3.2.2, eq. 15, read from the page image); the NTIA LFMF code uses the same 80/f^(1/3) km test distance [R48].
- Diffraction zone: one unit of P.526's normalised distance X is 61 km at 30 MHz with k = 4/3; the first-mode slope is 17.6 dB per unit X, about 0.29 dB/km ([R5] eq. 17a). The full residue series gives 0.30–0.31 dB/km over land and 0.26–0.28 dB/km over sea between 100 and 300 km [E].

### 4. Antenna height

For low antennas the height-gain factor is |1 + jkhΔ| per terminal, so nothing is gained below h0 = λ/(2π|Δ|) (table above). Bullington's engineering form is to use the larger of the actual height and the "minimum effective antenna height", accurate to ±3 dB, with the plane-earth formula valid once both antennas are more than a wavelength above ground, or more than 5–10 wavelengths above sea water ([R30] pp. 596–597, Fig. 3, [P] read). P.368's own limit, h = 1.2 σ^(1/2) λ^(3/2) when εr ≪ 60λσ ([R1] recommends 3, Note 1), evaluates to 85 m at 30 MHz over sea, which is the same quantity [E].

Two consequences at 30 MHz, from the chapter's residue-series runs, confirmed by an independent re-computation [E]:

- **Over land, height is the dominant design variable.** Raising both antennas from the ground to 10 m gains 8.3 dB (σ = 3 mS/m) and to 30 m gains 24 dB; above h0 the loss follows the plane-earth law 40 log d − 20 log(h1h2).
- **Over sea, modest height loses signal.** Because arg Δ ≈ 44°, |1 + jkhΔ| first falls: both antennas at 30 m lose 3.7 dB and at 100 m lose 4–6 dB, while a single antenna at 300 m gains only 3–7 dB (30–100 km). The flat-earth factor recovers to unity at about 120 m; the spherical-earth series puts the break-even between about 150 and 300 m. These homogeneous-sea height factors do not predict a station on a real cliff: the intervening land, edge diffraction, local antenna pattern and atmospheric profile change the problem ([R3] §9 cites Furutsu and Pielou et al.; originals not read). Heights above 50 m also lie outside the selected public LFMF API, though the historical residue-series calculation can explore them as theory.

### 5. Polarisation

Horizontally polarised surface waves "are very heavily attenuated and have little or no practical worth" ([R3] §1, p. 1). The chapter's numbers [E] at 30 km: over sea with 3 m antennas, vertical 104 dB and horizontal 168 dB; over land with 3 m antennas, 150 and 168 dB; with 10 m antennas over land 143 and 147 dB; with 30 m antennas the two are equal within 0.5 dB. So the vertical-polarisation requirement is absolute over sea and for antennas below about one wavelength over land, and disappears for elevated antennas over land. Inside forest the preference reverses (see "Forest").

### 6. Atmosphere

P.368-9 curves were computed with GRWAVE for an exponential atmosphere (N0 = 315, scale height 7.35 km); the equivalent effective-earth-radius factor rises from 1 below 10 kHz through 1.20–1.25 at MF to the VHF value of 4/3 ([R3] §3.3, p. 7; Rotheram's abstract confirms the effective-radius interpretation [R35]). **P.368-10 (2022) replaced the curves by the LFMF software and states a linear decrease of refractive index with height** ([R1] Annex 1); at 30 MHz the two assumptions should agree closely, and the chapter's k = 4/3 results match the GRWAVE curves to within the 1–2 dB reading accuracy.

Sensitivity to k (sea, 30 MHz, own calculation [E]): at 100 km, 134.1/132.4/130.6/129.4 dB for k = 1/1.33/2/3; at 300 km, 195.5/185.5/174.2/165.8 dB. The refractivity gradient is therefore a second-order effect inside 100 km and a first-order one (±10 dB for k between 1 and 2) at 300 km.

Deep surface-based ducts go further. Hansen measured 4–32 MHz on a 235 km Point Loma–Point Mugu sea path for a month plus one hourly 24-hour run; below 20 MHz the traces were compatible with sea-state variation, but the wind was inferred from those same received levels through Barrick's model and the subsequent correction is partly circular. At 26 MHz the trace varied oppositely to the sub-20 MHz pattern, and 32 MHz reached levels at times 25 dB above smooth-earth theory, attributed to ducting ([R15] full text, [P]). The tall transmitting monopole sat 97 ft above sea level beside a bluff while the receiving monopole was about 10 ft above the beach; receiver calibration was ±0.75 dB, but the bluff-gain uncertainty made total loss uncertainty unknown. Thus this is a notable upper-HF anomalous received-level event, not an independently calibrated, ground-level surface-wave loss measurement. Pappert & Goodhart's separate waveguide-mode calculations using radiosonde profiles concluded that super-refractive layers explain the above-20 MHz enhancement ([R16] abstract, [P]). For that path the k = 4/3 value at 32 MHz is 172.5 dB; a 25 dB enhancement corresponds to k of 10 or more (24 dB at k = 10), i.e. near-trapping [E; re-computed]. Thin evaporation ducts cannot trap a 10 m wave; deep subsidence-inversion ducts can. No comparable measurement for other climates was found.

### 7. Rough sea

Barrick replaced the smooth-sea impedance by an effective impedance that adds a roughness term obtained from the ocean wave-height spectrum (Rayleigh–Rice perturbation with a Leontovich boundary) and fed it to a standard spherical-earth program ([R17] abstracts, [P]; [R18] [T] read). The journal abstract says the excess loss is negligible below about 2 MHz and maximal at about 10–15 MHz; the report shows that at 30 MHz it is large even at short range, because the roughness term then dominates the impedance. The theory's assumptions are small height (k0ζ)² ≪ 1 and small slopes; Barrick states that the Neumann–Pierson impedance curves are usable to about 30 MHz at a wind of 23 knots and the Phillips curves to about 40 MHz at 25 knots, that (k0ζ)² is about 0.4 at 30 MHz in sea state 6 (wave heights ±4 ft; the OCR of this figure is poor), that calmer seas extend validity to 100 MHz for sea state 3, and that above about 50 MHz with winds over 25 knots the technique fails ([R18], discussion of assumptions and of the impedance curves, and Conclusions pp. 89–90; earlier page numbers are not legible in the scan text). The ITU handbook is blunter: the assumptions hold for HF over the sea and "are violated for VHF radio waves over moderately disturbed seas" ([R3] §8, p. 18). Swell is not included ([R18] p. 90).

A slightly rough sea can also give slightly more field than a smooth one at ranges up to about 100 km ([R3] §8, Fig. 8 at 10 MHz, sea state 2), and an inductive surface impedance in general can support a trapped surface wave that interferes with the normal ground wave ([R36] abstract, 1–30 MHz, [P]).

### 8. Sea motion and Doppler

The sea imposes Doppler only on energy it scatters. First-order backscatter is two discrete lines at ±f_B, with f_B = √(g/(πλ)) = 0.102√f_MHz Hz: 0.56 Hz at 30 MHz, 0.46 Hz at 20 MHz, 0.72 Hz at 50 MHz ([R19] abstract; Crombie [R20]; arithmetic [E]). For a bistatic angle φ the resonant ocean wavenumber is 2k0 cos(φ/2) and the shift is f_B√cos(φ/2), tending to zero for forward scatter. For a stationary ensemble the coherent mean field has no mean Doppler shift; its effective impedance changes complex propagation. That does not assert every realized forward channel has zero phase fluctuation. A diffuse sea-scattered component reaches the receiver from off-path patches. Using Barrick's upper-limit σ⁰ = −17 dB near grazing [R19] and integrating a patch of area about D × D/2 at the midpoint of a 50 km path, this component is estimated at roughly 25–35 dB below the direct wave, spread over 0 to ±0.56 Hz and concentrated near zero [E, order of magnitude only: P_scat/P_dir ≈ σ⁰·(4/π)·(A/D²)·|W(D/2)|⁴/|W(D)|² ≈ −17 + 1 − 3 − 2 dB with A/D² = 0.5 and the smooth-sea attenuation factors at 25 and 50 km (re-computed), reduced by 5–15 dB for partial sea development and antenna-pattern weighting]. The 25–35 dB value is a heuristic sensitivity choice, not a derived or measured forward-path K: importing a backscatter cross-section, a midpoint patch and an assumed 5–15 dB weighting does not establish a bistatic integral or a rigorous bound. Mean-impedance theory cannot supply the missing incoherent power/covariance. No forward-path measurement was found. Bistatic HF radar practice establishes only a geometry-specific strong direct component, not a statistical K: on a 12.19 MHz SeaSonde pair the direct signal is so strong that its range-processing sidelobes mask the Bragg echo from cells near the transmitter–receiver baseline ([R53] §3.2, [P] read).

### 9. Terrain, forest, buildings

- **Irregular terrain.** Smooth-earth theory is adequate where height irregularities are small relative to the wavelength ([R3] §4, p. 9). At 28 MHz (λ = 10.7 m) ordinary relief violates that; Adams et al. found integral-equation terrain predictions differing from smooth-earth ones by up to 15 dB at 28 MHz on moderately irregular paths ([R24] §§8.3, 9). Obstacles matter most when close to the receiver ([R3] §14, MF data). Terrain can also help: in the ITM a small roughness (Δh from 0 to 25 m) reduces attenuation by up to 10 dB, "a phenomenon that might be likened to obstacle gain" ([R25], discussion of the terrain parameter Δh; the example is at 150 MHz, not low VHF).
- **Forest.** Tamir modelled the forest as a lossy slab; between two antennas inside it the dominant field is a lateral wave that climbs to the canopy, skims the tree tops and descends ([R37] abstract, 1–100 MHz, [P]). Dence & Tamir split the loss into an interface term, a term for vegetation above each antenna, a ground-interference term and an antenna-resistance term, and predicted for low antennas that vertical polarisation is preferable and that raising the frequency helps ([R38] abstract, 2–200 MHz, [P]). Hill tabulates their slab parameters: "thin" forest 5 m high, εr 1.03, σ 3×10⁻⁵ S/m; "average" 10 m, 1.1, 10⁻⁴; "dense" 20 m, 1.3, 3×10⁻⁴; the slab model is considered valid below about 200 MHz ([R23] Table 1 and §5.1). Sachs & Wyatt fitted Thailand jungle data to within a 6 dB standard deviation with σ = 0.15 mS/m ([R39] abstract, [P]).
- **Buildings.** Hill represents built-up areas as a slab with εr = 10B/ln(1 + 10B) for fractional coverage B (1.82 for B = 0.2) ([R23] §5.2). The formula is derived from Causebrook's low-density theory, which had been fitted to MF measurements near London; the report checks it against no HF measurement.

### 10. What else arrives

For frequencies above 30 MHz the only permanent beyond-horizon mechanisms are diffraction and tropospheric scatter, with ducting and layer reflection occasional ([R9] Annex 1 §1, [S] read). The ionosphere adds, at 30–60 MHz: regular F2 reflection near solar maximum, sporadic-E reflection and scatter, meteor trails, auroral field-aligned irregularities, trans-equatorial modes, and continuous D/E-region scatter ([R13] Table 1; [R14] §8.1). ITM is limited to above 20 MHz precisely because of ionospheric propagation, and its authors warn of occasional ionospheric reflection near that limit and of sporadic-E scatter below about 100 MHz, adding that such effects appear only on very long paths and for very small fractions of time ([R25] §3, p. 12). The newest ITU wide-range model, P.2001-6 (30 MHz–50 GHz, 3 km to at least 1,000 km), includes a sporadic-E sub-model alongside diffraction, ducting and troposcatter ([R8] scope and §4.4).

## Measured and reported values

### A. Smooth homogeneous Earth at 30 MHz (reference values)

Own calculation [E]: Sommerfeld–Norton flat-earth function below about 10 km, residue series (up to 1,500 modes, roots tracked numerically) beyond; k = 4/3; antennas on the ground; P.368 reference radiator (1 kW, short vertical monopole, 300 mV/m at 1 km). Checks: (i) flat and spherical branches agree to 0.1–0.3 dB at 10 km; (ii) P.368-9 Fig. 2 (sea) read at 97.5, 290 and 310 km gives about 40, −10 and −15.5 dBµV/m against the chapter's 40.0, −11.1 and −17.0; Fig. 6 (land) read at 1, 10, 100 km gives about 80, 40, −10 against 80.8, 40.5, −10.0 [R2]; (iii) Barrick's Fig. 8 (σ = 4 S/m, k = 4/3) agrees with the chapter's σ = 4 run to about 2 dB after the 6 dB convention shift [R18] (re-read: about 176 dB near 270 km at 30 MHz, against 181.5 dB in the P.368 convention); (iv) an independently written residue-series code reproduces every row of both tables to 0.1 dB and the distances to 1 km ([90](90-cross-check-calculations.md) §1 for the P.368 ground classes; the sea, low-salinity and fresh-water rows were re-computed with the same code). Treat the values as P.368-equivalent to ±1.5 dB.

Field strength E, dB(µV/m), 1 kW reference:

| Ground | 1 km | 3 km | 10 km | 30 km | 100 km | 300 km |
|---|---|---|---|---|---|---|
| Sea (5 S/m, 70) | 109.1 | 98.7 | 85.4 | 68.4 | 39.2 | −14.0 |
| Land (3×10⁻², 40) | 86.9 | 67.7 | 46.4 | 25.9 | −4.0 | −65.0 |
| Land (3×10⁻³, 22) | 80.8 | 61.8 | 40.5 | 20.0 | −10.0 | −71.3 |
| Very dry (1×10⁻⁴, 3) | 66.6 | 47.6 | 26.3 | 5.7 | −24.4 | −86.3 |

Basic transmission loss Lb = 171.5 − E, dB (P.368 convention), and the distance at which Lb reaches 150, 180 and 200 dB:

| Ground | 1 km | 3 km | 10 km | 30 km | 100 km | 300 km | d(150) | d(180) | d(200) |
|---|---|---|---|---|---|---|---|---|---|
| Free space | 62.0 | 71.5 | 82.0 | 91.5 | 102.0 | 111.5 | — | — | — |
| Sea (5, 70) | 62.4 | 72.8 | 86.1 | 103.1 | 132.4 | 185.5 | 163 km | 278 km | 357 km |
| Sea (4, 80) | 62.6 | 73.2 | 87.1 | 105.5 | 135.5 | 189.7 | 150 | 263 | 340 |
| Sea, low salinity (1, 80) | 64.6 | 78.1 | 99.1 | 121.6 | 151.2 | 209.6 | 96 | 196 | 266 |
| Fresh water (3×10⁻³, 80) | 80.7 | 99.0 | 120.1 | 140.5 | 170.2 | 230.6 | 47 | 131 | 196 |
| Land (3×10⁻², 40) | 84.6 | 103.8 | 125.2 | 145.7 | 175.6 | 236.5 | 37 | 113 | 178 |
| Wet ground (1×10⁻², 30) | 88.0 | 107.0 | 128.3 | 148.8 | 178.7 | 239.8 | 32 | 104 | 167 |
| Land (3×10⁻³, 22) | 90.7 | 109.8 | 131.0 | 151.5 | 181.5 | 242.8 | 28 | 96 | 158 |
| Medium dry (1×10⁻³, 15) | 93.9 | 112.9 | 134.2 | 154.7 | 184.7 | 246.2 | 24 | 86 | 147 |
| Dry (3×10⁻⁴, 7) | 99.7 | 118.8 | 140.1 | 160.6 | 190.7 | 252.4 | 17 | 70 | 128 |
| Very dry (1×10⁻⁴, 3) | 104.9 | 124.0 | 145.2 | 165.8 | 196.0 | 257.8 | 13 | 57 | 112 |

Frequency dependence (same method; P.368 itself stops at 30 MHz, so 50 MHz is an extrapolation of the theory, not of the Recommendation) — distance at which Lb = 180 dB: sea 405 km (20 MHz), 296 km (28 MHz), 278 km (30 MHz), 183 km (50 MHz); land 3 mS/m 126, 100, 96, 66 km; very dry ground 79, 61, 57, 38 km [E].

Coastal siting (Millington mixed-path method of P.368 Annex 2 [R1] applied to these curves, [E]): a strip of land between one ground-level antenna and the shoreline adds, at 30 MHz, about 4/6/9/14/19 dB for strip widths of 50 m/100 m/300 m/1 km/3 km over 3 mS/m ground, and 3/4/7/11/16 dB over 3×10⁻² S/m land (sea leg 30–100 km; one end only; re-computed independently, uncertain by about 1.5 dB).

### B. Sea paths

| Item | Value | Conditions | Source, grade |
|---|---|---|---|
| Agreement with theory | "In general, the data compare well with theory"; 36 measurements contribute to month-average Figure 2 | 4–32 MHz, Point Loma–Point Mugu, 235 km entirely over ocean, May 29–June 26 1974 twice daily; 24-hour run June 27–28 hourly. Both monopole structures were about 40 ft tall and had identical radial ground screens. TX site elevation about 97 ft above sea level at a bluff; RX site about 10 ft above sea level at the beach. | Hansen 1977 full text [R15] pp. 397–400; publisher abstract corroborates schedule and conclusion [P] |
| Measurement quality and absolute-loss uncertainty | accepted soundings only when (signal + noise)/noise > 20 dB; receiver measurements calibrated to better than ±0.75 dB | received-power calibration; transmitter/receiver antenna gain was measured/modelled; bluff gain was estimated using a terrain integral-equation program | [R15] pp. 398–400. The author explicitly says the bluff-gain uncertainty is unknown, so overall loss uncertainty cannot be assigned; ±0.75 dB is not total link-loss accuracy. |
| Variations below 20 MHz | consistent with sea-state change, "somewhat correlated" with wind; 10 and 18 MHz traces broadly follow inferred sea-state variation | sea-state wind was inferred from the same 10–18 MHz received-signal variation using Barrick curves and then used to correct the measured levels; measured station winds had mixed correlation and large RMS discrepancies (shore-average correlation 0.64; island average −0.42; several individual stations near zero/negative) | [R15] pp. 400–403. Agreement is conditional and partly circular, not an independent validation of loss-vs-wind; short observation duration and spatially variable winds limit attribution. |
| Enhancement above 20 MHz | month mean at frequencies above 20 MHz exceeded calculated values; during the 24-hour test the >20 MHz level had a different time trend than lower HF. At 32 MHz the signal was at times 25 dB above smooth-earth theory, attributed by the author to ducting | same single path and 24-hour run; CW monitoring reported no ionospheric skywave during that 24-hour test; the study suggests a coastal inversion/refraction path rather than a rough-sea-only explanation | [R15] pp. 400–404; Pappert & Goodhart's distinct numerical model with radiosonde profiles attributes similar above-20 MHz enhancement to super-refraction/ducting [R16] abstract only. Do not infer duct frequency or climate-wide occurrence from this one episode. |
| Sea-state loss, 7 and 14 MHz | wind-speed dependence clear, "particularly at 14 MHz"; attenuation rates agree with Barrick | monostatic radar, relative attenuation with range, upwind/downwind | Forget et al. 1982 abstract [R21] [P] |
| Sea-state swing at 15 MHz | up to 15 dB at 100 miles | Barrick's computed curves, Atlantic, σ = 4 S/m | [R18] p. 90 [T] read |
| Added loss at 30 MHz, Phillips isotropic spectrum | at 100 km: about 1, 4.5, 6, 8, 9, 11 dB for sea states 1–6 (5–30 kt); at 10 km: 0, 1.5, 3, 6, 8, 10 dB; at 200 km: 2, 5.5, 7.5, 9.5, 11.5, 13 dB; at 1,000 km: 5 to 26 dB | theory; antennas just above surface; figure read to ±1 dB | [R18] Fig. 31 [T] read |
| Same, Neumann–Pierson spectrum, upwind/downwind | sea state 3: about 4–7 dB at 10–300 km; sea states 5–6: 11–15 dB at 10–100 km; at 1,000 km about 26 dB (sea state 5) and beyond the figure's 28 dB scale (sea state 6) | theory; figure read to ±1.5 dB | [R18] Fig. 15 [T] read |
| Long-term one-way record | 16.188 MHz source; 8-month record. At the 7 km tower (receiver ground plane ~0.7λ above sea, direct visibility) variation tracked humidity/air-sea temperature and may include non-coupled space wave; at the 45 km sea-level, below-horizon site, wave/wind conditions dominated. Seasonal conductivity swing 4.5→2.5 S/m had no detectable seasonal received-power effect. At the latter site, a simple wind-based rough-impedance model performed as well or better than models using measured wave spectra or more complex EM propagation. | Martha's Vineyard coastal path, fixed shore transmitter, one-way tone; 16.188 MHz; relative, low-pass-filtered variation over months, not absolute loss validation at 30 MHz | Kirincich & Emery 2023 §§3–5 [R22] [P, full text read] |
| Near-ground clutter channel | 40 MHz measured channel transfer for indoor-indoor, outdoor-indoor, and outdoor NLoS paths up to 200 m; reported near-scalar attenuation/delay with little/no multipath under those sampled geometries | Robotic receiver; small antenna and calibrated comparison system; short-range clutter, not over-horizon surface-wave path | Dagefu et al. 2015, full text [R41] [P] |
| HF surface-wave radar practice | operating band 3–50 MHz; long-range mode 100–200 km; highest-frequency mode 15–20 km range | two-way sea echo, not a one-way link | Wikipedia [R46] [G], read (band cited there to a 1986 Coastal Engineering Research Center technical note, ranges to a 2010 SeaSonde specification sheet) |

Notes. (1) The two wave-spectrum models differ by up to about 5 dB at 30 MHz for the same wind, neither being consistently larger (Neumann–Pierson higher for sea states 5–6 at short range, Phillips higher for low sea states at long range), and Barrick himself attributes any error to the spectra rather than the electromagnetics ([R18] p. 90). (2) Smooth-water calculations at 30 MHz give a 3–4 dB difference between 4 and 5 S/m at 100–300 km [E]. Kirincich and Emery's contrary observation is specifically at 16.188 MHz on 7/45 km coastal paths with different receiving heights; conductivity variations had no detectable seasonal power effect there. Do not treat that result as a 30 MHz refutation or use the smooth-model difference as a measured seasonal change. It instead motivates a matched 30 MHz test that separates ground-wave and line-of-sight geometries. (3) The ITU handbook mentions slow fading from "changing tidal effects" on sea paths ([R3] §1) without numbers; the coastal-strip figures above imply that a tide exposing 100–300 m of beach can change the level by several dB [E].

### C. Land paths

**Measured, 28 MHz, Boulder, Colorado** ([R24] Table 2, [T] read). Fixed vertical monopole with partial ground screen (about 6 dBi at 28 MHz), mobile van receiver, daytime, CW; "path loss" is received level with transmitter power and both measured antenna gain patterns removed, median in 0.16 km bins. Median loss in dB:

| Path | 5 km | 10 km | 15 km | 20 km | 25 km | 30 km | 35 km | 40 km |
|---|---|---|---|---|---|---|---|---|
| North (flattest) | 100 | 118 | 120 | 127 | | | | |
| East (plains) | 100 | 110 | 124 | 131 | 138 | 140 | 148 | 145 |
| South (rolling) | 103 | 110 | 133 | 138 | 140 | | | |
| Mountain | 120 | 138 | 139 | 140 | 142 | | | |

Upper-to-lower decile spread within a bin: 1–13 dB on the East path, 3–13 dB North, 11–18 dB Mountain ([R24] Table 3). Path-to-path difference of medians about 20 dB regardless of distance. Multipath ripple up to 20 dB over less than 50 m, with a near-sinusoidal pattern indicating one dominant and one weaker component rather than Rayleigh fading (shown at 7.5 and 11.5 MHz). Snow 0.15–0.3 m deep had no appreciable effect at 28 MHz. Ground constants that bracketed the 28 MHz data: σ = 0.01–0.03 S/m, εr = 10–30; the worked wave-tilt example gave εr = 13.7, σ = 3.2 mS/m, and individual 28 MHz wave-tilt values ranged over about εr 13–36 and σ 2.5–25 mS/m ([R24] Table 5, App. B). Skywave was assumed negligible within 20 km and was not measured. The medians of the three lowland paths at 10 km (110–118 dB) are 10–20 dB lower than zero-height P.368-convention values for the bracketing ground constants (about 128–130 dB at 28 MHz; compare table A). The report's own figures largely explain this [E, curves read by eye to ±2 dB]. Its smooth-earth curves lie 6–10 dB below P.368-convention values for the same ground constants: about 122 against 130 dB at 10 km and 133 against 143 dB at 20 km at 28 MHz for σ = 0.03 S/m, εr = 10 (Fig. 50), and about 95 against 101 dB at 10 km at 5.25 MHz for σ = 5 mS/m, εr = 10 (Fig. 47). That offset matches the 6 dB convention difference plus a few dB of model detail, so the report's "path loss" behaves like the free-space-antenna convention. The rest is terrain: on the East path the 10 km median is about 12 dB below the report's own smooth-earth curve, and the integral-equation prediction reproduces the measured peaks and valleys ([R24] §8.3). A roughly 6 dB adjustment describes the low-height reference comparison only; a full comparison must use the actual antenna heights, fields and loss definitions, with terrain retained. The transmitter-site elevation above the path terrain, which P.368 would count as effective antenna height, was not checked.

**Measured, 20 and 50 MHz, random sites, low antennas** ([R26] §2.5 and Figs 23, 25, 27, [T] read; tabulations in [R27]). CW, vertical polarisation; values are readings from the figures (±2 dB) of median and inter-decile range of basic transmission loss:

| Area (terrain parameter Δh) | f, heights | 3 km | 5 km | 10 km | 20 km | 30 km | 50 km |
|---|---|---|---|---|---|---|---|
| Colorado plains (90 m) | 20 MHz, 3.3/1.3 m | 100 (80–110) | 101 (95–108) | 121 (116–126) | 126 (122–131) | ≈137 (127–146) | 146 (141–151) |
| | 50 MHz, 4/0.55 m | 113 (91–130) | 130 (110–137) | 137 (125–148) | 144 (131–153) | 158 (148–167) | 163 (155–170) |
| | 50 MHz, 4/1.7 m | 113 (85–125) | 123 (106–133) | 130 (122–144) | 140 (133–148) | 157 (148–163) | 159 (150–168) |
| Colorado mountains (650 m) | 20 MHz, 3.3/1.3 m | | | 143 (108–147) | 147 (140–158) | 157 (145–165) | 153 (145–162) |
| | 50 MHz, 4/0.55 m | | | 139 (127–148) | 162 (144–168) | 165 (152–172) | 175 (162–180) |
| | 50 MHz, 4/1.7 m | | | 135 (117–151) | 163 (146–173) | 165 (148–173) | 169 (163–176) |
| NE Ohio, hilly, partly wooded (90 m) | 20 MHz, 3.7/3 m | | | 124 (115–138) | 132 (124–140) | 134 (129–146) | 146 (140–>160) |
| | 50 MHz, 4.2/1 m | | | 130 (117–147) | 138 (132–148) | 142 (133–>155) | 158 (140–>168) |

About 190 plains paths, 46 mountain paths. Longley & Reasoner add that some groups span 60 dB between best and worst path of the same length, and that repeated measurements on different days over a single path differed by "as much as 15 to 20 dB" ([R26] §2.6, p. 50; statement covers their whole 20–10,000 MHz data set).

**Model output, 45 MHz** ([R25] §7.1, Fig. 5, Table 6, [T] read). ITM area mode, vertical, both antennas 2 m, Δh = 90 m, εr 15, σ 5 mS/m, continental temperate, single-message statistics. Quantiles of Lb (50 %/90 %/95 %): 1 km 96.9/109.7/113.4 dB; 5 km 119.5/132.6/—; 10 km 130.3/143.7/147.7; 30 km 145.3/159.1/163.2. For a 16 dBW, 25 kHz FM set tolerating 128 dB the predicted range is 8.7 km at 50 % and 2.9 km at 95 % reliability.

**Egli** ([R29] pp. 1383–1385, [P] read). Median field over irregular terrain equals the plane-earth field reduced by a terrain factor proportional to (40/f_MHz)², independent of distance, i.e. the median equals plane earth at 40 MHz; site-to-site distribution log-normal with σ = 8.3 dB in the VHF data (centre 127.5 MHz; about 1,400 measurements at 50–250 MHz) and 11.6 dB at UHF; data adequate only to 30–40 miles.

**Forest and jungle** ([R40] §3 summary, [T] read by OCR). Thailand, wet-dry monsoon forest of about 130 tons per acre, 0.1 MHz–10 GHz, 25 ft to 30 miles, antennas 7–80 ft:

- The vegetation loss is independent of distance beyond about 0.1 mile, consistent with a tree-top lateral wave.
- Vertical polarisation, low antennas, 1–12 MHz: 40 log d plus a constant excess rising from 0 dB at 1 MHz to 19 dB at 2 MHz and "gradually to 24 dB at 25 Mc/s".
- Above 25 MHz over rough terrain the loss is 10–20 dB greater than expected without vegetation, with scatter comparable to Egli's.
- Horizontal polarisation suffers less loss than vertical "by as much as 20 dB at 25 Mc/s" at low antenna heights; the difference vanishes by about 400 MHz and 80 ft. Received waves are strongly depolarised.
- Raising one antenna from 10 to 80 ft gains 18 dB at 25 and 50 MHz (vertical, rough foliated terrain).
- Pseudo-periodic spatial variation with a half-cycle of about 0.37 λ (§5.1.3; the §3 summary's scan reads "0.3?") and mean amplitude 10 dB at 25 MHz (vertical), 4 dB (horizontal).
- General statistical model beyond 2 miles: Lb = 116.57 + 20 log f(MHz) + 40 log d(miles) − G(h), with G = −17 + 20 log(Ht·Hr) at 25 MHz and −16 + 20 log(Ht·Hr) at 50 MHz, heights 10–80 ft, vertical ([R40] §5.5.2, eq. 5, Table 5.15).

ITU-R P.833-10 gives a typical woodland specific attenuation at 30 MHz of about 0.02 dB/m vertical and 0.006 dB/m horizontal, capped by a maximum attenuation because the lateral/surface wave takes over ([R10] Fig. 2 and eq. 1, [S] read; figure read by eye). Hill's calculation for a 56.6 km forested path in south-west Germany found forest cover important at 5 MHz but a much smaller effect at 20 and 30 MHz, where diffraction dominates ([R23] §6.2; calculation only, no measurements).

**Short-range cluttered and indoor, low VHF** ([R41] abstract, [P]). Near-ground indoor/outdoor links to 200 m, line-of-sight and obstructed, robot-borne receiver, thousands of positions: the channel is "a nearly ideal scalar attenuation and delay transfer function", with minimal phase distortion, "little to no evidence of multipath" and no small-scale fading, unlike higher VHF. Frequency and delay-spread numbers are not in the abstract.

### D. Variability

| Quantity | Value | Conditions | Source, grade |
|---|---|---|---|
| Location variability, smooth open ground | log-normal, σ 3–4 dB (mean 3.5) between nearby points | stated generally for ground wave; data mainly LF/MF | P.368-10 Annex 1 Note 2 [R1] [S]; [R3] §9 gives 2–4 dB |
| Location variability, irregular terrain | σ 8.3 dB (VHF); ITM deviations "on the order of 10 dB"; inter-decile 15–35 dB at 20–50 MHz | see C | [R29] [P]; [R25] statistics section [T]; [R26] [T] |
| Time vs location variability, short low-antenna land paths | path-to-path variability "considerably greater than the time variability" | ITM design basis | [R25] §4 [T] read |
| Long-term (hourly-median) variability, 40–88 MHz | Y(10 %) = 1.30·Y0(10), Y(90 %) = 1.29·Y0(90); Y0(10) peaks at 10.3 dB and Y0(90) at −8.1 dB near effective distance 150–200 km; tails Y(1) = 2.00·Y(10), Y(0.1) = 2.73·Y(10), Y(99) = 1.82·Y(90), Y(99.9) = 2.41·Y(90) | continental temperate; data from continental USA, median frequency 47.1 MHz; all hours | TN101 §10.3, Figs 10.2, 10.3 (all-year frequency factors at 47 MHz) and 10.5 [R28] [T] read |
| → worked example | 100 km, 3 m antennas, 30 MHz: effective distance about 116 km, Y(10) ≈ +11 dB, Y(90) ≈ −8 dB, Y(99) ≈ −14 dB, Y(99.9) ≈ −19 dB; at the peak (about 165 km) Y(1) ≈ +27 dB, Y(99) ≈ −19 dB | TN101 eqs 10.1–10.7 applied here; extrapolated from 40 to 30 MHz | [E] from [R28] |
| Standard deviation near the horizon | "may be as much as twenty decibels for short transhorizon paths" where diffraction and scatter are comparable | continental temperate, all bands | [R28] §10.2, p. 10-8 |
| Short-term behaviour by mechanism | diffraction "fades slowly if at all"; scatter is Rayleigh; aircraft give "rapid, intense, and relatively regular fading"; meteor bursts and some ionospheric modes "add spikes" | general VHF/UHF | [R28] §10, p. 10-1 |
| Troposcatter slow/fast fading | hourly medians log-normal, σ 4–8 dB by climate; within about 5 min Rayleigh; rate "a few fades per minute at lower frequencies", proportional to frequency and path length | P.617 scope above 30 MHz; rate data from 2 GHz | [R9] Annex 1 §4 and Attachment §2 [S] read |
| Seasonal and day-to-day ground-wave changes | 5–15 dB winter–summer at 0.5–1 MHz; rms day-to-day 2–4 dB at MF over tens of km | LF/MF only; **no 30 MHz data found** | [R1] Note 2; [R3] §11.2 |
| Phase/time-of-arrival stability | Loran-C time of arrival varies by up to ±0.5 µs, diurnally and with fronts, correlated with the dry-term refractivity gradient | 100 kHz, long paths | [R3] §19.6 [S] |
| → scaled to 30 MHz, 100 km | phase excursion of order 100° over hours (72° from a 20 N-unit surface change via nkd, plus about 20° from a k change of 1.25 to 1.6), i.e. apparent Doppler of order 10⁻⁵–10⁻⁴ Hz | own arithmetic | [E] |

### E. Modes that contaminate or replace the groundwave

Levels are basic transmission loss between isotropic antennas (free-space-antenna convention) unless stated. "Delay" is excess over the groundwave for a 100–300 km link.

| Mode | Level | Excess delay | Doppler | Occurrence | Sources |
|---|---|---|---|---|---|
| **Tropospheric scatter** | Median 172/180/187/192 dB at 200/300/400/500 km at 30 MHz for antennas that see the horizon; low antennas lose a further 23/13/4 dB per end at 3/10/30 m height (300 km) | µs | fading a few per minute; Rayleigh within 5 min | permanent | P.617-6 eq. 4 evaluated here (N0 = 330, dN = 40, zero horizon angles; re-computed: 171.9/180.5/186.6/191.5 dB), formula applied at its lower frequency bound; lobing factor 2 sin(2πh sinα/λ) [E from R9]. TN101: diffraction and scatter about equal at an angular distance corresponding to 65(100/f)^(1/3) km = 97 km at 30 MHz beyond the radio horizons, for a 9,000 km effective radius ([R28] eq. 10.1) |
| **Ducting / super-refraction (sea)** | up to 25 dB enhancement at 32 MHz, 235 km | none | slow | climate-specific; hours | [R15], [R16] [P] |
| **Sporadic E, one hop** | Free space plus Γ; Γ = 15–17 dB at 1,500–2,000 km for foEs = 7 MHz and 26–33 dB for foEs = 5 MHz at 30 MHz, i.e. Lb ≈ 142–158 dB at 1,500 km. At 100 km Γ ≈ 187 dB for foEs = 10 MHz: no short-range Es path | none on a short link (co-channel interference only); 30–40 µs relative to ionospheric scatter on the Round Hill–Cedar Rapids path (about 1,700 km great circle [E]) | small; polarisation fluctuates rapidly about a fixed direction for dense layers | Europe/N. Africa: foEs ≥ 7 MHz about 4 % of May–Aug 0800–2300 h, 2.7 % of May–Aug, 1.1 % annually, 0.1–0.3 % in winter/equinox; ≥ 10 MHz 0.5 % of summer daytime. Events last minutes to hours over patches about 100 km across | P.534-6 eqs 1–5 and Fig. 2 (read by eye) [R11] [S]; delay from Abel et al. [R33] [P]; [R13] §1.5, [R14] §8.1.3. Γ arithmetic [E] |
| **Regular F2** | near free-space loss when open (not quantified in the sources read; skywave topic) | none on a short link | not given in the sources read | near solar maximum "a significant fraction of the time" above 30 MHz; severe to 50 MHz at mid-latitudes (day, equinox and winter) and 60 MHz at low latitudes; distances 3,000–6,000 km E–W, to 10,000 km N–S | [R13] §1.3 and Table 1 [S] |
| **Ionosphere-supported ground backscatter** | Roughly 150–190 dB total for CW (see estimate below) | 3.5–7.5 ms via Es (ground range 500–1,100 km); 10–20 ms via F2 (1,500–3,000 km) | Sea: Bragg lines ±0.56 Hz plus ionospheric shift; land: near zero plus ionospheric shift | whenever F2 or Es supports 30 MHz obliquely; documented as self-interference at 35–45 MHz in 1957 | Eshleman 1957, pp. 1713–1714 [R34] [P] read (qualitative); level and delay [E] |
| **Meteor trails** | Lb ≈ 167 dB (q = 10¹⁴ m⁻¹) and 187 dB (10¹³) at 30 MHz on a 100 km path; 178/198 dB at 1,000 km (hot spots about 100 km off the great circle). P.843-1 Fig. 3 (q = 10¹⁴, horizontal polarisation, trails at right angles to the plane of propagation) gives about 168.5 dB at 100 km, 171.5 dB at 300 km and 181.5 dB at 1,000 km at 40 MHz, i.e. about 165, 168 and 178 dB at 30 MHz with λ³ scaling [E] | 0.4–0.7 ms at 100 km (path midpoint to hot spot); 0.37 ms at 300 km (hot spot) [E]; rms spread within one trail < 100 ns for 90 %, < 400 ns for 99 %; multi-trail overlap > 1–2 ms on < 1–2 % of trails | trail drift "of the order of 20 Hz at 40 MHz", 2–20 Hz; head/forming-trail whistles from zero to several kHz, > 90 % above carrier | flux I(q > q0) = 160/q0 m⁻² s⁻¹; diurnal ×4 (max 06 h, min 18 h local); seasonal ×4 (min February, max July, N. hemisphere); about 5 % of trails usably oriented; underdense decay constant 0.07 s (100 km path) to 0.8 s (1,000 km) at 30 MHz; overdense seconds, occasionally minutes | P.843-1 §§1–12 [R12] [S]; [R14] §8.1.4; Bailey et al. pp. 1187–1188 [R32] [P]; geometry and losses [E] |
| **Ionospheric (D/E) forward scatter** | weak, continuous; signal falls roughly as f^(−7.5) with scaled antennas ([R14]; measured effective exponents 4–12, typically 6–8, [R32]); 1 µV at the receiver corresponded to 199 dB terminal-to-terminal loss in the 1950s 30 kW, 18 dB-rhombic systems | n/a for short links (ranges 800–2,000 km, scatter height about 85 km) | fading 0.2–5 Hz in a 2 kHz bandwidth; Rayleigh; ±20 dB about the mean; scatterer rms velocity 6–7 m/s | persistent in most areas | [R14] §8.1.6 [S]; [R32] pp. 1186–1187 and Table I [P]; [R33] p. 1263 [P] |
| **Trans-equatorial** | "strong", low attenuation | n/a (paths 3,000–9,000 km) | afternoon type ±2–4 Hz; evening type fading to about 15 Hz and spread "sometimes exceeds 40 Hz" | high sunspot years; afternoon 17–19 h, evening 20–23 h LMT; equinoxes; to 60 MHz (afternoon) and 100 MHz (evening) | [R13] §1.4 [S]; [R14] §8.1.2; Nielson & Crochet abstract [R42] [P]; Heron & McNamara abstract [R43] [P] |
| **Auroral field-aligned scatter** | not quantified in sources read | ms-class, inferred from the off-great-circle geometry [E] | "rasping note due to multiple Doppler shifting"; no figure in the sources read | local K ≥ 5; late afternoon to midnight; mid to high geomagnetic latitudes | [R13] Table 1, §1.7 [S] |
| **Equatorial flutter (spread F)** | deep fades with high mean level | — | rate proportional to frequency, "ten fades per second being typical at 15 MHz" (so about 20/s at 30 MHz) | after sunset to midnight, within ±20–30° geomagnetic latitude, equinox maxima near solar maximum | [R14] §6.5.4.1.2 [S] |
| **Aircraft scatter** | equivalent Lb 171–181 dB for an aircraft of 100–1,000 m² bistatic cross-section at 10 km altitude over the midpoint of a 100 km path; 170 dB at 50 km and 193 dB at 200 km (100 m²); measured airplane flutter on a 60.75 MHz television carrier: fluctuations of about 2–4 dB, geometry not given in the abstracts | 3–13 µs | up to ±2v/λ = ±50 Hz at 250 m/s, ±14 Hz at 70 m/s; passes through zero when crossing the path | traffic-dependent; seconds to minutes per pass | bistatic radar equation λ²σ/((4π)³R1²R2²) [E, re-computed]; qualitative support [R28] p. 10-1, [R9] Attachment §2; flutter [R52] [P] abstract |

Backscatter estimate [E]: for CW the whole illuminated annulus scatters. With range R = 1,000 km, annulus area 2πR·ΔR ≈ 3×10¹² m² (ΔR = 500 km), σ⁰ ≈ −25 dB (Barrick's sea upper limit is −17 dB [R19]), λ = 10 m and isotropic antennas, P_r/P_t = λ²σ⁰A/((4π)³R⁴) ≈ +20 + 100 − 33 − 240 = −153 dB. Low-angle gain of ground-mounted verticals (−5 to −10 dBi each), ionospheric focusing/defocusing and partial illumination move this to roughly 160–190 dB. No publication giving this as a level relative to a groundwave was found, and no measured backscatter level at 25–50 MHz was obtained; treat it as a plausibility bound, not a measurement.

Meteor-rate estimate [E]: trails with q above about 2×10¹³ m⁻¹ give echoes at or above a 180 dB groundwave on a 100 km path. P.843's flux gives 8×10⁻¹² m⁻² s⁻¹; over a 200 km-radius patch of the meteor region that is about one trail per second, of which 1–5 % are suitably oriented (P.843's 5 % is for a 1,000 km path), i.e. some tens to a hundred or more echoes per hour before antenna-pattern weighting (a ground-mounted vertical with the 3cos²θ pattern has about −1 to +2.5 dBi at the 40–60° elevations involved, 2–6 dB below its low-angle maximum), with the ×4 diurnal and seasonal swings. Cross-check: a model run for a 1 kW, 30 MHz, 1,288 km system (15 and 7 dBi antennas, May) with a 197.8 dB power factor gave a mean interval between usable bursts of 2.6–8.1 s over the day and a 1.0 s mean burst duration ([R44] Fig. 1, [T]); scaling the rate as 1/q0 to a 180 dB threshold gives 60–180 per hour. That scaling corresponds to an amplitude-distribution index ψ = 1; with the 1.1–1.4 that P.843-1 gives as typical, it becomes about 25–145 per hour ([R12] §3) [E]. Short paths have less favourable geometry, so the true figure could be several times lower; an order-of-magnitude uncertainty should be carried. Weitzen examined duty cycle, throughput and waiting time at ranges below 400 km and concluded that connectivity can be maintained there with efficient protocols and suitable antenna patterns ([R51], abstract; no numbers in the abstract). For scale, the Canadian JANET link ran at about a 5 % duty cycle ([R45] p. 1667, [P]).

### F. Weak-signal extremes and what 180 dB corresponds to

"180 dB transmitter-to-receiver" is a system loss; antenna gains must be added to compare with Lb. At 30 MHz electrically small antennas are inefficient, so the same 180 dB can mean very different geometries. The table below equates the 180 dB with Lb; where external noise dominates, the GUI's 180 dB at −164 dBm/Hz instead corresponds to Lb near 175 dB (see "Comparison with the modem's presets"):

| Geometry giving about 180 dB | Basis |
|---|---|
| Smooth sea, vertical antennas at the waterline: about 280 km (σ = 5 S/m, k = 4/3); 260 km at 4 S/m; shortened by roughly 45–55 km in sea states 5–6 (Barrick's Phillips-spectrum 12–14 dB near 230–280 km, at about 0.26 dB/km); lengthened to 320–370 km for k = 2–3 | [E] table A; [R18] Fig. 31 |
| Same with 300 m of beach at one end: about 240 km | [E] Millington |
| Smooth homogeneous land, ground-level antennas: 57 km (very dry) to 113 km (3×10⁻² S/m land) | [E] table A |
| Same with 10 m masts at both ends: about 125 km over 3 mS/m ground (8.3 dB gain at 0.3 dB/km) | [E] |
| Irregular terrain, 1–4 m antennas, 50 MHz: the worst decile of 50 km mountain paths (medians 169–175 dB, upper decile 176–180 dB) | [R26] Fig. 25, measured |
| ITM, 45 MHz, 2 m antennas, average terrain: 95 % quantile is 163 dB at 30 km and rising about 0.6 dB/km, so 180 dB at roughly 55–60 km for the worst 5 % of situations, and beyond 80 km at the median | extrapolation of [R25] Fig. 5 [E] |
| Jungle, 3 m (10 ft) antennas: 47 km (29 miles) at 25 MHz, 35 km at 50 MHz | [R40] eq. 5 evaluated [E, re-computed]; at the edge of the measured range |
| Hilltop-to-hilltop tropospheric scatter: about 300 km | P.617-6 extrapolated [E] |
| Two −25 dBi body-worn or loaded-whip antennas (an assumed gain, not from a cited source): Lb = 130 dB, i.e. about 10 km at the ITM median for 2 m antennas, or 3–5 km at 90–95 % | [R25] Fig. 5 [E] |

Documented practice. ITM's worked low-band FM example (45 MHz, 40 W, −3 dBi antennas at 2 m) gives 2.9–8.7 km for 95–50 % reliability [R25]. The US Army's tactical radio manual gives SINCGARS (30–88 MHz, 25 kHz channels) transmission ranges of 200 m to 10 km over its three power settings and 40 km line of sight with a power amplifier; with the vertically polarised OE-254 broadband mast antenna at both ends its planning ranges are 57.9 km over average and 48.3 km over difficult terrain at high power, and 19.3 km at low power ([R47] §4-6 and Table 9-4, [T] read). Meteor-burst circuits at 30–100 MHz are operated to 1,800 km ([R12] considering b). No amateur 10 m or 6 m ground-wave or troposcatter record, and no HF surface-wave-radar one-way loss figure at upper HF, was found (see last section).

## Moderate → adverse → worst-case parameter table

All for 30 MHz unless stated. "GW" = groundwave. Worst-case cells that combine effects nobody has reported together are marked †.

| Parameter | Moderate | Adverse | Worst case / outage |
|---|---|---|---|
| Lb, sea, 100 km, waterline verticals | 132 dB, smooth sea [E, checked against R2] | 141–143 dB, sea state 5–6 [R18 Fig. 31] | about 157–162 dB with 300 m of beach at each end (13–19 dB) and sea state 6 † [E] |
| Lb, sea, 300 km | 186 dB (k = 4/3) [E]; 174 dB for k = 2 | 196 dB (k = 1) [E] | about 205–210 dB: k = 1 plus sea state 6 † [E, R18] |
| Lb, land, ground-level, 10 km | 125–131 dB (3×10⁻² to 3×10⁻³ S/m land) [E] | 140–145 dB (dry) [E] | 148 dB at the 95 % ITM quantile for 2 m antennas at 45 MHz [R25] |
| Lb, irregular land, 50 km, 1–4 m antennas | 146 dB median at 20 MHz, plains [R26] | 159–163 dB median at 50 MHz, plains [R26] | 175–180 dB, mountain upper decile at 50 MHz [R26] |
| Location variability | σ 3–4 dB, open smooth ground [R1] | σ 8–10 dB, irregular terrain [R29, R25] | 60 dB spread between paths of equal length [R26] |
| Vegetation excess loss | 0.6–2 dB per 100 m of woodland (H/V) [R10] | 10–20 dB, antennas inside forest above 25 MHz [R40] | 24 dB constant excess at 25 MHz, vertical, low antennas in jungle [R40] |
| Sea-state added loss, 100 km | 1–4.5 dB (5–10 kt) [R18] | 6–8 dB (15–20 kt) | 9–15 dB (25–30 kt; model-dependent; theory at its validity limit) [R18] |
| Refractivity effect | ±2 dB at 100 km for k = 1–2 [E] | ±10 dB at 300 km for k = 1–2 [E] | +25 dB ducting enhancement at 32 MHz, 235 km [R15]; no documented deep fade counterpart at HF |
| Hourly-median variability (beyond horizon, 40–88 MHz statistics) | +11/−8 dB (10 %/90 %) at 100 km [E from R28] | +13/−10 dB at 150–200 km effective distance [R28] | −19 dB (99 %), −25 dB (99.9 %); +27 dB (1 %) [R28] |
| Short-term fading of the wanted path | none measurable (diffraction "fades slowly if at all") [R28] | Rician ripple of tenths of a dB from sea clutter [E] | Rayleigh, a few fades per minute, when troposcatter dominates (elevated sites, beyond about 100–300 km) [R9] |
| Doppler shift/spread of the wanted GW | 10⁻⁵–10⁻⁴ Hz equivalent phase drift [E] | synthetic sea-term sensitivity near first-order ±0.56 Hz with selected −25 to −35 dB power [E], not a measured or universal spectral bound | troposcatter fade spectrum of order 0.01–0.1 Hz [R9, qualitative] |
| Delay spread of the wanted GW | near-scalar channel and little/no multipath in Dagefu et al.'s 40 MHz, ≤200 m near-ground clutter measurements [R41]; this does not establish long-path 30 MHz behavior | µs-class terrain echoes; ripple 20 dB over < 50 m of travel [R24] | still flat in 3 kHz; no source gives delay spread at 20–50 MHz beyond 200 m |
| Meteor echoes | absent at high signal levels | bursts 0.07–0.8 s, 0.4–0.7 ms late, 2–20 Hz shift, of order ten to a hundred per hour at the 180 dB level (order-of-magnitude uncertainty) [E from R12, R44] | overdense trails lasting seconds to minutes; kHz head-echo chirps; showers [R12, R32] |
| Aircraft | none | one component at 171–181 dB, Doppler sweeping through zero, up to ±50 Hz [E] | aircraft component stronger than a 180 dB GW for the duration of a pass [E] |
| Sporadic-E co-channel interference | absent (winter, about 0.1 % of time) [R11] | present about 1 % annually, 4 % of summer daytime (Europe, foEs ≥ 7 MHz), interferers from 1,000–2,000 km arriving 20–40 dB stronger than a 180 dB GW for equal radiated power [R11, E] | hours-long openings; multi-hop to 4,000 km [R11] |
| F2 co-channel and backscatter | absent (solar minimum, night) | daytime openings near solar maximum; backscatter echo 10–20 ms late at 160–190 dB [R13, R34, E] | open most daylight hours at solar maximum, equinox/winter; 50–60 MHz reached [R13] |
| Flutter-type modes | absent at mid-latitudes | afternoon TEP, ±2–4 Hz [R13] | evening TEP: spread > 40 Hz, fades to 15 Hz; equatorial flutter about 20 fades/s; auroral scatter at K ≥ 5 [R13, R14] |
| Polarisation | vertical, either sense irrelevant above one wavelength height over land [E] | horizontal on low land antennas: 17 dB worse at 3 m [E] | horizontal at sea level: 64 dB worse at 3 m [E]; in jungle vertical is up to 20 dB worse than horizontal at 25 MHz [R40] |

## How the literature models or simulates this

| Model | What it is | Validated domain | Known limits at 20–50 MHz |
|---|---|---|---|
| ITU-R P.368-10 / LFMF-SmoothEarth; GRWAVE (P.368-9) | Smooth homogeneous spherical Earth; flat-earth and residue-series solutions; Millington for mixed paths | 10 kHz–30 MHz; LFMF repository states 0.01–30 MHz and antenna heights below 50 m [R48, G] | Stops at 30 MHz; no terrain, vegetation, sea state or ducting; antennas "on or near" the ground; P.368-10 changed the atmosphere from exponential to linear [R1] |
| ITU-R P.526-16 | First-mode diffraction with height gain; full method for 10 MHz and above | all frequencies; LFMF required when K > 1 | β = 1 over land above 20 MHz: no ground-constant sensitivity [R5] |
| Barrick effective impedance | Rough-sea surface impedance inserted in a smooth-earth code | HF; checked at 7 and 14 MHz for attenuation rate [R21] and broadly at 4–20 MHz [R15]; Kirincich–Emery's 16.188 MHz 45 km below-horizon one-way result is a relevant but not 30 MHz replication [R22] | Perturbation limit reached at 30 MHz and 25 kt; spectrum-model dependence of several dB; no swell [R18]. In the 16.188 MHz record, simple wind-based rough impedance did as well or better than complex alternatives; the 7 km high/visible receiver sampled a different propagation mixture [R22] |
| Integral-equation codes (WAGNER, WAGSLAB) | Hufford/Ott integral equation over terrain profile with optional forest/building/snow slab | LF–HF; compared with 5–28 MHz measurements [R24] and, with "encouraging agreement", with nine paths at 120 kHz–50 MHz, of which the 20 MHz Colorado-mountain data were the hardest case [R50] | 2-D profile only: off-path reflections ignored, errors to 25 dB at 28 MHz on very irregular paths [R24]; cost grows with frequency and roughness [R23]; slab parameters uncertain |
| Longley–Rice / ITM | Semi-empirical: two-ray, diffraction, troposcatter with empirical time/location/situation variability | 20 MHz–20 GHz; low-VHF end built on the 20/50/100 MHz Colorado and Ohio data [R25, R26] | Excludes ionospheric modes by construction; sparse ground cover only; urban, dense forest, time-of-day effects need added allowances [R25 §3] |
| NBS Technical Note 101 | Point-to-point transhorizon loss with climate-dependent long-term variability | 40 MHz–10 GHz; 40–88 MHz variability from US data at a median of 47 MHz [R28] | Below 40 MHz is extrapolation |
| Egli | Plane earth times (40/f)² with log-normal terrain spread | 40–1,000 MHz, to 30–40 miles [R29] | Fits distance only; fails in frequency and height in jungle [R40 §5.5.1] |
| Tamir / Dence–Tamir slab; Jansky & Bailey statistical model | Lateral-wave theory; empirical 40 log d law with height function | 2–200 MHz forest [R37, R38]; Thailand data to 30 miles [R40] | One forest type; slab parameters not transferable without measurement [R23] |
| ITU-R P.1546-6, P.1812-8, P.2001-6 | Empirical curves (P.1546) and path-specific models from 30 MHz | 30 MHz lower bound; P.1546 curves are for 100 MHz and scaled to 30–300 MHz [R6 Annex 2 §1] | P.1546 ignores ionospheric effects though it notes sporadic E below about 90 MHz beyond 500 km [R6 §6]; only P.2001 includes sporadic E [R8] |
| ITU-R P.617-6 | Empirical troposcatter median and distribution | frequencies above 30 MHz; fading-rate data at 2 GHz [R9] | 30 MHz is the boundary; assumes antennas with horizon gain |
| ITU-R P.534-6, P.843-1, P.844-1 | Sporadic-E field strength and occurrence; meteor-burst link equations; catalogue of VHF ionospheric interference modes | mid-latitudes within ±60° geomagnetic for Es [R11]; 30–100 MHz meteor circuits [R12] | P.844 dates from 1994 and is qualitative; high-latitude Es statistics "need to be further clarified" [R11] |
| Parabolic-equation and time-domain codes | Terrain plus refractivity, calibrated against the analytical models [R49, abstract] | not assessed: the Sevgi-group papers were not obtained, and no 2015–2026 PE/FDTD validation against measurements at 20–50 MHz was found | — |

## What a high-fidelity simulation would need

This is a list of options with their limits, not a design. Better structures may exist.

**Inputs that the evidence says cannot be omitted**

1. Path class: all-sea, mixed with stated land-strip widths at each end, smooth land, irregular land (with Δh or a profile), forest, built-up.
2. Antenna height above local ground or sea, polarisation, and real antenna efficiency. Over land a height error of a few metres is worth 5–10 dB; over sea the height gain is negative up to about 100 m.
3. Ground constants. Over land εr matters more than σ at 30 MHz; over sea σ (3–5 S/m) matters beyond 100 km.
4. Sea state (wind speed, direction relative to path, and ideally a wave spectrum including swell).
5. Refractivity: at least an effective-earth-radius factor distribution; for warm-sea climates a duct occurrence model.
6. Epoch: solar-cycle phase, season, local time, geomagnetic latitude and activity, to switch the ionospheric modes on and off.
7. Loss convention, stated explicitly (P.368 or free-space-antenna), since 6 dB hangs on it.

**Structure options**

- *Option A: static scalar channel plus slow level process.* Deterministic median from a smooth-earth or terrain model; log-normal site term; slow log-normal time term (hours) from TN101-type distributions; no Doppler, no delay. Defensible for land paths inside about 50 km and sea paths inside about 150 km when the loss is well below 160 dB. Limit: wrong whenever any row of table E is within about 10 dB of the wanted level.
- *Option B: A plus a small coherent-plus-diffuse sea term.* A synthetic Rician component may use a selected sub-hertz spectrum and K=25–35 dB for sensitivity. The first-order ocean Bragg frequency is not a bound on higher-order, advected or other moving-path spectra. Neither K nor a complete spectrum is measured or derived here; [R53] establishes a strong direct signal in its radar geometry only.
- *Option C: A or B plus an event layer.* Independent point processes for meteor echoes (Poisson arrivals, exponential decay with the P.843 time constant, 0.4–0.7 ms delay, 2–20 Hz shift, power-law amplitude distribution with exponent 1.1–1.4, diurnal and seasonal modulation), aircraft passes (deterministic Doppler track from a flight geometry, level from the bistatic radar equation), and ionospheric openings (on/off states with P.534 occurrence statistics; when "on", co-channel interferers and, optionally, a backscatter echo at 3.5–20 ms with about 1 Hz spread). Limit: rates and levels for the event layer are mostly derived, not measured, at these link budgets.
- *Option D: mode-switched long-path model.* Beyond roughly 100 km over land or 300 km over sea, replace the diffraction term by the power sum of diffraction and Rayleigh-fading troposcatter with a ground-lobing factor for the actual antenna heights. Limit: P.617 at 30 MHz is an extrapolation.
- *Option E: physics-based terrain/refractivity propagation* (integral equation or parabolic equation) for specific paths, with Barrick's impedance for the sea. Limit: 2-D, expensive, and shown to miss off-path multipath by up to 25 dB at 28 MHz.

**Observables to validate against**

- The P.368 curves and Barrick's Fig. 8 for the smooth-earth core (the chapter's implementation reproduces both to 1–2 dB, and an independent one reproduces table A to 0.1 dB).
- Adams et al.'s 28 MHz medians, decile spreads and 20 dB-in-50 m ripple, after the 6 dB convention shift described under C; the 20/50 MHz Colorado and Ohio medians and inter-decile ranges.
- Hansen's 235 km record: agreement below 20 MHz, +25 dB episodes at 32 MHz.
- P.843's delay-spread statistics and burst decay constants for the meteor layer.
- P.534 occurrence percentages for the Es switch.
- For anything involving Doppler on the wanted path there is nothing to validate against; a new measurement (a GPS-disciplined carrier over a 30–100 km sea and land path, logged in complex baseband for weeks) would be needed.

## Critical inputs, empirical anchors, and transfer limits

The following map distinguishes a measured environmental/input variable from a measured communication output. A detailed solver cannot remove uncertainty in unmeasured surface state, profile, geometry, antenna, or propagation-mode attribution. Conversely, an operational radar range cannot be inserted as if it were one-way modem path loss.

| Critical input or desired output | Primary locator and actual observation/condition | Supported use here | Transfer boundary / missing validation |
|---|---|---|---|
| Smooth homogeneous ground conductivity/permittivity; baseline surface-wave/diffraction loss | P.368-10 Annex 1 and P.368-9 curves [R1–R3]; ground-wave handbook eqs. 5–19. NTIA-84-151 §1, tables 2–3, appendix B and Figs. 47/50 [R24] provides four daytime Boulder routes up to 45 km at 5.25, 7.5, 11.5 and 28 MHz, median bins and wave-tilt ground estimates. | The chapter's Sommerfeld–Norton and residue-series reference outputs; empirical check that ground and terrain inputs matter at 28 MHz. NTIA path samples have roughly 20 dB path-median spread at equal distance, 13–18 dB within-bin decile spread on mountain geometry, and measured 20 dB ripple over <50 m at 7.5/11.5 MHz. | NTIA measurements are composite received fields (direct plus reflections), mostly daylight; the model predicts a direct component. Wave-tilt ground constants are approximate, transmitter-site effective height is unresolved, and the path result is not a 30 MHz sea surface wave. Do not fit one deterministic loss curve to the route scatter. |
| Sea conductivity, salinity, temperature and vertical stratification | P.368 sea constants [R2, R3]; 25 MHz Fraser River-plume CODAR study [R55] §§2–4 analyzes the single Westshore station's radial-current solutions over 1.6 years; ferry conductivity is sampled at nominal 2 m, and the authors state no wave record existed inside the radar domain and warn surface freshwater stratification can make the EM-sensed skin conductivity lower than the 2 m sample. The observed working range increased from 19.4 km at 0.9 S/m to 37.4 km at 3.5 S/m. K&E [R22] is a distinct 16.188 MHz coastal path with observed 4.5→2.5 S/m seasonal conductivity swing. | Physical prior and plausible ranges for seawater/estuary input; motivate surface-layer sensitivity and separate low-height below-horizon from elevated line-of-sight cases. | Neither study validates 30 MHz absolute loss. The 25 MHz dependent variable is the farthest usable radial-velocity solution from a single radar station, with a 6 dB first-order Bragg-line SNR threshold; it is capped at 39.5 km, and records reaching that limit were excluded whether the limit was spurious or real ([R55] §3b, §4b). Because the conductivity relation rises toward 37.4 km, this exclusion truncates/selects away its upper-range tail; the published range relation should not be treated as uncensored at high conductivity. It is not one-way received power. K&E found no detectable seasonal received-power effect on its 45 km 16.188 MHz series, despite the conductivity swing. Conductivity range alone cannot justify a temporal fading law. |
| Sea state: full spectrum, significant height, frequency and directional distribution; roughness scattering loss | Barrick report [R18] model/figures; Hansen [R15] full article describes one 235 km route, dual-daily soundings over one month and an hourly 24-hour run; sea-state correction uses inferred winds from the same received levels. Halverson et al. [R55] §§2, 4–5 explicitly separates rough-surface propagation loss from the Bragg scattering cross-section. They report opposite signs of working-range dependence across 30 MHz North Sea, 25 MHz Adriatic and lower-frequency fetch-limited examples, and explain that loss and target-strength terms compete. | Bound candidate sea-surface model inputs; retain both propagation-loss and competing scattered-return terms in radar-specific sensitivity checks. Treat Barrick sea-state curves at 30 MHz as approximate because the small-roughness perturbation assumption is at/near its limit. | Hansen's wind/sea-state agreement is not an independent validation because the model derived winds from the signal being corrected; station winds were spatially inconsistent, and the TX bluff-gain error prevents overall loss-uncertainty assignment. Wind speed is not a sufficient wave-state input; fetched sea, swell, directionality, breaking waves, wave/current interaction and local water depth are unresolved. Radar working-range sign is not a modem forward-link fade sign. No matched 30 MHz complex-baseband sea-path record with wave spectrum and direct/scattered decomposition is in evidence. |
| Terrain profile, local obstacles, soil moisture and vegetation | NTIA-84-151 [R24] methods, terrain profiles, wave-tilt appendix and 28 MHz tables; Longley–Reasoner [R26] Figs. 23/25/27 using NTIA irregular-terrain spot data; Hill [R23]; Jansky–Bailey [R40] §§3, 5.5. | Form competing geometries: unobstructed smooth earth, irregular terrain with diffraction/reflections, and propagation partly within/over forest. The 20/50 MHz NTIA records bracket 30 MHz but are spot VHF terrain measurements. | None yields a route-independent 30 MHz long-path coherent channel. The advertised raw Phase 1 data have not been parsed in this review; 20/50 MHz data are not direct 30 MHz measurements and do not label every path's mode. Dense high-fidelity terrain should be used only when a georeferenced route and surface inputs are known. |
| Tropospheric refractivity and ducts; meteorology needed for mode switching | P.617-6 [R9] only supports elevated-site scatter model scope at its low-frequency edge; P.2001 [R8] modeling scope. Hansen's 32 MHz +25 dB episodes [R15] and Pappert–Goodhart radiosonde-based duct calculation [R16] are separate evidence. | Conditioned event/alternative-mode branch; represent ordinary refraction and a deliberately rare deep-duct path as distinct atmospheric states. | One coastal path and abstract-only duct study do not provide climate-wide rates, joint seasonal distributions, or an observed 30 MHz time series. Radiosonde profiles at a path/time and validation are needed before deterministic duct prediction is claimed. |
| Ionospheric co-channel and return paths | P.534-6 [R11] supplies Es occurrence/frequency support; P.844-1 [R13] and Ionosphere Handbook [R14] describe VHF sharing/propagation; Eshleman [R34] documents qualitative 35–45 MHz ground-backscatter interference. | Separate unwanted independent transmitters (Es/F2 skywave) from a wanted groundwave and from an ionosphere-supported ground-backscatter echo. Their carrier, timing, Doppler, polarization and power are not the same channel component. | No matched 25–50 MHz measurements of wanted direct level and ground-backscatter level/path delay were found. The annular backscatter level in table E is an order-of-magnitude calculation, not measured evidence. |
| Meteor/aircraft scatter, event strength and time structure | P.843-1 [R12] §§1–12 has meteor-flux, trail geometry, decay and frequency-scaled echo evidence; Haakinson [R44] is a model; aircraft link levels here are bistatic-radar-equation estimates, while [R52] abstracts report 60.75 MHz flutter without useful geometry. | Keep meteor bursts and moving aircraft as separate intermittent paths/interferers; use geometry-dependent delay and Doppler and mark the amplitude/rate as model or calculated bound. | P.843's representative trial geometry and 40 MHz values are not paired 30 MHz 100–300 km modem records; no aircraft cross-section or flight-track-conditioned channel record was found. Coincident occurrence with a 180 dB wanted path is unverified. |
| Wanted/competing field levels, delay/Doppler and modem success at 30 MHz | No located primary source measures all these jointly. Rogers et al. [R56] gives a 30–100 MHz NLOS measurement-campaign description but the retrieved proceedings text omits numerical channel data/mechanism attribution. Dagefu [R41] only measures 40 MHz clutter links to 200 m. | The chapter's computed levels and structure options are hypotheses for transparent experiments; modem sensitivity must be measured separately using fixed public waveform hypotheses and explicit external-noise assumptions. | The key empirical gap remains: no path-resolved, complex-baseband long-path 30 MHz dataset with synchronized environment inputs, co-channel survey, antenna patterns, and modem decode outcome. Until that exists, no unique scenario probability or validated 180 dB channel law follows from the literature. |

## Comparison with the modem's presets

The 30 MHz rows (4, 5 and 7 with 11.6 s bits, 6 with 257 s bits; 225 Hz occupied) run with one of the HF presets of [00](00-method-and-modem-context.md) §5. Each preset is an idealized direct path that never fades plus at most one echo, no stronger than the direct path, whose delay and Doppler wander stay within the configured bounds. The comparisons below relate those settings to calculations and measured regimes for selected geometries; they do not establish physical upper/lower bounds or prove that a named mechanism is the only way those values could occur. In particular, no reviewed source supplies a path-resolved, complex-baseband long-path 30 MHz measurement that bounds the wanted path's delay or Doppler. The Sub9 and EME presets have no counterpart in this chapter.

| Preset (Doppler; delay) | What this chapter's evidence says |
| --- | --- |
| None (0; 0) | A single quasi-static ground path is a useful nominal simplification, not a measured bound. The chapter estimates an equivalent phase drift of 10⁻⁵–10⁻⁴ Hz under the stated smooth-refractivity geometry [E]; this is not an observed long-path Doppler maximum. The whole-bit tolerances of 00 §4.1 (0.1 Hz and 15 mHz/s for rows 4, 5 and 7; about 5 mHz and 30 µHz/s for row 6 by the same rule [E]) are engineering comparisons only. Wanted-path delay and delay spread above the short-range Dagefu geometries remain unbounded by the reviewed 30 MHz evidence. What "None" also omits is the level trajectory: sea state (1–15 dB at 100 km), refractivity (±2 dB at 100 km, ±10 dB at 300 km), hourly medians (+11/−8 dB) and a rare, observed-on-one-route duct enhancement (+25 dB). |
| HF best (0.1 Hz; 0.5 ms) | The setting can be compared with distinct conditional mechanisms, not rejected as physically impossible. NTIA terrain measurements show a dominant plus weaker component and spatial ripple at 7.5/11.5 MHz on measured paths [R24], but they do not bound 30 MHz temporal Doppler or delay. Short-range 40 MHz Dagefu observations show little multipath only for their ≤200 m geometries [R41]. A 0.4–0.7 ms excess path is a calculated meteor-geometry scale for 100–300 km links, but P.843-type meteor arrivals are intermittent bursts (0.07–0.8 s, modeled rate at the 180 dB level) with estimated 2–20 Hz shifts, not a measured upper bound on every wanted channel. Across 225 Hz a fixed 0.5 ms echo is nearly flat (\|cos(πΔfτ)\| ≈ 0.94 at the band edges [E]); the simulator's stationary weak echo is therefore an abstraction of selected effects. Sea-clutter diffuse energy here is only an estimated −25 to −35 dB and sub-hertz, with no direct forward-path measurement. |
| HF moderate (10 Hz; 6 ms) | 6 ms corresponds to about 1,800 km excess path. An Es-supported ground-backscatter component is one physically considered route to millisecond delay; the 3.5–7.5 ms delay and ~1 Hz spread are estimates, not measured bounds. Aircraft and meteor heads provide other calculated Doppler scales near 10 Hz but have much shorter estimated delays. This preset resembles an F.1487 low-latitude disturbed-skywave condition; the comparison does not establish exclusivity or a measured groundwave equivalent. |
| HF worst (30 Hz; 7 ms) | 7 ms corresponds to about 2,100 km excess path. Ionosphere-supported ground backscatter is one candidate on a short direct-path link, but no matched field measurement fixes its delay, level or spread; the estimated ~1 Hz spread does not exclude other components or geometries. At 30–50 MHz, tens-of-Hz scales are separately associated with evening trans-equatorial propagation, auroral scatter, equatorial flutter, aircraft and meteor heads. They may arise as wanted or unwanted independent arrivals depending on path and transmitter geometry; they do not prove that any one component is the exclusive source of this preset combination. |
| Aurora best (10 Hz; 3 ms), Aurora worst (55 Hz; 11 ms) | Quantitative auroral scatter at 30–50 MHz was not found in sources read. The 3 ms and 11 ms preset delays can be contrasted with estimated Es and F2 backscatter path delays, but these are scenario calculations, not observed limits. 55 Hz is above the simple aircraft Doppler estimate (±50 Hz at 250 m/s) and the cited evening-TEP spreads (sometimes >40 Hz); neither is an absolute physical bound, and different transmitter/receiver/scatterer geometry can change them. No reviewed measurement demonstrates these exact joint delay/Doppler combinations on a wanted 30 MHz ground path. |

Three structural differences matter more than any single preset value. Calculations indicate that meteor and aircraft components can exceed a 180 dB groundwave for a burst or pass in selected geometries; this is not a measured joint distribution. Sporadic-E and F2 signals at 30 MHz are commonly co-channel arrivals from other transmitters, which no preset represents. The presets also omit the large, poorly measured level trajectory of the wanted path. The literature reviewed here does not provide a 30 MHz upper bound on its delay or Doppler.

**Level and noise.** With the GUI's default −164 dBm/Hz, the rows' "4 W, 180 dB" gives C/N0 = 36 − 180 + 164 = 20 dB-Hz. Where external noise dominates, a real 30 MHz link with an ideal short monopole transmitting gives the same C/N0 when Lb + F_a − 13.5 dB = 180 dB (00 §3.1). That is Lb ≈ 175 dB at the galactic-noise floor (F_a ≈ 18 dB), about 167 dB with rural man-made noise and about 158 dB in a city ([90](90-cross-check-calculations.md) §1.5). In the P.368 convention of table A, Lb = 175 dB is reached with ground-level antennas at about 260 km over sea, 80–100 km over smooth land and 46 km over very dry ground [E]. Those are about 20 km (sea) and 10–15 km (land) shorter than the 180 dB distances used elsewhere in this chapter, which equate the GUI's loss with Lb. The −164 dBm/Hz default (F_a = 10 dB) is 8 dB below galactic noise at 30 MHz. The equivalence fails where an inefficient receiving antenna brings the external noise below the receiver's own noise; the receiving-antenna loss then counts in full, as in the −25 dBi row of table F.

## Contradictions, open questions, and things not found

**Disagreements and tensions**

1. *Sea state at upper HF.* Barrick's journal abstract places the maximum excess loss at 10–15 MHz; his report's 30 MHz curves show 8–15 dB even at 10–100 km for sea states 5–6. Hansen's one 235 km path showed sub-20-MHz variations compatible with the rough-sea model, but the study inferred equivalent wind from the 10–18 MHz received-level record using Barrick's model, then used that inferred wind to correct the same path's levels; this is not an independent sea-state validation. At 26 MHz the trend was opposite and at 32 MHz the received signal reached +25 dB over smooth-earth theory, attributed to atmospheric ducting. Kirincich & Emery (2023) found that simple analytical models matched an 8-month record better than ground-wave models and blamed, in part, differences between wave-spectrum-based impedance models. The theory is at its validity limit at 30 MHz, and Barrick's 30 MHz numbers should not be treated as better than ±5 dB.
2. *Conductivity and radar working range are not the same observable.* Kirincich & Emery's full text reports a single 16.188 MHz tone on a Martha's Vineyard coastal transmitter-to-receiver link, with 8 months of relative received-power variation at about 7 km (high receiver, direct visibility) and 45 km (near sea level, below transmitter horizon). The 45 km time series did not show a detectable seasonal received-power response to the measured 4.5→2.5 S/m conductivity change; simple wind-driven rough-impedance models performed as well as or better than more elaborate alternatives there. This is not a 30 MHz measurement and is not an absolute loss validation. In contrast, the 25 MHz Westshore CODAR study's 1.6-year *working range* depended strongly on conductivity (observed range increases from 19.4 to 37.4 km over 0.9 to 3.5 S/m; a model predicted 17→37.5 km over 1.3→3.5 S/m), but working range is the farthest usable radial-velocity solution from a single station, requiring at least 6 dB first-order-Bragg-line SNR, not one-way received power. Its possible range was capped at 39.5 km; every record reaching that cap was excluded whether the return was spurious or real (§3b, §4b). This removes/censors the upper-range tail of the relation and can bias high-conductivity range low. The study also found sea-state effects can raise or lower working range: sea state increases both propagation loss and the Bragg scattering target strength, and fetch, wave direction, receiver noise, tide and shallow freshwater stratification affect the result. These results are not contradictory measurements of the same transfer function. Smooth-water 30 MHz calculations give a 3–4 dB difference between 4 and 5 S/m at 100–300 km [E]; neither coastal study validates that 30 MHz value.
3. *Polarisation in forest.* Dence & Tamir predict vertical preferable for low antennas; Jansky & Bailey measured horizontal up to 20 dB better at 25 MHz with low antennas in Thai forest. P.833's attenuation curves side with the measurements (vertical attenuates more below 1 GHz).
4. *Forest effect at 30 MHz.* Hill's calculation finds forest cover minor at 20–30 MHz on a long irregular path; Jansky & Bailey measure 10–24 dB. Different geometries (antennas outside versus inside the forest), but Hill had no data.
5. *Smooth-earth versus measurement at 28 MHz.* Adams et al.'s lowland medians are 10–20 dB below zero-height P.368-convention losses at 10 km. Largely resolved: the report's own smooth-earth curves lie 6–10 dB below P.368-convention values for the same ground constants, consistent with the 6 dB convention difference plus model detail, and the remainder is terrain, which the report's integral-equation prediction reproduces (see C). The transmitter-site elevation above the path terrain was not checked.
6. *P.368 atmosphere.* The 2022 revision changed from an exponential to a linear refractivity profile and from published curves to software. No published comparison of the two at 30 MHz was found.
7. *Loss conventions.* The approximately 6 dB difference in the corresponding low-terminal plane example, and its failure as a universal terrain/height correction, is a standing trap; the ITU handbook itself warns that the transmission-loss concept "may lead to confusion" over ground ([R3] §4, p. 10). Adams et al.'s "path loss" falls on the Barrick side in practice (see C), and the GUI's "path loss" at −164 dBm/Hz is a third quantity (see "Comparison with the modem's presets").

**Looked for and not found (gaps, not negatives)**

- Hansen (1977): the publisher abstract and a complete text-rendered copy of the article at Academia.edu were retrieved and read on 2026-11-13. The read copy is a secondary-hosted transcription, not claimed as an author-hosted or publisher full-text file; the AGU publisher record corroborates its abstract. The article documents stepped-frequency 4–32 MHz sounding over one 235 km Point Loma–Point Mugu ocean path (36 month-mean samples in Figure 2, twice-daily runs May 29–June 26, plus one hourly June 27–28 run), with identical approximately 40-foot monopoles and ground screens. TX was about 97 ft above sea level beside a bluff; RX was about 10 ft above sea level at the beach. Sounder samples were retained only when (S+N)/N exceeded 20 dB; the received-power calibration was better than ±0.75 dB, but unknown uncertainty in the estimated transmitting-site bluff gain meant the article could not assign overall loss uncertainty. It reports 32 MHz episodes up to 25 dB above smooth-earth prediction, attributed to ducting. The article further shows that the sub-20 MHz sea-state comparison uses an equivalent wind inferred from those same received levels via Barrick's model; comparison against independently recorded winds was spatially inconsistent. This is a useful upper-HF anomalous received-level event, not an independent validation of absolute path loss or a general sea-state law. Raw complex records, refractivity profiles and matched duct statistics remain unavailable in the inspected sources.
- Forget, Broche & de Maistre (1982) and Barrick (1971, parts 1 and 2): publisher abstracts and citations in the retrieved literature were consulted; no full text was obtained. Their result limits are not silently promoted to numerical 30 MHz observations: the rough-sea calculations and abstract-level summaries remain distinguished from the Hansen event record and from this chapter's computed transfer curves.
- Kirincich & Emery (2023) full text was obtained and read through the publisher's article link listed in [R22]. It reports 16.188 MHz, two receiver geometries at about 7 and 45 km, relative one-way observations, and model comparisons; it is not 30 MHz evidence. Full-text review resolves the former abstract-only description and the exact tone/frequency gap. The Dagefu et al. (2015) TAP and Radio Science papers are distinct publications, both on short-range lower-VHF clutter/near-ground links; neither is an over-horizon 30 MHz test.
- Halverson et al. (2017), the 25 MHz Westshore CODAR study, was read from the author-hosted full-text PDF. It analyzes 1.6 years of one-station radial-current working-range data, not detailed path-loss/SNR variability. The 300 kHz system uses 0.5 km range cells and a 6 dB first-order Bragg-line SNR threshold, with a maximum configured range of 39.5 km. The authors exclude all records reaching that cap, whether spurious or real, and warn that farthest-bin radial solutions beyond 37 km are often spurious because first- and second-order lines can blend. Thus the reported 19.4–37.4 km conductivity relation is conditioned on valid, sub-cap detections and its upper-range tail is truncated; excluding both real and spurious maximum-range cases may bias the high-conductivity relation downward. Conductivity is based on an instrumented ferry at nominal 2 m depth while the EM skin depth is only centimetres; the authors warn that plume stratification can make the sensed surface conductivity lower. There is no direct wave measurement in the radar footprint; wind at a buoy 50 km away is a proxy, with fetch/direction and wave-spectrum effects unresolved. This is evidence about radar observability and environmental covariates, not a direct one-way 30 MHz modem channel.
- NTIA/ITS lists 1963–64 Phase 1 irregular-terrain data for Colorado plains, Colorado mountains, and Ohio hills. The official Part 1 report (plains) was fetched by the parent from `https://its.ntia.gov/publications/download/IER_38-ITSA_38.pdf` to `/tmp/fable-IER_38-ITSA_38.pdf` and extracted with `pdftotext` to `/tmp/fable-IER_38-ITSA_38.txt`; I inspected the methods and limitations in that extracted text on this pass. It covers 20.084/49.72 MHz Colorado CW tests (plus 100 MHz), largely 1963–64 with additions to June 1965. For those two frequencies the transmitter was vertical at 3.3/4 m; the receiving vertical whips were 1.3 m at 20 MHz and 0.55/1.7 m at 50 MHz. Principal road sites were selected at 5, 10, 20, 30, 50 and 80 km; ≤50 km readings usually lasted seconds because short-term change was <1 dB, while 80 km values were averaged for minutes because variations reached 5 dB. The report warns that 20/50 MHz antenna gains can only be regarded accurate within ±3 dB: vehicle/ground/nearby surroundings alter antennas, and the 20/50 antenna gain measurement method was difficult. Some 20/50 measurements also reused sites selected for 100 MHz improvement searches, so site selection is not an unbiased random sample of every terrain geometry. These are terrain spot measurements, not a surface-wave-only or path-resolved multipath dataset. The subsequent completion pass downloaded and parsed the official Phase 1 ZIP and read Parts 2/3; the new analysis below supersedes that earlier access gap. The earlier section's 20/50 MHz medians and spreads come from Longley–Reasoner [R26]; the separately labeled completion table below is a fresh archive analysis, retaining its own inclusion/censoring rules.
- Rogers et al. (2016), *Ground Wave Propagation Measurement in the HF to VHF Frequency Ranges over Non-Line-of-Sight Terrains*, was recovered as a National Academies-hosted two-page proceedings paper/abstract. It describes a 30–100 MHz campaign at hundreds of metres to kilometres, with whip, monopole, folded conical helix and optimized wire antennas; terrain profiles and soil parameters were measured, with received power from a spectrum analyzer. The available proceedings text does not expose tabulated 30 MHz outcomes, complex impulse responses, a demonstrated propagation mechanism, or matched input files. It is evidence that directly relevant measurements exist and a route to pursue the full paper/data, but cannot yet set a loss law or prove that each NLOS result was a surface wave rather than diffraction, reflection, or mixed paths.
- Rotheram (1981) Part 1 and the GRWAVE documentation; Sevgi's HF surface-wave-radar propagation papers beyond one overview abstract; any parabolic-equation or FDTD validation at 20–50 MHz from 2015–2026 (none found; the newest measurement comparison of a terrain code at these frequencies obtained is the 1979 WAGNER study [R50]).
- Liao & Sarabandi (2005, 2008) and Tamir (1977) remain unselected alternatives. Casciato, Oveisgharan & Sarabandi (2003) was obtained in full; its idealized 30 MHz coastal geometry and limits are recorded below.
- Low-VHF urban, suburban and forest delay-spread measurements: Dagefu et al.'s full-text 40 MHz study reports near-scalar transfer and little/no multipath in its specific ≤200 m indoor/outdoor scenarios; further evidence in 30–88 MHz was not located. It does not validate over-horizon groundwave delay. Nothing on building penetration loss at 30 MHz: ITU-R P.2109-2 covers 80 MHz–100 GHz only ([R54], read).
- Mountain obstacle-gain measurements at low VHF; only the ITM remark.
- Any measured Doppler spectrum or phase-stability record of a 20–50 MHz groundwave link; any measurement of vegetation-and-wind fading at 30 MHz (P.833's wind data are at much higher frequencies). Bistatic HF radar papers found give Bragg-peak positions and levels relative to the spectral continuum, not to the direct path; an open-archive copy of a bistatic Doppler-spectra simulation-versus-experiment study (HAL hal-00905980) is behind bot protection and was not obtained.
- Amateur 10 m and 6 m ground-wave or troposcatter records and levels: not found (the records located concern skywave paths or 144 MHz ducting). One-way loss budgets for 25–45 MHz surface-wave-radar paths beyond the 7/45 km 16.188 MHz Kirincich–Emery study: not found; that study is a coastal HF case and does not measure a 30 MHz path.
- Measured ionospheric ground-backscatter at 25–50 MHz: the completion search found and read Huyghebaert et al. 2022 at 32.55 MHz, including its processing/censoring and mode-separation evidence. It does not calibrate a direct-groundwave-relative 30 MHz level; see below. The former “not searched” status is superseded.
- Meteor-burst rates and levels measured on 100–300 km paths: Weitzen (1987) treats ranges below 400 km [R51], abstract only and without numbers; COMET and SNOTEL records were not obtained.
- Round-the-world echoes at 28–30 MHz (a delay near 138 ms follows from the circumference at F-region height [E]) remain unselected contextual records. Quantitative 32.55 MHz auroral-region propagation evidence is now supplied by Huyghebaert et al. 2022 below, with its distinct radar observable.
- Search/access record checked 2026-11-13 (queries retained to make the search boundary reproducible): (1) `"Dependence of 25-MHz HF Radar Working Range on Near-Surface Conductivity"` — publisher record returned 403, but the author-hosted PDF `2017_Halverson_JAOT.pdf` was retrieved at CODAR and read; (2) `Ground Wave Propagation Measurement in the HF to VHF Frequency Ranges over Non-Line-of-Sight Terrains Rogers` — National Academies-hosted two-page proceedings text retrieved, full supporting paper/data not recovered; (3) `NTIA 1984 path search` — NTIA-84-151 OCR PDF accessible; (4) `Hansen 1977 Measurements of basic transmission loss for HF ground wave propagation over seawater pdf 235 km` — publisher abstract retrieved, and the complete readable text-rendered article copy was subsequently opened/read at Academia.edu; (5) `Forget Broche de Maistre 1982 attenuation distance wind speed HF surface waves ocean pdf` — retrieved citations/abstract-level pointers only; (6) `Rogers Vogel Lacker Ling 2016 NLOS 30-100 MHz propagation measurements full paper` — no full paper beyond the proceedings text. The official NTIA legacy data listing and Part 1 report page were then opened. The web research browser rejected the Phase 1 zip as unsupported `application/x-zip-compressed` and the Part 1 25.8 MB PDF as too large; on the follow-up, the parent retrieved the official report to `/tmp/fable-IER_38-ITSA_38.pdf` and ran `pdftotext` to `/tmp/fable-IER_38-ITSA_38.txt`, which supplied the report methods/limits summarized above. That initial route limitation was later remedied by public-file downloads and local archive/PDF inspection; see the completion analysis below. These are route-specific access outcomes, not universal unavailability. Existing focused bibliography and prior source record remain material; this search was a bounded follow-up, not a claim of exhaustive global absence.
- The above 2015–2026 primary sources include Dagefu et al. (2015, distinct 40 MHz TAP and Radio Science short-range work), Halverson et al. (2017, 25 MHz radar working range), Kirincich & Emery (2023, full text at 16.188 MHz), Rogers et al. (2016 proceedings at 30–100 MHz), and Deacon et al. (2021, 2022). They do not supply a matched 30 MHz long-path modem measurement with path-resolved wanted and competing arrivals. The ITU listing also shows a 2026 revision of P.372 (radio noise), relevant to [50](50-noise-and-interference-mf-hf.md); its current P.372-18 text/code status and noise interpretation are owned by chapter 50 and are not a new blanket access gap here.

**Open questions that matter to the modem**

- Is there any real 30 MHz link at 180 dB on which the groundwave is the strongest component most of the time? This chapter's estimates put meteor, aircraft and (when the band is open) backscatter components within about ±10 dB of it. On re-check the meteor level agrees with ITU-R P.843-1 Fig. 3 to 2–3 dB, and meteor-burst connectivity below 400 km is reported to be maintainable [R51]. The aircraft level rests on assumed cross-sections of 100–1,000 m², and the only low-VHF measurement found (2–4 dB flutter at 60.75 MHz [R52]) has no stated geometry. The backscatter level is an unmeasured bound. The balance also depends on the path: on a 280 km sea path an aircraft at 10 km altitude gives about 189–199 dB [E], 10–20 dB below the groundwave, while q = 10¹⁴ m⁻¹ meteor echoes arrive about 12 dB above it [E]. The conclusion remains derived, and it is the single most consequential unverified statement in this chapter.
- How large is the sea-clutter pedestal on a forward path?
- How often do HF-trapping ducts occur outside southern California?

## Completion analysis of accessible groundwave evidence

### Official NTIA Phase 1: executed data inspection and bounded analysis

The [official ZIP](https://its.ntia.gov/media/66293/phase_1.zip) was downloaded
and parsed on 13 November 2026: **683,228 bytes**, SHA-256
`638d501cc571b21121e608b2ee745bf11b57dc246d4c9e0887581b9e34984d99`.
It contains 506 entries, 2,703,577 uncompressed bytes: measurement-parameter,
terminal-coordinate and excess-loss tables, plus mapped terrain profiles.
[Part II](https://its.ntia.gov/publications/download/IER_38-ITSA_38-2.pdf)
(5,461,300 bytes; SHA-256
`18974bbdd2668220fc320934d199b70533618f5a9c25cf299d6dc599c0ebcc56`)
and [Part III](https://its.ntia.gov/publications/download/IER_38-ITSA_38-3.pdf)
(24,584,659 bytes; SHA-256
`c015b3c3b2a17268de185b2857a5dfc7a4e2d50304fdfb380b4ef619798e7278`)
were read for methods, parameter tables, record format, calibration and sample
site records. These are actual downloaded primary bodies, not just catalogue
entries. PDF text was extracted with `pdftotext -layout`; tables were parsed
from the ASCII ZIP files rather than noisy PDF OCR.

The compact manifest is `phase_1.txt` (method/readme),
`phase_1/{pl,mt,oh}pars.txt` (system parameters), `*path.txt` (terminal
coordinates/distances), `*data.txt` (spot observations), `plxdata.txt` and
`mtxdata.txt` (corrected cross-polarization), and the three `*_pfl/` directories
(mapped terrain profiles). The README campaigns are plains autumn1963–summer1964,
mountains summer1963–autumn1964 and Ohio summer1963–summer1964. Individual
observation dates/comments are in the report site pages, not the ASCII loss
columns; no environmental time series was inferred from those campaign labels.
The present analysis is a descriptive observation audit, not a per-site
atmosphere/soil fit. Those additional inputs would be prerequisites for such a fit.

The archive's README defines attenuation **relative to free space**, in dB.
A value `500+q` means attenuation exceeds sensitivity threshold q; it is not
an observed fade depth. Although the README describes missing cells as blank,
the actual tables contain **999.0**. These are retained as missing, separately
from `500≤a<999` censored observations: the printed Ohio tables distinguish
unavailable `**` from unmeasurable-signal indications, and interpreting 999 as
499 dB observable attenuation would contradict the system's maximum loss.
Principal `P` sites and 100 MHz-selected alternate `V/H` sites are separate;
alternates are nearby optimized sites, not independent random terrain samples.
The corrected cross-polarization files are not pooled into the vertical data.

| Dataset / selected vertical column | Exact frequency; TX/RX height | Principal valid / censored / missing | Valid-only q10 / median / q90 excess loss, dB |
| --- | --- | --- | --- |
| Colorado plains A20 | 20.084 MHz; 3.3/1.3 m | 133 / 0 / 52 | 34.46 / 48.60 / 59.78 |
| Colorado plains A50-1 | 49.72 MHz; 4/.55 m | 133 / 0 / 52 | 38.62 / 56.70 / 66.80 |
| Colorado mountains A20 | 20.084 MHz; 3.3/1.3 m | 41 / 0 / 7 | 44.40 / 64.70 / 76.60 |
| Colorado mountains A50-1 | 49.72 MHz; 4/.55 m | 41 / 0 / 7 | 41.70 / 68.40 / 76.00 |
| Ohio hills A20 | 19.97 MHz; 3.7/1.3 m, with TX exceptions below | 210 / 10 / 26 | 41.38 / 49.60 / 58.90 |
| Ohio hills A50-1 | 49.72 MHz; 4.2/.6 m, with TX exceptions below | 183 / 63 / 0 | 36.00 / 44.60 / 51.26 |

These are **new descriptive statistics [E from T]**, not the older R26
Longley–Reasoner summary. Counts across all P/V/H records are 449 plains, 138
mountains and 598 Ohio, of which 185/48/246 are principal. The quantiles use
linear interpolation at `(n−1)p` after excluding missing and censored values;
they describe detected records, not the full attenuation distribution.
Especially in Ohio, deleting 63 censored A50-1 observations can shift a
high-loss quantile substantially. No Gaussian tail, confidence interval,
outage probability or geographic ranking is inferred from this table.

The following checks make the analysis reproducible with standard Python 3
(no numerical package is required). `archive` is the downloaded ZIP path.

```python
import zipfile
z = zipfile.ZipFile(archive)
def rows(name):
    return [line.split() for line in z.read(name).decode('latin1').splitlines()
            if line and line[0].isalnum() and not line.startswith('Path')]
def quantile(a, p):
    a = sorted(a); x = (len(a)-1)*p; i = int(x)
    return a[i] + (a[min(i+1, len(a)-1)]-a[i])*(x-i)
for region in ('pl', 'mt', 'oh'):
    r = rows('phase_1/' + region + 'data.txt')
    principal = [v for v in r if v[0].split('-')[-1].startswith('P')]
    start = 2 if region == 'oh' else 1  # Ohio has a transmitter column
    for column in (start, start+1):
        a = [float(v[column]) for v in principal]
        valid = [v for v in a if v < 500]
        print(region, column, len(valid), sum(500 <= v < 999 for v in a),
              a.count(999.), [quantile(valid, p) for p in (.1, .5, .9)])
```

A matched-cell unit check uses `PL-05-P1`, distance 4.96 km: A20=33.0 dB and
A50-1=51.1 dB. The reports' `Lbf=32.45+20log10(f_MHz*d_km)` conversion gives
**Lb=105.41664 and 131.39026 dB**, respectively. This verifies the observable
and parsing convention; it is not a smooth-earth prediction. The terrain,
antenna, environment and branch differ between bands, so interpolating these
values to 30 MHz would not constitute measurement validation.

Distance stratification exposes why the pooled medians are inadequate. For
Ohio principal paths of 35–52 km, A20 has **62 detections, 7 censored and 17
missing**, detected median **52.75 dB**; A50-1 has **34 detections, 52 censored**,
detected median **45.20 dB**. The apparently smaller 50 MHz detected median
therefore cannot establish lower 50 MHz total-path loss. Nominal site labels
are also not exact distances: use the PATH table, retaining its discrepancies.
`PL-80-P58` has a zero distance placeholder and missing 20/50 measurements;
`MT-05-P13` has PATH distance 5.08 km but profile extent 4.97 km. Such entries
must be reconciled with the original site page before a terrain-model fit,
not silently resized or counted as zero-distance links.

**Methods that materially change a future model comparison:** Part II pp.3–5
specifies carrier input power, antenna/line corrections and loss definitions.
Part III Table 1 gives actual Ohio 19.97/49.72 MHz; the ASCII PARS rounds these
to 20.0/49.7. Its central-transmitter 10 km sites 1–17,22 and 20 km sites
1–5,7,9,10 used lower TX heights (about 2.62/2.74 m, printed site values), so the
global PARS heights do not suffice. Part III pp.9–10 says Ohio frequencies were
measured from one vehicle within a short interval, unlike many Colorado
20/50 versus 100 MHz comparisons; antenna gains remain about ±3 dB uncertain.
Part III pp.16–19 ties each site to date, operator comments, photograph and
profile, and warns that unmatched antenna impedance/environment cannot be
separated from apparent terrain loss. Profiles were mapped later from older
maps; they are not modern DEMs or complex temporal recordings.

Consequently this dataset closes the **accessible data inspection** gap and
supplies numerical terrain spot observables. It does not close exact 30 MHz,
complex phase, environmental time-history or modem-outcome gaps. A future
same-geometry terrain comparison should hold out entire principal site groups,
use per-site report inputs, retain nondetections as inequalities, and compare
within frequency. No ITM/PE/WAGNER fit or physical validation was executed here.

### Resolved coastline and competing-mode source questions

[Casciato, Oveisgharan and Sarabandi (2003)](https://doi.org/10.1029/2002RS002696),
§§2.3–3, Figs.3–6, supplies an explicit 30 MHz idealized land/sea boundary
comparison. A vertical dipole at x=−100λ, observation x=−50…150λ and equal
heights 1/5/10λ demonstrates differences between direct-plus-reflected and total
fields. The method combines a perturbation impedance transition with cliff
UTD; it assumes a locally distant source and has perturbation/boundary limits.
An abrupt reflected-field discontinuity is an approximation artifact, discussed
by the authors, not a physical fade to insert into a modem. Their impedance
choices do not excite the trapped pole they call a surface wave; the Norton
term remains relevant. This supports a resolved coast/height alternative to
Millington, not empirical accuracy or a bound on all real coastal fading.
The adjacent Astra-6 bibliography's author names were incorrect; this is the
verified DOI identity.

[Huyghebaert et al. (2022)](https://doi.org/10.3389/fspas.2022.886037),
Experiment/Table1 and Discussion I–IV, directly observes multiple propagation
modes at **32.55 MHz** in northern Norway on **27 August 2021, 16:30–20:00 UTC**:
meteor, short direct feedthrough, OTH link, ground/sea scatter and ionospheric
scatter. It uses 100 kHz coded bandwidth, 10 ms repeated codes, 200 ms
integrations and 2 s averaged products. Data storage selects SNR above about
3 dB; later analysis applies 6 dB and other clutter/ambiguity filters.
Ground/sea scatter is a narrow near-zero-Doppler population; direct feedthrough
is absent on two of the three examined links. Strong overlapping targets and
wide spectra compromise clutter subtraction. This is valuable nearby-frequency,
active-auroral evidence that competitors coexist; it is not a 30 MHz
surface-path-normalized amplitude law, an uncensored Doppler population, or
proof that ±1 Hz covers all competitors. “Backscatter not searched” is now obsolete.

The associated [RADAR dataset, DOI10.22000/549](https://doi.org/10.22000/549)
is public CC BY4.0, published 3 May 2022. Its metadata reports 3,087,899,648-byte
tar and archive MD5 `26b09023c7cf1479e16278c8aab9403a`. A bounded HTTP range
inspection actually read the archive metadata and identified the payload
`data/dataset/simone_norway_eregion_paper_data.zip` (3,087,878,390 bytes).
The 128 KiB request stalled after 13,230 bytes and timed out at 60 s; a distinct
40 KiB tail request failed connection. **No radar payload reanalysis is claimed.**
The repository metadata does not supply raw-voltage calibration or complete
software details. Published selected radar products cannot be silently treated
as uncensored weak-signal captures. The source is available; this route's
payload transfer, and a faithful selected-product reproduction, remain a
precise external-access/data-processing dependency if that replay is selected.

### Rough sea: what is actually supported

Barrick1970 [R18], §§III–IV and Figs.15/31, is the read primary mathematical
basis despite the unavailable 1971 journal parts. Its **mean effective complex
impedance** adds a roughness term derived from a surface-height spectrum to
the smooth-sea impedance, then uses a spherical-earth field calculation. Its
Phillips isotropic and Neumann–Pierson directional branches assume developed
wind seas and a perturbative roughness domain. The coherent mean field is
separate from the time-varying incoherent scattered field; the report explicitly
defers the latter's power spectra to later work. Therefore its mean-loss
curves cannot select a forward-path Rician K, time covariance, or a phase-noise
PSD. Option B's K=25–35 dB is **only an engineering sensitivity parameter**.

For physically conditioned research, retain three distinct comparisons:
(1) smooth sea with measured/stated conductivity and antenna height;
(2) the read mean-impedance theory with its named spectrum, direction, wind-sea
assumptions and small-roughness qualification; (3) a measured complex channel
or a separately justified scattering theory for fluctuations. The existing
16.188 MHz one-way and 25 MHz radar studies constrain transfer and confounders;
they do not collapse these three into a universal 30 MHz sea-state process.
Attributions of 32 MHz enhancement to ducting remain episode-specific and must
not be folded into a positive or negative roughness correction.

### Complete disposition of named upper-HF gaps (GW07–GW16)

| Named source or phenomenon | Disposition and consuming conclusion |
| --- | --- |
| NTIA Phase1 ZIP; Parts2/3 | Retrieved, parsed/read and descriptively analyzed above. Exact 30 MHz temporal/complex calibration remains outside their observable. GW07 data inspection completed. |
| Rogers2016 NLOS campaign | Two-page primary proceedings read; exact-title/author searches again found no fuller numerical report/data. The description establishes a campaign, not a 30 MHz loss/phase distribution. No scenario parameter depends on unavailable data. |
| Hansen1977; Kirincich–Emery2023; Halverson2017; both Dagefu2015 papers | Full texts already read; geometry, calibration, sensitivity censoring and radar-versus-one-way limitations retained in sections B/C. Do not count repeated access as new literature. |
| Forget–Broche–de Maistre1982 | Publisher abstract only after DOI/title/author routes. No numerical parameter from its unrecovered body. Nearby-frequency mean-loss discussion remains subordinate to read R18/R22. |
| Barrick1971 Parts1/2 | Abstracts; historical local files named as PDFs are only 301-byte failed responses, not full texts. Read1970 author report supplies the selected mean-impedance theory. No missing fluctuation statistic is invented. |
| Rotheram1981 Part1/2; GRWAVE documentation | Original bodies not recovered; current P.368-10/NTIA LFMF supplies the executed baseline. Exponential-atmosphere GRWAVE remains an alternative, not claimed numerical parity. |
| Sevgi overview and subsequent HF radar PE/FDTD; Apaydın–Sevgi; 2015–2026 20–50 MHz field validation | Bibliographic alternatives, not chosen as a qualified terrain solver. Read NTIA integral-equation reports supply the actual measurement comparisons. Canonical-code agreement is distinct from field accuracy. |
| Liao–Sarabandi2005/2008; Tamir1977 | Unselected alternatives, no unique coefficient imported. Read Hill82-114 and Jansky–Bailey supply explicit forest theory/measurement limits. |
| Casciato/Sarabandi coastline family | Specific2003 full paper identified/read above; correct authors and approximation limits replace the former blanket “not located.” |
| Weitzen1987; COMET; SNOTEL | Weitzen remains abstract-only. Targeted COMET search found official NTIA89-241's 35–40 MHz system-history discussion, not a matched 100–300 km 30 MHz event-rate/level dataset. P.843 and read Haakinson model remain the selected theoretical alternatives; no operational-network availability is transferred to the modem. |
| Aircraft/flutter and assumed RCS | The60.75MHz abstracts and bistatic-equation estimates retain their frequency/geometry limits. No surveyed flight/RCS-conditioned30MHz capture found; event incidence remains external, not zero. |
| 25–50MHz ionospheric ground/sea backscatter and auroral modes | New32.55MHz primary evidence above establishes mechanisms and processing limits; direct-path-relative calibration and general probabilities remain unmeasured. |
| 28–30MHz round-the-world echoes | Search found amateur publications/discussion, not a selected calibrated complex path corpus. Circumference/c delay is geometric context; no long-echo population or delay bound is adopted. |
| Mountain obstacle gain; 30–88MHz long-path urban/forest delay; 30MHz wind/vegetation fading; building entry | No qualifying applicable field distribution in inspected routes. Terrain/ray controls remain conditional; P.2109's80MHz lower bound prohibits using it as a30MHz building-loss law. |
| 10m/6m amateur ground/troposcatter; one-way25–45MHz radar budgets; geographic duct incidence | No calibrated matched record recovered. QSO success/radar reach cannot identify a mode, phase PSD or service probability. |
| HAL00905980 bistatic spectra | Access-limited lead, not a selected numerical input; Bragg/scattered spectra do not by themselves normalize power to a forward direct path. |
| P.372 old-edition lead | Reconciled to current shared chapter50's P.372-18 review; no independent “unopened radio-noise” blocker. |
| Classical Sommerfeld/Norton/Bremmer/Wait/Millington/Hufford/Ott | Selected equations use reviewed ITU/NTIA synthesis and pinned implementation; unread originals have no unique unverified implementation obligation. See20's individual disposition. |

The added search strings included `25 50 MHz ground backscatter measured`,
`Rogers Vogel Lacker 2016 ground wave propagation pdf`,
`COMET meteor 40 MHz propagation measurements`,
`28 MHz round the world delay measurements`, exact Casciato/Forget/Hill/Wait
DOIs and titles, and recent near-band phase/terrain searches. No quota exhaustion
ended these searches. Coverage now includes US irregular terrain, Japanese MF
coast, Baltic/navigation context, Canadian plume, northeastern-US coastal sea
and active northern Norway. It does not establish tropical, southern-hemisphere,
polar-winter or arid long-path30MHz probabilities.

## References

Access status: **read** = supporting passage read in the full text (for scanned reports, via the OCR text and, where stated, the figure images); **abstract** = only an abstract or index record was seen (publisher abstracts via DOI metadata, re-read through the OpenAlex index); **secondary** = quoted through another source, which is named.

- [R1] ITU-R Rec. P.368-10 (08/2022), *Ground-wave propagation prediction method for frequencies between 10 kHz and 30 MHz*. https://www.itu.int/rec/R-REC-P.368/en — read. [S]
- [R2] ITU-R Rec. P.368-9 (02/2007, superseded), *Ground-wave propagation curves for frequencies between 10 kHz and 30 MHz*. Same landing page — read; Figs 2 and 6 digitised by eye. [S]
- [R3] ITU-R, *Handbook on Ground Wave Propagation*, edition 2014. https://www.itu.int/dms_pub/itu-r/opb/hdb/R-HDB-59-2014-PDF-E.pdf — read (§§1, 3–5, 8, 9, 11.2, 13, 14, 19, references). [S]/[B]
- [R4] ITU-R Rec. P.341-7 (08/2019), *The concept of transmission loss for radio links*, Annex 3. https://www.itu.int/rec/R-REC-P.341/en — read. [S]
- [R5] ITU-R Rec. P.526-16 (11/2025), *Propagation by diffraction*, §3. https://www.itu.int/rec/R-REC-P.526/en — read. [S]
- [R6] ITU-R Rec. P.1546-6 (08/2019), Annex 2. https://www.itu.int/rec/R-REC-P.1546/en — read. [S]
- [R7] ITU-R Rec. P.1812-8 (09/2025). https://www.itu.int/rec/R-REC-P.1812/en — scope only; listed for completeness, no value taken. [S]
- [R8] ITU-R Rec. P.2001-6 (09/2025), scope and §4.4. https://www.itu.int/rec/R-REC-P.2001/en — read in part. [S]
- [R9] ITU-R Rec. P.617-6 (09/2025), Annex 1 §§1, 4.1 and Attachment §2. https://www.itu.int/rec/R-REC-P.617/en — read. [S]
- [R10] ITU-R Rec. P.833-10 (09/2021), §2, Fig. 2. https://www.itu.int/rec/R-REC-P.833/en — read. [S]
- [R11] ITU-R Rec. P.534-6 (09/2021), Annex 1 §§1–4, Fig. 2. https://www.itu.int/rec/R-REC-P.534/en — read. [S]
- [R12] ITU-R Rec. P.843-1 (08/1997), *Communication by meteor-burst propagation*. https://www.itu.int/rec/R-REC-P.843/en — read in full. [S]
- [R13] ITU-R Rec. P.844-1 (08/1994), *Ionospheric factors affecting frequency sharing in the VHF and UHF bands (30 MHz–3 GHz)*. https://www.itu.int/rec/R-REC-P.844/en — read in full. [S]
- [R14] ITU-R, *Handbook: The Ionosphere and its Effects on Radiowave Propagation* (1998), ch. 8 and §6.5.4. https://www.itu.int/dms_pub/itu-r/opb/hdb/R-HDB-32-1998-PDF-E.pdf — read. [S]/[B]
- [R15] Hansen, P. (1977), Measurements of basic transmission loss for HF ground wave propagation over seawater, *Radio Science* 12(3), 397–404. https://doi.org/10.1029/RS012i003p00397; [publisher abstract](https://agupubs.onlinelibrary.wiley.com/doi/abs/10.1029/RS012i003p00397); [full text as displayed on Academia.edu](https://www.academia.edu/77456760/Measurements_of_basic_transmission_loss_for_HF_ground_wave_propagation_over_seawater) — article text read; publisher abstract cross-checked; the readable copy is secondary-hosted, and this record does not claim it is an author manuscript. Methods, calibration caveat and discussion are cited by page number. [P]
- [R16] Pappert, R. A., & Goodhart, C. L. (1979), A numerical study of tropospheric ducting at HF, *Radio Science* 14(5), 803–813. https://doi.org/10.1029/RS014i005p00803 — abstract. [P]
- [R17] Barrick, D. E. (1971), Theory of HF and VHF propagation across the rough sea, 1 and 2, *Radio Science* 6(5), 517–526 and 527–533. https://doi.org/10.1029/RS006i005p00517, https://doi.org/10.1029/RS006i005p00527 — abstracts. [P]
- [R18] Barrick, D. E. (1970), *Theory of Ground-Wave Propagation Across a Rough Sea at Dekameter Wavelengths*, Battelle Memorial Institute research report, AD 865840. https://archive.org/details/DTIC_AD0865840 — read (OCR text; Figs 8, 15, 31 from the scan). [T]
- [R19] Barrick, D. E. (1972), First-order theory and analysis of MF/HF/VHF scatter from the sea, *IEEE Trans. Antennas Propag.* 20(1), 2–10. https://doi.org/10.1109/TAP.1972.1140123 — abstract. [P]
- [R20] Crombie, D. D. (1955), Doppler spectrum of sea echo at 13.56 Mc/s, *Nature* 175, 681–682. https://doi.org/10.1038/175681a0 — abstract (bibliographic record only, no abstract text). [P]
- [R21] Forget, P., Broche, P., & de Maistre, J. C. (1982), Attenuation with distance and wind speed of HF surface waves over the ocean, *Radio Science* 17(3), 599–610. https://doi.org/10.1029/RS017i003p00599 — abstract. [P]
- [R22] Kirincich, A., & Emery, B. (2023), Revisiting HF ground wave propagation losses over the ocean: a comparison of long-term observations and models, *Radio Science* 58(4), e2022RS007550. https://doi.org/10.1029/2022RS007550; [publisher full text](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2022RS007550) — full text read, including exact 16.188 MHz operating tone, receiver heights/geometries, model comparisons and limits. [P]
- [R23] Hill, D. A. (1982), *HF Ground Wave Propagation over Forested and Built-up Terrain*, NTIA Report 82-114. https://its.ntia.gov/publications/details?pub=2107 — read. [T]
- [R24] Adams, J. E., Carroll, J. C., Costa, E. A., Ebaugh, D. R., Godwin, J. R., Haakinson, E. J., Layton, D. H., & Smith, D. (1984), *Measurements and Predictions of HF Ground Wave Radio Propagation over Irregular, Inhomogeneous Terrain*, NTIA Report 84-151. https://its.ntia.gov/publications/download/84-151_ocr.pdf — read (OCR text; Figs 47 and 50 from the scan). [T]
- [R25] Hufford, G. A., Longley, A. G., & Kissick, W. A. (1982), *A Guide to the Use of the ITS Irregular Terrain Model in the Area Prediction Mode*, NTIA Report 82-100. https://its.ntia.gov/publications/details?pub=2091 — read. [T]
- [R26] Longley, A. G., & Reasoner, R. K. (1970), *Comparison of Propagation Measurements with Predicted Values in the 20 to 10,000 MHz Range*, ESSA Tech. Rep. ERL 148-ITS 97. https://its.ntia.gov/publications/download/ERL148-ITS97.pdf — read (§§2.5–2.6; Figs 23, 25, 27 from the scan). [T]
- [R27] Johnson, M. E., Miles, M. J., McQuate, P. L., & Barsis, A. P. (1967), *Tabulations of VHF Propagation Data Obtained over Irregular Terrain at 20, 50, and 100 MHz*, ESSA Tech. Rep. IER 38-ITSA 38, Parts 1–3. [Part 1 PDF](https://its.ntia.gov/publications/download/IER_38-ITSA_38.pdf); [NTIA Part 1 record](https://its.ntia.gov/publications/details?pub=2004); [Part 2 record](https://its.ntia.gov/publications/details?pub=2005); [Part 3 record](https://its.ntia.gov/publications/details?pub=2006); [official Phase 1 data listing](https://its.ntia.gov/data/legacy-radio-propagation-data) and [direct Phase 1 ZIP](https://its.ntia.gov/media/66293/phase_1.zip). The official Part 1 report was retrieved by the parent to `/tmp/fable-IER_38-ITSA_38.pdf`, then extracted to `/tmp/fable-IER_38-ITSA_38.txt` with `pdftotext`; Part I methods/limits were read for this chapter. The completion pass retrieved the ZIP and Parts 2/3 and analyzed the principal-site 20/50 MHz columns; exact source hashes, parsing rules, results and calibration limits are below. The earlier section's 20/50 MHz medians come from [R26]; the separately labeled completion table is the fresh ASCII-data analysis. Neither is an exact 30 MHz complex-channel measurement. [T]
- [R28] Rice, P. L., Longley, A. G., Norton, K. A., & Barsis, A. P. (1967 revision), *Transmission Loss Predictions for Tropospheric Communication Circuits*, NBS Technical Note 101, vol. 1 §§2 and 10. https://nvlpubs.nist.gov/nistpubs/Legacy/TN/nbstechnicalnote101-1.pdf — read (text; Figs 10.2, 10.3 and 10.5 from the scan). [T]
- [R29] Egli, J. J. (1957), Radio propagation above 40 Mc over irregular terrain, *Proc. IRE* 45(10), 1383–1391. https://doi.org/10.1109/JRPROC.1957.278224 — read (scan at worldradiohistory.com). [P]
- [R30] Bullington, K. (1957), Radio propagation fundamentals, *Bell Syst. Tech. J.* 36(3), 593–626. https://archive.org/details/bstj36-3-593 — read (OCR). [P]
- [R31] Bullington, K. (1947), Radio propagation at frequencies above 30 megacycles, *Proc. IRE* 35(10), 1122–1136. Scan: https://www.worldradiohistory.com/Archive-IRE/40s/IRE-1947-10.pdf — abstract (fragment only; the scan's OCR is unusable). [P]
- [R32] Bailey, D. K., Bateman, R., & Kirby, R. C. (1955), Radio transmission at VHF by scattering and other processes in the lower ionosphere, *Proc. IRE* 43(10), 1181–1230. Scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1955-10.pdf — read (pp. 1181–1188, Table I). [P]
- [R33] Abel, W. G., deBettencourt, J. T., Chisholm, J. H., & Roche, J. F. (1955), Investigations of scattering and multipath properties of ionospheric propagation at radio frequencies exceeding the MUF, *Proc. IRE* 43(10), 1255–1268. Same scan — read (p. 1263 only). [P]
- [R34] Eshleman, V. R. (1957), On the wavelength dependence of the information capacity of meteor-burst propagation, *Proc. IRE* 45(12), 1710–1714. Scan: https://www.worldradiohistory.com/Archive-IRE/50s/IRE-1957-12.pdf — read (pp. 1713–1714). [P]
- [R35] Rotheram, S. (1981), Ground-wave propagation. Part 2: Theory for medium and long distances and reference propagation curves, *IEE Proc. F* 128(5), 285–295. https://doi.org/10.1049/ip-f-1.1981.0047 — abstract. [P]
- [R36] Hill, D. A., & Wait, J. R. (1980), Ground wave attenuation function for a spherical earth with arbitrary surface impedance, *Radio Science* 15(3), 637–643. https://doi.org/10.1029/RS015i003p00637 — abstract. [P]
- [R37] Tamir, T. (1967), On radio-wave propagation in forest environments, *IEEE Trans. Antennas Propag.* 15(6), 806–817. https://doi.org/10.1109/TAP.1967.1139054 — abstract. [P]
- [R38] Dence, D., & Tamir, T. (1969), Radio loss of lateral waves in forest environments, *Radio Science* 4(4), 307–318. https://doi.org/10.1029/RS004i004p00307 — abstract; parameter table via [R23] (secondary). [P]
- [R39] Sachs, D. L., & Wyatt, P. J. (1968), A conducting-slab model for electromagnetic propagation within a jungle medium, *Radio Science* 3(2), 125–134. https://doi.org/10.1002/rds196832125 — abstract. [P]
- [R40] Jansky & Bailey Engineering Dept., Atlantic Research Corp. (1966–67), *Tropical Propagation Research*, Final Report, Volume I, AD 660318. https://archive.org/details/DTIC_AD0660318 — read (OCR: §3 summary, §5.5). [T]
- [R41] Dagefu, F. T., Verma, G., Rao, C. R., Yu, P. L., Fink, J. R., Sadler, B. M., & Sarabandi, K. (2015), Short-range low-VHF channel characterization in cluttered environments, *IEEE Trans. Antennas Propag.* 63(6), 2719–2727. https://doi.org/10.1109/TAP.2015.2418346; author-posted full text at [ResearchGate](https://www.researchgate.net/publication/277605250_Short-Range_Low-VHF_Channel_Characterization_in_Cluttered_Environments) — read. Specific to near-ground 40 MHz and ≤200 m clutter geometries. [P]
- Dagefu, F. T., Choi, J., Sheikhsofla, M., Sadler, B. M., & Sarabandi, K. (2015), Performance assessment of lower VHF band for short-range communication and geolocation applications, *Radio Science* 50, 443–452. doi:10.1002/2014RS005601; [publisher full text](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2014RS005601) and [open manuscript PDF](https://deepblue.lib.umich.edu/bitstream/handle/2027.42/111943/rds20240.pdf?sequence=1) — read. Separate publication, also has a 40 MHz measurement system and short-range indoor/outdoor clutter test cases; not used as 30 MHz groundwave evidence. [P]
- [R42] Nielson, D. L., & Crochet, M. (1974), Ionospheric propagation of HF and VHF radio waves across the geomagnetic equator, *Rev. Geophys.* 12(4), 688–702. https://doi.org/10.1029/RG012i004p00688 — abstract. [P]
- [R43] Heron, M. L., & McNamara, L. F. (1979), Transequatorial VHF propagation through equatorial plasma bubbles, *Radio Science* 14(5), 897–910. https://doi.org/10.1029/RS014i005p00897 — abstract. [P]
- [R44] Haakinson, E. J. (1983), *Meteor Burst Communications Model*, NTIA Report 83-116. https://its.ntia.gov/publications/details?pub=2110 — read (sample output, §3.4). [T]
- [R45] Davis, G. W. L., Gladys, S. J., Lang, G. R., Luke, L. M., & Taylor, M. K. (1957), The Canadian JANET system, *Proc. IRE* 45(12), 1666–1678. Same scan as [R34] — read (p. 1667). [P]
- [R46] Wikipedia, "Coastal ocean dynamics applications radar" (citing a 1986 Coastal Engineering Research Center technical note for the band and a 2010 CODAR SeaSonde specification sheet for the ranges). https://en.wikipedia.org/wiki/Coastal_ocean_dynamics_applications_radar — read. [G]
- [R47] Headquarters, Department of the Army (5 August 2009), *FM 6-02.53 Tactical Radio Operations*, §4-3 to §4-6, Table 9-4 and Table H-1. Copy: https://archive.org/download/MManuals/Fm6-02-53TacticalRadioOperations.pdf — read. The Wikipedia article "SINCGARS" repeats §4-6 without citation. [T]
- [R48] NTIA/ITS, *LFMF* software repository, README. https://github.com/NTIA/LFMF — read. [G]
- [R49] Sevgi, L., Akleman, F., & Felsen, L. B. (2002), Groundwave propagation modeling: problem-matched analytical formulations and direct numerical techniques, *IEEE Antennas Propag. Mag.* 44(1), 55–75. https://doi.org/10.1109/74.997903 — abstract. [P]
- Also verified by abstract but used only as pointers to recent sporadic-E work: Deacon, C. J., Witvliet, B. A., Steendam, S. N., & Mitchell, C. N. (2021), *IEEE Trans. Antennas Propag.* 69(7), 4033–4048, https://doi.org/10.1109/TAP.2020.3044654 (50 MHz polarimetric and fading measurements of Es reflections; evidence of magneto-ionic double refraction); Deacon, C., Mitchell, C., & Watson, R. (2022), *Atmosphere* 13(6), 906, https://doi.org/10.3390/atmos13060906. [P]
- [R50] Ott, R. H., Vogler, L. E., & Hufford, G. A. (1979), *Ground Wave Propagation Over Irregular, Inhomogeneous Terrain: Comparisons of Calculations and Measurements*, NTIA Report 79-20. https://its.ntia.gov/publications/details?pub=1953 — read (abstract, path table, §4.3, via the OCR text). [T]
- [R51] Weitzen, J. A. (1987), Communicating via meteor burst at short ranges, *IEEE Trans. Commun.* 35(11), 1217–1221. https://doi.org/10.1109/TCOM.1987.1096702 — abstract. [P]
- [R52] Wonggeeratigun, A., Sangonchat, P., Noppanakeepong, S., Leelaruji, N., & Moriya, Y. (2003), The observation and simulation of the airplane flutter on low band television broadcasting signal, *Proc. Student Conf. on Research and Development (SCORED 2003)*, 118–122, https://doi.org/10.1109/SCORED.2003.1459677; and Wongkeeratikul, A., Supnithi, P., Noppanakeepong, S., Leelaruji, N., & Hemmakorn, N. (2008), Modeling and measurement of airplane flutter phenomena on TV broadcasting signal, *IEEE Trans. Broadcast.* 54(2), 173–181, https://doi.org/10.1109/TBC.2008.917051 — abstracts (60.75 MHz; fluctuation about 2–4 dB). [P]
- [R53] Lipa, B., Whelan, C., Rector, B., & Nyden, B. (2009), HF radar bistatic measurement of surface current velocities: drifter comparisons and radar consistency checks, *Remote Sensing* 1(4), 1190–1211. https://doi.org/10.3390/rs1041190 (open access; copy at https://www.codar.com/images/about/2009Lipa_Bistatic.pdf) — read (§§2, 3.2, 4). [P]
- [R54] ITU-R Rec. P.2109-2 (08/2023), *Prediction of building entry loss*. https://www.itu.int/rec/R-REC-P.2109/en — read (scope: 80 MHz to 100 GHz). [S]
- [R55] Halverson, M. J., Pawlowicz, R., & Chavanne, C. (2017), Dependence of 25-MHz HF Radar Working Range on Near-Surface Conductivity, Sea State, and Tides, *Journal of Atmospheric and Oceanic Technology* 34(2), 447–462, doi:10.1175/JTECH-D-16-0139.1. [Publisher record](https://journals.ametsoc.org/view/journals/atot/34/2/jtech-d-16-0139.1.xml) (403 in this access route); [author-hosted full text](https://www.codar.com/images/about/2017_Halverson_JAOT.pdf) — read, including study design, proxy/input limitations, conductivity and working-range results, and the authors' discussion distinguishing propagation loss from Bragg scattering strength. [P]
- [R56] Rogers, R. L., Vogel, W., Lacker, S., & Ling, H. (2016), Ground Wave Propagation Measurement in the HF to VHF Frequency Ranges over Non-Line-of-Sight Terrains, *2016 IEEE International Symposium on Antennas and Propagation / USNC-URSI National Radio Science Meeting*, proceedings contribution. [National Academies-hosted two-page text](https://sites.nationalacademies.org/cs/groups/pgasite/documents/webpage/pga_183636.pdf) — read only the available short proceedings text; full paper, numerical outcome tables and raw data not recovered. [P]
- Own calculations [E]: pure-Python implementation of the Sommerfeld–Norton and residue-series solutions and the auxiliary estimates. The working scripts were temporary and are not part of this record; an independently written residue-series code reproduces table A, the height-gain, frequency, k and mixed-path figures ([90](90-cross-check-calculations.md) §1, and the review re-computation).

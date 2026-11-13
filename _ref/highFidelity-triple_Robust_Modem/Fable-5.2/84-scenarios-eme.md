# EME 5.76 GHz scenario specification

- **Purpose/status.** Fixed mathematical research specification, not an empirically
  validated 5.76 GHz channel or a modem result. The baseline kernel, interpolation,
  polarization alternatives and severity controls are specified below. Better
  models may supersede them. No renderer or receiver trial is implemented here.
  Use the [common native source contract](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract);
  rounded waveform values below never replace its actual sample/slot geometry.
- **Scope.** Passive two-Earth-station Earth–Moon–Earth (EME), 5.760 GHz
  carrier, configured-calendar/keyed-ordinal receive. This is not lunar-asset
  downlink, orbital SAR, or a claim about transmitter authorization or band
  availability.
- **Evidence status.** Direct whole-disc radar support is closest at 3.83 cm
  (Haystack, 7.84 GHz); the 5.76 GHz scattering law is interpolated, with no
  measured 5–6 cm whole-disc spectrum or calibrated I/Q record. The Mini-RF
  4.2-cm studies are local SAR, and the JPL DSN report is a lunar-asset
  downlink. See the transfer boundaries in chapter 40.

## Frozen inputs and deterministic geometry

| Input | Reference value / rule | Status and provenance |
|---|---|---|
| Transmit station A | Geodetic 40.000° N, 75.000° W, 0 m; WGS-84 | Explicit assumed coordinate fixture; not a named/ surveyed installation |
| Receive station B | Geodetic 50.000° N, 10.000° E, 0 m; WGS-84 | Assumed fixture, matching a station-pair geometry analyzed in chapter 40 |
| Geometry source | JPL DE440s geometric centre states plus the explicit NAIF lunar/Earth orientation inputs below; UTC input, TDB dynamics; solve the two retarded legs once. Apparent directions are only for epoch visibility selection; beam angles use the geometric retarded legs defined below, with no second light-time correction | The earlier DE441/Horizons proposal is replaced here by the fixed DE440-compatible input set below; record acquired file hashes and exact software build |
| Epoch selection | Sample UTC hourly from 2025-01-01 00:00 through 2025-01-08 00:00 inclusive; retain samples with both apparent lunar elevations ≥10° and select the smallest absolute elevation difference, breaking ties by earliest UTC. If none qualify, extend the hourly scan through 2025-12-31 23:00. Then scan UTC seconds within ±1,800 s of the selected hour under the same elevation rule and tie-break; save the resulting epoch and state vectors. Generate one-second geometry knots over the complete prepared native message, a two-slot preroll, two following scored quiet slots, the propagation delay and all channel/filter tails. Do not truncate a longer message at 360 seconds. The actual selected epoch and archived vectors have not been generated here; this is deterministic input acquisition, not an unresolved model choice. This bounded rule does not claim the globally closest elevation equality over the year. | Deterministic selection rule avoids asserting unverified visibility at a hand-picked timestamp; archived result is part of the run input |
| RF and waveform reference | \(f_c=5.760\) GHz, \(\lambda=c/f_c=0.052047\) m; 40 W and the EME native research profile in [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract), with Fs=48,000, DATA IF=1,725 Hz and HelperOff. Requested bit duration 26.4 s, rate 360, added symbols 1, FHSS on/dwell 0.4 s, quiet 0, ramp 0.02 s, margin 0.005 s; no waits. Use actual returned slots (60 s recurrence for this fixture) | Explicit research fixture. The historical row 8 has 90-s spacing; it is context, not this profile’s calendar. 40 W is an assumption, not operating advice |
| Antennas | Both 3.0-m circular-aperture dishes, aperture efficiency 0.55, boresight tracking, Gaussian power beam \(G_i(\theta)/G_{i0}=\exp[-4\ln2(\theta/\beta_i)^2]\), HPBW \(\beta_i=70\lambda/D=1.215°\); fixed 0.05° offset toward increasing elevation at A and toward increasing azimuth on the local tangent sky at B (angular displacement, not an azimuth-coordinate increment) | Assumed identical systems. A 0.518° lunar disc is narrower than each beam; do not call this perfect uniform illumination |
| Polarization | Transmit/receive opposite-sense circular (the strong classical return); ideal matching in baseline | Define each circular sense looking along that wave’s propagation direction; OC is opposite rotational sense. This is a simulator convention, not a claim about every amateur feed |
| Mean lunar return | \(\eta=\sigma/(\pi a^2)=0.065\), \(a=1737.4\) km | 23-cm calibrated value and classical 3.8-cm support; interpolate to this carrier only as a working mean, not measurement |
| Station noise | Receive-end \(T_{sys,B}=80\) K at receiver input reference plane; \(N_0=-198.6+10\log_{10}(T_{sys}/1\,K)=-179.57\) dBm/Hz | Explicit assumed on-Moon system temperature including lunar brightness, sky, antenna, feed loss and receiver. Transmit-end receiver noise is irrelevant to this one-way budget; swap roles for the reciprocal trial. Do not add lunar/sky/feed noise a second time |
| Oscillator/tracking | GPS-disciplined references; TX-minus-RX residual δf(t)=2+0.02t Hz, t in seconds from first source slot, initial residual phase zero; remove predicted two-way bulk Doppler using the same retarded geometry, with no frequency step during a 26.4-s bit | Deliberate controlled assumptions, not a sourced population distribution. Preserve raw and corrected residuals |
| Slow environment | Clear path, no rain/cloud attenuation, no added ionospheric scintillation, no local interference; log measured/assumed elevation and Sun separation | Baseline isolates the lunar mechanism; these effects are separate scenario branches |

### Exact geometry input set

Use native [CSPICE N0067](https://naif.jpl.nasa.gov/naif/toolkit) as a reference geometry implementation, with these
public NAIF files (names/metadata inspected 2026-11-13, binary kernels not
executed here): [de440s.bsp](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp),
[moon_pa_de440_200625.bpc](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/moon_pa_de440_200625.bpc),
[moon_de440_250416.tf](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/fk/satellites/moon_de440_250416.tf),
[earth_1962_260806_2126_combined.bpc](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/earth_1962_260806_2126_combined.bpc)
and [naif0012.tls](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/naif0012.tls).
Use the named files, not a mutable `latest` alias. Lunar cells are fixed in
**MOON_PA_DE440**, frame 31008, and transformed with its time-dependent
rotation/derivative to J 2000/ICRF. DE440 centre states alone do not define
physical libration. This coherent DE440 set replaces the earlier mixed,
unpinned DE441 proposal for the 2025 fixture; no claim of inferior DE441
accuracy is implied. Verify coverage through every episode before running.

Station coordinates are constant in ITRF93 for this control, WGS-84 semimajor
axis 6378137 m and inverse flattening 298.257223563; no tectonic motion or tidal
station displacement is added. Transform station states from ITRF93 to J 2000
using the named Earth PCK, then add the geometric Earth-centre barycentric
state. Obtain centre states with `spkezr_c(...,"J 2000","NONE","SSB",...)`;
convert SPICE kilometres to metres exactly once. Use `sxform_c` for state
rotations and `str2et_c` with the named leap-second file. Use these exact named frames rather than mutable aliases. Do not use `LT`, `CN`, stellar-aberration-corrected
vectors, or observer-table apparent coordinates as geometric inputs to the
explicit two-leg solve. A Horizons cross-check uses VECTORS with
`VEC_CORR=NONE`, `REF_SYSTEM=ICRF`, `REF_PLANE=FRAME`, `CENTER=500@0`.

For epoch visibility, use u=(Moon(tm)−station(tr))/|Moon(tm)−station(tr)|
from the receive-leg retarded centre solution and station barycentric velocity
v. The first-order apparent direction is normalize[u+v/c−u(u·v/c)]; rotate
this to the local horizon. Atmospheric refraction is zero. Beam centres use
the geometric centre-to-station ray at each leg's own retarded epochs, then
the specified tangent-sky pointing displacement; aberration is not applied a
second time to surface rays. This exact geometric beam convention is an
approximation, not a claim of modeling every apparent servo correction.
The solver is Euclidean retarded propagation in barycentric coordinates;
relativistic/Shapiro and atmospheric corrections are excluded. Its 1e−13-s
residual and phase convergence concern numerical consistency, not physical
submillimetre range accuracy. Represent time as an epoch plus small relative
seconds, solve for light-time differences, and interpolate local state offsets;
subtracting two large absolute TDB doubles is not an adequate sub-picosecond
residual test. Record input precision separately.

Input files retain their NAIF notices and provenance; SPICE use/distribution
follows the published [NAIF rules](https://naif.jpl.nasa.gov/naif/rules.html),
not an invented generic open-source license. No kernel or third-party source
is vendored by this documentation change. Acquisition/hash/coverage and the
native geometry checks remain unexecuted fixture-validation steps.

The station-pair table in chapter 40 reports for this geometry a 2025
two-station limb-to-limb width with 10th percentile 41 Hz, median 79 Hz, 90th
percentile 117 Hz and maximum 128 Hz, conditional on both elevations above 5°.
Those are annual derived estimates, not observations and not a substitute for
computing the selected epoch's state. This specification uses ≥10° and recomputes
the geometry, so the reported percentiles are context only.

## Deterministic scattering and coupling model

At each sample epoch, use station-to-Moon retarded vectors and the lunar
apparent rotation state from the same ephemeris solution. Let \(x\) be a point on
the spherical lunar surface, \(A,B\) the station locations, \(R_A,R_B\) their
centre ranges, θ_A(x), θ_B(x) angular offsets in the transmit/receive antenna
patterns, and \(q_p(x;f_c)\) the per-unit-surface-area differential cross-section
for polarization branch \(p\). Evaluate both incident and outgoing directions;
do not assume monostatic incidence for separated sites.

Define normalized power patterns g_i(θ)=G_i(θ)/G_i0, with g_i(0)=1, and use the mean-power weight

\[
w_p(x)=\frac{g_A(\theta_A(x))g_B(\theta_B(x))q_p(x;f_c)}
{R_A(x)^2R_B(x)^2},\qquad
\bar P_p=\int_{S_M} w_p(x)\,dA.
\]

For a visible surface on both legs, the mean received power is
`P_r,p = P_t G_A0 G_B0 λ²/(4π)³ × ∫ g_A g_B q_p/(R_A² R_B²) dA`,
with linear power gains, q per surface area, ranges in metres and the stated
polarization convention. Normalize the gain patterns to boresight exactly once.

The reference boresight gains are kept outside \(w\). Normalize the angular
kernel for the selected receive polarization to its corresponding source
cross-section before applying the two beam factors: in the wide-beam,
far-field limit, \(\int q_{p_0}dA\) must recover the selected
opposite-circular cross-section \(\sigma_{p_0}\). Model the other polarization
branch separately using its measured/interpolated ratio; do not normalize each
polarization branch to the same \(\sigma\). For an unequal-station bistatic link the surface scattering
law must be evaluated at the actual bistatic angle, and its departure from the
monostatic law is an explicit uncertainty branch. Use T52 with the MEAN bistatic extension defined in the frozen-kernel section
below. Monostatic normalization remains fixed when visibility, bistatic angle or
beams change: do not restore lost coupling by renormalizing the visible region.

For each point at lunar reflection epoch \(t_m\), solve the two legs at their
retarded station epochs. Let \(\ell(x,t_m)\) be the sum of the transmitter-to-\(x\)
distance at emission and the \(x\)-to-receiver distance at reception, with
emission and reception times solved consistently from light time. Let \(x_0\)
be a reference point whose path length is the minimum over the mutually illuminated visible surface at that epoch. Then nonnegative excess propagation delay and two-way Doppler come from the same retarded path length:

\[
\tau(x,t_m)=\frac{\ell(x,t_m)-\ell(x_0,t_m)}{c},\qquad
f_D(x,t_m)=-\frac{1}{\lambda}\frac{d\ell(x,t_m)}{dt_r}.
\]

Here t_r is receiver time, and the derivative includes the retarded-time chain rule dt_m/dt_r. The sign convention is positive received frequency for decreasing path length;
bulk Doppler is the reference-point value and is removed only by the declared
tracker. Verify the sign and the finite-difference derivative using the same
retarded solution, not simultaneous station positions.
The reference x0 minimizes the continuous spherical-surface retarded path, not
the path over the finite cell centres. Solve this smooth two-coordinate
minimum from the normalized station-direction bisector, and verify local
stationarity/positive curvature plus the independent endpoint check. Using the
nearest grid cell as zero delay would introduce a mesh-dependent bias.
The normalized joint delay–Doppler/polarization power measure is

\[
S_p(\tau,f;t)=\frac{1}{\bar P_p}\int_{S_M} w_p(x)
\delta(\tau-\tau(x,t))\delta(f-f_D(x,t))\,dA.
\]

For waveform synthesis, partition the disc into equal-area cells and integrate
weights over each cell. Draw one independent zero-mean, unit-variance circular complex Gaussian latent coefficient per cell and hold that coefficient over the complete continuous run. At each receiver time multiply it by the square root of that cell's current physical power weight, including the link prefactor and current beam, visibility and range terms. This preserves evolving geometry without normalizing each realized field to a fixed power. At every sample,
propagate its phase from that cell's retarded bistatic path length and sum each cell's complex coefficient times the transmitted waveform delayed by the full moving station-to-station path, x(t−ℓ_cell(t)/c), with retarded epochs solved consistently before receiver filtering. The 1-s ephemeris grid is not the waveform sample rate: interpolate the retarded geometry and phase continuously to the actual receiver sample times; do not redraw scatterers at
sample, slot or message boundaries. This gives a continuous Rayleigh
(\(K=0\)) field as an explicit stochastic assumption;
do not add a direct nonfading path. Keep a convergence record for equal-area
refinement by factors of four until total coupling changes <0.1 dB and the
reported delay/Doppler quantiles change <2% or 1 µs/1 Hz, whichever is larger.
These are numerical criteria, not evidence of physical accuracy.

## Reference budget and severity branches

For far-field wide-beam reference only, use

\[
L_{iso}=10\log_{10}\!\left[\frac{(4\pi)^3R_A^2R_B^2}
{\sigma\lambda^2}\right]\quad\text{dB},\quad
P_r=P_t+G_A+G_B-L_{iso}.
\]

For equal 384,400-km ranges this reduces to the monostatic expression in
chapter 40 and gives \(L_{iso}\approx284.1\) dB at 5.760 GHz with \(\eta=0.065\).
With 3-m, 55%-efficient dishes, \(G_0=10\log_{10}[0.55(\pi D/\lambda)^2]
\approx42.55\) dBi each. Thus the illustrative receive carrier is about
−153.0 dBm and \(C/N_0\approx26.6\) dB-Hz against 80 K, before actual
two-station geometry, beam-weighted coupling, tracking or polarization losses.
This is an arithmetic reference, not a predicted observed level. For the actual
scenario, use the full integral and separate \(T_{sys}\), signal coupling and
receiver reference planes.

The finite moderate/severe/outage/discriminator branches below are the
executable research definitions. Apogee or a smaller cross-section can be
studied as separate synthetic sensitivities; do not alter actual ephemeris
ranges while still presenting the result as the selected station geometry.

## State, measurements and acceptance limits

- Keep one continuous channel state through the complete native-source episode,
  all waveform slots, source silence, observation gaps, and delayed tails. Do
  not reset fading at message boundaries. Compute and retain the actual illuminated bistatic delay support; 2a/c ≈ 11.6 ms is the monostatic full-disc limit.
- Emit calibrated complex baseband at the receiver reference plane, truth
  \(S_p(\tau,f;t)\), unweighted and beam-weighted total return, per-end gain,
  path lengths/rates, residual Doppler, \(T_{sys}\)/\(N_0\), and time-aligned
  clean input. Emit public receiver outputs separately from private channel
  truth.
- Report: integrated return / coupling loss; cumulative excess-delay fractions
  (t50, t90, rms, support); Doppler support, power-containing 50/90% bands and
  autocorrelation widths with definitions; complex-gain correlation and fade
  depth distribution; selected circular-polarization power and the other branch only if specified; C/N0; per-bit decode,
  erasure and message completion; and state/quiet-slot traces. Give the actual
  ephemeris, geometry, resolution, random seed and all units.
- Compare the integrated whole-disc delay profile against the 3.8-cm/23-cm
  Lincoln Laboratory tabulations and the 68-cm pulse-length curve where
  applicable. Compare predicted libration extent against the chapter 40 annual
  geometry calculations and the 5.7-GHz visual widths reported by G3WDG. These
  are reference reproduction checks with different bands/estimators, not
  independent 5.76-GHz validation.
- Correctness gates before receiver interpretation: geometry-specific delay endpoints within 1 µs of an independent calculation, with 2a/c checked only in the monostatic full-disc limit; path/Doppler signs and range-rate finite differences
  agree; wide-beam integral recovers σ to 0.1 dB; equal-distance/equal-station
  limit recovers the monostatic equation; valid noise-only and invalid-capture cases produce their actually scored outcomes; and
  output duration does not produce completion. A pass only establishes the
  implementation of this assumed model.

## Competing model branches and unresolved validation

1. **Evans–Hagfors + diffuse two-component disc** (H30 alternative): strongest
   whole-disc delay-law basis, but 5.76-GHz interpolation, imperfect 3.8-cm
   Hagfors fit, and ambiguous component decomposition remain.
2. **Uniform surface-area (DS) or projected-area (DP) kernel:** fixed
   sensitivity comparator. It discards the measured central highlight and
   should not silently replace the baseline.
3. **Local Mini-RF texture/Stokes lookup:** future regional heterogeneity
   branch only after calibrated products and mapping to 5.76-GHz material
   response are available. Gao 2023, Campbell 2025 and Fassett 2025 do not
   directly supply the required disc-level C-band cross-section or complex
   scattering law.
4. **JPL DSN directive-antenna WSSUS model:** useful antenna/directivity
   comparator for the lunar asset downlink; its one-way near-grazing geometry is
   not a substitute for the bistatic EME model.
5. **Two-tap or flat Rayleigh model:** cheap modem diagnostic only. Match total
   power and a named quantile, and record the lost delay–Doppler coupling; do
   not claim physical equivalence.

No empirical validation or receiver qualification is claimed. The decisive next
data would be calibrated 5–6-cm two-station complex echo recordings with station
coordinates/time standards, dish patterns, polarization calibration, noise
temperatures, ephemeris, and enough time/bandwidth to estimate the joint
delay–Doppler/Stokes statistics. Until then, this scenario supports
mathematical implementation checks and bounded model-sensitivity experiments
only.

## Frozen kernel, polarization and numerical contracts

The baseline uses selected LL67 Table II opposite-circular measurements
(printed p.6, PDF page index 8). These are per **surface area**, not projected
area. The sparse selection is part of the mathematical control, distinct from
chapter 40’s full-table calculation.

| Incidence θ, degrees | 3.8 cm, dB | 23 cm, dB |
| ---: | ---: | ---: |
| 2.4 | −0.83 | 2.77 |
| 7.5 | −4.03 | −1.53 |
| 16.9 | −8.78 | −8.58 |
| 24.0 | −10.93 | −12.58 |
| 34.2 | −13.73 | −16.33 |
| 42.2 | −15.23 | −17.73 |
| 55.3 | −17.43 | −19.98 |
| 66.6 | −20.13 | −22.58 |
| 77.1 | −23.98 | −26.53 |
| 82.1 | −26.93 | −29.43 |
| 87.0 | −32.03 | −34.93 |

**T52:** λ=c/5760000000, c=299792458 m/s. With endpoint wavelengths
exactly 0.038 and 0.23 m, set α=ln(λ/0.038)/ln(0.23/0.038)=0.1747114641.
At each row use F=10^(((1−α)d3.8+αd23)/10); interpolate **linear power
linearly in angle** between rows. Extend constantly to 0° and taper linearly in
power from 87° to zero at 90°. The back hemisphere contributes zero.
For any nonnegative F define N[F]=F/(2πa²∫₀^(π/2)F(θ)sinθ dθ), so its
surface integral is one. Set qOC=σOC N[F]. No extra cosine or diffuse power is
added to this already total selected-polarization kernel.

The finite uncertainty set has no assigned natural probabilities:

| ID | Fixed alternate law at the same σOC | Interpretation |
| --- | --- | --- |
| T38 / T23 | T52 recipe with α=0 /1 | Endpoint-shape sensitivities, not confidence limits at 5.76 GHz |
| H30 | σOC{0.70N[H]+0.30N[cos^1.5θ]}, H=(cos⁴θ+44sin²θ)^(−3/2) | Hagfors-shaped highlight and diffuse alternative |
| H25 / H35 | Same H, diffuse fraction 0.25 /0.35 and complementary highlight | Explicit component-decomposition sensitivity |
| DP | σOC N[cosθ] | Uniform projected-disc brightness |
| DS | σOC/(2πa²) on visible hemisphere | Uniform surface-area return; different from DP |

C=44 and the diffuse fractions are declared interpolated choices, not a unique
fit. The Cρ/2 prefactor in LL67 Eq.33 cancels during N[H]; σ fixes power
separately. The 3.8-cm fit is imperfect. Sultan-Salem–Tyler 2006 further show
Kirchhoff-law limitations, including energy conservation and ambiguous physical
slope interpretation. Normalizing a chosen backscatter shape does not validate
a full electromagnetic surface model.

**Bistatic law:** at each cell define n as surface normal and uA,uB as unit
vectors toward the retarded stations. Exclude n·uA≤0 or n·uB≤0. MEAN uses
θ=[acos(n·uA)+acos(n·uB)]/2. BISECTOR instead uses
θ=acos[n·(uA+uB)/|uA+uB|]. OPP multiplies MEAN by
[1+exp(−(b/1°)²)]/2, b=acos(uA·uB). Keep the monostatic normalization
in all three. OPP is a fixed 0 to−3.01 dB sensitivity, **not** a measured
whole-disc opposition law. Baseline is T52+MEAN; vary alternatives individually
before combining them in a named stress.

**Polarization alternative:** qSC=rσOC N[cosθ], r=0.20, with 0.10 and 0.30
sensitivity endpoints. This is additional orthogonal received power, not a
redistribution of σOC. Draw independent OC and SC proper Gaussian cell fields
by default. The correlation control sets zSC=0.9zOC+sqrt(1−0.9²)zIndependent.
Keep separate branch power normalization. Ideal OC observes OC only; a wrong
port observes SC only; the fixed leakage control observes
v=cos(10°)vOC+sin(10°)vSC and applies the same linear transform to channel
noise. These ratios/correlation/feed errors are assumptions, not calibrated
5.76 GHz Jones statistics. No arbitrary scalar polarization loss is hidden. The two ideal receiver ports
have independent proper thermal noise with spectral covariance
E[n(f)n(f)ᴴ]=kB·80 K·I2 W/Hz in baseline (160 K in ST); cross-covariance is
zero and both ports use the same declared reference plane. Apply the leakage
row to this matrix as well as the signal. Thus the unit-norm leakage row
preserves baseline noise power. This fixes a mathematical noise model, not a
measured feed/noise covariance. The separate atmospheric overlay replaces both
port temperatures by its stated Tsys before the same transformation.

**Grid and random field:** use a full body-fixed equal-area sphere, initially
Nμ=256,Nφ=512; μj=−1+2(j+1/2)/Nμ, φk=2π(k+1/2)/Nφ,
ΔA=4πa²/(NμNφ), cell index i=jNφ+k. Keep each cell’s coefficient through
visibility changes. Double each dimension for convergence, at most 1024×2048;
an unmet convergence tolerance is a failed numerical qualification. Compare
ensemble integrals across meshes, not unrelated sample-for-sample speckles.
This grid is numerical quadrature, not evidence of a physical number of
independent facets. A converged native reduced-kernel or blocked synthesis is
permitted; its cost/error must be reported. The literal cell sum is the
reference equation, not an assertion of affordable waveform execution.

Use the explicit `std::mt19937_64`, upper-52-bit midpoint uniform conversion and
Box–Muller law from [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract).
Use its paired diagnostic roots 5489+r (r=0,…,19), path domain 1 and receiver
noise domain 6. Two standard independent normal variates divided by sqrt(2)
give each proper complex cell coefficient. Within the path domain generate OC
cells in index order, followed by SC-independent cells. Do not consume random
numbers in worker-completion order or redraw per sample/slot/message. The finite
seed set is a diagnostic, not independent natural field episodes or a success
probability. Subsequent iid model trials use the separately sampled episode
roots and inference design in 86. Preserve generator, math-library/compiler and
numeric fixture hashes; a seed alone does not promise cross-platform bit identity.

**Power and phase:** use the canonical unit DATA-reference source `x=Emission/sqrt(mu*G²)` from 00, with Helper off, and zero physical source outside its support. This is the bounded native reference, not a realized-message average. The cell weights already include Pt; do not apply transmit power a second time. For cell power pi, its voltage is
sqrt(pi)zi exp[−i2πℓi(tr)/λ] x(te,i). Here
tr−tm=|B(tr)−x(tm)|/c and tm−te=|x(tm)−A(te)|/c.
The full retarded path, not just excess delay, determines te and calendar
arrival. Solve these equations to a residual below 1e−13 s; retain a reference-path subtraction
before forming residual carrier phase to avoid large-number cancellation.
Fixed Gaussian phases may absorb the constant initial path phase once.
The ensemble mean is Σpi, but individual coherent realizations interfere and
fade; do not renormalize them or add a deterministic nonfading return just
because the literature calls a component quasispecular.

Use 1-s geometry knots with cubic Hermite path/derivative interpolation, halve
spacing until corrected phase differs <0.01 rad over a bit, and independently
check path-rate derivatives with 1,0.5,0.25-s finite differences. Apply continuous
bulk correction before the 48,000-complex-sample/s representation. The same
Fs and DATA IF=1,725 Hz configure the native source, PublicProfile, audio
projection and receiver; there is no independent 12 kHz or 960 Hz receiver
calendar and no implicit resampler. Helper is off in this fixture. Record the
whole-message spectral guard including libration spread, oscillator drift,
source pulse margins and any real-audio image. If it fails, report a failed
fixture rather than silently moving carriers or deleting hypotheses.

The baseline channel adds no filter beyond the canonical source/receiver
filters. For the explicitly filtered numerical control, use a 513-tap causal
low-pass FIR at 48 kHz: m=n−256, n=0,…,512,
h[n]=sinc(2·5500m/48000)·(2·5500/48000)·w[n],
sinc(z)=sin(πz)/(πz), sinc(0)=1,
w[n]=0.42−0.5cos(2πn/512)+0.08cos(4πn/512).
Normalize by Σh once for unit DC gain. Its group delay is 256/Fs and support
512/Fs; carry both through the public arrival bound and quiet-tail checks.
Verify pass/stop response and band containment; this filter is not a measured
front-end response. Receiver complex white noise before the filter has
E|n|²=kBTsysFs, half in each real component. Respect [00](00-method-and-modem-context.md)’s
real/complex PSD convention when projecting to native audio.

Public ephemerides, station positions and clocks may drive the receiver’s
predicted minimum-path bulk correction. **Private** per-cell phase, scattering,
fade/outage times and correct decode decisions may not. Archive prediction
inputs separately from channel truth; a +5 Hz public-prediction error control
changes correction only, not the physical path. Append the declared oscillator
phase 2π(2t+0.01t²) continuously. The receiver still scores its ordinary public
hypotheses; ephemeris access is not knowledge of the scattering realization.

### Public uncertainty and observation bounds

The public observation is fixed from two native slots before epoch through
1800 s after epoch, independent of realized fading or decoder success. The
common thirteen-bit fixture is intended to fit that horizon; verify actual native slot/tail bounds during setup and require the entire common 00/86 q0+2 scoring window to end by 1800 s. Otherwise this fixed-horizon fixture fails setup and is not shortened to fit. Failure to finish within a local horizon is censored or a
resource failure, never physical completion. Evaluate geometry over this entire
interval, including signal-free parts. A later extended-horizon experiment is
explicitly a different fixture, with correspondingly enlarged bounds.

Publish nominal full propagation delay 2.6 s with arrival uncertainty ±0.5 s,
excess-delay support [0,0.012] s, residual CFO search ±100 Hz, residual linear
clock drift ±0.05 Hz/s and clock-phase uncertainty [0,2π). These bounds are
chosen before channel realization. The prescribed oscillator is at most 38 Hz
by 1800 s, and D-track adds 5 Hz; bulk ephemeris correction is already removed.
Publish relative Doppler support ±250 Hz as a conservative model search bound
and verify the computed geometric support against it. Do not confuse this
surface spread with a new CFO draw. For the filtered control the 3.1 s maximum
arrival bound already includes its 10.667 ms causal support; still preserve
that filter delay/tail in scoring. If any actual geometry, native aperture,
clock, support or sample-containment check exceeds these bounds, the fixture
fails setup and no receiver assertion has run. Never clip physics to fit.
For native construction set profile `arrival_low_seconds=2.1`,
`arrival_high_seconds=3.1`, `spread_delay_ms=12`,
`delay_excursion_seconds=.5`, `cfo_bound_hz=350`,
`clock_bound_ppm=0`, `audio_pair_ppm=0`. Use matching receiver
`frequency_bound_hz=350`, `spread_delay_seconds=.012`,
`delay_excursion_seconds=.5`, zero relative/endpoint sampling-clock bounds,
`time_varying_channel=true`, `time_varying_common_doppler_hz=250`,
`time_varying_atmospheric_hz=0`, automatic native coarse precision, and the
public source/noise normalization of 00. The 350-Hz static search includes
the declared 100-Hz residual CFO plus 250-Hz relative support; it is not an
extra injected shift. LO phase drift here is not a sample-clock error. The
native receiver has no separate Hz/s drift-bound field: its slope hypotheses
are derived from the hertz-valued bounds and actual geometry, so the ±0.05 Hz/s
physical envelope is an experiment input, not a proved receiver-coverage claim.
Allocate the complete finite source/filter/arrival support before realization;
a failed admission or hypothesis mismatch stays visible in the outcome.
The Gaussian coefficients are unbounded in amplitude; these geometry/CFO
bounds do not falsely impose an amplitude-tail bound.

## Fixed severity and model-discrimination controls

Use the common prepared native message and actual transmit_slot(i)/slot(i)
scoring apertures from [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract).
Keep the channel alive from preroll through true source silence and tails.
Set i=floor(M/2), M=wire_bits(), retaining later DATA; the common thirteen-bit
fixture therefore uses interior slots 6 and 7 and resumes 8. The zero-signal
interval starts before every source/filter sample contributing to those scored
apertures, continues through their end, and is logged in exact native samples.
Paired model comparisons share source and random seeds; success statistics
require independent full episodes under the statistical protocol.

| ID | Fixed change from T52+MEAN | Meaning |
| --- | --- | --- |
| M | Baseline | Moderate reference, not typical availability |
| Sη | η=0.040 only | −2.109 dB signal-power sensitivity |
| ST | Tsys=160 K only | −3.010 dB C/N0 sensitivity |
| SP | Both offsets 0.30°, same directions | Coupling loss and Doppler-centroid shift |
| SW | A(t)=6sin²[π(t−t1)/(t2−t1)] dB over start of source slot i through end of slot i+1, zero elsewhere | Smooth 6 dB attenuation control, not measured lunar scintillation |
| SC | Sη+ST+SP+SW | Combined severe stress; no inferred joint probability |
| O-valid | Signal zero over all scored apertures of slots i and i+1, valid samples/noise retained, resume slot i+2 | Two false quiet scores can cause premature completion; report actual integrity/outcome |
| O-invalid | Same interval publicly invalid, production invalid-capture behavior | Known capture loss differs from physical absence; no success from invalidity |
| O-final | Genuine final source silence through at least two scored quiet slots and tails | Physical-completion check; EOF/cancellation is not success |
| D-kernel | T38,T23,H25,H30,H35,DP,DS one at a time, matched σ | Shape and carrier-covariance sensitivity |
| D-bistatic | BISECTOR and OPP separately | Unknown bistatic-law transfer sensitivity |
| D-pol | OC/SC/leakage, r=.10,.20,.30, correlation 0/.9 | Fixed polarization normalization/dependence controls |
| D-dish | D=7 m at both ends, same efficiency/power/pointing, baseline 3 m comparator | +7.36 dB boresight gain per end; beam loss alone does not imply worse link |
| D-track | +5 Hz predictor error | Public tracking robustness |
| D-flat | Collapse excess delays to their power-weighted mean and relative Doppler to zero; one proper Gaussian held all run, same mean power | Deliberately crude flat/static comparator, not physical equivalence |

For a **separate** atmospheric-noise sensitivity, choose internal noise 50 K,
clear external noise 30 K, effective layer 275 K and receive-leg transmission
L=10^(−A/10). Set Tsys=50+30L+275(1−L) K and signal voltage×sqrt(L).
This freezes radiative-transfer inputs without inventing a rain rate or a
measured scintillation process. The two legs may share the same slow envelope
or use one affected end only as separate controls; neither independence nor
perfect dependence is an empirical finding.

For D-dish, report both fixed-Tsys and beam-consistent thermal results. In the
latter use uniform lunar brightness 225 K, background 3 K and the same fixed
receiver/spillover term at both diameters, calibrated so centred 3 m totals 80 K.
For centred Gaussian beams, F=1−exp[−4ln2(rMoon/β)²] and
Tsys=Tfixed+3+(225−3)F. Determine Tfixed once from 3 m, then hold it fixed.
Use the two-dimensional integral for offsets. This assumed comparator avoids
adding lunar noise twice or pretending 80 K is dish-independent measurement.

## Reproduced arithmetic and remaining qualification

Scalar θ-midpoint quadrature with 400,000 points on 2026-11-13 gives T52 raw
η=0.06881554 before normalization. With τ=(2a/c)(1−cosθ), normalized
T52 gives t50=0.70796 ms,t90=5.08696 ms,t99=9.02085 ms,mean 1.70952 ms,
RMS about mean 2.18263 ms,support 11.590685 ms. An independent Simpson
quadrature in μ=cosθ at 40,000 intervals gives t50=0.707955 ms,t90=5.086946 ms,
mean 1.709519 ms,RMS 2.182632 ms; doubling from 20,000 changes t90 by 22.7 ns.
The agreement checks quadrature/Jacobians, not lunar truth. These sparse-table
values are distinct from the chapter 40 full-table estimate and K1JT estimator.

Frequency correlation is R(Δf)=∫q exp(−i2πΔfτ)dA/σ. At 75 Hz it is
0.585732−0.337345i, magnitude 0.675932; at 150 Hz it is
0.464799−0.284429i, magnitude 0.544920. Independent or identical carrier
fading therefore fails this chosen-model control. Centred equal-range two-beam
coupling is−0.25090 dB for 3 m and−1.16888 dB for 7 m relative to each pair’s
own boresight-gain budget; neither value includes 0.05° offsets.

An ephemeris-independent discriminator fixes R=384400 km, centred beams,
zero offsets/bulk Doppler and angular rotationω=80λ/(4a) about the sky y-axis.
Its monostatic fD=−2aω sinθ cosφ/λ has exactly 80 Hz limb-to-limb support.
Reversingω reverses Doppler while preserving power/delay. DS must give uniform
delay density,t50=a/c; DP must give CDF=2τ/T−(τ/T)²,T=2a/c. These different
controls detect a duplicated/missing projected-area cosine. For a constant
unit-power source verify ensemble E|v|²=Σpi, not the square of summed positive
amplitudes. Unit-pulse tests must preserve delayed energy and full support.

Report central equal-tail 50/90%-power Doppler bands, PSD FWHM,
PSD-autocorrelation FWHM, visual estimator (if any), and first field-correlation
|Rh(Δt)|≤0.5 separately. Reciprocal bandwidth is not automatically that field
coherence time. Geometry, derivative, noise, mesh, phase, filtering, native
receiver, outage and completion checks remain **unexecuted acceptance tests**.
The declared FIR was also checked by direct scalar frequency-response sums:
DC gain 0 dB, response−0.000516 dB at 5 kHz and−84.5055 dB at 6 kHz;
group delay 5.333333 ms, causal support 10.666667 ms. These are isolated
coefficient/response checks, not waveform or receiver execution. Two additional small controls were evaluated: a two-cell unit-power proper
four-phase ensemble has exact mean power 1, but individual powers range from
0.133975 to 1.866025 for cell powers 0.25/0.75. This demonstrates coherent
interference without per-realization normalization; it is not a Gaussian-tail
validation. A rotating-point, equal-range retarded-path check with R=384400 km,
θ=0.7 rad,φ=0.3 rad and the stated ω gives −24.7030543 Hz. Finite differences
at 1/0.5/0.25 s agree with the analytic retarded-time derivative within
2.4e−6 Hz for both rotation signs; this is a scalar toy-geometry check, not a
DE440 station-geometry run. The centred thermal comparator gives fills 0.118448
and 0.496609 for 3/7 m, hence 80 K and 163.9518 K with the fixed receiver term.
Only these scalar checks and the quadratures above were run for this document. No 5–6 cm
whole-disc calibrated two-station complex echo was acquired; the verified
433.8 MHz SigMF and local 4.2/12.6 cm orbital products in chapter 41 do not
close that empirical dependency. Fixed mathematical experiments can proceed
without claiming that missing validation has occurred.

# EME 5.76 GHz scenario specification

- **Purpose/status.** Candidate research scenario, not yet a fully fixed input
  contract, a modem result or an
  empirically validated lunar channel. It turns the source-limited evidence in
  chapters [40](40-eme-lunar-scattering-and-geometry.md) and
  [41](41-eme-systems-practice-and-environment.md) into a proposed input
  contract. The angular kernels, interpolation and mixture choices below still
  require source extraction and explicit numerical definitions before this
  branch is experiment ready; see the [active plan](80-completion-plan.md).
  Numeric source values remain evidence-graded in those chapters.
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
| Geometry source | JPL DE441 via Horizons; topocentric apparent directions and two-leg retarded light-time solution; UTC input, TDB internal ephemeris time; include Earth rotation/UT1 and station geodetic transforms | DE441/Horizons source described in [40](40-eme-lunar-scattering-and-geometry.md); record API/query, ephemeris version and returned vectors with each run |
| Epoch selection | Sample UTC hourly from 2025-01-01 00:00 through 2025-01-08 00:00 inclusive; retain samples with both apparent lunar elevations ≥10° and select the smallest absolute elevation difference, breaking ties by earliest UTC. If none qualify, extend the hourly scan through 2025-12-31 23:00. Then scan UTC seconds within ±1,800 s of the selected hour under the same elevation rule and tie-break; save the resulting epoch and state vectors. Sample ±180 s around it at 1-s increments for the geometry sequence. This bounded rule does not claim the globally closest elevation equality over the year. | Deterministic selection rule avoids asserting unverified visibility at a hand-picked timestamp; archived result is part of the run input |
| RF and waveform reference | \(f_c=5.760\) GHz, \(\lambda=c/f_c=0.052047\) m; use EME row 8 from [00](00-method-and-modem-context.md): 40 W, 16.7 ms cells, 75 Hz carrier spacing, 225 Hz occupied bandwidth, 26.4 s bit, 90 s slot spacing | Existing source-row waveform fixture; 40 W is a scenario assumption, not operating advice |
| Antennas | Both 3.0-m circular-aperture dishes, aperture efficiency 0.55, boresight tracking, Gaussian power beam \(G_i(\theta)/G_{i0}=\exp[-4\ln2(\theta/\beta_i)^2]\), HPBW \(\beta_i=70\lambda/D=1.215°\); fixed 0.05° pointing offset at each end | Assumed identical systems. A 0.518° lunar disc is narrower than each beam; do not call this perfect uniform illumination |
| Polarization | Transmit/receive opposite-sense circular (the strong classical return); ideal matching in baseline | The source chapters flag the amateur 5.76-GHz sense and practical feed discrimination as unresolved; set and report the actual simulator convention |
| Mean lunar return | \(\eta=\sigma/(\pi a^2)=0.065\), \(a=1737.4\) km | 23-cm calibrated value and classical 3.8-cm support; interpolate to this carrier only as a working mean, not measurement |
| Station noise | Receive-end \(T_{sys,B}=80\) K at receiver input reference plane; \(N_0=-198.6+10\log_{10}(T_{sys}/1\,K)=-179.57\) dBm/Hz | Explicit assumed on-Moon system temperature including lunar brightness, sky, antenna, feed loss and receiver. Transmit-end receiver noise is irrelevant to this one-way budget; swap roles for the reciprocal trial. Do not add lunar/sky/feed noise a second time |
| Oscillator/tracking | GPS-disciplined references; residual frequency offset 1 Hz at each end, drift 0.01 Hz/s each; remove predicted two-way bulk Doppler using the same retarded geometry, with no frequency step during a 26.4-s bit | Deliberate controlled assumptions, not a sourced population distribution. Preserve raw and corrected residuals |
| Slow environment | Clear path, no rain/cloud attenuation, no added ionospheric scintillation, no local interference; log measured/assumed elevation and Sun separation | Baseline isolates the lunar mechanism; these effects are separate scenario branches |

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
monostatic law is an explicit uncertainty branch. In the baseline approximation
treat the 3.8-cm and 23-cm opposite-circular scattering laws in chapter 40 as candidate endpoints for a family of interpolation and mixture choices. A reproducible run must freeze the endpoint angular functions, interpolation variable, linear-power or dB convention, and normalized diffuse/specular mixture; no unique 5.2-cm kernel is established here. Do not label an interpolation as 5.8-GHz data.

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

| Branch | Controlled inputs | Intended observable and interpretation |
|---|---|---|
| Baseline | Frozen inputs above; η=0.065; a declared member of the 5.2-cm scattering-model family; clear environment | Geometry-conditioned τ/f/polarization kernel, integrated received level, C/N0, Rayleigh amplitude statistics, and decode/message-completion outcomes. It is a moderate reference case, not a claim of typical availability. |
| Geometric and beam-coupling sensitivity | Set range at apogee 406,700 km and η=0.04 (the low reported 3.6-cm cross-section, ±3 dB); compare baseline 3-m beams with 7-m Gaussian beams (HPBW ≈0.52°, Moon-sized), keeping all other inputs fixed. Report each change separately and the joint stress only as an explicitly synthetic combination. | Source chapter 40 estimates +2.1 dB isotropic loss from η and +1.3 dB for a centered 7-m two-way beam under its approximation. Recalculate the actual bistatic integral; these offsets are checks, not guaranteed combined penalty. Do not describe the combined case as observed or percentile-based. At unchanged 40 W, efficiency and Tsys, the 7-m dishes have about 7.36 dB more gain per end; a 1.3 dB coupling penalty alone does not make their full link adverse versus the 3-m pair. |
| Outage / null control | Set one station below the geometric horizon at a later sample, or set received signal power to zero while preserving the configured calendar and receiver state. Record which branch was used. | Record the actual DATA, null or erasure decision and integrity outcome. A valid noise-only capture during an in-message fade can produce two quiet scores and premature completion; source truth is evaluator-only. Separately model a known invalid capture interval and its public validity behavior. EOF, injected null, quota or cancellation must not themselves signal success. |

## State, measurements and acceptance limits

- Keep one continuous channel state through the entire 360-s geometry sequence (361 timestamps at one-second spacing),
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

1. **Evans–Hagfors + diffuse two-component disc** (baseline): strongest
   whole-disc delay-law basis, but 5.76-GHz interpolation, imperfect 3.8-cm
   Hagfors fit, and ambiguous component decomposition remain.
2. **Uniform diffuse disc or uniform projected-area kernel:** simple
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

The cited scattering laws are monostatic; use as a declared baseline their incidence-angle law at the average of local incident and outgoing angles, normalized to the selected cross section, without claiming that this approximation is a measured bistatic law. Specify the chosen source-law coefficients and interpolation endpoint tables in each run manifest, and vary that angular mapping as model uncertainty. The 0.05° pointing offsets require explicit position angles at both stations, and the sample rate and random seed must be fixed in a particular run manifest. These inputs and the selected ephemeris epoch remain prerequisites for a bitwise-reproducible fixture.

The Moon is in the far field of each individual station dish: 2D²/λ is about 346 m for 3 m and 1.88 km for 7 m. That does not place it in the Fraunhofer far field of a hypothetical coherent aperture spanning the entire lunar diameter. The surface-element integration and retarded phase account for the distinct extended-target geometry.

The full earliest-path propagation delay and its change must be retained in the receiver calendar and reported separately from relative excess delays. For a repeatable numerical fixture, select the kernel coefficients, endpoint interpolation and polarization mixture, beam offset directions, sample rate, random seed, and actual ephemeris epoch in a versioned run manifest; the choices in this research are a model family, not a unique fully frozen empirical fixture.

One fully declared **synthetic reference kernel**, not a measured 5.76 GHz
distribution, can be constructed from chapter 40's selected 3.8 and 23 cm
opposite-circular table values. At each tabulated incidence angle, interpolate
the two tabulated cross-section values in dB linearly in log wavelength to
λ = 0.052047 m. Convert the resulting dB values to linear power and interpolate
linearly in angle; extend the 2.4° value constantly down to 0° and taper the
87° value linearly in power to zero at 90°. Set it to zero on the unseen side.
For bistatic geometry use the mean local incidence/emergence angle as an
explicit approximation, then normalize the reference monostatic surface integral
to σ = 0.065 πa². The table represents total selected-polarization power: do
not add another diffuse fraction on top. This approximation and endpoint
interpolation are model uncertainty, not direct 5.76 GHz validation.

For this control choose 12,000 complex samples/s in a baseband already tracked
by the declared predicted bulk Doppler, integer seed 1729 for a named
versioned generator, a 0.05° pointing offset toward local north at A and toward
local east at B, and the epoch-selection procedure above. Record the resulting
epoch, Earth-orientation/ephemeris versions and generator implementation; the
integer seed alone cannot guarantee cross-library bit identity. The unspecified
other-polarization branch remains a separate sensitivity input rather than an
invented calibrated measurement.

The synthesized transmitted complex waveform has unit mean power because the
per-cell physical power weights already include transmitter power. If instead
the waveform is represented in watts, remove transmitter power from the channel
weights. Retain and report the bulk correction phase and residual; apply the
correction before sampling into the 12 kHz complex stream, or use an intermediate
rate that safely covers the uncorrected shift. The lunar bulk shift can exceed
the 6 kHz Nyquist limit, so sampling the uncorrected signal at 12 kHz first would
alias it. Verify that the residual signal spectrum fits the chosen rate. Report
the other polarization as unavailable unless its own explicit sensitivity model
and normalization are supplied.

# Parameterized scenario specifications: sub-9 kHz

This chapter turns the evidence in [propagation](10-sub9khz-propagation.md),
[antenna/link](11-sub9khz-antennas-links-and-practice.md), and
[noise](12-sub9khz-noise.md) into defined research cases for a later native
harness. It distinguishes three reference planes: an ideal magnetic-field
receiver, a relative electric-field waveguide calculation, and a transmitting
antenna circuit. Their outputs cannot be joined into an absolute link budget
without additional calibration. The numerical controls below can be specified
without pretending that such calibration exists. None has been run through the
modem or qualified against a matched field recording.

Use the [common source, calendar and evidence contract](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract).
It fixes the native source revision, message, keys, random-number construction,
public receiver inputs and completion rule. These choices are candidate
implementations; a validated alternative may improve physical accuracy,
computational cost or both.

## 1. Common inputs and their provenance

| Input | Selected value or law | Evidence and meaning |
| --- | --- | --- |
| Physical carrier | 8,270 Hz; DATA occupied width 0.0625 Hz | Canonical sub-9 kHz fixture in 00; the actual emitted support includes pulse margins. The occupied width is not automatically the receiver's equivalent noise bandwidth. |
| Source and duration | Rate 0.1, added symbols 1, FHSS off, minimum 64 inner coefficients, requested 16 s; actual tile 5,760.4 s | Native geometry in 00. One tile is about 96 min, but the thirteen-bit `A` message and final quiet decisions require much longer. |
| Digital injection | Complex-envelope Fs = 960 samples/s, positive DATA IF 240 Hz, Helper off; apply the source/channel containment guard | Ideal translated branch. This is not direct 8.27 kHz audio or a calibrated sound card. |
| Clock and epoch | Exact sample clock; zero oscillator offset/drift in the base case; 2025-03-20 09:00:00 UTC public epoch | Fixed experimental coordinates. The date does not imply measured weather, geomagnetism or solar geometry. |
| Reference magnetic signal | RMS complex-envelope amplitude 2.47 fT, with separate amplitude multipliers 1, 0.1 and 0.01 | Engineering levels. The first is motivated by the chapter-11 illustrative field, but is **not** obtained from the waveguide Ez solver by an assumed E/H ratio. The lower levels are sensitivity tests, not natural percentiles. |
| External magnetic noise | Separate fixed amplitude densities 20 and 50 fT/√Hz; proper complex Gaussian control, flat across the sampled band | Literature-informed order of magnitude, with estimator/site limits in 12. The Gaussian law and flatness are assumptions. |
| Ideal receiver self-noise | Separate 0 and 5 fT/√Hz Gaussian cases, independent of external noise | Fixed sensitivity choices, not a measured receiver. The two terms remain separately observable. |
| Magnetic receiver transfer | Unit, frequency-independent response in fT at the evaluation plane; digital input is divided by 1 fT | Explicit ideal receiver. There is no invented loop area, antenna impedance or connector-voltage calibration. |
| Circuit branch | 61 m vertical; 122 m horizontal top wire; 2 mm diameter; C₀ = 0.902 nF, effective height 49.8 m; coil Q = 400, ground resistance 30 Ω, tanδ = 0.003 | Chapter 11 electrostatic/lumped calculation, not a measured 200 ft installation. Wire/support/nearby-object corrections are omitted in this selected model and are model discrepancy, not zero measured loss. |
| Waveguide branch | Exactly 1,000,000 m homogeneous path; ground εr = 22, σ = 0.003 S/m; B = 50 µT, dip 65° downward, azimuth 0 relative to propagation | Fixed model inputs. No geographic endpoint pair is asserted to have exactly this range or material profile. |
| Waveguide ionosphere | Night reference h′ = 87 km, β = 0.30 km⁻¹; transition law in §4 | A selected Wait–Spies profile, not a measured ionogram or unique night-time state. |

The random-number domains and twenty paired diagnostic roots are those in 00.
Their finite realizations establish reproducible comparisons; they do not
supply the occurrence probability of a natural condition. The field level,
noise law, transition timing, antenna detuning and interference level are
separate factors unless a case below explicitly couples them.

### Field and source normalization

Let `s[k]` be the complete native emitted stream, including actual DATA halos
and tails. Obtain `mu=emission.report().data_reference.mean_norm` and
`G=emission.report().data_gain`; the emitted DATA's deterministic reference
power is `R_emit=mu G²`. Define `u[k]=s[k]/sqrt(R_emit)` and use
`b[k]=A_B u[k]`, with `A_B` selected from 2.47, 0.247 and 0.0247 fT.
At the pinned source revision `G=1/sqrt(mu)`, so `R_emit=1` up to rounding:
ordinary emitted samples need only the `A_B` multiplier. Applying a second
`1/sqrt(mu)` would double-normalize them. The canonical reference uses fixed
bit-zero bounded quadrature (at most sixteen tiles and 2,048 nodes per tile),
not realized-message RMS. Save the reference's node count, sampled tiles,
relative standard error and reported gains. It is not recomputed from received
samples or changed after a fade. A Helper-enabled comparison scales the entire
composite by the same factor, preserving the native returned DATA/Helper ratio.
Report actual per-slot energy separately; a fixed-bit quadrature reference does
not prove equal energy in every scored aperture.

Use an RMS complex-envelope convention: the corresponding real field is
`sqrt(2) Re{b(t) exp(j 2π fc t)}`. A physical positive-frequency magnetic PSD
`N_B` in fT²/Hz becomes the two-sided complex-envelope PSD `N_B` near the
carrier. Generate external noise as

`n_ext[k] = sqrt(N_B Fs/2) (z₁[k] + j z₂[k])`,

with independent standard normals from domain 2 in 00. Receiver noise uses
domain 6 and its own density. Thus `E|n_ext|² = N_B Fs`; neither quadrature gets
the entire variance. Add the independent receiver term at the ideal receiver
input, after the wanted-path operation. No wanted-path fade is applied to the
receiver-local noise.

For the 2.47 fT reference, external noise alone gives C/N₀ = −18.167 and
−26.125 dB-Hz at 20 and 50 fT/√Hz. Adding 5 fT/√Hz in quadrature gives
−18.430 and −26.169 dB-Hz. The 0.247 and 0.0247 fT cases reduce these figures
by 20 and 40 dB respectively. An *ideal rectangular* 0.0625 Hz noise filter
would add 12.041 dB to C/N₀ to obtain SNR; this is an arithmetic check, not a
claim about native filter ENBW or decoding threshold. Likewise, multiplying
C/N₀ by a 5,760.4 s interval adds 37.605 dB to an energy ratio, but does not
establish independent-bit decoding or a complete-message probability.

Magnetic units must remain explicit. `H = B/μ₀`; 1 fT/√Hz corresponds to
−181.984 dB re 1 A/m/√Hz. The figures 11 and 250 fT/√Hz in chapter 12 are
P.372 plotted hourly-median reference levels, not universal floor/ceiling
bounds. `E = cB` is a plane-wave relation and is not used to calibrate the
magnetic branch from modal Ez or a local interference field.

## 2. Scenario A — stationary, weak-signal magnetic controls

A0 is exactly the magnetic stream above: unit wanted transfer, constant phase,
zero offset and delay, independent external and receiver Gaussian terms, and
no interference, impulses, detuning, clipping or AGC. Enumerate the three
signal levels × two external densities × two receiver densities; do not draw
these twelve settings from an invented population. Use the same underlying
Gaussian sequences across paired comparisons, scaled only by the declared
density. A separate all-noise capture has the same duration and state but no
transmitted DATA, to measure false DATA and false completion decisions.

Use fixed public receiver bounds of 0.001 Hz carrier offset and 0.001 Hz
Doppler span for A0/A1 and the magnetic B/C cases, exact clock/arrival timing,
and zero physical propagation delay; they are declared search inputs, not
realized phase/noise truth. Do not apply a second native Doppler filter in
addition to the specified phase law. The T branch adds its declared 0.2 s
circuit support allowance. A frequency-selective E renderer needs its own
qualified public delay/tail bounds before receiver use.

A0 is a stationary-model control, not a claim that a real midlatitude night
lasts for the complete message. It isolates the modem's behavior at explicit
field-to-noise ratios. It does not require the missing absolute antenna or
waveguide calibration.

A1 adds the following deterministic carrier-phase sensitivity to A0:

`gφ(t) = exp[j Φ sin(2π(t−t_epoch)/7200 s)]`,

with separate Φ = 0, π/2 and π radians. This is a smooth, continuous, two-hour
phase cycle; its maximum instantaneous frequency displacement is
Φ/7200 = 0, 0.218 and 0.436 mHz. Preserve its phase through all slots and
quiet intervals. These values test long integration sensitivity; they are not
a fit to a sub-9 kHz Doppler spectrum. Do not add another independent
Doppler process representing the same phase modulation.

Record input/output PSD and ENBW, actual in-aperture energy, residual phase,
Helper/DATA state where applicable, bit/null/erasure decisions, integrity
outcome, final physical completion, reacquisition, DSP workspace and complete
receiver service/backlog. Only the evaluator may see the signal and channel
truth. A favorable A0/A1 result is conditional on this mathematical law and
the public receiver scope, not a measured communications availability.

## 3. Scenario B — explicit environmental and equipment stresses

These are separable interventions. First run each alone, then the declared
combinations; combining individually severe coordinates does not establish a
naturally occurring worst case.

Define a raised-cosine window `w(t; a, T, r)` to be zero outside `[a,a+T]`,
`[1−cos(π(t−a)/r)]/2` for the first `r` seconds, one until `a+T−r`, and
`[1−cos(π(a+T−t)/r)]/2` during the last `r` seconds. Here `T ≥ 2r`.
All time arguments refer to continuous UTC elapsed seconds, not chunk-local
time.

**B1 — a noise-level excursion.** Start from the 50 fT/√Hz A0 external-noise
case. Multiply its amplitude by `10^[L(t)/20]`, where
`L(t) = 10 w(t; 2025-03-20 11:45 UTC, 1800 s, 60 s)` dB.
The peak amplitude density is 158.114 fT/√Hz. Keep receiver self-noise
unchanged. A paired control uses `L(t)=0`. The 10 dB magnitude is informed by
regional seasonal differences in Gu et al., but this half-hour event and its
ramp are engineering choices; they are not a measured storm jump. Full Gu
methods are now read: its intermittent wideband acquisition and approximately
61 Hz Welch resolution do not calibrate 0.0625 Hz tails or this time history
([12, P47](12-sub9khz-noise.md#references)).

**B2 — a line crossing the DATA band.** Add a deterministic interference
field `i(t) = A_I exp[j 2π∫_{t_epoch}^t Δf(v)dv]`, phase zero at the public epoch, at the receiver
field plane, with `Δf(t) = 0.002 sin[2π(t−t_epoch)/3600 s]` Hz.
Use `A_I = 20 sqrt(0.0625) × 10^(J/20)` fT RMS for separate
J = −10, 0 and +20 dB relative to the 20 fT/√Hz background power in an ideal
0.0625 Hz band. The corresponding amplitudes are 1.581, 5 and 50 fT.
This is an in-band coherent interferer, not a claim that a specific mains
harmonic has that amplitude or crossing probability. To investigate an actual
50/60 Hz comb, use measured fundamental-frequency timestamps and line
amplitudes; harmonic number multiplies the fundamental frequency error, but
does not determine radiated field strength. That empirical replacement is
not supplied by the generic line test.

**B3 — antenna detuning.** Apply the circuit in §5, with separate positive
and negative capacitance trajectories. Let `a=2025-03-20 11:30 UTC` and

- `r_C(t)=0` before `a`;
- `r_C(t)=[1−cos(π(t−a)/600)]/2` for `a ≤ t < a+600 s`;
- `r_C(t)=[1+cos(π(t−a−600)/1800)]/2` for `a+600 ≤ t < a+2400 s`;
- `r_C(t)=0` thereafter.

Then `C(t)=C₀[1+δ r_C(t)]` for δ = +0.001 or −0.001; inductance stays fixed.
The drive voltage stays fixed at the baseline value, so feed power is allowed
to change. There is no hidden retuning servo or constant-power compensation.
For the chosen S2-like circuit, the extrema give approximately −0.086 dB/
−8.234° and −0.094 dB/+8.244° current change at 8,270 Hz. Resonance moves
−4.132 and +4.138 Hz respectively. The sign of the resonance shift is
`Δf/f ≈ −ΔC/(2C)`. These are circuit calculations, not measured wind statistics.

**B4 — synthetic impulses and nonlinear receiver response.** The fully
defined [C1 pulse law](85-scenarios-noise-and-equipment.md#c1-equal-power-independent-and-clustered-impulses)
and front-end controls in 85 are optional separately named extensions. Its
5 ms pulse at 48 ksample/s and specified event energies are wideband controls;
preserve their physical bandwidth and energy when changing sample rate rather
than silently reusing a variance proportional to the new Fs. A 48 ksample/s
complex noise band spanning ±24 kHz cannot simply be relabeled as a real
8.27 kHz magnetic band: a real-audio export must define its positive-RF
passband, conjugate spectrum, anti-alias response and corresponding filter
tails. Keep the uncalibrated extension as a complex-envelope mathematical
control until those mappings are qualified. At this field plane substitute `N_B` in fT²/Hz for the power PSD
and use a field-to-voltage calibration before applying hardware voltage or
power thresholds. Without such a calibration, run only a dimensionless
normalized nonlinearity and label its threshold in field/noise units. Never
apply a −dBm threshold directly to fT samples. A true sferic replay additionally
requires a calibrated broadband waveform, antenna component/orientation,
receiver transfer, arrival times and saturation flags. Gu's verified Figshare deposit has been analyzed in chapter 12; it contains derived amplitudes/spectra but no suitable calibrated high-rate event waveform.

For a defined coupled sensitivity use B1 and B3 with their exact schedules,
and separately B1 plus A1 with Φ=π/2. The deterministic shared clock is the
entire assumed dependence. For an independence discriminator translate B1's
window by exactly +6 h while leaving the other trajectories unchanged.
Neither alternative is called an estimated natural copula. Wanted-path
propagation, local lightning noise and transmitter weather need distinct
physical paths; sharing one fade multiplier among them is not justified.

## 4. Scenario E — selected relative electric-field waveguide reference

This branch provides a specified physical-model comparison without confusing
Ez with the magnetic receiver in A/B. Use
[LongwaveModePropagator v0.4.1](https://github.com/fgasdia/LongwaveModePropagator.jl/releases/tag/v0.4.1),
commit `9231e40d86c201944d08a9100c927bf3ca985105`, as an **offline reference**.
Its low-frequency mode-search change matters at 8.27 kHz: the older triangular
search region could miss modes, and this release uses a rectangular region
below 12 kHz. A pinned version alone does not establish mode completeness.
The modem/channel renderer remains native; Julia is not an operating signal
chain stage.

Use `Transmitter(VerticalDipole(), 8270.0, 1000.0)`,
`GroundSampler(1_000_000.0, Fields.Ez)`, homogeneous `Ground(22, 0.003)`,
`BField(50e-6, deg2rad(65), 0)`, and electrons only with the documented
`waitprofile(z,h′,β; cutoff_low=0, threshold=1e12)` and
`electroncollisionfrequency(z)`. The
[scenario interface](https://fgasdia.github.io/LongwaveModePropagator.jl/stable/generated/basic/)
does not use transmitter latitude/longitude for this distance calculation.
The 1,000 W setting is a common normalization for ratios, **not** the 100 W
feed power of the 61 m antenna. No direct relation between those power
conventions is assumed.

The executed baseline used `LMPParams()` and the default mesh at that pinned commit; the convergence reference below tightens its stated numerical controls. Record returned fields/dependency versions when reproducing it.
Material defaults include top height 110 km, Earth radius 6,369 km, curvature
on at reference height 50 km, exact susceptibility, root tolerance 10⁻⁵,
Vern7 reflection integration tolerance 10⁻⁵, 2,049 wavefield heights and
Tsit5 wavefield tolerance 10⁻⁶. Radiation-resistance correction is off. The
[pinned source](https://github.com/fgasdia/LongwaveModePropagator.jl/blob/9231e40d86c201944d08a9100c927bf3ca985105/src/LongwaveModePropagator.jl)
and [public API](https://fgasdia.github.io/LongwaveModePropagator.jl/stable/lib/public/)
resolve the remaining defaults. The executed environment is recorded below; a moving `stable` documentation URL alone does not reproduce it.

E0 holds `(h′,β)=(87,0.30)` everywhere. E1 uses exactly

`u=(t−2025-03-20 12:00:00 UTC)/1800 s`,

`h′(t)=80.5−6.5 tanh(u) km`, `β(t)=0.40+0.10 tanh(u) km⁻¹`,

uniformly along the path. This approaches `(87,0.30)` before and `(74,0.50)`
after the transition. It is a single synthetic transition, **not** the
computed sunrise of a geographic route, a natural multiday cycle, or an
inferred solar-angle law. It stays in its asymptotic day state for the rest
of the long message. Replacing it by a solar-driven segmented path would be
a different, separately validated case.

Let `E_ref` be E0's complex Ez at 8,270 Hz and 1,000 km. Calculate
`G_E(t)=E_E1(t)/E_ref`; retain raw complex values and mode lists. If the
reference field is zero or numerically ill-conditioned, reject this
normalization rather than clipping it. The initial mathematical comparison
uses `y(t)=G_E(t)x(t−d/c)` with `d/c=3.335641 ms` and no additional carrier-phase
rotation for that delay. The ratio already defines the carrier-phase change;
the explicit envelope delay is the selected vacuum-delay reference. This is
a **narrowband relative-transfer approximation**: it does not claim that
waveguide group delay is d/c or that all pulse-margin/Helper frequencies see
the same gain. Absolute electric-field noise and receiver calibration remain
separate inputs before an absolute E-field link trial.

For time interpolation start on a 60 s grid, covering the complete capture
and prior support. Linearly interpolate the real and imaginary parts rather
than extrapolating an unwrapped phase through a modal null. At E0, the E1
midpoint and both transition shoulders (`u=−2,+2`), compare against 30 s and
15 s grids and solve directly at midpoint holdouts. Require complex error
`|G_interp−G_direct| ≤ 0.01 max(1,|G_direct|)`; separately report amplitude
and phase errors away from `|G_direct|<0.01`. These are numerical acceptance
coordinates, not empirical error bars. Failure leads to local refinement or
an explicit unresolved numerical result, never a phase reset.

Before using this scalar approximation for the full native source, evaluate
frequency dependence at the carrier, both occupied-band edges, and the
declared source guard edges. These are computational bounds, not hard spectral support: the finite shaped source has nonzero spectral tails. Refine each interval at its midpoint
until the same complex-error criterion is satisfied against direct solutions;
no default dense sweep is prescribed. A frequency-selective branch must use
the resulting `G_E(t,ν)` on the **single composite waveform**, with continuous
filter state and declared interpolation/tail bounds. It must qualify causal
rendering, group delay, carrier convention, spectral containment and delayed
tails before receiver use. If edge frequencies leave the validated model
band, report the branch unavailable rather than extrapolating the 8.27 kHz
model. The rectangular-mesh mode search should also be repeated with halved
mesh spacing, a widened search boundary and tolerances one decade tighter at
these representative states; inspect newly found mode contributions and
complex-field changes. The bounded solver checks below were executed. Causal native rendering and source-tail qualification remain implementation checks; numerical convergence is not field validation.

### Executed waveguide checks and a preserved numerical failure

The reference calculations used Julia 1.10.10 and LMP 0.4.1 at the commit above. Algorithm dependencies were OrdinaryDiffEq 6.92.0, OrdinaryDiffEqVerner 1.4.0, OrdinaryDiffEqTsit5 1.3.0, RootsAndPoles 2.0.0, StaticArrays 1.9.25, ModifiedHankelFunctionsOfOrderOneThird 1.1.3, Romberg 0.2.0 and Interpolations 0.15.2; SciMLBase was 2.77.2, DiffEqCallbacks 3.9.1 and LinearSolve 2.39.1. The resolved Manifest SHA-256 was `b0eafc39159c50a9de2aeb63f4232f3b9ecbb691676d0ef284c3e8740a7636fe`. This is reference research, with no native receiver or deployed channel implementation. The initial RAM-disk precompile failed with `No space left on device`; after relocation to disk and removal of this run's incomplete cache, package loading and the calculations succeeded. That setup failure performed no numerical assertions.

The default 8.27-kHz search is the rectangular eigenangle region Re θ = 1–89.9°, Im θ = −89–0°, with 1° coarse spacing (9,315 input nodes). The widened comparison uses Re θ = 0.1–89.99°, Im θ = −89.9–0°, also 1° (9,510 nodes); its half-spacing check has 37,739 nodes. Tight controls change root tolerance to 10⁻⁶, reflection integration tolerance to 10⁻⁶ and wavefield tolerance to 10⁻⁷; no tolerance was relaxed. The original E0, midpoint and day calculations returned 18, 16 and 17 modes. Widening/tightening returned 18, 17 and 19. The E0 sets share 17 roots and replace one deep root; widening is **not** a proven superset. Returned roots were retained in each run, without claiming that every physical root was found.

The original default E0 field was approximately −204.92 + 332.24i µV/m; midpoint/E0 was 1.24919 − 0.16103i (+2.004 dB, −7.345°), and day/E0 was 1.36889 + 0.44756i (+3.168 dB, +18.105°). Halving the original E0 mesh changed the complex field by 1.95×10⁻⁴ relative. **The original widened E0 sum failed with NaN**, although its 18 returned eigenangles were finite. This failure is retained: it is not a successful unmodified upstream run.

The cause is intermediate Hankel-function overflow in the deepest returned mode, not evidence of an infinite received field. For θ ≈ 0.046129305 − 1.554635363i, the asymptotic exponential has real exponent −717.53364; its reciprocal exceeds Float64's approximately 709.78271 logarithmic limit. The propagation attenuation exponent is −394.59221. LMP's [modal source](https://github.com/fgasdia/LongwaveModePropagator.jl/blob/9231e40d86c201944d08a9100c927bf3ca985105/src/modesum.jl) forms individually large height and excitation factors that cancel at zero height.

For this **homogeneous, curved-Earth, zero-height vertical transmitter and zero-height Ez receiver only**, let S = sin θ at the curvature reference, S₀ = sin θ₀ after `referencetoground`, and let `(D′,R,Rg)` be the unchanged `solvedmodalequation` outputs. The ground-to-ground modal excitation/height product simplifies exactly to

`P = S₀² √S (1+Rg₁₁)² (1−R₂₂ Rg₂₂)/(D′ Rg₁₁)`.

In the original source, the excitation denominator is `(F₁h₁+F₂h₂)²`, while both ground height gains equal `F₁h₁+F₂h₂`; cancellation gives this expression without calculating either overflowing factor. Keep the original complex square-root branch, ground reflection matrices, phase and normalization. An independent equation and arithmetic review confirmed the identity. Across 106 finite original modal terms, the maximum absolute difference was 1.28×10⁻¹³ µV/m and maximum relative difference 8.37×10⁻¹⁶. The two originally NaN terms instead give finite contributions of magnitude about 2.5×10⁻¹⁶⁹ µV/m. **Every returned mode is included**, including these terms. This is a separately checked equivalent reference evaluation, not a general LMP fix or a license to discard modes, change fields, disable curvature, alter source height, or infer a segmented-waveguide result. Require finite nonzero `D′*Rg[1,1]`.

The following portable Julia fragment specifies that evaluation; use the package/dependency versions above and the stated profiles. `tight=false, wide=false` reproduces the baseline numerical controls; `spacing=0.5` selects the mesh comparison. The unchanged upstream `propagate(guide,tx,GroundSampler(d,Fields.Ez); modes=modes,params=p,unwrap=false)` is the comparison that must retain its NaN failure where present.

```julia
using LongwaveModePropagator
const LMP = LongwaveModePropagator
function ground_reference(h, beta; f=8270.0, spacing=1.0,
                          tight=true, wide=true)
    d = 1e6
    tx = Transmitter(VerticalDipole(), f, 1000.0)
    e = Species(LMP.QE, LMP.ME,
        z -> waitprofile(z,h,beta;cutoff_low=0,threshold=1e12),
        electroncollisionfrequency)
    guide = HomogeneousWaveguide(BField(50e-6,deg2rad(65),0),
                                 e,Ground(22,0.003))
    p = LMPParams()
    if tight
        p = LMPParams(p; grpfparams=GRPFParams(100000,1e-6,true),
            integrationparams=IntegrationParams(p.integrationparams;tolerance=1e-6),
            wavefieldintegrationparams=IntegrationParams(
                p.wavefieldintegrationparams;tolerance=1e-7))
    end
    mesh = wide ? LMP.defaultmesh(f;Δr_coarse=deg2rad(spacing),
                rmin=deg2rad(.1),rmax=deg2rad(89.99),imin=deg2rad(-89.9)) :
                LMP.defaultmesh(f;Δr_coarse=deg2rad(spacing))
    modes = findmodes(PhysicalModeEquation(tx.frequency,guide),mesh;params=p)
    Q = .6822408*sqrt(f*1000)/sqrt(abs(sin(d/p.earthradius)))
    terms = ComplexF64[]
    for ea in modes
        df,R,Rg = LMP.solvedmodalequation(
            PhysicalModeEquation(ea,tx.frequency,guide);params=p)
        S = ea.sinθ
        S0 = referencetoground(ea;params=p).sinθ
        P = S0^2*sqrt(S)*(1+Rg[1,1])^2*(1-R[2,2]*Rg[2,2])/(df*Rg[1,1])
        push!(terms,Q*P*cis(-tx.frequency.k*
            (referencetoground(S;params=p)-1)*d))
    end
    return sum(terms), modes, terms
end
```

The root triangulation dependency shuffles insertion order. Repeated solves can differ within root/integration error; bit-for-bit root lists are not promised by a fixed profile and mesh. Report complex-field changes and roots, not just a printed decimal match. The finite-term comparison above is performed with identical roots, so this variation cannot explain away or manufacture the algebraic agreement.

The selected response checks comprise 52 solves for profiles, meshes, time and initial frequency holdouts, followed by four locally refined frequency solves. The 1° widened/tight E0 reference in that run was `−204.85024+332.09227i µV/m`; its finite magnitude makes the declared normalization usable. Small final-digit differences from earlier runs reflect the root-search behavior above.

| Profile at 8,270 Hz | Returned modes, 1° → 0.5° | Relative complex-field change | Amplitude / phase change |
|---|---:|---:|---:|
| Night 87/.30 | 18 → 19 | 2.24×10⁻⁵ | −0.000021 dB / +0.00128° |
| Midpoint 80.5/.40 | 17 → 18 | 2.50×10⁻⁶ | +0.000009 dB / −0.00013° |
| Day 74/.50 | 19 → 19 | 7.35×10⁻⁶ | +0.000045 dB / +0.00030° |
| Shoulder u=−2 | 19 → 19 | 1.68×10⁻⁵ | +0.000132 dB / +0.00041° |
| Shoulder u=+2 | 19 → 19 | 1.78×10⁻⁵ | −0.000098 dB / −0.00079° |

For each time holdout `t=t₀−3600,t₀,t₀+3600 s`, directly solve the stated profile at t and at `t±Δt/2`; compare the average endpoint complex field with the direct field. The normalized error is `|G_interp−G_direct|/max(1,|G_direct|)`, whose acceptance limit remains 0.01. E0's constant-profile interpolation is exact algebraically. The non-monotonic tiny shoulder errors below are comparable to numerical search variability, rather than evidence that temporal refinement worsens the physical model.

| Time holdout | 60-s grid error | 30-s grid error | 15-s grid error |
|---|---:|---:|---:|
| u=−2 | 2.67×10⁻⁶ | 1.41×10⁻⁵ | 3.53×10⁻⁵ |
| u=0 | 0.003067 | 0.001484 | 0.000434 |
| u=+2 | 2.34×10⁻⁵ | 1.32×10⁻⁵ | 3.71×10⁻⁶ |

The largest 60-s amplitude/phase interpolation errors were −0.0221 dB and −0.0988° at u=0. These representative holdouts pass; they do not certify every possible time or an untested geographic sunrise trajectory.

The inspected native source at revision `280b32e47af6f2066fcc44a8966d6af0b220d706` uses `Bocc/2 + 1/ramp_seconds + 200 Hz` for its guard (`src/triad_stream.cpp`, `Emission::source`, lines 476–486 and constant at line 17). With Bocc=0.0625 Hz, ramp=0.02 s and Helper off, this gives physical RF guard edges **8,019.96875 and 8,520.03125 Hz**. The finite RRC/message source is not compactly bandlimited (`src/triad_shaping.cpp`, line 246); guard edges are computational bounds with residual tails, not complete hard support.

Initial frequency knots were `[8019.96875,8269.96875,8270,8270.03125,8520.03125] Hz`; solve each adjacent midpoint directly. Use the same E0 **carrier** reference and LMP relative-vacuum phase convention at all frequencies. At the three night/mid/day profiles, occupied-edge field differences from the carrier were ≤3.59×10⁻⁵ relative (≤0.00028 dB and 0.00154°). Those values are near solver variability and cannot establish ultra-precise group delay. The full guard has materially larger variation:

| Profile | Low guard / carrier, amplitude and phase | High guard / carrier, amplitude and phase | Largest initial interpolation error |
|---|---:|---:|---:|
| Night | −0.395 dB / −1.492° | +0.581 dB / +1.849° | 0.01057 **fails** |
| Midpoint | +0.417 dB / +6.248° | −1.065 dB / −2.085° | 0.02714 **fails** |
| Day | −0.070 dB / +6.708° | −0.124 dB / −5.761° | 0.00687 |

For the two failing upper intervals, retain the direct midpoint at 8,395.03125 Hz as a new knot and check 8,332.53125 and 8,457.53125 Hz. The child-interval errors were 0.00902/0.00318 (night) and 0.00976/0.00597 (midpoint), all below 0.01. No threshold was relaxed and the original failures remain recorded. These are tested interval-midpoint errors, not a uniform mathematical supremum over frequency. The carrier-only scalar approximation is acceptable as the specified very narrow occupied-band control; it does **not** qualify the full guarded composite waveform. Native frequency/time interpolation, causal filtering, reference delay and nonzero spectral tails still require implementation checks. A common two-dimensional time/frequency grid also needs its own holdouts.

A separate 16-solve domain comparison used the same geometry, ground and magnetoionic inputs at 1/3/6/9 kHz, night/day profiles, widened rectangular 1°/0.5° meshes and tight tolerances. The official low-frequency rectangular mesh applies below 12 kHz; no high-frequency wedge was used at 1 kHz. Each returned root set was evaluated at 100/1,000/3,000 km without another root search. The 100-km value is a numerical mode-sum comparison, not validation of near-field completeness; 9 kHz is a boundary check, not strictly below 9 kHz.

| Frequency / profile | Returned roots, 1° → 0.5° | Largest relative complex-field difference over three ranges |
|---|---:|---:|
| 1 kHz / night | 3 → 3 | 6.28×10⁻⁶ |
| 1 kHz / day | 3 → 2 | **0.501 — fails** |
| 3 kHz / night | 9 → 9 | 2.78×10⁻⁵ |
| 3 kHz / day | 6 → 6 | 5.04×10⁻⁵ |
| 6 kHz / night | 14 → 14 | 2.46×10⁻⁵ |
| 6 kHz / day | 13 → 13 | 2.42×10⁻⁵ |
| 9 kHz / night | 21 → 21 | 4.76×10⁻⁶ |
| 9 kHz / day | 21 → 21 | 4.91×10⁻⁶ |

The 1-kHz/day failure is a root-census defect, not a newly inferred physical mode or a demonstrated frequency-domain limit. The failing list contains `1.4164281059−0.4554907696i` and `1.4164229223−0.4554866308i`, only 6.6333×10⁻⁶ rad apart, plus one deep mode. The half mesh returns one nearby root and the deep mode. The pinned `findmodes` implementation rounds roots to five decimal places at root tolerance 10⁻⁶ before applying `unique!`. These two candidates straddle a rounding-bin boundary (`1.41643` versus `1.41642`), defeating the intended near-duplicate exclusion. The original 1,000-km field was `442.75301−304.99674i µV/m` (54.60978 dBµV/m); half spacing gives `221.38311−152.48699i` (48.58915 dBµV/m). No candidate was silently deleted to turn this failure into a pass.

One additional widened 1° solve with root/reflection tolerances 10⁻⁷ returns two roots and `221.36752−152.51934i` (48.58932 dBµV/m). For each of the two coarse candidates and the half-mesh candidate, an actual-helper Newton discriminator uses `F=modalequation(R,Rg)` and `θ_next=θ−F/D′`, with `(D′,R,Rg)=solvedmodalequation(θ,me)` and reflection tolerance 10⁻⁹. All three reach approximately `1.41643075−0.45549149i` after one step; later steps jitter within about 1.7×10⁻⁸ rad rather than converging to arbitrarily exact decimals. Separate `solvemodalequation` residuals remain of order 10⁻⁶ because the two integration routes have finite error. This is consistent evidence for a shared root, not by itself a proof of multiplicity.

Independent checks strengthen the diagnosis: 48-point determinant contours centered on `1.41642279074−0.45548663076i`, enclosing both candidates at radii 2×10⁻⁵ and 6×10⁻⁵ rad give winding number +1 at reflection tolerances 10⁻⁶, 10⁻⁸ and 10⁻¹⁰ (largest sampled phase increment 0.692 rad, no sampled zero). The official **unwidened** 1° and 0.5° meshes both return two roots for this same profile; tight 1,000-km amplitudes are 48.589169 and 48.589168 dBµV/m, with relative complex-field difference ≤2.16×10⁻⁶ over the three ranges. A second profile from the official 1-kHz example, h′=75 km, β=.35 km⁻¹, vertical B and Ground(15,.001), also returns two roots with those meshes. These are corroborating counterchecks, not an all-root proof. A scan of pair separations below 10⁻⁴ rad in all 84 archived pilot, identity, convergence, refinement, domain and diagnostic solves found only the failing pair; no such pair occurred in the selected 8.27-kHz checks.

The original failed result remains rejected. A future robust root-census implementation must compare distances/residuals and preserve genuinely distinct nearby roots; decimal-bin rounding is inadequate. No upstream library code or native modem was changed here. The broader research check is completed with a diagnosed failure and independently corroborated reference values; it is not a claim that unmodified LMP passes every mesh or profile.


These finite comparisons are useful counterexamples and numerical controls, not an independent propagation validation. In particular they cannot resolve Taylor's below-4-kHz measurements versus the older profile theories, select a natural day/night attenuation law, establish global root completeness, or justify extrapolating the 8.27-kHz scenario to the entire band.


## 5. Scenario T — transmitting-antenna circuit and Helper limits

The selected electrical model is a series RLC, with no wire resistance beyond
the explicitly modeled loss terms. For ω₀=2π·8270, set

`R_rad = 160π²(h_e fc/c)² = 0.00298022 Ω`,

`X₀ = 1/(ω₀C₀) = 21335.758 Ω`, `L = 1/(ω₀²C₀) = 0.41060356 H`,

`R_coil = X₀/400 = 53.33940 Ω`,

`R_diel(t) = tanδ/[ω₀ C(t)]`, `R(t)=R_rad+R_coil+30+R_diel(t)`.

At C₀, R = 147.34965 Ω, Q_loaded = 144.7968, radiation efficiency is
2.02255×10⁻⁵ (−46.941 dB), and a 100 W sinusoidal feed gives 0.823807 A RMS,
17.5765 kV RMS across C and 2.02255 mW e.m.r.p. This is an idealized
single-frequency result; it is not a measured efficiency, safe operating
limit or authorization to transmit. The chapter-11 modeled 29 kV corona
onset has unmeasured end/support/weather corrections and is not used as a
certified limit.

For a complete electrical response solve continuously

`dq/dt = i`, `L di/dt = v(t) − R(t)i − q/C(t)`,

with source voltage `v(t)=sqrt(2) V₀ Re{u(t) exp(jω₀t)}` and
`V₀=sqrt(100 R₀)=121.38766 V RMS`. This defines both electrical memory and
detuning; it must not be replaced by a blockwise phase draw. Initial charge
and current are zero before the earliest native source support. Keep state
through gaps, the final silence, and a declared 0.2 s circuit tail. The
stationary free-response envelope time constant is `2L/R=5.57319 ms`; a
0.2 s continuation reduces that envelope by about 2.6×10⁻¹⁶. This is a
model-tail check, not a bound on an unmodeled real antenna's modes.

A candidate native reference discretization is RK4 with a maximum substep
`1/(16 Fs)` on the 48,000 samples/s direct physical-band branch, evaluating
the analytic C(t) at each stage and linearly interpolating the voltage samples.
Halve the substep and double the source rate on short steady, detuning and
turn-off holdouts before accepting the interpolation/integration error.
An exact state-space or envelope implementation can later replace this
expensive reference when it reproduces the specified transfer and memory.
The bounded circuit checks below were executed against an analytic sinusoidal source; a complete sampled native waveform has not been rendered through the circuit. The simple instantaneous
ratio `R₀/[R(t)+j(ω₀L−1/(ω₀C(t)))]` supplies the B3 slow-detuning arithmetic
checks only; it is not the full circuit's transient response.

### Bounded circuit checks executed in the research pass

The constant-C equation was integrated with ordinary RK4 at `dt=1/(16×48000)` and half that step. A 20-ms cosine turn-on from `q=i=0` was compared at every step with the exact solution: `i_p=I_pk cos(ω₀t)`, `q_p=I_pk sin(ω₀t)/ω₀`, plus the homogeneous solution `exp(−at)[A cos(ω_dt)+B sin(ω_dt)]`, where `a=R/(2L)`, `ω_d=sqrt(1/(LC)−a²)`, `A=0`, `B=−I_pk/ω_d` and `I_pk=sqrt(2)V₀/R`. Normalize current error by `I_pk` and charge error by `I_pk/ω₀`; the maximum is 4.414×10⁻⁵ and 2.759×10⁻⁶, respectively. Their ratio is consistent with fourth-order convergence. An independent 20-ms **source-off** continuation initialized from the exact turn-on state has normalized endpoint errors 4.691×10⁻⁶ and 2.908×10⁻⁷. This tests decay as well as driven steady response.

A deliberately faster-than-B3 holdout increases C linearly by 0.1% over 20 ms. The two RK4 runs end at `(q,i)=(1.4495281×10⁻⁵ C, −0.8426729 A)` and `(1.4494666×10⁻⁵ C, −0.8427087 A)`, a normalized endpoint difference below 3.1×10⁻⁵. That is a convergence comparison, not a measured weather model or exact reference. These short research integrations contain no modem DSP or receiver state.

The voltage reconstruction needs its own correction: **linear interpolation of 48-ksample/s voltage is not transparent at 8.27 kHz**. Its fundamental response is `sinc²(πf/Fs)`, giving 0.906078 (−0.8567 dB) at 48 ksample/s and 0.975823 (−0.2126 dB) at 96 ksample/s. Reducing only the ODE substep does not remove this bias or the interpolation images. Therefore the original 48-kHz linear-interpolation candidate is not accepted as a high-fidelity source interface. A later native implementation must use qualified band-limited reconstruction or demonstrate convergence of the complete source at higher rate, including images, occupied support and tails. The analytic-driver checks qualify the RLC integration alone.

With the declared constant-R approximation, normalized current at 550/950/2400 Hz is −66.72/−61.90/−53.20 dB relative to resonance; at 8,720 Hz it is −23.74 dB. At 8,270±0.03125 Hz it differs by only −0.0000052 dB with phases ∓0.06270°. These are exact impedance calculations, not calibrated multi-frequency coil losses. They explain why a narrow DATA passband may survive the circuit while distant physical Helper frequencies do not. The circuit cannot be applied only to DATA and called a complete transmitted link.

The ordinary A/B magnetic controls do not automatically inherit this circuit.
A hybrid control may multiply their wanted signal by the circuit's normalized
current response, but must be labeled as an imposed TX sensitivity with an
independently assumed received field. To establish a physical absolute link,
calibrate current moment, effective height, radiation/ground losses, modal
normalization, receive component and receiver transfer jointly. Applying
100 W to the circuit and assigning 2.47 fT at a receiver does not accomplish
that calibration.

Helper remains off in the base case. A direct 48 ksample/s Helper experiment
must use the actual native returned tone placement and powers: low and
auxiliary pilots are 550 and 950 Hz, while the high pilot is chosen above the
DATA band (at least about 8,720 Hz in this 8.27 kHz configuration, with any
further leakage-avoidance increments). It is **not** fixed at 2,400 Hz here.
The tuned 8.27 kHz T is not flat across those frequencies. The single-frequency
loss constants and 8.27 kHz propagation reference above do not validate a
550 Hz antenna/waveguide transfer. A mathematical multi-tone circuit control
can retain its declared lumped elements, but a physical Helper-link claim
requires frequency-dependent coil/ground/dielectric losses, effective height,
source power division and a suitable low-frequency propagation model. Reject
an invalid physical-band configuration rather than relocating Helper tones
or hiding their attenuation.

## 6. Scenario C — fades, physical outages and completion

C1 starts from A0 at 50 fT/√Hz and multiplies only the wanted field by
`g(t)=1−(1−a_min)w(t; 2025-03-20 11:45 UTC,1800 s,60 s)`, for separate
`a_min=10⁻³` and 0. Noise, phase evolution, oscillator/circuit/filter state
and decoder state continue. This thirty-minute fade is a deterministic
interruption within a very long bit; it does not test two consecutive
quiet-slot decisions by itself.

C2 uses the thirteen-bit common fixture and its interior slots 6 and 7,
with later DATA retained. Compute the union of all source and receiver
contributing apertures for the two slots from the actual native calendar,
including source support, channel delay and selected filter tails. Set the
wanted gain to zero on that union's containing interval; begin a 60 s
raised-cosine descent before the interval and end a 60 s ascent after it.
If a nonzero deep-fade comparison is desired, replace zero by 10⁻³ on the
same interval. The zero branch is fully specified without an unknown
“receiver threshold.” The nonzero branch measures its actual decisions; it
must not be declared below threshold without evidence.

Apply the fade before the earliest contributing sample, with public delay
bounds appropriate to the selected operator: zero for A0/A1; d/c plus the
qualified renderer's tail for E; 0.2 s circuit tail for T. Do not approximate
two scoring apertures by a thirty-minute interruption: sub-9 kHz no-wait
recurrence is 6,750 s in the common fixture. Keep channel evolution continuous
through the entire interior fade and subsequent real message end. The first nominal final quiet slots are 13 and 14, but do not assume either
is wholly quiet when delayed signal or filter tails still contribute. Following
00/86, the evaluator finds `q0`, the first public slot whose entire scoring-input
window begins after the last nonzero source sample plus the maximum propagation
support and filter tail. Render through the complete window of `q0+2`, including
native lookahead: three final quiet opportunities are observed, while successful
physical completion still requires only two scored quiet slots. The receiver
is not told `q0`, the interior fade, active-slot count or final endpoint.

Count quiet/DATA/erasure outcomes, accepted truncations, integrity rejections,
premature completion, later reacquisition and eventual complete-message
result. Two quiet decisions caused by a deep fade can be observationally
indistinguishable from physical transmission end; zero premature completion
is not a physically justified universal requirement. Transport EOF,
cancellation, quotas and an evaluator stop time are never successful physical
completion. This is a contract/stress experiment, not an estimated natural
outage rate.

## 7. Validation, uncertainty and remaining empirical dependencies

The required evidence is deliberately staged:

1. Check the source/PSD/unit conventions, deterministic trajectories, circuit
   impedance, tail bound and zero-effect limits with short component records.
   Check a deliberate chunk-boundary change leaves the same physical sequence.
2. For E, preserve the bounded solver checks and failures in §4; complete
   native rendering and full-source/tail checks before receiver use. Preserve convergence failures and model
   alternatives separately from stochastic receiver variability.
3. Test single mechanisms before the explicitly paired combinations. Carry
   all configured public hypotheses and complete receiver costs. A short
   single-bit check cannot qualify a thirteen-bit message, and independent
   simultaneous simulations are not same-stream receiver throughput.
4. Use the bounded statistical design in
   [86](86-statistics-and-calculation-checks.md). Report conditional simulation
   intervals only for the declared stochastic law. Natural availability needs
   independent field episodes, selection/censoring information and a justified
   joint population; twenty fixed diagnostic seeds do not provide it.

The arithmetic in §§1, 3 and 5 and the circuit integration checks in §5 were executed from the written formulas. No native receiver trial or empirical waveform replay was executed. The separate waveguide numerical status is recorded in §4. Implementation readiness of
a mathematical control is distinct from an implemented harness and from
empirical qualification.

The missing empirical links are now specific: a calibrated 50–100 m
sub-9 kHz electric antenna's current/efficiency and detuning time series;
matched complex wanted-field and receiver-noise recordings on long apertures;
receiver effective area/height, component orientation, transfer and self-noise;
sub-9 kHz marked sferic/line amplitudes and timing; and independent
geometry/season/receiver holdouts. Gu's Figshare v1 files and CC BY 4.0 licence are verified; chapter 12 now analyzes all numeric arrays. Its sub-9-kHz products are regional monthly/hour-of-day log-band amplitudes. The saved minute workspace is labelled 28 kHz, and the deposit supplies no raw coherent capture or traceable transfer-calibration record. Reuveni's 75 Hz,
one-minute records sampled every fifteen minutes and Gu's intermittent
wideband measurements cannot be silently resampled into an observed
0.0625 Hz joint process. Practitioner successful decodes are useful
conditional feasibility records but generally lack a full trial denominator.
The selected LMP branch also does not resolve the below-3–4 kHz model
contradictions in chapter 10 or supply its undocumented magnetic component.

Source access on 13 November 2026 for this scenario specification comprised
the official LMP v0.4.1 release, basic scenario documentation, public API and
pinned main source, inspected to settle the model/version/interface choices.
The recent literature searches, measurement conditions, contradictory findings
and individual inaccessible-source dispositions remain in 10–12. The previous
open choices of waveform, transition date/law, reference range, solver version,
noise law, detuning trajectory and fade schedule are replaced above by explicit
controls. The remaining calibration and validation dependencies are not
web-search-budget notices and are not claimed to have been experimentally
resolved.

## 8. Research dispositions and dependency map

These dispositions refer to the V01–V27 inventory in [87](87-evidence-remediation-audit.md). “Research resolved” means the cited reading, analysis, or explicitly limited model choice is documented; it does not mean the natural environment is known exactly. **E** denotes a required external measurement/calibration for an empirical claim; **I** denotes later native harness/receiver work. A remaining theoretical or numerical research question is named as such rather than reclassified as a new measurement.

| ID | Completed research result | Exact residual and permitted conclusion |
|---|---|---|
| V01 | Taylor's 2–4-kHz low-reliability attenuation estimates are separated from the classical anisotropic/isotropic calculations, Barr's resolution alternative and modern RT/FDTD comparison in 10. | Physical discrepancy remains; no universal low-band attenuation is selected. Barr's full paper remains unread, with no adopted numerical parameter depending on it. A calibrated low-band comparison is E; the table is an explicit theory/observation bracket. |
| V02 | Exact LMP v0.4.1 source/API, magnetic inputs and dependency environment established. | The executed checks in §4 retain the NaN and the separate 1-kHz duplicate-root failure, with independently checked diagnoses/reference evaluations. Selected mesh/time/frequency holdouts pass; global mode completeness and physical accuracy remain unproved. Root-census hardening is I, not an adopted fabricated propagation law. |
| V03 | Vector E/H→sensor transfer, loading and covariance chain derived in 11; public LMP electric sampler limitation verified. | Magnetic modal-sum interface is I; calibrated sensor/field covariance is E. E0 is not silently converted to B. |
| V04 | Source/feed/EMRP/ERP and receiver reference conventions reconciled; worked budgets now label P.368-style loss below 10 kHz as an equivalent coordinate outside the recommendation's range. | Absolute calibrated link/range is E, including mode excitation, field component and receiver transfer. |
| V05 | Existing 91-m measurements and full Siple primary article are compared with the 61-m electrostatic/circuit estimate. | No matched measured 50–100-m sub-9-kHz efficiency supplied by these sources. Measurement is E; modeled 2.02-mW result is not empirical efficiency. |
| V06 | Reference T dimensions and distinct bare/umbrella/lofted/ground-wire alternatives are explicit in 11 Tables A–E; selected C/he independently agree within ≈1%. | Site structure/soil/nearby objects are E; no “200 ft” scalar uniquely fixes an antenna. |
| V07 | Wheeler and Moore–Blair primary equations plus an independent reciprocal boundary-field derivation resolve the factor 2 for the stipulated short uniform-current good-conductor limit (11). | Maxwell surface asymptote is preferred in that limit; Wheeler remains a historical approximation. Finite-length/current, electrodes, layering and measured efficiency are E/model-domain dependencies. No ground-wire superiority or real range is certified; selected vertical T is unaffected. |
| V08 | RLC/loss/bandwidth arithmetic and exact driven/turn-off integration checks executed; distant Helper attenuation computed. | Sampled-voltage reconstruction fails transparency at 48 kHz with linear interpolation; complete source interface/native implementation is I. Real frequency-dependent losses/matching are E. |
| V09 | Deterministic slow and fast detuning laws are distinguished from the source geometry's measured weather effects; circuit fast-ramp convergence checked. | Joint C/loss/corona/weather distribution and installation limits are E. No probability is attached to the stress schedule. |
| V10 | Long-aperture operator records and their averaging/selection limits retained; phase-rate calculations remain conditional. | Matched raw phase/noise/oscillator traces needed for natural coherence and fade fits are E. A successful decode is not an outage denominator. |
| V11 | Full Thomson (2025) dawn/dusk and McCormick–Cohen (2021) split-profile methods/results read; frequency/path dependence and inverse-model selection made explicit. | E1 is a fixed two-parameter diagnostic; empirical correlated profile uncertainty is E. A ledge/SZA-driven alternative is documented, not asserted universally optimal. |
| V12 | Deterministic shallow/deep/null and outage cases cover failure mechanisms without inventing fade probabilities. | Natural tails, event durations and rates are E; complete-message/final-quiet behavior is I. |
| V13 | Existing flare/storm/eclipse evidence separated by frequency/path; recent eclipse and non-monotonic dawn findings prevent importing one signed gain law. | No matched sub-9-kHz storm/eclipse carrier population established. Sub9 magnitudes remain conditional sensitivities; measured event qualification is E. |
| V14 | Early/Trimpi mechanisms and the specific 3.79-kHz abstract's limitations remain explicit. | No calibrated general sub-9-kHz event law is inferred; raw matched episodes are E. Abstract-only event detail is not a numerical input. |
| V15 | LMP E0/E1 are correctly identified as magnetoionic; classical and recent directional evidence, ground segmentation and detection censoring are reconciled. | Synthetic reverse-bearing comparison is a separate physical case, not complex conjugation. Site-dependent direction calibration is E. |
| V16 | Ferguson (1998) manual recovered/read, including approximate/full mode conversion and profile/noise defaults; LMP/LWPC/MODEFNDR identities distinguished. | Marshall (2017) remains abstract-only after public-route checks, with no selected numerical input depending uniquely on it. Its quoted agreement is not adopted as a validation tolerance. |
| V17 | Read Wait/Galejs/NBS modal and near-field sources remain the derivation basis; Qin (2019) supplies a qualified low-VLF ray alternative. | Classical lateral/near-field model limits are preserved. Later native implementations require their own numerical checks (I), not new unsourced propagation laws. |
| V18 | Strategic ELF, Siple and heating experiments retain frequency, radiator and measurement scope; full 1974 Siple paper resolves that source omission. | Secondary or abstract-only historical leads are contextual; none sets 61-m efficiency or service probability. Such measurements are E. |
| V19 | Earth-mode/through-earth mechanisms and the separately read Raab treatment remain distinct from Earth–ionosphere propagation. | Matched mine/soil geometry and transfer data are E; the branch is not given an invented waveguide attenuation. |
| V20 | All 28 Gu members extracted; numeric arrays, dates, aggregation, band metadata and seasonal/lightning coupling analyzed in 12. | Accessible-data task resolved. No high-rate sub-9-kHz complex capture or traceable calibration is contained in this deposit (E for replay). |
| V21 | PSD/impulse/line/slow-state observables separated; Gu seasonal-correlation sensitivity executed. | Joint law remains unidentified; factorized/deterministic controls are valid conditional models. Calibrated target-band joint records are E. |
| V22 | Every previously named noise lead has a disposition in 12; original reports rather than unread article details support adopted gains/statistics. | Context-only unread items remain explicitly unread. No Hall/Middleton/ELF parameter is silently promoted to a sub-hertz 8-kHz fit. Target-band refitting is E. |
| V23 | Field→sensor→ADC/self-noise construction and source calibration limitations specified; instantaneous, band-average and plane-wave-equivalent units distinguished. | Transfer calibration, clipping metadata and field component covariance are E; native realization is I. |
| V24 | Books and operator records individually classified; successes, contradictory estimator conventions and limits retained. | Unread contextual books cannot supply unique critical inputs. No unsupported source access is claimed. |
| V25 | Defined A/B/C/E/T inputs, zero controls, dependence alternatives and component acceptance coordinates are recorded; circuit/data analyses executed. | LMP numerical results are in §4; native full-source/receiver and chunk/causal-render checks are I. A mathematically specified case is not an executed modem experiment. |
| V26 | Physical model→native interfaces and rejected interpolation choice are documented. | Native circuit, field renderer, sensor and receiver integration is I, outside this documentation-only scope. |
| V27 | Empirical claim requirements identify calibration, episode denominator/censoring, geometry/season holdouts and uncertainty. | A calibrated new campaign is E. No current table estimates universal service availability or worst-case occurrence. |

| Selected consequential input | Source → calculation | Consumer and boundary |
|---|---|---|
| Native geometry, source normalization, calendars and absence completion | 00's pinned compiled-source inspection → complete composite/support/reference energy | Every scenario; full-source execution and complete receiver evaluation remain I. |
| 2.47/0.247/0.0247 fT and 20/50 fT/√Hz with 0/5 fT/√Hz self-noise | Declared field levels and chapter 12 order-of-magnitude evidence → `C/N0=20log10(A)−10log10(N_ext+N_rx)` | A/B/C are ideal magnetic conditional controls, not field availability percentiles. |
| Independent proper complex Gaussian background | Explicit engineering law → quadrature variance `N_B Fs/2` | A0 statistical control; no claim this is an observed post-blanker law. |
| +10-dB noise ramp, harmonic trace, deterministic pulse/nonlinearity and fades | Literature establishes mechanisms; schedules/amplitudes in §§2–3/6 are chosen → causal factorized trajectories | B/C probe moderate/adverse/severe/outage mechanisms; neither frequency-grid statistics nor Gu correlations set a joint occurrence law. |
| Gu 4/6/8-kHz monthly bands | Verified deposit → 13.05/12.55/10.79-dB seasonal log-amplitude spans and correlation sensitivity | Context for slow-level comparisons only; B2 half-hour trajectory remains synthetic. |
| 61-m T: C/he, Q 400, 30 Ω, tanδ .003 | 11 electrostatics and explicit losses → RLC response,η 2.02255×10⁻⁵,τ 5.573 ms | T sensitivity; exact equation checks in §5, hardware calibration E. |
| Homogeneous 1,000-km path, εr 22, σ .003 S/m, B 50 µT, dip 65°, azimuth 0 | Declared inputs and pinned LMP source → complex Ez ratio | E0/E1 only; no sensor voltage, source-power calibration or geographic route inferred. |
| Night 87/.30 and tanh → day 74/.50 | Declared diagnostic Wait–Spies law, checked against literature's contrasting profiles → time-varying relative field | Synthetic E1; Thomson/McCormick alternatives constrain interpretation, not its event probability. |
| Mesh, solver/tolerance/defaults and interpolation bound | LMP source/API → executed numerical checks in §4 | Numerical error coordinates; do not substitute them for empirical model discrepancy. |
| Ground-wire normalization; below-4-kHz attenuation bracket | Wheeler/Moore–Blair/reciprocity resolve the shared asymptote; Wait/Taylor support a separate theory/observation bracket | Ground-wire finite-geometry qualification and low-band physical discrepancy remain distinct; neither calibrates the selected 8.27-kHz T/E. |

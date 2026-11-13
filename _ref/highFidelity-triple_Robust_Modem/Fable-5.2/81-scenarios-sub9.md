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
receiver transfer, arrival times and saturation flags. Gu's Figshare link is
identified, but those dataset contents have not been verified here.

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

Keep `LMPParams()` and the default mesh at that pinned commit, explicitly
recording all returned fields/dependency versions in any later calculation.
Material defaults include top height 110 km, Earth radius 6,369 km, curvature
on at reference height 50 km, exact susceptibility, root tolerance 10⁻⁵,
Vern7 reflection integration tolerance 10⁻⁵, 2,049 wavefield heights and
Tsit5 wavefield tolerance 10⁻⁶. Radiation-resistance correction is off. The
[pinned source](https://github.com/fgasdia/LongwaveModePropagator.jl/blob/9231e40d86c201944d08a9100c927bf3ca985105/src/LongwaveModePropagator.jl)
and [public API](https://fgasdia.github.io/LongwaveModePropagator.jl/stable/lib/public/)
resolve the remaining defaults; the future run must save its actual environment,
not rely on a moving `stable` documentation URL.

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
returned complete source-support edges. Refine each interval at its midpoint
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
complex-field changes. These solver and rendering checks are not performed
in this documentation pass. The frozen mathematical inputs remove “choose a
solver/profile” as a research omission; they do not manufacture numerical or
field validation.

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

A possible native reference discretization is RK4 with a maximum substep
`1/(16 Fs)` on the 48,000 samples/s direct physical-band branch, evaluating
the analytic C(t) at each stage and linearly interpolating the voltage samples.
Halve the substep and double the source rate on short steady, detuning and
turn-off holdouts before accepting the interpolation/integration error.
An exact state-space or envelope implementation can later replace this
expensive reference when it reproduces the specified transfer and memory.
Neither discretization was implemented or run here. The simple instantaneous
ratio `R₀/[R(t)+j(ω₀L−1/(ω₀C(t)))]` supplies the B3 slow-detuning arithmetic
checks only; it is not the full circuit's transient response.

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
2. For E, complete the selected solver's root, grid, frequency and rendering
   checks before using its output. Preserve convergence failures and model
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

The arithmetic in §§1, 3 and 5 was independently recomputed in this pass from
the written formulas. No waveguide solve, antenna time integration, native
receiver trial or empirical replay was executed. Implementation readiness of
a mathematical control is distinct from an implemented harness and from
empirical qualification.

The missing empirical links are now specific: a calibrated 50–100 m
sub-9 kHz electric antenna's current/efficiency and detuning time series;
matched complex wanted-field and receiver-noise recordings on long apertures;
receiver effective area/height, component orientation, transfer and self-noise;
sub-9 kHz marked sferic/line amplitudes and timing; and independent
geometry/season/receiver holdouts. Gu's identified Figshare dataset has not
yet supplied verified files, calibration or licence. Reuveni's 75 Hz,
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

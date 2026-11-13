# Groundwave experiment specifications

These specifications close the mathematical input choices for the 1 MHz and
30 MHz research branches. They distinguish **a reproducible control**, **a
source-conditioned propagation calculation**, and **a measured-path replay**.
The first two can be implemented from the definitions below; neither is a
validated real-world episode. The pinned LFMF reference and independent
smooth-earth calculations were executed in [90](90-cross-check-calculations.md);
public measurement archives were inspected in 20/25. No native channel
simulator, matched calibrated complex-channel replay or modem trial was run.
The measured-path requirements at the end remain external evidence
dependencies, not unnamed defaults in the controls.

The physical evidence and contradictory findings are in
[20](20-groundwave-1mhz.md) and [25](25-groundwave-30mhz.md). Moderate and adverse
are conditional comparisons. The finite grids below have no occurrence
probabilities, and their Cartesian product is not an observed natural worst
case. In particular, radar working range is not one-way communications loss,
and a successful reception does not identify a groundwave-dominant path.

## 1. Common source, time and reference planes

Use [00 §7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract)
for the pinned native source, waveform key, ASCII `A` message, calendar, sample
support, seeds and hidden evaluator truth. The following overrides make these
branches concrete.

| Quantity | Selected value or rule |
| --- | --- |
| Epoch | 2025-03-20 00:00:00 UTC; an arbitrary simulation epoch, not a claim about weather, solar state or a specific monitored site |
| 1 MHz waveform | Groundwave slow fixture: rate 1, added symbols 1, FHSS off; Fs=960 samples/s, DATA IF=240 Hz, Helper off |
| 30 MHz waveform | HF fixture: rate 36, added symbols 10, FHSS off, requested bit duration 16 s; Fs=960 samples/s, DATA IF=240 Hz, Helper off. The full native spectral-support and receiver guards still apply |
| Secondary waveform comparison | Repeat a selected physical case with the slow fixture at 30 MHz. This tests time-scale dependence without claiming the physical channel changed |
| Helper comparison | Fs=48,000 samples/s, DATA IF=1,725 Hz, actual native Helper plan; propagate the full composite through one physical operator. A separate comparison at 29.99 MHz, explicitly labeled with that carrier, permits a frequency-dependent LFMF magnitude across the full occupied band without exceeding its 30 MHz ceiling |
| Public channel bounds | `PublicProfileConfig.spread_delay_ms=12`, `arrival_low_seconds=0`, `arrival_high_seconds=.012`, `cfo_bound_hz=2`, `clock_bound_ppm=0`, `audio_pair_ppm=0`, `delay_excursion_seconds=0`; matching receiver `spread_delay_seconds=.012`, `frequency_bound_hz=2`, both clock bounds zero, `delay_excursion_seconds=0`, `time_varying_channel=true`, `time_varying_common_doppler_hz=2`, `time_varying_atmospheric_hz=0`. No additional injected oscillator or clock error occurs in the base controls; public search breadth is not an extra channel impairment. These bounds cover the specified delays and phase trajectories, not all possible natural channels. Keep every public receiver hypothesis |
| Nominal source power | 1 W e.m.r.p. for the LFMF branches: an ideal reference-radiator normalization, not 1 W of feed power into a real antenna. Feed/antenna efficiencies are 1 in this control and are not estimated from a GUI power setting |
| Reference antenna/noise plane | The lossless short-monopole receiving convention of P.341/P.372 used in [20](20-groundwave-1mhz.md): `Pr[dBm]=E[dBµV/m]−20 log10(f_MHz)−78.5`. Do not insert LFMF's `P_rx__dbm` directly into this plane |
| External noise | Fixed `Fa=80 dB` at 1 MHz; `Fa=18 dB` at 30 MHz. These are declared reference cases, not site medians, storm percentiles or universal quiet/adverse classifications |
| Receiver noise | Input-referred `Te=100 K`, added once, after the reference antenna. No feed loss, gain control, clipping, quantization, blockers or impulsive events in the base case |
| Base noise law | Independent proper complex white Gaussian external and receiver terms; `N0_ext=k*290*10^(Fa/10)` and `N0_rx=k*100`, W/Hz, with `k=1.380649e−23 J/K`. At sampling rate Fs, each real and imaginary component has variance `N0*Fs/2`. Use domains 2 and 6 in 00, with persistent physical-sample order |
| Record duration | The exact source support and final quiet slots from 00, plus all path/filter tails. Noise and channel time continue across all gaps; initial/final source zeros do not reset a delay, filter or phase state |

The resulting total noise densities are **−93.97519 dBm/Hz** and
**−155.95152 dBm/Hz**, respectively [E]. Rounding them to −94 and −156 for a
figure is acceptable; the equations define the inputs. The much larger MF
noise is not receiver thermal noise and must not be counted a second time by
adding an independently selected atmospheric-noise floor.

Let `x(t)` be the complete noiseless native complex emission. Use 00's
bounded deterministic DATA reference: `mu=report.data_reference.mean_norm`,
`G=report.data_gain`, `R_emit=mu*G²`, `u(t)=x(t)/sqrt(R_emit)`. Native
R_emit is approximately one; do not divide by sqrt(mu) again. Apply one
physical-power multiplier to the whole composite, preserving the actual
Helper/DATA ratio. A received component of reference power Pr watts is
`sqrt(Pr)*u(t−τ)`. Save the reference estimate and its uncertainty, without
integrating the complete message to redefine C/N0 or renormalizing a fade.

All phase signs below use the analytic signal `exp(+j2πft)` and a delay response
`exp(−j2πντ)` at offset frequency ν. A path's carrier phase at ν=0 is stated
separately; its arbitrary zero does not make delay vanish. Fractional delays
must be applied to the continuous source or by a qualified band-limited delay
operator, with one fixed operator and retained tails across chunk boundaries.
The proposed numerical acceptance is less than 0.01 dB magnitude error and
0.001 rad phase error across the occupied source band, refined once to establish
that conclusion. This is a numerical target, not a claim of physical accuracy.

## 2. Pinned smooth-earth magnitude adapter

Use the official [NTIA LFMF v1.1 release](https://github.com/NTIA/LFMF/releases/tag/v1.1),
commit `57886e9d0a29fd8f04a6416dc3f782322f5a3589`. The selected C++ entry point is

    LFMF(h_tx_m, h_rx_m, f_MHz, P_tx_W, N_s, d_km, epsilon_r, sigma_S_per_m, 1, result)

The [pinned interface](https://github.com/NTIA/LFMF/blob/57886e9d0a29fd8f04a6416dc3f782322f5a3589/include/LFMF.h)
returns field, basic loss, received power and solution method; it does not return
complex phase. Save all outputs and the return code. `N_s` is surface refractivity
in N-units, not an effective-earth-radius factor. Choose

    N_s = ln(0.25/0.04665)/0.005577 = 301.0199014826777

so that the pinned implementation's `a_e/6370 km` is exactly 4/3. Its internal
transmitting and receiving gains are both 4.77 dBi. The received-power output
therefore uses a different receive reference. Its hardcoded power formula is
`P_rx=E−20log10(f_MHz)−72.43`, **6.07 dB** above this chapter's rounded
`E−20log10(f_MHz)−78.5` conversion. The ideal antenna-gain ratio is
`10log10(3/.75)=6.0206 dB`; implementation constants introduce the remaining
rounding difference. Use the field output and the declared conversion instead. These details follow the
[pinned implementation](https://github.com/NTIA/LFMF/blob/57886e9d0a29fd8f04a6416dc3f782322f5a3589/src/LFMF.cpp).

For each selected route, calculate at the carrier with `P_tx_W=1`; independently
repeat at 1000 W and check the 30 dB field-level change and power-independent
basic loss. The equations below call that 1 W field `E1`. To check the printed
P.368 conversion, first use the 1 kW field: `L368=142+20 log10(f_MHz)−E1000`.
Keep the returned result, reference-radiator convention and rounding residual
visible rather than silently subtracting antenna gain twice.

For the **selected flat-magnitude control**, use

    Pr_dBm = E1 − 20 log10(f_MHz) − 78.5
    H(ν) = sqrt(10^((Pr_dBm−30)/10)) exp(−j2πν d/c),  c=299792458 m/s.

Thus the source is delayed by `d/c`, with carrier phase zero and no propagation
Doppler. The phase is an explicitly synthetic vacuum-slope control. It is not
phase calculated by LFMF, Millington or a measured groundwave.

At 30 MHz, evaluating LFMF at `30 MHz+ν` for positive ν exceeds its documented
ceiling. Consequently the 30 MHz control holds its carrier magnitude fixed
across the envelope and labels that approximation. A frequency-dependent
comparison uses **29.99 MHz** with every evaluated source frequency within
0.01–30 MHz. For a richer phase model, [90 §1.1](90-cross-check-calculations.md#11-method-and-checks)
gives the Fock/Bremmer/Wait complex prescription. The original research solver
was recovered, and independent Airy-root checks now reproduce its complex
outputs (90 §1.1). This qualifies the recorded constant-radius arithmetic,
not a measured environmental phase history or a native streaming operator.
A second build of LFMF is a numerical comparison, not an independent physical
theory. The [input validator](https://github.com/NTIA/LFMF/blob/57886e9d0a29fd8f04a6416dc3f782322f5a3589/src/ValidateInputs.cpp)
also rejects heights outside 0–50 m and `N_s` outside 250–400: the raised 100 m
receiver below is a separate ray control, never an LFMF call.

## 3. One-megahertz cases

### 1M-A: homogeneous daytime reference

Use `fc=1 MHz`, range 100 km, both heights 0 m, `epsilon_r=15`,
`sigma=0.01 S/m`, vertical polarization and the adapter above. This is a smooth,
homogeneous, static-earth calculation. There is no skywave, clutter, cochannel
transmitter or time-dependent phase. Electrical-ground alternatives are
`sigma={0.005,0.01,0.02} S/m`, holding every other input fixed; the middle member
is the baseline. These are sensitivity coordinates, not an inferred population.

The close-frequency Chilcote et al. 2015 observations in [20](20-groundwave-1mhz.md)
found daytime frequency broadening/shifts below 0.05 Hz in their 810/1030 kHz,
22.8–241.4 km, 131 s-spectrogram experiment. That supports testing a quiet
comparison; it does not prove phase is constant over this modem's complete
message or set a universal 0.05 Hz stochastic process. A separate phase stress
can therefore be applied as `phi(t)=0.3 sin(2πt/600)` radians, maximum frequency
excursion 0.0005 Hz. Its amplitude and period are chosen controls, not fitted
Chilcote parameters.

### 1M-B: mixed coastal route

The fixed route is 200 km: land 50 km (`sigma=.003 S/m`, `epsilon_r=22`), sea
100 km (`sigma=5 S/m`, `epsilon_r=80`), land 50 km with the same land constants;
both heights 0 m. Use the P.368 Annex 2 amplitude construction. If `E_i(D)` is
the adapter's dB field for an entirely homogeneous route of type i at total
distance D and `D_i=sum_{k=1}^i d_k`, define

    E_forward = E_1(d_1) + sum_{i=2}^n [E_i(D_i) − E_i(D_(i−1))].

Repeat with the segment order and lengths reversed to obtain `E_reverse`, then
`E_mixed=(E_forward+E_reverse)/2`. Insert this field in the reference-plane
conversion of section 2 and use delay `200 km/c`. This defines amplitude only;
no coastal phase recovery has been measured or inferred here. The construction
is from [P.368-10 Annex 2](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.368-10-202208-I%21%21PDF-E.pdf).

The symmetric route alone is a weak reciprocity check. Also evaluate a distinct
20 km land / 120 km sea / 60 km land route, first with the last land segment at
`.001 S/m, epsilon_r=15`, then in reverse. Require the averaged answer to agree
under reversal within numerical rounding; do not require either directional
intermediate to agree. As a conductivity sensitivity, repeat the baseline with
sea sigma 4 S/m and with both land sigmas .001 S/m, one change at a time.

### 1M-B2: near-band observed geography and method disagreement

Use the published Nishioka–Inasawa–Miyashita 2014 route summarized in
[20](20-groundwave-1mhz.md): **1.242 MHz**, 19 km land followed by 71 km sea,
land `(sigma,epsilon_r)=(.001,15)`, sea `(5,70)`, nominal heights zero.
These are the paper's modeling inputs, not a new local ground survey.
The paper observes relative coastal recovery with an unknown receive gain;
it cannot validate absolute field or phase. Retain the carrier label 1.242 MHz.

The following **executed reference calculations**, in dBµV/m for **1 kW**
e.m.r.p., use the pinned LFMF with the section 2 radius and the same curves
for both stitching methods (90 §1.6). For this chapter's 1 W source subtract
30 dB from every field before the section 2 receive-plane conversion.

| Total path, km | Homogeneous land | Homogeneous sea | Millington land→sea | Equivalent-distance land→sea | Equivalent-distance sea→land |
| --- | --- | --- | --- | --- | --- |
| 19 | 57.326177 | 83.855695 | 57.326177 | 57.326177 | 57.326177 |
| 30 | 49.028822 | 79.788803 | 61.888994 | 56.661127 | 54.655940 |
| 50 | 39.626383 | 75.125498 | 59.642799 | 55.479029 | 52.792259 |
| 90 | 28.318421 | 69.442402 | 55.027506 | 53.199046 | 50.632386 |

The equivalent-distance algorithm preserves field at the interface, substitutes
its distance on the next homogeneous curve, then adds the remaining physical
segment length. This is the construction in [47 CFR 73.183(d)–(e), preceding
§73.184's graphs](https://www.govinfo.gov/content/pkg/CFR-2025-title47-vol4/pdf/CFR-2025-title47-vol4-sec73-184.pdf).
Here it uses **LFMF curves**, not the official FCC curves and their sea
permittivity 80. The land interface maps to 246.301119 km on the sea curve.
The forward/reverse equivalent-distance answers differ by 2.0–2.7 dB, whereas
Millington is reciprocal by construction and permits recovery after the coast.
This isolates a method choice. It is not GRWAVE exponential-atmosphere parity,
an FCC service-area determination, or a fit to the unknown-gain measurements.
For the modem control, use Millington amplitude and the explicit vacuum-slope
phase of section 2; the phase remains synthetic.

### 1M-C: seasonal and layered-ground comparisons

**C1 is a complete homogeneous-state comparison**, not a time model of a
freezing front: use the same 100 km route as A with the fixed pairs
`(sigma,epsilon_r)={(0.01,15),(0.003,7),(0.0001,3.2)}`. Each state is held
constant for a whole record. This preserves the distinction between temporal
seasonal medians, a mapped conductivity class and a layered physical surface.
Do not infer a freezing probability or transition time from those three pairs.

**C2 is a fully specified boundary-response component check**, deliberately not
a complete groundwave forecast. At 1 MHz illuminate a horizontal two-layer
medium at 5° grazing angle with a TM plane wave. Use time sign `exp(+jωt)`,
`epsilon_c=epsilon_r−j sigma/(ω epsilon0)`, `epsilon0=8.8541878128e−12 F/m`,
`mu0=1.25663706212e−6 H/m`, `k0=ω/c`, and conserved horizontal wavenumber
`k_parallel=k0 cos(5°)`. For each medium choose

    q_i = sqrt(k0² epsilon_ci − k_parallel²),  Re(q_i)>=0, Im(q_i)<=0
    Z_i = q_i/(ω epsilon0 epsilon_ci)
    Z_in = Z_1 (Z_2 + j Z_1 tan(q_1 h)) / (Z_1 + j Z_2 tan(q_1 h))
    R_TM = (Z_in − Z_air)/(Z_in + Z_air).

`Z_air` is obtained from `epsilon_c=1`. Case C2-soil uses top thickness `h=1 m`,
`(sigma,epsilon_r)=(.0001,5)` over `(.01,15)`; C2-ice uses `h=.5 m`,
`(.00001,3.2)` over `(5,80)`. The comparison is h=0, returning the lower
half-space impedance exactly. A second check sets both media equal, for which
thickness must not change `Z_in`. These idealized material values are assumptions;
the equations do not transform a thin ice sheet into an ice half-space.

Feeding that boundary response into a curved-earth or segmented surface-wave
solver is a distinct implementation and validation task. The normal-incidence
or single-angle reflection coefficient must not simply multiply the 100 km
LFMF field and be called layered-groundwave loss. Published 308/319 kHz sea-ice
calculations in [20](20-groundwave-1mhz.md) remain useful model comparators with
frequency and empirical-validation limits; no numerical 1 MHz layer correction
is selected from the historical scratch calculation.

### 1M-D: one coherent night-return control

Use a 200 km all-land version of A. Define a flat virtual mirror at 100 km
height, so the reflected path length is `2 sqrt((100 km)²+(100 km)²)` and its
excess over the 200 km surface reference is **276.333544 µs** [E]. This idealized
mirror is a geometry control, not a traced E/F ionospheric solution.

With `tau0=200 km/c`, `delta=276.333544 µs`, and surface gain g from section 2,

    y_wanted(t) = g [u(t−tau0) + r exp(j phi(t)) u(t−tau0−delta)]
    r = 10^(R/20),  R in {−10,0,+10} dB
    phi(t) = phi0 + 2π(0.001)t + 0.5 sin(2πt/600),  phi0 in {0,π}.

`t=0` is the simulation epoch and phase continues through silence. Run all six
finite controls; no independent random fade is assigned to the same source's
return. The selected R is the ratio **after** both antenna patterns and path
losses, so a separate elevation-pattern penalty would count them twice. The
initial phase, drift and sinusoid are engineering assumptions. Delayed-copy
frequency selectivity, cancellation, and an apparent phase jump at a deep null
then emerge from the sum. No additional independent Doppler process is added.

P.1147's composite short-loop field and P.1321's individual-mode illustration
are retained as **separate source-model comparators**, not automatic gains for
this equation. Chapter 20 §G records their 100–200 km printed disagreement and
unmatched receiver observable. The selected R already refers to the same
receiver plane for both components; a physical prediction must first supply
that matched field/pattern transformation. Neither a PSD-only AM archive nor
300 kHz estimator variance supplies the missing complex tap amplitudes.

For a transition control, multiply only the reflected term by
`w(t)=ramp((t−t_on)/600)` where `ramp(z)=0` for z≤0, `(1−cos(πz))/2` for
0<z<1, and 1 for z≥1; `t_on` is the first sample of active slot 3. This is a
prescribed onset. Night geometry, solar conditions and reflection-height
occurrence require measured or ray-traced state before being called a real
night episode. Near-MF observations of tenths-hertz/terminator shifts in [20]
justify further offset alternatives, not treating the 1 mHz chosen drift as a
universal bound.

### 1M-E: signal-level and physical-completion controls

For A and D separately, keep their relative transfer, phase, noise and delays
fixed, and rescale the **surface reference only once before adding paths** so
its `C/N0` is `{-10,0,10,20,30,40} dB-Hz`. With total N0 as above, select
`g²=N0*10^(C/N0_dBHz/10)`. These sensitivity controls intentionally replace the
1 W/range-derived magnitude; label their result as normalized receiver
sensitivity, not predicted range or real feed power. D's total wanted power can
rise or fall relative to that reference through coherent interference.

Use both the actual-source-end control and the two-interior-slot fade in 00.
For the latter, attenuate **all wanted paths** by zero over the specified
contributing source/quiet-score intervals, retaining later source DATA. Also
compare a nonzero floor of amplitude `1e−3` (60 dB power attenuation). Noise is
unchanged. Add 1 s raised-cosine attenuation ramps outside the fully covered
interval, enlarging the fade to keep every declared delayed contribution
covered. Truth stays in the evaluator. An observed early completion is a result,
not something to repair by disclosing the fade or endpoint to the receiver.

## 4. Thirty-megahertz cases

### 30M-A: low-antenna sea reference

Use `fc=30 MHz`, range 100 km, both heights 2 m, `sigma=5 S/m`,
`epsilon_r=80`, `N_s` from section 2, vertical polarization and 1 W e.m.r.p.
The smooth-earth magnitude is held constant across the modem envelope as
specified above. With effective radius 4/3 times 6370 km, the two 2 m geometric
radio horizons total **11.65733 km** [E]. A direct unobstructed ray therefore
cannot be silently substituted for this 100 km surface-path control.

Repeat independently with `(sigma,epsilon_r)=(4,70)` and with `N_s=250` and
`N_s=350`, one change at a time. These are allowed model-input alternatives, not
a duct climatology. A constant effective radius does not reproduce arbitrary
vertical refractivity, duct capture or sea-state dynamics. Set path phase zero
at the carrier and delay `100 km/c`; absence of phase fluctuations is a control.

### 30M-B: explicit time-varying sensitivity, and the rough-sea boundary

To isolate sensitivity to slow level/phase changes without pretending to have
measured sea-state statistics, multiply A by

    m(t)=10^[−A(1−cos(2πt/600))/40] exp[j B sin(2πt/300)]
    (A,B) in {(0,0),(3,0.1),(10,1),(20,π)}.

A is the maximum power attenuation in dB; B is radians. The selected nonzero
pairs share a prescribed phase/level timing, not independent stochastic extrema.
Compare a second dependence alternative with the phase sine shifted by π/2,
using the same amplitude law. These controls are fully specified and synthetic;
they must not be labeled a Barrick simulation or a recorded rough-sea event.

**B2 is a source-conditioned mean-loss comparison.** Change A to heights 0 m,
`sigma=4 S/m`, `epsilon_r=80`, keeping 30 MHz and 100 km. Barrick's read 1970
report [25 R18] Fig.31 uses a Phillips isotropic developed wind-sea spectrum;
its figure-read added losses at this frequency/range are about **1, 6 and
11 dB** for sea states **1, 3 and 6** (5, 15 and 30 knot wind labels), each
about ±1 dB reading uncertainty. Apply each loss as one constant amplitude
factor `10^(−L/20)` for the entire record; compare L±1 dB separately. This is
a declared approximation to the published coherent mean-loss curves, not a
new integration of its roughness spectrum or a measured sea state. The roughest
branch is a perturbation-limit sensitivity: small height/slope assumptions
are marginal at upper HF, and the wind label does not include swell, fetch
or local wave history. It must not be called a validated severe-sea forecast.

The corresponding physical implementation alternative is the report's
**complex mean impedance derived from a named directional height spectrum**,
inserted in a spherical-earth solution. Validate its smooth limit, spectrum
normalization, small-roughness domain and the source figures before replacing
B2. It supplies a coherent mean, not the incoherent fluctuation PSD or a Rician K.
B2 deliberately retains section 2's synthetic phase; an additional sinusoid
would be an explicitly named stress combination, not a consequence of Barrick.
The Neumann–Pierson directional spectrum is a competing theory branch, with
several-dB differences already recorded in 25, not an uncertainty distribution.

The 16.188 MHz one-way Kirincich–Emery and 25 MHz Halverson radar results in 25
constrain transfer and confounding; their power/radar-range observations do
not validate this 30 MHz loss or phase. A measured sea spectrum plus a matched
complex forward channel would permit a separate fluctuation comparison.
No universal sea-state occurrence rate is assigned.

### 30M-C: short terrain and raised-receiver geometry controls

These are **normalized scalar ray/diffraction controls** with surface-reference
`C/N0=20 dB-Hz`, total N0 from section 1 and `g=sqrt(100*N0)`. Their magnitude
normalization replaces the 1 W/LFMF link budget; it does not assert the real
antenna pattern, clutter loss or received level of an actual 1 km link.

**C1, knife edge:** transmitter and receiver heights are 2 m on flat ground,
horizontal separation 1000 m, and a zero-thickness edge is centered at 500 m
with its top 20 m above the ground. The obstruction is therefore 18 m above the
straight line. Define

    v(f)=18 sqrt[2(500+500)/(lambda(f)*500*500)]
    D(v)=(1+j)/2 * integral_v^infinity exp(−jπz²/2) dz
    H(ν)=g D(v(fc+ν)) exp(−j2πν*1000/c).

This is a specified ideal knife-edge Fresnel approximation, with
`D(−infinity)=1`, `D(0)=.5`, and **v(fc)=0.5092930791** [E]. It is not a terrain
survey or a model of vegetation/building interiors. Check the analytic Fresnel
limits and convergence of the selected evaluation method before modem use.
Do not add an unrelated smooth-earth attenuation to this normalized control.

**C2, visible raised receiver:** keep range 1000 m, transmitter height 2 m,
raise the receiver to 100 m, and remove the knife edge. Use a planar ground with
`epsilon_r=15`, `sigma=.003 S/m`. Let

    Ld=sqrt(1000²+(100−2)²),  Lr=sqrt(1000²+(100+2)²)
    theta=atan((100+2)/1000)
    epsilon_c(f)=15−j*.003/(2πf epsilon0)
    Rv(f)=[epsilon_c sin(theta)−sqrt(epsilon_c−cos²(theta))]
          /[epsilon_c sin(theta)+sqrt(epsilon_c−cos²(theta))]
    H(ν)=g {exp(−j2πν Ld/c)
       +(Ld/Lr) Rv(fc+ν) exp(−j2πfc(Lr−Ld)/c) exp(−j2πν Lr/c)}.

Use the passive square-root branch stated in C2-soil. g is the direct-path
reference, so the reflected path is not normalized separately. This assumes
scalar vertical polarization with equal ray antenna response; actual elevated
antenna pattern and terrain are deliberately unspecified physical effects,
not implicit ideal field evidence. Compare direct-only by setting Rv=0.
The 100 m height is outside LFMF's input range; this is explicitly a distinct
space-wave geometry and not a 30 MHz surface-wave qualification.

### 30M-D: one competing coherent path

Take A or C2 as the wanted reference, separately. Let its unmodified output be
`y0(t)` and add one delayed copy with **5 ms excess delay**, **+1 Hz frequency
offset**, initial phase 0, and post-path power ratio R in `{−10,0,+10} dB:

    y_wanted(t)=y0(t)+10^(R/20) w(t) exp(j2πt) y0(t−0.005).

`w(t)` rises by the raised-cosine ramp in 1M-D over 10 s from the first sample of
active slot 3; it stays at 1 until the last sample of active slot 9, then falls
over 10 s. If another explicitly substituted message has fewer than ten active
slots, reject this case definition rather than silently shorten its event.
For the common thirteen-bit fixture all specified slots exist.

This is a delayed, shifted same-source path and passes Helper and DATA together.
It is a mathematically closed scatter/ionospheric sensitivity control; it is not
assigned simultaneously to sporadic E, aircraft, meteor and tropospheric scatter.
For an identified physical mechanism, range/velocity, bistatic geometry,
antenna elevation response and event duration must replace these selected
numbers with a consistent source or ray solution. An unrelated transmitter
would instead require its own waveform and carrier state, as specified in [85](85-scenarios-noise-and-equipment.md).

### 30M-E: severe loss and disappearance

First evaluate a separate 100 km low-land route with heights 0 m,
`(sigma,epsilon_r)=(.003,22)` and `(.0001,3)`; this is a nominal smooth-earth
calculation, with no assertion of real terrain transparency. A distinct 300 km
sea route uses A's 2 m heights and sea constants. Keep their calculated field
outputs even if they are below the chosen receiver's sensitivity; do not cap
loss at 180 dB or replace a very small physical component by zero implicitly.

Then apply the C/N0 grid and the two completion/fade controls from 1M-E to A and
D separately. A **surface-only disappearance** suppresses A's surface term but
preserves any declared delayed wanted return. A **total wanted disappearance**
suppresses both. Surviving same-source skywave/scatter can carry wanted DATA;
it is not independent interference merely because it is not groundwave.
Non-detection provides an upper bound conditioned on the detector/noise, not
proof of exact physical zero.

### 30M-F: observed terrain-loss comparator at its actual frequencies

The official NTIA Phase 1 archive was retrieved and analyzed in
[25's completion section](25-groundwave-30mhz.md#completion-analysis-of-accessible-groundwave-evidence).
It supplies **20.084/49.72 MHz** Colorado and **19.97/49.72 MHz** Ohio spot
observables, not a 30 MHz complex replay. Preserve its principal/optimized-site
separation, per-site antenna exceptions, ±3 dB gain uncertainty, dates,
missing `999` cells and `500+q` loss lower bounds. The six principal-site
summaries in 25 are detected-only descriptive statistics.

A compact adapter check is PL-05-P1, 4.96 km: excess attenuation 33.0 dB at
20.084 MHz and 51.1 dB at 49.72 MHz gives basic losses **105.41664 and
131.39026 dB** from `Lbf=32.45+20log10(f_MHz*d_km)`. This checks data units
and observation definitions. It does not set a scalar 30 MHz gain or temporal
fading law. A future same-frequency terrain comparison must use the actual
profile, gains and site record and hold out whole site groups; no model fit
or modem trial is claimed. The observed Ohio censoring prevents interpreting
a lower detected-only 50 MHz median as a more favorable complete distribution.

## 5. Traceability, dependence and validation decisions

| Critical input | Evidence or calculation | Permitted inference and remaining limit |
| --- | --- | --- |
| Smooth-earth field and ground-constant response | P.368-10 Annex 1; pinned LFMF v1.1 interface and source, sections 1–2 above | Exact reproducible model inputs; no measured local conductivity, terrain or antenna efficiency is supplied |
| Coastal amplitude construction | P.368-10 Annex 2 equations (1)–(3); 1M-B; Nishioka2014 §3/Figs.2–3 and executed 1M-B2 | Reciprocal amplitude reference and observed near-band relative recovery; equivalent-distance method disagreement isolated. No absolute/phase validation |
| Short-time MF behavior | Chilcote et al. 2015 methods/results at 810/1030 kHz, summarized with conditions in 20; R-Mode/eLoran comparators remain at lower frequencies | Select a quiet baseline and separate night return; not an inferred 1–600 s coherent groundwave PSD or a joint occurrence model |
| Layer/ice behavior | Exact selected TM slab equations and numerical material values in 1M-C; nearby-frequency literature in 20 | Boundary-response check only. Full curved-earth layered-path field still requires a separately qualified solver and physical inputs |
| Night return delay | Virtual-mirror geometry and c, 1M-D | 276.333544 µs for the stated mirror; R, phase and onset are engineering assumptions, not a fitted E/F distribution |
| Sea roughness and salinity | Barrick1970 §§III–IV/Fig.31 → B2 constant mean-loss comparison; Kirincich–Emery and Halverson methods/limitations in 25 | Mean-loss theory versus synthetic fluctuations are separate. Radar range and remote conductivity/wind proxies do not determine a 30 MHz forward channel |
| Terrain/raised receiver | Explicit 1 km Fresnel/two-ray equations, 30M-C; NTIA Phase 1 method/decoded cells → F; Casciato2003 §§2.3–3 → resolved-coast alternative | Geometry and observed spot loss are distinct. No long-path complex, building or forest calibration; homogeneous height gain is not a cliff prediction |
| Noise and equipment | Fixed Fa/Te in section 1; physical-plane conventions in 00/20/50; optional cases in 85 | Constant Gaussian base law is an assumption; empirical PSD/APD/occupancy, antenna factors and front-end recovery remain separate evidence |
| Outage and source completion | Native source/calendar contract in 00; declared all-path versus surface-only attenuation | Hidden evaluator truth, continuous state and quiet slots; no known future fade or packet length is disclosed to the receiver |

**Numerical checks and implementation gates.** The pinned LFMF reference tests,
return codes, power normalizations and independent smooth-earth comparisons
were executed in 90 §1.1; the implemented receive-power constants and 6.07 dB
reference difference are explicit above. Mixed-route reference calculations
and the slab scalar limits are also recorded. Native adapter checks must still
cover carrier-center and translated-band limits, mixed-route reversal,
equal-layer and zero-thickness limits,
Fresnel endpoints, ray delay signs, the six coherent MF comparisons and
continuous event phase. Compare the deterministic wanted path at Fs=960 and Fs=48,000 where both are
valid, with the same continuous phase/delay history. Verify noise PSD and receiver
equivalent noise bandwidth separately: equal seed numbers at different sample
rates do not create the same noise realization. Refine the delay/filter operator
once; do not use a failed containment check or aliasing to characterize receiver
sensitivity.
The historical field/range/phase/height calculations were regenerated in 90.
Their printed rounding and constant-radius model are explicit; they are
arithmetic references, not independent evidence of physical accuracy.

**Severity and time/dependence map (GW15).** “Moderate,” “adverse” and “severe”
below rank the specified conditional mechanism or receiver stress. They are
not site percentiles, and rows do not define one naturally probable joint event.

| Mechanism and source → selected case | Moderate/reference | Adverse/severe or outage comparison | Time/correlation and observable consequence |
| --- | --- | --- | --- |
| P.368/LFMF field → 1M-A, 30M-A/E | Fixed homogeneous soil or smooth sea, declared antenna plane | Poor land, longer sea range, changed refractivity or lower C/N0 | Material/geometry constant throughout a record. Field weakens; environmental evolution and mode identity are not inferred from loss |
| P.368 Annex2; Nishioka relative recovery → 1M-B/B2 | Homogeneous and coastal amplitudes on the same curves | Unequal land segments, reversal, equivalent-distance comparator | Spatial recovery, not temporal fading. Delay/phase control remains synthetic; no random coastal phase draws |
| Gracely/Handbook; Blomquist; P.527 → 1M-C1 | Fixed reference material state | Dry/frozen half-space alternatives; season-sign counterexample retained in 20 | Months/season and daily statistics differ. Soil, vegetation, refractivity and skywave may share causes; no fitted joint law or imposed freeze-transition time |
| Layered boundary theory; Hehenkamp adjacent MF → 1M-C2 | Zero-thickness/equal-medium limits | Specified soil or ice slab | Static boundary response only; not a 100 km propagation gain. Snow/permafrost/ice field and temporal corrections remain unvalidated |
| Chilcote day/night evidence; distinct P.1147/P.1321 observables → 1M-D | Quiet A; weak return R=−10 dB | Comparable/strong return R=0/+10 dB, coherent null, prescribed onset | Same source, continuous phase, shared path copies; geometry delay known, gain/drift/onset chosen. Spectral-power records do not determine complex taps |
| Barrick mean impedance; contradictory coastal/radar observables → 30M-B2 | Smooth or low mean-loss comparison | Published stronger mean-loss branch with perturbation warning; directional-spectrum alternative | State fixed per record; mean field is separate from fluctuations. Salinity/tide/wave history needs joint inputs, not independent marginal maxima |
| Explicit sensitivity rather than measured phase → 1M-A phase alternative, 30M-B | No prescribed variation | Stated level/phase functions and phase-lag alternative | Exact periodic controls, never labeled a wind-sea process or calibrated groundwave PSD |
| Fresnel/two-ray geometry; NTIA spot loss; Casciato ideal coast → 30M-C/F | Unobstructed scalar ray/short route | Edge obstruction, raised receiver, actual-frequency observed-loss comparison | Geometry fixed; spatial ripple is not time Doppler at a stationary terminal. Moving foliage/buildings lack a selected temporal law |
| P.843/Haakinson meteor, Hansen duct episode, SIMONe nearby modes → 30M-D | Surface/ray reference with declared absent competitor control | Specified delayed same-source term; separate mechanism-specific candidate branches in 25 | +1 Hz/5 ms/onset/duration are controlled choices, not bounds on meteor/auroral spectra or rates. Unknown natural competitors are not certified absent |
| P.341/P.372; explicit Fa/Te → sections1/2, 85 alternatives | Fixed reference antenna and Gaussian noise | Named noise/impulse/blocker/antenna alternatives from85 | Common receiver plane required. Instrument/antenna drift and interference are separate from propagation; no silent power renormalization |
| Physical-source/calendar contract → 1M-E, 30M-E | Source ends after its actual native emission | Deep finite fade, surface-only disappearance, total wanted disappearance | All relevant delayed tails retained; only evaluator knows source/fade. A surviving same-source return may carry DATA; non-detection is conditional, not exact physical zero |

The component evidence in 25 does not supply a calibrated **180 dB** mode ranking.
Meteor arrival rate, aircraft RCS/trajectory, duct refractivity, backscatter
geometry, ocean scattering and auroral event conditioning remain separate
inputs. The new 32.55 MHz SIMONe source establishes coexistence and selection
limits, not direct-path-relative calibration. Its selected radar products are
not a weak-signal IQ replay. A scientifically conditioned severe branch must
retain each unknown competitor as an explicit alternative or “unmeasured”;
the surface-only branch is only the declared control. These research selections
allow later implementation without claiming universal extreme probabilities.

**Model uncertainty.** Run the stated conductivity, refractivity, path-ratio and
dependence alternatives as distinct paired controls. Add alternative receiver
noise/impulse/equipment models from 85 as named comparisons with their complete
physical planes, never as an unrecorded change in Fa. Do not multiply independent
marginal maxima into a natural joint probability. A common latent storm/wetness
history needs evidence for its coupling; in its absence the paired alternatives
measure sensitivity, not climatic reliability.

**Matched empirical validation.** A measured replay needs source/antenna power,
patterns and heights; route, conductivity/terrain/shoreline and time; calibrated
complex samples and noise estimator; an absolute time/frequency reference; and
uncensored nondetections. Fit and check on different dates/routes. At 1 MHz the
missing decisive record is a calibrated mixed land/sea level-and-phase path,
plus an independently observed night-return interval. At 30 MHz it is a one-way
low, below-horizon sea path paired with a separately raised visible receiver,
with sea spectrum, refractivity and complex phase. The existing literature is
valuable but does not supply this joint dataset.

**Receiver qualification.** Channel agreement, source construction, successful
decode, false/premature quiet-slot completion and complete-receiver capacity are
separate outcomes. Preserve full public search, failed acquisitions, tails,
quiet-slot observations, erasures and reacquisition; use [86](86-statistics-and-calculation-checks.md)
for conditional success estimates and bounded stopping. Neither these controls
nor the available papers justify a complete native-receiver pass or a real-world
range/success probability. Better physical or numerical implementations may be
found; retain the source and geometry limits when replacing a selected control.

## 6. Checks actually performed and access record

On 2026-11-13 the pinned GitHub release, header, implementation, input validator
and official P.368-10 Annexes 1–2 were read. The NTIA documentation wiki route
was inaccessible in this web tool; the official source files supplied the API,
units, refractivity expression, gain convention and domain checks instead.
The direct `src/LFMF_CPP.cpp` route did not exist; the implementation is in
`src/LFMF.cpp`. No source access failure was reclassified as a physics result.

A separate small Python 3 scalar calculation checked `N_s=301.0199014826777`,
the 276.333544 µs mirror excess, the 11.65733 km summed horizon, the knife-edge
parameter 0.5092930791, and the two noise-density values above. Complex arithmetic
also verified the two slab controls' zero-thickness/equal-medium limits and
passive reflection magnitudes (0.46389184 for the soil slab, 0.94396540 for the
ice slab). An asymmetric arbitrary-field example checked the forward/reverse
Millington algebra; it was not an LFMF propagation calculation. Local link
targets existed and `git diff --check` passed. These were arithmetic and document
checks. The later executed reference work in 90 additionally covers all 95
official LFMF vectors (5 valid, 90 invalid), 144 field/loss pairs, 216 range
thresholds, 18 conductivity magnitude/phase pairs, 45 height cells and 12
power-normalization pairs. Independent complex residue checks cover 96 cells;
maximum agreement errors are about 0.000201 dB and 0.0000133 rad. Official versus
original unrounded fields differ by at most 0.05475 dB for the compared set,
with a documented near-range curvature-branch difference. These are solver
arithmetic checks, not a native DSP implementation or receiver trial.
The explicit research inputs replace earlier instructions such as “specify a
measured spectrum” inside a nominally runnable case; the unavailable measured
complex rough-sea and full layered-path alternatives remain separately
identified. Chapter 25 records actual Phase 1 data analysis and the SIMONe
public-payload transfer limit; chapter 20 records the AM archive's PSD-only
contents and unresolved frequency-index details. None was relabeled as a
calibrated complex-channel replay. Temporary source data are reproducible from
the URLs/hashes; durable findings and small reproducing expressions remain here.
Temporary downloads and parses under `/tmp/fable-ground-research` served review
of these receipts and were removed after its acceptance. The large
AM nightly archive was already removed after its inventory and byte checks;
its public identifier, hashes, decoding inputs and results above remain.

## 7. Individual groundwave research dispositions

These are the final research-pass dispositions for GW01–GW16 in
[87](87-evidence-remediation-audit.md), dated 13 November 2026. “Completed”
means the stated source/data/calculation or specification work was performed;
it never means that a missing empirical channel was measured. The full source
access and named-item ledgers remain in [20](20-groundwave-1mhz.md) and
[25](25-groundwave-30mhz.md), rather than being duplicated here.

| Item | Completed research and consuming result | Remaining boundary and classification |
| --- | --- | --- |
| **GW01 — near-1 MHz coherent channel** | Targeted source/data review plus inspection of one complete AM nightly archive and the public dataset manifest. The inspected product is relative PSD, with explicit cadence, hash, shape and unresolved frequency-index details. This supports night/Doppler context and the quiet-versus-synthetic distinction in 1M-A/D. | A matched, calibrated 1–600 s groundwave-dominant complex record was not found. Its acquisition is external measurement work; PSD cannot supply missing phase. Reproducing the archived Doppler ridge would additionally require the identified processing/index metadata and would still not close this channel requirement. |
| **GW02 — seasonal/day-to-day behavior** | Source comparisons separate mean deviation, rms, monthly/seasonal medians, path length and possible skywave. Read Blomquist's cold-region body and the R-Mode estimator/mitigation bodies; preserve contrary winter enhancement and calibration effects. Consumed by 1M-C and the severity map. | Seasonal cause and joint soil/vegetation/refractivity/skywave dependence remain unresolved in the evidence. No universal coefficient or cross-band temporal process is fitted. A contemporaneous matched environmental/complex record is external evidence, not a remaining unnamed literature task. |
| **GW03 — conductivity and mixed paths** | Fixed Nishioka's actual 1.242 MHz coastal geography and its modeled materials; established FCC map units/vintage without pretending to obtain a precise current raster. Executed same-input Millington and equivalent-distance comparisons in both directions (1M-B2; 90 §1.6). Slab and complete-path quantities are separate. | The observations have unknown receive gain and no phase calibration. The comparison uses LFMF curves, not official FCC service curves or GRWAVE parity. Local maps and a field survey remain needed for a deployment prediction; the research controls use explicit tuples. |
| **GW04 — ice, snow and frozen layers** | Checked Hill–Wait title/DOI/NIST routes; retained the full-read Hehenkamp 308/319 kHz model and its unvalidated status. Added cold-region contrary evidence, and explicitly separated frozen half-space, finite TM slab and complete curved-earth propagation in 1M-C. | Primary Hill–Wait originals remain access-limited; no uniquely imported equation depends on them. No matched MF ice/snow/permafrost measurement was found. Boundary checks do not validate an end-to-end ice gain; a qualified layered propagation implementation is later work. |
| **GW05 — MF competing skywave** | Reconciled P.1147 composite loop observable versus P.1321 per-mode illustration, retaining the printed disagreement without assigning it wholly to physics or antenna projection. Defined geometry delay, post-receiver-plane ratio, phase, onset and coherent cancellation separately in 1M-D. | No matched near-1 MHz complex tap distribution or joint event prevalence. R, phase trajectory and ramp remain explicit controls. The historical crossover arithmetic is conditional and does not qualify a whip-receiver range. |
| **GW06 — named MF sources** | Every named original/lead has a read, access-limited or explicitly noncritical disposition in 20, including the two newly read R-Mode papers and empirical archive findings. No source is described as read merely because its record was found. | Unselected originals do not supply hidden numerical coefficients. The identified empirical gaps remain external; the selected equations have reviewed ITU/NTIA support. |
| **GW07 — NTIA archive and Parts II/III** | Downloaded, inventoried and parsed Phase 1; read the primary methods and sample records in Parts II/III. Reproduced six principal-site count/quantile rows, a paired loss-unit check and distance-stratified censoring example. Manifest, hashes, code and antenna/site exceptions are durable in 25; consumed by 30M-F. | These are spot received-loss observations at their actual 20/50 MHz frequencies, not exact 30 MHz complex data. No terrain solver fit was executed. A per-site fit would require the report-specific geometry/date/calibration inputs and retention of nondetections. |
| **GW08 — Rogers NLOS campaign** | Rechecked exact title/authors and available proceedings routes; the two-page primary contribution remains the only recovered body. Its campaign description is retained as context. | No fuller numerical outcomes or raw data recovered. It sets no loss/phase law and does not prove a surface-wave mode. A later public data release would be new evidence. |
| **GW09 — upper-HF temporal channel** | Reviewed actual observables in Hansen, Kirincich–Emery, Halverson, NTIA and the newly read SIMONe source; separated power, spot loss, radar products and complex phase. 30M-B/D retain explicitly chosen temporal controls. | No matched long-path 20–50 MHz forward groundwave complex/phase record found. Stationarity, fluctuation PSD and a natural temporal distribution remain unvalidated; a coherent campaign is external work. |
| **GW10 — rough sea and salinity** | Selected the full-read Barrick1970 mean-impedance theory as the mathematical basis; recorded spectrum/roughness limits and adjacent-frequency observational confounders. Added a source-conditioned mean-loss comparison in 30M-B2. Corrected the unsupported forward K/PSD inference. | Mean-loss theory does not determine incoherent fluctuation power or covariance. Barrick1971/Forget1982 bodies remain access-limited; no selected coefficient requires an unread body. A matched wave spectrum/complex channel is external; a full spectral impedance solver is a later implementation alternative. |
| **GW11 — atmosphere, terrain and structures** | Read Casciato2003's explicit 30 MHz coast/height theory; combined its applicability limits with NTIA terrain methods and existing short-range 40 MHz evidence. Kept Fresnel/two-ray controls, constant-radius versus atmosphere limits and source-specific loss conventions. | No calibrated long-path urban/forest/building temporal law or modern PE/FDTD field-validation claim. Homogeneous height gain cannot predict a cliff. A chosen terrain solver would need separate numerical and matched physical qualification. |
| **GW12 — competing modes and 180 dB** | Audited each retained meteor/aircraft/duct/backscatter/sea-clutter claim. Added full-read SIMONe32.55 MHz coexistence and processing limits; inspected its public dataset metadata and archive header. The 30M-D/E branches distinguish surviving wanted paths from total disappearance. | No calibrated 180 dB mode ranking, general event-rate law or direct-path-relative competitor spectrum. SIMONe payload range transfer timed out/failed connection; no payload replay claimed. That specific transfer/processing dependency matters only if a selected-product replay is pursued. |
| **GW13 — named upper-HF sources** | Every previously named upper-HF source/phenomenon has a disposition in 25, including source substitutions, the actual NTIA analysis, resolved Casciato identity, new backscatter evidence and shared P.372 reconciliation. | Unselected or access-limited bodies do not underpin new numerical claims. “Not found” is limited to recorded routes, not proof that a phenomenon or dataset does not exist. |
| **GW14 — numerical verification** | Pinned LFMF public vectors, power normalizations, field/range/height/radius/conductivity results and independent complex residue checks were executed in 90. Section 6 reports their scope; source hashes and tolerances are in 90. Mixed-path and scalar boundary checks are explicit. | Numerical agreement is not field validation. Native streaming fractional delay, sample-rate invariance, full noise/receiver planes, physical completion and modem outcomes are later implementation tests; none was counted as passed. |
| **GW15 — severity and coverage** | Section 5 maps every retained mechanism to its baseline/adverse/severe or outage comparison, source class, time/dependence status and receiver implication. Added coastal model disagreement, mean-sea-loss theory and observed terrain comparator without turning them into random natural laws. | Unmeasured dependence and extreme probabilities remain unknown. The Cartesian product of conditional stresses is not a measured natural worst case or a reliability prediction. |
| **GW16 — classical originals** | Recorded the individually named unread classics and why reviewed P.368/HDB-GW/NTIA equations or implementations suffice for the selected calculation. No broad recovery campaign is required by a uniquely used original result. | Revisit an original if a future selected solver depends uniquely on it. Current completion does not claim that every historical publication was obtained or read. |

No web-search-budget exhaustion prevented this pass. Public-data availability,
source reading, arithmetic verification, native implementation and empirical
qualification remain distinct outcomes. Better physical models and numerical
implementations can supersede these choices when their domains and evidence
are established.

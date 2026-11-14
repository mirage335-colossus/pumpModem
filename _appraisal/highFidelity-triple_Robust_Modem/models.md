# Implemented channel models and their boundaries

The native [scenario catalog](src/scenarios.cpp) defines reproducible mathematical
experiments. A scenario's evidence label travels into its execution receipt.
Passing a generator check establishes the stated numerical behavior; it does
not establish field calibration, real-world availability, modem sensitivity or
complete-receiver capacity. The [research reconciliation](research-reconciliation.md)
connects these choices to recent and contrary evidence. The larger
[reference collection](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/README.md)
remains the source for alternatives and unresolved measurement needs.

All reference waveforms use the canonical native source and physical time.
The default complex sample rate is **1920 samples/s**, twice the proposed
960-sample/s reference, to provide more fractional-delay interpolation margin.
This changes numerical sampling, not RF carrier, bit duration or propagation
rate. The native driver can select a separately labeled fast waveform for
integration tests. Such a test does not qualify the slow reference waveform.
Helper is a separate whole-composite comparison; the realistic VLF circuit
case is DATA-only because the distant Helper tones are outside its envelope
approximation and can be heavily suppressed by the antenna.

| Scenario | Reference waveform | Implemented comparison |
| --- | --- | --- |
| `awgn` | Rate36, added10, requested16s | Identity propagation and explicitly declared complex white-noise level |
| `vlf-resonant`, `vlf-detuned`, `vlf-phase-wander` | Rate0.1, added1, requested16s; native minimum gives about5760.4s active tile | Calculated high-Q antenna envelope; independent detuning and bounded two-hour phase-wander controls |
| `groundwave-1mhz`, `groundwave-1mhz-sea`, `groundwave-1mhz-night` | Rate1, added1, requested16s | Fixed LFMF magnitude reference and synthetic vacuum phase; optional competing ray |
| `groundwave-30mhz`, `groundwave-30mhz-land` | Rate36, added10, requested16s | Fixed LFMF magnitude at the upper model endpoint; synthetic phase |
| `hf-quiet`, `hf-disturbed`, `hf-transition`, `hf-nvis-disturbed`, `hf-drm6` | Rate36, added10, requested16s | Standard Gaussian-spectrum taps with distinct delay/centroid/width tuples; separate absorption episode |
| `eme-disk-episode`, `eme-patch-episode`, `eme-severe-episode` | Rate360, added1, FHSS, requested26.4s | Joint spectral synthesis for the 3m/30m beams and 164Hz adverse width, through 360s from channel time zero |
| `noise-bursts`, `noise-white`, `noise-white-ar1`, `noise-white-shot`, `frontend-overload` | Rate36, added10, requested16s | Constructed noise, matched component ablations and swept-blocker/equipment controls |

The source returns the actual geometry and calendar. Requested duration is not
an instruction to override its minimum coefficients, shaping, halos, public
hypotheses or slot recurrence. The receiver receives public bounds, noise floor
and waveform settings; it does not receive path realization, transmitted bits,
fade placement or the evaluator's final observation horizon.

## Common signal and energy convention

The same linear path sum acts on the complete native emission:

```
y_signal(t) = sum_p sqrt(q_p) g_p(t) exp(j phase_p(t)) x(t - tau_p(t))
tau_p(t) = tau_p(0) + delay_rate_p * t
phase_p(t) = phase_p(0) - 2*pi*RF*delay_rate_p*t
             + 2*pi*(residual_doppler_p*t + residual_drift_p*t*t/2)
```

Initial phase absorbs the fixed carrier phase. Geometric delay motion already
creates carrier Doppler; the residual term is an additional declared mechanism.
The delayed envelope supplies the corresponding offset-frequency phase. All
DATA carriers and Helper therefore share one physical operator. Signal-only
attenuation precedes receiver-local noise, and no realized message, fade,
window or episode is renormalized to unit energy.

The native emitted DATA has a deterministic reference, `mu*G*G`, from its
bounded fixed-bit power calculation. Report that reference and actual sample
energy separately. The scenario C/N0 values refer to a unit normalized DATA
reference at the stated plane before stochastic fading or antenna detuning.
Complex white noise obeys `E|n[k]|² = Fs*10^(-C/N0_dBHz/10)`, with half the
variance in each real component. A C/N0 override makes a conditional-level
experiment; it does not preserve the default physical link budget silently.

## Sub-9 kHz antenna envelope

The 200ft request is represented by the reference's rounded **61m vertical,
122m horizontal top wire, 2mm wire diameter**. These dimensions describe a
calculated T antenna, not a surveyed installation. The electrostatic/lumped
inputs are `C0=.902nF`, effective height `he=49.8m`, coil `Q=400`, ground
resistance `30ohm`, and loss tangent `.003`. At `fc=8270Hz`, the native catalog
computes

```
omega0 = 2*pi*fc
L = 1/(omega0²*C0)
X0 = 1/(omega0*C0)
Rrad = 160*pi²*(he*fc/c)²
R = Rrad + X0/400 + 30 + .003*X0
```

This gives `L≈.41060356H`, `R≈147.34965ohm`, and envelope decay time
`2L/R≈5.57319ms`. The narrowband complex envelope is normalized to unit
steady resonant current. Its exact exponential discretization retains turn-on,
turn-off and state through silence; it is compared with full series-RLC
impedance on a declared narrow offset band. It does not claim the response at
550/950/2400Hz from an 8270Hz first-order envelope approximation. Those remote
Helper tones are attenuated by tens of decibels in the full reference circuit.

`vlf-detuned` adds `+4.135Hz` fixed detuning and `+1e-5Hz/s` drift. The former
is the first-order resonance shift magnitude for a `.1%` capacitance change.
The actual implemented experiment holds L/C/R fixed and changes the envelope
frequency offset; it is not a full time-varying capacitance/ground-loss model.
Both values are engineering sensitivities, not measured weather statistics.

The `1000km/c` delay is a vacuum-phase control. No native LWPC/LMP waveguide
solver or frequency-dependent waveguide transfer is implied. The default
C/N0, approximately **−18.43dB-Hz**, comes from separate hypothetical magnetic
coordinates `2.47fT` wanted amplitude, `20fT/sqrt(Hz)` external noise and
`5fT/sqrt(Hz)` receiver noise. This conditional ratio is deliberately **not**
a conversion of the calculated electric antenna current or waveguide Ez into
magnetic field. Missing installation, antenna, vector-field and noise
calibration prevents an absolute range/efficiency claim. See
[Fable81 sections1/5](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/81-scenarios-sub9.md)
and the [antenna derivation and counterevidence](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md).

## Groundwave snapshots and competing path

The four magnitude snapshots are the independently reproduced outputs in
[Fable90 section1.1](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/90-cross-check-calculations.md):
[NTIA LFMF v1.1](https://github.com/NTIA/LFMF/tree/57886e9d0a29fd8f04a6416dc3f782322f5a3589),
100km, zero TX/RX height, vertical polarization, 1kW e.m.r.p.,
`Ns=301.1043147623609`, effective radius `6371km*4/3`. Sea uses
`sigma=5S/m, epsilon_r=70`; land uses `.003S/m,22`. No external solver is
silently rerun or extrapolated during these fixed-snapshot experiments.

| Frequency/material | E at1kW, dBµV/m | Basic loss, dB | Selected1W received power, dBm | Reference C/N0, dB-Hz |
| --- | ---: | ---: | ---: | ---: |
| 1MHz land | 37.847171738 | 104.138818513 | −70.652828 | 23.347172 |
| 1MHz sea | 68.489835437 | 73.496154814 | −40.010165 | 53.989835 |
| 30MHz sea | 39.150484393 | 132.377930952 | −98.891941 | 57.084254 |
| 30MHz land | −9.972689615 | 181.501104961 | −148.015115 | 7.961080 |

The displayed precision identifies the reproduced arithmetic, not physical
accuracy. Convert the1kW field to1W by subtracting30dB, then use the declared
grounded receive plane `Pr=E1−20log10(f_MHz)−78.5dBm`. Do not use LFMF's
returned received-power coordinate, which uses a different receive reference.
External noise uses fixed `Fa=80dB` at1MHz or `18dB` at30MHz above
`−174dBm/Hz`, plus independent `kB*100K` receiver noise. These Fa values are
design coordinates, not site medians or environmental percentiles. The signal
path uses normalized gain1 because C/N0 already includes the loss.

Every snapshot uses `tau=100km/c` and zero initial carrier phase. That phase
is synthetic: the public LFMF API returns magnitude, not phase. The magnitude
is held flat over the envelope, especially at30MHz where querying positive
sidebands would exceed the official model's upper frequency limit. No terrain,
coastal recovery, conductivity evolution or surface-wave dominance follows.
P.368 starts at10kHz and is not used for the sub-9kHz case. Its formal30MHz
endpoint is not proof that a real30MHz route excludes direct/reflected space
wave or an ionospheric competitor. [ITU-R P.368-10](https://www.itu.int/rec/R-REC-P.368-10-202208-I/en)
provides the reference's scope.

`groundwave-1mhz-night` adds one coherent ray at +2ms delay, relative power
`.25`, and +.01Hz residual frequency. Those values are a proposed competing
skywave sensitivity. The extra power is not normalized away and the tuple is
not attributed to a measured nightly path. The catalog does not combine
unrelated propagation extremes into a case described as measured.
The receiver retains its zero inter-ray-centroid model and overall public
frequency search. That is an intentional estimator mismatch: this experiment
does not qualify a matched two-centroid receiver, and a sensitivity loss must
retain that limitation rather than being attributed solely to a physical floor.

## HF standard and nonstationary controls

`hf-quiet` uses two independent equal-power zero-mean proper-complex Gaussian
taps at0/.5ms with **D=.1Hz**, and `hf-disturbed` uses0/7ms with **D=30Hz**.
These are the midlatitude-quiet and high-latitude-disturbed comparisons in
[ITU-R F.1487 Annex3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf).
D is the full **2σ power-spectrum width of each tap**. The ideal covariance is
`exp(-pi²*D²*lag²/2)` and ensemble power sums to1. The channel reuses the
canonical stationary `fixed-grid-gaussian-v1` native implementation, whose
finite FIR/interpolation has its own checked approximation. No periodic
finite-tone fading substitute is introduced for these taps.

The native scenario check now also calls the actual canonical
`FixedGaussianRay` at **D=.1 and30Hz**. It fixes32 seed episodes before
evaluation, each with1024 covariance pairs sampled at `dt=1/(8D)`, using raw
samples without per-episode power normalization. The two widths deliberately
share dimensionless realizations, so they are32 paired seed episodes, not64
independent trials. They agree with width/time rescaling to8.89e−16. The
following receipt applies to both widths at the corresponding physical lag:

| D×lag | Physical lag,D=.1 /30Hz | Raw covariance estimate | Ideal real covariance | Between-episode SEM,real /imaginary |
| --- | --- | --- | ---: | --- |
| 0 | 0 /0s | .997502+j0 | 1 | .007223 /0 |
| .25 | 2.5s /.008333s | .733364+j.008749 | .734603 | .006909 /.005210 |
| .5 | 5s /.016667s | .290166+j.012685 | .291213 | .007066 /.006383 |
| 1 | 10s /.033333s | .005739+j.013748 | .007192 | .007817 /.008340 |

The SEM is calculated across whole seed-episode covariance estimates, retaining
within-episode correlation. It is a descriptive Monte Carlo uncertainty
estimate, not a claim about natural HF reliability. The deterministic time-scale
invariant is asserted; these sampled covariances are reported against the
ideal law without fitting a post-outcome pass threshold. The renderer's existing
independent Gaussian moment/covariance checks remain separate.

`hf-transition` starts from the midlatitude-disturbed0/2ms,D=1Hz pair and
applies40dB signal-only absorption over seconds120..240, with10s cosine edges.
This interval is physical elapsed time, not re-timed to a favorable decoded
slot. All channel/noise state continues, and later DATA may return. This is a
constructed counterexample for erasure/premature physical completion, not a
measured ionospheric event or a population of blackout lengths. Standard
stationarity is a comparator; it does not qualify arbitrary disturbed or
long-duration skywave behavior. See the more detailed
[HF scenario evidence](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md).

## Coupled and measured HF extensions

The [adverse HF protocol](hf-experiments.md) and [source reconciliation](hf-research.md)
describe the additional presets and their evidence classes. The optional native
`HfModeProfile` leaves the earlier path arithmetic unchanged when absent.
It adds smooth per-mode voltage transitions, bounded delay motion, a coherent/
diffuse Rice mixture, single-port Jones projection, local dispersion, a coupled
parabolic-layer eikonal, and a bounded measured-envelope operator.

With baseband offset `nu`, the local transfer phase is
`phi(t) - 2*pi*nu*tau_g(t) - pi*D(t)*nu^2`. Its delay and Doppler derivatives
obey `d(tau_g)/dt = -d(f_D)/d(nu)`. The geometric moving-path control uses
`phi=-2*pi*rf*delta_tau`; the refractive model derives its carrier phase
separately from the phase path. Group delay is not substituted into that
geometric expression. Unmodeled collisions, caustics and cutoff crossing are
not implied by a passing analytic fixture.

For the vertical parabolic layer, let `b=f/fo<1`, bottom height `h`, and
semi-thickness `Y`. Round-trip phase and group lengths are

```
P = 2*h + Y*(1 - (1-b*b)*atanh(b)/b)
G = 2*h + 2*Y*b*atanh(b)
tau_g = G/c
D = 2*Y/(c*fo) * (atanh(b) + b/(1-b*b))
```

The renderer uses the carrier phase difference from `P(t)-P(0)`, the changing
envelope delay from `G(t)`, and the local frequency slope `D(t)`. Independent
quadrature integrates the refractive index and inverse index with a turning-
point substitution; tests also check frequency and time derivatives and actual
rendered off-center tones. Public delay and frequency allowances remain fixed
and are shared by each static/moving pair.

The [independent ensemble](hf-ensemble.md) extends `fo(t)` with at most two
additional sinusoids and one immutable native C2 quintic-knot field. For summed
amplitudes `A`, it admits the entire range `fo0 ± A` (at most five percent),
with `|fo'| ≤ Σ 2π A_i/P_i + 3.75 A_k/T_k`. Periods and knot spacing are at
least 30 s. The same eikonal supplies all phase/delay/dispersion values;
`|tau_g'| ≤ (f/fo_min) D(f,fo_min) |fo'|` also enters the monotone retarded-time
check. Occupied-band admission evaluates worst dispersion at the declared
minimum critical frequency, not a guessed sinusoidal minimum time.
Native tests cover negative-time and knot joins, frequency/time derivatives,
global bounds, chunk equivalence, and seed-domain isolation.

Signed analytic-HF placement is limited to ±86400 s. It shifts HF evolution
while keeping source/capture/additive-noise clocks and the physical-zero path
reference fixed. Capture-clock residual CFO/drift remain separate. Nonzero
generic linear path-delay rate, measured traces and non-HF paths cannot use
this shift. The explicit additive-noise seed changes no path realization.
These bounded controls permit independent placement and noise holdouts; they
are not an empirically calibrated ionospheric stochastic process.

The tiny dispersive correction is represented by an eighth-order centered
second-derivative stencil implementing the first-order expansion of
`exp(-i*pi*D*nu^2)`. Admission bounds its approximation on the stated source
band, requires sufficient physical delay, and charges four additional samples
of source support and filter halo. Positive/negative slopes, interpolation
accuracy and finite-source tails have separate checks. This numerical operator
is not a license to amplify normal intramode dispersion into an invented HF
failure mechanism.

Jones mode gains are `(p_rx^H e_m)*(e_m^H p_tx)`. Both launch and receive
projection are charged once; the two orthonormal modes sum before receiver
noise. Receiver-terminal historical Watterson fits receive no second Jones
loss. A single linear antenna can observe destructive interference even when
the full vector field retains energy; compute cannot recover energy absent at
that port.

The Ding fixture embeds the exact deposited samples and their hashes. It uses
one full-record RMS normalization, interpolates Cartesian complex voltage,
and only then applies an optional amplitude/phase ablation. Offset and clock
controls retain the same normalized record. Admission validates finite data,
sampling, offset and support; time never wraps. Measurements and model code
remain distinct: the record lacks a clock, absolute gain and instrument-filter
calibration. Its observed phase-statistic discrepancy and the complete-slot
versus complete-message boundary are recorded in
[the fixture provenance](fixtures/hf-ding-2021/README.md).

### Separate cutoff/reflection component

The [scalar full-wave probe](hf-fullwave.md) solves the static, normally
incident, unmagnetized cold-plasma wave equation with vacuum input/output
ports. The same electron-density and collision profiles determine phase,
reflection, transmission and absorption; there is no additional independently
prescribed fade multiplier. Collisionless cutoff is admitted by that boundary-
value calculation, not by weakening the existing eikonal admission checks.
Its spatial-grid and frequency-step checks concern numerical component
accuracy. They do not qualify O/X coupling, oblique paths, three-dimensional
scattering, caustics, a time-varying waveform or complete reception.

### Canonical GUI settings are a separate channel stratum

The native GUI-profile adapter calls the canonical profile preparation path
and uses its native `StreamingChannel` directly. It does not stack the
appraisal eikonal renderer around it. This preserves the application's
waveform, public search geometry, endpoint clock draws and physical channel
placement without silently applying clocks or tails twice. A result must
report the requested settings and resolved profile, including any explicit
fixture bit duration distinct from the target-C/N0 planner's duration.

The source authority is [`robust_profile.cpp`](../../src/robust_profile.cpp):
`each-65` sets slow and fast selective depths to 0.65 separately. The channel
uses bounded Gaussian-erf coefficients; 65% is neither an occurrence
probability nor a bound of ±65% on the composite received waveform. The
physical fast coefficient width is one tenth of the declared modulation
allowance, with a further factor of ten for slow evolution. Public margins,
receiver oscillator bounds and actual endpoint oscillator draws are distinct
quantities; use the resolved values rather than applying one multiplier to
all of them. A canonical setting check establishes application semantics,
not independent environmental calibration.

## Noise and equipment controls

These two cases exercise the native renderer's stateful noise and equipment
paths independently of propagation. Their mechanism choices follow
[Fable85 C1–C3](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md),
with an explicitly different, bounded digital-baseband parameterization.
They are not replays, calibrated storm laws, or models of a named receiver.
The unit DATA reference and **32 dB-Hz** define the fixed baseline complex
white variance `v0=1920/10^(32/10)≈1.2114381`. A level override changes the
white term only; pulse and colored strengths retain this reference. Neither
the average of a realized noise trace nor received DATA controls those levels.

`noise-bursts` adds three separately seeded terms:

- Proper complex white receiver noise of variance `v0` at the reference rate.
- Stationary proper complex AR(1) colored noise of variance `.25*v0`, with
  `a=exp(-2*pi*3/Fs)`. The 3 Hz corner labels the sampled first-order law;
  it is not a brick-wall band limit.
- Independent Poisson arrivals at **8/s**, proper complex Gaussian marks
  with `E|mark|²=2*v0/(8*.02)`, and causal **20 ms exponential decay**.
  The continuous-time mean shot power is `lambda*E|mark|²*tau/2=v0`.
  The renderer supplies stationary prehistory to its stated decay tolerance,
  retains overlapping events, and keeps state through source silence.

The pulses have finite energy, with exponentially decaying rather than
compact support. This is not Fable85's 5 ms double-exponential shape,
bimodal energy distribution or four-child clustered arrival branch.
Clustered impulses can have materially different quiet-slot and false-DATA
outcomes even at equal average power; this remains a specific useful
alternative when that question is investigated. The sampled process does
not establish an analog antenna/preselector bandwidth or an HF lightning
occurrence rate. Signal fades do not attenuate any receiver-local noise term.

`frontend-overload` adds a continuous blocker of amplitude **30**, power900
relative to unit DATA, with instantaneous frequency `-150+t/6 Hz` and phase
equal to its continuous integral. The declared **0–1800 s** control sweeps
from −150 to +150 Hz without restarting. It then applies the actual native
chain: 300 Hz one-pole lowpass, attenuation-only AGC with target magnitude1,
20 ms attack and2 s release, radial clipping at .8, and12-bit component
quantization. The filter starts at0 and gain at1: the acquisition transient
is part of this control, not claimed stationary nonlinear initialization.
The blocker continues through physical quiet and may correctly prevent a
completion decision. Its integrated phase, filter and gain never restart at
chunks or slots.

The frontend model has no upstream LNA/mixer intermodulation, reciprocal
mixing, AM/PM compression or measured hardware P1dB/IP3. Its ADC/filter order
must not be presented as universal RF hardware. The declared sweep stays
inside Nyquist at1920 samples/s for the entire1800 s interval; the renderer
independently rejects a requested longer trajectory if it reaches Nyquist.
Noise spectral tails are properties of the declared sampled processes, not
proof of zero analog aliasing. Native scenario tests verify these strengths,
enabled state and sweep endpoints; native renderer tests separately verify
noise moments, pulse memory, clipping and gain behavior.

## EME coupled kernel and numerical limits

The implemented hypothesis is a **frozen monostatic rotating sphere at5.760GHz**,
not the full retarded bistatic DE440 calculation in the reference. It reuses
lunar radius `a=1737.4km` and the recorded minimum-path delay anchor
`tau_bulk=2.4720213723s`, while independently selecting a **limb-to-limb88Hz**
residual spread for `eme-disk-episode`/`eme-patch-episode` or **164Hz** for `eme-severe-episode`.
The severe coordinate comes from the5760MHz row in
[Taylor's2010 EME analysis, p5](https://wsjt.sourceforge.io/EME2010_K1JT.pdf):
86Hz median and164Hz maximum for its specified self-echo geometry at
latitude±40°. Those values are conditional geometric calculations, not a
target-band measured spectrum or universal maximum; the paper explains that
near-equatorial and two-station geometries can differ. The severe experiment
retains the3m beam, scattering shape, power reference and bulk-delay anchor to
isolate libration rate. It does not assert that this delay and severity occurred
at the same ephemeris epoch.

Bulk carrier Doppler is assumed continuously and perfectly
compensated; bulk envelope delay is held fixed. That explicit idealization
cannot qualify an imperfect ephemeris tracker or changing two-leg geometry.
The receiver's Gaussian `2 sigma` width field is zero for these cases: an
88Hz or164Hz lunar limb support is not a Gaussian spectrum. A separate public
44Hz or82Hz atmospheric-frequency bound selects changing-channel receiver
hypotheses without disclosing the cell realization or claiming its shape
matches a Gaussian.

For each surface incidence `theta` and azimuth `phi`,

```
mu = cos(theta)
tau = tau_bulk + 2*a*(1-mu)/c
f = (limb_width/2)*sqrt(1-mu²)*cos(phi)
delay_rate = -f/RF
```

The renderer moves envelope delay by this same slope and produces its carrier
phase from it, so geometric Doppler is counted once. Each cell has one
immutable proper-complex Gaussian coefficient; it is not redrawn at a chunk,
bit, slot, quiet interval or message boundary. There is no nonfading direct
ray and no realized-field renormalization.

T52 takes eleven opposite-circular, per-surface-area rows at3.8/23cm from
[MIT Lincoln Laboratory's1967 report, TableII](https://ntrs.nasa.gov/api/citations/19670030824/downloads/19670030824.pdf).
At each angle the dB values interpolate logarithmically in wavelength; between
angles **linear power interpolates linearly in angle**. The first value extends
to0°, and the87° value tapers linearly to zero at90°. The exact integral of
this piecewise-linear law times `sin(theta)` supplies the fixed widebeam
normalizer. No extra cosine is applied. The data and interpolation choices are
also recorded in [Fable84](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/84-scenarios-eme.md).
A model interpolated from these wavelengths is not measured5.76GHz scattering.
The more adverse Doppler support does not resolve that independent uncertainty.

The renderer uses32 equal **T52-power** rings, sampled at each bin's midpoint
quantile, and128 equally spaced azimuth midpoints:4096 cells. Power quantiles
resolve the strong forward lobe better than a small equally spaced area grid.
Each cell starts with weight1/4096. Centered identical Gaussian power beams
multiply this weight by `exp(-8 ln2*(angular_offset/HPBW)²)`, where
`HPBW=70*lambda/D` degrees. The3m and30m experiments vary footprint while
holding boresight reference power fixed; they do not claim the corresponding
aperture-gain improvement of a real30m dish. The conditional widebeam
reference is10dB-Hz. Beam loss is never normalized away. Polarization diversity,
beam offsets, topography, nonuniform materials and evolving footprint are not
implemented by this scalar frozen model.

The independent native model check compares moments against4096 power rings
and uses the analytic azimuthal covariance `J0(2*pi*f_max*lag)` as an oracle.
The2048→4096-ring oracle change is also measured. Tested frequency separations
are0/75/150/225Hz; short lags are0,1/60,.05s; extended diagnostic lags are
.1,.5,1,2,4,26.4s. A direct build with GCC on13November2026 produced:

| Numerical observable | 3m beam | 30m patch holdout |
| --- | ---: | ---: |
| Mean normalized power | .940449 | .237194 |
| Absolute power integral error,32-ring versus4096-ring | .00004454 | .00008299 |
| Absolute mean-delay error | 6.41µs | .1167µs |
| Absolute RMS-delay error | 26.71µs | .05698µs |
| Absolute RMS-Doppler error | .00462Hz | .00215Hz |
| Maximum unnormalized joint covariance error,short lags | .010673 | .0002290 |
| Maximum joint covariance error,extended diagnostic lags | .025940 | .0134943 |
| Maximum2048→4096-ring Bessel-oracle change | .0000857 | .0000659 |

The executable test accepts beam power error<.002, mean/RMS delay errors
<20/60µs, Doppler RMS error<.1Hz, short-lag covariance error<.015 and oracle
refinement<.002. These are engineering numerical criteria for the specified
observables. The30m patch holdout uses the same criteria and independent fine
axisymmetric integration; its tolerances were not retuned after its result.
**The extended-lag error is reported, not accepted as universal
long-integration accuracy.** The finite spectrum can produce recurrence or
long-lag structure absent from the continuous sphere. It must remain an
explicit model discrepancy in an EME receiver conclusion. More cell sampling
or continuous per-delay-ring spectrum synthesis merits investigation when a
receiver result changes under grid refinement; a favorable small-grid decode
does not dismiss the weakness.

A separate deterministic projection now demonstrates a consequential weakness
of the32×128 disk grid. For frozen independent Gaussian cells, the variance of
the normalized coherent time average is exactly

```
V(T) = sum_p q_p sinc²(pi*f_p*T),   sinc(x) = sin(x)/x.
```

This is a channel projection, not a decoder success probability. The reference
uses4096 power rings and continuous azimuth integration, with the independent
2048-ring refinement. The continuous ring average follows
`[2 integral_0^A J0(2u) du - J1(2A)]/A`, where `A=pi*f_max*T`.
Prefix Simpson integrals with steps.04/.02 supply bounded-cost evaluation;
direct32768/65536-azimuth sums at seven arguments from0 to`pi*82*26.4`
cross-check the identity and integration. Their largest disagreement is
`6.66e-12`, and the largest direct-azimuth refinement is`1.09e-16`.
No million-cell waveform or matched-random-grid decode is needed.

| Limb width | Average time |32×128 variance | Continuous-azimuth reference | Absolute error | Relative error | Error,dB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
|88Hz |.25s |.140628 |.141216 |.00058751 |−.416% |−.0181 |
|88Hz |.5s |.0714269 |.0735298 |.00210284 |−2.860% |−.1260 |
|88Hz |1s |.0335351 |.0374899 |.00395481 |−10.549% |−.4841 |
|88Hz |4s |.00554306 |.00949988 |.00395682 |−41.651% |−2.3397 |
|88Hz |26.4s |.0000881011 |.00143799 |.00134989 |−93.873% |−12.1277 |
|164Hz |.25s |.0768258 |.0786844 |.00185859 |−2.362% |−.1038 |
|164Hz |.5s |.0361669 |.0401764 |.00400953 |−9.980% |−.4566 |
|164Hz |1s |.0160892 |.0202953 |.00420617 |−20.725% |−1.0086 |
|164Hz |4s |.00204950 |.00509137 |.00304186 |−59.746% |−3.9519 |
|164Hz |26.4s |.0000289079 |.000771573 |.00074267 |−96.253% |−14.2636 |

The2048→4096-ring reference changes at26.4s are`4.03e-6` and`2.16e-6`
absolute, about.28% of each reference. Across all rows the largest absolute
ring change is`2.36e-5`; the largest integral-step change is`6.31e-12`.
These refinements remain separate from a rigorous error bound, but cannot
explain the much larger finite-grid discrepancy. The.25/.5/1s rows also expose
the error at short receiver coherence horizons. No acceptance cutoff was
chosen after observing these results.

**The current disk and severe grids do not qualify long-integration EME
sensitivity.** Their finite azimuth lattice undersamples near-zero projected
Doppler as integration time increases. The patch retains its separately
reported, unqualified extended covariance error; the table above is only for
the3m beam. A timeout or decode failure with these grids is not evidence of a
physical sensitivity floor. More appropriate continuous-spectrum synthesis or
a demonstrably converged quadrature needs qualification before interpreting
such a floor. Merely increasing to64×256 and comparing uncoupled frozen
Gaussian cell draws would confound the numerical change with channel
realization, so no such preset or paired-decode claim is introduced.

### Doppler-aligned first-slot comparison

**Historical, retired operational presets:** `eme-disk-slot` and
`eme-patch-slot` used a separate bounded quadrature. Their failed/full-horizon
and positive short-horizon findings are retained here; the executable now uses
the full-calendar spectral presets below. The old comparison retained the 88 Hz width, 3 m/30 m centered beams, original T52
power law, physical waveform, reference C/N0 and coupled moving delays.
Their numerical horizon is **30 seconds from channel time zero**. The planned
first DATA slot, including its approximately 2.47-second propagation delay,
can fit in that horizon; the current 269.466-second five-slot episode cannot.
The historical six-slot driver horizon was roughly 329 seconds.
This is not qualification of message completion or two scored quiet slots.

The parameterization aligns a quadrature axis with Doppler:

```
x in [-1,1], psi in [-pi/2,pi/2]
mu = sqrt(1-x*x) cos(psi)
f = 44*x
tau = tau_bulk + 2*a*(1-mu)/c
dA/a² = dx dpsi
```

There are 4096 uniform midpoint nodes in `x` and 16 Gauss–Legendre nodes in
`psi` on `[0,pi/2]`. The mirror cells at opposite `psi` have identical delay,
Doppler and centered-beam weight. Adding their independent proper-complex
Gaussian variances combines them into one coefficient without changing the
Gaussian process law. Thus that retired factory produced exactly **65,536 paths**,
within the renderer limit. It deliberately exposes no arbitrary grid or
severe-width option that could silently invalidate this bound.

For a Legendre weight `w` on `[-1,1]`, each cell has
`q = F(acos(mu))*beam_loss*dx*w/(4*I)`, where
`I = integral_0^(pi/2) F(theta) sin(theta) dtheta` is the same exact original
widebeam normalizer. The factor includes the transformed interval and mirror
variance. Neither sampled total power nor realized field energy is normalized.
The renderer continues to derive carrier Doppler from the same `delay_rate`.

Native checks compare this coordinate system against the original
4096-power-ring representation with analytic Bessel azimuth integration for
joint covariance. They independently integrate projected averages in the
original ring/azimuth coordinates at durations .25/1/4/26.4 seconds and centers
0/±5/±15 Hz. Dense azimuth counts follow the maximum angular phase rate;
halving those counts and changing 4096 to 2048 power rings report separate
refinement errors. Positive and negative frequency centers agree by symmetry.
The full native scenario test, including the previous negative controls,
took 4.51 seconds in the direct local build on 13 November 2026.

| Numerical observable | 3 m disk slot | 30 m patch slot |
| --- | ---: | ---: |
| Mean normalized power | .939205682 | .237469482 |
| Signed power error against original fine-ring integral | −.00119870 | +.00019231 |
| Signed mean-delay error | +.4774 µs | −.09464 µs |
| Signed RMS-delay error | +1.6343 µs | −.37680 µs |
| Signed RMS-Doppler error | +.009549 Hz | −.002048 Hz |
| Maximum absolute joint covariance error through 30 s | .00172445 | .000495637 |
| Maximum covariance change, 2048→4096-ring oracle | .000428471 | .000351144 |
| Maximum projected-variance error over tested times/centers | .0341021 dB | .0923665 dB |
| Maximum projection change, azimuth refinement | 2.18e−11 | 4.92e−12 |
| Maximum projection change, radial refinement | 2.351e−5 | 2.352e−5 |

Both pass the original beam-energy, delay/Doppler moment and short-covariance
criteria without retuning those criteria. The additional projections and
extended covariances are numerical diagnostics, not a newly invented universal
sensitivity tolerance. Radial oracle error remains material to the smallest
projected variances; reporting angular convergence alone would hide it. These
comparisons establish the stated finite numerical experiment, not field
calibration or complete-receiver throughput.

The uniform Doppler spacing is `88/4096 Hz`, producing an exact covariance
recurrence at **4096/88 = 46.54545 seconds**: midpoint samples have covariance
equal to minus their total mean power there. The native test checks this
failure explicitly. A bounded first-slot run must reject a rendered or oracle
episode beyond 30 seconds; it cannot silently substitute this grid into the
full physical calendar. Channel time does not restart at each chunk or slot.

The bounded alternatives investigation also preserves two negative results.
Increasing the old grid to 32×2048 cells left 26.4-second disk projection errors
of −.909 dB at 88 Hz and −1.779 dB at 164 Hz. Concentrating angular quadrature
near zero Doppler repaired that one projection but did not converge the tested
joint covariances. Those methods are not adopted. For the severe width,
8192×16 Doppler-aligned cells gave small tested projection/covariance errors,
but need 131,072 paths and recur at 49.951 seconds. The 8192×8 alternative fits
the path cap but failed the existing .002 beam-energy criterion. Those
literal path sums did not provide a severe or whole-episode replacement.

This test takes no target-band measurement as calibration and has no field
sensitivity acceptance threshold. Lunar scattering uncertainties and numerical
quadrature error are separate. A literal hundreds-of-thousands-cell waveform
sum is not required merely because it is a convenient oracle for integrated
moments.

### Joint spectral synthesis over a complete calendar

The episode presets retain the same frozen sphere, T52 power law, centred
beam pattern, 5.760GHz RF and physical calendar as the controls. The 360s
numerical support includes the complete three-bit `e` reference episode and
its two physical quiet slots. Geometry and receiver search priors are
unchanged. Model support and receiver success are separate observations.

The renderer integrates the same Doppler-aligned coordinates described
above on a fixed Fourier lattice at 240 latent samples/s with 262,144 bins.
Each Doppler bin receives 32 Gauss–Legendre nodes over its conditional
azimuth coordinate. The limb cells keep their clipped frequency-bin mass.
The renderer draws independent unit proper complex Gaussian variables in
signed Doppler-bin and node order using `mt19937_64` with a separate path
seed domain. It multiplies each draw by the square root of its power cell.
The uniform spacing is about .00091553Hz, and the fundamental carrier
lattice period is 1,092.2667s. No process restarts at a read, slot, quiet
interval or message. The requested observation length only determines
how many inverse-FFT samples are retained. The 360s limit follows the
tested observables below, rather than the period alone.

For one cell with carrier Doppler `ν`, initial envelope delay `τ`, and
coefficient `g`, the existing normalized Blackman-sinc interpolator
represents `x(t−τ)` by weights `w_j` on adjacent native source samples.
The reduced renderer adds `g w_j` to the spectrum of every affected
integer-delay coefficient and `g (ν/ν_max) w′_j` to its derivative spectrum.
The **same** Gaussian draw goes to every affected coefficient in both
spectra. As a result, the transfer functions at distinct signal frequencies
retain the same scatterers and correlated delay response. Independent
same-spectrum tap draws would discard that coupling.

The canonical native inverse FFT turns those spectra into correlated
complex time series. At native sample `n`, reconstruct each coefficient
with the same type of 32-point windowed sinc and combine its derivative
with factor `ν_max n/RF`. This retains the first-order term in the
geometric change `τ(t)=τ−νt/RF`, with no approximation of the carrier
phase beyond the declared Doppler binning. At 360s and ±82Hz the maximum
envelope movement is .00984 native samples. At the 362.5Hz source guard
the ideal sinusoidal first-order Taylor remainder is at most about
6.9×10⁻⁵ in voltage relative to a unit sinusoid. The actual renderer also
has finite-stencil and binary32 latent FFT/interpolation error. The
same-seed moving-facet check below includes those terms. The implementation
uses binary64 for model geometry, weights and spectral accumulation before
the FFT buffer conversion, and for the final sample accumulation.

The full-horizon test compares the candidate to an independent 2,048/4,096
power-ring calculation with analytic Bessel azimuth covariance. It includes
lags 0–360s and frequency separations 0/75/225/725Hz. A separate
off-lattice Doppler integral splits the original T52 angular knots and
checks projected averages over .25/1/4/26.4/269.466/329.466/360s at
0/±5/±15Hz. That integral refines both Doppler nodes and the smooth interval
quadrature. The real (symmetrized) cross-slot covariance compares pairs of 26.4s windows with
60/120/240s offsets. All checks preserve the original wide-beam power
reference. The 16-node trial missed the .015 joint-covariance consistency
limit at 725Hz, despite matching its low-frequency projections. Increasing
the conditional quadrature to 32 nodes retained the test limit and reduced
the worst sampled error from .03024 to .000304 in the disk case.

| Sampled numerical observable | 3m disk, 88Hz | 30m patch, 88Hz | 3m disk, 164Hz |
| --- | ---: | ---: | ---: |
| Mean normalized power | .940553 | .237218 | .940553 |
| Mean excess delay | 1.56292ms | .123270ms | 1.56292ms |
| RMS excess delay | 2.06939ms | .144616ms | 2.06939ms |
| RMS Doppler | 14.580Hz | 4.50789Hz | 27.1719Hz |
| Maximum absolute joint covariance difference | .00030352 | .00009973 | .00026741 |
| Maximum independent power-ring refinement change | .00029926 | .00027626 | .00071005 |
| Maximum projected-variance difference | .006531dB | .003044dB | .006531dB |
| Maximum continuum projection refinement change | .00000411dB | .00003633dB | .00000840dB |
| Maximum cross-slot covariance difference divided by its projected variance | 2.53×10⁻⁹ | 4.68×10⁻⁹ | 8.83×10⁻¹⁰ |

With the severe preset, six sparse native waveform probes used the same
seed as a direct moving-facet sum, including ±362.5Hz source tones and
timestamps through 329.467s. The maximum complex sample difference was
3.35×10⁻⁵. This test includes both latent-grid and interpolated sample
phases. These moment and waveform comparisons qualify the stated numerical
probes. They do not establish calibration against 5.76GHz captures,
evolving lunar orientation or bistatic geometry.

At 360s the spectral workspace allowance is 303,333,216 bytes before
the channel's source/history reservation. The construction FFTs 108
delay/derivative rows, then releases the spectral buffer. It retains
the requested prefix and its interpolation support. A severe reference
channel preflight rendered 19,200 samples representing 10s at 10dB-Hz
with seed 9101. It took 11.19s elapsed at 235,444KiB peak RSS, including
about 11.09s of channel preparation and .043s of sample rendering. It
exercised the native canonical source and channel. Its render timing does
not measure native receiver throughput. The numerical and preflight
receipts are in the appraisal's temporary `local-evidence/` directory.

## Reproduction and retention

The five superseded operational lunar presets are removed, without aliases to
the new physics/numerics. The independent `lunar_paths` construction and concise
coarse-grid projection failure checks remain numerical references for the
current model, not alternative operational receiver campaigns.

The native scenario test is `tests/test_scenarios.cpp`, built through the
appraisal's contained CMake target. It verifies the catalog/provenance, four
reference-plane conversions, antenna arithmetic, F.1487 widths/powers,
invalid-input rejection, beam-energy loss, coupled delay–Doppler support and
the stated quadrature comparisons, including the explicit finite-horizon
Doppler-lattice recurrence. Renderer checks independently exercise
sample-level state, delay, filtering and stochastic moments. Native receiver
checks are a third evidence layer.

Model sources and concise results belong here. Builds, raw captures and trial
receipts belong in this subtree's ignored temporary output paths. The
quadrature receipt above serves the current finite-cell numerical limit;
replace it when the implementation or tested grid changes, preserving any
unresolved failure. No external solver source, large ephemeris kernels or
archived whole campaign is required by these fixed reference models.
The temporary lunar grid scouts and discriminator sources, binaries and outputs
were removed after integration. The maintained native tests and concise
positive/negative findings above retain their relevant evidence. Current native
trial receipts remain while the receiver failures and numerical limits they
document are unresolved; obsolete cloud source bundles were removed after the
results were retrieved.

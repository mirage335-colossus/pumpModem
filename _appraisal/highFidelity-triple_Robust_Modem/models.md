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
| `vlf-resonant`, `vlf-detuned` | Rate0.1, added1, requested16s; native minimum gives about5760.4s active tile | Calculated high-Q antenna envelope; detuning is an independent sensitivity |
| `groundwave-1mhz`, `groundwave-1mhz-sea`, `groundwave-1mhz-night` | Rate1, added1, requested16s | Fixed LFMF magnitude reference and synthetic vacuum phase; optional competing ray |
| `groundwave-30mhz`, `groundwave-30mhz-land` | Rate36, added10, requested16s | Fixed LFMF magnitude at the upper model endpoint; synthetic phase |
| `hf-quiet`, `hf-disturbed`, `hf-transition` | Rate36, added10, requested16s | Two standard Gaussian-spectrum Rayleigh taps; separate absorption episode |
| `eme-disk`, `eme-patch`, `eme-severe` | Rate360, added1, FHSS, requested26.4s | Coupled frozen spherical delay–Doppler kernel; 3m/30m beams and separate164Hz severe self-echo geometry |
| `eme-disk-slot`, `eme-patch-slot` | Same88Hz reference waveform and link level | Doppler-aligned65536-cell comparison, restricted to30s from channel time zero |
| `noise-bursts`, `frontend-overload` | Rate36, added10, requested16s | Separate constructed colored/impulse-noise and swept-blocker/equipment controls |

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
residual spread for `eme-disk`/`eme-patch` or **164Hz** for `eme-severe`.
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

`eme-disk-slot` and `eme-patch-slot` add a different bounded quadrature without
replacing the original finite-grid controls or their failed long-integration
evidence. They retain the 88 Hz width, 3 m/30 m centered beams, original T52
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
Gaussian process law. Thus the factory produces exactly **65,536 paths**,
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
the path cap but failed the existing .002 beam-energy criterion. There is no
qualified severe replacement or full-episode replacement in these results.

This test takes no target-band measurement as calibration and has no field
sensitivity acceptance threshold. Lunar scattering uncertainties and numerical
quadrature error are separate. A literal hundreds-of-thousands-cell waveform
sum is not required merely because it is a convenient oracle for integrated
moments.

## Reproduction and retention

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

# Transverse HF diffraction appraisal

The baseline is `e3bb7cf04c84cffa8797d821dabf2e6a294de016`. This first
branch adds a transverse refractive mechanism complementary to
[the stratified Jones reflection model](hf-magnetoionic.md). It does not
replace that model or qualify an operational channel. The physical and
numerical choices below precede native outcomes.

## Physical construction

Use a scalar, collisionless, phase-only lens on the ascending leg of one
ideal reflected hop. Unfold an ideal planar reflection; the nominal distance
from source to screen is 200 km and from screen through the fold to receiver
400 km. Interpreted as a symmetric 600 km ray at 60° elevation over a flat
surface, the turn height is about 260 km, the screen near 173 km and the
other crossing at that height about 100 km away. The fold supplies only
the known specular geometry. It does not solve for the background reflection
coefficient, cutoff, actual height, or ground-wave coupling. Incident C/N0
is referred to normalized voltage for this nominal path, with geometrical
spreading already in that reference; no screen-dependent rescaling occurs.

The 10 MHz carrier has effective Fresnel distance `D=L1 L2/(L1+L2)`
of 133.33 km and Fresnel radius `sqrt(lambda D)` near 2.0 km.
Use a Gaussian perturbation of the ray-normal column with standard
deviations 1.6 and 2.4 km. A 3 km effective optical thickness with
ambient electron density `5e10/m^3` gives 0.015 TECU. The primary
depletion is −0.00857 TECU, leaving `2.14e10/m^3` at its center.
The screen-off control keeps the same ambient column. Nominal screen
drift is 120 m/s, y impact zero and closest screen epoch 128 s; the
separate 1.2 km impact/87 m/s/219 s holdout is fixed independently of
receiver outcomes. Physical phase, diffraction, gain, and dispersion use
the same column at every time and RF frequency.

For `exp(-i omega t)`, the differential phase is
`k ell [sqrt(1-X_b-X_d g)-sqrt(1-X_b)]`, where
`X = e^2 N_e / (eps0 m_e omega^2)`, `g` is the unit Gaussian, and
`ell=3000 m`. The ambient phase relative to vacuum is
`k ell [sqrt(1-X_b)-1]`. Add the ambient and total vacuum phases once.
The carrier has `X_b≈.0403`, a center differential phase near 7.35 rad
and center x/y curvature ratios about 1.83/0.81 relative to the Fresnel
quadratic. The one-axis ray fold is a location for wave witnesses, never
a divergent gain prescription. Near a cancellation only a complex
error interval is meaningful; do not divide by a zero field.

For the nominal-axis point receiver, the normalized scalar field is
`1 + [1/(i lambda D)] integral exp(i k |u|²/(2D)) [exp(i phi(u))-1] d²u`,
times the ambient/geometrical phasor. This subtracts the exact bare
reference before integrating the localized perturbation. In production,
expand the exact square-root phase exponential in powers of `g`.
Each Gaussian moment factors into two analytic complex integrals.
Cauchy's coefficient estimate and the 2D integral bound
`2*pi*sigma_x*sigma_y/m` bound the discarded series terms. The same
coefficient majorant with the Gaussian displacement decay bounds the
perturbed field beyond 12.8 km before replacing it with the exact ambient
response. Advection has one uninterrupted physical clock.

The scalar column uses the square-root eikonal, rather than dropping
terms after the linear differential TEC law; even the ambient phase
shift of about −12.8 rad would move by about 0.13 rad under that
linearization. The prescribed phase screen still neglects transverse
diffraction within its thickness, ray walkoff there, return from its
interfaces, collisions, and magnetic mode coupling. At 10 MHz the
dimensionless thickness `ell/(k sigma_x²)` is about .0056, the peak
screen deflection is about .013 rad, and the corresponding thickness
walkoff scale is 40 m or 2.5% of `sigma_x`. The quartic free-path phase
term at transverse radius 7.2 km is about .01 rad across both legs; these
are scaling estimates, not complex-field error bounds at a null. A
50-µT field would give `f_gyro/f≈.14`, so an oblique actual magnetic
mode cannot be presumed equivalent even for an underdense column. Within
this model a stationary-ray-scale transverse excursion of several km gives
sub-microsecond excess geometric delay and drift phase evolving on tens of
seconds; it does not fill millisecond spread-F, fast distributed scatter
or near-cutoff reflection-caustic coverage.

[Hocke and Igarashi (2003)](https://doi.org/10.1029/2002RS002691)
show that wave diffraction and interference matter in oblique HF focusing
and caustics. [Ding et al. (2021)](https://doi.org/10.1029/2020RS007239)
report one 41-second 12.7 MHz skywave observation and a fitted distributed
multiple-screen model, with model/observation limitations detailed in
[HF research](hf-research.md#nonstationarity-and-scintillation-avoid-an-immortal-coherent-component)
and the [review of HF simulators](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md).
[Grillo and Cordes (2018)](https://arxiv.org/abs/1810.09058) provide the
two-transverse-coordinate plasma-lens wave integral and caustic context
in a different physical application. These sources motivate the model;
they do not calibrate the chosen column, motion, aperture or frequency.

## Numerical admission and cost pilot

The direct complex surface integral uses the scalar index from dielectric
constants, without the production expansion. Fix five spatial cases:
center, a geometric-fold neighborhood, an off-axis/edge-frequency
holdout, the opposite column sign, and far rim. Add one independent
off-frequency quadrature at the minimum of a predeclared 641-point
carrier-only scan out to 6.4 km. Check 100/50/25 m at 6σ and a separate
8σ window at 25 m. The sum of fine-versus-wide, middle-versus-fine,
and production-versus-wide sampled complex differences must be at
most 2e−8 in nominal path voltage, with the resolved nonzero test
below. These are witness differences, not a uniform continuum bound.
Use at most 30 million spatial cells. There is no production lateral
grid or marching step in this one-screen analytic solver.

Check flux separately with an incident finite Gaussian field through the
unit-modulus screen and the canonical native Fourier propagator. Require
global integral relative error ≤2e−12, outer-eighth power fraction
≤1e−8, unscreened analytic Gaussian error ≤2e−8, and reciprocal numerical
propagation/screen undo error ≤2e−12. Separate 256²/125 m,
512²/62.5 m (density) and 512²/125 m (padded width) checks must keep
the central complex difference from the original ≤2e−8. Local intensity
can exceed one; finite-beam flux is a separate invariant. Plane-wide
finite energy cannot be inferred by summing the nominal-point gain
along a trajectory.

The causal simulation supports 1920 samples/s and occupied ±384 Hz.
Reuse the native guard-taper FIR designer with the 600 km vacuum
earliest arrival (3.8427 samples), explicitly add 32 numerical delay
samples, retain lags 4..84, and charge the complete 84-sample tail in
the observation calendar. Raw band-limited prearrival energy, lost
energy after the numerical delay, and actual first retained nonzero
lag are different diagnostics. Advection runs at nominal physical
screen-crossing time: output time minus outgoing 400 km flight and the
numerical delay. The renderer is a slowly varying sampled medium; the
finite-FIR bound comparing output-time and individual lag coordinates is
not a time-dependent Maxwell bound.

Predeclare 129 x knots at 100 m, 128 RF bins for the retained bank, 256
at five mesh witnesses with reused coincident knots, 15 off-grid RF
witnesses per knot, and two independently placed off-knot positions per
interval at nine RF witnesses. The count is 21,391 RF evaluations per
table (fixed maximum 23,000). Use exact RF x derivatives and complex
Hermite interpolation; do not interpolate amplitude and wrapped phase.
Require complex errors ≤.0002 and discarded energy fraction ≤1e−6.
On occupied witnesses with raw voltage ≥.0005 also require sampled
error ≤10% of that voltage. Below .0005 report absolute resolution
and withhold any claimed exact depth; phase rotation is undefined at a
zero. Require series tail ≤1e−13, uniform far-rim bound ≤1e−8, and
finite-FIR retarded peak bound ≤.005 for each declared drift. The finite
reserve is 1 MiB for preparation and a single scalar FIR history in
the stream. No failed witness is overridden by decoder success.

First run the targeted native core test. The separate bounded numerical
probe runs six direct spatial witnesses, the three Gaussian flux
geometries, four tables (off/center/holdout/maximum drift), and one
240,000-sample channel-only render per qualified table. Cap the probe at
45 seconds elapsed and 30 million spatial cells. Report preparation,
render time, work counts and resident workspace separately, before any
full receiver interpretation. This is a cost/admission probe on this
implementation and hardware; no real-time receiver claim follows.

The first core test passed, but that first independent probe did not.
Five of the six quadrature witnesses exceeded the predeclared complex
gate (worst 6.19e−8). The carrier reference had an independently
reproduced mismatch: the two formulas used electron constants from
different adjustments, giving a relative radius mismatch of 2.04e−9
and an ambient carrier phase difference near 2.65e−8 rad with no lens.
Separately, both 32 km flux tests exceeded their edge gate
(outer-eighth fractions near 5.65e−7); the 64 km padded witness passed
with 8.10e−18. Flux round trip, center convergence, and all four causal
table gates passed in that first probe. Before retesting, align the
physical constants to [CODATA 2022](https://www.physics.nist.gov/cuu/pdf/wall_2022.pdf)
and promote the passing 64 km box to the base flux grid. Refine
spacing with 1024²/62.5 m and padding with 1024²/125 m, still capped at
45 seconds and now 64 MiB flux workspace. Keep every original spatial
witness, physical lens and acceptance tolerance. The failed first
probe was withheld from receiver qualification.

On the corrected compiled implementation, the focused native test passed
19 checks and the independent probe passed in 1.282 seconds. The latter
used 16,404,480 quadrature cells. Worst guarded complex quadrature
error was 1.093e−8; the refined flux integrals agreed to 2.084e−13
relative with outer-eighth fractions ≤8.11e−18 and grid/padding center
changes ≤1.26e−14. The sampled center lens had peak power 13.53
relative to the nominal path. The separate quadrature at the scan's
near-minimum, x=850 m and −118.7 Hz, found power .007838 (−21.06 dB).
These are local, coherent finite-model values; the broader impact
holdout has different extrema. No exact zero was demonstrated.

All four sampled tables passed with 21,391 RF evaluations each. The
largest sampled complex errors were 6.645e−5 (frequency) and 4.994e−5
(position); maximum discarded energy fraction was 6.19e−11 and the
maximum finite-FIR retarded error bound was .0007184 at 180 m/s.
Active table construction took .053–.056 seconds on the local CPU
lane, while each 240,000-sample active render took about .051 seconds.
The resident shared table and stream accounted for 335,840 bytes.
These are propagation costs, separate from complete receiver service.
The passing probe has appraisal-source digest
`81896f0290986198169a410301085b485a789e3737632bedfea690875b81cb2e`;
the first failed and passing raw receipts are temporary local evidence
for reproduction while the receiver investigation is open.

The depth claim for the actual sampled FIR requires a separate check at
the archived coordinate of 850 m, zero cross-track impact and −118.7 Hz.
The ordinary table tests nearby off-knot coordinates at 831.7 and
873.1 m. Before rerunning the numerical probe, fix the comparison at
850 m. Compare the production coefficients, interpolated from adjacent
spatial knots, with direct-position 128- and 256-bin FIRs of the same
support and numerical delay. Reuse the even frequency bins. Compare in
complex voltage with the independent wide quadrature using the same
native sign and delay, and retain its estimated finite-box and spacing
differences. The sum of the production-to-128, 128-to-256, 256-to-wide,
and independent quadrature differences must meet the existing .0002 and
resolved-relative gates. This adds 257 standalone RF responses and
reuses the archived quadrature case without adding spatial cells.

Run the actual stream on a tone with the physical 120 m/s clock and
observe the nearest output sample after warming the complete impulse
history. Compare the complex result with the interpolated FIR at that
sample's coordinate within 2e−12. Separately report a fixed 10 m scan of
the production FIR through 6.4 km and the 840/850/860 m power bracket.
This distinguishes the checked coordinate, its nearest clock sample and
its local samples. The scan reports a discrete minimum and a 65-point
plot trace at 100 m spacing. The comparison coordinates were fixed
before receiver outcomes.

The extended probe passed in 1.268 seconds. At 850 m the production
FIR gave .007835673 relative power (−21.06 dB) and independent
quadrature gave .007838329. The complex difference was 2.637e−5.
The guarded estimate was 2.656e−5, or 0.0300% of the checked voltage.
The 128-to-256-bin complex difference at the coincident position was
1.55e−9. Production powers at 840/850/860 m were
.010085/.007836/.010407, and 850 m was the minimum on the prescribed
scan. The nearest streamed tone sample at 850.0274 m measured .007836238
power and agreed with the FIR evaluated there to 2.61e−16 in complex
voltage. The temporary raw receipt `hf-wave-optics-fade-witness.json`
contains the plot trace and uses appraisal-source digest
`c5f8a48213aa6f6f1a2df497ca6e98c1429e900aff7540fbed2f35126409d901`.

The finite-beam flux check uses a square-integrable Gaussian illumination
to test energy redistribution, and the point-source fade uses the
unfolded spherical source geometry. In a separate same-geometry
spherical diagnostic, distributing the prescribed column over eight
depth slices changed the checked voltage from .08853 to .08260, about
0.60 dB deeper. The sensitivity to the assumed thickness is larger
than the numerical FIR error in this case. [Independent thickness
diagnostic](hf-phase-screens.md#independent-thickness-diagnostic-for-the-single-gaussian).

For arbitrary or separated phase screens, the same FFT primitive
supports a 2D march with a complex field per plane and bounded
transposed workspace; preserve the same density realizations across RF.
A 512² forward/inverse pair costs about 4.7 million complex radix-two
butterflies per interval before mask or gather costs. A dense product of
all time and RF witnesses from the analytic model would not be the
right implementation. For common rigid advection of fixed planes under
a plane-wave incident boundary, render a whole field per frequency
then follow its position track; independent screen velocities or
curved incident illumination break that shortcut. Before adopting an
arbitrary-screen backend, independently vary spacing, padding,
frequency sampling and displacement, compare one-screen and
interacting-screen observables, and charge the actual native work. No
distributed-scattering coverage is attributed to the Gaussian lens.

## Complete-receiver and endpoint interaction protocol

The [eight-row lens cohort](campaigns/hf-wave-lens-receiver.json) was fixed
before receiver results. It compares an ambient-only control with the lens at
0 dB-Hz, fixed −3 and +10 dB-Hz brackets, and requested 64 versus 128 seconds
at 0 versus −3 dB-Hz. The actual bit durations and measured incident and
received energies determine the energy comparison. The longer waveform
traverses different gains; equal incident energy is not equal received
information. A fresh impact, velocity, placement and payload and two negative
controls complete the design. No receiver policy or decision threshold is
changed. Each row has a 180-second limit; the full eight-row budget is
1,440 seconds. Censoring and invalid reporting are separate from communications
failures. Any diagnostic follow-up must preserve the selected actual IQ and
use the existing full-prefix physical reference without sending its truth to
the receiver.

The [five-row endpoint cohort](campaigns/hf-wave-chain-receiver.json) uses
one continuous canonical source through native TX clock/DAC/oscillator
processing, the physical lens, then native RX clock/oscillator/noise/frontend
processing. This ordering matters for a changing dispersive channel. Fixed
buffers bridge the stages; the adapter neither copies DSP implementations nor
resets the waveform or channel at slot boundaries. All three configurations
declare the same public relative clock bound of 4 ppm, endpoint bound of 2 ppm,
1 Hz oscillator allowance and 5 Hz physical allowance. The actual endpoint
errors are off or TX/RX audio +1/−1 ppm and RF +0.01/−0.01 ppm at 10 MHz.
The nominal 240 Hz conversion IF remains in both native endpoint stages.

The final interaction adds a constructed RF-plane tone of amplitude 0.3 at
carrier-relative +100 Hz, after propagation and before RX processing. It
continues through both final quiet observations and the capture end, including
the no-wanted-signal and wrong-key controls. The adapter distinguishes the
wanted waveform's complete public support from the continuing interference
field. Only this explicitly continuing tone permits the evaluator to stop
before that field's natural end. The unchanged receiver must still establish
completion from two scored quiet slots; evaluator EOF is never completion.

Thermal noise is added once at the native RX plane with the declared variance.
The intermediate signed bands are conservative engineering allowances, not
proofs of compact spectral support. Helper remains unqualified for these
1,920 Hz physical tables. The direct APP noise generator and native RX noise
generator are different streams, so the direct-lens and endpoint-off cohorts
are not same-IQ comparisons. Preparation and exclusive render times and all
live stage workspaces remain charged. These are complete-message observations,
not complete-service CPU-capacity measurements. Each endpoint row has a
180-second limit, with a 900-second aggregate budget.

### Direct-lens receive results

The first ambient invocation completed in 21.82 seconds and decoded
`A`. The initial runner marked its receipt invalid because the Off
operator correctly reported zero phase-series terms. The original
[runner record](local-evidence/hf-wave-lens-receiver/run.json) remains
intact. Offline strict validation accepted the unchanged native receipt
after correcting the reporting rule. The separate
[seven-row continuation](local-evidence/hf-wave-lens-receiver-remaining/report.md)
executed each remaining frozen input once. All eight raw native receipts
passed offline strict validation.

| Fixed lens case | Incident C/N0 (dB-Hz) | Actual bit duration (s) | Quiet active slots | Premature completions | Correct payload |
| --- | ---: | ---: | ---: | ---: | --- |
| Ambient Off | 0 | 69.6 | 0 | 0 | `A` |
| Primary | 0 | 69.6 | 0 | 0 | `A` |
| Primary | +10 | 69.6 | 0 | 0 | `A` |
| Primary | −3 | 69.6 | 11 | 2 | None |
| Primary | −3 | 139.2 | 0 | 0 | `A` |
| Independent impact, velocity and `B` payload | 0 | 69.6 | 1 | 0 | None |

The +10 dB-Hz noise and wrong-key controls made zero DATA decisions
and accepted no messages. No wanted row reported a wrong-bit DATA
decision or an accepted wrong payload. The long primary −3 dB-Hz row
received correctly at 100.035% of the short 0 dB-Hz row's incident
energy divided by noise. Its selected received energy divided by
noise was 96.887% of the short row's value. The exact stage energies
come from the declared incident and propagated DATA planes.

### Same-input holdout and bounded sensitivity comparison

All four [frozen follow-ups](campaigns/hf-wave-lens-followup.json) completed
and passed strict validation in 123.21 seconds aggregate. At 69.6 seconds
per bit, the holdout missed only active slot 2 at both 0 and +3 dB-Hz
and recovered the complete `B` at +10 dB-Hz. The 139.2-second,
−3 dB-Hz row also recovered the complete `B`. Compared with the
69.6-second 0 dB-Hz row, that row used 100.035% of incident energy
divided by noise and retained 98.580% of selected received energy
divided by noise. Changing the duration also changes the physical
trajectory sampled by each bit. These are observed bracket points and
one paired duration comparison for the fixed held field and waveform
roots.

The 0 dB-Hz replication exactly matches the archived full-capture IQ
and validity digest
`80ff806e6e6dab7d3ef1311c725c4261435108af56a2d801959d5e3d7f5b2b42`,
the qualified channel table and all fifteen native scores and decisions.
Presence repeated at −6.215 nats against 21.493, and only that active
slot was missed. The retained slot 2 aperture `[345524,480445)`
has digest
`c087a7e77465c07b674cd5cb97f8340645241dbc5af83912f554f29717fe03a8`.
The ordinary receiver finished before the private template comparison.
Its 2,158,736-byte selected-IQ buffer was reserved before reception.

All three private emissions preserved the earlier received samples.
They used the original physical-table hash
`93e1614361dd8f5ec053e6a5b7fd53cc2a46f5fe7e38998c565ef23ad7815542`.
The target-absent mean was exactly zero over the selected aperture,
with nonzero received signal before it. For the known conditional
means at that aperture, target-present versus target-absent gave
`D²/v = 89.803` and an observed log likelihood ratio of 76.622.
Correct versus alternate bit gave `D²/v = 178.607` and an observed log
likelihood ratio of 156.170. Thus the loss leaves substantial available
information in this constructed observation under known-channel scoring.
These values do not establish what a bounded blind receiver can recover.
The private work took 1.303 seconds.

The existing full-grid phase shadow is unavailable for this lens
geometry. Its 43 arrival cells and 13 public epochs require 559 timing
atoms, above the 512-atom cap. Even the fixed 97-model bank would use
about 53.3 billion FFT row-stages, above its 40 billion cap. The
post-receive comparison preserves every native public hypothesis and
executes no shadow phase search.

The failed holdout slot already ran the short varying, coherent-gain
and drift models. The existing compare-admitted policy extends those
comparisons to earlier successful slots and supplies no additional
model for this failed presence decision. The phase-filter, sticky
and bridge variants require the fixed 39-atom, 128-row lattice at
1,920 samples/s and ±1 Hz. This lens uses 559 timing atoms and ±5 Hz.
Reconsider a phase or gain model here only if it supplies distinct
likelihood support over the full configured hypotheses at bounded
cost and preserves discrimination on fresh complete-message controls.

The original binary SHA-256 is
`d951992789968ccf2a2334fa5706efb4d18b9e864c3fc3baa3ad9ca9fad60639`.
The follow-up binary is
`5f9251c15b8c712e84211052f3fea5ae435880d7b1a47f2eb1cf5002d19853f6`.
Both bind native source SHA-256
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
Retain the raw receipts and [follow-up report](local-evidence/hf-wave-lens-followup/report.md)
through review of the observed presence loss and paired duration effect.
Keep the summaries and the regression fixtures when redundant raw receipts
can be removed.

### Endpoint control runtime and continuation

The original endpoint command session ended with code 143 before it wrote
a campaign report. Its [frozen input](local-evidence/hf-wave-chain-receiver/frozen-input.json)
and raw receipts remain available. Offline strict validation passed all three
wanted rows. Endpoints Off, Clocks and Clocks with the RF tone each accepted
the correct complete payload and scored all 15 slots in 148.64, 148.07 and
146.46 seconds, respectively. The wrong-key row has empty output files and no
native result. The origin of the session interruption is unavailable.

The [noise-plus-tone receipt](local-evidence/hf-wave-chain-receiver/003-chain-noise-rf-tone.stdout.json)
records a separate native deadline outcome at 179.536 seconds. The receiver
accepted and hashed the complete 2,554,080-sample capture and committed six
of 15 public slots before cancellation. All six were quiet. The tone energy is
229,868.55 at the APP stage, with zero propagated wanted DATA energy. The native
RX signal energy is 229,808.31 after retiming. The elapsed wall time includes
10.648 seconds of source and channel rendering and 168.745 seconds of receiver
control and wait. The completed slot receipts show a drift and coherent-gain
fallback at every committed slot. Peak receiver reservation was 3.046 GB
against a 9.789 GB limit. The native receipt leaves completion unevaluated.

Use the observed six commits and total elapsed time only to size the next cap.
Linear extrapolation gives about 449 seconds for 15 commits. The commits
arrived in groups, so the extrapolation lacks a service-time confidence bound.
The fixed continuation uses separate invocations for the original
[noise-plus-tone](campaigns/hf-wave-chain-noise-continuation.json) and
[wrong-key-plus-tone](campaigns/hf-wave-chain-wrong-key-continuation.json)
inputs, each capped at 540 seconds and twelve million samples. Stop after
those two invocations regardless of outcomes. The additional wall budget
is at most 1,080 seconds. Keep the emission profile, input seeds, receiver
hypotheses and RF tone fixed. Compare the noise row against its recorded
full-capture and physical-table hashes. Preserve the original censor and
count a negative communications outcome only after its complete native
receiver receipt validates.

Both continuation receipts passed strict validation. Each scored all 15
public slots quiet, with zero false DATA, physical completions, or accepted
messages. The noise and wrong-key runs took 186.40 and 203.82 native wall
seconds, respectively. Receiver control and wait accounted for 175.42 and
191.84 seconds. The runner took 390.64 seconds across the two invocations.
These are two finite negative observations on the declared model and inputs.
[Noise continuation](local-evidence/hf-wave-chain-noise-continuation/report.md),
[wrong-key continuation](local-evidence/hf-wave-chain-wrong-key-continuation/report.md).

Both frozen episodes exactly match their original inputs. The noise retry
reproduced the full 2,554,080-sample IQ and validity digest
`528b117adca8f1551902cc30194aff17bd60e6ea50f3049c66dbac9435956fb6`.
The original noise and both continuations share physical table SHA-256
`e1cd0a8b7b6ce295d4b6798c2cea3bf8ea22cd506002cd1438e212a8f8b12399`.
The continuations used executable SHA-256
`5f9251c15b8c712e84211052f3fea5ae435880d7b1a47f2eb1cf5002d19853f6`
and the same canonical native source as the original cohort. The new
wrong-key capture provides the first complete observation of that input.

The matched noise trace demonstrates why the 449-second budget estimate does
not predict complete-receiver service. Six slots were committed by 165.77
seconds, and the remaining nine were committed by 186.31 seconds in groups.
Capture rendering, overlapping work and ordered commit all contribute to the
elapsed time on the local Ryzen 5 PRO 5650U with 12 logical CPUs in affinity.
Keep the initial censor and the external interruption in the original run
record. Retain their raw traces and both continuations for the current
endpoint compute-cost comparison. [Host receipt](local-evidence/hf-wave-host.json).

# Coupled selected multiple-hop HF returns

This continuation carries diffraction orders and both polarizations through
two ionospheric reflections and an intermediate ground reflection. A competing
one-hop F return passes through the same local E regions using their transmission
operators. Both returns share a source and receiving antenna. The physical
construction and bounded selection rules below precede numerical or receiver
outcomes; implementation availability is not numerical admission.

## Physical question and source reconciliation

The existing [_ref skywave review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md)
distinguishes ground-reflected, chordal and E/F routes, actual path geometry and
instrument error. Its [simulator review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md)
also separates statistical model consistency from measured channel validation.
This experiment does not replace those distinctions with a scalar delay table.

[Psiaki (2019)](https://doi.org/10.1029/2019RS006792) identifies phase, group
delay and endpoint directions needed to concatenate rays; the paper's own
calculation is single-hop. [Pederick and Cervera (2016)](https://doi.org/10.1002/2015RS005856)
distinguish ground-hop counts and chordal routes. Their Fresnel-based ground
power model is useful for material scales, but its polarization-averaged loss
does not supply coherent inter-hop phase. [McNamara, Barton and Bullett (2006)](https://doi.org/10.1029/2005RS003347)
describe the extra absorbing-region traversals and ground loss of 2F relative
to 1F; neither an extra-hop loss constant nor unmodified one-hop attenuation is
a universal replacement for those paths.

Rapid fading need not require equally rapid bulk density change.
[Balser and Smith (1962)](https://nvlpubs.nist.gov/nistpubs/jres/66D/jresv66Dn6p721_A1b.pdf)
report several-second multiple-hop fading and slower typical one-hop fading,
with unresolved-path and short-record limitations. [James et al. (2006)](https://doi.org/10.1029/2005RS003385)
identify beating between wavefronts and antenna polarization effects in
20–30 dB, 0.1–1 s satellite fades. Their 7.2 km/s receiver motion does not
calibrate a stationary ground receiver's fading rate. The height law below is
an explicitly constructed slow-moving reflector, not a copy of that rate.

[Aricoche, Hysell and Morton (2026)](https://doi.org/10.3389/fspas.2026.1848834)
report roughly 20 dB fading, faster changes than their inversion captured, and
common multiple-hop signals. Their selected observations do not resolve multiple
paths and often blend O/X modes; this is not evidence that hop interference
caused the reported fades. The described November 2023 interval was geomagnetically
quiet. Their warning that geometrical-optics phase models cannot predict
diffraction-driven amplitude scintillation argues against using a ray-only
phase curve as a measured amplitude channel.

[Gherm, Zernov and Strangeways (2005)](https://doi.org/10.1029/2004RS003093)
combine distinct E/F returns and separate propagation time from slow medium
time, but their weak/moderate scalar treatment excludes important caustic and
overlapping-Fresnel-volume cases. These sources motivate coupled path tests;
none validates this particular finite local-plane construction.

## Selected-return geometry and phase contract

Use the existing full vector Maxwell scattering matrices, including all
incident and outgoing retained orders and TE/TM components. Within each route,
all blocks share the same transverse lattice, port normalization and global
coordinate origin. For vacuum propagation distance d, use the order-specific
factor `D(d)=exp(+i kz d)` under the existing `exp(-i omega t)` convention.

The two-E contribution is
`D(h2) R2 D(h1+h2) G R1 D(h1) a`.
The one-F contribution includes the upward transmission through E1, reflection
from the stratified magnetic F layer, and downward transmission through E2.
It has its own central incidence angle chosen for the same source/receiver
range. It cannot reuse the two-E angular lattice. Apply each route's horizontal
source-to-receiver phase, `exp(i kx X)`, and the same physical dipole pattern;
project onto the common receiving antenna only at the final endpoint.

Ground uses complex polarization-dependent Fresnel coefficients with
`epsilon_g=epsilon_r+i sigma/(omega epsilon0)`, the passive square-root branch,
and the solver's tangential-electric-field gauge. The PEC limit is −1 for both
TE and TM in this gauge. Reference amplitudes use one fixed 1,000 km free-space
distance for both fields and both routes, including their physical path-length
and dipole factors. Do not normalize individual received branches to force
equal power or a null. Per-route port passivity is distinct from the coherent
point-field sum, whose magnitude need not be at most one.

These are **conditional selected returns through local planar regions**.
The periodic plane-wave blocks are not a finite transmitting beam or a complete
Earth–ionosphere boundary-value solution. Retained-order footprint separations
describe the selected corridor; they do not prove other real arrivals absent.
Other incidence sectors, additional hop orders, chordal paths, curvature and
finite-aperture effects remain outside this construction. The one-hop and
two-hop controls isolate contributions, rather than claiming that the omitted
physical layers or all other real paths disappeared.

## Physical parameters fixed before pilot outcomes

Carrier frequency is 10 MHz; the common transverse periods are 24 km and 17 km.
Each E region has a cos-squared background and a flat local corrugated slice
centered at −0.1 of its vertical support. The modes are (1,0), (0,1) and a
non-collinear mixed mode, with conjugate negative partners. Density amplitudes
are normalized to the density whose plasma frequency is 10 MHz.

| Parameter | Primary | Held field |
|---|---|---|
| E1/E2 lower faces | 111 / 103 km | 104 / 112 km |
| Two-E incidence | 50° | 48° |
| Receiver range | `2(h1+h2) tan(theta2)` | Same formula |
| F lower face / support | 284 / 24 km | 278 / 24 km |
| E1/E2 support | 960 / 1,080 m | 960 / 1,080 m |
| E1/E2 local slice | 80 / 90 m | 80 / 90 m |
| E1/E2 peak plasma frequency | 6.55 / 6.44 MHz | 6.38 / 6.51 MHz |
| E1/E2 collision rate | 1,300 / 1,850 s^-1 | 1,950 / 2,450 s^-1 |
| E1 magnetic field, microtesla | (18,17,−43) | (−11,25,−46) |
| E2 magnetic field, microtesla | (17,18,−44) | (−13,24,−45) |
| F peak plasma frequency / collision rate | 8.35 MHz / 90 s^-1 | 8.30 MHz / 120 s^-1 |
| F magnetic field | Average of E1/E2 fields | Same rule |
| Ground relative permittivity / conductivity | 10 / 0.01 S/m | 80 / 5 S/m |
| F height law | `300 sin(2 pi (T−137)/540)` m | `260 sin(2 pi (T−390)/470)` m |

| Density modes | Magnitudes | Phases, radians | Mixed order |
|---|---|---|---|
| Primary E1 | .00120, .00100, .00080 | .45, −.70, 1.10 | (1,1) |
| Primary E2 | .00100, .00125, .00065 | −1.05, .60, −.20 | (1,−1) |
| Held E1 | .00095, .00110, .00060 | .80, −.40, .33 | (1,−1) |
| Held E2 | .00110, .00090, .00075 | −.83, .95, −1.38 | (1,1) |

The transmitting dipole is fixed at `(0,sqrt(.86),sqrt(.14))`. The source,
noise, public calendar and model clock remain independent of the physical axis
selection described next. No receiver outcome is used to choose a field.

## Bounded physical receive-axis search

Use thirty-six real unit receiving axes in the laboratory yz plane, at
`index * 5 degrees`. The same axis projects both routes. Retain the original
y-axis finding. Evaluate the primary channel alone on 4,097 fixed times over
one height-motion period and a uniform 225 Hz band integral over −112.5 to
+112.5 Hz, initially with 65 trapezoidal frequency points. Check the selected
fade with 129 points and independent RF/displacement witnesses. This uniform
band metric is not a waveform-weighted DATA energy or a worst-frequency bound.
It is the nominal RRC DATA band. The canonical 0.02 s ramp and skirt allowance
give a wider declared emission guard of ±362.5 Hz; the physical operator is
qualified to ±384 Hz, also used by the native endpoint bridge. Neither guard
is an absolute-RF offset of 10 MHz in the source-band receipt fields.

Eligibility requires at least 18 dB carrier and 6 dB band-integrated reduction
of coherent power relative to the sum of the separate route powers, with
incoherent reference power at least 0.001 at the fade and mean incoherent band
power at least 0.003. These floors reject axes that simply suppress both paths.
Select the greatest eligible band reduction, with lowest index breaking a tie.
Report raw depth and numerical allowances; only supported conservative depth
can qualify the stated fade class. A failed numerical check cannot be repaired
by selecting a different, easier antenna axis.

Selection uses vector error bounds valid for every unit axis, not a y-axis
error reused after rotating the receiving antenna. Final admission also scans
the assembled FIR at independent occupied frequencies over the same 4,097
positions and checks the selected minima against the physical response. Both
physical and assembled minima constrain the relative-error budget.

Freeze the selected primary axis before any communication trial. The held axis
is `(primary_index+3) modulo 36`, fixed prospectively as a 15° offset; do not
retune it after observing the held field. This is an adversarial physical search,
not a sample from a natural antenna or ionospheric distribution. If no eligible
axis exists, record that failure rather than inserting independent path gains.

## Numerical admission and moving-reflector safeguard

First run cheap independent ground Fresnel/PEC/flux reductions, planar isotropic
image-path phase and delay, vacuum-E transmission recovery, zero-ground
two-hop removal, and direct nested mode/polarization summation. Composition
must retain noncommuting vector operations. Validate complete composed routes
under vertical and modal refinement, not just their individual layers.

Initial transverse half-orders are 2/3/4 for coarse/production/refined solves;
E mesh is 3/1.5 m and F mesh 30/15 m. Preparation is bounded at 48 MiB with the
existing per-RF dense-work ceiling. A carrier pilot has a 120 s process cap;
full primary or held preparation has a 360 s cap and at most 181 underlying
layer solves. Disabled route controls should not reconstruct unused branches.
Preserve absolute voltage 0.004, RF/FIR 0.0006, static passivity 2e-9 and
near-minimum relative-error 0.15 gates. Displacement tap and staged finite-support
errors each have a separate 0.0012 voltage gate. Frequency/displacement/tail/assembled
stream checks remain separate. One focused numerical recovery may use at most
twice the time and four times the preparation allowance, without changing
physical parameters or gates. Preserve the original rejection.

The approximately 1.1 rad/s moving-reflector phase cannot be frozen casually
across a 130/1920 s FIR memory: that can introduce about 0.075 rad of error.
Factor its carrier phase analytically, define reflection time relative to the
physical downstream flight, and test noncommutation with the downstream
dispersive E operator. A constant-velocity control must evaluate that operator
at the Doppler-shifted frequency. Require a weighted downstream FIR delay-moment
commutator bound, including pre-stage gain/derivative and finite-support error,
or a correctly staged dynamic implementation. Common modelling delay is not
physical propagation time. No fast-fading waveform is admitted until these
checks pass. These checks concern the prescribed translated-profile surrogate;
they do not prove general time-dependent Maxwell or plasma-fluid accuracy.

Fixed-angle translation also differs from a reflector joining fixed ground
endpoints. For the reference thin mirror, use the exact image range
`L(d)=hypot(X,2(hF+d))`, carrier phase `k0*(L(d)-L(0))`, its chromatic
remainder, and amplitude `Rref/L(d)`. Keeping only the linear height phase
would miss about 0.022 rad at the primary displacement extreme. Evaluate the
height law at the retarded physical reflection time, including its varying
downstream flight. Independently recompute incidence, launch/projection,
transmission/reflection and propagation at displacements ±A and ±A/2 and RF
offsets 0 and ±112.5 Hz. Charge the resulting angular-freezing error to both
the 0.004 absolute and 0.15 relative near-minimum budgets. If those checks fail,
the frozen angular approximation is not admitted. The image formula is exact
for the reference thin mirror; finite dispersive plasma and O/X modes can move
the stationary ray and virtual reflection height, so this is not an exact
finite-dipole Green function.

The proposed native stream uses 1,920 samples/s, a checked RF offset band of
±384 Hz, a common added 32-sample model delay, and 129 retained taps starting at
physical lag 3, with the complete 131-sample tail and a 2,400 s horizon. These
are admission assumptions to verify, not permission to crop an unresolved tail.

## Complete-receiver requirements and stopping rule

After physical selection and numerical admission, freeze eight complete trials:
primary two-hop only, one-hop only and both routes at +30 dB-Hz with fresh `E`,
and held both routes at +30 with `F`, all requiring correct complete delivery.
Characterize primary both routes at +20 with a 64 s bit request and +17 with a
128 s request; measure actual incident and per-decision received energy rather
than assuming equality. The native harness keeps signal amplitude fixed and
changes C/N0 through noise variance: duration pairs must therefore compare
incident and propagated **E/N0**, with raw signal energy reported separately.
This is an energy/noise comparison, not a claim of equal raw transmitted energy.
Both complete noise and wrong-key controls at +30 must
produce no DATA decisions or accepted messages. The
[frozen campaign](campaigns/hf-vector-two-hop-receiver.json) uses source/channel
seed 73613 and noise seed 93613 and retains full default native worker allocation.
These points are named
engineering requirements, not promises about a natural population.

Each receiver trial is capped at 900 s and 12 million samples; the initial
eight-trial aggregate cap is 6,000 s. Stop on invalid evidence, numerical
rejection or execution limits; record valid delivery failures without censoring
the other independent rows. These three new campaigns explicitly enable the
runner's `stop_on_execution_failure` option; existing campaigns retain their
previous behavior. The option and refusal/timeout/valid-miss distinctions pass
the 138 runner tests before receiver execution. If all four direct delivery anchors pass, permit
one prospectively required native clock/continuing-tone combination at +30;
if it misses, permit its matched endpoint-Off discriminator. No additional
parameter sweep is implied. Trials remain CPU reference observations unless a
demonstrated suitable native accelerated workload justifies a RunPod deployment.

## Measured-channel lead and evidence lifecycle

The new bounded audit of the Aricoche et al. primary PDF verifies 10 microsecond
PRN chips, 0.1 s repetition, GPS-disciplined timing tested to ±50 ns, no AGC,
and 1 MS/s sampling decimated to 100 kS/s. Processing uses ten-second coherent
intervals and one-minute products. The displayed carrier phase is integrated
Doppler with an arbitrary offset, not an absolute phase calibration. The data
statement points to repository identifiers in supplementary material, but none
was located in the inspected article, supplement routes or indexed records.
No public complex-IQ member, exact replica, per-capture gain/noise calibration
or reusable data license was verified. This is a concrete one-way measurement
lead, not an imported disturbed-event replay fixture or evidence that no data
exist.

A second concrete lead is [Dear et al. (2024)](https://doi.org/10.1029/2023RS007877)
and [Zenodo record 7552900](https://zenodo.org/records/7552900). Its inventory
includes the 15.5 MB raw received file `Rtl_220Ksps_21122021_1329.dat`, CIR and
correlation products. The paper describes complex IQ at 220 ksample/s and a
255-chip MLS BPSK sounder over an 87 km NVIS path. This is an actual raw-IQ
lead, but the week was explicitly quiet, absolute transmitter/receiver timing
was unsynchronized, and the inspected record has no declared reuse license.
Per-capture gain/AGC, phase/frequency reference, filter response and absolute
noise calibration were not verified. The companion record 7619093 could not
be inspected. No files were imported: this lead might support relative
delay/Doppler checks, but it cannot fill the calibrated disturbed-event gap.

A further primary-source lead is the Antarctica–Spain oblique sounder:
[system methods](https://doi.org/10.1029/2007RS003812) and
[subsequent campaigns](https://doi.org/10.1002/2016RS006232) describe a
12,760 km link, wideband PRN sounding, GPS coarse synchronization, adaptive
receiver gain and noise estimates. The papers direct readers to an IGME
archive requiring registration. No public complex-capture manifest, per-capture
gain/noise calibration or reuse license was verified in this audit; processed
scattering products are not interchangeable with a calibrated time-varying
complex channel. It is a concrete multihop measurement lead requiring access
and metadata verification, not a fixture admitted by this suite. No external
contact, account creation or capture import was performed.

Following the user's public-sources-only direction, a targeted NOAA/VIPIR
audit checks the [Hurricane Sandy observation](https://www.ngdc.noaa.gov/stp/space-weather/online-publications/stp_presentations/2013/sww_2013/mabie-et-al_sww_2013_poster.pdf)
at Wallops, 29 October 2012, 20:50–21:00 UT, against the
[public WP937 archive](https://data.ngdc.noaa.gov/instruments/remote-sensing/active/profilers-sounders/ionosonde/data/WP937/).
The poster describes fixed 2.340 MHz pulses interleaved with a sweep and
disturbed E-region echoes, but no exact raw event member was verified.
[RIQ documentation](https://data.ngdc.noaa.gov/instruments/remote-sensing/active/profilers-sounders/ionosonde/documentation/VIPIR/readriq-docs.pdf)
provides complex pulse samples and instrumental timing; it does not by itself
establish gain/phase/AGC calibration to a receiver-input voltage. The
[pynasonde example](https://pynasonde.readthedocs.io/en/latest/examples/vipir/proc_riq/)
names `PL407_2024058061501.RIQ`, but that raw member is absent from the inspected
[public tree](https://github.com/shibaji7/pynasonde/tree/main/examples/data)
and its raw URL returns 404. It is not linked to a disturbed event either.
Such vertical pulse-radar data could constrain echo/spread observables if
obtained; discrete sweeps and two-way radar returns cannot be relabeled as a
simultaneous calibrated oblique communications channel. No raw file was imported.

A further review reconciles the already retained
[ION-FAST archive audit](local-evidence/hf-3d-preflight/ionfast-archive-audit.json)
with [Moiseev et al. (2024)](https://doi.org/10.3390/atmos15020188).
The [public deposit](https://zenodo.org/records/7861107) is CC-BY-4.0 and contains
three complex64 capture files: availability and reuse permission are established.
It is a sequential 8–30 MHz chirp at 100 kHz/s, taking about 220 s; a 225 Hz
interval is traversed in 2.25 ms. It does not observe one carrier continuously
for a long modem message. The metadata says 15 April 2023 while filenames say
14 April. The notebook applies empirical delay shifts and a 1.045 frequency
multiplier without supplying an absolute sample epoch, calibrated channel
replica or per-capture gain/noise reference. The paper also identifies AGC-delay
artifacts in the commercial comparison, so that comparison is not calibrated
truth. Its rapid Es changes concern separate August heating observations,
not automatically these April files. This reuses the existing bounded archive
inspection; no new full-archive download or raw replay is implied. Relative
ionogram reproduction would not close the disturbed-event temporal-channel gap.

Keep prospective inputs, rejected numerical approximations, admitted physical
tables/probes and complete receipts in `local-evidence/hf-multihop-*` while
they support current claims, unresolved failures or required regressions.
Distill superseded success logs and reuse the current build. The initial
[43-receipt continuity check](local-evidence/hf-multihop-preflight/baseline-qualification.json)
validates every existing receipt and preserves the known failed qualification.
New numerical admission and receiver outcomes will be recorded here before
any corresponding coverage claim.

## Rejected carrier and focused numerical recovery

The initial primary carrier is rejected, before axis selection or receiver
execution. Its [receipt](local-evidence/hf-multihop-numerics/carrier-primary-initial.stdout.json)
reports mesh error 0.05331, order-refinement error 0.03343 and outer-order
flux 0.001023, exceeding the unchanged 0.004 / 0.004 / 0.00005 gates.
Preparation took 8.8 s on the local Ryzen 5 PRO 5650U. Isolating the routes
with the same binary identifies two different numerical needs:

| Carrier contribution | Mesh voltage error | Order voltage error | Outer-order flux |
|---|---:|---:|---:|
| Two-E ground bounce | 0.001164 | 0.033423 | 0.001023 |
| One-F via E transmissions | 0.052147 | 0.00000448 | 0.00000000967 |

The focused recovery keeps the medium and all gates fixed. It uses two-E
coarse/production/refined half-orders 4/5/6, retains one-F orders 2/3/4, and
refines only the F vertical mesh to 5/2.5 m. The shared solver receives explicit
preallocation/work guards: at most 169 modes and 12 billion dense terms for
the widened two-E solve, and 81 modes / 9,600 purely stratified layers for F.
The recovery preparation quote is 96 MiB, below the prospectively allowed
192 MiB maximum; carrier and full-table process limits are 240 and 720 s.
Channel/source-workspace limits are 160 MiB direct / 192 MiB with native endpoints,
including preparation and the other channel allocations. They are **not**
complete-receiver memory limits: native receiver owners, DSP scratch, input and
retained results remain charged to the separate automatic application/receiver
budget and are reported in each complete receipt. These revised numerical
budgets are declared before the recovery outcome. They do not authorize
automatic order-7/8 expansion or weaken a sensitivity or physics gate.

Preparation dominates these research trials. The current complete-receiver
CLI constructs a fresh qualified table per invocation; its existing service
replay reuses already noisy samples and is not an admitted route for this
channel. The table can be shared immutably within a native process, but no
existing full-receiver batch/cache interface supplies that reuse. Do not
substitute recorded noisy voltage for a clean generated channel to save time.
These bounded trials retain independent native preparation; a future native
batch interface would need exact-source numerical provenance and per-trial
state/ownership checks before reducing that cost.

The [recovery carrier](local-evidence/hf-multihop-numerics/carrier-primary-recovery.stdout.json)
passes in 41.94 s: mesh error 0.001339, order error 0.002342 and outer-order
flux 0.000005545. Its charged preparation peak is 85,699,072 bytes and maximum
per-RF dense work is 8.217 billion terms. This passes the carrier screen only;
the full frequency, moving-angle, time-variation, null and FIR checks remain
separate gates. An early one-F angular screen uses nominal carrier plus four
fixed height offsets before paying for a full broadband table.

That [angular screen](local-evidence/hf-multihop-numerics/angular-primary.stdout.json)
rejects the frozen-angle/image-corrected approximation: the y-axis voltage
error reaches 1.309 and every candidate antenna has maximum error at least
0.908. The field itself is of comparable magnitude. Each full one-F point
takes approximately 1.8 s. Exact thin-mirror curvature is insufficient because
the finite magnetic plasma layer contributes substantial angle-dependent
phase and amplitude. No waveform or receiver result is admitted from this
approximation.

The next bounded repair pilot interpolates the full complex pre/post-F
operators, factoring only the known image-range phase. Use five height nodes
at normalized displacements −1, −0.5, 0, 0.5, 1 and four independent midpoint
witnesses at three RF offsets: 0 and ±112.5 Hz. Require both complex YZ error
and a cancellation-free weighted pre/post error to be at most 0.0006.
If five nodes fail, reuse their witnesses as nine fit nodes and test eight
fresh odd-eighth displacements, retaining both results. Independently verify
vertical and order refinement at ±A at the carrier before accepting those
interpolation results. The prospective maximum is 55 RF points / 165 layer
solves, 180 s and 96 MiB. This pilot does not admit a waveform; any resulting
full model must also bound the derivatives of both varying stages and pass
the original frequency, FIR, tail and null gates.

Both fields pass the nine-node pilot; five nodes are rejected in each:

| Field | Five-node field / stage error | Nine-node field / stage error | Elapsed |
|---|---:|---:|---:|
| [Primary](local-evidence/hf-multihop-numerics/angular-interpolation-primary.stdout.json) | 0.02785 / 0.04989 | 0.00003479 / 0.00006393 | 108.44 s |
| [Held](local-evidence/hf-multihop-numerics/angular-interpolation-holdout.stdout.json) | 0.03346 / 0.04652 | 0.00003947 / 0.00005790 | 109.20 s |

Each uses all 55 RF points / 165 layer solves, with the fresh nine-node
witnesses kept distinct from the promoted five-node witnesses. The moving
endpoint mesh/order checks pass in both fields. Freeze nine height nodes for
the full operator in both fields before any receiver result.

The prospective full moving-angle construction replaces the rejected
frozen-angle waveform approximation. Its maximum is 29 stationary two-E RF
points plus 148 one-F RF points: **502 layer solves**, under the unchanged
720 s / 96 MiB recovery envelope. The F points comprise 81 joint fit points,
13 independent nominal-height frequency points, 24 height-midpoint points,
8 endpoint-band points, 2 additional cross-height points, 10 vertical/order
refinements and 10 freshly recomputed moved-angle responses at assembled-FIR
minima. Disabled branches omit their work. This supersedes the original
181-solve frozen-angle plan; its rejected evidence remains retained.

Temporal admission must use uniform bounds on both complex stage polynomials
and their height derivatives, including chromatic delay, range/pattern changes,
restored carrier phase and the implicit retarded clock. Bernstein control
bounds over fixed subintervals supply these bounds; derivatives estimated only
at knots do not. The product of two degree-eight stages can be degree sixteen.
Finite-grid interpolation, convolution aliasing and retained-support errors
remain separate terms. Passing the earlier interpolation pilots does not
replace these waveform checks.

In the staged temporal reference, each post-F tap and its pre-F input use the
same reflection-interaction height for that tap. The collapsed FIR uses one
common retarded height. The bound therefore charges the variation of **both**
stage coefficients across those interaction times. This is not a generic
time-varying cascade that indexes the post-F coefficient at receiver output
time. The implicit reflection clock uses the bound `V/(1−V/c)`; the added
32-sample modelling delay remains outside the physical flight clock.

The coherent-fade and relative-null allowances sum the separate two-E and
one-F numerical budgets. Within each branch, charge the larger of product
and cancellation-free stage errors for each mesh, order and RF category.
The moving F-stage comparisons align spatial orders and TE/TM components,
zero-extending absent orders during refinement. This prevents compensating
pre/post errors from passing a static product check and reappearing during
retarded evolution. These bookkeeping refinements reuse existing solves.

Only the fitted-polynomial amplitude/derivative bounds are uniform in height.
The 0.15 relative-null allowance is a combined **finite-witness** numerical
budget, not an all-channel rigorous bound. The staged-support metric bounds
cyclic alias terms folding into retained lags; discarded-energy and direct
RF/FIR checks separately assess omitted support. It must not be described as
a uniform bound on every physical tail.

After corrected axis selection, check the isolated moving F waveform before
the complete mixed waveform. Its temporal and interpolation machinery is the
same, so a refusal there can be diagnosed without repeating the expensive
stationary two-E preparation. This changes only the order of numerical
admission; all four predeclared strong receiver requirements remain required.

The [corrected channel-only search](local-evidence/hf-multihop-numerics/moving-axis-search-primary-corrected.stdout.json)
selects **axis 11 (55°)**. The held field therefore uses **axis 14 (70°)**;
both are now frozen in the native scenarios and the independent receipt reader.
At the selected height displacement 280.880 m, carrier coherent/incoherent
power is 0.003853 / 0.420906 and uniform-band power is 0.009093 / 0.420906.
Raw cancellation depths are 20.38 and 16.65 dB. Charging the summed branch
voltage allowance 0.004984 gives conservative depths 19.65 and 16.15 dB.
Mean incoherent band power is 0.464860, so the selection is not merely an
antenna suppressing both returns. Preparation took 485.08 s, with 167 RF
points / 472 layer solves and the unchanged 85,699,072-byte peak quote.
This is a physical selection receipt, **not full waveform admission**. The
earlier search made before corrected branch-error accounting is retained only
as a superseded numerical diagnostic, not as the selection authority.

The [isolated moving-F full waveform](local-evidence/hf-multihop-numerics/moving-one-hop-primary-full.stdout.json)
passes in 289.85 s with 148 RF points / 444 layer solves. The retarded-stage
voltage bound is 0.001221, displacement-tap error 0.000138, direct RF/FIR error
0.0000123 and combined relative witness allowance 0.003272. The actual stream
matches its direct coefficient calculation and is invariant to chunking.
This establishes the moving stage's admission at the frozen primary axis;
the mixed-path cancellation and held field still require their own full checks.

The subsequent [primary mixed waveform](local-evidence/hf-multihop-numerics/moving-both-primary-full.stdout.json)
also passes: 503.89 s, 177 RF points / 502 layer solves, relative minimum-voltage
allowance 0.08543 against the unchanged 0.15 gate, and RF/FIR error 0.0000625.
Fresh physical recomputation at the actual assembled minima gives minimum
power 0.003485 versus 0.003487 assembled. The specified coherent fade remains
supported after the full allowance. Actual stream/direct and chunking errors
are zero. This admits the selected primary waveform without selecting an
easier axis or weakening the near-null accuracy requirement.

Before those combined waveform trials, the contained build and all **41
appraisal check groups passed** in 192.70 s
([log](local-evidence/hf-multihop-preflight/build-and-checks.log)). This includes
all six actual native plans with frozen axes 11/14, strict independent receipt
validation, and early resource-limit checks for the two negative controls.
Superseded axis-0/3 plan copies were removed after that current integration
passed. These are software/integration checks, not eight executed reception
trials or a clearance of the historical failed communications requirements.

The updated receipt reader also passes a second
[43-case continuity audit](local-evidence/hf-multihop-preflight/reader-continuity-qualification.json):
all original receipt validations pass and every historical gate is unchanged
from the baseline. The overall result remains failed on the existing required
communications outcomes; adding a new physical model does not clear those
failures. At this audit the main receiver executable was still the original
binary, while the separate numerical probe had been rebuilt.

### Sampled fade timing

Depth alone cannot establish rapid fading. The final diagnostic evaluates the
admitted **actual FIR** at 4,096 unique, uniformly spaced output times over one
motion period, using the stream's retarded clock. It computes the carrier and
225 Hz uniform-band responses directly from the taps. The fixed two-E response
separates the moving-F contribution for the incoherent reference. A shared
65/129-point quadrature refinement uses the larger coherent and smaller
incoherent powers and the existing full voltage allowance.

The report counts samples below the existing 18 dB carrier and 6 dB band
cancellation thresholds, and their intersection, with the same brightness
floors. Circular runs merge across the period boundary; a run of n samples has
reported sample-to-sample span `(n-1)*period/4096`. Resolution is 0.13184 s
primary and 0.11475 s held. These are **sampled spans**, not a guarantee of
uninterrupted fading between samples; the quadrature comparison is likewise
a refinement check, not a rigorous integration-error bound. Isolated routes
and numerically rejected tables emit unevaluated zero summaries. No new
Maxwell solves, receiver gates or natural-event frequency claims are added.
The source-reviewed addition passes all nine affected native/reader/integration
check groups in 59.46 s
([log](local-evidence/hf-multihop-preflight/temporal-build-and-checks.log)).

The [held mixed waveform](local-evidence/hf-multihop-numerics/moving-both-holdout-full.stdout.json)
passes in 497.44 s with the same 177 RF points / 502 layer solves. Its relative
minimum allowance is 0.006818, FIR voltage error 0.0000214 and retarded-stage
bound 0.0007884. It has no samples meeting the carrier/band cancellation class;
the independently fixed antenna/field is not retuned to force a deep fade.
Its 4,096-sample temporal diagnostic is evaluated, with zero threshold runs.
Both primary and held full stream/direct and chunking comparisons pass.

## Receiver evidence validation recovery

The [first campaign](local-evidence/hf-vector-two-hop-receiver/run.json) stops
after its first execution in 224.74 s. The native two-E-only receipt reports
a qualified table and complete correct `E`, but the reader rejects the table's
descriptive digest-scope string. The existing native JSON writer escapes UTF-8
bytes individually, turning multiplication signs in the new description into
different decoded text. No numerical, input, energy or receiver predicate is
the cause. The static-only null scan correctly reports one position; it was
not the mismatch.

A source-bound offline audit verifies the frozen campaign, executable hash,
exact episode and command, sample/time limits and unchanged original receipt
SHA-256 `671285da957a13f19649b00d6848f32be065fb27dc84611d1fa52fc72e9adf8a`.
Replacing only that description in an in-memory copy with the new ASCII
literal makes the complete receipt validator pass. Original files remain
unchanged and the production reader still rejects the malformed description.
Producer and reader now both use `9 x 9 x 98 x 3`; all 138 runner tests pass,
including the byte-escaped-string regression. The
[recovery campaign](local-evidence/hf-vector-two-hop-receiver-recovery/run.json)
repeats the unchanged eight operating points with the rebuilt executable.
Its first observation is a validation repeat, not an independent channel or
noise realization. No communications failure or receiver repair is inferred
from the original metadata stop.

The repeated two-E-only execution passes strict validation and delivers `E`
again. Its table hash and complete 2,554,140-sample receiver-input hash match
the original execution exactly; the input SHA-256 is
`86d4be3ac69b0851c7c55335f05e27aea009ad743f29a92ab107d4ae7cf79fef`.
This directly checks that the reporting correction did not change the channel
or the received samples.

Both isolated-path required deliveries pass. The moving-F observation uses
24 receiver workers on 12 hardware threads and reports 2,914,373,753 bytes
peak reserved receiver memory against a 7,645,679,079-byte receiver limit.
Its channel preparation takes 290.85 s; complete execution is 310.48 s.
Preparation, render/control overlap and complete receiver work remain separate
accounting fields, not an isolated DSP or real-time capacity measurement.

The [post-recovery continuity audit](local-evidence/hf-multihop-preflight/post-recovery-reader-continuity.json)
again validates all 43 older receipts and preserves every historical numerical,
delivery, integrity, false-DATA and bounded-execution gate. The rebuilt target
is not the older spec's executable, so current-binary applicability is explicitly
missing at this intermediate audit; historical failures are not cleared.

## Prospectively bounded sensitivity follow-up

All four +30 direct delivery anchors pass, including the independently fixed
held `F`. Before either original +20/+17 outcome is available, a further
decision rule is frozen. The primary +30 capture has private active-window
`E/nu` of 26,552–39,112 and affine log evidence of 24,667–37,575, against a
21.493 threshold. Scaling the same clean signal to C/N0 0 predicts private
`E/nu` of 26.55–39.11. That motivates a near-boundary experiment; it does not
predict achievable unknown-channel delivery.

If both original wanted points pass, run the separate
[four-row sensitivity campaign](campaigns/hf-vector-two-hop-sensitivity.json):
wanted 0/64, wanted −3/128, noise 0/64 and wrong-key −3/128, where each pair
is C/N0 in dB-Hz / requested bit seconds. Keep field, axis, payload and seeds
fixed. Cap it at four trials, 900 s each and 3,600 s total, with the same
execution-refusal stop rule. Require zero DATA and accepted messages in each
negative cell. This covers two named negative cells; **it is not the full
two-operating-point by two-control-type matrix**. The wanted points characterize
the boundary and do not promise an operational sensitivity floor.

If short +20 passes but long +17 fails, omit that lower campaign and permit
only long +20 to test whether 3 dB additional incident E/N0 repairs the loss.
If short +20 fails, omit the lower campaign and permit short +25 between the
existing +20 failure and +30 success; only a successful +25 permits long +22.
Then stop this bisection. These conditional observations are distinct from
the previously declared clock/interference experiment. Expand native and reader
operating-point admission only after the original campaign finishes, preserving
its frozen executable and all historical source identities.

The observed +20 short and +17 long points both deliver `E` with thirteen
correct DATA decisions, two ending quiet decisions and no recorded integrity
failure. The long case has 1.995967 times the raw incident signal energy and
1.969054 times the raw propagated energy. With the actual noise variance
included, incident E/N0 differs by only +0.00153 dB and propagated E/N0 by
−0.05742 dB. For these direct proper-white-noise captures,
`E/N0 = sum(abs(clean_samples)^2) / complex_noise_variance`.
Individual active-window energy/noise ratios differ by factors 0.6901–1.4557
because message timing samples a different portion of the evolving channel.
Thus whole-capture equality must not be substituted for per-bit information.

Capture duration rises from 1,330.281 to 2,239.881 s; actual bit duration is
69.6 versus 139.2 s. Receiver control/wait time rises from 15.158 to 30.267 s,
while rendering rises from 4.295 to 8.097 s. These fields can overlap DSP and
are not isolated service measurements. The pair has no observed duration
regression at these bright operating points, but does not locate the sensitivity
boundary. Its results select the predeclared 0/−3 follow-up branch; the +25/+22
and long +20 alternatives are not executed.

## Eight-trial findings and finite qualification

The [validated recovery campaign](local-evidence/hf-vector-two-hop-receiver-recovery/run.json)
completes all eight trials in 3,640.67 s with no execution failures. All six
wanted observations deliver exactly. The noise control has fifteen quiet
decisions and no DATA or accepted message. **The wrong-key control fails its
zero-false-DATA requirement:** thirteen DATA decisions are followed by two
quiet decisions and one physical completion. The codec rejects that message;
no false message is accepted. The runner's `valid_communication_failures=0`
counts wanted delivery failures and must not conceal this separate failed gate.

The [source-bound wrong-key ledger](local-evidence/hf-multihop-wrong-key-ledger.json)
records all thirteen false decisions and both ending slots. Operational log
evidence is 33.1586–1,902.9461 against threshold 21.4934. Three false decisions
use affine evidence and ten use bounded varying-channel evidence; removing
only the latter cannot repair this capture. The native rule compares keyed
alternatives with a noise null, rather than a calibrated structured-other-signal
null. This extends the already documented
[rejection weakness](hf-receiver-repair.md), not proof of a defect introduced
by the new multiple-hop simulator. The wanted and wrong-key captures share
configuration, but key substitution changes their waveform and whole-input
hash; they are not identical-IQ comparisons. Private clean energy in the ledger
is diagnostic and is not supplied to the receiver. In this control it measures
the unrecognized transmission, not a wanted-signal reference.

This distinction also follows from the statistical literature.
[Kelly (1986)](https://doi.org/10.1109/TAES.1986.310745) derives adaptive
detection using independent, signal-free secondary observations with matching
noise covariance; that assumption is not supplied by a strong wrong-key
transmission. [Besson and Orlando (2007)](https://doi.org/10.1109/LSP.2007.898355)
analyze interference/covariance mismatch between test and training data and
the resulting robustness/rejection tradeoffs. Structured covariance estimation
also has finite-sample sensitivity and threshold-calibration costs, conditional
on the chosen model ([Abramovich et al., 2010](https://doi.org/10.1109/TAES.2010.5417169)).
These radar detection results motivate constraints, not a ready HF repair.

The reference collection's [HF noise review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/50-noise-and-interference-mf-hf.md)
and [scenario review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md)
likewise separate median/decile noise levels and public spectral aggregates
from target-site subsecond complex covariance. Any future structured-null
candidate must freeze its family, complexity and calibration independently of
these failures, retain a no-DATA outcome, and measure rejection alongside weak
wanted-message sensitivity on fresh controls. Estimating covariance from
signal-contaminated or temporally mismatched data can sacrifice the very weak
signal being sought. No covariance, identity-classification or receiver repair
is implemented or promoted by this experiment.

The [51-cell audit](local-evidence/hf-multihop-preflight/qualification-51.json)
validates every receipt, preserves all 43 historical gate dictionaries, and
binds all eight new cells to the actual current executable at this stage.
Seven new cells satisfy their declared requirements; the wrong-key false-DATA
gate fails. Every original integrity/false-DATA/completion failure remains
recorded, with historical source applicability distinguished from the new
executable. The aggregate qualification remains failed.

## Native endpoint resource-accounting recovery

The [first endpoint trial](local-evidence/hf-vector-two-hop-chain/run.json)
stops after 500.07 s before producing receiver slots: the native RX constructor
rejects the explicit source working-storage charge. The bridge passed the
physical channel's preparation peak, including its 96 MiB temporary solve
reserve, as if it remained live. The unchanged native source/RX guard permits
65 MiB for the simulated endpoint's source/channel working storage; this is
separate from the complete Robust receiver's multi-gigabyte budget. The
appraisal's larger outer channel budget cannot override the endpoint guard.
No delivery or completion failure is inferred from this setup error.

The recovery retains the preparation-peak admission and outer 192 MiB limit,
and separately accounts for the retained table, stream, bridges and persistent
configuration after temporary solve storage is released. The native RX guard
remains unchanged; merely increasing the outer limit would not repair the
ownership error. The repaired implementation passes
all 41 appraisal test groups in 183.46 s. The native budget checks distinguish
the exact pre-table refusal from merely observing zero source reads; the
two lower wanted plans and all four receive preflights are also exercised.
[Full software check log](local-evidence/hf-multihop-preflight/workspace-build-and-checks.log).

The [recovered endpoint trial](local-evidence/hf-vector-two-hop-chain-recovery/run.json)
then completes execution with a strict valid receipt in 615.20 s, resolving the
source-admission error. The charged preparation peak remains 100,987,272 bytes;
aggregate resident channel storage is 394,112 bytes, including the physical
stage's 358,120-byte retained quote. These are bounded ownership charges, not
measured process RSS. The unchanged canonical native source digest is
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.

**The declared complete-delivery requirement fails.** All thirteen active bits
are correct affine DATA, but both ending slots are `ambiguous`, so no physical
completion or accepted message occurs. Their DATA evidence is 14.2334/14.5722,
below 21.4934, while quiet energy is 22,292.21/21,965.09, above 16,237.85. There
are no active erasures, wrong bits or premature completions. This is a valid
communications failure, separate from the original setup error. The matched
endpoint-Off discriminator is therefore admitted under the frozen protocol.

Native preparation/render/control-wait time is 495.578/13.927/105.448 s.
The receiver preserves W=24 for H=12, peaks at 24 workers and nine owners,
and charges 3,105,457,269 bytes within its 8,019,424,743-byte receiver limit.
No private per-slot wanted-energy references are available in this endpoint
capture; direct-path energy diagnostics are not substituted for them.

The [matched endpoint-Off trial](local-evidence/hf-vector-two-hop-chain-off/run.json)
delivers exact `E` in 614.35 s. Its thirteen DATA bits are correct and both
ending slots are quiet: energy 15,833.92/15,785.14 is below the same 16,237.85
threshold. DATA evidence in those slots is −9.6545/−9.7052. Both endpoint runs
retain the same public search bounds, noise variance, source/axis configuration,
capture horizon and spectral allowances. Actual audio errors ±1 ppm and RF
errors ±0.01 ppm become zero, and the continuing amplitude-0.3, +100 Hz tone
is removed. The received IQ therefore differs; this does not isolate clocks
from the tone or prove one alone is causal.

The full physical plan is identical in both endpoint cases and the original
direct primary, with coefficient-table hash
`ff2b3f4e40bf6fde12fa89fc733240f08038de0020f497f09adba1652f0008aa`.
The Off case retains the same preparation/resident charges and W=24/H=12;
native preparation/render/control-wait time is 496.942/7.389/109.756 s. Peak
receiver ownership is thirteen owners and 3,104,752,757 reserved bytes. These
are complete observation costs under the stated bounds, not a qualified
real-time ceiling or isolated scorer-service benchmark.

## Prospective near-boundary pair after the lower wanted misses

Both lower wanted cases, 0/64 and −3/128, complete execution with all thirteen
active slots classified quiet. This leaves a 20 dB, or 100× E/N0, gap to the
successful +20/64 and +17/128 pair. Before any further reception, the
[additional fixed pair](campaigns/hf-vector-two-hop-boundary.json) freezes
**+6/64 and +3/128**, with unchanged primary field, axis, `E` payload and seeds.
The choice raises the lower pair's energy/noise ratio by 6 dB; it is made after
the lower wanted outcomes, while their two negative controls are still pending.
It is an adaptive follow-up, not an independent holdout or a retrospectively
reclassified success requirement.

This explicitly extends the earlier investigator-chosen no-further-bisection
efficiency rule. The original campaigns and their frozen criteria remain intact.
Both new points are declared together, capped at 900 s each / 1,800 s aggregate,
with no retuning between them and no automatic subsequent sweep. If both deliver,
the short-duration bracket contracts to 0 failure / +6 success; a short success
and long failure exposes a duration regression; two misses advance the observed
failure boundary without establishing a sensitivity floor. Accepted incorrect
content remains an integrity failure regardless of characterization status.
The receiver and its thresholds are unchanged. Existing negative-control gates
remain required and their failures remain visible; this pair does not claim
new false-alarm calibration or a receiver repair.

## Completed lower-energy characterization

The [four-row lower campaign](local-evidence/hf-vector-two-hop-sensitivity/run.json)
finishes in 2,214.45 s with four strict valid receipts and no execution failures.
Both wanted observations miss all thirteen active slots, classifying them quiet;
there is no DATA, physical completion or accepted content. The noise 0/64 and
wrong-key −3/128 controls each have fifteen quiet decisions, zero false DATA
and zero accepted messages. These controls do not erase the +30 wrong-key
failure, and they do not constitute a complete control matrix at every point.

| Wanted point | Actual seconds/bit | Per-active-window clean E/N0 | Active operational log evidence | Complete delivery |
|---|---:|---:|---:|---|
| 0 dB-Hz / requested 64 s | 69.6 | 26.5520–39.1119 (14.241–15.923 dB) | −6.5466 to 7.4126 | No |
| −3 dB-Hz / requested 128 s | 139.2 | 26.8616–38.6516 (14.291–15.872 dB) | −5.9225 to 11.8559 | No |

The operational threshold is 21.4934. Every slot executes the coarse proposal
search, four affine fine candidates, eight varying candidates, linear-drift
proposals and coherent-gain blocks. The miss is therefore not attributed to
skipped acquisition. The longer capture has 1.995967× raw incident energy and
1.969054× propagated energy; after noise normalization, the ratios are
1.000353× incident and 0.986865× propagated E/N0. Neither wanted point delivers:
this is neither a repaired duration trend nor a demonstrated duration regression.

The existing `known_mean_miss` analysis is reused only after strict validation
of each disjoint actual decision window and the proper white-noise plane. At
the fixed single-known-waveform false-alarm constraint 10⁻⁹, conditional presence
miss probabilities span 0.002209–0.098622 for the short case and
0.002600–0.091462 for the long case. Even this favorable reference does not
promise perfect reception at these energies. It knows the exact received
waveform, phase and amplitude; it is not BER, native false-alarm calibration,
message completion or an attainable practical sensitivity floor. No whole-message
energy pooling is used and no wanted-signal reference is assigned to a wrong-key
control.

Native preparation is 494.388/494.306 s for the short/long pair; rendering is
5.189/9.725 s and receiver control/wait is 34.619/70.827 s. Both retain W=24/H=12
and reach 24 workers. These descriptive costs do not qualify real-time capacity.

The [source-bound decision-window analysis](local-evidence/hf-multihop-preflight/two-hop-sensitivity-analysis.json)
retains exact energy apertures, raw-receipt and binary identities, table/input
hashes, branch work and the existing scalar reference. Numerically underflowed
bright-case probabilities are explicitly marked, rather than reported as an
exactly zero failure probability. Negative and ending windows receive no
wanted-waveform oracle.

## Near-boundary observations

The admission-only extension passes the three affected native integration,
runner and qualification groups in 61.21 s after the earlier 41-group pass.
[Final affected checks](local-evidence/hf-multihop-preflight/boundary-build-and-checks.log).
Neither the physical solver nor the receiver or thresholds changes.

The +6/64 observation recovers twelve correct DATA bits, but slot 3 is a quiet
erasure. Both ending slots are quiet, producing physical completion; the codec
then rejects `message_contains_erasures`. No wrong content is accepted. This
advances the short-message failure boundary to +6 rather than treating partial
bit recovery as delivery.

The missed aperture contains 50,979.8346 clean energy-sample units over 135,065
samples. Dividing by noise variance 482.2822 gives E/N0 105.7054 (20.241 dB).
The favorable exactly-known-mean presence reference has miss probability
6.58646×10⁻¹⁸ at its fixed 10⁻⁹ false-alarm constraint. That contrast is useful
for receiver investigation, but channel knowledge and search-prior costs are
not free and the reference is not a demonstrated practical decoder.

Native bit evidence strongly prefers the correct side: [−6.7487, 20.4432],
separation 27.1919. Operational log evidence 19.7501 remains 1.7433 below the
21.4934 threshold, while quiet energy 15,855.638 is below 16,237.853. The slot
executes four affine candidates, eight varying candidates, 512 drift slopes /
1,965 frequency bins and coherent-gain blocks. This is a near-threshold
presence failure with correct-bit preference, not missing search execution.
The existing strong wrong-key false-DATA failure prevents promoting a simple
threshold reduction as a repair.

The [completed boundary pair](local-evidence/hf-vector-two-hop-boundary/run.json)
takes 1,049.72 s with two strict valid receipts and no execution failures.
**The +3/128 partner delivers exact `E`**, with thirteen correct affine DATA
decisions, two quiet ending decisions and one accepted message. Its active
decision-window E/N0 spans 106.9380–153.8748 (20.291–21.872 dB); operational
log evidence is 25.8583–90.3030, above 21.4934 throughout.

Total incident E/N0 changes by +0.001534 dB and propagated E/N0 by −0.057424 dB
relative to the short partner. Raw incident and propagated energy still rise
1.995967× and 1.969054×, respectively; they are not held constant. Crucially,
the same evolving physical field redistributes energy between decision windows:
ordinal ratios range 0.690102–1.455695. The formerly erased slot 3 receives
1.455695× E/N0 (about +1.63 dB), and its operational evidence rises
19.7501→76.1755. This is a positive complete-message time/E/N0 example,
not an isolated integration improvement at equal information in every bit or
a demonstrated receiver repair.

The long-duration complete-message bracket is now −3 dB-Hz failure / +3 success
for this fixed realization. The short-duration bracket is +6 failure / +20
success. Neither is a natural-condition sensitivity floor or a success-rate
estimate. No point-specific negative-control qualification is claimed for the
new pair; the original strong wrong-key failure remains unresolved.

The long trial takes 537.168 s natively, including 491.787 s preparation,
8.742 s rendering and 36.427 s receiver control/wait. It preserves W=24/H=12,
reaches 24 workers and six owners, and reserves 3,056,244,354 bytes within its
8,591,981,645-byte receiver limit. The physical plan and coefficient-table hash
match the short case and the preceding direct primary cases.

## Final finite audit and retained limits

The [59-cell qualification audit](local-evidence/hf-multihop-preflight/qualification-59.json)
validates all 59 receipts and preserves every preceding 43-cell and 51-cell gate
dictionary. The actual target executable matches
`8ebb312bc9a361ae4c642ea188776f35e468e8bd5de5732023438e86ec2fce01`,
with appraisal source
`1cdab5b39ead580301372884612586e45ce66d4ebea256410d8b4162ac12c8ec`.
Only the two latest rows bind to this exact appraisal executable; earlier rows
retain their historical identities rather than being relabeled current. The
canonical native source is unchanged throughout the increment.

The aggregate qualification remains **failed**. The new field preserves the
strong wrong-key false-DATA failure and the required impaired-endpoint delivery
failure; characterization misses remain visible without retroactively making
them promised operating points. Previous integrity, false-DATA and completion
failures also remain intact. Software correctness, valid evidence and satisfied
communications requirements are separate outcomes.

This increment contains sixteen unique strict complete receiver observations:
eight deliveries and four misses among twelve wanted observations, plus four
negative controls. The original metadata-recovery execution repeats an input
and is not an extra independent case; the original endpoint setup refusal has
no receiver outcome. Numerical probes are separate from complete-message trials.
The source-bound sensitivity analysis validates 120 decision windows in eight
selected upper/lower/boundary captures and applies its favorable presence
reference only to 78 active wanted windows.

The specific selected coupled-hop and rapid-cancellation cases are now
implemented, numerically admitted and exercised through the native receiver.
This is not general curved-Earth multiple-hop coverage, naturally distributed
rapid plasma evolution or calibrated disturbed-event replay. The public-source
audit above establishes concrete data leads and their limitations, not the
nonexistence of suitable measurements. The named missing strata remain explicit
in the qualification specification. No unusual-day reliability probability,
universal sensitivity floor, competitor gain, real-time capacity or receiver
repair is established. No RunPod resource was created for this increment.

Retain the new numerical/communications receipts, continuity audits and compact
analysis while they support these unresolved failures and current validation
claims. Applied preparation patches and superseded build-only logs are removed;
the compatible build and source-bound full/affected test logs remain. When a
claim is superseded, distill its finding and provenance here before removing
redundant evidence.

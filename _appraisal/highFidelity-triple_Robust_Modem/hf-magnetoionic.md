# Coupled magnetic-polarization HF appraisal

This continuation begins from `e0e7b74ff2a69083f952c1ad436bc92d4fe1ec6d`
on 14 November 2026. Its objective is to put a previously missing physical
mechanism through complete native reception, with independent numerical
checks. The prior [scalar propagation investigation](hf-inference-and-propagation.md)
and its receiver failures remain evidence; they are not superseded by adding
a new model. All changes remain in this appraisal directory.

## Physical scope and source reconciliation

The reference review distinguishes O/X mode behavior from arbitrary scalar
fading and requires explicit launch/receive polarization and mode provenance
([Astra skywave review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md),
[midlatitude/NVIS review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md)).
The new model solves a cold, collisional, layered plasma at normal incidence
with an arbitrary fixed geomagnetic vector. Its reflected and transmitted
fields are Jones matrices in fixed laboratory x/y coordinates. Polarization
conversion, attenuation, phase and dispersion must follow from that one
dielectric model. The receiving antenna is fixed before outcomes; a private
second-polarization energy measurement does not give the receiver diversity.

The vector stratified formulation follows the electromagnetic boundary
problem, rather than assigning independent random O/X gains; see
[Berreman (1972)](https://doi.org/10.1364/JOSA.62.000502).
At normal incidence, `Dz=0`. For nonzero `epsilon_zz`, eliminating the induced
longitudinal electric field is an exact algebraic reduction of the local cold
Maxwell equations. It retains transverse/longitudinal coupling. Unresolved
near-singular longitudinal resonances must be rejected without artificial
collisions or denominator clipping. Warm/kinetic spatial dispersion and
propagating pressure/Bernstein branches require a different model; the
[magnetized mode-conversion literature](https://arxiv.org/abs/physics/0602155)
does not justify silently continuing through an unresolved resonance.

[Jiang et al. (2025)](https://doi.org/10.1029/2024JA033316) model differences
in O/X absorption that depend on frequency, direction and ray geometry.
Those are modeled China-link results, not measured transfer functions for
this fixture. They motivate coupled attenuation and polarization, without
calibrating the constructed column. By contrast,
[Strong, Cohen and Galkin (2025)](https://doi.org/10.1029/2024GL113123)
compare ionosonde- and VLF-inferred absorption, including O/X separation
across 2.5–4.5 MHz. Their lower-frequency X returns saturate, and their
discussion cautions that a D-region profile suitable for VLF can give
inaccurate HF absorption. This is empirical context with detection and
profile-model limits, not a calibrated complex response for this column.
The 2026
[PyRayHF work](https://doi.org/10.1029/2026RS008702) validates group-path
calculations against other ray methods; group-path agreement alone does not
validate complex field amplitudes or interference. Forward/paraxial vector
models such as [Rino and Carrano (2021)](https://doi.org/10.1016/j.jastp.2021.105558)
also do not independently qualify near-cutoff reflection.

This one-dimensional slice does not model horizontal irregularities,
three-dimensional strong scattering, reflection caustics, an oblique hop or
multiple ground reflections. A slowly changing sequence of qualified frozen
responses is still an explicit finite-memory approximation, not a solution of
the fully time-dependent plasma equations.

## Frozen physical path and numerical hierarchy

Before numerical and communications outcomes, fix RF at 3.4 MHz and
`B=(25,0,43.3012701892)` microtesla: magnitude 50 microtesla, tilted 30 degrees
from vertical. The field does not rotate with time. Use the existing D/F
cosine-squared column geometry, F collision frequency 60/s and fixed D peak
electron density `8e8 m^-3`. Let `Y=f_gyro/f_RF`; F peak plasma frequency is
`sqrt(1-Y)*f_RF*(.9999+.0002*q)`, for `q` from zero to one. This crosses the
lower magnetoionic cutoff in a constructed sensitivity case. It is not a
measured storm distribution, and the small selected density span does not
represent the total range of disturbed-day densities.

The physical profile traverses a clamped half-cosine over 600 seconds,
starting at physical second 60. A separate time-history holdout starts at
180 seconds and traverses over 780 seconds. Both are selected before decoder
outcomes. The latter changes temporal evolution/placement on the same
physical manifold; it is not an independent density-profile family.

The first table design has 17 profile knots, two independent non-knot
profile witnesses per interval, 128 frequency bins and 256-bin refinement
witnesses. Frequency holdouts include occupied/guard anchors and off-grid
frequencies. Production layer spacing is at most 10 m, with 20 m witnesses
at 17 frequencies at each of three fixed profiles, not a complete coarse
frequency grid. The first design is bounded to 3,444 vector RF
solves, preceded by a cheap cost/geometry pilot. An elapsed cap is selected
from that pilot before the full preparation, rather than interpreting a
timeout as physical failure.

The 27-solve pilot then completed with initial analytic solver checks passing.
At carrier q=0/.5/1, total reflected power for an x launch was approximately
0.002774/0.029870/0.058419. The two laboratory output components were
comparable: this establishes a mode-cutoff/projection test, not a demonstrated
deep polarization-cancellation case. Minimum sampled `|epsilon_zz|` was
0.38122. Across nine profile/frequency points, 20 m versus 5 m maximum Jones
entry differences were `2.523e-5` in reflection and `3.974e-5` in transmission.
Measured 10 m solve time was approximately 8.2 ms on one local CPU lane.
Keep the originally selected 10 m production/20 m witness meshes and set
the full numerical probe's elapsed cap to 180 seconds before its outcome.
These abbreviated times select a practical test budget; they are not receiver
capacity measurements.

Reuse the existing scalar causal FIR design at 1920 samples/s, with occupied
band ±384 Hz. Fixed TX x excites both reflected x/y components. The provisional
support is lags 1..81 including a 32-sample modeling delay; numerical gates
must establish its adequacy before it is used. Cubic Hermite interpolation is
in fixed Cartesian coordinates, with no phase extraction at zeros or
arbitrary eigenvector-label interpolation. Each component must meet the fixed
0.005 absolute response-error and `1e-6` discarded-energy gates. The separate
finite-FIR output-time versus per-tap-retarded bound must be at most 0.005
in peak incident-amplitude units for the declared transition. This bound is
not an evolving-Maxwell error bound. Failure withholds receiver admission;
any finer numerical follow-up preserves the physical span and original failure.

An absolute-error pass does not qualify relative fidelity near a field null.
Before the first full probe, add zero-extra-solve occupied-band diagnostics
comparing actual complex approximation errors with the corresponding raw
component amplitudes, retaining their stage/profile/frequency locations.
Undefined relative errors at an exact zero remain explicit. These diagnostics
do not change the frozen absolute gates or turn a sampled comparison into a
uniform bound. An exact-zero stream control uses the same prepared table.

Cheap independent controls precede table preparation: B=0/scalar reduction,
longitudinal circular eigenchannels, perpendicular O-mode reduction,
appropriate B-reversal transpose symmetry, vacuum, lossless and passive
flux checks, mesh convergence, singular-case rejection, chunk invariance,
zero signal and retained tails. Magnetic media must not be forced to satisfy
ordinary reciprocal symmetry at fixed B.

## Frozen complete-receiver cohort

Before numerical admission or decoder outcomes,
[`hf-magnetoionic-receiver.json`](campaigns/hf-magnetoionic-receiver.json)
fixes eight complete-receiver observations. Primary x reception uses incident
C/N0 of 40, 20 and 0 dB-Hz; y reception uses 40. A matched x/20 comparison
uses the existing native 7 ms spread setting. Default zero spread is a one-ray
receiver model; the 7 ms setting adds sparse delayed-ray anchors, not an
81-tap matched channel. Both retain the complete public arrival interval.

The fresh `B` payload uses separate source/noise roots and the predeclared
180/780-second time-history holdout at 20 dB-Hz. The primary `A` cases share
source/noise roots 65001/75001; holdout roots are 65002/75002. Fixed noise-only
and wrong-key controls use the x/40 geometry. All request 64-second bits;
actual native duration, source length and physical quiet observations must
come from the canonical calendar. Caps are 600 seconds and twelve million
samples per row, eight rows/4,800 seconds overall. The full physical table
must first pass its numerical gates; its setup cost remains charged in every
CLI invocation that builds it. These are heterogeneous descriptive cases,
not an independent sample for a natural error-rate confidence interval.

## Changing polarization: separately frozen parallel-field case

Review of the first pilot found that its reflected polarization was almost
constant across the density transition. It tests magnetic cutoff and dispersion,
but cannot stand in for changing antenna cancellation. A separately selected
18-solve pilot therefore uses `B=(0,0,50)` microtesla and the upper cutoff,
`sqrt(1+Y)*f_RF*(.9999+.0002*q)`, with all other physical constants unchanged.
The two circular modes remain uncoupled in this symmetry; their coherent
superposition at a fixed linear antenna changes as their relative phase changes.
This is physical two-mode interference, not inter-mode conversion.

The pilot samples q=0/.25/.5/.75/1 at carrier and ±384 Hz, plus three carrier
5 m mesh witnesses. At carrier q=.25, total reflected power is .127658 while
the x antenna receives only .0080315 of that power, a 20.95 dB projection loss.
Carrier x fractions across the five profiles are .70983/.00803/.64070/.78087/.14615;
the normalized polarization overlap squared at q=.5 versus q=0 is .137878.
The largest pilot mesh differences are `1.196e-5` (reflection) and `1.117e-5`
(transmission). These pilot points establish a distinct mechanism, not full-band
numerical admission. The parallel symmetry makes the longitudinal mode exactly
decoupled; its small `|epsilon_zz|` does not justify dividing through a spurious
coupling term.

Before decoder outcomes, retain the same 17-profile/128-frequency design,
3,444-solve maximum, 180-second probe cap and absolute numerical gates as the
tilted case. Raw polarization/Stokes and relative-error diagnostics use already
computed RF solutions, at no extra solve cost. The changing cutoff means that
the counts of occupied off-grid/profile/mesh witnesses are respectively
181/348/27 per component, versus 187/352/33 for the tilted path.

The 16-interval parallel probe then passed its frozen absolute gates in
25.47 seconds, but the separate relative diagnostic prevented receiver use:
at q=.11662658773652741 and −118.7 Hz, the raw x magnitude was .000282765 and
its interpolation error .000644046, or 2.278 times the field. The sampled x
fraction of total reflected power was `6.049e-7`. An absolute .005 tolerance
alone therefore cannot establish the fidelity of this deep null. No receiver
observation uses this original parallel table.

Before further outcomes, freeze a numerical-only follow-up with 32 parallel
profile intervals and the unchanged physical trajectory, frequency design,
Hermite rule and absolute gates. Retain the original failed q/f as a separate
witness with 10, 5 and 2.5 m direct solves. The cap is 6,311 solves/180 seconds,
including those three additional solves. For this archived **nonzero** witness,
require `epsilon = |H_table-H_2.5m| + |H_5m-H_2.5m| <= .10*|H_2.5m|`.
This named local target limits the guarded amplitude interval to ±10% (about
−.92/+ .83 dB in power); it is not a uniform relative-error requirement at
every possible zero. Report mesh differences and evidence of convergence;
the finest-mesh difference is not a rigorous continuum bound. At zero or an
unresolved field, report an absolute interval and resolution floor.

If 32 intervals fail, allow exactly one fixed 64-interval follow-up, capped at
12,039 solves and 180 seconds, retaining the original witness and new independent
holdouts. A failure at that cap remains unresolved; no physical parameter or
tolerance changes are allowed. Tilted-field reception retains its original
16-interval method and evidence.

[`hf-magnetoionic-parallel.json`](campaigns/hf-magnetoionic-parallel.json)
freezes seven receiver observations before this full numerical probe: x at
40 and 20 dB-Hz/64 requested seconds, x at 17 dB-Hz/128 requested seconds,
y at 20/64, the fresh `B` 180/780-second holdout at 20/64, and x/40 noise and
wrong-key controls. Roots are 66001/76001 and 66002/76002 for the holdout.
The longer case is a near-equal incident-energy comparison; actual native bit
duration and propagated energy must be reported. It crosses the same physical
transition on a different calendar and is not an identical-history comparison.
Caps are seven rows, 600 seconds/twelve million samples per row and 4,200 seconds
overall. A rejected table prevents receiver execution; no gate or physical
profile will be relaxed to admit it.

## Long observed-voltage stress cases

The [Gannon fixture provenance](fixtures/hf-gannon-2024/README.md) supplies two
complete 3,600-second, 10-complex-sample/s recordings from W2NAF on 10 May 2024,
starting at 17:00 and 20:00 UTC. The applicable nine-channel stream metadata
identifies column 4 as nominal 10 MHz. It is distinct from the archive's
interleaved one-channel stream; full archive paths and hashes resolve duplicate
basenames. A single two-hour RMS preserves the raw knot-energy ratio of
14.9685 (11.75 dB), rather than making each hour equally strong. Cartesian
interpolation between the knots changes their continuous-time energy differently;
the full-hour interpolated power ratio is about 9.3953. Native receipts report
both knot and continuous means. Relative digital
voltage does not establish path loss or a calibrated incident C/N0.

The source is the [2026 Gannon dataset](https://zenodo.org/records/20466479),
reconciled against [GRAPE instrument methods](https://doi.org/10.5194/essd-15-1403-2023).
Instrument-family GPS discipline does not establish this individual record's
clock, gain or noise floor. Nominal 10 MHz may include both WWV and WWVH.
The recordings are storm-era observations, not proof that every feature was
storm-caused, and their ten-Hz recorded bandwidth is not a measured broadband
modem impulse response.

Multiplication by recorded voltage also multiplies its measurement noise by
the modem signal. A true beacon blackout can consequently produce synthetic
modulated output from recording noise. The native analytic control explicitly
compares carrier-plus-noise, absent-carrier-plus-noise, and the known zero true
channel with the same replay operator and fixed scale. No success will be
interpreted as field blackout availability or a calibrated false-alarm result.

[`hf-gannon-receiver.json`](campaigns/hf-gannon-receiver.json) freezes six
complete receiver observations before outcomes: 17:00 at synthetic incident
40/20 dB-Hz, 20:00 at 20, 20:30 placement with fresh `B` payload/noise at 20,
and 17:00 noise/wrong-key controls at 40. Primary roots are 67001/77001;
holdout roots are 67002/77002. All request 64-second bits, Helper off, with
the canonical reference waveform. The declared 5 Hz atmospheric allowance
and 1 ms synthetic delay are model choices, not measured support guarantees.
In particular, Cartesian chords near a null can have fast instantaneous phase
rotation; the record's sample rate does not establish a strict 5 Hz Doppler bound.
No wrap, stretching, carrier removal, local normalization or gap repair is
allowed. Caps are six rows, 600 seconds/twelve million samples per row and
3,600 seconds overall. If a wanted case fails, diagnose its first missed active
slot and at most one later active slot with the existing isolated-bit conditional
oracle; this is not the identical full-message observation.

## Sensitivity references

Reception comparisons must keep three losses distinct: attenuation or antenna
projection removing available signal energy; inference losing information
present at that antenna; and presence/framing decisions failing despite useful
bit preference. Incident C/N0 is not a post-channel sensitivity rating.

The ideal known-channel diagnostic is a useful reference, not a feasible
blind-receiver performance promise. Longer transmission at fixed energy can
introduce additional unknown channel evolution. For example,
[Sethuraman et al. (2009)](https://arxiv.org/html/0712.2872v2) derive different
low-SNR behavior for stationary correlated Rayleigh channels under fixed
peak-to-average constraints than under fixed peak power. Their assumptions
are not a quantitative law for this deterministic ionosphere. They demonstrate
why coherent AWGN scaling cannot be imposed as a universal benchmark, while
leaving avoidable finite-receiver losses open to diagnosis and improvement.

## Executed evidence

### First tilted-field numerical and receiver results

The full 3,444-solve numerical probe passed in 27.98 seconds. Maximum profile
interpolation errors were .002034/.002146 for x/y, mesh errors below `1.95e-5`,
frequency-refinement errors below `2.28e-7`, and discarded energy about
`3.55e-11`. The 600-second finite-retardation bounds were
`3.2321e-5`/`3.4100e-5`. The largest sampled occupied-band relative component
error was about 1.68%; none of those sampled errors exceeded the corresponding
raw component magnitude. Zero input, chunk invariance, tails and rejection of
the inadmissible 1 ms transition passed. These are sampled numerical checks
plus a finite-FIR time bound, not a uniform evolving-plasma guarantee.

The eight frozen complete-receiver cases all ran once, uncensored, in 481.33
seconds of aggregate native elapsed time on the local Ryzen 5 PRO 5650U CPU.
Native automatic ownership retained H=12/W=24. No GPU or RunPod instance was
needed. These elapsed measurements include physical table preparation and
are not a receiver-capacity qualification.

| Tilted-field observation | Complete correct payload | Missed active bits |
|---|---:|---:|
| x, 40 dB-Hz | `A` | 0 |
| x, 20 dB-Hz | No | 3 |
| x, 0 dB-Hz | No | 13 |
| y, 40 dB-Hz | `A` | 0 |
| x, 7 ms spread, 20 dB-Hz | No | 4 |
| Fresh `B` and 180/780-second history, 20 dB-Hz | No | 5 |

No wanted case accepted wrong content or ended prematurely. Missing opening
bits left fragments that the codec rejected. Noise produced no DATA or accepted
message. Wrong-key reception produced **seven false DATA and six ambiguous
decisions**, followed by quiet completion; the codec accepted no message.
This is an observed decision-layer weakness even without a false accepted
payload. The sparse-delay receiver comparison does not repair the weak message.
Its capture is 13 samples longer, and a common-prefix IQ hash was not measured;
the fixed seeds/source/channel do not justify claiming an identical full capture.

The first native row succeeded but the strict receipt validator initially
rejected its tail duration due to a one-ULP difference. Review also found wrong
occupied-band sample counts and a fabricated aggregate-energy shape in the
test fixture. Repairs use a four-ULP scalar tail tolerance, exact integer
geometry/counts, and the real regional/aggregate receipt schema. A compact
projection of the actual receipt is retained as a schema regression fixture.
The successful native row was revalidated offline, not rerun; a seven-row
continuation exactly preserves the original remaining observations. Original
invalid harness status and both reports remain visible. The strengthened runner
passed 108 checks at this checkpoint.

The first receiver executable SHA-256 is
`16a9c3830c228fd71ea2e2ab2f5ffa4852fb80019bfbc2054d3b0f94d5fc1e7f`,
with appraisal source `01b86480be64488663fc39abd70617c23ce9ddd2ab0ca926eb95deb96fea805d`.
Canonical native source remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
Raw receipts, table/input hashes and complete slot outcomes are indexed by
`local-evidence/hf-magnetoionic/receiver-cohort-summary.json`.

The [two magnetic diagnostics](campaigns/hf-magnetoionic-diagnostics.json)
use isolated first bits, with three independently qualified physical channel
instances per row; all preparation is charged. In the tilted 20 dB-Hz case,
binary distance/noise is 22.743 and known-channel binary error probability
.0003729, but those assume a signal is already known to be present. At the
native presence false-alarm target `4.6296e-10`, even the known-phase/amplitude
isolated wanted signal has only **9.09% detection probability**, with
energy/noise 11.4552. Its missing opening bit therefore cannot be attributed
solely to deficient blind inference. This is an isolated, per-observation
reference; joint-message information, coding and Helper could change the problem.
The three new tilted tables exactly reproduce the original coefficient digest
`5f6c1d7191094f592695080843267c0b0a71a22952cf2c3467a0e8f18ee041e9`.

After this diagnostic and before its outcome, freeze one additional
[`hf-magnetoionic-midpoint.json`](campaigns/hf-magnetoionic-midpoint.json)
observation: tilted x at 30 dB-Hz, unchanged 64-second request, `A`, source/noise
roots 65001/75001, 600-second/twelve-million-sample cap. This narrows the
20-to-40 dB-Hz message bracket without further bisection. With identical source
and physical response, +10 dB gives tenfold isolated energy/noise; no new oracle
run is required to establish that scaling. Actual table/source/calendar and
clean-energy identities must be checked before interpreting the result.

That midpoint completed correctly in 53.89 seconds, with all thirteen active
bits correct and no premature ending. Source-generation metadata, all fifteen
public slots, bounds, coefficient digest and every regional clean energy match
the earlier 20 dB-Hz observation exactly. Noise variance changes from 19.2 to
1.92. Slot 0 now has native presence score 92.40 against threshold 21.49.
The fixed-case complete-message bracket is therefore **20 dB-Hz fails / 30
dB-Hz succeeds**; no additional SNR search was run. Both source identities and
the exact comparison are in `local-evidence/hf-magnetoionic/midpoint-summary.json`.

The parallel 20 dB-Hz isolated reference has binary distance/noise 736.66 and
wanted energy/noise 375.61, with conditional known-presence probability rounding
to one. Its complete native message also succeeds. All three reference tables
match the seven parallel receiver tables, digest
`4b9349084eaecd378e251b28083287026cb4ac341e37eea8e6b37316249d04bc`.
The two magnetic references complete in 360.63 seconds total, with 10,332 and
36,117 RF solves respectively; three preparations per row remain fully charged.
Both actual schemas pass independent validation. These are four private
diagnostics in total including Gannon, alongside **22 complete-receiver
observations**, not pooled success-rate trials.

### Parallel numerical refinement outcome

The 32-interval probe completed all 6,311 solves in 47.07 seconds and correctly
withheld its table. The archived witness's guarded error was 29.59%, despite
the new ordinary heldout grid having only about 1.24% maximum relative x error.
Keeping the original failed point was consequential: fresh sample positions
alone would have missed it. The 10-to-2.5 m and 5-to-2.5 m differences were
`9.076e-6` and `1.821e-6`, supporting spatial convergence at that point.

The predeclared 64-interval follow-up completed 12,039 solves in 93.45 seconds
and passed. The archived fine-reference x magnitude is .000279536; guarded
complex error is `1.160e-5`, or **4.15%**, with power interval
`[7.179e-8,8.476e-8]`. At this particular q/f point, x power is about 62.3 dB
below total reflected vector power. That is a narrowband polarization null,
not a 62 dB whole-modem attenuation or sensitivity result.

Maximum sampled profile errors are `6.655e-5`/`6.294e-5` for x/y, mesh errors
below `4.784e-5`, and discarded energy below `3.378e-11`. None of the
703 knot-offgrid, 1,395 profile-heldout or 27 mesh occupied checks per component
has error exceeding its raw field. The final table also passes direct
convolution, both-component tails, chunk invariance, zero input and fast-law
rejection. The same 780-second holdout is slower than the qualified 600-second
law. Its waveform still carries the physical loss; it is never renormalized.

At the nearest time sample to the archived q, a unit monochromatic diagnostic
measures output-time versus per-tap-retarded x difference `9.899e-6` (3.54% of
the sampled fine field). The broader finite-FIR bound is `4.838e-5`, about
17.3% of that small field. Thus the 10% local static-resolution target is **not**
a 10% guarantee for dynamic propagation. This finite-kernel comparison also
does not validate an evolving-Maxwell solution.

For scale only, `C/N0*T*error^2` using the largest sampled profile error is
about .0031 at 40 dB-Hz/69.6 seconds, and about `3.1e-5` at the weak 20/69.6
or near-equal-energy 17/139.2 settings. This is an indicative coherent-tone
error-energy scale at sampled responses; it is not a uniform error bound for
the time-varying modem operator or a receiver sensitivity limit.

The 16/32/64 receipts remain separate in `local-evidence/hf-magnetoionic/`.
Only the 64-interval parallel table is admitted for the frozen seven-case
receiver campaign. The original tilted table retains its prior construction.

### Parallel complete-message outcomes

All seven frozen observations completed once, uncensored, in 845.75 seconds
of native elapsed time. **All five wanted messages completed correctly**:
x/40, x/20, y/20, the fresh `B` time-history holdout, and the longer x/17 case.
They had no wrong DATA, active erasures or premature endings. The noise control
had fifteen quiet decisions and no message. The wrong-key control produced
thirteen false DATA followed by physical completion, but codec rejection and
no accepted message. This control remains a failure of DATA discrimination;
one rejected payload does not establish general wrong-key safety.

The duration comparison uses actual native bit times **69.6 and 139.2 seconds**.
Its transmitted energy/noise ratio is 1.000348945, while energy/noise at the
selected antenna is .952965604 of the shorter case. The longer message succeeds
with nearly equal incident energy and about 4.7% less received energy. This is
a concrete positive time/energy comparison on the frozen physical trajectory,
not a population trend or a reversal of the earlier unrelated long-message
failures. Its physical calendar changes: the longer transmission traverses the
transition differently and spends more time in its final state.

The numerical null is local in frequency/time; successful complete broadband
messages do not show reception through an arbitrary full-band polarization
blackout. The y case is a separately fixed receiving antenna, not private
adaptive diversity supplied to the x receiver. The holdout changes time law,
payload and source/noise roots on the same density-profile manifold.

Executable SHA-256:
`0cfa388d91176fe642b6f7ec8919abd27ad0d589b4220c3e1f231f465435f3db`.
All seven actual receipts also passed independent strict offline validation,
recorded in `local-evidence/hf-magnetoionic-parallel/strict-validation-parallel64-v1.json`.
The source/table/input identities, actual energy and complete slot outcomes
remain in `local-evidence/hf-magnetoionic/parallel-receiver-cohort-summary.json`.

### Long-record outcomes and conditional failure diagnosis

All six Gannon receiver observations completed once, uncensored, in 112.71
seconds. Generic validation at launch was followed by strict offline validation
of the actual receipts: source/serialized hashes, exact archive members and
stream, UTC placement, joint normalization, finite support and measurement
limitations all passed. The original run remains intact; the separate
`local-evidence/hf-gannon/strict-validation-gannon-v1.json` binds the later
validator and receipt hashes. This is not six additional communications trials.

| Observed-voltage stress | Complete correct payload | Other outcome |
|---|---:|---|
| 17:00 record, 40 dB-Hz | `A` | All 13 active bits correct |
| 17:00 record, 20 dB-Hz | `A` | All 13 active bits correct |
| 20:00 record, 20 dB-Hz | No | Slot 2 quiet; other 12 active bits correct; codec rejects one erasure |
| 20:30 placement, fresh `B`, 20 dB-Hz | `B` | Complete physical ending |
| 17:00 noise control | No accepted message | Zero false DATA |
| 17:00 wrong key | No accepted message | 13 false DATA; codec rejection |

There were no premature completions or accepted wrong messages. The later
placement is within the same 20:00 record; it is not another independently
sampled disturbed event. Noise-only here means no synthetic modem source plus
independent simulated receiver noise, not a field noise-only recording.

The predeclared follow-up rule selected the first missed slot (2) and first
later correct slot (3), recorded in
[`hf-gannon-diagnostics.json`](campaigns/hf-gannon-diagnostics.json). Both
isolated-bit diagnostics completed in 2.16 seconds total. The earlier native
presence scores were 9.72 and 58.93 against threshold 21.49; slot 2 already
favored the correct bit, but failed presence admission. Conditional on the
entire recorded-voltage multiplier, the isolated known-waveform squared
distances divided by synthetic noise variance were 482.67 and 804.12,
with known-presence energy/noise about 243 and 412. The fixed unshifted
full-aperture references retained 11.47% and 3.91% of clean coherent energy;
these are different, much less informed diagnostics than the known operator
or the native receiver's searched models.

Short .25-second independent-gain partitions raise retained energy to about
52.6% and 46.6%, but their correct-branch marginalized log likelihoods remain
negative (−512.5/−459.6). Their estimation cost defeats a simple short-block
presence repair. No likelihood threshold, receiver prior or production policy
was changed. These private isolated-bit observations omit adjacent DATA and
condition on measurement noise as part of the multiplier. They identify a
useful inference investigation, not an exact-IQ full-message repair or a
physical field-sensitivity bound.

The six-receiver executable SHA-256 is
`d0dfbdb2b0242cd20ca27b5678f888a212bed520abf69f72f6eba694de9f71df`;
the diagnostic executable is
`cd62157b7c2729207ccb0bd01d66afb7614eca1fe2a9f16407f9fc35a65a9b1b`.
Source identities, inputs and scoped comparisons remain in
`local-evidence/hf-gannon/receiver-cohort-summary.json` and
`local-evidence/hf-gannon/diagnostics-summary.json`.

### Software checks and reproduction

The final contained `build.sh test` passed **28/28 CTest groups in 83.59 seconds**,
including 113 strict-runner tests. All 22 receiver observations and four private
diagnostics completed uncensored. Software passes do not erase the failed
messages, false DATA or rejected numerical approximations above. Full-check
log and final build identity are in `local-evidence/hf-magnetoionic/final-build-and-tests.log`
and `final-validation.json` in the same directory.

Execution used a Ryzen 5 PRO 5650U, twelve visible logical CPUs and H=12/W=24
native automatic CPU reception; Linux reported 14.95 GiB physical RAM. The
physical solver uses native binary64 complex/double arithmetic. No GPU work or
capacity qualification is claimed for these runs. Compiler/runtime, memory
and backend provenance are in `local-evidence/hf-magnetoionic/execution-environment.json`.
No RunPod instance was deployed for this continuation.

From the appraisal directory, `./build.sh test` runs the software checks.
`timeout 180 .build/bin/high_fidelity_magnetoionic_probe --parallel-upper`
runs the expensive final parallel numerical admission; omitting the option
checks the tilted table. Use the standard runner with the fixed manifests:

```sh
python3 -B run.py --probe .build/bin/high_fidelity_appraisal \
  --campaign campaigns/hf-magnetoionic-parallel.json
python3 -B run.py --probe .build/bin/high_fidelity_appraisal \
  --campaign campaigns/hf-gannon-receiver.json
```

The earlier failures remain explicit. The first integrated focused run passed seven of eight groups, including
integration. The scenario inventory test omitted the new magnetic renderer
when counting mutually exclusive spectral models. Its repair adds that model
to the count while retaining the finite-level and model-exclusivity checks;
the affected scenario/kernel rerun passed two of two groups in 1.35 seconds.
This fixture failure is retained in `focused-tests.log`, separately from
numerical propagation and communication outcomes. Standalone analytic checks
passed 76 magnetic, 36 scalar and 44 existing scalar-kernel assertions.

One direct build invocation also failed because it attempted to write the
shared read-only compiler cache. The contained build wrapper supplied its
existing local temporary/read-only cache settings and the build passed; no
source or numerical acceptance criterion was changed for that environment issue.

### Evidence lifecycle

Numerical and communications protocols and outcomes remain distinct.
Temporary receipts belong in `local-evidence/hf-magnetoionic/` and named bounded
campaign directories. Retain them while numerical admission, polarization
loss and receiver sensitivity questions remain unresolved; when superseded,
distill the findings here and remove redundant outputs. No production receiver
policy or native hook is changed by this model expansion.
The large measurement download and unselected recordings were removed after
verified extraction. Superseded standalone executables were removed after
the integrated targets passed. Retain the two small exact Gannon fixtures
while the recorded-input regressions require them; retain raw source/metadata
only while provenance questions remain current. Do not cache whole old campaigns
or copied receiver implementations as alternative operating paths.

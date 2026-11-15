# Intrinsic evolution of a coupled vector HF field

This experiment changes the plasma density structure during a complete message.
It extends the [two-transverse-coordinate Maxwell model](hf-vector-3d.md) rather
than multiplying independently chosen gain, phase and delay traces. The native
modem remains the canonical receiver. The physical construction, numerical
admission rules and reception requirements below were frozen before the new
outcomes; the subsequent sections preserve executed checks and their scope.

## Physical question and literature reconciliation

Rigid translation preserves a field's Fourier magnitudes and the phase closure
of a triad of spatial modes. Real sporadic-E structures can develop and deform.
[Hysell, Bui and Larsen (2024)](https://doi.org/10.1029/2024JA033088) report
secondary structures oblique to the primary fronts and model coupled neutral
and plasma dynamics. Their coherent radar backscatter, including interference
cutouts, does not measure a one-way modem channel. [Schinder et al. (2026)](https://doi.org/10.3389/fspas.2026.1858870)
find broader irregular structure with reconstructed realistic winds than with
idealized wind shears. Their two-dimensional fluid calculation and limited wind
resolution do not determine the coefficients or time laws used here.
Their idealized instability snapshots at 125–250 s make minute-scale deformation
a relevant stress timescale, while their realistic-wind examples extend to
1,000–1,750 s. The reconstructed winds have thirty-minute resolution: those
simulation snapshots do not measure natural density changes at 144/192 s.
The events below are prescribed physical hypotheses, not measured event replays.
[Aricoche, Hysell and Morton (2026)](https://doi.org/10.3389/fspas.2026.1848834)
report roughly 20 dB fades, rapid fading missed by their inversion, and common
multiple-hop returns. Their ten-second coherent integrations and one-minute
products do not validate this event law. They reinforce the need to retain
rapid/deep F-region fading and coupled multiple-hop paths as distinct coverage
gaps. A bounded inspection of the paper's repository pointers did not identify
a downloadable calibrated complex-channel capture suitable for direct replay.

The existing [_ref synthesis](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md)
and [channel-model review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md)
distinguish observed signatures, theoretical models and field calibration.
[Gherm, Zernov and Strangeways (2005)](https://doi.org/10.1029/2004RS003093)
separate fast propagation time from slowly changing medium time. Their scalar,
weak/moderate-scattering and caustic restrictions do not validate this magnetic
3D construction. They motivate checking the actual flight/memory timescale
rather than assuming that a field constant over an RF cycle is adequate.

Here the density changes over 144–192 s, comparable to 69.6/139.2 s native bits.
Propagation is a sequence of frozen collisional Maxwell solutions assembled
into a causal time-varying filter. The imposed density history is not a solved
plasma-fluid trajectory or a general time-dependent Maxwell solution. Temporal
interpolation, numerical conditioning and medium-clock substitution require
their own checks. Static passivity alone is not an energy theorem for this
time-varying operator.

## Frozen physical construction

The primary retains field 2 from the preceding spatial experiment. The fresh
held field is a distinct moderate spatial realization, field 4; the expensive
field 3 is not silently weakened or relabeled. Its original numerical failures
and recovered grade remain in the preceding record.

| Parameter | Primary field 2 | Fresh held field 4 |
|---|---:|---:|
| RF carrier / incidence | 10 MHz / 50° | 10 MHz / 51° |
| Height / layer support / local slice | 112 km / 960 m / 80 m | 108 km / 1,000 m / 90 m |
| Peak plasma frequency / collision rate | 6.55 MHz / 1,300 s⁻¹ | 6.47 MHz / 1,720 s⁻¹ |
| Magnetic field, μT | (18,17,−43) | (20,11,−45) |
| Transmitted TM power fraction | 0.10 | 0.16 |
| Transverse periods x/y | 24/17 km | 21.5/18 km |
| Receiver y / temporal winding | 0.073 Ly / +1 | −0.093 Ly / −1 |
| x drift / spatial epoch | 8 m/s / 600 s | 7 m/s / 470 s |
| Mode (1,0), density amplitude / phase | 0.0018 / 0.45 | 0.0013 / −0.65 |
| Mode (0,1), density amplitude / phase | 0.0015 / −0.70 | 0.0011 / 0.90 |
| Mixed mode, density amplitude / phase | (1,1), 0.0012 / 1.10 | (1,−1), 0.00085 / −1.40 |
| Intrinsic event start / duration | 490 / 192 s | 838 / 144 s |

Density amplitudes use the density whose plasma frequency is 10 MHz; phases
are radians. Each positive spatial harmonic has its conjugate partner. The
event clock is `T=(output_sample−32)/1920`, referenced to the propagation
stream's capture origin, independently of its spatial drift epoch, payload,
bit duration and signal strength.

Inside the event let `u=(T−start)/duration`, `B=64u³(1−u)³` and
`Q=2(2u−1)B`; both are zero outside. Each spatial coefficient is multiplied
by `1+(a+i b)B+(c+i d)Q`. The tuples `(a,b,c,d)` are frozen as:

| Mode | Primary | Held |
|---|---|---|
| (1,0) | (−0.35,0,0,0.20) | (0,−0.38,0.18,0) |
| (0,1) | (0,−0.55,0.25,0) | (−0.42,0,0,0.30) |
| Mixed | (−0.40,0,0,0.30) | (−0.24,0,0,−0.45) |

This gives continuous onset and recovery with zero first and second derivatives
at the event boundaries. A full-envelope positivity check is required before
solving. Changing Fourier magnitudes and a translation-invariant phase closure
independently witness intrinsic deformation. The matched rigid cases retain
the same fields and motion with these factors identically one.

## Numerical admission and bounded recovery

Reuse the shared full vector Maxwell solver and its spatial order, mesh,
refined-edge, frequency, FIR and relative-voltage checks. The original rigid
table remains the baseline. Store a chronological additive residual FIR along
the actual moving trajectory, with exact zero residual outside the event.
Interpolate fixed laboratory complex filter coefficients, not eigenmode labels.

The prospective initial design has sixteen time intervals and 265 RF solves:

- The existing 29-solve rigid table.
- Fifteen new interior states, each using nine RF fit frequencies.
- Sixteen independent midpoint states, each at −384, 0 and +384 Hz.
- Four staggered states at u=3/64,17/64,43/64,59/64, each using nine RF
  frequencies for a direct FIR comparison and two off-grid RF witnesses.
- Three interior carrier states at u=.25,.5,.75 with mesh/order/refined-order
  checks, reusing their production solutions and adding nine solves.

The initial standalone numerical probe is bounded at 240 s and a 48 MiB
preparation allowance. Require midpoint/off-grid assembled voltage error and
direct FIR coefficient L1 error at most 0.0012; retain existing spatial 0.004,
edge-flux 0.00005, RF/FIR 0.0006 and discarded-energy 0.000001 gates. Charge
numerical allowances against the lowest checked actual-path voltage, with the
unchanged 0.15 relative limit. Require a checked departure of at least 0.001
complex voltage from the rigid field to establish a material exercised effect.
Chunk, tail and tone checks must cross both event boundaries.

An analytic Hermite derivative bound limits coefficient-clock substitution
over retained FIR memory plus geometric flight. Its sum with the rigid
contribution must remain at most 0.004. This bounds the implemented frozen-
coefficient surrogate; it does not bound material memory, near-cutoff dwell,
dynamic Maxwell error or unobserved between-witness interpolation error.

Only if the sixteen-interval time approximation rejects may numerical support
increase to thirty-two intervals under a 480 s probe cap. The physical field,
time law and error gates remain fixed, and the original rejection remains
evidence. A remaining numerical rejection prevents receiver admission. Passing
finite witnesses does not become a uniform continuum or field-validity proof.

## Initial pilot and review-directed minimum witnesses

The initial primary 16-interval probe completes all 265 RF solves in 73.234 s.
Its planned temporal voltage and direct-tap L1 errors are respectively
0.0000384459 and 0.0000381277. The checked mode-magnitude change is 0.000630
in normalized density, phase-closure change 0.503412 rad, and maximum complex
voltage departure from the rigid field 0.0130077. Its planned numerical gates
and three event-crossing stream checks pass. This observation retains compiled
appraisal digest `46c076e4eb8c3d8dc8d318ac3280d7c0840a00427b1ca69249094fdb7742f2c9`
and the [original receipt](local-evidence/hf-vector-evolving-preflight/primary-16.stdout.json).

Independent review identifies a missing admission check: the fixed knot,
midpoint and staggered witnesses can miss a deeper minimum created between
those times by the assembled temporal interpolation. Before any receiver run,
add a 4,097-point scan over the event at −384, −196.5, 0, +196.5 and +384 Hz,
then one direct production RF solve at each frequency's lowest-voltage time.
These five additional solves bring the full admitted design to **270 RF solves**
under the same 240 s cap. Charge errors at these minima against the unchanged
0.0012 assembled-voltage gate, and include both raw and assembled minima in
the relative-error denominator. This remains a finite targeted witness,
not a uniform physical-minimum proof. The original 265-solve pilot is retained
with its original coverage and does not admit complete receiver use.

Review also confirms that pure-TE cross-polarized flux and mixed-launch reflected
flux refer to different incident states; their numerical values need not be
ordered. Preserve that distinction rather than changing valid calculations.
The shared carrier-only API must reject active intrinsic evolution, matching
the CLI, because its background-only result would omit the changing medium.

## Admitted evolving waveforms

Both unchanged fields pass the expanded 270-solve design at sixteen intervals.
The primary takes 74.456 s and the held field 75.964 s internally; the external
process measurements are 74.498 s and 76.014 s. No thirty-two-interval recovery
is needed. [Primary receipt](local-evidence/hf-vector-evolving-preflight/primary-270.stdout.json),
[held receipt](local-evidence/hf-vector-evolving-preflight/holdout-270.stdout.json).

| Numerical witness | Primary | Held |
|---|---:|---:|
| Maximum temporal assembled voltage error | 0.0000384459 | 0.0000661877 |
| Maximum direct FIR coefficient L1 error | 0.0000381277 | 0.0000640403 |
| Minimum directly checked event-path power | 0.01053447 | 0.01199596 |
| Minimum scanned assembled event-path power | 0.01053474 | 0.01199627 |
| Relative error allowance at checked minimum | 0.0201727 | 0.00462880 |
| Additional coefficient-clock error bound | 0.0000139742 | 0.0000264042 |
| Maximum departure from rigid complex voltage | 0.0130077 | 0.0264462 |
| Maximum phase-closure change, rad | 0.503412 | 0.381515 |

The five scan minima fall at the event's return boundary for the primary and
its onset boundary for the held field. These two constructions do not produce
an unresolved deep interior null. Their explicit temporal deformation is
exercised; a successful numerical check is not evidence for an arbitrary
stronger intrinsic event. The scans and direct checks retain their sampled scope.

Both preflights use compiled appraisal digest
`4d4c58ac813c8f0dede71e36b2735e51fa5a2fbefabc4650801fb6273a50081d`
and probe binary SHA-256
`55fed79038a15707afbb56e6fc267d724ba9f448e977090213a9144a3cd2247e`.
Later reader/private-energy integration changes retain these original identities.
Complete receiver receipts independently rebuild and check the physical table
under their own compiled application identity.

## Predeclared reception requirements

The [eight-row campaign](campaigns/hf-vector-evolving-receiver.json) is fixed
before these outcomes. Its `reception_requirement` metadata is frozen with the
campaign before the native process starts and is never passed as a receiver
option. The qualification reader rejects a later mismatch or downgrade.

| Row class | Requirement | Purpose |
|---|---|---|
| Primary rigid and evolving, `C`, +20 dB-Hz | Correct complete delivery | Matched medium-evolution anchor |
| Fresh held rigid and evolving, `D`, +20 dB-Hz | Correct complete delivery | Independent field, payload and event-placement anchor |
| Primary evolving `C`, +15 dB-Hz | Characterization | Sensitivity near the previously observed received-energy scale |
| Primary evolving `C`, doubled bit duration, +12 dB-Hz | Characterization | Nearly equal incident energy; measured selected-energy comparison |
| Primary evolving noise and wrong key, +20 dB-Hz | No DATA and no accepted message | Negative controls |

The required anchors are explicit engineering requirements for these named
points, not a promise about a natural population. Historical success is not
fresh evidence. Every wanted row still requires content integrity; every row
requires full input, ordered valid scoring and bounded work, including quiet
completion. A delivery miss is recorded while the independent remaining rows
continue. Numerical rejection, invalid evidence or a time limit stop admission.

The initial receiver budget is eight rows, at most 600 s per row and 4,200 s
aggregate. Only after all four strong anchors and their numerical checks pass
may one +30 dB-Hz native clock/continuing-interferer combination run; if it
misses, one matched endpoint-off discriminator is permitted. No additional
parameter sweep is implied. These bounded CPU runs are not receiver capacity
measurements. A RunPod rental needs a demonstrated suitable native workload.

Private clean energy over each exact public observation is accumulated during
the same channel render before receiver noise. It is not supplied to the modem.
This permits decision-scale information references without repeating expensive
Maxwell solves or pooling a known complete message into a misleading per-bit
claim. Doubling duration also changes which bits encounter the fixed event;
report those exposures and selected energies rather than assuming equal
information from equal incident energy.

## What the other coverage gaps require

The low-level solver already retains full polarization/order scattering
matrices. A complete multiple-hop path additionally needs a vector handoff,
ground reflection, common physical timing, geometry and delayed returns before
the final antenna projection. Cascading the existing projected scalar channels
would lose that information. [Psiaki (2019)](https://doi.org/10.1029/2019RS006792)
describe path quantities needed for concatenation, while [Pederick et al. (2016)](https://doi.org/10.1002/2015RS005856)
distinguish ground-reflected and chordal routes and their different losses.
An unused ground-reflection component would not exercise the missing complete
path, so it is not added as apparent coverage in this increment.

The [measurement audit](hf-research.md#measured-evolution-data-and-the-channel-observable)
now includes accessible processed Es echo and coherent radar records, in
addition to the earlier long observed-voltage and ION-FAST records. These can
constrain morphology and timescales. Their backscatter amplitudes do not supply
a calibrated one-way complex modem channel or a disturbed-event reliability
distribution. The required calibration and geometry distinctions remain explicit.

## Receiver execution and original validation stop

The first campaign invocation completes the rigid and evolving primary `C`
messages in 31.157 and 95.523 native seconds respectively. Both native receipts
report all thirteen DATA bits correct and one correctly completed message.
The runner nevertheless stops after the evolving observation: its digest-scope
string still expects coefficients alone, while the strengthened native hash
also binds the time law and its parameters. This is a receipt-reader mismatch,
not an observed propagation or receiver failure. The original
[stopped run](local-evidence/hf-vector-evolving-receiver/run.json) and both raw
receipts remain unchanged. Correcting the expectation must retain rejection of
the old, insufficient digest description; it must not weaken the numerical or
message requirements. Subsequent execution and validation are recorded separately.

The corrected reader passes all 127 runner and 23 qualification/analysis checks,
including rejection of the stale digest scope. Both original raw receipts then
pass direct strict revalidation against their exact frozen episodes. Neither
original receipt nor the stopped campaign record is rewritten. A rebuilt
application and the unchanged eight-row campaign provide the subsequent full
run; the two repeated observations are validation recovery, not independent
success samples.

The corrected full run uses appraisal digest
`1510ed6d36d328cb49d638b239b9ab73e3c693f675d02c558debfb6eee5f04fc`
and application SHA-256
`b8ad3e44dfe125b2de90dd007768306ebdd567b04fec7430df9367f6a64c2b05`.
The canonical modem digest remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`;
the configured base is `a21dd79f2004f5a5b7efe64113a08f203e6e870b`, with the
contained changes bound by the appraisal digest. Native execution is on the
Ryzen 5 PRO 5650U with twelve visible logical CPUs and the default twenty-four
receiver workers. No GPU result, real-time capacity claim or RunPod spend is
attached to these CPU observations.

## Direct receiver outcomes and decision-window reference

The corrected [eight-row run](local-evidence/hf-vector-evolving-receiver-validated/report.md)
finishes in 751.512 s with no execution or receipt failures. The four required
+20 dB-Hz messages pass with thirteen correct DATA bits each; no wrong content
is accepted. Both negative controls produce zero DATA decisions and zero accepted
messages. Both weaker characterization cases retain twelve quiet active slots,
one correct DATA bit and a premature physical completion rejected by the codec.

| Wanted case | Required complete delivery | Correct DATA / active bits | Native wall s | Incident E/N0 | Selected-antenna E/N0 |
|---|---|---:|---:|---:|---:|
| Primary rigid C, +20 | Pass | 13/13 | 31.393 | 90,993.087 | 1,214.413 |
| Primary evolving C, +20 | Pass | 13/13 | 96.959 | 90,993.087 | 1,221.088 |
| Held rigid D, +20 | Pass | 13/13 | 29.824 | 90,993.284 | 1,302.368 |
| Held evolving D, +20 | Pass | 13/13 | 97.072 | 90,993.284 | 1,314.531 |
| Primary evolving C, +15 | Characterization: no delivery | 1/13 | 113.939 | 28,774.541 | 386.142 |
| Primary evolving C, +12, doubled bit duration | Characterization: no delivery | 1/13 | 151.934 | 28,784.639 | 420.932 |

These energies are dimensionless totals over the complete generated capture;
the [decision-window analysis](local-evidence/hf-vector-evolving-preflight/decision-window-reference.json)
separately uses the clean energy in each actual scored observation. Every run
accounts for observed plus outside-observation energy; the outside term happens
to be zero in these six captures. No received samples or channel state are passed
to the receiver by this private instrumentation.

Doubling actual bit duration from 69.6 to 139.2 s changes incident E/N0 by only
+0.0351% and raises total selected E/N0 by about 9.01%, but still recovers only
one bit. The fixed event overlaps public slots 5–7 in the short case and 3–4 in
the long case. Equal incident energy therefore does not imply identical channel
exposure or identical per-decision information.

At the fixed single-known-hypothesis false-alarm probability 10^-9, the exact
known-waveform/phase/amplitude presence reference gives miss probabilities
0.00403–0.17636 across active short-case windows and 0.00102–0.14840 for the
long case. The corresponding window E/N0 ranges are 23.993–37.385 and
24.789–41.248. This is a conditional information reference, not a practical
blind-receiver BER, message-delivery probability or attainable sensitivity claim.
The receiver must pay for its unknown timing, channel and phase hypotheses.

Most weak-case misses lie outside the intrinsic event. The saved
[matched +20 evidence comparison](local-evidence/hf-vector-evolving-preflight/outside-event-evidence.json)
finds exactly equal clean window energy, bit/presence evidence, thresholds and
decisions for every outside-event window in each rigid/evolving pair. A second
rigid weak replay would not discriminate the identical pre-event samples.
Instead, a [single frozen +17.5 midpoint](campaigns/hf-vector-evolving-midpoint.json)
narrows the five-dB whole-message bracket once, with unchanged physics, key,
noise seed and calendar. It remains characterization, with no further sweep
regardless of outcome. The four passed anchors also admit the prospectively
specified [native clock/interference case](campaigns/hf-vector-evolving-chain.json).

The admitted +30 dB-Hz [combined case](local-evidence/hf-vector-evolving-chain/report.md)
executes in 203.725 native seconds. All thirteen active DATA bits are correct,
but both ending quiet slots are ambiguous and no message completes. Its frozen
`required_complete` gate therefore **fails**, even though bit reception succeeds.
The paired endpoint-Off discriminator is triggered by that result, as specified
before execution. Switching clocks, RF offsets and tone together distinguishes
the combined interaction; it cannot by itself assign fault to one of them.
The [matched endpoint-Off observation](local-evidence/hf-vector-evolving-chain-off/report.md)
finishes in 199.568 native seconds and delivers the complete `C`, with thirteen
correct DATA bits and both ending slots scored quiet. Public search bounds,
physical field and event, payload, noise seed and incident C/N0 are retained.
This reproduces the unresolved equipment/ending interaction in an intrinsically
evolving field; it is not evidence of a new deformation-specific defect.

The [single midpoint](local-evidence/hf-vector-evolving-midpoint/report.md)
executes in 102.729 native seconds and recovers eleven of thirteen bits. Public
slots 7 and 8 are scored quiet; this splits the message, and both resulting
fragments are rejected by the codec. There is no accepted incorrect payload.
For this fixed realization, the tested whole-message bracket is now
**+17.5 dB-Hz failure / +20 dB-Hz success**, rather than five dB wide. This one
observation does not measure a transition probability or a universal floor.

The two missed windows contain E/N0 43.365 and 42.665. Their conditional
known-mean presence miss probabilities are 0.000458 and 0.000598 at the declared
false-alarm constraint. Native bit evidence favors the correct bit by 27.143
and 27.844 log units, but operational presence evidence is 19.281 and 19.847,
below the 21.493 admission threshold. Both energy-based quiet checks pass.
This locates the immediate mechanism in presence admission and physical ending,
not a wrong binary choice. The private reference grants information unavailable
to the blind receiver, so the gap alone does not prove that lowering its threshold
is safe or that a practical receiver attains the known-mean limit. Negative
controls and complete-message checks remain necessary for any proposed repair.
[Exact midpoint reference](local-evidence/hf-vector-evolving-preflight/midpoint-decision-reference.json).

Only the new midpoint's CLI/orchestration admission and the routine test
registration change before the final rebuild; propagation and native receiver
algorithms are unchanged. Its appraisal digest is
`d6956c3ecdb8be99be129ed8a9001cf6f3068ab19adc9bf1139a0ed4158db847`,
application SHA-256
`a0ac71e1081c619fe25cf9400fc190124ca3792ce33adf8849428645637ad258`.
Earlier receipts retain their exact previous identities and are not relabeled
as executions of this final binary.

The suite now registers `high_fidelity_hf_vector_evolving_waveform`, using the
existing `evolving-3d` native probe with a 240 s timeout. It exercises the
chronological residual, direct minimum witnesses and event-crossing stream as
a routine named check. The held field's full solve stays in the bounded campaign
rather than being duplicated in every routine check. The new group passes in
75.04 s; native integration, runner and qualification groups also pass in the
[final four-group run](local-evidence/hf-vector-evolving-preflight/ctest-final-affected.log)
(140.78 s total). Together with the earlier complete 39-group run, this gives
applicable full-plus-affected coverage of the forty registered groups, not a
claim that one final forty-group invocation was run.

## Finite qualification and preservation of earlier failures

The existing [finite-domain specification](qualifications/hf-vector-domain.json)
now contains 43 cells: the original 32 objects unchanged, plus these eleven
unique observations. The [current report](local-evidence/hf-vector-evolving-preflight/qualification.json)
validates all 43 receipts (42 strict originals and the previously documented
single metadata-amended historical receipt). Numerical and bounded-execution
requirements pass throughout. **Overall qualification remains failed.**

Five of six predeclared delivery anchors deliver; the combined clock/tone
anchor does not. The old integrity and false-DATA failures also remain. Across
all 36 wanted observations, fourteen deliver and twenty-two do not; these are
descriptive mixed historical/current observations, including characterization,
not independent success trials or environmental probabilities. All seven
negative-control observations remain separate from wanted delivery counts.

The first expanded audit catches a reader regression: legacy held fields 1 and
3 had been mapped to primary geometry by the new held-field predicate. Five
old receipts then failed metadata validation. Preserve that
[original failed audit](local-evidence/hf-vector-evolving-preflight/qualification-reader-regression.json).
Restoring the established held descriptors and adding independent literal
expectations for all fourteen legacy vector scenario IDs repairs the reader.
The original 32 gate counts again match the preceding audit exactly: nine
deliveries/eighteen misses/five negatives, one integrity failure, and one failed
false-DATA control. No historical receipt, requirement or physical field is
changed to obtain that result.

The final Python-only correction passes **128 runner and 23 qualification
checks** ([log](local-evidence/hf-vector-evolving-preflight/reader-final-tests.log)).
It does not rebuild or relabel native executables. The report's actual target
binary hash matches `a0ac71e1…`; only the midpoint has that exact compiled
appraisal identity. Ten new and all thirty-two older observations retain
historical applicability under the strict hash rule, even though the canonical
modem source is unchanged. Passing named historical anchors is therefore not
reported as full qualification of the latest executable or checkout.

The report SHA-256 is
`c7238522f5ff206413d770d314e34b3b2d86ad4b2fc036fd17fdf2da428b0d57`;
the specification SHA-256 is
`c2245a27546b1b917d0cc993baf9f920e782db9f463b4f6d8ebfb68473dab33e`.
The post-build receipt reader is
`f482b46de1184b10435ec5c17d09b228af6aa41996204398baa21b58d783afe5`;
the qualification reader is
`4b34caae1f3e22d0710ce9d5453e391ece4b83216682b000b6ce789a52e2f723`.
These external-reader identities are distinct from the embedded native build
identity above. Missing calibrated observations, coupled multiple-hop paths,
rapid/deep fading populations and broader operating requirements remain explicit
missing strata rather than implicit passes.

## Evidence lifecycle

Keep frozen inputs, rejected numerical attempts, admitted preflights and complete
receiver receipts under `local-evidence/hf-vector-evolving-*` while they support
this capability, unresolved failures or required regressions. Distill and remove
superseded intermediate success logs. Reuse the shared native implementation
and existing build; do not retain another modem or a copied source tree.
Superseded successful compile/unit logs and duplicate preflight plans have been
removed; compact admission audits and the complete scored receipts retain the
actual geometry and identities. The stopped original campaign, numerical pilot
and validation-regression report remain because they explain failed checks and
the limits of later recovery. None of these artifacts licenses a general
disturbed-day reliability or computational-ceiling claim.

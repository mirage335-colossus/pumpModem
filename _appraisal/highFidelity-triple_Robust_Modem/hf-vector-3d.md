# Two-transverse-coordinate vector HF tests

This continuation adds a second transverse density coordinate to the existing
bidirectional Maxwell calculation. A single cold collisional dielectric tensor
couples the same density field to diffraction, reflection and polarization.
The construction is periodic in x and y and layered in z. It is a three-dimensional
spatial field with a prescribed slow rigid drift, not a general evolving or
multiple-hop ionosphere.

## Why this increment and what can falsify it

The previous [vector experiment](hf-vector-oblique.md) was invariant in y.
Noncoplanar scattering and its interaction with magnetic polarization were
therefore absent. A native Fourier formulation for crossed anisotropic media
is supported by [Li (2003)](https://doi.org/10.1088/1464-4258/5/4/307) and
[Popov and Nevière (2001)](https://doi.org/10.1364/JOSAA.18.002886).
The same finite scattering machinery can retain both transverse indices;
there is no independent scalar fade or polarization trace multiplied afterward.

The cost scales with the cube of retained spatial mode count. Taking a Cartesian
product of the earlier 49 x orders would produce 2,401 spatial modes and is not
an economical next test. The frozen pilot instead uses 25, 49 and 81 modes for
coarse, production and refined calculations. Its perturbations are deliberately
moderate; passing this case cannot qualify all strong or small-scale scattering.
The initial grade has a 1.2-billion dense-term cap per RF solve and a conservative
32 MiB preparation allowance. The original 2-D branch keeps its earlier limits.
These are implementation admission limits, not CPU-capacity measurements.

[Smith et al. (2020)](https://doi.org/10.1029/2020JA028201) show why spatial
orientation and much smaller HF scattering structures can matter. Their
short, frozen-density, collisionless calculation does not validate this slow-time
model or provide a calibrated communications channel. Likewise,
[Poker Flat morphology fits](https://doi.org/10.3390/s23052477) concern three
GNSS scintillation events, and the [reported 5 km echo-clustering scale](https://doi.org/10.1029/2022JA031233)
is not a measured electron-density periodic wavelength. The present periods,
harmonics and phases are explicit experimental hypotheses. The source and
estimator cautions in the [local skywave research](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md)
and [source-bound scenario specification](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md)
remain applicable; unrelated measured extremes do not define a natural joint law.

[Schinder et al. (2026)](https://doi.org/10.3389/fspas.2026.1858870)
provide a consequential counterpoint: realistic reconstructed winds produce
irregular broad spectra, unlike idealized periodic wind-shear structures.
Their two-dimensional fluid model and its observation limits do not supply a
calibrated modem channel. The [research reconciliation](hf-research.md#intrinsic-evolution-versus-rigidly-translated-spatial-fields)
therefore retains intrinsic evolution and broader spatial spectra as distinct
requirements; adding y dependence does not silently satisfy them.

## Fields fixed before numerical or receiver outcomes

Both fields retain the corresponding earlier cos-squared Es background and
flat corrugated slice. Positive harmonics below are in units of the electron
density whose plasma frequency is 10 MHz; their negative partners are complex
conjugates. Positivity is checked before solving. The mixed harmonic and two
noncollinear axial harmonics cannot be reduced to one transverse coordinate.

| Parameter | Primary (field 2) | Holdout (field 3) |
|---|---:|---:|
| Carrier / incident angle | 10 MHz / 50° | 10 MHz / 53° |
| Height / layer support / local slice | 112 km / 960 m / 80 m | 104 km / 1,200 m / 100 m |
| Peak plasma frequency / collision rate | 6.55 MHz / 1,300 s⁻¹ | 6.12 MHz / 2,450 s⁻¹ |
| Lx / Ly | 24 km / 17 km | 20.5 km / 18.5 km |
| Receiver y | 0.073 Ly | −0.112 Ly |
| Magnetic field (x,y,z), μT | (18,17,−43) | (−11,25,−46) |
| Incident TM power fraction | 0.10 | 0.23 |
| Harmonic (1,0), magnitude / phase | 0.0018 / 0.45 rad | 0.0016 / −1.05 rad |
| Harmonic (0,1), magnitude / phase | 0.0015 / −0.70 rad | 0.0019 / 0.60 rad |
| Mixed harmonic, magnitude / phase | (1,1), 0.0012 / 1.10 rad | (1,−1), 0.0011 / −0.20 rad |
| vx / trajectory winding w | 8 m/s / +1 | 7.5 m/s / −1 |
| Initial epoch | 600 s | 520 s |

The planar control removes every transverse perturbation. The in-plane control
removes only terms with n≠0, retaining the same magnetic field, background,
antenna and axial harmonic. Out-of-plane reflected/transmitted flux and the
projected-voltage change from this one-axis control are explicit witnesses of
what the extra coordinate contributes.

For the prescribed rigid drift, vy/Ly = w vx/Lx. After solving and checking the
full spatial spectrum, applying the propagation and receiving-position phases,
and projecting the full lab electric field onto the antenna, each order has
temporal index k=m+w n. Combining equal k is exact along this declared path.
It permits reuse of the native 257-knot causal FIR stream. Minima, interpolation
bounds and duration comparisons concern this trajectory, not all x/y positions.
The maximum 2,400-second capture stays shorter than a full 3,000-second primary
or approximately 2,733-second holdout traversal; no wrapped recording is used.
Fields named `period_averaged_*` are coherent averages along this trajectory
after combining equal temporal indices. They are not spatial averages over the
whole x/y periodic cell. Carrier minima and certified finite-series lower bounds
have the same trajectory-only scope.

## Numerical and communications protocol

Cheap checks precede carrier pilots: vacuum and planar Fresnel limits, exact
one-transverse-coordinate reduction, whole-coordinate rotation, vector Born
scattering, two-axis translation and full coherent-input passivity. A general
unmagnetized three-dimensional field can mix local TE/TM labels through geometry;
zero cross-polarization is required only in the appropriate in-plane symmetry
limit. All four scattering blocks and longitudinal electric fields are retained.

The carrier pilot uses ±2/±3/±4 orders per axis and 3 m/1.5 m vertical refinement.
The existing voltage, edge-flux, passivity and solve-residual gates remain fixed.
A pilot may use one extra in-plane RF solve for its ablation witness. Each initial pilot
has a 120-second wall limit; an initial full table has a 600-second limit. Rejected
approximations remain recorded. Physical fields and tolerances must not be
weakened after observing failure merely to produce a receiver result.

Full admission retains 29 RF solves, 13 independent RF-frequency witnesses,
2,048 trajectory witnesses, 54 assembled-table comparisons and actual impulse,
tail, moving-tone and chunk checks. Spatial order/refinement/edge checks occur
before temporal folding. The ordinary canonical receiver receives the waveform
without physical coefficients or transmitted bits supplied to acquisition.

The [frozen ten-row campaign](campaigns/hf-vector-3d-receiver.json) selects two
primary C/N0 points, two independent-field/payload points, planar and in-plane
controls, a doubled-duration/−3 dB case, strong noise/wrong-key controls and one
bright native clock/interference interaction. Its initial allowance was 3,600 seconds,
300 seconds per direct row and 600 seconds for the endpoint row. The numerical
recovery below changed only the two held-field row caps to 900 seconds and the
aggregate allowance to 4,800 seconds before any receiver outcome. If primary +20
misses, one +30 primary point is permitted. If the bright combined endpoint
misses, one matched endpoint-off +30 discriminator is permitted. No extra sweep
is implied. Numerical rejection, censored execution, accepted wrong content,
missing delivery and complete success remain different outcomes.

The duration pair preserves approximately equal incident E/N0, not necessarily
received information: a longer exposure traverses a different part of the field.
Record both incident and selected-antenna energies and complete unsuccessful
receiver work. These selected observations do not estimate an environmental
success rate or a real-time processor ceiling.

## Numerical support recovery before receiver trials

The fixed stronger field did not fit the initial diffraction grid. Two larger
carrier-only comparisons were admitted in sequence, each with a 300-second cap,
60-billion dense-term limit per RF solve and a 256 MiB preconstruction allowance.
They preserve the physical field and every accuracy threshold; only numerical
support increases. The second comparison ran only after the first rejected.

| Held-field production/refined half-orders per axis | Projected spatial L1 difference | Maximum outer-order flux | Carrier result / wall time |
|---|---:|---:|---|
| 3 / 4 | 0.039034 | 0.0094224 | Rejected / 1.94 s |
| 5 / 6 | 0.0080377 | 0.00029814 | Rejected / 15.60 s |
| 7 / 8 | 0.0012536 | 0.0000084884 | Passed / 93.60 s |

The unchanged gates are 0.004 for the spatial voltage difference and 0.00005
for outer-order power. The successful carrier also has a 0.00030739 mesh
difference and 31.7615% of incident flux reflected into nonzero-y orders.
Its maximum RF work is 40,786,430,702 dense terms and its conservative live
workspace quote is 175,245,312 bytes. These are simulation costs, separate
from the receiver's search cost and ceiling.

This supports a grade-7 full-table attempt, with grade-6 and grade-8 comparison
solves. The primary and its controls retain grade 3. Only the exact active
held-field specification receives the larger 256 MiB/60-billion-term allowance.
All 29 RF solves, 54 independent assembled-table witnesses and actual stream
checks remain required. The carrier pass alone does not admit a waveform.
The original failed grade remains reproducible with `holdout-3d carrier`;
`holdout-3d carrier-wide5` and `carrier-wide7` reproduce the numerical ladder.
The plain `holdout-3d` full-table command selects the recovered grade.

## Broadband numerical outcome

The recovered held field passes the full table and stream probe in **636.113 s**:
29 RF solves, 54 assembly witnesses, refined spatial voltage difference
0.00125763, maximum outer-order flux 0.00000855655 and mesh difference
0.000308760. The worst checked-band relative voltage allowance is 0.4147%.
Assembly error is 0.00000537046; the moving-tone reference differs by
8.06e-15 and chunk output agrees exactly. The maximum per-RF work remains
40,786,430,702 dense terms. [Full held-field receipt](local-evidence/hf-3d-preflight/holdout-full.stdout.json)

This preflight used appraisal digest
`13d0939a0e59743f22de0d24f120309d176f202f7b5e11c18761003f5f77bd7b`.
Subsequent integration review added an explicit refined-edge admission
predicate (all three saved refined edges already satisfy it), the held
scenario's 320 MiB caller allowance, and target-binary freshness checking in
the qualification reader. These do not alter the calculated field coefficients;
the original preflight retains its own identity. Receiver receipts bind the
rebuilt application independently and require its full numerical checks.

The primary full table passed in 9.797 s, with a 0.001977 spatial-refinement
error and 0.000009542 outer-order flux. Replaying the legacy primary numerical
probe reproduced its archived numerical observables; the larger table object
adds 112 bytes. This is equality of the reported witnesses, not proof that an
old receiver receipt belongs to the new binary.

## Complete receiver observations

The ten frozen rows and the one conditionally authorized endpoint-off row all
finish within their declared budgets. Every receipt contains all 15 planned
scored slots and the full input horizon, with 24 receiver workers on the local
Ryzen 5 PRO 5650U (12 logical CPUs, approximately 15 GiB visible memory).
The initial campaign takes 1,834.72 s and the follow-up 143.04 s. These are CPU
runs; no RunPod instance or GPU was used. The physical-field preparation is
included in trial wall time and is not receiver search cost.

| Frozen episode | Incident C/N0, dB-Hz | Complete message | Active quiet slots / ending decisions | Trial wall / field preparation, s |
|---|---:|---|---|---:|
| Primary `A` | 0 | None | 13 / quiet, quiet | 49.65 / 9.87 |
| Primary `A` | +20 | Correct `A` | 0 / quiet, quiet | 30.69 / 9.93 |
| Independent held field `B` | 0 | None | 13 / quiet, quiet | 694.81 / 654.48 |
| Independent held field `B` | +20 | Correct `B` | 0 / quiet, quiet | 669.81 / 647.86 |
| Planar control `A` | +20 | Correct `A` | 0 / quiet, quiet | 23.59 / 2.70 |
| In-plane control `A` | +20 | Correct `A` | 0 / quiet, quiet | 26.96 / 5.01 |
| Longer primary `A` | −3 | None | 13 / quiet, quiet | 90.79 / 9.90 |
| Primary wrong key | +20 | No message or DATA | Not applicable / quiet, quiet | 52.20 / 9.86 |
| Primary noise only | +20 | No message or DATA | Not applicable / quiet, quiet | 49.81 / 9.88 |
| Native clocks + continuing tone, `A` | +30 | None; all 13 DATA bits correct | 0 / ambiguous, ambiguous | 145.13 / 10.12 |
| Matched actual endpoint effects Off, `A` | +30 | Correct `A` | 0 / quiet, quiet | 142.89 / 10.02 |

There are **five correct deliveries and four misses among nine wanted
observations**, with no incorrectly accepted content in these eleven new
observations. The two negative controls produce no DATA. These are selected
brackets, ablations and controls, not independent trials of a natural population.
The [receiver audit](local-evidence/hf-3d-preflight/receiver-audit.json) preserves
the row hashes, energy planes, outcomes and costs; original frozen inputs and
receipts remain in [the first run](local-evidence/hf-3d-receiver/run.json) and
[the triggered follow-up](local-evidence/hf-3d-endpoint-off/run.json).

The native endpoint pair repeats the earlier completion failure in a genuinely
two-transverse-coordinate field: correct DATA alone does not deliver a message
when both ending apertures remain ambiguous. Its Off control removes actual
clock/oscillator errors and the continuing tone together while retaining the
public search settings. It establishes the combined effect, not separate
clock-versus-interferer causation. It must remain a complete-message regression
when a quiet-decision repair is proposed; simply forcing ambiguous slots quiet
would need renewed fragment and false-message checks.

The On case uses TX audio/RF errors +1/+0.01 ppm and RX audio/RF errors
−1/−0.01 ppm, all within the retained public hypotheses. Its continuing RF
interferer has amplitude 0.3 at +100 Hz. The Off case zeroes these effects.
Source generation, noise seed 81501, variance 1.92, physical table, public
calendar and receiver bounds compare exactly equal. Received IQ is necessarily
different. Propagation also runs through the continuing interferer in the On
case; Off stops at the source tail and pads the remaining observation. Both
score the same 2,554,173-sample capture and fifteen public slots.

The longer case requests 128 s instead of 64 s per bit; the actual native
durations are 139.2 s and 69.6 s respectively. Its incident energy/noise
ratio is 1.000349 times the shorter case, but selected-antenna energy/noise is
1.093001 times as large because the exposure changes. Capture duration grows
1.683765 times and measured total wall time 1.828473 times. Both cases miss;
this pair demonstrates neither a sensitivity recovery nor a longer-is-worse
cliff. For each field's 0/+20 bracket the clean signal and physical table are
identical and noise variance changes by exactly 100. The new fields do not
contain the earlier deep antenna null: primary trajectory carrier power ranges
0.01074–0.01875 and held power 0.14417–0.19541 relative to incident power.

## Information reference at the correct observation scale

[`analyze_vector_presence.py`](analyze_vector_presence.py) reads the already
validated direct white-noise receipts without rerunning the simulator or modem.
It verifies the frozen episode, complete horizon and unmodified additive-noise
plane. E is the sum of clean received squared magnitude after antenna projection
and before noise; nu is E[|n[k]|²] per complex sample. It is not incident energy,
energy per bit or the distance between alternative messages.

For one exactly known full-message mean in proper-complex white Gaussian noise,
the optimum presence test at the illustrative single-hypothesis false-alarm
probability alpha=1e−9 has
`Pmiss = Phi(Phi^-1(1-alpha) - sqrt(2*E/nu))`.
This conditional reference knows payload, channel, timing, phase and amplitude.
Its constraint is not the native search threshold, and it does not include
unknown-message search, bit decisions, quiet completion or multiple testing.

| Weak characterization row | Whole-horizon E/nu | Conditional whole-message presence miss probability |
|---|---:|---:|
| Primary 0 dB-Hz | 12.14424 | 0.85757 |
| Held field 0 dB-Hz | 151.02573 | 2.5756e−30 |
| Longer primary −3 dB-Hz | 13.27368 | 0.80105 |

The tiny held-field whole-message value must **not** be interpreted as an
established practical receiver sensitivity gap. The receipt has thirteen
disjoint active observation intervals, which the script checks against the
public plan and full horizon. Nonnegative energy implies at least one interval
has E/nu no greater than 151.02573/13 = **11.61736**. At the same illustrative
per-test alpha, even a detector given its exact mean has miss probability at
least **0.880515**. Whole-message pooling of thirteen known bits can therefore
change the result substantially. This is an observation-scale bound, not a
measurement of any particular slot's clean energy, the native error probability,
or a universal message-decoder bound. Existing receipts retain only aggregate
clean energy; native noisy slot evidence is not substituted for it.

This check guards against diagnosing every weak miss as receiver mismatch or
calling an optimistic known-payload reference an implementable gain. The desired
graceful unknown-message sensitivity/compute behavior still requires matched
decision-scale references and public-search costs. The current calculations do
not establish superiority to PSK31, Olivia, FT8 or WSPR.

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/analyze_vector_presence.py \
  --run-directory _appraisal/highFidelity-triple_Robust_Modem/local-evidence/hf-3d-receiver \
  --output _appraisal/highFidelity-triple_Robust_Modem/local-evidence/hf-3d-preflight/known-message-presence.json
```

## Executable qualification requirements

[`qualification.py`](qualification.py) reads the
[explicit finite-domain specification](qualifications/hf-vector-domain.json)
without running DSP. Each cell binds the exact campaign bytes and canonical
campaign hash, episode, frozen executable hash, source/build identity and raw
receipt hash. The existing sole metadata amendment is admitted only by its
own hash and exact old/new JSON field. The original invalid-receipt and stopped
campaign status remain visible.

The observations and predicates are separate: numerical admission, complete-message
delivery, accepted-content integrity, negative-control false DATA, and completion of all
planned input and scored slots within declared time/sample/workspace limits.
Each wanted cell states whether delivery is an operational requirement or a
characterization outcome. These sensitivity campaigns did not predeclare
guaranteed-success anchors, so all their wanted cells consistently remain
characterization; successful bright cases are not promoted retrospectively.
Every miss stays in the all-wanted delivery summary, and delivery qualification
remains incomplete without declared anchors. A miss alone does not establish
either a receiver defect or an information-limited channel.
A completed campaign cannot pass these predicates merely by executing. A
historically valid receipt cannot silently qualify a changed application;
source/binary applicability is a separate required dimension. The reader hashes
the explicitly named target binary on each evaluation; a changed or missing
binary makes applicability incomplete. It does not certify an unbuilt checkout.
A passed bounded
execution check is not a real-time processor-capacity measurement.

The initial audit of the 21 archived vector observations validates all 21
receipts and their numerical/full-work evidence. Among 18 wanted observations,
4 deliver correctly and 14 fail delivery (13 non-deliveries and one wrong accepted
payload). One accepted-content integrity gate and one negative-control
false-DATA gate fail. All 21 are valid historical evidence but have unverified
applicability to the current application binary. No source equivalence is
assumed merely because the canonical native digest is unchanged.

The initial reader incorrectly treated every wanted sensitivity point as a
required delivery target. Review corrected this before the final audit: the
original four successes and fourteen delivery failures remain, but a miss at a characterization
point does not become a newly invented operational requirement. Integrity and
false-DATA failures remain independent failed requirements.

The final [32-cell report](local-evidence/hf-3d-preflight/qualification.json)
adds the eleven new observations. All 32 receipts, numerical gates and bounded
execution gates validate (31 original strict receipts and one explicitly
audited metadata amendment). The 27 wanted observations contain nine correct
deliveries and eighteen failed deliveries, including the historical wrong
accepted payload. Integrity is 31 passed / one failed; the five negative
controls have four passed / one failed false-DATA gate. Current binary
applicability is eleven passed / twenty-one historical unverified. There are
no predeclared required operational delivery anchors, so that qualification
dimension remains incomplete. Overall status is **failed**, independently of
those missing requirements, because integrity and false-DATA failures remain.

The broader requirements retain explicit missing strata: calibrated broadband
complex-channel observations from independent disturbed events, multiple-hop
coupled propagation, rapid intrinsic plasma evolution, and wider independent
3D field/hardware/interference/acquisition combinations, and predeclared delivery
anchors with meaningful operating requirements. The report supplies no
natural-condition reliability probability. Passing named deterministic cases
would establish only those cases; missing strata cannot become passes.

Run from the repository root:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/qualification.py \
  --spec _appraisal/highFidelity-triple_Robust_Modem/qualifications/hf-vector-domain.json \
  --output _appraisal/highFidelity-triple_Robust_Modem/local-evidence/hf-3d-preflight/qualification.json
```

Exit 0 means all declared requirements pass; exit 1 means failed or incomplete
qualification, and exit 2 means an invalid specification. The expected exit for
this domain is currently 1. Unit tests include an explicit all-gates-pass
synthetic case, characterization misses, required delivery misses, stale target
binaries, failure controls and missing evidence; they do not supply any
communications observation.

The [native exact-prefix witness](hf-fragment-integrity.md#exact-native-input-ambiguity-under-an-outage)
adds a separate theoretical constraint: under a constructed complete outage,
the receiver cannot distinguish a legitimate short message from the identical
prefix of a continuing longer transmission at the short completion deadline.
This is a demonstrated lack of distinguishing input, separate from receivers
that overlook useful weak-signal evidence.

## Evidence lifecycle

The eleven new receiver receipts bind canonical native digest
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`,
compiled appraisal digest
`b8a57e7bcb261234ab2e416963a9033aeb4fdcd8e8479ba790cead90e439c956`,
and executable SHA-256
`7584dfc7797b5b0838c68edcf9006b7f9f7862de240bad7d577988417ab205f6`.
The GNU 14.2 Release build uses OpenSSL 3.5.7; runtime details are in the
[local host receipt](local-evidence/hf-3d-preflight/local-runtime.json).
All **39 CTest groups passed in 108.77 s** before subsequent Python-only
requirement-wording and scalar-reference changes. The affected qualification
group was then rerun and its **17 checks passed**, including overlap rejection,
incomplete-aperture rejection and the observation-scale bound. These later
analysis edits do not reidentify the already compiled native binary. A future
rebuild must pass the target-binary applicability gate again.

Keep the rejected numerical pilot, admitted final witnesses, frozen inputs and
complete receipts under `local-evidence/hf-3d-*` while the numerical capability
and resulting communication failures remain under investigation. Remove
superseded successful intermediate pilots after their useful conclusions are
reconciled here. Use the existing build/cache and shared native solver; retain
no duplicate modem or copied source tree. Public ION-FAST archive inspection
retains concise metadata, code-as-text and calibration findings only; the
notebook was not executed and raw capture files were not downloaded.

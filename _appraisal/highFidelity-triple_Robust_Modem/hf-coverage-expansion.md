# HF physical and operating coverage expansion

Frozen before new native outcomes, 14 November 2026. This continuation addresses
the gaps retained by [the independent ensemble](hf-ensemble.md): few physical
parameter combinations, no actual sample-clock errors in the detailed channel,
incomplete correspondence to GUI settings, and no coupled cutoff/absorption
reference. It does not treat a finite campaign as qualification of most natural
ionospheres or all allowed GUI configurations.

## Three distinct model layers

1. **Parameterized detailed ionosphere.** `hf-ensemble-design` retains the
   coupled vertical cold-plasma eikonal, now with eight independently declared
   physical coefficients and independent knot/placement/noise controls. The
   entire coefficient box shares one public receiver envelope; no realized
   trajectory narrows that envelope. An eight-point midpoint Latin-hypercube
   design plus static and maximum-rate anchors replaces a collection of
   individually selected examples. The generator imports no receiver bank or
   outcomes. This remains an unmagnetized, guarded, constructed channel.
2. **Canonical GUI operating path.** `hf-gui-native` uses the actual native
   `validate_settings` / `prepare_profile`, `StreamingChannel`, and
   `CompleteStreamReceiver`. This tests real oscillator/sample-clock
   perturbations, configured bounds and physical quiet completion through the
   canonical simulator. Its simplified channel is not relabeled as a measured
   ionosphere. It is a separate renderer from the detailed eikonal: stacking
   them would put transmitter clock errors after propagation and duplicate
   channel/tail effects. Their combination remains unqualified.
3. **Static collisional full-wave reference.** A native scalar, normal-incidence,
   B=0 scattering calculation derives reflection, transmission and absorption
   jointly from density and collision profiles. Independent analytic and mesh
   checks are required. This tests a previously missing physical component,
   including reflection transition, without relaxing the eikonal cutoff guard.
   It is not an evolving waveform channel, magnetoionic O/X calculation,
   three-dimensional caustic model or communications trial. See
   [the full-wave methods and gates](hf-fullwave.md).

The [research reconciliation](hf-research.md) distinguishes recent observation,
numerical theory and this implementation. No newly available long calibrated
complex disturbed-HF record was identified; the finite Ding record is not
extended by repeating or stretching it.

## Frozen physical domain and sampling

The design uses RF 3.4 MHz, a 245 km bottom / 52 km half-thickness parabola and
nominal critical frequency 3.73 MHz. Primary, secondary and knot amplitudes are
each 0–20 kHz. Primary period is 110–480 s; secondary period 480–1400 s; knot
spacing 70–200 s; both phases span −π to π. Physical placement is 0–1200 s.
The fixed public envelope is 1–3 ms arrival, ±1 Hz center-frequency allowance,
and 80 μs delay excursion. Native global-extrema/rate tests must validate the
entire box at a conservative 384 Hz occupied-band allowance before trial runs.

`coverage_design.py` freezes one permutation of eight bins per axis by sorting
SHA-256 keys with design seed 20261114. Periods and knot spacing use logarithmic
bins; amplitudes, phases and placement use linear bins. Each axis has all eight
bins represented, but each pair of axes has only eight of 64 joint-bin cells:
that is explicit sparse pair coverage, not a continuous worst-case guarantee.
The generator can check its immutable manifests and audit status without
discarding censored, unavailable, failed or unexecuted rows.

- [`hf-domain-screen.json`](campaigns/hf-domain-screen.json): eight design points
  followed by static and maximum-rate anchors; complete slot-1 observations at
  requested 128 s and 0 dB-Hz, selected public timing, unchanged phase receiver.
  Root/noise seeds are paired across trajectories, with separate knot seeds.
  Ten trials, 100 s each, 900 s aggregate, eight million samples each.
- [`hf-domain-receiver.json`](campaigns/hf-domain-receiver.json): three fixed
  design points, a fresh independently hashed parameter/placement/knot/noise
  holdout carrying `A`, plus noise and wrong-key controls. Six complete native
  receive trials, 180 s each, 1080 s aggregate, eight million samples each.

No heterogeneous design points are pooled into success-probability intervals.
Additional noise seeds are not additional ionospheres. All proposed points and
their physical admission or resource outcomes remain in the coverage account.

## Canonical GUI strata and semantics

“Each 65%” selects separate slow and fast modulation depths of .65 under the
canonical GaussianErf law; it is not a quantile, reliability percentage, or a
joint ±65% received-envelope bound. HF Worst begins at 7 ms / 30 Hz and Margin
3 expands public/path supports to 21 ms / 90 Hz. Actual endpoint oscillator
draws remain within ±accuracy/2 per endpoint rather than being multiplied by
Margin. `gui_clock_edge` deliberately selects opposite endpoints of that
allowed interval; it is an explicit boundary realization, not a random GUI draw.

The contained bridge fixes the declared waveform controls at rate 120, added
symbols 1, RF 3.4 MHz, and independent RF/audio oscillators. It uses 1920 Hz
samples / 500 Hz DATA IF without Helper and 6000 / 1725 with Helper. The
synthetic canonical stream, not an audio device, is exercised. Exact controls,
realized TX/RX ppm, diffusion, timing, source bounds and source identity belong
in every receipt. Requested GPU work must actually execute or be unavailable.

[`hf-gui-admission.json`](campaigns/hf-gui-admission.json) freezes seven cheap
planning rows, including the broad-arrival auroral corner. Admission and public
geometry are checked before expensive receiver work. A plan is not reception.

[`hf-gui-coverage.json`](campaigns/hf-gui-coverage.json) freezes eight native
receive rows: a true Off baseline using the canonical target-C/N0 planner;
HF Worst / Each 65% at Margin 1 and 3 with actual clock-edge errors; a matched
lower-C/N0 contrast; Helper; a noise control; default broad arrival; and
Helper/noise. Except for Off, these explicitly override active bit duration to
2 s. Target C/N0 stays 32 dB-Hz while actual C/N0 is varied independently.
Fixture overrides do not qualify the nominal GUI planner's sensitivity.
Caps are 120 s per row, 960 s aggregate and eight million samples per row.
Unsuccessful acquisition and physical quiet opportunities remain real work.

The experimental phase supplement is deliberately unavailable on this GUI
bridge. The canonical receiver still executes its complete supported path;
that does not qualify the restricted phase supplement over GUI bounds. Native
CLI availability checks and strict runner rejection of incompatible flags
remain separate from successful communication results.

## Intermediate references and conditional follow-ups

The existing physical diagnostic already accumulates independent complex-gain
partitions at .25, 1, 4 and 16 s plus the whole aperture. Emit those existing
statistics for physical and selected public templates. They include finite-rank
and gain-prior costs and can expose the price of lost long coherence. Public
winning cells are selected from the noisy data, so these diagnostic partition
scores are not calibrated post-selection receiver evidence. Neither they nor
the private exact-channel binary reference are counted as native decisions.

Retain the prior long-TID native slot-0 miss as one matched full-timing physical
diagnostic, now with partition results. Its received-IQ digest and original
whole-aperture scores must remain unchanged. Then allow at most two new physical
diagnostics: choose the first actual missed/wrong active native slot in frozen
receiver-manifest order, one per trajectory. If no native wanted case fails,
use the first two wanted shadow-reference misses in screen order instead.
Use full public timing and retain the native-versus-shadow distinction.
These three component rows each cap at 100 s / eight million samples.

For the first new native wanted failure, allow at most two complete operational
brackets: +3 dB at fixed requested duration and twice requested duration at
−3 dB relative to the original. Keep source/trajectory/noise identities and
launch fixed. Longer reception traverses different physical history; there is
no monotone search or presumed constant received energy. Each row caps at
180 s / twelve million samples; retain all limits and failures. No repeated
optimization until success and no receiver-bank tuning are part of this study.

The total ceiling is 36 modem rows (including seven cheap plans) with 3740 s
of declared aggregate wall caps, plus the fixed bounded full-wave component
matrix and correctness checks. One intensive native job runs at a time with
the default full `W=2*H` software budget. Cloud acceleration is considered only
for a demonstrated supported native bottleneck; independent trials are not
receiver-throughput measurements.

Raw receipts belong in ignored `local-evidence/hf-domain-*`, `hf-gui-*`,
`hf-coverage-*` and `hf-fullwave-*` while they support these unresolved findings
and numerical checks. Preserve one compatible build. Distill resolved findings
here and in the existing coverage/validation guides before deleting redundant
outputs; do not retain a duplicate receiver or alternate historical codebase.

## Declared causal amendments

After the ten-row screen, its sole shadow-reference miss was the maximum-rate
anchor. Before observing its complete-message outcome, freeze
[`hf-domain-edge-receiver.json`](campaigns/hf-domain-edge-receiver.json): the
same physical parameters, placement, source and noise, now through the native
receiver, capped at 180 s and eight million samples. A shadow miss alone does
not establish operational failure. This adds one receiver row to the original
ceiling; if it fails, the existing two-row +3 dB / double-duration −3 dB
allowance applies to this first new native domain failure. The physical
diagnostic selection above concerns `hf-domain-receiver` and this edge follow-up;
the GUI renderer does not support the private eikonal-template diagnostic.

The admitted GUI rows realize 10.4 s active bits despite the explicit 2 s
fixture requests, and use the canonical minute calendar. Keep that preparation
result in all timing/energy interpretations. The auroral planning rejection is
specific to the fixed 1920 Hz / 500 Hz appraisal audio layout. A separate cheap
canonical-probe plan at 6000 Hz / 1725 Hz, Helper off, is authorized here before
its outcome to distinguish that fixture limit from an inherent preset limit.
It adds one planning observation capped at 20 s; it is not a receiver trial.
The amended maximum is 38 modem/planning rows and 3940 s of declared aggregate
caps. Conditional rows remain unused when their selection criteria do not occur.

The frozen GUI 40/20 dB-Hz pair exposed wrong accepted content at 40 and correct
content at 20. Before new outcomes, declare three complete native follow-ups in
[`hf-gui-branch-holdouts.json`](campaigns/hf-gui-branch-holdouts.json): the 30 dB-Hz
log midpoint with unchanged roots, fresh additive-noise root 72002 at 40, and
fresh physical root 62002 at 40 with noise root 72001. Each captures the slot-2
IQ digest. These distinguish a local transition, noise dependence and another
physical realization; they are not a reliability sample or receiver repair.
The three rows add at most 360 s and eight million samples per row, making the
final maximum 41 rows / 4300 s. Existing native APIs do not expose independently
forced varying-channel scoring on that same GUI observation, so these runs
preserve the actual native branch policy instead of substituting an oracle.

Completion review identified a useful missing counterfactual despite the lack
of a CLI: the existing canonical coarse acquisition, public-coordinate
preparation, affine scoring and short varying-channel scoring can be called by
a contained native diagnostic. The new `gui-varying` mode renders the complete
original source/horizon, retains the selected complete observation and scores
both branches independently with public geometry and native priors. It does
not change receiver policy or deliver decisions to the codec. Unsupported
coherent-gain geometry, work/memory caps and cancellation must remain explicit.

Before diagnostic outcomes, freeze
[`hf-gui-varying-pair.json`](campaigns/hf-gui-varying-pair.json): slot 2 of the
original 40/20 dB-Hz pair, followed by a 40 dB-Hz complete-receiver parity replay
under the new build. Full-stream hashes must match the original captures and
the affine scores must reproduce the existing result. If the varying-only
native classification at 40 supports correct DATA1 against the original wrong
affine DATA0 and the 20 dB-Hz control is valid, execute
[`hf-gui-varying-fresh.json`](campaigns/hf-gui-varying-fresh.json) on the first
actual wrong active slot (0) of physical seed 62002. Its whole-stream hash must
match the earlier fresh-path capture; that earlier receipt selected slot 2, so
there is no historical selected-slot-0 digest to claim. Each of these four
maximum rows is capped at 120 s / eight million samples. This makes the final
ceiling 45 rows / 4780 s, without increasing any original episode's cap.

## Executed physical and operating cohort

Before the additional GUI branch diagnostic, **40 modem/planning observations**
ran in 487.979 s: 20 complete-receiver trials, 12 shadow/physical diagnostics,
and eight plans. These are different evidence categories, not 40 communication
trials or independent samples. There was one explicit unavailable narrow-audio
plan, no wall/sample censoring, and seven wanted-message failures among the
20 receiver trials. The 14 static full-wave profiles are additional component
calculations, not included in that count. No RunPod instance was needed or
rented: the bounded CPU execution took about eight minutes, and the experimental
phase scorer has no qualified GPU route. No accelerator or real-time capacity
claim follows.

| Retained campaign | Rows | Wall s | Observed scope |
| --- | ---: | ---: | --- |
| [Domain screen](local-evidence/hf-domain-screen/report.md) | 10 | 21.906 | Eight interior points and static anchor pass shadow references; maximum-rate edge misses |
| [Domain receiver](local-evidence/hf-domain-receiver/report.md) | 6 | 165.163 | Four correct wanted messages; noise/wrong-key controls quiet |
| [Maximum-edge receiver](local-evidence/hf-domain-edge-receiver/report.md) | 1 | 23.001 | Two missed active bits; premature completion and rejection |
| [Edge physical diagnostic](local-evidence/hf-domain-edge-diagnostic/report.md) | 1 | 5.608 | Same failed slot IQ, full public timing, known-context and gain-cost measurements |
| [Edge brackets](local-evidence/hf-domain-edge-brackets/report.md) | 2 | 68.445 | Both fail; neither increased energy nor longer near-equal-energy transmission repairs the message |
| [Previous long miss](local-evidence/hf-coverage-long-miss/report.md) | 1 | 13.346 | Exact historical IQ and whole-aperture score parity; five gain partitions now reported |
| [GUI admission](local-evidence/hf-gui-admission/report.md) | 7 | 0.865 | Six prepared, one explicitly unavailable audio layout; aggregate status remains `incomplete_checks` |
| [GUI receiver](local-evidence/hf-gui-coverage/report.md) | 8 | 176.461 | Five correct wanted messages, one wrong accepted message, two quiet noise controls |
| [GUI holdouts](local-evidence/hf-gui-branch-holdouts/report.md) | 3 | 13.072 | Wrong accepted content at the midpoint, fresh noise and fresh physical path |
| [Wider-audio auroral plan](local-evidence/hf-gui-auroral-wide-plan/execution.json) | 1 | 0.110 | Canonical preparation admitted; no reception executed |

This cohort uses appraisal source
`0505c94964ed1b82a66ab699272a896c95bdbb27a1a50f6243bef888e4a84b07`,
appraisal probe
`4543501a19b39551b8055104a040d5bc88fd8e3b8cf0e597e537edcf3bf1bab2`,
and canonical native source
`1749313fcabbf16527ac687c5cf4947acde3d00dca6bcd364b87d453b6d79a16`.
Base commit is `13db435fc85114be2e2a0298580d5c3e2bcba996`, GNU 14.2 Release,
Ryzen 5 PRO 5650U, six physical / twelve logical CPUs, automatic W=24.
The OS reports 15,677,412 KiB total RAM; dynamic memory admission and actual
stage allocation are recorded per receipt. All execution was CPU. The separate
canonical planning binary has SHA-256
`79e5e6de2bbdd77772c7520c11921a7e8c6bb5b58d001e8890386aba5cb0cf85`
and an older configure commit, `2ffbb0f4e5ffe59bcb94191280d4c020c3b9f628`,
but reports the same canonical native source hash. It is an admission contrast,
not a matched observation or receiver result.

The [22-group check](.build/hf-coverage-checks.log) passed in 81.52 s,
including 233 whole-domain and 36 full-wave assertions. The first build had
passed 21/22; its full-wave test used a stale wrong-sign zero-index reference.
That failure remains in [the original log](.build/hf-coverage-checks-initial.log).
The corrected test uses the independently derived `r=1−t` boundary relation.
A separate resonance safeguard rejects unresolved opaque-layer feedback rather
than assuming underflowed transmission always decouples layers. After the
small physical-partition reporting addition, the affected integration/runner
checks [passed again](.build/hf-coverage-final-checks.log) in 52.13 s.
The runner had 58 checks and the frozen-design analyzer nine at this point.
These checks are separate from observed communications.

### Physical-domain outcomes and energy/time boundary

The native domain check bounds group delay to 2.08794242–2.15915608 ms,
excursion to 71.2137 microseconds, spectral margin to .919887 Hz and group-delay
rate magnitude to `1.77352e−6`. These fit the same frozen public envelope for
every design point. The eight sampled bins on each axis still cover only eight
of 64 joint-bin cells for each pair. Neither midpoint coverage nor a passing
sample qualifies the intervening continuous parameter box.

All eight interior screen points and the static anchor pass the stated
presence/bit references. Their smallest outer-weighted presence score is
24.68996 against 21.49338. The maximum-rate anchor scores −3.16686, with bit
gap 2.50124 against 5.29330. Those are selected-timing shadow references.
The three original native `e` messages pass through canonical affine admissions;
their supplemental branch runs only on quiet slots. In contrast, the fresh
13-bit `A` holdout actually uses supplemental DATA evidence at slots 1, 6 and 7.
The paired noise and wrong-key controls score five quiet slots each, with no
false DATA or accepted message.

The maximum-rate native message fails as follows:

| Actual active bit s | C/N0 dB-Hz | Total received signal energy / sample-noise variance | Active decisions | Complete-message result |
| ---: | ---: | ---: | --- | --- |
| 139.2 | 0 | 419.115744 | DATA0, quiet, quiet | Premature completion; rejected |
| 139.2 | +3 | 836.245850 | DATA0, quiet, quiet | Premature completion; rejected |
| 266.8 | −3 | 401.938674 | quiet, quiet, DATA1 | Rejected; no premature flag |

No message is accepted in these three rows. The long row has .959015928 times
the original energy/noise ratio, a −.1817418 dB change; it samples a different
continuous trajectory history. This is a failed operational near-equal-energy
contrast, not a monotonicity theorem. The increased-energy row also fails.

The full-timing diagnostic exactly matches the failed native slot-1 IQ hash
`31dc3ea308d0d86f9c24df598b4dc8430b4279160d1df1f8fa3fb1a2d2b09ea7`.
Its combined supplemental bit scores `[1.272510341, −2.920321748]` match native
scores within `1.34e−15`; both use all 39 timing atoms with prior mass one.
Timing-index enumeration differs, and native telemetry does not expose enough
coordinates to certify that enumeration mapping. After the outer prior,
presence is −.79194 and bit gap 4.19283, still below both references.
The known-context binary distance/noise is 276.36871, with conditional error
`3.32e−32`. It excludes acquisition and completion and is not an achievable
blind-receiver bound.

### Coherence recovery is not free

The new partition output separates retained clean energy from the cost of
unknown independent complex gains. The values below use the correct hypothesis
on a public template selected from the same noisy search; their log likelihoods
are **descriptive, not calibrated post-selection receiver evidence**.

| Observation | Gain duration s | Complex rank | Retained clean energy | Public-template log LR | Known-physical-template log LR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Maximum edge | .25 | 558 | 94.209% | 22.133 | 22.867 |
| Maximum edge | 1 | 140 | 58.165% | 17.027 | 37.303 |
| Maximum edge | 4 | 35 | 37.385% | 6.305 | 69.620 |
| Maximum edge | 16 | 9 | 21.228% | 17.995 | 97.824 |
| Maximum edge | Whole | 1 | 6.993% | 20.477 | 121.264 |
| Previous long TID miss | .25 | 2089 | 81.597% | −1.867 | −1.609 |
| Previous long TID miss | 1 | 523 | 80.588% | 7.252 | 10.054 |
| Previous long TID miss | 4 | 131 | 68.948% | 17.960 | 30.809 |
| Previous long TID miss | 16 | 33 | 34.040% | 20.873 | 62.687 |
| Previous long TID miss | Whole | 1 | 7.243% | 19.310 | 116.370 |

The previous long-miss replay exactly preserves its old IQ, observation bounds,
known-binary measurements, whole-aperture physical/public projections, public
proposal records and combined scores. High short-block projection therefore
cannot be promoted to a demonstrated receiver improvement: even the exact
physical template has negative observed likelihood at the shortest partition
in that weak long observation. See the [research discussion](hf-research.md)
for the limited `rho−log(1+rho)` unknown-gain information calculation and why
physically correlated phase state may permit a better tradeoff than independent
gains. The receiver still needs bounded proposals, properly weighted hypotheses
and measured complete service; private channel knowledge is not supplied to it.

### Canonical GUI counterexamples

HF Worst / Each 65% at Margin 1 with opposite clock endpoints completes `e`.
At Margin 3 and 40 dB-Hz, the same declared path seed produces DATA bits `000`,
accepted as a space instead of `001` (`e`). Slot 2 uses fixed affine evidence:
bit scores 6097.548 / 5315.465, a wrong-bit gap of 782.083. The slot reason is
`fixed_affine_data_evidence` and varying-branch status is
`coherent_evidence_sufficient`; the varying-channel branch is not evaluated.
This is wrong content under a present signal, not noise-only false DATA or
premature completion. The FEC-off short prefix code accepts both valid tokens;
the message receipt correctly reports `authenticated: false`.

At 20 dB-Hz, slot 2 uses bounded varying-channel evidence, scores
50.881 / 112.445, and correctly completes `e`. The two rows share exact source
record/energy, propagated signal energy, clock/path/timing realizations and
public geometry. Noise variance is .192 versus 19.2. Automatic memory limits
differ slightly, with both fully admitted; the complete records are not
identical and the noisy IQ must differ. Changing Margin also changes actual
spread processes as well as public bounds, so Margin 1/3 is not an isolated
search-margin comparison.

The three declared follow-ups all accept wrong content: 30 dB-Hz and fresh
noise 72002 at 40 produce a space; physical root 62002 at 40 produces `100`
(`o`). The fresh path's first and third bits are wrong. Each uses affine DATA
admission. Those observations strengthen failure finding without estimating
an error probability or proving that forced broader scoring would repair them.
The same-IQ diagnostic below addresses that remaining causal question.

Off with actual canonical duration planning, Helper-on, the lower-C/N0 row and
broad arrival complete correctly. Both GUI noise controls remain quiet. The
broad-arrival wanted trial takes 94.904 s of native wall time, versus shorter
narrow-arrival cases with different realized timing/clock conditions. This is
whole experiment work, not an isolated cause comparison or receiver-capacity
measurement. Actual bit duration is 10.4 s with the canonical minute calendar.
The 2 s fixture request does not certify a 2 s modem configuration.

## Same-observation native branch counterfactual

The [three-row pair/parity campaign](local-evidence/hf-gui-varying-pair/report.md)
completed in 8.946 s; the conditional [fresh-path diagnostic](local-evidence/hf-gui-varying-fresh/report.md)
completed in 2.244 s. This brings the continuation to **44 modem/planning
observations in 499.168 s**: twenty original complete-receiver trials plus one
exact receiver replay, fifteen component diagnostics, and eight plans. The
fourteen static full-wave profiles remain separate. One narrow-audio plan was
unavailable; no episode was time/sample censored. The extra receiver replay
repeats a known failure and is not a new independent failure sample.

These four added rows use final appraisal source
`fa088629761b7ed328913c314f9d21cc0fca354d17f45c6bfaec2a493b279b80`
and probe
`24a08e4e0fba100b777b32feecf4b49f74fb3bd8dcb96fb422c19e55c4ad0b78`.
Canonical native source, compiler and base commit remain as above. The original
40-row cohort stays bound to `0505c949…`; it was not rerun wholesale.

Before executing these comparisons, independent review caught a missing
ordinary/line outer half-weight in the initial diagnostic. Even an unscored
line branch reserves its mass. The repair obtains the native line reservation,
preserves the raw branch scores, and classifies local copies with the fixed
outer weight. It retains unscored drift mass as well. A new runner regression
rejects the original omission even when every reported stage is otherwise
internally consistent. No unweighted diagnostic campaign was executed. The
pre-repair [build](.build/hf-gui-diagnostic-build.log) and
[three-group check](.build/hf-gui-diagnostic-checks.log) are retained as software
checks that did not establish prior parity. The corrected build then passed
[all three affected groups](.build/hf-gui-diagnostic-prior-checks.log) in 64.70 s,
including **63 runner tests**. An earlier new runner test had incorrectly
expected `[]` rather than the established `{}` empty-statistics object; that
expectation-only correction is separate from the substantive prior repair.

Both original diagnostic rows reproduce the original complete-stream IQ hash.
The 40 dB-Hz affine bit pair and presence match the original receiver exactly;
the 20 dB-Hz combined pair and presence also match exactly. The same-build
40 dB-Hz complete-receiver replay preserves its whole-stream hash, selected
slot digest, and all five decisions/bits/evidence values, including the wrong
accepted space. Retained-line processing would rewrite the canonical slot
reason; neither original row has that reason, so the comparison does not hide
a retained nuisance-line contribution.

| Observation | Truth / old affine bit | Prior-corrected affine scores, bit0 / bit1 | Short-varying scores, bit0 / bit1 | Varying-only native diagnostic |
| --- | --- | --- | --- | --- |
| Original40, slot2 | 1 / 0 | 6097.548 / 5315.465 | 50174.654 / 75824.490 | Correct DATA1 |
| Original20, slot2 | 1 / no affine DATA | −3.547 / −5.777 | 50.881 / 112.445 | Correct DATA1; combined reproduces original native result |
| Fresh physical62002/40, slot0 | 0 / 1 | 4119.940 / 5732.699 | 80560.881 / 54187.955 | Correct DATA0 |

The fresh row satisfies the predeclared condition. Its complete-stream hash
matches the previous fresh-path capture and its affine pair/presence match
that failed native slot exactly. The newly measured selected-slot-0 hash is
`f2dc1f3f3ce1ee86754737c8557559a6bdd5999ed23037e4499caed37bf16948`.
The historical fresh receipt selected slot 2, so it supplies no old slot-0
selected digest to compare. The fresh message also had a wrong third bit;
this diagnostic checks only its first wrong bit. Neither it nor the original
pair demonstrates corrected complete-message reception.

The ordinary weight is .5; affine, short-varying and reserved drift each have
inner weight 1/3. Coherent-gain geometry is explicitly unavailable under its
native work limit. No drift or coherent-line score was supplied, and no omitted
prior mass was reassigned. The native classifier runs only on diagnostic
copies; no decision enters an operating receiver or codec.

For the original40 and fresh40 rows respectively, the additional varying stage
renders 6,644,168 and 6,511,968 basis rows over nineteen candidates, taking
.15234 and .08288 s. That is +21.5% and +16.2% relative to each separately
measured coarse-plus-affine component interval. The sequential maximum DSP
scratch remains the affine quote, 163,767,016 / 163,177,856 bytes; 193,032,192
bytes of W=24 worker-stack reserve plus process/metadata memory are separate.
These are isolated stage observations. Scheduling contention, other active
slots, quiet decisions, Helper acquisition and total stream service can change
the practical cost. They do not establish a receiver-throughput ceiling or a
free operating-policy change.

The result narrows the receiver investigation: **useful opposing evidence
already exists in the compiled broader model on these exact failed samples**.
A justified next policy candidate is to compare admitted varying-channel
alternatives before committing a bit under declared time-varying conditions,
while preserving public priors, unsupported-path reporting and resource caps.
Its complete-message, null-control, Helper and same-stream capacity effects
still require qualification. Simply raising confidence thresholds, changing the
assumed noise level, or calling every short-block projection a recovery would
not resolve the demonstrated model-selection issue. The separate weak eikonal
failure still needs broader structured phase handling; this GUI finding does
not repair that sensitivity floor.

## Reusing the bounded coverage

From the repository root, build/check the contained native targets, then check
that the design still matches its frozen manifests:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
python3 -B _appraisal/highFidelity-triple_Robust_Modem/coverage_design.py --check
```

Select a campaign according to the affected mechanism; do not routinely run
all historical campaigns. For example, the ten-point physical screen is a
roughly 22-second check on the recorded host, while the selected message cohort
and GUI cohort each take roughly three minutes. Their caps, not those observed
times, govern a different host. A screen cannot substitute for native message
completion. Run the scalar physics probe separately; it is not a modem trial.

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-domain-screen.json
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_fullwave_probe
```

Substitute `hf-domain-receiver.json` or `hf-gui-coverage.json` for those native
cohorts. The runner creates a fresh ignored result directory; it never overwrites
an earlier campaign. The design's `--audit-run PATH/TO/run.json` preserves every
planned/executed/censored/unavailable status and verifies canonical manifest
identity. The conditional and causal follow-ups above retain their original
selection history; do not count selected failures or exact replays as fresh
reliability samples.

The remaining physical gates are combined detailed ionosphere plus actual
clocks, magnetoionic/oblique dynamic propagation, 3D strong scattering and
caustics, and long independently measured complex channels with adequate
metadata. The static full-wave component cannot fill these gaps by simply
varying its single-frequency coefficient per sample. Broader weak-signal GUI
qualification and fair energy/time/payload comparisons with other modems also
remain open. Existing positive and negative operating results remain useful
without implying that most unusual natural-day failures will be caught.

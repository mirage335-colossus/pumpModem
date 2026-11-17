# High-fidelity triple Robust Modem appraisal

This contained suite exercises the canonical native modem against calculated,
reference-reproduced, standard-statistical and explicitly synthetic channel
hypotheses for **sub-9 kHz / 200 ft antennas, 1 MHz groundwave, 30 MHz
groundwave, 1–30 MHz skywave, and Earth–Moon–Earth**. All appraisal code,
fixtures, documentation and generated evidence remain in this directory.
Python launches native executables and analyzes receipts; it implements no DSP.

**Using the findings with the newer modem:** the
[admission/carrier-key applicability map](coverage.md#applicability-after-the-admission-and-carrier-key-changes)
distinguishes GUI history filtering from native DATA admission, explains the
configured-frequency keystream revision, and connects known failures and
tested/rejected/contemplated implementations to the evidence needed for reuse.
The source-bound results below are preserved historical observations. The
[current-waveform continuation](hf-current-admission.md) explicitly regenerates
selected failures and controls; it does not refresh every old campaign.
Failure guides, source and campaign definitions are versioned; links into
ignored `local-evidence/` refer to retained local receipts, which a fresh
checkout does not include.

The current continuation demonstrates **protected complete-message recovery**
through coupled magnetic polarization, oblique reflection and diffraction with
actual clock errors and a continuing interferer: the baseline cannot complete
`e` or fresh `a`, while the predictive candidate's rejected fragments recover
the exact payload through the bounded native-codec consumer. All six native
admission tiers preserve these two recoveries. Their premature physical
completions remain recorded. The shorter physical foreign-key control leaves
9/6/5/3/3/1 false DATA decisions across the six tiers, so even Maximum fails
the zero-false-DATA gate; no incorrect content is accepted. The current-waveform
unprotected `Ae` witness still accepts truncated `A`, rejecting general
predictive-quiet promotion. See the linked continuation for frozen inputs,
independent controls, timing, energy comparisons and applicability limits.

The declared longer-duration follow-up then removes all DATA losses and
premature completions for both protected payloads: the experimental quiet
policy delivers them directly. It costs approximately 2.047× incident DATA
energy and 1.74–1.75× candidate receiver service. Maximum native admission
passes the four longer-case repair checks, including complete foreign-key and
tone/no-DATA controls; primary Strict still leaves one false DATA decision.
The canonical baseline still cannot complete either wanted message. These are
finite conditional appraisal results, with separate per-policy audits; neither
the GUI/default receiver nor general disturbed-day qualification is promoted.

The archived [receiver-repair investigation](hf-predictive-completion.md) repairs
five selected completion failures, but **the candidate fails the broader
integrity criterion**. Across ten paired complete observations, it completes
oblique `A`, `B` and prospectively selected fresh `D`, evolving-field `C` and
two-hop `E`. Each preserves thirteen correct DATA decisions and scores two
ending quiet slots on the identical capture that the baseline cannot finish.
Both policies preserve endpoint-Off `A` delivery, no DATA or accepted content in
the complete noise control, and the +6 dB-Hz/69.6-second miss versus
+3 dB-Hz/139.2-second delivery. Wrong-key reception still produces three false
DATA decisions in both policies without accepted content.

The decisive active-fade follow-up transmits `Ae`, suppresses its wanted signal
in public slots 13–14, then resumes correct DATA in slot 15 with input still
open. The baseline leaves both faded slots ambiguous and accepts no message.
The candidate calls them quiet and prematurely accepts the shorter `A`:
**one new wrong-content acceptance, so general promotion fails**. The test
reaches the new decision branch and intentionally fails its acceptance check;
it is not an execution failure. Earlier candidate failures remain recorded.
Two proposed DATA guards are also rejected because they can create high-SNR
rejection floors.

The [protected-fragment consumer](hf-protected-reassembly.md)
has seven standalone complete native signal-chain observations. It recovers complete
RS20 `B` and fresh `e` after native interior-outage splitting, using one bounded
span and the existing decoder, without another DSP search. Clean, noise and
separate-message controls pass. The first wrong-key case produces 72 false DATA
decisions. The added wrong-key joining case actually attempts one combined
decode and rejects it, while **136 false DATA decisions persist**. Neither
wrong-key control accepts native or consumer content. These are contained
candidate results; operational qualification and calibrated disturbed-event
replay remain failed or unqualified.

A separate [combined predictive/protected trial](hf-protected-reassembly.md#completed-wanted-observation)
now recovers exact RS20 `e` after the candidate changes four ambiguous slots
to quiet while preserving 62 correct DATA decisions. Two actual rejected
fragments become one 64-position native decode with two erasures, completing
at slot 66 without incorrect content. The premature physical completion
remains recorded, and coding costs 21.333 times the uncoded nominal airtime
and energy. After a preserved setup failure, the corrected wrong-key trial
actually attempts and rejects a combined decode without accepted content.
However, both policies produce 61 false DATA decisions, and an additional
ambiguous slot gives three erasures instead of the frozen two. **Both declared
requirements fail; the combined route remains unqualified.**

The [normalized-covariance diagnostic](hf-normalized-null.md) adds two analytic
aperture diagnostics and a separate matched static native-slot pair. The latter
reproduces actual wrong-key false DATA and separates it from the wanted control
in the normalized scores. The analytic wrong-key case already abstains natively.
Neither pair establishes a qualified structured-interference null or an
operational DATA veto; native decisions remain unchanged.

The preceding [coupled multiple-hop extension](hf-multihop.md) combines two Es reflections,
a lossy ground bounce and a competing F-region return, retaining magnetic
polarization and diffraction through the selected paths. Full numerical
admission rejects an inaccurate fixed-angle approximation before receiver use.
The primary constructed field contains qualified sampled subsecond deep
carrier cancellation and longer band-energy fades. Sixteen unique complete
observations include eight correct deliveries and four misses among twelve
wanted cases. At the nearer boundary, +6 dB-Hz/69.6-second bits miss one bit,
while +3 dB-Hz/139.2-second bits deliver at nearly equal incident E/N0. The
clock/interference combination still fails physical completion, and the strong
wrong-key control produces thirteen false DATA decisions but no accepted
message. Three other negative controls stay quiet. Those failures remain
requirements failures. These are selected local
planar corridors, not a global ionosphere or a calibrated disturbed-event replay.
The public-data audit records concrete raw-IQ leads and their calibration,
event-selection and access limitations.

The preceding [intrinsic-evolution extension](hf-vector-evolving.md) deforms the plasma
density during a message in that same coupled 3D calculation. Two fixed fields
pass independent temporal, spatial, frequency and minimum-voltage checks before
receiver admission. Four complete-delivery requirements are declared before
execution, alongside weaker duration/energy comparisons and negative controls.
Private energy measurements use each actual public decision window, keeping
whole-message energy separate from per-decision information.
The four direct delivery requirements pass and both negative controls stay
quiet. The strong native clock/interference case still fails message completion
despite thirteen correct DATA bits; its matched endpoint-Off case completes.
This is a retained failed requirement, not a receiver repair.
The eleven unique complete observations also narrow one fixed weak-signal
delivery bracket to +17.5 failure / +20 success. At the midpoint, eleven correct
bits still do not make a delivered message: two consecutive quiet decisions
split it into codec-rejected fragments.

The preceding [two-transverse-coordinate extension](hf-vector-3d.md) adds coupled 3D
spatial diffraction to the same native magnetic/reflection calculation, with
explicit numerical rejection and support recovery. Its
[executable qualification reader](hf-vector-3d.md#executable-qualification-requirements)
separates completed tests from satisfied reception requirements and checks the
actual target binary. The [exact-input outage witness](hf-fragment-integrity.md#exact-native-input-ambiguity-under-an-outage)
shows a framing ambiguity that additional receiver compute alone cannot remove.
Eleven new complete receiver observations include successful controls and a
reproduced completion failure despite thirteen correct DATA bits. The
32-cell audit remains failed on documented integrity and false-DATA requirements;
the separate delivery observations and missing qualification strata remain visible.

The preceding [coupled vector/oblique extension](hf-vector-oblique.md) calculates
magnetic polarization, transverse diffraction and oblique reflection in one
bidirectional Maxwell operator. Independent analytic, flux, refinement and
assembled-waveform checks precede complete native reception. Its frozen fields,
physical ablations and endpoint interactions expose another `B`→`i` fragment
acceptance and strong wrong-key false DATA, while focused higher-C/N0 controls
establish complete reception. A bright clock/interference case also recovers
every active bit yet misses completion because both ending slots are ambiguous;
its matched endpoint-off control delivers. This materially extends the environmental model;
it does not repair those receiver failures or qualify general disturbed-day
availability. [Public archive/schema inspection](hf-research.md#transverse-diffraction-and-usable-measurement-audit)
also distinguishes available sounding-parameter products from calibrated complex
channel recordings.

The [correlated-presence follow-up](hf-correlated-presence.md) demonstrates
two complete-message repairs on matched captures using a normalized public
gain-history search. It preserves a fresh payload, ambient reception and
quiet negative controls, but the original separated-screen incorrect-space
acceptance remains: general promotion fails. Its duration follow-up removes
an artificial pilot admission limit, preserves the archived 139.2-second
case at nearly equal energy, and keeps its complete noise control quiet. The
[fragment-integrity follow-up](hf-fragment-integrity.md) separately verifies
the existing protected codecs and delivers the failed lens payload using
RS20, with its roughly fivefold duration and energy costs disclosed. Codec
checks, shadow evidence and complete-message reception remain distinct.

The [transverse wave-optics extension](hf-wave-optics.md) adds a plasma lens
whose focusing, cancellation, phase and frequency response share one density
model. The [separated-screen extension](hf-phase-screens.md) propagates the
field through two finite plasma regions, with an independently constructed
holdout. Mesh, extent, slab thickness, frequency, continuous interpolation and
deep-fade witnesses precede receiver admission. A finer local check finds a
deeper sampled fade than the original spatial grid; numerical qualification
therefore includes the actual FIR at those refined positions. These are
constructed scalar forward-propagation tests, not measured disturbed-day
channels or a general three-dimensional Maxwell calculation.

The [recorded-failure investigation](hf-recorded-diagnosis.md) reproduces the
archived Gannon message miss on the exact received IQ and compares native
evidence with full-prefix, target-absent and alternate-bit references. It
separates projection loss, gain assumptions and public-prior costs, and rules
out a gain-variance-only repair on the evaluated unpartitioned banks. Those
private references retain the recording-noise limitation and do not establish
an implementable receiver repair.

Wave and endpoint campaigns use the native CPU receiver on the local Ryzen 5
PRO 5650U with 12 available logical CPUs and the unchanged 24-worker budget.
The [host receipt](local-evidence/hf-wave-host.json) records the actual Linux
and GNU 14.2 environment. No RunPod instance was needed for these bounded
runs. Their elapsed costs and workspace observations are not a real-time
capacity qualification or a GPU measurement. Keep this receipt with the
associated current cost comparisons until those findings are superseded.

The expanded tests now include complete physical-calendar EME episodes and a
complete VLF message representing more than nine hours of transmission. They
also expose failures: compound noise can produce false DATA, broad lunar
scattering can produce an accepted wrong message, and slow phase wander can
lose a weak VLF signal while increasing computation. These are retained test
results, not hidden by a software-check pass. Read the [validation record](validation.md)
and [coverage matrix](coverage.md) for exact outcomes, identities and limits.

No fixture has independent local field/equipment calibration. In particular,
the VLF antenna circuit and magnetic receiver levels are separate reference
planes, groundwave magnitude snapshots do not supply measured phase or terrain,
and the lunar process is a frozen monostatic statistical approximation. The
suite does not qualify environmental availability, unknown-calendar acquisition,
rare-error probabilities or complete-receiver real-time capacity. The
[models](models.md), [research reconciliation](research-reconciliation.md),
[HF research synthesis](hf-research.md), [HF experiment map](hf-experiments.md),
and [protocol](experiment-protocol.md) preserve those distinctions and the
source limits in [`_ref`](../../_ref/highFidelity-triple_Robust_Modem).

The [magnetic propagation and long-record continuation](hf-magnetoionic.md)
adds a native two-polarization cold-plasma calculation, antenna cancellation
derived from the same dielectric response, and two complete hour-long Gannon
storm-era recordings. The simulator rejects an inadequately resolved deep null
before admitting a refined waveform. Complete-message trials and conditional
failure diagnostics remain separate from numerical checks. Recorded-voltage
replay explicitly retains its measurement-noise confound; it is not a calibrated
broadband channel or proof of field blackout tolerance.

The [inference and propagation follow-up](hf-inference-and-propagation.md)
adds 32 bounded observations, including 15 complete receivers. It separates
phase-reachability from rate-support failures and retains GUI errors on clean
IQ. The fixed smooth-frequency candidate recovers individual bits but no
additional complete long message. A numerically admitted coupled
cutoff/absorption waveform now runs through the canonical receiver: the fixed
one-ray configuration recovers `A` at incident C/N0 = 40 dB-Hz, fails at 30,
20 and 0 dB-Hz, and stays quiet on its matched noise control. This is a scoped
sensitivity bracket, not qualification of an optimal receiver or most unusual
real HF conditions.

The [physical and operating coverage expansion](hf-coverage-expansion.md)
adds a frozen parameter design independent of the receiver bank, canonical GUI
preparation with actual sample/RF clock errors, and a separately validated
[static collisional full-wave reference](hf-fullwave.md). These are three
distinct model layers; they do not jointly qualify a dynamic magnetized channel.

Its [44 modem/planning observations](hf-coverage-expansion.md#same-observation-native-branch-counterfactual)
and fourteen static full-wave profiles expose another failed energy/time
boundary and wrong accepted GUI content. Same-IQ diagnostics then find correct-bit
evidence in existing native processing that early affine admission skipped,
including a fresh physical path. The subsequent
[receiver investigation](hf-receiver-repair.md) now recovers four archived
wrong messages with an explicitly approved opt-in native comparison. It also
finds fresh-payload failures and a wrong-key accepted-message regression, so
that comparison is not a general repair. A separate normalized phase-history
filter recovers the maximum-rate physical edge and a fresh trajectory through
the complete native receiver. A fixed second transition prior also recovers
the previously failed long weak message, while a new long trajectory still
fails. Ordinary receiver defaults are unchanged.

The earlier [waveform-kernel admission tests](hf-fullwave.md) also rejected a physical
model approximation: interpolation between accurate frozen ionospheric
responses can be highly inaccurate between them. A numerically converged
static solver therefore does not automatically supply a validated dynamic
channel. The suite preserves these failed approximation checks alongside
failed communications. Subsequent phase-factored interpolation passes the
declared sampled full-band, causal-FIR and finite-retardation gates and feeds
the contained coupled reflection scenario. Its complete-receiver results are
recorded [separately](hf-inference-and-propagation.md); neither numerical
admission nor modem success would establish evolving-Maxwell or field validity.

The earlier [independent HF ensemble](hf-ensemble.md) adds multiscale and
nonperiodic density trajectories, separate trajectory/noise seeds, independent
channel placement, competing paths, and selected Helper/noise interactions.
Its frozen screen, complete-message cohort and conditional SNR/time brackets
keep receiver mismatches, information loss, null controls and resource limits
separate. The earlier [public phase search](hf-public-search.md) and
[phase refinement](hf-phase-refinement.md) remain opt-in experiments with fixed
public priors. Ordinary receiver defaults are unchanged.

Its [31-trial results](hf-ensemble.md#executed-evidence-and-findings)
include a near-equal-energy long-message failure, premature completion across
mode changes, and nonwhite-noise false DATA. Fresh trajectory/placement checks
also pass, and a native bit rescue contradicts a standalone shadow miss.
These results separate observed receiver weakness from private known-channel
information and physical fading; they do not qualify general HF robustness.

## Build and quick correctness checks

From the repository root:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

This configures an ignored `.build/` and links the production C++ modem.
The coupled multiple-hop continuation passes **41/41 CTest groups in 183.46 s**,
including 140 runner checks, exact lower-signal plan admission, and native
preparation-versus-resident memory accounting. Insufficient channel budgets
are rejected before physical table preparation or source consumption.
[Current full check log](local-evidence/hf-multihop-preflight/workspace-build-and-checks.log).
The numerical physical trials and complete communications results are recorded
separately in the [multiple-hop investigation](hf-multihop.md); a software-check
pass does not override its failed false-DATA requirement or earlier failures.
The subsequent admission-only extension for the frozen +6/+3 pair passes all
three affected native integration, runner and qualification groups in 61.21 s.
[Final affected check log](local-evidence/hf-multihop-preflight/boundary-build-and-checks.log).

The intrinsic-evolution continuation passes **39/39 CTest groups in 105.37
seconds**, including 127 runner and 23 qualification/analysis checks. Native
checks cover the evolution law, energy-window accounting, identical rendered IQ
with instrumentation enabled, partial captures and energy outside observations.
[Full pre-reader-correction check log](local-evidence/hf-vector-evolving-preflight/ctest-current.log).
After the receipt-scope correction, both affected Python groups again pass
(127 runner and 23 qualification/analysis checks); the native calculations are
unchanged. The original validation stop remains in the experiment record.
These software checks are separate from complete receiver outcomes in the
[evolving-field record](hf-vector-evolving.md).
The newly registered evolving-waveform group passes in 75.04 s, and the final
four affected groups pass in 140.78 s, including native integration. Coverage
now spans forty registered groups through the full and affected runs;
[final affected log](local-evidence/hf-vector-evolving-preflight/ctest-final-affected.log).
The subsequent Python-only historical-held descriptor correction passes
128 runner and 23 qualification checks. All 43 qualification receipts validate
with the original 32 outcomes preserved; the overall qualification remains
failed. [Final reader checks](local-evidence/hf-vector-evolving-preflight/reader-final-tests.log)
and [qualification record](hf-vector-evolving.md#finite-qualification-and-preservation-of-earlier-failures)
retain the original failed audit and exact compiled/external-reader identities.

The preceding 3D spatial/qualification continuation passes **39/39 CTest groups in
108.77 seconds**, including the added 3D assembled waveform, exact-input
completion ambiguity and finite qualification-reader groups.
[Current complete check log](local-evidence/hf-3d-preflight/ctest-final.log).
After the final Python-only qualification/reference edits, the affected reader
group passes all **17 checks**; this includes the disjoint-observation energy
bound and rejection of overlapping or incomplete apertures. The compiled native
binary and communication receipts retain their original source identity.
These checks do not change the failed communications requirements.

The preceding coupled-vector extension exercised 35 CTest groups: 34 passed in the
initial full run (88.79 s); the scenario-catalog assertion omitted the new
propagation family and its finite horizon. After that test was corrected,
the affected group passed in 1.34 s. The four affected native-chain,
integration, runner and waveform-receipt groups then passed in 58.28 s.
The newly added automatic assembled-vector-waveform group passed in 8.96 s,
bringing that preceding suite to 36 groups. The final endpoint-C/N0 admission
change passed all 121 runner and 13 waveform-receipt checks. These are applicable full and affected
checks, not a claim of a second single 36-group run.
[Original and corrected logs](local-evidence/hf-vector-oblique-preflight/)
preserve that failure separately from communications outcomes.

The preceding correlated-presence and fragment-integrity extension passed
**34/34 CTest groups in 89.13 s**
([log](local-evidence/hf-presence-integrity-tests.log)). The subsequent duration
admission changes passed the geometry check and all three affected integration,
runner and waveform-receipt groups in 60.19 s, including 119 runner and 13
waveform-receipt checks
([records](local-evidence/hf-duration-preflight/)). The final duration campaign
binds to appraisal source
`d6260db20cec9a95a4c2507e6d5619ba6cdd3b8378575bc0a03deaa0aca289f0`;
earlier comparisons retain their own exact build identities in the
[correlated-presence guide](hf-correlated-presence.md). Canonical production
source is unchanged. All fourteen new complete-receiver observations validate
their receipts; two retain valid communication failures. A valid receipt is
not a successful message or promotion of the candidate.

The preceding wave/scattering and matched-diagnostic extension passed
**32/32 CTest groups in 83.06 s**, including 129 runner and wave-receipt
checks ([final log](local-evidence/hf-wave-final-tests.log)). Its
appraisal source digest is
`178d4ea3eee441faa9301086ac7337749394d3ee62c742bc76b118a8c17b0e5c`;
canonical native source remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
These checks passed while the communication cohort deliberately retains
failed delivery and accepted incorrect content. Its observations and earlier
setup/numerical failures are recorded in the linked model guides and
[coverage record](coverage.md).

The magnetic/long-record extension passed **28/28 CTest groups in 83.59 s**,
including 113 runner cases. Its [validation log](local-evidence/hf-magnetoionic/final-build-and-tests.log)
and [source-bound communications results](hf-magnetoionic.md) keep software
checks separate from successful messages and preserved failures.
The physical/operating extension passed all **22 CTest groups in 81.52 s**
([log](.build/hf-coverage-checks.log)). A small diagnostic-report addition then
passed the affected integration/runner groups again in 52.13 s
([log](.build/hf-coverage-final-checks.log)). This includes 233 physical-domain
assertions and 36 full-wave assertions. The final same-IQ diagnostic passed
all three affected groups in 64.70 s, including 63 runner cases; the frozen
design has nine cases. These are software checks, not communication successes.
Exact build identities, the repaired prior-accounting defect and replay parity
are recorded in the [coverage expansion](hf-coverage-expansion.md).

The historical canonical `test triad` result was 32/32 in 62.22 s
([log](.build/hf-native-triad-checks.log)). The newly approved native comparison
hook received fresh checks: 31/32 passed with parallelism two, and the receiver
test timed out at 60 seconds; the unchanged isolated receiver test then passed
in 56.05 seconds. This is not reported as an all-green original run.
Current affected checks, historical failures, repairs and complete
message results are retained in [HF validation](hf-validation.md), including
the failed first phase-refinement initialization and later long-slot recovery.
Current group coverage is:

| Group | Main coverage |
| --- | --- |
| `channel` | Continuous state, silence/tails, interpolation, phase wander, validation and resource limits |
| `scenarios` | Physical/source tuples, normalization, reference observables and adverse numerical controls |
| `hf_scenarios` | NVIS, DRM6, extended HF profiles and an off-grid eikonal holdout |
| `hf_ionosphere` | Eikonal phase/group-delay consistency, independent quadrature, and frequency/time derivatives |
| `hf_vector_oblique` | Coupled bidirectional vector scattering, vacuum/Fresnel/Born reductions, coherent passivity, translation and magnetic reversal |
| `hf_vector_oblique_waveform` | Primary qualified RF/FIR table, 54 direct assembly witnesses, impulse/tail, moving tone and chunk invariance |
| `hf_vector_3d_waveform` | Coupled two-transverse-coordinate primary, spatial checks before trajectory folding, broadband/FIR and streaming checks |
| `hf_completion_ambiguity` | Exact native IQ-prefix identity, three DATA/two QUIET decisions and premature acceptance before EOF under a constructed outage |
| `qualification` | Hash-bound finite evidence, current target-binary observation, separate requirements and characterization, stale/missing/censored evidence |
| `hf_admission_replay` | Canonical admission/controller replay parity, preserved presence, bounded ordered slots, quiet/erasure and codec behavior |
| `current_admission` | Changed-binary rejection, timeout stream retention, valid execution separated from failed communications requirements |
| `protected_oblique_audit` | Retrospective physical repair checks separating combined delivery, integrity, false DATA, premature completion and provenance |
| `hf_design` | Fixed whole-domain phase/group bounds, strict physical coefficients, seed/placement isolation and mutation order |
| `hf_gui_profile` | Canonical preset preparation, actual endpoint clock errors, margin semantics, Helper, noise-seed isolation and rejection |
| `hf_fullwave` | Independent zero-index, Fresnel/slab, Joule-loss and smooth-barrier references; passive opaque layers and unresolved resonances |
| `hf_fullwave_kernel` | Causal support, continuous-history interpolation, finite-band error gates and independent off-grid rejection of a frequency-sampling delay alias |
| `hf_magnetoionic` | Cold vector-plasma scattering, independent Maxwell/analytic reductions, passivity, field reversal and singular rejection |
| `hf_magnetoionic_kernel` | Fixed-basis interpolation, path-specific numerical inventories, hard local-null admission, finite memory and retained tails |
| `hf_wave_optics` | Exact scalar phase, analytic/quadrature agreement, causal FIR and continuous lens traversal, independent deep-fade witnesses |
| `hf_phase_screens` | Separated-screen propagation, scalar analytic limits, depth/mesh/extent refinement, continuous finite support and resolved off-grid nulls |
| `hf_native_chain` | Canonical TX/physical-channel/RX composition, clock and oscillator order, DAC/ADC, bounded buffers, complete tails and continuing RF interference |
| `hf_history` | 175 matched public-aperture block-history, projection, nuisance-rank and conditional-oracle checks |
| `hf_phase_bank` | Frozen phase dictionary, matched projection, uncertainty and boundary regression checks |
| `hf_public_statistics` | Exact effective-template FFT/direct-sum agreement, shared phase/FFT preparation parity, full priors, phase bounds and resource guards |
| `hf_phase_refinement` | Direct-DFT proposal/final-score agreement, fixed priors, boundaries, staged objectives and off-grid recovery |
| `hf_native_supplement` | Disabled-path parity, reserved mass, shared executor, line/quiet policy and invalidity |
| `hf_admitted_evidence` | Opt-in complete native model comparison, Off parity, coherent gain, retained line, cancellation and exact memory refusal |
| `hf_phase_filter` | Normalized log-domain forward sums, exhaustive independent path references, finite phase/frequency support, partial groups and tiny-state recovery |
| `hf_public_receiver` | Public arrival-grid closure, full prior mass, numerical support, telemetry isolation and memory admission |
| `hf_replay` | 208 finite measured-record support, interpolation and full-record trace-integrity checks |
| `hf_gannon_replay` | Exact long-record hashes/UTC grid, shared normalization, finite support, corruption rejection and measurement-noise confound |
| `impairment_observables` | Independent AR(1)/shot moments and covariance, resonator response, ADC and AGC recovery |
| `lunar_spectral` | Joint delay–Doppler covariance through 360 s, independent refinement, long integration and waveform parity |
| `integration` | Actual native source/receiver, physical completion, noise/EOF, recovery, HF history and numerical-admission boundaries |
| `runner` | Immutable identity, GUI and trajectory controls, known-binary/gain diagnostics, censoring and statistical reporting |
| `wave_receipts` | Digest-bound native fixtures, Off versus active numerical inventories, strict wave/endpoint admission and rejected provenance mutations |
| `ensemble` | Five checks for signed evidence, outer priors, null controls, canonical hashes, fixed brackets and censoring |
| `coverage_design` | Frozen physical sampling, independent holdout, sparse pair coverage and complete status accounting |
| `competitor_calibration` | Compiled calibration for the native channel bridge into upstream FLDigi/WSJT-X modes |
| `competitor_runner` | Six external-comparison manifest and receipt parser cases |

`build.sh configure` and `build.sh build` are also available. Compatible
compiler-cache entries are read without writing outside this subtree. The
approved generic native hook is covered by the fresh Triad check above;
the experimental receiver is enabled only by an explicit appraisal option.

## Reference experiments

Inspect actual native geometry before a long run:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --list
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode plan --scenario vlf-resonant --waveform reference --pulse tapered-rrc
```

Run the four main reference campaigns serially:

```sh
for campaign in propagation-breadth receiver-controls weak-holdouts eme-episodes; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

These 41 episodes took about nine minutes on the tested Ryzen 5 PRO 5650U,
using the full automatic 24-worker budget, with zero censored executions.
They include strong/weak comparisons, land/sea reference levels, distinct HF
profiles, recovery, Helper/noise/address controls, three fresh weak seeds per
selected stratum, and full EME messages. Their heterogeneous outcomes are not
pooled into a success probability. Consult each manifest's explicit per-episode
and aggregate caps; different hardware can reach those caps.

## Public phase-history continuation

Run a matched CFO/affine/nonlinear shadow comparison on a complete public
observation, without making receiver decisions:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode hf-search --scenario hf-vertical-eikonal-motion --waveform reference \
  --bit-seconds 128 --cn0 0 --seed 10201 --search-slot 1 --search-timing full \
  --max-wall-seconds 600
```

Enable the experimental branch inside the complete native receiver:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode receive --scenario hf-vertical-eikonal-motion --waveform reference \
  --message e --bit-seconds 128 --cn0 0 --seed 10201 \
  --hf-public-phase-receiver --capture-digest-slot 1 --max-wall-seconds 600
```

Replay the bounded ten-episode receiver campaign, including ordinary-receiver
comparisons, weaker signals, the off-grid trajectory and negative controls:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-public-receiver.json
```

This stage uses a configured calendar, one tap and IID proper-complex white
noise with known variance on Helper-off CPU. The fixed branch weight is 0.25;
it shares one native acquisition and retains the native codec and two scored quiet slots.
The original moving-ionosphere miss and the longer −6 dB-Hz case now recover
`e`; the latter uses 522 s rather than 139.2 s per DATA bit, a 3.75× physical
increase for the requested 4× duration. The off-grid case still loses its final
bit, with premature physical completion and an integrity rejection. These
observations do not establish a general sensitivity floor or an
orders-of-magnitude advantage. Read the [fixed protocol and bounds](hf-public-search.md)
and [source-bound outcomes and remaining gaps](hf-validation.md) before extending
the experiment.

## Expanded HF adverse cases

The original expansion added 14 HF presets beyond the five original quiet,
disturbed, transition, NVIS and DRM6 scenarios. They cover fitted Watterson
components; O/X splitting and single-antenna cancellation; coherent-trough
phase evolution; static and moving vertical-ionosphere eikonals; mode loss and
recovery; and five replays/ablations of one finite measured 12.7 MHz record.
The continuation adds `hf-vertical-eikonal-holdout`, a constructed trajectory
whose 137 s period and phase origin are absent from the original search bank,
plus two critical-frequency trajectories, an unresolved two-ray compound and
an original off-grid trajectory at a CFO prior edge.
The literature assumptions, counterevidence and domain limits are in
[hf-research.md](hf-research.md); case-specific observables and interpretation
limits are in [hf-experiments.md](hf-experiments.md).

Five initial native campaigns cover 88 episodes. All 88 completed without
execution failures or censoring. The table also lists five predeclared
follow-up tiers, all complete: phase-history (16), RS20/RS60 FEC (3),
equal-energy RS60 (1), default Helper-enabled O/X (2), and 2 s capture-lead
Helper controls (2). Outcomes are
conditional on the frozen models and seeds, not a population success rate:

| Manifest | Cases | Fixed budget |
| --- | ---: | --- |
| `hf-unusual.json` | 20 | 180 s per episode, 2,400 s aggregate, 8 million samples per episode |
| `hf-measured-replay.json` | 15 | 120 s per episode, 1,800 s aggregate, 1 million samples per episode |
| `hf-history-trend.json` | 18 | 240 s per episode, 2,400 s aggregate, 12 million samples per episode |
| `hf-sensitivity-brackets.json` | 30 | 60 s per episode, 900 s aggregate, 1 million samples per episode |
| `hf-common-text.json` | 5 | 600 s per episode, 3,000 s aggregate, 10 million samples per episode; completed |
| `hf-phase-followup.json` | 16 | 240 s per episode, 2,400 s aggregate, 12 million samples per episode; completed |
| `hf-native-fec.json` | 3 | 600 s per episode, 2,100 s aggregate, 25 million samples per episode; completed |
| `hf-fec-equal-energy.json` | 1 | 600 s trial, 660 s aggregate, 25 million sample cap; complete |
| `hf-helper-controls.json` | 2 | O/X cancellation with Helper at 20/0 dB-Hz; seed 10103, 180 s each, 420 s aggregate, 3 million samples each; completed |
| `hf-helper-noise-support.json` | 2 | AWGN Helper control (40 dB-Hz, seed 5489) and O/X Helper control (20 dB-Hz, seed 10103), 2 s capture lead; 180 s each, 420 s aggregate, 3 million samples each; complete |

Run them serially with:

```sh
for campaign in hf-unusual hf-measured-replay hf-history-trend hf-sensitivity-brackets hf-common-text; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

`hf-unusual` (20 episodes, 128.415 s) found correct completion in all eight
bright cases and none of eight 0 dB-Hz cases; one weak O/X split case reached a
premature physical endpoint. The mode-loss case also reached a premature
endpoint before later DATA returned. Its frozen raw slot trace has DATA at
slots 5–12; seven complete public apertures (slots 6–12) begin after the 300 s
mode return, since slot 5 starts at 299.9917 s. The old generic
`resumed_data_slots=0` field undercounts this; a mode-aware reporting regression
now covers the fix, but the episode itself was not rerun. Three negative
controls had no false DATA or physical completion.

`hf-measured-replay` (15 episodes, 6.455 s) returned the correct first-slot bit
in five bright counterfactuals and quiet in five 0 dB-Hz cases; its noisy
first-slot case was quiet. Four private oracle/history measurements are
known-channel diagnostics, not receiver outcomes. `hf-history-trend` (18
episodes, 137.180 s) used requested 16/64/128 s spans, corresponding to actual
DATA bit durations of 23.2/69.6/139.2 s. At 0 dB-Hz both static and moving
64 s requested cases completed correctly; neither 16 s case did, and the moving
128 s case did not complete correctly. At −6 dB-Hz none of the six receive
episodes completed correctly; the longest static and moving observations had
physical endpoints. The matched known-channel oracle error fell from about
9.27e−3 at the 16 s requested span to 1.60e−5 at 64 s and 2.06e−9 at 128 s.
These conditional oracle values are not operational receiver probabilities;
the durations share channel origins and are correlated.

`hf-sensitivity-brackets` (30 episodes, 42.646 s) found all static eikonal
cases correct at 4/8/12 dB-Hz, and all three fresh 8 dB-Hz static seeds passed.
Moving eikonal failed at 4 dB-Hz with a premature endpoint, while 8/12 and all
three fresh 8 dB-Hz seeds passed. Measured-complex and phase-only first-slot
cases were quiet at 4 dB-Hz and DATA at 8/12 and the three fresh 8 dB-Hz seeds;
amplitude-only first-slot cases returned DATA at all tested levels. These are
finite descriptive brackets; the slot cases do not test full messages and the
small holdouts do not establish a reliability rate.

`hf-common-text` (5 episodes, 236.150 s) completed AWGN `HELLO`, disturbed-HF
`HELLO`, and both O/X `HELLO` cases; quiet-HF `HELLO` at 8 dB-Hz did not
complete correctly despite a physical endpoint. Equal C/N0 does not imply equal
energy or latency, and WSPR remains a separate structured service.

All 112 frozen native episodes of the original HF expansion completed uncensored.
The added phase follow-up comprises 9 oracle diagnostics, 6 wanted full-message holdouts
(4 correct, 2 failed/rejected), and 1 noise-only receive case (no DATA or
physical completion). FEC, equal-energy, and Helper outcomes retain distinct
raw-clean, decoded-message, and acquisition meanings; the detailed receipts,
including failed receptions and fixture limits, are in [HF validation](hf-validation.md).
The one-faded-slot RS20 pilot and rejected Helper/eikonal preflight are separate
from the 112-episode count. Helper controls transmit `e`; neither
pair demonstrated a narrowed Helper frequency proposal. The 2 s AWGN case
passed coarse/onset support, then used final frequency fallback because its
frequency prior has zero width. The O/X cases found no coarse pilot, and the
longer O/X lead is not phase matched to the default. See the
validation record for details. At that checkpoint, the 13-group CTest run passed in 68.01 s,
including the one-faded-slot integration assertion and the new 2 s capture-lead
regression, with 40 runner unit checks. Current software checks are listed above.
Competitor channel/capture (36) and
replay (15) results remain separate from native modem outcomes.

```sh
for campaign in hf-phase-followup hf-native-fec hf-fec-equal-energy hf-helper-controls hf-helper-noise-support; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

The measured envelope's nominal last knot is 40.94 s,
while its first full public observation is 19.4375 s and its complete
message/calendar is 259.4375 s. It can test a complete first observation but
cannot provide a complete-message measured-replay result. The 20-second late
offset and alternate clock convention re-index the same recorded event; the
interpreting the rounded 41-second record duration gives 2048/41 samples per
second, approximately 49.9512 Hz. They are not independent replays. Its missing sample clock and instrument/filter
calibration prevent interpreting it as a broadband impulse response. The
independent external comparison framework is in
[`competitor/README.md`](competitor/README.md). Its ordinary FT8/WSPR
deep-standard settings remain the main comparison tier; optional stronger
settings are separate, and no tier is claimed as the supremum of all possible
receiver processing.

The deliberately longer VLF tier preserves the original slow RRC observation,
compares the explicitly selected tapered RRC pulse, tests weak static and
phase-varying conditions, and checks antenna detuning:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/vlf-reference.json
```

Its seven episodes have a 1,900-second aggregate cap; the original RRC slot
alone has a 1,200-second cap. The five-slot tapered message retains the
5,760.4-second DATA bit and all **64,806,069 samples** at 1,920 samples/s.
It completed correctly in 64.69 seconds of computation, including two scored
quiet slots. That timing includes source/channel work and is not a calibrated
receiver-capacity claim. Tapered RRC is also the canonical live shaping planner's
choice for this geometry; its input is explicitly different from the retained
RRC reference. No waveform, search hypothesis or physical clock is silently
changed to make a case finish.

`noise-ablation.json` contains three matched noise-only component diagnostics;
`eme-discriminator.json` contains a selected high-energy EME comparison and
same-aperture bit oracle. They diagnose observed failures without retuning the
receiver. The older exploratory `awgn-trend.json` remains a separate duration/
energy baseline; two seeds per cell cannot establish a smooth sensitivity floor.
`integration-smoke.json` is short-span wiring coverage, not reference sensitivity.
Superseded finite-grid lunar presets and the old weak-signal pilot manifest are
retired; their original results remain labeled historical in validation.

Each run creates a unique folder under ignored `local-evidence/`, containing
frozen inputs, exact source/binary identities, raw native JSON, a machine-readable
run report and Markdown summary. `--output-dir` must name a new directory under
that root. Communication failure, false DATA, wrong messages, physical completion,
and execution censoring are separate outcomes. A completed campaign means its
observations were collected, not that every communication succeeded.

### Weak physical long-alarm campaigns

The maintained
[two-hop long-alarm matrix](campaigns/two-hop-long-alarm-matrix.json) shares
one qualified immutable physical table across six matched rows: text `E` at
−3/+3 dB-Hz and raw `0`/`1` at −3/+3 dB-Hz. Each requested 128-second bit is
139.2 seconds in the current waveform. Raw rows contain five distinct attempts
at slots 0, 3, 6, 9 and 12, with one continuous channel/receiver within each
row; only the qualified physical table is shared between rows. It retains the
full `W=2*H` automatic worker budget, a 1,200-second aggregate cap and
4,350,000 samples per row.

The complementary
[physical random long-alarm pair](campaigns/physical-random-long-alarm.json)
uses the scalar changing-rate physical trajectory for both raw-bit polarities.
Each row has the same five-attempt spacing and actual 139.2-second bits; the
two-row campaign is capped at 180 seconds per row / 360 seconds total. The
polarities share one constructed physical history and are not independent
natural trials.

Run either frozen manifest with the usual native probe and a fresh output
directory under ignored `local-evidence/`:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-long-alarm-matrix.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-physical-alarm/two-hop-long-alarm-matrix

python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/physical-random-long-alarm.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-physical-alarm/physical-random-long-alarm
```

The [validation record](../../docs/validation.md#weak-physical-channel-alarm-investigation--16-november-2026)
keeps historical results, current native outcomes and runner validation status
separate; runner completion is not message success. Current physical-alarm
receipts and binaries are temporary evidence for the active matched diagnosis.
After disposition, remove superseded binaries and prototype outputs. Retain
unresolved failure receipts only until their evidence is summarized and the
associated revalidation duty ends.

### Additive-gain discriminator and conditional checks

The [additive-gain discriminator](campaigns/two-hop-additive-gain-discriminator.json)
is the frozen +3 dB-Hz matched test of the additive persistent-plus-OU gain
family. It validly increased wanted evidence but **did not repair** the missed
slot or complete `E`; retain that failed criterion as part of the result. The
[boundary campaign](campaigns/two-hop-additive-boundary.json) required a +3
baseline parity row before its matched +4 dB-Hz existing/additive pair. Its
first invocation hit the runner's file-size limit before either +4 receive;
the resource-corrected run reproduced the +3 baseline and completed the paired
replays. On that same capture, the existing model remained 12/13 while the
additive model received 13/13 and completed `E`. This is one fixed-history
endpoint gain; it does not repair the +3 gate, establish a sensitivity
threshold, or qualify broad field reliability. See the
[source-bound validation record](../../docs/validation.md#weak-physical-channel-alarm-investigation--16-november-2026)
for full hashes, costs, failure provenance and limits.

That endpoint gain activated the
[fresh long-noise control](campaigns/physical-long-additive-noise.json),
which must retain all-quiet/no-message behavior while executing the additive
family. Final-source checks complete: the six-row physical matrix preserves
its original decisions and weak misses, the changing-rate pair retains 5/5
correct raw bits for both polarities, and all fifteen noise apertures stay
quiet with the additive bank active. The K2 factor repair and the removal of
the obsolete 32-second duration cutoff also pass their scoped checks; broad
field reliability remains unqualified. Use a fresh `--output-dir` for each
campaign run (under ignored `local-evidence/`):

```sh
run_id=choose-a-new-unique-run-id

python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-additive-gain-discriminator.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-physical-alarm/${run_id}-additive-gain

python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-additive-boundary.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-physical-alarm/${run_id}-additive-boundary

python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/physical-long-additive-noise.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-physical-alarm/${run_id}-additive-noise
```

These binaries and receipts are temporary validation artifacts, not additional
operating implementations. Superseded prototype binaries, duplicate setup
outputs and the retired 8/32-model trial outputs have been removed after their
findings were summarized. Keep the active counterfactual/final binary pair and
compact raw receipts while they support current qualification or unresolved
weak points. Remove them once superseded or their release-assurance duty ends;
keep compatible build caches that still serve current work.

### HF Worst severe-spread regression

The maintained `hf-disturbed` profile has two independent proper-complex
Gaussian rays at 0/7 ms, each with a 30 Hz full 2σ Doppler width. It is a
standard severe statistical profile, not a plasma reconstruction or the GUI's
bounded frequency-wander law. The [profile distinctions](../../docs/channel-models.md#distinguish-hf-stress-profiles)
and [current results](../../docs/validation.md#hf-worst-30-hz--7-ms-follow-through--16-november-2026)
record why the earlier small-spread physical episode cannot qualify this case.

Three frozen manifests exercise the canonical receiver: the
[matched raw pair](campaigns/hf-disturbed-long-alarm.json),
[fresh-history raw pair](campaigns/hf-disturbed-long-alarm-fresh-history.json),
and [noise control](campaigns/hf-disturbed-long-alarm-noise-control.json).
Each wanted row sends five separate 139.2-second bits, retaining one channel
history and receiver through 15 slots with two scored quiet slots per attempt.
The two polarities share a seed; they are not independent natural histories.
The noise control scores five ordinary slots with no transmitted signal.
Each row is capped at 180 seconds and 4,350,000 samples, with automatic W=2H.
Use a new output directory and inspect the raw decisions: characterization
mode deliberately preserves valid misses rather than declaring them execution
failures.

```sh
run_id=choose-a-new-unique-run-id
for campaign in hf-disturbed-long-alarm hf-disturbed-long-alarm-fresh-history hf-disturbed-long-alarm-noise-control; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" \
    --output-dir "_appraisal/highFidelity-triple_Robust_Modem/local-evidence/${run_id}-${campaign}" || break
done
```

Three bounded native appraisal hooks retain the same long first-aperture
checks with broader GUI-like declarations and common-frequency motion:

```sh
for witness in --hf-nominal-gain-witness --hf-nominal-gain-witness-bit-one --hf-nominal-gain-witness-motion; do
  timeout -k 0.5s 119.5s \
    _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --mode slot --scenario hf-disturbed --waveform reference --seed 9202 \
    --cn0 12 --bit-seconds 128 --backend cpu --control wanted --fec off \
    --max-samples 270000 --max-wall-seconds 119.5 --capture-digest-slot 0 \
    "$witness" > "_appraisal/highFidelity-triple_Robust_Modem/local-evidence/${run_id}${witness}.json" || break
done
```

The first hook scores bit zero with the statistical declaration, GUI-like
Margin 3 declaration (21 ms delay, 90 Hz relative Doppler and ±90 Hz common
frequency allowance), and a +30 Hz unitary rotation. The bit-one hook scores
the latter two controls for bit one. The motion hook applies the existing
native `BoundedRandomSpread` phase law (±30 Hz, one-second knots, seed 70031)
and makes one GUI-like decision for bit zero. These rotations affect both
signal and circular white noise; their noise law is unchanged, but this is not
an exact replay with unrotated noise. The receiver never sees the trajectory.
The hooks require the frozen settings and use the ordinary native decisions,
full public priors and memory admission. They do not execute a separate
receiver algorithm or assert natural occurrence probabilities.

Expected current invariants are correct DATA in every wanted control, complete
valid apertures, active bounded gain/frequency telemetry, five correct raw
attempts and quiet pairs per wanted stream, and five QUIET decisions in the
noise control. Preserve failures and source/configuration/input hashes when
repeating; a successful command alone is not a reception pass. The current
validation binds the measured native source `3ae11493…`, appraisal source
`282c817a…`, and pinned binary `e82e8ab2…`; subsequent source changes require
their own applicable evidence. These are nominal-epoch/clock witnesses, not a
qualification of arbitrary clock age, every delay inside the declared bound,
all paths, processor capacity or multi-day availability.

Keep these three hooks while they are the maintained repeatable regression
path for the severe-spread, broader-bound and moving-frequency cases. Remove
them when equivalent generic paired-IQ controls replace them or the receive
model is retired. Current baseline/candidate pins and compact receipts under
`local-evidence/current-hf-worst/` support the source-bound matched comparison;
remove them once superseded or their validation duty ends. Retire diagnostic
prototype executables and scratch implementations after their positive and
negative findings are distilled into the maintained validation record.

## Native modes and bounds

- `plan` reports exact public geometry, source assumptions, workspace and sample/
  numerical-horizon admission without rendering.
- `channel` renders a bounded physical-time component.
- `receive` evaluates the complete configured message/calendar through physical
  quiet completion. EOF cannot count as successful completion.
- `slot` observes the complete first public aperture. It preserves the canonical
  source sequence and cannot claim full-message completion.
- `diagnose` measures full-prior VLF acquisition and an abbreviated persistent
  fine session from the complete first aperture. It makes no bit decision or
  complete-service capacity claim.
- `oracle` computes eligible AWGN/EME known-waveform, known-channel binary and
  presence references; `--oracle-slot N` selects a public bit. These do not pay
  blind acquisition or unknown-channel estimation and are not operational bounds.

The runner supports these modes plus explicit pulse, message, chunk, selected
oracle slot and bounded per-episode resource overrides. Pulse defaults to RRC;
`tapered-rrc` is always explicit. `duration_seconds` controls channel rendering;
receive geometry comes from the canonical calendar and message. Never compress
that clock or infer completion from the transmitted length.

The full-episode lunar renderer supports the declared 5.76 GHz, 1,920 Hz,
Helper-off source within **360 physical seconds** and its validated spectral
allowance. Unsupported longer/Helper configurations are rejected. The default
five-slot message takes 269.466 seconds. Shared spectral draws preserve joint
frequency/time structure and moving-delay effects; short blocks are not
independently re-randomized. Numerical agreement is reported separately from
physical model validity and receiver success.

## Optional cloud execution and evidence lifecycle

`cloud-bundle.py` creates a manifest-verified native source closure of about
2 MB, excluding credentials, builds and captures. `cloud-build.sh --help`
describes the pinned official CUDA development image, exact full-GPU checks and
cache recipe. Prefer a full RTX PRO 6000 Blackwell when acceleration is useful;
`--backend cuda|vulkan|gpu` must execute the requested device or report unavailable.
A prior RunPod campaign executed CUDA on a full RTX PRO 6000 Blackwell Server
Edition but was slower than CPU on its short paired fixtures. The subsequent
pod failed during provider container creation and was removed before any new
workload ran. The new complete reference results are local CPU measurements.
No cloud resource is retained by this appraisal.

Keep the current build/cache and compact receipts while their explicit
validation or failure-diagnosis purpose remains active. Remove superseded
executables, scratch sources and duplicate captures after recording their
conclusions. Preserve unresolved failure cases and exact provenance until a
replacement experiment resolves the same question. Do not retain whole obsolete
campaigns or copied source trees for speculative future use.

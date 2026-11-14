# High-fidelity triple Robust Modem appraisal

This contained suite exercises the canonical native modem against calculated,
reference-reproduced, standard-statistical and explicitly synthetic channel
hypotheses for **sub-9 kHz / 200 ft antennas, 1 MHz groundwave, 30 MHz
groundwave, 1–30 MHz skywave, and Earth–Moon–Earth**. All appraisal code,
fixtures, documentation and generated evidence remain in this directory.
Python launches native executables and analyzes receipts; it implements no DSP.

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
| `hf_design` | Fixed whole-domain phase/group bounds, strict physical coefficients, seed/placement isolation and mutation order |
| `hf_gui_profile` | Canonical preset preparation, actual endpoint clock errors, margin semantics, Helper, noise-seed isolation and rejection |
| `hf_fullwave` | Independent zero-index, Fresnel/slab, Joule-loss and smooth-barrier references; passive opaque layers and unresolved resonances |
| `hf_fullwave_kernel` | Causal support, continuous-history interpolation, finite-band error gates and independent off-grid rejection of a frequency-sampling delay alias |
| `hf_magnetoionic` | Cold vector-plasma scattering, independent Maxwell/analytic reductions, passivity, field reversal and singular rejection |
| `hf_magnetoionic_kernel` | Fixed-basis interpolation, path-specific numerical inventories, hard local-null admission, finite memory and retained tails |
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

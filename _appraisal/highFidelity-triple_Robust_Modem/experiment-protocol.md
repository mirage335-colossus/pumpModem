# Native high-fidelity appraisal protocol

Updated 13 November 2026. The contained appraisal has a native renderer,
scenario catalog, executable, bounded campaign runner and 13 CTest groups.
The latest full 13-group check passed in 68.01 seconds.
The [model inventory](models.md) records implemented equations and
limits; the [research reconciliation](research-reconciliation.md) maps them to
the literature. The added [HF research synthesis](hf-research.md) reconciles
measured, fitted, analytic and contrary evidence; the [HF experiment map](hf-experiments.md)
states its receiver questions and limits. This protocol governs runs and the
claims that their results can support. Passing source tests verifies selected
software behavior only.

## Questions and evidence layers

Keep four claims separate:

1. **Implementation correctness:** does the C++ renderer implement its
   declared transfer, noise and front-end rules with bounded state and resources?
2. **Model fidelity:** does the selected rule reproduce a source observable or
   independent measurement using the same estimator, bandwidth and reference
   plane? Is its domain sufficient for the claim?
3. **Modem behavior:** does the canonical native complete receiver receive,
   erase, reject, physically complete and reacquire on the declared episode?
4. **Computational capacity:** can the complete receiver sustain a configured
   incoming stream and workspace on an identified processor?

Evidence for one item does not prove the next. A simulation outcome is
conditional on its channel model. None of the current implementation's fixed
references is a local environmental calibration. Unknown epoch, ordinal,
timing and address acquisition is outside the receiver's current configured
calendar contract. Capacity is a separate local same-stream measurement under
the [Robust capacity policy](../../docs/robust-manual-capacity.md).

## Implemented model families and fidelity labels

Scenarios are declared in [`src/scenarios.cpp`](src/scenarios.cpp) and
[`src/hf_scenarios.cpp`](src/hf_scenarios.cpp); each native
receipt includes scenario provenance, assumptions, public receiver bounds,
source/build identity, actual geometry, and reported channel/receiver work.
The `reference` waveform preserves the canonical physical duration; `smoke`
uses shorter integration geometry for fast wiring checks.

| Family | Current reference case | Claim the current model can support | Critical limit |
| --- | --- | --- | --- |
| Sub-9 kHz / 200 ft | `vlf-resonant`, `vlf-detuned`, `vlf-phase-wander` | Calculated high-Q antenna envelope, conditional magnetic-noise level, and a separate smooth phase-wander hypothesis | No LWPC/LMP solve; independent electric and magnetic planes; phase law is constructed, not a measured 8.27 kHz record |
| 1 MHz groundwave | `groundwave-1mhz`, `-sea`, `-night` | Reference-reproduced NTIA LFMF homogeneous magnitude, plus separately synthetic phase and competing ray | No measured local path, terrain or complex phase; night ray is a sensitivity choice |
| 30 MHz groundwave | `groundwave-30mhz`, `-land` | Fixed LFMF magnitude comparison at model endpoint | Formal model endpoint does not establish actual surface-wave dominance or exclude direct/reflected/sky/duct paths |
| 1–30 MHz skywave | Original `hf-quiet`, `hf-disturbed`, `hf-transition`, `hf-nvis-disturbed`, `hf-drm6`; 14 additional Watterson, O/X, coherent-trough, vertical-eikonal, mode-transition and measured-replay presets | Continuous finite Gaussian-tap approximations, scalar fitted comparisons, vector mode projections, coupled analytic phase/group delay, constructed mode/fade schedules, and a finite deposited complex-envelope record | Not a worldwide or long-episode population; the NVIS fixture is skywave-only and omits the optional groundwave component; the measured record has no sample clock and cannot support a broadband impulse-response or full-message claim |
| EME at 5.76 GHz | `eme-disk-episode`, `eme-patch-episode`, `eme-severe-episode` | Joint spectral approximation of the frozen beam-weighted monostatic delay–Doppler kernel, numerically checked through 360 s | Helper-off 1,920 Hz source only; not full retarded bistatic DE440 geometry or measured whole-disk IQ; legacy finite-grid and first-slot cases retain their narrower limits |
| Noise/equipment controls | `noise-bursts`, `frontend-overload` | Constructed colored/impulse and blocker/AGC/clipping/quantization paths | Not a site capture, lightning law or named radio's RF nonlinearity |

The detailed parameters, units, energy conventions and numerical acceptance
limits are in [models.md](models.md). An “adverse,” “weak,” or “outage” label
names the specified conditional model; it does not imply environmental
probability. Do not combine maxima from unrelated references and call the
result a measured natural episode.

The 14 added HF preset identifiers, their independent observables and
limits are listed in [hf-experiments.md](hf-experiments.md). The source review
in [hf-research.md](hf-research.md) distinguishes measured records, historical
fits, analytic models, and deliberate counterexamples. The measured replay
supports a complete first observation of 19.4375 s within its nominal 40.94 s
last knot, but the 259.4375 s complete message exceeds that record. Its
20-second offset and alternate clock convention are two views of one event,
not independent episodes. Interpreting the rounded 41-second record duration gives 2048/41 samples per
second, approximately 49.9512 Hz.

The HF tuple review checked [F.1487 Annex 3 §3.4](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)
and the newer [ETSI ES 201 980 V4.3.1 Annex B.1](https://www.etsi.org/deliver/etsi_es/201900_201999/201980/04.03.01_60/es_201980v040301p.pdf):
the selected DRM-6 tuple and Gaussian 2-sigma
width convention remain unchanged from the fixed V4.2.1 fixture. This is a
source cross-check, not evidence that these standard profiles bound every
natural channel. The maintained [model notes](models.md#hf-standard-and-nonstationary-controls)
and [Fable skywave synthesis](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md)
retain the scope of the comparisons.

## Entry points and bounded campaign files

Run from the repository root. The standalone wrapper configures/builds under
the appraisal subtree and reads any shared compiler cache in read-only mode:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

The latest full suite passed all 13 CTest groups in 68.01 seconds; see the
[final log](.build/hf-expansion-complete-checks.log). The run includes the
2-second Helper capture-lead and one-faded-slot RS20 integration assertions,
40 runner checks, and the replay, history, and competitor checks. The former seven-group
35.25-second run is historical, not a result for the expanded suite. The
current groups cover renderer primitives and scenario models, HF
profile/eikonal/history/phase-bank/replay checks, impairment observables, lunar spectral
numerics, native receiver integration, the campaign runner, compiled competitor
calibration, and competitor manifest parsing. A prior separate
canonical Triad run passed 32/32 checks in 64.90 seconds. Its contained test harness
forced inclusion of a header missing from the pre-existing CPU-only canonical
probe; the original setup failure is preserved. This was a test-harness
workaround, not a modem source change. The canonical native source SHA is
unchanged in this expansion; the prior result remains bound to its original
build and is not a fresh run. Smoke, exploratory AWGN, weak-signal,
RunPod, EME-slot and VLF results are recorded in [validation](validation.md),
with old three-quiet-opportunity outcomes separated from current
two-quiet-slot evidence. None implies environmental qualification.

The executable's supported modes are:

- `--mode plan`: prepare canonical source/calendar/receiver settings and print
  full physical horizon, sample-budget fit, actual geometry, public bounds and
  provenance. This is the required cheap preflight for long cases.
- `--mode channel`: render/report a bounded source span without modem scoring.
- `--mode receive`: run a continuous stream through the canonical native
  receiver. Transport EOF does not create physical completion.
- `--mode slot`: receive only the complete first public DATA observation; it
  cannot establish message completion or scored quiet-slot behavior.
- `--mode diagnose`: render the complete first static VLF reference observation,
  run canonical acquisition over the full public hypotheses, and measure a
  short persistent fine session. This component diagnostic makes no bit or
  message decision and cannot qualify complete receiver service.
- `--mode oracle`: compare known binary waveforms for eligible Helper-off AWGN
  or EME scenarios with white noise, and calculate an ideal known-signal
  presence probability at the receiver's design false-alarm target. Both
  calculations assume known signal/channel details and omit blind acquisition,
  realized false-alarm measurement and complete reception.

Use `--list` to inspect the full scenario records and `--help` for the options
on the compiled binary:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --list
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --mode plan --scenario vlf-resonant --waveform reference
```

`run.py` accepts a frozen JSON campaign and starts one native process per
episode. It validates inputs and receipt identity, bounds per-trial and total
wall time, sample count, each output stream (8 MiB), and total retained
campaign output (256 MiB). Python does not generate waveforms or perform
channel/receiver DSP. Example smoke invocation:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/integration-smoke.json
```

The manifest supports all six modes and explicit `pulse`, `oracle_slot`,
`chunk`, and `message` fields. `oracle_slot` is an oracle-only public slot
index; `duration_seconds` sets the channel-render span and does not shorten a
receive episode. `--pulse rrc|tapered-rrc` selects an explicit canonical pulse;
the default remains `rrc`. Receipts bind the requested and effective message,
pulse and public geometry. The existing fade/gap control changes the default
requested `e` to effective `A` so that the interruption is interior; that
documented substitution is checked rather than silently accepted for other
cases. Chunk changes must retain the channel's continuous state.

Use these test tiers to keep future checks affordable:

| Tier / manifest | Frozen cases | Purpose and maximum budget |
| --- | ---: | --- |
| `build.sh test` | 13 CTest groups | All groups passed in 68.01 s, including fade-option and 2 s Helper capture-lead integration assertions; no long VLF reception |
| `integration-smoke.json` | 10 | Short geometry and wiring; 600 s aggregate |
| `propagation-breadth.json` | 16 | Groundwave land/sea defaults, weak levels, HF profiles and recovery; 120 s per episode, 1,800 s aggregate, 8 million samples per episode |
| `receiver-controls.json` | 12 | Noise, blockers, Helper, mismatched hypotheses, physical fade, marked gap and EOF; the same 120 s / 1,800 s / 8 million ceilings |
| `weak-holdouts.json` | 6 | Three fresh seeds each for two fixed weak conditions; the same ceilings; descriptive only |
| `eme-episodes.json` | 7 | Disk, patch and severe-width strong/weak full-message pairs plus noise-only; 120 s per episode, 1,000 s aggregate, 1 million samples per episode |
| `vlf-reference.json` | 7 | Two phase-cost diagnostics, a bright tapered complete message, weak static/wander observations, detuned rendering and the original RRC observation; 1,900 s aggregate with explicit 40–1,200 s episode caps and at most 70 million samples |
| `hf-unusual.json` | 20 | Fitted/profile/O-X and mode-recovery adverse comparisons; 180 s per episode, 2,400 s aggregate, 8 million samples per episode |
| `hf-measured-replay.json` | 15 | Finite measured-envelope, ablation, clock and aperture diagnostics; 120 s per episode, 1,800 s aggregate, 1 million samples per episode |
| `hf-history-trend.json` | 18 | Static/moving ionosphere across integration durations and signal levels; 240 s per episode, 2,400 s aggregate, 12 million samples per episode |
| `hf-sensitivity-brackets.json` | 30 | Frozen intermediate 4/8/12 dB-Hz brackets for static/moving eikonal and measured complex/amplitude/phase records; 60 s per episode, 900 s aggregate, 1 million samples per episode |
| `hf-common-text.json` | 5 | Fixed Robust `HELLO` operating points and O/X contrast before energy normalization; 600 s per episode, 3,000 s aggregate, 10 million samples per episode |
| `hf-phase-followup.json` | 16 | Frozen phase-bank/oracle and fixed-seed receiver follow-ups; 240 s per episode, 2,400 s aggregate, 12 million samples per episode |
| `hf-native-fec.json` | 3 | RS20/RS60 coded delivery on AWGN/HF; 600 s per episode, 2,100 s aggregate, 25 million samples per episode; compare with raw-clean/FEC-off endpoint |
| `hf-fec-equal-energy.json` | 1 | Frozen seed-matched RS60/raw-clean energy comparison; 600 s trial, 660 s aggregate, 25 million sample cap; complete |
| `hf-helper-controls.json` | 2 | Default 0.25 s capture-lead O/X Helper controls; 180 s each, 420 s aggregate, 3 million samples each; complete |
| `hf-helper-noise-support.json` | 2 | AWGN/OX Helper controls at 2 s capture lead; 180 s each, 420 s aggregate, 3 million samples each; complete |
| `noise-ablation.json` | 3 | Selected seed-matched noise-only component diagnosis; not independent false-alarm confirmation |
| `eme-discriminator.json` | 2 | Selected high-energy receiver and known-channel bit diagnostic after a wrong accepted EME bit |

The five added HF campaigns have their own fixed caps and run serially through
the same native executable:

```sh
for campaign in hf-unusual hf-measured-replay hf-history-trend hf-sensitivity-brackets hf-common-text; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

The phase-history, native-FEC, equal-energy, default 0.25 s Helper-enabled O/X,
and 2 s Helper capture-lead campaigns are complete:

```sh
for campaign in hf-phase-followup hf-native-fec hf-fec-equal-energy hf-helper-controls hf-helper-noise-support; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

The broader reference and long VLF tiers are explicit experiments, not part of
the default correctness command. These manifests are frozen before execution;
their presence is not a passed result. Consult [coverage](coverage.md) and
[validation](validation.md) for source-bound outcomes, incomplete work and
censoring. Do not retry a censored case automatically or expand the sample
budget after observing its outcome. A repeated phase diagnostic remains a
component measurement, not an independent communication trial.

All 112 frozen native HF campaign episodes completed uncensored. The 16-case
phase follow-up contains 9 oracle diagnostics, 6 wanted full-message holdouts
(4 correct and 2 failed/rejected), and 1 noise-only receive case with no DATA
or physical completion. The three FEC cases include an RS20 rejection after
seven erasures and an RS60 exact `HELLO` decode after repairing ten erasures,
although raw-clean reception was false. The equal-energy RS60 case had 100
quiet active-bit decisions out of 184 (84 DATA decisions), 26 rejected codec
fragments, and no accepted message. Both Helper control manifests transmit `e`.
Neither pair produced a narrowed Helper frequency proposal. The 2 s AWGN case
passed coarse/onset support but used final frequency fallback because its prior
has zero width; O/X cases found no coarse pilot. Bright messages completed
through the public DATA search. The 2 s O/X lead
changes absolute channel phase and is not a phase-matched ablation. These are
conditional mechanism observations, not pooled reliability estimates. Detailed
receipts and interpretation limits are consolidated in [HF validation](hf-validation.md).

The separate one-faded-slot RS20 pilot is not included in the 112 episodes; its
integration assertion passed in the final 13-group CTest run (68.01 s). Helper
on the eikonal profile was rejected at numerical preflight and is unsupported
geometry, not a natural-channel reception failure. The original 53 broad-appraisal
episodes, competitor channel/capture (36) and replay (15) cases remain distinct
and are not pooled with the HF campaigns.
See the [HF experiment map](hf-experiments.md) for frozen questions and
interpretation limits. The external framework exercises PSK31, Olivia, FT8 and WSPR as distinct
services. Its ordinary FT8/WSPR deep-standard settings remain the default
comparison tier; optional stronger settings are separate. The ordinary tier
is not asserted to be the global best achievable decoder configuration.

The integration manifest has ten episodes and a ten-minute total budget. Five
short `channel` episodes touch the five requested environment families; the
remaining controls compare smoke AWGN, noise-only, input EOF and Helper-on
reception. It is harness coverage. It cannot be used for reference-waveform
sensitivity or physical-model calibration.

The historical 12-episode `weak-signal-pilot.json` used the reference waveform
(the superseded maintained manifest is retired; its frozen receipt remains),
one wanted episode for each environment family, four selected interior
fade/resume cases, a paired AWGN duration contrast and a capture-gap control.
It declares a 15-minute aggregate budget, 180 seconds per trial, and
80,000,000 samples per trial. These are bounded pilot settings, not universal
limits or a receiver capacity result. The `cn0_db_hz` episode override changes
the native white-noise term at the stated normalized DATA reference; it does
not change the scenario's colored/impulse terms or assert a physical RF power
budget. All 12 pilot episodes ran: both groundwave wanted cases, disturbed HF,
and both AWGN duration cases completed; the full-message EME wanted case
had no physical completion at 10 dB-Hz, while VLF wanted and EME outage hit their wall
limits. This is failure-finding evidence, not a success probability or current
receiver qualification. The older appraisal observation horizon included an
unnecessary third quiet opportunity; this was a driver/horizon correction,
not a `CompleteStreamReceiver` algorithm change.

The corrected VLF full-message plan is a material admission limit: at 1,920
samples/s the three-bit `e` fixture occupies a **64,806,069-sample,
five-public-slot complete horizon**, including two scored quiet slots. Its
single DATA bit lasts 5,760.4 seconds. Historical CPU and CUDA one-slot attempts
at C/N0=20 dB-Hz each accepted 12,966,069 samples but timed out before scoring any slot,
after 120 and 180 seconds respectively. CUDA executed 2,533 FFTs and
331,415,552 FFT rows without scoring a DATA slot. These are censored compute
observations, not sensitivity failures or a VLF floor. A previous
pre-correction CPU full-horizon attempt also timed out, with six slots and
77,766,069 samples. Preserve old and corrected geometries separately; do not
shorten the bit, omit silence/quiet slots, or speed up physical time to force a
pass.

The expanded VLF campaign preserves that RRC observation and adds an explicitly
labeled tapered-RRC comparison, as used by the native live shaping planner for
this configuration. The pulse choice leaves the bit and public observation
durations unchanged. Short diagnostic continuation costs inform bounded run
allowances only; a complete-message result still requires the complete
five-slot horizon. The weak static and phase-wander cases are first-observation
tests, not full-message sensitivity claims.

Any explicit `cuda`, `vulkan` or `gpu` request reports execution/fallback. A
compatible build flag is not device evidence, and CPU work cannot be called GPU
work. The local build disables CUDA and Vulkan. A separate RunPod RTX PRO 6000
Blackwell Server Edition run verified CUDA execution on three matched CPU/CUDA
episode pairs, but CUDA was slower in all three and sustained utilization was
not measured. This does not qualify speedup or capacity. Include setup, upload,
build, execution and retrieval in future cost and turnaround decisions; do not
rent merely to run cheap deterministic tests.

## Channel validation before receiver interpretation

The native tests exercise deterministic transfer and state behavior, scenario
tuples, mathematical bounds, source identity, and a small complete receiver
path. The impairment checks use independent AR(1) and shot-noise moment and
covariance formulas, a Green-function resonator reference with quadrature
refinement, ADC grid/saturation checks, and a scheduled blocker-removal AGC
recovery fixture. The latter is distinct from the catalog's continuous blocker.
The phase-wander checks retain chronology through silence and compare the
constructed phase and Doppler analytically.

The `eme-*-episode` model uses one joint delay–Doppler spectral realization
throughout the captured calendar, with common draws across frequencies and
time. Independent ring/Bessel covariance and off-lattice projected-average
checks cover the declared 360 s horizon; a same-seed moving-facet reference
also checks actual FFT/interpolation samples and envelope drift. The initial
16-node conditional quadrature failed the original covariance gate at a wide
frequency separation. Increasing it to 32 nodes passed that unchanged gate;
the sampled covariance discrepancy is at most 0.000304 in normalized wide-beam
power units and the projected-variance discrepancy is at most 0.00654 dB.
Reference-refinement assertions remain part of the test. These are sampled
numerical consistency limits, not arbitrary-window bounds, measured channel
validation, or a receiver sensitivity allowance.

The native spectral renderer supports the 1,920 Hz Helper-off DATA source only
and rejects a horizon beyond 360 s. The default five-slot reference `e` calendar
is about 269.47 s and fits; a longer message does not acquire support merely
because a larger sample budget is available. The 360 s numerical limit is not
an instruction to compress or extend the receiver's physical calendar. Helper
requests for this model report unavailable. See the full
[numerical construction and limits](models.md#joint-spectral-synthesis-over-a-complete-calendar).

For the legacy finite-cell renderer, the lunar endpoint-dot-product cache
reuses the exact two neighboring interpolation endpoints for cells sharing an initial delay; it
retains each cell, delay trajectory and phase, differing from direct
interpolation only by floating-point operation order. For a bounded 4-second,
1,920-sample/s, 4,096-cell component case, direct rendering took 1.722 seconds
and cached rendering 0.681 seconds, while workspace rose from 2.61 to 4.81
MiB. A 16-single-path comparison differed by less than `3e-14`. These timings
exclude canonical source preparation and receiver work; they support this
specific exact reuse optimization, not a complete-message or general EME
performance claim. Extend validation only when a selected question needs it.

The retired 65,536-cell Doppler-aligned `eme-disk-slot` and `eme-patch-slot`
also have tested sampled projected-variance/covariance comparisons at 0, ±5,
and ±15 Hz for the 88 Hz profiles; numerical results and refinement limits are
in [models](models.md). The legacy 164 Hz finite-grid control remains
inadequately converged; it must not inherit the new spectral model's numerical
checks. A historical bounded CPU episode at
C/N0=30 dB-Hz received the first 26.4-second DATA observation as bit 0 at
29.466 seconds physical time. Rendering 56,575 output samples took 301.462
seconds; receiver control/wait took 2.196 seconds, for 303.713 seconds total.
This validates a single selected DATA observation, not complete-message
reception, a sensitivity floor, or receiver throughput. A matched C/N0=10
episode on the same 65,536-cell path was ambiguous despite high ideal-knowledge
detectability; see the precise metrics and limits in [validation](validation.md).
The older 4,096-cell bright EME case at the same nominal
C/N0 physically completed but decoded bit 1 and message `a` instead of `e`;
these different quadratures were not a matched comparison, so this result does
not by itself establish the cause. Its known-channel/known-waveform selected
slot oracle had squared distance 19,595.898, signal energies about 24.6 seconds
and noise variance 1.92, yielding an ideal binary error below the floating-point
representation's range. That oracle omits blind acquisition and receiver false-alarm
constraints; it is not a complete-reception bound.

At a minimum, a channel report should address the observable used to justify it:

- Path models: gain/power, phase convention, delay/tail support, frequency
  response and delay–Doppler joint structure.
- Stochastic taps: stationary initialization, spectrum-width convention,
  covariance versus time/frequency lag, finite-record uncertainty, and
  cross-frequency dependence.
- Noise/interference: separate signal and noise energies, calibrated PSD and
  APD, correlation and event intervals, with front-end recovery separately
  measured.
- EME quadrature: input surface law, beam and polarization, integrated power,
  delay and Doppler observables, grid refinement and the declared long-lag
  discrepancy.
- VLF antenna envelope: resonance/detuning and turn-on/turn-off limits against
  the declared circuit; this does not validate propagation or a complete
  physical antenna installation.

Where possible, compare a held-out measurement episode or geometry. Keep its
reference plane, detection threshold, estimator, bandwidth and censoring
convention. When measurements are unavailable, label the result a mathematical
check, standard/reference reproduction, or synthetic hypothesis. A published
power curve does not supply phase; a radar footprint does not supply a
whole-disk communication channel; and a simulation-to-simulation match is not
field validation. The [research reconciliation](research-reconciliation.md)
preserves the relevant contrary findings.

## Receiver endpoints and physical truth

Each wanted message is produced by native `Emission` and the canonical bounded
message interface. The channel transforms the complete composite source,
including DATA, Helper when enabled, silence and delayed tails. `CompleteStreamReceiver`
receives only samples and declared validity, plus public profile/calendar,
receiver noise and resource configuration. The evaluator alone knows the
transmitted truth and final observation horizon. No source bit, future fade,
known source endpoint or transport status is disclosed to score or stop the
receiver.

Keep these outcomes distinct: correct complete message; wrong bits or false
DATA; erasure/ambiguous evidence; integrity rejection; accepted truncation;
physical outage; premature completion during a fade; valid quiet-slot
completion; missed/delayed completion; reacquisition after resumed DATA;
invalid capture gap; resource/workspace rejection; and runner timeout. The
production two-scored-quiet-slot rule determines completion. A message ending
inside a physical fade is not known-complete; if transmission resumes, retain
the interior slots as erasures as required by the modem contract.

Paired controls should isolate the mechanism: identity/AWGN, noise-only,
wrong-key or wrong-ordinal, Helper off/on, physical outage while valid noise
continues, capture invalidity, genuine final quiet and EOF without quiet. Do
not multiply a Cartesian product of every control. Use the smallest counterexample
that can distinguish the question under study. Wrong-key and wrong-ordinal
controls test false acceptance; they do not qualify unknown-identity acquisition.

## Sensitivity, duration and statistical claims

Declare the sensitivity endpoint before observing outcomes. For example, define
a target complete-message probability and estimate the conditional required
input level only when independent episode sampling and a fixed inferential
design are available. A receiver sensitivity sweep can otherwise report only
per-case completions and failures. Pair a level curve with a matched ideal or
reference receiver where possible, and sweep near the observed transition
rather than insist the real channel follows a smooth AWGN curve. Fading outage,
identifiability loss and finite-block code thresholds can create real cliffs.

For a fair energy trend, freeze the transmitted native source, bit/slot
geometry, channel episode, receiver hypotheses, integration time and noise
reference plane. Change only the declared wanted-energy or noise variable. Do
not per-window normalize the realized channel. Report actual transmitted and
received signal-energy sums, additive-noise energy, normalized reference,
completion outcome, time-to-completion including failures/timeouts, receiver
work and resource admission. A hypothetical bound is not delivered modem
sensitivity.

The current campaigns have fixed, intentionally diagnostic seeds. Their cases
are paired comparisons, not an iid sample of nature. Bits, bursts, quiet slots
and repeat timing runs within an episode are dependent. The smoke, weak-signal
pilot and five expanded manifests define **no confirmatory stratum**, so the
campaign runner emits no binomial confidence interval for them. In particular,
the three-seed weak holdouts are descriptive failure-finding cases, not a
reliability estimate. If a conditional synthetic success
probability is needed later, predeclare a homogeneous stratum, independently
sampled complete episodes and a fixed sample count with the source's exact
Clopper–Pearson decision rule. Fable 86 describes a proposed fixed 41-episode
confirmation for one particular 0.8-versus-0.95 question; it is not a default
requirement. Never extend fixed (n) after looking at outcomes or pool unlike
families.

The script's total-wall limit includes every episode in the frozen manifest.
An execution `time_limit`, `resource_limit`, unavailable backend, invalid
receipt, incomplete sample or runner timeout remains visible in outputs; none
is silently counted as a successful communication trial. Preserve failed
inputs and original output until the current question is resolved.

## Computational-capacity boundary and artifact lifecycle

A long simulated episode can be compute-expensive, but its wall time includes
source generation, propagation rendering and receiver work. It cannot by itself
establish CPU service. A capacity claim needs identified local hardware and a
bounded same-stream backlog measurement of complete receiver work, with
renderer cost accounted separately. Current receipts separate source
preparation, channel preparation and sample rendering; receiver work can
overlap rendering and must not be inferred by subtracting these costs from
wall time. Keep `W=2*H`, full memory ownership,
explicit user limits and the project's staged validation contract. Neither
multiple independent episodes nor a GPU forecast is local CPU capacity.

All generated build files, native receipts and campaign reports are contained
under ignored `_appraisal/highFidelity-triple_Robust_Modem/.build/` and
`local-evidence/`. Keep a raw campaign only for its open sensitivity,
regression or diagnosis question. When resolved, distill the conclusion,
source identity, assumptions, failures and unresolved limits into the existing
appraisal evidence, then remove raw outputs. Retain no solver source copies,
redundant campaigns or obsolete implementations.

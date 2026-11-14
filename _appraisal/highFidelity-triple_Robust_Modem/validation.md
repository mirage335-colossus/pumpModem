# Native appraisal validation record

Updated 14 November 2026. This record retains actual checks, diagnostic outcomes,
corrective history and the evidence limits for the contained appraisal. No
scenario qualifies a real environment, universal receiver sensitivity floor,
or complete-receiver CPU/GPU capacity.

The subsequent [adverse HF expansion and comparison record](hf-validation.md)
contains the current HF models, executed campaigns, phase-history and coding
findings, and updated software checks. It also records the later
[public phase-search continuation](hf-public-search.md), the approved generic
two-file native hook and fresh canonical/appraisal checks. The source identities
and seven-group checks below belong to the preceding broad five-environment appraisal; they
remain historical evidence with their original scope.

**Legacy observation-horizon scope:** the integration smoke, exploratory
AWGN receiver trend, 12-episode weak-signal pilot, 4,096-cell full-message EME
episode, and three-pair RunPod CPU/CUDA campaign used an appraisal horizon with
three quiet opportunities. The appraisal geometry is now `q0 + 2`, exactly
two scored quiet slots, removing one unnecessary future observation. This is a
driver/horizon change, not a production `CompleteStreamReceiver` algorithm
change. Keep those older outcomes bound to their original horizon and source
identity. Separate 0 dB-Hz AWGN episodes below use the corrected five-slot
horizon. EME one-slot diagnostics do not exercise quiet completion.

## Pre-HF-expansion source and build identity

At this pre-HF-expansion checkpoint, the post-correction local Release app used
GCC 14.2.0, OpenSSL 3.5.7,
the canonical `IndependentApskV2` stack, and source commit
`2c648af11d64c55c71e1220b21298de3deaf89f5` plus the contained appraisal changes. Native source SHA-256 is
`be47453d07fb27afe4cf4c168f76390de193cf57a403009a1078dccca6f01aa2`; appraisal
source SHA-256 is
`391800b7f6e0480e01865ec80ca39fd971e25bffea94e775e58c4b2bad1ee477`; build
options SHA-256 is
`3636f58f5e5803a48c78c56be3de84fce810a285a8b7c7222781040e596e6206`; compiled
app SHA-256 is
`1b445caf59f6ed8a7c7635f399102c90101e3ae0546e97695aef54491ac075f2`. These
fingerprints bind the final checks, noise ablations and lunar discriminators,
not environmental validity. The preceding 48-episode reference campaign used
appraisal SHA `81f62c53311b9aceec68a15af09e54dfe4cd508277ed18a58a5750dd1046e41c`
and binary SHA `0ffda32b13786221bc62c831b8762069c89090bd61db1b519b0ebaa4b89861b8`.
Its raw receipts were not rewritten after the recovery-counter correction,
noise-ablation addition and retirement of obsolete lunar presets. Those changes
do not alter the modeled source or receiver for the original 48 cases.

## Pre-HF-expansion local deterministic checks

The recorded command was:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

All seven appraisal CTest groups passed in 35.25 seconds:

| Test | Scope |
| --- | --- |
| `high_fidelity_channel` | Renderer state, deterministic transforms, stochastic checks, and exact interpolation-cache/fallback comparison |
| `high_fidelity_scenarios` | Scenario parameters, units, rejection bounds, lunar-kernel moments and grid comparisons |
| `high_fidelity_hf_scenarios` | NVIS and DRM6 source tuples, normalization, bounds, moments and continuous chunking |
| `high_fidelity_impairment_observables` | AR(1) and shot-noise moments/covariance, independent resonator solution, ADC and AGC recovery |
| `high_fidelity_lunar_spectral` | Whole-horizon joint covariance, independent reference convergence, coherent projection, sparse waveform parity, workspace and continuity |
| `high_fidelity_integration` | Canonical native short-message source, receiver, quiet/EOF, noise, budget, and VLF geometry controls |
| `high_fidelity_runner` | 33 runner unit cases covering manifest/receipt identity, message and component scope, backend retention, bounds, and fixed-sample reporting |

The separate canonical Triad suite passed 32/32 checks in 64.90 seconds. Its
contained CMake test harness forced inclusion of a header missing from the
pre-existing CPU-only canonical probe; the original setup failure was retained.
This was a test-harness workaround, not a production modem source change. These
checks are software evidence, not empirical channel or capacity qualification.
The final appraisal log is the ignored `.build/comprehensive-release-checks.log`.
At that checkpoint the production native source hash was unchanged, so the earlier
canonical 32/32 result remained applicable to it; it was not relabeled as a new run.
The current continuation's source identities and fresh checks are recorded separately
in [HF validation](hf-validation.md). These historical tests verify
two scored quiet slots, keep censored episodes distinct from observed
communication failures, require actual CUDA execution when retained, and reject
generic GPU fallback.

The expanded check initially exposed a missing `<set>` include, a workspace
exception-type mismatch, and two brittle HF prose assertions. Those were
corrected without changing their physical conditions. More substantively, the
first lunar conditional quadrature failed the original joint covariance gate
at 725 Hz separation; increasing its nodes from 16 to 32 passed the unchanged
limit. Independent review additionally bound the radial-reference convergence
and clarified the real/symmetrized cross-window observable. Receipt mutations
exposed acceptance of a substituted short message and impossible successful
receive outcomes; the runner now rejects both. Original checks and repairs are
software evidence, not additional independent reception trials.

Channel cancellation is checked every 64 rendered samples, including after
source exhaustion. A targeted 0.25-second deadline diagnostic
([receipt](local-evidence/deadline-censor-check.json)) ended as `time_limit`
with zero scored slots and `completion_evaluated=false`, without a missed or
correct completion claim. It rendered 133,056 samples but hashed/accepted only
131,072; the receipt explicitly separates the cancelled partial chunk from the
hashed prefix. Older timeout receipts retain their original `missed_completion`
field but are censored by run status and must not be counted as sensitivity failures.

## Bounded integration smoke

The 10-episode `integration-smoke-2026-11` manifest completed all 10 episodes
in 31.255 seconds at
[`local-evidence/20261113T234036-1b97ee1d/integration-smoke`](local-evidence/20261113T234036-1b97ee1d/integration-smoke).
It executed zero runner failures and zero valid incomplete communications.
Campaign SHA-256 is
`8d173ef0045c86af60a61dd2a795cdeb17ac6cfcd072f60fe081945aeb2101f7`; probe
SHA-256 is
`18f887854827cdabf7579117d9ee25b402e7cd391321927f3221f15fc8be14fd`.

Five 10-second `channel` episodes rendered VLF antenna envelope, 1 MHz groundwave,
30 MHz groundwave, HF skywave, and EME. `channel` mode intentionally does not
call the receiver; it confirms signal-bearing rendering and receipt plumbing.
The smoke EME render took 1.519 seconds for the 10-second sampled input, with
no receiver work in that episode. This number includes the declared source and
channel invocation and is not a receiver throughput estimate.

The five receiver episodes were: AWGN at 32 dB-Hz (complete), AWGN at 8 dB-Hz
(complete), AWGN noise-only (zero false DATA and zero completion), AWGN input
EOF (no physical completion), and HF quiet with Helper enabled (complete).
The two AWGN level cases share a seed and both happen to complete in this one
smoke episode. They do not locate a sensitivity threshold or estimate a
probability. The campaign declares no confirmatory stratum or interval; results
are descriptive integration evidence only.

## Exploratory AWGN duration and level trend

The fixed `awgn-duration-cn0-trend-2026-11` campaign completed 24/24 episodes
in 50.258 seconds with no runner errors at
[`local-evidence/20261113T234426-419cfc25/awgn-trend`](local-evidence/20261113T234426-419cfc25/awgn-trend).
It crossed requested DATA durations of 16 and 32 seconds with C/N0 overrides
of -20, -12, -4, and +4 dB-Hz. The canonical waveform actually produces 23.2 s
and 34.8 s DATA bit periods for those requests. Each receiver cell used two
paired seeds. At -20, -12 and -4 dB-Hz, all four episodes per level were
incomplete across both durations; at +4 dB-Hz, all four completed. In the
-4 dB-Hz cells, all twelve active observations across the four runs were
classified quiet. Their internal log-evidence extrema were -3.076 to 7.733,
below the operational threshold 21.493; these were not time/resource failures.

At 0 dB-Hz, separate same-seed episodes using the corrected five-slot
appraisal horizon show the intermediate duration contrast: the 16-second
request (23.2 s actual bit) had all active
slots quiet and no completion. The 32-second request (34.8 s actual bit)
decoded two DATA slots, missed one bit as an erasure, and physically completed
after two quiet slots; the message was rejected as incomplete. This suggests
additional integration helps in this episode but does not establish an
operating sensitivity trend.

Fresh public-source oracle receipts
[`awgn-public-oracle-16.json`](local-evidence/awgn-public-oracle-16.json) and
[`awgn-public-oracle-32.json`](local-evidence/awgn-public-oracle-32.json)
reproduce the -4 dB-Hz known-waveform binary error values, 0.001515752 and
0.000105999. The oracle knows the two candidate waveforms and channel; it does
not model signal-presence testing, blind acquisition, false-alarm constraints,
coding or complete-message success. Thus this small equal-prior Pe is not the
receiver's fixed-false-alarm detectability limit, and the observed gap does not
indicate an input calibration defect.

A separate known-phase Neyman–Pearson signal-versus-null calculation at fixed
false-alarm probability `alpha = 4.63e-10` gives ideal maximum detection
probabilities of 0.0305318 (16 s requested, 23.2 s actual) and 0.20052026
(32 s requested, 34.8 s actual) at -4 dB-Hz; 0.7297
and 0.9878 at 0 dB-Hz; and approximately 1 at +4 dB-Hz. These are ideal
known-hypothesis calculations, not receiver outcomes or complete-message
bounds. Only two fixed seeds were used per receiver cell, with no confirmatory
stratum or confidence bound; the coarse sweep cannot resolve a smooth trend
from a decoding cliff. Streaming evidence across ordinals with explicit
false-alarm control is a candidate for study, since repeated history rescans
are unnecessary work. No such receiver change is implemented or qualified.

## Reference weak-signal pilot

The fixed `weak-signal-pilot-2026-11` campaign executed all 12 declared
episodes in 590.300 seconds, but ended `incomplete_checks` with two native
`time_limit` outcomes, at
[`local-evidence/20261113T234551-c2d5b716/weak-signal-pilot`](local-evidence/20261113T234551-c2d5b716/weak-signal-pilot).
Its campaign SHA-256 is
`63a3f84d02910f7f3b6b218ec0e5479992caf1d3468f2439eca7a6ad34de5c0c`; it used
probe SHA-256
`18f887854827cdabf7579117d9ee25b402e7cd391321927f3221f15fc8be14fd`, with appraisal
source SHA-256 `f94ac8b34f4a80a6088bf827d00e8ef74be2ee69d4a005316cbc6b776bf62de1`.
Each raw receipt retains its original source/build identity. The 12 episodes included wanted
receptions in the five families, interior-fade controls, two AWGN duration
cases and a declared capture-gap control.

The 1 MHz groundwave, 30 MHz groundwave, disturbed-HF skywave, short and long
AWGN wanted cases completed correctly. The EME wanted case processed its
declared episode but did not complete correctly; this is the one valid
incomplete communication outcome. The full VLF wanted case and EME outage
control hit their 180-second trial limits, so the runner counted two execution
failures and reports no fixed-sample statistics. The VLF case had a 77,766,069
sample horizon and 24 CPU workers on the 12-thread host. The VLF outage-control
case used short smoke geometry and is not high-fidelity VLF outage evidence.
The MF, HF and short-VLF outage controls, and the capture-gap control, did not
physically complete their intended message. These are observations from the
declared episodes, not natural-condition probabilities, sensitivity limits or
hardware-capacity measurements.

The MF, HF and short-VLF fades each produced one premature physical completion,
then five resumed DATA slots and a second physical completion; both fragments
were rejected by the codec. The declared capture gap instead produced no
physical completion, two ambiguous slots and five resumed DATA slots. This
distinguishes known invalid capture from unmarked physical absence. A fade
that makes two scored slots indistinguishable from final silence can trigger
the physical endpoint rule; receiver-side computation alone cannot recover an
unobservable distinction. Keep this failure separate from wrong-bit sensitivity
and from the deliberately abbreviated VLF outage fixture.

Preserve these outputs as provenance for their old source behavior; they do
not qualify current receiver completion behavior as described above.

## Doppler-aligned EME single-slot diagnostics

The historical, now retired `eme-disk-slot` renderer used a Doppler-aligned
65,536-cell quadrature. The old operational IDs are not aliases for the new
full-calendar spectral process.
Its sampled projected-variance/covariance checks for the 88 Hz disk/patch
profiles at 0 and ±5/±15 Hz are detailed in [models](models.md). The old
164 Hz severe-width 4,096-cell grid remains a finite numerical control with
large long-lag error; it is not a qualified severe-width renderer.

The bright one-slot receipt
[`eme-qualified-slot.json`](local-evidence/eme-qualified-slot.json) used seed
8101, C/N0=30 dB-Hz, and 56,575 rendered samples representing 29.466 seconds
physical time, including the complete 26.4-second first DATA observation. It
classified the first DATA bit correctly as 0. Rendering took 301.462 seconds;
receiver control/wait took 2.196 seconds; total wall was 303.713 seconds. This
is one observed DATA slot only; it does not establish a full-message result,
quiet completion, sensitivity floor or receiver throughput. The receipt's
appraisal-source hash was
`7c3c2e1e02601c1726a33cf2db3005959d65c1eab4fe398b2d250ba23429aa69`.

The follow-up weak slot
[`eme-qualified-weak-slot.json`](local-evidence/eme-qualified-weak-slot.json)
matched the seed, 65,536-cell path, input geometry, and public receiver settings
at C/N0=10 dB-Hz; the noise variance is 100 times the bright case. The physical
signal energy is unchanged at 45,410.2896, with `E/N0=45,410.2896/192=236.512`
in the declared normalized units. Despite this high ideal-knowledge contrast,
the blind receiver returned an ambiguous active-slot decision: log evidence
−6.18264 versus its 21.4934 threshold. The quiet energy was 6,438.968 versus
6,285.057 threshold. It evaluated eight varying candidates over 810 blocks,
4,979,328 basis rows, and 128 drift slopes / 6,009 drift bins; the varying
search reported `short_block_partial_bandwidth`, and coherent-gain search
reported that its center grid exceeded the work limit. Render plus receiver
took 268.946 seconds (263.860 rendering, 5.039 receiver control/wait).
The weak receipt's appraisal-source SHA-256 is
`d235bdcb00581259c14e05a86635fe8ce3c2e607064ce11cd5025c94ae0ff7a7`;
both trials use the same canonical native source hash recorded above.
No wrong bit was produced, and the slot-mode trial
deliberately did not test physical message completion.

This is an **ideal-knowledge contrast**: a cheap known-waveform, known-channel
signal-presence calculation using the same signal energy and false-alarm level
predicts detection probability near 1. That is not a binary error bound for
the blind receiver, not a matched-knowledge comparison and not a physical
channel floor. It identifies a practical receiver/search question despite
ample integrated signal energy; the short-block/partial-bandwidth evidence and
work-limited coherent grid deserve investigation before assuming more physical
energy is the remedy. A matched single-slot contrast is only a renderer and
first-observation check, not a sensitivity curve.

Separately, the legacy full-message 4,096-cell `eme-disk` run at C/N0=30
physically completed but decoded `a` rather than transmitted `e`, with bit 1
wrong. Its selected known-channel/known-waveform slot oracle had squared
distance 19,595.898, hypothesis energies about 24.6 seconds and noise variance
1.92, giving an equal-prior binary error below the floating-point
representation's range. That oracle
presumes the known waveform and channel; it excludes blind acquisition and the
receiver's false-alarm constraint. The wrong full-message decode and ideal
oracle therefore expose a knowledge/receiver mismatch, not a demonstrated
physical EME outage. The 65,536-cell one-slot result uses different numerical
geometry and is not a matched full-message comparison.

The bright finite-grid failure took the `coherent_evidence_sufficient` branch,
skipping the time-varying alternative even though its accepted bit was wrong.
A bounded channel-model compatibility check before accepting strong affine
evidence, and efficient short-coherence searches retaining the full DATA band,
are concrete receiver candidates. They need matched positive/negative trials
and complete-service costs; lowering a threshold to fit this one failure would
not establish a general remedy. No production receiver algorithm was changed.

## Matched CPU/CUDA RunPod episodes

The `cuda-paired-native-2026-11` campaign completed 6/6 paired episodes in
38.405 seconds with actual CUDA execution and no runner errors at
[`local-evidence/20261113T235040-b789ac3f/cuda-paired`](local-evidence/20261113T235040-b789ac3f/cuda-paired).
Campaign SHA-256 is
`ee35ce6cb28f6b1ff702d13948532ba51c33e72b3f68230cac3eb6bd93830a2a`; probe
SHA-256 is
`ed1bbdd0d0e200553b5db7a0c1cd5651b99066d51816f1ea9677060b65642b1f`. The
appraisal source SHA-256 is
`7fd86383cce38e308ac282667196f9449340739f89d2ce9661cce33b7366412a`, native
source SHA-256 is
`be47453d07fb27afe4cf4c168f76390de193cf57a403009a1078dccca6f01aa2`, and
build-options SHA-256 is
`a401d50b109ff75de14ce073c2cc73ea271a7cbd1bebedbc60a47474df3f7cc2`. The
cloud receipt did not provide a source commit at configure time. Each CPU/CUDA
pair has exactly matching rendered-input hashes and public geometry;
backend-specific workspace admission and available-memory reporting differ.
All six outcomes match: AWGN and disturbed-HF wanted cases complete, while
noise-only Helper acquisition remains incomplete.

Per-episode total wall times were 0.957 s CPU versus 2.472 s CUDA for AWGN,
5.209 versus 5.966 s for disturbed HF, and 8.513 versus 11.820 s for
noise-only Helper acquisition. The disturbed-HF path executed an FP16 FFT;
INT8 and FP16 matrix contraction calls were zero. Thus CUDA was slower in all three
matched episodes. These short complete-process timings do not measure
sustained throughput. A partial utilization monitor had only about seven
nonzero samples, at most 15% over 180 seconds, and mostly idle readings; it is
not a representative utilization or capacity result.
The paired behavior also predates the exact two-quiet-slot correction noted
above. It verifies actual CUDA execution and parity for these frozen old-source
episodes, not current-source receiver qualification.

A separate post-horizon-correction disturbed-HF CUDA receipt
([five-slot result](local-evidence/hf-two-quiet-cuda.json)) completed correctly
with one physical completion, zero wrong bits and five scored slots in 6.416 s.
Its appraisal-source hash is
`deb3e8775416660ad250cb33e23b888ce6a8eb03ce1388e02dc5452a608339c8`.
This checks the corrected horizon on CUDA; it is not a throughput comparison.

The verified RunPod device was a full NVIDIA RTX PRO 6000 Blackwell server GPU
(97,887 MiB, 600 W; MIG disabled), with CUDA 12.8 / nvcc 12.8.93, driver
595.91.07, Torch 2.9.1 (`CXX11_ABI=true`), GCC 13.3.0 and CMake 3.28.3. The
host was an EPYC 9555 with 54 workers over an effective 27 hardware threads;
the OS affinity/quota floor was 27.2 and the cgroup RAM limit was
187,999,997,952 bytes. This confirms the requested accelerator actually ran
these paired native episodes, but provides no speedup, sustained utilization,
or complete-receiver capacity qualification. No account or billing identifiers
are retained here.

## Corrective findings retained

Several early failures revealed integration requirements; the fixes retain the
original boundary rather than weakening the check:

- A direct deferred `add_subdirectory` configuration failed under the root
  project's source-relative dependency contract. The appraisal now configures
  the canonical root and uses its `CMAKE_PROJECT_DataPump_INCLUDE` hook to
  defer an `include()` of the appraisal CMake file until canonical targets are
  defined. The contained configure/build and linked native target now pass.
- The first reception crop omitted source pre-roll, so early public observations
  could lose contributing input and produce an invalid negative result. The
  driver now inserts the declared 0.25-second capture lead, computes the
  observation horizon from the full native geometry, and keeps filter and
  propagation support in the quiet-completion boundary. No message length or
  EOF is used to declare success.
- The initial wrong-key path risked letting transmitter-specific emitted
  power alter the receiver's calibration. Receiver basis gain is now calculated
  from the receiver's own configured waveform/key. Transmitter truth stays on
  the evaluator side, so a wrong-key trial cannot get a favorable receiver
  normalization from its actual signal.
- Repeated lunar cells with the same moving-delay group had redundant source
  dot products. The renderer now caches the exact two neighboring fractional
  interpolation endpoint products for each base/sample and linearly blends
  them, retaining all cells and each cell's phase and delay trajectory. The
  deterministic comparison against direct per-cell interpolation differs by
  less than `3e-14`; workspace-limited cache disable falls back to the direct
  expression. A bounded 4-second, 1,920-sample/s, 4,096-cell component run
  measured 1.722 seconds direct and 0.681 seconds cached, with workspace
  increasing from 2.61 to 4.81 MiB. That component timing excludes canonical
  source preparation and receiver work. It applies to repeated exact initial
  delay groups in this kernel and is not a complete-message speedup claim.

## Full-duration VLF status

The historical six-slot VLF reference plan reports a 5,760.4-second bit,
77,766,069 samples, 40,503.16094
seconds of represented calendar time, and six scored public slots for the
three-bit short-message fixture plus physical quiet completion. The physical
clock remains uncompressed. The current corrected five-slot plan needs
64,806,069 samples (33,753.16094 seconds); both fit the executed pilot's
80-million-sample cap. The limit observed here is wall time, not sample admission.

A direct CPU reference receive with an 80,000,000-sample cap processed all
77,766,069 samples but reached its declared 120-second wall limit before
physical completion. The receipt
[`vlf-reference-cpu.json`](local-evidence/vlf-reference-cpu.json) reports
`time_limit` / `receive_cancelled_before_physical_completion`: no scored slots,
no physical completion, no decoded message, `capacity_qualified=false`, and
`isolated_receiver_service_measured=false`. Source/channel preparation and
rendering took 45.218 seconds; receiver control and wait took 72.699 seconds;
total wall time was 120.029 seconds. This is a censored, incomplete run, not a
communication sensitivity failure or physical outage. It used the CPU with
24 workers and six peak owners on a 12-hardware-thread host. A component VLF
channel preflight also exists, but does not include full-message receiver work.

The subsequent first-slot diagnostics at C/N0=20 dB-Hz preserve the full
5,760.4-second native bit and accepted all 12,966,069 samples. The
[CPU result](local-evidence/vlf-slot-cpu.json) reached 120 s with zero scored
slots (8.097 s rendering, 111.653 s control/wait); the
[CUDA result](local-evidence/vlf-slot-cuda.json) reached 180 s with zero scored
slots (8.879 s rendering, 170.458 s control/wait). CUDA actually executed 2,533
FFTs and 331,415,552 FFT rows. Different hosts and deadlines prevent a speed
comparison. These diagnostics locate a substantial native scoring cost without
establishing a sensitivity failure or hardware capacity ceiling. A useful next
receiver investigation is bounded stage attribution and reuse of sufficient
statistics within the same stream, preserving every public hypothesis and
the full worker budget rather than rescanning long histories.

### Full-geometry VLF cost attribution

The [RRC diagnostic](local-evidence/comprehensive-vlf-diagnostic.json) and
[tapered diagnostic](local-evidence/comprehensive-vlf-tapered-diagnostic.json)
use the same native receiver source and 12-logical-CPU Ryzen 5 PRO 5650U with
the default 24-worker budget. Both render all **12,966,069** samples of the
first public observation (6,753.16094 physical seconds, including the
5,760.4-second DATA bit) at seed 5489 and C/N0=+20 dB-Hz. The RRC capture's
input SHA-256, `41526ec1d88be100bd66989098d728c41173f32c9a30985b40bdc44be27a23b8`,
matches both earlier time-limited CPU/CUDA first-slot captures. The tapered
waveform intentionally has different samples, and its source level uses the
same deterministic normalization rule. Each receipt retains its own appraisal
source hash and the common canonical native source hash.

`--mode diagnose` uses the canonical coarse search over the complete public
observation, then holds one canonical fine session across 2/3/4/6-second
checkpoints aligned with the public segment calendar inside the DATA aperture.
Fine candidates come only from complete native coarse selection. Both pulses
score all 55 frequency points, three coarse arrival points and 26 keyed
epoch/bit workspaces, with two declared receiver taps; both retain four fine
coordinates. The 0.2 s tap separation is a public receiver allowance, not a
second measured VLF propagation path. The fine crop retains 12 affine and 12
adaptive states and one DC projection per 480-row block throughout. It neither
admits a bit or null decision nor replaces a full public-slot evaluation.

| Pulse | Source/channel render | Complete coarse acquisition | Six-second fine sample | Fine process CPU | Cache witness |
| --- | ---: | ---: | ---: | ---: | --- |
| RRC | 10.536 s | 1.723 s | 0.7740 s | 0.7832 s | No native cache fills; source inspection identifies direct basis fallback |
| Tapered RRC | 9.313 s | 1.824 s | 0.01344 s | 0.04199 s | Two prefix fills, three final fills (49,152 rows), 2,304 interpolated and zero direct basis columns |

Both samples do 1,105,920 logical basis rows and finish all four checkpoints
with positive basis work. The roughly 58-fold wall contrast is scoped to this
fine crop with the observed cache-fill history and stable state shape. The
short continuation timings and changing cache work do not establish a whole
slot service forecast, speedup, or sustainable backlog margin. The unchanged
RRC branch falls back to direct mapped-source renders when the pulse is not
tapered; the per-epoch source mutex then serializes candidates sharing an epoch.
The historical CUDA `statistics_blocks=193216` proves that run also entered
fine scoring. CUDA's small-projection native fallback only admits one tap in
this branch, whereas this public VLF model retains two, so the old CUDA run
incurred device statistics and per-block synchronous readback. No new CPU/GPU
speed comparison follows from these receipts.

The raw `Format` default is RRC. The current native GUI/live profile instead
passes its format through `plan_shaping` and `plan_duration` before preparing
the public waveform. A linked `plan_shaping` witness for this exact
1,920 sample/s, 0.1 Hz, one-added-symbol, non-FHSS shape selects tapered RRC
without fallback for both the minimum-duration request and the 16 s request.
Both pulses retain 5,760.4 s DATA duration, 0.02 s ramp and 0.0625 Hz placement;
the planner selects the taper on lower modeled far-outside energy. The appraiser
therefore exposes an explicit `--pulse tapered-rrc` control and preserves the
original RRC pulse for historical and unchanged-reference comparisons.

The antenna circuit acts on DATA-centered complex samples: native `Emission`
removes its digital IF after constructing the composite signal. In the matched
RRC capture the normalized transmitted and received signal energies are
5,757.389055 and 5,757.386916 seconds, inconsistent with mistakenly detuning the
resonator by the metadata's 240 Hz IF. This is a reference-plane check within
the calculated resonant circuit, not an absolute field or antenna measurement.

The fixed [VLF campaign](campaigns/vlf-reference.json) preserves both cost
diagnostics, a bright tapered five-slot message, matched weak static and
two-hour phase-wander tapered observations at the declared magnetic C/N0,
a full first-aperture detuned circuit render, and one unchanged RRC first
observation with a 1,200-second budget informed by the measured direct-render
cost. The phase-wander case adds a documented bounded atmospheric prior; its
supplemental stages need their own actual result. The seven episodes have a
1,900-second overall cap, execute once, and do not require a successful decode
in an adverse case or pool unlike conditions into a probability.

The phase-wander law is the [Fable A1 engineering control](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/81-scenarios-sub9.md#2-scenario-a--stationary-weak-signal-magnetic-controls).
[Thomson et al. (2025)](https://doi.org/10.1029/2024JA033610) measured amplitude
and phase changes around dawn and dusk on other paths, including the 20.27 kHz
ICV–Cambridge path; one transmitter's unstable phase precluded a comparable
phase record. [Ajakaiye et al. (2025)](https://doi.org/10.1029/2025JA033796)
found path-dependent amplitude responses at 20.27–26.70 kHz during one storm.
Ma et al. (2026) reduced the dominant diurnal propagation-delay error on six
historical Alpha timing paths by predicting phase variation at the receiver.
Their validation uses stable timing transmissions and a path-specific diurnal
model. The 8.27 kHz, two-hour synthetic phase law remains an independent
engineering control with uncalibrated magnitude, period and occurrence rate.
[Ma et al.](https://doi.org/10.3390/s26113295)

### Frozen full-duration VLF campaign

The [frozen input](local-evidence/comprehensive-vlf-reference/frozen-input.json)
identifies the campaign by SHA-256
`f0266b291266517d1b12366888e635fe6a49acdd08435dc8773af8c5a9a06f91`
and the executed CPU probe by
`0ffda32b13786221bc62c831b8762069c89090bd61db1b519b0ebaa4b89861b8`.
The repeat cost controls reproduce the respective capture hashes above.
RRC records 2,304 direct basis columns and `unsupported-or-abrupt-pulse`.
Tapered RRC records 2,304 interpolated columns, zero direct columns and three
native fills. Fine checkpoint wall sums are 0.556 s and 0.01599 s, respectively.
The shorter RRC timing than in the preflight shows why the crop contrast
cannot serve as a portable receiver speed ratio.
[RRC](local-evidence/comprehensive-vlf-reference/000-vlf-rrc-phase-cost.stdout.json)
[Tapered RRC](local-evidence/comprehensive-vlf-reference/001-vlf-tapered-phase-cost.stdout.json)

The bright tapered RRC episode at +20 dB-Hz accepts all **64,806,069** samples
over **33,753.16094** physical seconds. Ordered decisions are `0, 0, 1, quiet,
quiet`, with complete valid observations and exactly one correct physical
completion after the second quiet slot. The decoded message is `e`. Elapsed
wall time is 64.690 s, with 45.677 s of source/channel render and 17.198 s of
receiver control and wait. The run uses 24 workers, three peak running owners
and 2,306,061,401 peak reserved receiver bytes, with zero backpressure.
Coarse work takes 1.72–2.17 s per slot and fine work 8.89–16.25 s per slot,
including approximately 1.245 billion basis rows per slot. DSP can overlap
rendering and other owners. These are complete functional episode timings.
[Bright tapered receive](local-evidence/comprehensive-vlf-reference/002-vlf-tapered-bright-message.stdout.json)

At −18.42995 dB-Hz the tapered static and phase-wander episodes share the
source, white-noise seed, noise variance and 12,966,069-sample valid aperture.
The wander changes the wanted phase and declares an additional public
atmospheric frequency allowance of 0.436332 mHz. Both execute the complete
first observation at `W=24`, with 55 coarse frequency points, three arrival
points, 26 keyed workspaces and four retained fine coordinates.

| Weak first observation | Native decision | Operational log-e, threshold 21.49338 | Bit log separation | Quiet energy, threshold 27,392.53 | Wall | Peak reserved receiver bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Static resonant | DATA `0`, correct | 81.51996 | 88.69608 | 26,960.21 | 20.268 s | 990,577,109 |
| Two-hour phase wander | Quiet while wanted | 16.85885 | 24.57779 | 26,924.67 | 64.577 s | 1,394,822,197 |

The quiet-while-wanted result is a complete slot miss. The varying and
coherent-gain stages both report `fine_cells_too_slow_for_short_block`.
At one fine cell per 60 s, their two-cell minimum exceeds both fixed short
block limits. Linear drift still runs across 104 slopes and 79 frequency
bins, taking 43.381 s of the 51.809 s fine phase. Phase wander therefore
lowers detection evidence and increases work on this matched input despite
preserving wanted energy. A single linear drift of frequency describes
quadratic phase. The imposed phase has curvature that changes sign within
the observation. The result identifies a failure of the current admitted
models on this episode, with no message-completion judgment from either slot.
[Static receipt](local-evidence/comprehensive-vlf-reference/003-vlf-tapered-weak-static-slot.stdout.json)
[Phase-wander receipt](local-evidence/comprehensive-vlf-reference/004-vlf-tapered-weak-wander-slot.stdout.json)

The raw public-aperture known-signal presence comparison uses the rendered
wanted-vector energy `E` before independent proper complex Gaussian noise.
The complete rendered interval equals the public aperture in both receipts.
The configured sequence, including any leading waveform from the following
slot inside the aperture, is part of that wanted vector. Its normalized
energy is 5,757.61996 s in each episode, so `rho=E/noise_variance=82.65097`.
At the design false-alarm target `alpha=exp(−21.493378688)=4.62961e−10`, the
known-vector Neyman–Pearson test has miss probability
`Q(sqrt(2*rho) − Q^{-1}(alpha))=8.18e−12` in each case. This is an ideal
presence probability with exact waveform, timing, phase and amplitude known.
The native rule additionally faces search, prior penalties, projection and
bit discrimination. Its false-alarm rate remains unmeasured by this pair.
The result shows a substantial opportunity for receiver-side tracking under
this channel prior, with the useful fraction of the gap still to be measured.

A follow-up receiver candidate would share block statistics already produced
for admitted public hypotheses and carry a bounded smooth phase/frequency
state across long blocks. A 600 s centered local tangent to this imposed
phase has at most `pi*(2*pi/7200)^2*600^2/8=0.10766` radian residual and spans
ten fine cells. With exact local tangent parameters that gives a coherent
power ratio of at least `cos(0.10766)^2` (0.0504 dB maximum loss within each
block). The calculation demonstrates room for a slower block cadence. A
practical receiver must infer the state at weak signal levels, retain bit
discrimination and account for the added model mass in its false-alarm
bound. Retaining static and faster models and using the same forward block
statistics would keep this candidate applicable across different channel
conditions without a new transmit exchange or an unbounded history rescan.

The detuned circuit render completes the same first-aperture duration at
seed 5489 and +20 dB-Hz in 9.717 s. Its normalized wanted energy drops from
5,757.62209 s at TX input to 5,637.53386 s after the circuit and path, a
0.09154 dB loss. Detuning increases from 4.135 to 4.20253 Hz. The frozen
circuit's adiabatic one-pole response at those endpoints predicts losses
of 0.09011 and 0.09305 dB, which bracket the observed in-aperture result.
This row is a channel-only transfer measurement under the imposed circuit
law. [Detuned render](local-evidence/comprehensive-vlf-reference/005-vlf-tapered-detuned-render.stdout.json)

The unchanged RRC first-slot episode accepts all 12,966,069 samples with the
same input hash as both earlier censored attempts. It scores a complete valid
DATA `0` with operational log-e 573,805.13 in **567.458 s** against the
predeclared 1,200 s wall budget. Source/channel rendering takes 9.014 s,
receiver control and wait take 558.212 s, and native coarse and fine phases
take 1.733 and 556.170 s, respectively. Peak receiver reservation is
990,515,669 bytes with 24 workers, one running owner and zero backpressure.
The bright tapered first slot has the same count of 1,244,742,624 basis
rows and takes 8.893 s of fine work during its complete receive. Together with
the matched crop and cache receipts, these timings identify the exact direct
basis route as the practical cost problem for this RRC geometry. An RRC
optimization could batch exact mapped source synthesis across both keyed
patterns, sharing pulse weights and batching keyed chips with the native
source bank. Qualification needs exact coordinate and edge handling, with
the current direct route available for geometries outside the supported
batch path. No complete RRC message was evaluated by this first-slot run.
[Bright RRC slot](local-evidence/comprehensive-vlf-reference/006-vlf-rrc-bright-slot.stdout.json)

All seven episodes execute once in 750.383 s with zero execution failures.
The runner's zero `valid_communication_failures` counts complete-message
receive episodes. The first-observation phase-wander miss remains an
explicit adverse outcome. [Campaign summary](local-evidence/comprehensive-vlf-reference/run.json)

## Remaining qualification boundaries

- There is no held-out field transfer/noise recording or calibrated antenna,
  ground/terrain, HF episode, or target-band whole-disk 5.8 GHz channel. The
  implemented models remain the calculated/reference/synthetic hypotheses in
  [models.md](models.md).
- The smoke campaign used short `smoke` geometry and the AWGN trend used only
  two fixed seeds per receive cell; neither provides a complete high-fidelity
  sensitivity curve. All 12 `weak-signal-pilot` episodes executed, with two
  censored time limits, so the campaign ended `incomplete_checks`. The current
  tapered VLF reference completes its bright five-slot message. The historical
  full RRC receive remains censored, and the uncensored RRC follow-up covers
  its first slot only.
- No conditional success probability, rare-failure bound, independent channel
  episode population, or environmental availability is qualified.
- The current tests assume configured public calendar and keyed ordinal;
  unknown epoch, ordinal, initial timing and address acquisition remain outside
  scope.
- No local same-stream backlog/capacity measurement was performed. The local
  build disables tensor and Vulkan backends; the separate RunPod pair did
  execute CUDA, but its short process timings and sparse utilization sampling
  do not qualify GPU speedup, sustained throughput or complete-receiver
  capacity. Smoke renderer wall time and EME component timing likewise do not
  qualify CPU/GPU capacity.
- A RunPod execution receipt is retained above with device, source, arithmetic,
  wall-time and host-resource provenance. This validation summary contains no
  account or billing identifiers and makes no claim about provider charges.

Keep the smoke and VLF raw receipts while the implementation-validation and
diagnosis purposes above remain active. When those questions are resolved,
preserve the concise outcomes and fingerprints above, then remove redundant
ignored outputs. Any new VLF measurement uses a separately bounded,
predeclared follow-up.

## Expanded propagation and receiver controls

The frozen `propagation-breadth`, `receiver-controls` and `weak-holdouts`
campaigns executed **16/16, 12/12 and 6/6** episodes in 226.042, 145.310
and 34.551 seconds respectively, with zero execution failures or censoring.
Their source/build identity is the `81f62c...` appraisal / `0ffda32...` binary
identified above as the preceding campaign. Receipts are retained at
[`comprehensive-propagation-breadth`](local-evidence/comprehensive-propagation-breadth/report.md),
[`comprehensive-receiver-controls`](local-evidence/comprehensive-receiver-controls/report.md),
and [`comprehensive-weak-holdouts`](local-evidence/comprehensive-weak-holdouts/report.md).
These are fixed descriptive mechanism tests, not pooled success-probability
samples. They ran serially on the Ryzen 5 PRO 5650U, 6 cores/12 logical CPUs,
16,053,669,888 bytes system RAM, GNU 14.2 Release native CPU build, with the
canonical automatic **24-worker** budget. The complete receiver admitted up
to five concurrent ready owners in the five-slot MF case; its recorded peak
reserved memory was 1,211,084,036 bytes. Wall times include source/channel and
receiver work and are not a calibrated CPU-capacity result.

Both default-level 1 MHz land/sea episodes and the separate competing night
ray completed correctly. So did the 30 MHz sea/land cases and the C/N0=20
quiet, disturbed, 7ms/1Hz NVIS, and DRM6 skywave cases. The six explicit weaker
cases all completed their observation horizon without a correct message:
MF land at −16 dB-Hz and 30MHz land plus the four skywave models at 0 dB-Hz.
Keeping default groundwave levels matters: overriding both land and sea to
one C/N0 would remove the magnitude difference this comparison tests.

The 863.767-second HF transition message spans the entire 120–240 second
fade and recovery. Slot 3 was quiet; slot 4 carried DATA as the fade ended,
and slots 5–12 carried DATA with their whole observations after the fade.
The receiver recorded one reacquisition and no premature completion. It
physically completed at two final quiet slots but rejected the message for
one missing slot; FEC is off in this fixture. The original generic
`resumed_data_slots=0` counted only explicitly configured gap controls, not
scenario-owned fades. The raw slot and message receipts establish recovery;
a reporting correction and regression test are recorded separately below.

The negative controls exposed material weaknesses, rather than merely lower
wanted-message success:

- At C/N0=32 dB-Hz, the compound colored/impulsive **noise-only** episode
  classified all five slots as DATA (operational log evidence about 715–1263
  against a 21.493 threshold). It did not physically complete. The wanted
  companion produced two false DATA decisions in its quiet tail, preventing
  completion. Native `noise_statistics_valid=true` is a receiver computation
  flag, not proof that this non-white/non-Gaussian noise obeys its assumed law.
- The paired C/N0=8 episode completed, but only one of three fresh weak-noise
  holdouts completed. This is not evidence that lowering SNR helps reception:
  the override changes only white noise, leaving the colored and shot terms
  fixed, and it also changes the receiver's white-noise reference. It does
  not uniformly scale the compound noise law or trace one AWGN ROC.
- The Helper-on wrong-key case made two false DATA decisions and a physical
  completion, but the codec rejected the resulting message for an erasure;
  no message was accepted. Wrong ordinal and Helper noise-only produced no
  false DATA or physical completion. A correct-key Helper reference completed.
  These cases keep keyed waveform rejection, physical absence and message
  integrity distinct; they do not establish semantic transmitter identity.
- The continuous frontend blocker prevented wanted completion without false
  DATA in its noise companion. Its catalog fixture does not remove the blocker;
  recovery after removal is instead covered by the independent native AGC test.
- Two interior physically quiet slots caused a premature completion, followed
  by resumed DATA and a later completion. A marked capture gap at the same
  interval resumed five DATA slots without physical completion, and EOF caused
  none. Genuine silence can be indistinguishable from message end under the
  two-quiet-slot contract; invalid capture samples are not physical silence.
- All three independent disturbed-HF weak holdouts failed to complete. These
  three seeds are failure-finding evidence, not a reliability confidence bound.

The non-white false DATA and wrong-address cases are retained as reproducible
improvement targets. Receiver-local noise-spectrum/covariance estimation,
robust likelihoods or bounded impulse rejection merit evaluation against these
same held-out controls, including quiet slots, without transmitter feedback.
Such changes must preserve weak-signal energy, every public hypothesis and
complete-service workspace/throughput; blindly increasing a universal threshold
or clipping all large samples could hide the failure by sacrificing sensitivity.
No production receiver change or unmeasured improvement is claimed here.

## Full-calendar lunar outcomes and final discriminators

The [EME campaign](local-evidence/comprehensive-eme-episodes/report.md)
executed all seven full-calendar episodes in 123.972 seconds, with zero
execution failures or censoring. Each received 517,375 samples over the
269.466-second, five-slot calendar. The supported spectral model's whole-horizon
numerical checks precede these receiver observations; no old finite-grid preset
is used for them.

| Model and conditional C/N0 | Actual receiver outcome |
| --- | --- |
| Disk, 30 dB-Hz, seed9501 | DATA `0,0,0`, then two quiet slots; third bit wrong; accepted a space instead of `e` |
| Disk, 10 dB-Hz, same seed | Three ambiguous active slots; no physical completion |
| Narrow patch, 30 dB-Hz, seed9502 | DATA `0,0,1`, two quiet slots; correct complete `e` |
| Narrow patch, 10 dB-Hz, same seed | Three wanted slots called quiet; no physical completion |
| Severe disk, 30 dB-Hz, seed9503 | Same wrong third bit and accepted space; physical completion without integrity rejection |
| Severe disk, 10 dB-Hz, same seed | Two ambiguous and one quiet active slot; no physical completion |
| Disk noise-only, 30 dB-Hz, seed9504 | Five quiet slots; no false DATA or physical completion |

The beam cases use separate declared seeds; this table is not a matched estimate
of beam improvement, a population rate or a universal severity ordering.

Independent review requested two discriminators, frozen before execution in
`eme-discriminator.json`. Both completed in 28.470 seconds total on the final
binary. The seed9501 disk episode at **60 dB-Hz still accepted the wrong space**.
Its public slot geometry, source gain, source/output sample counts and exact
wanted-energy sum (141,399.05175878672 sample units) match the original 30 dB-Hz
case; only white variance falls from 1.92 to 0.00192. This is a finite,
conditional high-SNR counterexample, not proof of an asymptotic error floor.
The known-channel selected-bit-2 calculation at the original 30 dB-Hz level
uses public samples 235,107–286,975, squared waveform distance 17,867.85806827436,
and hypothesis energies 24.70611045196691 / 24.6931240366793 seconds. Its ideal
binary error rounds to zero below floating-point range. This does not mean zero
physical error probability, nor does it pay unknown-channel estimation, timing,
address search or message completion. It establishes that the simulated
observation retains bit information when the channel is known; it does not by
itself prove that the current error is avoidable at the same unknown-channel
information and computational budget.
[Discriminator receipts](local-evidence/comprehensive-eme-discriminator/report.md)

A large affine-model evidence value can therefore coexist with a wrong accepted
message in this modeled channel. A useful next receiver experiment would test
model adequacy before letting large early evidence suppress broader
changing-channel hypotheses, and compare a bounded likelihood retaining the
joint delay–Doppler structure. It must include the wrong-bit, no-signal and
quiet-completion cases and measure full receiver cost; simply spending more on
the same mismatched score need not repair it. These are identified improvement
targets, not implemented or performance-qualified receiver changes.

The separate [noise ablation](local-evidence/comprehensive-noise-ablation/report.md)
completed all three episodes in 6.290 seconds. The seed9301 public geometry,
white variance, message/calendar, pulse and sample horizon match the original
compound noise-only case. White-only produced five quiet slots. White plus
AR(1) produced five ambiguous slots but no false DATA. White plus exponential
shot noise produced five false DATA decisions, with log evidence about 533–989;
none of these three completed a physical message. Thus the shot component alone
reproduces the selected false-DATA failure, while the white baseline does not.
This is a matched mechanism discriminator, not a calibrated atmospheric noise
law or false-alarm-rate estimate. No receiver threshold was retuned.

## Final verification and lifecycle

The final seven-group test run passed in 35.25 seconds, including 33 runner
cases and an actual reference HF-transition regression. That regression now
counts eight complete, valid post-fade DATA observations using the explicit
240-second recovery boundary and checks native reacquisition. The original
campaign's incorrect zero recovery count remains visible in its immutable raw
receipt and is corrected in the analysis above. The change affects reporting,
not the production receiver or waveform.

All **53 newly declared campaign episodes** executed without time or resource
censoring: 48 reference/component episodes plus five review-requested mechanism
diagnostics. Their actual communication failures remain failures. The updated
smoke manifest's changed EME row was additionally executed alone: a native
10-second reference channel capture with 19,200 samples, positive signal energy,
and 5.993 seconds runtime. The other unchanged historical smoke rows were not
relabeled as new runs.

The five superseded operational lunar presets and the obsolete runnable
weak-signal pilot manifest were removed. The pilot's frozen historical inputs
remain with the explicit old-source failure record. The independent
`lunar_paths` numerical reference and concise coarse-grid failure regression
remain because they test the current spectral construction against a different
quadrature. The temporary shaping witness executable was removed after its
result was distilled. Current native build/cache and compact receipts serve
reproducibility and the unresolved shot-noise, wrong-message and phase-wander
failures; remove superseded receipts after replacement experiments resolve
those same questions, preserving the findings and exact provenance.

The follow-up RunPod allocation failed at provider container creation before
source upload or execution. It was stopped and removed after user confirmation;
no persistent volume was created or retained. It contributes no new GPU evidence.
The previously observed native CUDA results remain bound to their earlier
source/horizon and hardware. The current full-calendar results are CPU results.

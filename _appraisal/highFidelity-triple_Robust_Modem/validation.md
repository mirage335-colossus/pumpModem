# Native appraisal validation record

Updated 13 November 2026. This record retains actual checks, diagnostic outcomes,
corrective history and the evidence limits for the contained appraisal. No
scenario qualifies a real environment, universal receiver sensitivity floor,
or complete-receiver CPU/GPU capacity.

**Legacy observation-horizon scope:** the integration smoke, exploratory
AWGN receiver trend, 12-episode weak-signal pilot, 4,096-cell full-message EME
episode, and three-pair RunPod CPU/CUDA campaign used an appraisal horizon with
three quiet opportunities. The appraisal geometry is now `q0 + 2`, exactly
two scored quiet slots, removing one unnecessary future observation. This is a
driver/horizon change, not a production `CompleteStreamReceiver` algorithm
change. Keep those older outcomes bound to their original horizon and source
identity. Separate 0 dB-Hz AWGN episodes below use the corrected five-slot
horizon. EME one-slot diagnostics do not exercise quiet completion.

## Current source and build identity

The latest post-correction local Release app uses GCC 14.2.0, OpenSSL 3.5.7,
the canonical `IndependentApskV2` stack, and source commit
`5f2cc949e4adb9a185b3cc0a64ef49e2ec3f7e89`. Native source SHA-256 is
`be47453d07fb27afe4cf4c168f76390de193cf57a403009a1078dccca6f01aa2`; appraisal
source SHA-256 is
`409677cd1b9e4d06e12462a48cef88d9570b7dc7872cff988e1bbfc567902900`; build
options SHA-256 is
`3636f58f5e5803a48c78c56be3de84fce810a285a8b7c7222781040e596e6206`; compiled
app SHA-256 is
`db1df62ca19f1d82da1fa02ad29f624ab337d795893043a22e9d7e563bd280d3`. These
fingerprints bind the current check record, not environmental validity.

## Current local deterministic checks

The recorded command was:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

All four appraisal CTest groups passed in 9.35 seconds:

| Test | Scope |
| --- | --- |
| `high_fidelity_channel` | Renderer state, deterministic transforms, stochastic checks, and exact interpolation-cache/fallback comparison |
| `high_fidelity_scenarios` | Scenario parameters, units, rejection bounds, lunar-kernel moments and grid comparisons |
| `high_fidelity_integration` | Canonical native short-message source, receiver, quiet/EOF, noise, budget, and VLF geometry controls |
| `high_fidelity_runner` | 20 runner unit cases covering manifest/receipt identity, backend retention, bounds, and fixed-sample reporting |

The separate canonical Triad suite passed 32/32 checks in 64.90 seconds. Its
contained CMake test harness forced inclusion of a header missing from the
pre-existing CPU-only canonical probe; the original setup failure was retained.
This was a test-harness workaround, not a production modem source change. These
checks are software evidence, not empirical channel or capacity qualification.
The final appraisal log is the ignored `.build/checks-final.log`. Tests verify
two scored quiet slots, keep censored episodes distinct from observed
communication failures, require actual CUDA execution when retained, and reject
generic GPU fallback.

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

The new `eme-disk-slot` renderer uses a Doppler-aligned 65,536-cell quadrature.
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

## Remaining qualification boundaries

- There is no held-out field transfer/noise recording or calibrated antenna,
  ground/terrain, HF episode, or target-band whole-disk 5.8 GHz channel. The
  implemented models remain the calculated/reference/synthetic hypotheses in
  [models.md](models.md).
- The smoke campaign used short `smoke` geometry and the AWGN trend used only
  two fixed seeds per receive cell; neither provides a complete high-fidelity
  sensitivity curve. All 12 `weak-signal-pilot` episodes executed, with two
  censored time limits, so the campaign ended `incomplete_checks`. The separate
  full VLF reference receive was incomplete at its declared wall limit, as
  described above.
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

Keep the smoke raw receipts and incomplete VLF receipt while their
implementation-validation and diagnosis purposes are active. When those
questions are resolved, preserve the concise outcomes and fingerprints above,
then remove redundant ignored outputs. Do not rerun or overwrite the VLF
receipt without a separately bounded, predeclared follow-up.

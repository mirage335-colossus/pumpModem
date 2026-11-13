# Native implementation options

This chapter supplies the missing implementation discussion referenced by
[00](00-method-and-modem-context.md), [33](33-hf-channel-models-and-simulators.md)
and [60](60-simulation-methods-and-validation.md). It is a **design proposal**
supporting the [completion plan](80-completion-plan.md), not an implemented or
benchmarked simulator. It extends the existing
[Astra native options](../Astra-6/60-native-implementation-options.md) and
[appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
with current interface and build findings rather than creating a second protocol.

Initial source inspection used `07bcbc5da41ea5e8ec43517ffb92e51710037744`; the completion pass rechecked interfaces at `33f6e461fce12f4079870091b6d1aaa9c2f50821`. The production
`src/`, `include/`, `tests/`, `cmake/` and `docs/` trees have no changes between
the chapters' cited `0801e5c7` and that revision. This establishes source
continuity, not the accuracy of every earlier interpretation or planner receipt.

## Existing integration route

Use an independently invoked appraisal-owned C++ executable with the following
flow. All intensive source, channel and receiver work stays native and in process.

```text
declared scenario and public receiver configuration
    -> canonical profile and message preparation
    -> canonical composite Helper and DATA emission
    -> continuous native physical channel and equipment stages
    -> canonical complete stream receiver
    -> outcomes compared with a separate evaluator truth timeline
```

The truth timeline is for evaluation. It must not reveal transmitted bits,
termination time, realized fades or exact hidden channel parameters to the
receiver. Declared ephemeris assistance and calendar knowledge are different:
they are explicit receiver assumptions whose availability and error are tested.

| Responsibility | Existing interface and source | Planning consequence |
| --- | --- | --- |
| GUI-equivalent preparation | `live::detail::prepare_profile`, [robust_profile.hpp](../../../include/datapump/robust_profile.hpp) | Reuse current setting and duration preparation when claiming the GUI's configuration; disclose fixture overrides |
| Public geometry and emission | `PublicProfile`, `Emission`, `EmissionOptions`, [triad_stream.hpp](../../../include/datapump/triad_stream.hpp) | Generate the real three-carrier DATA and Helper composite; do not build another waveform |
| Message and FEC | `PreparedTextMessage`, [triad_message_receiver.hpp](../../../include/datapump/triad_message_receiver.hpp) | Use native bounded message generation and codec processing |
| Synthetic reference channel | `StreamingChannel`, `StreamChannelConfig`, [triad_modem_v3.hpp](../../../include/datapump/triad_modem_v3.hpp) | Retain explicit Off and existing synthetic controls; a new physical renderer can consume the same canonical source |
| Continuous receiver | `CompleteStreamReceiver::push`, `drain_recent_slots`, `drain_messages`, `report`, [triad_stream_receiver.hpp](../../../include/datapump/triad_stream_receiver.hpp) | Feed I/Q plus validity, honor admission/backpressure, commit through the production receiver |
| Real audio | `AudioInput::project`, `real_pcm`, [robust_audio_frontend.hpp](../../../include/datapump/robust_audio_frontend.hpp) | Qualify PCM projection separately from direct I/Q injection |
| Native acceleration | [triad_acceleration.hpp](../../../include/datapump/triad_acceleration.hpp) | Preserve requested versus actual execution and fallback reporting |

The injection loop in [triad-receiver-probe.cpp](../../../tools/triad-receiver-probe.cpp)
is an example to follow, not source to copy. Its CLI is not a generic arbitrary
capture loader. `score_receiver_slot` is useful for bounded component work but
does not qualify continuous message reception or scheduler throughput.

Two source details affect the design. First, `PatternSource` has rational sample
rate support, but the complete `PublicProfile` stream currently rejects a sample
rate denominator other than one in [triad_stream.cpp](../../../src/triad_stream.cpp).
A low-rate latent fading process is feasible; a sub-Hz complete-stream clock
cannot be assumed available. Second, the existing `ray_coherence_seconds` option
already supplies proper-complex OU gains with exponential covariance. The
continuous-time ideal has a Lorentzian spectrum; sampling yields its aliased
discrete-time counterpart. Chapter 00 now corrects its earlier blanket claim of
Lorentzian absence. Arbitrary measured spectra, coupled scattering and empirical
calibration remain different requirements.

## Exact fixture and public receiver mapping

[00 section 7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract)
is the selected source contract. In particular, a positive DATA IF is required
even for translated complex injection; `Format.encrypt=true` needs the two-root
`Crypto::amateur()` fixture rather than the single 32-byte constructor; the
Helper power option is total requested power, so equal total/DATA values give
zero Helper. Native Emission already normalizes DATA with its bounded fixed-bit
reference. Use its `mu*data_gain²` reference once when assigning physical power.
A nominal 16 s HF request returns 23.2 s active geometry, and exact integer
ceilings determine pulse halos. Save returned values rather than infer them
from rounded historical manual rows.

Construct `ReceiverConfig` from pinned defaults plus the explicit per-scenario
bounds. Match `frequency_bound_hz` to profile `cfo_bound_hz`, relative clock
bounds, profile arrival interval, and spread with the exact ms-to-seconds
conversion. Set endpoint clocks and profile `audio_pair_ppm` consistently.
Use `data_basis_gain=physical_multiplier*Emission.report().data_gain` for a raw
PatternSource basis, with public noise variance in the same sample/physical
plane. A non-flat or nonlinear front-end comparison must state its fixed public
calibration rather than insert evaluator-known instantaneous gain/noise.

There is **no independent native drift-bound field in Hz/s**. The varying-channel
common/atmospheric fields are hertz; the fine bank derives slope hypotheses
from their values and source duration, with its own admission limits. A declared
physical drift envelope is not proof that this receiver family can represent it.
Preserve all configured hypotheses and report model mismatch or insufficient
resource admission as a result. No source-only inspection qualifies acquisition
or capacity.

## Candidate channel representations

Choose the simplest representation that preserves the observables needed for
the scientific question. Keep a more detailed alternative only when it can
change a consequential answer or bound a material uncertainty.

| Candidate | Useful first application | Cost and limitation | Discriminator before adoption |
| --- | --- | --- | --- |
| Deterministic response or slowly varying transfer | Antenna/matching filters; smooth-earth reference; known bulk Doppler | Cheap after preparation; environmental uncertainty and rapid fading are absent | Reproduce gain, phase, delay and reference-plane conventions against independent references |
| Conditioned stochastic taps | Standard HF controls and a measured locally stationary episode | Scales with active taps and sample count; independent taps impose an uncorrelated-scattering assumption | Match delay profile, spectrum, temporal/cross-frequency covariance and fade durations |
| Coupled delay–Doppler representation | Disturbed HF; reduced lunar-scattering model | More state and preparation; nonstationarity and interpolation must be controlled | Match the same joint observable and estimator as the source, including censoring |
| Geometry plus scatterer or surface sum | EME with beam weighting; selected ground/ionospheric subproblems | Scales with scatterers and update rate; geometry alone does not determine scattering amplitudes | Convergence of integrated power, spectra and covariance, then comparison with independent data |
| Measured transfer replay | A specific calibrated sounding episode | Limited recording bandwidth, duration and dynamic range; repetitions are not independent events | Reproduce held-out measurements without leaking sounder noise or known-message information |
| Recorded additive-noise replay | Site-specific atmospheric or interference bursts | Receiver transfer and recording saturation may already be embedded | Verify calibration and noise-only content; avoid applying the recorded front end twice |
| Detailed external physical solver used offline | Difficult terrain, waveguide modes, ionospheric rays or beam geometry | Inputs and licenses may dominate; solver detail is not evidence of better prediction | Demonstrate a relevant discrepancy that a reduced model cannot represent |
| Learned surrogate | Later acceleration of a validated expensive model | Tail, phase and out-of-domain behavior can be unreliable | Independent episode/geometry holdouts and stress counterexamples; no initial dependency |

The first implementation should contain deterministic references and one
scientifically ready stochastic or replay branch. Do not implement every listed
alternative in advance. Use native reference libraries where suitable, after
checking license, version, conventions and domain; avoid recreating mature
ground-wave or ephemeris solvers merely for uniform style.

## State and signal conventions

The executable specification must define an operator such as

```text
y(t) = sum_p a_p(t, f) x(t - tau_p(t)) exp(j phi_p(t)) + n(t)
```

with a precise realization of frequency dependence. That notation does not
license independent per-carrier gains: a common linear operator on the complete
emission must generate the intended cross-frequency correlation. A polarimetric
model may require vector fields or a Jones matrix before projection onto the
receiving antenna; a scalar reduction needs a stated validity condition.

Specify RF-to-baseband signs, phase reference, bulk versus excess delay, physical
versus sample time, and how a moving nondispersive delay produces phase and
Doppler. Do not count that geometric Doppler again as an independent shift.
Dispersive ionospheric phase and group delays need their own relation. Station
clock error, propagation motion and deliberate Doppler compensation remain
separate sources with declared shared or independent references.

For an absolute link, calibrate at named planes: source power, radiated power,
received field or antenna-terminal power, receiver noise and digital units.
Account for antenna efficiency, polarization, matching, feed loss and external
versus receiver noise exactly once. The present streaming baseline uses complex
noise variance `E|n|^2`; profile preparation uses
`C/N0 = tx_dbm - path_loss_db - noise_dbm_hz` and variance
`Fs / 10^(C/N0/10)` in its normalized signal convention. `effects.snr_db` is not
the streaming noise control. These conventions must not be mistaken for watts
per digital sample or a physical noise calibration for every environment.

Place propagation, external noise/interferers, receiver input filtering,
nonlinearity/AGC and conversion in a physically justified order. Specify whether
a dataset already includes any of those stages. Never renormalize each episode
or bit to its realized received power; that would remove fades or outages.
Record finite bandwidth and truncation for heavy-tailed noise models; an
infinite-variance distribution cannot simultaneously promise a finite physical
noise power without an explicit modification.

Maintain all state through chunk boundaries, transmitter silence, erasures,
quiet slots and delayed tails. A validity-marked missing observation is not a
valid noise-only sample. Counter-based randomness or an equivalent indexed design
should make seeds reproducible under chunking and worker scheduling. Independent
seed domains must not break intended environmental dependence.

Low-rate latent processes, analytic state advancement and bounded rendering of
requested windows can make long physical episodes tractable. They require
stationary initialization where appropriate, preserved history for filters and
delays, and the same elapsed physical time. Time-compressing a 96-minute bit,
resetting a process at each window, or supplying only favorable windows changes
the question. If full reception is unaffordable, report a component result.

## Contained build options

The canonical root [CMakeLists.txt](../../../CMakeLists.txt) creates
`datapump_triad`, `datapump_triad_receiver` and `datapump_robust_session` even
with `BUILD_TESTING=OFF`. These are source-tree targets, not a published installed
modem SDK. Link their transitive dependencies rather than maintaining copied
source lists or manually reconstructing codec and acceleration link flags.

A naive outer appraisal project using `add_subdirectory(repository)` is unsafe:
[VendoredLzma.cmake](../../../cmake/VendoredLzma.cmake) resolves canonical inputs
against `CMAKE_SOURCE_DIR`. Two plausible approaches remain:

1. Configure the canonical root and pass an appraisal-owned
   `CMAKE_PROJECT_DataPump_INCLUDE` hook. The hook schedules appraisal target
   registration at the end of root processing with `cmake_language(DEFER ...)`.
   Keep hook, target definitions and build output under the appraisal directory.
   The [project include hook](https://cmake.org/cmake/help/latest/variable/CMAKE_PROJECT_PROJECT-NAME_INCLUDE.html)
   and [deferred calls](https://cmake.org/cmake/help/latest/command/cmake_language.html#deferring-calls)
   are documented CMake mechanisms. **Their combined use here has not been
   configure/build tested.** Pin a compatible CMake version and check variable
   lifetime, single registration, target visibility and transitive dependencies.
2. If that route proves fragile, propose a small explicit root registration or
   reusable package/export change. This needs later authorization for edits
   outside the contained directory; it must not be silently worked around by
   copying production source.

The first future implementation check is a bounded configure/link smoke test
of the selected route. Existing `build.sh` supports a separate build directory
and additional CMake arguments. Do not publish an untested new invocation as an
available command. A successful link would establish integration only, not
scientific fidelity.

## Resources and validation

Keep the [receiver allocation contract](../../../docs/development.md#receiver-cpu-allocation):
resolve `W = 2*H` once from effective affinity/quota-aware hardware capacity,
respect exact explicit caps, and share one bounded receiver executor. Charge
channel buffers and latent state in addition to the receiver's full live-owner,
DSP, input, stack and retained-result bounds. The simulation must not hide
unbounded precomputation outside its memory report.

The literal EME reference is 256×512 cells at 48 ksample/s: over 1800 s
that is 11,324,620,800,000 cell-sample products before receiver work. Fifteen
sub-9 kHz complete messages already span at least 421.875 physical hours.
Neither is an automatically admitted campaign. Chapter 86 proposes a short
component calibration and independent holdout, a ten-minute pilot limit and a
thirty-minute upper budget for a particular complete-message campaign. These
are proposed bounds for that design, not project or user mandates. Before any
campaign, set a geometry-appropriate, predeclared budget from the scientific
question, minimum required endpoints, resource forecast and stopping rule. A
budget that cannot admit required work leaves that work unrun or the design
unadmitted; it does not waive endpoints. Reconcile the proposed pilot with the
separate 12+6 failure-finding pilot in the adjacent appraisal protocol, keeping
diagnostic failure finding distinct from conditional probability inference.
A reduced delay–Doppler or low-rate latent representation can change cost
substantially, but needs a small exact-reference comparison before its results
stand in for the literal model. These operation counts are not processor
timing estimates or permission to rent hardware. See the research-plan audit
items [C13](87-evidence-remediation-audit.md#c13).

Choose bulk arithmetic by the
[automatic capacity contract](../../../docs/gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling).
Sensitive long-time coordinates or phase may need higher precision without
forcing every bulk operation to FP64. State precision error separately from
physical-model uncertainty. GPU capability, compatible builds and CPU fallback
are not GPU execution evidence. No cloud spending is authorized by this plan.

Reuse existing tests for canonical behavior. New channel work needs focused
checks for identity, units, signs, delay and tail support, covariance, chunk
invariance, stationary start, nonlinear recovery and resource rejection. Run
the applicable [named test groups](../../../docs/building.md#testing-stages)
after implementation. Only then perform bounded source-observable and receiver
pilots under the existing appraisal protocol. Keep generator verification,
empirical validation, complete-message behavior and hardware capacity separate.

An option advances when it explains a consequential effect with credible
inputs, passes its mathematical checks, and has an affordable validation path.
Reject or simplify it when added detail is unidentifiable, unsupported or
irrelevant to the receiver outcome. Preserve the reason and conditions for
reconsideration rather than retaining an abandoned implementation.

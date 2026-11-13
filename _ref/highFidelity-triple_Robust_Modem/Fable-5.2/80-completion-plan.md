# Research completion and implementation handoff

Updated 13 November 2026. This document records the completed research and
specification work and the concrete work that remains before a simulator or
receiver result can be claimed. The original request prioritized serious
literature research and prohibited edits outside this Fable-5.2 directory.
Accordingly, this pass supplies **a research foundation and selected
mathematical experiment specifications**, not a built simulator, new field
measurements, real-link qualification or complete-receiver performance results.

The [remediation audit](87-evidence-remediation-audit.md) answers the historical
search-budget question directly. Renewed searches and full-source inspections
have addressed those work obligations; remaining source/data limitations are
individually stated rather than attributed to an exhausted web allowance.
Several formerly inaccessible sources and datasets were retrieved successfully.
No claim of universally exhaustive literature coverage or claim-by-claim
certification of every historical sentence is made.

The five requested families all have mechanism/evidence syntheses, explicit
moderate/control-to-severe/outage alternatives, numerical specifications,
contradictions, implementation choices and independent validation routes.
**Observed conditions, standardized tests, derived predictions and deliberately
constructed stresses remain different evidence classes.** A complete specified
stress does not close a missing measured-environment branch. Better models and
implementations may still be found.

## 1. Scope and source baseline

This execution pass began at clean
`33f6e461fce12f4079870091b6d1aaa9c2f50821`, after the original research and
planning/audit commits. All changes remain under
`_ref/highFidelity-triple_Robust_Modem/Fable-5.2`. Production source, maintained
guides and `_appraisal` were read only. No commit, publication, messages to
researchers, paid access, hardware rental or field experiment was performed.

The eventual independent suite belongs under
`_appraisal/highFidelity-triple_Robust_Modem`, with the canonical native C++
modem linked in process. Writing it is a later task with that directory in scope.
The adjacent [Astra synthesis](../Astra-6/README.md) and
[appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
remain useful inputs. Their budgets/references do not automatically incorporate
this research or qualify the later implementation.

## 2. Completed research work

| Work | Durable result |
| --- | --- |
| Original notice/source inventory | Chapter-local source/topic dispositions and [87](87-evidence-remediation-audit.md)'s N1–N7/G01–G17 map; old missing-source lists were investigated individually |
| Foundational and recent literature | Updated 10–60 with actual query/date/route records, inspected primary passages, source-access status, contrary findings and applicability limits |
| Data feasibility | VLF/Arctic HF archive inspection, EME SigMF/PDS metadata, full large NTIA reports; distinguish processed observables, waveform data, calibration and licensing |
| Source and receiver interface | [00](00-method-and-modem-context.md) pins deterministic native source/key/codec/calendar, positive IF, actual timing, Helper power semantics, power normalization and hidden truth |
| Five-family specifications | [81](81-scenarios-sub9.md)–[84](84-scenarios-eme.md) close selected mathematical choices; [85](85-scenarios-noise-and-equipment.md) supplies explicit shared noise/equipment controls |
| Numerical/statistical credibility | [60](60-simulation-methods-and-validation.md), [86](86-statistics-and-calculation-checks.md) and [90](90-cross-check-calculations.md) separate checked arithmetic, unexecuted renderers and empirical validation |
| Native implementation handoff | [70](70-implementation-options.md) maps real interfaces, containment/build choices, physical state and computational costs |
| Independent review | Source/domain, native-interface, scalar/statistical and completion reviews corrected residual overclaims and cross-chapter inconsistencies |

Material corrections include the sub-9 kHz 5,760.4 s tile and integer sample
halos; dynamically placed high Helper pilot; total versus DATA/Helper power;
native DATA normalization; MF spectral-bound versus coherent-phase claims;
radar range censoring and inverse-fitted sea-state comparisons; decoded versus
scheduled-pulse denominators; orbital-versus-whole-disc radar transfer;
man-made versus atmospheric impulse models; and fixed-sample versus sequential
inference. The owning chapters retain the evidence and uncertainty.

## 3. Evidence gates

| Gate | Required evidence | Present status |
| --- | --- | --- |
| Research handoff | Traceable selected inputs, physical applicability, explicit laws/alternatives, contradiction and validation routes | Supplied for all five families, with the open empirical branches below |
| Mathematical branch specified | Two implementers need not choose a new scientific distribution, geometry rule, calibration constant or state law | Selected controls in 81–85; conditional physical/replay extensions are separately labeled |
| Experiment ready | Inputs acquired, versions/licenses checked, numerical method and resource admission implementable on the selected machine | Requires per-branch setup/build/admission checks; not granted to the whole suite |
| Generator verified | Independent mathematical checks of implemented units, state, covariance, delays, tails and convergence | Scalar checks only here; no channel renderer built |
| Reference reproduced | Same instrument/estimator/domain reproduces a source observable | Not established by literature extraction or scalar agreement |
| Empirically validated | Independent held-out measurement episodes and uncertainty/residual analysis | Not established for the target installations/bands |
| Receiver characterized | Full canonical receiver, physical completion and declared outcomes over admitted independent episodes | No receiver campaign run |
| Capacity qualified | Complete same-stream work/memory on identified hardware and executed arithmetic | No new capacity evidence |

A source-limited empirical branch remains open even when its mathematical
comparator is specified. None of these gates can be passed by deleting a notice,
counting papers, matching another implementation's arithmetic or obtaining an
unreviewed archive.

## 4. Per-family handoff and unresolved evidence

| Family | Selected starting points | Remaining empirical limit and next falsifier |
| --- | --- | --- |
| Sub-9 kHz / 200 ft | 81's magnetic-field controls, explicit 61 m RLC/detuning model, fixed noise/phase/outage trajectories, pinned LMP relative-Ez calculation | No matching calibrated 61 m installation below 9 kHz; waveguide E/B conversion and below-4-kHz discrepancy remain. First compare circuit impedance/current and source-matched waveguide field/phase, then long-duration calibrated noise; do not claim range from the magnetic control. |
| Groundwave 1 MHz | 82 1M-A/B smooth/mixed amplitude; C homogeneous/layer-boundary discriminator; D coherent night path; E normalized weak-signal/outage controls | No source-matched 1–600 s coherent groundwave amplitude/phase population. Verify reference-radiator conversion, LFMF outputs and mixed-path limits before comparison to actual field/phase data. |
| Groundwave 30 MHz | 82 30M-A sea/smooth reference; B prescribed perturbation; C1 knife-edge and C2 direct/reflected geometry; D competing coherent path; E severe loss/disappearance | Surface/space/diffraction/ducting mechanisms may differ by site. Full rough-sea propagation and measured replay remain conditional. Radar working-range and selected/ducted HF sea-path measurements cannot define generic communication loss. |
| Skywave 1–30 MHz | 83 F.1487/DRM controls, literal Watterson component tuples, explicitly hypothetical joint/microtap/O–X/transition/outage laws | Processed sounding and detected-burst summaries do not establish coherent fine-spacing covariance or natural transition rates. Compare source-matched estimators and censoring before fitting a joint process; retain Gaussian/OU/shared-mode disagreements. |
| Earth–Moon–Earth | 84 fixed T52/Hagfors/diffuse alternatives, pinned retarded geometry/orientation acquisition, power/thermal/polarization/clock/pointing and outage controls | No calibrated whole-disc 5–6 cm IQ validation has been produced. Execute geometry/convergence first; compare actual echo spectra/polarimetry within their band and geometry. Orbital SAR and 433.8-MHz records constrain different quantities. |
| Shared noise/equipment | 85 C0–C4 explicit power waves, finite pulses/clustering, tones/sweeps, nonlinear AGC/ADC, oscillator and linear clock controls | Constructed parameters are not a target receiver or natural occurrence distribution. Device response and calibrated waveform records are needed before a hardware/site claim. |

The research addresses moderate through severe conditions by preserving several
kinds of evidence, not by inventing a universal worst-channel percentile.
An arbitrarily absent wanted signal is an outage. Joint maxima from unrelated
sources are not an observed event or a valid population draw.

## 5. Source and data status to preserve

Each selected numerical value carries a measured, standardized, calculated or
assumed status and its reference plane. Chapter-local tables record source
identity, inspected passage/figure, frequency, geometry, bandwidth, estimator,
selection/censoring and transfer limits. Inaccessible nonselected works remain
leads; they do not supply implied support.

The full Gu, Chilcote, Halverson, Hansen, CCIR, Gherm/Heitmann, Fockens/Giesbrecht,
Lemmon/NTIA and recent lunar/polar sources materially improved the synthesis.
The actual query logs are those recorded during this pass; earlier searches
without logs cannot be reconstructed. No bounded unsuccessful search proves
that a source or measurement does not exist.

Public Gu and QAN–ALE repositories now have inspected metadata and bounded
archive-content checks in 12/32. Their license, variables and observation
selection determine which analyses are possible. A record is not coherent IQ
merely because it has complex values or a MATLAB filename. A processed data
archive can support summary/availability checks while remaining unsuitable for
sample-level channel replay. EME SigMF metadata has similarly narrower claims
than analyzed calibrated I/Q.

No raw dataset or whole paper is added to the repository. Temporary downloads
serve this active source/schema review and are removed after its findings and
retrieval identities are recorded. Future implementation must reacquire and
hash its selected inputs, retain only data indispensable to a named validation
question, and set a cleanup trigger.

## 6. Common physical and native contracts

Read 00's exact fixture before implementation. Use the canonical prepared
message, deterministic key, returned source support and public calendar. The
three DATA carriers and Helper traverse one physical channel. Match source,
channel output and receiver sample rate/IF. Normalization uses the native
bounded DATA reference, never a realized fade or received-message average.

The receiver receives declared public search/clock/arrival/support bounds and
noise/gain calibration. It does not receive realized channel truth, seeds,
message length, source endpoint, future availability, or the experiment's
success labels. Ephemeris aid must be explicit and imperfect where specified.
No separate receiver drift field in Hz/s may be invented from Hz-valued native
options; keep hypothesis-model limitations and computational admission visible.

All phase, delay lines, filters, oscillator, noise, AGC and ordinals continue
through chunks, gaps, fading and silence. Missing samples retain invalidity;
they do not become zero samples or a compressed time axis. Source EOF and
budget exhaustion are not successful completion.

The common evaluator horizon uses the first public slot q0 whose entire
scoring-input window follows source end plus declared propagation/filter
support, then observes through q0+2, including lookahead. Physical message
completion still requires two scored quiet slots. An explicitly longer fixed
horizon, such as EME's 1800 s, must contain that minimum and retain all state.
Interior two-quiet fade counterexamples retain later transmitted data and are
scored as premature completion when appropriate.

## 7. Contained implementation sequence

Do these stages in order under a later appraisal-directory task. No stage is
implicitly executed by this plan.

1. **Configure and link one native harness.** Try 70's root-CMake include/defer
   route without copied source. Verify target visibility, dependencies, single
   registration and build-output containment. If it fails, record the exact
   external integration edit needed before seeking broader scope.
2. **Prove identity and units.** Generate the frozen native source, verify its
   actual codec bits/geometry/power reference and Helper placement, and pass an
   identity channel. Validate complex noise PSD, field/power conventions,
   positive IF and receiver calibration separately from reception probability.
3. **Implement deterministic references.** Finite delay/filter, RLC, normalized
   path, ray/diffraction and scalar lunar geometry checks come before large
   stochastic renderers. Use limiting cases and independent references.
4. **Implement one stochastic discriminator.** Start with an exact small
   covariance or sampled OU reference, then the qualified streaming generator.
   Check stationary initialization, distribution/covariance/spectrum, long-time
   behavior and chunk invariance. Preserve the same physical episode across
   matched implementations.
5. **Add the selected physical solver adapter.** Pin source/version/license,
   inputs, units, numerical controls and warnings. Separate offline environment
   preparation from the in-process native source/channel/receiver. Do not turn
   a research solver into a second operating modem implementation.
6. **Add shared equipment and completion tests.** Compare identical upstream
   streams through linear and nonlinear controls. Check pulse energy, overload,
   AGC recovery, clock interpolation and quiet-slot tails before decode scoring.
7. **Reproduce a source observable.** Match its actual band, antenna, threshold,
   bandwidth, estimator and selection. Record residuals and sensitivity to
   uncertain inputs. Calibration and held-out evaluation must be distinct.
8. **Admit a bounded full-receiver pilot.** Only after computational and
   mathematical gates pass, use the exact 86 strata and outcomes. Retain
   unsuccessful search, tracking, reacquisition, quiet completion and resource
   failure in the accounting. Then decide whether new empirical inputs or a
   better model would change the conclusion.

## 8. Computational admission and statistical decisions

Chapter 86's fifteen literal strata and at most 195 independent episodes are
conditional **statistical ceilings**, not a request to execute that campaign.
The first later pilot has a ten-minute wall budget and begins with at most a
60-second calibration plus abbreviated holdout for one admitted component
stratum. A complete-message campaign needs an applicable upper cost forecast
of at most thirty minutes, with actual hardware, full `W=2H`, memory admission
and all required receiver work included. If unavailable or unaffordable, leave
it unrun/inconclusive. Do not time-compress the signal, silently shrink public
hypotheses, or extend to days after seeing results.

Fifteen sub-9 kHz episodes already represent at least 421.875 hours of physical
stream before the extra quiet margin, preroll and tails. The literal 256×512
EME cell grid at 48 ksample/s for 1800 s represents
11,324,620,800,000 cell-sample products. Those are scale estimates, not measured
CPU/GPU costs. Low-rate latent state, delay–Doppler aggregation and qualified
native bulk work may make bounded experiments feasible; a reduced renderer
must reproduce the selected observables against small exact references first.
No cloud rental or spending is authorized by those possibilities.

Fixed diagnostic seeds prove reproducibility, not iid sampling or natural
occurrence. Future independent episode draws, estimand, population, allocation,
confidence rule, multiplicity, censoring and stop rule are fixed before results.
The selected small-n intervals will often be inconclusive. A point failure,
resource rejection, missing required work or unrun condition is never a pass.

## 9. Validation ladder and acceptance checks

| Check | Acceptance evidence | Failure consequence |
| --- | --- | --- |
| Source/API | Native fixture matches pinned values, positive IF, actual sample rate and deterministic DATA normalization | Correct the manifest/harness; no physical result |
| Dimensions/limits | Independent scalar identities and limiting cases; no E/B or gain/noise-plane substitution | Correct affected scenarios and all dependent conclusions |
| State and numerical fidelity | Fixed finite realization, convergence tolerances, stationary/coupled initialization, chunk invariance and full tails | Simplify/refine before receiver interpretation |
| Source reproduction | Matching estimator/domain and documented residuals/censoring | Preserve disagreement; do not call the environment validated |
| Independent measurement | Held-out episodes not used to fit ground/antenna/kernel/noise parameters | Restrict claims to the calibration domain or control |
| Complete receiver | All required work, true physical completion, correct message and exact resource limits | Retain separate failure reasons and denominators |
| Capacity | Measured complete same-stream service and workspace on named device/arithmetic | No claim for another target or changed runtime |

Mathematical consistency does not establish a real-world error bar. In
particular, chapter 90's historical groundwave magnitudes have not been
recertified by its 144 field/loss conversion checks. The selected official
solver must still be executed and independently compared.

## 10. Decisions that remain legitimately open

- Which independently calibrated site/antenna and receiver measurements will
  qualify the proposed physical models; the absent matching data cannot be
  manufactured from nearby-frequency studies.
- Whether an added physical mechanism changes the modem outcome enough to
  justify its cost, after a cheaper comparator and measured residual are known.
- Which streaming approximation meets the mathematical and resource gates for
  a specific admitted workload. Candidate equations are not performance results.
- Whether the tested native receiver's hypothesis family is adequate for a
  particular physical process. Research cannot infer acquisition/reliability
  from local tracking or source geometry.
- Which additional scope is needed if contained build integration fails or if
  useful findings are to be adopted into maintained guides/production code.

These are later implementation/measurement decisions. They do not justify
leaving accessible consequential literature unread, and they must not be
presented as already solved.

## 11. Documentation acceptance and retention

The final document checks cover local links/anchors, fenced blocks, whitespace,
source-status consistency, shared fixture/horizon/power conventions and the
edit boundary. Independent reviews and scalar checks are recorded in 86/87;
unexecuted software/physical checks remain labeled. Documentation-only work
does not require receiver benchmarks.

Keep the research while it supplies concrete scientific provenance and design
requirements. After authorized adoption, distill unique positive, negative and
conditional findings into the existing maintained guides and appraisal records,
verify their provenance survives, then remove superseded redundant material.
Do not retain whole campaigns or alternative implementations for speculative
future use, and do not delete another contributor's work as part of this handoff.

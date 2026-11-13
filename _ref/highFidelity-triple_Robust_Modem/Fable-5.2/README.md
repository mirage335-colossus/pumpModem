# High fidelity weak signal research completion

This directory contains the completed **research and experiment-specification
handoff** for the five requested weak-signal environments: sub-9 kHz with a
200 ft antenna, 1 MHz and 30 MHz groundwave, 1–30 MHz skywave, and
Earth–Moon–Earth. The [completion handoff](80-completion-plan.md) records what
was completed and the ordered implementation/validation work that follows.

The earlier web-search-budget notices have received substantive follow-up.
The [remediation audit](87-evidence-remediation-audit.md) maps all seven
historical notice sites and the underlying source/model obligations to their
current outcomes. Newly accessible full papers, large NTIA reports and public
VLF/HF archives were inspected; source and scenario claims were corrected.
Some named sources and target measurements remain unavailable or insufficient.
Those empirical limits remain explicit and are not attributed to an exhausted
search allowance.

The selected mathematical controls now specify source fixtures, physical
planes, probability/dependence laws, initialization, trajectories, uncertainty
alternatives and validation checks. Their assumed parameters are not promoted
to observed natural distributions. No simulator was built, no complete modem
trial or capacity measurement was run, and no target physical link is qualified.
The design remains open to better evidence, models and implementations.

The sixteen original chapters contained about 234,000 whitespace-delimited
words at the first planning audit. This is provenance, not evidence that every
historical sentence is certified. The completion pass concentrated primary-source
and independent checks on consequential claims and selected experiment inputs,
with chapter-local dispositions for unresolved contextual leads.

## Reading map

| Subject | Existing research | Completion focus |
| --- | --- | --- |
| Evidence, units and modem context | [00](00-method-and-modem-context.md) | Correct source-derived overstatements, separate source type from review status, bind scenario assumptions |
| Sub-9 kHz propagation | [10](10-sub9khz-propagation.md) | Waveguide validity, measured phase and level, transitions and model discrepancy |
| Sub-9 kHz antennas and practice | [11](11-sub9khz-antennas-links-and-practice.md) | Reproducible 200 ft geometry, matching and earth losses, usable bandwidth and field evidence |
| Sub-9 kHz noise | [12](12-sub9khz-noise.md) | Calibrated joint waveform statistics, site dependence, controlled blanking comparisons |
| Groundwave at 1 MHz | [20](20-groundwave-1mhz.md) | Smooth and mixed paths, ground uncertainty, phase/delay, skywave coexistence |
| Groundwave at 30 MHz | [25](25-groundwave-30mhz.md) | Surface versus space wave, diffraction and clutter; competing explanations of measurements |
| Skywave level and outages | [30](30-skywave-signal-level-and-outages.md) | Path availability, disturbance episodes and censoring |
| Midlatitude, NVIS and long-path dispersion | [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) | Matched estimators, O/X dependence and transition evidence |
| High-latitude and equatorial dispersion | [32](32-skywave-dispersion-highlatitude-equatorial.md) | Joint severe conditions and limits of radar proxies |
| HF models and simulators | [33](33-hf-channel-models-and-simulators.md) | Local stationary baselines, nonstationary alternatives and measured validation |
| Lunar scattering and geometry | [40](40-eme-lunar-scattering-and-geometry.md) | Beam-weighted coupled delay and Doppler, radar-to-EME transfer |
| EME systems and environment | [41](41-eme-systems-practice-and-environment.md) | Station geometry, calibration, pointing, clocks and system temperature |
| MF/HF noise and interference | [50](50-noise-and-interference-mf-hf.md) | Noise versus congestion, calibrated bursts and temporal dependence |
| Equipment | [55](55-equipment-and-front-end-effects.md) | Reference planes, measured transfer functions, clocks, nonlinear recovery |
| Simulation and statistics | [60](60-simulation-methods-and-validation.md) | Generator credibility, long-time behavior, bounded inference and model discrepancy |
| Native implementation planning | [70](70-implementation-options.md) | Existing interfaces, candidate representations, containment and build feasibility |
| Completion handoff | [80](80-completion-plan.md) | Completed research, per-family limits, ordered native implementation and validation gates |
| Sub-9 kHz scenarios | [81](81-scenarios-sub9.md) | Top-loaded reference, transition and fade; explicitly required path/solver inputs |
| 1 and 30 MHz groundwave scenarios | [82](82-scenarios-groundwave.md) | Surface, coastal, terrain, competing-mode and outage branches |
| HF skywave scenarios | [83](83-scenarios-skywave.md) | Standard controls, measured-mode parameters, geographic strata and censored observations |
| EME scenarios | [84](84-scenarios-eme.md) | 5.76 GHz bistatic geometry, link budget and coupled lunar scattering |
| Noise and equipment scenarios | [85](85-scenarios-noise-and-equipment.md) | Reference planes, observable front-end effects and conditional noise cases |
| Statistics and scalar checks | [86](86-statistics-and-calculation-checks.md) | Fixed-sample pilot design and independently recalculated examples |
| Search-limit remediation audit | [87](87-evidence-remediation-audit.md) | Historical notice census, substantive remedies, open evidence obligations and new source-access checks |
| Calculation appendix | [90](90-cross-check-calculations.md) | Reproducible arithmetic and physical applicability checked separately |

## Relationship to existing work

The adjacent [Astra-6 synthesis](../Astra-6/README.md) and
[appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
already provide a shorter overview and an experimental design. They are inputs
to this handoff, not work to duplicate. The appraisal directory currently contains
design documentation only. Its references to Astra-6 do not automatically make
the larger Fable-5.2 draft superseded, or authorize editing either destination.

The planning and subsequent research task change files only in this Fable-5.2 directory. Future
experiment code and operating/test documentation belong under
`_appraisal/highFidelity-triple_Robust_Modem`; changing that directory, canonical
source or maintained guides is a later scope decision. No such implementation
is implied by writing the plan.

The governing native architecture and validation contracts remain in
[the documentation index](../../../docs/README.md). In particular, keep one
canonical C++ modem, one continuous physical channel through Helper, DATA and
quiet tails, configured-calendar reception, two scored quiet slots for message
completion, and the full `W = 2*H` receiver worker budget.

## Evidence status

The research handoff is complete for the selected mathematical specifications.
Empirical branches remain conditional where calibrated time records, coherent
channel data, target antenna/front-end measurements or independent holdouts
are missing. Source retrieval, numerical consistency, generator verification,
measured physical fidelity, modem reliability and real-time capacity are
separate results; only the first two have new evidence in this directory.

The next implementation starts with a contained native link/identity check and
small numerical discriminators, then admits full-message trials only under
[86's explicit computational and inference limits](86-statistics-and-calculation-checks.md).
The long sub-9 kHz apertures and literal lunar surface sum must not become
routine multi-day campaigns. Preserve one canonical C++ receiver, continuous
Helper/DATA/quiet-tail state, configured-calendar scope, physical completion,
all public hypotheses and `W=2H`.

Keep these documents while they serve a concrete research/appraisal purpose.
After authorized adoption, preserve unique findings and provenance in the
maintained guides and appraisal records before removing superseded material.

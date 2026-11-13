# High fidelity weak signal research completion

This directory contains substantial but **incomplete** literature research and
candidate scenario specifications for the triple Robust Modem. The active
[completion plan](80-completion-plan.md) defines the remaining work and the gates
between research, experiment readiness, simulator construction and receiver
qualification. The [remediation audit](87-evidence-remediation-audit.md) shows
that historical search-budget and coverage gaps have only partly been remedied;
both explicit exhaustion notices still remain.

The November 13, 2026 planning audit found sixteen existing chapters, about
234,000 whitespace-delimited words, but no index or implementation-options chapter. The size of that
corpus is an inventory fact, not evidence that its claims have all been verified.
The initial planning additions supplied this index, a [native implementation options chapter](70-implementation-options.md), and the completion plan. The subsequent research pass corrected selected original chapters and added the scenario and numerical notes indexed below. The entire historical corpus has not been independently revalidated claim by claim.

No new simulator, communication trial, capacity measurement or physical-link
qualification is supplied by these planning documents. The design remains open
to better models and implementations.

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
| Completion work | [80](80-completion-plan.md) | Active source-by-source completion sequence, acceptance checks and remaining gates |
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
to this plan, not work to duplicate. The appraisal directory currently contains
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

## Completion standard

Research is ready when consequential claims and parameters are traceable,
contradictions and unknowns are explicit, all five families have implementable
scenario specifications, and each model has a credible independent validation
route. **That all-family gate has not yet been established.** Several candidate
scenarios still need numerical laws, solver/environment inputs or measured
records before they are experiment ready. A research-ready scenario may still
lack empirical validation. Simulator
verification, measured physical fidelity, modem reliability and real-time
capacity require their own evidence and must retain separate statuses.

Keep these documents while they serve that research and appraisal purpose.
Consolidate findings only after their destination and provenance are preserved;
do not delete the detailed research merely because an index or plan now exists.

# High fidelity weak-signal research

This directory contains the research for sub-9 kHz with a 200 ft antenna,
1 MHz and 30 MHz groundwave, 1–30 MHz skywave, and Earth–Moon–Earth.
The requested research work is complete as a literature, public-data analysis,
calculation and implementation-specification handoff. Independent all-family
review concluded that the research is ready to conclude; its remaining
scientific and qualification limits are explicitly recorded.

Start with the [research execution and conclusions](80-completion-plan.md).
The [evidence audit](87-evidence-remediation-audit.md) tracks all 200 original
work items and distinguishes completed analyses, contradictory or unidentified
physics, inaccessible source bodies, external measurements and later native
implementation. Detailed source passages, conventions, inputs, results and
reproduction recipes remain in the subject chapters.

The earlier web-search-budget notices have received substantive remedies:
additional primary texts, actual public-data analysis, official reference-model
runs and independent numerical checks. They are historical notices, not the
reason this work stops. A downloaded source, corrected calculation or failed
access route is not by itself scientific closure; each consequential conclusion
retains its evidence and domain limits.

The research does not establish a universal natural worst-case distribution,
qualify an implemented simulator, demonstrate complete-receiver communication
success, or measure hardware capacity. Some questions require calibrated
measurements that the inspected public sources do not contain. The documents
specify those inputs and preserve alternative models rather than inventing
missing distributions. Better models and implementations may still be found.

## Reading map

| Subject | Research chapter | Coverage |
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
| Research execution and qualification | [80](80-completion-plan.md) | Executed packages, conclusions, remaining evidence and later qualification |
| Sub-9 kHz scenarios | [81](81-scenarios-sub9.md) | Top-loaded reference, transition and fade; explicitly required path/solver inputs |
| 1 and 30 MHz groundwave scenarios | [82](82-scenarios-groundwave.md) | Surface, coastal, terrain, competing-mode and outage branches |
| HF skywave scenarios | [83](83-scenarios-skywave.md) | Standard controls, measured-mode parameters, geographic strata and censored observations |
| EME scenarios | [84](84-scenarios-eme.md) | 5.76 GHz bistatic geometry, link budget and coupled lunar scattering |
| Noise and equipment scenarios | [85](85-scenarios-noise-and-equipment.md) | Reference planes, observable front-end effects and conditional noise cases |
| Statistics and scalar checks | [86](86-statistics-and-calculation-checks.md) | Fixed-sample pilot design and independently recalculated examples |
| Evidence audit | [87](87-evidence-remediation-audit.md) | Original obligations, executed results, access/measurement limits and qualification boundaries |
| Calculation appendix | [90](90-cross-check-calculations.md) | Reproducible arithmetic and physical applicability checked separately |

## Relationship to existing work

The adjacent [Astra-6 synthesis](../Astra-6/README.md) and
[appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
provided a shorter overview and experimental design and were inputs to this
handoff. At the original 13 November 2026 scope review, the appraisal directory
contained design documentation only; that was the status then, not its current
contents. The directory has since grown into a contained suite with native
experiment code and source-bound results. See its
[current appraisal overview](../../../_appraisal/highFidelity-triple_Robust_Modem/README.md)
for executed work and limits. Those later results do not automatically make the
larger Fable-5.2 draft superseded, or authorize editing either destination.

The original planning and subsequent research task covered files only in this
Fable-5.2 directory. The plan placed future experiment code and operating/test
documentation under `_appraisal/highFidelity-triple_Robust_Modem`; changing that
directory, canonical source or maintained guides was a separate scope decision.
No such implementation was implied by writing the plan.

The governing native architecture and validation contracts remain in
[the documentation index](../../../docs/README.md). In particular, keep one
canonical C++ modem, one continuous physical channel through Helper, DATA and
quiet tails, configured-calendar reception, two scored quiet slots for message
completion, and the full `W = 2*H` receiver worker budget.

## Evidence status

Read the executed result and its scope together. A standard channel, deterministic
outage or explicit covariance law is a reproducible control; a measured episode
supports only its frequency, geometry, estimator and selection. Numerical
convergence does not establish that nature follows the model. Source reproduction,
empirical transfer and complete-receiver qualification have separate gates.

The proposed implementation preserves one canonical native C++ modem,
continuous Helper/DATA/quiet-tail state, configured-calendar scope, all public
hypotheses, physical completion and `W=2H`. Bounded discriminators and explicit
resource admission come before costly trials. The statistical design separates
failure finding, conditional probability confirmation and hardware capacity.

These documents are retained research material under the
[research-material rule](../../../docs/development.md#research-material). After
authorized adoption, distill unique findings, contrary evidence, citations and
provenance into the maintained guides and appraisal records. Distillation does
not make these documents removable; remove them only at the user's explicit
request. Significant calculation outputs follow the
[theoretical-calculation rule](../../../docs/development.md#theoretical-calculations).
Temporary downloads and other calculation outputs have their own recorded
review purpose and cleanup trigger.

On 25 November 2026, citations, access routes and values that the 13 November
rewrites had removed were restored. Chapters 80 and 87 again carry their
original source-check tables. Chapters 10–12, 20, 25, 30, 31, 33, 40, 50, 55,
81, 84, 86, 87 and 90 end with a "Superseded values from earlier versions"
section, which quotes each removed passage with the commit that removed it. The
current text of each chapter governs. Citations removed from maintained guides
before this research existed are recorded in
[retired-guidance citations](../../retired-guidance-citations/README.md).

# High fidelity weak signal research

Research in this directory is **substantial but incomplete**. It covers
sub-9 kHz with a 200 ft antenna, 1 MHz and 30 MHz groundwave, 1–30 MHz skywave,
and Earth–Moon–Earth. The prior pass produced a bounded research and
specification handoff; it did not close every obligation in the original
request. The earlier wording “completed research” overstated that result.

The [completion plan](80-completion-plan.md) separates unfinished literature,
public-data analysis, calculations and physical specifications from external
measurement dependencies and later implementation. The
[incomplete-work inventory and remediation audit](87-evidence-remediation-audit.md)
identifies the questions, evidence, next actions and closure criteria.

The old web-search-budget notices received substantive follow-up, including
new full-source retrievals, public-data inspection and corrected interpretations.
Those remedies remain useful. Some accessible evidence is still unanalyzed,
source claims remain unresolved or inconsistent, and synthetic controls do not
supply missing natural-environment distributions. A successful source download,
a closed mathematical control or a document check is not research completion.

No simulator, complete-receiver trial, physical-link qualification or hardware
capacity result is supplied by these documents. Those later stages retain their
own evidence gates. Better models and implementations remain possible.

The original sixteen chapters contained about 234,000 whitespace-delimited
words. This is historical inventory, not a verified-claim count. The current
26-document audit addresses the original requirements, present obligations and
historical omissions; its method and coverage boundaries are in the audit.

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
| Completion plan | [80](80-completion-plan.md) | Unfinished research, dependencies, execution packages and objective completion gates |
| Sub-9 kHz scenarios | [81](81-scenarios-sub9.md) | Top-loaded reference, transition and fade; explicitly required path/solver inputs |
| 1 and 30 MHz groundwave scenarios | [82](82-scenarios-groundwave.md) | Surface, coastal, terrain, competing-mode and outage branches |
| HF skywave scenarios | [83](83-scenarios-skywave.md) | Standard controls, measured-mode parameters, geographic strata and censored observations |
| EME scenarios | [84](84-scenarios-eme.md) | 5.76 GHz bistatic geometry, link budget and coupled lunar scattering |
| Noise and equipment scenarios | [85](85-scenarios-noise-and-equipment.md) | Reference planes, observable front-end effects and conditional noise cases |
| Statistics and scalar checks | [86](86-statistics-and-calculation-checks.md) | Fixed-sample pilot design and independently recalculated examples |
| Incomplete-work inventory | [87](87-evidence-remediation-audit.md) | Original/current obligations, source/data/calculation gaps, consequences and closure checks |
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

The all-family research-completion gate has **not** passed. The selected
mathematical controls are useful starting points, but source-backed physical
specification, claim consistency, public-data analysis and independent
calculation work remain. Some empirical calibration/replay branches depend on
external inputs; that does not make every uncompleted analysis external.

Follow the research work packages before treating the directory as ready for
an all-family appraisal. Later implementation preserves one canonical native
C++ modem, continuous Helper/DATA/quiet-tail state, configured-calendar scope,
all public hypotheses, physical completion and `W=2H`. Numerical work must use
bounded discriminators and explicit resource admission; long physical durations
are not permission for routine multi-day campaigns.

Keep these documents while they serve a concrete research/appraisal purpose.
After authorized adoption, preserve unique findings and provenance in the
maintained guides and appraisal records before removing superseded material.

# Completion plan for high fidelity weak signal research

Prepared and re-audited November 13, 2026. **Active completion plan; research remains incomplete.** The immediate objective is to complete the research
foundation and an implementable experimental specification for all five requested
environments. The eventual objective is a contained, independently validated
appraisal of the canonical complete native receiver. Those are separate
milestones: finishing the literature work does not establish that a simulator is
realistic or that the modem works in the modeled conditions.

**The recommended course is to preserve the substantial existing research,
repair consequential claims, consolidate its evidence into physically consistent
scenario specifications, and validate a small native implementation in stages.**
More literature and more physical detail should be added where they can change a
model choice, an applicability limit or an experimental conclusion. Better
implementations and better explanations may still be found.

This is the requested plan, with supporting gap analysis and targeted source
checks. It is not a completed systematic review of every claim in the roughly
234,000-word draft. No modem experiment, production edit, field measurement,
hardware rental or new calibration was performed for this plan.

The [search-limit remediation audit](87-evidence-remediation-audit.md) corrects
the previous impression that the research pass closed the original backlog.
Seven historical search-coverage notices were identified: six retain their
original notice wording and one was revised without closing its coverage gap.
Both explicit search-exhaustion statements remain. Some sources and calculations
were substantively repaired; other sources, measurements and model definitions
remain uninspected or unspecified. The current plan uses the actual remaining
work, not the presence of new scenario files, as its completion criterion.

**Current decision:** continue research and specification work inside this
directory. Sections 6–9 retain the technical requirements; sections 10 and 13–16
give the active deliverables, order, gates and recovery rules. Historical source
checks in section 5 are useful inputs, not a declaration of complete coverage.

## 1. Scope and authority

All edits for this planning task are confined to
`_ref/highFidelity-triple_Robust_Modem/Fable-5.2`. Reading other repository files
establishes compatibility and avoids duplicate work. Eventual experimental code,
build integration and operating/test documentation belong under
`_appraisal/highFidelity-triple_Robust_Modem`, but the present request does not
authorize starting that implementation or changing files there.

The five required families remain:

1. Sub-9 kHz with a 200 ft antenna.
2. Groundwave around 1 MHz.
3. Groundwave around 30 MHz.
4. Skywave over 1–30 MHz.
5. Earth–Moon–Earth.

The adjacent [Astra-6 index](../Astra-6/README.md) records an earlier clarification
of a 60.96 m top-loaded vertical and emphasis on passive EME at 5.8 GHz. Existing
Fable calculations also use those anchors. Retain them as the working reference
branches; record their origin. They do not specify an actual installation,
authorize a transmitter, or exclude useful alternative antennas and EME bands.
The 61 m T with a 122 m top wire in chapter 11 is a particular modeled example,
not the unique meaning of “200 ft antenna.”

The user requested serious research first and cautioned against overstating
implementation choices. Accordingly, proposed models, available code,
mathematically verified implementations, empirically validated channels and
receiver qualifications must have different statuses throughout this work.

The following are binding project requirements, not optional experimental
preferences:

- Link the canonical native C++ modem; do not copy it or create a Python DSP or
  persistent receiver implementation.
- Preserve all selected channel effects and continuous state, including Helper,
  DATA, observation gaps, source silence, delayed energy and filter tails.
- Preserve configured-calendar and keyed-ordinal scope. Unknown-start and broader
  address acquisition remain separate unqualified work.
- Determine physical completion from two scored quiet slots. Received lengths,
  known simulation endpoints, transport EOF, cancellation and quotas cannot
  supply successful completion.
- Preserve the full `W = 2*H` software receiver budget and exact explicit limits.
  Use the maintained ownership and memory admission rules.
- Use bounded hierarchical testing with declared strata, budgets, uncertainty
  decisions and stopping rules. Do not turn long standard test-duration examples
  into routine multi-day campaigns.
- Keep automatic precision and algorithm choices directed at challenging-case
  complete-receiver capacity. Qualify actual device and arithmetic execution.

See [development](../../../docs/development.md),
[current architecture](../../../docs/robust-current.md),
[channel behavior](../../../docs/channel-models.md),
[testing stages](../../../docs/building.md#testing-stages), and
[precision](../../../docs/gpu-precision-controls.md). Future implementation must
re-read the applicable current contracts rather than treating this plan as a
frozen replacement.

## 2. What exists and what completion still requires

The initial planning checkout was clean at
`07bcbc5da41ea5e8ec43517ffb92e51710037744`. Fable-5.2 contained sixteen Markdown
chapters totaling 7,367 lines. They already cover mechanisms, reported values,
moderate/adverse/extreme comparisons, model alternatives, source-access limits
and contradictions. Rewriting them wholesale would lose useful detail and make
the evidence harder to audit.

The original chapters cite production revision `0801e5c7`. Comparison with the
current checkout found no changes in `src/`, `include/`, `tests/`, `cmake/` or
`docs/` between those revisions. Thus a different HEAD does not by itself make
their implementation descriptions stale. Interpretations still require checking;
the historical planner receipts have not been regenerated or independently
qualified here.

At the present audit, that commit is still HEAD. The sixteen chapter changes
and nine planning/scenario additions are uncommitted. Preserve those edits and
distinguish their content from the baseline. This audit adds a traceable
remediation account; it does not retroactively create a research-completion
commit or verify every previous source receipt.

| Existing material | Keep | Remaining completion work |
| --- | --- | --- |
| Chapters 00 and 90 | Conventions, source context, formulas, comparison calculations | Claim audit; reproducibility of consequential calculations; corrected interpretations |
| Chapters 10–12 | Extensive antenna, waveguide and noise distinctions | Reproducible installation assumptions; matched data; joint long-duration episodes |
| Chapters 20 and 25 | Separate groundwave regimes and validity cautions | Fresh accessible primary sources; phase/delay checks; competing mechanisms |
| Chapters 30–33 | Availability, dispersion, standards, geography and nonstationarity | Joint-state evidence; source-estimator reconciliation; usable episode/data specifications |
| Chapters 40 and 41 | Lunar geometry, scattering, systems and uncertainties | Modern polarimetry transfer limits; actual whole-disc C-band evidence; station-specific inputs |
| Chapters 50 and 55 | Noise/interference and equipment inventories | Reopened sources; calibrated waveform and front-end specifications |
| Chapter 60 | Numerical, statistical and validation alternatives | A selected bounded inference procedure; dependence and stopping corrections |
| Adjacent Astra-6 synthesis | Concise scientific framing and native options | Crosswalk unique Fable findings into the existing design; preserve disagreements |
| Existing appraisal protocol | Scenario fields, staged checks, physical-completion outcomes | Add Fable-specific requirements when later editing that destination is authorized |

Two missing destinations were referenced throughout the original chapters:
`README.md` and `70-implementation-options.md`. The planning additions now provide
both. The [implementation chapter](70-implementation-options.md) maps real native
interfaces and viable alternatives; its proposed integration has not been built.

### 2.1 Original corrections identified before the research pass

The following table records the original audit, not the live state. The subsequent research pass changed the affected chapters and added the specifications linked below; see their source and calculation limits rather than assuming every historic claim has been exhaustively rechecked. The OU, severity and evidence-grade descriptions, 30 MHz table/prose mismatch, mirror-model scope, fixed-sample inference and dependent-KS qualifications have been corrected. The 5.17 kHz measured and modeled attenuation ranges overlap at their reported precision; the discrepancy below roughly 4 kHz remains unresolved. Source access can differ between sessions: Gu et al.'s abstract and dataset record were available in this pass, but publisher full text was not.

| Location | Problem or uncertainty | Required disposition |
| --- | --- | --- |
| 00, existing-channel omissions | Blanket absence of Lorentzian spectra conflicts with the native OU-gain option | Describe exponential covariance and sampled spectrum accurately; distinguish flexibility and calibration gaps |
| 00, evidence grades; preprints across chapters | `[P]` is defined as peer-reviewed while some references are explicitly preprints | Track document type, review status and access separately; preserve preprint findings at their actual status |
| 00, severity definitions | “Moderate on most days” and “adverse recurs” need a specified population or observed frequency | Use conditioned examples when occurrence statistics are absent; retain maxima and percentiles separately |
| 90, groundwave section 1.2 | “Land classes differ by only a few decibels” is too broad for its own 30 MHz table | The listed 100 km land values span 175.6 to 196.0 dB, or 20.4 dB; qualify the comparison and recheck the argument |
| 90, fixed-height skywave illustration | Inferences about larger delay spreads can overrun a simplified mirror-model domain | Limit conclusions to the displayed geometry; do not rule out all discrete multihop/retarded paths |
| 90, computational provenance | Original scripts were scratch-only; formulas remain but the original executable evidence is absent | Reproduce decision-relevant values independently; do not claim those original scripts were rerun |
| 60, sequential inference | Fixed-sample intervals and sequential methods are listed without a selected compatible stopping design | Choose fixed sample, error-spending looks, or a justified anytime-valid method before execution |
| 60, dependent distribution checks | Replacing sample count by an effective count is not a universal calibration of a dependent-sample KS test | Use episode replicates, justified block methods or null simulations preserving dependence |
| 25, Kirincich and Emery | Previously abstract-only source is now accessible in full | Extract actual frequency, antenna heights, mechanisms, estimator and residuals before importing its conclusions |
| 50, Cyprus HF interference | Previously unavailable source is now accessible in full | Update access status and distinguish congestion from noise power and link availability |
| 40 and 41, recent lunar work | Several relevant newer papers are abstract/preview-level leads | Read primary full text where available; retain local/orbital versus whole-disc EME boundaries |

### 2.2 Reconciliation with adjacent work

Use the [existing experiment protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
as the common experimental backbone. Compare its fields and gates with the Fable
chapters and record only missing requirements or genuine disagreements here.
Do not create a competing second operating protocol. Its current references to
Astra-6 and proposed pilot budgets are historical design choices, not proof that
Fable's extra evidence is incorporated or that those budgets suit every geometry.

When broader edits are later authorized, integrate accepted Fable findings into
the appraisal's scenarios and the appropriate maintained guides. Before removing
anything, verify the unique positive, negative and conditional findings and
their provenance have survived. Neither the original lifecycle note nor this
plan authorizes deleting another contributor's research now.

## 3. Define what can be completed

### 3.1 Research readiness

The research milestone is complete when all five families meet the following
criteria. The corpus has not yet demonstrated all of them:

- A mechanism and applicability map, including what is observed, standardized,
  calculated, hypothesized or deliberately adversarial.
- Traceable support for every consequential numerical parameter and conclusion
  used by a selected scenario, with source passage and measurement conditions.
- A reproducible representative moderate/control case, supported adverse case,
  severe or outage case, and a model-discriminating alternative. When evidence
  cannot support one, an explicit gap replaces a fabricated natural scenario.
- An executable specification of the input distributions, dependence, state,
  units, reference planes and output observables, without requiring code yet.
- At least one plausible implementation and a simpler comparator, with costs,
  limits, next falsifier and conditions for reconsidering alternatives.
- A concrete independent-validation route, or a clearly bounded reason empirical
  validation is unavailable and the weaker claim that remains possible.

For this gate, an **executable specification** means two independent implementers
can produce the same declared mathematical experiment from its source/input
record without choosing unrecorded distributions, scattering laws, calibration
constants, units, interpolation or initial state. Source-backed alternatives
may remain, but each alternative must itself be fully specified. A placeholder
such as “obtain a trace,” “choose an ionosphere,” or “freeze the kernel at run
time” leaves that branch at design status. A fully specified synthetic control
can become ready independently; it does not close the measured branch.

The entire draft should receive a consistency and support pass, with the deepest
source work concentrated on claims that drive scenarios or model selection.
Nonessential unresolved historical details may remain labeled leads. A selected
scenario cannot rely on them as established inputs.

Every historical chapter must be covered by the claim inventory and consistency
review; not every contextual anecdote needs the same depth of primary-source
checking as a parameter that determines physical feasibility or an extreme.

### 3.2 Later evidence gates

| Gate | Evidence needed | What remains unproven |
| --- | --- | --- |
| Research ready | Traceable synthesis and executable specifications | Correct code or physical accuracy |
| Generator verified | Mathematical/reference checks, state and unit checks | Agreement with a real environment |
| Reference reproduced | Same estimator reproduces a standard or published target | Independent empirical validation |
| Empirically validated within a domain | Independent held-out measurements, uncertainty and residuals | Unmeasured sites, bands or extremes |
| Receiver behavior characterized | Canonical full-receiver trials with declared scope and outcomes | Rare reliability or global availability without adequate inference |
| Capacity qualified | Current complete same-stream work and memory on named hardware | Other processors, arithmetic or altered configurations |

A model can be useful at an earlier gate. Report its actual gate rather than
blocking all progress until every physical unknown is solved, or granting it a
stronger status because it appears detailed.

Add **experiment ready** between research ready and generator verified: inputs
or explicitly chosen mathematical substitutes are obtainable, versions/licenses
are known, required laws and numerical tolerances are frozen, and the validation
and sampling contracts can be executed. Research can document an unavoidable
data gap honestly while that empirical branch remains not experiment ready.
Do not give the whole five-family suite this status because one control passes.

## 4. Literature completion method

### 4.1 Search coverage

Search by mechanism and measurement method as well as by the five family names.
Use foundational work without a lower year cutoff, then a distinct recent-work
pass for 2015 through the search date to cover the period named in the old
notices. Give 2020–2026 a separate update pass, including 2025–2026 material where
it exists. Record publication date separately from online date and search-engine
crawl date. Check errata, later versions and papers that challenge a commonly
cited result.

| Track | Query families and source venues |
| --- | --- |
| Sub-9 kHz | Electrically short/top-loaded antennas; loading and ground loss; waveguide modal attenuation/phase; D-region transitions; ELF/VLF noise APD and sferic clustering; calibrated communication reports |
| 1 MHz | Ground-wave field and phase; LFMF/GRWAVE; mixed conductivity and coastal recovery; seasonal soil properties; MF R-Mode; night skywave contamination |
| 30 MHz | Upper-HF surface wave; rough sea and parabolic-equation models; low-VHF terrain/clutter measurements; direct/reflected/diffracted paths; ducting and unwanted arrivals |
| Skywave | Watterson/ITS models; oblique sounding; NVIS and O/X modes; auroral/polar/equatorial scattering; Doppler-delay coupling; disturbed-state availability; interference occupancy |
| EME | Lunar radar cross section; Hagfors and alternative scattering laws; delay–Doppler spectra; bistatic geometry; Mini-RF polarimetry; measured EME echoes; station noise and clocks |
| Shared methods | Nonstationary channel validation; dependent noise; fractional delay and time scaling; oscillator spectra; sequential inference; rare events; model discrepancy |

Start with primary journals and proceedings, author/institutional repositories,
NTIA/NASA/JPL and other laboratory reports, and official standards. Use citation
indexes to discover work, not as substitutes for source passages. Follow both
backward references and later citing work for each decision-driving source.
Seek relevant non-English primary literature when a geographic or historical
gap warrants it; identify translation limits and verify numerical tables and
definitions in the original. Technical reports and practitioner records are
valuable but keep their evidence roles distinct from peer review.

For each search batch retain, in the relevant chapter's research notes, date,
database or repository, actual query, time/language coverage, inclusion rule,
useful discoveries and exclusions. Do not reconstruct fictitious logs for the
earlier chapters. Mark their older search provenance as incomplete where needed.

### 4.2 Claim and parameter extraction

Use compact evidence tables in the relevant chapters, with a shared format:

| Field | Required content |
| --- | --- |
| Claim | Exact proposition or parameter, separated from interpretation |
| Identity | Authors, title, year, DOI/report identifier, version or standard edition |
| Locator and access | Page/section/figure/table; full supporting passage read, abstract, secondary or unverified |
| Evidence type | Field/communications measurement, radar/sounding, laboratory measurement, numerical model, theory, standard, practical report or in-house calculation |
| Conditions | Carrier/bandwidth, location/geometry, date/season/state, antenna/sensor, estimator, threshold and integration time |
| Sampling | Number and independence of episodes, exclusions, non-detections, saturation and observation coverage |
| Result | Units, reference plane, width/statistic convention, uncertainty and reported range |
| Applicability | Where transfer is justified; where it is extrapolation or contradicted |
| Use | Scenario/model decision affected; independent check or missing evidence |

Do not quote more of a source than needed. Distinguish a directly read result
from a later paper repeating it. Do not use absence from a bounded search as
proof that a measurement or model does not exist.

Build a contradiction table beside the affected synthesis: competing claims,
matched and unmatched conditions, plausible causes of disagreement, the next
discriminator and current disposition. Differences in bandwidth, estimator,
source detection threshold, polarization or geometry may explain disagreement;
if they do not, retain multiple models and propagate that uncertainty.

### 4.3 Search stopping and access limits

Stop a branch when the next model decision is adequately supported, targeted
backward/forward searches add no decision-relevant evidence, and remaining gaps
have explicit consequences. This is a practical saturation rule, not a claim
of exhaustive literature coverage. Reopen when a new dataset, contradictory
result or failed validation could change that decision.

For a consequential inaccessible source, check the author manuscript,
institutional repository, report predecessor and lawful public dataset. If still
unavailable, use only the accessible claim at its true status, keep a weaker
conclusion or select another model. Do not invent detailed support from an
abstract. Requests to authors, paid access and field campaigns are future actions
requiring their own authorization; no messages or purchases are part of this plan.

### 4.4 Concrete searches and evidence coverage

The following are **planned queries**, not searches claimed as already run.
Adapt vocabulary to the actual index and record the executed query. Start with
the sources in [audit groups G01–G17](87-evidence-remediation-audit.md), then use
independent mechanism searches to avoid following only the original authors'
citation choices. For each track inspect foundational work, the 2015–2019 gap,
2020–2024 updates, and 2025 through the actual search date separately.

| Track | Initial exact query examples | Primary routes and required extraction |
| --- | --- | --- |
| Sub-9 kHz propagation | `"ELF VLF" "attenuation" "3 kHz"`; `"Earth ionosphere waveguide" "phase" "5 kHz"`; exact titles in chapter 10's missing-source list | Radio Science/JGR and author repositories; field component, path, ionosphere, estimator, loss/phase uncertainty, lower-frequency discrepancy |
| 200 ft antenna | `"VLF" "top loaded" "measured efficiency"`; `"sub 9 kHz" "antenna current"`; `"VLF antenna" "detuning" rain corona | Original antenna reports, theses and calibrated communication records; geometry, current, coil/ground loss, capacitance, weather and voltage limits |
| Sub-9 kHz noise | `"ELF VLF noise" "amplitude probability"`; `"sferics" "clustering" noise`; `"VLF" "noise" "2025"` and `"2026"` | NTIA, AGU, Copernicus and dataset records; waveform bandwidth, calibration, APD/PSD, durations, clipping, event dependence and observation denominator |
| 1 MHz coverage gaps | `"630 m" "field strength" groundwave`; `"160 m" WSPR groundwave`; `NAVTEX measured reception 490 518`; `DRM medium wave field trial skywave` | Original field studies, ITU M.823 and IALA, plus Valčić 2026; distinguish operator records, RSSI, decoded reports and calibrated field/phase |
| MF time dependence | `"medium frequency" "ground wave" "phase stability"`; `"R-Mode" "phase" conductivity`; `"1 MHz" "groundwave" seasonal` | Measured phase series and ground surveys; cadence/window, receiver clock contribution, temporal and route uncertainty, no silent LF-to-MF transfer |
| 30 MHz mechanisms | `"Ground Wave Propagation Measurement" "Rogers"`; `"25-MHz" "Conductivity" "Tides"`; `"30 MHz" "surface wave" measurement`; `"HF VHF" "NLOS" terrain measurement` | Exact proceedings/AMS source, NTIA field reports and original measurements; actual frequency, heights/horizon, path type, two-way versus one-way observable |
| HF missing source sets | Exact titles/identifiers for `CCIR Report 549`, `CCIR Report 266`, `WHISPER Davies Willink Angling Cannon 2004`, `Clune Fine Freedman Nissen Perry 1991`, and each named DRM/Canadian/Indian/Chinese study | Official report archives, institutional theses and primary journals; source-by-source methods, resolution, detection rule and parameter extraction |
| HF joint states | `"HF channel" "cross frequency" correlation`; `"ionospheric channel" "nonstationary" measured`; `"polar cap" "off great circle"`; `"equatorial" "delay Doppler" sounding`; `"May 2024" "oblique sounding"` | Primary sounding/radar/communications records; simultaneous versus scheduled bands, coherent phase, censoring, joint delay–Doppler/availability and nonstationarity |
| EME scattering | `"lunar radar" "5.76 GHz" spectrum`; `"Moon" "3.8 cm" "scattering law"`; `"Earth Moon Earth" "SigMF"`; `"lunar radar" bistatic polarization 2025 2026` | Original Lincoln Laboratory/ZPC/Hagfors equations, calibrated echo records and dataset hosts; area versus solid-angle laws, polarization, whole-disc/patch and bistatic transfer |
| EME thermal and propagation | Exact `10.1155/2019/3940837`; `"lunar brightness" "5 GHz"`; `"moonbounce" scintillation`; original Fang/Pontes and Mollen/Liu/Fang titles | Radiometry and scintillation primary reports; beam-integrated temperature, calibration bias, source extent and the two-leg dependence problem |
| MF/HF noise and equipment | `"Fockens" "radio noise" thesis`; `"Lemmon" "HF" noise model`; `"Giesbrecht" "HF" noise`; `"TR-25-579"`; `HF receiver blocking compression phase noise` | NTIA/ITU and measured hardware papers; antenna-port PSD/APD, filter ENBW, detector, occupancy threshold, compression/exclusion and target hardware applicability |
| Numerical credibility | Exact missing source names in 60; `HF simulator on air validation`; `nonstationary channel simulator covariance validation`; `fractional delay time varying error`; `dependent episodes confidence sequence` | Original method papers/standards and maintained native interfaces; assumptions, finite implementation, long-time artifacts, inference theorem applicability and cheaper comparators |

For each track, record which primary publisher/report repository and which
independent discovery index were actually searched. A blocked index is missing
coverage, not a negative search result. Include negative/failure studies and
counterexamples, backward references and later citing work. Non-English
material is particularly relevant when geography or the named original source
demands it; record translation and numerical-reading limitations.

Use a coverage matrix with rows for these tracks and columns for time period,
measurement/theory/communications/radar evidence, quiet-to-outage behavior,
geography, source route, actual query/date, inspected candidates, exclusions,
contradictions and current saturation decision. The closure criterion is that
all required cells have an explicit supported result or bounded unresolved
outcome and no critical decision depends on an uninspected lead. It is not a
quota of queries or papers. Do not infer complete 2015–2026 coverage from a
single query containing those years.

### 4.5 Claim inventory and priority

Inventory the entire sixteen-chapter corpus and the new scenario/design notes.
Preserve chapter-local source identifiers; use small claim identifiers only
where they make dependencies auditable. Avoid a second bibliography or copies
of source text. Split a paragraph when its measurement, extrapolation and
implementation suggestion have different support.

- **Critical:** all selected scenario parameters, feasibility/range and noise
  claims, observed extremes, exclusions of plausible mechanisms, equations and
  claims that select one model over another. Inspect supporting primary text
  and calculation; trace every dependent use. None may remain silently inferred
  from a search snippet or unsupported recollection.
- **Material:** background mechanisms or alternatives that could change the
  interpretation. Check the source and transfer conditions; promote to critical
  if the scenario uses them. An accessible authoritative synthesis may identify
  a primary lead, but label the dependence honestly.
- **Contextual:** historical detail or unused leads. Verify identity and remove
  unsupported precision, or retain explicitly as a nonessential unresolved
  note. Do not spend effort on an anecdote ahead of missing physical inputs.

For every critical row include its downstream scenario/calculation, evidence
status, contradictory source, remaining action, and closure check. Keep
measurement uncertainty, parameter uncertainty, model discrepancy and random
realization variability separate. Do not report a percentage “verified” until
the denominator and claim-splitting rules are defined; paragraph or citation
counts are not that denominator.

## 5. Targeted new evidence already found

These source checks make the next pass concrete. They are additions or access
updates, not evidence that all recent literature has been covered. Source
availability is recorded as observed on November 13, 2026.

| Source and access | Contribution to completion | Transfer limit |
| --- | --- | --- |
| [ITU-R P.372-18](https://www.itu.int/rec/R-REC-P.372-18-202609-I), official edition/status page | Confirm current noise-standard identity; preserve distinction between updated standard and new measurements | Does not supply a site-specific sub-9 kHz extreme distribution |
| [Gu et al. 2021](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2020JA029101), abstract and [dataset record](https://doi.org/10.6084/m9.figshare.13536611) accessible in the research pass; full-text access differed by session | Regional VLF noise and lightning dependence; candidate data-access investigation | One region and recorded measurement bandwidths; not a target-antenna or universal storm calibration |
| [Reuveni et al. 2011](https://www.ursi.org/proceedings/procGA11/ursi/EP1-8.pdf), full proceedings paper | Temporal/seasonal noise statistics from a Negev record, including 4 and 7.5 kHz | Old single-site record; recent indexing does not make it new measurement evidence |
| [Kirincich and Emery 2023](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2022RS007550), full text | Ocean-path measurement/model comparison, geometry dependence and value of measured roughness inputs | Extract exact operating frequency; neither generic 30 MHz validation nor proof that a complex model always wins |
| [Hehenkamp et al. 2024](https://www.mdpi.com/1424-8220/24/1/282), full text | MF mixed-path phase/delay model and field comparison; uncertainty in ground maps | MF R-Mode evidence is not direct 1 MHz amplitude validation |
| [Hehenkamp et al. 2025](https://ars.copernicus.org/articles/22/77/2025/), full text | Layered sea ice over seawater as an alternative to a homogeneous ice half-space | 319 kHz modeled case; not a measured 1 MHz propagation result |
| [Dagefu et al. 2015](https://deepblue.lib.umich.edu/bitstream/handle/2027.42/111943/rds20240.pdf?sequence=1), full text | Separate 40 MHz short-range clutter/penetration study, DOI 10.1002/2014RS005601 | Do not substitute it for the different cited IEEE TAP paper or extrapolate to long-range surface wave |
| [Haralambous et al. 2022](https://www.mdpi.com/2076-3417/12/22/11808), full text | Cyprus HF spectral congestion, threshold dependence and time structure | Site/user/quiet-geomagnetic scope; occupancy is not ambient-noise PSD or modem availability |
| [Male et al. 2021](https://www.mdpi.com/1424-8220/21/6/2210), full text | Measured O/X dependence and availability on a 5.4 MHz NVIS path | Short path/season sample; does not resolve general cross-frequency Helper/DATA covariance |
| [Ponomarchuk et al., May 2024 storm study](https://naukaru.ru/en/storage/download/242871), full paper | Oblique-path mode/availability evidence during a severe event | Sounder observations do not supply this modem's BER or a complete delay–Doppler distribution |
| [Themens et al. 2024](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2024GL111677), full text | Disturbance history and high-latitude ionospheric context | Same geomagnetic index need not imply the same channel; still not a receiver trial |
| [Gao et al. 2023](https://doi.org/10.1016/j.isprsjprs.2023.01.008), abstract/indexed material; full fetch denied | Candidate update for local lunar dielectric/polarimetric interpretation | Local orbital observations cannot be used as a whole-disc 5.8 GHz EME kernel |
| [Campbell 2025](https://doi.org/10.1016/j.icarus.2025.116738), publisher preview/abstract | Challenges unique physical interpretation of polarimetric components | Other wavelengths and local scattering; full-text review remains needed |
| [JPL IPN 42-216C](https://ipnpr.jpl.nasa.gov/progress_report/42-216/42-216C.pdf), full report | Explicit example of geometry-dependent lunar channel modeling assumptions | Lunar-asset downlink with surface multipath is not passive Earth–Moon–Earth reflection |
| [Howard et al. 2021](https://arxiv.org/abs/1810.08240), abstract and publication identity checked | Candidate finite-sample time-uniform inference framework | Select and verify a theorem under the actual episode assumptions before use |
| [Waudby-Smith et al. 2024](https://arxiv.org/abs/2103.06476), abstract checked | More recent asymptotic confidence-sequence alternative | Asymptotic validity cannot be presented as a finite-sample rare-event guarantee |

The planning searches covered sub-9 kHz noise and antenna evidence, recent MF
mixed-path and upper-HF/low-VHF measurements, storm sounding and HF congestion,
NVIS dependence, Mini-RF/EME measurements and sequential inference. They used
focused mechanism and exact-title searches followed by primary-source opens.
They did not search every database, language, thesis or nonpublic dataset. The
next research pass must record its queries under section 4 rather than promote
this bounded check into a comprehensive search claim.

## 6. Physical scenarios and severity

### 6.1 Common scenario specification

Use the fields in the existing appraisal protocol. Add explicit distinctions
where a chapter presently leaves them implicit:

- Define the scientific question, relevant receiver configuration and time scales
  before selecting model complexity.
- Fix or parameterize endpoint positions, RF band, occupied DATA/Helper spectrum,
  antenna patterns/polarization, power, environment and observation duration.
- Carry units, power reference planes, phase/delay signs and width definitions
  with every parameter. A scalar “path loss” cannot hide antenna loss and noise.
- Separate a physical route being available from conditional reception on that
  route. Include non-detections and measurement censoring.
- Describe correlations and shared causes, including cross-frequency,
  cross-polarization, level/spread, signal/noise and equipment/environment effects.
- State the evolution law, initialization, state memory, stationarity interval,
  event onset/recovery, delayed tails and finite simulation bounds.
- Name evidence, calibration data, independent holdout, estimator and uncertainty.
- Separate public receiver knowledge from evaluator-only truth.

Keep four severity classes visible: observed severe episode; standardized test
condition; physical extrapolation with uncertainty; deliberately adversarial
stress. “Worst” must be conditional on the defined population, support or event.
An arbitrarily absent signal is an outage, not a finite universal worst channel.
Do not assign an occurrence probability to a maximum found in one record.

Use causal coupling before statistical convenience. For example, an ionospheric
disturbance can change mode availability, received level and spectral structure
together; dish pointing changes both illuminated lunar scattering and received
power. If only marginal statistics exist, retain alternative dependence
assumptions and bound their consequences. Sampling independent marginal maxima
does not reconstruct a measured joint extreme.

### 6.2 Sub-9 kHz work package

**Research question:** which combinations of antenna, matching, path and noise
support usable observations over the modem's very long apertures?

Complete the geometry record for the top-loaded vertical: height versus wire
length, conductor radius/material, top wires, downlead/feed, supports and nearby
objects, grounding/return geometry and soil layering. Specify transmitter power,
current/voltage constraints, tuning control and weather state. Check capacitance,
effective height, radiation and loss resistance, matching loss, efficiency,
usable modulation bandwidth and detuning together. A favorable propagation
calculation cannot compensate for an inconsistent radiator or matching model.

Keep electric antenna, magnetic loop, near-field coupling, conductive
earth-electrode and radiative waveguide branches distinct. Use measured-current
or field evidence where available, including failure reports. The modeled 61 m
T loss cases in chapter 11 remain assumptions until supported for an installation.

For propagation, map distance and frequency domains, waveguide modes,
conductivity and D-region assumptions, nonreciprocity, phase/group delay,
terminator motion and disturbance episodes. The 5.17 kHz measurement and model intervals overlap at the stated precision. Resolve or bound the remaining discrepancy below roughly 4 kHz before using the model for precise range forecasts. Higher-frequency VLF data do not automatically establish
sub-9 kHz attenuation or phase stability.

For noise, obtain calibrated records or defensible models of PSD, APD, sferic
waveform/duration/arrival clustering, mains structure and nonstationarity. Track
sensor transfer, mean versus rms estimators, bandwidth and clipping. Model
blanking as a receiver operation with lost wanted energy and signal distortion;
an uncontrolled successful decode is not a measured universal blanking gain.

**Scenario branches:** a declared quiet-path reference; a day/night transition;
a matched atmospheric-noise episode; an antenna-detuning/recovery event; and an
explicit below-detectability/outage case. Do not combine unrelated maximum noise,
loss and phase excursions and label them observed.

**Acceptance:** reproducible antenna assumptions and sensitivity bounds; measured
anchors with residuals; a continuous joint signal/noise/equipment episode;
separate prior uncertainty and realization variability. If matched antenna or
field data are absent, leave the result forecast-only and specify the smallest
measurement that would change the model choice.

### 6.3 Groundwave at 1 MHz work package

**Research question:** what determines the wanted surface-wave field, phase and
interference environment along a specified route?

Verify the smooth homogeneous-earth baseline and its power/loss conventions
against current official P.368 references or a pinned independent implementation.
Then introduce mixed land/sea paths, conductivity/permittivity uncertainty,
seasonal/layered ground and antenna heights only where supported. Do not fit
ground constants to the validation data and call the fitted agreement independent.
Distinguish field-amplitude evidence from R-Mode phase or delay evidence.

Separate deterministic route loss from local temporal variation, terrain/clutter,
urban shielding and external noise. At night, compare surface-only with
surface-plus-skywave under declared illumination and ionospheric conditions.
Keep the wanted source's coherent arrivals separate from independent co-channel
transmitters. Use layered sea ice when that physical branch is selected; a
homogeneous fresh-water-ice half-space answers a different question.

**Scenario branches:** homogeneous ground reference; mixed coastal path; seasonal
or layered-ground perturbation; nighttime competing skywave; and weak wanted
field under calibrated local noise.

**Acceptance:** independent loss/phase reference checks, correct reference
radiator conversion, explicit environmental uncertainties, a competing-mode
control and a held-out field/phase comparison within the measurement's domain.
Retain disagreement when ground-map uncertainty dominates added model detail.

### 6.4 Groundwave at 30 MHz work package

**Research question:** under what geometry is the received wanted signal actually
surface-wave dominated, and which other mechanisms govern severe reception?

Treat smooth-earth 30 MHz calculations as reference physics. For each measured
case classify surface wave, direct/reflected space wave, terrain diffraction,
penetration/clutter, sea roughness, ducting or skywave contributions. Record
antenna heights and horizon visibility before attributing variation to rough sea.
Do not rename any lower-VHF short-range result “30 MHz groundwave.”

Extract the full Kirincich–Emery comparison with environmental inputs and model
residuals. Test the hypothesis that input uncertainty, rather than inadequate
solver complexity, limits prediction. Preserve the distinct Dagefu publications
and their actual frequency and short-range clutter geometries.

Examine the chapter's derived unwanted-arrival budgets near very large wanted
losses: aircraft/ionospheric backscatter and other propagation routes need
matched measurement or an explicit uncertainty bound. The fact that a smooth
surface-wave solution exists does not show it dominates what a receiver hears.

**Scenario branches:** sea/coastal reference with specified antenna height;
rough-sea episode; terrain/clutter path; competing space-wave or skywave case;
and no viable wanted surface path. Keep “30 MHz nominal groundwave” available as
a measurement label when the mechanism remains unresolved.

**Acceptance:** a mechanism-assignment table for every selected source; separate
measured and calculated quantities; no frequency transfer without its physical
argument; model residuals and a plausible simpler comparator; uncertainty in the
wanted-to-competing-arrival ratio.

### 6.5 Skywave from 1 to 30 MHz work package

**Research question:** how do conditional channel dispersion and changing route
availability jointly affect Helper acquisition, DATA tracking and completion?

Retain distinct strata for MF/low-HF nighttime propagation, midlatitude oblique
paths, NVIS, long/multihop paths, auroral/polar paths and equatorial spread-F
conditions. Frequency, geomagnetic latitude, local time, season, solar state,
disturbance history and geometry condition each stratum. Cover quiet behavior
as well as disturbed episodes that impair or enable propagation.

Use Watterson/F.1487 as a reproducible local stationary comparator, preserving
its width convention, not as a universal channel. Evaluate non-Gaussian or
multi-component spectra, unequal/specular-diffuse modes, delay–Doppler coupling,
drifting delays and mode birth/death where measurements require them. Do not
turn a radar spectral width or ionosonde maximum observable frequency into a
communications fading distribution without a transfer argument.

Reconcile source estimators: Doppler centroid versus width, 2-sigma versus
half-power or percentile width, mode delay versus total/rms spread, detection
threshold, record length and sounder resolution. Preserve outages and saturated
or undetected observations rather than conditioning them away silently.

Add O/X availability and dependence as a measured mechanism, while retaining
general Helper/DATA cross-frequency covariance as unresolved. Render all three
DATA carriers and Helper through one physical channel at their actual
frequencies. Include compatible atmospheric noise, congestion, receiver
blocking and clock error; frequency occupancy statistics are not additive-noise
PSD or an automatic link-outage probability.

**Scenario branches:** stationary quiet control; one measured NVIS/oblique
episode; a high-latitude disturbed episode; an equatorial branch; a nonstationary
transition/blackout and recovery; and a low-HF/MF case with its specific evidence
limits. These are coverage requirements for the research, not a command to run
every parameter combination.

**Acceptance:** per-stratum evidence and missing-work status; at least one
source-matched joint observable per selected model; explicit nonstationarity and
availability; compatible noise/interference assumptions; a model disagreement
test that could distinguish standard taps from a richer representation.

### 6.6 Earth–Moon–Earth work package

**Research question:** which beam-weighted lunar scattering and station effects
matter for passive EME near 5.8 GHz, and how far can evidence from other bands or
radar geometries be transferred?

Freeze two station positions, observation epoch/time system, RF carrier,
antenna patterns/efficiency/polarization, pointing policy, power and system-noise
model. Compute two-leg light time, range and bulk Doppler consistently, with
declared ephemeris and compensation error. Separate mean travel time from
excess delay across the scattering surface and from equipment latency.

Model the illuminated and received lunar surface with a justified scattering
law, including specular/diffuse contributions, surface heterogeneity and
polarization where they matter. Delay and Doppler must come from the same
geometry; arbitrary independent draws destroy that coupling. Whole-disc and
narrow-beam patch cases require different weighting and may have different
received power as well as spread. A narrow patch is not a free spread reduction.

Build a radar-transfer table: Earth-based monostatic whole-disc observations;
two-ground-station bistatic EME; local orbital SAR; orbital bistatic radar; and
lunar-surface-to-Earth downlinks. Record wavelength, incidence/bistatic angle,
footprint, polarization, resolution and measured quantity. Use modern Mini-RF
results for the quantities they actually constrain. Keep nonunique polarimetric
interpretations and interpolation between wavelengths visible.

For systems, include antenna-weighted lunar/sky/ground noise, receiver/feed loss,
polarization mismatch, pointing, oscillator drift, filtering and selected weather
effects with stated elevation/frequency domains. Avoid counting loss or noise
already embedded in measured system temperature twice.

**Scenario branches:** a declared whole-disc geometry; a lower- and higher-spread
geometry; a beam-weighted patch comparison; pointing/clock perturbations; and
insufficient C/N0 or visibility loss. Other bands provide transfer controls, not
automatic qualification of 5.8 GHz.

**Acceptance:** independently checked ephemeris/signs and integrated power;
joint delay–Doppler and polarization observables with named definitions;
convergence evidence; source-transfer limits; and a data plan for raw calibrated
echo/IQ or other independent whole-disc measurements. Until such evidence is
available, derived C-band spectra and link margins remain predictions.

## 7. Shared equipment and numerical work

Complete the chain from physical source to digital receiver before interpreting
a weak-signal threshold. Chapter 55 should provide selectable, evidence-backed
equipment models rather than a single universal radio: antenna/matching
response; transmitter nonlinearity; filters and group delay; independent or
shared frequency/sample references; AGC and blanker state; clipping, quantization
and recovery; timestamp error and observation gaps.

For each model record what was measured, which settings were used and whether
the calibration includes noise, AGC or saturation. Separate actual radio
measurements from vendor specifications and synthetic stress settings. Test the
ordering of blockers, filters and nonlinear stages: a blocker removed after
clipping can already have destroyed the wanted signal.

Reproduce chapter 90's consequential calculations in priority order: level/noise
reference-plane conversions; antenna efficiency and bandwidth; groundwave loss
and phase; lunar link power and geometric widths; coherence-width conversions;
and long-time numerical stability. Scalar analysis scripts can support a
documented calculation, but must not become another DSP implementation or own
receiver state. Select native reference implementations for computational
channel algorithms and the eventual appraisal.

Use limiting cases, dimensions and independent methods, not merely two
translations of the same formula. Bind results to input and code identity and
state rounding. Original scratch code is unavailable; preserve original numbers
and any later correction explicitly. Agreement among formulas establishes
arithmetic consistency, not empirical physical validation.

## 8. Native implementation stages after research

The [implementation options](70-implementation-options.md) identify actual
interfaces, current limitations and the build-containment hypothesis. The
following sequence is proposed for later authorized implementation:

1. **Prove contained linkage.** Configure the canonical root with an
   appraisal-owned integration hook, then link a minimal native driver to the
   existing targets. Check source identity and absence of copied DSP. If the
   hook fails, assess a small canonical registration change before inventing a
   separate build of the modem.
2. **Prove unit and state boundaries.** Use canonical emission, identity and
   constant gain/delay controls, I/Q plus validity ingress, backpressure and
   physical quiet completion. Keep truth private to the evaluator.
3. **Add one ready channel.** A homogeneous 1 MHz reference can establish level
   conventions; a stationary HF reference can establish stochastic state and
   receiver integration. Choose by available validation inputs and bounded cost,
   rather than asserting that either qualifies the other families.
4. **Add discriminating structure.** Introduce one measured episode or one
   physical mechanism with a simpler comparator and an observable falsifier.
   Validate the renderer before interpreting modem outcomes.
5. **Expand by evidence readiness.** Add the remaining family branches with
   their own acceptance gates. Share numerical primitives where the physics
   permits; do not force every environment into independent two-ray fading.
6. **Measure full receiver behavior.** Use the existing complete native stream
   receiver and codec, including unsuccessful acquisition and quiet completion.
   Separate direct I/Q from real-audio/frontend qualification.
7. **Assess cost only when relevant.** Report renderer cost, preparation,
   receiver work and total turnaround separately. Capacity claims require the
   project's current complete-work procedure, not a simulator timing proxy.

Current native full-stream sample clocks are integer-rate. Very low-rate latent
processes or mathematically valid state advancement can reduce rendering cost;
they do not authorize changing physical bit durations or silently assuming a
sub-Hz complete-stream interface. Any required canonical extension is a separate
reviewed change outside this planning scope.

The first implementation should avoid a generic plugin framework, a learned
surrogate, a complete global ionospheric solver, every possible front end, or a
universal scenario language unless a concrete accepted case requires it. A
small number of explicit native components and strict scenario validation is a
simpler starting point. Keep alternatives as findings with reconsideration
conditions; remove abandoned implementations after preserving their conclusions.

## 9. Validation and bounded statistical design

### 9.1 Validation ladder

Use the existing appraisal stages, adding a written acceptance sheet per model:

1. Analytic/reference controls: gain, units, phase sign, delay, Doppler,
   conservation/normalization, noise power and finite support.
2. Stateful numerical checks: stationary initialization, chunk/worker invariance,
   arbitrary read boundaries, filter/delay history, tails, long-time phase and
   clock evolution, explicit unavailable/error results and bounded memory.
3. Source-observable reproduction: same spectrum estimator, bandwidth,
   integration, detection threshold, sampling interval and censoring treatment.
4. Independent physical validation: held-out episode, path or geometry chosen
   before fitting, with uncertainty and model discrepancy reported.
5. Complete receiver outcomes: acquisition, tracking, decisions, erasures,
   completion, integrity and reacquisition under public configuration.
6. Separate current-hardware capacity qualification when in scope.

Matching one scalar spread is not sufficient if models with different joint
structure produce different receiver behavior. Conversely, do not require every
observable for a deliberately limited reference model and then mislabel it as a
complete environment. Tolerances must follow measurement uncertainty, numerical
error and decision sensitivity, not an arbitrary universal dB or FP64 threshold.

### 9.2 Small pilots and probability claims

Before any costly run, state the population, primary endpoint, model version,
strata, independent experimental unit, finite trial and compute budget, inference
method, stopping rule and expansion condition. Use cheap checks first. A proposed
screening default is one deterministic control and a small number of independent
episodes for one ready model, followed by selected paired comparisons; it is
failure finding, not a reliability estimate. Choose the actual number after
cost and correlation are known, using the existing protocol as a starting point.

For an independent fixed-sample binary endpoint, exact or justified approximate
intervals are straightforward. If looking repeatedly, predeclare error-spending
looks or use an appropriate time-uniform method. An anytime-valid method still
needs its probability and dependence assumptions; it does not make correlated
slots independent. Recent asymptotic confidence-sequence methods must not be
used to promise finite-sample rare-event coverage.

As a scale check, zero failures in `n` independent fixed-sample trials gives the
one-sided exact upper failure bound `1 - alpha^(1/n)`. At 95% confidence, about
299 zero-failure trials are needed merely to put this bound below 1% [derivation
from `(1-p)^n = alpha`]. A handful of successes cannot establish rare reliability.
This is an illustration, not a prescribed campaign size.

Use independently generated or genuinely separate episodes as the unit of
replication. Slots within a fade, overlapping windows, repeated recorded noise,
reused soundings and timing replicas have dependence. Preserve event history
and select justified blocks or hierarchical analysis where appropriate. A
source's effective-sample-size estimate is not automatically valid for every
test statistic or nonstationary process.

Common random numbers can improve paired model/receiver comparisons, provided
the pairing preserves the intended physics. Hold out fresh episodes for any
choice tuned on pilots. Predeclare the main endpoint and account for multiple
looks/comparisons; do not present a selectively chosen winning configuration as
unbiased evidence. Report model-to-model uncertainty separately from sampling
uncertainty and unknown environmental population weights.

Rare-event importance sampling requires support coverage, likelihood weights
and diagnostics. Without those it is stress testing, not a tail-probability
estimate. Do not scale standard minimum fading-cycle recommendations directly
into multi-day routine tests. Preserve the standard's interpretation, then use
bounded component checks or report an unresolved long-time qualification gate.

### 9.3 Receiver outcomes that must not disappear

Report correct full messages, wrong bits, false DATA, erasures, integrity
rejections, accepted truncation, premature completion, missed/delayed completion,
reacquisition/suffix behavior and resource failures separately. State the
denominator and scoring horizon. Reaching the horizon is censored or incomplete
coverage, not successful completion.

The most important targeted edge case moves the same deep fade across Helper,
initial DATA, interior DATA, final DATA and quiet-scoring intervals. Include a
fade long enough to hide ongoing transmission over two quiet decisions, followed
by return of the signal. Measure the ambiguity and receiver/codec outcome; do
not secretly replace the physical-completion rule with transmitter length.

Include valid noise-only inputs, incorrect-address/key controls within the
configured receiver scope, and observation gaps marked invalid. Propagation
outage and capture loss have different semantics. Unknown epoch/ordinal
acquisition remains outside current qualification even if these controls pass.

### 9.4 Computational evidence

Future implementation changes require the smallest relevant deterministic
checks and applicable named groups. `./build.sh test triad` precedes refreshing
manual calibration; shared GUI/audio changes need their affected groups. The
documentation-only plan does not require running receiver benchmarks.

For a capacity claim, follow the current
[manual workflow](../../../docs/robust-manual-capacity.md) and
[performance guidance](../../../docs/triad-performance.md): complete nominal and
conservative same-stream work, public hypotheses, unsuccessful Helper, fine
tracking, physical completion, reacquisition, preparation, orchestration and
memory. Declare backlog intervals separately from fill/drain. Keep all `W=2*H`
workers available; stage width may be smaller only when useful work is limited.
Independent simultaneous simulations are not productive parallelism within one
received stream.

Preserve representative target coverage: RTX 4090 desktop and Laptop, RTX 4060
Ti 16 GB, Ryzen 5 PRO 5650U and Ryzen 7 8840U CPU/integrated graphics. Keep wider
GUI compatibility targets distinct. Actual-device measurements, compatible
builds and forced paths have different evidentiary status; Intel host and
unavailable AMD paths remain explicit. For the 5650U, evaluate the documented
native Vulkan route rather than inferring GPU unavailability from ROCm alone.

If accelerated development becomes appropriate and spending is authorized,
prefer a verified full RTX PRO 6000 Blackwell or record a concrete better-fit
reason. Record edition, allocation, memory, runtime, executed arithmetic and
fallbacks. Hardware peaks or GPU timing cannot qualify local CPU capacity.

## 10. Deliverables and dependencies

Maintain substantive research in the existing chapters. Use their evidence and
contradiction tables rather than creating a session registry or duplicate
bibliography. The following work packages identify responsibilities and
acceptance, not assignments to particular agents or an automated schedule.

Effort classes are provisional: **S** is a bounded local check or editorial
pass; **M** requires several sources and a coherent synthesis; **L** has
substantial scientific/data dependence. They are not calendar promises. Measure
pace on an initial sample of difficult claims before estimating total effort.

| Package | Responsibility and output | Dependencies | Acceptance and stop condition | Effort |
| --- | --- | --- | --- | --- |
| A. Baseline and crosswalk | Research editor: reconcile all sixteen chapters with current source, Astra synthesis and appraisal protocol | Existing files | Unique findings, duplicates, conflicts and destination identified; no blind wholesale rewrite | S–M |
| B. Claim repair and numerical audit | Domain reviewers: section 2.1 corrections, source/status checks and prioritized chapter 90 reproduction | A | Consequential claims supported or explicitly weakened; original failures retained | M |
| C. Search and evidence completion | Literature researchers by family: chapter evidence/contradiction tables and dated search coverage | A; B informs priorities | Decision-driving evidence and contrary findings traced; remaining gaps have consequences and next discriminator | L |
| D. Data feasibility | Domain/data specialist: accessible soundings, noise/echo records, calibration, licensing and holdout feasibility | C can proceed incrementally | Named obtainable input or honest no-data branch; dataset reuse rights and limitations known | M–L |
| E. Shared physical specification | DSP/research owner: units, reference planes, operator, dependence and state rules in 00/60/70 | A–C | No hidden normalization, double-counted Doppler/loss or incompatible channel composition | M |
| F. Five family scenario specifications | Domain owners: section 6 branches with concrete inputs, source status and validation targets | C–E | All five families covered; absent evidence stays explicit; no unsupported natural probability | L |
| G. Statistical acceptance design | Statistical reviewer: actual endpoints, units, budgets, stopping, multiplicity and holdouts | D–F | Inference matches dependence and sampling; affordable initial test or declared limited alternative | M |
| H. Research readiness review | Independent domain/native/statistical review: trace selected scenarios to primary evidence | B–G | Section 3.1 met; no missing scientific input concealed as implementation detail | M |
| I. Contained native integration | Native engineer: appraisal-owned build and minimal canonical driver | H or a reviewed ready subset; later implementation authorization | Configure/link and identity/state/quiet checks pass; no copied modem or outside writes | S–M |
| J. First renderer and validation | Native/DSP engineer with domain review: one accepted model and comparator | I; relevant D/F/G | Mathematical checks and source-observable reproduction pass; empirical status honest | M–L |
| K. Receiver pilots and remaining families | Receiver/domain owners: complete outcomes and evidence-guided expansion | J; per-family readiness | All declared endpoints reported; unrun/inconclusive coverage retained; no reliability inflation | L |
| L. Capacity and promotion if requested | Performance/release owners: current complete-service evidence and maintained-guide integration | K; appropriate scope/authorization | Applicable gates satisfied on named hardware; publication/promotion separate | Separate estimate |

The critical research path is `A → B/C → E/F → G → H`, with data feasibility D
running alongside source work. Literature tracks can proceed independently after
shared conventions are fixed. A ready, reviewed family can later advance through
I/J without pretending the other families are qualified. Full completion of the
five-family appraisal still requires their outstanding gates.

Do not commit to a total implementation schedule before the build smoke test,
first validated renderer and one bounded complete-receiver pilot establish cost.
Data access and long physical coherence can dominate elapsed time. After those
checks, estimate remaining work from measured setup/validation costs, report
ranges and assumptions, and retain separate scientific and engineering risks.

## 11. Decisions that remain open

| Decision | Working position for planning | When a choice is needed |
| --- | --- | --- |
| Antenna and sub-9 kHz carrier | Use the recorded top-loaded 60.96 m reference and chapter-specific examples; retain alternatives | Before a site-specific link budget or validation claim |
| EME band and stations | Emphasize recorded 5.8 GHz branch; keep other-band transfer evidence | Before ephemeris/scattering calibration and station-specific margins |
| Routes and environmental populations | Use explicitly hypothetical or named measured scenarios | Before occurrence probabilities or availability estimates |
| Hardware/frontend settings | Parameterize evidence-backed configurations | Before absolute sensitivity, clipping or audio-interface claims |
| Model family | Start from simple verified references, retain evidence-backed alternatives | At each renderer's design review after its discriminator is specified |
| Primary success/reliability target | Characterize outcomes first; no invented universal pass percentage | Before a statistical qualification campaign |
| Field measurements or paid datasets | Useful possible gap-closing routes, not authorized actions | Once existing data cannot resolve a consequential question |
| Out-of-directory integration | Document exact required edit and why contained route fails | Before that edit; this task remains confined to Fable-5.2 |

These choices do not block the present completion plan. Use parameterized
branches while researching; ask for a deployment choice only when it changes a
dependent commitment that cannot be resolved from existing evidence.

## 12. Reproducibility and retention

Future scenario and result records should bind source revision, local diff or
hashes, native binary, compiler/libraries, model/data versions, configuration,
seed domains, public receiver assumptions, estimator and hardware. For datasets
record access route, rights, calibration, duration/bandwidth, preprocessing,
integrity hash and independent-content limits. A random seed alone is not a
reproducible experiment.

Do not copy whole papers, source trees or campaigns into this research directory.
Keep only permitted indispensable data with a concrete unresolved question and
cleanup trigger. The future appraisal should own its ignored generated-output
paths and limits. Existing unrelated local evidence and caches must not be
purged as part of this plan.

Keep concise findings after a branch closes: what helped, what failed, the
conditions, uncertainty and when an alternative merits reconsideration. Retain
evidence needed for unresolved failures and required assurance until resolved;
then remove redundant raw outputs and obsolete implementations. A finished plan
does not supersede scientific evidence that has not yet been distilled.

## 13. Active research execution sequence

All stages below can produce research documentation inside this directory.
Physical measurements, future simulator code, paid services and contacting
authors are not implied. Complete independent ready work while a source route
is unavailable; retain its unsatisfied gate. Do not restart already-supported
corrections merely to produce another research pass.

### Stage 1 — Establish the exact backlog and repair stale statuses

**Inputs:** baseline commit, working-tree chapters, audit N1–N7 and G01–G17,
existing scenario notes. **Outputs:** current disposition in each owning
chapter; claim and coverage matrices using sections 4.4–4.5; dependency links
from source claims to scenarios and calculations.

1. Expand each collective “not obtained/not searched” list into individual
   source/topic obligations. Include chapters without literal budget notices;
   those still contain critical measurement and theory gaps.
2. Preserve the Cameron 2024 and Yang 2019 access-status repairs made during
   this audit in chapters 32 and 41, including source locators and corrected
   detection/thermal interpretation. Complete any further parameter extraction
   needed by selected scenarios; those repairs do not supply simultaneous
   carrier covariance or a whole-disc 5.76 GHz scattering measurement. Use the
   same source-to-dependent-claim reconciliation for other stale labels.
3. Add the bounded NAVTEX, DRDC, 25 MHz radar, 30–100 MHz terrain and NTIA short
   paper findings from the audit. Do not upgrade the inaccessible AMS article,
   proceedings abstract or short companion paper beyond what was inspected.
4. Correct readiness wording where a scenario still delegates selection of
   physical inputs or laws to its implementer. Identify each unfilled input.
5. Check the previous consequential corrections in section 2.1 against their
   evidence once; retain valid work and reopen only unsupported/inconsistent
   items. This includes differing claims about which standard/source passages
   were read in different sessions.

**Exit:** all historical notice obligations have individual dispositions, the
whole corpus has an inventory, and no source is upgraded solely because its
URL works. An explicit unresolved disposition is completion of this inventory
stage, not closure of the scientific obligation. A representative sample may
estimate effort; it cannot replace the full inventory.

### Stage 2 — Finish targeted literature and contradiction coverage

Run the independent family tracks in section 4.4, starting with critical
unverified claims and the audit's accessible sources. Use chapter-local
extraction tables, with a short dated coverage record and exclusion reasons.
Follow citations in both directions and independently search for challenges.
Record the actual search cutoff; do not label future publications as reviewed.

For sub-9 kHz, prioritize the 200 ft radiator/calibration gap, below-4-kHz
propagation discrepancy and long-time noise/phase statistics. For 1 MHz,
complete the explicitly interrupted 630/160 m, NAVTEX and DRM searches plus
temporal/mixed-ground data. For 30 MHz, prioritize mechanism identification
and nearby-frequency empirical controls before adding solver sophistication.
For skywave, finish the missing named-source sets, geographic/low-HF coverage,
and measurement conditioning before assigning joint distributions. For EME,
extract original angular scattering laws and thermal calibration, then seek
whole-disc C-band and appropriately bounded other-band records. Shared work
covers noise, equipment and simulator/inference methods.

**Contradiction deliverable:** for each consequential disagreement, state the
two propositions, source locators, whether the observable/geometry/estimator
matches, competing explanations and a next discriminator. Retain unresolved
competing models when neither is eliminated. Distinguish no observed return
from no physical energy; a model's failure to predict a receiver display with
unknown inputs is not automatically falsification of its propagation law.

**Exit:** every required coverage cell is supported or explicitly limited;
every critical claim has inspected supporting evidence or a repaired/weaker
statement; no named unsearched critical lead remains hidden. If an inaccessible
source would decide a required mechanism, carry an open gate or fully specified
uncertainty branch rather than declaring the search finished because tools ran
out. Do not claim the literature itself is exhaustive.

### Stage 3 — Establish usable data and independent checks

For each measured/replay branch, identify the actual dataset or an obtainable
figure/table, its owner/version, access route and reuse terms. Verify enough
content to establish that the required observable exists. A DOI, download link
or “data available on request” statement is not a usable input.

The data record must state measurement dates/path/frequency, sensor and front
end, sample/record cadence, bandwidth, units/reference plane, clocks, missing
records, detection and compression thresholds, calibration/error budget and
processing already applied. Identify whether records are raw voltage/IQ,
transfer estimates, spectra, summaries or images. Do not construct coherent
phase from power-only data or temporal samples from percentiles.

Select independent calibration and holdout episodes/sites/epochs with a
scientific reason. A second crop of one event, new random seeds from a fitted
model or another paper reusing the same campaign is not independent field
validation. If digitizing a published plot, record scale/resolution and reading
error, preserve censoring, and bound conclusions to the observable in the plot.

**Family-specific first inputs:** a sub-9 kHz antenna/field/noise record or
declared calculated envelope; MF field/phase and conductivity measurements;
near-30-MHz geometry/environment records; HF sounder traces and the May 2024
event data; EME echo metadata/geometry and original scattering tables; calibrated
noise and front-end/blocker records. Treat the 433.8 MHz LoRa data as a candidate
at that band, not a substitute for a 5.76 GHz measurement.

**Exit per branch:** either an inspected, licensed/usable input plus a holdout
route, or a precise missing-data dependency and a separately named analytic or
synthetic comparator. Missing empirical data keeps empirical readiness open.

A specific HF starting point is Cameron section 2's
[QAN–ALE archive, DOI 10.7910/DVN/RGDNR1](https://dataverse.harvard.edu/dataset.xhtml?persistentId=doi:10.7910/DVN/RGDNR1).
The paper identifies unfiltered 2012–2016 data including low-SNR and 18.4 MHz
records. The audit reached a JavaScript/robot-verification page, so contents,
file format, license and suitability as coherent measurements remain unchecked.
Establish those before selection; do not infer raw IQ, independent holdouts or
complete attempt denominators from the existence of the archive.

### Stage 4 — Close calculations and physical model contracts

Reproduce all critical chapter 90 and scenario calculations independently from
their displayed formulas and source inputs. Save concise reproducible equations
and reference values here; do not depend on unavailable historical scratch
scripts. Verify unit/reference-plane changes, power versus field quantities,
antenna effective height/efficiency, bandwidth and noise density, delay/Doppler
signs, angular/surface weights, polarization and estimator definitions.

Check simple limits and competing representations: homogeneous and mixed ground,
vacuum phase, single/multiple path interference, fixed versus moving delay,
stationary versus changing taps, monostatic versus bistatic EME, wide versus
restricted lunar beams and integrated-power conservation. A scalar arithmetic
match alone cannot choose between physical models. Define numerical convergence
targets and relate them to source uncertainty and the intended receiver question.

Freeze common state semantics and the order of antenna, propagation, external
noise/interferers, receiver filtering/nonlinearity and conversion. Explain
identifiable and unidentifiable inputs. Specify each competing uncertainty
branch completely, including dependence, support/truncation and initialization;
avoid both unbounded arbitrary extremes and unsupported tight bounds.

**Exit:** a source-to-equation-to-parameter chain exists for all critical inputs;
alternative laws have explicit parameters and discriminators; independent
review finds no hidden normalization, missing Jacobian, double-counted loss,
delay or Doppler. Nonphysical stress laws remain explicitly synthetic.

### Stage 5 — Finish the five family specifications

Update 81–85 from the accepted evidence, not from plausible defaults alone.
Each family needs a reproducible moderate/reference branch, supported adverse
or severe branch, outage/recovery branch, and model-discriminating alternative.
Coverage can share a carefully defined episode; it need not become a Cartesian
product of every marginal extreme. A required but unsupported observed branch
remains open rather than being replaced silently by a standard test point.

For every branch fill all common fields in section 6.1 plus:

- Concrete source/input identifiers and exact numerical laws, interpolation,
  initialization and units. Identify every supplied number as measured,
  standardized, calculated, assumed or an uncertainty variable.
- Full time extent, onset/recovery, memory and tails relative to Helper, DATA,
  bit apertures and the two scored quiet slots. Preserve observations during
  source silence and avoid using evaluator truth to finish reception.
- One physical frequency response for the actual composite emission, with
  stated cross-carrier, time and polarization dependence; alternative
  dependence branches when measurement cannot determine it.
- Validation observable/estimator, independent reference, calibration and
  holdout split, tolerance derivation, and the simpler comparator that would
  make added detail unnecessary.
- Public receiver knowledge versus hidden truth, plausible implementation and
  memory/work scaling, and conditions that would justify reconsidering it.

**Specific unresolved contracts:** 81 still needs selected solver/environment
and calibrated receive inputs for a physical branch; 82 needs route/ground data
and a pinned complex-field comparator for empirical cases; 83 needs source traces
and joint-state laws; 84 needs numerical angular kernels/interpolation and their
uncertainty choices; 85 needs target transfer/calibration or explicitly synthetic
equipment laws. Resolve these before calling those branches experiment ready.

**Exit:** an independent reader can instantiate each ready branch without
further scientific choices; missing branches and their exact inputs stay visible.

### Stage 6 — Finalize bounded inference and native feasibility

For each planned receiver question choose a primary outcome, population/stratum,
independent episode sampling mechanism, horizon, error budget, finite workload
cap, uncertainty decision and stopping rule. Separate physical outage from
conditional receiver failure, false acquisition/completion, reacquisition,
latency and resource exhaustion. Predeclare how censoring and invalid external
interruptions affect every denominator.

The twenty-episode proposal in 86 is a screen under specified iid episode draws,
not a required sample size for every physical family or a reliability guarantee.
No probability interval is justified for an arbitrary hand-picked set of worst
cases. Treat those as conditional counterexamples. Use exact deterministic
checks first; choose simulation size by the decision and relevant dependence,
and report inconclusive outcomes rather than extending a fixed-n design after
viewing results. Keep communication trials separate from capacity measurements.

Review 70 against current native source/interfaces and the maintained contracts.
Confirm the proposed contained route, physical state/tail semantics, public
hypotheses, configured-calendar limits, full W=2H and memory admission. Estimate
costs structurally; do not turn estimates or old Python timings into native
capacity evidence. Record exact external edits that would be needed if the
contained route proves impossible; no such edits are authorized by this task.

**Exit:** research/statistical/native reviews agree on implementable contracts
and honest evidence gates; all unexecuted checks remain unexecuted. The later
appraisal build smoke test, physical validation and receiver trials still follow
sections 8–9 after their scope is authorized.

### Stage 7 — Independent research readiness review and handoff

Review each critical claim and every selected numerical input against its source
passage and conditions. Use independent reviewers for consequential equations,
physical transfer and statistical assumptions. Sample contextual prose for
consistency only after critical rows are complete; expand review when an error
suggests a shared failure pattern. Agreement among reviewers is not evidence
without checked sources/calculations.

Run documentation checks and reconcile source-status contradictions, broken
references, missing units, inconsistent estimator names and unsupported readiness
language across chapters, scenarios, index and this plan. Mark every original
notice obligation with its actual current outcome. Produce a concise per-family
gate statement and an exact later implementation handoff: ready inputs, models,
comparators, required checks, unresolved inputs and limits on permissible claims.

**Exit:** section 16's checklist passes for the claimed scope. If a family is
still blocked on indispensable evidence, report partial research readiness and
its dependency; do not mark all five complete. Completing this plan does not
execute its stages.

## 14. Current gate assessment

The prior pass improved the chapters and created scenarios and statistical
notes. The following assessment supersedes any inference that those additions
closed the research work. It is qualitative; no complete critical-claim
denominator has yet been established.

| Area | Current evidence | Remaining gate |
| --- | --- | --- |
| Historical notices | Census and grouped obligations audited; Cameron/Yang access entries reconciled | Remaining individual source/topic closure and chapter reconciliation |
| Whole-corpus evidence | Selected corrections and primary-source checks | Full inventory, critical claim traceability and reproducible recent coverage |
| Sub-9 kHz | Antenna calculations, mechanism synthesis and conditional controls | Target-scale calibration, low-frequency discrepancy, matched long-time environment and fixed physical branch |
| 1 MHz | Standards and nearby-frequency mixed-path evidence | Interrupted field-record search, route/temporal data and independent phase/loss validation route |
| 30 MHz | Better mechanism distinctions and nearby-band comparisons | Source-specific data, competing arrivals and justified model/environment choices |
| Skywave | Standards, several measured anchors and censored event specifications | Missing source sets, per-stratum joint-state evidence, usable traces and covariance assumptions |
| EME | Corrected geometry/link arithmetic and candidate surface representation | Frozen scattering kernels and transfer uncertainty, thermal calibration and whole-disc evidence gap |
| Noise/equipment | Source-specific Cyprus evidence, NTIA abstract, physical-plane specification | Full selected source extraction, calibrated target transfers and event/joint statistics |
| Statistics/calculations | Fixed-sample methodology and selected scalar checks | Scenario-specific estimands/sampling/holdouts and all critical calculation reproductions |
| Native implementation | Current interface/design proposal | No contained simulator built or qualified; later implementation scope |

No simulator verification, independent physical validation, complete modem trial
or hardware-capacity qualification is claimed. A documented access failure or
bounded unsuccessful search is not evidence that no suitable source/data exists.

## 15. Access failures and search resource limits

Prevent another ambiguous “budget exhausted” endpoint by making the remaining
scientific work recoverable before each search branch ends:

1. Name the decision/claim and exact missing source or query set. Record what
   was actually searched/read and what remains. Do not reconstruct fictitious
   historical search logs.
2. Distinguish tool quota, timeout/size limit, DNS/network failure, publisher
   access denial, paywall, missing bibliographic identity, no relevant result
   and unavailable dataset. These require different remedies and imply
   different confidence; none alone proves scientific absence.
3. For source access, try lawful primary publisher, DOI, author/institutional
   manuscript, official report archive and associated data routes as applicable.
   A review or smaller companion paper can support its own narrower claims.
   It does not stand in for unseen methods/tables in the original. Observe
   actual tool and access restrictions; do not bypass them.
4. Prioritize critical primary passages and preserve capacity for extraction
   and contradiction checking rather than accumulating discovery snippets.
   Reuse inspected source passages across families. If a tool limit occurs,
   record remaining work and continue permitted independent work; do not
   assert that launching more searches or agents resolves the restriction.
5. If evidence remains unavailable, choose among an explicit open gate,
   defensible bounded uncertainty branches, or correction/withdrawal of the
   unsupported claim. Record which requested coverage is lost. No source
   withdrawal can make a missing required family disappear from the scope.
6. Reopen access-limited items when a new route is found, as with Cameron and
   Yang in this audit. Preserve earlier failed access honestly, but prevent
   a historic failure from becoming a permanent assertion of unavailability.

No paid access, messages to authors, measurements or hardware rental is needed
to approve this plan. If future work genuinely needs one, first prepare the
exact evidence request or measurement specification and explain the dependency;
continue independent authorized research meanwhile.

## 16. Acceptance checklist and deliverable locations

The research handoff may claim completion only for the scope whose checks pass:

- [ ] N1–N7 and every named underlying obligation have current source-specific
  dispositions, including obligations discovered beyond the seven notices.
- [ ] All sixteen original chapters and 70–86 are represented in the claim
  inventory; every critical claim/input is supported, corrected, withdrawn or
  explicitly open with its effect on the relevant family gate.
- [ ] Search coverage through a stated date, actual queries/routes and material
  exclusions are recorded; recent, negative and contradictory evidence is
  assessed. No universal exhaustive-literature claim is made.
- [ ] All five requested families have the required moderate-to-outage coverage
  or an explicitly unsatisfied branch; observed, standardized, inferred and
  adversarial cases retain distinct labels and occurrence claims.
- [ ] Each ready branch has complete numerical laws/inputs and causal dependence,
  valid reference planes, calibration/uncertainty and continuous state/tails.
- [ ] All decision-driving calculations are independently reproducible and their
  physical applicability is reviewed separately from arithmetic.
- [ ] Data access, content, licensing, preprocessing and holdout independence
  are established where needed; missing data prevent empirical replay claims.
- [ ] Alternative models, disagreements and discriminators are explicit; added
  complexity has a purpose and better implementations remain possible.
- [ ] Statistical design matches the actual episode population and dependence,
  includes censoring/multiplicity/stopping and respects bounded test policy.
- [ ] Native integration design preserves the canonical receiver, full public
  work, quiet-slot completion, W=2H and memory bounds; unbuilt/unrun work is clear.
- [ ] Independent source/domain/statistical review and proportionate document
  checks pass; index, plan, audit and chapters agree on remaining limits.

Keep source extraction, contradictions and search coverage in their owning
chapters; shared conventions in 00/60; numerical derivations in 90 and concise
independent checks in 86; native choices in 70; scenario contracts in 81–85;
historical notice mapping in 87; active dependencies and gate status here.
Update rather than append competing documents. No session/agent registry is
needed. Keep indispensable data only with a specific purpose and cleanup rule;
do not archive whole papers or redundant campaigns for speculative use.

The immediate execution order is **inventory and stale access repair → primary
evidence and contradictions → data feasibility and calculations → fully defined
scenarios → statistical/native review → independent readiness decision**.
Independent family tracks can overlap; shared units/dependence contracts must
settle before integration. Package A is partly complete through this audit;
B–G are partly advanced but open; H has not established all-family readiness.
I–L remain the later implementation/qualification sequence, not completed work.

Estimate remaining effort only after Stage 1 exposes the denominator and a
small representative extraction pilot measures work per critical claim and
source-access class. Report ranges for accessible-source work separately from
unpredictable data/access dependencies. File length, agent count, elapsed time
and a web-search quota are not substitutes for this estimate or for completion.

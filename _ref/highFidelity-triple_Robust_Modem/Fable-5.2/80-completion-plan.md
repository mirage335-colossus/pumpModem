# Plan to complete the high-fidelity research

Updated 13 November 2026. **The research is incomplete.** The previous
research/specification handoff closed useful bounded tasks, but its completion
language was too broad. This plan restores the unfinished research obligations
from the original request and the earlier plan, and makes their closure
observable. The companion [inventory](87-evidence-remediation-audit.md) records
specific questions, evidence already inspected, source routes, dependencies,
acceptance tests and consequences if evidence remains unavailable.

This is a plan, not a report that the planned analyses have run. Completing this
planning revision does not pass the all-family research gate. Source access,
source interpretation, numerical verification, physical calibration, software
verification and receiver qualification are different achievements.

## 1. Scope, baseline and intended outcome

The requested research concerns sub-9 kHz with a 200 ft antenna, groundwave at
1 MHz and 30 MHz, skywave at 1–30 MHz, and Earth–Moon–Earth. It must explain
moderate through adverse, severe and outage conditions supported by observations,
radar, communications practice or physical theory; include recent and contrary
literature; and propose defensible implementations without claiming they are
optimal. There is no single universal natural worst case. An extreme requires a
specified environment, observable, bandwidth, integration time and conditioning
population. A deliberate stress control must be identified separately.

The audit baseline is `c42dea1c15f134a44a3ac4ed7b85936afdac687a`. Historical
comparisons include original research `07bcbc5d` and the earlier completion plan
at `33f6e461`. All 26 current Markdown documents are mapped in 87. This restores
requirements that the shortened handoff omitted; it does not pretend every
sentence of the original roughly 234,000-word corpus has been certified.

Current work changes documentation only inside this Fable-5.2 directory. Future
experiment code and test documentation belong under
`_appraisal/highFidelity-triple_Robust_Modem`, subject to the user's then-current
scope. This plan neither modifies that directory nor requires implementation to
be undertaken before the research is assessed. Read-only comparison with the
[existing appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
and [Astra-6 synthesis](../Astra-6/README.md) is part of completion, so existing
work is reused and disagreements are resolved rather than duplicated.

## 2. What completion means

| Gate | Required result | What it does not establish |
| --- | --- | --- |
| Planning/audit | Known obligations and historical omissions have concrete actions, dependencies, deliverables, closure/fallback criteria and coverage mapping | All claims verified or research finished |
| Research | Decision-driving literature, contradictions, accessible data and calculations are addressed; each requested family has physically justified branches and explicit limits | A universal natural distribution, deployed-link reliability or implemented simulator |
| Mathematical specification | All selected laws, inputs, units, operators, state, tails and reference planes are reproducible | Nature follows a selected law |
| Data ready | Exact usable products, license, version/hash, schema, units, calibration, censoring and holdouts are known | Raw waveform availability where only summaries exist |
| Generator verified | Independent reference, convergence, state and normalization checks pass in executed software | Empirical fidelity |
| Source reproduced | A source observable is reproduced using its actual estimator, geometry and selection | Transfer to another frequency, path or receiver |
| Empirically qualified | Independent matched observations support the stated domain and uncertainty | Unseen environments or worst-case prevalence |
| Receiver qualified | Canonical complete receiver passes declared communication and resource endpoints | Real-time capacity on unmeasured hardware |

Research closure can conclude that a mechanism has competing explanations or
that a particular measurement has not been found. It must still finish feasible
source review, data-usability analysis and decision-driving calculations, state
what follows from theory, and preserve the unsupported branch. An unanswered
observed-condition requirement cannot silently become a completed synthetic
control. Conversely, the original request explicitly permits theoretically
contemplated conditions: a supported theoretical branch need not wait for a new
field campaign merely to be documented as theory.

A source may be closed as contextual and unused when its unique claim is
withdrawn or a reviewed authoritative source actually supplies it. A failed
access attempt closes neither the scientific question nor the whole family.
No percentage complete should be reported before a defined claim denominator
exists. Even then, percentages cannot substitute for the remaining critical
questions.

## 3. Priority, work types and dependency order

The inventory uses **P0** for misleading status or evidence dependencies that
must be repaired before interpreting a result; **P1** for consequential research
or selected-scenario work; **P2** for material alternatives, coverage or transfer
questions; and **P3** for contextual leads. Priority is not permission to omit a
requested domain. Types are **R** source review, **D** existing-data analysis,
**A** calculation, **S** specification, **E** genuinely external evidence, and
**I** later implementation/qualification. An item can have several types; split
its accessible steps from its external dependency before declaring it blocked.

The main dependency sequence is:

1. Correct status and build the consequential-claim/source/consumer map.
2. Extract decisive primary evidence and make contradictions explicit.
3. Analyze usable public data and reproduce consequential calculations.
4. Specify physically conditioned branches and their evidence limits.
5. Reconcile statistical purpose, computational feasibility and native interfaces.
6. Conduct independent all-family research-readiness review.
7. Later, implement and qualify only admitted experiments within authorized scope.

Family source work can proceed in parallel once common units and evidence fields
are fixed. Independent mathematical checks can begin before every contextual
source is retrieved. Data collection does not need to wait for model selection,
but fitting and holdout selection must not use the same outcomes twice.

## 4. Work packages and observable exits

### WP00 — Repair status and restore the audit denominator

**Inputs:** all 26 documents, original 16-chapter draft, historical plan, 87
inventory and existing source receipts. **Covers:** C01–C06 and family P0 items.

- Correct stale access and attribution statements wherever repeated, including
  summaries, tables, references and scenario prose. Preserve dated unsuccessful
  attempts as history, not current status.
- Enumerate each consequential assertion: selected parameter/equation/table,
  feasibility/range statement, extreme, model choice, distribution, implementation
  constraint and contrary finding. Split measured values from inferred causes.
- For each, record stable claim ID, owning heading, source locator and actual
  read level, evidence type, frequency/geometry/estimator, uncertainty, consuming
  scenarios/calculations, unresolved action and acceptance evidence. Maintain
  this scientific map in existing chapters/87; do not create an agent registry.
- Reconcile original named missing-source lists against that map. A grouped
  source row must still identify every named source and its individual use/status.
- Compare Astra-6 and the appraisal protocol read-only: reused result, unique
  finding, conflict, obsolete proposal, missing evidence and eventual destination.

**Exit:** no unsupported global completion claim; each known consequential
obligation and original named lead has a disposition; dependency links identify
which results must change if a source or calculation is corrected. This audit
has identified the work, but the complete assertion-by-assertion extraction is
still a research package, not already finished by the inventory's existence.

### WP01 — Complete the targeted literature and contradiction review

**Depends on:** WP00's questions and source identities; independent family work
can proceed immediately. **Covers:** R portions of V, GW, H, E and S inventories.

For each consequential question, search the relevant primary publisher/report
repository, author/institutional copy, data/supplement repository and citations
of the decisive paper. Resolve exact title/edition/DOI before pursuing ambiguous
names. Inspect methods, tables/figures, calibration and exclusions, not only the
abstract. Public access failures may be tried through another ordinary lawful
primary route; a web reader's MIME/size failure is not proof of restricted access.
Do not bypass access controls or initiate author contact or purchases as a
routine consequence of this plan.

Use these coverage dimensions, with actual searched/unsearched cells:

- Foundational pre-2015 work; 2015–2019; 2020–2024; 2025 to the actual search date.
  A recent publication is not automatically more applicable than a classical
  measurement, and an older model is not automatically validated today.
- Mechanism, frequency and geometry; quiet/moderate, adverse, severe and outage;
  latitude, local time, season, solar/geomagnetic state and terrain/sea/site class.
- Direct calibrated complex measurements, power/summary measurements, radar or
  sounder proxies, communications successes and failures, physical theory,
  standards and deliberate synthetic controls.
- Time scale and resolution: sample/pulse, carrier coherence, symbol/slot,
  complete message and inter-message/environment evolution. Include joint
  amplitude/phase/delay/Doppler, cross-frequency and cross-mode dependence.
- Competing mechanisms, null/negative evidence, detection thresholds and omitted
  events. Use translated regional primary leads when they can change a geographic
  conclusion; do not claim every language/database was searched.

Record query/date/interface, source identity and inspected locator, inclusion or
exclusion reason, next citation route and which matrix cell it addresses. Reuse
existing genuine logs; never invent the original undocumented searches. Stop a
bounded search when the planned routes and decisive backward/forward citations
have been inspected and additional hits repeat known evidence without resolving
an open critical claim. Record remaining holes explicitly; no fixed query count
proves comprehensive coverage.

For every material contradiction, record the exact competing propositions,
source locators, matched or differing observables, candidate explanations,
smallest discriminating calculation/data check, and consequences for scenarios.
Examples include sub-4-kHz daytime attenuation, MF seasonal sign, P.1147 versus
P.1321 short-range levels, HF width/availability estimators, noise-survey
estimators and lunar angular/width/polarization conventions. “Sources differ”
without this comparison does not close the contradiction.

**Exit:** all selected numerical/qualitative inputs are traceable to inspected
support or explicitly bounded theory/assumption; contrary evidence changes the
branch or uncertainty where appropriate. Context-only unread sources remain
named but cannot supply hidden numerical authority.

### WP02 — Analyze already identified public data

**Depends on:** a precise model question and readme/schema inspection; not on a
finished waveform renderer. **Covers:** V20, GW07–GW09, H data items, E04/E05/E27,
and shared public-noise items.

| Data route | Bounded analysis to perform | Required artifact and exit |
| --- | --- | --- |
| Gu ELF archive, DOI 10.6084/m9.figshare.13536611.v1 | Reuse verified access/version/checksum; inspect units, cadence, calibration and exact figure/MAT/text structure. Reproduce a supported published curve and test whether any series supports the required time/frequency statistic. | File-level usable/excluded map, reproduced observable and uncertainty. Derived monthly/hourly summaries cannot become high-rate noise or sub-hertz waveform law. |
| QAN–ALE Harvard Dataverse archive | Enumerate all schemas/dates, resolve metadata-template mismatch, decode units such as signal-power fields from producer methods/code, reconstruct operating schedule and attempted-pulse denominator, identify missing/invalid/nondetected records and estimator resolution. Use selected episodes first, then a coverage-preserving subset. | Counts and units reconciled to article figures/tables, conditional event statistics with censoring and independent holdout; unresolved operations/calibration prevent unconditional availability, not every summary analysis. |
| NTIA Phase 1 ZIP and Parts 2/3 | Inspect the official archive/readmes/formats, then a minimal route/frequency subset. Recover transmitter/receiver calibration, antenna/terrain, sampling duration, site selection and censoring. Read remaining report methods before interpreting values. | Decode manifest, usable record counts, limited model-versus-observable comparison at actual 20.084/49.72 MHz. Neither interpolation to 30 MHz nor identification as pure surface wave is automatic. |
| HF storm/radar/sounder figures and public records | Obtain the smallest source data product that resolves the question; otherwise digitize exact axes/curves with error bounds, masks and threshold conventions. Match schedules, path/frequency, solar/geomagnetic and local-time metadata. | Reproducible event-level observables and uncertainty. Figure resolution, radar backscatter and decoded-link selection bound inference; no manufactured complex IQ. |
| 433.8-MHz EME SigMF/Zenodo | Metadata access is complete. Plan a bounded public raw subset after confirming sample format, time alignment, channel labels and calibration. Separate direct/CW leakage, lunar return and receiver processing; retain unsuccessful windows if the schedule supports them. | Checksums, calibrated or explicitly relative observables and source-matched estimator. A different-band self-echo is not a measured 5.76-GHz two-station channel. |
| Mini-RF PDS4 | Select one product only if its labels, polarization and calibration can discriminate a model assumption. Reproduce a local published quantity, including processing-version and leakage limitations. | Product-level observability and transfer limits. Focused/local SAR imagery cannot substitute for whole-disc complex EME time history. |
| Noise surveys and associated files | Recover detector/bandwidth/excision/floor/overload rules; reproduce a representative PSD/APD or summary using actual source definitions; retain site/time stratification. | Reconciled estimator and selection effects, no unsupported narrowband occupancy or universal temporal law. |

Each dataset record needs URL/DOI, version/date, license, verified hash when bytes
are obtained, units, calibration/reference plane, schema, missingness, included
and excluded counts, estimator and uncertainty, fit/holdout separation, and a
cleanup trigger. Do not retain whole archives merely because access succeeded.
A product that cannot answer the stated question may be excluded with evidence;
its failure to contain raw IQ must not conceal other feasible analyses.

### WP03 — Reproduce the calculations that drive conclusions

**Depends on:** source conventions and exact inputs, with results fed back to
all consumer claims. **Covers:** C09–C12, H loss checks, GW numerical checks,
V antenna/propagation checks, E geometry/kernel checks and S noise checks.

| Calculation | Minimum independent discriminator | Closure or failure disposition |
| --- | --- | --- |
| HF P.533 loss | Pin normative revision, official code commit, coefficient data and all inputs; reproduce published Kranji–Beijing example and selected ordinary/adverse cells. Decompose absorption, Lz, above-MUF terms, hop/control points, screening and distance rules. | Explain the roughly 11 dB disagreement with the historical simplified calculation; correct dependent ranges/presets. Do not hide a systematic discrepancy inside an arbitrary 10 dB margin. |
| P.372 noise | Pin code/data including v14.3 corrections to atmospheric time-block interpolation and SigmaDU/SigmaDL; reproduce source examples and component combination at stated reference planes. | Distinguish medians, percentiles, power addition and variability; no derived time process from marginal deciles. |
| Groundwave | Reproduce actual fields and 216 threshold-distance entries with a suitable pinned reference; separately verify 21 radius sensitivities, 18 conductivity amplitude/phase pairs, 45 height cells, derivatives and 24 downstream noise/range combinations. | Magnitude agreement does not certify complex phase. Preserve model-domain/terrain/height limitations; regenerate or withdraw affected conclusions. The 144 field/loss identities already checked are not these checks. |
| 200 ft antenna and sub-9 propagation | Separate alternative geometries, source-side impedance/current, matching/loss, vector fields and sensor response. Execute selected LMP model/root convergence and circuit checks with independent limiting cases. | No Ez-to-B shortcut; no calibrated efficiency/range inferred from a constructed circuit. Distinguish LMP from LWPC and pin the actual software. |
| Lunar response | Independently integrate all outstanding whole-disc and beam-limited Doppler width columns, source weight/width conventions, beam/noise integrals and frequency covariance. Execute specified NAIF geometry/time/frame inputs with limiting and convergence checks. | Delay-integral agreement does not validate Doppler columns. Report geometry/kernel predictions as predictions until source-matched empirical comparisons exist. |
| Remaining applicability | Test field/antenna/reference-plane translations; line drift versus temporal crossing; Faraday geometry/outcome; two-path correlation versus any claimed extremum. | Correct or scope the conclusion, not just its arithmetic. Keep assumptions and untested physical transfer visible locally. |

Use a small independently derived reference, dimensional identities and
convergence/limiting cases before expensive calculations. Independence must be
specified: copying the same equation into a second script tests arithmetic,
not the equation's physical validity. Retain exact input, method/version,
expected observable, numerical tolerance, output/residual and pass/fail evidence
in the owning calculation/scenario chapter. Never relabel unrun checks passed.
Analysis scripts may support research, but cannot become a second modem DSP
implementation or persistent receiver.

### WP04 — Close the sub-9 kHz / 200 ft physical specification

**Depends on:** V01–V24 source/data/analysis results and WP03. Resolve transmitter
topology, effective height, earth return, losses, bandwidth, voltage/current and
weather detuning; distinguish source soil from propagation soil. Treat sub-4-kHz
attenuation disagreement, magnetoionic directionality, D-region profile
uncertainty and low-frequency mode convergence as live questions. Specify what
supports the coherent phase/fade/transition law over the unusually long receive
aperture. Match the E/B sensor and self-noise to the actual modeled field.

**Deliverable:** revise 10–12/81 with a source-to-receiver chain, explicit
quiet/adverse/severe/outage and countermodel cases, missing empirical conditions,
and complete parameter provenance. **Exit:** no assumed 61 m efficiency,
magnetic conversion or fade distribution presented as measured; every selected
control executable from stated inputs, with valid theoretical alternatives.

### WP05 — Close both groundwave physical specifications

**Depends on:** GW01–GW16, shared noise and WP03. Separate smooth homogeneous
surface wave, mixed/coastal recovery, layers/ice, terrain/height/reflection,
rough sea and competing skywave/duct/scatter. Fix actual geometry before using
conductivity maps. Compare seasonal claims on their actual statistics. At
30 MHz, decide which mechanism can dominate each received level; high loss is
not evidence that the remaining signal is groundwave. A sinusoidal attenuation
stress is not a rough-sea model.

**Deliverable:** 20/25/82 source-to-mechanism and parameter tables, paired
model disagreements, measured-versus-assumed temporal laws, and clear 1 MHz
versus 30 MHz coverage. **Exit:** official magnitude references executed,
phase limitations retained, adjacent-band/radar data used only for observable
support, and competing modes cannot be silently omitted from an extreme case.

### WP06 — Close HF level, dispersion, dependence and availability

**Depends on:** H inventory, WP01–WP03 and shared noise. Reconcile quiet/local
stationarity versus disturbed and nonstationary conditions; NVIS, midlatitude,
high-latitude, equatorial and long-haul coverage; O/X/multimode and carrier
covariance; delay and Doppler width definitions; geometric/absorption outage
versus missed operation/detection. Separate radar velocity/spectral width,
ionosonde output, narrowband decoded level and one-way complex channel evidence.

Inspect the newly identified September 2026 Zhu et al. REC533/ICEPAC/VOACAP
comparison in full before adopting any result. Resolve the P.533 numerical
baseline before interpreting skywave preset/range conclusions. Analyze QAN–ALE
and source-resolved storm records before calibrating availability or transitions.
Do not infer an unconditional inverse-square absorption correction from a
remote riometer or a selected detected-pulse population.

**Deliverable:** corrected 30–33/83, estimator conversion and contradiction
tables, conditioned severity branches, shared/independent process alternatives,
and data-derived quantities only where supported. **Exit:** the selected HF
model family has a stated observable/domain and uncertainty; missing joint
covariance remains an explicit model disagreement rather than a hidden default.

### WP07 — Close EME scattering, geometry and station specification

**Depends on:** E01–E34 plus WP02–WP03 and equipment. Reconcile 3.8/23-cm angular
laws and calibration, whole-disc versus local SAR, delay/Doppler width
estimators, polarization basis, frequency transfer and speckle dependence.
Execute retarded two-leg geometry, orientation/time conversions, beam-weighted
response/noise and the exact source/quiet horizon. Treat cross section and
system temperature at 5.76 GHz as assumptions unless matching evidence supports
them. Separate satellite point-source scintillation from two-leg extended-lunar
evidence and state the transferable limits.

**Deliverable:** corrected 40/41/84, source/mathematics transfer table, verified
geometry and kernel observables, public-product results and station worksheet.
**Exit:** all selected constants and conventions trace to evidence or explicit
theory/assumption; the expensive full-grid renderer has a credible bounded
reference and approximation plan; no target-band empirical law is invented.

### WP08 — Close shared noise, equipment and stochastic methods

**Depends on:** S inventory plus family reference planes. Reconcile mean/median,
PSD/APD, bandwidth and excision conventions across noise surveys; distinguish
occupancy, wanted/co-channel signals, man-made impulses and atmospheric noise.
Review actual phase/clock/allan/AGC/filter/overload evidence, including recovery
and quiet tails. Establish what each front-end control represents and which
values remain deliberate engineering choices.

Complete the remaining methods-source review and specify finite state,
initialization, correlation, interpolation, delay motion, random seeds,
chunk invariance, tail/error bounds and convergence. Preserve the already checked
finite-FIR covariance arithmetic; do not count it as a new gap or as proof of a
native renderer. Compare Gaussian, OU, finite coherent and non-Gaussian models
using their intended observable and domain, not convenience alone.

**Deliverable:** 50/55/60/85/86 corrections and parameter-to-reference-plane
crosswalk. **Exit:** each impairment has an order in the physical chain and a
source/assumption tag; uncontrolled mixtures cannot masquerade as a measured
joint environment. Define false completion and receiver sensitivity endpoints,
including impulses/blockers/front-end/clock cases absent from the current
15-stratum screen.

### WP09 — Reconcile statistics, cost and native feasibility

**Depends on:** research/model choices, exact receiver geometry and cheap cost
bounds. **Covers:** C13–C16 and I design portions of family inventories.

First select the scientific decision: failure finding, model comparison,
conditional success probability, or natural-population reliability. Reconcile
the appraisal protocol's small 12-wanted/6-negative-control pilot with 86's
15-stratum, at-most-195-episode conditional design; they are different proposals,
not mandatory cumulative workloads. Determine whether proposed p≥0.8 and
multiplicity rules can support the intended conclusion with that sample budget.
If the chosen interval cannot pass even with all successes, it is a screen with
limited power, not a probability-qualification plan. Include noise/equipment
coverage and preserve all invalid/setup/resource failures in their proper counts.

Predeclare population, independent episode unit, estimand, path/condition strata,
allocation, uncertainty, multiplicity, censoring, paired-seed use, stop rule and
failure categories. Repeated timing runs and paired realizations are not new
independent natural trials. No joint prevalence from separately sampled marginal
extremes. Choose a budget appropriate to the question and measured cost before
seeing results; insufficient budget leaves the claim unqualified.

The ten-minute pilot and thirty-minute campaign caps in 86 are **proposal
choices**, not user/project-wide mandates. Preserve their provenance for that
design; revise only through a new explicit, bounded design consistent with
[the intensive-test policy](../../../docs/robust-current.md#testing-computationally-intensive-robust-changes).
A cap may reject a campaign, but cannot erase required scientific endpoints.
Fifteen sub-9 episodes represent at least 421.875 hours of physical stream before
extra margins. The literal 256×512 EME grid at 48 ksample/s for 1800 s requires
11,324,620,800,000 cell-sample products. Neither is a measured compute cost.

For each candidate representation specify exact reference operator, cheaper
candidate, protected observable/error bound, minimal discriminator, full state
and memory bound, approximation domain and fallback. Count preparation,
unsuccessful acquisition, tracking, reacquisition and quiet completion, not just
channel rendering. A small grid, latent-state update, delay–Doppler aggregation
or native bulk contraction merits evaluation only with error/state evidence.
No routine multi-day exhaustive campaign or unapproved cloud expenditure.

Bind source/configuration hashes and actual returned geometry, sample rate,
positive IF, DATA power and native public hypothesis bounds. Check whether
physical Doppler/drift/support is admitted; reject or explicitly revise an
incompatible fixture rather than clip the physical input. Preserve the
configured-calendar/keyed-ordinal scope; unknown-start acquisition remains
unqualified. The contained CMake/build route in 70 is a proposal until executed.

**Exit:** one coherent staged design with affordable discriminators, complete
endpoints and clear interpretation of pass/fail/inconclusive; no capacity or
hardware claim from design documents alone.

### WP10 — Independent research readiness decision

**Depends on:** WP00–WP09's research deliverables, not a compulsory new field
campaign or completed simulator. Review all five families and cross-cutting
inputs against the original request, the coverage map and every P0/P1 item.
Trace a representative critical chain in each family and every high-impact
correction from source through calculation to scenario and conclusion; expand
sampling wherever an inconsistency appears. Check that no difficult branch was
removed simply to declare completion.

**Exit:** explicit per-family decision: research-supported, theory-supported
with limits, unresolved consequential research, externally uncalibrated, or
later implementation. Any remaining P0/P1 research work must be completed or
have a genuine, documented limitation with its missing requested coverage still
visible. Contextual source dispositions are not all-family scientific closure.
The final report must state what remains unknown and which claims are permitted.

### WP11 — External evidence and later implementation

External measurements are separate work packages with exact missing inputs:
calibrated 50–100 m sub-9-kHz antenna/current/field/sensor records; coherent
near-1-MHz and 30-MHz path time series; target HF joint/transition evidence;
paired calibrated C-band whole-disc EME; and target equipment/site telemetry.
Each proposal must define frequency, geometry, calibration, clocks, raw/derived
products, complete attempted-window denominator, invalid/missing records,
conditions and independent holdouts. Public literature and archive routes must
be evaluated first. Lack of a new campaign does not prevent all feasible
research, but it forbids unsupported observed reliability/range claims.

When implementation is in scope, use the existing canonical C++ modem and
contained appraisal harness, in this order: configure/link identity and no-effect
fixture; deterministic physical/reference operators; one qualified stochastic
renderer; selected solver adapter; shared front-end and physical-completion
checks; source-observable reproduction; bounded complete-receiver pilot;
independent empirical comparisons. No Python operating modem or duplicate
receiver, no hidden channel truth, no reset at Helper/DATA/quiet boundaries,
no discarded delayed tails, and no message-length/EOF completion shortcut.

Preserve all public hypotheses, explicit resource limits and the full
`W=2*H` receiver budget. Follow native, precision, representative-hardware and
GPU-selection contracts when those decisions arise; an implementation plan is
not hardware qualification or spending authorization. Report actual device,
runtime and arithmetic for any later measurement. Better representations remain
eligible when they preserve outcomes and improve complete-service feasibility.

## 5. Scheduling and uncertainty

Do not attach an invented completion date to a source inventory. First run a
small representative research pilot: one available full-text extraction, one
scan/ambiguous citation, one archive-schema analysis and one independent scalar
check. Record elapsed effort, number of dependent claims discovered, access
latency and rework. Then estimate remaining local work by item class and
critical dependencies; keep external waiting time separate. Update estimates
when evidence changes, without reducing acceptance criteria to fit a date.

Parallel streams are sub-9, groundwave, HF, EME and shared methods/equipment.
The common claim schema, reference planes, source versions and statistical
purpose require integration before branch acceptance. Numerical findings feed
back into literature/model selection; this is an expected dependency, not an
excuse for endless expansion. A new lead enters the critical path only if it
can change a selected claim, requested coverage or implementation decision.

The first execution tranche should finish WP00, then run the P.533/P.372 version
and example check, NTIA archive feasibility, QAN–ALE schema/denominator work,
EME epoch/width checks, and the sub-9 field/antenna convention audit in parallel
where independent. This makes the largest avoidable uncertainty concrete before
committing to a full renderer or field campaign.

## 6. Acceptance checklist and maintenance

- [ ] All 26 documents and original named gaps are mapped; consequential claims
  and their consumers are enumerated, not merely counted by text matches.
- [ ] Source identity/read level, source versus inference, and actual search
  coverage are consistent in summaries, tables, bibliographies and scenarios.
- [ ] Material contradictory findings have matched observables, explicit
  alternative explanations and a resolved or bounded consequence.
- [ ] Accessible public data have undergone the planned usability/observable
  analysis, or a concrete access/content limitation and lost coverage is recorded.
- [ ] Decision-driving physical calculations are independently reproduced;
  historical arithmetic checks are not substituted for missing solver/phase work.
- [ ] All five requested families cover moderate, adverse, severe and outage
  mechanisms with observation/theory/control and time/dependence limits visible.
- [ ] Selected branches have complete parameters, reference planes, state,
  uncertainty, alternatives and actual receiver-interface bounds.
- [ ] Statistical purpose, sample unit, budget, stopping rule, endpoints and
  computational feasibility agree across 60/70/80/81–86 and adjacent proposals.
- [ ] External evidence, later implementation, empirical fidelity and hardware
  capacity remain distinct; unrun work is never reported as passed.
- [ ] Independent readiness review confirms no original requirement disappeared
  through renaming, source-access disposition or choice of synthetic controls.

Check relative links/anchors, tables/fences, whitespace, stable inventory IDs,
source-status consistency and the allowed edit boundary after each integrated
revision. Documentation-only planning requires no modem benchmark. Keep durable
scientific provenance, source-specific failed results and limitations; remove
temporary papers/data/calculation output after their concrete investigation ends.
After authorized adoption, distill unique findings into the appropriate maintained
guides/appraisal records before removing superseded material. Do not edit those
other destinations as part of this research-only scope.

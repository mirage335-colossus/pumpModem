# Audit of search limitations and evidence remediation

Audited November 13, 2026 against commit
`07bcbc5da41ea5e8ec43517ffb92e51710037744` and the subsequent working-tree
research additions. **The historical search gaps have only partly been remedied.**
Seven identifiable search-coverage notices occur in the committed chapters.
Six retain their original notice wording; the seventh, in chapter 41, now
describes a targeted rather than systematic search and adds newer references.
Both explicit statements that a search allowance or budget ran out remain.
Several consequential source, calibration and model-specification gaps remain
as well. The earlier description of the research pass as completed must not be
read as closure of these obligations or as research readiness for every family.

The [completion plan](80-completion-plan.md) is the active plan for closing this
work. This audit supplies its historical evidence and concrete backlog; it is
not a second operating protocol or a claim-by-claim certification of the corpus.

## Baseline and limits of this audit

The cited commit is the current HEAD at audit time, titled “DRAFT - WIP -
degrading research beginning with Fable 5.2 continued with Opus 5.5 .”, committed
November 13, 2026 at 11:44:32 -0600. It is the only commit returned by the
directory's path history. Its sixteen chapters are the historical comparison
set. The later sixteen chapter edits and nine added documents were uncommitted
at the start of this audit; they must not be attributed to a later Git commit.

The audit compared those versions, inspected search-limit and missing-work
sections across the sixteen chapters, reviewed the scenario companions, and
tested selected consequential access claims against primary sources. It did
not independently verify every assertion in approximately 234,000 words of
historical research. The seven-notice census is an exact textual inventory of
the identified search-coverage notices, not a count of all missing sources or
unique scientific questions. Broader historical searches produced 173 candidate
lines about missing material or source access across fifteen chapters. Those
lines were not all individually adjudicated in this audit; they are locators,
not 173 distinct unresolved obligations. One paragraph can list many sources; a single
gap can recur in several chapters. The grouped obligations below are planning
units, not a statistical estimate of research completeness.

## Historical notice census

Line numbers below refer to the baseline and the pre-audit working files;
these particular notice locations coincide in both. Later insertions may move
lines, so the quoted phrase and named section identify the durable location.

| Notice | Chapter and line | Historical subject | Current disposition |
| --- | --- | --- | --- |
| N1 | [12](12-sub9khz-noise.md), 10 | Web searching limited; recent literature thinner | Retained. Sources not obtained and topics not searched remain in its final gap section |
| N2 | [20](20-groundwave-1mhz.md), 12 | Eleven searches; incomplete 2015–2026 coverage | Retained. A search count is historical provenance, not a coverage criterion |
| N3 | [20](20-groundwave-1mhz.md), 401 | Allowance ran out before renewed searches for 630/160 m, NAVTEX and DRM field records | Retained. A relevant NAVTEX paper was found during this audit, but the grouped obligation is not closed |
| N4 | [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md), 10 | Budget ran out after about 25 searches | Retained. Later direct downloads and rechecks are described, but full coverage is not demonstrated |
| N5 | [31](31-skywave-dispersion-midlatitude-nvis-longhaul.md), 417 | Sources not obtained in full because of budget or access | Retained. CCIR, Canadian wideband, MITRE/Chinese conference, WHISPER, DRM, Indian and book leads still need individual dispositions |
| N6 | [41](41-eme-systems-practice-and-environment.md), 339 | 2015–2026 searches limited to priority topics | Revised with recent source identities and transfer limits; still explicitly non-systematic. No resulting whole-disc C-band measurement is established |
| N7 | [60](60-simulation-methods-and-validation.md), 19 | Limited literature search and briefly/unsearched topics | Retained. Its final gap section still identifies simulator, noise and verification literature not inspected |

The following exact-expression check returns **seven baseline matches and six
working-tree matches** in the sixteen historical chapters. N6 changed wording;
following that paragraph manually gives seven surviving notice sites. Do not
report seven literal current matches for this expression.

```sh
git grep -n -i -E 'Search limitation: eleven web searches|search allowance ran out|web-search budget ran out|not obtained in full \(search budget or access\)|web searching for this chapter was limited|searches for 2015–2026 peer-reviewed work were limited|literature search was limited' 07bcbc5da41ea5e8ec43517ffb92e51710037744 -- _ref/highFidelity-triple_Robust_Modem/Fable-5.2
```

For current original chapters, apply the same expression with `rg` to the
sixteen chapter paths, excluding the index and documents 70–87: this audit and
the plan themselves quote the notices. Broader searches for budget, allowance,
exhaustion, limited search, not searched/obtained, and abstract-only statements
are necessary to discover obligations outside this phrase census. Mere keyword
matches also include unrelated link budgets and physical search limits and
must be inspected in context.

## How remediation is classified

Use separate source-access and scientific-support decisions. Full text becoming
available resolves an access obstacle; it does not establish that a particular
model parameter or extrapolation is supported.

| Disposition | Required evidence |
| --- | --- |
| Resolved by evidence | Supporting primary passage and locator inspected; conditions and uncertainty extracted; affected claim/scenario updated consistently |
| Resolved by correction or withdrawal | Unsupported assertion corrected or removed, dependent conclusions repaired, and resulting coverage loss made explicit |
| Partly resolved | Particular corrected/source-supported elements identified alongside remaining obligations |
| Open | Named search, source inspection, calibration, derivation or specification is still needed |
| Access limited | Actual access level and attempted lawful route recorded; weaker claim bounded; alternative route and consequence specified |
| Not established by record | No adequate trace from old obligation to supporting remedy; no inference that an unrecorded investigation did or did not occur |

A historical “not found” statement need not be erased. Date it and append the
current disposition. A general disclaimer or a synthetic fallback cannot close
a required empirical claim. Conversely, a deliberately withdrawn, nonessential
claim need not force an indefinite search for its original source.

## Substantive remedies and remaining obligations

These groups cover the consequential omissions found in the chapter-level
audit. They are not an exhaustive bibliography. Package names refer to the
existing A–L work packages in the completion plan; the active execution order
is in its section 13.

| Group and provenance | What has actually improved | Remaining work and effect on readiness | Packages |
| --- | --- | --- | --- |
| G01 — N1; 12 final gap section | Additional regional noise references and narrower Gaussian/noise claims | Extract recent sub-9 kHz PSD/APD, sferic clustering, duration, storms, mains and receiver calibration; no universal narrowband Gaussian or event probability from site-limited summaries | B/C/D/F |
| G02 — 10 final gaps; 81 | Measured/model overlap at 5.17 kHz clarified; lower-frequency disagreement retained | Inspect missing finite-difference and attenuation sources; define field component, path, ionosphere and uncertainty below roughly 4 kHz; select a solver and source-matched comparison | B/C/E/F |
| G03 — 11 open questions; 81 | Antenna assumptions, field/current conversions and some historical reports clarified | No demonstrated matching 61 m installation record below 9 kHz; efficiency, ground/coil loss, weather/corona/detuning and long-window phase remain conditional. Specify calibrated inputs or bounded forecast-only alternatives | C/D/E/F |
| G04 — N2/N3; 20 | MF mixed-path and layered-ice evidence has clearer scope; this audit found a 2026 NAVTEX field paper | Finish named 630/160 m, NAVTEX, DRM and M.823/IALA searches/inspection. Nearby frequency or receiver RSSI cannot silently become calibrated 1 MHz path loss | B/C/D/F |
| G05 — 20 temporal/ground gaps; 82 | Complex-field comparator and phase-reference requirements are clearer | Find or bound 1–600 s amplitude/phase evolution, conductivity/seasonal/layered uncertainty and nighttime mode mixtures. A route-average field standard supplies no joint temporal process | C/D/E/F |
| G06 — 25 missing measurements; 82 | Kirincich–Emery frequency/geometry and distinct Dagefu studies corrected; wanted versus unwanted scattering distinguished | Retrieve 25 MHz sea-state and 30–100 MHz terrain leads; identify surface/space/diffraction/radar mechanisms before importing values; obtain path-resolved competitors or keep a derived sensitivity | B/C/D/F |
| G07 — N4/N5; 31 tables J and final gaps | Watterson/source-estimator checks and NVIS O/X interpretation improved | Dispose of each named source individually: CCIR 549/266, WHISPER, Canadian wideband, MITRE/Chinese statistics, DRM, Indian sounding, books and historical spread studies. A collective “access” label is insufficient | B/C |
| G08 — 30/32; 83 | Storm context, detection censoring and geographic strata improved; Cameron full text located in this audit | Extract detected/non-detected denominators, operational gaps, instrument ceilings and joint observables. Obtain the May 2024 trace before calling it replay; strengthen equatorial and low-HF coverage independently | B/C/D/F |
| G09 — 31/33; 83 | Continuous phase and one-composite-channel requirements clarified | Actual carrier-spacing complex covariance, mode birth/death, slow quality variation and model bandwidth validity remain unresolved. Specify competing dependence/model branches and a source-matched discriminator | C/D/E/F |
| G10 — 40/41; 84 | Coupled geometry, power normalization and radar-transfer limits corrected | Retrieve original angular laws and conventions; freeze numerical endpoint functions, wavelength interpolation, polarization and diffuse/specular mixture. Current kernel family is not one deterministic ready-to-run channel | B/C/E/F |
| G11 — N6; 41 measurements and access gaps | Recent local SAR evidence and its non-transfer to whole-disc EME made explicit; Yang full text located in this audit | Find calibrated whole-disc 5–6 cm echo/IQ or preserve the prediction-only limit; inspect 433.8 MHz LoRa data on its own terms; assess beam-weighted lunar temperature and unmeasured EME scintillation without substituting point-source statistics | C/D/E/F |
| G12 — 50 missing-source lists; 85 | Cyprus papers now substantively inspected with thresholds/bandwidth/antenna caveats; unsupported 250 Hz occupancy conversion removed | Inspect Fockens/thesis, Lemmon/Giesbrecht and other decision-driving noise works; retain distinction between occupancy, antenna-port PSD/APD and decode probability; no fabricated subchannel probability | B/C/D/F |
| G13 — 50/85 NTIA source note | TR 25-579 identified and limited to its abstract; this audit found a short primary companion paper | Full report methods, calibration, compression handling and tables remain uninspected here. Companion study supplies useful partial evidence, not full-report closure | B/C/D |
| G14 — 55; 85 | Noise planes, physical ordering and conditional equipment cases clarified | Pin target front ends or explicitly synthetic transfer functions; specify phase noise, clocks, filtering, nonlinear recovery, AGC, ADC and blocker measurements. Hardware realism remains unqualified | C/D/E/F |
| G15 — N7; 60/70/86 | Episode-level replication, fixed-sample stopping, censoring and dependent-test caveats corrected | Inspect remaining selected generator/validation methods; tie validation tolerances to source uncertainty and use; specify a concrete sampling distribution and holdouts before invoking probabilistic intervals | B/C/E/G |
| G16 — 00/90 and all scenarios | OU description, source grades, numerical examples and several physical interpretations repaired | Independently reproduce all decision-driving calculations and trace assumptions, estimator differences and source values; scalar agreement does not validate models | B/E/G |
| G17 — all chapters and 70–86 | Index, native options and family specifications now exist | A complete claim inventory, reproducible recent-search coverage and independent readiness review are still absent. Missing physical inputs cannot be filed as mere coding details | A–H |

The improvements in this table are substantive. They do not support a claim
that all old notices were merely deleted, nor a claim that every obligation
behind them was completed. No such all-or-nothing inference is warranted.

## Primary-source checks that change the next actions

These are bounded checks for this audit, not the promised comprehensive search.
Access was checked on November 13, 2026. Each source must ultimately be entered
once in its owning chapter's evidence table with exact supporting locators.

| Source and inspected access | Consequence and remaining limit |
| --- | --- |
| [Cameron et al. 2024](https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2023RS007897), full publisher text; measurement and statistical-analysis sections | Chapter 32 was abstract-only at audit start; its source status and detection-conditioned interpretation are now corrected, with section/figure locators. The full text gives the Qaanaaq–Alert acquisition/cadence, quality gates and operational coverage. The scheduled frequencies do not establish simultaneous complex covariance at the modem's carrier spacing; a calibrated joint scenario remains open |
| [QAN–ALE dataset, DOI 10.7910/DVN/RGDNR1](https://dataverse.harvard.edu/dataset.xhtml?persistentId=doi:10.7910/DVN/RGDNR1), identified in Cameron section 2 | The paper identifies unfiltered 2012–2016 data, including low-SNR and 18.4 MHz records. Dataset access reached a JavaScript/robot-verification page; file contents, format and license were not inspected. This is a concrete acquisition candidate, not verified usable IQ or a ready holdout |
| [DRDC/RDDC 2018 sounding report](https://publications.gc.ca/collections/collection_2019/rddc-drdc/D68-2-122-2018-eng.pdf), full report, sections 2.2–3 | Useful high-latitude estimator, instrument-limit and local-stationarity evidence. Use its actual path and censoring; do not convert a sounder ceiling into an environmental upper bound |
| [Nissen et al. 2003](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2002RS002746), full publisher text | Useful low-frequency long-path spread evidence, but selected single-mode records exclude multimode cases. Preserve that selection when comparing distributions |
| [Yang et al. 2019](https://doi.org/10.1155/2019/3940837), [full article copy](https://pdfs.semanticscholar.org/e85f/02aa867290416a7c6d972882a7dc556cf947.pdf), pp. 1–9 | Chapter 41's snippet-only status at audit start is now corrected, retaining earlier access history and adding supporting section locators. Its regional passive radiometry at 3, 7.8, 19.35 and 37 GHz includes calibration-bias/topography/seasonal concerns; it supplies neither a disc-integrated 5.76 GHz antenna temperature nor a coherent radar scattering kernel |
| [Fassett et al. 2025, LPSC 2129](https://www.hou.usra.edu/meetings/lpsc2025/pdf/2129.pdf), full conference abstract | Local X/C-band bistatic SAR constraints support caution about incidence and phase dependence. Full inspection of an abstract does not make it a full journal paper or a whole-disc EME dataset |
| [Valčić et al. 2026](https://hrcak.srce.hr/en/345745), full NAVTEX article, sections 2–4 | Relevant weak/failed 490/518 kHz reception observations partially address G04. Receiver-display power and assumed ground/transmitter inputs do not establish calibrated 1 MHz transfer or maximum range; investigate the anomaly without adopting the authors' model-failure interpretation as established |
| [Rogers et al. 2016 proceedings lead](https://sites.nationalacademies.org/cs/groups/pgasite/documents/webpage/pga_183636.pdf), proceedings abstract entry pp. 155–156 | Describes 30–100 MHz NLOS terrain measurements and antenna/soil/topographic inputs. Quantitative data are not supplied by this abstract; retrieve the associated paper/data before parameter use |
| [25 MHz radar range study, 2017](https://journals.ametsoc.org/view/journals/atot/34/2/jtech-d-16-0139.1.xml), DOI 10.1175/JTECH-D-16-0139.1 | Search/index material identified a near-surface conductivity/sea-state/tide study; publisher open returned HTTP 403. Full methods/results remain uninspected in this audit. Radar working range is not one-way communications path loss |
| [Haralambous et al. 2022, HF noise over Cyprus](https://www.mdpi.com/2072-4292/14/23/6040), full HTML | Confirms an actual source-access remedy and method-specific noise evidence in chapter 50. Separate it from the related [congestion paper](https://www.mdpi.com/2076-3417/12/22/11808); these are different observables |
| [ITU-R P.372-18 official record](https://www.itu.int/rec/R-REC-P.372-18-202609-I), edition/status page | Confirms the September 2026 edition. This audit's metadata check is not a new reading of all normative equations/tables; retain earlier passage-level receipts where present and resolve differing access records |
| [NTIA TR 25-579](https://its.ntia.gov/publications/details?pub=4563), official September 2025 record and abstract | Abstract supports scope, sites and broad comparison only. Earlier large-PDF retrieval failed; no new full-report inspection is claimed |
| [Hicks and Achatz, 2026 USNC-URSI paper](https://usncursi.org/archive/nrsm/2026/papers/1203.pdf), full two-page text, sections III–IV | Gives a partial alternative primary route: active-rod measurement chain and minute sweeps; 1,071 of 1,443 MGS captures were excluded for compression. Treat this as selection/censoring evidence, not ambient-tail calibration. The paper has inconsistent site naming in its summary; resolve identity before reuse. It does not replace TR 25-579 |

The NTIA short paper's text was available, but a web screenshot attempt returned
a cache miss; no visual verification of its table values is claimed. The
compression count above is stated in the prose. No data downloads, waveform
analysis, receiver tests or site calibration were performed in this audit.

The parent audit used exact queries `site.its.ntia.gov "TR-25-579"`,
`site.itu.int "P.372-18" "2026"`, and `"McMurdo" "radio noise" NTIA 2025`,
followed by the official records and the short-paper PDF. Domain checks also
used targeted title/DOI and source opens; their returned source identities and
inspection levels are above. Complete query/result logs for those checks were
not retained, so this table is source-check provenance rather than a
reproducible systematic-search log. The active plan requires such logs for the
next coverage pass and does not invent historical queries.

## What must be true before calling the old gaps remedied

1. Every N1–N7 notice has a dated current disposition, mapping each of its
   named sources and unsearched topics to a supporting passage, corrected
   claim, or explicit unresolved consequence. This includes G01–G17 and any
   further obligations discovered during the full chapter inventory.
2. Newly available full texts are reconciled with their owning chapters and
   all dependent scenario assertions. A source-status update alone is not
   quantitative extraction or model validation.
3. Required source gaps have an inspected primary route or an honestly weaker
   research branch. If a required observed family remains unsupported, keep
   that family gate open; a synthetic case cannot satisfy it by relabeling.
4. Selected scenario definitions contain numerical laws and inputs, including
   uncertainty/dependence branches, instead of instructions that an implementer
   must still research. Named missing datasets remain external prerequisites
   for empirical replay or validation.
5. The coverage matrix, contradiction review and independent source audit
   satisfy the completion plan's gates. Formatting checks, citation counts,
   exhausted web resources and reviewer agreement without evidence cannot
   substitute for those gates.

This audit and plan can be complete while the research remains in progress.
That distinction is the applicable status now.

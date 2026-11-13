# Research execution, conclusions and remaining qualification

Updated 13 November 2026. This record replaces the unexecuted plan at
`280b32e47af6f2066fcc44a8966d6af0b220d706` with the work actually performed.
The independent all-family review concluded **ready to conclude** after the
active analyses and consequential corrections were completed. The
[200-item evidence audit](87-evidence-remediation-audit.md) retains the original
obligations and their current dispositions; the owning chapters contain source
locators, inputs, equations, results and scientific limits.

The task is research for sub-9 kHz with a 200 ft antenna, groundwave at 1 MHz and
30 MHz, skywave at 1–30 MHz, and Earth–Moon–Earth. It includes measured,
radar-derived, communication and theoretically contemplated conditions from
moderate through severe and outage. There is no universal natural worst case
without a specified environment, observable, bandwidth, integration time and
conditioning population. Deliberate stress controls remain separately labeled.

All repository edits remain in this Fable-5.2 directory. Reference calculations,
public-data analysis and official propagation-code runs were performed in
temporary workspaces. No simulator integration, modem trial, receiver capacity
benchmark, hardware measurement, author contact or paid service was performed.

## 1. Execution against the twelve research work packages

| Package | Result and evidence | Limit of the result |
| --- | --- | --- |
| WP00 — status, claim and source reconciliation | Source/parameter/consumer maps and individual historical-source dispositions now accompany chapters 10–60 and scenarios 81–85. Corrections reach summaries, tables and dependent calculations. Chapter 70 compares Astra-6 and the existing appraisal protocol read-only. | A bibliography entry is not a full-text receipt; inspected passages and contextual unread sources remain distinct. |
| WP01 — literature and contradictions | Primary methods, tables and contrary findings were added across all families, including relevant 2025–2026 observations and models. Actual queries/read scopes are recorded in the owning chapters. | No assertion that every publication, language or database has been searched. Inaccessible bodies cannot supply unique selected numerical parameters. |
| WP02 — existing public data | Gu figure/MAT products, the NTIA Phase 1 archive, an AM night archive, QAN/OTT received-burst data, HF event figures, public EME I/Q, lunar radar products and noise products were inspected or analyzed using their actual observables. | Processed summaries, relative PSD, detected bursts and local radar images do not become calibrated continuous complex channel records. |
| WP03 — independent calculations | Chapter 90 reproduces groundwave fields, ranges, sensitivities and lunar integrals. Chapters 30/50 run pinned official P.533/P.372 code; 81 checks the antenna circuit and selected LMP fields; 84 executes retarded lunar geometry and beam/noise calculations. | Numerical agreement verifies the stated equations and inputs, not environmental accuracy or universal mode completeness. Original failed checks remain visible. |
| WP04 — sub-9 kHz and antenna | Chapters 10–12/81 separate antenna geometries, matching and ground losses, magnetic/electric sensors, waveguide models, transitions, atmospheric noise and constructed severe cases. Public-data and circuit/model checks replace several previously unexecuted proposals. | Actual installation efficiency, sensor calibration and sub-hertz joint noise/phase distributions require matched measurements. |
| WP05 — groundwave | Chapters 20/25/82 add censor-aware 20/50 MHz measurements, coastal and equivalent-distance comparisons, reference-plane corrections, rough-sea alternatives and a mechanism/severity/time map. | Nearby-frequency measured loss does not establish pure surface-wave propagation, exact 30 MHz behavior or a long-duration phase law. |
| WP06 — HF skywave | Chapters 30–33/83 reconcile official loss predictions, absorption and event observations, measured width definitions, public sounding data and stationary/nonstationary model alternatives. | Detected-only records and uncertain operating schedules do not identify unconditional communication availability. Standard channel controls are not estimated natural joint distributions. |
| WP07 — EME | Chapters 40/41/84 reproduce a relative lunar self-echo and a local polarization observable, execute pinned ephemeris/orientation inputs and check coupled delay/Doppler, beam and noise models. | Neither product calibrates a whole-disc, two-station 5.76 GHz channel. Assumed scattering and station quantities remain explicit. |
| WP08 — noise and equipment | Chapters 50/55/85 reconcile source bandwidths, APD/PSD estimators, marginal noise combination, directional noise, receiver effects and RF-to-ADC accounting. | Actual device transfer, recovery, close-in phase noise and joint environmental statistics require equipment/site records. |
| WP09 — numerical and statistical design | Chapters 60/86 give finite covariance/generator references, bounded convergence checks and a 12+6 diagnostic pilot distinct from an attainable conditional probability confirmation. | Reference mathematics is not native generator verification or a modem reliability result. |
| WP10 — implementation feasibility | Chapter 70 binds the proposal to canonical native source/emission/receiver interfaces, public geometry, continuous state, resource admission and the existing appraisal design. | Implementation and actual-device qualification remain later work within their own authorized scope. |
| WP11 — independent review and documentation | Independent source and numerical review, central reconciliation and local documentation checks are recorded below. | Review is scoped to decision-driving research and the documented obligations; it is not certification of every historical sentence. |

## 2. Consequential findings and corrections

### Shared reference planes

[00](00-method-and-modem-context.md#311-grounded-monopole-reference-plane-check)
and [90](90-cross-check-calculations.md) resolve an apparent 6 dB discrepancy.
The ground-level total vertical electric field and a free-space incoming plane
wave use different receive-area conventions. The short-monopole conversion
`C/N0 = P_emrp − L_b − F_a + 177.5` remains correct to ITU rounding at its
specified reference planes. Applying gain 3 to an already enhanced total ground
field would count the enhancement twice. Arbitrary antenna patterns, magnetic
sensors, external directional noise and receiver noise do not share that simple
conversion. Passive loss and its thermal noise are charged once.

P.533 uses its own received-power convention; its per-mode basic loss is not
silently substituted for terminal loss. P.1147's loop observable is likewise
not automatically a P.368/P.372 vertical electric field. The model comparisons
now retain these distinctions next to the affected conclusions.

### Sub-9 kHz, 61 m antenna and noise

[11](11-sub9khz-antennas-links-and-practice.md) distinguishes a selected
61 m top-loaded reference from bare vertical, umbrella, lofted, ground-wire,
electrode and magnetic-loop alternatives. Antenna capacitance/effective height,
matching, loss, bandwidth and detuning are conditional calculations. They are
not measurements of an unspecified “200 ft antenna.”

The Gu version-1 archive was fully extracted and its aggregation reproduced.
Its minute series is centered at **28 kHz**, so the million-plus finite values
cannot calibrate an 8 kHz temporal law. The 4/6/8 kHz products are monthly
summaries. The apparent 6 kHz noise/lightning correlation is strongly sensitive
to shared seasonality: the documented paired, month-demeaned check reduces the
correlation from about 0.89 to about 0.03. This supplies a concrete reason not
to infer a stochastic coupling law from the unadjusted plot.

The driven RLC calculations distinguish integration convergence from source
reconstruction. Linear interpolation of a 48 kHz sampled drive causes about
**0.857 dB** attenuation at 8.27 kHz; reducing circuit substeps cannot remove it.
[81](81-scenarios-sub9.md) records 84 reference solves and the independent
counterchecks. A deep-mode intermediate Hankel overflow is avoided by an
independently derived, algebraically equivalent ground-level residue evaluation;
all modes remain included. A separate 1 kHz coarse-mesh result was 6.02 dB too
large because two numerical estimates of the same simple root survived a
rounding-based duplicate filter. Newton convergence, a contour count and the
documented mesh diagnose that failure. Both original failures remain visible;
these remedies do not qualify the whole library or elevated/magnetic fields.

Selected profile and time holdouts pass. Two initial full-guard frequency
interpolations failed the 1% criterion and passed after local bisection without
a relaxed threshold. Guard-edge variation reaches about 1.065 dB and 6.708°;
the very narrow carrier-only control does not qualify the complete emitted
waveform. The declared guard is not compact physical spectral support.
The separate ground-wire factor-of-two question is resolved for the stipulated
uniform-current, electrically short, good-conductor limit through primary
Moore–Blair equations and a reciprocal-field derivation. Finite-length and
installed-current corrections prevent multiplying a practical efficiency claim
by four without further evidence.

### Groundwave at 1 and 30 MHz

[90](90-cross-check-calculations.md) independently checks **144 field/loss
pairs, 216 threshold distances, 21 effective-radius cells, 18 complex
conductivity pairs, 45 height cells and 44 downstream noise-dependent ranges**.
The earlier plan's count of 24 downstream combinations was wrong. All 95
pinned LFMF test vectors reproduce their expected valid/invalid outcomes;
valid rounded results differ by at most 0.047503 dB. A separate complex
Airy-root calculation addresses phase; the scalar library API alone does not.

The actual NTIA Phase 1 archive and Parts II/III distinguish censor codes,
missing/equipment-failure records, gain corrections and site selection. Their
20/50 MHz results supply a measured loss comparison with explicit limitations,
not a measured 30 MHz surface-wave coefficient. A complete AM nightly archive
contains relative PSD products, not I/Q. The 1.242 MHz coastal measurement and
same-input Millington/equivalent-distance controls give more specific mixed-path
evidence than a generic conductivity class.

[82](82-scenarios-groundwave.md) separates surface and space wave, coast,
layer/height/terrain uncertainty, rough-sea mean loss, coexisting skywave and
other arrivals. Where observations cannot identify a temporal law, coherent,
time-varying and outage controls are alternative hypotheses without assigned
natural occurrence probabilities.

### Skywave at 1–30 MHz

The official P.533 calculation now explains the former approximately 11 dB
Kranji–Beijing disagreement. The published example uses a one-based input-hour
convention; clock interpretation and differences in geometry, magnetic,
ionospheric and absorption terms account for the discrepancy. The reproduced
native basic loss is **161.670457 dB**; the actual 02 UTC case is
**170.340365 dB**. [30](30-skywave-signal-level-and-outages.md) replaces the
old tables with **1,320 native cells**, explicit frequency-domain limits and
terminal received-power normalization. The out-of-domain 1.8 MHz P.533 columns
are withdrawn rather than padded with an arbitrary error margin.

A separate normative ambiguity remains: the printed P.533 absorption expression
sums penetration-point contributions, while the inspected native implementation
averages those points before multiplying by hop count. Under matched inputs
these are distinct calculations. Both branches are retained; reproducing the
reference code does not prove that it resolves the printed recommendation's
physical intent. The inherited P.1147 Table 3 rounding error (151 rather than
150 dB for the specified 40°/10,000 km cell) was also corrected and its affected
scalar tables and budgets rerun.

Recent ionosonde, GNSS, passive radio and storm studies strengthen the evidence
for severe event scenarios while also exposing transfer limits. An ionogram
blackout, fmin rise, noise decrease, TEC change, decoded WSPR path and wanted
communication-link attenuation are different observables. Examples include
non-monotonic responses across flare classes, solar radio-burst contamination,
station-specific disturbance durations and contradictory directional noise
mechanisms. These do not establish a universal flare-to-link-loss conversion.

[31](31-skywave-dispersion-midlatitude-nvis-longhaul.md) and
[32](32-skywave-dispersion-highlatitude-equatorial.md) retain exact delay and
Doppler width definitions, resolution, selection, aliasing and censoring.
Absolute time of flight is not delay spread. A reported Doppler shift is not a
Doppler-spectrum width. The public QAN archive has **5,588 MAT files and
1,597,429 records**; a separate scientific reader agrees exactly on all arrays
of the selected twelve-file cross-check, including the nine files rejected by
the first custom reader. The initial rejection was a reader limitation, not
proof of archive corruption. Dataset calibration, field definitions and
operating schedules still bound which statistical interpretations are valid.
The attempted published QAN direction-fraction reproduction does not match:
for example, about 62.58% versus the reported 65.6% at 11.1 MHz under the
explicit provisional gate. No gate was tuned to force agreement. Public
metadata and all inspected ZIP members contain no producer field dictionary.

The distinct Ottawa–Alert archive gives 62.3786% or 64.3851% reconstructed
unreceived pulses under two explicit operation-day assumptions. Their difference
is denominator sensitivity, not a confidence interval. An actual eight-hour
5 August 2017 join supplies 33 received pulses out of 64 assumed opportunities
and four remote-riometer comparisons, including one-minute alignment sensitivity.
It is a descriptive selected event, not causal attribution or an independent
population estimate. Three event-figure readings also received an independent
visual check; missing black traces remain unresolved observations, not zero RF.

The Java NVIS public products supply actual low-latitude received I/Q, correcting
a broader earlier absence claim. All **1,938,428** little-endian complex-float32
samples in the 15.5 MB raw product exactly match the companion MATLAB signal
and its 220 ksample/s time grid: about **8.811 s** of received waveform. Published
weekly width summaries do not reproduce under the explicitly documented
straightforward estimators; an imaginary stored RMS value, manual selection and
missing producer processing details remain visible. The raw product is useful
without being an absolutely calibrated long-duration complex channel or an
already implemented modem replay.

[33](33-hf-channel-models-and-simulators.md) compares locally stationary
Watterson/OU controls, dynamic parameter models, coupled scattering, physical
ray/ionosphere models and measured replay. New 2026 models are individually
identified; unavailable bodies remain abstract-level leads. Complexity or
self-consistency alone is not empirical validation, and better implementations
may still be found.

### Earth–Moon–Earth

The downloaded 433.8 MHz SigMF recording gives a relative lunar echo delay of
**2.43632 s** and echo-minus-direct frequency **−253 Hz**, with split/window
checks and displaced null controls. Unsynchronized absolute clocks and absent
power calibration prevent absolute C/N0 or a communication availability claim.
The 2026 Mini-RF products reproduce local CPR on **548,573 valid pixels** to a
maximum numerical difference of **6.62×10⁻⁸**. This is an orbital/local
observable, not a full-disc Earth-to-Earth transfer function.

[84](84-scenarios-eme.md) executes five exact NAIF inputs, retarded geometry,
frame checks and visibility for **2025-01-03 17:02:32 UTC**. The center diagnostic has two-leg delay **2.4836116705 s** and analytic Doppler
**+2,919.428095 Hz**. The actual waveform uses the minimum-surface reference,
with delay **2.4720213723 s** and local-interpolant Doppler
**+2,919.42362 Hz**. These distinct references require explicit tracking
assistance or receiver bounds; the earlier finite-difference value is withdrawn.
The actual assumed 3 m station budget is **26.855 dB-Hz**. Beam/noise calculations
cover 1–100 m dishes and retain coupled delay/Doppler and polarization limits.

Independent quadrature checks all fourteen lunar laws and corrects several
width-dependent conclusions. A 25 m dish's half-power delay is about 0.1 ms,
not 0.5 ms. A “6 Hz width” implies different dish sizes for FWHM, twice-RMS or
90%-power width. Surface/beam normalization and absolute-time differentiation
errors found during execution are documented with their corrected methods.
The original failures are not replaced by unexplained final numbers.

### Noise, equipment, numerical methods and inference

Pinned P.372 code reproduction and literal addition of positive random powers
are now separate branches. At the selected quiet-rural 30 MHz point the native
combined median is **17.916115 dB**, below the Galactic median
**18.026211 dB**; it cannot be the median of a pointwise nonnegative power sum.
An independently integrated sum under the stated marginal/dependence assumptions
gives **19.45085 dB**. Neither marginal calculation identifies a temporal
noise waveform. The original 1962 NBS work includes **0.64 Hz APDs**, correcting
a broad claim that no near-sub-hertz measurements existed; it still does not
provide continuous joint phase/arrival data for the selected link.

[55](55-equipment-and-front-end-effects.md) and
[85](85-scenarios-noise-and-equipment.md) specify filters, gain, nonlinear
recovery, sampling clocks, oscillators and RF-to-ADC noise in explicit planes.
Standards, manufacturer specifications and particular published devices are
not calibration of the user's station. The official 2024 MIL-STD-188-110D
Change 1 Appendix E was retrieved and compared with 2017; the inspected clauses
are substantively unchanged. This is a source comparison, not compliance testing.

The finite calculations in [86](86-statistics-and-calculation-checks.md) include
Gaussian/OU covariance, shared/gated states, truncated spectra and maximum-null
statistics. A whole-filter C4 comparison **failed** its original 10⁻⁶ MSE
criterion at **2.2853×10⁻⁴**. Freezing the physical filter and checking only its
interpolation answers a different, valid question; the bounded interpolation
check passes, without erasing the original failure.

The existing appraisal's **12 wanted + 6 negative/control** pilot is a
failure-finding design. A separate predeclared conditional H1 confirmation at
20 dB-Hz uses **n=41**, pass at least 38, fail at most 27, otherwise inconclusive,
with exact 85.26% power at p=0.95 against p=0.8 under its stated independent-trial
assumptions. It is not an estimate of environmental prevalence. The former
195-episode catalog is not a required extra campaign; shared noise/equipment
coverage and multiplicity are explicit in the reconciled design.

## 3. What remains a measurement or access dependency

These are scientific limitations of the available evidence, not hidden claims
that the proposed software is already validated. The owning chapters identify
which accessible analysis was completed before assigning the dependency.

| Evidence needed | Why the current sources do not supply it | Consequence and concrete next input |
| --- | --- | --- |
| Actual 61 m sub-9 kHz installation and receiver | Published systems differ in geometry, matching, ground and sensor; selected monthly/noise products lack the required waveform and calibration. | Obtain complex antenna impedance/current/voltage, vector-field/sensor transfer, noise/overload and environmental records for a declared installation before an absolute range or natural coherence claim. |
| Long coherent 1 MHz / 30 MHz ground-dominant channels | Retrieved products are loss samples, relative PSD, adjacent-band measurements or radar geometries, with missing phase or competing propagation. | Matched, calibrated complex records with geometry, frequency, operations and ground/sea/weather metadata are needed for temporal calibration. |
| QAN/OTT field dictionary and operations | Public received-burst summaries do not by themselves define every stored variable, absolute clock reference or attempted-pulse history. | Obtain producer schema/calibration and operation logs before treating ambiguous fields as absolute power/time or missed receptions as propagation failure. Use the documented conditional statistics meanwhile. |
| Fine-spacing joint HF covariance and severe-event occurrence | Individual widths and selected events do not identify joint path/mode/frequency laws or population rates. | Matched coherent sounding with noise, nondetections, durations and independent geometry/time holdouts is required for those claims. |
| Whole-disc 5.76 GHz EME and actual stations | Different-band self-echo and local orbital radar cannot supply the selected bistatic scattering, polarization, equipment and atmosphere jointly. | Calibrated two-station complex observations, noise/pointing/clock records and attempted-operation schedules are needed for empirical C-band qualification. |
| Target equipment and narrowband noise populations | Device-specific transfer/recovery, calibration and close-in phase noise are missing; many records are triggered, summarized, filtered or censored. | Acquire synchronized calibrated wanted/noise/blocker and hardware state records with prehistory, clipping and recovery. |
| Specific inaccessible primary bodies | Ordinary lawful primary routes yielded metadata/abstracts or actual access restrictions for some named sources. | They remain named and unused as unique numerical authority. Revisit when a lawful full copy becomes available; do not infer their uninspected tables. |

No author-contact, purchase or new physical measurement is implied by the
current authorization. A new useful public product can update these conclusions;
absence from the inspected corpus is not proof that such evidence cannot exist.

## 4. Later implementation and qualification sequence

The research supplies model choices and reproducible reference targets. Future
experiment code/test documentation belongs under the already specified
`_appraisal/highFidelity-triple_Robust_Modem` boundary; it is not created here.
[70](70-implementation-options.md) describes the contained native route and
reuses the existing appraisal schema and canonical compiled modem.

1. Implement one admitted source/channel/equipment case at a time, preserving
   actual Helper, DATA, quiet tails, clocks, reference planes and delayed energy.
   Retain explicit Off and existing synthetic baselines.
2. Verify normalization, covariance, phase, interpolation, continuous state,
   chunk independence and tail handling against the references. Failed or
   inapplicable checks prevent the corresponding qualification claim.
3. Reproduce source observables with their actual estimators, censoring and
   geometry. Keep a source reproduction separate from target-domain validation.
4. Run the bounded diagnostic pilot, including wanted, no-signal, wrong-address,
   interruption, reacquisition and physical-completion controls. Public receiver
   hypotheses remain unchanged; evaluator truth must not aid acquisition.
5. Run a predeclared independent confirmation only for a selected claim and
   admitted cost. Do not pool different physical conditions as iid trials or
   reuse timing replicas as independent communication outcomes.
6. Measure complete same-stream receiver capacity separately, using current
   native hardware/allocation contracts, explicit memory bounds and `W=2*H`.
   A compatible build, fallback, narrow stage speed or reference solver runtime
   is not an actual-device complete-receiver measurement.

No detailed renderer or GPU purchase is selected merely because it is more
complex. Prefer the least expensive representation that preserves the required
observable, keep a materially different countermodel, and revisit choices when
new evidence changes the accuracy/cost tradeoff.

## 5. Reproducibility, retention and review

The source baseline and unchanged production trees are recorded in00/70.
Owned chapters retain exact source editions and commits, downloaded-product
hashes where verified, schemas, selected inputs, equations, numerical outputs,
original failures and the distinction between an independent derivation and a
second evaluation of the same equation. Embedded recipes provide durable
reproduction without committing papers, raw archives, third-party binaries or
a second modem implementation.

Temporary data/calculations serve the current independent review. Once that
purpose is complete and the durable receipt suffices, remove this task's
redundant downloads and scratch outputs. Preserve evidence for any unresolved
numerical failure until its question is resolved. Do not purge unrelated or
another contributor's temporary material. Source URLs and the documented
product/version/hash identify reacquisition when needed.

**Final review, 13 November 2026:** ready to conclude. The independent review
examined the original research objective, selected-source support, actual
public-data/reference results, failed checks, remaining feasible actions and
source-to-consumer corrections. Its required follow-through included the LMP
failure diagnoses, Java public-I/Q inspection, independent event-figure reading,
Wheeler summary correction, recovered-script history, EME reference distinction
and final audit integration. Those actions are complete. The review found no
remaining broad research campaign necessary before concluding this request;
missing matched calibration, producer/operation records, unused access-limited
bodies and later native qualification remain explicit scientific boundaries.

**Documentation validation:** all 26 Markdown documents checked; 437 local
file/anchor links, 296 Markdown tables, balanced fences and all 200 expected
unique audit IDs pass. `git diff --check` passes. All 26 changed repository
files are inside `_ref/highFidelity-triple_Robust_Modem/Fable-5.2`; production
source, maintained guides and the appraisal directory are unchanged. These
checks validate documentation structure and scope, not receiver performance.

The identified task-owned reference workspaces, public-data copies, runtimes,
figures and temporary calculation outputs were removed after review. Durable
source/version/hash receipts, equations, selected inputs, results and failed
checks remain in the chapters. Unrelated pre-existing scratch material was
left untouched. The final documentation/scope checks were repeated after this
housekeeping.

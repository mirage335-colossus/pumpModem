# HF skywave channel evidence for triple Robust Modem

## Purpose and scope

This chapter gathers the physical and measurement evidence needed to design
high-fidelity 1–30 MHz HF skywave experiments for the native triple Robust
Modem. It is a research map, not an implementation specification, sensitivity
claim, or performance qualification. The central engineering consequence is
that a single “HF channel” setting cannot stand for the worldwide ionosphere:
the received waveform depends on frequency, path geometry, hop/layer selection,
time, solar illumination, geomagnetic latitude and activity, and local
interference. A useful modem test must state which of those conditions it
represents.

The evidence supports retaining the stationary Gaussian-scatter (Watterson)
channel as a reproducible baseline and interoperability reference. It also
supports separate, conditional test families for drift, changing multipath,
high-latitude irregularities, equatorial spread-F, E-layer/magneto-ionic
structure, and absorption or path loss. These families are not interchangeable.
In particular, a receiver-channel impairment trial presumes that enough wanted
energy arrives to decode. A solar-flare or proton-event blackout is instead a
link-availability/outage trial; making a fading process deeper does not make it
a physically faithful absorption experiment.

HF denotes 3–30 MHz; the lower 1–3 MHz portion of this task is MF/HF boundary
territory, although ionospheric skywave and the cited models may extend there.
ITU-R P.533-14 is currently the in-force HF circuit prediction recommendation
(2–30 MHz); its limits and purpose should be kept explicit. ITU-R P.531 addresses
trans-ionospheric satellite links, not ordinary ground-to-ground HF skywave.
Its list of Faraday rotation, group delay/phase advance, scintillation,
refraction and Doppler is useful physical context, but it should not be imported
as a directly calibrated skywave channel law.

## What the standard channel does and does not say

Watterson, Juroshek and Bensema’s 1970 model represents delayed modes with
randomly varying complex gains. F.1487 defines stationary Gaussian-scatter tap
processes, optionally with two magneto-ionic components [HF01, HF02].
Stationarity fixes statistics, not each realization. Keep common frequency
shift, stochastic spectral width, and changing path powers as separate controls.

F.1487 is a modem-comparison recommendation: its suggested grid spans
differential delays of 0–20 ms and Doppler spreads of 0.1–40 Hz. These are test
axes, not a climatological joint distribution. Its general tap law can
represent two O/X spectra with negligible differential delay; resolvable O/X
delays require distinct components [HF02].

ITU-R P.533 [HF03] answers a different question: it predicts monthly-median skywave
field strength, available frequencies, circuit reliability, time/frequency
spreads, and path modes using climatological inputs. It selects candidate E/F2
and multihop modes and accounts for path absorption and above-MUF loss. It is
valuable for constructing plausible path and link-budget strata, but a monthly
median prediction does not supply an instantaneous channel impulse response,
fade-duration distribution, disturbed-auroral dynamics, or modem-level
success probability. The trans-ionospheric scintillation and TEC methods in
P.531 [HF04] are not substitutes for that missing skywave channel response. Likewise,
ionosonde climatology constrains layer critical frequencies and virtual heights
at particular locations and sampling times; a vertical ionogram does not
uniquely determine a long oblique path’s instantaneous multipath, polarization,
or scattering function.

## Measurement vocabulary: shifts, spreads, and delays

Source values below must retain the estimator and observation window that
produced them. They are not all the same measurement.

| Quantity | Interpretation and units | Report separately |
|---|---|---|
| Differential path delay | Relative arrival time between resolvable modes/taps; ms or µs. It is not total path time-of-flight. | Tap/mode delays, delay resolution, sounding bandwidth and threshold. |
| RMS delay spread | Power-weighted second central moment of an estimated delay profile; time units. | Definition, included energy/noise threshold, path ensemble and window. “RMS 2σ” is a particular convention, not automatically a maximum delay. |
| Doppler center/shift | Spectral centroid or a tracked common frequency offset; Hz, with sign convention. May include coherent phase-path motion and transmitter/receiver oscillator error. | Per-mode center, common drift, oscillator components and any detrending. |
| Doppler spread | Width of a time-varying path’s Doppler spectrum; Hz. A Gaussian component’s 2σ width is one model parameterization. | RMS 2σ, full observed support, percentile, estimator bandwidth/window, and whether values refer to individual taps or a composite. |
| Maximum observed spread | Largest estimate in a finite sample or event, not a population quantile or hard physical bound. | Sample count, duration, selection rule, instrumentation limits and geomagnetic context. |
| Frequency/time selectivity | Response variation across the occupied signal bandwidth and over the observation interval. | Occupied bandwidth, channel coherence estimator and burst duration; don’t infer a universal coherence time from Doppler support alone. |
| Absorption / availability | Path loss or failure to support a mode, often varying with frequency and illumination. | dB path loss or measured received SNR/power, mode/frequency availability, outage duration and path. This is not Doppler spread. |

As a concrete warning against unlabeled maxima, Nissen et al. [HF06] analyzed 1 MHz
bandwidth sounding over a 3,100 km Sondrestrom–Bedford high-latitude path in
1992. They report RMS (2σ) delay and Doppler spreads, shifts and spread factors;
their largest reported RMS Doppler spread is 16.8 Hz, the 90th percentile is
13 Hz, and the 10th percentile 1.75 Hz. Their RMS delay spread is reported as
up to 780 µs, with 400 µs at the 90th percentile and 17 µs at the 10th. The
ionosphere was mostly mildly disturbed, with solar flares on three dates. The
authors removed measurements with significant contributions from multiple
propagation modes, leaving about 100 probes; these results describe selected
individual-mode responses, not the complete multimode channel [HF06]. They are
statistics from that route, dates, waveform and estimator—not a universal
“worst HF” bound. A separate polar-cap link study [HF08] found
spread estimates up to 65 Hz during elevated geomagnetic activity, but this is
again the maximum reported for its measured link and processing, not a
population percentile or a prescribed modem setting.

## Why stationarity sometimes works and sometimes fails

Stationary Watterson statistics [HF01, HF02] are a good controlled approximation when the
experiment is short enough, the selected propagation mode remains present,
path geometry and mean conditions change slowly over the scored interval, and
the question concerns fading/multipath around a stable statistical regime.
They are particularly useful for paired modem comparisons: fixed seeds or
matched channel realizations can isolate the receiver change while preserving a
repeatable signal/noise control. The model can also include stochastic gains
without claiming that the ionosphere is static.

The approximation becomes weaker when the observation spans sunrise/sunset,
moving layer profiles, abrupt mode appearance/disappearance, strong horizontal
gradients, or a transition between quiet and disturbed conditions. Such
conditions can change common Doppler drift, mode delays, path powers and
available rays together. A single stationary spectrum may capture a local
segment but not the full event or transition. Ponomarenko et al.’s high-latitude SuperDARN climatology [HF09] emphasizes the forecasting difficulty created by
intrinsic polar/auroral variability. Cameron et al.’s 2.5-year polar-cap link [HF08]
record shows real off-great-circle (OGC) propagation associated with polar-cap
patches and arcs. Across the most consistent year (April 2013–April 2014),
65.6% of detected 11.1 MHz signals had >30° deflection; this is a fraction of
received signals passing the study’s SNR >0 dB screen, not availability or a
fraction of all transmissions. The statistic excludes signals absorbed,
escaped, or deflected away from the receiver. OGC receptions also had increased
time-of-flight and Doppler spread and lower SNR. The full campaign covered 855
operating days from 2012–2016. The paper links its [unfiltered QAN–ALE data at
Harvard Dataverse](https://doi.org/10.7910/DVN/RGDNR1); this is an actionable
data lead, not a claim that the deposit contains raw IQ. These observations
remain specific to that link and selection procedure.

Equatorial spread-F is a different regime, driven by F-region electron-density
irregularities whose effects depend on whether the radio path intersects the
irregularity structure. Olugbon et al. [HF10] report two rare daytime ESF-like events
in 2019 over Nigeria, each within an approximately two-hour interval. Their HF
Doppler trace showed nonuniform broadening; a roughly 1 Hz E-layer Doppler
component coexisted with more severe spread-F intervals, and the nearby
digisonde did not sample exactly the same reflection point. This cautions
against treating a site’s ionogram as a simultaneous direct measurement of the
oblique data path. The familiar postsunset equatorial ESF is not the only
condition, and one latitude’s event record cannot give a global occurrence
rate.

TIDs can produce structured, correlated Doppler motion rather than only
zero-mean broadening. In a 2026 10 MHz midlatitude link analysis [HF13], Fernandes et
al. report daytime Doppler residuals fitted by Cauchy statistics, while
nighttime residuals were described by a mixture of exponential and log-normal
statistics. Ray tracing in IRI-2020 was used to interpret which ionospheric
altitudes contribute. These residuals are a link-level observable after its
measurement processing; they are not direct distributions of Watterson tap
gains and should not be substituted for a per-path Doppler spectrum. Aricoche,
Hysell and Morton’s 2026 Alaskan beacon work [HF14] similarly derives scattering/range
and frequency-spreading implications from a structured high-latitude model,
while explicitly noting that rapid fading in its data exceeded the inversion’s
ability to capture. Together these studies argue for testing structured drift
and model mismatch separately from a stationary fading law.

## Path physics beyond a tap table

Geometric-optics ray tracing through a specified electron-density and magnetic
field model can help explain candidate modes, group delays, arrival directions
and polarization. Magnetoionic O/X splitting arises because the geomagnetic
field makes the ionosphere birefringent. Components may share a nominal ray
family yet have different phase/group behavior, powers, polarization, and
resolvable delay; near a junction frequency their separation is especially
important. An NVIS study measured different O/X channel responses and evaluated
polarization diversity under its specific link conditions [HF18]. A scalar
complex-baseband tap abstraction can emulate selected
effects, but only if mode delay, relative power, phase, polarization handling
and frequency dependence are explicit. Do not claim that two taps are
physically “O and X” unless the parameters trace to a declared path model or
measurement.

Oblique sounding and actual links matter because vertical ionosondes do not
reproduce launch angle, hop length, ground reflection, oblique absorption,
horizontal gradients, or off-great-circle paths. P.533 gives a planning-level
mode selection and path loss model and can seed scenarios, while a ray tracer
such as PHaRLAP or an Appleton-Hartree implementation can examine a specified
profile and magnetic field; high-latitude terminator simulations are one
example of this geometry-specific use [HF15]. Ray tracing itself has limits: it depends on the
input profile, boundary/irregularity representation, numerical resolution and
chosen ray branches; geometrical optics does not automatically reproduce
diffuse scattering or small-scale scintillation. Keep the deterministic ray
geometry and the stochastic residual/scattering process as separate layers.

Near-vertical incidence skywave (NVIS) has short ground range and near-vertical
launch, with different multipath and layer geometry from long oblique/multihop
routes. Dear et al.’s one-week 2024 Java NVIS sounder trial [HF07], under the equatorial
ionization-anomaly crest and described as quiet conditions, measured RMS delay
values from 0.2 to 1.3 ms and Doppler shifts from 0.7 to 1.1 Hz. The paper says
more measurements are needed for a comprehensive local model. These are useful
counterexamples to importing only midlatitude long-path examples; they do not
establish a disturbed equatorial NVIS distribution. Sporadic-E reflections,
E/F layer combinations, and multihop paths can add discrete modes or alter
which ray family arrives. Their presence is path/frequency/time conditional;
for example, a 2026 polar-cap link analysis identifies sporadic-E as a specific
propagation aid on that path [HF16]. Test these as mode configurations, not by
stretching a single nominal tap profile indiscriminately. A separate 12-day
NVIS study at 5.4 MHz found O/X mean delay spreads near 0.3 ms and maxima near
2.7–2.9 ms, but its authors caution that their frequency-offset measurement
was dominated by low-stability SDR clocks rather than ionospheric Doppler
[HF18]. That is a useful example of why instrumentation error and spread
statistics need separate reporting.

Absorption outages need their own evidence. During the 28 October 2021 flare
sequence, ionosondes at Hermanus and Grahamstown continued to record F2 critical
frequencies, while E/F1 echoes disappeared for about two hours after an M2.2
flare. From the missing lower-layer echoes, the authors infer restrictions on
complex modes using E/F1, including a 1F1E double-hop; this was not a direct
measurement of outage on all communication links [HF11]. During the September
2017 solar proton events, SuperDARN-derived
attenuation [HF12] was confined to the polar cap and agreed with nearby riometer
measurements. D-region collisional absorption and E/F mode changes are related
but distinct outcomes. Depending on frequency and illuminated path, the
receiver may see reduced margin, loss of one path, a changed mode set, or no
usable skywave. The final condition is “unavailable/outage,” not a modem
channel that should be scored as if it were a weak yet decodeable waveform.

## Scenario families for controlled modem experiments

The following families are a coverage plan; numeric bounds should be populated
from a declared standard row, measurement, propagation calculation, or explicit
engineering stress. Do not invent a probability distribution from one maximum.

| Family | Evidence/intent | Minimum channel declaration | Keep separate from |
|---|---|---|---|
| Reference stationary | F.1487 [HF02] comparison, named Watterson condition, quiet single-mode control. | Tap delays/powers; per-tap 2σ Doppler spectrum; component centers; noise/interference; scored duration and seed. | Time-varying common drift or changing mode geometry. |
| Moderate midlatitude | P.533-plausible mode set plus observed/declared fading, weak common drift, and realistic noise. | Frequency, season/local time, path distance/hops, path profile/version, delay and Doppler estimator, link SNR/noise source. | A claim of representative worldwide HF performance. |
| NVIS and E-region | Short/near-vertical path; measured quiet NVIS delay/shift [HF07], with optional E/F layer or O/X branches [HF17]. | Launch/path geometry, mode IDs, relative delays/powers, polarization assumptions, regional ionogram/sounder context. | Long oblique high-latitude statistics. |
| Structured temporal variation | TID/terminator/layer-height drift with time-correlated phase or Doppler; slow path-power evolution or mode birth/death. | Deterministic trajectory and rate, stochastic residual law, change timescale, duration, and separate mean shift vs residual spread. | Stationary Watterson broadening and independent oscillator offset. |
| Adverse high latitude | Auroral/polar-cap irregularities, OGC deflection [HF08], multimode/range spread, broad Doppler; include matched quiet baseline. | Link coordinates/frequency and geomagnetic/solar context; path azimuth if known; observed spread metric/window; received SNR and availability. | A universal max-Doppler assumption or exact great-circle model. |
| Adverse equatorial | Postsunset ESF-like or measured spread-F [HF10]; also allow observed unusual daytime events as a separate cohort. | Magnetic latitude, local time, path intersection, frequency, colocated-but-noncoincident ionogram context, event duration and sounding resolution. | Generalizing one Doppler receiver record to all equatorial propagation. |
| Absorption/outage | Flare [HF11], energetic-particle/polar-cap absorption [HF12], auroral absorption or layer cutoff. | Event, sunlight/latitude/frequency, path loss or measured power/SNR trajectory, affected/unaffected modes, outage interval. | Decode-through-fade results and Gaussian noise-only tests. |
| Extreme/scientific edge | F.1487 comparative sweep limits or a published measured extreme used as a bounded stress. | Label explicitly as standards sweep, observed finite maximum, or synthetic extrapolation; state duration, estimator, and falsifier. | “Natural worst case,” tail probability, or service claim. |

The [skywave appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md#skywave-validation) specifies the matched signal and negative controls. Low SNR or severe impairment can alter admission without identifying the physical cause; repeated processing of one capture does not establish independent environmental evidence.

## Native modeling and validation options

The safest growth path is a layered, in-process native channel whose provenance
is explicit: (1) a deterministic propagation skeleton naming mode delays,
relative powers, carrier frequency, path geometry and time-varying common phase;
(2) a stochastic residual layer for per-mode complex-gain statistics and
short-term scattering; (3) separate transmitter/receiver oscillator error;
(4) frequency-dependent path loss and mode availability; and (5) receiver-side
noise/interference. This lets tests compare Watterson with measured or
physics-guided alternatives without conflating a changing delay path with
Doppler, oscillator error, absorption, or AWGN. Preserve continuous phase and
channel state across chunks and the emitted DATA plus Helper waveform; do not
reset a realization at public slot boundaries.

Concrete analytic controls, matched comparisons and mode-removal procedures are specified in the [appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md#skywave-validation). Their relevance depends on preserving the observation method and the physical regime represented by the source data.

The intended falsifiers are concrete. A Watterson-only claim is falsified for
a declared regime if measured residuals, stationarity checks, path-delay
profiles or availability transitions systematically fall outside its stated
model envelope. A ray-guided scenario is weakened if its predicted path
geometry fails simultaneous oblique-sounding/link observations. A receiver
robustness claim is weakened by reproducible weak-signal misses, wrong bits,
false admission on no-signal/wrong-address/interference controls, or failure to
complete only after scored physical quiet slots. A scenario that produces only
easy high-SNR passes does not establish a weak-signal receiver ceiling. All
these checks establish behavior under their input family only; none alone
qualifies operational range, field availability, or computational throughput.

## Evidence limits and gaps

The literature is geographically and instrumentally sparse relative to the
full 1–30 MHz range and global path/season/solar-cycle cross-product. Recent
2020–2026 literature improves visibility into polar high-latitude, low-latitude
NVIS, equatorial disturbances and long-term Doppler residuals, but does not
provide a common modem waveform, uniform estimator, all-season empirical tail
distribution, or triple-modem sensitivity curve. Scattering sounder bandwidth,
antenna geometry, ionogram cadence, noise threshold, selected path, event
selection and Doppler estimator materially change what “spread” means.

Several open questions therefore remain testable but unquantified here:
frequency-resolved multipath distributions for oblique and NVIS links during
disturbed equatorial conditions; simultaneous O/X complex channel response and
polarization at the modem antenna; event-conditioned path-loss and outage
statistics for HF communication rather than radar observables; joint delay,
Doppler, amplitude and coherence statistics during TID/auroral transitions;
and whether the actual triple modem retains DATA/Helper discriminability over
these conditions. These require measured channel captures or a fully specified
physical/stochastic synthetic model and should remain explicitly unqualified
until such evidence exists.

## Annotated primary references

Access labels describe what was inspected for this chapter: **full text** means
the paper/recommendation itself was opened; **abstract** means the primary
publisher abstract and selected metadata; **metadata** means bibliographic
records only. Source summaries are intentionally brief.

| ID | Reference and access | Evidence used / applicability |
|---|---|---|
| HF01 | Watterson, C. C., J. R. Juroshek, and W. D. Bensema (1970), “Experimental confirmation of an HF channel model,” *IEEE Transactions on Communication Technology*, 18(6), 792–803. [Bibliographic record](https://jglobal.jst.go.jp/en/detail?JGLOBAL_ID=201602006827430280). **Metadata** | Experimental source of the stationary statistical HF tap model; metadata verified, full paper not inspected. |
| HF02 | ITU-R Recommendation F.1487 (2000), “Testing of HF modems with bandwidths of up to about 12 kHz using ionospheric channel simulators.” [Official PDF](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf). **Full text** | Defines the Gaussian-scatter modem comparison model; not a world climatology. |
| HF03 | ITU-R Recommendation P.533-14 (2019), “Method for the prediction of the performance of HF circuits.” [Official PDF](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.533-14-201908-I!!PDF-E.pdf). **Full text** | Current in-force planning method; frequency/mode/median field strength/absorption/reliability. |
| HF04 | ITU-R Recommendation P.531-16 (2025), “Ionospheric propagation data and prediction methods required for the design of satellite networks and systems.” [Official summary](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.531-16-202509-I!!SUM-HTM-E.htm). **Abstract/summary** | Trans-ionospheric context only; not ground HF channel calibration. |
| HF05 | Basler, R. P. (1988), “Ionospheric distortion of HF signals,” *Radio Science*, 23(4), 569–579. [DOI 10.1029/RS023i004p00569](https://doi.org/10.1029/RS023i004p00569). **Abstract** | Disturbed polar-cap, auroral and equatorial one-hop F-layer measurements; range and Doppler distortion. |
| HF06 | Nissen, J. C., et al. (2003), “Measured channel parameters for the disturbed wide-bandwidth HF channel,” *Radio Science*, 38(4). [DOI 10.1029/2002RS002746](https://doi.org/10.1029/2002RS002746). **Full text** | Reports RMS spreads and percentiles after excluding significant multimode measurements. |
| HF07 | Dear, V. et al. (2024), “Ionospheric Channel Impulse Response Measurement System for NVIS Propagation Mode Over Java Island Based on Low-Cost SDR Platform,” *Radio Science*, 59, e2023RS007877. [DOI 10.1029/2023RS007877](https://doi.org/10.1029/2023RS007877). **Full text** | One-week low-latitude NVIS campaign; measured quiet-condition delay/shift, authors identify incomplete coverage. |
| HF08 | Cameron, T. et al. (2024), “Statistical Analysis of Off-Great Circle Radio Wave Propagation in the Polar Cap,” *Radio Science*, 59(4), e2023RS007897. [DOI 10.1029/2023RS007897](https://doi.org/10.1029/2023RS007897). **Full text** | 855 operating days; OGC statistics apply to received signals after SNR >0 dB selection. The paper links unfiltered data. |
| HF09 | Ponomarenko, P. et al. (2023), “Climatology of HF Propagation Characteristics at Very High Latitudes From SuperDARN Observations,” *Radio Science*. [DOI 10.1029/2023RS007657](https://doi.org/10.1029/2023RS007657). **Full text** | High-latitude variability and observation-based propagation climatology using 10–18 MHz SuperDARN data. |
| HF10 | Olugbon, O. et al. (2021), “Daytime Equatorial Spread F-Like Irregularities Detected by HF Doppler Receiver and Digisonde,” *Space Weather*. [DOI 10.1029/2020SW002676](https://doi.org/10.1029/2020SW002676). **Full text** | Two uncommon daytime ESF-like events; irregularity and Doppler traces are nonuniform and noncoincident in space. |
| HF11 | Habarulema, J. B. et al. (2022), “Ionospheric Response to the M- and X-Class Solar Flares of 28 October 2021 Over the African Sector,” *Space Weather*, 20(8), e2022SW003104. [DOI 10.1029/2022SW003104](https://doi.org/10.1029/2022SW003104). **Full text** | Ionosonde E/F1 echoes disappeared for ~2 h while F2 critical frequencies remained observable; communication-mode restriction is inferred. |
| HF12 | Bland, E. C. et al. (2018), “SuperDARN Radar-Derived HF Radio Attenuation During the September 2017 Solar Proton Events,” *Space Weather*. [DOI 10.1029/2018SW001916](https://doi.org/10.1029/2018SW001916). **Full text** | Polar-cap absorption observations and agreement with a nearby riometer; supports distinct outage/attenuation test. |
| HF13 | Fernandes, S. et al. (2026), “Characterizing ionospheric variability through HF Doppler measurements: a statistical and numerical ray tracing analysis,” *Frontiers in Astronomy and Space Sciences*, 13:1713968. [DOI 10.3389/fspas.2026.1713968](https://doi.org/10.3389/fspas.2026.1713968). **Full text** | Long-term 10 MHz midlatitude link Doppler residual fits vary day/night; link residuals are not tap-gain spectra. |
| HF14 | Aricoche, J. A., D. L. Hysell, and Y. J. Morton (2026), “Probing the main ionospheric trough with an Alaskan HF beacon network,” *Frontiers in Astronomy and Space Sciences*, 13:1848834. [DOI 10.3389/fspas.2026.1848834](https://doi.org/10.3389/fspas.2026.1848834). **Full text** | High-latitude irregularity-derived range/frequency spreading; rapid fading exceeds their inversion model. |
| HF15 | Cameron, T. et al. (2024), “High-Latitude Off-Great Circle Propagation Associated With the Solar Terminator,” *Radio Science*. [DOI 10.1029/2023RS007917](https://doi.org/10.1029/2023RS007917). **Full text** | 3-D E-CHAIM ray tracing; conditional sunrise/sunset path deflections and geometry dependence. |
| HF16 | Cameron, T. et al. (2026), “Ionospheric Effects on Maximum Usable Frequency for a Cross-Auroral and a Polar Cap HF Radio Link,” *Radio Science*, 61(5), e2025RS008395. [DOI 10.1029/2025RS008395](https://doi.org/10.1029/2025RS008395). **Full text** | Multi-frequency cross-auroral and polar-cap MRF/MUF observations; frequency sampling and O/X mode ambiguity limit interpretation. |
| HF17 | ITU-R Recommendation BS.1615-3 (2023), “Planning parameters for digital sound broadcasting at frequencies below 30 MHz.” [Official PDF](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1615-3-202311-I!!PDF-E.pdf). **Full text** | Connects P.533 mode delays and field strength to a predicted median time-delay profile; planning/model context. |
| HF18 | Male, J. et al. (2021), “Analysis of the Ordinary and Extraordinary Ionospheric Modes for NVIS Digital Communications Channels,” *Sensors*, 21(6), 2210. [DOI 10.3390/s21062210](https://doi.org/10.3390/s21062210). **Full text** | Measures O/X mode channel behavior for NVIS and tests polarization diversity; applicability remains specific to its link and conditions. |

## Compact takeaways

1. Preserve Watterson as a stationary *statistics* baseline; do not call it a
   fixed channel or universal atmosphere.
2. Keep Doppler center, RMS 2σ spread, observed maximum/support, structured
   drift, differential mode delay and absorption as separate fields.
3. Use P.533 and ionosondes to condition candidate modes and median links; use
   oblique channel soundings and specified ray paths for finer channel evidence.
4. Give NVIS, oblique midlatitude, equatorial spread-F, auroral/polar, O/X,
   sporadic-E/multihop and outage conditions separate scenario identities.
5. Label every result by exact waveform, channel realization, geography,
   frequency, estimator/window, noise, receiver configuration and outcome.
   No literature result here establishes native triple Robust Modem sensitivity
   or receiver throughput.

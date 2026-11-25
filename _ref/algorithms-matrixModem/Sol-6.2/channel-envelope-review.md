# HF propagation envelope for a Matrix Modem


Research date: 24 November 2026. This note identifies the physical HF channel
regimes a tone-based modem must be compared against, what the literature can
and cannot quantify, and how to construct useful reproducible channel cases.
It is a research synthesis, not a calibrated global channel model or a claim
that any receiver has passed these conditions. The emphasis is on terrestrial
3–30 MHz ionospheric skywave. Other propagation paths in the repository's
high-fidelity research remain relevant, but their measurements cannot be
silently substituted for HF skywave.

## Main conclusion

There is no single meaningful scalar called “worst HF.” Frequency, path length
and launch geometry, propagation mode, local time, season, solar illumination,
geomagnetic state, and receiver site jointly select the paths that exist and
the interference that reaches the antenna. The literature supports several
distinct adverse conditions: fast fading and frequency spreading, delayed or
changing modes, abrupt frequency/path drift, external-noise or interference
limitation, severe absorption, and outright loss of usable skywave. These
conditions can co-occur, but available papers rarely measure their joint
distribution on one communication link. Keep them as separately labelled
axes until matched field data supports a joint model.

The fixed 6.25 Hz spacing idea is not an all-regime robustness guarantee.
Narrow, long tone integration can be attractive in quiet or slowly varying
channels, but a single tone cannot distinguish wanted-signal fading from
channel nulls, and a long observation can span many fading states or a change
in mode. A repetition or hopping receiver may combine useful diversity only
when the channel is sufficiently different at the observations it combines.
On a flat single-mode passband, a few kHz of hopping can still encounter the
same fade. On a path with separated modes, tone gains can decorrelate over
frequency, but the delay, gain and coherence statistics depend on that path.
Consequently, the channel cases below must be measured in the receiver's
actual occupied band and at its actual observation duration.

## Terms that must not be collapsed

| Quantity | Physical or measurement meaning | Report with it |
|---|---|---|
| Common Doppler shift | Mean carrier displacement from path motion and changing phase path | Sign, time history, transmitter/receiver reference errors and detrending |
| Doppler spread | Spectral width of time-varying propagation gains; it controls temporal correlation/fading | Estimator, window, percentile, per-path versus composite, and width convention |
| Differential delay | Relative delay between resolvable propagation modes | Sounder bandwidth/resolution, mode powers, threshold and uncertainty |
| Within-mode delay dispersion | Delay structure internal to a mode | Resolution and whether the modem's waveform can resolve it |
| Oscillator error and phase noise | Transmitter/receiver equipment contribution | Reference source, frequency, warm-up, temperature and independent calibration |
| Path loss and absorption | Propagation power reduction, possibly frequency and mode dependent | Frequency, reference plane, received power/SNR trajectory, site noise and outage interval |
| Interference/noise | External atmospheric, man-made, galactic, co-channel, impulsive and receiver-added terms | Separate source, spectrum, time statistics, antenna, bandwidth and front-end state |

“Doppler width” values across papers are not automatically comparable.
ITU-R F.1487 expresses Gaussian Doppler widths with a 2σ convention. Some
measurement studies report RMS widths, the band containing a chosen fraction
of power, FWHM, or thresholded support. A reported largest value from a finite
campaign is not a population percentile or hard physical limit. Likewise,
RMS delay spread is not maximum delay, and differential mode delay is not
propagation time-of-flight. Preserve source definitions in both scenario
metadata and plots.

The receiver also needs both a physical channel center and its search range.
Common ionospheric Doppler, oscillator offset, deterministic drift and
per-path Doppler spread have different effects. A broad receiver search does
not establish that the physical signal occupies that entire search support.
Do not set a single “Doppler” knob to the sum of all four and call it a
measured spread.

## What established channel models cover

### Watterson and ITU-R F.1487

The Watterson channel is a useful and widely reproduced statistical test
channel. It applies delayed modes with complex Gaussian fading gains. ITU-R
Recommendation F.1487 (in force as F.1487-0, May 2000) specifies methods for
testing HF modems using ionospheric channel simulators, including Gaussian
scattering profiles and region/condition parameter sets. Its Annex 3 values
are controlled comparison points. For example, its high-latitude disturbed
case uses 7 ms delay spread and 30 Hz Doppler spread, while disturbed NVIS
uses 7 ms and 1 Hz, with a groundwave component when required. Those are
standardized test selections, not universal climatological percentiles or
joint extrema.

Watterson's original paper reports experimental confirmation on selected
Illinois–Colorado paths and approximately 3 kHz channels. A later NTIA
technical report explains the narrowband model's limiting assumptions:
stationary channel statistics, negligible intra-mode time dispersion and a
low-ray path. The precise boundaries depend on implementation and the
question being asked, but the operational lesson remains: a stationary
finite-tap simulator is a reproducible baseline, not a model of every
wideband, time-changing, multimode, spread-F or outage event. It does not
produce mode births/deaths, sunrise/sunset transitions, changing path
geometry, absorption blackouts or arbitrary non-Gaussian interference unless
those processes are added explicitly.

ITU-R P.533-14 is a different tool. It predicts HF circuit performance and
planning quantities such as mode availability, field strength, median
conditions and circuit reliability from path and climatological inputs. It
can help choose a candidate frequency, season, time and mode for a scenario.
It does not provide the instantaneous complex impulse response or guarantee
the modem's decode probability on a particular fading realization. Vertical
ionosonde measurements likewise constrain local layer state but do not fully
specify an oblique path's mode set, O/X polarization, scattering function or
received interference.

### Current empirical anchors and their limits

| Evidence | What it supports | What it does not establish |
|---|---|---|
| Dear et al. (2024), one-week Java Island NVIS sounder | Quiet equatorial-crest NVIS measurements; reported RMS delay spread 0–1.3 ms and Doppler frequency 0.7–1.1 Hz; multiple impulse arrivals in inspected examples | Disturbed equatorial-NVIS tails, global distributions, or a modem error rate. The authors explicitly call for longer/comprehensive measurement |
| Cameron et al. (2024), Qaanaaq–Alert polar-cap HF link, 2.5 years of multi-frequency observations | High-latitude path deflection and selected received-signal statistics; demonstrates off-great-circle routes and changing geometry under polar-cap structure | Fraction of all attempted transmissions that are available. Statistics selected received signals meeting the paper's SNR threshold, so missing/absorbed signals are censored |
| Ponomarenko et al. (2023), SuperDARN very-high-latitude climatology | Regional/seasonal variability in HF radar propagation observations and limits of prediction | Communication-link fading distributions. Radar backscatter geometry/processing and selected echoes differ from a modem link |
| Nissen et al. (2003), 3100 km disturbed wideband HF sounding | Percentiles for delay/Doppler spread on a specific high-latitude link. Their largest RMS 2σ Doppler spread was 16.8 Hz, 90th percentile 13 Hz; RMS delay spread reached 780 µs | Full multimode population: significant multimode records were excluded from the individual-mode statistics. Nor does one path establish a worldwide maximum |
| Willink (1997) analysis of DAMSON high-latitude links | Historical direct measurements across four high-latitude links and separate shift/spread/mode statistics | Current solar-cycle occurrence, equipment-neutral contemporary SDR statistics, or a global channel law |
| Olugbon et al. (2021), two daytime ESF-like HF Doppler events over Nigeria | Equatorial disturbance can yield nonuniform frequency broadening, coexisting components and spatial mismatch with a nearby ionosonde | Typical nighttime ESF occurrence or global event rates |
| Fernandes et al. (2026), long-term 10 MHz midlatitude Doppler residuals | Residual distributions and fits can differ by day/night; link-scale Doppler need not look Gaussian | A universal tap-gain Doppler spectrum. Residuals depend on path, reference correction and analysis |
| ITU-R P.372, radio noise | Atmospheric, man-made and galactic external-noise treatment for system planning | Time-resolved local noise, impulsive/co-channel events or a specific antenna/front end |

The spread of findings is a warning against treating latitude alone as the
severity label. Quiet Java NVIS gives sub-millisecond RMS delays with an
approximately hertz shift; severe high-latitude cases may show much broader
spread and deflected/multimode paths. Equatorial spread-F has its own local
time, season, longitude and irregularity dependence. Solar-cycle dependence
also differs among mechanisms and stations. A low-activity year or a quiet
local ionogram cannot be treated as proof that a long oblique link has no
severe propagation episode.

## Natural regimes from benign to unavailable

### Quiet, stable single-mode or dominant-mode reception

The path has adequate received margin, slowly changing geometry and a fading
law approximately stationary over the observation. A long integration can
collect energy effectively if equipment offset, phase noise and path drift
are also bounded. A repeated waveform may improve acquisition when the
receiver can combine observations whose channel gains remain correlated
enough for a valid matched process, or exploit differences that produce real
diversity. The exact design depends on the channel coherence time and
frequency correlation measured at the occupied tones. An apparently quiet
channel does not guarantee independent frequency fades across a wide hop set.

### Moderate midlatitude or NVIS variability

Mode powers fluctuate, multiple modes may arrive, and sub-second-to-seconds
frequency/phase changes can matter to long integrations. Sunrise/sunset,
terminator gradients, TIDs, sporadic-E, O/X splitting and ground reflections
can change which mode dominates. A receiver should be tested with a stationary
control and a matched nonstationary version that evolves mode phase/power or
delay continuously. An instantaneous independent-fade draw at every symbol
would erase natural temporal correlation and can unfairly penalize a receiver
that learns from channel history.

### Disturbed high-latitude and polar paths

Auroral and polar-cap irregularities can broaden Doppler, add delay structure,
change arrival direction and route a signal off the great-circle path.
Different paths and different periods do not share one maximum. OGC statistics
are conditioned on the signal being received. Absorption and deflection can
remove observations entirely. Test a received-but-distorted link separately
from a receiver-side no-signal availability/outage case.

### Equatorial irregularities and spread-F

Irregularities are strongly dependent on geographic region, magnetic/local
time, season and event state. Range and frequency spread need not track each
other with a fixed ratio, and a vertical ionogram may not sample the same
structure as the oblique communication path. Use observed/localized cases
where available, with path intersection and measurement aperture recorded.
Do not turn a measured single-frequency Doppler trace into a complete
wideband channel impulse response without additional evidence.

### Absorption, MUF closure and hard outage

Solar X-ray flare shortwave fadeouts, polar-cap absorption, auroral
absorption, disturbed propagation and MUF changes affect path loss and mode
availability. They are not equivalent to increasing a Gaussian fade variance.
When the received signal energy disappears beneath site noise or interference,
no decoder computation recovers it. Model the received power and noise
trajectory, mode loss/recovery, outage duration and resumption as a separate
link-availability experiment. Distinguish an interior erasure during an
otherwise continuing message from physical completion after the transmitter
is silent and the receiver has scored the required quiet interval.

## Noise, interference and hardware are part of the observed channel

The input at the antenna is usefully recorded as wanted propagation plus
external atmospheric/man-made/galactic noise, structured or impulsive
interference, and receiver-added noise/artifacts. This is experiment
bookkeeping, not an assumption that the processes are independent or Gaussian.
HF atmospheric noise is spatially and temporally variable. Man-made noise
depends on site and equipment, and a directional antenna can change the
observed atmospheric-noise level. Co-channel carriers, switching noise,
lightning impulses, front-end overload and recovery can produce errors that
an AWGN-only receiver test will not expose.

Maintain independent evidence for (a) the wanted channel response, (b) each
interference/noise source at an identified reference plane, and (c) receiver
overload, quantization, AGC and oscillator behavior. A received-data capture
can be jointly influenced by propagation and local interference. If those
terms are not separated, report the composite observation instead of
attributing a failure to ionospheric fading.

## Scenario bank for a fair Matrix Modem comparison

Every scenario should identify carrier band, path geometry/mode, UTC/local
time and solar/geomagnetic context when known, channel realization and seed,
exact frequency/delay/Doppler convention, noise/interference provenance,
front-end/reference settings, waveform and receiver processing, and the
observation window. The following cases are a stratified coverage plan. They
are not a probability distribution and their levels must be sourced or
explicitly labelled synthetic.

| Case family | Controlled content | Essential comparison/control |
|---|---|---|
| Stationary reference | Named F.1487/Watterson profile, fixed tap set and continuous complex-gain state | Matched AWGN or zero-spread path at the same received energy; multiple seeds after deterministic validation |
| Slow coherent path | Quiet single/dominant mode with slowly varying complex gain; separately measured oscillator offset and drift | Ideal disciplined reference and realistic equipment reference on identical samples |
| Delay/mode selectivity | Two or more paths with sourced or explicitly synthetic delays and power ratios, including evolving relative phases | Single-path control; preserve same total received energy and label the frequency-correlation aperture |
| Time-selective fading | Continuous per-mode Gaussian or measured gain process with declared spectrum and stationarity duration | Same geometry with zero spread; short-window and long-window observation comparison |
| Structured drift / mode transition | TID or terminator-like phase drift, mode birth/death or changing mode power, with measured or declared time scale | Stationary process with same local instantaneous spread; estimate loss from nonstationarity separately |
| High-latitude received distortion | A named measured or F.1487 profile with path context, possible multimode/OGC structure | Keep SNR-threshold selection explicit; pair received-signal decoding with a separate unavailable/outage trial |
| Equatorial disturbed case | Regional/local-time-conditioned spread-F or other irregularity evidence | NVIS and oblique geometry separate; ionosonde non-coincidence noted |
| Noise/interference | Recorded or parameterized atmospheric/man-made noise, tones, co-channel, impulsive noise, receiver artifacts | Wanted-signal-off false alarm control and wanted-signal-only control; do not normalize by an unspecified noise bandwidth |
| Absorption and outage/recovery | Measured or sourced received-power/noise trajectory and mode availability, including loss and return | Distinguish true outage from weak but continuously receivable fading; score reacquisition and public completion semantics |
| Finite-history repetition | Repetition lags spanning below/near/above the measured channel coherence time, including frequency hop choices | Compare receiver-history correlation with known-template, noncoherent energy and differential candidates at equal energy, airtime and overhead |

For each trial report at least detection/message success, false alarm or
wrong-content decisions, acquisition latency, energy and airtime, missed
candidate coverage, receiver work and memory, plus channel-state summary.
Separate equal-energy and equal-power comparisons. Repetition that doubles
transmission time cannot be called an energy saving unless transmit power or
message definition makes the accounting clear. Likewise, a wide frequency
hop is not frequency diversity until measured channel-gain correlations
support that claim.

## Modeling route and falsifiers

Keep a layered model in the native receiver test path: a declared propagation
mode/tap skeleton; continuous, stateful per-mode complex gain; independent
oscillator error; frequency-dependent path loss and availability; and separate
noise/interference/front-end effects. Use Watterson as a stable comparison
baseline. Add time-varying mode geometry or empirically shaped processes only
with explicit provenance and a matched simpler control. The channel state
must persist over processing chunks so artificial resets do not create
unphysical independent diversity.

A Watterson-only claim for a stated envelope is weakened if measurements show
nonstationary mode changes, materially dispersed paths, systematic
non-Gaussian temporal structure or availability events outside its model.
A claimed frequency-diversity benefit is weakened if channel measurements
show high cross-frequency gain correlation over the candidate hop set. A
claimed history-correlation benefit is weakened if realistic channel evolution
destroys the predictability at the selected lag, or if noisy-reference
correlation underperforms an energy/template baseline at matched energy and
false-alarm rate. These tests challenge the signal model and the assumptions
before any GPU acceleration result is treated as useful modem performance.

## Primary sources consulted

Access labels describe what was directly checked in this pass. Project
references listed below also link to more complete source-level notes and
contrary findings.

| Source | Access and use |
|---|---|
| Watterson, Juroshek & Bensema (1970), “Experimental Confirmation of an HF Channel Model,” *IEEE Transactions on Communication Technology*, 18(6), 792–803. [Bibliographic record](https://jglobal.jst.go.jp/en/detail?JGLOBAL_ID=201602006827430280) | Metadata checked; the full source paper was not available in this pass. Historical experimental basis of the stationary tap model. |
| ITU-R F.1487-0 (2000), “Testing of HF modems with bandwidths of up to about 12 kHz using ionospheric channel simulators.” [Official PDF](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf) | Recommendation and official record checked. Defines reproducible modem test cases, not climatological occurrence probabilities. |
| NTIA Report 90-255, *A Wideband HF Channel Model* (1990). [Official NTIA PDF](https://its.ntia.gov/umbraco/surface/download/publication?reportNumber=90-255_ocr.pdf) | Full text checked. Explains assumptions/limits of narrowband Watterson and motivation for wideband dispersive models. |
| ITU-R P.533-14 (2019), “Method for the prediction of the performance of HF circuits.” [In-force ITU record](https://www.itu.int/rec/R-REC-P.533-14-201908-I/en) | Official status and summary checked. Planning/circuit prediction role, distinct from instantaneous waveform response. |
| ITU-R P.372, “Radio noise.” [Historical P.372-17 contents](https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.372-17-202408-I!!TOC-HTM-E.htm), [current P.372-18](https://www.itu.int/rec/R-REC-P.372-18-202609-I/en) | Use this standard for external noise classes, with local site/capture qualification described in the [link budget](link-budget-and-operation-review.md). |
| Nissen et al. (2003), “Measured Channel Parameters for the Disturbed Wide-Bandwidth HF Channel,” *Radio Science*, 38(4). [DOI](https://doi.org/10.1029/2002RS002746) | Full article and reported estimator/selection limits checked via publisher/project source notes. One high-latitude path, multimode records excluded from selected results. |
| Ponomarenko et al. (2023), “Climatology of HF Propagation Characteristics at Very High Latitudes From SuperDARN Observations,” *Radio Science*. [DOI](https://doi.org/10.1029/2023RS007657) | Publisher article checked. Radar-climatology evidence, not directly a modem-link fading distribution. |
| Dear et al. (2024), “Ionospheric Channel Impulse Response Measurement System for NVIS Propagation Mode Over Java Island Based on Low-Cost SDR Platform,” *Radio Science*, 59, e2023RS007877. [Full article](https://doi.org/10.1029/2023RS007877) | Full text checked. One-week quiet equatorial NVIS measurement, with authors' limitations retained. |
| Cameron et al. (2024), “Statistical Analysis of Off-Great Circle Radio Wave Propagation in the Polar Cap,” *Radio Science*, 59, e2023RS007897. [DOI](https://doi.org/10.1029/2023RS007897) | Publisher article and selection criteria checked. Long multi-frequency high-latitude link record; received-signal censoring matters. |
| Cameron et al. (2024), “High-Latitude Off-Great Circle Propagation Associated With the Solar Terminator,” *Radio Science*, 59, e2023RS007917. [Full article](https://doi.org/10.1029/2023RS007917) | Full article checked. Ray-tracing study demonstrates conditional terminator geometry; it does not establish observed occurrence statistics. |
| Olugbon et al. (2021), “Daytime Equatorial Spread F-Like Irregularities Detected by HF Doppler Receiver and Digisonde,” *Space Weather*. [DOI](https://doi.org/10.1029/2020SW002676) | Publisher article checked through the project's existing full-text notes. Two events, not a climatology. |
| Fernandes et al. (2026), “Characterizing ionospheric variability through HF Doppler measurements: a statistical and numerical ray tracing analysis,” *Frontiers in Astronomy and Space Sciences*, 13:1713968. [Full article](https://doi.org/10.3389/fspas.2026.1713968) | Full article checked through project source notes. Midlatitude link residual distributions; interpretation is specific to its signal/reference processing. |
| Willink (1997), *Analysis of high-latitude HF propagation characteristics and their impact for data communications*. [Government of Canada record](https://publications.gc.ca/site/eng/9.892998/publication.html) | Report metadata/abstract checked. DAMSON historical four-link analysis. |

## Repository evidence and open questions

This synthesis cross-checks the Matrix Modem research in
[`Opus-5.5/01-hf-channel-moderate-to-worst.md`](../../algorithms-matrixModem/Opus-5.5/01-hf-channel-moderate-to-worst.md)
and [`Opus-5.5/10-other-propagation-paths.md`](../../algorithms-matrixModem/Opus-5.5/10-other-propagation-paths.md),
the high-fidelity propagation chapter
[`30-skywave-1-to-30mhz.md`](../../highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md),
and maintained channel controls in
[`docs/channel-models.md`](../../../docs/channel-models.md). Those sources hold
additional empirical values, noise/front-end controls, local validation and
source-specific caveats. Existing GUI presets named HF Worst or disturbed
should be read with the exact simulator law and margin settings, not treated
as physical maxima. Their behavior is evidence for implementation tests, not
for the prevalence of a corresponding real channel.

Important uncertainties remain: long-duration, calibrated, simultaneous
complex response and noise/interference records across representative
midlatitude, NVIS, equatorial and polar paths; joint statistics of delay,
Doppler, gain, polarization and mode availability; local front-end overload
and interference distributions; and realistic outage/recovery sequences
matched to modem transmissions. Published studies rarely align waveforms,
observation apertures, estimators, noise reference planes and path regimes.
No field record reviewed here validates Matrix Modem sensitivity, LPI, or
Tensor Core throughput. The next simulation conclusions should therefore be
reported as conditional on a named standard, capture or synthetic law, and
not as universal operation over natural HF conditions.

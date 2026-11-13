# Proposed experiment protocol

Prepared 13 November 2026. **Nothing in this protocol has been executed.**
Numerical budgets below are proposed engineering pilot limits, not literature
parameters, environmental probabilities or project-wide resource defaults.
The first phase is research; the
[literature index](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/README.md)
and [native options](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md)
record the evidence and unresolved choices.

## Observable objectives

The eventual suite should determine how the **existing complete native
receiver** behaves under a small number of well-supported moderate, adverse
and outage trajectories. Its initial purposes are to find missing physical
mechanisms, discriminate channel representations and expose meaningful
receiver failures. It cannot estimate worldwide availability from a few
selected events, and it need not demonstrate decoding during a physical outage.

Keep four questions separate:

1. Does the generator implement the declared mathematical/physical model?
2. Does that model reproduce independent measurements in its declared domain?
3. Does the modem receive, erase, reject, complete and reacquire correctly on
   those inputs under its public configuration?
4. Can the complete receiver sustain the required stream and workspace on
   the measured processor? This is a separate capacity question.

## Scenario record before execution

Every new scenario requires the following fields. Missing physical inputs may
be deliberate design assumptions, but must be named as such. No universal
default values are implied.

| Group | Required fields |
| --- | --- |
| Identity | Stable scenario name/version; environment family; scientific question; measured/standardized/derived/proposed/extrapolated status |
| Provenance | Source citation and page/table/figure; observation dates; geography; access level; source-data availability and license |
| Geometry | TX/RX coordinates or explicit hypothetical path; distance; height/elevation; terrain/ground/water; EME station/lunar geometry and time |
| Antennas | Full geometry/pattern/polarization, effective height or gain, efficiency and reference plane; matching/feed losses; uncertainty |
| Frequency | RF carrier, occupied DATA and Helper bands, interference bands, all conversion/sample rates and equivalent filter bandwidths |
| Environmental state | Relevant ionosphere/geomagnetic/solar, ground/sea/weather or lunar surface inputs; coupling and evolution; input model versions |
| Channel | Transfer representation; bulk versus excess delay; phase/group-delay convention; Doppler center and width definitions; polarization; path birth/death |
| Noise/interference | PSD/APD/event law, bandwidth and calibration plane; time/frequency/spatial correlations; burst waveform/duration; blocker positions |
| Front end | Measured or explicitly synthetic filters, clocks, gain/AGC, clipping/compression, converter scale and recovery |
| Randomness | Independent seed domains and their intentional correlations; generator version; episode-level independence and stationarity assumptions |
| Receiver | Canonical source/binary fingerprints; waveform/key/ordinal/calendar/FEC; full public hypotheses; thresholds; arithmetic; resource settings |
| Outcome | Primary endpoint; exact failure definitions; denominator; physical availability; scoring horizon; quiet-slot and reacquisition treatment |
| Sampling | Strata, finite trial and time/work budgets; decision uncertainty; stopping rule; holdout separation; multiple-look handling |
| Retention | Output paths, maximum retained data, purpose, cleanup trigger and unresolved failures |

Ground-loop scenarios must specify whether they mean conductive earth
electrodes or a closed magnetic loop. A “5.8 GHz EME” scenario must specify
station antenna beams and lunar footprint; a whole-disk and a narrow-patch
experiment cannot share an unlabeled scattering spectrum. A groundwave
scenario must declare whether skywave, space wave or ducting is excluded,
included, or an unresolved interpretation of its measurements.

## Initial strata and minimum counterexamples

The following is a coverage inventory, not a command to run a Cartesian
product of all parameters. Select one scientifically ready branch first.

| Family | Moderate/control | Supported adverse branch | Outage or discriminating alternative |
| --- | --- | --- | --- |
| Sub-9 kHz vertical | Measured/derived antenna response and a specified quiet-path/noise episode | Lossy matching/ground, waveguide transition or observed burst noise, with compatible inputs | Detuning/front-end recovery or absent usable link; separate electrode/loop coupling |
| 1 MHz groundwave | Homogeneous path with known ground and calibrated level | Mixed land/sea or terrain case; compatible local noise | Surface-only versus surface-plus-skywave; loss below detection |
| 30 MHz nominal groundwave | Explicit smooth-earth/coastal reference | Measured sea/terrain/atmospheric variation | Competing space-wave/ducted explanation; no supported surface path |
| HF skywave | Named stationary standard and measured quiet-mode control | One declared NVIS, auroral/polar, equatorial or transition episode | Mode loss/absorption; mismatched diffuse versus discrete generator |
| 5.8 GHz EME | Declared station geometry, beams and reduced scattering kernel | Broader/asymmetric lunar scattering, pointing or clock error under a supported geometry | Beam-weighted patch versus whole disk; occultation/insufficient C/N0 |

Do not label an invented combination of each row's maxima a measured natural
case. Such an adversarial combination can have a separate name and purpose,
without an occurrence probability.

## Stage A: cheap deterministic checks

Before a full receiver run, check the new renderer with bounded inputs:

- Exact bypass, constant gain and constant delay; explicit no-effect controls.
- A delayed impulse and tone for interpolation, gain, phase sign and tail
  extent; distinguish geometric delay-derived Doppler from added residuals.
- Two known paths for carrier correlation and a frequency-selective notch;
  summed-carrier output must equal the linear model's composite response.
- Continuous state under different chunk sizes, source silence, read limits
  and retries; no reseeding or slot-boundary phase reset.
- A known moving nondispersive path with consistent envelope delay and carrier
  phase; a separate dispersive transfer control if that model is used.
- Calibrated noise power after filtering; finite-band impulse energy and
  event-duration checks; no accidental per-window signal normalization.
- Clipping/filter/AGC recovery using the declared order, with wanted signal
  absent and with a strong blocker.
- Declared resource rejection and correct ownership/tail bounds.

These are proposed acceptance checks for new code. Existing production source,
channel and stream tests should be reused where they already test the contract.
Do not copy their implementation or add redundant tests merely to increase a
test count. If no new code is written, these checks are not claimed as run.

## Stage B: validate the channel independently of the modem

First reproduce one source observable for the selected regime, processed with
the source's bandwidth, estimator, threshold and averaging. A power-delay
profile must not be compared to a different RMS/support definition. Treat
sounder non-detections and saturation as censoring or availability evidence,
not convenient missing rows.

Use a measurement episode or geometry held out from calibration. Depending on
the regime, compare received level, frequency response, delay–Doppler structure,
polarization, cross-frequency coherence, fade level-crossings/durations,
noise APD/PSD/burst statistics and outage transitions. Do not demand every
observable for a model that only claims a limited subproblem; do not extend
that limited claim to the whole environment.

When raw measurements are unavailable, reproducibility of a published figure
or standard reference is useful but weaker. Label the model “reference
reproduction” or “physical/stochastic hypothesis,” not empirically validated.
If two candidate models fit the available observables but disagree on modem
outcomes, retain model uncertainty and identify the measurement that would
distinguish them. Do not select the one that gives better modem performance.

## Family-specific validation procedures

These proposed procedures supplement Stages A and B. They are not executed
results or instructions to run a full campaign. Select only those needed for
the first declared model and retain the bounded sampling rules below.

### Groundwave validation

Scientific basis: [20-groundwave-1-and-30mhz](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/20-groundwave-1-and-30mhz.md).

Use a minimal validation ladder: (i) reproduce the source’s homogeneous curves and unit conventions; (ii) verify limits against a free-space path, perfect-conductor limit, and frequency/height changes; (iii) compare shoreline recovery with the measured 1.242-MHz case and published mixed-path curves; (iv) compare 30-MHz near-coast and terrain behavior to NTIA/Hansen/Casciato evidence while distinguishing observation from analytic field; (v) measure new local paths using a calibrated transmitter, receiver, antenna patterns, noise floor, GPS/range, surface/meteorological observations, and multiple time windows; (vi) only then map received distributions into modem acquisition, sustained decoding, and physical message completion. Never call an ITU prediction, component parity, simulated fade profile, or one favorable measured link a high-fidelity channel qualification.

For both frequency families, report the complete configuration, input waveform and channel seed, path geometry, surface layers/properties, antenna type/height/polarization, measured/reference field or received power, noise/interference bandwidth and level, realized fades, decoder outcome, and uncertainty. Preserve a no-propagation-impairment control and a mechanism-isolated control for each added effect. A path-loss result should not be compared with a decoder output unless source power and antenna calibration are matched.

### Skywave validation

Scientific basis: [30-skywave-1-to-30mhz](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md).

Validation begins with analytic/unit controls: a single unshifted tap, a
constant-shift tap with zero spread, a zero-mean Watterson tap with declared
2σ width, two static delayed rays, and a known deterministic drift. Estimate
the output channel statistics using the same observables that the empirical
source used; verify the distinction between the input parameter and measured
finite-record estimate. Then use matched baseline/candidate channel seeds and
identical transmit waveform, receiver noise, search bounds and resource limits.
For nonstationary hypotheses, create a short discriminator where a constant
shift and a shaped drift have matched mean shift but different within-window
trajectory; a receiver that succeeds only on the fitted constant is not
validated for the drift family. For O/X or multipath, preserve actual mode
delay and power and add a one-path-removal or wrong-polarization control. For
absorption, reduce the correct path/mode power from a declared path-loss model
and include a “no surviving skywave” unavailable case; do not call that a
fading-model pass.

For every family, use paired wanted DATA/Helper over the same propagation
realization where this matches the operating signal chain, plus noise-only,
incorrect-address, competing-bit, structured-interference and physical quiet
completion controls. Report reception, erasure, wrong-bit and outage outcomes
separately. A low SNR or severe impairment can alter admission without proving
which physical mechanism caused the failure. Where uncertainty is the point,
use independent realizations or event blocks; repeated processing of one
capture is not an independent channel trial.

### EME validation

Scientific basis: [40-earth-moon-earth-5p8ghz](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/40-earth-moon-earth-5p8ghz.md).

1. **Geometry:** compare center delay with ephemeris and 2R/c, disk support with 2a/c, and Doppler with numerical derivative of the full two-leg path. Check self-echo and separated stations at rise/transit/set. This does not validate scattering or receiver performance.
2. **Radar statistics:** compare published maps and 4/13 cm models qualitatively; for the 7.209 GHz bistatic study, match only its measured region, footprints, albedo, CPR and spectrum width. Do not fit a global 5.8 GHz parameter to one crater or link.
3. **Target-frequency measurement:** collect timestamped 5.8 GHz I/Q plus TX power, patterns, pointing, station coordinates, noise temperature, polarization, weather, solar separation/activity and noise-only records. Calibrate clocks/oscillators and remove cable/IF offsets. Own-echo trials do not validate interstation EME.
4. **Matched discrimination:** estimate delay/Doppler, power-delay support, spectrum, coherence/fade duration, frequency correlation, polarization and SNR uncertainty. Compare Candidate A/B on identical sessions/configuration; hold out sessions, not adjacent packets.
5. **Adverse states and gate:** sample lunar range/elevation/beam coupling and locally measured rain/high-noise conditions. Preserve conditional distributions rather than combining unrelated extremes. Keep the simplest model predicting held-out statistics and adverse tails within uncertainty. A synthetic decode is not field sensitivity, availability or unknown-waveform acquisition evidence.

### Front-end validation

Scientific basis: [50-noise-interference-and-frontends](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/50-noise-interference-and-frontends.md).

These are proposed controls, not observed pass criteria for a particular hardware platform.

1. **Calibrate the chain before the site survey.** Measure receiver noise/system temperature and gain versus frequency using calibrated noise or hot/cold references. Record impedance, filters, cable loss, LNA, ADC rate/bit depth/full scale and decimation. Inject known tone/noise at antenna and receiver inputs to locate excess noise/loss.
2. **Measure linearity with and without blockers.** Use a two-tone intermodulation test plus a strong out-of-band blocker and weak in-band reference. Measure compression, spurious-free dynamic range and in-band desensitization at the actual gain/filters. Repeat at each front-end gain and attenuation setting. Search for products that move when blocker frequency or level changes; this falsifies an additive-noise-only explanation.
3. **Preserve ADC/AGC evidence.** Save raw samples and log AGC/gain changes, peak/RMS, clipping counts, crest factor and full-scale occupancy. Run fixed-gain repeats. Bursts appearing only after AGC changes, or a baseline tracking gain rather than antenna orientation, indicate an instrument/control effect until resolved.
4. **Record spectra and tails at matched bandwidth.** Report equivalent noise bandwidth, APD/CCDF thresholds, event rate/inter-arrival, pulse widths, autocorrelation and spectral occupancy. Give uncertainty for tails and account for temporal dependence. A flat averaged PSD alone does not establish white noise.
5. **Use controls that separate causes.** Rotation/crossed loops test direction; termination tests receiver floor; antenna-port injection tests calibration; choke/distance and local device on/off test coupling; a synchronized reference receiver tests common events. Keep transmitter-off and wrong-address/key controls under matched RF conditions so bursts cannot be scored as the target.
6. **Keep channel and interference distinct.** For sky wave, estimate path delay/frequency from probes/controls and assess blinded holdouts. For ground wave/EME, match waveform, pointing, filters, gain, calibration, clocks and time window. Separate intended-signal detection, false acceptance on null/wrong-address trials and robustness to measured interference. Higher estimated noise alone is not failure; replaying an unblinded waveform is not independent evidence.

### Band-specific measurement controls

Scientific basis: [noise, interference and front ends](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/50-noise-interference-and-frontends.md).

**Sub-9 kHz antenna comparisons.** For a fair comparison, define each antenna's transfer function and loss/noise budget, record height and azimuth, and share a receiver calibration path. Orthogonal crossed loops help distinguish an orientation null from a source minimum; rotate the antennas through predeclared azimuths. Measure preamplifier/tuner noise at the antenna terminals with calibrated source impedance. Test active matching stages with realistic blockers. Record common-mode current; treat a feedline choke as a diagnostic. Compare open/capped/terminated states only with known impedance and bandwidth.

**MF/HF site characterization.** For each site/band, retain calibrated spectra/APDs, local source inventory, antenna pattern/polarization, UTC/local time, solar/ionospheric state and weather/lightning covariates. Use quiet-band scans and controlled source on/off or distance tests to separate local from distant noise. Repeat across day/night and seasons. A 1 MHz vertical and 30 MHz resonant antenna have different field-to-voltage responses. For ground wave, record ground condition and path geometry; label sky-wave receptions by path/state. Preserve a no-signal series for each condition.

**5.8 GHz EME noise controls.** Report clear-sky noise and Moon-on-beam change separately. Do not substitute a 3.2 GHz/9.6 cm lunar result for 5.8 GHz. Compare same-night/elevation/gain off-Moon and Moon pointings; use noise injection or hot/cold references to check gain stability. Record terminated input, cold/blank sky, lunar disk, ground and Sun-safe scenes where practical. Keep oscillator, sample-clock and Doppler errors separate from random receiver noise.

## Stage C: a bounded communication pilot

Proposed first pilot for **one selected environmental stratum**:

- Two independently generated or independently selected event trajectories
  at each of three states: moderate, observed-adverse and outage/transition.
- At most two predeclared received-level settings per trajectory: a control
  and a weak setting chosen from existing applicable native evidence or an
  explicitly labeled feasibility calculation. Total: at most **12 wanted
  complete-receiver trials** for that stratum.
- At most six companion negative/control trials chosen to cover noise-only,
  wrong key/ordinal, structured interference, physical quiet and resumed DATA
  after a fade. Select the controls that actually exercise the uncertainty;
  do not automatically multiply every combination.
- Use the smallest bounded canonical message/stream that includes acquisition,
  DATA, relevant channel evolution and genuine quiet-slot completion. A case
  whose physical episode cannot be represented this way remains unqualified;
  shortening time while keeping only a few favorable samples is not equivalent.
- Freeze the model, thresholds and held-out episodes before inspecting the
  pilot outcomes. If a receiver change is later compared, use matched inputs
  and label the paired nature of the data.

This is a **failure-finding pilot**, not a statistically precise reliability
claim. It is not enough to establish a small error probability, a tail
percentile or a broad capacity pass. It may stop immediately on a decisive
model/implementation inconsistency. Otherwise stop at the predeclared budget,
report uncertainty, and choose the smallest next experiment that could change
the conclusion. No automatic trial expansion or prolonged campaign is implied.

Before running, estimate the cost using one cheap applicable native case and
declare a finite wall-time/work budget appropriate to the selected geometry.
If even the initial complete trial is too expensive under the project testing
contract, perform the nearest valid channel/component discriminator and label
the missing full-receiver outcome. Do not replace it with an unqualified
abbreviated capacity claim or a multi-day run.

For any later probability study, predeclare a population and an appropriate
fixed-sample or sequential inference method. Correlated slots, reused noise
and repeated timings cannot be counted as independent successes. Rare-event
biasing requires valid weights and support; otherwise it remains adversarial
stress testing. See the [methodological review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/00-evidence-and-modeling-method.md).

## Outcomes, especially physical completion

Keep a chronological truth timeline independent of the receiver. Never give
the receiver the realized path, transmitted bit, stopping point or hidden
environmental state as an undeclared prior.

Record at least:

| Outcome | Required interpretation |
| --- | --- |
| Correct complete message | All selected message content recovered, integrity status recorded, completion from actual scored quiet |
| Erasure/ambiguous slot | No bit assertion; preserve whether later DATA resumes and how codec state handles the gap |
| Wrong bit or false DATA | Separate wrong-bit discrimination from admission on noise/interference/wrong address |
| Premature completion | Receiver completed while physical transmission continued, including an outage hiding the signal |
| Integrity rejection | Payload was not accepted; not interchangeable with successful communication |
| Accepted truncation | Shorter accepted message where permitted by the format; compare to original transmitted content |
| Missed/delayed completion | Real physical quiet was present but completion was absent or delayed |
| Reacquisition/suffix behavior | State and message outcome when energy returns after a gap or apparent completion |
| Resource/backlog failure | Actual native admission/backpressure/capacity result, not a channel decoding statistic |
| Physical outage | No usable wanted signal under the declared geometry/level; report availability and safe receiver behavior separately |

The most important time-placement controls move an otherwise identical deep
fade across Helper, initial DATA, interior DATA, the final DATA interval and
the two quiet-scoring apertures. Keep these as a targeted discriminator, not
an exhaustive grid. A validity-marked capture gap is a separate control from
a valid noise-only observation during a propagation fade.

Two scored quiet slots remain the production physical-completion contract.
The experiment must not replace them with packet length, transport EOF,
timeout, local quota or known simulated TX length. It must also avoid counting
tail energy as new DATA or truncating tails to make completion easier. Preserve
the global keyed ordinal across codec resets.

## Stage D: computational claims, only if requested by the investigation

Communication success does not establish real-time capacity. When native
receiver changes or capacity questions enter scope, follow the maintained
[testing stages](../../docs/building.md#testing-stages),
[complete-work contract](../../docs/robust-manual-capacity.md) and
[CPU allocation](../../docs/development.md#receiver-cpu-allocation).
Run `./build.sh test triad` before a new manual calibration. Do not edit or
reuse manual-case outcomes merely because an environmental model is new.

Keep full `W=2*H` receiver software capacity and exact explicit limits.
Separate channel-renderer cost, source preparation, receiver work and end-to-end
turnaround. Measure same-stream productive service over declared backlog
intervals, including unsuccessful Helper, all public hypotheses, fine search,
tracking, quiet completion, reacquisition and recurring preparation. Report
actual hardware, executed arithmetic, fallback reasons and memory ownership.
Simulator slowness and multiple parallel independent simulations are not the
receiver's hardware ceiling.

No expensive CPU/GPU run, cloud expenditure or physical transmission is part
of this research deliverable. No communication or capacity trial has passed
by virtue of this protocol being written.

## Reporting and containment

Future source/configuration manifests, tests and concise results stay under
this appraisal directory; research interpretation and literature updates stay
under the requested `_ref/.../Astra-6` directory. Link the canonical production
code, do not copy it. Add only necessary isolated build integration when an
implementation is selected and reviewed.

A report should state executed, failed, inconclusive, unavailable and not-run
coverage separately. Retain negative controls and original failures when a
repair passes. Model agreement, code correctness, field realism, modem
reliability and processor capacity are different claims. A more detailed
implementation or a better algorithm remains possible at every stage.

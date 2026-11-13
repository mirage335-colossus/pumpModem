# Evidence, fidelity and the meaning of worst case

Research date: 13 November 2026. This is a research and design document, not a
record of executed modem experiments. The [index](README.md) defines the scope.

## What this review can establish

The purpose is to identify physically defensible environments for a separate
appraisal of the existing native three-carrier Robust modem. The requested
environments span very different mechanisms: a sub-9 kHz electrically small
antenna and Earth–ionosphere waveguide, MF/HF surface propagation, HF
magnetoionic propagation, and passive lunar scattering at 5.8 GHz. A common
software interface is useful; a common empirical channel law is not assumed.

This is a broad, structured literature review with targeted source inspection
and citation chaining. It is **not an exhaustive systematic review**, a field
survey, or a measurement of the user's installation. Search accessibility,
language and indexing limit coverage. A later paper, a better propagation
solver, or a better-conditioned statistical representation may change the
proposals. No proposed implementation is designated the best possible one.

The search covered foundational theory, original measurement campaigns,
author manuscripts, government research and operational recommendations.
Recent searches explicitly included 2020–2026. Publication date, observation
date and web crawl date are different: a newly indexed old paper is not recent
evidence. The domain chapters identify which recent work was actually found
and what it supports. The earlier repository
[changing-HF review](../../resilience-triple_Robust_Modem/Astra-6/report.md)
supplied some literature leads; its older source-code observations are not
treated as current implementation facts.

### Search coverage and selection

| Branch | Principal search combinations and follow-up | Decision supported |
| --- | --- | --- |
| Sub-9 kHz | electrically small/top-loaded VLF antenna, ground/earth antenna, waveguide attenuation, sferic noise, D-region response, recent measurements | Separate antenna feasibility, propagation and receiver noise; identify realistic alternatives |
| Groundwave | Sommerfeld/Norton, spherical Earth, mixed land/sea, Millington, HF surface wave, conductivity, roughness, terrain, coastal measurements | Determine when a surface-wave model applies at 1 and 30 MHz |
| Skywave | Watterson, HF delay/Doppler measurements, high latitude, NVIS, off-great-circle, flare/geomagnetic disturbance, nonstationary ionosphere | Distinguish modem test presets from conditioned environmental models |
| EME | lunar radar delay–Doppler, microwave scattering/polarization, libration, 5.8 GHz/6 cm, bistatic geometry, microwave lunar noise | Translate radar evidence into a passive communication channel without losing geometry |
| Noise/front end | radio-noise surveys, impulsive/heavy-tail models, man-made noise, interference, clipping/AGC, antenna temperature | Preserve burst structure, bandwidth, calibration plane and hardware limitations |
| Validation/extremes | model discrepancy, conditional extremes, rare-event simulation, sequential uncertainty | Avoid false confidence from fitted simulators, independent maxima or repeated testing |

Inclusion favors a source that changes a modeling choice, supplies a bounded
observation, contradicts a convenient assumption, or supplies a reproducible
reference calculation. Reviews and standards guide discovery and baselines;
primary measurements remain necessary for environmental claims. Generic
satellite-to-Moon links, terrestrial cellular fading, and radar target detection
are not automatically evidence for passive EME communication. Analogous work
is retained only with its transfer limitation stated.

Each chapter carries its own annotated bibliography. Source IDs are local to
that chapter, so they cannot silently collide. Direct source links, access
status and observation context accompany consequential numbers. A complete
paper being available is not a claim that every equation, dataset and cited
reference was independently reproduced. “Full text inspected” means relevant
sections were inspected; an abstract-only item cannot support hidden table
values or detailed validation conclusions. Unverified parameter values remain
open instead of acquiring authority through repetition.

### Evidence labels

| Label | Meaning | Permitted use |
| --- | --- | --- |
| Measured | Reported observations with a specified instrument, population and processing | A reference case within those limits |
| Standardized | A recommendation or agreed test construction | A reproducible baseline; not a natural upper bound |
| Derived | A calculation from stated physical assumptions | A checkable scale or limit; not a measurement |
| Proposed | An engineering choice for the eventual suite | A hypothesis to validate |
| Extrapolated | Transfer across frequency, geography, hardware or statistics | Sensitivity analysis with explicit uncertainty |
| Unresolved | Evidence or necessary inputs unavailable | No fitted parameter or probability claim |

## Five different meanings of adverse conditions

1. **Observed severe episode.** Reproduce a documented event, keeping its
   location, path, frequency, date, measurement bandwidth and instrument limits.
2. **Conditional statistical tail.** A stated percentile within a specified
   population, season, propagation regime and observation duration. A percentile
   of hourly median power is not a percentile of instantaneous fade depth.
3. **Physically compatible stress scenario.** Combine mechanisms that can occur
   together, with uncertain weights or probabilities explicitly left unknown.
4. **Adversarial engineering stress.** Deliberately combine extreme values to
   expose a receiver weakness, even when joint occurrence is unsupported.
5. **Outage or observability limit.** No usable path, complete occlusion,
   overwhelming interference, or no recoverable signal within the available
   observation. The expected result can be an erasure or unavailability.

There is no finite universal worst-case link under unlimited loss and
interference. This does not make severe testing pointless. It means the
testable object must be a bounded, documented family and an explicit receiver
outcome. Severity is also not monotone: moving a weaker echo can place a null
on a carrier, while a larger coherent frequency shift can be easier to track
than a smaller irregular shift.

The statistical literature explicitly studies extremes in which only some
variables become large. Heffernan and Tawn's conditional formulation motivates
looking at dependence rather than assembling marginal maxima [M3]. This is
methodological support, **not a calibrated joint HF/VLF/EME distribution**.
More recent work by Tendijck, Tawn and Jonathan shows that apparently similar
conditional representations can conceal different extremal dependence [M4].
Thus even a fitted conditional model needs tail diagnostics and model
comparisons; choosing a sophisticated family does not resolve missing data.

## A physical description before a fading generator

Use a conditional state description

\[
\theta(t)=\{\text{geometry, environment, antenna state, RF state, clocks}\}.
\]

The environment may contain measured trajectories, slowly varying inputs and
stochastic residuals. “Chaotic” in ordinary language should not be converted
into a claim of mathematically demonstrated chaos, a particular attractor, or
sample-independent randomness. Different processes have different persistence
and observability.

A useful *linear propagation* representation is a polarized time-varying
kernel, followed by receiver processing:

\[
\mathbf v(t)=\int \mathbf H(t,\tau;\theta)\,
\mathbf x(t-\tau)\,d\tau+\mathbf n_{\rm ext}(t)+\mathbf i(t),
\qquad y[n]=\mathcal R\{\mathbf v, n_{\rm int};\theta\}[n].
\]

The matrices include the chosen polarization bases and antenna projections;
\(\mathcal R\) may include filtering, conversion, oscillator effects, gain
control, compression and quantization in their actual physical order. This is
a proposed decomposition, not a promise that all channels are linear or that
all hardware effects commute. The classical time-varying channel framework
distinguishes general, WSS, US and WSSUS descriptions; those special statistical
assumptions should be tested over the relevant observation window [M1].

Important modeling consequences:

- **Common environment does not mean identical gains.** The three carriers and
  Helper probe one channel at different frequencies. Their correlation follows
  from the delay/polarization/scattering structure, rather than being set to
  either zero or one for convenience.
- **Propagation phase and envelope delay need a consistent convention.** For a
  nondispersive path of length \(L(t)\), carrier phase includes
  \(-2\pi f_c L(t)/c\), and its time derivative gives the corresponding
  Doppler. In a dispersive ionosphere, phase delay and group delay differ;
  derive both from the frequency-dependent transfer function. Do not equate
  group-delay motion to carrier Doppler without that assumption.
- **Avoid double counting Doppler.** Delaying the envelope, including RF
  propagation phase, and adding another independently calculated Doppler term
  can describe the same motion twice. Residual scatter Doppler and oscillator
  error need separate definitions.
- **Bulk delay is not excess-delay spread.** The long EME flight time belongs in
  calendar/timestamp geometry. The echo's relative delay structure belongs in
  the channel convolution. Both matter, in different ways.
- **TX phase noise is evaluated at emission time.** Widely separated delayed
  paths need not see an identical transmitted phase sample. Receiver oscillator
  error acts at receive time. A single common phase factor is an approximation
  whose bandwidth/delay conditions must be recorded.
- **Power normalization must not erase the experiment.** A unit-average-power
  fading law is useful for a controlled comparison. Renormalizing every
  realization or short window removes the deep fades and antenna/path losses
  that a physical link-budget experiment is supposed to test.
- **A disappeared signal is not a disappeared environment.** Thermal noise,
  lightning, other transmitters, front-end state and oscillator evolution
  continue through fades, silence and acquisition failures.

These are proposed consistency requirements derived from the decomposition;
the domain chapters supply the physical evidence needed to instantiate it.

## Definitions that must travel with every number

For a locally defined power-delay profile \(P(\tau)\), record its measurement
threshold and resolution before calculating

\[
\bar\tau={\int\tau P(\tau)d\tau\over\int P(\tau)d\tau},\qquad
\tau_{\rm rms}=\sqrt{{\int(\tau-\bar\tau)^2P(\tau)d\tau\over\int P(\tau)d\tau}}.
\]

Finite support, a last detectable echo, an energy-containing interval and RMS
spread are different quantities. The same distinctions apply to Doppler:
mean shift, RMS width, Gaussian \(2\sigma\), FWHM, threshold-defined width and
largest instantaneous excursion cannot be exchanged without a model. A weak
sounder misses tails; its reported maximum is not proof of compact support.

The following are required descriptive fields for a usable literature-derived
parameter, not values supplied by this review:

| Quantity | Necessary context |
| --- | --- |
| Received level/path loss | TX power reference, antenna gain/efficiency, polarization, calibration plane, averaging |
| SNR or C/N0 | Signal power definition, noise-equivalent bandwidth, PSD sidedness, filtering, time window |
| Fading | Amplitude/power convention, averaging, distribution, level-crossing and duration statistics |
| Delay/Doppler | Definition, sounder ambiguity/resolution, threshold, time window, bulk compensation |
| Noise impulses | Analog bandwidth, sample rate, saturation/blanking, event grouping and censoring |
| Occurrence probability | Population, exposure, excluded intervals, dependence and confidence method |
| Model residual | Calibration inputs, independent validation population, instrumentation uncertainty |

For flat noise in a stated equivalent bandwidth \(B\),
\(C/N_0\,[\mathrm{dB\!\cdot\!Hz}]=C/N\,[\mathrm{dB}]+10\log_{10}B\).
This conversion does not turn an impulsive or colored-noise experiment into
AWGN. For nonflat spectra, integrate the PSD through the actual filter. Keep
physical dBm/Hz separate from the modem's normalized I/Q and audio dBFS/Hz.

## Fidelity is validated at several levels

The relevant question is whether the model preserves the phenomena that can
change the receiver decision, over the stated domain. A detailed global
simulation with poorly known inputs can be less useful than a calibrated
local model. Conversely, matching mean received power cannot validate burst
outages or coherent tracking.

Calibration and validation are separate. Kennedy and O'Hagan explicitly
separate uncertain parameters from model inadequacy [M2]. Here, fitting the
simulator to the same observations used for its acceptance would hide both.
This review therefore proposes independent measurement episodes and an
explicit discrepancy allowance, without requiring a particular Bayesian
implementation.

The evidence ladder should be:

1. Analytic limits, dimensional consistency and conservation where applicable.
2. Reproduction of a source's observable under its instrument processing.
3. Holdout observations with changed time, location or environment, each
   reported separately rather than pooled into a favorable average.
4. Modem outcomes using the canonical complete receiver.
5. Separate runtime/workspace measurements for the actual native receiver and
   hardware, if a computational claim is made.

Simulation truth must not leak into the receiver. Known truth may score an
experiment, but cannot silently set arrival, fading trajectory, path count,
bit identity or noise power beyond the declared receiver contract. Include
model-mismatched generators: a receiver evaluated only against its own
assumed channel family can pass because its errors are shared.

## Rare failures and bounded computation

Long continuous fading episodes create dependent samples. Many slots from one
episode are not independent field trials; repeated timings of one waveform
are not communication-success replications. The experimental unit can be an
independently generated, environmentally conditioned trajectory, with all its
slots kept together. Dependence across recorded episodes also needs attention.

Importance sampling and subset methods can reduce expensive rare-event
simulation, but only relative to a specified probability law. The weighted
estimator requires a proposal covering every contributing failure region;
poor proposals can produce unstable estimates [M5]. Until an environmental law
and likelihood ratios are defensible, deliberately adverse cases should be
reported as **stress coverage**, not converted into an outage probability.

Repeatedly looking at ordinary fixed-sample intervals and stopping after a
favorable result changes coverage. Time-uniform confidence sequences offer a
principled alternative under their stated stochastic conditions [M6]. They do
not make correlated slot samples independent, repair model mismatch, or supply
a missing population distribution. Fixed predeclared budgets and stages are a
simpler acceptable alternative for this first suite.

The [appraisal protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md)
specifies a proposed small staged investigation. It separates deterministic
falsification, channel validation, communication outcomes and CPU capacity.
No heavy simulation or calibration was executed for this review.

## Annotated methodological references

- **M1 — P. A. Bello (1963), “Characterization of Randomly Time-Variant Linear
  Channels.”** IEEE Transactions on Communications Systems 11(4), 360–393.
  [DOI](https://doi.org/10.1109/TCOM.1963.1088793).
  Access: bibliographic record and original abstract reproduced by the
  [COIT archive](https://forohistorico.coit.es/index.php/biblioteca/articulos-seminales/item/characterization-of-randomly-time-variant-linear-channels);
  linked original PDF could not be fetched. Used only for the general/special
  channel distinction, not an uninspected quantitative result.
- **M2 — M. C. Kennedy and A. O'Hagan (2001), “Bayesian calibration of computer
  models.”** JRSS B 63(3), 425–464.
  [DOI](https://doi.org/10.1111/1467-9868.00294),
  [author-paper copy](https://www.asc.ohio-state.edu/statistics/comp_exp/jour.club/kennedy01.pdf).
  Access: abstract and accessible paper text; PDF extraction is imperfect.
  Supports separating calibration uncertainty and discrepancy. It supplies no
  radio-channel calibration or preferred implementation for this project.
- **M3 — J. E. Heffernan and J. A. Tawn (2004), “A conditional approach for
  multivariate extreme values (with discussion).”** JRSS B 66(3), 497–546.
  [Publisher](https://rss.onlinelibrary.wiley.com/doi/abs/10.1111/j.1467-9868.2004.02050.x).
  Access: publisher abstract and introductory text; separate full-text fetch
  failed. Relevant to non-simultaneous extremes and dependence, not evidence
  for any particular propagation percentile.
- **M4 — S. Tendijck, J. Tawn and P. Jonathan (2023; online 2022), “Extremal
  characteristics of conditional models.”** Extremes 26, 139–156.
  [Publisher](https://link.springer.com/article/10.1007/s10687-022-00453-7),
  [inspected manuscript](https://arxiv.org/pdf/2202.11673).
  Access: full manuscript sections and publication metadata. A useful caution
  about extrapolating tail dependence; its examples concern other physical
  domains and do not establish an RF model.
- **M5 — J. L. Beck and K. M. Zuev (2015), “Rare Event Simulation.”**
  [Author manuscript](https://arxiv.org/pdf/1508.05047),
  [handbook DOI](https://doi.org/10.1007/978-3-319-11259-6_24-1).
  Access: full manuscript, especially importance-sampling/support discussion.
  Provides methods and their limitations; the present suite would still need
  a justified channel probability law and checked weight diagnostics.
- **M6 — S. R. Howard, A. Ramdas, J. McAuliffe and J. Sekhon (2021),
  “Time-uniform, nonparametric, nonasymptotic confidence sequences.”**
  Annals of Statistics 49(2), 1055–1080.
  [DOI](https://doi.org/10.1214/20-AOS1991),
  [inspected author manuscript, revised 2022](https://arxiv.org/pdf/1810.08240).
  Access: full manuscript sections on assumptions and bounded observations.
  Sequential validity is conditional on the chosen theorem's assumptions;
  no automatic guarantee is claimed for environmental time series.

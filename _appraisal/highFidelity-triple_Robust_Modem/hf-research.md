# HF skywave: adverse mechanisms and comparison requirements

Reviewed 14 November 2026. This continuation reconciles the existing
[`_ref` HF model review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md),
[measured midlatitude/NVIS evidence](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md),
[high-latitude/equatorial evidence](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/32-skywave-dispersion-highlatitude-equatorial.md)
and [Astra synthesis](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md)
with additional primary-source checks. It explains what the expanded tests
should discriminate. Implemented operators, numerical checks and executed
receiver results must be established separately in [models](models.md) and
[validation](validation.md); a proposed case below is not an executed result.

The objective is to find losses of useful long-history gain, including strong
signals decoded incorrectly, weak signals mistaken for absence, and computation
that grows without sufficient reception benefit. A detailed simulator can still
make this receiver look artificially good if it preserves phase longer than the
physical path does. Conversely, arbitrarily increasing every impairment can
produce a channel with no demonstrated physical counterpart.

## Evidence levels and the reference baseline

Keep these labels attached to individual inputs and outcomes:

| Evidence | What may be claimed | What remains unestablished |
| --- | --- | --- |
| Measured complex record | Replay of the recorded observable, bandwidth, time interval and processing | An unobserved broadband impulse response, longer episode, other path or population |
| Measured summary fit | Reproduction of the published parameters and stated statistics | The exact recorded realization or omitted joint dependence |
| Physical construction | Behavior conditional on a declared, internally consistent physical approximation | That its selected inputs occurred together on a measured day |
| Constructed stress/control | A reproducible mechanism discriminator or numerical boundary | Occurrence frequency or designation as a measured worst case |

The Watterson baseline remains useful. Its original evaluation selected three
approximately stationary intervals totaling about 36 minutes from roughly
17 hours on one 1,294 km path, at 5.864/9.259 MHz. The measured component widths
were about 0.005–0.4 Hz. Those observations do not establish that stationary,
independent Gaussian taps describe a disturbed day, rapid spread-F, or hours of
phase-sensitive integration. The original report and later scope restrictions
are examined in [Fable 33](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md);
the [primary report](https://archive.org/details/DTIC_AD0699486) and
[F.1487 standard](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)
have different validation purposes.

The literal Watterson I1 E-mode fit has O/X component centroids 0.0022 and
0.0170 Hz, yielding a derived 67.6 s beat period. Its received component powers
already include antenna effects. Preserve the other fitted modes for a complete
summary-fit reference; do not apply additional polarization losses and describe
the result as the same measured fit. A Jones interference construction is a
separate experiment. Exact powers, widths and transcription qualifications are
in the [local component table](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md).

## Couple the phase, delay and polarization that the model actually represents

For a locally smooth transfer function `H(f,t) = A(f,t) exp(i theta(f,t))`,
using the ordinary baseband Fourier convention,

```text
group_delay = -(partial theta / partial f) / (2*pi)
Doppler     =  (partial theta / partial t) / (2*pi)
partial group_delay / partial t = -partial Doppler / partial f
```

These identities are independent numerical checks, away from zeros where phase
is undefined. For an offset `nu` about the RF carrier, a local expansion
`theta = phi(t) - 2*pi*nu*tau_g(t) - pi*D(t)*nu^2` gives group delay
`tau_g + D*nu` and offset-dependent Doppler
`phi_dot/(2*pi) - nu*tau_g_dot - D_dot*nu^2/2`.
This is a reduced local model, not a complete ray or full-wave solution.

The rule `Doppler = -RF * group_delay_rate` holds for a nondispersive moving
path; it is not a general plasma relation. A fresh full-text check of
[Aricoche, Hysell and Morton (2026)](https://www.frontiersin.org/journals/astronomy-and-space-sciences/articles/10.3389/fspas.2026.1848834/full)
confirms that their Alaskan 2.9/3.4 MHz group-delay and integrated carrier-phase
trajectories differ quantitatively. They observed approximately 20 dB fading,
with the fastest changes beyond their inversion's ability to reproduce.
Ten-second coherent processing and minute-scale estimates do not supply an
arbitrary subsecond phase trajectory. A Gaussian fit to one observed spread
quantity is not a validation of a Gaussian Doppler power spectrum.

Ordinary intra-mode dispersion is a useful small-effect control. The existing
literature compilation spans approximately 8–1,000 microseconds/MHz, with
different measured and predicted geometries. Across 225 Hz this is only
0.0018–0.225 microseconds of differential delay. A large impairment attributed
solely to those slopes should prompt a unit/numerical check. The local review's
strong wording that dispersion is irrelevant should be read within this
narrowband, away-from-cutoff scope; it does not eliminate branch disappearance,
mode merging or a reflection-caustic problem. The source-status distinctions
remain in [Fable 31](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md).

For one transmit and one receive antenna, use the scalar port response
`p_R^H U_R diag(H_O,H_X) U_T^H p_T`. Unit polarization vectors and both launch
and receive projections are required. Two circular components can cancel at a
linear receive port even when substantial vector-field energy exists. That
energy is unavailable to this antenna; computation cannot recover a missing
observation. An ideal second polarization port is a separately labeled antenna
reference, not information supplied to the actual receiver.

[Male et al. (2021)](https://www.mdpi.com/1424-8220/21/6/2210) measured a
5.4 MHz Barcelona–Cambrils NVIS link over 12 days. Their dawn/dusk intervals
favoring X over O, and conditional impulse-response correlations, oppose the
assumption that O/X modes are always equally strong and independent. Reported
morning/evening transitions span roughly 30 minutes/more than an hour in their
setting. A short excerpt may test a transition, but accelerating that clock
does not reproduce it. The study is conditional on its sounding/detection
procedure and supplies neither a universal mode-availability law nor a
single-antenna diversity gain.

## Nonstationarity and scintillation: avoid an immortal coherent component

[HiCIRF, Nickisch et al. (2012)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2011RS004928)
explicitly identifies missing large-scale electron-content phase variation on
its coherent component as a possible source of falsely favorable long
integration. Its stochastic treatment also omits reflection-caustic effects
and has a stated oblique-path qualification. This continuation successfully
opened the full primary body, whereas the earlier `_ref` recheck encountered
an access failure. The article's references to sponsor validation reports do
not replace those unavailable reports. A Rayleigh/Rician comparison should
therefore include continuing common phase evolution and preserve its physical
clock through fades and silence.

[Gherm, Zernov and Strangeways (2005)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2004RS003093)
provide physically coupled log-amplitude/phase statistics from an evolving
random medium. Their weak/moderate-fluctuation and separated-ray assumptions
cannot simply be extended to strong scintillation or caustics. Their published
1,000 km, 8.1 MHz worked example and simulation-moment agreement are theoretical
model checks, not a fitted measured day. Preserve coupled observables when
testing a reduced model; independent amplitude and phase knobs do not acquire
physical validity merely by matching their separate marginals.

An additional measured lead is [Ding et al. (2021)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2020RS007239):
a single-hop 12.7 MHz direct skywave, recorded on a ship in the Bohai Sea over
41 seconds on 20 August 2019, with 763 km ground distance and approximately
891 km group distance. The detrended phase standard deviation was 1.02 rad.
This is a direct propagation observation within a radar experiment, not the
ocean-scattered radar echo. Its fitted multiple-phase-screen parameters are not
independently measured plasma truth. The authors omit slow background
modulation and identify measured/simulated high-frequency differences associated
with receive filtering.

The [associated data record](https://doi.org/10.5281/zenodo.4698276) is licensed
CC BY 4.0. Retrieval and inspection found `ElecDirectwave.mat`, containing
one finite complex-double vector `yDire` of length 2,048. It has no sample-rate,
time-coordinate or voltage-calibration metadata. Mapping all samples onto
41 seconds implies 49.951 Hz; nominal 50 Hz and 40.96 seconds is also plausible
but unverified. Any replay must disclose its chosen endpoint convention.
The remaining model examples are simulated, not additional measured records.
The archive SHA256 is
`6475632af2f46815691e73efde81a3418864514255b7e5edd0ce2a2c6d78fcf7`;
the measured MAT SHA256 is
`8b2b663989bf294e8279622b6e30dc7d9a278aaf8914634e0a6e38ddca7f6c53`.
This supports a conditional narrowband complex-envelope replay, including the
instrument/filter/noise limitations. It does not establish a calibrated
broadband channel. Keep normalization fixed for the record and reject wrapping,
repetition or stretching to invent a longer observation. These files are
retained only while extracting and validating the finite measured fixture;
remove redundant archive/simulation material once that purpose is resolved.

Recent papers strengthen the alternatives without closing their validation
gaps. [Liu et al. (2026)](https://doi.org/10.1016/j.phycom.2026.103160) describe
continuous-phase, time-varying cluster synthesis for near-space vehicles; the
inspected abstract/introduction distinguish theoretical agreement from an
empirical channel fit. The full methods were not obtained. The three distinct
2026 Niu/high-latitude measurement-model leads and their access limits remain
listed in [Fable 33](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md).
Neither a decoded Doppler residual distribution nor an ionogram supplies all
missing joint amplitude, phase, delay and mode-transition information.

## Trajectory diversity independent of receiver templates

A fresh full-text check of [Olugbon et al. (2021)](https://doi.org/10.1029/2020SW002676)
supports testing several evolving timescales and changing mode structure.
Their Nigerian 6.957 MHz events occurred at low geomagnetic activity
(`SYM-H >= -50 nT`), so a storm-only test category would miss the reported
morphology. One event had approximately 15-minute HF Doppler oscillations and
30-minute electron-density oscillations; separate GPS records showed roughly
4- and 13-minute components. These were different observables and reflection
regions, not a fitted multisine channel. HF samples were 10 s apart, ionograms
15 minutes apart, and the HF/ionosonde reflection points differed by about
135 km. Preserve those cadence and geometry limits when selecting a constructed
trajectory; neither interpolation nor time compression supplies measured faster
variability.

[Fernandes et al. (2026)](https://doi.org/10.3389/fspas.2026.1713968) reinforce the
need to preserve temporal regime: their 1 Hz, 10 MHz Fort Collins–Newark
residuals favored daytime Cauchy and nighttime log-normal/exponential-power
fits. The article explicitly limits IRI-2020 to large-scale context and excludes
amplitude/phase analysis. These estimated frequency-residual distributions
cannot calibrate a tap Doppler spectrum or a joint complex channel law.

For a bounded failure search, freeze critical-frequency trajectories, physical
launch positions and stochastic channel seeds independently of receiver-bank
parameters and noise seeds. A sum of distinct slow oscillations plus bounded
nonperiodic evolution is a physical construction within the declared layer
approximation. Evaluate carrier phase, group delay and local dispersion through
the same eikonal. Bound the entire trajectory's critical frequency and rate
before rendering; reject cutoff crossing, invalid delay slopes and insufficient
dispersion/interpolation support. A conservative bound is an admission condition,
not evidence that all extreme values occur simultaneously.

The native C2 quintic knot law is a reproducible constructed alternative to a
single sinusoid. Its first and second derivatives vanish at every knot; this
artificial regularity and the chosen knot distribution are not measured plasma
statistics. Separate knot realizations and offsets test different histories but
do not sample the natural frequency of disturbed days. The scalar collisionless
vertical-layer approximation also omits magnetic splitting, collisions,
horizontal structure, diffraction, caustics and cutoff-induced mode loss.
Selected multipath/mode envelopes can probe interactions without removing those
limits or renormalizing physical fades.

Follow failures with the same received history where a diagnostic permits it,
then fresh trajectories and message positions. Increasing the transmission
duration generally traverses a different part of a nonstationary channel;
there is no guaranteed monotone success boundary in time. Keep operational
fixed-launch SNR/time brackets separate from matched-history diagnostics and
retain failures, unsupported cases and resource censoring in coverage reports.
The [measured-record audit](hf-experiments.md#measured-record-availability-audit)
states why this constructed diversity does not close the long-record gap.

## Absorption and critical-frequency boundaries

[Zawdie et al. (2017)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2017RS006256)
was checked in full here, improving the earlier bibliographic-only access.
Appleton–Hartree and Sen–Wyller absorption agree much better when each uses
the appropriate collision-frequency definition; reusing the same numerical
collision frequency can manufacture disagreement. Their climatological
collision variations change total attenuation by about 30% in the studied
calculations. A prescribed voltage gate is therefore a mechanism control,
not automatically a solar-flare or D-region absorption reconstruction.

Critical-frequency examples must keep O/X branch support, refractive phase,
group delay and loss consistent. A local Taylor response is unsuitable at a
singular turning point. The recent [PyRayHF paper](https://doi.org/10.1029/2026RS008702)
and [author code](https://github.com/victoriyaforsythe/PyRayHF) offer stratified
O/X forward-operator references; this pass obtained project documentation and
indexed primary excerpts, not the full article. The paper/package license
metadata differ, so inspect a pinned repository license before reuse. A ray
solution alone does not supply small-scale scintillation or validate an
instantaneous complete channel.

The separate [scalar full-wave component](hf-fullwave.md) addresses reflection,
transmission and collisional loss through one declared density profile. It does
not relax the eikonal's cutoff guard. [Wang et al. (2015)](https://link.springer.com/article/10.1186/s40623-015-0285-0)
explain why WKB fails near reflection/conversion and use a uniform treatment
with geomagnetic coupling. Their first author is Chen **Wang**. The present
unmagnetized normal-incidence calculation uses the scalar Maxwell equation;
it does not reproduce that paper's O/X results or its high-power heating physics.

The collision convention matters even for this smaller problem. The new
component's `nu` is an explicitly declared cold-fluid momentum-relaxation rate
in s^-1, with `omega=2*pi*f`. It is not automatically an empirical
monoenergetic, thermal-average or Sen–Wyller collision frequency. Zawdie et al.
show that the conversion depends on the adopted electron-energy dependence;
the often encountered factor 5/2 is not a universal conversion. Keep the
profile law and collision convention alongside every reported result.

[Gondarenko and Bernhardt (2009)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2009GL038379)
provide a counterexample to a single ordered severity scale: absorption depends
nonmonotonically on incidence angle and magnetization, while near a critical
angle their magnetized calculation becomes nearly insensitive to collision
rate through mode conversion. This is not evidence that increasing collisions
generally decreases absorption. Their electromagnetic/electrostatic mode
coupling is absent from the scalar component; its successful power accounting
cannot close this gap.

Recent ray-model literature retains a complementary limit:
[Hirvonen et al. (2025), AU-Ray §2](https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2024RS008209)
require the weighted collision frequency to remain well below wave frequency
for their pseudo-real ray approximation and identify strong D-layer absorption
as problematic. Agreement among ray programs is therefore not independent
validation in that regime. Conversely, full-wave cost does not imply broad
physical coverage: [Smith et al. (2020)](https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2019JA027499)
used a 3.6×3.6×250 km, 4.5 m grid on 4,400 processors for a 150 microsecond
frozen-density F-region calculation, with collisions approximated as zero.
That supports a separate scattering question, not long collisional channel
replay. The inexpensive one-dimensional component deliberately leaves its
three-dimensional irregularities and magnetic structure unrepresented.

New observational evidence also needs the right observable.
[Verhulst et al. (2026)](https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2026RS008688)
detect flare/particle-event signatures in passive ionosonde band-power records
relative to frequency-specific 14-day medians. Their study is explicitly a
qualitative proof of principle; the received mixture also changes with the
area from which broadcasts can propagate. These records support absorption
monitoring, not arbitrary-input complex channel replay or a quantitative map
from flare class to this component's density/collision profiles.

## What a useful sensitivity comparison must measure

The aspiration of much lower transmit power and more tolerated path loss is a
test hypothesis. Both consume the same received link budget: their dB changes
add. Longer airtime can provide more energy, but should not receive credit as
a simultaneous power, energy and latency improvement.

Use fixed transmitter schedules without receiver feedback. Preserve the
actual source waveform and public search hypotheses. Report transmitted and
received energy, active and observation duration, payload/service type,
reference noise bandwidth, false output, wrong content, erasures, physical
completion, processing time and workspace. Separate equal-power/airtime
comparisons from equal-information/energy comparisons and normal operating
points. A short benchmark is a conditional result, not a smooth sensitivity
floor or a rare-error estimate.

FT8 and WSPR are not simple QRSS amplitude detectors. The
[FT4/FT8 primary protocol description](https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf)
describes phase-coherent symbol-block information, LDPC decoding, BP/OSD and
prior-assisted decoding. The meaningful question is whether Robust's longer
history adds useful gain under evolving phase. FT8's 77-bit formats and WSPR's
50-bit structured beacon service differ from general text; compare their
actual supported content and overhead rather than matching character counts.

Pin upstream native decoders and disclose their priors. In
[WSJT-X 3.0.2's CLI](https://raw.githubusercontent.com/WSJTX/wsjtx/v3.0.2/lib/jt9.f90),
default calls match common simulator examples; `-c b -x b` blanks those calls
and the grid, while generic FT8 AP remains enabled. Fresh episode directories
avoid hash/history leakage. Direct inspection of the extracted WSJT-X 3.0.2
`wsprd --help` confirms that `-H` prevents hash-table use/update and `-B`
disables block demodulation in favor of single-symbol noncoherent demodulation.
Omit `-B` to preserve that baseline capability. Installed FLDigi 4.2.06 and
WSJT-X 3.0.2 are versioned test choices,
not assertions that their online moving documentation describes those builds.

Keep three comparison levels distinct: actual canonical complete reception;
an evaluator-only fixed-timing projection/history diagnostic with explicit
nuisance dimension and incorrect-bit basis; and an exact known-channel,
known-waveform white-noise oracle. More retained projected energy alone does
not establish improved detection because more fitted channel coefficients also
increase the noise penalty. No private channel truth may enter the actual
receiver. A high-energy miss with a favorable oracle motivates investigation
of model/search mismatch; an antenna null or complete outage can defeat every
decoder with the same observation.

The existing history diagnostic computes 0.25, 1, 4 and 16 second partitions
and the full aperture, with a complex gain per block and its associated
rank/shrinkage penalty. Reporting those already-computed alternatives helps
separate coherence loss from coefficient-fitting cost. It does not turn their
scores into operational false-alarm guarantees: a public winning template,
timing or frequency selected from the same noisy observation is still
data-selected. Calibration must include that selection and all searched
hypotheses. Private clean-energy and known-channel comparisons remain
diagnostics, even when they agree with a native score.

Conservative noise-only (`H0`) scoring does not certify bit selection with a
signal present (`H1`). With fixed public mixture weights and an unchanged
noise denominator, dropping nonnegative likelihood terms without
renormalizing can only lower the detection score relative to the full
mixture. That preserves a pointwise ordering for its threshold test; it does
not preserve the ordering between the two bit hypotheses when omitted
channel histories support them differently. Quiet noise controls therefore
leave a distinct wrong-content question under channel mismatch. In a
different, explicitly scoped model,
[Asyhari and Guillén i Fàbregas (2012)](https://guillen.eng.cam.ac.uk/files/journals/imperfect_csir_final.pdf)
analyze nearest-neighbor MIMO block-fading decoding with imperfect receiver
channel information through generalized mutual information and outage.
Their result motivates checking the actual decoding metric under mismatch;
it does not identify a cause in this receiver. The executed
[same-I/Q native comparison](hf-coverage-expansion.md) now reproduces the
original full-stream hashes and baseline scores at 40 and 20 dB-Hz. At
40 dB-Hz, explicitly scoring the broader varying-channel branch changes the
selected wrong affine bit 0 to correct bit 1; the first wrong slot on fresh
physical seed 62002 likewise becomes correct bit 0. The diagnostic preserves
unscored line/drift prior mass. These component counterfactuals identify useful
evidence skipped by affine early admission in those slots. They do not repair
the complete receiver or establish complete-message success or capacity; the
fresh message's additional wrong slot 2 remains untested by this diagnostic.

Unknown gain per block has an information cost even if clean projection
recovers all signal energy. For a fixed unit-norm waveform `u`, independent
proper complex Gaussian gain `g~CN(0,1)` and white noise `n~CN(0,I)`, write
`y=sqrt(rho)*g*u+n`. After marginalizing `g`, signal-present and noise-only
covariances are `I+rho*u*u^H` and `I`. Their exact divergence, in nats, is

```
D(P_signal || P_noise) = rho - log(1+rho) = rho^2/2 + O(rho^3).
```

For `N` independent equal-energy blocks with total mean energy/noise ratio
`R`, this becomes `N*[R/N-log(1+R/N)]`, approximately `R^2/(2*N)` when
`R/N << 1`. This is a fixed-waveform, fixed-prior presence-versus-noise
calculation, not bit-error probability or a capacity result for the modem.
It explains why increasingly short independent-gain partitions cannot receive
free credit for their clean projection. Correlated or lower-dimensional
physical histories can preserve more information, but a practical receiver
must exploit that structure with calibrated public hypotheses.

The primary results support the broader caution under their own assumptions:
[Sethuraman et al. (2009)](https://doi.org/10.1109/TIT.2009.2012995)
derive quadratic low-SNR capacity asymptotics for stationary noncoherent
Rayleigh models with shrinking peak/average-power constraints;
[Bergel and Benedetto (2011)](https://arxiv.org/html/1107.0639v1)
study underspread Gaussian WSSUS OFDM and an SNR-dependent effective coherence
time under stated power constraints. Neither establishes a universal HF
penalty or licenses using a known-channel bound for unknown channel history.
The existing [Fable 86](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/86-statistics-and-calculation-checks.md)
and [Fable 90](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/90-cross-check-calculations.md)
coherent-energy calculations likewise remain distinct from detection and
complete-message probabilities.

## Research-to-test discriminators

These are acceptance questions, not claims that the associated checks have run.

| Mechanism | Independent observable | Receiver comparison |
| --- | --- | --- |
| O/X cancellation and selective mode loss | Unit Jones basis, launch/receive power accounting, analytic port nulls, continuous branch envelopes | Matched aligned/canceling/one-mode histories; correct and wrong content plus noise-only output |
| Refractive versus geometric motion | Phase-frequency derivative, time-frequency cross derivative, causal delay bounds | Same mean energy with differing phase histories; preserve all public uncertainty |
| Small realistic dispersion | Complex response over occupied band; independent stencil/refinement error | Paired zero/source-scale dispersion, with changes expected to be small |
| Coupled scintillation and evolving coherence | Joint amplitude/phase trajectory, finite-window spectrum/covariance, record duration/filter limits | Fixed-airtime and longer-history energy/cost trends, with fresh selected holdouts |
| Mode loss/absorption/recovery | Continuous state, no fade normalization, source silence and delayed tails | Long enough physical episodes to observe recovery and both scored quiet slots |
| Intensive historical processing | Wrong-basis contrast, rank/penalty, actual native stage counters and complete costs | Strong, weak and noise-only observations; distinguish lost information from wasted work |

Retain contrary outcomes. An unusual-day counterexample is useful evidence even
when the software checks pass; a model's numerical consistency is not proof that
all of its joint conditions occur in nature.

## Competing evidence and phase continuity follow-up

The [receiver repair protocol](hf-receiver-repair.md) separates three questions:
an early native model-selection shortcut, unknown weak-signal phase history,
and omitted propagation physics. Their evidence is not interchangeable.
Conservative missing alternative mass under the noise null does not guarantee
correct bit ordering under a present, mismatched signal. The opt-in native
comparison therefore evaluates every admitted built-in alternative and the
retained-line residual alternatives with their original weights. It does not
use observed SNR or a confidence gap as a proof that competing models can be
skipped. Whether this repairs complete messages at acceptable complete-service
cost is an experiment, not a consequence of the noise bound.

For weak phase history, [Suvorova et al. (2018)](https://doi.org/10.1109/TSP.2018.2877176)
provide a phase-continuous hidden-Markov detector for a slowly varying tone,
contrasting with frequency tracking that loses phase between blocks. Its
Ornstein–Uhlenbeck prior and simplified gravitational-wave simulation do not
calibrate ionospheric dynamics. [Colavolpe, Barbieri and Caire (2004)](https://www.eurasip.org/Proceedings/Eusipco/Eusipco2004/defevent/papers/cr1592.pdf)
derive sum-product inference under oscillator phase noise; their AWGN,
Nyquist-pulse and slow-phase assumptions exclude much of the HF channel.
Together these support a bounded correlated-phase discriminator, not a claim
that all HF uncertainty can be reduced to one phase process.

The new finite phase/frequency lattice is a declared engineering prior. It
transports phase while frequency changes, marginalizes histories rather than
maximizing a data-selected path, and charges the whole initial/timing/radius
prior. Its transition rate and amplitude support are not measured HF laws.
Numerical path loss is also a theoretical failure: a probability-domain
forward recursion can underflow a temporarily unlikely phase class that later
dominates. The implementation must retain log-domain support and pass an
independent high-dynamic-range enumeration before interpreting its outputs.
Neither a calibrated finite-model likelihood nor a known-channel diagnostic
proves proximity to a general communications limit.

## From static full-wave profiles to waveform tests

The `_ref` distinction between absorption/outage and fading remains essential.
[Verhulst et al. (2026)](https://doi.org/10.1029/2026RS008688) report HF
absorption observations, but passive band-power changes do not supply a
calibrated complex channel kernel. The existing scalar full-wave solver can
independently couple reflection, transmission and absorption for constructed
profiles. Turning those responses into a waveform still requires bounded
frequency interpolation, a declared reference delay, causal memory and tails.

[Gondarenko et al. (2003)](https://doi.org/10.1029/2003JA009985) distinguish
slow-time field evolution from the fixed-density profiles actually solved in
their study. [Knepp et al. (2009)](https://doi.org/10.1029/2008RS004054) derive
wideband impulse responses for a phase-screen scattering geometry; that
geometry does not validate a near-cutoff reflecting column. These sources
justify testing a quasi-static kernel approximation against frozen complex
responses, while withholding a true evolving-plasma error bound. A causal
finite-memory implementation can still be a poor physical approximation.
Interpolation error must be checked at independent profiles and frequencies;
discarding negative-time FFT coefficients, normalizing away loss, or silently
adding delay cannot establish fidelity. Static passivity alone also does not
prove energy conservation for an interpolated time-varying operator.

The subsequent tests provide contrary evidence to convenient simplifications.
The normalized phase lattice represents more than 98.9% of clean coherent
energy in a failed long observation, yet its marginalized public evidence
misses admission. This agrees with the need to distinguish physical support
from usable channel knowledge; it does not prove the lattice prior is optimal
or a stationary noncoherent bound applies. The separately frozen sticky prior
tests that distinction without changing thresholds or selecting noisy maxima.
Likewise, accurate frozen full-wave FIRs fail endpoint profile interpolation
by as much as .814 complex amplitude. The profile-grid discriminator therefore
refines physical parameters, not the already converged frequency grid.

[Weber et al. (2026)](https://arxiv.org/html/2609.05811v1) compare modeled HF
maximum usable frequencies with ionosondes and successful FT8 reports. Their
climatological modeling and the successful-report operability lower bound
cannot supply failed-link denominators or long, calibrated complex channel
records. This is useful propagation evidence, with a different role from
receiver waveform validation. It does not close the measured disturbed-day
coverage gap or establish an orders-of-magnitude modem advantage.

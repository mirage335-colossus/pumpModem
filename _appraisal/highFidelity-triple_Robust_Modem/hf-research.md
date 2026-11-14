# HF skywave: adverse mechanisms and comparison requirements

Reviewed 13 November 2026. This continuation reconciles the existing
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

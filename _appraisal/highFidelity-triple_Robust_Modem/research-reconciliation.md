# Research reconciliation for the native appraisal

Updated 16 November 2026. This is an implementation map that reconciles the
existing [`_ref` evidence](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/README.md)
with focused primary-source checks. It selects bounded model families and
records evidence limits; it is not a channel calibration or modem result. The
appraisal executable must record the exact references, input tuples, seeds and
source identity it actually uses. Generator correctness, independent channel
validation, modem outcomes and receiver capacity remain different claims.

The subsequent [focused HF reconciliation](hf-research.md) extends this map
with a deposited complex scintillation record, coupled refractive phase/group
delay, single-port magnetoionic interference, recent contrary evidence, and
executable upstream modem comparisons. Its [experiment protocol](hf-experiments.md)
preserves the boundaries between measured data, analytic models and constructed
adverse controls.

The [independent-trajectory extension](hf-research.md#trajectory-diversity-independent-of-receiver-templates)
adds a distinct coverage requirement: select physical evolution before consulting
the receiver bank, keep physical trajectory and noise identities separate, and
follow misses with bounded energy/time brackets and fresh message placements.
Its multiscale and nonperiodic laws remain constructed stresses with explicit
cadence, cutoff and ray-approximation limits. The
[measured-data audit](hf-experiments.md#measured-record-availability-audit)
distinguishes useful public polar sounding summaries and received audio from a
long, calibrated complex channel replay; no additional replay fixture is implied.

The [cutoff/reflection component](hf-fullwave.md) adds a different evidence
axis: a static scalar cold-plasma boundary-value calculation, with collisional
loss and reflection derived together. This follows the existing
[_ref separation of absorption, mode changes and outage](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/30-skywave-1-to-30mhz.md),
not an assumption that deeper Watterson fades reproduce a blackout. The
[focused source review](hf-research.md#absorption-and-critical-frequency-boundaries)
records collision-convention, magnetic mode-conversion and strong-absorption
ray limits. The component neither replaces the guarded eikonal renderer nor
constitutes a dynamic channel or a complete-message trial.

The separate canonical GUI route tests the application's own profile
construction. Its `Each 65%` is an implementation parameter: slow and fast
selective depths are each 0.65 in
[`selective_depths`](../../src/robust_profile.cpp), not a 65th-percentile
ionosphere or an academic claim about disturbance incidence. Physical model
coverage and coverage of public GUI settings must have separate denominators.

The most useful correction to a too-simple “worse channel” scale is that the
mechanisms are conditional and can oppose each other. More propagation delay,
Doppler, noise or antenna loss does not universally mean a worse receiver
outcome. A route can appear or disappear, a disturbance can create an alternate
mode, and high-Q antennas/front ends can change slowly over the modem's long
symbols. Preserve full channel state and test the mechanism that the case
supports.

## Evidence-to-model map

| Family | Smallest defensible first representation | Evidence and provenance | Counterevidence and boundary | First independent check |
| --- | --- | --- | --- | --- |
| **8.27 kHz, 60.96 m top-loaded vertical** | Keep antenna/electronics, propagation, and noise as separate operators. Use the documented 61 m / 122 m hat / 2 mm wire electrostatic/lumped-circuit tuple as a *calculated sensitivity fixture*; separately expose the magnetic-field ideal-receiver branch and relative Wait–Spies / Longwave Mode Propagator path. Do not join electric and magnetic reference planes without calibrated field, antenna and receiver transfer. Preserve the specified σ, magnetic field, path length and h′/β tuple from Fable 81 for a repeatable case. | The reference tuple and its limits are in [Fable 81, inputs](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/81-scenarios-sub9.md) and [Astra VLF review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/10-sub9khz-antenna.md). The recent peer-reviewed [Teysseyre et al. (2025)](https://doi.org/10.1029/2024RS008150) used LMP to show that low-conductivity path segments and σ below 10⁻³ S/m can materially change inferred waveguide parameters; segments of tens of kilometres mattered in its studied paths. | 200 ft alone gives neither efficiency nor range. Teysseyre's under-50% electron-density-error aim is specific to that inversion, not a modem tolerance. P.368 begins at 10 kHz. Existing research's 2–12 dB/Mm field-amplitude values are path-, direction-, frequency- and time-conditioned; they are not a universal 8.27 kHz loss law. No provided fixture is calibrated to a real installation. | Check exact identity/complex gain, phase sign, delay and tail first; compare a source-reproduced LMP observable using the same frequency/path/model definition. Separately check antenna circuit limits and receiver noise. Prefer a calibrated transfer replay when one is available. Test chunk invariance and keep a sufficiently long VLF physical-time diagnostic; never speed up the channel clock to make the 96-minute native bit shorter. |
| **1 MHz groundwave** | Use ITU-R P.368 smooth, homogeneous spherical-earth vertical-field prediction as a reference amplitude curve with stated εr, conductivity, frequency, antenna heights, polarization and distance. Model a coastal path only as a separately declared mixed-path comparator. Use the native research tuple; do not derive transmitter feasibility from its ideal reference radiator. | [ITU-R P.368-10](https://www.itu.int/rec/R-REC-P.368-10-202208-I/en) supplies the standardized reference regime (10 kHz–30 MHz). Fable 82 pins a smooth-earth LFMF adapter and separates the homogeneous, coastal, material-layer and night-return questions. The primary 1.242 MHz mixed-route report is [Nishioka, Inasawa & Miyashita (2014)](https://doi.org/10.1587/comex.3.182), also documented in [Fable 82](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/82-scenarios-groundwave.md). | P.368 does not specify a local route, local noise, clutter, or whether night skywave was mistaken for groundwave. Nishioka's measurement is a nearby-frequency/geography analogue; its receiver level is not an absolute calibrated modem link. The source bibliography was corrected in Fable 70: this DOI is Nishioka et al. 2014, **not Kawano**. Mixed-path methods can diverge when conductivity inputs are uncertain. | First reproduce the pinned homogeneous reference curve and unit normalization; compare coastal direction/reversal independently. Do not invent a time-fading process from a spatial loss curve. To claim a modem level, separately specify path loss, antenna and external/receiver noise at compatible planes. |
| **30 MHz nominal groundwave** | Keep the P.368 smooth-earth surface-wave endpoint as a reference calculation, alongside explicit low antenna heights and distance. A near-coast/terrain record is a separate mixed mechanism with all competing direct, reflected, surface, diffraction, duct and skywave contributions identified where possible. | P.368-10 reaches 30 MHz but this endpoint only supports evaluation of its model input. [Fockens et al. (2022)](https://doi.org/10.1109/TEMC.2021.3137237) measured 1.8–28 MHz at 16 Dutch residential locations and found higher loss and different frequency scaling than P.368; their 50–1000 m low-antenna paths involved direct, reflected and surface waves, so the result is counterevidence to universal P.368 use, not pure surface-wave calibration. See [Fable 82](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/82-scenarios-groundwave.md) and [Astra's mechanism review](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/20-groundwave-1-and-30mhz.md). | “At 30 MHz” does not mean surface-wave dominant. More detailed models are not guaranteed to predict better: [Kirincich & Emery (2023)](https://doi.org/10.1029/2022RS007550) report that a simple ocean-path loss model matched their coastal HF radar observations better than more complex alternatives, with site/height conditions affecting the comparison. Their radar's ocean backscatter path is not a generic communication channel. | Validate the smooth-earth calculation and free-space/perfect-conductor limiting checks independently. Keep terrain, coastal, sea-state and ducting branches separate unless a joint episode supports their dependence. Compare against held-out path measurements with their actual antenna geometry and estimator; do not call a composite of unrelated extremes “measured worst case.” |
| **1–30 MHz skywave** | Begin with a stationary F.1487/Watterson comparison: each path's complex zero-mean diffuse gain and Gaussian Doppler spectrum use an explicitly stated **2σ** width; separate taps for materially separated delays or O/X modes. Add a time-varying centroid/drift, mode transition or fade only as a named episode. Keep arrival delay, mean shift, drift, spread, absorption and oscillator error distinct. | [ITU-R F.1487](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf) gives reproducible test profiles, not a worldwide channel distribution. [Fable 31](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md) summarizes measured paths and estimator limits; the 2024 Java NVIS report, for example, is a one-week observation. | Watterson selected nearly stationary intervals, not all HF. Its Gaussian spectrum is not universal: references retain Lorentzian observations, diffuse within-mode delay, coupled delay/Doppler and nonstationarity. New peer-reviewed [Fernandes et al. (2026)](https://doi.org/10.3389/fspas.2026.1713968) analyze one 10 MHz WWV–K2MFF residual link and report daytime Cauchy versus nighttime log-normal/exponential-power best fits in the detailed analysis. These are *estimated frequency residuals on one link*, not a tap-gain spectrum, and the study did not analyze joint channel amplitude/phase. Do not directly drive tap gains from those residuals. IRI-2020 ray tracing supplies large-scale path context, not the short-term residual law. | Check stationary tap covariance, delay profile and 2σ spectrum using the source's observable definitions. Then compare one same-mean constant-shift path against a measured/declared within-window drift trajectory; use session-held-out records if available. Keep spread-F/O-X/mode birth-death or absorption as separate hypotheses. A blackout is outage, not high-rate fading. |
| **5.76–5.8 GHz EME** | Use the existing Fable 84 retarded two-leg DE440 geometry and a low-dimensional beam-weighted lunar-surface scattering sum. Calculate excess delay from the same total path length and Doppler from its time derivative. Keep mean radar-equation return, beam/polarization loss, delay-Doppler kernel shape and receiver (T_{sys}) separate. Retain same-surface phase as a correlation hypothesis, with alternatives where useful. | [Fable 84](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/84-scenarios-eme.md) contains the reproducible DE440 geometry, assumed link inputs and finite competing kernels. [Zhang et al. (2020)](https://pmc.ncbi.nlm.nih.gov/articles/PMC7180880/) report bistatic 7.209125 GHz lunar radar observations with localized projected footprints about 503 km / 1,118 km; the observed/calculated spectra differ by station and their Doppler widths are about 35 and 90 Hz (40/90 Hz were used in one SNR calculation). [Taylor's 5.76 GHz analysis](https://wsjt.sourceforge.io/EME2010_K1JT.pdf) supplies conditional geometry-dependent width calculations, not observations of this modem. | The 2026 [JEIT EME channel model](https://doi.org/10.11999/JEIT251028) is recent but its 1.296 GHz ±39.88 Hz spread and BER are model/link-simulation outputs, not field measurements and not a 5.8 GHz transfer. A narrow beam and a disk-wide model yield different kernels. No cited measurement calibrates a complete whole-disk 5.8 GHz complex channel. Geometry-only Doppler is not scattering validation. | Verify center delay against 2R/c, disk delay support against 2a/c, beam-weighted cell power, and finite-difference versus analytic two-leg Doppler. Converge independent cell grids using the *same* scattering law. Validate the selected kernel against only the radar footprint/observable it shares; retain uncalibrated models as alternatives. Never re-randomize surface phase each short interval just to force fading. |

## Noise, references and decision statistics

Model input noise at a stated plane. Keep external atmospheric/man-made noise,
receiver-added noise, interference, oscillator error, antenna/front-end transfer,
and propagation separate. A PSD describes average spectral power; it does not
specify a burst waveform, APD, event timing, or nonstationary coupling. ITU-R
P.372 is a useful reference law for background levels, but current [P.372-18
(September 2026)](https://www.itu.int/rec/R-REC-P.372-18-202609-I/_page.print)
was not inspected by the cited research and does not supply local waveform
records. Dutch HF noise measurements above older P.372 estimates at some sites
and the small qualitative NTIA impulse record are contrary to universal
correction/burst assumptions; see [Fable 50](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/50-noise-and-interference-mf-hf.md).
Use calibrated noise captures for a site-specific claim. If absent, label the
noise law a standardized reference or explicit synthetic stress, and validate
PSD, APD, correlation, pulse duration/rate and front-end recovery separately.

For a theoretical baseline, Shannon's (C=B\log_2(1+S/N)) and finite-block
AWGN bounds such as [Polyanskiy, Poor & Verdú (2010)](https://doi.org/10.1109/TIT.2010.2043769)
apply only with their AWGN, bandwidth, energy, block length and known-channel
assumptions. They do not directly bound the current receiver's acquisition,
long-message code, frequency search, fading, interference or synchronized
calendar. Compare at a declared energy/noise convention and, where useful,
with a genie-aided/reference decoder that pays the same observation time. A
sharp waterfall or outage can be physically real. The useful question is
whether receiver loss is materially worse than the matched reference, not
whether every real-world curve is smooth.

For a concrete contrary theoretical result, [Yang, Durisi, Koch and Polyanskiy,
*Diversity versus Channel Knowledge at Finite Block-Length*](https://arxiv.org/abs/1204.2927)
find a nonmonotonic coherence-time tradeoff under noncoherent Rayleigh block
fading at fixed block length/error probability: diversity and channel-estimation
cost compete. This is not an HF/VLF/EME performance bound, but it rules out using
known-channel AWGN smoothness as a universal acceptance criterion. It does not
excuse implementation cliffs; compare matched observations, available known-
channel information, declared inference assumptions and executed receiver work.

Keep sensitivity sweeps paired by complete episode and fixed waveform/channel
history. Never renormalize each received window or select favorable parts of a
long episode. Report correct complete messages, wrong bits, erasures, missed or
premature quiet completion, integrity failures, outage and time-to-completion
with timeouts separately. The proposed cap of 12 wanted plus six selected
controls is a *failure-finding pilot for one selected stratum*, not rare-failure
probability evidence. [Fable 86](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/86-statistics-and-calculation-checks.md)
specifies a separate fixed-41-episode conditional confirmation only if that
question and its workload are later admitted. Repeated bits, slots and timing
replicas within one episode are not independent trials.

Recent wireless-channel evaluation literature offers methods but not domain
qualification. [Baur et al.](https://arxiv.org/abs/2408.00634) argue that
generic metrics such as MMD can be hard to interpret and recommend physical
channel and downstream communication checks. Its contemporary wireless
measurements/models do not validate VLF, MF/HF or EME. Adopt the methodological
lesson: check physically meaningful observables (power, phase, delay-Doppler,
cross-frequency/time covariance and tails) and modem decisions. Do not use one
aggregate distribution score as fidelity proof.

## Corrections and unresolved evidence

The later [bounded short-model competition](../../docs/validation.md#bounded-short-model-competition--16-november-2026)
revises the tradeoff assessment, not the recorded earlier outcomes. Short-model
competition and public quarter-clock coverage now work together: matched
default-mode replays recover the bright HF and EME 30 dB-Hz messages, preserve
the weaker HF message, and reduce fresh-`A` errors from six to four. The latter
still accepts incorrect content; its existing Guarded browser rating hides the
record. Visibility is an operator mitigation, not recovered data. These measured
costs and benefits are weighed against general resilience rather than a
pointwise no-new-admission requirement. Longer drift/gain models remain
fallbacks to avoid forcing disproportionate work in slow geometries.

Fresh paired histories retain four previous complete-message successes, but the
new HF `A` history worsens from seven to eight/nine wrong bits and does not
recover after +3 dB. Full admitted-model comparison on the same 43 dB-Hz
capture produces exactly the same scores and decisions while executing drift
on all 15 slots instead of two. This rules out the deferred implemented drift
search as a demonstrated repair for that capture; it does not prove which
unmodeled physical mechanism causes the error. Keep this counterexample beside
the earlier gains, and distinguish its persistent model/support question from
a modest, recoverable sensitivity tradeoff. Measured per-family process CPU
increases also reach 67–69% for the fresh EME pair, despite smaller wall changes.
Do not describe the new default as a free comparison or a capacity qualification.

The mechanism has relevant theoretical support and limits. In a different
noncoherent receiver, [Tarasak, Minn and Bhargava (2004)](https://personal.utdallas.edu/~hxm025000/TVTMar04ML_DiffSTBC.pdf)
show that within-block channel variation defeats the assumed constant gain and
creates a high-SNR floor; their multistage differential space-time method reduces
but does not remove it. This is a mechanism analogue, not a transferable HF
performance result. [Vilà-Insa and Riba (2025)](https://doi.org/10.1109/LWC.2025.3536921)
relate high-SNR noncoherent discrimination to codeword-conditioned spans under
their full-rank block-Rayleigh assumptions. It reinforces checking timing/model
support; it does not qualify this modem's changing-channel bank.
Fixed weighted e-value averaging can preserve null guarantees without assuming
independent scores ([Vovk and Wang, 2021](https://doi.org/10.1214/20-AOS2020)).
That property does not guarantee correct-bit selection or rejection of a
structured foreign signal. Proper model competition is therefore useful
without being a proof of robustness to every unmodeled trajectory.

The [16 November native follow-through](../../docs/validation.md#slow-gain-coherence-and-current-appraisal-reconciliation--16-november-2026)
distinguishes a source revision from a receiver repair. The current carrier-v1
waveform no longer misses the archived first VLF wander slot, but a complete
message at the same weak level loses a later bit. Two additional slow Gaussian
gain laws in the existing native fixed-coordinate scorer recover that message
without adding timing/frequency histories or lowering thresholds. Old short and
fully coherent models retain fixed prior mass. Their scales are chosen from
public geometry, not learned from this failed capture or supplied channel truth.

These laws are receiver alternatives, not a physical VLF or HF spectrum. An
exponential complex covariance has a Lorentzian spectrum and random magnitude;
a bounded phase-only excursion establishes neither property. The
[AR(1)/Jakes comparison by Ghandour-Haidar et al. (2012)](https://doi.org/10.1016/j.sigpro.2011.08.014)
shows that an approximation suitable for correlation matching need not minimize
slow-channel estimation error. That result concerns channel estimation, not a
modem detection bound. The [maintained synthesis](../../docs/triad-stochastic.md#nonstationary-channel-research-reconciliation)
retains this counterevidence and the conditional low-SNR covariance assumptions.
Current HF full-model comparison still has a wrong-content counterexample;
normalized white-noise evidence alone does not establish correct bit selection.
Neither improvement nor simulation coverage establishes a days-long outage
guarantee across the five physical path families.

The fresh-`A` HF follow-through gives a more specific counterexample. Its fixed
short-gain branch prefers the correct bit, while the frequency tracker dominates
the normalized mixture with the wrong bit; the reversal persists in the paired
thermal-off capture. A native diagnostic reuses the same per-block correlations
and Gram matrices to compute, for each bit,
`max_timing sum_blocks max_frequency(r* G^dagger r)`. Timing stays fixed across
the whole aperture. The reset transition gives every frequency path positive
prior; no uncharged path restriction is removed. On this frozen noisy-selected
bank, correct/wrong clean capture is 10361.928/10428.850 squared-sample units,
with resolved numerical ranks and direct residual checks.

This supports a conditional mathematical warning, not a global impossibility
claim. For the same block templates and finite positive Gaussian gain
innovations, the high-SNR leading log-likelihood term is captured energy divided
by noise variance. Correlation changes finite history penalties and determinant
terms but not that span. Thus a finite-coherence Gaussian transport model alone
cannot reverse this bank's leading wrong-bit ordering; it may still improve a
finite-SNR result. A changed timing bank, within-block template, singular gain
constraint or acquisition rule changes the premise. In particular, the ordinary
coarse clocks and their half-step refinement omit quarter-clock atoms already
included in the nine-clock public prior. This connects the present diagnosis
to the earlier [timing-complement result](hf-correlated-presence.md), and makes
bounded public timing nomination a concrete discriminator before a larger
channel-model rewrite. Private maxima, known-timing witnesses and added public
nominations remain distinct from qualified receiver decisions.

The public quarter-clock shadow changes this selected raw-tracker ordering to
the correct bit in both noisy and thermal-off twins, with unchanged wrong-key
shadow evidence. However, six complete default-mode trials preserve all 60
decisions without repairing a message: the earlier affine scorer already
admits DATA before the tracker in the bright failures. Experimental comparison
of all models repairs one fresh-`A` bit but still accepts incorrect content.
Additional affine centers also fail the declared two-episode discriminator:
wanted scores remain exactly wrong and wrong-key evidence rises. That evaluator
is removed. Affine already includes residual quarter-clock offsets, so its
negative result concerns new epoch/arrival anchoring. These findings favor
improving acquisition support and model validity together; they do not justify
blindly expanding history search or trusting the largest misspecified evidence.
One final fixed 15 dB-Hz baseline/candidate full-message pair executes the default
tracker but recovers no bits: both retain 13 ambiguous active slots. Varying
basis work rises by 10.7% with unchanged workspace. The production timing
expansion is therefore removed under its predeclared no-benefit rule; the
opt-in diagnostic remains. Reconsider promotion only with an executed default
benefit and matched negative-control preservation, not this selected-aperture
improvement alone.

Current-waveform EME adds a complementary result. At 30 dB-Hz, comparing the
already-admitted alternatives repairs the wrong third bit on identical complete
IQ, while the earlier affine-first default still fails. Unconditional full
comparison has a tradeoff: the HF fresh-`A` comparison accepts wrong content, including
when strong coherent-line evidence is present. Thus neither unconditional
comparison nor line presence establishes a universally safe selection rule. The final
line-conditioned reason string does not isolate the responsible mixture
component. Keep acquisition support, model adequacy and decision policy as
separate unresolved mechanisms; changing a threshold or forcing all work is
not a demonstrated five-path solution.

Recent work reinforces the distinction between carrier-frequency tracking and
waveform timing, without qualifying this modem. Sciacca et al.'s LuGRE snapshot
analysis explicitly scales the local code as well as compensating the carrier
([11 February 2026 preprint revision, §III-A](https://arxiv.org/html/2601.06081v2)).
Its known GNSS codes, 0.2–2 s snapshots and coupled Doppler scaling are different
from independent endpoint clocks and minute-scale unknown data bits. Lin et al.
report misleading coarse correlation peaks under acoustic multipath and fading,
and use multiple resampling branches
([JMSE, 2024](https://www.mdpi.com/2077-1312/12/8/1271)). Their model assumes
a common Doppler factor across paths; the lake captures were initially high
SNR, and the weakest/noisier cases retain failures. Neither paper transfers a
sensitivity or compute ceiling to HF, VLF, groundwave or EME. The implementation
case for bounded quarter-clock nomination comes from the native omission and
matched modem evidence, not from importing their reported gains.

Two additional low-cost estimation approaches also have material transfer
limits. [Schmid et al., Asilomar 2024](https://arxiv.org/html/2408.02312v1)
combine expectation maximization and belief propagation for blind equalization,
but the evaluated channel stays constant across 100 BPSK symbols, initialization
uses a variational autoencoder, and the belief-propagation curves retain
high-SNR error floors. This is not a qualified replacement for changing gains
within one long unknown bit. [Li et al., February 2026 revision](https://arxiv.org/html/2507.09268v2)
reduce fractional delay/Doppler search using separability and Fibonacci
refinement, but their AFDM estimator relies on chirps, guarded pilots and a
pilot 30 dB above data. Neither result authorizes narrowing this modem's public
search or transfers its measured complexity/sensitivity gain. Reconsider these
methods only with an appropriate observation model and explicit waveform or
initialization costs, while preserving the current negative controls.

- The mixed-route MF paper at DOI [`10.1587/comex.3.182`](https://doi.org/10.1587/comex.3.182)
  is Nishioka, Inasawa and Miyashita (2014), as corrected in [Fable 70](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/70-implementation-options.md).
  Do not attribute it to Kawano. The paper compares measured and predicted
  relative behavior; it is not a calibrated complex transfer capture.
- Teysseyre et al. (2025) is the published peer-reviewed [Radio Science
  article](https://doi.org/10.1029/2024RS008150), not the earlier 2024 preprint.
  Its conductivity/map-resolution result is scoped to VLF waveguide inference.
- The 2020 7.209 GHz radar record's beam footprint and bistatic estimator do
  not calibrate whole-disk 5.8 GHz communication. The 2026 1.296 GHz EME paper
  is a modeled alternative, not independent field validation.
- Fresh review confirmed ITU lists P.372-18 in force from 8 September 2026;
  full text and any changed tables/algorithms remain unchecked. Do not cite its
  status as validation of the current generator.
- Current appraiser claims remain under the public configured calendar/keyed
  ordinal. None of these propagation references qualifies unknown-time,
  unknown-ordinal or address acquisition. Physical transmission-level feasibility
  also needs measured antenna, system/noise, station and environmental inputs.

External sources were checked 13–16 November 2026. This file is a concise decision
map, not an exhaustive literature review; detailed source passages, search
limits, arithmetic and alternatives remain in the linked research chapters.

## Expanded numerical and mechanism checks

The full-calendar lunar implementation uses joint spectral draws and a bounded
native FFT representation. FFT use alone does not guarantee the intended
covariance. As a distinct alternative, Dietrich and Newsam's circulant embedding
method gives exact Gaussian samples on a finite grid when the target Toeplitz
covariance has an appropriate nonnegative definite circulant embedding. That
condition was not established for this coupled lunar process; the current
implementation instead reports an explicitly approximate spectral quadrature
and tests it against independent ring/Bessel covariance and off-grid projection
integrals through the whole supported episode. Increasing recurrence time alone
would not repair an inadequately resolved joint delay–Doppler kernel.
[Dietrich & Newsam (1997)](https://epubs.siam.org/doi/10.1137/S1064827592240555)

The added NVIS and DRM6 fixtures retain source-defined gain, delay, centroid
and Gaussian 2σ-width conventions. Independent checking of the newer ETSI
V4.3.1 (November 2023) Annex B.1, equations B.3–B.4 and Table B.1 found the
DRM6 tuple unchanged from the pinned older edition. F.1487 Annex 3 §3.4 is the
NVIS source; it permits an added groundwave component where required, whereas
this named fixture deliberately tests skywave taps only. Neither standard is
a universal worst-case population.
[ETSI ES 201 980 V4.3.1](https://www.etsi.org/deliver/etsi_es/201900_201999/201980/04.03.01_60/es_201980v040301p.pdf),
[ITU-R F.1487](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)

The compound-noise failures justify matched component ablations, not arbitrary
threshold tuning. The NBS atmospheric-noise study explicitly treats amplitude
statistics as bandwidth dependent and discusses nonstationarity between
measurements; NTIA's higher-order study also considers temporal correlations,
pulse widths and spacing. Accordingly the native tests check AR(1) and shot
covariance and moments as well as receiver decisions. Their synthetic laws
remain separate from environmental observations.
[NBS (1962)](https://nvlpubs.nist.gov/nistpubs/jres/66D/jresv66Dn6p713_A1b.pdf),
[NTIA TR-93-293](https://its.ntia.gov/publications/details?pub=2319)

# Research reconciliation for the native appraisal

Prepared 13 November 2026. This is an implementation map that reconciles the
existing [`_ref` evidence](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/README.md)
with focused primary-source checks. It selects bounded model families and
records evidence limits; it is not a channel calibration or modem result. The
appraisal executable must record the exact references, input tuples, seeds and
source identity it actually uses. Generator correctness, independent channel
validation, modem outcomes and receiver capacity remain different claims.

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
| **1–30 MHz skywave** | Begin with a stationary F.1487/Watterson comparison: each path's complex zero-mean diffuse gain and Gaussian Doppler spectrum use an explicitly stated **2σ** width; separate taps for materially separated delays or O/X modes. Add a time-varying centroid/drift, mode transition or fade only as a named episode. Keep arrival delay, mean shift, drift, spread, absorption and oscillator error distinct. | [ITU-R F.1487](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf) gives reproducible test profiles, not a worldwide channel distribution. [Fable 31](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/31-skywave-dispersion-midlatitude-nvis-longhaul.md) summarizes measured paths and estimator limits; the 2024 Java NVIS report, for example, is a one-week observation. | Watterson selected nearly stationary intervals, not all HF. Its Gaussian spectrum is not universal: references retain Lorentzian observations, diffuse within-mode delay, coupled delay/Doppler and nonstationarity. New peer-reviewed [Fernandes et al. (2026)](https://doi.org/10.3389/fspas.2026.1713968) analyze one 10 MHz WWV–K2MFF residual link and report daytime Cauchy versus nighttime exponential/log-normal best fits. These are *estimated frequency residuals on one link*, not a tap-gain spectrum, and the study did not measure channel amplitude/phase. Do not directly drive tap gains from those residuals. IRI-2020 ray tracing supplies large-scale path context, not the short-term residual law. | Check stationary tap covariance, delay profile and 2σ spectrum using the source's observable definitions. Then compare one same-mean constant-shift path against a measured/declared within-window drift trajectory; use session-held-out records if available. Keep spread-F/O-X/mode birth-death or absorption as separate hypotheses. A blackout is outage, not high-rate fading. |
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

External sources were checked 13 November 2026. This file is a concise decision
map, not an exhaustive literature review; detailed source passages, search
limits, arithmetic and alternatives remain in the linked research chapters.
